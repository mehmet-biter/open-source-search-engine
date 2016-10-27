#include "gb-include.h"

#include "RdbIndex.h"
#include "BigFile.h"
#include "Titledb.h"	// For MAX_DOCID
#include "Process.h"
#include "BitOperations.h"
#include "Conf.h"
#include <set>
#include <unordered_set>
#include <algorithm>
#include "RdbTree.h"
#include "RdbBuckets.h"
#include "ScopedLock.h"
#include <fcntl.h>

#include <iterator>

static const int32_t s_defaultMaxPendingTimeMs = 5000;
static const uint32_t s_defaultMaxPendingSize = 2000000;

// larger number equals more memory used; but faster generateIndex
// 40000000 * 8 bytes = 320 megabytes
static const uint32_t s_generateMaxPendingSize = 40000000;

static const int64_t s_rdbIndexCurrentVersion = 0;

RdbIndex::RdbIndex()
	: m_file()
	, m_fixedDataSize(0)
	, m_useHalfKeys(false)
	, m_ks(0)
	, m_rdbId(RDB_NONE)
	, m_version(s_rdbIndexCurrentVersion)
	, m_docIds(new docids_t)
	, m_docIdsMtx()
	, m_pendingDocIdsMtx()
	, m_pendingDocIds(new docids_t)
	, m_prevPendingDocId(MAX_DOCID + 1)
	, m_lastMergeTime(gettimeofdayInMilliseconds())
	, m_needToWrite(false) {
}

// dont save index on deletion!
RdbIndex::~RdbIndex() {
}

void RdbIndex::reset() {
	m_file.reset();

	/// @todo ALC do we need to lock here?
	m_docIds.reset(new docids_t);
	m_pendingDocIds.reset(new docids_t);

	m_prevPendingDocId = MAX_DOCID + 1;
	m_lastMergeTime = gettimeofdayInMilliseconds();

	m_needToWrite = false;
}

/// @todo ALC collapse RdbIndex::set into constructor
void RdbIndex::set(const char *dir, const char *indexFilename, int32_t fixedDataSize , bool useHalfKeys ,
                   char keySize, rdbid_t rdbId) {
	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. dir [%s], indexFilename [%s]", dir, indexFilename);

	reset();

	m_fixedDataSize = fixedDataSize;
	m_file.set(dir, indexFilename);
	m_useHalfKeys = useHalfKeys;
	m_ks = keySize;
	m_rdbId = rdbId;
}

bool RdbIndex::close(bool urgent) {
	bool status = true;
	if (m_needToWrite) {
		status = writeIndex();
	}

	// clears and frees everything
	if (!urgent) {
		reset();
	}

	return status;
}

bool RdbIndex::writeIndex() {
	log(LOG_INFO, "db: Saving %s", m_file.getFilename());

	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	if (g_conf.m_readOnlyMode) {
		logTrace(g_conf.m_logTraceRdbIndex, "END. Read-only mode, not writing index. filename [%s]. Returning true.",
		         m_file.getFilename());
		return true;
	}

	if (!m_needToWrite) {
		logTrace(g_conf.m_logTraceRdbIndex, "END. no need, not writing index. filename [%s]. Returning true.",
		         m_file.getFilename());
		return true;
	}

	// open a new file
	if (!m_file.open(O_RDWR | O_CREAT | O_TRUNC)) {
		logError("END. Could not open %s for writing: %s. Returning false.", m_file.getFilename(), mstrerror(g_errno));
		return false;
	}

	// write index data
	bool status = writeIndex2();

	// on success, we don't need to write it anymore
	if (status) {
		m_needToWrite = false;
	}

	logTrace(g_conf.m_logTraceRdbIndex, "END. filename [%s], returning %s", m_file.getFilename(), status ? "true" : "false");

	return status;
}

bool RdbIndex::writeIndex2() {
	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	g_errno = 0;

	int64_t offset = 0LL;

	// make sure we always write the newest tree
	// remove const as m_file.write does not accept const buffer
	docids_ptr_t tmpDocIds = std::const_pointer_cast<docids_t>(mergePendingDocIds());

	// first 8 bytes is the index version
	m_file.write(&m_version, sizeof(m_version), offset);
	if (g_errno) {
		logError("Failed to write to %s (m_version): %s", m_file.getFilename(), mstrerror(g_errno));
		return false;
	}
	offset += sizeof(m_version);

	// next 8 bytes are the total number of docids in the index file
	size_t docid_count = tmpDocIds->size();

	m_file.write(&docid_count, sizeof(docid_count), offset);
	if (g_errno) {
		logError("Failed to write to %s (docid_count): %s", m_file.getFilename(), mstrerror(g_errno));
		return false;
	}

	offset += sizeof(docid_count);

	m_file.write(&(*tmpDocIds)[0], docid_count * sizeof((*tmpDocIds)[0]), offset);
	if (g_errno) {
		logError("Failed to write to %s (docids): %s", m_file.getFilename(), mstrerror(g_errno));
		return false;
	}

	log(LOG_INFO, "db: Saved %zu index keys for %s", docid_count, getDbnameFromId(m_rdbId));

	logTrace(g_conf.m_logTraceRdbIndex, "END - OK, returning true.");

	return true;
}


bool RdbIndex::readIndex() {
	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	// bail if does not exist
	if (!m_file.doesExist()) {
		logError("Index file [%s] does not exist.", m_file.getFilename());
		logTrace(g_conf.m_logTraceRdbIndex, "END. Returning false");
		return false;
	}

	// . open the file
	// . do not open O_RDONLY because if we are resuming a killed merge
	//   we will add to this index and write it back out.
	if (!m_file.open(O_RDWR)) {
		logError("Could not open index file %s for reading: %s.", m_file.getFilename(), mstrerror(g_errno));
		logTrace(g_conf.m_logTraceRdbIndex, "END. Returning false");
		return false;
	}

	bool status = readIndex2();

	m_file.closeFds();

	logTrace(g_conf.m_logTraceRdbIndex, "END. Returning %s", status ? "true" : "false");

	// return status
	return status;
}

bool RdbIndex::readIndex2() {
	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	g_errno = 0;

	int64_t offset = 0;
	size_t docid_count = 0;

	// first 8 bytes is the index version
	m_file.read(&m_version, sizeof(m_version), offset);
	if (g_errno) {
		logError("Had error reading offset=%" PRId64" from %s: %s", offset, m_file.getFilename(), mstrerror(g_errno));
		return false;
	}
	offset += sizeof(m_version);

	// next 8 bytes are the total number of docids in the index file
	m_file.read(&docid_count, sizeof(docid_count), offset);
	if (g_errno) {
		logError("Had error reading offset=%" PRId64" from %s: %s", offset, m_file.getFilename(), mstrerror(g_errno));
		return false;
	}
	offset += sizeof(docid_count);

	docids_ptr_t tmpDocIds(new docids_t);
	int64_t readSize = docid_count * sizeof((*tmpDocIds)[0]);

	int64_t expectedFileSize = offset + readSize;
	if (expectedFileSize != m_file.getFileSize()) {
		logError("Index file size[%" PRId64"] differs from expected size[%" PRId64"]", m_file.getFileSize(), expectedFileSize);
		return false;
	}

	tmpDocIds->resize(docid_count);
	m_file.read(&(*tmpDocIds)[0], readSize, offset);
	if (g_errno) {
		logError("Had error reading offset=%" PRId64" from %s: %s", offset, m_file.getFilename(), mstrerror(g_errno));
		return false;
	}

	logTrace(g_conf.m_logTraceRdbIndex, "END. Returning true with %zu docIds loaded", tmpDocIds->size());

	// replace with new index
	swapDocIds(tmpDocIds);

	return true;
}

bool RdbIndex::verifyIndex() {
	logTrace( g_conf.m_logTraceRdbIndex, "BEGIN. filename [%s]", m_file.getFilename());

	if (m_version != s_rdbIndexCurrentVersion) {
		logTrace(g_conf.m_logTraceRdbIndex, "END. Index format have changed m_version=%" PRId64" currentVersion=%" PRId64". Returning false",
		         m_version, s_rdbIndexCurrentVersion);
		return false;
	}

	logTrace(g_conf.m_logTraceRdbIndex, "END. Returning true");
	return true;
}

docidsconst_ptr_t RdbIndex::mergePendingDocIds() {
	ScopedLock sl(m_pendingDocIdsMtx);
	return mergePendingDocIds_unlocked();
}

docidsconst_ptr_t RdbIndex::mergePendingDocIds_unlocked() {
	// don't need to merge when there are no pending docIds
	if (m_pendingDocIds->empty()) {
		return getDocIds();
	}

	m_lastMergeTime = gettimeofdayInMilliseconds();

	auto cmplt_fn = [](uint64_t a, uint64_t b) {
		return (a & s_docIdMask) < (b & s_docIdMask);
	};

	auto cmpeq_fn = [](uint64_t a, uint64_t b) {
		return (a & s_docIdMask) == (b & s_docIdMask);
	};

	// merge pending docIds into docIds
	std::stable_sort(m_pendingDocIds->begin(), m_pendingDocIds->end(), cmplt_fn);


	docids_ptr_t tmpDocIds(new docids_t);
	auto docIds = getDocIds();
	std::merge(docIds->begin(), docIds->end(), m_pendingDocIds->begin(), m_pendingDocIds->end(), std::back_inserter(*tmpDocIds), cmplt_fn);

	// in reverse because we want to keep the newest entry
	auto it = std::unique(tmpDocIds->rbegin(), tmpDocIds->rend(), cmpeq_fn);
	tmpDocIds->erase(tmpDocIds->begin(), it.base());

	// replace existing even if size doesn't change (could change from positive to negative key)
	swapDocIds(tmpDocIds);
	m_pendingDocIds->clear();

	return getDocIds();
}

void RdbIndex::addRecord(char *key) {
	ScopedLock sl(m_pendingDocIdsMtx);
	addRecord_unlocked(key, false);
}

void RdbIndex::addRecord_unlocked(char *key, bool isGenerateIndex) {
	m_needToWrite = true;

	if (m_rdbId == RDB_POSDB || m_rdbId == RDB2_POSDB2) {
		if (key[0] & 0x02 || !(key[0] & 0x04)) {
			//it is a 12-byte docid+pos or 18-byte termid+docid+pos key
			uint64_t keyneg = !KEYNEG(key);
			uint64_t doc_id = ((extract_bits(key, 58, 96) << s_docIdOffset) | keyneg);
			if (doc_id != m_prevPendingDocId) {
				m_pendingDocIds->push_back(doc_id);
				m_prevPendingDocId = doc_id;
			}
		}
	} else {
		logError("Not implemented for dbname=%s", getDbnameFromId(m_rdbId));
		gbshutdownLogicError();
	}

	bool doMerge = false;
	if (isGenerateIndex) {
		if (m_pendingDocIds->size() >= s_generateMaxPendingSize) {
			doMerge = true;
		}
	} else {
		if ((m_pendingDocIds->size() >= s_defaultMaxPendingSize) ||
		    (gettimeofdayInMilliseconds() - m_lastMergeTime >= s_defaultMaxPendingTimeMs)) {
			doMerge = true;
		}
	}

	if (doMerge) {
		(void)mergePendingDocIds_unlocked();
	}
}

void RdbIndex::printIndex() {
	auto docIds = getDocIds();
	for (auto it = docIds->begin(); it != docIds->end(); ++it) {
		logf(LOG_DEBUG, "inindex: docId=%" PRIu64" isDel=%d", (*it >> s_docIdOffset), ((*it & s_delBitMask) == 0));
	}

	ScopedLock sl(m_pendingDocIdsMtx);
	for (auto it = m_pendingDocIds->begin(); it != m_pendingDocIds->end(); ++it) {
		logf(LOG_DEBUG, "pending: docId=%" PRIu64" isDel=%d", (*it >> s_docIdOffset), ((*it & s_delBitMask) == 0));
	}
}


void RdbIndex::addList(RdbList *list) {
	// sanity check
	if (list->getKeySize() != m_ks) {
		g_process.shutdownAbort(true);
	}

	// . do not reset list, because of HACK in RdbDump.cpp we set m_listPtrHi < m_list
	//   so our first key can be a half key, calling resetListPtr()
	//   will reset m_listPtrHi and fuck it up

	// bail now if it's empty
	if (list->isEmpty()) {
		return;
	}

	char key[MAX_KEY_BYTES];

	ScopedLock sl(m_pendingDocIdsMtx);
	for (; !list->isExhausted(); list->skipCurrentRecord()) {
		list->getCurrentKey(key);
		addRecord_unlocked(key, true);
	}
}

bool RdbIndex::generateIndex(collnum_t collnum, const RdbTree *tree) {
	reset();

	if (g_conf.m_readOnlyMode) {
		return false;
	}

	log(LOG_INFO, "db: Generating index for %s tree", getDbnameFromId(m_rdbId));

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbList list;
	if (!tree->getList(collnum, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, m_useHalfKeys)) {
		return false;
	}

	list.resetListPtr();
	addList(&list);

	// make sure it's all sorted and merged
	(void)mergePendingDocIds();

	return true;
}

bool RdbIndex::generateIndex(collnum_t collnum, const RdbBuckets *buckets) {
	reset();

	if (g_conf.m_readOnlyMode) {
		return false;
	}

	log(LOG_INFO, "db: Generating index for %s buckets", getDbnameFromId(m_rdbId));

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbList list;
	if (!buckets->getList(collnum, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, m_useHalfKeys)) {
		return false;
	}

	list.resetListPtr();
	addList(&list);

	// make sure it's all sorted and merged
	(void)mergePendingDocIds();

	return true;
}

// . attempts to auto-generate from data file, f
// . returns false and sets g_errno on error
bool RdbIndex::generateIndex(BigFile *f) {
	reset();

	if (g_conf.m_readOnlyMode) {
		return false;
	}

	log(LOG_INFO, "db: Generating index for %s/%s", f->getDir(), f->getFilename());

	if (!f->doesPartExist(0)) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "db: Cannot generate index for this headless data file");
		return false;
	}

	// scan through all the recs in f
	int64_t offset = 0;
	const int64_t fileSize = f->getFileSize();

	// if file is length 0, we don't need to do much
	// g_errno should be set on error
	if (fileSize == 0 || fileSize < 0) {
		return (fileSize == 0);
	}

	// don't read in more than 10 megs at a time initially
	int64_t bufSize = fileSize;
	if (bufSize > 10 * 1024 * 1024) {
		bufSize = 10 * 1024 * 1024;
	}
	char *buf = (char *)mmalloc(bufSize, "RdbIndex");

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();

	// a rec needs to be at least this big
	// negative keys do not have the dataSize field
	int32_t minRecSize = (m_fixedDataSize == -1) ? 0 : m_fixedDataSize;

	// POSDB
	if (m_ks == 18) {
		minRecSize += 6;
	} else if (m_useHalfKeys) {
		minRecSize += m_ks - 6;
	} else {
		minRecSize += m_ks;
	}

	// for parsing the lists into records
	char key[MAX_KEY_BYTES];
	int64_t next = 0LL;

	ScopedLock sl(m_pendingDocIdsMtx);
	// read in at most "bufSize" bytes with each read
	for (; offset < fileSize;) {
		// keep track of how many bytes read in the log
		if (offset >= next) {
			if (next != 0) {
				logf(LOG_INFO, "db: Read %" PRId64" bytes [%s]", next, f->getFilename());
			}

			next += 500000000; // 500MB
		}

		// our reads should always block
		int64_t readSize = fileSize - offset;
		if (readSize > bufSize) {
			readSize = bufSize;
		}

		// if the readSize is less than the minRecSize, we got a bad cutoff
		// so we can't go any more
		if (readSize < minRecSize) {
			mfree(buf, bufSize, "RdbMap");
			return true;
		}

		// otherwise, read it in
		if (!f->read(buf, readSize, offset)) {
			mfree(buf, bufSize, "RdbMap");
			log(LOG_WARN, "db: Failed to read %" PRId64" bytes of %s at offset=%" PRId64". Map generation failed.",
			    bufSize, f->getFilename(), offset);
			return false;
		}

		RdbList list;

		// set the list
		list.set(buf, readSize, buf, readSize, startKey, endKey, m_fixedDataSize, false, m_useHalfKeys, m_ks);

		// . HACK to fix useHalfKeys compression thing from one read to the nxt
		// . "key" should still be set to the last record we read last read
		if (offset > 0) {
			// ... fix for posdb!!!
			if (m_ks == 18) {
				list.setListPtrLo(key + (m_ks - 12));
			} else {
				list.setListPtrHi(key + (m_ks - 6));
			}
		}

		bool advanceOffset = true;

		// . parse through the records in the list
		for (; !list.isExhausted(); list.skipCurrentRecord()) {
			char *rec = list.getCurrentRec();
			if (rec + 64 > list.getListEnd() && offset + readSize < fileSize) {
				// set up so next read starts at this rec that MAY have been cut off
				offset += (rec - buf);

				advanceOffset = false;
				break;
			}

			// WARNING: when data is corrupted these may cause segmentation faults?
			list.getCurrentKey(key);
			int32_t recSize = list.getCurrentRecSize();

			// don't chop keys
			if (recSize < 6) {
				log(LOG_WARN, "db: Got negative recsize of %" PRId32" at offset=%" PRId64, recSize,
				    offset + (rec - buf));

				// @todo ALC do we want to abort here?
				return false;
			}

			// do we have a breech?
			if (rec + recSize > buf + readSize) {
				// save old
				int64_t oldOffset = offset;

				// set up so next read starts at this rec that got cut off
				offset += (rec - buf);

				// . if we advanced nothing, then we'll end up looping forever
				// . this will == 0 too, for big recs that did not fit in our
				//   read but we take care of that below
				// . this can happen if merge dumped out half-ass
				// . the write split a record...
				if (rec - buf == 0 && recSize <= bufSize) {
					log(LOG_WARN,
					    "db: Index generation failed because last record in data file was split. Power failure while writing?");

					// @todo ALC do we want to abort here?
					return false;
				}

				// is our buf big enough to hold this type of rec?
				if (recSize > bufSize) {
					mfree(buf, bufSize, "RdbIndex");
					bufSize = recSize;
					buf = (char *)mmalloc(bufSize, "RdbIndex");
					if (!buf) {
						log(LOG_WARN, "db: Got error while generating the index file: %s. offset=%" PRIu64".",
						    mstrerror(g_errno), oldOffset);
						return false;
					}
				}
				// read again starting at the adjusted offset
				advanceOffset = false;
				break;
			}

			addRecord_unlocked(key, true);
		}

		if (advanceOffset) {
			offset += readSize;
		}
	}

	// don't forget to free this
	mfree(buf, bufSize, "RdbMap");

	// make sure it's all sorted and merged
	(void)mergePendingDocIds_unlocked();

	// otherwise, we're done
	return true;
}

docidsconst_ptr_t RdbIndex::getDocIds() {
	ScopedLock sl(m_docIdsMtx);
	return m_docIds;
}

void RdbIndex::swapDocIds(docidsconst_ptr_t docIds) {
	ScopedLock sl(m_docIdsMtx);
	m_docIds.swap(docIds);
}
