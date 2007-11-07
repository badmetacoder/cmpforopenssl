/****************************************************************************
*																			*
*						Public/Private Key Write Routines					*
*						Copyright Peter Gutmann 1992-2006					*
*																			*
****************************************************************************/

#include <stdio.h>
#define PKC_CONTEXT		/* Indicate that we're working with PKC context */
#if defined( INC_ALL )
  #include "context.h"
  #include "asn1.h"
  #include "asn1_ext.h"
  #include "misc_rw.h"
  #include "pgp.h"
#else
  #include "context/context.h"
  #include "misc/asn1.h"
  #include "misc/asn1_ext.h"
  #include "misc/misc_rw.h"
  #include "misc/pgp.h"
#endif /* Compiler-specific includes */

/* Although there is a fair amount of commonality between public and private-
   key functions, we keep them distinct to enforce red/black separation.

   The DLP algorithms split the key components over the information in the
   AlgorithmIdentifier and the actual public/private key components, with the
   (p, q, g) set classed as domain parameters and included in the
   AlgorithmIdentifier and y being the actual key.

	params = SEQ {
		p INTEGER,
		q INTEGER,
		g INTEGER,
		j INTEGER OPTIONAL,		-- X9.42 only
		validationParams [...]	-- X9.42 only
		}

	key = y INTEGER				-- g^x mod p

   For peculiar historical reasons (copying errors and the use of obsolete
   drafts as reference material) the X9.42 interpretation used in PKIX 
   reverses the second two parameters from FIPS 186 (so it uses p, g, q 
   instead of p, q, g), so when we read/write the parameter information we 
   have to switch the order in which we read the values if the algorithm 
   isn't DSA */

#define hasReversedParams( cryptAlgo ) \
		( ( cryptAlgo ) == CRYPT_ALGO_DH || \
		  ( cryptAlgo ) == CRYPT_ALGO_ELGAMAL )

#ifdef USE_PKC

/****************************************************************************
*																			*
*								Write Public Keys							*
*																			*
****************************************************************************/

/* Write X.509 SubjectPublicKeyInfo public keys */

static int writeRsaSubjectPublicKey( STREAM *stream, 
									 const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;
	const int length = sizeofBignum( &rsaKey->rsaParam_n ) + \
					   sizeofBignum( &rsaKey->rsaParam_e );

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	/* Write the SubjectPublicKeyInfo header field (the +1 is for the 
	   bitstring) */
	writeSequence( stream, sizeofAlgoID( CRYPT_ALGO_RSA ) + \
						   ( int ) sizeofObject( \
										sizeofObject( length ) + 1 ) );
	writeAlgoID( stream, CRYPT_ALGO_RSA );

	/* Write the BITSTRING wrapper and the PKC information */
	writeBitStringHole( stream, ( int ) sizeofObject( length ), 
						DEFAULT_TAG );
	writeSequence( stream, length );
	writeBignum( stream, &rsaKey->rsaParam_n );
	return( writeBignum( stream, &rsaKey->rsaParam_e ) );
	}

static int writeDlpSubjectPublicKey( STREAM *stream, 
									 const CONTEXT_INFO *contextInfoPtr )
	{
	const CRYPT_ALGO_TYPE cryptAlgo = contextInfoPtr->capabilityInfo->cryptAlgo;
	const PKC_INFO *dlpKey = contextInfoPtr->ctxPKC;
	const int parameterSize = ( int ) sizeofObject( \
								sizeofBignum( &dlpKey->dlpParam_p ) + \
								sizeofBignum( &dlpKey->dlpParam_q ) + \
								sizeofBignum( &dlpKey->dlpParam_g ) );
	const int componentSize = sizeofBignum( &dlpKey->dlpParam_y );
	int totalSize;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DSA || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ELGAMAL ) );

	/* Determine the size of the AlgorithmIdentifier and the BITSTRING-
	   encapsulated public-key data (the +1 is for the bitstring) */
	totalSize = sizeofAlgoIDex( cryptAlgo, CRYPT_ALGO_NONE, parameterSize ) + \
				( int ) sizeofObject( componentSize + 1 );

	/* Write the SubjectPublicKeyInfo header field */
	writeSequence( stream, totalSize );
	writeAlgoIDex( stream, cryptAlgo, CRYPT_ALGO_NONE, parameterSize );

	/* Write the parameter data */
	writeSequence( stream, sizeofBignum( &dlpKey->dlpParam_p ) + \
						   sizeofBignum( &dlpKey->dlpParam_q ) + \
						   sizeofBignum( &dlpKey->dlpParam_g ) );
	writeBignum( stream, &dlpKey->dlpParam_p );
	if( hasReversedParams( cryptAlgo ) )
		{
		writeBignum( stream, &dlpKey->dlpParam_g );
		if( BN_is_zero( &dlpKey->dlpParam_q ) )
			/* If it's an Elgamal key created by PGP, the q parameter
			   isn't present so we write it as a zero value.  We could also
			   omit it entirely, but it seems safer to write it as a non-
			   value than to (implicitly) change the ASN.1 structure of
			   the DLP parameters */
			writeShortInteger( stream, 0, DEFAULT_TAG );
		else
			writeBignum( stream, &dlpKey->dlpParam_q );
		}
	else
		{
		writeBignum( stream, &dlpKey->dlpParam_q );
		writeBignum( stream, &dlpKey->dlpParam_g );
		}

	/* Write the BITSTRING wrapper and the PKC information */
	writeBitStringHole( stream, componentSize, DEFAULT_TAG );
	return( writeBignum( stream, &dlpKey->dlpParam_y ) );
	}

#ifdef USE_ECC

static int writeEccSubjectPublicKey( STREAM *stream, 
									 const CONTEXT_INFO *contextInfoPtr )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ECDSA );
	
	return( CRYPT_ERROR_NOTAVAIL );
	}
#endif /* USE_ECC */

#ifdef USE_SSH1

/* Write SSH public keys */

static int writeSsh1RsaPublicKey( STREAM *stream, 
								  const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	writeUint32( stream, BN_num_bits( &rsaKey->rsaParam_n ) );
	writeBignumInteger16Ubits( stream, &rsaKey->rsaParam_e );
	return( writeBignumInteger16Ubits( stream, &rsaKey->rsaParam_n ) );
	}
#endif /* USE_SSH1 */

#ifdef USE_SSH

static int writeSshRsaPublicKey( STREAM *stream, 
								 const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	writeUint32( stream, sizeofString32( "ssh-rsa", 7 ) + \
						 sizeofBignumInteger32( &rsaKey->rsaParam_e ) + \
						 sizeofBignumInteger32( &rsaKey->rsaParam_n ) );
	writeString32( stream, "ssh-rsa", 7 );
	writeBignumInteger32( stream, &rsaKey->rsaParam_e );
	return( writeBignumInteger32( stream, &rsaKey->rsaParam_n ) );
	}

static int writeSshDlpPublicKey( STREAM *stream, 
								 const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *dsaKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DSA ) );

	/* SSHv2 uses PKCS #3 rather than X9.42-style DH keys, so we have to 
	   treat this algorithm type specially */
	if( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH )
		{
		writeUint32( stream, sizeofString32( "ssh-dh", 6 ) + \
							 sizeofBignumInteger32( &dsaKey->dlpParam_p ) + \
							 sizeofBignumInteger32( &dsaKey->dlpParam_g ) );
		writeString32( stream, "ssh-dh", 6 );
		writeBignumInteger32( stream, &dsaKey->dlpParam_p );
		return( writeBignumInteger32( stream, &dsaKey->dlpParam_g ) );
		}

	writeUint32( stream, sizeofString32( "ssh-dss", 7 ) + \
						 sizeofBignumInteger32( &dsaKey->dlpParam_p ) + \
						 sizeofBignumInteger32( &dsaKey->dlpParam_q ) + \
						 sizeofBignumInteger32( &dsaKey->dlpParam_g ) + \
						 sizeofBignumInteger32( &dsaKey->dlpParam_y ) );
	writeString32( stream, "ssh-dss", 7 );
	writeBignumInteger32( stream, &dsaKey->dlpParam_p );
	writeBignumInteger32( stream, &dsaKey->dlpParam_q );
	writeBignumInteger32( stream, &dsaKey->dlpParam_g );
	return( writeBignumInteger32( stream, &dsaKey->dlpParam_y ) );
	}
#endif /* USE_SSH */

#ifdef USE_SSL

/* Write SSL public keys */

static int writeSslDlpPublicKey( STREAM *stream, 
								 const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *dhKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH );

	writeBignumInteger16U( stream, &dhKey->dlpParam_p );
	return( writeBignumInteger16U( stream, &dhKey->dlpParam_g ) );
	}
#endif /* USE_SSL */

#ifdef USE_PGP

/* Write PGP public keys */

int writePgpRsaPublicKey( STREAM *stream, const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	sputc( stream, PGP_VERSION_OPENPGP );
	writeUint32Time( stream, rsaKey->pgpCreationTime );
	sputc( stream, PGP_ALGO_RSA );
	writeBignumInteger16Ubits( stream, &rsaKey->rsaParam_n );
	return( writeBignumInteger16Ubits( stream, &rsaKey->rsaParam_e ) );
	}

int writePgpDlpPublicKey( STREAM *stream, const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *dlpKey = contextInfoPtr->ctxPKC;
	const CRYPT_ALGO_TYPE cryptAlgo = contextInfoPtr->capabilityInfo->cryptAlgo;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DSA || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ELGAMAL ) );

	sputc( stream, PGP_VERSION_OPENPGP );
	writeUint32Time( stream, dlpKey->pgpCreationTime );
	sputc( stream, ( cryptAlgo == CRYPT_ALGO_DSA ) ? \
		   PGP_ALGO_DSA : PGP_ALGO_ELGAMAL );
	writeBignumInteger16Ubits( stream, &dlpKey->dlpParam_p );
	if( cryptAlgo == CRYPT_ALGO_DSA )
		writeBignumInteger16Ubits( stream, &dlpKey->dlpParam_q );
	writeBignumInteger16Ubits( stream, &dlpKey->dlpParam_g );
	return( writeBignumInteger16Ubits( stream, &dlpKey->dlpParam_y ) );
	}
#endif /* USE_PGP */

/* Umbrella public-key write functions */

static int writePublicKeyRsaFunction( STREAM *stream, 
									  const CONTEXT_INFO *contextInfoPtr,
									  const KEYFORMAT_TYPE formatType,
									  const char *accessKey )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );
	assert( formatType == KEYFORMAT_CERT || formatType == KEYFORMAT_SSH || \
			formatType == KEYFORMAT_SSH1 || formatType == KEYFORMAT_PGP );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "public" ) )
		retIntError();

	switch( formatType )
		{
		case KEYFORMAT_CERT:
			return( writeRsaSubjectPublicKey( stream, contextInfoPtr ) );

#ifdef USE_SSH
		case KEYFORMAT_SSH:
			return( writeSshRsaPublicKey( stream, contextInfoPtr ) );
#endif /* USE_SSH */

#ifdef USE_SSH1
		case KEYFORMAT_SSH1:
			return( writeSsh1RsaPublicKey( stream, contextInfoPtr ) );
#endif /* USE_SSH1 */

#ifdef USE_PGP
		case KEYFORMAT_PGP:
			return( writePgpRsaPublicKey( stream, contextInfoPtr ) );
#endif /* USE_PGP */
		}

	retIntError();
	}

static int writePublicKeyDlpFunction( STREAM *stream, 
									  const CONTEXT_INFO *contextInfoPtr,
									  const KEYFORMAT_TYPE formatType,
									  const char *accessKey )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DSA || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ELGAMAL ) );
	assert( formatType == KEYFORMAT_CERT || formatType == KEYFORMAT_SSH || \
			formatType == KEYFORMAT_SSL || formatType == KEYFORMAT_PGP );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "public" ) )
		retIntError();

	switch( formatType )
		{
		case KEYFORMAT_CERT:
			return( writeDlpSubjectPublicKey( stream, contextInfoPtr ) );

#ifdef USE_SSH
		case KEYFORMAT_SSH:
			return( writeSshDlpPublicKey( stream, contextInfoPtr ) );
#endif /* USE_SSH */

#ifdef USE_SSL
		case KEYFORMAT_SSL:
			return( writeSslDlpPublicKey( stream, contextInfoPtr ) );
#endif /* USE_SSL */

#ifdef USE_PGP
		case KEYFORMAT_PGP:
			return( writePgpDlpPublicKey( stream, contextInfoPtr ) );
#endif /* USE_PGP */
		}

	retIntError();
	}

#ifdef USE_ECC

static int writePublicKeyEccFunction( STREAM *stream, 
									  const CONTEXT_INFO *contextInfoPtr,
									  const KEYFORMAT_TYPE formatType,
									  const char *accessKey )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ECDSA );
	assert( formatType == KEYFORMAT_CERT );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "public" ) )
		retIntError();

	switch( formatType )
		{
		case KEYFORMAT_CERT:
			return( writeEccSubjectPublicKey( stream, contextInfoPtr ) );
		}

	retIntError();
	}
#endif /* USE_ECC */

/****************************************************************************
*																			*
*								Write Private Keys							*
*																			*
****************************************************************************/

/* Write private keys */

static int writeRsaPrivateKey( STREAM *stream, 
							   const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;
	int length = sizeofBignum( &rsaKey->rsaParam_p ) + \
				 sizeofBignum( &rsaKey->rsaParam_q );

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	/* Add the length of any optional components that may be present */
	if( !BN_is_zero( &rsaKey->rsaParam_exponent1 ) )
		length += sizeofBignum( &rsaKey->rsaParam_exponent1 ) + \
				  sizeofBignum( &rsaKey->rsaParam_exponent2 ) + \
				  sizeofBignum( &rsaKey->rsaParam_u );

	/* Write the the PKC fields */
	writeSequence( stream, length );
	writeBignumTag( stream, &rsaKey->rsaParam_p, 3 );
	if( BN_is_zero( &rsaKey->rsaParam_exponent1 ) )
		return( writeBignumTag( stream, &rsaKey->rsaParam_q, 4 ) );
	writeBignumTag( stream, &rsaKey->rsaParam_q, 4 );
	writeBignumTag( stream, &rsaKey->rsaParam_exponent1, 5 );
	writeBignumTag( stream, &rsaKey->rsaParam_exponent2, 6 );
	return( writeBignumTag( stream, &rsaKey->rsaParam_u, 7 ) );
	}

static int writeRsaPrivateKeyOld( STREAM *stream, 
								  const CONTEXT_INFO *contextInfoPtr )
	{
	const PKC_INFO *rsaKey = contextInfoPtr->ctxPKC;
	const int length = sizeofShortInteger( 0 ) + \
					   sizeofBignum( &rsaKey->rsaParam_n ) + \
					   sizeofBignum( &rsaKey->rsaParam_e ) + \
					   sizeofBignum( &rsaKey->rsaParam_d ) + \
					   sizeofBignum( &rsaKey->rsaParam_p ) + \
					   sizeofBignum( &rsaKey->rsaParam_q ) + \
					   sizeofBignum( &rsaKey->rsaParam_exponent1 ) + \
					   sizeofBignum( &rsaKey->rsaParam_exponent2 ) + \
					   sizeofBignum( &rsaKey->rsaParam_u );

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );

	/* The older format is somewhat restricted in terms of what can be
	   written since all components must be present, even the ones that are
	   never used.  If anything is missing, we can't write the key since
	   nothing would be able to read it */
	if( BN_is_zero( &rsaKey->rsaParam_n ) || \
		BN_is_zero( &rsaKey->rsaParam_d ) || \
		BN_is_zero( &rsaKey->rsaParam_exponent1 ) )
		return( CRYPT_ERROR_NOTAVAIL );

	/* Write the the PKC fields */
	writeSequence( stream, sizeofShortInteger( 0 ) + \
						   sizeofAlgoID( CRYPT_ALGO_RSA ) + \
						   ( int ) sizeofObject( \
										sizeofObject( length ) ) );
	writeShortInteger( stream, 0, DEFAULT_TAG );
	writeAlgoID( stream, CRYPT_ALGO_RSA );
	writeOctetStringHole( stream, ( int ) sizeofObject( length ), 
						  DEFAULT_TAG );
	writeSequence( stream, length );
	writeShortInteger( stream, 0, DEFAULT_TAG );
	writeBignum( stream, &rsaKey->rsaParam_n );
	writeBignum( stream, &rsaKey->rsaParam_e );
	writeBignum( stream, &rsaKey->rsaParam_d );
	writeBignum( stream, &rsaKey->rsaParam_p );
	writeBignum( stream, &rsaKey->rsaParam_q );
	writeBignum( stream, &rsaKey->rsaParam_exponent1 );
	writeBignum( stream, &rsaKey->rsaParam_exponent2 );
	return( writeBignum( stream, &rsaKey->rsaParam_u ) );
	}

/* Umbrella private-key write functions */

static int writePrivateKeyRsaFunction( STREAM *stream, 
									   const CONTEXT_INFO *contextInfoPtr,
									   const KEYFORMAT_TYPE formatType,
									   const char *accessKey )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_RSA );
	assert( formatType == KEYFORMAT_PRIVATE || \
			formatType == KEYFORMAT_PRIVATE_OLD );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "private" ) )
		retIntError();

	switch( formatType )
		{
		case KEYFORMAT_PRIVATE:
			return( writeRsaPrivateKey( stream, contextInfoPtr ) );

		case KEYFORMAT_PRIVATE_OLD:
			return( writeRsaPrivateKeyOld( stream, contextInfoPtr ) );
		}

	retIntError();
	}

static int writePrivateKeyDlpFunction( STREAM *stream, 
									   const CONTEXT_INFO *contextInfoPtr,
									   const KEYFORMAT_TYPE formatType,
									   const char *accessKey )
	{
	const PKC_INFO *dlpKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			( contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DH || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_DSA || \
			  contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ELGAMAL ) );
	assert( formatType == KEYFORMAT_PRIVATE );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "private" ) || formatType != KEYFORMAT_PRIVATE )
		retIntError();

	/* When we're generating a DH key ID, only p, q, and g are initialised,
	   so we write a special-case zero y value.  This is a somewhat ugly
	   side-effect of the odd way in which DH "public keys" work */
	if( BN_is_zero( &dlpKey->dlpParam_y ) )
		return( writeShortInteger( stream, 0, DEFAULT_TAG ) );

	/* Write the key components */
	return( writeBignum( stream, &dlpKey->dlpParam_x ) );
	}

#ifdef USE_ECC

static int writePrivateKeyEccFunction( STREAM *stream, 
									   const CONTEXT_INFO *contextInfoPtr,
									   const KEYFORMAT_TYPE formatType,
									   const char *accessKey )
	{
	const PKC_INFO *eccKey = contextInfoPtr->ctxPKC;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC && \
			contextInfoPtr->capabilityInfo->cryptAlgo == CRYPT_ALGO_ECDSA );
	assert( formatType == KEYFORMAT_PRIVATE );
	assert( isReadPtr( accessKey, 6 ) );

	/* Make sure that we really intended to call this function */
	if( strcmp( accessKey, "private" ) || formatType != KEYFORMAT_PRIVATE )
		retIntError();

	/* Write the key components */
	return( writeBignum( stream, &dlpKey->eccParam_x ) );
	}
#endif /* USE_ECC */

/****************************************************************************
*																			*
*							Write Flat Public Key Data						*
*																			*
****************************************************************************/

#ifdef USE_KEA

/* Generate KEA domain parameters from flat-format values */

static int generateDomainParameters( BYTE *domainParameters,
									 const void *p, const int pLength,
									 const void *q, const int qLength,
									 const void *g, const int gLength )
	{
	STREAM stream;
	BYTE hash[ CRYPT_MAX_HASHSIZE + 8 ];
	BYTE dataBuffer[ 16 + ( CRYPT_MAX_PKCSIZE * 3 ) + 8 ];
	HASHFUNCTION hashFunction;
	const int pSize = sizeofInteger( p, pLength );
	const int qSize = sizeofInteger( q, qLength );
	const int gSize = sizeofInteger( g, gLength );
	int hashSize, dataSize, i, status;

	assert( isWritePtr( domainParameters, CRYPT_MAX_HASHSIZE ) );
	assert( isReadPtr( p, pLength ) );
	assert( isReadPtr( q, qLength ) );
	assert( isReadPtr( g, gLength ) );

	/* Write the parameters to a stream.  The stream length is in case
	   KEA is at some point extended up to the max.allowed PKC size */
	sMemOpen( &stream, dataBuffer, 16 + ( CRYPT_MAX_PKCSIZE * 3 ) );
	writeSequence( &stream, pSize + qSize + gSize );
	writeInteger( &stream, p, pLength, DEFAULT_TAG );
	writeInteger( &stream, q, qLength, DEFAULT_TAG );
	status = writeInteger( &stream, g, gLength, DEFAULT_TAG );
	assert( cryptStatusOK( status ) );
	dataSize = stell( &stream );
	sMemDisconnect( &stream );

	/* Hash the DSA/KEA parameters and reduce them down to get the domain
	   identifier */
	getHashParameters( CRYPT_ALGO_SHA, &hashFunction, &hashSize );
	hashFunction( NULL, hash, hashSize, dataBuffer, dataSize, HASH_ALL );
	zeroise( dataBuffer, CRYPT_MAX_PKCSIZE * 3 );
	hashSize /= 2;	/* Output = hash result folded in half */
	for( i = 0; i < hashSize; i++ )
		domainParameters[ i ] = hash[ i ] ^ hash[ hashSize + i ];

	return( hashSize );
	}
#endif /* USE_KEA */

/* If the keys are stored in a crypto device rather than being held in the
   context, all we have available are the public components in flat format.
   The following code writes flat-format public components in the X.509
   SubjectPublicKeyInfo format.  The parameters are:

	Algo	Comp1	Comp2	Comp3	Comp4
	----	-----	-----	-----	-----
	RSA		  n		  e		  -		  -
	DLP		  p		  q		  g		  y */

int writeFlatPublicKey( void *buffer, const int bufMaxSize, 
						const CRYPT_ALGO_TYPE cryptAlgo, 
						const void *component1, const int component1Length,
						const void *component2, const int component2Length,
						const void *component3, const int component3Length,
						const void *component4, const int component4Length )
	{
	STREAM stream;
	const int comp1Size = sizeofInteger( component1, component1Length );
	const int comp2Size = sizeofInteger( component2, component2Length );
	const int comp3Size = ( component3 == NULL ) ? 0 : \
						  sizeofInteger( component3, component3Length );
	const int comp4Size = ( component4 == NULL ) ? 0 : \
						  sizeofInteger( component4, component4Length );
	const int parameterSize = ( cryptAlgo == CRYPT_ALGO_DH || \
								cryptAlgo == CRYPT_ALGO_DSA || \
								cryptAlgo == CRYPT_ALGO_ELGAMAL ) ? \
				( int ) sizeofObject( comp1Size + comp2Size + comp3Size ) : \
							  ( cryptAlgo == CRYPT_ALGO_KEA ) ? \
				( int) sizeofObject( 10 ) : 0;
	const int componentSize = ( cryptAlgo == CRYPT_ALGO_RSA ) ? \
				( int ) sizeofObject( comp1Size + comp2Size ) : \
							  ( cryptAlgo == CRYPT_ALGO_KEA ) ? \
				component4Length : comp4Size;
	int totalSize, status;

	assert( !isEccAlgo( cryptAlgo ) );
	assert( ( buffer == NULL && bufMaxSize == 0 ) || \
			isWritePtr( buffer, bufMaxSize ) );
	assert( isReadPtr( component1, component1Length ) );
	assert( isReadPtr( component2, component2Length ) );
	assert( comp3Size == 0 || isReadPtr( component3, component3Length ) );
	assert( comp4Size == 0 || isReadPtr( component4, component4Length ) );
	assert( cryptAlgo == CRYPT_ALGO_DH || cryptAlgo == CRYPT_ALGO_RSA || \
			cryptAlgo == CRYPT_ALGO_DSA || cryptAlgo == CRYPT_ALGO_ELGAMAL || \
			cryptAlgo == CRYPT_ALGO_KEA );

	/* Determine the size of the AlgorithmIdentifier and the BITSTRING-
	   encapsulated public-key data (the +1 is for the bitstring) */
	totalSize = sizeofAlgoIDex( cryptAlgo, CRYPT_ALGO_NONE, parameterSize ) + \
				( int ) sizeofObject( componentSize + 1 );
	if( buffer == NULL )
		{
		/* It's a size-check call via sizeofFlatPublicKey(), return the 
		   overall size */
		return( ( int ) sizeofObject( totalSize ) );
		}

	sMemOpen( &stream, buffer, bufMaxSize );

	/* Write the SubjectPublicKeyInfo header field */
	writeSequence( &stream, totalSize );
	writeAlgoIDex( &stream, cryptAlgo, CRYPT_ALGO_NONE, parameterSize );

	/* Write the parameter data if necessary */
	if( isDlpAlgo( cryptAlgo ) && cryptAlgo != CRYPT_ALGO_KEA )
		{
		writeSequence( &stream, comp1Size + comp2Size + comp3Size );
		writeInteger( &stream, component1, component1Length, DEFAULT_TAG );
		if( hasReversedParams( cryptAlgo ) )
			{
			writeInteger( &stream, component3, component3Length, DEFAULT_TAG );
			writeInteger( &stream, component2, component2Length, DEFAULT_TAG );
			}
		else
			{
			writeInteger( &stream, component2, component2Length, DEFAULT_TAG );
			writeInteger( &stream, component3, component3Length, DEFAULT_TAG );
			}
		}
#ifdef USE_KEA
	if( cryptAlgo == CRYPT_ALGO_KEA )
		{
		BYTE domainParameters[ 10 + 8 ];
		const int domainParameterLength = \
					generateDomainParameters( domainParameters,
											  component1, component1Length,
											  component2, component2Length,
											  component3, component3Length );

		writeOctetString( &stream, domainParameters, domainParameterLength,
						  DEFAULT_TAG );
		}
#endif /* USE_KEA */

	/* Write the BITSTRING wrapper and the PKC information */
	writeBitStringHole( &stream, componentSize, DEFAULT_TAG );
	if( cryptAlgo == CRYPT_ALGO_RSA )
		{
		writeSequence( &stream, comp1Size + comp2Size );
		writeInteger( &stream, component1, component1Length, DEFAULT_TAG );
		status = writeInteger( &stream, component2, component2Length, 
							   DEFAULT_TAG );
		}
	else
#ifdef USE_KEA
		if( cryptAlgo == CRYPT_ALGO_KEA )
			status = swrite( &stream, component4, component4Length );
		else
#endif /* USE_KEA */
			status = writeInteger( &stream, component4, component4Length, 
								   DEFAULT_TAG );

	/* Clean up */
	sMemDisconnect( &stream );
	return( status );
	}

int sizeofFlatPublicKey( const CRYPT_ALGO_TYPE cryptAlgo, 
						 const void *component1, const int component1Length,
						 const void *component2, const int component2Length,
						 const void *component3, const int component3Length,
						 const void *component4, const int component4Length )
	{
	return( writeFlatPublicKey( NULL, 0, cryptAlgo, 
								component1, component1Length,
								component2, component2Length,
								component3, component3Length,
								component4, component4Length ) );
	}

/****************************************************************************
*																			*
*								Write DL Values								*
*																			*
****************************************************************************/

/* Unlike the simpler RSA PKC, DL-based PKCs produce a pair of values that
   need to be encoded as structured data.  The following two functions 
   perform this en/decoding.  SSH assumes that DLP values are two fixed-size
   blocks of 20 bytes, so we can't use the normal read/write routines to 
   handle these values */

static int encodeDLValuesFunction( BYTE *buffer, const int bufSize, 
								   const BIGNUM *value1, const BIGNUM *value2, 
								   const CRYPT_FORMAT_TYPE formatType )
	{
	STREAM stream;
	int length, status;

	assert( isWritePtr( buffer, bufSize ) );
	assert( isReadPtr( value1, sizeof( BIGNUM ) ) );
	assert( isReadPtr( value2, sizeof( BIGNUM ) ) );
	assert( formatType == CRYPT_FORMAT_CRYPTLIB || \
			formatType == CRYPT_FORMAT_PGP || \
			formatType == CRYPT_IFORMAT_SSH );

	sMemOpen( &stream, buffer, bufSize );

	/* Write the DL components to the buffer */
	switch( formatType )
		{
		case CRYPT_FORMAT_CRYPTLIB:
			writeSequence( &stream, sizeofBignum( value1 ) + \
									sizeofBignum( value2 ) );
			writeBignum( &stream, value1 );
			status = writeBignum( &stream, value2 );
			break;

#ifdef USE_PGP
		case CRYPT_FORMAT_PGP:
			writeBignumInteger16Ubits( &stream, value1 );
			status = writeBignumInteger16Ubits( &stream, value2 );
			break;
#endif /* USE_PGP */

#ifdef USE_SSH
		case CRYPT_IFORMAT_SSH:
			/* SSH uses an awkward (and horribly inflexible) fixed format 
			   with each of the nominally 160-bit DLP values at fixed 
			   positions in a 2 x 20-byte buffer, so we zero-fill the entire 
			   buffer and then drop the encoded bignums into their fixed 
			   locations in the buffer */
			for( length = 0; length < 4; length++ )
				{
				status = swrite( &stream, \
							"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 10 );
				}
			if( cryptStatusError( status ) )
				break;
			length = BN_num_bytes( value1 );
			BN_bn2bin( value1, buffer + 20 - length );
			length = BN_num_bytes( value2 );
			BN_bn2bin( value2, buffer + 40 - length );
			break;
#endif /* USE_SSH */

		default:
			retIntError();
		}
	assert( cryptStatusOK( status ) );

	/* Clean up */
	length = stell( &stream );
	sMemDisconnect( &stream );
	return( cryptStatusOK( status ) ? length : status );
	}

/****************************************************************************
*																			*
*							Context Access Routines							*
*																			*
****************************************************************************/

void initKeyWrite( CONTEXT_INFO *contextInfoPtr )
	{
	PKC_INFO *pkcInfo = contextInfoPtr->ctxPKC;

	assert( isWritePtr( contextInfoPtr, sizeof( CONTEXT_INFO ) ) && \
			contextInfoPtr->type == CONTEXT_PKC );

	/* Set the access method pointers */
	if( isDlpAlgo( contextInfoPtr->capabilityInfo->cryptAlgo ) )
		{
		pkcInfo->writePublicKeyFunction = writePublicKeyDlpFunction;
		pkcInfo->writePrivateKeyFunction = writePrivateKeyDlpFunction;
		pkcInfo->encodeDLValuesFunction = encodeDLValuesFunction;
		}
	else
#ifdef USE_ECC
		if( isEccAlgo( contextInfoPtr->capabilityInfo->cryptAlgo ) )
			{
			pkcInfo->writePublicKeyFunction = writePublicKeyEccFunction;
			pkcInfo->writePrivateKeyFunction = writePrivateKeyEccFunction;
			}
		else
#endif /* USE_ECC */
			{
			pkcInfo->writePublicKeyFunction = writePublicKeyRsaFunction;
			pkcInfo->writePrivateKeyFunction = writePrivateKeyRsaFunction;
			}
	}
#else


void initKeyWrite( CONTEXT_INFO *contextInfoPtr )
	{
	}

#endif /* USE_PKC */
