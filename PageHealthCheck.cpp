#include "gb-include.h"

#include "TcpServer.h"
#include "Pages.h"
#include "SafeBuf.h"
#include "Stats.h"


bool sendPageHealthCheck( TcpSocket *s , HttpRequest *r ) {
	StackBuf<64*1024> p;
	int32_t uptime = time(NULL) - g_stats.m_uptimeStart;
	
	p.safePrintf("{\n\"status\":\"active\",\n\"uptime_secs\":%" PRId32"\n}\n", uptime);
	
	return g_httpServer.sendDynamicPage (s, (char*)p.getBufStart(), p.length(), -1, false, "application/json", -1, NULL, "utf8" );
}
