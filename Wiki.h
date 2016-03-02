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

	// . load from disk
	// . wikititles.txt (loads wikititles.dat if and date is newer)
	bool load();
	
private:
	bool loadText ( int32_t size );

	HashTableX m_ht;
	
	BigFile m_f;

	void *m_state;
	void (* m_callback)(void *);

	char m_opened;
};

extern class Wiki g_wiki;

#endif
