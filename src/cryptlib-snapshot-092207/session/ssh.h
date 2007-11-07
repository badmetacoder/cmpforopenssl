/****************************************************************************
*																			*
*						SSHv1/SSHv2 Definitions Header File					*
*						Copyright Peter Gutmann 1998-2004					*
*																			*
****************************************************************************/

#ifndef _SSH_DEFINED

#define _SSH_DEFINED

/****************************************************************************
*																			*
*								SSH Constants								*
*																			*
****************************************************************************/

/* Default SSH port */

#define SSH_PORT				22

/* Various SSH constants */

#define ID_SIZE					1	/* ID byte */
#define LENGTH_SIZE				4	/* Size of packet length field */
#define UINT_SIZE				4	/* Size of integer value */
#define PADLENGTH_SIZE			1	/* Size of padding length field */
#define BOOLEAN_SIZE			1	/* Size of boolean value */

#define SSH1_COOKIE_SIZE		8	/* Size of SSHv1 cookie */
#define SSH1_HEADER_SIZE		5	/* Size of SSHv1 packet header */
#define SSH1_CRC_SIZE			4	/* Size of CRC value */
#define SSH1_MPI_LENGTH_SIZE	2	/* Size of MPI length field */
#define SSH1_SESSIONID_SIZE		16	/* Size of SSHv1 session ID */
#define SSH1_SECRET_SIZE		32	/* Size of SSHv1 shared secret */
#define SSH1_CHALLENGE_SIZE		32	/* Size of SSHv1 RSA auth.challenge */
#define SSH1_RESPONSE_SIZE		16	/* Size of SSHv1 RSA auth.response */

#define SSH2_COOKIE_SIZE		16	/* Size of SSHv2 cookie */
#define SSH2_HEADER_SIZE		5	/* Size of SSHv2 packet header */
#define SSH2_MIN_ALGOID_SIZE	4	/* Size of shortest SSHv2 algo.name */
#define SSH2_MIN_PADLENGTH_SIZE	4	/* Minimum amount of padding for packets */
#define SSH2_PAYLOAD_HEADER_SIZE 9	/* Size of SSHv2 inner payload header */
#define SSH2_FIXED_KEY_SIZE		16	/* Size of SSHv2 fixed-size keys */
#define SSH2_DEFAULT_KEYSIZE	128	/* Size of SSHv2 default DH key */

/* SSH packet/buffer size information.  The extra packet data is for
   additional non-payload information including the header, MAC, and up to
   256 bytes of padding */

#define MAX_PACKET_SIZE			262144L
#define EXTRA_PACKET_SIZE		512
#define DEFAULT_PACKET_SIZE		16384
#define MAX_WINDOW_SIZE			0x7FFFFFFFL

/* SSH protocol-specific flags that encode details of implementation bugs 
   that we need to work around */

#define SSH_PFLAG_NONE			0x000/* No protocol-specific flags */
#define SSH_PFLAG_HMACKEYSIZE	0x001/* Peer is using short HMAC keys */
#define SSH_PFLAG_SIGFORMAT		0x002/* Peer omits sig.algo name */
#define SSH_PFLAG_NOHASHSECRET	0x004/* Peer omits secret in key derive */
#define SSH_PFLAG_NOHASHLENGTH	0x008/* Peer omits length in exchge.hash */
#define SSH_PFLAG_WINDOWBUG		0x010/* Peer requires unnec.window-adjusts */
#define SSH_PFLAG_TEXTDIAGS		0x020/* Peer dumps text diagnostics on error */
#define SSH_PFLAG_PAMPW			0x040/* Peer chokes on "password" as PAM submethod */
#define SSH_PFLAG_DUMMYUSERAUTH	0x080/* Peer requires dummy userAuth msg.*/
#define SSH_PFLAG_ZEROLENIGNORE	0x100/* Peer sends zero-length SSH_IGNORE */
#define SSH_PFLAG_CUTEFTP		0x200/* CuteFTP, drops conn.during handshake */

/* Various data sizes used for read-ahead and buffering.  The minimum SSH
   packet size is used to determine how much data we can read when reading
   a packet header, the SSHv2 header remainder size is how much data we've
   got left once we've extracted just the length but no other data, the
   SSHv2 remainder size is how much data we've got left once we've
   extracted all fixed information values, and the SSHv1 maximum header size
   is used to determine how much space we need to reserve at the start of
   the buffer when encoding SSHv1's variable-length data packets (SSHv2 has
   a fixed header size so this isn't a problem any more) */

#define MIN_PACKET_SIZE			16
#define SSH2_HEADER_REMAINDER_SIZE \
								( MIN_PACKET_SIZE - LENGTH_SIZE )
#define SSH1_MAX_HEADER_SIZE	( LENGTH_SIZE + 8 + ID_SIZE + LENGTH_SIZE )

/* SSH ID information */

#define SSH_ID					"SSH-"		/* Start of SSH ID */
#define SSH_ID_SIZE				4	/* Size of SSH ID */
#define SSH_VERSION_SIZE		4	/* Size of SSH version */
#define SSH_ID_MAX_SIZE			255	/* Max.size of SSHv2 ID string */
#define SSH1_ID_STRING			"SSH-1.5-cryptlib"
#define SSH2_ID_STRING			"SSH-2.0-cryptlib"	/* cryptlib SSH ID strings */

/* SSHv1 packet types */

#define SSH1_MSG_DISCONNECT		1	/* Disconnect session */
#define SSH1_SMSG_PUBLIC_KEY	2	/* Server public key */
#define SSH1_CMSG_SESSION_KEY	3	/* Encrypted session key */
#define SSH1_CMSG_USER			4	/* User name */
#define SSH1_CMSG_AUTH_RSA		6	/* RSA public key */
#define SSH1_SMSG_AUTH_RSA_CHALLENGE 7	/* RSA challenge from server */
#define SSH1_CMSG_AUTH_RSA_RESPONSE 8	/* RSA response from client */
#define SSH1_CMSG_AUTH_PASSWORD	9	/* Password */
#define SSH1_CMSG_REQUEST_PTY	10	/* Request a pty */
#define SSH1_CMSG_WINDOW_SIZE	11	/* Terminal window size change */
#define SSH1_CMSG_EXEC_SHELL	12	/* Request a shell */
#define SSH1_CMSG_EXEC_CMD		13	/* Request command execution */
#define SSH1_SMSG_SUCCESS		14	/* Success status message */
#define SSH1_SMSG_FAILURE		15	/* Failure status message */
#define SSH1_CMSG_STDIN_DATA	16	/* Data from client stdin */
#define SSH1_SMSG_STDOUT_DATA	17	/* Data from server stdout */
#define SSH1_SMSG_EXITSTATUS	20	/* Exit status of command run on server */
#define SSH1_MSG_IGNORE			32	/* No-op */
#define SSH1_CMSG_EXIT_CONFIRMATION 33 /* Client response to server exitstatus */
#define SSH1_MSG_DEBUG			36	/* Debugging/informational message */
#define SSH1_CMSG_MAX_PACKET_SIZE 38	/* Maximum data packet size */

/* Further SSHv1 packet types that aren't used but which we need to
   recognise */

#define SSH1_CMSG_PORT_FORWARD_REQUEST		28
#define SSH1_CMSG_AGENT_REQUEST_FORWARDING	30
#define SSH1_CMSG_X11_REQUEST_FORWARDING	34
#define SSH1_CMSG_REQUEST_COMPRESSION		37

/* SSHv2 packet types.  There is some overlap with SSHv1, but an annoying
   number of messages have the same name but different values.  Note also
   that the keyex (static DH keys) and keyex_gex (ephemeral DH keys) message
   types overlap */

#define SSH2_MSG_DISCONNECT		1	/* Disconnect session */
#define SSH2_MSG_IGNORE			2	/* No-op */
#define SSH2_MSG_DEBUG			4	/* No-op */
#define SSH2_MSG_SERVICE_REQUEST 5	/* Request authentiction */
#define SSH2_MSG_SERVICE_ACCEPT	6	/* Acknowledge request */
#define SSH2_MSG_KEXINIT		20	/* Hello */
#define SSH2_MSG_NEWKEYS		21	/* Change cipherspec */
#define SSH2_MSG_KEXDH_INIT		30	/* DH, phase 1 */
#define SSH2_MSG_KEXDH_REPLY	31	/* DH, phase 2 */
#define SSH2_MSG_KEXDH_GEX_REQUEST_OLD 30 /* Ephem.DH key request */
#define SSH2_MSG_KEXDH_GEX_GROUP 31	/* Ephem.DH key response */
#define SSH2_MSG_KEXDH_GEX_INIT	32	/* Ephem.DH, phase 1 */
#define SSH2_MSG_KEXDH_GEX_REPLY 33	/* Ephem.DH, phase 2 */
#define SSH2_MSG_KEXDH_GEX_REQUEST_NEW 34 /* Ephem.DH key request */
#define SSH2_MSG_USERAUTH_REQUEST 50 /* Request authentication */
#define SSH2_MSG_USERAUTH_FAILURE 51 /* Authentication failed */
#define SSH2_MSG_USERAUTH_SUCCESS 52 /* Authentication succeeded */
#define SSH2_MSG_USERAUTH_BANNER 53	/* No-op */
#define SSH2_MSG_USERAUTH_INFO_REQUEST 60 /* Generic auth.svr.request */
#define SSH2_MSG_USERAUTH_INFO_RESPONSE 61 /* Generic auth.cli.response */
#define SSH2_MSG_GLOBAL_REQUEST	80	/* Perform a global ioctl */
#define SSH2_MSG_GLOBAL_SUCCESS	81	/* Global request succeeded */
#define SSH2_MSG_GLOBAL_FAILURE	82	/* Global request failed */
#define	SSH2_MSG_CHANNEL_OPEN	90	/* Open a channel over an SSH link */
#define	SSH2_MSG_CHANNEL_OPEN_CONFIRMATION 91	/* Channel open succeeded */
#define SSH2_MSG_CHANNEL_OPEN_FAILURE 92	/* Channel open failed */
#define	SSH2_MSG_CHANNEL_WINDOW_ADJUST 93	/* No-op */
#define SSH2_MSG_CHANNEL_DATA	94	/* Data */
#define SSH2_MSG_CHANNEL_EXTENDED_DATA 95	/* Out-of-band data */
#define SSH2_MSG_CHANNEL_EOF	96	/* EOF */
#define SSH2_MSG_CHANNEL_CLOSE	97	/* Close the channel */
#define SSH2_MSG_CHANNEL_REQUEST 98	/* Perform a channel ioctl */
#define SSH2_MSG_CHANNEL_SUCCESS 99	/* Channel request succeeded */
#define SSH2_MSG_CHANNEL_FAILURE 100/* Channel request failed */

/* Special-case expected-packet-type values that are passed to
   readPacketSSHx() to handle situations where more than one return value is
   valid.  CMSG_USER can return failure meaning "no password" even if
   there's no actual failure, CMSG_AUTH_PASSWORD can return SMSG_FAILURE
   which indicates a wrong password used iff it's a response to the client
   sending a password, and MSG_USERAUTH_REQUEST can similarly return a
   failure or success response.

   In addition to these types there's a "any" type which is used during the
   setup negotiation which will accept any (non-error) packet type and return
   the type as the return code */

#define SSH1_MSG_SPECIAL_USEROPT	500	/* Value to handle SSHv1 user name */
#define SSH1_MSG_SPECIAL_PWOPT		501	/* Value to handle SSHv1 password */
#define SSH1_MSG_SPECIAL_RSAOPT		502	/* Value to handle SSHv1 RSA challenge */
#define SSH1_MSG_SPECIAL_ANY		503	/* Any SSHv1 packet type */
#define SSH2_MSG_SPECIAL_USERAUTH	504	/* Value to handle SSHv2 combined auth.*/
#define SSH2_MSG_SPECIAL_USERAUTH_PAM 505	/* Value to handle SSHv2 PAM auth.*/
#define SSH2_MSG_SPECIAL_CHANNEL	506	/* Value to handle channel open */
#define SSH2_MSG_SPECIAL_REQUEST	507	/* Value to handle SSHv2 global/channel req.*/

/* SSHv1 cipher types */

#define SSH1_CIPHER_NONE		0	/* No encryption */
#define SSH1_CIPHER_IDEA		1	/* IDEA/CFB */
#define SSH1_CIPHER_DES			2	/* DES/CBC */
#define SSH1_CIPHER_3DES		3	/* 3DES/inner-CBC (nonstandard) */
#define SSH1_CIPHER_TSS			4	/* Deprecated */
#define SSH1_CIPHER_RC4			5	/* RC4 */
#define SSH1_CIPHER_BLOWFISH	6	/* Blowfish */
#define SSH1_CIPHER_CRIPPLED	7	/* Reserved, from ssh 1.2.x source */

/* SSHv1 authentication types */

#define SSH1_AUTH_RHOSTS		1	/* .rhosts or /etc/hosts.equiv */
#define SSH1_AUTH_RSA			2	/* RSA challenge-response */
#define SSH1_AUTH_PASSWORD		3	/* Password */
#define SSH1_AUTH_RHOSTS_RSA	4	/* .rhosts with RSA challenge-response */
#define SSH1_AUTH_TIS			5	/* TIS authsrv */
#define SSH1_AUTH_KERBEROS		6	/* Kerberos */
#define SSH1_PASS_KERBEROS_TGT	7	/* Kerberos TGT-passing */

/* SSHv2 disconnection codes */

#define SSH2_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT		1
#define SSH2_DISCONNECT_PROTOCOL_ERROR					2
#define SSH2_DISCONNECT_KEY_EXCHANGE_FAILED				3
#define SSH2_DISCONNECT_RESERVED						4
#define SSH2_DISCONNECT_MAC_ERROR						5
#define SSH2_DISCONNECT_COMPRESSION_ERROR				6
#define SSH2_DISCONNECT_SERVICE_NOT_AVAILABLE			7
#define SSH2_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED	8
#define SSH2_DISCONNECT_HOST_KEY_NOT_VERIFIABLE			9
#define SSH2_DISCONNECT_CONNECTION_LOST					10
#define SSH2_DISCONNECT_BY_APPLICATION					11
#define SSH2_DISCONNECT_TOO_MANY_CONNECTIONS			12
#define SSH2_DISCONNECT_AUTH_CANCELLED_BY_USER			13
#define SSH2_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE	14
#define SSH2_DISCONNECT_ILLEGAL_USER_NAME				15

/* SSHv2 channel open failure codes */

#define SSH_OPEN_ADMINISTRATIVELY_PROHIBITED			1
#define SSH_OPEN_CONNECT_FAILED							2
#define SSH_OPEN_UNKNOWN_CHANNEL_TYPE					3
#define SSH_OPEN_RESOURCE_SHORTAGE						4

/* SSHv2 requires the use of a number of additional (pseudo)-algorithm
   types that don't correspond to normal cryptlib algorithms.  To handle
   these, we define pseudo-algoID values that fall within the range of
   the normal algorithm ID types but that aren't normal algorithm IDs */

#define CRYPT_PSEUDOALGO_DHE		( CRYPT_ALGO_LAST_CONVENTIONAL - 4 )
#define CRYPT_PSEUDOALGO_COPR		( CRYPT_ALGO_LAST_CONVENTIONAL - 3 )
#define CRYPT_PSEUDOALGO_PASSWORD	( CRYPT_ALGO_LAST_CONVENTIONAL - 2 )
#define CRYPT_PSEUDOALGO_PAM		( CRYPT_ALGO_LAST_CONVENTIONAL - 1 )

/* When working with SSH channels there are a number of SSH-internal
   attributes that aren't exposed as cryptlib-wide attribute types.  The
   following values are used to access SSH-internal channel attributes */

typedef enum {
	SSH_ATTRIBUTE_NONE,						/* No channel attribute */
	SSH_ATTRIBUTE_ACTIVE,					/* Channel is active */
	SSH_ATTRIBUTE_WINDOWCOUNT,				/* Data window count */
	SSH_ATTRIBUTE_ALTCHANNELNO,				/* Secondary channel no. */
	SSH_ATRIBUTE_LAST						/* Last channel attribute */
	} SSH_ATTRIBUTE_TYPE;

/* Check whether an algorithm ID is one of the above pseudo-algorithm
   types */

#define isPseudoAlgo( algorithm ) \
		( algorithm >= CRYPT_PSEUDOALGO_DHE && \
		  algorithm <= CRYPT_PSEUDOALGO_PAM )

/* Check whether a DH value is valid for a given server key size */

#define isValidDHsize( value, serverKeySize, extraLength ) \
		( ( value ) > ( ( serverKeySize ) - 8 ) + ( extraLength ) && \
		  ( value ) < ( ( serverKeySize ) + 2 ) + ( extraLength ) )

/****************************************************************************
*																			*
*								SSH Structures								*
*																			*
****************************************************************************/

/* Mapping of SSHv2 algorithm names to cryptlib algorithm IDs, in preferred
   algorithm order */

typedef struct {
	const char FAR_BSS *name;				/* Algorithm name */
	const CRYPT_ALGO_TYPE algo;				/* Algorithm ID */
	} ALGO_STRING_INFO;

/* SSH handshake state information.  This is passed around various
   subfunctions that handle individual parts of the handshake */

typedef struct SH {
	/* SSHv1 session state information/SSHv2 exchange hash */
	BYTE cookie[ SSH2_COOKIE_SIZE + 8 ];	/* Anti-spoofing cookie */
	BYTE sessionID[ CRYPT_MAX_HASHSIZE ];	/* Session ID/exchange hash */
	int sessionIDlength;
	CRYPT_CONTEXT iExchangeHashcontext;		/* Hash of exchanged info */

	/* Information needed to compute the session ID.  SSHv1 requires the
	   host and server key modulus, SSHv2 requires the client and server DH
	   values (along with various other things, but these are hashed
	   inline).  The SSHv2 values are in MPI-encoded form, so we need to
	   reserve a little extra room for the length and leading zero-padding.
	   Since the data fields are rather large and also disjoint, we alias
	   one to the other to save space */
	BYTE clientKeyexValue[ CRYPT_MAX_PKCSIZE + 16 ];
	BYTE serverKeyexValue[ CRYPT_MAX_PKCSIZE + 16 ];
	int clientKeyexValueLength, serverKeyexValueLength;
	#define hostModulus				clientKeyexValue
	#define serverModulus			serverKeyexValue
	#define hostModulusLength		clientKeyexValueLength
	#define serverModulusLength		serverKeyexValueLength

	/* Encryption algorithm and key information */
	CRYPT_ALGO_TYPE pubkeyAlgo;				/* Host signature algo */
	BYTE secretValue[ CRYPT_MAX_PKCSIZE + 8 ];	/* Shared secret value */
	int secretValueLength;

	/* Short-term server key (SSHv1) or DH key agreement context (SSHv2),
	   and the client requested DH key size for the SSHv2 key exchange.
	   Alongside the actual key size, we also store the original encoded
	   form, which has to be hashed as part of the exchange hash.  The
	   long-term host key is stored as the session info iKeyexCryptContext
	   for the client and privateKey for the server */
	CRYPT_CONTEXT iServerCryptContext;
	int serverKeySize, requestedServerKeySize;
	BYTE encodedReqKeySizes[ UINT_SIZE * 3 ];
	int encodedReqKeySizesLength;

	/* Tables mapping SSHv2 algorithm names to cryptlib algorithm IDs.
	   These are declared once in ssh2.c and referred to here via pointers
	   to allow them to be static const, which is necessary in some
	   environments to get them into the read-only segment */
	const ALGO_STRING_INFO FAR_BSS *algoStringPubkeyTbl;

	/* Function pointers to handshaking functions.  These are set up as
	   required depending on whether the protocol being used is v1 or v2,
	   and the session is client or server */
	int ( *beginHandshake )( SESSION_INFO *sessionInfoPtr,
							 struct SH *handshakeInfo );
	int ( *exchangeKeys )( SESSION_INFO *sessionInfoPtr,
						   struct SH *handshakeInfo );
	int ( *completeHandshake )( SESSION_INFO *sessionInfoPtr,
								struct SH *handshakeInfo );
	} SSH_HANDSHAKE_INFO;

/* Channel number and ID used to mark an unused channel */

#define UNUSED_CHANNEL_NO	CRYPT_ERROR
#define UNUSED_CHANNEL_ID	0

/****************************************************************************
*																			*
*								SSH Functions								*
*																			*
****************************************************************************/

/* Unlike SSL, SSH only hashes portions of the handshake, and even then not
   complete packets but arbitrary bits and pieces.  In order to perform the
   hashing, we have to be able to bookmark positions in a stream to allow
   the data at that point to be hashed once it's been encoded.  The following
   macros set and complete a bookmark.

   When we create or continue a packet stream, the packet type is written
   before we can set the bookmark.  To handle this, we also provide a macro
   that sets the bookmark for a full packet by adjusting for the packet type
   that's already been written */

#define streamBookmarkSet( stream, pointer, offset ) \
		pointer = sMemBufPtr( stream ); \
		offset = stell( stream )
#define streamBookmarkSetFullPacket( stream, pointer, offset ) \
		pointer = sMemBufPtr( stream ) - ID_SIZE; \
		offset = stell( stream ) - ID_SIZE
#define streamBookmarkComplete( stream, offset ) \
		offset = stell( stream ) - offset

/* Prototypes for functions in ssh2.c */

int readAlgoString( STREAM *stream, const ALGO_STRING_INFO *algoInfo,
					CRYPT_ALGO_TYPE *algo, const BOOLEAN useFirstMatch,
					void *errorInfo );
int writeAlgoString( STREAM *stream, const CRYPT_ALGO_TYPE algo );
int completeKeyex( SESSION_INFO *sessionInfoPtr,
				   SSH_HANDSHAKE_INFO *handshakeInfo,
				   const BOOLEAN isServer );
void openPacketStreamSSH( STREAM *stream, const SESSION_INFO *sessionInfoPtr,
						  const int bufferSize, const int packetType );
int continuePacketStreamSSH( STREAM *stream, const int packetType );
int processHelloSSH( SESSION_INFO *sessionInfoPtr,
					 SSH_HANDSHAKE_INFO *handshakeInfo, int *keyexLength,
					 const BOOLEAN isServer );

/* Prototypes for functions in ssh2_chn.c */

typedef enum { CHANNEL_NONE, CHANNEL_READ, CHANNEL_WRITE,
			   CHANNEL_BOTH, CHANNEL_LAST } CHANNEL_TYPE;

int createChannel( SESSION_INFO *sessionInfoPtr );
int addChannel( SESSION_INFO *sessionInfoPtr, const long channelNo,
				const int maxPacketSize, const void *type,
				const int typeLen, const void *arg1, const int arg1Len );
int deleteChannel( SESSION_INFO *sessionInfoPtr, const long channelNo,
				   const CHANNEL_TYPE channelType,
				   const BOOLEAN closeLastChannel );
int deleteChannelAddr( SESSION_INFO *sessionInfoPtr, const char *addrInfo,
					   const int addrInfoLen );
int selectChannel( SESSION_INFO *sessionInfoPtr, const long channelNo,
				   const CHANNEL_TYPE channelType );
int getCurrentChannelNo( const SESSION_INFO *sessionInfoPtr,
						 const CHANNEL_TYPE channelType );
CHANNEL_TYPE getChannelStatus( const SESSION_INFO *sessionInfoPtr,
							   const long channelNo );
CHANNEL_TYPE getChannelStatusAddr( const SESSION_INFO *sessionInfoPtr,
								   const char *addrInfo,
								   const int addrInfoLen );
int getChannelAttribute( const SESSION_INFO *sessionInfoPtr,
						 const CRYPT_ATTRIBUTE_TYPE attribute,
						 void *data, int *dataLength );
int setChannelAttribute( SESSION_INFO *sessionInfoPtr,
						 const CRYPT_ATTRIBUTE_TYPE attribute,
						 const void *data, const int dataLength );
int getChannelExtAttribute( const SESSION_INFO *sessionInfoPtr,
							const SSH_ATTRIBUTE_TYPE attribute,
							void *data, int *dataLength );
int setChannelExtAttribute( const SESSION_INFO *sessionInfoPtr,
							const SSH_ATTRIBUTE_TYPE attribute,
							const void *data, const int dataLength );
int enqueueResponse( SESSION_INFO *sessionInfoPtr, const int type,
					 const int noParams, const long channelNo,
					 const int param1, const int param2, const int param3 );
int sendEnqueuedResponse( SESSION_INFO *sessionInfoPtr, const int offset );
int enqueueChannelData( SESSION_INFO *sessionInfoPtr, const int type,
						const long channelNo, const int param );
int appendChannelData( SESSION_INFO *sessionInfoPtr, const int offset );

/* Prototypes for functions in ssh2_msg.c */

int sendChannelOpen( SESSION_INFO *sessionInfoPtr );
int processChannelOpen( SESSION_INFO *sessionInfoPtr, STREAM *stream );
int processChannelControlMessage( SESSION_INFO *sessionInfoPtr,
								  STREAM *stream );
int closeChannel( SESSION_INFO *sessionInfoPtr,
				  const BOOLEAN closeLastChannel );

/* Prototypes for functions in ssh2_cry.c */

typedef enum { MAC_START, MAC_END, MAC_ALL, MAC_LAST } MAC_TYPE;

int initDHcontextSSH( CRYPT_CONTEXT *iCryptContext, int *keySize,
					  const void *keyData, const int keyDataLength,
					  const int requestedKeySize );
int initSecurityInfo( SESSION_INFO *sessionInfoPtr,
					  SSH_HANDSHAKE_INFO *handshakeInfo );
int initSecurityContextsSSH( SESSION_INFO *sessionInfoPtr );
void destroySecurityContextsSSH( SESSION_INFO *sessionInfoPtr );
int hashAsString( const CRYPT_CONTEXT iHashContext,
				  const BYTE *data, const int dataLength );
int hashAsMPI( const CRYPT_CONTEXT iHashContext, const BYTE *data,
			   const int dataLength );
int macPayload( const CRYPT_CONTEXT iMacContext, const long seqNo,
				const BYTE *data, const int dataLength,
				const int packetDataLength, const MAC_TYPE macType,
				const int macLength, const BOOLEAN isRead );

/* Prototypes for functions in ssh2_rw.c */

int wrapPacketSSH2( SESSION_INFO *sessionInfoPtr, STREAM *stream,
					const int offset );
int sendPacketSSH2( SESSION_INFO *sessionInfoPtr, STREAM *stream,
					const BOOLEAN sendOnly );
int readPacketHeaderSSH2( SESSION_INFO *sessionInfoPtr,
						  const int expectedType, long *packetLength,
						  int *packetExtraLength,
						  READSTATE_INFO *readInfo );
int readPacketSSH2( SESSION_INFO *sessionInfoPtr, int expectedType,
					const int minPacketSize );
int getDisconnectInfo( SESSION_INFO *sessionInfoPtr, STREAM *stream );

/* Prototypes for session mapping functions */

void initSSH1processing( SESSION_INFO *sessionInfoPtr,
						 SSH_HANDSHAKE_INFO *handshakeInfo,
						 const BOOLEAN isServer );
void initSSH2processing( SESSION_INFO *sessionInfoPtr,
						 SSH_HANDSHAKE_INFO *handshakeInfo,
						 const BOOLEAN isServer );
void initSSH2clientProcessing( SESSION_INFO *sessionInfoPtr,
							   SSH_HANDSHAKE_INFO *handshakeInfo );
void initSSH2serverProcessing( SESSION_INFO *sessionInfoPtr,
							   SSH_HANDSHAKE_INFO *handshakeInfo );

#ifndef USE_SSH
  #define initSSH2processing	initSSH1processing
#endif /* USE_SSH */
#ifndef USE_SSH1
  #define initSSH1processing	initSSH2processing
#endif /* USE_SSH1 */
#endif /* _SSH_DEFINED */
