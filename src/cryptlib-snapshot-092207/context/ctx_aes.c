/****************************************************************************
*																			*
*						cryptlib AES Encryption Routines					*
*						Copyright Peter Gutmann 2000-2007					*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "crypt.h"
  #include "context.h"
  #include "aes.h"
  #include "aesopt.h"
#else
  #include "crypt.h"
  #include "context/context.h"
  #include "crypt/aes.h"
  #include "crypt/aesopt.h"
#endif /* Compiler-specific includes */

/* The AES code separates encryption and decryption to make it easier to
   do encrypt-only or decrypt-only apps, however since we don't know
   what the user will choose to do we have to do both key schedules (this
   is a relatively minor overhead compared to en/decryption, so it's not a 
   big problem).

   When building with VC++, the asm code used is aescrypt2.asm, built with
   'yasm -Xvc -D ASMV2 -f win32 aescrypt2.asm', which provides the best
   performance by using asm for the en/decrypt functions and C for the
   key schedule */

#ifdef USE_AES

/* The size of an AES key and block and a keyscheduled AES key */

#define AES_KEYSIZE		32
#define AES_BLOCKSIZE	16
#define AES_EXPANDED_KEYSIZE sizeof( AES_CTX )

/* The scheduled AES key and key schedule control and function return 
   codes */

#define AES_EKEY	aes_encrypt_ctx
#define AES_DKEY	aes_decrypt_ctx
#define AES_2KEY	AES_CTX

#if 1

/* The following macros are from the AES implementation, and aren't cryptlib
   code.

   Assign memory for AES contexts in 'UNIT_SIZE' blocks of bytes with two 
   such blocks in cryptlib's AES context (one encryption and one decryption). 
   The cryptlib key schedule is then two AES contexts plus an extra UNIT_SIZE 
   block to allow for alignment adjustment by up to 'UNIT_SIZE' - 1 bytes to 
   align each of the internal AES contexts on UNIT_SIZE boundaries */

#define UNIT_SIZE	16

/* The size of the AES context rounded up (if necessary) to a multiple 16 bytes	*/

#define BYTE_SIZE( x )	( UNIT_SIZE * ( ( sizeof( x ) + UNIT_SIZE - 1 ) / UNIT_SIZE ) )

/* The size of the cryptlib AES context plus UNIT_SIZE bytes for possible upward 
   alignment to a UNIT_SIZE byte boundary */

#define KS_SIZE		( BYTE_SIZE( AES_EKEY ) + BYTE_SIZE( AES_DKEY ) + UNIT_SIZE )

/* The base address for the cryptlib AES context */

#define KS_BASE(x)	( ( unsigned char * )( ( ( AES_CTX * ) x )->ksch ) )

/* The AES encrypt context address rounded up (if necessary) to a 16 byte boundary */

#define EKEY( x )	( ( AES_EKEY * ) ALIGN_CEIL( KS_BASE( x ), UNIT_SIZE ) )

/* The AES decrypt context address rounded up (if necessary) to a 16 byte boundary */

#define DKEY( x )	( ( AES_DKEY * ) ALIGN_CEIL( KS_BASE( x ) + BYTE_SIZE( AES_EKEY ), UNIT_SIZE ) )

#else

/* The following macros are from the AES implementation, and aren't cryptlib
   code */

#if defined( __GNUC__ )
  #define ALIGNED_UCHAR( n )	unsigned char _al_##n __attribute__ (( aligned( n ) ))
#elif defined( _MSC_VER ) && ( _MSC_VER > 1200 )
  #define ALIGNED_UCHAR( n )	__declspec(align( n )) unsigned char _al_##n
#endif /* Compilers that support data alignment */

#ifdef ALIGNED_UCHAR
  extern ALIGNED_UCHAR( 16 );	/* 16-byte aligned variable */
  #define ADR_OFFSET( x, n )	( ( ( unsigned char * )( x ) - &_al_##n ) & ( ( n ) - 1 ) )
  #define ALIGN_DOWN( x )		( ( unsigned char * )( x ) - ADR_OFFSET( ( x ),16 ) )
#else
  /* Data is DWORD-aligned anyway but we need to have 16-byte alignment for
     key data in case we're using the VIA ACE.  The value -16 expands to
     0xFFFFFFF0 in 32-bit mode and 0xFFFFFFFFFFFFFFFF0 in 64-bit mode */
  #if defined( _MSC_VER ) && VC_LT_2005( _MSC_VER )
	#define ULONG_PTR		unsigned long
  #endif /* VC++ 6 or below */
  #define ALIGN_DOWN( x )		( ( ULONG_PTR )( x ) & -16 )
#endif /* Alignment macros */

/* The AES code assumes that objects are allocated and aligned for the 
   '_unit' type but don't assume what the length of a '_unit' is except that 
   it's not greater than 16 bytes */

typedef unsigned long _unit;

#define BY_SIZE( x )		( sizeof( _unit ) * ( ( sizeof( x ) + sizeof( _unit ) - 1 ) / sizeof( _unit ) ) )
#define KS_BASE( x )		( ( unsigned char * )( ( ( AES_CTX * ) x )->ksch ) )

#if defined( USE_VIA_ACE_IF_PRESENT )
  #define KS_SIZE			( BY_SIZE( AES_EKEY ) + BY_SIZE( AES_DKEY ) + 24 )
  #define EKEY( x )			( ( AES_EKEY * ) ALIGN_DOWN( KS_BASE( x ) + 12 ) )
  #define DKEY( x )			( ( AES_DKEY * ) ALIGN_DOWN( KS_BASE( x ) + BY_SIZE( AES_EKEY ) + 24 ) )
#else
  #define KS_SIZE			( BY_SIZE( AES_EKEY ) + BY_SIZE( AES_DKEY ) )
  #define EKEY( x )			( ( AES_EKEY * ) KS_BASE( x ) )
  #define DKEY( x )			( ( AES_DKEY * )( KS_BASE( x ) + BY_SIZE( AES_EKEY ) ) )
#endif /* USE_VIA_ACE_IF_PRESENT */

#endif /* 1 */

/* A type to hold the cryptlib AES context */

typedef unsigned long _unit;
typedef struct {	
	_unit ksch[ ( KS_SIZE + sizeof( _unit ) - 1 ) / sizeof( _unit ) ];
	} AES_CTX;

#define	ENC_KEY( x )		EKEY( ( x )->key )
#define	DEC_KEY( x )		DKEY( ( x )->key )

/****************************************************************************
*																			*
*								AES Self-test Routines						*
*																			*
****************************************************************************/

/* AES FIPS test vectors */

/* The data structure for the ( key, plaintext, ciphertext ) triplets */

typedef struct {
	const int keySize;
	const BYTE key[ AES_KEYSIZE + 8 ];
	const BYTE plaintext[ AES_BLOCKSIZE + 8 ];
	const BYTE ciphertext[ AES_BLOCKSIZE + 8 ];
	} AES_TEST;

static const AES_TEST FAR_BSS testAES[] = {
	{ 16,
	  { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F },
	  { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 
		0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
	  { 0x69, 0xC4, 0xE0, 0xD8, 0x6A, 0x7B, 0x04, 0x30, 
		0xD8, 0xCD, 0xB7, 0x80, 0x70, 0xB4, 0xC5, 0x5A } },
	{ 24,
	  { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17 },
	  { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 
		0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
	  { 0xDD, 0xA9, 0x7C, 0xA4, 0x86, 0x4C, 0xDF, 0xE0, 
		0x6E, 0xAF, 0x70, 0xA0, 0xEC, 0x0D, 0x71, 0x91 } },
	{ 32,
	  { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
		0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 
		0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F },
	  { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 
		0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
	  { 0x8E, 0xA2, 0xB7, 0xCA, 0x51, 0x67, 0x45, 0xBF, 
		0xEA, 0xFC, 0x49, 0x90, 0x4B, 0x49, 0x60, 0x89 } }
	};

#if 0

/* Test the AES code against the test vectors from the AES FIPS */

static void printVector( const char *description, const BYTE *data,
						 const int length )
	{
	int i;

	printf( "%s = ", description );
	for( i = 0; i < length; i++ )
		printf( "%02x", data[ i ] );
	putchar( '\n' );
	}

static int updateKey( BYTE *key, const int keySize,
					  CONTEXT_INFO *contextInfo, 
					  const CAPABILITY_INFO *capabilityInfo,
					  const BYTE *newKey1, const BYTE *newKey2 )
	{
	BYTE keyData[ AES_KEYSIZE + 8 ];
	int i;

	switch( keySize )
		{
		case 16:
			memcpy( keyData, newKey2, keySize );
			break;

		case 24:
			memcpy( keyData, newKey1 + 8, keySize );
			memcpy( keyData + 8, newKey2, AES_BLOCKSIZE );

		case 32:
			memcpy( keyData, newKey1, AES_BLOCKSIZE );
			memcpy( keyData + 16, newKey2, AES_BLOCKSIZE );
		}

	for( i = 0; i < keySize; i++ )
		key[ i ] ^= keyData[ i ];
	return( capabilityInfo->initKeyFunction( contextInfo, key, 
											 keySize ) );
	}

static int mct( CONTEXT_INFO *contextInfo, 
			    const CAPABILITY_INFO *capabilityInfo,
				const BYTE *initialKey, const int keySize,
				const BYTE *initialIV, const BYTE *initialPT )
	{
	BYTE key[ AES_KEYSIZE + 8 ], iv[ AES_KEYSIZE + 8 ];
	BYTE temp[ AES_BLOCKSIZE + 8 ];
	int i;

	memcpy( key, initialKey, keySize );
	if( iv != NULL )
		memcpy( iv, initialIV, AES_BLOCKSIZE );
	memcpy( temp, initialPT, AES_BLOCKSIZE );
	for( i = 0; i < 100; i++ )
		{
		BYTE prevTemp[ AES_BLOCKSIZE + 8 ];
		int j, status;

		status = capabilityInfo->initKeyFunction( contextInfo, key, 
												  keySize );
		if( cryptStatusError( status ) )
			return( status );
		printVector( "Key", key, keySize );
		if( iv != NULL )
			printVector( "IV", iv, AES_BLOCKSIZE );
		printVector( "Plaintext", temp, AES_BLOCKSIZE );
		if( iv != NULL )
			memcpy( contextInfo->ctxConv->currentIV, iv, AES_BLOCKSIZE );
		for( j = 0; j < 1000; j++ )
			{
/*			memcpy( prevTemp, temp, AES_BLOCKSIZE ); */
			if( iv != NULL && j == 0 )
				{
				status = capabilityInfo->encryptCBCFunction( contextInfo, temp, 
															 AES_BLOCKSIZE );
				memcpy( prevTemp, temp, AES_BLOCKSIZE );
				memcpy( temp, iv, AES_BLOCKSIZE );
				}
			else
				{
				status = capabilityInfo->encryptFunction( contextInfo, temp, 
														  AES_BLOCKSIZE );
				if( iv != NULL )
					{
					BYTE tmpTemp[ AES_BLOCKSIZE + 8 ];

					memcpy( tmpTemp, temp, AES_BLOCKSIZE );
					memcpy( temp, prevTemp, AES_BLOCKSIZE );
					memcpy( prevTemp, tmpTemp, AES_BLOCKSIZE );
					}
				}
			if( cryptStatusError( status ) )
				return( status );
			}
		printVector( "Ciphertext", temp, AES_BLOCKSIZE );
		putchar( '\n' );
		status = updateKey( key, keySize, contextInfo, capabilityInfo, 
							prevTemp, temp );
		if( cryptStatusError( status ) )
			return( status );
		}
	
	return( CRYPT_OK );
	}
#endif

static int selfTest( void )
	{
	/* ECB */
	static const BYTE FAR_BSS mctECBKey[] = \
		{ 0x8D, 0x2E, 0x60, 0x36, 0x5F, 0x17, 0xC7, 0xDF, 0x10, 0x40, 0xD7, 0x50, 0x1B, 0x4A, 0x7B, 0x5A };
	static const BYTE FAR_BSS mctECBPT[] = \
		{ 0x59, 0xB5, 0x08, 0x8E, 0x6D, 0xAD, 0xC3, 0xAD, 0x5F, 0x27, 0xA4, 0x60, 0x87, 0x2D, 0x59, 0x29 };
	/* CBC */
	static const BYTE FAR_BSS mctCBCKey[] = \
		{ 0x9D, 0xC2, 0xC8, 0x4A, 0x37, 0x85, 0x0C, 0x11, 0x69, 0x98, 0x18, 0x60, 0x5F, 0x47, 0x95, 0x8C };
	static const BYTE FAR_BSS mctCBCIV[] = \
		{ 0x25, 0x69, 0x53, 0xB2, 0xFE, 0xAB, 0x2A, 0x04, 0xAE, 0x01, 0x80, 0xD8, 0x33, 0x5B, 0xBE, 0xD6 };
	static const BYTE FAR_BSS mctCBCPT[] = \
		{ 0x2E, 0x58, 0x66, 0x92, 0xE6, 0x47, 0xF5, 0x02, 0x8E, 0xC6, 0xFA, 0x47, 0xA5, 0x5A, 0x2A, 0xAB };
	/* OFB */
	static const BYTE FAR_BSS mctOFBKey[] = \
		{ 0xB1, 0x1E, 0x4E, 0xCA, 0xE2, 0xE7, 0x1E, 0x14, 0x14, 0x5D, 0xD7, 0xDB, 0x26, 0x35, 0x65, 0x2F };
	static const BYTE FAR_BSS mctOFBIV[] = \
		{ 0xAD, 0xD3, 0x2B, 0xF8, 0x20, 0x4C, 0x33, 0x33, 0x9C, 0x54, 0xCD, 0x58, 0x58, 0xEE, 0x0D, 0x13 };
	static const BYTE FAR_BSS mctOFBPT[] = \
		{ 0x73, 0x20, 0x49, 0xE8, 0x9D, 0x74, 0xFC, 0xE7, 0xC5, 0xA4, 0x96, 0x64, 0x04, 0x86, 0x8F, 0xA6 };
	/* CFB-128 */
	static const BYTE FAR_BSS mctCFBKey[] = \
		{ 0x71, 0x15, 0x11, 0x93, 0x1A, 0x15, 0x62, 0xEA, 0x73, 0x29, 0x0A, 0x8B, 0x0A, 0x37, 0xA3, 0xB4 };
	static const BYTE FAR_BSS mctCFBIV[] = \
		{ 0x9D, 0xCE, 0x23, 0xFD, 0x2D, 0xF5, 0x36, 0x0F, 0x79, 0x9C, 0xF1, 0x79, 0x84, 0xE4, 0x7C, 0x8D };
	static const BYTE FAR_BSS mctCFBPT[] = \
		{ 0xF0, 0x66, 0xBE, 0x4B, 0xD6, 0x71, 0xEB, 0xC1, 0xC4, 0xCF, 0x3C, 0x00, 0x8E, 0xF2, 0xCF, 0x18 };
	const CAPABILITY_INFO *capabilityInfo = getAESCapability();
	BYTE keyData[ AES_EXPANDED_KEYSIZE + 8 ];
	int i, status;

	/* The AES code requires 16-byte alignment for data structures, before 
	   we try anything else we make sure that the compiler voodoo required
	   to handle this has worked */
	if( aes_test_alignment_detection( 16 ) != EXIT_SUCCESS  )
		return( CRYPT_ERROR_FAILED );

#if 1
	for( i = 0; i < sizeof( testAES ) / sizeof( AES_TEST ); i++ )
		{
		status = testCipher( capabilityInfo, keyData, testAES[ i ].key, 
							 testAES[ i ].keySize, testAES[ i ].plaintext,
							 testAES[ i ].ciphertext );
		if( cryptStatusError( status ) )
			return( status );
		}
#endif

#if 0	/* OK */
	staticInitContext( &contextInfo, CONTEXT_CONV, capabilityInfo,
					   &contextData, sizeof( CONV_INFO ), keyData );
	status = mct( &contextInfo, capabilityInfo, mctECBKey, 16, 
				  NULL, mctECBPT );
	staticDestroyContext( &contextInfo );
	if( cryptStatusError( status ) )
		return( CRYPT_ERROR_FAILED );
#endif
#if 0	/* OK */
	staticInitContext( &contextInfo, CONTEXT_CONV, capabilityInfo,
					   &contextData, sizeof( CONV_INFO ), keyData );
	status = mct( &contextInfo, capabilityInfo, mctCBCKey, 16, 
				  mctCBCIV, mctCBCPT );
	staticDestroyContext( &contextInfo );
	if( cryptStatusError( status ) )
		return( CRYPT_ERROR_FAILED );
#endif

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
		return( AES_EXPANDED_KEYSIZE );

	return( getDefaultInfo( type, varParam, constParam ) );
	}

/****************************************************************************
*																			*
*							AES En/Decryption Routines						*
*																			*
****************************************************************************/

/* Encrypt/decrypt data in ECB/CBC/CFB modes.  These are just basic wrappers
   for the AES code, which either calls down to the C/asm AES routines or
   uses hardware assist to perform the operation directly */

static int encryptECB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_ecb_encrypt( buffer, buffer, noBytes, 
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int decryptECB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_ecb_decrypt( buffer, buffer, noBytes, 
							   DEC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int encryptCBC( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_cbc_encrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int decryptCBC( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_cbc_decrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   DEC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int encryptCFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_cfb_encrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int decryptCFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_cfb_decrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int encryptOFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_ofb_encrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

static int decryptOFB( CONTEXT_INFO *contextInfoPtr, BYTE *buffer, 
					   int noBytes )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	return( ( aes_ofb_decrypt( buffer, buffer, noBytes, convInfo->currentIV,
							   ENC_KEY( convInfo ) ) == EXIT_SUCCESS ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

/****************************************************************************
*																			*
*							AES Key Management Routines						*
*																			*
****************************************************************************/

/* Key schedule an AES key */

static int initKey( CONTEXT_INFO *contextInfoPtr, const void *key, 
					const int keyLength )
	{
	CONV_INFO *convInfo = contextInfoPtr->ctxConv;

	/* Copy the key to internal storage */
	if( convInfo->userKey != key )
		memcpy( convInfo->userKey, key, keyLength );
	convInfo->userKeyLength = keyLength;

	/* Call the AES key schedule code */
	if( aes_encrypt_key( convInfo->userKey, keyLength, 
						 ENC_KEY( convInfo ) ) != EXIT_SUCCESS || \
		aes_decrypt_key( convInfo->userKey, keyLength, 
						 DEC_KEY( convInfo ) ) != EXIT_SUCCESS )
		return( CRYPT_ERROR_FAILED );
	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*						Capability Access Routines							*
*																			*
****************************************************************************/

static const CAPABILITY_INFO FAR_BSS capabilityInfo = {
	CRYPT_ALGO_AES, bitsToBytes( 128 ), "AES", 3,
	bitsToBytes( 128 ), bitsToBytes( 128 ), bitsToBytes( 256 ),
	selfTest, getInfo, NULL, initKeyParams, initKey, NULL,
	encryptECB, decryptECB, encryptCBC, decryptCBC,
	encryptCFB, decryptCFB, encryptOFB, decryptOFB
	};

const CAPABILITY_INFO *getAESCapability( void )
	{
	/* If we're not using compiler-generated tables, we have to manually
	   initialise the tables before we can use AES (this is only required
	   for old/broken compilers that aren't tough enough for the
	   preprocessor-based table calculations) */
#ifndef FIXED_TABLES
	gen_tabs();
#endif /* FIXED_TABLES */

	return( &capabilityInfo );
	}
#endif /* USE_AES */
