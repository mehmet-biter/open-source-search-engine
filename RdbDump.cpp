#include "RdbDump.h"
#include "Rdb.h"
#include "RdbCache.h"
#include "Collectiondb.h"
#include "Statsdb.h"


// . return false if blocked, true otherwise
// . sets g_errno on error
bool RdbDump::set(collnum_t collnum,
                  BigFile *file,
                  RdbBuckets *buckets, // optional buckets to dump
                  RdbTree *tree, // optional tree to dump
                  RdbIndex *treeIndex, // only present if buckets/tree is set
                  RdbMap *map,
                  RdbIndex *index,
                  int32_t maxBufSize,
                  int32_t niceness,
                  void *state,
                  void (*callback)(void *state),
                  bool useHalfKeys,
                  int64_t startOffset,
                  const char *prevLastKey,
                  char keySize,
                  Rdb *rdb) {
	m_collnum = collnum;

	// use 0 for collectionless
	if (rdb && rdb->isCollectionless()) {
		m_collnum = 0;
	}

	// are we like catdb/statsdb etc.?
	m_doCollCheck = true;
	if ( rdb && rdb->isCollectionless() ) m_doCollCheck = false;
	// RdbMerge also calls us but rdb is always set to NULL and it was
	// causing a merge on catdb (collectionless) to screw up
	if ( ! rdb ) m_doCollCheck = false;

	m_file          = file;
	m_buckets       = buckets;
	m_tree          = tree;
	m_treeIndex     = treeIndex;
	m_map           = map;
	m_index         = index;
	m_state         = state;
	m_callback      = callback;
	m_list          = NULL;
	m_niceness      = niceness;
	m_tried         = false;
	m_isSuspended   = false;
	m_ks            = keySize;

	// reset this in case we run out of mem, it doesn't get set properly
	// and needs to be NULL for RdbMem's call to getLastKeyinQueue()
	m_lastKeyInQueue  = NULL;
	KEYMIN(m_firstKeyInQueue,m_ks);

	m_isDumping     = false;
	m_writing       = false;
	m_buf           = NULL;
	m_verifyBuf     = NULL;
	m_maxBufSize    = maxBufSize;
	m_offset        = startOffset ;
	m_rolledOver    = false; // true if m_nextKey rolls over back to 0
	KEYMIN(m_nextKey,m_ks);
	m_nextNode      = 0 ; // used in dumpTree()
	// if we're dumping indexdb, allow half keys
	m_useHalfKeys  = useHalfKeys;
	//m_prevLastKey  = prevLastKey;
	KEYSET(m_prevLastKey,prevLastKey,m_ks);
	// for setting m_rdb->m_needsSave after deleting the dump list
	m_rdb = rdb;

	// . don't dump to a pre-existing file
	// . seems like Rdb.cpp makes a new BigFile before calling this
	// . now we can resume merges, so we can indeed dump to the END
	//   of a pre-exiting file, but not when dumping a tree!
	if ((m_tree || m_buckets) && m_file->getFileSize() > 0) {
		g_errno = EEXIST;
		log(LOG_WARN, "db: Could not dump to %s. File exists.", m_file->getFilename());
		return true;
	}

	// . open the file nonblocking, sync with disk, read/write
	// . NOTE: O_SYNC doesn't work too well over NFS
	// . we need O_SYNC when dumping trees only because we delete the
	//   nodes/records as we dump them
	// . ensure this sets g_errno for us
	// . TODO: open might not block! fix that!
	int32_t flags = O_RDWR | O_CREAT;
	// a niceness bigger than 0 means to do non-blocking dumps
	if (niceness > 0) {
		flags |= O_ASYNC | O_NONBLOCK;
	}

	if (!m_file->open(flags)) {
		return true;
	}

	// . get the file descriptor of the first real file in BigFile
	// . we should only dump to the first file in BigFile otherwise,
	//   we'd have to juggle fd registration
	m_fd = m_file->getfd(0, false);
	if (m_fd < 0) {
		log(LOG_LOGIC, "db: dump: Bad fd of first file in BigFile.");
		return true;
	}

	// we're now considered to be in dumping state
	m_isDumping = true;

	// . if no tree was provided to dump it must be RdbMerge calling us
	// . he'll want to call dumpList() on his own
	if (!m_tree && !m_buckets) {
		return true;
	}

	// how many recs in tree?
	int32_t nr;
	const char *structureName;
	if (m_tree) {
		nr = m_tree->getNumUsedNodes();
		structureName = "tree";
	} else if (m_buckets) {
		nr = m_buckets->getNumKeys();
		structureName = "buckets";
	}

	log(LOG_INFO,"db: Dumping %" PRId32" recs from %s to files.", nr, structureName);

	// keep a total count for reporting when done
	m_totalPosDumped = 0;
	m_totalNegDumped = 0;

	// we have our own flag here since m_dump::m_isDumping gets
	// set to true between collection dumps, RdbMem.cpp needs
	// a flag that doesn't do that... see RdbDump.cpp.
	// this was in Rdb.cpp but when threads were turned off it was
	// NEVER getting set and resulted in corruption in RdbMem.cpp.
	m_rdb->setInDumpLoop(true);

	// . start dumping the tree
	// . return false if it blocked
	if (!dumpTree(false)) {
		return false;
	}

	// no longer dumping
	doneDumping();

	// return true since we didn't block
	return true;
}

void RdbDump::reset ( ) {
	// free verify buf if there
	if (m_verifyBuf) {
		mfree(m_verifyBuf, m_verifyBufSize, "RdbDump4");
		m_verifyBuf = NULL;
	}
}

void RdbDump::doneDumping() {
	int32_t saved = g_errno;

	m_isDumping = false;
	// print stats
	log(LOG_INFO,
	    "db: Dumped %" PRId32" positive and %" PRId32" negative recs. "
	    "Total = %" PRId32".",
	     m_totalPosDumped , m_totalNegDumped ,
	     m_totalPosDumped + m_totalNegDumped );

	// . map verify
	// . if continueDumping called us with no collectionrec, it got
	//   deleted so RdbBase::m_map is nuked too i guess
	if (saved != ENOCOLLREC && m_map) {
		log(LOG_INFO, "db: map # pos=%" PRId64" neg=%" PRId64, m_map->getNumPositiveRecs(), m_map->getNumNegativeRecs());
	}

	// free the list's memory
	if (m_list) {
		m_list->freeList();
	}

	// reset verify buffer
	reset();

	// did collection get deleted/reset from under us?
	if (saved == ENOCOLLREC) {
		return;
	}

	// save the map to disk. true = allDone
	if (m_map) {
		m_map->writeMap(true);
	}

	// regenerate treeIndex
	if (m_treeIndex) {
		bool result = m_tree ? m_treeIndex->generateIndex(m_tree, m_collnum) : m_treeIndex->generateIndex(m_buckets, m_collnum);
		if (!result) {
			logError("db: Index generation failed");
			gbshutdownCorrupted();
		}
	}

	if (m_index) {
		m_index->writeIndex();
	}

	// now try to merge this collection/db again
	// if not already in the linked list. but do not add to linked list
	// if it is statsdb or catdb.
	if (m_rdb && !m_rdb->isCollectionless()) {
		addCollnumToLinkedListOfMergeCandidates(m_collnum);
	}

#ifdef GBSANITYCHECK
	// sanity check
	log("DOING SANITY CHECK FOR MAP -- REMOVE ME");
	if ( m_map && ! m_map->verifyMap ( m_file ) ) {
		g_process.shutdownAbort(true); }
	// now check the whole file for consistency
	if ( m_ks == 18 ) { // map->m_rdbId == RDB_POSDB ) {
		collnum_t collnum = g_collectiondb.getCollnum ( m_coll );
		class RdbBase *base = m_rdb->m_bases[collnum];
		int32_t startFileNum = base->getNumFiles()-1;
		log("sanity: startfilenum=%" PRId32,startFileNum);
		dumpPosdb(m_coll,
			  startFileNum, // startFileNum
			   1                    , // numFiles
			   false                , // includeTree
			   -1                   , // termId
			   true                 );// justVerify?
	}
#endif
}


void RdbDump::tryAgainWrapper2 ( int fd , void *state ) {
	// debug msg
	log(LOG_INFO,"db: Trying to get data again.");
	// stop waiting
	g_loop.unregisterSleepCallback ( state , tryAgainWrapper2 );
	// bitch about errors
	if (g_errno) log("db: Had error: %s.",mstrerror(g_errno));
	// get THIS ptr from state
	RdbDump *THIS = (RdbDump *)state;
	// continue dumping the tree or give control back to caller
	THIS->continueDumping ( );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . dumps the RdbTree, m_tree, into m_file
// . also sets and writes the RdbMap for m_file
// . we methodically get RdbLists from the RdbTree
// . dumped recs are ordered by key if "orderedDump" was true in call to set()
//   otherwise, lists are ordered by node #
// . we write each list of recs to the file until the whole tree has been done
// . we delete all records in list from the tree after we've written the list
// . if a cache was provided we incorporate the list into the cache before
//   deleting it from the tree to keep the cache in sync. NO we do NOT!
// . called again by writeBuf() when it's done writing the whole list
bool RdbDump::dumpTree(bool recall) {
	if (g_conf.m_logTraceRdbDump) {
		logTrace(g_conf.m_logTraceRdbDump, "BEGIN");
		logTrace(g_conf.m_logTraceRdbDump, "recall.: %s", recall ? "true" : "false");

		const char *s = "none";
		if (m_rdb) {
			s = getDbnameFromId(m_rdb->getRdbId());
			logTrace(g_conf.m_logTraceRdbDump, "m_rdbId: %02x", m_rdb->getRdbId());
		}

		logTrace(g_conf.m_logTraceRdbDump, "name...: [%s]", s);
	}

	// set up some vars
	char maxEndKey[MAX_KEY_BYTES];
	KEYMAX(maxEndKey,m_ks);

	// if dumping statsdb, we can only dump records 30 seconds old or
	// more because Statsdb.cpp can "back modify" such records in the tree
	// because it may have a query that took 10 seconds come in then it
	// needs to add a partial stat to the last 10 stats for those 10 secs.
	// we use Global time at this juncture
	if (m_rdb->getRdbId() == RDB_STATSDB) {
		int32_t nowSecs = getTimeGlobal();
		StatKey *sk = (StatKey *)maxEndKey;
		sk->m_zero = 0x01;
		sk->m_labelHash = 0xffffffff;
		// leave last 60 seconds in there just to be safe
		sk->m_time1 = nowSecs - 60;
	}

	// this list will hold the list of nodes/recs from m_tree
	m_list = &m_ourList;

	for (;;) {
		// if the lastKey was the max end key last time then we're done
		if (m_rolledOver) {
			logTrace(g_conf.m_logTraceRdbDump, "END - m_rolledOver, returning true");
			return true;
		}

		// this is set to -1 when we're done with our unordered dump
		if (m_nextNode == -1) {
			logTrace(g_conf.m_logTraceRdbDump, "END - m_nextNode, returning true");
			return true;
		}

		// . NOTE: list's buffer space should be re-used!! (TODO)
		// . "lastNode" is set to the last node # in the list

		if (!recall) {
			bool status = true;

			m_t1 = gettimeofdayInMilliseconds();
			if (m_tree) {
				logTrace(g_conf.m_logTraceRdbDump, "m_tree");

				status = m_tree->getList(m_collnum, m_nextKey, maxEndKey, m_maxBufSize, m_list,
				                         &m_numPosRecs, &m_numNegRecs, m_useHalfKeys);
			} else if (m_buckets) {
				logTrace(g_conf.m_logTraceRdbDump, "m_buckets");

				status = m_buckets->getList(m_collnum, m_nextKey, maxEndKey, m_maxBufSize, m_list,
				                            &m_numPosRecs, &m_numNegRecs, m_useHalfKeys);
			}

			logTrace(g_conf.m_logTraceRdbDump, "status: %s", status ? "true" : "false");

			// if error getting list (out of memory?)
			if (!status) {
				logError("db: Had error getting data for dump: %s. Retrying.", mstrerror(g_errno));

				// retry for the remaining two types of errors
				if (!g_loop.registerSleepCallback(1000, this, tryAgainWrapper2)) {
					log(LOG_WARN, "db: Retry failed. Could not register callback.");

					logTrace(g_conf.m_logTraceRdbDump, "END - retry failed, returning true");
					return true;
				}

				logTrace(g_conf.m_logTraceRdbDump, "END - returning false");

				// wait for sleep
				return false;
			}

			// don't dump out any neg recs if it is our first time dumping
			// to a file for this rdb/coll. TODO: implement this later.
			//if ( removeNegRecs )
			//	m_list.removeNegRecs();

			// if(!m_list->checkList_r ( false , // removeNegRecs?
			// 			 false , // sleep on problem?
			// 			 m_rdb->m_rdbId )) {
			// 	log("db: list to dump is not sane!");
			// 	g_process.shutdownAbort(true);
			// }
		}

		int64_t t2 = gettimeofdayInMilliseconds();

		log(LOG_INFO, "db: Get list took %" PRId64" ms. %" PRId32" positive. %" PRId32" negative.",
		    t2 - m_t1, m_numPosRecs, m_numNegRecs);

		// keep a total count for reporting when done
		m_totalPosDumped += m_numPosRecs;
		m_totalNegDumped += m_numNegRecs;

		// . check the list we got from the tree for problems
		// . ensures keys are ordered from lowest to highest as well
		if (g_conf.m_verifyWrites || g_conf.m_verifyDumpedLists) {
			const char *s = "none";
			if (m_rdb) {
				s = getDbnameFromId(m_rdb->getRdbId());
			}

			const char *ks1 = "";
			const char *ks2 = "";
			char tmp1[32];
			char tmp2[32];

			strcpy(tmp1, KEYSTR(m_firstKeyInQueue, m_list->getKeySize()));
			ks1 = tmp1;

			if (m_lastKeyInQueue) {
				strcpy(tmp2, KEYSTR(m_lastKeyInQueue, m_list->getKeySize()));
				ks2 = tmp2;
			}

			log(LOG_INFO, "dump: verifying list before dumping (rdb=%s collnum=%i k1=%s k2=%s)", s, (int)m_collnum, ks1,
			    ks2);
			m_list->checkList_r(false, m_rdb->getRdbId());
		}

		// if list is empty, we're done!
		if (m_list->isEmpty()) {
			// consider that a rollover?
			if (m_rdb->getRdbId() == RDB_STATSDB) {
				m_rolledOver = true;
			}
			return true;
		}

		// get the last key of the list
		const char *lastKey = m_list->getLastKey();
		// advance m_nextKey
		KEYSET(m_nextKey, lastKey, m_ks);
		KEYINC(m_nextKey, m_ks);
		if (KEYCMP(m_nextKey, lastKey, m_ks) < 0) {
			m_rolledOver = true;
		}

		// if list is empty, we're done!
		if (m_list->isEmpty()) {
			logTrace(g_conf.m_logTraceRdbDump, "END - list empty, returning true");
			return true;
		}

		// . set m_firstKeyInQueue and m_lastKeyInQueue
		// . this doesn't work if you're doing an unordered dump, but we should
		//   not allow adds when closing
		m_lastKeyInQueue = m_list->getLastKey();

		// ensure we are getting the first key of the list
		m_list->resetListPtr();

		//m_firstKeyInQueue = m_list->getCurrentKey();
		m_list->getCurrentKey(m_firstKeyInQueue);

		// . write this list to disk
		// . returns false if blocked, true otherwise
		// . sets g_errno on error

		// . if this blocks it should call us (dumpTree() back)
		if (!dumpList(m_list, m_niceness, false)) {
			logTrace(g_conf.m_logTraceRdbDump, "END - after dumpList, returning false");
			return false;
		}

		// close up shop on a write/dumpList error
		if (g_errno) {
			logTrace(g_conf.m_logTraceRdbDump, "END - g_errno set [%"
					PRId32
					"], returning true", g_errno);
			return true;
		}

		// . if dumpList() did not block then keep on truckin'
		// . otherwise, wait for callback of dumpTree()
	}
}

// . return false if blocked, true otherwise
// . sets g_errno on error
// . this one is also called by RdbMerge to dump lists
bool RdbDump::dumpList(RdbList *list, int32_t niceness, bool recall) {
	// if we had a write error and are being recalled...
	if (recall) {
		m_offset -= m_bytesToWrite;
	} else {
		// assume we don't hack the list
		m_hacked = false;
		m_hacked12 = false;

		// save ptr to list... why?
		m_list = list;
		// nothing to do if list is empty
		if (m_list->isEmpty()) {
			return true;
		}

		// we're now in dump mode again
		m_isDumping = true;

		// don't check list if we're dumping an unordered list from tree!
		if (g_conf.m_verifyWrites) {
			m_list->checkList_r();
		}

		// before calling RdbMap::addList(), always reset list ptr
		// since we no longer call this in RdbMap::addList() so we don't
		// mess up the possible HACK below
		m_list->resetListPtr();

		// . SANITY CHECK
		// . ensure first key is >= last key added to the map map
		if (m_offset > 0 && m_map) {
			char k[MAX_KEY_BYTES];
			m_list->getCurrentKey(k);

			char lastKey[MAX_KEY_BYTES];
			m_map->getLastKey(lastKey);

			if (KEYCMP(k, lastKey, m_ks) <= 0) {
				log(LOG_LOGIC, "db: Dumping list key out of order. "
						    "lastKey=%s k=%s",
				    KEYSTR(lastKey, m_ks),
				    KEYSTR(k, m_ks));
				g_errno = EBADENGINEER;
				gbshutdownLogicError();
			}
		}

		if (g_conf.m_verifyWrites) {
			rdbid_t rdbId = RDB_NONE;
			if (m_rdb) rdbId = m_rdb->getRdbId();
			m_list->checkList_r(false, rdbId);
			m_list->resetListPtr();
		}

		// HACK! POSDB
		if (m_ks == 18 && m_offset > 0) {
			char k[MAX_KEY_BYTES];
			m_list->getCurrentKey(k);
			// . same top 6 bytes as last key we added?
			// . if so, we should only add 6 bytes from this key, not 12
			//   so on disk it is compressed consistently
			if (memcmp((k) + (m_ks - 12), (m_prevLastKey) + (m_ks - 12), 12) == 0) {
				char tmp[MAX_KEY_BYTES];
				char *p = m_list->getList();
				// swap high 12 bytes with low 6 bytes for first key
				gbmemcpy (tmp, p, m_ks - 12);
				gbmemcpy (p, p + (m_ks - 12), 12);
				gbmemcpy (p + 12, tmp, m_ks - 12);
				// big hack here
				m_list->setList(p + 12);
				m_list->setListPtr(p + 12);
				m_list->setListPtrLo(p);
				m_list->setListPtrHi(p + 6);
				m_list->setListSize(m_list->getListSize() - 12);
				// turn on both bits to indicate double compression
				*(p + 12) |= 0x06;
				m_hacked12 = true;
			}
		}

		// . HACK
		// . if we're doing an ordered dump then hack the list's first 12 byte
		//   key to make it a 6 byte iff the last key we dumped last time
		//   shares the same top 6 bytes as the first key of this list
		// . this way we maintain compression consistency on the disk
		//   so IndexTable.cpp can expect all 6 byte keys for the same termid
		//   and RdbList::checkList_r() can expect the half bits to always be
		//   on when they can be on
		// . IMPORTANT: calling m_list->resetListPtr() will mess this HACK up!!
		if (m_useHalfKeys && m_offset > 0 && !m_hacked12) {
			char k[MAX_KEY_BYTES];
			m_list->getCurrentKey(k);
			// . same top 6 bytes as last key we added?
			// . if so, we should only add 6 bytes from this key, not 12
			//   so on disk it is compressed consistently
			if (memcmp((k) + (m_ks - 6), (m_prevLastKey) + (m_ks - 6), 6) == 0) {
				m_hacked = true;
				char tmp[MAX_KEY_BYTES];
				char *p = m_list->getList();
				gbmemcpy (tmp, p, m_ks - 6);
				gbmemcpy (p, p + (m_ks - 6), 6);
				gbmemcpy (p + 6, tmp, m_ks - 6);
				// big hack here
				m_list->setList(p + 6);
				// make this work for POSDB, too
				m_list->setListPtr(p + 6);
				m_list->setListPtrLo(p + 6 + 6);
				m_list->setListPtrHi(p);
				m_list->setListSize(m_list->getListSize() - 6);
				// hack on the half bit, too
				*(p + 6) |= 0x02;
			}
		}

		// update old last key
		m_list->getLastKey(m_prevLastKey);

		// now write it to disk
		m_buf = m_list->getList();
		m_bytesToWrite = m_list->getListSize();
	}

	// make sure we have enough mem to add to map after a successful
	// dump up here, otherwise, if we write it and fail to add to map
	// the map is not in sync if we core thereafter
	if (m_map && !m_map->prealloc(m_list)) {
		log(LOG_ERROR, "db: Failed to prealloc list into map: %s.", mstrerror(g_errno));

		// g_errno should be set to something if that failed
		if (g_errno == 0) {
			gbshutdownLogicError();
		}

		return true;
	}

	// tab to the old offset
	int64_t offset = m_offset;
	// might as well update the offset now, even before write is done
	m_offset += m_bytesToWrite ;
	// write thread is out
	m_writing = true;

	// . if we're called by RdbMerge directly use m_callback/m_state
	// . otherwise, use doneWritingWrapper() which will call dumpTree()
	// . BigFile::write() return 0 if blocked,-1 on error,>0 on completion
	// . it also sets g_errno on error
	bool isDone = m_file->write(m_buf, m_bytesToWrite, offset, &m_fstate, this, doneWritingWrapper, niceness);

	// return false if it blocked
	if (!isDone) {
		return false;
	}

	// done writing
	m_writing = false;

	// return true on error
	if (g_errno) {
		return true;
	}

	// . delete list from tree, incorporate list into cache, add to map
	// . returns false if blocked, true otherwise, sets g_errno on error
	// . will only block in calling updateTfndb()
	return doneDumpingList();
}

// . delete list from tree, incorporate list into cache, add to map
// . returns false if blocked, true otherwise, sets g_errno on error
bool RdbDump::doneDumpingList() {
	logTrace(g_conf.m_logTraceRdbDump, "BEGIN");

	// . if error was EFILECLOSE (file got closed before we wrote to it)
	//   then try again. file can close because fd pool needed more fds
	// . we cannot do this retry in BigFile.cpp because the BigFile
	//   may have been deleted/unlinked from a merge, but we could move
	//   this check to Msg3... and do it for writes, too...
	// . seem to be getting EBADFD errors now, too (what code is it?)
	//   i don't remember, just do it on *all* errors for now!
	if (g_errno) {
		if (m_isSuspended) {
			// bail on error
			logError("db: Had error dumping data: %s.", mstrerror(g_errno));
			logTrace( g_conf.m_logTraceRdbDump, "END - returning true" );
			return true;
		} else {
			logError("db: Had error dumping data: %s. Retrying.", mstrerror(g_errno));

			bool rc = dumpList(m_list, m_niceness, true);
			logTrace(g_conf.m_logTraceRdbDump, "END. Returning %s", rc ? "true" : "false");
			return rc;
		}
	}

	// should we verify what we wrote? useful for preventing disk
	// corruption from those pesky Western Digitals and Maxtors?
	if (g_conf.m_verifyWrites) {
		// a debug message, if log disk debug messages is enabled
		log(LOG_DEBUG,"disk: Verifying %" PRId32" bytes written.", m_bytesToWrite);

		// make a read buf
		if (m_verifyBuf && m_verifyBufSize < m_bytesToWrite) {
			mfree(m_verifyBuf, m_verifyBufSize, "RdbDump3");
			m_verifyBuf = NULL;
			m_verifyBufSize = 0;
		}

		if (!m_verifyBuf) {
			m_verifyBuf = (char *)mmalloc(m_bytesToWrite, "RdbDump3");
			m_verifyBufSize = m_bytesToWrite;
		}

		// out of mem? if so, skip the write verify
		if (!m_verifyBuf) {
			return doneReadingForVerify();
		}

		// read what we wrote
		bool isDone = m_file->read(m_verifyBuf, m_bytesToWrite, m_offset - m_bytesToWrite, &m_fstate, this,
		                           doneReadingForVerifyWrapper, m_niceness);

		// return false if it blocked
		if (!isDone) {
			logTrace(g_conf.m_logTraceRdbDump, "END - isDone is false. returning false");
			return false;
		}
	}

	bool rc = doneReadingForVerify();
	logTrace(g_conf.m_logTraceRdbDump, "END - after doneReadingForVerify. Returning %s", rc ? "true" : "false");
	return rc;
}


void RdbDump::doneReadingForVerifyWrapper ( void *state ) {
	RdbDump *THIS = (RdbDump *)state;

	// return if this blocks
	if (!THIS->doneReadingForVerify()) {
		return;
	}

	// continue
	THIS->continueDumping();
}

bool RdbDump::doneReadingForVerify ( ) {
	logTrace( g_conf.m_logTraceRdbDump, "BEGIN" );

	// if someone reset/deleted the collection we were dumping...
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);

	// . do not do this for statsdb/catdb which always use collnum of 0
	// . RdbMerge also calls us but gives a NULL m_rdb so we can't
	//   set m_isCollectionless to false
	if (!cr && m_doCollCheck) {
		g_errno = ENOCOLLREC;
		logError("db: lost collection while dumping to disk. making map null so we can stop.");
		m_map = NULL;

		// m_file is probably invalid too since it is stored in cr->m_bases[i]->m_files[j]
		m_file = NULL;
		m_index = NULL;
	}

	// see if what we wrote is the same as what we read back
	if (m_verifyBuf && g_conf.m_verifyWrites && memcmp(m_verifyBuf, m_buf, m_bytesToWrite) != 0 && !g_errno) {
		logError("disk: Write verification of %" PRId32" bytes to file %s failed at offset=%" PRId64". Retrying.",
		         m_bytesToWrite, m_file->getFilename(), m_offset - m_bytesToWrite);

		// try writing again
		bool rc = dumpList(m_list, m_niceness, true);
		logTrace( g_conf.m_logTraceRdbDump, "END - after retrying dumpList. Returning %s", rc ? "true" : "false" );
		return rc;
	}

	// time dump to disk (and tfndb bins)
	int64_t t1 = gettimeofdayInMilliseconds();

	// sanity check
	if (m_list->getKeySize() != m_ks) {
		logError("Sanity check failed. m_list->m_ks [%02x]!= m_ks [%02x]", m_list->getKeySize(), m_ks);
		gbshutdownCorrupted();
	}

	bool triedToFix = false;

tryAgain:
	// . register this with the map now
	// . only register AFTER it's ALL on disk so we don't get partial
	//   record reads and we don't read stuff on disk that's also in tree
	// . add the list to the rdb map if we have one
	// . we don't have maps when we do unordered dumps
	// . careful, map is NULL if we're doing unordered dump
	if (m_map && !m_map->addList(m_list)) {
		// keys  out of order in list from tree?
		if (g_errno == ECORRUPTDATA) {
			logError("m_map->addList resulted in ECORRUPTDATA");

			if (m_tree) {
				logError("trying to fix tree");
				m_tree->fixTree();
			}

			if (m_buckets) {
				logError("Contains buckets, cannot fix this yet");
				m_list->printList();	//@@@@@@ EXCESSIVE
				gbshutdownCorrupted();
			}


			if (triedToFix) {
				logError("already tried to fix, exiting hard");
				gbshutdownCorrupted();
			}

			triedToFix = true;
			goto tryAgain;
		}

		g_errno = ENOMEM;

		// this should never happen now since we call prealloc() above
		logError("Failed to add data to map, exiting hard");
		gbshutdownCorrupted();
	}

	int64_t t2 = gettimeofdayInMilliseconds();
	log(LOG_TIMING, "db: adding to map took %" PRIu64" ms", t2 - t1);

	if (m_index) {
		m_index->addList(m_list);
		log(LOG_TIMING, "db: adding to index took %" PRIu64" ms", gettimeofdayInMilliseconds() - t2);
	}

	// . HACK: fix hacked lists before deleting from tree
	// . iff the first key has the half bit set
	if (m_hacked) {
		char tmp[MAX_KEY_BYTES];
		char *p = m_list->getList() - 6 ;
		gbmemcpy (tmp, p, 6);
		gbmemcpy (p, p + 6, m_ks - 6);
		gbmemcpy (p + (m_ks - 6), tmp, 6);
		// undo the big hack
		m_list->setList(p);
		// make this work for POSDB...
		m_list->setListPtr(p);
		m_list->setListPtrLo(p + m_ks - 12);
		m_list->setListPtrHi(p + m_ks - 6);
		m_list->setListSize(m_list->getListSize() + 6);
		// hack off the half bit, we're 12 bytes again
		*p &= 0xfd;
		// turn it off again just in case
		m_hacked = false;
	}

	if (m_hacked12) {
		char tmp[MAX_KEY_BYTES];
		char *p = m_list->getList() - 12 ;
		// swap high 12 bytes with low 6 bytes for first key
		gbmemcpy (tmp, p, 12);
		gbmemcpy (p, p + 12, 6);
		gbmemcpy (p + 6, tmp, 12);
		// big hack here
		m_list->setList(p);
		m_list->setListPtr(p);
		m_list->setListPtrLo(p + 6);
		m_list->setListPtrHi(p + 12);
		m_list->setListSize(m_list->getListSize() + 12);
		// hack off the half bit, we're 12 bytes again
		*p &= 0xf9;
		m_hacked12 = false;
	}

	// if we're NOT dumping a tree then return control to RdbMerge
	if (!m_tree && !m_buckets) {
		logTrace( g_conf.m_logTraceRdbDump, "END - !m_tree && !m_buckets, returning true" );
		return true;
	}

	// . delete these nodes from the tree now that they're on the disk
	//   now that they can be read from list since addList() was called
	// . however, while we were writing to disk a key that we were
	//   writing could have been deleted from the tree. To prevent
	//   problems we should only delete nodes that are present in tree...
	// . actually i fixed that problem by not deleting any nodes that
	//   might be in the middle of being dumped
	// . i changed Rdb::addNode() and Rdb::deleteNode() to do this
	// . since we made it here m_list MUST be ordered, therefore
	//   let's try the new, faster deleteOrderedList and let's not do
	//   balancing to make it even faster
	// . balancing will be restored once we're done deleting this list

	int64_t t3 = gettimeofdayInMilliseconds();

	// tree delete is slow due to checking for leaks, not balancing
	bool s;
	if (m_tree) {
		s = m_tree->deleteList(m_collnum, m_list, true);
	} else if (m_buckets) {
		s = m_buckets->deleteList(m_collnum, m_list);
	}

	// problem?
	if (!s && !m_tried) {
		m_tried = true;

		if (m_file) {
			log(LOG_ERROR, "db: Corruption in tree detected when dumping to %s. Fixing. Your memory had an error. "
			               "Consider replacing it.", m_file->getFilename());
		}

		log(LOG_WARN, "db: was collection restarted/reset/deleted before we could delete list from tree? collnum=%hd", m_collnum);

		// reset error in that case
		g_errno = 0;
	}

	log(LOG_TIMING,"db: dump: deleteList: took %" PRId64,gettimeofdayInMilliseconds()-t3);

	logTrace( g_conf.m_logTraceRdbDump, "END - OK, returning true" );
	return true;
}


// continue dumping the tree
void RdbDump::doneWritingWrapper(void *state) {
	// get THIS ptr from state
	RdbDump *THIS = (RdbDump *)state;

	// done writing
	THIS->m_writing = false;

	// bitch about errors
	if (g_errno && THIS->m_file) {
		log( LOG_WARN, "db: Dump to %s had write error: %s.", THIS->m_file->getFilename(), mstrerror( g_errno ) );
	}
		
	// delete list from tree, incorporate list into cache, add to map
	if (!THIS->doneDumpingList()) {
		return;
	}

	// continue
	THIS->continueDumping();
}

void RdbDump::continueDumping() {
	// if someone reset/deleted the collection we were dumping...
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// . do not do this for statsdb/catdb which always use collnum of 0
	// . RdbMerge also calls us but gives a NULL m_rdb so we can't
	//   set m_isCollectionless to false
	if (!cr && m_doCollCheck) {
		g_errno = ENOCOLLREC;
		// m_file is invalid if collrec got nuked because so did the Rdbbase which has the files
		// m_file is probably invalid too since it is stored in cr->m_bases[i]->m_files[j]
		m_file = NULL;
		log(LOG_WARN, "db: continue dumping lost collection");
	} else if (g_errno) {
		// bitch about errors, but i guess if we lost our collection
		// then the m_file could be invalid since that was probably stored
		// in the CollectionRec::RdbBase::m_files[] array of BigFile ptrs
		// so we can't say m_file->getFilename()
		log(LOG_WARN, "db: Dump to %s had error writing: %s.", m_file->getFilename(),mstrerror(g_errno));
	}

	// go back now if we were NOT dumping a tree
	if (!(m_tree || m_buckets)) {
		m_isDumping = false;
		m_callback(m_state);
		return;
	}

	// . continue dumping the tree
	// . return if this blocks
	// . if the collrec was deleted or reset then g_errno will be
	//   ENOCOLLREC and we want to skip call to dumpTree(
	if (g_errno != ENOCOLLREC && !dumpTree(false)) {
		return;
	}

	// close it up
	doneDumping();

	// call the callback
	m_callback(m_state);
}
