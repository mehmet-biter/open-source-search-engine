#include "gb-include.h"

#include "TcpServer.h"
#include "UdpServer.h"
#include "Rdb.h"
#include "Pages.h"
#include "Dns.h"
#include "SafeBuf.h"
#include "Msg13.h"
#include "ip.h"
#include "Url.h"
#include "max_coll_len.h"
#include <algorithm>

static void printTcpTable  (SafeBuf *p, const char *title, TcpServer *server);
static void printUdpTable  (SafeBuf *p, const char *title, const UdpServer *server,
			     const char *coll, int32_t fromIp ,
			    bool isDns = false );

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSockets ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	StackBuf<128*1024> p;
	int32_t collLen = 0;
	const char *coll = r->getString( "c", &collLen );
	char tmp_coll[MAX_COLL_LEN+1];
	if(coll) {
		//copy collection name into tmpbuf and nul-terminate it
		if ( (size_t)collLen > MAX_COLL_LEN )
			collLen = MAX_COLL_LEN;
		memcpy(tmp_coll,coll,collLen);
		tmp_coll[collLen] = '\0';
		coll = tmp_coll;
	}
		
	// print standard header
	g_pages.printAdminTop ( &p, s , r );

	// now print out the sockets table for each tcp server we have
	printTcpTable(&p,"HTTP Server"    ,g_httpServer.getTcp());
	printTcpTable(&p,"HTTPS Server"    ,g_httpServer.getSSLTcp());
	printUdpTable(&p, "Udp Server", &g_udpServer, coll, s->m_ip);
	printUdpTable(&p, "Udp Server (dns)", &g_dns.getUdpServer(), coll, s->m_ip, true);

	// from msg13.cpp print the queued url download requests
	printHammerQueueTable ( &p );

	// calculate buffer length
	int32_t bufLen = p.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						bufLen );
}


void printTcpTable ( SafeBuf* p, const char *title, TcpServer *server ) {
	// table headers for urls current being spiderd
	p->safePrintf ( "<table %s>"
		       "<tr class=hdrow><td colspan=19>"
		       "<center>"
		       //"<font size=+1>"
		       "<b>%s</b>"
		       //"</font>"
		       "</td></tr>"
		       "<tr bgcolor=#%s>"
		       "<td><b>#</td>"
		       "<td><b>fd</td>"
		       "<td><b>age</td>"
		       "<td><b>idle</td>"
		       //"<td><b>timeout</td>"
		       "<td><b>ip</td>"
		       "<td><b>port</td>"
		       "<td><b>state</td>"
		       "<td><b>bytes read</td>"
		       "<td><b>bytes to read</td>"
		       "<td><b>bytes sent</td>"
		       "<td><b>bytes to send</td>"
		       "</tr>\n"
			, TABLE_STYLE
			, title 
			, DARK_BLUE
			);
	// current time in milliseconds
	int64_t now = gettimeofdayInMilliseconds();
	// store in buffer for sorting
	int32_t       times[MAX_TCP_SOCKS];
	TcpSocket *socks[MAX_TCP_SOCKS];
	int32_t nn = 0;
	for ( int32_t i = 0 ; i<=server->m_lastFilled && nn<MAX_TCP_SOCKS; i++ ) {
		// get the ith socket
		TcpSocket *s = server->m_tcpSockets[i];
		// continue if empty
		if ( ! s ) continue;
		// store it
		times[nn] = now - s->m_startTime;
		socks[nn] = s;
		nn++;
	}
	// bubble sort
 keepSorting:
	// assume no swap will happen
	bool didSwap = false;
	for ( int32_t i = 1 ; i < nn ; i++ ) {
		if ( times[i-1] >= times[i] ) continue;
		int32_t       tmpTime = times[i-1];
		TcpSocket *tmpSock = socks[i-1]; 
		times[i-1] = times[i];
		socks[i-1] = socks[i];
		times[i  ] = tmpTime;
		socks[i  ] = tmpSock;
		didSwap = true;
	}
	if ( didSwap ) goto keepSorting;

	// now fill in the columns
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// get the ith socket
		TcpSocket *s = socks[i];
		// set socket state
		const char *st = "ERROR";
		switch ( s->m_sockState ) {
		case ST_AVAILABLE:  st="available";  break;
		//case ST_CLOSED:     st="closed";     break;
		case ST_CONNECTING: st="connecting"; break;
		case ST_READING:    st="reading";    break;
		case ST_SSL_ACCEPT:    st="ssl accept";    break;
		case ST_SSL_SHUTDOWN:    st="ssl shutdown";    break;
		case ST_WRITING:    st="sending";    break;
		case ST_NEEDS_CLOSE:    st="needs close";    break;
		case ST_CLOSE_CALLED:    st="close called";    break;
		case ST_SSL_HANDSHAKE: st = "ssl handshake"; break;
		}
		// bgcolor is lighter for incoming requests
		const char *bg = "c0c0f0";
		if ( s->m_isIncoming ) bg = "e8e8ff";
		// times
		int32_t elapsed1 = now - s->m_startTime      ;
		int32_t elapsed2 = now - s->m_lastActionTime ;
		p->safePrintf ("<tr bgcolor=#%s>"
			       "<td>%" PRId32"</td>" // i
			       "<td>%i</td>" // fd
			       "<td>%" PRId32"ms</td>"  // elapsed seconds since start
			       "<td>%" PRId32"ms</td>"  // last action
			       //"<td>%" PRId32"</td>"  // timeout
			       "<td>%s</td>"  // ip
			       "<td>%hu</td>" // port
			       "<td>%s</td>"  // state
			       ,
			       bg ,
			       i,
			       s->m_sd ,
			       elapsed1,
			       elapsed2,
			       //s->m_timeout ,
			       iptoa(s->m_ip) ,
			       s->m_port ,
			       st );


		// tool tip to show top 500 bytes of send buf
		if ( s->m_readOffset && s->m_readBuf ) {
			p->safePrintf("<td><a title=\"");
			SafeBuf tmp;
			tmp.safeTruncateEllipsis ( s->m_readBuf , 
						   s->m_readOffset ,
						   500 );
			p->htmlEncode ( tmp.getBufStart() );
			p->safePrintf("\">");
			p->safePrintf("<u>%" PRId32"</u></td>",s->m_readOffset);
		}
		else
			p->safePrintf("<td>0</td>");

		p->safePrintf( "<td>%" PRId32"</td>" // bytes to read
			       "<td>%" PRId32"</td>" // bytes sent
			       ,
			       s->m_totalToRead ,
			       s->m_sendOffset
			       );

		// tool tip to show top 500 bytes of send buf
		if ( s->m_totalToSend && s->m_sendBuf ) {
			p->safePrintf("<td><a title=\"");
			SafeBuf tmp;
			tmp.safeTruncateEllipsis ( s->m_sendBuf , 
						   s->m_totalToSend ,
						   500 );
			p->htmlEncode ( tmp.getBufStart() );
			p->safePrintf("\">");
			p->safePrintf("<u>%" PRId32"</u></td>",s->m_totalToSend);
		}
		else
			p->safePrintf("<td>0</td>");

		p->safePrintf("</tr>\n");

	}
	// end the table
	p->safePrintf ("</table><br>\n" );
}

static bool sortByStartTime(const UdpStatistic &s1, const UdpStatistic &s2) {
	return (s1.getStartTime() < s2.getStartTime());
}

static void printUdpTable(SafeBuf *p, const char *title, const UdpServer *server, const char *coll, int32_t fromIp, bool isDns) {
	if (!coll) {
		coll = "main";
	}

	// time now
	int64_t now = gettimeofdayInMilliseconds();

	std::vector<UdpStatistic> udp_statistics = server->getStatistics();
	std::sort(udp_statistics.begin(), udp_statistics.end(), sortByStartTime);

	// count how many of each msg we have
	int32_t msgCount0[MAX_MSG_TYPES] = {};
	int32_t msgCount1[MAX_MSG_TYPES] = {};
	for (auto it = udp_statistics.begin(); it != udp_statistics.end(); ++it) {
		if ( it->getNiceness() == 0 ) {
			msgCount0[it->getMsgType()]++;
		} else {
			msgCount1[it->getMsgType()]++;
		}
	}

	const char *wr = server->getWriteRegistered() ? " [write registered]" : "";

	// print the counts
	p->safePrintf ( "<table %s>"
			"<tr class=hdrow><td colspan=19>"
			"<center>"
			"<b>%s Summary</b> (%" PRId32" transactions)%s"
			"</td></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>niceness</td>"
			"<td><b>msg type</td>"
			"<td><b>total</td>"
			"</tr>",
			TABLE_STYLE,
			title , server->getNumUsedSlots() ,
			wr ,
			DARK_BLUE );
	for ( int32_t i = 0; i < MAX_MSG_TYPES; i++ ) {
		if ( msgCount0[i] <= 0 ) {
			continue;
		}
		p->safePrintf("<tr bgcolor=#%s><td>0</td><td>0x%" PRIx32"</td><td>%" PRId32"</td></tr>", LIGHT_BLUE,i, msgCount0[i]);
	}

	for ( int32_t i = 0; i < MAX_MSG_TYPES; i++ ) {
		if ( msgCount1[i] <= 0 ) {
			continue;
		}
		p->safePrintf("<tr bgcolor=#%s><td>1</td><td>0x%" PRIx32"</td><td>%" PRId32"</td></tr>", LIGHT_BLUE,i, msgCount1[i]);
	}

	p->safePrintf ( "</table><br>" );

	const char *dd = isDns ? "<td><b>hostname</b></td>" : "<td><b>msgType</b></td><td><b>desc</b></td><td><b>hostId</b></td>";
	p->safePrintf ( "<table %s>"
			"<tr class=hdrow><td colspan=19>"
			"<center>"
			"<b>%s</b> (%" PRId32" transactions)"
			"(%" PRId32" incoming)"
			"</td></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>age</td>"
			"<td><b>last read</td>"
			"<td><b>last send</td>"
			"<td><b>timeout</td>"
			"<td><b>ip</td>"
			"%s"
			"<td><b>nice</td>"
			"<td><b>transId</td>"
			"<td><b>called</td>"
			"<td><b>dgrams read</td>"
			"<td><b>dgrams to read</td>"
			"<td><b>acks sent</td>"
			"<td><b>dgrams sent</td>"
			"<td><b>dgrams to send</td>"
			"<td><b>acks read</td>"
			"<td><b>resends</td>"
			"</tr>\n" , 
			TABLE_STYLE,
			title , server->getNumUsedSlots() , 
			server->getNumUsedSlotsIncoming() ,
			DARK_BLUE ,
			dd );


	// now fill in the columns
	for (auto it = udp_statistics.begin(); it != udp_statistics.end(); ++it) {
		char e0[32] = "--";
		char e1[32] = "--";
		char e2[32] = "--";

		if (it->getStartTime() != 0LL) {
			sprintf(e0, "%" PRId64"ms", (now - it->getStartTime()));
		}

		if (it->getLastReadTime() != 0LL) {
			sprintf(e1, "%" PRId64"ms", (now - it->getLastReadTime()));
		}

		if (it->getLastSendTime() != 0LL) {
			sprintf(e2, "%" PRId64"ms", (now - it->getLastSendTime()));
		}

		// bgcolor is lighter for incoming requests
		const char *bg = it->hasCallback() ? LIGHT_BLUE : LIGHTER_BLUE;
		Host *h = g_hostdb.getUdpHost(it->getIp(), it->getPort());
		const char *eip = "??";
		uint16_t eport = 0;
		const char *ehostId = "-1";
		char tmpHostId[64];
		if ( h ) {
			// host can have 2 ip addresses, get the one most
			// similar to that of the requester
			eip = iptoa(g_hostdb.getBestIp(h));
			eport = h->getExternalHttpPort();
			sprintf(tmpHostId, "%s%" PRId32, h->m_isProxy ? "proxy" : "", h->m_hostId);
			ehostId = tmpHostId;
		} else {
			// if no corresponding host, it could be a request from an external
			// cluster, so just show the ip
			sprintf(tmpHostId, "%s", iptoa(it->getIp()));
			ehostId = tmpHostId;
			eip = tmpHostId;
		}

		bool calledHandler = it->hasCallback() ? it->hasCalledCallback() : it->hasCalledHandler();

		p->safePrintf ( "<tr bgcolor=#%s>"
				"<td>%s</td>"  // age
				"<td>%s</td>"  // last read
				"<td>%s</td>"  // last send
				"<td>%" PRId64"</td>",  // timeout
				bg ,
				e0 ,
				e1 ,
				e2 ,
				it->getTimeout() );

		// now use the ip for dns and hosts
		p->safePrintf("<td>%s:%" PRIu32"</td>",
			      iptoa(it->getIp()),(uint32_t)it->getPort());

		const char *cf1 = "";
		const char *cf2 = "";
		if ( it->getConvertedNiceness() ) {
			cf1 = "<font color=red>";
			cf2 = "</font>";
		}

		if ( isDns ) {
			p->safePrintf("<td><nobr>%s", it->getExtraInfo());
			// get the domain from the hostname
			int32_t dlen;
			const char *dbuf = ::getDomFast(it->getExtraInfo(), &dlen, false);
			p->safePrintf( " <a href=\"/admin/tagdb?user=admin&tagtype0=manualban&tagdata0=1&u=%s&c=%s\">"
					       "[<font color=red><b>BAN %s</b></font>]</nobr></a> " ,
			               dbuf , coll , dbuf );
			p->safePrintf("</td><td>%s%" PRId32"%s</td>", cf1, (int32_t)it->getNiceness(), cf2);
		} else {
			// clickable hostId
			const char *toFrom = it->hasCallback() ? "to" : "from";
			p->safePrintf (	"<td>0x%02x</td>"  // msgtype
					"<td><nobr>%s</nobr></td>"  // desc
					"<td><nobr>%s <a href=http://%s:%hu/"
					"admin/sockets?"
					"c=%s>%s</a></nobr></td>"
					"<td>%s%" PRId32"%s</td>" , // niceness
					it->getMsgType() ,
					it->getDescription(),
					// begin clickable hostId
					toFrom,
					eip     ,
					eport   ,
					coll ,
					ehostId ,
					cf1,
					(int32_t)it->getNiceness(),
					cf2
					// end clickable hostId
					);
		}

		const char *rf1 = "";
		const char *rf2 = "";
		if ( it->getResendCount() ) {
			rf1 = "<b style=color:red;>";
			rf2 = "</b>";
		}
			
		p->safePrintf ( "<td>%" PRIu32"</td>" // transId
				"<td>%i</td>" // called handler
				"<td>%" PRId32"</td>" // dgrams read
				"<td>%" PRId32"</td>" // dgrams to read
				"<td>%" PRId32"</td>" // acks sent
				"<td>%" PRId32"</td>" // dgrams sent
				"<td>%" PRId32"</td>" // dgrams to send
				"<td>%" PRId32"</td>" // acks read
				"<td>%s%hhu%s</td>" // resend count
				"</tr>\n" ,
				(uint32_t)it->getTransId(),
				calledHandler,
				it->getNumDatagramRead() ,
				it->getNumPendingRead() ,
				it->getNumAckSent() ,
				it->getNumDatagramSent() ,
				it->getNumPendingSend() ,
				it->getNumAckRead() ,
				rf1 ,
				it->getResendCount() ,
				rf2
				);
	}
	// end the table
	p->safePrintf ("</table><br>\n" );
}
