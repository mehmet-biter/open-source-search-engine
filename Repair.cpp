// Copyright 2007, Gigablast Inc.

#include "gb-include.h"

#include "Repair.h"
#include "Rdb.h"
#include "Spider.h"
#include "Msg1.h"
#include "Pages.h"
#include "PingServer.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderLoop.h"
#include "Process.h"
#include "Tagdb.h"
#include "Sections.h"
#include "Posdb.h"
#include "Clusterdb.h"
#include "Linkdb.h"
#include "XmlDoc.h"
#include "max_niceness.h"


static void repairWrapper ( int fd , void *state ) ;
static void loopWrapper   ( void *state , RdbList *list , Msg5 *msg5 ) ;

static bool saveAllRdbs ( void *state , void (* callback)(void *state) ) ;
static bool anyRdbNeedsSave ( ) ;
static void doneSavingRdb ( void *state );

char g_repairMode = 0;

// the global class
Repair g_repair;

static Rdb **getSecondaryRdbs ( int32_t *nsr ) {
	static Rdb *s_rdbs[50];
	static int32_t s_nsr = 0;
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_nsr = 0;

		s_rdbs[s_nsr++] = g_titledb2.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb2.getRdb    ();
		s_rdbs[s_nsr++] = g_spiderdb2.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb2.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb2.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb2.getRdb      ();
	}
	*nsr = s_nsr;
	return s_rdbs;
}

static Rdb **getAllRdbs ( int32_t *nsr ) {
	static Rdb *s_rdbs[50];
	static int32_t s_nsr = 0;
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_nsr = 0;
		s_rdbs[s_nsr++] = g_titledb.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb.getRdb    ();
		s_rdbs[s_nsr++] = g_spiderdb.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb.getRdb      ();

		s_rdbs[s_nsr++] = g_titledb2.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb2.getRdb    ();
		s_rdbs[s_nsr++] = g_spiderdb2.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb2.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb2.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb2.getRdb      ();
	}
	*nsr = s_nsr;
	return s_rdbs;
}


Repair::Repair() {
	// Coverity
	m_completed = false;
	m_needsCallback = false;
	m_docQuality = 0;
	m_docId = 0;
	m_isDelete = false;
	m_totalMem = 0;
	m_stage = 0;
	m_tfn = 0;
	m_count = 0;
	m_updated = false;
	m_nextTitledbKey = 0;
	m_nextSpiderdbKey = 0;
	m_nextPosdbKey = 0;
	m_nextLinkdbKey = 0;
	m_endKey = 0;
	m_uh48 = 0;
	m_priority = 0;
	m_contentHash = 0;
	m_clusterdbKey = 0;
	m_spiderdbKey = 0;
	memset(m_srBuf, 0, sizeof(m_srBuf));
	memset(m_tmpBuf, 0, sizeof(m_tmpBuf));
	m_chksum1LongLong = 0;
	m_isNew = false;
	m_SAVE_START = 0;
	m_lastDocId = 0;
	m_prevDocId = 0;
	m_completedFirstScan = false;
	m_completedSpiderdbScan = false;
	m_lastTitledbKey = 0;
	m_lastSpiderdbKey = 0;
	m_recsScanned = 0;
	m_recsOutOfOrder = 0;
	m_recsetErrors = 0;
	m_recsCorruptErrors = 0;
	m_recsXmlErrors = 0;
	m_recsDupDocIds = 0;
	m_recsNegativeKeys = 0;
	m_recsOverwritten = 0;
	m_recsUnassigned = 0;
	m_noTitleRecs = 0;
	m_recsWrongGroupId = 0;
	m_recsRoot = 0;
	m_recsNonRoot = 0;
	m_recsInjected = 0;
	m_spiderRecsScanned = 0;
	m_spiderRecSetErrors = 0;
	m_spiderRecNotAssigned = 0;
	m_spiderRecBadTLD = 0;
	m_rebuildTitledb = false;
	m_rebuildPosdb = false;
	m_rebuildClusterdb = false;
	m_rebuildSpiderdb = false;
	m_rebuildSitedb = false;
	m_rebuildLinkdb = false;
	m_rebuildTagdb = false;
	m_fullRebuild = true;
	m_rebuildRoots = true;
	m_rebuildNonRoots = true;
	m_collnum = 0;
	m_newCollLen = 0;
	m_newCollnum = 0;
	m_colli = 0;
	m_numColls = 0;
	m_SAVE_END = 0;
	m_cr=NULL;
	m_startTime = 0;
	m_isSuspended = false;
	m_numOutstandingInjects = 0;
	m_allowInjectToLoop = false;
	m_msg5InUse = false;
	m_saveRepairState = false;
	m_isRetrying = false;
	
	memset(m_newColl, 0, sizeof(m_newColl));
	memset(&m_collOffs, 0, sizeof(m_collOffs));
	memset(&m_collLens, 0, sizeof(m_collLens));
}

// main.cpp calls g_repair.init()
bool Repair::init ( ) {
	//logf(LOG_DEBUG,"repair: TODO: alloc s_docs[] on demand to save mem");
	m_msg5InUse       = false;
	m_isSuspended     = false;
	m_saveRepairState = false;
	m_isRetrying      = false;
	m_needsCallback   = false;
	m_completed       = false;
	if( ! g_loop.registerSleepCallback( 1 , NULL , repairWrapper ) ) {
		log(LOG_WARN, "repair: Failed register callback.");
		return false;
	}
	return true;
}

bool Repair::isRepairActive() { 
	return g_repairMode >= 4; 
}

// . call this once every second 
// . this is responsible for advancing from one g_repairMode to the next
void repairWrapper ( int fd , void *state ) {

	g_errno = 0;

	// . all hosts should have their g_conf.m_repairMode parm set
	// . it is global now, not collection based, since we need to
	//   lock down titledb for the scan and there could be recs from
	//   the collection we are repairing in titledb's rdbtree, which,
	//   when dumped, would mess up our scan.
	if ( ! g_conf.m_repairingEnabled ) return;

	// if the power went off
	if ( ! g_process.m_powerIsOn ) return;

	// if it got turned back on after being suspended, start where
	// we left off, this is how we re-enter Repair::loop()
	if ( g_repair.m_isSuspended && g_repairMode == 4 ) {
		// unsuspend it
		g_repair.m_isSuspended = false;
		// note it
		log("repair: Resuming repair scan after suspension.");
		// try to read another title rec, or whatever
		g_repair.loop();
		return;
	}

	// if we are in retry mode
	if ( g_repair.m_isRetrying && g_repairMode == 4 ) {
		// reset it
		g_repair.m_isRetrying = false;
		// try to read another title rec, or whatever
		g_repair.loop();
		return;
	}

	//
	// ok, repairing is enabled at this point
	//

	// are we just starting?
	if ( g_repairMode == 0 ) {
		// turn spiders off since repairing is enabled
		g_conf.m_spideringEnabled = false;
		//g_conf.m_injectionEnabled = false;
		// wait for a previous repair to finish?
		//if ( g_pingServer.getMinRepairMode() != 0 ) return;
		// if some are not done yet with the previous repair, wait...
		// no because we are trying to load up repair.dat
		//if ( g_pingServer.getMaxRepairMode() == 8 ) return;

		g_repair.m_startTime = gettimeofdayInMilliseconds();
		// enter repair mode level 1
		g_repairMode = 1;
		// note it
		log("repair: Waiting for all writing operations to stop.");
	}

	// we can only enter repairMode 2 once all "writing" has stopped
	if ( g_repairMode == 1 ) {
		// wait for all merging to stop just to be on the safe side
		if ( g_merge.isMerging() ) return;
		// this is >= 0 is correct, -1 means no outstanding spiders
		if ( g_spiderLoop.m_maxUsed >= 0 ) return;
		// wait for ny outstanding unlinks or renames to finish
		if ( g_unlinkRenameThreads > 0 ) return;
		// . make sure all Msg4s are done and have completely added all
		//   recs they were supposed to
		// . PROBLEM: if resuming a repair after re-starting, we can
		//   not turn on repairing
		// . SOLVED: saveState() for msg4 uses different filename
		if ( hasAddsInQueue() ) return;
		// . ok, go to level 2
		// . we can only get to level *3* once PingServer.cpp sees 
		//   that all hosts in the cluster are in level 2. that way we
		//   guarantee not to add or delete any recs from any rdb, 
		//   because that could damage the repair. PingServer will 
		//   call g_repair.allHostsRead() when they all report they 
		//   have a repair mode of 2.
		g_repairMode = 2;
		// note it
		log("repair: All oustanding writing operations stopped. ");
		log("repair: Waiting for all other hosts to stop, too.");
	}

	// we can only enter mode 3 once all hosts are in 2 or higher
	if ( g_repairMode == 2 ) {
		// we are still waiting on some guy if this is <= 1
		if ( g_pingServer.getMinRepairMode() <= 1 ) return;
		// wait for others to sync clocks, lest xmldoc cores when
		// it calls getTimeGlobal() like in getNewTagBuf()
		if ( ! isClockInSync() ) return;
		// . this will return true if everything is saved to disk that
		//   needs to be, otherwise false if waiting on an rdb to finish
		//   saving
		// . do this after all hosts are done writing, otherwise
		//   they might add data to our rdbs!
		if ( ! saveAllRdbs ( NULL , NULL ) ) return;
		// note it
		//log("repair: Initializing the new Rdbs and scan parameters.");
		// reset scan info BEFORE calling Repair::load()
		g_repair.resetForNewCollection();
		// before calling loop for the first time, init the scan,
		// this will block and only return when it is done
		g_repair.initScan();
		// on error this sets g_repairingEnabled to false
		if ( ! g_conf.m_repairingEnabled ) return;
		// save "addsinprogress" file now so that the file will be 
		// saved as essentially an empty file at this point. 
		saveAddsInProgress ( NULL );
		// sanity check
		//g_process.shutdownAbort(true);
		// hey, everyone is done "writing"
		g_repairMode = 3;
		// not eit
		log("repair: All data saved and clock synced.");
		log("repair: Waiting for all hosts to save and sync clocks.");
	}

	if ( g_repairMode == 3 ) {
		// wait for others to save everything
		if ( g_pingServer.getMinRepairMode() <= 2 ) return;
		// start the loop
		log("repair: All hosts saved.");
		log("repair: Loading repair-addsinprogress.dat");
		// . tell Msg4 to load state using the new filename now
		// . load "repair-addsinprogress" file
		loadAddsInProgress ( "repair-" );
		//log("repair: Scanning titledb file #%" PRId32".",  g_repair.m_fn );
		log("repair: Starting repair scan.");
		// advance
		g_repairMode = 4;
		// now start calling the loop. returns false if blocks
		if ( ! g_repair.loop() ) return;
	}

	// we can only enter mode 4 once we have completed the repairs
	// and have dumped all the in-memory data to disk
	if ( g_repairMode == 4 ) {
		// special case
		if ( g_repair.m_needsCallback ) {
			// only do once
			g_repair.m_needsCallback = false;
			// note it in log
			log("repair: calling needed callback for msg4");
			// and call the loop then. returns false if blocks..
			if ( ! g_repair.loop() ) return;
		}
		// wait for scan loops to complete
		if ( ! g_repair.m_completedFirstScan  ) return;
		if ( ! g_repair.m_completedSpiderdbScan ) return;
		// note it
		log("repair: Scan completed.");
		log("repair: Waiting for other hosts to complete scan.");
		// ok, we are ready to update the data files
		g_repairMode = 5;
	}		

	// we can only enter mode 5 once all hosts are in 4 or higher
	if ( g_repairMode == 5 ) {
		// if add queues still adding, wait, otherwise they will not
		// be able to add to our rebuild collection
		if ( hasAddsInQueue() ) return;
		// note it
		log("repair: All adds have been flushed.");
		log("repair: Waiting for all other hosts to flush out their "
		    "add operations.");
		// update repair mode
		g_repairMode = 6;
	}

	if ( g_repairMode == 6 ) {
		// wait for everyone to get to mode 6 before we dump, otherwise
		// data might arrive in the middle of the dumping and it stays
		// in the in-memory RdbTree!
		if ( g_pingServer.getMinRepairMode() < 6 ) return;
		// do not dump if we are doing a full rebuild or a
		// no split list rebuild -- why?
		//if(! g_repair.m_fullRebuild && ! g_repair.m_rebuildNoSplits){
		//if ( ! g_repair.m_rebuildNoSplits ) {
		// we might have to dump again
		g_repair.dumpLoop();
		// are we done dumping?
		if ( ! g_repair.dumpsCompleted() ) return;
		//}
		// wait for all merging to stop just to be on the safe side
		if ( g_merge.isMerging() ) return;
		// wait for ny outstanding unlinks or renames to finish
		if ( g_unlinkRenameThreads > 0 ) return;
		// note it
		log("repair: Final dump completed.");
		log("repair: Updating rdbs to use newly repaired data.");
		// everyone is ready
		g_repairMode = 7;
	}

	// we can only enter mode 6 once we are done updating the original
	// rdbs with the rebuilt/repaired data. we move the old rdb data files
	// into the trash and replace it with the new data.
	if ( g_repairMode == 7 ) {
		// wait for autosave...
		if ( g_process.m_mode ) return; // = SAVE_MODE;		
		// save to disk so it zeroes out indexdbRebuild-saved.dat
		// which should have 0 records in it cuz we dumped it above
		// in g_repair.dumpLoop()
		if ( ! saveAllRdbs ( NULL , NULL ) ) return;
		// . this blocks and gets the job done
		// . this will move the old *.dat and *-saved.dat files into
		//   a subdir in the trash subdir
		// . it will rename the rebuilt files to remove the "Rebuild"
		//   from their filenames
		// . it will then restart the primary rdbs using those newly
		//   rebuilt and renamed files
		// . this will not allow itself to be called more than once
		//   per scan/repair process
		g_repair.updateRdbs();
		// note this
		log("repair: resetting secondary rdbs.");
		// . only do this after indexdbRebuild-saved.dat has had a
		//   chance to save to "zero-out" its file on disk
		// . all done with these guys, free their mem
		g_repair.resetSecondaryRdbs();
		// save "repair-addsinprogress" now so that the file will 
		// be saved as essentially an empty file at this 
		// point. 
		saveAddsInProgress ( "repair-" );
		// reset it again in case it gets saved again later
		g_repair.resetForNewCollection();
		// unlink the repair.dat file, in case we core and are unable
		// to save the freshly-reset repair.dat file
		log("repair: unlinking repair.dat");
		char tmp[1024];
		sprintf ( tmp, "%s/repair.dat", g_hostdb.m_dir );
		::unlink ( tmp );
		// do not save it again! we just unlinked it!!
		g_repair.m_saveRepairState = false;
		// note it
		log("repair: Waiting for other hosts to complete update.");
		// ready to reset
		g_repairMode = 8;
		// mark it
		g_repair.m_completed = true;
	}

	// go back to 0 once all hosts do not equal 5
	if ( g_repairMode == 8 ) {
		// nobody can be in 7 (they might be 0!)
		if ( g_pingServer.getMinRepairModeBesides0() != 8 ) return;

		// note it
		log("repair: Exiting repair mode.  took %" PRId64" ms", 
		    gettimeofdayInMilliseconds() - g_repair.m_startTime);
		// turn it off to prevent going back to mode 1 again
		g_conf.m_repairingEnabled = false;
		// ok reset
		g_repairMode = 0;
	}
}

		
void Repair::resetForNewCollection ( ) {
	m_stage                 = 0;
	m_lastDocId             = 0;
	m_prevDocId             = 0;
	m_completedFirstScan  = false;
	m_completedSpiderdbScan = false;
	//m_completedIndexdbScan  = false;
}

// . PingServer.cpp will call this g_repair.allHostsReady() when all hosts
//   have completely stopped spidering and merging
// . returns false if blocked, true otherwise
//void Repair::allHostsReady () {
void Repair::initScan ( ) {

	// reset some stuff for the titledb scan
	m_nextTitledbKey.setMin();
	m_nextSpiderdbKey.setMin();
	m_lastSpiderdbKey.setMin();
	m_nextPosdbKey.setMin ();
	m_nextLinkdbKey.setMin  ();
	m_endKey.setMax();
	m_titleRecList.reset();
	m_count = 0;

	// all Repair::updateRdbs() to be called
	m_updated = false;

	// titledb scan stats
	m_recsScanned      = 0;
	m_recsNegativeKeys  = 0;
	m_recsOutOfOrder   = 0;
	m_recsetErrors     = 0;
	m_recsCorruptErrors = 0;
	m_recsXmlErrors     = 0;
	m_recsDupDocIds     = 0;
	m_recsOverwritten  = 0;
	m_recsUnassigned   = 0;
	m_recsWrongGroupId = 0;
	m_noTitleRecs = 0;

	m_spiderRecsScanned     = 0;
	m_spiderRecSetErrors    = 0;
	m_spiderRecNotAssigned  = 0;
	m_spiderRecBadTLD       = 0;

	m_rebuildTitledb    = g_conf.m_rebuildTitledb;
	m_rebuildPosdb      = g_conf.m_rebuildPosdb;
	m_rebuildClusterdb  = g_conf.m_rebuildClusterdb;
	m_rebuildSpiderdb   = g_conf.m_rebuildSpiderdb;
	m_rebuildLinkdb     = g_conf.m_rebuildLinkdb;
	m_fullRebuild       = g_conf.m_fullRebuild;

	m_rebuildRoots      = g_conf.m_rebuildRoots;
	m_rebuildNonRoots   = g_conf.m_rebuildNonRoots;

	m_numOutstandingInjects = 0;


	// we call Msg14::injectUrl() directly and that will add to ALL the
	// necessary secondary rdbs automatically
	if ( m_fullRebuild ) {
		// why rebuild titledb? its the base. no we need to
		// rebuild it for new event displays.
		m_rebuildTitledb    = true;
		m_rebuildSpiderdb   = false;
		m_rebuildPosdb    = true;
		m_rebuildClusterdb  = true;
		m_rebuildLinkdb     = true;
	}

	// rebuilding spiderdb means we must rebuild tfndb, too
	if ( m_rebuildSpiderdb ) {
		logf(LOG_DEBUG,"repair: Not rebuilding tfndb like "
		     "we should because it is broken!");
		// TODO: put this back when it is fixed!
		// see the comment in addToTfndb2() below
		// YOU HAVE TO REBUILD spiderdb first then rebuild
		// tfndb when that is done...
		//m_rebuildTfndb = true;
	}

	// . set the list of ptrs to the collections we have to repair
	// . should be comma or space separated in g_conf.m_collsToRepair
	// . none listed means to repair all collections
	char *s    = g_conf.m_collsToRepair.getBufStart();
	char *cbuf = g_conf.m_collsToRepair.getBufStart();
	char emptyStr[1]; emptyStr[0] = '\0';
	if ( ! s    ) s    = emptyStr;
	if ( ! cbuf ) cbuf = emptyStr;
	// reset the list of ptrs to colls to repair
	m_numColls = 0;
	// scan through the collections in the string, if there are any
 collLoop:
	// skip non alnum chars
	while ( *s && !is_alnum_a(*s) ) s++;
	// if not at the end of the string, grab the collection
	if ( *s ) {
		m_collOffs[m_numColls] = s - cbuf;
		// hold it
		char *begin = s;
		// find the length
		while ( *s && *s != ',' && !is_wspace_a(*s) ) s++;
		// store that, too
		m_collLens[m_numColls] = s - begin;
		// advance the number of collections
		m_numColls++;
		// get the next collection if under 100 collections still
		if ( m_numColls < 100 ) goto collLoop;
	}

	// split the mem we have available among the rdbs
	m_totalMem = g_conf.m_repairMem;
	// 30MB min
	if ( m_totalMem < 30000000 ) m_totalMem = 30000000;

	//
	// try to get some more mem. 
	//

	// weight factors
	float weight = 0;
	if ( m_rebuildTitledb    ) weight += 100.0;
	if ( m_rebuildPosdb      ) weight += 100.0;
	if ( m_rebuildClusterdb  ) weight +=   1.0;
	if ( m_rebuildSpiderdb   ) weight +=   5.0;
	if ( m_rebuildLinkdb     ) weight +=  20.0;
	if ( m_rebuildTagdb      ) weight +=   5.0;
	// assign memory based on weight
	int32_t titledbMem    = 0;
	int32_t posdbMem    = 0;
	int32_t clusterdbMem  = 0;
	int32_t spiderdbMem   = 0;
	int32_t linkdbMem     = 0;
	float tt = (float)m_totalMem;
	if ( m_rebuildTitledb    ) titledbMem    = (int32_t)((100.0 * tt)/weight);
	if ( m_rebuildPosdb      ) posdbMem    = (int32_t)((100.0 * tt)/weight);
	if ( m_rebuildClusterdb  ) clusterdbMem  = (int32_t)((  1.0 * tt)/weight);
	if ( m_rebuildSpiderdb   ) spiderdbMem   = (int32_t)((  5.0 * tt)/weight);
	if ( m_rebuildLinkdb     ) linkdbMem     = (int32_t)(( 20.0 * tt)/weight);

	if ( m_numColls <= 0 ) {
		log("rebuild: Rebuild had no collection specified. You need "
		    "to enter a collection or list of collections.");
		goto hadError;
	}

	
	// init secondary rdbs
	if ( m_rebuildTitledb ) {
		if ( ! g_titledb2.init2    ( titledbMem    ) ) goto hadError;
		// clean tree in case loaded from saved file
		Rdb *r = g_titledb2.getRdb();
		if ( r ) r->cleanTree();
	}

	if ( m_rebuildPosdb ) {
		if ( ! g_posdb2.init2    ( posdbMem    ) ) goto hadError;
		// clean tree in case loaded from saved file
		Rdb *r = g_posdb2.getRdb();
		if ( r ) r->cleanTree();
	}

	if ( m_rebuildClusterdb )
		if ( ! g_clusterdb2.init2  ( clusterdbMem  ) ) goto hadError;
	if ( m_rebuildSpiderdb )
		if ( ! g_spiderdb2.init2   ( spiderdbMem   ) ) goto hadError;
	if ( m_rebuildLinkdb )
		if ( ! g_linkdb2.init2     ( linkdbMem     ) ) goto hadError;

	g_errno = 0;

	// reset current coll we are repairing
	m_colli = -1;
	m_completedFirstScan  = false;

	// . tell it to advance to the next collection
	// . this will call addColl() on the appropriate Rdbs
	// . it will call addColl() on the primary rdbs for m_fullRebuild
	getNextCollToRepair();

	// if could not get any, bail
	if ( ! m_cr ) goto hadError;

	g_errno = 0;

	// load the old repair state if on disk, this will block
	load();
	// now we can save if we need to
	m_saveRepairState = true;
	// if error loading, ignore it
	g_errno = 0;

	return;

	// on any init2() error, reset all and return true
 hadError:
	int32_t saved = g_errno;
	// all done with these guys
	resetSecondaryRdbs();
	// pull back g_errno
	g_errno = saved;
	// note it
	log("repair: Had error in repair init. %s. Exiting.",
	    mstrerror(g_errno));
	// back to step 0
	g_repairMode = 0;

	m_colli = -1;
	g_conf.m_repairingEnabled = false;
	
	return;
}

// . sets m_coll/m_collLen to the next collection to repair
// . sets m_coll to NULL when none are left (we are done)
void Repair::getNextCollToRepair ( ) {
	// . advance index into collections
	// . can be index into m_colls or into g_collectiondb
	m_colli++;
	// ptr to first coll
	if ( m_numColls ) {
		if ( m_colli >= m_numColls ) {
			//m_coll = NULL;
			//m_collLen = 0;
			return;
		}
		char *buf = g_conf.m_collsToRepair.getBufStart();
		char *coll    = buf + m_collOffs [m_colli];
		int collLen = m_collLens[m_colli];
		m_cr = g_collectiondb.getRec (coll, collLen);
		// if DNE, set m_coll to NULL to stop repairing
		if ( ! m_cr ) { g_errno = ENOCOLLREC; return; }
	}
	// otherwise, we are repairing every collection by default
	else {
		m_cr = NULL;
		// loop m_colli over all the possible collnums
		while ( ! m_cr && m_colli < g_collectiondb.m_numRecs )
			m_cr = g_collectiondb.m_recs [ ++m_colli ];
		if ( ! m_cr ) {
			//m_coll = NULL;
			//m_collLen = 0;
			g_errno = ENOCOLLREC;
			return;
		}
		//m_coll    = m_cr->m_coll;
		//m_collLen = m_cr->m_collLen;
	}

	// collection cannot be deleted while we are in repair mode...
	m_collnum = m_cr->m_collnum;

	log("repair: now rebuilding for collection '%s' (%i)"
	    , m_cr->m_coll
	    , (int)m_collnum
	    );

	char *coll = m_cr->m_coll;

	// add collection to secondary rdbs
	if ( m_rebuildTitledb ) {
		if ( //! g_titledb2.addColl    ( m_coll ) &&
		    ! g_titledb2.getRdb()->addRdbBase1(coll) &&
		     g_errno != EEXIST ) goto hadError;
	}

	if ( m_rebuildPosdb ) {
		if ( ! g_posdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	if ( m_rebuildClusterdb ) {
		if ( ! g_clusterdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	if ( m_rebuildSpiderdb ) {
		if ( ! g_spiderdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	if ( m_rebuildLinkdb ) {
		if ( ! g_linkdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	return;

 hadError:
	// note it
	log("repair: Had error getting next coll to repair: %s. Exiting.",
	    mstrerror(g_errno));
	// a mode of 5 means we are done repairing and waiting to go back to
	// mode 0, but only PingServer.cpp will only set our mode to 0 once 
	// it has verified all other hosts are in mode 5 or 0.
	//g_repairMode = 5;
	return;
}


void loopWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	Repair *THIS = (Repair *)state;
	THIS->m_msg5InUse = false;
	THIS->loop(NULL);
}

//void loopWrapper3 ( void *state ) {
//	//Repair *THIS = (Repair *)state;
//	// this hold "tr" in one case
//	g_repair.loop(state);
//}


enum {
	STAGE_TITLEDB_0  = 0 ,
	STAGE_TITLEDB_1      ,
	STAGE_TITLEDB_2      ,
	STAGE_TITLEDB_3      ,
	STAGE_TITLEDB_4      ,
	/*
	STAGE_TITLEDB_5      ,
	STAGE_TITLEDB_6      ,
	*/
	STAGE_SPIDERDB_0     
	/*
	STAGE_SPIDERDB_1     ,
	STAGE_SPIDERDB_2A    ,
	STAGE_SPIDERDB_2B    ,
	STAGE_SPIDERDB_3     ,
	STAGE_SPIDERDB_4     ,

	STAGE_INDEXDB_0      ,
	STAGE_INDEXDB_1      ,
	STAGE_INDEXDB_2      ,

	STAGE_DATEDB_0       ,
	STAGE_DATEDB_1       ,
	STAGE_DATEDB_2      
	*/
};

bool Repair::save ( ) {
	// do not do a blocking save for auto save if
	// we never entere repair mode
	if ( ! m_saveRepairState ) return true;
	// log it
	log("repair: saving repair.dat");
	char tmp[1024];
	sprintf ( tmp , "%s/repair.dat", g_hostdb.m_dir );
	File ff;
	ff.set ( tmp );
	if ( ! ff.open ( O_RDWR | O_CREAT | O_TRUNC ) ) {
		log(LOG_WARN, "repair: Could not open %s : %s", ff.getFilename(), mstrerror(g_errno));
		return false;
	}
	// first 8 bytes are the size of the DATA file we're mapping
	g_errno = 0;
	int32_t      size   = &m_SAVE_END - &m_SAVE_START;
	int64_t offset = 0LL;
	ff.write ( &m_SAVE_START , size , offset ) ;
	ff.close();
	return true;
}

bool Repair::load ( ) {

	char tmp[1024];
	sprintf ( tmp , "%s/repair.dat", g_hostdb.m_dir );
	File ff;
	ff.set ( tmp );

	logf(LOG_INIT,"repair: Loading %s to resume repair.",tmp);

	if ( ! ff.open ( O_RDONLY ) ) {
		log(LOG_WARN, "repair: Could not open %s : %s", ff.getFilename(), mstrerror(g_errno));
		return false;
	}
	// first 8 bytes are the size of the DATA file we're mapping
	g_errno = 0;
	int32_t      size   = &m_SAVE_END - &m_SAVE_START;
	int64_t offset = 0LL;
	ff.read ( &m_SAVE_START, size , offset ) ;
	ff.close();

	// resume titledb scan?
	m_nextTitledbKey = m_lastTitledbKey;
	// resume spiderdb scan?
	m_nextSpiderdbKey = m_lastSpiderdbKey;

	// reinstate the valuable vars
	m_cr   = g_collectiondb.m_recs [ m_collnum ];
	//m_coll = m_cr->m_coll;


	m_stage = STAGE_TITLEDB_0;
	if ( m_completedFirstScan  ) m_stage = STAGE_SPIDERDB_0;

	return true;
}


// . this is the main repair loop
// . this is repsonsible for calling all the repair functions
// . all repair callbacks given come back into this loop
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Repair::loop ( void *state ) {
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	m_allowInjectToLoop = false;

	// if the power went off
	if ( ! g_process.m_powerIsOn ) {
		// sleep 1 second and retry
		m_isRetrying = true;
		return true;
	}

	// was repairing turned off all of a sudden?
	if ( ! g_conf.m_repairingEnabled ) {
		//log("repair: suspending repair.");
		// when it gets turned back on, the sleep callback above
		// will notice it was suspended and call loop() again to
		// resume where we left off...
		m_isSuspended = true;
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, repair suspended", __FILE__, __func__, __LINE__);
		return true;
	}

	// if we re-entered this loop from doneWithIndexDocWrapper
	// do not launch another msg5 if it is currently out!
	if ( m_msg5InUse ) 
	{
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, waiting for msg5", __FILE__, __func__, __LINE__);
		return false;
	}

	// set this to on
	g_process.m_repairNeedsSave = true;

 loop1:

	if ( g_process.m_mode == EXIT_MODE )
	{
		return true;
	}

	if ( m_stage == STAGE_TITLEDB_0  ) 
	{
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: STAGE_TITLEDB_0 - scanRecs", __FILE__, __func__, __LINE__);
		m_stage++;
		if ( ! scanRecs()       ) 
		{
			return false;
		}
	}
	
	if ( m_stage == STAGE_TITLEDB_1  ) 
	{
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: STAGE_TITLEDB_1 - gotScanRecList", __FILE__, __func__, __LINE__);
		m_stage++;
		if ( ! gotScanRecList()   ) 
		{
			return false;
		}
	}
	
	if ( m_stage == STAGE_TITLEDB_2  ) {
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: STAGE_TITLEDB_2", __FILE__, __func__, __LINE__);
		m_stage++;
	}
	// get the site rec to see if it is banned first, before injecting it
	if ( m_stage == STAGE_TITLEDB_3 ) {
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: STAGE_TITLEDB_3", __FILE__, __func__, __LINE__);
		
		// if we have maxed out our injects, wait for one to come back
		if ( m_numOutstandingInjects >= g_conf.m_maxRepairSpiders ) {
			m_allowInjectToLoop = true;
			return false;
		}
		m_stage++;
		
		// BEGIN NEW STUFF
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: injectTitleRec", __FILE__, __func__, __LINE__);
		bool status = injectTitleRec();
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: injectTitleRec returned %s", __FILE__, __func__, __LINE__, status?"true":"false");
			
		//return false; // (state)
		// try to launch another
		if ( m_numOutstandingInjects<g_conf.m_maxRepairSpiders ) {
			m_stage = STAGE_TITLEDB_0;
			if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: Still have more free repair spiders, loop.", __FILE__, __func__, __LINE__);
			goto loop1;
		}
		
		// if we are full and it blocked... wait now
		if ( ! status ) 
		{
			if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, return false. Full queue and blocked.", __FILE__, __func__, __LINE__);
			return false;
		}
	}
	
	if ( m_stage == STAGE_TITLEDB_4  ) {
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: STAGE_TITLEDB_4", __FILE__, __func__, __LINE__);
		m_stage++;
		//if ( ! addToTfndb2()       ) return false;
	}

	// if we are not done with the titledb scan loop back up
	if ( ! m_completedFirstScan ) {
		m_stage = STAGE_TITLEDB_0;
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: loop, set STAGE_TITLEDB_0", __FILE__, __func__, __LINE__);
		goto loop1;
	}

	// if we are waiting for injects to come back, return
	if ( m_numOutstandingInjects > 0 ) {
		// tell injection complete wrapper to call us back, otherwise
		// we never end up moving on to the spider phase
		g_repair.m_allowInjectToLoop = true;
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, return false. Have %" PRId32" outstanding injects", __FILE__, __func__, __LINE__, m_numOutstandingInjects);
		return false;
	}

	// reset list
	//m_list.reset();

	// . spiderdb scan
	// . put new spider recs into g_spiderdb2
	/*
 loop2:
	if ( m_stage == STAGE_SPIDERDB_0 ) {
		m_stage++;
		if ( ! scanSpiderdb()     ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_1 ) {
		m_stage++;
		if ( ! getTfndbListPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_2A ) {
		m_stage++;
		if ( ! getTagRecPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_2B ) {
		m_stage++;
		if ( ! getRootQualityPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_3 ) {
		m_stage++;
		if ( ! addToSpiderdb2Part2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_4 ) {
		m_stage++;
		if ( ! addToTfndb2Part2()  ) return false;
	}
	// if we are not done with the titledb scan loop back up
	if ( ! m_completedSpiderdbScan ) {
		m_stage = STAGE_SPIDERDB_0;
		goto loop2;
	}
	*/

	// reset list
	m_titleRecList.reset();

	// . indexdb scan
	// . delete indexdb recs whose docid is not in tfndb
	// . delete duplicate docid in same termlist docids
	// . turn this off for now to get buzz ready faster
	/*
 loop3:
	if ( m_stage == STAGE_INDEXDB_0 ) {
		m_stage++;
		if ( ! scanIndexdb()      ) return false;
	}
	if ( m_stage == STAGE_INDEXDB_1 ) {
		m_stage++;
		if ( ! gotIndexRecList()  ) return false;
	}
	if ( m_stage == STAGE_INDEXDB_2 ) {
		m_stage++;
		if ( ! addToIndexdb2()    ) return false;
	}
	// if we are not done with the titledb scan loop back up
	if ( ! m_completedIndexdbScan ) {
		m_stage = STAGE_INDEXDB_0;
		goto loop3;
	}
	*/

	// in order for dump to work we must be in mode 4 because
	// Rdb::dumpTree() checks that
	g_repairMode = 4;

	// force dump to disk of the newly rebuilt rdbs, because we need to
	// make sure their trees are empty when the primary rdbs assume
	// the data and map files of the secondary rdbs. i don't want to
	// have to mess with tree data as well.

	// if we do not complete the dump here it will be monitored above
	// in the sleep wrapper, repairWrapper(), and that will call 
	// Repair::loop() (this function) again when the dump is done
	// and we will be able to advance passed this m_stage
	// . dump the trees of all secondary rdbs that need it
	//dumpLoop();
	// are we done dumping?
	//if ( ! dumpsCompleted() ) return false;
	
	// we are all done with the repair loop
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
	return true;
}



// this blocks
void Repair::updateRdbs ( ) {

	if ( m_updated ) return;

	// do not double call
	m_updated = true;

	// . replace old rdbs with the new ones
	// . these calls must all block otherwise things will get out of sync
	Rdb *rdb1;
	Rdb *rdb2;

	if ( m_rebuildTitledb ) {
		rdb1 = g_titledb.getRdb ();
		rdb2 = g_titledb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	if ( m_rebuildPosdb ) {
		rdb1 = g_posdb.getRdb();
		rdb2 = g_posdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	if ( m_rebuildClusterdb ) {
		rdb1 = g_clusterdb.getRdb();
		rdb2 = g_clusterdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	if ( m_rebuildSpiderdb ) {
		rdb1 = g_spiderdb.getRdb();
		rdb2 = g_spiderdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	if ( m_rebuildLinkdb ) {
		rdb1 = g_linkdb.getRdb();
		rdb2 = g_linkdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
}

void Repair::resetSecondaryRdbs ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// use niceness of 1
		rdb->reset();
	}
}

bool Repair::dumpLoop ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];

		// use niceness of 1
		rdb->dumpTree ( 1 );
	}
	g_errno = 0;
	// . register sleep wrapper to check when dumping is done
	// . it will call Repair::loop() when done
	return false;
}

bool Repair::dumpsCompleted ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// anything in tree/buckets?
		if ( rdb->getNumUsedNodes() ) return false;
		// still dumping?
		if ( rdb->isDumping      () ) return false;
	}
	// no more dump activity
	return true;
}


// . this is only called from repairLoop()
// . returns false if blocked, true otherwise
// . grab the next scan record
bool Repair::scanRecs ( ) {
	// just the tree?
	//int32_t nf          = 1;
	//bool includeTree = false;
	RdbBase *base = g_titledb.getRdb()->getBase ( m_collnum );
	//if ( m_fn == base->getNumFiles() ) { nf = 0; includeTree = true; }
	// always clear last bit of g_nextKey
	m_nextTitledbKey.n0 &= 0xfffffffffffffffeLL;
	// for saving
	m_lastTitledbKey = m_nextTitledbKey;
	log(LOG_DEBUG,"repair: nextKey=%s endKey=%s"
	    "coll=%s collnum=%" PRId32" "
	    "bnf=%" PRId32,//fn=%" PRId32" nf=%" PRId32,
	    KEYSTR(&m_nextTitledbKey,sizeof(key96_t)),
	    KEYSTR(&m_endKey,sizeof(key96_t)),
	    m_cr->m_coll,
	    (int32_t)m_collnum,
	    (int32_t)base->getNumFiles());//,m_fn,nf);
	// sanity check
	if ( m_msg5InUse ) {
		g_process.shutdownAbort(true); }
	// when building anything but tfndb we can get the rec
	// from the twin in case of data corruption on disk
	bool fixErrors = true;
	//if ( m_rebuildTfndb ) fixErrors = false;
	// get the list of recs
	g_errno = 0;
	if ( m_msg5.getList ( RDB_TITLEDB        ,
			      m_collnum           ,
			      &m_titleRecList      ,
			      m_nextTitledbKey   ,
			      m_endKey         , // should be maxed!
			      1024             , // min rec sizes
			      true             , // include tree?
			      0                , // max cache age
			      0                , // startFileNum
			      -1               , // m_numFiles   
			      this             , // state 
			      loopWrapper      , // callback
			      MAX_NICENESS     , // niceness
			      fixErrors        , // do error correction?
			      NULL             , // cache key ptr
			      0                , // retry num
			      -1               , // maxRetries
			      true             , // compensate for merge
			      -1LL,              // sync point
			      false,             // isRealMerge
			      true))             // allowPageCache
		return true;
	m_msg5InUse = true;
	return false;
}


// . this is only called from repairLoop()
// . returns false if blocked, true otherwise
bool Repair::gotScanRecList ( ) {

	// get the base
	//RdbBase *base = g_titledb.getRdb()->getBase ( m_collnum );

	if ( g_errno == ECORRUPTDATA ) {
		log("repair: Encountered corruption1 in titledb. "
		    "NextKey=%s",
		    KEYSTR(&m_nextTitledbKey,sizeof(key96_t)));
		/*
		// get map for this file
		RdbMap  *map  = base->getMap(m_fn);
		// what page has this key?
		int32_t page = map->getPage ( (char *)&m_nextTitledbKey );
		// advance the page number
	advancePage:
		page++;
		// if no more pages, we are done!
		if ( page >= map->getNumPages() ) {
			log("repair: No more pages in rdb map, done with "
			    "titledb file.");
			g_errno = 0; m_recsCorruptErrors++;
			goto fileDone;
		}
		// get key from that page
		key96_t next = *(key96_t *)map->getKeyPtr ( page );
		// keep advancing if its the same key!
		if ( next == m_nextTitledbKey ) goto advancePage;
		// ok, we got a new key, use it
		m_nextTitledbKey = next;
		*/
		// get the docid
		//int64_t dd = Titledb::getDocIdFromKey(&m_nextTitledbKey);
		// inc it
		//dd++;
		// re-make key
		//m_nextTitledbKey = Titledb::makeFirstTitleRecKey ( dd );
		// advance one if positive, must always start on a neg
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
		// count as error
		m_recsCorruptErrors++;
	}

	// was there an error? list will probably be empty
	if ( g_errno ) {
		log("repair: Got error reading title rec: %s.",
		    mstrerror(g_errno));
		// keep retrying, might be OOM
		m_stage = STAGE_TITLEDB_0 ;
		// sleep 1 second and retry
		m_isRetrying = true;
		// exit the loop code, Repair::loop() will be re-called
		return false;
	}
		
	/*
	// a hack
	if ( m_count > 100 ) { // && m_fn == 0 ) {
		logf(LOG_INFO,"repair: hacking titledb complete.");
		//m_completedFirstScan = true;
		//m_stage = STAGE_SPIDERDB_0;
		m_list.reset();
		//return true;
	}
	*/

	// all done with this bigfile if this list is empty
	if ( m_titleRecList.isEmpty() ) { //||m_recsScanned > 10 ) {
		// note it
		//logf(LOG_INFO,"repair: Scanning ledb file #%" PRId32".",  m_fn );
		m_completedFirstScan = true;
		logf(LOG_INFO,"repair: Completed titledb scan of "
		     "%" PRId64" records.",m_recsScanned);
		//logf(LOG_INFO,"repair: Starting spiderdb scan.");
		m_stage = STAGE_SPIDERDB_0;
		// force spider scan completed now too!
		m_completedSpiderdbScan = true;
		g_repair.m_allowInjectToLoop = true;
		return true;
	}

	// nextRec2:
	key96_t tkey = m_titleRecList.getCurrentKey();
	int64_t docId = Titledb::getDocId ( &tkey );
	// save it
	//m_currentTitleRecKey = tkey;

	// save it
	m_docId = docId;
	// is it a delete?
	m_isDelete = false;
	// we need this to compute the tfndb key to add/delete
	//m_ext = -1;
	m_uh48 = 0LL;

	// count the title recs we scan
	m_recsScanned++;

	// skip if bad... CORRUPTION
	if ( tkey < m_nextTitledbKey ) {
		log("repair: Encountered corruption2 in titledb. "
		    "key=%s < NextKey=%s"
		    "FirstDocId=%" PRIu64".",
		    //p1-1,
		    KEYSTR(&tkey,sizeof(key96_t)),
		    KEYSTR(&m_nextTitledbKey,sizeof(key96_t)),
		    docId);
		m_nextTitledbKey += (uint32_t)1;
		// advance one if positive, must always start on a negative key
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}
	else {
		// advance m_nextTitledbKey to get next titleRec
		m_nextTitledbKey = m_titleRecList.getCurrentKey();
		m_nextTitledbKey += (uint32_t)1;
		// advance one if positive, must always start on a negative key
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
	}

	// are we the host this url is meant for?
	//uint32_t gid = getGroupId ( RDB_TITLEDB , &tkey );
	uint32_t shardNum = getShardNum (RDB_TITLEDB , &tkey );
	if ( shardNum != getMyShardNum() ) {
		m_recsWrongGroupId++;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	// . if one of our twins is responsible for it...
	// . is it assigned to us? taken from assigendToUs() in SpiderCache.cpp
	// . get our group from our hostId
	int32_t  numHosts;
	//Host *hosts = g_hostdb.getGroup ( g_hostdb.m_groupId, &numHosts);
	Host *hosts = g_hostdb.getShard ( shardNum , &numHosts );
	int32_t  ii =  docId % numHosts ;
	// . are we the host this url is meant for?
	// . however, if you are rebuilding tfndb, each twin must scan all
	//   title recs and make individual entries for those title recs
	if ( hosts[ii].m_hostId != g_hostdb.m_hostId ){//&&!m_rebuildTfndb ) {
		m_recsUnassigned++;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	/*
	// is the list from the tree in memory?
	int32_t id2;
	if ( m_fn == base->getNumFiles() ) id2 = 255;
	else                               id2 = base->m_fileIds2[m_fn];

	// that is the tfn...
	m_tfn = id2;
	*/

	// is it a negative titledb key?
	if ( (tkey.n0 & 0x01) == 0x00 ) {
		// count it
		m_recsNegativeKeys++;
		// otherwise, we need to delete this
		// docid from tfndb...
		m_isDelete = true;
	}

	// if not rebuilding tfndb, skip this
	//if ( ! m_rebuildTfndb && m_isDelete ) {
	if ( m_isDelete ) {
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	return true;
}

static void doneWithIndexDoc ( XmlDoc *xd ) {
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	// preserve
	int32_t saved = g_errno;
	// nuke it
	mdelete ( xd , sizeof(XmlDoc) , "xdprnuke");
	delete ( xd );
	// reduce the count
	g_repair.m_numOutstandingInjects--;
	// error?
	if ( saved ) {
		g_repair.m_recsetErrors++;
		g_repair.m_stage = STAGE_TITLEDB_0; // 0
		return;
	}
	/*
	// find the i
	int32_t i ; for ( i = 0 ; i < MAX_OUT_REPAIR ; i++ ) {
		if ( ! s_inUse[i] ) continue;
		if ( xd == &s_docs[i] ) break;
	}
	if ( i >= MAX_OUT_REPAIR ) { g_process.shutdownAbort(true); }
	// reset it i guess
	xd->reset();
	// give back the tr
	s_inUse[i] = 0;
	*/
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
}

static void doneWithIndexDocWrapper ( void *state ) {
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	// clean up
	doneWithIndexDoc ( (XmlDoc *)state );
	// and re-enter the loop to get next title rec
	g_repair.loop ( NULL );
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
}


//bool Repair::getTagRec ( void **state ) {
bool Repair::injectTitleRec ( ) {
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);

	// no, now we specify in call to indexDoc() which
	// dbs we want to update
	//if ( ! m_fullRebuild && ! m_removeBadPages ) return true;

	// scan for our docid in the title rec list
	char *titleRec = NULL;
	int32_t titleRecSize = 0;
	// convenience var
	RdbList *tlist = &m_titleRecList;
	// scan the titleRecs in the list
	for ( ; ! tlist->isExhausted() ; tlist->skipCurrentRecord ( ) ) {
		// get the rec
		char *rec     = tlist->getCurrentRec();
		int32_t  recSize = tlist->getCurrentRecSize();
		// get that key
		key96_t *k = (key96_t *)rec;
		// skip negative recs, first one should not be negative however
		if ( ( k->n0 & 0x01 ) == 0x00 ) continue;
		// get docid of that guy
		int64_t dd = Titledb::getDocId(k);
		// compare that
		if ( m_docId != dd ) continue;
		// we got it!
		titleRec = rec;
		titleRecSize = recSize;
		break;
	}

	XmlDoc *xd = NULL;
	try { xd = new ( XmlDoc ); }
	catch ( ... ) {
                g_errno = ENOMEM;
		m_recsetErrors++;
		m_stage = STAGE_TITLEDB_0; // 0
		return true;
	}
        mnew ( xd , sizeof(XmlDoc),"xmldocpr");    

	// clear out first since set2 no longer does
	//xd->reset();
	if ( ! xd->set2 ( titleRec,-1,m_cr->m_coll , NULL , MAX_NICENESS ) ) {
		m_recsetErrors++;
		m_stage = STAGE_TITLEDB_0; // 0
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, return true. XmlDoc->set2 failed", __FILE__, __func__, __LINE__);
		return true;
	}
	// set callback
	xd->setCallback ( xd , doneWithIndexDocWrapper );

	// clear any error involved with cache, it doesn't matter so much
	g_errno = 0;

	// invalidate certain things to recompute!
	// we are now setting from docid
	xd->m_tagRecValid    = false;

	// rebuild the title rec! otherwise we re-add the old one!!!!!!!
	xd->m_titleRecBufValid = false;
	// free it since set2() should have uncompressed it!
	//mfree ( titleRec , titleRecSize, "repair" );
	// and so xd doesn't free it
	xd->m_titleRecBuf.purge();// = NULL;

	// use the ptr_utf8Content that we have
	xd->m_recycleContent = true;

	// rebuild the content hash since we change that function sometimes
	xd->m_contentHash32Valid = false;

	// claim it, so "tr" is not overwritten
	m_numOutstandingInjects++;

	bool addToSecondaryRdbs = true;

	xd->m_usePosdb     = m_rebuildPosdb;
	xd->m_useClusterdb = m_rebuildClusterdb;
	xd->m_useLinkdb    = m_rebuildLinkdb;
	xd->m_useSpiderdb  = m_rebuildSpiderdb;
	xd->m_useTitledb   = m_rebuildTitledb;
	xd->m_useSecondaryRdbs = addToSecondaryRdbs;

	// always use tagdb because if we update the sitenuminlinks
	// or whatever, we want to add that to tagdb
	xd->m_useTagdb     = true;

	// not if rebuilding link info though! we assume the old link info is
	// bad...
	if ( m_rebuildLinkdb ) {
		xd->m_useTagdb = false;

		// also need to preserve the "lost link" flag somehow
		// from the old linkdb...
		//log("repair: would lose linkdb lost flag.");
		// core until we find a way to preserve the old discovery
		// date from the old linkdb!
		//log("repair: fix linkdb rebuild. coring.");
		//g_process.shutdownAbort(true);
	}

	if ( ! g_conf.m_rebuildRecycleLinkInfo ) {
		// then recompute link info as well!
		xd->m_linkInfo1Valid = false;
		// make null to be safe
		xd->ptr_linkInfo1  = NULL;
		xd->size_linkInfo1 = 0;
	}
	// . also lookup site rank again!
	// . this will use the value in tagdb if less than 48 hours otherwise
	//   it will recompute it
	// . CRAP! this makes the data undeletable if siterank changes!
	//   so we have to be able to re-save our title rec with the new
	//   site rank info...
	if ( xd->m_useTitledb ) {
		// save for logging
		xd->m_logLangId         = xd->m_langId;
		xd->m_logSiteNumInlinks = xd->m_siteNumInlinks;
		// recompute site, no more domain sites allowed
		xd->m_siteValid = false;
		xd->ptr_site    = NULL;
		xd->size_site   = 0;
		// recalculate the sitenuminlinks
		xd->m_siteNumInlinksValid = false;
		// recalculate the langid
		xd->m_langIdValid = false;
		// recalcualte and store the link info
		xd->m_linkInfo1Valid = false;
		// make null to be safe
		xd->ptr_linkInfo1  = NULL;
		xd->size_linkInfo1 = 0;
		// re-get the tag rec from tagdb
		xd->m_tagRecValid     = false;
		xd->m_tagRecDataValid = false;
	}


	xd->m_priority = -1;
	xd->m_priorityValid = true;

	// this makes sense now that we set from docid using set3()?
	//xd->m_recycleContent = true;

	xd->m_contentValid = true;
	xd->m_content = xd->ptr_utf8Content;
	xd->m_contentLen = xd->size_utf8Content - 1;

	// . get the meta list to add
	// . sets m_usePosdb, m_useTitledb, etc.
	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: Calling indexDoc", __FILE__, __func__, __LINE__);
	bool status = xd->indexDoc ( );
	// blocked?
	if ( ! status ) 
	{
		if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, return false. XmlDoc->indexDoc blocked", __FILE__, __func__, __LINE__);
		return false;
	}

	// give it back
	doneWithIndexDoc ( xd );

	if( g_conf.m_logTraceRepairs ) log(LOG_TRACE,"%s:%s:%d: END, return true", __FILE__, __func__, __LINE__);
	return true;
}

// . returns false if fails cuz buffer cannot be grown (oom)
// . this is called by Parms.cpp
bool Repair::printRepairStatus ( SafeBuf *sb , int32_t fromIp ) {
	// default is a repairMode of 0, "not running"
	const char *status = "not running";
	if ( g_repairMode == 0 && g_conf.m_repairingEnabled )
		status = "waiting for previous rebuild to complete";
	if ( g_repairMode == 1 )
		status = "waiting for spiders or merge to stop";
	if ( g_repairMode == 2 )			
		status = "waiting for all hosts in network to stop "
			"spidering and merging";
	if ( g_repairMode == 3 )			
		status = "waiting for all hosts to save";
	if ( g_repairMode == 4 ) {
		if ( m_completedFirstScan )
			status = "scanning old spiderdb";
		else
			status = "scanning old records";
	}
	if ( g_repairMode == 5 ) 
		status = "waiting for final dump to complete";
	if ( g_repairMode == 6 ) 
		status = "waiting for others to finish scan and dump";
	if ( g_repairMode == 7 )			
		status = "updating rdbs with new data";
	if ( g_repairMode == 8 )			
		status = "waiting for all hosts to complete update";
	if ( ! g_process.m_powerIsOn && g_conf.m_repairingEnabled )
		status = "waiting for power to return";

	// the titledb scan stats (phase 1)
	int64_t ns     = m_recsScanned ;
	int64_t nr     = g_titledb.getRdb()->getNumTotalRecs() ;
	float     ratio  = nr ? ((float)ns * 100.0) / (float)nr : 0.0;
	int64_t errors = 
		m_recsOutOfOrder +
		m_recsetErrors   +
		m_recsCorruptErrors +
		m_recsXmlErrors   +
		m_recsDupDocIds    ;

	// the spiderdb scan stats (phase 2)
	int64_t ns2     = m_spiderRecsScanned ;
	int64_t nr2     = g_spiderdb.getRdb()->getNumTotalRecs() ;
	float     ratio2  = nr2 ? ((float)ns2 * 100.0) / (float)nr2 : 0.0;
	int64_t errors2 = m_spiderRecSetErrors;

	const char *newColl = " &nbsp; ";
	//if ( m_fullRebuild ) newColl = m_newColl;

	const char *oldColl = " &nbsp; ";
	if ( m_cr ) oldColl = m_cr->m_coll;

	Host *mh = g_pingServer.m_minRepairModeHost;
	int32_t  minHostId = -1;
	char  minIpBuf[64];
	minIpBuf[0] = '\0';
	int16_t minPort = 80;
	if ( mh ) {
		minHostId = mh->m_hostId;
		int32_t minHostIp = g_hostdb.getBestIp ( mh );
		strcpy(minIpBuf,iptoa(minHostIp));
		minPort = mh->m_httpPort;
	}

	// now show the rebuild status
	sb->safePrintf ( 
			 "<table%s"
			 " id=\"repairstatustable\">"

			 "<tr class=hdrow><td colspan=2><b><center>"
			 "Rebuild Status</center></b></td></tr>\n"

			 "<tr bgcolor=#%s><td colspan=2>"
			 "<font size=-2>"
			 "Use this to rebuild a database or to reindex "
			 "all pages to pick up new link text. Or to "
			 "reindex all pages to pick up new site rank info "
			 "from tagdb. To pick up "
			 "new link text you should rebuild titledb and posdb. "
			 "If unsure, just do a full rebuild, but it will "
			 "require about 2GB more than the disk used before "
			 "the rebuild, so at its peak the rebuild will use "
			 "a little more than double the disk space you "
			 "are using now. Also you will want to set "
			 "recycle link text to false to pick up the new link "
			 "text. However, if you just want to pick up "
			 "new sitenuminlinks tags in tagdb to get more "
			 "accurate siteranks for each result, then you can "
			 "leave the recycle link text set to true."
			 ""
			 "<br><br>"
			 "All spidering for all collections will be disabled "
			 "when the rebuild is in progress. But you should "
			 "still be able to conduct searches on the original "
			 "index. You can pause "
			 "the rebuild by disabling <i>rebuild mode enabled"
			 "</i>. Each shard should save its rebuid state so "
			 "you can safely shut shards down when rebuilding "
			 "and they should resume on startup. When the rebuild "
			 "completes it moves the original files to the trash "
			 "subdirectory and replaces them with the newly "
			 "rebuilt files."
			 "</font>"
			 "</td></tr>"

			 // status (see list of above statuses)
			 "<tr bgcolor=#%s><td width=50%%><b>status</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td width=50%%><b>rebuild mode</b>"
			 "</td>"
			 "<td>%" PRId32"</td></tr>\n"

			 "<tr bgcolor=#%s>"

			 "<td width=50%%><b>min rebuild mode</b></td>"
			 "<td>%" PRId32"</td></tr>\n"

			 "<tr bgcolor=#%s>"
			 "<td width=50%%><b>host ID with min rebuild mode"
			 "</b></td>"

			 "<td><a href=\"http://%s:%hu/admin/rebuild\">"
			 "%" PRId32"</a></td></tr>\n"

			 "<tr bgcolor=#%s><td><b>old collection</b></td>"
			 "<td>%s</td></tr>"

			 "<tr bgcolor=#%s><td><b>new collection</b></td>"
			 "<td>%s</td></tr>"

			 ,
			 TABLE_STYLE ,


			 LIGHT_BLUE ,
			 LIGHT_BLUE ,
			 status ,

			 LIGHT_BLUE ,
			 (int32_t)g_repairMode,

			 LIGHT_BLUE ,
			 (int32_t)g_pingServer.m_minRepairMode,

			 LIGHT_BLUE ,
			 minIpBuf, // ip string
			 minPort,  // port
			 (int32_t)minHostId,

			 LIGHT_BLUE ,
			 oldColl ,

			 LIGHT_BLUE ,
			 newColl
			 );

	sb->safePrintf ( 
			 // docs done, includes overwritten title recs
			 "<tr bgcolor=#%s><td><b>titledb recs scanned</b></td>"
			 "<td>%" PRId64" of %" PRId64"</td></tr>\n"

			 // percent complete
			 "<tr bgcolor=#%s><td><b>titledb recs scanned "
			 "progress</b></td>"
			 "<td>%.2f%%</td></tr>\n"

			 // title recs set errors, parsing errors, etc.
			 //"<tr bgcolor=#%s><td><b>title recs injected</b></td>"
			 //"<td>%" PRId64"</td></tr>\n"

			 // title recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>titledb rec error count</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 // sub errors
			 "<tr bgcolor=#%s><td> &nbsp; key out of order</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; set errors</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; corrupt errors</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; xml errors</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; dup docid errors</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; negative keys</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 //"<tr bgcolor=#%s><td> &nbsp; overwritten recs</b></td>"
			 //"<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; twin's "
			 "respsponsibility</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; wrong shard</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; root urls</b></td>"
			 "<td>%" PRId64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; non-root urls</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; no title rec</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 //"<tr><td><b> &nbsp; Other errors</b></td>"
			 //"<td>%" PRId64"</td></tr>\n"

			 // time left in hours
			 //"<tr><td><b>Time Left in Phase %" PRId32"</b></td>"
			 //"<td>%.2f hrs</td></tr>\n"

			 ,
			 DARK_BLUE,
			 ns     ,
			 nr     ,
			 DARK_BLUE,
			 ratio  ,
			 //DARK_BLUE,
			 //m_recsInjected ,
			 DARK_BLUE,
			 errors ,
			 DARK_BLUE,
			 m_recsOutOfOrder ,
			 DARK_BLUE,
			 m_recsetErrors  ,
			 DARK_BLUE,
			 m_recsCorruptErrors  ,
			 DARK_BLUE,
			 m_recsXmlErrors  ,
			 DARK_BLUE,
			 m_recsDupDocIds ,
			 DARK_BLUE,
			 m_recsNegativeKeys ,
			 //DARK_BLUE,
			 //m_recsOverwritten ,
			 DARK_BLUE,
			 m_recsUnassigned ,

			 DARK_BLUE,
			 m_recsWrongGroupId ,

			 DARK_BLUE,
			 m_recsRoot ,
			 DARK_BLUE,
			 m_recsNonRoot ,

			 DARK_BLUE,
			 m_noTitleRecs
			 );


	sb->safePrintf(
			 // spider recs done
			 "<tr bgcolor=#%s><td><b>spider recs scanned</b></td>"
			 "<td>%" PRId64" of %" PRId64"</td></tr>\n"

			 // percent complete
			 "<tr bgcolor=#%s><td><b>spider recs scanned "
			 "progress</b></td>"
			 "<td>%.2f%%</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec not "
			 "assigned to us</b></td>"
			 "<td>%" PRId32"</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec errors</b></td>"
			 "<td>%" PRId64"</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec bad tld</b></td>"
			 "<td>%" PRId32"</td></tr>\n"

			 // time left in hours
			 //"<tr bgcolor=#%s><td><b>"
			 //"Time Left in Phase %" PRId32"</b></td>"
			 //"<td>%.2f hrs</td></tr>\n"

			 ,
			 LIGHT_BLUE ,
			 ns2    ,
			 nr2    ,
			 LIGHT_BLUE ,
			 ratio2 ,
			 LIGHT_BLUE ,
			 m_spiderRecNotAssigned ,
			 LIGHT_BLUE ,
			 errors2,
			 LIGHT_BLUE ,
			 m_spiderRecBadTLD
			 );


	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );

	// . count the recs in each secondary rdb
	// . those are the rdbs we are adding the recs to
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		const char *bg = DARK_BLUE;
		Rdb *rdb = rdbs[i];
		int64_t tr = rdb->getNumTotalRecs();
		// skip if init2() as not called on it b/c the
		// m_dbname will be 0
		if ( tr == 0 ) continue;
		sb->safePrintf(
			 "<tr bgcolor=#%s><td><b>%s2 recs</b></td>"
			 "<td>%" PRId64"</td></tr>\n" ,
			 bg,
			 rdb->getDbname(),
			 rdb->getNumTotalRecs());
	}

	// close up that table
	sb->safePrintf("</table>\n<br>");

	// print a table
	const char *rr[23];
	if ( m_fullRebuild )       rr[0] = "Y";
	else                       rr[0] = "N";

	if ( m_rebuildTitledb )    rr[1] = "Y";
	else                       rr[1] = "N";

	if ( m_rebuildPosdb )      rr[3] = "Y";
	else                       rr[3] = "N";
	if ( m_rebuildClusterdb )  rr[5] = "Y";
	else                       rr[5] = "N";
	if ( m_rebuildSpiderdb )   rr[7] = "Y";
	else                       rr[7] = "N";
	if ( m_rebuildLinkdb )     rr[9] = "Y";
	else                       rr[9] = "N";


	if ( m_rebuildRoots  )     rr[11] = "Y";
	else                       rr[11] = "N";
	if ( m_rebuildNonRoots  )  rr[12] = "Y";
	else                       rr[12] = "N";

	sb->safePrintf ( 

			 "<table %s "
			 "id=\"repairstatustable2\">"

			 // current collection being repaired
			 "<tr class=hdrow><td colspan=2><b><center>"
			 "Rebuild Settings In Use</center></b></td></tr>"

			 // . print parms for this repair
			 // . they may differ than current controls because
			 //   the current controls were changed after the
			 //   repair started
			 "<tr bgcolor=#%s>"
			 "<td width=50%%><b>full rebuild</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild titledb</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild posdb</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild clusterdb</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild spiderdb</b></td>"
			 "<td>%s</td></tr>\n" 

			 "<tr bgcolor=#%s><td><b>rebuild linkdb</b></td>"
			 "<td>%s</td></tr>\n" 

			 "<tr bgcolor=#%s><td><b>rebuild root urls</b></td>"
			 "<td>%s</td></tr>\n" 

			 "<tr bgcolor=#%s>"
			 "<td><b>rebuild non-root urls</b></td>"
			 "<td>%s</td></tr>\n" 

			 "</table>\n"
			 "<br>\n"
			 ,
			 TABLE_STYLE,

			 LIGHT_BLUE,
			 rr[0],

			 LIGHT_BLUE,
			 rr[1],

			 LIGHT_BLUE,
			 rr[3],

			 LIGHT_BLUE,
			 rr[5],

			 LIGHT_BLUE,
			 rr[7],

			 LIGHT_BLUE,
			 rr[9],

			 LIGHT_BLUE,
			 rr[11],

			 LIGHT_BLUE,
			 rr[12] 
			 );
	return true;
}

static bool   s_savingAll = false;

// . return false if blocked, true otherwise
// . will call the callback when all have been saved
// . used by Repair.cpp to save all rdbs before doing repair work
bool saveAllRdbs ( void *state , void (* callback)(void *state) ) {
	// only call once
	if ( s_savingAll ) {
		//log("db: Already saving all.");
		// let them know their callback will not be called even 
		// though we returned false
		if ( callback ) { g_process.shutdownAbort(true); }
		return false;
	}
	// set it
	s_savingAll = true;
	// TODO: why is this called like 100x per second when a merge is
	// going on? why don't we sleep longer in between?
	//bool close ( void *state , 
	//	     void (* callback)(void *state ) ,
	//	     bool urgent ,
	//	     bool exitAfterClosing );

	int32_t nsr;
	Rdb **rdbs = getAllRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// skip if not initialized
		if ( ! rdb->isInitialized() ) continue;
		// save/close it
		rdb->close(NULL,doneSavingRdb,false,false);
	}

	// return if still waiting on one to close
	if ( anyRdbNeedsSave() ) return false;
	// all done
	return true;
}

// return false if one or more is still not closed yet
bool anyRdbNeedsSave ( ) {
	int32_t count = 0;
	int32_t nsr;
	Rdb **rdbs = getAllRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		count += rdb->needsSave();
	}
	if ( count ) return true;
	s_savingAll = false;
	return false;
}

// returns false if waiting on some to save
void doneSavingRdb ( void *state ) {
	if ( ! anyRdbNeedsSave() ) return;
	// all done
	s_savingAll = false;
}
