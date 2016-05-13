// Matt Wells, copyright Jul 2001

#ifndef GB_STOPWORDS_H
#define GB_STOPWORDS_H

#include "Unicode.h"

// . this returns true if h is the hash of an ENGLISH stop word
// . list taken from www.superjournal.ac.uk/sj/application/demo/stopword.htm 
// . stop words with "mdw" next to them are ones I added
bool isStopWord ( const char *s , int32_t len , int64_t h ) ;

// . damn i forgot to include these above
// . i need these so m_bitScores in IndexTable.cpp doesn't have to require
//   them! Otherwise, it's like all queries have quotes around them again...
bool isQueryStopWord ( const char *s , int32_t len , int64_t h , int32_t langId ) ;

// is it a COMMON word?
int32_t isCommonWord ( int64_t h ) ;

bool initWordTable(class HashTableX *table, char* words[], 
		   //int32_t size ,
		   char *label);

// for Process.cpp::resetAll() to call when exiting to free all mem
void resetStopWordTables();


#endif // GB_STOPWORDS_H
