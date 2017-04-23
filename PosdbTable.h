#ifndef GB_POSDB_TABLE_H
#define GB_POSDB_TABLE_H

#include "RdbList.h"
#include "HashTableX.h"


float getDiversityWeight ( unsigned char diversityRank );
float getDensityWeight   ( unsigned char densityRank );
float getWordSpamWeight  ( unsigned char wordSpamRank );
float getLinkerWeight    ( unsigned char wordSpamRank );
float getHashGroupWeight ( unsigned char hg );

#define WIKI_WEIGHT    0.10 // was 0.20
#define SITERANKDIVISOR 3.0
#define SITERANKMULTIPLIER 0.33333333

void printTermList ( int32_t i, const char *list, int32_t listSize ) ;

// if query is 'the tigers' we weight bigram "the tigers" x 1.20 because
// its in wikipedia.
// up this to 1.40 for 'the time machine' query
#define WIKI_BIGRAM_WEIGHT 1.40



//forward declarations
class TopTree;
class Msg2;
class Msg39Request;
class DocIdScore;
class Query;
class QueryTerm;


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
	
	// delNonMatchingDocIdsFromSubLists() set these. They
	// point to m_subLists that have been reduced in size 
	// to only contain the docids matching all required term ids
	int32_t   m_matchingSubListSize		    [MAX_SUBLISTS];
	const char *m_matchingSubListStart	[MAX_SUBLISTS];
	const char *m_matchingSubListEnd	[MAX_SUBLISTS];
	const char *m_matchingSubListCursor	[MAX_SUBLISTS];
	const char *m_matchingSubListSavedCursor[MAX_SUBLISTS];
	int32_t   m_numMatchingSubLists;
	
	
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


class PosdbTable {

 public:

	// . returns false on error and sets errno
	// . "termFreqs" are 1-1 with q->m_qterms[]
	// . sets m_q to point to q
	void init(Query *q, bool debug, void *logstate, TopTree *topTree, Msg2 *msg2, Msg39Request *r);

	// pre-allocate m_whiteListTable
	bool allocWhiteListTable ( ) ;

	void prepareWhiteListTable();

	// pre-allocate memory since intersection runs in a thread
	bool allocTopScoringDocIdsData();

	float getMaxScoreForNonBodyTermPair(const char *wpi,  const char *wpj, const char *endi, const char *endj, int32_t qdist);
	float getBestScoreSumForSingleTerm(int32_t i, const char *wpi, const char *endi, DocIdScore *pdcs, const char **highestScoringNonBodyPos);
	float getScoreForTermPair(const char *wpi, const char *wpj, int32_t fixedDistance);
	void findMinTermPairScoreInWindow(const char **ptrs, const char **highestScoringNonBodyPos, float *scoreMatrix);

	float getTermPairScoreForAny   ( int32_t i, int32_t j,
					 const char *wpi, const char *wpj, 
					 const char *endi, const char *endj,
					 DocIdScore *pdcs );


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

	// functions used by intersectlist
	bool genDebugScoreInfo1(int32_t *numProcessed, int32_t *topCursor, QueryTermInfo *qtibuf);
	bool genDebugScoreInfo2(DocIdScore *dcs, int32_t *lastLen, uint64_t *lastDocId, char siteRank, float score, int32_t intScore, char docLang);
	bool advanceTermListCursors(const char *docIdPtr, QueryTermInfo *qtibuf);
	bool prefilterMaxPossibleScoreByDistance(QueryTermInfo *qtibuf, const int32_t *qpos, float minWinningScore);
	void mergeTermSubListsForDocId(QueryTermInfo *qtibuf, char *miniMergeBuf, const char **miniMergedList, const char **miniMergedEnd, int *highestInlinkSiteRank);

	void createNonBodyTermPairScoreMatrix(const char **miniMergedList, const char **miniMergedEnd, float *scoreMatrix);
	float getMinSingleTermScoreSum(const char **miniMergedList, const char **miniMergedEnd, const char **highestScoringNonBodyPos, DocIdScore *pdcs);
	float getMinTermPairScoreSlidingWindow(const char **miniMergedList, const char **miniMergedEnd, const char **highestScoringNonBodyPos, const char **winnerStack, const char **xpos, float *scoreMatrix, DocIdScore *pdcs);


	uint64_t m_docId;

	bool m_hasMaxSerpScore;

	float m_siteRankMultiplier;

	// how long to add the last batch of lists
	int64_t       m_addListsTime;
	int64_t       m_t1 ;
	int64_t       m_t2 ;

	int64_t       m_estimatedTotalHits;

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

	// Best minimum score in a "sliding window"
	float m_bestMinTermPairWindowScore;
	// Position pointers of best minimum score
	const char **m_bestMinTermPairWindowPtrs;

	// how many docs in the collection?
	int64_t m_docsInColl;

	Msg2 *m_msg2;

	// if getting more than MAX_RESULTS results, use this top tree to hold
	// them rather than the m_top*[] arrays above
	TopTree *m_topTree;

	SafeBuf m_scoreInfoBuf;
	SafeBuf m_pairScoreBuf;
	SafeBuf m_singleScoreBuf;

	SafeBuf m_topScoringDocIdsBuf;	// Buffer containing pointers to scoring info

	// a reference to the query
	Query          *m_q;
	int32_t m_nqt;

	// has init() been called?
	bool            m_initialized;

	// are we in debug mode?
	bool m_debug;

	// for debug msgs
	void *m_logstate;

	Msg39Request *m_msg39req;

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
	void delNonMatchingDocIdsFromSubLists( QueryTermInfo *qti );

	// for intersecting docids
	void addDocIdVotes( const QueryTermInfo *qti , int32_t listGroupNum );
	void makeDocIdVoteBufForRarestTerm( const QueryTermInfo *qti , bool isRangeTerm );
	bool makeDocIdVoteBufForBoolQuery() ;
	void delDocIdVotes ( const QueryTermInfo *qti );	// for negative query terms...
	bool findCandidateDocIds();


	// upper score bound
	float getMaxPossibleScore ( const QueryTermInfo *qti ,
				    int32_t bestDist ,
				    int32_t qdist ,
				    const QueryTermInfo *qtm ) ;

	int64_t getTotalHits() const { return m_docIdVoteBuf.length() / 6; }

	// stuff set in setQueryTermInf() function:
	SafeBuf              m_qiBuf;
	int32_t                 m_numQueryTermInfos;
	// the size of the smallest set of sublists. each sublists is
	// the main term or a synonym, etc. of the main term.
	int32_t                 m_minTermListSize;
	// which query term info has the smallest set of sublists
	int32_t                 m_minTermListIdx;
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
	char        m_usePageTemperature;
	char        m_reserved1;
	char        m_reserved2;
	int32_t        m_docLang; // langId
	int32_t        m_numRequiredTerms;
	// NEW 20170423
	float		m_adjustedSiteRank;
	double		m_pageTemperature;


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


void reinitializeRankingSettings();

#endif // GB_POSDB_TABLE_H
