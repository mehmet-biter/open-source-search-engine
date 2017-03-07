#include "gb-include.h"

#include "Process.h"
#include "Rdb.h"
#include "Clusterdb.h"
#include "Collectiondb.h"
#include "Hostdb.h"
#include "Tagdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Sections.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderLoop.h"
#include "Doledb.h"
#include "Statsdb.h"
#include "JobScheduler.h"
#include "Statistics.h"
#include "PingServer.h"
#include "Dns.h"
#include "Repair.h"
#include "RdbCache.h"
#include "RdbMerge.h"
#include "Spider.h"
#include "HttpServer.h"
#include "Speller.h"
#include "Spider.h"
#include "Profiler.h"
#include "Msg4.h"
#include "Msg5.h"
#include "Wiki.h"
#include "Wiktionary.h"
#include "Proxy.h"
#include "Rebalance.h"
#include "SpiderProxy.h"
#include "PageInject.h"
#include "Timezone.h"
#include "CountryCode.h"
#include "File.h"
#include "Docid2Siteflags.h"
#include "UrlRealtimeClassification.h"
#include "Conf.h"
#include "Mem.h"
#include <sys/statvfs.h>
#include <pthread.h>
#include <fcntl.h>

bool g_inAutoSave;

// for resetAll()
extern void resetPageAddUrl    ( );
extern void resetHttpMime      ( );
extern void reset_iana_charset ( );
extern void resetDomains       ( );
extern void resetEntities      ( );
extern void resetQuery         ( );
extern void resetAbbrTable     ( );
extern void resetUnicode       ( );

// our global instance
Process g_process;

static pthread_t s_mainThreadTid;

static const char * const g_files[] = {
	//"gb.conf",

	//"hosts.conf",

	"ucdata/kd_data.dat",
	"ucdata/kdmap.dat",
	"ucdata/lowermap.dat",
	"ucdata/properties.dat",
	"ucdata/scripts.dat",
	"ucdata/uppermap.dat",

	"gbstart.sh",
	"gbconvert.sh",

	"antiword" ,  // msword
	"pstotext" ,  // postscript

	// required for SSL server support for both getting web pages
	// on https:// sites and for serving https:// pages
	"gb.pem",

	// the main binary!
	"gb",
	
	"antiword-dir/8859-1.txt",
	"antiword-dir/8859-10.txt",
	"antiword-dir/8859-13.txt",
	"antiword-dir/8859-14.txt",
	"antiword-dir/8859-15.txt",
	"antiword-dir/8859-16.txt",
	"antiword-dir/8859-2.txt",
	"antiword-dir/8859-3.txt",
	"antiword-dir/8859-4.txt",
	"antiword-dir/8859-5.txt",
	"antiword-dir/8859-6.txt",
	"antiword-dir/8859-7.txt",
	"antiword-dir/8859-8.txt",
	"antiword-dir/8859-9.txt",
	"antiword-dir/Default",
	"antiword-dir/Example",
	"antiword-dir/MacRoman.txt",
	"antiword-dir/UTF-8.txt",
	"antiword-dir/Unicode",
	"antiword-dir/cp1250.txt",
	"antiword-dir/cp1251.txt",
	"antiword-dir/cp1252.txt",
	"antiword-dir/cp437.txt",
	"antiword-dir/cp850.txt",
	"antiword-dir/cp852.txt",
	"antiword-dir/fontnames",
	"antiword-dir/fontnames.russian",
	"antiword-dir/koi8-r.txt",
	"antiword-dir/koi8-u.txt",
	"antiword-dir/roman.txt",

	// . thumbnail generation
	// . i used 'apt-get install netpbm' to install
	"bmptopnm",
	"giftopnm",
	"jpegtopnm",
	"libjpeg.so.62",
	"libnetpbm.so.10",
	"libpng12.so.0",
	"libtiff.so.4",

	"libcld2_full.so",

	"LICENSE",
	"pngtopnm",
	"pnmscale",
	"ppmtojpeg",
	"tifftopnm",

	"mysynonyms.txt",

	"wikititles.txt.part1",
	"wikititles.txt.part2",

	"wiktionary-buf.txt",
	"wiktionary-lang.txt",
	"wiktionary-syns.dat",

	// gives us siteranks for the most popular sites:
	"sitelinks.txt",

	"unifiedDict.txt",
	
	NULL
};


///////
//
// used to make package to install files for the package.
// so do not include hosts.conf or gb.conf
//
///////
bool Process::getFilesToCopy ( const char *srcDir , SafeBuf *buf ) {

	// sanirty
	int32_t slen = strlen(srcDir);
	if ( srcDir[slen-1] != '/' ) { g_process.shutdownAbort(true); }

	for ( int32_t i = 0 ; i < (int32_t)sizeof(g_files) / (int32_t)sizeof(g_files[0]) ; i++ ) {
		// terminate?
		if ( ! g_files[i] ) break;
		// skip subdir shit it won't work
		if ( strstr(g_files[i],"/") ) continue;
		// if not first
		if ( i > 0 ) buf->pushChar(' ');
		// append it
		buf->safePrintf("%s%s"
				, srcDir
				, g_files[i] );
	}

	// and the required runtime subdirs
	buf->safePrintf(" %santiword-dir",srcDir);
	buf->safePrintf(" %sucdata",srcDir);
	buf->safePrintf(" %shtml",srcDir);

	return true;
}


bool Process::checkFiles ( const char *dir ) {
	// make sure we got all the files
	bool needsFiles = false;

	for ( int32_t i = 0 ; i < (int32_t)sizeof(g_files) / (int32_t)sizeof(g_files[0]) ; i++ ) {
		// terminate?
		if ( ! g_files[i] ) {
			break;
		}

		File f;
		const char *dd = dir;
		if ( g_files[i][0] != '/' ) {
			f.set ( dir , g_files[i] );
		} else {
			f.set ( g_files[i] );
			dd = "";
		}

		if ( ! f.doesExist() ) {
			log(LOG_ERROR, "db: %s%s file missing.", dd, g_files[i]);
			needsFiles = true;
		}
	}

	if ( needsFiles ) {
		log(LOG_ERROR, "db: Missing files. See above. Exiting.");
		return false;
	}

	return true;
}

static void heartbeatWrapper(int fd, void *state);
static void processSleepWrapper(int fd, void *state);
static void diskUsageWrapper(int fd, void *state);
static void reloadDocid2SiteFlags(int fd, void *state);


Process::Process ( ) {
	m_mode = Process::NO_MODE;
	m_exiting = false;
	m_powerIsOn = true;
	m_totalDocsIndexed = -1LL;

	// Coverity
	memset(m_rdbs, 0, sizeof(m_rdbs));
	m_numRdbs = 0;
	m_urgent = false;
	m_lastSaveTime = 0;
	m_processStartTime = 0;
	m_sentShutdownNote = false;
	m_blockersNeedSave = false;
	m_repairNeedsSave = false;
	m_try = 0;
	m_firstShutdownTime = 0;
	m_callbackState = NULL;
	m_callback = NULL;
	m_lastHeartbeatApprox = 0;
	m_suspendAutoSave = false;
	m_powerOffTime = 0;
	m_calledSave = false;
	m_diskUsage = 0.0;
	m_diskAvail = 0;
}

bool Process::init ( ) {
	g_inAutoSave = false;
	s_mainThreadTid = pthread_self();
	// -1 means unknown
	m_diskUsage = -1.0;
	m_diskAvail = -1LL;
	m_powerIsOn = true;
	m_numRdbs = 0;
	m_suspendAutoSave = false;
	// . init the array of rdbs
	// . primary rdbs
	// . let's try to save tfndb first, that is the most important,
	//   followed by titledb perhaps...
	m_rdbs[m_numRdbs++] = g_titledb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_posdb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_spiderdb.getRdb    ();
	m_rdbs[m_numRdbs++] = g_clusterdb.getRdb   (); 
	m_rdbs[m_numRdbs++] = g_tagdb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_statsdb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_linkdb.getRdb      ();

	// save what urls we have been doled
	m_rdbs[m_numRdbs++] = g_doledb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_titledb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_posdb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_spiderdb2.getRdb   ();
	m_rdbs[m_numRdbs++] = g_clusterdb2.getRdb  ();
	m_rdbs[m_numRdbs++] = g_linkdb2.getRdb     ();
	m_rdbs[m_numRdbs++] = g_tagdb2.getRdb      ();
	/////////////////
	// CAUTION!!!
	/////////////////
	// Add any new rdbs to the END of the list above so 
	// it doesn't screw up Rebalance.cpp which uses this list too!!!!
	/////////////////


	//call these back right before we shutdown the
	//httpserver.
	m_callbackState = NULL;
	m_callback      = NULL;

	// do not do an autosave right away
	m_lastSaveTime = 0;//gettimeofdayInMillisecondsLocal();
	// reset this
	m_sentShutdownNote = false;
	// this is used for shutting down as well
	m_blockersNeedSave = true;
	m_repairNeedsSave  = true;
	// count tries
	m_try = 0;
	// reset this timestamp
	m_firstShutdownTime = 0;
	// set the start time, local time
	m_processStartTime = gettimeofdayInMillisecondsLocal();
	// reset this
	m_lastHeartbeatApprox = 0;
	m_calledSave = false;

	// heartbeat check
	if ( ! g_loop.registerSleepCallback(100,NULL,heartbeatWrapper,0)) {
		return false;
	}

	// get first snapshot of load average...
	//update_load_average(gettimeofdayInMillisecondsLocal());

	// . continually call this once per second
	// . once every half second now so that autosaves are closer together
	//   in time between all hosts
	if ( ! g_loop.registerSleepCallback(500,NULL,processSleepWrapper)) {
		return false;
	}

	if (!g_loop.registerSleepCallback(10000, NULL, diskUsageWrapper, 0)) {
		return false;
	}

	if (!g_loop.registerSleepCallback(60000, NULL, reloadDocid2SiteFlags, 0)) {
		return false;
	}

	// success
	return true;
}

bool Process::isAnyTreeSaving ( ) {
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->isCollectionless() ) continue;
		if ( rdb->isSavingTree() ) return true;
		// we also just disable writing below in Process.cpp
		// while saving other files. so hafta check that as well
		// since we use isAnyTreeSaving() to determine if we can
		// write to the tree or not.
		if ( ! rdb->isWritable() ) return true;
	}
	return false;
}

// set Process::m_diskUsage
static float getDiskUsage ( int64_t *diskAvail ) {
	struct statvfs s;
	if(statvfs(g_hostdb.m_dir,&s)!=0) {
		log(LOG_WARN, "build: statvfs(%s) failed: %s.", g_hostdb.m_dir, mstrerror(errno));
		return -1;
	}
	
	*diskAvail = s.f_bavail * s.f_frsize;
	return (s.f_blocks-s.f_bavail)*100.0/s.f_blocks;
}

void diskUsageWrapper(int /*fd*/, void * /*state*/) {
	// skip if exiting
	if ( g_process.m_mode == Process::Process::EXIT_MODE ) {
		return;
	}

	g_process.m_diskUsage = getDiskUsage( &g_process.m_diskAvail );
}

void Process::callHeartbeat () {
	heartbeatWrapper ( 0 , NULL );
}

void heartbeatWrapper(int /*fd*/, void * /*state*/) {
	static int64_t s_last = 0LL;
	int64_t now = gettimeofdayInMilliseconds();
	if ( s_last == 0LL ) {
		s_last = now;
		return;
	}
	// . log when we've gone 100+ ms over our scheduled beat
	// . this is a sign things are jammed up
	int64_t elapsed = now - s_last;
	if ( elapsed > 200 ) {
		log( LOG_WARN, "heartbeatWrapper: elapsed=%" PRId64 "ms - is main thread slow?", elapsed);
	}
	s_last = now;

	// save this time so the sig alarm handler can see how long
	// it has been since we've been called, so after 10000 ms it
	// can dump core and we can see what is holding things up
	g_process.m_lastHeartbeatApprox = gettimeofdayInMilliseconds();
}


static void reloadDocid2SiteFlags(int fd, void *state) {
	g_d2fasm.reload_if_needed();
}


// called by PingServer.cpp only as of now
int64_t Process::getTotalDocsIndexed() {
	if ( m_totalDocsIndexed == -1LL ) {
		Rdb *rdb = g_clusterdb.getRdb();
		// useCache = true
		m_totalDocsIndexed = rdb->getNumTotalRecs(true);
	}
	return m_totalDocsIndexed;
}

void processSleepWrapper(int /*fd*/, void * /*state*/) {

	if ( g_process.m_mode == Process::Process::EXIT_MODE ) {
		g_process.shutdown2();
		return;
	}

	if ( g_process.m_mode == Process::SAVE_MODE ) {
		g_process.save2();
		return;
	}

	if ( g_process.m_mode == Process::LOCK_MODE ) {
		g_process.save2();
		return;
	}

	if ( g_process.m_mode != Process::NO_MODE ) {
		return;
	}

	// update global rec count
	static int32_t s_rcount = 0;

	// every 2 seconds
	if ( ++s_rcount >= 4 ) {
		s_rcount = 0;
		// PingServer.cpp uses this
		Rdb *rdb = g_clusterdb.getRdb();
		g_process.m_totalDocsIndexed = rdb->getNumTotalRecs();
	}

	// do not do autosave if no power
	if ( ! g_process.m_powerIsOn ) return;

	// . i guess try to autoscale the cluster in cast hosts.conf changed
	// . if all pings came in and all hosts have the same hosts.conf
	//   and if we detected any shard imbalance at startup we have to
	//   scan all rdbs for records that don't belong to us and send them
	//   where they should go
	// . returns right away in most cases
	g_rebalance.rebalanceLoop();

	// in PageInject.cpp startup up any imports that might have been
	// going on before we shutdown last time. 
	resumeImports();

	// if doing the final part of a repair.cpp loop where we convert
	// titledb2 files to titledb etc. then do not save!
	if ( g_repairMode == 7 ) return;

	// autosave? override this if power is off, we need to save the data!
	if ( g_conf.m_autoSaveFrequency <= 0 ) return;

	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return;

	// skip autosave while sync in progress!
	if ( g_process.m_suspendAutoSave ) return;

	// need to have a clock unified with host #0. i guess proxy
	// does not sync with host #0 though
	//if ( ! isClockInSync() && ! g_hostdb.m_myHost->m_isProxy ) return;

	// get time the day started
	int32_t now;
	if ( g_hostdb.m_myHost->m_isProxy ) {
		now = getTimeLocal();
	} else {
		// need to be in sync with host #0's clock
		if ( ! isClockInSync() ) {
			return;
		}
		// that way autosaves all happen at about the same time
		now = getTimeGlobal();
	}

	// set this for the first time
	if ( g_process.m_lastSaveTime == 0 )
		g_process.m_lastSaveTime = now;
	
	//
	// we now try to align our autosaves with start of the day so that
	// all hosts autosave at the exact same time!! this should keep
	// performance somewhat consistent.
	//
	
	// get frequency in minutes
	int32_t freq = (int32_t)g_conf.m_autoSaveFrequency ;
	// convert into seconds
	freq *= 60;
	// how many seconds into the day has it been?
	int32_t offset   = now % (24*3600);
	int32_t dayStart = now - offset;
	// how many times should we have autosaved so far for this day?
	int32_t autosaveCount = offset / freq;
	// convert to when it should have been last autosaved
	int32_t nextLastSaveTime = (autosaveCount * freq) + dayStart;
	
	// if we already saved it for that time, bail
	if ( g_process.m_lastSaveTime >= nextLastSaveTime ) return;
	
	//int64_t now = gettimeofdayInMillisecondsLocal();
	// . get a snapshot of the load average...
	// . MDW: disable for now. not really used...
	//update_load_average(now);
	// convert from minutes in milliseconds
	//int64_t delta = (int64_t)g_conf.m_autoSaveFrequency * 60000LL;
	// if power is off make this every 30 seconds temporarily!
	//if ( ! g_process.m_powerIsOn ) delta = 30000;
	// return if we have not waited int32_t enough
	//if ( now - g_process.m_lastSaveTime < delta ) return;
	// update
	g_process.m_lastSaveTime = nextLastSaveTime;//now;
	// save everything
	logf(LOG_INFO,"db: Autosaving.");
	g_inAutoSave = true;
	g_process.save();
	g_inAutoSave = false;
}

bool Process::save ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	// bail if doing something already
	if ( m_mode != NO_MODE ) return true;
	// log it
	logf(LOG_INFO,"db: Entering lock mode for saving.");
	m_mode   = Process::LOCK_MODE; // Process::SAVE_MODE;
	m_urgent = false;
	m_calledSave = false;
	return save2();
}

void Process::shutdownAbort ( bool save_on_abort ) {
	bool force_abort = false;

	// write fatal_error file
	int fd = open(getAbortFileName(), O_WRONLY | O_CREAT, getFileCreationFlags() );
	if ( !fd ) {
		log(LOG_ERROR, "process: Unable to create file '%s'", getAbortFileName());

		// we can't create file, so make sure we exit gb restart loop
		force_abort = true;
	}

	printStackTrace(true);

	// follow usual segfault logic, only when required & we can create file
	// if we can't create file, and we follow the usual logic, we risk not blocking startup
	if ( save_on_abort && !force_abort ) {
		shutdown(true);
	}

	abort();
}

bool Process::shutdown ( bool urgent, void  *state, void (*callback) (void *state )) {
	// bail if doing something already
	if ( m_mode != NO_MODE ) {
		// if already in exit mode, just return
		if ( m_mode == Process::Process::EXIT_MODE ) {
			return true;
		}

		// otherwise, log it!
		log("process: shutdown called, but mode is %" PRId32, (int32_t)m_mode);

		return true;
	}

	m_mode   = Process::Process::EXIT_MODE;
	m_urgent = urgent;

	m_calledSave = false;

	// check memory buffers for overruns/underrunds to see if that caused this core
	if ( urgent ) {
		g_mem.printBreeches();
	}

	if (!shutdown2()) {
		m_callbackState = state;
		m_callback = callback;
		return false;
	}

	return true;
}

// return false if blocked/waiting
bool Process::save2 ( ) {
	// only the main process can call this
	if ( pthread_self() != s_mainThreadTid ) {
		return true;
	}

	// . wait for any dump to complete
	// . when merging titldb, it sets Rdb::m_dump.m_isDumping to true
	//   because it is dumping the results of the merge to a file.
	//   occasionally it will initiate a dump of tfndb which will not be 
	//   possible because Rdb/RdbDump checks g_process.m_mode == Process::SAVE_MODE,
	//   and do not allow dumps to begin if that is true! so we end up in 
	//   deadlock! the save can not complete 
	if ( isRdbDumping() ) {
		return false;
	}

	// ok, now nobody is dumping, etc. make it so no dumps can start.
	// Rdb.cpp/RdbDump.cpp check for this and will not dump if it is 
	// set to Process::SAVE_MODE
	m_mode = Process::SAVE_MODE;

	logf(LOG_INFO,"gb: Saving data to disk. Disabling writes.");

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ETRYAGAIN error replies
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites( false );

	bool useThreads = true;

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if (!saveRdbTrees(useThreads, false)) {
		return false;
	}

	if (!saveRdbIndexes()) {
		return false;
	}

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	// . save these last since maps can be auto-regenerated at startup
	if (!saveRdbMaps()) {
		return false;
	}

	// . save the conf files and caches. these block the cpu.
	// . save these first since more important than the stuff below
	// . no, to avoid saving multiple times, put this last since the
	//   stuff above may block and we have to re-call this function      
	if ( ! saveBlockingFiles1() ) return false;

	// save addsInProgress.dat etc. if power goes off. this should be the
	// one time we are called from power going off... since we do not
	// do autosave when the power is off. this just blocks and never
	// returns false, so call it with checking the return value.
	if ( ! g_process.m_powerIsOn ) {
		saveBlockingFiles2() ;
	} else if ( g_jobScheduler.are_new_jobs_allowed() ) {
		saveBlockingFiles2() ;
	}

	// reenable tree writes since saves were completed
	enableTreeWrites( false );

	log(LOG_INFO,"gb: Saved data to disk. Re-enabling Writes.");

	// update
	//m_lastSaveTime = gettimeofdayInMillisecondsLocal();

	// unlock
	m_mode = Process::NO_MODE;

	return true;
}



// . return false if blocked/waiting
// . this is the SAVE BEFORE EXITING
bool Process::shutdown2() {

	// only the main process can call this
	if ( pthread_self() != s_mainThreadTid ) {
		return true;
	}

	if ( m_urgent ) {
		log(LOG_INFO,"gb: Shutting down urgently. Timed try #%" PRId32".", m_try++);
	} else {
		log(LOG_INFO,"gb: Shutting down. Timed try #%" PRId32".", m_try++);
	}

	// switch to urgent if having problems
	if ( m_try >= 10 ) {
		m_urgent = true;
	}

	// turn off statsdb so it does not try to add records for these writes
	g_statsdb.m_disabled = true;

	finalizeRealtimeUrlClassification(),

	Statistics::finalize();

	log("gb: disabling threads");
	// now disable threads so we don't exit while threads are
	// outstanding
	g_jobScheduler.disallow_new_jobs();

	// Stop merging
	g_merge.haltMerge();

	static bool s_printed = false;

	// wait for all threads to return
	bool b = g_jobScheduler.are_io_write_jobs_running();
	if ( b && ! m_urgent ) {
		log(LOG_INFO,"gb: Has write threads out. Waiting for them to finish.");
		return false;
	} else if ( ! s_printed && ! m_urgent ) {
		s_printed = true;
		log(LOG_INFO,"gb: No write threads out.");
	}

	// disable all spidering
	// we can exit while spiders are in the queue because
	// if they are in the middle of being added they will be
	// saved by spider restore
	// wait for all spiders to clear

	// don't shut the crawler down on a core
	//g_conf.m_spideringEnabled = false;

	//g_conf.m_injectionEnabled = false;

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ECLOSING error msgs
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites( true );

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if (!saveRdbTrees(false, true)) {
		if (!m_urgent) {
			return false;
		}
	}

	if (!saveRdbIndexes()) {
		if (!m_urgent) {
			return false;
		}
	}

	// save this right after the trees in case we core
	// in saveRdbMaps() again due to the core we are
	// handling now corrupting memory
	if ( m_repairNeedsSave ) {
		m_repairNeedsSave = false;
		g_repair.save();
	}

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	if (!saveRdbMaps()) {
		if (!m_urgent) {
			return false;
		}
	}

	int64_t now = gettimeofdayInMillisecondsLocal();
	if ( m_firstShutdownTime == 0 ) {
		m_firstShutdownTime = now;
	}

	// these udp servers will not read in new requests or allow
	// new requests to be sent. they will timeout any outstanding
	// UdpSlots, and when empty they will return true here. they will
	// close their m_sock and set it to -1 which should force their
	// thread to exit.
	// if not urgent, they will wait for a while for the 
	// sockets/slots to clear up.
	// however, if 5 seconds or more have elapsed then force it
	bool udpUrgent = m_urgent;
	if ( now - m_firstShutdownTime >= 3000 ) udpUrgent = true;

	if ( ! g_dns.getUdpServer().shutdown ( udpUrgent ) )
		if ( ! udpUrgent ) return false;

	// . send notes to all the hosts in the network telling them we're
	//   shutting down
	// . this returns false if it blocks
	// . we don't care if it blocks or not
	// . don't bother asking the hosts to send an email alert for us
	//   since we're going down gracefully by letting everyone know
	// . don't send this unless we are very sure we can shutdown NOW
	// . i.e. no blocking after this call!
	if ( ! m_sentShutdownNote && ! m_urgent ) {
		log(LOG_INFO,"gb: Broadcasting shutdown notice.");
		m_sentShutdownNote = true;
		g_pingServer.broadcastShutdownNotes ( false, NULL, NULL );
	}

	//broadcastShutdownNotes uses g_udpServer so we do this last.
	if ( ! g_udpServer.shutdown ( udpUrgent ) ) {
		if ( !udpUrgent ) {
			return false;
		}
	}

	g_profiler.stopRealTimeProfiler(false);
	g_profiler.cleanup();

	// save the conf files and caches. these block the cpu.
	if ( m_blockersNeedSave ) {
		m_blockersNeedSave = false;
		if ( !g_conf.m_readOnlyMode ) {
			logf( LOG_INFO, "gb: Saving miscellaneous data files." );
		}
		saveBlockingFiles1() ;
		saveBlockingFiles2() ;
	}

	// always disable threads at this point so g_jobScheduler.submit() will
	// always return false and we do not queue any new jobs for spawning
	g_jobScheduler.disallow_new_jobs();

	// urgent means we need to dump core, SEGV or something
	if ( m_urgent ) {
		// log it
		log( LOG_WARN, "gb: Dumping core after saving." );

		// at least destroy the page caches that have shared memory
		// because they seem to not clean it up
		//resetPageCaches();

		abort();
	}


	// cleanup threads, this also launches them too
	g_jobScheduler.cleanup_finished_jobs();


	//ok, resetAll will close httpServer's socket so now is the time to 
	//call the callback.
	if ( m_callbackState ) {
		( *m_callback )( m_callbackState );
	}

	// tell Mutlicast::reset() not to destroy all the slots! that cores!
	m_exiting = true;

	// let everyone free their mem
	resetAll();

	// show what mem was not freed
	g_mem.printMem();

	log("gb. EXITING GRACEFULLY.");

	// make a file called 'cleanexit' so bash keep alive loop will stop
	// because bash does not get the correct exit code, 0 in this case,
	// even though we explicitly say 'exit(0)' !!!! poop
	StackBuf<128> cleanFileName;
	cleanFileName.safePrintf("%s/cleanexit",g_hostdb.m_dir);
	SafeBuf nothing;
	// returns # of bytes written, -1 if could not create file
	if ( nothing.save ( cleanFileName.getBufStart() ) == -1 ) {
		log( LOG_WARN, "gb: could not create %s", cleanFileName.getBufStart() );
	}

	// exit abruptly. Yes, _exit() and not plain exit(). The reason is that the above cleanup is not complete.
	// Eg. some globals can ahve a Msg5 which can have a Msg3 which may believe they have outstanding RdbScans
	// and thus core dump in sanity checks.
	_exit(0);
}


void Process::disableTreeWrites ( bool shuttingDown ) {
	// loop over all Rdbs
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		// if we save doledb while spidering it screws us up
		// because Spider.cpp can not directly write into the
		// rdb tree and it expects that to always be available!
		if ( ! shuttingDown && rdb->getRdbId() == RDB_DOLEDB )
			continue;
		rdb->disableWrites();
	}
	// don't save spider related trees if not shutting down
	if ( ! shuttingDown ) return;
	// disable all spider trees and tables
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(i);
		if ( ! sc ) {
			continue;
		}
		sc->m_waitingTree .disableWrites();
		sc->m_waitingTable.disableWrites();
		sc->m_doleIpTable .disableWrites();
	}
	
}

void Process::enableTreeWrites ( bool shuttingDown ) {
	// loop over all Rdbs
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->enableWrites();
	}
	// don't save spider related trees if not shutting down
	if ( ! shuttingDown ) return;
	// enable all waiting trees
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->m_waitingTree .enableWrites();
		sc->m_waitingTable.enableWrites();
		sc->m_doleIpTable .enableWrites();
	}
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::isRdbDumping ( ) {
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->isDumping() ) return true;
	}
	return false;
}

bool Process::isRdbMerging ( ) {
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->isMerging() ) return true;
	}
	return false;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbTrees ( bool useThread , bool shuttingDown ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;

	// no thread if shutting down
	if ( shuttingDown ) {
		useThread = false;
		log("gb: trying to shutdown");
	}

	// turn off statsdb until everyone is done
	//g_statsdb.m_disabled = true;

	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		if ( m_calledSave ) {
			log("gb: already saved trees, skipping.");
			break;
		}

		Rdb *rdb = m_rdbs[i];

		// if we save doledb while spidering it screws us up
		// because Spider.cpp can not directly write into the
		// rdb tree and it expects that to always be available!
		if ( ! shuttingDown && rdb->getRdbId() == RDB_DOLEDB ) {
			continue;
		}

		// note it
		if ( rdb->getDbname() ) {
			log( "gb: calling save tree for %s", rdb->getDbname() );
		} else {
			log( "gb: calling save tree for rdbid %i", ( int ) rdb->getRdbId() );
		}

		rdb->saveTree(useThread);
		rdb->saveTreeIndex(useThread);
	}

	// . save waitingtrees for each collection, blocks.
	// . can we make this non-blocking?
	// . true = "usethreads"
	// . all writes have been disabled, so should be cleanly saved
	// . if this did not block that means it does not need any saving
	// . this just launched all the write threads for the trees/tables
	//   that need to be saved. it sets m_isSaving once they are all 
	//   launched.
	// . and sets m_isSaving=false on SpiderCache::doneSaving when they
	//   are all done.
	if ( shuttingDown ) g_spiderCache.save ( useThread );

	// do not re-save the stuff we just did this round
	m_calledSave = true;
	// quickly re-enable if statsdb tree does not need save any more
	//if ( ! g_statsdb.m_rdb.needsSave() ) g_statsdb.m_disabled = false;
	// check if any need to finish saving
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		// do not return until all saved if we are shutting down
		if ( shuttingDown ) {
			break;
		}

		Rdb *rdb = m_rdbs[i];

		//if ( rdb->needsSave ( ) ) return false;

		// we disable the tree while saving so we can't really add recs
		// to one rdb tree while saving, but for crawlbot
		// we might have added or deleted collections.
		if ( rdb->isSavingTree ( ) ) {
			return false;
		}
	}

	// only save spiderdb based trees if shutting down so we can
	// still write to them without writes being disabled
	if ( ! shuttingDown ) {
		return true;
	}

	// reset for next call
	m_calledSave = false;

	// everyone is done saving
	return true;
}

bool Process::saveRdbIndexes() {
	// never if in read only mode
	if (g_conf.m_readOnlyMode) {
		return true;
	}

	// loop over all Rdbs and save them
	for (int32_t i = 0; i < m_numRdbs; i++) {
		Rdb *rdb = m_rdbs[i];
		rdb->saveIndexes();
	}

	// everyone is done saving
	return true;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbMaps() {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->saveMaps();
	}

	// everyone is done saving
	return true;
}

bool Process::saveBlockingFiles1 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;

	// save the gb.conf file now
	g_conf.save();
	// save the conf files
	// if autosave and we have over 20 colls, just make host #0 do it
        g_collectiondb.save();
	// . save repair state
	// . this is repeated above too
	// . keep it here for auto-save
	g_repair.save();

	// save our place during a rebalance
	g_rebalance.saveRebalanceFile();

	// save stats on spider proxies if any
	saveSpiderProxyStats();

	// . save the add state from Msg4.cpp
	// . these are records in the middle of being added to rdbs across
	//   the cluster
	// . saves to "addsinprogress.saving" and moves to .saved
	// . eventually this may replace "spiderrestore.dat"
	if (g_repair.isRepairActive()) {
		saveAddsInProgress("repair-");
	} else {
		saveAddsInProgress(NULL);
	}

	// in fctypes.cpp. save the clock offset from host #0's clock so
	// our startup is fast again
	saveTimeAdjustment();

	return true;
}

bool Process::saveBlockingFiles2 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	// the robots.txt cache
	Msg13::getHttpCacheRobots()->save();

	// save dns caches
	RdbCache *c = g_dns.getCache();
	if (c && c->useDisk()) {
		c->save();
	}

	// if doing titlerec imports in PageInject.cpp, save cursors,
	// i.e. file offsets
	saveImportStates();

	return true;
}

void Process::resetAll ( ) {
	g_log             .reset();
	g_hostdb          .reset();
	g_spiderLoop      .reset();

	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->reset();
	}

	g_collectiondb    .reset();
	g_dns             .reset();
	g_udpServer       .reset();
	g_httpServer      .reset();
	g_loop            .reset();
	g_speller         .reset();
	g_spiderCache     .reset();
	g_jobScheduler    .finalize();
	g_ucUpperMap      .reset();
	g_ucLowerMap      .reset();
	g_ucProps         .reset();
	g_ucScripts       .reset();
	g_profiler        .reset();

	// reset disk page caches
	resetPageCaches();

	// termfreq cache in Posdb.cpp
	g_termFreqCache.reset();

	g_wiktionary.reset();

	g_countryCode.reset();

	s_clusterdbQuickCache.reset();
	s_hammerCache.reset();

	resetDecompTables();
	resetPageAddUrl();
	resetHttpMime();
	reset_iana_charset();
	resetDomains();
	resetEntities();
	resetQuery();
	resetAbbrTable();
	resetUnicode();

	// reset other caches
	g_dns.reset();
	g_spiderCache.reset();
	g_spiderLoop.reset();
	g_wiki.reset();
	g_profiler.reset();
	resetMsg13Caches();
	resetStopWordTables();
	resetTimezoneTables();
}

#include "Msg3.h"

void Process::resetPageCaches ( ) {
	log("gb: Resetting page caches.");
	for ( rdbid_t i = RDB_NONE; i < RDB_END; i = (rdbid_t)((int)i+1) ) {
		RdbCache *rpc = getDiskPageCache ( i );
		if ( ! rpc ) continue;
		rpc->reset();
	}
}

// ============================================================================
double Process::getLoadAvg() {
	//todo: obtain load averages from /proc/loadavg
	//(original code was disabled)
	return 0.0;
}

//
// ============================================================================

// make sure ntpd is running, we can't afford to get our clock
// out of sync for credit card transactions
bool Process::checkNTPD ( ) {

	if ( ! g_conf.m_isLive ) {
		return true;
	}

	FILE *pd = popen("ps auxww | grep ntpd | grep -v grep","r");
	if ( ! pd ) {
		log("gb: failed to ps auxww ntpd");
		if ( ! g_errno ) {
			g_errno = EBADENGINEER;
		}
		return false;
	}

	char tmp[1024];
	char *ss = fgets ( tmp , 1000 , pd );
	pclose(pd);

	if ( ! ss ) {
		log("gb: failed to ps auxww ntpd 2");
		if ( ! g_errno ) {
			g_errno = EBADENGINEER;
		}
		return false;
	}

	// must be there
	if ( ! strstr ( tmp,"ntpd") ) {
		log("gb: all proxies must have ntpd running! this one does not!");
		if ( ! g_errno ) {
			g_errno = EBADENGINEER;
		}
		return false;
	}
	return true;
}



