#include "gb-include.h"
#include "XmlDoc.h"
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif
#include "SummaryCache.h"

static void handleRequest20   ( UdpSlot *slot , int32_t netnice );
static bool gotReplyWrapperxd ( void *state ) ;


static bool sendCachedReply ( Msg20Request *req, const void *cached_summary, size_t cached_summary_len, UdpSlot *slot );


Msg20::Msg20 () { constructor(); }
Msg20::~Msg20() { reset(); }

void Msg20::constructor () {
	m_request = NULL;
	m_r       = NULL;
	m_inProgress = false;
	m_launched = false;
	m_ii = -1;
	reset();
	m_mcast.constructor();
}

void Msg20::destructor  () { reset(); m_mcast.destructor(); }

#include "Process.h"

void Msg20::freeReply() {
	if (!m_r) {
		return;
	}

	// sometimes the msg20 reply carries an merged bffer from
	// msg40 that is a constructed ptr_eventSummaryLines from a
	// merge operation in msg40. this fixes the "merge20buf1" memory
	// leak from Msg40.cpp
	m_r->destructor();

	if ( m_ownReply ) {
		mfree(m_r, m_replyMaxSize, "Msg20b");
	}

	m_r = NULL;
}

void Msg20::reset() {
	// not allowed to reset one in progress
	if ( m_inProgress ) { 
		// do not core on abrupt exits!
		if (g_process.m_mode == EXIT_MODE ) {
			log("msg20: msg20 not being freed because exiting.");
			return;
		}
		// otherwise core
		g_process.shutdownAbort(true); 
	}

	m_launched = false;
	if ( m_request ) {
		mfree( m_request, m_requestSize, "Msg20rb1" );
	}

	freeReply();

	m_request      = NULL; // the request buf ptr
	m_gotReply     = false;
	m_errno        = 0;
	m_requestDocId = -1LL;
	m_callback     = NULL;
	m_state        = NULL;
	m_ownReply     = true;
}

bool Msg20::registerHandler ( ) {
	// . register ourselves with the udp server
    // . it calls our callback when it receives a msg of type 0x20
    if ( ! g_udpServer.registerHandler ( msg_type_20, handleRequest20 ))
		return false;

	return true;
}

// copy "src" to ourselves
void Msg20::copyFrom ( Msg20 *src ) {
	gbmemcpy ( this , src , sizeof(Msg20) );

	// make sure it does not free it!
	src->m_r = NULL;
	m_request = NULL;

	// make sure destructor does not free this
	src->m_request = NULL;
	src->destructor();
}

// returns true and sets g_errno on error, otherwise, blocks and returns false
bool Msg20::getSummary ( Msg20Request *req ) {
	// reset ourselves in case recycled
	reset();

	// consider it "launched"
	m_launched = true;

	// save it
	m_requestDocId = req->m_docId;
	m_state        = req->m_state;
	m_callback     = req->m_callback;
	m_callback2    = NULL;

	// does this ever happen?
	if ( g_hostdb.getNumHosts() <= 0 ) {
		log("build: hosts2.conf is not in working directory, or "
		    "contains no valid hosts.");
		g_errno = EBADENGINEER;
		return true;
	}

	if ( req->m_docId < 0 && ! req->ptr_ubuf ) {
		log("msg20: docid<0 and no url for msg20::getsummary");
		g_errno = EBADREQUEST;
		return true;
	}

	// get groupId from docId, if positive
	uint32_t shardNum;
	if ( req->m_docId >= 0 ) 
		shardNum = g_hostdb.getShardNumFromDocId(req->m_docId);
	else {
		int64_t pdocId = g_titledb.getProbableDocId(req->ptr_ubuf);
		shardNum = getShardNumFromDocId(pdocId);
	}

	// we might be getting inlinks for a spider request
	// so make sure timeout is inifinite for that...
	const int32_t timeout = (req->m_niceness==0)
	                      ? multicast_msg20_summary_timeout
	                      : multicast_infinite_send_timeout;

	// get our group
	int32_t  allNumHosts = g_hostdb.getNumHostsPerShard();
	Host *allHosts    = g_hostdb.getShard ( shardNum );

	// put all alive hosts in this array
	Host *cand[32];
	int64_t  nc = 0;
	for ( int32_t i = 0 ; i < allNumHosts ; i++ ) {
		// get that host
		Host *hh = &allHosts[i];
		// skip if dead
		if ( g_hostdb.isDead(hh) ) continue;

		// Respect no-spider, no-query directives from hosts.conf 
		if ( !req->m_getLinkInfo && ! hh->m_queryEnabled ) continue;
		if ( req->m_getLinkInfo && ! hh->m_spiderEnabled ) continue;
		// add it if alive
		cand[nc++] = hh;
	}
	// if none alive, make them all candidates then
	bool allDead = (nc == 0);
	for ( int32_t i = 0 ; allDead && i < allNumHosts ; i++ ) {
		// NEVER add a noquery host to the candidate list, even
		// if the query host is dead
		if ( ! allHosts[i].m_queryEnabled ) continue;
		cand[nc++] = &allHosts[i];
	}

	if ( nc == 0 ) {
		log("msg20: error sending mcast: no queryable hosts "
		    "availble to handle summary generation");
		g_errno = EBADENGINEER;
		m_gotReply = true;
		return true;
	}

	// route based on docid region, not parity, because we want to hit
	// the urldb page cache as much as possible
	int64_t sectionWidth =((128LL*1024*1024)/nc)+1;
	int64_t probDocId    = req->m_docId;
	// i think reference pages just pass in a url to get the summary
	if ( probDocId < 0 && req->size_ubuf ) 
		probDocId = g_titledb.getProbableDocId ( req->ptr_ubuf );
	if ( probDocId < 0        ) {
		log("query: Got bad docid/url combo.");
		probDocId = 0;
	}
	// we mod by 1MB since tied scores resort to sorting by docid
	// so we don't want to overload the host responsible for the lowest
	// range of docids. CAUTION: do this for msg22 too!
	// in this way we should still ensure a pretty good biased urldb
	// cache... 
	// . TODO: fix the urldb cache preload logic
	int32_t hostNum = (probDocId % (128LL*1024*1024)) / sectionWidth;
	if ( hostNum < 0 ) hostNum = 0; // watch out for negative docids
	if ( hostNum >= nc ) { g_process.shutdownAbort(true); }
	int32_t firstHostId = cand [ hostNum ]->m_hostId ;

	m_requestSize = 0;
	m_request = req->serialize ( &m_requestSize );
	// . it sets g_errno on error and returns NULL
	// . we MUST call gotReply() here to set m_gotReply
	//   otherwise Msg40.cpp can end up looping forever
	//   calling Msg40::launchMsg20s()
	if ( ! m_request ) { gotReply(NULL); return true; }

	// . otherwise, multicast to a host in group "groupId"
	// . returns false and sets g_errno on error
	// . use a pre-allocated buffer to hold the reply
	// . TMPBUFSIZE is how much a UdpSlot can hold w/o allocating
	if (!m_mcast.send(m_request, m_requestSize, msg_type_20, false, shardNum, false, probDocId, this, NULL, gotReplyWrapper20, timeout, req->m_niceness, firstHostId, NULL, false)) {
		// sendto() sometimes returns "Network is down" so i guess
		// we just had an "error reply".
		log("msg20: error sending mcast %s",mstrerror(g_errno));
		m_gotReply = true;
		return true;
	}

	// we are officially "in progress"
	m_inProgress = true;

	// we blocked
	return false;
}

void Msg20::gotReplyWrapper20 ( void *state , void */*state2*/ ) {
	Msg20 *THIS = (Msg20 *)state;
	// gotReply() does not block, and does NOT call our callback
	THIS->gotReply ( NULL ) ;
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
	else THIS->m_callback2 ( THIS->m_state );
}

// . set m_reply/m_replySize to the reply
void Msg20::gotReply ( UdpSlot *slot ) {
	// we got the reply
	m_gotReply = true;
	// no longer in progress, we got a reply
	m_inProgress = false;
	// sanity check
	if ( m_r ) { g_process.shutdownAbort(true); }

	// free our serialized request buffer to save mem
	if ( m_request ) {
		mfree ( m_request , m_requestSize  , "Msg20rb2" );
		m_request = NULL;
	}

	// save error so Msg40 can look at it
	if ( g_errno ) { 
		m_errno = g_errno;
		log( LOG_WARN, "query: msg20: got reply for docid %" PRId64" : %s", m_requestDocId,mstrerror(g_errno));
		return; 
	}
	// . get the best reply we got
	// . we are responsible for freeing this reply
	bool freeit;
	// . freeit is true if mcast will free it
	// . we should always own it since we call deserialize and has ptrs
	//   into it
	char *rp = NULL;
	if ( slot ) {
		rp             = slot->m_readBuf;
		m_replySize    = slot->m_readBufSize;
		m_replyMaxSize = slot->m_readBufMaxSize;
		freeit = false;
	}
	else {
		rp =m_mcast.getBestReply(&m_replySize,&m_replyMaxSize,&freeit);
	}

	relabel( rp , m_replyMaxSize, "Msg20-mcastGBR" );

	// sanity check. make sure multicast is not going to free the
	// slot's m_readBuf... we need to own it.
	if ( freeit ) {
		log(LOG_LOGIC,"query: msg20: gotReply: Bad engineer.");
		g_process.shutdownAbort(true);
	}

	// see if too small for a getSummary request
	if ( m_replySize < (int32_t)sizeof(Msg20Reply) ) { 
		log("query: Summary reply is too small.");
		//g_process.shutdownAbort(true);
		m_errno = g_errno = EREPLYTOOSMALL; return; }

	// cast it
	m_r = (Msg20Reply *)rp;

	// we own it now
	m_ownReply = true;

	// deserialize it, sets g_errno on error??? not yet TODO!
	m_r->deserialize();
}

// . this is called
// . destroys the UdpSlot if false is returned
void handleRequest20 ( UdpSlot *slot , int32_t netnice ) {
	// . check g_errno
	// . before, we were not sending a reply back here and we continued
	//   to process the request, even though it was empty. the slot
	//   had a NULL m_readBuf because it could not alloc mem for the read
	//   buf i'm assuming. and the slot was saved in a line below here...
	//   state20->m_msg22.m_parent = slot;
	if ( g_errno ) {
		log(LOG_WARN, "net: Msg20 handler got error: %s.",mstrerror(g_errno));
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	// ensure request is big enough
	if ( slot->m_readBufSize < (int32_t)sizeof(Msg20Request) ) {
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. Bad request size", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}

	// parse the request
	Msg20Request *req = (Msg20Request *)slot->m_readBuf;

	// . turn the string offsets into ptrs in the request
	// . this is "destructive" on "request"
	int32_t nb = req->deserialize();
	// sanity check
	if ( nb != slot->m_readBufSize ) { g_process.shutdownAbort(true); }

	// sanity check, the size include the \0
	if ( req->m_collnum < 0 ) {
		log(LOG_WARN, "query: Got empty collection in msg20 handler. FIX! "
		    "from ip=%s port=%i",iptoa(slot->getIp()),(int)slot->getPort());
		    
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , ENOTFOUND );
		return; 
	}

	int64_t cache_key = req->makeCacheKey();
	const void *cached_summary;
	size_t cached_summary_len;
	if(g_stable_summary_cache.lookup(cache_key, &cached_summary, &cached_summary_len) ||
	   g_unstable_summary_cache.lookup(cache_key, &cached_summary, &cached_summary_len))
	{
		log(LOG_DEBUG, "query: Summary cache hit");
		sendCachedReply(req,cached_summary,cached_summary_len,slot);
		return;
	} else
		log(LOG_DEBUG, "query: Summary cache miss");

	// if it's not stored locally that's an error
	if ( req->m_docId >= 0 && ! g_titledb.isLocal ( req->m_docId ) ) {
		log(LOG_WARN, "query: Got msg20 request for non-local docId %" PRId64, req->m_docId);
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , ENOTLOCAL ); 
		return; 
	}

	// sanity
	if ( req->m_docId == 0 && ! req->ptr_ubuf ) { //g_process.shutdownAbort(true); }
		log( LOG_WARN, "query: Got msg20 request for docid of 0 and no url for "
		    "collnum=%" PRId32" query %s",(int32_t)req->m_collnum,req->ptr_qbuf);

		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , ENOTFOUND );
		return; 
	}

	int64_t startTime = gettimeofdayInMilliseconds();

	// alloc a new state to get the titlerec
	XmlDoc *xd;

	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("query: msg20 new(%" PRId32"): %s", (int32_t)sizeof(XmlDoc),
		    mstrerror(g_errno));
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror( g_errno ));
		g_udpServer.sendErrorReply ( slot, g_errno ); 
		return; 
	}
	mnew ( xd , sizeof(XmlDoc) , "xd20" );

	// ok, let's use the new XmlDoc.cpp class now!
	xd->set20 ( req );

	// encode slot
	xd->m_slot = slot;

	// set the callback
	xd->setCallback ( xd , gotReplyWrapperxd );

	// set set time
	xd->m_setTime = startTime;
	xd->m_cpuSummaryStartTime = 0;

	// . now as for the msg20 reply!
	// . TODO: move the parse state cache into just a cache of the
	//   XmlDoc itself, and put that cache logic into XmlDoc.cpp so
	//   it can be used more generally.
	Msg20Reply *reply = xd->getMsg20Reply ( );

	// this is just blocked
	if ( reply == (void *)-1 ) return;

	// got it?
	gotReplyWrapperxd ( xd );
}

bool gotReplyWrapperxd ( void *state ) {
	// grab it
	XmlDoc *xd = (XmlDoc *)state;

	// get it
	UdpSlot *slot = (UdpSlot *)xd->m_slot;
	// parse the request
	Msg20Request *req = (Msg20Request *)slot->m_readBuf;
	// print time
	int64_t now = gettimeofdayInMilliseconds();
	int64_t took = now - xd->m_setTime;
	int64_t took2 = 0;
	if ( xd->m_cpuSummaryStartTime) {
		took2 = now - xd->m_cpuSummaryStartTime;
	}

	// if there is a baclkog of msg20 summary generation requests this
	// is really not the cpu it took to make the smmary, but how long it
	// took to get the reply. this request might have had to wait for the
	// other summaries to finish computing before it got its turn, 
	// meanwhile its clock was ticking. TODO: make this better?
	// only do for niceness 0 otherwise it gets interrupted by quickpoll
	// and can take a int32_t time.
	if ( req->m_niceness == 0 && (req->m_isDebug || took > 100 || took2 > 100 ) ) {
		log(LOG_TIMING, "query: Took %" PRId64" ms (total=%" PRId64" ms) to compute summary for d=%" PRId64" "
		    "u=%s status=%s q=%s",
		    took2,
			took,
		    xd->m_docId,xd->m_firstUrl.getUrl(),
		    mstrerror(g_errno),
		    req->ptr_qbuf);
	}

	// error?
	if ( g_errno ) { xd->m_reply.sendReply ( req, xd ); return true; }
	// this should not block now
	Msg20Reply *reply = xd->getMsg20Reply ( );
	// sanity check, should not block here now
	if ( reply == (void *)-1 ) { g_process.shutdownAbort(true); }
	// NULL means error, -1 means blocked. on error g_errno should be set
	if ( ! reply && ! g_errno ) { g_process.shutdownAbort(true);}
	// send it off. will send an error reply if g_errno is set
	return reply->sendReply ( req, xd );
}

Msg20Reply::Msg20Reply ( ) {
	// seems to be an issue... caused a core with bogus size_dbuf
	int32_t *sizePtr = &size_tbuf;
	int32_t *sizeEnd = &size_note;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		*sizePtr = 0;
}


// we need to free the ptr_summaryLines if it is pointing into a new buffer
// which is what Msg40 sometimes does to it when it merges Msg20Reply's 
// summaries for events together.
Msg20Reply::~Msg20Reply ( ) {
	destructor();
}

void Msg20Reply::destructor ( ) {
}

#include "Stats.h"

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
bool Msg20Reply::sendReply ( Msg20Request *req, XmlDoc *xd ) {

	// get it
	UdpSlot *slot = (UdpSlot *)xd->m_slot;

	if ( g_errno ) {
		// extract titleRec ptr
		log(LOG_ERROR, "query: Had error generating msg20 reply for d=%" PRId64": %s",xd->m_docId, mstrerror(g_errno));
		// don't forget to delete this list
	haderror:
		mdelete ( xd, sizeof(XmlDoc) , "Msg20" );
		delete ( xd );
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror( g_errno ));
		g_udpServer.sendErrorReply ( slot , g_errno ) ;
		return true;
	}

	// now create a buffer to store title/summary/url/docLen and send back
	int32_t  need = getStoredSize();
	char *buf  = (char *)mmalloc ( need , "Msg20Reply" );
	if ( ! buf ) goto haderror;

	// should never have an error!
	int32_t used = serialize ( buf , need );

	// sanity
	if ( used != need ) { g_process.shutdownAbort(true); }

	// use blue for our color
	int32_t color = 0x0000ff;

	// but use dark blue for niceness > 0
	if ( xd->m_niceness > 0 ) color = 0x0000b0;

	// sanity check
	if ( ! xd->m_utf8ContentValid ) { g_process.shutdownAbort(true); }

	// for records
	int32_t clen = 0;

	if ( xd->m_utf8ContentValid ) clen = xd->size_utf8Content - 1;

	// show it in performance graph
	if ( xd->m_startTimeValid ) {
		g_stats.addStat_r( clen, xd->m_startTime, gettimeofdayInMilliseconds(), color );
	}
	
	
	//put the reply into the summary cache
	if(m_isDisplaySumSetFromTags && !req->m_highlightQueryTerms)
		g_stable_summary_cache.insert(req->makeCacheKey(), buf, need);
	else
		g_unstable_summary_cache.insert(req->makeCacheKey(), buf, need);

	// . del the list at this point, we've copied all the data into reply
	// . this will free a non-null State20::m_ps (ParseState) for us
	mdelete ( xd , sizeof(XmlDoc) , "xd20" );
	delete ( xd );
	
	g_udpServer.sendReply(buf, need, buf, need, slot);

	return true;
}


static bool sendCachedReply ( Msg20Request *req, const void *cached_summary, size_t cached_summary_len, UdpSlot *slot )
{
	//copy the cached summary to a new temporary buffer, so that UDPSlot/Server can free it when possible
	char *buf  = (char *)mmalloc ( cached_summary_len , "Msg20Reply" );
	if(!buf) {
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror( g_errno ));
		g_udpServer.sendErrorReply ( slot , g_errno ) ;
		return true;
	}
	memcpy(buf,cached_summary,cached_summary_len);
	
	g_udpServer.sendReply(buf, cached_summary_len, buf, cached_summary_len, slot);
	
	return true;
}


// . this is destructive on the "buf". it converts offs to ptrs
// . sets m_r to the modified "buf" when done
// . sets g_errno and returns -1 on error, otherwise # of bytes deseril
int32_t Msg20::deserialize ( char *buf , int32_t bufSize ) { 
	if ( bufSize < (int32_t)sizeof(Msg20Reply) ) {
		g_errno = ECORRUPTDATA; return -1; }
	m_r = (Msg20Reply *)buf;
	// do not free "buf"/"m_r"
	m_ownReply = false;
	return m_r->deserialize ( );
}

int32_t Msg20Request::getStoredSize ( ) {
	return getMsgStoredSize(sizeof(*this), &size_qbuf, &size_displayMetas);
}

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *Msg20Request::serialize ( int32_t *retSize ) {
	// make a buffer to serialize into
	int32_t  need = getStoredSize();
	// alloc if we should
	char *buf = (char *)mmalloc ( need , "Msg20Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;

	return serializeMsg(sizeof(*this),
			    &size_qbuf, &size_displayMetas,
			    &ptr_qbuf,
			    this,
			    retSize,
			    buf, need);
}

// convert offsets back into ptrs
int32_t Msg20Request::deserialize ( ) {
	return deserializeMsg(sizeof(*this),
			      &size_qbuf, &size_displayMetas,
		              &ptr_qbuf,
		              ((char*)this) + sizeof(*this));
}


//make a cache key for a request
int64_t Msg20Request::makeCacheKey() const
{
	SafeBuf hash_buffer;
	hash_buffer.pushLong(m_numSummaryLines);
	hash_buffer.pushLong(m_getHeaderTag);
	hash_buffer.pushLongLong(m_docId);
	hash_buffer.pushLong(m_titleMaxLen);
	hash_buffer.pushLong(m_summaryMaxLen);
	hash_buffer.pushLong(m_summaryMaxNumCharsPerLine);
	hash_buffer.pushLong(m_collnum);
	hash_buffer.pushLong(m_highlightQueryTerms);
	hash_buffer.pushLong(m_getSummaryVector);
	hash_buffer.pushLong(m_showBanned);
	hash_buffer.pushLong(m_includeCachedCopy);
	hash_buffer.pushLong(m_doLinkSpamCheck);
	hash_buffer.pushLong(m_isLinkSpam);
	hash_buffer.pushLong(m_isSiteLinkInfo);
	hash_buffer.pushLong(m_getLinkInfo);
	hash_buffer.pushLong(m_onlyNeedGoodInlinks);
	hash_buffer.pushLong(m_getLinkText);
	hash_buffer.safeMemcpy(ptr_qbuf,size_qbuf);
	hash_buffer.safeMemcpy(ptr_ubuf,size_ubuf);
	hash_buffer.safeMemcpy(ptr_linkee,size_linkee);
	hash_buffer.safeMemcpy(ptr_displayMetas,size_displayMetas);
	int64_t h = hash64(hash_buffer.getBufStart(), hash_buffer.length());
	return h;
}


int32_t Msg20Reply::getStoredSize ( ) {
	return getMsgStoredSize(sizeof(*this), &size_tbuf, &size_note);
}


// returns NULL and set g_errno on error
int32_t Msg20Reply::serialize ( char *buf , int32_t bufSize ) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(this,sizeof(*this));
	if(ptr_htag)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_htag,size_htag);
	if(ptr_ubuf)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_ubuf,size_ubuf);
	if(ptr_rubuf)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_rubuf,size_rubuf);
	if(ptr_displaySum)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_displaySum,size_displaySum);
	if(ptr_dbuf)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_dbuf,size_dbuf);
	if(ptr_vbuf)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_vbuf,size_vbuf);
	if(ptr_imgData)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_imgData,size_imgData);
	if(ptr_site)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_site,size_site);
	if(ptr_linkInfo)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_linkInfo,size_linkInfo);
	if(ptr_outlinks)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_outlinks,size_outlinks);
	if(ptr_vector1)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_vector1,size_vector1);
	if(ptr_vector2)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_vector2,size_vector2);
	if(ptr_vector3)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_vector3,size_vector3);
	if(ptr_linkText)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_linkText,size_linkText);
	if(ptr_surroundingText)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_surroundingText,size_surroundingText);
	if(ptr_linkUrl)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_linkUrl,size_linkUrl);
	if(ptr_rssItem)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_rssItem,size_rssItem);
	if(ptr_categories)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_categories,size_categories);
	if(ptr_content)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_content,size_content);
	if(ptr_templateVector)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_templateVector,size_templateVector);
	if(ptr_metadataBuf)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_metadataBuf,size_metadataBuf);
	if(ptr_note)
		VALGRIND_CHECK_MEM_IS_DEFINED(ptr_note,size_note);
#endif
	int32_t retSize;
	serializeMsg(sizeof(*this),
	             &size_tbuf, &size_note,
	             &ptr_tbuf,
	             this,
	             &retSize,
	             buf, bufSize);
	if ( retSize > bufSize ) { g_process.shutdownAbort(true); }
	// return it
	return retSize;
}

// convert offsets back into ptrs
int32_t Msg20Reply::deserialize ( ) {
	int32_t bytesParsed = deserializeMsg(sizeof(*this),
					     &size_tbuf, &size_note,
					     &ptr_tbuf,
					     ((char*)this) + sizeof(*this));
	if(bytesParsed<0)
		return bytesParsed;
	
	// sanity
	if ( ptr_linkInfo && ((LinkInfo *)ptr_linkInfo)->m_lisize !=
		    size_linkInfo ) { 
		log("xmldoc: deserialize msg20 reply corruption error");
		log("xmldoc: DO YOU NEED TO NUKE CACHEDB.DAT?????");
		return -1;
	}

	// return how many bytes we used
	return bytesParsed;
}
