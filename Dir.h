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

	char *getNextFilename ( char *pattern = NULL );

	// . calls getNextFilename and returns number of files matching the 
	//   pattern
	int getNumFiles             ( char *pattern = NULL );

	char *getDir     ( ) { return m_dirname; }
	char *getDirName ( ) { return m_dirname; }
	char *getDirname ( ) { return m_dirname; }
	char *getFullName ( char *filename ); // prepends path

	 Dir     ( );
	~Dir     ( );

 private:

	char          *m_dirname;
	DIR           *m_dir;
	bool m_needsClose;
};

#endif // GB_DIR_H
