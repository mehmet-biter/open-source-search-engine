// Matt Wells, Copyright May 2001

// . TODO: don't closes block us? if we have many fd's our closes might block!!
// . TODO: must we create a separate fd for each non-blocking read even if
//         on the same file?????? that would save us...

// . this class simulates having 1K file descriptors.
// . by using it's open/write/read/close it will make it seem like you have 5K file descriptors
// . minimizes the # of open/closes it does.

// On my solaris ultra 1 i could do 28,000 open/close pairs per second.
// my 400mhz pentium linux box was 2.5 times faster! it only had 256 file
// descriptors to work with, while the sun box had 1024.

// the sockets must share with these so we'd like to set a maximum for each.

#ifndef GB_FILE_H
#define GB_FILE_H

#define MAX_FILENAME_LEN 128

#include <fcntl.h>           // for open
#include <pthread.h>


bool doesFileExist ( const char *filename ) ;

int64_t getFileSize ( const char *filename ) ;

// for avoiding unlink/opens that mess up our threaded read
int32_t getCloseCount_r ( int fd );

// prevent fd from being closed on us when we are writing
void enterWriteMode ( int fd ) ;
void exitWriteMode  ( int fd ) ;

class File {
public:

	 File ( );
	~File ( );

	// . if you don't need to do a full open then just set the filename
	// . useful for unlink/rename/reserve/...
	void set ( const char *dir , const char *filename );
	void set ( const char *filename );

	// returns false and sets errno on error, returns true on success
	bool rename ( const char *newFilename );

	void setForceRename( bool forceRename ) {
		m_forceRename = forceRename;
	}

	bool calledOpen () { return m_calledOpen; }
	bool calledSet  () { return m_calledSet; }

	// . get the file extension of this file
	// . return NULL if none
	const char *getExtension() const;
	
	// uses lseek to get file's current position
	int32_t getCurrentPos() const;

	// . open() returns true on success, false on failure, errno is set.
	// . opens for reading/writing only
	// . returns false if does not exist
	bool open  ( int flags , int permissions = 
		     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );

	// . use an offset of -1 to use current file seek position
	// . returns what ::read returns
	// . returns -1 on lseek failure (if offset is valid)
	// . returns 0 on EOF
	// . returns numBytesRead if not error
	// . a negative offset means current read offset
	int  read    ( void *buf , int32_t size , int32_t offset );

	// . use an offset of -1 to use current file seek position
	// . returns what ::write returns
	// . returns -1 on lseek failure (if offset is valid)
	// . returns numBytesWritten if not error
	// . this is non-blocking so may return < "numBytesToWrite"
	// . a negative offset means current write offset
	int  write(const void *buf, int32_t size, int32_t offset);

	// . this will really close this file
	bool close   ( );  

	// used by threaded unlinks and renames by BigFile.cpp
	void close1_r ();
	void close2   ();

	// . returns -1 on error
	// . otherwise returns file size in bytes
	// . returns 0 if does not exist
	int64_t getFileSize() const;

	// . when was it last touched?
	time_t getLastModifiedTime() const;

	// . returns -1 on error and sets errno
	// . returns  0 if does not exist
	// . returns  1 if it exists
	// . a simple stat check
	int32_t doesExist() const;

	// . static so you don't need an instant of this class to call it
	// . returns false and sets errno on error
	bool unlink ( );

	// . will try to REopen the file to get the fd if necessary
	// . used by BigFile
	// . returns -2 if we've never been officially opened
	// . returns -1 on error getting the fd or opening this file
	// . must call open() before calling this
	int   getfd          ( ) ;

	const char *getFilename() const { return m_filename; }

private:
	char m_filename [ MAX_FILENAME_LEN ];

	bool m_closedIt;
	
	// initializes the fd pool
	static bool initialize ();

	// free the least-used file.
	bool closeLeastUsed ( );

	void close1_r_unlocked();
	void close2_unlocked();

	int32_t m_closeCount;

	// now just the real fd. is -1 if not opened
	int m_fd;

	// save the permission and flag sets in case of re-opening
	int m_flags;
	//int m_permissions;
	
	bool m_calledOpen;
	bool m_calledSet;

	bool m_forceRename;
	
	pthread_mutex_t m_mtxFdManipulation;
	bool open_unlocked(int flags, int permissions);
	bool close_unlocked();
};

#endif // GB_FILE_H
