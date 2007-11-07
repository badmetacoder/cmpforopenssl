/****************************************************************************
*																			*
*						cryptlib RC2 Encryption Routines					*
*						Copyright Peter Gutmann 1996-2005					*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "crypt.h"
  #include "context.h"
  #include "rc2.h"
#else
  #include "crypt.h"
  #include "context/context.h"
  #include "crypt/rc2.h"
#endif /* Compiler-specific includes */

#ifdef USE_RC2

/* Defines to map from EAY to native naming */

#define RC2_BLOCKSIZE				RC2_BLOCK
#define RC2_EXPANDED_KEYSIZE		sizeof( RC2_KEY )

/* The RC2 key schedule provides a mechanism for reducing the effective key
   size for export-control purposes, typically used to create 40-bit
   espionage-enabled keys.  BSAFE always sets the bitcount to the actual
   key size (so for example for a 128-bit key it first expands it up to 1024
   bits and then folds it back down again to 128 bits).  Because this scheme
   was copied by early S/MIME implementations (which were just BSAFE
   wrappers), it's become a part of CMS/SMIME so we use it here even though
   other sources do it differently */

#define effectiveKeysizeBits( keySize )		bytesToBits( keySize )

/****************************************************************************
*																			*
*								RC2 Self-test Routines						*
*																			*
****************************************************************************/

/* RC2 test vectors from RFC 2268 */

static const struct RC2_TEST {
	const BYTE key[ 16 ];
	const BYTE plaintext[ 8 ];
	const BYTE ciphertext[ 8 ];
	} FAR_BSS testRC2[] = {
	{ { 0x88, 0xBC, 0xA9, 0x0E, 0x90, 0x87, 0x5A, 0x7F,
		0x0F, 0x79, 0xC3, 0x84, 0x62, 0x7B, 0xAF, 0xB2 },
	  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	  { 0x22, 0x69, 0x55, 0x2A, 0xB0, 0xF8, 0x5C, 0xA6 } }
	};

/* Test the RC2 code against the RC2 test vectors */

static int selfTest( void )
	{
	const CAPABILITY_INFO *capabilityInfo = getRC2Capability();
	BYTE keyData[ RC2_EXPANDED_KEYSIZE + 8 ];
	int i, status;

	for( i = 0; i < sizeof( testRC2 ) / sizeof( struct RC2_TEST ); i++ )
		{
		status = testCipher( capabilityInfo, keyData, testRC2[ i ].key, 
							 16, testRC2[ i ].plaintext, 
							 testRC2[ i ].ciphertext );
		if( cryptStatusError( status ) )
			return( status );
		}

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*								Control Routines							*
*																			*
****************************************************************************/

/* Return context subtype-specific information */

static int getInfo( const CAPABILITY_INFO_TYPE type, void *varParam,
					const int constParam )
	{
	if( type == CAPABILITY_INFO_STATESIZE )
		return( RC2_EXPANDED_KEYSIZE );

	return( getDefaultInfo( type, varParam, constParam ) );
	}

/****************************************************************************
*																			*
*							RC2 En/Decryption Routines						*
*																			*
****************************************************************************/

/* Encrypt/decrypt data in ECB mode */

static int encryptECB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	int blockCount = noBytes / RC2_BLOCKSIZE;

	while( blockCount-- > 0 )
		{
		/* Encrypt a block of data */
		RC2_ecb_encrypt( buffer, buffer, rc2Key, RC2_ENCRYPT );

		/* Move on to next block of data */
		buffer += RC2_BLOCKSIZE;
		}

	return( CRYPT_OK );
	}

static int decryptECB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	int blockCount = noBytes / RC2_BLOCKSIZE;

	while( blockCount-- > 0 )
		{
		/* Decrypt a block of data */
		RC2_ecb_encrypt( buffer, buffer, rc2Key, RC2_DECRYPT );

		/* Move on to next block of data */
		buffer += RC2_BLOCKSIZE;
		}

	return( CRYPT_OK );
	}

/* Encrypt/decrypt data in CBC mode */

static int encryptCBC( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	/* Encrypt the buffer of data */
	RC2_cbc_encrypt( buffer, buffer, noBytes, ( RC2_KEY * ) convInfo->key,
					 convInfo->currentIV, RC2_ENCRYPT );

	return( CRYPT_OK );
	}

static int decryptCBC( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	/* Decrypt the buffer of data */
	RC2_cbc_encrypt( buffer, buffer, noBytes, ( RC2_KEY * ) convInfo->key,
					 convInfo->currentIV, RC2_DECRYPT );

	return( CRYPT_OK );
	}

/* Encrypt/decrypt data in CFB mode */

static int encryptCFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	int i, ivCount = convInfo->ivCount;

	/* If there's any encrypted material left in the IV, use it now */
	if( ivCount > 0 )
		{
		int bytesToUse;

		/* Find out how much material left in the encrypted IV we can use */
		bytesToUse = RC2_BLOCKSIZE - ivCount;
		if( noBytes < bytesToUse )
			bytesToUse = noBytes;

		/* Encrypt the data */
		for( i = 0; i < bytesToUse; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i + ivCount ];
		memcpy( convInfo->currentIV + ivCount, buffer, bytesToUse );

		/* Adjust the byte count and buffer position */
		noBytes -= bytesToUse;
		buffer += bytesToUse;
		ivCount += bytesToUse;
		}

	while( noBytes > 0 )
		{
		ivCount = ( noBytes > RC2_BLOCKSIZE ) ? RC2_BLOCKSIZE : noBytes;

		/* Encrypt the IV */
		RC2_ecb_encrypt( convInfo->currentIV, convInfo->currentIV, rc2Key,
						 RC2_ENCRYPT );

		/* XOR the buffer contents with the encrypted IV */
		for( i = 0; i < ivCount; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i ];

		/* Shift the ciphertext into the IV */
		memcpy( convInfo->currentIV, buffer, ivCount );

		/* Move on to next block of data */
		noBytes -= ivCount;
		buffer += ivCount;
		}

	/* Remember how much of the IV is still available for use */
	convInfo->ivCount = ( ivCount % RC2_BLOCKSIZE );

	return( CRYPT_OK );
	}

/* Decrypt data in CFB mode.  Note that the transformation can be made
   faster (but less clear) with temp = buffer, buffer ^= iv, iv = temp
   all in one loop */

static int decryptCFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	BYTE temp[ RC2_BLOCKSIZE + 8 ];
	int i, ivCount = convInfo->ivCount;

	/* If there's any encrypted material left in the IV, use it now */
	if( ivCount > 0 )
		{
		int bytesToUse;

		/* Find out how much material left in the encrypted IV we can use */
		bytesToUse = RC2_BLOCKSIZE - ivCount;
		if( noBytes < bytesToUse )
			bytesToUse = noBytes;

		/* Decrypt the data */
		memcpy( temp, buffer, bytesToUse );
		for( i = 0; i < bytesToUse; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i + ivCount ];
		memcpy( convInfo->currentIV + ivCount, temp, bytesToUse );

		/* Adjust the byte count and buffer position */
		noBytes -= bytesToUse;
		buffer += bytesToUse;
		ivCount += bytesToUse;
		}

	while( noBytes > 0 )
		{
		ivCount = ( noBytes > RC2_BLOCKSIZE ) ? RC2_BLOCKSIZE : noBytes;

		/* Encrypt the IV */
		RC2_ecb_encrypt( convInfo->currentIV, convInfo->currentIV, rc2Key,
						 RC2_ENCRYPT );

		/* Save the ciphertext */
		memcpy( temp, buffer, ivCount );

		/* XOR the buffer contents with the encrypted IV */
		for( i = 0; i < ivCount; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i ];

		/* Shift the ciphertext into the IV */
		memcpy( convInfo->currentIV, temp, ivCount );

		/* Move on to next block of data */
		noBytes -= ivCount;
		buffer += ivCount;
		}

	/* Remember how much of the IV is still available for use */
	convInfo->ivCount = ( ivCount % RC2_BLOCKSIZE );

	/* Clear the temporary buffer */
	zeroise( temp, RC2_BLOCKSIZE );

	return( CRYPT_OK );
	}

/* Encrypt/decrypt data in OFB mode */

static int encryptOFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	int i, ivCount = convInfo->ivCount;

	/* If there's any encrypted material left in the IV, use it now */
	if( ivCount > 0 )
		{
		int bytesToUse;

		/* Find out how much material left in the encrypted IV we can use */
		bytesToUse = RC2_BLOCKSIZE - ivCount;
		if( noBytes < bytesToUse )
			bytesToUse = noBytes;

		/* Encrypt the data */
		for( i = 0; i < bytesToUse; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i + ivCount ];

		/* Adjust the byte count and buffer position */
		noBytes -= bytesToUse;
		buffer += bytesToUse;
		ivCount += bytesToUse;
		}

	while( noBytes > 0 )
		{
		ivCount = ( noBytes > RC2_BLOCKSIZE ) ? RC2_BLOCKSIZE : noBytes;

		/* Encrypt the IV */
		RC2_ecb_encrypt( convInfo->currentIV, convInfo->currentIV, rc2Key,
						 RC2_ENCRYPT );

		/* XOR the buffer contents with the encrypted IV */
		for( i = 0; i < ivCount; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i ];

		/* Move on to next block of data */
		noBytes -= ivCount;
		buffer += ivCount;
		}

	/* Remember how much of the IV is still available for use */
	convInfo->ivCount = ( ivCount % RC2_BLOCKSIZE );

	return( CRYPT_OK );
	}

/* Decrypt data in OFB mode */

static int decryptOFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer,
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;
	int i, ivCount = convInfo->ivCount;

	/* If there's any encrypted material left in the IV, use it now */
	if( ivCount > 0 )
		{
		int bytesToUse;

		/* Find out how much material left in the encrypted IV we can use */
		bytesToUse = RC2_BLOCKSIZE - ivCount;
		if( noBytes < bytesToUse )
			bytesToUse = noBytes;

		/* Decrypt the data */
		for( i = 0; i < bytesToUse; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i + ivCount ];

		/* Adjust the byte count and buffer position */
		noBytes -= bytesToUse;
		buffer += bytesToUse;
		ivCount += bytesToUse;
		}

	while( noBytes > 0 )
		{
		ivCount = ( noBytes > RC2_BLOCKSIZE ) ? RC2_BLOCKSIZE : noBytes;

		/* Encrypt the IV */
		RC2_ecb_encrypt( convInfo->currentIV, convInfo->currentIV, rc2Key,
						 RC2_ENCRYPT );

		/* XOR the buffer contents with the encrypted IV */
		for( i = 0; i < ivCount; i++ )
			buffer[ i ] ^= convInfo->currentIV[ i ];

		/* Move on to next block of data */
		noBytes -= ivCount;
		buffer += ivCount;
		}

	/* Remember how much of the IV is still available for use */
	convInfo->ivCount = ( ivCount % RC2_BLOCKSIZE );

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							RC2 Key Management Routines						*
*																			*
****************************************************************************/

/* Key schedule an RC2 key */

static int initKey( CONTEXT_INFO *contextInfoPtr, const void *key,
					const int keyLength )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;
	RC2_KEY *rc2Key = ( RC2_KEY * ) convInfo->key;

	/* Copy the key to internal storage */
	if( convInfo->userKey != key )
		memcpy( convInfo->userKey, key, keyLength );
	convInfo->userKeyLength = keyLength;

	RC2_set_key( rc2Key, keyLength, key, effectiveKeysizeBits( keyLength ) );
	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*						Capability Access Routines							*
*																			*
****************************************************************************/

static const CAPABILITY_INFO FAR_BSS capabilityInfo = {
	CRYPT_ALGO_RC2, bitsToBytes( 64 ), "RC2", 3,
	MIN_KEYSIZE, bitsToBytes( 128 ), bitsToBytes( 1024 ),
	selfTest, getInfo, NULL, initKeyParams, initKey, NULL,
	encryptECB, decryptECB, encryptCBC, decryptCBC,
	encryptCFB, decryptCFB, encryptOFB, decryptOFB
	};

const CAPABILITY_INFO *getRC2Capability( void )
	{
	return( &capabilityInfo );
	}

#endif /* USE_RC2 */
