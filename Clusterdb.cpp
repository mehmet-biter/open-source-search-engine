#include "gb-include.h"

#include "Clusterdb.h"
#include "Rebalance.h"
#include "Collectiondb.h"
#include "JobScheduler.h"
#include "Conf.h"

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
	return m_rdb.init ( "clusterdb"   ,
			    0             , // no data now! just docid/s/c
			    2, // g_conf.m_clusterdbMinFilesToMerge,
			    g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , // maxTreeNodes  ,
			    true          , // half keys?
			    12,             // key size
			    false);         //useIndexFile
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Clusterdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t maxTreeNodes  = treeMem / ( 16 + CLUSTER_REC_SIZE );
	// initialize our own internal rdb
	return m_rdb.init ( "clusterdbRebuild" ,
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       , // g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  ,
			    true          , // half keys?
			    12,             // key size
			    false);         //useIndexFile
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
