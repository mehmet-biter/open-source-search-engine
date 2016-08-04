#include "gb-include.h"

#include "TcpServer.h"
#include "UdpServer.h"
#include "Rdb.h"
#include "Pages.h"
#include "Dns.h"
#include "SafeBuf.h"
#include "Msg13.h"

static void printTcpTable  (SafeBuf *p, const char *title, TcpServer *server);
static void printUdpTable  (SafeBuf *p, const char *title, UdpServer *server,
			     const char *coll, int32_t fromIp ,
			    bool isDns = false );

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSockets ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 128*1024 ];
	SafeBuf p(buf, 128*1024);
	int32_t collLen = 0;
	const char *coll = r->getString( "c", &collLen );
	char tmp_coll[MAX_COLL_LEN+1];
	if(coll) {
		//copy collection name into tmpbuf and nul-terminate it
		if ( (size_t)collLen >= sizeof(tmp_coll) )
			collLen = sizeof(tmp_coll);
		memcpy(tmp_coll,coll,collLen);
		tmp_coll[collLen] = '\0';
		coll = tmp_coll;
	}
		
	// print standard header
	g_pages.printAdminTop ( &p, s , r );

	// now print out the sockets table for each tcp server we have
	printTcpTable(&p,"HTTP Server"    ,g_httpServer.getTcp());
	printTcpTable(&p,"HTTPS Server"    ,g_httpServer.getSSLTcp());
	printUdpTable(&p,"Udp Server" , &g_udpServer,coll,s->m_ip);
	//printUdpTable(&p,"Udp Server(async)",&g_udpServer2,coll,s->m_ip);
	printUdpTable(&p,"Udp Server (dns)", &g_dns.m_udpServer,
		      coll,s->m_ip,true/*isDns?*/);

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

void printUdpTable ( SafeBuf *p, const char *title, UdpServer *server ,
		     const char *coll, int32_t fromIp ,
		     bool isDns ) {
	if ( ! coll ) coll = "main";

	// time now
	int64_t now = gettimeofdayInMilliseconds();
	// get # of used nodes
	//int32_t n = server->getTopUsedSlot();
	// store in buffer for sorting
	int32_t     times[50000];//MAX_UDP_SLOTS];
	UdpSlot *slots[50000];//MAX_UDP_SLOTS];
	int32_t nn = 0;
	for (UdpSlot *s = server->getActiveHead(); s; s = s->m_activeListNext) {
		if ( nn >= 50000 ) {
			log("admin: Too many udp sockets.");
			break;
		}
		// if empty skip it
		//if ( server->isEmpty ( i ) ) continue;
		// get the UdpSlot
		//UdpSlot *s = server->getUdpSlotNum(i);
		// if data is NULL that's an error
		//if ( ! s ) continue;
		// store it
		times[nn] = now - s->getStartTime();
		slots[nn] = s;
		nn++;
	}
	// bubble sort
 keepSorting:
	// assume no swap will happen
	bool didSwap = false;
	for ( int32_t i = 1 ; i < nn ; i++ ) {
		if ( times[i-1] >= times[i] ) continue;
		int32_t     tmpTime = times[i-1];
		UdpSlot *tmpSlot = slots[i-1]; 
		times[i-1] = times[i];
		slots[i-1] = slots[i];
		times[i  ] = tmpTime;
		slots[i  ] = tmpSlot;
		didSwap = true;
	}
	if ( didSwap ) goto keepSorting;

	// count how many of each msg we have
	int32_t msgCount0[MAX_MSG_TYPES] = {};
	int32_t msgCount1[MAX_MSG_TYPES] = {};
	for ( int32_t i = 0; i < nn; i++ ) {
		UdpSlot *s = slots[i];
		if ( s->getNiceness() == 0 )
			msgCount0[s->getMsgType()]++;
		else
			msgCount1[s->getMsgType()]++;
	}

	const char *wr = "";
	if ( server->m_writeRegistered )
		wr = " [write registered]";

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
	for ( int32_t i = 0; i < 96; i++ ) {
		if ( msgCount0[i] <= 0 ) continue;
		p->safePrintf("<tr bgcolor=#%s>"
			      "<td>0</td><td>0x%" PRIx32"</td><td>%" PRId32"</td></tr>",
			      LIGHT_BLUE,i, msgCount0[i]);
	}
	for ( int32_t i = 0; i < 96; i++ ) {
		if ( msgCount1[i] <= 0 ) continue;
		p->safePrintf("<tr bgcolor=#%s>"
			      "<td>1</td><td>0x%" PRIx32"</td><td>%" PRId32"</td></tr>",
			      LIGHT_BLUE,i, msgCount1[i]);
	}
	p->safePrintf ( "</table><br>" );

	const char *dd = "";
	if ( ! isDns ) 
		dd =    "<td><b>msgType</td>"
			"<td><b>desc</td>"
			"<td><b>hostId</td>";
	else {
		dd = //"<td><b>dns ip</b></td>"
		     "<td><b>hostname</b></td>";
	}

	p->safePrintf ( "<table %s>"
			"<tr class=hdrow><td colspan=19>"
			"<center>"
			//"<font size=+1>"
			"<b>%s</b> (%" PRId32" transactions)"
			//"(%" PRId32" requests waiting to processed)"
			"(%" PRId32" incoming)"
			//"</font>"
			"</td></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>age</td>"
			"<td><b>last read</td>"
			"<td><b>last send</td>"
			"<td><b>timeout</td>"
			"<td><b>ip</td>"
			//"<td><b>port</td>"
			//"<td><b>desc</td>"
			//"<td><b>hostId</td>"
			//"<td><b>nice</td>";
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
			//callbackReadyCount ,
			server->getNumUsedSlotsIncoming() ,
			DARK_BLUE ,
			dd );


	// now fill in the columns
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// get from sorted list
		UdpSlot *s = slots[i];
		// set socket state
		//char *st = "ERROR";
		//if ( ! s->isDoneReading() ) st = "reading";
		//if ( ! s->isDoneSending() ) st = "reading";
		// times
		int64_t elapsed0 = (now - s->getStartTime()    ) ;
		int64_t elapsed1 = (now - s->getLastReadTime() ) ;
		int64_t elapsed2 = (now - s->getLastSendTime() ) ;
		char e0[32],e1[32], e2[32];
		sprintf ( e0 , "%" PRId64"ms" , elapsed0 );
		sprintf ( e1 , "%" PRId64"ms" , elapsed1 );
		sprintf ( e2 , "%" PRId64"ms" , elapsed2 );
		if ( s->getStartTime()    == 0LL ) strcpy ( e0 , "--" );
		if ( s->getLastReadTime() == 0LL ) strcpy ( e1 , "--" );
		if ( s->getLastSendTime() == 0LL ) strcpy ( e2 , "--" );
		// bgcolor is lighter for incoming requests
		const char *bg = LIGHT_BLUE;//"c0c0f0";
		// is it incoming
		if ( ! s->hasCallback() ) bg = LIGHTER_BLUE;//"e8e8ff";
		Host *h = g_hostdb.getHost ( s->getIp() , s->getPort() );
		const char           *eip     = "??";
		uint16_t  eport   =  0 ;
		//int32_t          ehostId = -1 ;
		const char           *ehostId = "-1";
		//char tmpIp    [64];
		// print the ip

		char tmpHostId[64];
		if ( h ) {
			// host can have 2 ip addresses, get the one most
			// similar to that of the requester
			eip     = iptoa(g_hostdb.getBestIp ( h , fromIp ));
			//eip     = iptoa(h->m_externalIp) ;
			//eip     = iptoa(h->m_ip) ;
			eport   = h->m_externalHttpPort ;
			//ehostId = h->m_hostId ;
			if ( h->m_isProxy )
				sprintf(tmpHostId,"proxy%" PRId32,h->m_hostId);
			else
				sprintf(tmpHostId,"%" PRId32,h->m_hostId);
			ehostId = tmpHostId;
		}
		// if no corresponding host, it could be a request from an external
		// cluster, so just show the ip
		else {
		        sprintf ( tmpHostId , "%s" , iptoa(s->getIp()) );
			ehostId = tmpHostId;
			eip     = tmpHostId;
		}
		// set description of the msg
		msg_type_t msgType        = s->getMsgType();
		const char *desc          = "";
		char *rbuf          = s->m_readBuf;
		char *sbuf          = s->m_sendBuf;
		int32_t  rbufSize      = s->m_readBufSize;
		int32_t  sbufSize      = s->m_sendBufSize;
		bool  weInit        = s->hasCallback();
		char  calledHandler = s->m_calledHandler;
		if ( weInit ) calledHandler = s->m_calledCallback;
		char *buf     = NULL;
		int32_t  bufSize = 0;
		char tt [ 64 ];

		if (msgType == msg_type_0) {
			buf = weInit ? sbuf : rbuf;
		} else if (msgType == msg_type_1) {
			buf = weInit ? sbuf : rbuf;
		} else if (msgType == msg_type_13) {
			// . if callback was called this slot's sendbuf can be bogus
			// . i put this here to try to avoid a core dump
			if (weInit) {
				if (!s->m_calledCallback) {
					buf = sbuf;
					bufSize = sbufSize;
				}
			} else {
				buf = rbuf;
				bufSize = rbufSize;
			}
		}

		if ( buf ) {
			int32_t rdbId = (msgType == msg_type_1) ? buf[0] : buf[24];
			Rdb *rdb = NULL;
			if (rdbId >= 0 && !isDns) {
				rdb = getRdbFromId((uint8_t) rdbId);
			}

			tt[0] = ' ';
			tt[1] = '\0';
			if (rdb) {
				const char *cmd = ( msgType == msg_type_1 ) ? "add to" : "get from";
				sprintf(tt, "%s %s", cmd, rdb->m_dbname);
			}
			desc = tt;
		}

		if ( msgType == msg_type_c ) {
			desc = "getting ip";
		} else if ( msgType == msg_type_11 ) {
			desc = "ping";
		} else if ( msgType == msg_type_4 ) {
			desc = "meta add";
		} else if ( msgType == msg_type_13 ) {
			bool isRobotsTxt = true;
			if ( buf && bufSize >= (int32_t)sizeof(Msg13Request)-(int32_t)MAX_URL_LEN ) {
				Msg13Request *r = (Msg13Request *)buf;
				isRobotsTxt = r->m_isRobotsTxt;
			}
			desc = isRobotsTxt ? "get robots.txt" : "get web page";
		} else if ( msgType == msg_type_22 ) {
			desc = "get titlerec";
		} else if ( msgType == msg_type_20 ) {
			desc = "get summary";
		} else if ( msgType == msg_type_39 ) {
			desc = "get docids";
		} else if ( msgType == msg_type_7 ) {
			desc = "inject";
		} else if ( msgType == msg_type_25 ) {
			desc = "get link info";
		} else if ( msgType == msg_type_fd ) {
			desc = "proxy forward";
		}
		
		p->safePrintf ( "<tr bgcolor=#%s>"
				"<td>%s</td>"  // age
				"<td>%s</td>"  // last read
				"<td>%s</td>"  // last send
				"<td>%" PRId64"</td>",  // timeout
				bg ,
				e0 ,
				e1 ,
				e2 ,
				s->m_timeout );

		// now use the ip for dns and hosts
		p->safePrintf("<td>%s:%" PRIu32"</td>",
			      iptoa(s->getIp()),(uint32_t)s->getPort());

		const char *cf1 = "";
		const char *cf2 = "";
		if ( s->m_convertedNiceness ) {
			cf1 = "<font color=red>";
			cf2 = "</font>";
		}

		if ( isDns ) {
			p->safePrintf("<td><nobr>%s", s->m_hostname);
			// get the domain from the hostname
			int32_t dlen;
			char *dbuf = ::getDomFast ( s->m_hostname,&dlen,false);
			p->safePrintf( " <a href=\"/admin/tagdb?user=admin&tagtype0=manualban&tagdata0=1&u=%s&c=%s\">"
					       "[<font color=red><b>BAN %s</b></font>]</nobr></a> " ,
			               dbuf , coll , dbuf );
			p->safePrintf("</td><td>%s%" PRId32"%s</td>", cf1, (int32_t)s->getNiceness(), cf2);
		} else {
			// clickable hostId
			const char *toFrom = "to";
			if ( ! s->hasCallback() ) toFrom = "from";
			p->safePrintf (	"<td>0x%02x</td>"  // msgtype
					"<td><nobr>%s</nobr></td>"  // desc
					"<td><nobr>%s <a href=http://%s:%hu/"
					"admin/sockets?"
					"c=%s>%s</a></nobr></td>"
					"<td>%s%" PRId32"%s</td>" , // niceness
					s->getMsgType() ,
					desc,
					// begin clickable hostId
					toFrom,
					eip     ,
					eport   ,
					coll ,
					ehostId ,
					cf1,
					(int32_t)s->getNiceness(),
					cf2
					// end clickable hostId
					);
		}

		const char *rf1 = "";
		const char *rf2 = "";
		if ( s->getResendCount() ) {
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
				(uint32_t)s->getTransId(),
				calledHandler,
				s->getNumDgramsRead() ,
				s->getDatagramsToRead() ,
				s->getNumAcksSent() ,
				s->getNumDgramsSent() ,
				s->getDatagramsToSend() ,
				s->getNumAcksRead() ,
				rf1 ,
				s->getResendCount() ,
				rf2
				);
	}
	// end the table
	p->safePrintf ("</table><br>\n" );
}
