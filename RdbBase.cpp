#include "gb-include.h"

#include "Rdb.h"
#include "Conf.h"
#include "Clusterdb.h"
#include "Hostdb.h"
#include "Tagdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Sections.h"
#include "Spider.h"
#include "Linkdb.h"
#include "Collectiondb.h"
#include "RdbMerge.h"
#include "Repair.h"
#include "Rebalance.h"
#include "JobScheduler.h"
#include "Process.h"
#include "Sanity.h"
#include "Dir.h"
#include "File.h"
#include "GbMoveFile.h"
#include "GbMakePath.h"
#include "Mem.h"
#include "ScopedLock.h"
#include <sys/stat.h> //mkdir(), stat()
#include <fcntl.h>
#include <algorithm>
#include <set>
#include <signal.h>
#include <algorithm>

// An RdbBase instance is a set of files for a database, eg PosDB. Each file consists of one data file (which may
// actually be multiple part files), one map file, and optionally one index file. Example:
//   posdb0001.dat
//   posdb0001.map
//   posdb0151.dat
//   posdb0151.map
//   posdb0231.dat
//   posdb0231.map
//   posdb0233.dat
//   posdb0233.map
//   posdb0235.dat
//   posdb0235.map
// The files (RdbBase::FileInfo) are kept in RdbBase::m_fileInfo and are sorted according to file-id. The file ID
// is the number (1/151/231/233/235 in above example). Normal files have an odd number. During merge the temporary
// destination merge file has an even number and additional filename components. Eg:
//   posdb0001.dat
//   posdb0001.map
//   posdb0151.dat
//   posdb0151.map
//   posdb0230.003.0235.dat
//   posdb0230.003.0235.map
//   posdb0231.dat
//   posdb0231.map
//   posdb0233.dat
//   posdb0233.map
//   posdb0235.dat
//   posdb0235.map
// The extra filename components are the source file count and the fileId of the last source file.
//
// TitleDB is special due to legacy. The *.dat files have an additional component that is always "000". eg:
//   titledb0001-000.dat
//   titledb0000-000.002.0003.dat
// The extra component is not used anymore and there are no clues about what it was used for.
//
// RdbBase::attemptMerge() is called periodically and for various reasons and with different parameters. It selects
// a consecutive range of files to merge (eg 0231..0235), inserts a lowId-1 file (0230), and then hands off the
// hard work to RdbMerge.
//
// During merge, files can be marked as unreadable (testable with RdbBase::isReadable()) because the file may be
// incomplete (eg. the destination merge file) or about to be deleted (source files when merge has finishes).
//
// When RdbMerge finishes it calls back to RdbBase::incorporateMerge() which makes a circus trick with finishing
// the merge with multiple callbacks, phases and error recovery strategies. Ultimately, RdbBase::renamesDone() is
// called which cleans up (removes knowledge of deleted files, relinquishes merge space lock, ..)


bool g_dumpMode = false;

// NEVER merge more than this many files, our current merge routine
// does not scale well to many files
static const int32_t absoluteMaxFilesToMerge = 50;

GbThreadQueue RdbBase::m_globalIndexThreadQueue;

RdbBase::RdbBase()
  : m_numFiles(0),
    m_mtxFileInfo(),
    m_docIdFileIndex(new docids_t),
    m_attemptOnlyMergeResumption(true),
    m_dumpingFileNumber(-1),
    m_dumpingFileId(-1),
    m_submittingJobs(false),
    m_outstandingJobCount(0),
    m_mtxJobCount()
{
	m_rdb = NULL;
	m_nextMergeForced = false;
	m_collectionDirName[0] = '\0';
	m_mergeDirName[0] = '\0';
	m_dbname[0] = '\0';
	m_dbnameLen = 0;

	// use bogus collnum just in case
	m_collnum = -1;

	// init below mainly to quiet coverity
	m_fixedDataSize = 0;
	m_coll = NULL;
	m_didRepair = false;
	m_tree = NULL;
	m_buckets = NULL;
	m_minToMergeDefault = 0;
	m_minToMerge = 0;
	m_numFilesToMerge = 0;
	m_mergeStartFileNum = 0;
	m_useHalfKeys = false;
	m_useIndexFile = false;
	m_isTitledb = false;
	m_ks = 0;
	m_pageSize = 0;
	m_niceness = 0;
	m_premergeNumPositiveRecords = 0;
	m_premergeNumNegativeRecords = 0;
	memset(m_fileInfo, 0, sizeof(m_fileInfo));

	reset();
}

void RdbBase::reset ( ) {
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		mdelete(m_fileInfo[i].m_file, sizeof(BigFile), "RdbBFile");
		delete m_fileInfo[i].m_file;

		mdelete(m_fileInfo[i].m_map, sizeof(RdbMap), "RdbBMap");
		delete m_fileInfo[i].m_map;

		mdelete(m_fileInfo[i].m_index, sizeof(RdbIndex), "RdbBIndex");
		delete m_fileInfo[i].m_index;
	}

	m_numFiles  = 0;
	m_isMerging = false;
}

RdbBase::~RdbBase ( ) {
	//close ( NULL , NULL );
	reset();
}

bool RdbBase::init(const char *dir,
                   const char *dbname,
                   int32_t fixedDataSize,
                   int32_t minToMergeArg,
                   bool useHalfKeys,
                   char keySize,
                   int32_t pageSize,
                   const char *coll,
                   collnum_t collnum,
                   RdbTree *tree,
                   RdbBuckets *buckets,
                   Rdb *rdb,
                   bool useIndexFile) {

	if(!dir)
		gbshutdownLogicError();

	m_didRepair = false;

	sprintf(m_collectionDirName, "%scoll.%s.%" PRId32, dir, coll, (int32_t)collnum);

	// use override from hosts.conf if present
	const char *mergeSpaceDir = strlen(g_hostdb.m_myHost->m_mergeDir) > 0 ? g_hostdb.m_myHost->m_mergeDir : g_conf.m_mergespaceDirectory;
	sprintf(m_mergeDirName, "%s/%d/coll.%s.%d", mergeSpaceDir, getMyHostId(), coll, (int32_t)collnum);

	// logDebugAdmin
	log(LOG_DEBUG,"db: adding new base for dir=%s coll=%s collnum=%" PRId32" db=%s",
	    dir,coll,(int32_t)collnum,dbname);

	//make sure merge space directory exists
	if(makePath(m_mergeDirName,getDirCreationFlags())!=0) {
		g_errno = errno;
		log(LOG_ERROR, "makePath(%s) failed with errno=%d (%s)", m_mergeDirName, errno, strerror(errno));
		return false;
	}

 top:
	// reset all
	reset();

	m_coll    = coll;
	m_collnum = collnum;
	m_tree    = tree;
	m_buckets = buckets;
	m_rdb     = rdb;

	// save the dbname NULL terminated into m_dbname/m_dbnameLen
	m_dbnameLen = strlen ( dbname );
	gbmemcpy ( m_dbname , dbname , m_dbnameLen );
	m_dbname [ m_dbnameLen ] = '\0';
	// store the other parameters
	m_fixedDataSize    = fixedDataSize;
	m_useHalfKeys      = useHalfKeys;
	m_isTitledb        = rdb->isTitledb();
	m_ks               = keySize;
	m_pageSize         = pageSize;
	m_useIndexFile		= useIndexFile;

	if (m_useIndexFile) {
		char indexName[64];
		sprintf(indexName, "%s-saved.idx", m_dbname);
		m_treeIndex.set(m_collectionDirName, indexName, m_fixedDataSize, m_useHalfKeys, m_ks, m_rdb->getRdbId(), false);

		// only attempt to read/generate when we have tree/bucket
		if ((m_tree && m_tree->getNumUsedNodes() > 0) || (m_buckets && m_buckets->getNumKeys() > 0)) {
			if (!(m_treeIndex.readIndex() && m_treeIndex.verifyIndex())) {
				g_errno = 0;
				log(LOG_WARN, "db: Could not read index file %s", indexName);

				// if 'gb dump X collname' was called, bail, we do not want to write any data
				if (g_dumpMode) {
					return false;
				}

				log(LOG_INFO, "db: Attempting to generate index file %s/%s-saved.dat. May take a while.",
				    m_collectionDirName, m_dbname);

				bool result = m_tree ? m_treeIndex.generateIndex(m_collnum, m_tree) : m_treeIndex.generateIndex(m_collnum, m_buckets);
				if (!result) {
					logError("db: Index generation failed for %s/%s-saved.dat.", m_collectionDirName, m_dbname);
					gbshutdownCorrupted();
				}

				log(LOG_INFO, "db: Index generation succeeded.");

				// . save it
				// . if we're an even #'d file a merge will follow
				//   when main.cpp calls attemptMerge()
				log("db: Saving generated index file to disk.");

				bool status = m_treeIndex.writeIndex(false);
				if (!status) {
					log(LOG_ERROR, "db: Save failed.");
					return false;
				}
			}
		}
	}

	// we can't merge more than MAX_RDB_FILES files at a time
	if ( minToMergeArg > MAX_RDB_FILES ) {
		minToMergeArg = MAX_RDB_FILES;
	}
	m_minToMergeDefault = minToMergeArg;

	// . set our m_files array
	if ( ! setFiles () ) {
		// try again if we did a repair
		if ( m_didRepair ) {
			goto top;
		}
		// if no repair, give up
		return false;
	}

	// now diskpagecache is much simpler, just basically rdbcache...
	return true;
}

// . move all files into trash subdir
// . this is part of PageRepair's repair algorithm. all this stuff blocks.
bool RdbBase::moveToTrash(const char *dstDir) {
	// loop over all files
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// rename the map file
		{
			BigFile *f = m_fileInfo[i].m_map->getFile();
			logf(LOG_INFO,"repair: Renaming %s to %s%s", f->getFilename(), dstDir, f->getFilename());
			if ( ! f->rename(f->getFilename(),dstDir) ) {
				log( LOG_WARN, "repair: Moving file had error: %s.", mstrerror( errno ) );
				return false;
			}
		}

		// rename index file if used
		if (m_useIndexFile) {
			BigFile *f = m_fileInfo[i].m_index->getFile();
			if (f->doesExist()) {
				logf(LOG_INFO, "repair: Renaming %s to %s%s", f->getFilename(), dstDir, f->getFilename());
				if (!f->rename(f->getFilename(),dstDir)) {
					log(LOG_WARN, "repair: Moving file had error: %s.", mstrerror(errno));
					return false;
				}
			}
		}

		// move the data file
		{
			BigFile *f = m_fileInfo[i].m_file;
			logf(LOG_INFO,"repair: Renaming %s to %s%s", f->getFilename(), dstDir, f->getFilename());
			if ( ! f->rename(f->getFilename(),dstDir) ) {
				log( LOG_WARN, "repair: Moving file had error: %s.", mstrerror( errno ) );
				return false;
			}
		}
	}

	if (m_useIndexFile) {
		// rename tree index file
		BigFile *f = m_treeIndex.getFile();
		if (f->doesExist()) {
			logf(LOG_INFO, "repair: Renaming %s to %s%s", f->getFilename(), dstDir, f->getFilename());
			if (!f->rename(f->getFilename(),dstDir)) {
				log(LOG_WARN, "repair: Moving file had error: %s.", mstrerror(errno));
				return false;
			}
		}
	}

	// now just reset the files so we are empty, we should have our
	// setFiles() called again once the newly rebuilt rdb files are
	// renamed, when RdbBase::rename() is called below
	reset();

	// success
	return true;
}

// . newly rebuilt rdb gets renamed to the original, after we call 
//   RdbBase::trash() on the original.
// . this is part of PageRepair's repair algorithm. all this stuff blocks.
bool RdbBase::removeRebuildFromFilenames() {
	// loop over all files
	// DON'T STOP IF ONE FAILS
	for (int32_t i = 0; i < m_numFiles; i++) {
		// rename the map file
		// get the "base" filename, does not include directory
		removeRebuildFromFilename(m_fileInfo[i].m_file);

		// rename the map file
		removeRebuildFromFilename(m_fileInfo[i].m_map->getFile());

		// rename the index file
		if (m_useIndexFile) {
			removeRebuildFromFilename(m_fileInfo[i].m_index->getFile());
		}
	}

	// reset all now
	reset();

	// now PageRepair should reload the original
	return true;
}

bool RdbBase::removeRebuildFromFilename ( BigFile *f ) {
	// get the filename
	const char *ff = f->getFilename();
	// copy it
	char buf[1024];
	strncpy ( buf , ff, sizeof(buf) );
	buf[ sizeof(buf)-1 ]='\0';
	
	// remove "Rebuild" from it
	char *p = strstr ( buf , "Rebuild" );
	if ( ! p ) {
		log( LOG_WARN, "repair: Rebuild not found in filename=%s", buf);
		return false;
	}
	// bury it
	int32_t  rlen = strlen("Rebuild");
	char *end  = buf + strlen(buf);
	int32_t  size = end - (p + rlen);
	// +1 to include the ending \0
	memmove ( p , p + rlen , size + 1 );
	// now rename this file
	logf(LOG_INFO,"repair: Renaming %s to %s",
	     f->getFilename(),buf);
	if ( ! f->rename(buf,NULL) ) {
		log( LOG_WARN, "repair: Rename to %s failed", buf );
		return false;
	}
	return true;
}

bool RdbBase::parseFilename( const char* filename, int32_t *p_fileId, int32_t *p_fileId2,
                             int32_t *p_mergeNum, int32_t *p_endMergeFileId ) {
	// then a 4 digit number should follow filename
	const char *s = filename + m_dbnameLen;
	if ( !isdigit(*(s+0)) || !isdigit(*(s+1)) || !isdigit(*(s+2)) || !isdigit(*(s+3)) ) {
		return false;
	}

	// optional 5th digit
	int32_t len = 4;
	if ( isdigit(*(s+4)) ) {
		len = 5;
	}

	// . read that id
	// . older files have lower numbers
	*p_fileId = atol2 ( s , len );
	s += len;


	*p_fileId2 = -1;

	// if we are titledb, we got the secondary id
	if ( *s == '-' ) {
		*p_fileId2 = atol2 ( s + 1 , 3 );
		s += 4;
	}

	// assume no mergeNum
	*p_mergeNum = -1;
	*p_endMergeFileId = -1;

	// if file id is even, we need the # of files being merged
	// otherwise, if it is odd, we do not
	if ( ( *p_fileId & 0x01 ) == 0x00 ) {
		if ( *s != '.' ) {
			return false;
		}
		++s;

		// 3 digit number (mergeNum)
		if ( !isdigit(*(s+0)) || !isdigit(*(s+1)) || !isdigit(*(s+2)) ) {
			return false;
		}

		*p_mergeNum = atol2( s , 3 );
		s += 4;

		// 4 digit number (endMergeNum)
		if ( !isdigit(*(s+0)) || !isdigit(*(s+1)) || !isdigit(*(s+2)) || !isdigit(*(s+3)) ) {
			return false;
		}
		*p_endMergeFileId = atol2( s, 4 );
	}

	return true;
}


bool RdbBase::hasFileId(int32_t fildId) const {
	for(int i=0; i<m_numFiles; i++)
		if(m_fileInfo[i].m_fileId==fildId)
			return true;
	return false;
}



// . this is called to open the initial rdb data and map files we have
// . first file is always the merge file (may be empty)
// . returns false on error
bool RdbBase::setFiles ( ) {
	if(!cleanupAnyChrashedMerged())
		return false;

	if(!loadFilesFromDir(m_collectionDirName,false))
		return false;

	if(!loadFilesFromDir(m_mergeDirName,true))
		return false;

	// spiderdb should start with file 0001.dat or 0000.dat
	if ( m_numFiles > 0 && m_fileInfo[0].m_fileId > 1 && m_rdb->getRdbId() == RDB_SPIDERDB ) {
		//isj: is that even true anymore? Ok, crashed merges and lost file0000* are not a
		//good thing but I don't see why it should affect spiderdb especially bad.
		return fixNonfirstSpiderdbFiles();
	}

	// create global index
	generateGlobalIndex();

	// ensure files are sharded correctly
	verifyFileSharding();

	return true;
}


//Clean up any unfinished and unrecoverable merge
//  Because a half-finished mergedir/mergefile.dat can be resumed easily we don't clean
//  up mergedir/*.dat.  Half-copied datadir/mergefile.dat are removed because the
//  copy/move can easily be restarted (and it would be too much effort to restart copying
//  halfway).  Orphaned mergedir/*.map and mergedir/*.idx are removed.  Orphaned data/*.map
//  and data/*.idx are removed.  Missing *.map and *.idx are automatically regenerated.
bool RdbBase::cleanupAnyChrashedMerged() {
	//note: we could submit the unlik() calls to the jobscheduler if we really wanted
	//but since this recovery-cleanup is done during startup I don't see a big problem
	//with waiting for unlink() to finish because the collection will not be ready for
	//use until cleanup has happened.

	log(LOG_DEBUG, "Cleaning up any unfinished merges of %s %s", m_coll, m_dbname);

	//Remove datadir/mergefile.dat
	{
		Dir dir;
		dir.set(m_collectionDirName);
		if(!dir.open())
			return false;
		char pattern[128];
		sprintf(pattern,"%s*",m_dbname);
		while(const char *filename = dir.getNextFilename(pattern)) {
			if(strstr(pattern,".dat")!=NULL) {
				int32_t fileId, fileId2;
				int32_t mergeNum, endMergeFileId;
				if(parseFilename(filename,&fileId,&fileId2,&mergeNum,&endMergeFileId)) {
					if((fileId%2)==0) {
						char fullname[1024];
						sprintf(fullname,"%s/%s",m_collectionDirName,filename);
						log(LOG_DEBUG,"Removing %s", fullname);
						if(unlink(fullname)!=0) {
							g_errno = errno;
							log(LOG_ERROR,"unlink(%s) failed with errno=%d (%s)", fullname, errno, strerror(errno));
							return false;
						}
					}
				}
			}
		}
	}

	//Remove orphaned datadir/*.map and datadir/*.idx
	{
		std::set<int32_t> existingDataDirFileIds;
		Dir dir;
		dir.set(m_collectionDirName);
		if(!dir.open())
			return false;
		char pattern[128];
		sprintf(pattern,"%s*",m_dbname);
		while(const char *filename = dir.getNextFilename(pattern)) {
			if(strstr(filename,".dat")!=NULL) {
				int32_t fileId, fileId2;
				int32_t mergeNum, endMergeFileId;
				if(parseFilename(filename,&fileId,&fileId2,&mergeNum,&endMergeFileId)) {
					existingDataDirFileIds.insert(fileId);
				}
			}
		}
		dir.close();
		log(LOG_DEBUG,"Found %lu .dat files for %s", existingDataDirFileIds.size(), m_dbname);
		if(!dir.open())
			return false;
		while(const char *filename = dir.getNextFilename(pattern)) {
			int32_t fileId, fileId2;
			int32_t mergeNum, endMergeFileId;
			if(parseFilename(filename,&fileId,&fileId2,&mergeNum,&endMergeFileId)) {
				if(existingDataDirFileIds.find(fileId)==existingDataDirFileIds.end() &&  //unseen fileid
				   (strstr(filename,".map")!=NULL || strstr(filename,".idx")!=NULL))     //.map or .idx
				{
					char fullname[1024];
					sprintf(fullname,"%s/%s",m_collectionDirName,filename);
					log(LOG_DEBUG,"Removing %s", fullname);
					if(unlink(fullname)!=0) {
						g_errno = errno;
						log(LOG_ERROR,"unlink(%s) failed with errno=%d (%s)", fullname, errno, strerror(errno));
						return false;
					}
				}
			}
		}
	}
	
	//Remove orphaned mergedir/*.map and mergedir/*.idx
	{
		std::set<int32_t> existingMergeDirFileIds;
		Dir dir;
		dir.set(m_mergeDirName);
		if(!dir.open())
			return false;
		char pattern[128];
		sprintf(pattern,"%s*",m_dbname);
		while(const char *filename = dir.getNextFilename(pattern)) {
			if(strstr(filename,".dat")!=NULL) {
				int32_t fileId, fileId2;
				int32_t mergeNum, endMergeFileId;
				if(parseFilename(filename,&fileId,&fileId2,&mergeNum,&endMergeFileId)) {
					existingMergeDirFileIds.insert(fileId);
				}
			}
		}
		dir.close();
		log(LOG_DEBUG,"Found %lu .dat files for %s in merge-space", existingMergeDirFileIds.size(), m_dbname);
		if(!dir.open())
			return false;
		while(const char *filename = dir.getNextFilename(pattern)) {
			int32_t fileId, fileId2;
			int32_t mergeNum, endMergeFileId;
			if(parseFilename(filename,&fileId,&fileId2,&mergeNum,&endMergeFileId)) {
				if(existingMergeDirFileIds.find(fileId)==existingMergeDirFileIds.end() &&  //unseen fileid
				   (strstr(filename,".map")!=NULL || strstr(filename,".idx")!=NULL))     //.map or .idx
				{
					char fullname[1024];
					sprintf(fullname,"%s/%s",m_mergeDirName,filename);
					log(LOG_DEBUG,"Removing %s", fullname);
					if(unlink(fullname)!=0) {
						g_errno = errno;
						log(LOG_ERROR,"unlink(%s) failed with errno=%d (%s)", fullname, errno, strerror(errno));
						return false;
					}
				}
			}
		}
	}

	log(LOG_DEBUG, "Cleaned up any unfinished merges of %s %s", m_coll, m_dbname);
	return true;
}



bool RdbBase::loadFilesFromDir(const char *dirName, bool isInMergeDir) {
	Dir dir;
	if (!dir.set(dirName))
		return false;

	if (!dir.open()) {
		// we are getting this from a bogus dir
		log(LOG_WARN, "db: Had error opening directory %s", dirName);
		return false;
	}

	// note it
	log(LOG_DEBUG, "db: Loading files for %s coll=%s (%" PRId32") from %s",
	    m_dbname, m_coll, (int32_t)m_collnum, dirName);

	// . set our m_files array
	// . addFile() will return -1 and set g_errno on error
	// . the lower the fileId the older the data 
	//   (but not necessarily the file)
	// . we now put a '*' at end of "*.dat*" since we may be reading in
	//   some headless BigFiles left over froma killed merge
	while (const char *filename = dir.getNextFilename("*.dat*")) {
		// ensure filename starts w/ our m_dbname
		if (strncmp(filename, m_dbname, m_dbnameLen) != 0) {
			continue;
		}

		int32_t fileId;
		int32_t fileId2;
		int32_t mergeNum;
		int32_t endMergeFileId;

		if (!parseFilename(filename, &fileId, &fileId2, &mergeNum, &endMergeFileId)) {
			continue;
		}

		// validation

		// if we are titledb, we got the secondary id
		// . if we are titledb we should have a -xxx after
		// . if none is there it needs to be converted!
		if (m_isTitledb && fileId2 == -1) {
			// critical
			log(LOG_ERROR, "gb: bad title filename of %s. Halting.", filename);
			g_errno = EBADENGINEER;
			return false;
		}

		// don't add if already in there (happens for eg dbname0001.dat.part*)
		if (hasFileId(fileId))
			continue;

		// sometimes an unlink() does not complete properly and we end up with
		// remnant files that are 0 bytes. so let's clean up and skip them
		SafeBuf fullFilename;
		fullFilename.safePrintf("%s/%s", dirName, filename);
		struct stat st;
		if (stat(fullFilename.getBufStart(), &st) != 0) {
			logError("stat(%s) failed with errno=%d (%s)", fullFilename.getBufStart(), errno, strerror(errno));
			return false;
		}

		// zero-sized non-merge files are suspicions and typically indicate data loss. So crahs if they are found
		if (st.st_size == 0 && (fileId&2)!=0) {
			logError("zero sized file found: %s", filename);
			gbshutdownCorrupted();
		}

		if(isInMergeDir)
			log(LOG_WARN,"db: found leftover merge file in merge dir: %s", fullFilename.getBufStart());
		// . put this file into our array of files/maps for this db
		// . MUST be in order of fileId for merging purposes
		// . we assume older files come first so newer can override
		//   in RdbList::merge() routine
		if (addFile(false, fileId, fileId2, mergeNum, endMergeFileId, isInMergeDir) < 0) {
			return false;
		}
	}

	return true;
}


bool RdbBase::fixNonfirstSpiderdbFiles() {
	log( LOG_WARN, "db: missing file id 0001.dat for %s in coll %s. "
	    "Fix this or it'll core later. Just rename the next file "
	    "in line to 0001.dat/map. We probably cored at a "
	    "really bad time during the end of a merge process.",
	    m_dbname, m_coll );

	// do not re-do repair! hmmm
	if ( m_didRepair ) return false;

	// just fix it for them
	BigFile bf;
	SafeBuf oldName;
	oldName.safePrintf("%s%04" PRId32".dat", m_dbname, m_fileInfo[0].m_fileId);
	bf.set ( m_collectionDirName, oldName.getBufStart() );

	// rename it to like "spiderdb.0001.dat"
	SafeBuf newName;
	newName.safePrintf("%s/%s0001.dat",m_collectionDirName,m_dbname);
	bf.rename(newName.getBufStart(),NULL);

	// and delete the old map
	SafeBuf oldMap;
	oldMap.safePrintf("%s/%s0001.map",m_collectionDirName,m_dbname);
	File omf;
	omf.set ( oldMap.getBufStart() );
	omf.unlink();

	// get the map file name we want to move to 0001.map
	BigFile cmf;
	SafeBuf curMap;
	curMap.safePrintf("%s%04" PRId32".map", m_dbname, m_fileInfo[0].m_fileId);
	cmf.set(m_collectionDirName, curMap.getBufStart());

	// rename to spiderdb0081.map to spiderdb0001.map
	cmf.rename(oldMap.getBufStart(), NULL);

	if( m_useIndexFile ) {
		// and delete the old index
		SafeBuf oldIndex;
		oldIndex.safePrintf("%s/%s0001.idx",m_collectionDirName,m_dbname);
		File oif;
		oif.set ( oldIndex.getBufStart() );
		oif.unlink();

		// get the index file name we want to move to 0001.idx
		BigFile cif;
		SafeBuf curIndex;
		curIndex.safePrintf("%s%04" PRId32".idx", m_dbname, m_fileInfo[0].m_fileId);
		cif.set(m_collectionDirName, curIndex.getBufStart());

		// rename to spiderdb0081.map to spiderdb0001.map
		cif.rename(oldIndex.getBufStart(),NULL);
	}

	// replace that first file then
	m_didRepair = true;
	return true;
}


//Generate filename from the total 4 combinations of titledb/not-titledb and normal-file/merging-file
void RdbBase::generateFilename(char *buf, size_t bufsize, int32_t fileId, int32_t fileId2, int32_t mergeNum, int32_t endMergeFileId, const char *extension) {
	if ( mergeNum <= 0 ) {
		if ( m_isTitledb && fileId2>=0 ) {
			snprintf( buf, bufsize, "%s%04d-%03d.%s",
			          m_dbname, fileId, fileId2, extension );
		} else {
			snprintf( buf, bufsize, "%s%04d.%s",
			          m_dbname, fileId, extension );
		}
	} else {
		if ( m_isTitledb && fileId2>=0 ) {
			snprintf( buf, bufsize, "%s%04d-%03d.%03d.%04d.%s",
			          m_dbname, fileId, fileId2, mergeNum, endMergeFileId, extension );
		} else {
			snprintf( buf, bufsize, "%s%04d.%03d.%04d.%s",
			          m_dbname, fileId, mergeNum, endMergeFileId, extension );
		}
	}
}


// return the fileNum we added it to in the array
// return -1 and set g_errno on error
int32_t RdbBase::addFile ( bool isNew, int32_t fileId, int32_t fileId2, int32_t mergeNum, int32_t endMergeFileId, bool isInMergeDir ) {
	// sanity check
	if ( fileId2 < 0 && m_isTitledb )
		gbshutdownLogicError();

	// can't exceed this
	if ( m_numFiles >= MAX_RDB_FILES ) {
		g_errno = ETOOMANYFILES;
		log( LOG_LOGIC, "db: Can not have more than %" PRId32" files. File add failed.", ( int32_t ) MAX_RDB_FILES );
		return -1;
	}

	// set the data file's filename
	char name[1024];
	generateDataFilename(name, sizeof(name), fileId, fileId2, mergeNum, endMergeFileId);

	// HACK: skip to avoid a OOM lockup. if RdbBase cannot dump
	// its data to disk it can backlog everyone and memory will
	// never get freed up.
	ScopedMemoryLimitBypass scopedMemoryLimitBypass;
	BigFile *f;

	const char *dirName = !isInMergeDir ? m_collectionDirName : m_mergeDirName ;

	try {
		f = new (BigFile);
	} catch ( ... ) {
		g_errno = ENOMEM;
		log( LOG_WARN, "RdbBase: new(%i): %s", ( int ) sizeof( BigFile ), mstrerror( g_errno ) );
		return -1;
	}
	mnew( f, sizeof( BigFile ), "RdbBFile" );

	f->set(dirName, name);

	// if new ensure does not exist
	if ( isNew && f->doesExist() ) {
		log( LOG_WARN, "rdb: creating NEW file %s/%s which already exists!", f->getDir(), f->getFilename() );

		if (f->getFileSize() == 0) {
			// this used to move it to trash. but we probably want to know if we have any 0 byte file.
			// so force a core
			logError("zero sized file found");
			gbshutdownCorrupted();
		}

		// nuke it either way
		mdelete ( f , sizeof(BigFile),"RdbBFile");
		delete (f); 

		// we are done. i guess merges will stockpile, that sucks... things will slow down
		return -1;
	}

	RdbMap  *m ;
	try {
		m = new (RdbMap);
	} catch ( ... ) {
		g_errno = ENOMEM;
		log( LOG_WARN, "RdbBase: new(%i): %s", (int)sizeof(RdbMap), mstrerror(g_errno) );
		mdelete ( f , sizeof(BigFile),"RdbBFile");
		delete (f); 
		return -1; 
	}

	mnew ( m , sizeof(RdbMap) , "RdbBMap" );

	RdbIndex *in = NULL;
	if( m_useIndexFile ) {
		try {
			in = new (RdbIndex);
		} catch ( ... ) {
			g_errno = ENOMEM;
			log( LOG_WARN, "RdbBase: new(%i): %s", (int)sizeof(RdbIndex), mstrerror(g_errno) );
			mdelete ( f , sizeof(BigFile),"RdbBFile");
			delete (f); 
			mdelete ( m , sizeof(RdbMap),"RdbBMap");
			delete (m); 
			return -1; 
		}

		mnew ( in , sizeof(RdbIndex) , "RdbBIndex" );
	}

	// reinstate the memory limit
	scopedMemoryLimitBypass.release();

	// debug help
	if ( isNew ) {
		log( LOG_DEBUG, "db: adding new file %s/%s", f->getDir(), f->getFilename() );
	}

	// if not a new file sanity check it
	for ( int32_t j = 0 ; ! isNew && j < f->getMaxParts() - 1 ; j++ ) {
		// might be headless
		File *ff = f->getFile2(j);
		if ( ! ff ) {
			continue;
		}

		if ( ff->getFileSize() == MAX_PART_SIZE ) {
			continue;
		}

		log ( LOG_WARN, "db: File %s %s has length %" PRId64", but it should be %" PRId64". "
		      "You should move it to a temporary directory "
		      "and restart. It probably happened when the power went "
		      "out and a file delete operation failed to complete.",
		      f->getDir(),
		      ff->getFilename() ,
		      (int64_t)ff->getFileSize(),
		      (int64_t)MAX_PART_SIZE);

		gbshutdownCorrupted();
	}

	// set the map file's  filename
	char mapName[1024];
	generateMapFilename(mapName,sizeof(mapName),fileId,fileId2,0,-1);
	m->set(dirName, mapName, m_fixedDataSize, m_useHalfKeys, m_ks, m_pageSize);
	if ( ! isNew && ! m->readMap ( f ) ) {
		// if out of memory, do not try to regen for that
		if ( g_errno == ENOMEM ) {
			return -1;
		}

		g_errno = 0;
		log("db: Could not read map file %s",mapName);

		// if 'gb dump X collname' was called, bail, we do not
		// want to write any data
		if ( g_dumpMode ) {
			return -1;
		}

		log( LOG_INFO, "db: Attempting to generate map file for data file %s* of %" PRId64" bytes. May take a while.",
		     f->getFilename(), f->getFileSize() );

		// this returns false and sets g_errno on error
		if (!m->generateMap(f)) {
			log(LOG_ERROR, "db: Map generation failed.");
			gbshutdownCorrupted();
		}

		log( LOG_INFO, "db: Map generation succeeded." );

		// . save it
		// . if we're an even #'d file a merge will follow
		//   when main.cpp calls attemptMerge()
		log("db: Saving generated map file to disk.");

		// true = alldone
		bool status = m->writeMap( true );
		if ( ! status ) {
			log( LOG_ERROR, "db: Save failed." );
			return -1;
		}
	}

	if (!isNew) {
		log(LOG_DEBUG, "db: Added %s for collnum=%" PRId32" pages=%" PRId32,
		    mapName, (int32_t)m_collnum, m->getNumPages());
	}

	if( m_useIndexFile ) {
		char indexName[1024];

		// set the index file's  filename
		generateIndexFilename(indexName,sizeof(indexName),fileId,fileId2,0,-1);
		in->set(dirName, indexName, m_fixedDataSize, m_useHalfKeys, m_ks, m_rdb->getRdbId(), !isNew);
		if (!isNew && !(in->readIndex() && in->verifyIndex())) {
			// if out of memory, do not try to regen for that
			if (g_errno == ENOMEM) {
				return -1;
			}

			g_errno = 0;
			log(LOG_WARN, "db: Could not read index file %s",indexName);

			// if 'gb dump X collname' was called, bail, we do not want to write any data
			if (g_dumpMode) {
				return -1;
			}

			log(LOG_INFO, "db: Attempting to generate index file for data file %s* of %" PRId64" bytes. May take a while.",
			     f->getFilename(), f->getFileSize() );

			// this returns false and sets g_errno on error
			if (!in->generateIndex(f)) {
				logError("db: Index generation failed for %s.", f->getFilename());
				gbshutdownCorrupted();
			}

			log(LOG_INFO, "db: Index generation succeeded.");

			// . save it
			// . if we're an even #'d file a merge will follow
			//   when main.cpp calls attemptMerge()
			log("db: Saving generated index file to disk.");

			bool status = in->writeIndex(true);
			if ( ! status ) {
				log( LOG_ERROR, "db: Save failed." );
				return -1;
			}
		}

		if (!isNew) {
			log(LOG_DEBUG, "db: Added %s for collnum=%" PRId32" docId count=%" PRIu64,
			    indexName, (int32_t)m_collnum, in->getDocIds()->size());
		}
	}

	if (!isNew) {
		// open this big data file for reading only
		if ( mergeNum < 0 ) {
			f->open(O_RDONLY);
		} else {
			// otherwise, merge will have to be resumed so this file
			// should be writable
			f->open(O_RDWR);
		}
		f->setFlushingIsApplicable();
	}

	// find the position to add so we maintain order by fileId
	int32_t i ;
	for ( i = 0 ; i < m_numFiles ; i++ ) {
		if ( m_fileInfo[i].m_fileId >= fileId ) {
			break;
		}
	}

	// cannot collide here
	if ( i < m_numFiles && m_fileInfo[i].m_fileId == fileId ) {
		log(LOG_LOGIC,"db: addFile: fileId collided.");
		return -1;
	}

	// shift everyone up if we need to fit this file in the middle somewhere
	memmove( m_fileInfo+i+1, m_fileInfo+i, (m_numFiles-i)*sizeof(m_fileInfo[0]));

	// insert this file into position #i
	m_fileInfo[i].m_fileId  = fileId;
	m_fileInfo[i].m_fileId2 = fileId2;
	m_fileInfo[i].m_file    = f;
	m_fileInfo[i].m_map     = m;
	m_fileInfo[i].m_index   = in;
	if(!isInMergeDir) {
		if(fileId&1)
			m_fileInfo[i].m_allowReads = true;
		else
			m_fileInfo[i].m_allowReads = false;
	} else {
		m_fileInfo[i].m_allowReads = false;//until we know for sure it is finished
	}
	m_fileInfo[i].m_pendingGenerateIndex = false;

	// are we resuming a killed merge?
	if ( g_conf.m_readOnlyMode && ((fileId & 0x01)==0) ) {
		log("db: Cannot start in read only mode with an incomplete "
		    "merge, because we might be a temporary cluster and "
		    "the merge might be active.");

		gbshutdownCorrupted();
	}

	// inc # of files we have
	m_numFiles++;

	// if we added a merge file, mark it
	if ( mergeNum >= 0 ) {
		m_mergeStartFileNum = i + 1 ; //merge was starting w/ this file
	}

	return i;
}

int32_t RdbBase::addNewFile(int32_t *fileIdPtr) {
	//No clue about why titledb is different. it just is.
	int32_t id2 = m_isTitledb ? 0 : -1;
	
	ScopedLock sl(m_mtxFileInfo); //a bit heavy-handed but incorporateMerge modifies and may call
	                              //attemptMerge again while the normalt RdbDump calls addFile() too.

	int32_t maxFileId = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		if ( m_fileInfo[i].m_fileId > maxFileId ) {
			int32_t currentFileId = m_fileInfo[i].m_fileId;
			if ( ( currentFileId & 0x01 ) == 0 ) {
				// merge file
				const char* filename = m_fileInfo[i].m_file->getFilename();

				int32_t mergeFileId;
				int32_t mergeFileId2;
				int32_t mergeNum;
				int32_t endMergeFileId;
				if ( parseFilename( filename, &mergeFileId, &mergeFileId2, &mergeNum, &endMergeFileId ) ) {
					maxFileId = endMergeFileId;
				} else {
					// error parsing file, fallback to previous behaviour
					maxFileId = currentFileId;
				}
			} else {
				maxFileId = currentFileId;
			}
		}
	}

	// . we like to keep even #'s for merge file names
	*fileIdPtr = maxFileId + ( ( ( maxFileId & 0x01 ) == 0 ) ? 1 : 2 );

	int32_t rc = addFile( true, *fileIdPtr, id2, -1, -1, false );
	if(rc>=0)
		m_fileInfo[rc].m_allowReads = false; //until we know for sure. See markNewFileReadable()
	return rc;
}


//Mark a newly dumped file as finished
void RdbBase::markNewFileReadable() {
	ScopedLock sl(m_mtxFileInfo);
	if(m_numFiles==0)
		gbshutdownLogicError();
	if(m_fileInfo[m_numFiles-1].m_allowReads)
		gbshutdownLogicError();
	m_fileInfo[m_numFiles-1].m_allowReads = true;
}


bool RdbBase::isManipulatingFiles() const {
	//note: incomplete check but not worse than the original
	ScopedLock sl(const_cast<RdbBase*>(this)->m_mtxJobCount);
	return m_submittingJobs || m_outstandingJobCount!=0;
}


void RdbBase::incrementOutstandingJobs() {
	ScopedLock sl(m_mtxJobCount);
	m_outstandingJobCount++;
	if(m_outstandingJobCount<=0) gbshutdownLogicError();
}

bool RdbBase::decrementOustandingJobs() {
	ScopedLock sl(m_mtxJobCount);
	if(m_outstandingJobCount<=0) gbshutdownLogicError();
	m_outstandingJobCount--;
	return m_outstandingJobCount==0 && !m_submittingJobs;
}

static int32_t getMaxLostPositivesPercentage(rdbid_t rdbId) {
	switch (rdbId) {
		case RDB_POSDB:
		case RDB2_POSDB2:
			return g_conf.m_posdbMaxLostPositivesPercentage;
		case RDB_TAGDB:
		case RDB2_TAGDB2:
			return g_conf.m_tagdbMaxLostPositivesPercentage;
		case RDB_CLUSTERDB:
		case RDB2_CLUSTERDB2:
			return g_conf.m_clusterdbMaxLostPositivesPercentage;
		case RDB_TITLEDB:
		case RDB2_TITLEDB2:
			return g_conf.m_titledbMaxLostPositivesPercentage;
		case RDB_SPIDERDB:
		case RDB2_SPIDERDB2:
			return g_conf.m_spiderdbMaxLostPositivesPercentage;
		case RDB_LINKDB:
		case RDB2_LINKDB2:
			return g_conf.m_linkdbMaxLostPositivesPercentage;
		case RDB_NONE:
		case RDB_END:
		default:
			logError("rdb: bad lookup rdbid of %i", (int)rdbId);
			gbshutdownLogicError();
	}
}

// . called after the merge has successfully completed
// . the final merge file is always file #0 (i.e. "indexdb0000.dat/map")
bool RdbBase::incorporateMerge ( ) {
	// merge source range [a..b), merge target x
	int32_t a = m_mergeStartFileNum;
	int32_t b = std::min(m_mergeStartFileNum + m_numFilesToMerge, m_numFiles);
	int32_t x = a - 1; // file #x is the merged file

	// shouldn't be called if no files merged
	if ( a >= b ) {
		// unless resuming after a merge completed and we exited
		// but forgot to finish renaming the final file!!!!
		log("merge: renaming final file");

		// decrement this count
		if ( m_isMerging ) {
			m_rdb->decrementNumMerges();
		}

		// exit merge mode
		m_isMerging = false;
	}


	// . we can't just unlink the merge file on error anymore
	// . it may have some data that was deleted from the original file
	if ( g_errno ) {
		log( LOG_ERROR, "db: Merge failed for %s, Exiting.", m_dbname);

		// we don't have a recovery system in place, so save state and dump core
		gbshutdownAbort(true);
	}

	// note
	log(LOG_INFO,"db: Writing map %s.", m_fileInfo[x].m_map->getFilename());

	// . ensure we can save the map before deleting other files
	// . sets g_errno and return false on error
	// . allDone = true
	bool status = m_fileInfo[x].m_map->writeMap( true );
	if ( !status ) {
		// unable to write, let's abort
		gbshutdownResourceError();
	}

	if( m_useIndexFile ) {
		status = m_fileInfo[x].m_index->writeIndex(true);
		if ( !status ) {
			// unable to write, let's abort
			log( LOG_ERROR, "db: Could not write index for %s, Exiting.", m_dbname);
			gbshutdownAbort(true);
		}
	}

	// print out info of newly merged file
	int64_t postmergePositiveRecords = m_fileInfo[x].m_map->getNumPositiveRecs();
	int64_t postmergeNegativeRecords = m_fileInfo[x].m_map->getNumNegativeRecs();
	log(LOG_INFO, "merge: Merge succeeded. %s (#%d) has %" PRId64" positive and %" PRId64" negative recs.",
	    m_fileInfo[x].m_file->getFilename(), x, postmergePositiveRecords, postmergeNegativeRecords);
	log(LOG_INFO, "merge: Files had %" PRId64" positive and %" PRId64" negative recs.",
	    m_premergeNumPositiveRecords, m_premergeNumNegativeRecords);

	// there should never be a scenario where we get 0 positive recs
	if (postmergePositiveRecords == 0) {
		logError("Merge ended with 0 positive records.");
		gbshutdownCorrupted();
	}

	// . bitch if bad news
	if ( postmergePositiveRecords > m_premergeNumPositiveRecords ) {
		log(LOG_INFO,"merge: %s gained %" PRId64" positives.", m_dbname, postmergePositiveRecords - m_premergeNumPositiveRecords);
		//note: also seen when resuming an interrupted merge, inwhich case there is probably nothing wrong
	}

	if ( postmergePositiveRecords < m_premergeNumPositiveRecords - m_premergeNumNegativeRecords ) {
		int64_t lostPositive = m_premergeNumPositiveRecords - postmergePositiveRecords;
		double lostPercentage = (lostPositive * 100.00) / m_premergeNumPositiveRecords;

		log(LOG_INFO,"merge: %s: lost %" PRId64" (%.2f%%) positives", m_dbname, lostPositive, lostPercentage);

		int32_t maxLostPercentage = getMaxLostPositivesPercentage(m_rdb->getRdbId());
		if (lostPercentage > maxLostPercentage) {
			log(LOG_ERROR, "merge: %s: lost more than %d of positive records. Aborting.", m_dbname, maxLostPercentage);
			gbshutdownCorrupted();
		}
	}

	if ( postmergeNegativeRecords > m_premergeNumNegativeRecords ) {
		log(LOG_INFO,"merge: %s: gained %" PRId64" negatives.", m_dbname, postmergeNegativeRecords - m_premergeNumNegativeRecords);
	}

	if ( postmergeNegativeRecords < m_premergeNumNegativeRecords - m_premergeNumPositiveRecords ) {
		log(LOG_INFO,"merge: %s: lost %" PRId64" negatives.", m_dbname, m_premergeNumNegativeRecords - postmergeNegativeRecords);
	}

	{
		ScopedLock sl(m_mtxJobCount);
		if(m_outstandingJobCount!=0)
			gbshutdownCorrupted();
	}

	// . before unlinking the files, ensure merged file is the right size!!
	// . this will save us some anguish
	m_fileInfo[x].m_file->invalidateFileSize();

	int64_t fs = m_fileInfo[x].m_file->getFileSize();
	if (fs == 0) {
		// zero sized file?
		logError("zero sized file after merge for file=%s", m_fileInfo[x].m_file->getFilename());
		gbshutdownCorrupted();
	}

	// get file size from map
	int64_t fs2 = m_fileInfo[x].m_map->getFileSize();

	// compare, if only a key off allow that. that is an artificat of
	// generating a map for a file screwed up from a power outage. it
	// will end on a non-key boundary.
	if ( fs != fs2 ) {
		log( LOG_ERROR, "build: Map file size does not agree with actual file "
		    "size for %s. Map says it should be %" PRId64" bytes but it "
		    "is %" PRId64" bytes.",
		    m_fileInfo[x].m_file->getFilename(), fs2 , fs );
		if ( fs2-fs > 12 || fs-fs2 > 12 )
			gbshutdownCorrupted();
		// now print the exception
		log( LOG_WARN, "build: continuing since difference is less than 12 "
		    "bytes. Most likely a discrepancy caused by a power "
		    "outage and the generated map file is off a bit.");
	}

	{
		ScopedLock sl(m_mtxFileInfo);

		//allow/disallow reads while incorporating merged file
		m_fileInfo[x].m_allowReads = true; //newly merge file is finished and valid
		for (int i = a; i < b; i++) {
			m_fileInfo[i].m_allowReads = false; //source files will be deleted shortly
		}

		submitGlobalIndexJob_unlocked(false, -1);
	}

	{
		ScopedLock sl(m_mtxJobCount);
		m_submittingJobs = true;
	}
	
	// on success unlink the files we merged and free them
	for ( int32_t i = a ; i < b && i < m_numFiles; i++ ) {
		// debug msg
		log(LOG_INFO,"merge: Unlinking merged file %s/%s (#%" PRId32").",
		    m_fileInfo[i].m_file->getDir(),m_fileInfo[i].m_file->getFilename(),i);

		// . these links will be done in a thread
		// . they will save the filename before spawning so we can
		//   delete the m_fileInfo[i].m_file now
		if ( ! m_fileInfo[i].m_file->unlink(unlinkDoneWrapper, this) ) {
			incrementOutstandingJobs();
		} else {
			// debug msg
			// MDW this cores if file is bad... if collection got delete from under us i guess!!
			log(LOG_INFO,"merge: Unlinked %s (#%" PRId32").", m_fileInfo[i].m_file->getFilename(), i);
		}

		// debug msg
		log(LOG_INFO,"merge: Unlinking map file %s (#%" PRId32").", m_fileInfo[i].m_map->getFilename(),i);

		if ( ! m_fileInfo[i].m_map->unlink(unlinkDoneWrapper, this) ) {
			incrementOutstandingJobs();
		} else {
			// debug msg
			log(LOG_INFO,"merge: Unlinked %s (#%" PRId32").", m_fileInfo[i].m_map->getFilename(), i);
		}

		if( m_useIndexFile ) {
			log(LOG_INFO,"merge: Unlinking index file %s (#%" PRId32").", m_fileInfo[i].m_index->getFilename(),i);

			if ( ! m_fileInfo[i].m_index->unlink(unlinkDoneWrapper, this) ) {
				incrementOutstandingJobs();
			} else {
				// debug msg
				log(LOG_INFO,"merge: Unlinked %s (#%" PRId32").", m_fileInfo[i].m_index->getFilename(), i);
			}
		}
	}

	if(g_errno) {
		log(LOG_ERROR, "merge: unlinking source files failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}
	
	// wait for the above unlinks to finish before we do this rename
	// otherwise, we might end up doing this rename first and deleting
	// it!
	{
		ScopedLock sl(m_mtxJobCount);
		m_submittingJobs = false;
		if(m_outstandingJobCount!=0)
			return true;
	}

	unlinksDone();
	return true;
}


void RdbBase::unlinkDoneWrapper(void *state) {
	RdbBase *that = static_cast<RdbBase*>(state);
	log("merge: done unlinking file for collnum=%d #outstanding_jobs=%d",
	    (int)that->m_collnum, that->m_outstandingJobCount);
	that->unlinkDone();
}


void RdbBase::unlinkDone() {
	if(g_errno) {
		log(LOG_ERROR, "merge: unlinking source files failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}
	// bail if waiting for more to come back
	if(!decrementOustandingJobs())
		return; //still more to finish
	unlinksDone();
}

void RdbBase::unlinksDone() {
	// debug msg
	log (LOG_INFO,"merge: Done unlinking all files.");

	if(g_errno) {
		log(LOG_ERROR, "merge: unlinking source files failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}

	// merge source range [a..b), merge target x
	int32_t a = m_mergeStartFileNum;
	//int32_t b = std::min(m_mergeStartFileNum + m_numFilesToMerge, m_numFiles);
	int32_t x = a - 1; // file #x is the merged file

	// sanity check
	m_fileInfo[x].m_file->invalidateFileSize();
	int64_t fs = m_fileInfo[x].m_file->getFileSize();
	// get file size from map
	int64_t fs2 = m_fileInfo[x].m_map->getFileSize();
	// compare
	if ( fs != fs2 ) {
		log("build: Map file size does not agree with actual file size");
		gbshutdownCorrupted();
	}

	// . the fileId of the merge file becomes that of the first sourcefile, which happens to be one more than the tmp.merge file
	// . but secondary id should remain the same
	m_fileInfo[x].m_fileId |= 1;

	{
		ScopedLock sl(m_mtxJobCount);
		m_submittingJobs = true;
	}
	
	log(LOG_INFO,"db: Renaming %s of size %" PRId64" to to final filename(s)", m_fileInfo[x].m_file->getFilename(), fs);

	char newMapFilename[1024];
	generateMapFilename(newMapFilename,sizeof(newMapFilename),m_fileInfo[x].m_fileId,m_fileInfo[x].m_fileId2,0,-1);
	if ( ! m_fileInfo[x].m_map->rename(newMapFilename, m_collectionDirName, renameDoneWrapper, this) ) {
		incrementOutstandingJobs();
	} else if(g_errno) {
		log(LOG_ERROR, "merge: renaming file(s) failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}

	if( m_useIndexFile ) {
		char newIndexFilename[1024];
		generateIndexFilename(newIndexFilename,sizeof(newIndexFilename),m_fileInfo[x].m_fileId,m_fileInfo[x].m_fileId2,0,-1);
		if ( ! m_fileInfo[x].m_index->rename(newIndexFilename, m_collectionDirName, renameDoneWrapper, this) ) {
			incrementOutstandingJobs();
		} else if(g_errno) {
			log(LOG_ERROR, "merge: renaming file(s) failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
			gbshutdownAbort(true);
		}
	}

	char newDataName[1024];
	generateDataFilename(newDataName,sizeof(newDataName),m_fileInfo[x].m_fileId,m_fileInfo[x].m_fileId2,0,-1);
	// rename it, this may block
	if ( ! m_fileInfo[x].m_file->rename(newDataName, m_collectionDirName, renameDoneWrapper,this) ) {
		incrementOutstandingJobs();
	} else if(g_errno) {
		log(LOG_ERROR, "merge: renaming file(s) failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}

	{
		ScopedLock sl(m_mtxJobCount);
		m_submittingJobs = false;
		if(m_outstandingJobCount!=0)
			return;
	}
	
	renamesDone();
}


void RdbBase::renameDoneWrapper(void *state) {
	RdbBase *that = static_cast<RdbBase*>(state);
	log(LOG_DEBUG, "rdb: thread completed rename operation for collnum=%d #outstanding_jobs=%d",
	    (int)that->m_collnum, that->m_outstandingJobCount);
	that->renameDone();
}


void RdbBase::checkThreadsAgainWrapper(int /*fd*/, void *state) {
	RdbBase *that = static_cast<RdbBase*>(state);
	g_loop.unregisterSleepCallback ( state,checkThreadsAgainWrapper);
	that->renameDone();
}


void RdbBase::renameDone() {
	if(g_errno) {
		log(LOG_ERROR, "merge: renaming file(s) failed, g_errno=%d (%s)", g_errno, mstrerror(g_errno));
		gbshutdownAbort(true);
	}
	// bail if waiting for more to come back
	if(!decrementOustandingJobs())
		return;
	renamesDone();
}

void RdbBase::renamesDone() {
	// some shorthand variable notation
	int32_t a = m_mergeStartFileNum;
	int32_t b = m_mergeStartFileNum + m_numFilesToMerge;

	//
	// wait for all threads accessing this bigfile to go bye-bye
	//
	log("db: checking for outstanding read threads on unlinked files");
	bool wait = false;
	for ( int32_t i = a ; i < b ; i++ ) {
		BigFile *bf = m_fileInfo[i].m_file;
		if ( g_jobScheduler.is_reading_file(bf) ) wait = true;
	}
	if ( wait ) {
		log("db: waiting for read thread to exit on unlinked file");
		if ( !g_loop.registerSleepCallback( 100, this, checkThreadsAgainWrapper ) ) {
			gbshutdownResourceError();
		}
		return;
	}


	// file #x is the merge file
	// rid ourselves of these files
	{
		ScopedLock sl(m_mtxFileInfo); //lock while manipulating m_fileInfo
		buryFiles(a, b);
		submitGlobalIndexJob_unlocked(false, -1);
	}

	// sanity check
	if ( m_numFilesToMerge != (b-a) ) {
		log(LOG_LOGIC,"db: Bury oops.");
		gbshutdownLogicError();
	}

	// decrement this count
	if ( m_isMerging ) {
		m_rdb->decrementNumMerges();
	}

	// exit merge mode
	m_isMerging = false;
	
	g_merge.mergeIncorporated(this);

	// try to merge more when we are done
	attemptMergeAll();
}

void RdbBase::renameFile( int32_t currentFileIdx, int32_t newFileId, int32_t newFileId2 ) {
	// make a fake file before us that we were merging
	// since it got nuked on disk incorporateMerge();
	char fbuf[256];

	if(m_isTitledb) {
		sprintf(fbuf, "%s%04" PRId32"-%03" PRId32".dat", m_dbname, newFileId, newFileId2);
	} else {
		sprintf(fbuf, "%s%04" PRId32".dat", m_dbname, newFileId);
	}

	log(LOG_INFO, "merge: renaming final merged file %s", fbuf);
	m_fileInfo[currentFileIdx].m_file->rename(fbuf,NULL);

	m_fileInfo[currentFileIdx].m_fileId = newFileId;
	m_fileInfo[currentFileIdx].m_fileId2 = newFileId2;

	// we could potentially have a 'regenerated' map file that has already been moved.
	// eg: merge dies after moving map file, but before moving data files.
	//     next start up, map file will be regenerated. means we now have both even & odd map files
	sprintf(fbuf, "%s%04" PRId32".map", m_dbname, newFileId);
	log(LOG_INFO, "merge: renaming final merged file %s", fbuf);
	m_fileInfo[currentFileIdx].m_map->rename(fbuf);

	if (m_useIndexFile) {
		sprintf(fbuf, "%s%04" PRId32".idx", m_dbname, newFileId);
		log(LOG_INFO, "merge: renaming final merged file %s", fbuf);
		m_fileInfo[currentFileIdx].m_index->rename(fbuf);
	}
}


void RdbBase::buryFiles ( int32_t a , int32_t b ) {
	// on succes unlink the files we merged and free them
	for ( int32_t i = a ; i < b ; i++ ) {
		mdelete ( m_fileInfo[i].m_file , sizeof(BigFile),"RdbBase");
		delete m_fileInfo[i].m_file;
		mdelete ( m_fileInfo[i].m_map , sizeof(RdbMap),"RdbBase");
		delete m_fileInfo[i].m_map;
		mdelete ( m_fileInfo[i].m_index , sizeof(RdbIndex),"RdbBase");
		delete m_fileInfo[i].m_index;
	}
	// bury the merged files
	int32_t n = m_numFiles - b;
	memmove(m_fileInfo+a, m_fileInfo+b, n*sizeof(m_fileInfo[0]));
	// decrement the file count appropriately
	m_numFiles -= (b-a);
	// sanity
	log("rdb: bury files: numFiles now %" PRId32" (b=%" PRId32" a=%" PRId32" collnum=%" PRId32")",
	    m_numFiles,b,a,(int32_t)m_collnum);
}


//Get the min-to-merge configuration for this collection and/or RDB
int32_t RdbBase::getMinToMerge(const CollectionRec *cr, rdbid_t rdbId, int32_t minToMergeOverride) const {
	// always obey the override
	if(minToMergeOverride >= 2) {
		log(LOG_INFO, "merge: Overriding min files to merge of %d with %d", m_minToMerge, minToMergeOverride);
		return minToMergeOverride;
	}

	logTrace(g_conf.m_logTraceRdbBase, "m_minToMergeDefault: %d", m_minToMergeDefault);

	// if m_minToMergeDefault is -1 then we should let cr override, but if m_minToMergeDefault
	// is actually valid at this point, use it as is
	if(m_minToMergeDefault>0) {
		log(LOG_INFO, "merge: Using already-set m_minToMergeDefault of %d for %s", m_minToMergeDefault, m_dbname);
		return m_minToMergeDefault;
	}

	int32_t result = -1;

	// if the collection exist use its values
	if (cr) {
		switch(rdbId) {
			case RDB_POSDB:
				result = cr->m_posdbMinFilesToMerge;
				logTrace(g_conf.m_logTraceRdbBase, "posdb. m_minToMerge: %d", m_minToMerge);
				break;
			case RDB_TITLEDB:
				result = cr->m_titledbMinFilesToMerge;
				logTrace(g_conf.m_logTraceRdbBase, "titledb. m_minToMerge: %d", m_minToMerge);
				break;
			case RDB_SPIDERDB:
				result = cr->m_spiderdbMinFilesToMerge;
				logTrace(g_conf.m_logTraceRdbBase, "spiderdb. m_minToMerge: %d", m_minToMerge);
				break;
			// case RDB_CLUSTERDB:
			//	result = cr->m_clusterdbMinFilesToMerge;
			//	logTrace(g_conf.m_logTraceRdbBase, "clusterdb. m_minToMerge: %d", m_minToMerge);
			//	break;
			case RDB_LINKDB:
				result = cr->m_linkdbMinFilesToMerge;
				logTrace(g_conf.m_logTraceRdbBase, "linkdb. m_minToMerge: %d", m_minToMerge);
				break;
			case RDB_TAGDB:
				result = cr->m_tagdbMinFilesToMerge;
				logTrace(g_conf.m_logTraceRdbBase, "tagdb. m_minToMerge: %d", m_minToMerge);
				break;
			default:
				; //no per-collection override
		}
	}
	log(LOG_INFO, "merge: Using min files to merge %d for %s", result, m_dbname);
	return result;
}

// . the DailyMerge.cpp will set minToMergeOverride for titledb, and this
//   overrides "forceMergeAll" which is the same as setting 
//   "minToMergeOverride" to "2". (i.e. perform a merge if you got 2 or more 
//   files)
// . now return true if we started a merge, false otherwise
bool RdbBase::attemptMerge(int32_t niceness, bool forceMergeAll, int32_t minToMergeOverride) {
	logTrace( g_conf.m_logTraceRdbBase, "BEGIN. minToMergeOverride: %" PRId32, minToMergeOverride);

	// don't do merge if we're in read only mode
	if ( g_conf.m_readOnlyMode ) {
		logTrace( g_conf.m_logTraceRdbBase, "END, in read-only mode" );
		return false;
	}

	// nor if the merge class is halted
	if ( g_merge.isHalted()  ) {
		logTrace( g_conf.m_logTraceRdbBase, "END, is suspended" );
		return false;
	}
	
	// shutting down? do not start another merge then
	if (g_process.isShuttingDown()) {
		logTrace( g_conf.m_logTraceRdbBase, "END, shutting down" );
		return false;
	}
	
	// . wait for all unlinking and renaming activity to flush out
	// . otherwise, a rename or unlink might still be waiting to happen
	//   and it will mess up our merge
	// . right after a merge we get a few of these printed out...
	if(m_outstandingJobCount) {
		log(LOG_INFO,"merge: Waiting for unlink/rename "
		    "operations to finish before attempting merge "
		    "for %s. (collnum=%" PRId32")",m_dbname,(int32_t)m_collnum);
		logTrace( g_conf.m_logTraceRdbBase, "END, wait for unlink/rename" );
		return false;
	}

	if ( forceMergeAll ) m_nextMergeForced = true;

	if ( m_nextMergeForced ) forceMergeAll = true;

	if ( forceMergeAll ) {
		log(LOG_INFO,"merge: forcing merge for %s. (collnum=%" PRId32")",m_dbname,(int32_t)m_collnum);
	}

	rdbid_t rdbId = getIdFromRdb ( m_rdb );

	// if a dump is happening it will always be the last file, do not
	// include it in the merge
	int32_t numFiles = m_numFiles;
	if ( numFiles > 0 && m_rdb->isDumping() ) numFiles--;

	// set m_minToMerge from coll rec if we're indexdb
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	// now see if collection rec is there to override us
	//if ( ! cr ) {
	if ( ! cr ) {
		g_errno = 0;
		log("merge: Could not find coll rec for %s.",m_coll);
	}

	m_minToMerge = getMinToMerge(cr,rdbId,minToMergeOverride);

	// if still -1 that is a problem
	if ( m_minToMerge <= 0 ) {
		log( LOG_WARN, "Got bad minToMerge of %" PRId32" for %s. Set its default to "
		    "something besides -1 in Parms.cpp or add it to "
		    "CollectionRec.h.",
		    m_minToMerge,m_dbname);
		//m_minToMerge = 2;
		gbshutdownLogicError();
	}

	// print it
	log( LOG_INFO, "merge: Considering merging %" PRId32" %s files on disk. %" PRId32" files needed to trigger a merge.",
	     numFiles, m_dbname, m_minToMerge );
	
	if ( g_merge.isMerging() )
	{
		logTrace( g_conf.m_logTraceRdbBase, "END, is merging" );
		return false;
	}

	// bail if already merging THIS class
	if ( m_isMerging ) {
		log(LOG_INFO, "merge: Waiting for other merge to complete before merging %s.", m_dbname);
		logTrace( g_conf.m_logTraceRdbBase, "END, already merging this" );
		return false;
	}

	// are we resuming a killed merge?
	bool resuming = false;
	for ( int32_t j = 0 ; j < numFiles ; j++ ) {
		// if an even-numered file exist then we are resuming a merge
		if((m_fileInfo[j].m_fileId & 0x01) == 0) {
			resuming = true;
			logTrace( g_conf.m_logTraceRdbBase, "Resuming a merge" );
			break;
		}
	}

	if(m_attemptOnlyMergeResumption && !resuming) {
		m_attemptOnlyMergeResumption = false;
		log(LOG_INFO, "merge: No interrupted merge of %s. Won't consider initiating a merge until next call", m_dbname);
		logTrace( g_conf.m_logTraceRdbBase, "END, no interrupted merge" );
		return false;
	}
	//on next call to attempMerge() we are allowed to do normal non-interrupted merges
	m_attemptOnlyMergeResumption = false;

	// this triggers the negative rec concentration and
	// tries to merge on one file...
	if ( ! resuming && m_numFiles <= 1 ) {
		m_nextMergeForced = false;
		logTrace( g_conf.m_logTraceRdbBase, "END, too few files (%" PRId32")", m_numFiles);
		return false;
	}

	// . don't merge if we don't have the min # of files
	// . but skip this check if there is a merge to be resumed from b4
	if ( ! resuming && ! forceMergeAll && numFiles < m_minToMerge ) {
		// now we no longer have to check this collection rdb for
		// merging. this will save a lot of cpu time when we have
		// 20,000+ collections. if we dump a file to disk for it
		// then we set this flag back to false in Rdb.cpp.
		logTrace( g_conf.m_logTraceRdbBase, "END, min files not reached (%" PRId32" / %" PRId32")",numFiles,m_minToMerge);
		return false;
	}

	// remember niceness for calling g_merge.merge()
	m_niceness = niceness;

	// bitch if we got token because there was an error somewhere
	if ( g_errno ) {
		log(LOG_LOGIC,"merge: attemptMerge: %s failed: %s",
		    m_dbname,mstrerror(g_errno));
		g_errno = 0 ;
		log(LOG_LOGIC,"merge: attemptMerge: %s: uh oh...",m_dbname);
		// we don't have the token, so we're fucked...
		return false;
	}

	if ( m_isMerging || g_merge.isMerging() ) {
		logTrace(g_conf.m_logTraceRdbBase, "END, already merging");
		return false;
	}

	// or if # threads out is positive
	if(m_outstandingJobCount!=0) {
		logTrace( g_conf.m_logTraceRdbBase, "END, threads already running" );
		return false;
	}

	// clear for take-off
	// . i used to just merge all the files into 1
	// . but it may be more efficient to merge just enough files as
	//   to put m_numFiles below m_minToMerge
	// . if we have the files : A B C D E F and m_minToMerge is 6
	//   then merge F and E, but if D is < E merged D too, etc...
	// . this merge algorithm is definitely better than merging everything
	//   if we don't do much reading to the db, only writing
	int32_t mergeFileCount = 0;
	int32_t mergeFileId;
	int32_t mergeFileNum;
	bool    foundInterruptedMerge = false;

	logTrace( g_conf.m_logTraceRdbBase, "Checking files" );

	// Detect interrupted merges
	for(int32_t i = 0; i < numFiles; i++) {
		// skip odd numbered files
		if(m_fileInfo[i].m_fileId & 0x01) {
			continue;
		}
		log(LOG_DEBUG,"merge:found interrupted merge file %s", m_fileInfo[i].m_file->getFilename());

		// store the merged data into this fileid and number
		mergeFileId = m_fileInfo[i].m_fileId;
		mergeFileNum = i;

		// files being merged into have a filename like
		// indexdb0000.003.dat where the 003 indicates how many files
		// is is merging in case we have to resume them due to power loss or whatever
		//todo: don't re-parse filename. Just store the count+end in FileInfo structure
		int32_t fileId, fileId2;
		int32_t endMergeFileId;
		if(!parseFilename( m_fileInfo[i].m_file->getFilename(), &fileId, &fileId2, &mergeFileCount, &endMergeFileId )) {
			log(LOG_LOGIC,"merge:attemptMerge:resuming: couln't parse pre-accepted filename of %s", m_fileInfo[i].m_file->getFilename());
			gbshutdownLogicError();
		}

		if(m_isTitledb && fileId2 < 0) { // if titledb we must have a "-023" part now
			log(LOG_LOGIC,"merge:attemptMerge:resuming: unexpected filename for %s coll=%s file=%s",m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());
			gbshutdownCorrupted();
		}

		if(mergeFileCount <= 0) {
			log(LOG_LOGIC,"merge:attemptMerge:resuming: unexpected filename for %s coll=%s file=%s",m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());
			gbshutdownCorrupted();
		}
		if(mergeFileCount == 1) {
			log(LOG_LOGIC,"merge:attemptMerge:resuming: fishy filename for %s coll=%s file=%s",m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());
		}

		if(endMergeFileId <= 0) {
			log(LOG_LOGIC,"merge:attemptMerge:resuming: unexpected filename for %s coll=%s file=%s",m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());
			gbshutdownCorrupted();
		}

		int32_t endMergeFileNum = mergeFileNum;
		for(int32_t j = mergeFileNum+1; j < mergeFileNum+mergeFileCount && j<m_numFiles; j++) {
			if(m_fileInfo[j].m_fileId <= endMergeFileId) {
				endMergeFileNum = j;
			}
		}

		log(LOG_INFO,"merge: Resuming interrupted merge for %s coll=%s, mergefile=%s", m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());

		int32_t currentFilesToMerge = endMergeFileNum - mergeFileNum;
		if(currentFilesToMerge<0)
			gbshutdownLogicError();

		if(currentFilesToMerge <= mergeFileCount) {
			log(LOG_INFO, "merge: Only merging %" PRId32" instead of the original %" PRId32" files.", currentFilesToMerge, mergeFileCount);
		} else if(currentFilesToMerge == mergeFileCount) {
			//excellent
		} else {
			//What? This should not happen. Eg if we have these files:
			//  file0000.002.0007.dat
			//  file0001.dat
			//  file0003.dat
			//  file0005.dat
			//  file0007.dat
			//then somehow extra files came into existence where they shouldn't
			log(LOG_ERROR,"merge:attemptMerge:resuming: found more merge-source files than expected for %s coll=%s file=%s",m_dbname,m_coll,m_fileInfo[i].m_file->getFilename());
			gbshutdownCorrupted();
		}

		// how many files to merge?
		mergeFileCount = currentFilesToMerge;

		foundInterruptedMerge = true;
		break;
	}

	//If there isn't an interrupted merge then we can do a normal new merge
	if(!foundInterruptedMerge) {
		// look at this merge:
		// indexdb0003.dat.part1
		// indexdb0003.dat.part2
		// indexdb0003.dat.part3
		// indexdb0003.dat.part4
		// indexdb0003.dat.part5
		// indexdb0003.dat.part6
		// indexdb0003.dat.part7
		// indexdb0039.dat
		// indexdb0039.dat.part1
		// indexdb0045.dat
		// indexdb0047.dat
		// indexdb0002.002.dat
		// indexdb0002.002.dat.part1
		// it should have merged 45 and 46 since they are so much smaller
		// even though the ratio between 3 and 39 is lower. we did not compute
		// our dtotal correctly...

		// . use greedy method
		// . just merge the minimum # of files to stay under m_minToMerge
		// . files must be consecutive, however
		// . but ALWAYS make sure file i-1 is bigger than file i
		mergeFileCount = numFiles - m_minToMerge + 2 ;

		// titledb should always merge at least 50 files no matter what though
		// cuz i don't want it merging its huge root file and just one
		// other file... i've seen that happen... but don't know why it didn't
		// merge two small files! i guess because the root file was the
		// oldest file! (38.80 days old)???
		if ( m_isTitledb && mergeFileCount < 50 && m_minToMerge > 200 ) {
			// force it to 50 files to merge
			mergeFileCount = 50;

			// but must not exceed numFiles!
			if ( mergeFileCount > numFiles ) {
				mergeFileCount = numFiles;
			}
		}

		if ( mergeFileCount > absoluteMaxFilesToMerge ) {
			mergeFileCount = absoluteMaxFilesToMerge;
		}

		// but if we are forcing then merge ALL, except one being dumped
		if ( m_nextMergeForced ) {
			mergeFileCount = numFiles;
		}

		int32_t mini;
		selectFilesToMerge(mergeFileCount,numFiles,&mini);

		// if no valid range, bail
		if ( mini == -1 ) { 
			log(LOG_LOGIC,"merge: gotTokenForMerge: Bad engineer. mini is -1.");
			return false; 
		}
		// . merge from file #mini through file #(mini+n)
		// . these files should all have ODD fileIds so we can sneak a new
		//   mergeFileId in there
		mergeFileId = m_fileInfo[mini].m_fileId - 1;

		// get new id, -1 on error
		int32_t      fileId2;
		fileId2 = m_isTitledb ? 0 : -1;

		// . make a filename for the merge
		// . always starts with file #0
		// . merge targets are named like "indexdb0000.002.dat"
		// . for titledb is "titledb0000-023.dat.003" (23 is id2)
		// . this now also sets m_mergeStartFileNum for us... but we override
		//   below anyway. we have to set it there in case we startup and
		//   are resuming a merge.
		int32_t endMergeFileNum = mini + mergeFileCount - 1;
		int32_t endMergeFileId = m_fileInfo[endMergeFileNum].m_fileId;
		log( LOG_INFO, "merge: mergeFileCount=%d mini=%d mergeFileId=%d endMergeFileNum=%d endMergeFileId=%d",
		     mergeFileCount, mini, mergeFileId, endMergeFileNum, endMergeFileId );

		{
			//The lock of m_mtxFileInfo is delayed until now because the previous accesses were reads only, but
			// we must hold the mutex while colling addFile() which modifies the array.
			ScopedLock sl(m_mtxFileInfo);
			mergeFileNum = addFile ( true, mergeFileId , fileId2, mergeFileCount, endMergeFileId, true );
			if (mergeFileNum >= 0) {
				submitGlobalIndexJob_unlocked(false, -1);
			}
		}

		if ( mergeFileNum < 0 ) {
			log(LOG_LOGIC,"merge: attemptMerge: Could not add new file."); 
			g_errno = 0;
			return false; 
		}

		// is it a force?
		if ( m_nextMergeForced ) {
			log(LOG_INFO, "merge: Force merging all %s files, except those being dumped now.", m_dbname);
		}
		// clear after each call to attemptMerge()
		m_nextMergeForced = false;
		
		// sanity check
		if ( mergeFileCount <= 1 ) {
			log(LOG_LOGIC,"merge: attemptMerge: Not merging %" PRId32" files.", mergeFileCount);
			return false; 
		}
	}
	

	// . save the start number and the count of files we're merging
	m_mergeStartFileNum = mergeFileNum + 1;
	m_numFilesToMerge   = mergeFileCount;

	const char *coll = cr ? cr->m_coll : "";

	// log merge parms
	log(LOG_INFO,"merge: Merging %" PRId32" %s files to file id %" PRId32" now. "
	    "collnum=%" PRId32" coll=%s",
	    mergeFileCount,m_dbname,mergeFileId,(int32_t)m_collnum,coll);

	// print out file info
	m_premergeNumPositiveRecords = 0;
	m_premergeNumNegativeRecords = 0;
	for ( int32_t i = m_mergeStartFileNum; i < m_mergeStartFileNum + m_numFilesToMerge; ++i ) {
		m_premergeNumPositiveRecords += m_fileInfo[i].m_map->getNumPositiveRecs();		
		m_premergeNumNegativeRecords += m_fileInfo[i].m_map->getNumNegativeRecs();
		log(LOG_INFO,"merge: %s (#%" PRId32") has %" PRId64" positive "
		     "and %" PRId64" negative records." ,
		     m_fileInfo[i].m_file->getFilename(),
		     i , 
		     m_fileInfo[i].m_map->getNumPositiveRecs(),
		     m_fileInfo[i].m_map->getNumNegativeRecs() );
	}
	log(LOG_INFO,"merge: Total positive = %" PRId64" Total negative = %" PRId64".",
	    m_premergeNumPositiveRecords,m_premergeNumNegativeRecords);

	// assume we are now officially merging
	m_isMerging = true;

	m_rdb->incrementNumMerges();

	logTrace( g_conf.m_logTraceRdbBase, "merge!" );
	// . start the merge
	// . returns false if blocked, true otherwise & sets g_errno
	if (!g_merge.merge(rdbId,
	                   m_collnum,
	                   m_fileInfo[mergeFileNum].m_file,
	                   m_fileInfo[mergeFileNum].m_map,
	                   m_fileInfo[mergeFileNum].m_index,
	                   m_mergeStartFileNum,
	                   m_numFilesToMerge,
	                   m_niceness)) {
		// we started the merge so return true here
		logTrace( g_conf.m_logTraceRdbBase, "END, started OK" );
		return true;
	}
	
	// hey, we're no longer merging i guess
	m_isMerging = false;
	// decerment this count
	m_rdb->decrementNumMerges();

	// bitch on g_errno then clear it
	if ( g_errno ) {
		log( LOG_WARN, "merge: Had error getting merge token for %s: %s.", m_dbname, mstrerror( g_errno ) );
	}
	g_errno = 0;

	log("merge: did not block for some reason.");
	logTrace( g_conf.m_logTraceRdbBase, "END" );
	return true;
}


void RdbBase::selectFilesToMerge(int32_t mergeFileCount, int32_t numFiles, int32_t *p_mini) {
	float minr = 99999999999.0;
	int64_t mint = 0x7fffffffffffffffLL ;
	int32_t mini = -1;
	bool minOld = false;
	int32_t nowLocal = getTimeLocal();
	for(int32_t i = 0; i + mergeFileCount <= numFiles; i++) {
		//Consider the filees [i..i+mergeFileCount)

		//if any of the files in the range are makred unreadable then skip that range.
		//This should only happen for the last range while a new file is being dumped
		bool anyUnreadableFiles = false;
		for (int32_t j = i; j < i + mergeFileCount; j++) {
			if (!m_fileInfo[j].m_allowReads) {
				anyUnreadableFiles = true;
				break;
			}
		}
		if (anyUnreadableFiles) {
			log(LOG_DEBUG,"merge: file range [%d..%d] contains unreadable files", i, i+mergeFileCount-1);
			continue;
		}

		// oldest file
		time_t date = -1;
		// add up the string
		int64_t total = 0;
		for(int32_t j = i; j < i + mergeFileCount; j++) {
			total += m_fileInfo[j].m_file->getFileSize();
			time_t mtime = m_fileInfo[j].m_file->getLastModifiedTime();
			// skip on error
			if(mtime < 0) {
				continue;
			}

			if(mtime > date) {
				date = mtime;
			}
		}

		// does it have a file more than 30 days old?
		bool old = ( date < nowLocal - 30*24*3600 );

		// not old if error (date will be -1)
		if(date < 0) {
			old = false;
		}

		// if it does, and current winner does not, force ourselves!
		if(old && ! minOld) {
			mint = 0x7fffffffffffffffLL ;
		}

		// and if we are not old and the min is, do not consider
		if(!old && minOld) {
			continue;
		}

		// if merging titledb, just pick by the lowest total
		if(m_isTitledb) {
			if(total < mint) {
				mini   = i;
				mint   = total;
				minOld = old;
				log(LOG_INFO,"merge: titledb i=%" PRId32" mergeFileCount=%" PRId32" "
				    "mint=%" PRId64" mini=%" PRId32" "
				    "oldestfile=%.02fdays",
				    i,mergeFileCount,mint,mini,
				    ((float)nowLocal-date)/(24*3600.0) );
			}
			continue;
		}

		// . get the average ratio between mergees
		// . ratio in [1.0,inf)
		// . prefer the lowest average ratio
		double ratio = 0.0;
		for(int32_t j = i; j < i + mergeFileCount - 1; j++) {
			int64_t s1 = m_fileInfo[j  ].m_file->getFileSize();
			int64_t s2 = m_fileInfo[j+1].m_file->getFileSize();
			int64_t tmp;
			if(s2 == 0 ) continue;
			if(s1 < s2) { tmp = s1; s1 = s2 ; s2 = tmp; }
			ratio += (double)s1 / (double)s2 ;
		}
		if(mergeFileCount >= 2 ) ratio /= (double)(mergeFileCount-1);
		// sanity check
		if(ratio < 0.0) {
			logf(LOG_LOGIC,"merge: ratio is negative %.02f",ratio);
			gbshutdownLogicError();
		}

		// the adjusted ratio
		double adjratio = ratio;
		// . adjust ratio based on file size of current winner
		// . if winner is ratio of 1:1 and we are 10:1 but winner
		//   is 10 times bigger than us, then we have a tie.
		// . i think if we are 10:1 and winner is 3 times bigger
		//   we should have a tie
		if(mini >= 0 && total > 0 && mint > 0) {
			double sratio = (double)total/(double)mint;
			//if(mint>total ) sratio = (float)mint/(float)total;
			//else              sratio = (float)total/(float)mint;
			adjratio *= sratio;
		}


		// debug the merge selection
		int64_t prevSize = 0;
		if(i > 0)
			prevSize = m_fileInfo[i-1].m_file->getFileSize();
		log(LOG_INFO,"merge: i=%" PRId32" n=%" PRId32" ratio=%.2f adjratio=%.2f "
		    "minr=%.2f mint=%" PRId64" mini=%" PRId32" prevFileSize=%" PRId64" "
		    "mergeFileSize=%" PRId64" oldestfile=%.02fdays "
		    "collnum=%" PRId32,
		    i,mergeFileCount,ratio,adjratio,minr,mint,mini,
		    prevSize , total,
		    ((float)nowLocal-date)/(24*3600.0) ,
		    (int32_t)m_collnum);

		// bring back the greedy merge
		if(total >= mint) {
			continue;
		}

		// . don't get TOO lopsided on me now
		// . allow it for now! this is the true greedy method... no!
		// . an older small file can be cut off early on by a merge
		//   of middle files. the little guy can end up never getting
		//   merged unless we have this.
		// . allow a file to be 4x bigger than the one before it, this
		//   allows a little bit of lopsidedness.
		if(i > 0  && m_fileInfo[i-1].m_file->getFileSize() < total/4) {
			continue;
		}

		//min  = total;
		minr   = ratio;
		mint   = total;
		mini   = i;
		minOld = old;
	}

	*p_mini = mini;
}


// . use the maps and tree to estimate the size of this list w/o hitting disk
// . used by Indexdb.cpp to get the size of a list for IDF weighting purposes
int64_t RdbBase::estimateListSize(const char *startKey, const char *endKey, char *maxKey,
			          int64_t oldTruncationLimit) const {
	// . reset this to low points
	// . this is on
	KEYSET(maxKey,endKey,m_ks);
	bool first = true;
	// do some looping
	char newGuy[MAX_KEY_BYTES];
	int64_t totalBytes = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// the start and end pages for a page range
		int32_t pg1 , pg2;
		// get the start and end pages for this startKey/endKey
		m_fileInfo[i].m_map->getPageRange(startKey,
						endKey,
						&pg1,
						&pg2,
						newGuy,
						oldTruncationLimit);
		// . get the range size add it to count
		// . some of these records are negative recs (deletes) so
		//   our count may be small
		// . also, count may be a bit small because getRecSizes() may
		//   not recognize some recs on the page boundaries as being
		//   in [startKey,endKey]
		// . this can now return negative sizes
		// . the "true" means to subtract page sizes that begin with
		//   delete keys (the key's low bit is clear)
		// . get the minKey and maxKey in this range
		// . minKey2 may be bigger than the actual minKey for this
		//   range, likewise, maxKey2 may be smaller than the actual
		//   maxKey, but should be good estimates
		int64_t maxBytes = m_fileInfo[i].m_map->getMaxRecSizes ( pg1, pg2, startKey, endKey, true );//subtrct

		// get the min as well
		int64_t minBytes = m_fileInfo[i].m_map->getMinRecSizes ( pg1, pg2, startKey, endKey, true );//subtrct

		int64_t avg = (maxBytes + minBytes) / 2LL;

		// use that
		totalBytes += avg;

		// if not too many pages then don't even bother setting "maxKey"
		// since it is used only for interpolating if this term is
		// truncated. if only a few pages then it might be way too
		// small.
		if ( pg1 + 5 > pg2 ) continue;
		// replace *maxKey automatically if this is our first time
		if(first) { 
			KEYSET(maxKey,newGuy,m_ks);
			first = false;
			continue;
		}
		// . get the SMALLEST max key
		// . this is used for estimating what the size of the list
		//   would be without truncation
		if(KEYCMP(newGuy,maxKey,m_ks)>0)
			KEYSET(maxKey,newGuy,m_ks);
	}

	// TODO: now get from the btree!
	// before getting from the map (on disk IndexLists) get upper bound
	// from the in memory b-tree
	//int32_t n=getTree()->getListSize (startKey, endKey, &minKey2, &maxKey2);
	int64_t n;
	if(m_tree)
		n = m_tree->estimateListSize(m_collnum, startKey, endKey, NULL, NULL);
	else
		n = m_buckets->estimateListSize(m_collnum, startKey, endKey, NULL, NULL);

	totalBytes += n;
	// ensure totalBytes >= 0
	if ( totalBytes < 0 ) totalBytes = 0;

	return totalBytes;
}

int64_t RdbBase::estimateNumGlobalRecs() const {
	return getNumTotalRecs() * g_hostdb.m_numShards;
}

// . return number of positive records - negative records
int64_t RdbBase::getNumTotalRecs() const {
	int64_t numPositiveRecs = 0;
	int64_t numNegativeRecs = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// skip even #'d files -- those are merge files
		if ( (m_fileInfo[i].m_fileId & 0x01) == 0 ) continue;
		numPositiveRecs += m_fileInfo[i].m_map->getNumPositiveRecs();
		numNegativeRecs += m_fileInfo[i].m_map->getNumNegativeRecs();
	}
	// . add in the btree
	// . TODO: count negative and positive recs in the b-tree
	// . assume all positive for now
	// . for now let Rdb add the tree in RdbBase::getNumTotalRecs()
	if(m_tree) {
		numPositiveRecs += m_tree->getNumPositiveKeys(m_collnum);
		numNegativeRecs += m_tree->getNumNegativeKeys(m_collnum);
	}
	else {
		// i've seen this happen when adding a new coll i guess
		if ( ! m_buckets ) return 0;

		//these routines are slow because they count every time.
		numPositiveRecs += m_buckets->getNumKeys(m_collnum);
	}
	return numPositiveRecs - numNegativeRecs;
}

// . how much mem is allocated for all of our maps?
// . we have one map per file
int64_t RdbBase::getMapMemAllocated() const {
	int64_t allocated = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		allocated += m_fileInfo[i].m_map->getMemAllocated();
	return allocated;
}

int32_t RdbBase::getNumFiles() const {
	ScopedLock sl(m_mtxFileInfo);
	return m_numFiles;
}

// sum of all parts of all big files
int32_t RdbBase::getNumSmallFiles() const {
	int32_t count = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		count += m_fileInfo[i].m_file->getNumParts();
	return count;
}

int64_t RdbBase::getDiskSpaceUsed() const {
	int64_t count = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		count += m_fileInfo[i].m_file->getFileSize();
	return count;
}


//Calculate how much space will be needed for merging files [startFileNum .. startFileNum+numFiles)
//The estimate is an upper bound.
uint64_t RdbBase::getSpaceNeededForMerge(int startFileNum, int numFiles) const {
	//The "upper bound" is implicitly true. Due to internal fragmenation in the file system we will
	//likely use a fewer blocks/segments than the original files. It can be wrong if the target
	//file system  uses blocks/sectors/segments/extends much larger than the source file system.
	uint64_t total = 0;
	for(int i=0; i<startFileNum+numFiles && i<m_numFiles; i++)
		total += m_fileInfo[i].m_file->getFileSize();
	return total;
}

void RdbBase::saveMaps() {
	logTrace(g_conf.m_logTraceRdbBase, "BEGIN");

	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		if ( ! m_fileInfo[i].m_map ) {
			log("base: map for file #%i is null", i);
			continue;
		}

		bool status = m_fileInfo[i].m_map->writeMap ( false );
		if ( !status ) {
			// unable to write, let's abort
			gbshutdownResourceError();
		}
	}

	logTrace(g_conf.m_logTraceRdbBase, "END");
}

void RdbBase::saveTreeIndex() {
	logTrace(g_conf.m_logTraceRdbBase, "BEGIN");

	if (!m_useIndexFile) {
		logTrace(g_conf.m_logTraceRdbBase, "END. useIndexFile disabled");
		return;
	}

	if (!m_treeIndex.writeIndex(false)) {
		// unable to write, let's abort
		gbshutdownResourceError();
	}

	logTrace(g_conf.m_logTraceRdbBase, "END");
}

void RdbBase::saveIndexes() {
	logTrace(g_conf.m_logTraceRdbBase, "BEGIN");

	if (!m_useIndexFile) {
		return;
	}

	for (int32_t i = 0; i < m_numFiles; i++) {
		if (!m_fileInfo[i].m_index) {
			log(LOG_WARN, "base: index for file #%i is null", i);
			continue;
		}

		if ((m_fileInfo[i].m_fileId & 0x01) == 0) {
			// don't write index for files that are merging
			continue;
		}

		if (!m_fileInfo[i].m_index->writeIndex(true)) {
			// unable to write, let's abort
			gbshutdownResourceError();
		}
	}
	logTrace(g_conf.m_logTraceRdbBase, "END");
}

bool RdbBase::verifyFileSharding ( ) {
	// if swapping in from CollectionRec::getBase() then do
	// not re-verify file sharding! only do at startup
	if ( g_loop.m_isDoingLoop ) return true;

	// skip for now to speed up startup
	static int32_t s_count = 0;
	s_count++;
	if ( s_count == 50 )
		log(LOG_WARN, "db: skipping shard verification for remaining files");
	if ( s_count >= 50 ) 
		return true;

	Msg5 msg5;
	RdbList list;
	int32_t minRecSizes = 64000;
	rdbid_t rdbId = m_rdb->getRdbId();
	if ( rdbId == RDB_TITLEDB ) minRecSizes = 640000;
	
	log ( LOG_DEBUG, "db: Verifying shard parity for %s of %" PRId32" bytes for coll %s (collnum=%" PRId32")...",
	      m_dbname , minRecSizes, m_coll , (int32_t)m_collnum );

	if ( ! msg5.getList ( m_rdb->getRdbId(),
			      m_collnum       ,
			      &list         ,
			      KEYMIN()      ,
			      KEYMAX()      ,
			      minRecSizes   ,
			      true          , // includeTree   ,
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cachekey
			      0             , // retryNum
			      -1            , // maxRetries
			      -1LL          , // syncPint
			      true          , // isRealMerge
			      true)) {        // allowPageCache
		log( LOG_DEBUG, "db: HEY! it did not block");
		return false;
	}

	int32_t count = 0;
	int32_t got   = 0;
	int32_t printed = 0;
	char k[MAX_KEY_BYTES];

	for ( list.resetListPtr() ; ! list.isExhausted() ; list.skipCurrentRecord() ) {
		list.getCurrentKey(k);

		// skip negative keys
		if (KEYNEG(k)) {
			continue;
		}

		count++;
		uint32_t shardNum = getShardNum( rdbId , k );

		if ( shardNum == getMyShardNum() ) {
			got++;
			continue;
		}

		if ( ++printed > 100 ) continue;

		// avoid log spam... comment this out. nah print out 1st 100.
		log ( "db: Found bad key in list belongs to shard %" PRId32, shardNum);
	}

	if ( got == count ) {
		return true;
	}

	// tally it up
	g_rebalance.m_numForeignRecs += count - got;
	log ( LOG_INFO, "db: Out of first %" PRId32" records in %s for %s.%" PRId32", only %" PRId32" belong "
	      "to our group.",count,m_dbname,m_coll,(int32_t)m_collnum,got);

	//log ( "db: Exiting due to Posdb inconsistency." );
	return true;//g_conf.m_bypassValidation;
}

bool RdbBase::initializeGlobalIndexThread() {
	return m_globalIndexThreadQueue.initialize(generateGlobalIndex, "generate-index");
}

void RdbBase::finalizeGlobalIndexThread() {
	m_globalIndexThreadQueue.finalize();
}

docids_ptr_t RdbBase::prepareGlobalIndexJob(bool markFileReadable, int32_t fileId) {
	ScopedLock sl(m_mtxFileInfo);
	return prepareGlobalIndexJob_unlocked(markFileReadable, fileId);
}

docids_ptr_t RdbBase::prepareGlobalIndexJob_unlocked(bool markFileReadable, int32_t fileId) {
	docids_ptr_t tmpDocIdFileIndex(new docids_t);

	// global index does not include RdbIndex from tree/buckets
	for (int32_t i = 0; i < m_numFiles; i++) {
		if (markFileReadable && m_fileInfo[i].m_fileId == fileId) {
			m_fileInfo[i].m_pendingGenerateIndex = true;
		}

		if(m_fileInfo[i].m_allowReads || m_fileInfo[i].m_pendingGenerateIndex) {
			auto docIds = m_fileInfo[i].m_index->getDocIds();
			tmpDocIdFileIndex->reserve(tmpDocIdFileIndex->size() + docIds->size());
			std::transform(docIds->begin(), docIds->end(), std::back_inserter(*tmpDocIdFileIndex),
			               [i](uint64_t docId) {
			                   return ((docId << s_docIdFileIndex_docIdOffset) | i); // docId has delete key
			               });
		}
	}

	return tmpDocIdFileIndex;
}

void RdbBase::submitGlobalIndexJob(bool markFileReadable, int32_t fileId) {
	if (!m_useIndexFile) {
		return;
	}

	ThreadQueueItem *item = new ThreadQueueItem(this, prepareGlobalIndexJob(markFileReadable, fileId), markFileReadable, fileId);
	m_globalIndexThreadQueue.addItem(item);

	log(LOG_INFO, "db: Submitted job %p to generate global index for %s", item, m_rdb->getDbname());
}

void RdbBase::submitGlobalIndexJob_unlocked(bool markFileReadable, int32_t fileId) {
	if (!m_useIndexFile) {
		return;
	}

	ThreadQueueItem *item = new ThreadQueueItem(this, prepareGlobalIndexJob_unlocked(markFileReadable, fileId), markFileReadable, fileId);
	m_globalIndexThreadQueue.addItem(item);

	log(LOG_INFO, "db: Submitted job %p to generate global index for %s", item, m_rdb->getDbname());
}

bool RdbBase::hasPendingGlobalIndexJob() {
	if (!m_useIndexFile) {
		return false;
	}

	return !m_globalIndexThreadQueue.isEmpty();
}

void RdbBase::generateGlobalIndex(void *item) {
	ThreadQueueItem *queueItem = static_cast<ThreadQueueItem*>(item);

	log(LOG_INFO, "db: Processing job %p to generate global index", item);

	std::stable_sort(queueItem->m_docIdFileIndex->begin(), queueItem->m_docIdFileIndex->end(),
	                 [](uint64_t a, uint64_t b) {
		                 return (a & s_docIdFileIndex_docIdMask) < (b & s_docIdFileIndex_docIdMask);
	                 });

	// in reverse because we want to keep the highest file position
	auto it = std::unique(queueItem->m_docIdFileIndex->rbegin(), queueItem->m_docIdFileIndex->rend(),
	                      [](uint64_t a, uint64_t b) {
		                      return (a & s_docIdFileIndex_docIdMask) == (b & s_docIdFileIndex_docIdMask);
	                      });
	queueItem->m_docIdFileIndex->erase(queueItem->m_docIdFileIndex->begin(), it.base());

	// free up used space
	queueItem->m_docIdFileIndex->shrink_to_fit();

	// replace with new index
	ScopedLock sl(queueItem->m_base->m_mtxFileInfo);
	ScopedLock sl2(queueItem->m_base->m_docIdFileIndexMtx);
	queueItem->m_base->m_docIdFileIndex.swap(queueItem->m_docIdFileIndex);

	if (queueItem->m_markFileReadable) {
		for (auto i = 0; i < queueItem->m_base->m_numFiles; ++i) {
			if (queueItem->m_base->m_fileInfo[i].m_fileId == queueItem->m_fileId) {
				queueItem->m_base->m_fileInfo[i].m_allowReads = true;
				queueItem->m_base->m_fileInfo[i].m_pendingGenerateIndex = false;
				break;
			}
		}
	}

	log(LOG_INFO, "db: Processed job %p to generate global index", item);

	delete queueItem;
}

/// @todo ALC we should free up m_fileInfo[i].m_index->m_docIds when we don't need it, and load it back when we do
void RdbBase::generateGlobalIndex() {
	if (!m_useIndexFile) {
		return;
	}

	log(LOG_INFO, "db: Generating global index for %s", m_rdb->getDbname());

	docids_ptr_t tmpDocIdFileIndex(new docids_t);

	ScopedLock sl(m_mtxFileInfo);
	// global index does not include RdbIndex from tree/buckets
	for (int32_t i = 0; i < m_numFiles; i++) {
		if(!m_fileInfo[i].m_allowReads) {
			continue;
		}

		auto docIds = m_fileInfo[i].m_index->getDocIds();
		tmpDocIdFileIndex->reserve(tmpDocIdFileIndex->size() + docIds->size());
		std::transform(docIds->begin(), docIds->end(), std::back_inserter(*tmpDocIdFileIndex),
		               [i](uint64_t docId) {
			               return ((docId << s_docIdFileIndex_docIdOffset) | i); // docId has delete key
		               });
	}
	sl.unlock();

	std::stable_sort(tmpDocIdFileIndex->begin(), tmpDocIdFileIndex->end(),
	          [](uint64_t a, uint64_t b) {
		          return (a & s_docIdFileIndex_docIdMask) < (b & s_docIdFileIndex_docIdMask);
	          });

	// in reverse because we want to keep the highest file position
	auto it = std::unique(tmpDocIdFileIndex->rbegin(), tmpDocIdFileIndex->rend(),
	                      [](uint64_t a, uint64_t b) {
		                      return (a & s_docIdFileIndex_docIdMask) == (b & s_docIdFileIndex_docIdMask);
	                      });
	tmpDocIdFileIndex->erase(tmpDocIdFileIndex->begin(), it.base());

	// free up used space
	tmpDocIdFileIndex->shrink_to_fit();

	// replace with new index
	ScopedLock sl2(m_docIdFileIndexMtx);
	m_docIdFileIndex.swap(tmpDocIdFileIndex);
}

void RdbBase::printGlobalIndex() {
	logf(LOG_TRACE, "db: global index");

	auto globalIndex = getGlobalIndex();
	for (auto key : *globalIndex) {
		logf(LOG_TRACE, "db: docId=%" PRId64" index=%" PRId64" isDel=%d key=%" PRIx64,
		     key >> RdbBase::s_docIdFileIndex_docIdDelKeyOffset,
		     key & RdbBase::s_docIdFileIndex_filePosMask,
		     ((key & RdbBase::s_docIdFileIndex_delBitMask) == 0),
		     key);
	}
}

docidsconst_ptr_t RdbBase::getGlobalIndex() {
	ScopedLock sl(m_docIdFileIndexMtx);
	return m_docIdFileIndex;
}
