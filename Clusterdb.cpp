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

	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir  ,
			    "clusterdb"   ,
			    0             , // no data now! just docid/s/c
			    2, // g_conf.m_clusterdbMinFilesToMerge,
			    g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , // maxTreeNodes  ,
			    true          , // half keys?
			    false,  // is titledb
			    12);     // key size
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
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       , // g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  ,
			    true          , // half keys?
			    false         ,  // is titledb
			    12            );     // key size
}

bool Clusterdb::verify(const char *coll) {
	log ( LOG_DEBUG, "db: Verifying Clusterdb for coll %s...", coll );
	g_jobScheduler.disallow_new_jobs();

	Msg5 msg5;
	RdbList list;
	key96_t startKey;
	key96_t endKey;
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
			      true,           // isRealMerge
			      true))          // allowPageCache
	{
		g_jobScheduler.allow_new_jobs();
		log("db: HEY! it did not block");
		return false;
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key96_t k = list.getCurrentKey();
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
		log (LOG_WARN, "db: Out of first %" PRId32" records in clusterdb, "
		     "only %" PRId32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log(LOG_WARN, "db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Clusterdb inconsistency." );
		g_jobScheduler.allow_new_jobs();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Clusterdb passed verification successfully for "
			"%" PRId32" recs.", count );
	// DONE
	g_jobScheduler.allow_new_jobs();
	return true;
}

key96_t Clusterdb::makeClusterRecKey ( int64_t     docId,
				     bool          familyFilter,
				     uint8_t       languageBits,
				     int32_t          siteHash,
				     bool          isDelKey,
				     bool          isHalfKey ) {
	key96_t key;
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
