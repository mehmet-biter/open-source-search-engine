// JAB: this is required for pwrite() in this module
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "BigFile.h"
#include "Dir.h"
#include "JobScheduler.h"
#include "Stats.h"
#include "Statsdb.h"
#include "Sanity.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include <new>
#include <vector>
#include <pthread.h>

// main.cpp will wait for this to be zero before exiting so all unlink/renames
// can complete
int32_t g_unlinkRenameThreads = 0;

static int64_t g_lastDiskReadCompleted = 0LL;

static void readwriteWrapper_r  ( void *state );

static void  doneWrapper        ( void *state, job_exit_t exit_type );
static bool  readwrite_r        ( FileState *fstate );


static void updateDiskReadCompleted() {
	//Small function for easier suppression in debug build and running with helgrind or similar
	//Original comment: this is thread safe...
	//Technically it isn't but the variable isn't normally torn between cache lines and most modern architectures handles this fine.
	g_lastDiskReadCompleted = gettimeofdayInMilliseconds();
}


//A set (list in this case) of filenames that we intend to unlink or rename (src name).
//it is needed for preventing queued read operations from working on deleted files.
struct UnlinkFilename {
	char filename[1024];
};
static std::vector<UnlinkFilename> s_pendingFileMetaOperations;
static GbMutex s_pending_mtx;



static bool isPendingUnlink(const char *filename) {
	ScopedLock sl(s_pending_mtx);
	for(std::vector<UnlinkFilename>::const_iterator iter=s_pendingFileMetaOperations.begin(); iter!=s_pendingFileMetaOperations.end(); ++iter) {
		if(strcmp(iter->filename,filename)==0)
			return true;
	}
	return false;
}

static void addPendingUnlink(const char *filename) {
	ScopedLock sl(s_pending_mtx);
	//we cannot have two simultaenous operations on a file
	for(std::vector<UnlinkFilename>::const_iterator iter=s_pendingFileMetaOperations.begin(); iter!=s_pendingFileMetaOperations.end(); ++iter) {
		if(strcmp(iter->filename,filename)==0)
			gbshutdownLogicError();
	}
	UnlinkFilename ruf;
	strcpy(ruf.filename,filename);
	s_pendingFileMetaOperations.push_back(ruf);
}

static void removePendingUnlink(const char *filename) {
	ScopedLock sl(s_pending_mtx);
	//double-remove is allowed.
	for(std::vector<UnlinkFilename>::iterator iter=s_pendingFileMetaOperations.begin(); iter!=s_pendingFileMetaOperations.end(); ++iter) {
		if(strcmp(iter->filename,filename)==0) {
			s_pendingFileMetaOperations.erase(iter);
			return;
		}
	}
}



BigFile::~BigFile () {
	close();
}

//#define O_DIRECT 040000

BigFile::BigFile () {
	//m_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH ;
	m_flags       = O_RDWR ; // | O_DIRECT;
	m_maxParts = 0;
	m_numParts = 0;
	m_vfd = -1;
	//m_vfdAllowed = false;
	m_fileSize = -1;
	m_lastModified = -1;
	m_numThreads = 0;
	m_isClosing = false;
	g_lastDiskReadCompleted = 0;

	// init rest to avoid logging junk
	m_isUnlink=false;	
	m_part=-1;
	m_partsRemaining=-1;
	memset(m_tinyBuf, 0, sizeof(m_tinyBuf));
	memset(m_littleBuf, 0, sizeof(m_littleBuf));
	memset(m_tmpBaseBuf, 0, sizeof(m_tmpBaseBuf));
	
	// Coverity
	m_callback = NULL;
	m_state = NULL;

	//memset ( m_littleBuf , 0 , LITTLEBUFSIZE );
	// avoid a malloc for small files.
	// this way we can save in memory RdbMaps upon a core, even malloc/free
	// related cores, cuz we won't have to do a malloc to save!
	//m_fileBuf.setBuf ( m_littleBuf,LITTLEBUFSIZE,0,false);
	// for this make the length always equal the capacity so when we
	// call reserve it builds on the whole thing
	//m_fileBuf.setLength ( m_fileBuf.getCapacity() );
}




void BigFile::logAllData(int32_t log_type)
{
	log(log_type, "Dumping BigFile at %p", (void*)this);

	struct tm tm_buf;
	struct tm *stm = localtime_r(&m_lastModified,&tm_buf);
	
	log(log_type, "m_flags................: %" PRId32, m_flags);
	log(log_type, "m_maxParts.............: %" PRId32, m_maxParts);
	log(log_type, "m_numParts.............: %d", m_numParts);
	log(log_type, "m_vfd..................: %" PRId32, m_vfd);
	log(log_type, "m_fileSize.............: %" PRId64, m_fileSize);
	log(log_type, "m_lastModified.........: %04d%02d%02d-%02d%02d%02d", stm->tm_year+1900,stm->tm_mon+1,stm->tm_mday,stm->tm_hour,stm->tm_min,stm->tm_sec);
	
	log(log_type, "m_numThreads...........: %" PRId32, m_numThreads);
	log(log_type, "m_isClosing............: [%s]", m_isClosing?"true":"false");
	log(log_type, "m_isUnlink.............: [%s]", m_isUnlink?"true":"false");
	log(log_type, "m_part.................: %" PRId32, m_part);
	log(log_type, "m_partsRemaining.......: %" PRId32, m_partsRemaining);

	loghex( log_type, m_tinyBuf, sizeof(m_tinyBuf), 		"m_tinyBuf..............: (hex dump)");
	loghex( log_type, m_littleBuf, sizeof(m_littleBuf), 	"m_littleBuf............: (hex dump)");
	loghex( log_type, m_tmpBaseBuf, sizeof(m_tmpBaseBuf),	"m_tmpBaseBuf...........: (hex dump)");
	
	// SafeBufs
	loghex( log_type, m_dir.getBufStart(), m_dir.getBufUsed(),                  			"m_dir..................: (hex dump)");
	loghex( log_type, m_baseFilename.getBufStart(), m_baseFilename.getBufUsed(),      		"m_baseFilename.........: (hex dump)");
	loghex( log_type, m_newBaseFilename.getBufStart(), m_newBaseFilename.getBufUsed(),      "m_newBaseFilename......: (hex dump)");
	loghex( log_type, m_newBaseFilenameDir.getBufStart(), m_newBaseFilenameDir.getBufUsed(),"m_newBaseFilenameDir...: (hex dump)");
	
	log(log_type, "g_lastDiskReadCompleted: %" PRId64, g_lastDiskReadCompleted);
	log(log_type, "g_unlinkRenameThreads..: %" PRId32, g_unlinkRenameThreads);
}



// we alternate parts into "dirname" and "stripeDir"
// . return false and set g_errno on error
bool BigFile::set ( const char *dir, const char *baseFilename, const char *stripeDir ) {

	logTrace( g_conf.m_logTraceBigFile, "BEGIN. dir [%s] baseFilename [%s] stripeDir [%s]",dir, baseFilename, stripeDir);

	// reset filsize
	m_fileSize = -1;
	m_lastModified = -1;

	m_dir.reset();
	m_baseFilename.reset();

	m_dir.setLabel("bfd");
	m_baseFilename.setLabel("bfbf");

	// use this 32 byte char buf to avoid a malloc if possible
	m_baseFilename.setBuf (m_tmpBaseBuf,sizeof(m_tmpBaseBuf),0,false);

	if ( ! m_dir.safeStrcpy( dir ) ) {
		logTrace( g_conf.m_logTraceBigFile, "END. Return false, m_dir.safeStrcpy failed" );
		return false;
	}
	
	if ( ! m_baseFilename.safeStrcpy( baseFilename ) ) {
		logTrace( g_conf.m_logTraceBigFile, "END. Return false, m_baseFilename.safeStrcpy failed" );
		return false;
	}

	// reset # of parts
	m_numParts = 0;
	m_maxParts = 0;

	m_filePtrsBuf.reset();

	// now add parts from both directories
	if ( ! addParts ( dir ) ) {
		log(LOG_WARN,"%s:%s:%d: END. addParts failed", __FILE__, __func__, __LINE__ );
		return false;
	}

	logTrace( g_conf.m_logTraceBigFile, "END. Return true - OK" );
	return true;
}



bool BigFile::reset ( ) {
	// RdbMap calls BigFile (m_file)::reset() so we need to free
	// the files and their safebufs for their filename and dir.
	close ();
	// reset filsize
	m_fileSize = -1;
	m_lastModified = -1;
	// m_baseFilename contains the "dir" in it
	//sprintf(m_baseFilename ,"%s/%s", dirname  , baseFilename );
	//strcpy ( m_baseFilename , baseFilename  );
	//strcpy ( m_dir          , dir           );
	//if ( stripeDir ) strcpy ( m_stripeDir    , stripeDir     );
	//else             m_stripeDir[0] = '\0';
	// reset # of parts
	//m_numParts = 0;
	//m_maxParts = 0;
	// now add parts from both directories
	// MDW: why is this in reset() function? remove...
	//if ( ! addParts ( m_dir.getBufStart() ) ) return false;
	//if ( ! addParts ( m_stripeDir ) ) return false;
	return true;
}
	

bool BigFile::addParts ( const char *dirname ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. dirname [%s]", dirname);
	
	// if dirname is NULL return true
	if ( ! dirname || ! dirname[0] ) {
		logTrace( g_conf.m_logTraceBigFile, "END - No dirname" );
		return true;
	}
	
	// . now set the names of all the Files that we consist of
	// . get the directory entry and find out what parts we have
	Dir dir;
	dir.set ( dirname );

	// set our directory class
	if ( !dir.open() ) {
		log( LOG_ERROR, "disk: openDir ('%s') failed", dirname );
		return false;
	}
	
	// match files with this pattern in the directory
	char pattern[256];
	sprintf(pattern,"%s*", m_baseFilename.getBufStart() );

	// length of the base filename
	int32_t blen = strlen ( m_baseFilename.getBufStart() );

	// . set our m_files array
	// . addFile() will return false on problems
	// . the lower the fileId the older the file (w/ exception of #0)
	
	logTrace( g_conf.m_logTraceBigFile, "Look for [%s]", pattern);
	
	const char *filename;
	while ( ( filename = dir.getNextFilename ( pattern ) ) ) {
		logTrace( g_conf.m_logTraceBigFile, "  Checking [%s]", filename);
		
		// if filename len is exactly blen it's part 0
		int32_t flen = strlen(filename);
		int32_t part = -1;
		if ( flen == blen ) {
			part = 0;
			// some files have the same first X chars, like 
			// indexdb.store-info-bak but are not part files
			logTrace( g_conf.m_logTraceBigFile, "  Default to part 0" );
		} else if ( flen > blen && strncmp(filename+blen,".part",5)!=0) {
			logTrace( g_conf.m_logTraceBigFile, "  No good." );
			continue;
		} else if (flen - blen < 6 ) {
			log( LOG_WARN, "disk: Part extension too small for '%s'. Must end in .partN to be valid.", filename );
			continue;
		} else {
			part = atoi ( filename + blen + 5 );
			logTrace( g_conf.m_logTraceBigFile, "  Detected part %" PRId32, part);
		}

		// make this part file
		if( !addPart( part ) ) {
			log( LOG_ERROR,"%s:%s:%d: END. addPart failed, returning false.", __FILE__, __func__, __LINE__ );
			return false;
		}
	}

	logTrace( g_conf.m_logTraceBigFile, "END - OK" );
	return true;
}



// WE CAN'T REALLOC the safebuf because there might be a thread 
// referencing the file ptr. so let's just keep the m_filePtrs[] array
// and realloc on that.
bool BigFile::addPart ( int32_t n ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN n [%" PRId32"] filename [%s]", n, getFilename());

	// . grow our dynamic array and return ptr to last element
	// . n's come in NOT necessarily in order!!!
	int32_t need = (n+1) * sizeof(File *);
	// capacity must be length always for this
	if ( m_filePtrsBuf.getCapacity() != m_filePtrsBuf.getLength() ) {
		log(LOG_ERROR, "%s:%s:%d: Capacity/Length mismatch when adding part %" PRId32, __FILE__, __func__, __LINE__, n);
		logAllData(LOG_ERROR);
		gbshutdownLogicError();
	}

	// init using tiny buf to save a malloc for small files
	if ( m_filePtrsBuf.getCapacity() == 0 ) {
		memset (m_tinyBuf,0,8);
		m_filePtrsBuf.setBuf ( m_tinyBuf,8,0,false);
		m_filePtrsBuf.setLength ( m_filePtrsBuf.getCapacity() );
	}

	// how much more mem do we need?
	int32_t delta = need - m_filePtrsBuf.getLength();

	// . make sure our CAPACITY is increased by what we need
	// . SafeBuf::reserve() ADDS this much to current capacity
	// . true = clear new mem new new file ptrs are null because
	//   there may be gaps or not exist because the BigFile was being
	//   merged.
	if ( delta > 0 && ! m_filePtrsBuf.reserve ( delta ,"bfbuf", true ) ) {
		log(LOG_ERROR, "%s:%s:%d: Failed to reserve %" PRId32" more mem for part", __FILE__, __func__, __LINE__, delta);
		logAllData(LOG_ERROR);
		return false;
	}

	// make length the capacity. so if buf is resized in call to
	// SafeBuf::reserve() it will copy over all of the old buf to new buf
	m_filePtrsBuf.setLength ( m_filePtrsBuf.getCapacity() );

	File **filePtrs = (File **)m_filePtrsBuf.getBufStart();

	File *f = NULL;

	if ( m_numParts == 0 ) {
		if ( LITTLEBUFSIZE < sizeof(File) ) {
			log(LOG_ERROR, "%s:%s:%d: LITTLEBUFSIZE too small", __FILE__, __func__, __LINE__ );
			logAllData(LOG_ERROR);
			gbshutdownLogicError();
		}
		f = new(m_littleBuf) File();
	} else {
		try {
			f = new (File); 
		} catch ( ... ) {
			g_errno = ENOMEM;

			//### BR 20151217: Fix. Previously returned the return code from log(...)
			logError("new failed. size: %i, err [%s]", (int)sizeof(File), mstrerror(g_errno));
			logAllData(LOG_ERROR);
			return false;
		}
		mnew ( f , sizeof(File) , "BigFile" );
	}
	
	char buf[1024];

	// make the filename for this new File class
	makeFilename_r ( m_baseFilename.getBufStart() , NULL, n , buf , 1024 );

	// and set it with that
	f->set ( buf );

	// store the ptr to it in m_filePtrs
	filePtrs [ n ] = f;
	++m_numParts;

	// set maxPart
	if ( n+1 > m_maxParts ) {
		m_maxParts = n+1;
		logTrace( g_conf.m_logTraceBigFile, "New m_maxParts: %" PRId32, m_maxParts );
	}
	
	logTrace( g_conf.m_logTraceBigFile, "END - OK. New File object prepared. returning true" );
	return true;
}


bool BigFile::doesExist() const {
	return m_numParts != 0;
}


// if we can open it with a valid fd, then it exists
bool BigFile::doesPartExist ( int32_t n ) {
	if ( n >= m_maxParts ) return false;
	// f will be null if part does not exist
	File *f = getFile2(n);
	if ( f ) return true;
	return false;
}


static int64_t s_vfd = 0;


// . overide File::open so we can set m_numParts
// . set maxFileSize when opening a new file for writing and using 
//   DiskPageCache
// . use maxFileSize of -1 for us to use getFileSize() to set it
bool BigFile::open(int flags) {
    m_flags       = flags;
	m_isClosing   = false;

	// . init the page cache for this vfd
	// . this returns our "virtual fd", not the same as File::m_vfd
	// . returns -1 and sets g_errno on failure
	// . we pass m_vfd to getPages() and addPages()
	if ( m_vfd == -1 ) {
		m_vfd = ++s_vfd;
	}
	return true;
}

// get the filename of the nth file using m_dir/m_stripeDir & m_baseFilename
void BigFile::makeFilename_r ( char *baseFilename    , 
			       char *baseFilenameDir , 
			       int32_t  n               , 
			       char *buf             ,
			       int32_t bufSize ) {
	char *dir = m_dir.getBufStart();
	if ( baseFilenameDir && baseFilenameDir[0] ) dir = baseFilenameDir;
	int32_t r;
	// ensure we do not breach the buffer
	// int32_t dirLen = strlen(dir);
	// int32_t baseLen = strlen(baseFilename);
	// int32_t need = dirLen + 1 + baseLen + 1;
	// if ( need < bufSize ) gbshutdownLogicError();
	//static char s[1024];
	// if ( (n % 2) == 0 || ! m_stripeDir[0] ) 
	// 	sprintf ( buf, "%s/%s",   dir      , baseFilename );
	// else    sprintf ( buf, "%s/%s", m_stripeDir, baseFilename );
	if ( n == 0 ) {
		r = snprintf ( buf, bufSize, "%s/%s",dir,baseFilename);
		if ( r < bufSize ) return;
		// truncation is bad
		gbshutdownLogicError();
	}
	// return if it fit into "buf"
	r = snprintf ( buf, bufSize, "%s/%s.part%" PRId32,dir,baseFilename,n);
	if ( r < bufSize ) return;
	// truncation is bad
	gbshutdownLogicError();
}


// . get the fd of the nth file
// . will try to open the file if it hasn't yet been opened
int BigFile::getfd ( int32_t n , bool forReading ) {
	// boundary check
	if ( n >= m_maxParts && ! addPart ( n ) ) {
		log( LOG_ERROR, "disk: Part number %" PRId32" > %" PRId32". fd not available.", n, m_maxParts );
		    
		// return -1 to indicate can't do it
		return -1;
	}

	// get the File ptr from the table
	File *f = getFile2(n);
	// if part does not exist then create it! addPart(n) will do that?
	if (!f) {
		// don't create File if we're getting it for reading
		if (forReading) {
			log( LOG_WARN, "disk: Don't create file when we're getting it for reading" );
			return -1;
		}

		if (!addPart(n)) {
			log(LOG_WARN, "disk: Unable to add part %" PRId32, n);
			return -1;
		}

		f = getFile2(n);
		if (!f) {
			log(LOG_WARN, "disk: Unable to get part %" PRId32, n);
			return -1;
		}
	}

	// open it if not opened
	if (!f->calledOpen()) {
		if (!f->open(m_flags, getFileCreationFlags())) {
			log(LOG_WARN, "disk: Failed to open file part #%" PRId32".", n);
			return -1;
		}
	}

	// get it's file descriptor
	int fd = f->getfd();
	if (fd >= -1) {
		return fd;
	}

	// otherwise, fd is -2 and it's never been opened?!?!
	g_errno = EBADENGINEER;
	log( LOG_LOGIC, "disk: fd is -2." );

	return -1;
}


// . return -2 on error
// . return -1 if does not exist
// . otherwise return the big file's complete file size (can be well over 2gb)
int64_t BigFile::getFileSize() const {
	// return if already computed
	if ( m_fileSize >= 0 ) {
		return m_fileSize;
	}

	// add up the sizes of each file
	int64_t totalSize = 0;
	for ( int32_t n = 0 ; n < m_maxParts ; n++ ) {
		// shortcut
		const File *f = getFile2(n);
		// we can have headless big files... count the heads.
		// this can happen if the first Files were deleted because
		// of an ongoing merge operation.
		if ( ! f ) { 
			totalSize += MAX_PART_SIZE; 
			continue; 
		}
		// . returns -2 on error, -1 if does not exist
		// . TODO: it returns 0 if does not exist! FIX...
		int64_t size = f->getFileSize();
		if ( size == -2 ) return -2;
		if ( size == -1 ) break;
		totalSize += size;
	}
	// save time
	m_fileSize = totalSize;
	return totalSize;
}


// . return -2 on error
// . return -1 if does not exist
// . otherwise returns the oldest of the last mod dates of all the part files
time_t BigFile::getLastModifiedTime ( ) {
	// return if already computed
	if ( m_lastModified >= 0 ) return m_lastModified;

	// add up the sizes of each file
	time_t min = -1;
	for ( int32_t n = 0 ; n < m_maxParts ; n++ ) {
		// shortcut
		File *f = getFile2(n);
		// we can have headless big files... count the heads
		if ( ! f ) continue;
		// returns -1 on error, 0 if file does not exist
		time_t date = f->getLastModifiedTime();
		if ( date == -1 ) return -2;
		if ( date ==  0 ) break;
		// check min
		if ( date < min || min == -1 ) min = date;
	}
	// save time
	m_lastModified = min;
	return m_lastModified;
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we need a ptr to the ptr to this BigFile so if we get deleted and
//   a signal is still pending for us, the callback will know we are nuked
bool BigFile::read  ( void       *buf    , 
		      int64_t        size   ,
		      int64_t   offset , 
		      FileState  *fs     ,                 
		      void       *state  ,
		      void      (* callback)(void *state) ,
		      int32_t        niceness                ,
		      int32_t        allocOff  ) {
	g_errno = 0;
	return readwrite ( buf , size , offset , false/*doWrite?*/, 
			   fs  , state, callback , niceness , allocOff );
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
bool BigFile::write ( void       *buf    ,
                      int64_t        size   ,
		      int64_t   offset , 
		      FileState  *fs     ,
		      void       *state  ,
		      void      (* callback)(void *state) ,
		      int32_t        niceness) {
	// sanity check
	if ( g_conf.m_readOnlyMode ) {
		logf(LOG_DEBUG,"disk: BigFile: Trying to write while in "
		     "read only mode.");
		return true;
	}
	g_errno = 0;
	return readwrite ( buf , size , offset , true/*doWrite?*/ , 
			   fs  , state, callback , niceness , 0 );
}


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we divide into 2 writes in case write spans 2 files
// . only BigFiles will support non-blocking read/writes for now
// . damn, i thought linux supported non-blocking file reads, but it doesn't!
// . we use the aio.h calls
// . we should us kaio from sgi cuz it's in the kernel and only uses 4 threads
//   whereas using librt.a creates a thread every time we call aio_read/write()
// . fstate is used by aio_read/write()
// . we need a ptr to the ptr to this BigFile so if we get deleted and
//   a signal is still pending for us, the callback will know we are nuked
bool BigFile::readwrite ( void         *buf      ,
                          int64_t          size     ,
			  int64_t     offset   , 
			  bool          doWrite  ,
			  FileState    *fstate   ,
			  void         *state    ,
			  void        (* callback) ( void *state ) ,
			  int32_t          niceness ,
			  int32_t          allocOff ) {
	// are we blocking?
	bool isNonBlocking = m_flags & O_NONBLOCK;
	// if we're non blocking and caller didn't supply an "fstate"
	if ( isNonBlocking && ! fstate ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"disk: readwrite() call is "
		    "specified as non-blocking, but no state provided.");
		return true;
	}
	// reset file size in case we change it here
	if ( doWrite ) {
		m_fileSize = -1;
		m_lastModified = getTimeLocal();
	}
	// . sanity check
	// . when our offset was just a int32_t 2gig+ files, when dumped,
	//   had negative offsets, bad engineer
	if ( offset < 0 ) {
		log(LOG_LOGIC,"disk: readwrite() offset is %" PRId64" "
		    "< 0. filename=%s/%s. dumping core. try deleting "
		    "the .map file for it and restarting.",offset,
		    m_dir.getBufStart(),m_baseFilename.getBufStart());
		gbshutdownLogicError();
	}

	// if we're not blocking use a fake fstate
	FileState tmp;
	if ( ! fstate ) {
		fstate = &tmp;
	}

	// reset this
	fstate->m_errno = 0;
	// set up fstate
	fstate->m_bigfile     = this;
	// buf may be NULL if caller passed in a NULL "buf" and it did not hit 
	// the disk page cache. Threads.cpp will have to allocate it right
	// before it launches the thread.
	fstate->m_buf         = (char *)buf;
	// if getPages() allocates a buf, this will point to it
	fstate->m_allocBuf    = NULL;
	fstate->m_allocSize   = 0;
	// when buf is passed in as NULL we allocate it in Threads.cpp right 
	// before we launch it to save memory.
	// we have to know where to start storing
	// the read into it for RdbScan, it is not immediately at the 
	// beginning of the allocated buffer because RdbScan may have to 
	// turn the first key from a 6 byte half key into a 12 byte key so it
	// needs some initial padding. this is because RdbLists should never
	// start with a 6 byte half key.
	fstate->m_allocOff    = allocOff;
	fstate->m_bytesToGo   = size;
	fstate->m_offset      = offset;
	fstate->m_doWrite     = doWrite;
	fstate->m_bytesDone   = 0;
	fstate->m_state       = state;
	fstate->m_callback    = callback;
	fstate->m_niceness    = niceness;
	fstate->m_flags       = m_flags;

	// sanity
	if ( fstate->m_bytesToGo > 150000000 ) {
		log( LOG_WARN, "file: huge read of %" PRId64" bytes", ( int64_t ) size );
	}

	// . set our fd's before entering the thread in case RdbMerge
	//   calls our unlinkPart() 
	// . it's thread-UNsafe to call getfd() from within the thread
	// . FUCK! what if we get unlinked and another file gets this fd!!
	// . now we do do unlinks in a thread in File.cpp, but since we
	//   employ the getCloseCount_r() scheme we can detect when this
	//   situation occurs and pass a g_errno back to the caller.
	fstate->m_filenum1    =  offset          / MAX_PART_SIZE;
	fstate->m_filenum2    = (offset + size ) / MAX_PART_SIZE;

	// . save the open count for this fd
	// . if it changes when we're done with the read we do a re-read
	// . it gets incremented once every time File calls ::open and gets
	//   back this fd
	// . fd1 and fd1 are now set in Threads.cpp since we only want to do
	//   the open right before we actually launch the thread.
	fstate->m_fd1  = -3;
	fstate->m_fd2  = -3;

	// . if we are writing, prevent these fds from being closed on us
	//   by File::closedLeastUsed(), because the fd could then be re-opened
	//   by someone else doing a write and we end up writing to THAT FILE!
	// . the closeCount mechanism helps us DETECT when something like this
	//   happens, but it will not prevent the write from going through
	if ( doWrite ) {
		// actually have to do the open here for writing so it
		// can prevent the fds from being closed on us
		fstate->m_fd1 = getfd ( fstate->m_filenum1 , !doWrite);
		fstate->m_fd2 = getfd ( fstate->m_filenum2 , !doWrite);

		enterWriteMode( fstate->m_fd1 );
		enterWriteMode( fstate->m_fd2 );

		fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
		fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );
	}
	//grab the filenames of the associated files so we later can check for pending deletion
	if(fstate->m_filenum1<m_maxParts)
		strcpy(fstate->m_filename1, getFile2(fstate->m_filenum1)->getFilename());
	else
		fstate->m_filename1[0] = '\0';
	if(fstate->m_filenum2<m_maxParts)
		strcpy(fstate->m_filename2, getFile2(fstate->m_filenum2)->getFilename());
	else
		fstate->m_filename2[0] = '\0';

	// get the close counts after calling getfd() since if getfd() calls
	// File::open() that will inc the counts
	// closeCount1 and 2 are now set in Threads.cpp since we want to only 
	// open the fd right before we launch the thread.
	//fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
	//fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );

	fstate->m_errno       = 0;
	fstate->m_startTime   = gettimeofdayInMilliseconds();
	fstate->m_vfd         = m_vfd;

	// . if we're blocking then do it now
	// . this should return false and set g_errno on error, true otherwise
	if ( !isNonBlocking || !callback || !g_jobScheduler.are_new_jobs_allowed() ) 	{
		goto skipThread;
	}

	// . otherwise, spawn a thread to do this i/o
	// . this returns false and sets g_errno on error, true on success
	// . we should return false cuz we blocked
	// . thread will add signal to g_loop on completion to call
	if ( g_jobScheduler.submit_io(readwriteWrapper_r, doneWrapper, fstate, thread_type_unspecified_io, niceness, doWrite) ) {
		return false;
	}

	// sanity check
	if ( ! callback ) {
		gbshutdownLogicError();
	}

	// thread spawn failed, do it blocking then
	log(LOG_INFO, "disk: Doing blocking disk access. This will hurt performance. isWrite=%" PRId32".",(int32_t)doWrite);

	// come here if we haven't spawned a thread
skipThread:

	// if there was no room in the thread queue, then we must do this here
	fstate->m_fd1         = getfd ( fstate->m_filenum1 , !doWrite );
	fstate->m_fd2         = getfd ( fstate->m_filenum2 , !doWrite );
	fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
	fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );

	// clear g_errno from the failed thread spawn
	g_errno = 0;

	// since Threads.cpp usually allocs the buffer before launching,
	// we must do it here now
	FileState *fs = fstate;
	if ( ! fs->m_doWrite && ! fs->m_buf && fs->m_bytesToGo > 0 ) {
		int64_t need = fs->m_bytesToGo + fs->m_allocOff;
		char *p = (char *) mmalloc ( need , "ThreadReadBuf" );
		if ( p ) {
			fs->m_buf       = p + fs->m_allocOff;
			fs->m_allocBuf  = p;
			fs->m_allocSize = need;
		} else {
			log( LOG_WARN, "disk: read buf alloc failed for %" PRId64" bytes.", need );
		}
	}

	// . this returns false and sets errno on error
	// . set g_errno to the errno
	if ( ! readwrite_r ( fstate ) ) {
		g_errno = errno;
	}

	// exit write mode
	if ( doWrite ) {
		exitWriteMode( fstate->m_fd1 );
		exitWriteMode( fstate->m_fd2 );
	}

	// set this up here
	fstate->m_bytesDone = fstate->m_bytesToGo;
	// and this too
	fstate->m_doneTime = gettimeofdayInMilliseconds();

	// if it read less than 8MB/s bitch
	int64_t now   = gettimeofdayInMilliseconds() ;
	int64_t took  = now - fstate->m_startTime ;
	int64_t      rate  = 100000;
	if ( took  > 500 ) rate = fstate->m_bytesDone / took ;
	if ( rate < 8000 && fstate->m_niceness <= 0 ) {
		log(LOG_INFO,"disk: Read %" PRId64" bytes in %" PRId64" "
		    "ms (%" PRId64"KB/s).",
		    fstate->m_bytesDone,took,rate);
		g_stats.m_slowDiskReads++;
	}

	// default graph color is black
	int color = 0x00000000; 

	if ( fstate->m_doWrite ) {
		// use red for writes, though
		color = 0x00ff0000;
	} else if ( fstate->m_niceness > 0 ) {
		// but gray for low priority reads
		color = 0x00808080;
	}

	// add the stat
	g_stats.addStat_r ( fstate->m_bytesDone, fstate->m_startTime, now, color );

	// now log our stuff here
	if ( g_errno && g_errno != EBADENGINEER ) {
		log( LOG_WARN, "disk: readwrite: %s", mstrerror(g_errno));
	}

	// . this EBADENGINEER can happen right after a merge if
	//   the file is renamed because the fd may have changed from
	//   under us
	// . i added EBADF because RbdDump was failing because of this when
	//   trying to write the tree to a file
	// . EBADF happens when we unlink a file from under a read or write
	// . the closeCount code below was not saving us from coring on EBADF
	//   because the closeCount is only changed if another file is opened
	//   with that fd, it is not incremented on a close() but rather on
	//   an open()
	/*
	if ( g_errno == EBADENGINEER ) { // || g_errno == EBADF ) {
		int32_t fn1 = fstate->m_filenum1;
		int32_t fn2 = fstate->m_filenum2;
		char *s = getFilename();
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%" PRId32")",s,fn1);
		log(LOG_DEBUG,"disk: Closing old fd2 (%s,%" PRId32")",s,fn2);
		// get the File ptr from the table
		File *f1 = getFile(fn1);
		File *f2 = getFile(fn2);
		if ( f2 == f1 ) f2 = NULL;
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%" PRId32")",s,fn1);
		if ( f2) log(LOG_DEBUG,"disk: Closing old fd2 (%s,%" PRId32")",s,fn2);
		if ( f1 ) f1->close();
		if ( f2 ) f2->close();
	}
	*/
	// we didn't block so return true
	return true;
}


// . this should be called from the main process after getting our call OUR callback here
// Use of ThreadEntry parameter is NOT thread safe
void doneWrapper ( void *state, job_exit_t exit_type ) {
	FileState *fstate = (FileState *)state;

	if( exit_type != job_exit_normal ) {
		log(LOG_INFO, "disk: Read canceled due to JobScheduler exit type %d.", (int)exit_type);
		//call calback with m-errno set
		fstate->m_errno = ECLOSING;
		fstate->m_callback ( fstate->m_state );
		return;
	}

	// any writes we did in the disk read thread were done to the
	// "tmp" FileState class on the stack, so now we have the real deal
	// we can update all this junk.
	fstate->m_bytesDone = fstate->m_bytesToGo;

	// exit write mode
	if ( fstate->m_doWrite ) {
		// THIS could have been deleted!!
		exitWriteMode( fstate->m_fd1 );
		exitWriteMode( fstate->m_fd2 );
	}
	// if it read less than 8MB/s bitch
	int64_t took = fstate->m_doneTime - fstate->m_startTime;
	int32_t      rate = 100000;
	if ( took > 500 ) rate = fstate->m_bytesDone / took ;
	bool slow = false;
	if ( rate < 8000 ) slow = true;
	if ( slow && fstate->m_niceness <= 0 ) {
		log(LOG_INFO, "disk: Read %" PRId64" bytes in %" PRId64" ms (%" PRId32"KB/s).", fstate->m_bytesDone,took,rate);
		g_stats.m_slowDiskReads++;
	}

	// recall g_errno from state's m_errno
	g_errno = fstate->m_errno;

	// add the stat
	if ( ! g_errno ) {
		// default graph color is black (disk read)
		int color = 0x00000000;
		if ( fstate->m_doWrite ) {
			// use red for writes
			color = 0x00ff0000;
		} else if ( fstate->m_niceness > 0 ) {
			// but gray for low priority reads
			color = 0x00808080;
		}
		// add it
		g_stats.addStat_r ( fstate->m_bytesDone, fstate->m_startTime, fstate->m_doneTime, color );
	}

	// now log our stuff here
	int32_t tt = ( g_errno == EFILECLOSED ) ? LOG_INFO : LOG_WARN;
	if ( g_errno ) {
		log( tt, "disk: err=%s. fd1=%" PRId32" fd2=%" PRId32" "
				     "off=%" PRId64" toread=%" PRId32,
		     mstrerror( g_errno ),
		     ( int32_t ) fstate->m_fd1,
		     ( int32_t ) fstate->m_fd2,
		     ( int64_t ) fstate->m_offset,
		     ( int32_t ) fstate->m_bytesToGo
		);
	}

	// . this EBADENGINEER can happen right after a merge if
	//   the file is renamed because the fd may have changed from
	//   under us
	// . i added EBADF because RbdDump was failing because of this when
	//   trying to write the tree to a file
	// . the closeCount code below was not saving us from coring on EBADF
	//   because the closeCount is only changed if another file is opened
	//   with that fd, it is not incremented on a close() but rather on
	//   an open()
	/*
	if ( g_errno == EBADENGINEER ) { // || g_errno == EBADF ) {
		int32_t fn1 = fstate->m_filenum1;
		int32_t fn2 = fstate->m_filenum2;
		// CAUTION: if file got delete THIS will be invalid!!!
		BigFile *THIS = fstate->m_bigfile;
		char *s = THIS->getFilename();
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%" PRId32")",s,fn1);
		log(LOG_DEBUG,"disk: Closing old fd2 (%s,%" PRId32")",s,fn2);
		// get the File ptr from the table
		File *f1 = THIS->getFile(fn1);
		File *f2 = THIS->getFile(fn2);
		if ( f2 == f1 ) f2 = NULL;
		if ( f1 ) { f1->close();log(LOG_DEBUG,"disk: Closed old fd1");}
		if ( f2 ) { f2->close();log(LOG_DEBUG,"disk: Closed old fd2");}
	}
	*/
	// call the callback, with errno set if there was an error
	fstate->m_callback ( fstate->m_state );
}


static void readwriteWrapper_r ( void *state ) {
	int64_t time_start = gettimeofdayInMilliseconds();

	// extract our class
	FileState *fstate = (FileState *)state;

	//check if the file (part) is scheduled to be deleted. If so, abort.
	if(fstate->m_filename1[0] && isPendingUnlink(fstate->m_filename1)) {
		log(LOG_WARN,"readwriteWrapper_r: file %s is marked for unlinking; aborting read/write",fstate->m_filename1);
		fstate->m_errno = EFILECLOSED;
		fstate->m_doneTime = gettimeofdayInMilliseconds();
		return;
	}
	if(fstate->m_filename2[0] && isPendingUnlink(fstate->m_filename2)) {
		log(LOG_WARN,"readwriteWrapper_r: file %s is marked for unlinking; aborting read/write",fstate->m_filename2);
		fstate->m_errno = EFILECLOSED;
		fstate->m_doneTime = gettimeofdayInMilliseconds();
		return;
	}
	
	if( !fstate->m_doWrite && !fstate->m_buf && fstate->m_bytesToGo>0 ) {
		int32_t need = fstate->m_allocOff + fstate->m_bytesToGo;
		char *p = (char *) mmalloc ( need , "ThreadReadBuf" );
		if ( p ) {
			fstate->m_buf       = p + fstate->m_allocOff;
			fstate->m_allocBuf  = p;
			fstate->m_allocSize = need;
		} else {
			log( LOG_WARN, "thread: read buf alloc failed for %" PRId32" bytes.", need );
		}
	}
	fstate->m_fd1 = fstate->m_bigfile->getfd (fstate->m_filenum1,!fstate->m_doWrite);
	fstate->m_fd2 = fstate->m_bigfile->getfd (fstate->m_filenum2,!fstate->m_doWrite);

	// is this bad?
	if ( fstate->m_fd1 < 0 ) {
		log( LOG_WARN, "disk: fd1 is %i for %s", fstate->m_fd1, fstate->m_bigfile->getFilename() );
	}

	if ( fstate->m_fd2 < 0 ) {
		log( LOG_WARN, "disk: fd2 is %i for %s", fstate->m_fd2, fstate->m_bigfile->getFilename() );
	}

	fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
	fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );
	
	// clear thread's errno
	errno = 0;

	// . do the readwrite_r() since we're a thread now
	// . this SHOULD NOT set g_errno, we're a thread!
	// . it does have it's own errno however
	
	bool status = readwrite_r ( fstate );

	// set errno
	if ( ! status ) {
		fstate->m_errno = errno;
	}

	// . if open count changed on us our file got unlinked from under us
	//   and another file was opened with that same fd!!! 
	// . just fail the read so caller knows it is bad
	// . do not do this for writes because RdbDump can fail when writing!
	// . in that case hopefully write will fail if the fd was re-opened
	//   for another file in RDONLY mode, but, if per chance it opens
	//   a different file for dumping or merging with this same fd then
	//   we may be seriously screwing things up!! TODO: investigate
	// . f1 and f2 can be non-null and invalid here now on the ssds
	//   i saw this happen on gk153... i preserved the core/gb on there
	//if ( (getCloseCount_r (fstate->m_fd1) != fstate->m_closeCount1 || 
	//      getCloseCount_r (fstate->m_fd2) != fstate->m_closeCount2   )) {
	// get current close counts. we can't access BigFile because it
	// might have been deleted or closed on us, i saw this before.
	int32_t cc1 = getCloseCount_r ( fstate->m_fd1 );
	int32_t cc2 = getCloseCount_r ( fstate->m_fd2 );
	if ( cc1 != fstate->m_closeCount1 || cc2 != fstate->m_closeCount2  ) {
		log( LOG_WARN, "file: c1a=%" PRId32" c1b=%" PRId32" c2a=%" PRId32" c2b=%" PRId32,
		    cc1, fstate->m_closeCount1, cc2, fstate->m_closeCount2 );

		if ( ! fstate->m_doWrite ) {
			fstate->m_errno = EFILECLOSED;
		} else {
			// we use s_writing[] locks in File.cpp to prevent a write
			// operation's fd from being closed under him
			log(LOG_ERROR,"PANIC: fd closed on us while writing. This should "
			 "never happen!! Simultaneous writes?");
		}
	}
	
	int64_t time_took = gettimeofdayInMilliseconds() - time_start;

	if ( !fstate->m_doWrite && time_took >= g_conf.m_logDiskReadTimeThreshold ) {
		log( LOG_WARN, "Disk read of %" PRId64" bytes took %" PRId64" ms", fstate->m_bytesDone, time_took );
	}

	fstate->m_doneTime = gettimeofdayInMilliseconds();
}


// . returns false and sets errno on error, true on success
// . don't log shit when you're in a thread anymore
// Use of ThreadEntry parameter is NOT thread safe
static bool readwrite_r ( FileState *fstate ) {
	// if no buffer to read into the alloc in Threads.cpp failed
	if ( ! fstate->m_buf ) {
		errno = EBUFTOOSMALL;
		log( LOG_WARN, "disk: read buf is NULL. malloc failed?");
		return false;
	}

	// how many total bytes to write?
	int64_t bytesToGo = fstate->m_bytesToGo;
	// how many bytes we've written so far
	int64_t bytesDone = fstate->m_bytesDone;
	// get current offset
	int64_t offset = fstate->m_offset + fstate->m_bytesDone;
	// are we writing? or reading?
	bool doWrite = fstate->m_doWrite;
	// point to buf
	char *p = fstate->m_buf + bytesDone;

	for (;;) {
		// return here if done
		if (bytesDone >= bytesToGo) {
			return true;
		}

		// translate offset to a filenum and offset
		int32_t filenum = offset / MAX_PART_SIZE;
		int64_t localOffset = offset % MAX_PART_SIZE;

		// how many bytes to read/write to first little file?
		int64_t avail = MAX_PART_SIZE - localOffset;

		// how may bytes do we have left to read/write
		int64_t len = bytesToGo - bytesDone;

		// how many bytes can we write to it now
		if (len > avail) {
			len = avail;
		}

		// get the fd for this filenum
		int fd = -1;
		if (filenum == fstate->m_filenum1) {
			fd = fstate->m_fd1;
		} else if (filenum == fstate->m_filenum2) {
			fd = fstate->m_fd2;
		}

		// return -1 on error
		if (fd < 0) {
			errno = EBADENGINEER;
			log(LOG_LOGIC, "disk: fd < 0 for filenum %d. Bad engineer.", filenum);
			return false;
		}

		// reset this
		errno = 0;

		// n holds how many bytes read/written
		ssize_t n;

		// do the read/write blocking
		if (doWrite) {
			n = pwrite(fd, p, len, localOffset);
		} else {
			n = pread(fd, p, len, localOffset);
		}

		// debug msg
		if (g_conf.m_logDebugDisk) {
			const char *s = (fstate->m_doWrite) ? "wrote" : "read";
			const char *t = (fstate->m_flags & O_NONBLOCK) ? "yes" : "no";    // are we blocking?

			// this is bad for real-time threads cuz our unlink() routine
			// may have been called by RdbMerge and our m_files may be
			// altered
			// MDW: don't access m_bigfile in case bigfile was deleted
			// since we are in a thread
			log(LOG_DEBUG, "disk::readwrite: %s %zi bytes of %" PRId64" @ offset %" PRId64
					    "(nonBlock=%s) "
					    "fd %i "
					    "cc1=%i=?%i cc2=%i=?%i errno=%s",
			    s, n, len, localOffset,
			    t,
			    fd,
			    (int) fstate->m_closeCount1,
			    (int) getCloseCount_r(fstate->m_fd1),
			    (int) fstate->m_closeCount2,
			    (int) getCloseCount_r(fstate->m_fd2),
			    mstrerror(errno));
		}

		updateDiskReadCompleted();

		// . if n is 0 that's strange!!
		// . i think the fd will have been closed and re-opened on us if this
		//   happens... usually
		if (n == 0 && len > 0) {
			// MDW: don't access m_bigfile in case bigfile was deleted
			// since we are in a thread
			log(LOG_WARN, "disk: Read of %" PRId64" bytes at offset %" PRId64" "
					    " failed because file is too short for that "
					    "offset? Our fd was probably stolen from us by another "
					    "thread. fd1=%i fd2=%i len=%" PRId64" filenum=%i "
					    "localoffset=%" PRId64". error=%s.",
					    len, fstate->m_offset,
					    fstate->m_fd1,
					    fstate->m_fd2,
					    len,
					    filenum,
					    localOffset,
					    mstrerror(errno));
			errno = EBADENGINEER;
			return false;
		}

		// on other errno, return -1
		if (n < 0) {
			log(LOG_ERROR, "disk::readwrite_r: %s", mstrerror(errno));
			gbshutdownAbort(true);
		}

		// . flush the write
		// . linux's write cache may be messing with my data!
		// . no, turns out write errors (garbage written) happens anyway...
		// . now we flush all writes! skip bdflush man.
		// . only allow syncing if file is non-blocking, because blocking
		//   writes are used for when we call RdbTree::fastSave_r() and it
		//   takes forever to dump Spiderdb if we sync each little write
#ifndef __APPLE_
		if (g_conf.m_flushWrites && doWrite && (fstate->m_flags & O_NONBLOCK) && fdatasync(fd) < 0) {
			log(LOG_WARN, "disk: fdatasync: %s", mstrerror(errno));
			// ignore an error here
			errno = 0;
		}
#endif
		// update the count
		bytesDone += n;
		// inc the main offset and the buffer ptr, "p"
		offset += n;
		p += n;
		// add to fileState
		fstate->m_bytesDone += n;
	}
}


////////////////////////////////////////
// non-blocking unlink/rename code
////////////////////////////////////////

bool BigFile::unlink ( ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. filename [%s]", getFilename());
	
	bool rc = unlinkRename( NULL , -1 , false, NULL, NULL );
	// rc indicates blocked/unblocked

	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}



bool BigFile::move ( const char *newDir ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. filename [%s] newDir [%s]", getFilename(), newDir);

	bool rc = rename( m_baseFilename.getBufStart() , newDir );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}


bool BigFile::rename(const char *newBaseFilename, const char *newBaseFilenameDir, bool force ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. newBaseFilename [%s] newBaseFilenameDir [%s]", newBaseFilename, newBaseFilenameDir);
	
	bool rc = unlinkRename ( newBaseFilename, -1, false, NULL, NULL, newBaseFilenameDir, force );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}


bool BigFile::unlinkPart(int32_t part ) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. part %" PRId32, part);
	
	bool rc = unlinkRename ( NULL, part, false, NULL, NULL );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}


bool BigFile::unlink(void (*callback)(void *state), void *state) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN." );

	bool rc = unlinkRename ( NULL , -1 , true, callback , state );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}


bool BigFile::rename(const char *newBaseFilename, void (*callback)(void *state), void *state, bool force) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. filename [%s] newBaseFilename [%s]", getFilename(), newBaseFilename);

	bool rc=unlinkRename ( newBaseFilename, -1, true, callback, state, NULL, force );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}


bool BigFile::unlinkPart(int32_t part, void (*callback)(void *state), void *state) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN. part %" PRId32, part);

	// set return value to false if we blocked somewhere
	bool rc = unlinkRename ( NULL, part, true, callback, state );
	// rc indicates blocked/unblocked
	
	logTrace( g_conf.m_logTraceBigFile, "END. returning [%s]", rc?"true":"false");
	return rc;
}

struct UnlinkRenameState {
	UnlinkRenameState(BigFile *bigfile, File *file, int32_t i)
		: m_bigfile(bigfile)
		, m_file(file)
		, m_i(i)
		, m_errno(0) {
	}

	BigFile *m_bigfile;
	File *m_file;
	int32_t m_i;
	int32_t m_errno;
};

/**
 *
 * @param newBaseFilename non-NULL for renames, NULL for unlinks
 * @param part part num to unlink, -1 for all (or rename)
 * @param useThread should thread be used
 * @param callback function to call when operation is done
 * @param state state to be passed to callback function
 * @param newBaseFilenameDir if NULL, defaults to m_dir, the current dir in which this file already exists
 * @param force should rename be done even if destination file exists
 * @return false if blocked, true otherwise
 */
// . sets g_errno on error
bool BigFile::unlinkRename(const char *newBaseFilename, int32_t part, bool useThread,
                           void (*callback)(void *state), void *state, const char *newBaseFilenameDir, bool force) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN" );

	// fail in read only mode
	if ( g_conf.m_readOnlyMode ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "disk: cannot unlink or rename files in read only mode");
		return true;
	}

	// . wait for any previous unlink to finish
	// . we can only store one callback at a time, m_callback, so we
	//   must do this for now
	if ( m_numThreads > 0 && ( callback != m_callback || state != m_state ) ) {
		g_errno = EBADENGINEER;
		log(LOG_ERROR, "%s:%s:%d: END. Unlink/rename threads already in progress. ", __FILE__, __func__, __LINE__ );
		return true;
	}
	
	// . is this a rename?
	// . hack off any directory in newBaseFilename
	if ( newBaseFilename ) {
		// well, now Rdb.cpp's moveToTrash() moves an old rdb file
		// into the trash subdir, so we must preserve the full path
		const char *s ;
		while( (s=strchr(newBaseFilename,'/'))) {
			newBaseFilename = s+1;
		}

		// now this is dynamic to save mem when we have 100,000+ files
		m_newBaseFilename.reset();
		m_newBaseFilenameDir.reset();

		m_newBaseFilename.setLabel("nbfn");
		m_newBaseFilenameDir.setLabel("nbfnd");

		if (!m_newBaseFilename.safeStrcpy(newBaseFilename)) {
			log(LOG_ERROR, "%s:%s:%d: set m_newBaseFilename failed", __FILE__, __func__, __LINE__);
			logAllData(LOG_ERROR);
			return false;
		}

		if (!m_newBaseFilenameDir.safeStrcpy(newBaseFilenameDir)) {
			log(LOG_ERROR, "%s:%s:%d: set m_newBaseFilenameDir failed", __FILE__, __func__, __LINE__);
			logAllData(LOG_ERROR);
			return false;
		}
		
		// in case newBaseFilenameDir was NULL
		m_newBaseFilenameDir.nullTerm();
		
		// set the op flag
		m_isUnlink = false;

		logTrace( g_conf.m_logTraceBigFile, "Rename mode" );
	} else {
		m_isUnlink = true;
		logTrace( g_conf.m_logTraceBigFile, "Unlink mode" );
	}

	// . unlink likes to sometimes just unlink one part at a time
	// . this should be -1 to unlink all at once
	m_part = part;

	// the state varies
	void (*startRoutine)(void *state);
	void (*doneRoutine )(void *state, job_exit_t exit_type);

	const int32_t startPartNumber = (m_part >= 0) ? m_part : 0;

	// how many parts have we done?
	m_partsRemaining = m_maxParts;

	// is it only 1 to be unlinked?
	if ( m_part >= 0 ) {
		m_partsRemaining = 1;
	}

	if (m_isUnlink) {
		// First mark the files for unlink so no further read-jobs will be submitted for those parts
		for ( int32_t i = startPartNumber ; i < m_maxParts ; i++ ) {
			if ( m_part >= 0 && i != m_part )
				break;
			File *f = getFile2(i);
			if ( !f )
				continue;
			addPendingUnlink(f->getFilename());
		}
	
		//then cancel all queued read jobs for this bigfile
		if (part == -1 ) {
			// remove all queued threads that point to us that have not
			// yet been launched
			g_jobScheduler.cancel_file_read_jobs(this);
		}
	}

	
	//then prepare/submit the rename/unlink
	for ( int32_t i = startPartNumber; i < m_maxParts ; i++ ) {
		// break out if we should only unlink one part
		if ( m_part >= 0 && i != m_part ) {
			break;
		}

		// get the ith file to rename/unlink
		File *f = getFile2(i);
		if ( ! f ) {
			// one less part to do
			m_partsRemaining--;
			continue;
		}

		// remove it from disk
		if (  m_isUnlink ) {
			startRoutine = unlinkWrapper;
			doneRoutine  = doneUnlinkWrapper;
		} else {
			startRoutine = renameWrapper;
			doneRoutine  = doneRenameWrapper;
		}

		f->setForceRename( force );

		// assume thread launched, doneRoutine() will decrement these
		m_numThreads++; 
		g_unlinkRenameThreads++;

		UnlinkRenameState *job_state;
		try {
			job_state = new UnlinkRenameState(this, f, i);
		} catch (...) {
			g_errno = ENOMEM;
			log(LOG_WARN, "disk: Failed to allocate memory for unlink/rename for %s.", f->getFilename());
			return false;
		}

		mnew(job_state, sizeof(UnlinkRenameState), "UnlinkRenameState");

		// use thread?
		if ( useThread ) {
			// save callback for when all parts are unlinked or renamed
			m_callback = callback;
			m_state    = state;

			// . we spawn the thread here now
			// . returns true on successful spawning
			// . we can't make a disk thread cuz Threads.cpp checks its
			//   FileState member for readSize for thread throttling
			if ( g_jobScheduler.submit(startRoutine, doneRoutine, job_state, thread_type_unlink, 1/*niceness*/) ) {
				logTrace( g_conf.m_logTraceBigFile, "Thread function called OK" );
				continue;
			}

			// otherwise, thread spawn failed, do it blocking then
			log( LOG_INFO, "disk: Failed to launch unlink/rename thread for %s. Doing blocking unlink. "
					"part=%" PRId32"/%" PRId32". This will hurt performance.", f->getFilename(),i,m_part);
		}

		// log these for now, remove later
		log(LOG_DEBUG,"disk: Unlinking/renaming %s without thread.", f->getFilename());

		// before we call doneRoutine(), we must NULLify the callback 
		m_callback = NULL;

		// clear errno, cause startRoutine() may set it
		errno = 0;
		
		// these are normally called from a thread
		startRoutine ( job_state );
		
		// copy errno over to g_errno
		if ( errno ) {
			g_errno = errno;
		}
			
		// wrap it up
		doneRoutine  ( job_state , job_exit_normal );

		// don't delete job_state (already deleted in doneRoutine)
	}

	// if one blocked, we block, but never return false if !useThread
	if ( m_numThreads > 0 && useThread ) {
		logTrace( g_conf.m_logTraceBigFile, "m_numThreads [%" PRId32"] && useThread", m_numThreads );

		return false;
	}
	
	// . if we launched no threads update OUR base filename right now
	if ( ! m_isUnlink ) {
		m_baseFilename.set ( m_newBaseFilename.getBufStart() );
	}
		
	// we did not block
	return true;
}



void BigFile::renameWrapper(void *state) {
	UnlinkRenameState *job_state = static_cast<UnlinkRenameState*>(state);
	BigFile *that = job_state->m_bigfile;

	that->renameWrapper(job_state->m_file, job_state->m_i);

	if (g_errno && !job_state->m_errno) {
		job_state->m_errno = g_errno;
	}
}


void BigFile::renameWrapper(File *f, int32_t i) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN" );

	// . get the new full name for this file
	// . based on m_dir/m_stripeDir and m_baseFilename
	char newFilename [ 1024 ];
	makeFilename_r ( m_newBaseFilename.getBufStart(), m_newBaseFilenameDir.getBufStart(), i, newFilename, 1024 );

	log( LOG_TRACE,"%s:%s:%d: disk: rename [%s] to [%s]", __FILE__, __func__, __LINE__, f->getFilename(), newFilename );

	if (!f->rename(newFilename)) {
		g_errno = errno;
	}

	logTrace( g_conf.m_logTraceBigFile, "END" );
}



void BigFile::unlinkWrapper(void *state) {
	UnlinkRenameState *job_state = static_cast<UnlinkRenameState*>(state);
	BigFile *that = job_state->m_bigfile;

	that->unlinkWrapper(job_state->m_file);

	if (g_errno && !job_state->m_errno) {
		job_state->m_errno = g_errno;
	}
}


/// @todo ALC we could we use f->unlink here?
void BigFile::unlinkWrapper(File *f) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN" );

	//We have to wait for all running io-jobs reading from that File to
	//finish before unlinking+closing. otherwise the read threads will
	//refer to closed file descriptors or get unhappy when they can't
	//re-open the file. The problem is that JobScheduler doesn't have an
	//API for checking that, and FileState doesn't have a File* member
	//(because they can become invalid/deleted), so there is no easy correct
	//way of waiting for read jobs using that file to finishes. Insteads we
	//do it the hackish way: check if someone is reading the bigfile every
	//100ms, break after 5 seconds because then it is highly unlikely that
	//any unfinished job refers to that area/file anymore.
	for(int i=0; i<50; i++) {
		if(!g_jobScheduler.is_reading_file(this))
			break;
		usleep(100000); //sleep 100ms
	}

	log( LOG_TRACE,"%s:%s:%d: disk: unlink [%s]", __FILE__, __func__, __LINE__, f->getFilename() );

	//now real unlink
	::unlink ( f->getFilename() );

	if ( errno != 0 ) {
		log( LOG_TRACE,"%s:%s:%d: disk: unlink [%s] has error [%s]", __FILE__, __func__, __LINE__,
		     f->getFilename(), mstrerror( errno ) );
		g_errno = errno;
	}

	// we must close the file descriptor in the thread otherwise the
	// file will not actually be unlinked in this thread
	f->close1_r();

	removePendingUnlink(f->getFilename());

	logTrace( g_conf.m_logTraceBigFile, "END" );
}

void BigFile::doneRenameWrapper(void *state, job_exit_t /*exit_type*/) {
	UnlinkRenameState *job_state = static_cast<UnlinkRenameState*>(state);

	g_errno = job_state->m_errno;

	BigFile *that = job_state->m_bigfile;
	that->doneRenameWrapper(job_state->m_file);

	mdelete(job_state, sizeof(FileState), "FileState");
	delete job_state;
}

void BigFile::doneRenameWrapper(File *f) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN" );

	// one less
	m_numThreads--;
	g_unlinkRenameThreads--;

	// otherwise, it's a more serious error i guess
	if ( g_errno ) {
		log(LOG_ERROR, "%s:%s:%d: doneRenameWrapper. rename failed: [%s] [%s]", __FILE__, __func__, __LINE__, getFilename(), mstrerror(g_errno));
		logAllData(LOG_ERROR);
		//@@@ BR: Why continue??
	}

	// one less part to do
	m_partsRemaining--;
	
	// return if more to do
	if ( m_partsRemaining > 0 ) {
		logTrace( g_conf.m_logTraceBigFile, "END - still more parts" );
		return;
	}

	// update OUR base filename now after all Files are renamed
	m_baseFilename.reset();
	m_baseFilename.setLabel("nbfnn");
	m_baseFilename.safeStrcpy(m_newBaseFilename.getBufStart());

	// . all done, call the main callback
	// . this is NULL if we were not called in a thread
	if ( m_callback ) {
		m_callback ( m_state );
	}

	logTrace( g_conf.m_logTraceBigFile, "END" );
}


void BigFile::doneUnlinkWrapper(void *state, job_exit_t /*exit_type*/) {
	UnlinkRenameState *job_state = static_cast<UnlinkRenameState*>(state);

	g_errno = job_state->m_errno;

	BigFile *that = job_state->m_bigfile;
	that->doneUnlinkWrapper(job_state->m_file, job_state->m_i);

	mdelete(job_state, sizeof(FileState), "FileState");
	delete job_state;
}

void BigFile::doneUnlinkWrapper(File *f, int32_t i) {
	logTrace( g_conf.m_logTraceBigFile, "BEGIN" );

	//unmark file for deletion since it already has
	removePendingUnlink(f->getFilename());

	// finish the close
	f->close2();

	// one less
	m_numThreads--;
	g_unlinkRenameThreads--;

	// otherwise, it's a more serious error i guess
	if ( g_errno ) {
		log(LOG_ERROR, "%s:%s:%d: doneUnlinkWrapper. unlink failed: %s", __FILE__, __func__, __LINE__, mstrerror(g_errno));
		logAllData(LOG_ERROR);
		//@@@ BR: Why continue??
	}

	// . remove the part if it checks out
	// . this will also close the file when it deletes it
	File *fi = getFile2(i);
	if ( f == fi ) {
		removePart ( i );
	} else {
		log(LOG_ERROR, "%s:%s:%d: doneUnlinkWrapper. unlink had bad file ptr.", __FILE__, __func__, __LINE__ );
		logAllData(LOG_ERROR);
	}

	// one less part to do
	m_partsRemaining--;

	// return if more to do
	if ( m_partsRemaining > 0 ) {
		logTrace( g_conf.m_logTraceBigFile, "END - still more parts" );
		return;
	}

	// . all done, call the main callback
	// . this is NULL if we were not called in a thread
	if ( m_callback ) {
		m_callback ( m_state );
	}

	logTrace( g_conf.m_logTraceBigFile, "END" );
}

void BigFile::removePart ( int32_t i ) {
	//File *f = getFile2(i);
	File **filePtrs = (File **)m_filePtrsBuf.getBufStart();
	File *f = filePtrs[i];

	// . thread should have stored the filename for unlinking
	// . now delete it from memory
	if ( f == (File *)m_littleBuf ) {
		f->~File();
	} else {
		mdelete(f, sizeof(File), "BigFile");
		delete (f);
	}

	// and clear from our table
	filePtrs[i] = NULL;
	// we have one less part
	m_numParts--;
	// max part num may be different
	if ( m_maxParts != i+1 ) return;
	// set m_maxParts
	int32_t j;
	for ( j = i ; j >= 0 ; j-- ) {
		File *fj = filePtrs[j];
		if ( fj ) { m_maxParts = j+1; break; }
	}
	// may have no more part files left which means no max part num
	if ( j < 0 ) m_maxParts = 0;
}

// used by RdbMap after reading in during start up, we don't want to waste
// all the fds, but we can't call BigFile::close() because then RdbMap::unlink
// doesn't work.
bool BigFile::closeFds ( ) {
	for ( int32_t i = 0 ; i < m_maxParts ; i++ ) {
		File *f = getFile2(i);
		if ( ! f ) continue;
		f->close();
	}
	return true;
}

bool BigFile::close ( ) {
	// do not double call this
	if ( m_isClosing ) return true;
	// this end up being called again through a sequence of like 20
	// subroutines, so put a stop to that circle
	m_isClosing = true;
	File **filePtrs = (File **)m_filePtrsBuf.getBufStart();
	for ( int32_t i = 0 ; i < m_maxParts ; i++ ) {
		File *f = filePtrs[i];
		if ( ! f ) continue;
		// remove from our array of File ptrs
		filePtrs[i]   = NULL;
		// the destructor calls close, no need to call here
		//f->close();
		//f->destructor();
		// if we were using the stack buf in BigFile then just
		// call File::destructor()
		if ( f == (File *)m_littleBuf ) {
			f->~File();
			continue;
		}
		// otherwise, delete as we normally would
		mdelete ( f , sizeof(File) , "BigFile" );
		delete ( f );
	}
	m_numParts   = 0;
	m_maxParts   = 0;

	// remove all queued threads that point to us that have not
	// yet been launched
	g_jobScheduler.cancel_file_read_jobs(this);
	return true;
}
