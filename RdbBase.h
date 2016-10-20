// Matt Wells, copyright Sep 2000

// . the core database class, we have one of these for each collection and
//   pointer to them are stored in the new "Rdb" class
// . records stored on disk ordered by lowest key first
// . allows simple key-based record retrieval
// . uses non-blocking i/o with callbacks
// . thread UNsafe for maximum speed
// . has a "groupMask" that allows us to split db between multiple Rdb's
// . uses BigFile class to support files bigger than 2gb
// . can instantly delete records in memory 
// . deletes records on disk by re-writing them to disk with key low bit CLEAR
// . we merge files (non-blocking operation) into 1 file to save on disk seeks
// . adding a record with the same key as an existing one we will replace it
//   unless you set dedup to false which is yet to be supported
// . when mem is low dumps records from tree to disk, frees as it dumps
// . has a key-to-diskOffset/dataSize map in memory (good for small records)
//   for mapping a file of records on disk
// . this key-to-offset map takes up sizeof(key96_t)+  bytes per disk page
// . we can map .8 gigs of disk with 1 meg of mem (using page size of 8k)
// . memory is only freed by the Mem.h class when it finds it's running out
// . addRecord will only return false if there's some lack of memory problems
// . we can only dump the RdbTree to disk if it's using at least "minMem" or 
//   we are shutting down and Rdb::close() was called 

#ifndef GB_RDBBASE_H
#define GB_RDBBASE_H

#include "Conf.h"
#include "Mem.h"
#include "RdbScan.h"
#include "RdbDump.h"
#include "RdbMerge.h"
#include "Msg3.h"               // MAX_RDB_FILES definition
#include "Dir.h"
#include "RdbMem.h"
#include "RdbIndex.h"
#include "GbMutex.h"

extern RdbMerge g_merge;

class RdbBuckets;
class RdbTree;

class RdbBase {

 public:

	 RdbBase ( );
	~RdbBase ( );

	// . the more memory the tree has the less file merging required
	// . when a slot's key is ANDed with "groupMask" the result must equal
	//   "groupId" in order to be in this database
	// . "minMem" is how much mem must be used before considering dumping
	//   the RdbTree (our unbalanced btree) to disk
	// . you can fix the dataSize of all records in this rdb by setting
	//   "fixedDataSize"
	// . if "maskKeyLowLong" we mask the lower int32_t of the key and then
	//   compare that to the groupId to see if the record belongs
	// . this is currently just used by Spiderdb
	// . otherwise, we mask the high int32_t in the key
	bool init ( const char  *dir,        // working directory
		    const char  *dbname,     // "indexdb","tagdb",...
		    int32_t   fixedDataSize   ,
		    int32_t   minToMerge      ,
		    bool   useHalfKeys     ,
		    char   keySize         ,
		    int32_t   pageSize        ,
		    const char                *coll    ,
		    collnum_t            collnum ,
		    RdbTree             *tree    ,
		    RdbBuckets          *buckets ,
		    RdbDump             *dump    ,
		    Rdb           *rdb    ,
		    bool           useIndexFile);

	void closeMaps ( bool urgent );
	void saveMaps  ();

	void closeIndexes ( bool urgent );
	void saveIndexes();

	void saveTreeIndex();


	int32_t getFixedDataSize() const { return m_fixedDataSize; }

	bool useHalfKeys() const { return m_useHalfKeys; }

	BigFile *getFile(int32_t n) { return m_fileInfo[n].m_file; }
	int32_t getFileId(int32_t n) { return m_fileInfo[n].m_fileId; }
	RdbMap *getMap(int32_t n) { return m_fileInfo[n].m_map; }
	RdbIndex *getIndex(int32_t n) { return m_fileInfo[n].m_index; }

	RdbIndex *getTreeIndex() {
		if (m_useIndexFile) {
			return &m_treeIndex;
		}
		return NULL;
	}

	collnum_t  getCollnum() const { return m_collnum; }

	const char *getDbName() const { return m_dbname; }

	docidsconst_ptr_t getGlobalIndex();


	float getPercentNegativeRecsOnDisk ( int64_t *totalArg ) const;

	// how much mem is allocated for our maps?
	int64_t getMapMemAllocated() const;

	int32_t       getNumFiles() const { return m_numFiles; }

	// sum of all parts of all big files
	int32_t      getNumSmallFiles() const;
	int64_t getDiskSpaceUsed() const;

	// returns -1 if variable (variable dataSize)
	int32_t getRecSize ( ) const {
		if ( m_fixedDataSize == -1 ) return -1;
		return m_ks + m_fixedDataSize;
	}

	// use the maps and tree to estimate the size of this list
	int64_t getListSize(const char *startKey, const char *endKey, char *maxKey,
	                    int64_t oldTruncationLimit) const;

	// positive minus negative
	int64_t getNumTotalRecs() const;

	int64_t getNumGlobalRecs() const;
	
	// private:

	// returns true if merge was started, false if no merge could
	// be launched right now for some reason.
	bool attemptMerge ( int32_t niceness , bool forceMergeAll , 
			    // -1 means to not override it
			    int32_t minToMergeOverride = -1 );

	// called after merge completed
	bool incorporateMerge ( );

	// . you'll lose your data in this class if you call this
	void reset();

	// . set the m_files, m_fileMaps, m_fileIds arrays and m_numFiles
	bool setFiles ( ) ;

	bool verifyFileSharding ( );

	// . add a (new) file to the m_files/m_maps/m_fileIds arrays
	// . return array position we added it to
	// . return -1 and set errno on error
	int32_t addNewFile();

	// these are used for computing load on a machine
	bool isMerging() const { return m_isMerging; }
	bool isDumping() const { return m_dump->isDumping(); }

	bool hasMergeFile() const {
		return m_hasMergeFile;
	}

	//are files being unlinked or renamed?
	bool isManipulatingFiles() const;
	
	bool isFileBeingUnlinked(int32_t fileNum) const {
		return m_isUnlinking &&
		       fileNum >= m_mergeStartFileNum &&
		       fileNum <  m_mergeStartFileNum+m_numFilesToMerge;
	}
	
	// bury m_files[] in [a,b)
	void buryFiles ( int32_t a , int32_t b );

	// PageRepair indirectly calls this to move the map and data of this
	// rdb into the trash subdir after renaming them, because they will
	// be replaced by the rebuilt files.
	bool moveToTrash(const char *dstDir);
	// PageRepair indirectly calls this to rename the map and data files
	// of a secondary/rebuilt rdb to the filenames of the primary rdb.
	// after that, RdbBase::setFiles() is called to reload them into
	// the primary rdb. this is called after moveToTrash() is called for
	// the primary rdb.
	bool removeRebuildFromFilenames ( ) ;

	void specialInjectFileInit(const char *dir,
	                           const char *filename,
	                           collnum_t collnum,
	                           Rdb *rdb,
	                           int32_t fixedDataSize,
	                           bool useHalfKeys,
	                           char ks,
	                           int32_t pageSize,
	                           int32_t minToMerge);

	void forceNextMerge() { m_nextMergeForced = true; }

	//msg3 needs to know so it can compensate for ongoing merges
	int32_t mergeStartFileNum() const { return m_mergeStartFileNum; }
	int32_t numFilesToMerge() const { return m_numFilesToMerge; }

private:
	bool parseFilename( const char* filename, int32_t *p_fileId, int32_t *p_fileId2,
	                    int32_t *p_mergeNum, int32_t *p_endMergeFileId );

	// . we try to minimize the number of files to minimize disk seeks
	// . records that end up as not found will hit all these files
	// . when we get "m_minToMerge" or more files a merge kicks in
	// . TODO: merge should combine just the smaller files... kinda
	// . files are sorted by fileId
	// . older files are listed first (lower fileIds)
	// . filenames should include the directory (full filenames)
	// . TODO: RdbMgr should control what rdb gets merged?
	struct FileInfo {
		BigFile *m_file;
		int32_t m_fileId;
		int32_t m_fileId2; // for titledb/tfndb linking
		RdbMap *m_map;
		RdbIndex *m_index;
	} m_fileInfo[MAX_RDB_FILES + 1];
	int32_t m_numFiles;

	// mapping of docId to file
	// key format
	// dddddddd dddddddd dddddddd dddddddd  d = docId
	// dddddd.Z ........ ffffffff ffffffff  Z = delBit
	//                                      f = fileIndex
	docids_ptr_t m_docIdFileIndex;
	GbMutex m_docIdFileIndexMtx;

public:
	void generateGlobalIndex();
	void printGlobalIndex();

	static const char s_docIdFileIndex_docIdOffset = 24;
	static const char s_docIdFileIndex_docIdDelKeyOffset = 26;
	static const uint64_t s_docIdFileIndex_docIdMask    = 0xfffffffffc000000ULL;
	static const uint64_t s_docIdFileIndex_delBitMask   = 0x0000000001000000ULL;
	static const uint64_t s_docIdFileIndex_filePosMask  = 0x000000000000ffffULL;

private:
	void selectFilesToMerge(int32_t mergeNum, int32_t numFiles, int32_t *p_mini);

	bool hasFileId(int32_t fildId) const;
	void generateFilename(char *buf, size_t bufsize, int32_t fileId, int32_t fileId2, int32_t mergeNum, int32_t endMergeFileId);

	static void unlinkDoneWrapper(void *state);
	void unlinkDone();
	static void renameDoneWrapper(void *state);
	static void checkThreadsAgainWrapper(int /*fd*/, void *state);
	void renameDone();
	bool removeRebuildFromFilename(BigFile *f);

	void renameFile( int32_t currentFileIdx, int32_t newFileId, int32_t newFileId2 );

	// Add a (new) file to the m_files/m_maps/m_fileIds arrays
	// Return return array position of new entry, or -1 on error
	int32_t addFile(bool isNew, int32_t fileId, int32_t fileId2, int32_t mergeNum, int32_t endMergeFileId);

	int32_t m_x;
	int32_t m_a;

	// this class contains a ptr to us
	class Rdb           *m_rdb;

	int32_t      m_fixedDataSize;

	Dir       m_dir;
	char      m_dbname [32];
	int32_t      m_dbnameLen;

	const char      *m_coll;
	collnum_t  m_collnum;

	bool m_didRepair;

	// for storing records in memory
	RdbTree    *m_tree;  
	RdbBuckets *m_buckets;

	// index for in memory records
	RdbIndex m_treeIndex;

	// for dumping a table to an rdb file
	RdbDump    *m_dump;  

	int32_t      m_minToMergeDefault; //from init() call
	int32_t      m_minToMerge;  // need at least this many files b4 merging
	int32_t      m_numFilesToMerge   ;
	int32_t      m_mergeStartFileNum ;

	// should our next merge in waiting force itself?
	bool      m_nextMergeForced;

	// . when we dump list to an rdb file, can we use short keys?
	// . currently exclusively used by indexdb
	bool      m_useHalfKeys;

	bool	m_useIndexFile;

	bool m_isTitledb;

	// key size
	char      m_ks;

	int32_t      m_pageSize;

	bool      m_niceness;

	// we now determine when in merge mode
	bool      m_isMerging;

	// have we create the merge file?
	bool      m_hasMergeFile;

	// Record counts for files being merged. Calculated in attemptMerge() and then used
	// for logging in incorporateMerge()
	int64_t m_premergeNumPositiveRecords;
	int64_t m_premergeNumNegativeRecords;

	int32_t m_numThreads;

	bool m_isUnlinking;
};

extern bool g_dumpMode;

#endif // GB_RDBBASE_H
