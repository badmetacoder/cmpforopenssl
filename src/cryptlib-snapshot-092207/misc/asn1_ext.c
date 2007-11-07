/****************************************************************************
*																			*
*					ASN.1 Supplemental Read/Write Routines					*
*						Copyright Peter Gutmann 1992-2006					*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "asn1.h"
  #include "asn1_ext.h"
#else
  #include "misc/asn1.h"
  #include "misc/asn1_ext.h"
#endif /* Compiler-specific includes */

/****************************************************************************
*																			*
*							Object Identifier Routines						*
*																			*
****************************************************************************/

/* A table mapping OID's to algorithm types.  We take advantage of the fact
   that object identifiers were designed to be handled in the encoded form
   (without any need for decoding) and compare expected OID's with the raw
   encoded form.  Some OID's are for pure algorithms, others are for aWithB
   type combinations (usually encryption + hash), in this case the algorithm
   is the encryption algorithm and the subAlgorithm is the hash algorithm.

   There are multiple OID's for RSA, the main ones being rsa (which doesn't
   specify an exact data format and is deprecated), rsaEncryption (as per
   PKCS #1, recommended), and rsaSignature (ISO 9796).  We use rsaEncryption
   and its derived forms (e.g. md5WithRSAEncryption) rather than alternatives
   like md5WithRSA.  There is also an OID for rsaKeyTransport that uses
   PKCS #1 padding but isn't defined by RSADSI.

   There are a great many OIDs for DSA and/or SHA.  We list the less common
   ones after all the other OIDs so that we always encode the more common
   form, but can decode many forms (there are even more OIDs for SHA or DSA
   with common parameters that we don't bother with).

   AES has a whole series of OIDs that vary depending on the key size used,
   this isn't of any use since we can tell the keysize from other places so
   we just treat them all as a generic single AES OID */

typedef struct {
	const CRYPT_ALGO_TYPE algorithm;	/* The basic algorithm */
	const int parameter;				/* The algorithm subtype or mode */
	const BYTE FAR_BSS *oid;			/* The OID for this algorithm */
	} ALGOID_INFO;

static const ALGOID_INFO FAR_BSS algoIDinfoTbl[] = {
	/* RSA and <hash>WithRSA */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x01" ) },
	  /* rsaEncryption (1 2 840 113549 1 1 1) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_MD2,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x02" ) },
	  /* md2withRSAEncryption (1 2 840 113549 1 1 2) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_MD4,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x03" ) },
	  /* md4withRSAEncryption (1 2 840 113549 1 1 3) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_MD5,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x04" ) },
	  /* md5withRSAEncryption (1 2 840 113549 1 1 4) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x05" ) },
	  /* sha1withRSAEncryption (1 2 840 113549 1 1 5) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x06\x2B\x24\x03\x03\x01\x01" ) },
	  /* Another rsaSignatureWithsha1 (1 3 36 3 3 1 1) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_RIPEMD160,
	  MKOID( "\x06\x06\x2B\x24\x03\x03\x01\x02" ) },
	  /* rsaSignatureWithripemd160 (1 3 36 3 3 1 2) */
#ifdef USE_SHA2
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_SHA2,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x0B" ) },
	  /* sha256withRSAEncryption (1 2 840 113549 1 1 11) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_SHA2,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x0C" ) },
	  /* sha384withRSAEncryption (1 2 840 113549 1 1 12) */
	{ CRYPT_ALGO_RSA, CRYPT_ALGO_SHA2,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x01\x0D" ) },
	  /* sha512withRSAEncryption (1 2 840 113549 1 1 13) */
#endif /* USE_SHA2 */
#ifdef USE_DSA
	/* DSA and dsaWith<hash> */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x07\x2A\x86\x48\xCE\x38\x04\x01" ) },
	  /* dsa (1 2 840 10040 4 1) */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x0C" ) },
	  /* Peculiar deprecated dsa (1 3 14 3 2 12), but used by CDSA and the
	     German PKI profile */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x07\x2A\x86\x48\xCE\x38\x04\x03" ) },
	  /* dsaWithSha1 (1 2 840 10040 4 3) */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x1B" ) },
	  /* Another dsaWithSHA1 (1 3 14 3 2 27) */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x02\x01\x01\x02" ) },
	  /* Yet another dsaWithSHA-1 (2 16 840 1 101 2 1 1 2) */
	{ CRYPT_ALGO_DSA, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x0D" ) },
	  /* When they ran out of valid dsaWithSHA's, they started using invalid
	     ones.  This one is from JDK 1.1 and is actually dsaWithSHA, but it's
		 used as if it were dsaWithSHA-1 (1 3 14 3 2 13) */
#endif /* USE_DSA */

	/* Elgamal and elgamalWith<hash>.  The latter will never actually be
	   used since we won't be doing Elgamal signing, only key exchange */
#ifdef USE_ELGAMAL
	{ CRYPT_ALGO_ELGAMAL, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x0A\x2B\x06\x01\x04\x01\x97\x55\x01\x02\x01" ) },
	  /* elgamal (1 3 6 1 4 1 3029 1 2 1) */
	{ CRYPT_ALGO_ELGAMAL, CRYPT_ALGO_SHA,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x97\x55\x01\x02\x01\x01" ) },
	  /* elgamalWithSHA-1 (1 3 6 1 4 1 3029 1 2 1 1) */
	{ CRYPT_ALGO_ELGAMAL, CRYPT_ALGO_RIPEMD160,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x97\x55\x01\x02\x01\x02" ) },
	  /* elgamalWithRIPEMD-160 (1 3 6 1 4 1 3029 1 2 1 2) */
#endif /* USE_ELGAMAL */

#ifdef USE_DH
	/* DH */
	{ CRYPT_ALGO_DH, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x07\x2A\x86\x48\xCE\x3E\x02\x01" ) },
	  /* dhPublicKey (1 2 840 10046 2 1) */
#endif /* USE_DH */

	/* KEA */
#ifdef USE_KEA
	{ CRYPT_ALGO_KEA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x02\x01\x01\x16" ) },
	  /* keyExchangeAlgorithm (2 16 840 1 101 2 1 1 22) */
#endif /* USE_KEA */

	/* Hash algorithms */
#ifdef USE_MD2
	{ CRYPT_ALGO_MD2, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x02\x02" ) },
	  /* md2 (1 2 840 113549 2 2) */
	{ CRYPT_ALGO_MD2, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x0B\x60\x86\x48\x01\x86\xF8\x37\x01\x02\x08\x28" ) },
	  /* Another md2 (2 16 840 1 113719 1 2 8 40) */
#endif /* USE_MD2 */
#ifdef USE_MD4
	{ CRYPT_ALGO_MD4, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x02\x04" ) },
	  /* md4 (1 2 840 113549 2 4) */
	{ CRYPT_ALGO_MD4, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x02\x82\x06\x01\x0A\x01\x03\x01" ) },
	  /* Another md4 (0 2 262 1 10 1 3 1) */
	{ CRYPT_ALGO_MD4, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x0B\x60\x86\x48\x01\x86\xF8\x37\x01\x02\x08\x5F" ) },
	  /* Yet another md4 (2 16 840 1 113719 1 2 8 95) */
#endif /* USE_MD4 */
#ifdef USE_MD5
	{ CRYPT_ALGO_MD5, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x02\x05" ) },
	  /* md5 (1 2 840 113549 2 5) */
	{ CRYPT_ALGO_MD5, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x02\x82\x06\x01\x0A\x01\x03\x02" ) },
	  /* Another md5 (0 2 262 1 10 1 3 2) */
	{ CRYPT_ALGO_MD5, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x0B\x60\x86\x48\x01\x86\xF8\x37\x01\x02\x08\x32" ) },
	  /* Yet another md5 (2 16 840 1 113719 1 2 8 50) */
#endif /* USE_MD5 */
	{ CRYPT_ALGO_SHA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x1A" ) },
	  /* sha1 (1 3 14 3 2 26) */
	{ CRYPT_ALGO_SHA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x0B\x60\x86\x48\x01\x86\xF8\x37\x01\x02\x08\x52" ) },
	  /* Another sha1 (2 16 840 1 113719 1 2 8 82) */
#ifdef USE_RIPEMD160
	{ CRYPT_ALGO_RIPEMD160, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x05\x2B\x24\x03\x02\x01" ) },
	  /* ripemd160 (1 3 36 3 2 1) */
	{ CRYPT_ALGO_RIPEMD160, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x02\x82\x06\x01\x0A\x01\x03\x08" ) },
	  /* Another ripemd160 (0 2 262 1 10 1 3 8) */
#endif /* USE_RIPEMD160 */
#ifdef USE_SHA2
	{ CRYPT_ALGO_SHA2, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x01" ) },
	  /* sha2-256 (2 16 840 1 101 3 4 2 1) */
	{ CRYPT_ALGO_SHA2, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x02" ) },
	  /* sha2-384 (2 16 840 1 101 3 4 2 2) */
	{ CRYPT_ALGO_SHA2, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x02\x03" ) },
	  /* sha2-512 (2 16 840 1 101 3 4 2 3) */
#endif /* USE_SHA2 */

	/* MAC algorithms */
#ifdef USE_HMAC_MD5
	{ CRYPT_ALGO_HMAC_MD5, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2B\x06\x01\x05\x05\x08\x01\x01" ) },
	  /* hmac-MD5 (1 3 6 1 5 5 8 1 1) */
#endif /* USE_HMAC_MD5 */
	{ CRYPT_ALGO_HMAC_SHA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2B\x06\x01\x05\x05\x08\x01\x02" ) },
	  /* hmac-SHA (1 3 6 1 5 5 8 1 2) */
	{ CRYPT_ALGO_HMAC_SHA, CRYPT_ALGO_NONE,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x02\x07" ) },
	  /* Another hmacWithSHA1 (1 2 840 113549 2 7) */

	/* Ciphers */
	{ CRYPT_ALGO_AES, CRYPT_MODE_ECB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x01" ) },
	  /* aes128-ECB (2 16 840 1 101 3 4 1 1) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_ECB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x15" ) },
	  /* aes192-ECB (2 16 840 1 101 3 4 1 21) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_ECB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x29" ) },
	  /* aes256-ECB (2 16 840 1 101 3 4 1 41) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x02" ) },
	  /* aes128-CBC (2 16 840 1 101 3 4 1 2) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x16" ) },
	  /* aes192-CBC (2 16 840 1 101 3 4 1 22) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x2A" ) },
	  /* aes256-CBC (2 16 840 1 101 3 4 1 42) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_OFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x03" ) },
	  /* aes128-OFB (2 16 840 1 101 3 4 1 3) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_OFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x17" ) },
	  /* aes192-OFB (2 16 840 1 101 3 4 1 23) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_OFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x2B" ) },
	  /* aes256-OFB (2 16 840 1 101 3 4 1 43) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x04" ) },
	  /* aes128-CFB (2 16 840 1 101 3 4 1 4) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x18" ) },
	  /* aes192-CFB (2 16 840 1 101 3 4 1 24) */
	{ CRYPT_ALGO_AES, CRYPT_MODE_CFB,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x03\x04\x01\x2C" ) },
	  /* aes256-CFB (2 16 840 1 101 3 4 1 44) */
	{ CRYPT_ALGO_BLOWFISH, CRYPT_MODE_ECB,
	  MKOID( "\x06\x0A\x2B\x06\x01\x04\x01\x97\x55\x01\x01\x01" ) },
	  /* blowfishECB (1 3 6 1 4 1 3029 1 1 1) */
	{ CRYPT_ALGO_BLOWFISH, CRYPT_MODE_CBC,
	  MKOID( "\x06\x0A\x2B\x06\x01\x04\x01\x97\x55\x01\x01\x02" ) },
	  /* blowfishCBC (1 3 6 1 4 1 3029 1 1 2) */
	{ CRYPT_ALGO_BLOWFISH, CRYPT_MODE_CFB,
	  MKOID( "\x06\x0A\x2B\x06\x01\x04\x01\x97\x55\x01\x01\x03" ) },
	  /* blowfishCFB (1 3 6 1 4 1 3029 1 1 3) */
	{ CRYPT_ALGO_BLOWFISH, CRYPT_MODE_OFB,
	  MKOID( "\x06\x0A\x2B\x06\x01\x04\x01\x97\x55\x01\x01\x04" ) },
	  /* blowfishOFB (1 3 6 1 4 1 3029 1 1 4) */
	{ CRYPT_ALGO_CAST, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x2A\x86\x48\x86\xF6\x7D\x07\x42\x0A" ) },
	  /* cast5CBC (1 2 840 113533 7 66 10) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_ECB,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x06" ) },
	  /* desECB (1 3 14 3 2 6) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_ECB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x02\x01" ) },
	  /* Another desECB (0 2 262 1 10 1 2 2 1) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x07" ) },
	  /* desCBC (1 3 14 3 2 7) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x02\x02" ) },
	  /* Another desCBC (0 2 262 1 10 1 2 2 2) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_OFB,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x08" ) },
	  /* desOFB (1 3 14 3 2 8) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_OFB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x02\x03" ) },
	  /* Another desOFB (0 2 262 1 10 1 2 2 3) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_CFB,
	  MKOID( "\x06\x05\x2B\x0E\x03\x02\x09" ) },
	  /* desCFB (1 3 14 3 2 9) */
	{ CRYPT_ALGO_DES, CRYPT_MODE_CFB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x02\x05" ) },
	  /* Another desCFB (0 2 262 1 10 1 2 2 5) */
	{ CRYPT_ALGO_3DES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x07" ) },
	  /* des-EDE3-CBC (1 2 840 113549 3 7) */
	{ CRYPT_ALGO_3DES, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x03\x02" ) },
	  /* Another des3CBC (0 2 262 1 10 1 2 3 2) */
#ifdef USE_IDEA
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_ECB,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x81\x3C\x07\x01\x01\x01" ) },
	  /* ideaECB (1 3 6 1 4 1 188 7 1 1 1) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_ECB,
	  MKOID( "\x06\x06\x2B\x24\x03\x01\x02\x01" ) },
	  /* Another ideaECB (1 3 36 3 1 2 1) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_ECB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x05\x01" ) },
	  /* Yet another ideaECB (0 2 262 1 10 1 2 5 1) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CBC,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x81\x3C\x07\x01\x01\x02" ) },
	  /* ideaCBC (1 3 6 1 4 1 188 7 1 1 2) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CBC,
	  MKOID( "\x06\x06\x2B\x24\x03\x01\x02\x02" ) },
	  /* Another ideaCBC (1 3 36 3 1 2 2) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x05\x02" ) },
	  /* Yet another ideaCBC (0 2 262 1 10 1 2 5 2) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_OFB,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x81\x3C\x07\x01\x01\x04" ) },
	  /* ideaOFB (1 3 6 1 4 1 188 7 1 1 4) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_OFB,
	  MKOID( "\x06\x06\x2B\x24\x03\x01\x02\x03" ) },
	  /* Another ideaOFB (1 3 36 3 1 2 3) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_OFB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x05\x03" ) },
	  /* Yet another ideaOFB (0 2 262 1 10 1 2 5 3) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CFB,
	  MKOID( "\x06\x0B\x2B\x06\x01\x04\x01\x81\x3C\x07\x01\x01\x03" ) },
	  /* ideaCFB (1 3 6 1 4 1 188 7 1 1 3) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CFB,
	  MKOID( "\x06\x06\x2B\x24\x03\x01\x02\x04" ) },
	  /* Another ideaCFB (1 3 36 3 1 2 4) */
	{ CRYPT_ALGO_IDEA, CRYPT_MODE_CFB,
	  MKOID( "\x06\x09\x02\x82\x06\x01\x0A\x01\x02\x05\x05" ) },
	  /* Yet another ideaCFB (0 2 262 1 10 1 2 5 5) */
#endif /* USE_IDEA */
#ifdef USE_RC2
	{ CRYPT_ALGO_RC2, CRYPT_MODE_CBC,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x02" ) },
	  /* rc2CBC (1 2 840 113549 3 2) */
	{ CRYPT_ALGO_RC2, CRYPT_MODE_ECB,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x03" ) },
	  /* rc2ECB (1 2 840 113549 3 3) */
#endif /* USE_RC2 */
#ifdef USE_RC4
	{ CRYPT_ALGO_RC4, CRYPT_MODE_OFB,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x04" ) },
	  /* rc4 (1 2 840 113549 3 4) */
#endif /* USE_RC4 */
#ifdef USE_RC5
	{ CRYPT_ALGO_RC5, CRYPT_MODE_CBC,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x09" ) },
	  /* rC5-CBCPad (1 2 840 113549 3 9) */
	{ CRYPT_ALGO_RC5, CRYPT_MODE_CBC,
	  MKOID( "\x06\x08\x2A\x86\x48\x86\xF7\x0D\x03\x08" ) },
	  /* rc5CBC (sometimes used interchangeably with the above) (1 2 840 113549 3 8) */
#endif /* USE_RC5 */
#ifdef USE_SKIPJACK
	{ CRYPT_ALGO_SKIPJACK, CRYPT_MODE_CBC,
	  MKOID( "\x06\x09\x60\x86\x48\x01\x65\x02\x01\x01\x04" ) },
	  /* fortezzaConfidentialityAlgorithm (2 16 840 1 101 2 1 1 4) */
#endif /* USE_SKIPJACK */

	{ CRYPT_ALGO_NONE, CRYPT_ALGO_NONE, NULL },
	{ CRYPT_ALGO_NONE, CRYPT_ALGO_NONE, NULL }
	};

/* Map an OID to an algorithm type.  The parameter value can be NULL if no
   sub-algorithm is expected, but we return an error code if the OID has a
   sub-algorithm type present */

static CRYPT_ALGO_TYPE oidToAlgorithm( const BYTE *oid, const int oidLength,
									   int *parameter )
	{
	const BYTE oidByte = oid[ 6 ];
	int i;

	assert( oidLength >= 7 );

	/* Clear the return value */
	if( parameter != NULL )
		*parameter = CRYPT_ALGO_NONE;

	/* Look for a matching OID.  For quick-reject matching we check the byte
	   furthest inside the OID that's likely to not match, this rejects the
	   majority of mismatches without requiring a full comparison */
	for( i = 0; algoIDinfoTbl[ i ].algorithm != CRYPT_ALGO_NONE && \
				i < FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ); i++ )
		{
		const ALGOID_INFO *algoIDinfoPtr = &algoIDinfoTbl[ i ];

		if( sizeofOID( algoIDinfoPtr->oid ) == oidLength && \
			algoIDinfoPtr->oid[ 6 ] == oidByte && \
			!memcmp( algoIDinfoPtr->oid, oid, oidLength ) )
			{
			/* If we're expecting a sub-algorithm, return the sub-algorithm
			   type alongside the main algorithm type */
			if( parameter != NULL )
				{
				*parameter = algoIDinfoPtr->parameter;
				return( algoIDinfoPtr->algorithm );
				}

			/* If we're not expecting a sub-algorithm but there's one
			   present, mark it as an error */
			if( algoIDinfoPtr->parameter != CRYPT_ALGO_NONE )
				return( CRYPT_ALGO_NONE );

			return( algoIDinfoPtr->algorithm );
			}
		}
	if( i >= FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		retIntError_Ext( CRYPT_ALGO_NONE );

	return( CRYPT_ALGO_NONE );
	}

/* Map an algorithm and optional sub-algorithm/mode to an OID.  These
   functions are almost identical, the only difference is that the one used
   for checking only doesn't throw an exception when it encounters an
   algorithm value that it can't encode as an OID */

static const BYTE *algorithmToOID( const CRYPT_ALGO_TYPE algorithm,
								   const int parameter )
	{
	int i;

	for( i = 0; algoIDinfoTbl[ i ].algorithm != CRYPT_ALGO_NONE && \
				i < FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ); i++ )
		{
		if( algoIDinfoTbl[ i ].algorithm == algorithm )
			break;
		}
	if( i >= FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		retIntError_Null();
	while( algoIDinfoTbl[ i ].algorithm == algorithm && \
		   i < FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		{
		if( algoIDinfoTbl[ i ].parameter == parameter )
			return( algoIDinfoTbl[ i ].oid );
		i++;
		}
	if( i >= FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		retIntError_Null();

	assert( NOTREACHED );
	return( NULL );	/* Get rid of compiler warning */
	}

static const BYTE *algorithmToOIDcheck( const CRYPT_ALGO_TYPE algorithm,
										const int parameter )
	{
	int i;

	for( i = 0; algoIDinfoTbl[ i ].algorithm != CRYPT_ALGO_NONE && \
				i < FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ); i++ )
		{
		if( algoIDinfoTbl[ i ].algorithm == algorithm )
			break;
		}
	if( i >= FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		retIntError_Null();
	while( algoIDinfoTbl[ i ].algorithm == algorithm && \
		   i < FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		{
		if( algoIDinfoTbl[ i ].parameter == parameter )
			return( algoIDinfoTbl[ i ].oid );
		i++;
		}
	if( i >= FAILSAFE_ARRAYSIZE( algoIDinfoTbl, ALGOID_INFO ) )
		retIntError_Null();

	return( NULL );
	}

/* Read the start of an AlgorithmIdentifier record, used by a number of
   routines.  The 'parameter' member can be either a CRYPT_ALGO_TYPE or a 
   CRYPT_MODE_TYPE, which is why it's given as a generic integer rather than 
   a more specific type */

static int readAlgoIDheader( STREAM *stream, CRYPT_ALGO_TYPE *algorithm,
							 int *parameter, int *extraLength,
							 const int tag )
	{
	CRYPT_ALGO_TYPE cryptAlgo;
	BYTE oidBuffer[ MAX_OID_SIZE + 8 ];
	int oidLength, algoParam, length, status;

	/* Clear the return values */
	if( algorithm != NULL )
		*algorithm = CRYPT_ALGO_NONE;
	if( parameter != NULL )
		*parameter = CRYPT_ALGO_NONE;
	if( extraLength != NULL )
		*extraLength = 0;

	/* Determine the algorithm information based on the AlgorithmIdentifier
	   field */
	if( tag == DEFAULT_TAG )
		readSequence( stream, &length );
	else
		readConstructed( stream, &length, tag );
	status = readEncodedOID( stream, oidBuffer, &oidLength, MAX_OID_SIZE,
							 BER_OBJECT_IDENTIFIER );
	if( cryptStatusError( status ) )
		return( status );
	length -= oidLength;
	if( oidLength != sizeofOID( oidBuffer ) || length < 0 )
		/* It's a stream-related error, make it persistent */
		return( sSetError( stream, CRYPT_ERROR_BADDATA ) );
	if( ( cryptAlgo = oidToAlgorithm( oidBuffer, oidLength, \
									  &algoParam ) ) == CRYPT_ALGO_NONE )
		return( CRYPT_ERROR_NOTAVAIL );
	if( algorithm != NULL )
		*algorithm = cryptAlgo;
	if( parameter != NULL )
		*parameter = algoParam;

	/* If the caller has specified that there should be no parameters 
	   present, make sure that there's either no data or an ASN.1 NULL
	   present, and nothing else */
	if( extraLength == NULL )
		return( ( length > 0 ) ? readNull( stream ) : CRYPT_OK );

	/* If the parameters are null parameters, check them and exit */
	if( length == sizeofNull() )
		return( readNull( stream ) );

	/* Handle any remaining parameters */
	*extraLength = ( int ) length;
	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*					EncryptionAlgorithmIdentifier Routines					*
*																			*
****************************************************************************/

/* EncryptionAlgorithmIdentifier parameters:

	aesXcbc, aesXofb: AES FIPS

		iv				OCTET STRING SIZE (16)

	aesXcfb: AES FIPS

		SEQUENCE {
			iv			OCTET STRING SIZE (16),
			noOfBits	INTEGER (128)
			}

	cast5cbc: RFC 2144
		SEQUENCE {
			iv			OCTET STRING DEFAULT 0,
			keyLen		INTEGER (128)
			}

	blowfishCBC, desCBC, desEDE3-CBC: Blowfish RFC/OIW
		iv				OCTET STRING SIZE (8)

	blowfishCFB, blowfishOFB, desCFB, desOFB: Blowfish RFC/OIW
		SEQUENCE {
			iv			OCTET STRING SIZE (8),
			noBits		INTEGER (64)
			}

	ideaCBC: Ascom Tech
		SEQUENCE {
			iv			OCTET STRING OPTIONAL
			}

	ideaCFB: Ascom Tech
		SEQUENCE {
			r	  [ 0 ]	INTEGER DEFAULT 64,
			k	  [ 1 ]	INTEGER DEFAULT 64,
			j	  [ 2 ]	INTEGER DEFAULT 64,
			iv	  [ 3 ]	OCTET STRING OPTIONAL
			}

	ideaOFB: Ascom Tech
		SEQUENCE {
			j			INTEGER DEFAULT 64,
			iv			OCTET STRING OPTIONAL
			}

	rc2CBC: RFC 2311
		SEQUENCE {
			rc2Param	INTEGER (58),	-- 128 bit key
			iv			OCTET STRING SIZE (8)
			}

	rc4: (Unsure where this one is from)
		NULL

	rc5: RFC 2040
		SEQUENCE {
			version		INTEGER (16),
			rounds		INTEGER (12),
			blockSize	INTEGER (64),
			iv			OCTET STRING OPTIONAL
			}

	skipjackCBC: SDN.701
		SEQUENCE {
			iv			OCTET STRING
			}

   Because of the somewhat haphazard nature of encryption
   AlgorithmIdentifier definitions, we can only handle the following
   algorithm/mode combinations:

	AES ECB, CBC, CFB, OFB
	Blowfish ECB, CBC, CFB, OFB
	CAST128 CBC
	DES ECB, CBC, CFB, OFB
	3DES ECB, CBC, CFB, OFB
	IDEA ECB, CBC, CFB, OFB
	RC2 ECB, CBC
	RC4
	RC5 CBC
	Skipjack CBC */

/* Magic value to denote 128-bit RC2 keys */

#define RC2_KEYSIZE_MAGIC		58

/* Read an EncryptionAlgorithmIdentifier/DigestAlgorithmIdentifier */

static int readAlgoIDInfo( STREAM *stream, QUERY_INFO *queryInfo,
						   const int tag )
	{
	int mode, length, status;

	/* Read the AlgorithmIdentifier header and OID */
	status = readAlgoIDheader( stream, &queryInfo->cryptAlgo, &mode,
							   &length, tag );
	if( cryptStatusError( status ) )
		return( status );
	queryInfo->cryptMode = mode;

	/* Some broken implementations use sign + hash algoIDs in places where
	   a hash algoID is called for, if we find one of these we modify the
	   read AlgorithmIdentifier information to make it look like a hash
	   algoID */
	if( ( queryInfo->cryptAlgo >= CRYPT_ALGO_FIRST_PKC && \
		  queryInfo->cryptAlgo <= CRYPT_ALGO_LAST_PKC ) && \
		( queryInfo->cryptMode >= CRYPT_ALGO_FIRST_HASH && \
		  queryInfo->cryptMode <= CRYPT_ALGO_LAST_HASH ) )
		{
		queryInfo->cryptAlgo = ( CRYPT_ALGO_TYPE ) queryInfo->cryptMode;
		queryInfo->cryptMode = CRYPT_MODE_NONE;
		}

	/* Hash algorithms will either have NULL parameters or none at all
	   depending on which interpretation of which standard the sender used,
	   so if it's not a conventional encryption algorithm we just skip any
	   remaining parameter data and return */
	if( ( queryInfo->cryptAlgo >= CRYPT_ALGO_FIRST_HASH && \
		  queryInfo->cryptAlgo <= CRYPT_ALGO_LAST_HASH ) || \
		( queryInfo->cryptAlgo >= CRYPT_ALGO_FIRST_MAC && \
		  queryInfo->cryptAlgo <= CRYPT_ALGO_LAST_MAC ) )
		return( ( length > 0 ) ? sSkip( stream, length ) : CRYPT_OK );

	/* If it's not a hash/MAC algorithm, it has to be a conventional
	   encryption algorithm */
	if( queryInfo->cryptAlgo < CRYPT_ALGO_FIRST_CONVENTIONAL || \
		queryInfo->cryptAlgo > CRYPT_ALGO_LAST_CONVENTIONAL )
		return( CRYPT_ERROR_NOTAVAIL );

	/* Read the algorithm-specific parameters.  In theory we should do
	   something with some of the values like the IV size parameter, but
	   since the standard never explains what to do if it's something other
	   than the algorithm block size (Left pad? Right pad? Sign-extend?
	   Repeat the data?) it's safer not to do anything ("Never check for an
	   error but you don't know how to handle").  In any case there are no
	   known cases of these strange values ever being used (probably because
	   all existing software would break) so for now we just make sure that
	   they're present but otherwise ignore them */
	switch( queryInfo->cryptAlgo )
		{
		case CRYPT_ALGO_3DES:
		case CRYPT_ALGO_AES:
		case CRYPT_ALGO_BLOWFISH:
		case CRYPT_ALGO_DES:
			if( queryInfo->cryptMode == CRYPT_MODE_ECB )
				/* The NULL parameter has already been read in
				   readAlgoIDheader() */
				return( CRYPT_OK );
			if( ( queryInfo->cryptMode == CRYPT_MODE_CBC ) || \
				( queryInfo->cryptAlgo == CRYPT_ALGO_AES && \
				  queryInfo->cryptMode == CRYPT_MODE_OFB ) )
				return( readOctetString( stream, queryInfo->iv,
								&queryInfo->ivLength,
								( queryInfo->cryptAlgo == CRYPT_ALGO_AES ) ? \
									16 : 8, CRYPT_MAX_IVSIZE ) );
			readSequence( stream, NULL );
			readOctetString( stream, queryInfo->iv, &queryInfo->ivLength,
							 8, CRYPT_MAX_IVSIZE );
			return( readShortInteger( stream, NULL ) );

#ifdef USE_CAST
		case CRYPT_ALGO_CAST:
			readSequence( stream, NULL );
			readOctetString( stream, queryInfo->iv, &queryInfo->ivLength,
							 8, CRYPT_MAX_IVSIZE );
			return( readShortInteger( stream, NULL ) );
#endif /* USE_CAST */

#ifdef USE_IDEA
		case CRYPT_ALGO_IDEA:
			{
			int paramTag;

			if( queryInfo->cryptMode == CRYPT_MODE_ECB )
				/* The NULL parameter has already been read in
				   readAlgoIDheader() */
				return( CRYPT_OK );
			status = readSequence( stream, NULL );
			if( cryptStatusError( status ) )
				return( status );
			paramTag = peekTag( stream );
			if( queryInfo->cryptMode == CRYPT_MODE_CFB )
				{
				int itemsProcessed = 0;
				
				/* Skip the CFB r, k, and j parameters */
				while( ( paramTag == MAKE_CTAG_PRIMITIVE( 0 ) || \
						 paramTag == MAKE_CTAG_PRIMITIVE( 1 ) || \
						 paramTag == MAKE_CTAG_PRIMITIVE( 2 ) ) && \
					   itemsProcessed++ < 4 )
					{
					long value;

					status = readShortIntegerTag( stream, &value, paramTag );
					if( cryptStatusError( status ) )
						return( status );
					if( value != 64 )
						return( CRYPT_ERROR_NOTAVAIL );
					paramTag = peekTag( stream );
					}
				if( itemsProcessed >= 4 )
					return( CRYPT_ERROR_BADDATA );
				return( readOctetStringTag( stream, queryInfo->iv,
											&queryInfo->ivLength,
											8, CRYPT_MAX_IVSIZE, 3 ) );
				}
			if( queryInfo->cryptMode == CRYPT_MODE_OFB && \
				paramTag == BER_INTEGER )
				{
				long value;

				/* Skip the OFB j parameter */
				status = readShortInteger( stream, &value );
				if( cryptStatusError( status ) )
					return( status );
				if( value != 64 )
					return( CRYPT_ERROR_NOTAVAIL );
				}
			return( readOctetString( stream, queryInfo->iv,
									 &queryInfo->ivLength,
									 8, CRYPT_MAX_IVSIZE ) );
			}
#endif /* USE_CAST */

#ifdef USE_RC2
		case CRYPT_ALGO_RC2:
			/* In theory we should check that the parameter value ==
			   RC2_KEYSIZE_MAGIC (corresponding to a 128-bit key) but in
			   practice this doesn't really matter, we just use whatever we
			   find inside the PKCS #1 padding */
			readSequence( stream, NULL );
			if( queryInfo->cryptMode != CRYPT_MODE_CBC )
				return( readShortInteger( stream, NULL ) );
			readShortInteger( stream, NULL );
			return( readOctetString( stream, queryInfo->iv,
									 &queryInfo->ivLength,
									 8, CRYPT_MAX_IVSIZE ) );
#endif /* USE_RC2 */

#ifdef USE_RC4
		case CRYPT_ALGO_RC4:
			/* The NULL parameter has already been read in
			   readAlgoIDheader() */
			return( CRYPT_OK );
#endif /* USE_RC4 */

#ifdef USE_RC5
		case CRYPT_ALGO_RC5:
			{
			long val1, val2, val3;

			readSequence( stream, NULL );
			readShortInteger( stream, &val1 );			/* Version */
			readShortInteger( stream, &val2 );			/* Rounds */
			status = readShortInteger( stream, &val3 );	/* Block size */
			if( cryptStatusError( status ) )
				return( status );
			if( val1 != 16 || val2 != 12 || val3 != 64 )
				/* This algorithm makes enough of a feature of its variable
				   parameters that we do actually check to make sure that
				   they're sensible, since it may just be possible that
				   someone playing with an implementation decides to use
				   weird values */
				return( CRYPT_ERROR_NOTAVAIL );
			return( readOctetString( stream, queryInfo->iv,
									 &queryInfo->ivLength,
									 8, CRYPT_MAX_IVSIZE ) );
			}
#endif /* USE_RC5 */

#ifdef USE_SKIPJACK
		case CRYPT_ALGO_SKIPJACK:
			readSequence( stream, NULL );
			return( readOctetString( stream, queryInfo->iv,
									 &queryInfo->ivLength,
									 8, CRYPT_MAX_IVSIZE ) );
#endif /* USE_SKIPJACK */
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_NOTAVAIL );
	}

/* Write an EncryptionAlgorithmIdentifier record */

static int writeContextCryptAlgoID( STREAM *stream,
									const CRYPT_CONTEXT iCryptContext )
	{
	const BYTE *oid;
	BYTE iv[ CRYPT_MAX_IVSIZE + 8 ];
	CRYPT_ALGO_TYPE algorithm;
	CRYPT_MODE_TYPE mode;
	int oidSize, ivSize = 0, sizeofIV = 0, paramSize, status;

	/* Extract the information that we need to write the
	   AlgorithmIdentifier */
	status = krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE,
							  &algorithm, CRYPT_CTXINFO_ALGO );
	if( cryptStatusOK( status ) )
		status = krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE,
								  &mode, CRYPT_CTXINFO_MODE );
	if( cryptStatusOK( status ) && !isStreamCipher( algorithm ) && \
		needsIV( mode ) )
		{
		MESSAGE_DATA msgData;

		setMessageData( &msgData, iv, CRYPT_MAX_IVSIZE );
		status = krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE_S,
								  &msgData, CRYPT_CTXINFO_IV );
		ivSize = msgData.length;
		sizeofIV = ( int ) sizeofObject( ivSize );
		}
	if( cryptStatusError( status ) )
		{
		assert( NOTREACHED );
		return( status );
		}
	if( ( oid = algorithmToOIDcheck( algorithm, mode ) ) == NULL )
		/* Some algorithm+mode combinations can't be encoded using the
		   available PKCS #7 OIDs, the best that we can do is return a
		   CRYPT_ERROR_NOTAVAIL */
		return( CRYPT_ERROR_NOTAVAIL );
	oidSize = sizeofOID( oid );

	/* Write the algorithm-specific parameters */
	switch( algorithm )
		{
		case CRYPT_ALGO_3DES:
		case CRYPT_ALGO_AES:
		case CRYPT_ALGO_BLOWFISH:
		case CRYPT_ALGO_DES:
			{
			const int noBits = ( algorithm == CRYPT_ALGO_AES ) ? 128 : 64;

			paramSize = ( mode == CRYPT_MODE_ECB ) ? sizeofNull() : \
				( ( mode == CRYPT_MODE_CBC ) || \
				  ( algorithm == CRYPT_ALGO_AES && mode == CRYPT_MODE_OFB ) ) ? \
				  sizeofIV : \
				  ( int ) sizeofObject( sizeofIV + sizeofShortInteger( noBits ) );
			writeSequence( stream, oidSize + paramSize );
			if( algorithm == CRYPT_ALGO_AES )
				{
				int keySize;

				/* AES uses a somewhat odd encoding in which the last byte
				   of the OID jumps in steps of 20 depending on the key
				   size, so we adjust the OID that we actually write based
				   on the key size.  It's somewhat unlikely that any
				   implementation actually cares about this since the size
				   information is always communicated anderswhere, but we do
				   it just in case */
				krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE,
								 &keySize, CRYPT_CTXINFO_KEYSIZE );
				swrite( stream, oid, oidSize - 1 );
				sputc( stream, oid[ oidSize - 1 ] + \
							   ( ( keySize == 16 ) ? 0 : \
								 ( keySize == 24 ) ? 20 : 40 ) );
				}
			else
				swrite( stream, oid, oidSize );
			if( mode == CRYPT_MODE_ECB )
				return( writeNull( stream, DEFAULT_TAG ) );
			if( ( mode == CRYPT_MODE_CBC ) || \
				( algorithm == CRYPT_ALGO_AES && mode == CRYPT_MODE_OFB ) )
				return( writeOctetString( stream, iv, ivSize, DEFAULT_TAG ) );
			writeSequence( stream, sizeofIV + sizeofShortInteger( noBits ) );
			writeOctetString( stream, iv, ivSize, DEFAULT_TAG );
			return( writeShortInteger( stream, noBits, DEFAULT_TAG ) );
			}

#ifdef USE_CAST
		case CRYPT_ALGO_CAST:
			paramSize = sizeofIV + sizeofShortInteger( 128 );
			writeSequence( stream, oidSize + \
								   ( int ) sizeofObject( paramSize ) );
			swrite( stream, oid, oidSize );
			writeSequence( stream, paramSize );
			writeOctetString( stream, iv, ivSize, DEFAULT_TAG );
			return( writeShortInteger( stream, 128, DEFAULT_TAG ) );
#endif /* USE_CAST */

#ifdef USE_IDEA
		case CRYPT_ALGO_IDEA:
			paramSize = ( mode == CRYPT_MODE_ECB ) ? \
						sizeofNull() : \
						( int ) sizeofObject( sizeofIV );
			writeSequence( stream, oidSize + paramSize );
			swrite( stream, oid, oidSize );
			if( mode == CRYPT_MODE_ECB )
				return( writeNull( stream, DEFAULT_TAG ) );
			writeSequence( stream, sizeofIV );
			return( writeOctetString( stream, iv, ivSize, \
									  ( mode == CRYPT_MODE_CFB ) ? \
										3 : DEFAULT_TAG ) );
#endif /* USE_IDEA */

#ifdef USE_RC2
		case CRYPT_ALGO_RC2:
			paramSize = ( ( mode == CRYPT_MODE_ECB ) ? 0 : sizeofIV ) + \
						sizeofShortInteger( RC2_KEYSIZE_MAGIC );
			writeSequence( stream, oidSize + \
								   ( int ) sizeofObject( paramSize ) );
			swrite( stream, oid, oidSize );
			writeSequence( stream, paramSize );
			if( mode != CRYPT_MODE_CBC )
				return( writeShortInteger( stream, RC2_KEYSIZE_MAGIC,
										   DEFAULT_TAG ) );
			writeShortInteger( stream, RC2_KEYSIZE_MAGIC, DEFAULT_TAG );
			return( writeOctetString( stream, iv, ivSize, DEFAULT_TAG ) );
#endif /* USE_RC2 */

#ifdef USE_RC4
		case CRYPT_ALGO_RC4:
			writeSequence( stream, oidSize + sizeofNull() );
			swrite( stream, oid, oidSize );
			return( writeNull( stream, DEFAULT_TAG ) );
#endif /* USE_RC4 */

#ifdef USE_RC5
		case CRYPT_ALGO_RC5:
			paramSize = sizeofShortInteger( 16 ) + \
						sizeofShortInteger( 12 ) + \
						sizeofShortInteger( 64 ) + \
						sizeofIV;
			writeSequence( stream, oidSize + \
								   ( int ) sizeofObject( paramSize ) );
			swrite( stream, oid, oidSize );
			writeSequence( stream, paramSize );
			writeShortInteger( stream, 16, DEFAULT_TAG );	/* Version */
			writeShortInteger( stream, 12, DEFAULT_TAG );	/* Rounds */
			writeShortInteger( stream, 64, DEFAULT_TAG );	/* Block size */
			return( writeOctetString( stream, iv, ivSize, DEFAULT_TAG ) );
#endif /* USE_RC5 */

#ifdef USE_SKIPJACK
		case CRYPT_ALGO_SKIPJACK:
			writeSequence( stream, oidSize + \
								   ( int ) sizeofObject( sizeofIV ) );
			swrite( stream, oid, oidSize );
			writeSequence( stream, sizeofIV );
			return( writeOctetString( stream, iv, ivSize, DEFAULT_TAG ) );
#endif /* USE_SKIPJACK */
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_NOTAVAIL );
	}

/****************************************************************************
*																			*
*							AlgorithmIdentifier Routines					*
*																			*
****************************************************************************/

/* Because AlgorithmIdentifiers are only defined for a subset of the
   algorithms that cryptlib supports, we have to check that the algorithm
   and mode being used can be represented in encoded data before we try to
   do anything with it */

BOOLEAN checkAlgoID( const CRYPT_ALGO_TYPE algorithm,
					 const CRYPT_MODE_TYPE mode )
	{
	return( ( algorithmToOIDcheck( algorithm, mode ) != NULL ) ? \
			TRUE : FALSE );
	}

/* Determine the size of an AlgorithmIdentifier record */

int sizeofAlgoIDex( const CRYPT_ALGO_TYPE algorithm,
					const int parameter, const int extraLength )
	{
	const BYTE *oid = algorithmToOID( algorithm, parameter );

	if( oid == NULL )
		{
		assert( NOTREACHED );
		return( 0 );
		}
	return( ( int ) sizeofObject( sizeofOID( oid ) + \
								  ( ( extraLength > 0 ) ? extraLength : \
														  sizeofNull() ) ) );
	}

int sizeofAlgoID( const CRYPT_ALGO_TYPE algorithm )
	{
	return( sizeofAlgoIDex( algorithm, CRYPT_ALGO_NONE, 0 ) );
	}

/* Write an AlgorithmIdentifier record */

int writeAlgoIDex( STREAM *stream, const CRYPT_ALGO_TYPE algorithm,
				   const int parameter, const int extraLength )
	{
	const BYTE *oid = algorithmToOID( algorithm, parameter );
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	if( oid == NULL )
		{
		assert( NOTREACHED );
		return( CRYPT_ERROR_NOTAVAIL );
		}

	/* Write the AlgorithmIdentifier field */
	writeSequence( stream, sizeofOID( oid ) + \
				   ( ( extraLength > 0 ) ? extraLength : sizeofNull() ) );
	status = swrite( stream, oid, sizeofOID( oid ) );
	if( extraLength > 0 )
		/* Parameters will be written by the caller */
		return( status );

	/* No extra parameters so we need to write a NULL */
	return( writeNull( stream, DEFAULT_TAG ) );
	}

int writeAlgoID( STREAM *stream, const CRYPT_ALGO_TYPE algorithm  )
	{
	return( writeAlgoIDex( stream, algorithm, CRYPT_ALGO_NONE, 0 ) );
	}

/* Read an AlgorithmIdentifier record.  There are three versions of 
   this:

	readAlgoID: Reads an algorithm, assumes that there are no algorithm 
		parameters present and returns an error if there are.

	readAlgoIDext: Reads an algorithm and secondary algorithm or mode, 
		assumes that there are no algorithm parameters present and returns 
		an error if there are.

	readAlgoIDparams: Reads an algorithm and the length of the extra 
		information */

int readAlgoID( STREAM *stream, CRYPT_ALGO_TYPE *algorithm )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( algorithm, sizeof( CRYPT_ALGO_TYPE ) ) );

	return( readAlgoIDheader( stream, algorithm, NULL, NULL, DEFAULT_TAG ) );
	}

int readAlgoIDext( STREAM *stream, CRYPT_ALGO_TYPE *algorithm,
				   CRYPT_ALGO_TYPE *altCryptAlgo )
	{
	int altAlgo, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( algorithm, sizeof( CRYPT_ALGO_TYPE ) ) );
	assert( isWritePtr( altCryptAlgo, sizeof( CRYPT_ALGO_TYPE ) ) );

	status = readAlgoIDheader( stream, algorithm, &altAlgo, NULL, DEFAULT_TAG );
	if( cryptStatusOK( status ) && ( altCryptAlgo != NULL ) )
		*altCryptAlgo = altAlgo;	/* enum vs. int conversion */
	return( status );
	}

int readAlgoIDparams( STREAM *stream, CRYPT_ALGO_TYPE *algorithm, 
					  int *extraLength )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( algorithm, sizeof( CRYPT_ALGO_TYPE ) ) );
	assert( isWritePtr( extraLength, sizeof( int ) ) );

	return( readAlgoIDheader( stream, algorithm, NULL, extraLength, DEFAULT_TAG ) );
	}

/* Determine the size of an AlgorithmIdentifier record from an encryption
   context */

int sizeofContextAlgoID( const CRYPT_CONTEXT iCryptContext,
						 const int parameter, const int flags )
	{
	int cryptAlgo, status;

	assert( isHandleRangeValid( iCryptContext ) );

	/* If it's a standard write, determine how large the algoID and
	   parameters are.  Because this is a rather complex operation, the
	   easiest way to do it is to write to a null stream and get its
	   size */
	if( flags == ALGOID_FLAG_NONE )
		{
		STREAM nullStream;

		sMemOpen( &nullStream, NULL, 0 );
		status = writeContextAlgoID( &nullStream, iCryptContext,
									 parameter, ALGOID_FLAG_NONE );
		if( cryptStatusOK( status ) )
			status = stell( &nullStream );
		sMemClose( &nullStream );
		return( status );
		}

	assert( flags == ALGOID_FLAG_ALGOID_ONLY );

	/* Write the algoID only */
	status = krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE,
							  &cryptAlgo, CRYPT_CTXINFO_ALGO );
	if( cryptStatusError( status ) )
		return( status );
	return( sizeofAlgoIDex( cryptAlgo, parameter, 0 ) );
	}

/* Write an AlgorithmIdentifier record from an encryption context */

int writeContextAlgoID( STREAM *stream, const CRYPT_CONTEXT iCryptContext,
						const int parameter, const int flags )
	{
	int cryptAlgo, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isHandleRangeValid( iCryptContext ) );

	status = krnlSendMessage( iCryptContext, IMESSAGE_GETATTRIBUTE,
							  &cryptAlgo, CRYPT_CTXINFO_ALGO );
	if( cryptStatusError( status ) )
		return( status );
	if( flags & ALGOID_FLAG_ALGOID_ONLY )
		return( writeAlgoIDex( stream, cryptAlgo, parameter, 0 ) );

	/* If we're writing parameters such as key and block sizes and IVs
	   alongside the algorithm identifier, it has to be a conventional
	   context */
	assert( parameter == CRYPT_ALGO_NONE );
	assert( cryptAlgo >= CRYPT_ALGO_FIRST_CONVENTIONAL && \
			cryptAlgo <= CRYPT_ALGO_LAST_CONVENTIONAL );

	return( writeContextCryptAlgoID( stream, iCryptContext ) );
	}

/* Turn an AlgorithmIdentifier into a hash/encryption context */

int readContextAlgoID( STREAM *stream, CRYPT_CONTEXT *iCryptContext,
					   QUERY_INFO *queryInfo, const int tag )
	{
	QUERY_INFO localQueryInfo, *queryInfoPtr = ( queryInfo == NULL ) ? \
											   &localQueryInfo : queryInfo;
	MESSAGE_CREATEOBJECT_INFO createInfo;
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( iCryptContext == NULL || \
			isWritePtr( iCryptContext, sizeof( CRYPT_CONTEXT ) ) );
	assert( queryInfo == NULL || \
			isWritePtr( queryInfo, sizeof( QUERY_INFO ) ) );

	/* Read the algorithm info.  If we're not creating a context from the
	   info, we're done */
	if( iCryptContext != NULL )
		*iCryptContext = CRYPT_ERROR;
	status = readAlgoIDInfo( stream, queryInfoPtr, tag );
	if( cryptStatusError( status ) || iCryptContext == NULL )
		return( status );

	/* Create the object from it */
	setMessageCreateObjectInfo( &createInfo, queryInfoPtr->cryptAlgo );
	status = krnlSendMessage( SYSTEM_OBJECT_HANDLE, IMESSAGE_DEV_CREATEOBJECT,
							  &createInfo, OBJECT_TYPE_CONTEXT );
	if( cryptStatusError( status ) )
		return( status );
	if( queryInfoPtr->cryptAlgo > CRYPT_ALGO_LAST_CONVENTIONAL )
		{
		/* If it's not a conventional encryption algorithm, we're done */
		*iCryptContext = createInfo.cryptHandle;
		return( CRYPT_OK );
		}
	status = krnlSendMessage( createInfo.cryptHandle, IMESSAGE_SETATTRIBUTE,
							  &queryInfoPtr->cryptMode, CRYPT_CTXINFO_MODE );
	if( cryptStatusOK( status ) && \
		!isStreamCipher( queryInfoPtr->cryptAlgo ) )
		{
		int ivLength;

		status = krnlSendMessage( createInfo.cryptHandle,
								  IMESSAGE_GETATTRIBUTE, &ivLength,
								  CRYPT_CTXINFO_IVSIZE );
		if( cryptStatusOK( status ) )
			{
			MESSAGE_DATA msgData;

			setMessageData( &msgData, queryInfoPtr->iv,
							min( ivLength, queryInfoPtr->ivLength ) );
			status = krnlSendMessage( createInfo.cryptHandle,
									  IMESSAGE_SETATTRIBUTE_S, &msgData,
									  CRYPT_CTXINFO_IV );
			}
		}
	if( cryptStatusError( status ) )
		{
		/* If there's an error in the parameters stored with the key we'll
		   get an arg or attribute error when we try to set the attribute so
		   we translate it into an error code which is appropriate for the
		   situation.  In addition since this is (arguably) a stream format
		   error (the data read from the stream is invalid), we also set the
		   stream status */
		krnlSendNotifier( createInfo.cryptHandle, IMESSAGE_DECREFCOUNT );
		if( cryptArgError( status ) )
			return( sSetError( stream, CRYPT_ERROR_BADDATA ) );
		}
	else
		*iCryptContext = createInfo.cryptHandle;
	return( status );
	}

/* Read/write a non-crypto algorithm identifier, used for things like 
   content types.  This just wraps the given OID up in the 
   AlgorithmIdentifier and writes it */

int readGenericAlgoID( STREAM *stream, const BYTE *oid )
	{
	int length, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( oid, sizeofOID( oid ) ) );

	/* Read the AlgorithmIdentifier wrapper and OID.  One possible 
	   complication here is the standard NULL vs.absent AlgorithmIdentifier 
	   parameter issue, to handle this we allow either option */
	status = readSequence( stream, &length );
	if( cryptStatusOK( status ) )
		{
		status = readFixedOID( stream, oid );
		length -= sizeofOID( oid );
		}
	if( cryptStatusOK( status ) && length > 0 )
		status = readNull( stream );
	return( status );
	}

int writeGenericAlgoID( STREAM *stream, const BYTE *oid )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( oid, sizeofOID( oid ) ) );

	writeSequence( stream, sizeofOID( oid ) );
	return( writeOID( stream, oid ) );
	}

/****************************************************************************
*																			*
*							Message Digest Routines							*
*																			*
****************************************************************************/

/* Read/write a message digest value.  This is another one of those oddball
   functions which is present here because it's the least inappropriate place
   to put it */

int writeMessageDigest( STREAM *stream, const CRYPT_ALGO_TYPE hashAlgo,
						const void *hash, const int hashSize )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( hashAlgo >= CRYPT_ALGO_FIRST_HASH && \
			hashAlgo <= CRYPT_ALGO_LAST_HASH );
	assert( isReadPtr( hash, hashSize ) );

	writeSequence( stream, sizeofAlgoID( hashAlgo ) + \
				   ( int ) sizeofObject( hashSize ) );
	writeAlgoID( stream, hashAlgo );
	return( writeOctetString( stream, hash, hashSize, DEFAULT_TAG ) );
	}

int readMessageDigest( STREAM *stream, CRYPT_ALGO_TYPE *hashAlgo,
					   void *hash, const int hashMaxLen, int *hashSize )
	{
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( hash, 16 ) );
	assert( isWritePtr( hashSize, sizeof( int ) ) );

	/* Clear the return values */
	memset( hash, 0, 16 );
	*hashSize = 0;

	/* Read the message digest, enforcing sensible size values */
	readSequence( stream, NULL );
	status = readAlgoID( stream, hashAlgo );
	if( cryptStatusError( status ) )
		return( status );
	return( readOctetString( stream, hash, hashSize, 16, hashMaxLen ) );
	}

/****************************************************************************
*																			*
*								CMS Header Routines							*
*																			*
****************************************************************************/

/* Read and write CMS headers.  When reading CMS headers we check a bit more
   than just the header OID, which means that we need to provide additional
   information alongside the OID information.  This is provided as
   CMS_CONTENT_INFO in the OID info extra data field */

int readCMSheader( STREAM *stream, const OID_INFO *oidInfo, long *dataSize,
				   const BOOLEAN isInnerHeader )
	{
	const OID_INFO *oidInfoPtr;
	BOOLEAN isData = FALSE;
	long length, value;
	int tag, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( oidInfo, sizeof( OID_INFO ) ) );
	assert( dataSize == NULL || isWritePtr( dataSize, sizeof( long ) ) );

	/* Clear the return value */
	if( dataSize != NULL )
		*dataSize = 0;

	/* Read the outer SEQUENCE and OID.  We can't use a normal
	   readSequence() here because the data length could be much longer than
	   the maximum allowed in the readSequence() sanity check */
	readLongSequence( stream, &length );
	status = readOIDEx( stream, oidInfo, &oidInfoPtr );
	if( cryptStatusError( status ) )
		return( status );

	/* If the content type is data, the content is an OCTET STRING rather
	   than a SEQUENCE so we remember the type for later.  Since there
	   are a pile of CMS OIDs of the same length as OID_CMS_DATA, we check 
	   for a match on the last byte before we perform a full OID match */
	assert( sizeofOID( OID_CMS_DATA ) == 11 );
	if( sizeofOID( oidInfoPtr->oid ) == sizeofOID( OID_CMS_DATA ) && \
		oidInfoPtr->oid[ 10 ] == OID_CMS_DATA[ 10 ] && \
		!memcmp( oidInfoPtr->oid, OID_CMS_DATA, \
				 sizeofOID( OID_CMS_DATA ) ) )
		isData = TRUE;

	/* Some Microsoft software produces an indefinite encoding for a single
	   OID so we have to check for this */
	if( length == CRYPT_UNUSED )
		{
		status = checkEOC( stream );
		if( cryptStatusError( status ) )
			return( status );
		if( status == TRUE )
			/* We've seen EOC octets, the item has zero length (for example
			   with a detached signature), we're done */
			return( oidInfoPtr->selectionID );
		}

	/* If the content is supplied externally (for example with a detached
	   signature), denoted by the fact that the total content consists only
	   of the OID, we're done */
	if( length != CRYPT_UNUSED && length <= sizeofOID( oidInfoPtr->oid ) )
		return( oidInfoPtr->selectionID );

	/* Read the content [0] tag and OCTET STRING/SEQUENCE.  This requires
	   some special-case handling, see the comment in writeCMSHeader() for
	   more details */
	status = readLongConstructed( stream, NULL, 0 );
	if( cryptStatusError( status ) )
		return( status );
	tag = peekTag( stream );
	if( isData )
		{
		/* It's pure data content, it must be an OCTET STRING */
		if( tag != BER_OCTETSTRING && \
			tag != ( BER_OCTETSTRING | BER_CONSTRUCTED ) )
			status = CRYPT_ERROR_BADDATA;
		}
	else
		if( isInnerHeader )
			{
			/* It's an inner header, it should be an OCTET STRING but
			   alternative interpretations are possible based on the old
			   PKCS #7 definition of inner content */
			if( tag != BER_OCTETSTRING && \
				tag != ( BER_OCTETSTRING | BER_CONSTRUCTED ) && \
				tag != BER_SEQUENCE )
				status = CRYPT_ERROR_BADDATA;
			}
		else
			/* It's an outer header containing other than data, it must be a
			   SEQUENCE */
			if( tag != BER_SEQUENCE )
				status = CRYPT_ERROR_BADDATA;
	if( cryptStatusError( status ) )
		return( sSetError( stream, status ) );
	status = readLongGenericHole( stream, &length, tag );
	if( cryptStatusError( status ) )
		return( status );
	if( dataSize != NULL )
		*dataSize = length;

	/* If it's structured (i.e. not data in an OCTET STRING), check the
	   version number of the content if required */
	if( !isData && oidInfoPtr->extraInfo != NULL )
		{
		const CMS_CONTENT_INFO *contentInfoPtr = oidInfoPtr->extraInfo;

		status = readShortInteger( stream, &value );
		if( cryptStatusError( status ) )
			return( status );
		if( value < contentInfoPtr->minVersion || \
			value > contentInfoPtr->maxVersion )
			return( sSetError( stream, CRYPT_ERROR_BADDATA ) );
		}

	return( oidInfoPtr->selectionID );
	}

int writeCMSheader( STREAM *stream, const BYTE *contentOID,
					const long dataSize, const BOOLEAN isInnerHeader )
	{
	BOOLEAN isOctetString = ( isInnerHeader || \
							  ( sizeofOID( contentOID ) == 11 && \
							  !memcmp( contentOID, OID_CMS_DATA, 11 ) ) ) ? \
							TRUE : FALSE;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contentOID, sizeofOID( contentOID ) ) );

	/* The handling of the wrapper type for the content is rather complex.
	   If it's an outer header, it's an OCTET STRING for data and a SEQUENCE
	   for everything else.  If it's an inner header it usually follows the
	   same rule, however for signed data the content was changed from

		content [0] EXPLICIT ANY DEFINED BY contentType OPTIONAL

	   in PKCS #7 to

		eContent [0] EXPLICIT OCTET STRING OPTIONAL

	   for CMS (it was always an OCTET STRING for encrypted data).  To
	   complicate things, there are some older implementations based on the
	   original PKCS #7 interpretation that use a SEQUENCE (namely
	   AuthentiCode).  To resolve this, we use an OCTET STRING for inner
	   content unless the content type is spcIndirectDataContext */
	if( isInnerHeader && sizeofOID( contentOID ) == 12 && \
		!memcmp( contentOID, OID_MS_SPCINDIRECTDATACONTEXT, 12 ) )
		isOctetString = FALSE;

	/* If a size is given, write the definite form */
	if( dataSize != CRYPT_UNUSED )
		{
		writeSequence( stream, sizeofOID( contentOID ) + ( ( dataSize > 0 ) ? \
					   ( int ) sizeofObject( sizeofObject( dataSize ) ) : 0 ) );
		writeOID( stream, contentOID );
		if( dataSize <= 0 )
			return( CRYPT_OK );	/* No content, exit */
		writeConstructed( stream, sizeofObject( dataSize ), 0 );
		if( isOctetString )
			return( writeOctetStringHole( stream, dataSize, DEFAULT_TAG ) );
		return( writeSequence( stream, dataSize ) );
		}

	/* No size given, write the indefinite form */
	writeSequenceIndef( stream );
	writeOID( stream, contentOID );
	writeCtag0Indef( stream );
	return( isOctetString ? writeOctetStringIndef( stream ) : \
							writeSequenceIndef( stream ) );
	}

/* Read and write an encryptedContentInfo header.  The inner content may be
   implicitly or explicitly tagged depending on the exact content type */

int sizeofCMSencrHeader( const BYTE *contentOID, const long dataSize,
						 const CRYPT_CONTEXT iCryptContext )
	{
	STREAM nullStream;
	int status, cryptInfoSize;

	assert( isReadPtr( contentOID, sizeofOID( contentOID ) ) );
	assert( isHandleRangeValid( iCryptContext ) );

	/* Determine the encoded size of the AlgorithmIdentifier */
	sMemOpen( &nullStream, NULL, 0 );
	status = writeContextCryptAlgoID( &nullStream, iCryptContext );
	cryptInfoSize = stell( &nullStream );
	sMemClose( &nullStream );
	if( cryptStatusError( status ) )
		return( status );

	/* Calculate the encoded size of the SEQUENCE + OID + AlgoID + [0] for
	   the definite or indefinite forms (the size 2 is for the tag + 0x80
	   indefinite-length indicator) */
	if( dataSize != CRYPT_UNUSED )
		return( ( int ) \
				( sizeofObject( sizeofOID( contentOID ) + \
								cryptInfoSize + sizeofObject( dataSize ) ) - dataSize ) );
	return( 2 + sizeofOID( contentOID ) + cryptInfoSize + 2 );
	}

int readCMSencrHeader( STREAM *stream, const OID_INFO *oidInfo,
					   CRYPT_CONTEXT *iCryptContext, QUERY_INFO *queryInfo )
	{
	QUERY_INFO localQueryInfo, *queryInfoPtr = ( queryInfo == NULL ) ? \
											   &localQueryInfo : queryInfo;
	long length;
	int selectionID, tag, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( oidInfo, sizeof( OID_INFO ) ) );
	assert( iCryptContext == NULL || \
			isWritePtr( iCryptContext, sizeof( CRYPT_CONTEXT ) ) );
	assert( queryInfo == NULL || \
			isWritePtr( queryInfo, sizeof( QUERY_INFO ) ) );

	/* Clear the return values */
	if( iCryptContext != NULL )
		*iCryptContext = CRYPT_ERROR;
	memset( queryInfoPtr, 0, sizeof( QUERY_INFO ) );

	/* Set up the basic query info fields.  Since this isn't a proper key 
	   exchange or signature object, we can't properly set up all of the
	   fields like type (it's not any CRYPT_OBJECT_TYPE) or version */
	queryInfoPtr->formatType = CRYPT_FORMAT_CMS;

	/* Read the outer SEQUENCE, OID, and AlgorithmIdentifier.  We can't use
	   a normal readSequence() here because the data length could be much
	   longer than the maximum allowed in the readSequence() sanity check */
	readLongSequence( stream, NULL );
	status = readOID( stream, oidInfo, &selectionID );
	if( cryptStatusOK( status ) )
		status = readContextAlgoID( stream, iCryptContext, queryInfoPtr,
									DEFAULT_TAG );
	if( cryptStatusError( status ) )
		return( status );

	/* Read the content [0] tag, which may be either primitive or constructed
	   depending on the content */
	tag = peekTag( stream );
	status = readLongGenericHole( stream, &length, tag );
	if( cryptStatusOK( status ) && \
		( tag != MAKE_CTAG( 0 ) && tag != MAKE_CTAG_PRIMITIVE( 0 ) ) )
		{
		sSetError( stream, CRYPT_ERROR_BADDATA );
		status = CRYPT_ERROR_BADDATA;
		}
	if( cryptStatusError( status ) )
		{
		if( iCryptContext != NULL )
			krnlSendNotifier( *iCryptContext, IMESSAGE_DECREFCOUNT );
		return( status );
		}
	queryInfoPtr->size = length;

	return( selectionID );
	}

int writeCMSencrHeader( STREAM *stream, const BYTE *contentOID,
						const long dataSize,
						const CRYPT_CONTEXT iCryptContext )
	{
	STREAM nullStream;
	int cryptInfoSize, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contentOID, sizeofOID( contentOID ) ) );
	assert( isHandleRangeValid( iCryptContext ) );

	/* Determine the encoded size of the AlgorithmIdentifier */
	sMemOpen( &nullStream, NULL, 0 );
	status = writeContextCryptAlgoID( &nullStream, iCryptContext );
	cryptInfoSize = stell( &nullStream );
	sMemClose( &nullStream );
	if( cryptStatusError( status ) )
		return( status );

	/* If a size is given, write the definite form */
	if( dataSize != CRYPT_UNUSED )
		{
		writeSequence( stream, sizeofOID( contentOID ) + cryptInfoSize + \
					   ( int ) sizeofObject( dataSize ) );
		writeOID( stream, contentOID );
		status = writeContextCryptAlgoID( stream, iCryptContext );
		if( cryptStatusError( status ) )
			return( status );
		return( writeOctetStringHole( stream, dataSize, \
									  MAKE_CTAG_PRIMITIVE( 0 ) ) );
		}

	/* No size given, write the indefinite form */
	writeSequenceIndef( stream );
	writeOID( stream, contentOID );
	status = writeContextCryptAlgoID( stream, iCryptContext );
	if( cryptStatusError( status ) )
		return( status );
	return( writeCtag0Indef( stream ) );
	}
