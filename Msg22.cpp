#include "gb-include.h"

#include "Msg22.h"
#include "Titledb.h"
#include "UdpServer.h"
#include "UdpSlot.h"
#include "Collectiondb.h"
#include "Process.h"

static void handleRequest22 ( UdpSlot *slot , int32_t netnice ) ;

Msg22Request::Msg22Request() {
	//use memset() to clear out the padding bytes in the structure
	memset(this, 0, sizeof(*this));
	m_inUse = false;
}

bool Msg22::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x22
	return g_udpServer.registerHandler ( msg_type_22, handleRequest22 );
}

Msg22::Msg22() {
	m_availDocId = 0;
	m_titleRecPtrPtr = NULL;
	m_titleRecSizePtr = NULL;
	m_callback = NULL;
	m_state = NULL;
	m_found = false;
	m_errno = 0;
	m_outstanding = false;
	m_r = NULL;
}

Msg22::~Msg22(){
}


// . sets m_availDocId or sets g_errno to ENOTFOUND on error
// . calls callback(state) when done
// . returns false if blocked true otherwise
bool Msg22::getAvailDocIdOnly ( Msg22Request  *r              ,
				int64_t preferredDocId ,
				char *coll ,
				void *state ,
				void (* callback)(void *state) ,
				int32_t niceness ) {
	return getTitleRec ( r ,
			     NULL     , //   url
			     preferredDocId    ,
			     coll     ,
			     NULL     , // **titleRecPtrPtr
			     NULL     , //  *titleRecSizePtr
			     false    , //   justCheckTfndb
			     true     , //   getAvailDocIdOnly
			     state    ,
			     callback ,
			     niceness ,
			     9999999  ); // timeout
}


// . if url is NULL use the docId to get the titleRec
// . if titleRec is NULL use our own internal m_myTitleRec
// . sets g_errno to ENOTFOUND if TitleRec does not exist for this url/docId
// . if g_errno is ENOTFOUND m_docId will be set to the best available docId
//   for this url to use if we're adding it to Titledb
// . if g_errno is ENOTFOUND and m_docId is 0 then no docIds were available
// . "url" must be NULL terminated
bool Msg22::getTitleRec ( Msg22Request  *r              ,
			  char          *url            ,
			  int64_t      docId          ,
			  char          *coll           ,
			  char         **titleRecPtrPtr ,
			  int32_t          *titleRecSizePtr,
			  bool           justCheckTfndb ,
			  // when indexing spider replies we just want
			  // a unique docid... "docId" should be the desired
			  // one, but we might have to change it.
			  bool           getAvailDocIdOnly  ,
			  void          *state          ,
			  void         (* callback) (void *state) ,
			  int32_t           niceness       ,
			  int32_t           timeout ) {

	m_availDocId = 0;

	// sanity
	if ( getAvailDocIdOnly && justCheckTfndb ) { g_process.shutdownAbort(true); }
	if ( getAvailDocIdOnly && url            ) { g_process.shutdownAbort(true); }

	//if ( url ) log(LOG_DEBUG,"build: getting TitleRec for %s",url);
	// sanity checks
	if ( url    && docId!=0LL ) { g_process.shutdownAbort(true); }
	if ( url    && !url[0]    ) { 
                log("msg22: BAD URL! It is empty!");
                m_errno = g_errno = EBADENGINEER;
                return true;
//g_process.shutdownAbort(true); 
	}
	if ( docId!=0LL && url    ) { g_process.shutdownAbort(true); }
	if ( ! coll               ) { g_process.shutdownAbort(true); }
	if ( ! callback           ) { g_process.shutdownAbort(true); }
	if ( r->m_inUse           ) { g_process.shutdownAbort(true); }
	if ( m_outstanding        ) { g_process.shutdownAbort(true); }
	// sanity check
	if ( ! justCheckTfndb && ! getAvailDocIdOnly ) {
		if ( ! titleRecPtrPtr  ) { g_process.shutdownAbort(true); }
		if ( ! titleRecSizePtr ) { g_process.shutdownAbort(true); }
	}

	// remember, caller want us to set this
	m_titleRecPtrPtr  = titleRecPtrPtr;
	m_titleRecSizePtr = titleRecSizePtr;
	// assume not found. this can be NULL if justCheckTfndb is true,
	// like when it is called from XmlDoc::getIsNew()
	if ( titleRecPtrPtr  ) *titleRecPtrPtr  = NULL;
	if ( titleRecSizePtr ) *titleRecSizePtr = 0;

	// save callback
	m_state           = state;
	m_callback        = callback;

	// save it
	m_r = r;
	// set request
	r->m_docId           = docId;
	r->m_niceness        = niceness;
	r->m_justCheckTfndb  = justCheckTfndb;
	r->m_getAvailDocIdOnly   = getAvailDocIdOnly;
	r->m_collnum         = g_collectiondb.getCollnum ( coll );
	r->m_addToCache      = 0;
	r->m_maxCacheAge     = 0;
	// url must start with http(s)://. must be normalized.
	if ( url && url[0] != 'h' ) {
		log("msg22: BAD URL! does not start with 'h'");
		m_errno = g_errno = EBADENGINEER;
		return true;
	}
	// store url
	if ( url ) {
		strncpy(r->m_url, url, sizeof(r->m_url)-1);
		r->m_url[ sizeof(r->m_url)-1 ] = '\0';
	}
	else {
		r->m_url[0] = '\0';
	}

	// if no docid provided, use probable docid
	if ( ! docId ) {
		if( url ) {
			docId = Titledb::getProbableDocId ( url );
		}
		else {
			// Should never happen. Dump core if it does. Coverity 1361199
			logError("No URL and no docId!");
			gbshutdownLogicError();
		}
	}

	// get groupId from docId
	uint32_t shardNum = getShardNumFromDocId ( docId );

	// if niceness 0 can't pick noquery host.
	// if niceness 1 can't pick nospider host.
	Host *firstHost = g_hostdb.getLeastLoadedInShard ( shardNum, r->m_niceness );
	int32_t firstHostId = firstHost->m_hostId;

	m_outstanding = true;
	r->m_inUse    = true;

	// . send this request to the least-loaded host that can handle it
	// . returns false and sets g_errno on error
	// . use a pre-allocated buffer to hold the reply
	// . TMPBUFSIZE is how much a UdpSlot can hold w/o allocating
	if (!m_mcast.send((char *)r, r->getSize(), msg_type_22, false, shardNum, false, 0, this, NULL, gotReplyWrapper22, timeout * 1000, r->m_niceness, firstHostId, false)) {
		log("db: Requesting title record had error: %s.",
		    mstrerror(g_errno) );
		// set m_errno
		m_errno = g_errno;
		// no, multicast will free since he owns it!
		//if (replyBuf) mfree ( replyBuf , replyBufMaxSize , "Msg22" );
		return true;	
	}
	// otherwise, we blocked and gotReplyWrapper will be called
	return false;
}

void Msg22::gotReplyWrapper22(void *state1, void *state2) {
	Msg22 *THIS = static_cast<Msg22*>(state1);
	THIS->gotReply();
}

void Msg22::gotReply ( ) {
	// save g_errno
	m_errno = g_errno;
	// back
	m_outstanding = false;
	m_r->m_inUse    = false;

	// bail on error, multicast will free the reply buffer if it should
	if ( g_errno ) {
		if ( m_r->m_url[0] )
			log("db: Had error getting title record for %s : %s.",
			    m_r->m_url,mstrerror(g_errno));
		else
			log("db: Had error getting title record for docId of "
			    "%" PRId64": %s.",m_r->m_docId,mstrerror(g_errno));
		// free reply buf right away
		m_mcast.reset();
		m_callback ( m_state );
		return;
	}

	// get the reply
	int32_t  replySize = -1 ;
	int32_t  maxSize   ;
	bool  freeIt    ;
	char *reply     = m_mcast.getBestReply (&replySize, &maxSize, &freeIt);
	relabel( reply, maxSize, "Msg22-mcastGBR" );

	// a NULL reply happens when not found at one host and the other host
	// is dead... we need to fix Multicast to return a g_errno for this
	if ( ! reply ) {
		// set g_errno for callback
		m_errno = g_errno = EBADENGINEER;
		log("db: Had problem getting title record. Reply is empty.");
		m_callback ( m_state );
		return;
	}		

	// if replySize is only 8 bytes that means a not found
	if ( replySize == 8 ) {
		// we did not find it
		m_found = false;
		// get docid provided
		int64_t d = *(int64_t *)reply;
		// this is -1 or 0 if none available
		m_availDocId = d;
		// nuke the reply
		mfree ( reply , maxSize , "Msg22");
		// store error code
		m_errno = ENOTFOUND;

		// this is having problems in Msg23::gotTitleRec()
		m_callback ( m_state );
		return;
	}

	// sanity check. must either be an empty reply indicating nothing
	// available or an 8 byte reply above!
	if ( m_r->m_getAvailDocIdOnly ) { g_process.shutdownAbort(true); }

	// otherwise, it was found
	m_found = true;

	// if just checking tfndb, do not set this, reply will be empty!
	if ( ! m_r->m_justCheckTfndb ) {
		*m_titleRecPtrPtr  = reply;
		*m_titleRecSizePtr = replySize;
	}
	// if they don't want the title rec, nuke it!
	else {
		// nuke the reply
		mfree ( reply , maxSize , "Msg22");
	}

	// all done
	m_callback ( m_state );
}


class State22 {
public:
	UdpSlot   *m_slot;
	int64_t  m_pd;
	int64_t  m_docId1;
	int64_t  m_docId2;
	RdbList    m_tlist;
	Msg5       m_msg5;
	int64_t  m_availDocId;
	int64_t  m_uh48;
	class Msg22Request *m_r;
	// free slot request here too
	char *m_slotReadBuf;
	int32_t  m_slotAllocSize;

	State22() {
		m_slot = NULL;
		m_pd = 0;
		m_docId1 = 0;
		m_docId2 = 0;
		m_availDocId = 0;
		m_uh48 = 0;
		m_r = NULL;
		m_slotReadBuf = NULL;
		m_slotAllocSize = 0;
	}

	~State22() {
		if ( m_slotReadBuf )
			mfree(m_slotReadBuf,m_slotAllocSize,"st22");
		m_slotReadBuf = NULL;
	}
		
};

static void gotTitleList ( void *state , RdbList *list , Msg5 *msg5 ) ;

void handleRequest22 ( UdpSlot *slot , int32_t netnice ) {
	// get the request
	Msg22Request *r = (Msg22Request *)slot->m_readBuf;

	// sanity check
	int32_t  requestSize = slot->m_readBufSize;
	if ( requestSize < r->getMinSize() ) {
		log(LOG_WARN, "db: Got bad request size of %" PRId32" bytes for title record. "
		    "Need at least 28.",  requestSize );
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *tbase = getRdbBase( RDB_TITLEDB, r->m_collnum );
	if ( ! tbase ) {
		log(LOG_WARN, "db: Could not get title rec in collection # %" PRId32" because rdbbase is null.", (int32_t)r->m_collnum);
		g_errno = EBADENGINEER;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , g_errno );
		return; 
	}

	// overwrite what is in there so niceness conversion algo works
	r->m_niceness = netnice;

	// if just checking tfndb, do not do the cache lookup in clusterdb
	if ( r->m_justCheckTfndb ) {
		r->m_maxCacheAge = 0;
	}

	g_titledb.getRdb()->readRequestGet  (requestSize);

	// sanity check
	if ( r->m_collnum < 0 ) { g_process.shutdownAbort(true); }


	// make the state now
	State22 *st ;
	try { st = new (State22); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log(LOG_WARN, "query: Msg22: new(%" PRId32"): %s", (int32_t)sizeof(State22),
		mstrerror(g_errno));
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	mnew ( st , sizeof(State22) , "Msg22" );

	// store ptr to the msg22request
	st->m_r = r;
	// save for sending back reply
	st->m_slot = slot;

	// then tell slot not to free it since m_r references it!
	// so we'll have to free it when we destroy State22
	st->m_slotAllocSize = slot->m_readBufMaxSize;
	st->m_slotReadBuf   = slot->m_readBuf;
	slot->m_readBuf = NULL;

	// . if docId was explicitly specified...
	// . we may get multiple tfndb recs
	if ( ! r->m_url[0] ) {
	   st->m_docId1 = r->m_docId;
	   st->m_docId2 = r->m_docId;
	}

	// but if we are requesting an available docid, it might be taken
	// so try the range
	if ( r->m_getAvailDocIdOnly ) {
	   int64_t pd = r->m_docId;
	   int64_t d1 = Titledb::getFirstProbableDocId ( pd );
	   int64_t d2 = Titledb::getLastProbableDocId  ( pd );
	   // sanity - bad url with bad subdomain?
	   if ( pd < d1 || pd > d2 ) { g_process.shutdownAbort(true); }
	   // make sure we get a decent sample in titledb then in
	   // case the docid we wanted is not available
	   st->m_docId1 = d1;
	   st->m_docId2 = d2;
	}

	// . otherwise, url was given, like from Msg15
	// . we may get multiple tfndb recs
	if ( r->m_url[0] ) {
	   int32_t  dlen = 0;
	   // this causes ip based urls to be inconsistent with the call
	   // to getProbableDocId(url) below
	   const char *dom  = getDomFast ( r->m_url , &dlen );
	   // bogus url?
	   if ( ! dom ) {
	       log(LOG_WARN, "msg22: got bad url in request: %s from "
		   "hostid %" PRId32" for msg22 call ",
		   r->m_url,slot->m_host->m_hostId);
	       g_errno = EBADURL;
	       log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
	       g_udpServer.sendErrorReply ( slot , g_errno );
	       mdelete ( st , sizeof(State22) , "Msg22" );
	       delete ( st );
	       return;
	   }
	   int64_t pd = Titledb::getProbableDocId (r->m_url,dom,dlen);
	   int64_t d1 = Titledb::getFirstProbableDocId ( pd );
	   int64_t d2 = Titledb::getLastProbableDocId  ( pd );
	   // sanity - bad url with bad subdomain?
	   if ( pd < d1 || pd > d2 ) { g_process.shutdownAbort(true); }
	   // store these
	   st->m_pd     = pd;
	   st->m_docId1 = d1;
	   st->m_docId2 = d2;
	   st->m_uh48   = hash64b ( r->m_url ) & 0x0000ffffffffffffLL;
	}

	// make the cacheKey ourself, since Msg5 would make the key wrong
	// since it would base it on startFileNum and numFiles
	key96_t cacheKey ; cacheKey.n1 = 0; cacheKey.n0 = r->m_docId;
	// make titledb keys
	key96_t startKey = Titledb::makeFirstKey ( st->m_docId1 );
	key96_t endKey   = Titledb::makeLastKey  ( st->m_docId2 );

	// . load the list of title recs from disk now
	// . our file range should be solid
	// . use 500 million for min recsizes to get all in range
	if ( ! st->m_msg5.getList ( RDB_TITLEDB       ,
				    r->m_collnum ,
				    &st->m_tlist      ,
				    startKey          , // startKey
				    endKey            , // endKey
				    500000000         , // minRecSizes
				    true              , // includeTree
				    0,//r->m_maxCacheAge  , // max cache age
				    0,//startFileNum      ,
				    -1                 , // numFiles
				    st , // state             ,
				    gotTitleList      ,
				    r->m_niceness     ,
				    true              , // do error correct?
				    &cacheKey         ,
				    0                 , // retry num
				    -1                , // maxRetries
				    -1LL,               // sync point
				    false,              // isRealMerge
				    true))              // allowPageCache
		return ;

	// we did not block, nice... in cache?
	gotTitleList ( st , NULL , NULL );
}

void gotTitleList ( void *state , RdbList *list , Msg5 *msg5 ) {

	State22 *st = (State22 *)state;
	// shortcut
	Msg22Request *r = st->m_r;

	// send error reply on error
	if ( g_errno ) { 
	hadError:
		log(LOG_WARN, "db: Had error getting title record from titledb: %s.",
		    mstrerror(g_errno));
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( st->m_slot , g_errno );
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st ); 
		return ;
	}

	// convenience var
	RdbList *tlist = &st->m_tlist;

	// set probable docid
	int64_t pd = 0LL;
	if ( r->m_url[0] ) {
		pd = Titledb::getProbableDocId(r->m_url);
		if ( pd != st->m_pd ) { 
			log("db: crap probable docids do not match! u=%s",
			    r->m_url);
			g_errno = EBADENGINEER;
			goto hadError;
		}
	}

	// the probable docid is the PREFERRED docid in this case
	if ( r->m_getAvailDocIdOnly ) pd = st->m_r->m_docId;

	// . these are both meant to be available docids
	// . if ad2 gets exhausted we use ad1
	int64_t ad1 = st->m_docId1;
	int64_t ad2 = pd;

	bool docIdWasFound = false;

	// scan the titleRecs in the list
	for ( ; ! tlist->isExhausted() ; tlist->skipCurrentRecord ( ) ) {
		// get the rec
		char *rec     = tlist->getCurrentRec();
		int32_t  recSize = tlist->getCurrentRecSize();
		// get that key
		key96_t *k = (key96_t *)rec;
		// skip negative recs, first one should not be negative however
		if ( ( k->n0 & 0x01 ) == 0x00 ) continue;

		// get docid of that titlerec
		int64_t dd = Titledb::getDocId(k);

		if ( r->m_getAvailDocIdOnly ) {
			// make sure our available docids are availble!
			if ( dd == ad1 ) ad1++;
			if ( dd == ad2 ) ad2++;
			continue;
		}
		// if we had a url make sure uh48 matches
		else if ( r->m_url[0] ) {
			// get it
			int64_t uh48 = Titledb::getUrlHash48(k);

			// make sure our available docids are availble!
			if ( dd == ad1 ) ad1++;
			if ( dd == ad2 ) ad2++;
			// we must match this exactly
			if ( uh48 != st->m_uh48 ) continue;
		}
		// otherwise, check docid
		else {
			// compare that
			if ( r->m_docId != dd ) continue;
		}

		// flag that we matched m_docId
		docIdWasFound = true;

		// ok, if just "checking tfndb" no need to go further
		if ( r->m_justCheckTfndb ) {
			// send back a good reply (empty means found!)
			g_udpServer.sendReply(NULL,0,NULL,0,st->m_slot);
			// don't forget to free the state
			mdelete ( st , sizeof(State22) , "Msg22" );
			delete ( st );
			return;
		}

		// use rec as reply
		char *reply = rec;

		// . send this rec back, it's a match
		// . if only one rec in list, steal the list's memory
		if ( recSize != tlist->getAllocSize() ) {
			// otherwise, alloc space for the reply
			reply = (char *)mmalloc (recSize, "Msg22");
			if ( ! reply ) goto hadError;
			gbmemcpy ( reply , rec , recSize );
		}
		// otherwise we send back the whole list!
		else {
			// we stole this from list
			tlist->setOwnData(false);
		}
		// off ya go
		g_udpServer.sendReply(reply,recSize,reply,recSize,st->m_slot);
		// don't forget to free the state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		// all done
		return;
	}

	// maybe no available docid if we breached our range
	if ( ad1 >= pd           ) ad1 = 0LL;
	if ( ad2 >  st->m_docId2 ) ad2 = 0LL;
	// get best
	int64_t ad = ad2;
	// but wrap around if we need to
	if ( ad == 0LL ) ad = ad1;

	// remember it. this might be zero if none exist!
	st->m_availDocId = ad;
	// note it
	if ( ad == 0LL && (r->m_getAvailDocIdOnly || r->m_url[0]) ) 
		log("msg22: avail docid is 0 for pd=%" PRId64"!",pd);

	// . ok, return an available docid
	if ( r->m_url[0] || r->m_justCheckTfndb || r->m_getAvailDocIdOnly ) {
		// store docid in reply
		char *p = st->m_slot->m_tmpBuf;
		// send back the available docid
		*(int64_t *)p = st->m_availDocId;
		// send it
		g_udpServer.sendReply (p, 8, p, 8, st->m_slot);
		// don't forget to free state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		return;
	}

	// not found! and it was a docid based request...
	log("msg22: could not find title rec for docid %" PRIu64" collnum=%" PRId32,
	    r->m_docId,(int32_t)r->m_collnum);
	g_errno = ENOTFOUND;
	goto hadError;
}
