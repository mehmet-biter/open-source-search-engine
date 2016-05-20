#include "gb-include.h"

#include "Proxy.h"
#include "Statsdb.h"
#include "Msg13.h"
#include "XmlDoc.h"

Proxy g_proxy;

static void proxyHandlerWrapper ( TcpSocket *s );
//static void gotReplyWrapperPage( void *state, TcpSocket *s );
static void gotHttpReplyWrapper ( void *state, UdpSlot *slot ) ;
//static void gotDataFeedRequestWrapper( void *state );
static void uncountStripe ( class StateControl *stC ) ;

struct StateControl{
	int32_t m_pageNum;
	int64_t m_start;
	int32_t m_reqNum;
	SafeBuf m_sb;
	TcpSocket *m_s;
	int64_t m_startTime;
	bool m_isQuery;
	uint32_t m_hash;
	int32_t m_hostId;
	int32_t m_raw;
	int32_t m_stripe        ;
	int32_t m_numQueryTerms ;
	UdpSlot *m_slot;
	char    *m_slotReadBuf;
	int32_t     m_slotReadBufMaxSize;
	int32_t     m_retries;
	int64_t     m_timeout;
	HttpRequest m_hr;
	Host *m_forwardHost;
};

static void freeStateControl ( StateControl *stC );

Proxy::Proxy() {
	m_proxyId = -1;
	m_proxyRunning = false;
	for (int32_t i =0; i < MAX_HOSTS; i++)
		m_numOutstanding[i] = 0;
	for ( int32_t i = 0 ; i < MAX_STRIPES ; i++ ) {
		m_termsOutOnStripe   [i] = 0;
		m_queriesOutOnStripe [i] = 0;
		m_stripeLastHostId   [i] = -1;
	}
	m_nextStripe = 0;
	m_lastHost = 0;
	m_mainHost = 0;
        //m_msg3c    = NULL;
}

Proxy::~Proxy() {
	/*
        if(m_msg3c) {
                // free the msg3c
                mdelete(m_msg3c, sizeof(Msg3c), "Proxy-Msg3c");
                delete m_msg3c;
                m_msg3c = NULL;
        }
	*/
}

bool Proxy::initHttpServer ( uint16_t httpPort, 
			uint16_t httpsPort ) {
	if ( ! g_httpServer.init( httpPort, httpsPort, 
				  proxyHandlerWrapper ) ) {
		return false;
	}
	return true;
}

bool Proxy::initProxy ( int32_t proxyId, uint16_t udpPort,
			uint16_t udpPort2,UdpProtocol *dp ) {
	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) ){
		log("proxy: setrlimit: core: %s", mstrerror(errno) );
		return false;
	}

	m_proxyId = proxyId;

	// set this in Hostdb too!
	g_hostdb.m_hostId = proxyId;

	m_proxyRunning = true;
	// load up hosts.conf
	/*char *hostsConf = "./hosts.conf";
	g_hostdb.reset();
	if ( ! g_hostdb.init(hostsConf, m_proxyId, NULL,
			     true  ) ) {//isproxy
		log("db: hostdb init failed." ); return 1; }*/

	//gb.conf should be in the same directory as gb
 	//if ( ! g_conf.init ( "./" ) ) { // , h->m_hostId ) ) {
	//	log("db: Conf init failed." ); return 1; }

	//We log the http requests, althogh this is directly done by us
	g_conf.m_logHttpRequests = true;

	//Don't send email alerts, the machines on the cluster can do that
	//g_conf.m_sendEmailAlerts = false;

	// my new email

	// if proxy, always have autosave on so we can save user accouting
	// info regularly for billing feed access. save every 5 minutes.
	g_conf.m_autoSaveFrequency = 5;


	// no, now proxies do too! for out of socket conditions in tcpserver
	g_conf.m_sendEmailAlerts = true;

	g_conf.m_sendEmailAlertsToEmail1 = true;
	// verizon bought us out... smtp-sl.vtext.com
	// was messages.alltel.com
	strcpy ( g_conf.m_email1Addr , "5054503518@vtext.com");
	strcpy ( g_conf.m_email1From , "sysadmin@gigablast.com");
	//strcpy ( g_conf.m_email1MX   , "gbmxrec-vtext.com");
	strcpy ( g_conf.m_email1MX   , "");

	// start pinging right away, udpServer has already been init'ed
	if ( ! g_pingServer.init() ) {
		log("db: PingServer init failed." ); return false; 
	}

	if ( ! g_pingServer.registerHandler() ) 
		return false;

	//Also have to init pages because we need to know which requests to
	//forward. html/gif's, etc can be taken care here itself.
	g_pages.init ( );

	Msg13 msg13;	if ( ! msg13.registerHandler () ) return false;	

	// . then dns Distributed client
	// . server should listen to a socket and register with g_loop
	// . Only the distributed cache shall call the dns server.
	if ( ! g_dns.init( g_hostdb.m_myHost->m_dnsClientPort ) ) {
		log("db: Dns distributed client init failed." ); return 1; }

	MsgC msgc; if ( ! msgc.registerHandler() ) return false;

	//need to init collectiondb too because of addurl
	//set isdump to true because we aren't going to store any data in the
	//collection
	if ( !g_collectiondb.loadAllCollRecs( ) ){ //isDump
		log ("db: collectiondb init failed.");
		return false;
	}
	//init g_msg
	g_msg = "";

	return true;
}

void proxyHandlerWrapper ( TcpSocket *s ){
	g_proxy.handleRequest (s);
}

bool Proxy::handleRequest (TcpSocket *s){

	// if we are a spider compression proxy, do not really act like
	// a proxy at all!
	if ( g_hostdb.m_myHost->m_type == HT_SCPROXY ) {
		//httprequest changes the buf
		g_httpServer.requestHandler(s);
		g_msg = "";
		return true;
	}

	HttpRequest hr;

	bool status = hr.set ( s->m_readBuf , s->m_readOffset , s ) ;
	if ( ! status ) {
		// log a bad request
		log("http: Got bad request from %s: %s",
		    iptoa(s->m_ip),mstrerror(g_errno));
		// cancel the g_errno, we'll send a BAD REQUEST reply to them
		g_errno = 0;
		// . this returns false if blocked, true otherwise
		// . this sets g_errno on error
		// . this will destroy(s) if cannot malloc send buffer
		g_httpServer.sendErrorReply ( s , 400, "Bad Request" );
		return false;
	}

	bool isAdmin = g_conf.isMasterAdmin(s,&hr);

	int32_t redirLen = hr.getRedirLen() ;
	char *redir = NULL;
	if(redirLen > 0) redir = hr.getRedir();

	// redirect everyone away if we should
	if ( !redir &&
	     !isAdmin && 
	     // . we put this here to redirect all traffic somewhere else
	     // . you can set that url in the master controls
	     // . it only redirects there if the raw/code/site/sites is NULL
	     *g_conf.m_redirect != '\0' &&
	     hr.getLong("xml", -1) == -1 &&
	     hr.getLong("raw", -1) == -1 &&
	     hr.getString("code")  == NULL &&
	     hr.getString("site")  == NULL &&
	     hr.getString("sites") == NULL) {
		//direct all non-raw, non admin traffic away.
		redir = g_conf.m_redirect;
		redirLen = gbstrlen(g_conf.m_redirect);
	}

	// . just requesting a static file, like rants.html or logo.gif?
	// . if so just handle that as a normal html/image file
	int32_t n = g_pages.getDynamicPageNumber ( &hr );
	char *path = hr.getPath();
	
	// . i guess right now the proxy is handling admin pages itself
	// . i am changing this so that if &forward=<hostid> is in the url then

	//   the proxy forwards the control page request to the given hostid
	int32_t forward = hr.getLong("forward",-1);

	bool handleIt = true;
	if ( forward != -1       ) handleIt = false;
	//if ( n == PAGE_ROOT      ) handleIt = false;
	if ( n == PAGE_GET       ) handleIt = false;
	if ( n == PAGE_RESULTS   ) handleIt = false;

	// proxy now handles the shell addurl page, the actual request
	// made by the ajax in the shell to add the url has a &id= in it
	// and should go to the backend for adding the url. but we can
	// handle the shell, just the add url page html including the ajax
	// script.
	int32_t cgiId = hr.getLong("id",0);
	if ( n == PAGE_ADDURL && cgiId ) handleIt = false;

	// our new cached page representation format
	if ( ! strncmp(path,"/?id="        ,5 ) ) handleIt = false;

	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	int32_t max;
	if ( tcp == &g_httpServer.m_ssltcp ) max = g_conf.m_httpsMaxSockets;
	else                                 max = g_conf.m_httpMaxSockets;


	// . page addurl uses the udpserver to send the addurl stuff to one of
	//   the hosts, so we need udpserver.
	// . handle the request ourselves if it is not one of these
	//   pages and "forward" was not specified in the url cgi fields
	if ( handleIt ) {
		//httprequest changes the buf
		g_httpServer.requestHandler(s);
		g_msg = "";
		return true;
	}

	// just a safety catch
	if ( max < 20 ) max = 20;

	// enforce the open socket quota iff not admin and not from intranet
	if ( ! isAdmin && tcp->m_numIncomingUsed >= max && 
	     !tcp->closeLeastUsed()) {
		static int32_t s_last = 0;
		static int32_t s_count = 0;
		int32_t now = getTimeLocal();
		if ( now - s_last < 5 ) 
			s_count++;
		else {
			log("query: Too many sockets open. Sending 500 "
			    "http status code to %s. (msgslogged=%" PRId32")[2]",
			    iptoa(s->m_ip),s_count);
			s_count = 0;
			s_last = now;
		}
		g_stats.m_closedSockets++;; 
		return g_httpServer.sendErrorReply ( s , 500 , 
						     "Too many sockets open.");
	}

	bool err2 = false;
	if ( err2 ) {
	hadError2:
		g_errno = ENOMEM;
		log("proxy: new(%i): %s",(int32_t)sizeof(StateControl),
		    mstrerror(g_errno));
		g_msg = " (error: out of memory.)";
		printRequest(s, &hr);
		int32_t bs;
		bool st;
		st=g_httpServer.sendErrorReply(s,500,mstrerror(g_errno),&bs);
		return s;
	}

	// if we get here that means we've got something to forward.
	StateControl *stC;
	try { stC = new (StateControl) ; }
	// return true and set g_errno if couldn't make a new File class
	catch ( ... ) { 
	  goto hadError2;
	}
	mnew ( stC, sizeof(StateControl), "Proxy");

	// make a copy of this now
	if ( ! stC->m_hr.copy ( &hr ) ) {
		mdelete(stC,sizeof(StateControl),"Proxy");
		delete(stC);
		goto hadError2;
	}

	// reset to -1 in case freeStateControl is called
	stC->m_hostId = -1;
	stC->m_slot = NULL;

	// support &xml=1 or &raw=9 or &raw=8 to indicate xml output is wanted
	stC->m_raw = hr.getLong ( "xml", 0 );
	stC->m_raw = hr.getLong("raw",stC->m_raw);
	
	stC->m_s = s;

	stC->m_pageNum = n;

	stC->m_startTime = gettimeofdayInMilliseconds();

	stC->m_isQuery = false;

	//check if we've got a query
	if ( n == PAGE_RESULTS )
		stC->m_isQuery = true;

	stC->m_hash = hash32( hr.getRequest(), hr.getRequestLen() );

	// assume we are not doing a search query (stripe load balancing)
	stC->m_stripe = -1;

	// we need to know how many terms (excluding synonyms)
	// so we can do stripe load balancing by number of query terms.
	// i.e. sending 3 queries of only one term each is about the
	// same as sending one larger query to a single stripe.
	const char *qs = hr.getString("q",NULL);
	Query q;
	if ( qs ) 
		q.set2 ( qs , langUnknown , false ); // 2 = autodetect bool
	// clear g_errno in case Query::set() set it
	g_errno = 0;
	// save it. might be zero!
	stC->m_numQueryTerms = q.getNumTerms();


	Host *h = NULL;

	//if we want the main page, or if it is addurl, cannot send addurl to
	// another host if turing is on. Pick 1 host and keep sending to it
	if ( n == PAGE_REINDEX ||
	     n == PAGE_INJECT  ||
	     n == PAGE_ADDURL  ||
	     //n == PAGE_ROOT   || // FOR DEBUG!!
	     //n == PAGE_RESULTS || // FOR DEBUG!!
	     n == PAGE_SITEDB   )
		// get host #0
		h = g_hostdb.getHost ( 0 );
	else 
		h = pickBestHost ( stC );

	stC->m_retries = 0;

	stC->m_forwardHost = h;

	// . TODO: make both this and Multicast.cpp use a getTimeout() function
	// . default timeout to 8 seconds
	stC->m_timeout = 8 * 1000;
	// set the timeout
	int32_t  firstResult = hr.getLong("s", 0);
	int32_t  docsWanted  = hr.getLong("n", 10);
	// how many docsids request? first 4 bytes of request.
	int32_t  rr          = hr.getLong("rerank",-1);
	// . how many milliseconds of waiting before we re-route?
	// . 100 ms per doc wanted, but if they all end up 
	//   clustering then docsWanted is no indication of the
	//   actual number of titleRecs (or title keys) read
	// . it may take a while to do dup removal on 1 million docs
	int64_t wait = 5000 + 100  * (docsWanted+firstResult);
	// those big UOR queries should not get re-routed all the time
	wait += 1000 * stC->m_numQueryTerms;
	// a min of 8 seconds is good
	if ( wait < 8000 ) wait = 8000;
	// seems like buzz is hammering the cluster and 0x39's are 
	// timing out too much because of huge title recs taking 
	// forever with Msg20
	//if ( wait < 120000 ) wait = 120000;
	// never re-route if it has a rerank, those take forever
	if ( rr >= 0 ) wait = 3000 * 1000;
	// set it
	stC->m_timeout = wait;

	// for now the tool and free and you don't have to login
	return forwardRequest ( stC );
}


bool Proxy::forwardRequest ( StateControl *stC ) {

	// this was a function arg... now it is in "stC"
	Host *h = stC->m_forwardHost;

	stC->m_hostId = h->m_hostId;

	TcpSocket *s = stC->m_s;

	//log (LOG_DEBUG,"query: proxy: (hash=%" PRIu32") %s from "
	//     "hostId #%" PRId32", port %i", stC->m_hash, hr.getRequest(),
	//     h->m_hostId,h->m_httpPort);

	// if sending to the temporary network, add one to port
	int32_t port = h->m_httpPort;
	if ( g_conf.m_useTmpCluster ) port += 1;

	// put ip at end of request
	char *req     = s->m_readBuf;
	int32_t  reqSize = s->m_readOffset;
	// but then TcpServer.cpp leaves some room for a \0 and ip
	char *p = req + reqSize;
	// NULL terminate it
	*p = '\0';
	p += 1;
	// then add in ip
	*(int32_t *)p = s->m_ip;
	p += 4;

	bool isQCProxy = (g_hostdb.m_myHost->m_type & HT_QCPROXY);

	// . alter the request buffer
	// . set the please compress reply flag
	if ( isQCProxy && *req == 'G' ) *req = 'Z'; 

	// update size
	reqSize = p - req;
	// sanity check
	if ( reqSize > s->m_readBufSize ) { char *xx=NULL;*xx=0;}

	// sanity check
	if ( h->m_isProxy ) { char *xx=NULL;*xx=0; }

	// if we are a QUERY COMPRESSION proxy send to the specified address
	int32_t dstIp   = h->m_ip;
	int32_t dstPort = h->m_port;
	int32_t dstId   = h->m_hostId;
	if ( isQCProxy ) {
		dstIp   = g_hostdb.m_myHost->m_forwardIp;
		dstPort = g_hostdb.m_myHost->m_forwardPort;
		dstId   = -1;
	}

	// rewrite &xml=1 as &raw=8 so old search engine sends back xml
	if ( req[0]=='G' &&
	     req[1]=='E' && 
	     req[2]=='T' &&
	     req[3] == ' ' ) {
		// replace &xml=1 in request with &raw=8 to support others
		char *p = req + 4;
		char *pend = req + reqSize;
		// skip GET
		for ( ; p < pend ; p++ ) {
			// stop after url is over
			if ( *p == ' ' ) break;
			// match?
			if ( p[0] != '?' && p[0] != '&' ) continue;
			if ( p[1] != 'x' ) continue;
			if ( p[2] != 'm' ) continue;
			if ( p[3] != 'l' ) continue;
			if ( p[4] != '=' ) continue;
			if ( p[5] != '1' ) continue;
			p[1] = 'r';
			p[2] = 'a';
			p[3] = 'w';
			p[5] = '9';
			break;
		}
	}


	// . let's use the udp server instead because it quickly switches
	//   to using eth1 if eth0 does two or more resends without an ACK,
	//   and vice versa. this ensure that if a network switch fails then
	//   we won't notice it besides a possible one-time 100ms delay.
	// . additionally, we can now accept tcp requests for admin pages
	//   even if such requests come from the proxy ip! because now they
	//   will just have to be from our ssh tunnel!!
	// . returns false and sets g_errno on error, true on success
	// . after resending the request 4 times with no ACK recv'd, call
	//   it a EUDPTIMEDOUT error and deal with that below...
	bool status;
	status = g_udpServer.sendRequest ( req         ,
					   reqSize     ,
					   0xfd        , //msgType 0xfd for fwd
					   dstIp , // h->m_ip     ,
					   dstPort , // h->m_port   ,
					   dstId , // h->m_hostId ,
					   NULL        , // the slotPtr
					   stC         , // state
					   gotHttpReplyWrapper ,
					   stC->m_timeout ,
					   -1          , // backoff
					   -1          , // maxwait
					   NULL        , // replyBuf
					   0           , // replyBufMaxSize
					   0           , // niceness
					   4           );// maxResends

	// if no error, return false, we blocked
	if ( status ) return false;

	//if not, we've got an error
	g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	freeStateControl(stC);

	return true;
}
	    
void gotHttpReplyWrapper ( void *state, UdpSlot *slot ) { // TcpSocket *s ){
	g_proxy.gotReplyPage(state,slot);
}

void Proxy::gotReplyPage ( void *state, UdpSlot *slot ) {

	StateControl *stC = (StateControl *) state;

	char *reply = slot->m_readBuf;
	int32_t  size  = slot->m_readBufSize;

	char *req = slot->m_sendBufAlloc;

	// . AND this is what we forwaded to a host in the flock
	// . we can free this because it reference the tcp buffer
	slot->m_sendBufAlloc = NULL;

	// . try another host if this one times out
	// . if it is dead before we send to it then it will not ACK our
	//   requests, we will resend 10 times within about 300 ms and then
	//   we will get slot->m_errno set to EUDPTIMEDOUT
	// . it will also set the errno to EUDPTIMEDOUT if the timeout we
	//   gave sendRequest() above is reached.
	if ( slot->m_errno == EUDPTIMEDOUT && //stC->m_forward < 0 &&
	     // try this thrice i guess... hopefully we won't pick the same
	     // host we did before!
	     ++stC->m_retries <= 3 ) {
		// reduce the query load counts
		uncountStripe ( stC );
		// pick another host! should NEVER return NULL
		Host *h = pickBestHost ( stC );
		log("proxy: hostid #%" PRId32" timed out. req=%s Rerouting "
		    "forward request "
		    "to hostid #%" PRId32" instead.",stC->m_hostId,
		    req,//stC->m_s->m_readBuf,
		    h->m_hostId);
		// . try a resend!
		// . it will block or it will call sendErrorReply
		stC->m_forwardHost = h;
		forwardRequest ( stC );//, h );
		// all done
		return;
	}

	// save it so we can free "reply" when state is destroyed
	stC->m_slot = slot;

	// save reply so we can free it when this state is freed
	// no i am just transferring into the socket's sendbuf now
	stC->m_slotReadBuf = NULL;

	// should we uncompress the reply?
	bool doUncompress = ( req[0] == 'Z' );
	// do not allow regular proxy to uncompress it though!
	if ( ! (g_hostdb.m_myHost->m_type & HT_QCPROXY ) ) doUncompress=false;

	// don't let udp server free the reply, we forward this to end user
	slot->m_readBuf = NULL;

	// sanity check
	//if ( s->m_readOffset < 0 ) { char *xx=NULL;*xx=0; }
	if ( slot->m_readBufSize < 0 ) { char *xx=NULL;*xx=0; }

	int64_t nowms = gettimeofdayInMilliseconds();

	if ( size == 0 ){
		log (LOG_WARN,"query: Proxy: Lost the request");
		g_errno = EBADREQUEST;
	hadError:
		log(LOG_WARN,"proxy: error=%s req=%s",mstrerror(g_errno),req);
		g_httpServer.sendErrorReply(stC->m_s,500,mstrerror(g_errno));
		freeStateControl(stC);
		return;
	}

	uint64_t took = nowms - stC->m_startTime;

	// if reply was compressed then uncompress it
	if ( doUncompress ) {
		// sanity check
		if ( size < 12 ) { char *xx=NULL;*xx=0; }
		// parse it up
		unsigned char *p = (unsigned char *)reply;
		// get the sizes
		int32_t need  = *(int32_t *)p; p += 4; // uncompressed total size
		int32_t size1 = *(int32_t *)p; p += 4; // size of compressed mime
		int32_t size2 = *(int32_t *)p; p += 4; // size of compressed content
		// note it
		//logf(LOG_DEBUG,"proxy: uncompressing from %" PRId32" to %" PRId32,
		//     size1+size2+12,need);
		// make the decompressed buf
		unsigned char *dbuf = (unsigned char *)mmalloc ( need,"pdbuf");
		unsigned char *dend = dbuf + need;
		if ( ! dbuf ) goto hadError;
		unsigned char *dptr = dbuf;

		uint32_t bytes1 = dend - dptr;
		// ucompress the http mime
		int err1 = gbuncompress (dptr , &bytes1, p , size1 );
		p += size1;
		dptr += bytes1;

		if ( size2 ) {
			uint32_t bytes2 = dend - dptr;
			// uncompress the http content
			int err2 = gbuncompress ( dptr , &bytes2, p , size2 );
			p += size2;
			dptr += bytes2;
			if ( err2 != Z_OK || err1 != Z_OK ) {
				g_errno = EUNCOMPRESSERROR;
				goto hadError;
			}
		}

		// sanity check
		if ( dptr - dbuf != need ) { char *xx=NULL;*xx=0; }

		// free original compressed reply
		mfree ( reply , size , "origreply");

		// now re-set these to the uncompressed mime/pagecontent
		reply = (char *)dbuf;
		size  = need;
	}


	// if we are a regular proxy forwarding a compressed reply to a
	// query compression proxy, then the reply is compressed, just leave
	// it alone
	if ( ( req[0] == 'Z' ) && (g_hostdb.m_myHost->m_type & HT_PROXY ) ) {
		//now should be able to print
		HttpRequest r;
		r.set(stC->m_s->m_readBuf, stC->m_s->m_readOffset, stC->m_s);
		// log the request
		printRequest(stC->m_s, &r, took, NULL,0);//content,contentLen);
		// add stat for stats graph
		if ( stC->m_isQuery ) {
			g_stats.logAvgQueryTime(stC->m_startTime);
			// i dont check if query is raw or not
			int32_t color = 0x00b58869;
			if ( stC->m_raw ) color = 0x00753d30;
			int64_t nowms = gettimeofdayInMilliseconds();
			// . add the stat
			// . use brown for the stat
			g_stats.addStat_r ( 0               ,
					    stC->m_startTime , 
					    nowms ,
					    //"query",
					    color ,
					    STAT_QUERY );
			// add to statsdb as well
			g_statsdb.addStat ( 0 , // niceness
					    "query" ,
					    stC->m_startTime ,
					    nowms            ,
					    stC->m_numQueryTerms );
			g_stats.m_numSuccess++;
		}

		// let tcp server free it when done
		g_httpServer.m_tcp.sendMsg( stC->m_s, reply, size, size, size, NULL, NULL );

		// free mem
		freeStateControl(stC);
		return;
	}
	//char *reply = s->m_readBuf;
	//int32_t size = s->m_readOffset;
	HttpMime mime;
	// re-store original mime from uncompressed mime
	mime.set ( reply, size, NULL);
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 )
		g_msg = " (error: unknown.)";

	if ( stC->m_isQuery && httpStatus == 200 ){
		g_stats.logAvgQueryTime(stC->m_startTime);
		// i dont check if query is raw or not
		int32_t color = 0x00b58869;
		if ( stC->m_raw ) color = 0x00753d30;
		int64_t nowms = gettimeofdayInMilliseconds();
		// . add the stat
		// . use brown for the stat
		g_stats.addStat_r ( 0               ,
				    stC->m_startTime , 
				    nowms ,
				    //"query",
				    color ,
				    STAT_QUERY );
		// add to statsdb as well
		g_statsdb.addStat ( 0 , // niceness
				    "query" ,
				    stC->m_startTime ,
				    nowms            ,
				    stC->m_numQueryTerms );
		g_stats.m_numSuccess++;
	}
	else if ( stC->m_isQuery && httpStatus != 200 )
		g_stats.m_numFails++;
	
	//now should be able to print
	HttpRequest r;
	r.set(stC->m_s->m_readBuf, stC->m_s->m_readOffset, stC->m_s);

	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = size  - mime.getMimeLen();

	printRequest(stC->m_s, &r, took, content,contentLen);

	// . add the login bar to all pages we send back
	// . we could also use to automatically update copyright years 
	//   and add any common elements to every page...
	// . make a new reply to send back...
	// . it may free the old "reply" or it may set newReply=reply...
	int32_t newReplySize = size;
	char *newReply = reply;

	// make sure it is HTTP/1.0 not HTTP/1.1
	if ( reply[0] == 'H' &&
	     reply[1] == 'T' &&
	     reply[2] == 'T' &&
	     reply[3] == 'P' &&
	     reply[4] == '/' &&
	     reply[5] == '1' &&
	     reply[6] == '.' &&
	     reply[7] == '1' )
		reply[7] = '0';

	// . try this one instead
	// . returns false if blocked
	TcpServer *tcp = &g_httpServer.m_tcp;

	// are we using ssl?
	if ( stC->m_s->m_ssl ) {
		tcp = &g_httpServer.m_ssltcp;
	}

	tcp->sendMsg( stC->m_s, newReply, newReplySize, newReplySize, newReplySize, NULL, NULL );

	// do not let udpslot free that we are sending it off
	//slot->m_readBuf = NULL;
	
	freeStateControl(stC);
}


void freeStateControl ( StateControl *stC ){
	if ( ! stC ) return;

	if ( stC->m_hostId >= 0 ) {
		g_proxy.m_numOutstanding[stC->m_hostId]--;
		uncountStripe ( stC );
	}

	// free the reply buffer
	if ( stC->m_slot ) {
		// save reply so we can free it when this state is freed
		char *reply = stC->m_slotReadBuf;
		int32_t  size  = stC->m_slotReadBufMaxSize;
		if ( reply ) mfree ( reply , size , "proxy" );
		// do not double free!
		stC->m_slotReadBuf = NULL;
	}

	mdelete(stC,sizeof(StateControl),"Proxy");
	delete(stC);
}

void uncountStripe ( StateControl *stC ) {
	// if stripe is -1, it was not a search query request
	int32_t stripe = stC->m_stripe;
	if ( stripe < 0 ) return;
	// a more refined load balancing act
	g_proxy.m_termsOutOnStripe[stripe] -= stC->m_numQueryTerms;
	// dec this too
	g_proxy.m_queriesOutOnStripe[stripe]--;
}


// . now do stripe balancing
// . this prevents one machine from receving all the Msg39 requests while its
//   twin gets none
Host *Proxy::pickBestHost( StateControl *stC ) {

	// sanity check, for m_stripeLastHostId array size, which is only 8 now
	int32_t numStripes = g_hostdb.getNumStripes();
	if ( numStripes > MAX_STRIPES ) { char*xx=NULL;*xx=0; }

	// see which stripes have non-dead hosts!
	char stripeDead[MAX_STRIPES];
	bool allDead = true;
	memset ( stripeDead , 1 , MAX_STRIPES );
	int32_t nh = g_hostdb.getNumHosts();
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		if ( g_hostdb.isDead ( h ) ) continue;
		// hey, we are not all dead!
		allDead = false;
		// clear it
		stripeDead [ h->m_stripe ] = 0;
	}

	// start at this stripe
	int32_t ns = m_nextStripe;

	int32_t min ;
	int32_t minns = -1;
	for ( int32_t i = 0 ; i < numStripes ; i++ ) {
		// get stripe number
		if ( ++ns >= numStripes ) ns = 0;
		// skip if whole stripe is dead, and other stripes are not dead
		if ( stripeDead[ns] && ! allDead ) continue;
		// how loaded is this stripe?
		int32_t termsOut = m_termsOutOnStripe[ns];
		// skip if his load is tied or higher than our current winner
		if ( minns != -1 && termsOut >= min ) continue;
		// got a new winner
		minns = ns;
		min   = termsOut;
	}

	// sanity check
	if ( minns == -1 ) { char *xx=NULL;*xx=0; }

	// rotate the prefered next stripe
	if ( ++m_nextStripe >= numStripes ) m_nextStripe = 0;

	// find the next host in line for stripe #minns
	int32_t bestHostId = m_stripeLastHostId[minns];
	// count iterations
	//int32_t count = 0;
 loop:
	// inc it, wrap it
	if ( ++bestHostId >= g_hostdb.getNumHosts() ) bestHostId = 0;

	// skip if dead
	if ( g_hostdb.isDead( bestHostId ) && ! allDead ) goto loop;

	// get the host
	Host *h = g_hostdb.getHost ( bestHostId );

	// saity check
	if ( h->m_isProxy ) { char *xx=NULL;*xx=0; }

	// advance until it is from the least-loaded stripe
	if ( h->m_stripe != minns ) goto loop;


	// add query terms to it for load balancing purposes
	m_termsOutOnStripe[minns] += stC->m_numQueryTerms;
	// inc this too
	m_queriesOutOnStripe[minns]++;
	// save it
	m_stripeLastHostId[minns] = bestHostId;

	// store ino into state so we can reduce the count when we get a reply
	stC->m_stripe        = minns;
	//stC->m_numQueryTerms = numQueryTerms;

	// return it
	return g_hostdb.getHost( bestHostId );
}


void Proxy::printRequest(TcpSocket *s, HttpRequest *r, 
			 uint64_t took ,
			 char *content,
			 int32_t contentLen ) {
	//LOG THE REQUEST

	// get time format: 7/23/1971 10:45:32
	time_t tt = getTimeLocal();
	struct tm *timeStruct = localtime ( &tt );
	char bufTime[64];
	strftime ( bufTime , 63 , "%b %d %T", timeStruct);
	//char *ref = r->getReferer ();

	// if autobanned and we should not log, return now
	if (g_msg&&!g_conf.m_logAutobannedQueries && strstr(g_msg,"autoban")){ 
		g_msg = ""; 
		return; 
	}

	char *req = s->m_readBuf;
	//int32_t  reqLen = s->m_readOffset;

	logf (LOG_INFO,"http: %s %s %s %s",
	      bufTime,iptoa(s->m_ip),req,//r->getRequest(),
	      g_msg);


	//reset g_msg
	g_msg = "";
	if ( (int32_t)took < g_conf.m_logQueryTimeThreshold ) return;

	if ( ! g_conf.m_logQueryReply || ! content || contentLen <= 0 ) {
		logf (LOG_INFO,"http: Took %" PRIu64" ms "
		      "(len=%" PRId32" bytes) "
		      "for request %s",
		      took, contentLen, r->getRequest());
		return;
	}


	// copy into buf
	char *p = (char *)mmalloc ( contentLen+1,"proxycont");
	if ( ! p ) return;

	for ( int32_t i = 0 ; i < contentLen ; i++ ) {
		if ( content[i] && ! is_binary_a(content[i]) ) { 
			p[i]=content[i]; continue; }
		// fix 0's and binary stuff
		p[i]='?';
	}
	// null terminate
	p[contentLen]=0;

	logf (LOG_INFO,"http: Took %" PRIu64" ms "
	      "(len=%" PRId32" bytes) "
	      "for request %s reply=%s",
	      took, contentLen, r->getRequest(),content);

	mfree ( p , contentLen+1, "proxycont");
}
