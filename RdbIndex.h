#ifndef GB_RDBINDEX_H
#define GB_RDBINDEX_H

#include "BigFile.h"
#include "rdbid_t.h"
#include "Sanity.h"
#include "GbMutex.h"
#include <vector>
#include <memory>
#include <atomic>

class RdbTree;
class RdbBuckets;
class RdbList;

typedef std::vector<uint64_t> docids_t;
typedef std::shared_ptr<docids_t> docids_ptr_t;
typedef std::shared_ptr<const docids_t> docidsconst_ptr_t;

class RdbIndex {
public:
	RdbIndex();
	~RdbIndex();

	static void timedMerge(int /*fd*/, void *state);
	static void mergePendingDocIds(void *state);

	// . does not write data to disk
	// . frees all
	void reset(bool isStatic);

	// set the filename, and if it's fixed data size or not
	void set(const char *dir, const char *indexFilename, int32_t fixedDataSize, bool useHalfKeys, char keySize,
	         rdbid_t rdbId, bool isStatic);

	bool rename(const char *newIndexFilename) {
		return m_file.rename(newIndexFilename, NULL);
	}

	bool rename(const char *newIndexFilename, const char *newDir, void (*callback)(void *state), void *state) {
		return m_file.rename(newIndexFilename, newDir, callback, state);
	}

	const char *getFilename() const { return m_file.getFilename(); }

	BigFile *getFile  ( ) { return &m_file; }

	bool close( bool urgent );

	bool writeIndex(bool finalWrite);
	bool readIndex();

	bool verifyIndex();

	bool unlink() { return m_file.unlink(); }

	bool unlink(void (*callback)(void *state), void *state) {
		return m_file.unlink(callback, state);
	}

	// . attempts to auto-generate from data file
	// . returns false and sets g_errno on error
	bool generateIndex(BigFile *f);
	bool generateIndex(collnum_t collnum, const RdbBuckets *buckets);
	bool generateIndex(collnum_t collnum, const RdbTree *tree);

	void addList(RdbList *list);

	void addRecord(const char *key);

	// key format
	// ........ ........ ........ dddddddd  d = docId
	// dddddddd dddddddd dddddddd dddddd.Z  Z = delBit
	docidsconst_ptr_t getDocIds();

	bool exist(uint64_t docId);

	void printIndex();

	static const char s_docIdOffset = 2;
	static const uint64_t s_docIdMask = ~0x03ULL;
	static const uint64_t s_delBitMask = 0x01ULL;

private:
	void addRecord_unlocked(const char *key);

	bool writeIndex2(bool finalWrite);
	bool readIndex2();

	docidsconst_ptr_t mergePendingDocIds(bool finalWrite = false);
	docidsconst_ptr_t mergePendingDocIds_unlocked(bool finalWrite = false);

	void swapDocIds(docidsconst_ptr_t docIds);

	// the index file
	BigFile m_file;

	int32_t m_fixedDataSize;
	bool m_useHalfKeys;
	char m_ks;
	rdbid_t m_rdbId;

	// verification
	int64_t m_version;

	// always sorted
	docidsconst_ptr_t m_docIds;
	GbMutex m_docIdsMtx;

	// newest record pending merge into m_docIds
	GbMutex m_pendingDocIdsMtx;
	pthread_cond_t m_pendingMergeCond;
	bool m_pendingMerge;

	docids_ptr_t m_pendingDocIds;
	uint64_t m_prevPendingDocId;

	int64_t m_lastMergeTime;

	// when close is called, must we write the index?
	bool m_needToWrite;

	bool m_registeredCallback;

	std::atomic<bool> m_generatingIndex;
};

#endif // GB_RDBINDEX_H
