#ifndef DOCIDDELETE_H
#define DOCIDDELETE_H

#include <time.h>
#include <cstdint>

class DocDelete {
public:
	DocDelete();

	bool init();

	static void reload(int /*fd*/, void *state);

	static void processFile(void *item);
	static void processDoc(void *item);

	static void indexedDoc(void *state);

protected:
	bool load();

	const char *m_filename;
	const char *m_tmp_filename;
	const char *m_currentdocid_filename;

private:
	void reload();

	time_t m_lastModifiedTime;
};

extern DocDelete g_docDelete;

#endif //DOCIDDELETE_H
