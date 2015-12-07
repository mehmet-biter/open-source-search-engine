// Matt Wells, copyright Dec 2008

#ifndef _WIKI_H_
#define _WIKI_H_

#include "BigFile.h"
#include "HashTableX.h"

class Wiki {
public:
	Wiki();
	~Wiki();

	void reset();

	int32_t getNumWordsInWikiPhrase ( int32_t i , class Words *words );

	bool isInWiki ( uint32_t h ) { return ( m_ht.getSlot ( &h ) >= 0 ); }

	// . load from disk
	// . wikititles.txt (loads wikititles.dat if and date is newer)
	bool load();
	
private:
	bool loadText ( int32_t size );

	HashTableX m_ht;
	
	char m_buf[5000];

	char m_randPhrase[512];

	BigFile m_f;

	void *m_state;
	void (* m_callback)(void *);

	int32_t m_errno;

	char m_opened;
	FileState m_fs;
};

extern class Wiki g_wiki;

#endif
