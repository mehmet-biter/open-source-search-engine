#include "SpiderLoop.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderCache.h"
#include "Doledb.h"
#include "UdpSlot.h"
#include "Collectiondb.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "DailyMerge.h"
#include "Process.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "PingServer.h"
#include "ip.h"
#include "Conf.h"
#include "Mem.h"
#include "ScopedLock.h"


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

/////////////////////////
/////////////////////////      SPIDERLOOP
/////////////////////////

// a global class extern'd in .h file
SpiderLoop g_spiderLoop;

SpiderLoop::SpiderLoop ( ) {
	m_crx = NULL;
	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );

	// Coverity
	m_numSpidersOut = 0;
	m_launches = 0;
	m_maxUsed = 0;
	m_sc = NULL;
	m_gettingDoledbList = false;
	m_activeList = NULL;
	m_bookmark = NULL;
	m_activeListValid = false;
	m_activeListCount = 0;
	m_recalcTime = 0;
	m_recalcTimeValid = false;
	m_doleStart = 0;
}

SpiderLoop::~SpiderLoop ( ) {
	reset();
}

// free all doc's
void SpiderLoop::reset() {
	// delete all doc's in use
	for ( int32_t i = 0 ; i < MAX_SPIDERS ; i++ ) {
		if ( m_docs[i] ) {
			mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
			delete (m_docs[i]);
		}
		m_docs[i] = NULL;
	}
	m_list.freeList();
	m_lockTable.reset();
	m_winnerListCache.reset();
}

void SpiderLoop::init() {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	m_crx = NULL;
	m_activeListValid = false;
	m_activeList = NULL;
	m_recalcTime = 0;
	m_recalcTimeValid = false;

	// we aren't in the middle of waiting to get a list of SpiderRequests
	m_gettingDoledbList = false;

	// clear array of ptrs to Doc's
	memset ( m_docs , 0 , sizeof(XmlDoc *) * MAX_SPIDERS );
	// . m_maxUsed is the largest i such that m_docs[i] is in use
	// . -1 means there are no used m_docs's
	m_maxUsed = -1;
	m_numSpidersOut = 0;

	// for locking. key size is 8 for easier debugging
	m_lockTable.set ( 8,sizeof(UrlLock),0,NULL,0,false, "splocks", true ); // useKeyMagic? yes.

	if ( ! m_winnerListCache.init ( 20000000 , // maxcachemem, 20MB
					-1     , // fixedatasize
					false , // supportlists?
					10000  , // maxcachenodes
					false , // use half keys
					"winnerspidercache", // dbname
					false  ) )
		log(LOG_WARN, "spider: failed to init winnerlist cache. slows down.");

	// don't register callbacks when we're not using it
	if (!g_hostdb.getMyHost()->m_spiderEnabled) {
		logTrace(g_conf.m_logTraceSpider, "END");
		return;
	}

	// sleep for .1 seconds = 100ms
	if (!g_loop.registerSleepCallback(50,this,doneSleepingWrapperSL))
	{
		log(LOG_ERROR, "build: Failed to register timer callback. Spidering is permanently disabled. Restart to fix.");
	}

	logTrace( g_conf.m_logTraceSpider, "END" );
}



// call this every 50ms it seems to try to spider urls and populate doledb
// from the waiting tree
void SpiderLoop::doneSleepingWrapperSL ( int fd , void *state ) {
	// if spidering disabled then do not do this crap
	if ( ! g_conf.m_spideringEnabled )  return;
	if ( ! g_hostdb.getMyHost( )->m_spiderEnabled ) return;

	// or if trying to exit
	if (g_process.isShuttingDown()) return;
	// skip if udp table is full
	if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) return;

	int32_t now = getTimeLocal();

	// point to head of active linked list of collection recs
	CollectionRec *nextActive = g_spiderLoop.getActiveList(); 
	collnum_t nextActiveCollnum = nextActive ? nextActive->m_collnum : static_cast<collnum_t>( -1 );

	for ( ; nextActive ;  ) {
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
		if ( ! sc->m_waitingTreeNeedsRebuild && now - sc->getLastScanTime() > 24*3600 ) {
			// if a scan is ongoing, this will re-set it
			sc->resetWaitingTreeNextKey();
			sc->m_waitingTreeNeedsRebuild = true;
			log( LOG_INFO, "spider: hit spider queue rebuild timeout for %s (%" PRId32")",
			     crp->m_coll, (int32_t)crp->m_collnum );
		}

		if (sc->m_waitingTreeNeedsRebuild) {
			// re-entry is false because we are entering for the first time
			logTrace(g_conf.m_logTraceSpider, "Calling populateWaitingTreeFromSpiderdb");
			sc->populateWaitingTreeFromSpiderdb(false);
		}

		logTrace( g_conf.m_logTraceSpider, "Calling populateDoledbFromWaitingTree" );
		sc->populateDoledbFromWaitingTree ( );
	}

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


void SpiderLoop::gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// process the doledb list
	g_spiderLoop.gotDoledbList2();
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
	if (g_process.isShuttingDown()) {
		logTrace( g_conf.m_logTraceSpider, "END, shutting down"  );
		return;	
	}
	
	// don't spider if we have dead host
	time_t now = time(NULL);
	static bool s_hasDeadHost = g_hostdb.hasDeadHost();
	static time_t s_updatedTime = now;

	if ((now - s_updatedTime) >= g_conf.m_spiderDeadHostCheckInterval) {
		s_updatedTime = now;
		s_hasDeadHost = g_hostdb.hasDeadHost();
	}

	if (s_hasDeadHost) {
		logTrace(g_conf.m_logTraceSpider, "END, has dead host");
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
	if ( g_collectiondb.getNumRecs() <= 0 ) {
		logTrace( g_conf.m_logTraceSpider, "END, no collections"  );
		return;
	}

	// not while repairing
	if ( g_repairMode ) {
		logTrace( g_conf.m_logTraceSpider, "END, in repair mode"  );
		return;
	}
		
	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.inSyncWithHost0() ) {
		logTrace( g_conf.m_logTraceSpider, "END, not in sync with host#0"  );
		return;
	}
	
	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( ! g_pingServer.hostsConfInAgreement() ) {
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
	// skip if gone
    if ( ! cr ) goto subloop;

	// stop if not enabled
	if ( ! cr->m_spideringEnabled ) goto subloop;

	// set current time, synced with host #0
	nowGlobal = (uint32_t)getTimeGlobal();

	// get max spiders
	int32_t maxSpiders = cr->m_maxNumSpiders;

	logTrace( g_conf.m_logTraceSpider, "maxSpiders: %" PRId32 , maxSpiders );

	// obey max spiders per collection too
	if ( m_sc->m_spidersOut >= maxSpiders ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, Too many spiders active for collection"  );
		goto subloop;
	}

	// shortcut
	SpiderColl *sc = cr->m_spiderColl;

	if ( sc && sc->isDoledbIpTableEmpty() ) {
		logTrace( g_conf.m_logTraceSpider, "Loop, doleIpTable is empty"  );
		goto subloop;
	}

	// sanity check
	if ( nowGlobal == 0 ) { g_process.shutdownAbort(true); }

	// need this for msg5 call
	key96_t endKey;
	endKey.setMax();

	for ( ; ; ) {
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
		m_doleStart = gettimeofdayInMilliseconds();

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
			        -1,               // syncPoint
			        false,            // isRealMerge
			        true))            // allowPageCache
	{
		// return if it blocked
		logTrace( g_conf.m_logTraceSpider, "END, getList blocked" );

		return;
	}

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

	// try another read
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

	// update m_msg5StartKey for next read
	if ( m_list.getListSize() > 0 ) {
		// what is m_list.m_ks ?
		m_list.getLastKey((char *)&m_sc->m_msg5StartKey);
		m_sc->m_msg5StartKey += 1;
	}

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		int64_t now = gettimeofdayInMilliseconds();
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
		// return false to indicate to try another
		return false;
	}

	// bail if list is empty
	if ( m_list.getListSize() <= 0 ) {
		return true;
	}

	time_t nowGlobal = getTimeGlobal();

	// reset ptr to point to first rec in list
	m_list.resetListPtr();

 listLoop:
	// get the current rec from list ptr
	char *rec = (char *)m_list.getCurrentRec();

	// the doledbkey
	key96_t *doledbKey = (key96_t *)rec;

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
	int32_t pri = Doledb::getPriority ( doledbKey );

	// if the key went out of its priority because its priority had no
	// spider requests then it will bleed over into another priority so
	// in that case reset it to the top of its priority for next time
	int32_t pri3 = Doledb::getPriority ( &m_sc->m_nextDoledbKey );
	if ( pri3 != m_sc->m_pri2 ) {
		m_sc->m_nextDoledbKey = Doledb::makeFirstKey2 ( m_sc->m_pri2);
	}

	if ( g_conf.m_logDebugSpider ) {
		int32_t pri4 = Doledb::getPriority ( &m_sc->m_nextDoledbKey );
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

	// in milliseconds. how long to wait between downloads from same IP.
	// only for parnent urls, not including child docs like robots.txt
	// iframe contents, etc.
	int32_t maxSpidersOutPerIp = 1;
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		if ( cr->m_spiderPriorities[i] != pri ) {
			continue;
		}

		if ( cr->m_maxSpidersPerRule[i] > max ) {
			max = cr->m_maxSpidersPerRule[i];
		}

		if ( cr->m_spiderIpMaxSpiders[i] > maxSpidersOutPerIp ) {
			maxSpidersOutPerIp = cr->m_spiderIpMaxSpiders[i];
		}
	}

	// skip? and re-get another doledb list from next priority...
	if ( out >= max ) {
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
	SpiderRequest *sreq = (SpiderRequest *)(rec + sizeof(key96_t)+4);

	// sanity check. check for http(s)://
	// might be a docid from a pagereindex.cpp
	if ( sreq->m_url[0] != 'h' && ! is_digit(sreq->m_url[0]) ) {
		log(LOG_WARN, "spider: got corrupt doledb record. ignoring. pls fix!!!" );

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
skipDoledbRec:
		// skip
		m_list.skipCurrentRecord();

		// if not exhausted try the next doledb rec in this list
		if ( ! m_list.isExhausted() ) {
			goto listLoop;
		}

		// print a log msg if we corked things up even
		// though we read 50k from doledb
		if ( m_list.getListSize() > 50000 ) {
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
		goto skipDoledbRec;
	}

	logDebug( g_conf.m_logDebugSpider, "spider: %" PRId32" spiders out for %s for %s", ipOut, iptoa( sreq->m_firstIp ), sreq->m_url );

	// sometimes we have it locked, but is still in doledb i guess.
	// seems like we might have give the lock to someone else and
	// there confirmation has not come through yet, so it's still
	// in doledb.

	{
		ScopedLock sl(m_lockTableMtx);

		// get the lock... only avoid if confirmed!
		int64_t lockKey = makeLockTableKey(sreq);
		int32_t slot = m_lockTable.getSlot(&lockKey);
		if (slot >= 0) {
			// get the corresponding lock then if there
			UrlLock *lock = (UrlLock *)m_lockTable.getValueFromSlot(slot);

			// if there and confirmed, why still in doledb?
			if (lock) {
				// fight log spam
				static int32_t s_lastTime = 0;
				if (nowGlobal - s_lastTime >= 2) {
					// why is it not getting unlocked!?!?!
					log("spider: spider request locked but still in doledb. uh48=%" PRId64" firstip=%s %s",
					    sreq->getUrlHash48(), iptoa(sreq->m_firstIp), sreq->m_url);
					s_lastTime = nowGlobal;
				}

				// just increment then i guess
				m_list.skipCurrentRecord();

				// let's return false here to avoid an infinite loop
				// since we are not advancing nextkey and m_pri is not
				// being changed, that is what happens!
				if (m_list.isExhausted()) {
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
		}
	}

	// log this now
	if ( g_conf.m_logDebugSpider ) {
		logf( LOG_DEBUG, "spider: trying to spider url %s", sreq->m_url );
	}

	// reset reason why crawl is not running, because we basically are now
	cr->m_spiderStatus = SP_INPROGRESS; // this is 7

	// be sure to save state so we do not re-send emails
	cr->setNeedsSave();

	// sometimes the spider coll is reset/deleted while we are
	// trying to get the lock in spiderUrl() so let's use collnum
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
	bool status = spiderUrl(sreq, doledbKey, collnum);

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
}



// . spider the next url that needs it the most
// . returns false if blocked on a spider launch, otherwise true.
// . returns false if your callback will be called
// . returns true and sets g_errno on error
bool SpiderLoop::spiderUrl(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum) {
	// sanity
	if ( ! m_sc ) { g_process.shutdownAbort(true); }

	// wait until our clock is synced with host #0 before spidering since
	// we store time stamps in the domain and ip wait tables in 
	// SpiderCache.cpp. We don't want to freeze domain for a long time
	// because we think we have to wait until tomorrow before we can
	// spider it.

	// turned off?
	if ( ( (! g_conf.m_spideringEnabled ||
		// or if trying to exit
		g_process.isShuttingDown()
		) && ! sreq->m_isInjecting ) ||
	     // repairing the collection's rdbs?
	     g_repairMode ) {
		// try to cancel outstanding spiders, ignore injects
		for ( int32_t i = 0 ; i <= m_maxUsed ; i++ ) {
			// get it
			XmlDoc *xd = m_docs[i];
			if ( ! xd                      ) continue;
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
		return true; 
	}
	// do not launch another spider if less than 25MB of memory available.
	// this causes us to dead lock when spiders use up all the mem, and
	// file merge operation can not get any, and spiders need to add to 
	// titledb but can not until the merge completes!!
	int64_t freeMem = g_mem.getFreeMem();
	if (freeMem < 25*1024*1024 ) {
		static int32_t s_lastTime = 0;
		static int32_t s_missed   = 0;
		s_missed++;
		int32_t now = getTime();
		// don't spam the log, bug let people know about it
		if ( now - s_lastTime > 10 ) {
			log("spider: Need 25MB of free mem to launch spider, "
			    "only have %" PRId64". Failed to launch %" PRId32" times so "
			    "far.", freeMem , s_missed );
			s_lastTime = now;
		}
	}

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

	logDebug(g_conf.m_logDebugSpider, "spider: deleting doledb tree key=%s", KEYSTR(doledbKey, sizeof(*doledbKey)));

	// now we just take it out of doledb instantly
	bool deleted = g_doledb.getRdb()->deleteTreeNode(collnum, (const char *)doledbKey);

	// if url filters rebuilt then doledb gets reset and i've seen us hit
	// this node == -1 condition here... so maybe ignore it... just log
	// what happened? i think we did a quickpoll somewhere between here
	// and the call to spiderDoledUrls() and it the url filters changed
	// so it reset doledb's tree. so in that case we should bail on this
	// url.
	if (!deleted) {
		g_errno = EADMININTERFERENCE;
		log("spider: lost url about to spider from url filters "
		    "and doledb tree reset. %s",mstrerror(g_errno));
		return true;
	}


	// now remove from doleiptable since we removed from doledb
	m_sc->removeFromDoledbIpTable(sreq->m_firstIp);

	// DO NOT add back to waiting tree if max spiders
	// out per ip was 1 OR there was a crawldelay. but better
	// yet, take care of that in the winReq code above.

	// . now add to waiting tree so we add another spiderdb
	//   record for this firstip to doledb
	// . true = callForScan
	// . do not add to waiting tree if we have enough outstanding
	//   spiders for this ip. we will add to waiting tree when
	//   we receive a SpiderReply in addSpiderReply()
	if (
	     // this will just return true if we are not the 
	     // responsible host for this firstip
	     ! m_sc->addToWaitingTree(sreq->m_firstIp) &&
	     // must be an error...
	     g_errno ) {
		const char *msg = "FAILED TO ADD TO WAITING TREE";
		log("spider: %s %s",msg,mstrerror(g_errno));
		//us->sendErrorReply ( udpSlot , g_errno );
		//return;
	}

	int64_t lockKeyUh48 = makeLockTableKey ( sreq );

	logDebug(g_conf.m_logDebugSpider, "spider: adding lock uh48=%" PRId64" lockkey=%" PRId64,
	         sreq->getUrlHash48(),lockKeyUh48);

	// . add it to lock table to avoid respider, removing from doledb
	//   is not enough because we re-add to doledb right away
	// . return true on error here
	UrlLock tmp;
	tmp.m_firstIp = sreq->m_firstIp;
	tmp.m_spiderOutstanding = 0;
	tmp.m_collnum = collnum;

	if (!addLock(lockKeyUh48, &tmp)) {
		return true;
	}

	// now do it. this returns false if it would block, returns true if it
	// would not block. sets g_errno on error. it spiders m_sreq.
	return spiderUrl2(sreq, doledbKey, collnum);
}

bool SpiderLoop::spiderUrl2(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	// . find an available doc slot
	// . we can have up to MAX_SPIDERS spiders (300)
	int32_t i;
	for ( i=0 ; i<MAX_SPIDERS ; i++ ) if (! m_docs[i]) break;

	// come back later if we're full
	if ( i >= MAX_SPIDERS ) {
		log(LOG_DEBUG,"build: Already have %" PRId32" outstanding spiders.",
		    (int32_t)MAX_SPIDERS);
		g_process.shutdownAbort(true);
	}

	XmlDoc *xd;
	// otherwise, make a new one if we have to
	try { xd = new (XmlDoc); }
	// bail on failure, sleep and try again
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("build: Could not allocate %" PRId32" bytes to spider "
		    "the url %s. Will retry later.",
		    (int32_t)sizeof(XmlDoc),  sreq->m_url );
		    
		logTrace( g_conf.m_logTraceSpider, "END, new XmlDoc failed" );
		return true;
	}
	// register it's mem usage with Mem.cpp class
	mnew ( xd , sizeof(XmlDoc) , "XmlDoc" );
	// add to the array
	m_docs [ i ] = xd;

	CollectionRec *cr = g_collectiondb.getRec(collnum);
	const char *coll = "collnumwasinvalid";
	if ( cr ) coll = cr->m_coll;

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: spidering firstip9=%s(%" PRIu32") "
		     "uh48=%" PRIu64" prntdocid=%" PRIu64" k.n1=%" PRIu64" k.n0=%" PRIu64,
		     iptoa(sreq->m_firstIp),
		     (uint32_t)sreq->m_firstIp,
		     sreq->getUrlHash48(),
		     sreq->getParentDocId() ,
		     sreq->m_key.n1,
		     sreq->m_key.n0);

	// this returns false and sets g_errno on error
	if (!xd->set4(sreq, doledbKey, coll, NULL, MAX_NICENESS)) {
		// i guess m_coll is no longer valid?
		mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
		delete (m_docs[i]);
		m_docs[i] = NULL;
		// error, g_errno should be set!
		logTrace( g_conf.m_logTraceSpider, "END, xd->set4 returned false" );
		return true;
	}

	// call this after doc gets indexed
	xd->setCallback ( xd  , indexedDocWrapper );

	// increase m_maxUsed if we have to
	if ( i > m_maxUsed ) m_maxUsed = i;
	// count it
	m_numSpidersOut++;
	// count this
	m_sc->m_spidersOut++;

	m_launches++;

	// sanity check
	if (sreq->m_priority <= -1 ) {
		log("spider: fixing bogus spider req priority of %i for "
		    "url %s",
		    (int)sreq->m_priority,sreq->m_url);
		sreq->m_priority = 0;
		//g_process.shutdownAbort(true); 
	}

	// update this
	m_sc->m_outstandingSpiders[(unsigned char)sreq->m_priority]++;

	if ( g_conf.m_logDebugSpider )
		log(LOG_DEBUG,"spider: sc_out=%" PRId32" waiting=%" PRId32" url=%s",
		    m_sc->m_spidersOut,
		    m_sc->m_waitingTree.getNumUsedNodes(),
			sreq->m_url);

	// . return if this blocked
	// . no, launch another spider!
	logTrace( g_conf.m_logTraceSpider, "calling xd->indexDoc" );
	bool status = xd->indexDoc();
	logTrace( g_conf.m_logTraceSpider, "indexDoc status [%s]" , status?"true":"false");

	// if we were injecting and it blocked... return false
	if ( ! status ) {
		logTrace( g_conf.m_logTraceSpider, "END, indexDoc blocked" );
		return false;
	}

	// deal with this error
	indexedDoc ( xd );

	// "callback" will not be called cuz it should be NULL
	logTrace( g_conf.m_logTraceSpider, "END, return true" );
	return true;
}

void SpiderLoop::indexedDocWrapper ( void *state ) {
	// . process the results
	// . return if this blocks
	if ( ! g_spiderLoop.indexedDoc ( (XmlDoc *)state ) ) return;
}



// . this will delete m_docs[i]
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool SpiderLoop::indexedDoc ( XmlDoc *xd ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	// get our doc #, i
	int32_t i = 0;
	for ( ; i < MAX_SPIDERS ; i++ ) if ( m_docs[i] == xd) break;
	// sanity check
	if ( i >= MAX_SPIDERS ) { g_process.shutdownAbort(true); }

	// . decrease m_maxUsed if we need to
	// . we can decrease all the way to -1, which means no spiders going on
	if ( m_maxUsed == i ) {
		m_maxUsed--;
		while ( m_maxUsed >= 0 && ! m_docs[m_maxUsed] ) m_maxUsed--;
	}
	// count it
	m_numSpidersOut--;

	// get coll
	collnum_t collnum = xd->m_collnum;
	// if coll was deleted while spidering, sc will be NULL
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);
	// decrement this
	if ( sc ) sc->m_spidersOut--;
	// get the original request from xmldoc
	SpiderRequest *sreq = &xd->m_sreq;
	// update this. 
	if ( sc ) sc->m_outstandingSpiders[(unsigned char)sreq->m_priority]--;

	// note it
	// this should not happen any more since indexDoc() will take
	// care of g_errno now by clearing it and adding an error spider
	// reply to release the lock!!
	if ( g_errno ) {
		log("spider: spidering %s has error: %s. uh48=%" PRId64". "
		    "cn=%" PRId32,
		    xd->m_firstUrl.getUrl(),
		    mstrerror(g_errno),
		    xd->getFirstUrlHash48(),
		    (int32_t)collnum);
		// don't release the lock on it right now. just let the
		// lock expire on it after MAX_LOCK_AGE seconds. then it will
		// be retried. we need to debug gb so these things never
		// hapeen...
	}

	// we don't need this g_errno passed this point
	g_errno = 0;

	// we are responsible for deleting doc now
	mdelete ( m_docs[i] , sizeof(XmlDoc) , "Doc" );
	delete (m_docs[i]);
	m_docs[i] = NULL;

	// we did not block, so return true
	logTrace( g_conf.m_logTraceSpider, "END" );
	return true;
}



// use -1 for any collnum
int32_t SpiderLoop::getNumSpidersOutPerIp(int32_t firstIp, collnum_t collnum) {
	ScopedLock sl(m_lockTableMtx);
	int32_t count = 0;

	// scan the slots
	for (int32_t i = 0; i < m_lockTable.getNumSlots(); i++) {
		// skip if empty
		if (!m_lockTable.m_flags[i]) {
			continue;
		}

		// cast lock
		UrlLock *lock = (UrlLock *)m_lockTable.getValueFromSlot(i);

		// skip if not outstanding, just a 5-second expiration wait
		// when the spiderReply returns, so that in case a lock
		// request for the same url was in progress, it will be denied.
		if (!lock->m_spiderOutstanding) {
			continue;
		}

		// correct collnum?
		if (lock->m_collnum != collnum && collnum != -1) {
			continue;
		}

		// skip if not yet expired
		if (lock->m_firstIp == firstIp) {
			count++;
		}
	}

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
	if (!m_activeListValid) {
		buildActiveList();
		//m_crx = m_activeList;
		// recompute every 3 seconds, it seems kinda buggy!!
		m_recalcTime = nowGlobal + 3;
		m_recalcTimeValid = true;
	}

	return m_activeList;
}



void SpiderLoop::buildActiveList ( ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	// when do we need to rebuild the active list again?
	m_recalcTimeValid = false;

	m_activeListValid = true;

	m_activeListCount = 0;

	// reset the linked list of active collections
	m_activeList = NULL;
	bool found = false;

	CollectionRec *tail = NULL;

	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		// get rec
		CollectionRec *cr = g_collectiondb.getRec(i);
		// skip if gone
		if ( ! cr ) continue;
		// stop if not enabled
		bool active = true;
		if ( ! cr->m_spideringEnabled ) active = false;

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

bool SpiderLoop::isLocked(int64_t key) const {
	ScopedLock sl(m_lockTableMtx);
	return m_lockTable.isInTable(&key);
}

int32_t SpiderLoop::getLockCount() const {
	ScopedLock sl(m_lockTableMtx);
	return m_lockTable.getNumUsedSlots();
}

bool SpiderLoop::addLock(int64_t key, const UrlLock *lock) {
	ScopedLock sl(m_lockTableMtx);
	return m_lockTable.addKey(&key, lock);
}

void SpiderLoop::removeLock(int64_t key) {
	ScopedLock sl(m_lockTableMtx);
	m_lockTable.removeKey(&key);
}

void SpiderLoop::clearLocks(collnum_t collnum) {
	ScopedLock sl(m_lockTableMtx);

	// remove locks from locktable for all spiders out
	for (;;) {
		bool restart = false;

		// scan the slots
		for (int32_t i = 0; i < m_lockTable.getNumSlots(); i++) {
			// skip if empty
			if (!m_lockTable.m_flags[i]) {
				continue;
			}

			UrlLock *lock = (UrlLock *)m_lockTable.getValueFromSlot(i);
			// skip if not our collnum
			if (lock->m_collnum != collnum) {
				continue;
			}

			// nuke it!
			m_lockTable.removeSlot(i);

			// restart since cells may have shifted
			restart = true;
		}

		if (!restart) {
			break;
		}
	}
}
