/****************************************************************************
*																			*
*				cryptlib RSA Key Generation/Checking Routines				*
*						Copyright Peter Gutmann 1997-2006					*
*																			*
****************************************************************************/

#define PKC_CONTEXT		/* Indicate that we're working with PKC context */
#if defined( INC_ALL )
  #include "crypt.h"
  #include "context.h"
  #include "keygen.h"
#else
  #include "crypt.h"
  #include "context/context.h"
  #include "context/keygen.h"
#endif /* Compiler-specific includes */

/* We use F4 as the default public exponent e unless the user chooses to
   override this with some other value:

	Fn = 2^(2^n) + 1, n = 0...4.

	F0 = 3, F1 = 5, F2 = 17, F3 = 257, F4 = 65537.
   
   The older (X.509v1) recommended value of 3 is insecure for general use 
   and more recent work indicates that values like 17 (used by PGP) are also 
   insecure against the Hastad attack.  We could work around this by using 
   41 or 257 as the exponent, however current best practice favours F4 
   unless you're doing banking standards, in which case you set e=2 (EMV) 
   and use raw, unpadded RSA (HBCI) to make it easier for students to break 
   your banking security as a homework exercise.

   Since some systems may be using 16-bit bignum component values, we use an
   exponent of 257 for these cases to ensure that it fits in a single
   component value */

#ifndef RSA_PUBLIC_EXPONENT
  #ifdef SIXTEEN_BIT
	#define RSA_PUBLIC_EXPONENT		257
  #else
	#define RSA_PUBLIC_EXPONENT		65537L
  #endif /* 16-bit bignum components */
#endif /* RSA_PUBLIC_EXPONENT */

/* The minimum allowed public exponent.  In theory this could go as low as 3,
   however there are all manner of obscure corner cases that have to be 
   checked if this exponent is used, and in general the necessary checking 
   presents a more or less intractable problem.  To avoid this minefield, we
   require a minimum exponent of at 17, the next generally-used value above 
   3.  However even this is only used by PGP 2.x, the next minimum is 33 (a
   weird value used by OpenSSH, see the comment further down), and then 257
   or (in practice) 65537 by everything else */

#if defined( USE_PGP )
  #define MIN_PUBLIC_EXPONENT		17
#elif defined( USE_SSH )
  #define MIN_PUBLIC_EXPONENT		33
#else
  #define MIN_PUBLIC_EXPONENT		257
#endif /* Smallest exponents used by various crypto protocols */

/****************************************************************************
*																			*
*							Utility Functions								*
*																			*
****************************************************************************/

/* Enable various side-channel protection mechanisms */

static int enableSidechannelProtection( PKC_INFO *pkcInfo, 
										const BOOLEAN isPrivateKey )
	{
	BIGNUM *n = &pkcInfo->rsaParam_n, *e = &pkcInfo->rsaParam_e;
	BIGNUM *k = &pkcInfo->rsaParam_blind_k;
	BIGNUM *kInv = &pkcInfo->rsaParam_blind_kInv;
	MESSAGE_DATA msgData;
	BYTE buffer[ CRYPT_MAX_PKCSIZE + 8 ];
	int noBytes = bitsToBytes( pkcInfo->keySizeBits );
	int bnStatus = BN_STATUS, status;

	/* Generate a random bignum for blinding.  Since this merely has to be 
	   unpredictable to an outsider but not cryptographically strong, and to 
	   avoid having more crypto RNG output than necessary sitting around in 
	   memory, we get it from the nonce PRNG rather than the crypto one.  In
	   addition we don't have to perform a range check on import to see if 
	   it's larger than 'n' since we're about to reduce it mod n in the next 
	   step, and doing so would give false positives */
	setMessageData( &msgData, buffer, noBytes );
	status = krnlSendMessage( SYSTEM_OBJECT_HANDLE, IMESSAGE_GETATTRIBUTE_S,
							  &msgData, CRYPT_IATTRIBUTE_RANDOM_NONCE );
	if( cryptStatusOK( status ) )
		{
		buffer[ 0 ] &= 255 >> ( -pkcInfo->keySizeBits & 7 );
		status = extractBignum( k, buffer, noBytes, MIN_PKCSIZE - 8, 
								CRYPT_MAX_PKCSIZE, NULL );
		}
	zeroise( buffer, noBytes );
	if( cryptStatusError( status ) )
		return( status );

	/* Set up the blinding and unblinding values */
	CK( BN_mod( k, k, n, pkcInfo->bnCTX ) );	/* k = rand() mod n */
	CKPTR( BN_mod_inverse( kInv, k, n, pkcInfo->bnCTX ) );
												/* kInv = k^-1 mod n */
	CK( BN_mod_exp_mont( k, k, e, n, pkcInfo->bnCTX,
						 &pkcInfo->rsaParam_mont_n ) );
												/* k = k^e mod n */
	if( bnStatusError( bnStatus ) )
		return( getBnStatus( bnStatus ) );

	/* Use constant-time modexp() to protect the private key from timing 
	   channels if required */
	if( isPrivateKey )
		{
		BN_set_flags( &pkcInfo->rsaParam_exponent1, BN_FLG_EXP_CONSTTIME );
		BN_set_flags( &pkcInfo->rsaParam_exponent2, BN_FLG_EXP_CONSTTIME );
		}

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Generate an RSA Key								*
*																			*
****************************************************************************/

/* Adjust p and q if necessary to ensure that the CRT decrypt works */

static int fixCRTvalues( PKC_INFO *pkcInfo, const BOOLEAN fixPKCSvalues )
	{
	BIGNUM *p = &pkcInfo->rsaParam_p, *q = &pkcInfo->rsaParam_q;

	/* Make sure that p > q, which is required for the CRT decrypt */
	if( BN_cmp( p, q ) >= 0 )
		return( CRYPT_OK );

	/* Swap the values p and q and, if necessary, the PKCS parameters e1
	   and e2 that depend on them (e1 = d mod (p - 1) and
	   e2 = d mod (q - 1)), and recompute u = qInv mod p */
	BN_swap( p, q );
	if( !fixPKCSvalues )
		return( CRYPT_OK );
	BN_swap( &pkcInfo->rsaParam_exponent1, &pkcInfo->rsaParam_exponent2 );
	return( BN_mod_inverse( &pkcInfo->rsaParam_u, q, p,
							pkcInfo->bnCTX ) != NULL ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

/* Evaluate the Montgomery forms for public and private components */

static int getRSAMontgomery( PKC_INFO *pkcInfo, const BOOLEAN isPrivateKey )
	{
	/* Evaluate the public value */
	if( !BN_MONT_CTX_set( &pkcInfo->rsaParam_mont_n, &pkcInfo->rsaParam_n,
						  pkcInfo->bnCTX ) )
		return( CRYPT_ERROR_FAILED );
	if( !isPrivateKey )
		return( CRYPT_OK );

	/* Evaluate the private values */
	return( BN_MONT_CTX_set( &pkcInfo->rsaParam_mont_p, &pkcInfo->rsaParam_p,
							 pkcInfo->bnCTX ) && \
			BN_MONT_CTX_set( &pkcInfo->rsaParam_mont_q, &pkcInfo->rsaParam_q,
							 pkcInfo->bnCTX ) ? \
			CRYPT_OK : CRYPT_ERROR_FAILED );
	}

/* Generate an RSA key pair into an encryption context */

int generateRSAkey( CONTEXT_INFO *contextInfoPtr, const int keySizeBits )
	{
	PKC_INFO *pkcInfo = contextInfoPtr->ctxPKC;
	BIGNUM *d = &pkcInfo->rsaParam_d, *p = &pkcInfo->rsaParam_p;
	BIGNUM *q = &pkcInfo->rsaParam_q;
	BIGNUM *tmp = &pkcInfo->tmp1;
	int pBits, qBits, bnStatus = BN_STATUS, status;

	/* Determine how many bits to give to each of p and q */
	pBits = ( keySizeBits + 1 ) / 2;
	qBits = keySizeBits - pBits;
	pkcInfo->keySizeBits = pBits + qBits;

	/* Generate the primes p and q and set them up so that the CRT decrypt
	   will work */
	BN_set_word( &pkcInfo->rsaParam_e, RSA_PUBLIC_EXPONENT );
	status = generatePrime( pkcInfo, p, pBits, RSA_PUBLIC_EXPONENT,
							contextInfoPtr );
	if( cryptStatusOK( status ) )
		status = generatePrime( pkcInfo, q, qBits, RSA_PUBLIC_EXPONENT,
								contextInfoPtr );
	if( cryptStatusOK( status ) )
		status = fixCRTvalues( pkcInfo, FALSE );
	if( cryptStatusError( status ) )
		return( status );

	/* Compute:

		d = eInv mod (p - 1)(q - 1)
		e1 = d mod (p - 1)
		e2 = d mod (q - 1) */
	CK( BN_sub_word( p, 1 ) );
	CK( BN_sub_word( q, 1 ) );
	CK( BN_mul( tmp, p, q, pkcInfo->bnCTX ) );
	CKPTR( BN_mod_inverse( d, &pkcInfo->rsaParam_e, tmp, pkcInfo->bnCTX ) );
	CK( BN_mod( &pkcInfo->rsaParam_exponent1, d,
				p, pkcInfo->bnCTX ) );
	CK( BN_mod( &pkcInfo->rsaParam_exponent2, d, q, pkcInfo->bnCTX ) );
	CK( BN_add_word( p, 1 ) );
	CK( BN_add_word( q, 1 ) );
	if( bnStatusError( bnStatus ) )
		return( getBnStatus( bnStatus ) );

	/* Compute:

		n = pq
		u = qInv mod p */
	CK( BN_mul( &pkcInfo->rsaParam_n, p, q, pkcInfo->bnCTX ) );
	CKPTR( BN_mod_inverse( &pkcInfo->rsaParam_u, q, p, pkcInfo->bnCTX ) );
	if( bnStatusError( bnStatus ) )
		return( getBnStatus( bnStatus ) );

	/* Evaluate the Montgomery forms */
	status = getRSAMontgomery( pkcInfo, TRUE );
	if( cryptStatusError( status ) )
		return( status );

	/* Enable side-channel protection if required */
	if( contextInfoPtr->flags & CONTEXT_SIDECHANNELPROTECTION )
		status = enableSidechannelProtection( pkcInfo, TRUE );
	return( status );
	}

/****************************************************************************
*																			*
*							Initialise/Check an RSA Key						*
*																			*
****************************************************************************/

/* Perform validity checks on the private key.  We have to make the PKC_INFO
   data non-const because the bignum code wants to modify some of the values
   as it's working with them */

static BOOLEAN checkRSAPrivateKeyComponents( PKC_INFO *pkcInfo )
	{
	BIGNUM *n = &pkcInfo->rsaParam_n, *e = &pkcInfo->rsaParam_e;
	BIGNUM *d = &pkcInfo->rsaParam_d, *p = &pkcInfo->rsaParam_p;
	BIGNUM *q = &pkcInfo->rsaParam_q;
	BIGNUM *p1 = &pkcInfo->tmp1, *q1 = &pkcInfo->tmp2, *tmp = &pkcInfo->tmp3;
	const BN_ULONG eWord = BN_get_word( e );
	int bnStatus = BN_STATUS;

	/* Calculate p - 1, q - 1 */
	CKPTR( BN_copy( p1, p ) );
	CK( BN_sub_word( p1, 1 ) );
	CKPTR( BN_copy( q1, q ) );
	CK( BN_sub_word( q1, 1 ) );
	if( bnStatusError( bnStatus ) )
		return( FALSE );

	/* Verify that:
	
		n = p * q */
	CK( BN_mul( tmp, p, q, pkcInfo->bnCTX ) );
	if( bnStatusError( bnStatus ) || BN_cmp( n, tmp ) != 0 )
		return( FALSE );

	/* Verify that:

		p, q < d
		( d * e ) mod p-1 == 1 
		( d * e ) mod q-1 == 1
	
	   Some implementations don't store d since it's not needed when the CRT
	   shortcut is used, so we can only perform this check if d is present */
	if( !BN_is_zero( d ) )
		{
		if( BN_cmp( p, d ) >= 0 )
			return( FALSE );
		if( BN_cmp( q, d ) >= 0 )
			return( FALSE );
		CK( BN_mod_mul( tmp, d, e, p1, pkcInfo->bnCTX ) );
		if( bnStatusError( bnStatus ) || !BN_is_one( tmp ) )
			return( FALSE );
		CK( BN_mod_mul( tmp, d, e, q1, pkcInfo->bnCTX ) );
		if( bnStatusError( bnStatus ) || !BN_is_one( tmp ) )
			return( FALSE );
		}

	/* Verify that:

		( q * u ) mod p == 1 */
	CK( BN_mod_mul( tmp, q, &pkcInfo->rsaParam_u, p, pkcInfo->bnCTX ) );
	if( bnStatusError( bnStatus ) || !BN_is_one( tmp ) )
		return( FALSE );

	/* A very small number of systems/compilers can't handle 32 * 32 -> 64
	   ops, which means that we have to use 16-bit bignum components.  For
	   the common case where e = F4, the value won't fit into a bignum
	   component, so we have to use the full BN_mod() form of the checks
	   that are carried out further on */
#ifdef SIXTEEN_BIT
	CK( BN_mod( tmp, p1, e, pkcInfo->bnCTX ) );
	if( bnStatusError( bnStatus ) || BN_is_zero( tmp ) )
		return( FALSE );
	CK( BN_mod( tmp, q1, e, pkcInfo->bnCTX ) );
	if( bnStatusError( bnStatus ) || BN_is_zero( tmp ) )
		return( FALSE );
	return( TRUE );
#endif /* Systems without 32 * 32 -> 64 ops */

	/* We don't allow bignum e values, both because it doesn't make sense to
	   use them and because the tests below assume that e will fit into a
	   machine word.  The check for a bignum e is eWord == BN_MASK2, but we
	   make this a general check for valid e values */
	if( eWord < MIN_PUBLIC_EXPONENT || eWord >= BN_MASK2 )
		return( FALSE );

	/* Verify that e is a small prime.  The easiest way to do this would be
	   to compare it to a set of standard values, but there'll always be some
	   wierdo implementation that uses a nonstandard value and that would
	   therefore fail the test, so we perform a quick check that just tries
	   dividing by all primes below 1000.  In addition since in almost all
	   cases e will be one of a standard set of values, we don't bother with
	   the trial division unless it's an unusual value.  This test isn't
	   perfect, but it'll catch obvious non-primes.

	   Note that OpenSSH hardcodes e = 35, which is both a suboptimal
	   exponent (it's less efficient that a safer value like 257 or F4)
	   and non-prime.  The reason for this was that the original SSH used an
	   e relatively prime to (p-1)(q-1), choosing odd (in both senses of the
	   word) numbers > 31.  33 or 35 probably ended up being chosen
	   frequently, so it was hardcoded into OpenSSH.  In order to use
	   OpenSSH keys, you need to comment out this test and the following
	   one */
	if( eWord != 17 && eWord != 257 && eWord != 65537L )
		{
		static const unsigned int FAR_BSS smallPrimes[] = {
			   2,   3,   5,   7,  11,  13,  17,  19,
			  23,  29,  31,  37,  41,  43,  47,  53,
			  59,  61,  67,  71,  73,  79,  83,  89,
			  97, 101, 103, 107, 109, 113, 127, 131,
			 137, 139, 149, 151, 157, 163, 167, 173,
			 179, 181, 191, 193, 197, 199, 211, 223,
			 227, 229, 233, 239, 241, 251, 257, 263,
			 269, 271, 277, 281, 283, 293, 307, 311,
			 313, 317, 331, 337, 347, 349, 353, 359,
			 367, 373, 379, 383, 389, 397, 401, 409,
			 419, 421, 431, 433, 439, 443, 449, 457,
			 461, 463, 467, 479, 487, 491, 499, 503,
			 509, 521, 523, 541, 547, 557, 563, 569,
			 571, 577, 587, 593, 599, 601, 607, 613,
			 617, 619, 631, 641, 643, 647, 653, 659,
			 661, 673, 677, 683, 691, 701, 709, 719,
			 727, 733, 739, 743, 751, 757, 761, 769,
			 773, 787, 797, 809, 811, 821, 823, 827,
			 829, 839, 853, 857, 859, 863, 877, 881,
			 883, 887, 907, 911, 919, 929, 937, 941,
			 947, 953, 967, 971, 977, 983, 991, 997,
			 0, 0
			 };
		int i;

		for( i = 0; 
			 eWord > smallPrimes[ i ] && smallPrimes[ i ] > 0 && \
				i < FAILSAFE_ARRAYSIZE( smallPrimes, int ); 
			 i++ )
			{
			if( eWord % smallPrimes[ i ] == 0 )
				return( FALSE );
			}
		if( i >= FAILSAFE_ARRAYSIZE( smallPrimes, int ) )
			retIntError();
		}

	/* Verify that:

		gcd( ( p - 1 )( q - 1), e ) == 1
	
	   Since e is a small prime, we can do this much more efficiently by 
	   checking that:

		( p - 1 ) mod e != 0
		( q - 1 ) mod e != 0 */
	if( BN_mod_word( p1, eWord ) == 0 || BN_mod_word( q1, eWord ) == 0 )
		return( FALSE );

	/* Verify that:

		e1 < p
		e2 < q */
	if( BN_cmp( &pkcInfo->rsaParam_exponent1, p ) >= 0 )
		return( FALSE );
	if( BN_cmp( &pkcInfo->rsaParam_exponent2, q ) >= 0 )
		return( FALSE );

	return( TRUE );
	}

/* Initialise and check an RSA key.  Unlike the DLP check, this function
   combines the initialisation with the checking, since the two are deeply
   intertwingled */

int initCheckRSAkey( CONTEXT_INFO *contextInfoPtr )
	{
	PKC_INFO *pkcInfo = contextInfoPtr->ctxPKC;
	BIGNUM *n = &pkcInfo->rsaParam_n, *e = &pkcInfo->rsaParam_e;
	BIGNUM *d = &pkcInfo->rsaParam_d, *p = &pkcInfo->rsaParam_p;
	BIGNUM *q = &pkcInfo->rsaParam_q;
	const BOOLEAN isPrivateKey = \
		( contextInfoPtr->flags & CONTEXT_ISPUBLICKEY ) ? FALSE : TRUE;
	int length, bnStatus = BN_STATUS, status = CRYPT_OK;

	/* Make sure that the necessary key parameters have been initialised */
	if( BN_is_zero( n ) || BN_is_zero( e ) )
		return( CRYPT_ARGERROR_STR1 );
	if( isPrivateKey )
		{
		if( BN_is_zero( p ) || BN_is_zero( q ) )
			return( CRYPT_ARGERROR_STR1 );
		if( BN_is_zero( d ) && \
			( BN_is_zero( &pkcInfo->rsaParam_exponent1 ) || \
			  BN_is_zero( &pkcInfo->rsaParam_exponent2 ) ) )
			/* Either d or e1 et al must be present, d isn't needed if we
			   have e1 et al and e1 et al can be reconstructed from d */
			return( CRYPT_ARGERROR_STR1 );
		}

	/* Make sure that the key paramters are valid:

		nLen >= MIN_PKCSIZE, nLen <= CRYPT_MAX_PKCSIZE

		e >= MIN_PUBLIC_EXPONENT, e < n
		
		|p-q| > 128 bits
		
	   BN_get_word() works even on 16-bit systems because it returns 
	   BN_MASK2 (== UINT_MAX) if the value can't be represented in a machine
	   word */
	length = BN_num_bytes( n );
	if( isShortPKCKey( length ) )
		/* Special-case handling for insecure-sized public keys */
		return( CRYPT_ERROR_NOSECURE );
	if( length < MIN_PKCSIZE || length > CRYPT_MAX_PKCSIZE )
		return( CRYPT_ARGERROR_STR1 );
	if( BN_get_word( e ) < MIN_PUBLIC_EXPONENT )
		return( CRYPT_ARGERROR_STR1 );
	if( BN_cmp( e, n ) >= 0 )
		return( CRYPT_ARGERROR_STR1 );
	if( isPrivateKey )
		{
		/* Make sure that p and q differ by at least 128 bits */
		CKPTR( BN_copy( &pkcInfo->tmp1, p ) );
		CK( BN_sub( &pkcInfo->tmp1, &pkcInfo->tmp1, q ) );
		if( bnStatusError( bnStatus ) || BN_num_bits( &pkcInfo->tmp1 ) < 128 )
			return( CRYPT_ARGERROR_STR1 );
		}

	/* If we're not using PKCS keys that have exponent1 = d mod ( p - 1 )
	   and exponent2 = d mod ( q - 1 ) precalculated, evaluate them now.
	   If there's no u precalculated, evaluate it now */
	if( isPrivateKey )
		{
		if( BN_is_zero( &pkcInfo->rsaParam_exponent1 ) )
			{
			BIGNUM *exponent1 = &pkcInfo->rsaParam_exponent1;
			BIGNUM *exponent2 = &pkcInfo->rsaParam_exponent2;

			CKPTR( BN_copy( exponent1, p ) );/* exponent1 = d mod ( p - 1 ) ) */
			CK( BN_sub_word( exponent1, 1 ) );
			CK( BN_mod( exponent1, d, exponent1, pkcInfo->bnCTX ) );
			CKPTR( BN_copy( exponent2, q ) );/* exponent2 = d mod ( q - 1 ) ) */
			CK( BN_sub_word( exponent2, 1 ) );
			CK( BN_mod( exponent2, d, exponent2, pkcInfo->bnCTX ) );
			if( bnStatusError( bnStatus ) )
				return( getBnStatus( bnStatus ) );
			}
		if( BN_is_zero( &pkcInfo->rsaParam_u ) )
			{
			CKPTR( BN_mod_inverse( &pkcInfo->rsaParam_u, q, p,
								   pkcInfo->bnCTX ) );
			if( bnStatusError( bnStatus ) )
				return( getBnStatus( bnStatus ) );
			}
		}

	/* Make sure that p and q are set up correctly for the CRT decryption and
	   precompute the Montgomery forms */
	if( isPrivateKey )
		status = fixCRTvalues( pkcInfo, TRUE );
	if( cryptStatusOK( status ) )
		status = getRSAMontgomery( pkcInfo, isPrivateKey );
	if( cryptStatusError( status ) )
		return( status );

	/* Now that we've got the various other values set up, perform further
	   validity checks on the private key */
	if( isPrivateKey && !checkRSAPrivateKeyComponents( pkcInfo ) )
		return( CRYPT_ARGERROR_STR1 );

	pkcInfo->keySizeBits = BN_num_bits( &pkcInfo->rsaParam_n );

	/* Enable side-channel protection if required */
	if( contextInfoPtr->flags & CONTEXT_SIDECHANNELPROTECTION )
		status = enableSidechannelProtection( pkcInfo, isPrivateKey );
	return( status );
	}
