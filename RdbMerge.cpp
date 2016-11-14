#include "RdbMerge.h"
#include "Rdb.h"
#include "Process.h"
#include "Spider.h" //dedupSpiderdbList()
#include "MergeSpaceCoordinator.h"
#include "Conf.h"


RdbMerge::RdbMerge()
  : m_mergeSpaceCoordinator(NULL),
    m_doneMerging(false),
    m_getListOutstanding(false),
    m_startFileNum(0),
    m_numFiles(0),
    m_fixedDataSize(0),
    m_targetFile(NULL),
    m_targetMap(NULL),
    m_targetIndex(NULL),
    m_isMerging(false),
    m_isHalted(false),
    m_dump(),
    m_msg5(),
    m_list(),
    m_niceness(0),
    m_rdbId(RDB_NONE),
    m_collnum(0),
    m_ks(0)
{
	memset(m_startKey, 0, sizeof(m_startKey));
	memset(m_endKey, 0, sizeof(m_endKey));
}

RdbMerge::~RdbMerge() {
	delete m_mergeSpaceCoordinator;
}



// . return false if blocked, true otherwise
// . sets g_errno on error
// . if niceness is 0 merge will block, otherwise will not block
// . we now use niceness of 1 which should spawn threads that don't allow
//   niceness 2 threads to launch while they're running
// . spider process now uses mostly niceness 2 
// . we need the merge to take priority over spider processes on disk otherwise
//   there's too much contention from spider lookups on disk for the merge
//   to finish in a decent amount of time and we end up getting too many files!
bool RdbMerge::merge(rdbid_t rdbId,
                     collnum_t collnum,
                     BigFile *targetFile,
                     RdbMap *targetMap,
                     RdbIndex *targetIndex,
                     int32_t startFileNum,
                     int32_t numFiles,
                     int32_t niceness)
{
	if(m_isHalted) {
		logTrace(g_conf.m_logTraceRdbBase, "END, merging is halted");
		return true;
	}
	if(m_isMerging) {
		logTrace(g_conf.m_logTraceRdbBase, "END, already merging");
		return true;
	}

	if(!m_mergeSpaceCoordinator) {
		const char *mergeSpaceDir = strlen(g_hostdb.m_myHost->m_mergeDir) > 0 ? g_hostdb.m_myHost->m_mergeDir : g_conf.m_mergespaceDirectory;
		m_mergeSpaceCoordinator = new MergeSpaceCoordinator(g_conf.m_mergespaceLockDirectory, g_conf.m_mergespaceMinLockFiles, mergeSpaceDir);
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(rdbId, collnum);
	if (!base) {
		return true;
	}

	Rdb *rdb = getRdbFromId(rdbId);

	m_rdbId           = rdbId;
	m_collnum         = rdb->isCollectionless() ? 0 : collnum;
	m_targetFile      = targetFile;
	m_targetMap       = targetMap;
	m_targetIndex     = targetIndex;
	m_startFileNum    = startFileNum;
	m_numFiles        = numFiles;
	m_fixedDataSize   = base->getFixedDataSize();
	m_niceness        = niceness;
	m_doneMerging     = false;
	m_ks              = rdb->getKeySize();

	// . set the key range we want to retrieve from the files
	// . just get from the files, not tree (not cache?)
	KEYMIN(m_startKey,m_ks);
	KEYMAX(m_endKey,m_ks);

	// if we're resuming a killed merge, set m_startKey to last
	// key the map knows about.
	// the dump will start dumping at the end of the targetMap's data file.
	if ( m_targetMap->getNumRecs() > 0 ) {
		log(LOG_INIT,"db: Resuming a killed merge.");
		m_targetMap->getLastKey(m_startKey);
		KEYINC(m_startKey,m_ks);
	}

	//calculate how much space we need for resulting merged file
	m_spaceNeededForMerge = base->getSpaceNeededForMerge(m_startFileNum,m_numFiles);
	
	if(!g_loop.registerSleepCallback(5000, this, getLockWrapper, 0, true))
		return true;

	// we're now merging since we accepted to try
	m_isMerging = true;

	return false;
}


void RdbMerge::getLockWrapper(int /*fd*/, void *state) {
	log(LOG_TRACE,"RdbMerge::getLockWrapper(%p)",state);
	RdbMerge *that = static_cast<RdbMerge*>(state);
	that->getLock();
}


void RdbMerge::getLock() {
	log(LOG_DEBUG,"Rdbmerge(%p)::getLock(), m_rdbId=%d",this,m_rdbId);
	if(m_mergeSpaceCoordinator->acquire(m_spaceNeededForMerge)) {
		log(LOG_INFO,"Rdbmerge(%p)::getLock(), m_rdbId=%d: got lock for %" PRIu64 " bytes", this, m_rdbId, m_spaceNeededForMerge);
		g_loop.unregisterSleepCallback(this,getLockWrapper);
		gotLock();
	} else
		log(LOG_INFO,"Rdbmerge(%p)::getLock(), m_rdbId=%d: Didn't get lock for %" PRIu64 " bytes; retrying in a bit...", this, m_rdbId, m_spaceNeededForMerge);
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
bool RdbMerge::gotLock() {
	// . get last mapped offset
	// . this may actually be smaller than the file's actual size
	//   but the excess is not in the map, so we need to do it again
	int64_t startOffset = m_targetMap->getFileSize();

	// if startOffset is > 0 use the last key as RdbDump:m_prevLastKey
	// so it can compress the next key it dumps providee m_useHalfKeys
	// is true (key compression) and the next key has the same top 6 bytes
	// as m_prevLastKey
	char prevLastKey[MAX_KEY_BYTES];
	if (startOffset > 0) {
		m_targetMap->getLastKey(prevLastKey);
	} else {
		KEYMIN(prevLastKey, m_ks);
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(m_rdbId, m_collnum);
	if (!base) {
		relinquishMergespaceLock();
		m_isMerging = false;
		//no need for calling incorporateMerge() because the base/collection is cone
		return true;
	}

	// . set up a a file to dump the records into
	// . returns false and sets g_errno on error
	// . this will open m_target as O_RDWR | O_NONBLOCK | O_ASYNC ...

	m_dump.set(m_collnum,
	           m_targetFile,
	           NULL, // buckets to dump is NULL, we call dumpList
	           NULL, // tree to dump is NULL, we call dumpList
	           NULL,
	           m_targetMap,
	           m_targetIndex,
	           0, // m_maxBufSize. not needed if no tree!
	           m_niceness, // niceness of dump
	           this, // state
	           dumpListWrapper,
	           base->useHalfKeys(),
	           startOffset,
	           prevLastKey,
	           m_ks,
	           NULL);
	// what kind of error?
	if ( g_errno ) {
		log(LOG_WARN, "db: gotLock: merge.set: %s.", mstrerror(g_errno));
		relinquishMergespaceLock();
		m_isMerging = false;
		base->incorporateMerge();
		return true;
	}

	// . this returns false on error and sets g_errno
	// . it returns true if blocked or merge completed successfully
	return resumeMerge ( );
}

void RdbMerge::haltMerge() {
	if(m_isHalted) {
		return;
	}

	m_isHalted = true;

	// . we don't want the dump writing to an RdbMap that has been deleted
	// . this can happen if the close is delayed because we are dumping
	//   a tree to disk
	m_dump.setSuspended();
}

void RdbMerge::doSleep() {
	log(LOG_WARN, "db: Merge had error: %s. Sleeping and retrying.", mstrerror(g_errno));
	g_errno = 0;
	g_loop.registerSleepCallback(1000, this, tryAgainWrapper);
}

// . return false if blocked, otherwise true
// . sets g_errno on error
bool RdbMerge::resumeMerge() {
	if(m_isHalted) {
		return true;
	}

	// the usual loop
	for (;;) {
		// . this returns false if blocked, true otherwise
		// . sets g_errno on error
		// . we return true if it blocked
		if (!getNextList()) {
			return false;
		}

		// if g_errno is out of memory then msg3 wasn't able to get the lists
		// so we should sleep and retry...
		if (g_errno == ENOMEM) {
			doSleep();
			return false;
		}

		// if list is empty or we had an error then we're done
		if (g_errno || m_doneMerging) {
			doneMerging();
			return true;
		}

		// . otherwise dump the list we read to our target file
		// . this returns false if blocked, true otherwise
		if (!dumpList()) {
			return false;
		}
	}
}

// . return false if blocked, true otherwise
// . sets g_errno on error
bool RdbMerge::getNextList() {
	// return true if g_errno is set
	if (g_errno || m_doneMerging) {
		return true;
	}

	// it's suspended so we count this as blocking
	if(m_isHalted) {
		return false;
	}

	// if the power is off, suspend the merging
	if (!g_process.m_powerIsOn) {
		doSleep();
		return false;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(m_rdbId, m_collnum);
	if (!base) {
		// hmmm it doesn't set g_errno so we set it here now
		// otherwise we do an infinite loop sometimes if a collection
		// rec is deleted for the collnum
		g_errno = ENOCOLLREC;
		return true;
	}

	// otherwise, get it now
	return getAnotherList();
}

bool RdbMerge::getAnotherList() {
	log(LOG_DEBUG,"db: Getting another list for merge.");

	// clear it up in case it was already set
	g_errno = 0;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(m_rdbId, m_collnum);
	if (!base) {
		return true;
	}

	// if merging titledb files, we must adjust m_endKey so we do
	// not have to read a huge 200MB+ tfndb list
	char newEndKey[MAX_KEY_BYTES];
	KEYSET(newEndKey,m_endKey,m_ks);

	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . we return false if it blocked
	// . m_maxBufSize may be exceeded by a rec, it's just a target size
	// . niceness is usually MAX_NICENESS, but reindex.cpp sets to 0
	// . this was a call to Msg3, but i made it call Msg5 since
	//   we now do the merging in Msg5, not in msg3 anymore
	// . this will now handle truncation, dup and neg rec removal
	// . it remembers last termId and count so it can truncate even when
	//   IndexList is split between successive reads
	// . IMPORTANT: when merging titledb we could be merging about 255
	//   files, so if we are limited to only X fds it can have a cascade
	//   affect where reading from one file closes the fd of another file
	//   in the read (since we call open before spawning the read thread)
	//   and can therefore take 255 retries for the Msg3 to complete 
	//   because each read gives a EFILCLOSED error.
	//   so to fix it we allow one retry for each file in the read plus
	//   the original retry of 25
	int32_t nn = base->getNumFiles();
	if ( m_numFiles > 0 && m_numFiles < nn ) nn = m_numFiles;

	int32_t bufSize = g_conf.m_mergeBufSize;
	// get it
	m_getListOutstanding = true;
	bool rc = m_msg5.getList(m_rdbId,
				 m_collnum,
				 &m_list,
				 m_startKey,
				 newEndKey,       // usually is maxed!
				 bufSize,
				 false,           // includeTree?
				 0,               // max cache age for lookup
				 m_startFileNum,  // startFileNum
				 m_numFiles,
				 this,            // state
				 gotListWrapper,  // callback
				 m_niceness,      // niceness
				 true,            // do error correction?
				 NULL,            // cache key ptr
				 0,               // retry #
				 nn + 75,         // max retries (mk it high)
				 -1LL,            // sync point
				 true,            // isRealMerge? absolutely!
				 false);
	if(rc)
		m_getListOutstanding = false;
	return rc;
	
}

void RdbMerge::gotListWrapper(void *state, RdbList *list, Msg5 *msg5) {
	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;

	THIS->m_getListOutstanding = false;

	for (;;) {
		// if g_errno is out of memory then msg3 wasn't able to get the lists
		// so we should sleep and retry
		if (g_errno == ENOMEM) {
			THIS->doSleep();
			return;
		}

		// if g_errno we're done
		if (g_errno || THIS->m_doneMerging) {
			THIS->doneMerging();
			return;
		}

		// return if this blocked
		if (!THIS->dumpList()) {
			return;
		}

		// return if this blocked
		if (!THIS->getNextList()) {
			return;
		}

		// otherwise, keep on trucking
	}
}

// called after sleeping for 1 sec because of ENOMEM
void RdbMerge::tryAgainWrapper(int fd, void *state) {
	// if power is still off, keep things suspended
	if (!g_process.m_powerIsOn) {
		return;
	}

	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;

	// unregister the sleep callback
	g_loop.unregisterSleepCallback(THIS, tryAgainWrapper);

	// clear this
	g_errno = 0;

	// return if this blocked
	if (!THIS->getNextList()) {
		return;
	}

	// if this didn't block do the loop
	gotListWrapper(THIS, NULL, NULL);
}
		
// similar to gotListWrapper but we call getNextList() before dumpList()
void RdbMerge::dumpListWrapper(void *state) {
	// debug msg
	log(LOG_DEBUG,"db: Dump of list completed: %s.",mstrerror(g_errno));

	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;

	for (;;) {
		// collection reset or deleted while RdbDump.cpp was writing out?
		if (g_errno == ENOCOLLREC) {
			THIS->doneMerging();
			return;
		}
		// return if this blocked
		if (!THIS->getNextList()) {
			return;
		}

		// if g_errno is out of memory then msg3 wasn't able to get the lists
		// so we should sleep and retry
		if (g_errno == ENOMEM) {
			// if the dump failed, it should reset m_dump.m_offset of
			// the file to what it was originally (in case it failed
			// in adding the list to the map). we do not need to set
			// m_startKey back to the startkey of this list, because
			// it is *now* only advanced on successful dump!!
			THIS->doSleep();
			return;
		}

		// . if g_errno we're done
		// . if list is empty we're done
		if (g_errno || THIS->m_doneMerging) {
			THIS->doneMerging();
			return;
		}

		// return if this blocked
		if (!THIS->dumpList()) {
			return;
		}

		// otherwise, keep on trucking
	}
}

// . return false if blocked, true otherwise
// . set g_errno on error
// . list should be truncated, possible have all negative keys removed,
//   and de-duped thanks to RdbList::indexMerge_r() and RdbList::merge_r()
bool RdbMerge::dumpList() {
	// return true on g_errno
	if (g_errno) {
		return true;
	}

	// . it's suspended so we count this as blocking
	// . resumeMerge() will call getNextList() again, not dumpList() so
	//   don't advance m_startKey
	if(m_isHalted) {
		return false;
	}

	// if we use getLastKey() for this the merge completes but then
	// tries to merge two empty lists and cores in the merge function
	// because of that. i guess it relies on endkey rollover only and
	// not on reading less than minRecSizes to determine when to stop
	// doing the merge.
	m_list.getEndKey(m_startKey) ;
	KEYINC(m_startKey,m_ks);

	/////
	//
	// dedup for spiderdb before we dump it. try to save disk space.
	//
	/////
	if (m_rdbId == RDB_SPIDERDB) {
		dedupSpiderdbList( &m_list );
	}

	// if the startKey rolled over we're done
	if (KEYCMP(m_startKey, KEYMIN(), m_ks) == 0) {
		m_doneMerging = true;
	}

	log(LOG_DEBUG,"db: Dumping list.");

	// . send the whole list to the dump
	// . it returns false if blocked, true otherwise
	// . it sets g_errno on error
	// . it calls dumpListWrapper when done dumping
	// . return true if m_dump had an error or it did not block
	// . if it gets a EFILECLOSED error it will keep retrying forever
	return m_dump.dumpList(&m_list, m_niceness, false);
}

void RdbMerge::doneMerging() {
	// save this
	int32_t saved_errno = g_errno;

	// let RdbDump free its m_verifyBuf buffer if it existed
	m_dump.reset();

	// . free the list's memory, reset() doesn't do it
	// . when merging titledb i'm still seeing 200MB allocs to read from tfndb.
	m_list.freeList();

	log(LOG_INFO,"db: Merge status: %s.",mstrerror(g_errno));

	// . reset our class
	// . this will free it's cutoff keys buffer, trash buffer, treelist
	// . TODO: should we not reset to keep the mem handy for next time
	//   to help avoid out of mem errors?
	m_msg5.reset();

	// . do we really need these anymore?
	m_isMerging     = false;

	// if collection rec was deleted while merging files for it
	// then the rdbbase should be NULL i guess.
	if (saved_errno == ENOCOLLREC) {
		return;
	}

	// if we are exiting then dont bother renaming the files around now.
	// this prevents a core in RdbBase::incorporateMerge()
	if (g_process.m_mode == Process::EXIT_MODE) {
		log(LOG_INFO, "merge: exiting. not ending merge.");
		return;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(m_rdbId, m_collnum);
	if (!base) {
		return;
	}

	// pass g_errno on to incorporate merge so merged file can be unlinked
	base->incorporateMerge();

	relinquishMergespaceLock();
}


void RdbMerge::relinquishMergespaceLock() {
	if(m_mergeSpaceCoordinator)
		m_mergeSpaceCoordinator->relinquish();
}
