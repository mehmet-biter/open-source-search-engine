#include "Linkdb.h"
#include "Conf.h"
#include "Titledb.h"
#include "linkspam.h"
#include "Collectiondb.h"
#include "Rebalance.h"
#include "Process.h"

Linkdb g_linkdb;
Linkdb g_linkdb2;

void Linkdb::reset() {
	m_rdb.reset();
}

bool Linkdb::init ( ) {

	key224_t  k;
	// sanity tests
	uint32_t    linkeeSiteHash32 = (uint32_t)rand();
	uint32_t    linkerSiteHash32 = (uint32_t)rand();
	uint64_t linkeeUrlHash64  = (uint64_t)rand() << 32LL | rand();
	// mask it to 32+15 bits
	linkeeUrlHash64 &= 0x00007fffffffffffLL;
	unsigned char linkerSiteRank = 13;
	int32_t      ip               = rand();
	int32_t      ipdom3 = ipdom(ip);
	int64_t docId = ((uint64_t)rand() << 32 | rand()) & DOCID_MASK;
	int32_t discoveryDate = 1339784732;
	int32_t lostDate      = discoveryDate + 86400*23;
	char linkSpam = 1;
	k = makeKey_uk ( linkeeSiteHash32 ,
			 linkeeUrlHash64  ,
			 linkSpam         , // islinkspam?
			 linkerSiteRank   ,
			 ip               ,
			 docId            ,
			 discoveryDate    ,
			 lostDate         ,
			 false            , // newaddtooldpage?
			 linkerSiteHash32 ,
			 false            ); // is del?

	// jan 1 2008
	uint32_t epoch = LINKDBEPOCH;
	int32_t dd2 = (discoveryDate - epoch) / 86400;
	dd2 = dd2 * 86400  + epoch;
	int32_t ld2 = (lostDate - epoch) / 86400;
	if ( lostDate == 0 ) ld2 = 0;
	ld2 = ld2 * 86400  + epoch;

	// now test it
	if(getLinkeeSiteHash32_uk(&k)!=linkeeSiteHash32){g_process.shutdownAbort(true);}
	if(getLinkeeUrlHash64_uk(&k)!=linkeeUrlHash64){g_process.shutdownAbort(true);}
	if ( isLinkSpam_uk    ( &k ) != linkSpam       ) {g_process.shutdownAbort(true);}
	if (getLinkerSiteHash32_uk(&k)!=linkerSiteHash32){g_process.shutdownAbort(true);}
	if ( getLinkerSiteRank_uk(&k) != linkerSiteRank){g_process.shutdownAbort(true);}
	if ( getLinkerIp24_uk ( &k ) != ipdom3         ) {g_process.shutdownAbort(true);}
	if ( getLinkerIp_uk ( &k ) != ip         ) {g_process.shutdownAbort(true);}
	if ( getLinkerDocId_uk( &k ) != docId          ) {g_process.shutdownAbort(true);}
	if ( getDiscoveryDate_uk(&k) != dd2  ) {g_process.shutdownAbort(true);}

	// more tests
	setDiscoveryDate_uk (&k,discoveryDate);
	if ( getDiscoveryDate_uk(&k) != dd2  ) {g_process.shutdownAbort(true);}


	int32_t ip3 = 0xabcdef12;
	setIp32_uk ( &k , ip3 );
	int32_t ip4 = getLinkerIp_uk ( &k );
	if ( ip3 != ip4 ) { g_process.shutdownAbort(true); }

	int64_t maxTreeMem = g_conf.m_linkdbMaxTreeMem;
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = sizeof(key192_t)+4 +4+4+4+4
	// . 32 bytes per record when in the tree
	int32_t maxTreeNodes = maxTreeMem /(sizeof(key224_t)+16);

	// init the rdb
	return m_rdb.init ( "linkdb" ,
			    0        , // fixeddatasize is 0 since no data
			    // keep it high since we are mostly ssds now and
			    // the reads are small...
			    -1,//g_conf.m_linkdbMinFilesToMerge ,
			    // fix this to 15 and rely on the page cache of
			    // just the satellite files and the daily merge to
			    // keep things fast.
			    //15       , 
			    maxTreeMem ,
			    maxTreeNodes ,
			    false, // true     , // use half keys
			    sizeof(key224_t),    // key size
			    false);         //useIndexFile
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Linkdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t nodeSize = ( sizeof(key224_t) + 12 + 4 ) + sizeof(collnum_t);
	int32_t maxTreeNodes  = treeMem / nodeSize;
	// initialize our own internal rdb
	return m_rdb.init ( "linkdbRebuild" ,
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       , // g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  ,
			    false, // true          , // half keys?
			    sizeof(key224_t),  // key size
			    false);         //useIndexFile
}

bool Linkdb::verify(const char *coll) {
	log ( LOG_DEBUG, "db: Verifying Linkdb for coll %s...", coll );

	Msg5 msg5;
	RdbList list;
	key224_t startKey;
	key224_t endKey;
	startKey.setMin();
	endKey.setMax();
	int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_LINKDB   ,
			      cr->m_collnum      ,
			      &list         ,
			      (char*)&startKey      ,
			      (char*)&endKey        ,
			      minRecSizes   ,
			      true          , // includeTree   ,
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cacheKey
			      0             , // retryNum
			      -1            , // maxRetries
			      -1LL          , // syncPoint
			      true          , // isRealMerge
			      true))          // allowPageCache
	{
		log(LOG_DEBUG, "db: HEY! it did not block");
		return false;
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key224_t k;
		list.getCurrentKey((char*)&k);
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		//uint32_t shardNum = getShardNum ( RDB_LINKDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_LINKDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %" PRId32" records in Linkdb , "
		     "only %" PRId32" belong to our group.",count,got);

		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to inconsistency.");
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Linkdb passed verification successfully for "
	      "%" PRId32" recs.", count );
	// DONE
	return true;
}

// make a "url" key
key224_t Linkdb::makeKey_uk ( uint32_t  linkeeSiteHash32       ,
			      uint64_t  linkeeUrlHash64        ,
			      bool      isLinkSpam       ,
			      unsigned char      linkerSiteRank   ,
			      uint32_t  linkerIp               ,
			      int64_t linkerDocId ,
			      uint32_t      discoveryDate ,
			      uint32_t      lostDate ,
			      bool      newAddToOldPage ,
			      uint32_t linkerSiteHash32 ,
			      bool      isDelete         ) {
	// mask it
	linkeeUrlHash64 &= LDB_MAXURLHASH;

	key224_t k;

	k.n3 = linkeeSiteHash32;
	k.n3 <<= 32;
	k.n3 |= (linkeeUrlHash64>>15) & 0xffffffff;

	// finish the url hash
	k.n2 = linkeeUrlHash64 & 0x7fff;

	k.n2 <<= 1;
	if ( isLinkSpam ) k.n2 |= 0x01;

	// make it 8-bites for now even though only needs 4
	k.n2 <<= 8;
	k.n2 |= (unsigned char)~linkerSiteRank;

	k.n2 <<= 8;
	//k.n2 |= linkerHopCount;
	// this is now part of the linkerip, steve wants the full ip
	k.n2 |= (linkerIp >> 24);

	//uint32_t id = ipdom(linkerIp);
	//if ( id > 0xffffff ) { g_process.shutdownAbort(true); }
	k.n2 <<= 24;
	k.n2 |= (linkerIp & 0x00ffffff);

	k.n2 <<= 8;
	k.n2 |= (((uint64_t)linkerDocId) >> 30);

	k.n1 = (((uint64_t)linkerDocId) & 0x3fffffffLL);

	// two reserved bits
	k.n1 <<= 2;

	// sanity checks
	//if(discoveryDate && discoveryDate < 1025376000){g_process.shutdownAbort(true);}
	if ( lostDate && lostDate < LINKDBEPOCH){
		lostDate = LINKDBEPOCH;
		//g_process.shutdownAbort(true);
	}

	// . convert discovery date from utc into days since jan 2008 epoch
	// . the number is for jan 2012, so subtract 4 years to do 2008
	uint32_t epoch = LINKDBEPOCH;
	if ( discoveryDate && discoveryDate < epoch ) {
		discoveryDate = epoch;
		//g_process.shutdownAbort(true);
	}
	uint32_t nd = (discoveryDate - epoch) / 86400;
	if ( discoveryDate == 0 ) nd = 0;
	// makeEndKey_uk() maxes this out!
	if ( nd > 0x3fff ) nd = 0x3fff;

	k.n1 <<= 14;
	k.n1 |= nd;

	// one reservied bit
	k.n1 <<= 1;

	k.n1 <<= 1;
	if ( newAddToOldPage ) k.n1 |= 0x01;

	// the "lost" date. 0 if not yet lost.
	uint32_t od = (lostDate - LINKDBEPOCH) / 86400;
	if ( lostDate == 0 ) od = 0;
	// makeEndKey_uk() maxes this out!
	if ( od > 0x3fff ) od = 0x3fff;
	k.n1 <<= 14;
	k.n1 |= od;

	// 2 bits of linker site hash
	k.n1 <<= 2;
	k.n1 |= linkerSiteHash32 >> 30;

	// rest of linker site hash
	k.n0 = linkerSiteHash32;
	// halfbit - unused now!
	k.n0 <<= 1;
	// delbit
	k.n0 <<= 1;
	if ( ! isDelete ) k.n0 |= 0x01;
 
	return k;
}

void Linkdb::printKey(const char *k) {
	key224_t *key = (key224_t*)k;
	logf(LOG_TRACE, "k=%s "
			     "linkeesitehash32=0x%08" PRIx32" "
			     "linkeeurlhash=0x%012" PRIx64" "
			     "linkspam=%" PRId32" "
			     "siterank=%02" PRId32" "
			     "ip32=%s "
			     "docId=%012" PRIu64" "
			     "discovered=%" PRIu32" "
			     "lost=%" PRIu32" "
			     "sitehash32=0x%08" PRIx32" "
			     "shardNum=%" PRIu32" "
			     "%s",
	     KEYSTR(&k, sizeof(key224_t)),
	     (int32_t)Linkdb::getLinkeeSiteHash32_uk(key),
	     (int64_t)Linkdb::getLinkeeUrlHash64_uk(key),
	     (int32_t)Linkdb::isLinkSpam_uk(key),
	     (int32_t)Linkdb::getLinkerSiteRank_uk(key),
	     iptoa((int32_t)Linkdb::getLinkerIp_uk(key)),
	     (uint64_t)Linkdb::getLinkerDocId_uk(key),
	     (uint32_t)Linkdb::getDiscoveryDate_uk(key),
	     (uint32_t)Linkdb::getLostDate_uk(key),
	     (int32_t)Linkdb::getLinkerSiteHash32_uk(key),
	     (uint32_t)getShardNum(RDB_LINKDB, k),
	     KEYNEG(k) ? " (delete)" : "");
}
