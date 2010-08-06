/* crypto/crmf/crmf_err.c */
/* ====================================================================
 * Copyright (c) 1999-2010 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

/* NOTE: this file was auto generated by the mkerr.pl script: any changes
 * made to it will be overwritten when the script next updates this file,
 * only reason strings will be preserved.
 */

#include <stdio.h>
#include <openssl/err.h>
#include <openssl/crmf.h>

/* BEGIN ERROR CODES */
#ifndef OPENSSL_NO_ERR

#define ERR_FUNC(func) ERR_PACK(ERR_LIB_CRMF,func,0)
#define ERR_REASON(reason) ERR_PACK(ERR_LIB_CRMF,0,reason)

static ERR_STRING_DATA CRMF_str_functs[]=
	{
{ERR_FUNC(CRMF_F_CRMF_ATAV_OLDCERTID_NEW),	"CRMF_ATAV_OldCertId_new"},
{ERR_FUNC(CRMF_F_CRMF_CERTREQMSG_PUSH0_CONTROL),	"CRMF_CERTREQMSG_push0_control"},
{ERR_FUNC(CRMF_F_CRMF_CERTREQMSG_PUSH0_EXTENSION),	"CRMF_CERTREQMSG_push0_extension"},
{ERR_FUNC(CRMF_F_CRMF_CERTREQMSG_PUSH0_REGINFO),	"CRMF_CERTREQMSG_PUSH0_REGINFO"},
{ERR_FUNC(CRMF_F_CRMF_CERTREQMSG_SET1_PUBLICKEY),	"CRMF_CERTREQMSG_set1_publicKey"},
{ERR_FUNC(CRMF_F_CRMF_CERTREQMSG_SET_VALIDITY),	"CRMF_CERTREQMSG_set_validity"},
{ERR_FUNC(CRMF_F_CRMF_CR_NEW),	"CRMF_cr_new"},
{ERR_FUNC(CRMF_F_CRMF_PASSWORDBASEDMAC_NEW),	"CRMF_passwordBasedMac_new"},
{0,NULL}
	};

static ERR_STRING_DATA CRMF_str_reasons[]=
	{
{ERR_REASON(CRMF_R_CRMFERROR)            ,"crmferror"},
{ERR_REASON(CRMF_R_ERROR_SETTING_PUBLIC_KEY),"error setting public key"},
{ERR_REASON(CRMF_R_UNSUPPORTED_ALGORITHM),"unsupported algorithm"},
{0,NULL}
	};

#endif

void ERR_load_CRMF_strings(void)
	{
#ifndef OPENSSL_NO_ERR

	if (ERR_func_error_string(CRMF_str_functs[0].error) == NULL)
		{
		ERR_load_strings(0,CRMF_str_functs);
		ERR_load_strings(0,CRMF_str_reasons);
		}
#endif
	}
