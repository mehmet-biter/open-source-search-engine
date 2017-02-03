// Zak Betz,  Copyright, Jun 2008

// A sorted list of sorted lists for storing rdb keys.  Faster and more
// cache friendly than RdbTree.  Optimized for batched adds, amortized O(1)
// add operation, O(log n) for retrival, ranged getList for k keys is 
// O(log(n) + k) where as RdbTree is O(k * log(n)).
// Memory is allocated and used on a on demand basis rather than all up
// front as with RdbTree, so memory usage is much lower most of the time.

// Collections are handled as a linked list, each RdbBuckets has a nextColl
// pointer.  The front bucket acts as the gatekeeper for all of the
// other buckets, Only it's values for needsSave and isWritable are 
// significant

//when selecting bucketnum and also when deduping, use KEYCMPNEGEQ which
//will mask off the delbit, that way pos and neg keys will be in the same
//bucket and only the newest key will survive.  When getting or deleting
//a list, use KEYCMP within a bucket and use KEYCMPNEGEQ to select the 
//bucket nums.  This is because iterators in rdb dump get a list, then
//add 1 to a key and get the next list and adding 1 to a pos key will get
//the negative one.

#ifndef GB_RDBBUCKETS_H
#define GB_RDBBUCKETS_H

#include <cstdint>
#include <functional>
#include "rdbid_t.h"
#include "types.h"
#include "GbMutex.h"

class BigFile;
class RdbList;
class RdbTree;
class RdbBuckets;

/**
 * Data is stored in m_keys
 *
 * when fixedDataSize == 0 (no data)
 * - recSize == keySize
 *
 * when fixedDataSize > 0 (fixed sized data)
 * - recSize == keySize + sizeof(char*)
 *
 * when fixedDataSize == -1 (variable sized data)
 * - recSize == keySize + sizeof(char*) + sizeof(int32_t)
 */
class RdbBucket {
public:
	RdbBucket();
	~RdbBucket();

	bool set(RdbBuckets *parent, char *newbuf);
	void reset();
	void reBuf(char *newbuf);

	char *getFirstKey();
	const char *getFirstKey() const { return const_cast<RdbBucket *>(this)->getFirstKey(); }

	const char *getEndKey() const { return m_endKey; }

	int32_t getNumKeys() const { return m_numKeys; }
	int32_t getNumSortedKeys() const { return m_lastSorted; }

	const char *getKeys() const { return m_keys; }

	collnum_t getCollnum() const { return m_collnum; }
	void setCollnum(collnum_t c) { m_collnum = c; }

	bool addKey(const char *key, const char *data, int32_t dataSize);
	char *getKeyVal(const char *key, char **data, int32_t *dataSize);

	int32_t getNode(const char *key); //returns -1 if not found
	int32_t getNumNegativeKeys() const;

	bool getList(RdbList *list, const char *startKey, const char *endKey, int32_t minRecSizes,
	             int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys);

	bool deleteNode(int32_t i);

	bool deleteList(RdbList *list);

	int getListSizeExact(const char *startKey, const char *endKey);

	//Save State
	int64_t fastSave_r(int fd, int64_t offset);
	int64_t fastLoad(BigFile *f, int64_t offset);
	
	//Debug
	bool selfTest(int32_t bucketnum, const char *prevKey);
	void printBucket(int32_t idx, std::function<void(const char*, int32_t)> print_fn = nullptr);
	void printBucketStartEnd(int32_t idx);

	bool sort();
	RdbBucket *split(RdbBucket *newBucket);

private:
	char *m_endKey;
	char *m_keys;
	RdbBuckets *m_parent;
	int32_t m_numKeys;
	int32_t m_lastSorted;
	collnum_t m_collnum;
};

class RdbBuckets {
	friend class RdbBucket;

public:
	RdbBuckets( );
	~RdbBuckets( );
	void clear();
	void reset();

	bool set(int32_t fixedDataSize, int32_t maxMem, const char *allocName, rdbid_t rdbId, const char *dbname, char keySize);

	bool resizeTable(int32_t numNeeded);

	int32_t addNode(collnum_t collnum, const char *key, const char *data, int32_t dataSize);

	bool addList(collnum_t collnum, RdbList *list);

	char *getKeyVal(collnum_t collnum, const char *key, char **data, int32_t *dataSize);

	bool getList(collnum_t collnum, const char *startKey, const char *endKey, int32_t minRecSizes, RdbList *list,
	             int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys) const;

	bool deleteNode(collnum_t collnum, const char *key);

	bool deleteList(collnum_t collnum, RdbList *list);

	int64_t getListSize(collnum_t collnum, const char *startKey, const char *endKey, char *minKey, char *maxKey) const;

	int getListSizeExact(collnum_t collnum, const char *startKey, const char *endKey);


	bool addBucket (RdbBucket *newBucket, int32_t i);
	int32_t getBucketNum(collnum_t collnum, const char *key) const;
	char bucketCmp(collnum_t acoll, const char *akey, RdbBucket* b) const;

	bool collExists(collnum_t coll) const;

	const RdbBucket* getBucket(int i) const { return m_buckets[i]; }
	int32_t getNumBuckets() const { return m_numBuckets; }

	const char *getDbname() const { return m_dbname; }

	uint8_t getKeySize() const { return m_ks; }
	int32_t getFixedDataSize() const { return m_fixedDataSize; }
	int32_t getRecSize() const { return m_recSize; }

	void setSwapBuf(char *s) { m_swapBuf = s; }
	char *getSwapBuf() { return m_swapBuf; }

	bool needsSave() const { return m_needsSave; }
	bool isSaving() const { return m_isSaving; }

	char *getSortBuf() { return m_sortBuf; }
	int32_t getSortBufSize() const { return m_sortBufSize; }

	bool isWritable() const { return m_isWritable; }
	void disableWrites() { m_isWritable = false; }
	void enableWrites() { m_isWritable = true; }

	int32_t getMaxMem() const { return m_maxMem; }

	void setNeedsSave(bool s);

	int32_t getMemAllocated() const;
	int32_t getMemAvailable() const;
	bool is90PercentFull() const;
	bool needsDump() const;
	bool hasRoom(int32_t numRecs) const;

	int32_t getNumKeys() const;
	int32_t getMemOccupied() const;

	int32_t getNumNegativeKeys() const;
	int32_t getNumPositiveKeys() const;

	void cleanBuckets();
	bool delColl(collnum_t collnum);

	//just for this collection
	int32_t getNumKeys(collnum_t collnum) const;

	//DEBUG
	void verifyIntegrity();
	bool selfTest(bool thorough, bool core);
	int32_t addTree(RdbTree *rt);
	void printBuckets(std::function<void(const char*, int32_t)> print_fn = nullptr);
	void printBucketsStartEnd();

	bool repair();
	bool testAndRepair();

	//Save/Load/Dump
	bool fastSave(const char *dir, bool useThread, void *state, void (*callback)(void *state));
	bool loadBuckets(const char *dbname);

private:
	//syntactic sugar
	RdbBucket* bucketFactory();
	void updateNumRecs(int32_t n, int32_t bytes, int32_t numNeg);

	bool fastSave_r();
	int64_t fastSaveColl_r(int fd);
	bool fastLoad(BigFile *f, const char *dbname);
	int64_t fastLoadColl(BigFile *f, const char *dbname);

private:
	GbMutex m_mtx;
	RdbBucket **m_buckets;
	RdbBucket *m_bucketsSpace;
	char *m_masterPtr;
	int32_t m_masterSize;
	int32_t m_firstOpenSlot;	//first slot in m_bucketSpace that is available (never-used or empty)
	int32_t m_numBuckets;		//number of used buckets
	int32_t m_maxBuckets;		//current number of (pre-)allocated buckets
	uint8_t m_ks;
	int32_t m_fixedDataSize;
	int32_t m_recSize;
	int32_t m_numKeysApprox;//includes dups
	int32_t m_numNegKeys;
	int32_t m_maxMem;
	int32_t m_maxBucketsCapacity;	//max number of buckets given the memory limit
	int32_t m_dataMemOccupied;

	rdbid_t m_rdbId;
	const char *m_dbname;
	char *m_swapBuf;
	char *m_sortBuf;
	int32_t m_sortBufSize;

	bool m_repairMode;
	bool m_isWritable;
	bool m_isSaving;
	// true if buckets was modified and needs to be saved
	bool m_needsSave;
	const char *m_dir;
	void *m_state;

	void (*m_callback)(void *state);

	int32_t m_saveErrno;
	const char *m_allocName;
};

#endif // GB_RDBBUCKETS_H
