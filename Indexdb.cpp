#include "gb-include.h"

#include "Indexdb.h"
#include "Url.h"
#include "Clusterdb.h"
#include "Threads.h"

// a global class extern'd in .h file
Indexdb g_indexdb;

// for rebuilding indexdb
Indexdb g_indexdb2;

// resets rdb
void Indexdb::reset() { 
	m_rdb.reset();
}

bool Indexdb::init ( ) {
	// fake it for now
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Indexdb::init2 ( int32_t treeMem ) {
	//if ( ! setGroupIdTable () ) return false;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize     = (sizeof(key_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = treeMem  / nodeSize ;
	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want indexdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "indexdbRebuild"            ,
			    true                        , // dedup same keys?
			    0                           , // fixed data size
			    200                         , // min files to merge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    true                        , // use half keys?
			    false                       , // indexdbSaveCache
			    NULL                      ) ) // s_pc
		return false;
	return true;
}

bool Indexdb::verify ( char *coll ) {
	return true;
}

void Indexdb::deepVerify ( char *coll ) {
	log ( LOG_INFO, "db: Deep Verifying Indexdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	
	collnum_t collnum = g_collectiondb.getCollnum(coll);
	RdbBase *rdbBase = g_indexdb.m_rdb.getBase(collnum);
	int32_t numFiles = rdbBase->getNumFiles();
	int32_t currentFile = 0;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
deepLoop:
	// done after scanning all files
	if ( currentFile >= numFiles ) {
		g_threads.enableThreads();
		log ( LOG_INFO, "db: Finished deep verify for %"INT32" files.",
				numFiles );
		return;
	}
	// scan this file
	if ( ! msg5.getList ( RDB_INDEXDB   ,
			      cr->m_collnum ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      currentFile   , // startFileNum  ,
			      1             , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      false         )) {
		g_threads.enableThreads();
		log("db: HEY! it did not block");
		return;
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_INDEXDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_INDEXDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		BigFile *f = rdbBase->getFile(currentFile);
		log ("db: File %s: Out of first %"INT32" records in indexdb, "
		     "only %"INT32" belong to our group.",
		     f->getFilename(),count,got );
	}
	//else
	//	log ( LOG_INFO, "db: File %"INT32": Indexdb passed verification "
	//	      "successfully for %"INT32" recs.",currentFile,count );
	// next file
	currentFile++;
	goto deepLoop;
}

// . see Indexdb.h for format of the 12 byte key
// . TODO: substitute var ptrs if you want extra speed
key_t Indexdb::makeKey ( int64_t          termId   , 
			 unsigned char      score    , 
			 uint64_t docId    , 
			 bool               isDelKey ) {
	// make sure we mask out the hi bits we do not use first
	termId = termId & TERMID_MASK;
	key_t key ;
	char *kp = (char *)&key;
	char *tp = (char *)&termId;
	char *dp = (char *)&docId;
	// store termid
	*(int16_t *)(kp+10) = *(int16_t *)(tp+4);
	*(int32_t  *)(kp+ 6) = *(int32_t  *)(tp  );
	// store the complement of the score
	kp[5] = ~score;
	// . store docid
	// . make room for del bit and half bit
	docId <<= 2;
	*(int32_t *)(kp+1) = *(int32_t *)(dp+1);
	kp[0] = dp[0];
	// turn off half bit
	kp[0] &= 0xfd;
	// turn on/off delbit
	if ( isDelKey ) kp[0] &= 0xfe;
	else            kp[0] |= 0x01;
	// key is complete
	return key;
}

// . accesses RdbMap to estimate size of the indexList for this termId
// . returns an UPPER BOUND
int64_t Indexdb::getTermFreq ( collnum_t collnum , int64_t termId ) {
	// establish the list boundary keys
	key_t startKey = makeStartKey ( termId );
	key_t endKey   = makeEndKey   ( termId );
	// . ask rdb for an upper bound on this list size
	// . but actually, it will be somewhat of an estimate 'cuz of RdbTree
	key_t maxKey;
	// divide by 6 since indexdb's recs are 6 bytes each, except for first
	int64_t maxRecs;
	// . don't count more than these many in the map
	// . that's our old truncation limit, the new stuff isn't as dense
	int32_t oldTrunc = 100000;
	// get maxKey for only the top "oldTruncLimit" docids because when
	// we increase the trunc limit we screw up our extrapolation! BIG TIME!
	maxRecs=m_rdb.getListSize(collnum,startKey,endKey,&maxKey,oldTrunc )/6;
	// . TRUNCATION NOW OBSOLETE
	return maxRecs;
}

// keys are stored from lowest to highest
key_t Indexdb::makeStartKey ( int64_t termId ) {
	return makeKey ( termId , 255/*score*/ , 
			 0x0000000000000000LL/*docId*/ , true/*delKey?*/ );
}
key_t Indexdb::makeEndKey   ( int64_t termId ) {
	return makeKey ( termId , 0/*score*/ , 
			 0xffffffffffffffffLL/*docId*/ , false/*delKey?*/ );
}
