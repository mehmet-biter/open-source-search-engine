// Matt Wells, copyright Sep 2003

// Speller is a class for doing spell checking on user queries.

// . TODO: we might be able to use this as a related searches feature too, but
//         we might have to use a different distance metric (getSimilarity()) 
//         that is more word based and less letter based.

#ifndef GB_SPELLER_H
#define GB_SPELLER_H

#define MAX_FRAG_SIZE 1024

// max int32_t returned by getPhrasePopularity() function
#define MAX_PHRASE_POP 16800

#include "StopWords.h"
#include "Query.h"
#include "Multicast.h"
#include "HashTableX.h"

class Speller {

 public:

	Speller();
	~Speller();


	void reset();

	bool init();

	void test (char *ff);

	int64_t getLangBits64 ( int64_t wid ) ;

	int32_t getPhrasePopularity( char *s, uint64_t h, unsigned char langId );

	bool canSplitWords( char *s, int32_t slen, bool *isPorn, char *splitWords, unsigned char langId );
	
	bool findNext( char *s, char *send, char **nextWord, bool *isPorn, unsigned char langId );

	// . dump out the first "numWordsToDump" words and phrases
	//   encountered will scanning the records in Titledb
	// . use these words/phrases to make the dictionaries
	bool generateDicts ( int32_t numWordsToDump , char *coll );

	bool loadUnifiedDict();

	void dictLookupTest ( char *ff );

	char *getPhraseRecord(char *phrase, int len);

	bool getPhraseLanguages(char *phrase, int len, int64_t *array);
	bool getPhraseLanguages2 (char *phraseRec , int64_t *array) ;

	HashTableX m_unifiedDict;

	SafeBuf m_unifiedBuf;
};

extern class Speller g_speller;

#endif // GB_SPELLER_H
