// Matt Wells, copyright Jul 2001

#ifndef GB_MATCHES_H
#define GB_MATCHES_H

#include "Words.h"
#include "Pos.h"
#include "Bits.h"

// do not hash more than this many query words into the hash table
#define MAX_QUERY_WORDS_TO_MATCH 1000

// . i upped this from 500 to 3000 to better support the BIG HACK
//   getting 3000 matches slows down the summary generator a lot.
// . i raised MAX_MATCHES to 3000 for huge UOR queries made in SearchInput.cpp
//   from facebook interests
#define MAX_MATCHES              3000

#define MAX_MATCHGROUPS 300

typedef int32_t mf_t;

// . values for Match::m_flags
// . dictates the "match group" that the match belongs to
#define MF_TITLEGEN                   0x0001 // in generated title?
#define MF_TITLETAG                   0x0002
#define MF_LINK                       0x0004 // in non-anomalous link text
#define MF_HOOD                       0x0010 // in non-anomalous neighborhood
#define MF_BODY                       0x0040 // in body
#define MF_METASUMM                   0x0080 // in meta summary
#define MF_METADESC                   0x0100 // in meta description
#define MF_METAKEYW                   0x0200 // in meta keywords
#define MF_RSSTITLE                   0x1000 
#define MF_RSSDESC                    0x2000 
#define MF_URL                        0x4000  // in url

class Xml;
class Sections;
class Url;
class LinkInfo;
class Title;
class Phrases;
class QueryTerm;
class Query;

class Match {
 public:
	// word # we match in the document using "m_words" below
	int32_t m_wordNum;

	// # of words in this match, like if we match a phrase
	// we have > 1 words in the match
	int32_t m_numWords;

	// word # we match in the query
	int32_t m_qwordNum;

	// # of query words we match if we are a phrase, otherwise
	// this is 1
	int32_t m_numQWords;

	// "match group" or type of match. i.e. MF_TITLETAG, MF_METASUMM, ...
	mf_t m_flags;

	// . for convenience, these four class ptrs are used by Summary.cpp
	// . m_wordNum is relative to this "words" class (and scores,bits,pos)
	Words    *m_words;
	Sections *m_sections;
	Bits     *m_bits;
	Pos      *m_pos;
};

class Matches {

 public:

	void setQuery ( Query *q );

	bool set( Words *bodyWords, Phrases *bodyPhrases,
			  Sections *bodySections, Bits *bodyBits, Pos *bodyPos, Xml *xml,
			  Title *tt, Url *firstUrl, LinkInfo *linkInfo, int32_t niceness );

	bool addMatches(char *s, int32_t slen, mf_t flags, int32_t niceness );

	// . this sets the m_matches[] array
	// . m_matches[i] is -1 if it matches no term in the query
	// . m_matches[i] is X if it matches term #X in the query
	// . returns false and sets errno on error
	bool addMatches( Words *words, Phrases *phrases = NULL, Sections *sections = NULL,
					 Bits *bits = NULL, Pos *pos = NULL, mf_t flags = 0 );

	// how many words matched a rawTermId?
	int32_t getNumMatches() {
		return m_numMatches;
	}

	// janitorial stuff
	Matches ( ) ;
	~Matches( ) ;
	void reset ( ) ;
	void reset2 ( ) ;

	// used internally and by PageGet.cpp
	bool isMatchableTerm ( class QueryTerm *qt );

	// used internally
	int32_t getNumWordsInMatch( Words *words, int32_t wn, int32_t n, int32_t *numQWords, int32_t *qwn,
								bool allowPunctInPhrase = true );

	// how many words matched a rawTermId?
	Match  m_matches[MAX_MATCHES];
	int32_t   m_numMatches;

	// . hash query word ids into a small hash table
	// . we use this to see what words in the document are query terms
	int64_t m_qtableIds      [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	int32_t      m_qtableWordNums [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	char      m_qtableFlags    [ MAX_QUERY_WORDS_TO_MATCH * 3 ];
	int32_t      m_numSlots;
	Query    *m_q;
	int32_t      m_numAlnums;

	// . 1-1 with Query::m_qwords[] array of QWords
	// . shows the match flags for that query word
	mf_t     *m_qwordFlags;
	int32_t m_qwordAllocSize;
	char m_tmpBuf[128];

	// . one words/scores/bits/pos/flags class per "match group"
	// . match groups examples = body, a single link text, a meta tag, etc.
	// . match groups are basically disjoint chunks of text information
	// . the document body (web page) is considered a single match group
	// . a single link text is considered a match group
	// . a single meta summary tag is a match group, ...
	Words    *m_wordsPtr    [MAX_MATCHGROUPS];
	Sections *m_sectionsPtr [MAX_MATCHGROUPS];
	Bits     *m_bitsPtr     [MAX_MATCHGROUPS];
	Pos      *m_posPtr      [MAX_MATCHGROUPS];
	mf_t      m_flags       [MAX_MATCHGROUPS];
	int32_t      m_numMatchGroups;

	Words    m_wordsArray    [MAX_MATCHGROUPS];
	Bits     m_bitsArray     [MAX_MATCHGROUPS];
	Pos      m_posArray      [MAX_MATCHGROUPS];
};

#endif // GB_MATCHES_H
