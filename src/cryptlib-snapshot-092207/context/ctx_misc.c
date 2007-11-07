/****************************************************************************
*																			*
*						cryptlib Context Support Routines					*
*						Copyright Peter Gutmann 1995-2005					*
*																			*
****************************************************************************/

#define PKC_CONTEXT		/* Indicate that we're working with PKC context */
#if defined( INC_ALL )
  #include "crypt.h"
  #include "context.h"
#else
  #include "crypt.h"
  #include "context/context.h"
#endif /* Compiler-specific includes */

/* Prototypes for functions in keyload.c */

int getKeysize( CONTEXT_INFO *contextInfoPtr, const int requestedKeyLength );

/****************************************************************************
*																			*
*						Capability Management Functions						*
*																			*
****************************************************************************/

/* Check that a capability info record is consistent.  This is a complex
   function which is called from an assert() macro, so we only need to define
   it when we're building the debug version of the code */

#ifndef NDEBUG

BOOLEAN capabilityInfoOK( const CAPABILITY_INFO *capabilityInfoPtr,
						  const BOOLEAN asymmetricOK )
	{
	CRYPT_ALGO_TYPE cryptAlgo = capabilityInfoPtr->cryptAlgo;

	/* Check the algorithm and mode parameters */
	if( cryptAlgo <= CRYPT_ALGO_NONE || cryptAlgo >= CRYPT_ALGO_LAST_MAC || \
		capabilityInfoPtr->algoName == NULL || \
		capabilityInfoPtr->algoNameLen < 3 || \
		capabilityInfoPtr->algoNameLen > CRYPT_MAX_TEXTSIZE - 1 )
		return( FALSE );

	/* Make sure that the minimum functions are present */
	if( isStreamCipher( cryptAlgo ) )
		{
		if( capabilityInfoPtr->encryptOFBFunction == NULL || \
			capabilityInfoPtr->decryptOFBFunction == NULL )
			return( FALSE );
		}
	else
		if( asymmetricOK )
			{
			/* If asymmetric capabilities (e.g. decrypt but not encrypt,
			   present in some tinkertoy tokens) are OK, we only check
			   that there's at least one useful capability available */
			if( capabilityInfoPtr->decryptFunction == NULL && \
				capabilityInfoPtr->signFunction == NULL )
				return( FALSE );
			}
		else
			/* We need at least one mechanism pair to be able to do anything
			   useful with the capability */
			if( ( capabilityInfoPtr->encryptFunction == NULL || \
				  capabilityInfoPtr->decryptFunction == NULL ) && \
				( capabilityInfoPtr->encryptCBCFunction == NULL || \
				  capabilityInfoPtr->decryptCBCFunction == NULL ) && \
				( capabilityInfoPtr->encryptCFBFunction == NULL || \
				  capabilityInfoPtr->decryptCFBFunction == NULL ) && \
				( capabilityInfoPtr->encryptOFBFunction == NULL || \
				  capabilityInfoPtr->decryptOFBFunction == NULL ) && \
				( capabilityInfoPtr->signFunction == NULL || \
				  capabilityInfoPtr->sigCheckFunction == NULL ) )
				return( FALSE );

	/* Make sure that the algorithm/mode-specific parameters are
	   consistent */
	if( capabilityInfoPtr->minKeySize > capabilityInfoPtr->keySize || \
		capabilityInfoPtr->maxKeySize < capabilityInfoPtr->keySize )
		return( FALSE );
	if( cryptAlgo >= CRYPT_ALGO_FIRST_CONVENTIONAL && \
		cryptAlgo <= CRYPT_ALGO_LAST_CONVENTIONAL )
		{
		if( ( capabilityInfoPtr->blockSize < bitsToBytes( 8 ) || \
        	  capabilityInfoPtr->blockSize > CRYPT_MAX_IVSIZE ) || \
			( capabilityInfoPtr->minKeySize < MIN_KEYSIZE || \
			  capabilityInfoPtr->maxKeySize > CRYPT_MAX_KEYSIZE ) )
			return( FALSE );
		if( capabilityInfoPtr->initKeyParamsFunction == NULL || \
			capabilityInfoPtr->initKeyFunction == NULL )
			return( FALSE );
		if( !isStreamCipher( cryptAlgo ) && \
			 capabilityInfoPtr->blockSize < bitsToBytes( 64 ) )
			return( FALSE );
		if( ( capabilityInfoPtr->encryptCBCFunction != NULL && \
			  capabilityInfoPtr->decryptCBCFunction == NULL ) || \
			( capabilityInfoPtr->encryptCBCFunction == NULL && \
			  capabilityInfoPtr->decryptCBCFunction != NULL ) )
			return( FALSE );
		if( ( capabilityInfoPtr->encryptCFBFunction != NULL && \
			  capabilityInfoPtr->decryptCFBFunction == NULL ) || \
			( capabilityInfoPtr->encryptCFBFunction == NULL && \
			  capabilityInfoPtr->decryptCFBFunction != NULL ) )
			return( FALSE );
		if( ( capabilityInfoPtr->encryptOFBFunction != NULL && \
			  capabilityInfoPtr->decryptOFBFunction == NULL ) || \
			( capabilityInfoPtr->encryptOFBFunction == NULL && \
			  capabilityInfoPtr->decryptOFBFunction != NULL ) )
			return( FALSE );
		}
	if( cryptAlgo >= CRYPT_ALGO_FIRST_PKC && \
		cryptAlgo <= CRYPT_ALGO_LAST_PKC )
		{
		const int minKeySize = isEccAlgo( cryptAlgo ) ? \
							   MIN_PKCSIZE_ECC : MIN_PKCSIZE;

		if( capabilityInfoPtr->blockSize || \
			( capabilityInfoPtr->minKeySize < minKeySize || \
			  capabilityInfoPtr->maxKeySize > CRYPT_MAX_PKCSIZE ) )
			return( FALSE );
		if( capabilityInfoPtr->initKeyFunction == NULL )
			return( FALSE );
		}
	if( cryptAlgo >= CRYPT_ALGO_FIRST_HASH && \
		cryptAlgo <= CRYPT_ALGO_LAST_HASH )
		{
		if( ( capabilityInfoPtr->blockSize < bitsToBytes( 128 ) || \
			  capabilityInfoPtr->blockSize > CRYPT_MAX_HASHSIZE ) || \
			( capabilityInfoPtr->minKeySize || capabilityInfoPtr->keySize || \
			  capabilityInfoPtr->maxKeySize ) )
			return( FALSE );
		}
	if( cryptAlgo >= CRYPT_ALGO_FIRST_MAC && \
		cryptAlgo <= CRYPT_ALGO_LAST_MAC )
		{
		if( ( capabilityInfoPtr->blockSize < bitsToBytes( 128 ) || \
			  capabilityInfoPtr->blockSize > CRYPT_MAX_HASHSIZE ) || \
			( capabilityInfoPtr->minKeySize < MIN_KEYSIZE || \
			  capabilityInfoPtr->maxKeySize > CRYPT_MAX_KEYSIZE ) )
			return( FALSE );
		if( capabilityInfoPtr->initKeyFunction == NULL )
			return( FALSE );
		}

	return( TRUE );
	}
#endif /* !NDEBUG */

/* Get information from a capability record */

void getCapabilityInfo( CRYPT_QUERY_INFO *cryptQueryInfo,
						const CAPABILITY_INFO FAR_BSS *capabilityInfoPtr )
	{
	memset( cryptQueryInfo, 0, sizeof( CRYPT_QUERY_INFO ) );
	memcpy( cryptQueryInfo->algoName, capabilityInfoPtr->algoName,
			capabilityInfoPtr->algoNameLen );
	cryptQueryInfo->algoName[ capabilityInfoPtr->algoNameLen ] = '\0';
	cryptQueryInfo->blockSize = capabilityInfoPtr->blockSize;
	cryptQueryInfo->minKeySize = capabilityInfoPtr->minKeySize;
	cryptQueryInfo->keySize = capabilityInfoPtr->keySize;
	cryptQueryInfo->maxKeySize = capabilityInfoPtr->maxKeySize;
	}

/* Find the capability record for a given encryption algorithm */

const CAPABILITY_INFO FAR_BSS *findCapabilityInfo(
					const CAPABILITY_INFO_LIST *capabilityInfoList,
					const CRYPT_ALGO_TYPE cryptAlgo )
	{
	const CAPABILITY_INFO_LIST *capabilityInfoListPtr;

	/* Find the capability corresponding to the requested algorithm/mode */
	for( capabilityInfoListPtr = capabilityInfoList;
		 capabilityInfoListPtr != NULL;
		 capabilityInfoListPtr = capabilityInfoListPtr->next )
		if( capabilityInfoListPtr->info->cryptAlgo == cryptAlgo )
			return( capabilityInfoListPtr->info );

	return( NULL );
	}

/****************************************************************************
*																			*
*							Shared Context Functions						*
*																			*
****************************************************************************/

/* Default handler to get object subtype-specific information.  This 
   fallback function is called if the object-specific primary get-info 
   handler doesn't want to handle the query */

int getDefaultInfo( const CAPABILITY_INFO_TYPE type, 
					void *varParam, const int constParam )
	{
	switch( type )
		{
		case CAPABILITY_INFO_KEYSIZE:
			return( getKeysize( varParam, constParam ) );

		case CAPABILITY_INFO_STATESIZE:
			return( 0 );
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR );	/* Get rid of compiler warning */
	}

/****************************************************************************
*																			*
*							Bignum Support Routines 						*
*																			*
****************************************************************************/

#ifdef USE_PKC

/* Clear temporary bignum values used during PKC operations */

void clearTempBignums( PKC_INFO *pkcInfo )
	{
	BN_clear( &pkcInfo->tmp1 );
	BN_clear( &pkcInfo->tmp2 );
	BN_clear( &pkcInfo->tmp3 );
	BN_CTX_clear( pkcInfo->bnCTX );
	}

/* Initialse and free the bignum information in a context */

void initContextBignums( PKC_INFO *pkcInfo, 
						 const BOOLEAN useSideChannelProtection )
	{
	/* Initialise the bignum information */
	BN_init( &pkcInfo->param1 );
	BN_init( &pkcInfo->param2 );
	BN_init( &pkcInfo->param3 );
	BN_init( &pkcInfo->param4 );
	BN_init( &pkcInfo->param5 );
	BN_init( &pkcInfo->param6 );
	BN_init( &pkcInfo->param7 );
	BN_init( &pkcInfo->param8 );
	if( useSideChannelProtection )
		{
		BN_init( &pkcInfo->blind1 );
		BN_init( &pkcInfo->blind2 );
		}
	BN_init( &pkcInfo->tmp1 );
	BN_init( &pkcInfo->tmp2 );
	BN_init( &pkcInfo->tmp3 );
	pkcInfo->bnCTX = BN_CTX_new();
	BN_MONT_CTX_init( &pkcInfo->montCTX1 );
	BN_MONT_CTX_init( &pkcInfo->montCTX2 );
	BN_MONT_CTX_init( &pkcInfo->montCTX3 );
	}

void freeContextBignums( PKC_INFO *pkcInfo, int contextFlags )
	{
	if( !( contextFlags & CONTEXT_DUMMY ) )
		{
		BN_clear_free( &pkcInfo->param1 );
		BN_clear_free( &pkcInfo->param2 );
		BN_clear_free( &pkcInfo->param3 );
		BN_clear_free( &pkcInfo->param4 );
		BN_clear_free( &pkcInfo->param5 );
		BN_clear_free( &pkcInfo->param6 );
		BN_clear_free( &pkcInfo->param7 );
		BN_clear_free( &pkcInfo->param8 );
		if( contextFlags & CONTEXT_SIDECHANNELPROTECTION )
			{
			BN_clear_free( &pkcInfo->blind1 );
			BN_clear_free( &pkcInfo->blind2 );
			}
		BN_clear_free( &pkcInfo->tmp1 );
		BN_clear_free( &pkcInfo->tmp2 );
		BN_clear_free( &pkcInfo->tmp3 );
		BN_MONT_CTX_free( &pkcInfo->montCTX1 );
		BN_MONT_CTX_free( &pkcInfo->montCTX2 );
		BN_MONT_CTX_free( &pkcInfo->montCTX3 );
		BN_CTX_free( pkcInfo->bnCTX );
		}
	if( pkcInfo->publicKeyInfo != NULL )
		clFree( "contextMessageFunction", pkcInfo->publicKeyInfo );
	}

/* Convert a byte string into a BIGNUM value */

int extractBignum( BIGNUM *bn, const void *buffer, const int length,
				   const int minLength, const int maxLength, 
				   const BIGNUM *maxRange )
	{
	BN_ULONG bnWord;

	assert( isWritePtr( bn, sizeof( BIGNUM ) ) );
	assert( isReadPtr( buffer, length ) );
	assert( minLength >= 1 && minLength <= maxLength && \
			maxLength <= CRYPT_MAX_PKCSIZE );
	assert( maxRange == NULL || isReadPtr( maxRange, sizeof( BIGNUM ) ) );

	/* Make sure that we've been given valid input.  This should have been 
	   checked by the caller anyway using far more specific checks than the
	   very generic values that we use here, but we perform the check anyway
	   just to be sure */
	if( length <= 0 || length > CRYPT_MAX_PKCSIZE )
		return( CRYPT_ERROR_BADDATA );

	/* Convert the byte string into a bignum */
	if( BN_bin2bn( buffer, length, bn ) == NULL )
		return( CRYPT_ERROR_MEMORY );

	/* The following should never happen because BN_bin2bn() works with 
	   unsigned values, but we perform the check anyway just in case
	   someone messes with the underlying bignum code */
	if( BN_is_negative( bn ) )
		retIntError();

	/* A zero- or one-valued bignum, on the other hand, is an error, since 
	   we should never find zero or one in a PKC-related value.  This check
	   is somewhat redundant with the one that follows, we place it here to
	   make it explicit (and because the cost is near zero) */
	bnWord = BN_get_word( bn );
	if( bnWord < BN_MASK2 && bnWord <= 1 )
		return( CRYPT_ERROR_BADDATA );

	/* Check that the final bignum value falls within the allowed length 
	   range.  We have to do this after the value has been processed 
	   otherwise it could be defeated via zero-padding */
	if( BN_num_bytes( bn ) < minLength || BN_num_bytes( bn ) > maxLength )
		return( CRYPT_ERROR_BADDATA );

	/* Finally, if the caller has supplied a maximum-range bignum value, 
	   make sure that the value that we've read is less than this */
	if( maxRange != NULL && BN_cmp( bn, maxRange ) >= 0 )
		return( CRYPT_ERROR_BADDATA );

	return( CRYPT_OK );
	}
#else

void clearTempBignums( PKC_INFO *pkcInfo )
	{
	}
void initContextBignums( PKC_INFO *pkcInfo, 
						 const BOOLEAN useSideChannelProtection )
	{
	}
void freeContextBignums( PKC_INFO *pkcInfo, int contextFlags )
	{
	}
#endif /* USE_PKC */

/****************************************************************************
*																			*
*							Self-test Support Functions						*
*																			*
****************************************************************************/

/* Statically initialised a context used for the internal self-test */

void staticInitContext( CONTEXT_INFO *contextInfoPtr, 
						const CONTEXT_TYPE type, 
						const CAPABILITY_INFO *capabilityInfoPtr,
						void *contextData, const int contextDataSize,
						void *keyData )
	{
	memset( contextInfoPtr, 0, sizeof( CONTEXT_INFO ) );
	memset( contextData, 0, contextDataSize );
	contextInfoPtr->type = type;
	contextInfoPtr->capabilityInfo = capabilityInfoPtr;
	switch( type )
		{
		case CONTEXT_CONV:
			contextInfoPtr->ctxConv = ( CONV_INFO * ) contextData;
			contextInfoPtr->ctxConv->key = keyData;
			break;

		case CONTEXT_HASH:
			contextInfoPtr->ctxHash = ( HASH_INFO * ) contextData;
			contextInfoPtr->ctxHash->hashInfo = keyData;
			break;

		case CONTEXT_MAC:
			contextInfoPtr->ctxMAC = ( MAC_INFO * ) contextData;
			contextInfoPtr->ctxMAC->macInfo = keyData;
			break;

		case CONTEXT_PKC:
			/* PKC context initialisation is a bit more complex because we
			   have to set up all of the bignum values as well */
			contextInfoPtr->ctxPKC = ( PKC_INFO * ) contextData;
			initContextBignums( contextData, 
								( capabilityInfoPtr->cryptAlgo == \
								  CRYPT_ALGO_RSA ) ? TRUE : FALSE );
			initKeyRead( contextInfoPtr );
			initKeyWrite( contextInfoPtr );		/* For calcKeyID() */
			break;

		default:
			assert( NOTREACHED );
		}
	}

void staticDestroyContext( CONTEXT_INFO *contextInfoPtr )
	{
	if( contextInfoPtr->type == CONTEXT_PKC )
		{
		freeContextBignums( contextInfoPtr->ctxPKC, 
					( contextInfoPtr->capabilityInfo->cryptAlgo == \
					  CRYPT_ALGO_RSA ) ? CONTEXT_SIDECHANNELPROTECTION : 0 );
		}
	memset( contextInfoPtr, 0, sizeof( CONTEXT_INFO ) );
	}

/* Perform a self-test of a cipher, encrypting and decrypting one block of 
   data and comparing it to a fixed test value */

int testCipher( const CAPABILITY_INFO *capabilityInfo, 
				void *keyDataStorage, const void *key, 
				const int keySize, const void *plaintext,
				const void *ciphertext )
	{
	CONTEXT_INFO contextInfo;
	CONV_INFO contextData;
	BYTE temp[ CRYPT_MAX_IVSIZE + 8 ];
	int status;

	assert( isReadPtr( capabilityInfo, sizeof( CAPABILITY_INFO ) ) );
	assert( isWritePtr( keyDataStorage, \
						capabilityInfo->getInfoFunction( CAPABILITY_INFO_STATESIZE,
														 NULL, 0 ) ) );
	assert( isReadPtr( key, keySize ) );
	assert( isReadPtr( plaintext, capabilityInfo->blockSize ) );
	assert( isReadPtr( ciphertext, capabilityInfo->blockSize ) );

	memcpy( temp, plaintext, capabilityInfo->blockSize );

	staticInitContext( &contextInfo, CONTEXT_CONV, capabilityInfo,
					   &contextData, sizeof( CONV_INFO ), 
					   keyDataStorage );
	status = capabilityInfo->initKeyFunction( &contextInfo, key, keySize );
	if( cryptStatusOK( status ) )
		status = capabilityInfo->encryptFunction( &contextInfo, temp, 
												  capabilityInfo->blockSize );
	if( cryptStatusOK( status ) && \
		memcmp( ciphertext, temp, capabilityInfo->blockSize ) )
		status = CRYPT_ERROR_FAILED;
	if( cryptStatusOK( status ) )
		status = capabilityInfo->decryptFunction( &contextInfo, temp, 
												  capabilityInfo->blockSize );
	staticDestroyContext( &contextInfo );
	if( cryptStatusError( status ) || \
		memcmp( plaintext, temp, capabilityInfo->blockSize ) )
		return( CRYPT_ERROR_FAILED );
	
	return( CRYPT_OK );
	}

/* Perform a self-test of a hash or MAC */

int testHash( const CAPABILITY_INFO *capabilityInfo, 
			  void *hashDataStorage, const void *data, const int dataLength,
			  const void *hashValue )
	{
	CONTEXT_INFO contextInfo;
	HASH_INFO contextData;
	int status = CRYPT_OK;

	assert( isReadPtr( capabilityInfo, sizeof( CAPABILITY_INFO ) ) );
	assert( isWritePtr( hashDataStorage, \
						capabilityInfo->getInfoFunction( CAPABILITY_INFO_STATESIZE,
														 NULL, 0 ) ) );
	assert( ( data == NULL && dataLength == 0 ) || \
			isReadPtr( data, dataLength ) );
	assert( isReadPtr( hashValue, capabilityInfo->blockSize ) );

	staticInitContext( &contextInfo, CONTEXT_HASH, capabilityInfo,
					   &contextData, sizeof( HASH_INFO ), hashDataStorage );
	if( cryptStatusOK( status ) && data != NULL )
		{
		/* Some of the test vector sets start out with empty strings, so we 
		   only call the hash function if we've actually been fed data to 
		   hash */
		status = capabilityInfo->encryptFunction( &contextInfo, 
												  ( void * ) data, 
												  dataLength );
		contextInfo.flags |= CONTEXT_HASH_INITED;
		}
	if( cryptStatusOK( status ) )
		status = capabilityInfo->encryptFunction( &contextInfo, NULL, 0 );
	if( cryptStatusOK( status ) && \
		memcmp( contextInfo.ctxHash->hash, hashValue, 
				capabilityInfo->blockSize ) )
		status = CRYPT_ERROR_FAILED;
	staticDestroyContext( &contextInfo );

	return( status );
	}

int testMAC( const CAPABILITY_INFO *capabilityInfo, 
			 void *macDataStorage, const void *key, 
			 const int keySize, const void *data, const int dataLength,
			 const void *hashValue )
	{
	CONTEXT_INFO contextInfo;
	MAC_INFO contextData;
	int status = CRYPT_OK;

	assert( isReadPtr( capabilityInfo, sizeof( CAPABILITY_INFO ) ) );
	assert( isWritePtr( macDataStorage, \
						capabilityInfo->getInfoFunction( CAPABILITY_INFO_STATESIZE,
														 NULL, 0 ) ) );
	assert( isReadPtr( key, keySize ) );
	assert( isReadPtr( data, dataLength ) );
	assert( isReadPtr( hashValue, capabilityInfo->blockSize ) );

	staticInitContext( &contextInfo, CONTEXT_MAC, capabilityInfo,
					   &contextData, sizeof( MAC_INFO ), macDataStorage );
	status = capabilityInfo->initKeyFunction( &contextInfo, key, keySize );
	if( cryptStatusOK( status ) )
		{
		status = capabilityInfo->encryptFunction( &contextInfo, 
												  ( void * ) data, 
												  dataLength );
		contextInfo.flags |= CONTEXT_HASH_INITED;
		}
	if( cryptStatusOK( status ) )
		status = capabilityInfo->encryptFunction( &contextInfo, NULL, 0 );
	if( cryptStatusOK( status ) && \
		memcmp( contextInfo.ctxMAC->mac, hashValue, 
				capabilityInfo->blockSize ) )
		status = CRYPT_ERROR_FAILED;
	staticDestroyContext( &contextInfo );

	return( status );
	}
