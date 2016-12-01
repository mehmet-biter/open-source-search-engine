// . this is a virtual TCP socket (TcpSocket)
// . they are 1-1 with all socket descriptors
// . it's used to control the non-blocking polling etc. of the sockets
// . we also use it for re-using sockets w/o having to reconnect

#ifndef GB_TCPSOCKET_H
#define GB_TCPSOCKET_H

#include <sys/time.h>             // timeval data type
#include <openssl/ssl.h>

// . states of a non-blocking TcpSocket 
// . held by TcpSocket's m_sockState member variable
enum TcpSocketState {
	ST_AVAILABLE      = 0,   // means it's connected but not being used
	ST_CONNECTING     = 2,
	//ST_CLOSED       = 3,
	ST_READING        = 4,
	ST_WRITING        = 5,
	ST_NEEDS_CLOSE    = 6,
	ST_CLOSE_CALLED   = 7,
	ST_SSL_ACCEPT     = 8,
	ST_SSL_SHUTDOWN   = 9,
	ST_SSL_HANDSHAKE  = 10,
};

#define TCP_READ_BUF_SIZE 1024

#include "SafeBuf.h"

class TcpSocket {

 public:

	// some handy little thingies...
	bool isAvailable     ( ) { return ( m_sockState == ST_AVAILABLE  ); }
	bool isConnecting    ( ) { return ( m_sockState == ST_CONNECTING ); }
	//bool isClosed      ( ) { return ( m_sockState == ST_CLOSED     ); }
	bool isReading       ( ) { return ( m_sockState == ST_READING ||
					    m_sockState == ST_SSL_ACCEPT ); }
	bool isSending       ( ) { return ( m_sockState == ST_WRITING    ); }
	bool isReadingReply  ( ) { return ( isReading() && m_sendBuf); }
	bool isSendingReply  ( ) { return ( isSending() &&   m_readBuf); }
	bool isSendingRequest( ) { return ( isSending() && ! m_readBuf); }
	bool sendCompleted   ( ) { return ( m_totalSent == m_totalToSend ); }
	bool readCompleted   ( ) { return ( m_totalRead == m_totalToRead ); }

	void setTimeout   (int32_t timeout ) { m_timeout = timeout; }


	// . call m_callback when on transcation completion, error or timeout
	// . m_sockState is the caller's state data
	void           (* m_callback )( void *state , TcpSocket *socket );
	void            *m_state;

	class TcpServer *m_this;

	int         m_sd;               // socket descriptor

	char *m_hostname;
	int32_t m_hostnameSize; // include null terminator

 	int64_t   m_startTime;        // time the send/read started
	int64_t   m_lastActionTime;   // of send or receive or connect

	// m_ip is 0 on dns lookup error, -1 if not found
	int32_t        m_ip;               // ip of connected host
	int16_t       m_port;             // port of connected host
	TcpSocketState        m_sockState;        // see #defines above

	// userid that is logged in
	//int32_t m_userId32;

	int32_t        m_numDestroys;

	char m_tunnelMode;

	// . getMsgPiece() is called when we need more to send
	char       *m_sendBuf;
	int32_t        m_sendBufSize;
	int32_t        m_sendOffset;
	int32_t        m_sendBufUsed; // how much of it is relevant data
	int32_t        m_totalSent;   // bytes sent so far
	int32_t        m_totalToSend;

	// NOTE: for now i've skipped allowing reception of LARGE msgs and
	//       thereby freezing putMsgPiece() for a while
	// . putMsgPiece() is called to flush m_readBuf (if > m_maxReadBufSize)
	char       *m_readBuf;        // might be NULL if unalloc'd
	int32_t        m_readBufSize;    // size of alloc'd buffer, m_readBuf
	int32_t        m_readOffset;     // next position to read into m_readBuf
	//int32_t        m_storeOffset;  // how much of it is stored (putMsgPiece)
	int32_t        m_totalRead;    // bytes read so far
	int32_t        m_totalToRead;    // -1 means unknown
	//void       *m_readCallbackData; // maybe holds reception file handle

	//char        m_tmpBuf[TCP_READ_BUF_SIZE];

	bool        m_waitingOnHandler;
	
	char        m_prefLevel;
	// is it in incoming request socket?
	char        m_isIncoming;

	// timeout (ms) relative to m_lastActionTime (last read or write)
	int32_t        m_timeout;

	// . max bytes to read as a function of content type
	// . varies from collection to collection so you must specify it
	//   in call to HttpServer::getDoc()
	int32_t        m_maxTextDocLen;  // if reading text/html or text/plain
	int32_t        m_maxOtherDocLen; // if reading other doc types

	char        m_niceness;
	bool        m_streamingMode;

	bool m_writeRegistered;

	// SSL members
	SSL  *m_ssl;

	class UdpSlot *m_udpSlot;

	// m_handyBuf is used to hold the parmlist we generate in Pages.cpp
	// which we then broadcast to all the nodes in the cluster. so its
	// just a substitute for avoid the new of a state class.
	SafeBuf m_handyBuf;
	// this maps the requested http path to a service in our
	// WebPages[] array. like "search" or "admin controls" etc.
	int32_t m_pageNum;
};

#endif // GB_TCPSOCKET_H
