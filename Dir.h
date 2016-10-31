#ifndef GB_DIR_H
#define GB_DIR_H

#include <dirent.h>
#include <cstddef>

class Dir {
public:
	Dir();
	~Dir();

	void reset();

	bool set(const char *dirName);
	bool set(const char *d1, const char *d2);

	bool open();
	void close();

	const char *getNextFilename(const char *pattern = NULL);

private:
	char *m_dirname;
	DIR *m_dir;
	char m_dentryBuffer[1024];
};


class DirIterator {
public:
	DirIterator(const char *dirName);
	const char *getNextFilename(const char *pattern = NULL);
private:
	Dir m_dir;
	bool opened;
	bool exhausted;
};


#endif // GB_DIR_H
