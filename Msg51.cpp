// TODO: if the first 20 or so do NOT have the same hostname, then stop
// and set all clusterRecs to CR_OK

#include "Msg51.h"

#include "gb-include.h"

#include "Clusterdb.h"
#include "Stats.h"
#include "HashTableT.h"
#include "HashTableX.h"
#include "RdbCache.h"
#include "ScopedLock.h"
#include "Sanity.h"
#include "Titledb.h"
#include "Collectiondb.h"


// how many Msg0 requests can we launch at the same time?
#define MSG51_MAX_REQUESTS 60

static const int signature_init = 0xe1d3c5b7;

// . these must be 1-1 with the enums above
// . used for titling the counts of g_stats.m_filterStats[]
const char * const g_crStrings[] = {
	"cluster rec not found"  ,  // 0
	"uninitialized"          ,
	"got clusterdb record"   ,
	"has adult bit"          ,
	"has wrong language"     ,
	"clustered"              ,
	"malformed url"          ,
	"banned url"             ,
	"empty title and summary",
	"summary error"          ,
	"duplicate"              ,
	"clusterdb error (subcount of visible)" ,
        "duplicate url",
	"wasted summary lookup"  ,
	"visible"                ,
	"blacklisted"            ,
	"ruleset filtered"       ,
	"end -- do not use"      
};

RdbCache s_clusterdbQuickCache;
static bool     s_cacheInit = false;


Msg51::Msg51() : m_slot(NULL), m_numSlots(0)
{
	m_clusterRecs     = NULL;
	m_clusterLevels   = NULL;
	pthread_mutex_init(&m_mtx,NULL);
	set_signature();
	reset();
}

Msg51::~Msg51 ( ) {
	reset();
	clear_signature();
}

void Msg51::reset ( ) {
	m_clusterRecs     = NULL;
	m_clusterLevels   = NULL;
	m_numSlots = 0;
	if( m_slot ) {
		delete[] m_slot;
		m_slot = NULL;
	}

	// Coverity
	m_docIds = NULL;
	m_numDocIds = 0;
	m_callback = NULL;
	m_state = NULL;
	m_nexti = 0;
	m_numRequests = 0;
	m_numReplies = 0;
	m_errno = 0;
	m_niceness = 0;
	m_collnum = 0;
	m_maxCacheAge = 0;
	m_addToCache = false;
	m_isDebug = false;
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg51::getClusterRecs ( const int64_t     *docIds,
			     char          *clusterLevels            ,
			     key96_t         *clusterRecs              ,
			     int32_t           numDocIds                ,
			     collnum_t collnum ,
			     int32_t           maxCacheAge              ,
			     bool           addToCache               ,
			     void          *state                    ,
			     void        (* callback)( void *state ) ,
			     int32_t           niceness                 ,
			     // output
			     bool           isDebug                  )
{
	verify_signature();
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: NULL collection. msg51.");

	// reset this msg
	reset();

	// get the collection rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) {
		log("db: msg51. Collection rec null for collnum %" PRId32".",
		    (int32_t)collnum);
		g_errno = EBADENGINEER;
		gbshutdownLogicError();
	}
	// keep a pointer for the caller
	m_maxCacheAge   = maxCacheAge;
	m_addToCache    = addToCache;
	m_state         = state;
	m_callback      = callback;
	m_collnum = collnum;
	// these are storage for the requester
	m_docIds        = docIds;
	m_clusterLevels = clusterLevels;
	m_clusterRecs   = clusterRecs;
	m_numDocIds     = numDocIds;
	m_isDebug       = isDebug;

	// bail if none to do
	if ( m_numDocIds <= 0 ) return true;

	m_nexti      = 0;
	// for i/o mostly
	m_niceness   = niceness;
	m_errno      = 0;

	// reset these
	m_numRequests = 0;
	m_numReplies  = 0;
	// clear/initialize these
	if(m_numDocIds<MSG51_MAX_REQUESTS)
		m_numSlots = m_numDocIds;
	else
		m_numSlots = MSG51_MAX_REQUESTS;
	m_slot = new Slot[m_numSlots];
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		m_slot[i].m_msg51 = this;
		m_slot[i].m_inUse = false;
	}
	// . do gathering
	// . returns false if blocked, true otherwise
	// . send up to MSG51_MAX_REQUESTS requests at the same time
	return sendRequests ( -1 );
}


bool Msg51::sendRequests(int32_t k) {
	verify_signature();
	ScopedLock sl(m_mtx);
	return sendRequests_unlocked(k);
}

// . returns false if blocked, true otherwise
// . sets g_errno on error (and m_errno)
// . k is a hint of which msg0 to use
// . if k is -1 we do a complete scan to find available m_msg0[x]
bool Msg51::sendRequests_unlocked(int32_t k) {
	verify_signature();

	bool anyAsyncRequests = false;
 sendLoop:

	// bail if no slots available
	if ( m_numRequests - m_numReplies >= m_numSlots ) return false;

	// any requests left to send?
	if ( m_nexti >= m_numDocIds ) {
		if ( anyAsyncRequests ) //we started an async request
			return false;
		if ( m_numRequests > m_numReplies ) //still waiting for replies
			return false;
		// we are done!
		return true;
	}

	// sanity check
	if ( m_clusterLevels[m_nexti] <  0      ||
	     m_clusterLevels[m_nexti] >= CR_END   ) {
		gbshutdownLogicError(); }

	// skip if we already got the rec for this guy!
	if ( m_clusterLevels[m_nexti] != CR_UNINIT ) {
		m_nexti++;
		goto sendLoop;
	}

	// . check our quick local cache to see if we got it
	// . use a max age of 1 hour
	// . this cache is primarly meant to avoid repetetive lookups
	//   when going to the next tier in Msg3a and re-requesting cluster
	//   recs for the same docids we did a second ago
	RdbCache *c = &s_clusterdbQuickCache;
	if ( ! s_cacheInit ) c = NULL;
	int32_t      crecSize;
	char     *crecPtr = NULL;
	key96_t     ckey = (key96_t)m_docIds[m_nexti];
	if ( c ) {
		RdbCacheLock rcl(*c);
		bool found = c->getRecord ( m_collnum    ,
				       ckey      , // cache key
				       &crecPtr  , // pointer to it
				       &crecSize ,
				       false     , // do copy?
				       3600      , // max age in secs
				       true      , // inc counts?
				       NULL      );// cachedTime
		if ( found ) {
			// sanity check
			if ( crecSize != sizeof(key96_t) ) gbshutdownLogicError();
			m_clusterRecs[m_nexti] = *(key96_t *)crecPtr;
			// it is no longer CR_UNINIT, we got the rec now
			m_clusterLevels[m_nexti] = CR_GOT_REC;
			// debug msg
			//logf(LOG_DEBUG,"query: msg51 getRec k.n0=%" PRIu64" rec.n0=%" PRIu64,
			//     ckey.n0,m_clusterRecs[m_nexti].n0);
			m_nexti++;
			goto sendLoop;
		}
	}

	// . do not hog all the udpserver's slots!
	// . must have at least one outstanding reply so we can process
	//   his reply and come back here...
	if ( g_udpServer.getNumUsedSlots() > 1000 &&
	     m_numRequests > m_numReplies ) return false;

	// find empty slot
	int32_t slot ;

	// ignore bogus hints
	if ( k >= m_numSlots ) k = -1;

	// if hint was provided use that
	if ( k >= 0 && ! m_slot[k].m_inUse )
		slot = k;
	// otherwise, do a scan for the empty slot
	else {
		for ( slot = 0 ; slot < m_numSlots ; slot++ )
			// break out if available
			if(!m_slot[slot].m_inUse)
				break;
	}

	// sanity check -- must have one!!
	if ( slot >= m_numSlots ) gbshutdownLogicError();

	// send it, returns false if blocked, true otherwise
	if( !sendRequest(slot) )
		anyAsyncRequests = true;

	// update any hint to make our loop more efficient
	if ( k >= 0 ) k++;
	
	goto sendLoop;
}

// . send using m_msg0s[i] class
bool Msg51::sendRequest ( int32_t    i ) {
	// what is the docid?
	int64_t  d;
	// point to where we want the last 64 bits of the cluster rec
	// to be store, "dataPtr"
	void    *dataPtr = NULL;

	// save it
	int32_t ci = m_nexti;
	// store where the cluster rec will go
	dataPtr = (void *)(PTRTYPE)ci;
	// what's the docid?
	d = m_docIds[m_nexti];
	// advance so we do not do this docid again 
	m_nexti++;

	m_slot[i].m_ci = ci;
	m_slot[i].m_inUse = true;
	// count it
	m_numRequests++;
	// lookup in clusterdb, need a start and endkey
	key96_t startKey = Clusterdb::makeFirstClusterRecKey ( d );
	key96_t endKey   = Clusterdb::makeLastClusterRecKey  ( d );
	
	// bias clusterdb lookups (from Msg22.cpp)
//	int32_t           numTwins     = g_hostdb.getNumHostsPerShard();
//	int64_t      sectionWidth = (DOCID_MASK/(int64_t)numTwins) + 1;
//	int32_t           hostNum      = (d & DOCID_MASK) / sectionWidth;
//	int32_t           numHosts     = g_hostdb.getNumHostsPerShard();
//	uint32_t  shardNum     = getShardNum(RDB_CLUSTERDB,&startKey);
//	Host          *hosts        = g_hostdb.getShard ( shardNum );
//	if ( hostNum >= numHosts ) gbshutdownLogicError();
//	int32_t firstHostId = hosts [ hostNum ].m_hostId ;

	// if we are doing a full split, keep it local, going across the net
	// is too slow!
	//if ( g_conf.m_fullSplit ) firstHostId = -1;
	int32_t firstHostId = -1;
	
	// . send the request for the cluster rec, use Msg0
	// . returns false and sets g_errno on error
	// . otherwise, it blocks and returns true
	bool s = m_slot[i].m_msg0.getList( -1            , // hostid
				     -1            , // ip
				     -1            , // port 
				     m_maxCacheAge ,
				     m_addToCache  ,
				     RDB_CLUSTERDB ,
				     m_collnum        ,
				     &m_slot[i].m_list,
				     (char *)&startKey      ,
				     (char *)&endKey        ,
				     36            , // minRecSizes 
				     &m_slot[i],     // state
				     gotClusterRecWrapper51  ,
				     m_niceness    ,
				     true        , // doErrorCorrection
				     true        , // includeTree
				     true        , // doMerge?
				     firstHostId ,
				     0           , // startFileNum
				     -1          , // numFiles
				     30000       , // timeout
				     -1          , // syncPoint
				     &m_slot[i].m_msg5, // use for local reads
				     false       , // isRealMerge?
				     true        , // allow page cache?
				     false       , // force local indexdb?
				     false       , // noSplit?
				     -1          );// forceParitySplit

	// loop for more if blocked, slot #i is used, block it
	//if ( ! s ) { i++; continue; }
	if ( ! s ) { 
		// only wanted this for faster disk page cache hitting so make
		// sure it is not "double used" by another msg0
		//m_msg0[i].m_msg5 = NULL; 
		return false; 
	}
	// otherwise, process the response
	gotClusterRec ( &m_slot[i] );
	return true;
}

void Msg51::gotClusterRecWrapper51(void *state) {
	Slot *slot = static_cast<Slot*>(state);
	Msg51 *THIS = slot->m_msg51;
	verify_signature_at(THIS->signature);
	{
		ScopedLock sl(THIS->m_mtx);
		// process it
		THIS->gotClusterRec(slot);
		// get slot number for re-send on this slot
		int32_t    k = (int32_t)(slot-THIS->m_slot);
		// . if not all done, launch the next one
		// . this returns false if blocks, true otherwise
		if ( ! THIS->sendRequests_unlocked(k) ) return;
	}
	// we don't need to go on if we're not doing deduping
	THIS->m_callback ( THIS->m_state );
	return;
}

// . sets m_errno to g_errno if not already set
void Msg51::gotClusterRec(Slot *slot) {
	verify_signature();

	// count it
	m_numReplies++;

	// free up
	slot->m_inUse = false;

	RdbList *list = slot->m_msg0.m_list;

	// update m_errno if we had an error
	if ( ! m_errno ) m_errno = g_errno;

	if ( g_errno ) 
		// print error
		log(LOG_DEBUG,
		    "query: Had error getting cluster info got docId=d: "
		    "%s.",mstrerror(g_errno));

	// this doubles as a ptr to a cluster rec
	int32_t    ci = slot->m_ci;
	// get docid
	int64_t docId = m_docIds[ci];
	// assume error!
	m_clusterLevels[ci] = CR_ERROR_CLUSTERDB;

	// bail on error
	if ( g_errno || list->getListSize() < 12 ) {
		//log(LOG_DEBUG,
		//    "build: clusterdb rec for d=%" PRId64" dptr=%" PRIu32" "
		//     "not found. where is it?", docId, (int32_t)ci);
		g_errno = 0;
		return;
	}

	// . steal rec from this multicast
	// . point to cluster rec, a int32_t   
	key96_t *rec = &m_clusterRecs[ci];

	// store the cluster rec itself
	*rec = *(key96_t *)(list->getList());
	// debug note
	log(LOG_DEBUG,
	    "build: had clusterdb SUCCESS for d=%" PRId64" dptr=%" PRIu32" "
	    "rec.n1=%" PRIx32",%016" PRIx64" sitehash26=0x%" PRIx32".", (int64_t)docId, (int32_t)ci,
	    rec->n1,rec->n0,
	    Clusterdb::getSiteHash26((char *)rec));

	// check for docid mismatch
	int64_t docId2 = Clusterdb::getDocId ( rec );
	if ( docId != docId2 ) {
		logf(LOG_DEBUG,"query: docid mismatch in clusterdb.");
		return;
	}

	// it is legit, set to CR_OK
	m_clusterLevels[ci] = CR_OK;

	RdbCacheLock rcl(s_clusterdbQuickCache);
	// . init the quick cache
	if(!s_cacheInit &&
		s_clusterdbQuickCache.init(200*1024,         // maxMem
					   sizeof(key96_t),  // fixedDataSize (clusterdb rec)
					   false,            // support lists
					   10000,            // max recs
					   false,            // use half keys?
					   "clusterdbQuickCache" ,
					   false,            // load from disk?
					   sizeof(key96_t),  // cache key size
					   sizeof(key96_t))) // cache data size
		// only init once if successful
		s_cacheInit = true;
	// . add the record to our quick cache as a int64_t
	// . ignore any error
	if(s_cacheInit)
		s_clusterdbQuickCache.addRecord(m_collnum,
						(key96_t)docId, // docid is key
						(char *)rec,
						sizeof(key96_t), // recSize
						0);

	// clear it in case the cache set it, we don't care
	g_errno = 0;
}

// . cluster the docids based on the clusterRecs
// . returns false and sets g_errno on error
// . if maxDocIdsPerHostname is -1 do not do hostname clsutering
bool setClusterLevels ( const key96_t   *clusterRecs,
			const int64_t *docIds,
			int32_t       numRecs              ,
			int32_t       maxDocIdsPerHostname ,
			bool       doHostnameClustering ,
			bool       familyFilter         ,
			bool       isDebug              ,
			// output to clusterLevels[]
			char    *clusterLevels        ) {
	
	if ( numRecs <= 0 ) return true;

	// skip if not clustering on anything
	//if ( ! doHostnameClustering && ! familyFilter ) {
	//	memset ( clusterLevels, CR_OK, numRecs );
	//	return true;
	//}

	HashTableX ctab;
	// init to 2*numRecs for speed. use 0 for niceness!
	if ( ! ctab.set ( 8 , 4 , numRecs * 2,NULL,0,false,"clustertab" ) )
		return false;

	// time it
	u_int64_t startTime = gettimeofdayInMilliseconds();

	for(int32_t i=0; i<numRecs; i++) {
		char *crec = (char *)&clusterRecs[i];
		// . set this cluster level
		// . right now will be CR_ERROR_CLUSTERDB or CR_OK...
		char *level = &clusterLevels[i];

		// sanity check
		if ( *level == CR_UNINIT ) gbshutdownLogicError();
		// and the adult bit, for cleaning the results
		if ( familyFilter && Clusterdb::hasAdultContent ( crec ) ) {
			*level = CR_DIRTY;
			continue;
		}
		// if error looking up in clusterdb, use a 8 bit domainhash from docid
		bool fakeIt = (*level==CR_ERROR_CLUSTERDB);
		// assume ok, show it, it is visible
		*level = CR_OK;
		// site hash comes next
		if(!doHostnameClustering)
			continue;

		// . get the site hash
		// . these are only 32 bits!
		int64_t h;
		if(fakeIt)
			h = Titledb::getDomHash8FromDocId(docIds[i]);
		else
			h = Clusterdb::getSiteHash26 ( crec );

		// inc this count!
		if ( fakeIt ) {
			g_stats.m_filterStats[CR_ERROR_CLUSTERDB]++;
		}

		// if it matches a siteid on our black list
		//if ( checkNegative && sht.getSlot((int64_t)h) > 0 ) {
		//	*level = CR_BLACKLISTED_SITE; goto loop; }
		// look it up
		uint32_t score = ctab.getScore(h) ;
		// if still visible, just continue
		if ( score < (uint32_t)maxDocIdsPerHostname ) {
			if ( ! ctab.addTerm(h))
				return false;
			continue;
		}
		// otherwise, no lonegr visible
		*level = CR_CLUSTERED;
	}


	// debug
	for ( int32_t i = 0 ; i < numRecs && isDebug ; i++ ) {
		char *crec = (char *)&clusterRecs[i];
		uint32_t siteHash26 = Clusterdb::getSiteHash26(crec);
		logf(LOG_DEBUG,"query: msg51: hit #%" PRId32") sitehash26=%" PRIu32" "
		     "rec.n0=%" PRIx64" docid=%" PRId64" cl=%" PRId32" (%s)",
		     (int32_t)i,
		     (int32_t)siteHash26,
		     clusterRecs[i].n0,
		     (int64_t)docIds[i],
		     (int32_t)clusterLevels[i],
		     g_crStrings[(int32_t)clusterLevels[i]] );
	}


	//log(LOG_DEBUG,"build: numVisible=%" PRId32" numClustered=%" PRId32" numErrors=%" PRId32,
	//    *numVisible,*numClustered,*numErrors);
	// show time
	uint64_t took = gettimeofdayInMilliseconds() - startTime;
	if ( took > 3 )
		log(LOG_INFO,"build: Took %" PRId64" ms to do clustering.",took);

	// we are all done
	return true;
}
