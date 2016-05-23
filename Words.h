// Matt Wells, copyright Jul 2001

// . used to parse XML/HTML (romantic char set) into words
// . TODO: ensure WordType m_types[] array is only 1 byte per entry
// . ??? a word should end at any non-alnum ??? then using phrasing for "tim's"

#ifndef GB_WORDS_H
#define GB_WORDS_H

// now keep this small and malloc if we need more... save some stack
#define MAX_WORDS (1024)

// now Matches.h has 300 Words classes handy... try to do away with this
// make sure it does not slow us down!!
#define WORDS_LOCALBUFSIZE 80

char *getFieldValue ( char *s ,int32_t  slen, char *field , int32_t *valueLen ) ;

unsigned char getCharacterLanguage ( const char *utf8Char ) ;

#include "Xml.h"
#include "SafeBuf.h"
#include "StopWords.h"
#include "fctypes.h"
#include "Titledb.h"

#define NUM_LANGUAGE_SAMPLES 1000

// this bit is set in the tag id to indicate a back tag
#define BACKBIT     ((nodeid_t)0x8000)
#define BACKBITCOMP ((nodeid_t)0x7fff)

class Words {

 public:

	// . set words from a string
	// . s must be NULL terminated
	// . NOTE: we never own the data
	// . there is typically no html in "s"
	// . html tags are NOT parsed out
	bool set( char *s, bool computeIds, int32_t niceness );

	// . similar to above
	// . but we temporarily stick a \0 @ s[slen] for parsing purposes
	bool set( char *s, int32_t slen, bool computeIds, int32_t niceness = 0 );

	// . new function to set directly from an Xml, rather than extracting
	//   text first
	// . use range (node1,node2] and if node2 is -1 that means the last one
	bool set( Xml *xml, bool computeIds, int32_t niceness = 0, int32_t node1 = 0, int32_t node2 = -1 );

	inline bool addWords( char *s, int32_t nodeLen, bool computeIds, int32_t niceness );

	// get the spam modified score of the ith word (baseScore is the 
	// score if the word is not spammed)
	int32_t getNumWords() const {
		return m_numWords;
	}
	int32_t getNumAlnumWords() const {
		return m_numAlnumWords;
	}
	char *getWord( int32_t n ) {
		return m_words[n];
	}
	const char *getWord( int32_t n ) const {
		return m_words[n];
	}
	int32_t getWordLen( int32_t n ) const {
		return m_wordLens[n];
	}
	int32_t getNumTags() const {
		return m_numTags;
	}

	// . size of string from word #a up to and NOT including word #b
	// . "b" can be m_numWords to mean up to the end of the doc
	int32_t getStringSize( int32_t a, int32_t b ) const {
		// do not let it exceed this
		if ( b >= m_numWords ) {
			b = m_numWords;
		}

		// pedal it back. we might equal a then. which is ok, that
		// means to just return the length of word #a then
		b--;

		if ( b < a ) {
			return 0;
		}

		if ( a < 0 ) {
			return 0;
		}

		int32_t size = m_words[b] - m_words[a];

		// add in size of word #b
		size += m_wordLens[b];

		return size;
	}

	// . CAUTION: don't call this for punct "words"... it's bogus for them
	// . this is only for alnum "words"
	int64_t getWordId( int32_t n ) const {
		return m_wordIds [n];
	}

	bool isStopWord ( int32_t n ) const {
		return ::isStopWord( m_words[n], m_wordLens[n], m_wordIds[n] );
	}

	bool isQueryStopWord ( int32_t n , int32_t langId ) const {
		return ::isQueryStopWord( m_words[n], m_wordLens[n], m_wordIds[n], langId );
	}

	// . how many quotes in the nth word?
	// . how many plusses in the nth word?
	// . used exclusively by Query class for parsing query syntax
	int32_t getNumQuotes( int32_t n ) const {
		int32_t count = 0;
		for ( int32_t i = 0; i < m_wordLens[n]; i++ ) {
			if ( m_words[n][i] == '\"' ) {
				count++;
			}
		}

		return count;
	}

	// . do we have a ' ' 't' '\n' or '\r' in this word?
	// . caller should not call this is isPunct(n) is false, pointless.

	bool hasSpace( int32_t n ) const {
		for ( int32_t i = 0; i < m_wordLens[n]; i++ ) {
			if ( is_wspace_utf8( &m_words[n][i] ) ) {
				return true;
			}
		}

		return false;
	}

	bool hasChar( int32_t n, char c ) const {
		for ( int32_t i = 0; i < m_wordLens[n]; i++ ) {
			if ( m_words[n][i] == c ) {
				return true;
			}
		}

		return false;
	}

	bool isSpaces( int32_t n, int32_t starti = 0 ) const {
		for ( int32_t i = starti; i < m_wordLens[n]; i++ ) {
			if ( !is_wspace_utf8( &m_words[n][i] ) ) {
				return false;
			}
		}
		return true;
	}

	// if this is set from xml, every word is either a word or an xml node
	nodeid_t getTagId( int32_t n ) const {
		if ( !m_tagIds ) {
			return 0;
		}

		return ( m_tagIds[n] & BACKBITCOMP );
	}

	bool isBackTag( int32_t n ) const {
		if ( !m_tagIds ) {
			return false;
		}

		if ( m_tagIds[n] & BACKBIT ) {
			return true;
		}

		return false;
	}

	// CAUTION!!!
	//
	// "BACKBIT" is set in the tagid  of m_tagIds[] to indicate the tag is
	// a "back tag" as opposed to a "front tag". i.e. </a> vs. <a>
	// respectively. so mask it out by doing "& BACKBITCOMP" if you just
	// want the pure tagid!!!!
	//
	// CAUTION!!!
	nodeid_t       *getTagIds()       { return m_tagIds; }
	const nodeid_t *getTagIds() const { return m_tagIds; }
	char           *       *getWords()       { return m_words; }
	const char     * const *getWords() const { return (const char*const*)m_words; }
	char           *       *getWordPtrs()       { return m_words; }
	const char     * const *getWordPtrs() const { return (const char*const*)m_words; }
	int32_t        *getWordLens()       { return m_wordLens; }
	const int32_t  *getWordLens() const { return m_wordLens; }
	int64_t        *getWordIds()       { return m_wordIds; }
	const int64_t  *getWordIds() const { return m_wordIds; }
	const int32_t  *getNodes() const { return m_nodes; }
	
	// 2 types of "words": punctuation and alnum
	// isPunct() will return true on tags, too, so they are "punct"
	bool      isPunct  ( int32_t n ) const { return m_wordIds[n] == 0;}
	bool      isAlnum  ( int32_t n ) const { return m_wordIds[n] != 0;}
	bool      isAlpha  ( int32_t n ) const { 
		if ( m_wordIds[n] == 0LL ) return false;
		if ( isNum ( n )         ) return false;
		return true;
	}

	int32_t getAsLong ( int32_t n ) const {
		// skip if no digit
		if ( ! is_digit ( m_words[n][0] ) ) return -1;
		return atol2(m_words[n],m_wordLens[n]); 
	}

	bool      isNum    ( int32_t n ) const { 
		if ( ! is_digit(m_words[n][0]) ) return false;
		char *p    = m_words[n];
		char *pend = p + m_wordLens[n];
		for (  ; p < pend ; p++ )
			if ( ! is_digit(*p) ) return false;
		return true;
	}

	// . are all alpha char capitalized?
	bool      isUpper  ( int32_t n ) const {
		// skip if not alnum...
		if ( m_wordIds[n] == 0LL ) {
			return false;
		}

		char *p = m_words[n];
		char *pend = p + m_wordLens[n];
		char cs;
		for ( ; p < pend; p += cs ) {
			cs = getUtf8CharSize( p );
			if ( is_digit( *p ) ) {
				continue;
			}

			if ( is_lower_utf8( p ) ) {
				return false;
			}
		}

		return true;
	}

	bool isCapitalized( int32_t n ) const {
		if ( !is_alpha_utf8( m_words[n] ) ) {
			return false;
		}

		return is_upper_utf8( m_words[n] );
	}

	unsigned char isBounded(int wordi);
	 Words     ( );
	~Words     ( );
	void reset ( ); 

	// returns -1 and sets g_errno on error
	int32_t getLanguage ( class Sections *sections = NULL ,
			   int32_t maxSamples = NUM_LANGUAGE_SAMPLES,
			   int32_t niceness = 0,
			   int32_t *langScore = NULL);

	char *getContent() { 
		if ( m_numWords == 0 ) return NULL;
		return m_words[0]; 
	}

	int32_t getPreCount() const { return m_preCount; }

private:

	bool allocateWordBuffers(int32_t count, bool tagIds = false);
	
	char  m_localBuf [ WORDS_LOCALBUFSIZE ];

	char *m_localBuf2;
	int32_t  m_localBufSize2;

	char *m_buf;
	int32_t  m_bufSize;
	Xml  *m_xml ;  // if the class is set from xml, rather than a string

	int32_t           m_preCount  ; // estimate of number of words in the doc
	char          **m_words    ;  // pointers to the word
	int32_t           *m_wordLens ;  // length of each word
	int64_t      *m_wordIds  ;  // lower ascii hash of word
	int32_t           *m_nodes    ;  // Xml.cpp node # (for tags only)
	nodeid_t       *m_tagIds   ;  // tag for xml "words"

 	int32_t           m_numWords;      // # of words we have
	int32_t           m_numAlnumWords;

	bool           m_hasTags;

	int32_t m_numTags;
};

#endif // GB_WORDS_H
