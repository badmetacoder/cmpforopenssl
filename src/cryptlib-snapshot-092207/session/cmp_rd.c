/****************************************************************************
*																			*
*								Read CMP Messages							*
*						Copyright Peter Gutmann 1999-2006					*
*																			*
****************************************************************************/

#include <stdio.h>
#if defined( INC_ALL )
  #include "crypt.h"
  #include "asn1.h"
  #include "asn1_ext.h"
  #include "session.h"
  #include "cmp.h"
#else
  #include "crypt.h"
  #include "misc/asn1.h"
  #include "misc/asn1_ext.h"
  #include "session/session.h"
  #include "session/cmp.h"
#endif /* Compiler-specific includes */

/* Prototypes for functions in lib_sign.c */

int checkRawSignature( const void *signature, const int signatureLength,
					   const CRYPT_CONTEXT iSigCheckContext,
					   const CRYPT_CONTEXT iHashContext );

#ifdef USE_CMP

/****************************************************************************
*																			*
*								Utility Routines							*
*																			*
****************************************************************************/

/* Read the CMP/Entrust MAC information:

	macInfo ::= SEQUENCE {
		algoID			OBJECT IDENTIFIER (entrustMAC),
		algoParams		SEQUENCE {
			salt		OCTET STRING,
			pwHashAlgo	AlgorithmIdentifier (SHA-1)
			iterations	INTEGER,
			macAlgo		AlgorithmIdentifier (HMAC-SHA1)
			} OPTIONAL
		} */

static int readMacInfo( STREAM *stream, CMP_PROTOCOL_INFO *protocolInfo,
						const void *password, const int passwordLength,
						void *errorInfo )
	{
	MESSAGE_CREATEOBJECT_INFO createInfo;
	BYTE salt[ CRYPT_MAX_HASHSIZE + 8 ];
	long value;
	int saltLength, iterations, status;

	/* Read the various parameter fields */
	readSequence( stream, NULL );
	status = readFixedOID( stream, OID_ENTRUST_MAC );
	if( cryptStatusError( status ) )
		{
		/* If we don't find this OID we specifically report it as an unknown
		   algorithm problem rather than a generic bad data error */
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADALG;
		retExt( status, 
				( status, SESSION_ERRINFO_VOID, 
				  "Unrecognised MAC algorithm" ) );
		}
	if( peekTag( stream ) == BER_NULL )
		/* No parameters, use the same values as for the previous
		   transaction */
		return( CRYPT_OK );
	readSequence( stream, NULL );
	readOctetString( stream, salt, &saltLength, 4, CRYPT_MAX_HASHSIZE );
	readUniversal( stream );			/* pwHashAlgo */
	readShortInteger( stream, &value );
	status = readUniversal( stream );	/* macAlgo */
	if( cryptStatusError( status ) )
		retExt( status, 
				( status, SESSION_ERRINFO_VOID, 
				  "Invalid MAC algorithm information" ) );
	iterations = ( int ) value;
	if( iterations < 1 || iterations > CMP_MAX_PASSWORD_ITERATIONS )
		{
		/* Prevent DoS attacks due to excessive iteration counts (bad
		   algorithm is about the most appropriate error we can return
		   here).  The spec never defines any appropriate limits for this
		   value, which leads to interesting effects when submitting a
		   request for bignum iterations to some implementations */
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADALG;
		retExt( CRYPT_ERROR_BADDATA,
				( CRYPT_ERROR_BADDATA, SESSION_ERRINFO_VOID, 
				  "Invalid MAC iteration count %d", iterations ) );
		}

	/* If we're the responder and the MAC parameters aren't set yet, set
	   them based on the initiator's values.  If we're using MAC protection
	   and the parameters match our original MAC, reuse the MAC context.
	   As usual the spec is ambiguous over the use of the MAC info, leaving
	   it possible for implementations to re-key the MAC on a per-message
	   basis.  We try and cache MAC info as much as possible to reduce the
	   performance hit from re-keying for each message */
	if( protocolInfo->saltSize <= 0 )
		{
		status = initMacInfo( protocolInfo->iMacContext, password,
							  passwordLength, salt, saltLength, iterations );
		memcpy( protocolInfo->salt, salt, saltLength );
		protocolInfo->saltSize = saltLength;
		protocolInfo->iterations = iterations;
		if( cryptStatusError( status ) )
			retExt( status,
					( status, SESSION_ERRINFO_VOID, 
					  "Couldn't initialise MAC information" ) );
		return( CRYPT_OK );
		}
	if( protocolInfo->iterations && \
		saltLength == protocolInfo->saltSize && \
		!memcmp( salt, protocolInfo->salt, saltLength ) && \
		iterations == protocolInfo->iterations )
		{
		protocolInfo->useAltMAC = FALSE;
		return( CRYPT_OK );
		}
	protocolInfo->useAltMAC = TRUE;	/* Use the alternative MAC context */

	/* If we've got an alternative MAC context using the parameters from a
	   previous message already set up, reuse this */
	if( protocolInfo->iAltMacContext != CRYPT_ERROR && \
		saltLength == protocolInfo->altSaltSize && \
		!memcmp( salt, protocolInfo->altSalt, saltLength ) && \
		iterations == protocolInfo->altIterations )
		return( CRYPT_OK );

	/* This is a new set of parameters, create a new altMAC context with
	   them */
	setMessageCreateObjectInfo( &createInfo, CRYPT_ALGO_HMAC_SHA );
	status = krnlSendMessage( SYSTEM_OBJECT_HANDLE, IMESSAGE_DEV_CREATEOBJECT,
							  &createInfo, OBJECT_TYPE_CONTEXT );
	if( cryptStatusError( status ) )
		return( status );
	status = initMacInfo( createInfo.cryptHandle, password, passwordLength,
						  salt, saltLength, iterations );
	if( cryptStatusError( status ) )
		{
		krnlSendNotifier( createInfo.cryptHandle, IMESSAGE_DECREFCOUNT );
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Couldn't initialise alternative MAC information" ) );
		}
	if( protocolInfo->iAltMacContext != CRYPT_ERROR )
		krnlSendNotifier( protocolInfo->iAltMacContext,
						  IMESSAGE_DECREFCOUNT );
	protocolInfo->iAltMacContext = createInfo.cryptHandle;
	memcpy( protocolInfo->altSalt, salt, saltLength );
	protocolInfo->altSaltSize = saltLength;
	protocolInfo->altIterations = iterations;

	return( CRYPT_OK );
	}

/* Read a cert encrypted with CMP's garbled reinvention of CMS content:

	EncryptedCert ::= SEQUENCE {
		dummy			[0]	... OPTIONAL,		-- Ignored
		cekAlg			[1]	AlgorithmIdentifier,-- CEK algorithm
		encCEK			[2]	BIT STRING,			-- Encrypted CEK
		dummy			[3]	... OPTIONAL,		-- Ignored
		dummy			[4] ... OPTIONAL,		-- Ignored
		encData			BIT STRING				-- Encrypted cert
		} */

static int readEncryptedCert( STREAM *stream,
							  const CRYPT_CONTEXT iImportContext,
							  void *errorInfo )
	{
	CRYPT_CONTEXT iSessionKey;
	MECHANISM_WRAP_INFO mechanismInfo;
	QUERY_INFO queryInfo;
	BYTE *encKeyPtr;
	int encKeyLength, encCertLength, status;

	/* Read the CEK algorithm identifier and encrypted CEK.  All of the
	   values are optional although there's no indication of why or what
	   you're supposed to do if they're not present (OTOH for others there's
	   no indication of what you're supposed to do when they're present
	   either) so we treat an absent required value as an error and ignore
	   the others */
	readSequence( stream, NULL );
	if( peekTag( stream ) == MAKE_CTAG( CTAG_EV_DUMMY1 ) )
		readUniversal( stream );				/* Junk */
	status = readContextAlgoID( stream, &iSessionKey, &queryInfo,
								CTAG_EV_CEKALGO );
	if( cryptStatusError( status ) )			/* CEK algo */
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Invalid encrypted certificate CEK algorithm" ) );
	status = readBitStringHole( stream, &encKeyLength, 56, 
								CTAG_EV_ENCCEK );
	if( cryptStatusOK( status ) &&				/* Encrypted CEK */
		( encKeyLength < 56 || encKeyLength > CRYPT_MAX_PKCSIZE ) )
		status = CRYPT_ERROR_OVERFLOW;
	if( cryptStatusOK( status ) )
		{
		encKeyPtr = sMemBufPtr( stream );
		sSkip( stream, encKeyLength );
		if( peekTag( stream ) == MAKE_CTAG( CTAG_EV_DUMMY2 ) )
			readUniversal( stream );			/* Junk */
		if( peekTag( stream ) == MAKE_CTAG( CTAG_EV_DUMMY3 ) )
			readUniversal( stream );			/* Junk */
		status = readBitStringHole( stream, &encCertLength, 128, 
									DEFAULT_TAG );
		}
	if( cryptStatusOK( status ) &&				/* Encrypted cert */
		( encCertLength < 128 || encCertLength > 8192 ) )
		status = CRYPT_ERROR_BADDATA;
	if( cryptStatusOK( status ) && \
		encCertLength > sMemDataLeft( stream ) )
		return( CRYPT_ERROR_UNDERFLOW );
	if( cryptStatusOK( status ) && \
		( queryInfo.cryptMode == CRYPT_MODE_ECB || \
		  queryInfo.cryptMode == CRYPT_MODE_CBC ) )
		{
		int blockSize;

		/* Make sure that the data length is valid.  Checking at this point
		   saves a lot of unnecessary processing and allows us to return a
		   more meaningful error code */
		krnlSendMessage( iSessionKey, IMESSAGE_GETATTRIBUTE, &blockSize,
						 CRYPT_CTXINFO_BLOCKSIZE );
		if( queryInfo.size % blockSize )
			status = CRYPT_ERROR_BADDATA;
		}
	if( cryptStatusError( status ) )
		{
		krnlSendNotifier( iSessionKey, IMESSAGE_DECREFCOUNT );
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Invalid encrypted certificate CEK data" ) );
		}

	/* Copy the encrypted key to the buffer and import it into the session
	   key context */
	setMechanismWrapInfo( &mechanismInfo, encKeyPtr, encKeyLength,
						  NULL, 0, iSessionKey, iImportContext );
	status = krnlSendMessage( SYSTEM_OBJECT_HANDLE, IMESSAGE_DEV_IMPORT,
							  &mechanismInfo, MECHANISM_ENC_PKCS1 );
	clearMechanismInfo( &mechanismInfo );
	if( cryptStatusError( status ) )
		{
		krnlSendNotifier( iSessionKey, IMESSAGE_DECREFCOUNT );
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Couldn't decrypt encrypted certificate CEK" ) );
		}

	/* Decrypt the returned cert */
	status = krnlSendMessage( iSessionKey, IMESSAGE_CTX_DECRYPT,
							  sMemBufPtr( stream ), encCertLength );
	krnlSendNotifier( iSessionKey, IMESSAGE_DECREFCOUNT );
	if( cryptStatusError( status ) )
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Couldn't decrypt returned encrypted certificate using "
				  "CEK" ) );
	return( CRYPT_OK );
	}

/* Read the kitchen-sink field in the PKI header */

static int readGeneralInfo( STREAM *stream, CMP_PROTOCOL_INFO *protocolInfo )
	{
	int generalInfoEndPos = stell( stream ), length;
	int iterationCount = 0, status;

	/* Go through the various attributes looking for anything that we can
	   use */
	readConstructed( stream, NULL, CTAG_PH_GENERALINFO );
	status = readSequence( stream, &length );
	generalInfoEndPos += length;
	while( cryptStatusOK( status ) && \
		   stell( stream ) < generalInfoEndPos && \
		   iterationCount++ < FAILSAFE_ITERATIONS_MED )
		{
		BYTE oid[ MAX_OID_SIZE + 8 ];

		/* Read the attribute.  Since there are only two attribute types
		   that we use, we hardcode the read in here rather than performing
		   a general-purpose attribute read */
		readSequence( stream, NULL );
		status = readEncodedOID( stream, oid, &length, MAX_OID_SIZE,
								 BER_OBJECT_IDENTIFIER );
		if( cryptStatusError( status ) )
			break;

		/* Process the cryptlib presence-check value */
		if( length == sizeofOID( OID_CRYPTLIB_PRESENCECHECK ) && \
			!memcmp( oid, OID_CRYPTLIB_PRESENCECHECK, length ) )
			{
			/* The other side is running cryptlib, we can make some common-
			   sense assumptions about its behaviour */
			protocolInfo->isCryptlib = TRUE;
			status = readSet( stream, NULL );/* Attribute */
			continue;
			}

		/* Check for the ESSCertID, which fixes CMP's broken cert
		   identification mechanism */
		if( length == sizeofOID( OID_ESS_CERTID ) && \
			!memcmp( oid, OID_ESS_CERTID, length ) )
			{
			int endPos;

			/* Extract the cert hash from the ESSCertID */
			readSet( stream, NULL );		/* Attribute */
			readSequence( stream, NULL );	/* SigningCerts */
			readSequence( stream, NULL );	/* Certs */
			readSequence( stream, &length );/* ESSCertID */
			endPos = stell( stream ) + length;
			status = readOctetString( stream, protocolInfo->certID,
									  &protocolInfo->certIDsize,
									  8, CRYPT_MAX_HASHSIZE );
			if( cryptStatusOK( status ) && \
				protocolInfo->certIDsize != KEYID_SIZE )
				status = CRYPT_ERROR_BADDATA;
			if( cryptStatusError( status ) )
				continue;
			protocolInfo->certIDchanged = TRUE;
			if( stell( stream ) < endPos )
				/* Skip the issuerSerial if there's one present.  We can't
				   really do much with it in this form without rewriting it
				   into the standard issuerAndSerialNumber form, but in any
				   case we don't need it because we've already got the cert
				   ID */
				status = readUniversal( stream );
			continue;
			}

		/* It's something that we don't recognise, skip it */
		status = readUniversal( stream );
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MED )
		retIntError();

	return( status );
	}
#endif /* USE_CMP */

/****************************************************************************
*																			*
*								Read Status Info							*
*																			*
****************************************************************************/

/* The following code is shared between CMP and TSP due to TSP's use of
   random elements cut & pasted from CMP without any real understanding of
   their function or semantics */

#if defined( USE_CMP ) || defined( USE_TSP )

/* Map a PKI failure info value to an error string */

static const char *getFailureString( const int value )
	{
	static const char FAR_BSS *failureStrings[] = {
		"Unrecognized or unsupported Algorithm Identifier",
		"The integrity check failed (e.g. signature did not verify)",
		"This transaction is not permitted or supported",
		"The messageTime was not sufficiently close to the system time as "
			"defined by local policy",
		"No certificate could be found matching the provided criteria",
		"The data submitted has the wrong format",
		"The authority indicated in the request is different from the one "
			"creating the response token",
		"The requester's data is incorrect (used for notary services)",
		"Timestamp is missing but should be there (by policy)",
		"The proof-of-possession failed",
		"The certificate has already been revoked",
		"The certificate has already been confirmed",
		"Invalid integrity, password based instead of signature or vice "
			"versa",
		"Invalid recipient nonce, either missing or wrong value",
		"The TSA's time source is not available",
		"The requested TSA policy is not supported by the TSA",
		"The requested extension is not supported by the TSA",
		"The additional information requested could not be understood or is "
			"not available",
		"Invalid sender nonce, either missing or wrong size",
		"Invalid certificate template or missing mandatory information",
		"Signer of the message unknown or not trusted",
		"The transaction identifier is already in use",
		"The version of the message is not supported",
		"The sender was not authorized to make the preceding request or "
			"perform the preceding action",
		"The request cannot be handled due to system unavailability",
		"The request cannot be handled due to system failure",
		"Certificate cannot be issued because a duplicate certificate "
			"already exists",
		"Unknown PKI failure code", "Unknown PKI failure code"
		};
	int bitIndex = 0, bitFlags = value, iterationCount = 0;

	/* Find the first failure string corresponding to a bit set in the
	   failure info */
	if( bitFlags == 0 )
		return( "Missing PKI failure code" );
	while( !( bitFlags & 1 ) && iterationCount++ < FAILSAFE_ITERATIONS_MED )
		{
		bitIndex++;
		bitFlags >>= 1;
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MED )
		retIntError_Ext( "Internal error" );
	if( bitIndex >= sizeof( failureStrings ) / sizeof( char * ) )
		return( "Unknown PKI failure code" );

	return( ( char * ) failureStrings[ bitIndex ] );
	}

/* Read PKIStatus information:

	PKIStatusInfo ::= SEQUENCE {
		status			INTEGER,
		statusString	SEQUENCE OF UTF8String OPTIONAL,
		failInfo		BIT STRING OPTIONAL
		} 

  Note that readPkiStatusInfo() is declared global, since it's used by the 
  TSP code due to TSP's use of random elements cut & pasted from CMP */

static int readFreeText( STREAM *stream, char *string, const int stringMaxLen )
	{
	int endPos, stringLength, status;

	/* Read the status string(s).  There can be more than one of these,
	   there's no indication of what the subsequent ones are used for and
	   not much we can do with them in any case, so we skip them */
	readSequence( stream, &endPos );
	endPos += stell( stream );
	status = readCharacterString( stream, string, &stringLength, stringMaxLen,
								  BER_STRING_UTF8 );
	if( cryptStatusError( status ) )
		{
		strlcpy_s( string, stringMaxLen, "Invalid PKI free text" );
		return( status );
		}
	string[ stringLength ] = '\0';
	return( ( stell( stream ) < endPos ) ? \
			sSkip( stream, endPos - stell( stream ) ) : CRYPT_OK );
	}

int readPkiStatusInfo( STREAM *stream, ERROR_INFO *errorInfo )
	{
	long value, endPos;
	int length, status;

	/* Clear the return values */
	memset( errorInfo, 0, sizeof( ERROR_INFO ) );

	/* Read the outer wrapper and status value */
	readSequence( stream, &length );
	endPos = stell( stream ) + length;
	status = readShortInteger( stream, &value );
	if( cryptStatusError( status ) )
		{
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
				   "Invalid PKI status value" );
		return( status );
		}
	errorInfo->errorCode = ( int ) value;
	if( stell( stream ) < endPos && peekTag( stream ) == BER_SEQUENCE )
		{
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
				   "Server returned error: " );

		status = readFreeText( stream, errorInfo->errorString + 23,
							   MAX_ERRMSG_SIZE - ( 32 + 1 ) );
		if( cryptStatusError( status ) )
			return( status );
		}
	if( stell( stream ) < endPos )
		{
		char textBitString[ 128 + 8 ], *textBitStringPtr = textBitString;
		int bitString, textBitStringLen, errorMsgLen;
		int i, noBits, bitMask, bitNo = -1;

		/* Read the failure info and slot it into the error string */
		status = readBitString( stream, &bitString );
		if( cryptStatusError( status ) )
			{
			strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
					   "Invalid PKI failure info" );
			return( status );
			}
		strlcpy_s( textBitString, 128, "Server returned status value " );
		textBitStringPtr = textBitString + strlen( textBitString );
		i = bitString;
		for( noBits = 0; i > 0 && noBits < 32; noBits++ )
			i >>= 1;
		bitMask = 1 << ( noBits - 1 );
		for( i = 0; i < noBits; i++ )
			{
			if( bitString & bitMask )
				{
				/* If there's no bit set yet, set it.  If there's already
				   a bit set, set it to a no-value that indicates that more
				   than one bit is set */
				bitNo = ( bitNo == -1 ) ? ( noBits - 1 ) - i : -2;
				*textBitStringPtr++ = '1';
				}
			else
				*textBitStringPtr++ = '0';
			bitMask >>= 1;
			}
		if( bitNo >= 0 )
			sprintf_s( textBitString, 64,
					   "Server returned status bit %d: ", bitNo );
		else
			strlcpy_s( textBitStringPtr, 64, "'B: " );
		textBitStringLen = strlen( textBitString );
		if( ( errorMsgLen = strlen( errorInfo->errorString ) ) > 0 )
			{
			/* There's error message text present, move it up to make room
			   for the bit string text */
			memmove( errorInfo->errorString + textBitStringLen, 
					 errorInfo->errorString,
					 min( errorMsgLen + 1, \
						  MAX_ERRMSG_SIZE - ( textBitStringLen + 1 ) ) );
			memcpy( errorInfo->errorString, textBitString, textBitStringLen );
			}
		else
			{
			/* If there's a failure code present, turn it into an error
			   string */
			if( bitString )
				{
				strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, textBitString );
				strlcat_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
						   getFailureString( bitString ) );
				}
			}
		errorInfo->errorString[ MAX_ERRMSG_SIZE - 1 ] = '\0';

		/* If we can return something more useful than the generic "failed"
		   error code, try and do so */
		if( bitString & CMPFAILINFO_BADALG )
			return( CRYPT_ERROR_NOTAVAIL );
		if( ( bitString & CMPFAILINFO_BADMESSAGECHECK ) || \
			( bitString & CMPFAILINFO_BADPOP ) || \
			( bitString & CMPFAILINFO_WRONGINTEGRITY ) )
			return( CRYPT_ERROR_WRONGKEY );
		if( ( bitString & CMPFAILINFO_BADREQUEST ) || \
			( bitString & CMPFAILINFO_SIGNERNOTTRUSTED ) || \
			( bitString & CMPFAILINFO_NOTAUTHORIZED ) )
			return( CRYPT_ERROR_PERMISSION );
		if( bitString & CMPFAILINFO_BADDATAFORMAT )
			return( CRYPT_ERROR_BADDATA );
		if( ( bitString & CMPFAILINFO_UNACCEPTEDPOLICY ) || \
			( bitString & CMPFAILINFO_UNACCEPTEDEXTENSION ) || \
			( bitString & CMPFAILINFO_BADCERTTEMPLATE ) )
			return( CRYPT_ERROR_INVALID );
		if( ( bitString & CMPFAILINFO_TRANSACTIONIDINUSE ) || \
			( bitString & CMPFAILINFO_DUPLICATECERTREQ ) )
			return( CRYPT_ERROR_DUPLICATE );
		}
	else
		/* If there was a problem but there's no extra error information
		   present, return a "This page deliberately left blank" error */
		if( errorInfo->errorCode != PKISTATUS_OK )
			strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
					   "Server returned nonspecific error information" );

	/* A PKI status code is a bit difficult to turn into anything useful,
	   the best we can do is to report that the operation failed and let
	   the user get the exact details from the PKI status info */
	return( ( errorInfo->errorCode == PKISTATUS_OK || \
			  errorInfo->errorCode == PKISTATUS_OK_WITHINFO ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}
#endif /* USE_CMP || USE_TSP */

#ifdef USE_CMP

/****************************************************************************
*																			*
*								PKI Body Functions							*
*																			*
****************************************************************************/

/* Read request body */

static int readRequestBody( STREAM *stream, SESSION_INFO *sessionInfoPtr,
							CMP_PROTOCOL_INFO *protocolInfo,
							const int messageType )
	{
	CMP_INFO *cmpInfo = sessionInfoPtr->sessionCMP;
	MESSAGE_DATA msgData;
	BYTE authCertID[ CRYPT_MAX_HASHSIZE + 8 ];
	int value, length, status;

	/* Import the CRMF request */
	status = readSequence( stream, &length );
	if( cryptStatusOK( status ) )
		status = importCertFromStream( stream,
								&sessionInfoPtr->iCertRequest,
								( messageType == CTAG_PB_P10CR ) ? \
									CRYPT_CERTTYPE_CERTREQUEST : \
								( messageType == CTAG_PB_RR ) ? \
									CRYPT_CERTTYPE_REQUEST_REVOCATION : \
									CRYPT_CERTTYPE_REQUEST_CERT,
								length );
	if( cryptStatusError( status ) )
		{
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADCERTTEMPLATE;
		retExt( status, 
				( status, SESSION_ERRINFO, "Invalid CRMF request" ) );
		}

	/* If the request is from an encryption-only key, remember this so that
	   we can peform special-case processing later on */
	status = krnlSendMessage( sessionInfoPtr->iCertRequest,
							  IMESSAGE_GETATTRIBUTE, &value,
							  CRYPT_CERTINFO_SELFSIGNED );
	if( cryptStatusOK( status ) && !value )
		{
		/* If the request indicates that it's a signing key then it has to
		   be signed */
		status = krnlSendMessage( sessionInfoPtr->iCertRequest,
								  IMESSAGE_GETATTRIBUTE, &value,
								  CRYPT_CERTINFO_KEYUSAGE );
		if( cryptStatusOK( status ) && \
			( value & ( CRYPT_KEYUSAGE_DIGITALSIGNATURE | \
						CRYPT_KEYUSAGE_NONREPUDIATION ) ) )
			{
			protocolInfo->pkiFailInfo = CMPFAILINFO_BADCERTTEMPLATE;
			retExt( CRYPT_ERROR_INVALID,
					( CRYPT_ERROR_INVALID, SESSION_ERRINFO, 
					  "CRMF request is for a signing key but the request "
					  "isn't signed" ) );
			}
		protocolInfo->cryptOnlyKey = TRUE;
		}

	/* Record the identity of the PKI user (for a MAC'd request) or cert (for
	   a signed request) that authorised this request */
	setMessageData( &msgData, authCertID, CRYPT_MAX_HASHSIZE );
	status = krnlSendMessage( protocolInfo->useMACreceive ? \
								cmpInfo->userInfo : \
								sessionInfoPtr->iAuthInContext,
							  IMESSAGE_GETATTRIBUTE_S, &msgData,
							  CRYPT_CERTINFO_FINGERPRINT_SHA );
	if( cryptStatusOK( status ) )
		status = krnlSendMessage( sessionInfoPtr->iCertRequest,
								  IMESSAGE_SETATTRIBUTE_S, &msgData,
								  CRYPT_IATTRIBUTE_AUTHCERTID );
	if( cryptStatusError( status ) || messageType != CTAG_PB_IR )
		return( status );

	/* If it's an ir, the subject may not know their DN or may only know
	   their CN, in which case they'll send an empty/CN-only subject DN in
	   the hope that we can fill it in for them.  In addition there may be
	   other constraints that the CA wants to apply, these are handled by
	   applying the PKI user info to the request */
	status = krnlSendMessage( sessionInfoPtr->iCertRequest,
							  IMESSAGE_SETATTRIBUTE, &cmpInfo->userInfo,
							  CRYPT_IATTRIBUTE_PKIUSERINFO );
	if( cryptStatusError( status ) )
		{
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADCERTTEMPLATE;
		retExt( CRYPT_ERROR_INVALID,
				( CRYPT_ERROR_INVALID, SESSION_ERRINFO, 
				  "User information in request can't be reconciled with "
				  "our information for the user" ) );
		}
	return( CRYPT_OK );
	}

/* Read response body */

static int readResponseBody( STREAM *stream, SESSION_INFO *sessionInfoPtr,
							 CMP_PROTOCOL_INFO *protocolInfo )
	{
	MESSAGE_CREATEOBJECT_INFO createInfo;
	void *bodyInfoPtr;
	int bodyLength, tag, status;

	/* If it's a revocation response, the only returned data is the status
	   value */
	if( protocolInfo->operation == CTAG_PB_RR )
		{
		readSequence( stream, NULL );		/* Outer wrapper */
		readSequence( stream, NULL );		/* Inner wrapper */
		return( readPkiStatusInfo( stream, &sessionInfoPtr->errorInfo ) );
		}

	/* It's a cert response, unwrap the body to find the certificate
	   payload */
	readSequence( stream, NULL );			/* Outer wrapper */
	if( peekTag( stream ) == MAKE_CTAG( 1 ) )
		readUniversal( stream );			/* caPubs */
	readSequence( stream, NULL );
	readSequence( stream, NULL );			/* Inner wrapper */
	readUniversal( stream );				/* certReqId */
	status = readPkiStatusInfo( stream, &sessionInfoPtr->errorInfo );
	if( cryptStatusOK( status ) )
		{
		readSequence( stream, NULL );		/* certKeyPair wrapper */
		tag = EXTRACT_CTAG( peekTag( stream ) );
		status = readConstructed( stream, &bodyLength, tag );
		if( cryptStatusOK( status ) && bodyLength > sMemDataLeft( stream ) )
			status = CRYPT_ERROR_UNDERFLOW;
		}
	if( cryptStatusError( status ) )
		return( status );

	/* Process the returned cert as required */
	bodyInfoPtr = sMemBufPtr( stream );
	switch( tag )
		{
		case  CTAG_CK_CERT:
			/* Plaintext cert, we're done */
			break;

		case CTAG_CK_ENCRYPTEDCERT:
			/* Cert encrypted with CMP's garbled attempt at doing CMS, try
			   and decrypt it */
			status = readEncryptedCert( stream, sessionInfoPtr->privateKey,
										sessionInfoPtr );
			break;

		case CTAG_CK_NEWENCRYPTEDCERT:
			/* Cert encrypted with CMS, unwrap it */
			status = envelopeUnwrap( bodyInfoPtr, bodyLength,
									 bodyInfoPtr, &bodyLength,
									 bodyLength,
									 sessionInfoPtr->privateKey );
			if( cryptStatusError( status ) )
				retExt( cryptArgError( status ) ? \
						CRYPT_ERROR_FAILED : status,
						( cryptArgError( status ) ? \
						  CRYPT_ERROR_FAILED : status, SESSION_ERRINFO,
						  "Couldn't decrypt CMS enveloped certificate" ) );
			break;

		default:
			retExt( status,
					( status, SESSION_ERRINFO, 
					  "Unknown returned certificate encapsulation type %d",
					  tag ) );
		}
	if( cryptStatusError( status ) )
		return( status );

	/* Import the cert as a cryptlib object */
	setMessageCreateObjectIndirectInfo( &createInfo, bodyInfoPtr, bodyLength,
										CRYPT_CERTTYPE_CERTIFICATE );
	status = krnlSendMessage( SYSTEM_OBJECT_HANDLE,
							  IMESSAGE_DEV_CREATEOBJECT_INDIRECT, &createInfo,
							  OBJECT_TYPE_CERTIFICATE );
	if( cryptStatusError( status ) )
		retExt( status,
				( status, SESSION_ERRINFO, 
				  "Invalid returned certificate" ) );
	sessionInfoPtr->iCertResponse = createInfo.cryptHandle;

	/* In order to acknowledge receipt of this message we have to return at a
	   later point a hash of the cert carried in this message created using
	   the hash algorithm used in the cert signature.  This makes the CMP-
	   level transport layer dependant on the certificate format it's
	   carrying (so the code will repeatedly break every time a new cert
	   format is added), but that's what the standard requires */
	status = krnlSendMessage( sessionInfoPtr->iCertResponse,
							  IMESSAGE_GETATTRIBUTE,
							  &protocolInfo->confHashAlgo,
							  CRYPT_IATTRIBUTE_CERTHASHALGO );
	if( cryptStatusError( status ) )
		retExt( status,
				( status, SESSION_ERRINFO, 
				  "Couldn't extract confirmation hash type from "
				  "certificate" ) );
	if( protocolInfo->confHashAlgo != CRYPT_ALGO_MD5 && \
		protocolInfo->confHashAlgo != CRYPT_ALGO_SHA )
		{
		/* Certs can only provide MD5 and SHA-1 fingerprints */
		retExt( CRYPT_ERROR_NOTAVAIL,
				( CRYPT_ERROR_NOTAVAIL, SESSION_ERRINFO, 
				  "Can't confirm certificate issue using algorithm %d",
				  protocolInfo->confHashAlgo ) );
		}

	return( CRYPT_OK );
	}

/* Read conf body */

static int readConfBody( STREAM *stream, SESSION_INFO *sessionInfoPtr,
						 CMP_PROTOCOL_INFO *protocolInfo )
	{
	MESSAGE_DATA msgData;
	BYTE certHash[ CRYPT_MAX_HASHSIZE + 8 ];
	int length, status;

	/* Read the client's returned confirmation information */
	status = readSequence( stream, &length );
	if( cryptStatusOK( status ) && length <= 0 )
		{
		/* Missing certStatus, the client has rejected the cert.  This isn't
		   an explicit error since it's a valid protocol outcome, so we
		   return an OK status but set the overall protocol status to a
		   generic error value to indicate that we don't want to continue
		   normally */
		protocolInfo->status = CRYPT_ERROR;
		return( CRYPT_OK );
		}
	readSequence( stream, NULL );
	status = readOctetString( stream, certHash, &length,
							  8, CRYPT_MAX_HASHSIZE );
	if( cryptStatusError( status ) )
		retExt( status, 
				( status, SESSION_ERRINFO, "Invalid cert confirmation" ) );

	/* Get the local cert hash and compare it to the client's one.  Since
	   we're the server, this is a cryptlib-issued cert so we know that
	   it'll always use SHA-1 */
	setMessageData( &msgData, certHash, length );
	status = krnlSendMessage( sessionInfoPtr->iCertResponse,
							  IMESSAGE_COMPARE, &msgData,
							  MESSAGE_COMPARE_FINGERPRINT );
	if( cryptStatusError( status ) )
		{
		/* The user is confirming an unknown cert, the best that we can do
		   is return a generic cert-mismatch error */
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADCERTID;
		retExt( CRYPT_ERROR_NOTFOUND,
				( CRYPT_ERROR_NOTFOUND, SESSION_ERRINFO, 
				  "Returned cert hash doesn't match issued certificate" ) );
		}
	return( CRYPT_OK );
	}

/* Read genMsg body */

static int readGenMsgBody( STREAM *stream, SESSION_INFO *sessionInfoPtr,
						   const BOOLEAN isRequest )
	{
	int bodyLength, status;

	status = readSequence( stream, &bodyLength );
	if( cryptStatusError( status ) )
		retExt( CRYPT_ERROR_BADDATA,
				( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
				  "Invalid genMsg header" ) );
	if( isRequest )
		{
		/* It's a request GenMsg, check for a PKIBoot request */
		if( bodyLength < sizeofObject( sizeofOID( OID_PKIBOOT ) ) || \
			bodyLength > sMemDataLeft( stream ) )
			retExt( CRYPT_ERROR_BADDATA,
					( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
					  "Invalid genMsg length %d", bodyLength ) );
		readSequence( stream, NULL );
		status = readFixedOID( stream, OID_PKIBOOT );
		if( cryptStatusError( status ) )
			retExt( CRYPT_ERROR_NOTAVAIL,
					( CRYPT_ERROR_NOTAVAIL, SESSION_ERRINFO, 
					  "Invalid genMsg type, expected PKIBoot request" ) );
		return( CRYPT_OK );
		}

	/* It's a PKIBoot response with the InfoTypeAndValue handled as CMS
	   content (see the comment for writeGenMsgBody()), import the cert
	   trust list.  Since this isn't a true cert chain and isn't used as
	   such, we use data-only certs (specified using the special-case
	   CRYPT_ICERTTYPE_CTL type specifier) */
	status = importCertFromStream( stream, &sessionInfoPtr->iCertResponse,
								   CRYPT_ICERTTYPE_CTL, bodyLength );
	if( cryptStatusError( status ) )
		retExt( status, 
				( status, SESSION_ERRINFO, "Invalid PKIBoot response" ) );
	return( CRYPT_OK );
	}

/* Read error body */

static int readErrorBody( STREAM *stream, SESSION_INFO *sessionInfoPtr )
	{
	ERROR_INFO *errorInfo = &sessionInfoPtr->errorInfo;
	int endPos, length, status;

	/* Read the outer wrapper and PKI status info.  An error return status
	   is valid when we read the status info since we're reading an error
	   status and converting it into a cryptlib status, so we don't exit
	   unless it's a problem with the status info itself */
	readConstructed( stream, NULL, CTAG_PB_ERROR );
	readSequence( stream, &length );	/* Outer wrapper */
	endPos = stell( stream ) + length;
	status = readPkiStatusInfo( stream, &sessionInfoPtr->errorInfo );
	if( status == CRYPT_ERROR_BADDATA || status == CRYPT_ERROR_UNDERFLOW )
		return( status );

	/* In addition to the PKI status info there can be another layer of
	   error information wrapped around it which is exactly the same only
	   different, so if we haven't got anything from the status info we
	   check to see whether this layer can give us anything */
	if( stell( stream ) < endPos && peekTag( stream ) == BER_INTEGER )
		{
		/* If there's an error code present and we haven't already set the
		   error code from the pkiStatusInfo, set it now */
		if( !errorInfo->errorCode )
			{
			long value;

			status = readShortInteger( stream, &value );
			if( cryptStatusOK( status ) )
				errorInfo->errorCode = ( int ) value;
			}
		else
			readUniversal( stream );
		}
	if( stell( stream ) < endPos && peekTag( stream ) == BER_SEQUENCE && \
		!*errorInfo->errorString )
		/* Read the error text, ignoring any possible error status since the
		   overall error code from the status info is more meaningful than
		   a data format problem in trying to read the error text */
		readFreeText( stream, errorInfo->errorString,
					  MAX_ERRMSG_SIZE - 1 );

	return( status );
	}

/****************************************************************************
*																			*
*								Read a PKI Header							*
*																			*
****************************************************************************/

/* Read a PKI header and make sure that it matches the header that we sent
   (for EE or non-initial CA/RA messages) or set up the EE information in
   response to an initial message (for an initial CA/RA message).  We ignore
   all the redundant fields in the header that don't directly affect the
   protocol, based on the results of CMP interop testing this appears to be
   standard practice among implementors.  This also helps get around problems
   with implementations that get the fields wrong, since most of the fields
   aren't generally useful it doesn't affect the processing while making the
   code more tolerant of implementation errors:

	header				SEQUENCE {
		version			INTEGER (2),
		dummy		[4]	EXPLICIT DirectoryName,		-- Ignored
		senderDN	[4]	EXPLICIT DirectoryName,		-- Copied if non-clib
		protAlgo	[1]	EXPLICIT AlgorithmIdentifier,
		protKeyID	[2] EXPLICIT OCTET STRING,		-- Copied if changed
		dummy		[3] EXPLICIT OCTET STRING,		-- Ignored
		transID		[4] EXPLICIT OCTET STRING,
		nonce		[5] EXPLICIT OCTET STRING,		-- Copied if non-clib
		dummy		[6] EXPLICIT OCTET STRING,		-- Ignored
		dummy		[7] SEQUENCE OF UTF8String,		-- Ignored
		generalInfo	[8] EXPLICIT SEQUENCE OF Info OPT	-- cryptlib-specific info
		} */

static int readPkiHeader( STREAM *stream, CMP_PROTOCOL_INFO *protocolInfo,
						  void *errorInfo,
						  const BOOLEAN isServerInitialMessage )
	{
	CRYPT_ALGO_TYPE cryptAlgo, hashAlgo;
	BYTE buffer[ CRYPT_MAX_HASHSIZE + 8 ];
	int length, streamPos, endPos, status;

	/* Clear per-message state information */
	protocolInfo->userIDchanged = protocolInfo->certIDchanged = FALSE;
	protocolInfo->macInfoPos = CRYPT_ERROR;
	protocolInfo->senderDNPtr = NULL;
	protocolInfo->senderDNlength = 0;

	/* Read the wrapper and skip the static info, which matches what we sent
	   and is protected by the MAC so there's little point in looking at
	   it */
	readSequence( stream, &length );
	endPos = stell( stream ) + length;
	readShortInteger( stream, NULL );		/* Version */
	if( !protocolInfo->isCryptlib )
		{
		/* The ID of the key used for integrity protection (or in general
		   the identity of the sender) can be specified either as the sender
		   DN or the senderKID or both, or in some cases even indirectly via
		   the transaction ID.  With no real guidance as to which one to use,
		   implementors are using any of these options to identify the key.
		   Since we need to check that the integrity-protection key we're
		   using is correct so that we can report a more appropriate error
		   than bad signature or bad data, we need to remember the sender DN
		   for later in case this is the only form of key identification
		   provided.  Unfortunately since the sender DN can't uniquely
		   identify a cert, if this is all we get then the caller can still
		   get a bad signature error, yet another one of CMPs many wonderful
		   features */
		status = readConstructed( stream, &protocolInfo->senderDNlength, 4 );
		protocolInfo->senderDNPtr = sMemBufPtr( stream );
		if( cryptStatusOK( status ) && protocolInfo->senderDNlength > 0 )
			readUniversal( stream );		/* Sender DN */
		}
	else
		/* cryptlib includes a proper certID so the whole signer
		   identification mess is avoided and we can ignore the sender DN */
		readUniversal( stream );			/* Sender DN */
	status = readUniversal( stream );		/* Recipient */
	if( peekTag( stream ) == MAKE_CTAG( CTAG_PH_MESSAGETIME ) )
		status = readUniversal( stream );	/* Message time */
	if( cryptStatusError( status ) )
		retExt( CRYPT_ERROR_BADDATA, 
				( CRYPT_ERROR_BADDATA, SESSION_ERRINFO_VOID, 
				  "Invalid PKI header" ) );
	if( peekTag( stream ) != MAKE_CTAG( CTAG_PH_PROTECTIONALGO ) )
		/* The message was sent without integrity protection, report it as
		   a signature error rather than the generic bad data error that
		   we'd get from the following read */
		retExt( CRYPT_ERROR_SIGNATURE,
				( CRYPT_ERROR_SIGNATURE, SESSION_ERRINFO_VOID, 
				  "Message was sent without integrity protection" ) );
	status = readConstructed( stream, NULL, CTAG_PH_PROTECTIONALGO );
	if( cryptStatusError( status ) )
		/* If there was a problem we should exit now since an error status
		   from the following readAlgoIDex() is interpreted to indicate the
		   presence of the weird Entrust MAC rather than a real error */
		retExt( status,
				( status, SESSION_ERRINFO_VOID, 
				  "Invalid integrity protection info in PKI header" ) );
	streamPos = stell( stream );
	status = readAlgoIDext( stream, &cryptAlgo, &hashAlgo );
	if( cryptStatusOK( status ) )
		{
		/* It's a known signature algorithm, use the CA cert to verify it
		   rather than the MAC */
		protocolInfo->useMACreceive = FALSE;
		protocolInfo->hashAlgo = hashAlgo;
		}
	else
		{
		/* It's nothing normal, it must be the Entrust MAC algorithm info,
		   remember where it starts so that we can process it later */
		sClearError( stream );
		protocolInfo->macInfoPos = streamPos;
		readUniversal( stream );
		protocolInfo->useMACreceive = TRUE;
		}
	if( peekTag( stream ) == MAKE_CTAG( CTAG_PH_SENDERKID ) )
		{								/* Sender protection keyID */
		if( isServerInitialMessage )
			{
			BYTE userID[ CRYPT_MAX_HASHSIZE + 8 ];
			int userIDsize;

			/* Read the PKI user ID that we'll need to handle the integrity
			   protection on the message */
			readConstructed( stream, NULL, CTAG_PH_SENDERKID );
			status = readOctetString( stream, userID, &userIDsize,
									  8, CRYPT_MAX_HASHSIZE );
			if( cryptStatusError( status ) )
				retExt( status,
						( status, SESSION_ERRINFO_VOID, 
						  "Invalid user ID in PKI header" ) );

			/* If there's already been a previous transaction (which means
			   that we have PKI user info present) and the current
			   transaction matches what was used in the previous one, we
			   don't have to update the user info */
			if( protocolInfo->userIDsize <= 0 || \
				protocolInfo->userIDsize != userIDsize || \
				memcmp( protocolInfo->userID, userID, userIDsize ) )
				{
				memcpy( protocolInfo->userID, userID, userIDsize );
				protocolInfo->userIDsize = userIDsize;
				protocolInfo->userIDchanged = TRUE;
				if( protocolInfo->iMacContext != CRYPT_ERROR )
					{
					krnlSendNotifier( protocolInfo->iMacContext,
									  IMESSAGE_DECREFCOUNT );
					protocolInfo->iMacContext = CRYPT_ERROR;
					}
				}
			}
		else
			/* We're in the middle of an ongoing transaction, skip the user
			   ID, which we already know */
			readUniversal( stream );
		}
	else
		{
		/* If we're the server, the client must provide a PKI user ID in the
		   first message unless we got one in an earlier transaction */
		if( isServerInitialMessage && protocolInfo->userIDsize <= 0 )
			retExt( status, 
					( status, SESSION_ERRINFO_VOID, 
					  "Missing user ID in PKI header" ) );
		}
	if( peekTag( stream ) == MAKE_CTAG( CTAG_PH_RECIPKID ) )
		readUniversal( stream );			/* Recipient protection keyID */

	/* Record the transaction ID or make sure that it matches the one that
	   we sent.  There's no real need to do an explicit duplicate check
	   since a replay attempt will be rejected as a duplicate by the cert
	   store and the locking performed at that level makes it a much better
	   place to catch duplicates, but we do it anyway */
	status = readConstructed( stream, NULL, CTAG_PH_TRANSACTIONID );
	if( cryptStatusError( status ) )
		retExt( status, 
				( status, SESSION_ERRINFO_VOID, 
				  "Missing transaction ID in PKI header" ) );
	if( isServerInitialMessage )
		/* This is the first message and we're the server, record the
		   transaction ID for later */
		status = readOctetString( stream, protocolInfo->transID,
								  &protocolInfo->transIDsize,
								  4, CRYPT_MAX_HASHSIZE );
	else
		{
		/* Make sure that the transaction ID for this message matches the
		   recorded value (the bad recipient nonce/bad signature error code
		   is the best that we can provide here) */
		status = readOctetString( stream, buffer, &length,
								  4, CRYPT_MAX_HASHSIZE );
		if( cryptStatusOK( status ) && \
			( protocolInfo->transIDsize < 4 || \
			  protocolInfo->transIDsize != length || \
			  memcmp( protocolInfo->transID, buffer, length ) ) )
			{
			protocolInfo->pkiFailInfo = CMPFAILINFO_BADRECIPIENTNONCE;
			retExt( CRYPT_ERROR_SIGNATURE,
					( CRYPT_ERROR_SIGNATURE, SESSION_ERRINFO_VOID, 
					  "Returned message transaction ID doesn't match our "
					  "transaction ID" ) );
			}
		}
	if( cryptStatusError( status ) )
		retExt( status, 
				( status, SESSION_ERRINFO_VOID, 
				  "Invalid transaction ID in PKI header" ) );

	/* Read the sender nonce, which becomes the new recipient nonce, and skip
	   the recipient nonce if there's one present.  These values may be
	   absent, either because the other side doesn't implement them or
	   because they're not available, for example because it's sending a
	   response to an error that occurred before it could read the nonce from
	   a request.  In any case we don't bother checking the nonce values
	   since the transaction ID serves the same purpose */
	if( peekTag( stream ) == MAKE_CTAG( CTAG_PH_SENDERNONCE ) )
		{
		readConstructed( stream, NULL, CTAG_PH_SENDERNONCE );
		status = readOctetString( stream, protocolInfo->recipNonce,
								  &protocolInfo->recipNonceSize,
								  4, CRYPT_MAX_HASHSIZE );
		if( cryptStatusError( status ) )
			{
			protocolInfo->pkiFailInfo = CMPFAILINFO_BADSENDERNONCE;
			retExt( status,
					( status, SESSION_ERRINFO_VOID, 
					  "Invalid sender nonce in PKI header" ) );
			}
		}
	if( peekTag( stream ) == MAKE_CTAG( CTAG_PH_RECIPNONCE ) )
		{
		readConstructed( stream, NULL, CTAG_PH_RECIPNONCE );
		status = readUniversal( stream );
		if( cryptStatusError( status ) )
			{
			protocolInfo->pkiFailInfo = CMPFAILINFO_BADRECIPIENTNONCE;
			retExt( status,
					( status, SESSION_ERRINFO_VOID, 
					  "Invalid recipient nonce in PKI header" ) );
			}
		}

	/* Generate a new sender nonce (unless this is the first message and
	   we're still setting things up) and see if there's anything useful
	   present in the general info */
	if( protocolInfo->senderNonceSize > 0 )
		{
		MESSAGE_DATA msgData;

		setMessageData( &msgData, protocolInfo->senderNonce,
						protocolInfo->senderNonceSize );
		krnlSendMessage( SYSTEM_OBJECT_HANDLE, IMESSAGE_GETATTRIBUTE_S,
						 &msgData, CRYPT_IATTRIBUTE_RANDOM_NONCE );
		}
	if( stell( stream ) < endPos && \
		peekTag( stream ) == MAKE_CTAG( CTAG_PH_FREETEXT ) )
		status = readUniversal( stream );	/* Junk */
	if( stell( stream ) < endPos && \
		peekTag( stream ) == MAKE_CTAG( CTAG_PH_GENERALINFO ) )
		{
		status = readGeneralInfo( stream, protocolInfo );
		if( cryptStatusError( status ) )
			retExt( status,
					( status, SESSION_ERRINFO_VOID, 
					  "Invalid generalInfo information in PKI header" ) );
		}
	if( stell( stream ) < endPos )
		/* Skip any remaining junk */
		status = sseek( stream, endPos );

	return( status );
	}

/****************************************************************************
*																			*
*							Read a PKI Message								*
*																			*
****************************************************************************/

/* Read a PKI message:

	PkiMessage ::= SEQUENCE {
		header			PKIHeader,
		body			CHOICE { [0]... [24]... },
		protection	[0]	BIT STRING
		}

   Note that readPkiDatagram() has already performed an initial valid-ASN.1
   check before we get here */

int readPkiMessage( SESSION_INFO *sessionInfoPtr,
					CMP_PROTOCOL_INFO *protocolInfo,
					int messageType )
	{
	ERROR_INFO *errorInfo = &sessionInfoPtr->errorInfo;
	MESSAGE_CREATEOBJECT_INFO createInfo;
	STREAM stream;
	const BOOLEAN isServerInitialMessage = ( messageType == CRYPT_UNUSED );
	BOOLEAN useMAC;
	int protPartStart, protPartSize, bodyStart;
	int length, integrityInfoLength, tag, status;

	/* Strip off the header and PKIStatus wrapper */
	sMemConnect( &stream, sessionInfoPtr->receiveBuffer,
				 sessionInfoPtr->receiveBufEnd );
	readSequence( &stream, NULL );		/* Outer wrapper */
	protPartStart = stell( &stream );
	status = readPkiHeader( &stream, protocolInfo, sessionInfoPtr,
							isServerInitialMessage );
	if( cryptStatusError( status ) )
		{
		sMemDisconnect( &stream );
		return( status );
		}

	/* Set up state information based on the header that we've just read.  If
	   this is the first message from the client and we've been sent a new
	   user ID or cert ID, process the user/authentication info.  We
	   couldn't process this info before this point because we didn't know
	   what would be required, but now that we've read the header we can
	   set it up and get the user authentication information from the cert
	   store */
	useMAC = ( protocolInfo->macInfoPos > 0 ) ? TRUE : FALSE;
	if( protocolInfo->isCryptlib )
		sessionInfoPtr->flags |= SESSION_ISCRYPTLIB;
	if( protocolInfo->userIDchanged )
		{
		/* We've got a new PKI user ID, if it looks like a cryptlib encoded
		   ID save it in encoded form, otherwise save it as is.  Note that
		   the value passed to encodePKIUserValue() is the number of code
		   groups to produce in the encoded value, not the input length */
		if( protocolInfo->isCryptlib && \
			protocolInfo->userIDsize == 9 )
			{
			BYTE encodedUserID[ CRYPT_MAX_TEXTSIZE + 8 ];
			int encodedUserIDLength;

			status = encodedUserIDLength = \
				encodePKIUserValue( encodedUserID, CRYPT_MAX_TEXTSIZE,
									protocolInfo->userID, 3 );
			if( !cryptStatusError( status ) )
				status = updateSessionAttribute( &sessionInfoPtr->attributeList,
									CRYPT_SESSINFO_USERNAME, encodedUserID,
									encodedUserIDLength, CRYPT_MAX_TEXTSIZE,
									ATTR_FLAG_ENCODEDVALUE );
			}
		else
			status = updateSessionAttribute( &sessionInfoPtr->attributeList,
									CRYPT_SESSINFO_USERNAME,
									protocolInfo->userID,
									protocolInfo->userIDsize,
									CRYPT_MAX_TEXTSIZE, ATTR_FLAG_NONE );
		if( cryptStatusOK( status ) && isServerInitialMessage && useMAC )
			status = initServerAuthentMAC( sessionInfoPtr, protocolInfo );
		}
	if( cryptStatusOK( status ) && protocolInfo->certIDchanged )
		{
		status = addSessionAttribute( &sessionInfoPtr->attributeList,
									  CRYPT_SESSINFO_SERVER_FINGERPRINT,
									  protocolInfo->certID,
									  protocolInfo->certIDsize );
		if( cryptStatusOK( status ) && isServerInitialMessage )
			status = initServerAuthentSign( sessionInfoPtr, protocolInfo );
		}
	if( cryptStatusError( status ) )
		{
		sMemDisconnect( &stream );
		return( status );
		}

	/* Determine the message body type.  An error response can occur at any
	   point in an exchange so we process this immediately.  We don't do an
	   integrity verification at this point since it's not certain what we
	   should report if the check fails, and an unauthenticated error
	   message is better than an authenticated paketewhainau */
	tag = EXTRACT_CTAG( peekTag( &stream ) );
	if( tag == CTAG_PB_ERROR )
		{
		status = readErrorBody( &stream, sessionInfoPtr );
		sMemDisconnect( &stream );
		return( status );
		}

	/* If this is an initial message we don't know what to expect yet so we
	   set the type to whatever we find, as long as it's a valid message to
	   send to a CA */
	if( isServerInitialMessage && \
		( tag == CTAG_PB_IR || tag == CTAG_PB_CR || tag == CTAG_PB_P10CR || \
		  tag == CTAG_PB_KUR || tag == CTAG_PB_RR || tag == CTAG_PB_GENM ) )
		protocolInfo->operation = messageType = tag;

	/* If we're using a MAC for authentication, we can finally set up the
	   MAC info using the appropriate password */
	if( useMAC )
		{
		const ATTRIBUTE_LIST *passwordPtr = \
					findSessionAttribute( sessionInfoPtr->attributeList,
										  CRYPT_SESSINFO_PASSWORD );
		BYTE decodedValue[ 64 + 8 ];
		const BYTE *decodedValuePtr = decodedValue;
		int decodedValueLength;

		if( passwordPtr->flags & ATTR_FLAG_ENCODEDVALUE )
			{
			/* It's an encoded value, get the decoded form */
			decodedValueLength = decodePKIUserValue( decodedValue, 64,
							passwordPtr->value, passwordPtr->valueLength );
			if( cryptStatusError( decodedValueLength ) )
				{
				assert( NOTREACHED );
				sMemDisconnect( &stream );
				retExt( decodedValueLength,
						( decodedValueLength, SESSION_ERRINFO, 
						  "Invalid PKI user password" ) );
				}
			}
		else
			{
			decodedValuePtr = passwordPtr->value;
			decodedValueLength = passwordPtr->valueLength;
			}

		/* We couldn't initialise the MAC information when we read the
		   header because the order of the information used to set this up
		   is backwards, so we have to go back and re-process it now */
		if( cryptStatusOK( status ) )
			{
			const int streamPos = stell( &stream );

			sseek( &stream, protocolInfo->macInfoPos );
			status = readMacInfo( &stream, protocolInfo, decodedValuePtr,
								  decodedValueLength, sessionInfoPtr );
			sseek( &stream, streamPos );
			}
		zeroise( decodedValue, CRYPT_MAX_TEXTSIZE );
		if( cryptStatusError( status ) )
			{
			sMemDisconnect( &stream );
			return( status );
			}
		}

	/* Make sure that it's what we're after, remember where the message body
	   starts, and skip it (it'll be processed after we verify its integrity) */
	if( tag != messageType )
		{
		sMemDisconnect( &stream );
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADREQUEST;
		if( isServerInitialMessage )
			/* This is the first message and we got no recognisable message
			   of any type */
			retExt( CRYPT_ERROR_BADDATA,
					( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
					  "Invalid message type %d", tag ) );
		retExt( CRYPT_ERROR_BADDATA,
				( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
				  "Invalid message type, expected %d, got %d", messageType,
				  tag ) );
		}
	status = readConstructed( &stream, &length, messageType );
	if( cryptStatusOK ( status ) )
		{
		bodyStart = stell( &stream );
		status = sSkip( &stream, length );
		}
	if( cryptStatusError( status ) )
		{
		sMemDisconnect( &stream );
		protocolInfo->pkiFailInfo = CMPFAILINFO_BADDATAFORMAT;
		retExt( CRYPT_ERROR_BADDATA,
				( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
				  "Invalid message body start" ) );
		}

	/* Read the start of the message integrity info */
	protPartSize = stell( &stream ) - protPartStart;
	status = readConstructed( &stream, &integrityInfoLength,
							  CTAG_PM_PROTECTION );
	if( cryptStatusOK( status ) && \
		integrityInfoLength > sMemDataLeft( &stream ) )
		{
		/* If there integrity protection is missing, report it as a wrong-
		   integrity-info problem, the closest we can get to the real
		   error.  This has already been checked by the high-level PKI
		   datagram read code anyway, but we check gain here just to be
		   safe */
		protocolInfo->pkiFailInfo = CMPFAILINFO_WRONGINTEGRITY;
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
				   "Signature/MAC data is missing or truncated" );
		status = CRYPT_ERROR_SIGNATURE;
		}
	if( cryptStatusOK( status ) && tag == CTAG_PB_IR && !useMAC )
		{
		/* An ir has to be MAC'ed, in theory this doesn't really matter but
		   the spec requires that we only allow a MAC.  If it's not MAC'ed it
		   has to be a cr, which is exactly the same only different */
		protocolInfo->pkiFailInfo = CMPFAILINFO_WRONGINTEGRITY;
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
				   "Received signed ir, should be MAC'ed" );
		status = CRYPT_ERROR_SIGNATURE;
		}
	if( cryptStatusOK( status ) && tag == CTAG_PB_RR && useMAC )
		{
		/* An rr can't be MAC'ed because the trail from the original PKI
		   user to the cert being revoked can become arbitrarily blurred,
		   with the cert being revoked having a different DN,
		   issuerAndSerialNumber, and public key after various updates,
		   replacements, and reissues.  In fact cryptlib tracks the
		   resulting directed graph via the cert store log and allows
		   fetching the original authorising issuer of a cert using the
		   KEYMGMT_FLAG_GETISSUER option, however this requires that the
		   client also be running cryptlib, or specifically that it submit
		   a cert ID in the request, this being the only identifier that
		   reliably identifies the cert being revoked.  Since it's somewhat
		   unsound to assume this, we don't currently handle MAC'ed rr's,
		   however everything is in place to allow them to be implemented
		   if they're really needed */
		protocolInfo->pkiFailInfo = CMPFAILINFO_WRONGINTEGRITY;
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
				   "Received MAC'ed rr, should be signed" );
		status = CRYPT_ERROR_SIGNATURE;
		}
	if( cryptStatusError( status ) )
		{
		sMemDisconnect( &stream );
		return( status );
		}

	/* Verify the message integrity */
	if( protocolInfo->useMACreceive )
		{
		const CRYPT_CONTEXT iMacContext = ( protocolInfo->useAltMAC ) ? \
					protocolInfo->iAltMacContext : protocolInfo->iMacContext;
		int protectionLength;

		/* Read the BIT STRING encapsulation, MAC the data, and make sure
		   that it matches the value attached to the message */
		status = readBitStringHole( &stream, &protectionLength, 16, 
									DEFAULT_TAG );
		if( cryptStatusOK( status ) )
			{
			if( protectionLength > sMemDataLeft( &stream ) )
				status = CRYPT_ERROR_UNDERFLOW;
			else
				if( protectionLength < 16 || \
					protectionLength > CRYPT_MAX_HASHSIZE )
					status = CRYPT_ERROR_BADDATA;
			}
		if( cryptStatusOK( status ) )
			status = hashMessageContents( iMacContext,
							sessionInfoPtr->receiveBuffer + protPartStart,
							protPartSize );
		if( cryptStatusOK( status ) )
			{
			MESSAGE_DATA msgData;

			setMessageData( &msgData, sMemBufPtr( &stream ),
							protectionLength );
			if( cryptStatusError( \
				krnlSendMessage( iMacContext, IMESSAGE_COMPARE, &msgData,
								 MESSAGE_COMPARE_HASH ) ) )
				status = CRYPT_ERROR_SIGNATURE;
			}
		if( cryptStatusError( status ) )
			{
			sMemDisconnect( &stream );
			retExt( CRYPT_ERROR_SIGNATURE,
					( CRYPT_ERROR_SIGNATURE, SESSION_ERRINFO, 
					  "Bad message MAC" ) );
			}
		}
	else
		{
		if( !protocolInfo->isCryptlib )
			{
			MESSAGE_DATA msgData;

			/* Make sure that the sig-check key that we'll be using is the
			   correct one.  Because of CMP's use of a raw signature format
			   we have to do this manually rather than relying on the sig-
			   check code to do it for us, and because of the braindamaged
			   way of identifying integrity-protection keys for non-cryptlib
			   messages even this isn't enough to definitely tell us that
			   we're using the right key, in which case we'll get a bad data
			   or bad sig response from the sig check code */
			setMessageData( &msgData, protocolInfo->senderDNPtr,
							protocolInfo->senderDNlength );
			status = krnlSendMessage( sessionInfoPtr->iAuthInContext,
									  IMESSAGE_COMPARE, &msgData,
									  MESSAGE_COMPARE_SUBJECT );
			if( cryptStatusError( status ) )
				{
				/* A failed comparison is reported as a generic CRYPT_ERROR,
				   convert it into a wrong-key error if necessary */
				sMemDisconnect( &stream );
				retExt( ( status == CRYPT_ERROR ) ? \
						CRYPT_ERROR_WRONGKEY : status,
						( ( status == CRYPT_ERROR ) ? \
						  CRYPT_ERROR_WRONGKEY : status, SESSION_ERRINFO, 
						  "Message signature key doesn't match our "
						  "signature check key, signature can't be "
						  "checked" ) );
				}
			}

		/* Hash the data and verify the signature */
		setMessageCreateObjectInfo( &createInfo, protocolInfo->hashAlgo );
		status = krnlSendMessage( SYSTEM_OBJECT_HANDLE,
								  IMESSAGE_DEV_CREATEOBJECT, &createInfo,
								  OBJECT_TYPE_CONTEXT );
		if( cryptStatusOK( status ) )
			{
			status = hashMessageContents( createInfo.cryptHandle,
							sessionInfoPtr->receiveBuffer + protPartStart,
							protPartSize );
			if( cryptStatusOK( status ) )
				status = checkRawSignature( sMemBufPtr( &stream ),
											integrityInfoLength,
											sessionInfoPtr->iAuthInContext,
											createInfo.cryptHandle );
			krnlSendNotifier( createInfo.cryptHandle, IMESSAGE_DECREFCOUNT );
			if( cryptStatusError( status ) )
				{
				sMemDisconnect( &stream );
				retExt( CRYPT_ERROR_SIGNATURE,
						( CRYPT_ERROR_SIGNATURE, SESSION_ERRINFO, 
						  "Bad message signature" ) );
				}
			}
		}
	if( cryptStatusError( status ) )
		{
		sMemDisconnect( &stream );
		return( status );
		}
	sseek( &stream, bodyStart );

	/* If it's a client request, import the encapsulated request data */
	switch( messageType )
		{
		case CTAG_PB_IR:
		case CTAG_PB_CR:
		case CTAG_PB_P10CR:
		case CTAG_PB_KUR:
		case CTAG_PB_RR:
			status = readRequestBody( &stream, sessionInfoPtr, protocolInfo,
									  messageType );
			break;

		case CTAG_PB_IP:
		case CTAG_PB_CP:
		case CTAG_PB_KUP:
		case CTAG_PB_RP:
			status = readResponseBody( &stream, sessionInfoPtr,
									   protocolInfo );
			break;

		case CTAG_PB_CERTCONF:
			status = readConfBody( &stream, sessionInfoPtr, protocolInfo );
			break;

		case CTAG_PB_PKICONF:
			/* If it's a confirmation there's no message body and we're
			   done */
			break;

		case CTAG_PB_GENM:
		case CTAG_PB_GENP:
			status = readGenMsgBody( &stream, sessionInfoPtr,
									 messageType == CTAG_PB_GENM );
			break;

		default:
			assert( NOTREACHED );
			retExt( CRYPT_ERROR_BADDATA,
					( CRYPT_ERROR_BADDATA, SESSION_ERRINFO, 
					  "Unexpected message type %d", messageType ) );
		}
	sMemDisconnect( &stream );
	return( status );
	}
#endif /* USE_CMP */
