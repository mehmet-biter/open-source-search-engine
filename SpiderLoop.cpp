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
		    "lockkey=%" PRId64" "
		    "spiderout=%" PRId32" "
		    "confirmed=%" PRId32" "
		    "firstip=%s "
		    "expires=%" PRId32" "
		    "hostid=%" PRId32" "
		    "timestamp=%" PRId32" "
		    "sequence=%" PRId32" "
		    "collnum=%" PRId32" "
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
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
	
	//m_cri     = 0;
	m_crx = NULL;
	m_activeListValid = false;
	m_activeListModified = false;
	m_activeList = NULL;
	m_recalcTime = 0;
	m_recalcTimeValid = false;
	// falsify this flag
	m_outstanding1 = false;

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
	if ( !g_loop.registerSleepCallback(20000, this, updateAllCrawlInfosSleepWrapper)) {
		log(LOG_ERROR, "build: failed to register updatecrawlinfowrapper");
	}
		
	logTrace( g_conf.m_logTraceSpider, "END" );
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

	// point to head of active linked list of collection recs
	CollectionRec *nextActive = g_spiderLoop.getActiveList(); 
	collnum_t nextActiveCollnum = nextActive ? nextActive->m_collnum : static_cast<collnum_t>( -1 );

	//for ( int32_t i = 0 ; i < nc ; i++ ) {
	for ( ; nextActive ;  ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);

		// before we assign crp to nextActive, ensure that it did not get deleted on us.
		// if the next collrec got deleted, tr will be NULL
		CollectionRec *tr = g_collectiondb.getRec( nextActiveCollnum );

		// if it got deleted or restarted then it will not
		// match most likely
		if ( tr != nextActive ) {
			// this shouldn't happen much so log it
			log("spider: collnum %" PRId32" got deleted. rebuilding active list", (int32_t)nextActiveCollnum);

			// rebuild the active list now
			nextActive = g_spiderLoop.getActiveList();
			nextActiveCollnum = nextActive ? nextActive->m_collnum : static_cast<collnum_t>( -1 );

			continue;
		}

		// now we become him
		CollectionRec *crp = nextActive;

		// update these two vars for next iteration
		nextActive = crp->m_nextActive;
		nextActiveCollnum = nextActive ? nextActive->m_collnum : static_cast<collnum_t>( -1 );

		// skip if not enabled
		if ( ! crp->m_spideringEnabled ) {
			continue;
		}

		// get it
		SpiderColl *sc = g_spiderCache.getSpiderColl(crp->m_collnum);

		// skip if none
		if ( ! sc ) {
			continue;
		}

		// always do a scan at startup & every 24 hrs
		// AND at process startup!!!
		if ( ! sc->m_waitingTreeNeedsRebuild && now - sc->m_lastScanTime > 24*3600 ) {
			// if a scan is ongoing, this will re-set it
			sc->m_nextKey2.setMin();
			sc->m_waitingTreeNeedsRebuild = true;
			log( LOG_INFO, "spider: hit spider queue rebuild timeout for %s (%" PRId32")",
			     crp->m_coll, (int32_t)crp->m_collnum );
		}

//@@@@@@
//@@@ BR: Why not check m_waitingTreeNeedsRebuild before calling??
		// try this then. it just returns if
		// sc->m_waitingTreeNeedsRebuild is false so it
		// should be fast in those cases
		// re-entry is false because we are entering for the first time
		logTrace( g_conf.m_logTraceSpider, "Calling populateWaitingTreeFromSpiderdb" );
		sc->populateWaitingTreeFromSpiderdb ( false );


		logTrace( g_conf.m_logTraceSpider, "Calling populateDoledbFromWaitingTree" );
		sc->populateDoledbFromWaitingTree ( );
	}

	// if recently called, do not call again from the sleep wrapper
	int64_t nowms = gettimeofdayInMillisecondsLocal();
	if ( nowms - g_spiderLoop.m_lastCallTime < 50 )
		return;

	// if we have a ton of collections, reduce cpu load from calling
	// spiderDoledUrls()
	static uint64_t s_skipCount = 0;
	s_skipCount++;

	// so instead of every 50ms make it every 200ms if we got 100+ collections in use.
	g_spiderLoop.getActiveList(); 
	int32_t activeListCount = g_spiderLoop.m_activeListCount;
	if ( ! g_spiderLoop.m_activeListValid ) {
		activeListCount = 0;
	}

	int32_t skip = 1;
	if ( activeListCount >= 200 ) {
		skip = 8;
	} else if ( activeListCount >= 100 ) {
		skip = 4;
	} else if ( activeListCount >= 50 ) {
		skip = 2;
	}

	if ( ( s_skipCount % skip ) != 0 ) {
		return;
	}

	// spider some urls that were doled to us
	logTrace( g_conf.m_logTraceSpider, "Calling spiderDoledUrls"  );

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
	logTrace( g_conf.m_logTraceSpider, "BEGIN"  );

	m_lastCallTime = gettimeofdayInMillisecondsLocal();

collLoop:

	// start again at head if this is NULL
	if ( ! m_crx ) m_crx = getActiveList();

	bool firstTime = true;

	// detect overlap
	m_bookmark = m_crx;

	// get this
	m_sc = NULL;

	// set this in the loop
	CollectionRec *cr = NULL;
	uint32_t nowGlobal = 0;

	m_launches = 0;

subloop:

	QUICKPOLL(MAX_NICENESS);

	// must be spidering to dole out
	if ( ! g_conf.m_spideringEnabled ) {
		logTrace( g_conf.m_logTraceSpider, "END, spidering disabled"  );
		return;
	}

	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) {
		logTrace( g_conf.m_logTraceSpider, "END, spidering disabled (2)"  );
		return;
	}

	// or if trying to exit
	if ( g_process.m_mode == EXIT_MODE ) {
		logTrace( g_conf.m_logTraceSpider, "END, shutting down"  );
		return;	
	}
	
	// if we don't have all the url counts from all hosts, then wait.
	// one host is probably down and was never up to begin with
	if ( ! s_countsAreValid ) {
		logTrace( g_conf.m_logTraceSpider, "END, counts not valid"  );
		return;
	}
	
	// if we do not overlap ourselves
	if ( m_gettingDoledbList ) {
		logTrace( g_conf.m_logTraceSpider, "END, already getting DoledbList"  );
		return;
	}
	
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) {
		logTrace( g_conf.m_logTraceSpider, "END, in read-only mode"  );
		return;
	}
	
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) {
		logTrace( g_conf.m_logTraceSpider, "END, doing daily merge"  );
		return;
	}
		
	// skip if too many udp slots being used
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) {
		logTrace( g_conf.m_logTraceSpider, "END, using max UDP slots"  );
		return;
	}
	
	// stop if too many out. this is now 50 down from 500.
	if ( m_numSpidersOut >= MAX_SPIDERS ) {
		logTrace( g_conf.m_logTraceSpider, "END, reached max spiders"  );
		return;
	}
	
	// a new global conf rule
	if ( m_numSpidersOut >= g_conf.m_maxTotalSpiders ) {
		logTrace( g_conf.m_logTraceSpider, "END, reached max total spiders"  );
		return;
	}
		
	// bail if no collections
	if ( g_collectiondb.m_numRecs <= 0 ) {
		logTrace( g_conf.m_logTraceSpider, "END, no collections"  );
		return;
	}

	// not while repairing
	if ( g_repairMode ) {
		logTrace( g_conf.m_logTraceSpider, "END, in repair mode"  );
		return;
	}
		
	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) {
		logTrace( g_conf.m_logTraceSpider, "END, not in sync with host#0"  );
		return;
	}
	
	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( ! g_pingServer.m_hostsConfInAgreement ) {
		logTrace( g_conf.m_logTraceSpider, "END, host config disagreement"  );
		return;
	}
		
	// if nothin in the active list then return as well
	if ( ! m_activeList ) {
		logTrace( g_conf.m_logTraceSpider, "END, nothing in active list"  );
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
	if ( m_crx == m_bookmark && ! firstTime && m_launches == 0 ) {
		logTrace( g_conf.m_logTraceSpider, "END, end of list?"  );
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
	if ( ! m_activeList ) {
		logTrace( g_conf.m_logTraceSpider, "END, active list empty" );
		return;
	}


	cr = m_crx;

	// Fix to shut up STACK
	if( !m_crx ) {
		goto collLoop;
	}


	// advance for next time we call goto subloop;
	m_crx = m_crx->m_nextActive;


	// get the spider collection for this collnum
	m_sc = g_spiderCache.getSpiderColl(cr->m_collnum);

	// skip if none
	if ( ! m_sc ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, no spider cache for this collection"  );
		goto subloop;
	}
		
	// always reset priority to max at start
	m_sc->setPriority ( MAX_SPIDER_PRIORITIES - 1 );

subloopNextPriority:

	QUICKPOLL(MAX_NICENESS);

	// skip if gone
    if ( ! cr ) goto subloop;

	// stop if not enabled
	if ( ! cr->m_spideringEnabled ) goto subloop;

	// shortcut
	CrawlInfo *ci = &cr->m_localCrawlInfo;

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
	if ( nowGlobal < cr->m_spiderRoundStartTime ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, Spider start time not reached" );
		goto subloop;
	}

	// if populating this collection's waitingtree assume
	// we would have found something to launch as well. it might
	// mean the waitingtree-saved.dat file was deleted from disk
	// so we need to rebuild it at startup.
	if ( m_sc->m_waitingTreeNeedsRebuild ) {
		ci->m_lastSpiderCouldLaunch = nowGlobal;
	}

	// get max spiders
	int32_t maxSpiders = cr->m_maxNumSpiders;

	logTrace( g_conf.m_logTraceSpider, "maxSpiders: %" PRId32 , maxSpiders );

	// if some spiders are currently outstanding
	if ( m_sc->m_spidersOut ) {
		// do not end the crawl until empty of urls because
		// that url might end up adding more links to spider
		// when it finally completes
		ci->m_lastSpiderCouldLaunch = nowGlobal;
	}

	// obey max spiders per collection too
	if ( m_sc->m_spidersOut >= maxSpiders ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, Too many spiders active for collection"  );
		goto subloop;
	}

	// shortcut
	SpiderColl *sc = cr->m_spiderColl;

	if ( sc && sc->m_doleIpTable.isEmpty() ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, doleIpTable is empty"  );
		goto subloop;
	}

	// sanity check
	if ( nowGlobal == 0 ) { g_process.shutdownAbort(true); }

	// need this for msg5 call
	key_t endKey;
	endKey.setMax();

	// init the m_priorityToUfn map array?
	if ( ! m_sc->m_ufnMapValid ) {
		// reset all priorities to map to a ufn of -1
		for ( int32_t i = 0 ; i < MAX_SPIDER_PRIORITIES ; i++ ) {
			m_sc->m_priorityToUfn[i] = -1;
		}

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
			if ( sp >= MAX_SPIDER_PRIORITIES){
				g_process.shutdownAbort(true);
			}

			// skip if already mapped
			if ( m_sc->m_priorityToUfn[sp] != -1 ) continue;

			// map that
			m_sc->m_priorityToUfn[sp] = i;
		}

		// all done
		m_sc->m_ufnMapValid = true;
	}

	for ( ; ; ) {
		QUICKPOLL( MAX_NICENESS );

		// shortcut
		ci = &cr->m_localCrawlInfo;

		// reset priority when it goes bogus
		if ( m_sc->m_pri2 < 0 ) {
			// reset for next coll
			m_sc->setPriority( MAX_SPIDER_PRIORITIES - 1 );

			logTrace( g_conf.m_logTraceSpider, "Loop, pri2 < 0"  );
			goto subloop;
		}

		// sanity
		if ( cr != m_sc->getCollectionRec() ) {
			g_process.shutdownAbort(true);
		}

		// skip the priority if we already have enough spiders on it
		int32_t out = m_sc->m_outstandingSpiders[ m_sc->m_pri2 ];

		// how many spiders can we have out?
		int32_t max = 0;
		for ( int32_t i = 0; i < cr->m_numRegExs; i++ ) {
			if ( cr->m_spiderPriorities[ i ] != m_sc->m_pri2 ) {
				continue;
			}

			if ( cr->m_maxSpidersPerRule[ i ] > max ) {
				max = cr->m_maxSpidersPerRule[ i ];
			}
		}

		// if we have one out, do not end the round!
		if ( out > 0 ) {
			// assume we could have launched a spider
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		}

		// always allow at least 1, they can disable spidering otherwise
		// no, we use this to disabled spiders... if ( max <= 0 ) max = 1;
		// skip?
		if ( out >= max ) {
			// try the priority below us
			m_sc->devancePriority();

			// and try again
			logTrace( g_conf.m_logTraceSpider, "Loop, trying previous priority" );

			continue;
		}

		break;
	}

	// we only launch one spider at a time... so lock it up
	m_gettingDoledbList = true;

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		m_doleStart = gettimeofdayInMillisecondsLocal();

		if ( m_sc->m_msg5StartKey != m_sc->m_nextDoledbKey ) {
			log( "spider: msg5startKey differs from nextdoledbkey" );
		}
	}

	// seems like we need this reset here... strange
	m_list.reset();

	logTrace( g_conf.m_logTraceSpider, "Getting list (msg5)" );

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
				0               , // max cache age
				0               , // startFileNum
				-1              , // numFiles (all)
				this            , // state 
				gotDoledbListWrapper2 ,
				MAX_NICENESS    , // niceness
				true,             // do err correction
				NULL,             // cacheKeyPtr
			        0,                // retryNum
			        -1,               // maxRetries
			        true,             // compensateForMerge
			        -1,               // syncPoint
			        false,            // isRealMerge
			        true))            // allowPageCache
	{
		// return if it blocked
		logTrace( g_conf.m_logTraceSpider, "END, getList blocked" );

		return;
	}

	// breathe
	QUICKPOLL ( MAX_NICENESS );

	int32_t saved = m_launches;

	// . add urls in list to cache
	// . returns true if we should read another list
	// . will set startKey to next key to start at
	bool status = gotDoledbList2 ( );
	logTrace( g_conf.m_logTraceSpider, "Back from gotDoledList2. Get more? %s", status ? "true" : "false" );

	// if we did not launch anything, then decrement priority and
	// try again. but if priority hits -1 then subloop2 will just go to 
	// the next collection.
	if ( saved == m_launches ) {
		m_sc->devancePriority();

		logTrace( g_conf.m_logTraceSpider, "Loop, get next priority" );

		goto subloopNextPriority;
	}

	logTrace( g_conf.m_logTraceSpider, "END, loop" );

	if ( status ) {
		// try another read
		// now advance to next coll, launch one spider per coll
		goto subloop;
	}

	// now advance to next coll, launch one spider per coll
	goto subloop;
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
			     "%" PRId64"ms "
			     "size=%" PRId32" bytes",
			     took,m_list.getListSize());
	}

	bool bail = false;
	// bail instantly if in read-only mode (no RdbTrees!)
	if ( g_conf.m_readOnlyMode ) bail = true;
	// or if doing a daily merge
	if ( g_dailyMerge.m_mergeMode ) bail = true;
	// skip if too many udp slots being used
	if (g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) bail =true;
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

		return true;
	}

	time_t nowGlobal = getTimeGlobal();

	// reset ptr to point to first rec in list
	m_list.resetListPtr();

 listLoop:

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
		g_process.shutdownAbort(true);
	}

	// if its negative inc by two then! this fixes the bug where the
	// list consisted only of one negative key and was spinning forever
	if ( (m_sc->m_nextDoledbKey & 0x01) == 0x00 )
		m_sc->m_nextDoledbKey += 2;

	// did it hit zero? that means it wrapped around!
	if ( m_sc->m_nextDoledbKey.n1 == 0x0 &&
	     m_sc->m_nextDoledbKey.n0 == 0x0 ) {
		// TODO: work this out
		g_process.shutdownAbort(true);
	}

	// get priority from doledb key
	int32_t pri = g_doledb.getPriority ( doledbKey );

	// if the key went out of its priority because its priority had no
	// spider requests then it will bleed over into another priority so
	// in that case reset it to the top of its priority for next time
	int32_t pri3 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
	if ( pri3 != m_sc->m_pri2 ) {
		m_sc->m_nextDoledbKey = g_doledb.makeFirstKey2 ( m_sc->m_pri2);
	}

	if ( g_conf.m_logDebugSpider ) {
		int32_t pri4 = g_doledb.getPriority ( &m_sc->m_nextDoledbKey );
		log( LOG_DEBUG, "spider: setting pri2=%" PRId32" queue doledb nextkey to %s (pri=%" PRId32")",
		     m_sc->m_pri2, KEYSTR(&m_sc->m_nextDoledbKey,12), pri4 );
	}

	// update next doledbkey for this priority to avoid having to
	// process excessive positive/negative key annihilations (mdw)
	m_sc->m_nextKeys [ m_sc->m_pri2 ] = m_sc->m_nextDoledbKey;

	// sanity
	if ( pri < 0 || pri >= MAX_SPIDER_PRIORITIES ) { g_process.shutdownAbort(true); }

	// skip the priority if we already have enough spiders on it
	int32_t out = m_sc->m_outstandingSpiders[pri];

	// how many spiders can we have out?
	int32_t max = 0;

	// in milliseconds. ho wint32_t to wait between downloads from same IP.
	// only for parnent urls, not including child docs like robots.txt
	// iframe contents, etc.
	int32_t sameIpWaitTime = 5000; // ms
	int32_t maxSpidersOutPerIp = 1;
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != pri ) {
			continue;
		}

		if ( cr->m_maxSpidersPerRule[i] > max ) {
			max = cr->m_maxSpidersPerRule[i];
		}

		if ( cr->m_spiderIpWaits[i] < sameIpWaitTime ) {
			sameIpWaitTime = cr->m_spiderIpWaits[i];
		}

		if ( cr->m_spiderIpMaxSpiders[i] > maxSpidersOutPerIp ) {
			maxSpidersOutPerIp = cr->m_spiderIpMaxSpiders[i];
		}
	}

	// skip? and re-get another doledb list from next priority...
	if ( out >= max ) {
		// assume we could have launched a spider
		if ( max > 0 ) {
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		}

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
	if ( m_list.getCurrentRecSize() <= 16 ) { g_process.shutdownAbort(true); }

	int32_t ipOut = 0;
	int32_t globalOut = 0;

	// get the "spider rec" (SpiderRequest) (embedded in the doledb rec)
	SpiderRequest *sreq = (SpiderRequest *)(rec + sizeof(key_t)+4);

	// sanity check. check for http(s)://
	// might be a docid from a pagereindex.cpp
	if ( sreq->m_url[0] != 'h' && ! is_digit(sreq->m_url[0]) ) {
		// note it
		if ( (g_corruptCount % 1000) == 0 ) {
			log( "spider: got corrupt doledb record. ignoring. pls fix!!!" );
		}
		g_corruptCount++;

		goto skipDoledbRec;
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
		if ( maxSpidersOutPerIp > 0 ) {
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		}

skipDoledbRec:
		// skip
		m_list.skipCurrentRecord();

		// if not exhausted try the next doledb rec in this list
		if ( ! m_list.isExhausted() ) {
			goto listLoop;
		}

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
		if ( maxSpidersOutPerIp > 0 ) {
			ci->m_lastSpiderCouldLaunch = nowGlobal;
		}
		goto skipDoledbRec;
	}

	if ( g_conf.m_logDebugSpider ) {
		log( LOG_DEBUG, "spider: %" PRId32" spiders out for %s for %s", ipOut, iptoa( sreq->m_firstIp ), sreq->m_url );
	}

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
	if ( slot >= 0 ) {
		// get the corresponding lock then if there
		lock = (UrlLock *) ht->getValueFromSlot( slot );
	}

	// if there and confirmed, why still in doledb?
	if ( lock && lock->m_confirmed ) {
		// fight log spam
		static int32_t s_lastTime = 0;
		if ( nowGlobal - s_lastTime >= 2 ) {
			// why is it not getting unlocked!?!?!
			log( "spider: spider request locked but still in doledb. uh48=%" PRId64" firstip=%s %s",
			     sreq->getUrlHash48(), iptoa(sreq->m_firstIp), sreq->m_url );
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

			// try returning true now that we skipped to
			// the next priority level to avoid the infinite
			// loop as described above.
			return true;
		}
		// try the next record in this list
		goto listLoop;
	}

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		logf( LOG_DEBUG, "spider: trying to spider url %s", sreq->m_url );
	}

	// assume we launch the spider below. really this timestamp indicates
	// the last time we COULD HAVE LAUNCHED *OR* did actually launch
	// a spider
	ci->m_lastSpiderCouldLaunch = nowGlobal;

	// if we thought we were done, note it if something comes back up
	if ( ! ci->m_hasUrlsReadyToSpider ) 
		log("spider: got a reviving url for coll %s (%" PRId32") to crawl %s",
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

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

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
	bool status = spiderUrl9 ( sreq, doledbKey, collnum, sameIpWaitTime, maxSpidersOutPerIp ) ;

	// just increment then i guess
	m_list.skipCurrentRecord();

	// if it blocked, wait for it to return to resume the doledb list 
	// processing because the msg12 is out and we gotta wait for it to 
	// come back. when lock reply comes back it tries to spider the url
	// then it tries to call spiderDoledUrls() to keep the spider queue
	// spidering fully.
	if ( ! status ) {
		return false;
	}

	// if exhausted -- try another load with m_nextKey set
	if ( m_list.isExhausted() ) {
		// if no more in list, fix the next doledbkey,
		// m_sc->m_nextDoledbKey
		log ( LOG_DEBUG, "spider: list exhausted." );
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
	//if ( ! sreq->m_doled ) { g_process.shutdownAbort(true); }

	// sanity
	if ( ! m_sc ) { g_process.shutdownAbort(true); }

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
			g_udpServer.cancel ( &xd->m_msg13 , msg_type_13 );
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
			    "only have %" PRId64". Failed to launch %" PRId32" times so "
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
			//g_process.shutdownAbort(true); }
		}
		// keep chugging
		continue;
	}

	// reset g_errno
	g_errno = 0;
	// breathe
	QUICKPOLL(MAX_NICENESS);

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
	//	log("spider: already have lock for lockKey=%" PRIu64,lockKey);
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
		log("spider: deleting doledb tree node %" PRId32,node);

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
		const char *msg = "FAILED TO ADD TO WAITING TREE";
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
		log("spider: adding lock uh48=%" PRId64" lockkey=%" PRId64,
		    m_sreq->getUrlHash48(),lockKeyUh48);
	if ( ! ht->addKey ( &lockKeyUh48 , &tmp ) )
		return true;

	// now do it. this returns false if it would block, returns true if it
	// would not block. sets g_errno on error. it spiders m_sreq.
	return spiderUrl2 ( );
}



bool SpiderLoop::spiderUrl2 ( ) {

	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
		
	// sanity check
	//if ( ! m_sreq->m_doled ) { g_process.shutdownAbort(true); }

	// . find an available doc slot
	// . we can have up to MAX_SPIDERS spiders (300)
	int32_t i;
	for ( i=0 ; i<MAX_SPIDERS ; i++ ) if (! m_docs[i]) break;

	// breathe
	QUICKPOLL(MAX_NICENESS);

	// come back later if we're full
	if ( i >= MAX_SPIDERS ) {
		log(LOG_DEBUG,"build: Already have %" PRId32" outstanding spiders.",
		    (int32_t)MAX_SPIDERS);
		g_process.shutdownAbort(true);
	}

	// breathe
	QUICKPOLL(MAX_NICENESS);

	XmlDoc *xd;
	// otherwise, make a new one if we have to
	try { xd = new (XmlDoc); }
	// bail on failure, sleep and try again
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("build: Could not allocate %" PRId32" bytes to spider "
		    "the url %s. Will retry later.",
		    (int32_t)sizeof(XmlDoc),  m_sreq->m_url );
		    
		logTrace( g_conf.m_logTraceSpider, "END, new XmlDoc failed" );
		return true;
	}
	// register it's mem usage with Mem.cpp class
	mnew ( xd , sizeof(XmlDoc) , "XmlDoc" );
	// add to the array
	m_docs [ i ] = xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	const char *coll = "collnumwasinvalid";
	if ( cr ) coll = cr->m_coll;

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
	//	if ( ! sreq2 && pdocid == 0LL ) { g_process.shutdownAbort(true); }
	//	// for now core on this
	//	if ( ! sreq2 ) {  g_process.shutdownAbort(true); }
	//	// log it
	//	logf(LOG_DEBUG,"spider: spidering uh48=%" PRIu64" pdocid=%" PRIu64,
	//	     uh48,pdocid);
	//}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: spidering firstip9=%s(%" PRIu32") "
		     "uh48=%" PRIu64" prntdocid=%" PRIu64" k.n1=%" PRIu64" k.n0=%" PRIu64,
		     iptoa(m_sreq->m_firstIp),
		     (uint32_t)m_sreq->m_firstIp,
		     m_sreq->getUrlHash48(),
		     m_sreq->getParentDocId() ,
		     m_sreq->m_key.n1,
		     m_sreq->m_key.n0);

	// this returns false and sets g_errno on error
	if (!xd->set4(m_sreq, m_doledbKey, coll, NULL, MAX_NICENESS)) {
		// i guess m_coll is no longer valid?
		mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
		delete (m_docs[i]);
		m_docs[i] = NULL;
		// error, g_errno should be set!
		logTrace( g_conf.m_logTraceSpider, "END, xd->set4 returned false" );
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

	// sanity check
	if (m_sreq->m_priority <= -1 ) { 
		log("spider: fixing bogus spider req priority of %i for "
		    "url %s",
		    (int)m_sreq->m_priority,m_sreq->m_url);
		m_sreq->m_priority = 0;
		//g_process.shutdownAbort(true); 
	}
	//if (m_sreq->m_priority >= MAX_SPIDER_PRIORITIES){g_process.shutdownAbort(true);}
	// update this
	m_sc->m_outstandingSpiders[(unsigned char)m_sreq->m_priority]++;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: sc_out=%" PRId32" waiting=%" PRId32" url=%s",
		    m_sc->m_spidersOut,
		    m_sc->m_waitingTree.m_numUsedNodes,
		    m_sreq->m_url);


	// debug log
	//log("XXX: incremented count to %" PRId32" for %s",
	//    m_sc->m_spidersOut,m_sreq->m_url);
	//if ( m_sc->m_spidersOut != m_numSpidersOut ) { g_process.shutdownAbort(true); }

	// . return if this blocked
	// . no, launch another spider!
	logTrace( g_conf.m_logTraceSpider, "calling xd->indexDoc" );
	bool status = xd->indexDoc();
	logTrace( g_conf.m_logTraceSpider, "indexDoc status [%s]" , status?"true":"false");

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
		logTrace( g_conf.m_logTraceSpider, "END, indexDoc blocked" );
		return false;
	}

	// deal with this error
	indexedDoc ( xd );

	// "callback" will not be called cuz it should be NULL
	logTrace( g_conf.m_logTraceSpider, "END, return true" );
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
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
	
	// save the error in case a call changes it below
	//int32_t saved = g_errno;

	// get our doc #, i
	//int32_t i = doc - m_docs[0];
	int32_t i = 0;
	for ( ; i < MAX_SPIDERS ; i++ ) if ( m_docs[i] == xd) break;
	// sanity check
	if ( i >= MAX_SPIDERS ) { g_process.shutdownAbort(true); }
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
	//log("XXX: decremented count to %" PRId32" for %s",
	//    sc->m_spidersOut,sreq->m_url);
	//if ( sc->m_spidersOut != m_numSpidersOut ) { g_process.shutdownAbort(true); }

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
		log("spider: spidering %s has error: %s. uh48=%" PRId64". "
		    //"Respidering "
		    //"in %" PRId32" seconds. MAX_LOCK_AGE when lock expires. "
		    "cn=%" PRId32,
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
	//if ( ! m_uh48 ) { g_process.shutdownAbort(true); }

	// the lock we had in g_spiderLoop.m_lockTable for the doleKey
	// is now remove in Rdb.cpp when it receives a negative dole key to
	// add to doledb... assuming we added that meta list!!
	// m_uh48 should be set from above
	//if ( ! removeAllLocks () ) return false;

	// we did not block, so return true
	logTrace( g_conf.m_logTraceSpider, "END" );
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
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

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

		// . if doing respider with roundstarttime....
		if ( nowGlobal < cr->m_spiderRoundStartTime ) {
			active = false;
		}

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
	
	logTrace( g_conf.m_logTraceSpider, "END" );
}

// hostId is the remote hostid sending us the lock request
void removeExpiredLocks ( int32_t hostId ) {
	// when we last cleaned them out
	static time_t s_lastTime = 0;

	int32_t nowGlobal = getTimeGlobalNoCore();

	// only do this once per second at the most
	if ( nowGlobal <= s_lastTime ) return;

	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

	restart:

	// scan the slots
	int32_t ns = ht->m_numSlots;
	// . clean out expired locks...
	// . if lock was there and m_expired is up, then nuke it!
	// . when Rdb.cpp receives the "fake" title rec it removes the
	//   lock, only it just sets the m_expired to a few seconds in the
	//   future to give the negative doledb key time to be absorbed.
	//   that way we don't repeat the same url we just got done spidering.
	// . this happens when we launch our lock request on a url that we
	//   or a twin is spidering or has just finished spidering, and
	//   we get the lock, but we avoided the negative doledb key.
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// if collnum got deleted or reset
		collnum_t collnum = lock->m_collnum;
		if ( collnum >= g_collectiondb.m_numRecs ||
		     ! g_collectiondb.m_recs[collnum] ) {
			log("spider: removing lock from missing collnum "
					    "%" PRId32,(int32_t)collnum);
			goto nuke;
		}
		// skip if not yet expired
		if ( lock->m_expires == 0 ) continue;
		if ( lock->m_expires >= nowGlobal ) continue;
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock after waiting. elapsed=%" PRId32"."
					    " lockKey=%" PRIu64" hid=%" PRId32" expires=%" PRIu32" "
					    "nowGlobal=%" PRIu32,
			    (nowGlobal - lock->m_timestamp),
			    lockKey,hostId,
			    (uint32_t)lock->m_expires,
			    (uint32_t)nowGlobal);
		nuke:
		// nuke the slot and possibly re-chain
		ht->removeSlot ( i );
		// gotta restart from the top since table may have shrunk
		goto restart;
	}
	// store it
	s_lastTime = nowGlobal;
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

	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

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
						 msg_type_c1 ,
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
	
	logTrace( g_conf.m_logTraceSpider, "Sent %" PRId32" requests, got %" PRId32" replies" , s_requests, s_replies);
	// return false if we blocked awaiting replies
	if ( s_replies < s_requests ) {
		logTrace( g_conf.m_logTraceSpider, "END. requests/replies mismatch" );
		return;
	}

	// how did this happen?
	log("spider: got bogus crawl info replies!");
	s_inUse = false;

	logTrace( g_conf.m_logTraceSpider, "END" );
	return;
}



// . Parms.cpp calls this when it receives our "spiderRoundNum" increment above
// . all hosts should get it at *about* the same time
void spiderRoundIncremented ( CollectionRec *cr ) {

	log("spider: incrementing spider round for coll %s to %" PRId32" (%" PRIu32")",
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
		log( LOG_WARN, "spider: got crawlinfo reply error from host %" PRId32": %s. spidering will be paused.",
		     h->m_hostId,mstrerror(g_errno));
		// just clear it
		g_errno = 0;
		// if never had any reply... can't be valid then
		if ( ! ptr ) s_validReplies--;
	}

	// inc it
	s_replies++;

	if ( s_replies > s_requests ) {
		g_process.shutdownAbort(true);
	}


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
		// get collnum
		collnum_t collnum = (collnum_t)(ptr->m_collnum);

		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) {
			log("spider: updatecrawlinfo collnum %" PRId32" "
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
			log("spider: got all crawlinfo replies. all shards up. spidering back on.");
		s_countsAreValid = true;
	}

	// loop over 
	for ( int32_t x = 0 ; x < g_collectiondb.m_numRecs ; x++ ) {
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
			// get the CrawlInfo for the ith host
			CrawlInfo *stats = &cia[k];
			// point to the stats for that host
			int64_t *ss = (int64_t *)stats;
			int64_t *gs = (int64_t *)gi;
			// add each hosts counts into the global accumulators
			for ( int32_t j = 0 ; j < NUMCRAWLSTATS ; j++ ) {
				*gs = *gs + *ss;
				// crazy stat?
				if ( *ss > 1000000000LL || *ss < -1000000000LL ) {
					log( LOG_WARN, "spider: crazy stats %" PRId64" from host #%" PRId32" coll=%s",
					     *ss, k, cr->m_coll );
				}
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
			// m_spiderStatus was SP_ROUNDDONE...
			cr->m_spiderStatus = SP_INPROGRESS;

			// revival?
			if ( ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider )
				log("spider: reviving crawl %s from host %" PRId32, cr->m_coll,k);
		} // end loop over hosts

		if ( hadUrlsReady &&
		     // and it no longer does now...
		     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
			log(LOG_INFO,
			    "spider: all %" PRId32" hosts report "
			    "%s (%" PRId32") has no "
			    "more urls ready to spider",
			    s_replies,cr->m_coll,(int32_t)cr->m_collnum);
			// set crawl end time
			cr->m_diffbotCrawlEndTime = getTimeGlobalNoCore();
		}

		// crawl notification: reset done alert flag here if cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider is true

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

		// update status
		if ( ! cr->m_spiderStatus || 
		     cr->m_spiderStatus == SP_INPROGRESS ||
		     cr->m_spiderStatus == SP_INITIALIZING )
			cr->m_spiderStatus = SP_ROUNDDONE;

		// only host #0 sends emails
		if ( g_hostdb.m_myHost->m_hostId != 0 )
			continue;

		// crawl notification: send notification here if necessary

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
	// just a single collnum
	if ( slot->m_readBufSize != 1 ) { g_process.shutdownAbort(true); }

	char *req = slot->m_readBuf;

	if ( ! slot->m_host ) {
		log("handc1: no slot->m_host from ip=%s udpport=%i",
		    iptoa(slot->getIp()),(int)slot->getPort());
		g_errno = ENOHOSTS;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__ );
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	SafeBuf replyBuf;

	uint32_t now = (uint32_t)getTimeGlobalNoCore();

	uint64_t nowMS = gettimeofdayInMillisecondsGlobalNoCore();

	//SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
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

		uint32_t spiderDoneTimer = (uint32_t)SPIDER_DONE_TIMER;

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
				    "%" PRId64" ms from now for coll=%s",
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
		// log("spider: sending ci for coll %s to host %" PRId32,
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



