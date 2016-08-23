// Matt Wells, copyright Apr 2001

// . non-blocking dump of an RdbTree to an RdbFile
// . RdbFile can be used with RdbGet even in the middle of dumping
// . uses a little mem for an RdbMap and some for write buffering
// . frees the nodes as it dumps them to disk (flushes cache)
// . can also do a non-key-ordered dump for quick saving of an RdbTree
// . Then you can use RdbDump::load() to load it back to the tree

#ifndef GB_RDBDUMP_H
#define GB_RDBDUMP_H

#include "BigFile.h"
#include "RdbList.h"

class Rdb;
class RdbBuckets;
class RdbCache;
class RdbMap;
class RdbTree;


class RdbDump {
public:
    RdbDump() { m_isDumping = false; }

	void reset();

	bool isDumping() const { return m_isDumping; }

	// . set up for a dump of rdb records to a file
	// . returns false and sets errno on error
	bool set(collnum_t collnum,
	         BigFile *file,
	         int32_t id2, // in Rdb::m_files[] array
	         RdbBuckets *buckets, // optional buckets to dump
	         RdbTree *tree, // optional tree to dump
	         RdbMap *map,
	         RdbCache *cache, // for caching dumped tree
	         int32_t maxBufSize,
	         bool dedup, // for merging tree into cache
	         int32_t niceness,
	         void *state,
	         void (*callback )(void *state),
	         bool useHalfKeys,
	         int64_t startOffset,
	         const char *prevLastKey,
	         char keySize,
	         void *pc,
	         int64_t maxFileSize,
	         Rdb *rdb);

	// a niceness of 0 means to block on the dumping
	int32_t getNiceness() { return m_niceness; }

	// . dump the tree to the file
	// . returns false if blocked, true otherwise
	bool dumpTree(bool recall);

	bool dumpList(RdbList *list, int32_t niceness, bool isRecall);

	void doneDumping();

	bool doneReadingForVerify();

	// called when we've finished writing an RdbList to the file
	bool doneDumpingList(bool addToMap);

	char *getFirstKeyInQueue() { return m_firstKeyInQueue; }

	char *getLastKeyInQueue() { return m_lastKeyInQueue; }

	void continueDumping();

	// private:

	bool m_isDumping; // true if we're in the middle of dumping

	// true if the actual write thread is outstanding
	bool m_writing;

	RdbTree *m_tree;
	RdbBuckets *m_buckets;
	RdbMap *m_map;
	RdbCache *m_cache;
	int32_t m_maxBufSize;
	bool m_dedup; // used for merging/adding tree to cache
	void *m_state;

	void (*m_callback)(void *state);

	int64_t m_offset;

	BigFile *m_file;
	int32_t m_id2; // secondary id of file we are dumping to
	RdbList *m_list; // holds list to dump
	RdbList m_ourList; // we use for dumping a tree, point m_list
	char *m_buf; // points into list
	char *m_verifyBuf;
	int32_t m_verifyBufSize;
	int32_t m_bytesToWrite;
	int32_t m_bytesWritten;
	char m_addToMap;

	char m_firstKeyInQueue[MAX_KEY_BYTES];
	char *m_lastKeyInQueue;

	char m_prevLastKey[MAX_KEY_BYTES];

	int32_t m_nextNode;
	char m_nextKey[MAX_KEY_BYTES];
	bool m_rolledOver; // true if m_nextKey rolls back to 0

	// . file descriptor of file #0 in the BigFile
	// . we're dumping to this guy
	int m_fd;

	// we pass this to BigFile::write() to do non-blocking writes
	FileState m_fstate;

	// a niceness of 0 means the dump will block, otherwise, will not
	int32_t m_niceness;

	bool m_useHalfKeys;
	bool m_hacked;
	bool m_hacked12;

	int32_t m_totalPosDumped;
	int32_t m_totalNegDumped;

	// recall info
	int64_t m_t1;
	int32_t m_numPosRecs;
	int32_t m_numNegRecs;

	// for setting m_rdb->m_needsSave after deleting list from tree
	Rdb *m_rdb;

	collnum_t m_collnum;

	bool m_doCollCheck;

	bool m_tried;

	bool m_isSuspended;

	char m_ks;
};

#endif // GB_RDBDUMP_H
