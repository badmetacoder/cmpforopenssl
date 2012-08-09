/* crypto/crmf/crmf_msg.c
 * Functions for creating CRMF (RFC 4211) messages for OpenSSL
 */
/* ====================================================================
 * Originally written by Martin Peylo for the OpenSSL project.
 * <martin dot peylo at nsn dot com>
 * 2010-2012 Miikka Viljanen <mviljane@users.sourceforge.net>
 */
/* ====================================================================
 * Copyright (c) 2007-2010 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2007-2012 Nokia Siemens Networks Oy. ALL RIGHTS RESERVED.
 * CMP support in OpenSSL originally developed by 
 * Nokia Siemens Networks for contribution to the OpenSSL project.
 */

/* ############################################################################ */
/* In this file are the functions which build and evaluate the CRMF messages    */
/* ############################################################################ */

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/crmf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <string.h>


/* ############################################################################ */
/* TODO there are some optional settings which are not cared for yet */
CRMF_CERTREQMSG * CRMF_cr_new( const long certReqId, const EVP_PKEY *pkey, const X509_NAME *subject, int popoMethod, X509_EXTENSIONS *extensions) {
	CRMF_CERTREQMSG *certReqMsg;
	int i;

	if( !(certReqMsg = CRMF_CERTREQMSG_new())) goto err;

	/* version MUST be 2 if supplied.  It SHOULD be omitted. */
	/* CRMF_CERTREQMSG_set_version2( certReqMsg); */

	CRMF_CERTREQMSG_set_certReqId( certReqMsg, certReqId);
	if (!CRMF_CERTREQMSG_set1_publicKey( certReqMsg, pkey)) {
		CRMFerr(CRMF_F_CRMF_CR_NEW, CRMF_R_ERROR_SETTING_PUBLIC_KEY);
		goto err;
	}

	CRMF_CERTREQMSG_set1_subject( certReqMsg, subject);

	/* XXX: set validity time here? */

	for (i = 0; i < sk_X509_EXTENSION_num(extensions); i++)
		/* X509v3_add_ext will allocate new stack if there isn't one already */
		X509v3_add_ext(&certReqMsg->certReq->certTemplate->extensions, sk_X509_EXTENSION_value(extensions, i), i);
	
		CRMF_CERTREQMSG_calc_and_set_popo( certReqMsg, pkey, popoMethod);

	return certReqMsg;
err:
	CRMFerr(CRMF_F_CRMF_CR_NEW, CRMF_R_CRMFERROR);
	if( certReqMsg)
		CRMF_CERTREQMSG_free( certReqMsg);
	return NULL;
}

