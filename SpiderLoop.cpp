#include "gb-include.h"

#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderLoop.h"
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
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"


// . this was 10 but cpu is getting pegged, so i set to 45
// . we consider the collection done spidering when no urls to spider
//   for this many seconds
// . i'd like to set back to 10 for speed... maybe even 5 or less
// . back to 30 from 20 to try to fix crawls thinking they are done
//   maybe because of the empty doledb logic taking too long?
//#define SPIDER_DONE_TIMER 30
// try 45 to prevent false revivals
//#define SPIDER_DONE_TIMER 45
// try 30 again since we have new localcrawlinfo update logic much faster
//#define SPIDER_DONE_TIMER 30
// neo under heavy load go to 60
//#define SPIDER_DONE_TIMER 60
// super overloaded
//#define SPIDER_DONE_TIMER 90
#define SPIDER_DONE_TIMER 20



bool SpiderLoop::printLockTable ( ) {
	// count locks
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// get the key
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// show it
		log("dump: lock. "
		    "lockkey=%"INT64" "
		    "spiderout=%"INT32" "
		    "confirmed=%"INT32" "
		    "firstip=%s "
		    "expires=%"INT32" "
		    "hostid=%"INT32" "
		    "timestamp=%"INT32" "
		    "sequence=%"INT32" "
		    "collnum=%"INT32" "
		    ,lockKey
		    ,(int32_t)(lock->m_spiderOutstanding)
		    ,(int32_t)(lock->m_confirmed)
		    ,iptoa(lock->m_firstIp)
		    ,lock->m_expires
		    ,lock->m_hostId
		    ,lock->m_timestamp
		    ,lock->m_lockSequence
		    ,(int32_t)lock->m_collnum
		    );
	}
	return true;
}



/////////////////////////
/////////////////////////      SPIDERLOOP
/////////////////////////

static void indexedDocWrapper ( void *state ) ;
static void doneSleepingWrapperSL ( int fd , void *state ) ;

// a global class extern'd in .h file
SpiderLoop g_spiderLoop;

SpiderLoop::SpiderLoop ( ) {
	m_crx = NULL;
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
}

SpiderLoop::~SpiderLoop ( ) { reset(); }

// free all doc's
void SpiderLoop::reset() {
	// delete all doc's in use
	for ( int32_t i = 0 ; i < MAX_SPIDERS ; i++ ) {
		if ( m_docs[i] ) {
			mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
			delete (m_docs[i]);
		}
		m_docs[i] = NULL;
		//m_lists[i].freeList();
	}
	m_list.freeList();
	m_lockTable.reset();
	m_lockCache.reset();
	m_winnerListCache.reset();
}

void updateAllCrawlInfosSleepWrapper ( int fd , void *state ) ;



void SpiderLoop::startLoop ( ) {
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	//m_cri     = 0;
	m_crx = NULL;
	m_activeListValid = false;
	m_activeListModified = false;
	m_activeList = NULL;
	m_recalcTime = 0;
	m_recalcTimeValid = false;
	// falsify this flag
	m_outstanding1 = false;
	// not flushing
	m_msg12.m_gettingLocks = false;
	// we aren't in the middle of waiting to get a list of SpiderRequests
	m_gettingDoledbList = false;
	// we haven't registered for sleeping yet
	m_isRegistered = false;
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
	// . m_maxUsed is the largest i such that m_docs[i] is in use
	// . -1 means there are no used m_docs's
	m_maxUsed = -1;
	m_numSpidersOut = 0;
	m_processed = 0;
	// for locking. key size is 8 for easier debugging
	m_lockTable.set ( 8,sizeof(UrlLock),0,NULL,0,false,MAX_NICENESS,
			  "splocks", true ); // useKeyMagic? yes.

	if ( ! m_lockCache.init ( 20000 , // maxcachemem
				  4     , // fixedatasize
				  false , // supportlists?
				  1000  , // maxcachenodes
				  false , // use half keys
				  "lockcache", // dbname
				  false  ) )
		log("spider: failed to init lock cache. performance hit." );


	if ( ! m_winnerListCache.init ( 20000000 , // maxcachemem, 20MB
					-1     , // fixedatasize
					false , // supportlists?
					10000  , // maxcachenodes
					false , // use half keys
					"winnerspidercache", // dbname
					false  ) )
		log("spider: failed to init winnerlist cache. slows down.");

	// dole some out
	//g_spiderLoop.doleUrls1();
	// spider some urls that were doled to us
	//g_spiderLoop.spiderDoledUrls( );
	// sleep for .1 seconds = 100ms
	if (!g_loop.registerSleepCallback(50,this,doneSleepingWrapperSL))
	{
		log(LOG_ERROR, "build: Failed to register timer callback. Spidering is permanently disabled. Restart to fix.");
	}

	// crawlinfo updating
	// save bandwidth for now make this every 4 seconds not 1 second
	// then try not to send crawlinfo the host should already have.
	// each collrec can have a checksum for each host of the last
	// info we sent it. but we should resend all every 100 secs anyway
	// in case host when dead.
	// now that we only send the info on startup and if changed,
	// let's move back down to 1 second
	// . make it 20 seconds because handlerequestc1 is always on
	//   profiler when we have thousands of collections
	if ( !g_loop.registerSleepCallback(20000,
					   this,
					   updateAllCrawlInfosSleepWrapper))
	{
		log(LOG_ERROR, "build: failed to register updatecrawlinfowrapper");
	}
		
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
}



// call this every 50ms it seems to try to spider urls and populate doledb
// from the waiting tree
void doneSleepingWrapperSL ( int fd , void *state ) {
	//SpiderLoop *THIS = (SpiderLoop *)state;
	// dole some out
	//g_spiderLoop.doleUrls1();

	// if spidering disabled then do not do this crap
	if ( ! g_conf.m_spideringEnabled )  return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;
	
	//if ( ! g_conf.m_webSpideringEnabled )  return;
	// or if trying to exit
	if ( g_process.m_mode == EXIT_MODE ) return;	
	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;

	// wait for clock to sync with host #0
	if ( ! isClockInSync() ) { 
		// let admin know why we are not spidering
		static char s_printed = false;
		if ( ! s_printed ) {
			logf(LOG_DEBUG,"spider: NOT SPIDERING until clock "
			     "is in sync with host #0.");
			s_printed = true;
		}
		return;
	}

	//if ( g_hostdb.hasDeadHost() ) return;

	static int32_t s_count = -1;
	// count these calls
	s_count++;

	int32_t now = getTimeLocal();


	// reset SpiderColl::m_didRound and m_nextDoledbKey if it is maxed
	// because we might have had a lock collision
	//int32_t nc = g_collectiondb.m_numRecs;

 redo:

	// point to head of active linked list of collection recs
	CollectionRec *nextActive = g_spiderLoop.getActiveList(); 
	collnum_t nextActiveCollnum ;
	if ( nextActive ) nextActiveCollnum = nextActive->m_collnum;

	//for ( int32_t i = 0 ; i < nc ; i++ ) {
	for ( ; nextActive ;  ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// before we assign crp to nextActive, ensure that it did
		// not get deleted on us.
		// if the next collrec got deleted, tr will be NULL
		CollectionRec *tr = g_collectiondb.getRec( nextActiveCollnum );
		// if it got deleted or restarted then it will not
		// match most likely
		if ( tr != nextActive ) {
			// this shouldn't happen much so log it
			log("spider: collnum %"INT32" got deleted. "
			    "rebuilding active list",
			    (int32_t)nextActiveCollnum);
			// rebuild the active list now
			goto redo;
		}
		// now we become him
		CollectionRec *crp = nextActive;
		// update these two vars for next iteration
		nextActive        = crp->m_nextActive;
		nextActiveCollnum = -1;
		if ( nextActive ) nextActiveCollnum = nextActive->m_collnum;
		// if list was modified a collection was deleted/added
		//if ( g_spiderLoop.m_activeListModified ) goto top;
		// // get collectionrec
		// CollectionRec *cr = g_collectiondb.getRec(i);
		// if ( ! cr ) continue;
		// skip if not enabled
		if ( ! crp->m_spideringEnabled ) continue;

		// get it
		//SpiderColl *sc = cr->m_spiderColl;
		SpiderColl *sc = g_spiderCache.getSpiderColl(crp->m_collnum);
		// skip if none
		if ( ! sc ) continue;
		// also scan spiderdb to populate waiting tree now but
		// only one read per 100ms!!
		// MDW: try taking this out
		//if ( (s_count % 10) == 0 ) {
		// always do a scan at startup & every 24 hrs
		// AND at process startup!!!
		if ( ! sc->m_waitingTreeNeedsRebuild &&
		     now - sc->m_lastScanTime > 24*3600) {
			// if a scan is ongoing, this will re-set it
			sc->m_nextKey2.setMin();
			sc->m_waitingTreeNeedsRebuild = true;
			log(LOG_INFO,
			    "spider: hit spider queue "
			    "rebuild timeout for %s (%"INT32")",
			    crp->m_coll,(int32_t)crp->m_collnum);
			// flush the ufn table
			//clearUfnTable();
		}
	
	
//@@@@@@
//@@@ BR: Why not check m_waitingTreeNeedsRebuild before calling??
		// try this then. it just returns if
		// sc->m_waitingTreeNeedsRebuild is false so it
		// should be fast in those cases
		// re-entry is false because we are entering for the first time
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Calling populateWaitingTreeFromSpiderdb", __FILE__, __func__, __LINE__);
		sc->populateWaitingTreeFromSpiderdb ( false );


		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Calling populateDoledbFromWaitingTree", __FILE__, __func__, __LINE__);
		sc->populateDoledbFromWaitingTree ( );
		// . skip if still loading doledb lists from disk this round
		// . we use m_didRound to share spiders across all collections
		//if ( ! sc->m_didRound ) continue;
		// ensure at the top!
		//if( sc->m_pri2!=MAX_SPIDER_PRIORITIES-1){char*xx=NULL;*xx=0;}
		// ok, reset it so it can start a new doledb scan
		//sc->m_didRound = false;
		// reset this as well. if there are no spiderRequests
		// available on any priority level for this collection,
		// then it will remain true. but if we attempt to spider
		// a url, or can't spider a url b/c of a max oustanding
		// constraint, we set this to false. this is used to
		// send notifications when a crawl is basically in hiatus.
		//sc->m_encounteredDoledbRecs = false;
		//sc->m_nextDoledbKey.setMin();
		// if list was modified a collection was deleted/added
		//if ( g_spiderLoop.m_activeListModified ) goto top;
	}

	// set initial priority to the highest to start spidering there
	//g_spiderLoop.m_pri = MAX_SPIDER_PRIORITIES - 1;

	// if recently called, do not call again from the sleep wrapper
	int64_t nowms = gettimeofdayInMillisecondsLocal();
	if ( nowms - g_spiderLoop.m_lastCallTime < 50 )
		return;

	// if we have a ton of collections, reduce cpu load from calling
	// spiderDoledUrls()
	static uint64_t s_skipCount = 0;
	s_skipCount++;
	// so instead of every 50ms make it every 200ms if we got
	// 100+ collections in use.
	//CollectionRec *tmp = g_spiderLoop.getActiveList(); 
	g_spiderLoop.getActiveList(); 
	int32_t activeListCount = g_spiderLoop.m_activeListCount;
	if ( ! g_spiderLoop.m_activeListValid )
		activeListCount = 0;
	int32_t skip = 1;
	if ( activeListCount >= 50 ) 
		skip = 2;
	if ( activeListCount >= 100 ) 
		skip = 4;
	if ( activeListCount >= 200 ) 
		skip = 8;
	if ( ( s_skipCount % skip ) != 0 ) 
		return;


	// spider some urls that were doled to us
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Calling spiderDoledUrls", __FILE__, __func__, __LINE__);
	g_spiderLoop.spiderDoledUrls( );
}


void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// process the doledb list and try to launch a spider
	g_spiderLoop.gotDoledbList2();
	// regardless of whether that blocked or not try to launch another 
	// and try to get the next SpiderRequest from doledb
	g_spiderLoop.spiderDoledUrls();
}




//////////////////////////
//////////////////////////
//
// The second KEYSTONE function.
//
// Scans doledb and spiders the doledb records.
//
// Doledb records contain SpiderRequests ready for spidering NOW.
//
// 1. gets all locks from all hosts in the shard
// 2. sends confirm msg to all hosts if lock acquired:
//    - each host will remove from doledb then
//    - assigned host will also add new "0" entry to waiting tree if need be
//    - calling addToWaitingTree() will trigger populateDoledbFromWaitingTree()
//      to add a new entry into waiting tree, not the one just locked.
// 3. makes a new xmldoc class for that url and calls indexDoc() on it
//
//////////////////////////
//////////////////////////

// now check our RDB_DOLEDB for SpiderRequests to spider!
void SpiderLoop::spiderDoledUrls ( ) {

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	//char *reb = g_rebalance.getNeedsRebalance();
	//if ( ! reb || *reb ) {return;

	//if ( g_conf.m_logDebugSpider )
	//	log("spider: trying to get a doledb rec to spider. "
	//	    "currentnumout=%"INT32"",m_numSpidersOut);

	m_lastCallTime = gettimeofdayInMillisecondsLocal();

	// when getting a lock we keep a ptr to the SpiderRequest in the
	// doledb list, so do not try to read more just yet until we know
	// if we got the lock or not
	if ( m_msg12.m_gettingLocks ) {
		// this should no longer happen
		char *xx=NULL; *xx=0;
		// make a note, maybe this is why spiders are deficient?
		if ( g_conf.m_logDebugSpider )
			log("spider: failed to get doledb rec to spider: "
			    "msg12 is getting locks");
		return;
	}

	// turn on for now
	//g_conf.m_logDebugSpider = 1;

 collLoop:

	// start again at head if this is NULL
	if ( ! m_crx ) m_crx = getActiveList();

	bool firstTime = true;

	// detect overlap
	//CollectionRec *start = m_crx;
	m_bookmark = m_crx;

	// log this now
	//logf(LOG_DEBUG,"spider: getting collnum to dole from");
	// get this
	m_sc = NULL;
	// avoid infinite loops
	//int32_t count = g_collectiondb.m_numRecs;
	// set this in the loop
	CollectionRec *cr = NULL;
	uint32_t nowGlobal = 0;
	// debug
	//log("spider: m_cri=%"INT32"",(int32_t)m_cri);
	// . get the next collection to spider
	// . just alternate them
	//for ( ; count > 0 ; m_cri++ , count-- ) {

	m_launches = 0;

 subloop:

	QUICKPOLL(MAX_NICENESS);

	// must be spidering to dole out
	if ( ! g_conf.m_spideringEnabled ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, spidering disabled", __FILE__, __func__, __LINE__);
		return;
	}
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, spidering disabled (2)", __FILE__, __func__, __LINE__);
		return;
	}

	// or if trying to exit
	if ( g_process.m_mode == EXIT_MODE ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, shutting down", __FILE__, __func__, __LINE__);
		return;	
	}
	
	// if we don't have all the url counts from all hosts, then wait.
	// one host is probably down and was never up to begin with
	if ( ! s_countsAreValid ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, counts not valid", __FILE__, __func__, __LINE__);
		return;
	}
	
	// if we do not overlap ourselves
	if ( m_gettingDoledbList ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, already getting DoledbList", __FILE__, __func__, __LINE__);
		return;
	}
	
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, in read-only mode", __FILE__, __func__, __LINE__);
		return;
	}
	
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, doing daily merge", __FILE__, __func__, __LINE__);
		return;
	}
		
	// skip if too many udp slots being used
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, using max UDP slots", __FILE__, __func__, __LINE__);
		return;
	}
	
	// stop if too many out. this is now 50 down from 500.
	if ( m_numSpidersOut >= MAX_SPIDERS ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, reached max spiders", __FILE__, __func__, __LINE__);
		return;
	}
	
	// a new global conf rule
	if ( m_numSpidersOut >= g_conf.m_maxTotalSpiders ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, reached max total spiders", __FILE__, __func__, __LINE__);
		return;
	}
		
	// bail if no collections
	if ( g_collectiondb.m_numRecs <= 0 ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, no collections", __FILE__, __func__, __LINE__);
		return;
	}

	// not while repairing
	if ( g_repairMode ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, in repair mode", __FILE__, __func__, __LINE__);
		return;
	}
		
	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, not in sync with host#0", __FILE__, __func__, __LINE__);
		return;
	}
	
	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( ! g_pingServer.m_hostsConfInAgreement ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, host config disagreement", __FILE__, __func__, __LINE__);
		return;
	}
		
	// if nothin in the active list then return as well
	if ( ! m_activeList ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, nothing in active list", __FILE__, __func__, __LINE__);
		return;
	}

	// if we hit the end of the list, wrap it around
	if ( ! m_crx ) m_crx = m_activeList;

	// we use m_bookmark to determine when we've done a round over all
	// the collections. but it will be set to null sometimes when we
	// are in this loop because the active list gets recomputed. so
	// if we lost it because our bookmarked collection is no longer 
	// 'active' then just set it to the list head i guess
	if ( ! m_bookmark || ! m_bookmark->m_isActive )
		m_bookmark = m_activeList;

	// i guess return at the end of the linked list if no collection
	// launched a spider... otherwise do another cycle to launch another
	// spider. i could see a single collection dominating all the spider
	// slots in some scenarios with this approach unfortunately.
	if ( m_crx == m_bookmark && ! firstTime && m_launches == 0 )
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, end of list?", __FILE__, __func__, __LINE__);
		return;
	}

	// reset # launches after doing a round and having launched > 0
	if ( m_crx == m_bookmark && ! firstTime )
		m_launches = 0;

	firstTime = false;

	// if a collection got deleted re-calc the active list so
	// we don't core trying to access a delete collectionrec.
	// i'm not sure if this can happen here but i put this in as a 
	// precaution.
	if ( ! m_activeListValid ) { 
		m_crx = NULL; 
		goto collLoop; 
	}

	// return now if list is just empty
	if ( ! m_activeList ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, active list empty", __FILE__, __func__, __LINE__);
		return;
	}

	cr = m_crx;

	// advance for next time we call goto subloop;
	m_crx = m_crx->m_nextActive;


	// get the spider collection for this collnum
	m_sc = g_spiderCache.getSpiderColl(cr->m_collnum);//m_cri);
	// skip if none
	if ( ! m_sc ) {
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, no spider cache for this collection", __FILE__, __func__, __LINE__);
		goto subloop;
	}
		
	// always reset priority to max at start
	m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );

 subloopNextPriority:

	QUICKPOLL(MAX_NICENESS);

		// wrap it if we should
		//if ( m_cri >= g_collectiondb.m_numRecs ) m_cri = 0;
		// get rec
		//cr = g_collectiondb.m_recs[m_cri];
		// skip if gone
        if ( ! cr ) goto subloop;

		// stop if not enabled
		if ( ! cr->m_spideringEnabled ) goto subloop;

		// hit crawl round max?
		if ( cr->m_maxCrawlRounds > 0 &&
		     cr->m_isCustomCrawl &&
		     cr->m_spiderRoundNum >= cr->m_maxCrawlRounds ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
				cr->localCrawlInfoUpdate();
			// mark it has false
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXROUNDS ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXROUNDS;
			}
			
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, crawl round max reached", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		// hit pages to crawl max?
		if ( cr->m_maxToCrawl > 0 &&
		     cr->m_isCustomCrawl == 1 && // bulk jobs exempt!
		     cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound >=
		     cr->m_maxToCrawl ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
			{
				cr->localCrawlInfoUpdate();
			}
				
			// now once all hosts have no urls ready to spider
			// then the send email code will be called.
			// do it this way for code simplicity.
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXTOCRAWL ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXTOCRAWL;
			}
			
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, max pages to crawl reached", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		// hit pages to process max?
		if ( cr->m_maxToProcess > 0 &&
		     cr->m_isCustomCrawl == 1 && // bulk jobs exempt!
		     cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound >=
		     cr->m_maxToProcess ) {
			// should we resend our local crawl info to all hosts?
			if ( cr->m_localCrawlInfo.m_hasUrlsReadyToSpider )
				cr->localCrawlInfoUpdate();
			// now we can update it
			cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = false;
			// prevent having to save all the time
			if ( cr->m_spiderStatus != SP_MAXTOPROCESS ) {
				cr->m_needsSave = true;
				cr->m_spiderStatus = SP_MAXTOPROCESS;
			}
			
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, max pages to process reached", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		// shortcut
		CrawlInfo *ci = &cr->m_localCrawlInfo;

		// . if nothing left to spider...
		// . this is what makes us fast again! but the problem
		//   is is that if they change the url filters so that 
		//   something becomes ready to spider again we won't know
		//   because we do not set this flag back to true in
		//   Parms.cpp "doRebuild" because we don't want to get
		//   another email alert if there is nothing ready to spider
		//   after they change the parms. perhaps we should set
		//   the # of urls spidered to the "sentEmail" flag so we
		//   know if that changes to send another...
		// . bug in this...
		//if ( cr->m_isCustomCrawl && ! ci->m_hasUrlsReadyToSpider ) 
		//	continue;

		// get the spider collection for this collnum
		//m_sc = g_spiderCache.getSpiderColl(cr->m_collnum);//m_cri);
		// skip if none
		//if ( ! m_sc ) goto subloop;
		// skip if we completed the doledb scan for every spider
		// priority in this collection
		// MDW: this was the only thing based on the value of
		// m_didRound, but it was preventing addSpiderReply() from
		// calling g_spiderLoop.spiderDoledUrls( ) to launch another
		// spider. but now since we use m_crx logic and not
		// m_didRound logic to spread spiders equally over all 
		// collections, let's try without this
		//if ( m_sc->m_didRound ) goto subloop;

		// set current time, synced with host #0
		nowGlobal = (uint32_t)getTimeGlobal();

		// the last time we attempted to spider a url for this coll
		//m_sc->m_lastSpiderAttempt = nowGlobal;
		// now we save this so when we restart these two times
		// are from where we left off so we do not end up setting
		// hasUrlsReadyToSpider to true which in turn sets
		// the sentEmailAlert flag to false, which makes us
		// send ANOTHER email alert!!
		ci->m_lastSpiderAttempt = nowGlobal;

		// sometimes our read of spiderdb to populate the waiting
		// tree using evalIpLoop() takes a LONG time because
		// a niceness 0 thread is taking a LONG time! so do not
		// set hasUrlsReadyToSpider to false because of that!!
		if ( m_sc->m_gettingList1 )
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		// update this for the first time in case it is never updated.
		// then after 60 seconds we assume the crawl is done and
		// we send out notifications. see below.
		if ( ci->m_lastSpiderCouldLaunch == 0 )
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		//
		// . if doing respider with roundstarttime....
		// . roundstarttime is > 0 if m_collectiveRespiderFrequency
		//   is > 0, unless it has not been set to current time yet
		// . if m_collectiveRespiderFrequency was set to 0.0 then
		//   PageCrawlBot.cpp also sets m_roundStartTime to 0.
		//
		if ( nowGlobal < cr->m_spiderRoundStartTime ) 
		{
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, Spider start time not reached", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		// if populating this collection's waitingtree assume
		// we would have found something to launch as well. it might
		// mean the waitingtree-saved.dat file was deleted from disk
		// so we need to rebuild it at startup.
		if ( m_sc->m_waitingTreeNeedsRebuild ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;

		// get max spiders
		int32_t maxSpiders = cr->m_maxNumSpiders;
		if ( m_sc->m_isTestColl ) {
			// parser does one at a time for consistency
			if ( g_conf.m_testParserEnabled ) maxSpiders = 1;
			// need to make it 6 since some priorities essentially
			// lock the ips up that have urls in higher 
			// priorities. i.e. once we dole a url out for ip X, 
			// then if later we add a high priority url for IP X it
			// can't get spidered until the one that is doled does.
			if ( g_conf.m_testSpiderEnabled ) maxSpiders = 6;
		}

		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: maxSpiders: %"INT32"", __FILE__, __func__, __LINE__, maxSpiders);
	
		// if some spiders are currently outstanding
		if ( m_sc->m_spidersOut )
		{
			// do not end the crawl until empty of urls because
			// that url might end up adding more links to spider
			// when it finally completes
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		}

		// debug log
		//if ( g_conf.m_logDebugSpider )
		//	log("spider: has %"INT32" spiders out",m_sc->m_spidersOut);
		// obey max spiders per collection too
		if ( m_sc->m_spidersOut >= maxSpiders )
		{
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, Too many spiders active for collection", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		// shortcut
		SpiderColl *sc = cr->m_spiderColl;

		if ( sc && sc->m_doleIpTable.isEmpty() )
		{
			if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, doleIpTable is empty", __FILE__, __func__, __LINE__);
			goto subloop;
		}

		/*
		// . HACK. 
		// . TODO: we set spidercoll->m_gotDoledbRec to false above,
		//   then make Rdb.cpp set spidercoll->m_gotDoledbRec to
		//   true if it receives a rec. then if we see that it
		//   got set to true do not update m_nextKeys[i] or
		//   m_isDoledbEmpty[i] etc. because we might have missed
		//   the new doledb rec coming in.
		// . reset our doledb empty timer every 3 minutes and also
		// . reset our doledb empty status
		int32_t wait = SPIDER_DONE_TIMER - 10;
		if ( wait < 10 ) { char *xx=NULL;*xx=0; }
		if ( sc && nowGlobal - sc->m_lastEmptyCheck >= wait ) {
			// assume doledb not empty
			sc->m_allDoledbPrioritiesEmpty = 0;
			// reset the timer
			sc->m_lastEmptyCheck = nowGlobal;
			// reset all empty flags for each priority
			for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
				sc->m_nextKeys[i] = g_doledb.makeFirstKey2(i);
				sc->m_isDoledbEmpty[i] = 0;
			}
		}

		// . if all doledb priorities are empty, skip it quickly
		// . do this only after we update lastSpiderAttempt above
		// . this is broken!! why??
		if ( sc && sc->m_allDoledbPrioritiesEmpty >= 3 )
			continue;
		*/
		// ok, we are good to launch a spider for coll m_cri
		//break;
		//}
	
	// if none, bail, wait for sleep wrapper to re-call us later on
	//if ( count == 0 ) return;

	// sanity check
	if ( nowGlobal == 0 ) { char *xx=NULL;*xx=0; }

	// sanity check
	//if ( m_cri >= g_collectiondb.m_numRecs ) { char *xx=NULL;*xx=0; }

	// grab this
	//collnum_t collnum = m_cri;
	//CollectionRec *cr = g_collectiondb.m_recs[collnum];

	// update the crawlinfo for this collection if it has been a while.
	// should never block since callback is NULL.
	//if ( ! updateCrawlInfo(cr,NULL,NULL,true) ) { char *xx=NULL;*xx=0; }

	// get this
	//char *coll = cr->m_coll;

	// need this for msg5 call
	key_t endKey; endKey.setMax();

	// start at the top each time now
	//m_sc->m_nextDoledbKey.setMin();

	// set the key to this at start (break Spider.cpp:4683
	//m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri );

	// init the m_priorityToUfn map array?
	if ( ! m_sc->m_ufnMapValid ) {
		// reset all priorities to map to a ufn of -1
		for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ )
			m_sc->m_priorityToUfn[i] = -1;
		// initialize the map that maps priority to first ufn that uses
		// that priority. map to -1 if no ufn uses it.
		for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
			// breathe
			QUICKPOLL ( MAX_NICENESS );
			// get the ith rule priority
			int32_t sp = cr->m_spiderPriorities[i];
			// must not be filtered or banned
			if ( sp < 0 ) continue;
			// sanity
			if ( sp >= MAX_SPIDER_PRIORITIES){char *xx=NULL;*xx=0;}
			// skip if already mapped
			if ( m_sc->m_priorityToUfn[sp] != -1 ) continue;
			// map that
			m_sc->m_priorityToUfn[sp] = i;
		}
		// all done
		m_sc->m_ufnMapValid = true;
	}

 loop:

	QUICKPOLL(MAX_NICENESS);

	// shortcut
	//CrawlInfo *ci = &cr->m_localCrawlInfo;
	ci = &cr->m_localCrawlInfo;

	// bail if waiting for lock reply, no point in reading more
	if ( m_msg12.m_gettingLocks ) {
		// assume we would have launched a spider for this coll
		ci->m_lastSpiderCouldLaunch = nowGlobal;
		// wait for sleep callback to re-call us in 10ms
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, still waiting for lock reply", __FILE__, __func__, __LINE__);
		return;
	}

	// reset priority when it goes bogus
	if ( m_sc->m_pri2 < 0 ) {
		// i guess the scan is complete for this guy
		//m_sc->m_didRound = true;
		// count # of priority scan rounds done
		//m_sc->m_numRoundsDone++;
		// reset for next coll
		m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );
		//m_sc->m_pri2 = MAX_SPIDER_PRIORITIES - 1;
		// reset key now too since this coll was exhausted
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		// we can't keep starting over because there are often tons
		// of annihilations between positive and negative keys
		// and causes massive disk slow down because we have to do
		// like 300 re-reads or more of about 2k each on coeus
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [ m_sc->m_pri2 ];
		// and this
		//m_sc->m_msg5StartKey = m_sc->m_nextDoledbKey;
		// was it all empty? if did not encounter ANY doledb recs
		// after scanning all priorities, set empty to true.
		//if ( ! m_sc->m_encounteredDoledbRecs &&
		//     // if waiting tree is rebuilding... could be empty...
		//     ! m_sc->m_waitingTreeNeedsRebuild )
		//	m_sc->m_lastDoledbReadEmpty = true;
		// and go up top
		//goto collLoop;
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, pri2 < 0", __FILE__, __func__, __LINE__);
		goto subloop;
	}

	// . skip priority if we knows its empty in doledb
	// . this will save us a call to msg5 below
	//if ( m_sc->m_isDoledbEmpty [ m_sc->m_pri2 ] ) {
	//	// decrease the priority
	//	m_sc->devancePriority();
	//	// and try the one below
	//	goto loop;
	//}

	// shortcut
	//CollectionRec *cr = m_sc->m_cr;
	// sanity
	if ( cr != m_sc->getCollectionRec() ) { char *xx=NULL;*xx=0; }
	// skip the priority if we already have enough spiders on it
	int32_t out = m_sc->m_outstandingSpiders[m_sc->m_pri2];
	// how many spiders can we have out?
	int32_t max = 0;
	for ( int32_t i =0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != m_sc->m_pri2 ) continue;
		//if ( ! cr->m_spidersEnabled[i] ) continue;
		if ( cr->m_maxSpidersPerRule[i] > max )
			max = cr->m_maxSpidersPerRule[i];
	}
	// get the max # of spiders over all ufns that use this priority!
	//int32_t max = getMaxAllowableSpidersOut ( m_sc->m_pri2 );
	//int32_t ufn = m_sc->m_priorityToUfn[m_sc->m_pri2];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	//int32_t max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	//if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	//if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;

	// if we have one out, do not end the round!
	if ( out > 0 ) {
		// assume we could have launched a spider
		ci->m_lastSpiderCouldLaunch = nowGlobal;
	}

	// always allow at least 1, they can disable spidering otherwise
	// no, we use this to disabled spiders... if ( max <= 0 ) max = 1;
	// skip?
	if ( out >= max ) {
		// count as non-empty then!
		//m_sc->m_encounteredDoledbRecs = true;
		// try the priority below us
		m_sc->devancePriority();
		//m_sc->m_pri--;
		// set the new key for this priority if valid
		//if ( m_sc->m_pri >= 0 )
		//	//m_sc->m_nextDoledbKey = 
		//	//	g_doledb.makeFirstKey2(m_sc->m_pri);
		//	m_sc->m_nextDoledbKey = m_sc->m_nextKeys[m_sc->m_pri2];
		// and try again
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, trying previous priority", __FILE__, __func__, __LINE__);
		goto loop;
	}

	// we only launch one spider at a time... so lock it up
	m_gettingDoledbList = true;

	// log this now
	if ( g_conf.m_logDebugSpider )
		m_doleStart = gettimeofdayInMillisecondsLocal();

	// debug
	if ( g_conf.m_logDebugSpider &&
	     m_sc->m_msg5StartKey != m_sc->m_nextDoledbKey )
		log("spider: msg5startKey differs from nextdoledbkey");

	// seems like we need this reset here... strange
	m_list.reset();

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Getting list (msg5)", __FILE__, __func__, __LINE__);
	// get a spider rec for us to spider from doledb (mdw)
	if ( ! m_msg5.getList ( RDB_DOLEDB      ,
				cr->m_collnum, // coll            ,
				&m_list         ,
				m_sc->m_msg5StartKey,//m_sc->m_nextDoledbKey,
				endKey          ,
				// need to make this big because we don't
				// want to end up getting just a negative key
				//1             , // minRecSizes (~ 7000)
				// we need to read in a lot because we call
				// "goto listLoop" below if the url we want
				// to dole is locked.
				// seems like a ton of negative recs
				// MDW: let's now read in 50k, not 2k,of doledb
				// spiderrequests because often the first one
				// has an ip already in use and then we'd
				// just give up on the whole PRIORITY! which
				// really freezes the spiders up.
				// Also, if a spider request is corrupt in
				// doledb it would cork us up too!
				50000            , // minRecSizes
				true            , // includeTree
				false           , // addToCache
				0               , // max cache age
				0               , // startFileNum
				-1              , // numFiles (all)
				this            , // state 
				gotDoledbListWrapper2 ,
				MAX_NICENESS    , // niceness
				true            ))// do error correction?
	{
		// return if it blocked
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, getList blocked", __FILE__, __func__, __LINE__);
		return;
	}
	
	// debug
	//log(LOG_DEBUG,"spider: read list of %"INT32" bytes from spiderdb for "
	//    "pri=%"INT32"+",m_list.m_listSize,(int32_t)m_sc->m_pri);
	// breathe
	QUICKPOLL ( MAX_NICENESS );

	int32_t saved = m_launches;

	// . add urls in list to cache
	// . returns true if we should read another list
	// . will set startKey to next key to start at
	bool status = gotDoledbList2 ( );
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Back from gotDoledList2. Get more? %s", __FILE__, __func__, __LINE__, status?"true":"false");

	// if we did not launch anything, then decrement priority and
	// try again. but if priority hits -1 then subloop2 will just go to 
	// the next collection.
	if ( saved == m_launches ) {
		m_sc->devancePriority();
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Loop, get next priority", __FILE__, __func__, __LINE__);
		goto subloopNextPriority;
	}


	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, loop", __FILE__, __func__, __LINE__);
	if ( status ) {
		// . if priority is -1 that means try next priority
		// . DO NOT reset the whole scan. that was what was happening
		//   when we just had "goto loop;" here
		// . this means a reset above!!!
		//if ( m_sc->m_pri2 == -1 ) return;
		// bail if waiting for lock reply, no point in reading more
		// mdw- i moved this check up to loop: jump point.
		//if ( m_msg12.m_gettingLocks ) return;
		// gotDoledbList2() always advances m_nextDoledbKey so
		// try another read
		// now advance to next coll, launch one spider per coll
		goto subloop;//loop;
	}
	// wait for the msg12 get lock request to return... 
	// or maybe spiders are off
	//return;
	// now advance to next coll, launch one spider per coll
	goto subloop;//loop;
}



// spider the spider rec in this list from doledb
// returns false if would block indexing a doc, returns true if would not,
// and returns true and sets g_errno on error
bool SpiderLoop::gotDoledbList2 ( ) {
	// unlock
	m_gettingDoledbList = false;

	// shortcuts
	CollectionRec *cr = m_sc->getCollectionRec();
	CrawlInfo *ci = &cr->m_localCrawlInfo;

	// update m_msg5StartKey for next read
	if ( m_list.getListSize() > 0 ) {
		// what is m_list.m_ks ?
		m_list.getLastKey((char *)&m_sc->m_msg5StartKey);
		m_sc->m_msg5StartKey += 1;
		// i guess we had something? wait for nothing to be there
		//m_sc->m_encounteredDoledbRecs = true;
	}

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		int64_t now = gettimeofdayInMillisecondsLocal();
		int64_t took = now - m_doleStart;
		if ( took > 2 )
			logf(LOG_DEBUG,"spider: GOT list from doledb in "
			     "%"INT64"ms "
			     "size=%"INT32" bytes",
			     took,m_list.getListSize());
	}

	bool bail = false;
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) bail = true;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) bail = true;
	// skip if too many udp slots being used
	if(g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) bail =true;
	// stop if too many out
	if ( m_numSpidersOut >= MAX_SPIDERS ) bail = true;

	if ( bail ) {
		// assume we could have launched a spider
		ci->m_lastSpiderCouldLaunch = getTimeGlobal();
		// return false to indicate to try another
		return false;
	}


	// bail if list is empty
	if ( m_list.getListSize() <= 0 ) {
		// don't bother with this priority again until a key is
		// added to it! addToDoleIpTable() will be called
		// when that happens and it might unset this then.
		m_sc->m_isDoledbEmpty [ m_sc->m_pri2 ] = 1;

		/*
		// if all priorities now empty set another flag
		m_sc->m_allDoledbPrioritiesEmpty++;
		for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
			if ( m_sc->m_isDoledbEmpty[m_sc->m_pri2] ) continue;
			// must get empties 3 times in a row to ignore it
			// in case something was added to doledb while
			// we were reading from doledb.
			m_sc->m_allDoledbPrioritiesEmpty--;
			break;
		}
		*/
	
		// if no spiders...
		//if ( g_conf.m_logDebugSpider ) {
		//	log("spider: empty doledblist collnum=%"INT32" "
		//	    "inwaitingtree=%"INT32"",
		//	    (int32_t)cr->m_collnum,
		//	    m_sc->m_waitingTree.m_numUsedNodes);
		//}
		//if ( g_conf.m_logDebugSpider )
		//	log("spider: resetting doledb priority pri=%"INT32"",
		//	    m_sc->m_pri);
		// trigger a reset
		//m_sc->m_pri = -1;
		// . let the sleep timer init the loop again!
		// . no, just continue the loop
		//return true;
		// . this priority is EMPTY, try next
		// . will also set m_sc->m_nextDoledbKey
		// . will also set m_sc->m_msg5StartKey
		//m_sc->devancePriority();
		// this priority is EMPTY, try next
		//m_sc->m_pri = m_sc->m_pri - 1;
		// how can this happen?
		//if ( m_sc->m_pri < -1 ) m_sc->m_pri = -1;
		// all done if priority is negative, it will start over
		// at the top most priority, we've completed a round
		//if ( m_sc->m_pri < 0 ) return true;
		// set to next priority otherwise
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [m_sc->m_pri];
		// and load that list from doledb for that priority
		return true;
	}

	// if debugging the spider flow show the start key if list non-empty
	/*if ( g_conf.m_logDebugSpider ) {
		// 12 byte doledb keys
		int32_t pri = g_doledb.getPriority(&m_sc->m_nextDoledbKey);
		int32_t stm = g_doledb.getSpiderTime(&m_sc->m_nextDoledbKey);
		int64_t uh48 = g_doledb.getUrlHash48(&m_sc->m_nextDoledbKey);
		logf(LOG_DEBUG,"spider: loading list from doledb startkey=%s"
		     " pri=%"INT32" time=%"UINT32" uh48=%"UINT64"",
		     KEYSTR(&m_sc->m_nextDoledbKey,12),
		     pri,
		     stm,
		     uh48);
		     }*/
	

	time_t nowGlobal = getTimeGlobal();

	// double check
	//if ( ! m_list.checkList_r( true , false, RDB_DOLEDB) ) { 
	//	char *xx=NULL;*xx=0; }

	// debug parm
	//int32_t lastpri = -2;
	//int32_t lockCount = 0;

	// reset ptr to point to first rec in list
	m_list.resetListPtr();

 listLoop:

	// all done if empty
	//if ( m_list.isExhausted() ) {
	//	// copied from above
	//	m_sc->m_didRound = true;
	//	// and try next colleciton immediately
	//	return true;
	//}

	// breathe
	QUICKPOLL(MAX_NICENESS);
	// get the current rec from list ptr
	char *rec = (char *)m_list.getListPtr();
	// the doledbkey
	key_t *doledbKey = (key_t *)rec;

	// get record after it next time
	m_sc->m_nextDoledbKey = *doledbKey ;

	// sanity check -- wrap watch -- how can this really happen?
	if ( m_sc->m_nextDoledbKey.n1 == 0xffffffff           &&
	     m_sc->m_nextDoledbKey.n0 == 0xffffffffffffffffLL ) {
		char *xx=NULL;*xx=0; }

	// only inc it if its positive! because we do have negative
	// doledb keys in here now
	//if ( (m_sc->m_nextDoledbKey & 0x01) == 0x01 )
	//	m_sc->m_nextDoledbKey += 1;
	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	//else
	//	m_sc->m_nextDoledbKey += 2;

	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	if ( (m_sc->m_nextDoledbKey & 0x01) == 0x00 )
		m_sc->m_nextDoledbKey += 2;

	// did it hit zero? that means it wrapped around!
	if ( m_sc->m_nextDoledbKey.n1 == 0x0 &&
	     m_sc->m_nextDoledbKey.n0 == 0x0 ) {
		// TODO: work this out
		char *xx=NULL;*xx=0; }

	// get priority from doledb key
	int32_t pri = g_doledb.getPriority ( doledbKey );

	// if the key went out of its priority because its priority had no
	// spider requests then it will bleed over into another priority so
	// in that case reset it to the top of its priority for next time
	int32_t pri3 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
	if ( pri3 != m_sc->m_pri2 ) {
		m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri2);
		// the key must match the priority queue its in as nextKey
		//if ( pri3 != m_sc->m_pri2 ) { char *xx=NULL;*xx=0; }
	}

	if ( g_conf.m_logDebugSpider ) {
		int32_t pri4 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
		log("spider: setting pri2=%"INT32" queue doledb nextkey to "
		    "%s (pri=%"INT32")",
		    m_sc->m_pri2,KEYSTR(&m_sc->m_nextDoledbKey,12),pri4);
		//if ( pri4 != m_sc->m_pri2 ) { char *xx=NULL;*xx=0; }
	}

	// update next doledbkey for this priority to avoid having to
	// process excessive positive/negative key annihilations (mdw)
	m_sc->m_nextKeys [ m_sc->m_pri2 ] = m_sc->m_nextDoledbKey;

	// sanity
	if ( pri < 0 || pri >= MAX_SPIDER_PRIORITIES ) { char *xx=NULL;*xx=0; }
	// skip the priority if we already have enough spiders on it
	int32_t out = m_sc->m_outstandingSpiders[pri];
	// get the first ufn that uses this priority
	//int32_t max = getMaxAllowableSpidersOut ( pri );
	// how many spiders can we have out?
	int32_t max = 0;
	// in milliseconds. ho wint32_t to wait between downloads from same IP.
	// only for parnent urls, not including child docs like robots.txt
	// iframe contents, etc.
	int32_t sameIpWaitTime = 5000; // 250; // ms
	int32_t maxSpidersOutPerIp = 1;
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != pri ) continue;
		//if ( ! cr->m_spidersEnabled[i] ) continue;
		if ( cr->m_maxSpidersPerRule[i] > max )
			max = cr->m_maxSpidersPerRule[i];
		if ( cr->m_spiderIpWaits[i] < sameIpWaitTime )
			sameIpWaitTime = cr->m_spiderIpWaits[i];
		if ( cr->m_spiderIpMaxSpiders[i] > maxSpidersOutPerIp )
			maxSpidersOutPerIp = cr->m_spiderIpMaxSpiders[i];
	}

	//int32_t ufn = m_sc->m_priorityToUfn[pri];
	// how many can we have? crap, this is based on ufn, not priority
	// so we need to map the priority to a ufn that uses that priority
	//int32_t max = 0;
	// see if it has a maxSpiders, if no ufn uses this priority then
	// "max" will remain set to 0
	//if ( ufn >= 0 ) max = m_sc->m_cr->m_maxSpidersPerRule[ufn];
	// turned off?
	//if ( ufn >= 0 && ! m_sc->m_cr->m_spidersEnabled[ufn] ) max = 0;
	// if we skipped over the priority we wanted, update that
	//m_pri = pri;
	// then do the next one after that for next round
	//m_pri--;
	// always allow at least 1, they can disable spidering otherwise
	//if ( max <= 0 ) max = 1;
	// skip? and re-get another doledb list from next priority...
	if ( out >= max ) {
		// come here if we hit our ip limit too
		//	hitMax:
		// assume we could have launched a spider
		if ( max > 0 ) ci->m_lastSpiderCouldLaunch = nowGlobal;
		// this priority is maxed out, try next
		//m_sc->devancePriority();
		// assume not an empty read
		//m_sc->m_encounteredDoledbRecs = true;
		//m_sc->m_pri = pri - 1;
		// all done if priority is negative
		//if ( m_sc->m_pri < 0 ) return true;
		// set to next priority otherwise
		//m_sc->m_nextDoledbKey=g_doledb.makeFirstKey2 ( m_sc->m_pri );
		//m_sc->m_nextDoledbKey = m_sc->m_nextKeys [m_sc->m_pri];
		// and load that list
		return true;
	}

	// no negatives - wtf?
	// if only the tree has doledb recs, Msg5.cpp does not remove
	// the negative recs... it doesn't bother to merge.
	if ( (doledbKey->n0 & 0x01) == 0 ) { 
		// just increment then i guess
		m_list.skipCurrentRecord();
		// if exhausted -- try another load with m_nextKey set
		if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		goto listLoop;
	}

	// what is this? a dataless positive key?
	if ( m_list.getCurrentRecSize() <= 16 ) { char *xx=NULL;*xx=0; }

	int32_t ipOut = 0;
	int32_t globalOut = 0;

	// get the "spider rec" (SpiderRequest) (embedded in the doledb rec)
	SpiderRequest *sreq = (SpiderRequest *)(rec + sizeof(key_t)+4);
	// sanity check. check for http(s)://
	if ( sreq->m_url[0] != 'h' &&
	     // might be a docid from a pagereindex.cpp
	     ! is_digit(sreq->m_url[0]) ) { 
		// note it
		if ( (g_corruptCount % 1000) == 0 )
			log("spider: got corrupt doledb record. ignoring. "
			    "pls fix!!!");
		g_corruptCount++;
		goto skipDoledbRec;
		// skip for now....!! what is causing this???
		//m_list.skipCurrentRecord();
		// if exhausted -- try another load with m_nextKey set
		//if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		//goto listLoop;
	}		


	// . how many spiders out for this ip now?
	// . TODO: count locks in case twin is spidering... but it did not seem
	//   to work right for some reason
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		if ( ! xd ) continue;
		if ( ! xd->m_sreqValid ) continue;
		// to prevent one collection from hogging all the urls for
		// particular IP and starving other collections, let's make 
		// this a per collection count.
		// then allow msg13.cpp to handle the throttling on its end.
		// also do a global count over all collections now
		if ( xd->m_sreq.m_firstIp == sreq->m_firstIp ) globalOut++;
		// only count for our same collection otherwise another
		// collection can starve us out
		if ( xd->m_collnum != cr->m_collnum ) continue;
		if ( xd->m_sreq.m_firstIp == sreq->m_firstIp ) ipOut++;
	}
	// don't give up on this priority, just try next in the list.
	// we now read 50k instead of 2k from doledb in order to fix
	// one ip from bottle corking the whole priority!!
	if ( ipOut >= maxSpidersOutPerIp ) {
		// assume we could have launched a spider
		if ( maxSpidersOutPerIp > 0 ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		//goto hitMax;
	skipDoledbRec:
		// skip
		m_list.skipCurrentRecord();
		// if not exhausted try the next doledb rec in this list
		if ( ! m_list.isExhausted() ) goto listLoop;
		// print a log msg if we corked things up even
		// though we read 50k from doledb
		static bool s_flag = true;
		if ( m_list.m_listSize > 50000 && s_flag ) {
			s_flag = true;
			log("spider: 50k not big enough");
		}
		// list is exhausted...
		return true;
	}

	// but if the global is high, only allow one out per coll so at 
	// least we dont starve and at least we don't make a huge wait in
	// line of queued results just sitting there taking up mem and
	// spider slots so the crawlbot hourly can't pass.
	if ( globalOut >= maxSpidersOutPerIp && ipOut >= 1 ) {
		// assume we could have launched a spider
		if ( maxSpidersOutPerIp > 0 ) 
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		goto skipDoledbRec;
	}

	if ( g_conf.m_logDebugSpider )
		log("spider: %"INT32" spiders out for %s for %s",
		    ipOut,iptoa(sreq->m_firstIp),
		    sreq->m_url);

	// sometimes we have it locked, but is still in doledb i guess.
	// seems like we might have give the lock to someone else and
	// there confirmation has not come through yet, so it's still
	// in doledb.
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// shortcut
	int64_t lockKey = makeLockTableKey ( sreq );
	// get the lock... only avoid if confirmed!
	int32_t slot = ht->getSlot ( &lockKey );
	UrlLock *lock = NULL;
	if ( slot >= 0 ) 
		// get the corresponding lock then if there
		lock = (UrlLock *)ht->getValueFromSlot ( slot );
	// if there and confirmed, why still in doledb?
	if ( lock && lock->m_confirmed ) {
		// fight log spam
		static int32_t s_lastTime = 0;
		if ( nowGlobal - s_lastTime >= 2 ) {
			// why is it not getting unlocked!?!?!
			log("spider: spider request locked but still in "
			    "doledb. uh48=%"INT64" firstip=%s %s",
			    sreq->getUrlHash48(), 
			    iptoa(sreq->m_firstIp),sreq->m_url );
			s_lastTime = nowGlobal;
		}
		// just increment then i guess
		m_list.skipCurrentRecord();
		// let's return false here to avoid an infinite loop
		// since we are not advancing nextkey and m_pri is not
		// being changed, that is what happens!
		if ( m_list.isExhausted() ) {
			// crap. but then we never make it to lower priorities.
			// since we are returning false. so let's try the
			// next priority in line.
			//m_sc->m_pri--;
			//m_sc->devancePriority();
			// try returning true now that we skipped to
			// the next priority level to avoid the infinite
			// loop as described above.
			return true;
			//return false;//true;
		}
		// try the next record in this list
		goto listLoop;
	}

	// . no no! the SpiderRequests in doledb are in our group because
	//   doledb is split based on ... firstIp i guess...
	//   BUT now lock is done based on probable docid since we do not
	//   know the firstIp if injected spider requests but we do know
	//   their probable docids since that is basically a function of
	//   the url itself. THUS we now must realize this by trying to
	//   get the lock for it and failing!
	/*
	// . likewise, if this request is already being spidered, if it
	//   is in the lock table, skip it...
	// . if this is currently locked for spidering by us or another
	//   host (or by us) then return true here
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// shortcut
	//int64_t uh48 = sreq->getUrlHash48();
	// get the lock key
	uint64_t lockKey ;
	lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);
	// check tree
	int32_t slot = ht->getSlot ( &lockKey );
	// if more than an hour old nuke it and clear it
	if ( slot >= 0 ) {
		// get the corresponding lock then if there
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot ( slot );
		// if 1hr+ old, nuke it and disregard
		if ( nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
			// unlock it
			ht->removeSlot ( slot );
			// it is gone
			slot = -1;
		}
	}
	// if there say no no -- will try next spiderrequest in doledb then
	if ( slot >= 0 ) {
		// just increment then i guess
		m_list.skipCurrentRecord();
		// count locks
		//if ( pri == lastpri ) lockCount++;
		//else                  lockCount = 1;
		//lastpri = pri;
		// how is it we can have 2 locked but only 1 outstanding
		// for the same priority?
		// basically this url is done being spidered, but we have
		// not yet processed the negative doledb key in Rdb.cpp which
		// will remove the lock from the lock table... so this 
		// situation is perfectly fine i guess.. assuming that is
		// what is going on
		//if ( lockCount >= max ) { char *xx=NULL;*xx=0; }
		// this is not good
		static bool s_flag = false;
		if ( ! s_flag ) {
			s_flag = true;
			log("spider: got url %s that is locked but in dole "
			    "table... skipping",sreq->m_url);
		}
		// if exhausted -- try another load with m_nextKey set
		if ( m_list.isExhausted() ) return true;
		// otherwise, try the next doledb rec in this list
		goto listLoop;
	}
	*/

	// force this set i guess... why isn't it set already? i guess when
	// we added the spider request to doledb it was not set at that time
	//sreq->m_doled = 1;

	//
	// sanity check. verify the spiderrequest also exists in our
	// spidercache. we no longer store doled out spider requests in our
	// cache!! they are separate now.
	//
	//if ( g_conf.m_logDebugSpider ) {
	//	// scan for it since we may have dup requests
	//	int64_t uh48   = sreq->getUrlHash48();
	//	int64_t pdocid = sreq->getParentDocId();
	//	// get any request from our urlhash table
	//	SpiderRequest *sreq2 = m_sc->getSpiderRequest2 (&uh48,pdocid);
	//	// must be there. i guess it could be missing if there is
	//	// corruption and we lost it in spiderdb but not in doledb...
	//	if ( ! sreq2 ) { char *xx=NULL;*xx=0; }
	//}

	// log this now
	if ( g_conf.m_logDebugSpider ) 
		logf(LOG_DEBUG,"spider: trying to spider url %s",sreq->m_url);

	/*
	if ( ufn >= 0 ) {
		int32_t siwt = m_sc->m_cr->m_spiderIpWaits[ufn];
		if ( siwt >= 0 ) sameIpWaitTime = siwt;
	}

	if ( ufn >= 0 ) {
		maxSpidersOutPerIp = m_sc->m_cr->m_spiderIpMaxSpiders[ufn];
		if ( maxSpidersOutPerIp < 0 ) maxSpidersOutPerIp = 999;
	}
	*/

	// assume we launch the spider below. really this timestamp indicates
	// the last time we COULD HAVE LAUNCHED *OR* did actually launch
	// a spider
	ci->m_lastSpiderCouldLaunch = nowGlobal;

	// set crawl done email sent flag so another email can be sent again
	// in case the user upped the maxToCrawl limit, for instance,
	// so that the crawl could continue.
	//ci->m_sentCrawlDoneAlert = 0;

	// if we thought we were done, note it if something comes back up
	if ( ! ci->m_hasUrlsReadyToSpider ) 
		log("spider: got a reviving url for coll %s (%"INT32") to crawl %s",
		    cr->m_coll,(int32_t)cr->m_collnum,sreq->m_url);

	// if changing status, resend our local crawl info to all hosts?
	if ( ! ci->m_hasUrlsReadyToSpider )
		cr->localCrawlInfoUpdate();

	// there are urls ready to spider
	ci->m_hasUrlsReadyToSpider = true;

	// newly created crawls usually have this set to false so set it
	// to true so getSpiderStatus() does not return that "the job
	// is completed and no repeat is scheduled"...
	if ( cr->m_spiderStatus == SP_INITIALIZING ) {
		// this is the GLOBAL crawl info, not the LOCAL, which
		// is what "ci" represents...
		// MDW: is this causing the bug?
		// the other have already reported that there are no urls
		// to spider, so they do not re-report. we already
		// had 'hasurlsreadytospider' set to true so we didn't get
		// the reviving log msg.
		cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;
		// set this right i guess...?
		ci->m_lastSpiderAttempt = nowGlobal;
	}

	// reset reason why crawl is not running, because we basically are now
	cr->m_spiderStatus = SP_INPROGRESS; // this is 7
	//cr->m_spiderStatusMsg = NULL;

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

	// assume not an empty read
	//m_sc->m_encounteredDoledbRecs = true;

	// shortcut
	//char *coll = m_sc->m_cr->m_coll;
	// sometimes the spider coll is reset/deleted while we are
	// trying to get the lock in spiderUrl9() so let's use collnum
	collnum_t collnum = m_sc->getCollectionRec()->m_collnum;

	// . spider that. we don't care wheter it blocks or not
	// . crap, it will need to block to get the locks!
	// . so at least wait for that!!!
	// . but if we end up launching the spider then this should NOT
	//   return false! only return false if we should hold up the doledb
	//   scan
	// . this returns true right away if it failed to get the lock...
	//   which means the url is already locked by someone else...
	// . it might also return true if we are already spidering the url
	bool status = spiderUrl9 ( sreq , doledbKey , collnum, sameIpWaitTime ,
				   maxSpidersOutPerIp ) ;

	// just increment then i guess
	m_list.skipCurrentRecord();

	// if it blocked, wait for it to return to resume the doledb list 
	// processing because the msg12 is out and we gotta wait for it to 
	// come back. when lock reply comes back it tries to spider the url
	// then it tries to call spiderDoledUrls() to keep the spider queue
	// spidering fully.
	if ( ! status ) return false;

	// if exhausted -- try another load with m_nextKey set
	if ( m_list.isExhausted() ) {
		// if no more in list, fix the next doledbkey,
		// m_sc->m_nextDoledbKey
		log ( LOG_DEBUG,"spider: list exhausted.");
		return true;
	}
	// otherwise, it might have been in the lock cache and quickly
	// rejected, or rejected for some other reason, so try the next 
	// doledb rec in this list
	goto listLoop;

	//
	// otherwise, it blocked, trying to get the lock across the network.
	// so reset the doledb scan assuming it will go through. if it does
	// NOT get the lock, then it will be in the lock cache for quick
	// "return true" from spiderUrl() above next time we try it.
	//

	// once we get a url from doledb to spider, reset our doledb scan.
	// that way if a new url gets added to doledb that is high priority
	// then we get it right away.  
	//
	// NO! because the lock request can block then fail!! and we end
	// up resetting and in an infinite loop!
	//
	//m_sc->m_pri = -1;

	//return false;
}



// . spider the next url that needs it the most
// . returns false if blocked on a spider launch, otherwise true.
// . returns false if your callback will be called
// . returns true and sets g_errno on error
bool SpiderLoop::spiderUrl9 ( SpiderRequest *sreq ,
			      key_t *doledbKey ,
			      //char *coll ,
			      collnum_t collnum ,
			      int32_t sameIpWaitTime ,
			      int32_t maxSpidersOutPerIp ) {
	// sanity check
	//if ( ! sreq->m_doled ) { char *xx=NULL;*xx=0; }
	// if waiting on a lock, wait
	if ( m_msg12.m_gettingLocks ) { char *xx=NULL;*xx=0; }
	// sanity
	if ( ! m_sc ) { char *xx=NULL;*xx=0; }

	// sanity check
	// core dump? just re-run gb and restart the parser test...
	if ( g_conf.m_testParserEnabled &&
	     ! sreq->m_isInjecting ) { 
		char *xx=NULL;*xx=0; }

	// wait until our clock is synced with host #0 before spidering since
	// we store time stamps in the domain and ip wait tables in 
	// SpiderCache.cpp. We don't want to freeze domain for a int32_t time
	// because we think we have to wait until tomorrow before we can
	// spider it.
	if ( ! isClockInSync() ) { 
		// let admin know why we are not spidering
		static char s_printed = false;
		if ( ! s_printed ) {
			logf(LOG_DEBUG,"spider: NOT SPIDERING until clock "
			     "is in sync with host #0.");
			s_printed = true;
		}
		return true;
	}
	// turned off?
	if ( ( (! g_conf.m_spideringEnabled ||
		// or if trying to exit
		g_process.m_mode == EXIT_MODE
		) && // ! g_conf.m_webSpideringEnabled ) &&
	       ! sreq->m_isInjecting ) || 
	     // repairing the collection's rdbs?
	     g_repairMode ||
	     // power went off?
	     ! g_process.m_powerIsOn ) {
		// try to cancel outstanding spiders, ignore injects
		for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
			// get it
			XmlDoc *xd = m_docs[i];
			if ( ! xd                      ) continue;
			//if ( xd->m_sreq.m_isInjecting ) continue;
			// let everyone know, TcpServer::cancel() uses this in
			// destroySocket()
			g_errno = ECANCELLED;
			// cancel the socket trans who has "xd" as its state. 
			// this will cause XmlDoc::gotDocWrapper() to be called
			// now, on this call stack with g_errno set to 
			// ECANCELLED. But if Msg16 was not in the middle of 
			// HttpServer::getDoc() then this will have no effect.
			g_httpServer.cancel ( xd );//, g_msg13RobotsWrapper );
			// cancel any Msg13 that xd might have been waiting for
			g_udpServer.cancel ( &xd->m_msg13 , 0x13 );
		}
		return true;
	}
	// do not launch any new spiders if in repair mode
	if ( g_repairMode ) { 
		g_conf.m_spideringEnabled = false; 
		//g_conf.m_injectionEnabled = false; 
		return true; 
	}
	// do not launch another spider if less than 25MB of memory available.
	// this causes us to dead lock when spiders use up all the mem, and
	// file merge operation can not get any, and spiders need to add to 
	// titledb but can not until the merge completes!!
	if ( g_conf.m_maxMem - g_mem.m_used < 25*1024*1024 ) {
		static int32_t s_lastTime = 0;
		static int32_t s_missed   = 0;
		s_missed++;
		int32_t now = getTime();
		// don't spam the log, bug let people know about it
		if ( now - s_lastTime > 10 ) {
			log("spider: Need 25MB of free mem to launch spider, "
			    "only have %"INT64". Failed to launch %"INT32" times so "
			    "far.", g_conf.m_maxMem - g_mem.m_used , s_missed );
			s_lastTime = now;
		}
	}

	// we store this in msg12 for making a fakedb key
	//collnum_t collnum = g_collectiondb.getCollnum ( coll );

	// shortcut
	int64_t lockKeyUh48 = makeLockTableKey ( sreq );

	//uint64_t lockKey ;
	//lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);
	//lockKey = g_titledb.getFirstProbableDocId(sreq->m_probDocId);

	// . now that we have to use msg12 to see if the thing is locked
	//   to avoid spidering it.. (see comment in above function)
	//   we often try to spider something we are already spidering. that
	//   is why we have an rdbcache, m_lockCache, to make these lock
	//   lookups quick, now that the locking group is usually different
	//   than our own!
	// . we have to check this now because removeAllLocks() below will
	//   remove a lock that one of our spiders might have. it is only
	//   sensitive to our hostid, not "spider id"
	// sometimes we exhaust the doledb and m_nextDoledbKey gets reset
	// to zero, we do a re-scan and get a doledbkey that is currently
	// being spidered or is waiting for its negative doledb key to
	// get into our doledb tree
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		if ( ! xd ) continue;

		// jenkins was coring spidering the same url in different
		// collections at the same time
		if ( ! xd->m_collnumValid ) continue;
		if ( xd->m_collnum != collnum ) continue;

		// . problem if it has our doledb key!
		// . this happens if we removed the lock above before the
		//   spider returned!! that's why you need to set
		//   MAX_LOCK_AGE to like an hour or so
		// . i've also seen this happen because we got stuck looking
		//   up like 80,000 places and it was taking more than an
		//   hour. it had only reach about 30,000 after an hour.
		//   so at this point just set the lock timeout to
		//   4 hours i guess.
		// . i am seeing this again and we are trying over and over
		//   again to spider the same url and hogging the cpu so

		//   we need to keep this sanity check in here for times
		//   like this
		if ( xd->m_doledbKey == *doledbKey ) { 
			// just note it for now
			log("spider: spidering same url %s twice. "
			    "different firstips?",
			    xd->m_firstUrl.getUrl());
			//char *xx=NULL;*xx=0; }
		}
		// keep chugging
		continue;
	}

	// reset g_errno
	g_errno = 0;
	// breathe
	QUICKPOLL(MAX_NICENESS);

	// sanity. ensure m_sreq doesn't change from under us i guess
	if ( m_msg12.m_gettingLocks ) { char *xx=NULL;*xx=0; }

	// get rid of this crap for now
	//g_spiderCache.meterBandwidth();

	// save these in case getLocks() blocks
	m_sreq      = sreq;
	m_doledbKey = doledbKey;
	//m_coll      = coll;
	m_collnum = collnum;



	// if we already have the lock then forget it. this can happen
	// if spidering was turned off then back on.
	// MDW: TODO: we can't do this anymore since we no longer have
	// the lockTable check above because we do not control our own
	// lock now necessarily. it often is in another group's lockTable.
	//if ( g_spiderLoop.m_lockTable.isInTable(&lockKey) ) {
	//	log("spider: already have lock for lockKey=%"UINT64"",lockKey);
	//	// proceed
	//	return spiderUrl2();
	//}
	
	// count it
	m_processed++;

	// now we just take it out of doledb instantly
	int32_t node = g_doledb.m_rdb.m_tree.deleteNode(m_collnum,
							(char *)m_doledbKey,
							true);

	if ( g_conf.m_logDebugSpider )
		log("spider: deleting doledb tree node %"INT32,node);

	// if url filters rebuilt then doledb gets reset and i've seen us hit
	// this node == -1 condition here... so maybe ignore it... just log
	// what happened? i think we did a quickpoll somewhere between here
	// and the call to spiderDoledUrls() and it the url filters changed
	// so it reset doledb's tree. so in that case we should bail on this
	// url.
	if ( node == -1 ) { 
		g_errno = EADMININTERFERENCE;
		log("spider: lost url about to spider from url filters "
		    "and doledb tree reset. %s",mstrerror(g_errno));
		return true;
	}


	// now remove from doleiptable since we removed from doledb
	m_sc->removeFromDoledbTable ( sreq->m_firstIp );

	// DO NOT add back to waiting tree if max spiders
	// out per ip was 1 OR there was a crawldelay. but better
	// yet, take care of that in the winReq code above.

	// . now add to waiting tree so we add another spiderdb
	//   record for this firstip to doledb
	// . true = callForScan
	// . do not add to waiting tree if we have enough outstanding
	//   spiders for this ip. we will add to waiting tree when
	//   we receive a SpiderReply in addSpiderReply()
	if ( //sc && //out < cq->m_maxSpidersOutPerIp &&
	     // this will just return true if we are not the 
	     // responsible host for this firstip
	     // DO NOT populate from this!!! say "false" here...
	     ! m_sc->addToWaitingTree ( 0 , sreq->m_firstIp, false ) &&
	     // must be an error...
	     g_errno ) {
		char *msg = "FAILED TO ADD TO WAITING TREE";
		log("spider: %s %s",msg,mstrerror(g_errno));
		//us->sendErrorReply ( udpSlot , g_errno );
		//return;
	}


	// . add it to lock table to avoid respider, removing from doledb
	//   is not enough because we re-add to doledb right away
	// . return true on error here
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	UrlLock tmp;
	tmp.m_hostId = g_hostdb.m_myHost->m_hostId;
	tmp.m_timestamp = 0;
	tmp.m_expires = 0;
	tmp.m_firstIp = m_sreq->m_firstIp;
	tmp.m_spiderOutstanding = 0;
	tmp.m_confirmed = 1;
	tmp.m_collnum = m_collnum;
	if ( g_conf.m_logDebugSpider )
		log("spider: adding lock uh48=%"INT64" lockkey=%"INT64"",
		    m_sreq->getUrlHash48(),lockKeyUh48);
	if ( ! ht->addKey ( &lockKeyUh48 , &tmp ) )
		return true;

	// now do it. this returns false if it would block, returns true if it
	// would not block. sets g_errno on error. it spiders m_sreq.
	return spiderUrl2 ( );
}



bool SpiderLoop::spiderUrl2 ( ) {

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
		
	// sanity check
	//if ( ! m_sreq->m_doled ) { char *xx=NULL;*xx=0; }

	// . find an available doc slot
	// . we can have up to MAX_SPIDERS spiders (300)
	int32_t i;
	for ( i=0 ; i<MAX_SPIDERS ; i++ ) if (! m_docs[i]) break;

	// breathe
	QUICKPOLL(MAX_NICENESS);

	// come back later if we're full
	if ( i >= MAX_SPIDERS ) {
		log(LOG_DEBUG,"build: Already have %"INT32" outstanding spiders.",
		    (int32_t)MAX_SPIDERS);
		char *xx = NULL; *xx = 0;
	}

	// breathe
	QUICKPOLL(MAX_NICENESS);

	XmlDoc *xd;
	// otherwise, make a new one if we have to
	try { xd = new (XmlDoc); }
	// bail on failure, sleep and try again
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("build: Could not allocate %"INT32" bytes to spider "
		    "the url %s. Will retry later.",
		    (int32_t)sizeof(XmlDoc),  m_sreq->m_url );
		    
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, new XmlDoc failed", __FILE__, __func__, __LINE__);
		return true;
	}
	// register it's mem usage with Mem.cpp class
	mnew ( xd , sizeof(XmlDoc) , "XmlDoc" );
	// add to the array
	m_docs [ i ] = xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	char *coll = "collnumwasinvalid";
	if ( cr ) coll = cr->m_coll;

	// . pass in a pbuf if this is the "qatest123" collection
	// . we will dump the SafeBuf output into a file in the
	//   test subdir for comparison with previous versions of gb
	//   in order to see what changed
	SafeBuf *pbuf = NULL;
	if ( !strcmp( coll,"qatest123") && g_conf.m_testParserEnabled ) 
		pbuf = &xd->m_sbuf;

	//
	// sanity checks
	//
	//int64_t uh48;
	//int64_t pdocid;
	//if ( g_conf.m_logDebugSpider ) {
	//	// scan for it since we may have dup requests
	//	uh48   = m_sreq->getUrlHash48();
	//	pdocid = m_sreq->getParentDocId();
	//	// get any request from our urlhash table
	//	SpiderRequest *sreq2 = m_sc->getSpiderRequest2 (&uh48,pdocid);
	//	// must be valid parent
	//	if ( ! sreq2 && pdocid == 0LL ) { char *xx=NULL;*xx=0; }
	//	// for now core on this
	//	if ( ! sreq2 ) {  char *xx=NULL;*xx=0; }
	//	// log it
	//	logf(LOG_DEBUG,"spider: spidering uh48=%"UINT64" pdocid=%"UINT64"",
	//	     uh48,pdocid);
	//}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: spidering firstip9=%s(%"UINT32") "
		     "uh48=%"UINT64" prntdocid=%"UINT64" k.n1=%"UINT64" k.n0=%"UINT64"",
		     iptoa(m_sreq->m_firstIp),
		     (uint32_t)m_sreq->m_firstIp,
		     m_sreq->getUrlHash48(),
		     m_sreq->getParentDocId() ,
		     m_sreq->m_key.n1,
		     m_sreq->m_key.n0);


	// this returns false and sets g_errno on error
	if ( ! xd->set4 ( m_sreq       ,
			  m_doledbKey  ,
			  coll       ,
			  pbuf         ,
			  MAX_NICENESS ) ) {
		// i guess m_coll is no longer valid?
		mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
		delete (m_docs[i]);
		m_docs[i] = NULL;
		// error, g_errno should be set!
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, xd->set4 returned false", __FILE__, __func__, __LINE__);
		return true;
	}

	// . launch count increment
	// . this is only used locally on this host to set its
	//   m_hasUrlsReadyToSpider to false.
	//cr->m_localCrawlInfo.m_numUrlsLaunched++;

	// call this after doc gets indexed
	xd->setCallback ( xd  , indexedDocWrapper );

	/*
	// set it from provided parms if we are injecting via Msg7
	if ( m_sreq->m_isInjecting ) {
		// now fill these in if provided too!
		if ( m_content ) {
			if ( m_sreq->m_firstIp ) {
				xd->m_ip      = m_sreq->m_firstIp;
				xd->m_ipValid = true;
			}
			xd->m_isContentTruncated      = false;
			xd->m_isContentTruncatedValid = true;
			xd->m_httpReplyValid          = true;
			xd->m_httpReply               = m_content;
			xd->m_httpReplySize           = m_contentLen + 1;
			if ( ! m_contentHasMime ) xd->m_useFakeMime = true;
		}
		// a special callback for injected docs
		//xd->m_injectionCallback = m_callback;
		//xd->m_injectionState    = m_state;
	}
	*/

	// increase m_maxUsed if we have to
	if ( i > m_maxUsed ) m_maxUsed = i;
	// count it
	m_numSpidersOut++;
	// count this
	m_sc->m_spidersOut++;

	g_spiderLoop.m_launches++;

	// count it as a hit
	//g_stats.m_spiderUrlsHit++;
	// sanity check
	if (m_sreq->m_priority <= -1 ) { 
		log("spider: fixing bogus spider req priority of %i for "
		    "url %s",
		    (int)m_sreq->m_priority,m_sreq->m_url);
		m_sreq->m_priority = 0;
		//char *xx=NULL;*xx=0; 
	}
	//if(m_sreq->m_priority >= MAX_SPIDER_PRIORITIES){char *xx=NULL;*xx=0;}
	// update this
	m_sc->m_outstandingSpiders[(unsigned char)m_sreq->m_priority]++;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: sc_out=%"INT32" waiting=%"INT32" url=%s",
		    m_sc->m_spidersOut,
		    m_sc->m_waitingTree.m_numUsedNodes,
		    m_sreq->m_url);


	// debug log
	//log("XXX: incremented count to %"INT32" for %s",
	//    m_sc->m_spidersOut,m_sreq->m_url);
	//if ( m_sc->m_spidersOut != m_numSpidersOut ) { char *xx=NULL;*xx=0; }

	// . return if this blocked
	// . no, launch another spider!
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: calling xd->indexDoc", __FILE__, __func__, __LINE__);
	bool status = xd->indexDoc();
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: indexDoc status [%s]", __FILE__, __func__, __LINE__, status?"true":"false");

	// . reset the next doledbkey to start over!
	// . when spiderDoledUrls() see this negative priority it will
	//   reset the doledb scan to the top priority.
	// . MDW, no, 2/25/2015. then the 'goto loop;' statement
	//   basially thinks a round was completed and exits
	//m_sc->m_pri2 = -1;	
	// maybe then try setting to 127
	//m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );

	// if we were injecting and it blocked... return false
	if ( ! status ) 
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, indexDoc blocked", __FILE__, __func__, __LINE__);
		return false;
	}

	// deal with this error
	indexedDoc ( xd );

	// "callback" will not be called cuz it should be NULL
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, return true", __FILE__, __func__, __LINE__);
	return true;
}



// . the one that was just indexed
// . Msg7.cpp uses this to see what docid the injected doc got so it
//   can forward it to external program
//static int64_t s_lastDocId = -1;
//int64_t SpiderLoop::getLastDocId ( ) { return s_lastDocId; }

void indexedDocWrapper ( void *state ) {
	// . process the results
	// . return if this blocks
	if ( ! g_spiderLoop.indexedDoc ( (XmlDoc *)state ) ) return;
	//a hack to fix injecting urls, because they can
	//run at niceness 0 but most of the spider pipeline
	//cannot.  we should really just make injection run at
	//MAX_NICENESS. OK, done! mdw
	//if ( g_loop.m_inQuickPoll ) return;
	// . continue gettings Spider recs to spider
	// . if it's already waiting for a list it'll just return
	// . mdw: keep your eye on this, it was commented out
	// . this won't execute if we're already getting a list now
	//g_spiderLoop.spiderUrl ( );
	// spider some urls that were doled to us
	g_spiderLoop.spiderDoledUrls( );
}



// . this will delete m_docs[i]
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool SpiderLoop::indexedDoc ( XmlDoc *xd ) {
if( 	g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	// save the error in case a call changes it below
	//int32_t saved = g_errno;

	// get our doc #, i
	//int32_t i = doc - m_docs[0];
	int32_t i = 0;
	for ( ; i < MAX_SPIDERS ; i++ ) if ( m_docs[i] == xd) break;
	// sanity check
	if ( i >= MAX_SPIDERS ) { char *xx=NULL;*xx=0; }
	// set to -1 to indicate inject
	//if ( i < 0 || i >= MAX_SPIDERS ) i = -1;

	//char injecting = false;
	//if ( xd->m_sreq.m_isInjecting ) injecting = true;

	// save it for Msg7.cpp to pass docid of injected doc back 
	//s_lastDocId = xd->m_docId;


	// . decrease m_maxUsed if we need to
	// . we can decrease all the way to -1, which means no spiders going on
	if ( m_maxUsed == i ) {
		m_maxUsed--;
		while ( m_maxUsed >= 0 && ! m_docs[m_maxUsed] ) m_maxUsed--;
	}
	// count it
	m_numSpidersOut--;

	// get coll
	collnum_t collnum = xd->m_collnum;//tiondb.getCollnum ( xd->m_coll );
	// if coll was deleted while spidering, sc will be NULL
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);
	// decrement this
	if ( sc ) sc->m_spidersOut--;
	// get the original request from xmldoc
	SpiderRequest *sreq = &xd->m_sreq;
	// update this. 
	if ( sc ) sc->m_outstandingSpiders[(unsigned char)sreq->m_priority]--;

	// debug log
	//log("XXX: decremented count to %"INT32" for %s",
	//    sc->m_spidersOut,sreq->m_url);
	//if ( sc->m_spidersOut != m_numSpidersOut ) { char *xx=NULL;*xx=0; }

	// breathe
	QUICKPOLL ( xd->m_niceness );

	// are we a re-spider?
	bool respider = false;
	if ( xd->m_oldDocValid && xd->m_oldDoc ) respider = true;


	// note it
	// this should not happen any more since indexDoc() will take
	// care of g_errno now by clearing it and adding an error spider
	// reply to release the lock!!
	if ( g_errno ) {
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		log("spider: spidering %s has error: %s. uh48=%"INT64". "
		    //"Respidering "
		    //"in %"INT32" seconds. MAX_LOCK_AGE when lock expires. "
		    "cn=%"INT32"",
		    xd->m_firstUrl.getUrl(),
		    mstrerror(g_errno),
		    xd->getFirstUrlHash48(),
		    //(int32_t)MAX_LOCK_AGE,
		    (int32_t)collnum);
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ------ *** LOCAL ERROR ***  ------");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// log("spider: ----CRITICAL CRITICAL CRITICAL----");
		// don't release the lock on it right now. just let the
		// lock expire on it after MAX_LOCK_AGE seconds. then it will
		// be retried. we need to debug gb so these things never
		// hapeen...
	}
		
	// breathe
	QUICKPOLL ( xd->m_niceness );

	// . call the final callback used for injecting urls
	// . this may send a reply back so the caller knows the url
	//   was fully injected into the index
	// . Msg7.cpp uses a callback that returns a void, so use m_callback1!
	//if ( xd->m_injectionCallback && injecting ) {
	//	g_errno = saved;
	//	// use the index code as the error for PageInject.cpp
	//	if ( ! g_errno && xd->m_indexCode ) g_errno = xd->m_indexCode;
	//	xd->m_injectionCallback ( xd->m_injectionState );
	//}

	// we don't need this g_errno passed this point
	g_errno = 0;

	// breathe
	QUICKPOLL ( xd->m_niceness );

	// did this doc get a chance to add its meta list to msg4 bufs?
	//bool addedMetaList = m_docs[i]->m_listAdded;

	// set this in case we need to call removeAllLocks
	//m_uh48 = 0LL;
	//if ( xd->m_sreqValid ) m_uh48 = xd->m_sreq.getUrlHash48();

	// we are responsible for deleting doc now
	mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
	delete (m_docs[i]);
	m_docs[i] = NULL;

	// we remove the spider lock from g_spiderLoop.m_lockTable in Rdb.cpp
	// when it receives the negative doledb key. but if the this does not
	// happen, we have a problem then!
	//if ( addedMetaList ) return true;

	// sanity
	//if ( ! m_uh48 ) { char *xx=NULL; *xx=0; }

	// the lock we had in g_spiderLoop.m_lockTable for the doleKey
	// is now remove in Rdb.cpp when it receives a negative dole key to
	// add to doledb... assuming we added that meta list!!
	// m_uh48 should be set from above
	//if ( ! removeAllLocks () ) return false;

	// we did not block, so return true
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
	return true;
}



// use -1 for any collnum
int32_t SpiderLoop::getNumSpidersOutPerIp ( int32_t firstIp , collnum_t collnum ) {
	int32_t count = 0;
	// count locks
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	// scan the slots
	int32_t ns = ht->m_numSlots;
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		//QUICKPOLL(niceness);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		// skip if not outstanding, just a 5-second expiration wait
		// when the spiderReply returns, so that in case a lock
		// request for the same url was in progress, it will be denied.
		if ( ! lock->m_spiderOutstanding ) continue;
		// must be confirmed too
		if ( ! lock->m_confirmed ) continue;
		// correct collnum?
		if ( lock->m_collnum != collnum && collnum != -1 ) continue;
		// skip if not yet expired
		if ( lock->m_firstIp == firstIp ) count++;
	}
	/*
	for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
		// get it
		XmlDoc *xd = m_docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// check it
		if ( xd->m_firstIp == firstIp ) count++;
	}
	*/
	return count;
}



CollectionRec *SpiderLoop::getActiveList() {

	uint32_t nowGlobal = (uint32_t)getTimeGlobal();

	if ( nowGlobal >= m_recalcTime && m_recalcTimeValid )
		m_activeListValid = false;

	// we set m_activeListValid to false when enabling/disabling spiders,
	// when rebuilding url filters in Collectiondb.cpp rebuildUrlFilters()
	// and when updating the site list in updateSiteList(). all of these
	// could possible make an inactive collection active again, or vice
	// versa. also when deleting a collection in Collectiondb.cpp. this
	// keeps the below loop fast when we have thousands of collections
	// and most are inactive or empty/deleted.
	if ( ! m_activeListValid || m_activeListModified ) {
		buildActiveList();
		//m_crx = m_activeList;
		// recompute every 3 seconds, it seems kinda buggy!!
		m_recalcTime = nowGlobal + 3;
		m_recalcTimeValid = true;
		m_activeListModified = false;
	}

	return m_activeList;
}



void SpiderLoop::buildActiveList ( ) {
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	//log("spider: rebuilding active list");

	//m_bookmark = NULL;

	// set current time, synced with host #0
	uint32_t nowGlobal = (uint32_t)getTimeGlobal();

	// when do we need to rebuild the active list again?
	m_recalcTimeValid = false;

	m_activeListValid = true;

	m_activeListCount = 0;

	// reset the linked list of active collections
	m_activeList = NULL;
	bool found = false;

	CollectionRec *tail = NULL;

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get rec
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if gone
		if ( ! cr ) continue;
		// stop if not enabled
		bool active = true;
		if ( ! cr->m_spideringEnabled ) active = false;
		if ( cr->m_spiderStatus == SP_MAXROUNDS ) active = false;
		if ( cr->m_spiderStatus == SP_MAXTOCRAWL ) active = false;
		if ( cr->m_spiderStatus == SP_MAXTOPROCESS ) active = false;
		//
		// . if doing respider with roundstarttime....
		// . roundstarttime is > 0 if m_collectiveRespiderFrequency
		//   is > 0, unless it has not been set to current time yet
		// . if m_collectiveRespiderFrequency was set to 0.0 then
		//   PageCrawlBot.cpp also sets m_roundStartTime to 0.
		//
		if ( nowGlobal < cr->m_spiderRoundStartTime ) {
			active = false;
			// no need to do this now since we recalc every
			// 3 seconds anyway...
			// if ( cr->m_spiderRoundStartTime < m_recalcTime ) {
			// 	m_recalcTime = cr->m_spiderRoundStartTime;
			// 	m_recalcTimeValid = true;
			// }
		}

		// MDW: let's not do this either unless it proves to be a prob
		//if ( sc->m_needsRebuild ) active = true;

		// we are at the tail of the linked list OR not in the list
		cr->m_nextActive = NULL;

		cr->m_isActive = false;

		if ( ! active ) continue;

		cr->m_isActive = true;

		m_activeListCount++;

		if ( cr == m_crx ) found = true;


		// if first one, set it to head
		if ( ! tail ) {
			m_activeList = cr;
			tail = cr;
			continue;
		}

		// if not first one, add it to end of tail
		tail->m_nextActive = cr;
		tail = cr;
	}

	// we use m_bookmark so we do not get into an infinite loop
	// in spider urls logic above
	if ( ! found ) {
		m_bookmark = NULL;
		m_crx = NULL;
	}
	
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
}



void doneSendingNotification ( void *state ) {
	EmailInfo *ei = (EmailInfo *)state;
	collnum_t collnum = ei->m_collnum;
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( cr != ei->m_collRec ) cr = NULL;
	char *coll = "lostcoll";
	if ( cr ) coll = cr->m_coll;
	log(LOG_INFO,"spider: done sending notifications for coll=%s (%i)", 
	    coll,(int)ei->m_collnum);

	// all done if collection was deleted from under us
	if ( ! cr ) return;

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = false;

	// we can re-use the EmailInfo class now
	// pingserver.cpp sets this
	//ei->m_inUse = false;

	// so we do not send for this status again, mark it as sent for
	// but we reset sentCrawlDoneAlert to 0 on round increment below
	//log("spider: setting sentCrawlDoneAlert status to %"INT32"",
	//    (int32_t)cr->m_spiderStatus);

	// mark it as sent. anytime a new url is spidered will mark this
	// as false again! use LOCAL crawlInfo, since global is reset often.
	cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 1;//cr->m_spiderStatus;//1;

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

	// sanity
	if ( cr->m_spiderStatus == 0 ) { char *xx=NULL;*xx=0; }

	float respiderFreq = cr->m_collectiveRespiderFrequency;

	// if not REcrawling, set this to 0 so we at least update our
	// round # and round start time...
	if ( respiderFreq < 0.0 ) {
		respiderFreq = 0.0;
	}
	
	////////
	//
	// . we are here because hasUrlsReadyToSpider is false
	// . we just got done sending an email alert
	// . now increment the round only if doing rounds!
	//
	///////


	// if not doing rounds, keep the round 0. they might want to up
	// their maxToCrawl limit or something.
	if ( respiderFreq <= 0.0 ) return;

	// if we hit the max to crawl rounds, then stop!!! do not
	// increment the round...
	if ( cr->m_spiderRoundNum >= cr->m_maxCrawlRounds &&
	     // there was a bug when maxCrawlRounds was 0, which should
	     // mean NO max, so fix that here:
	     cr->m_maxCrawlRounds > 0 ) return;

	int32_t seconds = (int32_t)(respiderFreq * 24*3600);
	// add 1 for lastspidertime round off errors so we can be assured
	// all spiders have a lastspidertime LESS than the new
	// m_spiderRoundStartTime we set below.
	if ( seconds <= 0 ) seconds = 1;

	// now update this round start time. all the other hosts should
	// sync with us using the parm sync code, msg3e, every 13.5 seconds.
	//cr->m_spiderRoundStartTime += respiderFreq;
	char roundTime[128];
	sprintf(roundTime,"%"UINT32"", (uint32_t)(getTimeGlobal() + seconds));
	char roundStr[128];
	sprintf(roundStr,"%"INT32"", cr->m_spiderRoundNum + 1);

	// we have to send these two parms to all in cluster now INCLUDING
	// ourselves, when we get it in Parms.cpp there's special
	// code to set this *ThisRound counts to 0!!!
	SafeBuf parmList;
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundStr,-1 ,
				    "spiderRoundNum");
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundTime, -1 ,
				    "spiderRoundStart");

	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundNum" ); 
	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundStart" ); 
	// this uses msg4 so parm ordering is guaranteed
	g_parms.broadcastParmList ( &parmList , NULL , NULL );

	// log it
	log("spider: new round #%"INT32" starttime = %"UINT32" for %s"
	    , cr->m_spiderRoundNum
	    , (uint32_t)cr->m_spiderRoundStartTime
	    , cr->m_coll
	    );
}

bool sendNotificationForCollRec ( CollectionRec *cr )  {
	// wtf? caller must set this
	if ( ! cr->m_spiderStatus ) { char *xx=NULL; *xx=0; }

	log(LOG_INFO,
	    "spider: sending notification for crawl status %"INT32" in "
	    "coll %s. "
	    ,(int32_t)cr->m_spiderStatus
	    ,cr->m_coll
	    );

	if ( cr->m_sendingAlertInProgress ) return true;

	// ok, send it
	EmailInfo *ei = (EmailInfo *)mcalloc ( sizeof(EmailInfo),"eialrt");
	if ( ! ei ) {
		log("spider: could not send email alert: %s",
		    mstrerror(g_errno));
		return true;
	}

	// set it up
	ei->m_finalCallback = doneSendingNotification;
	ei->m_finalState    = ei;
	ei->m_collnum       = cr->m_collnum;
	// collnums can be recycled, so ensure collection with the ptr
	ei->m_collRec       = cr;

	SafeBuf *buf = &ei->m_spiderStatusMsg;
	// stop it from accumulating status msgs
	buf->reset();
	int32_t status = -1;
	getSpiderStatusMsg ( cr , buf , &status );
					 
	// if no email address or webhook provided this will not block!
	// DISABLE THIS UNTIL FIXED

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = true;

	// ok, put it back...
	if ( ! sendNotification ( ei ) ) return false;

	// so handle this ourselves in that case:
	doneSendingNotification ( ei );
	
	mfree ( ei , sizeof(EmailInfo) ,"eialrt" );

	return true;
}



void gotCrawlInfoReply ( void *state , UdpSlot *slot);

static int32_t s_requests = 0;
static int32_t s_replies  = 0;
static int32_t s_validReplies  = 0;
static bool s_inUse = false;
// we initialize CollectionRec::m_updateRoundNum to 0 so make this 1
static int32_t s_updateRoundNum = 1;

// . just call this once per second for all collections
// . figure out how to backoff on collections that don't need it so much
// . ask every host for their crawl infos for each collection rec
void updateAllCrawlInfosSleepWrapper ( int fd , void *state ) {

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);

	// i don't know why we have locks in the lock table that are not
	// getting removed... so log when we remove an expired locks and see.
	// piggyback on this sleep wrapper call i guess...
	// perhaps the collection was deleted or reset before the spider
	// reply could be generated. in that case we'd have a dangling lock.
	removeExpiredLocks ( -1 );

	if ( s_inUse ) return;

	// "i" means to get incremental updates since last round
	// "f" means to get all stats
	char *request = "i";
	int32_t requestSize = 1;

	static bool s_firstCall = true;
	if ( s_firstCall ) {
		s_firstCall = false;
		request = "f";
	}

	s_inUse = true;

	// send out the msg request
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		// count it as launched
		s_requests++;
		// launch it
		if ( ! g_udpServer.sendRequest ( request,
						 requestSize,
						 0xc1 , // msgtype
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL, // retslot
						 NULL, // state
						 gotCrawlInfoReply ) ) {
			log("spider: error sending c1 request: %s",
			    mstrerror(g_errno));
			s_replies++;
		}
	}
	
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Sent %"INT32" requests, got %"INT32" replies", __FILE__, __func__, __LINE__, s_requests, s_replies);
	// return false if we blocked awaiting replies
	if ( s_replies < s_requests )
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END. requests/replies mismatch", __FILE__, __func__, __LINE__);
		return;
	}

	// how did this happen?
	log("spider: got bogus crawl info replies!");
	s_inUse = false;
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
	return;
}



// . Parms.cpp calls this when it receives our "spiderRoundNum" increment above
// . all hosts should get it at *about* the same time
void spiderRoundIncremented ( CollectionRec *cr ) {

	log("spider: incrementing spider round for coll %s to %"INT32" (%"UINT32")",
	    cr->m_coll,cr->m_spiderRoundNum,
	    (uint32_t)cr->m_spiderRoundStartTime);

	// . need to send a notification for this round
	// . we are only here because the round was incremented and
	//   Parms.cpp just called us... and that only happens in 
	//   doneSending... so do not send again!!!
	//cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;

	// . if we set sentCrawlDoneALert to 0 it will immediately
	//   trigger another round increment !! so we have to set these
	//   to true to prevent that.
	// . if we learnt that there really are no more urls ready to spider
	//   then we'll go to the next round. but that can take like
	//   SPIDER_DONE_TIMER seconds of getting nothing.
	cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = true;
	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;

	cr->m_localCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_localCrawlInfo.m_pageProcessSuccessesThisRound  = 0;
	cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound  = 0;

	cr->localCrawlInfoUpdate();

	cr->m_needsSave = true;
}

void gotCrawlInfoReply ( void *state , UdpSlot *slot ) {

	// loop over each LOCAL crawlinfo we received from this host
	CrawlInfo *ptr   = (CrawlInfo *)(slot->m_readBuf);
	CrawlInfo *end   = (CrawlInfo *)(slot->m_readBuf+ slot->m_readBufSize);
	//int32_t       allocSize           = slot->m_readBufMaxSize;

	// host sending us this reply
	Host *h = slot->m_host;

	// assume it is a valid reply, not an error, like a udptimedout
	s_validReplies++;

	// reply is error? then use the last known good reply we had from him
	// assuming udp reply timed out. empty buf just means no update now!
	if ( ! slot->m_readBuf && g_errno ) {
		log("spider: got crawlinfo reply error from host %"INT32": %s. "
		    "spidering will be paused.",
		    h->m_hostId,mstrerror(g_errno));
		// just clear it
		g_errno = 0;
		// if never had any reply... can't be valid then
		if ( ! ptr ) s_validReplies--;
	}

	// inc it
	s_replies++;

	if ( s_replies > s_requests ) { char *xx=NULL;*xx=0; }


	// crap, if any host is dead and not reporting it's number then
	// that seriously fucks us up because our global count will drop
	// and something that had hit a max limit, like maxToCrawl, will
	// now be under the limit and the crawl will resume.
	// what's the best way to fix this?
	//
	// perhaps, let's just keep the dead host's counts the same
	// as the last time we got them. or maybe the simplest way is to
	// just not allow spidering if a host is dead 

	// the sendbuf should never be freed! it points into collrec
	// it is 'i' or 'f' right now
	slot->m_sendBufAlloc = NULL;

	/////
	//  SCAN the list of CrawlInfos we received from this host, 
	//  one for each non-null collection
	/////

	// . add the LOCAL stats we got from the remote into the GLOBAL stats
	// . readBuf is null on an error, so check for that...
	// . TODO: do not update on error???
	for ( ; ptr < end ; ptr++ ) {

		QUICKPOLL ( slot->m_niceness );

		// get collnum
		collnum_t collnum = (collnum_t)(ptr->m_collnum);

		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) {
			log("spider: updatecrawlinfo collnum %"INT32" "
			    "not found",(int32_t)collnum);
			continue;
		}
		
		//CrawlInfo *stats = ptr;

		// just copy into the stats buf
		if ( ! cr->m_crawlInfoBuf.getBufStart() ) {
			int32_t need = sizeof(CrawlInfo) * g_hostdb.m_numHosts;
			cr->m_crawlInfoBuf.setLabel("cibuf");
			cr->m_crawlInfoBuf.reserve(need);
			// in case one was udp server timed out or something
			cr->m_crawlInfoBuf.zeroOut();
		}

		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		if ( cia )
			gbmemcpy ( &cia[h->m_hostId] , ptr , sizeof(CrawlInfo));

		// mark it for computation once we got all replies
		cr->m_updateRoundNum = s_updateRoundNum;
	}

	// keep going until we get all replies
	if ( s_replies < s_requests ) return;
	
	// if it's the last reply we are to receive, and 1 or more 
	// hosts did not have a valid reply, and not even a
	// "last known good reply" then then we can't do
	// much, so do not spider then because our counts could be
	// way off and cause us to start spidering again even though
	// we hit a maxtocrawl limit!!!!!
	if ( s_validReplies < s_replies ) {
		// this will tell us to halt all spidering
		// because a host is essentially down!
		s_countsAreValid = false;
	} else {
		if ( ! s_countsAreValid )
			log("spider: got all crawlinfo replies. all shards "
			    "up. spidering back on.");
		s_countsAreValid = true;
	}

	// loop over 
	for ( int32_t x = 0 ; x < g_collectiondb.m_numRecs ; x++ ) {
		QUICKPOLL ( slot->m_niceness );

		// a niceness 0 routine could have nuked it?
		if ( x >= g_collectiondb.m_numRecs )
			break;

		CollectionRec *cr = g_collectiondb.m_recs[x];
		if ( ! cr ) continue;

		// must be in need of computation
		if ( cr->m_updateRoundNum != s_updateRoundNum ) continue;

		CrawlInfo *gi = &cr->m_globalCrawlInfo;
		int32_t hadUrlsReady = gi->m_hasUrlsReadyToSpider;

		// clear it out
		gi->reset();

		// retrieve stats for this collection and scan all hosts
		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		// if empty for all hosts, i guess no stats...
		if ( ! cia ) continue;

		for ( int32_t k = 0 ; k < g_hostdb.m_numHosts; k++ ) {
			QUICKPOLL ( slot->m_niceness );
			// get the CrawlInfo for the ith host
			CrawlInfo *stats = &cia[k];
			// point to the stats for that host
			int64_t *ss = (int64_t *)stats;
			int64_t *gs = (int64_t *)gi;
			// add each hosts counts into the global accumulators
			for ( int32_t j = 0 ; j < NUMCRAWLSTATS ; j++ ) {
				*gs = *gs + *ss;
				// crazy stat?
				if ( *ss > 1000000000LL ||
				     *ss < -1000000000LL ) 
					log("spider: crazy stats %"INT64" "
					    "from host #%"INT32" coll=%s",
					    *ss,k,cr->m_coll);
				gs++;
				ss++;
			}
			// . special counts
			gi->m_pageDownloadSuccessesThisRound +=
				stats->m_pageDownloadSuccessesThisRound;
			gi->m_pageProcessSuccessesThisRound +=
				stats->m_pageProcessSuccessesThisRound;

			if ( ! stats->m_hasUrlsReadyToSpider ) continue;
			// inc the count otherwise
			gi->m_hasUrlsReadyToSpider++;
			// . no longer initializing?
			// . sometimes other shards get the spider 
			//  requests and not us!!!
			if ( cr->m_spiderStatus == SP_INITIALIZING )
				cr->m_spiderStatus = SP_INPROGRESS;
			// i guess we are back in business even if
			// m_spiderStatus was SP_MAXTOCRAWL or 
			// SP_ROUNDDONE...
			cr->m_spiderStatus = SP_INPROGRESS;

			// revival?
			if ( ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider )
				log("spider: reviving crawl %s from host %" INT32"", cr->m_coll,k);
		} // end loop over hosts

		if ( hadUrlsReady &&
		     // and it no longer does now...
		     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
			log(LOG_INFO,
			    "spider: all %"INT32" hosts report "
			    "%s (%"INT32") has no "
			    "more urls ready to spider",
			    s_replies,cr->m_coll,(int32_t)cr->m_collnum);
			// set crawl end time
			cr->m_diffbotCrawlEndTime = getTimeGlobalNoCore();
		}

		// should we reset our "sent email" flag?
		bool reset = false;

		// can't reset if we've never sent an email out yet
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) reset = true;
		    
		// must have some urls ready to spider now so we can send
		// another email after another round of spidering
		if (!cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider) reset=false;

		// . if we have urls ready to be spidered then prepare to send
		//   another email/webhook notification.
		// . do not reset this flag if SP_MAXTOCRAWL etc otherwise we 
		//   end up sending multiple notifications, so this logic here
		//   is only for when we are done spidering a round, which 
		//   happens when hasUrlsReadyToSpider goes false for all 
		//   shards.
		if ( reset ) {
			log("spider: resetting sent crawl done alert to 0 "
			    "for coll %s",cr->m_coll);
			cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;
		}

		// update cache time
		cr->m_globalCrawlInfo.m_lastUpdateTime = getTime();
		
		// make it save to disk i guess
		cr->m_needsSave = true;

		// if spidering disabled in master controls then send no
		// notifications
		// crap, but then we can not update the round start time
		// because that is done in doneSendingNotification().
		// but why does it say all 32 report done, but then
		// it has urls ready to spider?
		if ( ! g_conf.m_spideringEnabled )
			continue;

		// and we've examined at least one url. to prevent us from
		// sending a notification if we haven't spidered anything
		// because no seed urls have been added/injected.
		//if ( cr->m_globalCrawlInfo.m_urlsConsidered == 0 ) return;
		if ( cr->m_globalCrawlInfo.m_pageDownloadAttempts == 0 &&
		     // if we don't have this here we may not get the
		     // pageDownloadAttempts in time from the host that
		     // did the spidering.
		     ! hadUrlsReady ) 
			continue;

		// but of course if it has urls ready to spider, do not send 
		// alert... or if this is -1, indicating "unknown".
		if ( cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) 
			continue;

		// update status if nto already SP_MAXTOCRAWL, etc. we might
		// just be flat out of urls
		if ( ! cr->m_spiderStatus || 
		     cr->m_spiderStatus == SP_INPROGRESS ||
		     cr->m_spiderStatus == SP_INITIALIZING )
			cr->m_spiderStatus = SP_ROUNDDONE;

		//
		// TODO: set the spiderstatus outright here...
		// maxtocrawl, maxtoprocess, etc. based on the counts.
		//

		// only host #0 sends emails
		if ( g_hostdb.m_myHost->m_hostId != 0 )
			continue;

		// . if already sent email for this, skip
		// . localCrawlInfo stores this value on disk so persistent
		// . we do it this way so SP_ROUNDDONE can be emailed and then
		//   we'd email SP_MAXROUNDS to indicate we've hit the maximum
		//   round count. 
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) {
			// debug
			logf(LOG_DEBUG,"spider: already sent alert for %s"
			     , cr->m_coll);
			continue;
		}

		// do email and web hook...
		sendNotificationForCollRec ( cr );

		// deal with next collection rec
	}

	// initialize
	s_replies  = 0;
	s_requests = 0;
	s_validReplies = 0;
	s_inUse    = false;
	s_updateRoundNum++;
}

void handleRequestc1 ( UdpSlot *slot , int32_t niceness ) {
	//char *request = slot->m_readBuf;
	// just a single collnum
	if ( slot->m_readBufSize != 1 ) { char *xx=NULL;*xx=0;}

	char *req = slot->m_readBuf;

	if ( ! slot->m_host ) {
		log("handc1: no slot->m_host from ip=%s udpport=%i",
		    iptoa(slot->m_ip),(int)slot->m_port);
		g_errno = ENOHOSTS;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	//int32_t now = getTimeGlobal();
	SafeBuf replyBuf;

	uint32_t now = (uint32_t)getTimeGlobalNoCore();

	uint64_t nowMS = gettimeofdayInMillisecondsGlobalNoCore();

	//SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {

		QUICKPOLL(slot->m_niceness);

		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;

		// shortcut
		CrawlInfo *ci = &cr->m_localCrawlInfo;

		// this is now needed for alignment by the receiver
		ci->m_collnum = i;

		SpiderColl *sc = cr->m_spiderColl;

		/////////
		//
		// ARE WE DONE SPIDERING?????
		//
		/////////

		// speed up qa pipeline
		uint32_t spiderDoneTimer = (uint32_t)SPIDER_DONE_TIMER;
		if ( cr->m_coll[0] == 'q' &&
		     cr->m_coll[1] == 'a' &&
		     strcmp(cr->m_coll,"qatest123")==0)
			spiderDoneTimer = 10;

		// if we haven't spidered anything in 1 min assume the
		// queue is basically empty...
		if ( ci->m_lastSpiderAttempt &&
		     ci->m_lastSpiderCouldLaunch &&
		     ci->m_hasUrlsReadyToSpider &&
		     // the next round we are waiting for, if any, must
		     // have had some time to get urls! otherwise we
		     // will increment the round # and wait just
		     // SPIDER_DONE_TIMER seconds and end up setting
		     // hasUrlsReadyToSpider to false!
		     now > cr->m_spiderRoundStartTime + spiderDoneTimer &&
		     // no spiders currently out. i've seen a couple out
		     // waiting for a diffbot reply. wait for them to
		     // return before ending the round...
		     sc && sc->m_spidersOut == 0 &&
		     // it must have launched at least one url! this should
		     // prevent us from incrementing the round # at the gb
		     // process startup
		     //ci->m_numUrlsLaunched > 0 &&
		     //cr->m_spideringEnabled &&
		     //g_conf.m_spideringEnabled &&
		     ci->m_lastSpiderAttempt - ci->m_lastSpiderCouldLaunch > 
		     spiderDoneTimer ) {

			// break it here for our collnum to see if
			// doledb was just lagging or not.
			bool printIt = true;
			if ( now < sc->m_lastPrinted ) printIt = false;
			if ( printIt ) sc->m_lastPrinted = now + 5;

			// doledb must be empty
			if ( ! sc->m_doleIpTable.isEmpty() ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "doledb not empty for coll=%s",cr->m_coll);
				goto doNotEnd;
			}

			uint64_t nextTimeMS ;
			nextTimeMS = sc->getNextSpiderTimeFromWaitingTree ( );

			// and no ips awaiting scans to get into doledb
			// except for ips needing scans 60+ seconds from now
			if ( nextTimeMS &&  nextTimeMS < nowMS + 60000 ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree key is ready for scan "
				    "%"INT64" ms from now for coll=%s",
				    nextTimeMS - nowMS,cr->m_coll );
				goto doNotEnd;
			}

			// maybe wait for waiting tree population to finish
			if ( sc->m_waitingTreeNeedsRebuild ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree is building for coll=%s",
				    cr->m_coll );
				goto doNotEnd;
			}

			// this is the MOST IMPORTANT variable so note it
			log(LOG_INFO,
			    "spider: coll %s has no more urls to spider",
			    cr->m_coll);
			// assume our crawl on this host is completed i guess
			ci->m_hasUrlsReadyToSpider = 0;
			// if changing status, resend local crawl info to all
			cr->localCrawlInfoUpdate();
			// save that!
			cr->m_needsSave = true;
		}

	doNotEnd:

		int32_t hostId = slot->m_host->m_hostId;

		bool sendIt = false;

		// . if not sent to host yet, send
		// . this will be true when WE startup, not them...
		// . but once we send it we set flag to false
		// . and if we update anything we send we set flag to true
		//   again for all hosts
		if ( cr->shouldSendLocalCrawlInfoToHost(hostId) ) 
			sendIt = true;

		// they can override. if host crashed and came back up
		// it might not have saved the global crawl info for a coll
		// perhaps, at the very least it does not have
		// the correct CollectionRec::m_crawlInfoBuf because we do
		// not save the array of crawlinfos for each host for
		// all collections.
		if ( req && req[0] == 'f' )
			sendIt = true;

		if ( ! sendIt ) continue;

		// note it
		// log("spider: sending ci for coll %s to host %"INT32"",
		//     cr->m_coll,hostId);
		
		// save it
		replyBuf.safeMemcpy ( ci , sizeof(CrawlInfo) );

		// do not re-do it unless it gets update here or in XmlDoc.cpp
		cr->sentLocalCrawlInfoToHost ( hostId );
	}

	g_udpServer.sendReply_ass( replyBuf.getBufStart(), replyBuf.length(), replyBuf.getBufStart(),
							   replyBuf.getCapacity(), slot );

	// udp server will free this
	replyBuf.detachBuf();
}



