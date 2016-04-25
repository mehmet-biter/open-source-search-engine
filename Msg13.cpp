#include "gb-include.h"

#include "Msg13.h"
#include "UdpServer.h"
#include "HttpServer.h"
#include "Stats.h"
#include "HashTableX.h"
#include "XmlDoc.h"
#include "SpiderProxy.h" // OP_GETPROXY OP_RETPROXY
#include "zlib.h"

char *g_fakeReply = 
	"HTTP/1.0 200 (OK)\r\n"
	"Content-Length: 0\r\n"
	"Connection: Close\r\n"
	"Content-Type: text/html\r\n\r\n\0";

bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts );
void gotIframeExpandedContent ( void *state ) ;

bool addToHammerQueue ( Msg13Request *r ) ;
void scanHammerQueue ( int fd , void *state );
void downloadTheDocForReals ( Msg13Request *r ) ;

// utility functions
bool getTestSpideredDate ( Url *u , int32_t *origSpiderDate , char *testDir ) ;
bool addTestSpideredDate ( Url *u , int32_t  spideredTime   , char *testDir ) ;
bool getTestDoc ( char *u , class TcpSocket *ts , Msg13Request *r );
bool addTestDoc ( int64_t urlHash64 , char *httpReply , int32_t httpReplySize ,
		  int32_t err , Msg13Request *r ) ;

static void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) ;
static void handleRequest13 ( UdpSlot *slot , int32_t niceness ) ;
//static bool downloadDoc     ( UdpSlot *slot, Msg13Request* r ) ;
static void gotHttpReply    ( void *state , TcpSocket *ts ) ;
static void gotHttpReply2 ( void *state , 
			    char *reply , 
			    int32_t  replySize ,
			    int32_t  replyAllocSize ,
			    TcpSocket *ts ) ;
static void passOnReply     ( void *state , UdpSlot *slot ) ;

bool hasIframe           ( char *reply, int32_t replySize , int32_t niceness );

char getContentTypeQuick ( HttpMime *mime, char *reply, int32_t replySize , 
			   int32_t niceness ) ;
int32_t convertIntoLinks    ( char *reply, int32_t replySize , Xml *xml , 
			   int32_t niceness ) ;


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
	if ( ! g_udpServer.registerHandler ( 0x13, handleRequest13 )) 
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
	if ( ! s_rt.set ( 8 ,sizeof(UdpSlot *),0,NULL,0,true,0,"wait13tbl") )
		return false;

	if ( ! g_loop.registerSleepCallback(10,NULL,scanHammerQueue) )
		return log("build: Failed to register timer callback for "
			   "hammer queue.");


	// success
	return true;
}

// . returns false if blocked, returns true otherwise
// . returns true and sets g_errno on error
bool Msg13::getDoc ( Msg13Request *r,
		     bool isTestColl , 
		     void *state,void(*callback)(void *state)){

	// reset in case we are being reused
	reset();

	// set these even though we are not doing events, so we can use
	// the event spider proxies on scproxy3
	r->m_requireGoodDate = 0;
	r->m_harvestLinksIfNoGoodDate = 1;

	m_state    = state;
	m_callback = callback;

	m_request = r;
	// sanity check
	if ( r->m_urlIp ==  0 ) { char *xx = NULL; *xx = 0; }
	if ( r->m_urlIp == -1 ) { char *xx = NULL; *xx = 0; }

	// set this
	//r->m_urlLen    = gbstrlen ( r->ptr_url );
	r->m_urlHash64 = hash64 ( r->ptr_url , r->size_url-1);//m_urlLen );

	// no! i don't want to store images in there
	//if ( isTestColl )
	//	r->m_useTestCache = true;

	// default to 'qa' test coll if non given
	if ( r->m_useTestCache &&
	     ! r->m_testDir[0] ) {
		r->m_testDir[0] = 'q';
		r->m_testDir[1] = 'a';
	}

	// sanity check, if spidering the test coll make sure one of 
	// these is true!! this prevents us from mistakenly turning it off
	// and not using the doc cache on disk like we should
	if ( isTestColl &&
	     ! r->m_testDir[0] &&
	     //! g_conf.m_testSpiderEnabled &&
	     //! g_conf.m_testParserEnabled &&
	     //! r->m_isPageParser &&
	     r->m_useTestCache ) {
		char *xx=NULL;*xx=0; }

	//r->m_testSpiderEnabled = (bool)g_conf.m_testSpiderEnabled;
	//r->m_testParserEnabled = (bool)g_conf.m_testParserEnabled;
	// but default to parser dir if we are the test coll so that
	// the [analyze] link works!
	//if ( isTestColl && ! r->m_testSpiderEnabled )
	//	r->m_testParserEnabled = true;

	// is this a /robots.txt url?
	if ( r->size_url - 1 > 12 && 
	     ! strncmp ( r->ptr_url + r->size_url -1 -11,"/robots.txt",11))
		r->m_isRobotsTxt = true;

	// force caching if getting robots.txt so is compressed in cache
	if ( r->m_isRobotsTxt )
		r->m_compressReply = true;

	// do not get .google.com/ crap
	//if ( strstr(r->ptr_url,".google.com/") ) { char *xx=NULL;*xx=0; }

	// set it for this too
	//if ( g_conf.m_useCompressionProxy ) {
	//	r->m_useCompressionProxy = true;
	//	r->m_compressReply       = true;
	//}

	// make the cache key
	r->m_cacheKey  = r->m_urlHash64;
	// a compressed reply is different than a non-compressed reply
	if ( r->m_compressReply ) r->m_cacheKey ^= 0xff;



	if ( r->m_isSquidProxiedUrl )
		// sets r->m_proxiedUrl that we use a few times below
		setProxiedUrlFromSquidProxiedRequest ( r );

	// . if gigablast is acting like a squid proxy, then r->ptr_url
	//   is a COMPLETE http mime request, so hash the following fields in 
	//   the http mime request to make the cache key
	//   * url
	//   * cookie
	// . this is r->m_proxiedUrl which we set above
	if ( r->m_isSquidProxiedUrl )
		r->m_cacheKey = computeProxiedCacheKey64 ( r );


	// always forward these so we can use the robots.txt cache
	if ( r->m_isRobotsTxt ) r->m_forwardDownloadRequest = true;

	// always forward for now until things work better!
	r->m_forwardDownloadRequest = true;	

	// assume no http proxy ip/port
	r->m_proxyIp = 0;
	r->m_proxyPort = 0;

	// download it ourselves rather than forward it off to another host?
	//if ( r->m_forwardDownloadRequest ) return forwardRequest ( ); 

	return forwardRequest ( ); 

	// gotHttpReply() and passOnReply() call our Msg13::gotDocReply*() 
	// functions if Msg13Request::m_parent is non-NULL
	//r->m_parent = this;

	// . returns false if blocked, etc.
	// . if this doesn't block it calls getFinalReply()
	//return downloadDoc ( NULL , r ) ;
}

bool Msg13::forwardRequest ( ) {

	// shortcut
	Msg13Request *r = m_request;

	//
	// forward this request to the host responsible for this url's ip
	//
	int32_t nh     = g_hostdb.m_numHosts;
	int32_t hostId = hash32h(((uint32_t)r->m_firstIp >> 8), 0) % nh;

	if((uint32_t)r->m_firstIp >> 8 == 0) {
		// If the first IP is not set for the request then we don't
		// want to hammer the first host with spidering enabled.
		hostId = hash32n ( r->ptr_url ) % nh;
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
		       "uh48=%"UINT64" to "
		       "host %"INT32" (child=%"INT32")", r->ptr_url, iptoa(r->m_firstIp), 
		       r->m_urlHash48, hostId,
		       r->m_skipHammerCheck);

	// sanity
	if ( r->m_useTestCache && ! r->m_testDir[0] ) { char *xx=NULL;*xx=0;}

	// fill up the request
	int32_t requestBufSize = r->getSize();

	// we have to serialize it now because it has cookies as well as
	// the url.
	char *requestBuf = serializeMsg ( sizeof(Msg13Request),
					  &r->size_url,
					  &r->size_cookie,
					  &r->ptr_url,
					  r,
					  &requestBufSize ,
					  NULL , 
					  0,//RBUF_SIZE , 
					  false );
	// g_errno should be set in this case, most likely to ENOMEM
	if ( ! requestBuf ) return true;

	// . otherwise, send the request to the key host
	// . returns false and sets g_errno on error
	// . now wait for 2 minutes before timing out
	if ( ! g_udpServer.sendRequest ( requestBuf, // (char *)r    ,
					 requestBufSize  , 
					 0x13         , // msgType 0x13
					 h->m_ip      ,
					 h->m_port    ,
					 // it was not using the proxy! because
					 // it thinks the hostid #0 is not
					 // the proxy... b/c ninad screwed that
					 // up by giving proxies the same ids
					 // as regular hosts!
					 -1 , // h->m_hostId  ,
					 NULL         ,
					 this         , // state data
					 gotForwardedReplyWrapper  ,
					 200000 )){// 200 sec timeout
		// sanity check
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// report it
		log("spider: msg13 request: %s",mstrerror(g_errno));
		// g_errno must be set!
		return true;
	}
	// otherwise we block
	return false;
}

void gotForwardedReplyWrapper ( void *state , UdpSlot *slot ) {
	// shortcut
	Msg13 *THIS = (Msg13 *)state;
	// return if this blocked
	if ( ! THIS->gotForwardedReply ( slot ) ) return;
	// callback
	THIS->m_callback ( THIS->m_state );
}

bool Msg13::gotForwardedReply ( UdpSlot *slot ) {
	// don't let udpserver free the request, it's our m_request[]
	// no, now let him free it because it was serialized into there
	//slot->m_sendBufAlloc = NULL;
	// what did he give us?
	char *reply          = slot->m_readBuf;
	int32_t  replySize      = slot->m_readBufSize;
	int32_t  replyAllocSize = slot->m_readBufMaxSize;
	// UdpServer::makeReadBuf() sets m_readBuf to -1 when calling
	// alloc() with a zero length, so fix that
	if ( replySize == 0 ) reply = NULL;
	// this is messed up. why is it happening?
	if ( reply == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// we are responsible for freeing reply now
	if ( ! g_errno ) slot->m_readBuf = NULL;

	return gotFinalReply ( reply , replySize , replyAllocSize );
}

#include "PageInject.h"

bool Msg13::gotFinalReply ( char *reply, int32_t replySize, int32_t replyAllocSize ){

	// how is this happening? ah from image downloads...
	if ( m_replyBuf ) { char *xx=NULL;*xx=0; }
		
	// assume none
	m_replyBuf     = NULL;
	m_replyBufSize = 0;

	// shortcut
	Msg13Request *r = m_request;

	//log("msg13: reply=%"XINT32" replysize=%"INT32" g_errno=%s",
	//    (int32_t)reply,(int32_t)replySize,mstrerror(g_errno));

	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads )
		logf(LOG_DEBUG,"spider: FINALIZED %s firstIp=%s",
		     r->ptr_url,iptoa(r->m_firstIp));


	// . if timed out probably the host is now dead so try another one!
	// . return if that blocked
	if ( g_errno == EUDPTIMEDOUT ) {
		// try again
		log("spider: retrying1. had error for %s : %s",
		    r->ptr_url,mstrerror(g_errno));
		// return if that blocked
		if ( ! forwardRequest ( ) ) return false;
		// a different g_errno should be set now!
	}

	if ( g_errno ) {
		// this error msg is repeated in XmlDoc::logIt() so no need
		// for it here
		if ( g_conf.m_logDebugSpider )
			log("spider: error for %s: %s",
			    r->ptr_url,mstrerror(g_errno));
		return true;
	}

	// set it
	m_replyBuf          = reply;
	m_replyBufSize      = replySize;
	m_replyBufAllocSize = replyAllocSize;

	// sanity check
	if ( replySize > 0 && ! reply ) { char *xx=NULL;*xx=0; }

	// no uncompressing if reply is empty
	if ( replySize == 0 ) return true;

	// if it was not compressed we are done! no need to uncompress it
	if ( ! r->m_compressReply ) return true;

	// get uncompressed size
	uint32_t unzippedLen = *(int32_t*)reply;
	// sanity checks
	if ( unzippedLen > 10000000 ) {
		log("spider: downloaded probable corrupt gzipped doc "
		    "with unzipped len of %"INT32"",(int32_t)unzippedLen);
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
		    "len should be %"INT32" but is %"INT32". ziperr=%"INT32"",
		    (int32_t)uncompressedLen,
		    (int32_t)unzippedLen,
		    (int32_t)zipErr);
		mfree (newBuf, unzippedLen, "Msg13UnzipError");
		g_errno = ECORRUPTDATA;//EBADREPLYSIZE;
		return true;
	}
	// all http replies should end in a \0. otherwise its likely
	// a compression error. i think i saw this on roadrunner core
	// a machine once in XmlDoc.cpp because httpReply did not end in \0
	//if ( uncompressedLen>0 && newBuf[uncompressedLen-1] ) {
	//	log("spider: had http reply with no NULL term");
	//	mfree(newBuf,unzippedLen,"Msg13Null");
	//	g_errno = EBADREPLYSIZE;
	//	return true;
	//}

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
		log("http: got doc %s %"INT32" to %"INT32"",
		    r->ptr_url,(int32_t)replySize,(int32_t)uncompressedLen);

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
		ht->set ( 4,0,16,NULL,0,false,MAX_NICENESS,"twitchtbl",true);
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
	//if ( niceness == 0 ) { char *xx=NULL;*xx=0; }

	// make sure we do not download gigablast.com admin pages!
	if ( g_hostdb.isIpInNetwork ( r->m_firstIp ) && r->size_url-1 >= 7 ) {
		Url url;
		url.set ( r->ptr_url );
		// . never download /master urls from ips of hosts in cluster
		// . TODO: FIX! the pages might be in another cluster!
		// . pages are now /admin/* not any /master/* any more.
		if ( ( //strncasecmp ( url.getPath() , "/master/" , 8 ) == 0 ||
		       strncasecmp ( url.getPath() , "/admin/"  , 7 ) == 0 )) {
			log("spider: Got request to download possible "
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
	key_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
	// see if in there already
	bool inCache = c->getRecord ( (collnum_t)0     , // share btwn colls
				      k                , // cacheKey
				      &rec             ,
				      &recSize         ,
				      true             , // copy?
				      r->m_maxCacheAge , // 24*60*60 ,
				      true             ); // stats?

	r->m_foundInCache = false;

	// . an empty rec is a cached not found (no robot.txt file)
	// . therefore it's allowed, so set *reply to 1 (true)
	if ( inCache ) {
		// log debug?
		//if ( r->m_isSquidProxiedUrl )
		if ( g_conf.m_logDebugSpider )
			log("proxy: found %"INT32" bytes in cache for %s",
			    recSize,r->ptr_url);

		r->m_foundInCache = true;

		// helpful for debugging. even though you may see a robots.txt
		// redirect and think we are downloading that each time,
		// we are not... the redirect is cached here as well.
		//log("spider: %s was in cache",r->ptr_url);
		// . send the cached reply back
		// . this will free send/read bufs on completion/g_errno
		g_udpServer.sendReply_ass ( rec , recSize , rec, recSize,slot);
		return;
	}

	// log it so we can see if we are hammering
	if ( g_conf.m_logDebugRobots || g_conf.m_logDebugDownloads ||
	     g_conf.m_logDebugMsg13 )
		logf(LOG_DEBUG,"spider: DOWNLOADING %s firstIp=%s",
		     r->ptr_url,iptoa(r->m_firstIp));

	// temporary hack
	if ( r->m_parent ) { char *xx=NULL;*xx=0; }

	// assume we do not add it!
	//r->m_addToTestCache = false;

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

	// sanity
	if ( r->m_useTestCache && ! r->m_testDir[0] ) { char *xx=NULL;*xx=0;}

	// try to get it from the test cache?
	TcpSocket ts;
	if ( r->m_useTestCache && getTestDoc ( r->ptr_url, &ts , r ) ) {
		// save this
		r->m_udpSlot = slot;
		// store the request so gotHttpReply can reply to it
		if ( ! s_rt.addKey ( &r->m_cacheKey , &r ) ) {
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// sanity check
		if ( ts.m_readOffset  < 0 ) { char *xx=NULL;*xx=0; }
		if ( ts.m_readBufSize < 0 ) { char *xx=NULL;*xx=0; }
		// reply to it right away
		gotHttpReply ( r , &ts );
		// done
		return;
	}

	// if wanted it to be in test cache but it was not, we have to 
	// download it, so use a fresh ip! we ran into a problem when
	// downloading a new doc from an old ip in ips.txt!!
	if ( r->m_useTestCache )
		r->m_urlIp = 0;

	// save this
	r->m_udpSlot = slot;
	// sanity check
	if ( ! slot ) { char *xx=NULL;*xx=0; }

	// send to a proxy if we are doing compression and not a proxy
	if ( r->m_useCompressionProxy && ! g_hostdb.m_myHost->m_isProxy ) {
		// use this key to select which proxy host
		int32_t key = ((uint32_t)r->m_firstIp >> 8);
		// send to host "h"
		Host *h = g_hostdb.getBestSpiderCompressionProxy(&key);
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log(LOG_DEBUG,"spider: sending to compression proxy "
			    "%s:%"UINT32"",iptoa(h->m_ip),(uint32_t)h->m_port);
		// . otherwise, send the request to the key host
		// . returns false and sets g_errno on error
		// . now wait for 2 minutes before timing out
		if ( ! g_udpServer.sendRequest ( (char *)r    ,
						 r->getSize() ,
						 0x13         , // msgType 0x13
						 h->m_ip      ,
						 h->m_port    ,
						 // we are sending to the proxy
						 // so make this -1
						 -1 , // h->m_hostId  ,
						 NULL         ,
						 r            , // state data
						 passOnReply  ,
						 200000 , // 200 sec timeout
						 -1,//backoff
						 -1,//maxwait
						 NULL,//replybuf
						 0,//replybufmaxsize
						 niceness)) {
			// g_errno should be set
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// wait for it
		return;
	}

	// we skip it if its a frame page, robots.txt, root doc or some other
	// page that is a "child" page of the main page we are spidering.
	// MDW: i moved this AFTER sending to the compression proxy...
	// if ( ! r->m_skipHammerCheck ) {
	// 	// if addToHammerQueue() returns true and queues this url for
	// 	// download later, when ready to download call this function
	// 	r->m_hammerCallback = downloadTheDocForReals;
	// 	// this returns true if added to the queue for later
	// 	if ( addToHammerQueue ( r ) ) return;
	// }

	// do not get .google.com/ crap
	//if ( strstr(r->ptr_url,".google.com/") ) { char *xx=NULL;*xx=0; }

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
		log("spider: error adding to waiting table %s",r->ptr_url);
		
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

	// user can turn off proxy use with this switch
	//if ( ! g_conf.m_useProxyIps ) useProxies = false;

	// for diffbot turn ON if use robots is off
	if ( r->m_forceUseFloaters ) useProxies = true;

	CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// if you turned on automatically use proxies in spider controls...
	if ( ! useProxies && 
	     cr &&
	     r->m_urlIp != 0 &&
	     r->m_urlIp != -1 &&
	     // either the global or local setting will work
	     //( g_conf.m_automaticallyUseProxyIps || 
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

	// request is just the urlip
	//ProxyRequest *pr ;
	//pr = (ProxyRequest *)mmalloc ( sizeof(ProxyRequest),"proxreq");
	//if ( ! pr ) {
	//	log("sproxy: error: %s",mstrerror(g_errno));
	//	g_udpServer.sendErrorReply(r->m_udpSlot,g_errno);
	//}

	// try to get a proxy ip/port to download our url with
	//pr->m_urlIp      = r->m_urlIp;
	//pr->m_retryCount = r->m_retryCount;
	//pr->m_opCode     = OP_GETPROXY;

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
	if ( ! g_udpServer.sendRequest ( (char *)r,
					 // just the top part of the
					 // Msg13Request is sent to
					 // handleRequest54() now
					 r->getProxyRequestSize() ,
					 0x54         , // msgType 0x54
					 h->m_ip      ,
					 h->m_port    ,
					 -1 , // h->m_hostId  ,
					 NULL         ,
					 r         , // state data
					 gotProxyHostReplyWrapper  ,
					 udpserver_sendrequest_infinite_timeout )){
		// sanity check
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// report it
		log("spider: msg54 request1: %s %s",
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
	// shortcut
	Msg13Request *r = (Msg13Request *)state;
	//Msg13 *THIS = r->m_parent;
	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;
	// error getting spider proxy to use?
	if ( g_errno ) {
		// note it
		log("sproxy: got proxy request error: %s",mstrerror(g_errno));
		
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
		log("sproxy: bad 54 reply size of %"INT32" != %"INT32" %s",
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
		int32_t len = gbstrlen(prep->m_usernamePwd);
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

	// if addToHammerQueue() returns true and queues this url for
	// download later, when ready to download call this function
	//r->m_hammerCallback = downloadTheDocForReals3;

	// . returns true if we queued it for trying later
	// . scanHammerQueue() will call downloadTheDocForReals3(r) for us
	//if ( addToHammerQueue ( r ) ) return;

	// now forward the request
	// now the reply should have the proxy host to use
	// return if this blocked
	//if ( ! THIS->forwardRequest() ) return;
	// it did not block...
	//THIS->m_callback ( THIS->m_state );
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
		    "spider: adding new time to hammercache for %s %s = %"INT64"",
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
		    "of %"INT32" for "
		    "firstIp=%s "
		    "url=%s "
		    "to msg13::hammerCache",
		    0,//-1,
		    iptoa(r->m_firstIp),
		    r->ptr_url);



	// flag this
	//if ( g_conf.m_qaBuildMode ) r->m_addToTestCache = true;
	// note it here
	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("spider: downloading %s (%s) (skiphammercheck=%"INT32")",
		    r->ptr_url,iptoa(r->m_urlIp) ,
		    (int32_t)r->m_skipHammerCheck);

	char *agent = g_conf.m_spiderUserAgent;

	// . for bulk jobs avoid actual downloads of the page for efficiency
	// . g_fakeReply is just a simple mostly empty 200 http reply
	if ( r->m_isCustomCrawl == 2 ) {
		int32_t slen = gbstrlen(g_fakeReply);
		int32_t fakeBufSize = slen + 1;
		// try to fix memleak
		char *fakeBuf = g_fakeReply;//mdup ( s, fakeBufSize , "fkblk");
		//r->m_freeMe = fakeBuf;
		//r->m_freeMeSize = fakeBufSize;
		gotHttpReply2 ( r , 
				fakeBuf,
				fakeBufSize, // include \0
				fakeBufSize, // allocsize
				NULL ); // tcpsock
		return;
	}


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
		    "sproxy: got proxy %s:%"UINT32" "
		    "and agent=\"%s\" to spider "
		    "%s %s (numBannedProxies=%"INT32")",
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
				     r->m_proxyUsernamePwdAuth ) )
		// return false if blocked
		return;
	// . log this so i know about it
	// . g_errno MUST be set so that we do not DECREMENT
	//   the outstanding dom/ip counts in gotDoc() below
	//   because we did not increment them above
	logf(LOG_DEBUG,"spider: http server had error: %s",mstrerror(g_errno));
	// g_errno should be set
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// if called from ourselves. return true with g_errno set.
	//if ( r->m_parent ) return true;
	// if did not block -- should have been an error. call callback
	gotHttpReply ( r , NULL );
	return ;
}

static int32_t s_55Out = 0;

void doneReportingStatsWrapper ( void *state, UdpSlot *slot ) {
	//Msg13Request *r = (Msg13Request *)state;
	// note it
	if ( g_errno )
		log("sproxy: 55 reply: %s",mstrerror(g_errno));
	// do not free request, it was part of original Msg13Request
	//slot->m_sendBuf = NULL;
	// clear g_errno i guess
	g_errno = 0;
	// don't let udpserver free the request, it's our m_urlIp
	slot->m_sendBufAlloc = NULL;
	// resume on our way down the normal pipeline
	//gotHttpReply ( state , r->m_tcpSocket );
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

	// if it has link to "google.com/recaptcha"
	// TODO: use own gbstrstr so we can do QUICKPOLL(niceness)
	// TODO: ensure NOT in an invisible div
	if ( strstr ( ts->m_readBuf , "google.com/recaptcha/api/challenge") ) {
		*msg = "recaptcha link";
		return true;
	}

	//CollectionRec *cr = g_collectiondb.getRec ( r->m_collnum );

	// if it is a seed url and there are no links, then perhaps we
	// are in a blacklist somewhere already from triggering a spider trap
	// i've seen this flub on a site where they just return a script
	// and it is not banned, so let's remove this until we thinkg
	// of something better.
	// if ( //isInSeedBuf ( cr , r->ptr_url ) &&
	//      // this is set in XmlDoc.cpp based on hopcount really
	//      r->m_isRootSeedUrl &&
	//      ! strstr ( ts->m_readBuf, "<a href" ) ) {
	// 	*msg = "root/seed url with no outlinks";
	// 	return true;
	// }


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
		char *msg = "No more proxies to try. Using reply as is.";
		if ( r->m_hasMoreProxiesToTry )  msg = "Trying another proxy.";
		char tmpIp[64];
		sprintf(tmpIp,"%s",iptoa(r->m_urlIp));
		log("msg13: detected that proxy %s is banned "
		    "(banmsg=%s) "
		    "(tries=%"INT32") by "
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

	//r->m_tcpSocket = ts;

	//ProxyRequest *preq; // = &r->m_proxyRequest;
	//preq = (ProxyRequest *)mmalloc ( sizeof(ProxyRequest),"stupid");
	//preq->m_urlIp = r->m_urlIp;
	//preq->m_retHttpProxyIp = r->m_proxyIp;
	//preq->m_retHttpProxyPort = r->m_proxyPort;
	//preq->m_lbId = r->m_lbId; // the LoadBucket ID
	//preq->m_opCode = OP_RETPROXY; // tell host #0 to reduce proxy load cn

	// tell host #0 to reduce proxy load cnt
	r->m_opCode = OP_RETPROXY; 

	//r->m_blocked = false;

	Host *h = g_hostdb.getFirstAliveHost();
	// now return the proxy. this will decrement the load count on
	// host "h" for this proxy.
	if ( g_udpServer.sendRequest ( (char *)r,
				       r->getProxyRequestSize(),
				       0x54 ,
				       h->m_ip      ,
				       h->m_port    ,
				       -1 , // h->m_hostId  ,
				       NULL         ,
				       r        , // state data
				       doneReportingStatsWrapper  ,
				       10000 )){// 10 sec timeout
		// it blocked!
		//r->m_blocked = true;
		s_55Out++;
		// sanity
		if ( s_55Out > 500 )
			log("sproxy: s55out > 500 = %"INT32"",s_55Out);
	}
	// sanity check
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// report it
	if ( g_errno ) log("spider: msg54 request2: %s %s",
			   mstrerror(g_errno),r->ptr_url);
	// it failed i guess proceed
	gotHttpReply( state , ts );
}
/*
static void doneInjectingProxyReply ( void *state ) {
	Msg7 *msg7 = (Msg7 *)state;
	if ( g_errno )
		log("msg13: got error injecting proxied req: %s",
		    mstrerror(g_errno));
	g_errno = 0;
	mdelete ( msg7 , sizeof(Msg7) , "dm7");
	delete ( msg7 );
}

static bool markupServerReply ( Msg13Request *r , TcpSocket *ts );
*/

void gotHttpReply ( void *state , TcpSocket *ts ) {

	/*
 	// cast it
	Msg13Request *r = (Msg13Request *)state;

	//////////
	//
	// before we mark it up, let's inject it!!
	// if a squid proxied request.
	// now inject this url into the main or GLOBAL-INDEX collection
	// so we can start accumulating sectiondb vote/markup stats
	// but do not wait for the injection to complete before sending
	// it back to the requester.
	//
	//////////
	if ( ! r->m_foundInCache && 
	     r->m_isSquidProxiedUrl ) {
		// make a new msg7 to inject it
		Msg7 *msg7;
		try { msg7 = new (Msg7); }
		catch ( ... ) { 
			g_errno = ENOMEM;
			log("squid: msg7 new(%i): %s",
			    (int)sizeof(Msg7),mstrerror(g_errno));
			return;
		}
		mnew ( msg7, sizeof(Msg7), "m7st" );

		int32_t httpReplyLen = ts->m_readOffset;

		// parse out the http mime
		HttpMime hm;
		hm.set ( ts->m_readBuf , httpReplyLen , NULL );
		if ( hm.getHttpStatus() != 200 ) 
			goto skipInject;

		// inject requires content be null terminated. sanity check
		if ( ts->m_readBuf && httpReplyLen > 0 &&
		     ts->m_readBuf[httpReplyLen] ) { char *xx=NULL;*xx=0;}

		// . this may or may not block, we give it a callback that
		//   just delete the msg7. we do not want this to hold up us 
		//   returning the proxied reply to the client browser.
		// . so frequently hit sites will accumulate useful voting 
		//   info  since we inject each one
		// . if we hit the page cache above we won't make it this far 
		//   though
		// . but i think that cache is only for 60 seconds
		if ( msg7->inject ( "main", // put in main collection
				    r->m_proxiedUrl,//url,
				    r->m_proxiedUrlLen,
				    ts->m_readBuf,
				    msg7 ,
				    doneInjectingProxyReply ) ) {
			log("msg7: inject error: %s",mstrerror(g_errno));
			mdelete ( msg7 , sizeof(Msg7) , "dm7");
			delete ( msg7 );
			// we can't return here we have to pass the request
			// on to the browser client...
			g_errno = 0;
			//return;
		}
	}

 skipInject:

	// now markup the reply with the sectiondb info
	// . it can block reading the disk
	// . returns false if blocked
	// . only do markup if its a proxied request
	// . our squid proxy simulator is only a markup simulator
	if ( ! r->m_foundInCache && r->m_isSquidProxiedUrl ) {
		// . now transform the html to include sectiondb data
		// . this will also send the reply back so no need to
		//   have a callback here
		// . it returns false if it did nothing because the
		//   content type was not html or http status was not 200
		// . if it returns false then just pass through it
		// . if it returns true, then it will be responsible for
		//   sending back the udp reply of the marked up page
		// . returns false if blocked, true otherwise
		// . returns true and sets g_errno on error
		// . when done it just calls gotHttpReply2() on its own
		if ( ! markupServerReply ( r , ts ) ) 
			return;
		// oom error? force ts to NULL so it will be sent below
		if ( g_errno ) {
			log("msg13: markupserverply: %s",mstrerror(g_errno));
			ts = NULL;
		}
	}
	*/

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
	if ( ! g_errno ) { char *xx=NULL;*xx=0; } // g_errno=EBADENG...
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

	// ' connection reset' debug stuff
	// log("spider: httpreplysize=%i",(int)replySize);
	// if ( replySize == 0 )
	// 	log("hey");

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
		    "of %"INT64" for firstIp=%s url=%s "
		    "to msg13::hammerCache",
		    timeToAdd,iptoa(r->m_firstIp),r->ptr_url);


	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log(LOG_DEBUG,"spider: got http reply for firstip=%s url=%s "
		    "err=%s",
		    iptoa(r->m_firstIp),r->ptr_url,mstrerror(savedErr));
	

	// sanity. this was happening from iframe download
	//if ( g_errno == EDNSTIMEDOUT ) { char *xx=NULL;*xx=0; }

	// . sanity check - robots.txt requests must always be compressed
	// . saves space in the cache
	if ( ! r->m_compressReply && r->m_isRobotsTxt ) {char *xx=NULL; *xx=0;}
	// null terminate it always! -- unless already null terminated...
	if ( replySize > 0 && reply[replySize-1] ) reply[replySize++] = '\0';
	// sanity check
	if ( replySize > replyAllocSize ) { char *xx=NULL;*xx=0; }

	// save original size
	int32_t originalSize = replySize;

	// . add the reply to our test cache
	// . if g_errno is set to something like "TCP Timed Out" then
	//   we end up saving a blank robots.txt or doc here...
	
	if ( r->m_useTestCache && r->m_addToTestCache )
		addTestDoc ( r->m_urlHash64,reply,replySize,
			     savedErr , r );

	// note it
	if ( r->m_useTestCache && 
	     ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 ) )
		logf(LOG_DEBUG,"spider: got reply for %s "
		     "firstIp=%s uh48=%"UINT64"",
		     r->ptr_url,iptoa(r->m_firstIp),r->m_urlHash48);

	int32_t niceness = r->m_niceness;

	// sanity check
	if ( replySize>0 && reply[replySize-1]!= '\0') { char *xx=NULL;*xx=0; }

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
				      "HTTP/1.0 %"INT32"\r\n"
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
			if ( newSize >= 2048 ) { char *xx=NULL;*xx=0; }
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

	/*
	if ( replySize > 0 && 
	     goodStatus && 
	     r->m_forEvents &&
	     ! r->m_isRobotsTxt &&
	     r->m_compressReply ) {
		// Links class required Xml class
		if ( ! xml.set ( content   ,
				 contentLen , // lennotsize! do not include \0
				 false     , // ownData?
				 false     , // purexml?
				 0         , // version! (unused)
				 false     , // set parents?
				 niceness  ) )
			log("scproxy: xml set had error: %s",
			    mstrerror(g_errno));
		// definitely compute the wordids so Dates.cpp can see if they
		// are a month name or whatever...
		if ( ! words.set ( &xml , true , niceness ) ) 
			log("scproxy: words set had error: %s",
			    mstrerror(g_errno));
	}

	if ( replySize > 0 && 
	     goodStatus &&
	     r->m_forEvents &&
	     !r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		int32_t cs = getCharsetFast ( &mime,
					   r->ptr_url,
					   content,
					   contentLen,
					   niceness);
		if ( cs != csUTF8 && // UTF-8
		     cs != csISOLatin1 && // ISO-8859-1
		     cs != csASCII &&
		     cs != csUnknown &&
		     cs != cswindows1256 &&
		     cs != cswindows1250 &&
		     cs != cswindows1255 &&
		     cs != cswindows1252 ) { // windows-1252
			// record in the stats
			docsPtr     = &g_stats.m_compressBadCharsetDocs;
			bytesInPtr  = &g_stats.m_compressBadCharsetBytesIn;
			bytesOutPtr = &g_stats.m_compressBadCharsetBytesOut;
			replySize = 0;
		}
	}
	*/

	if ( replySize > 0 && 
	     goodStatus &&
	     //r->m_forEvents &&
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

	/*
	if ( replySize > 0 && 
	     goodStatus && 
	     r->m_forEvents &&
	     ! r->m_isRobotsTxt && 
	     r->m_compressReply ) {
		// make sure we loaded the unifiedDict (do now in main.cpp)
		//g_speller.init();
		// detect language, if we can
		int32_t score;
		// returns -1 and sets g_errno on error, 
		// because 0 means langUnknown
		int32_t langid = words.getLanguage(NULL,1000,niceness,&score);
		// anything 2+ is non-english
		if ( langid >= 2 ) {
			// record in the stats
			docsPtr     = &g_stats.m_compressBadLangDocs;
			bytesInPtr  = &g_stats.m_compressBadLangBytesIn;
			bytesOutPtr = &g_stats.m_compressBadLangBytesOut;
			replySize = 0;
		}
	}
	*/

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	bool hasIframe2 = false;
	if ( r->m_compressReply &&
	     goodStatus &&
	     ! r->m_isRobotsTxt )
		hasIframe2 = hasIframe ( reply , replySize, niceness ) ;

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	if ( hasIframe2 && 
	     ! r->m_attemptedIframeExpansion &&
	     ! r->m_isSquidProxiedUrl ) {
		// must have ts i think
		if ( ! ts ) { char *xx=NULL; *xx=0; }
		// sanity
		if ( ts->m_readBuf != reply ) { char *xx=NULL;*xx=0;}
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
		// crap... had an error, give up i guess
		// record in the stats
		//docsPtr     = &g_stats.m_compressHasIframeDocs;
		//bytesInPtr  = &g_stats.m_compressHasIframeBytesIn;
		//bytesOutPtr = &g_stats.m_compressHasIframeBytesOut;
	}

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// compute content hash
	if ( r->m_contentHash32 && 
	     replySize>0 && 
	     goodStatus &&
	     r->m_compressReply &&
	     // if we got iframes we can't tell if content changed
	     ! hasIframe2 ) {
		// compute it
		int32_t ch32 = getContentHash32Fast( (unsigned char *)content ,
						  contentLen ,
						  niceness );
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

	// by default assume it has a good date
	int32_t status = 1;

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// sanity
	if ( reply && replySize>0 && reply[replySize-1]!='\0') {
		char *xx=NULL;*xx=0; }

	// force it good for debugging
	//status = 1;
	// xml set error?
	//if ( status == -1 ) {
	//	// sanity
	//	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	//	// g_errno must have been set!
	//	savedErr = g_errno;
	//	replySize = 0;
	//}
	// these are typically roots!
	if ( status == 1 && 
	     // override HasIFrame with "FullPageRequested" if it has
	     // an iframe, because that is the overriding stat. i.e. if
	     // we ignored if it had iframes, we'd still end up here...
	     ( ! docsPtr || docsPtr == &g_stats.m_compressHasIframeDocs ) &&
	     r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressFullPageDocs;
		bytesInPtr  = &g_stats.m_compressFullPageBytesIn;
		bytesOutPtr = &g_stats.m_compressFullPageBytesOut;
	}
	// hey, it had a good date on it...
	else if ( status == 1 && 
		  ! docsPtr &&
		  r->m_compressReply ) {
		// record in the stats
		docsPtr     = &g_stats.m_compressHasDateDocs;
		bytesInPtr  = &g_stats.m_compressHasDateBytesIn;
		bytesOutPtr = &g_stats.m_compressHasDateBytesOut;
	}

	// sanity check
	if ( status != -1 && status != 0 && status != 1 ){char *xx=NULL;*xx=0;}

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
			log("msg13: compression failed1 %s",r->ptr_url);
			
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
			    "(%"INT32") url=%s",
			    zError(zipErr),(int32_t)zipErr,r->ptr_url);
			mfree (compressedBuf, need, "Msg13ZipError");
			g_errno = ECORRUPTDATA;
			log("msg13: compression failed2 %s",r->ptr_url);
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply(slot,g_errno);
			return;
		}
		// . free the uncompressed reply so tcpserver does not have to
		// . no, now TcpServer will nuke it!!! or if called from
		//   gotIframeExpansion(), then deleting the xmldoc will nuke
		//   it
		//mfree ( reply , replyAllocSize , "msg13ubuf" );
		// it is toast
		//if ( ts ) ts->m_readBuf = NULL;
		// record the uncompressed size.
		reply          = compressedBuf;
		replySize      = 4 + compressedLen;
		replyAllocSize = need;
		// sanity check
		if ( replySize<0||replySize>100000000 ) { char *xx=NULL;*xx=0;}
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
		key_t k; k.n1 = 0; k.n0 = r->m_cacheKey;
		// add it, use a generic collection
		c->addRecord ( (collnum_t) 0 , k , reply , replySize );
		// ignore errors caching it
		g_errno = 0;
	}

	// shortcut
	UdpServer *us = &g_udpServer;

	// how many have this key?
	int32_t count = s_rt.getCount ( &r->m_cacheKey );
	// sanity check
	if ( count < 1 ) { char *xx=NULL;*xx=0; }

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
			     // this page had a bad mime
			     err != ECORRUPTHTTPGZIP &&
			     // broken pipe
			     err != EPIPE &&
			     err != EINLINESECTIONS &&
			     // connection reset by peer
			     err != ECONNRESET ) {
				log("http: bad error from httpserver get doc: %s",
				    mstrerror(err));
				char*xx=NULL;*xx=0;
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
			// because calling sendreply_ass with a NULL 
			// 'copy' cores it.
			if ( reply && ! copy ) {
				copyAllocSize = 0;
				err = ENOMEM;
			}
		}
		// this is not freeable
		if ( copy == g_fakeReply ) copyAllocSize = 0;
		// get request
		Msg13Request *r2;
		r2 = *(Msg13Request **)s_rt.getValueFromSlot(tableSlot);
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
				    
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			g_udpServer.sendErrorReply ( slot , err );
			continue;
		}
		// for debug for now
		if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
			log("msg13: sending reply for %s",r->ptr_url);
		// send reply
		us->sendReply_ass ( copy,replySize,copy,copyAllocSize, slot );
		// now final udp slot will free the reply, so tcp server
		// no longer has to. set this tcp buf to null then.
		if ( ts && ts->m_readBuf == reply && count == 0 ) 
			ts->m_readBuf = NULL;
	}
	// return now if we sent a regular non-error reply. it will have
	// sent the reply buffer and udpserver will free it when its done
	// transmitting it. 
	//if ( ts && ! savedErr ) return;
	// otherwise, we sent back a quick little error reply and have to
	// free the buffer here now. i think this was the mem leak we were
	// seeing.
	//if ( ! reply ) return;
	// do not let tcpserver free it
	//if ( ts ) ts->m_readBuf = NULL;
	// we free it - if it was never sent over a udp slot
	if ( savedErr && compressed ) 
		mfree ( reply , replyAllocSize , "msg13ubuf" );

	if ( g_conf.m_logDebugSpider || g_conf.m_logDebugMsg13 )
		log("msg13: handled reply ok %s",r->ptr_url);
}


void passOnReply ( void *state , UdpSlot *slot ) {
	// send that back
	Msg13Request *r = (Msg13Request *)state;
	// core for now
	//char *xx=NULL;*xx=0;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;

	/*
	// do not pass it on, we are where it stops if this is non-null
	if ( r->m_parent ) {
		r->m_parent->gotForwardedReply ( slot );
		return ;
	}
	*/

	if ( g_errno ) {
		log("spider: error from proxy for %s: %s",
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
	//int32_t  replyAllocSize = slot->m_readBufSize;
	// just forward it on
	g_udpServer.sendReply_ass( reply, replySize, reply, replyAllocSize, r->m_udpSlot );
}

//
//
// . UTILITY FUNCTIONS for injecting into the "qatest123" collection
// . we need to ensure that the web pages remain constant so we store them
//
//

// . returns true if found on disk in the test subdir
// . returns false with g_errno set on error
// . now that we are lower level in Msg13.cpp, set "ts" not "slot"
bool getTestDoc ( char *u , TcpSocket *ts , Msg13Request *r ) {
	// sanity check
	//if ( strcmp(m_coll,"qatest123") ) { char *xx=NULL;*xx=0; }
	// hash the url into 64 bits
	int64_t h = hash64 ( u , gbstrlen(u) );
	// read the spider date file first
	char fn[300]; 
	File f;

	// default to being from PageInject
	//char *td = "test-page-inject";
	//if ( r->m_testSpiderEnabled ) td = "test-spider";
	//if ( r->m_testParserEnabled ) td = "test-parser";
	//if ( r->m_isPageParser      ) td = "test-page-parser";
	char *td = r->m_testDir;
	//if ( ! td ) td = "test-page-parser";
	if ( ! td[0] ) { char *xx=NULL;*xx=0; }
	// make http reply filename
	sprintf(fn,"%s/%s/doc.%"UINT64".html",g_hostdb.m_dir,td,h);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) {
		//if ( g_conf.m_logDebugSpider )
			log("test: doc not found in test cache: %s (%"UINT64")",
			    u,h);
		return false;
	}
	// get size
	int32_t fs = f.getFileSize();
	// error?
	if ( fs == -1 ) 
		return log("test: error getting file size from test");
	// make a buf
	char *buf = (char *)mmalloc ( fs + 1 , "gtd");
	// no mem?
	if ( ! buf ) return log("test: no mem to get html file");
	// open it
	f.open ( O_RDWR );
	// read the HTTP REPLY in
	int32_t rs = f.read ( buf , fs , 0 );
	// not read enough?
	if ( rs != fs ) {
		mfree ( buf,fs,"gtd");
		return log("test: read returned %"INT32" != %"INT32"",rs,fs);
	}
	f.close();
	// null term it
	buf[fs] = '\0';

	// was it error=%"UINT32" ?
	if ( ! strncmp(buf,"errno=",6) ) {
		ts->m_readBuf     = NULL;
		ts->m_readBufSize = 0;
		ts->m_readOffset  = 0;
		g_errno = atol(buf+6);
		// fix mem leak
		mfree ( buf , fs+1 , "gtd" );
		// log it for now
		if ( g_conf.m_logDebugSpider )
			log("test: GOT ERROR doc in test cache: %s (%"UINT64") "
			    "[%s]",u,h, mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		return true;
	}

	// log it for now
	//if ( g_conf.m_logDebugSpider )
		log("test: GOT doc in test cache: %s (qa/doc.%"UINT64".html)",
		    u,h);
		
	//fprintf(stderr,"scp gk252:/e/test-spider/doc.%"UINT64".* /home/mwells/gigablast/test-parser/\n",h);

	// set the slot up now
	//slot->m_readBuf        = buf;
	//slot->m_readBufSize    = fs;
	//slot->m_readBufMaxSize = fs;
	ts->m_readBuf     = buf;
	ts->m_readOffset  = fs ;
	// if we had something, trim off the \0 so msg13.cpp can add it back
	if ( fs > 0 ) ts->m_readOffset--;
	ts->m_readBufSize = fs + 1;
	return true;
}

bool getTestSpideredDate ( Url *u , int32_t *origSpideredDate , char *testDir ) {
	// hash the url into 64 bits
	int64_t uh64 = hash64(u->getUrl(),u->getUrlLen());
	// read the spider date file first
	char fn[2000]; 
	File f;
	// get the spider date then
	sprintf(fn,"%s/%s/doc.%"UINT64".spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	f.set ( fn );
	// try to get it
	if ( ! f.doesExist() ) return false;
	// get size
	int32_t fs = f.getFileSize();
	// error?
	if ( fs == -1 ) return log("test: error getting file size from test");
	// open it
	f.open ( O_RDWR );
	// make a buf
	char dbuf[200];
	// read the date in (int format)
	int32_t rs = f.read ( dbuf , fs , 0 );
	// sanity check
	if ( rs <= 0 ) { char *xx=NULL;*xx=0; }
	// get it
	*origSpideredDate = atoi ( dbuf );
	// close it
	f.close();
	// note it
	//log("test: read spiderdate of %"UINT32" for %s",*origSpideredDate,
	//    u->getUrl());
	// good to go
	return true;
}

bool addTestSpideredDate ( Url *u , int32_t spideredTime , char *testDir ) {

	// ensure dir exists
	::mkdir(testDir,getDirCreationFlags());

	// set this
	int64_t uh64 = hash64(u->getUrl(),u->getUrlLen());
	// make that into a filename
	char fn[300]; 
	sprintf(fn,"%s/%s/doc.%"UINT64".spiderdate.txt",
		g_hostdb.m_dir,testDir,uh64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// make it into buf
	char dbuf[200]; sprintf ( dbuf ,"%"INT32"\n",spideredTime);
	// open it
	f.open ( O_RDWR | O_CREAT );
	// write it now
	int32_t ws = f.write ( dbuf , gbstrlen(dbuf) , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != (int32_t)gbstrlen(dbuf) )
		return log("test: error writing %"INT32" != %"INT32" to %s",ws,
			   (int32_t)gbstrlen(dbuf),fn);
	// close it up
	//f.close();
	return true;
}

// add it to our "qatest123" subdir
bool addTestDoc ( int64_t urlHash64 , char *httpReply , int32_t httpReplySize ,
		  int32_t err , Msg13Request *r ) {

	char fn[300];
	// default to being from PageInject
	//char *td = "test-page-inject";
	//if ( r->m_testSpiderEnabled ) td = "test-spider";
	//if ( r->m_testParserEnabled ) td = "test-parser";
	//if ( r->m_isPageParser      ) td = "test-page-parser";
	char *td = r->m_testDir;
	if ( ! td[0] ) { char *xx=NULL;*xx=0; }
	// make that into a filename
	sprintf(fn,"%s/%s/doc.%"UINT64".html",g_hostdb.m_dir,td,urlHash64);
	// look it up
	File f; f.set ( fn );
	// if already there, return now
	if ( f.doesExist() ) return true;
	// open it
	f.open ( O_RDWR | O_CREAT );
	// log it for now
	//if ( g_conf.m_logDebugSpider )
	log("test: ADDING doc to test cache: %"UINT64"",urlHash64);

	// write error only?
	if ( err ) {
		char ebuf[256];
		sprintf(ebuf,"errno=%"INT32"\n",err);
		f.write(ebuf,gbstrlen(ebuf),0);
		f.close();
		return true;
	}

	// write it now
	int32_t ws = f.write ( httpReply , httpReplySize , 0 );
	// close it
	f.close();
	// panic?
	if ( ws != httpReplySize )
		return log("test: error writing %"INT32" != %"INT32" to %s",ws,
			   httpReplySize,fn);
	// all done, success
	return true;
}

// . convert html/xml doc in place into a buffer of links, \n separated
// . return new reply size
// . return -1 on error w/ g_errno set on error
// . replySize includes terminating \0??? i dunno
int32_t convertIntoLinks ( char *reply , 
			int32_t replySize , 
			Xml *xml ,
			int32_t niceness ) {
	// the "doQuickSet" is just for us and make things faster and
	// more compressed...
	Links links;
	if ( ! links.set ( false , // useRelNoFollow
			   xml , 
			   NULL , // parentUrl
			   false , // setLinkHashes
			   NULL  , // baseUrl
			   0 , // version (unused)
			   niceness ,
			   false ,
			   NULL,
			   true ) )  // doQuickSet? YES!!!
		return -1;
	// use this to ensure we do not breach
	char *dstEnd = reply + replySize;
	// . store into the new buffer
	// . use gbmemcpy() because it deal with potential overlap issues
	char *dst    = reply;
	// store the thing first
	if ( dst + 100 >= dstEnd ) 
		// if no room, forget it
		return 0;
	// first the mime
	dst += sprintf ( dst , 
			 "HTTP/1.0 200\r\n"
			 "Content-Length: " );
	// save that place
	char *saved = dst;
	// now write a placeholder number
	dst += sprintf ( dst , "00000000\r\n\r\n" );

	// save this
	char *content = dst;
			 
	// this tells xmldoc.cpp what's up
	//gbmemcpy ( dst , "<!--links-->\n", 13 );
	//dst += 13;
	// iterate over the links
	for ( int32_t i = 0 ; i < links.m_numLinks ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get link
		char *str = links.getLink(i);
		// get size
		int32_t len = links.getLinkLen(i);
		// ensure no breach. if so, return now
		if ( dst + len + 2 > dstEnd ) return dst - reply;
		// lead it
		gbmemcpy ( dst, "<a href=", 8 );
		dst += 8;
		// copy over, should be ok with overlaps
		gbmemcpy ( dst , str , len );
		dst += len;
		// end tag and line
		gbmemcpy ( dst , "></a>\n", 6 );
		dst += 6;
	}
	// null term it!
	*dst++ = '\0';
	// content length
	int32_t clen = dst - content - 1;
	// the last digit
	char *dptr = saved + 7;
	// store it up top in the mime header
	for ( int32_t x = 0 ; x < 8 ; x++ ) {
		//if ( clen == 0 ) *dptr-- = ' ';
		if ( clen == 0 ) break;
		*dptr-- = '0' + (clen % 10);
		clen /= 10;
	}
	// the new replysize is just this plain list of links
	return dst - reply;
}

// returns true if <iframe> tag in there
bool hasIframe ( char *reply, int32_t replySize , int32_t niceness ) {
	if ( ! reply || replySize <= 0 ) return false;
	char *p = reply;
	// exclude \0
	char *pend = reply + replySize - 1;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
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

char getContentTypeQuick ( HttpMime *mime,
			   char *reply , 
			   int32_t replySize , 
			   int32_t niceness ) {
	char ctype = mime->getContentType();
	char ctype2 = 0;
	if ( replySize>0 && reply ) {
		// mime is start of reply, so skip to content section
		char *content = reply + mime->getMimeLen();
		// defined in XmlDoc.cpp...
		ctype2 = getContentTypeFromContent(content,niceness);
	}
	if ( ctype2 ) ctype = ctype2;
	return ctype;
}


// returns false if blocks, true otherwise
bool getIframeExpandedContent ( Msg13Request *r , TcpSocket *ts ) {

	if ( ! ts ) { char *xx=NULL;*xx=0; }

	int32_t niceness = r->m_niceness;

	// ok, we've an attempt now
	r->m_attemptedIframeExpansion = true;

	// we are doing something to destroy reply, so make a copy of it!
	int32_t copySize = ts->m_readOffset + 1;
	char *copy = (char *)mdup ( ts->m_readBuf , copySize , "ifrmcpy" );
	if ( ! copy ) return true;
	// sanity, must include \0 at the end
	if ( copy[copySize-1] ) { char *xx=NULL;*xx=0; }

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
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
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
	if ( xd->m_didExpansion ) { char *xx=NULL;*xx=0; }

	// try to reconstruct ts
	//ts->m_readBuf = xd->m_httpReply;
	// and do not allow xmldoc to free that buf
	//xd->m_httpReply = NULL;

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
		if ( ! xd->m_mimeValid ) { char *xx=NULL;*xx=0; }
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
		if ( reply[replySize-1] ) { char *xx=NULL;*xx=0; }
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
	if ( reply && reply[replySize-1] != '\0') { char *xx=NULL;*xx=0; }
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
	if ( ! r->m_udpSlot ) { char *xx=NULL;*xx=0; }

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
		// and no proxies are available to use
		//! canUseProxies ) {
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
	if ( //r->m_hammerCallback == downloadTheDocForReals3b &&
	     r->m_numBannedProxies &&
	     r->m_numBannedProxies * DELAYPERBAN > crawlDelayMS ) {
		crawlDelayMS = r->m_numBannedProxies * DELAYPERBAN;
		if ( crawlDelayMS > MAX_PROXYCRAWLDELAYMS )
			crawlDelayMS = MAX_PROXYCRAWLDELAYMS;
	}

	// set the crawldelay we actually used when downloading this
	//r->m_usedCrawlDelay = crawlDelayMS;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: got timestamp of %"INT64" from "
		    "hammercache (waited=%"INT64" crawlDelayMS=%"INT32") "
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
		    "spider: adding %s to crawldelayqueue cd=%"INT32"ms "
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
		    "only waited %"INT64" ms of %"INT32" ms",
		    iptoa(r->m_firstIp),r->ptr_url,waited,
		    crawlDelayMS);
		// this guy has too many redirects and it fails us...
		// BUT do not core if running live, only if for test
		// collection
		// for now disable it seems like 99.9% good... but
		// still cores on some wierd stuff...
		// if doing this on qatest123 then core
		if(r->m_useTestCache ) { // && r->m_firstIp!=-1944679785 ) {
			char*xx = NULL; *xx = 0; }
	}
	// store time now
	//s_hammerCache.addLongLong(0,r->m_firstIp,nowms);
	// note it
	//if ( g_conf.m_logDebugSpider )
	//	log("spider: adding download end time of %"UINT64" for "
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
		//    "waited=%"INT64"ms crawldelay=%"INT32"ms", 
		//    r->ptr_url,waited,r->m_crawlDelayMS);

		// good to go
		//downloadTheDocForReals ( r );

		// sanity check
		if ( ! r->m_hammerCallback ) { char *xx=NULL;*xx=0; }

		// callback can now be either downloadTheDocForReals(r)
		// or downloadTheDocForReals3b(r) if it is waiting after 
		// getting a ProxyReply that had a m_proxyBackoff set

		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: calling hammer callback for "
			    "%s (timestamp=%"INT64",waited=%"INT64",crawlDelayMS=%"INT32")",
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
	if ( ! sp->m_usernamePwd ) return true;
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
	char *needle = "Proxy-Authorization: ";
	char *s = gb_strcasestr ( squidProxiedReqBuf , needle );
	if ( ! s ) return;
	// find next \r\n
	char *end = strstr ( s , "\r\n");
	if ( ! end ) return;
	// bury the \r\n as well
	end += 2;
	// bury that string
	int32_t reqLen = gbstrlen(squidProxiedReqBuf);
	char *reqEnd = squidProxiedReqBuf + reqLen;
	// include \0, so add +1
	gbmemcpy ( s ,end , reqEnd-end + 1);
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
	char *reqEnd = squidProxiedReqBuf + gbstrlen(squidProxiedReqBuf);
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

	//log("debug: cookiehash=%"INT64"",hash64(start,s-start));

	return h64;
}

#include "Pages.h"

bool printHammerQueueTable ( SafeBuf *sb ) {

	char *title = "Queued Download Requests";
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

	Msg13Request *r = s_hammerQueueHead ;

	int32_t count = 0;
	int64_t nowms = gettimeofdayInMilliseconds();

 loop:
	if ( ! r ) return true;

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
		sb->safePrintf( ":%"INT32"", port );
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
	char *coll = "none";
	if ( cr ) coll = cr->m_coll;
	sb->safePrintf("<td>");
	if ( cr ) {
		sb->safePrintf("<a href=/admin/sockets?c=");
		sb->urlEncode(coll);
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

	// print next entry now
	r = r->m_nextLink;
	goto loop;

}
