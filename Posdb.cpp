#include "Posdb.h"

#include "gb-include.h"

#include "Posdb.h"
#include "JobScheduler.h"
#include "Rebalance.h"
#include "RdbCache.h"
#include "Sanity.h"

#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif



// a global class extern'd in .h file
Posdb g_posdb;

// for rebuilding posdb
Posdb g_posdb2;



// resets rdb
void Posdb::reset() { 
	m_rdb.reset();
}

bool Posdb::init ( ) {
	// sanity check
	key144_t k;
	int64_t termId = 123456789LL;
	int64_t docId = 34567292222LL;
	int32_t dist = MAXWORDPOS-1;//54415;
	int32_t densityRank = 10;
	int32_t diversityRank = MAXDIVERSITYRANK-1;//11;
	int32_t wordSpamRank = MAXWORDSPAMRANK-1;//12;
	int32_t siteRank = 13;
	int32_t hashGroup = 1;
	int32_t langId = 59;
	int32_t multiplier = 13;
	char shardedByTermId = 1;
	char isSynonym = 1;
	g_posdb.makeKey ( &k ,
			  termId ,
			  docId,
			  dist,
			  densityRank , // 0-15
			  diversityRank,
			  wordSpamRank,
			  siteRank,
			  hashGroup ,
			  langId,
			  multiplier,
			  isSynonym , // syn?
			  false , // delkey?
			  shardedByTermId );
	// test it out
	if ( g_posdb.getTermId ( &k ) != termId ) gbshutdownLogicError();
	//int64_t d2 = g_posdb.getDocId(&k);
	if ( g_posdb.getDocId (&k ) != docId ) gbshutdownLogicError();
	if ( g_posdb.getHashGroup ( &k ) !=hashGroup) gbshutdownLogicError();
	if ( g_posdb.getWordPos ( &k ) !=  dist ) gbshutdownLogicError();
	if ( g_posdb.getDensityRank (&k)!=densityRank)gbshutdownLogicError();
	if ( g_posdb.getDiversityRank(&k)!=diversityRank)gbshutdownLogicError();
	if ( g_posdb.getWordSpamRank(&k)!=wordSpamRank)gbshutdownLogicError();
	if ( g_posdb.getSiteRank (&k) != siteRank ) gbshutdownLogicError();
	if ( g_posdb.getLangId ( &k ) != langId ) gbshutdownLogicError();
	if ( g_posdb.getMultiplier ( &k ) !=multiplier)gbshutdownLogicError();
	if ( g_posdb.getIsSynonym ( &k ) != isSynonym) gbshutdownLogicError();
	if ( g_posdb.isShardedByTermId(&k)!=shardedByTermId)gbshutdownLogicError();
	// more tests
	setDocIdBits ( &k, docId );
	setMultiplierBits ( &k, multiplier );
	setSiteRankBits ( &k, siteRank );
	setLangIdBits ( &k, langId );
	// test it out
	if ( g_posdb.getTermId ( &k ) != termId ) gbshutdownLogicError();
	if ( g_posdb.getDocId (&k ) != docId ) gbshutdownLogicError();
	if ( g_posdb.getWordPos ( &k ) !=  dist ) gbshutdownLogicError();
	if ( g_posdb.getDensityRank (&k)!=densityRank)gbshutdownLogicError();
	if ( g_posdb.getDiversityRank(&k)!=diversityRank)gbshutdownLogicError();
	if ( g_posdb.getWordSpamRank(&k)!=wordSpamRank)gbshutdownLogicError();
	if ( g_posdb.getSiteRank (&k) != siteRank ) gbshutdownLogicError();
	if ( g_posdb.getHashGroup ( &k ) !=hashGroup) gbshutdownLogicError();
	if ( g_posdb.getLangId ( &k ) != langId ) gbshutdownLogicError();
	if ( g_posdb.getMultiplier ( &k ) !=multiplier)gbshutdownLogicError();
	if ( g_posdb.getIsSynonym ( &k ) != isSynonym) gbshutdownLogicError();

	/*
	// more tests
	key144_t sk;
	key144_t ek;
	g_posdb.makeStartKey(&sk,termId);
	g_posdb.makeEndKey  (&ek,termId);

	RdbList list;
	list.set(NULL,0,NULL,0,0,true,true,18);
	key144_t ka;
	ka.n2 = 0x1234567890987654ULL;
	ka.n1 = 0x5566778899aabbccULL;
	ka.n0 = (uint16_t)0xbaf1;
	list.addRecord ( (char *)&ka,0,NULL,true );
	key144_t kb;
	kb.n2 = 0x1234567890987654ULL;
	kb.n1 = 0x5566778899aabbccULL;
	kb.n0 = (uint16_t)0xeef1;
	list.addRecord ( (char *)&kb,0,NULL,true );

	char *p = list.m_list;
	char *pend = p + list.m_listSize;
	for ( ; p < pend ; p++ )
		log("db: %02" PRId32") 0x%02" PRIx32,p-list.m_list,
		    (int32_t)(*(unsigned char *)p));
	list.resetListPtr();
	list.checkList_r(false,true,RDB_POSDB);
	gbshutdownLogicError();
	*/

	// make it lower now for debugging
	//maxTreeMem = 5000000;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize      = (sizeof(key144_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = g_conf.m_posdbMaxTreeMem / nodeSize ;

	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want posdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	return m_rdb.init ( g_hostdb.m_dir,
	                    "posdb",
	                    true, // dedup same keys?
	                    0, // fixed data size
	                    // -1 means look in CollectionRec::m_posdbMinFilesToMerge
	                    -1,
	                    g_conf.m_posdbMaxTreeMem, // g_conf.m_posdbMaxTreeMem  ,
	                    maxTreeNodes                ,
	                    // now we balance so Sync.cpp can ordered huge lists
                        true                        , // balance tree?
                        true                        , // use half keys?
                        false                       , // g_conf.m_posdbSav
	                    // newer systems have tons of ram to use
	                    // for their disk page cache. it is slower than
	                    // ours but the new engine has much slower things
			            NULL,//&m_pc                       ,
			            false , // istitledb?
			            false , // preloaddiskpagecache?
			            sizeof(key144_t),
			            false,
			            false,
						true);
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Posdb::init2 ( int32_t treeMem ) {
	//if ( ! setGroupIdTable () ) return false;
	// . what's max # of tree nodes?
	// . each rec in tree is only 1 key (12 bytes)
	// . but has 12 bytes of tree overhead (m_left/m_right/m_parents)
	// . this is UNUSED for bin trees!!
	int32_t nodeSize     = (sizeof(key144_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = treeMem  / nodeSize ;
	// . set our own internal rdb
	// . max disk space for bin tree is same as maxTreeMem so that we
	//   must be able to fit all bins in memory
	// . we do not want posdb's bin tree to ever hit disk since we
	//   dump it to rdb files when it is 90% full (90% of bins in use)
	return m_rdb.init ( g_hostdb.m_dir              ,
			    "posdbRebuild"            ,
			    true                        , // dedup same keys?
			    0                           , // fixed data size
			    // change back to 200!!
			    //2                         , // min files to merge
			    //230                       , // min files to merge
			    1000                        , // min files to merge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    true                        , // use half keys?
			    false                       , // posdbSaveCache
			    NULL                        , // s_pc
			    false ,
			    false ,
			    sizeof(key144_t),
			true );
}


bool Posdb::addColl ( const char *coll, bool doVerify ) {
	if ( ! m_rdb.addRdbBase1 ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// do a deep verify to figure out which files are corrupt
	//deepVerify ( coll );
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}

bool Posdb::verify ( const char *coll ) {
	return true;
#if 0
	log ( LOG_DEBUG, "db: Verifying Posdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	RdbList list;
	key144_t startKey;
	key144_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_POSDB   ,
			      cr->m_collnum      ,
			      &list         ,
			      &startKey      ,
			      &endKey        ,
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
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	bool printedKey = false;
	bool printedZeroKey = false;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key144_t k;
		list.getCurrentKey(&k);
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_POSDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_POSDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
		else if ( !printedKey ) {
			log ( "db: Found bad key in list (only printing once): "
			      "%" PRIx64" %" PRIx64" %" PRIx32, k.n2, k.n1 ,(int32_t)k.n0);
			printedKey = true;
		}
		if ( k.n1 == 0 && k.n0 == 0 ) {
			if ( !printedZeroKey ) {
				log ( "db: Found Zero key in list, passing. "
				      "(only printing once)." );
				printedZeroKey = true;
			}
			if ( shardNum != getMyShardNum() )
				got++;
		}
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %" PRId32" records in posdb, only %" PRId32" belong "
		     "to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to Posdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Posdb passed verification successfully for %" PRId32" "
			"recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
#endif
}


// . see Posdb.h for format of the 12 byte key
// . TODO: substitute var ptrs if you want extra speed
void Posdb::makeKey ( void              *vkp            ,
		      int64_t          termId         ,
		      uint64_t docId          , 
		      int32_t               wordPos        ,
		      char               densityRank    ,
		      char               diversityRank  ,
		      char               wordSpamRank   ,
		      char               siteRank       ,
		      char               hashGroup      ,
		      char               langId         ,
		      int32_t               multiplier     ,
		      bool               isSynonym      ,
		      bool               isDelKey       ,
		      bool shardedByTermId ) {

	// sanity
	if ( siteRank      > MAXSITERANK      ) gbshutdownLogicError();
	if ( wordSpamRank  > MAXWORDSPAMRANK  ) gbshutdownLogicError();
	if ( densityRank   > MAXDENSITYRANK   ) gbshutdownLogicError();
	if ( diversityRank > MAXDIVERSITYRANK ) gbshutdownLogicError();
	if ( langId        > MAXLANGID        ) gbshutdownLogicError();
	if ( hashGroup     > MAXHASHGROUP     ) gbshutdownLogicError();
	if ( wordPos       > MAXWORDPOS       ) gbshutdownLogicError();
	if ( multiplier    > MAXMULTIPLIER    ) gbshutdownLogicError();

	key144_t *kp = (key144_t *)vkp;

	// make sure we mask out the hi bits we do not use first
	termId = termId & TERMID_MASK;
	kp->n2 = termId;
	// then 16 bits of docid
	kp->n2 <<= 16;
	kp->n2 |= docId >> (38-16); // 22

	// rest of docid (22 bits)
	kp->n1 = docId & (0x3fffff);
	// a zero bit for aiding b-stepping alignment issues
	kp->n1 <<= 1;
	kp->n1 |= 0x00;
	// 4 site rank bits
	kp->n1 <<= 4;
	kp->n1 |= siteRank;
	// 4 langid bits
	kp->n1 <<= 5;
	kp->n1 |= (langId & 0x1f);
	// the word position, 18 bits
	kp->n1 <<= 18;
	kp->n1 |= wordPos;
	// the hash group, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= hashGroup;
	// the word span rank, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= wordSpamRank;
	// the diversity rank, 4 bits
	kp->n1 <<= 4;
	kp->n1 |= diversityRank;
	// word form bits, F-bits. right now just use 1 bit
	kp->n1 <<= 2;
	if ( isSynonym ) kp->n1 |= 0x01;

	// density rank, 5 bits
	kp->n0 = densityRank;
	// is in outlink text? reserved
	kp->n0 <<= 1;
	// a 1 bit for aiding b-stepping
	kp->n0 <<= 1;
	kp->n0 |= 0x01;
	// multiplier bits, 5 bits
	kp->n0 <<= 5;
	kp->n0 |= multiplier;
	// one maverick langid bit, the 6th bit
	kp->n0 <<= 1;
	if ( langId & 0x20 ) kp->n0 |= 0x01;
	// compression bits, 2 of 'em
	kp->n0 <<= 2;
	// delbit
	kp->n0 <<= 1;
	if ( ! isDelKey ) kp->n0 |= 0x01;

	if ( shardedByTermId ) setShardedByTermIdBit ( kp );

	// get the one we lost
	// char *kstr = KEYSTR ( kp , sizeof(POSDBKEY) );
	// if (!strcmp(kstr,"0x0ca3417544e400000000000032b96bf8aa01"))
	// 	log("got lost key");
}

RdbCache g_termFreqCache;
static bool s_cacheInit = false;

// . accesses RdbMap to estimate size of the indexList for this termId
// . returns an UPPER BOUND
// . because this is over POSDB now and not indexdb, a document is counted
//   once for every occurence of term "termId" it has... :{
int64_t Posdb::getTermFreq ( collnum_t collnum, int64_t termId ) {

	// establish the list boundary keys
	key144_t startKey ;
	key144_t endKey   ;
	makeStartKey ( &startKey, termId );
	makeEndKey   ( &endKey  , termId );


	if ( ! s_cacheInit ) {
		int32_t maxMem = 5000000; // 5MB now... save mem (was: 20000000)
		int32_t maxNodes = maxMem / 17; // 8+8+1
		if( ! g_termFreqCache.init ( maxMem   , // maxmem 20MB
					     8        , // fixed data size
					     false    , // supportlists?
					     maxNodes ,
					     false    , // use half keys?
					     "tfcache", // dbname
					     false    , // load from disk?
					     8        , // cache key size
					     0          // data key size
					     ))
			log("posdb: failed to init termfreqcache: %s",
			    mstrerror(g_errno));
		// ignore errors
		g_errno = 0;
		s_cacheInit = true;
	}

	// . check cache for super speed
	// . TODO: make key incorporate collection
	// . colnum is 0 for now
	int64_t val = g_termFreqCache.getLongLong2 ( collnum ,
						       termId  , // key
						       500   , // maxage secs
						       true    );// promote?



	// -1 means not found in cache. if found, return it though.
	if ( val >= 0 ) {
		//log("posdb: got %" PRId64" in cache",val);
		return val;
	}

	// . ask rdb for an upper bound on this list size
	// . but actually, it will be somewhat of an estimate 'cuz of RdbTree
	key144_t maxKey;
	//int64_t maxRecs;
	// . don't count more than these many in the map
	// . that's our old truncation limit, the new stuff isn't as dense
	//int32_t oldTrunc = 100000;
	// turn this off for this
	int64_t oldTrunc = -1;
	// get maxKey for only the top "oldTruncLimit" docids because when
	// we increase the trunc limit we screw up our extrapolation! BIG TIME!
	int64_t maxRecs = m_rdb.getListSize(collnum,
					    (char *)&startKey,
					    (char *)&endKey,
					    (char *)&maxKey,
					    oldTrunc );


	int64_t numBytes = m_rdb.m_buckets.getListSize(collnum,
						(char *)&startKey,
						(char *)&endKey,
						NULL,NULL);



	// convert from size in bytes to # of recs
	maxRecs += numBytes / sizeof(POSDBKEY);

	// RdbList list;
	// makeStartKey ( &startKey, termId );
	// makeEndKey   ( &endKey  , termId );
	// int numNeg = 0;
	// int numPos = 0;
	// m_rdb.m_buckets.getList ( collnum ,
	// 			  (char *)&startKey,
	// 			  (char *)&endKey,
	// 			  -1 , // minrecsizes
	// 			  &list,
	// 			  &numPos,
	// 			  &numNeg,
	// 			  true );
	// if ( numPos*18 != numBytes ) {
	// 	gbshutdownLogicError(); }

	

	// and assume each shard has about the same #
	maxRecs *= g_hostdb.m_numShards;

	// over all splits!
	//maxRecs *= g_hostdb.m_numShards;
	// . assume about 8 bytes per key on average for posdb.
	// . because of compression we got 12 and 6 byte keys in here typically
	//   for a single termid
	//maxRecs /= 8;

	// log it
	//log("posdb: approx=%" PRId64" exact=%" PRId64,maxRecs,numBytes);

	// now cache it. it sets g_errno to zero.
	g_termFreqCache.addLongLong2 ( collnum, termId, maxRecs );
	// return it
	return maxRecs;
}


const char *getHashGroupString ( unsigned char hg ) {
	if ( hg == HASHGROUP_BODY ) return "body";
	if ( hg == HASHGROUP_TITLE ) return "title";
	if ( hg == HASHGROUP_HEADING ) return "header";
	if ( hg == HASHGROUP_INLIST ) return "in list";
	if ( hg == HASHGROUP_INMETATAG ) return "meta tag";
	//if ( hg == HASHGROUP_INLINKTEXT ) return "offsite inlink text";
	if ( hg == HASHGROUP_INLINKTEXT ) return "inlink text";
	if ( hg == HASHGROUP_INTAG ) return "tag";
	if ( hg == HASHGROUP_NEIGHBORHOOD ) return "neighborhood";
	if ( hg == HASHGROUP_INTERNALINLINKTEXT) return "onsite inlink text";
	if ( hg == HASHGROUP_INURL ) return "in url";
	if ( hg == HASHGROUP_INMENU ) return "in menu";
	return "unknown!";
}



void printTermList ( int32_t i, const char *list, int32_t listSize ) {
	// first key is 12 bytes
	bool firstKey = true;
	const char *px = list;//->m_list;
	const char *pxend = px + listSize;//list->m_listSize;
	for ( ; px < pxend ; ) {
		int32_t wp = g_posdb.getWordPos(px);
		int32_t dr = g_posdb.getDensityRank(px);
		int32_t hg = g_posdb.getHashGroup(px);
		int32_t syn = g_posdb.getIsSynonym(px);
		log("seo: qterm#%" PRId32" pos=%" PRId32" dr=%" PRId32" hg=%s syn=%" PRId32
		    , i
		    , wp
		    , dr
		    , getHashGroupString(hg)
		    , syn
		    );
		if ( firstKey && g_posdb.getKeySize(px)!=12)
			gbshutdownLogicError();
		else if ( ! firstKey&& g_posdb.getKeySize(px)!=6)
			gbshutdownLogicError();
		if ( firstKey ) px += 12;
		else            px += 6;
		firstKey = false;
	}
}



int Posdb::printList ( RdbList &list ) {
	bool justVerify = false;
	POSDBKEY lastKey;
	// loop over entries in list
	for ( list.resetListPtr() ; ! list.isExhausted() && ! justVerify ;
	      list.skipCurrentRecord() ) {
		key144_t k; list.getCurrentKey(&k);
		// compare to last
		const char *err = "";
		if ( KEYCMP((char *)&k,(char *)&lastKey,sizeof(key144_t))<0 ) 
			err = " (out of order)";
		lastKey = k;
		// is it a delete?
		const char *dd = "";
		if ( (k.n0 & 0x01) == 0x00 ) dd = " (delete)";
		int64_t d = g_posdb.getDocId(&k);
		uint8_t dh = g_titledb.getDomHash8FromDocId(d);
		char *rec = list.m_listPtr;
		int32_t recSize = 18;
		if ( rec[0] & 0x04 ) recSize = 6;
		else if ( rec[0] & 0x02 ) recSize = 12;
		// alignment bits check
		if ( recSize == 6  && !(rec[1] & 0x02) ) {
			int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			//int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes
			//log("debug1: d=%" PRId64" nd1=%" PRId64" nd2=%" PRId64" nd3=%" PRId64,
			//d,nd1,nd2,nd3);
			err = " (alignerror1)";
			if ( nd1 < d ) err = " (alignordererror1)";
			//g_process.shutdownAbort(true);
		}
		if ( recSize == 12 && !(rec[1] & 0x02) )  {
			//int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes
			//log("debug1: d=%" PRId64" nd1=%" PRId64" nd2=%" PRId64" nd3=%" PRId64,
			//d,nd1,nd2,nd3);
			//if ( nd2 < d ) gbshutdownLogicError();
			//g_process.shutdownAbort(true);
			err = " (alignerror2)";
			if ( nd2 < d ) err = " (alignorderrror2)";
		}
		// if it 
		if ( recSize == 12 &&  (rec[7] & 0x02)) { 
			//int64_t nd1 = g_posdb.getDocId(rec+6);
			// seems like nd2 is it, so it really is 12 bytes but
			// does not have the alignment bit set...
			int64_t nd2 = g_posdb.getDocId(rec+12);
			//int64_t nd3 = g_posdb.getDocId(rec+18);
			// what size is it really?
			// seems like 12 bytes really as well!
			//log("debug2: d=%" PRId64" nd1=%" PRId64" nd2=%" PRId64" nd3=%" PRId64,
			//d,nd1,nd2,nd3);
			//g_process.shutdownAbort(true);
			err = " (alignerror3)";
			if ( nd2 < d ) err = " (alignordererror3)";
		}

		log(
		       "k=%s "
		       "tid=%015" PRIu64" "
		       "docId=%012" PRId64" "

		       "siterank=%02" PRId32" "
		       "langid=%02" PRId32" "
		       "pos=%06" PRId32" "
		       "hgrp=%02" PRId32" "
		       "spamrank=%02" PRId32" "
		       "divrank=%02" PRId32" "
		       "syn=%01" PRId32" "
		       "densrank=%02" PRId32" "
		       "mult=%02" PRId32" "

		       "dh=0x%02" PRIx32" "
		       "rs=%" PRId32 //recSize
		       "%s" // dd
		       "%s" // err
		       "\n" ,
		       KEYSTR(&k,sizeof(key144_t)),
		       (int64_t)g_posdb.getTermId(&k),
		       d ,
		       (int32_t)g_posdb.getSiteRank(&k),
		       (int32_t)g_posdb.getLangId(&k),
		       (int32_t)g_posdb.getWordPos(&k),
		       (int32_t)g_posdb.getHashGroup(&k),
		       (int32_t)g_posdb.getWordSpamRank(&k),
		       (int32_t)g_posdb.getDiversityRank(&k),
		       (int32_t)g_posdb.getIsSynonym(&k),
		       (int32_t)g_posdb.getDensityRank(&k),
		       (int32_t)g_posdb.getMultiplier(&k),
		       (int32_t)dh,
		       recSize,
		       dd ,
		       err );

		continue;
	}

	// startKey = *(key144_t *)list.getLastKey();
	// startKey += (uint32_t) 1;
	// // watch out for wrap around
	// if ( startKey < *(key144_t *)list.getLastKey() ) return;
	// goto loop;
	return 1;
}
