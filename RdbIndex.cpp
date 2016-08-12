#include "gb-include.h"

#include "RdbIndex.h"
#include "BigFile.h"
#include "Titledb.h"	// For MAX_DOCID
#include "Process.h"
#include "BitOperations.h"
#include "Conf.h"

RdbIndex::RdbIndex() {
	reset();
}

// dont save index on deletion!
RdbIndex::~RdbIndex() {
	reset();
}

void RdbIndex::set(const char *dir, const char *indexFilename,
                   int32_t fixedDataSize , bool useHalfKeys , char keySize, char rdbId) {
	logTrace(g_conf.m_logTraceRdbIndex, "BEGIN. dir [%s], indexFilename [%s]", dir, indexFilename);

	reset();
	m_fixedDataSize = fixedDataSize;
	m_file.set ( dir , indexFilename );
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


void RdbIndex::reset() {
	//@todo: IMPLEMENT!
//	log( LOG_ERROR,"%s:%s: NOT IMPLEMENTED YET", __FILE__, __func__);

	m_lastDocId = MAX_DOCID + 1;
	m_needToWrite = false;

	m_docIds.clear();

	//@@@ free mem here

	m_file.reset();
}


bool RdbIndex::writeIndex() {
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
		logError("END. Could not open %s for writing: %s. Returning false.", m_file.getFilename(), mstrerror(g_errno))
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

	// the current disk offset
	int64_t offset = 0LL;
	g_errno = 0;

	// first 8 bytes are the size of the DATA file we're indexing
	size_t total_size = sizeof(m_docIds.front()) * m_docIds.size();
	m_file.write(&total_size, sizeof(total_size), offset);
	if ( g_errno )  {
		logError("Failed to write to %s (total_size): %s", m_file.getFilename(), mstrerror(g_errno))
		return false;
	}
	offset += sizeof(total_size);

	m_file.write(&m_docIds[0], total_size, offset);
	if ( g_errno )  {
		logError("Failed to write to %s (total_size): %s", m_file.getFilename(), mstrerror(g_errno))
		return false;
	}

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
	g_errno = 0;

	//@todo: IMPLEMENT!
	logError("NOT IMPLEMENTED YET");

#if 0
	// first 8 bytes are the size of the DATA file we're indexing
	m_file.read ( &m_offset , 8 , offset );
	if ( g_errno ) {
		log( LOG_WARN, "db: Had error reading %s: %s.", m_file.getFilename(),mstrerror(g_errno));
		return false;
	}
	offset += 8;
#endif
	return true;
}


bool RdbIndex::addRecord(char rdbId, char *key) {

	if (rdbId == RDB_POSDB) {

		if (key[0] & 0x02 || !(key[0] & 0x04)) {
			//it is a 12-byte docid+pos or 18-byte termid+docid+pos key

			uint64_t doc_id = extract_bits(key, 58, 96);
			if (doc_id != m_lastDocId) {
				log(LOG_ERROR, "@@@ GOT DocId %" PRIu64 "", doc_id);

				m_lastDocId = doc_id;

				//@todo: IMPLEMENT!
				logError("ADD TO INDEX - NOT IMPLEMENTED YET");

				m_needToWrite = true;
			}
		}
	}
	return true;
}


void RdbIndex::printIndex() {
	//@todo: IMPLEMENT!
	logError("NOT IMPLEMENTED YET");
}

#include <set>
#include <unordered_set>
#include <algorithm>

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
	int64_t fileSize = f->getFileSize();

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
	int32_t minRecSize = 0;

	// negative keys do not have the dataSize field... so undo this
	if (m_fixedDataSize == -1) {
		minRecSize += 0;
	} else {
		minRecSize += m_fixedDataSize;
	}

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
	int32_t recSize = 0;
	char *rec = buf;
	int64_t next = 0LL;

	std::unordered_set<uint64_t> unique_docids_set;
	std::pair<std::unordered_set<uint64_t>::iterator,bool> result;

	uint64_t docid = 0;
	int64_t total = 0;

	// read in at most "bufSize" bytes with each read
readLoop:
	// keep track of how many bytes read in the log
	if (offset >= next) {
		if (next != 0) {
			logf(LOG_INFO, "db: Read %" PRId64" bytes [%s]", next, f->getFilename());
		}

		next += 500000000; // 500MB
	}

	// our reads should always block
	int64_t readSize = fileSize - offset;
	if ( readSize > bufSize ) {
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
	if ( offset > 0 ) {
		// ... fix for posdb!!!
		if (m_ks == 18) {
			list.m_listPtrLo = key + (m_ks - 12);
		} else {
			list.m_listPtrHi = key + (m_ks - 6);
		}
	}

	// . parse through the records in the list
	// . stolen from RdbMap::addList()
nextRec:
	rec = list.getCurrentRec ();
	if (rec + 64 > list.getListEnd() && offset + readSize < fileSize) {
		// set up so next read starts at this rec that MAY have been
		// cut off
		offset += (rec - buf);
		goto readLoop;
	}

	// WARNING: when data is corrupted these may cause segmentation faults?
	list.getCurrentKey(key);
	recSize = list.getCurrentRecSize();

	// don't chop keys
	if ( recSize < 6 ) {
		log( LOG_WARN, "db: Got negative recsize of %" PRId32" at offset=%" PRId64, recSize , offset + (rec-buf));
//		log( LOG_WARN, "db: Got negative recsize of %" PRId32" at offset=%" PRId64" lastgoodoff=%" PRId64,
//		     recSize , offset + (rec-buf), m_offset );
//
//		// it truncates to m_offset!
//		if (truncateFile(f)) {
//			goto done;
//		}
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
			log(LOG_WARN, "db: Index generation failed because last record in data file was split. Power failure while "
					"writing? Truncating file to %" PRId64" bytes. (lost %" PRId64" bytes)", offset, fileSize - offset);

			// when merge resumes it call our getFileSize()
			// in RdbMerge.cpp::gotLock() to set the dump offset
			// otherwise, if we don't do this and write data
			// in the middle of a split record AND then we crash
			// without saving the map again, the second call to
			// generateMap() will choke on that boundary and
			// we'll lose a massive amount of data like we did
			// with newspaperarchive
//			m_offset = offset;
			goto done;
		}

		// is our buf big enough to hold this type of rec?
		if (recSize > bufSize) {
			mfree(buf, bufSize, "RdbIndex");
			bufSize = recSize;
			buf = (char *) mmalloc(bufSize, "RdbIndex");
			if (!buf) {
				log(LOG_WARN, "db: Got error while generating the index file: %s. offset=%" PRIu64".",
				    mstrerror(g_errno), oldOffset);
				return false;
			}
		}
		// read agin starting at the adjusted offset
		goto readLoop;
	}

	docid = extract_bits(key, 58, 96);
	result = unique_docids_set.insert(docid);
	if (result.second) {
		m_docIds.push_back(docid);
	}

	++total;

//	if (!addRecord(key, rec, recSize)) {
//		// if it was key out of order, it might be because the
//		// power went out and we ended up writing a a few bytes of
//		// garbage then a bunch of 0's at the end of the file.
//		// if the truncate works out then we are done.
//		if (g_errno == ECORRUPTDATA && truncateFile(f)) {
//			goto done;
//		}
//
//		// otherwise, give it up
//		mfree(buf, bufSize, "RdbIndex");
//		log(LOG_WARN, "db: Index generation failed: %s.", mstrerror(g_errno));
//		return false;
//	}

	// skip current good record now
	if (list.skipCurrentRecord()) {
		goto nextRec;
	}

	// advance offset
	offset += readSize;

	// loop if more to go
	if ( offset < fileSize ) {
		goto readLoop;
	}

done:
	// don't forget to free this
	mfree(buf, bufSize, "RdbMap");

	if (!m_docIds.empty()) {
		std::sort(m_docIds.begin(), m_docIds.end());
		m_needToWrite = true;
	}

	logf(LOG_DEBUG, "@@@ totalRec=%lu unique=%lu", total, unique_docids_set.size());

//	// if there was bad data we probably added out of order keys
//	if (m_needVerify) {
//		logError("Fixing map for [%s]. Added at least %" PRId64" bad keys.", f->getFilename(), m_badKeys);
//
//		verifyMap2();
//		m_needVerify = false;
//	}

	// otherwise, we're done
	return true;
}


