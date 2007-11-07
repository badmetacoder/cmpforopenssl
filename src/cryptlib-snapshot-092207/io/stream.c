/****************************************************************************
*																			*
*							Stream I/O Functions							*
*						Copyright Peter Gutmann 1993-2006					*
*																			*
****************************************************************************/

#include <stdio.h>
#include <stdarg.h>
#if defined( INC_ALL )
  #include "stream.h"
#else
  #include "io/stream.h"
#endif /* Compiler-specific includes */

/* Prototypes for functions in str_file.c */

int fileRead( STREAM *stream, void *buffer, const int length );
int fileWrite( STREAM *stream, const void *buffer, const int length );
int fileFlush( STREAM *stream );
int fileSeek( STREAM *stream, const long position );

/****************************************************************************
*																			*
*								Utility Functions							*
*																			*
****************************************************************************/

/* Refill a stream buffer from backing storage */

static int refillStream( STREAM *stream )
	{
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_FILE );

	/* If we've reached EOF we can't refill it */
	if( stream->flags & STREAM_FFLAG_EOF )
		{
		/* If partial reads are allowed, return an indication of how much 
		   data we got.  This only works once, after this the persistent 
		   error state will return an underflow error before we get to this
		   point */
		stream->status = CRYPT_ERROR_UNDERFLOW;
		return( ( stream->flags & STREAM_FLAG_PARTIALREAD ) ? \
				OK_SPECIAL : CRYPT_ERROR_UNDERFLOW );
		}

	/* If we've moved to a different place in the file, get new data into 
	   the buffer */
	if( ( stream->flags & STREAM_FFLAG_POSCHANGED ) && \
		!( stream->flags & STREAM_FFLAG_POSCHANGED_NOSKIP ) )
		{
		status = fileSeek( stream, stream->bufCount * stream->bufSize );
		if( cryptStatusError( status ) )
			return( sSetError( stream, status ) );
		}

	/* Try and read more data into the stream buffer */
	status = fileRead( stream, stream->buffer, stream->bufSize );
	if( cryptStatusError( status ) )
		return( sSetError( stream, status ) );
	if( status < stream->bufSize )
		{
		/* If we got less than we asked for, remember that we're at the end
		   of the file */
		stream->flags |= STREAM_FFLAG_EOF;
		if( status == 0 )
			{
			/* We ran out of input on an exact buffer boundary.  If partial 
			   reads are allowed, return an indication of how much data we 
			   got.  This only works once, after this the persistent error 
			   state will return an underflow error before we get to this 
			   point */
			stream->status = CRYPT_ERROR_UNDERFLOW;
			return( ( stream->flags & STREAM_FLAG_PARTIALREAD ) ? \
					OK_SPECIAL : CRYPT_ERROR_UNDERFLOW );
			}
		}

	/* We've refilled the stream buffer from the file, remember the 
	   details */
	if( !( stream->flags & STREAM_FFLAG_POSCHANGED ) )
		{
		stream->bufCount++;
		stream->bufPos = 0;
		}
	stream->bufEnd = status;
	stream->flags &= ~( STREAM_FFLAG_POSCHANGED | \
						STREAM_FFLAG_POSCHANGED_NOSKIP );

	return( CRYPT_OK );
	}

/* Empty a stream buffer to backing storage */

static int emptyStream( STREAM *stream, const BOOLEAN forcedFlush )
	{
	int status = CRYPT_OK;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_FILE );

	/* If the stream position has been changed, this can only have been from 
	   a rewind of the stream, in which case we move back to the start of 
	   the file */
	if( stream->flags & STREAM_FFLAG_POSCHANGED )
		{
		status = fileSeek( stream, 0 );
		if( cryptStatusError( status ) )
			return( sSetError( stream, status ) );
		}

	/* Try and write the data to the stream's backing storage */
	status = fileWrite( stream, stream->buffer, stream->bufPos );
	if( cryptStatusError( status ) )
		return( sSetError( stream, status ) );

	/* Reset the position-changed flag and, if we've written another buffer 
	   full of data, remember the details.  If it's a forced flush we leave
	   everything as is, to remember the last write position in the file */
	stream->flags &= ~STREAM_FFLAG_POSCHANGED;
	if( !forcedFlush )
		{
		stream->bufCount++;
		stream->bufPos = 0;
		}

	return( CRYPT_OK );
	}

/* Expand a virtual file stream's buffer to make room for new data when it
   fills up */

static int expandVirtualFileStream( STREAM *stream, const int length )
	{
	void *newBuffer;
	int newSize;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( sIsVirtualFileStream( stream ) );

	/* If it's a small buffer allocated when we initially read a file and it 
	   doesn't look like we'll be overflowing a standard-size buffer, just 
	   expand it up to STREAM_VFILE_BUFSIZE */
	if( stream->bufSize < STREAM_VFILE_BUFSIZE && \
		stream->bufPos + length < STREAM_VFILE_BUFSIZE - 1024 )
		newSize = STREAM_VFILE_BUFSIZE;
	else
		/* Increasing the stream buffer size in STREAM_VFILE_BUFSIZE steps */
		newSize = stream->bufSize + STREAM_VFILE_BUFSIZE;

	/* Allocate the buffer and copy the new data across, using a safe realloc
	   that wipes the original buffer.  If the malloc fails we return 
	   CRYPT_ERROR_OVERFLOW rather than CRYPT_ERROR_MEMORY since the former 
	   is more appropriate for the emulated-I/O environment */
	if( ( newBuffer = clDynAlloc( "expandVirtualFileStream", \
								  stream->bufSize + STREAM_VFILE_BUFSIZE ) ) == NULL )
		return( sSetError( stream, CRYPT_ERROR_OVERFLOW ) );
	memcpy( newBuffer, stream->buffer, stream->bufEnd );
	zeroise( stream->buffer, stream->bufEnd );
	clFree( "expandVirtualFileStream", stream->buffer );
	stream->buffer = newBuffer;
	stream->bufSize = newSize;

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Read/Write Functions							*
*																			*
****************************************************************************/

/* Read data from a stream */

int sgetc( STREAM *stream )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE );
	assert( stream->bufPos >= 0 && stream->bufPos <= stream->bufEnd );
	assert( isReadPtr( stream->buffer, stream->bufSize ) );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( stream->bufPos < 0 || stream->bufPos > stream->bufEnd )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_READ ) );
		}

	/* If there's a problem with the stream, don't try to do anything */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	switch( stream->type )
		{
		case STREAM_TYPE_MEMORY:
			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* Read the data from the stream buffer */
			if( stream->bufPos >= stream->bufEnd )
				return( sSetError( stream, CRYPT_ERROR_UNDERFLOW ) );
			return( stream->buffer[ stream->bufPos++ ] );

		case STREAM_TYPE_FILE:
			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* Read the data from the file */
			if( stream->bufPos >= stream->bufEnd || \
				( stream->flags & STREAM_FFLAG_POSCHANGED ) )
				{
				int status = refillStream( stream );
				if( cryptStatusError( status ) )
					return( ( status == OK_SPECIAL ) ? 0 : status );
				}
			return( stream->buffer[ stream->bufPos++ ] );
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_READ );		/* Get rid of compiler warning */
	}

int sread( STREAM *stream, void *buffer, const int length )
	{
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE || \
			stream->type == STREAM_TYPE_NETWORK );
	assert( stream->bufPos >= 0 && stream->bufPos <= stream->bufEnd );
	assert( stream->type == STREAM_TYPE_NETWORK || \
			isReadPtr( stream->buffer, stream->bufSize ) );
	assert( isWritePtr( buffer, length ) );
	assert( length > 0 );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( stream->bufPos < 0 || stream->bufPos > stream->bufEnd || \
		!isWritePtr( buffer, length ) )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_READ ) );
		}

	/* If there's a problem with the stream, don't try to do anything */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	switch( stream->type )
		{
		case STREAM_TYPE_MEMORY:
			{
			int localLength = length;

			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* If partial reads are allowed, return whatever's left in the
			   stream buffer.  This only occurs for virtual file streams  
			   that have been translated into memory streams */
			if( stream->flags & STREAM_FLAG_PARTIALREAD )
				{
				assert( sIsVirtualFileStream( stream ) );

				localLength = stream->bufEnd - stream->bufPos;
				}
			
			/* Read the data from the stream buffer */
			if( stream->bufPos + localLength > stream->bufEnd )
				{
				memset( buffer, 0, length );	/* Clear the output buffer */
				return( sSetError( stream, CRYPT_ERROR_UNDERFLOW ) );
				}
			memcpy( buffer, stream->buffer + stream->bufPos, localLength );
			stream->bufPos += localLength;

			/* Usually reads are atomic so we just return an all-OK 
			   indicator, however if we're performing partial reads we need
			   to return an exact byte count */
			return( ( stream->flags & STREAM_FLAG_PARTIALREAD ) ? \
					localLength : CRYPT_OK );
			}

		case STREAM_TYPE_FILE:
			{
			BYTE *bufPtr = buffer;
			int dataLength = length, bytesCopied = 0, iterationCount = 0;

			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* Read the data from the file */
			while( dataLength > 0 && \
				   iterationCount++ < FAILSAFE_ITERATIONS_LARGE )
				{
				const int oldDataLength = dataLength;
				int bytesToCopy;

				/* If the stream buffer is empty, try and refill it */
				if( stream->bufPos >= stream->bufEnd || \
					( stream->flags & STREAM_FFLAG_POSCHANGED ) )
					{
					status = refillStream( stream );
					if( cryptStatusError( status ) )
						return( ( status == OK_SPECIAL ) ? \
								bytesCopied : status );
					}

				/* Copy as much data as we can out of the stream buffer */
				bytesToCopy = min( dataLength, \
								   stream->bufEnd - stream->bufPos );
				memcpy( bufPtr, stream->buffer + stream->bufPos, 
						bytesToCopy );
				stream->bufPos += bytesToCopy;
				bufPtr += bytesToCopy;
				bytesCopied += bytesToCopy;
				dataLength -= bytesToCopy;
				if( dataLength >= oldDataLength )
					retIntError();
				}
			if( iterationCount >= FAILSAFE_ITERATIONS_LARGE )
				retIntError();

			/* Usually reads are atomic so we just return an all-OK 
			   indicator, however if we're performing partial reads we need
			   to return an exact byte count */
			return( ( stream->flags & STREAM_FLAG_PARTIALREAD ) ? \
					bytesCopied : CRYPT_OK );
			}

#ifdef USE_TCP
		case STREAM_TYPE_NETWORK:
			assert( stream->readFunction != NULL );
			assert( ( stream->nFlags & STREAM_NFLAG_ISSERVER ) || \
					( stream->host != NULL && stream->hostLen > 0 ) || \
					stream->netSocket != CRYPT_ERROR );
			assert( stream->protocol != STREAM_PROTOCOL_HTTP || \
					( stream->protocol == STREAM_PROTOCOL_HTTP && \
					  length == sizeof( HTTP_DATA_INFO ) ) );
			assert( stream->timeout >= 0 && stream->timeout <= 300 );

			/* Read the data from the network.  Reads are normally atomic, 
			   but if the partial-write flag is set can be restarted after
			   a timeout */
			status = stream->readFunction( stream, buffer, length );
			if( cryptStatusError( status ) )
				{
				if( status != CRYPT_ERROR_COMPLETE )
					return( status );

				/* If we get a CRYPT_ERROR_COMPLETE status this means that
				   the other side has closed the connection.  This status is 
				   returned when there are intermediate protocol layers such 
				   as HTTP or tunnelling over a cryptlib session involved.
				   When this occurs we update the stream state and map the 
				   status to a standard read error.  The exact code to 
				   return here is a bit uncertain, it isn't specifically a 
				   read error because either the other side is allowed to 
				   close the connection after it's said its bit (and so it's 
				   not a read error), or it has to perform a 
				   cryptographically protected close (in which case any 
				   non-OK status indicates a problem).  The most sensible 
				   status is probably a read error */
				sioctl( stream, STREAM_IOCTL_CONNSTATE, NULL, FALSE );
				return( CRYPT_ERROR_READ );
				}
			if( status < length && \
				!( ( stream->flags & STREAM_FLAG_PARTIALREAD ) || \
				   ( stream->nFlags & STREAM_NFLAG_ENCAPS ) ) )
				{
				/* If we didn't read all of the data and partial reads 
				   aren't allowed, report a read timeout.  The situation
				   for HTTP streams is a bit special because what we're
				   sending to the read function is an HTTP_DATA_INFO
				   structure, so we have to extract the actual length
				   information from that */
				if( stream->protocol == STREAM_PROTOCOL_HTTP )
					{
					const HTTP_DATA_INFO *httpDataInfo = \
									( HTTP_DATA_INFO * ) buffer;

					retExt( CRYPT_ERROR_TIMEOUT, 
							( CRYPT_ERROR_TIMEOUT, STREAM_ERRINFO, 
							  "Read timed out with %d of %d bytes read",
							  httpDataInfo->bytesTransferred, 
							  httpDataInfo->bytesAvail ) );
					}
				retExt( CRYPT_ERROR_TIMEOUT,
						( CRYPT_ERROR_TIMEOUT, STREAM_ERRINFO, 
						  "Read timed out with %d of %d bytes read",
						  status, length ) );
				}
			return( status );
#endif /* USE_TCP */
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_READ );		/* Get rid of compiler warning */
	}

/* Write data to a stream */

int sputc( STREAM *stream, const int ch )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE );
	assert( stream->type == STREAM_TYPE_NULL || \
			isWritePtr( stream->buffer, stream->bufSize ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->bufPos >= 0 && stream->bufPos <= stream->bufSize );
	assert( !( stream->flags & STREAM_FLAG_READONLY ) );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( stream->type != STREAM_TYPE_NULL && \
		( stream->bufPos < 0 || stream->bufPos > stream->bufSize ) )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_WRITE ) );
		}

	/* If there's a problem with the stream, don't try to do anything until
	   the error is cleared */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	switch( stream->type )
		{
		case STREAM_TYPE_NULL:
			assert( !stream->flags );

			/* It's a null stream, just record the write and return */
			stream->bufPos++;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			return( CRYPT_OK );

		case STREAM_TYPE_MEMORY:
			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* Write the data to the stream buffer */
			if( stream->bufPos >= stream->bufSize )
				{
				if( sIsVirtualFileStream( stream ) )
					{
					int status;

					status = expandVirtualFileStream( stream, 1 );
					if( cryptStatusError( status ) )
						return( status );
					}
				else
					return( sSetError( stream, CRYPT_ERROR_OVERFLOW ) );
				}
			stream->buffer[ stream->bufPos++ ] = ch;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			if( sIsVirtualFileStream( stream ) )
				/* This is a memory stream emulating a file stream, set the
				   dirty bit */
				stream->flags |= STREAM_FLAG_DIRTY;

			return( CRYPT_OK );

		case STREAM_TYPE_FILE:
			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* Write the data to the file */
			if( stream->bufPos >= stream->bufSize )
				{
				int status;

				status = emptyStream( stream, FALSE );
				if( cryptStatusError( status ) )
					return( status );
				}
			stream->buffer[ stream->bufPos++ ] = ch;
			stream->flags |= STREAM_FLAG_DIRTY;

			return( CRYPT_OK );
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_WRITE );	/* Get rid of compiler warning */
	}

int swrite( STREAM *stream, const void *buffer, const int length )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE || \
			stream->type == STREAM_TYPE_NETWORK );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_NETWORK || \
			isWritePtr( stream->buffer, stream->bufSize ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_NETWORK || \
			( stream->bufPos >= 0 && stream->bufPos <= stream->bufSize ) );
	assert( isReadPtr( buffer, length ) );
	assert( length > 0 );
	assert( !( stream->flags & STREAM_FLAG_READONLY ) );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( ( stream->type != STREAM_TYPE_NULL && \
		  stream->type != STREAM_TYPE_NETWORK && \
		  ( stream->bufPos < 0 || stream->bufPos > stream->bufSize ) ) || \
		!isReadPtr( buffer, length ) )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_WRITE ) );
		}

	/* If there's a problem with the stream, don't try to do anything until
	   the error is cleared */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	switch( stream->type )
		{
		case STREAM_TYPE_NULL:
			assert( !stream->flags );

			/* It's a null stream, just record the write and return */
			stream->bufPos += length;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			return( CRYPT_OK );

		case STREAM_TYPE_MEMORY:
			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* Write the data to the stream buffer */
			if( stream->bufPos + length > stream->bufSize )
				{
				if( sIsVirtualFileStream( stream ) )
					{
					int status;

					status = expandVirtualFileStream( stream, length );
					if( cryptStatusError( status ) )
						return( status );
					}
				else
					return( sSetError( stream, CRYPT_ERROR_OVERFLOW ) );
				}
			memcpy( stream->buffer + stream->bufPos, buffer, length );
			stream->bufPos += length;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			if( sIsVirtualFileStream( stream ) )
				/* This is a memory stream emulating a file stream, set the
				   dirty bit */
				stream->flags |= STREAM_FLAG_DIRTY;

			return( CRYPT_OK );

		case STREAM_TYPE_FILE:
			{
			const BYTE *bufPtr = buffer;
			int dataLength = length, iterationCount = 0;

			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* Write the data to the file */
			while( dataLength > 0 && \
				   iterationCount++ < FAILSAFE_ITERATIONS_LARGE )
				{
				const int bytesToCopy = \
						min( dataLength, stream->bufSize - stream->bufPos );

				if( bytesToCopy > 0 )
					{
					memcpy( stream->buffer + stream->bufPos, bufPtr, 
							bytesToCopy );
					stream->bufPos += bytesToCopy;
					bufPtr += bytesToCopy;
					dataLength -= bytesToCopy;
					}
				if( stream->bufPos >= stream->bufSize )
					{
					int status;

					status = emptyStream( stream, FALSE );
					if( cryptStatusError( status ) )
						return( status );
					}
				}
			if( iterationCount >= FAILSAFE_ITERATIONS_LARGE )
				retIntError();
			stream->flags |= STREAM_FLAG_DIRTY;

			return( CRYPT_OK );
			}

#ifdef USE_TCP
		case STREAM_TYPE_NETWORK:
			{
			int status;

			assert( stream->writeFunction != NULL );
			assert( ( stream->nFlags & STREAM_NFLAG_ISSERVER ) || \
					( stream->host != NULL && stream->hostLen > 0 ) || \
					stream->netSocket != CRYPT_ERROR );
			assert( stream->protocol != STREAM_PROTOCOL_HTTP || \
					( stream->protocol == STREAM_PROTOCOL_HTTP && \
					  length == sizeof( HTTP_DATA_INFO ) ) );
			assert( stream->timeout >= 0 && stream->timeout <= 300 );

			/* Write the data to the network.  Writes are normally atomic, 
			   but if the partial-write flag is set can be restarted after
			   a timeout */
			status = stream->writeFunction( stream, buffer, length );
			if( cryptStatusError( status ) )
				return( status );
			if( status < length && \
				!( stream->flags & STREAM_FLAG_PARTIALWRITE ) )
				{
				/* If we didn't write all of the data and partial writes 
				   aren't allowed, report a write timeout.  The situation
				   for HTTP streams is a bit special because what we're
				   sending to the write function is an HTTP_DATA_INFO
				   structure, so we have to extract the actual length
				   information from that */
				if( stream->protocol == STREAM_PROTOCOL_HTTP )
					{
					const HTTP_DATA_INFO *httpDataInfo = \
									( HTTP_DATA_INFO * ) buffer;

					retExt( CRYPT_ERROR_TIMEOUT, 
							( CRYPT_ERROR_TIMEOUT, STREAM_ERRINFO, 
							  "Write timed out with %d of %d bytes written",
							  httpDataInfo->bytesTransferred,
							  httpDataInfo->bufSize ) );
					}
				retExt( CRYPT_ERROR_TIMEOUT, 
						( CRYPT_ERROR_TIMEOUT, STREAM_ERRINFO, 
						  "Write timed out with %d of %d bytes written",
						  status, length ) );
				}
			return( status );
			}
#endif /* USE_TCP */
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_WRITE );	/* Get rid of compiler warning */
	}

/* Commit data in a stream to backing storage */

int sflush( STREAM *stream )
	{
	int status = CRYPT_OK, flushStatus;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_FILE || \
			sIsVirtualFileStream( stream ) );
	assert( isReadPtr( stream->buffer, stream->bufSize ) );
	assert( !( stream->flags & STREAM_FLAG_READONLY ) );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( !isReadPtr( stream->buffer, stream->bufSize ) )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_WRITE ) );
		}

	/* If there's a problem with the stream, don't try to do anything until
	   the error is cleared */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	/* If the data is unchanged, there's nothing to do */
	if( !( stream->flags & STREAM_FLAG_DIRTY ) )
		return( CRYPT_OK );

	/* If there's data still in the stream buffer and it's not is a virtual 
	   file stream that's handled via a memory stream, write it to disk (for
	   virtual file streams, the data is committed in an atomic operation 
	   when the file is flushed).  If there's an error at this point we 
	   still try and flush whatever data we have to disk, so we don't bail 
	   out immediately if there's a problem */
	if( stream->bufPos > 0 && !sIsVirtualFileStream( stream ) )
		status = emptyStream( stream, TRUE );

	/* Commit the data */
	flushStatus = fileFlush( stream );
	stream->flags &= ~STREAM_FLAG_DIRTY;

	return( cryptStatusOK( status ) ? flushStatus : status );
	}

/****************************************************************************
*																			*
*								Meta-data Functions							*
*																			*
****************************************************************************/

/* Move to an absolute position in a stream */

int sseek( STREAM *stream, const long position )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE );
	assert( position >= 0 );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( position < 0 )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_READ ) );
		}

	/* If there's a problem with the stream, don't try to do anything */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	switch( stream->type )
		{
		case STREAM_TYPE_NULL:
			assert( !stream->flags );

			/* Move to the position in the stream buffer.  We never get 
			   called directly with an sseek on a memory stream, but end up 
			   here via a translated sSkip() call */
			stream->bufPos = ( int ) position;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			return( CRYPT_OK );

		case STREAM_TYPE_MEMORY:
			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* Move to the position in the stream buffer */
			if( ( int ) position > stream->bufSize )
				{
				stream->bufPos = stream->bufSize;
				return( sSetError( stream, CRYPT_ERROR_UNDERFLOW ) );
				}
			stream->bufPos = ( int ) position;
			if( stream->bufEnd < stream->bufPos )
				stream->bufEnd = stream->bufPos;
			return( CRYPT_OK );

		case STREAM_TYPE_FILE:
			{
			int newBufCount;

			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* If it's a currently-disconnected file stream, all that we can 
			   do is rewind the stream.  This occurs when we're doing an 
			   atomic flush of data to disk and we rewind the stream prior 
			   to writing the new/updated data.  The next buffer-connect 
			   operation will reset the stream state, so there's nothing to 
			   do at this point */
			if( stream->bufSize <= 0 )
				{
				assert( position == 0 );
				return( CRYPT_OK );
				}

			/* It's a file stream, remember the new position in the file */
			newBufCount = position / stream->bufSize;
			if( newBufCount != stream->bufCount )
				{
				/* We're not within the current buffer any more, remember 
				   that we have to explicitly update the file position on
				   the next read */
				stream->flags |= STREAM_FFLAG_POSCHANGED;

				/* If we're already positioned to read the next bufferful 
				   of data, we don't have to explicitly skip ahead to it */
				if( newBufCount == stream->bufCount + 1 ) 
					stream->flags |= STREAM_FFLAG_POSCHANGED_NOSKIP ;

				stream->bufCount = newBufCount;
				}
			stream->bufPos = position % stream->bufSize;
			return( CRYPT_OK );
			}
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_WRITE );	/* Get rid of compiler warning */
	}

/* Skip a number of bytes in a stream */

int sSkip( STREAM *stream, const long offset )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NULL || \
			stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE );
	assert( offset > 0 );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( offset <= 0 )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_READ ) );
		}

	/* If there's a problem with the stream, don't try to do anything */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	/* By far the most common use of sSkip() is to skip data in a memory
	   stream, so we handle it inline */
	if( stream->type == STREAM_TYPE_MEMORY && \
		stream->bufPos + offset <= stream->bufSize )
		{
		stream->bufPos += offset;
		if( stream->bufEnd < stream->bufPos )
			stream->bufEnd = stream->bufPos;
		return( CRYPT_OK );
		}

	return( sseek( stream, stream->bufPos + offset ) );
	}

/* Peek at the next data value in a stream */

int sPeek( STREAM *stream )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_MEMORY || \
			stream->type == STREAM_TYPE_FILE );
	assert( stream->bufPos >= 0 && stream->bufPos <= stream->bufEnd );
	assert( isReadPtr( stream->buffer, stream->bufSize ) );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();
	if( stream->bufPos < 0 || stream->bufPos > stream->bufEnd || \
		!isReadPtr( stream->buffer, stream->bufSize ) )
		{
		assert( NOTREACHED );
		return( sSetError( stream, CRYPT_ERROR_READ ) );
		}

	/* If there's a problem with the stream, don't try to do anything until
	   the error is cleared */
	if( cryptStatusError( stream->status ) )
		return( stream->status );

	/* Read the data from the buffer, but without advancing the read pointer
	   like sgetc() does */
	switch( stream->type )
		{
		case STREAM_TYPE_MEMORY:
			assert( !( stream->flags & ~STREAM_MFLAG_MASK ) );

			/* Read the data from the stream buffer */
			if( stream->bufPos >= stream->bufEnd )
				return( sSetError( stream, CRYPT_ERROR_UNDERFLOW ) );
			return( stream->buffer[ stream->bufPos ] );

		case STREAM_TYPE_FILE:
			assert( !( stream->flags & ~STREAM_FFLAG_MASK ) );

			/* Read the data from the file */
			if( stream->bufPos >= stream->bufEnd || \
				( stream->flags & STREAM_FFLAG_POSCHANGED ) )
				{
				int status = refillStream( stream );
				if( cryptStatusError( status ) )
					return( ( status == OK_SPECIAL ) ? 0 : status );
				}
			return( stream->buffer[ stream->bufPos ] );
		}

	assert( NOTREACHED );
	return( CRYPT_ERROR_READ );		/* Get rid of compiler warning */
	}

/****************************************************************************
*																			*
*								IOCTL Functions								*
*																			*
****************************************************************************/

/* Perform an IOCTL on a stream */

int sioctl( STREAM *stream, const STREAM_IOCTL_TYPE type, void *data,
			const int dataLen )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( ( ( stream->type == STREAM_TYPE_FILE || \
				sIsVirtualFileStream( stream ) ) && \
			  ( type == STREAM_IOCTL_IOBUFFER || \
				type == STREAM_IOCTL_PARTIALREAD ) ) || \
			( stream->type == STREAM_TYPE_NETWORK ) );
	assert( type > STREAM_IOCTL_NONE && type < STREAM_IOCTL_LAST );

	/* Check that the input parameters are in order */
	if( !isWritePtr( stream, sizeof( STREAM ) ) )
		retIntError();

	switch( type )
		{
		case STREAM_IOCTL_IOBUFFER:
			assert( ( data == NULL && dataLen == 0 ) || \
					isWritePtr( data, dataLen ) );
			assert( dataLen == 0 || \
					dataLen == 512 || dataLen == 1024 || \
					dataLen == 2048 || dataLen == 4096 || \
					dataLen == 8192 || dataLen == 16384 );

			/* If it's a virtual file stream emulated in memory, don't do
			   anything */
			if( sIsVirtualFileStream( stream ) )
				break;

			stream->buffer = data;
			stream->bufSize = dataLen;

			/* We've switched to a new I/O buffer, reset all buffer- and 
			   stream-state related variables and remember that we have to 
			   reset the stream position, since there may be a position-
			   change pending that hasn't been reflected down to the 
			   underlying file yet (if it was within the same buffer, the 
			   POSCHANGED flag won't have been set since only the bufPos is 
			   changed) */
			stream->bufPos = stream->bufEnd = stream->bufCount = 0;
			sClearError( stream );
			stream->flags &= ~( STREAM_FFLAG_EOF | \
								STREAM_FFLAG_POSCHANGED_NOSKIP );
			stream->flags |= STREAM_FFLAG_POSCHANGED;
			break;

		case STREAM_IOCTL_PARTIALREAD:
			assert( data == NULL && dataLen == 0 );

			stream->flags |= STREAM_FLAG_PARTIALREAD;
			break;

		case STREAM_IOCTL_PARTIALWRITE:
			assert( data == NULL && dataLen == 0 );

			stream->flags |= STREAM_FLAG_PARTIALWRITE;
			break;

#ifdef USE_TCP
		case STREAM_IOCTL_READTIMEOUT:
		case STREAM_IOCTL_WRITETIMEOUT:
			/* These two values are stored as a shared timeout value
			   which is updated on each data read or write by the
			   caller, so there's no need to maintain distinct values */
			if( data != NULL )
				{
				assert( dataLen == 0 );

				*( ( int * ) data ) = stream->timeout;
				}
			else
				{
				assert( dataLen >= 0 );
				stream->timeout = dataLen;
				if( stream->iTransportSession != CRYPT_ERROR )
					krnlSendMessage( stream->iTransportSession,
									 IMESSAGE_SETATTRIBUTE, &stream->timeout,
									 ( type == STREAM_IOCTL_READTIMEOUT ) ? \
										CRYPT_OPTION_NET_READTIMEOUT : \
										CRYPT_OPTION_NET_WRITETIMEOUT );
				}
			break;

		case STREAM_IOCTL_HANDSHAKECOMPLETE:
			assert( data == NULL );
			assert( dataLen == 0 );
			assert( stream->timeout > 0 );
			assert( stream->savedTimeout >= 0 );

			/* The security protocol handshake has completed, change the 
			   stream timeout value from the connect/handshake timeout to
			   the standard data transfer timeout */
			stream->timeout = stream->savedTimeout;
			stream->savedTimeout = CRYPT_ERROR;
			if( stream->iTransportSession != CRYPT_ERROR )
				krnlSendMessage( stream->iTransportSession,
								 IMESSAGE_SETATTRIBUTE, &stream->timeout,
								 CRYPT_OPTION_NET_CONNECTTIMEOUT );
			break;

		case STREAM_IOCTL_CONNSTATE:
			if( data != NULL )
				{
				assert( dataLen == 0 );

				*( ( int * ) data ) = \
					( stream->nFlags & STREAM_NFLAG_LASTMSG ) ? FALSE : TRUE;
				}
			else
				{
				assert( dataLen == TRUE || dataLen == FALSE );
				if( dataLen )
					stream->nFlags &= ~STREAM_NFLAG_LASTMSG;
				else
					stream->nFlags |= STREAM_NFLAG_LASTMSG;
				}
			break;

		case STREAM_IOCTL_GETCLIENTNAME:
			{
			const int length = strlen( stream->clientAddress ) + 1;

			assert( data != NULL && dataLen > 8 );
			assert( isWritePtr( data, dataLen ) );

			if( length <= 1 )
				return( CRYPT_ERROR_NOTFOUND );
			if( length > dataLen )
				return( CRYPT_ERROR_OVERFLOW );
			memcpy( data, stream->clientAddress, length );
			break;
			}

		case STREAM_IOCTL_GETCLIENTPORT:
			assert( data != NULL );
			assert( dataLen == 0 );

			if( stream->clientPort <= 0 )
				return( CRYPT_ERROR_NOTFOUND );
			*( ( int * ) data ) = stream->clientPort;
			break;

		case STREAM_IOCTL_HTTPREQTYPES:
			assert( stream->protocol == STREAM_PROTOCOL_HTTP );

			if( data != NULL )
				{
				assert( dataLen == 0 );

				*( ( int * ) data ) = \
					stream->nFlags & STREAM_NFLAG_HTTPREQMASK;
				}
			else
				{
				assert( data == NULL );
				assert( !( dataLen & ~STREAM_NFLAG_HTTPREQMASK ) && \
						( dataLen & STREAM_NFLAG_HTTPREQMASK ) );

				stream->nFlags &= ~STREAM_NFLAG_HTTPREQMASK;
				stream->nFlags |= dataLen;

				/* If only an HTTP GET is possible and it's a client-side 
				   stream, it's read-only */
				if( dataLen == STREAM_NFLAG_HTTPGET && \
					!( stream->nFlags & STREAM_NFLAG_ISSERVER ) )
					stream->flags = STREAM_FLAG_READONLY;
				else
					{
					/* Reset the read-only flag if we're changing the HTTP
					   operation type to one that allows writes */
					stream->flags &= ~STREAM_FLAG_READONLY;
					}
				}
			break;

		case STREAM_IOCTL_LASTMESSAGE:
			assert( stream->protocol == STREAM_PROTOCOL_HTTP || \
					stream->protocol == STREAM_PROTOCOL_CMP );
			assert( data == NULL );
			assert( dataLen == TRUE );

			stream->nFlags |= STREAM_NFLAG_LASTMSG;
			break;

		case STREAM_IOCTL_CLOSESENDCHANNEL:
			assert( data == NULL );
			assert( dataLen == 0 );
			assert( !( stream->nFlags & STREAM_NFLAG_USERSOCKET ) );

			/* If this is a user-supplied socket, we can't perform a partial 
			   close without affecting the socket as seen by the user, so we 
			   only perform the partial close if it's a cryptlib-controlled 
			   socket */
			if( !( stream->nFlags & STREAM_NFLAG_USERSOCKET ) )
				stream->transportDisconnectFunction( stream, FALSE );
			break;
#endif /* USE_TCP */

		default:
			assert( NOTREACHED );
		}

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*								Misc Functions								*
*																			*
****************************************************************************/

/* Convert a file stream to a memory stream.  Usually this allocates a 
   buffer and reads the stream into it, however if it's a read-only memory-
   mapped file it just creates a second reference to the data to save
   memory */

int sFileToMemStream( STREAM *memStream, STREAM *fileStream,
					  void **bufPtrPtr, const int length )
	{
	void *bufPtr;
	int status;

	assert( isWritePtr( memStream, sizeof( STREAM ) )  );
	assert( isWritePtr( fileStream, sizeof( STREAM ) ) && \
			fileStream->type == STREAM_TYPE_FILE );
	assert( isWritePtr( *bufPtrPtr, sizeof( void * ) ) );
	assert( length > 0 );

	/* Check that the input parameters are in order */
	if( !isWritePtr( memStream, sizeof( STREAM ) ) || \
		!isWritePtr( fileStream, sizeof( STREAM ) ) || \
		length <= 0 )
		retIntError();

	/* Clear return value */
	memset( memStream, 0, sizeof( STREAM ) );
	*bufPtrPtr = NULL;

	/* If it's a read-only memory-mapped file stream, create the memory 
	   stream as a reference to the file stream */
	if( ( fileStream->flags & \
		  ( STREAM_FLAG_READONLY | STREAM_FFLAG_MMAPPED ) ) == \
		( STREAM_FLAG_READONLY | STREAM_FFLAG_MMAPPED ) )
		{
		/* Make sure that there's enough data left in the memory-mapped
		   stream to reference it as a file stream */
		if( length > sMemDataLeft( fileStream ) )
			return( CRYPT_ERROR_UNDERFLOW );

		/* Create a second reference to the memory-mapped stream and advance 
		   the read pointer in the memory-mapped file stream to mimic the 
		   behaviour of a read from it to the memory stream */
		status = sMemConnect( memStream, fileStream->buffer + \
										 fileStream->bufPos, length );
		if( cryptStatusError( status ) )
			return( status );
		status = sSkip( fileStream, length );
		if( cryptStatusError( status ) )
			{
			sMemDisconnect( memStream );
			return( status );
			}
		return( CRYPT_OK );
		}

	/* It's a file stream, allocate a buffer for the data and read it in as
	   a memory stream */
	if( ( bufPtr = clAlloc( "sFileToMemStream", length ) ) == NULL )
		return( CRYPT_ERROR_MEMORY );
	status = sread( fileStream, bufPtr, length );
	if( cryptStatusOK( status ) )
		status = sMemConnect( memStream, bufPtr, length );
	if( cryptStatusError( status ) )
		{
		clFree( "sFileToMemStream", bufPtr );
		return( status );
		}
	*bufPtrPtr = bufPtr;
	return( CRYPT_OK );
	}
