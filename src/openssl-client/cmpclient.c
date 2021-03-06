/* vim: set ts=2 sts=2 sw=2 expandtab: */
/* cmpclient.c
 * A simple example CMP client utilizing OpenSSL
 */

/* ====================================================================
 * Originally written by Martin Peylo for the OpenSSL project.
 * <martin dot peylo at nsn dot com>
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
 * Copyright 2007-2010 Nokia Siemens Networks Oy. ALL RIGHTS RESERVED.
 * CMP support in OpenSSL originally developed by 
 * Nokia Siemens Networks for contribution to the OpenSSL project.
 */

/* =========================== CHANGE LOG =============================
 * 2007 - Martin Peylo - Initial Creation
 * 2008 - Sami Lehtonen - added the use of optional OpenSSL Engine and CR
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/cmp.h>
#include <openssl/bio.h>
#include <openssl/rsa.h>
#include <openssl/dsa.h>

#include <openssl/engine.h>

#include <cmpclient.h>
#include <cmpclient_help.h>

#define OPENSSL_LOAD_CONF     1

/* ############################################################################ */
/* ############################################################################ */

/* set by CLA */
static int verbose_flag;
static int   opt_serverPort=0;
static char* opt_serverName=NULL;
static char* opt_serverPath=NULL;
static char* opt_httpProxy=NULL;
static char* opt_srvCertFile=NULL;
static char* opt_caPubsDir=NULL;
static char* opt_clCertFile=NULL;
static char* opt_newClCertFile=NULL;
static char* opt_clKeyFile=NULL;
static char* opt_clKeyPass="";
static char* opt_newClKeyPass="";
static char* opt_newClKeyFile=NULL;
static char* opt_recipient=NULL;
static char* opt_subjectName=NULL;
static char* opt_user=NULL;
static char* opt_password=NULL;
static char* opt_engine=NULL;
static char* opt_extCertsOutDir=NULL;
static char* opt_rootCerts=NULL;
static char* opt_extraCertsIn=NULL;
static int opt_hex=0;
static int opt_proxy=0;
static int opt_sequenceSet=0;
static int opt_doIr=0;
static int opt_doCr=0;
static int opt_doKur=0;
static int opt_doRr=0;
static int opt_doInfo=0;
static int opt_doGenM=0;
static int opt_doPathValidation=0;
int opt_pem=0;

static char** opt_extraCerts=NULL;
static int opt_nExtraCerts=0;

/* calculated from CLA */
static unsigned char *idString=NULL;
static unsigned char *password=NULL;
static size_t idStringLen=0, passwordLen=0;
static X509 *srvCert=NULL;
static ENGINE *engine=NULL;

static STACK_OF(X509) *extraCerts = NULL;

/* ############################################################################ */
/* ############################################################################ */
void printUsage( const char* cmdName) {
  printf("Usage: %s [COMMON OPTIONS] [CMD] [OPTIONS]\n", cmdName);
  printf("Use the \"Certificate Management Protocol\" as client\n");
  printf("\n");
  printf("Written by Martin Peylo <martin.peylo@nsn.com>\n");
  printf("\n");
  printf("The COMMON OPTIONS have to be set for each CMD:\n");
  printf(" --server SERVER    the IP address of the CMP server\n");
  printf(" --port PORT        the port of the CMP server\n");
  printf(" --path PATH        the path location inside the HTTP CMP server\n");
  printf("                    as in e.g. SERVER:PORT/PATH\n");
  printf(" --srvCert          location of the CMP server's certificate (e.g. CA or RA)\n");
  printf(" --pem              Use PEM format when saving certificates (default is DER).\n");
  printf("\n");
  printf("The OPTIONAL COMMON OPTIONS may to be set:\n");
  printf(" --engine ENGINE       the OpenSSL engine\n");
  printf(" --extcertsout DIR     directory where received certificates\n"); 
  printf("                       located in the \"extraCerts\" field will be saved\n");
  printf("                       with a [8Byte subject hash].0 filename\n");
  printf("                       NB: multiple certificates with same DN but other Serial have the same hash!\n");
  printf(" --rootcerts DIR       directory of root certificates. the certificates should have names\n");
  printf("                       int the form hash.0, where 'hash' is the hashed certificate subject name.\n");
  printf("                       see the -hash option of OpenSSL's x509 utility.\n");
  /* TODO: ADD this:
     printf(" --rootcerts FILE      single PEM/DER format file with root certificates\n");
  */
  printf(" --extcertsin DIR      directory where extra certificates needed\n"); 
  printf("                       for path validation of own and other's certificates\n");
  printf("                       is located\n");
  /* TODO: ADD this:
     printf(" --extcertsin FILE      single PEM/DER format file with extra certificates\n");
  */
  printf(" --validate_path       enable validation of the CA certificate's trust path.\n");
  printf("\n");
  printf("One of the following can be used as CMD:\n");
  printf(" --ir    do initial certificate request sequence\n");
  printf(" --kur   do key update request sequence\n");
  printf(" --cr    do renewal of a certificate\n");
  printf(" --rr    do revocation request sequence\n");
  printf(" --info  do PKI Information request sequence\n");
  printf(" --genm  MSG send a General Message containing given MSG type\n");
  printf("            supported messages: ckuann, currentcrl\n");
  printf("\n");
  printf("The following OPTIONS have to be set when needed by CMD:\n");
  printf(" --user USER           the user (reference) for an IR message\n");
  printf(" --password PASSWORD   the password (secret) for an IR message\n");
  printf(" --hex                 user and password are HEX, not ASCII\n");
/* XXX TODO: OpenSSL commonly seems to use "/" as delimiter */
  printf(" --subject NAME        X509 subject name for the certificate Template\n");
  printf("                       example: CN=MyName\n");
/* XXX TODO: OpenSSL commonly seems to use "/" as delimiter */
  printf(" --recipient NAME      X509 name of the recipient. Can be used for the IR\n");
  printf("                       if the client doesn't have the CA's certificate yet.\n");
/* XXX TODO: that should always be used as Input and never overwritten */
  printf(" --clcert FILE         location of the client's certificate to be used to sign the CMP messages\n");
  printf("                       also used as external identity certificate when doing IR according to RFC 4210 E.7\n");
/* XXX TODO: that should always be the destination of the new cert and always overwritten if a new cert is received */
  printf(" --newclcert FILE      location of the client's new certificate\n");
  printf("                       this is created (respectively overwritten!) at IR,CR and KUR\n");
/* XXX TODO: that should always be the private key for the --clcert and never overwritten*/
  printf(" --key FILE            location of the private key for the client certificate given in --clcert\n");
  printf(" --keypass PASSWORD    password of the client's private key given in --key\n");
/* XXX TODO: that should always be the private key for the new certificate and only created if it hasn't existed before - never overwritten */
  printf(" --newkey FILE         location of the client's new private key\n");
  printf("                       if file does not exist for IR, CR or KUR, this will be created with standard parameters\n");
  printf(" --newkeypass PASSWORD password of the client's new private key given in --newkey\n");
  printf("                       this is overwritten at KUR\n");
/* XXX TODO: the following should be added */
#if 0
  printf(" --newkeypass PASSWORD    password of the client's new private key given in --newkey\n");
#endif
  printf(" --extracert FILE      certificate that will be added to the extraCerts field\n");
  printf("                       when sending any PKIMessage.  Can be given multiple times\n");
  printf("                       in order to specify several certificates.\n");
  printf("\n");
  printf("Optional options only for IR with the --ir CMD:\n");
  printf(" --capubs DIRECTORY the directory where received CA certificates will be saved\n");
  printf("                    according to 5.3.2. those can only come in an IR protected with\n");
  printf("                    \"shared secret information\"\n");
  printf("\n");
  printf("Other options are:\n");
  printf(" --proxy       set proxy from $http_proxy environment variable if available\n");
  printf(" --verbose     ignored so far\n");
  printf(" --brief       ignored so far\n");
  printf(" --help        shows this help\n");
  printf("\n");
  exit(1);
}

char *getCertFilename(X509 *cert, char *destDir) {
#define CERTFILEPATHLEN 512
  char certFile[CERTFILEPATHLEN];
  X509 *existingCert = NULL;
  int n = 0;
  unsigned long hash = X509_subject_name_hash(cert);
  /* for certificates with the same subject name we only try
   * names from hash.0 to hash.9 */
  do {
    snprintf(certFile, CERTFILEPATHLEN, "%s/%8lx.%d", destDir, hash, n++);
    existingCert = HELP_read_cert(certFile);
    if (existingCert) {
      /* check if we already have this exact same cert */
      int cmp = X509_cmp(cert, existingCert);
      X509_free(existingCert);
      if (cmp == 0) return NULL;
    }
  } while (existingCert != NULL && n < 10);

  if (!existingCert)
    return strdup(certFile);

  printf("ERROR: unable to get a suitable filename for saving certificate\n");
  return NULL;
}

/* ############################################################################ */
/* this function writes all the certificates from the caPubs field of a received
 * ip or kup message into the given directory */
/* ############################################################################ */
int writeCaPubsCertificates( char *destDir, CMP_CTX *cmp_ctx) {
  X509 *cert = NULL;
  int n = 0;

  if (!destDir) goto err;

  printf( "Received %d CA certificates, saving to %s\n", CMP_CTX_caPubs_num(cmp_ctx), destDir);
  while ( (cert=CMP_CTX_caPubs_pop(cmp_ctx)) != NULL) {
    char *certFile = getCertFilename(cert, destDir);
    if (!certFile) continue;

    if(!HELP_write_cert(cert, certFile)) {
      printf("ERROR: could not write CA certificate number %d to %s!\n", n, certFile);
    }
    free(certFile);
  }
  return n;
err:
  return 0;
}

/* ############################################################################ */
/* this function writes all the certificates from the extCerts field of received
 * messages into the given directory */
/* ############################################################################ */
int writeExtraCerts( char *destDir, CMP_CTX *cmp_ctx) {
  X509 *cert = NULL;
  int n = 0;

  if (!destDir) goto err;

  printf( "Received %d certificates in extCerts, saving to %s\n", CMP_CTX_extraCertsIn_num(cmp_ctx), destDir);
  while ( (cert=CMP_CTX_extraCertsIn_pop(cmp_ctx)) != NULL) {
    char *certFile = getCertFilename(cert, destDir);
    if (!certFile) continue;

    if(!HELP_write_cert(cert, certFile)) {
      printf("ERROR: could not write CA certificate number %d to %s!\n", n, certFile);
    }
    free(certFile);
  }
  return n;
err:
  return 0;
}

/* ############################################################################ */
/* ############################################################################ */
void doIr(CMP_CTX *cmp_ctx) {
  /* EVP_PKEY *pkey=NULL; */
  EVP_PKEY *newPkey=NULL;
  X509 *newClCert=NULL;
  X509 *extIdCert=NULL;

  /* set to the context what we already know */
  CMP_CTX_set1_referenceValue( cmp_ctx, idString, idStringLen);
  CMP_CTX_set1_secretValue( cmp_ctx, password, passwordLen);
  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  if (srvCert)
    CMP_CTX_set1_srvCert( cmp_ctx, srvCert);
  CMP_CTX_set1_timeOut( cmp_ctx, 60);
  if (opt_subjectName) {
    X509_NAME *subject = HELP_create_X509_NAME(opt_subjectName);
    CMP_CTX_set1_subjectName( cmp_ctx, subject);
    X509_NAME_free(subject);
  }
  if (opt_recipient) {
    X509_NAME *recipient = HELP_create_X509_NAME(opt_recipient);
    CMP_CTX_set1_recipient( cmp_ctx, recipient);
    X509_NAME_free(recipient);
  }
  if (opt_nExtraCerts > 0)
    CMP_CTX_set1_extraCertsOut( cmp_ctx, extraCerts);

  /* using RFC4210's E.7 using external identity certificate */
  if (opt_clCertFile) {
    if(!(newPkey = HELP_readPrivKey(opt_clKeyFile, opt_clKeyPass))) {
      printf("FATAL: could not read external identity certificate private key from %s!\n", opt_clKeyFile);
      exit(1);
    }
    CMP_CTX_set0_pkey( cmp_ctx, newPkey);

    if (!(extIdCert = HELP_read_cert(opt_clCertFile))) {
      printf("FATAL: could not read external identity certificate from %s!\n", opt_clCertFile);
      exit(1);
    }
    CMP_CTX_set1_clCert( cmp_ctx, extIdCert);
  }

/* TODO: make this a generic function and use for IR, CR and KUR */
  /* load key to be certificated from file or generate new if file is not there*/
  FILE *key = fopen(opt_newClKeyFile, "r");
  if (key != NULL) {
    fclose(key);
    printf("INFO: Using existing key file \"%s\"\n", opt_newClKeyFile);
    if (opt_engine) {
      if (!(newPkey = ENGINE_load_private_key (engine, opt_newClKeyFile, NULL, opt_newClKeyPass))) {
        printf("FATAL: could not read private key /w engine\n");
        exit(1);
      }
    } else { // no engine specified reading private key from file
      if(!(newPkey = HELP_readPrivKey(opt_newClKeyFile, opt_newClKeyPass))) {
        printf("FATAL: could not read private client key!\n");
        exit(1);
      }
    }
  } else {
    /* generate new private key */
    newPkey = HELP_generateRSAKey();
    /* newPkey = HELP_generateDSAKey(); */
    HELP_savePrivKey(newPkey, opt_newClKeyFile, opt_newClKeyPass);
  }

  CMP_CTX_set0_newPkey( cmp_ctx, newPkey);

  /* TODO: create CLI option for implicit confim */
  /* CL does not support this, it just ignores it.
   * CMP_CTX_set_option( cmp_ctx, CMP_CTX_OPT_IMPLICITCONFIRM, CMP_CTX_OPT_SET);
   */

  newClCert = CMP_doInitialRequestSeq( cmp_ctx);

  if( newClCert) {
    printf( "SUCCESS: received initial Client Certificate. FILE %s, LINE %d\n", __FILE__, __LINE__);
  } else {
    printf( "ERROR: received no initial Client Certificate. FILE %s, LINE %d\n", __FILE__, __LINE__);
    ERR_load_crypto_strings();
    ERR_print_errors_fp(stderr);
    exit(1);
  }
  if(!HELP_write_cert(newClCert, opt_newClCertFile)) {
    printf("FATAL: could not write new client certificate to %s!\n", opt_newClCertFile);
    exit(1);
  }

  /* if the option caPubsDir was given, see if we received certificates in
   * the caPubs field and write them into the given directory */
  if (opt_caPubsDir) {
    writeCaPubsCertificates(opt_caPubsDir, cmp_ctx);
	}

  /* if the option extcertsout was given, see if we received certificates in
   * the extCerts field and write them into the given directory */
  if (opt_extCertsOutDir) {
    writeExtraCerts(opt_extCertsOutDir, cmp_ctx);
	}

  return;
}

/* ############################################################################ */
/* ############################################################################ */
void doRr(CMP_CTX *cmp_ctx) {
  EVP_PKEY *initialPkey=NULL; /* TODO: s/intitialPkey/pkey/ */
  X509 *initialClCert=NULL;   /* TODO: s/initialClCert/clCert/ */

  // ENGINE_load_private_key(e, path, NULL, "password"); 

  if (opt_engine) {
    if (!(initialPkey = ENGINE_load_private_key (engine, opt_clKeyFile, NULL, opt_clKeyPass))) {
      printf("FATAL: could not read private key /w engine\n");
      exit(1);
    }
  } else { // no engine specified reading private key from file
    if(!(initialPkey = HELP_readPrivKey(opt_clKeyFile, opt_clKeyPass))) {
      printf("FATAL: could not read private client key!\n");
      exit(1);
    }
  }
  if(!(initialClCert = HELP_read_cert(opt_clCertFile))) {
    printf("FATAL: could not read client certificate!\n");
    exit(1);
  }

  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  CMP_CTX_set0_pkey( cmp_ctx, initialPkey);
  CMP_CTX_set1_srvCert( cmp_ctx, srvCert);
  CMP_CTX_set1_clCert( cmp_ctx, initialClCert);
  CMP_CTX_set1_referenceValue( cmp_ctx, idString, idStringLen);
  CMP_CTX_set1_secretValue( cmp_ctx, password, passwordLen);

  if (opt_nExtraCerts > 0)
    CMP_CTX_set1_extraCertsOut( cmp_ctx, extraCerts);

  /* CL does not support this, it just ignores it.
   * CMP_CTX_set_option( cmp_ctx, CMP_CTX_OPT_IMPLICITCONFIRM, CMP_CTX_OPT_SET);
   */

  CMP_doRevocationRequestSeq( cmp_ctx);

  return;
}


/* ############################################################################ */
/* ############################################################################ */
void doCr(CMP_CTX *cmp_ctx) {
  EVP_PKEY *pkey=NULL;
  X509 *clCert=NULL;
  X509 *newClCert=NULL;

  /* XXX TODO: where is the newPkey?  Shouldn't CR be used to get new
   * certificates (possibly also for new keys) ? XXX TODO */

  // ENGINE_load_private_key(e, path, NULL, "password"); 

  if (opt_engine) {
    if (!(pkey = ENGINE_load_private_key (engine, opt_clKeyFile, NULL, opt_clKeyPass))) {
      printf("FATAL: could not read private key /w engine\n");
      exit(1);
    }
  } else { // no engine specified reading private key from file
    if(!(pkey = HELP_readPrivKey(opt_clKeyFile, opt_clKeyPass))) {
      printf("FATAL: could not read private client key!\n");
      exit(1);
    }
  }
  if(!(clCert = HELP_read_cert(opt_clCertFile))) {
    printf("FATAL: could not read client certificate!\n");
    exit(1);
  }

  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  CMP_CTX_set0_pkey( cmp_ctx, pkey);
  CMP_CTX_set1_srvCert( cmp_ctx, srvCert);
  CMP_CTX_set1_clCert( cmp_ctx, clCert);

  if (opt_nExtraCerts > 0)
    CMP_CTX_set1_extraCertsOut( cmp_ctx, extraCerts);

  /* CL does not support this, it just ignores it.
   * CMP_CTX_set_option( cmp_ctx, CMP_CTX_OPT_IMPLICITCONFIRM, CMP_CTX_OPT_SET);
   */

  newClCert = CMP_doCertificateRequestSeq( cmp_ctx);

  if( newClCert) {
    printf( "SUCCESS: received renewed Client Certificate. FILE %s, LINE %d\n", __FILE__, __LINE__);
  } else {
    printf( "ERROR: received no renewed Client Certificate. FILE %s, LINE %d\n", __FILE__, __LINE__);
    exit(1);
  }
  if(!HELP_write_cert( newClCert, opt_newClCertFile)) {
    printf("FATAL: could not write new client certificate!\n");
    exit(1);
  }

  return;
}

/* ############################################################################ */
/* ############################################################################ */
void doKur(CMP_CTX *cmp_ctx) {
  EVP_PKEY *pkey=NULL;
  X509 *clCert=NULL;

  EVP_PKEY *updatedPkey=NULL;
  X509 *updatedClCert=NULL;

  if (opt_subjectName) {
    X509_NAME *subject = HELP_create_X509_NAME(opt_subjectName);
    CMP_CTX_set1_subjectName( cmp_ctx, subject);
    X509_NAME_free(subject);
  }
  if (opt_recipient) {
    X509_NAME *recipient = HELP_create_X509_NAME(opt_recipient);
    CMP_CTX_set1_recipient( cmp_ctx, recipient);
    X509_NAME_free(recipient);
  }
  if(!(pkey = HELP_readPrivKey(opt_clKeyFile, opt_clKeyPass))) {
    printf("FATAL: could not read private client key!\n");
    exit(1);
  }
  if(!(clCert = HELP_read_cert(opt_clCertFile))) {
    printf("FATAL: could not read client certificate!\n");
    exit(1);
  }

  /* generate RSA key */
  updatedPkey = HELP_generateRSAKey();
  /* updatedPkey = HELP_generateDSAKey(); */
  if(!HELP_savePrivKey( updatedPkey, opt_newClKeyFile, opt_newClKeyPass)) {
    printf("FATAL: could not save private client key!");
    exit(1);
  }

  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  CMP_CTX_set0_pkey( cmp_ctx, pkey);
  CMP_CTX_set0_newPkey( cmp_ctx, updatedPkey);
  CMP_CTX_set1_clCert( cmp_ctx, clCert);
  CMP_CTX_set1_srvCert( cmp_ctx, srvCert);

  if (opt_nExtraCerts > 0)
    CMP_CTX_set1_extraCertsOut( cmp_ctx, extraCerts);

  updatedClCert = CMP_doKeyUpdateRequestSeq( cmp_ctx);

  if( updatedClCert) {
    printf( "SUCCESS: received updated Client Certificate, and %d CA certs in caPubs. FILE %s, LINE %d\n", 
    		CMP_CTX_caPubs_num(cmp_ctx), __FILE__, __LINE__);
  } else {
    printf( "ERROR: received no updated Client Certificate. FILE %s, LINE %d\n", __FILE__, __LINE__);
    ERR_load_crypto_strings();
    ERR_print_errors_fp(stderr);
    exit(1);
  }
  if(!HELP_write_cert( updatedClCert, opt_newClCertFile)) {
    printf("FATAL: could not write new client certificate!\n");
    exit(1);
  }

  return;
}

/* ############################################################################ */
/* ############################################################################ */
void doInfo(CMP_CTX *cmp_ctx) {
  STACK_OF(CMP_INFOTYPEANDVALUE) *res=NULL;

  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  /* TODO XXX: can't we sign with the clCert also? XXX TODO */
  CMP_CTX_set1_referenceValue( cmp_ctx, idString, idStringLen);
  CMP_CTX_set1_secretValue( cmp_ctx, password, passwordLen);
  CMP_CTX_set1_srvCert( cmp_ctx, srvCert);

  res = CMP_doGeneralMessageSeq( cmp_ctx, 0, NULL);

  if( res) {
    printf( "SUCCESS: Doing PKI Information Request/Response. FILE %s, LINE %d\n", __FILE__, __LINE__);
  } else {
    printf( "ERROR: Doing PKI Information Request/Response. FILE %s, LINE %d\n", __FILE__, __LINE__);
    ERR_load_crypto_strings();
    ERR_print_errors_fp(stderr);
    exit(1);
  }

  return;
}

/* ############################################################################ */
/* ############################################################################ */
void doGenM(CMP_CTX *cmp_ctx, int genm_type, void *value) {
  STACK_OF(CMP_INFOTYPEANDVALUE) *resp = NULL;

  CMP_CTX_set1_serverName( cmp_ctx, opt_serverName);
  CMP_CTX_set1_serverPath( cmp_ctx, opt_serverPath);
  CMP_CTX_set1_serverPort( cmp_ctx, opt_serverPort);
  CMP_CTX_set1_referenceValue( cmp_ctx, idString, idStringLen);
  CMP_CTX_set1_secretValue( cmp_ctx, password, passwordLen);
  CMP_CTX_set1_srvCert( cmp_ctx, srvCert);

  resp = CMP_doGeneralMessageSeq( cmp_ctx, genm_type, NULL);

  if( resp) {
    printf( "SUCCESS sending General Message. FILE %s, LINE %d\n", __FILE__, __LINE__);

    switch (opt_doGenM) {
      case NID_id_it_caKeyUpdateInfo:
        {
          /* CMP_CAKEYUPDANNCONTENT *cku = (CMP_CAKEYUPDANNCONTENT*) resp; */
          /* TODO write out the received certs! */
        }
        break;

      case NID_id_it_currentCRL:
        {
          /* TODO write out the received CRL! */
#if 0
          X509_CRL *crl = (X509_CRL*) resp;
          BIO  *bio;
          bio=BIO_new(BIO_s_file());
          BIO_write_filename(bio, "/tmp/crl.der");
          i2d_X509_CRL_bio(bio, crl);
          BIO_free(bio);
#endif
        }
        break;

      default:
        break;
    }

  } else {
    printf( "ERROR sending General Message. FILE %s, LINE %d\n", __FILE__, __LINE__);
    ERR_load_crypto_strings();
    ERR_print_errors_fp(stderr);
    exit(1);
  }

  return;
}

/* ############################################################################ */
/* allocate appropriate space for CLI argument strings and copy them */
/* ############################################################################ */
char * createOptStr(char ** opt) {
  size_t len = strlen(optarg);

  if( *opt!=NULL) free(*opt); 
  if (! (*opt = (char*) malloc(len+1))) {
    printf( "FATAL failed to allocate mememory to store CLI option string, aborting");
    exit(1);
  }
  strncpy(*opt, optarg, len);
  (*opt)[len] = '\0';

  return *opt;
}



/* ############################################################################ */
/* ############################################################################ */
void parseCLA( int argc, char **argv) {
  /* manage command line options */
  int c;
  /* getopt_long stores the option index here. */
  int option_index = 0;

  static struct option long_options[] =
  {
    {"verbose",  no_argument,          &verbose_flag, 1},
    {"brief",    no_argument,          &verbose_flag, 0},
    {"server",   required_argument,    0, 'a'},
    {"port",     required_argument,    0, 'b'},
    {"ir",       no_argument,          0, 'c'},
    {"kur",      no_argument,          0, 'd'},
    {"genm",     required_argument,    0, 'G'},
    {"user",     required_argument,    0, 'e'},
    {"password", required_argument,    0, 'f'},
    {"pem",      no_argument,          0, 'E'},
    {"srvcert",   required_argument,   0, 'g'},
    {"clcert",   required_argument,    0, 'h'},
    {"subject",  required_argument,    0, 'S'},
    {"recipient",required_argument,    0, 'R'},
    {"capubs",   required_argument,    0, 'U'},
    {"help",     no_argument,          0, 'i'},
    {"key",      required_argument,    0, 'j'},
    {"keypass",  required_argument,    0, 'J'},
    {"newkey",   required_argument,    0, 'k'},
    {"newkeypass",required_argument,   0, 'P'},
    {"newclcert",required_argument,    0, 'l'},
    {"hex",      no_argument,          0, 'm'},
    {"info",     no_argument,          0, 'n'},
    {"validate_path",no_argument,      0, 'V'},
    {"path",     required_argument,    0, 'o'},
    {"proxy",    optional_argument,    0, 'p'},
    {"cr",	     no_argument,          0, 't'},
    {"rr",	     no_argument,          0, 'r'},
    {"engine",   required_argument,    0, 'u'},
    {"extracert",required_argument,    0, 'X'},
    {"extcertsout",required_argument,  0, 'O'},
    {"rootcerts",required_argument,    0, 'T'},
    {"extcertsin ",required_argument,  0, 'N'},
    {0, 0, 0, 0}
  };

  while (1)
  {
    c = getopt_long (argc, argv, "a:b:cde:f:g:G:h:iIj:J:k:l:mno:O:p::P:rR:sS:tT:N:u:U:X:", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1)
      break;

    switch (c)
    {
      case 0:
        /* If this option set a flag, do nothing else now. */
        if (long_options[option_index].flag != 0)
          break;
        printf ("option %s", long_options[option_index].name);
        if (optarg)
          printf (" with arg %s", optarg);
        printf ("\n");
        break;

      case 'N':
        createOptStr( &opt_extraCertsIn);
        break;

      case 'T':
        createOptStr( &opt_rootCerts);
        break;

      case 'U':
        createOptStr( &opt_caPubsDir);
        break;

      case 'a':
        createOptStr( &opt_serverName);
        break;

      case 'b':
        if (optarg)
          opt_serverPort = atoi(optarg);
        break;

      case 'V':
        opt_doPathValidation = 1;
        break;

      case 'c':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        opt_sequenceSet = 1;
        opt_doIr = 1;
        break;

      case 't':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        opt_sequenceSet = 1;
        opt_doCr = 1;
        break;

      case 'r':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        opt_sequenceSet = 1;
        opt_doRr = 1;
        break;

      case 'd':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        opt_sequenceSet = 1;
        opt_doKur = 1;
        break;

      case 'G':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        {
          char *genm_type = NULL;
          createOptStr( &genm_type);
          if (!strcmp(genm_type, "ckuann")) {
            opt_doGenM = NID_id_it_caKeyUpdateInfo;
          }
          else if (!strcmp(genm_type, "curcrl")) {
            opt_doGenM = NID_id_it_currentCRL;
          }
          else {
            fprintf( stderr, "ERROR: unknown/unsupported General Message '%s'\n", genm_type);
            exit(1);
          }
        }
        opt_sequenceSet = 1;
        break;

      case 'n':
        if( opt_sequenceSet) {
          fprintf( stderr, "ERROR: only one message sequence can be set at once!\n");
          printUsage( argv[0]);
        }
        opt_sequenceSet = 1;
        opt_doInfo = 1;
        break;

      case 'X':
        opt_extraCerts = (char**) realloc(opt_extraCerts, (opt_nExtraCerts+1) * sizeof(char*));
        createOptStr( &opt_extraCerts[opt_nExtraCerts]);
        opt_nExtraCerts++;
        break;

      case 'e':
        createOptStr( &opt_user);
        break;
      case 'f':
        createOptStr( &opt_password);
        break;
      case 'E':
        opt_pem= 1;
        break;
      case 'g':
        createOptStr( &opt_srvCertFile);
        break;
      case 'h':
        createOptStr( &opt_clCertFile);
        break;
      case 'S':
        createOptStr( &opt_subjectName);
        break;
      case 'R':
        createOptStr( &opt_recipient);
        break;
      case 'i':
        printUsage( argv[0]);
        break;
      case 'j':
        createOptStr( &opt_clKeyFile);
        break;
      case 'J':
        opt_clKeyPass=NULL;
        createOptStr( &opt_clKeyPass);
        break;
      case 'P':
        opt_newClKeyPass=NULL;
        createOptStr( &opt_newClKeyPass);
        break;
      case 'k':
        createOptStr( &opt_newClKeyFile);
        break;
      case 'l':
        createOptStr( &opt_newClCertFile);
        break;
      case 'm':
        opt_hex = 1;
        break;
      case 'o':
        createOptStr( &opt_serverPath);
        break;
      case 'p':
        opt_proxy = 1;
        if (optarg)
          createOptStr( &opt_httpProxy);
        break;
      case 'u':
        createOptStr( &opt_engine);
        break;
      case 'O':
        createOptStr( &opt_extCertsOutDir);
        break;

      case '?':
        /* getopt_long already printed an error message. */
        break;

      default:
        abort ();
    }
  }

  if (optind < argc) {
    printf ("ERROR: the following arguments were not recognized: ");
    while (optind < argc)
      printf ("%s ", argv[optind++]);
    printf("\n\n");
    printUsage( argv[0]);
  }

  if (!(opt_serverName && opt_serverPort)) {
    // printf("ERROR: setting server, port and srvCert is mandatory for all sequences\n\n");
    printf("ERROR: setting server and port is mandatory for all sequences\n\n");
    printUsage( argv[0]);
  }

  if (!(opt_srvCertFile || opt_rootCerts) && !opt_doIr) {
    /* TODO: actually that could be done with --recipient when protection is
     * done with preshared keys for all (?) messages */
    printf("ERROR: setting srvcert or rootCerts is necessary for all sequences except IR\n\n");
    printUsage( argv[0]);
  }

  if (!opt_sequenceSet) {
    printf("ERROR: supply a CMD\n");
    printUsage( argv[0]);
  }

  if( opt_doKur) {
    if (!(opt_clCertFile && opt_clKeyFile)) {
      printf("ERROR: setting srvcert, clcert, and key is mandatory for KUP\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doIr) {
    /* for IR, a mean for signing the CMP message has to be supplied */
    /* TODO ? in case both would be given, the user/password will be preferred */
    if (!((opt_user && opt_password) || (opt_clCertFile && opt_clKeyFile))) {
      printf("ERROR: giving user/password or clcert/key/keypass CLI option is mandatory for IR\n\n");
      printUsage( argv[0]);
    }
    if (!opt_srvCertFile && !opt_recipient) {
      printf("ERROR: setting srvcert or recipient is mandatory for IR\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doCr) {
    if (!(opt_clCertFile && opt_clKeyFile)) {
      printf("ERROR: clcert and key is mandatory for CR\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doRr) {
    if (!opt_srvCertFile && !opt_recipient) {
      printf("ERROR: setting srvcert or recipient is mandatory for RR\n\n");
      printUsage( argv[0]);
    }
    if (!(opt_clCertFile && opt_clKeyFile)) {
      printf("ERROR: clcert and key is mandatory for RR\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doInfo) {
    if (!(opt_user && opt_password )) {
      printf("ERROR: setting user and password is mandatory for PKIInfo\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doGenM) {
    if (!(opt_user && opt_password )) {
      printf("ERROR: setting user and password is mandatory for a GenM\n\n");
      printUsage( argv[0]);
    }
  }

  if( opt_doIr || opt_doKur) {
    /* for IR,CR,Kur a a place to store the new certificate and the location for the
     * (new) key and its password have to be supplied */
    if (!(opt_newClCertFile && opt_newClKeyFile)) {
      printf("ERROR: giving newclcert/newkey is mandatory for trying to get a new Certificate through IR/KUR\n\n");
      printUsage( argv[0]);
    }
  }

#if 0
  if(opt_clKeyFile && !opt_clKeyPass) {
    printf("ERROR: giving keypass is mandatory when giving key\n\n");
    printUsage( argv[0]);
  }

  if(opt_newClKeyFile && !opt_newClKeyPass) {
    printf("ERROR: giving newkeypass is mandatory when giving newkey\n\n");
    printUsage( argv[0]);
  }
#endif

  return;
}

/* ############################################################################ */
/* ############################################################################ */
int getHttpProxy( char **name, int *port) {
  char *proxy=NULL;
  char *colon=NULL;
  char format[32];
  unsigned int maxlen;

  if( opt_httpProxy) {
    proxy = opt_httpProxy;
  } else {
    if( !opt_proxy) return 0;
    proxy = getenv("http_proxy");
    if( proxy) {
      if( !(proxy = strdup(proxy))) {
        printf( "FATAL failed to allocate mememory to proxy string, aborting");
        exit(0);
      }
    } else {
      /* no proxy setting found */
      return 0;
    }
  }

  /* convert all colons to space */
  while( (colon = strchr(proxy, ':'))) {
    *colon = ' ';
  }

  /* this will be long enough */
  maxlen = strlen(proxy) + 1;
  *name = calloc(1, maxlen);

  snprintf(format, 32, "http //%%%us %%d", (unsigned int) maxlen);

  if( (sscanf( proxy, format, *name, port) < 1)) {
    /* maybe it is set without leading http:// */
    snprintf(format, 32, "%%%us %%d", maxlen);
    if( (sscanf( proxy, format, *name, port) < 1)) {
      printf("ERROR: Failed to determine proxy from \"%s\"\n", proxy);
      free(proxy);
      return 0;
    }
  }
  printf("INFO: found proxy setting, Name=%s, Port=%d\n", *name, *port);
  free(proxy);
  return 1;
}

/* ############################################################################ */
/* ############################################################################ */
int set_engine (const char* e)
{
  engine = ENGINE_by_id(e);

  if (!engine)
  {
    printf ("ERROR: SSL Engine %s not found!\n", e);
    return 0;
  }

  if (!ENGINE_init(engine)) {
    char buf[256];

    ENGINE_free(engine);
    printf ("ERROR: Failed to initialize Engine %s\n%s\n", 
        e, buf);
    return 0;
  }

  return 1;
}

/* ############################################################################ */
/* ############################################################################ */
int main(int argc, char **argv) {
  char *httpProxyName = NULL;
  int httpProxyPort = 0;
  CMP_CTX *cmp_ctx = NULL;

  parseCLA(argc, argv);

  ENGINE_load_builtin_engines();

  if (opt_engine)
  {	
    if (!set_engine(opt_engine))
      exit(1);
  }

  /* read CA certificate */
  if(opt_srvCertFile && !(srvCert = HELP_read_cert(opt_srvCertFile))) {
    printf("FATAL: could not read CA certificate!\n");
    exit(1);
  }

  /* read given extraCerts, if any */
  if( opt_nExtraCerts > 0) {
    int i;
    if( !(extraCerts = sk_X509_new_null())) {
      printf("FATAL: could not create structure to store extra certificates!\n");
      exit(1);
    }
    for (i = 0; i < opt_nExtraCerts; i++) {
      X509 *cert = HELP_read_cert(opt_extraCerts[i]);
      if (!cert) {
        printf("FATAL: could not read extraCerts\n");
        exit(1);
      }
      sk_X509_push(extraCerts, cert);
    }
  }

  /* XXX this is not freed yet */
  cmp_ctx = CMP_CTX_create();

  if (getHttpProxy( &httpProxyName, &httpProxyPort)) {
    CMP_CTX_set1_proxyName(cmp_ctx, httpProxyName);
    CMP_CTX_set1_proxyPort(cmp_ctx, httpProxyPort);
  }


  if (!cmp_ctx) {
    printf("FATAL: could not create CMP_CTX\n");
    exit(1);
  }

  /* TODO move the handling of all common options such as server ip, port etc. here */

  if (opt_rootCerts) {
    X509_STORE *trusted_store = HELP_create_cert_store(opt_rootCerts);
    CMP_CTX_set0_trustedStore(cmp_ctx, trusted_store);
  }

  if (opt_extraCertsIn) {
    X509_STORE *untrusted_store = HELP_create_cert_store(opt_extraCertsIn);
    CMP_CTX_set0_untrustedStore(cmp_ctx, untrusted_store);
  }

  if (opt_user && opt_password) {
    if (opt_hex) {
      /* get str representation of hex passwords */
      idStringLen = HELP_hex2str(opt_user, &idString);
      passwordLen = HELP_hex2str(opt_password, &password);
    } else {
      idStringLen = strlen(opt_user);
      idString = (unsigned char*) opt_user;
      passwordLen = strlen(opt_password);
      password = (unsigned char*) opt_password;
    }
  }

  if( opt_doIr) {
    doIr(cmp_ctx);
  }

  if( opt_doCr) {
    doCr(cmp_ctx);
  }

  if( opt_doKur) {
    doKur(cmp_ctx);
  }

  if( opt_doRr) {
    doRr(cmp_ctx);
  }

  if( opt_doInfo) {
    doInfo(cmp_ctx);
  }

  if( opt_doGenM) {
    doGenM(cmp_ctx, opt_doGenM, NULL);
  }

  return 0;
}

