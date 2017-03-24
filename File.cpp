#include "gb-include.h"

#include "File.h"
#include "Conf.h"
#include "Loop.h"            // MAX_NUM_FDS etc.
#include "GbMoveFile2.h"
#include "Sanity.h"
#include "ScopedLock.h"
#include "GbMutex.h"
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>       // for open/lseek
#include <sys/stat.h>        // for open

// THE FOLLOWING IS ALL STATIC 'CUZ IT'S THE FD POOL
// if someone is using a file we must make sure this is true...
static bool	s_isInitialized = false;

static GbMutex s_mtx;
static int64_t s_timestamps [ MAX_NUM_FDS ]; // when was it last accessed
static bool    s_writing    [ MAX_NUM_FDS ]; // is it being written to?
static bool    s_unlinking  [ MAX_NUM_FDS ]; // is being unlinked/renamed
static bool    s_open       [ MAX_NUM_FDS ]; // is opened?
static File   *s_filePtrs   [ MAX_NUM_FDS ];

// . how many open files are we allowed?? hardcode it!
// . rest are used for sockets
// . we use 512 for sockets as of now
// . this linux kernel has 1024 fd's
// . i saw the tcp server using 211 sockets when spidering, must be doing
//   a lot of robots.txt lookups! let's set this down from 800 to 500
static const int s_maxNumOpenFiles = 500;
static int       s_numOpenFiles    = 0;

// . keep track of number of times an fd was closed
// . so if we do a read on an fd, and it gets unlinked and a new file opened
//   with that same fd, we know it, and can compensate in BigFile.cpp for it
// . here is the updated sequence:
//   a. read begins with fd1
//   -> read stores s_closeCounts[fd1] in FState
//   b. we close fd1
//   -> we do s_closeCounts[fd1]++
//   c. we open another file with fd1
//   d. read reads the wrong file!
//   -> s_closeCounts[fd1] changed so g_errno is set.
// . UPDATE: now we just inc s_closeCounts[fd1] write after calling ::open().
//           Since ::open() is never called in a thread, this should be ok,
//           because i now call ::close1_r() in the unlink or rename thread.

#include "Loop.h" // MAX_NUM_FDS
static int32_t s_closeCounts [ MAX_NUM_FDS ];

static void sanityCheck ( ) {
	if ( ! g_conf.m_logDebugDisk ) {
		log("disk: sanity check called but not in debug mode");
		return;
	}
	int32_t openCount = 0;
	for ( int i = 0 ; i < MAX_NUM_FDS ; i++ )
		if ( s_open[i] ) openCount++;
	if ( openCount != s_numOpenFiles ) gbshutdownCorrupted();
}


// for avoiding unlink/opens that mess up our threaded read
int32_t getCloseCount_r ( int fd ) {
	if ( fd < 0 ) return 0;
	if ( fd >= MAX_NUM_FDS ) {
		log( LOG_WARN, "disk: got fd of %i out of bounds 2 of %i", fd,(int)MAX_NUM_FDS);
		return 0;
	}
	return s_closeCounts [ fd ];
}


File::File ( ) {
	m_fd = -1;

	// initialize m_maxFileSize and the virtual fd table
	if ( ! s_isInitialized ) {
		initialize ();
	}

	m_calledOpen = false;
	m_calledSet  = false;
	m_closedIt	= false;
	m_closeCount = 0;
	m_flags = 0;

	pthread_mutex_init(&m_mtxFdManipulation,NULL);
	
	logDebug( g_conf.m_logDebugDisk, "disk: constructor fd %i this=0x%" PTRFMT, m_fd, (PTRTYPE)this );
}


File::~File ( ) {
	logDebug( g_conf.m_logDebugDisk, "disk: destructor fd %i this=0x%" PTRFMT, m_fd, (PTRTYPE)this );

	close ();

	// set m_calledSet to false so BigFile.cpp see it as 'empty'
	m_calledSet  = false;
	m_calledOpen = false;
	
	pthread_mutex_destroy(&m_mtxFdManipulation);
}


void File::set ( const char *dir , const char *filename ) {
	if ( ! dir ) { set ( filename ); return; }
	char buf[1024];
	if ( dir[strlen(dir)-1] == '/' )
		snprintf ( buf , 1020, "%s%s" , dir , filename );
	else
		snprintf ( buf , 1020, "%s/%s" , dir , filename );
	set ( buf );
}

void File::set ( const char *filename ) {
	// reset m_filename
	m_filename[0] = '\0';

	// return if NULL
	if ( ! filename ) {
		log ( LOG_LOGIC,"disk: Provided filename is NULL");
		return;
	}

	// bail if too long
	int32_t len = strlen ( filename );
	// account for terminating '\0'
	if ( len + 1 >= MAX_FILENAME_LEN ) {
		log ( LOG_ERROR, "disk: Provided filename %s length of %" PRId32" is bigger than %" PRId32".",
	          filename, len, (int32_t)MAX_FILENAME_LEN-1);
	 	return;
	}

	// copy into m_filename
	memcpy ( m_filename, filename, len+1 );

	m_calledSet  = true;
	// TODO: make this a bool returning function if ( ! m_filename ) g_log
}

bool File::rename ( const char *newFilename ) {
	if ( ::access(newFilename, F_OK) == 0 ) {
		// new file exists
		logError("disk: trying to rename [%s] to [%s] which exists.", getFilename(), newFilename);
		gbshutdownLogicError();
	}

	if ( ::rename(getFilename(), newFilename) != 0 ) {
		// reset errno if file does not exist
		if ( errno == ENOENT ) {
			logError("disk: file [%s] does not exist.", getFilename());
			errno = 0;
		} else {
			logError("disk: rename [%s] to [%s]: [%s]", getFilename(), newFilename, mstrerror(errno));
		}
		logTrace( g_conf.m_logTraceFile, "END" );
		return false;
	}

	// set to our new name
	set ( newFilename );

	return true;
}


bool File::movePhase1(const char *newFilename) {
	logTrace(g_conf.m_logTraceFile, "BEGIN oldFilename='%s' newFilename='%s'", getFilename(), newFilename);

	if(::access( newFilename,F_OK) == 0) {
		logError("disk: trying to rename [%s] to [%s] which exists.", getFilename(), newFilename);
		gbshutdownLogicError();
	}
	if(moveFile2Phase1(getFilename(), newFilename) != 0) {
		logTrace(g_conf.m_logTraceFile, "END newFilename='%s'. Returning false", newFilename);
		return false;
	}

	logTrace(g_conf.m_logTraceFile, "END newFilename='%s'. Returning true", newFilename);
	return true;
}

bool File::movePhase2(const char *newFilename) {
	logTrace(g_conf.m_logTraceFile, "BEGIN oldFilename='%s' newFilename='%s'", getFilename(), newFilename);

	if(moveFile2Phase2(getFilename(), newFilename) != 0) {
		logTrace(g_conf.m_logTraceFile, "END newFilename='%s'. Returning false", newFilename);
		return false;
	}

	set(newFilename);

	// only redirect fd if it's valid
	if (m_fd != -1) {
		// ensure that we release the original file if the move was across filesystems
		// we don't call close directly here because we may have some pending reads for the fd,
		// so we redirect existing fd to new file

		// don't use m_flags here as last open could be writeMap
		int fd = ::open(getFilename(), O_RDWR, getFileCreationFlags());
		if (fd == -1) {
			logError("Unable to open %s", getFilename());
			gbshutdownResourceError();
		}

		if (dup2(fd, m_fd) == -1) {
			logError("dup2 failed with error=%s", mstrerror(errno));
			gbshutdownResourceError();
		}

		::close(fd);
	}

	logTrace(g_conf.m_logTraceFile, "END newFilename='%s'. Returning true", newFilename);
	return true;
}


void File::rollbackMovePhase1(const char *newFilename) {
	logTrace(g_conf.m_logTraceFile, "BEGIN oldFilename='%s' newFilename='%s'", getFilename(), newFilename);

	if(::unlink(newFilename)!=0) {
		if(errno!=ENOENT)
			log(LOG_ERROR, "%s:%s:%d: disk: trying to rollback rename-phase1 [%s] to [%s], unlink() failed with errno=%d (%s).", __FILE__, __func__, __LINE__,
			    getFilename(), newFilename, errno, strerror(errno));
		else
			log(LOG_WARN, "%s:%s:%d: disk: trying to rollback rename-phase1 [%s] to [%s], unlink() failed with errno=%d (%s).", __FILE__, __func__, __LINE__,
			    getFilename(), newFilename, errno, strerror(errno));
	}
	//yes, we return void because when a rollback doesn't work then there isn't much we can do
	logTrace(g_conf.m_logTraceFile, "END newFilename='%s'", newFilename);
}


// . open the file
// . only call once per File after calling set()
bool File::open(int flags) {
	ScopedLock sl(m_mtxFdManipulation);
	return open_unlocked(flags,getFileCreationFlags());
}

bool File::open_unlocked(int flags, int permissions) {
	// if we already had another file open then we must close it first.
	if ( m_fd >= 0 ) {
		log( LOG_LOGIC, "disk: Open already called. Closing and re-opening." );
		close_unlocked();
	}

	// save these in case we need to reopen in getfd()
	m_flags       = flags;
	m_calledOpen  = true;

	// open for real, return true on success
	if ( getfd() >= 0 ) {
		return true;
	}

	// log the error
	log( LOG_ERROR, "disk: open: %s", mstrerror( g_errno ) );

	// . close the virtual fd so we can call open again
	// . sets s_fds [ m_vfd ] to -2 (available)
	// . and sets our m_vfd to -1
	close_unlocked();

	// otherwise bitch and return false
	return false;
}

// . returns number of bytes written
// . returns -1 on error
// . may return < numBytesToWrite if non-blocking
int File::write(const void *buf, int32_t  numBytesToWrite, int32_t offset) {
	// safety catch!
	if ( g_conf.m_readOnlyMode ) {
		logf(LOG_DEBUG,"disk: Trying to write while in read only mode.");
		return -1;
	}

	// this return -2 if never opened, -1 on error, fd on success
	int fd = getfd();
	if ( fd < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "disk: write: fd is negative");
		return -1;
	}
	// write it
	int n;
	if ( offset < 0 ) n = ::write ( fd , buf , numBytesToWrite );
	else              n =  pwrite ( fd , buf , numBytesToWrite , offset );
	// update linked list
	//promoteInLinkedList ( this );
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }

	// log an error
	if ( n < 0 ) {
		log( LOG_WARN, "disk: write(%s) : %s", getFilename(), strerror( g_errno ) );
	}

	return n;
}

int File::read(void *buf, int32_t numBytesToRead, int32_t offset) {
	// this return -2 if never opened, -1 on error, fd on success
	int fd = getfd();
	if ( fd < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "disk: read: fd is negative");
		return -1;
	}
	// do the read
	int n ;
	if ( offset < 0 ) n = ::read  ( fd , buf , numBytesToRead );
	else              n =  pread  ( fd , buf , numBytesToRead , offset );
	// update linked list
	//promoteInLinkedList ( this );
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	if ( n < 0 ) {
		log( LOG_WARN, "disk: read(%s) : %s", getFilename(), strerror( g_errno ) );
	}
	return n;
}

// uses lseek to get file's current position
int32_t File::getCurrentPos() const {
	return (int32_t) ::lseek ( m_fd , 0 , SEEK_CUR );
}



void File::close1_r ( ) {
	ScopedLock sl(s_mtx);
	return close1_r_unlocked();
}


// . BigFile calls this from inside a rename or unlink thread
// . it calls File::close() proper when out of the thread
// . PROBLEM #1: we close this fd, an open happens for the fd we just closed
//               and a pending read reads from the wrong fd. to fix this
//               i inc'd s_closeCountds[fd] right after the call to ::open()
//               BUT what if it is opened by a socket???!?!?!?! Then the
//               read should have got EBADF i guess...
// . otherwise, any read for this fd might fail with BADFD if it got closed
//   before or during the read. in that case BigFile should retry
// . PROBLEM #2: yeah, but if its a write, what then? if opened for writing,
//               NEVER allow the fd to be closed in closeLeastUsed()!!!
//               because if merge and dump going on at same time, and both get
//               their fds closed in closedLeastUsed(), then merge reopens his
//               file but with dumps fd, and a dump in mid thread using the
//               same old fd writes, he will write to the merge file!!!
void File::close1_r_unlocked() {
	// assume no close
	m_closedIt = false;

	// debug. don't log in thread - might hurt us
	log(LOG_DEBUG,"disk: close1_r: Closing fd %i for %s after unlink/rename.",m_fd,getFilename());

	// problem. this could be a closed map file, m_vfd=-1.
	if ( m_fd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_DEBUG,"disk: close1_r: fd %i < 0",m_fd);
		return ;
	}

	// panic!
	if ( s_writing [ m_fd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In write mode and closing.");
		return;
	}
	// if already being unlinked, skip
	if ( s_unlinking [ m_fd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In unlink mode and closing.");
		return;
	}

	// . do not allow closeLeastUsed to close this fd as well
	// . that can really mess us up:
	// . 1. we close this fd being unlinked/renamed
	// . 2. another file gets that fd
	// . 3. closeLeastUsed closes it again and sets our s_fds[m_vfd] to -1
	//      this leaving the other file with a seemingly valid fd that
	//      always gives EBADF errors cuz it was closed.
	s_unlinking [ m_fd ] = true;

	if ( m_fd == 0 ) {
		log( LOG_WARN, "disk: closing1 fd of 0" );
	}

	if ( ::close(m_fd) == 0 ) {
		m_closedIt = true;
		// close2() needs to see m_fd so it can set flags...
		// so m_fd MUST be intact
		//m_fd = -1;
		return;
	}
	log( LOG_WARN, "disk: close(%i): %s.", m_fd, strerror(errno) );
}



void File::close2() {
	ScopedLock sl(s_mtx);
	return close2_unlocked();
}


// . just update the counts
// . BigFile.cpp calls this when done unlinking/renaming this file
void File::close2_unlocked() {
	// if already gone, bail. this could be a closed map file, m_vfd=-1.
	if ( m_fd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_INFO,"disk: close2: fd %i < 0",m_fd);
		return;
	}

	// clear for later, but only if nobody else got our fd when opening
	// a file... because we called close() in a thread in close1_r()
	if ( s_filePtrs [ m_fd ] == this )
		s_unlinking [ m_fd ] = false;

	// return if we did not actually do a close in close1_r()
	if ( ! m_closedIt ) {
		// this can happen if the fd was always -1 before call to
		// close1_r(), like when deleting a map file... so we never
		// needed to call ::close() in close1_r().
		return;
	}

	if ( g_conf.m_logDebugDisk ) {
		sanityCheck();
	}

	// save this for stuff below
	int fd = m_fd;

	// now it is closed. do not try to re-close in destructor's call to
	// close() so set m_fd to -1
	m_fd = -1;

	// mark it as closed
	// CAUTION: since we closed the fd in a thread in close1_r() it may
	// have been returned for another file, so check here. make sure we are
	// still considered the 'owner'. if not then we were supplanted in
	// File::getfd() and s_numOpenFiles-- was called there as well so
	// we should skip everything below here.
	if ( s_filePtrs [ fd ] != this ) return;

	s_open        [ fd ] = false;
	s_filePtrs    [ fd ] = NULL;
	// i guess there is no need to do this close count inc
	// if we lost our fd already shortly after our thread closed
	// the fd, otherwise we'll falsely mess up the new owner
	// and he will do a re-read.
	s_closeCounts [ fd ]++;

	// to keep our sanityCheck() from coring, only decrement this
	// if we owned it still
	s_numOpenFiles--;

	//s_closeCounts [ fd ]++;

	logDebug( g_conf.m_logDebugDisk, "disk: close2 fd %i for %s #openfiles=%i this=0x%" PTRFMT,
	          fd,getFilename(), s_numOpenFiles, (PTRTYPE)this );

	if ( g_conf.m_logDebugDisk ) sanityCheck();
}

// . return -2 on error
// . return -1 if does not exist
// . return 0-N otherwise
// . closes the file for real!
// . analogous to a reset() routine
bool File::close ( ) {
	ScopedLock sl(m_mtxFdManipulation);
	return close_unlocked();
}

bool File::close_unlocked() {
	// return true if not open
	if ( m_fd < 0 ) return true;

	ScopedLock sl(s_mtx);

	// panic!
	if ( s_writing [ m_fd ] ) {
		log(LOG_LOGIC, "disk: In write mode and closing 2.");
		return false;
	}
	// if already being unlinked, skip
	if ( s_unlinking [ m_fd ] ) {
		log(LOG_LOGIC, "disk: In unlink mode and closing 2.");
		return false;
	}
	// . tally up another close for this fd, if any
	// . so if an open happens shortly here after, and
	//   gets this fd, then any read that was started
	//   before that open will know it!
	//s_closeCounts [ fd ]++;

	// otherwise we gotta really close it

	if ( g_conf.m_logDebugDisk ) sanityCheck();

	if ( m_fd == 0 ) log("disk: closing2 fd of 0");
	int status = ::close ( m_fd );
	// there was a closing error if status is non-zero. --- not checking
	// the error may lead to silent loss of data --- see "man 2 close"
	if ( status != 0 ) {
		log( LOG_WARN, "disk: close(%s) : %s", getFilename(), mstrerror(g_errno) );
		return false;
	}
	// sanity
	if ( ! s_open[m_fd] ) gbshutdownCorrupted();
	// mark it as closed
	s_open        [ m_fd ] = false;
	s_filePtrs    [ m_fd ] = NULL;
	s_closeCounts [ m_fd ]++;
	// otherwise decrease the # of open files
	s_numOpenFiles--;

	logDebug( g_conf.m_logDebugDisk, "disk: close0 fd %i for %s #openfiles=%i", m_fd,getFilename(), s_numOpenFiles );

	// set this to -1 to indicate closed
	m_fd = -1;

	// return true blue
	if ( g_conf.m_logDebugDisk ) sanityCheck();
	return true;
}


// . get the fd of this file
// . if it was closed by us we reopen it
// . may re-open a virtual fd whose real fd was closed
// . if we hit our max # of real fds allowed we'll have to close
//   the least used of those so we can open this one
// . return -2 if never been opened
// . return -1 on other errors
// . otherwise, return the file descriptor
int File::getfd () {
	if ( m_fd >= MAX_NUM_FDS ) {
		gbshutdownCorrupted();
	}

	// if m_vfd is -1 it's never been opened
	if ( ! m_calledOpen ) { // m_vfd < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"disk: getfd: Must call open() first.");
		gbshutdownLogicError();
	}

	ScopedLock sl(s_mtx);
	
	// if someone closed our fd, why didn't our m_fd get set to -1 ??!?!?!!
	if ( m_fd >= 0 && m_closeCount != s_closeCounts[m_fd] ) {
		log(LOG_DEBUG,"disk: invalidating existing fd %i "
		    "for %s this=0x%" PTRFMT" ccSaved=%i ccNow=%i",
		    (int)m_fd,getFilename(),(PTRTYPE)this,
		    (int)m_closeCount,
		    (int)s_closeCounts[m_fd]);
		m_fd = -1;
	}

	// return true if it's already opened
	if ( m_fd >=  0 ) {
		logDebug( g_conf.m_logDebugDisk, "disk: returning existing fd %i for %s this=0x%" PTRFMT" ccSaved=%i ccNow=%i",
		          m_fd,getFilename(),(PTRTYPE)this, (int)m_closeCount, (int)s_closeCounts[m_fd] );

		// but update the timestamp to reduce chance it closes on us
		s_timestamps [ m_fd ] = gettimeofdayInMillisecondsLocal();
		return m_fd;
	}

	// . a real fd of -1 means it's been closed and we gotta reopen it
	// . we have to close someone if we don't have enough room
	while ( s_numOpenFiles >= s_maxNumOpenFiles )  {
		if ( g_conf.m_logDebugDisk ) {
			sanityCheck();
		}

		if ( ! closeLeastUsed() ) {
			return -1;
		}

		if ( g_conf.m_logDebugDisk ) {
			sanityCheck();
		}
	}

	// time the calls to open just in case they are hurting us
	int64_t t1 = gettimeofdayInMilliseconds();
	// then try to open the new name
	int fd = ::open ( getFilename(), m_flags, getFileCreationFlags());
	// 0 means stdout, right? why am i seeing it get assigned???
	if ( fd == 0 ) {
		log(LOG_WARN, "disk: Got fd of 0 when opening %s.", getFilename());
		fd=::open(getFilename(), m_flags, getFileCreationFlags());
	}
	if ( fd == 0 ) {
		log(LOG_WARN, "disk: Got fd of 0 when opening2 %s.", getFilename());
	}
	if ( fd >= MAX_NUM_FDS ) {
		log(LOG_WARN, "disk: got fd of %i out of bounds 1 of %i - FAILING File::getfd", fd, (int)MAX_NUM_FDS);
		::close(fd);
		return -1;
	}

	// copy errno to g_errno
	if ( fd <= -1 ) {
		g_errno = errno;
		log( LOG_ERROR, "disk: error open(%s) : %s fd %i", getFilename(), strerror(g_errno), fd );
		return -1;
	}



	// if we got someone else's fd that called close1_r() in a
	// thread but did not have time to call close2() to fix
	// up these member vars, then do it here. close2() will
	// see that s_filePtrs[fd] does not equal the file ptr any more
	// and it will not update s_numOpenFiles in that case.
	if ( s_open [ fd ] ) {
		File *f = s_filePtrs [ fd ];
		logDebug( g_conf.m_logDebugDisk, "disk: swiping fd %i from %s before his close thread returned this=0x%" PTRFMT,
			  fd, f->getFilename(), (PTRTYPE)f );

		// he only incs/decs his counters if he owns it so in
		// close2() so dec this global counter here
		s_numOpenFiles--;
		s_open[fd] = false;
		s_filePtrs[fd] = NULL;
		if ( g_conf.m_logDebugDisk ) {
			sanityCheck();
		}
	}

	// sanity. how can we get an fd already opened?
	// because it was closed in a thread in close1_r()
	if ( s_open[fd] ) {
		gbshutdownCorrupted();
	}

	// . now inc that count in case there was someone reading on
	//   that fd right before it was closed and we got it
	// . ::close() call can now happen in a thread, so we
	//   need to inc this guy here now, too
	// . so when that read returns it will know to re-do
	// . this should really be named s_openCounts!!
	s_closeCounts [ fd ]++;

	// . we now record this
	// . that way if our fd gets closed in closeLeastUsed() or
	//   in close1_r() due to a rename/unlink then we know it!
	// . this fixes a race condition of closeCounts in Threads.cpp
	//   where we did not know that the fd had been stolen from
	//   us and assigned to another file because our close1_r()
	//   had called ::close() on our fd and our closeCount algo
	//   failed us. see the top of this file for more description
	//   into this bug fix.
	m_closeCount = s_closeCounts[fd];

	if ( t1 >= 0 ) {
		int64_t dt = gettimeofdayInMilliseconds() - t1 ;
		if ( dt > 1 ) {
			log( LOG_INFO, "disk: call to open(%s) blocked for %" PRId64" ms.", getFilename(), dt );
		}
	}

	if ( g_conf.m_logDebugDisk ) {
		sanityCheck();
	}

	// we're another open file
	s_numOpenFiles++;

	// debug log
	logDebug( g_conf.m_logDebugDisk, "disk: opened1 fd %i for %s #openfiles=%i this=0x%" PTRFMT,
	          fd, getFilename(), s_numOpenFiles, (PTRTYPE)this );

	// set this file descriptor, the other stuff remains the same
	m_fd = fd;

	// reset
	s_writing   [ fd ] = false;
	s_unlinking [ fd ] = false;
	s_timestamps[ fd ] = gettimeofdayInMillisecondsLocal();
	s_open      [ fd ] = true;
	s_filePtrs  [ fd ] = this;

	if ( g_conf.m_logDebugDisk ) {
		sanityCheck();
	}

	return fd;
}


// close the least used of all the file descriptors.
// we don't touch files opened for writing, however.
bool File::closeLeastUsed () {
	int64_t min  ;
	int    mini = -1;
	int64_t now = gettimeofdayInMillisecondsLocal();


	int32_t notopen = 0;
	int32_t writing = 0;
	int32_t unlinking = 0;
	int32_t young = 0;

	// get the least used of all the actively opened file descriptors.
	// we can't get files that were opened for writing!!!
	int i;
	for ( i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		//if ( s_fds   [ i ] < 0        ) continue;
		if ( ! s_open[i] ) { notopen++; continue; }
		// fds opened for writing are not candidates, because if
		// we close on a threaded write, that fd may be used to
		// re-open another file which gets garbled!
		if ( s_writing [ i ] ) { writing++; continue; }
		// do not close guys being unlinked they are in the middle
		// of being closed ALREADY in close1_r(). There should only be
		// like one unlink thread allowed to be active at a time so we
		// don't have to worry about it hogging all the fds.
		if ( s_unlinking [ i ] ) { unlinking++; continue; }
		// when we got like 1000 reads queued up, it uses a *lot* of
		// memory and we can end up never being able to complete a
		// read because the descriptors are always getting closed on us
		// so do a hack fix and do not close descriptors that are
		// about .5 seconds old on avg.
		if ( s_timestamps [ i ] == now ) { young++; continue; }
		if ( s_timestamps [ i ] == now - 1 ) { young++; continue; }
		if ( mini == -1 || s_timestamps [ i ] < min ) {
			min  = s_timestamps [ i ];
			mini = i;
		}
	}

	// if nothing to free then return false
	if ( mini == -1 ) {
		log( LOG_WARN, "File: closeLeastUsed: failed. All %" PRId32" descriptors are unavailable to be "
		            "closed and re-used to read from another file. notopen=%i writing=%i unlinking=%i young=%i",
		            ( int32_t ) s_maxNumOpenFiles, notopen, writing, unlinking, young );
		return false;
	}


	int fd = mini;

	// . tally up another close for this fd, if any
	// . so if an open happens shortly here after, and
	//   gets this fd, then any read that was started
	//   before that open will know it!
	//s_closeCounts [ fd ]++;

	// otherwise we gotta really close it
	log(LOG_INFO,"disk: Closing fd %d due to fd pressure", fd);
	if ( fd == 0 ) log("disk: closing3 fd of 0");
	int status = ::close ( fd );

	// -1 means can be reopened because File::close() wasn't called.
	// we're just conserving file descriptors
	//s_fds [ mini ] = -1;

	// if the real close was successful then decrement the # of open files
	if ( status == 0 ) {
		// it's not open
		s_open     [ fd ] = false;
		// if someone is trying to read on this let them know
		s_closeCounts [ fd ]++;

		s_numOpenFiles--;

		File *f = s_filePtrs [ fd ];
		// don't let him use the stolen fd
		f->m_fd = -1 ;

		// debug msg
		if ( g_conf.m_logDebugDisk ) {
			File *f = s_filePtrs [ fd ];
			const char *fname = "";
			if ( f ) fname = f->getFilename();
			logf(LOG_DEBUG,"disk: force closed fd %i for"
			     " %s. age=%" PRId64" #openfiles=%i this=0x%" PTRFMT,
			     fd,fname,now-s_timestamps[mini],
			     (int)s_numOpenFiles,
			     (PTRTYPE)this);
		}

		// no longer the owner
		s_filePtrs [ fd ] = NULL;

		// excise from linked list of active files
		//rmFileFromLinkedList ( f );
		// getfd() may not execute in time to ince the closeCount
		// so do it here. test by setting the max open files to like
		// 10 or so and spidering heavily.
		//s_closeCounts [ fd ]++;
	}


	if ( status == -1 ) {
		log( LOG_WARN, "disk: close(%i) : %s", fd, strerror( errno ) );
		return false;
	}

	if ( g_conf.m_logDebugDisk ) sanityCheck();

	return true;
}


int64_t getFileSize ( const char *filename ) {
	struct stat stats;

	int status = stat ( filename , &stats );

	// return the size if the status was ok
	if ( status == 0 ) {
		return stats.st_size;
	}

	// copy errno to g_errno
	g_errno = errno;

        // return 0 and reset g_errno if it just does not exist
	if ( g_errno == ENOENT ) { g_errno = 0; return 0; }

	// log & return -1 on any other error
	log( LOG_ERROR, "disk: error getFileSize(%s) : %s", filename, strerror( g_errno ) );
	return -1;
}


// . returns -2 on error
// . returns -1 if does not exist
// . otherwise returns file size in bytes
int64_t File::getFileSize() const {
	return ::getFileSize( getFilename() );
}

// . return 0 on error
time_t File::getLastModifiedTime() const {
	struct stat stats;
	if ( stat ( getFilename() , &stats ) == 0 ) {
		return stats.st_mtime;
	} else {
		g_errno = errno;
		log( LOG_WARN, "disk: error stat2(%s) : %s", getFilename(), strerror( g_errno ) );
		return 0;
	}
}


// . returns -1 on error
// . returns  0 if does not exist
// . returns  1 if it exists
int32_t File::doesExist() const {
	// preserve g_errno
	int old_errno = g_errno;

	// allow the substitution of another filename
	struct stat stats;

	// return true if it exists
	if ( stat ( getFilename() , &stats ) == 0 ) {
		return 1;
	}

	// copy errno to g_errno
	g_errno = errno;

	// return 0 if it just does not exist and reset g_errno
	if ( g_errno == ENOENT ) {
		g_errno = old_errno;
		return 0;
	}

	// resource temporarily unavailable (for newer libc)
	if ( g_errno == EAGAIN ) {
		g_errno = old_errno;
		return 0;
	}

	log( LOG_ERROR, "disk: error stat3(%s): %s", getFilename() , strerror(g_errno));
	return -1;
}


bool File::unlink() {
	logTrace( g_conf.m_logTraceFile, "BEGIN. filename [%s]", getFilename() );

	// safety catch!
	if ( g_conf.m_readOnlyMode ) {
		logf(LOG_DEBUG,"disk: Trying to unlink while in read only mode.");
		return false;
	}

	// give the fd back to the pull, free the m_vfd
	close ();

	// avoid unneccessary unlinking
	int32_t status = doesExist();
	// return true if we don't exist anyway
	if ( status == 0 ) {
		logTrace( g_conf.m_logTraceFile, "END - file did not exist. Return true." );
		return true;
	}

	// return false and set g_errno on error
	if ( status  < 0 ) {
		logTrace( g_conf.m_logTraceFile, "END - Error %" PRId32" calling doesExists. Return false.", status );
		return false;
	}


	// . log it so we can see what happened to timedb!
	// . don't log startup unlinks of "tmpfile"
	if ( ! strstr(getFilename(),"tmpfile") ) {
		log(LOG_INFO,"disk: unlinking %s", getFilename() );
	}


	// remove ourselves from the disk
	if ( ::unlink ( getFilename() ) == 0 ) {
		logTrace( g_conf.m_logTraceFile, "END - OK, returning true." );
		return true;
	}

	// copy errno to g_errno
	g_errno = errno;

	// return false and set g_errno on error
	log(LOG_ERROR,"%s:%s: END. Unlinking [%s] caused error [%s]. Returning false.", __FILE__,__FUNCTION__, getFilename(),strerror(g_errno));
	return false;
}


// called by File::open() when it's found out that we're not initialized.
bool File::initialize ( ) {
	ScopedLock sl(s_mtx);

	if ( s_isInitialized ) return true;

	//	log ( 0 , "file::initialize: running");

	// reset all the virtual file descriptos
	for ( int i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		s_timestamps  [ i ] = 0LL;
		s_writing     [ i ] = false;
		s_unlinking   [ i ] = false;
		s_open        [ i ] = false;
		s_closeCounts [ i ] = 0;
		s_filePtrs    [ i ] = NULL;
	}

	s_isInitialized = true;

	return true;
}

const char *File::getExtension() const {
	// keep backing up over m_filename till we hit a . or / or beginning
	const char *f = getFilename();
	int32_t i = strlen(m_filename);
	while ( --i > 0 ) {
		if ( f[i] == '.' ) break;
		if ( f[i] == '/' ) break;
	}
	if ( i == 0               ) return NULL;
	if ( f[i] == '/' ) return NULL;
	return &f[i+1];
}

// We do not close the fd in closeLeastUsed() for fear of getting it
// reassigned mid-thread and getting a write into a file that should not
// have had it. this can be *VERY* bad. like if one file is being merged
// into and a file being dumped. both lose their fd from closeLeastUsed()
// and the merge guy gets a new fd which happens to be the old fd of the
// dump, so when the dump thread lets its write go it writes into the merge
// file.
void enterWriteMode ( int fd ) {
	if ( fd >= 0 ) s_writing [ fd ] = true;
}
void exitWriteMode ( int fd ) {
	if ( fd >= 0 ) s_writing [ fd ] = false;
}
