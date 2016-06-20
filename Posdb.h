// Matt Wells, Copyright May 2012

// . format of an 18-byte posdb key
//   tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
//   tttttttt tttttttt dddddddd dddddddd  d = docId (38 bits)
//   dddddddd dddddddd dddddd0r rrrggggg  r = siterank, g = langid
//   wwwwwwww wwwwwwww wwGGGGss ssvvvvFF  w = word postion , s = wordspamrank
//   pppppb1N MMMMLZZD                    v = diversityrank, p = densityrank
//                                        M = multiplier, b = in outlink text
//                                        L = langIdShiftBit (upper bit)
//   G: 0 = body 
//      1 = intitletag 
//      2 = inheading 
//      3 = inlist 
//      4 = inmetatag
//      5 = inlinktext
//      6 = tag
//      7 = inneighborhood
//      8 = internalinlinktext
//      9 = inurl
//     10 = inmenu
//
//   F: 0 = original term
//      1 = conjugate/sing/plural
//      2 = synonym
//      3 = hyponym

//   NOTE: N bit is 1 if the shard of the record is determined by the
//   termid (t bits) and NOT the docid (d bits). N stands for "nosplit"
//   and you can find that logic in XmlDoc.cpp and Msg4.cpp. We store 
//   the hash of the content like this so we can see if it is a dup.

//   NOTE: M bits hold scaling factor (logarithmic) for link text voting
//   so we do not need to repeat the same link text over and over again.
//   Use M bits to hold # of inlinks the page has for other terms.

//   NOTE: for inlinktext terms the spam rank is the siterank of the
//   inlinker!

//   NOTE: densityrank for title is based on # of title words only. same goes
//   for incoming inlink text.

//   NOTE: now we can b-step into the termlist looking for a docid match
//   and not worry about misalignment from the double compression scheme
//   because if the 6th byte's low bit is clear that means its a docid
//   12-byte key, otherwise its the word position 6-byte key since the delbit
//   can't be clear for those!

//   THEN we can play with a tuner for how these various things affect
//   the search results ranking.


#ifndef GB_POSDB_H
#define GB_POSDB_H

#include "Rdb.h"
#include "Conf.h"
#include "Titledb.h" // DOCID_MASK
#include "HashTableX.h"
#include "Sections.h"

// Ugly - but so is lots of code in .h files
extern void gbshutdownAbort( bool save_on_abort );


#define MAXSITERANK      0x0f // 4 bits
#define MAXLANGID        0x3f // 6 bits (5 bits go in 'g' the other in 'L')
#define MAXWORDPOS       0x0003ffff // 18 bits
#define MAXDENSITYRANK   0x1f // 5 bits
#define MAXWORDSPAMRANK  0x0f // 4 bits
#define MAXDIVERSITYRANK 0x0f // 4 bits
#define MAXHASHGROUP     0x0f // 4 bits
#define MAXMULTIPLIER    0x0f // 4 bits
#define MAXISSYNONYM     0x03 // 2 bits

// values for G bits in the posdb key
#define HASHGROUP_BODY                 0 // body implied
#define HASHGROUP_TITLE                1 
#define HASHGROUP_HEADING              2 // body implied
#define HASHGROUP_INLIST               3 // body implied
#define HASHGROUP_INMETATAG            4
#define HASHGROUP_INLINKTEXT           5
#define HASHGROUP_INTAG                6
#define HASHGROUP_NEIGHBORHOOD         7
#define HASHGROUP_INTERNALINLINKTEXT   8
#define HASHGROUP_INURL                9
#define HASHGROUP_INMENU               10 // body implied
#define HASHGROUP_END                  11

float getDiversityWeight ( unsigned char diversityRank );
float getDensityWeight   ( unsigned char densityRank );
float getWordSpamWeight  ( unsigned char wordSpamRank );
float getLinkerWeight    ( unsigned char wordSpamRank );
const char *getHashGroupString ( unsigned char hg );
float getHashGroupWeight ( unsigned char hg );
float getTermFreqWeight  ( int64_t termFreq , int64_t numDocsInColl );

#define WIKI_WEIGHT    0.10 // was 0.20
#define SITERANKDIVISOR 3.0
#define SITERANKMULTIPLIER 0.33333333

#define POSDBKEY key144_t

#define TERMID_MASK (0x0000ffffffffffffLL)

void printTermList ( int32_t i, const char *list, int32_t listSize ) ;

// if query is 'the tigers' we weight bigram "the tigers" x 1.20 because
// its in wikipedia.
// up this to 1.40 for 'the time machine' query
#define WIKI_BIGRAM_WEIGHT 1.40

class Posdb {

 public:

	// resets rdb
	void reset();

	// sets up our m_rdb from g_conf (global conf class)
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( int32_t treeMem );

	bool verify ( const char *coll );

	bool addColl ( const char *coll, bool doVerify = true );

	// . make a 16-byte key from all these components
	// . since it is 16 bytes, the big bit will be set
	static void makeKey ( void              *kp             ,
		       int64_t          termId         ,
		       uint64_t docId          , 
		       int32_t               wordPos        ,
		       char               densityRank    ,
		       char               diversityRank  ,
		       char               wordSpamRank   ,
		       char               siteRank       ,
		       char               hashGroup      ,
		       char               langId         ,
		       // multiplier: we convert into 7 bits in this function
		       int32_t               multiplier     ,
		       bool               isSynonym      ,
		       bool               isDelKey       ,
		       bool               shardByTermId  );


	int printList ( RdbList &list ) ;

	// we map the 32bit score to like 7 bits here
	static void setMultiplierBits ( void *vkp , unsigned char mbits ) {
		key144_t *kp = (key144_t *)vkp;
		if ( mbits > MAXMULTIPLIER ) { gbshutdownAbort(true); }
		kp->n0 &= 0xfc0f;
		// map score to bits
		kp->n0 |= ((uint16_t)mbits) << 4;
	}
	
	static void setDocIdBits ( void *vkp , uint64_t docId ) {
		key144_t *kp = (key144_t *)vkp;
		kp->n1 &= 0x000003ffffffffffLL;
		kp->n1 |= (docId<<(32+10));
		kp->n2 &= 0xffffffffffff0000LL;
		kp->n2 |= docId>>22;
	}
	
	static void setSiteRankBits ( void *vkp , char siteRank ) {
		key144_t *kp = (key144_t *)vkp;
		if ( siteRank > MAXSITERANK ) { gbshutdownAbort(true); }
		kp->n1 &= 0xfffffe1fffffffffLL;
		kp->n1 |= ((uint64_t)siteRank)<<(32+5);
	}
	
	static void setLangIdBits ( void *vkp , char langId ) {
		key144_t *kp = (key144_t *)vkp;
		if ( langId > MAXLANGID ) { gbshutdownAbort(true); }
		kp->n1 &= 0xffffffe0ffffffffLL;
		// put the lower 5 bits here
		kp->n1 |= ((uint64_t)(langId&0x1f))<<(32);
		// and the upper 6th bit here. n0 is a int16_t.
		// 0011 1111
		if ( langId & 0x20 ) kp->n0 |= 0x08;
	}

	// set the word position bits et al to this float
	static void setFloat ( void *vkp , float f ) {
		*(float *)(((char *)vkp) + 2) = f; }

	static void setInt ( void *vkp , int32_t x ) {
		*(int32_t *)(((char *)vkp) + 2) = x; }

	// and read the float as well
	static float getFloat ( const void *vkp ) {
		return *(const float *)(((char *)vkp) + 2); }

	static int32_t getInt ( const void *vkp ) {
		return *(const int32_t *)(((char *)vkp) + 2); }

	static void setAlignmentBit ( void *vkp , char val ) {
		char *p = (char *)vkp;
		if ( val ) p[1] = p[1] | 0x02;
		else       p[1] = p[1] & 0xfd;
	}

	static bool isAlignmentBitClear ( const void *vkp ) {
		return ( ( ((const char *)vkp)[1] & 0x02 ) == 0x00 );
	}

	static void makeStartKey ( void *kp, int64_t termId , 
			    int64_t docId=0LL){
		return makeKey ( kp,
				 termId , 
				 docId,
				 0, // wordpos
				 0, // density
				 0, // diversity
				 0, // wordspam
				 0, // siterank
				 0, // hashgroup
				 0, // langid
				 0, // multiplier
				 0, // issynonym/etc.
				 true ,  // isdelkey
				 false ); // shardbytermid?
	}

	static void makeEndKey  ( void *kp,int64_t termId, 
			   int64_t docId = MAX_DOCID ) {
		return makeKey ( kp,
				 termId , 
				 docId,
				 MAXWORDPOS,
				 MAXDENSITYRANK,
				 MAXDIVERSITYRANK,
				 MAXWORDSPAMRANK,
				 MAXSITERANK,
				 MAXHASHGROUP,
				 MAXLANGID,
				 MAXMULTIPLIER,
				 true, // issynonym/etc.
				 false, // isdelkey
				 true);// shard by termid?
	}

	// we got two compression bits!
	static unsigned char getKeySize ( const void *key ) {
		if ( (((const char *)key)[0])&0x04 ) return 6;
		if ( (((const char *)key)[0])&0x02 ) return 12;
		return 18;
	}

	// PosdbTable uses this to skip from one docid to the next docid
	// in a posdblist
	static char *getNextDocIdSublist ( char *p ,  char *listEnd ) {
		// key must be 12
		//if ( getKeySize(p) != 12 ) { gbshutdownAbort(true); }
		// skip that first key
		p += 12;
		// skip the 6 byte keys
		for ( ; p < listEnd && getKeySize(p) == 6 ; p += 6 );
		// done
		return p;
	}
		

	static int64_t getTermId ( const void *key ) {
		return ((const key144_t *)key)->n2 >> 16;
	}

	static int64_t getDocId ( const void *key ) {
		uint64_t d;
		d = ((const unsigned char *)key)[11];
		d <<= 32;
		d |= *(const uint32_t *)(((unsigned char *)key)+7);
		d >>= 2;
		return d;
		//int64_t d = ((key144_t *)key)->n2 & 0xffff;
		//d <<= 22;
		//d |= ((key144_t *)key)->n1 >> (32+8+2);
		//return d;
	}

	static unsigned char getSiteRank ( const void *key ) {
		return (((const key144_t *)key)->n1 >> 37) & MAXSITERANK;
	}

	static unsigned char getLangId ( const void *key ) {
		if ( ((const char *)key)[0] & 0x08 )
			return ((((const key144_t *)key)->n1 >> 32) & 0x1f) | 0x20;
		else
			return ((((const key144_t *)key)->n1 >> 32) & 0x1f) ;
	}

	static unsigned char getHashGroup ( const void *key ) {
		//return (((key144_t *)key)->n1 >> 10) & MAXHASHGROUP;
		return ((((const unsigned char *)key)[3]) >>2) & MAXHASHGROUP;
	}

	static int32_t getWordPos ( const void *key ) {
		//return (((key144_t *)key)->n1 >> 14) & MAXWORDPOS;
		return (*((const uint32_t *)((unsigned char *)key+2))) >> (8+6);
	}

	static inline void setWordPos ( char *key , uint32_t wpos ) {
		// truncate
		wpos &= MAXWORDPOS;
		if ( wpos & 0x01 ) key[3] |= 0x40;
		else               key[3] &= ~((unsigned char)0x40);
		if ( wpos & 0x02 ) key[3] |= 0x80;
		else               key[3] &= ~((unsigned char)0x80);
		wpos >>= 2;
		key[4] = ((char *)&wpos)[0];
		key[5] = ((char *)&wpos)[1];
	}

	static unsigned char getWordSpamRank ( const void *key ) {
		//return (((const key144_t *)key)->n1 >> 6) & MAXWORDSPAMRANK;
		return ((((const uint16_t *)key)[1]) >>6) & MAXWORDSPAMRANK;
	}

	static unsigned char getDiversityRank ( const void *key ) {
		//return (((const key144_t *)key)->n1 >> 2) & MAXDIVERSITYRANK;
		return ((((const unsigned char *)key)[2]) >>2) & MAXDIVERSITYRANK;
	}

	static unsigned char getIsSynonym ( const void *key ) {
		return (((const key144_t *)key)->n1 ) & 0x03;
	}

	static unsigned char getIsHalfStopWikiBigram ( const void *key ) {
		return ((const char *)key)[2] & 0x01;
	}

	static unsigned char getDensityRank ( const void *key ) {
		return ((*(const uint16_t *)key) >> 11) & MAXDENSITYRANK;
	}

	static inline void setDensityRank ( char *key , unsigned char dr ) {
		// shift up
		dr <<= 3;
		// clear out
		key[1] &= 0x07;
		// or in
		key[1] |= dr;
	}

	static char isShardedByTermId ( const void *key ) {return ((const char *)key)[1] & 0x01; }

	static void setShardedByTermIdBit ( void *key ) { 
		char *k = (char *)key;
		k[1] |= 0x01;
	}

	static unsigned char getMultiplier ( const void *key ) {
		return ((*(const uint16_t *)key) >> 4) & MAXMULTIPLIER; }

	int64_t getTermFreq ( collnum_t collnum, int64_t termId ) ;

	//RdbCache *getCache ( ) { return &m_rdb.m_cache; }
	Rdb      *getRdb   ( ) { return &m_rdb; }

private:
	Rdb m_rdb;
};

#define MAX_SUBLISTS 50

// . each QueryTerm has this attached additional info now:
// . these should be 1-1 with query terms, Query::m_qterms[]
class QueryTermInfo {
public:
	class QueryTerm *m_qt;
	// the required lists for this query term, synonym lists, etc.
	RdbList  *m_subLists        [MAX_SUBLISTS];
	// flags to indicate if bigram list should be scored higher
	char      m_bigramFlags     [MAX_SUBLISTS];
	// shrinkSubLists() set this:
	int32_t      m_newSubListSize  [MAX_SUBLISTS];
	char     *m_newSubListStart [MAX_SUBLISTS];
	char     *m_newSubListEnd   [MAX_SUBLISTS];
	char     *m_cursor          [MAX_SUBLISTS];
	char     *m_savedCursor     [MAX_SUBLISTS];
	// the corresponding QueryTerm for this sublist
	//class QueryTerm *m_qtermList [MAX_SUBLISTS];
	int32_t      m_numNewSubLists;
	// how many are valid?
	int32_t      m_numSubLists;
	// size of all m_subLists in bytes
	int64_t m_totalSubListsSize;
	// the term freq weight for this term
	float     m_termFreqWeight;
	// what query term # do we correspond to in Query.h
	int32_t      m_qtermNum;
	// the word position of this query term in the Words.h class
	int32_t      m_qpos;
	// the wikipedia phrase id if we start one
	int32_t      m_wikiPhraseId;
	// phrase id term or bigram is in
	int32_t      m_quotedStartId;
};

#include "Query.h"         // MAX_QUERY_TERMS, qvec_t

//forward declarations
class TopTree;
class Msg2;
class Msg39Request;
class DocIdScore;


class PosdbTable {

 public:

	// . returns false on error and sets errno
	// . "termFreqs" are 1-1 with q->m_qterms[]
	// . sets m_q to point to q
	void init (Query         *q               ,
		   char           debug           ,
		   void          *logstate        ,
		   TopTree       *topTree,
		   Msg2          *msg2, 
		   Msg39Request  *r );

	// pre-allocate m_whiteListTable
	bool allocWhiteListTable ( ) ;

	void prepareWhiteListTable();

	// pre-allocate memory since intersection runs in a thread
	bool allocTopTree ( );

	void  getTermPairScoreForNonBody   ( int32_t i, int32_t j,
					     const char *wpi,  const char *wpj, 
					     const char *endi, const char *endj,
					     int32_t qdist ,
					     float *retMax );
	float getSingleTermScore ( int32_t i, char *wpi , char *endi,
				   DocIdScore *pdcs,
				   char **bestPos );

	void evalSlidingWindow ( char **ptrs , 
				 int32_t   nr , 
				 char **bestPos ,
				 float *scoreMatrix  ,
				 int32_t   advancedTermNum );
	float getTermPairScoreForWindow ( int32_t i, int32_t j,
					  const char *wpi,
					  const char *wpj,
					  int32_t fixedDistance
					  );

	float getTermPairScoreForAny   ( int32_t i, int32_t j,
					 const char *wpi, const char *wpj, 
					 const char *endi, const char *endj,
					 DocIdScore *pdcs );

	bool makeDocIdVoteBufForBoolQuery_r ( ) ;

	// some generic stuff
	PosdbTable();
	~PosdbTable();
	void reset();

	// Msg39 needs to call these
	void freeMem ( ) ;

	// has init already been called?
	bool isInitialized() {
		return m_initialized;
	}

	uint64_t m_docId;

	bool m_hasMaxSerpScore;

	// hack for seo.cpp:
	float m_finalScore;
	float m_preFinalScore;

	float m_siteRankMultiplier;

	// how long to add the last batch of lists
	int64_t       m_addListsTime;
	int64_t       m_t1 ;
	int64_t       m_t2 ;

	int64_t       m_estimatedTotalHits;

	int32_t            m_errno;

	int32_t            m_numSlots;

	int32_t            m_maxScores;

	collnum_t       m_collnum;

	int32_t *m_qpos;
	int32_t *m_wikiPhraseIds;
	int32_t *m_quotedStartIds;
	int32_t  m_qdist;
	float *m_freqWeights;
	char  *m_bflags;
	int32_t  *m_qtermNums;
	float m_bestWindowScore;
	char **m_windowTermPtrs;

	// how many docs in the collection?
	int64_t m_docsInColl;

	Msg2 *m_msg2;

	// if getting more than MAX_RESULTS results, use this top tree to hold
	// them rather than the m_top*[] arrays above
	TopTree *m_topTree;

	SafeBuf m_scoreInfoBuf;
	SafeBuf m_pairScoreBuf;
	SafeBuf m_singleScoreBuf;

	SafeBuf m_stackBuf;

	// a reference to the query
	Query          *m_q;
	int32_t m_nqt;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	char            m_debug;

	// for debug msgs
	void *m_logstate;

	Msg39Request *m_r;

	// for gbsortby:item.price ...
	int32_t m_sortByTermNum;
	int32_t m_sortByTermNumInt;

	// fix core with these two
	int32_t m_sortByTermInfoNum;
	int32_t m_sortByTermInfoNumInt;

	// for gbmin:price:1.99
	int32_t m_minScoreTermNum;
	int32_t m_maxScoreTermNum;

	// for gbmin:price:1.99
	float m_minScoreVal;
	float m_maxScoreVal;

	// for gbmin:count:99
	int32_t m_minScoreTermNumInt;
	int32_t m_maxScoreTermNumInt;

	// for gbmin:count:99
	int32_t m_minScoreValInt;
	int32_t m_maxScoreValInt;


	// the new intersection/scoring algo
	void intersectLists10_r ( );	

	HashTableX m_whiteListTable;
	bool m_useWhiteTable;
	bool m_addedSites;

	// sets stuff used by intersect10_r()
	bool setQueryTermInfo ( );

	void shrinkSubLists ( QueryTermInfo *qti );

	// for intersecting docids
	void addDocIdVotes ( const QueryTermInfo *qti , int32_t listGroupNum );

	// for negative query terms...
	void rmDocIdVotes ( const QueryTermInfo *qti );

	// upper score bound
	float getMaxPossibleScore ( const QueryTermInfo *qti ,
				    int32_t bestDist ,
				    int32_t qdist ,
				    const QueryTermInfo *qtm ) ;

	// stuff set in setQueryTermInf() function:
	SafeBuf              m_qiBuf;
	int32_t                 m_numQueryTermInfos;
	// the size of the smallest set of sublists. each sublists is
	// the main term or a synonym, etc. of the main term.
	int32_t                 m_minListSize;
	// which query term info has the smallest set of sublists
	int32_t                 m_minListi;
	// intersect docids from each QueryTermInfo into here
	SafeBuf              m_docIdVoteBuf;

	int32_t m_filtered;

	// boolean truth table for boolean queries
	HashTableX m_bt;
	HashTableX m_ct;
	// size of the data slot in m_bt
	int32_t m_vecSize;

	// are all positive query terms in same wikipedia phrase like
	// 'time enough for love'?
	bool m_allInSameWikiPhrase;

	int32_t m_realMaxTop;
};


// distance used when measuring word from title/linktext/etc to word in body
#define FIXED_DISTANCE 400

class PairScore {
 public:
	int32_t  m_wordPos1;
	int32_t  m_wordPos2;
	int64_t m_termFreq1;
	int64_t m_termFreq2;
	float     m_tfWeight1;
	float     m_tfWeight2;
	int32_t m_qtermNum1;
	int32_t m_qtermNum2;
	int32_t m_qdist;
	float m_finalScore;
	char  m_isSynonym1;
	char  m_isSynonym2;
	char  m_isHalfStopWikiBigram1;
	char  m_isHalfStopWikiBigram2;
	char  m_diversityRank1;
	char  m_diversityRank2;
	char  m_densityRank1;
	char  m_densityRank2;
	char  m_wordSpamRank1;
	char  m_wordSpamRank2;
	char  m_hashGroup1;
	char  m_hashGroup2;
	char  m_inSameWikiPhrase;
	char  m_fixedDistance;
	char m_bflags1;
	char m_bflags2;
};

class SingleScore {
 public:
	int64_t m_termFreq;
	float   m_finalScore;
	int32_t m_wordPos;
	float   m_tfWeight;
	int32_t m_qtermNum;
	char    m_isSynonym;
	char    m_isHalfStopWikiBigram;
	char    m_diversityRank;
	char    m_densityRank;
	char    m_wordSpamRank;
	char    m_hashGroup;
	char    m_bflags;
	char    m_reserved0;
};
//above struct members are sorted on size as to minimize internal padding and final size


// we add up the pair scores of this many of the top-scoring pairs
// for inlink text only, so it is accumulative. but now we also
// have a parm "m_realMaxTop" which is <= MAX_TOP and can be used to
// tune this down.
#define MAX_TOP 10

// transparent query scoring info per docid
class DocIdScore {
 public:
	DocIdScore ( ) { reset(); }

	void reset ( ) {
		memset(this,0,sizeof(*this));
	}

	// we use QueryChange::getDebugDocIdScore() to "deserialize" per se
	bool serialize   ( class SafeBuf *sb );

	int64_t   m_docId;
	// made this a double because of intScores which can't be captured
	// fully with a float. intScores are used to sort by spidered time
	// for example. see Posdb.cpp "intScore".
	double      m_finalScore;
	char        m_siteRank;
	char        m_reserved0;
	char        m_reserved1;
	char        m_reserved2;
	int32_t        m_docLang; // langId
	int32_t        m_numRequiredTerms;

	int32_t m_numPairs;
	int32_t m_numSingles;

	// . m_pairScores is just all the term pairs serialized
	// . they contain their query term #1 of each term in the pair and
	//   they have the match number for each pair, since now each
	//   pair of query terms can have up to MAX_TOP associated pairs
	//   whose scores we add together to get the final score for that pair
	// . record offset into PosdbTable::m_pairScoreBuf
	// . Msg39Reply::ptr_pairScoreBuf will be this
	int32_t m_pairsOffset;
	// . record offset into PosdbTable.m_singleScoreBuf
	// . Msg39Reply::ptr_singleScoreBuf will be this
	int32_t m_singlesOffset;

	// Msg3a.cpp::mergeLists() should set these ptrs after it
	// copies over a top DocIdScore for storing the final results array
	class PairScore   *m_pairScores;
	class SingleScore *m_singleScores;
};


extern Posdb g_posdb;
extern Posdb g_posdb2;
extern RdbCache g_termFreqCache;

void reinitializeRankingSettings();

#endif // GB_POSDB_H
