#ifndef GB_RDBINDEX_H
#define GB_RDBINDEX_H

#include "BigFile.h"
#include "Sanity.h"
#include <vector>

class RdbTree;
class RdbBuckets;

class RdbIndex {
public:
	RdbIndex();
	~RdbIndex();

	// . does not write data to disk
	// . frees all
	void reset();

	// set the filename, and if it's fixed data size or not
	void set(const char *dir, const char *indexFilename, int32_t fixedDataSize, bool useHalfKeys, char keySize, char rdbId);

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

	bool unlink() { return m_file.unlink(); }

	bool unlink(void (*callback)(void *state), void *state) {
		return m_file.unlink(callback, state);
	}

	// . attempts to auto-generate from data file
	// . returns false and sets g_errno on error
	bool generateIndex(BigFile *f);
	bool generateIndex(RdbBuckets *buckets, collnum_t collnum);
	bool generateIndex(RdbTree *tree, collnum_t collnum);

	bool addRecord ( char rdbId, char *key);

private:
	void printIndex();

	// the index file
	BigFile m_file;

	std::vector<uint64_t> m_docIds;

	int32_t m_fixedDataSize;
	bool m_useHalfKeys;
	char m_ks;
	char m_rdbId;

	uint64_t m_lastDocId;

	// when close is called, must we write the index?
	bool m_needToWrite;
};

#endif // GB_RDBINDEX_H
