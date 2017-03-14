#include "gb-include.h"

#include "Msg5.h"
#include "RdbBase.h"
#include "Rdb.h"
#include "Stats.h"
#include "JobScheduler.h"
#include "Msg0.h"
#include "PingServer.h"
#include "Process.h"
#include "ip.h"
#include "Sanity.h"
#include "Conf.h"
#include "Mem.h"


//#define GBSANITYCHECK


int32_t g_numCorrupt = 0;

Msg5::Msg5() {
	m_waitingForList = false;
	//m_waitingForMerge = false;
	m_numListPtrs = 0;

	// Coverity
	m_list = NULL;
	memset(m_startKey, 0, sizeof(m_startKey));
	memset(m_endKey, 0, sizeof(m_endKey));
	m_callback = NULL;
	m_state = NULL;
	m_calledCallback = 0;
	m_includeTree = false;
	m_maxCacheAge = 0;
	m_numFiles = 0;
	m_startFileNum = 0;
	m_minRecSizes = 0;
	m_rdbId = RDB_NONE;
	m_newMinRecSizes = 0;
	m_round = 0;
	m_totalSize = 0;
	m_readAbsolutelyNothing = false;
	m_niceness = 0;
	m_doErrorCorrection = false;
	m_hadCorruption = false;
	m_msg0 = NULL;
	m_startTime = 0;
	memset(m_listPtrs, 0, sizeof(m_listPtrs));
	m_removeNegRecs = false;
	m_oldListSize = 0;
	m_maxRetries = 0;
	m_isRealMerge = false;
	m_ks = 0;
	m_allowPageCache = false;
	m_collnum = 0;
	m_errno = 0;
	// PVS-Studio
	memset(m_fileStartKey, 0, sizeof(m_fileStartKey));
	memset(m_minEndKey, 0, sizeof(m_minEndKey));

	reset();
}



Msg5::~Msg5() {
	reset();
}

// frees m_treeList
void Msg5::reset() {
	if ( m_waitingForList ) { // || m_waitingForMerge ) {
		log("disk: Trying to reset a class waiting for a reply.");
		// might being doing an urgent exit (mainShutdown(1)) or
		// g_process.shutdown(), so do not core here
		//g_process.shutdownAbort(true); 
	}
	m_msg3.reset();
	KEYMIN(m_prevKey,MAX_KEY_BYTES);// m_ks); m_ks is invalid
	m_numListPtrs = 0;
	// and the tree list
	m_treeList.freeList();
}


bool Msg5::getTreeList(RdbList *result, rdbid_t rdbId, collnum_t collnum, const void *startKey, const void *endKey) {
	m_rdbId = rdbId;
	m_collnum = collnum;
	m_newMinRecSizes = -1;
	int32_t dummy1,dummy2,dummy3,dummy4;
	return getTreeList(result, startKey, endKey, &dummy1, &dummy2, &dummy3, &dummy4);
}

bool Msg5::getTreeList(RdbList *result, const void *startKey, const void *endKey, int32_t *numPositiveRecs, 
	int32_t *numNegativeRecs, int32_t *memUsedByTree, int32_t *numUsedNodes) {
	RdbBase *base = getRdbBase(m_rdbId, m_collnum);
	if(!base) {
		log(LOG_WARN,"%s:%s:%d: base %d/%d unknown", __FILE__, __func__, __LINE__, m_rdbId, m_collnum);
		return false;
	}
	Rdb *rdb = getRdbFromId(m_rdbId);
	// set start time
	int64_t start = gettimeofdayInMilliseconds();
	// . returns false on error and sets g_errno
	// . endKey of m_treeList may be less than m_endKey
	const char *structName;

	if(rdb->useTree()) {
		// get the mem tree for this rdb
		RdbTree *tree = rdb->getTree();

		if( !tree ) {
			log(LOG_WARN,"%s:%s:%d: No tree!", __FILE__, __func__, __LINE__);
			return false;
		}

		if(!tree->getList(base->getCollnum(),
				  static_cast<const char*>(startKey),
				  static_cast<const char*>(endKey),
				  m_newMinRecSizes,
				  result,
				  numPositiveRecs,
				  numNegativeRecs,
				  base->useHalfKeys() ) )
			return true;
		structName = "tree";
		*memUsedByTree = tree->getMemOccupiedForList();
		*numUsedNodes = tree->getNumUsedNodes();
	} else {
		RdbBuckets *buckets = rdb->getBuckets();

		if( !buckets ) {
			log(LOG_WARN,"%s:%s:%d: No buckets!", __FILE__, __func__, __LINE__);
			return false;
		}

		if(!buckets->getList(base->getCollnum(),
				     static_cast<const char*>(startKey),
				     static_cast<const char*>(endKey),
				     m_newMinRecSizes,
				     result,
				     numPositiveRecs,
				     numNegativeRecs,
				     base->useHalfKeys()))
			return true;
		structName = "buckets";
	}

	int64_t now  = gettimeofdayInMilliseconds();
	int64_t took = now - start;
	if(took > 9)
		logf(LOG_INFO,"net: Got list from %s "
		     "in %" PRIu64" ms. size=%" PRId32" db=%s "
		     "niceness=%" PRId32".",
		     structName, took,m_treeList.getListSize(),
		     base->getDbName(),m_niceness);

	return true;
}



// . return false if blocked, true otherwise
// . set g_errno on error
// . fills "list" with the requested list
// . we want at least "minRecSizes" bytes of records, but not much more
// . we want all our records to have keys in the [startKey,endKey] range
// . final merged list should try to have a size of at least "minRecSizes"
// . if may fall short if not enough records were in [startKey,endKey] range
// . endKey of list will be set so that all records from startKey to that
//   endKey are in the list
// . a minRecSizes of 0x7fffffff means virtual inifinty, but it also has 
//   another special meaning. it tells msg5 to tell RdbTree's getList() to 
//   pre-allocate the list size by counting the recs ahead of time.
bool Msg5::getList ( rdbid_t     rdbId,
		     collnum_t collnum ,
		     RdbList *list          ,
		     const void    *startKey_      ,
		     const void    *endKey_        ,
		     int32_t     minRecSizes   , // requested scan size(-1 none)
		     bool     includeTree   ,
		     int32_t     maxCacheAge   , // in secs for cache lookup
		     int32_t     startFileNum  , // first file to scan
		     int32_t     numFiles      , // rel. to startFileNum,-1 all
		     void    *state         , // for callback
		     void   (* callback ) ( void    *state ,
					    RdbList *list  ,
					    Msg5    *msg5  ) ,
		     int32_t     niceness      ,
		     bool     doErrorCorrection ,
		     char    *cacheKeyPtr   , // NULL if none
		     int32_t     retryNum      ,
		     int32_t     maxRetries    ,
		     int64_t syncPoint ,
		     bool        isRealMerge ,
		     bool        allowPageCache ) {
	const char *startKey = static_cast<const char*>(startKey_);
	const char *endKey = static_cast<const char*>(endKey_);

	char fixedEndKey[MAX_KEY_BYTES];
	
	// make sure we are not being re-used prematurely
	if ( m_waitingForList ) {
		log(LOG_LOGIC,"disk: Trying to reset a class waiting for a reply.");
		g_process.shutdownAbort(true); 
	}

	if ( collnum < 0 ) {
		log(LOG_WARN,"msg5: called with bad collnum=%" PRId32,(int32_t)collnum);
		g_errno = ENOCOLLREC;
		return true;
	}

	// assume no error
	g_errno = 0;

	// sanity
	if ( ! list ) gbshutdownLogicError();

	// . reset the provided list
	// . this will not free any mem it may have alloc'd but it will set
	//   m_listSize to 0 so list->isEmpty() will return true
	list->reset();

	// key size set
	m_ks = getKeySizeFromRdbId(rdbId);

	// . complain if endKey < startKey
	// . no because IndexReadInfo does this to prevent us from reading
	//   a list
	if ( KEYCMP(startKey,endKey,m_ks)>0 ) return true;
	// log("Msg5::readList: startKey > endKey warning"); 

	// we no longer allow negative minRecSizes
	if ( minRecSizes < 0 ) {
		if ( g_conf.m_logDebugDb )
		      log(LOG_LOGIC,"net: msg5: MinRecSizes < 0, using 2GB.");
		minRecSizes = 0x7fffffff;
		//g_process.shutdownAbort(true);
	}

	// ensure startKey last bit clear, endKey last bit set
	if ( !KEYNEG(startKey) )
		log(LOG_REMIND,"net: msg5: StartKey lastbit set."); 

	// fix endkey
	if ( KEYNEG(endKey) ) {
		log(LOG_REMIND,"net: msg5: EndKey lastbit clear. Fixing.");
		//Previously it was fixed by setting the LSB in the endKey
		//input parameter. Code review showed that it should only
		//happend when called from doInject in main.cpp due to a bug.
		//Still, it could happen due to damaged network packets.
		KEYSET(fixedEndKey,endKey,m_ks);
		fixedEndKey[0] |= 0x01;
		endKey = fixedEndKey;
	}

	// remember stuff
	m_rdbId         = rdbId;
	m_collnum          = collnum;

	// why was this here? it was messing up the statsdb ("graph") link
	// in the admin panel.
	//CollectionRec *ttt = g_collectiondb.getRec ( m_collnum );
	//if ( ! ttt ) {
	//	g_errno = ENOCOLLREC;
	//	return true;
	//}

	m_list          = list;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);
	m_minRecSizes   = minRecSizes;
	m_includeTree   = includeTree;
	m_maxCacheAge   = maxCacheAge;
	m_startFileNum  = startFileNum;
	m_numFiles      = numFiles;
	m_state         = state;
	m_callback      = callback;
	m_calledCallback= 0;
	m_niceness      = niceness;
	m_maxRetries    = maxRetries;
	m_oldListSize   = 0;
	m_isRealMerge        = isRealMerge;
	m_allowPageCache     = allowPageCache;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase( m_rdbId, m_collnum );
	if ( ! base ) {
		return true;
	}

	// . these 2 vars are used for error correction
	// . doRemoteLookup is -2 if it's up to us to decide
	m_doErrorCorrection = doErrorCorrection;

	// these get changed both by cache and gotList()
	m_newMinRecSizes = minRecSizes;
	m_round          = 0;
	m_readAbsolutelyNothing = false;

	KEYSET(m_fileStartKey,m_startKey,m_ks);

#ifdef GBSANITYCHECK
	log("msg5: sk=%s", KEYSTR(m_startKey,m_ks));
	log("msg5: ek=%s", KEYSTR(m_endKey,m_ks));
#endif

	// hack it down
	if ( numFiles > base->getNumFiles() ) 
		numFiles = base->getNumFiles();

	// . make sure we set base above so Msg0.cpp:268 doesn't freak out
	// . if startKey is > endKey list is empty
	if ( KEYCMP(m_startKey,m_endKey,m_ks)>0 ) return true;
	// same if minRecSizes is 0
	if ( m_minRecSizes == 0    ) return true;

	// timing debug
	//log("Msg5:getting list startKey.n1=%" PRIu32,m_startKey.n1);
	// start the read loop - hopefully, will only loop once
	if ( readList ( ) ) {
		return true;
	}

	// tell Spider.cpp not to nuke us until we get back!!!
	m_waitingForList = true;

	// we blocked!!! must call m_callback
	return false;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . reads from cache, tree and files
// . calls gotList() to do the merge if we need to
// . loops until m_minRecSizes is satisfied OR m_endKey is reached
bool Msg5::readList ( ) {
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	Rdb *rdb = getRdbFromId(m_rdbId);
	RdbBase *base = getRdbBase( m_rdbId, m_collnum );
	if ( ! base ) {
		return true;
	}

	do { //until no more read is needed
	
		// . reset our tree list
		// . sets fixedDataSize here in case m_includeTree is false because
		//   we don't want merge to have incompatible lists
		m_treeList.reset();
		m_treeList.setFixedDataSize ( base->getFixedDataSize() );
		m_treeList.setKeySize(m_ks);
		// reset Msg3 in case gotList() is called without calling
		// Msg3::readList() first
		m_msg3.reset();

		// assume lists have no errors in them
		m_hadCorruption = false;

		// . restrict tree's endkey by calling msg3 now...
		// . this saves us from spending 1000ms to read 100k of negative
		//   spiderdb recs from the tree only to have most of the for naught
		// . this call will ONLY set m_msg3.m_endKey
		// . but only do this if dealing with spiderdb really
		// . also now for tfndb, since we scan that in RdbDump.cpp to dedup
		//   the spiderdb list we are dumping to disk. it is really for any
		//   time when the endKey is unbounded, so check that now
		const char *treeEndKey = m_endKey;
		bool compute = true;

		if ( ! m_includeTree           ) compute = false;

		// if endKey is "unbounded" then bound it...
		char max[MAX_KEY_BYTES]; KEYMAX(max,m_ks);
		if ( KEYCMP(m_endKey,max,m_ks) != 0 ) compute = false;

		// BUT don't bother if a small list, probably faster just to get it
		if ( m_newMinRecSizes < 1024   ) compute = false;

		// try to make merge read threads higher priority than
		// regular spider read threads
		int32_t niceness = m_niceness;
		if ( niceness > 0  ) niceness = 2;
		if ( m_isRealMerge ) niceness = 1;

		if ( compute ) {
			m_msg3.readList  ( m_rdbId          ,
					   m_collnum        ,
					   m_fileStartKey   , // modified by gotList()
					   m_endKey         ,
					   m_newMinRecSizes , // modified by gotList()
					   m_startFileNum   ,
					   m_numFiles       ,
					   NULL             , // state
					   NULL             , // callback
					   niceness         ,
					   0                , // retry num
					   m_maxRetries     , // -1=def
					   true);             // just get endKey?
			if ( g_errno ) {
				log(LOG_ERROR,"db: Msg5: getting endKey: %s",mstrerror(g_errno));
				return true;
			}
			treeEndKey = m_msg3.m_constrainKey;
		}

		// . get the list from our tree
		// . set g_errno and return true on error
		// . it is crucial that we get tree list before spawning a thread
		//   because Msg22 will assume that if the TitleRec is in the tree
		//   now we'll get it, because we need to have the latest version
		//   of a particular document and this guarantees it. Otherwise, if
		//   the doc is not in the tree then tfndb must tell its file number.
		//   I just don't want to think its in the tree then have it get
		//   dumped out right before we read it, then we end up getting the
		//   older version rather than the new one in the tree which tfndb
		//   does not know about until it is dumped out. so we could lose
		//   recs between the in-memory and on-disk phases this way.
		// . however, if we are getting a titlerec, we first read the tfndb
		//   list from the tree then disk. if the merge replaces the tfndb rec
		//   we want with another while we are reading the tfndb list from
		//   disk, then the tfndb rec we got from the tree was overwritten!
		//   so then we'd read from the wrong title file number (tfn) and
		//   not find the rec because the merge just removed it. so keeping
		//   the tfndb recs truly in sync with the titledb recs requires
		//   some dancing. the simplest way is to just scan all titleRecs
		//   in the event of a disagreement... so turn on m_scanAllIfNotFound,
		//   which essentially disregards tfndb and searches all the titledb
		//   files for the titleRec.
		if ( m_includeTree ) {
			int32_t numNegativeRecs = 0;
			int32_t numPositiveRecs = 0;
			int32_t memUsedByTree = 0;
			int32_t numRecs = 0;
			if(!getTreeList(&m_treeList, m_fileStartKey, treeEndKey, &numPositiveRecs, &numNegativeRecs, &memUsedByTree, &numRecs)) {
				return true;
			}

			// if our recSize is fixed we can boost m_minRecSizes to
			// compensate for these deletes when we call m_msg3.readList()
			int32_t rs = base->getRecSize() ;
			// . use an avg. rec size for variable-length records
			// . just use tree to estimate avg. rec size
			if ( rs == -1) {
				if(rdb->useTree()) {
					// get avg record size
					if ( numRecs > 0 ) rs = memUsedByTree / numRecs;
					// add 10% for deviations
					rs = (rs * 110) / 100;
					// what is the minimal record size?
					int32_t minrs     = sizeof(key96_t) + 4;
					// ensure a minimal record size
					if ( rs < minrs ) rs = minrs;
				}
				else {
					RdbBuckets *buckets = rdb->getBuckets();
					if( !buckets ) {
						log(LOG_WARN,"%s:%s:%d: No buckets!", __FILE__, __func__, __LINE__);
						return false;
					}

					rs = buckets->getNumKeys() / buckets->getMemOccupied();
					int32_t minrs = buckets->getRecSize() + 4;
					// ensure a minimal record size
					if ( rs < minrs ) rs = minrs;
				}
			}

			// . TODO: get avg recSize in this rdb (avgRecSize*numNeg..)
			// . don't do this if we're not merging because it makes
			//   it harder to compute the # of bytes to read used to
			//   pre-allocate a reply buf for Msg0 when !m_doMerge
			// . we set endKey for spiderdb when reading from tree above
			//   based on the current minRecSizes so do not mess with it
			//   in that case.
			if ( m_rdbId != RDB_SPIDERDB ) {
				//m_newMinRecSizes += rs * numNegativeRecs;
				int32_t nn = m_newMinRecSizes + rs * numNegativeRecs;
				if ( rs > 0 && nn < m_newMinRecSizes ) nn = 0x7fffffff;
				m_newMinRecSizes = nn;
			}

			// . if m_endKey = m_startKey + 1 and our list has a rec
			//   then no need to check the disk, it was in the tree
			// . it could be a negative or positive record
			// . tree can contain both negative/positive recs for the key
			//   so we should do the final merge in gotList()
			// . that can happen because we don't do an annihilation
			//   because the positive key may be being dumped out to disk
			//   but it really wasn't and we get stuck with it
			char kk[MAX_KEY_BYTES];
			KEYSET(kk,m_startKey,m_ks);
			KEYINC(kk,m_ks);
			if ( KEYCMP(m_endKey,kk,m_ks)==0 && ! m_treeList.isEmpty() ) {
				return gotList();
			}
		}
		// if we don't use the tree then at least set the key bounds cuz we
		// pick the min endKey between diskList and treeList below
		else {
			m_treeList.set ( m_fileStartKey , m_endKey );
		}

		// . if we're reading indexlists from 2 or more sources then some
		//   will probably be compressed from 12 byte keys to 6 byte keys
		// . it is typically only about 1% when files are small,
		//   and smaller than that when a file is large
		// . but just to be save reading an extra 2% won't hurt too much
		if ( base->useHalfKeys() ) {
			int32_t numSources = m_numFiles;
			if ( numSources == -1 )
				numSources = base->getNumFiles();
			// if tree is empty, don't count it
			if ( m_includeTree && ! m_treeList.isEmpty() ) numSources++;
			// . if we don't do a merge then we return the list directly
			//   (see condition where m_numListPtrs == 1 below)
			//   from Msg3 (or tree) and we must hit minRecSizes as
			//   close as possible for Msg3's call to constrain() so
			//   we don't overflow the UdpSlot's TMPBUFSIZE buffer
			// . if we just arbitrarily boost m_newMinRecSizes then
			//   the single list we get back from Msg3 will not have
			//   been constrained with m_minRecSizes, but constrained
			//   with m_newMinRecSizes (x2%) and be too big for our UdpSlot
			if ( numSources >= 2 ) {
				int64_t newmin = (int64_t)m_newMinRecSizes ;
				newmin = (newmin * 50LL) / 49LL ;
				// watch out for wrap around
				if ( (int32_t)newmin < m_newMinRecSizes )
					m_newMinRecSizes = 0x7fffffff;
				else    m_newMinRecSizes = (int32_t)newmin;
			}
		}

		// limit to 20MB so we don't go OOM!
		if ( m_newMinRecSizes > 2 * m_minRecSizes &&
		     m_newMinRecSizes > 20000000 )
			m_newMinRecSizes = 20000000;


		const char *diskEndKey = m_treeList.getEndKey();
		// sanity check
		if ( m_treeList.getKeySize() != m_ks ) { g_process.shutdownAbort(true); }

		// we are waiting for the list
		//m_waitingForList = true;

		// clear just in case
		g_errno = 0;

		// . now get from disk
		// . use the cache-modified constraints to reduce reading time
		// . return false if it blocked
		if ( ! m_msg3.readList  ( m_rdbId          ,
					  m_collnum        ,
					  m_fileStartKey   , // modified by gotList()
					  diskEndKey       ,
					  m_newMinRecSizes , // modified by gotList()
					  m_startFileNum   ,
					  m_numFiles       ,
					  m_callback ? this : NULL,
					  m_callback ? &gotListWrapper0 : NULL,
					  niceness         ,
					  0                , // retry num
					  m_maxRetries     , // max retries (-1=def)
					  false))
			return false;
		// . this returns false if blocked, true otherwise
		// . sets g_errno on error
		// . updates m_newMinRecSizes
		// . updates m_fileStartKey to the endKey of m_list + 1
		if ( ! gotList () ) return false;
		// bail on error from gotList() or Msg3::readList()
		if ( g_errno ) return true;

		// we may need to re-call getList
	} while(needsRecall());
	// we did not block
	return true;
}



bool Msg5::needsRecall() {
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	Rdb *rdb = getRdbFromId(m_rdbId);
	RdbBase *base = getRdbBase ( m_rdbId , m_collnum );

	// if collection was deleted from under us, base will be NULL
	if ( ! base && ! g_errno ) {
		log(LOG_WARN,"msg5: base lost for rdbid=%" PRId32" collnum %" PRId32,
		    (int32_t)m_rdbId,(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return false;
	}

	bool rc = true;

	// . return true if we're done reading
	// . sometimes we'll need to read more because Msg3 will shorten the
	//   endKey to better meat m_minRecSizes but because of 
	//   positive/negative record annihilation on variable-length
	//   records it won't read enough
	if( g_errno || m_newMinRecSizes <= 0 ) {
		rc = false;
	}

	// limit to just doledb for now in case it results in data loss
	if( rc && m_readAbsolutelyNothing && (m_rdbId==RDB_DOLEDB||m_rdbId==RDB_SPIDERDB) ) {
		rc = false;
	}

	// seems to be ok, let's open it up to fix this bug where we try
	// to read too many bytes a small titledb and it does an infinite loop
	if( rc && m_readAbsolutelyNothing ) {
		log(LOG_WARN, "rdb: read absolutely nothing more for dbname=%s on cn=%" PRId32, base->getDbName(),(int32_t)m_collnum);
		rc = false;
	}

	if( rc && KEYCMP(m_list->getEndKey(), m_endKey, m_ks ) >= 0 ) {
		rc = false;
	}

	if( rc ) {
		// this is kinda important. we have to know if we are abusing
		// the disk... we should really keep stats on this...
		bool logIt = true;
		if( m_round > 100 && (m_round % 1000) != 0 ) {
			logIt = false;
		}

		// seems very common when doing rebalancing then merging to have
		// to do at least one round of re-reading, so note that
		if( m_round == 0 ) {
			logIt = false;
		}

		// so common for doledb because of key annihilations
		if( m_rdbId == RDB_DOLEDB && m_round < 10 ) {
			logIt = false;
		}

		if ( logIt ) {
			log(LOG_WARN,"db: Reading %" PRId32" again from %s (need %" PRId32" total "
			     "got %" PRId32" totalListSizes=%" PRId32" sk=%s) "
			     "cn=%" PRId32" this=0x%" PTRFMT" round=%" PRId32".", 
			     m_newMinRecSizes , base->getDbName() , m_minRecSizes, 
			     m_list->getListSize(),
			     m_totalSize,
			     KEYSTR(m_startKey,m_ks),
			     (int32_t)m_collnum,(PTRTYPE)this, m_round );
		}

		m_round++;
		// record how many screw ups we had so we know if it hurts performance
		rdb->didReSeek();
		
		// return true will try to read more from disk
	}
	else {
		if( m_list ) {
			m_list->resetListPtr();
		}
		// return false cuz we don't need a recall
	}

	return rc;
}



void Msg5::gotListWrapper0(void *state) {
	Msg5 *that = static_cast<Msg5*>(state);
	that->gotListWrapper();
}

void Msg5::gotListWrapper() {
	// . this sets g_errno on error
	// . this will merge cache/tree and disk lists into m_list
	// . it will update m_newMinRecSizes
	// . it will also update m_fileStartKey to the endKey of m_list + 1
	// . returns false if it blocks
	if ( ! gotList ( ) ) return;
	// . throw it back into the loop if necessary
	// . only returns true if COMPLETELY done
	if ( needsRecall() && ! readList() ) return;
	// sanity check
	if ( m_calledCallback ) { g_process.shutdownAbort(true); }
	// set it now
	m_calledCallback = 1;
	// we are no longer waiting for the list
	m_waitingForList = false;
	// when completely done call the callback
	m_callback ( m_state, m_list, this );
}



// . this is the NEW gotList() !!! mdw
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg5::gotList ( ) {

	// we are no longer waiting for the list
	//m_waitingForList = false;

	// return if g_errno is set
	if ( g_errno && g_errno != ECORRUPTDATA ) return true;

	return gotList2();
}


// . this is the NEW gotList() !!! mdw
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg5::gotList2 ( ) {
	// reset this
	m_startTime = 0LL;
	// return if g_errno is set
	if ( g_errno && g_errno != ECORRUPTDATA ) return true;
	// put all the lists in an array of list ptrs
	int32_t n = 0;
	// all the disk lists
	for ( int32_t i = 0 ; n < MAX_RDB_FILES && i < m_msg3.getNumLists(); ++i ) {
		RdbList *list = m_msg3.getList(i);
		if(list==NULL) gbshutdownLogicError();
		// . skip list if empty
		// . was this causing problems?
		if ( ! m_isRealMerge ) {
			if ( list->isEmpty() ) continue;
		}
		m_listPtrs [ n++ ] = list;
	}

	// sanity check.
	if ( m_msg3.getNumLists() > MAX_RDB_FILES ) 
		log(LOG_LOGIC,"db: Msg3 had more than %" PRId32" lists.",
		    (int32_t)MAX_RDB_FILES);

	// . get smallest endKey from all the lists
	// . all lists from Msg3 should have the same endKey, but
	//   m_treeList::m_endKey may differ
	// . m_treeList::m_endKey should ALWAYS be >= that of the files
	// . constrain m_treeList to the startKey/endKey of the files
	//m_minEndKey = m_endKey;
	KEYSET(m_minEndKey,m_endKey,m_ks);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		//if ( m_listPtrs[i]->getEndKey() < m_minEndKey ) 
		//	m_minEndKey = m_listPtrs[i]->getEndKey();

		// sanity check
		//if ( KEYNEG(m_listPtrs[i]->getEndKey()) ) {
		//	g_process.shutdownAbort(true); }

		if ( KEYCMP(m_listPtrs[i]->getEndKey(),m_minEndKey,m_ks)<0 ) {
			KEYSET(m_minEndKey,m_listPtrs[i]->getEndKey(),m_ks);

			// crap, if list is all negative keys, then the
			// end key seems negative too! however in this
			// case RdbScan::m_endKey seems positive so
			// maybe we got a negative endkey in constrain?
			//if (! (m_minEndKey[0] & 0x01) )
			//	log("msg5: list had bad endkey");
		}
	}

	// sanity check
	//if ( KEYNEG( m_minEndKey) ) {g_process.shutdownAbort(true); }

	// . is treeList included?
	// . constrain treelist for the merge
	// . if used, m_listPtrs [ m_numListPtrs - 1 ] MUST equal &m_treeList
	//   since newer lists are listed last so their records override older
	if ( m_includeTree && ! m_treeList.isEmpty() ) {
		// only constrain if we are NOT the sole list because the
		// constrain routine sets our endKey to virtual infinity it
		// seems like and that makes SpiderCache think that spiderdb
		// is exhausted when it is only in the tree. so i added the
		// if ( n > 0 ) condition here.
		if ( n > 0 ) {
			char k[MAX_KEY_BYTES];
			m_treeList.getCurrentKey(k);
			m_treeList.constrain(m_startKey, m_minEndKey, -1, 0, k, m_rdbId, "tree");
		}
		m_listPtrs [ n++ ] = &m_treeList;
	}
	
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase( m_rdbId, m_collnum );
	if ( ! base ) {
		return true;
	}

	// if not enough lists, use a dummy list to trigger merge so tfndb
	// filter happens and we have a chance to weed out old titleRecs
	if ( m_rdbId == RDB_TITLEDB && m_numFiles != 1 && n == 1 && m_isRealMerge ) {
		//log(LOG_LOGIC,"db: Adding dummy list.");
		m_dummy.set ( NULL                      , // list data
			      0                         , // list data size
			      NULL                      , // alloc 
			      0                         , // alloc size
			      m_startKey                ,
			      m_minEndKey                  ,
			      base->getFixedDataSize() ,
			      true                      , // own data?
			      base->useHalfKeys()       ,
			      m_ks                      );
		m_listPtrs [ n++ ] = &m_dummy;
	}

	if ( n >= MAX_RDB_FILES ) {
		log( LOG_LOGIC, "net: msg5: Too many lists (%" PRId32" | %" PRId32").", m_msg3.getNumLists(), n );
	}

	// store # of lists here for use by the call to merge_r()
	m_numListPtrs = n;

	// count the sizes
	m_totalSize = 0;
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ ) {
		m_totalSize += m_listPtrs[ i ]->getListSize();
	}

	// . but don't breach minRecSizes
	// . this totalSize is just to see if we should spawn a thread, really
	//if ( totalSize > m_minRecSizes ) m_totalSize = m_minRecSizes;

#ifdef GBSANITYCHECK
	// who uses this now?
	//log("Msg5:: who is merging?????");
	// timing debug
	//    m_startKey.n1,
	//    gettimeofdayInMilliseconds()-m_startTime , 
	//    m_diskList.getListSize());
	// ensure both lists are legit
	// there may be negative keys in the tree
	// diskList may now also have negative recs since Msg3 no longer 
	// removes them for fears of delayed positive keys not finding their
	// negative key because it was merged out by RdbMerge
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ )
		m_listPtrs[i]->checkList_r(true);
#endif

	// . if no lists we're done
	// . if we were a recall, then list may not be empty
	if ( m_numListPtrs == 0 && m_list->isEmpty() ) {
		// just copy ptrs from this list into m_list
		m_list->set ( NULL                      , // list data
			      0                         , // list data size
			      NULL                      , // alloc 
			      0                         , // alloc size
			      m_startKey                ,
			      m_endKey                  ,
			      base->getFixedDataSize() ,
			      true                      , // own data?
			      base->useHalfKeys()       ,
			      m_ks                      );
		// . add m_list to our cache if we should
		// . this returns false if blocked, true otherwise
		// . sets g_errno on error
		// . only blocks if calls msg0 to patch a corrupted list
		// . it will handle calling callback if that happens
		return doneMerging();
	}

	if ( m_numListPtrs == 0 ) {
		m_readAbsolutelyNothing = true;
	}

	// if all lists from msg3 were 0... tree still might have something
	if ( m_totalSize == 0 && m_treeList.isEmpty() ) {
		m_readAbsolutelyNothing = true;
	}

	// if msg3 had corruption in a list which was detected in contrain_r()
	if ( g_errno == ECORRUPTDATA ) {
		// if we only had one list, we were not doing a merge
		// so return g_errno to the requested so he tries from the twin
		if ( m_numListPtrs == 1 ) {
			return true;
		}

		// assume nothing is wrong
		g_errno = 0;
		// if m_doErrorCorrection is true, repairLists() should fix
	}

	// . should we remove negative recs from final merged list?
	// . if we're reading from root and tmp merge file of root
	// . should we keep this out of the thread in case a file created?
	m_removeNegRecs = base->isRootFile(m_startFileNum);

	// . if we only have one list, just use it
	// . Msg3 should have called constrain() on it so it's m_list so
	//   m_listEnd and m_listSize all fit m_startKey/m_endKey/m_minRecSizes
	//   to a tee
	// . if it's a tree list it already fits to a tee
	// . same with cache list?? better be...
	// . if we're only reading one list it should always be empty right?
	// . i was getting negative keys in my RDB_DOLEDB list which caused
	//   Spider.cpp to core, so i add the "! m_removeNegRecs" constraint
	//   here... 
	// . TODO: add some code to just filter out the negative recs
	//   super quick just for this purpose
	// . crap, rather than do that just deal with the negative recs 
	//   in the caller code... in this case Spider.cpp::gotDoledbList2()
	if ( m_numListPtrs == 1 && m_list->isEmpty() &&
	     // just do this logic for doledb now, it was causing us to
	     // return search results whose keys were negative indexdb keys.
	     // or later we can just write some code to remove the neg
	     // recs from the single list!
	     ( m_rdbId == RDB_LINKDB || m_rdbId == RDB_DOLEDB ||
	     // this speeds up our queryloop querylog parsing in seo.cpp quite a bit
	     ( m_rdbId == RDB_POSDB && m_numFiles == 1 ) ) ) {
		// log any problems
		if ( m_listPtrs[0]->getOwnData() ) {
			// . bitch if not empty
			// . NO! might be our second time around if we had key
			//   annihilations between file #0 and the tree, and now
			//   we only have 1 non-empty list ptr, either from the tree
			//   or from the file
			//if ( ! m_list->isEmpty() ) 
			//	log("Msg5::gotList: why is it not empty? size=%" PRId32,
			//	    m_list->getListSize() );
			// just copy ptrs from this list into m_list
			m_list->set ( m_listPtrs[0]->getList          () ,
			              m_listPtrs[0]->getListSize      () ,
			              m_listPtrs[0]->getAlloc         () ,
			              m_listPtrs[0]->getAllocSize     () ,
			              m_listPtrs[0]->getStartKey      () ,
			              m_listPtrs[0]->getEndKey        () ,
			              m_listPtrs[0]->getFixedDataSize () ,
			              true                               , // own data?
			              m_listPtrs[0]->getUseHalfKeys   () ,
			              m_ks                               );
			// ensure we don't free it when we loop on freeLists() below
			m_listPtrs[0]->setOwnData ( false );

			// gotta set this too!
			if ( m_listPtrs[0]->isLastKeyValid() ) {
				m_list->setLastKey ( m_listPtrs[0]->getLastKey() );
			}

			// . remove titleRecs that shouldn't be there
			// . if the tfn of the file we read the titlerec from does not
			//   match the one in m_tfndbList, then remove it
			// . but if we're not merging lists, why remove it?
			//if ( m_rdbId == RDB_TITLEDB && m_msg3.m_numFileNums > 1 )
			//     stripTitleRecs ( m_list , m_tfns[0] , m_tfndbList );

			// . add m_list to our cache if we should
			// . this returns false if blocked, true otherwise
			// . sets g_errno on error
			// . only blocks if calls msg0 to patch a corrupted list
			// . it will handle calling callback if that happens
			return doneMerging();
		} else {
			log(LOG_LOGIC,"db: Msg5: list does not own data.");
		}
	}

	// time the perparation and merge
	m_startTime = gettimeofdayInMilliseconds();

	// . merge the lists 
	// . the startKey of the finalList is m_startKey, the first time
	// . but after that, we're adding diskLists, so us m_fileStartKey
	// . we're called multiple times for the same look-up in case of
	//   delete records in a variable rec-length db cause some recs in our
	//   disk lookups to be wiped out, thereby falling below minRecSizes
	// . this will set g_errno and return false on error (ENOMEM,...)
	// . older list goes first so newer list can override
	// . remove all negative-keyed recs since Msg5 is a high level msg call

	// . prepare for the merge, grows the buffer
	// . this returns false and sets g_errno on error
	// . should not affect the current list in m_list, only build on top
	if ( ! m_list->prepareForMerge ( m_listPtrs, m_numListPtrs, m_minRecSizes ) ) {
		log( LOG_WARN, "net: Had error preparing to merge lists from %s: %s", base->getDbName(),mstrerror(g_errno));
		return true;
	}

	if(m_callback) {
		if ( g_jobScheduler.submit(mergeListsWrapper, mergeDoneWrapper, this, m_isRealMerge ? thread_type_file_merge : thread_type_query_merge, m_niceness) ) {
			return false;
		}

		// thread creation failed
		if ( g_jobScheduler.are_new_jobs_allowed() )
			log(LOG_INFO,
			    "net: Failed to create thread to merge lists. Doing "
			    "blocking merge. (%s)",mstrerror(g_errno));
	}

	// clear g_errno because it really isn't a problem, we just block
	g_errno = 0;

	// repair any corruption
	repairLists();

	// do it
	mergeLists();

	// . add m_list to our cache if we should
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . only blocks if calls msg0 to patch a corrupted list
	// . it will handle calling callback if that happens
	return doneMerging();
}


// thread will run this first
void Msg5::mergeListsWrapper(void *state) {
	// we're in a thread now!
	Msg5 *that = static_cast<Msg5*>(state);

	// assume no error since we're at the start of thread call
	that->m_errno = 0;

	// repair any corruption
	that->repairLists();

	// do the merge
	that->mergeLists();

	if (g_errno && !that->m_errno) {
		that->m_errno = g_errno;
	}
}


// . now we're done merging
// . when the thread is done we get control back here, in the main process
// Use of ThreadEntry parameter is NOT thread safe
void Msg5::mergeDoneWrapper(void *state, job_exit_t exit_type) {
	Msg5 *that = static_cast<Msg5 *>(state);

	g_errno = that->m_errno;
	that->mergeDone(exit_type);
}

void Msg5::mergeDone(job_exit_t /*exit_type*/) {
	// we MAY be in a thread now

	// debug msg
	//log("msg3 back from merge thread (msg5=%" PRIu32")",THIS->m_state);

	// . add m_list to our cache if we should
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . only blocks if calls msg0 to patch a corrupted list
	// . it will handle calling callback if that happens
	if ( ! doneMerging() ) return;

	// . throw it back into the loop if necessary
	// . only returns true if COMPLETELY done
	if ( needsRecall() && ! readList() ) return;

	// sanity check
	if ( m_calledCallback ) { g_process.shutdownAbort(true); }

	// we are no longer waiting for the list
	m_waitingForList = false;

	// set it now
	m_calledCallback = 3;

	// when completely done call the callback
	m_callback ( m_state, m_list, this );
}

// check lists in the thread
void Msg5::repairLists() {
	// assume none
	m_hadCorruption = false;
	// return if no need to
	if ( ! m_doErrorCorrection ) return;
	// or if msg3 already check them and they were ok
	if ( m_msg3.isListChecked() ) return;
	// if msg3 said they were corrupt... this happens when the map
	// is generated over a bad data file and ends up writing the same key
	// on more than 500MB worth of data. so when we try to read a list
	// that has the startkey/endkey covering that key, the read size
	// is too big to ever happen...
	if ( m_msg3.listHadCorruption() ) m_hadCorruption = true;
	// time it
	//m_checkTime = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ ) {
		// . did it breech our minRecSizes?
		// . only check for indexdb, our keys are all size 12
		// . is this a mercenary problem?
		// . cored on 'twelfth night cake'
		// . no... this happens after merging the lists. if we had
		//   a bunch of negative recs we over read anticipating some
		//   recs will be deleted, so it isn't really necessary to
		//   bitch about this here..
		if ( g_conf.m_logDebugDb &&
		     m_rdbId == RDB_POSDB &&
		     m_listPtrs[i]->getListSize() > m_minRecSizes + 12 )
			// just log it for now, maybe force core later
			log(LOG_DEBUG,"db: Index list size is %" PRId32" but "
			    "minRecSizes is %" PRId32".",
			    m_listPtrs[i]->getListSize() ,
			    m_minRecSizes );

#ifdef GBSANITYCHECK
		// core dump on corruption
		bool status = m_listPtrs[i]->checkList_r(true);
#else
		// this took like 50ms (-O3) on lenny on a 4meg list
		bool status = m_listPtrs[i]->checkList_r(false);
#endif

		// if no errors, check the next list
		if ( status ) continue;
		// . show the culprit file
		// . logging the key ranges gives us an idea of how long
		//   it will take to patch the bad data
		int32_t nn = m_msg3.getFileNums();

		// TODO: fix this. can't call Collectiondb::getBase from within a thread!
		RdbBase *base = getRdbBase ( m_rdbId , m_collnum );
		if ( i < nn && base ) {
			BigFile *bf = base->getFile ( m_msg3.getFileNum(i) );
			log( LOG_WARN, "db: Corrupt filename is %s in collnum %" PRId32".", bf->getFilename(), (int32_t)m_collnum );
			log( LOG_WARN, "db: startKey=%s endKey=%s",
			     KEYSTR( m_listPtrs[i]->getStartKey(), m_ks ),
			     KEYSTR( m_listPtrs[i]->getEndKey(), m_ks ) );
		}

		// . remove the bad eggs from the list
		// . TODO: support non-fixed data sizes
		//if ( m_listPtrs[i]->getFixedDataSize() >= 0 )
		m_listPtrs[i]->removeBadData_r();
		//else
		//m_listPtrs[i]->reset();

		// otherwise we have a patchable error
		m_hadCorruption = true;
	}
}

void Msg5::mergeLists() {
	// . don't do any merge if this is true
	// . if our fetch of remote list fails, then we'll be called
	//   again with this set to false
	if ( m_hadCorruption ) return;

	// . if the key of the last key of the previous list we read from
	//   is not below startKey, reset the truncation count to avoid errors
	// . if caller does the same read over and over again then
	//   we would do a truncation in error eventually
	// . use m_fileStartKey, not just m_startKey, since we may be doing
	//   a follow-up read

	// . old Msg3 notes:
	// . otherwise, merge the lists together
	// . this may call growList() via RdbList::addRecord/Key() but it 
	//   shouldn't since we called RdbList::prepareForMerge() above
	// . we aren't allowed to do allocating in a thread!
	// . TODO: only merge the recs not cached, [m_fileStartKey, endKey]
	// . merge() might shrink m_endKey in diskList if m_minRecSizes
	//   contrained us OR it might decrement it by 1 if it's a negative key
	// .........................
	// . this MUST start at m_list->m_listPtr cuz this may not be the
	//   1st time we had to dive in to disk, due to negative rec
	//   annihilation
	// . old finalList.merge_r() Msg5 notes:
	// . use startKey of tree
	// . NOTE: tree may contains some un-annihilated key pairs because
	//   one of them was PROBABLY in the dump queue and we decided in
	//   Rdb::addRecord() NOT to do the annihilation, therefore it's good
	//   to do the merge to do the annihilation
	m_list->merge_r(m_listPtrs, m_numListPtrs, m_startKey, m_minEndKey, m_minRecSizes, m_removeNegRecs, m_rdbId, m_collnum, m_startFileNum);

	// maintain this info for truncation purposes
	if ( m_list->isLastKeyValid() ) 
		//m_prevKey = m_list->getLastKey();
		KEYSET(m_prevKey,m_list->getLastKey(),m_ks);
	else {
		// . lastKey should be set and valid if list is not empty
		// . we need it for de-duping dup tfndb recs that fall on our
		//   read boundaries
		if ( m_list->getListSize() > 0 )
			log(LOG_LOGIC,"db: Msg5. Last key invalid.");
	}
}


// . this returns false if blocked, true otherwise
// . sets g_errno on error
// . only blocks if calls msg0 to patch a corrupted list
// . it will handle calling callback if that happens
// . this is called when all files are done reading in m_msg3
// . sets g_errno on error
// . problem: say maxRecSizes is 1200 (1000 keys)
// . there are 10000 keys in the [startKey,endKey] range
// . we read 1st 1000 recs from the tree and store in m_treeList
// . we read 1st 1000 recs from disk 
// . all recs in tree are negative and annihilate the 1000 recs from disk
// . we are left with an empty list
bool Msg5::doneMerging ( ) {

	//m_waitingForMerge = false;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase( m_rdbId, m_collnum );
	if ( ! base ) {
		return true;
	}

	// . if there was a merge error, bitch about it
	// . Thread class should propagate g_errno when it was set in a thread
	if ( g_errno ) {
		log( LOG_WARN, "net: Had error merging lists from %s: %s.",
		    base->getDbName(),mstrerror(g_errno));
		return true;
	}

	// . was a list corrupted?
	// . if so, we did not even begin the merge yet
	// . try to get the list from a remote brother
	// . if that fails we have already removed the bad data, so begin
	//   our first merge
	if ( m_hadCorruption ) {
		// log it here, cuz logging in thread doesn't work too well
		log( LOG_WARN, "net: Encountered a corrupt list in rdb=%s collnum=%" PRId32,
		    base->getDbName(),(int32_t)m_collnum);
		// remove error condition, we removed the bad data in thread
		
		m_hadCorruption = false;

// 		if(g_numCorrupt++ >= g_conf.m_maxCorruptLists &&
// 		   g_conf.m_maxCorruptLists > 0) {
		g_numCorrupt++;
		if(g_conf.m_maxCorruptLists > 0 &&
		   (g_numCorrupt % g_conf.m_maxCorruptLists) == 0) {
			char msgbuf[1024];
			Host *h = g_hostdb.getHost ( 0 );
			snprintf(msgbuf, 1024,
				 "%" PRId32" corrupt lists. "
				 "cluster=%s "
				 "host=%" PRId32,
				 g_numCorrupt,
				 iptoa(h->m_ip),
				 g_hostdb.m_hostId);
			g_pingServer.sendEmail(NULL, msgbuf);
		}

		if(m_callback) {
			// try to get the list from remote host
			if ( ! getRemoteList() ) return false;
			// note that
			if ( ! g_errno ) {
				log("net: got remote list without blocking");
				g_process.shutdownAbort(true);
			}
		} else
			g_errno = ESHARDDOWN; //not allowed to get remote list (and return false).
		// if it set g_errno, it could not get a remote list
		// so try to make due with what we have
		if ( g_errno ) {
			// log a msg, we actually already removed it in thread
			log("net: Removed corrupted data.");
			// clear error
			g_errno = 0;
			// . merge the modified lists again
			// . this is not in a thread
			// . it should not block
			mergeLists();
		}
	}

	if ( m_isRealMerge )
		log(LOG_DEBUG,"db: merged list is %" PRId32" bytes long.",
		    m_list->getListSize());

	// log it
	int64_t now ;
	// only time it if we actually did a merge, check m_startTime
	if ( m_startTime ) now = gettimeofdayInMilliseconds();
	else               now = 0;
	int64_t took = now - m_startTime ;
	if ( g_conf.m_logTimingNet ) {
		if ( took > 5 )
			log(LOG_INFO,
			    "net: Took %" PRIu64" ms to do merge. %" PRId32" lists merged "
			     "into one list of %" PRId32" bytes.",
			     took , m_numListPtrs , m_list->getListSize() );
	}


	// . add the stat
	// . use turquoise for time to merge the disk lists
	// . we should use another color rather than turquoise
	// . these clog up the graph, so only log if took more than 1 ms
	// . only time it if we actually did a merge, check m_startTime
	if ( took > 1 && m_startTime ) {
		//"rdb_list_merge"
		g_stats.addStat_r( m_minRecSizes, m_startTime, now, 0x0000ffff );
	}

	// . scan merged list for problems
	// . this caught an incorrectly set m_list->m_lastKey before
#ifdef GBSANITYCHECK
	m_list->checkList_r(true, m_rdbId);
#endif

	// . TODO: call freeList() on each m_list[i] here rather than destructr
	// . free all lists we used
	// . some of these may be from Msg3, some from cache, some from tree
	for ( int32_t i = 0 ; i < m_numListPtrs ; i++ ) {
		m_listPtrs[i]->freeList();
		m_listPtrs[i] = NULL;
	}
	// and the tree list
	m_treeList.freeList();
	// . update our m_newMinRecSizes
	// . NOTE: this now ignores the negative records in the tree
	int64_t newListSize = m_list->getListSize();

	// scale proportionally based on how many got removed during the merge
	int64_t percent = 100LL;
	int64_t net = newListSize - m_oldListSize;
	// add 5% for inconsistencies
	if ( net > 0 ) percent =(((int64_t)m_newMinRecSizes*100LL)/net)+5LL;
	else           percent = 200;
	if ( percent <= 0 ) percent = 1;
	// set old list size in case we get called again
	m_oldListSize = newListSize;

	//int32_t delta = m_minRecSizes - (int32_t)newListSize;

	// how many recs do we have left to read?
	m_newMinRecSizes = m_minRecSizes - (int32_t)newListSize;
	
	// return now if we met our minRecSizes quota
	if ( m_newMinRecSizes <= 0 ) return true;

	// if we gained something this round then try to read the remainder
	//if ( net > 0 ) m_newMinRecSizes = delta;


	// otherwise, scale proportionately
	int32_t nn = ((int64_t)m_newMinRecSizes * percent ) / 100LL;
	if ( percent > 100 ) {
		if ( nn > m_newMinRecSizes ) m_newMinRecSizes = nn;
		else                         m_newMinRecSizes = 0x7fffffff;
	}
	else m_newMinRecSizes = nn;

	// . for every round we get call increase by 10 percent
	// . try to fix all those negative recs in the rebalance re-run
	m_newMinRecSizes *= (int32_t)(1.0 + (m_round * .10));

	// wrap around?
	if ( m_newMinRecSizes < 0 || m_newMinRecSizes > 1000000000 )
		m_newMinRecSizes = 1000000000;

	
	// . don't exceed original min rec sizes by 5 i guess
	// . watch out for wrap
	//int32_t max = 5 * m_minRecSizes ;
	//if ( max < m_minRecSizes ) max = 0x7fffffff;
	//if ( m_newMinRecSizes > max && max > m_minRecSizes )
	//	m_newMinRecSizes = max;

	// keep this above a certain point because if we didn't make it now
	// we got negative records messing with us
	if ( m_rdbId != RDB_DOLEDB &&
	     m_newMinRecSizes < 128000 ) m_newMinRecSizes = 128000;
	// . update startKey in case we need to read more
	// . we'll need to read more if endKey < m_endKey && m_newMinRecSizes 
	//   is positive
	// . we read more from files AND from tree
	//m_fileStartKey  = m_list->getEndKey() ;
	//m_fileStartKey += (uint32_t)1;
	KEYSET(m_fileStartKey,m_list->getEndKey(),m_ks);
	KEYINC(m_fileStartKey,m_ks);
	return true;
}



bool g_isDumpingRdbFromMain = 0;

// . if we discover one of the lists we read from a file is corrupt we go here
// . uses Msg5 to try to get list remotely
bool Msg5::getRemoteList ( ) {

	// skip this part if doing a cmd line 'gb dump p main 0 -1 1' cmd or
	// similar to dump out a local rdb.
	if ( g_isDumpingRdbFromMain ) {
		g_errno = 1;
		return true;
	}

	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . get list from ALL files, not just m_startFileNum/m_numFiles
	//   since our files may not be the same
	// . if doRemotely parm is not supplied replying hostId is unspecified
	// get our twin host, or a redundant host in our group
	//Host *group = g_hostdb.getGroup ( g_hostdb.m_groupId );
	Host *group = g_hostdb.getMyShard();
	int32_t  n     = g_hostdb.getNumHostsPerShard();
	// . if we only have 1 host per group, data is unpatchable
	// . we should not have been called if this is the case!!
	if ( n == 1 ) {
		g_errno = EBADENGINEER;
		//log("Msg5::gotRemoteList: no twins. data unpatchable.");
		return true;
	}
	if ( m_rdbId == RDB_STATSDB ) {
		g_errno = EBADENGINEER;
		log("net: Cannot patch statsdb data from twin because it is "
		    "not interchangable.");
		return true;
	}
	// tell them about
	log("net: Getting remote list from twin instead.");
	// make a new Msg0 for getting remote list
	try { m_msg0 = new ( Msg0 ); }
	// g_errno should be set if this is NULL
	catch ( ... ) {
		g_errno = ENOMEM;
		log("net: Could not allocate memory to get from twin.");
		return true;
	}
	mnew ( m_msg0 , sizeof(Msg0) , "Msg5" );
	// select our twin
	int32_t i;
	for ( i = 0 ; i < n ; i++ ) 
		if ( group[i].m_hostId != g_hostdb.m_hostId ) break;
	Host *h = &group[i];
	// get our groupnum. the column #
	int32_t forceParitySplit = h->m_shardNum;//group;
	// translate base to an id, for the sake of m_msg0
	//char rdbId = getIdFromRdb ( base->m_rdb );
	// . this returns false if blocked, true otherwise
	// . this sets g_errno on error
	// . get list from ALL files, not just m_startFileNum/m_numFiles
	//   since our files may not be the same
	// . if doRemotely parm is not supplied replying hostId is unspecified
	// . make minRecSizes as big as possible because it gets from ALL
	//   files and from tree!
	// . just make it 256k for now lest, msg0 bitch about it being too big
	//   if rdbId == RDB_INDEXDB passed the truncation limit
	// . wait forever for this host to reply... well, at least a day that
	//   way if he's dead we'll wait for him to come back up to save our
	//   data
	if ( ! m_msg0->getList ( h->m_hostId          ,
				 0                    , // max cached age
				 false                , // add to cache?
				 m_rdbId              , // rdbId
				 m_collnum            ,
				 m_list               ,
				 m_startKey           ,
				 m_endKey             ,
				 m_minRecSizes        , // was 256k minRecSizes
				 this                 ,
				 gotRemoteListWrapper ,
				 m_niceness           ,
				 false                , // do error correction?
				 true                 , // include tree?
				 -1                   , // first hostid
				 0                    , // startFileNum
				 -1                   , // numFiles (-1=all)
				 60*60*24*1000            , // timeout
				 -1                   , // syncPoint
				 NULL                 , // msg5
				 m_isRealMerge        , // merging files?
				 m_allowPageCache     , // allow page cache?
				 false                , // force local Indexdb
				 false                , // doIndexdbSplit
				 // "forceParitySplit" is a group # 
				 // (the groupId is a mask)
				 forceParitySplit     ))
		return false;
	// this is strange
	log("msg5: call to msg0 did not block");
	// . if we did not block then call this directly
	// . return false if it blocks
	return gotRemoteList ( ) ;
}

void Msg5::gotRemoteListWrapper( void *state ) {
	Msg5 *THIS = (Msg5 *)state;
	// return if this blocks
	if ( ! THIS->gotRemoteList() ) return;
	// sanity check
	if ( THIS->m_calledCallback ) { g_process.shutdownAbort(true); }
	// we are no longer waiting for the list
	THIS->m_waitingForList = false;
	// set it now
	THIS->m_calledCallback = 4;
	// if it doesn't block call the callback, g_errno may be set
	THIS->m_callback ( THIS->m_state , THIS->m_list , THIS );
}

// returns false if it blocks
bool Msg5::gotRemoteList ( ) {
	// free the Msg0
	mdelete ( m_msg0 , sizeof(Msg0) , "Msg5" );
	delete ( m_msg0 );
	// return true now if everything ok
	if ( ! g_errno ) {
		// . i modified checkList to set m_lastKey if it is not set
		// . we need it for the big merge for getting next key in
		//   RdbDump.cpp
		// . if it too is invalid, we are fucked
		if ( ! m_list->checkList_r ( false ) ) {
			log("net: Received bad list from twin.");
			g_errno = ECORRUPTDATA;
			goto badList;
		}
		// . success messages
		// . logging the key ranges gives us an idea of how long
		//   it will take to patch the bad data
		const char *sk = m_list->getStartKey();
		const char *ek = m_list->getEndKey  ();
		log("net: Received good list from twin. Requested %" PRId32" bytes "
		    "and got %" PRId32". "
		    "startKey=%s endKey=%s",
		    m_minRecSizes , m_list->getListSize() ,
		    KEYSTR(sk,m_ks),KEYSTR(ek,m_ks));
		// . HACK: fix it so end key is right
		// . TODO: fix this in Msg0::gotReply()
		// . if it is empty, then there must be nothing else left
		//   since the endKey was maxed in call to Msg0::getList()
		if ( ! m_list->isEmpty() )
			m_list->setEndKey ( m_list->getLastKey() );
		const char *k = m_list->getStartKey();
		log(LOG_DEBUG,
		    //"net: Received list skey.n1=%08" PRIx32" skey.n0=%016" PRIx64"." ,
		    //  k.n1 , k.n0 );
		    "net: Received list skey=%s." ,
		      KEYSTR(k,m_ks) );
		k = m_list->getEndKey();
		log(LOG_DEBUG,
		    //"net: Received list ekey.n1=%08" PRIx32" ekey.n0=%016" PRIx64"." ,
		    //  k.n1 , k.n0 );
		    "net: Received list ekey=%s",
		      KEYSTR(k,m_ks) );
		if ( ! m_list->isEmpty() ) {
			k = m_list->getLastKey();
			//log(LOG_DEBUG,"net: Received list Lkey.n1=%08" PRIx32" "
			//      "Lkey.n0=%016" PRIx64 , k.n1 , k.n0 );
			log(LOG_DEBUG,"net: Received list Lkey=%s",
			    KEYSTR(k,m_ks) );
		}
		//log("Msg5::gotRemoteList: received list is good.");
		return true;
	}

 badList:
	// it points to a corrupted list from twin, so reset
	m_list->reset();
	// because we passed m_list to Msg0, it called m_list->reset()
	// which set our m_mergeMinListSize to -1, so we have to call
	// the prepareForMerge() again
	if ( ! m_list->prepareForMerge ( m_listPtrs    , 
					 m_numListPtrs , 
					 m_minRecSizes ) ) {
		log("net: Had error preparing for merge: %s.",
		    mstrerror(g_errno));
		return true;
	}		
	// . if g_errno is timed out we couldn't get a patch list
	// . turn off error correction and try again
	log("net: Had error getting remote list: %s.", mstrerror(g_errno) );
	log("net: Merging repaired lists.");
	// clear g_errno so RdbMerge doesn't freak out
	g_errno = 0;
	// . we have the lists ready to merge
	// . hadCorruption should be false at this point
	mergeLists();
	// process the result
	return doneMerging();
}
