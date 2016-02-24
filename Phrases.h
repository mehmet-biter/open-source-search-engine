// Matt Wells, copyright Jul 2001

// . generate phrases and store their hashes into m_phraseIds[] array
// . hash() will then hash the phraseIds into the TermTable (hashtable)
// . will it hash a word as a phrase if it's the only word? No, it will not.
//   it only hashes 2+ word phrases

#ifndef _PHRASES_H_
#define _PHRASES_H_

#include "Bits.h"
#include "Words.h"

#define PHRASE_BUF_SIZE (MAX_WORDS * 14)

#define PSKIP 201

class Phrases {

 public:

	Phrases();
	~Phrases();
	void reset() ;

	bool set2 ( Words *words, Bits *bits , int32_t niceness ) {
		return set ( words,bits,true,false,TITLEREC_CURRENT_VERSION,
			     niceness); };

	// . set the hashes (m_phraseIds) of the phrases for these words
	// . a phraseSpam of PSKIP means word is not in a phrase
	// . "bits" describes the words in a phrasing context
	// . "spam" is % spam of each word (spam may be NULL)
	bool set ( Words    *words, 
		   Bits     *bits ,
		   bool      useStopWords ,
		   bool      useStems     ,
		   int32_t      titleRecVersion,
		   int32_t      niceness);

	int64_t getPhraseId2  ( int32_t n ) { return m_phraseIds2[n]; };
	int64_t *getPhraseIds2(        ) { return m_phraseIds2; };
	int64_t *getPhraseIds3(        ) { return m_phraseIds3; };
	int32_t      getPhraseSpam ( int32_t n ) { return m_phraseSpam[n]; };
	bool      hasPhraseId   ( int32_t n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      startsAPhrase ( int32_t n ) { return (m_phraseSpam[n]!=PSKIP);};
	bool      isInPhrase    ( int32_t n ) ;

	// . store phrase that starts with word #i into "dest"
	// . we also NULL terminated it in "dest"
	// . return length
	char *getPhrase ( int32_t i , int32_t *phrLen , int32_t npw );

	int32_t  getNumWordsInPhrase2( int32_t i ) { return m_numWordsTotal2[i]; };

	int32_t  getMaxWordsInPhrase( int32_t i , int64_t *pid ) ;
	int32_t  getMinWordsInPhrase( int32_t i , int64_t *pid ) ;

	// . leave this public so SimpleQuery.cpp can mess with it
	// . called by Phrases::set() above for each i
	// . we set phraseSpam to 0 to 100% typically
	// . we set phraseSpam to PSKIP if word #i cannot start a phrase
	void setPhrase ( int32_t i ,
			 int32_t niceness);

	// private:

	char  m_localBuf [ PHRASE_BUF_SIZE ];

	char *m_buf;
	int32_t  m_bufSize;

	// the two word hash
	int64_t     *m_phraseIds2  ;
	int64_t     *m_phraseIds3  ;
	unsigned char *m_phraseSpam ;
	// for the two word phrases:
	unsigned char *m_numWordsTotal2 ;
	unsigned char *m_numWordsTotal3 ;
	int32_t           m_numPhrases; // should equal the # of words

	// placeholders to avoid passing to subroutine
	Words      *m_words;
	int64_t  *m_wids;
	char      **m_wptrs;
	int32_t       *m_wlens;

	Bits    *m_bits;
	bool     m_useStems;
	bool     m_useStopWords;
	int32_t     m_titleRecVersion;
};

#endif
