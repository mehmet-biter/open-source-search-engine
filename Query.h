// Matt Wells, copyright Aug 2003

// Query is a class for parsing queries

#ifndef GB_QUERY_H
#define GB_QUERY_H

#include "SafeBuf.h"

// support big OR queries for image shingles
#define ABS_MAX_QUERY_LEN 62000

// raise for crazy bool query on diffbot
// seems like we alloc just enough to hold our words now so that this
// is really a performance capper but it is used in Summary.cpp
// and Matches.h so don't go too big just yet
#define ABS_MAX_QUERY_WORDS 99000

// . how many IndexLists might we get/intersect
// . we now use a int64_t to hold the query term bits for non-boolean queries
#define ABS_MAX_QUERY_TERMS 9000

#define GBUF_SIZE (16*1024)

// let's support up to 64 query terms for now
typedef uint64_t qvec_t;

#define MAX_EXPLICIT_BITS (sizeof(qvec_t)*8)

#define MAX_OVEC_SIZE 256

// field codes
#define FIELD_URL      1
#define FIELD_LINK     2
#define FIELD_SITE     3
#define FIELD_IP       4
#define FIELD_SUBURL   5
#define FIELD_TITLE    6
#define FIELD_TYPE     7
#define FIELD_EXT      21
//#define FIELD_COLL     22
#define FIELD_ILINK    23
#define FIELD_LINKS    24
#define FIELD_SITELINK 25
// non-standard field codes
#define FIELD_ZIP      8
//#define FIELD_UNUSED     9
//#define FIELD_UNUSED   10
//#define FIELD_UNUSED   11
//#define FIELD_UNUSED     12
//#define FIELD_UNUSED    13
//#define FIELD_UNUSED  14
//#define FIELD_UNUSED      15
//#define FIELD_UNUSED    16
//#define FIELD_UNUSED     17
#define FIELD_GENERIC  18
#define FIELD_ISCLEAN  19  // we hash field="isclean:" val="1" if doc clean
//#define FIELD_UNUSED 20
#define FIELD_CHARSET  30
#define FIELD_GBRSS    31
//#define FIELD_UNUSED       32
//#define FIELD_UNUSED      33
//#define FIELD_UNUSED      34
//#define FIELD_UNUSED     35
#define FIELD_GBLANG        36
//#define FIELD_UNUSED     37
//#define FIELD_UNUSED    38
//#define FIELD_UNUSED   39
//#define FIELD_UNUSED       40
#define FIELD_QUOTA            41
//#define FIELD_UNUSED      42
//#define FIELD_UNUSED         43
//#define FIELD_UNUSED   44
//#define FIELD_UNUSED          45
#define FIELD_GBCOUNTRY        46
//#define FIELD_UNUSED             47
//#define FIELD_UNUSED      48
#define FIELD_GBPERMALINK      49
//#define FIELD_UNUSED         50
#define FIELD_GBTERMID         50
#define FIELD_GBSECTIONHASH    51
#define FIELD_GBDOCID          52
#define FIELD_GBCONTENTHASH    53 // for deduping at spider time
#define FIELD_GBSORTBYFLOAT    54 // i.e. sortby:price -> numeric termlist
#define FIELD_GBREVSORTBYFLOAT 55 // i.e. sortby:price -> low to high
#define FIELD_GBNUMBERMIN      56
#define FIELD_GBNUMBERMAX      57
#define FIELD_GBPARENTURL      58
#define FIELD_GBSORTBYINT      59
#define FIELD_GBREVSORTBYINT   60
#define FIELD_GBNUMBERMININT   61
#define FIELD_GBNUMBERMAXINT   62
//#define FIELD_UNUSED         63
//#define FIELD_UNUSED         64
//#define FIELD_UNUSED         65
#define FIELD_GBNUMBEREQUALINT 66
#define FIELD_GBNUMBEREQUALFLOAT 67
#define FIELD_SUBURL2            68
#define FIELD_GBFIELDMATCH       69

// returns a FIELD_* code above, or FIELD_GENERIC if not in the list
char getFieldCode  ( const char *s , int32_t len , bool *hasColon = NULL ) ;

int32_t getNumFieldCodes ( );

class Query;

// . values for QueryField::m_flag
// . QTF_DUP means it is just for the help page in PageRoot.cpp to 
//   illustrate a second or third example
#define QTF_DUP  0x01
#define QTF_HIDE 0x02
#define QTF_BEGINNEWTABLE 0x04

struct QueryField {
	const char *text;
	char field;
	bool hasColon;
	const char *example;
	const char *desc;
	const char *m_title;
	char  m_flag;
};

extern struct QueryField g_fields[];
	
// reasons why we ignore a particular QueryWord's word or phrase
#define IGNORE_DEFAULT   1 // punct
#define IGNORE_CONNECTED 2 // connected sequence (cd-rom)
#define IGNORE_QSTOP     3 // query stop word (come 'to' me)
#define IGNORE_REPEAT    4 // repeated term (time after time)
#define IGNORE_FIELDNAME 5 // word is a field name, like title:
#define IGNORE_BREECH    6 // query exceeded MAX_QUERY_TERMS so we ignored part
#define IGNORE_BOOLOP    7 // boolean operator (OR,AND,NOT)
#define IGNORE_QUOTED    8 // word in quotes is ignored. "the day"

// boolean query operators (m_opcode field in QueryWord)
#define OP_OR         1
#define OP_AND        2
#define OP_NOT        3
#define OP_LEFTPAREN  4
#define OP_RIGHTPAREN 5
#define OP_UOR        6
#define OP_PIPE       7

// . these first two classes are functionless
// . QueryWord, like the Phrases class, is an extension on the Words class
// . the array of QueryWords, m_qwords[], is contained in the Query class
// . we compute the QueryTerms (m_qterms[]) from the QueryWords
class QueryWord {

 public:
	bool isAlphaWord() { return is_alnum_utf8(m_word); }

	void constructor ();
	void destructor ();

	// this ptr references into the actual query
	char       *m_word    ;
	int32_t        m_wordLen ;
	// the length of the phrase, if any. it starts at m_word
	int32_t        m_phraseLen;
	// this is the term hash with collection and field name and
	// can be looked up directly in indexdb
	int64_t   m_wordId ;
	int64_t   m_phraseId;
	// hash of field name then collection, used to hash termId
	int64_t   m_prefixHash;
	int32_t        m_posNum;
	// are we in a phrase in a wikipedia title?
	int32_t        m_wikiPhraseId;

	// . this is just the hash of m_term and is used for highlighting, etc.
	// . it is 0 for terms in a field?
	int64_t   m_rawWordId ;
	int64_t   m_rawPhraseId ;

	// the field as a convenient numeric code
	char        m_fieldCode ;
	// . '-' means to exclude from search results
	// . '+' means to include in all search results
	// . if we're a phrase term, signs distribute across quotes
	char        m_wordSign;
	char        m_phraseSign;
	// this is 1 if the associated word is a valid query term but its
	// m_explicitBit is 0. we use this to save explicit bits for those
	// terms that need them (like those terms in complicated nested boolean
	// expressions) and just use a hardCount to see how many hard required
	// terms are contained by a document. see IndexTable.cpp "hardCount"
	char        m_hardCount;
	// the parenthetical level of this word in the boolean expression.
	// level 0 is the first level.
	char        m_level;
	// is this word a query stop word?
	bool        m_isQueryStopWord ; 
	// is it a plain stop word?
	bool        m_isStopWord ; 
	bool        m_isPunct;
	// are we an op code?
	char        m_opcode;
	// . the ignore code
	// . explains why this query term should be ignored
	// . see #define'd IGNORE_* codes above
	char        m_ignoreWord   ;
	char        m_ignorePhrase ;

	// so we ignore gbsortby:offerprice in bool expressions
	bool        m_ignoreWordInBoolQuery;

	// is this query single word in quotes?
	bool        m_inQuotes ; 
	// is this word in a phrase that is quoted?
	bool        m_inQuotedPhrase;
	// what word # does the quote we are in start at?
	int32_t        m_quoteStart;
	int32_t        m_quoteEnd; // inclusive!
	// are we connected to the alnum word on our left/right?
	bool        m_leftConnected;
	bool        m_rightConnected;
	// if we're in middle or right end of a phrase, where does it start?
	int32_t        m_leftPhraseStart;
	// . what QueryTerm does our "phrase" map to? NULL if none.
	// . this allows us to OR in extra bits into that QueryTerm's m_bits
	//   member that correspond to the single word constituents
	// . remember, m_bits is a bit vector that represents the QueryTerms
	//   a document contains
	class QueryTerm *m_queryPhraseTerm;
	// . what QueryTerm does our "word" map to? NULL if none.
	// . used by QueryBoolean since it uses QueryWords heavily
	class QueryTerm *m_queryWordTerm;

	// user defined weights
	int32_t m_userWeight;
	char m_userType;
	float m_userWeightPhrase;

	char m_userTypePhrase;
	bool m_queryOp;
	// is it after a NOT operator? i.e. NOT ( x UOR y UOR ... )
	bool m_underNOT;
	// is this query word before a | (pipe) operator?
	bool m_piped;

	// for min/max score ranges like gbmin:price:1.99
	float m_float;

	// for gbminint:99 etc. uses integers instead of floats for better res
	int32_t  m_int;

	// for holding some synonyms
	SafeBuf m_synWordBuf;

	// when an operand is an expression...
	class Expression *m_expressionPtr;
};

// . we filter the QueryWords and turn them into QueryTerms
// . QueryTerms are the important parts of the QueryWords
class QueryTerm {
 public:
	void constructor ( ) ;

	// the query word we were derived from
	QueryWord *m_qword;

	// . are we a phrase termid or single word termid from that QueryWord?
	// . the QueryWord instance represents both, so we must choose
	bool       m_isPhrase;

	// this is phraseId for phrases, and wordId for words
	int64_t  m_termId;

	// used by Matches.cpp
	int64_t  m_rawTermId;

	// sign of the phrase or word we used
	char       m_termSign;

	// our representative bit (up to 16 MAX_QUERY_TERMS)
	qvec_t     m_explicitBit;

	// usually this equal m_explicitBit, BUT if a word is repeated
	// in different areas of the doc, we union all the individual
	// explicit bits of that repeated word into this bit vec. it is
	// used by Matches.cpp only so far.
	qvec_t     m_matchesExplicitBits;

	// this is 1 if the associated word is a valid query term but its
	// m_explicitBit is 0. we use this to save explicit bits for those
	// terms that need them (like those terms in complicated nested boolean
	// expressions) and just use a hardCount to see how many hard required
	// terms are contained by a document. see IndexTable.cpp "hardCount"
	char       m_hardCount;

	// the "number" of the query term used for evaluation boolean
	// expressions in Expression::isTruth(). Basically just the
	// QueryTermInfo for which this query term belongs. each QueryTermInfo
	// is like a single query term and all its synonyms, etc.
	int32_t       m_bitNum;

	// point to term, either m_word or m_phrase
	char      *m_term;
	int32_t       m_termLen;

	// point to the posdblist that represents us
	class RdbList   *m_posdbListPtr;

	// languages query term is in. currently this is only valid for
	// synonyms of other query terms. so we can show what language the
	// synonym is for in the xml/json feed.
	uint64_t m_langIdBits;
	bool m_langIdBitsValid;

	int64_t   m_termFreq;
	float     m_termFreqWeight;

	// . our representative bits
	// . the bits in this bit vector is 1-1 with the QueryTerms
	// . if a doc has query term #i then bit #i will be set
	// . if a doc EXplicitly has phrase "A B" then it may have 
	//   term A and term B implicity
	// . therefore we also OR the bits for term A and B into m_implicitBits
	// . THIS SHIT SHOULD be just used in setBitScores() !!!
	qvec_t m_implicitBits;

	// Summary.cpp and Matches.cpp use this one
	bool m_isQueryStopWord ; 

	// IndexTable.cpp uses this one
	bool m_inQuotes;

	// user defined weight for this term, be it phrase or word
	float m_userWeight;
	char m_userType;

	// . is this query term before a | (pipe) operator?
	// . if so we must read the whole termlist, like m_underNOT above
	bool m_piped;

	// . we ignore component terms unless their compound term is not cached
	// . now this is used to ignore low tf synonym terms only
	bool m_ignored ;

	// is it part of a UOR chain?
	bool m_isUORed;
	QueryTerm *m_UORedTerm;

	// . if synonymOf is not NULL, then m_term points into m_synBuf, not
	//   m_buf
	QueryTerm *m_synonymOf;
	int64_t m_synWids0;
	int64_t m_synWids1;
	int32_t      m_numAlnumWordsInSynonym;

	// like if we are the "nj" syn of "new jersey", this will be 2 words
	// since "new jersey", our base, is 2 alnum words.
	int32_t      m_numAlnumWordsInBase;

	// copied from derived QueryWord
	char m_fieldCode  ;
	bool isSplit() const;
	bool m_isRequired;

	unsigned char  m_isWikiHalfStopBigram:1;

	// if a single word term, what are the term #'s of the 2 phrases
	// we can be in? uses -1 to indicate none.
	int32_t  m_leftPhraseTermNum;
	int32_t  m_rightPhraseTermNum;

	// same as above basically
	class QueryTerm *m_leftPhraseTerm;
	class QueryTerm *m_rightPhraseTerm;

	char m_startKey[MAX_KEY_BYTES];
	char m_endKey  [MAX_KEY_BYTES];
	char m_ks;
};

#define MAX_EXPRESSIONS 100

// operand1 AND operand2 OR  ...
// operand1 OR  operand2 AND ...
class Expression {
public:
	bool addExpression (int32_t start, 
			    int32_t end, 
			    Query   *q,
			    int32_t    level );
	bool isTruth(const unsigned char *bitVec, int32_t vecSize) const;
	// . what QueryTerms are UNDER the influence of the NOT opcode?
	// . we read in the WHOLE termlist of those that are (like '-' sign)
	// . returned bit vector is 1-1 with m_qterms in Query class
	void print (SafeBuf *sbuf);

	int32_t m_expressionStartWord;
	int32_t m_numWordsInExpression;
	const Query *m_q;
};

// . this is the main class for representing a query
// . it contains array of QueryWords (m_qwords[]) and QueryTerms (m_qterms[])
class Query {

 public:
	void reset();

	Query();
	~Query();
	void constructor();
	void destructor();

	// . returns false and sets g_errno on error
	// . after calling this you can call functions below
	bool set2 ( const char *query    , 
		    uint8_t  langId ,
		    bool     queryExpansion ,
		    bool     useQueryStopWords = true ,
		    int32_t  maxQueryTerms = 0x7fffffff );

	const char *getQuery() const { return m_orig; }
	int32_t     getQueryLen() const { return m_origLen; }

	int32_t     getNumTerms() const { return m_numTerms; }
	char        getTermSign(int32_t i) const { return m_qterms[i].m_termSign; }
	bool        isPhrase(int32_t i) const { return m_qterms[i].m_isPhrase; }
	int64_t     getTermId(int32_t i) const { return m_qterms[i].m_termId; }
	int64_t     getRawTermId (int32_t i) const { return m_qterms[i].m_rawTermId; }
	const char *getTerm(int32_t i) const { return m_qterms[i].m_term; }
	int32_t     getTermLen(int32_t i) const { return m_qterms[i].m_termLen; }
	bool        isSplit() const;

	// the new way as of 3/12/2014. just determine if matches the bool
	// query or not. let's try to offload the scoring logic to other places
	// if possible.
	// bitVec is all the QueryWord::m_opBits some docid contains, so
	// does it match our boolean query or not?
	bool matchesBoolQuery(const unsigned char *bitVec, int32_t vecSize) const;

	// sets m_qwords[] array, this function is the heart of the class
	bool setQWords ( char boolFlag , bool keepAllSingles ,
			 class Words &words , class Phrases &phrases ) ;

	// sets m_qterms[] array from the m_qwords[] array
	bool setQTerms ( class Words &words ) ;

	// helper funcs for parsing query into m_qwords[]
	bool        isConnection(const char *s, int32_t len) const;

	void dumpToLog() const;

 public:

	// hash of all the query terms
	int64_t getQueryHash() const;

	// return -1 if does not exist in query, otherwise return the 
	// query word num
	int32_t getWordNum(int64_t wordId) const;

	// . bit vector that is 1-1 with m_qterms[]
	// . only has bits that we must have if we were default AND
	qvec_t         m_requiredBits;
	qvec_t         m_matchRequiredBits;
	qvec_t         m_negativeBits;
	qvec_t         m_forcedBits;
	// bit vector for terms that are synonyms
	qvec_t         m_synonymBits;  
	int32_t           m_numRequired;

	// language of the query
	uint8_t m_langId;

	bool m_useQueryStopWords;

	// use a generic buffer for m_qwords and m_expressions to point into
	// so we don't have to malloc for them
	char      m_gbuf [ GBUF_SIZE ];
	char     *m_gnext;

	QueryWord *m_qwords;
	int32_t       m_numWords;
	int32_t       m_qwordsAllocSize;

	// QueryWords are converted to QueryTerms
	int32_t      m_numTerms;

	int32_t m_numTermsUntruncated;

	SafeBuf    m_stackBuf;
	QueryTerm *m_qterms         ;

	// site: field will disable site clustering
	// ip: field will disable ip clustering
	// site:, ip: and url: queries will disable caching
	bool m_hasPositiveSiteField;
	bool m_hasIpField;
	bool m_hasUrlField;
	bool m_hasSubUrlField;
	bool m_hasQuotaField;

	// . we set this to true if it is a boolean query
	// . when calling Query::set() above you can tell it explicitly
	//   if query is boolean or not, OR you can tell it to auto-detect
	//   by giving different values to the "boolFlag" parameter.
	bool m_isBoolean;

	// if they got a gbdocid: in the query and it's not boolean, set these
	int64_t m_docIdRestriction;

	// for holding the filtered query, in utf8
	SafeBuf m_sb;
	char m_tmpBuf3[128];

	char *m_orig;
	int32_t m_origLen;
	SafeBuf m_osb;
	char m_otmpBuf[128];

	// . we now contain the parsing components for boolean queries
	// . m_expressions points into m_gbuf or is allocated
	Expression        m_expressions[MAX_EXPRESSIONS];
	int32_t              m_numExpressions;

	int32_t m_maxQueryTerms ;

	bool m_queryExpansion;

	bool m_truncated;

	bool m_hasUOR;
};

#endif // GB_QUERY_H
