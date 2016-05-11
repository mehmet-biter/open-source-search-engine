#ifndef GB_PROXY_H
#define GB_PROXY_H

#include "UdpServer.h"
#include "Stats.h"
#include "Pages.h"
#include "UdpProtocol.h"
#include "PingServer.h"
#include <sys/resource.h>  // setrlimit

#define MAX_STRIPES 8

class Proxy {
 public:
	Proxy();
	~Proxy();
	
	void reset();

	bool initHttpServer ( uint16_t httpPort, 
			      uint16_t httpsPort );

	bool initProxy ( int32_t proxyId,
			 uint16_t udpPort,
			 uint16_t udpPort2,
			 UdpProtocol *dp );

	bool handleRequest ( TcpSocket *s );

	bool forwardRequest ( struct StateControl *stC );

	void gotReplyPage ( void *state, class UdpSlot *slot );
	
	Host *pickBestHost ( struct StateControl *stC );

	bool isProxyRunning () {return m_proxyRunning;}
	// are we a proxy?
	bool isProxy        () {return m_proxyRunning;}

	//pages.getUser needs to know if we're proxy to display the admintop
	//and main.cpp needs to set this so that it can stop the proxy
	bool m_proxyRunning;

	// protected:
	void printRequest (TcpSocket *s, HttpRequest *r,
			   uint64_t took = 0,
			   char *content = NULL ,
			   int32_t contentLen = 0 );

	int32_t m_proxyId;
	//number of requests outstanding per hosts
	int32_t m_numOutstanding[MAX_HOSTS];
	//last host to which we sent the request
	int32_t m_lastHost;
	//host to which we pass the index page and the addurl page
	int32_t m_mainHost;

	// assume no more than 8 stripes for now
	int32_t m_stripeLastHostId   [MAX_STRIPES];
	// how many query terms are outstanding on this stripe
	int32_t m_termsOutOnStripe   [MAX_STRIPES];
	int32_t m_queriesOutOnStripe [MAX_STRIPES];
	int32_t m_nextStripe;
};

extern Proxy g_proxy;

#endif // GB_PROXY_H
