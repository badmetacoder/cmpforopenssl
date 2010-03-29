/* crmf_asn.c
 * 
 * OpenSSL ASN.1 definitions for CRMF (RFC 4211)
 *
 * Written by Martin Peylo <martin.peylo@nsn.com>
 */

/*
 * The following license applies to this file:
 *
 * Copyright (c) 2007, Nokia Siemens Networks (NSN)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of NSN nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY NSN ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NSN BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The following licenses apply to OpenSSL:
 *
 * OpenSSL License
 * ---------------
 *
 * ====================================================================
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
 * ====================================================================
 *
 * Original SSLeay License
 * -----------------------
 *
 * Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/crmf.h>

ASN1_SEQUENCE(CRMF_PRIVATEKEYINFO) = {
	ASN1_SIMPLE(CRMF_PRIVATEKEYINFO, version, ASN1_INTEGER),
	ASN1_SIMPLE(CRMF_PRIVATEKEYINFO, AlgorithmIdentifier, X509_ALGOR),
	ASN1_SIMPLE(CRMF_PRIVATEKEYINFO, privateKey, ASN1_OCTET_STRING),
	ASN1_IMP_SET_OF_OPT(X509_REQ_INFO, attributes, X509_ATTRIBUTE, 0)
} ASN1_SEQUENCE_END(CRMF_PRIVATEKEYINFO)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PRIVATEKEYINFO)


ASN1_CHOICE(CRMF_ENCKEYWITHID_IDENTIFIER) = {
	/* TODO 
	 * ASN1_IMP or ASN1_EXP?
	 * numbers 1,2...?
	 */
	ASN1_IMP(CRMF_ENCKEYWITHID_IDENTIFIER, value.string, ASN1_UTF8STRING, 1),
	ASN1_IMP(CRMF_ENCKEYWITHID_IDENTIFIER, value.generalName, GENERAL_NAME, 2)
} ASN1_CHOICE_END(CRMF_ENCKEYWITHID_IDENTIFIER)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_ENCKEYWITHID_IDENTIFIER)


ASN1_SEQUENCE(CRMF_ENCKEYWITHID) = {
	ASN1_SIMPLE(CRMF_ENCKEYWITHID, privateKey, CRMF_PRIVATEKEYINFO),
	/* TODO 
	 * ASN1_IMP_OPT or ASN1_EXP_OPT?
	 * numbers 0,1...?
	 */
	ASN1_IMP_OPT(CRMF_ENCKEYWITHID, identifier, CRMF_ENCKEYWITHID_IDENTIFIER,0)
} ASN1_SEQUENCE_END(CRMF_ENCKEYWITHID)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_ENCKEYWITHID)


ASN1_SEQUENCE(CRMF_CERTID) = {
	ASN1_SIMPLE(CRMF_CERTID, issuer, GENERAL_NAME),
	ASN1_SIMPLE(CRMF_CERTID, serialNumber, ASN1_INTEGER)
} ASN1_SEQUENCE_END(CRMF_CERTID)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_CERTID)


ASN1_SEQUENCE(CRMF_ENCRYPTEDVALUE) = {
	/* TODO 
	 * ASN1_IMP_OPT or ASN1_EXP_OPT?
	 * numbers 0,1...?
	 */
	ASN1_EXP_OPT(CRMF_ENCRYPTEDVALUE, intendedAlg, X509_ALGOR,0),
	ASN1_EXP_OPT(CRMF_ENCRYPTEDVALUE, symmAlg, X509_ALGOR,1),
	ASN1_EXP_OPT(CRMF_ENCRYPTEDVALUE, encSymmKey, ASN1_BIT_STRING,2),
	ASN1_EXP_OPT(CRMF_ENCRYPTEDVALUE, keyAlg, X509_ALGOR,3),
	ASN1_EXP_OPT(CRMF_ENCRYPTEDVALUE, valueHint, ASN1_OCTET_STRING,4),
	ASN1_SIMPLE(CRMF_ENCRYPTEDVALUE, encValue, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END(CRMF_ENCRYPTEDVALUE)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_ENCRYPTEDVALUE)


/* TODO CMS_ENVELOPEDDATA */
ASN1_SEQUENCE(CMS_ENVELOPEDDATA) = {
	ASN1_SIMPLE(CMS_ENVELOPEDDATA, version, ASN1_INTEGER)
} ASN1_SEQUENCE_END(CMS_ENVELOPEDDATA)
IMPLEMENT_ASN1_FUNCTIONS(CMS_ENVELOPEDDATA)


ASN1_CHOICE(CRMF_ENCRYPTEDKEY) = {
	/* TODO 
	 * ASN1_IMP or ASN1_EXP?
	 * numbers 1,2...?
	 */
	ASN1_IMP(CRMF_ENCRYPTEDKEY, value.encryptedValue, CRMF_ENCRYPTEDVALUE, 0),
	ASN1_EXP(CRMF_ENCRYPTEDKEY, value.envelopedData, CMS_ENVELOPEDDATA, 0)
} ASN1_CHOICE_END(CRMF_ENCRYPTEDKEY)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_ENCRYPTEDKEY)


ASN1_CHOICE(CRMF_PKIARCHIVEOPTIONS) = {
	/* TODO 
	 * ASN1_IMP or ASN1_EXP?
	 * numbers 1,2...?
	 */
	ASN1_EXP(CRMF_PKIARCHIVEOPTIONS, value.encryptedPrivKey, CRMF_ENCRYPTEDKEY, 0),
	ASN1_EXP(CRMF_PKIARCHIVEOPTIONS, value.keyGenParameters, ASN1_OCTET_STRING, 1),
	ASN1_EXP(CRMF_PKIARCHIVEOPTIONS, value.archiveRemGenPrivKey, ASN1_BOOLEAN, 2)
} ASN1_CHOICE_END(CRMF_PKIARCHIVEOPTIONS)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PKIARCHIVEOPTIONS)


ASN1_SEQUENCE(CRMF_SINGLEPUBINFO) = {
	ASN1_SIMPLE(CRMF_SINGLEPUBINFO, pubMethod, ASN1_INTEGER),
	ASN1_SIMPLE(CRMF_SINGLEPUBINFO, pubLocation, GENERAL_NAME)
} ASN1_SEQUENCE_END(CRMF_SINGLEPUBINFO)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_SINGLEPUBINFO)


ASN1_SEQUENCE(CRMF_PKIPUBLICATIONINFO) = {
	ASN1_SIMPLE(CRMF_PKIPUBLICATIONINFO, action, ASN1_INTEGER),
	ASN1_SEQUENCE_OF_OPT(CRMF_PKIPUBLICATIONINFO, pubinfos, GENERAL_NAME)
} ASN1_SEQUENCE_END(CRMF_PKIPUBLICATIONINFO)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PKIPUBLICATIONINFO)


ASN1_SEQUENCE(CRMF_PKMACVALUE) = {
	ASN1_SIMPLE(CRMF_PKMACVALUE, algId, X509_ALGOR),
	ASN1_SIMPLE(CRMF_PKMACVALUE, value, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END(CRMF_PKMACVALUE)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PKMACVALUE)


ASN1_SEQUENCE(CRMF_POPOPRIVKEY) = {
	ASN1_SIMPLE(CRMF_POPOPRIVKEY, value.thisMessage, ASN1_BIT_STRING),
	ASN1_SIMPLE(CRMF_POPOPRIVKEY, value.subsequentMessage, ASN1_INTEGER),
	ASN1_SIMPLE(CRMF_POPOPRIVKEY, value.dhMAC, ASN1_BIT_STRING),
	ASN1_SIMPLE(CRMF_POPOPRIVKEY, value.agreeMAC, CRMF_PKMACVALUE),
	ASN1_SIMPLE(CRMF_POPOPRIVKEY, value.encryptedKey, CMS_ENVELOPEDDATA),
} ASN1_SEQUENCE_END(CRMF_POPOPRIVKEY)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_POPOPRIVKEY)


ASN1_SEQUENCE(CRMF_PBMPARAMETER) = {
	ASN1_SIMPLE(CRMF_PBMPARAMETER, salt, ASN1_OCTET_STRING),
	ASN1_SIMPLE(CRMF_PBMPARAMETER, owf, X509_ALGOR),
	ASN1_SIMPLE(CRMF_PBMPARAMETER, iterationCount, ASN1_INTEGER),
	ASN1_SIMPLE(CRMF_PBMPARAMETER, mac, X509_ALGOR)
} ASN1_SEQUENCE_END(CRMF_PBMPARAMETER)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PBMPARAMETER)


ASN1_CHOICE(CRMF_POPOSIGNINGKEYINPUT_AUTHINFO) = {
	/* TODO 
	 * ASN1_IMP or ASN1_EXP?
	 * numbers 1,2...?
	 */
	ASN1_EXP(CRMF_POPOSIGNINGKEYINPUT_AUTHINFO, value.sender, GENERAL_NAME, 0),
	ASN1_IMP(CRMF_POPOSIGNINGKEYINPUT_AUTHINFO, value.publicKeyMAC, CRMF_PKMACVALUE, 1)
} ASN1_CHOICE_END(CRMF_POPOSIGNINGKEYINPUT_AUTHINFO)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_POPOSIGNINGKEYINPUT_AUTHINFO)


ASN1_SEQUENCE(CRMF_POPOSIGNINGKEYINPUT) = {
	ASN1_SIMPLE(CRMF_POPOSIGNINGKEYINPUT, authinfo, CRMF_POPOSIGNINGKEYINPUT_AUTHINFO),
	/* XXX I don't know if that's the right type */
	ASN1_SIMPLE(CRMF_POPOSIGNINGKEYINPUT, publicKey, X509_PUBKEY)
} ASN1_SEQUENCE_END(CRMF_POPOSIGNINGKEYINPUT)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_POPOSIGNINGKEYINPUT)


ASN1_SEQUENCE(CRMF_POPOSIGNINGKEY) = {
	ASN1_IMP_OPT(CRMF_POPOSIGNINGKEY, poposkInput, CRMF_POPOSIGNINGKEYINPUT,0),
	ASN1_SIMPLE(CRMF_POPOSIGNINGKEY, algorithmIdentifier, X509_ALGOR),
	ASN1_SIMPLE(CRMF_POPOSIGNINGKEY, signature, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END(CRMF_POPOSIGNINGKEY)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_POPOSIGNINGKEY)


ASN1_CHOICE(CRMF_PROOFOFPOSSESION) = {
	/* TODO 
	 * ASN1_IMP or ASN1_EXP?
	 * numbers 1,2...?
	 */
	ASN1_IMP(CRMF_PROOFOFPOSSESION, value.raVerified, ASN1_NULL, 0),
	ASN1_IMP(CRMF_PROOFOFPOSSESION, value.signature, CRMF_POPOSIGNINGKEY, 1),
	ASN1_IMP(CRMF_PROOFOFPOSSESION, value.keyEncipherment, CRMF_POPOPRIVKEY, 2),
	ASN1_IMP(CRMF_PROOFOFPOSSESION, value.keyAgreement, CRMF_POPOPRIVKEY, 3)
} ASN1_CHOICE_END(CRMF_PROOFOFPOSSESION)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_PROOFOFPOSSESION)


/* XXX is this the right way for an ANY DEFINED BY? */
/* TODO I'm pretty sure it's not */
/* ASN1_ADB_TEMPLATE(attributetypeandvalue_default) = ASN1_EXP(CRMF_ATTRIBUTETYPEANDVALUE, value, ASN1_ANY, 0); */

#if 0
ASN1_ADB(CRMF_ATTRIBUTETYPEANDVALUE) = {
	ADB_ENTRY(NID_sdsiCertificate, ASN1_EXP(CRMF_ATTRIBUTETYPEANDVALUE, value.sdsicert, ASN1_INTEGER, 0)),
} ASN1_ADB_END(CRMF_ATTRIBUTETYPEANDVALUE, 0, type, 0, &attributetypeandvalue_default_tt, NULL);
#endif /* 0 */


ASN1_SEQUENCE(CRMF_ATTRIBUTETYPEANDVALUE) = {
	ASN1_SIMPLE(CRMF_ATTRIBUTETYPEANDVALUE, type, ASN1_OBJECT),
	/* ASN1_ADB_OBJECT(CRMF_ATTRIBUTETYPEANDVALUE) */
	/* ASN1_ADB_OBJECT(CRMF_ATTRIBUTETYPEANDVALUE) */
	/* ASN1_SIMPLE(CRMF_ATTRIBUTETYPEANDVALUE, value, ASN1_TYPE) */
	/* XXX IMP/EXP ? */
	ASN1_IMP(CRMF_ATTRIBUTETYPEANDVALUE, value, ASN1_ANY, 0)
} ASN1_SEQUENCE_END(CRMF_ATTRIBUTETYPEANDVALUE)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_ATTRIBUTETYPEANDVALUE)
IMPLEMENT_ASN1_DUP_FUNCTION(CRMF_ATTRIBUTETYPEANDVALUE)

ASN1_SEQUENCE(CRMF_OPTIONALVALIDITY) = {
	ASN1_EXP_OPT(CRMF_OPTIONALVALIDITY, notBefore, ASN1_TIME, 0),
	ASN1_EXP_OPT(CRMF_OPTIONALVALIDITY, notAfter, ASN1_TIME, 1)
} ASN1_SEQUENCE_END(CRMF_OPTIONALVALIDITY)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_OPTIONALVALIDITY)


ASN1_SEQUENCE(CRMF_CERTTEMPLATE) = {
	/* Figured out that I have to use IMP here - don't know why */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, version, ASN1_INTEGER, 0),
	/* serialNumber MUST be omitted.  This field is assigned by the CA
	 * during certificate creation. */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, serialNumber, ASN1_INTEGER, 1),
	/* signingAlg MUST be omitted.  This field is assigned by the CA
	 * during certificate creation. */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, signingAlg, X509_ALGOR, 2),
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, issuer, X509_NAME, 3),
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, validity, CRMF_OPTIONALVALIDITY, 4),
	/* XXX why is this EXP? */
	ASN1_EXP_OPT(CRMF_CERTTEMPLATE, subject, X509_NAME, 5),
	/* XXX ... and why does this work when it's IMP? */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, publicKey, X509_PUBKEY, 6),
	/* issuerUID is deprecated in version 2 */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, issuerUID, ASN1_BIT_STRING, 7),
	/* subjectUID is deprecated in version 2 */
	ASN1_IMP_OPT(CRMF_CERTTEMPLATE, subjectUID, ASN1_BIT_STRING, 8),
	ASN1_IMP_SEQUENCE_OF_OPT(CRMF_CERTTEMPLATE, extensions, X509_EXTENSION, 9),
} ASN1_SEQUENCE_END(CRMF_CERTTEMPLATE)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_CERTTEMPLATE)


ASN1_SEQUENCE(CRMF_CERTREQUEST) = {
	ASN1_SIMPLE(CRMF_CERTREQUEST, certReqId, ASN1_INTEGER),
	ASN1_SIMPLE(CRMF_CERTREQUEST, certTemplate, CRMF_CERTTEMPLATE),
	/* XXX is this done right? */
	ASN1_SEQUENCE_OF_OPT(CRMF_CERTREQUEST, controls, CRMF_ATTRIBUTETYPEANDVALUE)
} ASN1_SEQUENCE_END(CRMF_CERTREQUEST)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_CERTREQUEST)


ASN1_SEQUENCE(CRMF_CERTREQMSG) = {
	ASN1_SIMPLE(CRMF_CERTREQMSG, certReq, CRMF_CERTREQUEST),
	/* XXX XXX XXX IMP or EXP ? */
	ASN1_IMP_OPT(CRMF_CERTREQMSG, popo, CRMF_PROOFOFPOSSESION, 0),
	/* XXX is this done right? */
	ASN1_IMP_SEQUENCE_OF_OPT(CRMF_CERTREQMSG, regInfo, CRMF_ATTRIBUTETYPEANDVALUE, 1)
} ASN1_SEQUENCE_END(CRMF_CERTREQMSG)
IMPLEMENT_ASN1_FUNCTIONS(CRMF_CERTREQMSG)


#if 0
ASN1_ITEM_TEMPLATE(CRMF_CERTREQMESSAGES) =
	ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, CertReqMessages, CRMF_CERTREQMSG)
	ASN1_ITEM_TEMPLATE_END(CRMF_CERTREQMESSAGES)

IMPLEMENT_ASN1_FUNCTIONS(CRMF_CERTREQMESSAGES)
#endif

