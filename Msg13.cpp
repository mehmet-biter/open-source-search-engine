#include "Msg13.h"
#include "UdpServer.h"
#include "HttpServer.h"
#include "Stats.h"
#include "HashTableX.h"
#include "XmlDoc.h"
#include "SpiderProxy.h" // OP_GETPROXY OP_RETPROXY
#include "RdbCache.h"
#include "Process.h"
#include "GbUtil.h"
#include "zlib.h"

static char g_fakeReply[] =
	"HTTP/1.0 200 (OK)\r\n"
	"Content-Length: 0\r\n"
	"Connection: Close\r\n"
	"Content-Type: text/html\r\n\r\n\0";

bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts );
void gotIframeExpandedContent ( void *state ) ;

bool addToHammerQueue ( Msg13Request *r ) ;
void scanHammerQueue ( int fd , void *state );
void downloadTheDocForReals ( Msg13Request *r ) ;

static void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) ;
static void handleRequest13 ( UdpSlot *slot , int32_t niceness ) ;
static void gotHttpReply    ( void *state , TcpSocket *ts ) ;
static void gotHttpReply2 ( void *state , 
			    char *reply , 
			    int32_t  replySize ,
			    int32_t  replyAllocSize ,
			    TcpSocket *ts ) ;
static void passOnReply     ( void *state , UdpSlot *slot ) ;

bool hasIframe           ( char *reply, int32_t replySize , int32_t niceness );


static bool setProxiedUrlFromSquidProxiedRequest ( Msg13Request *r );
static void stripProxyAuthorization ( char *squidProxiedReqBuf ) ;
static bool addNewProxyAuthorization ( SafeBuf *req , Msg13Request *r );
static void fixGETorPOST ( char *squidProxiedReqBuf ) ;
static int64_t computeProxiedCacheKey64 ( Msg13Request *r ) ;

// cache for robots.txt pages
static RdbCache s_httpCacheRobots;
// cache for other pages
static RdbCache s_httpCacheOthers;
// queue up identical requests
static HashTableX s_rt;

void resetMsg13Caches ( ) {
	s_httpCacheRobots.reset();
	s_httpCacheOthers.reset();
	s_rt.reset();
}

RdbCache *Msg13::getHttpCacheRobots() { return &s_httpCacheRobots; }
RdbCache *Msg13::getHttpCacheOthers() { return &s_httpCacheOthers; }

Msg13::Msg13() {
	m_replyBuf = NULL;
	m_state = NULL;
	m_callback = NULL;
	m_request = NULL;
	m_replyBufSize = 0;
	m_replyBufAllocSize = 0;
}

Msg13::~Msg13() {
	reset();
}

void Msg13::reset() {
	if (m_replyBuf) mfree(m_replyBuf,m_replyBufAllocSize,"msg13rb");
	m_replyBuf = NULL;
}


bool Msg13::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	if ( ! g_udpServer.registerHandler ( msg_type_13, handleRequest13 ))
		return false;

	// use 3MB per cache
	int32_t memRobots = 3000000;
	// make 10MB now that we have proxied url (i.e. squid) capabilities
	int32_t memOthers = 10000000;
	// assume 15k avg cache file
	int32_t maxCacheNodesRobots = memRobots / 106;
	int32_t maxCacheNodesOthers = memOthers / (10*1024);

	if ( ! s_httpCacheRobots.init ( memRobots ,
					-1        , // fixedDataSize
					false     , // lists o recs?
					maxCacheNodesRobots ,
					false     , // use half keys
					"robots.txt"  , // dbname
					true      ))// save to disk
		return false;

	if ( ! s_httpCacheOthers.init ( memOthers ,
					-1        , // fixedDataSize
					false     , // lists o recs?
					maxCacheNodesOthers ,
					false     , // use half keys
					"htmlPages"  , // dbname
					true      ))// save to disk
		return false;

	// . set up the request table (aka wait in line table)
	// . allowDups = "true"
	if ( ! s_rt.set ( 8 ,sizeof(UdpSlot *),0,NULL,0,true,"wait13tbl") )
		return false;

	if ( ! g_loop.registerSleepCallback(10, NULL, scanHammerQueue ) ) {
		log( "build: Failed to register timer callback for hammer queue." );
		return false;
	}


	// success
	return true;
}

// . returns false if blocked, returns true otherwise
// . returns true and sets g_errno on error
bool Msg13::getDoc ( Msg13Request *r, void *state, void(*callback)(void *) ) {
	// reset in case we are being reused
	reset();

	m_state    = state;
	m_callback = callback;

	m_request = r;

	// sanity check
	if ( r->m_urlIp ==  0 ) { g_process.shutdownAbort(true); }
	if ( r->m_urlIp == -1 ) { g_process.shutdownAbort(true); }

	// set this
	r->m_urlHash64 = hash64 ( r->ptr_url , r->size_url-1);

	// is this a /robots.txt url?
	if ( r->size_url - 1 > 12 && 
	     ! strncmp ( r->ptr_url + r->size_url -1 -11,"/robots.txt",11)) {
		r->m_isRobotsTxt = true;
	}

	// force caching if getting robots.txt so is compressed in cache
	if ( r->m_isRobotsTxt ) {
		r->m_compressReply = true;
	}

	// make the cache key
	r->m_cacheKey  = r->m_urlHash64;

	// a compressed reply is different than a non-compressed reply
	if ( r->m_compressReply ) {
		r->m_cacheKey ^= 0xff;
	}

	if ( r->m_isSquidProxiedUrl ) {
		// sets r->m_proxiedUrl that we use a few times below
		setProxiedUrlFromSquidProxiedRequest( r );
	}

	// . if gigablast is acting like a squid proxy, then r->ptr_url
	//   is a COMPLETE http mime request, so hash the following fields in 
	//   the http mime request to make the cache key
	//   * url
	//   * cookie
	// . this is r->m_proxiedUrl which we set above
	if ( r->m_isSquidProxiedUrl ) {
		r->m_cacheKey = computeProxiedCacheKey64( r );
	}

	// assume no http proxy ip/port
	r->m_proxyIp = 0;
	r->m_proxyPort = 0;

	return forwardRequest ( );
}

bool Msg13::forwardRequest ( ) {

	//
	// forward this request to the host responsible for this url's ip
	//
	int32_t nh     = g_hostdb.getNumHosts();
	int32_t hostId = hash32h(((uint32_t)m_request->m_firstIp >> 8), 0) % nh;

	if((uint32_t)m_request->m_firstIp >> 8 == 0) {
		// If the first IP is not set for the request then we don't
		// want to hammer the first host with spidering enabled.
		hostId = hash32n ( m_request->ptr_url ) % nh;
	}

	// get host to send to from hostId
	Host *h = NULL;
	// . pick first alive host, starting with "hostId" as the hostId
	// . if all dead, send to the original and we will timeout > 200 secs
	for ( int32_t count = 0 ; count <= nh ; count++ ) {
		// get that host
		//h = g_hostdb.getProxy ( hostId );;
		h = g_hostdb.getHost ( hostId );

		// Get the other one in shard instead of getting the first
		// one we find sequentially because that makes the load
		// imbalanced to the lowest host with spidering enabled.
		if(!h->m_spiderEnabled) {
			h = g_hostdb.getHost(g_hostdb.getHostIdWithSpideringEnabled(
			  h->m_hostId));
		}

		// stop if he is alive and able to spider
		if ( h->m_spiderEnabled && ! g_hostdb.isDead ( h ) ) break;
		// get the next otherwise
		if ( ++hostId >= nh ) hostId = 0;
	}


	hostId = 0; // HACK!!

	// forward it to self if we are the spider proxy!!!
	if ( g_hostdb.m_myHost->m_isProxy )
		h = g_hostdb.m_myHost;

	// log it
	if ( g_conf.m_logDebugSpider )
		logf ( LOG_DEBUG, 
		       "spider: sending download request of %s firstIp=%s "
		       "uh48=%" PRIu64" to "
		       "host %" PRId32" (child=%" PRId32")", m_request->ptr_url, iptoa(m_request->m_firstIp),
		       m_request->m_urlHash48, hostId,
		       m_request->m_skipHammerCheck);

	// fill up the request
	int32_t requestBufSize = m_request->getSize();

	// we have to serialize it now because it has cookies as well as
	// the url.
	char *requestBuf = serializeMsg ( sizeof(Msg13Request),
					  &m_request->size_url,
					  &m_request->size_cookie,
					  &m_request->ptr_url,
					  m_request,
					  &requestBufSize ,
					  NULL , 
					  0); //RBUF_SIZE
	// g_errno should be set in this case, most likely to ENOMEM
	if ( ! requestBuf ) return true;

	// . otherwise, send the request to the key host
	// . returns false and sets g_errno on error
	// . now wait for 2 minutes before timing out
	// it was not using the proxy! because it thinks the hostid #0 is not the proxy... b/c ninad screwed that
	// up by giving proxies the same ids as regular hosts!
	if (!g_udpServer.sendRequest(requestBuf, requestBufSize, msg_type_13, h->m_ip, h->m_port, -1, NULL, this, gotForwardedReplyWrapper, 200000, 1)) {
		// sanity check
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		// report it
		log("spider: msg13 request: %s",mstrerror(g_errno));
		// g_errno must be set!
		return true;
	}
	// otherwise we block
	return false;
}

void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg13 *THIS = (Msg13 *)state;
	// return if this blocked
	if ( ! THIS->gotForwardedReply ( slot ) ) return;
	// callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg13::gotForwardedReply ( UdpSlot *slot ) {
	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// UdpServer::makeReadBuf() sets m_readBuf to -1 when calling
	// alloc() with a zero length, so fix that
	if ( replySize == 0 ) reply = NULL;
	// this is messed up. why is it happening?
	if ( reply == (void *)-1 ) { g_process.shutdownAbort(true); }

	// we are responsible for freeing reply now
	if ( ! g_errno ) slot->m_readBuf = NULL;

	return gotFinalReply ( reply , replySize , replyAllocSize );
}

#include "PageInject.h"

bool Msg13::gotFinalReply ( char *reply, int32_t replySize, int32_t replyAllocSize ){

	// how is this happening? ah from image downloads...
	if ( m_replyBuf ) { g_process.shutdownAbort(true); }
		
	// assume none
	m_replyBuf     = NULL;
	m_replyBufSize = 0;

	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads )
		logf(LOG_DEBUG,"spider: FINALIZED %s firstIp=%s",
		     m_request->ptr_url,iptoa(m_request->m_firstIp));


	// . if timed out probably the host is now dead so try another one!
	// . return if that blocked
	if ( g_errno == EUDPTIMEDOUT ) {
		// try again
		log("spider: retrying1. had error for %s : %s",
		    m_request->ptr_url,mstrerror(g_errno));
		// return if that blocked
		if ( ! forwardRequest ( ) ) return false;
		// a different g_errno should be set now!
	}

	if ( g_errno ) {
		// this error msg is repeated in XmlDoc::logIt() so no need
		// for it here
		if ( g_conf.m_logDebugSpider )
			log("spider: error for %s: %s",
			    m_request->ptr_url,mstrerror(g_errno));
		return true;
	}

	// set it
	m_replyBuf          = reply;
	m_replyBufSize      = replySize;
	m_replyBufAllocSize = replyAllocSize;

	// sanity check
	if ( replySize > 0 && ! reply ) { g_process.shutdownAbort(true); }

	// no uncompressing if reply is empty
	if ( replySize == 0 ) return true;

	// if it was not compressed we are done! no need to uncompress it
	if ( ! m_request->m_compressReply ) return true;

	// get uncompressed size
	uint32_t unzippedLen = *(int32_t*)reply;
	// sanity checks
	if ( unzippedLen > 10000000 ) {
		log("spider: downloaded probable corrupt gzipped doc "
		    "with unzipped len of %" PRId32,(int32_t)unzippedLen);
		g_errno = ECORRUPTDATA;
		return true;
	}
	// make buffer to hold uncompressed data
	char *newBuf = (char*)mmalloc(unzippedLen, "Msg13Unzip");
	if( ! newBuf ) {
		g_errno = ENOMEM;
		return true;
	}
	// make another var to get mangled by gbuncompress
	uint32_t uncompressedLen = unzippedLen;
	// uncompress it
	int zipErr = gbuncompress( (unsigned char*)newBuf  ,  // dst
				   &uncompressedLen        ,  // dstLen
				   (unsigned char*)reply+4 ,  // src
				   replySize - 4           ); // srcLen
	if(zipErr != Z_OK || 
	   uncompressedLen!=(uint32_t)unzippedLen) {
		log("spider: had error unzipping Msg13 reply. unzipped "
		    "len should be %" PRId32" but is %" PRId32". ziperr=%" PRId32,
		    (int32_t)uncompressedLen,
		    (int32_t)unzippedLen,
		    (int32_t)zipErr);
		mfree (newBuf, unzippedLen, "Msg13UnzipError");
		g_errno = ECORRUPTDATA;//EBADREPLYSIZE;
		return true;
	}

	// count it for stats
	g_stats.m_compressedBytesIn += replySize;

	// free compressed
	mfree ( reply , replyAllocSize ,"ufree" );

	// assign uncompressed
	m_replyBuf          = newBuf;
	m_replyBufSize      = uncompressedLen;
	m_replyBufAllocSize = unzippedLen;

	// log it for now
	if ( g_conf.m_logDebugSpider )
		log("http: got doc %s %" PRId32" to %" PRId32,
		    m_request->ptr_url,(int32_t)replySize,(int32_t)uncompressedLen);

	return true;
}

bool isIpInTwitchyTable ( CollectionRec *cr , int32_t ip ) {
	if ( ! cr ) return false;
	HashTableX *ht = &cr->m_twitchyTable;
	if ( ht->m_numSlots == 0 ) return false;
	return ( ht->getSlot ( &ip ) >= 0 );
}

bool addIpToTwitchyTable ( CollectionRec *cr , int32_t ip ) {
	if ( ! cr ) return true;
	HashTableX *ht = &cr->m_twitchyTable;
	if ( ht->m_numSlots == 0 )
		ht->set ( 4,0,16,NULL,0,false,"twitchtbl",true);
	return ht->addKey ( &ip );
}

RdbCache s_hammerCache;
static bool s_flag = false;
static Msg13Request *s_hammerQueueHead = NULL;
static Msg13Request *s_hammerQueueTail = NULL;

// . only return false if you want slot to be nuked w/o replying
// . MUST always call g_udpServer::sendReply() or sendErrorReply()
void handleRequest13 ( UdpSlot *slot , int32_t niceness  ) {

 	// cast it
	Msg13Request *r = (Msg13Request *)slot->m_readBuf;
	// use slot niceness
	r->m_niceness = niceness;

	// deserialize it now
	deserializeMsg ( sizeof(Msg13),
			 &r->size_url,
			 &r->size_cookie,
			 &r->ptr_url,
			 ((char*)r) + sizeof(*r) );

	// . sanity - otherwise xmldoc::set cores!
	// . no! sometimes the niceness gets converted!
	//if ( niceness == 0 ) { g_process.shutdownAbort(true); }

	// make sure we do not download gigablast.com admin pages!
	if ( g_hostdb.isIpInNetwork ( r->m_firstIp ) && r->size_url-1 >= 7 ) {
		Url url;
		url.set ( r->ptr_url );
		// . never download /master urls from ips of hosts in cluster
		// . TODO: FIX! the pages might be in another cluster!
		// . pages are now /admin/* not any /master/* any more.
		if ( ( //strncasecmp ( url.getPath() , "/master/" , 8 ) == 0 ||
		       strncasecmp ( url.getPath() , "/admin/"  , 7 ) == 0 )) {
			log(LOG_WARN, "spider: Got request to download possible "
			    "gigablast control page %s. Sending back "
			    "ERESTRICTEDPAGE.",
			    url.getUrl());
			g_errno = ERESTRICTEDPAGE;
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
	}

	// . use a max cached age of 24hrs for robots.txt files
	// . this returns true if robots.txt file for hostname found in cache
	// . don't copy since, we analyze immediately and don't block
	char *rec;
	int32_t  recSize;
	// get the cache
	RdbCache *c = &s_httpCacheOthers;
	if ( r->m_isRobotsTxt ) c = &s_httpCacheRobots;
	// the key is just the 64 bit hash of the url
	key96_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
	// see if in there already
	bool inCache = c->getRecord ( (collnum_t)0     , // share btwn colls
				      k                , // cacheKey
				      &rec             ,
				      &recSize         ,
				      true             , // copy?
				      r->m_maxCacheAge , // 24*60*60 ,
				      true             ); // stats?

	// . an empty rec is a cached not found (no robot.txt file)
	// . therefore it's allowed, so set *reply to 1 (true)
	if (inCache) {
		logDebug(g_conf.m_logDebugSpider, "proxy: found %" PRId32" bytes in cache for %s", recSize,r->ptr_url);

		// helpful for debugging. even though you may see a robots.txt
		// redirect and think we are downloading that each time,
		// we are not... the redirect is cached here as well.
		//log("spider: %s was in cache",r->ptr_url);
		// . send the cached reply back
		// . this will free send/read bufs on completion/g_errno
		g_udpServer.sendReply(rec, recSize, rec, recSize, slot);
		return;
	}

	// log it so we can see if we are hammering
	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads ||
	     g_conf.m_logDebugMsg13 )
		logf(LOG_DEBUG,"spider: DOWNLOADING %s firstIp=%s",
		     r->ptr_url,iptoa(r->m_firstIp));

	// temporary hack
	if ( r->m_parent ) { g_process.shutdownAbort(true); }

	if ( ! s_flag ) {
		s_flag = true;
		s_hammerCache.init ( 15000       , // maxcachemem,
				     8          , // fixed data size
				     false      , // support lists?
				     500        , // max nodes
				     false      , // use half keys?
				     "hamcache" , // dbname
				     false      , // load from disk?
				     12         , // key size
				     12         , // data key size?
				     -1         );// numPtrsMax
	}

	// save this
	r->m_udpSlot = slot;

	// send to a proxy if we are doing compression and not a proxy
	if ( r->m_useCompressionProxy && ! g_hostdb.m_myHost->m_isProxy ) {
		// use this key to select which proxy host
		int32_t key = ((uint32_t)r->m_firstIp >> 8);
		// send to host "h"
		Host *h = g_hostdb.getBestSpiderCompressionProxy(&key);
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log(LOG_DEBUG,"spider: sending to compression proxy "
			    "%s:%" PRIu32,iptoa(h->m_ip),(uint32_t)h->m_port);

		// . otherwise, send the request to the key host
		// . returns false and sets g_errno on error
		// . now wait for 2 minutes before timing out
		// we are sending to the proxy so make hostId -1
		if (!g_udpServer.sendRequest((char *)r, r->getSize(), msg_type_13, h->m_ip, h->m_port, -1, NULL, r, passOnReply, 200000, niceness)) {
			// g_errno should be set
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror(g_errno));
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// wait for it
		return;
	}

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// was it in our table of ips that are throttling us?
	r->m_wasInTableBeforeStarting = isIpInTwitchyTable ( cr , r->m_urlIp );

	downloadTheDocForReals ( r );
}

static void downloadTheDocForReals2  ( Msg13Request *r ) ;
static void downloadTheDocForReals3a ( Msg13Request *r ) ;
static void downloadTheDocForReals3b ( Msg13Request *r ) ;

static void gotHttpReply9 ( void *state , TcpSocket *ts ) ;

static void gotProxyHostReplyWrapper ( void *state , UdpSlot *slot ) ;

void downloadTheDocForReals ( Msg13Request *r ) {

	// are we the first?
	bool firstInLine = s_rt.isEmpty ( &r->m_cacheKey );
	// wait in line cuz someone else downloading it now
	if ( ! s_rt.addKey ( &r->m_cacheKey , &r ) ) {
		log(LOG_WARN, "spider: error adding to waiting table %s",r->ptr_url);
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}

	// this means our callback will be called
	if ( ! firstInLine ) {
		log("spider: waiting in line %s",r->ptr_url);
		return;
	}

	downloadTheDocForReals2 ( r );
}

// insertion point when we try to get another proxy to use because the one
// we tried seemed to be ip-banned
void downloadTheDocForReals2 ( Msg13Request *r ) {

	bool useProxies = false;

	// for diffbot turn ON if use robots is off
	if ( r->m_forceUseFloaters ) useProxies = true;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// if you turned on automatically use proxies in spider controls...
	if ( ! useProxies && 
	     cr &&
	     r->m_urlIp != 0 &&
	     r->m_urlIp != -1 &&
	     cr->m_automaticallyUseProxies &&
	     isIpInTwitchyTable( cr, r->m_urlIp ) )
		useProxies = true;

	// we gotta have some proxy ips that we can use
	if ( ! g_conf.m_proxyIps.hasDigits() ) useProxies = false;


	// we did not need a spider proxy ip so send this reuest to a host
	// to download the url
	if ( ! useProxies ) {
		downloadTheDocForReals3a ( r );
		return;
	}

	// before we send out the msg13 request try to get the spider proxy
	// that is the best one to use. only do this if we had spider proxies
	// specified in m_spiderProxyBuf

	r->m_opCode = OP_GETPROXY;

	// if we are being called from gotHttpReply9() below trying to
	// get a new proxy because the last was banned, we have to set
	// these so handleRequest54() in SpiderProxy.cpp can call
	// returnProxy()

	// get first alive host, usually host #0 but if he is dead then
	// host #1 must take over! if all are dead, it returns host #0.
	// so we are guaranteed "h will be non-null
	Host *h = g_hostdb.getFirstAliveHost();

	// now ask that host for the best spider proxy to send to
	// just the top part of the Msg13Request is sent to handleRequest54() now
	if (!g_udpServer.sendRequest((char *)r, r->getProxyRequestSize(), msg_type_54, h->m_ip, h->m_port, -1, NULL, r, gotProxyHostReplyWrapper, udpserver_sendrequest_infinite_timeout)) {
		// sanity check
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		// report it
		log(LOG_WARN, "spider: msg54 request1: %s %s",
		    mstrerror(g_errno),r->ptr_url);
		// crap we gotta send back a reply i guess
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		// g_errno must be set!
		return;
	}
	// otherwise we block
	return;
}

void gotProxyHostReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg13Request *r = (Msg13Request *)state;
	//Msg13 *THIS = r->m_parent;
	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;
	// error getting spider proxy to use?
	if ( g_errno ) {
		// note it
		log(LOG_WARN, "sproxy: got proxy request error: %s",mstrerror(g_errno));
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}
	//
	// the reply is the ip and port of the spider proxy to use
	//
	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	//int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// bad reply? ip/port/LBid
	if ( replySize != sizeof(ProxyReply) ) {
		log(LOG_WARN, "sproxy: bad 54 reply size of %" PRId32" != %" PRId32" %s",
		    replySize,(int32_t)sizeof(ProxyReply),r->ptr_url);
		    
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
		return;
	}

	// set that
	ProxyReply *prep = (ProxyReply *)reply;

	r->m_proxyIp   = prep->m_proxyIp;
	r->m_proxyPort = prep->m_proxyPort;
	// the id of this transaction for the LoadBucket
	// need to save this so we can use it when we send a msg55 request
	// out to tell host #0 how long it took use to use this proxy, etc.
	r->m_lbId = prep->m_lbId;

	// assume no username:password
	r->m_proxyUsernamePwdAuth[0] = '\0';

	// if proxy had one copy into the buf
	if ( prep->m_usernamePwd[0] ) {
		int32_t len = strlen(prep->m_usernamePwd);
		gbmemcpy ( r->m_proxyUsernamePwdAuth , 
			   prep->m_usernamePwd ,
			   len );
		r->m_proxyUsernamePwdAuth[len] = '\0';
	}

	// if this proxy ip seems banned, are there more proxies to try?
	r->m_hasMoreProxiesToTry = prep->m_hasMoreProxiesToTry;

	// . how many proxies have been banned by the urlIP?
	// . the more that are banned the higher the self-imposed crawl delay.
	// . handleRequest54() in SpiderProxy.cpp will check s_banTable 
	//   to count how many are banned for this urlIp. it saves s_banTable
	//   (a hashtable) to disk so it should be persistent.
	r->m_numBannedProxies = prep->m_numBannedProxies;

	downloadTheDocForReals3a ( r );
}

//
// we need r->m_numBannedProxies to be valid for hammer queueing
// so we have to do the hammer queue stuff after getting the proxy reply
//
void downloadTheDocForReals3a ( Msg13Request *r ) {

	// if addToHammerQueue() returns true and queues this url for
	// download later, when ready to download call this function
	r->m_hammerCallback = downloadTheDocForReals3b;

	// . returns true if we queued it for trying later
	// . scanHammerQueue() will call downloadTheDocForReals3(r) for us
	if ( addToHammerQueue ( r ) ) return;

	downloadTheDocForReals3b( r );
}

void downloadTheDocForReals3b ( Msg13Request *r ) {

	int64_t nowms = gettimeofdayInMilliseconds();

	// assume no download start time
	r->m_downloadStartTimeMS = 0;

	// . store time now
	// . no, now we store 0 to indicate in progress, then we
	//   will overwrite it with a timestamp when the download completes
	// . but if measuring crawldelay from beginning of the download then
	//   store the current time
	// . do NOT do this when downloading robots.txt etc. type files
	//   which should have skipHammerCheck set to true
	if ( r->m_crawlDelayFromEnd && ! r->m_skipHammerCheck ) {
		s_hammerCache.addLongLong(0,r->m_firstIp, 0LL);//nowms);
		log("spider: delay from end for %s %s",iptoa(r->m_firstIp),
		    r->ptr_url);
	}
	else if ( ! r->m_skipHammerCheck ) {
		// get time now
		s_hammerCache.addLongLong(0,r->m_firstIp, nowms);
		log(LOG_DEBUG,
		    "spider: adding new time to hammercache for %s %s = %" PRId64,
		    iptoa(r->m_firstIp),r->ptr_url,nowms);
	}
	else {
		log(LOG_DEBUG,
		    "spider: not adding new time to hammer cache for %s %s",
		    iptoa(r->m_firstIp),r->ptr_url);
	}

	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: adding special \"in-progress\" time "
		    "of %" PRId32" for "
		    "firstIp=%s "
		    "url=%s "
		    "to msg13::hammerCache",
		    0,//-1,
		    iptoa(r->m_firstIp),
		    r->ptr_url);

	// note it here
	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("spider: downloading %s (%s) (skiphammercheck=%" PRId32")",
		    r->ptr_url,iptoa(r->m_urlIp) ,
		    (int32_t)r->m_skipHammerCheck);

	char *agent = g_conf.m_spiderUserAgent;

	// after downloading the doc call this callback
	void (* callback) ( void *,TcpSocket *);

	// if using a proxy tell host #0 we are done with that proxy so
	// it can do its proxy load balancing
	if ( r->m_proxyIp && r->m_proxyIp != -1 ) 
		callback = gotHttpReply9;
	// otherwise not using a proxy
	else
		callback = gotHttpReply;

	// debug note
	if ( r->m_proxyIp ) {
		char tmpIp[64];
		sprintf(tmpIp,"%s",iptoa(r->m_urlIp));
		log(LOG_INFO,
		    "sproxy: got proxy %s:%" PRIu32" "
		    "and agent=\"%s\" to spider "
		    "%s %s (numBannedProxies=%" PRId32")",
		    iptoa(r->m_proxyIp),
		    (uint32_t)(uint16_t)r->m_proxyPort,
		    agent,
		    tmpIp,
		    r->ptr_url,
		    r->m_numBannedProxies);
	}

	char *exactRequest = NULL;

	// if simulating squid just pass the proxied request on
	// to the webserver as it is, but without the secret
	// Proxy-Authorization: Basic abcdefgh base64 encoded
	// username/password info.
	if ( r->m_isSquidProxiedUrl ) {
		exactRequest = r->ptr_url;
		stripProxyAuthorization ( exactRequest );
	}

	// convert "GET http://xyz.com/abc" to "GET /abc" if not sending
	// to another proxy... and sending to the actual webserver
	if ( r->m_isSquidProxiedUrl && ! r->m_proxyIp )
		fixGETorPOST ( exactRequest );

	// ALSO ADD authorization to the NEW proxy we are sending to
	// r->m_proxyIp/r->m_proxyPort that has a username:password
	char tmpBuf[1024];
	SafeBuf newReq (tmpBuf,1024);
	if ( r->m_isSquidProxiedUrl && r->m_proxyIp ) {
		newReq.safeStrcpy ( exactRequest );
		addNewProxyAuthorization ( &newReq , r );
		newReq.nullTerm();
		exactRequest = newReq.getBufStart();
	}

	// indicate start of download so we can overwrite the 0 we stored
	// into the hammercache
	r->m_downloadStartTimeMS = nowms;

	// prevent core from violating MAX_DGRAMS #defined in UdpSlot.h
	int32_t maxDocLen1 = r->m_maxTextDocLen;
	int32_t maxDocLen2 = r->m_maxOtherDocLen;
	
	// fix core in UdpServer.cpp from sending back a too big reply
	if ( maxDocLen1 < 0 || maxDocLen1 > MAX_ABSDOCLEN )
		maxDocLen1 = MAX_ABSDOCLEN;
	if ( maxDocLen2 < 0 || maxDocLen2 > MAX_ABSDOCLEN )
		maxDocLen2 = MAX_ABSDOCLEN;

	// . download it
	// . if m_proxyIp is non-zero it will make requests like:
	//   GET http://xyz.com/abc
	if ( ! g_httpServer.getDoc ( r->ptr_url             ,
				     r->m_urlIp           ,
				     0                    , // offset
				     -1                   ,
				     r->m_ifModifiedSince ,
				     r                    , // state
				     callback       , // callback
				     30*1000              , // 30 sec timeout
				     r->m_proxyIp     ,
				     r->m_proxyPort   ,
				     maxDocLen1,//r->m_maxTextDocLen   ,
				     maxDocLen2,//r->m_maxOtherDocLen  ,
				     agent                ,
				     DEFAULT_HTTP_PROTO , // "HTTP/1.0"
				     false , // doPost?
				     r->ptr_cookie , // cookie
				     NULL , // additionalHeader
				     exactRequest , // our own mime!
				     NULL , // postContent
				     // this is NULL or '\0' if not there
				     r->m_proxyUsernamePwdAuth ) ) {
		// return false if blocked
		return;
	}

	// . log this so i know about it
	// . g_errno MUST be set so that we do not DECREMENT
	//   the outstanding dom/ip counts in gotDoc() below
	//   because we did not increment them above
	logf(LOG_DEBUG,"spider: http server had error: %s",mstrerror(g_errno));

	// g_errno should be set
	if ( ! g_errno ) { g_process.shutdownAbort(true); }

	// if did not block -- should have been an error. call callback
	gotHttpReply ( r , NULL );
	return ;
}

static int32_t s_55Out = 0;

void doneReportingStatsWrapper ( void *state, UdpSlot *slot ) {
	// note it
	if ( g_errno )
		log("sproxy: 55 reply: %s",mstrerror(g_errno));

	// clear g_errno i guess
	g_errno = 0;

	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;

	s_55Out--;
}

bool ipWasBanned ( TcpSocket *ts , const char **msg , Msg13Request *r ) {

	// ts will be null if we got a fake reply from a bulk job
	if ( ! ts )
		return false;

	// do not do this on robots.txt files
	if ( r->m_isRobotsTxt )
		return false;

	// g_errno is 104 for 'connection reset by peer'
	if ( g_errno == ECONNRESET ) {
		*msg = "connection reset";
		return true;
	}

	// proxy returns empty reply not ECONNRESET if it experiences 
	// a conn reset
	if ( g_errno == EBADMIME && ts->m_readOffset == 0 ) {
		*msg = "empty reply";
		return true;
	}

	// on other errors do not do the ban check. it might be a
	// tcp time out or something so we have no reply. but connection resets
	// are a popular way of saying, hey, don't hit me so hard.
	if ( g_errno ) return false;

	// if they closed the socket on us we read 0 bytes, assumed
	// we were banned...
	if ( ts->m_readOffset == 0 ) {
		*msg = "empty reply";
		return true;
	}

	// check the http mime for 403 Forbidden
	HttpMime mime;
	mime.set ( ts->m_readBuf , ts->m_readOffset , NULL );

	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus == 403 ) {
		*msg = "status 403 forbidden";
		return true;
	}
	if ( httpStatus == 999 ) {
		*msg = "status 999 request denied";
		return true;
	}
	// let's add this new one
	if ( httpStatus == 503 ) {
		*msg = "status 503 service unavailable";
		return true;
	}

	// TODO: compare a simple checksum of the page content to what
	// we have downloaded previously from this domain or ip. if it 
	// seems to be the same no matter what the url, then perhaps we
	// are banned as well.

	// otherwise assume not.
	*msg = NULL;

	return false;
}

// come here after telling host #0 we are done using this proxy.
// host #0 will update the loadbucket for it, using m_lbId.
void gotHttpReply9 ( void *state , TcpSocket *ts ) {

 	// cast it
	Msg13Request *r = (Msg13Request *)state;


	// if we got a 403 Forbidden or an empty reply
	// then assume the proxy ip got banned so try another.
	const char *banMsg = NULL;
	//bool banned = false;

	if ( g_errno )
		log("msg13: got error from proxy: %s",mstrerror(g_errno));

	if ( g_conf.m_logDebugSpider )
		log("msg13: got proxy reply for %s",r->ptr_url);

	//if ( ! g_errno ) 
	bool banned = ipWasBanned ( ts , &banMsg , r );


	// inc this every time we try
	r->m_proxyTries++;

	// log a handy msg if proxy was banned
	if ( banned ) {
		const char *msg = "No more proxies to try. Using reply as is.";
		if ( r->m_hasMoreProxiesToTry )  msg = "Trying another proxy.";
		char tmpIp[64];
		sprintf(tmpIp,"%s",iptoa(r->m_urlIp));
		log("msg13: detected that proxy %s is banned "
		    "(banmsg=%s) "
		    "(tries=%" PRId32") by "
		    "url %s %s. %s"
		    , iptoa(r->m_proxyIp) // r->m_banProxyIp
		    , banMsg
		    , r->m_proxyTries
		    , tmpIp
		    , r->ptr_url 
		    , msg );
	}

	if ( banned &&
	     // try up to 5 different proxy ips. try to try new c blocks
	     // if available.
	     //r->m_proxyTries < 5 &&
	     // if all proxies are banned for this r->m_urlIp then
	     // this will be false
	     r->m_hasMoreProxiesToTry ) {
		// tell host #0 to add this urlip/proxyip pair to its ban tbl
		// when it sends a msg 0x54 request to get another proxy.
		// TODO: shit, it also has to return the banned proxy...
		r->m_banProxyIp   = r->m_proxyIp;
		r->m_banProxyPort = r->m_proxyPort;
		// . re-download but using a different proxy
		// . handleRequest54 should not increment the outstanding
		//   count beause we should give it the same m_lbId
		// . skip s_rt table since we are already first in line and
		//   others may be waiting for us...
		downloadTheDocForReals2 ( r );
		return;
	}

	// tell host #0 to reduce proxy load cnt
	r->m_opCode = OP_RETPROXY; 

	Host *h = g_hostdb.getFirstAliveHost();
	// now return the proxy. this will decrement the load count on
	// host "h" for this proxy.
	if (g_udpServer.sendRequest((char *)r, r->getProxyRequestSize(), msg_type_54, h->m_ip, h->m_port, -1, NULL, r, doneReportingStatsWrapper, 10000)) {
		// it blocked!
		//r->m_blocked = true;
		s_55Out++;
		// sanity
		if ( s_55Out > 500 )
			log("sproxy: s55out > 500 = %" PRId32,s_55Out);
	}
	// sanity check
	//if ( ! g_errno ) { g_process.shutdownAbort(true); }
	// report it
	if ( g_errno ) log("spider: msg54 request2: %s %s",
			   mstrerror(g_errno),r->ptr_url);
	// it failed i guess proceed
	gotHttpReply( state , ts );
}

void gotHttpReply ( void *state , TcpSocket *ts ) {
	// if we had no error, TcpSocket should be legit
	if ( ts ) {
		gotHttpReply2 ( state , 
				ts->m_readBuf ,
				ts->m_readOffset ,
				ts->m_readBufSize,
				ts );
		// now after we return TcpServer will DESTROY "ts" and
		// free m_readBuf... so we should not have any reference to it
		return;
	}
	// sanity check, if ts is NULL must have g_errno set
	if ( ! g_errno ) { g_process.shutdownAbort(true); } // g_errno=EBADENG...
	// if g_errno is set i guess ts is NULL!
	gotHttpReply2 ( state ,  NULL ,0 , 0 , NULL );
}

void gotHttpReply2 ( void *state , 
		     char *reply , 
		     int32_t  replySize ,
		     int32_t  replyAllocSize ,
		     TcpSocket *ts ) {

	// save error
	int32_t savedErr = g_errno;

	Msg13Request *r    = (Msg13Request *) state;
	UdpSlot      *slot = r->m_udpSlot;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// error?
	if ( g_errno && ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 ) )
		log("spider: http reply (msg13) had error = %s "
		    "for %s at ip %s",
		    mstrerror(savedErr),r->ptr_url,iptoa(r->m_urlIp));

	bool inTable = false;
	bool checkIfBanned = false;
	if ( cr && cr->m_automaticallyBackOff    ) checkIfBanned = true;
	if ( cr && cr->m_automaticallyUseProxies ) checkIfBanned = true;
	// must have a collrec to hold the ips
	if ( checkIfBanned && cr && r->m_urlIp != 0 && r->m_urlIp != -1 )
		inTable = isIpInTwitchyTable ( cr , r->m_urlIp );

	// check if our ip seems banned. if g_errno was ECONNRESET that
	// is an indicator it was throttled/banned.
	const char *banMsg = NULL;
	bool banned = false;
	if ( checkIfBanned )
		banned = ipWasBanned ( ts , &banMsg , r );
	if (  banned )
		// should we turn proxies on for this IP address only?
		log("msg13: url %s detected as banned (%s), "
		    "for ip %s"
		    , r->ptr_url
		    , banMsg
		    , iptoa(r->m_urlIp) 
		    );

	// . add to the table if not in there yet
	// . store in our table of ips we should use proxies for
	// . also start off with a crawldelay of like 1 sec for this
	//   which is not normal for using proxies.
	if ( banned && ! inTable )
		addIpToTwitchyTable ( cr , r->m_urlIp );

	// did we detect it as banned?
	if ( banned && 
	     // retry iff we haven't already, but if we did stop the inf loop
	     ! r->m_wasInTableBeforeStarting &&
	     cr &&
	     ( cr->m_automaticallyBackOff || cr->m_automaticallyUseProxies ) &&
	     // but this is not for proxies... only native crawlbot backoff
	     ! r->m_proxyIp ) {
		// note this as well
		log("msg13: retrying spidered page with new logic for %s",
		    r->ptr_url);
		// reset this so we don't endless loop it
		r->m_wasInTableBeforeStarting = true;
		// reset error
		g_errno = 0;
		/// and retry. it should use the proxy... or at least
		// use a crawldelay of 3 seconds since we added it to the
		// twitchy table.
		downloadTheDocForReals2 ( r );
		// that's it. if it had an error it will send back a reply.
		return;
	}

	// do not print this if we are already using proxies, it is for
	// the auto crawldelay backoff logic only
	if ( banned && r->m_wasInTableBeforeStarting && ! r->m_proxyIp )
		log("msg13: can not retry banned download of %s "
		    "because we knew ip was banned at start",r->ptr_url);

	// get time now
	int64_t nowms = gettimeofdayInMilliseconds();

	// right now there is a 0 in there to indicate in-progress.
	// so we must overwrite with either the download start time or the
	// download end time.
	int64_t timeToAdd = r->m_downloadStartTimeMS;
	if ( r->m_crawlDelayFromEnd ) timeToAdd = nowms;

	// . now store the current time in the cache
	// . do NOT do this for robots.txt etc. where we skip hammer check
	if ( ! r->m_skipHammerCheck ) 
		s_hammerCache.addLongLong(0,r->m_firstIp,timeToAdd);

	// note it
	if ( g_conf.m_logDebugSpider && ! r->m_skipHammerCheck )
		log(LOG_DEBUG,"spider: adding last download time "
		    "of %" PRId64" for firstIp=%s url=%s "
		    "to msg13::hammerCache",
		    timeToAdd,iptoa(r->m_firstIp),r->ptr_url);


	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log(LOG_DEBUG,"spider: got http reply for firstip=%s url=%s "
		    "err=%s",
		    iptoa(r->m_firstIp),r->ptr_url,mstrerror(savedErr));
	

	// sanity. this was happening from iframe download
	//if ( g_errno == EDNSTIMEDOUT ) { g_process.shutdownAbort(true); }

	// . sanity check - robots.txt requests must always be compressed
	// . saves space in the cache
	if ( ! r->m_compressReply && r->m_isRobotsTxt ) {g_process.shutdownAbort(true);}
	// null terminate it always! -- unless already null terminated...
	if ( replySize > 0 && reply[replySize-1] ) reply[replySize++] = '\0';
	// sanity check
	if ( replySize > replyAllocSize ) { g_process.shutdownAbort(true); }

	// save original size
	int32_t originalSize = replySize;

	int32_t niceness = r->m_niceness;

	// sanity check
	if ( replySize>0 && reply[replySize-1]!= '\0') { g_process.shutdownAbort(true); }

	// assume http status is 200
	bool goodStatus = true;

	int64_t *docsPtr     = NULL;
	int64_t *bytesInPtr  = NULL;
	int64_t *bytesOutPtr = NULL;

	// use this mime
	HttpMime mime;
	int32_t httpStatus = 0; // 200;

	// do not do any of the content analysis routines below if we
	// had a g_errno like ETCPTIMEDOUT or EBADMIME or whatever...
	if ( savedErr ) goodStatus = false;

	// no, its on the content only, NOT including mime
	int32_t mimeLen = 0;

	// only bother rewriting the error mime if user wanted compression
	// otherwise, don't bother rewriting it.
	// DO NOT do this if savedErr is set because we end up calling
	// sendErorrReply() below for that!
	if ( replySize>0 && r->m_compressReply && ! savedErr ) {
		// assume fake reply
		if ( reply == g_fakeReply ) {
			httpStatus = 200;
		}
		else {
			// exclude the \0 i guess. use NULL for url.
			mime.set ( reply , replySize - 1, NULL );
			// no, its on the content only, NOT including mime
			mimeLen = mime.getMimeLen();
			// get this
			httpStatus = mime.getHttpStatus();
		}
		// if it's -1, unknown i guess, then force to 505
		// server side error. we get an EBADMIME for our g_errno
		// when we enter this loop sometimes, so in that case...
		if ( httpStatus == -1 ) httpStatus = 505;
		if ( savedErr         ) httpStatus = 505;
		// if bad http status, re-write it
		if ( httpStatus != 200 ) {
			char tmpBuf[2048];
			char *p = tmpBuf;
			p += sprintf( tmpBuf, 
				      "HTTP/1.0 %" PRId32"\r\n"
				      "Content-Length: 0\r\n" ,
				      httpStatus );
			// convery redirect urls back to requester
			char *loc    = mime.getLocationField();
			int32_t  locLen = mime.getLocationFieldLen();
			// if too big, forget it! otherwise we breach tmpBuf
			if ( loc && locLen > 0 && locLen < 1024 ) {
				p += sprintf ( p , "Location: " );
				gbmemcpy ( p , loc , locLen );
				p += locLen;
				gbmemcpy ( p , "\r\n", 2 );
				p += 2;
			}
			// close it up
			p += sprintf ( p , "\r\n" );
			// copy it over as new reply, include \0
			int32_t newSize = p - tmpBuf + 1;
			if ( newSize >= 2048 ) { g_process.shutdownAbort(true); }
			// record in the stats
			docsPtr     = &g_stats.m_compressMimeErrorDocs;
			bytesInPtr  = &g_stats.m_compressMimeErrorBytesIn;
			bytesOutPtr = &g_stats.m_compressMimeErrorBytesOut;
			// only replace orig reply if we are smaller
			if ( newSize < replySize ) {
				gbmemcpy ( reply , tmpBuf , newSize );
				replySize = newSize;
			}
			// reset content hash
			goodStatus = false;
		}
	}				 
				 
	//Xml xml;
	//Words words;

	// point to the content
	char *content = reply + mimeLen;
	// reduce length by that
	int32_t contentLen = replySize - 1 - mimeLen;
	// fix bad crap
	if ( contentLen < 0 ) contentLen = 0;

	// fake http 200 reply?
	if ( reply == g_fakeReply ) { content = NULL; contentLen = 0; }

	if ( replySize > 0 && 
	     goodStatus &&
	     !r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		// get the content type from mime
		char ct = mime.getContentType();
		if ( ct != CT_HTML &&
		     ct != CT_TEXT &&
		     ct != CT_XML &&
		     ct != CT_PDF &&
		     ct != CT_DOC &&
		     ct != CT_XLS &&
		     ct != CT_PPT &&
		     ct != CT_PS ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressBadCTypeDocs;
			bytesInPtr  = &g_stats.m_compressBadCTypeBytesIn;
			bytesOutPtr = &g_stats.m_compressBadCTypeBytesOut;
			replySize = 0;
		}
	}

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		g_process.shutdownAbort(true); }

	bool hasIframe2 = false;
	if ( r->m_compressReply &&
	     goodStatus &&
	     ! r->m_isRobotsTxt )
		hasIframe2 = hasIframe ( reply , replySize, niceness ) ;

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		g_process.shutdownAbort(true); }

	if ( hasIframe2 && 
	     ! r->m_attemptedIframeExpansion &&
	     ! r->m_isSquidProxiedUrl ) {
		// must have ts i think
		if ( ! ts ) { g_process.shutdownAbort(true); }
		// sanity
		if ( ts->m_readBuf != reply ) { g_process.shutdownAbort(true);}
		// . try to expand each iframe tag in there
		// . return without sending a reply back if this blocks
		// . it will return true and set g_errno on error
		// . when it has fully expanded the doc's iframes it we
		//   re-call this gotHttpReply() function but with the
		//   TcpServer's buf swapped out to be the buf that has the
		//   expanded iframes in it
		// . returns false if blocks
		// . returns true if did not block, sets g_errno on error
		// . if it blocked it will recall THIS function
		if ( ! getIframeExpandedContent ( r , ts ) ) {
			if ( g_conf.m_logDebugMsg13 ||
			     g_conf.m_logDebugSpider )
				log("msg13: iframe expansion blocked %s",
				    r->ptr_url);
			return;
		}
		// ok, did we have an error?
		if ( g_errno )
			log("scproxy: xml set for %s had error: %s",
			    r->ptr_url,mstrerror(g_errno));
		// otherwise, i guess we had no iframes worthy of expanding
		// so pretend we do not have any iframes
		hasIframe2 = false;
	}

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		g_process.shutdownAbort(true); }

	// compute content hash
	if ( r->m_contentHash32 && 
	     replySize>0 && 
	     goodStatus &&
	     r->m_compressReply &&
	     // if we got iframes we can't tell if content changed
	     ! hasIframe2 ) {
		// compute it
		int32_t ch32 = getContentHash32Fast( (unsigned char *)content , contentLen);
		// unchanged?
		if ( ch32 == r->m_contentHash32 ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressUnchangedDocs;
			bytesInPtr  = &g_stats.m_compressUnchangedBytesIn;
			bytesOutPtr = &g_stats.m_compressUnchangedBytesOut;
			// do not send anything back
			replySize = 0;
			// and set error
			savedErr = EDOCUNCHANGED;
		}
	}

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		g_process.shutdownAbort(true); }

	// these are typically roots!
	if ( // override HasIFrame with "FullPageRequested" if it has
	     // an iframe, because that is the overriding stat. i.e. if
	     // we ignored if it had iframes, we'd still end up here...
	     ( ! docsPtr || docsPtr == &g_stats.m_compressHasIframeDocs ) &&
	     r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressFullPageDocs;
		bytesInPtr  = &g_stats.m_compressFullPageBytesIn;
		bytesOutPtr = &g_stats.m_compressFullPageBytesOut;
	}
	else if ( ! docsPtr &&
		  r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressHasDateDocs;
		bytesInPtr  = &g_stats.m_compressHasDateBytesIn;
		bytesOutPtr = &g_stats.m_compressHasDateBytesOut;
	}


	if ( r->m_isRobotsTxt && 
	     goodStatus &&
	     ! savedErr &&
	     r->m_compressReply && 
	     httpStatus == 200 ) {

		// . just take out the lines we need...
		// . if no user-agent line matches * or gigabot/flurbot we
		//   will get just a \0 for the reply, replySize=1!

		// record in the stats
		docsPtr     = &g_stats.m_compressRobotsTxtDocs;
		bytesInPtr  = &g_stats.m_compressRobotsTxtBytesIn;
		bytesOutPtr = &g_stats.m_compressRobotsTxtBytesOut;
	}

	// unknown by default
	if ( ! docsPtr ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressUnknownTypeDocs;
		bytesInPtr  = &g_stats.m_compressUnknownTypeBytesIn;
		bytesOutPtr = &g_stats.m_compressUnknownTypeBytesOut;
	}		

	// assume we did not compress it
	bool compressed = false;
	// compress if we should. do not compress if we are original requester
	// because we call gotFinalReply() with the reply right below here.
	// CAUTION: do not compress empty replies.
	// do not bother if savedErr is set because we use sendErrorReply
	// to send that back!
	if ( r->m_compressReply && replySize>0 && ! savedErr ) {
		// how big should the compression buf be?
		int32_t need = sizeof(int32_t) +        // unzipped size
			(int32_t)(replySize * 1.01) + // worst case size
			25;                       // for zlib
		// for 7-zip
		need += 300;
		// back buffer to hold compressed reply
		uint32_t compressedLen;
		char *compressedBuf = (char*)mmalloc(need, "Msg13Zip");
		if ( ! compressedBuf ) {
			g_errno = ENOMEM;
			log(LOG_WARN, "msg13: compression failed1 %s",r->ptr_url);
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}

		// store uncompressed length as first four bytes in the
		// compressedBuf
		*(int32_t *)compressedBuf = replySize;
		// the remaining bytes are for data
		compressedLen = need - 4;
		// leave the first 4 bytes to hold the uncompressed size
		int zipErr = gbcompress( (unsigned char*)compressedBuf+4,
					 &compressedLen,
					 (unsigned char*)reply, 
					 replySize);
		if(zipErr != Z_OK) {
			log("spider: had error zipping Msg13 reply. %s "
			    "(%" PRId32") url=%s",
			    zError(zipErr),(int32_t)zipErr,r->ptr_url);
			mfree (compressedBuf, need, "Msg13ZipError");
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "msg13: compression failed2 %s",r->ptr_url);
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}

		// record the uncompressed size.
		reply          = compressedBuf;
		replySize      = 4 + compressedLen;
		replyAllocSize = need;
		// sanity check
		if ( replySize<0||replySize>100000000 ) { g_process.shutdownAbort(true);}
		// we did compress it
		compressed = true;
	}

	// record the stats
	if ( docsPtr ) {
		// we download a doc
		*docsPtr = *docsPtr + 1;
		// we spidered it at this size
		*bytesInPtr += originalSize;
		// and spit it back out at this size
		*bytesOutPtr += replySize;
		// and this always, the total
		g_stats.m_compressAllDocs++;
		g_stats.m_compressAllBytesIn  += originalSize;
		g_stats.m_compressAllBytesOut += replySize;
	}

	// store reply in the cache (might be compressed)
	if ( r->m_maxCacheAge > 0 ) { // && ! r->m_parent ) {
		// get the cache
		RdbCache *c = &s_httpCacheOthers;
		// use robots cache if we are a robots.txt file
		if ( r->m_isRobotsTxt ) c = &s_httpCacheRobots;
		// key is based on url hash
		key96_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
		// add it, use a generic collection
		c->addRecord ( (collnum_t) 0 , k , reply , replySize );
		// ignore errors caching it
		g_errno = 0;
	}

	// how many have this key?
	int32_t count = s_rt.getCount ( &r->m_cacheKey );
	// sanity check
	if ( count < 1 ) { g_process.shutdownAbort(true); }

	// send a reply for all waiting in line
	int32_t tableSlot;
	// loop
	for ( ; ( tableSlot = s_rt.getSlot ( &r->m_cacheKey) ) >= 0 ; ) {
		// use this
		int32_t err = 0;
		// set g_errno appropriately
		//if ( ! ts || savedErr ) err = savedErr;
		if ( savedErr ) err = savedErr;
		// sanity check. must be empty on any error
		if ( reply && replySize > 0 && err ) {
			// ETCPIMEDOUT can happen with a partial buf
			if ( err != ETCPTIMEDOUT && 
			     // sometimes zipped content from page
			     // is corrupt... we don't even request
			     // gzipped http replies but they send it anyway
			     err != ECORRUPTHTTPGZIP &&
			     // for proxied https urls
			     err != EPROXYSSLCONNECTFAILED &&
			     // now httpserver::gotDoc's call to
			     // unzipReply() can also set g_errno to
			     // EBADMIME
			     err != EBADMIME &&
			     // this happens sometimes in unzipReply()
			     err != ENOMEM &&
			     // broken pipe
			     err != EPIPE &&
			     // connection reset by peer
			     err != ECONNRESET ) {
				log("http: bad error from httpserver get doc: %s",
				    mstrerror(err));
				g_process.shutdownAbort(true);
			}
		}
		// replicate the reply. might return NULL and set g_errno
		char *copy          = reply;
		int32_t  copyAllocSize = replyAllocSize;
		// . only copy it if we are not the last guy in the table
		// . no, now always copy it
		if ( --count > 0 && ! err ) {
			copy          = (char *)mdup(reply,replySize,"msg13d");
			copyAllocSize = replySize;
			// oom doing the mdup? i've seen this core us so fix it
			// because calling sendreply with a NULL
			// 'copy' cores it.
			if ( reply && ! copy ) {
				copyAllocSize = 0;
				err = ENOMEM;
			}
		}
		// this is not freeable
		if ( copy == g_fakeReply ) copyAllocSize = 0;
		// get request
		Msg13Request *r2 = *(Msg13Request **)s_rt.getValueFromSlot(tableSlot);
		// get udp slot for this transaction
		UdpSlot *slot = r2->m_udpSlot;
		// remove from list
		s_rt.removeSlot ( tableSlot );
		// send back error?  maybe...
		if ( err ) {
			if ( g_conf.m_logDebugSpider ||
			     g_conf.m_logDebugMsg13 )
				log("proxy: msg13: sending back error: %s "
				    "for url %s with ip %s",
				    mstrerror(err),
				    r2->ptr_url,
				    iptoa(r2->m_urlIp));
				    
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror(err));
			g_udpServer.sendErrorReply(slot, err);
			continue;
		}
		// for debug for now
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log("msg13: sending reply for %s",r->ptr_url);

		// send reply
		g_udpServer.sendReply(copy, replySize, copy, copyAllocSize, slot);

		// now final udp slot will free the reply, so tcp server
		// no longer has to. set this tcp buf to null then.
		if ( ts && ts->m_readBuf == reply && count == 0 ) 
			ts->m_readBuf = NULL;
	}

	// we free it - if it was never sent over a udp slot
	if ( savedErr && compressed ) 
		mfree ( reply , replyAllocSize , "msg13ubuf" );

	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("msg13: handled reply ok %s",r->ptr_url);
}


void passOnReply ( void *state , UdpSlot *slot ) {
	// send that back
	Msg13Request *r = (Msg13Request *)state;

	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;

	if ( g_errno ) {
		log(LOG_WARN, "spider: error from proxy for %s: %s",
		    r->ptr_url,mstrerror(g_errno));
		    
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply(r->m_udpSlot, g_errno);
		return;
	}

	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// do not allow "slot" to free the read buf since it is being used
	// as the send buf for "udpSlot"
	slot->m_readBuf     = NULL;
	slot->m_readBufSize = 0;
	// prevent udpserver from trying to free g_fakeReply
	if ( reply == g_fakeReply ) replyAllocSize = 0;

	// just forward it on
	g_udpServer.sendReply(reply, replySize, reply, replyAllocSize, r->m_udpSlot);
}

// returns true if <iframe> tag in there
bool hasIframe ( char *reply, int32_t replySize , int32_t niceness ) {
	if ( ! reply || replySize <= 0 ) return false;
	char *p = reply;
	// exclude \0
	char *pend = reply + replySize - 1;
	for ( ; p < pend ; p++ ) {
		if ( *p != '<' ) continue;
		if ( to_lower_a (p[1]) != 'i' ) continue;
		if ( to_lower_a (p[2]) != 'f' ) continue;
		if ( to_lower_a (p[3]) != 'r' ) continue;
		if ( to_lower_a (p[4]) != 'a' ) continue;
		if ( to_lower_a (p[5]) != 'm' ) continue;
		if ( to_lower_a (p[6]) != 'e' ) continue;
		return true;
	}
	return false;
}

// returns false if blocks, true otherwise
bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts ) {

	if ( ! ts ) { g_process.shutdownAbort(true); }

	int32_t niceness = r->m_niceness;

	// ok, we've an attempt now
	r->m_attemptedIframeExpansion = true;

	// we are doing something to destroy reply, so make a copy of it!
	int32_t copySize = ts->m_readOffset + 1;
	char *copy = (char *)mdup ( ts->m_readBuf , copySize , "ifrmcpy" );
	if ( ! copy ) return true;
	// sanity, must include \0 at the end
	if ( copy[copySize-1] ) { g_process.shutdownAbort(true); }

	// need a new state for it, use XmlDoc itself
	XmlDoc *xd;
	try { xd = new ( XmlDoc ); }
	catch ( ... ) {
		mfree ( copy , copySize , "ifrmcpy" );
		g_errno = ENOMEM;
		return true;
	}
	mnew ( xd , sizeof(XmlDoc),"msg13xd");

	// make a fake spider request so we can do it
	SpiderRequest sreq;
	sreq.reset();
	strcpy(sreq.m_url,r->ptr_url);
	int32_t firstIp = hash32n(r->ptr_url);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	sreq.setKey( firstIp,0LL, false );
	sreq.m_isInjecting   = 1; 
	sreq.m_hopCount      = 0;//m_hopCount;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp       = firstIp;

	// log it now
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: expanding iframes for %s",r->ptr_url);

	// . use the enormous power of our new XmlDoc class
	// . this returns false with g_errno set on error
	// . sometimes niceness is 0, like when the UdpSlot
	//   gets its niceness converted, (see
	//   UdpSlot::m_converetedNiceness). 
	if ( ! xd->set4 ( &sreq       ,
			  NULL        ,
			  "main", // HACK!! m_coll  ,
			  NULL        , // pbuf
			  // give it a niceness of 1, we have to be
			  // careful since we are a niceness of 0!!!!
			  1, //niceness, // 1 , 
			  NULL , // content ,
			  false, // deleteFromIndex ,
			  0 )) { // forcedIp
		// log it
		log("scproxy: xmldoc set error: %s",mstrerror(g_errno));
		// now nuke xmldoc
		mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
		delete  ( xd );
		// g_errno should be set if that returned false
		return true;
	}

	// . re-set the niceness because it will core if we set it with
	//   a niceness of 0...
	xd->m_niceness = niceness;

	// we already downloaded the httpReply so this is valid. no need
	// to check robots.txt again for that url, but perhaps for the 
	// iframe urls.
	xd->m_isAllowed      = true;
	xd->m_isAllowedValid = true;

	// save stuff for calling gotHttpReply() back later with the
	// iframe expanded document
	xd->m_r   = r;

	// so XmlDoc::getExtraDoc doesn't have any issues
	xd->m_firstIp = 123456;
	xd->m_firstIpValid = true;

	// try using xmldoc to do it
	xd->m_httpReply          = copy;
	xd->m_httpReplySize      = copySize;
	xd->m_httpReplyAllocSize = copySize;
	xd->m_httpReplyValid     = true;

	// we claimed this buffer, do not let TcpServer destroy it!
	//ts->m_readBuf = NULL;//(char *)0x1234;

	// tell it to skip msg13 and call httpServer.getDoc directly
	xd->m_isSpiderProxy = true;

	// do not let XmlDoc::getRedirUrl() try to get old title rec
	xd->m_oldDocValid    = true;
	xd->m_oldDoc         = NULL;
	// can't be NULL, xmldoc uses for g_errno
	xd->ptr_linkInfo1    = (LinkInfo *)0x01; 
	xd->size_linkInfo1   = 0   ;
	xd->m_linkInfo1Valid = true;

	// call this as callback
	xd->setCallback ( xd , gotIframeExpandedContent );

	xd->m_redirUrlValid = true;
	xd->ptr_redirUrl    = NULL;
	xd->size_redirUrl   = 0;

	xd->m_downloadEndTimeValid = true;
	xd->m_downloadEndTime = gettimeofdayInMillisecondsLocal();

	// now get the expanded content
	char **ec = xd->getExpandedUtf8Content();
	// this means it blocked
	if ( ec == (void *)-1 ) {
		//log("scproxy: waiting for %s",r->ptr_url);
		return false;
	}
	// return true with g_errno set
	if ( ! ec ) {
		log("scproxy: iframe expansion error: %s",mstrerror(g_errno));
		// g_errno should be set
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		// clean up
	}

	// it did not block so signal gotIframeExpandedContent to not call
	// gotHttpReply()
	//xd->m_r = NULL;

	// hey... it did block and we are stil;l printing this!!
	// it happens when the iframe src is google or bing.. usually maps
	// so i'd think indicative of something special
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: got iframe expansion without blocking for url=%s"
		    " err=%s",r->ptr_url,mstrerror(g_errno));

	// save g_errno for returning
	int32_t saved = g_errno;

	// this also means that the iframe tag was probably not expanded
	// because it was from google.com or bing.com or had a bad src attribut
	// or bad url in the src attribute.
	// so we have set m_attemptedIframeExpansion, just recall using
	// the original TcpSocket ptr... and this time we should not be
	// re-called because m_attemptedIframeExpansion is now true
	//gotHttpReply2 ( r, NULL , 0 , 0 , NULL );

	// we can't be messing with it!! otherwise we'd have to reutrn
	// a new reply size i guess
	if ( xd->m_didExpansion ) { g_process.shutdownAbort(true); }

	// now nuke xmldoc
	mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
	delete  ( xd );

	// reinstate g_errno in case mdelete() reset it
	g_errno = saved;

	// no blocking then...
	return true;
}

void gotIframeExpandedContent ( void *state ) {
	// save error in case mdelete nukes it
	int32_t saved = g_errno;

	XmlDoc *xd = (XmlDoc *)state;
	// this was stored in xd
	Msg13Request *r = xd->m_r;

	//log("scproxy: done waiting for %s",r->ptr_url);

	// note it
	if ( g_conf.m_logDebugBuild ) 
		log("scproxy: got iframe expansion for url=%s",r->ptr_url);

	// assume we had no expansion or there was an error
	char *reply          = NULL;
	int32_t  replySize      = 0;

	// . if no error, then grab it
	// . if failed to get the iframe content then m_didExpansion should
	//   be false
	if ( ! g_errno && xd->m_didExpansion ) {
		// original mime should have been valid
		if ( ! xd->m_mimeValid ) { g_process.shutdownAbort(true); }
		// insert the mime into the expansion buffer! m_esbuf
		xd->m_esbuf.insert2 ( xd->m_httpReply ,
				      xd->m_mime.getMimeLen() ,
				      0 );
		// . get our buffer with the expanded iframes in it
		// . make sure that has the mime in it too
		//reply     = xd->m_expandedUtf8Content;
		//replySize = xd->m_expandedUtf8ContentSize;
		// just to make sure nothing bad happens, null this out
		xd->m_expandedUtf8Content = NULL;
		// this new reply includes the original mime!
		reply     = xd->m_esbuf.getBufStart();
		// include \0? yes.
		replySize = xd->m_esbuf.length() + 1;
		// sanity. must be null terminated
		if ( reply[replySize-1] ) { g_process.shutdownAbort(true); }
	}
	// if expansion did not pan out, use original reply i guess
	else if ( ! g_errno ) {
		reply     = xd->m_httpReply;
		replySize = xd->m_httpReplySize;
	}

	// log it so we know why we are getting EDNSTIMEDOUT msgs back
	// on the main cluster!
	if ( g_errno )
		log("scproxy: error getting iframe content for url=%s : %s",
		    r->ptr_url,mstrerror(g_errno));
	// sanity check
	if ( reply && reply[replySize-1] != '\0') { g_process.shutdownAbort(true); }
	// pass back the error we had, if any
	g_errno = saved;
	// . then resume the reply processing up above as if this was the
	//   document that was downloaded. 
	// . PASS g_errno BACK TO THIS if it was set, like ETCPTIMEDOUT
	gotHttpReply2 ( r, reply, replySize , replySize , NULL );

	// no, let's not dup it and pass what we got in, since ts is NULL
	// it should not free it!!!

	// . now destroy it
	// . the reply should have been sent back as a msg13 reply either
	//   as a normal reply or an error reply
	// . nuke out state then, including the xmldoc
	// . was there an error, maybe a TCPTIMEDOUT???
	mdelete ( xd , sizeof(XmlDoc) , "msg13xd" );
	delete  ( xd );
}

#define DELAYPERBAN 500

// how many milliseconds should spiders use for a crawldelay if
// ban was detected and no proxies are being used.
#define AUTOCRAWLDELAY 5000

// returns true if we queue the request to download later
bool addToHammerQueue ( Msg13Request *r ) {

	// sanity
	if ( ! r->m_udpSlot ) { g_process.shutdownAbort(true); }

	// skip if not needed
	if ( r->m_skipHammerCheck ) return false;

	// . make sure we are not hammering an ip
	// . returns 0 if currently downloading a url from that ip
	// . returns -1 if not found
	int64_t last = s_hammerCache.getLongLong(0,r->m_firstIp,-1,true);
	// get time now
	int64_t nowms = gettimeofdayInMilliseconds();
	// how long has it been since last download START time?
	int64_t waited = nowms - last;

	int32_t crawlDelayMS = r->m_crawlDelayMS;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	bool canUseProxies = false;
	if ( cr && cr->m_automaticallyUseProxies ) canUseProxies = true;
	if ( r->m_forceUseFloaters               ) canUseProxies = true;
	//if ( g_conf.m_useProxyIps          ) canUseProxies = true;
	//if ( g_conf.m_automaticallyUseProxyIps ) canUseProxies = true;

	// if no proxies listed, then it is pointless
	if ( ! g_conf.m_proxyIps.hasDigits() ) canUseProxies = false;

	// if not using proxies, but the ip is banning us, then at least 
	// backoff a bit
	if ( cr && 
	     r->m_urlIp !=  0 &&
	     r->m_urlIp != -1 &&
	     cr->m_automaticallyBackOff &&
	     // and it is in the twitchy table
	     isIpInTwitchyTable ( cr , r->m_urlIp ) ) {
		// then just back off with a crawldelay of 3 seconds
		if ( ! canUseProxies && crawlDelayMS < AUTOCRAWLDELAY )
			crawlDelayMS = AUTOCRAWLDELAY;
		// mark this so we do not retry pointlessly
		r->m_wasInTableBeforeStarting = true;
		// and obey crawl delay
		r->m_skipHammerCheck = false;
	}


	// . if we got a proxybackoff base it on # of banned proxies for urlIp
	// . try to be more sensitive for more sensitive website policies
	// . we don't know why this proxy was banned, or if we were 
	//   responsible, or who banned it, but be more sensitive anyway
	if ( r->m_numBannedProxies &&
	     r->m_numBannedProxies * DELAYPERBAN > crawlDelayMS ) {
		crawlDelayMS = r->m_numBannedProxies * DELAYPERBAN;
		if ( crawlDelayMS > MAX_PROXYCRAWLDELAYMS )
			crawlDelayMS = MAX_PROXYCRAWLDELAYMS;
	}

	// set the crawldelay we actually used when downloading this
	//r->m_usedCrawlDelay = crawlDelayMS;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: got timestamp of %" PRId64" from "
		    "hammercache (waited=%" PRId64" crawlDelayMS=%" PRId32") "
		    "for %s"
		    ,last
		    ,waited
		    ,crawlDelayMS
		    ,iptoa(r->m_firstIp));

	bool queueIt = false;
	if ( last > 0 && waited < crawlDelayMS ) queueIt = true;
	// a "last" of 0 means currently downloading
	if ( crawlDelayMS > 0 && last == 0LL ) queueIt = true;
	// a last of -1 means not found. so first time i guess.
	if ( last == -1 ) queueIt = false;
	// ignore it if from iframe expansion etc.
	if ( r->m_skipHammerCheck ) queueIt = false;

	// . queue it up if we haven't waited int32_t enough
	// . then the functionr, scanHammerQueue(), will re-eval all
	//   the download requests in this hammer queue every 10ms. 
	// . it will just lookup the lastdownload time in the cache,
	//   which will store maybe a -1 if currently downloading...
	if ( queueIt ) {
		// debug
		log(LOG_INFO,
		    "spider: adding %s to crawldelayqueue cd=%" PRId32"ms "
		    "ip=%s",
		    r->ptr_url,crawlDelayMS,iptoa(r->m_urlIp));
		// save this
		//r->m_udpSlot = slot; // this is already saved!
		r->m_nextLink = NULL;
		// we gotta update the crawldelay here in case we modified
		// it in the above logic.
		r->m_crawlDelayMS = crawlDelayMS;
		// when we stored it in the hammer queue
		r->m_stored = nowms;
		// add it to queue
		if ( ! s_hammerQueueHead ) {
			s_hammerQueueHead = r;
			s_hammerQueueTail = r;
		}
		else {
			s_hammerQueueTail->m_nextLink = r;
			s_hammerQueueTail = r;
		}
		return true;
	}
			
	// if we had it in cache check the wait time
	if ( last > 0 && waited < crawlDelayMS ) {
		log("spider: hammering firstIp=%s url=%s "
		    "only waited %" PRId64" ms of %" PRId32" ms",
		    iptoa(r->m_firstIp),r->ptr_url,waited,
		    crawlDelayMS);
		// this guy has too many redirects and it fails us...
		// BUT do not core if running live, only if for test
		// collection
		// for now disable it seems like 99.9% good... but
		// still cores on some wierd stuff...
	}
	// store time now
	//s_hammerCache.addLongLong(0,r->m_firstIp,nowms);
	// note it
	//if ( g_conf.m_logDebugSpider )
	//	log("spider: adding download end time of %" PRIu64" for "
	//	    "firstIp=%s "
	//	    "url=%s "
	//	    "to msg13::hammerCache",
	//	    nowms,iptoa(r->m_firstIp),r->ptr_url);
	// clear error from that if any, not important really
	g_errno = 0;
	return false;
}

// call this once every 10ms to launch queued up download requests so that
// we respect crawl delay for sure
void scanHammerQueue ( int fd , void *state ) {

	if ( ! s_hammerQueueHead ) return;

	int64_t nowms = gettimeofdayInMilliseconds();

 top:

	Msg13Request *r = s_hammerQueueHead;
	if ( ! r ) return;

	Msg13Request *prev = NULL;
	int64_t waited = -1LL;
	Msg13Request *nextLink = NULL;

	//bool useProxies = true;
	// user can turn off proxy use with this switch
	//if ( ! g_conf.m_useProxyIps ) useProxies = false;
	// we gotta have some proxy ips that we can use
	//if ( ! g_conf.m_proxyIps.hasDigits() ) useProxies = false;


	// scan down the linked list of queued of msg13 requests
	for ( ; r ; prev = r , r = nextLink ) { 

		// downloadTheDocForReals() could free "r" so save this here
		nextLink = r->m_nextLink;

		int64_t last;
		last = s_hammerCache.getLongLong(0,r->m_firstIp,30,true);
		// is one from this ip outstanding?
		if ( last == 0LL && r->m_crawlDelayFromEnd ) continue;


		int32_t crawlDelayMS = r->m_crawlDelayMS;

		// . if we got a proxybackoff base it on # of banned proxies 
		// . try to be more sensitive for more sensitive website policy
		// . we don't know why this proxy was banned, or if we were 
		//   responsible, or who banned it, but be more sensitive
		if ( //useProxies && 
		     r->m_numBannedProxies &&
		     r->m_hammerCallback == downloadTheDocForReals3b )
			crawlDelayMS = r->m_numBannedProxies * DELAYPERBAN;

		// download finished? 
		if ( last > 0 ) {
		        waited = nowms - last;
			// but skip if haven't waited int32_t enough
			if ( waited < crawlDelayMS ) continue;
		}
		// debug
		//log("spider: downloading %s from crawldelay queue "
		//    "waited=%" PRId64"ms crawldelay=%" PRId32"ms", 
		//    r->ptr_url,waited,r->m_crawlDelayMS);

		// good to go
		//downloadTheDocForReals ( r );

		// sanity check
		if ( ! r->m_hammerCallback ) { g_process.shutdownAbort(true); }

		// callback can now be either downloadTheDocForReals(r)
		// or downloadTheDocForReals3b(r) if it is waiting after 
		// getting a ProxyReply that had a m_proxyBackoff set

		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: calling hammer callback for "
			    "%s (timestamp=%" PRId64",waited=%" PRId64",crawlDelayMS=%" PRId32")",
			    r->ptr_url,
			    last,
			    waited,
			    crawlDelayMS);

		//
		// it should also add the current time to the hammer cache
		// for r->m_firstIp
		r->m_hammerCallback ( r );

		//
		// remove from future scans
		//
		if ( prev ) 
			prev->m_nextLink = nextLink;

		if ( s_hammerQueueHead == r )
			s_hammerQueueHead = nextLink;

		if ( s_hammerQueueTail == r )
			s_hammerQueueTail = prev;

		// if "r" was freed by downloadTheDocForReals() then
		// in the next iteration of this loop, "prev" will point
		// to a freed memory area, so start from the top again
		goto top;

		// try to download some more i guess...
	}
}

bool addNewProxyAuthorization ( SafeBuf *req , Msg13Request *r ) {

	if ( ! r->m_proxyIp   ) return true;
	if ( ! r->m_proxyPort ) return true;

	// get proxy from list to get username/password
	SpiderProxy *sp = getSpiderProxyByIpPort (r->m_proxyIp,r->m_proxyPort);

	// if none required, all done
	if ( ! sp->m_usernamePwd[0] ) return true;
	// strange?
	if ( req->length() < 8 ) return false;
	// back up over final \r\n
	req->m_length -= 2 ;
	// insert it
	req->safePrintf("Proxy-Authorization: Basic ");
	req->base64Encode ( sp->m_usernamePwd );
	req->safePrintf("\r\n");
	req->safePrintf("\r\n");
	req->nullTerm();
	return true;
}

// When the Msg13Request::m_isSquidProxiedUrl bit then request we got is
// using us like a proxy, so Msg13Request::m_url is in reality a complete
// HTTP request mime. so in that case we have to call this code to
// fix the HTTP request before sending it to its final destination.
//
// Remove "Proxy-authorization: Basic abcdefghij\r\n"
void stripProxyAuthorization ( char *squidProxiedReqBuf ) {
	//
	// remove the proxy authorization that has the username/pwd
	// so the websites we download the url from do not see it in the
	// http request mime
	//
 loop:
	// include space so it won't match anything in url
	char *s = gb_strcasestr ( squidProxiedReqBuf , "Proxy-Authorization: " );
	if ( ! s ) return;
	// find next \r\n
	const char *end = strstr ( s , "\r\n");
	if ( ! end ) return;
	// bury the \r\n as well
	end += 2;
	// bury that string
	int32_t reqLen = strlen(squidProxiedReqBuf);
	const char *reqEnd = squidProxiedReqBuf + reqLen;
	// include \0, so add +1
	memmove ( s ,end , reqEnd-end + 1);
	// bury more of them
	goto loop;
}


// . convert "GET http://xyz.com/abc" to "GET /abc"
// . TODO: add "Host:xyz.con\r\n" ?
void fixGETorPOST ( char *squidProxiedReqBuf ) {
	char *s = strstr ( squidProxiedReqBuf , "GET http" );
	int32_t slen = 8;
	if ( ! s ) {
		s = strstr ( squidProxiedReqBuf , "POST http");
		slen = 9;
	}
	if ( ! s ) {
		s = strstr ( squidProxiedReqBuf , "HEAD http");
		slen = 9;
	}
	if ( ! s ) return;
	// point to start of http://...
	char *httpStart = s + slen - 4;
	// https?
	s += slen;
	if ( *s == 's' ) s++;
	// skip ://
	if ( *s++ != ':' ) return;
	if ( *s++ != '/' ) return;
	if ( *s++ != '/' ) return;
	// skip until / or space or \r or \n or \0
	for ( ; *s && ! is_wspace_a(*s) && *s != '/' ; s++ );
	// bury the http://xyz.com part now
	char *reqEnd = squidProxiedReqBuf + strlen(squidProxiedReqBuf);
	// include the terminating \0, so add +1
	gbmemcpy ( httpStart , s , reqEnd - s + 1 );
	// now make HTTP/1.1 into HTTP/1.0
	char *hs = strstr ( httpStart , "HTTP/1.1" );
	if ( ! hs ) return;
	hs[7] = '0';
}

// . sets Msg13Request m_proxiedUrl and m_proxiedUrlLen
bool setProxiedUrlFromSquidProxiedRequest ( Msg13Request *r ) {

	// this is actually the entire http request mime, not a url
	//char *squidProxiedReqBuf = r->ptr_url;

	// shortcut. this is the actual squid request like
	// "CONNECT www.youtube.com:443 HTTP/1.1\r\nProxy-COnnection: ... "
	// or
	// "GET http://www.youtube.com/..."
	char *s = r->ptr_url;
	char *pu = NULL;

	if ( strncmp ( s , "GET http" ,8 ) == 0 ) 
		pu = s + 4;
	else if ( strncmp ( s , "POST http" ,9 ) == 0 )
		pu = s + 5;
	else if ( strncmp ( s , "HEAD http" ,9 ) == 0 )
		pu = s + 5;
	// this doesn't always have http:// usually just a hostname
	else if ( strncmp ( s , "CONNECT " ,8 ) == 0 )
		pu = s + 8;

	if ( ! pu ) return false;

	r->m_proxiedUrl = pu;

	// find end of it
	char *p = r->m_proxiedUrl;
	for ( ; *p && !is_wspace_a(*p) ; p++ );

	r->m_proxiedUrlLen = p - r->m_proxiedUrl;

	return true;
}

// . for the page cache we hash the url and the cookie to make the cache key
// . also the GET/POST method i guess
// . returns 0 on issues
int64_t computeProxiedCacheKey64 ( Msg13Request *r ) {

	// hash the url
	char *start = r->m_proxiedUrl;

	// how can this happen?
	if ( ! start ) {
		log("proxy: no proxied url");
		return 0LL;
	}

	// skip http:// or https://
	// skip forward
	char *s = start;
	if ( strncmp(s,"http://",7) == 0 ) s += 7;
	if ( strncmp(s,"https://",8) == 0 ) s += 8;
	// skip till we hit end of url
	// skip until / or space or \r or \n or \0
	char *cgi = NULL;
	for ( ; *s && ! is_wspace_a(*s)  ; s++ ) {
		if ( *s == '?' && ! cgi ) cgi = s; }
	// hash the url
	int64_t h64 = hash64 ( start , s - start );


	//
	// if file extension implies it is an image, do not hash cookie
	//
	char *extEnd = NULL;
	if ( cgi ) extEnd = cgi;
	else       extEnd = s;
	char *ext = extEnd;
	for ( ; ext>extEnd-6 && ext>start && *ext!='.' && *ext!='/' ; ext-- );
	if ( *ext == '.' && ext+1 < extEnd ) {
		HttpMime mime;
		const char *cts;
		ext++; // skip over .
		cts = mime.getContentTypeFromExtension ( ext , extEnd-ext );
		if ( strncmp(cts,"image/",6) == 0 ) return h64;
	}

	// this is actually the entire http request mime, not a url
	char *squidProxiedReqBuf = r->ptr_url;
	// now for cookie
	s = strstr ( squidProxiedReqBuf , "Cookie: ");
	// if not there, just return url hash
	if ( ! s ) return h64;
	// save start
	start = s + 8;
	// skip till we hit end of cookie line
	for ( ; *s && *s != '\r' && *s != '\n' ; s++ );
	// incorporate cookie hash
	h64 = hash64 ( start , s - start , h64 );

	//log("debug: cookiehash=%" PRId64,hash64(start,s-start));

	return h64;
}

#include "Pages.h"

bool printHammerQueueTable ( SafeBuf *sb ) {

	const char *title = "Queued Download Requests";
	sb->safePrintf ( 
			 "<table %s>"
			 "<tr class=hdrow><td colspan=19>"
			 "<center>"
			 "<b>%s</b>"
			 "</td></tr>"

			 "<tr bgcolor=#%s>"
			 "<td><b>#</td>"
			 "<td><b>age</td>"
			 "<td><b>first ip found</td>"
			 "<td><b>actual ip</td>"
			 "<td><b>crawlDelayMS</td>"
			 "<td><b># proxies banning</td>"
			 
			 "<td><b>coll</td>"
			 "<td><b>url</td>"

			 "</tr>\n"
			 , TABLE_STYLE
			 , title 
			 , DARK_BLUE
			 );

	int32_t count = 0;
	int64_t nowms = gettimeofdayInMilliseconds();


	for(Msg13Request *r = s_hammerQueueHead; r; r = r->m_nextLink) {
		// print row
		sb->safePrintf( "<tr bgcolor=#%s>"
			       "<td>%i</td>" // #
			       "<td>%ims</td>" // age in hammer queue
			       "<td>%s</td>"
				,LIGHT_BLUE
			       ,(int)count
			       ,(int)(nowms - r->m_stored)
			       ,iptoa(r->m_firstIp)
			       );

		sb->safePrintf("<td>%s</td>" // actual ip
			       , iptoa(r->m_urlIp));

		// print crawl delay as link to robots.txt
		sb->safePrintf( "<td><a href=\"");
		Url cu;
		cu.set ( r->ptr_url );
		bool isHttps = cu.isHttps();
		if ( isHttps ) {
			sb->safeStrcpy( "https://" );
		} else {
			sb->safeStrcpy( "http://" );
		}

		sb->safeMemcpy ( cu.getHost() , cu.getHostLen() );
		int32_t port = cu.getPort();
		int32_t defPort = isHttps ? 443 : 80;

		if ( port != defPort ) {
			sb->safePrintf( ":%" PRId32, port );
		}

		sb->safePrintf ( "/robots.txt\">"
				 "%i"
				 "</a>"
				 "</td>" // crawl delay MS
				 "<td>%i</td>" // proxies banning
				 , r->m_crawlDelayMS
				 , r->m_numBannedProxies
				 );

		// show collection name as a link, also truncate to 32 chars
		CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );
		const char *coll = "none";
		if ( cr ) coll = cr->m_coll;
		sb->safePrintf("<td>");
		if ( cr ) {
			sb->safePrintf("<a href=/admin/sockets?c=");
			urlEncode(sb,coll);
			sb->safePrintf(">");
		}
		sb->safeTruncateEllipsis ( coll , 32 );
		if ( cr ) sb->safePrintf("</a>");
		sb->safePrintf("</td>");
		// then the url itself
		sb->safePrintf("<td><a href=%s>",r->ptr_url);
		sb->safeTruncateEllipsis ( r->ptr_url , 128 );
		sb->safePrintf("</a></td>");
		sb->safePrintf("</tr>\n");
	}
	return true;
}
