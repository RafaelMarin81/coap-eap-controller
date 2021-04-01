/*
 * EAP peer: Method registration
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef EAP_METHODS_H
#define EAP_METHODS_H

#include "eap_common/eap_defs.h"
const struct eap_method * eap_peer_get_eap_method(struct eap_method *eap_methods, int vendor, EapType method);
//const struct eap_method * eap_peer_get_eap_method(int vendor, EapType method);
const struct eap_method * eap_peer_get_methods(struct eap_method *eap_methods, size_t *count);
//const struct eap_method * eap_peer_get_methods(size_t *count);

struct eap_method * eap_peer_method_alloc(int version, int vendor,
										  EapType method, const char *name);
/*struct eap_method * eap_peer_method_alloc(int version, int vendor,
					  EapType method, const char *name);*/
void eap_peer_method_free(struct eap_method *method);
int eap_peer_method_register(struct eap_method **eap_methods, struct eap_method *method);
//int eap_peer_method_register(struct eap_method *method);


#ifdef IEEE8021X_EAPOL
EapType eap_peer_get_type(struct eap_method *eap_methods, const char *name, int *vendor);
//EapType eap_peer_get_type(const char *name, int *vendor);
const char * eap_get_name(struct eap_method *eap_methods, int vendor, EapType type);
//const char * eap_get_name(int vendor, EapType type);
size_t eap_get_names(struct eap_method *eap_methods, char *buf, size_t buflen);
//size_t eap_get_names(char *buf, size_t buflen);
char ** eap_get_names_as_string_array(struct eap_method *eap_methods, size_t *num);
//char ** eap_get_names_as_string_array(size_t *num);
void eap_peer_unregister_methods(struct eap_method **eap_methods);
//void eap_peer_unregister_methods(void);

#else /* IEEE8021X_EAPOL */

static inline EapType eap_peer_get_type(const char *name, int *vendor)
{
	*vendor = EAP_VENDOR_IETF;
	return EAP_TYPE_NONE;
}

static inline const char * eap_get_name(struct eap_method *eap_methods, int vendor, EapType type)
{
	return NULL;
}

static inline size_t eap_get_names(struct eap_method *eap_methods, char *buf, size_t buflen)
{
	return 0;
}

static inline int eap_peer_register_methods(struct eap_method **eap_methods)
{
	return 0;
}

static inline void eap_peer_unregister_methods(struct eap_method **eap_methods)
{
}

static inline char ** eap_get_names_as_string_array(struct eap_method *eap_methods, size_t *num)
{
	return NULL;
}

#endif /* IEEE8021X_EAPOL */


#ifdef CONFIG_DYNAMIC_EAP_METHODS
int eap_peer_method_load(struct eap_method *eap_methods, const char *so);
//int eap_peer_method_load(const char *so);
int eap_peer_method_unload(struct eap_method **eap_methods, struct eap_method *method);
//int eap_peer_method_unload(struct eap_method *method);

#else /* CONFIG_DYNAMIC_EAP_METHODS */

static inline int eap_peer_method_load(struct eap_method *eap_methods,const char *so)
{
	return 0;
}

static inline int eap_peer_method_unload(struct eap_method *eap_methods, struct eap_method *method)
{
	return 0;
}

#endif /* CONFIG_DYNAMIC_EAP_METHODS */

/* EAP peer method registration calls for statically linked in methods */
int eap_peer_md5_register(struct eap_method **eap_methods);
int eap_peer_tls_register(struct eap_method **eap_methods);
int eap_peer_mschapv2_register(struct eap_method **eap_methods);
int eap_peer_peap_register(struct eap_method **eap_methods);
int eap_peer_ttls_register(struct eap_method **eap_methods);
int eap_peer_gtc_register(struct eap_method **eap_methods);
int eap_peer_otp_register(struct eap_method **eap_methods);
int eap_peer_sim_register(struct eap_method **eap_methods);
int eap_peer_leap_register(struct eap_method **eap_methods);
int eap_peer_psk_register(struct eap_method **eap_methods);
int eap_peer_aka_register(struct eap_method **eap_methods);
int eap_peer_aka_prime_register(struct eap_method **eap_methods);
int eap_peer_fast_register(struct eap_method **eap_methods);
int eap_peer_pax_register(struct eap_method **eap_methods);
int eap_peer_sake_register(struct eap_method **eap_methods);
int eap_peer_gpsk_register(struct eap_method **eap_methods);
int eap_peer_wsc_register(struct eap_method **eap_methods);
int eap_peer_ikev2_register(struct eap_method **eap_methods);
int eap_peer_vendor_test_register(struct eap_method **eap_methods);
int eap_peer_tnc_register(struct eap_method **eap_methods);

#endif /* EAP_METHODS_H */
