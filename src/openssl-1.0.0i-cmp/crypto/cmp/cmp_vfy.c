/* vim: set noet ts=4 sts=4 sw=4: */
/* crypto/cmp/cmp_vfy.c
 * Functions to verify CMP (RFC 4210) messages for OpenSSL
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

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/crmf.h>
#include <openssl/cmp.h>
#include <openssl/err.h>

/* ############################################################################ *
 * internal function
 *
 * validate a message protected by signature according to section 5.1.3.3
 * (sha1+RSA/DSA or any other algorithm supported by OpenSSL)
 * returns 0 on error
 * ############################################################################ */
static int CMP_verify_signature( CMP_PKIMESSAGE *msg, X509 *cert) {
    EVP_MD_CTX *ctx=NULL;
    CMP_PROTECTEDPART protPart;
    int ret;
    EVP_MD *digest;

    size_t protPartDerLen;
    unsigned char *protPartDer=NULL;

    if (!msg || !cert) return 0;

	/* create the DER representation of protected part */
    protPart.header = msg->header;
    protPart.body   = msg->body;
    protPartDerLen  = i2d_CMP_PROTECTEDPART(&protPart, &protPartDer);

    /* verify prtotection of protected part */
    ctx = EVP_MD_CTX_create();
    if(!(digest = (EVP_MD *)EVP_get_digestbynid(OBJ_obj2nid(msg->header->protectionAlg->algorithm)))) goto notsup;
    EVP_VerifyInit_ex(ctx, digest, NULL);
    EVP_VerifyUpdate(ctx, protPartDer, protPartDerLen);
    ret = EVP_VerifyFinal(ctx, msg->protection->data, msg->protection->length, X509_get_pubkey((X509*) cert));

    /* cleanup */
    EVP_MD_CTX_destroy(ctx);
    return ret;
notsup:
    CMPerr(CMP_F_CMP_VERIFY_SIGNATURE, CMP_R_ALGORITHM_NOT_SUPPORTED);
    return 0;
}

/* ############################################################################ *
 * internal function
 *
 * Validates a message protected with PBMAC
 * ############################################################################ */
static int CMP_verify_MAC( CMP_PKIMESSAGE *msg, const ASN1_OCTET_STRING *secret)
{
	ASN1_BIT_STRING *protection=NULL;
	int valid = 0;
	
	/* password based Mac */ 
	if (!(protection = CMP_protection_new( msg, NULL, secret)))
		goto err; /* failed to generate protection string! */
	
	valid = M_ASN1_BIT_STRING_cmp( protection, msg->protection) == 0;
	ASN1_BIT_STRING_free(protection);
	return valid;
err:
	return 0;
}

/* ############################################################################ *
 * internal function
 *
 * Attempt to validate certificate path. returns 1 if the path was
 * validated successfully and 0 if not.
 * ############################################################################ */
int CMP_validate_cert_path(X509_STORE *trusted_store,
                           X509_STORE *untrusted_store,
                           X509 *cert)
{
    int ret=0,valid=0;
    X509_STORE_CTX *csc=NULL;
    STACK_OF(X509) *untrusted_stack=NULL;

    if (!cert) goto end;

    if (!trusted_store) {
        CMPerr(CMP_F_CMP_VALIDATE_CERT_PATH, CMP_R_NO_TRUSTED_CERTIFICATES_SET);
        goto end;
    }

	/* A cert callback could be used to do additional checking, policies for example.*/
	/* X509_STORE_set_verify_cb(trusted_store, CMP_cert_callback); */

    if (!(csc = X509_STORE_CTX_new())) goto end;

    /* note: there doesn't seem to be a good way to get a stack of all
	 * the certs in an X509_STORE, so we need to try and find the chain
	 * of intermediate certs here. */
	if (untrusted_store)
        untrusted_stack = CMP_build_cert_chain(untrusted_store, cert);

	X509_STORE_set_flags(trusted_store, 0);
	if(!X509_STORE_CTX_init(csc, trusted_store, cert, untrusted_stack))
		goto end;

    /* CRLs could be handled here */
    /* if (crls) X509_STORE_CTX_set0_crls(csc, crls); */

    valid=X509_verify_cert(csc);

    X509_STORE_CTX_free(csc);

    ret=0;

end:
	if (untrusted_stack)
		sk_X509_pop_free(untrusted_stack, X509_free);
    
    if (valid > 0) {
        ret = 1;
    }

    return(ret);
}

#if 0
/* ############################################################################ *
 * NOTE: This is only needed if/when we want to do additional checking on the certificates!
 *       It is not currently used.
 * 
 * This is called for every valid certificate. Here we could add additional checks,
 * for policies for example.
 * ############################################################################ */
int CMP_cert_callback(int ok, X509_STORE_CTX *ctx)
{
    int cert_error = X509_STORE_CTX_get_error(ctx);
    X509 *current_cert = X509_STORE_CTX_get_current_cert(ctx);

    if (!ok)
    {
        switch(cert_error)
        {
            case X509_V_ERR_NO_EXPLICIT_POLICY:
                // policies_print(NULL, ctx);
            case X509_V_ERR_CERT_HAS_EXPIRED:

                /* since we are just checking the certificates, it is
                 * ok if they are self signed. But we should still warn
                 * the user.
                 */

            case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
                /* Continue after extension errors too */
            case X509_V_ERR_INVALID_CA:
            case X509_V_ERR_INVALID_NON_CA:
            case X509_V_ERR_PATH_LENGTH_EXCEEDED:
            case X509_V_ERR_INVALID_PURPOSE:
            case X509_V_ERR_CRL_HAS_EXPIRED:
            case X509_V_ERR_CRL_NOT_YET_VALID:
            case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
                ok = 1;

        }

        return ok;
    }

#if 0
    /* TODO: we could check policies here too */
    if (cert_error == X509_V_OK && ok == 2)
        policies_print(NULL, ctx);
#endif

    return(ok);
}
#endif


/* ############################################################################ *
 * internal function
 *
 * Find server certificate by:
 * - first see if we can find it in trusted store
 * - TODO: untrusted store
 * - then search for certs with matching name in extraCerts
 *   - if only one match found, return that
 *   - if more than one, try to find a cert with the matching senderKID if available
 *   - if keyID is not available, return first cert found
 * ############################################################################ */
static X509 *findSrvCert(CMP_CTX *ctx, CMP_PKIMESSAGE *msg)
{
	X509 *srvCert = NULL;
	X509_STORE_CTX *csc = X509_STORE_CTX_new();

	/* first attempt lookup in trusted_store */
	if (csc != NULL && X509_STORE_CTX_init(csc, ctx->trusted_store, NULL, NULL)) {
		X509_OBJECT obj;
		if (X509_STORE_get_by_subject(csc, X509_LU_X509, msg->header->sender->d.directoryName, &obj))
			srvCert = obj.data.x509;
		X509_STORE_CTX_free(csc);
	}

	/* not found in trusted_store, so look through extraCerts */
	if (!srvCert) {
		STACK_OF(X509) *found_certs = sk_X509_new_null();
		int n;

		for (n = 0; n < sk_X509_num(msg->extraCerts); n++) {
			X509 *cert = sk_X509_value(msg->extraCerts, n);
			X509_NAME *name = NULL;
			if (!cert) continue;
			name = X509_get_subject_name(cert);

			if (name && !X509_NAME_cmp(name, msg->header->sender->d.directoryName))
				sk_X509_push(found_certs, cert);
		}

		/* if found exactly one cert, we'll use that */
		if (sk_X509_num(found_certs) == 1)
			srvCert = sk_X509_pop(found_certs);

		/* found more than one with a matching name, so try to search
		   through the found certs by key ID if we have it.  if not,
		   just return first one. */
		else if (sk_X509_num(found_certs) > 1) {
			if (msg->header->senderKID) {
				for (n = 0; n < sk_X509_num(found_certs); n++) {
					X509 *cert = sk_X509_value(found_certs, n);
					ASN1_OCTET_STRING *cert_keyid = CMP_get_subject_key_id(cert);

					if (!ASN1_OCTET_STRING_cmp(cert_keyid, msg->header->senderKID)) {
						srvCert = cert;
						break;
					}
				}
			}

			if (!srvCert) {
				/* key id not available or we didn't find a cert with matching keyID.
				 * -> return the first one with matching name */
				srvCert = sk_X509_pop(found_certs);
			}
		}

		sk_X509_free(found_certs);
	}

	return srvCert;
}


/* ############################################################################ *
 * internal function
 *
 * Creates a new certificate store and adds all the self-signed certificates from
 * the given stack to the store.
 * ############################################################################ */
static X509_STORE *createTempTrustedStore(STACK_OF(X509) *stack)
{
	X509_STORE *store = X509_STORE_new();
	int i;

	for (i = 0; i < sk_X509_num(stack); i++) {
		X509 *cert = sk_X509_value(stack, i);
		EVP_PKEY *pubkey = X509_get_pubkey(cert);

		if (X509_verify(cert, pubkey))
			X509_STORE_add_cert(store, cert);
	}

	return store;
}

/* ############################################################################
 * Validates the protection of the given PKIMessage using either password
 * based mac or a signature algorithm. In the case of signature algorithm, the
 * certificate can be provided in ctx->srvCert or it is taken from 
 * extraCerts and validate against ctx->trusted_store utilizing 
 * ctx->untrusted_store and extraCerts.
 *
 * If ctx->permitTAInExtraCertsForIR is true, the trust anchor may be taken from
 * the extraCerts field when a self-signed certificate is found there which can
 * be used to validate the issued certificate returned in IP.  This is according
 * to the need given in 3GPP TS 33.310.
 *
 * returns 1 on success, 0 on error or validation failed
 * ############################################################################ */
int CMP_validate_msg(CMP_CTX *ctx, CMP_PKIMESSAGE *msg)
{
	X509 *srvCert = ctx->srvCert;
	int srvCert_valid = 0;
	int nid = 0;
	ASN1_OBJECT *algorOID=NULL;

	/* determine the nid for the used protection algorithm */
	X509_ALGOR_get0( &algorOID, NULL, NULL, msg->header->protectionAlg);
	nid = OBJ_obj2nid(algorOID);

	switch (nid) {
		/* 5.1.3.1.  Shared Secret Information */
		case NID_id_PasswordBasedMAC:
			return CMP_verify_MAC(msg, ctx->secretValue);

		/* TODO: 5.1.3.2.  DH Key Pairs */
		case NID_id_DHBasedMac:
			CMPerr(CMP_F_CMP_VALIDATE_MSG, CMP_R_UNSUPPORTED_PROTECTION_ALG_DHBASEDMAC);
			break;

		/* 5.1.3.3.  Signature */
		/* TODO: should that better whitelist DSA/RSA etc.? -> check all possible options from OpenSSL, should there be a makro? */
		default:
			if (!srvCert) {
				/* if we've already found and validated a server cert, and it matches the sender name,
				 * we will use that, this is used for PKIconf where the server
				 * certificate and others could be missing from the extraCerts */
				if (ctx->validatedSrvCert &&
					!X509_NAME_cmp(X509_get_subject_name(ctx->validatedSrvCert), msg->header->sender->d.directoryName)) {
					srvCert = ctx->validatedSrvCert;
					srvCert_valid = 1;
				}
				else {
					/* load the provided extraCerts to help with cert path validation */
					CMP_CTX_loadUntrustedStack(ctx, msg->extraCerts);

					/* try to find the server certificate from 1) trusted_store 2) untrusted_store 3) extaCerts*/
					srvCert = findSrvCert(ctx, msg);

					/* validate the that the found server Certificate is trusted */
					srvCert_valid = CMP_validate_cert_path(ctx->trusted_store, ctx->untrusted_store, srvCert);

					/* do an exceptional handling for 3GPP */	
					if (!srvCert_valid) {
						/* For IP: when the ctxOption is set, extract the Trust Anchor from
						 * ExtraCerts, provided that there is a self-signed certificate
						 * which can be used to validate the issued certificate - refer to 3GPP TS 33.310 */
						if (ctx->permitTAInExtraCertsForIR && CMP_PKIMESSAGE_get_bodytype(msg) == V_CMP_PKIBODY_IP) {
							X509_STORE *tempStore = createTempTrustedStore(msg->extraCerts);
							/* TODO: check that issued certificates can validate against
							 * trust achnor - and then exclusively use this CA */
							srvCert_valid = CMP_validate_cert_path(tempStore, ctx->untrusted_store, srvCert);

							if (srvCert_valid) {
								/* verify that our received certificate is issued and signed by srvCert */
								X509 *newClCert = CMP_CERTREPMESSAGE_get_certificate(ctx, msg->body->value.ip);
								if (newClCert) {
									EVP_PKEY *srvKey = X509_get_pubkey((X509*) srvCert);
									if (X509_NAME_cmp(newClCert->cert_info->issuer, srvCert->cert_info->subject) != 0
										|| !X509_verify(newClCert, srvKey))
										/* received cert cannot be validated using this srvCert */
										srvCert_valid = 0;
								}
							}
							
							X509_STORE_free(tempStore);
						}
					}
				}
				
				/* verification failed if no valid server cert was found */
				if (!srvCert_valid) return 0; 

				/* store trusted server cert for future messages in this interaction */
				ctx->validatedSrvCert = srvCert;
			}
			return CMP_verify_signature(msg, srvCert);
	}
	return 0;
}

