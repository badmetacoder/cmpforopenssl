/****************************************************************************
*																			*
*						Network Stream I/O Functions						*
*						Copyright Peter Gutmann 1993-2007					*
*																			*
****************************************************************************/

#if defined( INC_ALL )
  #include "stream.h"
#else
  #include "io/stream.h"
#endif /* Compiler-specific includes */

#ifdef USE_TCP

/* Network streams can work on multiple levels.  At the lowest level we have
   the raw network I/O layer, handled by calling setAccessMethodXXX(), which
   hooks up the transport-level I/O functions.  If there's a requirement to
   replace the built-in network I/O, it can be done by replacing the
   functionality at this level.

   Layered on top of the transport-level I/O via setStreamLayerXXX() is an
   optional higher layer protocol such as HTTP, which is added by calling
   the appropriate function to layer the higher-level protocol over the
   transport-level I/O.  Alternatively, we can use setStreamLayerDirect()
   to just pass the call straight down to the transport layer.

   In addition to these two layers, the higher level read requires an extra
   buffering layer in order to avoid making many calls to the transport-
   level I/O function, which is a particular problem for HTTP which has to
   take input a character at a time.  To avoid this problem, we use the
   bufferedRead layer which reads ahead as far as it can and then feeds the
   buffered result back to the caller as required.  We also need to use write
   buffering to avoid potential problems with interactions with some
   transport layers, details are given in the comment for the buffered write
   function.

   The layering looks as follows:

	--- httpRead ---+-- bufferedRead ---+--- tcpRead
		cmpRead							|
										+--- clibRead
										|
	------------------------------------+--- otherRead

	--- httpWrite --+-- bufferedWrite --+---- tcpWrite
		cmpWrite						|
										+---- clibWrite
										|
	------------------------------------+---- otherWrite

   When we allocate the readahead/write buffers we try and make them an
   optimal size to minimise unnecessary copying and not negatively affect
   network I/O.  If we make them too big, we'll have to move too much data
   around when we partially empty them.  If we make them too small, the
   buffering effect is suboptimal.  Since what we're buffering is PKI
   traffic, a 4K buffer should get most messages in one go.  This also
   matches many network stacks that use 4K I/O buffers, the BSD default */

#define NETWORK_BUFFER_SIZE		4096

/****************************************************************************
*																			*
*								Utility Functions							*
*																			*
****************************************************************************/

/* Copy error information from a cryptlib transport-layer session into a
   stream */

static int getSessionErrorInfo( STREAM *stream, const int errorStatus )
	{
	MESSAGE_DATA msgData;
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	status = krnlSendMessage( stream->iTransportSession,
							  IMESSAGE_GETATTRIBUTE, 
							  &stream->errorInfo->errorCode,
							  CRYPT_ATTRIBUTE_INT_ERRORCODE );
	if( cryptStatusError( status ) )
		stream->errorInfo->errorCode = CRYPT_OK;
	setMessageData( &msgData, stream->errorInfo->errorString, 
					MAX_ERRMSG_SIZE );
	krnlSendMessage( stream->iTransportSession, IMESSAGE_GETATTRIBUTE,
					 &msgData, CRYPT_ATTRIBUTE_INT_ERRORMESSAGE );
	return( errorStatus );
	}

#if defined( __WIN32__ ) && !defined( NDEBUG ) && 0

/* Code to test HTTP header parsing, call from just before the 
   openConnection() call in completeConnect() */

static int transportFileFunction( STREAM *stream, void *buffer,
								  const int length, const int flags )
	{
	FILE *filePtr = ( FILE * ) stream->callbackFunction;

	return( fread( buffer, 1, length, filePtr ) );
	}

static void testHttp( STREAM *stream )
	{
	STREAM streamCopy;
	ERROR_INFO errorInfoCopy;
	FILE *reportFile = stdout;
	void *buffer;
	int i, status;

	stream->protocol = STREAM_PROTOCOL_HTTP;
	stream->transportReadFunction = transportFileFunction;
	sioctl( stream, STREAM_IOCTL_HTTPREQTYPES, STREAM_NFLAG_HTTPGET );
	if( ( buffer = clAlloc( "", 16384 ) ) == NULL )
		{
		puts( "Out of memory." );
		return;
		}
#if 1
	reportFile = fopen( "r:/http_report.txt", "w" );
#endif
	if( reportFile == NULL )
		{
		printf( "Couldn't open file for report, hit a key." );
		getchar();
		putchar( '\n' );
		exit( EXIT_FAILURE );
		}
//	for( i = 1455; i <= 2000; i++ )
//	for( i = 0; i < 1000; i++ )
//	for( i = 1000; i < 1999; i++ )
//	for( i = 2000; i < 2999; i++ )
//	for( i = 3000; i < 3999; i++ )
	for( i = 0; i <= 3965; i++ )
		{
		FILE *filePtr;
		char fileName[ 128 ];

		sprintf( fileName, "d:/tmp/testcases/%08d", i );
		filePtr = fopen( fileName, "rb" );
		if( filePtr == NULL )
			{
			printf( "Failed to open file #%d, hit a key.", i );
			getchar();
			putchar( '\n' );
			continue;
			}
		memcpy( &streamCopy, stream, sizeof( STREAM ) );
		memcpy( &errorInfoCopy, stream->errorInfo, sizeof( ERROR_INFO ) );
		stream->callbackFunction = ( CALLBACKFUNCTION ) filePtr;/* Kludge */
		fprintf( reportFile, "%04d: ", i );
		if( reportFile != stdout )
			{
			if( !( i % 10 ) )
				putchar( '\n' );
			printf( "%04d ", i );
			}
		status = sread( stream, buffer, 16384 );
		fclose( filePtr );
		if( !cryptStatusError( status ) )
			fprintf( reportFile, 
					 "%d: cryptlib error: HTTP error not detected.\n", 
					 status );
		else
			fprintf( reportFile, "%d %s.\n", status, 
					 stream->errorInfo->errorString );
		fflush( reportFile );
		memcpy( stream, &streamCopy, sizeof( STREAM ) );
		memcpy( stream->errorInfo, &errorInfoCopy, sizeof( ERROR_INFO ) );
		}
	if( reportFile != stdout )
		fclose( reportFile );
	putchar( '\n' );
	}
#else
  #define testHttp( stream )
#endif /* Win32 debug build only */

/****************************************************************************
*																			*
*							URL Processing Functions						*
*																			*
****************************************************************************/

/* Parse a URI into:

	<schema>://[<user>@]<host>[:<port>]/<path>[?<query>] components

   This function is intended for use from the internal interface (i.e. to
   parse URLs supplied by the caller to the cryptlib API), and not so much
   for the external interface (i.e. URLs supplied by remote systems for
   processing by cryptlib).  Because of this it's rather more liberal with
   what it'll accept than a generic URL parser would be */

static int parseURL( URL_INFO *urlInfo, const char *url, const int urlLen,
					 const int defaultPort, const URL_TYPE urlTypeHint )
	{
	typedef struct {
		const char *schema;
		const int schemaLength;
		const URL_TYPE type;
		} URL_SCHEMA_INFO;
	static const URL_SCHEMA_INFO FAR_BSS urlSchemaInfo[] = {
		{ "http://", 7, URL_TYPE_HTTP },
		{ "https://", 8, URL_TYPE_HTTPS },
		{ "ssh://", 6, URL_TYPE_SSH },
		{ "scp://", 6, URL_TYPE_SSH },
		{ "sftp://", 7, URL_TYPE_SSH },
		{ "cmp://", 6, URL_TYPE_CMP },
		{ "tsp://", 6, URL_TYPE_TSP },
		{ NULL, 0, URL_TYPE_NONE }, { NULL, 0, URL_TYPE_NONE }
		};
	char *strPtr;
	int offset, length;

	assert( isWritePtr( urlInfo, sizeof( URL_INFO ) ) );
	assert( isReadPtr( url, urlLen ) );
	assert( defaultPort == CRYPT_UNUSED || \
			( defaultPort >= 22 && defaultPort <= 65536 ) );
	assert( urlTypeHint >= URL_TYPE_NONE && urlTypeHint < URL_TYPE_LAST );

	/* Clear return values */
	memset( urlInfo, 0, sizeof( URL_INFO ) );
	if( defaultPort != CRYPT_UNUSED )
		urlInfo->port = defaultPort;

	/* Skip leading and trailing whitespace and syntactic sugar */
	length = strStripWhitespace( &strPtr, url, urlLen );
	if( length <= 0 )
		return( CRYPT_ERROR_BADDATA );
	if( length >= MAX_URL_SIZE )
		return( CRYPT_ERROR_OVERFLOW );
	if( ( offset = strFindStr( strPtr, length, "://", 3 ) ) >= 0 )
		{
		int i;

		/* Extract the URI schema */
		urlInfo->schema = strPtr;
		urlInfo->schemaLen = offset + 3;
		length -= offset + 3;
		if( length <= 0 )
			return( CRYPT_ERROR_BADDATA );
		strPtr += offset + 3;
		length = strStripWhitespace( &strPtr, strPtr, length );
		if( length <= 0 )
			return( CRYPT_ERROR_BADDATA );

		/* Check whether the schema is one that we recognise */
		for( i = 0; 
			 urlSchemaInfo[ i ].type != URL_TYPE_NONE && \
				i < FAILSAFE_ARRAYSIZE( urlSchemaInfo, URL_SCHEMA_INFO ); 
			 i++ )
			{
			if( urlSchemaInfo[ i ].schemaLength == urlInfo->schemaLen && \
				!strCompare( urlSchemaInfo[ i ].schema, urlInfo->schema,
							 urlInfo->schemaLen ) )
				break;
			}
		if( i >= FAILSAFE_ARRAYSIZE( urlSchemaInfo, URL_SCHEMA_INFO ) )
			retIntError();
		urlInfo->type = urlSchemaInfo[ i ].type;

		/* If there's a URL hint given, make sure that the URL type matches */
		if( urlTypeHint != URL_TYPE_NONE )
			{
			switch( urlTypeHint )
				{
				case URL_TYPE_HTTP:
					/* An explicit HTTP URL must really be HTTP and not 
					   just a generic HTTP/HTTPS mix */
					if( urlInfo->type != URL_TYPE_HTTP )
						return( CRYPT_ERROR_BADDATA );
					break;

				case URL_TYPE_HTTPS:
					/* A requirement for an HTTPS URL can also match an HTTP
					   URL - this type is used for SSL, where the use of 
					   HTTPS is implied by the fact that an SSL session is 
					   being used even if it's a straight HTTP URL */
					if( urlInfo->type != URL_TYPE_HTTP && \
						urlInfo->type != URL_TYPE_HTTPS )
						return( CRYPT_ERROR_BADDATA );
					break;

				case URL_TYPE_SSH:
					if( urlInfo->type != URL_TYPE_SSH )
						return( CRYPT_ERROR_BADDATA );
					break;

				case URL_TYPE_CMP:
					if( urlInfo->type != URL_TYPE_CMP )
						return( CRYPT_ERROR_BADDATA );
					break;

				case URL_TYPE_TSP:
					if( urlInfo->type != URL_TYPE_TSP )
						return( CRYPT_ERROR_BADDATA );
					break;

				default:
					retIntError();
				}
			}
		}

	/* Check for user info before an '@' sign */
	if( ( offset = strFindCh( strPtr, length, '@' ) ) >= 0 )
		{
		/* Extract the user info */
		urlInfo->userInfoLen = \
					strStripWhitespace( ( char ** ) &urlInfo->userInfo,
										strPtr, offset );
		length -= offset + 1;
		if( length <= 0 || urlInfo->userInfoLen <= 0 )
			return( CRYPT_ERROR_BADDATA );
		strPtr += offset + 1;
		length = strStripWhitespace( &strPtr, strPtr, length );
		if( length <= 0 )
			return( CRYPT_ERROR_BADDATA );
		}

	/* IPv6 addresses use colons in their string representation, RFC 2732
	   requires that IPv6 addresses in URLs be delimited by square brackets
	   so if we find one at the start of the URI we treat it as an IPv6
	   address */
	if( *strPtr == '[' && \
		( length != 12 || strCompareZ( strPtr, "[Autodetect]" ) ) )
		{
		/* Strip the leading '[' delimiter */
		length = strStripWhitespace( &strPtr, strPtr + 1, length - 1 );
		if( length <= 0 )
			return( CRYPT_ERROR_BADDATA );

		/* Locate the end of the RFC 2732 IPv6 address.  Trailing whitespace
		   will be stripped later */
		if( ( offset = strFindCh( strPtr, length, ']' ) ) <= 0 )
			return( CRYPT_ERROR_BADDATA );
		urlInfo->host = strPtr;
		urlInfo->hostLen = offset;
		strPtr += offset + 1;
		length -= offset + 1;
		}
	else
		{
		int offset2;

		/* It's a non-IPv6 host name, check whether there's anything
		   following the name */
		urlInfo->host = strPtr;
		offset = strFindCh( strPtr, length, ':' );
		offset2 = strFindCh( strPtr, length, '/' );
		if( offset < 0 )
			offset = offset2;
		else
			{
			assert( offset >= 0 );
			if( offset2 >= 0 )
				offset = min( offset, offset2 );
			}
		if( offset <= 0 )
			{
			/* It's a standalone server name, we're done */
			urlInfo->hostLen = length;
			return( CRYPT_OK );
			}

		/* There's port/location info following the server name.  Trailing
		   whitespace will be stripped later */
		urlInfo->hostLen = offset;
		strPtr += offset;
		length -= offset;
		}
	urlInfo->hostLen = strStripWhitespace( ( char ** ) &urlInfo->host,
										   urlInfo->host, urlInfo->hostLen );
	if( urlInfo->hostLen <= 0 )
		return( CRYPT_ERROR_BADDATA );

	/* If there's nothing beyond the host name, we're done */
	if( length <= 0 )
		return( CRYPT_OK );
	length = strStripWhitespace( &strPtr, strPtr, length );
	if( length <= 0 )
		return( CRYPT_ERROR_BADDATA );

	/* Parse the remainder of the URI into port/location */
	if( *strPtr == ':' )
		{
		char portBuffer[ 16 + 8 ];
		const int portStrLen = min( length - 1, 15 );
		int port;

		/* Get the port to connect to.  If it's an invalid port we ignore it
		   and use the default one, which was set earlier */
		if( portStrLen <= 0 )
			return( CRYPT_ERROR_BADDATA );
		memcpy( portBuffer, strPtr + 1, portStrLen );
		portBuffer[ portStrLen ] = '\0';
		port = atoi( portBuffer );
		if( port >= 22 && port < 65535 )
			urlInfo->port = port;
		}
	if( ( offset = strFindCh( strPtr, length, '/' ) ) >= 0 )
		{
		const int locationLength = length - offset;

		if( locationLength <= 0 )
			return( CRYPT_ERROR_BADDATA );
		urlInfo->locationLen = \
					strStripWhitespace( ( char ** ) &urlInfo->location,
										strPtr + offset, locationLength );
		if( urlInfo->locationLen <= 0 )
			return( CRYPT_ERROR_BADDATA );
		}

	return( CRYPT_OK );
	}

/* Copy parsed URL info to a stream structure */

static int copyUrlToStream( STREAM *stream, const URL_INFO *urlInfo )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( urlInfo, sizeof( URL_INFO ) ) );

	if( ( stream->host = clAlloc( "copyUrlToStream", \
								  urlInfo->hostLen ) ) == NULL )
		return( CRYPT_ERROR_MEMORY );
	memcpy( stream->host, urlInfo->host, urlInfo->hostLen );
	stream->hostLen = urlInfo->hostLen;
	if( urlInfo->location != NULL )
		{
		if( ( stream->path = \
				clAlloc( "copyUrlToStream", urlInfo->locationLen ) ) == NULL )
			{
			clFree( "copyUrlToStream", stream->host );
			return( CRYPT_ERROR_MEMORY );
			}
		memcpy( stream->path, urlInfo->location, urlInfo->locationLen );
		stream->pathLen = urlInfo->locationLen;
		}
	stream->port = urlInfo->port;

	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Transport-layer Functions						*
*																			*
****************************************************************************/

/* Map the upper-layer I/O functions directly to the transport-layer
   equivalent.  This is used if we're performing raw I/O without any
   intermediate protocol layers or buffering */

static int transportDirectReadFunction( STREAM *stream, void *buffer,
										const int length )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( buffer, length ) );

	return( stream->transportReadFunction( stream, buffer, length,
										   TRANSPORT_FLAG_NONE ) );
	}

static int transportDirectWriteFunction( STREAM *stream, const void *buffer,
										 const int length )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( buffer, length ) );

	return( stream->transportWriteFunction( stream, buffer, length,
											TRANSPORT_FLAG_NONE ) );
	}

static int setStreamLayerDirect( STREAM *stream )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	stream->writeFunction = transportDirectWriteFunction;
	stream->readFunction = transportDirectReadFunction;

	return( CRYPT_OK );
	}

/* Send and receive data with a cryptlib session as the transport layer */

static int transportSessionConnectFunction( STREAM *stream,
											const char *host, const int hostLen,
											const int port )
	{
	int isActive, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( host == NULL && hostLen == 0 );
	assert( port == 0 );

	/* If the transport session hasn't been activated yet, activate it now */
	status = krnlSendMessage( stream->iTransportSession,
							  IMESSAGE_GETATTRIBUTE, &isActive,
							  CRYPT_SESSINFO_ACTIVE );
	if( cryptStatusOK( status ) && isActive )
		return( CRYPT_OK );
	status = krnlSendMessage( stream->iTransportSession,
							  IMESSAGE_SETATTRIBUTE, MESSAGE_VALUE_TRUE,
							  CRYPT_SESSINFO_ACTIVE );
	if( cryptStatusError( status ) )
		return( getSessionErrorInfo( stream, status ) );
	return( CRYPT_OK );
	}

static void transportSessionDisconnectFunction( STREAM *stream,
												const BOOLEAN fullDisconnect )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	krnlSendNotifier( stream->iTransportSession, IMESSAGE_DECREFCOUNT );
	}

static BOOLEAN transportSessionOKFunction( void )
	{
	return( TRUE );
	}

static int transportSessionReadFunction( STREAM *stream, BYTE *buffer,
										 const int length, const int flags )
	{
	MESSAGE_DATA msgData;
	int newTimeout = CRYPT_UNUSED, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( buffer, length ) );

	/* Read data from the session, overriding the timeout handling if
	   requested */
	if( ( flags & TRANSPORT_FLAG_NONBLOCKING ) && stream->timeout > 0 )
		newTimeout = 0;
	else
		if( ( flags & TRANSPORT_FLAG_BLOCKING ) && stream->timeout == 0 )
			newTimeout = 30;
	if( newTimeout != CRYPT_UNUSED )
		krnlSendMessage( stream->iTransportSession, IMESSAGE_SETATTRIBUTE,
						 &newTimeout, CRYPT_OPTION_NET_READTIMEOUT );
	setMessageData( &msgData, buffer, length );
	status = krnlSendMessage( stream->iTransportSession, IMESSAGE_ENV_POPDATA,
							  &msgData, 0 );
	if( newTimeout != CRYPT_UNUSED )
		krnlSendMessage( stream->iTransportSession, IMESSAGE_SETATTRIBUTE,
						 &stream->timeout, CRYPT_OPTION_NET_READTIMEOUT );
	if( cryptStatusError( status ) )
		return( getSessionErrorInfo( stream, status ) );
	if( msgData.length < length )
		retExt( CRYPT_ERROR_READ,
				( CRYPT_ERROR_READ, STREAM_ERRINFO, 
				  "Only read %d out of %d bytes via cryptlib session "
				  "object", msgData.length, length ) );
	return( length );
	}

static int transportSessionWriteFunction( STREAM *stream, const BYTE *buffer,
										  const int length, const int flags )
	{
	MESSAGE_DATA msgData;
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( buffer, length ) );

	setMessageData( &msgData, ( void * ) buffer, length );
	status = krnlSendMessage( stream->iTransportSession,
							  IMESSAGE_ENV_PUSHDATA, &msgData, 0 );
	if( cryptStatusOK( status ) )
		{
		setMessageData( &msgData, NULL, 0 );
		status = krnlSendMessage( stream->iTransportSession,
								  IMESSAGE_ENV_PUSHDATA, &msgData, 0 );
		}
	if( cryptStatusError( status ) )
		return( getSessionErrorInfo( stream, status ) );
	return( CRYPT_OK );
	}

/****************************************************************************
*																			*
*							Proxy Management Functions						*
*																			*
****************************************************************************/

/* Open a connection through a Socks proxy.  This is currently disabled
   since it doesn't appear to be used by anyone */

#if 0

static int connectViaSocksProxy( STREAM *stream )
	{
	MESSAGE_DATA msgData;
	BYTE socksBuffer[ 64 + CRYPT_MAX_TEXTSIZE + 8 ], *bufPtr = socksBuffer;
	char userName[ CRYPT_MAX_TEXTSIZE + 8 ];
	int length, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	/* Get the SOCKS user name, defaulting to "cryptlib" if there's none
	   set */
	setMessageData( &msgData, userName, CRYPT_MAX_TEXTSIZE );
	status = krnlSendMessage( DEFAULTUSER_OBJECT_HANDLE,
							  IMESSAGE_GETATTRIBUTE_S, &msgData,
							  CRYPT_OPTION_NET_SOCKS_USERNAME );
	if( cryptStatusOK( status ) )
		userName[ msgData.length ] = '\0';
	else
		strlcpy_s( userName, CRYPT_MAX_TEXTSIZE, "cryptlib" );

	/* Build up the SOCKSv4 request string:

		BYTE: version = 4
		BYTE: command = 1 (connect)
		WORD: port
		LONG: IP address
		STRING: userName + '\0'

	   Note that this has a potential problem in that it requires a DNS 
	   lookup by the client, which can lead to problems if the client
	   can't get DNS requests out because only SOCKSified access is allowed.
	   A related problem is when SOCKS is being used as a tunnelling
	   interface, because the DNS lookup will communicate data about the 
	   client to an observer outside the tunnel.

	   To work around this, there's a so-called SOCKSv4a protocol that
	   has the SOCKS proxy perform the lookup:

		BYTE: version = 4
		BYTE: command = 1 (connect)
		WORD: port
		LONG: IP address = 0x00 0x00 0x00 0xFF
		STRING: userName + '\0'
		STRING: FQDN + '\0'

	   Unfortunately there's no way to tell whether a SOCKS server supports
	   4a or only 4, but in any case since SOCKS support is currently 
	   disabled we leave the poke-and-hope 4a detection until such time as
	   someone actually requests it */
	*bufPtr++ = 4; *bufPtr++ = 1;
	mputWord( bufPtr, stream->port );
	status = getIPAddress( stream, bufPtr, stream->host );
	strlcpy_s( bufPtr + 4, CRYPT_MAX_TEXTSIZE, userName );
	length = 1 + 1 + 2 + 4 + strlen( userName ) + 1;
	if( cryptStatusError( status ) )
		{
		stream->transportDisconnectFunction( stream, TRUE );
		return( status );
		}

	/* Send the data to the server and read back the reply */
	status = stream->transportWriteFunction( stream, socksBuffer, length,
											 TRANSPORT_FLAG_FLUSH );
	if( cryptStatusOK( status ) )
		status = stream->transportReadFunction( stream, socksBuffer, 8,
												TRANSPORT_FLAG_BLOCKING );
	if( cryptStatusError( status ) )
		{
		/* The involvement of a proxy complicates matters somewhat because
		   we can usually connect to the proxy OK but may run into problems
		   going from the proxy to the remote server, so if we get an error
		   at this stage (which will typically show up as a read error from
		   the proxy) we report it as an open error instead */
		if( status == CRYPT_ERROR_READ || status == CRYPT_ERROR_COMPLETE )
			status = CRYPT_ERROR_OPEN;
		stream->transportDisconnectFunction( stream, TRUE );
		return( status );
		}

	/* Make sure that everything is OK:

		BYTE: null = 0
		BYTE: status = 90 (OK)
		WORD: port
		LONG: IP address */
	if( socksBuffer[ 1 ] != 90 )
		{
		int i;

		stream->transportDisconnectFunction( stream, TRUE );
		strlcpy_s( stream->errorInfo->errorString, MAX_ERRMSG_SIZE, 
				   "Socks proxy returned" );
		for( i = 0; i < 8; i++ )
			sprintf_s( stream->errorInfo->errorString + 20 + ( i * 3 ),
					   MAX_ERRMSG_SIZE - ( 20 + ( i * 3 ) ), " %02X", 
					   socksBuffer[ i ] );
		strlcat_s( stream->errorInfo->errorString, MAX_ERRMSG_SIZE, "." );
		stream->errorCode = socksBuffer[ 1 ];
		return( CRYPT_ERROR_OPEN );
		}

	return( CRYPT_OK );
	}
#endif /* 0 */

static int connectViaHttpProxy( STREAM *stream, ERROR_INFO *errorInfo )
	{
	BYTE buffer[ 64 + 8 ];
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( errorInfo, sizeof( ERROR_INFO ) ) );

	/* Open the connection via the proxy.  To do this we temporarily layer
	   HTTP I/O over the TCP I/O, then once the proxy messaging has been
	   completely we re-set the stream to pure TCP I/O and clear any stream
	   flags that were set during the proxying */
	setStreamLayerHTTP( stream );
	status = stream->writeFunction( stream, "", 0 );
	if( cryptStatusOK( status ) )
		status = stream->readFunction( stream, buffer, 64 );
	setStreamLayerDirect( stream );
	stream->flags = 0;
	if( cryptStatusError( status ) )
		{
		/* The involvement of a proxy complicates matters somewhat because
		   we can usually connect to the proxy OK but may run into problems
		   going from the proxy to the remote server, so if we get an error
		   at this stage (which will typically show up as a read error from
		   the proxy) we report it as an open error instead */
		if( status == CRYPT_ERROR_READ || status == CRYPT_ERROR_COMPLETE )
			status = CRYPT_ERROR_OPEN;
		errorInfo->errorCode = stream->errorInfo->errorCode;
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
				   stream->errorInfo->errorString );
		stream->transportDisconnectFunction( stream, TRUE );
		}
	return( status );
	}

/* Try and auto-detect HTTP proxy information */

#if defined( __WIN32__ )

/* The autoproxy functions were only documented in WinHTTP 5.1, so we have to
   provide the necessary defines and structures ourselves */

#ifndef WINHTTP_ACCESS_TYPE_DEFAULT_PROXY

#define HINTERNET	HANDLE

typedef struct {
	DWORD dwFlags;
	DWORD dwAutoDetectFlags;
	LPCWSTR lpszAutoConfigUrl;
	LPVOID lpvReserved;
	DWORD dwReserved;
	BOOL fAutoLogonIfChallenged;
	} WINHTTP_AUTOPROXY_OPTIONS;

typedef struct {
	DWORD dwAccessType;
	LPWSTR lpszProxy;
	LPWSTR lpszProxyBypass;
	} WINHTTP_PROXY_INFO;

typedef struct {
	BOOL fAutoDetect;
	LPWSTR lpszAutoConfigUrl;
	LPWSTR lpszProxy;
	LPWSTR lpszProxyBypass;
	} WINHTTP_CURRENT_USER_IE_PROXY_CONFIG;

#define WINHTTP_AUTOPROXY_AUTO_DETECT	1
#define WINHTTP_AUTO_DETECT_TYPE_DHCP	1
#define WINHTTP_AUTO_DETECT_TYPE_DNS_A	2
#define WINHTTP_ACCESS_TYPE_NO_PROXY	1
#define WINHTTP_NO_PROXY_NAME			NULL
#define WINHTTP_NO_PROXY_BYPASS			NULL

#endif /* WinHTTP 5.1 defines and structures */

typedef HINTERNET ( *WINHTTPOPEN )( LPCWSTR pwszUserAgent, DWORD dwAccessType,
									LPCWSTR pwszProxyName, LPCWSTR pwszProxyBypass,
									DWORD dwFlags );
typedef BOOL ( *WINHTTPGETDEFAULTPROXYCONFIGURATION )( WINHTTP_PROXY_INFO* pProxyInfo );
typedef BOOL ( *WINHTTPGETIEPROXYCONFIGFORCURRENTUSER )(
								WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *pProxyConfig );
typedef BOOL ( *WINHTTPGETPROXYFORURL )( HINTERNET hSession, LPCWSTR lpcwszUrl,
										 WINHTTP_AUTOPROXY_OPTIONS *pAutoProxyOptions,
										 WINHTTP_PROXY_INFO *pProxyInfo );
typedef BOOL ( *WINHTTPCLOSEHANDLE )( HINTERNET hInternet );

static int findProxyUrl( char *proxy, const int proxyMaxLen, 
						 const char *url, const int urlLen )
	{
	static HMODULE hWinHTTP = NULL;
	static WINHTTPOPEN pWinHttpOpen = NULL;
	static WINHTTPGETDEFAULTPROXYCONFIGURATION pWinHttpGetDefaultProxyConfiguration = NULL;
	static WINHTTPGETIEPROXYCONFIGFORCURRENTUSER pWinHttpGetIEProxyConfigForCurrentUser = NULL;
	static WINHTTPGETPROXYFORURL pWinHttpGetProxyForUrl = NULL;
	static WINHTTPCLOSEHANDLE pWinHttpCloseHandle = NULL;
	WINHTTP_AUTOPROXY_OPTIONS autoProxyOptions = \
			{ WINHTTP_AUTOPROXY_AUTO_DETECT,
			  WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A,
			  NULL, NULL, 0, FALSE };
	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieProxyInfo;
	WINHTTP_PROXY_INFO proxyInfo;
	HINTERNET hSession;
	char urlBuffer[ MAX_DNS_SIZE + 1 + 8 ];
	wchar_t unicodeURL[ MAX_DNS_SIZE + 1 + 8 ];
	size_t unicodeUrlLen, proxyLen;
	int offset = 0, proxyStatus;

	assert( isWritePtr( proxy, proxyMaxLen ) );
	assert( isReadPtr( url, urlLen ) );

	/* Under Win2K SP3, XP and 2003 (or at least Windows versions with
	   WinHTTP 5.1 installed in some way, it officially shipped with the
	   versions mentioned earlier) we can use WinHTTP AutoProxy support,
	   which implements the Web Proxy Auto-Discovery (WPAD) protocol from
	   an internet draft that expired in May 2001.  Under older versions of
	   Windows we have to use the WinINet InternetGetProxyInfo, however this
	   consists of a ghastly set of kludges that were never meant to be
	   exposed to the outside world (they were only crowbarred out of MS
	   as part of the DoJ consent decree), and user experience with them is
	   that they don't really work except in the one special way in which
	   MS-internal code calls them.  Since we don't know what this is, we
	   use the WinHTTP functions instead */
	if( hWinHTTP == NULL )
		{
		if( ( hWinHTTP = LoadLibrary( "WinHTTP.dll" ) ) == NULL )
			return( CRYPT_ERROR_NOTFOUND );

		pWinHttpOpen = ( WINHTTPOPEN ) \
						GetProcAddress( hWinHTTP, "WinHttpOpen" );
		pWinHttpGetDefaultProxyConfiguration = ( WINHTTPGETDEFAULTPROXYCONFIGURATION ) \
						GetProcAddress( hWinHTTP, "WinHttpGetDefaultProxyConfiguration" );
		pWinHttpGetIEProxyConfigForCurrentUser = ( WINHTTPGETIEPROXYCONFIGFORCURRENTUSER ) \
						GetProcAddress( hWinHTTP, "WinHttpGetIEProxyConfigForCurrentUser" );
		pWinHttpGetProxyForUrl = ( WINHTTPGETPROXYFORURL ) \
						GetProcAddress( hWinHTTP, "WinHttpGetProxyForUrl" );
		pWinHttpCloseHandle = ( WINHTTPCLOSEHANDLE ) \
						GetProcAddress( hWinHTTP, "WinHttpCloseHandle" );
		if( pWinHttpOpen == NULL || pWinHttpGetProxyForUrl == NULL || \
			pWinHttpCloseHandle == NULL )
			{
			FreeLibrary( hWinHTTP );
			return( CRYPT_ERROR_NOTFOUND );
			}
		}

	/* Autoproxy discovery using WinHttpGetProxyForUrl() can be awfully slow,
	   often taking several seconds, since it requires probing for proxy info
	   first using DHCP and then if that fails using DNS.  Since this is done
	   via a blocking call, everything blocks while it's in progress.  To
	   help mitigate this, we try for proxy info direct from the registry if
	   it's available, avoiding the lengthy autodiscovery process.  This also
	   means that discovery will work if no auto-discovery support is present,
	   for example on servers where the admin has set the proxy config
	   directly with ProxyCfg.exe */
	if( pWinHttpGetDefaultProxyConfiguration != NULL && \
		pWinHttpGetDefaultProxyConfiguration( &proxyInfo ) && \
		proxyInfo.lpszProxy != NULL )
		{
		proxyStatus = wcstombs_s( &proxyLen, proxy, proxyMaxLen,
								  proxyInfo.lpszProxy, MAX_DNS_SIZE );
		GlobalFree( proxyInfo.lpszProxy );
		if( proxyInfo.lpszProxyBypass != NULL )
			GlobalFree( proxyInfo.lpszProxy );
		if( proxyStatus == 0 )
			return( proxyLen );
		}

	/* The next fallback is to get the proxy info from MSIE.  This is also
	   usually much quicker than WinHttpGetProxyForUrl(), although sometimes
	   it seems to fall back to that, based on the longish delay involved.
	   Another issue with this is that it won't work in a service process
	   that isn't impersonating an interactive user (since there isn't a
	   current user), but in that case we just fall back to
	   WinHttpGetProxyForUrl() */
	if( pWinHttpGetIEProxyConfigForCurrentUser != NULL && \
		pWinHttpGetIEProxyConfigForCurrentUser( &ieProxyInfo ) )
		{
		proxyStatus = wcstombs_s( &proxyLen, proxy, proxyMaxLen,
								  ieProxyInfo.lpszProxy, MAX_DNS_SIZE );
		if( ieProxyInfo.lpszAutoConfigUrl != NULL )
			GlobalFree( ieProxyInfo.lpszAutoConfigUrl );
		if( ieProxyInfo.lpszProxy != NULL )
			GlobalFree( ieProxyInfo.lpszProxy );
		if( ieProxyInfo.lpszProxyBypass != NULL )
			GlobalFree( ieProxyInfo.lpszProxyBypass );
		if( proxyStatus == 0 )
			return( proxyLen );
		}

	/* WinHttpGetProxyForUrl() requires a schema for the URL that it's
	   performing a lookup on, if the URL doesn't contain one we use a
	   default value of "http://".  In addition we need to convert the
	   raw octet string into a null-terminated string for the mbstowcs()
	   Unicode conversion and following WinHttpGetProxyForUrl() lookup */
	if( strFindStr( url, urlLen, "://", 3 ) < 0 )
		{
		strlcpy_s( urlBuffer, MAX_DNS_SIZE, "http://" );
		offset = 7;
		}
	memcpy( urlBuffer + offset, url, min( urlLen, MAX_DNS_SIZE - offset ) );
	urlBuffer[ offset + min( urlLen, MAX_DNS_SIZE - offset ) ] = '\0';

	/* Locate the proxy used for accessing the resource at the supplied URL.
	   We have to convert to and from Unicode because the WinHTTP functions
	   all take Unicode strings as args.  Note that we use the libc widechar
	   functions rather than the Windows ones since the latter aren't
	   present in Win95 or Win98.

	   WinHttpGetProxyForUrl() can be rather flaky, in some cases it'll fail
	   instantly (without even trying auto-discovery) with GetLastError() =
	   87 (parameter error), but then calling it again some time later works
	   fine.  Because of this we leave it as the last resort after trying
	   all of the other get-proxy mechanisms */
	hSession = pWinHttpOpen( L"cryptlib/1.0",
							 WINHTTP_ACCESS_TYPE_NO_PROXY,
							 WINHTTP_NO_PROXY_NAME,
							 WINHTTP_NO_PROXY_BYPASS, 0 );
	if( hSession == NULL )
		return( CRYPT_ERROR_NOTFOUND );
	if( mbstowcs_s( &unicodeUrlLen, unicodeURL, MAX_DNS_SIZE,
					urlBuffer, MAX_DNS_SIZE ) != 0 )
		{
		pWinHttpCloseHandle( hSession );
		return( CRYPT_ERROR_NOTFOUND );
		}
	unicodeURL[ unicodeUrlLen ] = L'\0';
	proxyStatus = 0;
	memset( &proxyInfo, 0, sizeof( WINHTTP_PROXY_INFO ) );
	if( pWinHttpGetProxyForUrl( hSession, unicodeURL, &autoProxyOptions,
								&proxyInfo ) == TRUE )
		{
		proxyStatus = wcstombs_s( &proxyLen, proxy, proxyMaxLen,
								  proxyInfo.lpszProxy, MAX_DNS_SIZE );
		GlobalFree( proxyInfo.lpszProxy );
		if( proxyInfo.lpszProxyBypass != NULL )
			GlobalFree( proxyInfo.lpszProxy );
		}
	pWinHttpCloseHandle( hSession );
	if( proxyStatus != 0 )
		return( CRYPT_ERROR_NOTFOUND );
	return( proxyLen );
	}

#if 0

typedef BOOL ( WINAPI *INTERNETGETPROXYINFO )( LPCSTR lpszUrl, DWORD dwUrlLength,
							LPSTR lpszUrlHostName, DWORD dwUrlHostNameLength,
							LPSTR* lplpszProxyHostName,
							LPDWORD lpdwProxyHostNameLength );
typedef BOOL ( WINAPI *INTERNETINITIALIZEAUTOPROXYDLL )( DWORD dwVersion,
							LPSTR lpszDownloadedTempFile, LPSTR lpszMime,
							AutoProxyHelperFunctions* lpAutoProxyCallbacks,
							LPAUTO_PROXY_SCRIPT_BUFFER lpAutoProxyScriptBuffer );

static int findProxyUrl( char *proxy, const int proxyMaxLen, 
						 const char *url, const int urlLen )
	{
	static INTERNETGETPROXYINFO pInternetGetProxyInfo = NULL;
	static INTERNETINITIALIZEAUTOPROXYDLL pInternetInitializeAutoProxyDll = NULL;
	URL_INFO urlInfo;
	char urlHost[ MAX_DNS_SIZE + 8 ];
	char *proxyHost = NULL;
	int proxyHostLen, status;

	/* This gets somewhat complicated, under Win2K SP3, XP and 2003 (or at
	   least Windows versions with WinHTTP 5.1 installed in some way, it
	   officially shipped with the versions mentioned earlier) we can use
	   WinHTTP AutoProxy support, which implements the Web Proxy Auto-
	   Discovery (WPAD) protocol from an internet draft that expired in May
	   2001.  Under older versions of Windows we have to use the WinINet
	   InternetGetProxyInfo.

	   These functions were never meant to be used by the general public
	   (see the comment below), so they work in an extremely peculiar way
	   and only with the exact calling sequence that's used by MS code - it
	   looks like they were only intended as components of Windows-internal
	   implementation of proxy support, since they require manual handling
	   of proxy config script downloading, parsing, and all manner of other
	   stuff that really doesn't concern us.  Because of the extreme
	   difficulty in doing anything with these functions, we use the WinHTTP
	   approach instead */
	if( pInternetGetProxyInfo == NULL )
		{
		HMODULE hModJS;

		if( ( hModJS = LoadLibrary( "JSProxy.dll" ) ) == NULL )
			return( CRYPT_ERROR_NOTFOUND );

		pInternetGetProxyInfo = ( INTERNETGETPROXYINFO ) \
					GetProcAddress( hModJS, "InternetGetProxyInfo" );
		pInternetInitializeAutoProxyDll = ( INTERNETINITIALIZEAUTOPROXYDLL ) \
					GetProcAddress( hModJS, "InternetInitializeAutoProxyDll" );
		if( pInternetGetProxyInfo == NULL || \
			pInternetInitializeAutoProxyDll == NULL )
			{
			FreeLibrary( hModJS );
			return( CRYPT_ERROR_NOTFOUND );
			}

		pInternetInitializeAutoProxyDll( 0, TempFile, NULL,
										 &HelperFunctions, NULL )
		}

	/* InternetGetProxyInfo() is a somewhat screwball undocumented function
	   that was crowbarred out of MS as part of the DoJ consent decree.  It
	   takes as input four parameters that do the work of a single
	   parameter, the null-terminated target URL string.  The documentation
	   for the function was initially wrong, but has been partially
	   corrected in places after user complaints, there are still missing
	   parts, as well as possible errors (why is it necessary to specify a
	   length for a supposedly null-terminated string?).  In order to meet
	   the strange input-parameter requirements, we have to pre-parse the
	   target URL in order to provide the various bits and pieces that
	   InternetGetProxyInfo() requires */
	status = parseURL( &urlInfo, url, strlen( url ), 80, URL_TYPE_HTTP );
	if( cryptStatusError( status ) )
		return( status );
	if( urlInfo.hostLen > MAX_DNS_SIZE )
		return( CRYPT_ERROR_OVERFLOW );
	memcpy( urlHost, urlInfo.host, urlInfo.hostLen );
	urlHost[ urlInfo.hostLen ] = '\0';
	if( !pInternetGetProxyInfo( url, strlen( url ), urlHost, urlInfo.hostLen,
								&proxyHost, &proxyHostLen ) )
		return( CRYPT_ERROR_NOTFOUND );
	memcpy( proxy, proxyHost, proxyHostLen );
	proxy[ proxyHostLen ] = '\0';
	GlobalFree( proxyHost );
	return( CRYPT_OK );
	}
#endif

#else
  #define findProxyUrl( proxy, proxyMaxLen, url, urlLen )	CRYPT_ERROR_NOTFOUND
#endif /* __WIN32__ */

/****************************************************************************
*																			*
*								Buffering Functions							*
*																			*
****************************************************************************/

/* Buffered transport-layer read function.  This sits on top of the
   transport-layer read function and performs speculative read-ahead
   buffering to improve performance in protocols such as HTTP that have to
   read a byte at a time in places:

		   bPos		   bEnd
			|			|
			v			v
	+-------+-----------+-------+
	|		|///////////|		|
	+-------+-----------+-------+
			 -- Read -->

   We fill the buffer to bEnd, then empty it advancing bPos until there isn't
   enough data left to satisfy the read, whereupon we move the data down and
   refill from bEnd:

   bPos		   bEnd
	|			|
	v			v
	+-----------+---------------+
	|///////////|				|
	+-----------+---------------+
				 -- Write -->	  */

static int bufferedTransportReadFunction( STREAM *stream, BYTE *buffer,
										  const int length, const int flags )
	{
	const int bytesLeft = stream->bufEnd - stream->bufPos;
	int bytesToRead, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isWritePtr( buffer, length ) );
	assert( length > 0 );
	assert( bytesLeft >= 0 );

	/* If there's enough data in the buffer to satisfy the request, return it
	   directly */
	if( length <= bytesLeft )
		{
		if( length == 1 )
			/* Optimisation for char-at-a-time HTTP header reads */
			*buffer = stream->buffer[ stream->bufPos++ ];
		else
			{
			memcpy( buffer, stream->buffer + stream->bufPos, length );
			stream->bufPos += length;
			}
		assert( stream->bufPos <= stream->bufEnd );
		return( length );
		}

	/* We're about to refill the buffer, if there's a gap at the start move
	   everything down to make room for the new data */
	if( stream->bufPos > 0 )
		{
		if( bytesLeft > 0 )
			memmove( stream->buffer, stream->buffer + stream->bufPos,
					 bytesLeft );
		stream->bufEnd = bytesLeft;
		stream->bufPos = 0;
		}

	assert( stream->bufPos == 0 );
	assert( length > bytesLeft );

	/* If there's more room in the buffer, refill it */
	if( stream->bufEnd < stream->bufSize )
		{
		int bytesRead;

		/* Perform an explicitly blocking read of as many bytes as we can/are
		   asked for.  Since there may be data already present from an
		   earlier speculative read, we only read as much as we need to
		   fulfill the request */
		bytesRead = stream->transportReadFunction( stream,
										stream->buffer + stream->bufEnd,
										min( length - bytesLeft, \
											 stream->bufSize - stream->bufEnd ),
										TRANSPORT_FLAG_BLOCKING );
		if( cryptStatusError( bytesRead ) )
			return( bytesRead );
		stream->bufEnd += bytesRead;

		/* If there's room for more, perform a second, nonblocking read for
		   whatever might still be there.  An error at this point isn't
		   fatal since this was only a speculative read  */
		if( stream->bufEnd < stream->bufSize )
			{
			bytesRead = stream->transportReadFunction( stream,
										stream->buffer + stream->bufEnd,
										stream->bufSize - stream->bufEnd,
										TRANSPORT_FLAG_NONBLOCKING );
			if( !cryptStatusError( bytesRead ) )
				stream->bufEnd += bytesRead;
			}
		}
	assert( stream->bufEnd <= stream->bufSize );

	/* Read as much as we can from the buffer */
	bytesToRead = min( length, stream->bufEnd );
	memcpy( buffer, stream->buffer, bytesToRead );
	stream->bufPos += bytesToRead;
	assert( stream->bufPos <= stream->bufEnd );

	/* If we could satisfy the read from the buffer, we're done */
	if( length <= bytesToRead )
		return( length );

	/* We're drained the stream buffer and there's more to go, read it
	   directly into the caller's buffer */
	status = stream->transportReadFunction( stream,
								buffer + bytesToRead, length - bytesToRead,
								TRANSPORT_FLAG_BLOCKING );
	return( cryptStatusError( status ) ? status : status + bytesToRead );
	}

/* Buffered transport-layer write function.  This sits on top of the
   transport-layer write function and combines two (or more, although in
   practice only two ever occur) writes into a single write.  The reason for
   this is that when using TCP transport the delayed-ACK handling means
   that performing two writes followed by a read (typical for HTTP and CMP
   messages) leads to very poor performance, usually made even worse by TCP
   slow-start.

   The reason for this is that the TCP MSS is typically 1460 bytes on a LAN
   (Ethernet) or 512/536 bytes on a WAN, while HTTP headers are ~200-300
   bytes, far less than the MSS.  When an HTTP message is first sent, the
   TCP congestion window begins at one segment, with the TCP slow-start then
   doubling its size for each ACK.  Sending the headers separately will
   send one short segment and a second MSS-size segment, whereupon the TCP
   stack will wait for the responder's ACK before continuing.  The responder
   gets both segments, then delays its ACK for 200ms in the hopes of
   piggybacking it on responder data, which is never sent since it's still
   waiting for the rest of the HTTP body from the initiator.  As a result,
   this results in a 200ms (+ assorted RTT) delay in each message sent.

   There is a somewhat related situation that occurs as a result of TCP
   slow-start and that can't be avoided programmatically in which we can't
   send more than a single request initially, however most BSD-derived
   implementations set the server's congestion window to two segments in
   response to receiving the TCP handshake ACK, so for the initial message
   exchange the client can send a request of 1MSS and the server a response
   of 2MSS without running into congestion-control problems.

   A related problem is the fact that many TCP implementations will reset the
   congestion window after one retransmission timeout period if all data sent
   at that point has been acked, which means that both sides now restart with
   a congestion window of size 1.  Unfortunately there's nothing that can be
   done about this, however hopefully at some point TCP implementations will
   start to fall into line with RFC 3390 and allow initial windows of ~4K,
   which will fix this particular problem.
   
   There are other, non-portable workarounds for this as well, but they're
   so non-portable that they often don't even work across different versions
   of the same OS (e.g. different versions of the Linux kernel), let alone 
   variants of one OS type (e.g. OpenBSD vs. FreeBSD).  The least nonportable
   one is using writev() to combine a seperate header and body, which exists
   in most Unix versions and Win32.  Easier-to-use but almost totally non-
   portable are facilities like TCP_CORK (newer Linux kernels) and 
   TCP_NOPUSH (some *BSDs) which delay sending buffer contents until the 
   flag is reset again (so the use is "set TCP_CORK, write, write, write,
   reset TCP_CORK").  Because all of these are far more trouble than they're
   worth, and in any case we're only sending very small data quantities via
   these functions (PKI messages), we just assemble the whole datagram 
   ourselves, which works across all OSes */

static int bufferedTransportWriteFunction( STREAM *stream, const BYTE *buffer,
										   const int length, const int flags )
	{
	const BYTE *bufPtr = buffer;
	int byteCount = length, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( isReadPtr( buffer, length ) );
	assert( length > 0 );

	/* If it's not a flush and the buffer can absorb the data, copy it in and
	   exit */
	if( !( flags & TRANSPORT_FLAG_FLUSH ) && \
		stream->writeBufEnd + length <= stream->writeBufSize )
		{
		memcpy( stream->writeBuffer + stream->writeBufEnd, buffer, length );
		stream->writeBufEnd += length;
		assert( stream->writeBufEnd <= stream->writeBufSize );

		return( length );
		}

	/* It's a flush or too much data to buffer, assemble a complete buffer
	   and write it */
	if( stream->writeBufEnd > 0 )
		{
		const int bytesToCopy = min( byteCount, \
									 stream->writeBufSize - stream->writeBufEnd );
		const int bytesToWrite = stream->writeBufEnd + bytesToCopy;

		if( bytesToCopy > 0 )
			memcpy( stream->writeBuffer + stream->writeBufEnd, buffer,
					bytesToCopy );
		status = stream->transportWriteFunction( stream, stream->writeBuffer,
												 bytesToWrite,
												 TRANSPORT_FLAG_FLUSH );
		if( cryptStatusError( status ) || status < bytesToWrite )
			return( status );
		stream->writeBufEnd = 0;
		bufPtr += bytesToCopy;
		byteCount -= bytesToCopy;
		if( byteCount <= 0 )
			/* We've written everything, exit */
			return( length );
		}

	/* Write anything that's left directly */
	status = stream->transportWriteFunction( stream, bufPtr, byteCount,
											 TRANSPORT_FLAG_FLUSH );
	if( cryptStatusError( status ) || status < byteCount )
		return( status );
	return( length );
	}

/****************************************************************************
*																			*
*							Network Stream Functions						*
*																			*
****************************************************************************/

/* Initialise the network stream */

static int initStream( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
					   const NET_CONNECT_INFO *connectInfo,
					   const BOOLEAN isServer )
	{
	int timeout;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( protocol > STREAM_PROTOCOL_NONE && \
			protocol < STREAM_PROTOCOL_LAST );
	assert( isReadPtr( connectInfo, sizeof( NET_CONNECT_INFO ) ) );

	/* Set up the basic network stream info */
	memset( stream, 0, sizeof( STREAM ) );
	stream->type = STREAM_TYPE_NETWORK;
	stream->protocol = protocol;
	stream->port = connectInfo->port;
	stream->netSocket = stream->listenSocket = CRYPT_ERROR;
	stream->iTransportSession = CRYPT_ERROR;
	if( isServer )
		stream->nFlags = STREAM_NFLAG_ISSERVER;

	/* Set up the stream timeout information.  While we're connecting, the
	   stream timeout is the connect timeout.  Once we've connected it's set
	   to the data transfer timeout, so initially we set the stream timeout
	   to the connect timeout and the saved timeout to the data transfer
	   timeout */
	if( connectInfo->connectTimeout != CRYPT_ERROR )
		/* There's an explicit timeout specified, use that */
		timeout = connectInfo->connectTimeout;
	else
		/* Get the default timeout from the user object */
		if( cryptStatusError( \
				krnlSendMessage( connectInfo->iUserObject, IMESSAGE_GETATTRIBUTE,
								 &timeout, CRYPT_OPTION_NET_CONNECTTIMEOUT ) ) )
			timeout = 30;
	if( timeout < 5 )
		{
		/* Enforce the same minimum connect timeout as the kernel ACLs */
		assert( NOTREACHED );
		timeout = 5;
		}
	stream->timeout = timeout;
	if( connectInfo->timeout != CRYPT_ERROR )
		/* There's an explicit timeout specified, use that */
		timeout = connectInfo->timeout;
	else
		/* Get the default timeout from the user object */
		if( cryptStatusError( \
				krnlSendMessage( connectInfo->iUserObject, IMESSAGE_GETATTRIBUTE,
								 &timeout, CRYPT_OPTION_NET_READTIMEOUT ) ) )
			timeout = 30;
	stream->savedTimeout = timeout;

	return( CRYPT_OK );
	}

/* Connect a stream */

static int openConnection( STREAM *stream,
						   const NET_OPTION_TYPE options,
						   const char *proxyUrl, const int proxyUrlLen )
	{
	URL_INFO urlInfo;
	char urlBuffer[ MAX_DNS_SIZE + 8 ];
	const char *url = proxyUrl;
	int urlLen = proxyUrlLen, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( ( proxyUrl == NULL && proxyUrlLen == 0 ) || \
			isReadPtr( proxyUrl, proxyUrlLen ) );

	/* If we're using an already-active network socket supplied by the
	   user, there's nothing to do */
	if( stream->nFlags & STREAM_NFLAG_USERSOCKET )
		{
		/* If it's a dummy open to check parameters that can't be validated
		   at a higher level, pass the info on down to the low-level
		   checking routines */
		if( options == NET_OPTION_NETWORKSOCKET_DUMMY )
			return( stream->transportCheckFunction( stream ) );

		return( CRYPT_OK );
		}

	/* If we're not going via a proxy, perform a direct open */
	if( proxyUrl == NULL )
		return( stream->transportConnectFunction( stream, stream->host, 
												  stream->hostLen,
												  stream->port ) );

	/* We're going via a proxy.  If the user has specified automatic proxy
	   detection, try and locate the proxy information */
	if( !strCompareZ( proxyUrl, "[Autodetect]" ) )
		{
		status = findProxyUrl( urlBuffer, MAX_DNS_SIZE, stream->host, 
							   stream->hostLen );
		if( cryptStatusError( status ) )
			{
			/* The proxy URL was invalid, provide more information for the
			   caller */
			stream->errorInfo->errorCode = CRYPT_ERROR_NOTFOUND;
			strlcpy_s( stream->errorInfo->errorString, MAX_ERRMSG_SIZE,
					   "Couldn't auto-detect HTTP proxy" );
			return( CRYPT_ERROR_OPEN );
			}
		url = urlBuffer;
		urlLen = status;
		}

	/* Process the proxy details.  Since this is an HTTP proxy, we specify
	   the default port as port 80 */
	status = parseURL( &urlInfo, url, urlLen, 80, URL_TYPE_HTTP );
	if( cryptStatusError( status ) )
		{
		/* The proxy URL was invalid, provide more information for the
		   caller */
		stream->errorInfo->errorCode = CRYPT_ERROR_BADDATA;
		strlcpy_s( stream->errorInfo->errorString, MAX_ERRMSG_SIZE,
				   "Invalid HTTP proxy URL" );
		return( CRYPT_ERROR_OPEN );
		}

	/* Since we're going via a proxy, open the connection to the proxy
	   rather than directly to the target system.  */
	return( stream->transportConnectFunction( stream, urlInfo.host, 
											  urlInfo.hostLen, urlInfo.port ) );
	}

/* Clean up a stream to shut it down */

static void cleanupStream( STREAM *stream, const BOOLEAN cleanupTransport,
						   const BOOLEAN cleanupBuffers )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NETWORK );

	/* Clean up the transport system if necessary */
	if( cleanupTransport && !( stream->nFlags & STREAM_NFLAG_USERSOCKET ) )
		stream->transportDisconnectFunction( stream, TRUE );

	/* Clean up stream-related buffers if necessary */
	if( cleanupBuffers )
		{
		assert( stream->errorInfo != NULL );

		if( stream->bufSize > 0 )
			{
			zeroise( stream->buffer, stream->bufSize );
			clFree( "cleanupStream", stream->buffer );
			}
		if( stream->writeBufSize > 0 )
			{
			zeroise( stream->writeBuffer, stream->writeBufSize );
			clFree( "cleanupStream", stream->writeBuffer );
			}
		clFree( "cleanupStream", stream->errorInfo );
		}

	/* Clean up static stream data */
	if( stream->host != NULL )
		clFree( "cleanupStream", stream->host );
	if( stream->path != NULL )
		clFree( "cleanupStream", stream->path );

	zeroise( stream, sizeof( STREAM ) );
	}

/* Check for the use of a proxy when opening a stream */

static int checkForProxy( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
						  const NET_CONNECT_INFO *connectInfo,
						  char *proxyUrlBuffer, const int proxyUrlMaxLen )
	{
	MESSAGE_DATA msgData;
	int status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( protocol > STREAM_PROTOCOL_NONE && \
			protocol < STREAM_PROTOCOL_LAST );
	assert( isReadPtr( connectInfo, sizeof( NET_CONNECT_INFO ) ) );
	assert( isWritePtr( proxyUrlBuffer, proxyUrlMaxLen ) );

	/* Check for a local connection, which always bypasses the proxy.  We
	   only use the case-insensitive string compares for the text-format
	   host names, since the numeric forms don't need this. */
	if( ( stream->hostLen == 9 && \
		  !memcmp( stream->host, "127.0.0.1", 9 ) ) || \
		( stream->hostLen == 3 && \
		  !memcmp( stream->host, "::1", 3 ) ) || \
		( stream->hostLen == 9 && \
		  !strCompare( stream->host, "localhost", 9 ) ) || \
		( stream->hostLen == 10 && \
		  !strCompare( stream->host, "localhost.", 10 ) ) )	/* Are you local? */
		/* This is a local socket! We'll have no proxies here! */
		return( 0 );

	/* Check to see whether we're going through a proxy.  First we check for
	   a protocol-specific HTTP proxy (if appropriate), if there's none we
	   check for the more generic case of a SOCKS proxy.  In addition to the
	   obvious use of an HTTP proxy for HTTP, we also check for an HTTP URL
	   specified for use with other protocols (specifcally SSL/TLS), since
	   these can also go via a proxy even if the they're not an explicit use
	   of HTTP */
	if( ( protocol == STREAM_PROTOCOL_HTTP || \
		  connectInfo->options == NET_OPTION_HOSTNAME_TUNNEL ) )
		{
		/* Check whether there's an HTTP proxy configured */
		setMessageData( &msgData, proxyUrlBuffer, proxyUrlMaxLen );
		status = krnlSendMessage( connectInfo->iUserObject,
								  IMESSAGE_GETATTRIBUTE_S, &msgData,
								  CRYPT_OPTION_NET_HTTP_PROXY );
		if( cryptStatusOK( status ) )
			{
			stream->nFlags |= \
				( connectInfo->options == NET_OPTION_HOSTNAME ) ? \
				STREAM_NFLAG_HTTPPROXY : STREAM_NFLAG_HTTPTUNNEL;
			return( msgData.length );
			}
		}

	/* Check whether there's a SOCKS proxy configured */
	setMessageData( &msgData, proxyUrlBuffer, proxyUrlMaxLen );
	status = krnlSendMessage( connectInfo->iUserObject,
							  IMESSAGE_GETATTRIBUTE_S, &msgData,
							  CRYPT_OPTION_NET_SOCKS_SERVER );
	if( cryptStatusOK( status ) )
		return( msgData.length );

	/* There's no proxy configured */
	return( 0 );
	}

/* Complete a network connection after the client- or server-specific
   portions have been handled */

static int completeConnect( STREAM *stream,
							const STREAM_PROTOCOL_TYPE protocol,
							const NET_OPTION_TYPE options,
							const char *proxyUrl, const int proxyUrlLen,
							const CRYPT_USER iUserObject, 
							ERROR_INFO *errorInfo )
	{
	const BOOLEAN useTransportBuffering = \
						( options == NET_OPTION_TRANSPORTSESSION || \
						  protocol == STREAM_PROTOCOL_TCPIP ) ? \
						FALSE : TRUE;
	int status = CRYPT_OK;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( protocol > STREAM_PROTOCOL_NONE && \
			protocol < STREAM_PROTOCOL_LAST );
	assert( options > NET_OPTION_NONE && options < NET_OPTION_LAST );
	assert( ( proxyUrl == NULL && proxyUrlLen == 0 ) || \
			isReadPtr( proxyUrl, proxyUrlLen ) );
	assert( ( iUserObject == DEFAULTUSER_OBJECT_HANDLE ) || \
			isHandleRangeValid( iUserObject ) );
	assert( isWritePtr( errorInfo, sizeof( ERROR_INFO ) ) );

	/* Set up the access method pointers.  We can use either direct TCP/IP
	   access or a cryptlib stream for transport, and layered over that
	   either HTTP, the CMP socket protocol, or direct access to the
	   transport layer */
	if( options == NET_OPTION_TRANSPORTSESSION )
		{
		stream->transportConnectFunction = transportSessionConnectFunction;
		stream->transportDisconnectFunction = transportSessionDisconnectFunction;
		stream->transportWriteFunction = transportSessionWriteFunction;
		stream->transportReadFunction = transportSessionReadFunction;
		stream->transportOKFunction = transportSessionOKFunction;
		}
	else
		setAccessMethodTCP( stream );
	switch( protocol )
		{
		case STREAM_PROTOCOL_HTTP:
#ifdef USE_HTTP
			setStreamLayerHTTP( stream );
#else
			return( CRYPT_ERROR_NOTAVAIL );
#endif /* USE_HTTP */
			break;

		case STREAM_PROTOCOL_CMP:
#ifdef USE_CMP_TRANSPORT
			setStreamLayerCMP( stream );
#else
			return( CRYPT_ERROR_NOTAVAIL );
#endif /* USE_CMP_TRANSPORT */
			break;

		case STREAM_PROTOCOL_TCPIP:
			setStreamLayerDirect( stream );
			break;

		default:
			retIntError();
		}
	if( useTransportBuffering )
		{
		stream->bufferedTransportReadFunction = bufferedTransportReadFunction;
		stream->bufferedTransportWriteFunction = bufferedTransportWriteFunction;
		}
	else
		{
		stream->bufferedTransportReadFunction = stream->transportReadFunction;
		stream->bufferedTransportWriteFunction = stream->transportWriteFunction;
		}

	/* If we're running over a cryptlib session, make sure that we wait around
	   for a minimum amount of time during network comms in case the user has
	   specified nonblocking behaviour or quick timeouts */
	if( options == NET_OPTION_TRANSPORTSESSION )
		{
		static const int fixedTimeout = 30;
		int timeout;

		status = krnlSendMessage( iUserObject, IMESSAGE_GETATTRIBUTE,
								  &timeout, CRYPT_OPTION_NET_CONNECTTIMEOUT );
		if( cryptStatusOK( status ) && timeout < fixedTimeout )
			krnlSendMessage( stream->iTransportSession,
							 IMESSAGE_SETATTRIBUTE, ( void * ) &fixedTimeout,
							 CRYPT_OPTION_NET_CONNECTTIMEOUT );
		status = krnlSendMessage( iUserObject, IMESSAGE_GETATTRIBUTE,
								  &timeout, CRYPT_OPTION_NET_READTIMEOUT );
		if( cryptStatusOK( status ) && timeout < fixedTimeout )
			krnlSendMessage( stream->iTransportSession, IMESSAGE_SETATTRIBUTE,
							 ( void * ) &fixedTimeout,
							 CRYPT_OPTION_NET_READTIMEOUT );
		status = krnlSendMessage( iUserObject, IMESSAGE_GETATTRIBUTE,
								  &timeout, CRYPT_OPTION_NET_WRITETIMEOUT );
		if( cryptStatusOK( status ) && timeout < fixedTimeout )
			krnlSendMessage( stream->iTransportSession, IMESSAGE_SETATTRIBUTE,
							 ( void * ) &fixedTimeout,
							 CRYPT_OPTION_NET_WRITETIMEOUT );
		status = CRYPT_OK;	/* Reset status from above checks */
		}

	/* Wait for any async network driver binding to complete and make sure
	   that the network interface has been initialised */
	if( !krnlWaitSemaphore( SEMAPHORE_DRIVERBIND ) || \
		!stream->transportOKFunction() )
		{
		/* Provide more information on the nature of the problem */
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
				   "Networking subsystem not available" );

		/* Clean up */
		cleanupStream( stream, FALSE, FALSE );
		return( CRYPT_ERROR_NOTINITED );
		}

	/* Allocate room for the I/O buffers and error messages returned from the
	   lower-level networking code */
	if( ( stream->errorInfo = clAlloc( "completeConnect", \
									   sizeof( ERROR_INFO ) ) ) == NULL )
		{
		cleanupStream( stream, FALSE, FALSE );
		return( CRYPT_ERROR_MEMORY );
		}
	if( useTransportBuffering )
		{
		if( ( stream->buffer = clAlloc( "completeConnect", \
										NETWORK_BUFFER_SIZE ) ) != NULL )
			{
			stream->bufSize = NETWORK_BUFFER_SIZE;
			if( ( stream->writeBuffer = \
					clAlloc( "completeConnect", NETWORK_BUFFER_SIZE ) ) != NULL )
				stream->writeBufSize = NETWORK_BUFFER_SIZE;
			}
		if( stream->writeBufSize <= 0 )
			{
			cleanupStream( stream, FALSE, TRUE );
			return( CRYPT_ERROR_MEMORY );
			}
		}
	memset( stream->errorInfo, 0, sizeof( ERROR_INFO ) );
	status = openConnection( stream, options, proxyUrl, proxyUrlLen );
	if( cryptStatusError( status ) )
		{
		/* Copy back the error information to the caller */
		errorInfo->errorCode = stream->errorInfo->errorCode;
		strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
				   stream->errorInfo->errorString );

		/* Clean up */
		cleanupStream( stream, FALSE, TRUE );
		return( status );
		}

	/* If we're not going through a proxy, we're done */
	if( proxyUrl == NULL )
		return( CRYPT_OK );

	/* Complete the connect via the appropriate proxy type */
	return( connectViaHttpProxy( stream, errorInfo ) );
	}

/* Open and close a network connection.  This parses a location string
   (usually a URL) into <scheme>://<host>[:<port>]/<path>[?<query>]
   components and opens a connection to the host for non-stateless
   protocols */

int sNetConnect( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
				 const NET_CONNECT_INFO *connectInfo, ERROR_INFO *errorInfo )
	{
	URL_INFO urlInfo;
	char proxyUrlBuffer[ MAX_DNS_SIZE + 8 ], *proxyURL = NULL;
	int proxyUrlLen = 0, status;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( protocol == STREAM_PROTOCOL_TCPIP || \
			protocol == STREAM_PROTOCOL_HTTP || \
			protocol == STREAM_PROTOCOL_CMP );
	assert( isReadPtr( connectInfo, sizeof( NET_CONNECT_INFO ) ) );
	assert( isWritePtr( errorInfo, sizeof( ERROR_INFO ) ) );
	assert( ( connectInfo->options != NET_OPTION_HOSTNAME && \
			  connectInfo->options != NET_OPTION_HOSTNAME_TUNNEL ) || \
			( ( connectInfo->options == NET_OPTION_HOSTNAME || \
				connectInfo->options == NET_OPTION_HOSTNAME_TUNNEL ) && \
			  isReadPtr( connectInfo->name, connectInfo->nameLength ) && \
			  connectInfo->iCryptSession == CRYPT_ERROR && \
			  connectInfo->networkSocket == CRYPT_ERROR ) );
	assert( connectInfo->options != NET_OPTION_TRANSPORTSESSION || \
			( connectInfo->options == NET_OPTION_TRANSPORTSESSION && \
			  connectInfo->name == NULL && connectInfo->nameLength == 0 && \
			  connectInfo->iCryptSession != CRYPT_ERROR && \
			  connectInfo->networkSocket == CRYPT_ERROR ) );
	assert( ( connectInfo->options != NET_OPTION_NETWORKSOCKET && \
			  connectInfo->options != NET_OPTION_NETWORKSOCKET_DUMMY ) || \
			( ( connectInfo->options == NET_OPTION_NETWORKSOCKET || \
				connectInfo->options == NET_OPTION_NETWORKSOCKET_DUMMY ) && \
			  connectInfo->name == NULL && connectInfo->nameLength == 0 && \
			  connectInfo->iCryptSession == CRYPT_ERROR && \
			  connectInfo->networkSocket != CRYPT_ERROR ) );
	assert( connectInfo->iUserObject >= DEFAULTUSER_OBJECT_HANDLE &&
			connectInfo->iUserObject < MAX_OBJECTS );

	/* Clear the return values */
	memset( errorInfo, 0, sizeof( ERROR_INFO ) );

	/* Initialise the network stream info */
	initStream( stream, protocol, connectInfo, FALSE );
	switch( connectInfo->options )
		{
		case NET_OPTION_HOSTNAME:
		case NET_OPTION_HOSTNAME_TUNNEL:
			/* Parse the URI into its various components */
			status = parseURL( &urlInfo, connectInfo->name,
						connectInfo->nameLength, connectInfo->port,
						( protocol == STREAM_PROTOCOL_HTTP ) ? \
							URL_TYPE_HTTP : 
						( protocol == STREAM_PROTOCOL_CMP ) ? \
							URL_TYPE_HTTP : URL_TYPE_NONE );
			if( cryptStatusError( status ) )
				{
				/* There's an error in the URL format, provide more
				   information to the caller */
				strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
						   "Invalid host name/URL" );
				return( CRYPT_ERROR_OPEN );
				}
			status = copyUrlToStream( stream, &urlInfo );
			if( cryptStatusError( status ) )
				return( status );

			/* Check for the use of a proxy to establish the connection */
			status = checkForProxy( stream, protocol, connectInfo, 
									proxyUrlBuffer, MAX_DNS_SIZE );
			if( status > 0 )
				{
				proxyURL = proxyUrlBuffer;
				proxyUrlLen = status;
				}
			break;

		case NET_OPTION_TRANSPORTSESSION:
			stream->iTransportSession = connectInfo->iCryptSession;
			break;

		case NET_OPTION_NETWORKSOCKET:
		case NET_OPTION_NETWORKSOCKET_DUMMY:
			stream->netSocket = connectInfo->networkSocket;
			stream->nFlags |= STREAM_NFLAG_USERSOCKET;
			break;

		default:
			retIntError();
		}

	/* Set up access mechanisms and complete the connection */
	return( completeConnect( stream, protocol, connectInfo->options, 
							 proxyURL, proxyUrlLen, 
							 connectInfo->iUserObject, errorInfo ) );
	}

int sNetListen( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
				const NET_CONNECT_INFO *connectInfo, ERROR_INFO *errorInfo )
	{
	URL_INFO urlInfo;

	assert( isWritePtr( stream, sizeof( STREAM ) ) );
	assert( protocol == STREAM_PROTOCOL_TCPIP || \
			protocol == STREAM_PROTOCOL_HTTP || \
			protocol == STREAM_PROTOCOL_CMP );
	assert( isReadPtr( connectInfo, sizeof( NET_CONNECT_INFO ) ) );
	assert( isWritePtr( errorInfo, sizeof( ERROR_INFO ) ) );
	assert( connectInfo->options != NET_OPTION_HOSTNAME || \
			( connectInfo->options == NET_OPTION_HOSTNAME && \
			  connectInfo->iCryptSession == CRYPT_ERROR && \
			  connectInfo->networkSocket == CRYPT_ERROR ) );
	assert( connectInfo->options != NET_OPTION_TRANSPORTSESSION || \
			( connectInfo->options == NET_OPTION_TRANSPORTSESSION && \
			  connectInfo->name == NULL && connectInfo->nameLength == 0 && \
			  connectInfo->iCryptSession != CRYPT_ERROR && \
			  connectInfo->networkSocket == CRYPT_ERROR ) );
	assert( ( connectInfo->options != NET_OPTION_NETWORKSOCKET && \
			  connectInfo->options != NET_OPTION_NETWORKSOCKET_DUMMY ) || \
			( ( connectInfo->options == NET_OPTION_NETWORKSOCKET || \
				connectInfo->options == NET_OPTION_NETWORKSOCKET_DUMMY ) || \
			  connectInfo->name == NULL && connectInfo->nameLength == 0 &&  \
			  connectInfo->iCryptSession == CRYPT_ERROR && \
			  connectInfo->networkSocket != CRYPT_ERROR ) );
	assert( connectInfo->iUserObject >= DEFAULTUSER_OBJECT_HANDLE &&
			connectInfo->iUserObject < MAX_OBJECTS );

	/* Clear the return values */
	memset( errorInfo, 0, sizeof( ERROR_INFO ) );

	/* Initialise the network stream info */
	initStream( stream, protocol, connectInfo, TRUE );
	switch( connectInfo->options )
		{
		case NET_OPTION_HOSTNAME:
			if( connectInfo->name != NULL )
				{
				int status;

				/* Parse the interface URI into its various components */
				status = parseURL( &urlInfo, connectInfo->name,
							connectInfo->nameLength, connectInfo->port,
							( protocol == STREAM_PROTOCOL_HTTP || \
							  protocol == STREAM_PROTOCOL_CMP ) ? \
								URL_TYPE_HTTP : URL_TYPE_NONE );
				if( cryptStatusError( status ) )
					{
					/* There's an error in the format, provide more
					   information to the caller */
					strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE,
							   "Invalid interface name" );
					return( CRYPT_ERROR_OPEN );
					}
				status = copyUrlToStream( stream, &urlInfo );
				if( cryptStatusError( status ) )
					return( status );
				}
			break;

		case NET_OPTION_TRANSPORTSESSION:
			stream->iTransportSession = connectInfo->iCryptSession;
			break;

		case NET_OPTION_NETWORKSOCKET:
		case NET_OPTION_NETWORKSOCKET_DUMMY:
			stream->netSocket = connectInfo->networkSocket;
			stream->nFlags |= STREAM_NFLAG_USERSOCKET;
			break;

		default:
			retIntError();
		}

	/* Set up access mechanisms and complete the connection */
	return( completeConnect( stream, protocol, connectInfo->options, NULL, 0,
							 connectInfo->iUserObject, errorInfo ) );
	}

int sNetDisconnect( STREAM *stream )
	{
	assert( isWritePtr( stream, sizeof( STREAM ) ) );

	cleanupStream( stream, TRUE, TRUE );

	return( CRYPT_OK );
	}

/* Parse a URL into its various components */

int sNetParseURL( URL_INFO *urlInfo, const char *url, const int urlLen,
				  const URL_TYPE urlTypeHint )
	{
	assert( isWritePtr( urlInfo, sizeof( URL_INFO ) ) );
	assert( isReadPtr( url, urlLen ) );
	assert( urlTypeHint >= URL_TYPE_NONE && urlTypeHint < URL_TYPE_LAST );

	return( parseURL( urlInfo, url, urlLen, CRYPT_UNUSED, urlTypeHint ) );
	}

/* Get extended information about an error status on a network connection */

void sNetGetErrorInfo( STREAM *stream, ERROR_INFO *errorInfo )
	{
	assert( isReadPtr( stream, sizeof( STREAM ) ) );
	assert( stream->type == STREAM_TYPE_NETWORK );
	assert( isWritePtr( errorInfo, sizeof( ERROR_INFO ) ) );

	/* Remember the error code and message.  If we're running over a
	   cryptlib transport session we have to first pull the info up from the
	   session */
	if( stream->iTransportSession != CRYPT_ERROR )
		getSessionErrorInfo( stream, CRYPT_OK );
	errorInfo->errorCode = stream->errorInfo->errorCode;
	strlcpy_s( errorInfo->errorString, MAX_ERRMSG_SIZE, 
			   stream->errorInfo->errorString );
	}

#else

/****************************************************************************
*																			*
*							Network Stream Stubs							*
*																			*
****************************************************************************/

/* If there's no networking support present, we replace the network access
   routines with dummy ones that always return an error */

int sNetConnect( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
				 const NET_CONNECT_INFO *connectInfo, ERROR_INFO *errorInfo )
	{
	memset( stream, 0, sizeof( STREAM ) );
	memset( errorInfo, 0, sizeof( ERROR_INFO ) );
	return( CRYPT_ERROR_OPEN );
	}

int sNetListen( STREAM *stream, const STREAM_PROTOCOL_TYPE protocol,
				const NET_CONNECT_INFO *connectInfo, ERROR_INFO *errorInfo )
	{
	memset( stream, 0, sizeof( STREAM ) );
	memset( errorInfo, 0, sizeof( ERROR_INFO ) );
	return( CRYPT_ERROR_OPEN );
	}

int sNetDisconnect( STREAM *stream )
	{
	UNUSED( stream );

	return( CRYPT_OK );
	}

int sNetParseURL( URL_INFO *urlInfo, const char *url, const int urlLen,
				  const URL_TYPE urlTypeHint )
	{
	memset( urlInfo, 0, sizeof( URL_INFO ) );

	return( CRYPT_ERROR_BADDATA );
	}

void sNetGetErrorInfo( STREAM *stream, ERROR_INFO *errorInfo )
	{
	UNUSED( stream );

	memset( errorInfo, 0, sizeof( ERROR_INFO ) );
	}
#endif /* USE_TCP */
