// Matt Wells, copyright Sep 2000

// contains one RdbBase for each collection

#ifndef GB_RDB_H
#define GB_RDB_H

#include "RdbBase.h"
#include "RdbTree.h"
#include "RdbMem.h"
#include "RdbDump.h"
#include "RdbBuckets.h"
#include "RdbIndex.h"
#include "Hostdb.h"
#include "rdbid_t.h"
#include <atomic>

bool makeTrashDir() ;

// get the RdbBase class for an rdbId and collection name
class RdbBase *getRdbBase(rdbid_t rdbId, collnum_t collnum);

// maps an rdbId to an Rdb
class Rdb *getRdbFromId ( rdbid_t rdbId ) ;

// the reverse of the above
rdbid_t getIdFromRdb ( class Rdb *rdb ) ;
bool isSecondaryRdb ( rdbid_t rdbId ) ;

// get the dbname
const char *getDbnameFromId(rdbid_t rdbId);

// size of keys
char getKeySizeFromRdbId(rdbid_t rdbId);

// and this is -1 if dataSize is variable
int32_t getDataSizeFromRdbId ( rdbid_t rdbId );
void forceMergeAll(rdbid_t rdbId);

// main.cpp calls this
void attemptMergeAllCallback ( int fd , void *state ) ;
void attemptMergeAll ( );

class Rdb {
	/// @todo ALC remove this when method is fixed (main.cpp)
	friend int injectFile ( const char *filename , char *ips , const char *coll );

public:

	 Rdb ( );
	~Rdb ( );

	bool addRdbBase1 ( const char *coll );

	bool delColl ( const char *coll );

	bool resetBase ( collnum_t collnum );
	bool deleteAllRecs ( collnum_t collnum ) ;
	bool deleteColl ( collnum_t collnum , collnum_t newCollnum ) ;

	bool init ( const char  *dbname       , // "indexdb","tagdb",...
		    int32_t   fixedDataSize   , //= -1   ,
		    int32_t   minToMerge      , //, //=  2   ,
		    int32_t   maxTreeMem      , //=  1024*1024*32 ,
		    int32_t   maxTreeNodes    ,
		    bool   useHalfKeys     ,
		    char   keySize,
		    bool   useIndexFile);

	bool needsSave() const;

	// . returns false and sets g_errno on error
	// . caller should retry later on g_errno of ENOMEM or ETRYAGAIN
	// . returns the node # in the tree it added the record to
	// . key low bit must be set (otherwise it indicates a delete)
	/**
	 * @note special behaviour when index is enabled. if corresponding opposite key is found in tree/bucket,
	 * it is assumed that we want the opposite key deleted.
	 * eg:
	 *   positive key in tree, negative key passed to addRecord (positive key deleted)
	 *   negative key in tree, positive key passed to addRecord (negative key deleted)
	 *
	 * the reason for this is so that we can eliminate negative key in tree
	 * (negative key should never be there when index is used, unless for special reasons. eg: posdb for deleted document)
	 *
	 * the scenario where we want to eliminate a negative key is when we deleted a document, and then we respider successfully
	 */
	bool addRecord(collnum_t collnum, const char *key, const char *data, int32_t dataSize);

	// returns false if no room in tree or m_mem for a list to add
	bool hasRoom(int32_t numRecs, int32_t dataSize) const;

	bool canAdd() const;

	// . returns false on error and sets errno
	// . return true on success
	// . if we can't handle all records in list we don't add any and
	//   set errno to ETRYAGAIN or ENOMEM
	// . we copy all data so you can free your list when we're done
	bool addList(collnum_t collnum, RdbList *list) {
		return addList(collnum,list,true);
	}
	bool addListNoSpaceCheck(collnum_t collnum, RdbList *list) {
		return addList(collnum,list,false);
	}

	bool deleteTreeNode(collnum_t collnum, const char *key);

	void verifyTreeIntegrity();

	bool isSecondaryRdb() const {
		return ::isSecondaryRdb(m_rdbId);
	}
	
	bool isInitialized() const { return m_initialized; }

	int32_t getFixedDataSize() const { return m_fixedDataSize; }

	bool useHalfKeys() const { return m_useHalfKeys; }
	char getKeySize() const { return m_ks; }
	int32_t getPageSize() const { return m_pageSize; }

	bool isTitledb() const { return m_rdbId==RDB_TITLEDB || m_rdbId==RDB2_TITLEDB2; }

	RdbBuckets* getBuckets() {
		if (m_useTree) return NULL;
		return &m_buckets;
	}

	int32_t getAvailMem() const { return m_mem.getAvailMem(); }
	int32_t getUsedMem() const { return m_mem.getUsedMem(); }
	bool       useTree() const { return m_useTree;}

	int32_t       getNumUsedNodes() const;
	int32_t       getMaxTreeMem() const;
	int32_t       getTreeMemOccupied() const;
	int32_t       getTreeMemAllocated() const;
	int32_t       getNumNegativeKeys() const;

	void disableWrites();
	void enableWrites();
	bool isWritable() const;

	void cleanTree();

	RdbBase *getBase(collnum_t collnum );
	const RdbBase *getBase(collnum_t collnum ) const { return const_cast<Rdb*>(this)->getBase(collnum); }
	int32_t getNumBases() const;


	// how much mem is allocated for our maps?
	int64_t getMapMemAllocated() const;

	int32_t getNumFiles() const;

	// sum of all parts of all big files
	int32_t getNumSmallFiles() const;
	int64_t getDiskSpaceUsed() const;

	// use the maps and tree to estimate the size of this list
	int64_t estimateListSize(collnum_t collnum,
				 const char *startKey, const char *endKey, char *maxKey,
				 int64_t oldTruncationLimit) const;

	//Get list from tree or buckets. Returns true on success
	bool getTreeList(RdbList *result,
			 collnum_t collnum,
	                 const void *startKey, const void *endKey,
			 int32_t minRecSizes,
			 int32_t *numPositiveRecs, int32_t *numNegativeRecs,
			 int32_t *memUsedByTree, int32_t *numUsedNodes);

	// positive minus negative
	int64_t getNumTotalRecs(bool useCache = false) const;

	int64_t getCollNumTotalRecs(collnum_t collnum) const; //could technically be static

	// used for keeping track of stats
	void    didSeek() { m_numSeeks++; }
	void    didRead(int64_t bytes) { m_numRead += bytes; }
	void    didReSeek() { m_numReSeeks++; }
	int64_t getNumSeeks()   const { return m_numSeeks; }
	int64_t getNumReSeeks() const { return m_numReSeeks; }
	int64_t getNumRead()    const { return m_numRead ; }

	// net stats for "get" requests
	void    readRequestGet(int32_t bytes) { m_numReqsGet++; m_numNetReadGet += bytes; }
	void    sentReplyGet(int32_t bytes) { m_numRepliesGet++; m_numNetSentGet += bytes; }
	int64_t getNumRequestsGet() const { return m_numReqsGet; }
	int64_t getNetReadGet()     const { return m_numNetReadGet; }
	int64_t getNumRepliesGet()  const { return m_numRepliesGet; }
	int64_t getNetSentGet()     const { return m_numNetSentGet; }

	// net stats for "add" requests
	void    readRequestAdd(int32_t bytes) { m_numReqsAdd++; m_numNetReadAdd += bytes; }
	void    sentReplyAdd(int32_t bytes) { m_numRepliesAdd++ ; m_numNetSentAdd += bytes; }
	int64_t getNumRequestsAdd() const { return m_numReqsAdd; }
	int64_t getNetReadAdd()     const { return m_numNetReadAdd; }
	int64_t getNumRepliesAdd()  const { return m_numRepliesAdd; }
	int64_t getNetSentAdd()     const { return m_numNetSentAdd; }

	rdbid_t getRdbId() const { return m_rdbId; }
	const char* getDbname() const { return m_dbname; }

	bool isDumping() const { return m_isDumping; }

	bool isUseIndexFile() const { return m_useIndexFile; }

	// . you'll lose your data in this class if you call this
	void reset();

	bool isSavingTree() const;

	bool saveTree(bool useThread, void *state, void (*callback)(void *state));
	bool saveIndexes();
	bool saveMaps();

	// . load the tree named "saved.dat", keys must be out of order because
	//   tree is not balanced
	bool loadTree ( ) ;

	// . write out tree to a file with keys in order
	// . only shift.cpp/reindex.cpp programs set niceness to 0
	bool dumpTree();

	bool needsDump() const;

	// these are used for computing load on a machine
	bool isMerging() const;
	void incrementNumMerges() { ++m_numMergesOut; }
	void decrementNumMerges() { --m_numMergesOut; }

	// PageRepair.cpp calls this when it is done rebuilding an rdb
	// and wants to tell the primary rdb to reload itself using the newly
	// rebuilt files, pointed to by rdb2.
	bool updateToRebuildFiles ( Rdb *rdb2 , char *coll ) ;

	static void doneDumpingCollWrapper(void *state);

private:
	bool addRdbBase2 ( collnum_t collnum );
	void addBase(collnum_t collnum, RdbBase *base);

	// returns false if no room in tree or m_mem for a list to add
	bool hasRoom(RdbList *list);

	bool addList(collnum_t collnum, RdbList *list, bool checkForRoom);
	// get the directory name where this rdb stores its files
	const char *getDir() const { return g_hostdb.m_dir; }

	bool dumpCollLoop ( ) ;

	// . called when we've dumped the tree to disk w/ keys ordered
	void doneDumping ( );

	int32_t reclaimMemFromDeletedTreeNodes();
	int32_t m_lastReclaim;

	int32_t      m_fixedDataSize;

	char      m_dbname [32];
	int32_t      m_dbnameLen;

	bool		m_useIndexFile;

	// for storing records in memory
	RdbTree    m_tree;  
	RdbBuckets m_buckets;

	bool       m_useTree;

	// for dumping a table to an rdb file
	RdbDump   m_dump;

	// memory for us to use to avoid calling malloc()/mdup()/...
	RdbMem    m_mem;

	mutable int32_t m_cacheLastTime;
	mutable int64_t m_cacheLastTotal;

	std::atomic<int32_t> m_numMergesOut;

	int32_t      m_minToMerge;  // need at least this many files b4 merging

	int32_t m_dumpErrno;

	// a dummy data string for deleting records when m_fixedDataSize > 0

	// for keeping stats
	std::atomic<int64_t>     m_numSeeks;
	std::atomic<int64_t>     m_numReSeeks;
	std::atomic<int64_t> m_numRead;

	// network request/reply info for get requests
	std::atomic<int64_t>     m_numReqsGet;
	std::atomic<int64_t> m_numNetReadGet;
	std::atomic<int64_t>     m_numRepliesGet;
	std::atomic<int64_t> m_numNetSentGet;

	// network request/reply info for add requests
	std::atomic<int64_t>     m_numReqsAdd;
	std::atomic<int64_t> m_numNetReadAdd;
	std::atomic<int64_t>     m_numRepliesAdd;
	std::atomic<int64_t> m_numNetSentAdd;

	// . when we dump list to an rdb file, can we use short keys?
	// . currently exclusively used by indexdb
	bool      m_useHalfKeys;

	bool      m_niceness;

	char m_treeAllocName[64]; //for memory used m_tree/m_buckets
	char m_memAllocName[64]; //for memory used by m_mem

	collnum_t m_dumpCollnum;

	// set to true when dumping tree so RdbMem does not use the memory
	// being dumped to hold newly added records
	bool m_isDumping;

	rdbid_t m_rdbId;

	char m_ks; // key size
	int32_t m_pageSize;

	bool m_initialized;
};

#endif // GB_RDB_H
