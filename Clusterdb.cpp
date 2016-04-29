#include "gb-include.h"

#include "Clusterdb.h"
#include "Rebalance.h"
#include "JobScheduler.h"

// a global class extern'd in .h file
Clusterdb g_clusterdb;
Clusterdb g_clusterdb2;

// reset rdb
void Clusterdb::reset() { m_rdb.reset(); }

// . this no longer maintains an rdb of cluster recs
// . Msg22 now just uses the cache to hold cluster recs that it computes
//   from titlteRecs
// . clusterRecs are now just TitleRec keys...
// . we can load one the same from titledb as we could from clusterdb
//   and we still don't need to uncompress the titleRec to get the info
bool Clusterdb::init ( ) {
	// this should be about 200/4 = 50 megs per host on my current setup
	int32_t maxTreeMem = g_conf.m_clusterdbMaxTreeMem;
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t maxTreeNodes  = maxTreeMem / ( 16 + CLUSTER_REC_SIZE );

	bool bias = false;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir  ,
			    "clusterdb"   ,
			    true          , // dedup
			    //CLUSTER_REC_SIZE - sizeof(key_t),//fixedDataSize 
			    0             , // no data now! just docid/s/c
			    2, // g_conf.m_clusterdbMinFilesToMerge,
			    g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , // maxTreeNodes  ,
			    true          , //false         , // balance tree?
			    0,//maxCacheMem   , 
			    0,//maxCacheNodes ,
			    true          , // half keys?
			    g_conf.m_clusterdbSaveCache,
			    NULL,//&m_pc ,
			    false,  // is titledb
			    true ,  // preload disk page cache
			    12,     // key size
			    bias ); // bias disk page cache?
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Clusterdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t maxTreeNodes  = treeMem / ( 16 + CLUSTER_REC_SIZE );
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir     ,
			    "clusterdbRebuild" ,
			    true          , // dedup
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       , // g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , 
			    true          , // balance tree?
			    0             , // maxCacheMem   , 
			    0             , // maxCacheNodes ,
			    true          , // half keys?
			    false         , // g_conf.m_clusterdbSaveCache,
			    NULL          , // &m_pc ,
			    false         ,  // is titledb
			    false         ,  // preload disk page cache
			    12            ,     // key size
			    true          ); // bias disk page cache
}

bool Clusterdb::verify ( char *coll ) {
	log ( LOG_DEBUG, "db: Verifying Clusterdb for coll %s...", coll );
	g_jobScheduler.disallow_new_jobs();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_CLUSTERDB ,
			      cr->m_collnum          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
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
			      true          )) {
		g_jobScheduler.allow_new_jobs();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		//uint32_t groupId = getGroupId ( RDB_CLUSTERDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_CLUSTERDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %"INT32" records in clusterdb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Clusterdb inconsistency." );
		g_jobScheduler.allow_new_jobs();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Clusterdb passed verification successfully for "
			"%"INT32" recs.", count );
	// DONE
	g_jobScheduler.allow_new_jobs();
	return true;
}

#include "IndexList.h"


// return the percent similar
char Clusterdb::getSampleSimilarity ( char *vec0 , char *vec1, int32_t size ) {
	// . we sorted them above as uint32_ts, so we must make sure
	//   we use uint32_ts here, too
	uint32_t *t0 = (uint32_t *)vec0;
	uint32_t *t1 = (uint32_t *)vec1;
	// if either is empty, return 0 to be on the safe side
	if ( *t0 == 0 ) return 0;
	if ( *t1 == 0 ) return 0;

	// count matches between the sample vectors
	int32_t count = 0;
 loop:
	if( ((char*)t0 - vec0) > size ) {
		log( LOG_INFO, "query: sample vector 0 is malformed. "
		     "Returning 0%% similarity." );
		return 0;
	}
	if( ((char*)t1 - vec1) > size ) {
		log( LOG_INFO, "query: sample vector 1 is malformed. "
		     "Returning 0%% similarity." );
		return 0;
	}

	// terminate on a 0
	if      ( *t0 < *t1 ) { if ( *++t0 == 0 ) goto done; }
	else if ( *t1 < *t0 ) { if ( *++t1 == 0 ) goto done; }
	else    { 
		// if both are zero... do not inc count
		if ( *t0 == 0 ) goto done;
		count++; 
		t0++;
		t1++;
		if ( *t0 == 0 ) goto done;
		if ( *t1 == 0 ) goto done;
	}
	goto loop;

 done:
	// count total components in each sample vector
	while ( *t0 ) {
		t0++;
		if( ((char*)t0 - vec0) > size ) {
			log( LOG_INFO, "query: sample vector 0 is malformed. "
			     "Returning 0%% similarity." );
			return 0;
		}
	}
	while ( *t1 ) {
		t1++;
		if( ((char*)t1 - vec1) > size ) {
			log( LOG_INFO, "query: sample vector 1 is malformed. "
			     "Returning 0%% similarity." );
			return 0;
		}
	}
	int32_t total = 0;
	total += t0 - ((uint32_t *)vec0);
	total += t1 - ((uint32_t *)vec1);
	// how similar are they?
	// if both are empty, assume not similar at all. this happens if we
	// do not have a content vector for either, or if both are small docs
	// with no words or links in them (framesets?)
	if ( total == 0 ) return 0;
	int32_t sim = (count * 2 * 100) / total;
	if ( sim > 100 ) sim = 100;
	return (char)sim;
}

key_t Clusterdb::makeClusterRecKey ( int64_t     docId,
				     bool          familyFilter,
				     uint8_t       languageBits,
				     int32_t          siteHash,
				     bool          isDelKey,
				     bool          isHalfKey ) {
	key_t key;
	// set the docId upper bits
	key.n1 = (uint32_t)(docId >> 29);
	key.n1 &= 0x000001ff;
	// set the docId lower bits
	key.n0 = docId;
	key.n0 <<= 35;
	// set the family filter bit
	if ( familyFilter ) key.n0 |= 0x0000000400000000ULL;
	else                key.n0 &= 0xfffffffbffffffffULL;
	// set the language bits
	key.n0 |= ((uint64_t)(languageBits & 0x3f)) << 28;
	// set the site hash
	key.n0 |= (uint64_t)(siteHash & 0x03ffffff) << 2;
	// set the del bit
	if ( isDelKey ) key.n0 &= 0xfffffffffffffffeULL;
	else            key.n0 |= 0x0000000000000001ULL;
	// set half bit
	if ( !isHalfKey ) key.n0 &= 0xfffffffffffffffdULL;
	else              key.n0 |= 0x0000000000000002ULL;
	// return the key
	return key;
}
