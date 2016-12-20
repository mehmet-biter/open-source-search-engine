// Copyright Sep 2000 Matt Wells

// . this class makes the 2gb file limit transparent
// . you can actually set you own file size limit to whatever you want as 
//   int32_t as it's greater than zero
// . this is usually 2gb
// . TODO: fix the O_SYNC option
// . TODO: provide ability to pass flags

#ifndef GB_BIGFILE_H
#define GB_BIGFILE_H

#include "JobScheduler.h" //for job_exit_t
#include "SafeBuf.h"
#include "GbMutex.h"


#ifndef PRIVACORE_TEST_VERSION
// . use 512 disk megs per part file
// . at a GB_PAGE_SIZE of 16k this is 32k RdbMap pages (512k) per part file
// . let's use 2GB now to conserve file descriptors
#define MAX_PART_SIZE  (1920LL*1024LL*1024LL)
#else
#define MAX_PART_SIZE  (20LL*1024LL*1024LL)
#endif

class File;


class FileState {
public:
	// callback must be top 4 bytes of the state class we give to g_loop
	// callback must be first X bytes
	class BigFile  *m_bigfile;

	char           *m_buf;
	int64_t            m_bytesToGo;
	int64_t       m_offset;
	// . the original offset, because we set m_offset to m_currentOffset
	//   if the original offset specified is -1
	// . we also advance BigFile::m_currentOffset when done w/ read/write
	//int64_t       m_origOffset;
	bool            m_doWrite;
	int64_t            m_bytesDone;
	void           *m_state ;
	void          (*m_callback) ( void *state ) ;
	// goes from 0 to 1, the lower the niceness, the higher the priority
	int32_t            m_niceness;

	// . we get our fds before starting the read thread to avoid
	//   problems with accessing m_files since RdbMerge may call unlinkPart
	//   from the main thread while we're trying to get these things
	// . no read should span more than 2 file descriptors
	int32_t            m_filenum1;
	int32_t            m_filenum2;
	int             m_fd1 ;
	int             m_fd2 ;
	char            m_filename1[1024];
	char            m_filename2[1024];
	// hold the errno from the threaded read/write here
	int32_t            m_errno;
	// when we started for graphing purposes (in milliseconds)
	int64_t       m_startTime;
	int64_t       m_doneTime;

	// it is
	// a "virtual fd" for this whole file
	int64_t            m_vfd;

	// for avoiding unlink/reopens while doing a threaded read
	int32_t m_closeCount1 ;
	int32_t m_closeCount2 ;

	int32_t m_flags;

	// when we are given a NULL buffer to read into we must allocate
	// in Threads.cpp right before the
	// thread is launched. this will stop us from having 19000 unlaunched
	// threads each hogging up 32KB of memory waiting to read tfndb.
	// m_allocBuf points to what we allocated.
	char *m_allocBuf;
	int64_t  m_allocSize;
	// m_allocOff is offset into m_allocBuf where we start reading into 
	// from the file
	int64_t  m_allocOff;
	
	FileState() {
		m_bigfile = NULL;
		m_buf = NULL;
		m_bytesToGo = 0;
		m_offset = 0;
		m_doWrite = false;
		m_bytesDone = 0;
		m_state = NULL;
		m_callback = NULL;
		m_niceness = 0;
		m_filenum1 = 0;
		m_filenum2 = 0;
		m_fd1 = -1;
		m_fd2 = -1;
		memset(m_filename1, 0, sizeof(m_filename1));
		memset(m_filename2, 0, sizeof(m_filename2));
		m_errno = 0;
		m_startTime = 0;
		m_doneTime = 0;
		m_vfd = 0;
		m_closeCount1 = 0;
		m_closeCount2 = 0;
		m_flags = 0;
		m_allocBuf = NULL;
		m_allocSize = 0;
		m_allocOff = 0;
	}
	~FileState() {}
};


class BigFile {

 public:

	~BigFile();
	BigFile();

	// . set a big file's name
	bool set ( const char *dir, const char *baseFilename);

	bool doesExist() const;

	// does file part #n exist?
	bool doesPartExist ( int32_t n ) ;

	// . does not actually open any part file we have
	// . waits for a read/write operation before doing that
	// . if you set maxFileSize to -1 we set it to BigFile::getFileSize()
	// . if you are opening a new file for writing, you need to provide it
	bool open(int flags);

	void logAllData(int32_t log_type);

	void setBlocking();
	void setNonBlocking();

	// . return -2 on error
	// . return -1 if does not exist
	// . otherwise return the big file's complete file size (can b >2gb)
	int64_t getFileSize() const;
	void invalidateFileSize() { m_fileSize = -1; }

	// use the base filename as our filename
	const char *getFilename() const { return m_baseFilename.getBufStart(); }

	char *getDir() { return m_dir.getBufStart(); }

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . otherwise, returns 1 if the read was completed
	// . decides what 2gb part file(s) we should read from
	bool read  ( void       *buf    , 
		     int64_t        size   ,
		     int64_t   offset                         , 
		     FileState  *fs                      = NULL , 
		     void       *state                   = NULL , 
		     void      (* callback)(void *state) = NULL ,
		     int32_t        niceness                = 1    ,
		     int32_t        allocOff                = 0    );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . IMPORTANT: if returns -1 it MAY have written some bytes 
	//   successfully to OTHER parts that's why caller should be 
	//   responsible for maintaining current write offset
	bool  write ( const void    *buf,
	              int64_t        size   ,
		      int64_t   offset                         , 
		      FileState  *fs                      = NULL , 
		      void       *state                   = NULL , 
		      void      (* callback)(void *state) = NULL ,
		      int32_t       niceness                 = 1);

	// unlinks all part files
	bool unlink ( );

	// . renames all parts
	// . uses m_dir if newBaseFilenameDir is NULL
	bool rename(const char *newBaseFilename, const char *newBaseFilenameDir);

	bool move ( const char *newDir );

	// . these here all use threads and call your callback when done
	// . they return false if blocked, true otherwise
	// . they set g_errno on error
	bool unlink   ( void (* callback) ( void *state ) , 
		        void *state ) ;
	bool unlinkPart ( int32_t part , void (* callback) ( void *state ) , void *state ) ;
	bool rename(const char *newBaseFilename, void (* callback)(void *state), void *state);
	bool rename(const char *newBaseFilename, const char *newBaseFilenameDir, void (* callback)(void *state), void *state);

	// closes all part files
	bool close ();

	// just close all the fds of the part files, used by RdbMap.cpp.
	bool closeFds ( ) ;

	// which part (little File) of this BigFile has offset "offset"?
	int getPartNum(int64_t offset) const { return offset / MAX_PART_SIZE; }

	// . opens the nth file if necessary to get its fd
	// . returns -1 if none, >=0 on success
	int getfd ( int32_t n , bool forReading );//, int32_t *vfd = NULL );

	int32_t       getVfd       ( ) { return m_vfd; }

	// WARNING: some may have been unlinked from call to unlinkPart()
	int32_t getNumParts() const { return m_numParts; }

private:
	// makes the filename of part file #n
	void makeFilename_r(const char *baseFilename, const char *baseFilenameDir,
			    int32_t partNum,
			    char *buf, int32_t maxBufSize) const;

	void removePart ( int32_t i ) ;

	void (*m_callback)(void *state);
	void  *m_state;

	//counters for keeping track of unlinking
	bool m_unlinkJobsBeingSubmitted;
	int m_outstandingUnlinkJobCount;
	//counters for keeping track of renaming
	bool m_renameP1JobsBeingSubmitted;
	int m_outstandingRenameP1JobCount;
	bool m_renameP2JobsBeingSubmitted;
	int m_outstandingRenameP2JobCount;
	int m_latestsRenameP1Errno;
	GbMutex m_mtxMetaJobs; //protects above counters

	void incrementUnlinkJobsSubmitted();
	bool incrementUnlinkJobsFinished();
	void incrementRenameP1JobsSubmitted();
	bool incrementRenameP1JobsFinished();
	void incrementRenameP2JobsSubmitted();
	bool incrementRenameP2JobsFinished();

	// to hold the array of Files
	SafeBuf m_filePtrsBuf;

	// . wrapper for all reads and writes
	// . if doWrite is true then we'll do a write, otherwise we do a read
	// . returns false and sets errno on error, true on success
	bool readwrite ( void       *buf,
	                 int64_t        size,
			 int64_t   offset, 
			 bool        doWrite,
			 FileState  *fstate   ,
			 void       *state    ,
			 void      (* callback) ( void *state ) ,
			 int32_t        niceness ,
			 int32_t        allocOff       );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool rename(const char *newBaseFilename,
		    void (*callback)(void *state), void *state,
		    const char *newBaseFilenameDir);
	//job/thread worker functions helping rename()
	static void renameP1Wrapper(void *state);
	void renameP1Wrapper(File *f, int32_t i);
	static void doneP1RenameWrapper(void *state, job_exit_t exit_type);
	void doneP1RenameWrapper(File *f);
	static void renameP2Wrapper(void *state);
	void renameP2Wrapper(File *f, int32_t i);
	static void doneP2RenameWrapper(void *state, job_exit_t exit_type);
	void doneP2RenameWrapper(File *f);

	bool unlink(int32_t  part,
		    void (*callback)(void *state), void *state);
	//job/thread worker functions helping unlink()
	static void unlinkWrapper(void *state);
	void unlinkWrapper(File *f);
	static void doneUnlinkWrapper(void *state, job_exit_t exit_type);
	void doneUnlinkWrapper(File *f, int32_t i);

	// . add all parts from this directory
	// . called by set() above for normal dir
	bool addParts ( const char *dirname ) ;

	bool addPart ( int32_t n ) ;


	int32_t m_flags;

	int32_t             m_vfd;

	// our directory and filename
	SafeBuf m_dir      ;
	SafeBuf m_baseFilename ;

	// rename stores the new name here so we can rename the m_files[i] 
	// after the rename has completed and the rename thread returns
	SafeBuf m_newBaseFilename ;

	// if first char in this dir is 0 then use m_dir
	SafeBuf m_newBaseFilenameDir ;

	// prevent circular calls to BigFile::close() with this
	bool m_isClosing;

	mutable int64_t m_fileSize;

	// oldest of the last modified dates of all the part files
	time_t m_lastModified;

	// number of part files that actually exist
	int       m_numParts;
	// size of File* array (number of pointers in m_filePtrsBuf)
	int32_t      m_maxParts;

public:
	File *getFile2 ( int32_t n ) { 
		if ( n >= m_maxParts ) return NULL;
		File **filePtrs = (File **)m_filePtrsBuf.getBufStart();
		File *f = filePtrs[n];
		//if ( ! f ->calledSet() ) return NULL;
		// this will be NULL if addPart(n) never called
		return f;
	}
	const File *getFile2(int32_t n) const {
		return const_cast<BigFile*>(this)->getFile2(n);
	}

	static bool anyOngoingUnlinksOrRenames();

	bool reset ( );

	int32_t getMaxParts() const { return m_maxParts; }
	
	time_t getLastModifiedTime();
};

#endif // GB_BIGFILE_H
