/****************************************************************************
*																			*
*					  cryptlib Datatgram Encoding Routines					*
*						Copyright Peter Gutmann 1996-2006					*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "envelope.h"
  #include "asn1.h"
#else
  #include "envelope/envelope.h"
  #include "misc/asn1.h"
#endif /* Compiler-specific includes */

/*			 .... NO! ...				   ... MNO! ...
		   ..... MNO!! ...................... MNNOO! ...
		 ..... MMNO! ......................... MNNOO!! .
		.... MNOONNOO!	 MMMMMMMMMMPPPOII!	 MNNO!!!! .
		 ... !O! NNO! MMMMMMMMMMMMMPPPOOOII!! NO! ....
			...... ! MMMMMMMMMMMMMPPPPOOOOIII! ! ...
		   ........ MMMMMMMMMMMMPPPPPOOOOOOII!! .....
		   ........ MMMMMOOOOOOPPPPPPPPOOOOMII! ...
			....... MMMMM..	   OPPMMP	 .,OMI! ....
			 ...... MMMM::	 o.,OPMP,.o	  ::I!! ...
				 .... NNM:::.,,OOPM!P,.::::!! ....
				  .. MMNNNNNOOOOPMO!!IIPPO!!O! .....
				 ... MMMMMNNNNOO:!!:!!IPPPPOO! ....
				   .. MMMMMNNOOMMNNIIIPPPOO!! ......
				  ...... MMMONNMMNNNIIIOO!..........
			   ....... MN MOMMMNNNIIIIIO! OO ..........
			......... MNO! IiiiiiiiiiiiI OOOO ...........
		  ...... NNN.MNO! . O!!!!!!!!!O . OONO NO! ........
		   .... MNNNNNO! ...OOOOOOOOOOO .  MMNNON!........
		   ...... MNNNNO! .. PPPPPPPPP .. MMNON!........
			  ...... OO! ................. ON! .......
				 ................................

   Be very careful when modifying this code, the data manipulation that it
   performs is somewhat tricky */

#ifdef USE_ENVELOPES

/****************************************************************************
*																			*
*								Utility Routines							*
*																			*
****************************************************************************/

/* Sanity-check the envelope state */

static BOOLEAN sanityCheck( const ENVELOPE_INFO *envelopeInfoPtr )
	{
	/* Make sure that the buffer position is within bounds */
	if( envelopeInfoPtr->bufPos < 0 || \
		envelopeInfoPtr->bufPos > envelopeInfoPtr->bufSize || \
		envelopeInfoPtr->bufSize < MIN_BUFFER_SIZE )
		return( FALSE );

	/* Make sure that the block buffer position is within bounds */
	if( envelopeInfoPtr->blockSize > 0 && \
		( envelopeInfoPtr->blockBufferPos < 0 || \
		  envelopeInfoPtr->blockBufferPos >= envelopeInfoPtr->blockSize ) )
		return( FALSE );

	/* If we're drained the envelope buffer, we're done */
	if( envelopeInfoPtr->segmentStart == 0 && \
		envelopeInfoPtr->segmentDataStart == 0 && \
		envelopeInfoPtr->bufPos == 0 )
		return( TRUE );

	/* Make sure that the buffer internal bookeeping is OK */
	if( envelopeInfoPtr->segmentStart < 0 || \
		envelopeInfoPtr->segmentStart >= envelopeInfoPtr->bufPos || \
		envelopeInfoPtr->segmentDataStart < envelopeInfoPtr->segmentStart || \
		envelopeInfoPtr->segmentDataStart >= envelopeInfoPtr->bufPos )
		return( FALSE );

	return( TRUE );
	}

/****************************************************************************
*																			*
*							Header Processing Routines						*
*																			*
****************************************************************************/

/* Determine the length of the encoded length value and the threshold at which the
   length encoding changes for constructed indefinite-length strings.  The
   length encoding is the actual length if <= 127, or a one-byte length-of-
   length followed by the length if > 127 */

#define TAG_SIZE					1	/* Useful symbolic define */

#if INT_MAX > 32767

#define lengthOfLength( length )	( ( length < 0x80 ) ? 1 : \
									  ( length < 0x100 ) ? 2 : \
									  ( length < 0x10000 ) ? 3 : \
									  ( length < 0x1000000 ) ? 4 : 5 )

#define findThreshold( length )		( ( length < 0x80 ) ? 0x7F : \
									  ( length < 0x100 ) ? 0xFF : \
									  ( length < 0x10000 ) ? 0xFFFF : \
									  ( length < 0x1000000 ) ? 0xFFFFFF : INT_MAX )
#else

#define lengthOfLength( length )	( ( length < 0x80 ) ? 1 : \
									  ( length < 0x100 ) ? 2 : 3 )

#define findThreshold( length )		( ( length < 0x80 ) ? 127 : \
									  ( length < 0x100 ) ? 0xFF : INT_MAX )
#endif /* 32-bit ints */

/* Begin a new segment in the buffer.  The layout is:

			tag	len		 payload
	+-------+-+---+---------------------+-------+
	|		| |	  |						|		|
	+-------+-+---+---------------------+-------+
			^	  ^						^
			|	  |						|
		 sStart sDataStart			sDataEnd */

static int beginSegment( ENVELOPE_INFO *envelopeInfoPtr )
	{
	const int lLen = lengthOfLength( envelopeInfoPtr->bufSize );

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize && \
			envelopeInfoPtr->bufSize >= MIN_BUFFER_SIZE );
	assert( ( envelopeInfoPtr->blockSize == 0 ) || \
			( envelopeInfoPtr->blockBufferPos >= 0 && \
			  envelopeInfoPtr->blockBufferPos < envelopeInfoPtr->blockSize ) );

	/* Make sure that there's enough room in the buffer to accommodate the
	   start of a new segment.  In the worst case this is 6 bytes (OCTET
	   STRING tag + 5-byte length) + 15 bytes (blockBuffer contents for a
	   128-bit block cipher).  Although in practice we could eliminate this
	   condition, it would require tracking a lot of state information to
	   record which data had been encoded into the buffer and whether the
	   blockBuffer data had been copied into the buffer, so to keep it
	   simple we require enough room to do everything at once */
	if( envelopeInfoPtr->bufPos + TAG_SIZE + lLen + \
		envelopeInfoPtr->blockBufferPos >= envelopeInfoPtr->bufSize )
		return( CRYPT_ERROR_OVERFLOW );

	/* Adjust the buffer position indicators to handle potential
	   intermediate headers */
	envelopeInfoPtr->segmentStart = envelopeInfoPtr->bufPos;
	if( envelopeInfoPtr->payloadSize == CRYPT_UNUSED )
		{
		/* Begin a new segment after the end of the current segment.  We
		   always leave enough room for the largest allowable length field
		   because we may have a short segment at the end of the buffer which
		   is moved to the start of the buffer after data is copied out,
		   turning it into a longer segment.  For this reason we rely on
		   completeSegment() to get the length right and move any data down
		   as required */
		envelopeInfoPtr->bufPos += TAG_SIZE + lLen;
		}
	envelopeInfoPtr->segmentDataStart = envelopeInfoPtr->bufPos;
	assert( envelopeInfoPtr->bufPos + envelopeInfoPtr->blockBufferPos <= \
			envelopeInfoPtr->bufSize );

	/* Now copy anything left in the block buffer to the start of the new
	   segment.  We know that everything will fit because we've checked
	   earlier on that the header and blockbuffer contents will fit into
	   the remaining space */
	if( envelopeInfoPtr->blockBufferPos > 0 )
		{
		memcpy( envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos,
				envelopeInfoPtr->blockBuffer, envelopeInfoPtr->blockBufferPos );
		envelopeInfoPtr->bufPos += envelopeInfoPtr->blockBufferPos;
		}
	envelopeInfoPtr->blockBufferPos = 0;
	assert( envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );

	/* We've started the new segment, mark it as incomplete */
	envelopeInfoPtr->dataFlags &= ~ENVDATA_SEGMENTCOMPLETE;

	return( CRYPT_OK );
	}

/* Complete a segment of data in the buffer.  This is incredibly complicated
   because we need to take into account the indefinite-length encoding (which
   has a variable-size length field) and the quantization to the cipher block
   size.  In particular the indefinite-length encoding means that we can
   never encode a block with a size of 130 bytes (we get tag + length + 127 =
   129, then tag + length-of-length + length + 128 = 131), and the same for
   the next boundary at 256 bytes */

static BOOLEAN encodeSegmentHeader( ENVELOPE_INFO *envelopeInfoPtr )
	{
	STREAM stream;
	const BOOLEAN isEncrypted = \
			( envelopeInfoPtr->iCryptContext != CRYPT_ERROR ) ? TRUE : FALSE;
	const int oldHdrLen = envelopeInfoPtr->segmentDataStart - \
						  envelopeInfoPtr->segmentStart;
	int dataLen = envelopeInfoPtr->bufPos - envelopeInfoPtr->segmentDataStart;
	int hdrLen, quantisedTotalLen, remainder = 0;
	BOOLEAN needsPadding = envelopeInfoPtr->dataFlags & ENVDATA_NEEDSPADDING;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );
	assert( envelopeInfoPtr->segmentStart >= 0 && \
			envelopeInfoPtr->segmentStart < envelopeInfoPtr->bufPos );
	assert( envelopeInfoPtr->segmentDataStart >= \
								envelopeInfoPtr->segmentStart && \
			envelopeInfoPtr->segmentDataStart < envelopeInfoPtr->bufPos );

	/* If we're adding PKCS #5 padding, try and add one block's worth of
	   pseudo-data.  This adjusted data length is then fed into the block
	   size quantisation process, after which any odd-sized remainder is
	   ignored, and the necessary padding bytes are added to account for the
	   difference between the actual and padded size */
	if( needsPadding )
		{
		/* Check whether the padding will fit onto the end of the data.  This
		   check isn't completely accurate since the length encoding might
		   shrink by one or two bytes and allow a little extra data to be
		   squeezed in, however the extra data could cause the length
		   encoding to expand again, requiring a complex adjustment process.
		   To make things easier we ignore this possibility at the expense of
		   emitting one more segment than is necessary in a few very rare
		   cases */
		if( envelopeInfoPtr->segmentDataStart + dataLen + \
			envelopeInfoPtr->blockSize < envelopeInfoPtr->bufSize )
			dataLen += envelopeInfoPtr->blockSize;
		else
			needsPadding = FALSE;
		}

	/* Now that we've made any necessary adjustments to the data length,
	   determine the length of the length encoding (which may have grown or
	   shrunk since we initially calculated it when we began the segment) and
	   any combined data lengths based on it */
	hdrLen = ( envelopeInfoPtr->payloadSize == CRYPT_UNUSED ) ? \
			 TAG_SIZE + lengthOfLength( dataLen ) : 0;
	quantisedTotalLen = hdrLen + dataLen;

	/* Quantize and adjust the length if we're encrypting in a block mode */
	if( isEncrypted )
		{
		int threshold;

		/* Determine the length due to cipher block-size quantisation */
		quantisedTotalLen = dataLen & envelopeInfoPtr->blockSizeMask;

		/* If the block-size quantisation has moved the quantised length
		   across a length-of-length encoding boundary, adjust hdrLen to
		   account for this */
		threshold = findThreshold( quantisedTotalLen );
		if( quantisedTotalLen <= threshold && dataLen > threshold )
			hdrLen--;

		/* Remember how many bytes we can't fit into the current block
		   (these will be copied into the block buffer for later use), and
		   the new size of the data due to quantisation */
		remainder = dataLen - quantisedTotalLen;
		dataLen = quantisedTotalLen;
		}
	assert( ( envelopeInfoPtr->payloadSize != CRYPT_UNUSED && hdrLen == 0 ) || \
			( envelopeInfoPtr->payloadSize == CRYPT_UNUSED && \
			  hdrLen > 0 && hdrLen <= 6 ) );
	assert( remainder >= 0 && \
			( envelopeInfoPtr->blockSize == 0 || \
			  remainder < envelopeInfoPtr->blockSize ) );

	/* If there's not enough data present to do anything, tell the caller */
	if( quantisedTotalLen <= 0 )
		return( FALSE );
	assert( dataLen >= 0 );

	/* If there's a header between segments and the header length encoding
	   has shrunk (either due to the cipher block size quantization
	   shrinking the segment or because we've wrapped up a segment at less
	   than the original projected length), move the data down.  In the
	   worst case the shrinking can cover several bytes if we go from a
	   > 255 byte segment to a <= 127 byte one */
	if( hdrLen > 0 && hdrLen < oldHdrLen )
		{
		BYTE *segmentDataPtr = envelopeInfoPtr->buffer + \
							   envelopeInfoPtr->segmentStart;
		const int delta = oldHdrLen - hdrLen;

		memmove( segmentDataPtr + hdrLen, segmentDataPtr + oldHdrLen,
				 envelopeInfoPtr->bufPos - envelopeInfoPtr->segmentDataStart );
		envelopeInfoPtr->bufPos -= delta;
		envelopeInfoPtr->segmentDataStart -= delta;
		}
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );
	assert( envelopeInfoPtr->segmentDataStart >= \
								envelopeInfoPtr->segmentStart && \
			envelopeInfoPtr->segmentDataStart + dataLen <= \
								envelopeInfoPtr->bufSize );

	/* If we need to add PKCS #5 block padding, do so now (we know from the
	   quantisedTotalLen check above that there's enough room for this).
	   Since the extension of the data length to allow for padding data is
	   performed by adding one block of pseudo-data and letting the block
	   quantisation system take care of any discrepancies, we can calculate
	   the padding amount as the difference between any remainder after
	   quantisation and the block size */
	if( needsPadding )
		{
		const int padSize = envelopeInfoPtr->blockSize - remainder;
		int i;

		/* Add the block padding and set the remainder to zero, since we're
		   now at an even block boundary */
		for( i = 0; i < padSize; i++ )
			envelopeInfoPtr->buffer[ envelopeInfoPtr->bufPos + i ] = padSize;
		envelopeInfoPtr->bufPos += padSize;
		envelopeInfoPtr->dataFlags &= ~ENVDATA_NEEDSPADDING;
		remainder = 0;
		}
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );

	/* Move any leftover bytes across into the block buffer */
	if( remainder > 0 )
		{
		memcpy( envelopeInfoPtr->blockBuffer,
				envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos - \
										  remainder, remainder );
		envelopeInfoPtr->blockBufferPos = remainder;
		envelopeInfoPtr->bufPos -= remainder;
		}
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );

	/* If we're using the definite length form, exit */
	if( envelopeInfoPtr->payloadSize != CRYPT_UNUSED )
		return( TRUE );

	/* Insert the OCTET STRING header into the data stream */
	sMemOpen( &stream, envelopeInfoPtr->buffer + \
					   envelopeInfoPtr->segmentStart, hdrLen );
	writeOctetStringHole( &stream, dataLen, DEFAULT_TAG );
	assert( stell( &stream ) == hdrLen );
	sMemDisconnect( &stream );

	return( TRUE );
	}

static int completeSegment( ENVELOPE_INFO *envelopeInfoPtr,
							const BOOLEAN forceCompletion )
	{
	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );

	/* If we're enveloping data using indefinite encoding and we're not at
	   the end of the data, don't emit a sub-segment containing less then 10
	   bytes of data.  This is to protect against users who write code that
	   performs byte-at-a-time enveloping, at least we can quantize the data
	   amount to make it slightly more efficient.  As a side-effect, it
	   avoids occasional inefficiencies at boundaries where one or two bytes
	   may still be hanging around from a previous data block, since they'll
	   be coalesced into the following block */
	if( !forceCompletion && envelopeInfoPtr->payloadSize == CRYPT_UNUSED && \
		( envelopeInfoPtr->bufPos - envelopeInfoPtr->segmentDataStart ) < 10 )
		{
		/* We can't emit any of the small sub-segment, however there may be
		   (non-)data preceding this that we can hand over so we set the
		   segment data end value to the start of the segment */
		envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->segmentStart;
		return( CRYPT_OK );
		}

	/* Wrap up the segment */
	if( !( envelopeInfoPtr->dataFlags & ENVDATA_NOSEGMENT ) )
		{
		if( !encodeSegmentHeader( envelopeInfoPtr ) )
			/* Not enough data to complete the segment */
			return( CRYPT_ERROR_UNDERFLOW );
		}
	if( envelopeInfoPtr->iCryptContext != CRYPT_ERROR )
		{
		int status;

		status = krnlSendMessage( envelopeInfoPtr->iCryptContext,
						IMESSAGE_CTX_ENCRYPT,
						envelopeInfoPtr->buffer + \
								envelopeInfoPtr->segmentDataStart,
						envelopeInfoPtr->bufPos - \
								envelopeInfoPtr->segmentDataStart );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Remember how much data is now available to be read out */
	envelopeInfoPtr->segmentDataEnd = envelopeInfoPtr->bufPos;

	/* Mark this segment as being completed */
	envelopeInfoPtr->dataFlags |= ENVDATA_SEGMENTCOMPLETE;

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*								Copy to Envelope							*
*																			*
****************************************************************************/

/* Flush any remaining data through into the envelope buffer */

static int flushEnvelopeData( ENVELOPE_INFO *envelopeInfoPtr )
	{
	ACTION_LIST *hashActionPtr;
	BOOLEAN needNewSegment = envelopeInfoPtr->dataFlags & \
							 ENVDATA_NEEDSPADDING;
	int iterationCount, status;

	/* If we're using an explicit payload length, make sure that we copied
	   in as much data as was explicitly declared */
	if( envelopeInfoPtr->payloadSize != CRYPT_UNUSED && \
		envelopeInfoPtr->segmentSize != 0 )
		return( CRYPT_ERROR_UNDERFLOW );

#ifdef USE_COMPRESSION
	/* If we're using compression, flush any remaining data out of the
	   zStream */
	if( envelopeInfoPtr->flags & ENVELOPE_ZSTREAMINITED )
		{
		int bytesToCopy;

		/* If we've just completed a segment, begin a new one.  This action
		   is slightly anomalous in that normally a flush can't add more
		   data to the envelope and so we'd never need to start a new
		   segment during a flush, however since we can have arbitrarily
		   large amounts of data trapped in subspace via zlib we need to be
		   able to handle starting new segments at this point */
		if( envelopeInfoPtr->dataFlags & ENVDATA_SEGMENTCOMPLETE )
			{
			status = beginSegment( envelopeInfoPtr );
			if( cryptStatusError( status ) )
				return( status );
			if( envelopeInfoPtr->bufPos >= envelopeInfoPtr->bufSize )
				return( CRYPT_ERROR_OVERFLOW );
			}

		/* Flush any remaining compressed data into the envelope buffer */
		bytesToCopy = envelopeInfoPtr->bufSize - envelopeInfoPtr->bufPos;
		envelopeInfoPtr->zStream.next_in = NULL;
		envelopeInfoPtr->zStream.avail_in = 0;
		envelopeInfoPtr->zStream.next_out = envelopeInfoPtr->buffer + \
											envelopeInfoPtr->bufPos;
		envelopeInfoPtr->zStream.avail_out = bytesToCopy;
		status = deflate( &envelopeInfoPtr->zStream, Z_FINISH );
		if( status != Z_STREAM_END && status != Z_OK )
			{
			/* There was some problem other than the output buffer being
			   full */
			retIntError();
			}

		/* Adjust the status information based on the data flushed out of
		   the zStream.  We don't need to check for the output buffer being
		   full because this case is already handled by the check of the
		   deflate() return value */
		envelopeInfoPtr->bufPos += bytesToCopy - \
								   envelopeInfoPtr->zStream.avail_out;
		assert( envelopeInfoPtr->bufPos >= 0 && \
				envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize );

		/* If we didn't finish flushing data because the output buffer is
		   full, complete the segment and tell the caller that they need to
		   pop some data */
		if( status == Z_OK )
			{
			status = completeSegment( envelopeInfoPtr, TRUE );
			return( cryptStatusError( status ) ? \
					status : CRYPT_ERROR_OVERFLOW );
			}
		}
#endif /* USE_COMPRESSION */

	/* If we're encrypting data with a block cipher, we need to add PKCS #5
	   padding at the end of the last block */
	if( envelopeInfoPtr->blockSize > 1 )
		{
		envelopeInfoPtr->dataFlags |= ENVDATA_NEEDSPADDING;
		if( envelopeInfoPtr->dataFlags & ENVDATA_SEGMENTCOMPLETE )
			/* The current segment has been wrapped up, we need to begin a
			   new segment to contain the padding */
			needNewSegment = TRUE;
		}

	/* If we're carrying over the padding requirement from a previous block,
	   we need to begin a new block before we can try and add the padding.
	   This can happen if there was data left after the previous segment was
	   completed or if the addition of padding would have overflowed the
	   buffer when the segment was completed, in other words if the
	   needPadding flag is still set from the previous call */
	if( needNewSegment )
		{
		status = beginSegment( envelopeInfoPtr );
		if( cryptStatusError( status ) )
			return( status );
		if( envelopeInfoPtr->bufPos >= envelopeInfoPtr->bufSize )
			return( CRYPT_ERROR_OVERFLOW );
		}

	/* Complete the segment if necessary */
	if( !( envelopeInfoPtr->dataFlags & ENVDATA_SEGMENTCOMPLETE ) || \
		( envelopeInfoPtr->dataFlags & ENVDATA_NEEDSPADDING ) )
		{
		status = completeSegment( envelopeInfoPtr, TRUE );
		if( cryptStatusError( status ) )
			return( status );

		/* If there wasn't sufficient room to add the trailing PKCS #5
		   padding, tell the caller to try again */
		if( envelopeInfoPtr->dataFlags & ENVDATA_NEEDSPADDING )
			return( CRYPT_ERROR_OVERFLOW );
		}

	/* If we've completed the hashing, we're done.  In addition unlike CMS,
	   PGP handles authenticated attributes by extending the hashing of the
	   payload data to cover the additional attributes, so if we're using
	   the PGP format we can't wrap up the hashing yet */
	if( !( envelopeInfoPtr->dataFlags & ENVDATA_HASHACTIONSACTIVE ) || \
		envelopeInfoPtr->type == CRYPT_FORMAT_PGP )
		return( 0 );

	/* We've finished processing everything, complete each hash action if
	   necessary */
	assert( envelopeInfoPtr->actionList != NULL );
	iterationCount = 0;
	for( hashActionPtr = envelopeInfoPtr->actionList;
		 hashActionPtr != NULL && \
			( hashActionPtr->action == ACTION_HASH || \
			  hashActionPtr->action == ACTION_MAC ) && \
			iterationCount++ < FAILSAFE_ITERATIONS_MED;
		 hashActionPtr = hashActionPtr->next )
		{
		status = krnlSendMessage( hashActionPtr->iCryptHandle,
								  IMESSAGE_CTX_HASH, "", 0 );
		if( cryptStatusError( status ) )
			return( status );
		}
	if( iterationCount >= FAILSAFE_ITERATIONS_MED )
		retIntError();

	return( 0 );
	}

/* Copy data into the envelope.  Returns the number of bytes copied, or an
   overflow error if we're trying to flush data and there isn't room to
   perform the flush (this somewhat peculiar case is because the caller
   expects to have 0 bytes copied in this case) */

static int copyToEnvelope( ENVELOPE_INFO *envelopeInfoPtr,
						   const BYTE *buffer, const int length )
	{
	ACTION_LIST *hashActionPtr;
	BOOLEAN needCompleteSegment = FALSE;
	BYTE *bufPtr;
	int bytesToCopy, status;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( length >= 0 );
	assert( length == 0 || isReadPtr( buffer, length ) );

	/* Sanity-check the envelope state */
	if( !sanityCheck( envelopeInfoPtr ) )
		retIntError();

	/* If we're trying to copy into a full buffer, return a count of 0 bytes
	   unless we're trying to flush the buffer (the calling routine may
	   convert this to an overflow error if necessary) */
	if( envelopeInfoPtr->bufPos >= envelopeInfoPtr->bufSize )
		return( length > 0 ? 0 : CRYPT_ERROR_OVERFLOW );

	/* If we're generating a detached signature, just hash the data and
	   exit */
	if( envelopeInfoPtr->flags & ENVELOPE_DETACHED_SIG )
		{
		int iterationCount = 0;
		
		/* Unlike CMS, PGP handles authenticated attributes by extending the
		   hashing of the payload data to cover the additional attributes,
		   so if this is a flush and we're using the PGP format we can't
		   wrap up the hashing yet */
		if( length <= 0 && envelopeInfoPtr->type == CRYPT_FORMAT_PGP )
			return( 0 );

		assert( envelopeInfoPtr->actionList != NULL );
		for( hashActionPtr = envelopeInfoPtr->actionList;
			 hashActionPtr != NULL && \
				( hashActionPtr->action == ACTION_HASH || \
				  hashActionPtr->action == ACTION_MAC ) && \
				iterationCount++ < FAILSAFE_ITERATIONS_MED;
			 hashActionPtr = hashActionPtr->next )
			{
			status = krnlSendMessage( hashActionPtr->iCryptHandle,
									  IMESSAGE_CTX_HASH, ( void * ) buffer,
									  length );
			if( cryptStatusError( status ) )
				return( status );
			}
		if( iterationCount >= FAILSAFE_ITERATIONS_MED )
			retIntError();
		return( length );
		}

	/* If we're flushing data, wrap up the segment and exit */
	if( length <= 0 )
		return( flushEnvelopeData( envelopeInfoPtr ) );

	/* If we're using an explicit payload length, make sure that we don't
	   try and copy in more data than has been explicitly declared */
	if( envelopeInfoPtr->payloadSize != CRYPT_UNUSED && \
		length > envelopeInfoPtr->segmentSize )
		return( CRYPT_ERROR_OVERFLOW );

	/* If we've just completed a segment, begin a new one before we add any
	   data */
	if( envelopeInfoPtr->dataFlags & ENVDATA_SEGMENTCOMPLETE )
		{
		status = beginSegment( envelopeInfoPtr );
		if( cryptStatusError( status ) || \
			envelopeInfoPtr->bufPos >= envelopeInfoPtr->bufSize )
			return( 0 );	/* 0 bytes copied */
		}

	/* Copy over as much as we can fit into the buffer */
	bufPtr = envelopeInfoPtr->buffer + envelopeInfoPtr->bufPos;
	bytesToCopy = envelopeInfoPtr->bufSize - envelopeInfoPtr->bufPos;
	if( bytesToCopy <= 0 || envelopeInfoPtr->bufPos < 0 )
		retIntError();
#ifdef USE_COMPRESSION
	if( envelopeInfoPtr->flags & ENVELOPE_ZSTREAMINITED )
		{
		/* Compress the data into the envelope buffer */
		envelopeInfoPtr->zStream.next_in = ( BYTE * ) buffer;
		envelopeInfoPtr->zStream.avail_in = length;
		envelopeInfoPtr->zStream.next_out = bufPtr;
		envelopeInfoPtr->zStream.avail_out = bytesToCopy;
		status = deflate( &envelopeInfoPtr->zStream, Z_NO_FLUSH );
		if( status != Z_OK )
			{
			/* There was some problem other than the output buffer being
			   full */
			retIntError();
			}

		/* Adjust the status information based on the data copied into the
		   zStream and flushed from the zStream into the buffer */
		envelopeInfoPtr->bufPos += bytesToCopy - \
								   envelopeInfoPtr->zStream.avail_out;
		bytesToCopy = length - envelopeInfoPtr->zStream.avail_in;

		/* If the buffer is full (there's no more room left for further
		   input) we need to close off the segment */
		if( envelopeInfoPtr->zStream.avail_out <= 0 )
			needCompleteSegment = TRUE;
		}
	else
#endif /* USE_COMPRESSION */
		{
		/* We're not using compression */
		if( bytesToCopy > length )
			bytesToCopy = length;
		memcpy( bufPtr, buffer, bytesToCopy );
		envelopeInfoPtr->bufPos += bytesToCopy;

		/* Hash the data if necessary */
		if( envelopeInfoPtr->dataFlags & ENVDATA_HASHACTIONSACTIVE )
			{
			int iterationCount = 0;
			
			for( hashActionPtr = envelopeInfoPtr->actionList;
				 hashActionPtr != NULL && \
					( hashActionPtr->action == ACTION_HASH || \
					  hashActionPtr->action == ACTION_MAC ) && \
					iterationCount++ < FAILSAFE_ITERATIONS_MED;
				 hashActionPtr = hashActionPtr->next )
				{
				status = krnlSendMessage( hashActionPtr->iCryptHandle,
										  IMESSAGE_CTX_HASH, bufPtr,
										  bytesToCopy );
				if( cryptStatusError( status ) )
					return( status );
				}
			if( iterationCount >= FAILSAFE_ITERATIONS_MED )
				retIntError();
			}

		/* If the buffer is full (i.e. we've been fed more input data than we
		   could copy into the buffer) we need to close off the segment */
		if( bytesToCopy < length )
			needCompleteSegment = TRUE;
		}

	/* Adjust the bytes-left counter if necessary */
	if( envelopeInfoPtr->payloadSize != CRYPT_UNUSED )
		envelopeInfoPtr->segmentSize -= bytesToCopy;

	/* Close off the segment if necessary */
	if( needCompleteSegment )
		{
		status = completeSegment( envelopeInfoPtr, FALSE );
		if( cryptStatusError( status ) )
			return( status );
		}

	/* Make sure that we've left everything in a valid state */
	assert( envelopeInfoPtr->bufPos >= 0 && \
			envelopeInfoPtr->bufPos <= envelopeInfoPtr->bufSize && \
			envelopeInfoPtr->bufSize >= MIN_BUFFER_SIZE );
	assert( ( envelopeInfoPtr->blockSize == 0 ) || \
			( envelopeInfoPtr->blockBufferPos >= 0 && \
			  envelopeInfoPtr->blockBufferPos < envelopeInfoPtr->blockSize ) );
	assert( envelopeInfoPtr->segmentStart >= 0 && \
			envelopeInfoPtr->segmentStart < envelopeInfoPtr->bufPos );
	assert( envelopeInfoPtr->segmentDataStart >= \
								envelopeInfoPtr->segmentStart && \
			envelopeInfoPtr->segmentDataStart < envelopeInfoPtr->bufPos );

	return( bytesToCopy );
	}

/****************************************************************************
*																			*
*								Copy from Envelope							*
*																			*
****************************************************************************/

/* Copy data from the envelope and begin a new segment in the newly-created
   room.  If called with a zero length value this will create a new segment
   without moving any data.  Returns the number of bytes copied */

static int copyFromEnvelope( ENVELOPE_INFO *envelopeInfoPtr, BYTE *buffer,
							 const int length )
	{
	int bytesToCopy = length, remainder;

	assert( isWritePtr( envelopeInfoPtr, sizeof( ENVELOPE_INFO ) ) );
	assert( length >= 0 );
	assert( length == 0 || isWritePtr( buffer, length ) );

	/* Sanity-check the envelope state */
	if( !sanityCheck( envelopeInfoPtr ) )
		retIntError();

	/* If the caller wants more data than there is available in the set of
	   completed segments, try to wrap up the next segment to make more data
	   available */
	if( bytesToCopy > envelopeInfoPtr->segmentDataEnd )
		{
		/* Try and complete the segment if necessary.  This may not be
		   possible if we're using a block encryption mode and there isn't
		   enough room at the end of the buffer to encrypt a full block.  In
		   addition if we're generating a detached sig the data is
		   communicated out-of-band so there's no segmenting */
		if( !( envelopeInfoPtr->flags & ENVELOPE_DETACHED_SIG ) && \
			!( envelopeInfoPtr->dataFlags & ENVDATA_SEGMENTCOMPLETE ) )
			{
			const int status = completeSegment( envelopeInfoPtr, FALSE );
			if( cryptStatusError( status ) )
				return( status );
			}

		/* Return all of the data that we've got */
		bytesToCopy = min( bytesToCopy, envelopeInfoPtr->segmentDataEnd );
		}
	remainder = envelopeInfoPtr->bufPos - bytesToCopy;
	assert( remainder >= 0 && remainder <= envelopeInfoPtr->bufPos );

	/* Copy the data out and move any remaining data down to the start of the
	   buffer  */
	if( bytesToCopy > 0 )
		{
		memcpy( buffer, envelopeInfoPtr->buffer, bytesToCopy );

		/* Move any remaining data down in the buffer */
		if( remainder > 0 )
			memmove( envelopeInfoPtr->buffer,
					 envelopeInfoPtr->buffer + bytesToCopy, remainder );
		envelopeInfoPtr->bufPos = remainder;

		/* Update the segment location information.  The segment start
		   values track the start position of the last completed segment and
		   aren't updated until we begin a new segment, so they may
		   temporarily go negative at this point when the data from the last
		   completed segment is moved past the start of the buffer.  If this
		   happens we set them to a safe value of zero to ensure that they
		   pass the sanity checks elsewhere in the code */
		envelopeInfoPtr->segmentStart -= bytesToCopy;
		if( envelopeInfoPtr->segmentStart < 0 )
			envelopeInfoPtr->segmentStart = 0;
		envelopeInfoPtr->segmentDataStart -= bytesToCopy;
		if( envelopeInfoPtr->segmentDataStart < 0 )
			envelopeInfoPtr->segmentDataStart = 0;
		envelopeInfoPtr->segmentDataEnd -= bytesToCopy;
		assert( envelopeInfoPtr->segmentDataEnd >= 0 );
		}

	return( bytesToCopy );
	}

/****************************************************************************
*																			*
*							Envelope Access Routines						*
*																			*
****************************************************************************/

void initEnvelopeStreaming( ENVELOPE_INFO *envelopeInfoPtr )
	{
	/* Set the access method pointers */
	envelopeInfoPtr->copyToEnvelopeFunction = copyToEnvelope;
	envelopeInfoPtr->copyFromEnvelopeFunction = copyFromEnvelope;
	}
#endif /* USE_ENVELOPES */
