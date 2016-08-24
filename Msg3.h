// Matt Wells, copyright Sep 2001

// . gets an RdbList from disk
// . reads from N specified files and stores results in N RdbLists

#ifndef GB_MSG3_H
#define GB_MSG3_H
#include "rdbid_t.h"

class RdbCache *getDiskPageCache ( rdbid_t rdbId ) ;

// . max # of rdb files an rdb can have w/o merging
// . merge your files to keep the number of them low to cut down # of seeks
// . we try to keep it down to only 1 file through merging
// . now that we embed a title file num in tfndb for each docid, titledb
//   only needs to be merged to collide positive/negative recs to save disk
//   space, so we do not want to be limited by number of files for titledb
// . we bumped this up to 512 to help get more sites out of site search
//#define MAX_RDB_FILES 512
// allow us to spider for a while without having to merge
//#define MAX_RDB_FILES 2048
// make Msg5 footprint smaller
//#define MAX_RDB_FILES 512
// make Msg5 footprint smaller since we have "whitelist" in msg2.cpp
// we need to run one msg5 per whitelisted site then and we can have up to
// 500 sites in the whitelist.
#define MAX_RDB_FILES 1024

#include "RdbList.h"
#include "RdbScan.h"

class Msg3 {

 public:

	Msg3();
	~Msg3();

	// just sets # of read lists (m_numScansCompleted) to 0
	void reset();

	// . try to get at least minRecSizes worth of records
	// . endKey of "list" may be less than "endKey" provided
	// . sometimes there is a disk read error (due to merge deleting files)
	//   and retryNum/maxRetries help define the retries
	// . if "numFiles" is -1, it means read ALL files available
	// . if "justGetEndKey" is true, then the call just sets
	//   m_msg3.m_endKey and m_msg3.m_constrainKey. This is just used
	//   by Msg5.cpp to constrain the endKey so it can read the recs
	//   from the tree using that endKey, and not waste time.
	bool readList  ( rdbid_t           rdbId,
			 collnum_t collnum ,
			 const char       *startKey      ,
			 const char       *endKey        ,
			 int32_t           minRecSizes   , // scan size(-1 all)
			 int32_t           startFileNum  , // first file to scan
			 int32_t           numFiles      , // rel.2 startFileNum
			 void          *state         , // for callback
			 void         (* callback ) ( void *state ) ,
			 int32_t           niceness      , // = MAX_NICENESS ,
			 int32_t           retryNum      , // = 0             ,
			 int32_t           maxRetries    , // = -1
			 bool           compensateForMerge ,
			 bool           justGetEndKey = false ,
			 bool           allowPageCache = true ,
			 bool           hitDisk        = true );

	// for retrieving unmerged lists
	RdbList        *getList ( int32_t i )       { return &m_lists[i]; }
	const RdbList  *getList ( int32_t i ) const { return &m_lists[i]; }
	int32_t         getNumLists() const { return m_numScansCompleted; }
	bool            areAllScansCompleted() const { return m_numScansCompleted==m_numScansStarted; }

	void     *m_state;
	void    (* m_callback )( void *state );


	bool isListChecked() const { return m_listsChecked; }
	bool listHadCorruption() const { return m_hadCorruption; }
	int32_t getFileNums() const { return m_numFileNums; }
	int32_t getFileNum(int32_t i) const { return m_fileNums[i]; }

	// end key to use when calling constrain_r()
	char      m_constrainKey[MAX_KEY_BYTES];

private:
	// keep public for doneScanningWrapper to use
	bool      doneScanning    ( );

	// on read/write error we sleep and retry
	bool doneSleeping ();

	// this might increase m_minRecSizes
	void compensateForNegativeRecs ( class RdbBase *base ) ;

	// . sets page ranges for RdbScan (m_startpg[i], m_endpg[i])
	// . returns the endKey for all RdbScans
	void  setPageRanges ( class RdbBase *base     ,
			      int32_t      *fileNums     ,
			      int32_t       numFileNums  ,
			      const char  *startKey     ,
			      char      *endKey       ,
			      int32_t       minRecSizes  );

	static void doneScanningWrapper(void *state);
	static void doneSleepingWrapper3(int fd, void *state);

	// the rdb we're scanning for
	rdbid_t  m_rdbId;
	collnum_t m_collnum;

	bool m_validateCache;

	// the scan classes, 1 per file, used to read from that file
	RdbScan *m_scans ;

	// page ranges for each scan computed in setPageRanges()
	int32_t    *m_startpg ;
	int32_t    *m_endpg   ;

	char    *m_hintKeys    ;
	int32_t    *m_hintOffsets ;

	int32_t     m_startFileNum;
	int32_t     m_numFiles    ;

	int32_t    *m_fileNums    ;
	int32_t     m_numFileNums;

	int32_t      m_numScansStarted;
	int32_t      m_numScansCompleted;

	// hold the lists we read from disk here
	RdbList  *m_lists ;

	// key range to read
	const char     *m_fileStartKey;
	char      m_startKey[MAX_KEY_BYTES];
	char      m_endKey[MAX_KEY_BYTES];

	// min bytes to read
	int32_t      m_minRecSizes;

	// keep some original copies incase errno == ETRYAGAIN
	//key_t     m_endKeyOrig;
	char      m_endKeyOrig[MAX_KEY_BYTES];
	int32_t      m_minRecSizesOrig;

	int32_t      m_niceness;

	// last error received from doing all reads
	int       m_errno;

	// only retry up to m_maxRetries times in case it was a fluke
	int32_t        m_retryNum;
	int32_t        m_maxRetries;

	// for debugging
	int64_t   m_startTime;

	// . these hints make a call to constrain() fast
	// . used to quickly contrain the tail of a 1-list read
	int32_t        m_hintOffset;
	char        m_hintKey[MAX_KEY_BYTES];

	bool        m_compensateForMerge;

	char *m_alloc;
	int32_t  m_allocSize;
	int32_t  m_numChunks;
	char  m_ks;

	// for allowing the page cache
	bool  m_allowPageCache;

	bool  m_listsChecked;

	bool  m_hadCorruption;

	bool  m_hitDisk;
};

extern int32_t g_numIOErrors;

#endif // GB_MSG3_H
