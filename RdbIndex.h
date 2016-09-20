#ifndef GB_RDBINDEX_H
#define GB_RDBINDEX_H

#include "BigFile.h"
#include "Sanity.h"
#include "GbMutex.h"
#include <vector>
#include <memory>

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

	// . does not write data to disk
	// . frees all
	void reset();

	// set the filename, and if it's fixed data size or not
	void set(const char *dir, const char *indexFilename, int32_t fixedDataSize, bool useHalfKeys, char keySize, rdbid_t rdbId);

	bool rename(const char *newIndexFilename, bool force = false) {
		return m_file.rename(newIndexFilename, NULL, force);
	}

	bool rename(const char *newIndexFilename, void (*callback)(void *state), void *state, bool force = false) {
		return m_file.rename(newIndexFilename, callback, state, force);
	}

	char *getFilename() { return m_file.getFilename(); }
	const char *getFilename() const { return m_file.getFilename(); }

	BigFile *getFile  ( ) { return &m_file; }

	bool close( bool urgent );

	bool writeIndex();
	bool writeIndex2();

	bool readIndex();
	bool readIndex2();

	bool verifyIndex(int64_t dataFileSize);

	bool unlink() { return m_file.unlink(); }

	bool unlink(void (*callback)(void *state), void *state) {
		return m_file.unlink(callback, state);
	}

	// . attempts to auto-generate from data file
	// . returns false and sets g_errno on error
	bool generateIndex(BigFile *f);
	bool generateIndex(RdbBuckets *buckets, collnum_t collnum);
	bool generateIndex(RdbTree *tree, collnum_t collnum);

	void addList(RdbList *list);

	void addRecord(char *key);

	docidsconst_ptr_t getDocIds();

private:
	void addRecord_unlocked(char *key, bool isGenerateIndex);
	docidsconst_ptr_t mergePendingDocIds();
	void swapDocIds(docidsconst_ptr_t docIds);

	void printIndex();

	// the index file
	BigFile m_file;

	int32_t m_fixedDataSize;
	bool m_useHalfKeys;
	char m_ks;
	rdbid_t m_rdbId;

	// verification
	int64_t m_dataFileSize;

	// always sorted
	docidsconst_ptr_t m_docIds;
	GbMutex m_docIdsMtx;

	// newest record pending merge into m_docIds
	docids_ptr_t m_pendingDocIds;
	GbMutex m_pendingDocIdsMtx;
	uint64_t m_prevPendingDocId;

	int64_t m_lastMergeTime;

	// when close is called, must we write the index?
	bool m_needToWrite;
};

#endif // GB_RDBINDEX_H
