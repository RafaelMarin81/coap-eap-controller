// for mbed compatibility
#define failGracefully exit

// buffers for UDP and URIs
#define BUF_LEN 500
#define URI_BUF_LEN 32
#define MAXBUFLEN 100

#define ISSERVER 1
#define DEBUG 1

#ifdef __cplusplus
extern "C" {
#endif

#include "mainserver.h"
#include "lalarm.h"
#include "state_machines/statemachine.h"
#include "panautils.h"

	// Se supone que este se obtiene por panautils
#include "cmac.h"

#ifdef __cplusplus
}
#endif

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <semaphore.h>
#include "wpa_supplicant/src/common/defs.h"

//CantCoap
#include "cantcoap-master/cantcoap.h"
#include "wpa_supplicant/src/radius/radius.h"
#include "wpa_supplicant/src/radius/radius_client.h"
//#include "state_machines/coap_eap_session.h"
//#include "state_machines/coap_eap_session.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Just for testing
//#include <iostream>
//#include <sys/time.h>
//#include <AppKit/AppKit.h>

//using namespace std;

//Global variables
static bool fin = 1;
int global_sockfd = 0;
int sockfd  = 0;

int successes = 0;

/** Linked list of server's PANA sessions.*/
struct coap_eap_ctx_list* list_coap_eap_sessions = NULL;
struct coap_ctx_list* list_coap_ctx = NULL;

/** Mutex associated to PANA sessions' list.*/
pthread_mutex_t list_sessions_mutex;

/** Linked list of server's tasks.*/
struct task_list* list_tasks = NULL;
/** Last task. */
struct task_list* last_task = NULL;
/** Mutex associated to tasks' list. */
pthread_mutex_t list_tasks_mutex;

/** Alarm's list. */
struct lalarm_coap* list_alarms_coap_eap = NULL;


/** Semaphore used to wait for new tasks by workers. */
sem_t got_task; 

// Helper Functions 
void printf_hex(unsigned char *hex, unsigned int l){
    int i;
    if (hex != NULL){
        for (i=0; i < l; i++)
            printf("%02x",hex[i]);

        printf("\n");
    }
}



int split (const char *str, char c, char ***arr)
{
    int count = 1;
    int token_len = 1;
    int i = 0;
    char *p;
    char *t;

    p = (char *)str;
    while (*p != '\0')
    {
        if (*p == c)
            count++;
        p++;
    }

    *arr = (char**) malloc(sizeof(char*) * count);
    if (*arr == NULL)
        exit(1);

    p = (char *)str;
    while (*p != '\0')
    {
        if (*p == c)
        {
            (*arr)[i] = (char*) malloc( sizeof(char) * token_len );
            if ((*arr)[i] == NULL)
                exit(1);

            token_len = 0;
            i++;
        }
        p++;
        token_len++;
    }
    (*arr)[i] = (char*) malloc( sizeof(char) * token_len );
    if ((*arr)[i] == NULL)
        exit(1);

    i = 0;
    p = (char *)str;
    t = ((*arr)[i]);
    while (*p != '\0')
    {
        if (*p != c && *p != '\0')
        {
            *t = *p;
            t++;
        }
        else
        {
            *t = '\0';
            i++;
            t = ((*arr)[i]);
        }
        p++;
    }

    return count;
}



//> get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

unsigned short get_in_port(struct sockaddr *sa){
	if (sa->sa_family == AF_INET) {
		return (((struct sockaddr_in*)sa)->sin_port);
	}
	//printf("get_in_port: %d \n", ((struct sockaddr_in6*)sa)->sin6_port );

	return (((struct sockaddr_in6*)sa)->sin6_port);
}


unsigned int print_ipv6_address(char *output, struct in6_addr *v6addr){
	char 				pv6addr[INET6_ADDRSTRLEN];

	if(inet_ntop(AF_INET6, v6addr, pv6addr, sizeof(pv6addr)) == NULL){
		puts("inet_ntop(): Error converting IPv6 Source Address to presentation format");
		return 0;
	}

	sprintf(output, "coap://[%s]/auth", pv6addr);
	return 1;
}

struct network_task{
	char buf[BUF_LEN];
	int len;
	struct sockaddr_storage their_addr;	
	uint32_t session_id;
};


#if DEBUG
void printHexadecimal(CoapPDU *pdu){

    printf("PDU: (%d)\n",pdu->getTokenPointer());
    printf_hex(pdu->getPDUPointer(),pdu->getPDULength());

}
#endif

network_task* createNetworkTask( char *buf, int len, struct sockaddr_storage *their_addr){

	network_task *task = XMALLOC(network_task,1);

	memcpy(&(task->buf), buf, (size_t)len);
	(task->buf)[len] = '\0';
	
	task->len = len;
    memcpy(&(task->their_addr), their_addr, sizeof(struct sockaddr_storage));

	return task;
}

void setSessionID(struct network_task *task, uint32_t session_id){
	task->session_id = session_id;
}


void signal_handler(int sig) {
	printf("\nStopping server, signal: %d\n", sig);
	fin = 0;
}


#if DEBUG
void print_list_sessions(){
#ifdef DEBUG
//	struct pana_ctx_list* ptr = list_coap_eap_sessions;
    struct coap_eap_ctx_list* ptr = list_coap_eap_sessions;
	// lock the mutex, to assure exclusive access to the list
	pthread_mutex_lock(&list_sessions_mutex);

	while (ptr != NULL) {
		pana_debug("Showing session id: %#X", ptr->coap_eap_session->session_id);
		ptr = ptr->next;
	}

	// unlock mutex
	pthread_mutex_unlock(&list_sessions_mutex);
#endif
}
#endif

#if DEBUG
void printDebug(coap_eap_ctx * coap_eap_session){



	CoapPDU *response = new CoapPDU (coap_eap_session->lastReceivedMessage, BUF_LEN, BUF_LEN);
	response->setPDULength(coap_eap_session->lastReceivedMessage_len);

	if(response->validate() != 1)
	{
		printf("Error Parsing Message\n");
		exit(0);
	}

	struct timeval start;
	gettimeofday(&start, NULL);


	char s[INET6_ADDRSTRLEN];
	printf("DEBUG:::::\n MSGID: %d IP: %s TimeOfDay %ld\n", ntohs(response->getMessageID()),
			inet_ntop(((coap_eap_session)->recvAddr).ss_family,
					get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
					s, sizeof s),
			(start.tv_sec * 1000000 + start.tv_usec)
	);

}
#endif

// Task Functions
struct task_list* get_task() {
	int rc; /* return code of pthreads functions.  */

	struct task_list* task = NULL;

#if DEBUG
	pana_debug("Trying to get a task.");
#endif
	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_tasks_mutex);

	if (list_tasks != NULL) {
		task = list_tasks; 
		list_tasks = list_tasks->next;
		task->next = NULL;
	}


	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_tasks_mutex);

	/* return the task to the caller. */
	if (task == NULL) {
#if DEBUG
		pana_debug("Task not found");
#endif
		return NULL;
	}

	return task;
}

void add_task(task_function funcion, void * arg) {
	
	if(arg == NULL)
	{
		pana_error("ERROR: add_task: arg  == NULL ");
		exit(0);	
	}
	
	int rc; // return code of pthreads functions.
	// lock the mutex, to assure exclusive access to the list
	rc = pthread_mutex_lock(&list_tasks_mutex);

	struct task_list * new_element; // A new element in the list

	// create structure with new element
	new_element = XMALLOC(struct task_list,1);

	new_element->use_function = funcion;
	new_element->data = arg;
	new_element->next = NULL;


	/* add new session to the end of the list, updating list */
	/* pointers as required */
	if (list_tasks == NULL) { /* special case - list is empty */
		list_tasks = new_element;
		last_task = new_element;
	}
	else {
		last_task->next = new_element;
		last_task = last_task->next;
	}

#if DEBUG
	pana_debug("add_task: added task");
#endif
	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_tasks_mutex);
	/* signal the condition variable - there's a new task to handle */
	rc = sem_post(&got_task);
}

// Hash functions

int check_mac(unsigned char *key, unsigned char *mac, CoapPDU *pdu) {


	if(key == NULL || mac == NULL || pdu == NULL)
	{
			pana_error("ERROR: check_mac: ");
			if(key == NULL)
				pana_error("key == NULL ");
			if(mac == NULL)
				pana_error("mac == NULL ");
			if(pdu == NULL)
				pana_error("pdu == NULL ");
			if(pdu->getOptionPointer(CoapPDU::COAP_OPTION_AUTH) == NULL)
				pana_error("->getAUTHPointer() == NULL ");
		exit(0);	
	}

	unsigned char zero[16] = {0};
	unsigned char calculatedmac[16] = {0};

	memcpy(pdu->getOptionPointer(CoapPDU::COAP_OPTION_AUTH),&zero,16);


	int len = pdu->getPDULength();

	unsigned char *mypdu = (unsigned char *)malloc((pdu->getPDULength())*sizeof(unsigned char));
	memcpy(mypdu,pdu->getPDUPointer(),(size_t)len);

	AES_CMAC(key,pdu->getPDUPointer(),pdu->getPDULength(), (unsigned char *)&calculatedmac);

	if (memcmp(mac,calculatedmac,16)==0) return 1;
	return 0;
}

void override_auth_option_pdu(unsigned char *key, CoapPDU *pdu){
	if(key == NULL || pdu == NULL)
	{
			pana_error("ERROR: override_auth_option_pdu: ");
			if(key == NULL)
				pana_error("key == NULL ");
			if(pdu == NULL)
				pana_error("pdu == NULL ");
			if(pdu->getOptionPointer(CoapPDU::COAP_OPTION_AUTH) == NULL)
				pana_error("->getAUTHPointer() == NULL ");
        exit(0);
	}

    unsigned char macacero[16] = {0};

	AES_CMAC(key,pdu->getPDUPointer(),pdu->getPDULength(), (unsigned char *)&macacero);
	memcpy(pdu->getPDUPointer()+pdu->getPDULength()-21,&macacero,16);

}

// Retransmissions funtions

void storeLastSentMessageInSession(CoapPDU *pdu, coap_eap_ctx *coap_eap_session){
	
	if(coap_eap_session->lastSentMessage != NULL)
	{
		free((void *) coap_eap_session->lastSentMessage);
		coap_eap_session->lastSentMessage = NULL;
	}

	coap_eap_session->lastSentMessage = XMALLOC(uint8_t,pdu->getPDULength());
	memcpy(coap_eap_session->lastSentMessage,pdu->getPDUPointer() ,(size_t)pdu->getPDULength());
	coap_eap_session->lastSentMessage_len = pdu->getPDULength();
}

void storeLastReceivedMessageInSession(CoapPDU *pdu, coap_eap_ctx *coap_eap_session){
	
	if(coap_eap_session->lastReceivedMessage != NULL)
	{
		free((void *) coap_eap_session->lastReceivedMessage);
		coap_eap_session->lastReceivedMessage = NULL;
	}

#if DEBUG
	printf("Storing Last Received Message of length %d \n",pdu->getPDULength());
#endif
	coap_eap_session->lastReceivedMessage = XMALLOC(uint8_t,pdu->getPDULength());
	memcpy(coap_eap_session->lastReceivedMessage,pdu->getPDUPointer() ,(size_t)pdu->getPDULength());
	coap_eap_session->lastReceivedMessage_len = pdu->getPDULength();
	
}

void* process_receive_radius_msg(void* arg) {

	if(arg == NULL)
	{
		pana_error("ERROR: process_receive_radius_msg: arg  == NULL ");
		exit(0);	
	}

	#if DEBUG
	pana_debug("\n??\n"
			   "##\n"
				"######## ENTER: process_receive_radius_msg \n");
	#endif

	struct radius_func_parameter radius_params = *((struct radius_func_parameter*) arg);
	int radius_type = RADIUS_AUTH;

	//Get the function's parameters.
	struct radius_ms_radiug *radmsg = (struct radius_ms_radiug *)radius_params.msg;

	// Get the information about the new message received
	struct radius_client_data *radius_data = get_rad_client_ctx();
	struct radius_hdr *hdr = radius_msg_get_hdr((struct radius_msg *)radmsg);
	struct eap_auth_ctx *eap_ctx = search_eap_ctx_rad_client(hdr->identifier);

	if (eap_ctx == NULL){
		pana_error("eap_ctx NULL. It can't be used");
		return NULL;
	}


	coap_eap_ctx * coap_eap_session = (coap_eap_ctx*) (eap_ctx->eap_ll_ctx);
	pthread_mutex_lock(&(coap_eap_session->mutex));

#if DEBUG
	printDebug(coap_eap_session);
#endif



	//Delete the alarm associated to this message
	//pana_debug("Getting an alarm session in radius function with id: %x\n", coap_eap_session->session_id);
	//get_alarm_coap_eap_session(coap_eap_session->list_of_alarms, coap_eap_session->session_id, RETR_AAA);

//	if (eap_ctx != NULL) {

		radius_client_receive((struct radius_msg *)radmsg, radius_data, &radius_type);

		// In case of a EAP Fail is produced.
		if ((eap_auth_get_eapFail(eap_ctx) == TRUE)){
			pana_debug("Error: There's an eap fail in RADIUS");
			exit(0);
			//transition((coap_eap_ctx *) eap_ctx->eap_ll_ctx); // Check: has to be uncommented?
		}
		
		if ((eap_auth_get_eapReq(eap_ctx) == TRUE) || (eap_auth_get_eapSuccess(eap_ctx) == TRUE)) {
#if DEBUG
			pana_debug("There's an eap request in RADIUS");
			pana_debug("Trying to make a transition with the message from RADIUS");
#endif
			struct wpabuf * packet = eap_auth_get_eapReqData(&(coap_eap_session->eap_ctx));


			socklen_t addrLen = sizeof(struct sockaddr_in);
			if((&coap_eap_session->recvAddr)->ss_family==AF_INET6) {
				addrLen = sizeof(struct sockaddr_in6);
			}	
			
			coap_eap_session->message_id += 1;
			
			// FIXME: Orden de creaci??n, secuencia, y location path din??mico
			CoapPDU *response = new CoapPDU();
			response->setVersion(1);
			response->setMessageID(coap_eap_session->message_id);
			response->setToken((uint8_t *)&coap_eap_session->session_id,4);
// Cambiar a POST cuando se actualice la especificacion
			response->setCode(CoapPDU::COAP_POST);
//			response->setCode(CoapPDU::COAP_PUT);
			response->setType(CoapPDU::COAP_CONFIRMABLE);
/*
			char responseURI[10] = {0};
			strcpy(responseURI,"/boot/");
			strcat(responseURI,(const char *)&coap_eap_session->location);

			printf("\nResponseURI: %s\n", responseURI);
*/
			//response->setURI((char*)responseURI,strlen(responseURI));
			response->setURI(coap_eap_session->location, strlen(coap_eap_session->location));


			if (eap_auth_get_eapKeyAvailable(&(coap_eap_session->eap_ctx)))
			{
				size_t key_len = 0;
				u8 *key = eap_auth_get_eapKeyData(&(coap_eap_session->eap_ctx),(size_t *)&(coap_eap_session->key_len));

                //coap_eap_session->msk_key = XCALLOC(u8 , (coap_eap_session->key_len) );
                coap_eap_session->msk_key = XMALLOC(u8 , (coap_eap_session->key_len) );

                memcpy(coap_eap_session->msk_key,key,coap_eap_session->key_len);
				/****This is used for deriving AUTH_KEY****/			
				unsigned int seq_length = 
(14*sizeof(unsigned char))+(3*sizeof(unsigned int));			
//(3*sizeof(unsigned int));			

				unsigned char *sequence = XMALLOC(unsigned char, seq_length);
				unsigned char *ptr = sequence;

				coap_eap_session->auth_key = XMALLOC(u8,16);
				unsigned char label[] ="IETF COAP AUTH"; 
				memcpy(ptr,label,sizeof(unsigned char)*14);
				ptr += 14;

	
				memcpy(ptr,&(coap_eap_session->session_id),sizeof(unsigned int));
				ptr += 4;

				memcpy(ptr, &(coap_eap_session->nonce_c),sizeof(unsigned int));

				ptr += 4;			
				memcpy(ptr, &(coap_eap_session->nonce_s),sizeof(unsigned int));

//#if DEBUG
				printf("SEQUENCE: \n");
				printf_hex(sequence, seq_length);
//#endif

				AES_CMAC (coap_eap_session->msk_key, sequence, seq_length,
                          coap_eap_session->auth_key);
				XFREE(sequence);
				/*****  END AUTH KEY ****************/
				coap_eap_session->key_len= (uint16_t)key_len;
//#if DEBUG
				printf("Key_len = %d",(uint16_t) key_len);

				printf("AUTH_KEY: \n");
				printf_hex(coap_eap_session->auth_key,16);
//#endif

				uint8_t *zero = XMALLOC(uint8_t,16);
				memset(zero,0,16);
				response->addOption(CoapPDU::COAP_OPTION_AUTH,16,zero);

				coap_eap_session->CURRENT_STATE = 3;
#if DEBUG
				printf("Cambio a estado 3 message_id: %d, session_id: %X\n",coap_eap_session->message_id,coap_eap_session->session_id);
#endif
			}
#if DEBUG
			else{
				printf("message_id: %d\n",coap_eap_session->message_id);
				printf("NO Cambio a estado 3 message_id: %d, session_id: %X\n",coap_eap_session->message_id,coap_eap_session->session_id);
			}
#endif


			response->setPayload((unsigned char *)wpabuf_head(packet),(int)wpabuf_len(packet));
			if ((coap_eap_session->msk_key != NULL) && (coap_eap_session->auth_key != NULL))
				override_auth_option_pdu(coap_eap_session->auth_key,response);

			//printf("\n*****************Sending PUT /auth with EAP***************\n");

#if DEBUG
			if((coap_eap_session->msk_key != NULL))
			{
				printf("MSK KEY:\n");
				printf_hex(coap_eap_session->msk_key,16);

			}



			// Send new coap Message
			printf("PDU TO SEND: \n");
			//response->printHuman();s
            printHexadecimal(response);
#endif

			ssize_t sent = sendto(
					global_sockfd,
					response->getPDUPointer(),
                    (size_t)response->getPDULength(),
					0,
					(sockaddr *)&coap_eap_session->recvAddr,
					addrLen
					);
			if(sent<0) {
				DBG("Error sending packet: %ld.",sent);
				perror(NULL);
				return NULL;
			}

			storeLastSentMessageInSession(response,coap_eap_session);

			get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
			coap_eap_session->RT = coap_eap_session->RT_INIT;
			coap_eap_session->RTX_COUNTER = 0;
			add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);
			#if DEBUG
			char s[INET6_ADDRSTRLEN];
						printf("Alarma a??andida:::::::\n  MSGID: %d IP: %s\n", ntohs(response->getMessageID()),
								inet_ntop(((coap_eap_session)->recvAddr).ss_family,
										get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
										s, sizeof s)
						);
			#endif

		}

#if DEBUG
	printDebug(coap_eap_session);
#endif

	pthread_mutex_unlock(&(coap_eap_session->mutex));

#if DEBUG
	pana_debug("######## SALIMOS DE : process_receive_radius_msg \n"
			"##\n"
			"??\n"
	);
#endif
    //free(&radius_params);
	return NULL;

}

void coapRetransmitLastSentMessage(coap_eap_ctx * coap_eap_session){

	if(coap_eap_session == NULL)
	{
		pana_error("ERROR: coapRetransmit: coap_eap_session  == NULL ");
		exit(0);	
	}

#if DEBUG
	pana_debug(	"\n??\n"
			    "##\n"
				"######## ENTRAMOS EN: coapRetransmitLastSentMessage \n");
	printDebug(coap_eap_session);
#endif

	CoapPDU *response = new CoapPDU(coap_eap_session->lastSentMessage, BUF_LEN, BUF_LEN);
	response->setPDULength(coap_eap_session->lastSentMessage_len);

	if(response->validate() != 1){
			printf("Malformed CoapPDU \n");
			exit(0);
	}
	
	//response->printHuman();

	//struct sockaddr_storage * recvFrom = coap_eap_session->recvAddr;
	int sockfd = global_sockfd;
	
	socklen_t addrLen = sizeof(struct sockaddr_in);
	if( (&coap_eap_session->recvAddr)->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}


	coap_eap_session->RT=(coap_eap_session->RT*2);
	get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
	add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

	ssize_t sent = sendto(
			sockfd,
			response->getPDUPointer(),
            (size_t) response->getPDULength(),
			0,
			(sockaddr *)&coap_eap_session->recvAddr,
			addrLen
			);
	if(sent<0) {
		DBG("Error sending packet: %ld.",sent);
		perror(NULL);

	}


	delete response;


#if DEBUG
	printDebug(coap_eap_session);
	pana_debug("######## SALIMOS DE: coapRetransmitLastSentMessage \n"
			"##\n"
			"??\n"
	);
#endif
	
}


void remove_coap_eap_session(uint32_t id) {
	int rc;

	struct coap_eap_ctx_list* session = NULL;
	struct coap_eap_ctx_list* anterior = NULL;

	pana_debug("Trying to delete session with id: %d", ntohl(id));
	// lock the mutex, to assure exclusive access to the list 
	rc = pthread_mutex_lock(&list_sessions_mutex);

	if (list_coap_eap_sessions != NULL) {
		session = list_coap_eap_sessions;
		//If the session is the first
		if (session->coap_eap_session->session_id == id) {
			pana_debug("Found and deleted session with id: %d", ntohl(id));
			list_coap_eap_sessions = list_coap_eap_sessions->next;
			session->next=NULL;
			//XFREE(session); //fixme: Cuidado al poner este free. Hay que verlo con el de remove_alarm (lalarm.c)
			rc = pthread_mutex_unlock(&list_sessions_mutex);
			return;
		}
		session = list_coap_eap_sessions->next;
		anterior = list_coap_eap_sessions;
		while (session != NULL) {
			if (session->coap_eap_session->session_id == id) {
				anterior->next = session->next;
				session->next = NULL;
				//XFREE(session); //fixme: Cuidado al poner este free. Hay que verlo con el de remove_alarm (lalarm.c)
				break;
			}
			anterior = anterior->next;
			session = session->next;
		}
	}

	// unlock mutex 
	rc = pthread_mutex_unlock(&list_sessions_mutex);

}



void* process_retr_coap_eap(void *arg){

	if(arg == NULL)
	{
		pana_error("ERROR: process_retr_coap_eap: arg  == NULL ");
		exit(0);	
	}

#if DEBUG
	pana_debug(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: process_retr_coap_eap \n");
#endif

	struct retr_coap_func_parameter* retr_params;


	// Get the function's parameters.
	retr_params = (struct retr_coap_func_parameter*) arg;
	int alarm_id = retr_params->id;
	coap_eap_ctx * coap_eap_session = retr_params->session;
	pthread_mutex_lock(&(coap_eap_session->mutex));

#if DEBUG
	printDebug(coap_eap_session);
#endif

	// Depends on the alarm produced, it is processed.
	if (alarm_id == POST_ALARM) {

		#if DEBUG
		printf("Processing POST_AUTH alarm ocurred %d\n", coap_eap_session->session_id);
		#endif

		//get_alarm_coap_eap_session(coap_eap_session->list_of_alarms, coap_eap_session->session_id, POST_ALARM);
		get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
		coap_eap_session->RTX_COUNTER++;
		if (coap_eap_session->RTX_COUNTER < MAX_RETRANSMIT) {

			if (coap_eap_session->lastSentMessage != NULL)
			{
#if DEBUG
				printf("Retransmiting %f\n",coap_eap_session->RT);
#endif
				coapRetransmitLastSentMessage(coap_eap_session);
            }
			/*else
				printf("No message for retransmitting\n");
			*/
		}
		else {
			printf("Timeout %d\n",coap_eap_session->session_id);
			int session = coap_eap_session->session_id;
			remove_coap_eap_session(session);
			remove_coap_eap_session(session);
			//XFREE(coap_eap_session); //Rafa: Watch out!!!
		}
		
	} 
	else if (alarm_id == SESS_ALARM)
	{
		printf("Session expired\n");
	}	
	else {
		pana_debug("An UNKNOWN alarm ocurred");
	}


#if DEBUG
	printDebug(coap_eap_session);
#endif

	pthread_mutex_unlock(&(coap_eap_session->mutex));

#if DEBUG
	pana_debug("######## SALIMOS DE: process_retr_coap_eap \n"
			"##\n"
			"??\n"
	);
#endif
	return NULL;
}

// Coap EAP Session Functions
void add_coap_eap_session(coap_eap_ctx * session) {
	
	if(session == NULL)
	{
		pana_error("ERROR: add_coap_eap_session: session  == NULL ");
		exit(0);	
	}
	
	int rc; //> return code of pthreads functions.  

	struct coap_eap_ctx_list * new_element;

	/* create structure with new request */
	new_element = XMALLOC(struct coap_eap_ctx_list,1);
	new_element->coap_eap_session = session;
	new_element->next = NULL;

	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_sessions_mutex);

	/* add new session to the end of the list, updating list */
	/* pointers as required */
	if (list_coap_eap_sessions == NULL) { /* special case - list is empty */
		list_coap_eap_sessions = new_element;
	} 
	else {
		new_element->next = list_coap_eap_sessions;
		list_coap_eap_sessions = new_element;
	}

	pana_debug("add_session: added CoAP EAP session: %X",ntohl(new_element->coap_eap_session->session_id));
	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_sessions_mutex);

}

coap_eap_ctx* get_coap_eap_session(uint32_t id) {
	int rc; /* return code of pthreads functions.  */

	struct coap_eap_ctx_list* session = NULL;

//	pana_debug("Trying to get session of id: %X", htonl(id));
	/* lock the mutex, to assure exclusive access to the list */
	rc = pthread_mutex_lock(&list_sessions_mutex);

	if (list_coap_eap_sessions != NULL) {
		session = list_coap_eap_sessions;
		while (session != NULL) {
			if (session->coap_eap_session->session_id == id) break;
			session = session->next;
		}
	}

	/* unlock mutex */
	rc = pthread_mutex_unlock(&list_sessions_mutex);

	/* return the session to the caller. */
	if (session == NULL) {
		pana_debug("Session not found, id: %d", ntohl(id));
		exit(0);	
	}
	return session->coap_eap_session;
}


// Handler Funtions

void * handle_worker(void* data) {
	
	if(data == NULL)
	{
		pana_error("ERROR: handle_worker: data  == NULL ");
		exit(0);	
	}

#if DEBUG
	printf(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: handle_worker\n");
#endif

	int thread_id = *((int*) data); /* thread identifying number */
	int rc; /* return code of pthreads functions.  */
	struct task_list* a_task = NULL; /* pointer to a task. */

#if DEBUG
	pana_debug("thread '%d' as worker manager", thread_id);
	pana_debug("Starting thread '%d'", thread_id);
#endif

	/* lock the mutex, to access the requests list exclusively. */
	sem_wait(&got_task);

	/* do forever.... */
	while (fin) {

#if DEBUG
		pana_debug("thread '%d' tries to get a task", thread_id);
#endif

		if (list_tasks != NULL) { /* a request is pending */
			a_task = get_task();
		}

		if (a_task) {
			//printf("Hay task!");
			a_task->use_function(a_task->data);
		}
		rc = sem_wait(&got_task);
		/* and after we return from pthread_cond_wait, the mutex  */
		/* is locked again, so we don't need to lock it ourselves */
	}

#if DEBUG
		printf("######## SALIMOS DE: handle_worker\n"
		"##"
		"??");
#endif

	return NULL;
}


void* process_coap_msg(void *arg){
	if(arg == NULL)
	{
		pana_error("ERROR: process_coap_msg: arg  == NULL ");
		exit(0);	
	}

#if DEBUG
	printf("@"
			"##"
			"######## ENTRAMOS EN: process_coap_msg\n");
#endif

	struct network_task *mytask = (struct network_task *)arg;


	//printf_hex((unsigned char *)mytask->buf, (unsigned int) mytask->len);
	
	CoapPDU *request = new CoapPDU((uint8_t *)mytask->buf, BUF_LEN, BUF_LEN);
	//printf("The PDU Lenght is %d \n",mytask->len);
	request->setPDULength(mytask->len);

	if(request->validate() != 1){
		printf("Malformed CoapPDU \n");
		exit(0);
	}

	//request->printHuman();
	
	struct sockaddr_storage * recvFrom = &(mytask->their_addr);
	int sockfd = global_sockfd;
	uint32_t session_id = mytask->session_id;
	
	
	coap_eap_ctx *coap_eap_session = NULL;

#if DEBUG
	printf("Session ID: %x \n", session_id);
#endif

	coap_eap_session = get_coap_eap_session(session_id);

	if(coap_eap_session == NULL )
	{	
		printf("Error getting coap_eap_session\n");
		exit(0);		
	}
	
	int rc =  pthread_mutex_lock(&(coap_eap_session->mutex));

#if DEBUG
	printf("######## IN PROCESS RECEVIE COAP\n");
	//request->printHuman();
	printDebug(coap_eap_session);
#endif

	socklen_t addrLen = sizeof(struct sockaddr_in);
	if(recvFrom->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}


	//  prepare next message, a POST
	CoapPDU *pdu = new CoapPDU();
	pdu->setVersion(1);
	pdu->setType(CoapPDU::COAP_CONFIRMABLE);
	pdu->setCode(CoapPDU::COAP_POST);
	pdu->setToken((uint8_t*)&coap_eap_session->session_id,sizeof(uint32_t));
	pdu->setMessageID(coap_eap_session->message_id );
	pdu->setURI((char*)"boot",4);

	coap_eap_session->nonce_c = (unsigned int)rand();
	printf_hex((uint8_t *)&coap_eap_session->nonce_c,4);
	pdu->setPayload((uint8_t*)&coap_eap_session->nonce_c,4);
	coap_eap_session->CURRENT_STATE = 1;

	storeLastReceivedMessageInSession(request,coap_eap_session);
	storeLastSentMessageInSession(pdu,coap_eap_session);




	get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
	coap_eap_session->RTX_COUNTER = 0;
	coap_eap_session->RT = coap_eap_session->RT_INIT;
	add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

	#if DEBUG
	char s[INET6_ADDRSTRLEN];
	printf("Alarma a??andida:::::::\n  MSGID: %d IP: %s\n", ntohs(pdu->getMessageID()),
			inet_ntop(((coap_eap_session)->recvAddr).ss_family,
					get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
					s, sizeof s)
	);
	#endif



    //rc =  pthread_mutex_unlock(&(coap_eap_session->mutex));
	// send the packet
/*
	printf("SENDING GET ACK\n");
    printHexadecimal(response);
	//response->printHuman();
*/
/*
	ssize_t sent = sendto(
			sockfd,
			response->getPDUPointer(),
            (size_t)response->getPDULength(),
			0,
			(sockaddr*)recvFrom,
			addrLen
			);
	if(sent<0) {
		DBG("Error sending packet: %ld.",sent);
		perror(NULL);
		return NULL;
	}
*/
#if DEBUG
	printf("SENDING POST\n");
    printHexadecimal(pdu);
#endif

    ssize_t sent2 = sendto(
			sockfd,
			pdu->getPDUPointer(),
            (size_t) pdu->getPDULength(),
			0,
			(sockaddr*)recvFrom,
			addrLen
		     );
	if(sent2<0) {
		DBG("Error sending packet: %ld.",sent);
		perror(NULL);
		//return NULL;
	}


	delete request;
	//delete response;
	delete pdu;
	delete mytask;

#if DEBUG
	printDebug(coap_eap_session);
#endif

	rc =  pthread_mutex_unlock(&(coap_eap_session->mutex));

#if DEBUG
    printf("######## SALIMOS DE: process_coap_msg\n"
			"##\n"
			"??\n"
	);
#endif
	return 0;

}


void* process_acknowledgment(void * arg){

	if(arg == NULL)
	{
		pana_error("ERROR: process_acknowledgment: arg  == NULL ");
		exit(0);	
	}


#if DEBUG
	printf(	"\n??\n"
			"##\n"
			"######## ENTRAMOS EN: process_acknowledgment\n");
#endif

	struct network_task *mytask = (struct network_task *)arg;

	CoapPDU *request = new CoapPDU((uint8_t *)mytask->buf, BUF_LEN, BUF_LEN);
	request->setPDULength(mytask->len);
	
	if(request->validate() != 1){
			printf("Error: PDU invalido \n");
			exit(0);
	}

	
	struct sockaddr_storage * recvFrom = &mytask->their_addr;
	int sockfd = global_sockfd;

	coap_eap_ctx *coap_eap_session=NULL;
	uint32_t session_id;
	memcpy(&session_id, request->getTokenPointer(), (size_t) request->getTokenLength());
	coap_eap_session = get_coap_eap_session(session_id);
	
	
	if(coap_eap_session == NULL )
	{	
		printf("Error getting coap_eap_session\n");
		exit(0);		
	}
	
	int rc = pthread_mutex_lock(&(coap_eap_session->mutex));

#if DEBUG
	printf("######## PROCESSING... ACKNOWLEDGMENT\n");
	printDebug(coap_eap_session);
#endif


	//request->printHuman();

	socklen_t addrLen = sizeof(struct sockaddr_in);
	if(recvFrom->ss_family==AF_INET6) {
		addrLen = sizeof(struct sockaddr_in6);
	}

	char URI[10] = {0};
	int URI_len;
    char ** split_uri = NULL;
    int split_uri_len;

	CoapPDU *response = new CoapPDU();

	struct wpabuf * packet;
    unsigned char *lpath = NULL;
	ssize_t sent;
	uint8_t *dst;

	bool auth_option_present=FALSE, finish = FALSE;
	unsigned char mac[16] = {0};

	int session;

	//char responseURI[10] = {0};
	coap_eap_session->location = (char *) malloc(10*sizeof(char));
	memset(coap_eap_session->location,0,10);

	switch(coap_eap_session->CURRENT_STATE) {

		case 1:


			request->getURI(coap_eap_session->location,10,&URI_len);
			printf("==============");
			printf("\nURI PATH(%d): %s \n",URI_len, coap_eap_session->location);
			printf("==============");
/*
			//split_uri = str_split(URI,'/');

			split_uri_len = split(URI, '/', &split_uri);

			printf("\nURI 0: %s\n",split_uri[0]);
			printf("\nURI 1: %s\n",split_uri[1]);
			printf("\nURI 2: %s\n",split_uri[2]);
*/
			coap_eap_session->message_id += 1;
			response->setVersion(1);
			response->setMessageID(coap_eap_session->message_id);
			response->setToken(request->getTokenPointer(),(uint8_t)request->getTokenLength());

			// Se env??a el primer POST
			memcpy(&coap_eap_session->nonce_s,request->getPayloadPointer(), (size_t) request->getPayloadLength());

			#if DEBUG
			printf("nonce_s: %x \n", coap_eap_session->nonce_s);
			#endif

/*			coap_eap_session->location = *split_uri[2];

			strcpy(responseURI,"/boot/");
			responseURI[6]=coap_eap_session->location;
			memset(&responseURI[7],0,3);
			//strcat(responseURI,(const char*)&coap_eap_session->location);
*/


			//printf("\nResponseURI: %s\n", responseURI);

			response->setURI(coap_eap_session->location,strlen(coap_eap_session->location));



			// Nuevo Estado
			coap_eap_session->CURRENT_STATE = 2; // To send PUT;

			// Empezamos con el tratamiento EAP, enviamos el primer put
			eap_auth_set_eapRestart(&(coap_eap_session->eap_ctx), TRUE);
			eap_auth_step(&(coap_eap_session->eap_ctx));
			packet = eap_auth_get_eapReqData(&(coap_eap_session->eap_ctx));


// Cambiar a POST cuando se actualize la especificacion
			response->setCode(CoapPDU::COAP_POST);
//			response->setCode(CoapPDU::COAP_PUT);
			// Enviamos PUT
			response->setType(CoapPDU::COAP_CONFIRMABLE);
			response->setPayload((uint8_t *)wpabuf_head(packet),(uint8_t) wpabuf_len(packet));

			#if DEBUG
				printf("WPABUF_LEN %d \n", (int)wpabuf_len(packet));
				printf_hex((uint8_t *)wpabuf_head(packet), (int)wpabuf_len(packet));
			#endif

			storeLastReceivedMessageInSession(request,coap_eap_session);
			storeLastSentMessageInSession(response,coap_eap_session);


#if DEBUG
			printf("SENDING PUT\n");
			//response->printHuman();
            printHexadecimal(response);
#endif

            sent = sendto(
					sockfd,
					response->getPDUPointer(),
                    (size_t)response->getPDULength(),
					0,
					(sockaddr*)recvFrom,
					addrLen
				     );
			if(sent<0) {
				DBG("Error sending packet: %ld.",sent);
				perror(NULL);
				//return NULL;
			}


			get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
			coap_eap_session->RT = coap_eap_session->RT_INIT;
			coap_eap_session->RTX_COUNTER = 0;
			add_alarm_coap_eap(&(list_alarms_coap_eap),coap_eap_session,coap_eap_session->RT,POST_ALARM);

			#if DEBUG
			char s[INET6_ADDRSTRLEN];
			printf("Alarma a??andida:::::::\n MSGID: %d IP: %s\n", ntohs(response->getMessageID()),
					inet_ntop(((coap_eap_session)->recvAddr).ss_family,
							get_in_addr((struct sockaddr *)&(coap_eap_session)->recvAddr),
							s, sizeof s)
			);
			#endif

			break;

		case 2:
			#if DEBUG
			printf("Entramos en el estado 2\n");
			#endif

			if(request->getOptionPointer(CoapPDU::COAP_OPTION_AUTH) != NULL){
				printf("Error: estado 2, auth option presente \n");
				exit(0);
			}


			//get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);

			eap_auth_set_eapResp(&(coap_eap_session->eap_ctx), TRUE);
			eap_auth_set_eapRespData(&(coap_eap_session->eap_ctx), request->getPayloadPointer(), 
                    (size_t)request->getPayloadLength());
			eap_auth_step(&(coap_eap_session->eap_ctx));




			break;

		case 3:
			#if DEBUG
			printf("Entramos en el estado 3\n");
			printf("Final Binding.Checking AUTH OPTION\n");
			#endif

			auth_option_present = TRUE;
			dst = request->getPDUPointer();
			get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);


			memcpy(mac, &dst[request->getPDULength()-16], 16);

			if (auth_option_present)
			{ 	
				if (check_mac(coap_eap_session->auth_key,mac,request))
				{
					printf("MAC result: verificada correctamente %f!!!!!\n",coap_eap_session->RT);
					coap_eap_session->CURRENT_STATE = 4;
					successes++;
					printf("Successes: %d \n",successes);
					//if(successes == 10)
					//	exit(0);

				}
				else { 
					printf("MAC result:  fail\n");
				}
				int session = coap_eap_session->session_id;
				get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
				remove_coap_eap_session(session);
				remove_coap_eap_session(session);

				//remove_coap_eap_session(coap_eap_session->session_id);

				//session = coap_eap_session->session_id;
				//finish = TRUE;
			}else {
				printf("AUTH option is not present!!!!\n");
				int session = coap_eap_session->session_id;
				get_alarm_coap_eap_session(coap_eap_session->list_of_alarms, coap_eap_session->session_id, POST_ALARM);
				remove_coap_eap_session(session);				
				remove_coap_eap_session(session);
				//remove_coap_eap_session(coap_eap_session->session_id);			
			}
//			system("killall radiusd");


			break;

		default:
			break;

	}
	
	delete request;
	delete response;

#if DEBUG
//	printDebug(coap_eap_session);
#endif

	rc = pthread_mutex_unlock(&(coap_eap_session->mutex));

/* wtf!

	if(finish == TRUE)
	{
		remove_coap_eap_session(session);
	}
*/
#if DEBUG
	printf("######## SALIMOS DE: process_acknowledgment\n"
			"##\n"
			"??\n"
	);
#endif

}

int get_coap_address(	void *their_addr, unsigned short port) {
    int rc; /* return code of pthreads functions.  */

    struct coap_eap_ctx_list* session = NULL;
    socklen_t addrLen = sizeof(struct sockaddr_in);

    // pana_debug("Trying to get session of id: %d", id);
    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(&list_sessions_mutex);

    if (list_coap_eap_sessions != NULL) {
        session = list_coap_eap_sessions;
        while (session != NULL) {
            
			pana_debug("Checking address: ");
            printf_hex((unsigned char *)their_addr, (size_t)addrLen);
			printf("with port: %d\n", port);

            pana_debug("Checking ...: ");
            printf_hex((unsigned char *)
                    get_in_addr((struct sockaddr *) &(session->coap_eap_session)->recvAddr), (size_t)addrLen);
			unsigned short  portChecking = get_in_port((struct sockaddr *) &(session->coap_eap_session)->recvAddr);
			printf("with port: %d\n", portChecking);

			if (memcmp(
					get_in_addr((struct sockaddr *) &(session->coap_eap_session)->recvAddr)
					, their_addr, (size_t) addrLen) == 0){

				if(port == portChecking ){
					printf("Found!\n");
					break;
				}

//				break;

			}
            session = session->next;
        }
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(&list_sessions_mutex);

    /* return the session to the caller. */
    if (session == NULL) {
        pana_debug("Address not found");
        return FALSE;
    }
    return TRUE;
}




void * handle_network_management(void *data) {

#define MYPORT "5683"

	// the port users will be connecting to

	coap_eap_ctx *new_coap_eap_session = NULL;

	//To handle exit signals
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);

	fd_set mreadset; // master read set

	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	int ret;

	struct sockaddr_storage their_addr;
	char buf[BUF_LEN];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];


	char uriBuffer[URI_BUF_LEN];
	int recvURILen = 0;

	char * ipaddr = "aaaa::1";

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	// if ((rv = getaddrinfo(ipaddr, MYPORT, &hints, &servinfo)) != 0) {
	if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return NULL;
	}
	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((global_sockfd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}
		if (bind(global_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(global_sockfd);
			perror("listener: bind");
			continue;
		}
		break; 
	}
	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
		return NULL;
	}

	// socket para enviar

	memset(&hints, 0, sizeof hints);
	hints.ai_family 		= AF_INET6;
	hints.ai_socktype 		= SOCK_DGRAM;

	if ((rv = getaddrinfo(NULL, "8000", &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return NULL;
	}

	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) 
		{
			perror("talker: socket");
			continue;
		}
		break; 
	}

	if (p == NULL) {
		fprintf(stderr, "talker: failed to bind socket\n");
		return NULL;
	}



	// Radius
	int radius_sock=0; //Init it to a non-valid value

	struct sockaddr_in sa;
	struct sockaddr_in6 sa6;

	rad_client_init(AS_IP, AS_PORT, AS_SECRET);

	struct radius_client_data *radius_data = get_rad_client_ctx();

	if (radius_data != NULL) {
		if (IP_VERSION_AUTH==4)
			radius_sock = radius_data->auth_serv_sock;
		else if (IP_VERSION_AUTH==6)
			radius_sock = radius_data->auth_serv_sock6;
	}

	u8 udp_packet[MAX_DATA_LEN];
    struct sockaddr_in eap_ll_dst_addr, radius_dst_addr;
	struct sockaddr_in6 eap_ll_dst_addr6, radius_dst_addr6; //For ipv6 support
	int addr_size;

	//struct pana_func_parameter *pana_params;
	struct radius_func_parameter *radius_params;
	pana *msg;
	int length;



	while(fin){

		FD_ZERO(&mreadset);
		FD_SET(global_sockfd, &mreadset);
		FD_SET(radius_sock, &mreadset);
		
		// -- 
		sigset_t emptyset, blockset;

        sigemptyset(&blockset);         /* Block SIGINT */
        sigaddset(&blockset, SIGINT);
        sigprocmask(SIG_BLOCK, &blockset, NULL);

        /* Initialize nfds and readfds, and perhaps do other work here */
        /* Unblock signal, then wait for signal or ready file descriptor */

        sigemptyset(&emptyset);
        int retSelect  = pselect(FD_SETSIZE, &mreadset, NULL, NULL, NULL, &emptyset);

		//int retSelect = select(FD_SETSIZE,&mreadset,NULL,NULL,NULL);

		if(retSelect>0){


			if (FD_ISSET(radius_sock, &mreadset)) 
			{
#if DEBUG
				printf( "\n??\n"
						"##\n"
					"######## MENSAJE RADIUS RECIBIDO\n");
#endif

                length = 0;

				if (IP_VERSION==4){
					addr_size = sizeof (radius_dst_addr);
					length = (int) recvfrom(radius_sock, udp_packet, sizeof (udp_packet), 0, (struct sockaddr *) &(radius_dst_addr), (socklen_t *)&(addr_size));
				}
				else if (IP_VERSION==6){
					addr_size = sizeof (radius_dst_addr6);
					length = (int) recvfrom(radius_sock, udp_packet, sizeof (udp_packet), 0, (struct sockaddr *) &(radius_dst_addr6), (socklen_t *)&(addr_size));
				}
				if (length > 0) 
				{

//					radius_params = XCALLOC(struct radius_func_parameter,1);
                    radius_params = XMALLOC(struct radius_func_parameter,1);

					struct radius_msg *radmsg = radius_msg_parse(udp_packet, (size_t)length);
					radius_params->msg = (struct radius_msg *)XMALLOC ( char,length);
					memcpy(radius_params->msg, radmsg, (size_t)length);
					add_task(process_receive_radius_msg, radius_params);                    

				}
				else
					pana_error("recvfrom returned ret=%d, errno=%d", length, errno);

#if DEBUG
				printf("######## FIN PROCESAMIENTO MENSAJE RADIUS\n"
						"##\n"
						"??\n"
				);
#endif
			}




			// CoAP Traffic
			if(FD_ISSET(global_sockfd,&mreadset)){



				CoapPDU *recvPDU = new CoapPDU((uint8_t*)buf,BUF_LEN,BUF_LEN);

				addr_len = sizeof their_addr;
				if ((numbytes = (int) recvfrom(global_sockfd, buf, MAXBUFLEN-1 , 0,
								(struct sockaddr *)&their_addr, &addr_len)) == -1) {
					perror("recvfrom");
					exit(1);
				}
#if DEBUG

				printf(	"\n??\n"
						"##\n"
						"######## MENSAJE RECIBIDO\n");
				printf("listener: got packet from %s\n",
						inet_ntop(their_addr.ss_family,
							get_in_addr((struct sockaddr *)&their_addr),
							s, sizeof s));
				printf("port: %d\n", get_in_port((struct sockaddr *)&their_addr) );
				//printf("    sin6_port    : %d\n", ntohs(sin6->sin6_port));


				printf("listener: packet is %d bytes long\n", numbytes);
#endif
				buf[numbytes] = '\0';
#if DEBUG
				printf("listener: packet contains \"%s\"\n", buf);
#endif

				// validate packet
				if(numbytes>BUF_LEN) {
					INFO("PDU too large to fit in pre-allocated buffer");
					continue;
				}
				
				recvPDU->setPDULength(numbytes);
				if(recvPDU->validate()!=1) {
					INFO("Malformed CoAP packet");
					continue;
				}

#if DEBUG
				INFO("Message recibido \n");
				//recvPDU->printHuman();
                printHexadecimal(recvPDU);
#endif

				network_task * new_task  = createNetworkTask(buf, numbytes, &their_addr);


				if(recvPDU->getType() != CoapPDU::COAP_ACKNOWLEDGEMENT) {
#if DEBUG
					printf("######## GET RECIBIDO\n");
#endif
/*
					if(get_coap_address(get_in_addr((struct sockaddr *)&their_addr) , get_in_port((struct sockaddr *)&their_addr)) != NULL){
#if DEBUG
                        printf("SE HA ENCONTRADO, DROPPING\n");
#endif
                        continue;
                    }
*/
#if DEBUG
					printf("NO SE HA ENCONTRADO, CREAMOS UNA NUEVA\n");
#endif

					new_coap_eap_session = XMALLOC(coap_eap_ctx,1);
					init_CoAP_EAP_Session(new_coap_eap_session);
					
					int rc = pthread_mutex_lock(&(new_coap_eap_session->mutex));
						
						memcpy(&new_coap_eap_session->recvAddr, &their_addr, sizeof(struct sockaddr_storage));
						//new_coap_eap_session->recvAddr = their_addr;

#if DEBUG
						printf("The new session_id is %X\n", new_coap_eap_session->session_id);
#endif
						setSessionID(new_task,new_coap_eap_session->session_id);
						
						new_coap_eap_session->list_of_alarms=&(list_alarms_coap_eap);
						add_coap_eap_session(new_coap_eap_session);	
						
						storeLastReceivedMessageInSession(recvPDU,new_coap_eap_session);

					rc = pthread_mutex_unlock(&(new_coap_eap_session->mutex));
	
					add_task(process_coap_msg, new_task);                    	


				} else if(recvPDU->getType() == CoapPDU::COAP_ACKNOWLEDGEMENT){
#if DEBUG
					printf("######## ACK RECIBIDO\n");
#endif

					// Nos aseguramos de que el mensaje no es un duplicado
					uint32_t session_id;
					memcpy(&session_id, recvPDU->getTokenPointer(), (size_t)recvPDU->getTokenLength());

					coap_eap_ctx * coap_eap_session = get_coap_eap_session(session_id);

					if(coap_eap_session == NULL )
					{
						printf("Error getting coap_eap_session\n");
						exit(0);
					}

					int rc = pthread_mutex_lock(&(coap_eap_session->mutex));


					// Vemos que el ultimo mensaje recivido no sea el mismo que el actual
					CoapPDU *lastReceived =  new CoapPDU((uint8_t*)coap_eap_session->lastReceivedMessage,BUF_LEN,BUF_LEN);
					lastReceived->setPDULength(coap_eap_session->lastReceivedMessage_len);

					CoapPDU *lastSent =  new CoapPDU((uint8_t*)coap_eap_session->lastSentMessage,BUF_LEN,BUF_LEN);
					lastSent->setPDULength(coap_eap_session->lastSentMessage_len);


					if(lastReceived->validate() != 1 || lastSent->validate() != 1){
						INFO(" Malformed CoAP packet");
						rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
						continue;
					}

					if(ntohs(recvPDU->getMessageID()) != ntohs(lastSent->getMessageID()) )
					{
#if DEBUG
						pana_debug("DUPLICADO: Mensaje fuera de orden");
#endif
						//rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
						//continue;
							//coapRetransmitLastSentMessage(coap_eap_session);
					}

					 // Esto da un caso raro de mensaje duplicado en el 2 PUT ACK.
/*
					else if (lastReceived->getPDULength() == recvPDU->getPDULength() &&
						memcmp(lastReceived->getPDUPointer(),recvPDU->getPDUPointer(), (size_t)recvPDU->getPDULength()) == 0 )
					{	// Entendemos que son iguales, por lo que retransmitimos el ultimo mensaje enviado
						pana_debug("DUPLICADO: Esperamos alarma ");

//						coapRetransmitLastSentMessage(coap_eap_session);


						if(ntohs(recvPDU->getMessageID()) < ntohs(lastSent->getMessageID()) )
						{
							pana_debug("DUPLICADO: Mensaje duplicado, reenviamos");
							coapRetransmitLastSentMessage(coap_eap_session);
						}
						else{
							pana_debug("DUPLICADO: Mensaje fuera de lugar, dropping");
							rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
							continue;
						}
					}
*/
					else{

//						get_alarm_coap_eap_session(&list_alarms_coap_eap, coap_eap_session->session_id, POST_ALARM);
						storeLastReceivedMessageInSession(recvPDU,coap_eap_session);
						add_task(process_acknowledgment, new_task);                    
					}
				
					rc = pthread_mutex_unlock(&(coap_eap_session->mutex));
					delete lastReceived;
				}


				// code==0, no payload, this is a ping request, send RST
				if(recvPDU->getPDULength()==0&&recvPDU->getCode()==0) {
					INFO("CoAP ping request");
				}else
				{
					delete recvPDU;
				}

#if DEBUG
				printf("######## FIN PROCESAMIENTO DE MENSAJE RECIBIDO\n"
						"##\n"
						"??\n"
				);
#endif

			}	
		}
	}

	close(global_sockfd);
	return NULL;
}


void * handle_alarm_coap_management(void *data) {

#if DEBUG
    printf("Enter handle_alarm_coap_management\n");
#endif

    while (TRUE){ // Do it while the PAA is activated.

       // print_list_alarms(&list_alarms_coap_eap);

		struct retr_coap_func_parameter retrans_params;
		// Get the actual timestamp.
		double time = getTime();

		struct lalarm_coap* alarm = NULL;
		while ((alarm=get_next_alarm_coap_eap(&list_alarms_coap_eap, time)) != NULL)
        {
            printf("Looking for alarms\n");

            retrans_params.session = alarm->coap_eap_session;
			retrans_params.id = 0;
			
			if (alarm->id == POST_ALARM) 
			{
#if DEBUG
				printf("A POST_AUTH alarm ocurred %d\n",retrans_params.session->session_id);
#endif
				retrans_params.id = POST_ALARM;
				add_task(process_retr_coap_eap, &retrans_params);     
			}
#if DEBUG
			else { // An unknown alarm is activated.
				printf("\nAn UNKNOWN alarm ocurred\n");
			}
#endif
		}
		waitusec(TIME_WAKE_UP);
	}
	return NULL;
}

//>
//> MAIN
//>


int main(int argc, char* argv[]) {

	// Variables needed to use threads
	int num_threads = NUM_WORKERS +1; // Workers & network manager
	int i; //loop counter
	int thr_id[num_threads]; // thread IDs
	pthread_t p_threads[num_threads]; // thread's structures

	load_config_server();

#if DEBUG
    printf("\n Server operation mode:");

    if(MODE)
        printf("PASSTHROUGH\n\n");
    else
        printf("STANDALONE\n\n");
#endif

	global_sockfd = socket(AF_INET6, SOCK_DGRAM, 0);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	sem_init(&got_task, 0, 0);
#pragma clang diagnostic pop

	//Init the lockers
	pthread_mutex_init(&list_sessions_mutex, NULL);
	pthread_mutex_init(&list_tasks_mutex, NULL);

	//Init global variables
	list_alarms_coap_eap = init_alarms_coap();

	for (i = 0; i < NUM_WORKERS; i++) {
		thr_id[i] = i;
		pthread_create(&p_threads[i], NULL, handle_worker, (void*) &thr_id[i]);
		pthread_detach(p_threads[i]);
	}

    //Create alarm manager thread (void *(*)(void *))
    i+=1;
    thr_id[i] = i;
    pthread_create(&p_threads[i], NULL, handle_alarm_coap_management, NULL);

	//Once the workers are executed, the network manager function starts
	handle_network_management(NULL);
	printf("OpenPANA-CoAP: The server has stopped.\n");
	return 0;
}



