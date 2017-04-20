#include "RdbBuckets.h"
#include "BigFile.h"      // for saving and loading the tree
#include "RdbList.h"
#include "sort.h"
#include <unistd.h>
#include "Rdb.h"
#include "Sanity.h"
#include "Conf.h"
#include "Collectiondb.h"
#include "Mem.h"
#include "ScopedLock.h"
#include <fcntl.h>
#include "Posdb.h"

#define BUCKET_SIZE 8192
#define INIT_SIZE 4096
#define SAVE_VERSION 0


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

	const char *getFirstKey();
	const char *getFirstKey() const { return const_cast<RdbBucket *>(this)->getFirstKey(); }

	const char *getEndKey() const { return m_endKey; }

	int32_t getNumKeys() const { return m_numKeys; }
	int32_t getNumSortedKeys() const { return m_lastSorted; }

	const char *getKeys() const { return m_keys; }

	collnum_t getCollnum() const { return m_collnum; }
	void setCollnum(collnum_t c) { m_collnum = c; }

	bool addKey(const char *key, const char *data, int32_t dataSize);

	int32_t getNode(const char *key); //returns -1 if not found
	int32_t getNumNegativeKeys() const;

	bool getList(RdbList *list, const char *startKey, const char *endKey, int32_t minRecSizes,
	             int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys);

	bool deleteNode(int32_t i);

	bool deleteList(RdbList *list);

	//Save State
	int64_t fastSave_r(int fd, int64_t offset);
	int64_t fastLoad(BigFile *f, int64_t offset);

	//Debug
	bool selfTest (int32_t bucketnum, const char* prevKey);
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

RdbBucket::RdbBucket() {
	m_endKey = NULL;
	m_keys = NULL;
	m_parent = NULL;
	m_numKeys = 0;
	m_lastSorted = 0;
	m_collnum = 0;
}

bool RdbBucket::set(RdbBuckets* parent, char* newbuf) {
	m_endKey = NULL;
	m_parent = parent;
	m_lastSorted = 0;
	m_numKeys = 0;
	m_keys = newbuf;
	return true;
}

void RdbBucket::reBuf(char* newbuf) {
	if(!m_keys) {
		m_keys = newbuf;
		return;
	}
	gbmemcpy(newbuf, m_keys, m_numKeys * m_parent->m_recSize);
	if(m_endKey) {
		m_endKey = newbuf + (m_endKey - m_keys);
	}
	m_keys = newbuf;
}

RdbBucket::~RdbBucket() {
	reset();
}

void RdbBucket::reset() {
	m_numKeys = 0;
	m_lastSorted = 0;
	m_endKey = NULL;
}

int32_t RdbBuckets::getMemAllocated() const {
	ScopedLock sl(m_mtx);
	return (sizeof(RdbBuckets) + m_masterSize + m_dataMemOccupied);
}

//includes data in the data ptrs
int32_t RdbBuckets::getMemOccupied() const {
	ScopedLock sl(m_mtx);
	return (m_numKeysApprox * m_recSize) + m_dataMemOccupied + sizeof(RdbBuckets) + m_sortBufSize + BUCKET_SIZE * m_recSize;
}

bool RdbBuckets::needsDump() const {
	if (m_numBuckets + 1 < m_maxBuckets) {
		return false;
	}

	return (m_maxBuckets == m_maxBucketsCapacity);
}


static int cmp_int(const void *pv1, const void *pv2) {
	//the integer are all in the range [0..BUCKET_SIZE] so we don't have to worry about overflow/underflow
	return *((const int*)pv1) - *((const int*)pv2);
}

//be very conservative with this because if we say we can fit it
//and we can't then we'll get a partial list added and we will
//add the whole list again.
bool RdbBuckets::hasRoom(int32_t numRecs) const {
	ScopedLock sl(m_mtx);

	//Whether we have room or not depends on how many bucket splits will occur. We don't
	//know that until we see the keys, so we must be a conservative. If we answer yes but
	//the truth is false then we'll end up with duplicates because the caller will add a
	//partial list, trigger a dump, and then add the full list.

	int spareBuckets = m_maxBucketsCapacity-m_numBuckets;
	
	//If each insert would cause a split and we have enough spare buckets for that
	//then we definitely has room for it.
	if(numRecs <= spareBuckets)
		return true;

	//If an adversary inserts keys he'll use keys that would split the most number
	//of buckets, so that would mean the most full buckets.
	
	//This computation/estimation is a bit expensive, but in default configuration
	//we normally don't have more than 2373 buckets, and the upper layer (msg4) has
	//been optimized to not call this for every record.
	
	//Fetch the fill level of all buckets and sort the list
	int32_t *sortbuf = new int32_t[m_numBuckets];
	for(int i=0; i<m_numBuckets; i++)
		sortbuf[i] = m_buckets[i]->getNumKeys();
	qsort(sortbuf,m_numBuckets,sizeof(*sortbuf),cmp_int);
	
	int splits=0;
	int records=numRecs;
	for(int i=m_numBuckets-1; i>=0 && records>0; i--) {
		int room_in_this_bucket = BUCKET_SIZE - sortbuf[i];
		records -= room_in_this_bucket;
		splits++;
	}
	delete[] sortbuf;
	
	if(splits < spareBuckets)
		return true;
	else
		return false;
}

#define CMPFN(ks) \
	[](const void *a, const void *b) { \
		return static_cast<int>(KEYCMPNEGEQ(static_cast<const char*>(a), static_cast<const char*>(b), ks)); \
	};

static int (*getCmpFn(uint8_t ks))(const void*, const void *) {
	int (*cmpfn) (const void*, const void *) = NULL;

	if (ks == 18) {
		cmpfn = CMPFN(18);
	} else if (ks == 12) {
		cmpfn = CMPFN(12);
	} else if (ks == 16) {
		cmpfn = CMPFN(16);
	} else if (ks == 6) {
		cmpfn = CMPFN(6);
	} else if (ks == 24) {
		cmpfn = CMPFN(24);
	} else if (ks == 28) {
		cmpfn = CMPFN(28);
	} else if (ks == 8) {
		cmpfn = CMPFN(8);
	} else {
		gbshutdownAbort(true);
	}

	return cmpfn;
}

bool RdbBucket::sort() {
	if (m_lastSorted == m_numKeys) {
		return true;
	}

	if (m_numKeys < 2) {
		m_lastSorted = m_numKeys;
		return true;		
	}

	uint8_t ks = m_parent->m_ks;
	int32_t recSize = m_parent->m_recSize;
	int32_t fixedDataSize = m_parent->m_fixedDataSize;

	int32_t numUnsorted = m_numKeys - m_lastSorted;
	char *list1 = m_keys;
	char *list2 = m_keys + (recSize * m_lastSorted);
	char *list1end = list2;
	char *list2end = list2 + (recSize * numUnsorted);

	//sort the unsorted portion
	// . use merge sort because it is stable, and we need to always keep
	// . the identical keys that were added last
	// . now we pass in a buffer to merge into, otherwise one is mallocated,
	// . which can fail.  It falls back on qsort which is not stable.
	if (!m_parent->m_sortBuf) {
		gbshutdownAbort(true);
	}

	gbmergesort(list2, numUnsorted, recSize, getCmpFn(ks), m_parent->m_sortBuf, m_parent->m_sortBufSize);

	char* mergeBuf  = m_parent->m_swapBuf;
	if (!mergeBuf) {
		gbshutdownAbort(true);
	}

	char *p = mergeBuf;
	char v;
	char *lastKey = NULL;
	int32_t bytesRemoved = 0;
	int32_t dso = ks + sizeof(char*);//datasize offset
	int32_t numNeg = 0;

	for (;;) {
		if (list1 >= list1end) {
			// . just copy into place, deduping as we go
			while (list2 < list2end) {
				if (lastKey && KEYCMPNEGEQ(list2, lastKey, ks) == 0) {
					//this is a dup, we are removing data
					if (fixedDataSize != 0) {
						if (fixedDataSize == -1) {
							bytesRemoved += *(int32_t *)(lastKey + dso);
						} else {
							bytesRemoved += fixedDataSize;
						}
					}
					if (KEYNEG(lastKey)) {
						numNeg++;
					}
					p = lastKey;
				}
				gbmemcpy(p, list2, recSize);
				lastKey = p;
				p += recSize;
				list2 += recSize;
			}

			break;
		}

		if (list2 >= list2end) {
			// . if all that is left is list 1 just copy it into 
			// . place, since it is already deduped
			gbmemcpy(p, list1, list1end - list1);
			p += list1end - list1;
			break;
		}

		v = KEYCMPNEGEQ(list1, list2, ks);
		if (v < 0) {
			//never overwrite the merged list from list1 because
			//it is always older and it is already deduped
			if (lastKey && KEYCMPNEGEQ(list1, lastKey, ks) == 0) {
				if (KEYNEG(lastKey)) {
					numNeg++;
				}
				list1 += recSize;
				continue;
			}
			gbmemcpy(p, list1, recSize);
			lastKey = p;
			p += recSize;
			list1 += recSize;
		} else if (v > 0) {
			//copy it over the one we just copied in
			if (lastKey && KEYCMPNEGEQ(list2, lastKey, ks) == 0) {
				//this is a dup, we are removing data
				if (fixedDataSize != 0) {
					if (fixedDataSize == -1) {
						bytesRemoved += *(int32_t *)(lastKey + dso);
					} else {
						bytesRemoved += fixedDataSize;
					}
				}
				if (KEYNEG(lastKey)) {
					numNeg++;
				}
				p = lastKey;
			}

			gbmemcpy(p, list2, recSize);
			lastKey = p;
			p += recSize;
			list2 += recSize;
		} else {
			if (lastKey && KEYCMPNEGEQ(list2, lastKey, ks) == 0) {
				if (fixedDataSize != 0) {
					if (fixedDataSize == -1) {
						bytesRemoved += *(int32_t *)(lastKey + dso);
					} else {
						bytesRemoved += fixedDataSize;
					}
				}
				if (KEYNEG(lastKey)) {
					numNeg++;
				}
				p = lastKey;
			}

			//found dup, take list2's
			gbmemcpy(p, list2, recSize);
			lastKey = p;
			p += recSize;
			list2 += recSize;
			list1 += recSize; //fuggedaboutit!
		}
	}

	//we compacted out the dups, so reflect that here
	int32_t newNumKeys = (p - mergeBuf) / recSize;
	m_parent->updateNumRecs_unlocked(newNumKeys - m_numKeys, -bytesRemoved, -numNeg);
	m_numKeys = newNumKeys;

	if (m_keys != mergeBuf) {
		m_parent->m_swapBuf = m_keys;
	}

	m_keys = mergeBuf;
	m_lastSorted = m_numKeys;
	m_endKey = m_keys + ((m_numKeys - 1) * recSize);

	return true;
}

//make 2 half full buckets,
//addKey assumes that the *this bucket retains the lower half of the keys
//returns a new bucket with the remaining upper half.
RdbBucket *RdbBucket::split(RdbBucket *newBucket) {
	int32_t b1NumKeys = m_numKeys >> 1;
	int32_t b2NumKeys = m_numKeys - b1NumKeys;
	int32_t recSize = m_parent->m_recSize;

	//configure the new bucket
	gbmemcpy(newBucket->m_keys, m_keys + (b1NumKeys * recSize), b2NumKeys * recSize);
	newBucket->m_numKeys = b2NumKeys;
	newBucket->m_lastSorted = b2NumKeys;
	newBucket->m_endKey = newBucket->m_keys + ((b2NumKeys - 1) * recSize);

	//reconfigure the old bucket
	m_numKeys = b1NumKeys;
	m_lastSorted = b1NumKeys;
	m_endKey = m_keys + ((b1NumKeys - 1) * recSize);

	//add it to our parent
	return newBucket;
}


bool RdbBucket::addKey(const char *key, const char *data, int32_t dataSize) {
	uint8_t ks = m_parent->m_ks;
	int32_t recSize = m_parent->m_recSize;
	bool isNeg = KEYNEG(key);

	logTrace(g_conf.m_logTraceRdbBuckets, "BEGIN. key=%s dataSize=%" PRId32, KEYSTR(key, ks), dataSize);

	char *newLoc = m_keys + (recSize * m_numKeys);
	gbmemcpy(newLoc, key, ks);

	if (data) {
		*(const char **)(newLoc + ks) = data;
		if (m_parent->m_fixedDataSize == -1) {
			*(int32_t *)(newLoc + ks + sizeof(char *)) = (int32_t)dataSize;
		}
	}

	if (m_endKey == NULL) { //are we the first key?
		if (m_numKeys > 0) {
			gbshutdownAbort(true);
		}

		m_endKey = newLoc;
		m_lastSorted = 1;
	} else {
		// . minor optimization: if we are almost sorted, then
		// . see if we can't maintain that state.
		char v = KEYCMPNEGEQ(key, m_endKey, ks);
		if (v == 0) {
			// . just replace the old key if we were the same,
			// . don't inc num keys
			gbmemcpy(m_endKey, newLoc, recSize);
			if (KEYNEG(m_endKey)) {
				if (isNeg) {
					return true;
				} else {
					m_parent->updateNumRecs_unlocked(0, 0, -1);
				}
			} else if (isNeg) {
				m_parent->updateNumRecs_unlocked(0, 0, 1);
			}

			return true;
		} else if (v > 0) {
			// . if we were greater than the old key, 
			// . we can assume we are still sorted, which
			// . really helps us for adds which are in order
			if (m_lastSorted == m_numKeys) {
				m_lastSorted++;
			}

			m_endKey = newLoc;
		}
	}
	m_numKeys++;
	m_parent->updateNumRecs_unlocked(1, dataSize, isNeg ? 1 : 0);

	logTrace(g_conf.m_logTraceRdbBuckets, "END. Returning true");
	return true;
}

int32_t RdbBucket::getNode(const char *key) {
	sort();

	uint8_t ks = m_parent->m_ks;
	int32_t recSize = m_parent->m_recSize;
	int32_t i = 0;
	char v;
	char *kk;
	int32_t low = 0;
	int32_t high = m_numKeys - 1;

	while (low <= high) {
		int32_t delta = high - low;
		i = low + (delta >> 1);
		kk = m_keys + (recSize * i);
		v = KEYCMP(key, kk, ks);
		if (v < 0) {
			high = i - 1;
			continue;
		} else if (v > 0) {
			low = i + 1;
			continue;
		} else {
			return i;
		}
	}
	return -1;
}


bool RdbBucket::selfTest (int32_t bucketnum, const char* prevKey) {
	sort();

	char* last = NULL;
	char* kk = m_keys;
	int32_t recSize = m_parent->m_recSize;
	int32_t ks = m_parent->m_ks;

	//ensure our first key is > the last guy's end key
	if (prevKey != NULL && m_numKeys > 0) {
		if (KEYCMP(prevKey, m_keys, ks) > 0) {
			log(LOG_ERROR, "db: bucket's first key is less than last bucket's end key!!!!!");
			log(LOG_ERROR, "db:  bucket......: %" PRId32 "", bucketnum);
			log(LOG_ERROR, "db:  last key....: %s", KEYSTR(prevKey, ks));
			log(LOG_ERROR, "db:  first key...: %s", KEYSTR(m_keys, ks));
			log(LOG_ERROR, "db:  m_numKeys...: %" PRId32 "", m_numKeys);
			log(LOG_ERROR, "db:  m_lastSorted: %" PRId32 "", m_lastSorted);
			log(LOG_ERROR, "db:  m_keys......: %p", m_keys);

			printBucket(-1);
			return false;
			//gbshutdownAbort(true);
		}
	}

	for (int32_t i = 0; i < m_numKeys; i++) {
		if (ks == 18) {
			if (KEYNEG(kk) && Posdb::getTermId(kk) != 0) {
				log(LOG_ERROR, "db: key is negative!!!");
				log(LOG_ERROR, "db:  curr key....: %s", KEYSTR(kk, ks));
				printBucket(-1);
				return false;
			}
		}
		if (i > 0 && KEYCMP(last, kk, ks) > 0) {

			log(LOG_ERROR, "db: bucket's last key was out of order!!!!! num keys: %" PRId32" ks=%" PRId32" key#=%" PRId32,
			    m_numKeys, ks, i);
			log(LOG_ERROR, "db:  bucket......: %" PRId32 "", bucketnum);
			log(LOG_ERROR, "db:  last key....: %s", KEYSTR(last, ks));
			log(LOG_ERROR, "db:  curr key....: %s", KEYSTR(kk, ks));
			log(LOG_ERROR, "db:  m_numKeys...: %" PRId32 "", m_numKeys);
			log(LOG_ERROR, "db:  m_lastSorted: %" PRId32 "", m_lastSorted);
			log(LOG_ERROR, "db:  m_keys......: %p", m_keys);

			printBucket(-1);
			return false;
			//gbshutdownAbort(true);
		}
		last = kk;
		kk += recSize;
	}
	return true;
}


void RdbBuckets::printBuckets(std::function<void(const char *, int32_t)> print_fn) {
	ScopedLock sl(m_mtx);
 	for(int32_t i = 0; i < m_numBuckets; i++) {
		m_buckets[i]->printBucket(i, print_fn);
	}
}

void RdbBuckets::printBucketsStartEnd() {
	ScopedLock sl(m_mtx);
 	for(int32_t i = 0; i < m_numBuckets; i++) {
		m_buckets[i]->printBucketStartEnd(i);
	}
}



void RdbBucket::printBucket(int32_t idx, std::function<void(const char*, int32_t)> print_fn) {
	const char *kk = m_keys;
	int32_t keySize = m_parent->m_ks;

	logTrace(g_conf.m_logTraceRdbBuckets,"Bucket dump. bucket=%" PRId32 ", m_numKeys=%" PRId32 ", m_lastSorted=%" PRId32 "", idx, m_numKeys, m_lastSorted);

	for (int32_t i = 0; i < m_numKeys; i++) {
		if (print_fn) {
			print_fn(kk, keySize);
		} else {
			logf(LOG_TRACE, "db: i=%04" PRId32 " k=%s keySize=%" PRId32 "", i, KEYSTR(kk, keySize), keySize);
		}
		kk += m_parent->m_recSize;
	}
}


void RdbBucket::printBucketStartEnd(int32_t idx) {
	char e[MAX_KEY_BYTES*2+3];	// for hexdump
	char f[MAX_KEY_BYTES*2+3];	// for hexdump

	int32_t keySize = m_parent->m_ks;

	if( getNumKeys() ) {
		KEYSTR(getFirstKey(), keySize, f);
		KEYSTR(getEndKey(), keySize, e);
	}
	else {
		memset(e, 0, sizeof(e));
		memset(f, 0, sizeof(f));
	}

	log(LOG_INFO,"%s:%s:%d: bucket=%" PRId32 ", keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s, m_keys=%p", __FILE__, __func__, __LINE__, idx, getNumKeys(), getNumSortedKeys(), f, e, m_keys);
}


RdbBuckets::RdbBuckets()
  : m_mtx()
{
	m_numBuckets = 0; 
	m_masterPtr = NULL;
	m_buckets = NULL;
	m_swapBuf = NULL;
	m_sortBuf = NULL;
	m_isWritable = true;
	m_isSaving = false;
	m_dataMemOccupied = 0;
	m_needsSave = false;
	m_repairMode = false;

	// Coverity
	m_bucketsSpace = NULL;
	m_masterSize = 0;
	m_firstOpenSlot = 0;
	m_maxBuckets = 0;
	m_ks = 0;
	m_fixedDataSize = 0;
	m_recSize = 0;
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_maxMem = 0;
	m_maxBucketsCapacity = 0;
	m_rdbId = RDB_NONE;
	m_dbname = NULL;
	m_sortBufSize = 0;
	m_dir = NULL;
	m_state = NULL;
	m_callback = NULL;
	m_errno = 0;
	m_allocName = NULL;
}


bool RdbBuckets::set(int32_t fixedDataSize, int32_t maxMem, const char *allocName, rdbid_t rdbId,
                     const char *dbname, char keySize) {
	ScopedLock sl(m_mtx);

	m_numBuckets = 0;
	m_ks = keySize;
	m_rdbId = rdbId;
	m_allocName = allocName;
	m_fixedDataSize = fixedDataSize;
	m_recSize = m_ks;

	if (m_fixedDataSize != 0) {
		m_recSize += sizeof(char *);
		if (m_fixedDataSize == -1) {
			m_recSize += sizeof(int32_t);
		}
	}

	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_dbname = dbname;
	m_swapBuf = NULL;
 	m_sortBuf = NULL;

	//taken from sort.cpp, this is to prevent mergesort from mallocing
	m_sortBufSize = BUCKET_SIZE * m_recSize + sizeof(char*);

	if (m_buckets) {
		gbshutdownAbort(true);
	}
	m_maxBuckets = 0;
	m_masterSize = 0;
	m_masterPtr =  NULL;
	m_maxMem = maxMem;

	int32_t perBucket = sizeof(RdbBucket *) + sizeof(RdbBucket) + BUCKET_SIZE * m_recSize;
	int32_t overhead = m_sortBufSize + BUCKET_SIZE * m_recSize + sizeof(RdbBuckets); //thats us, silly
	int32_t avail = m_maxMem - overhead;

	m_maxBucketsCapacity = avail / perBucket;

	if (m_maxBucketsCapacity <= 0) {
		log( LOG_ERROR, "db: max memory for %s's buckets is way too small to accomodate even 1 bucket, "
		     "reduce bucket size(%" PRId32") or increase max mem(%" PRId32")", m_dbname, (int32_t)BUCKET_SIZE, m_maxMem);
		gbshutdownAbort(true);
	}

	if (!resizeTable_unlocked(INIT_SIZE)) {
		g_errno = ENOMEM;
		return false;
	}

	return true;
}

RdbBuckets::~RdbBuckets( ) {
	reset_unlocked();
}

void RdbBuckets::setNeedsSave(bool s) {
	m_needsSave = s;
}

void RdbBuckets::reset() {
	ScopedLock sl(m_mtx);
	reset_unlocked();
}

void RdbBuckets::reset_unlocked() {
	for (int32_t j = 0; j < m_numBuckets; j++) {
		m_buckets[j]->reset();
	}

	if (m_masterPtr) {
		mfree(m_masterPtr, m_masterSize, m_allocName );
	}

	// do not require saving after a reset
	m_needsSave = false;

	m_masterPtr = NULL;
	m_buckets = NULL;
	m_bucketsSpace = NULL;
	m_numBuckets = 0;
	m_maxBuckets = 0;
	m_dataMemOccupied = 0;
	m_firstOpenSlot = 0;
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_sortBuf = NULL;
	m_swapBuf = NULL;
}

void RdbBuckets::clear() {
	ScopedLock sl(m_mtx);

	for (int32_t j = 0; j < m_numBuckets; j++) {
		m_buckets[j]->reset();
	}

	m_numBuckets = 0;
	m_firstOpenSlot = 0;
	m_dataMemOccupied = 0;
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_needsSave = true;
}

RdbBucket* RdbBuckets::bucketFactory_unlocked() {
	m_mtx.verify_is_locked();

	if (m_numBuckets == m_maxBuckets - 1) {
		if (!resizeTable_unlocked(m_maxBuckets * 2)) {
			return NULL;
		}
	}

	RdbBucket *b;
	if (m_firstOpenSlot > m_numBuckets) {
		int32_t i = 0;
		for (; i < m_numBuckets; i++) {
			if (m_bucketsSpace[i].getNumKeys() == 0) {
				break;
			}
		}
		b = &m_bucketsSpace[i];
	} else {
		b = &m_bucketsSpace[m_firstOpenSlot];
		m_firstOpenSlot++;
	}

	return b;
}

bool RdbBuckets::resizeTable_unlocked(int32_t numNeeded) {
	m_mtx.verify_is_locked();

	logTrace(g_conf.m_logTraceRdbBuckets, "BEGIN. numNeeded=%" PRId32, numNeeded);

	if (numNeeded == m_maxBuckets) {
		return true;
	}

	if (numNeeded < INIT_SIZE) {
		numNeeded = INIT_SIZE;
	}

	if (numNeeded > m_maxBucketsCapacity) {
		if (m_maxBucketsCapacity <= m_maxBuckets) {
			log(LOG_INFO, "db: could not resize buckets currently have %" PRId32" "
			    "buckets, asked for %" PRId32", max number of buckets"
			    " for %" PRId32" bytes with keysize %" PRId32" is %" PRId32,
			    m_maxBuckets, numNeeded, m_maxMem, (int32_t)m_ks,
			    m_maxBucketsCapacity);
			g_errno = ENOMEM;
			return false;
		}
		numNeeded = m_maxBucketsCapacity;
	}

	int32_t perBucket = sizeof(RdbBucket*) + sizeof(RdbBucket) + BUCKET_SIZE * m_recSize;

	int32_t tmpMaxBuckets = numNeeded;
	int32_t newMasterSize = tmpMaxBuckets * perBucket + (BUCKET_SIZE * m_recSize) + m_sortBufSize;

	if(newMasterSize > m_maxMem) {
		log(LOG_WARN, "db: Buckets oops, trying to malloc more(%" PRId32") that max mem(%" PRId32"), should've caught this earlier.",
		    newMasterSize, m_maxMem);
		gbshutdownAbort(true);
	}

	char *tmpMasterPtr = (char*)mmalloc(newMasterSize, m_allocName);
	if (!tmpMasterPtr) {
		g_errno = ENOMEM;
		return false;
	}

	char *p = tmpMasterPtr;
	char *bucketMemPtr = p;
	p += (BUCKET_SIZE * m_recSize) * tmpMaxBuckets;
	m_swapBuf = p;
	p += (BUCKET_SIZE * m_recSize);
	m_sortBuf = p;
	p += m_sortBufSize;

	RdbBucket** tmpBucketPtrs = (RdbBucket**)p;
	p += tmpMaxBuckets * sizeof(RdbBucket*);
	RdbBucket* tmpBucketSpace = (RdbBucket*)p;
	p += tmpMaxBuckets * sizeof(RdbBucket);

	if (p - tmpMasterPtr != newMasterSize) {
		gbshutdownAbort(true);
	}

	for (int32_t i = 0; i < m_numBuckets; i++) {
		//copy them over one at a time so they
		//will now be contiguous and consistent
		//with the ptrs array.
		tmpBucketPtrs[i] = &tmpBucketSpace[i];
		gbmemcpy(&tmpBucketSpace[i], m_buckets[i], sizeof(RdbBucket));
		tmpBucketSpace[i].reBuf(bucketMemPtr);
		bucketMemPtr += (BUCKET_SIZE * m_recSize);
	}

	//now do the rest
	for (int32_t i = m_numBuckets; i < tmpMaxBuckets; i++) {
		tmpBucketSpace[i].set(this, bucketMemPtr);
		bucketMemPtr += (BUCKET_SIZE * m_recSize);
	}

	if (bucketMemPtr != m_swapBuf) {
		gbshutdownAbort(true);
	}

	if (m_masterPtr) {
		mfree(m_masterPtr, m_masterSize, m_allocName);
	}

	m_masterPtr = tmpMasterPtr;
	m_masterSize = newMasterSize;
	m_buckets = tmpBucketPtrs;
	m_bucketsSpace = tmpBucketSpace;
	m_maxBuckets = tmpMaxBuckets;
	m_firstOpenSlot = m_numBuckets;
	return true;
}

bool RdbBuckets::addNode(collnum_t collnum, const char *key, const char *data, int32_t dataSize) {
	ScopedLock sl(m_mtx);
	return addNode_unlocked(collnum, key, data, dataSize);
}

bool RdbBuckets::addNode_unlocked(collnum_t collnum, const char *key, const char *data, int32_t dataSize) {
	m_mtx.verify_is_locked();

	if (!m_isWritable || m_isSaving) {
		g_errno = ETRYAGAIN;
		return false;
	}

	m_needsSave = true;

	int32_t i = getBucketNum_unlocked(collnum, key);

	int32_t orgi = i;

	logTrace(g_conf.m_logTraceRdbBuckets,"Key %s -> bucket %" PRId32 "", KEYSTR(key, m_recSize), i);

	if (i == m_numBuckets || m_buckets[i]->getCollnum() != collnum) {
		int32_t bucketsCutoff = (BUCKET_SIZE >> 1);
		// when repairing the keys are added in order,
		// so fill them up all of the way before moving
		// on to the next one.
		if (m_repairMode) {
			bucketsCutoff = BUCKET_SIZE;
		}

		if (i != 0 &&
		    m_buckets[i - 1]->getCollnum() == collnum &&
		    m_buckets[i - 1]->getNumKeys() < bucketsCutoff) {
			i--;
		} 
		else 
		if (i == m_numBuckets) {
			m_buckets[i] = bucketFactory_unlocked();
			if (m_buckets[i] == NULL) {
				g_errno = ENOMEM;
				return -1;
			}
			m_buckets[i]->setCollnum(collnum);
			m_numBuckets++;
		} else {
			RdbBucket *newBucket = bucketFactory_unlocked();
			if ( !newBucket ) { 
				g_errno = ENOMEM;
				return false;
			}
			newBucket->setCollnum(collnum);
			addBucket_unlocked(newBucket, i);
		}
	}

	// check if we are full
	if (m_buckets[i]->getNumKeys() == BUCKET_SIZE) {
		logTrace(g_conf.m_logTraceRdbBuckets, "Bucket %" PRId32 " full (%" PRId32 "), splitting", i, BUCKET_SIZE);

		//split the bucket
		int64_t t = gettimeofdayInMilliseconds();
		m_buckets[i]->sort();
		RdbBucket *newBucket = bucketFactory_unlocked();
		if (newBucket == NULL) {
			log(LOG_WARN,"%s:%s:%d: Out of memory", __FILE__, __func__, __LINE__);
			g_errno = ENOMEM;
			return false;
		}
		newBucket->setCollnum(collnum);
		
		m_buckets[i]->split(newBucket);
		addBucket_unlocked(newBucket, i + 1);

		if (bucketCmp_unlocked(collnum, key, m_buckets[i]) > 0) {
			i++;
		}

		int64_t took = gettimeofdayInMilliseconds() - t;
		if (took > 10) {
			log(LOG_WARN, "db: split bucket in %" PRId64" ms for %s", took, m_dbname);
		}
	}

	if(g_conf.m_logTraceRdbBuckets) {
		char e[MAX_KEY_BYTES*2+3];	// for hexdump
		char f[MAX_KEY_BYTES*2+3];	// for hexdump

		if( m_buckets[i]->getNumKeys() ) {
			KEYSTR(m_buckets[i]->getFirstKey(), m_recSize, f);
			KEYSTR(m_buckets[i]->getEndKey(), m_recSize, e);
		}
		else {
			memset(e, 0, sizeof(e));
			memset(f, 0, sizeof(f));
		}

		logTrace(g_conf.m_logTraceRdbBuckets, "Key %s -> bucket %" PRId32 " (org %" PRId32 ")", KEYSTR(key, m_recSize), i, orgi);
		logTrace(g_conf.m_logTraceRdbBuckets, "  bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i, m_buckets[i]->getNumKeys(), m_buckets[i]->getNumSortedKeys(), f, e);

		if( i ) {
			if( m_buckets[i-1]->getNumKeys() ) {
				KEYSTR(m_buckets[i-1]->getFirstKey(), m_recSize, f);
				KEYSTR(m_buckets[i-1]->getEndKey(), m_recSize, e);
			}
			else {
				memset(e, 0, sizeof(e));
				memset(f, 0, sizeof(f));
			}
			logTrace(g_conf.m_logTraceRdbBuckets, "  prev bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i-1, m_buckets[i-1]->getNumKeys(), m_buckets[i-1]->getNumSortedKeys(), f, e);
		}

		if( i+1 < m_numBuckets ) {
			if( m_buckets[i+1]->getNumKeys() ) {
				KEYSTR(m_buckets[i+1]->getFirstKey(), m_recSize, f);
				KEYSTR(m_buckets[i+1]->getEndKey(), m_recSize, e);
			}
			else {
				memset(e, 0, sizeof(e));
				memset(f, 0, sizeof(f));
			}

			logTrace(g_conf.m_logTraceRdbBuckets, "  next bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i+1, m_buckets[i+1]->getNumKeys(), m_buckets[i+1]->getNumSortedKeys(), f, e);
		}
	}

	m_buckets[i]->addKey(key, data, dataSize);

	return true;
}

void RdbBuckets::addBucket_unlocked(RdbBucket *newBucket, int32_t i) {
	m_mtx.verify_is_locked();

	m_numBuckets++;
	int32_t moveSize = (m_numBuckets - i)*sizeof(RdbBuckets*);
	if (moveSize > 0) {
		memmove(&m_buckets[i + 1], &m_buckets[i], moveSize);
	}
	m_buckets[i] = newBucket;
}

bool RdbBuckets::getList(collnum_t collnum, const char *startKey, const char *endKey, int32_t minRecSizes,
                         RdbList *list, int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys) const {
	ScopedLock sl(m_mtx);
	return getList_unlocked(collnum, startKey, endKey, minRecSizes, list, numPosRecs, numNegRecs, useHalfKeys);
}

bool RdbBuckets::getList_unlocked(collnum_t collnum, const char *startKey, const char *endKey, int32_t minRecSizes,
                                  RdbList *list, int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys) const {
	m_mtx.verify_is_locked();

	if (numNegRecs) {
		*numNegRecs = 0;
	}
	if (numPosRecs) {
		*numPosRecs = 0;
	}

	// . set the start and end keys of this list
	// . set lists's m_ownData member to true
	list->reset();

	// got set m_ks first so the set ( startKey, endKey ) works!
	list->setKeySize(m_ks);
	list->set(startKey, endKey);
	list->setFixedDataSize(m_fixedDataSize);
	list->setUseHalfKeys(useHalfKeys);

	// bitch if list does not own his own data
	if (!list->getOwnData()) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: rdbbuckets: getList: List does not own data");
		return false;
	}

	// bail if minRecSizes is 0
	if ( minRecSizes == 0 ) {
		return true;
	}
	if (minRecSizes < 0) {
		minRecSizes = 0x7fffffff; //LONG_MAX;
	}

	int32_t startBucket = getBucketNum_unlocked(collnum, startKey);
	if (startBucket > 0 && bucketCmp_unlocked(collnum, startKey, m_buckets[startBucket - 1]) < 0) {
		startBucket--;
	}

	// if the startKey is past our last bucket, then nothing
	// to return
	if (startBucket == m_numBuckets || m_buckets[startBucket]->getCollnum() != collnum) {
		return true;
	}


	int32_t endBucket;
	if (bucketCmp_unlocked(collnum, endKey, m_buckets[startBucket]) <= 0) {
		endBucket = startBucket;
	} else {
		endBucket = getBucketNum_unlocked(collnum, endKey);
	}

	if (endBucket == m_numBuckets || m_buckets[endBucket]->getCollnum() != collnum) {
		endBucket--;
	}
	
	if (m_buckets[endBucket]->getCollnum() != collnum) {
		gbshutdownAbort(true);
	}

	int32_t growth = 0;

	if(startBucket == endBucket) {
		growth = m_buckets[startBucket]->getNumKeys() * m_recSize;
		if (growth > minRecSizes) {
			growth = minRecSizes + m_recSize;
		}

		if (!list->growList(growth)) {
			log(LOG_WARN, "db: Failed to grow list to %" PRId32" bytes for storing records from buckets: %s.",
			    growth, mstrerror(g_errno));
			return false;
		}

		return m_buckets[startBucket]->getList(list, startKey, endKey, minRecSizes, numPosRecs, numNegRecs, useHalfKeys);
	}

	// reserve some space, it is an upper bound
	for (int32_t i = startBucket; i <= endBucket; i++) {
		growth += m_buckets[i]->getNumKeys() * m_recSize;
	}

	if (growth > minRecSizes) {
		growth = minRecSizes + m_recSize;
	}

	if (!list->growList(growth)) {
		log(LOG_WARN, "db: Failed to grow list to %" PRId32" bytes for storing records from buckets: %s.",
		    growth, mstrerror(g_errno));
		return false;
	}

	// separate into 3 different calls so we don't have 
	// to search for the start and end keys within the buckets
	// unnecessarily.
	if (!m_buckets[startBucket]->getList(list, startKey, NULL, minRecSizes, numPosRecs, numNegRecs, useHalfKeys)) {
		return false;
	}

	int32_t i = startBucket + 1;
	for (; i < endBucket && list->getListSize() < minRecSizes; i++) {
		if (!m_buckets[i]->getList(list, NULL, NULL, minRecSizes, numPosRecs, numNegRecs, useHalfKeys)) {
			return false;
		}
	}

	if (list->getListSize() < minRecSizes) {
		if (!m_buckets[i]->getList(list, NULL, endKey, minRecSizes, numPosRecs, numNegRecs, useHalfKeys)) {
			return false;
		}
	}

	return true;

}

bool RdbBuckets::testAndRepair() {
	ScopedLock sl(m_mtx);

	if (!selfTest_unlocked(true, false)) {
		if (!repair_unlocked()) {
			return false;
		}

		m_needsSave = true;
	}

	return true;
}

bool RdbBuckets::repair_unlocked() {
	m_mtx.verify_is_locked();

	if (m_numBuckets == 0 && (m_numKeysApprox != 0 || m_numNegKeys != 0)) {
		m_numKeysApprox = 0;
		m_numNegKeys = 0;
		log("db: RdbBuckets repaired approx key count to reflect true number of keys.");
	}

	int32_t tmpMasterSize = m_masterSize;
	char *tmpMasterPtr = m_masterPtr;
	RdbBucket **tmpBucketPtrs = m_buckets;
	int32_t tmpNumBuckets = m_numBuckets;

	m_masterPtr = NULL;
	m_masterSize = 0;
	m_numBuckets = 0;

	reset_unlocked();

	if (!resizeTable_unlocked(INIT_SIZE)) {
		log(LOG_WARN, "db: RdbBuckets could not alloc enough memory to repair corruption.");
		g_errno = ENOMEM;
		return false;
	}

	m_repairMode = true;

	for (int32_t j = 0; j < tmpNumBuckets; j++) {
		collnum_t collnum = tmpBucketPtrs[j]->getCollnum();
		for (int32_t i = 0; i < tmpBucketPtrs[j]->getNumKeys(); i++) {
			const char *currRec = tmpBucketPtrs[j]->getKeys() + m_recSize * i;
			const char *data = NULL;
			int32_t dataSize = m_fixedDataSize;

			if (m_fixedDataSize != 0) {
				data = currRec + m_ks;
				if (m_fixedDataSize == -1)
					dataSize = *(int32_t *)(data + sizeof(char *));
			}

			if (!addNode_unlocked(collnum, currRec, data, dataSize)) {
				log(LOG_WARN, "db: got unrepairable error in RdbBuckets, could not re-add data");
				return false;
			}
		}
	}

	m_repairMode = false;

	if (tmpMasterPtr) {
		mfree(tmpMasterPtr, tmpMasterSize, m_allocName);
	}

	log(LOG_INFO, "db: RdbBuckets repair for %" PRId32" keys complete", m_numKeysApprox);
	return true;
}


void RdbBuckets::verifyIntegrity() {
	ScopedLock sl(m_mtx);
	selfTest_unlocked(true, true);
}


bool RdbBuckets::selfTest_unlocked(bool thorough, bool core) {
	m_mtx.verify_is_locked();

	if (m_numBuckets == 0 && m_numKeysApprox != 0) {
		return false;
	}

	int32_t totalNumKeys = 0;
	const char* last = NULL;
	collnum_t lastcoll = -1;
	int32_t numColls = 0;

	char e[MAX_KEY_BYTES*2+3];	// for hexdump
	char f[MAX_KEY_BYTES*2+3];	// for hexdump


	for (int32_t i = 0; i < m_numBuckets; i++) {
		RdbBucket *b = m_buckets[i];

		if (lastcoll != b->getCollnum()) {
			last = NULL;
			numColls++;
		}

		if (thorough) {
			if (!b->selfTest(i, last)) {


				if( m_buckets[i]->getNumKeys() ) {
					KEYSTR(m_buckets[i]->getFirstKey(), m_recSize, f);
					KEYSTR(m_buckets[i]->getEndKey(), m_recSize, e);
				}
				else {
					memset(e, 0, sizeof(e));
					memset(f, 0, sizeof(f));
				}

				log(LOG_ERROR, "  bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i, m_buckets[i]->getNumKeys(), m_buckets[i]->getNumSortedKeys(), f, e);

				if( i ) {
					if( m_buckets[i-1]->getNumKeys() ) {
						KEYSTR(m_buckets[i-1]->getFirstKey(), m_recSize, f);
						KEYSTR(m_buckets[i-1]->getEndKey(), m_recSize, e);
					}
					else {
						memset(e, 0, sizeof(e));
						memset(f, 0, sizeof(f));
					}

					log(LOG_ERROR, "  prev bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i-1, m_buckets[i-1]->getNumKeys(), m_buckets[i-1]->getNumSortedKeys(), f, e);
					m_buckets[i-1]->printBucket(i-1);
				}

				if( i+1 < m_numBuckets ) {

					if( m_buckets[i+1]->getNumKeys() ) {
						KEYSTR(m_buckets[i+1]->getFirstKey(), m_recSize, f);
						KEYSTR(m_buckets[i+1]->getEndKey(), m_recSize, e);
					}
					else {
						memset(e, 0, sizeof(e));
						memset(f, 0, sizeof(f));
					}

					log(LOG_ERROR, "  next bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i+1, m_buckets[i+1]->getNumKeys(), m_buckets[i+1]->getNumSortedKeys(), f, e);
					m_buckets[i+1]->printBucket(i+1);
				}

				
				if (!core) {
					return false;
				}
				gbshutdownCorrupted();
			}
		}

		totalNumKeys += b->getNumKeys();
		const char *kk = b->getEndKey();
		if (i > 0 && lastcoll == b->getCollnum() && last && KEYCMPNEGEQ(last, kk, m_ks) >= 0) {
			log(LOG_WARN, "rdbbuckets last key: %016" PRIx64"%08" PRIx32" num keys: %" PRId32,
			    *(int64_t *)(kk + (sizeof(int32_t))),
			    *(int32_t *)kk, b->getNumKeys());
			log(LOG_WARN, "rdbbuckets last key was out of order!!!!!");


			if( m_buckets[i]->getNumKeys() ) {
				KEYSTR(m_buckets[i]->getFirstKey(), m_recSize, f);
				KEYSTR(m_buckets[i]->getEndKey(), m_recSize, e);
			}
			else {
				memset(e, 0, sizeof(e));
				memset(f, 0, sizeof(f));
			}
			log(LOG_ERROR, "  bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i, m_buckets[i]->getNumKeys(), m_buckets[i]->getNumSortedKeys(), f, e);

			if( i ) {
				if( m_buckets[i-1]->getNumKeys() ) {
					KEYSTR(m_buckets[i-1]->getFirstKey(), m_recSize, f);
					KEYSTR(m_buckets[i-1]->getEndKey(), m_recSize, e);
				}
				else {
					memset(e, 0, sizeof(e));
					memset(f, 0, sizeof(f));
				}

				log(LOG_ERROR, "  prev bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i-1, m_buckets[i-1]->getNumKeys(), m_buckets[i-1]->getNumSortedKeys(), f, e);
			}

			if( i+1 < m_numBuckets ) {
				if( m_buckets[i+1]->getNumKeys() ) {
					KEYSTR(m_buckets[i+1]->getFirstKey(), m_recSize, f);
					KEYSTR(m_buckets[i+1]->getEndKey(), m_recSize, e);
				}
				else {
					memset(e, 0, sizeof(e));
					memset(f, 0, sizeof(f));
				}

				log(LOG_ERROR, "  next bucket. bucket=%" PRId32 ". keys=%" PRId32 ", sorted=%" PRId32 ", first=%s, end=%s", i+1, m_buckets[i+1]->getNumKeys(), m_buckets[i+1]->getNumSortedKeys(), f, e);
			}

			if (!core) {
				return false;
			}
			gbshutdownCorrupted();
		}

		last = kk;
		lastcoll = b->getCollnum();
	}

	if (totalNumKeys != m_numKeysApprox) {
		log(LOG_WARN, "db have %" PRId32" keys,  should have %" PRId32". %" PRId32" buckets in %" PRId32" colls for db %s",
		    totalNumKeys, m_numKeysApprox, m_numBuckets, numColls, m_dbname);
	}

	if (thorough && totalNumKeys != m_numKeysApprox) {
		return false;
	}
	return true;
}

char RdbBuckets::bucketCmp_unlocked(collnum_t acoll, const char *akey, const RdbBucket *b) const {
	m_mtx.verify_is_locked();

	if (acoll == b->getCollnum()) {
		return KEYCMPNEGEQ(akey, b->getEndKey(), m_ks);
	}

	if (acoll < b->getCollnum()) {
		return -1;
	}

	return 1;
}

int32_t RdbBuckets::getBucketNum_unlocked(collnum_t collnum, const char *key) const {
	m_mtx.verify_is_locked();

	if (m_numBuckets < 10) {
		int32_t i = 0;
		for (; i < m_numBuckets; i++) {
			RdbBucket *b = m_buckets[i];
			char v = bucketCmp_unlocked(collnum, key, b);
			if (v > 0) {
				continue;
			}
			break;
		}
		return i;
	}
	int32_t i = 0;
	char v;
	RdbBucket* b = NULL;
	int32_t low = 0;
	int32_t high = m_numBuckets - 1;
	while (low <= high) {
		int32_t delta = high - low;
		i = low + (delta >> 1);
		b = m_buckets[i];
		char v = bucketCmp_unlocked(collnum, key, b);
		if (v < 0) {
			high = i - 1;
			continue;
		} else if (v > 0) {
			low = i + 1;
			continue;
		} else {
			return i;
		}
	}
	
	//now fine tune:
	v = bucketCmp_unlocked(collnum, key, b);
	if (v > 0) {
		i++;
	}

	return i;
}

bool RdbBuckets::collExists(collnum_t collnum) const {
	ScopedLock sl(m_mtx);

	for (int32_t i = 0; i < m_numBuckets; i++) {
		if (m_buckets[i]->getCollnum() == collnum) {
			return true;
		}

		if (m_buckets[i]->getCollnum() > collnum) {
			break;
		}
	}

	return false;
}

int32_t RdbBuckets::getNumKeys(collnum_t collnum) const {
	ScopedLock sl(m_mtx);

	int32_t numKeys = 0;
	for (int32_t i = 0; i < m_numBuckets; i++) {
		if (m_buckets[i]->getCollnum() == collnum) {
			numKeys += m_buckets[i]->getNumKeys();
		}

		if (m_buckets[i]->getCollnum() > collnum) {
			break;
		}
	}

	return numKeys;
}

int32_t RdbBuckets::getNumKeys() const {
	ScopedLock sl(m_mtx);
	return m_numKeysApprox;
}

int32_t RdbBuckets::getNumKeys_unlocked() const {
	m_mtx.verify_is_locked();

	return m_numKeysApprox;
}

int32_t RdbBuckets::getNumNegativeKeys() const {
	ScopedLock sl(m_mtx);
	return m_numNegKeys;
}

int32_t RdbBuckets::getNumPositiveKeys() const {
	ScopedLock sl(m_mtx);
	return m_numKeysApprox - m_numNegKeys;
}

void RdbBuckets::updateNumRecs_unlocked(int32_t n, int32_t bytes, int32_t numNeg) {
	m_mtx.verify_is_locked();

	m_numKeysApprox += n;
	m_dataMemOccupied += bytes;
	m_numNegKeys += numNeg;
}

const char *RdbBucket::getFirstKey() {
	sort();
	return m_keys;
}

int32_t RdbBucket::getNumNegativeKeys ( ) const {
	int32_t numNeg = 0;
	int32_t recSize = m_parent->m_recSize;
	char *currKey = m_keys;
	char *lastKey = m_keys + (m_numKeys * recSize);

	while (currKey < lastKey) {
		if (KEYNEG(currKey)) {
			numNeg++;
		}
		currKey += recSize;
	}

	return numNeg;
}

bool RdbBucket::getList(RdbList* list, const char* startKey, const char* endKey, int32_t minRecSizes,
                        int32_t *numPosRecs, int32_t *numNegRecs, bool useHalfKeys) {
	sort();

	//get our bounds within the bucket:
	uint8_t ks = m_parent->m_ks;
	int32_t recSize = m_parent->m_recSize;
	int32_t start = 0;
	int32_t end = m_numKeys - 1;
	char v;
	char *kk = NULL;
	if (startKey) {
		int32_t low = 0;
		int32_t high = m_numKeys - 1;
		while (low <= high) {
			int32_t delta = high - low;
			start = low + (delta >> 1);
			kk = m_keys + (recSize * start);
			v = KEYCMP(startKey, kk, ks);
			if (v < 0) {
				high = start - 1;
				continue;
			} else if (v > 0) {
				low = start + 1;
				continue;
			} else {
				break;
			}
		}
		//now back up or move forward s.t. startKey
		//is <= start
		while (start < m_numKeys) {
			kk = m_keys + (recSize * start);
			v = KEYCMP(startKey, kk, ks);
			if (v > 0) {
				start++;
			} else {
				break;
			}
		}
	} else {
		start = 0;
	}


	if (endKey) {
		int32_t low = start;
		int32_t high = m_numKeys - 1;
		while (low <= high) {
			int32_t delta = high - low;
			end = low + (delta >> 1);
			kk = m_keys + (recSize * end);
			v = KEYCMP(endKey, kk, ks);
			if (v < 0) {
				high = end - 1;
				continue;
			} else if (v > 0) {
				low = end + 1;
				continue;
			} else {
				break;
			}

		}
		while (end > 0) {
			kk = m_keys + (recSize * end);
			v = KEYCMP(endKey, kk, ks);
			if (v < 0) {
				end--;
			} else {
				break;
			}
		}

	} else {
		end = m_numKeys - 1;
	}

	//keep track of our negative a positive recs
	int32_t numNeg = 0;
	int32_t numPos = 0;

	int32_t fixedDataSize = m_parent->m_fixedDataSize;

	char *currKey = m_keys + (start * recSize);

	//bail now if there is only one key and it is out of range.
	if (start == end && ((startKey && KEYCMP(currKey, startKey, ks) < 0) || (endKey && KEYCMP(currKey, endKey, ks) > 0))) {
		return true;
	}

	char *lastKey = NULL;
	for (int32_t i = start;
	     i <= end && list->getListSize() < minRecSizes;
	     i++, currKey += recSize) {
		if (fixedDataSize == 0) {
			if (!list->addRecord(currKey, 0, NULL)) {
				log(LOG_WARN, "db: Failed to add record to list for %s: %s. Fix the growList algo.",
				    m_parent->m_dbname, mstrerror(g_errno));
				return false;
			}
		} else {
			int32_t dataSize = fixedDataSize;
			if (fixedDataSize == -1) {
				dataSize = *(int32_t *)(currKey + ks + sizeof(char *));
			}
			if (!list->addRecord(currKey, dataSize, currKey + ks)) {
				log(LOG_WARN, "db: Failed to add record to list for %s: %s. Fix the growList algo.",
				    m_parent->m_dbname, mstrerror(g_errno));
				return false;
			}
		}
		if (KEYNEG(currKey)) {
			numNeg++;
		} else {
			numPos++;
		}
		lastKey = currKey;

#ifdef GBSANITYCHECK
		//sanity, remove for production
		if(startKey && KEYCMP(currKey, startKey, ks) < 0) {
			log("db: key is outside the "
				"keyrange given for getList."
				"  it is < startkey."
				"  %016" PRIx64"%08" PRIx32" %016" PRIx64"%08" PRIx32"."
				"  getting keys %" PRId32" to %" PRId32" for list"
				"bounded by %016" PRIx64"%08" PRIx32" %016" PRIx64"%08" PRIx32,
				*(int64_t*)(startKey+(sizeof(int32_t))),
				*(int32_t*)startKey,
				*(int64_t*)(currKey+(sizeof(int32_t))),
				*(int32_t*)currKey,
				start, end,
				 *(int64_t*)(startKey+(sizeof(int32_t))),
				*(int32_t*)startKey,
				 *(int64_t*)(endKey+(sizeof(int32_t))),
				*(int32_t*)endKey);

			printBucket(-1);
			gbshutdownAbort(true);
		}
		if(endKey &&   KEYCMP(currKey, endKey, ks) > 0) {
			log("db: key is outside the "
				"keyrange given for getList."
				"  it is > endkey"
				"  %016" PRIx64"%08" PRIx32" %016" PRIx64"%08" PRIx32"."
				"  getting keys %" PRId32" to %" PRId32" for list"
				"bounded by %016" PRIx64"%08" PRIx32" %016" PRIx64"%08" PRIx32,
				*(int64_t*)(currKey+(sizeof(int32_t))),
				*(int32_t*)currKey,
				*(int64_t*)(endKey+(sizeof(int32_t))),
				*(int32_t*)endKey,
				start, end,
				 *(int64_t*)(startKey+(sizeof(int32_t))),
				*(int32_t*)startKey,
				 *(int64_t*)(endKey+(sizeof(int32_t))),
				*(int32_t*)endKey);

			printBucket(-1);
			gbshutdownAbort(true);
		}
#endif
	}

	// set counts to pass back, we may be accumulating over multiple
	// buckets so add it to the count
	if (numNegRecs) {
		*numNegRecs += numNeg;
	}

	if (numPosRecs) {
		*numPosRecs += numPos;
	}

	//if we don't have an end key, we were not the last bucket, so don't 
	//finalize the list... yes do, because we might've hit min rec sizes
	if (endKey == NULL && list->getListSize() < minRecSizes) {
		return true;
	}

	if (lastKey != NULL) {
		list->setLastKey(lastKey);
	}

	// reset the list's endKey if we hit the minRecSizes barrier cuz
	// there may be more records before endKey than we put in "list"
	if (list->getListSize() >= minRecSizes && lastKey != NULL) {
		// use the last key we read as the new endKey
		char newEndKey[MAX_KEY_BYTES];
		KEYSET(newEndKey, lastKey, ks);

		// . if he's negative, boost new endKey by 1 because endKey's
		//   aren't allowed to be negative
		// . we're assured there's no positive counterpart to him 
		//   since Rdb::addRecord() doesn't allow both to exist in
		//   the tree at the same time
		// . if by some chance his positive counterpart is in the
		//   tree, then it's ok because we'd annihilate him anyway,
		//   so we might as well ignore him

		// we are little endian
		if (KEYNEG(newEndKey, 0, ks)) {
			KEYINC(newEndKey, ks);
		}

		// if we're using half keys set his half key bit
		if (useHalfKeys) {
			KEYOR(newEndKey, 0x02);
		}

		if (m_parent->m_rdbId == RDB_POSDB || m_parent->m_rdbId == RDB2_POSDB2) {
			newEndKey[0] |= 0x04;
		}

		// tell list his new endKey now
		list->setEndKey(newEndKey);
	}

	// reset list ptr to point to first record
	list->resetListPtr();

	// success
	return true;
}

bool RdbBuckets::deleteNode(collnum_t collnum, const char *key) {
	ScopedLock sl(m_mtx);

	int32_t i = getBucketNum_unlocked(collnum, key);

	logTrace(g_conf.m_logTraceRdbBuckets, "key=%s, bucket=%" PRId32 "", KEYSTR(key, m_recSize), i);

	if (i == m_numBuckets || m_buckets[i]->getCollnum() != collnum) {
		logTrace(g_conf.m_logTraceRdbBuckets, "END, return false. out of bounds or wrong collnum");
		return false;
	}

	int32_t node = m_buckets[i]->getNode(key);
	logTrace(g_conf.m_logTraceRdbBuckets, "node=%" PRId32 "", node);

	if (node == -1) {
		logTrace(g_conf.m_logTraceRdbBuckets, "END, return false. Key not found in bucket");
		return false;
	}

	m_needsSave = true;

	if (!m_buckets[i]->deleteNode(node)) {
		logTrace(g_conf.m_logTraceRdbBuckets, "bucket->deleteNode returned false. Moving up bucket");

		m_buckets[i]->reset();
		memmove(&m_buckets[i], &m_buckets[i + 1], (m_numBuckets - i - 1)*sizeof(RdbBuckets*));
		--m_numBuckets;
	}

	// did we delete the whole darn thing?
	if (m_numBuckets == 0) {
		if (m_numKeysApprox != 0) {
			log(LOG_ERROR, "db: bucket's number of keys is getting off by %" PRId32" after deleting a node", m_numKeysApprox);
			gbshutdownCorrupted();
		}
		m_firstOpenSlot = 0;
	}

	logTrace(g_conf.m_logTraceRdbBuckets, "END, returning true");
	return true;
}

bool RdbBuckets::deleteList_unlocked(collnum_t collnum, RdbList *list) {
	m_mtx.verify_is_locked();

	if (list->getListSize() == 0) {
		return true;
	}

	if (!m_isWritable || m_isSaving) {
		g_errno = EAGAIN;
		return false;
	}

	// . set this right away because the head bucket needs to know if we
	// . need to save
	m_needsSave = true;

	char startKey[MAX_KEY_BYTES];
	char endKey[MAX_KEY_BYTES];
	list->getStartKey(startKey);
	list->getEndKey(endKey);

	int32_t startBucket = getBucketNum_unlocked(collnum, startKey);
	if (startBucket > 0 && bucketCmp_unlocked(collnum, startKey, m_buckets[startBucket - 1]) < 0) {
		startBucket--;
	}

	// if the startKey is past our last bucket, then nothing
	// to delete
	if (startBucket == m_numBuckets || m_buckets[startBucket]->getCollnum() != collnum) {
		return true;
	}

	int32_t endBucket = getBucketNum_unlocked(collnum, endKey);
	if (endBucket == m_numBuckets || m_buckets[endBucket]->getCollnum() != collnum) {
		endBucket--;
	}

	list->resetListPtr();
	for (int32_t i = startBucket; i <= endBucket && !list->isExhausted(); i++) {
		if (!m_buckets[i]->deleteList(list)) {
			m_buckets[i]->reset();
			m_buckets[i] = NULL;
		}
	}
	int32_t j = 0;
	for (int32_t i = 0; i < m_numBuckets; i++) {
		if (m_buckets[i]) {
			m_buckets[j++] = m_buckets[i];
		}
	}
	m_numBuckets = j;

	//did we delete the whole darn thing?  
	if (m_numBuckets == 0) {
		if (m_numKeysApprox != 0) {
			log(LOG_ERROR, "db: bucket's number of keys is getting off by %" PRId32" after deleting a list", m_numKeysApprox);
			gbshutdownAbort(true);
		}
		m_firstOpenSlot = 0;
	}
	return true;
}

bool RdbBucket::deleteNode(int32_t i) {
	logTrace(g_conf.m_logTraceRdbBuckets, "i=%" PRId32 "", i);

	int32_t recSize = m_parent->m_recSize;
	char *rec = m_keys + (recSize * i);

	char *data = NULL;
	int32_t dataSize = m_parent->m_fixedDataSize;
	if (dataSize != 0) {
		data = *(char**)(rec + m_parent->m_ks);

		if (dataSize == -1) {
			dataSize = *(int32_t*)(data + sizeof(char*));
		}

		/// @todo ALC use proper note
		mdelete(data, dataSize, "RdbBucketData");
		delete data;
	}

	// delete record
	int32_t numNeg = KEYNEG(rec);
	memmove(rec, rec + recSize, (m_numKeys-i-1) * recSize);
	m_parent->updateNumRecs_unlocked(-1, -dataSize, -numNeg);
	--m_numKeys;

	// make sure there are still entries left
	if (m_numKeys) {
		m_endKey = m_keys + ((m_numKeys - 1) * recSize);
		m_lastSorted = m_numKeys;
		return true;
	}

	return false;
}

bool RdbBucket::deleteList(RdbList *list) {
	sort();

	uint8_t ks = m_parent->m_ks;
	int32_t recSize = m_parent->m_recSize;
	int32_t fixedDataSize = m_parent->m_fixedDataSize;
	char v;

	char *currKey = m_keys;
	char *p = currKey;
	char listkey[MAX_KEY_BYTES];

	char *lastKey = m_keys + (m_numKeys * recSize);
	int32_t br = 0; //bytes removed
	int32_t dso = ks + sizeof(char *);//datasize offset
	int32_t numNeg = 0;

	list->getCurrentKey(listkey);
	while (currKey < lastKey) {
		v = KEYCMP(currKey, listkey, ks);
		if (v == 0) {
			if (fixedDataSize != 0) {
				if (fixedDataSize == -1) {
					br += *(int32_t *)(currKey + dso);
				} else {
					br += fixedDataSize;
				}
			}
			if (KEYNEG(currKey)) {
				numNeg++;
			}

			// . forget it exists by advancing read ptr without 
			// . advancing the write ptr
			currKey += recSize;
			if (!list->skipCurrentRecord()) {
				break;
			}
			list->getCurrentKey(listkey);
			continue;
		} else if (v < 0) {
			// . copy this key into place, it was not in the 
			// . delete list
			if (p != currKey) {
				gbmemcpy(p, currKey, recSize);
			}
			p += recSize;
			currKey += recSize;
		} else { //list key > current key
			// . otherwise advance the delete list until 
			//listKey is <= currKey
			if (!list->skipCurrentRecord()) {
				break;
			}
			list->getCurrentKey(listkey);
		}
	}

	// . do we need to finish copying our list down to the 
	// . vacated mem?
	if (currKey < lastKey) {
		int32_t tmpSize = lastKey - currKey;
		gbmemcpy(p, currKey, tmpSize);
		p += tmpSize;
	}

	if (p > m_keys) {    //do we have anything left?
		int32_t newNumKeys = (p - m_keys) / recSize;
		m_parent->updateNumRecs_unlocked(newNumKeys - m_numKeys, -br, -numNeg);
		m_numKeys = newNumKeys;
		m_lastSorted = m_numKeys;
		m_endKey = m_keys + ((m_numKeys - 1) * recSize);
		return true;

	} else {
		//we deleted the entire bucket, let our parent know to free us
		m_parent->updateNumRecs_unlocked(-m_numKeys, -br, -numNeg);
		return false;
	}
}


// remove keys from any non-existent collection
void RdbBuckets::cleanBuckets() {
	ScopedLock sl(m_mtx);

	// the liberation count
	int32_t count = 0;

	for (;;) {
		bool restart = false;

		for (int32_t i = 0; i < m_numBuckets; i++) {
			RdbBucket *b = m_buckets[i];
			collnum_t collnum = b->getCollnum();
			if (collnum < g_collectiondb.getNumRecs()) {
				if (g_collectiondb.getRec(collnum)) {
					continue;
				}
			}

			// count # deleted
			count += b->getNumKeys();

			// delete that coll
			delColl_unlocked(collnum);

			// restart
			restart = true;
			break;
		}

		if (!restart) {
			break;
		}
	}

	if (count != 0) {
		log(LOG_LOGIC, "db: Removed %" PRId32" records from %s buckets for invalid collection numbers.",
		    count, m_dbname);
	}
}

bool RdbBuckets::delColl(collnum_t collnum) {
	ScopedLock sl(m_mtx);
	return delColl_unlocked(collnum);
}

bool RdbBuckets::delColl_unlocked(collnum_t collnum) {
	m_mtx.verify_is_locked();

	m_needsSave = true;

	RdbList list;
	int32_t minRecSizes = 1024 * 1024;
	int32_t numPosRecs = 0;
	int32_t numNegRecs = 0;

	for (;;) {
		if (!getList_unlocked(collnum, KEYMIN(), KEYMAX(), minRecSizes, &list, &numPosRecs, &numNegRecs, false)) {
			if (g_errno == ENOMEM && minRecSizes > 1024) {
				minRecSizes /= 2;
				continue;
			} else {
				log(LOG_WARN, "db: buckets could not delete collection: %s.", mstrerror(errno));
				return false;
			}
		}

		if (list.isEmpty()) {
			break;
		}

		deleteList_unlocked(collnum, &list);
	}

	log(LOG_INFO, "buckets: deleted all keys for collnum %" PRId32, (int32_t)collnum);
	return true;
}

int32_t RdbBuckets::addTree(RdbTree *rt) {
	ScopedLock sl(m_mtx);
	ScopedLock sl2(rt->getLock());

	int32_t n = rt->getFirstNode_unlocked();
	int32_t count = 0;

	const char *data = NULL;
	int32_t dataSize = 0;

	while (n >= 0) {
		if (m_fixedDataSize != 0) {
			data = rt->getData_unlocked(n);
			dataSize = rt->getDataSize_unlocked(n);
		}

		if (!addNode_unlocked(rt->getCollnum_unlocked(n), rt->getKey_unlocked(n), data, dataSize)) {
			break;
		}
		n = rt->getNextNode_unlocked(n);
		count++;
	}

	log(LOG_DEBUG, "db: added %" PRId32" keys from tree to buckets for %s.", count, m_dbname);
	return count;
}

//return the total bytes of the list bookended by startKey and endKey
int64_t RdbBuckets::estimateListSize(collnum_t collnum, const char *startKey, const char *endKey, char *minKey, char *maxKey) const {
	ScopedLock sl(m_mtx);

	if (minKey) {
		KEYSET(minKey, endKey, m_ks);
	}

	if (maxKey) {
		KEYSET(maxKey, startKey, m_ks);
	}

	int32_t startBucket = getBucketNum_unlocked(collnum, startKey);
	if (startBucket > 0 && bucketCmp_unlocked(collnum, startKey, m_buckets[startBucket - 1]) < 0) {
		startBucket--;
	}

	if (startBucket == m_numBuckets || m_buckets[startBucket]->getCollnum() != collnum) {
		return 0;
	}

	int32_t endBucket = getBucketNum_unlocked(collnum, endKey);
	if (endBucket == m_numBuckets || m_buckets[endBucket]->getCollnum() != collnum) {
		endBucket--;
	}

	int64_t retval = 0;
	for (int32_t i = startBucket; i <= endBucket; i++) {
		retval += m_buckets[i]->getNumKeys();
	}

	return retval * m_recSize;
}


// . caller should call f->set() himself
// . we'll open it here
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool RdbBuckets::fastSave(const char *dir, bool useThread, void *state, void (*callback)(void *state)) {
	ScopedLock sl(m_mtx);

	logTrace(g_conf.m_logTraceRdbBuckets, "BEGIN. dir=%s", dir);

	if (g_conf.m_readOnlyMode) {
		logTrace(g_conf.m_logTraceRdbBuckets, "END. Read only mode. Returning true.");
		return true;
	}

	// we do not need a save
	if (!m_needsSave) {
		logTrace(g_conf.m_logTraceRdbBuckets, "END. Don't need to save. Returning true.");
		return true;
	}

	// return true if already in the middle of saving
	if (m_isSaving) {
		logTrace(g_conf.m_logTraceRdbBuckets, "END. Is already saving. Returning false.");
		return false;
	}

	// note it
	logf(LOG_INFO, "db: Saving %s%s-buckets-saved.dat", dir, m_dbname);

	// save parms
	m_dir = dir;
	m_state = state;
	m_callback = callback;
	// assume no error
	m_errno = 0;
	// no adding to the tree now
	m_isSaving = true;

	if (useThread) {
		// make this a thread now
		if (g_jobScheduler.submit(saveWrapper, saveDoneWrapper, this, thread_type_unspecified_io, 1/*niceness*/)) {
			return false;
		}

		// if it failed
		if (g_jobScheduler.are_new_jobs_allowed()) {
			log(LOG_WARN, "db: Thread creation failed. Blocking while saving tree. Hurts performance.");
		}
	}

	sl.unlock();

	// no threads
	saveWrapper(this);
	saveDoneWrapper(this, job_exit_normal);

	logTrace(g_conf.m_logTraceRdbBuckets, "END. Returning true.");

	// we did not block
	return true;
}


void RdbBuckets::saveWrapper(void *state) {
	logTrace(g_conf.m_logTraceRdbBuckets, "BEGIN");

	// get this class
	RdbBuckets *that = (RdbBuckets *)state;

	ScopedLock sl(that->getLock());

	// assume no error since we're at the start of thread call
	that->m_errno = 0;

	// this returns false and sets g_errno on error
	that->fastSave_unlocked();

	// . resume adding to the tree
	// . this will also allow other threads to be queued
	// . if we did this at the end of the thread we could end up with
	//   an overflow of queued SAVETHREADs
	that->m_isSaving = false;

	// we do not need to be saved now?
	that->m_needsSave = false;

	if (g_errno && !that->m_errno) {
		that->m_errno = g_errno;
	}

	if (that->m_errno) {
		log(LOG_ERROR, "db: Had error saving tree to disk for %s: %s.", that->m_dbname, mstrerror(that->m_errno));
	} else {
		log(LOG_INFO, "db: Done saving %s with %" PRId32" keys (%" PRId64" bytes)",
		    that->m_dbname, that->m_numKeysApprox, that->m_bytesWritten);
	}

	logTrace(g_conf.m_logTraceRdbBuckets, "END");
}

/// @todo ALC cater for when exit_type != job_exit_normal
// we come here after thread exits
void RdbBuckets::saveDoneWrapper(void *state, job_exit_t exit_type) {
	logTrace(g_conf.m_logTraceRdbBuckets, "BEGIN");

	// get this class
	RdbBuckets *that = (RdbBuckets*)state;

	// store save error into g_errno
	g_errno = that->m_errno;

	// . call callback
	if (that->m_callback) {
		that->m_callback(that->m_state);
	}

	logTrace(g_conf.m_logTraceRdbBuckets, "END");
}

// . returns false and sets g_errno on error
// . NO USING g_errno IN A DAMN THREAD!!!!!!!!!!!!!!!!!!!!!!!!!
bool RdbBuckets::fastSave_unlocked() {
	m_mtx.verify_is_locked();

	if (g_conf.m_readOnlyMode) {
		return true;
	}

	// cannot use the BigFile class, since we may be in a thread and it
	// messes with g_errno
	char s[1024];
	sprintf(s, "%s/%s-buckets-saving.dat", m_dir, m_dbname);
	int fd = ::open(s, O_RDWR | O_CREAT | O_TRUNC, getFileCreationFlags());
	if (fd < 0) {
		m_errno = errno;
		log(LOG_ERROR, "db: Could not open %s for writing: %s.", s, mstrerror(errno));
		return false;
	}

	// clear our own errno
	errno = 0;

	// . save the header
	// remember total bytes written
	m_bytesWritten = fastSaveColl_unlocked(fd);

	// close it up
	close(fd);

	char s2[1024];
	sprintf(s2, "%s/%s-buckets-saved.dat", m_dir, m_dbname);
	if( ::rename(s, s2) == -1 ) {
		log(LOG_LOGIC,"%s:%s: ERROR %d renaming [%s] to [%s]", __FILE__, __func__, errno, s, s2);
		gbshutdownAbort(true);
	}

	return m_bytesWritten >= 0;
}

int64_t RdbBuckets::fastSaveColl_unlocked(int fd) {
	m_mtx.verify_is_locked();

	int64_t offset = 0;

	if (m_numKeysApprox == 0) {
		return offset;
	}

	int32_t version = SAVE_VERSION;
	int32_t err = 0;
	if (pwrite(fd, &version, sizeof(int32_t), offset) != 4) err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_numBuckets, sizeof(int32_t), offset) != 4)err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_maxBuckets, sizeof(int32_t), offset) != 4)err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_ks, sizeof(uint8_t), offset) != 1) err = errno;
	offset += sizeof(uint8_t);

	if (pwrite(fd, &m_fixedDataSize, sizeof(int32_t), offset) != 4) err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_recSize, sizeof(int32_t), offset) != 4) err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_numKeysApprox, sizeof(int32_t), offset) != 4)err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_numNegKeys, sizeof(int32_t), offset) != 4) err = errno;
	offset += sizeof(int32_t);

	if (pwrite(fd, &m_dataMemOccupied, sizeof(int32_t), offset) != 4)err = errno;
	offset += sizeof(int32_t);

	int32_t tmp = BUCKET_SIZE;
	if (pwrite(fd, &tmp, sizeof(int32_t), offset) != 4) err = errno;
	offset += sizeof(int32_t);

	// set it
	if (err) {
		errno = err;
	}

	// bitch on error
	if (errno) {
		m_errno = errno;
		close(fd);
		log(LOG_ERROR, "db: Failed to save buckets for %s: %s.", m_dbname, mstrerror(errno));
		return -1;
	}

	// position to store into m_keys, ...
	for (int32_t i = 0; i < m_numBuckets; i++) {
		offset = m_buckets[i]->fastSave_r(fd, offset);
		// returns -1 on error
		if (offset < 0) {
			close(fd);
			m_errno = errno;
			log(LOG_ERROR, "db: Failed to save buckets for %s: %s.", m_dbname, mstrerror(errno));
			return -1;
		}
	}
	return offset;
}

bool RdbBuckets::loadBuckets(const char *dbname) {
	ScopedLock sl(m_mtx);

	char filename[256];
	sprintf(filename, "%s-buckets-saved.dat", dbname);

	// set a BigFile to this filename
	BigFile file;
	const char *dir = g_hostdb.m_dir;
	if (*dir == '\0') {
		dir = ".";
	}

	file.set(dir, filename);
	if (!file.doesExist()) {
		return true;
	}

	// load the table with file named "THISDIR/saved"
	return fastLoad_unlocked(&file, dbname);
}

bool RdbBuckets::fastLoad_unlocked(BigFile *f, const char *dbname) {
	m_mtx.verify_is_locked();

	log(LOG_INIT, "db: Loading %s.", f->getFilename());

	// open it up
	if (!f->open(O_RDONLY)) {
		return false;
	}

	int64_t fsize = f->getFileSize();
	if (fsize == 0) {
		return true;
	}

	// start reading at offset 0
	int64_t offset = fastLoadColl_unlocked(f, dbname);
	if (offset < 0) {
		log(LOG_ERROR, "db: Failed to load buckets for %s: %s.", m_dbname, mstrerror(g_errno));
		return false;
	}

	return true;
}

int64_t RdbBuckets::fastLoadColl_unlocked(BigFile *f, const char *dbname) {
	m_mtx.verify_is_locked();

	int32_t maxBuckets;
	int32_t numBuckets;
	int32_t version;

	int64_t offset = 0;
	f->read(&version, sizeof(int32_t), offset);
	offset += sizeof(int32_t);
	if (version > SAVE_VERSION) {
		log(LOG_ERROR, "db: Failed to load buckets for %s: saved version is in the future or is corrupt, "
				"please restart old executable and do a ddump.", m_dbname);
		return -1;
	}

	f->read(&numBuckets, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&maxBuckets, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_ks, sizeof(uint8_t), offset);
	offset += sizeof(uint8_t);

	f->read(&m_fixedDataSize, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_recSize, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_numKeysApprox, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_numNegKeys, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_dataMemOccupied, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	int32_t bucketSize;
	f->read(&bucketSize, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	if (bucketSize != BUCKET_SIZE) {
		log(LOG_ERROR, "db: It appears you have changed the bucket size please restart the old executable and dump "
				"buckets to disk. old=%" PRId32" new=%" PRId32, bucketSize, (int32_t)BUCKET_SIZE);
		gbshutdownAbort(true);
	}

	m_dbname = dbname;

	if (g_errno) {
		return -1;
	}

	for (int32_t i = 0; i < numBuckets; i++) {
		// BUGFIX 20160705: Do NOT assign result of bucketFactory
		// directly to m_buckets[i], as bucketFactory may call 
		// resizeTable that modifies the m_buckets pointer. Cores
		// seen in binary generated with g++ 4.9.3
		RdbBucket *bf = bucketFactory_unlocked();
		if (!m_buckets) {
			log(LOG_ERROR, "db: m_buckets is NULL after call to bucketFactory()!");
			gbshutdownLogicError();
		}
		m_buckets[i] = bf;
		if (m_buckets[i] == NULL) {
			return -1;
		}

		offset = m_buckets[i]->fastLoad(f, offset);
		// returns -1 on error
		if (offset < 0) {
			return -1;
		}
		m_numBuckets++;
	}

	return offset;
}

// max key size -- posdb, 18 bytes, so use 18 here
#define BTMP_SIZE (BUCKET_SIZE*18+1000)

int64_t RdbBucket::fastSave_r(int fd, int64_t offset) {
	// first copy to a buf before saving so we can unlock!
	char tmp[BTMP_SIZE];
	char *p = tmp;

	memcpy(p, &m_collnum, sizeof(collnum_t));
	p += sizeof(collnum_t);

	memcpy(p, &m_numKeys, sizeof(int32_t));
	p += sizeof(m_numKeys);

	memcpy(p, &m_lastSorted, sizeof(int32_t));
	p += sizeof(m_lastSorted);

	int32_t endKeyOffset = m_endKey - m_keys;
	memcpy(p, &endKeyOffset, sizeof(int32_t));
	p += sizeof(int32_t);

	int32_t recSize = m_parent->m_recSize;

	memcpy(p, m_keys, recSize * m_numKeys);
	p += recSize * m_numKeys;

	int32_t size = p - tmp;
	if (size > BTMP_SIZE) {
		log(LOG_ERROR, "buckets: btmp_size too small. keysize>18 bytes?");
		gbshutdownAbort(true);
	}

	// now we can save it without fear of being interrupted and having
	// the bucket altered
	errno = 0;
	if (pwrite(fd, tmp, size, offset) != size) {
		log(LOG_WARN, "db:fastSave_r: %s.", mstrerror(errno));
		return -1;
	}

	return offset + size;
}

int64_t RdbBucket::fastLoad(BigFile *f, int64_t offset) {
	f->read(&m_collnum, sizeof(collnum_t), offset);
	offset += sizeof(collnum_t);

	f->read(&m_numKeys, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	f->read(&m_lastSorted, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	int32_t endKeyOffset;
	f->read(&endKeyOffset, sizeof(int32_t), offset);
	offset += sizeof(int32_t);

	int32_t recSize = m_parent->m_recSize;

	f->read(m_keys, recSize * m_numKeys, offset);
	offset += recSize * m_numKeys;

	m_endKey = m_keys + endKeyOffset;
	if (g_errno) {
		log(LOG_WARN, "bucket: fastload %s", mstrerror(g_errno));
		return -1;
	}

	return offset;
}
