/****************************************************************************
*																			*
*						cryptlib CMP Session Test Routines					*
*						Copyright Peter Gutmann 1998-2005					*
*																			*
****************************************************************************/

#include "cryptlib.h"
#include "test/test.h"

#if defined( __MVS__ ) || defined( __VMCMS__ )
  /* Suspend conversion of literals to ASCII. */
  #pragma convlit( suspend )
#endif /* IBM big iron */
#if defined( __ILEC400__ )
  #pragma convert( 0 )
#endif /* IBM medium iron */

/* If we're running the test with a crypto device, we have to set an extended
   timeout because of the long time it takes many devices to generate keys */

#define NET_TIMEOUT		180

#if defined( TEST_SESSION ) || defined( TEST_SESSION_LOOPBACK )

/****************************************************************************
*																			*
*								CMP Test Data								*
*																			*
****************************************************************************/

/* There are various CMP test CAs available, the following mappings can be
   used to test different ones.  Implementation peculiarities:

	#1 - cryptlib: Implicitly revokes cert being replaced during a kur (this
			is a requirement for maintaining cert store consistency).
			Tested: ir, cr/kur, rr
	#2 - cryptlib with PKIBoot/PnP PKI functionality, otherwise as for #1.
	#3 - Certicom: Requires signature for revocation rather than MAC,
			requires that all certs created after the ir one have the same
			DN as the ir cert.
			Tested: ir, cr/kur, rr
	#4 - ssh old: None (recently re-issued their CA cert which is broken, CA
			couldn't be re-tested.  In addition since CMP identifies the
			sender by DN the new cert can't be distinguished from the old
			one, causing all sig checks to fail).
			Tested (late 2000): ir, cr/kur, rr
	#5 - ssh new:
	#6 - Entrust: Won't allow altNames, changes sender and request DN,
			returns rejected response under an altered DN belonging to a
			completely different EE for anything but ir.
			Tested: ir
	#7 - Trustcenter: Requires HTTPS and pre-existing trusted private key
			distributed as PKCS #12 file, couldn't be tested.
	#8 - Baltimore: Server unavailable for testing.
			Tested: -
	#9 - Initech: Needs DN cn=CryptLIB EE 1,o=INITECH,c=KR.
			Tested: ir, cr/kur, rr
	#10 - RSA labs: Rejects signed requests, couldn't be tested beyond initial
			(MACd) ir.  Attempt to revoke newly-issued cert with MACd rr
			returns error indicating that the cert is already revoked.
			Tested: ir
	#11 - Cylink: Invalid CA root cert, requires use of DN from RA rather
			than CA when communicating with server.
			Tested: - */

#define CA_CRYPTLIB				1
#define CA_CRYPTLIB_PNPPKI		2

#define CA_NO					CA_CRYPTLIB

typedef struct {
	const char FAR_BSS *name;
	const C_CHR FAR_BSS *url, FAR_BSS *user, FAR_BSS *password;
	} CA_INFO;

static const CA_INFO FAR_BSS caInfo[] = {
	{ NULL },	/* Dummy so index == CA_NO */
	{ /*1*/ "cryptlib", TEXT( "http://localhost" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*2*/	"cryptlib/PKIBoot", /*"_pkiboot._tcp.cryptoapps.com"*/TEXT( "http://localhost" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*3*/ "Certicom", TEXT( "cmp://gandalf.trustpoint.com:8081" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*4*/ "ssh", TEXT( "cmp://interop-ca.ssh.com:8290" ), TEXT( "123456" ), TEXT( "interop" ) },
	{ /*5*/ "ssh", TEXT( "http://pki.ssh.com:8080/pkix/" ), TEXT( "62154" ), TEXT( "ssh" ) },
	{ /*6*/ "Entrust", TEXT( "cmp://204.101.128.45:829" ), TEXT( "39141091" ), TEXT( "ABCDEFGHIJK" ) },
	{ /*7*/ "Trustcenter", TEXT( "cmp://demo.trustcenter.de/cgi-bin/cmp:829" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*8*/ "Baltimore", TEXT( "cmp://hip.baltimore.ie:8290" ), TEXT( "pgutmann" ), TEXT( "the-magical-land-near-oz" ) },
	{ /*9*/ "Initech", TEXT( "cmp://61.74.133.49:8290" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*A*/ "RSA", TEXT( "cmp://ca1.kcspilot.com:32829" ), TEXT( "interop" ), TEXT( "interop" ) },
	{ /*B*/ "Cylink", TEXT( "cmp://216.252.217.227:8082" ), TEXT( "3986" ), TEXT( "11002" ) /* "3987", "6711" */ }
	};

/* Enable additional tests if we're using cryptlib as the server */

#if ( CA_NO == CA_CRYPTLIB ) || ( CA_NO == CA_CRYPTLIB_PNPPKI )
  #define SERVER_IS_CRYPTLIB
  #if ( CA_NO == CA_CRYPTLIB_PNPPKI )
	#define SERVER_PKIBOOT
  #endif /* cryptlib PKIBoot server */
#endif /* Extra tests for cryptib CA */

/* Define the following to work around CA bugs/quirks */

#if ( CA_NO == 3 )			/* Certicom */
  #define SERVER_IR_DN
#endif /* CA that requires same DN in cr as ir */
#if ( CA_NO == 6 )			/* Entrust */
  #define SERVER_NO_ALTNAMES
#endif /* CAs that won't allow altNames in requests */
#if ( CA_NO == 9 )			/* Initech */
  #define SERVER_FIXED_DN
#endif /* CAs that require a fixed DN in requests */

/* The following defines can be used to selectively enable or disable some
   of the test (for example to do an ir + rr, or ir + kur + rr) */

#ifdef SERVER_IS_CRYPTLIB
  #define TEST_IR
/*#define TEST_DUP_IR */
  #define TEST_KUR
  #define TEST_CR
  #define TEST_RR

  /* 3 cert reqs, 1 rev.req (kur = impl.rev) plus duplicate ir to check for
     rejection of second request for same user.  The duplicate-ir check is
	 currently disabled because it's enforced via database transaction
	 constraints, which means that once the initial ir has been recorded all
	 further issue operations with the same ID are excluded by the presence
	 of the ID for the ir.  This is a strong guarantee that subsequent
	 requests with the same ID will be disallowed, but not terribly useful
	 for self-test purposes */
  #define NO_CA_REQUESTS	( 4 + 0 )
#else
  #define TEST_IR
  #define TEST_KUR
  #define TEST_CR
  #define TEST_RR

  /* Loopback test requires SERVER_IS_CRYPTLIB */
  #define NO_CA_REQUESTS	0
#endif /* SERVER_IS_CRYPTLIB */

/* Define the following to enable testing of servers where the initial DN 
   (and optional additional information like the altName) is supplied by the 
   server (i.e. the user supplies a null DN) */

#ifdef SERVER_IS_CRYPTLIB
  #define SERVER_PROVIDES_DN
#endif /* CAs where the server provides the DN */

/* Cert request data for the various types of certs that a CMP CA can return */

static const CERT_DATA FAR_BSS cmpRsaSignRequestData[] = {
	/* Identification information */
  #ifdef SERVER_FIXED_DN
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "KR" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "INITECH" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "CryptLIB EE 1" ) },
  #else
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Dave's Signature Key" ) },
  #endif /* CAs that require a fixed DN in requests */

	/* Subject altName */
#ifndef SERVER_NO_ALTNAMES
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave@wetas-r-us.com" ) },
	{ CRYPT_CERTINFO_UNIFORMRESOURCEIDENTIFIER, IS_STRING, 0, TEXT( "http://www.wetas-r-us.com" ) },
#endif /* CAs that won't allow altNames in requests */

	/* Signature-only key */
	{ CRYPT_CERTINFO_KEYUSAGE, IS_NUMERIC, CRYPT_KEYUSAGE_DIGITALSIGNATURE },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};
static const CERT_DATA FAR_BSS cmpRsaSignRequestNoDNData[] = {
	/* Identification information - none, it's provided by the server */

	/* Subject altName - none, it's provided by the server */

	/* Signature-only key */
	{ CRYPT_CERTINFO_KEYUSAGE, IS_NUMERIC, CRYPT_KEYUSAGE_DIGITALSIGNATURE },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};
static const CERT_DATA FAR_BSS cmpRsaEncryptRequestData[] = {
	/* Identification information */
#ifdef SERVER_FIXED_DN
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "KR" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "INITECH" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "CryptLIB EE 1" ) },
#else
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Dave's Encryption Key" ) },
#endif /* CAs that require a fixed DN in requests */

	/* Subject altName */
#ifndef SERVER_NO_ALTNAMES
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave@wetas-r-us.com" ) },
	{ CRYPT_CERTINFO_UNIFORMRESOURCEIDENTIFIER, IS_STRING, 0, TEXT( "http://www.wetas-r-us.com" ) },
#endif /* CAs that won't allow altNames in requests */

	/* Encryption-only key */
	{ CRYPT_CERTINFO_KEYUSAGE, IS_NUMERIC, CRYPT_KEYUSAGE_KEYENCIPHERMENT },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};
static const CERT_DATA FAR_BSS cmpRsaCaRequestData[] = {
	/* Identification information */
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Dave's Intermediate CA Key" ) },

	/* Subject altName */
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave-ca@wetas-r-us.com" ) },
	{ CRYPT_CERTINFO_UNIFORMRESOURCEIDENTIFIER, IS_STRING, 0, TEXT( "http://www.wetas-r-us.com" ) },

	/* CA key */
	{ CRYPT_CERTINFO_CA, IS_NUMERIC, TRUE },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};
static const CERT_DATA FAR_BSS cmpDsaRequestData[] = {
	/* Identification information */
#ifdef SERVER_FIXED_DN
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "KR" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "INITECH" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "CryptLIB EE 1" ) },
#else
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
  #ifdef SERVER_IR_DN
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Dave's Signature Key" ) },
  #else
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Dave's DSA Key" ) },
  #endif /* CA that requires same DN in cr as ir */
#endif /* CAs that require a fixed DN in requests */

	/* Subject altName */
#ifndef SERVER_NO_ALTNAMES
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave@wetas-r-us.com" ) },
	{ CRYPT_CERTINFO_UNIFORMRESOURCEIDENTIFIER, IS_STRING, 0, TEXT( "http://www.wetas-r-us.com" ) },
#endif /* CAs that won't allow altNames in requests */

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};

/* PKI user data to authorise the issuing of the various certs */

static const CERT_DATA FAR_BSS cmpPkiUserData[] = {
	/* Identification information */
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Test PKI user" ) },

	/* Subject altName */
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave@wetas-r-us.com" ) },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};
static const CERT_DATA FAR_BSS cmpPkiUserCaData[] = {
	/* Identification information */
	{ CRYPT_CERTINFO_COUNTRYNAME, IS_STRING, 0, TEXT( "NZ" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONNAME, IS_STRING, 0, TEXT( "Dave's Wetaburgers" ) },
	{ CRYPT_CERTINFO_ORGANIZATIONALUNITNAME, IS_STRING, 0, TEXT( "Procurement" ) },
	{ CRYPT_CERTINFO_COMMONNAME, IS_STRING, 0, TEXT( "Test CA PKI user" ) },

	/* Subject altName */
	{ CRYPT_CERTINFO_RFC822NAME, IS_STRING, 0, TEXT( "dave@ca.wetas-r-us.com" ) },

	/* CA extensions */
	{ CRYPT_CERTINFO_KEYUSAGE, IS_NUMERIC,
	  CRYPT_KEYUSAGE_KEYCERTSIGN | CRYPT_KEYUSAGE_CRLSIGN },
	{ CRYPT_CERTINFO_CA, IS_NUMERIC, TRUE },

	{ CRYPT_ATTRIBUTE_NONE, IS_VOID }
	};

/****************************************************************************
*																			*
*								CMP Routines Test							*
*																			*
****************************************************************************/

/* Create various CMP objects */

static int createCmpRequest( const CERT_DATA *requestData,
							 const CRYPT_CONTEXT privateKey,
							 const CRYPT_ALGO_TYPE cryptAlgo,
							 const BOOLEAN useFixedKey,
							 const CRYPT_KEYSET cryptKeyset )
	{
	CRYPT_CERTIFICATE cryptRequest;
	int status;

	/* Create the CMP (CRMF) request */
	if( privateKey != CRYPT_UNUSED )
		{
		time_t startTime;
		int dummy;

		/* If we're updating an existing cert we have to vary something in
		   the request to make sure that the result doesn't duplicate an
		   existing cert, to do this we fiddle the start time */
		status = cryptGetAttributeString( privateKey, CRYPT_CERTINFO_VALIDFROM,
										  &startTime, &dummy );
		if( cryptStatusError( status ) )
			return( FALSE );
		startTime++;

		/* It's an update of existing information, sign the request with the
		   given private key */
		status = cryptCreateCert( &cryptRequest, CRYPT_UNUSED,
								  CRYPT_CERTTYPE_REQUEST_CERT );
		if( cryptStatusOK( status ) )
			status = cryptSetAttribute( cryptRequest,
						CRYPT_CERTINFO_CERTIFICATE, privateKey );
		if( cryptStatusOK( status ) )
			status = cryptSetAttributeString( cryptRequest,
						CRYPT_CERTINFO_VALIDFROM, &startTime, sizeof( time_t ) );
		if( cryptStatusOK( status ) )
			status = cryptSignCert( cryptRequest, privateKey );
		if( cryptKeyset != CRYPT_UNUSED )
			{
			if( cryptStatusError( \
					cryptAddPrivateKey( cryptKeyset, privateKey,
										TEST_PRIVKEY_PASSWORD ) ) )
				return( FALSE );
			}
		}
	else
		{
		CRYPT_CONTEXT cryptContext;

		/* It's a new request, generate a private key and create a self-
		   signed request */
		if( useFixedKey )
			{
			/* Use a fixed private key, for testing purposes */
			if( cryptAlgo == CRYPT_ALGO_RSA )
				loadRSAContextsEx( CRYPT_UNUSED, NULL, &cryptContext, NULL,
								   USER_PRIVKEY_LABEL, FALSE, FALSE );
			else
				loadDSAContextsEx( CRYPT_UNUSED, &cryptContext, NULL,
								   USER_PRIVKEY_LABEL, NULL );
			status = CRYPT_OK;
			}
		else
			{
			cryptCreateContext( &cryptContext, CRYPT_UNUSED, cryptAlgo );
			cryptSetAttributeString( cryptContext, CRYPT_CTXINFO_LABEL,
									 USER_PRIVKEY_LABEL,
									 paramStrlen( USER_PRIVKEY_LABEL ) );
			status = cryptGenerateKey( cryptContext );
			}
		if( cryptStatusOK( status ) )
			status = cryptCreateCert( &cryptRequest, CRYPT_UNUSED,
									  CRYPT_CERTTYPE_REQUEST_CERT );
		if( cryptStatusOK( status ) )
			status = cryptSetAttribute( cryptRequest,
						CRYPT_CERTINFO_SUBJECTPUBLICKEYINFO, cryptContext );
		if( cryptStatusOK( status ) && \
			!addCertFields( cryptRequest, requestData, __LINE__ ) )
			status = CRYPT_ERROR_FAILED;
		if( cryptStatusOK( status ) )
			status = cryptSignCert( cryptRequest, cryptContext );
		if( cryptKeyset != CRYPT_UNUSED )
			{
			if( cryptStatusError( \
					cryptAddPrivateKey( cryptKeyset, cryptContext,
										TEST_PRIVKEY_PASSWORD ) ) )
				return( FALSE );
			}
		cryptDestroyContext( cryptContext );
		}
	if( cryptStatusError( status ) )
		{
		printf( "Creation of CMP request failed with error code %d, line "
				"%d.\n", status, __LINE__ );
		return( FALSE );
		}

	return( cryptRequest );
	}

static int createCmpRevRequest( const CRYPT_CERTIFICATE cryptCert )
	{
	CRYPT_CERTIFICATE cryptRequest;
	int status;

	/* Create the CMP (CRMF) revocation request */
	status = cryptCreateCert( &cryptRequest, CRYPT_UNUSED,
							  CRYPT_CERTTYPE_REQUEST_REVOCATION );
	if( cryptStatusOK( status ) )
		status = cryptSetAttribute( cryptRequest, CRYPT_CERTINFO_CERTIFICATE,
									cryptCert );
	if( cryptStatusError( status ) )
		{
		printf( "Creation of CMP revocation request failed with error code "
				"%d, line %d.\n", status, __LINE__ );
		return( FALSE );
		}

	return( cryptRequest );
	}

static int createCmpSession( const CRYPT_CONTEXT cryptCACert,
							 const C_STR server, const C_STR user,
							 const C_STR password,
							 const CRYPT_CONTEXT privateKey,
							 const BOOLEAN isRevocation,
							 const BOOLEAN isUpdate,
							 const BOOLEAN isPKIBoot )
	{
	CRYPT_SESSION cryptSession;
	int status;

	/* Create the CMP session */
	status = cryptCreateSession( &cryptSession, CRYPT_UNUSED,
								 CRYPT_SESSION_CMP );
	if( status == CRYPT_ERROR_PARAM3 )	/* CMP session access not available */
		return( CRYPT_ERROR_NOTAVAIL );
	if( cryptStatusError( status ) )
		{
		printf( "cryptCreateSession() failed with error code %d, line %d.\n",
				status, __LINE__ );
		return( FALSE );
		}

	/* Set up the user and server information.  Revocation requests can be
	   signed or MACd so we handle either.  When requesting a cert using a
	   signed request (i.e.not an initialisation request) we use an update
	   since we're reusing the previously-generated cert data to request a
	   new one and some CAs won't allow this reuse for a straight request
	   but require explicit use of an update request */
	if( privateKey != CRYPT_UNUSED )
		{
		status = cryptSetAttribute( cryptSession,
									CRYPT_SESSINFO_CMP_REQUESTTYPE,
									isRevocation ? \
										CRYPT_REQUESTTYPE_REVOCATION : \
									isUpdate ? \
										CRYPT_REQUESTTYPE_KEYUPDATE : \
										CRYPT_REQUESTTYPE_CERTIFICATE );
		if( cryptStatusOK( status ) )
			status = cryptSetAttribute( cryptSession,
										CRYPT_SESSINFO_PRIVATEKEY,
										privateKey );
		}
	else
		{
		status = cryptSetAttributeString( cryptSession,
										  CRYPT_SESSINFO_USERNAME, user,
										  paramStrlen( user ) );
		if( cryptStatusOK( status ) )
			status = cryptSetAttribute( cryptSession,
										CRYPT_SESSINFO_CMP_REQUESTTYPE,
										isPKIBoot ? \
											CRYPT_REQUESTTYPE_PKIBOOT : \
										isRevocation ? \
											CRYPT_REQUESTTYPE_REVOCATION : \
											CRYPT_REQUESTTYPE_INITIALISATION );
		if( cryptStatusOK( status ) )
			status = cryptSetAttributeString( cryptSession,
									CRYPT_SESSINFO_PASSWORD, password,
									paramStrlen( password ) );
		}
	if( cryptStatusOK( status ) )
		status = cryptSetAttributeString( cryptSession,
										CRYPT_SESSINFO_SERVER_NAME, server,
										paramStrlen( server ) );
	if( cryptStatusOK( status ) && cryptCACert != CRYPT_UNUSED )
		status = cryptSetAttribute( cryptSession,
									CRYPT_SESSINFO_CACERTIFICATE,
									cryptCACert );
	if( cryptStatusError( status ) )
		{
		printf( "Addition of session information failed with error code %d, "
				"line %d.\n", status, __LINE__ );
		return( FALSE );
		}

	return( cryptSession );
	}

/* Request a particular cert type */

static int requestCert( const char *description, const CA_INFO *caInfoPtr,
						const C_STR readKeysetName,
						const C_STR writeKeysetName,
						const CERT_DATA *requestData,
						const CRYPT_ALGO_TYPE cryptAlgo,
						const CRYPT_CONTEXT cryptCACert,
						const BOOLEAN isPKIBoot, const BOOLEAN isDupIR,
						CRYPT_CERTIFICATE *issuedCert )
	{
	CRYPT_SESSION cryptSession;
	CRYPT_KEYSET cryptKeyset = CRYPT_UNUSED;
	CRYPT_CONTEXT privateKey = CRYPT_UNUSED;
	CRYPT_CERTIFICATE cryptCmpRequest, cryptCmpResponse;
	const BOOLEAN useExistingKey = ( requestData == NULL ) ? TRUE : FALSE;
	int status;

#ifdef SERVER_PROVIDES_DN
	printf( "Testing %s processing with absent subject DN...\n", description );
#else
	printf( "Testing %s processing...\n", description );
#endif /* SERVER_PROVIDES_DN */

	/* Read the key needed to request a new cert from a keyset if necessary,
	   and create a keyset to save a new key to if required.  We have to do
	   the write last in case the read and write keyset are the same */
	if( readKeysetName != NULL )
		{
		status = getPrivateKey( &privateKey, readKeysetName,
								USER_PRIVKEY_LABEL, TEST_PRIVKEY_PASSWORD );
		if( cryptStatusError( status ) )
			{
			printf( "Couldn't get private key to request new certificate, "
					"status = %d, line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		}
	if( writeKeysetName != NULL )
		{
		status = cryptKeysetOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_KEYSET_FILE, writeKeysetName,
								  CRYPT_KEYOPT_CREATE );
		if( cryptStatusError( status ) )
			{
			printf( "Couldn't create keyset to store certificate to, "
					"status = %d, line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		}

	/* Create the CMP session */
	cryptSession = createCmpSession( cryptCACert, caInfoPtr->url,
									 caInfoPtr->user, caInfoPtr->password,
									 privateKey, FALSE, useExistingKey,
									 isPKIBoot );
	if( cryptSession <= 0 )
		{
		if( cryptKeyset != CRYPT_UNUSED )
			cryptKeysetClose( cryptKeyset );
		return( cryptSession );
		}

	/* Set up the request.  Some CAs explicitly disallow multiple dissimilar
	   certs to exist for the same key (in fact for non-test servers other
	   CAs probably enforce this as well) but generating a new key for each
	   request is time-consuming so we only do it if it's enforced by the
	   CA */
	if( !isPKIBoot )
		{
#if defined( SERVER_IS_CRYPTLIB ) || defined( SERVER_FIXED_DN )
		cryptCmpRequest = createCmpRequest( requestData,
								useExistingKey ? privateKey : CRYPT_UNUSED,
								cryptAlgo, FALSE, cryptKeyset );
#else
		KLUDGE_WARN( "fixed key for request" );
		cryptCmpRequest = createCmpRequest( requestData,
								useExistingKey ? privateKey : CRYPT_UNUSED,
								cryptAlgo, TRUE, cryptKeyset );
#endif /* cryptlib and Initech won't allow two certs for same key */
		if( !cryptCmpRequest )
			return( FALSE );
		if( privateKey != CRYPT_UNUSED )
			cryptDestroyContext( privateKey );
		status = cryptSetAttribute( cryptSession, CRYPT_SESSINFO_REQUEST,
									cryptCmpRequest );
		cryptDestroyCert( cryptCmpRequest );
		if( cryptStatusError( status ) )
			{
			printf( "cryptSetAttribute() failed with error code %d, line %d.\n",
					status, __LINE__ );
			return( FALSE );
			}
		}

	/* Activate the session */
	status = cryptSetAttribute( cryptSession, CRYPT_SESSINFO_ACTIVE, TRUE );
	if( cryptStatusError( status ) )
		{
		if( cryptKeyset != CRYPT_UNUSED )
			cryptKeysetClose( cryptKeyset );
		if( isDupIR && status == CRYPT_ERROR_DUPLICATE )
			{
			/* If we're trying to get a duplicate cert issued then we're
			   supposed to fail at this point */
			cryptDestroySession( cryptSession );
			return( FALSE );
			}
		printExtError( cryptSession, "Attempt to activate CMP client session",
					   status, __LINE__ );
		cryptDestroySession( cryptSession );
		if( status == CRYPT_ERROR_OPEN || status == CRYPT_ERROR_READ )
			{
			/* These servers are constantly appearing and disappearing so if
			   we get a straight connect error we don't treat it as a serious
			   failure */
			puts( "  (Server could be down, faking it and continuing...)\n" );
			return( CRYPT_ERROR_FAILED );
			}
		if( status == CRYPT_ERROR_FAILED )
			{
			/* A general failed response is more likely to be due to the
			   server doing something unexpected than a cryptlib problem so
			   we don't treat it as a fatal error */
			puts( "  (This is more likely to be an issue with the server than "
				  "with cryptlib,\n   faking it and continuing...)\n" );
			return( CRYPT_ERROR_FAILED );
			}
		return( FALSE );
		}

	/* If it's a PKIBoot, which just sets (implicitly) trusted certs, we're
	   done */
	if( isPKIBoot )
		{
		cryptDestroySession( cryptSession );
		return( TRUE );
		}

	/* Obtain the response information */
	status = cryptGetAttribute( cryptSession, CRYPT_SESSINFO_RESPONSE,
								&cryptCmpResponse );
	cryptDestroySession( cryptSession );
	if( cryptStatusError( status ) )
		{
		printf( "cryptGetAttribute() failed with error code %d, line %d.\n",
				status, __LINE__ );
		return( FALSE );
		}
#ifndef SERVER_IS_CRYPTLIB
	puts( "Returned certificate details are:" );
	printCertInfo( cryptCmpResponse );
#endif /* Keep the cryptlib results on one screen */
	if( cryptKeyset != CRYPT_UNUSED )
		{
		status = cryptAddPublicKey( cryptKeyset, cryptCmpResponse );
		if( cryptStatusError( status ) )
			{
			printf( "Couldn't write certificate to keyset, status = %d, "
					"line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		cryptKeysetClose( cryptKeyset );
		}
	if( issuedCert != NULL )
		*issuedCert = cryptCmpResponse;
	else
		cryptDestroyCert( cryptCmpResponse );

	/* Clean up */
	printf( "%s processing succeeded.\n\n", description );
	return( TRUE );
	}

/* Revoke a previously-issued cert */

static int revokeCert( const char *description, const CA_INFO *caInfoPtr,
					   const C_STR keysetName,
					   const CRYPT_CERTIFICATE certToRevoke,
					   const CRYPT_CONTEXT cryptCACert,
					   const BOOLEAN signRequest )
	{
	CRYPT_SESSION cryptSession;
	CRYPT_CONTEXT privateKey = CRYPT_UNUSED;
	CRYPT_CERTIFICATE cryptCmpRequest, cryptCert = certToRevoke;
	int status;

	printf( "Testing %s revocation processing...\n", description );

	/* Get the cert to revoke if necessary.  In some cases the server won't
	   accept a revocation password, so we have to get the private key as
	   well to sign the request */
	if( signRequest || cryptCert == CRYPT_UNUSED )
		{
		CRYPT_KEYSET cryptKeyset;

		status = cryptKeysetOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_KEYSET_FILE, keysetName,
								  CRYPT_KEYOPT_READONLY );
		if( cryptStatusOK( status ) && signRequest )
			status = getPrivateKey( &privateKey, keysetName,
									USER_PRIVKEY_LABEL,
									TEST_PRIVKEY_PASSWORD );
		if( cryptStatusOK( status ) && cryptCert == CRYPT_UNUSED )
			status = cryptGetPublicKey( cryptKeyset, &cryptCert,
										CRYPT_KEYID_NAME,
										USER_PRIVKEY_LABEL );
		cryptKeysetClose( cryptKeyset );
		if( cryptStatusError( status ) )
			{
			puts( "Couldn't fetch certificate/key to revoke.\n" );
			return( FALSE );
			}
		}

	/* Create the CMP session and revocation request */
	cryptSession = createCmpSession( cryptCACert, caInfoPtr->url,
									 caInfoPtr->user, caInfoPtr->password,
									 privateKey, TRUE, FALSE, FALSE );
	if( privateKey != CRYPT_UNUSED )
		cryptDestroyContext( privateKey );
	if( cryptSession <= 0 )
		return( cryptSession );
	cryptCmpRequest = createCmpRevRequest( cryptCert );
	if( !cryptCmpRequest )
		return( FALSE );

	/* Set up the request and activate the session */
	status = cryptSetAttribute( cryptSession, CRYPT_SESSINFO_REQUEST,
								cryptCmpRequest );
	cryptDestroyCert( cryptCmpRequest );
	if( cryptStatusError( status ) )
		{
		printf( "cryptSetAttribute() failed with error code %d, line %d.\n",
				status, __LINE__ );
		return( FALSE );
		}
	status = cryptSetAttribute( cryptSession, CRYPT_SESSINFO_ACTIVE, TRUE );
	if( cryptStatusError( status ) )
		{
		printExtError( cryptSession, "Attempt to activate CMP client session",
					   status, __LINE__ );
		cryptDestroySession( cryptSession );
		if( cryptCert != certToRevoke )
			cryptDestroyCert( cryptCert );
		if( status == CRYPT_ERROR_OPEN || status == CRYPT_ERROR_READ )
			{
			/* These servers are constantly appearing and disappearing so if
			   we get a straight connect error we don't treat it as a serious
			   failure */
			puts( "  (Server could be down, faking it and continuing...)\n" );
			return( CRYPT_ERROR_FAILED );
			}
		if( status == CRYPT_ERROR_FAILED )
			{
			/* A general failed response is more likely to be due to the
			   server doing something unexpected than a cryptlib problem so
			   we don't treat it as a fatal error */
			puts( "  (This is more likely to be an issue with the server than "
				  "with cryptlib,\n   faking it and continuing...)\n" );
			return( CRYPT_ERROR_FAILED );
			}
		return( FALSE );
		}

	/* Clean up */
	if( cryptCert != certToRevoke )
		cryptDestroyCert( cryptCert );
	cryptDestroySession( cryptSession );
	printf( "%s processing succeeded.\n\n", description );
	return( TRUE );
	}

/* Test the full range of CMP functionality.  This performs the following
   tests:

	RSA sign:
		ir + ip + reject (requires cmp.c mod)
		ir + ip + certconf + pkiconf
		kur + kup + certconf + pkiconf
		cr + cp + certconf + pkiconf (not performed since same as kur)
		rr + rp (of ir cert)
		rr + rp (of kur cert)
	RSA encr.:
		ir + ip + reject (requires cmp.c mod)
		ir + ip + certconf + pkiconf
		rr + rp (of ir cert)
	DSA:
		cr + cp + certconf + pkiconf (success implies that ir/kur/rr
						works since they've already been tested for RSA) */

static int connectCMP( const BOOLEAN usePKIBoot, const BOOLEAN localSession )
	{
	CRYPT_CERTIFICATE cryptCACert = CRYPT_UNUSED, cryptCert;
	C_CHR readFileName[ FILENAME_BUFFER_SIZE ];
	C_CHR writeFileName[ FILENAME_BUFFER_SIZE ];
#ifdef SERVER_IS_CRYPTLIB
	CA_INFO cryptlibCAInfo, *caInfoPtr = &cryptlibCAInfo;
  #ifdef TEST_IR
	C_CHR userID[ 64 ], issuePW[ 64 ];
  #endif /* SERVER_IS_CRYPTLIB */
#else
	const CA_INFO *caInfoPtr = &caInfo[ CA_NO ];
#endif /* cryptlib */
	int status;

#ifdef SERVER_IS_CRYPTLIB
	/* Wait for the server to finish initialising */
	if( localSession && waitMutex() == CRYPT_ERROR_TIMEOUT )
		{
		printf( "Timed out waiting for server to initialise, line %d.\n",
				__LINE__ );
		return( FALSE );
		}

	/* Set up the fixed info in the CA info record */
	memcpy( &cryptlibCAInfo, &caInfo[ CA_NO ], sizeof( CA_INFO ) );
	cryptlibCAInfo.name = "cryptlib";

	/* Make sure that the required user info is present.  If it isn't, the
	   CA auditing will detect a request from a nonexistant user and refuse
	   to issue a certificate */
	if( !pkiGetUserInfo( NULL, NULL, NULL, TEXT( "Test PKI user" ) ) )
		{
		puts( "CA certificate store doesn't contain the PKI user "
			  "information needed to\nauthenticate certificate issue "
			  "operations.  This is probably because the\nserver loopback "
			  "test (which initialises the cert store) hasn't been run "
			  "yet.\nSkipping CMP test.\n" );
		return( CRYPT_ERROR_NOTAVAIL );
		}
#endif /* SERVER_IS_CRYPTLIB */

	/* Get the cert of the CA who will issue the cert unless we're doing a
	   PKIBoot, in which case the cert is obtained during the PKIBoot
	   process */
#ifndef SERVER_IS_CRYPTLIB
	printf( "Using the %s CMP server.\n", caInfoPtr->name );
#endif /* !SERVER_IS_CRYPTLIB */
#ifndef SERVER_PKIBOOT
	status = importCertFromTemplate( &cryptCACert, CMP_CA_FILE_TEMPLATE,
									 CA_NO );
	if( cryptStatusError( status ) )
		{
		printf( "Couldn't get CMP CA certificate, status = %d, line %d.\n",
				status, __LINE__ );
		return( FALSE );
		}
#endif /* !SERVER_PKIBOOT */

	/* Test each cert request type: Initialisation, cert request using cert
	   from initialisation for authentication, key update of cert from
	   initialisation, revocation of both certs.  We insert a 1s delay
	   between requests to give the server time to recycle */

	/* Initialisation request */
#ifdef TEST_IR
  #ifdef SERVER_IS_CRYPTLIB
	/* cryptlib implements per-user (rather than shared interop) IDs and
	   passwords so we need to read the user ID and password information
	   before we can perform any operations */
	status = pkiGetUserInfo( userID, issuePW, NULL,
							 TEXT( "Test PKI user" ) );
	if( status == CRYPT_ERROR_NOTAVAIL )
		{
		/* Cert store operations aren't available, exit but continue with
		   other tests */
	#ifndef SERVER_PKIBOOT
		cryptDestroyCert( cryptCACert );
	#endif /* !SERVER_PKIBOOT */
		return( TRUE );
		}
	else
		if( !status )
			{
	#ifndef SERVER_PKIBOOT
			cryptDestroyCert( cryptCACert );
	#endif /* !SERVER_PKIBOOT */
			return( FALSE );
			}

	/* Set up the variable info in the CA info record */
	cryptlibCAInfo.user = userID;
	cryptlibCAInfo.password = issuePW;
  #endif /* SERVER_IS_CRYPTLIB */
	/* Initialisation.  We define REVOKE_FIRST_CERT to indicate that we can
	   revoke this one later on */
	#define REVOKE_FIRST_CERT
	filenameParamFromTemplate( writeFileName, CMP_PRIVKEY_FILE_TEMPLATE, 1 );
	status = requestCert( "RSA signing cert.init.request", caInfoPtr, NULL,
						  usePKIBoot ? NULL : writeFileName,
#if defined( SERVER_PROVIDES_DN )
						  cmpRsaSignRequestNoDNData,
#else
						  cmpRsaSignRequestData,
#endif /* SERVER_PROVIDES_DN */
						  CRYPT_ALGO_RSA, cryptCACert, usePKIBoot, FALSE,
						  &cryptCert );
	if( status != TRUE )
		{
		/* If this is the self-test and there's a non-fatal error, make sure
		   we don't fail with a CRYPT_ERROR_INCOMPLETE when we're finished */
		cryptDestroyCert( cryptCACert );
		return( status );
		}
	if( usePKIBoot )
		{
		/* If we're testing the PKIBoot capability, there's only a single
		   request to process */
		cryptDestroyCert( cryptCACert );
		return( TRUE );
		}
	delayThread( 2 );
#endif /* TEST_IR */
#ifdef TEST_DUP_IR
	/* Attempt a second ir using the same PKI user data.  This should fail,
	   since the cert store only allows a single ir per user */
	if( requestCert( "Duplicate init.request", caInfoPtr, NULL, NULL,
					 cmpRsaSignRequestNoDNData, CRYPT_ALGO_RSA, cryptCACert,
					 FALSE, TRUE, NULL ) )
		{
		printf( "Duplicate init request wasn't detected by the CMP server, "
				"line %d.\n\n", __LINE__ );
		cryptDestroyCert( cryptCACert );
		return( FALSE );
		}
#endif /* TEST_DUP_IR */

	/* Cert request.  We have to perform this test before the kur since some
	   CAs implicitly revoke the cert being replaced, which means we can't
	   use it to authenticate requests any more once the kur has been
	   performed */
#ifdef TEST_CR
	/* We define REVOKE_SECOND_CERT to indicate that we can revoke this one
	   later on alongside the ir/kur'd cert, and save a copy to a file for
	   later use */
	#define REVOKE_SECOND_CERT
	filenameParamFromTemplate( readFileName, CMP_PRIVKEY_FILE_TEMPLATE, 1 );
	filenameParamFromTemplate( writeFileName, CMP_PRIVKEY_FILE_TEMPLATE, 2 );
	status = requestCert( "RSA signing certificate request", caInfoPtr,
						  readFileName, writeFileName, cmpRsaSignRequestData,
						  CRYPT_ALGO_RSA, cryptCACert, FALSE, FALSE, NULL );
	if( status != TRUE )
		{
  #if defined( TEST_IR )
		cryptDestroyCert( cryptCert );
  #endif /* TEST_IR || TEST_KUR */
		cryptDestroyCert( cryptCACert );
		return( status );
		}
	delayThread( 2 );
#endif /* TEST_CR */

	/* Key update request */
#ifdef TEST_KUR
  #ifdef TEST_IR
	/* We just created the cert, delete it so we can replace it with the
	   updated form */
	cryptDestroyCert( cryptCert );
  #endif /* TEST_IR */

	/* If it's a CA that implicitly revokes the cert being replaced (in
	   which case tracking things gets a bit too complicated since we now
	   need to use the updated rather than original cert to authenticate the
	   request) we just leave it unrevoked (the first cert is always
	   revoked) */
  #ifdef SERVER_IS_CRYPTLIB
	#undef REVOKE_FIRST_CERT
  #endif /* SERVER_IS_CRYPTLIB */

	/* Key update */
	filenameParamFromTemplate( readFileName, CMP_PRIVKEY_FILE_TEMPLATE, 1 );
	status = requestCert( "RSA signing certificate update", caInfoPtr,
						  readFileName, NULL, NULL, CRYPT_UNUSED,
						  cryptCACert, FALSE, FALSE, &cryptCert );
	if( status != TRUE )
		{
		cryptDestroyCert( cryptCACert );
		return( status );
		}
	delayThread( 2 );
#endif /* TEST_KUR */
#if 0
	/* DSA cert request.  We have to get this now because we're about to
	   revoke the cert we're using to sign the requests */
	filenameParamFromTemplate( readFileName, CMP_PRIVKEY_FILE_TEMPLATE, 1 );
	status = requestCert( "DSA certificate", caInfoPtr, readFileName, NULL,
						  cmpDsaRequestData, CRYPT_ALGO_DSA, cryptCACert,
						  FALSE, FALSE, NULL );
	if( status != TRUE )
		return( status );
	delayThread( 2 );
#endif /* 0 */
#if 0
	/* Encryption-only cert request.  This test requires a change in
	   certsign.c because when creating a cert request cryptlib always
	   allows signing for the request even if it's an encryption-only key
	   (this is required for PKCS #10, see the comment in the kernel code).
	   Because of this a request will always appear to be associated with a
	   signature-enabled key so it's necessary to make a code change to
	   disallow this.  Disallowing sigs for encryption-only keys would break
	   PKCS #10 since it's then no longer possible to create the self-signed
	   request, this is a much bigger concern than CMP.  Note that this
	   functionality is tested by the PnP PKI test, which creates the
	   necessary encryption-only requests internally and can do things that
	   we can't do from the outside */
	status = requestCert( "RSA encryption certificate", caInfoPtr,
						  readFileName, writeFileName, cmpRsaEncryptRequestData,
						  CRYPT_ALGO_RSA, cryptCACert, FALSE, FALSE, NULL );
	if( status != TRUE )
		return( status );
	delayThread( 2 );
#endif /* 0 */

	/* Revocation request */
#ifdef TEST_RR
	filenameParamFromTemplate( readFileName, CMP_PRIVKEY_FILE_TEMPLATE, 1 );
  #ifdef REVOKE_FIRST_CERT
	#ifdef SERVER_IR_DN
	status = revokeCert( "RSA initial/updated certificate", caInfoPtr,
						 readFileName, cryptCert, cryptCACert, TRUE );
	#else
	status = revokeCert( "RSA initial/updated certificate", caInfoPtr,
						 readFileName, cryptCert, cryptCACert, FALSE );
	#endif /* Certicom requires signed request */
	cryptDestroyCert( cryptCert );
	delayThread( 2 );
  #elif !defined( TEST_KUR ) || !defined( SERVER_IS_CRYPTLIB )
	/* We didn't issue the first cert in this run, try revoking it from
	   the cert stored in the key file unless we're talking to a CA that
	   implicitly revokes the cert being replaced during a kur */
	status = revokeCert( "RSA initial/updated certificate", caInfoPtr,
						 readFileName, CRYPT_UNUSED, cryptCACert, TRUE );
  #else
	/* This is a kur'd cert for which the original has been implicitly
	   revoked, we can't do much else with it */
	cryptDestroyCert( cryptCert );
  #endif /* REVOKE_FIRST_CERT */
	if( status != TRUE )
		{
		cryptDestroyCert( cryptCACert );
		return( status );
		}
  #ifdef REVOKE_SECOND_CERT
	/* We requested a second cert, revoke that too.  Note that we have to
	   sign this with the second cert since the first one may have just been
	   revoked */
	filenameParamFromTemplate( readFileName, CMP_PRIVKEY_FILE_TEMPLATE, 2 );
	status = revokeCert( "RSA signing certificate", caInfoPtr, readFileName,
						 CRYPT_UNUSED, cryptCACert, TRUE );
	if( status != TRUE )
		{
		cryptDestroyCert( cryptCACert );
		return( status );
		}
  #endif /* REVOKE_SECOND_CERT */
#endif /* TEST_RR */

	/* Clean up */
	cryptDestroyCert( cryptCACert );
	return( TRUE );
	}

int testSessionCMP( void )
	{
	return( connectCMP( FALSE, FALSE ) );
	}

/* Test the plug-and-play PKI functionality */

static int connectPNPPKI( const BOOLEAN isCaUser, const BOOLEAN useDevice,
						  const BOOLEAN localSession )
	{
	CRYPT_SESSION cryptSession;
	CRYPT_KEYSET cryptKeyset;
	C_CHR userID[ 64 ], issuePW[ 64 ];
	int status;

	/* Create the CMP session */
	status = cryptCreateSession( &cryptSession, CRYPT_UNUSED,
								 CRYPT_SESSION_CMP );
	if( status == CRYPT_ERROR_PARAM3 )	/* CMP session access not available */
		return( CRYPT_ERROR_NOTAVAIL );
	if( cryptStatusError( status ) )
		{
		printf( "cryptCreateSession() failed with error code %d, line %d.\n",
				status, __LINE__ );
		return( FALSE );
		}

	/* Open the device/create the keyset to contain the keys.  This doesn't
	   perform a full device.c-style auto-configure but assumes that it's
	   talking to a device that's already been initialised and is ready to
	   go */
	if( useDevice )
		{
		status = cryptDeviceOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_DEVICE_PKCS11,
								  TEXT( "[Autodetect]" ) );
		if( cryptStatusError( status ) )
			{
			printf( "Crypto device open failed with error code %d, "
					"line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		status = cryptSetAttributeString( cryptKeyset,
										  CRYPT_DEVINFO_AUTHENT_USER,
										  "test", 4 );
		if( cryptStatusError( status ) )
			{
			printf( "\nDevice login failed with error code %d, line %d.\n",
					status, __LINE__ );
			return( FALSE );
			}
		if( cryptDeleteKey( cryptKeyset, CRYPT_KEYID_NAME,
							TEXT( "Signature key" ) ) == CRYPT_OK )
			puts( "(Deleted a signature key object, presumably a leftover "
				  "from a previous run)." );
		if( cryptDeleteKey( cryptKeyset, CRYPT_KEYID_NAME,
							TEXT( "Encryption key" ) ) == CRYPT_OK )
			puts( "(Deleted an encryption key object, presumably a leftover "
				  "from a previous run)." );
		}
	else
		{
		status = cryptKeysetOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_KEYSET_FILE, isCaUser ? \
										PNPCA_PRIVKEY_FILE : PNP_PRIVKEY_FILE,
								  CRYPT_KEYOPT_CREATE );
		if( cryptStatusError( status ) )
			{
			printf( "User keyset create failed with error code %d, "
					"line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		}

	/* Wait for the server to finish initialising */
	if( localSession && waitMutex() == CRYPT_ERROR_TIMEOUT )
		{
		printf( "Timed out waiting for server to initialise, line %d.\n",
				__LINE__ );
		return( FALSE );
		}

	/* Get information needed for enrolment */
	status = pkiGetUserInfo( userID, issuePW, NULL, isCaUser ? \
								TEXT( "Test CA PKI user" ) : \
								TEXT( "Test PKI user" ) );
	if( status == CRYPT_ERROR_NOTAVAIL )
		/* Cert store operations aren't available, exit but continue with
		   other tests */
		return( TRUE );
	else
		if( !status )
			return( FALSE );

	/* Set up the information we need for the plug-and-play PKI process */
	status = cryptSetAttributeString( cryptSession,
									  CRYPT_SESSINFO_USERNAME, userID,
									  paramStrlen( userID ) );
	if( cryptStatusOK( status ) )
		status = cryptSetAttributeString( cryptSession,
										  CRYPT_SESSINFO_PASSWORD,
										  issuePW, paramStrlen( issuePW ) );
	if( cryptStatusOK( status ) )
		status = cryptSetAttributeString( cryptSession,
										  CRYPT_SESSINFO_SERVER_NAME,
										  caInfo[ CA_CRYPTLIB_PNPPKI ].url,
										  paramStrlen( caInfo[ CA_CRYPTLIB_PNPPKI ].url ) );
	if( cryptStatusOK( status ) )
		status = cryptSetAttribute( cryptSession,
									CRYPT_SESSINFO_CMP_PRIVKEYSET,
									cryptKeyset );
	if( cryptStatusOK( status ) && useDevice )
		{
		/* Keygen on a device can take an awfully long time for some devices,
		   so we set an extended timeout to allow for this */
		cryptSetAttribute( cryptSession, CRYPT_OPTION_NET_READTIMEOUT,
						   NET_TIMEOUT );
		status = cryptSetAttribute( cryptSession,
									CRYPT_OPTION_NET_WRITETIMEOUT,
									NET_TIMEOUT );
		}
	if( useDevice )
		cryptDeviceClose( cryptKeyset );
	else
		cryptKeysetClose( cryptKeyset );
	if( cryptStatusError( status ) )
		{
		printf( "Addition of session information failed with error code %d, "
				"line %d.\n", status, __LINE__ );
		return( FALSE );
		}

	/* Activate the session */
	status = cryptSetAttribute( cryptSession, CRYPT_SESSINFO_ACTIVE, TRUE );
	if( cryptStatusError( status ) )
		{
		printExtError( cryptSession, "Attempt to activate plug-and-play PKI "
					   "client session", status, __LINE__ );
		cryptDestroySession( cryptSession );
		return( FALSE );
		}

	/* Clean up */
	cryptDestroySession( cryptSession );

	/* If this is the intermediate CA cert, change the password to allow it
	   to be used with the standard PnP PKI test */
	if( isCaUser )
		{
		CRYPT_CONTEXT cryptKey;

		/* Get the newly-issued key */
		status = cryptKeysetOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_KEYSET_FILE, PNPCA_PRIVKEY_FILE,
								  CRYPT_KEYOPT_NONE );
		if( cryptStatusOK( status ) )
			{
			status = cryptGetPrivateKey( cryptKeyset, &cryptKey,
										 CRYPT_KEYID_NAME,
										 TEXT( "Signature key" ), issuePW );
			cryptKeysetClose( cryptKeyset );
			}
		if( cryptStatusError( status ) )
			{
			printf( "Certified private-key read failed with error code %d, "
					"line %d.\n", status, __LINE__ );
			return( FALSE );
			}

		/* Replace the keyset with one with the key protected with a
		   different password */
		status = cryptKeysetOpen( &cryptKeyset, CRYPT_UNUSED,
								  CRYPT_KEYSET_FILE, PNPCA_PRIVKEY_FILE,
								  CRYPT_KEYOPT_CREATE );
		if( cryptStatusOK( status ) )
			{
			status = cryptAddPrivateKey( cryptKeyset, cryptKey,
										 TEST_PRIVKEY_PASSWORD );
			cryptKeysetClose( cryptKeyset );
			}
		cryptDestroyContext( cryptKey );
		if( cryptStatusError( status ) )
			{
			printf( "Certified private-key password change failed with error "
					"code %d, line %d.\n", status, __LINE__ );
			return( FALSE );
			}
		}

	return( TRUE );
	}

int testSessionPNPPKI( void )
	{
	return( connectPNPPKI( FALSE, FALSE, FALSE ) );
	}

/* Test the CMP server */

static int cmpServerSingleIteration( const CRYPT_CONTEXT cryptPrivateKey,
									 const CRYPT_KEYSET cryptCertStore,
									 const BOOLEAN useDevice )
	{
	CRYPT_SESSION cryptSession;
	int status;

	/* Create the CMP session and add the CA key and cert store */
	status = cryptCreateSession( &cryptSession, CRYPT_UNUSED,
								 CRYPT_SESSION_CMP_SERVER );
	if( cryptStatusError( status ) )
		{
		printf( "SVR: cryptCreateSession() failed with error code %d, line "
				"%d.\n", status, __LINE__ );
		return( FALSE );
		}
	status = cryptSetAttribute( cryptSession,
							CRYPT_SESSINFO_PRIVATEKEY, cryptPrivateKey );
	if( cryptStatusOK( status ) )
		status = cryptSetAttribute( cryptSession,
							CRYPT_SESSINFO_KEYSET, cryptCertStore );
	if( cryptStatusOK( status ) && useDevice )
		{
		/* Keygen on a device can take an awfully long time for some devices,
		   so we set an extended timeout to allow for this */
		cryptSetAttribute( cryptSession, CRYPT_OPTION_NET_READTIMEOUT,
						   NET_TIMEOUT );
		status = cryptSetAttribute( cryptSession,
									CRYPT_OPTION_NET_WRITETIMEOUT,
									NET_TIMEOUT );
		}
	if( cryptStatusError( status ) )
		return( attrErrorExit( cryptSession, "SVR: cryptSetAttribute()",
							   status, __LINE__ ) );
	if( !setLocalConnect( cryptSession, 80 ) )
		return( FALSE );

	/* Activate the session */
	status = activatePersistentServerSession( cryptSession, TRUE );
	if( cryptStatusError( status ) )
		{
		status = extErrorExit( cryptSession, "SVR: Attempt to activate CMP "
							   "server session", status, __LINE__ );
		cryptDestroySession( cryptSession );
		return( status );
		}

	/* We processed the request, clean up */
	cryptDestroySession( cryptSession );
	return( TRUE );
	}

int testSessionCMPServer( void )
	{
	CRYPT_SESSION cryptSession;
	CRYPT_CONTEXT cryptCAKey;
	CRYPT_KEYSET cryptCertStore;
	int caCertTrusted, i, status;

	/* Acquire the init mutex */
	acquireMutex();

	puts( "SVR: Testing CMP server session..." );

	/* Perform a test create of a CMP server session to verify that we can
	   do this test */
	status = cryptCreateSession( &cryptSession, CRYPT_UNUSED,
								 CRYPT_SESSION_CMP_SERVER );
	if( status == CRYPT_ERROR_PARAM3 )	/* CMP session access not available */
		return( CRYPT_ERROR_NOTAVAIL );
	if( cryptStatusError( status ) )
		{
		printf( "SVR: cryptCreateSession() failed with error code %d, "
				"line %d.\n", status, __LINE__ );
		return( FALSE );
		}
	cryptDestroySession( cryptSession );

	/* Set up the server-side objects */
	if( !pkiServerInit( &cryptCAKey, &cryptCertStore, CA_PRIVKEY_FILE,
						CA_PRIVKEY_LABEL, cmpPkiUserData, cmpPkiUserCaData,
						"CMP" ) )
		return( FALSE );

	/* Make the CA key trusted for PKIBoot functionality */
	cryptGetAttribute( cryptCAKey, CRYPT_CERTINFO_TRUSTED_IMPLICIT,
					   &caCertTrusted );
	cryptSetAttribute( cryptCAKey, CRYPT_CERTINFO_TRUSTED_IMPLICIT, 1 );

	/* Tell the client that we're ready to go */
	releaseMutex();

	/* Run the server several times to handle the different requests */
	for( i = 0; i < NO_CA_REQUESTS; i++ )
		{
		printf( "SVR: Running server iteration %d.\n", i + 1 );
		if( !cmpServerSingleIteration( cryptCAKey, cryptCertStore, FALSE ) )
			{
#ifdef SERVER_IS_CRYPTLIB
			/* If we're running the loopback test and this is the second
			   iteration, the client is testing the ability to detect a
			   duplicate ir, so a failure is expected */
			if( i == 1 )
				{
				puts( "SVR: Failure was due to a rejected duplicate request "
					  "from the client,\n     continuing..." );
				continue;
				}
#endif /* SERVER_IS_CRYPTLIB */
			break;
			}
		}
	if( i == 0 )
		/* None of the requests succeeded */
		return( FALSE );
	printf( "SVR: %d of %d server requests were processed.\n", i,
			NO_CA_REQUESTS );

	/* Issue a CRL to make sure that the revocation was performed correctly.
	   We do this now because the cert management self-test can't easily
	   perform the check because it requires a CMP-revoked cert in order to
	   function */
	if( i == NO_CA_REQUESTS )
		{
		CRYPT_CERTIFICATE cryptCRL;
		int noEntries = 0;

		/* Issue the CRL */
		status = cryptCACertManagement( &cryptCRL, CRYPT_CERTACTION_ISSUE_CRL,
										cryptCertStore, cryptCAKey,
										CRYPT_UNUSED );
		if( cryptStatusError( status ) )
			return( extErrorExit( cryptCertStore, "cryptCACertManagement()",
								  status, __LINE__ ) );

		/* Make sure that the CRL contains at least one entry */
		if( cryptStatusOK( cryptSetAttribute( cryptCRL,
											  CRYPT_CERTINFO_CURRENT_CERTIFICATE,
											  CRYPT_CURSOR_FIRST ) ) )
			do
				noEntries++;
			while( cryptSetAttribute( cryptCRL,
									  CRYPT_CERTINFO_CURRENT_CERTIFICATE,
									  CRYPT_CURSOR_NEXT ) == CRYPT_OK );
		if( noEntries <= 0 )
			{
			puts( "CRL created from revoked certificate is empty, should "
				  "contain at least one\ncertificate entry." );
			return( FALSE );
			}

		/* Clean up */
		cryptDestroyCert( cryptCRL );
		}

	/* Clean up */
	if( !caCertTrusted )
		cryptSetAttribute( cryptCAKey, CRYPT_CERTINFO_TRUSTED_IMPLICIT, 0 );
	cryptKeysetClose( cryptCertStore );
	cryptDestroyContext( cryptCAKey );

	puts( "SVR: CMP session succeeded.\n" );
	return( TRUE );
	}

/* Perform a client/server loopback test */

#ifdef WINDOWS_THREADS

static int pnppkiServer( const BOOLEAN pkiBootOnly, const BOOLEAN isCaUser,
						 const BOOLEAN isIntermediateCA,
						 const BOOLEAN useDevice )
	{
	CRYPT_CONTEXT cryptCAKey;
	CRYPT_KEYSET cryptCertStore;
	int caCertTrusted;

	/* Acquire the PNP PKI init mutex */
	acquireMutex();

	printf( "SVR: Testing %s server session%s...\n",
			pkiBootOnly ? "PKIBoot" : "plug-and-play PKI",
			isCaUser ? " for CA cert" : \
				isIntermediateCA ? " using intermediate CA" : "" );

	/* Get the information needed by the server */
	if( isIntermediateCA )
		{
		/* The intermediate CA has a PnP-generated, so the key label is
		   the predefined PnP signature key one */
		if( !pkiServerInit( &cryptCAKey, &cryptCertStore,
							PNPCA_PRIVKEY_FILE, TEXT( "Signature key" ),
							cmpPkiUserData, cmpPkiUserCaData, "CMP" ) )
			return( FALSE );
		}
	else
		{
		if( !pkiServerInit( &cryptCAKey, &cryptCertStore, CA_PRIVKEY_FILE,
							CA_PRIVKEY_LABEL, cmpPkiUserData,
							cmpPkiUserCaData, "CMP" ) )
			return( FALSE );
		}

	/* Make the CA key trusted for PKIBoot functionality */
	cryptGetAttribute( cryptCAKey, CRYPT_CERTINFO_TRUSTED_IMPLICIT,
					   &caCertTrusted );
	cryptSetAttribute( cryptCAKey, CRYPT_CERTINFO_TRUSTED_IMPLICIT, 1 );

	/* Tell the client that we're ready to go */
	releaseMutex();

	/* Run the server once to handle the plug-and-play PKI process */
	if( !cmpServerSingleIteration( cryptCAKey, cryptCertStore, useDevice ) )
		return( FALSE );

	/* Clean up */
	if( !caCertTrusted )
		cryptSetAttribute( cryptCAKey,
						   CRYPT_CERTINFO_TRUSTED_IMPLICIT, 0 );
	cryptKeysetClose( cryptCertStore );
	cryptDestroyContext( cryptCAKey );

	puts( "SVR: Plug-and-play PKI session succeeded.\n" );
	return( TRUE );
	}

unsigned __stdcall cmpServerThread( void *dummy )
	{
	testSessionCMPServer();
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionCMPClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

#ifndef SERVER_IS_CRYPTLIB
	/* Because the code has to handle so many CA-specific peculiarities, we
	   can only perform this test when the CA being used is the cryptlib
	   CA */
	puts( "Error: The local CMP session test only works with the cryptlib "
		  "CA." );
	return( FALSE );
#endif /* !SERVER_IS_CRYPTLIB */

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server */
	status = connectCMP( FALSE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}

unsigned __stdcall cmpPKIBootServerThread( void *dummy )
	{
	pnppkiServer( TRUE, FALSE, FALSE, FALSE );
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionCMPPKIBootClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

#ifndef SERVER_IS_CRYPTLIB
	/* Because the code has to handle so many CA-specific peculiarities, we
	   can only perform this test when the CA being used is the cryptlib
	   CA */
	puts( "Error: The local CMP session test only works with the cryptlib "
		  "CA." );
	return( FALSE );
#endif /* !SERVER_IS_CRYPTLIB */

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpPKIBootServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server with PKIBoot enabled */
	status = connectCMP( TRUE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}

unsigned __stdcall cmpPnPPKIServerThread( void *dummy )
	{
	/* Call with the third parameter set to TRUE to use a chain of CA certs
	   (i.e. an intermediate CA between the root and end user) rather than
	   a single CA cert directly issuing the cert to the end user */
	pnppkiServer( FALSE, FALSE, FALSE, FALSE );
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionPNPPKIClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpPnPPKIServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server with PKIBoot enabled */
	status = connectPNPPKI( FALSE, FALSE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}

unsigned __stdcall cmpPnPPKIDeviceServerThread( void *dummy )
	{
	/* Call with the third parameter set to TRUE to use a chain of CA certs
	   (i.e. an intermediate CA between the root and end user) rather than
	   a single CA cert directly issuing the cert to the end user */
	pnppkiServer( FALSE, FALSE, FALSE, TRUE );
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionPNPPKIDeviceClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpPnPPKIDeviceServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server with PKIBoot enabled */
	status = connectPNPPKI( FALSE, TRUE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}

unsigned __stdcall cmpPnPPKICaServerThread( void *dummy )
	{
	pnppkiServer( FALSE, TRUE, FALSE, FALSE );
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionPNPPKICAClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpPnPPKICaServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server with PKIBoot enabled */
	status = connectPNPPKI( TRUE, FALSE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}

unsigned __stdcall cmpPnPPKIIntermedCaServerThread( void *dummy )
	{
	pnppkiServer( FALSE, FALSE, TRUE, FALSE );
	_endthreadex( 0 );
	return( 0 );
	}

int testSessionPNPPKIIntermedCAClientServer( void )
	{
	HANDLE hThread;
	unsigned threadID;
	int status;

	/* Start the server */
	createMutex();
	hThread = ( HANDLE ) _beginthreadex( NULL, 0, cmpPnPPKIIntermedCaServerThread,
										 NULL, 0, &threadID );
	Sleep( 1000 );

	/* Connect to the local server with PKIBoot enabled */
	status = connectPNPPKI( FALSE, FALSE, TRUE );
	waitForThread( hThread );
	destroyMutex();
	return( status );
	}
#endif /* WINDOWS_THREADS */

#endif /* TEST_SESSION || TEST_SESSION_LOOPBACK */
