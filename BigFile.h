// Copyright Sep 2000 Matt Wells

// . this class makes the 2gb file limit transparent
// . you can actually set you own file size limit to whatever you want as 
//   int32_t as it's greater than zero
// . this is usually 2gb
// . TODO: fix the O_SYNC option
// . TODO: provide ability to pass flags

#ifndef GB_BIGFILE_H
#define GB_BIGFILE_H

#include "File.h"
#include "JobScheduler.h" //for job_exit_t
#include "SafeBuf.h"


#ifndef PRIVACORE_TEST_VERSION
// . use 512 disk megs per part file
// . at a GB_PAGE_SIZE of 16k this is 32k RdbMap pages (512k) per part file
// . let's use 2GB now to conserve file descriptors
#define MAX_PART_SIZE  (1920LL*1024LL*1024LL)
#else
#define MAX_PART_SIZE  (20LL*1024LL*1024LL)
#endif

#define LITTLEBUFSIZE sizeof(File)


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
	// was it found in the disk page cache?
	char m_inPageCache;

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
};


class BigFile {

 public:

	~BigFile();
	BigFile();

	// . set a big file's name
	// . we split little files that make up this BigFile between
	//   "dir" and "stripeDir"
	bool set ( const char *dir, const char *baseFilename, const char *stripeDir = NULL );

	// self explanatory
	bool doesExist ( ) ;

	// does file part #n exist?
	bool doesPartExist ( int32_t n ) ;

	// . does not actually open any part file we have
	// . waits for a read/write operation before doing that
	// . if you set maxFileSize to -1 we set it to BigFile::getFileSize()
	// . if you are opening a new file for writing, you need to provide it
	bool open(int flags);

	void logAllData(int32_t log_type);

	int getFlags() { return m_flags; }

	void setBlocking    ( ) { m_flags &= ~((int32_t)O_NONBLOCK); }
	void setNonBlocking ( ) { m_flags |=         O_NONBLOCK ; }

	// . return -2 on error
	// . return -1 if does not exist
	// . otherwise return the big file's complete file size (can b >2gb)
	int64_t getFileSize ( );
	int64_t getSize     ( ) { return getFileSize(); }

	// use the base filename as our filename
	char       *getFilename()       { return m_baseFilename.getBufStart(); }
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
		     bool        allowPageCache          = true ,
		     bool        hitDisk                 = true ,
		     int32_t        allocOff                = 0    );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . IMPORTANT: if returns -1 it MAY have written some bytes 
	//   successfully to OTHER parts that's why caller should be 
	//   responsible for maintaining current write offset
	bool  write ( void       *buf    ,
	              int64_t        size   ,
		      int64_t   offset                         , 
		      FileState  *fs                      = NULL , 
		      void       *state                   = NULL , 
		      void      (* callback)(void *state) = NULL ,
		      int32_t       niceness                 = 1    ,
		      bool       allowPageCache           = true );

	// unlinks all part files
	bool unlink ( );

	// . renames ALL parts too
	// . doesn't change directory, just the base filename
	// . use m_dir if newBaseFilenameDir is NULL
	// . force = rename even if newFile exist
	bool rename ( const char *newBaseFilename, const char *newBaseFilenameDir=NULL, bool force=false ) ;

	bool move ( const char *newDir );

	// . returns false and sets g_errno on failure
	// . chop only parts LESS THAN "part"
	bool unlinkPart ( int32_t part );

	// . these here all use threads and call your callback when done
	// . they return false if blocked, true otherwise
	// . they set g_errno on error
	bool unlink   ( void (* callback) ( void *state ) , 
		        void *state ) ;
	bool rename   ( const char *newBaseFilename, void (* callback) ( void *state ) , void *state, bool force=false ) ;
	bool unlinkPart ( int32_t part , void (* callback) ( void *state ) , void *state ) ;

	// closes all part files
	bool close ();

	// just close all the fds of the part files, used by RdbMap.cpp.
	bool closeFds ( ) ;

	// what part (little File) of this BigFile has offset "offset"?
	int getPartNum ( int64_t offset ) { return offset / MAX_PART_SIZE; }

	// . opens the nth file if necessary to get it's fd
	// . returns -1 if none, >=0 on success
	int getfd ( int32_t n , bool forReading );//, int32_t *vfd = NULL );

	int32_t       getVfd       ( ) { return m_vfd; }

	// WARNING: some may have been unlinked from call to unlinkPart()
	int32_t getNumParts ( ) { return m_numParts; }

private:
	// makes the filename of part file #n
	void makeFilename_r ( char *baseFilename    , 
			      char *baseFilenameDir ,
			      int32_t  n               , 
			      char *buf             ,
			      int32_t maxBufSize );

	void removePart ( int32_t i ) ;

	// don't launch a threaded rename/unlink if one already in progress
	// since we only have one callback, m_callback
	int32_t m_numThreads;

	void (*m_callback)(void *state);
	void  *m_state;
	// is the threaded op an unlink? (or rename?)
	bool   m_isUnlink;
	int32_t   m_part; // part # to unlink (-1 for all)

	// number of parts remaining to be unlinked/renamed
	int32_t   m_partsRemaining;

	char m_tinyBuf[8];

	// to hold the array of Files
	SafeBuf m_filePtrsBuf;

	// enough mem for our first File so we can avoid a malloc
	char m_littleBuf[LITTLEBUFSIZE];

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
			 bool        allowPageCache ,
			 bool        hitDisk        ,
			 int32_t        allocOff       );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool unlinkRename ( const char *newBaseFilename,
			    int32_t  part                        ,
			    bool  useThread                   ,
			    void (* callback) ( void *state ) ,
			    void *state                       ,
			    const char *newBaseFilenameDir = NULL,
				bool force = false );
	//job/thread worker functions helping unlinkrename()
	static void renameWrapper(void *state);
	static void doneRenameWrapper(void *state, job_exit_t exit_type);
	void doneRenameWrapper(File *f);
	void renameWrapper(File *f, int32_t i);
	static void unlinkWrapper(void *state);
	void unlinkWrapper(File *f);
	static void doneUnlinkWrapper(void *state, job_exit_t exit_type);
	void doneUnlinkWrapper(File *f, int32_t i);

	// . add all parts from this directory
	// . called by set() above for normal dir as well as stripe dir
	bool addParts ( const char *dirname ) ;

	bool addPart ( int32_t n ) ;


	// for basefilename to avoid an alloc
	char m_tmpBaseBuf[32];


	//int32_t m_permissions;
	int32_t m_flags;

	int32_t             m_vfd;

	// our most important the directory and filename
	SafeBuf m_dir      ;
	SafeBuf m_baseFilename ;

	// rename stores the new name here so we can rename the m_files[i] 
	// after the rename has completed and the rename thread returns
	SafeBuf m_newBaseFilename ;

	// if first char in this dir is 0 then use m_dir
	SafeBuf m_newBaseFilenameDir ;

public:
	File *getFile2 ( int32_t n ) { 
		if ( n >= m_maxParts ) return NULL;
		File **filePtrs = (File **)m_filePtrsBuf.getBufStart();
		File *f = filePtrs[n];
		//if ( ! f ->calledSet() ) return NULL;
		// this will be NULL if addPart(n) never called
		return f;
	}

	bool reset ( );

	// determined in open() override
	int       m_numParts;
	// maximum part #
	int32_t      m_maxParts;

	// prevent circular calls to BigFile::close() with this
	bool m_isClosing;

	int64_t m_fileSize;

	// oldest of the last modified dates of all the part files
	time_t m_lastModified;
	time_t getLastModifiedTime();
};

extern int32_t g_unlinkRenameThreads;

#endif // GB_BIGFILE_H
