#ifndef GB_DIR_H
#define GB_DIR_H

#include <sys/types.h>      // for opendir
#include <dirent.h>         // for opendir
#include "File.h" // for File::getFileSize()

class Dir {

 public:

	bool set      ( const char *dirName );
	bool set      ( const char *d1, const char *d2 );

	void reset    ( );

	bool open     ( );

	bool close    ( );

	const char *getNextFilename ( const char *pattern = NULL );

	const char *getDir     ( ) { return m_dirname; }
	const char *getDirName ( ) { return m_dirname; }
	const char *getDirname ( ) { return m_dirname; }
	const char *getFullName ( const char *filename ); // prepends path

	 Dir     ( );
	~Dir     ( );

 private:

	char          *m_dirname;
	DIR           *m_dir;
	char          m_dentryBuffer[1024];
};

#endif // GB_DIR_H
