
#include "gb-include.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "JobScheduler.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"




/////////////////////////
/////////////////////////      SpiderColl
/////////////////////////

void SpiderColl::setCollectionRec ( CollectionRec *cr ) {
	m_cr = cr;
	// this was useful for debugging a null m_cr bug
	//log("sc: sc 0x%"PTRFMT" setting cr to 0x%"PTRFMT""
	//,(int32_t)this,(int32_t)cr);
}

CollectionRec *SpiderColl::getCollectionRec ( ) {
	//log("sc: sc 0x%"PTRFMT" getting cr of 0x%"PTRFMT""
	//,(int32_t)this,(int32_t)m_cr);
	return m_cr;
}

SpiderColl::SpiderColl () {
	m_overflowList = NULL;
	m_lastOverflowFirstIp = 0;
	m_lastPrinted = 0;
	m_deleteMyself = false;
	m_isLoading = false;
	m_gettingList1 = false;
	m_gettingList2 = false;
	m_lastScanTime = 0;
	m_isPopulatingDoledb = false;
	m_numAdded = 0;
	m_numBytesScanned = 0;
	m_lastPrintCount = 0;
	m_siteListIsEmptyValid = false;
	m_cr = NULL;
	//m_lastSpiderAttempt = 0;
	//m_lastSpiderCouldLaunch = 0;
	//m_numRoundsDone = 0;
	//m_lastDoledbReadEmpty = false; // over all priorities in this coll
	// re-set this to min and set m_needsWaitingTreeRebuild to true
	// when the admin updates the url filters page
	m_waitingTreeNeedsRebuild = false;
	m_nextKey2.setMin();
	m_endKey2.setMax();
	m_spidersOut = 0;
	m_coll[0] = '\0';// = NULL;
	reset();
	// reset this
	memset ( m_outstandingSpiders , 0 , 4 * MAX_SPIDER_PRIORITIES );
	// start off sending all colls local crawl info to all hosts to
	// be sure we are in sync
	memset ( m_sendLocalCrawlInfoToHost , 1 , MAX_HOSTS );
}

int32_t SpiderColl::getTotalOutstandingSpiders ( ) {
	int32_t sum = 0;
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ )
		sum += m_outstandingSpiders[i];
	return sum;
}


// load the tables that we set when m_doInitialScan is true
bool SpiderColl::load ( ) {

	// error?
	int32_t err = 0;
	// make the dir
	char *coll = g_collectiondb.getColl(m_collnum);
	// sanity check
	if ( ! coll || coll[0]=='\0' ) {
		log("spider: bad collnum of %"INT32"",(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return false;
		//char *xx=NULL;*xx=0; }
	}

	// reset this once
	//m_msg1Avail    = true;
	m_isPopulatingDoledb = false;

	// keep it kinda low if we got a ton of collections
	int32_t maxMem = 15000;
	int32_t maxNodes = 500;
	if ( g_collectiondb.m_numRecsUsed > 500 ) {
		maxNodes = 100;
		maxMem = maxNodes * 20;
	}

	if ( ! m_lastDownloadCache.init ( maxMem     , // maxcachemem,
					  8          , // fixed data size (MS)
					  false      , // support lists?
					  maxNodes   , // max nodes
					  false      , // use half keys?
					  "downcache", // dbname
					  false      , // load from disk?
					  12         , // key size (firstip)
					  12         , // data key size?
					  -1         ))// numPtrsMax
		return log("spider: dcache init failed");

	// this has a quickpoll in it, so that quickpoll processes
	// a restart request from crawlbottesting for this collnum which
	// calls Collectiondb::resetColl2() which calls deleteSpiderColl()
	// on THIS spidercoll, but our m_loading flag is set
	if (!m_sniTable.set   ( 4,8,0,NULL,0,false,MAX_NICENESS,"snitbl") )
		return false;
	if (!m_cdTable.set    (4,4,0,NULL,0,false,MAX_NICENESS,"cdtbl"))
		return false;
	// doledb seems to have like 32000 entries in it
	int32_t numSlots = 0; // was 128000
	if(!m_doleIpTable.set(4,4,numSlots,NULL,0,false,MAX_NICENESS,"doleip"))
		return false;
	// this should grow dynamically...
	if (!m_waitingTable.set (4,8,16,NULL,0,false,MAX_NICENESS,"waittbl"))
		return false;
	// . a tree of keys, key is earliestSpiderTime|ip (key=12 bytes)
	// . earliestSpiderTime is 0 if unknown
	// . max nodes is 1M but we should grow dynamically! TODO
	// . let's up this to 5M because we are hitting the limit in some
	//   test runs...
	// . try going to 20M now since we hit it again...
	// . start off at just 10 nodes since we grow dynamically now
	if (!m_waitingTree.set(0,10,true,-1,true,"waittree2",
			       false,"waitingtree",sizeof(key_t)))return false;
	m_waitingTreeKeyValid = false;
	m_scanningIp = 0;
	// prevent core with this
	//m_waitingTree.m_rdbId = RDB_NONE;

	// make dir
	char dir[500];
	sprintf(dir,"%scoll.%s.%"INT32"",g_hostdb.m_dir,coll,(int32_t)m_collnum);
	// load up all the tables
	if ( ! m_cdTable .load(dir,"crawldelay.dat"  ) ) err = g_errno;
	if ( ! m_sniTable.load(dir,"siteinlinks.dat" ) ) err = g_errno;
	// and its doledb data
	//if ( ! initializeDoleTables( ) ) err = g_errno;
	// our table that has how many of each firstIP are in doledb
	//if ( ! m_doleIpTable.load(dir,"doleiptable.dat") ) err = g_errno;

	// load in the waiting tree, IPs waiting to get into doledb
	BigFile file;
	file.set ( dir , "waitingtree-saved.dat" , NULL );
	bool treeExists = file.doesExist() > 0;

	// load the table with file named "THISDIR/saved"
	if ( treeExists && ! m_waitingTree.fastLoad(&file,&m_waitingMem) ) 
		err = g_errno;

	// init wait table. scan wait tree and add the ips into table.
	if ( ! makeWaitingTable() ) err = g_errno;
	// save it
	g_errno = err;
	// return false on error
	if ( g_errno ) 
		// note it
		return log("spider: had error loading initial table: %s",
			   mstrerror(g_errno));

	// if we hade doledb0001.dat files on disk then nuke doledb
	// and waiting tree and rebuild now with a tree-only doledb.
	RdbBase *base = getRdbBase ( RDB_DOLEDB , m_collnum );

	// . do this now just to keep everything somewhat in sync
	// . we lost dmoz.org and could not get it back in because it was
	//   in the doleip table but NOT in doledb!!!
	if ( ! makeDoleIPTable() ) return false;

	// delete the files and doledb-saved.dat and the waitingtree
	// and set the waitingtree into rebuild mode.
	if ( base && base->m_numFiles > 0 ) {
		nukeDoledb ( m_collnum );
		return true;
	}

	// otherwise true
	return true;
}

// . scan all spiderRequests in doledb at startup and add them to our tables
// . then, when we scan spiderdb and add to orderTree/urlhashtable it will
//   see that the request is in doledb and set m_doled...
// . initialize the dole table for that then
//   quickly scan doledb and add the doledb records to our trees and
//   tables. that way if we receive a SpiderReply() then addSpiderReply()
//   will be able to find the associated SpiderRequest.
//   MAKE SURE to put each spiderrequest into m_doleTable... and into
//   maybe m_urlHashTable too???
//   this should block since we are at startup...
bool SpiderColl::makeDoleIPTable ( ) {

	log(LOG_DEBUG,"spider: making dole ip table for %s",m_coll);

	key_t startKey ; startKey.setMin();
	key_t endKey   ; endKey.setMax();
	key_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_jobScheduler.are_new_jobs_allowed();
	// turn off regardless
	g_jobScheduler.disallow_new_jobs();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_DOLEDB    ,
			      m_collnum     ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      true          , // includeTree?
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0,//MAX_NICENESS  , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"spider: getList did not block.");
		return false;
	}
	// shortcut
	int32_t minSize=(int32_t)(sizeof(SpiderRequest)+sizeof(key_t)+4-MAX_URL_LEN);
	// all done if empty
	if ( list.isEmpty() ) goto done;
	// loop over entries in list
	for (list.resetListPtr();!list.isExhausted();list.skipCurrentRecord()){
		// get rec
		char *rec = list.getCurrentRec();
		// get key
		key_t k = list.getCurrentKey();		
		// skip deletes -- how did this happen?
		if ( (k.n0 & 0x01) == 0) continue;
		// check this out
		int32_t recSize = list.getCurrentRecSize();
		// zero?
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		// 16 is bad too... wtf is this?
		if ( recSize <= 16 ) continue;
		// crazy?
		if ( recSize<=minSize) {char *xx=NULL;*xx=0;}
		// . doledb key is 12 bytes, followed by a 4 byte datasize
		// . so skip that key and dataSize to point to spider request
		SpiderRequest *sreq = (SpiderRequest *)(rec+sizeof(key_t)+4);
		// add to dole tables
		if ( ! addToDoleTable ( sreq ) )
			// return false with g_errno set on error
			return false;
	}
	startKey = *(key_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey >= *(key_t *)list.getLastKey() ) goto loop;
 done:
	log(LOG_DEBUG,"spider: making dole ip table done.");
	// re-enable threads
	if ( enabled ) g_jobScheduler.allow_new_jobs();
	// we wrapped, all done
	return true;
}


CollectionRec *SpiderColl::getCollRec() {
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
	if ( ! cr ) log("spider: lost coll rec");
	return cr;
}

char *SpiderColl::getCollName() {
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return "lostcollection";
	return cr->m_coll;
}


// this one has to scan all of spiderdb
bool SpiderColl::makeWaitingTree ( ) {

	log(LOG_DEBUG,"spider: making waiting tree for %s",m_coll);

	key128_t startKey ; startKey.setMin();
	key128_t endKey   ; endKey.setMax();
	key128_t lastKey  ; lastKey.setMin();
	// turn off threads for this so it blocks
	bool enabled = g_jobScheduler.are_new_jobs_allowed();
	// turn off regardless
	g_jobScheduler.disallow_new_jobs();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      m_collnum     ,
			      &list         ,
			      &startKey     ,
			      &endKey       ,
			      minRecSizes   ,
			      true          , // includeTree?
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      MAX_NICENESS  , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        )){
		log(LOG_LOGIC,"spider: getList did not block.");
		return false;
	}
	// all done if empty
	if ( list.isEmpty() ) goto done;
	// loop over entries in list
	for (list.resetListPtr();!list.isExhausted();list.skipCurrentRecord()){
		// get rec
		char *rec = list.getCurrentRec();
		// get key
		key128_t k; list.getCurrentKey(&k);		
		// skip deletes -- how did this happen?
		if ( (k.n0 & 0x01) == 0) continue;
		// check this out
		int32_t recSize = list.getCurrentRecSize();
		// zero?
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		// 16 is bad too... wtf is this?
		if ( recSize <= 16 ) continue;
		// skip replies
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) continue;
		// get request
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// skip if not assigned to us
		if ( ! isAssignedToUs ( sreq->m_firstIp ) ) continue;
		// get first ip
		int32_t firstIp = sreq->m_firstIp;
		// skip if in dole ip table
		if ( m_doleIpTable.isInTable ( &firstIp ) ) continue;
		// make the key. use 1 for spiderTimeMS. this tells the
		// spider loop that it is temporary and should be updated
		key_t wk = makeWaitingTreeKey ( 1 , firstIp );
		// ok, add to waiting tree
		int32_t wn = m_waitingTree.addKey ( &wk );
		if ( wn < 0 ) {
			log("spider: makeWaitTree: %s",mstrerror(g_errno));
			return false;
		}
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: added time=1 ip=%s to waiting "
			    "tree (node#=%"INT32")", iptoa(firstIp),wn);
		// a tmp var
		int64_t fakeone = 1LL;
		// add to table now since its in the tree
		if ( ! m_waitingTable.addKey ( &firstIp , &fakeone ) ) {
			log("spider: makeWaitTree2: %s",mstrerror(g_errno));
			m_waitingTree.deleteNode3 ( wn , true );
			//log("sper: 6 del node %"INT32" for %s",wn,iptoa(firstIp));
			return false;
		}
	}
	startKey = *(key128_t *)list.getLastKey();
	startKey += (uint32_t) 1;
	// watch out for wrap around
	if ( startKey >= *(key128_t *)list.getLastKey() ) goto loop;
 done:
	log(LOG_DEBUG,"spider: making waiting tree done.");
	// re-enable threads
	if ( enabled ) g_jobScheduler.allow_new_jobs();
	// we wrapped, all done
	return true;
}

// for debugging query reindex i guess
int64_t SpiderColl::getEarliestSpiderTimeFromWaitingTree ( int32_t firstIp ) {
	// make the key. use 0 as the time...
	key_t wk = makeWaitingTreeKey ( 0, firstIp );
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode ( 0, (char *)&wk );
	// if empty, stop
	if ( node < 0 ) return -1;
	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get the key
	key_t *k = (key_t *)m_waitingTree.getKey ( node );
	// ok, we got one
	int32_t storedFirstIp = (k->n0) & 0xffffffff;
	// match? we call this with a firstIp of 0 below to indicate
	// any IP, we just want to get the next spider time.
	if ( firstIp != 0 && storedFirstIp != firstIp ) return -1;
	// get the time
	uint64_t spiderTimeMS = k->n1;
	// shift upp
	spiderTimeMS <<= 32;
	// or in
	spiderTimeMS |= (k->n0 >> 32);
	// make into seconds
	return spiderTimeMS;
}


bool SpiderColl::makeWaitingTable ( ) {
	log(LOG_DEBUG,"spider: making waiting table for %s.",m_coll);
	int32_t node = m_waitingTree.getFirstNode();
	for ( ; node >= 0 ; node = m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)m_waitingTree.getKey(node);
		// get ip from that
		int32_t ip = (key->n0) & 0xffffffff;
		// spider time is up top
		uint64_t spiderTimeMS = (key->n1);
		spiderTimeMS <<= 32;
		spiderTimeMS |= ((key->n0) >> 32);
		// store in waiting table
		if ( ! m_waitingTable.addKey(&ip,&spiderTimeMS) ) return false;
	}
	log(LOG_DEBUG,"spider: making waiting table done.");
	return true;
}


SpiderColl::~SpiderColl () {
	reset();
}

// we call this now instead of reset when Collectiondb::resetColl() is used
void SpiderColl::clearLocks ( ) {

	// remove locks from locktable for all spiders out i guess
	HashTableX *ht = &g_spiderLoop.m_lockTable;
 top:
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// skip if not our collnum
		if ( lock->m_collnum != m_collnum ) continue;
		// nuke it!
		ht->removeSlot(i);
		// restart since cells may have shifted
		goto top;
	}

	/*
	// reset these for SpiderLoop;
	m_nextDoledbKey.setMin();
	m_didRound = false;
	// set this to -1 here, when we enter spiderDoledUrls() it will
	// see that its -1 and set the m_msg5StartKey
	m_pri2 = -1; // MAX_SPIDER_PRIORITIES - 1;
	m_twinDied = false;
	m_lastUrlFiltersUpdate = 0;

	char *coll = "unknown";
	if ( m_coll[0] ) coll = m_coll;
	logf(LOG_DEBUG,"spider: CLEARING spider cache coll=%s",coll);

	m_ufnMapValid = false;

	m_doleIpTable .clear();
	m_cdTable     .clear();
	m_sniTable    .clear();
	m_waitingTable.clear();
	m_waitingTree .clear();
	m_waitingMem  .clear();

	//m_lastDownloadCache.clear ( m_collnum );

	// copied from reset() below
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
		m_nextKeys[i] =	g_doledb.makeFirstKey2 ( i );
		m_isDoledbEmpty[i] = 0;
	}

	// assume the whole thing is not empty
	m_allDoledbPrioritiesEmpty = 0;//false;
	m_lastEmptyCheck = 0;
	*/
}

void SpiderColl::reset ( ) {

	// these don't work because we only store one reply
	// which overwrites any older reply. that's how the 
	// key is. we can change the key to use the timestamp 
	// and not parent docid in makeKey() for spider 
	// replies later.
	// m_numSuccessReplies = 0;
	// m_numFailedReplies  = 0;

	// reset these for SpiderLoop;
	m_nextDoledbKey.setMin();
	//m_didRound = false;
	// set this to -1 here, when we enter spiderDoledUrls() it will
	// see that its -1 and set the m_msg5StartKey
	m_pri2 = -1; // MAX_SPIDER_PRIORITIES - 1;
	m_twinDied = false;
	m_lastUrlFiltersUpdate = 0;

	m_isPopulatingDoledb = false;

	char *coll = "unknown";
	if ( m_coll[0] ) coll = m_coll;
	log(LOG_DEBUG,"spider: resetting spider cache coll=%s",coll);

	m_ufnMapValid = false;

	m_doleIpTable .reset();
	m_cdTable     .reset();
	m_sniTable    .reset();
	m_waitingTable.reset();
	m_waitingTree .reset();
	m_waitingMem  .reset();
	m_winnerTree  .reset();
	m_winnerTable .reset();
	m_dupCache    .reset();

	if ( m_overflowList ) {
		mfree ( m_overflowList , OVERFLOWLISTSIZE * 4 ,"olist" );
		m_overflowList = NULL;
	}

	// each spider priority in the collection has essentially a cursor
	// that references the next spider rec in doledb to spider. it is
	// used as a performance hack to avoid the massive positive/negative
	// key annihilations related to starting at the top of the priority
	// queue every time we scan it, which causes us to do upwards of
	// 300 re-reads!
	for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
		m_nextKeys[i] =	g_doledb.makeFirstKey2 ( i );
		m_isDoledbEmpty[i] = 0;
	}

	// assume the whole thing is not empty
	//m_allDoledbPrioritiesEmpty = 0;//false;
	//m_lastEmptyCheck = 0;

}

bool SpiderColl::updateSiteNumInlinksTable ( int32_t siteHash32, 
					     int32_t sni, 
					     time_t timestamp ) {
	// do not update if invalid
	if ( sni == -1 ) return true;
	// . get entry for siteNumInlinks table
	// . use 32-bit key specialized lookup for speed
	uint64_t *val = (uint64_t *)m_sniTable.getValue32(siteHash32);
	// bail?
	if ( val && ((*val)&0xffffffff) > (uint32_t)timestamp ) return true;
	// . make new data for this key
	// . lower 32 bits is the addedTime
	// . upper 32 bits is the siteNumInlinks
	uint64_t nv = (uint32_t)sni;
	// shift up
	nv <<= 32;
	// or in time
	nv |= (uint32_t)timestamp;//sreq->m_addedTime;
	// just direct update if faster
	if  ( val ) *val = nv;
	// store it anew otherwise
	else if ( ! m_sniTable.addKey(&siteHash32,&nv) )
		// return false with g_errno set on error
		return false;
	// success
	return true;
}

// . we call this when we receive a spider reply in Rdb.cpp
// . returns false and sets g_errno on error
// . xmldoc.cpp adds reply AFTER the negative doledb rec since we decement
//   the count in m_doleIpTable here
bool SpiderColl::addSpiderReply ( SpiderReply *srep ) {

	////
	//
	// skip if not assigned to us for doling
	//
	////
	if ( ! isAssignedToUs ( srep->m_firstIp ) )
		return true;

	/////////
	//
	// remove the lock here
	//
	//////
	int64_t lockKey = makeLockTableKey ( srep );
	
	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	UrlLock *lock = (UrlLock *)ht->getValue ( &lockKey );
	time_t nowGlobal = getTimeGlobal();

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing lock uh48=%"INT64" "
		     "lockKey=%"UINT64"",  
		     srep->getUrlHash48(),
		     lockKey );

	// we do it this way rather than remove it ourselves
	// because a lock request for this guy
	// might be currently outstanding, and it will end up
	// being granted the lock even though we have by now removed
	// it from doledb, because it read doledb before we removed 
	// it! so wait 5 seconds for the doledb negative key to 
	// be absorbed to prevent a url we just spidered from being
	// re-spidered right away because of this sync issue.
	// . if we wait too long then the round end time, SPIDER_DONE_TIMER,
	//   will kick in before us and end the round, then we end up
	//   spidering a previously locked url right after and DOUBLE
	//   increment the round!
	if ( lock ) lock->m_expires = nowGlobal + 2;
	/////
	//
	// but do note that its spider has returned for populating the
	// waiting tree. addToWaitingTree should not add an entry if
	// a spiderReply is still pending according to the lock table,
	// UNLESS, maxSpidersPerIP is more than what the lock table says
	// is currently being spidered.
	//
	/////
	if ( lock ) lock->m_spiderOutstanding = false;
	// bitch if not in there
	// when "rebuilding" (Rebuild.cpp) this msg gets triggered too much...
	// so only show it when in debug mode.
	if ( !lock && g_conf.m_logDebugSpider)//ht->isInTable(&lockKey)) 
		logf(LOG_DEBUG,"spider: rdb: lockKey=%"UINT64" "
		     "was not in lock table",lockKey);

	// now just remove it since we only spider our own urls
	// and doledb is in memory
	g_spiderLoop.m_lockTable.removeKey ( &lockKey );

	// update the latest siteNumInlinks count for this "site" (repeatbelow)
	updateSiteNumInlinksTable ( srep->m_siteHash32, 
				    srep->m_siteNumInlinks,
				    srep->m_spideredTime );

	// . skip the rest if injecting
	// . otherwise it triggers a lookup for this firstip in spiderdb to
	//   get a new spider request to add to doledb
	// . no, because there might be more on disk from the same firstip
	//   so comment this out again
	//if ( srep->m_fromInjectionRequest )
	//	return true;

	// clear error for this
	g_errno = 0;

	// . update the latest crawl delay for this domain
	// . only add to the table if we had a crawl delay
	// . -1 implies an invalid or unknown crawl delay
	// . we have to store crawl delays of -1 now so we at least know we
	//   tried to download the robots.txt (todo: verify that!)
	//   and the webmaster did not have one. then we can 
	//   crawl more vigorously...
	//if ( srep->m_crawlDelayMS >= 0 ) {

	bool update = false;
	// use the domain hash for this guy! since its from robots.txt
	int32_t *cdp = (int32_t *)m_cdTable.getValue32(srep->m_domHash32);
	// update it only if better or empty
	if ( ! cdp ) update = true;

	// no update if injecting or from pagereindex (docid based spider request)
	if ( srep->m_fromInjectionRequest )
		update = false;

	//else if (((*cdp)&0xffffffff)<(uint32_t)srep->m_spideredTime) 
	//	update = true;
	// update m_sniTable if we should
	if ( update ) {
		// . make new data for this key
		// . lower 32 bits is the spideredTime
		// . upper 32 bits is the crawldelay
		int32_t nv = (int32_t)(srep->m_crawlDelayMS);
		// shift up
		//nv <<= 32;
		// or in time
		//nv |= (uint32_t)srep->m_spideredTime;
		// just direct update if faster
		if      ( cdp ) *cdp = nv;
		// store it anew otherwise
		else if ( ! m_cdTable.addKey(&srep->m_domHash32,&nv)){
			// return false with g_errno set on error
			//return false;
			log("spider: failed to add crawl delay for "
			    "firstip=%s",iptoa(srep->m_firstIp));
			// just ignore
			g_errno = 0;
		}
	}

	// . anytime we add a reply then
	//   we must update this downloadTable with the replies 
	//   SpiderReply::m_downloadEndTime so we can obey sameIpWait
	// . that is the earliest that this url can be respidered, but we
	//   also have a sameIpWait constraint we have to consider...
	// . we alone our responsible for adding doledb recs from this ip so
	//   this is easy to throttle...
	// . and make sure to only add to this download time hash table if
	//   SpiderReply::m_downloadEndTime is non-zero, because zero means
	//   no download happened. (TODO: check this)
	// . TODO: consult crawldelay table here too! use that value if is
	//   less than our sameIpWait
	// . make m_lastDownloadTable an rdbcache ...
	// . this is 0 for pagereindex docid-based replies
	if ( srep->m_downloadEndTime )
		m_lastDownloadCache.addLongLong ( m_collnum,
						  srep->m_firstIp ,
						  srep->m_downloadEndTime );
	// log this for now
	if ( g_conf.m_logDebugSpider )
		log("spider: adding spider reply, download end time %"INT64" for "
		    "ip=%s(%"UINT32") uh48=%"UINT64" indexcode=\"%s\" coll=%"INT32" "
		    "k.n1=%"UINT64" k.n0=%"UINT64"",
		    //"to SpiderColl::m_lastDownloadCache",
		    srep->m_downloadEndTime,
		    iptoa(srep->m_firstIp),
		    (uint32_t)srep->m_firstIp,
		    srep->getUrlHash48(),
		    mstrerror(srep->m_errCode),
		    (int32_t)m_collnum,
		    srep->m_key.n1,
		    srep->m_key.n0);
	
	// ignore errors from that, it's just a cache
	g_errno = 0;
	// sanity check - test cache
	//if ( g_conf.m_logDebugSpider && srep->m_downloadEndTime ) {
	//	int64_t last = m_lastDownloadCache.getLongLong ( m_collnum ,
	//						     srep->m_firstIp ,
	//							   -1,// maxAge
	//							   true );//pro
	//	if ( last != srep->m_downloadEndTime ) { char *xx=NULL;*xx=0;}
	//}

	// skip:

	// . add to wait tree and let it populate doledb on its batch run
	// . use a spiderTime of 0 which means unknown and that it needs to
	//   scan spiderdb to get that
	// . returns false if did not add to waiting tree
	// . returns false sets g_errno on error
	bool added = addToWaitingTree ( 0LL, srep->m_firstIp , true );

	// ignore errors i guess
	g_errno = 0;

	// if added to waiting tree, bail now, needs to scan spiderdb
	// in order to add to doledb, because it won't add to waiting tree
	// if we already have spiderrequests in doledb for this firstip
	if ( added ) return true;

	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );

	return true;
}


void SpiderColl::removeFromDoledbTable ( int32_t firstIp ) {

	// . decrement doledb table ip count for firstIp
	// . update how many per ip we got doled
	int32_t *score = (int32_t *)m_doleIpTable.getValue32 ( firstIp );

	// wtf! how did this spider without being doled?
	if ( ! score ) {
		//if ( ! srep->m_fromInjectionRequest )
		log("spider: corruption. received spider reply whose "
		    "ip has no entry in dole ip table. firstip=%s",
		    iptoa(firstIp));
		return;
	}

	// reduce it
	*score = *score - 1;

	// now we log it too
	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: removed ip=%s from doleiptable "
		    "(newcount=%"INT32")", iptoa(firstIp),*score);


	// remove if zero
	if ( *score == 0 ) {
		// this can file if writes are disabled on this hashtablex
		// because it is saving
		m_doleIpTable.removeKey ( &firstIp );
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0; }
	}
	// wtf!
	if ( *score < 0 ) { char *xx=NULL;*xx=0; }
	// all done?
	if ( g_conf.m_logDebugSpider ) {
		// log that too!
		logf(LOG_DEBUG,"spider: discounting firstip=%s to %"INT32"",
		     iptoa(firstIp),*score);
	}
}


bool SpiderColl::isInDupCache ( SpiderRequest *sreq , bool addToCache ) {

	// init dup cache?
	if ( ! m_dupCache.isInitialized() )
		// use 50k i guess of 64bit numbers and linked list info
		m_dupCache.init ( 90000, 
				  4 , // fixeddatasize (don't really need this)
				  false, // list support?
				  5000, // maxcachenodes
				  false, // usehalfkeys?
				  "urldups", // dbname
				  false, // loadfromdisk
				  12, // cachekeysize
				  0, // datakeysize
				  -1 ); // numptrsmax

	// quit add dups over and over again...
	int64_t dupKey64 = sreq->getUrlHash48();
	// . these flags make big difference in url filters
	// . NOTE: if you see a url that is not getting spidered that should be it might
	//   be because we are not incorporating other flags here...
	if ( sreq->m_fakeFirstIp ) dupKey64 ^= 12345;
	if ( sreq->m_isAddUrl    ) dupKey64 ^= 49387333;
	if ( sreq->m_isInjecting ) dupKey64 ^= 3276404;
	if ( sreq->m_isPageReindex) dupKey64 ^= 32999604;
	if ( sreq->m_forceDelete ) dupKey64 ^= 29386239;
	if ( sreq->m_hadReply    ) dupKey64 ^= 293294099;
	if ( sreq->m_sameDom     ) dupKey64 ^= 963493311;
	if ( sreq->m_sameHost    ) dupKey64 ^= 288844772;
	if ( sreq->m_sameSite    ) dupKey64 ^= 58320355;

	// . maxage=86400,promoteRec=yes. returns -1 if not in there
	// . dupKey64 is for hopcount 0, so if this url is in the dupcache
	//   with a hopcount of zero, do not add it
	if ( m_dupCache.getLong ( 0,dupKey64,86400,true ) != -1 ) {
	dedup:
		if ( g_conf.m_logDebugSpider )
			log("spider: skipping dup request url=%s uh48=%"UINT64"",
			    sreq->m_url,sreq->getUrlHash48());
		return true;
	}
	// if our hopcount is 2 and there is a hopcount 1 in there, do not add
	if ( sreq->m_hopCount >= 2 &&
	     m_dupCache.getLong ( 0,dupKey64 ^ 0x01 ,86400,true ) != -1 ) 
		goto dedup;
	// likewise, if there's a hopcount 2 in there, do not add if we are 3+
	if ( sreq->m_hopCount >= 3 &&
	     m_dupCache.getLong ( 0,dupKey64 ^ 0x02 ,86400,true ) != -1 ) 
		goto dedup;


	if ( ! addToCache ) return false;

	int32_t hc = sreq->m_hopCount;
	// limit hopcount to 3 for making cache key so we don't flood cache
	if ( hc >= 3 ) hc = 3;
	// mangle the key with hopcount before adding it to the cache
	dupKey64 ^= hc;
	// add it
	m_dupCache.addLong(0,dupKey64 ,1);

	return false;
}

// . Rdb.cpp calls SpiderColl::addSpiderRequest/Reply() for every positive
//   spiderdb record it adds to spiderdb. that way our cache is kept 
//   uptodate incrementally
// . returns false and sets g_errno on error
// . if the spiderTime appears to be AFTER m_nextReloadTime then we should
//   not add this spider request to keep the cache trimmed!!! (MDW: TODO)
// . BUT! if we have 150,000 urls that is going to take a int32_t time to
//   spider, so it should have a high reload rate!
bool SpiderColl::addSpiderRequest ( SpiderRequest *sreq , int64_t nowGlobalMS ) {
	// don't add negative keys or data less thangs
	if ( sreq->m_dataSize <= 0 ) {
		log( "spider: add spider request is dataless for uh48=%"UINT64"", sreq->getUrlHash48() );
		return true;
	}

	// . are we already more or less in spiderdb? true = addToCache
	// . put this above isAssignedToUs() so we *try* to keep twins in sync because
	//   Rdb.cpp won't add the spiderrequest if its in this dup cache, and we add
	//   it to the dupcache here...
	if ( isInDupCache ( sreq , true ) ) {
		logDebug( g_conf.m_logDebugSpider, "spider: skipping dup request url=%s uh48=%" UINT64,
		          sreq->m_url, sreq->getUrlHash48() );
		return true;
	}

	// skip if not assigned to us for doling
	if ( ! isAssignedToUs ( sreq->m_firstIp ) ) {
		logDebug( g_conf.m_logDebugSpider, "spider: spider request not assigned to us. skipping." );
		return true;
	}

	// . get the url's length contained in this record
	// . it should be NULL terminated
	// . we set the ip here too
	int32_t ulen = sreq->getUrlLen();
	// watch out for corruption
	if ( sreq->m_firstIp ==  0 || sreq->m_firstIp == -1 || ulen <= 0 ) {
		log("spider: Corrupt spider req with url length of "
		    "%"INT32" <= 0 u=%s. dataSize=%"INT32" firstip=%"INT32" uh48=%"UINT64". Skipping.",
		    ulen,sreq->m_url,
		    sreq->m_dataSize,sreq->m_firstIp,sreq->getUrlHash48());
		return true;
	}

	// update crawlinfo stats here and not in xmldoc so that we count
	// seeds and bulk urls added from add url and can use that to
	// determine if the collection is empty of urls or not for printing
	// out the colored bullets in printCollectionNavBar() in Pages.cpp.
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
	if ( cr ) {
		cr->m_localCrawlInfo .m_urlsHarvested++;
		cr->m_globalCrawlInfo.m_urlsHarvested++;
		cr->m_needsSave = true;
	}

	// . if already have a request in doledb for this firstIp, forget it!
	// . TODO: make sure we remove from doledb first before adding this
	//   spider request
	// . NOW: allow it in if different priority!!! so maybe hash the
	//   priority in with the firstIp???
	// . we really just need to add it if it beats what is currently
	//   in doledb. so maybe store the best priority doledb in the
	//   data value part of the doleiptable...? therefore we should
	//   probably move this check down below after we get the priority
	//   of the spider request.
	//char *val = (char *)m_doleIpTable.getValue ( &sreq->m_firstIp );
	//if ( val && *val > 0 ) {
	//	if ( g_conf.m_logDebugSpider )
	//		log("spider: request IP already in dole table");
	//	return true;
	//}

	// . skip if already in wait tree
	// . no, no. what if the current url for this firstip is not due to
	//   be spidered until 24 hrs and we are adding a url from this firstip
	//   that should be spidered now...
	//if ( m_waitingTable.isInTable ( &sreq->m_firstIp ) ) {
	//	if ( g_conf.m_logDebugSpider )
	//		log("spider: request already in waiting table");
	//	return true;
	//}


	// . we can't do this because we do not have the spiderReply!!!???
	// . MDW: no, we have to do it because tradesy.com has links to twitter
	//   on every page and twitter is not allowed so we continually
	//   re-scan a big spiderdblist for twitter's firstip. major performace
	//   degradation. so try to get ufn without reply. if we need
	//   a reply to get the ufn then this function should return -1 which
	//   means an unknown ufn and we'll add to waiting tree.
	// get ufn/priority,because if filtered we do not want to add to doledb
	int32_t ufn ;
	// HACK: set isOutlink to true here since we don't know if we have sre
	ufn = ::getUrlFilterNum(sreq,NULL,nowGlobalMS,false,MAX_NICENESS,m_cr,
				true,//isoutlink? HACK!
				NULL,// quota table quotatable
				-1 );  // langid not valid

	// spiders disabled for this row in url filters?
	if ( ufn >= 0 && m_cr->m_maxSpidersPerRule[ufn] == 0 ) {
		logDebug( g_conf.m_logDebugSpider, "spider: request spidersoff ufn=%" INT32" url=%s", ufn, sreq->m_url );
		return true;
	}

	// set the priority (might be the same as old)
	int32_t priority = -1;
	if ( ufn >= 0 ) priority = m_cr->m_spiderPriorities[ufn];

	// sanity checks
	if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

	// do not add to doledb if bad
	if ( m_cr->m_forceDelete[ufn] ) {
		logDebug( g_conf.m_logDebugSpider, "spider: request %s is filtered ufn=%" INT32, sreq->m_url, ufn );
		return true;
	}

	if ( m_cr->m_forceDelete[ufn] ) {
		logDebug( g_conf.m_logDebugSpider, "spider: request %s is banned ufn=%" INT32, sreq->m_url, ufn );
		return true;
	}

	// once in waiting tree, we will scan waiting tree and then lookup
	// each firstIp in waiting tree in spiderdb to get the best
	// SpiderRequest for that firstIp, then we can add it to doledb
	// as int32_t as it can be spidered now
	//bool status = addToWaitingTree ( spiderTimeMS,sreq->m_firstIp,true);
	bool added = addToWaitingTree ( 0 , sreq->m_firstIp , true );

	// if already doled and we beat the priority/spidertime of what
	// was doled then we should probably delete the old doledb key
	// and add the new one. hmm, the waitingtree scan code ...

	// update the latest siteNumInlinks count for this "site"
	if ( sreq->m_siteNumInlinksValid ) {
		// updates m_siteNumInlinksTable
		updateSiteNumInlinksTable ( sreq->m_siteHash32 , 
					    sreq->m_siteNumInlinks ,
					    (time_t)sreq->m_addedTime );
		// clear error for this if there was any
		g_errno = 0;
	}

	// log it
	logDebug( g_conf.m_logDebugSpider, "spider: %s request to waiting tree %s"
	          " uh48=%" UINT64
	          " firstIp=%s "
	          " pageNumInlinks=%" UINT32
	          " parentdocid=%" UINT64
	          " isinjecting=%" INT32
	          " ispagereindex=%" INT32
	          " ufn=%" INT32
	          " priority=%" INT32
	          " addedtime=%" UINT32,
	          added ? "ADDED" : "DIDNOTADD",
	          sreq->m_url,
	          sreq->getUrlHash48(),
	          iptoa(sreq->m_firstIp),
	          (uint32_t)sreq->m_pageNumInlinks,
	          sreq->getParentDocId(),
	          (int32_t)(bool)sreq->m_isInjecting,
	          (int32_t)(bool)sreq->m_isPageReindex,
	          (int32_t)sreq->m_ufn,
	          (int32_t)sreq->m_priority,
	          (uint32_t)sreq->m_addedTime );

	return true;
}

bool SpiderColl::printWaitingTree ( ) {
	int32_t node = m_waitingTree.getFirstNode();
	for ( ; node >= 0 ; node = m_waitingTree.getNextNode(node) ) {
		key_t *wk = (key_t *)m_waitingTree.getKey (node);
		// spider time is up top
		uint64_t spiderTimeMS = (wk->n1);
		spiderTimeMS <<= 32;
		spiderTimeMS |= ((wk->n0) >> 32);
		// then ip
		int32_t firstIp = wk->n0 & 0xffffffff;
		// show it
		log("dump: time=%"INT64" firstip=%s",spiderTimeMS,iptoa(firstIp));
	}
	return true;
}


//////
//
// . 1. called by addSpiderReply(). it should have the sameIpWait available
//      or at least that will be in the crawldelay cache table.
//      SpiderReply::m_crawlDelayMS. Unfortunately, no maxSpidersPerIP!!!
//      we just add a "0" in the waiting tree which means evalIpLoop() will
//      be called and can get the maxSpidersPerIP from the winning candidate
//      and add to the waiting tree based on that.
// . 2. called by addSpiderRequests(). It SHOULD maybe just add a "0" as well
//      to offload the logic. try that.
// . 3. called by populateWaitingTreeFromSpiderdb(). it just adds "0" as well,
//      if not doled
// . 4. UPDATED in evalIpLoop() if the best SpiderRequest for a firstIp is
//      in the future, this is the only time we will add a waiting tree key
//      whose spider time is non-zero. that is where we also take 
//      sameIpWait and maxSpidersPerIP into consideration. evalIpLoop() 
//      will actually REMOVE the entry from the waiting tree if that IP
//      already has the max spiders outstanding per IP. when a spiderReply
//      is received it will populate the waiting tree again with a "0" entry
//      and evalIpLoop() will re-do its check.
//
//////

// . returns true if we added to waiting tree, false if not
// . if one of these add fails consider increasing mem used by tree/table
// . if we lose an ip that sux because it won't be gotten again unless
//   we somehow add another request/reply to spiderdb in the future
bool SpiderColl::addToWaitingTree ( uint64_t spiderTimeMS, int32_t firstIp, bool callForScan ) {
	logDebug( g_conf.m_logDebugSpider, "spider: addtowaitingtree ip=%s", iptoa( firstIp ) );

	// we are currently reading spiderdb for this ip and trying to find
	// a best SpiderRequest or requests to add to doledb. so if this 
	// happens, let the scan know that more replies or requests came in
	// while we were scanning so that it should not delete the rec from
	// waiting tree and not add to doledb, then we'd lose it forever or
	// until the next waitingtree rebuild was triggered in time.
	//
	// Before i was only setting this in addSpiderRequest() so if a new
	// reply came in it was not setting m_gotNewDataForScanninIp and
	// we ended up losing the IP from the waiting tree forever (or until
	// the next timed rebuild). putting it here seems to fix that.
	if ( firstIp == m_scanningIp ) {
		m_gotNewDataForScanningIp = m_scanningIp;
		//log("spider: got new data for %s",iptoa(firstIp));
		//return true;
	}

	// . this can now be only 0
	// . only evalIpLoop() will add a waiting tree key with a non-zero
	//   value after it figures out the EARLIEST time that a 
	//   SpiderRequest from this firstIp can be spidered.
	if ( spiderTimeMS != 0 ) { char *xx=NULL;*xx=0; }

	// waiting tree might be saving!!!
	if ( ! m_waitingTree.m_isWritable ) {
		log( LOG_WARN, "spider: addtowaitingtree: failed. is not writable. saving?" );
		return false;
	}

	// only if we are the responsible host in the shard
	if ( ! isAssignedToUs ( firstIp ) ) 
		return false;

	// . do not add to waiting tree if already in doledb
	// . an ip should not exist in both doledb and waiting tree.
	// . waiting tree is meant to be a signal that we need to add
	//   a spiderrequest from that ip into doledb where it can be picked
	//   up for immediate spidering
	if ( m_doleIpTable.isInTable ( &firstIp ) ) {
		logDebug( g_conf.m_logDebugSpider, "spider: not adding to waiting tree, already in doleip table" );
		return false;
	}

	// sanity check
	// i think this trigged on gk209 during an auto-save!!! FIX!
	if ( ! m_waitingTree.m_isWritable ) { char *xx=NULL; *xx=0; }

	// see if in tree already, so we can delete it and replace it below
	int32_t ws = m_waitingTable.getSlot ( &firstIp ) ;

	// . this is >= 0 if already in tree
	// . if spiderTimeMS is a sooner time than what this firstIp already
	//   has as its earliest time, then we will override it and have to
	//   update both m_waitingTree and m_waitingTable, however
	//   IF the spiderTimeMS is a later time, then we bail without doing
	//   anything at this point.
	if ( ws >= 0 ) {
		// get timems from waiting table
		int64_t sms = m_waitingTable.getScore64FromSlot(ws);

		// make the key then
		key_t wk = makeWaitingTreeKey ( sms, firstIp );
		// must be there
		int32_t tn = m_waitingTree.getNode ( (collnum_t)0, (char *)&wk );
		// sanity check. ensure waitingTable and waitingTree in sync
		if ( tn < 0 ) { char *xx=NULL;*xx=0; }

		// not only must we be a sooner time, but we must be 5-seconds
		// sooner than the time currently in there to avoid thrashing
		// when we had a ton of outlinks with this first ip within an
		// 5-second interval.
		//
		// i'm not so sure what i was doing here before, but i don't
		// want to starve the spiders, so make this 100ms not 5000ms
		if ( (int64_t)spiderTimeMS > sms - 100 ) {
			logDebug( g_conf.m_logDebugSpider, "spider: skip updating waiting tree" );
			return false;
		}

		// log the replacement
		logDebug( g_conf.m_logDebugSpider, "spider: replacing waitingtree key oldtime=%" UINT32" newtime=%" UINT32" firstip=%s",
		          (uint32_t)(sms/1000LL),
		          (uint32_t)(spiderTimeMS/1000LL),
		          iptoa( firstIp ) );

		// remove from tree so we can add it below
		m_waitingTree.deleteNode3 ( tn , false );
	} else {
		// time of 0 means we got the reply for something we spidered
		// in doledb so we will need to recompute the best spider
		// requests for this first ip

		// log the replacement
		logDebug( g_conf.m_logDebugSpcache, "spider: adding new key to waitingtree newtime=%" UINT32"%s firstip=%s",
		          (uint32_t)(spiderTimeMS/1000LL),
		          ( spiderTimeMS == 0 ) ? "(replyreset)" : "",
		          iptoa( firstIp ) );
	}

	// make the key
	key_t wk = makeWaitingTreeKey ( spiderTimeMS, firstIp );
	// what is this?
	if ( firstIp == 0 || firstIp == -1 ) {
		log("spider: got ip of %s. cn=%"INT32" "
		    "wtf? failed to add to "
		    "waiting tree, but return true anyway.",
		    iptoa(firstIp) ,
		    (int32_t)m_collnum);
		// don't return true lest m_nextKey2 never gets updated
		// and we end up in an infinite loop doing 
		// populateWaitingTreeFromSpiderdb()
		return true;
	}

	// grow the tree if too small!
	int32_t used = m_waitingTree.getNumUsedNodes();
	int32_t max =  m_waitingTree.getNumTotalNodes();
	
	if ( used + 1 > max ) {
		int32_t more = (((int64_t)used) * 15) / 10;
		if ( more < 10 ) more = 10;
		if ( more > 100000 ) more = 100000;
		int32_t newNum = max + more;
		log("spider: growing waiting tree to from %"INT32" to %"INT32" nodes "
		    "for collnum %"INT32"",
		    max , newNum , (int32_t)m_collnum );
		if ( ! m_waitingTree.growTree ( newNum , MAX_NICENESS ) )
			return log("spider: failed to grow waiting tree to "
				   "add firstip %s",iptoa(firstIp) );
		if ( ! m_waitingTable.setTableSize ( newNum , NULL , 0 ) )
			return log("spider: failed to grow waiting table to "
				   "add firstip %s",iptoa(firstIp) );
	}


	// add that
	int32_t wn;
	if ( ( wn = m_waitingTree.addKey ( &wk ) ) < 0 ) {
		log("spider: waitingtree add failed ip=%s. increase max nodes "
		    "lest we lose this IP forever. err=%s",
		    iptoa(firstIp),mstrerror(g_errno));
		//char *xx=NULL; *xx=0;
		return false;
	}

	// note it
	logDebug( g_conf.m_logDebugSpider, "spider: added time=%"INT64" ip=%s to waiting tree scan=%" INT32" node=%" INT32,
	          spiderTimeMS , iptoa( firstIp ), (int32_t)callForScan, wn );

	// add to table now since its in the tree
	if ( ! m_waitingTable.addKey ( &firstIp , &spiderTimeMS ) ) {
		// remove from tree then
		m_waitingTree.deleteNode3 ( wn , false );
		//log("spider: 5 del node %"INT32" for %s",wn,iptoa(firstIp));
		return false;
	}
	// . kick off a scan, i don't care if this blocks or not!
	// . the populatedoledb loop might already have a scan in progress
	//   but usually it won't, so rather than wait for its sleepwrapper
	//   to be called we force it here for speed.
	// . re-entry is false because we are entering for the first time
	// . calling this everytime msg4 adds a spider request is super slow!!!
	//   SO TAKE THIS OUT FOR NOW
	// . no that was not it. mdw. put it back.
	if ( callForScan ) populateDoledbFromWaitingTree ( );
	// tell caller there was no error
	return true;
}

// . this scan is started anytime we call addSpiderRequest() or addSpiderReply
// . if nothing is in tree it quickly exits
// . otherwise it scan the entries in the tree
// . each entry is a key with spiderTime/firstIp
// . if spiderTime > now it stops the scan
// . if the firstIp is already in doledb (m_doleIpTable) then it removes
//   it from the waitingtree and waitingtable. how did that happen?
// . otherwise, it looks up that firstIp in spiderdb to get a list of all
//   the spiderdb recs from that firstIp
// . then it selects the "best" one and adds it to doledb. once added to
//   doledb it adds it to doleIpTable, and remove from waitingtree and 
//   waitingtable
// . returns false if blocked, true otherwise
int32_t SpiderColl::getNextIpFromWaitingTree ( ) {

	// if nothing to scan, bail
	if ( m_waitingTree.isEmpty() ) return 0;
	// reset first key to get first rec in waiting tree
	m_waitingTreeKey.setMin();
	// current time on host #0
	uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
 top:

	// we might have deleted the only node below...
	if ( m_waitingTree.isEmpty() ) return 0;

	// assume none
	int32_t firstIp = 0;
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode ( 0, (char *)&m_waitingTreeKey );
	// if empty, stop
	if ( node < 0 ) return 0;

	// get the key
	key_t *k = (key_t *)m_waitingTree.getKey ( node );

	// ok, we got one
	firstIp = (k->n0) & 0xffffffff;

	// sometimes we take over for a dead host, but if he's no longer
	// dead then we can remove his keys. but first make sure we have had
	// at least one ping from him so we do not remove at startup.
	// if it is in doledb or in the middle of being added to doledb 
	// via msg4, nuke it as well!
	if ( ! isAssignedToUs (firstIp) || m_doleIpTable.isInTable(&firstIp)) {
		// only delete if this host is alive and has sent us a ping
		// before so we know he was up at one time. this way we do not
		// remove all his keys just because we restarted and think he
		// is alive even though we have gotten no ping from him.
		//if ( hp->m_numPingRequests > 0 )
	removeFromTree:
		// these operations should fail if writes have been disabled
		// and becase the trees/tables for spidercache are saving
		// in Process.cpp's g_spiderCache::save() call
		m_waitingTree.deleteNode3 ( node , true );
		//log("spdr: 8 del node node %"INT32" for %s",node,iptoa(firstIp));
		// note it
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: removed1 ip=%s from waiting "
			    "tree. nn=%"INT32"",
			    iptoa(firstIp),m_waitingTree.m_numUsedNodes);

		// log it
		if ( g_conf.m_logDebugSpcache )
			log("spider: erasing waitingtree key firstip=%s",
			    iptoa(firstIp) );
		// remove from table too!
		m_waitingTable.removeKey  ( &firstIp );
		goto top;
	}

	// spider time is up top
	uint64_t spiderTimeMS = (k->n1);
	spiderTimeMS <<= 32;
	spiderTimeMS |= ((k->n0) >> 32);
	// stop if need to wait for this one
	if ( spiderTimeMS > nowMS ) return 0;
	// sanity
	if ( (int64_t)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
	// save key for deleting when done
	m_waitingTreeKey.n1 = k->n1;
	m_waitingTreeKey.n0 = k->n0;
	m_waitingTreeKeyValid = true;
	m_scanningIp = firstIp;
	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { 
		//char *xx=NULL;*xx=0; }
		log("spider: removing corrupt spiderreq firstip of %"INT32
		    " from waiting tree collnum=%i",
		    firstIp,(int)m_collnum);
		goto removeFromTree;
	}
	// avoid corruption
	
	// we set this to true when done
	//m_isReadDone = false;
	// compute the best request from spiderdb list, not valid yet
	//m_bestRequestValid = false;
	m_lastReplyValid   = false;

	// start reading spiderdb here
	m_nextKey = g_spiderdb.makeFirstKey(firstIp);
	m_endKey  = g_spiderdb.makeLastKey (firstIp);
	// all done
	return firstIp;
}

uint64_t SpiderColl::getNextSpiderTimeFromWaitingTree ( ) {
	// if nothing to scan, bail
	if ( m_waitingTree.isEmpty() ) return 0LL;
	// the key
	key_t mink; mink.setMin();
	// set node from wait tree key. this way we can resume from a prev key
	int32_t node = m_waitingTree.getNextNode (0,(char *)&mink );
	// if empty, stop
	if ( node < 0 ) return 0LL;
	// get the key
	key_t *wk = (key_t *)m_waitingTree.getKey ( node );
	// time from that
	uint64_t spiderTimeMS = (wk->n1);
	spiderTimeMS <<= 32;
	spiderTimeMS |= ((wk->n0) >> 32);
	// stop if need to wait for this one
	return spiderTimeMS;
}
	


static void gotSpiderdbListWrapper2( void *state , RdbList *list,Msg5 *msg5) {

	SpiderColl *THIS = (SpiderColl *)state;

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( THIS , "2" ) ) return;

	THIS->m_gettingList2 = false;

	THIS->populateWaitingTreeFromSpiderdb ( true );
}



//////////////////
//////////////////
//
// THE BACKGROUND FUNCTION
//
// when the user changes the ufn table the waiting tree is flushed
// and repopulated from spiderdb with this. also used for repairs.
//
//////////////////
//////////////////

// . this stores an ip into the waiting tree with a spidertime of "0" so
//   it will be evaluate properly by populateDoledbFromWaitingTree()
//
// . scan spiderdb to make sure each firstip represented in spiderdb is
//   in the waiting tree. it seems they fall out over time. we need to fix
//   that but in the meantime this should do a bg repair. and is nice to have
// . the waiting tree key is really just a spidertime and a firstip. so we will
//   still need populatedoledbfromwaitingtree to periodically scan firstips
//   that are already in doledb to see if it has a higher-priority request
//   for that firstip. in which case it can add that to doledb too, but then
//   we have to be sure to only grant one lock for a firstip to avoid hammering
//   that firstip
// . this should be called from a sleepwrapper, the same sleep wrapper we
//   call populateDoledbFromWaitingTree() from should be fine
void SpiderColl::populateWaitingTreeFromSpiderdb ( bool reentry ) {
 
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
	
	// skip if in repair mode
	if ( g_repairMode ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, in repair mode" );
		return;
	}
	
	// sanity
	if ( m_deleteMyself ) { char *xx=NULL;*xx=0; }
	// skip if spiders off
	if ( ! m_cr->m_spideringEnabled ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, spiders disabled" );
		return;
	}
	
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, spiders disabled (2)" );
		return;
	}
		
		
	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, UDP table full" );
		return;
	}
		
	// if entering for the first time, we need to read list from spiderdb
	if ( ! reentry ) {
		// just return if we should not be doing this yet
		if ( ! m_waitingTreeNeedsRebuild ) 
		{
			logTrace( g_conf.m_logTraceSpider, "END, !m_waitingTreeNeedsRebuild" );
			return;
		}
		
		// a double call? can happen if list read is slow...
		if ( m_gettingList2 ) 
		{
			logTrace( g_conf.m_logTraceSpider, "END, double call" );
			return;
		}

		// . borrow a msg5
		// . if none available just return, we will be called again
		//   by the sleep/timer function

		// . read in a replacement SpiderRequest to add to doledb from
		//   this ip
		// . get the list of spiderdb records
		// . do not include cache, those results are old and will mess
		//   us up
		log(LOG_DEBUG,"spider: populateWaitingTree: calling msg5: startKey=0x%"XINT64",0x%"XINT64" firstip=%s",
		    m_nextKey2.n1, m_nextKey2.n0, iptoa(g_spiderdb.getFirstIp(&m_nextKey2)));
		    
		// flag it
		m_gettingList2 = true;
		// make state
		//int32_t state2 = (int32_t)m_cr->m_collnum;
		// read the list from local disk
		if ( ! m_msg5b.getList ( RDB_SPIDERDB   ,
					 m_cr->m_collnum,
					 &m_list2       ,
					 &m_nextKey2    ,
					 &m_endKey2     ,
					 SR_READ_SIZE   , // minRecSizes (512k)
					 true           , // includeTree
					 false          , // addToCache
					 0              , // max cache age
					 0              , // startFileNum
					 -1             , // numFiles (all)
					 this,//(void *)state2,//this//state
					 gotSpiderdbListWrapper2 ,
					 MAX_NICENESS   , // niceness
					 true          )) // do error correct?
		{
			// return if blocked
			logTrace( g_conf.m_logTraceSpider, "END, msg5b.getList blocked" );
			return;
		}
	}

	// show list stats
	logDebug( g_conf.m_logDebugSpider, "spider: populateWaitingTree: got list of size %" INT32, m_list2.m_listSize );

	// unflag it
	m_gettingList2 = false;
	// stop if we are done
	//if ( m_isReadDone2 ) return;

	// if waitingtree is locked for writing because it is saving or
	// writes were disabled then just bail and let the scan be re-called
	// later
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, waitingTree not writable at the moment" );
		return;
	}

	// shortcut
	RdbList *list = &m_list2;
	// ensure we point to the top of the list
	list->resetListPtr();
	// bail on error
	if ( g_errno ) {
		log("spider: Had error getting list of urls from spiderdb2: %s.", mstrerror(g_errno));
		//m_isReadDone2 = true;
		logTrace( g_conf.m_logTraceSpider, "END" );
		return;
	}

	int32_t lastOne = 0;
	// loop over all serialized spiderdb records in the list
	for ( ; ! list->isExhausted() ; ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get spiderdb rec in its serialized form
		char *rec = list->getCurrentRec();
		// skip to next guy
		list->skipCurrentRecord();
		// negative? wtf?
		if ( (rec[0] & 0x01) == 0x00 ) {
			//logf(LOG_DEBUG,"spider: got negative spider rec");
			continue;
		}
		// if its a SpiderReply skip it
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec)) 
		{
			continue;
		}
			
		// cast it
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// get first ip
		int32_t firstIp = sreq->m_firstIp;
		// corruption?
		// if ( firstIp == 0 || firstIp == -1 )
		// 	gotCorruption = true;
		// if same as last, skip it
		if ( firstIp == lastOne ) 
		{
			logTrace( g_conf.m_logTraceSpider, "Skipping, IP [%s] same as last" , iptoa(firstIp));
			continue;
		}

		// set this lastOne for speed
		lastOne = firstIp;
		// check for dmoz. set up gdb on gk157/gk221 to break here
		// so we can see what's going on
		//if ( firstIp == -815809331 )
		//	log("got dmoz");
		// if firstip already in waiting tree, skip it
		if ( m_waitingTable.isInTable ( &firstIp ) ) 
		{
			logTrace( g_conf.m_logTraceSpider, "Skipping, IP [%s] already in waiting tree" , iptoa(firstIp));
			continue;
		}

		// skip if only our twin should add it to waitingtree/doledb
		if ( ! isAssignedToUs ( firstIp ) ) 
		{
			logTrace( g_conf.m_logTraceSpider, "Skipping, IP [%s] not assigned to us" , iptoa(firstIp));
			continue;
		}

		// skip if ip already represented in doledb i guess otherwise
		// the populatedoledb scan will nuke it!!
		if ( m_doleIpTable.isInTable ( &firstIp ) ) 
		{
			logTrace( g_conf.m_logTraceSpider, "Skipping, IP [%s] already in doledb" , iptoa(firstIp));
			continue;
		}

		// not currently spidering either. when they got their
		// lock they called confirmLockAcquisition() which will
		// have added an entry to the waiting table. sometimes the
		// lock still exists but the spider is done. because the
		// lock persists for 5 seconds afterwards in case there was
		// a lock request for that url in progress, so it will be
		// denied.

		// . this is starving other collections , should be
		//   added to waiting tree anyway! otherwise it won't get
		//   added!!!
		// . so now i made this collection specific, not global
		if ( g_spiderLoop.getNumSpidersOutPerIp (firstIp,m_collnum)>0)
		{
			logTrace( g_conf.m_logTraceSpider, "Skipping, IP [%s] is already being spidered" , iptoa(firstIp));
			continue;
		}

		// otherwise, we want to add it with 0 time so the doledb
		// scan will evaluate it properly
		// this will return false if we are saving the tree i guess
		if ( ! addToWaitingTree ( 0 , firstIp , false ) ) {
			log("spider: failed to add ip %s to waiting tree. "
			    "ip will not get spidered then and our "
			    "population of waiting tree will repeat until "
			    "this add happens."
			    , iptoa(firstIp) );
			logTrace( g_conf.m_logTraceSpider, "END, addToWaitingTree for IP [%s] failed" , iptoa(firstIp));
			return;
		}
		else
		{
			logTrace( g_conf.m_logTraceSpider, "IP [%s] added to waiting tree" , iptoa(firstIp));
		}

		// count it
		m_numAdded++;
		// ignore errors for this
		g_errno = 0;
	}

	// are we the final list in the scan?
	bool shortRead = ( list->getListSize() <= 0);//(int32_t)SR_READ_SIZE) ;

	m_numBytesScanned += list->getListSize();

	// reset? still left over from our first scan?
	if ( m_lastPrintCount > m_numBytesScanned )
		m_lastPrintCount = 0;

	// announce every 10MB
	if ( m_numBytesScanned - m_lastPrintCount > 10000000 ) {
		log("spider: %"UINT64" spiderdb bytes scanned for waiting tree "
		    "re-population for cn=%"INT32"",m_numBytesScanned,
		    (int32_t)m_collnum);
		m_lastPrintCount = m_numBytesScanned;
	}

	// debug info
	log(LOG_DEBUG,"spider: Read2 %"INT32" spiderdb bytes.",list->getListSize());
	// reset any errno cuz we're just a cache
	g_errno = 0;

	// if not done, keep going
	if ( ! shortRead ) {
		// . inc it here
		// . it can also be reset on a collection rec update
		key128_t lastKey  = *(key128_t *)list->getLastKey();

		if ( lastKey < m_nextKey2 ) {
			log("spider: got corruption 9. spiderdb "
			    "keys out of order for "
			    "collnum=%"INT32, (int32_t)m_collnum);
			g_corruptCount++;
			// this should result in an empty list read for
			// our next scan of spiderdb. unfortunately we could
			// miss a lot of spider requests then
			m_nextKey2  = m_endKey2;
		}
		else {
			m_nextKey2  = lastKey;
			m_nextKey2 += (uint32_t) 1;
		}

		// watch out for wrap around
		if ( m_nextKey2 < lastKey ) shortRead = true;
		// nah, advance the firstip, should be a lot faster when
		// we are only a few firstips...
		if ( lastOne && lastOne != -1 ) { // && ! gotCorruption ) {
			key128_t cand = g_spiderdb.makeFirstKey(lastOne+1);
			// corruption still seems to happen, so only
			// do this part if it increases the key to avoid
			// putting us into an infinite loop.
			if ( cand > m_nextKey2 ) m_nextKey2 = cand;
		}
	}

	if ( shortRead ) {
		// mark when the scan completed so we can do another one
		// like 24 hrs from that...
		m_lastScanTime = getTimeLocal();
		// log it
		// if ( m_numAdded )
		// 	log("spider: added %"INT32" recs to waiting tree from "
		// 	    "scan of %"INT64" bytes coll=%s",
		// 	    m_numAdded,m_numBytesScanned,
		// 	    m_cr->m_coll);
		// note it
		log("spider: rebuild complete for %s. Added %"INT32" recs to waiting tree, scanned %"INT64" bytes of spiderdb.",
		    m_coll,m_numAdded, m_numBytesScanned);
		    
		// reset the count for next scan
		m_numAdded = 0 ;
		m_numBytesScanned = 0;
		// reset for next scan
		m_nextKey2.setMin();
		// no longer need rebuild
		m_waitingTreeNeedsRebuild = false;
		// and re-send the crawlinfo in handerequestc1 to each host
		// so they no if we have urls ready to spider or not. because
		// if we told them no before we completed this rebuild we might
		// have found some urls.
		// MDW: let's not do this unless we find it is a problem
		//m_cr->localCrawlInfoUpdate();
	}

	// free list to save memory
	list->freeList();
	// wait for sleepwrapper to call us again with our updated m_nextKey2
	logTrace( g_conf.m_logTraceSpider, "END, done" );
	return;
}



//static bool    s_ufnTreeSet = false;
//static RdbTree s_ufnTree;
//static time_t  s_lastUfnTreeFlushTime = 0;

//////////////////////////
//////////////////////////
//
// The first KEYSTONE function.
//
// CALL THIS ANYTIME to load up doledb from waiting tree entries
//
// This is a key function.
//
// It is called from two places:
//
// 1) sleep callback
//
// 2) addToWaitingTree()
//    is called from addSpiderRequest() anytime a SpiderRequest
//    is added to spiderdb (or from addSpiderReply())
//
// It can only be entered once so will just return if already scanning 
// spiderdb.
//
//////////////////////////
//////////////////////////

// . for each IP in the waiting tree, scan all its SpiderRequests and determine
//   which one should be the next to be spidered. and put that one in doledb.
// . we call this a lot, like if the admin changes the url filters table
//   we have to re-scan all of spiderdb basically and re-do doledb
void SpiderColl::populateDoledbFromWaitingTree ( ) { // bool reentry ) {
	
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	// only one loop can run at a time!
	if ( m_isPopulatingDoledb ) {
		logTrace( g_conf.m_logTraceSpider, "END, already populating doledb" );
		return;
	}
	
	// skip if in repair mode
	if ( g_repairMode ) {
		logTrace( g_conf.m_logTraceSpider, "END, in repair mode" );
		return;
	}

	// let's skip if spiders off so we can inject/popoulate the index quick
	// since addSpiderRequest() calls addToWaitingTree() which then calls
	// this. 
	if ( ! g_conf.m_spideringEnabled ) {
		logTrace( g_conf.m_logTraceSpider, "END, spidering not enabled" );
		return;
	}
	
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) {
		logTrace( g_conf.m_logTraceSpider, "END, spidering not enabled (2)" );
		return;
	}


	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) {
		logTrace( g_conf.m_logTraceSpider, "END, no more UDP slots" );
		return;
	}

	// set this flag so we are not re-entered
	m_isPopulatingDoledb = true;
 loop:

	// if waiting tree is being saved, we can't write to it
	// so in that case, bail and wait to be called another time
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable ) {
		m_isPopulatingDoledb = false;
		logTrace( g_conf.m_logTraceSpider, "END, waitingTree not writable at the moment" );
		return;
	}

	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) {
		m_isPopulatingDoledb = false; 
		logTrace( g_conf.m_logTraceSpider, "END, shutting down" );
		return;
	}

	// . get next IP that is due to be spidered from
	// . also sets m_waitingTreeKey so we can delete it easily!
	int32_t ip = getNextIpFromWaitingTree();
	
	// . return if none. all done. unset populating flag.
	// . it returns 0 if the next firstip has a spidertime in the future
	if ( ip == 0 ) {
		m_isPopulatingDoledb = false;
		return;
	}

	// set read range for scanning spiderdb
	m_nextKey = g_spiderdb.makeFirstKey(ip);
	m_endKey  = g_spiderdb.makeLastKey (ip);

	logDebug( g_conf.m_logDebugSpider, "spider: for cn=%i nextip=%s nextkey=%s",
	          (int)m_collnum, iptoa(ip), KEYSTR( &m_nextKey, sizeof( key128_t ) ) );

	//////
	//
	// do TWO PASSES, one to count pages, the other to get the best url!!
	//
	//////
	// assume we don't have to do two passes
	m_countingPagesIndexed = false;

	// get the collectionrec
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	// but if we have quota based url filters we do have to count
	if ( cr && cr->m_urlFiltersHavePageCounts ) {
		// tell evalIpLoop() to count first
		m_countingPagesIndexed = true;
		// reset this stuff used for counting UNIQUE votes
		m_lastReqUh48a = 0LL;
		m_lastReqUh48b = 0LL;
		m_lastRepUh48  = 0LL;
		// and setup the LOCAL counting table if not initialized
		if ( m_localTable.m_ks == 0 ) 
			m_localTable.set (4,4,0,NULL,0,false,0,"ltpct" );
		// otherwise, just reset it so we can repopulate it
		else m_localTable.reset();
	}

	logDebug( g_conf.m_logDebugSpider, "spider: evalIpLoop: waitingtree nextip=%s numUsedNodes=%" INT32,
	          iptoa(ip), m_waitingTree.m_numUsedNodes );

//@@@@@@ BR: THIS SHOULD BE DEBUGGED AND ENABLED

	/*
	// assume using tree
	m_useTree = true;

	// . flush the tree every 12 hours
	// . i guess we could add incoming requests to the ufntree if
	//   they strictly beat the ufn tree tail node, HOWEVER, we 
	//   still have the problem of that if a url we spidered is due
	//   to be respidered very soon we will miss it, as only the reply
	//   is added back into spiderdb, not a new request.
	int32_t nowLocal = getTimeLocal();
	// make it one hour so we don't cock-block a new high priority 
	// request that just got added... crap, what if its an addurl
	// or something like that????
	if ( nowLocal - s_lastUfnTreeFlushTime > 3600 ) {
		s_ufnTree.clear();
		s_lastUfnTreeFlushTime = nowLocal;
	}

	int64_t uh48;

	//
	// s_ufnTree tries to cache the top X spiderrequests for an IP
	// that should be spidered next so we do not have to scan like
	// a million spiderrequests in spiderdb to find the best one.
	//

	// if we have a specific uh48 targetted in s_ufnTree then that
	// saves a ton of time!
	// key format for s_ufnTree:
	// iiiiiiii iiiiiiii iiiiiii iiiiiii  i = firstip
	// PPPPPPPP tttttttt ttttttt ttttttt  P = priority
	// tttttttt tttttttt hhhhhhh hhhhhhh  t = spiderTimeMS (40 bits)
	// hhhhhhhh hhhhhhhh hhhhhhh hhhhhhh  h = urlhash48
	key128_t key;
	key.n1 = ip;
	key.n1 <<= 32;
	key.n0 = 0LL;
	int32_t node = s_ufnTree.getNextNode(0,(char *)&key);
	// cancel node if not from our ip
	if ( node >= 0 ) {
		key128_t *rk = (key128_t *)s_ufnTree.getKey ( node );
		if ( (rk->n1 >> 32) != (uint32_t)ip ) node = -1;
	}
	if ( node >= 0 ) {
		// get the key
		key128_t *nk = (key128_t *)s_ufnTree.getKey ( node );
		// parse out uh48
		uh48 = nk->n0;
		// mask out spidertimems
		uh48 &= 0x0000ffffffffffffLL;
		// use that to refine the key range immensley!
		m_nextKey = g_spiderdb.makeFirstKey2 (ip, uh48);
		m_endKey  = g_spiderdb.makeLastKey2  (ip, uh48);
		// do not add the recs to the tree!
		m_useTree = false;
	}
	*/

	// turn this off until we figure out why it sux
	m_useTree = false;

	// so we know if we are the first read or not...
	m_firstKey = m_nextKey;

	// . initialize this before scanning the spiderdb recs of an ip
	// . it lets us know if we recvd new spider requests for m_scanningIp
	//   while we were doing the scan
	m_gotNewDataForScanningIp = 0;

	m_lastListSize = -1;

	// let evalIpLoop() know it has not yet tried to read from spiderdb
	m_didRead = false;

	// reset this
	int32_t maxWinners = (int32_t)MAX_WINNER_NODES;
	//if ( ! m_cr->m_isCustomCrawl ) maxWinners = 1;

	if ( m_winnerTree.m_numNodes == 0 &&
	     ! m_winnerTree.set ( -1 , // fixeddatasize
				  maxWinners , // maxnumnodes
				  true , // balance?
				  maxWinners * MAX_BEST_REQUEST_SIZE, // memmax
				  true, // owndata?
				  "wintree", // allocname
				  false, // datainptrs?
				  NULL, // dbname
				  sizeof(key192_t), // keysize
				  false, // useprotection?
				  false, // allowdups?
				  -1 ) ) { // rdbid
		m_isPopulatingDoledb = false;
		log("spider: winntree set: %s",mstrerror(g_errno));
		logTrace( g_conf.m_logTraceSpider, "END, after winnerTree.set" );
		return;
	}

	if ( ! m_winnerTable.isInitialized() &&
	     ! m_winnerTable.set ( 8 , // uh48 is key
				   sizeof(key192_t) , // winnertree key is data
				   64 , // 64 slots initially
				   NULL ,
				   0 ,
				   false , // allow dups?
				   MAX_NICENESS ,
				   "wtdedup" ) ) {
		m_isPopulatingDoledb = false;
		log("spider: wintable set: %s",mstrerror(g_errno));
		logTrace( g_conf.m_logTraceSpider, "END, after winnerTable.set" );
		return;
	}

	// clear it before evaluating this ip so it is empty
	m_winnerTree.clear();

	// and table as well now
	m_winnerTable.clear();

	// reset this as well
	m_minFutureTimeMS = 0LL;

	m_totalBytesScanned = 0LL;

	m_totalNewSpiderRequests = 0LL;

	m_lastOverflowFirstIp = 0;
	
	// . look up in spiderdb otherwise and add best req to doledb from ip
	// . if it blocks ultimately it calls gotSpiderdbListWrapper() which
	//   calls this function again with re-entry set to true
	if ( ! evalIpLoop () ) {
		logTrace( g_conf.m_logTraceSpider, "END, after evalIpLoop" );
		return ;
	}

	// oom error? i've seen this happen and we end up locking up!
	if ( g_errno ) { 
		log( "spider: evalIpLoop: %s", mstrerror(g_errno) );
		m_isPopulatingDoledb = false; 
		logTrace( g_conf.m_logTraceSpider, "END, error after evalIpLoop" );
		return; 
	}
	// try more
	goto loop;
}



static void gotSpiderdbListWrapper ( void *state , RdbList *list , Msg5 *msg5){
	SpiderColl *THIS = (SpiderColl *)state;
	// prevent a core
	THIS->m_gettingList1 = false;
	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) return;
	// return if that blocked
	if ( ! THIS->evalIpLoop() ) return;
	// we are done, re-entry popuatedoledb
	THIS->m_isPopulatingDoledb = false;
	// gotta set m_isPopulatingDoledb to false lest it won't work
	THIS->populateDoledbFromWaitingTree ( );
}



///////////////////
//
// KEYSTONE FUNCTION
//
// . READ ALL spiderdb recs for IP of m_scanningIp
// . add winner to doledb
// . called ONLY by populateDoledbFromWaitingTree()
//
// . continually scan spiderdb requests for a particular ip, m_scanningIp
// . compute the best spider request to spider next
// . add it to doledb
// . getNextIpFromWaitingTree() must have been called to set m_scanningIp
//   otherwise m_bestRequestValid might not have been reset to false
//
///////////////////

bool SpiderColl::evalIpLoop ( ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
	//testWinnerTreeKey ( );

	// sanity
	if ( m_scanningIp == 0 || m_scanningIp == -1 ) { char *xx=NULL;*xx=0;}

	// are we trying to exit? some firstip lists can be quite long, so
	// terminate here so all threads can return and we can exit properly
	if ( g_process.m_mode == EXIT_MODE ) {
		logTrace( g_conf.m_logTraceSpider, "END, shutting down" );
		return true;
	}

	// if this ip is in the winnerlistcache use that. it saves
	// us a lot of time.
	key_t cacheKey;
	cacheKey.n0 = m_scanningIp;
	cacheKey.n1 = 0;
	char *doleBuf = NULL;
	int32_t doleBufSize;
	RdbCache *wc = &g_spiderLoop.m_winnerListCache;
	time_t cachedTimestamp = 0;
	bool inCache = false;
	bool useCache = true;
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this ,"6" ) ) return false;

	// if doing site or page quotes for the sitepages or domainpages
	// url filter expressions, we can't muck with the cache because
	// we end up skipping the counting part.
	if ( ! cr )
		useCache = false;
	if ( cr && cr->m_urlFiltersHavePageCounts )
		useCache = false;
	if ( m_countingPagesIndexed )
		useCache = false;
	// assume not from cache
	if ( useCache ) {
		//wc->verify();
		inCache = wc->getRecord ( m_collnum     ,
					  (char *)&cacheKey ,
					  &doleBuf,
					  &doleBufSize  ,
					  false, // doCopy?
					  // we raised MAX_WINNER_NODES so
					  // grow from 600 to 1200
					  // (10 mins to 20 mins) to make
					  // some crawls faster
					  1200, // maxAge, 600 seconds
					  true ,// incCounts
					  &cachedTimestamp , // rec timestamp
					  true );  // promote rec?
		//wc->verify();
	}


	// if ( m_collnum == 18752 ) {
	// 	int32_t coff = 0;
	// 	if ( inCache && doleBufSize >= 4 ) coff = *(int32_t *)doleBuf;
	// 	log("spider: usecache=%i incache=%i dbufsize=%i currentoff=%i "
	// 	    "ctime=%i ip=%s"
	// 	    ,(int)useCache
	// 	    ,(int)inCache
	// 	    ,(int)doleBufSize
	// 	    ,(int)coff
	// 	    ,(int)cachedTimestamp
	// 	    ,iptoa(m_scanningIp));
	// }

	// doleBuf could be NULL i guess...
	if ( inCache ) {
		int32_t crc = hash32 ( doleBuf + 4 , doleBufSize - 4 );

		logDebug( g_conf.m_logDebugSpider, "spider: GOT %" INT32" bytes of SpiderRequests "
		          "from winnerlistcache for ip %s ptr=0x%" PTRFMT" crc=%" UINT32,
		          doleBufSize,
		          iptoa( m_scanningIp ),
		          (PTRTYPE)doleBuf,
		          crc);

		// we no longer re-add to avoid churn. but do not free it
		// so do not 'own' it.
		SafeBuf sb;
		sb.setBuf ( doleBuf, doleBufSize, doleBufSize, false );

		// now add the first rec m_doleBuf into doledb's tree
		// and re-add the rest back to the cache with the same key.
		bool rc = addDoleBufIntoDoledb(&sb,true);//,cachedTimestamp)

		logTrace( g_conf.m_logTraceSpider, "END, after addDoleBufIntoDoledb. returning %s", rc ? "true" : "false" );
		return rc;
	}

 top:

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this, "4" ) ) {
		logTrace( g_conf.m_logTraceSpider, "END, after tryToDeleteSpiderColl (4)" );
		return false;
	}

	// if first time here, let's do a read first
	if ( ! m_didRead ) {
		// reset list size to 0
		m_list.reset();
		// assume we did a read now
		m_didRead = true;
		// reset some stuff
		m_lastScanningIp = 0;

		// reset these that need to keep track of requests for
		// the same url that might span two spiderdb lists or more
		m_lastSreqUh48 = 0LL;

		// do a read. if it blocks it will recall this loop
		if ( ! readListFromSpiderdb () ) {
			logTrace( g_conf.m_logTraceSpider, "END, readListFromSpiderdb returned false" );
			return false;
		}
	}

 loop:

	// did our collection rec get deleted? since we were doing a read
	// the SpiderColl will have been preserved in that case but its
	// m_deleteMyself flag will have been set.
	if ( tryToDeleteSpiderColl ( this, "5" ) ) {
		// pretend to block since we got deleted!!!
		logTrace( g_conf.m_logTraceSpider, "END, after tryToDeleteSpiderColl (5)" );
		return false;
	}

	// . did reading the list from spiderdb have an error?
	// . i guess we don't add to doledb then
	if ( g_errno ) {
		log("spider: Had error getting list of urls from spiderdb: %s.",mstrerror(g_errno));

		// save mem
		m_list.freeList();

		logTrace( g_conf.m_logTraceSpider, "END, g_errno %" INT32, g_errno );
		return true;
	}


	// if we started reading, then assume we got a fresh list here
	logDebug( g_conf.m_logDebugSpider, "spider: back from msg5 spiderdb read2 of %" INT32" bytes (cn=%" INT32")",
	          m_list.m_listSize, (int32_t)m_collnum );

	// . set the winning request for all lists we read so far
	// . if m_countingPagesIndexed is true this will just fill in
	//   quota info into m_localTable...
	scanListForWinners();

	// if list not empty, keep reading!
	if ( ! m_list.isEmpty() ) {
		// update m_nextKey for successive reads of spiderdb by
		// calling readListFromSpiderdb()
		key128_t lastKey  = *(key128_t *)m_list.getLastKey();
		// sanity
		//if ( endKey != finalKey ) { char *xx=NULL;*xx=0; }
		// crazy corruption?
		if ( lastKey < m_nextKey ) {
			log("spider: got corruption. spiderdb "
			    "keys out of order for "
			    "collnum=%"INT32" for evaluation of "
			    "firstip=%s so terminating evaluation of that "
			    "firstip." ,
			    (int32_t)m_collnum,
			    iptoa(m_scanningIp));
			g_corruptCount++;
			// this should result in an empty list read for
			// m_scanningIp in spiderdb
			m_nextKey  = m_endKey;
		}
		else {
			m_nextKey  = lastKey;
			m_nextKey += (uint32_t) 1;
		}
		// . watch out for wrap around
		// . normally i would go by this to indicate that we are
		//   done reading, but there's some bugs... so we go
		//   by whether our list is empty or not for now
		if ( m_nextKey < lastKey ) m_nextKey = lastKey;
		// reset list to save mem
		m_list.reset();
		// read more! return if it blocked
		if ( ! readListFromSpiderdb() ) return false;
		// we got a list without blocking
		goto loop;
	}


	// . we are all done if last list read was empty
	// . if we were just counting pages for quota, do a 2nd pass!
	if ( m_countingPagesIndexed ) {
		// do not do again. 
		m_countingPagesIndexed = false;
		// start at the top again
		m_nextKey = g_spiderdb.makeFirstKey(m_scanningIp);
		// this time m_localTable should have the quota info in it so 
		// getUrlFilterNum() can use that
		m_didRead = false;
		// do the 2nd pass. read list from the very top.
		goto top;
	}

	// free list to save memory
	m_list.freeList();

	// . add all winners if we can in m_winnerTree into doledb
	// . if list was empty, then reading is all done so take the winner we 
	//   got from all the lists we did read for this IP and add him 
	//   to doledb
	// . if no winner exists, then remove m_scanningIp from m_waitingTree
	//   so we do not waste our time again. if url filters change then
	//   waiting tree will be rebuilt and we'll try again... or if
	//   a new spider request or reply for this ip comes in we'll try
	//   again as well...
	// . this returns false if blocked adding to doledb using msg1
	if ( ! addWinnersIntoDoledb() ) {
		logTrace( g_conf.m_logTraceSpider, "END, returning false. After addWinnersIntoDoledb" );
		return false;
	}

	// . do more from tree
	// . re-entry is true because we just got the  msg5 reply
	// . don't do this because populateDoledb calls us in a loop
	//   and we call it from all our callbacks if we blocked...
	//populateDoledbFromWaitingTree ( true );

	// we are done...
	logTrace( g_conf.m_logTraceSpider, "END, all done" );
	return true;
}



// . this is ONLY CALLED from evalIpLoop() above
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool SpiderColl::readListFromSpiderdb ( ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
		
	if ( ! m_waitingTreeKeyValid ) { char *xx=NULL;*xx=0; }
	if ( ! m_scanningIp ) { char *xx=NULL;*xx=0; }

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) {
		log("spider: lost collnum %"INT32"",(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		
		logTrace( g_conf.m_logTraceSpider, "END, ENOCOLLREC" );
		return true;
	}

	// i guess we are always restricted to an ip, because
	// populateWaitingTreeFromSpiderdb calls its own msg5.
	int32_t firstIp0 = g_spiderdb.getFirstIp(&m_nextKey);
	// sanity
	if ( m_scanningIp != firstIp0 ) { char *xx=NULL;*xx=0; }
	// sometimes we already have this ip in doledb/doleiptable
	// already and somehow we try to scan spiderdb for it anyway
	if ( m_doleIpTable.isInTable ( &firstIp0 ) ) { char *xx=NULL;*xx=0;}
		
	// if it got zapped from the waiting tree by the time we read the list
	if ( ! m_waitingTable.isInTable ( &m_scanningIp ) ) 
	{
		logTrace( g_conf.m_logTraceSpider, "END, IP no longer in waitingTree" );
		return true;
	}
	
	// sanity check
	int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
	// it gets removed because addSpiderReply() calls addToWaitingTree
	// and replaces the node we are scanning with one that has a better
	// time, an earlier time, even though that time may have come and
	// we are scanning it now. perhaps addToWaitingTree() should ignore
	// the ip if it equals m_scanningIp?
	if ( wn < 0 ) { 
		log("spider: waiting tree key removed while reading list "
		    "for %s (%"INT32")",
		    cr->m_coll,(int32_t)m_collnum);
		logTrace( g_conf.m_logTraceSpider, "END, waitingTree node was removed" );
		return true;
	}
	
	// sanity. if first time, this must be invalid
	//if ( needList && m_nextKey == m_firstKey && m_bestRequestValid ) {
	//	char *xx=NULL; *xx=0 ; }

	// . if the scanning ip has too many outstanding spiders
	// . looks a UrlLock::m_firstIp and UrlLock::m_isSpiderOutstanding
	//   since the lock lives for 5 seconds after the spider reply
	//   comes back.
	// . when the spiderReply comes back that will re-add a "0" entry
	//   to the waiting tree. 
	// . PROBLEM: some spiders don't seem to add a spiderReply!! wtf???
	//   they end up having their locks timeout after like 3 hrs?
	// . maybe just do not add to waiting tree in confirmLockAcquisition()
	//   handler in such cases? YEAH.. try that
	//int32_t numOutPerIp = getOustandingSpidersPerIp ( firstIp );
	//if ( numOutPerIp > maxSpidersPerIp ) {
	//	// remove from the tree and table
	//	removeFromWaitingTree ( firstIp );
	//	return true;
	//}

	// readLoop:

	// if we re-entered from the read wrapper, jump down
	//if ( needList ) {

	// sanity check
	if ( m_gettingList1 ) { char *xx=NULL;*xx=0; }
	// . read in a replacement SpiderRequest to add to doledb from
	//   this ip
	// . get the list of spiderdb records
	// . do not include cache, those results are old and will mess
	//   us up
	if (g_conf.m_logDebugSpider ) {
		// got print each out individually because KEYSTR
		// uses a static buffer to store the string
		SafeBuf tmp;
		tmp.safePrintf("spider: readListFromSpiderdb: calling msg5: ");
		tmp.safePrintf("firstKey=%s ", KEYSTR(&m_firstKey,sizeof(key128_t)));
		tmp.safePrintf("endKey=%s ", KEYSTR(&m_endKey,sizeof(key128_t)));
		tmp.safePrintf("nextKey=%s ", KEYSTR(&m_nextKey,sizeof(key128_t)));
		tmp.safePrintf("firstip=%s ", iptoa(m_scanningIp));
		tmp.safePrintf("(cn=%"INT32")",(int32_t)m_collnum);
		log(LOG_DEBUG,"%s",tmp.getBufStart());
	}
	
	// log this better
	logDebug(g_conf.m_logDebugSpider, "spider: readListFromSpiderdb: firstip=%s key=%s",
	         iptoa(m_scanningIp), KEYSTR( &m_nextKey, sizeof( key128_t ) ) );
		    
	// flag it
	m_gettingList1 = true;

	// . read the list from local disk
	// . if a niceness 0 intersect thread is taking a LONG time
	//   then this will not complete in a int32_t time and we
	//   end up timing out the round. so try checking for
	//   m_gettingList in spiderDoledUrls() and setting
	//   m_lastSpiderCouldLaunch
	if ( ! m_msg5.getList ( RDB_SPIDERDB   ,
				m_cr->m_collnum   ,
				&m_list        ,
				&m_nextKey      ,
				&m_endKey       ,
				SR_READ_SIZE   , // minRecSizes (512k)
				true           , // includeTree
				false          , // addToCache
				0              , // max cache age
				0              , // startFileNum
				-1             , // numFiles (all)
				this,//(void *)state2,//this,//state 
				gotSpiderdbListWrapper ,
				MAX_NICENESS   , // niceness
				true          )) // do error correct?
	{
		// return false if blocked
		logTrace( g_conf.m_logTraceSpider, "END, msg5.getList blocked" );
		return false ;
	}
	
	// note its return
	logDebug( g_conf.m_logDebugSpider, "spider: back from msg5 spiderdb read of %" INT32" bytes",m_list.m_listSize);
		
	// no longer getting list
	m_gettingList1 = false;

	// got it without blocking. maybe all in tree or in cache
	logTrace( g_conf.m_logTraceSpider, "END, didn't block" );
	return true;
}



static int32_t s_lastIn  = 0;
static int32_t s_lastOut = 0;

bool SpiderColl::isFirstIpInOverflowList ( int32_t firstIp ) {
	if ( ! m_overflowList ) return false;
	if ( firstIp == 0 || firstIp == -1 ) return false;
	if ( firstIp == s_lastIn ) return true;
	if ( firstIp == s_lastOut ) return false;
	for ( int32_t oi = 0 ; ; oi++ ) {
		// stop at end
		if ( ! m_overflowList[oi] ) break;
		// an ip of zero is end of the list
		if ( m_overflowList[oi] == firstIp ) {
			s_lastIn = firstIp;
			return true;
		}
	}
	s_lastOut = firstIp;
	return false;
}



// . ADDS top X winners to m_winnerTree
// . this is ONLY CALLED from evalIpLoop() above
// . scan m_list that we read from spiderdb for m_scanningIp IP
// . set m_bestRequest if an request in m_list is better than what is
//   in m_bestRequest from previous lists for this IP
bool SpiderColl::scanListForWinners ( ) {
	// if list is empty why are we here?
	if ( m_list.isEmpty() ) return true;

	// if waitingtree is locked for writing because it is saving or
	// writes were disabled then just bail and let the scan be re-called
	// later
	//
	// MDW: move this up in evalIpLoop() i think
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	// shortcut
	RdbList *list = &m_list;
	// ensure we point to the top of the list
	list->resetListPtr();

	// get this
	int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();//Local();
	uint32_t nowGlobal   = nowGlobalMS / 1000;

	//SpiderRequest *winReq      = NULL;
	//int32_t           winPriority = -10;
	//uint64_t       winTimeMS   = 0xffffffffffffffffLL;
	//int32_t           winMaxSpidersPerIp = 9999;
	SpiderReply   *srep        = NULL;
	int64_t      srepUh48 = 0;

	// for getting the top MAX_NODES nodes
	//int32_t           tailPriority = -10;
	//uint64_t       tailTimeMS   = 0xffffffffffffffffLL;

	// if we are continuing from another list...
	if ( m_lastReplyValid ) {
		srep     = (SpiderReply *)m_lastReplyBuf;
		srepUh48 = srep->getUrlHash48();
	}

	// show list stats
	logDebug( g_conf.m_logDebugSpider, "spider: readListFromSpiderdb: got list of size %" INT32" for firstip=%s",
	          m_list.m_listSize, iptoa( m_scanningIp ) );


	// if we don't read minRecSizes worth of data that MUST indicate
	// there is no more data to read. put this theory to the test
	// before we use it to indcate an end of list condition.
	if ( list->getListSize() > 0 && 
	     m_lastScanningIp == m_scanningIp &&
	     m_lastListSize < (int32_t)SR_READ_SIZE &&
	     m_lastListSize >= 0 ) {
		log("spider: shucks. spiderdb reads not full.");
	}

	m_lastListSize = list->getListSize();
	m_lastScanningIp = m_scanningIp;

	m_totalBytesScanned += list->getListSize();

	if ( list->isEmpty() ) {
		logDebug( g_conf.m_logDebugSpider, "spider: failed to get rec for ip=%s", iptoa( m_scanningIp ) );
	}


	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;

	key128_t finalKey;
	int32_t recCount = 0;

	// loop over all serialized spiderdb records in the list
	for ( ; ! list->isExhausted() ; ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// stop coring on empty lists
		if ( list->isEmpty() ) break;
		// get spiderdb rec in its serialized form
		char *rec = list->getCurrentRec();
		// count it
		recCount++;
		// sanity
		gbmemcpy ( (char *)&finalKey , rec , sizeof(key128_t) );
		// skip to next guy
		list->skipCurrentRecord();
		// negative? wtf?
		if ( (rec[0] & 0x01) == 0x00 ) {
			logf(LOG_DEBUG,"spider: got negative spider rec");
			continue;
		}
		// if its a SpiderReply set it for an upcoming requests
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec ) ) {

			// see if this is the most recent one
			SpiderReply *tmp = (SpiderReply *)rec;

			// . MDW: we have to detect corrupt replies up here so
			//   they do not become the winning reply because
			//   their date is in the future!!

			if ( tmp->m_spideredTime > nowGlobal + 1 ) {
				if ( m_cr->m_spiderCorruptCount == 0 ) {
					log("spider: got corrupt time "
						"spiderReply in "
						"scan "
						"uh48=%"INT64" "
						"httpstatus=%"INT32" "
						"datasize=%"INT32" "
						"(cn=%"INT32")",
						tmp->getUrlHash48(),
						(int32_t)tmp->m_httpStatus,
						tmp->m_dataSize,
						(int32_t)m_collnum);
				}
				m_cr->m_spiderCorruptCount++;
				// don't nuke it just for that...
				//srep = NULL;
				continue;
			}

			// . this is -1 on corruption
			// . i've seen -31757, 21... etc for bad http replies
			//   in the qatest123 doc cache... so turn off for that
			if ( tmp->m_httpStatus >= 1000 ) {
				if ( m_cr->m_spiderCorruptCount == 0 ) {
					log("spider: got corrupt 3 "
					    "spiderReply in "
					    "scan "
					    "uh48=%"INT64" "
					    "httpstatus=%"INT32" "
					    "datasize=%"INT32" "
					    "(cn=%"INT32")",
					    tmp->getUrlHash48(),
					    (int32_t)tmp->m_httpStatus,
					    tmp->m_dataSize,
					    (int32_t)m_collnum);
				}
				m_cr->m_spiderCorruptCount++;
				// don't nuke it just for that...
				//srep = NULL;
				continue;
			}
			// bad langid?
			if ( ! getLanguageAbbr (tmp->m_langId) ) {
				log("spider: got corrupt 4 spiderReply in "
				    "scan uh48=%"INT64" "
				    "langid=%"INT32" (cn=%"INT32")",
				    tmp->getUrlHash48(),
				    (int32_t)tmp->m_langId,
				    (int32_t)m_collnum);
				m_cr->m_spiderCorruptCount++;
				//srep = NULL;
				// if ( tmp->getUrlHash48() == 
				//      271713196158770LL )
				// 	log("hey");
				continue;
			}

			// reset reply stats if beginning a new url
			// these don't work because we only store one reply
			// which overwrites any older reply. that's how the 
			// key is. we can change the key to use the timestamp 
			// and not parent docid in makeKey() for spider 
			// replies later.
			// if ( srepUh48 != tmp->getUrlHash48() ) {
			// 	m_numSuccessReplies = 0;
			// 	m_numFailedReplies  = 0;
			// }

			// inc stats
			// these don't work because we only store one reply
			// which overwrites any older reply. that's how the 
			// key is. we can change the key to use the timestamp 
			// and not parent docid in makeKey() for spider 
			// replies later.
			// if ( tmp->m_errCode == 0 ) m_numSuccessReplies++;
			// else                       m_numFailedReplies ++;

			// if we are corrupt, skip us
			if ( tmp->getRecSize() > (int32_t)MAX_SP_REPLY_SIZE )
				continue;
			// if we have a more recent reply already, skip this 
			if ( srep && 
			     srep->getUrlHash48() == tmp->getUrlHash48() &&
			     srep->m_spideredTime >= tmp->m_spideredTime )
				continue;
			// otherwise, assign it
			srep     = tmp;
			srepUh48 = srep->getUrlHash48();
			continue;
		}
		// cast it
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// skip if our twin or another shard should handle it
		if ( ! isAssignedToUs ( sreq->m_firstIp ) )
			continue;

		int64_t uh48 = sreq->getUrlHash48();

		// reset reply stats if beginning a new url
		// these don't work because we only store one reply
		// which overwrites any older reply. that's how the key is.
		// we can change the key to use the timestamp and not
		// parent docid in makeKey() for spider replies later.
		// if ( ! srep ) {
		// 	m_numSuccessReplies = 0;
		// 	m_numFailedReplies  = 0;
		// }

		// . skip if our twin should add it to doledb
		// . waiting tree only has firstIps assigned to us so
		//   this should not be necessary
		//if ( ! isAssignedToUs ( sreq->m_firstIp ) ) continue;
		// null out srep if no match
		if ( srep && srepUh48 != uh48 ) srep = NULL;
		// if we are doing parser test, ignore all but initially
		// injected requests. NEVER DOLE OUT non-injected urls
		// when doing parser test
		if ( g_conf.m_testParserEnabled ) {
			// skip if already did it
			if ( srep ) continue;
			// skip if not injected
			if ( ! sreq->m_isInjecting ) {
				logDebug( g_conf.m_logDebugSpider, "spider: skipping8 %s", sreq->m_url );
				continue;
			}
		}

		// . ignore docid-based requests if spidered the url afterwards
		// . these are one-hit wonders
		// . once done they can be deleted
		if ( sreq->m_isPageReindex && srep && srep->m_spideredTime > sreq->m_addedTime ) {
			logDebug( g_conf.m_logDebugSpider, "spider: skipping9 %s", sreq->m_url );
			continue;
		}

		// if a replie-less new url spiderrequest count it
		if ( ! srep && m_lastSreqUh48 != uh48 &&
		     // avoid counting query reindex requests
		     ! sreq->m_fakeFirstIp )
			m_totalNewSpiderRequests++;

		//int32_t  ipdom ( int32_t ip ) { return ip & 0x00ffffff; };
		int32_t cblock = ipdom ( sreq->m_firstIp );

		bool countIt = true;

		// reset page inlink count on url request change
		if ( m_lastSreqUh48 != uh48 ) {
			m_pageNumInlinks = 0;
			m_lastCBlockIp = 0;
		}

		if ( cblock == m_lastCBlockIp )
			countIt = false;

		// do not count manually added spider requests
		if ( (sreq->m_isAddUrl || sreq->m_isInjecting) )
			countIt = false;

		// 20 is good enough
		if ( m_pageNumInlinks >= 20 )
			countIt = false;

		if ( countIt ) {
			int32_t ca;
			for ( ca = 0 ; ca < m_pageNumInlinks ; ca++ ) 
				if ( m_cblocks[ca] == cblock ) break;
			// if found in our list, do not count it, already did
			if ( ca < m_pageNumInlinks )
				countIt = false;
		}

		if ( countIt ) {
			m_cblocks[m_pageNumInlinks] = cblock;
			m_pageNumInlinks++;
			if ( m_pageNumInlinks > 20 ) { char *xx=NULL;*xx=0;}
		}

		// set this now. it does increase with each request. so 
		// initial requests will not see the full # of inlinks.
		sreq->m_pageNumInlinks = (uint8_t)m_pageNumInlinks;

		m_lastSreqUh48 = uh48;
		m_lastCBlockIp = cblock;

		// only add firstip if manually added and not fake

		//
		// just calculating page counts? if the url filters are based
		// on the # of pages indexed per ip or subdomain/site then
		// we have to maintain a page count table. sitepages.
		//
		if ( m_countingPagesIndexed ) { //&& sreq->m_fakeFirstIp ) {
			// get request url hash48 (jez= 220459274533043 )
			//int64_t uh48 = sreq->getUrlHash48();
			// do not repeatedly page count if we just have
			// a single fake firstip request. this just adds
			// an entry to the table that will end up in
			// m_pageCountTable so we avoid doing this count
			// again over and over. also gives url filters
			// table a zero-entry...
			//m_localTable.addScore(&sreq->m_firstIp,0);
			//m_localTable.addScore(&sreq->m_siteHash32,0);
			//m_localTable.addScore(&sreq->m_domHash32,0);
			// only add dom/site hash seeds if it is
			// a fake firstIp to avoid double counting seeds
			if ( sreq->m_fakeFirstIp ) continue;
			// count the manual additions separately. mangle their
			// hash with 0x123456 so they are separate.
			if ( (sreq->m_isAddUrl || sreq->m_isInjecting) &&
			     // unique votes per seed
			     uh48 != m_lastReqUh48a ) {
				// do not repeat count the same url
				m_lastReqUh48a = uh48;
				// sanity
				if ( ! sreq->m_siteHash32){char*xx=NULL;*xx=0;}
				if ( ! sreq->m_domHash32){char*xx=NULL;*xx=0;}
				// do a little magic because we count
				// seeds as "manual adds" as well as normal pg
				int32_t h32;
				h32 = sreq->m_siteHash32 ^ 0x123456;
				m_localTable.addScore(&h32);
				h32 = sreq->m_domHash32 ^ 0x123456;
				m_localTable.addScore(&h32);
			}
			// unique votes per other for quota
			if ( uh48 == m_lastReqUh48b ) continue;
			// update this to ensure unique voting
			m_lastReqUh48b = uh48;
			// now count pages indexed below here
			if ( ! srep ) continue;
			if ( srepUh48 == m_lastRepUh48 ) continue;
			m_lastRepUh48 = srepUh48;
			//if ( ! srep ) continue;
			// TODO: what is srep->m_isIndexedINValid is set????
			if ( ! srep->m_isIndexed ) continue;
			// keep count per site and firstip
			m_localTable.addScore(&sreq->m_firstIp,1);
			m_localTable.addScore(&sreq->m_siteHash32,1);
			m_localTable.addScore(&sreq->m_domHash32,1);

			logDebug( g_conf.m_logDebugSpider, "spider: sitequota: got %" INT32" indexed docs for site from "
			          "firstip of %s from url %s",
			          *( (int32_t *)m_localTable.getValue( &( sreq->m_siteHash32 ) ) ),
			          iptoa( sreq->m_firstIp ),
			          sreq->m_url );
			continue;
		}


		// if the spiderrequest has a fake firstip that means it
		// was injected without doing a proper ip lookup for speed.
		// xmldoc.cpp will check for m_fakeFirstIp and it that is
		// set in the spiderrequest it will simply add a new request
		// with the correct firstip. it will be a completely different
		// spiderrequest key then. so no need to keep the "fakes".
		// it will log the EFAKEFIRSTIP error msg.
		if ( sreq->m_fakeFirstIp && srep && srep->m_spideredTime > sreq->m_addedTime ) {
			logDebug( g_conf.m_logDebugSpider, "spider: skipping6 %s", sreq->m_url );
			continue;
		}

		// once we have a spiderreply, even i guess if its an error,
		// for a url, then bail if respidering is disabled
		if ( m_cr->m_isCustomCrawl && 
		     srep && 

		     // no! for bulk jobs and crawl jobs we ALWAYS retry
		     // on errors... tmp errors, but this is just a shortcut
		     // so only take this shortcut if there is no error
		     // and repeat is 0.0.
		     // ... CRAP we do not want error'ed urls to resuscitate
		     // and job that is not already in progress if it is not
		     // supposed to be a repeat crawl.
		     ( srep->m_errCode && 
		       // BUT skip this url if the job is not in progress
		       // even if the errCode is NON-zero, THUS we prevent
		       // a job from flip flopping from in progress to
		       // not in progress and sending out alerts. so once
		       // it goes to NOT in progress, that's it...
		       m_cr->m_spiderStatus != SP_INPROGRESS ) &&
		     // this means "repeat" is set to 0, to not repeat but
		     // if we get a &roundStart=1 request we do a round anyway.
		     m_cr->m_collectiveRespiderFrequency <= 0.0 ) {
			logDebug(g_conf.m_logDebugSpider, "spider: skipping0 %s", sreq->m_url );
			continue;
		}

		// sanity check. check for http(s)://
		if ( ( sreq->m_url[0] != 'h' ||
		       sreq->m_url[1] != 't' ||
		       sreq->m_url[2] != 't' ||
		       sreq->m_url[3] != 'p' ) &&
		     // might be a docid from a pagereindex.cpp
		     ! is_digit(sreq->m_url[0]) ) { 
			if ( m_cr->m_spiderCorruptCount == 0 )
				log( LOG_WARN, "spider: got corrupt 1 spiderRequest in "
				    "scan because url is %s (cn=%"INT32")"
				    ,sreq->m_url,(int32_t)m_collnum);
			m_cr->m_spiderCorruptCount++;
			continue;
		}
		if ( sreq->m_dataSize > (int32_t)sizeof(SpiderRequest) ) {
			if ( m_cr->m_spiderCorruptCount == 0 )
				log( LOG_WARN, "spider: got corrupt 11 spiderRequest in "
				    "scan because rectoobig u=%s (cn=%"INT32")"
				    ,sreq->m_url,(int32_t)m_collnum);
			m_cr->m_spiderCorruptCount++;
			continue;
		}
		if ( sreq->m_dataSize > (int32_t)sizeof(SpiderRequest) ||
			sreq->m_dataSize < 0 ) {
				if ( m_cr->m_spiderCorruptCount == 0 )
					log( LOG_WARN, "spider: got corrupt 11 spiderRequest in "
						"scan because size=%i u=%s (cn=%"INT32")"
						,(int)sreq->m_dataSize
						,sreq->m_url,(int32_t)m_collnum);
				m_cr->m_spiderCorruptCount++;
				continue;
		}
		
		
		int32_t delta = sreq->m_addedTime - nowGlobal;
		if ( delta > 86400 ) {
			static bool s_first = true;
			if ( m_cr->m_spiderCorruptCount == 0 || s_first ) {
				s_first = false;
				log( LOG_WARN, "spider: got corrupt 6 spiderRequest in "
				    "scan because added time is %"INT32" "
				    "(delta=%"INT32" "
				    "which is well into the future. url=%s "
				    "(cn=%i)"
				    ,(int32_t)sreq->m_addedTime
				    ,delta
				    ,sreq->m_url
				    ,(int)m_collnum);
			}
			m_cr->m_spiderCorruptCount++;
			continue;
		}


		// update SpiderRequest::m_siteNumInlinks to most recent value
		int32_t sni = sreq->m_siteNumInlinks;
		// get the # of inlinks to the site from our table
		uint64_t *val;
		val = (uint64_t *)m_sniTable.getValue32(sreq->m_siteHash32);
		// use the most recent sni from this table
		if ( val ) 
			sni = (int32_t)((*val)>>32);
		// if SpiderRequest is forced then m_siteHash32 is 0!
		else if ( srep && srep->m_spideredTime >= sreq->m_addedTime ) 
			sni = srep->m_siteNumInlinks;
		// assign
		sreq->m_siteNumInlinks = sni;
		// store rror count in request so xmldoc knows what it is
		// and can increment it and re-add it to its spiderreply if
		// it gets another error
		if ( srep ) {
			sreq->m_errCount = srep->m_errCount;
			// . assign this too from latest reply - smart compress
			// . this WAS SpiderReply::m_pubdate so it might be
			//   set to a non-zero value that is wrong now... but
			//   not a big deal!
			sreq->m_contentHash32 = srep->m_contentHash32;
			// if we tried it before
			sreq->m_hadReply = true;
		}

		// . get the url filter we match
		// . if this is slow see the TODO below in dedupSpiderdbList()
		//   which can pre-store these values assuming url filters do
		//   not change and siteNumInlinks is about the same.
		int32_t ufn = ::getUrlFilterNum(sreq,
					     srep,
					     nowGlobal,
					     false,
					     MAX_NICENESS,
					     m_cr,
					     false, // isOutlink?
					     // provide the page quota table
						&m_localTable,
						-1);
		// sanity check
		if ( ufn == -1 ) { 
			log("spider: failed to match url filter for "
			    "url = %s coll=%s", sreq->m_url,m_cr->m_coll);
			g_errno = EBADENGINEER;
			return true;
		}
		// set the priority (might be the same as old)
		int32_t priority = m_cr->m_spiderPriorities[ufn];
		// now get rid of negative priorities since we added a
		// separate force delete checkbox in the url filters
		if ( priority < 0 ) priority = 0;
		// sanity checks
		//if ( priority == -1 ) { char *xx=NULL;*xx=0; }
		if ( priority >= MAX_SPIDER_PRIORITIES) {char *xx=NULL;*xx=0;}

		logDebug( g_conf.m_logDebugSpider, "spider: got ufn=%" INT32" for %s (%" INT64")",
		          ufn, sreq->m_url, sreq->getUrlHash48() )

		if ( srep )
			logDebug(g_conf.m_logDebugSpider, "spider: lastspidered=%" UINT32"", srep->m_spideredTime );

		// spiders disabled for this row in url filteres?
		if ( m_cr->m_maxSpidersPerRule[ufn] <= 0 ) continue;

		// skip if banned (unless need to delete from index)
		bool skip = false;
		// if ( priority == SPIDER_PRIORITY_FILTERED ) skip = true;
		// if ( priority == SPIDER_PRIORITY_BANNED   ) skip = true;
		if ( m_cr->m_forceDelete[ufn] ) skip = true;
		// but if it is currently indexed we have to delete it
		if ( skip && srep && srep->m_isIndexed ) skip = false;
		if ( skip ) continue;

		// temp debug
		//char *xx=NULL;*xx=0;

		if ( m_cr->m_forceDelete[ufn] )
			// force it to a delete
			sreq->m_forceDelete = true;

		int64_t spiderTimeMS;
		spiderTimeMS = getSpiderTimeMS ( sreq,ufn,srep,nowGlobalMS );
		// how many outstanding spiders on a single IP?
		//int32_t maxSpidersPerIp = m_cr->m_spiderIpMaxSpiders[ufn];
		// sanity
		if ( (int64_t)spiderTimeMS < 0 ) { 
			log( LOG_WARN, "spider: got corrupt 2 spiderRequest in "
			    "scan (cn=%"INT32")",
			    (int32_t)m_collnum);
			continue;
		}
		// more corruption detection
		if ( sreq->m_hopCount < -1 ) {
			log( LOG_WARN, "spider: got corrupt 5 spiderRequest in "
			    "scan (cn=%"INT32")",
			    (int32_t)m_collnum);
			continue;
		}
		// 100 days out is corruption. ppl sometimes put
		// 3000 days to re-spider... so take this out
		/*
		int64_t delta2 = spiderTimeMS - nowGlobalMS;
		if ( delta2 > 86400LL*1000LL*100LL ) {
			log("spider: got corrupt 7 spiderRequest in "
			    "scan (cn=%"INT32") (delta=%"INT64") url=%s",
			    (int32_t)m_collnum,
			    delta2,
			    sreq->m_url);
			SafeBuf sb; g_spiderdb.print ( (char *)sreq , &sb );
			if ( srep ) g_spiderdb.print ( (char *)srep , &sb );
			log("spider: %s",sb.getBufStart());
			continue;
		}
		*/

		// save this shit for storing in doledb
		sreq->m_ufn = ufn;
		sreq->m_priority = priority;

		// if it is in future, skip it and just set m_futureTime and
		// and we will update the waiting tree
		// with an entry based on that future time if the winnerTree 
		// turns out to be empty after we've completed our scan
		if ( spiderTimeMS > nowGlobalMS ) {
			// if futuretime is zero set it to this time
			if ( ! m_minFutureTimeMS ) 
				m_minFutureTimeMS = spiderTimeMS;
			// otherwise we get the MIN of all future times
			else if ( spiderTimeMS < m_minFutureTimeMS )
				m_minFutureTimeMS = spiderTimeMS;
			if ( g_conf.m_logDebugSpider )
				log("spider: skippingx %s",sreq->m_url);
			continue;
		}

		// debug point
		// if ( ((long long)srep->m_spideredTime)*1000LL > 
		//      nowGlobalMS - 86400LL*1000LL*30LL )
		// 	log("spider: should not be spidering this!");

		//////
		//
		// MDW: no, take this out now that we allow multiple urls
		// in doledb from this firstip for speeding up the spiders.
		// the crawldelay will be used by Msg13.cpp when it tries
		// to download the url.
		//
		//////
		/*

		// how many "ready" urls for this IP? urls in doledb
		// can be spidered right now
		int32_t *score ;
		score = (int32_t *)m_doleIpTable.getValue32 ( sreq->m_firstIp );
		// how many spiders are current outstanding
		int32_t out2 = outNow;
		// add in any requests in doledb
		if ( score ) out2 += *score;

		// . do not add any more to doledb if we could violate ourquota
		// . shit we have to add it our it never gets in
		// . try regulating in msg13.cpp download code. just queue
		//   up requests to avoid hammering there.
		if ( out2 >= maxSpidersPerIp ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: skipping1 %s",sreq->m_url);
			continue;
		}

		// by ensuring only one spider out at a time when there
		// is a positive crawl-delay, we ensure that m_lastDownloadTime
		// is the last time we downloaded from this ip so that we
		// can accurately set the time in getSpiderTimeMS() for
		// when the next url from this firstip should be spidered.
		if ( out2 >= 1 ) {
			// get the crawldelay for this domain
			int32_t *cdp ;
			cdp = (int32_t *)m_cdTable.getValue (&sreq->m_domHash32);
			// if crawl delay is NULL, we need to download
			// robots.txt. most of the time it will be -1
			// which indicates not specified in robots.txt
			if ( ! cdp ) {
				if ( g_conf.m_logDebugSpider )
					log("spider: skipping2 %s",
					    sreq->m_url);
				continue;
			}
			// if we had a positive crawldelay and there is
			// already >= 1 outstanding spider on this ip, 
			// then skip this url
			if ( cdp && *cdp > 0 ) {
				if ( g_conf.m_logDebugSpider )
					log("spider: skipping3 %s",
					    sreq->m_url);
				continue;
			}
		}
		*/

		// debug. show candidates due to be spidered now.
		//if(g_conf.m_logDebugSpider ) //&& spiderTimeMS< nowGlobalMS )
		//	log("spider: considering ip=%s sreq spiderTimeMS=%"INT64" "
		//	    "pri=%"INT32" uh48=%"INT64"",
		//	    iptoa(sreq->m_firstIp),
		//	    spiderTimeMS,
		//	    priority,
		//	    sreq->getUrlHash48());


		// we can't have negative priorities at this point because
		// the s_ufnTree uses priority as part of the key so it
		// can get the top 100 or so urls for a firstip to avoid
		// having to hit spiderdb for every one!
		if ( priority < 0 ) { char *xx=NULL;*xx=0; }

		//
		// NO! then just a single root url can prevent all his
		// kids from getting spidered. because this logic was
		// priority based over time. so while the high priority url
		// would be sitting in the waiting tree, the kids whose
		// time it was to be spidered would be starving for attention.
		// only use priority if the high priority url can be spidered
		// now, so he doesn't lock the others out of the waiting tree.
		//
		// now pick the SpiderRequest with the best priority, then
		// break ties with the "spiderTime".
		//if ( priority <  winPriority ) 
		//	continue;
		// if tied, use times
		//if ( priority == winPriority && spiderTimeMS > winTimeMS ) 
		//	continue;

		// bail if it is locked! we now call 
		// msg12::confirmLockAcquisition() after we get the lock,
		// which deletes the doledb record from doledb and doleiptable
		// rightaway and adds a "0" entry into the waiting tree so
		// that evalIpLoop() repopulates doledb again with that
		// "firstIp". this way we can spider multiple urls from the
		// same ip at the same time.
		int64_t key = makeLockTableKey ( sreq );

		logDebug( g_conf.m_logDebugSpider, "spider: checking uh48=%" INT64" lockkey=%" INT64" used=%" INT32,
		          uh48, key, g_spiderLoop.m_lockTable.getNumUsedSlots() );

		// MDW
		if ( g_spiderLoop.m_lockTable.isInTable ( &key ) ) {
			logDebug( g_conf.m_logDebugSpider, "spider: skipping url lockkey=%" INT64" in lock table sreq.url=%s",
			          key, sreq->m_url );
			continue;
		}

		//int64_t uh48 = sreq->getUrlHash48();

		// make key
		key192_t wk = makeWinnerTreeKey( firstIp ,
						 priority ,
						 sreq->m_hopCount,
						 spiderTimeMS ,
						 uh48 );

		// assume our added time is the first time this url was added
		sreq->m_discoveryTime = sreq->m_addedTime;

		// if this url is already in the winnerTree then either we 
		// replace it or we skip ourselves. 
		//
		// watch out for dups in winner tree, the same url can have 
		// multiple spiderTimeMses somehow... i guess it could have 
		// different hop counts
		// as well, resulting in different priorities...
		// actually the dedup table could map to a priority and a node
		// so we can kick out a lower priority version of the same url.
		int32_t winSlot = m_winnerTable.getSlot ( &uh48 );
		if ( winSlot >= 0 ) {
			key192_t *oldwk = (key192_t *)m_winnerTable.getDataFromSlot ( winSlot );

			// get the min hopcount  
			SpiderRequest *wsreq ;
			wsreq =(SpiderRequest *)m_winnerTree.getData(0,(char *)oldwk);
			
			if ( wsreq ) {
				if ( sreq->m_hopCount < wsreq->m_hopCount )
					wsreq->m_hopCount = sreq->m_hopCount;
					
				if ( wsreq->m_hopCount < sreq->m_hopCount )
					sreq->m_hopCount = wsreq->m_hopCount;
					
				// and the min added time as well!
				// get the oldest timestamp so
				// gbssDiscoveryTime will be accurate.
				if ( sreq->m_discoveryTime < wsreq->m_discoveryTime )
					wsreq->m_discoveryTime = sreq->m_discoveryTime;
					
				if ( wsreq->m_discoveryTime < sreq->m_discoveryTime )
					sreq->m_discoveryTime = wsreq->m_discoveryTime;
			}

			

			// are we lower priority? (or equal)
			// smaller keys are HIGHER priority.
			if(KEYCMP( (char *)&wk, (char *)oldwk, sizeof(key192_t)) >= 0) 
			{
				continue;
			}
				
			// from table too. no it's a dup uh48!
			//m_winnerTable.deleteKey ( &uh48 );
			// otherwise we supplant it. remove old key from tree.
			m_winnerTree.deleteNode ( 0 , (char *)oldwk , false);
			// supplant in table and tree... just add below...
		}

		// get the top 100 spider requests by priority/time/etc.
		int32_t maxWinners = (int32_t)MAX_WINNER_NODES; // 40
		//if ( ! m_cr->m_isCustomCrawl ) maxWinners = 1;


//@todo BR: Why max winners based on bytes scanned??
		// if less than 10MB of spiderdb requests limit to 400
		if ( m_totalBytesScanned < 10000000 ) maxWinners = 400;

		// only put one doledb record into winner tree if
		// the list is pretty short. otherwise, we end up caching
		// too much. granted, we only cache for about 2 mins.
		// mdw: for testing take this out!
		if ( m_totalBytesScanned < 25000 ) maxWinners = 1;

		// sanity. make sure read is somewhat hefty for our 
		// maxWinners=1 thing
		if ( (int32_t)SR_READ_SIZE < 500000 ) { char *xx=NULL;*xx=0; }

		// only compare to min winner in tree if tree is full
		if ( m_winnerTree.getNumUsedNodes() >= maxWinners ) {
			// get that key
			int64_t tm1 = spiderTimeMS;
			// get the spider time of lowest scoring req in tree
			int64_t tm2 = m_tailTimeMS;
			// if they are both overdue, make them the same
			if ( tm1 < nowGlobalMS ) tm1 = 1;
			if ( tm2 < nowGlobalMS ) tm2 = 1;
			// skip spider request if its time is past winner's
			if ( tm1 > tm2 )
				continue;
			if ( tm1 < tm2 )
				goto gotNewWinner;
			// if tied, use priority
			if ( priority < m_tailPriority )
				continue;
			if ( priority > m_tailPriority )
				goto gotNewWinner;
			// if tied use hop counts so we are breadth first
			if ( sreq->m_hopCount > m_tailHopCount )
				continue;
			if ( sreq->m_hopCount < m_tailHopCount )
				goto gotNewWinner;
			// if hopcounts tied prefer the unindexed doc
			// i don't think we need this b/c spidertimems
			// for new docs should be less than old docs...
			// TODO: verify that
			//if ( sreq->m_isIndexed && ! m_tailIsIndexed )
			//	continue;
			//if ( ! sreq->m_isIndexed && m_tailIsIndexed )
			//	goto gotNewWinner;
			// if tied, use actual times. assuming both<nowGlobalMS
			if ( spiderTimeMS > m_tailTimeMS )
				continue;
			if ( spiderTimeMS < m_tailTimeMS )
				goto gotNewWinner;
			// all tied, keep it the same i guess
			continue;
			// otherwise, add the new winner in and remove the old
		gotNewWinner:
			// get lowest scoring node in tree
			int32_t tailNode = m_winnerTree.getLastNode();
			// from table too
			m_winnerTable.removeKey ( &m_tailUh48 );
			// delete the tail so new spiderrequest can enter
			m_winnerTree.deleteNode3 ( tailNode , true );

		}

		// somestimes the firstip in its key does not match the
		// firstip in the record!
		if ( sreq->m_firstIp != firstIp ) {
			log("spider: request %s firstip does not match "
			    "firstip in key collnum=%i",sreq->m_url,
			    (int)m_collnum);
			log("spider: ip1=%s",iptoa(sreq->m_firstIp));
			log("spider: ip2=%s",iptoa(firstIp));
			continue;
		}


		// . add to table which allows us to ensure same url not 
		//   repeated in tree
		// . just skip if fail to add...
		if ( m_winnerTable.addKey ( &uh48 , &wk ) < 0 ) continue;

		// use an individually allocated buffer for each spiderrequest
		// so if it gets removed from tree the memory can be freed by 
		// the tree which "owns" the data because m_winnerTree.set() 
		// above set ownsData
		// to true above.
		int32_t need = sreq->getRecSize();
		char *newMem = (char *)mdup ( sreq , need , "sreqbuf" );
		if ( ! newMem ) continue;

		// add it to the tree of the top urls to spider
		m_winnerTree.addNode( 0, 
				      (char *)&wk ,
				      (char *)newMem ,
				      need );

		// log("adding wk uh48=%llu #usednodes=%i",
		//     uh48,m_winnerTree.m_numUsedNodes);

		// sanity
		//SpiderRequest *sreq2 = (SpiderRequest *)m_winnerTree.
		//getData ( nn );

		// //////////////////////
		// // MDW dedup test
		// HashTableX dedup;
		// int32_t ntn = m_winnerTree.getNumNodes();
		// char dbuf[3*MAX_WINNER_NODES*(8+1)];
		// dedup.set ( 8,
		// 	    0,
		// 	    (int32_t)2*ntn, // # slots to initialize to
		// 	    dbuf,
		// 	    (int32_t)(3*MAX_WINNER_NODES*(8+1)),
		// 	    false,
		// 	    MAX_NICENESS,
		// 	    "windt");
		// for ( int32_t node = m_winnerTree.getFirstNode() ; 
		//       node >= 0 ; 
		//       node = m_winnerTree.getNextNode ( node ) ) {
		// // get data for that
		// SpiderRequest *sreq2;
		// sreq2 = (SpiderRequest *)m_winnerTree.getData ( node );
		// // parse it up
		// int32_t winIp;
		// int32_t winPriority;
		// int32_t winHopCount;
		// int64_t winSpiderTimeMS;
		// int64_t winUh48;
		// key192_t *winKey = (key192_t *)m_winnerTree.getKey ( node );
		// parseWinnerTreeKey ( winKey ,
		// 		     &winIp ,
		// 		     &winPriority,
		// 		     &winHopCount,
		// 		     &winSpiderTimeMS ,
		// 		     &winUh48 );
		// // sanity
		//if(winUh48 != sreq2->getUrlHash48() ) { char *xx=NULL;*xx=0;}
		// // make the doledb key
		// key_t doleKey = g_doledb.makeKey ( winPriority,
		// 				   // convert to secs from ms
		// 				   winSpiderTimeMS / 1000     ,
		// 				   winUh48 ,
		// 				   false                    );
		// // dedup. if we add dups the problem is is that they
		// // overwrite the key in doledb yet the doleiptable count
		// // remains undecremented and doledb is empty and never
		// // replenished because the firstip can not be added to
		// // waitingTree because doleiptable count is > 0. this was
		// // causing spiders to hang for collections. i am not sure
		// // why we should be getting dups in winnertree because they
		// // have the same uh48 and that is the key in the tree.
		// if ( dedup.isInTable ( &winUh48 ) ) {
		// 	log("spider: got dup uh48=%"UINT64" dammit", winUh48);
		// 	char *xx=NULL;*xx=0;
		// 	continue;
		// }
		// // do not allow dups
		// dedup.addKey ( &winUh48 );
		// }
		// // end dedup test
		//////////////////////////

		// set new tail priority and time for next compare
		if ( m_winnerTree.getNumUsedNodes() >= maxWinners ) {
			// for the worst node in the tree...
			int32_t tailNode = m_winnerTree.getLastNode();
			if ( tailNode < 0 ) { char *xx=NULL;*xx=0; }
			// set new tail parms
			key192_t *tailKey;
			tailKey = (key192_t *)m_winnerTree.getKey ( tailNode );
			// convert to char first then to signed int32_t
			parseWinnerTreeKey ( tailKey ,
					     &m_tailIp ,
					     &m_tailPriority,
					     &m_tailHopCount,
					     &m_tailTimeMS ,
					     &m_tailUh48 );
			// sanity
			if ( m_tailIp != firstIp ) { char *xx=NULL;*xx=0;}
		}

		/*
		// ok, we got a new winner
		winPriority = priority;
		winTimeMS   = spiderTimeMS;
		winMaxSpidersPerIp = maxSpidersPerIp;
		winReq      = sreq;
		// set these for doledb
		winReq->m_priority   = priority;
		winReq->m_ufn        = ufn;
		//winReq->m_spiderTime = spiderTime;
		*/
	}

	// if no spiderreply for the current url, invalidate this
	m_lastReplyValid = false;
	// if read is not yet done, save the reply in case next list needs it
	if ( srep ) { // && ! m_isReadDone ) {
		int32_t rsize = srep->getRecSize();
		if ( rsize > (int32_t)MAX_SP_REPLY_SIZE){char *xx=NULL;*xx=0; }
		gbmemcpy ( m_lastReplyBuf, srep, rsize );
		m_lastReplyValid = true;
	}

	logDebug(g_conf.m_logDebugSpider, "spider: Checked list of %" INT32" spiderdb bytes (%" INT32" recs) "
		    "for winners for firstip=%s. winnerTreeUsedNodes=%" INT32" #newreqs=%" INT64,
	         list->getListSize(), recCount,
	         iptoa( m_scanningIp ), m_winnerTree.getNumUsedNodes(), m_totalNewSpiderRequests );

	// reset any errno cuz we're just a cache
	g_errno = 0;


	/////
	//
	// BEGIN maintain firstip overflow list
	//
	/////
	bool overflow = false;
	// don't add any more outlinks to this firstip after we
	// have 10M spider requests for it.
	// lower for testing
	//if ( m_totalNewSpiderRequests > 1 )
// @todo BR: Another hardcoded limit..	
	if ( m_totalNewSpiderRequests > 10000000 )
		overflow = true;

	// need space
	if ( overflow && ! m_overflowList ) {
		int32_t need = OVERFLOWLISTSIZE*4;
		m_overflowList = (int32_t *)mmalloc(need,"list");
		m_overflowList[0] = 0;
	}
	//
	// ensure firstip is in the overflow list if we overflowed
	int32_t emptySlot = -1;
	bool found = false;
	int32_t oi;
	// if we dealt with this last round, we're done
	if ( m_lastOverflowFirstIp == firstIp )
		return true;
	m_lastOverflowFirstIp = firstIp;
	if ( overflow ) {
		logDebug( g_conf.m_logDebugSpider, "spider: firstip %s overflowing with %" INT32" new reqs",
		          iptoa(firstIp), (int32_t)m_totalNewSpiderRequests );
	}

	for ( oi = 0 ; ; oi++ ) {
		// sanity
		if ( ! m_overflowList ) break;
		// an ip of zero is end of the list
		if ( ! m_overflowList[oi] ) break;
		// if already in there, we are done
		if ( m_overflowList[oi] == firstIp ) {
			found = true;
			break;
		}
		// -1 means empty slot
		if ( m_overflowList[oi] == -1 ) emptySlot = oi;
	}
	// if we need to add it...
	if ( overflow && ! found && m_overflowList ) {
		log("spider: adding %s to overflow list",iptoa(firstIp));
		// reset this little cache thingy
		s_lastOut = 0;
		// take the empty slot if there is one
		if ( emptySlot >= 0 )
			m_overflowList[emptySlot] = firstIp;
		// or add to new slot. this is #defined to 200 last check
		else if ( oi+1 < OVERFLOWLISTSIZE ) {
			m_overflowList[oi] = firstIp;
			m_overflowList[oi+1] = 0;
		}
		else 
			log("spider: could not add firstip %s to "
			    "overflow list, full.", iptoa(firstIp));
	}
	// ensure firstip is NOT in the overflow list if we are ok
	for ( int32_t oi2 = 0 ; ! overflow ; oi2++ ) {
		// sanity
		if ( ! m_overflowList ) break;
		// an ip of zero is end of the list
		if ( ! m_overflowList[oi2] ) break;
		// skip if not a match
		if ( m_overflowList[oi2] != firstIp ) continue;
		// take it out of list
		m_overflowList[oi2] = -1;
		log("spider: removing %s from overflow list",iptoa(firstIp));
		// reset this little cache thingy
		s_lastIn = 0;
		break;
	}
	/////
	//
	// END maintain firstip overflow list
	//
	/////

	// ok we've updated m_bestRequest!!!
	return true;
}



// . this is ONLY CALLED from evalIpLoop() above
// . add another 0 entry into waiting tree, unless we had no winner
// . add winner in here into doledb
// . returns false if blocked and doledWrapper() will be called
// . returns true and sets g_errno on error
bool SpiderColl::addWinnersIntoDoledb ( ) {

	if ( g_errno ) {
		log("spider: got error when trying to add winner to doledb: "
		    "%s",mstrerror(g_errno));
		return true;
	}

	// gotta check this again since we might have done a QUICKPOLL() above
	// to call g_process.shutdown() so now tree might be unwritable
	RdbTree *wt = &m_waitingTree;
	if ( wt->m_isSaving || ! wt->m_isWritable )
		return true;

	/*
	  MDW: let's print out recs we add to doledb
	//if ( g_conf.m_logDebugSpider && m_bestRequestValid ) {
	if ( g_conf.m_logDebugSpider && m_bestRequestValid ) {
		log("spider: got best ip=%s sreq spiderTimeMS=%"INT64" "
		    "pri=%"INT32" uh48=%"INT64"",
		    iptoa(m_bestRequest->m_firstIp),
		    m_bestSpiderTimeMS,
		    (int32_t)m_bestRequest->m_priority,
		    m_bestRequest->getUrlHash48());
	}
	else if ( g_conf.m_logDebugSpider ) {
		log("spider: no best request for ip=%s",iptoa(m_scanningIp));
	}
	*/

	// ok, all done if nothing to add to doledb. i guess we were misled
	// that firstIp had something ready for us. maybe the url filters
	// table changed to filter/ban them all. if a new request/reply comes 
	// in for this firstIp then it will re-add an entry to waitingtree and
	// we will re-scan spiderdb. if we had something to spider but it was 
	// in the future the m_minFutureTimeMS will be non-zero, and we deal
	// with that below...
	if ( m_winnerTree.isEmpty() && ! m_minFutureTimeMS ) {
		// if we received new incoming requests while we were
		// scanning, which is happening for some crawls, then do
		// not nuke! just repeat later in populateDoledbFromWaitingTree
		if ( m_gotNewDataForScanningIp ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: received new requests, not "
				    "nuking misleading key");
			return true;
		}
		// note it - this can happen if no more to spider right now!
		if ( g_conf.m_logDebugSpider )
			log("spider: nuking misleading waitingtree key "
			    "firstIp=%s", iptoa(m_scanningIp));
		m_waitingTree.deleteNode ( 0,(char *)&m_waitingTreeKey,true);
		//log("spider: 7 del node for %s",iptoa(m_scanningIp));
		m_waitingTreeKeyValid = false;
		// note it
		uint64_t timestamp64 = m_waitingTreeKey.n1;
		timestamp64 <<= 32;
		timestamp64 |= m_waitingTreeKey.n0 >> 32;
		int32_t firstIp = m_waitingTreeKey.n0 &= 0xffffffff;
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: removed2 time=%"INT64" ip=%s from "
			    "waiting tree. nn=%"INT32".",
			    timestamp64, iptoa(firstIp),
			    m_waitingTree.m_numUsedNodes);

		m_waitingTable.removeKey  ( &firstIp  );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}


	// i've seen this happen, wtf?
	if ( m_winnerTree.isEmpty() && m_minFutureTimeMS ) { 
		// this will update the waiting tree key with minFutureTimeMS
		addDoleBufIntoDoledb ( NULL , false );
		return true;
	}

	// i am seeing dup uh48's in the m_winnerTree
	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;
	//char dbuf[147456];//3*MAX_WINNER_NODES*(8+1)];
	HashTableX dedup;
	int32_t ntn = m_winnerTree.getNumNodes();
	dedup.set ( 8,
		    0,
		    (int32_t)2*ntn, // # slots to initialize to
		    NULL,//dbuf,
		    0,//147456,//(int32_t)(3*MAX_WINNER_NODES*(8+1)),
		    false,
		    MAX_NICENESS,
		    "windt");

	///////////
	//
	// make winner tree into doledb list to add
	//
	///////////
	//m_doleBuf.reset();
	//m_doleBuf.setLabel("dolbuf");
	// first 4 bytes is offset of next doledb record to add to doledb
	// so we do not have to re-add the dolebuf to the cache and make it
	// churn. it is really inefficient.
	SafeBuf doleBuf;
	doleBuf.pushLong(4);
	int32_t added = 0;
	for ( int32_t node = m_winnerTree.getFirstNode() ; 
	      node >= 0 ; 
	      node = m_winnerTree.getNextNode ( node ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get data for that
		SpiderRequest *sreq2;
		sreq2 = (SpiderRequest *)m_winnerTree.getData ( node );
		// sanity
		if ( sreq2->m_firstIp != firstIp ) { char *xx=NULL;*xx=0; }
		//if ( sreq2->m_spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }
		if ( sreq2->m_ufn          < 0 ) { char *xx=NULL;*xx=0; }
		if ( sreq2->m_priority ==   -1 ) { char *xx=NULL;*xx=0; }
		// check for errors
		bool hadError = false;
		// parse it up
		int32_t winIp;
		int32_t winPriority;
		int32_t winHopCount;
		int64_t winSpiderTimeMS;
		int64_t winUh48;
		key192_t *winKey = (key192_t *)m_winnerTree.getKey ( node );
		parseWinnerTreeKey ( winKey ,
				     &winIp ,
				     &winPriority,
				     &winHopCount,
				     &winSpiderTimeMS ,
				     &winUh48 );
		// sanity
		if ( winIp != firstIp ) { char *xx=NULL;*xx=0;}
		if ( winUh48 != sreq2->getUrlHash48() ) { char *xx=NULL;*xx=0;}
		// make the doledb key
		key_t doleKey = g_doledb.makeKey ( winPriority,
						   // convert to secs from ms
						   winSpiderTimeMS / 1000     ,
						   winUh48 ,
						   false                    );
		// dedup. if we add dups the problem is is that they
		// overwrite the key in doledb yet the doleiptable count
		// remains undecremented and doledb is empty and never
		// replenished because the firstip can not be added to
		// waitingTree because doleiptable count is > 0. this was
		// causing spiders to hang for collections. i am not sure
		// why we should be getting dups in winnertree because they
		// have the same uh48 and that is the key in the tree.
		if ( dedup.isInTable ( &winUh48 ) ) {
			log("spider: got dup uh48=%"UINT64" dammit", winUh48);
			continue;
		}
		// count it
		added++;
		// do not allow dups
		dedup.addKey ( &winUh48 );
		// store doledb key first
		if ( ! doleBuf.safeMemcpy ( &doleKey, sizeof(key_t) ) ) 
			hadError = true;
		// then size of spiderrequest
		if ( ! doleBuf.pushLong ( sreq2->getRecSize() ) ) 
			hadError = true;
		// then the spiderrequest encapsulated
		if ( ! doleBuf.safeMemcpy ( sreq2 , sreq2->getRecSize() )) 
			hadError=true;
		// note and error
		if ( hadError ) {
			log("spider: error making doledb list: %s",
			    mstrerror(g_errno));
			    return true;
		}
	}

	// log("spider: added %"INT32" doledb recs to cache for cn=%i "
	//     "dolebufsize=%i",
	//     added,
	//     (int)m_collnum,
	//     (int)doleBuf.length());

	return addDoleBufIntoDoledb ( &doleBuf , false );//, 0 );
}



bool SpiderColl::validateDoleBuf ( SafeBuf *doleBuf ) {
	char *doleBufEnd = doleBuf->getBuf();
	// get offset
	char *pstart = doleBuf->getBufStart();
	char *p = pstart;
	int32_t jump = *(int32_t *)p;
	p += 4;
	// sanity
	if ( jump < 4 || jump > doleBuf->getLength() ) {
		char *xx=NULL;*xx=0; }
	bool gotIt = false;
	for ( ; p < doleBuf->getBuf() ; ) {
		if ( p == pstart + jump )
			gotIt = true;
		// first is doledbkey
		p += sizeof(key_t);
		// then size of spider request
		int32_t recSize = *(int32_t *)p;
		p += 4;
		// the spider request encapsulated
		SpiderRequest *sreq3;
		sreq3 = (SpiderRequest *)p;
		// point "p" to next spiderrequest
		if ( recSize != sreq3->getRecSize() ) { char *xx=NULL;*xx=0;}
		p += recSize;//sreq3->getRecSize();
		// sanity
		if ( p > doleBufEnd ) { char *xx=NULL;*xx=0; }
		if ( p < pstart     ) { char *xx=NULL;*xx=0; }
	}
	if ( ! gotIt ) { char *xx=NULL;*xx=0; }
	return true;
}



bool SpiderColl::addDoleBufIntoDoledb ( SafeBuf *doleBuf, bool isFromCache ) {
					// uint32_t cachedTimestamp ) {

	//validateDoleBuf ( doleBuf );

	////////////////////
	//
	// UPDATE WAITING TREE ENTRY
	//
	// Normally the "spidertime" is 0 for a firstIp. This will make it
	// a future time if it is not yet due for spidering.
	//
	////////////////////

	int32_t firstIp = m_waitingTreeKey.n0 & 0xffffffff;

	// sanity check. how did this happen? it messes up our crawl!
	// maybe a doledb add went through? so we should add again?
	int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
	if ( wn < 0 ) { 
		log("spider: waiting tree key removed while reading list for "
		    "%s (%"INT32")",m_coll,(int32_t)m_collnum);
		// play it safe and add it back for now...
		// when i try to break here in gdb it never happens because
		// of timing issues. heisenbug...
		// false = callForScan
		//if ( ! addToWaitingTree ( 0 , m_scanningIp , false ) )
		//	log("spider: failed to add wk2 to waiting tree: %s"
		//	    ,mstrerror(g_errno));
		return true;
	}

	/*

	  MDW: now we store multiple winners in doledb, so this logic
	  won't be used. maybe just rely on the crawldelay type logic
	  in Msg13.cpp to space things out. we could also deny locks if
	  too many urls being spidered for this ip.

	// how many spiders currently out for this ip?
	int32_t outNow=g_spiderLoop.getNumSpidersOutPerIp(m_scanningIp,m_collnum);

	// even if hadn't gotten list we can bail early if too many
	// spiders from this ip are out! 
	//int32_t out = g_spiderLoop.getNumSpidersOutPerIp ( m_scanningIp );
	if ( outNow >= m_bestMaxSpidersPerIp ) {
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: already got %"INT32" from this ip out. ip=%s",
			    m_bestMaxSpidersPerIp,
			    iptoa(m_scanningIp)
			    );
		// when his SpiderReply comes back it will call 
		// addWaitingTree with a "0" time so he'll get back in there
		//if ( wn < 0 ) { char *xx=NULL; *xx=0; }
		if ( wn >= 0 ) {
			m_waitingTree.deleteNode (wn,false );
			// note that
			//log("spdr: 1 del node %"INT32" for %s",wn,iptoa(firstIp));
		}
		// keep the table in sync now with the time
		m_waitingTable.removeKey( &m_bestRequest->m_firstIp );
		return true;
	}		
	*/

	//int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();//Local();

	// if best request has a future spiderTime, at least update
	// the wait tree with that since we will not be doling this request
	// right now.
	if ( m_winnerTree.isEmpty() && m_minFutureTimeMS && ! isFromCache ) {

		// save memory
		m_winnerTree.reset();
		m_winnerTable.reset();

		// if in the process of being added to doledb or in doledb...
		if ( m_doleIpTable.isInTable ( &firstIp ) ) {
			// sanity i guess. remove this line if it hits this!
			log("spider: wtf????");
			//char *xx=NULL;*xx=0;
			return true;
		}

		// before you set a time too far into the future, if we
		// did receive new spider requests, entertain those
		if ( m_gotNewDataForScanningIp &&
		     // we had twitter.com with a future spider date
		     // on the pe2 cluster but we kept hitting this, so
		     // don't do this anymore if we scanned a ton of bytes
		     // like we did for twitter.com because it uses all the
		     // resources when we can like 150MB of spider requests
		     // for a single firstip
		     m_totalBytesScanned < 30000 ) {
			if ( g_conf.m_logDebugSpider )
				log("spider: received new requests, not "
				    "updating waiting tree with future time");
			return true;
		}

		// get old time
		uint64_t oldSpiderTimeMS = m_waitingTreeKey.n1;
		oldSpiderTimeMS <<= 32;
		oldSpiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		// delete old node
		//int32_t wn = m_waitingTree.getNode(0,(char *)&m_waitingTreeKey);
		//if ( wn < 0 ) { char *xx=NULL;*xx=0; }
		if ( wn >= 0 ) {
			m_waitingTree.deleteNode3 (wn,false );
			//log("spdr: 2 del node %"INT32" for %s",wn,iptoa(firstIp));
		}

		// invalidate
		m_waitingTreeKeyValid = false;
		//int32_t  fip = m_bestRequest->m_firstIp;
		key_t wk2 = makeWaitingTreeKey ( m_minFutureTimeMS , firstIp );
		// log the replacement
		if ( g_conf.m_logDebugSpider )
			log("spider: scan replacing waitingtree key "
			    "oldtime=%"UINT32" newtime=%"UINT32" firstip=%s",
			    // bestpri=%"INT32" "
			    //"besturl=%s",
			    (uint32_t)(oldSpiderTimeMS/1000LL),
			    (uint32_t)(m_minFutureTimeMS/1000LL),
			    iptoa(firstIp)
			    //(int32_t)m_bestRequest->m_priority,
			    //	    m_bestRequest->m_url);
			    );
		// this should never fail since we deleted one above
		int32_t dn = m_waitingTree.addKey ( &wk2 );
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: RE-added time=%"INT64" ip=%s to "
			    "waiting tree node %"INT32"",
			    m_minFutureTimeMS , iptoa(firstIp),dn);

		// keep the table in sync now with the time
		m_waitingTable.addKey( &firstIp, &m_minFutureTimeMS );
		// sanity check
		if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}
		return true;
	}
	// we are coring here. i guess the best request or a copy of it
	// somehow started spidering since our last spider read, so i would
	// say we should bail on this spider scan! really i'm not exactly
	// sure what happened...
	// MDW: now we add a bunch of urls to doledb, so i guess we can't
	// check locks...
	/*
	int64_t key = makeLockTableKey ( m_bestRequest );
	if ( g_spiderLoop.m_lockTable.isInTable ( &key ) ) {
		log("spider: best request got doled out from under us");
		return true;
		char *xx=NULL;*xx=0; 
	}

	// make the doledb key first for this so we can add it
	key_t doleKey = g_doledb.makeKey ( m_bestRequest->m_priority     ,
					   // convert to seconds from ms
					   m_bestSpiderTimeMS / 1000     ,
					   m_bestRequest->getUrlHash48() ,
					   false                         );
	

	if ( g_conf.m_logDebugSpider )
		log("spider: got winner pdocid=%"INT64" url=%s",
		    m_bestRequest->m_probDocId,
		    m_bestRequest->m_url);


	// make it into a doledb record
	char *p = m_doleBuf;
	*(key_t *)p = doleKey;
	p += sizeof(key_t);
	int32_t recSize = m_bestRequest->getRecSize();
	*(int32_t *)p = recSize;
	p += 4;
	gbmemcpy ( p , m_bestRequest , recSize );
	p += recSize;
	// sanity check
	if ( p - m_doleBuf > (int32_t)MAX_DOLEREC_SIZE ) { char *xx=NULL;*xx=0; }
	*/

	// how did this happen?
	//if ( ! m_msg1Avail ) { char *xx=NULL;*xx=0; }

	char *doleBufEnd = doleBuf->getBuf();

	// add it to doledb ip table now so that waiting tree does not
	// immediately get another spider request from this same ip added
	// to it while the msg4 is out. but if add failes we totally bail
	// with g_errno set
	//
	// crap, i think this could be slowing us down when spidering
	// a single ip address. maybe use msg1 here not msg4?
	//if ( ! addToDoleTable ( m_bestRequest ) ) return true;
	// . MDW: now we have a list of doledb records in a SafeBuf:
	// . scan the requests in safebuf

	// get offset
	char *p = doleBuf->getBufStart();
	int32_t jump = *(int32_t *)p;
	// sanity
	if ( jump < 4 || jump > doleBuf->getLength() ) {
		char *xx=NULL;*xx=0; }
	// the jump includes itself
	p += jump;
	//for ( ; p < m_doleBuf.getBuf() ; ) {
	// save it
	char *doledbRec = p;
	// first is doledbkey
	p += sizeof(key_t);
	// then size of spider request
	p += 4;
	// the spider request encapsulated
	SpiderRequest *sreq3;
	sreq3 = (SpiderRequest *)p;
	// point "p" to next spiderrequest
	p += sreq3->getRecSize();

	// sanity
	if ( p > doleBufEnd ) { char *xx=NULL;*xx=0; }

	// for caching logic below, set this
	int32_t doledbRecSize = sizeof(key_t) + 4 + sreq3->getRecSize();
	// process sreq3 my incrementing the firstip count in 
	// m_doleIpTable
	if ( ! addToDoleTable ( sreq3 ) ) return true;	

	// only add the top key for now!
	//break;

	// 	// this logic is now in addToDoleTable()
	// 	// . if it was empty it is no longer
	// 	// . we have this flag here to avoid scanning empty doledb 
	// 	//   priorities because it saves us a msg5 call to doledb in 
	// 	//   the scanning loop
	// 	//int32_t bp = sreq3->m_priority;//m_bestRequest->m_priority;
	// 	//if ( bp <  0                     ) { char *xx=NULL;*xx=0; }
	// 	//if ( bp >= MAX_SPIDER_PRIORITIES ) { char *xx=NULL;*xx=0; }
	// 	//m_isDoledbEmpty [ bp ] = 0;
	// }

	// now cache the REST of the spider requests to speed up scanning.
	// better than adding 400 recs per firstip to doledb because
	// msg5's call to RdbTree::getList() is way faster.
	// even if m_doleBuf is from the cache, re-add it to lose the
	// top rec.
	// allow this to add a 0 length record otherwise we keep the same
	// old url in here and keep spidering it over and over again!

	//bool addToCache = false;
	//if( skipSize && m_doleBuf.length() - skipSize > 0 ) addToCache =true;
	// if winnertree was empty, then we might have scanned like 10M
	// twitter.com urls and not wanted any of them, so we don't want to
	// have to keep redoing that!
	//if ( m_doleBuf.length() == 0 && ! isFromCache ) addToCache = true;

	RdbCache *wc = &g_spiderLoop.m_winnerListCache;

	// remove from cache? if we added the last spider request in the
	// cached dolebuf to doledb then remove it from cache so it's not
	// a cached empty dolebuf and we recompute it not using the cache.
	if ( isFromCache && p >= doleBufEnd ) {
		//if ( addToCache ) { char *xx=NULL;*xx=0; }
		// debug note
		// if ( m_collnum == 18752 )
		// 	log("spider: rdbcache: adding single byte. skipsize=%i"
		// 	    ,doledbRecSize);
		// let's get this working right...
		//wc->removeKey ( collnum , k , start );
		//wc->markDeletedRecord(start);
		// i don't think we can remove keys from cache so add
		// a rec with a byte size of 1 to indicate for us to ignore.
		// set the timestamp to 12345 so the getRecord above will
		// not get it and promote it in the linked list.
		char byte = 0;
		key_t cacheKey;
		cacheKey.n0 = firstIp;
		cacheKey.n1 = 0;
		//wc->verify();
		wc->addRecord ( m_collnum,
				(char *)&cacheKey,
				&byte ,
				1 ,
		 		12345 );//cachedTimestamp );
		//wc->verify();
	}

	// if it wasn't in the cache and it was only one record we
	// obviously do not want to add it to the cache.
	else if ( p < doleBufEnd ) { // if ( addToCache ) {
		key_t cacheKey;
		cacheKey.n0 = firstIp;
		cacheKey.n1 = 0;
		char *x = doleBuf->getBufStart();
		// the new offset is the next record after the one we
		// just added to doledb
		int32_t newJump = (int32_t)(p - x);
		int32_t oldJump = *(int32_t *)x;
		// NO! we do a copy in rdbcache and copy the thing over
		// since we promote it. so this won't work...
		*(int32_t *)x = newJump;
		if ( newJump >= doleBuf->getLength() ) { char *xx=NULL;*xx=0;}
		if ( newJump < 4 ) { char *xx=NULL;*xx=0;}
		if ( g_conf.m_logDebugSpider ) // || m_collnum == 18752 )
			log("spider: rdbcache: updating "
			    "%"INT32" bytes of SpiderRequests "
			    "to winnerlistcache for ip %s oldjump=%"INT32
			    " newJump=%"INT32" ptr=0x%"PTRFMT,
			    doleBuf->length(),iptoa(firstIp),oldJump,
			    newJump,
			    (PTRTYPE)x);
		//validateDoleBuf ( doleBuf );
		//wc->verify();
		// inherit timestamp. if 0, RdbCache will set to current time
		// don't re-add just use the same modified buffer so we
		// don't churn the cache.
		// but do add it to cache if not already in there yet.
		if ( ! isFromCache ) {
			// if ( m_collnum == 18752 )
			// 	log("spider: rdbcache: adding record a new "
			// 	    "dbufsize=%i",(int)doleBuf->length());
			wc->addRecord ( m_collnum,
					(char *)&cacheKey,
					doleBuf->getBufStart(),//+ skipSize ,
					doleBuf->length() ,//- skipSize ,
					0);//cachedTimestamp );
		}
		//validateDoleBuf( doleBuf );
		/*
		// test it
		char *testPtr;
		int32_t testLen;
		bool inCache2 = wc->getRecord ( m_collnum     ,
						(char *)&cacheKey ,
						&testPtr,
						&testLen,
						false, // doCopy?
						600, // maxAge,600 secs
						true ,// incCounts
						NULL , // rec timestamp
						true );  // promote?
		if ( ! inCache2 ) { char *xx=NULL;*xx=0; }
		if ( testLen != m_doleBuf.length() ) {char *xx=NULL;*xx=0; }
		if ( *(int32_t *)testPtr != newJump ){char *xx=NULL;*xx=0; }
		SafeBuf tmp;
		tmp.setBuf ( testPtr , testLen , testLen , false );
		validateDoleBuf ( &tmp );
		*/
		//wc->verify();
	}

	// and the whole thing is no longer empty
	//m_allDoledbPrioritiesEmpty = 0;//false;
	//m_lastEmptyCheck = 0;


	m_msg4Start = gettimeofdayInMilliseconds();

	//RdbList *tmpList = &m_msg1.m_tmpList;

	// keep it on stack now that doledb is tree-only
	RdbList tmpList;

	// only add one doledb record at a time now since we
	// have the winnerListCache
	//m_doleBuf.setLength ( skipSize );

	//tmpList.setFromSafeBuf ( &m_doleBuf , RDB_DOLEDB );
	tmpList.setFromPtr ( doledbRec , doledbRecSize , RDB_DOLEDB );

	// now that doledb is tree-only and never dumps to disk, just
	// add it directly
	g_doledb.m_rdb.addList ( m_collnum , &tmpList , MAX_NICENESS );

	if ( g_conf.m_logDebugSpider )
		log("spider: adding doledb tree node size=%"INT32"",
		    doledbRecSize);


	// and it happens right away. just add it locally.
	bool status = true;

	// . use msg4 to transmit our guys into the rdb, RDB_DOLEDB
	// . no, use msg1 for speed, so we get it right away!!
	// . we' already incremented doleiptable counts... so we need to
	//   make sure this happens!!!
	/*
	bool status = m_msg1.addList ( tmpList ,
				       RDB_DOLEDB    ,
				       m_collnum    ,
				       this          ,
				       doledWrapper  ,
				       false , // forcelocal?
				       // Rdb.cpp can't call dumpTree()
				       // if niceness is 0!
				       MAX_NICENESS);
	// if it blocked set this to true so we do not reuse it
	if ( ! status ) m_msg1Avail = false;
	*/

	int32_t storedFirstIp = (m_waitingTreeKey.n0) & 0xffffffff;

	// log it
	if ( g_conf.m_logDebugSpcache ) {
		uint64_t spiderTimeMS = m_waitingTreeKey.n1;
		spiderTimeMS <<= 32;
		spiderTimeMS |= (m_waitingTreeKey.n0 >> 32);
		logf(LOG_DEBUG,"spider: removing doled waitingtree key"
		     " spidertime=%"UINT64" firstIp=%s "
		     //"pri=%"INT32" "
		     //"url=%s"
		     ,spiderTimeMS,
		     iptoa(storedFirstIp)
		     //(int32_t)m_bestRequest->m_priority,
		     //m_bestRequest->m_url);
		     );
	}

	// before adding to doledb remove from waiting tree so we do not try
	// to readd to doledb...
	m_waitingTree.deleteNode ( 0, (char *)&m_waitingTreeKey , true);
	m_waitingTable.removeKey  ( &storedFirstIp );
	//log("spider: 3 del node for %s",iptoa(storedFirstIp));
	
	// invalidate
	m_waitingTreeKeyValid = false;

	// sanity check
	if ( ! m_waitingTable.m_isWritable ) { char *xx=NULL;*xx=0;}

	// note that ip as being in dole table
	if ( g_conf.m_logDebugSpider )
		log("spider: added best sreq for ip=%s to doletable AND "
		    "removed from waiting table",
		    iptoa(firstIp));

	// save memory
	m_winnerTree.reset();
	m_winnerTable.reset();

	//validateDoleBuf( doleBuf );

	// add did not block
	return status;
}



uint64_t SpiderColl::getSpiderTimeMS ( SpiderRequest *sreq,
				       int32_t ufn,
				       SpiderReply *srep,
				       uint64_t nowGlobalMS ) {
	// . get the scheduled spiderTime for it
	// . assume this SpiderRequest never been successfully spidered
	int64_t spiderTimeMS = ((uint64_t)sreq->m_addedTime) * 1000LL;
	// how can added time be in the future? did admin set clock back?
	//if ( spiderTimeMS > nowGlobalMS ) spiderTimeMS = nowGlobalMS;
	// if injecting for first time, use that!
	if ( ! srep && sreq->m_isInjecting ) return spiderTimeMS;
	if ( ! srep && sreq->m_isPageReindex ) return spiderTimeMS;


	//log("spider: getting spider time %"INT64, spiderTimeMS);
	// to avoid hammering an ip, get last time we spidered it...
	int64_t lastMS ;
	lastMS = m_lastDownloadCache.getLongLong ( m_collnum       ,
						   sreq->m_firstIp ,
						   -1              , // maxAge
						   true            );// promote
	// -1 means not found
	if ( (int64_t)lastMS == -1 ) lastMS = 0;
	// sanity
	if ( (int64_t)lastMS < -1 ) { 
		log("spider: corrupt last time in download cache. nuking.");
		lastMS = 0;
	}
	// min time we can spider it
	int64_t minSpiderTimeMS1 = lastMS + m_cr->m_spiderIpWaits[ufn];
	// if not found in cache
	if ( lastMS == -1 ) minSpiderTimeMS1 = 0LL;

	/////////////////////////////////////////////////
	/////////////////////////////////////////////////
	// crawldelay table check!!!!
	/////////////////////////////////////////////////
	/////////////////////////////////////////////////
	int32_t *cdp = (int32_t *)m_cdTable.getValue ( &sreq->m_domHash32 );
	int64_t minSpiderTimeMS2 = 0;
	// limit to 60 seconds crawl delay. 
	// help fight SpiderReply corruption too
	if ( cdp && *cdp > 60000 ) *cdp = 60000;
	if ( cdp && *cdp >= 0 ) minSpiderTimeMS2 = lastMS + *cdp;

	// wait 5 seconds for all outlinks in order for them to have a
	// chance to get any link info that might have been added
	// from the page that supplied this outlink
	// CRAP! this slows down same ip spidering i think... yeah, without
	// this it seems the spiders are always at 10 (sometimes 8 or 9) 
	// when i spider techcrunch.com.
	//spiderTimeMS += 5000;

	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS1 ) spiderTimeMS = minSpiderTimeMS1;
	if ( spiderTimeMS < minSpiderTimeMS2 ) spiderTimeMS = minSpiderTimeMS2;
	// if no reply, use that
	if ( ! srep ) return spiderTimeMS;
	// if this is not the first try, then re-compute the spiderTime
	// based on that last time
	// sanity check
	if ( srep->m_spideredTime <= 0 ) {
		// a lot of times these are corrupt! wtf???
		//spiderTimeMS = minSpiderTimeMS;
		return spiderTimeMS;
		//{ char*xx=NULL;*xx=0;}
	}
	// compute new spiderTime for this guy, in seconds
	int64_t waitInSecs = (uint64_t)(m_cr->m_spiderFreqs[ufn]*3600*24.0);
	// do not spider more than once per 15 seconds ever!
	// no! might be a query reindex!!
	/*
	if ( waitInSecs < 15 && ! sreq->m_isPageReindex ) { //urlIsDocId ) { 
		static bool s_printed = false;
		if ( ! s_printed ) {
			s_printed = true;
			log("spider: min spider wait is 15 seconds, "
			    "not %"UINT64" (ufn=%"INT32")",waitInSecs,ufn);
		}
		waitInSecs = 15;//900; this was 15 minutes
	}
	*/
	// in fact, force docid based guys to be zero!
	//if ( sreq->m_urlIsDocId ) waitInSecs = 0;
	if ( sreq->m_isPageReindex ) waitInSecs = 0;
	// when it was spidered
	int64_t lastSpideredMS = ((uint64_t)srep->m_spideredTime) * 1000;
	// . when we last attempted to spider it... (base time)
	// . use a lastAttempt of 0 to indicate never! 
	// (first time)
	int64_t minSpiderTimeMS3 = lastSpideredMS + (waitInSecs * 1000LL);
	//  ensure min
	if ( spiderTimeMS < minSpiderTimeMS3 ) spiderTimeMS = minSpiderTimeMS3;
	// sanity
	if ( (int64_t)spiderTimeMS < 0 ) { char *xx=NULL;*xx=0; }

	return spiderTimeMS;
}



// . returns false with g_errno set on error
// . Rdb.cpp should call this when it receives a doledb key
// . when trying to add a SpiderRequest to the waiting tree we first check
//   the doledb table to see if doledb already has an sreq from this firstIp
// . therefore, we should add the ip to the dole table before we launch the
//   Msg4 request to add it to doledb, that way we don't add a bunch from the
//   same firstIP to doledb
bool SpiderColl::addToDoleTable ( SpiderRequest *sreq ) {
	// update how many per ip we got doled
	int32_t *score = (int32_t *)m_doleIpTable.getValue32 ( sreq->m_firstIp );
	// debug point
	if ( g_conf.m_logDebugSpider ){//&&1==2 ) { // disable for now, spammy
		int64_t  uh48 = sreq->getUrlHash48();
		int64_t pdocid = sreq->getParentDocId();
		int32_t ss = 1;
		if ( score ) ss = *score + 1;
		// if for some reason this collides with another key
		// already in doledb then our counts are off
		log("spider: added to doletbl uh48=%"UINT64" parentdocid=%"UINT64" "
		    "ipdolecount=%"INT32" ufn=%"INT32" priority=%"INT32" firstip=%s",
		    uh48,pdocid,ss,(int32_t)sreq->m_ufn,(int32_t)sreq->m_priority,
		    iptoa(sreq->m_firstIp));
	}
	// we had a score there already, so inc it
	if ( score ) {
		// inc it
		*score = *score + 1;
		// sanity check
		if ( *score <= 0 ) { char *xx=NULL;*xx=0; }
		// only one per ip!
		// not any more! we allow MAX_WINNER_NODES per ip!
		if ( *score > MAX_WINNER_NODES )
			log("spider: crap. had %"INT32" recs in doledb for %s "
			    "from %s."
			    "how did this happen?",
			    (int32_t)*score,m_coll,iptoa(sreq->m_firstIp));
		// now we log it too
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: added ip=%s to doleiptable "
			    "(score=%"INT32")",
			    iptoa(sreq->m_firstIp),*score);
	}
	else {
		// ok, add new slot
		int32_t val = 1;
		if ( ! m_doleIpTable.addKey ( &sreq->m_firstIp , &val ) ) {
			// log it, this is bad
			log("spider: failed to add ip %s to dole ip tbl",
			    iptoa(sreq->m_firstIp));
			// return true with g_errno set on error
			return false;
		}
		// now we log it too
		if ( g_conf.m_logDebugSpider )
			log(LOG_DEBUG,"spider: added ip=%s to doleiptable "
			    "(score=1)",iptoa(sreq->m_firstIp));
		// sanity check
		//if ( ! m_doleIpTable.m_isWritable ) { char *xx=NULL;*xx=0;}
	}

	// . these priority slots in doledb are not empty
	// . unmark individual priority buckets
	// . do not skip them when scanning for urls to spiderd
	int32_t pri = sreq->m_priority;
	m_isDoledbEmpty[pri] = 0;		
	// reset scan for this priority in doledb
	m_nextKeys     [pri] =g_doledb.makeFirstKey2 ( pri );

	return true;
}



// . decrement priority
// . will also set m_sc->m_nextDoledbKey
// . will also set m_sc->m_msg5StartKey
void SpiderColl::devancePriority() {
	// try next
	m_pri2 = m_pri2 - 1;
	// how can this happen?
	if ( m_pri2 < -1 ) m_pri2 = -1;
	// bogus?
	if ( m_pri2 < 0 ) return;
	// set to next priority otherwise
	//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
	m_nextDoledbKey = m_nextKeys [m_pri2];
	// and the read key
	m_msg5StartKey = m_nextDoledbKey;
}



void SpiderColl::setPriority(int32_t pri) {
	m_pri2 = pri;
	m_nextDoledbKey = m_nextKeys [ m_pri2 ];
	m_msg5StartKey = m_nextDoledbKey;
}


bool SpiderColl::printStats ( SafeBuf &sb ) {
	return true;
}




key_t makeWaitingTreeKey ( uint64_t spiderTimeMS , int32_t firstIp ) {
	// sanity
	if ( ((int64_t)spiderTimeMS) < 0 ) { char *xx=NULL;*xx=0; }
	// make the wait tree key
	key_t wk;
	wk.n1 = (spiderTimeMS>>32);
	wk.n0 = (spiderTimeMS&0xffffffff);
	wk.n0 <<= 32;
	wk.n0 |= (uint32_t)firstIp;
	// sanity
	if ( wk.n1 & 0x8000000000000000LL ) { char *xx=NULL;*xx=0; }
	return wk;
}



