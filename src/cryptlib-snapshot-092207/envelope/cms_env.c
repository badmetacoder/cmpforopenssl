/****************************************************************************
*																			*
*						cryptlib Enveloping Routines						*
*					  Copyright Peter Gutmann 1996-2006						*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "envelope.h"
  #include "asn1.h"
  #include "asn1_ext.h"
#else
  #include "envelope/envelope.h"
  #include "misc/asn1.h"
  #include "misc/asn1_ext.h"
#endif /* Compiler-specific includes */

/* Determine the size of the envelope payload after PKCS #5 block padding if
   necessary.  This isn't just the size rounded up to the nearest multiple of
   the block size since if the size is already a multiple of the block size,
   it expands by another block, so we make the payload look one byte longer
   before rounding to the block size to ensure the one-block expansion */

#define paddedSize( size, blockSize )	\
		( ( blockSize > 1 ) ? roundUp( size + 1, blockSize ) : size )

#ifdef USE_ENVELOPES

/****************************************************************************
*																			*
*								Utility Functions							*
*																			*
****************************************************************************/

/* Check that a requested algorithm type is valid with enveloped data */

BOOLEAN cmsCheckAlgo( const CRYPT_ALGO_TYPE cryptAlgo,
					  const CRYPT_MODE_TYPE cryptMode )
	{
	assert( cryptAlgo > CRYPT_ALGO_NONE && \
			cryptAlgo < CRYPT_ALGO_LAST );
	assert( ( cryptMode == CRYPT_MODE_NONE ) || \
			( cryptMode > CRYPT_MODE_NONE && \
			  cryptMode < CRYPT_MODE_LAST ) );

	return( checkAlgoID( cryptAlgo, cryptMode ) );
	}

/* Get the OID for a CMS content type.  If no type is explicitly given, we
   assume raw data */

typedef struct {
	const CRYPT_CONTENT_TYPE contentType;
	const BYTE *oid;
	} CONTENTOID_INFO;
	
static const CONTENTOID_INFO FAR_BSS contentOIDs[] = {
	{ CRYPT_CONTENT_DATA, OID_CMS_DATA },
	{ CRYPT_CONTENT_SIGNEDDATA, OID_CMS_SIGNEDDATA },
	{ CRYPT_CONTENT_ENVELOPEDDATA, OID_CMS_ENVELOPEDDATA },
	{ CRYPT_CONTENT_SIGNEDANDENVELOPEDDATA, MKOID( "\x06\x09\x2A\x86\x48\x86\xF7\x0D\x01\x07\x04" ) },
	{ CRYPT_CONTENT_DIGESTEDDATA, OID_CMS_DIGESTEDDATA },
	{ CRYPT_CONTENT_ENCRYPTEDDATA, OID_CMS_ENCRYPTEDDATA },
	{ CRYPT_CONTENT_COMPRESSEDDATA, OID_CMS_COMPRESSEDDATA },
	{ CRYPT_CONTENT_AUTHDATA, OID_CMS_AUTHDATA },
	{ CRYPT_CONTENT_AUTHENVDATA, OID_CMS_AUTHENVDATA },
	{ CRYPT_CONTENT_TSTINFO, OID_CMS_TSTOKEN },
	{ CRYPT_CONTENT_SPCINDIRECTDATACONTEXT, OID_MS_SPCINDIRECTDATACONTEXT },
	{ CRYPT_CONTENT_RTCSREQUEST, OID_CRYPTLIB_RTCSREQ },
	{ CRYPT_CONTENT_RTCSRESPONSE, OID_CRYPTLIB_RTCSRESP },
	{ CRYPT_CONTENT_RTCSRESPONSE_EXT, OID_CRYPTLIB_RTCSRESP_EXT },
	{ 0, NULL }, { 0, NULL }
	};

static const BYTE *getContentOID( const CRYPT_CONTENT_TYPE contentType )
	{
	int i;

	assert( contentType > CRYPT_CONTENT_NONE && \
			contentType < CRYPT_CONTENT_LAST );

	for( i = 0; contentOIDs[ i ].oid != NULL && \
				i < FAILSAFE_ARRAYSIZE( contentOIDs, CONTENTOID_INFO ); i++ )
		{
		if( contentOIDs[ i ].contentType == contentType )
			return( contentOIDs[ i ].oid );
		}

	assert( NOTREACHED );
	return( contentOIDs[ 0 ].oid );	/* Get rid of compiler warning */
	}

/* Copy as much post-data state information (i.e. signatures) from the
   auxiliary buffer to the main buffer as possible */

static int copyFromAuxBuffer( ENVELOPE_INFO *envelopeInfoPtr )
	{
	int bytesCopied, dataLeft;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Copy as much of the signature data as we can across */
	bytesCopied = min( envelopeInfoPtr->bufSize - envelopeInfoPtr->bufPos,
					   envelopeInfoPtr->auxBufPos );
	if( bytesCopied < 1 || \
		envelopeInfoPtr->bufPos + bytesCopied > envelopeInfoPtr->bufSize )
		{
		/* Sanity check */
		assert( NOTREACHED );
		return( CRYPT_ERROR_FAILED );
		}
	memcpy( envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos,
			envelopeInfoPtr->auxBuffer, bytesCopied );
	envelopeInfoPtr->bufPos += bytesCopied;

	/* Since we're in the post-data state, any necessary payload data
	   segmentation has been completed.  However, the caller can't copy out
	   any post-payload data because it's past the end-of-segment position.
	   In order to allow the buffer to be emptied to make room for new data
	   from the auxBuffer, we set the end-of-segment position to the end of
	   the new data */
	envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->bufPos;

	/* If there's anything left, move it down in the buffer */
	dataLeft = envelopeInfoPtr->auxBufPos - bytesCopied;
	if( dataLeft > 0 )
		memmove( envelopeInfoPtr->auxBuffer, \
				 envelopeInfoPtr->auxBuffer + bytesCopied, dataLeft );
	envelopeInfoPtr->auxBufPos = dataLeft;
	assert( dataLeft >= 0 );

	return( ( dataLeft > 0 ) ? CRYPT_ERROR_OVERFLOW : CRYPT_OK );
	}

/* Write one or more indefinite-length end-of-contents indicators */

static int writeEOCs( ENVELOPE_INFO *envelopeInfoPtr, const int count )
	{
	static const BYTE indefEOC[ 16 ] = \
						{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
						  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const int dataLeft = envelopeInfoPtr->bufSize - envelopeInfoPtr->bufPos;
	const int eocLength = count * 2;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( count > 0 && count <= 8 );

	if( dataLeft < eocLength )
		return( CRYPT_ERROR_OVERFLOW );
	memcpy( envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos, indefEOC,
			eocLength );
	envelopeInfoPtr->bufPos += eocLength;
	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*						Emit Content-Specific Headers						*
*																			*
****************************************************************************/

/* Write the header fields that encapsulate any enveloped data:

   SignedData/DigestedData */

static int writeSignedDataHeader( STREAM *stream,
								  const ENVELOPE_INFO *envelopeInfoPtr,
								  const BOOLEAN isSignedData )
	{
	const BYTE *contentOID = getContentOID( envelopeInfoPtr->contentType );
	ACTION_LIST *actionListPtr;
	long dataSize;
	int hashActionSize = 0, iterationCount;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Determine the size of the hash actions */
	iterationCount = 0;
	for( actionListPtr = envelopeInfoPtr->actionList; 
		 actionListPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX;
		 actionListPtr = actionListPtr->next )
		{
		const int actionSize = \
			sizeofContextAlgoID( actionListPtr->iCryptHandle,
								 CRYPT_ALGO_NONE, ALGOID_FLAG_ALGOID_ONLY );
		if( cryptStatusError( actionSize ) )
			return( actionSize );
		hashActionSize += actionSize;
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();
	
	/* Determine the size of the SignedData/DigestedData */
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED || \
		( envelopeInfoPtr->dataFlags & ENVDATA_HASINDEFTRAILER ) )
		dataSize = CRYPT_UNUSED;
	else
		{
		/* Determine the size of the content OID + content */
		dataSize = ( envelopeInfoPtr->payloadSize > 0 ) ? \
			sizeofObject( sizeofObject( envelopeInfoPtr->payloadSize ) ) : 0;
		dataSize = sizeofObject( sizeofOID( contentOID ) + dataSize );

		/* Determine the size of the version, hash algoID, content, cert
		   chain, and signatures */
		dataSize = sizeofShortInteger( 1 ) + sizeofObject( hashActionSize ) + \
				   dataSize + envelopeInfoPtr->extraDataSize + \
				   sizeofObject( envelopeInfoPtr->signActionSize );
		}

	/* Write the SignedData/DigestedData header, version number, and SET OF
	   DigestInfo */
	writeCMSheader( stream, ( isSignedData ) ? \
					OID_CMS_SIGNEDDATA : OID_CMS_DIGESTEDDATA, dataSize,
					FALSE );
	writeShortInteger( stream, 1, DEFAULT_TAG );
	writeSet( stream, hashActionSize );
	iterationCount = 0;
	for( actionListPtr = envelopeInfoPtr->actionList; 
		 actionListPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX;
		 actionListPtr = actionListPtr->next )
		{
		int status = writeContextAlgoID( stream,
							actionListPtr->iCryptHandle, CRYPT_ALGO_NONE,
							ALGOID_FLAG_ALGOID_ONLY );
		if( cryptStatusError( status ) )
			return( status );
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();

	/* Write the inner Data header */
	return( writeCMSheader( stream, contentOID, envelopeInfoPtr->payloadSize,
							TRUE ) );
	}

/* EncryptedContentInfo contained within EnvelopedData.  This may also be 
   Authenticated or AuthEnc data, so the encryption context can be 
   CRYPT_UNUSED */

static int writeEncryptedContentHeader( STREAM *stream,
							const BYTE *contentOID,
							const CRYPT_CONTEXT iCryptContext,
							const long payloadSize, const long blockSize )
	{
	const long blockedPayloadSize = ( payloadSize == CRYPT_UNUSED ) ? \
						CRYPT_UNUSED : paddedSize( payloadSize, blockSize );

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contentOID, sizeofOID( contentOID ) ) );
	assert( isHandleRangeValid( iCryptContext ) || \
			iCryptContext == CRYPT_UNUSED );
	assert( payloadSize == CRYPT_UNUSED || payloadSize > 0 );
	assert( blockSize > 1 && blockSize <= CRYPT_MAX_IVSIZE );

	return( writeCMSencrHeader( stream, contentOID, blockedPayloadSize,
								iCryptContext ) );
	}

/* EncryptedData, EnvelopedData */

static int getEncrypedContentSize( const ENVELOPE_INFO *envelopeInfoPtr,
								   const BYTE *contentOID,
								   long *blockedPayloadSize,
								   long *encrContentInfoSize )
	{
	long length;

	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( isReadPtr( contentOID, sizeofOID( contentOID ) ) );
	assert( isWritePtr( blockedPayloadSize, sizeof( long ) ) );
	assert( isWritePtr( encrContentInfoSize, sizeof( long ) ) );

	/* Calculate the size of the payload after encryption blocking */
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED )
		*blockedPayloadSize = CRYPT_UNUSED;
	else
		*blockedPayloadSize = paddedSize( envelopeInfoPtr->payloadSize, 
										  envelopeInfoPtr->blockSize );

	/* Calculate the size of the CMS ContentInfo header */
	length = sizeofCMSencrHeader( contentOID, *blockedPayloadSize, 
								  envelopeInfoPtr->iCryptContext );
	if( cryptStatusError( length ) )
		return( ( int ) length );
	*encrContentInfoSize = length;

	return( CRYPT_OK );
	}

static void writeEncryptionHeader( STREAM *stream, const BYTE *oid,
								   const int version,
								   const long blockedPayloadSize,
								   const long extraSize )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( oid, sizeofOID( oid ) ) );
	assert( version >= 0 && version < 10 );
	assert( blockedPayloadSize == CRYPT_UNUSED || blockedPayloadSize > 0 );
	assert( extraSize == CRYPT_UNUSED || extraSize > 0 );

	writeCMSheader( stream, oid,
					( blockedPayloadSize == CRYPT_UNUSED || \
					  extraSize == CRYPT_UNUSED ) ? CRYPT_UNUSED : \
						sizeofShortInteger( 0 ) + extraSize + blockedPayloadSize,
					FALSE );
	writeShortInteger( stream, version, DEFAULT_TAG );
	}

static int writeEncryptedDataHeader( STREAM *stream,
									 const ENVELOPE_INFO *envelopeInfoPtr )
	{
	const BYTE *contentOID = getContentOID( envelopeInfoPtr->contentType );
	long blockedPayloadSize, encrContentInfoSize;
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Calculate the size of the payload due to blocking and the ContentInfo
	   header */
	status = getEncrypedContentSize( envelopeInfoPtr, contentOID,
									 &blockedPayloadSize, 
									 &encrContentInfoSize );
	if( cryptStatusError( status ) )
		return( status );

	/* Write the EncryptedData header and version number, and
	   EncryptedContentInfo header */
	writeEncryptionHeader( stream, OID_CMS_ENCRYPTEDDATA, 0,
						   blockedPayloadSize, encrContentInfoSize );
	return( writeEncryptedContentHeader( stream, contentOID,
				envelopeInfoPtr->iCryptContext, envelopeInfoPtr->payloadSize,
				envelopeInfoPtr->blockSize ) );
	}

static int writeEnvelopedDataHeader( STREAM *stream,
									 const ENVELOPE_INFO *envelopeInfoPtr )
	{
	const BYTE *contentOID = getContentOID( envelopeInfoPtr->contentType );
	long blockedPayloadSize, encrContentInfoSize;
#ifdef USE_KEA
	const int originatorInfoSize = ( envelopeInfoPtr->extraDataSize > 0 ) ? \
			( int ) sizeofObject( envelopeInfoPtr->extraDataSize ) : 0;
#else
	#define originatorInfoSize	0
#endif /* USE_KEA */
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Calculate the size of the payload due to blocking and the ContentInfo
	   header */
	status = getEncrypedContentSize( envelopeInfoPtr, contentOID,
									 &blockedPayloadSize, 
									 &encrContentInfoSize );
	if( cryptStatusError( status ) )
		return( status );

	/* Write the EnvelopedData header and version number and start of the SET
	   OF RecipientInfo/EncryptionKeyInfo */
	writeEncryptionHeader( stream, OID_CMS_ENVELOPEDDATA,
					( originatorInfoSize > 0 ) ? 2 : 0, blockedPayloadSize,
					( envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED ) ? \
						CRYPT_UNUSED : \
						sizeofObject( envelopeInfoPtr->cryptActionSize ) + \
							originatorInfoSize + encrContentInfoSize );
#ifdef USE_KEA
	if( originatorInfoSize > 0 )
		{
		int status;

		/* Write the wrapper for the originator info and the originator info
		   itself */
		writeConstructed( stream, envelopeInfoPtr->extraDataSize, 0 );

		/* Export the originator cert chain either directly into the main
		   buffer or into the auxBuffer if there's not enough room */
		if( originatorInfoSize >= sMemDataLeft( stream ) )
			{
			/* The originator chain is too big for the main buffer, we have
			   to write everything from this point on into the auxBuffer.
			   This is then flushed into the main buffer in the calling code
			   before anything else is written */
			stream = ( STREAM * ) &envelopeInfoPtr->auxStream;
			}
		status = exportCertToStream( stream, envelopeInfoPtr->iExtraCertChain,
									 CRYPT_ICERTFORMAT_CERTSET );
		if( cryptStatusError( status ) )
			return( status );
		}
#endif /* USE_KEA */

	return( ( envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED ) ? \
			writeSetIndef( stream ) : \
			writeSet( stream, envelopeInfoPtr->cryptActionSize ) );
	}

/* AuthenticatedData */

static int writeAuthenticatedDataHeader( STREAM *stream,
							const ENVELOPE_INFO *envelopeInfoPtr )
	{
	const BYTE *contentOID = getContentOID( envelopeInfoPtr->contentType );
	const int macActionSize = \
				sizeofContextAlgoID( envelopeInfoPtr->actionList->iCryptHandle,
									 CRYPT_ALGO_NONE, ALGOID_FLAG_ALGOID_ONLY );

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	if( cryptStatusError( macActionSize ) )
		return( macActionSize );

	/* Write the AuthenticatedData header and version number and start of the SET
	   OF RecipientInfo */
 	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED )
		writeEncryptionHeader( stream, OID_CMS_AUTHDATA, 0, 1, CRYPT_UNUSED );
	else
		{
		int macSize, contentInfoSize, status;

		/* Determine the size of the MAC and the encapsulated content header */
		status = krnlSendMessage( envelopeInfoPtr->actionList->iCryptHandle, 
								  IMESSAGE_GETATTRIBUTE, &macSize, 
								  CRYPT_CTXINFO_BLOCKSIZE );
		if( cryptStatusError( status ) )
			return( status );
		contentInfoSize = sizeofObject( \
							sizeofObject( envelopeInfoPtr->payloadSize ) );
		contentInfoSize = sizeofObject( \
							sizeofOID( contentOID ) + contentInfoSize ) - \
						  envelopeInfoPtr->payloadSize;

		/* Write the data header */
		writeEncryptionHeader( stream, OID_CMS_AUTHDATA, 0,
				envelopeInfoPtr->payloadSize,
				( envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED ) ? \
					CRYPT_UNUSED : \
					sizeofObject( envelopeInfoPtr->cryptActionSize ) + \
					macActionSize + contentInfoSize + sizeofObject( macSize ) );
		}

	return( ( envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED ) ? \
			writeSetIndef( stream ) : \
			writeSet( stream, envelopeInfoPtr->cryptActionSize ) );
	}

/* CompressedData */

static int writeCompressedDataHeader( STREAM *stream,
									  ENVELOPE_INFO *envelopeInfoPtr )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Since compressing the data changes its length, we have to use the
	   indefinite-length encoding even if we know how big the payload is */
	envelopeInfoPtr->payloadSize = CRYPT_UNUSED;

	/* Write the CompressedData header, version number, and Zlib algoID */
	writeCMSheader( stream, OID_CMS_COMPRESSEDDATA, CRYPT_UNUSED, FALSE );
	writeShortInteger( stream, 0, DEFAULT_TAG );
	writeGenericAlgoID( stream, OID_ZLIB );

	/* Write the inner Data header */
	return( writeCMSheader( stream, getContentOID( envelopeInfoPtr->contentType ),
							CRYPT_UNUSED, TRUE ) );
	}

/****************************************************************************
*																			*
*						Content-Specific Pre-processing						*
*																			*
****************************************************************************/

/* Pre-process information for encrypted enveloping */

static int processKeyexchangeAction( ENVELOPE_INFO *envelopeInfoPtr,
									 ACTION_LIST *actionListPtr,
									 const CRYPT_DEVICE iCryptDevice )
	{
	int cryptAlgo, status;
#ifdef USE_KEA
	BYTE originatorDomainParams[ CRYPT_MAX_HASHSIZE + 8 ];
	int originatorDomainParamSize = 0;
#endif /* USE_KEA */

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( actionListPtr != NULL && \
			( actionListPtr->action == ACTION_KEYEXCHANGE_PKC || \
			  actionListPtr->action == ACTION_KEYEXCHANGE ) );
	assert( iCryptDevice == CRYPT_ERROR || \
			isHandleRangeValid( iCryptDevice ) );

	/* If the session key/MAC context is tied to a device, make sure that
	   the key exchange object is in the same device */
	if( iCryptDevice != CRYPT_ERROR )
		{
		CRYPT_DEVICE iKeyexDevice;

		status = krnlSendMessage( actionListPtr->iCryptHandle,
								  MESSAGE_GETDEPENDENT, &iKeyexDevice,
								  OBJECT_TYPE_DEVICE );
		if( cryptStatusError( status ) || iCryptDevice != iKeyexDevice )
			{
			setErrorInfo( envelopeInfoPtr, 
						  ( envelopeInfoPtr->usage == ACTION_CRYPT ) ? \
							CRYPT_ENVINFO_SESSIONKEY : CRYPT_ENVINFO_INTEGRITY,
						  CRYPT_ERRTYPE_CONSTRAINT );
			return( CRYPT_ERROR_INVALID );
			}
		}

#ifdef USE_KEA
	/* If there's an originator chain present, get the originator's domain
	   parameters */
	if( envelopeInfoPtr->iExtraCertChain != CRYPT_ERROR )
		{
		MESSAGE_DATA msgData;

		setMessageData( &msgData, originatorDomainParams,
						 CRYPT_MAX_HASHSIZE );
		status = krnlSendMessage( envelopeInfoPtr->iExtraCertChain,
								  IMESSAGE_GETATTRIBUTE_S, &msgData,
								  CRYPT_IATTRIBUTE_KEY_KEADOMAINPARAMS );
		if( cryptStatusError( status ) )
			return( status );
		originatorDomainParamSize = msgData.length;
		}

	/* If it's a key agreement action, make sure that there's originator
	   info present and that the domain parameters match */
	if( actionListPtr->action == ACTION_KEYEXCHANGE_PKC && \
		cryptStatusOK( krnlSendMessage( actionListPtr->iCryptHandle,
										IMESSAGE_CHECK, NULL,
										MESSAGE_CHECK_PKC_KA_EXPORT ) ) )
		{
		MESSAGE_DATA msgData;
		BYTE domainParams[ CRYPT_MAX_HASHSIZE + 8 ];

		if( originatorDomainParamSize <= 0 )
			{
			setErrorInfo( envelopeInfoPtr, CRYPT_ENVINFO_ORIGINATOR,
						  CRYPT_ERRTYPE_ATTR_ABSENT );
			return( CRYPT_ERROR_NOTINITED );
			}
		setMessageData( &msgData, domainParams, CRYPT_MAX_HASHSIZE );
		status = krnlSendMessage( actionListPtr->iCryptHandle,
								  IMESSAGE_GETATTRIBUTE_S, &msgData,
								  CRYPT_IATTRIBUTE_KEY_KEADOMAINPARAMS );
		if( cryptStatusError( status ) )
			return( status );
		if( ( originatorDomainParamSize != msgData.length ) || \
			memcmp( originatorDomainParams, domainParams,
					originatorDomainParamSize ) )
			{
			setErrorInfo( envelopeInfoPtr, CRYPT_ENVINFO_ORIGINATOR,
						  CRYPT_ERRTYPE_CONSTRAINT );
			return( CRYPT_ERROR_INVALID );
			}
		}
#endif /* USE_KEA */

	/* Remember that we now have a controlling action and connect the
	   controller to the subject */
	envelopeInfoPtr->actionList->flags &= ~ACTION_NEEDSCONTROLLER;
	actionListPtr->associatedAction = envelopeInfoPtr->actionList;

	/* Evaluate the size of the exported action.  If it's a conventional key
	   exchange, we force the use of the CMS format since there's no reason
	   to use the cryptlib format */
	status = iCryptExportKeyEx( NULL, &actionListPtr->encodedSize, 0,
						( actionListPtr->action == ACTION_KEYEXCHANGE ) ? \
							CRYPT_FORMAT_CMS : envelopeInfoPtr->type,
						envelopeInfoPtr->actionList->iCryptHandle,
						actionListPtr->iCryptHandle );
	if( cryptStatusOK( status ) )
		status = krnlSendMessage( actionListPtr->iCryptHandle,
								  IMESSAGE_GETATTRIBUTE, &cryptAlgo,
								  CRYPT_CTXINFO_ALGO );
	if( cryptStatusError( status ) )
		return( status );

	/* If there are any key exchange actions that will result in indefinite-
	   length encodings present, we can't use a definite-length encoding for
	   the key exchange actions */
	return( ( cryptAlgo == CRYPT_ALGO_ELGAMAL ) ? OK_SPECIAL : CRYPT_OK );
	}

static int preEnvelopeEncrypt( ENVELOPE_INFO *envelopeInfoPtr )
	{
	CRYPT_DEVICE iCryptDevice = CRYPT_ERROR;
	ACTION_LIST *actionListPtr;
	BOOLEAN hasIndefSizeActions = FALSE;
	int totalSize, iterationCount, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->usage == ACTION_CRYPT || \
			envelopeInfoPtr->usage == ACTION_MAC );

#ifdef USE_KEA
	/* If there's originator info present, find out what it'll take to encode
	   it into the envelope header */
	if( envelopeInfoPtr->iExtraCertChain != CRYPT_ERROR )
		{
		MESSAGE_DATA msgData;
		int status;

		/* Determine how big the originator cert chain will be */
		setMessageData( &msgData, NULL, 0 );
		status = krnlSendMessage( envelopeInfoPtr->iExtraCertChain,
								  IMESSAGE_CRT_EXPORT, &msgData,
								  CRYPT_ICERTFORMAT_CERTSET );
		if( cryptStatusError( status ) )
			return( status );
		envelopeInfoPtr->extraDataSize = msgData.length;

		/* If we have very long originator cert chains the auxBuffer may not
		   be large enough to contain the resulting chain, so we have to
		   expand it to handle the chain */
		if( envelopeInfoPtr->auxBufSize < envelopeInfoPtr->extraDataSize + 64 )
			{
			assert( envelopeInfoPtr->auxBuffer == NULL );
			if( ( envelopeInfoPtr->auxBuffer = \
					clDynAlloc( "preEnvelopeEncrypt", \
								envelopeInfoPtr->extraDataSize + 64 ) ) == NULL )
				return( CRYPT_ERROR_MEMORY );
			envelopeInfoPtr->auxBufSize = envelopeInfoPtr->extraDataSize + 64;
			}
		}
#endif /* USE_KEA */

	/* If there are no key exchange actions present, we're done */
	if( envelopeInfoPtr->preActionList == NULL )
		return( CRYPT_OK );

	/* Create the session/MAC key if necessary */
	if( envelopeInfoPtr->actionList == NULL )
		{
		MESSAGE_CREATEOBJECT_INFO createInfo;

		/* Create a default encryption action and add it to the action
		   list */
		setMessageCreateObjectInfo( &createInfo,
							( envelopeInfoPtr->usage == ACTION_CRYPT ) ? \
								envelopeInfoPtr->defaultAlgo : \
								envelopeInfoPtr->defaultMAC );
		status = krnlSendMessage( SYSTEM_OBJECT_HANDLE,
								  IMESSAGE_DEV_CREATEOBJECT, &createInfo,
								  OBJECT_TYPE_CONTEXT );
		if( cryptStatusError( status ) )
			return( status );
		status = krnlSendMessage( createInfo.cryptHandle, 
								  IMESSAGE_CTX_GENKEY, NULL, FALSE );
		if( cryptStatusOK( status ) && \
			addAction( &envelopeInfoPtr->actionList,
					   envelopeInfoPtr->memPoolState,
					   envelopeInfoPtr->usage,
					   createInfo.cryptHandle ) == NULL )
			status = CRYPT_ERROR_MEMORY;
		if( cryptStatusError( status ) )
			{
			krnlSendNotifier( createInfo.cryptHandle, IMESSAGE_DECREFCOUNT );
			return( status );
			}
		}
	else
		{
		/* If the session key/MAC context is tied to a device, get its handle
		   so we can check that all key exchange objects are also in the same
		   device */
		status = krnlSendMessage( envelopeInfoPtr->actionList->iCryptHandle,
								  MESSAGE_GETDEPENDENT, &iCryptDevice,
								  OBJECT_TYPE_DEVICE );
		if( cryptStatusError( status ) )
			iCryptDevice = CRYPT_ERROR;
		}
	assert( envelopeInfoPtr->actionList != NULL );

	/* Notify the kernel that the session key/MAC context is attached to the
	   envelope.  This is an internal object used only by the envelope, so
	   we tell the kernel not to increment its reference count when it
	   attaches it */
	krnlSendMessage( envelopeInfoPtr->objectHandle, IMESSAGE_SETDEPENDENT,
					 &envelopeInfoPtr->actionList->iCryptHandle,
					 SETDEP_OPTION_NOINCREF );

	/* Now walk down the list of key exchange actions evaluating their size
	   and connecting each one to the session key/MAC action */
	totalSize = 0; iterationCount = 0;
	for( actionListPtr = envelopeInfoPtr->preActionList;
		 actionListPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX; 
		 actionListPtr = actionListPtr->next )
		{
		status = processKeyexchangeAction( envelopeInfoPtr, actionListPtr,
										   iCryptDevice );
		if( cryptStatusError( status ) )
			{
			if( status != OK_SPECIAL )
				return( status );
			hasIndefSizeActions = TRUE;
			}
		totalSize += actionListPtr->encodedSize;
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();
	envelopeInfoPtr->cryptActionSize = hasIndefSizeActions ? \
									   CRYPT_UNUSED : totalSize;

	/* If we're MACing the data, hashing is now active */
	if( envelopeInfoPtr->usage == ACTION_MAC )
		envelopeInfoPtr->dataFlags |= ENVDATA_HASHACTIONSACTIVE;

	return( CRYPT_OK );
	}

/* Pre-process information for signed enveloping */

static int processSignatureAction( ENVELOPE_INFO *envelopeInfoPtr,
								   ACTION_LIST *actionListPtr )
	{
	int cryptAlgo, signatureSize, signingAttributes, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( isWritePtr( actionListPtr, sizeof( ACTION_LIST ) ) && \
			actionListPtr->action == ACTION_SIGN );
	assert( actionListPtr->associatedAction != NULL );

	/* Process signing certs if necessary and match the content-type in the
	   authenticated attributes with the signed content type if it's anything
	   other than 'data' (the data content-type is added automatically) */
	if( envelopeInfoPtr->type == CRYPT_FORMAT_CMS || \
		envelopeInfoPtr->type == CRYPT_FORMAT_SMIME )
		{
		/* If we're including signing certs and there are multiple signing
		   certs present, add the currently-selected one to the overall cert
		   collection */
		if( !( envelopeInfoPtr->flags & ENVELOPE_NOSIGNINGCERTS ) && \
			envelopeInfoPtr->iExtraCertChain != CRYPT_ERROR )
			{
			status = krnlSendMessage( envelopeInfoPtr->iExtraCertChain,
									  IMESSAGE_SETATTRIBUTE,
									  &actionListPtr->iCryptHandle,
									  CRYPT_IATTRIBUTE_CERTCOLLECTION );
			if( cryptStatusError( status ) )
				return( status );
			}

		/* If there's no content-type present and the signed content type
		   isn't 'data' or it's an S/MIME envelope, create signing attributes
		   to hold the content-type and smimeCapabilities.  Then, make sure
		   that the content-type in the attributes matches the actual content
		   type */
		if( actionListPtr->iExtraData == CRYPT_ERROR && \
			( envelopeInfoPtr->contentType != CRYPT_CONTENT_DATA || \
			  envelopeInfoPtr->type == CRYPT_FORMAT_SMIME ) )
			{
			MESSAGE_CREATEOBJECT_INFO createInfo;

			setMessageCreateObjectInfo( &createInfo,
										CRYPT_CERTTYPE_CMS_ATTRIBUTES );
			status = krnlSendMessage( SYSTEM_OBJECT_HANDLE,
									  IMESSAGE_DEV_CREATEOBJECT,
									  &createInfo, OBJECT_TYPE_CERTIFICATE );
			if( cryptStatusError( status ) )
				return( status );
			actionListPtr->iExtraData = createInfo.cryptHandle;
			}
		if( actionListPtr->iExtraData != CRYPT_ERROR )
			{
			int value;

			/* Delete any existing content-type (quietly fixing things if
			   necessary is easier than trying to report this error back to
			   the caller) and add our one */
			if( krnlSendMessage( actionListPtr->iExtraData,
						IMESSAGE_GETATTRIBUTE, &value,
						CRYPT_CERTINFO_CMS_CONTENTTYPE ) != CRYPT_ERROR_NOTFOUND )
				krnlSendMessage( actionListPtr->iExtraData,
								 IMESSAGE_DELETEATTRIBUTE, NULL,
								 CRYPT_CERTINFO_CMS_CONTENTTYPE );
			krnlSendMessage( actionListPtr->iExtraData,
							 IMESSAGE_SETATTRIBUTE,
							 &envelopeInfoPtr->contentType,
							 CRYPT_CERTINFO_CMS_CONTENTTYPE );
			}
		}

	/* Determine the type of signing attributes to use.  If none are
	   specified  (which can only happen if the signed content is data),
	   either get the signing code to add the default ones for us, or use
	   none at all if the use of default attributes is disabled */
	signingAttributes = actionListPtr->iExtraData;
	if( signingAttributes == CRYPT_ERROR )
		{
		int useDefaultAttributes;

		status = krnlSendMessage( envelopeInfoPtr->ownerHandle, 
								  IMESSAGE_GETATTRIBUTE,
								  &useDefaultAttributes,
								  CRYPT_OPTION_CMS_DEFAULTATTRIBUTES );
		if( cryptStatusError( status ) )
			return( status );
		signingAttributes = useDefaultAttributes ? \
							CRYPT_USE_DEFAULT : CRYPT_UNUSED;
		}

	/* Evaluate the size of the exported action */
	status = iCryptCreateSignatureEx( NULL, &signatureSize, 0,
						envelopeInfoPtr->type, actionListPtr->iCryptHandle,
						actionListPtr->associatedAction->iCryptHandle,
						signingAttributes,
						( actionListPtr->iTspSession != CRYPT_ERROR ) ? \
							actionListPtr->iTspSession : CRYPT_UNUSED );
	if( cryptStatusOK( status ) )
		status = krnlSendMessage( actionListPtr->iCryptHandle,
								  IMESSAGE_GETATTRIBUTE, &cryptAlgo,
								  CRYPT_CTXINFO_ALGO );
	if( cryptStatusError( status ) )
		return( status );
	if( cryptAlgo == CRYPT_ALGO_DSA || \
		actionListPtr->iTspSession != CRYPT_ERROR )
		{
		/* If there are any signature actions that will result in indefinite-
		   length encodings present, we can't use a definite-length encoding
		   for the signature */
		envelopeInfoPtr->dataFlags |= ENVDATA_HASINDEFTRAILER;
		actionListPtr->encodedSize = CRYPT_UNUSED;
		}
	else
		{
		actionListPtr->encodedSize = signatureSize;
		envelopeInfoPtr->signActionSize += signatureSize;
		}

	return( CRYPT_OK );
	}

static int preEnvelopeSign( ENVELOPE_INFO *envelopeInfoPtr )
	{
	ACTION_LIST *actionListPtr = envelopeInfoPtr->postActionList;
	int iterationCount, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->usage == ACTION_SIGN );

	/* Make sure that there's at least one signing action present */
	if( actionListPtr == NULL )
		return( CRYPT_ERROR_NOTINITED );

	assert( isWritePtr( actionListPtr, sizeof( ACTION_LIST ) ) );
	assert( actionListPtr->associatedAction != NULL );

	/* If we're generating a detached signature, the content is supplied
	   externally and has zero size */
	if( envelopeInfoPtr->flags & ENVELOPE_DETACHED_SIG )
		envelopeInfoPtr->payloadSize = 0;

	/* If it's an attributes-only message, it must be zero-length CMS signed
	   data with signing attributes present */
	if( envelopeInfoPtr->flags & ENVELOPE_ATTRONLY )
		{
		if( envelopeInfoPtr->type != CRYPT_FORMAT_CMS || \
			actionListPtr->iExtraData == CRYPT_ERROR )
			{
			setErrorInfo( envelopeInfoPtr, CRYPT_ENVINFO_SIGNATURE_EXTRADATA,
						  CRYPT_ERRTYPE_ATTR_ABSENT );
			return( CRYPT_ERROR_NOTINITED );
			}
		if( envelopeInfoPtr->payloadSize > 0 )
			{
			setErrorInfo( envelopeInfoPtr, CRYPT_ENVINFO_DATASIZE,
						  CRYPT_ERRTYPE_ATTR_VALUE );
			return( CRYPT_ERROR_INITED );
			}
		}

	/* If it's a CMS envelope we have to write the signing cert chain
	   alongside the signatures as extra data unless it's explicitly
	   excluded, so we record how large the info will be for later */
	if( ( envelopeInfoPtr->type == CRYPT_FORMAT_CMS || \
		  envelopeInfoPtr->type == CRYPT_FORMAT_SMIME ) && \
		!( envelopeInfoPtr->flags & ENVELOPE_NOSIGNINGCERTS ) )
		{
		if( actionListPtr->next != NULL )
			{
			MESSAGE_CREATEOBJECT_INFO createInfo;

			/* There are multiple sets of signing certs present, create a
			   signing-cert meta-object to hold the overall set of certs */
			setMessageCreateObjectInfo( &createInfo,
										CRYPT_CERTTYPE_CERTCHAIN );
			status = krnlSendMessage( SYSTEM_OBJECT_HANDLE,
									  IMESSAGE_DEV_CREATEOBJECT,
									  &createInfo, OBJECT_TYPE_CERTIFICATE );
			if( cryptStatusError( status ) )
				return( status );
			envelopeInfoPtr->iExtraCertChain = createInfo.cryptHandle;
			}
		else
			{
			MESSAGE_DATA msgData;

			/* There's a single signing cert present, determine its size */
			setMessageData( &msgData, NULL, 0 );
			status = krnlSendMessage( actionListPtr->iCryptHandle,
									  IMESSAGE_CRT_EXPORT, &msgData,
									  CRYPT_ICERTFORMAT_CERTSET );
			if( cryptStatusError( status ) )
				return( status );
			envelopeInfoPtr->extraDataSize = msgData.length;
			}
		}

	/* Evaluate the size of each signature action */
	iterationCount = 0;
	for( actionListPtr = envelopeInfoPtr->postActionList; 
		 actionListPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX;
		 actionListPtr = actionListPtr->next )
		{
		status = processSignatureAction( envelopeInfoPtr, actionListPtr );
		if( cryptStatusError( status ) )
			return( status );
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();
	if( envelopeInfoPtr->iExtraCertChain != CRYPT_ERROR )
		{
		MESSAGE_DATA msgData;

		/* We're writing the signing cert chain and there are multiple
		   signing certs present, get the size of the overall cert
		   collection */
		setMessageData( &msgData, NULL, 0 );
		status = krnlSendMessage( envelopeInfoPtr->iExtraCertChain,
								  IMESSAGE_CRT_EXPORT, &msgData,
								  CRYPT_ICERTFORMAT_CERTSET );
		if( cryptStatusError( status ) )
			return( status );
		envelopeInfoPtr->extraDataSize = msgData.length;
		}

	/* Hashing is now active */
	envelopeInfoPtr->dataFlags |= ENVDATA_HASHACTIONSACTIVE;

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Header Processing Routines						*
*																			*
****************************************************************************/

/* Write the envelope header */

static int writeEnvelopeHeader( ENVELOPE_INFO *envelopeInfoPtr )
	{
	STREAM stream;
	int length, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* If we're encrypting, set up the encryption-related information */
	if( envelopeInfoPtr->usage == ACTION_CRYPT )
		{
		status = initEnvelopeEncryption( envelopeInfoPtr,
								envelopeInfoPtr->actionList->iCryptHandle,
								CRYPT_ALGO_NONE, CRYPT_MODE_NONE, NULL, 0,
								FALSE );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Write the appropriate CMS header based on the envelope usage.  The
	   DigestedData/ACTION_HASH action is never taken since the higher-level 
	   code assumes that the presence of hash actions indicates the desire 
	   to create signed data and returns an error if no signature actions are 
	   present */
	sMemOpen( &stream, envelopeInfoPtr->buffer, envelopeInfoPtr->bufSize );
	switch( envelopeInfoPtr->usage )
		{
		case ACTION_CRYPT:
			if( envelopeInfoPtr->preActionList == NULL )
				status = writeEncryptedDataHeader( &stream,
												   envelopeInfoPtr );
			else
				status = writeEnvelopedDataHeader( &stream,
												   envelopeInfoPtr );
			break;

		case ACTION_SIGN:
			status = writeSignedDataHeader( &stream, envelopeInfoPtr, TRUE );
			break;

		case ACTION_HASH:
			status = writeSignedDataHeader( &stream, envelopeInfoPtr, FALSE );
			break;

		case ACTION_COMPRESS:
			status = writeCompressedDataHeader( &stream, envelopeInfoPtr );
			break;

		case ACTION_NONE:
			status = writeCMSheader( &stream,
								getContentOID( envelopeInfoPtr->contentType ),
								envelopeInfoPtr->payloadSize, FALSE );
			break;

		case ACTION_MAC:
			status = writeAuthenticatedDataHeader( &stream, envelopeInfoPtr );
			break;

		default:
			assert( NOTREACHED );
			return( CRYPT_ERROR_INTERNAL );
		}
	length = stell( &stream );
	sMemDisconnect( &stream );
	if( cryptStatusError( status ) )
		return( status );
	envelopeInfoPtr->bufPos = length;

	/* If we're not encrypting with key exchange actions, we're done */
	if( ( envelopeInfoPtr->usage != ACTION_CRYPT && \
		  envelopeInfoPtr->usage != ACTION_MAC ) || \
		envelopeInfoPtr->preActionList == NULL )
		{
		/* Make sure that we start a new segment if we try to add any data, 
		   set the block size mask to all ones if we're not encrypting since 
		   we can begin and end data segments on arbitrary boundaries, and 
		   inform the caller that we're done */
//		envelopeInfoPtr->dataFlags |= ENVDATA_SEGMENTCOMPLETE;
		if( envelopeInfoPtr->usage != ACTION_CRYPT )
			envelopeInfoPtr->blockSizeMask = -1;
		envelopeInfoPtr->lastAction = NULL;
		return( OK_SPECIAL );
		}

	/* Start emitting the key exchange actions */
	envelopeInfoPtr->lastAction = findAction( envelopeInfoPtr->preActionList,
											  ACTION_KEYEXCHANGE_PKC );
	if( envelopeInfoPtr->lastAction == NULL )
		envelopeInfoPtr->lastAction = findAction( envelopeInfoPtr->preActionList,
												  ACTION_KEYEXCHANGE );
	assert( envelopeInfoPtr->lastAction != NULL );

	return( CRYPT_OK );
	}

/* Write key exchange actions */

static int writeKeyex( ENVELOPE_INFO *envelopeInfoPtr )
	{
	const CRYPT_CONTEXT iCryptContext = \
							( envelopeInfoPtr->usage == ACTION_CRYPT ) ? \
							envelopeInfoPtr->iCryptContext : \
							envelopeInfoPtr->actionList->iCryptHandle;
	ACTION_LIST *lastActionPtr;
	int iterationCount = 0, status = CRYPT_OK;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Export the session key/MAC using each of the PKC or conventional 
	   keys.  If it's a conventional key exchange, we force the use of the 
	   CMS format since there's no reason to use the cryptlib format */
	for( lastActionPtr = envelopeInfoPtr->lastAction;
		 lastActionPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX;
		 lastActionPtr = lastActionPtr->next )
		{
		const CRYPT_FORMAT_TYPE formatType = \
						( lastActionPtr->action == ACTION_KEYEXCHANGE ) ? \
						CRYPT_FORMAT_CMS : envelopeInfoPtr->type;
		const int dataLeft = min( envelopeInfoPtr->bufSize - \
								  envelopeInfoPtr->bufPos, 8192 );
		int keyexSize;

		/* Make sure that there's enough room to emit this key exchange 
		   action */
		if( lastActionPtr->encodedSize + 128 > dataLeft )
			{
			status = CRYPT_ERROR_OVERFLOW;
			break;
			}

		/* Emit the key exchange action */
		status = iCryptExportKeyEx( envelopeInfoPtr->buffer + \
									envelopeInfoPtr->bufPos, &keyexSize,
									dataLeft, formatType, iCryptContext,
									lastActionPtr->iCryptHandle );
		if( cryptStatusError( status ) )
			break;
		envelopeInfoPtr->bufPos += keyexSize;
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();
	envelopeInfoPtr->lastAction = lastActionPtr;
	if( cryptStatusError( status ) )
		return( status );

	/* If it's an indefinite-length header, close off the set of key 
	   exchange actions */
	if( envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED )
		return( writeEOCs( envelopeInfoPtr, 1 ) );

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Trailer Processing Routines						*
*																			*
****************************************************************************/

/* Write signing cert chain.  This can grow arbitrarily large, and in 
   particular can become larger than the main envelope buffer if multiple 
   signatures with long chains and a small envelope buffer are used, so we 
   emit the cert chain into a dynamically-allocated auxiliary buffer if 
   there isn't enough room to emit it into the main buffer  */

static int writeCertchainTrailer( ENVELOPE_INFO *envelopeInfoPtr )
	{
	STREAM stream;
	void *certChainBufPtr;
	const int dataLeft = min( envelopeInfoPtr->bufSize - \
							  envelopeInfoPtr->bufPos, 32767 );
	const int eocSize = ( envelopeInfoPtr->payloadSize == CRYPT_UNUSED ) ? \
						( 3 * 2 ) : 0;
	int certChainBufSize, certChainSize, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Check whether there's enough room left in the buffer to emit the 
	   signing cert chain directly into it */
	if( envelopeInfoPtr->extraDataSize + 64 < dataLeft )
		{
		certChainBufPtr = envelopeInfoPtr->buffer + \
						  envelopeInfoPtr->bufPos + eocSize;
		certChainBufSize = dataLeft - eocSize;
		}
	else
		{
		/* If there's almost no room left in the buffer anyway, tell the 
		   user that they have to pop some data before they can continue.  
		   Hopefully this will create enough room to emit the certs directly 
		   into the buffer */
		if( dataLeft < 1024 )
			return( CRYPT_ERROR_OVERFLOW );

		/* We can't emit the certs directly into the envelope buffer, 
		   allocate an auxiliary buffer for them and from there copy them 
		   into the main buffer */
		assert( envelopeInfoPtr->auxBuffer == NULL );
		if( ( envelopeInfoPtr->auxBuffer = \
				clDynAlloc( "emitPostamble",
							envelopeInfoPtr->extraDataSize + 64 ) ) == NULL )
			return( CRYPT_ERROR_MEMORY );
		certChainBufPtr = envelopeInfoPtr->auxBuffer;
		certChainBufSize = envelopeInfoPtr->auxBufSize = \
									envelopeInfoPtr->extraDataSize + 64;
		}

	/* Write the end-of-contents octets for the Data OCTET STRING, [0], and 
	   SEQUENCE if necessary */
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED )
		{
		status = writeEOCs( envelopeInfoPtr, 3 );
		if( cryptStatusError( status ) )
			return( status );
		}
	envelopeInfoPtr->lastAction = envelopeInfoPtr->postActionList;

	/* Write the signing cert chain if it's a CMS signature and they're not 
	   explicitly excluded, followed by the SET OF SignerInfo header */
	sMemOpen( &stream, certChainBufPtr, certChainBufSize );
	if( ( envelopeInfoPtr->type == CRYPT_FORMAT_CMS || \
		  envelopeInfoPtr->type == CRYPT_FORMAT_SMIME ) && \
		!( envelopeInfoPtr->flags & ENVELOPE_NOSIGNINGCERTS ) )
		{
		status = exportCertToStream( &stream,
							( envelopeInfoPtr->iExtraCertChain != CRYPT_ERROR ) ? \
							  envelopeInfoPtr->iExtraCertChain : \
							  envelopeInfoPtr->lastAction->iCryptHandle,
							CRYPT_ICERTFORMAT_CERTSET );
		if( cryptStatusError( status ) )
			{
			sMemDisconnect( &stream );
			return( status );
			}
		}
	if( envelopeInfoPtr->dataFlags & ENVDATA_HASINDEFTRAILER )
		status = writeSetIndef( &stream );
	else
		status = writeSet( &stream, envelopeInfoPtr->signActionSize );
	certChainSize = stell( &stream );
	sMemDisconnect( &stream );
	if( cryptStatusError( status ) )
		return( status );

	/* If we're copying data via the auxBuffer, flush as much as we can into
	   the main buffer.  If we can't copy it all in, resulting in an overflow
	   error, we use the OK_SPECIAL status to tell the caller that although
	   an overflow occurred, it was due to the auxBuffer copy and not the
	   certchain write, and it's OK to move on to the next state */
	if( envelopeInfoPtr->auxBufSize > 0 )
		{
		envelopeInfoPtr->auxBufPos = certChainSize;
		status = copyFromAuxBuffer( envelopeInfoPtr );
		return( ( status == CRYPT_ERROR_OVERFLOW ) ? OK_SPECIAL : status );
		}

	/* Since we're in the post-data state, any necessary payload data 
	   segmentation has been completed.  However, the caller can't copy out 
	   any post-payload data because it's past the end-of-segment position.  
	   In order to allow the buffer to be emptied to make room for signature 
	   data, we set the end-of-segment position to the end of the new data */
	envelopeInfoPtr->bufPos += certChainSize;
	envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->bufPos;

	return( CRYPT_OK );
	}

/* Write signatures */

static int writeSignatures( ENVELOPE_INFO *envelopeInfoPtr )
	{
	ACTION_LIST *lastActionPtr;
	int iterationCount, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );

	/* Sign each hash using the associated signature key */
	iterationCount = 0;
	for( lastActionPtr = envelopeInfoPtr->lastAction;
		 lastActionPtr != NULL && iterationCount++ < FAILSAFE_ITERATIONS_MAX; 
		 lastActionPtr = lastActionPtr->next )
		{
		const int sigBufSize = min( envelopeInfoPtr->bufSize - \
									envelopeInfoPtr->bufPos, 32767 );
		int sigSize, signingAttributes = lastActionPtr->iExtraData;

		assert( lastActionPtr->action == ACTION_SIGN );

		/* Check whether there's enough room left in the buffer to emit the
		   signature directly into it.  Since sigs are fairly small (a
		   few hundred bytes), we always require enough room in the buffer
		   and don't bother with any overflow handling via the auxBuffer */
		if( lastActionPtr->encodedSize + 64 > sigBufSize )
			{
			status = CRYPT_ERROR_OVERFLOW;
			break;
			}

		/* Determine the type of signing attributes to use.  If none are
		   specified (which can only happen under circumstances controlled
		   by the pre-envelope signing code), either get the signing code to
		   add the default ones for us, or use none at all if the use of
		   default attributes is disabled */
		if( signingAttributes == CRYPT_ERROR )
			{
			int useDefaultAttributes;

			status = krnlSendMessage( envelopeInfoPtr->ownerHandle,
							 IMESSAGE_GETATTRIBUTE, &useDefaultAttributes,
							 CRYPT_OPTION_CMS_DEFAULTATTRIBUTES );
			if( cryptStatusError( status ) )
				return( status );
			signingAttributes = useDefaultAttributes ? CRYPT_USE_DEFAULT : \
													   CRYPT_UNUSED;
			}

		/* Sign the data */
		status = iCryptCreateSignatureEx( envelopeInfoPtr->buffer + \
										  envelopeInfoPtr->bufPos, &sigSize,
							sigBufSize, envelopeInfoPtr->type,
							lastActionPtr->iCryptHandle,
							lastActionPtr->associatedAction->iCryptHandle,
							signingAttributes,
							( lastActionPtr->iTspSession != CRYPT_ERROR ) ? \
							lastActionPtr->iTspSession : CRYPT_UNUSED );
		if( cryptStatusError( status ) )
			break;
		envelopeInfoPtr->bufPos += sigSize;
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MAX )
		retIntError();
	envelopeInfoPtr->lastAction = lastActionPtr;
	return( status );
	}

/* Write MAC value */

static int writeMAC( ENVELOPE_INFO *envelopeInfoPtr )
	{
	STREAM stream;
	MESSAGE_DATA msgData;
	BYTE hash[ CRYPT_MAX_HASHSIZE + 8 ];
	const int eocSize = ( envelopeInfoPtr->payloadSize == CRYPT_UNUSED ) ? \
						( 3 * 2 ) : 0;
	const int dataLeft = min( envelopeInfoPtr->bufSize - \
							  envelopeInfoPtr->bufPos, 512 );
	int length, status;

	/* Make sure that there's room for the MAC data in the buffer */
	if( dataLeft < eocSize + sizeofObject( CRYPT_MAX_HASHSIZE ) )
		return( CRYPT_ERROR_OVERFLOW );

	/* Write the end-of-contents octets for the Data OCTET STRING, [0], and 
	   SEQUENCE if necessary */
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED )
		{
		status = writeEOCs( envelopeInfoPtr, 3 );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Get the MAC value and write it to the buffer */
	setMessageData( &msgData, hash, CRYPT_MAX_HASHSIZE );
	status = krnlSendMessage( envelopeInfoPtr->actionList->iCryptHandle,
							  IMESSAGE_GETATTRIBUTE_S, &msgData,
							  CRYPT_CTXINFO_HASHVALUE );
	if( cryptStatusError( status ) )
		return( status );
	sMemOpen( &stream, envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos, 
			  dataLeft );
	status = writeOctetString( &stream, hash, msgData.length, DEFAULT_TAG );
	length = stell( &stream );
	sMemDisconnect( &stream );
	if( cryptStatusOK( status ) )
		envelopeInfoPtr->bufPos += length;

	return( status );
	}

/****************************************************************************
*																			*
*							Emit Envelope Preamble/Postamble				*
*																			*
****************************************************************************/

/* Output as much of the preamble as possible into the envelope buffer */

static int emitPreamble( ENVELOPE_INFO *envelopeInfoPtr )
	{
	int status = CRYPT_OK;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->envState >= ENVSTATE_NONE && \
			envelopeInfoPtr->envState <= ENVSTATE_DONE );

	/* If we've finished processing the header information, don't do
	   anything */
	if( envelopeInfoPtr->envState == ENVSTATE_DONE )
		return( CRYPT_OK );

	/* If we haven't started doing anything yet, perform various final
	   initialisations */
	if( envelopeInfoPtr->envState == ENVSTATE_NONE )
		{
		/* If there's no nested content type set, default to plain data */
		if( envelopeInfoPtr->contentType == CRYPT_CONTENT_NONE )
			envelopeInfoPtr->contentType = CRYPT_CONTENT_DATA;

		/* If there's an absolute data length set, remember it for when we
		   copy in data */
		if( envelopeInfoPtr->payloadSize != CRYPT_UNUSED )
			envelopeInfoPtr->segmentSize = envelopeInfoPtr->payloadSize;

		/* Perform any remaining initialisation.  MAC'd data is a special-
		   case form of encrypted data so we treat them as the same thing
		   at the key exchange level */
		if( envelopeInfoPtr->usage == ACTION_CRYPT || \
			envelopeInfoPtr->usage == ACTION_MAC )
			status = preEnvelopeEncrypt( envelopeInfoPtr );
		else
			if( envelopeInfoPtr->usage == ACTION_SIGN )
				status = preEnvelopeSign( envelopeInfoPtr );
		if( cryptStatusError( status ) )
			return( status );

		/* Delete any orphaned actions such as automatically-added hash
		   actions that were overridden with user-supplied alternate
		   actions */
		deleteUnusedActions( envelopeInfoPtr );

		/* Make sure that we start a new segment when we add the first lot
		   of payload data after we've emitted the header info */
		envelopeInfoPtr->dataFlags |= ENVDATA_SEGMENTCOMPLETE;

		/* We're ready to go, prepare to emit the outer header */
		envelopeInfoPtr->envState = ENVSTATE_HEADER;
		if( !checkActions( envelopeInfoPtr ) )
			retIntError();
		}

	/* Emit the outer header.  This always follows directly from the final
	   initialisation step, but we keep the two logically distinct to
	   emphasise that the former is merely finalising enveloping actions
	   without performing any header processing, while the latter is the
	   first stage that actually emits header data */
	if( envelopeInfoPtr->envState == ENVSTATE_HEADER )
		{
		status = writeEnvelopeHeader( envelopeInfoPtr );
		if( cryptStatusError( status ) )
			{
			/* If there's nothing else to emit, we're done */
			if( status == OK_SPECIAL )
				{
				envelopeInfoPtr->envState = ENVSTATE_DONE;
				return( CRYPT_OK );
				}

			return( status );
			}

		/* Move on to the next state */
		envelopeInfoPtr->envState = ENVSTATE_KEYINFO;
		}

	/* Handle key export actions */
	if( envelopeInfoPtr->envState == ENVSTATE_KEYINFO )
		{
		status = writeKeyex( envelopeInfoPtr );
		if( cryptStatusError( status ) )
			return( status );

		/* Move on to the next state */
		envelopeInfoPtr->envState = ENVSTATE_ENCRINFO;
		}

	/* Handle encrypted content information */
	if( envelopeInfoPtr->envState == ENVSTATE_ENCRINFO )
		{
		STREAM stream;
		const void *contentOID = getContentOID( envelopeInfoPtr->contentType );
		const int dataLeft = min( envelopeInfoPtr->bufSize - \
								  envelopeInfoPtr->bufPos, 8192 );
		int length;

		/* Make sure that there's enough room to emit the data header.  The
		   value used is only approximate, if there's not enough room left
		   the write will also return an overflow error */
		if( dataLeft < 256 )
			return( CRYPT_ERROR_OVERFLOW );

		/* Write the encrypted content header */
		sMemOpen( &stream, envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos,
				  dataLeft );
		if( envelopeInfoPtr->usage == ACTION_MAC )
			{
			/* If it's authenticated data, there's a MAC algorithm ID 
			   preceding standard EncapContent */
			status = writeContextAlgoID( &stream, 
										 envelopeInfoPtr->actionList->iCryptHandle,
										 CRYPT_ALGO_NONE, 
										 ALGOID_FLAG_ALGOID_ONLY );
			if( cryptStatusOK ( status ) )
				status = writeCMSheader( &stream, contentOID, 
										 envelopeInfoPtr->payloadSize, TRUE );
			}
		else
			/* It's encrypted data, it's EncrContent */
			status = writeEncryptedContentHeader( &stream, contentOID,
										envelopeInfoPtr->iCryptContext, 
										envelopeInfoPtr->payloadSize, 
										envelopeInfoPtr->blockSize );
		length = stell( &stream );
		sMemDisconnect( &stream );
		if( cryptStatusError( status ) )
			return( status );
		envelopeInfoPtr->bufPos += length;

		/* Make sure that we start a new segment if we try to add any data */
//		envelopeInfoPtr->dataFlags |= ENVDATA_SEGMENTCOMPLETE;

		/* We're done */
		envelopeInfoPtr->envState = ENVSTATE_DONE;
		}

	return( CRYPT_OK );
	}

/* Output as much of the postamble as possible into the envelope buffer */

static int emitPostamble( ENVELOPE_INFO *envelopeInfoPtr )
	{
	int status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->envState >= ENVSTATE_NONE && \
			envelopeInfoPtr->envState <= ENVSTATE_DONE );

	/* Before we can emit the trailer we need to flush any remaining data
	   from internal buffers */
	if( envelopeInfoPtr->envState == ENVSTATE_NONE )
		{
		status = envelopeInfoPtr->copyToEnvelopeFunction( envelopeInfoPtr,
													( BYTE * ) "", 0 );
		if( cryptStatusError( status ) )
			return( status );
		envelopeInfoPtr->envState = \
					( envelopeInfoPtr->usage == ACTION_SIGN ) ? \
					ENVSTATE_FLUSHED : ENVSTATE_SIGNATURE;
		}

	/* The only message type that has a trailer is signed or authenticated 
	   data, so if we're not signing/authenticating data we can exit now */
	if( envelopeInfoPtr->usage != ACTION_SIGN && \
		envelopeInfoPtr->usage != ACTION_MAC )
		{
		/* Emit the various end-of-contents octets if necessary */
		if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED || \
			( envelopeInfoPtr->usage == ACTION_CRYPT &&
			  envelopeInfoPtr->cryptActionSize == CRYPT_UNUSED ) )
			{
			/* Write the end-of-contents octets for the encapsulated data if
			   necessary.  Normally we have two EOC's, however compressed
			   data requires an extra one due to the explicit tagging */
			if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED && \
				( envelopeInfoPtr->usage == ACTION_CRYPT || \
				  envelopeInfoPtr->usage == ACTION_COMPRESS ) )
				status = writeEOCs( envelopeInfoPtr, 3 + \
									( ( envelopeInfoPtr->usage == \
										ACTION_COMPRESS ) ? \
									  3 : 2 ) );
			else
				{
				/* Write the remaining end-of-contents octets for the OCTET
				   STRING/SEQUENCE, [0], and SEQUENCE */
				status = writeEOCs( envelopeInfoPtr, 3 );
				}
			if( cryptStatusError( status ) )
				return( status );
			}

		/* Now that we've written the final end-of-contents octets, set the end-
		   of-segment-data pointer to the end of the data in the buffer so that
		   copyFromEnvelope() can copy out the remaining data */
		envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->bufPos;
		envelopeInfoPtr->envState = ENVSTATE_DONE;

		return( CRYPT_OK );
		}

	/* If there's any signature data left in the auxiliary buffer, try and
	   empty that first */
	if( envelopeInfoPtr->auxBufPos > 0 )
		{
		status = copyFromAuxBuffer( envelopeInfoPtr );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Handle signing cert chain */
	if( envelopeInfoPtr->envState == ENVSTATE_FLUSHED )
		{
		status = writeCertchainTrailer( envelopeInfoPtr );
		if( cryptStatusError( status ) && status != OK_SPECIAL )
			return( status );

		/* Move on to the next state */
		envelopeInfoPtr->envState = ENVSTATE_SIGNATURE;

		/* If we were copying from the auxBuffer and got an overflow error,
		   we have to resume later in the signature state */
		if( status == OK_SPECIAL )
			return( CRYPT_ERROR_OVERFLOW );
		}

	/* Handle signing actions */
	assert( envelopeInfoPtr->envState == ENVSTATE_SIGNATURE );

	/* Write the signatures/MACs */
	if( envelopeInfoPtr->usage == ACTION_SIGN )
		status = writeSignatures( envelopeInfoPtr );
	else
		status = writeMAC( envelopeInfoPtr );
	if( cryptStatusError( status ) )
		return( status );

	/* Write the end-of-contents octets for the OCTET STRING/SEQUENCE, [0],
	   and SEQUENCE if necessary.  If the trailer has an indefinite length
	   then we need to add an EOC for the trailer as well */
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED || \
		( envelopeInfoPtr->dataFlags & ENVDATA_HASINDEFTRAILER ) )
		{
		status = writeEOCs( envelopeInfoPtr,
							3 + ( ( envelopeInfoPtr->dataFlags & \
									ENVDATA_HASINDEFTRAILER ) ? \
								  1 : 0 ) );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Now that we've written the final end-of-contents octets, set the end-
	   of-segment-data pointer to the end of the data in the buffer so that
	   copyFromEnvelope() can copy out the remaining data */
	envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->bufPos;
	envelopeInfoPtr->envState = ENVSTATE_DONE;

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Envelope Access Routines						*
*																			*
****************************************************************************/

void initCMSEnveloping( ENVELOPE_INFO *envelopeInfoPtr )
	{
	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( !( envelopeInfoPtr->flags & ENVELOPE_ISDEENVELOPE ) );

	/* Set the access method pointers */
	envelopeInfoPtr->processPreambleFunction = emitPreamble;
	envelopeInfoPtr->processPostambleFunction = emitPostamble;
	envelopeInfoPtr->checkAlgo = cmsCheckAlgo;

	/* Set up the processing state information */
	envelopeInfoPtr->envState = ENVSTATE_NONE;

	/* Remember the current default settings for use with the envelope.
	   We force the use of the CBC encryption mode because this is the
	   safest and most efficient encryption mode, and the only mode defined
	   for many CMS algorithms.  Since the CMS algorithms represent only a
	   subset of what's available, we have to drop back to fixed values if
	   the caller has selected something exotic */
	krnlSendMessage( envelopeInfoPtr->ownerHandle, IMESSAGE_GETATTRIBUTE,
					 &envelopeInfoPtr->defaultHash, CRYPT_OPTION_ENCR_HASH );
	if( !checkAlgoID( envelopeInfoPtr->defaultHash, CRYPT_MODE_NONE ) )
		envelopeInfoPtr->defaultHash = CRYPT_ALGO_SHA;
	krnlSendMessage( envelopeInfoPtr->ownerHandle, IMESSAGE_GETATTRIBUTE,
					 &envelopeInfoPtr->defaultAlgo, CRYPT_OPTION_ENCR_ALGO );
	if( !checkAlgoID( envelopeInfoPtr->defaultAlgo,
					  ( envelopeInfoPtr->defaultAlgo == CRYPT_ALGO_RC4 ) ? \
					  CRYPT_MODE_OFB : CRYPT_MODE_CBC ) )
		envelopeInfoPtr->defaultAlgo = CRYPT_ALGO_3DES;
	krnlSendMessage( envelopeInfoPtr->ownerHandle, IMESSAGE_GETATTRIBUTE,
					 &envelopeInfoPtr->defaultMAC, CRYPT_OPTION_ENCR_MAC );
	if( !checkAlgoID( envelopeInfoPtr->defaultMAC, CRYPT_MODE_NONE ) )
		envelopeInfoPtr->defaultMAC = CRYPT_ALGO_HMAC_SHA;
	}
#endif /* USE_ENVELOPES */
