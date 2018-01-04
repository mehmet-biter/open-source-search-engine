#include "PosdbTable.h"
#include "Posdb.h"

#include "gb-include.h"

#include "PageTemperatureRegistry.h"
#include "Docid2Siteflags.h"
#include "ScalingFunctions.h"
#include "ScoringWeights.h"
#include "BitOperations.h"
#include "Msg2.h"
#include "Msg39.h"
#include "Sanity.h"
#include "Stats.h"
#include "Conf.h"
#include "TopTree.h"
#include "DocumentIndexChecker.h"
#include "Lang.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include <math.h>
#include <valarray>

#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#include <valgrind/helgrind.h>
#endif

#define BF_HALFSTOPWIKIBIGRAM 0x01  // "to be" in "to be or not to be"
#define BF_PIPED              0x02  // before a query pipe operator
#define BF_SYNONYM            0x04
#define BF_NEGATIVE           0x08  // query word has a negative sign before it
#define BF_BIGRAM             0x10
#define BF_NUMBER             0x20  // is it like gbsortby:price? numeric?

static const int INTERSECT_SCORING    = 0;
static const int INTERSECT_DEBUG_INFO = 1;


static bool  s_init = false;
static GbMutex s_mtx_weights;
static DerivedScoringWeights s_scoringWeights;
static bool  s_isCompatible     [HASHGROUP_END][HASHGROUP_END];
static bool  s_inBody           [HASHGROUP_END];


#define gbmin(a,b) ((a)<(b) ? (a) : (b))
#define gbmax(a,b) ((a)>(b) ? (a) : (b))

static inline const char *getWordPosList(uint64_t docId, const char *list, int32_t listSize);
static int docIdVoteBufKeyCompare_desc ( const void *h1, const void *h2 );
static void initWeights();



//struct used for the mini-merges (see mergeTermSubListsForDocId() etc)
struct MiniMergeBuffer {
	//the simpl merge buffer
	char buffer[300000];
	//the bufptr-to-which-term-did-it-come-from mapping. posbd keys in 'buffer' above is always a multiple of 6 so we only need a sixth
	int16_t termIndex[300000/6];
	std::vector<const char *> mergedListStart;
	std::vector<const char *> mergedListEnd;
	MiniMergeBuffer(int numQueryTerms)
	  : mergedListStart(numQueryTerms),
	    mergedListEnd(numQueryTerms)
	{
#ifdef _VALGRIND_
		VALGRIND_MAKE_MEM_UNDEFINED(termIndex,sizeof(termIndex));
#endif
	}
	int16_t *getTermIndexPtrForBufferPos(const char *ptr) {
		size_t bufferOffset = (size_t)(ptr-buffer)/6;
		return termIndex+bufferOffset;
	}
	const int16_t *getTermIndexPtrForBufferPos(const char *ptr) const {
		size_t bufferOffset = (size_t)(ptr-buffer)/6;
		return termIndex+bufferOffset;
	}
	int16_t getTermIndexForBufferPos(const char *ptr) const {
		return *getTermIndexPtrForBufferPos(ptr);
	}
};


// A 2D matrix used by createNonBodyTermPairScoreMatrix/findMinTermPairScoreInWindow/getMinTermPairScoreSlidingWindow
class PairScoreMatrix {
	std::vector<float> m;
	const int columns;
public:
	PairScoreMatrix(int size)
	  : m(size*size),
	    columns(size)
	    {}
	float get(int x, int y) const {
		return m[y*columns+x];
	}
	void set(int x, int y, float value) {
		m[y*columns+x] = value;
	}
};



//////////////////
//
// THE NEW INTERSECTION LOGIC
//
//////////////////


PosdbTable::PosdbTable() { 
	// top docid info
	m_q             = NULL;
	m_msg39req        = NULL;
	reset();
}


PosdbTable::~PosdbTable() { 
	reset(); 
}


void PosdbTable::reset() {
	// has init() been called?
	m_initialized          = false;
	//freeMem(); // not implemented
	// does not free the mem of this safebuf, only resets length
	m_docIdVoteBuf.reset();
	m_filtered = 0;
	m_queryTermInfos.clear();
	// assume no-op
	m_t1 = 0LL;
	m_whiteListTable.reset();
	m_addedSites = false;

	// Coverity
	m_docId = 0;
	m_hasMaxSerpScore = false;
	m_addListsTime = 0;
	m_t2 = 0;
	m_qpos.clear();
	m_wikiPhraseIds.clear();
	m_quotedStartIds.clear();
	m_bflags.clear();
	m_qtermNums.clear();
	m_bestMinTermPairWindowScore = 0.0;
	m_msg2 = NULL;
	m_topTree = NULL;
	m_nqt = 0;
	m_debug = false;
	m_sortByTermNum = -1;
	m_sortByTermNumInt = -1;
	m_sortByTermInfoNum = 0;
	m_sortByTermInfoNumInt = 0;
	m_useWhiteTable = false;
	m_numQueryTermInfos = 0;
	m_minTermListSize = 0;
	m_minTermListIdx = 0;
	m_vecSize = 0;
	m_allInSameWikiPhrase = 0;
	m_realMaxTop = 0;
}


// realloc to save mem if we're rat
void PosdbTable::freeMem ( ) {
	//@todo: ?
}



// . returns false on error and sets g_errno
// . NOTE: termFreqs is just referenced by us, not copied
// . sets m_startKeys, m_endKeys and m_minNumRecs for each termId
// . TODO: ensure that m_termFreqs[] are all UPPER BOUNDS on the actual #!!
//         we should be able to get an upper bound estimate from the b-tree
//         quickly using Msg36!
// . we now support multiple plus signs before the query term
// . lists[] and termFreqs[] must be 1-1 with q->m_qterms[]
void PosdbTable::init(Query *q, bool debug, TopTree *topTree, const DocumentIndexChecker &documentIndexChecker, Msg2 *msg2, Msg39Request *r) {
	// sanity check -- watch out for double calls
	if ( m_initialized )
		gbshutdownAbort(true);
	// clear everything
	reset();
	// we are now
	m_initialized = true;
	// set debug flag
	m_debug = (debug || g_conf.m_logDebugQuery);
	// we should save the lists!
	//m_lists    = msg2->m_lists;//lists;
	//m_numLists = q->m_numTerms;

	// seo.cpp supplies a NULL msg2 because it already sets
	// QueryTerm::m_posdbListPtrs
	if ( ! msg2 ) return;

	m_msg2 = msg2;
	// save the request
	m_msg39req = r;

	m_baseScoringParameters = r->m_baseScoringParameters;
	m_derivedScoringWeights.init(m_baseScoringParameters);
	if(g_conf.m_logTracePosdb || m_msg39req->m_debug)
		m_baseScoringParameters.traceToLog("posdbtable");
	
	// save this
	//m_coll = coll;
	// get the rec for it
//	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
//	if ( ! cr )
//		gbshutdownAbort(true);
	// set this now
	//m_collnum = cr->m_collnum;

	m_documentIndexChecker = &documentIndexChecker;
	m_topTree = topTree;

	// remember the query class, it has all the info about the termIds
	m_q = q;
	m_nqt = q->getNumTerms();

	m_realMaxTop = r->m_realMaxTop;
	if ( m_realMaxTop > MAX_TOP ) m_realMaxTop = MAX_TOP;

	if ( m_q->m_isBoolean ) m_baseScoringParameters.m_siteRankMultiplier = 0.0;

	// sanity
	if ( msg2->getNumLists() != m_q->getNumTerms() )
		gbshutdownAbort(true);
	// copy the list ptrs to the QueryTerm::m_posdbListPtr
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) 
		m_q->m_qterms[i].m_posdbListPtr = msg2->getList(i);
	// we always use it now
	if ( ! topTree )
		gbshutdownAbort(true);
}



namespace {
//getMaxScoreForNonBodyTermPair(), getScoreForTermPair() and getTermPairScoreForAny() need to keep several items for each of the
//two lists and set them based on the posdb pointer(s). This helper struct handles the common things
struct PosdbDecodeHelper {
	int32_t p;
	unsigned char hg;
	unsigned char mhg;
	unsigned char wsr;
	float spamw;
	float denw;
	//float diversityWeight; //todo?
	//unsigned char syn; //not supported anymore
	void set(const char *wp, const DerivedScoringWeights &derivedScoringWeights) {
		p = Posdb::getWordPos(wp);
		hg = Posdb::getHashGroup(wp);
		//temporary fix: posdb can have junk in it so clamp the hashgroup to the limit
		if(hg>=HASHGROUP_END)
			hg = HASHGROUP_END-1;
		mhg = hg;
		// reduce to either HASHGROUP_BODY/TITLE/INLINK/META
		if(s_inBody[mhg])
			mhg = HASHGROUP_BODY;
		wsr = Posdb::getWordSpamRank(wp);
		if(hg == HASHGROUP_INLINKTEXT)
			spamw = derivedScoringWeights.m_linkerWeights[wsr];
		else
			spamw = derivedScoringWeights.m_wordSpamWeights[wsr];
		denw = derivedScoringWeights.m_densityWeights[Posdb::getDensityRank(wp)];
		//syn = Posdb::getIsSynonym(wp);
	}
};
} //anonymous namespace



//
// Find the top score for the term for each hash group.
// INLINK_TEXT may have more than one entry in the top-scorer list, other hash groups only 1.
// INLINK_TEXT may override other lower scoring hashgroups' scores
// Sum up the best scores, and return that result. 
// Sets highestScoringNonBodyPos to the highest scoring position.
//
float PosdbTable::getBestScoreSumForSingleTerm(const MiniMergeBuffer *miniMergeBuffer, int32_t i, DocIdScore *pdcs, const char **highestScoringNonBodyPos) {
	const char *wpi = miniMergeBuffer->mergedListStart[i];
	const char *endi = miniMergeBuffer->mergedListEnd[i];
	float nonBodyMax = -1.0;
	int32_t lowestScoreTermIdx = 0;
	float bestScores[MAX_TOP] = {0};	// make Coverity happy
	const char *bestwpi[MAX_TOP];
	int32_t numTop = 0;

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// assume no terms!
	*highestScoringNonBodyPos = NULL;

	if ( wpi ) {
		// Sanity check
		if( wpi >= endi ) {
			logTrace(g_conf.m_logTracePosdb, "END, wpi %p >= %p", wpi, endi);
			return -1.0;
		}
#ifdef _VALGRIND_
		VALGRIND_CHECK_MEM_IS_DEFINED(wpi,endi-wpi);
#endif
		//stuff for detecting when a position has bigram matches on both sides
		const bool leftTermIsIgnored = m_queryTermInfos[i].m_leftTermIsIgnored;
		int prevBigramMatchPos = -1;
		int prevBigramMatchQtermIdx = -1;
		float prevBigramMatchTermWeight = 1.0;
		
		bool first = true;
		char bestmhg[MAX_TOP];
		do {
			float score = 100.0;
			PosdbDecodeHelper helper;
			helper.set(wpi, m_derivedScoringWeights);
			// good diversity?
			//unsigned char div = Posdb::getDiversityRank ( wpi );
			//score *= m_derivedScoringWeights.m_diversityWeights[div];
			//score *= m_derivedScoringWeights.m_diversityWeights[div];
			// hash group? title? body? heading? etc.
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper.mhg];
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper.mhg];
			// good density?
			score *= helper.denw;
			score *= helper.denw;
			// to make more compatible with pair scores divide by distance of 2
			//score /= 2.0;

			// word spam?
			score *= helper.spamw;
			score *= helper.spamw;

			int queryTermIndex = miniMergeBuffer->getTermIndexForBufferPos(wpi);
			const QueryTerm &qterm = m_q->m_qterms[queryTermIndex];

			score *= qterm.m_userWeight;

			score *= m_q->m_qterms[queryTermIndex].m_termFreqWeight;
			score *= m_q->m_qterms[queryTermIndex].m_termFreqWeight;

			score *= qterm.m_termWeight; //regular/bigram/synonym
			score *= qterm.m_termWeight; //regular/bigram/synonym

			if(leftTermIsIgnored && m_q->m_qterms[queryTermIndex].m_isPhrase) {
				//if the term to the left of us is ignored (stopword/qstopword/highfreqterm/...) then that bigram boost may be lost. Worst case:
				//        <ignored> term <ignored>
				// ==>
				//        <bigram-hit-left> <term-hit> <bigram-hit-2>
				//and then the boost from the left bigram hit would be lost. So detect this case and give it the extra boost:
				if(prevBigramMatchPos+2>=helper.p &&
				   prevBigramMatchQtermIdx!=queryTermIndex) {
					score *= prevBigramMatchTermWeight;
					score *= prevBigramMatchTermWeight;
				}
				//record where we got a bigram hit
				prevBigramMatchPos = helper.p;
				prevBigramMatchQtermIdx = queryTermIndex;
				prevBigramMatchTermWeight = qterm.m_termWeight;
			}
				
			// do not allow duplicate hashgroups!
			int32_t bro = -1;
			for( int32_t k=0; k < numTop; k++) {
				if( bestmhg[k] == helper.mhg && helper.hg != HASHGROUP_INLINKTEXT) {
					bro = k;
					break;
				}
			}

			if ( bro >= 0 ) {
				//
				// We already have a score for this hash group, update if
				// current score is higher
				//
				if ( score > bestScores[bro] ) {
					bestScores[bro] = score;
					bestwpi   [bro] = wpi;
					bestmhg   [bro] = helper.mhg;
				}
			}
			else 
			if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
				//
				// New hash group (or INLINKTEXT).
				// We have free slots in the top-list.
				// Store found score.
				//
				bestScores[numTop] = score;
				bestwpi   [numTop] = wpi;
				bestmhg   [numTop] = helper.mhg;
				numTop++;
			}
			else 
			if ( score > bestScores[lowestScoreTermIdx] ) {
				//
				// New hash group (or INLINKTEXT).
				// We have NO free slots in the top-list.
				// Replace lowest score in top-list with current higher score.
				//
				bestScores[lowestScoreTermIdx] = score;
				bestwpi   [lowestScoreTermIdx] = wpi;
				bestmhg   [lowestScoreTermIdx] =helper. mhg;
			}

			// If top-list is full, make lowestScoreTermIdx point to the lowest score
			// in the top-list.
			if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
				lowestScoreTermIdx = 0;
				for ( int32_t k = 1 ; k < m_realMaxTop; k++ ) {//MAX_TOP ; k++ ) {
					if ( bestScores[k] > bestScores[lowestScoreTermIdx] ) {
						continue;
					}
					lowestScoreTermIdx = k;
				}
			}

			// For findMinTermPairScoreInWindow() sub-out algo.
			// If the term is not in the body, and the score is the
			// highest non-body term score, return the position index.
			if ( score > nonBodyMax && ! s_inBody[helper.hg] ) {
				nonBodyMax = score;
				*highestScoringNonBodyPos = wpi;
			}

			// advance posdb pointer
			if(!first)
				wpi += 6;
			else {
				wpi += 12;
				first = false;
			}

		} while( wpi < endi );
	}

	// add up the top scores
	float sum = 0.0;
	for ( int32_t k = 0 ; k < numTop ; k++ ) {
		// if it is something like "enough for" in a wikipedia
		// phrase like "time enough for love" give it a boost!
		// now we set a special bit in the keys since we do a mini 
		// merge, we do the same thing for the syn bits
		if ( Posdb::getIsHalfStopWikiBigram(bestwpi[k]) ) {
			sum += (bestScores[k] * WIKI_BIGRAM_WEIGHT * WIKI_BIGRAM_WEIGHT);
		}
		else {
			// otherwise just add it up
			sum += bestScores[k];
		}
	}

	// wiki weight
	//sum *= ts;

	// shortcut
	//char *maxp = bestwpi[k];

	// if terms is a special wiki half stop bigram
	//if ( m_bflags[i] & BF_HALFSTOPWIKIBIGRAM ) {
	//	sum *= WIKI_BIGRAM_WEIGHT;
	//	sum *= WIKI_BIGRAM_WEIGHT;
	//}

	// empty list?
	//if ( ! wpi ) sum = -2.0;

	//
	// end the loop. return now if not collecting scoring info.
	//
	if ( ! pdcs ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
	}


	//#
	//# The below is for visual presentation of the scoring ONLY
	//#
		
	// none? wtf?
	if ( numTop <= 0 ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
	}
	
	// point into buf
	SingleScore *sx = (SingleScore *)m_singleScoreBuf.getBufPtr();
	int32_t need = sizeof(SingleScore) * numTop;

	// point to that
	if ( pdcs->m_singlesOffset < 0 ) {
		pdcs->m_singlesOffset = m_singleScoreBuf.length();
	}

	// reset this i guess
	pdcs->m_singleScores = NULL;

	// sanity
	if ( m_singleScoreBuf.getAvail() < need ) { 
		static bool s_first = true;
		if ( s_first ) {
			log("posdb: CRITICAL single buf overflow");
		}
		s_first = false;
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
		//gbshutdownAbort(true); }
	}

	// increase buf ptr over this then
	m_singleScoreBuf.incrementLength(need);

	// set each of the top scoring terms individiually
	for ( int32_t k = 0 ; k < numTop ; k++, sx++ ) {
		// udpate count
		pdcs->m_numSingles++;
		const char *maxp = bestwpi[k];
		memset(sx,0,sizeof(*sx));
		sx->m_isSynonym = Posdb::getIsSynonym(maxp);
		sx->m_isHalfStopWikiBigram = Posdb::getIsHalfStopWikiBigram(maxp);
		//sx->m_isSynonym = (m_bflags[i] & BF_SYNONYM) ;
		sx->m_diversityRank  = Posdb::getDiversityRank(maxp);
		sx->m_wordSpamRank   = Posdb::getWordSpamRank(maxp);
		sx->m_hashGroup      = Posdb::getHashGroup(maxp);
		sx->m_wordPos        = Posdb::getWordPos(maxp);
		sx->m_densityRank = Posdb::getDensityRank(maxp);

		float score = bestScores[k];

		int queryTermIndex = miniMergeBuffer->getTermIndexForBufferPos(maxp);
		//TODO: shouldn't we multiply with userweight here too?
		//float userWeight = m_q->m_qterms[queryTermIndex].m_userWeight;
		//score *= userWeight;

		score *= m_q->m_qterms[queryTermIndex].m_termFreqWeight;
		score *= m_q->m_qterms[queryTermIndex].m_termFreqWeight;

		// if terms is a special wiki half stop bigram
		if ( sx->m_isHalfStopWikiBigram ) {
			score *= WIKI_BIGRAM_WEIGHT;
			score *= WIKI_BIGRAM_WEIGHT;
		}
		sx->m_finalScore = score;
		sx->m_tfWeight = m_q->m_qterms[queryTermIndex].m_termFreqWeight;
		sx->m_qtermNum = m_qtermNums[i];
		//int64_t *termFreqs = (int64_t *)m_msg39req->ptr_termFreqs;
		//sx->m_termFreq = termFreqs[sx->m_qtermNum];
		sx->m_bflags   = m_bflags[i];
	}

	logTrace(g_conf.m_logTracePosdb, "END. sum=%f", sum);
	return sum;
}



// . advace two ptrs at the same time so it's just a linear scan
// . TODO: add all up, then basically taking a weight of the top 6 or so...
float PosdbTable::getMaxScoreForNonBodyTermPair(const MiniMergeBuffer *miniMergeBuffer, int i, int j, int32_t qdist) {
	const char *wpi = miniMergeBuffer->mergedListStart[i];
	const char *wpj = miniMergeBuffer->mergedListStart[j];
	const char *endi = miniMergeBuffer->mergedListEnd[i];
	const char *endj = miniMergeBuffer->mergedListEnd[j];

	// Sanity check
	if( wpi >= endi || wpj >= endj ) {
		return -1.0;
	}

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(wpi,endi-wpi);
	VALGRIND_CHECK_MEM_IS_DEFINED(wpj,endj-wpj);
#endif
	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	PosdbDecodeHelper helper1, helper2;
	helper1.set(wpi, m_derivedScoringWeights);
	helper2.set(wpj, m_derivedScoringWeights);

	bool firsti = true;
	bool firstj = true;

	float max = -1.0;

	for(;;) {

		if ( helper1.p <= helper2.p ) {
			// . skip the pair if they are in different hashgroups
			// . we no longer allow either to be in the body in this
			//   algo because we handle those cases in the sliding window
			//   algo!
			if( s_isCompatible[helper1.hg][helper2.hg] ) {
				// git distance
				int32_t dist = helper2.p - helper1.p;

				// if zero, make sure its 2. this happens when the same bigram
				// is used by both terms. i.e. street uses the bigram 
				// 'street light' and so does 'light'. so the wordpositions
				// are exactly the same!
				if ( dist < 2 ) {
					dist = 2;
				}
				
				// fix distance if in different non-body hashgroups
				if ( dist > 50 ) {
					dist = FIXED_DISTANCE;
				}

				// subtract from the dist the terms are apart in the query
				if ( dist >= qdist ) {
					dist =  dist - qdist;
				}
				
				// good density?
				float score = 100 * helper1.denw * helper2.denw;

				// hashgroup modifier
				score *= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
				score *= m_derivedScoringWeights.m_hashGroupWeights[helper2.hg];

				const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
				const QueryTerm &qterm1 = m_q->m_qterms[queryTermIndex1];
				const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
				const QueryTerm &qterm2 = m_q->m_qterms[queryTermIndex2];

				score *= qterm1.m_userWeight;
				score *= qterm2.m_userWeight;

				score *= qterm1.m_termWeight; //regular/bigram/synonym
				score *= qterm2.m_termWeight; //regular/bigram/synonym

				// word spam weights
				score *= helper1.spamw * helper2.spamw;
				// huge title? do not allow 11th+ word to be weighted high
				//if ( helper1.hg == HASHGROUP_TITLE && dist > 20 )
				//	score /= m_baseScoringParameters.m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
				// mod by distance
				score /= (dist + 1.0);
				// best?
				if ( score > max ) {
					max = score;
				}
			}		

			// advance posdb pointer
			if(!firsti)
				wpi += 6;
			else {
				wpi += 12;
				firsti = false;
			}
			
			// end of list?
			if ( wpi >= endi ) {
				break;	// exit for(;;) loop
			}
			
			helper1.set(wpi, m_derivedScoringWeights);
		}
		else {
			// . skip the pair if they are in different hashgroups
			// . we no longer allow either to be in the body in this
			//   algo because we handle those cases in the sliding window
			//   algo!
			if ( s_isCompatible[helper1.hg][helper2.hg] ) {
				// get distance
				int32_t dist = helper1.p - helper2.p;
				// if zero, make sure its 2. this happens when the same bigram
				// is used by both terms. i.e. street uses the bigram 
				// 'street light' and so does 'light'. so the wordpositions
				// are exactly the same!
				if ( dist < 2 ) dist = 2;
				// fix distance if in different non-body hashgroups
				if ( dist > 50 ) {
					dist = FIXED_DISTANCE;
				}

				// subtract from the dist the terms are apart in the query
				if ( dist >= qdist ) {
					dist =  dist - qdist;
					// add 1 for being out of order
					dist += qdist - 1;
				}
				else {
					//dist =  dist - qdist;
					// add 1 for being out of order
					dist += 1; // qdist - 1;
				}

				// compute score based on that junk
				//score = (MAXWORDPOS+1) - dist;
				// good diversity? uneeded for pair algo
				//score *= m_derivedScoringWeights.m_diversityWeights[div1];
				//score *= m_derivedScoringWeights.m_diversityWeights[div2];
				// good density?
				float score = 100 * helper1.denw * helper2.denw;
				// hashgroup modifier
				score *= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
				score *= m_derivedScoringWeights.m_hashGroupWeights[helper2.hg];

				const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
				const QueryTerm &qterm1 = m_q->m_qterms[queryTermIndex1];
				const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
				const QueryTerm &qterm2 = m_q->m_qterms[queryTermIndex2];
				
				score *= qterm1.m_userWeight;
				score *= qterm2.m_userWeight;

				score *= qterm1.m_termWeight; //regular/bigram/synonym
				score *= qterm2.m_termWeight; //regular/bigram/synonym

				// word spam weights
				score *= helper1.spamw * helper2.spamw;
				// huge title? do not allow 11th+ word to be weighted high
				//if ( helper1.hg == HASHGROUP_TITLE && dist > 20 )
				//	score /= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
				// mod by distance
				score /= (dist + 1.0);
				// best?
				if ( score > max ) {
					max = score;
				}
			}

			// advance posdb pointer
			if(!firstj)
				wpj += 6;
			else {
				wpj += 12;
				firstj = false;
			}

			// end of list?
			if ( wpj >= endj ) {
				break;	// exit for(;;) loop
			}
			
			helper2.set(wpj, m_derivedScoringWeights);
		}
	}

	logTrace(g_conf.m_logTracePosdb, "END.");
	return max;
}



float PosdbTable::getScoreForTermPair(const MiniMergeBuffer *miniMergeBuffer, const char *wpi, const char *wpj, int32_t fixedDistance, int32_t qdist) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	if ( ! wpi ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return -1.00;
	}
	if ( ! wpj ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return -1.00;
	}

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(wpi,6);
	VALGRIND_CHECK_MEM_IS_DEFINED(wpj,6);
#endif
	PosdbDecodeHelper helper1, helper2;
	helper1.set(wpi, m_derivedScoringWeights);
	helper2.set(wpj, m_derivedScoringWeights);

	float dist;
	// set this
	if ( fixedDistance != 0 ) {
		dist = fixedDistance;
	}
	else {
		dist = abs(helper1.p - helper2.p);
		// if zero, make sure its 2. this happens when the same bigram
		// is used by both terms. i.e. street uses the bigram 
		// 'street light' and so does 'light'. so the wordpositions
		// are exactly the same!
		if ( dist < 2 ) dist = 2;
		// subtract from the dist the terms are apart in the query
		if ( dist >= qdist ) dist =  dist - qdist;
		// out of order? penalize by 1 unit
		if ( helper2.p < helper1.p ) dist += 1;
	}
	// TODO: use left and right diversity if no matching query term
	// is on the left or right
	//score *= m_derivedScoringWeights.m_diversityWeights[div1];
	//score *= m_derivedScoringWeights.m_diversityWeights[div2];
	// good density?
	float score = 100 * helper1.denw * helper2.denw;
	// wikipedia phrase weight
	//score *= ts;
	// hashgroup modifier
	score *= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
	score *= m_derivedScoringWeights.m_hashGroupWeights[helper2.hg];

	const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
	const QueryTerm &qterm1 = m_q->m_qterms[queryTermIndex1];
	const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
	const QueryTerm &qterm2 = m_q->m_qterms[queryTermIndex2];

	score *= qterm1.m_userWeight;
	score *= qterm2.m_userWeight;
	
	score *= qterm1.m_termWeight; //regular/bigram/synonym
	score *= qterm2.m_termWeight; //regular/bigram/synonym

	// word spam weights
	score *= helper1.spamw * helper2.spamw;
	// mod by distance
	score /= (dist + 1.0);
	
	logTrace(g_conf.m_logTracePosdb, "END. score=%f", score);
	return score;
}



// . advance two ptrs at the same time so it's just a linear scan
// . TODO: add all up, then basically taking a weight of the top 6 or so...
// . skip body terms not in the sliding window as defined by bestMinTermPairWindowPtrs[]
float PosdbTable::getTermPairScoreForAny(const MiniMergeBuffer *miniMergeBuffer, int i, int j, const std::vector<const char *> &bestMinTermPairWindowPtrs, DocIdScore *pdcs) {
	const char *wpi = miniMergeBuffer->mergedListStart[i];
	const char *wpj = miniMergeBuffer->mergedListStart[j];
	const char *endi = miniMergeBuffer->mergedListEnd[i];
	const char *endj = miniMergeBuffer->mergedListEnd[j];

	// wiki phrase weight?
	float wikiPhraseWeight;

	logTrace(g_conf.m_logTracePosdb, "BEGIN. i=%d, j=%d", i, j);

	int32_t qdist;

	// but if they are in the same wikipedia phrase
	// then try to keep their positions as in the query.
	// so for 'time enough for love' ideally we want
	// 'time' to be 6 units apart from 'love'
	if ( m_wikiPhraseIds[j] == m_wikiPhraseIds[i] && m_wikiPhraseIds[j] ) { // zero means not in a phrase
		qdist = m_qpos[j] - m_qpos[i];
		// wiki weight
		wikiPhraseWeight = (float)WIKI_WEIGHT;
	}
	else {
		// basically try to get query words as close
		// together as possible
		qdist = 2;
		// this should help fix
		// 'what is an unsecured loan' so we are more likely
		// to get the page that has that exact phrase in it.
		// yes, but hurts how to make a lock pick set.
		//qdist = qpos[j] - qpos[i];
		// wiki weight
		wikiPhraseWeight = 1.0;
	}

	bool inSameQuotedPhrase = false;
	if ( m_quotedStartIds[i] == m_quotedStartIds[j] && m_quotedStartIds[i] >= 0 ) {
		inSameQuotedPhrase = true;
	}

	// oops.. this was not counting non-space punct for 2 units 
	// instead of 1
	if ( inSameQuotedPhrase ) {
		qdist = m_qpos[j] - m_qpos[i];		
	}

	PosdbDecodeHelper helper1, helper2;
	helper1.set(wpi, m_derivedScoringWeights);
	helper2.set(wpj, m_derivedScoringWeights);

	bool firsti = true;
	bool firstj = true;

	int32_t  lowestScoreTermIdx = -1;
	float bestScores[MAX_TOP] = {0};	 // make Coverity happy
	const char *bestwpi   [MAX_TOP];
	const char *bestwpj   [MAX_TOP];
	char  bestmhg1  [MAX_TOP];
	char  bestmhg2  [MAX_TOP];
	char  bestFixed [MAX_TOP];
	int32_t  numTop = 0;
	bool  fixedDistance;
	int32_t  bro;

	for(;;) {
		// . if p1/p2 is in body and not in window, skip
		// . this is how we restrict all body terms to the winning
		//   sliding window
		if ( s_inBody[helper1.hg] && wpi != bestMinTermPairWindowPtrs[i] ) {
			goto skip1;
		}
		
		if ( s_inBody[helper2.hg] && wpj != bestMinTermPairWindowPtrs[j] ) {
			goto skip2;
		}

		// make this strictly < now and not <= because in the event
		// of bigram terms, where p1==p2 we'd like to advance p2/wj to
		// point to the non-syn single term in order to get a better score
		// to fix the 'search engine' query on gigablast.com
		if ( helper1.p <= helper2.p ) {
			// git distance
			int32_t dist;
			dist = helper2.p - helper1.p;

			// if in the same quoted phrase, order is bad!
			if ( inSameQuotedPhrase ) {
				// debug
				//log("dddx: i=%" PRId32" j=%" PRId32" dist=%" PRId32" qdist=%" PRId32" posi=%" PRId32" "
				//    "posj=%" PRId32,
				//    i,j,dist,qdist,helper1.p,helper2.p);
				// TODO: allow for off by 1
				// if it has punct in it then dist will be 3, 
				// just a space or similar then dist should be 2.
				if ( dist > qdist && dist - qdist >= 2 ) {
					goto skip1;
				}
				
				if ( dist < qdist && qdist - dist >= 2 ) {
					goto skip1;
				}
			}

			// if zero, make sure its 2. this happens when the same bigram
			// is used by both terms. i.e. street uses the bigram 
			// 'street light' and so does 'light'. so the wordpositions
			// are exactly the same!
			if ( dist < 2 ) {
				dist = 2;
			}
			// fix distance if in different non-body hashgroups
			if ( dist < 50 ) {
				fixedDistance = false;
			}
			// body vs title, linktext vs title, linktext vs body
			else if ( helper1.mhg != helper2.mhg ) {
				dist = FIXED_DISTANCE;
				fixedDistance = true;
			}
			// link text to other link text
			else if ( helper1.mhg == HASHGROUP_INLINKTEXT ) {
				dist = FIXED_DISTANCE;
				fixedDistance = true;
			}
			else {
				fixedDistance = false;
			}
			
			// if both are link text and > 50 units apart that means
			// they are from different link texts
			//if ( helper1.hg == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip1;
			// subtract from the dist the terms are apart in the query
			if ( dist >= qdist ) {
				dist =  dist - qdist;
			}
			
			// good density?
			float score;
			score = 100 * helper1.denw * helper2.denw;
			// hashgroup modifier
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper2.hg];

			{
				const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
				const QueryTerm &qterm1 = m_q->m_qterms[queryTermIndex1];
				const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
				const QueryTerm &qterm2 = m_q->m_qterms[queryTermIndex2];
				
				score *= qterm1.m_termFreqWeight;
				score *= qterm2.m_termFreqWeight;
				
				score *= qterm1.m_userWeight;
				score *= qterm2.m_userWeight;
				
				score *= qterm1.m_termWeight; //regular/bigram/synonym
				score *= qterm2.m_termWeight; //regular/bigram/synonym
			}

			// the new logic
			if ( Posdb::getIsHalfStopWikiBigram(wpi) ) {
				score *= WIKI_BIGRAM_WEIGHT;
			}
			
			if ( Posdb::getIsHalfStopWikiBigram(wpj) ) {
				score *= WIKI_BIGRAM_WEIGHT;
			}
			
			// word spam weights
			score *= helper1.spamw * helper2.spamw;
			// huge title? do not allow 11th+ word to be weighted high
			//if ( helper1.hg == HASHGROUP_TITLE && dist > 20 )
			//	score /= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
			// mod by distance
			score /= (dist + 1.0);

			// if our hg1/hg2 hashgroup pairing already exists
			// in the bestScores array we have to beat it and then
			// we have to replace that. we can only have one per,
			// except for linktext!

			bro = -1;
			for ( int32_t k = 0 ; k < numTop ; k++ ) {
				if ( bestmhg1[k]==helper1.mhg && helper1.hg != HASHGROUP_INLINKTEXT ){
					bro = k;
					break;
				}
				if ( bestmhg2[k]==helper2.mhg && helper2.hg != HASHGROUP_INLINKTEXT ){
					bro = k;
					break;
				}
			}

			if ( bro >= 0 ) {
				if ( score > bestScores[bro] ) {
					bestScores[bro] = score;
					bestwpi   [bro] = wpi;
					bestwpj   [bro] = wpj;
					bestmhg1  [bro] = helper1.mhg;
					bestmhg2  [bro] = helper2.mhg;
					bestFixed [bro] = fixedDistance;
				}
			}
			else 
			if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
				bestScores[numTop] = score;
				bestwpi   [numTop] = wpi;
				bestwpj   [numTop] = wpj;
				bestmhg1  [numTop] = helper1.mhg;
				bestmhg2  [numTop] = helper2.mhg;
				bestFixed [numTop] = fixedDistance;
				numTop++;
			}
			else 
			if ( lowestScoreTermIdx >= 0 && score > bestScores[lowestScoreTermIdx] ) {
				bestScores[lowestScoreTermIdx] = score;
				bestwpi   [lowestScoreTermIdx] = wpi;
				bestwpj   [lowestScoreTermIdx] = wpj;
				bestmhg1  [lowestScoreTermIdx] = helper1.mhg;
				bestmhg2  [lowestScoreTermIdx] = helper2.mhg;
				bestFixed [lowestScoreTermIdx] = fixedDistance;
			}
			
			// set "lowestScoreTermIdx" to the lowest score out of the top scores
			if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
				lowestScoreTermIdx = 0;
				for ( int32_t k = 1 ; k < m_realMaxTop;k++){//MAX_TOP;k++
					if (bestScores[k] > bestScores[lowestScoreTermIdx] ) {
						continue;
					}
					
					lowestScoreTermIdx = k;
				}
			}

			
		skip1:
			// advance posdb pointer
			if(!firsti)
				wpi += 6;
			else {
				wpi += 12;
				firsti = false;
			}

			// end of list?
			if ( wpi >= endi ) {
				break;	// exit for(;;) loop
			}
			
			helper1.set(wpi, m_derivedScoringWeights);
		}
		else {
			// get distance
			int32_t dist;
			dist = helper1.p - helper2.p;

			// if in the same quoted phrase, order is bad!
			if ( inSameQuotedPhrase ) {
				// debug
				//log("dddy: i=%" PRId32" j=%" PRId32" dist=%" PRId32" qdist=%" PRId32" posi=%" PRId32" "
				//    "posj=%" PRId32,
				//    i,j,dist,qdist,helper1.p,p2);
				goto skip2;
			}

			// if zero, make sure its 2. this happens when the same bigram
			// is used by both terms. i.e. street uses the bigram 
			// 'street light' and so does 'light'. so the wordpositions
			// are exactly the same!
			if ( dist < 2 ) {
				dist = 2;
			}
			
			// fix distance if in different non-body hashgroups
			if ( dist < 50 ) {
				fixedDistance = false;
			}
			// body vs title, linktext vs title, linktext vs body
			else if ( helper1.mhg != helper2.mhg ) {
				dist = FIXED_DISTANCE;
				fixedDistance = true;
			}
			// link text to other link text
			else if ( helper1.mhg == HASHGROUP_INLINKTEXT ) {
				dist = FIXED_DISTANCE;
				fixedDistance = true;
			}
			else
				fixedDistance = false;
			// if both are link text and > 50 units apart that means
			// they are from different link texts
			//if ( helper1.hg == HASHGROUP_INLINKTEXT && dist > 50 ) goto skip2;
			// subtract from the dist the terms are apart in the query
			if ( dist >= qdist ) {
				dist =  dist - qdist;
				// add 1 for being out of order
				dist += qdist - 1;
			}
			else {
				//dist =  dist - qdist;
				// add 1 for being out of order
				dist += 1; // qdist - 1;
			}

			// good diversity? uneeded for pair algo
			// good density?
			float score;
			score = 100 * helper1.denw * helper2.denw;
			// hashgroup modifier
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper1.hg];
			score *= m_derivedScoringWeights.m_hashGroupWeights[helper2.hg];
			
			{
				const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
				const QueryTerm &qterm1 = m_q->m_qterms[queryTermIndex1];
				const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
				const QueryTerm &qterm2 = m_q->m_qterms[queryTermIndex2];
				
				score *= qterm1.m_termFreqWeight;
				score *= qterm2.m_termFreqWeight;
				
				score *= qterm1.m_userWeight;
				score *= qterm2.m_userWeight;
				
				score *= qterm1.m_termWeight; //regular/bigram/synonym
				score *= qterm2.m_termWeight; //regular/bigram/synonym
			}
			
			// word spam weights
			score *= helper1.spamw * helper2.spamw;
			// huge title? do not allow 11th+ word to be weighted high
			//if ( hg1 == HASHGROUP_TITLE && dist > 20 ) 
			//	score /= m_derivedScoringWeights.m_hashGroupWeights[hg1];
			// mod by distance
			score /= (dist + 1.0);

			// if our hg1/hg2 hashgroup pairing already exists
			// in the bestScores array we have to beat it and then
			// we have to replace that. we can only have one per,
			// except for linktext!

			bro = -1;
			for ( int32_t k = 0 ; k < numTop ; k++ ) {
				if ( bestmhg1[k]==helper1.mhg && helper1.hg !=HASHGROUP_INLINKTEXT ){
					bro = k;
					break;
				}
				if ( bestmhg2[k]==helper2.mhg && helper2.hg !=HASHGROUP_INLINKTEXT ){
					bro = k;
					break;
				}
			}
			if ( bro >= 0 ) {
				if ( score > bestScores[bro] ) {
					bestScores[bro] = score;
					bestwpi   [bro] = wpi;
					bestwpj   [bro] = wpj;
					bestmhg1  [bro] = helper1.mhg;
					bestmhg2  [bro] = helper2.mhg;
					bestFixed [bro] = fixedDistance;
				}
			}
			// best?
			else if ( numTop < m_realMaxTop ) { // MAX_TOP ) {
				bestScores[numTop] = score;
				bestwpi   [numTop] = wpi;
				bestwpj   [numTop] = wpj;
				bestmhg1  [numTop] = helper1.mhg;
				bestmhg2  [numTop] = helper2.mhg;
				bestFixed [numTop] = fixedDistance;
				numTop++;
			}
			else if ( score > bestScores[lowestScoreTermIdx] ) {
				bestScores[lowestScoreTermIdx] = score;
				bestwpi   [lowestScoreTermIdx] = wpi;
				bestwpj   [lowestScoreTermIdx] = wpj;
				bestmhg1  [lowestScoreTermIdx] = helper1.mhg;
				bestmhg2  [lowestScoreTermIdx] = helper2.mhg;
				bestFixed [lowestScoreTermIdx] = fixedDistance;
			}
			
			// set "lowestScoreTermIdx" to the lowest score out of the top scores
			if ( numTop >= m_realMaxTop ) { // MAX_TOP ) {
				lowestScoreTermIdx = 0;
				for ( int32_t k = 1 ; k < m_realMaxTop;k++){//MAX_TOP;k++
					if( bestScores[k] > bestScores[lowestScoreTermIdx]) {
						continue;
					}
					lowestScoreTermIdx = k;
				}
			}

		skip2:
			// advance posdb pointer
			if(!firstj)
				wpj += 6;
			else {
				wpj += 12;
				firstj = false;
			}

			// end of list?
			if ( wpj >= endj ) {
				break;	// exit for(;;) loop
			}
			
			helper2.set(wpj, m_derivedScoringWeights);
		}
	} // for(;;)

	if(g_conf.m_logTracePosdb) {
		logTrace(g_conf.m_logTracePosdb, "numTop=%d", numTop);
		for(int i=0; i<numTop; i++)
			logTrace(g_conf.m_logTracePosdb, "  bestScores[%d]=%f", i, bestScores[i]);
	}
		

	// add up the top scores
	float sum = 0.0;
	for ( int32_t k = 0 ; k < numTop ; k++ ) {
		sum += bestScores[k];
	}

	if (m_debug) {
		for ( int32_t k = 0 ; k < numTop ; k++ )
			log(LOG_INFO, "posdb: best score #%" PRId32" = %f",k,bestScores[k]);
		log(LOG_INFO, "posdb: best score sum = %f",sum);
	}

	// wiki phrase weight
	sum *= wikiPhraseWeight;

	if (m_debug) {
		log(LOG_INFO, "posdb: best score final = %f",sum);
	}

	//
	// end the loop. return now if not collecting scoring info.
	//
	if ( ! pdcs ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
	}
	
	// none? wtf?
	if ( numTop <= 0 ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
	}

	//
	// now store the PairScores into the m_pairScoreBuf for this 
	// top docid.
	//

	// point into buf
	PairScore *px = (PairScore *)m_pairScoreBuf.getBufPtr();
	int32_t need = sizeof(PairScore) * numTop;

	// point to that
	if ( pdcs->m_pairsOffset < 0 ) {
		pdcs->m_pairsOffset = m_pairScoreBuf.length();
	}

	// reset this i guess
	pdcs->m_pairScores = NULL;

	// sanity
	if ( m_pairScoreBuf.getAvail() < need ) { 
		// m_pairScores will be NULL
		static bool s_first = true;
		if ( s_first ) {
			log("posdb: CRITICAL pair buf overflow");
		}
		
		s_first = false;
		logTrace(g_conf.m_logTracePosdb, "END.");
		return sum;
	}
	// increase buf ptr over this then
	m_pairScoreBuf.incrementLength(need);

	// set each of the top scoring terms individiually
	for ( int32_t k = 0 ; k < numTop ; k++, px++ ) {
		pdcs->m_numPairs++;
		memset(px,0,sizeof(*px));
		const char *maxp1 = bestwpi[k];
		const char *maxp2 = bestwpj[k];
		float score = bestScores[k];
		bool fixedDist = bestFixed[k];
		score *= wikiPhraseWeight;
	
		const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(maxp1);
		const float termFreqWeight1 = m_q->m_qterms[queryTermIndex1].m_termFreqWeight;
		const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(maxp2);
		const float termFreqWeight2 = m_q->m_qterms[queryTermIndex2].m_termFreqWeight;

		// we have to encode these bits into the mini merge now
		if ( Posdb::getIsHalfStopWikiBigram(maxp1) ) {
			score *= WIKI_BIGRAM_WEIGHT;
		}
		
		if ( Posdb::getIsHalfStopWikiBigram(maxp2) ) {
			score *= WIKI_BIGRAM_WEIGHT;
		}
		
		//if ( m_bflags[i] & BF_HALFSTOPWIKIBIGRAM ) 
		//if ( m_bflags[j] & BF_HALFSTOPWIKIBIGRAM ) 
		// wiki phrase weight
		px->m_finalScore     = score;
		px->m_wordPos1       = Posdb::getWordPos(maxp1);
		px->m_wordPos2       = Posdb::getWordPos(maxp2);
		px->m_isSynonym1     = Posdb::getIsSynonym(maxp1);
		px->m_isSynonym2     = Posdb::getIsSynonym(maxp2);
		px->m_isHalfStopWikiBigram1 = Posdb::getIsHalfStopWikiBigram(maxp1);
		px->m_isHalfStopWikiBigram2 = Posdb::getIsHalfStopWikiBigram(maxp2);
		px->m_diversityRank1 = Posdb::getDiversityRank(maxp1);
		px->m_diversityRank2 = Posdb::getDiversityRank(maxp2);
		px->m_wordSpamRank1  = Posdb::getWordSpamRank(maxp1);
		px->m_wordSpamRank2  = Posdb::getWordSpamRank(maxp2);
		px->m_hashGroup1     = Posdb::getHashGroup(maxp1);
		px->m_hashGroup2     = Posdb::getHashGroup(maxp2);
		px->m_qdist          = qdist;
		// bigram algorithm fix
		//if ( px->m_wordPos1 == px->m_wordPos2 )
		//	px->m_wordPos2 += 2;
		px->m_densityRank1   = Posdb::getDensityRank(maxp1);
		px->m_densityRank2   = Posdb::getDensityRank(maxp2);
		px->m_fixedDistance  = fixedDist;
		px->m_qtermNum1      = m_qtermNums[i];
		px->m_qtermNum2      = m_qtermNums[j];
		px->m_tfWeight1      = termFreqWeight1;
		px->m_tfWeight2      = termFreqWeight2;
		px->m_bflags1        = m_bflags[i];
		px->m_bflags2        = m_bflags[j];

		// flag it as in same wiki phrase
		if ( almostEqualFloat(wikiPhraseWeight, (float)WIKI_WEIGHT) ) {
			px->m_inSameWikiPhrase = 1;
		}
		else {
			px->m_inSameWikiPhrase = 0;
		}
		
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(px,sizeof(*px));
#endif
		// only log for debug if it is one result
		if ( m_debug ) {
			// log each one for debug
			log(LOG_INFO, "posdb: result #%" PRId32" "
					    "i=%" PRId32" "
					    "j=%" PRId32" "
					    "termNum0=%" PRId32" "
					    "termNum1=%" PRId32" "
					    "finalscore=%f "
					    "tfw0=%f "
					    "tfw1=%f "
					    "fixeddist=%" PRId32" " // bool
					    "wikiPhraseWeight=%f "
					    "bflags0=%" PRId32" "
					    "bflags1=%" PRId32" "
					    "syn0=%" PRId32" "
					    "syn1=%" PRId32" "
					    "div0=%" PRId32" "
					    "div1=%" PRId32" "
					    "wspam0=%" PRId32" "
					    "wspam1=%" PRId32" "
					    "hgrp0=%s "
					    "hgrp1=%s "
					    "qdist=%" PRId32" "
					    "wpos0=%" PRId32" "
					    "wpos1=%" PRId32" "
					    "dens0=%" PRId32" "
					    "dens1=%" PRId32" ", k, i, j, px->m_qtermNum1, px->m_qtermNum2, score, termFreqWeight1,
			    termFreqWeight2, (int32_t) bestFixed[k], wikiPhraseWeight, (int32_t) m_bflags[i], (int32_t) m_bflags[j],
			    (int32_t) px->m_isSynonym1, (int32_t) px->m_isSynonym2, (int32_t) px->m_diversityRank1,
			    (int32_t) px->m_diversityRank2, (int32_t) px->m_wordSpamRank1, (int32_t) px->m_wordSpamRank2,
			    getHashGroupString(px->m_hashGroup1), getHashGroupString(px->m_hashGroup2), (int32_t) px->m_qdist,
			    (int32_t) px->m_wordPos1, (int32_t) px->m_wordPos2, (int32_t) px->m_densityRank1,
			    (int32_t) px->m_densityRank2
			);
		}
	}

	// do the same but for second bests! so seo.cpp's top term pairs
	// algo can do a term insertion, and if that hurts the best pair
	// the 2nd best might cover for it. ideally, we'd have all the term
	// pairs for this algo, but i think we'll have to get by on just this.

	logTrace(g_conf.m_logTracePosdb, "END. sum=%f", sum);
	return sum;
}

//
//
// TRY TO SPEED IT UP!!!
//
//


// returns false and sets g_errno on error
bool PosdbTable::setQueryTermInfo ( ) {

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// alloc space. assume max
	m_queryTermInfos.resize(m_q->m_numTerms);
#ifdef _VALGRIND_
	VALGRIND_MAKE_MEM_UNDEFINED(&(m_queryTermInfos[0]), sizeof(m_queryTermInfos[0])*m_queryTermInfos.size());
#endif
	
	int32_t nrg = 0;

	// assume not sorting by a numeric termlist
	m_sortByTermNum = -1;
	m_sortByTermNumInt = -1;

	m_hasMaxSerpScore = false;
	if ( m_msg39req->m_minSerpDocId ) {
		m_hasMaxSerpScore = true;
	}

	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];

		logTrace(g_conf.m_logTracePosdb, "i=%" PRId32 ", term=[%.*s], required=%s", i, qt->m_termLen, qt->m_term, qt->m_isRequired?"true":"false");

		if ( ! qt->m_isRequired ) {
			continue;
		}
		
		// set this stff
		const QueryWord     *qw =   qt->m_qword;
		// get one
		QueryTermInfo *qti = &(m_queryTermInfos[nrg]);
		// and set it
		qti->m_qtermNum      = i;
		qti->m_qterm         = qt;

		// this is not good enough, we need to count 
		// non-whitespace punct as 2 units not 1 unit
		// otherwise qdist gets thrown off and our phrasing fails.
		// so use QueryTerm::m_qpos just for this.
		qti->m_qpos          = qw->m_posNum;
		qti->m_wikiPhraseId  = qw->m_wikiPhraseId;
		qti->m_quotedStartId = qw->m_quoteStart;
		switch(qt->m_fieldCode) {
			// is it gbsortby:?
			case FIELD_GBSORTBYINT:
			case FIELD_GBREVSORTBYINT:
				m_sortByTermNumInt = i;
				m_sortByTermInfoNumInt = nrg;
				// tell topTree to use int scores
				m_topTree->m_useIntScores = true;
				break;
			default:
				; //not numeric condition
		}
		// count
		int32_t nn = 0;
		// also add in bigram lists
		int32_t left  = qt->m_leftPhraseTermNum;
		int32_t right = qt->m_rightPhraseTermNum;
		// terms
		const QueryTerm *leftTerm  = qt->m_leftPhraseTerm;
		const QueryTerm *rightTerm = qt->m_rightPhraseTerm;
		bool leftAlreadyAdded = false;
		bool rightAlreadyAdded = false;
		//
		// add the non-bigram list AFTER the
		// bigrams, which we like to do when we PREFER the bigram
		// terms because they are scored higher, specifically, as
		// in the case of being half stop wikipedia phrases like
		// "the tigers" for the query 'the tigers' we want to give
		// a slight bonus, 1.20x, for having that bigram since its
		// in wikipedia
		//

		//
		// add left bigram lists. BACKWARDS.
		//
		if ( left>=0 && leftTerm && leftTerm->m_isWikiHalfStopBigram ){
			// assume added
			leftAlreadyAdded = true;
			// get list
			RdbList *list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subList[nn].m_qt = &m_q->m_qterms[left];
			qti->m_subList[nn].m_list = list;
			// special flags
			qti->m_subList[nn].m_bigramFlag = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
			// add list of member terms as well
			m_q->m_qterms[left].m_bitNum = nrg;
			// only really add if useful
			if ( list && !list->isEmpty() ) {
				nn++;
			}

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) {
					continue;
				}
				
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subList[nn].m_qt = bt;
				qti->m_subList[nn].m_list = list;
				qti->m_subList[nn].m_bigramFlag = 0;
				qti->m_subList[nn].m_bigramFlag |= BF_HALFSTOPWIKIBIGRAM;
				qti->m_subList[nn].m_bigramFlag |= BF_SYNONYM;
				if (qt->m_piped) {
					qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
				}
				// add list of member terms as well
				bt->m_bitNum = nrg;
				if ( list && !list->isEmpty() ) {
					nn++;
				}
			}
		}
		//
		// then the right bigram if also in a wiki half stop bigram
		//
		if ( right >=0 && rightTerm && rightTerm->m_isWikiHalfStopBigram ){
			// assume added
			rightAlreadyAdded = true;
			// get list
			RdbList *list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subList[nn].m_qt = &m_q->m_qterms[right];
			qti->m_subList[nn].m_list = list;
			// special flags
			qti->m_subList[nn].m_bigramFlag = BF_HALFSTOPWIKIBIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
			// add list of member terms as well
			m_q->m_qterms[right].m_bitNum = nrg;
			// only really add if useful
			if ( list && !list->isEmpty() ) {
				nn++;
			}

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) {
					continue;
				}
				
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subList[nn].m_qt = bt;
				qti->m_subList[nn].m_list = list;
				qti->m_subList[nn].m_bigramFlag = 0;
				qti->m_subList[nn].m_bigramFlag |= BF_HALFSTOPWIKIBIGRAM;
				qti->m_subList[nn].m_bigramFlag |= BF_SYNONYM;
				if (qt->m_piped) {
					qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
				}
				// add list of member terms as well
				bt->m_bitNum = nrg;
				if ( list && !list->isEmpty() ) {
					nn++;
				}
			}
		}

		//
		// then the non-bigram termlist
		//
		// add to it. add backwards since we give precedence to
		// the first list and we want that to be the NEWEST list!
		RdbList *list = m_q->m_qterms[i].m_posdbListPtr;
		// add list ptr into our required group
		qti->m_subList[nn].m_qt         = qt;
		qti->m_subList[nn].m_list       = list;
		// special flags
		qti->m_subList[nn].m_bigramFlag = 0;
		// before a pipe operator?
		if ( qt->m_piped )
			qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
		// is it a negative term?
		if ( qt->m_termSign=='-')
			qti->m_subList[nn].m_bigramFlag |= BF_NEGATIVE;

		// numeric posdb termlist flags. instead of word position
		// they have a float stored there for sorting etc.
		switch(qt->m_fieldCode) {
			case FIELD_GBSORTBYINT:
			case FIELD_GBREVSORTBYINT:
				qti->m_subList[nn].m_bigramFlag |= BF_NUMBER;
				break;
			default:
				;
		}

		// add list of member terms
		qt->m_bitNum = nrg;

		// only really add if useful
		// no, because when inserting NEW (related) terms that are
		// not currently in the document, this list may initially
		// be empty.
		if ( list && !list->isEmpty() ) {
			nn++;
		}
			
		// 
		// add left bigram now if not added above
		//
		if ( left >= 0 && ! leftAlreadyAdded ) {
			// get list
			list = m_q->m_qterms[left].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subList[nn].m_qt = &m_q->m_qterms[left];
			qti->m_subList[nn].m_list = list;
			// special flags
			qti->m_subList[nn].m_bigramFlag = BF_BIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
			// add list of member terms as well
			m_q->m_qterms[left].m_bitNum = nrg;
			// only really add if useful
			if ( list && !list->isEmpty() ) {
				nn++;
			}

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != leftTerm ) {
					continue;
				}
				
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subList[nn].m_qt = bt;
				qti->m_subList[nn].m_list = list;
				qti->m_subList[nn].m_bigramFlag = 0;
				qti->m_subList[nn].m_bigramFlag |= BF_SYNONYM;
				if (qt->m_piped) {
					qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
				}
				// add list of member terms as well
				bt->m_bitNum = nrg;
				if ( list && !list->isEmpty() ) {
					nn++;
				}
			}
		}
		
		// 
		// add right bigram now if not added above
		//
		if ( right >= 0 && ! rightAlreadyAdded ) {
			// get list
			list = m_q->m_qterms[right].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subList[nn].m_qt = &m_q->m_qterms[right];
			qti->m_subList[nn].m_list = list;
			// special flags
			qti->m_subList[nn].m_bigramFlag = BF_BIGRAM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
			// add list of member terms as well
			m_q->m_qterms[right].m_bitNum = nrg;
			// only really add if useful
			if ( list && !list->isEmpty() ) {
				nn++;
			}

			// add bigram synonyms! like "new jersey" bigram
			// has the synonym "nj"
			for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
				QueryTerm *bt = &m_q->m_qterms[k];
				if ( bt->m_synonymOf != rightTerm ) {
					continue;
				}
				
				list = m_q->m_qterms[k].m_posdbListPtr;
				qti->m_subList[nn].m_qt = bt;
				qti->m_subList[nn].m_list = list;
				qti->m_subList[nn].m_bigramFlag = 0;
				qti->m_subList[nn].m_bigramFlag |= BF_SYNONYM;
				if (qt->m_piped) {
					qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
				}
				// add list of member terms as well
				//qti->m_qtermList[nn] = bt;
				bt->m_bitNum = nrg;
				if ( list && !list->isEmpty() ) {
					nn++;
				}
			}
		}

		//
		// ADD SYNONYM TERMS
		//
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			QueryTerm *qt2 = &m_q->m_qterms[k];
			const QueryTerm *st = qt2->m_synonymOf;
			// skip if not a synonym of this term
			if ( st != qt ) {
				continue;
			}
			
			// its a synonym, add it!
			list = m_q->m_qterms[k].m_posdbListPtr;
			// add list ptr into our required group
			qti->m_subList[nn].m_qt = qt2;
			qti->m_subList[nn].m_list = list;
			// special flags
			qti->m_subList[nn].m_bigramFlag = BF_SYNONYM;
			// before a pipe operator?
			if ( qt->m_piped ) qti->m_subList[nn].m_bigramFlag |= BF_PIPED;
			// set bitnum here i guess
			qt2->m_bitNum = nrg;
			// only really add if useful
			if ( list && !list->isEmpty() ) {
				nn++;
			}
		}


		// store # lists in required group. nn might be zero!
		qti->m_numSubLists = nn;
		// crazy?
		if ( nn >= MAX_SUBLISTS ) {
			log("query: too many sublists. %" PRId32" >= %" PRId32,
			    nn,(int32_t)MAX_SUBLISTS);
			logTrace(g_conf.m_logTracePosdb, "END.");
			g_errno = EQUERYTOOBIG;
			return false;
		}
		
		// count # required groups
		nrg++;
	}

	//Detect the cases where the base term to the left of the qti-baseterm is ignored. This is used by getBestScoreSumForSingleTerm()
	//to ensure that bigram match boosts are not lost.
	for(int i=0; i<nrg; i++) {
		QueryTermInfo &qti = m_queryTermInfos[i];
		qti.m_leftTermIsIgnored = false;
		if(m_queryTermInfos[i].m_qterm->m_leftPhraseTerm) {
			for(int j=0; j<m_q->m_numTerms; j++) {
				QueryTerm *qt = &m_q->m_qterms[j];
				if(qt==m_queryTermInfos[i].m_qterm->m_leftPhraseTerm) {
					if(qt->m_qword->m_ignoreWord!=IGNORE_NO_IGNORE)
						qti.m_leftTermIsIgnored = true;
					break; //assumption: only one bigram per term. May not be true when bigram-synonyms are added by word variations
				}
			}
		}
	}

	//
	// get the query term with the least data in posdb including syns
	//
	m_minTermListSize	= 0;
	m_minTermListIdx	= -1;
	int64_t grand 		= 0LL;

	for ( int32_t i = 0 ; i < nrg ; i++ ) {
		// compute total sizes
		QueryTermInfo *qti = &(m_queryTermInfos[i]);
		// do not consider for first termlist if negative
		if ( qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
			continue;
		}
		
		int64_t totalSubListsSize = 0;
		for(int32_t q = 0; q < qti->m_numSubLists; q++) {
			const RdbList *l = qti->m_subList[q].m_list;
			totalSubListsSize += l->getListSize();
		}
		
		// add up this now
		grand += totalSubListsSize;

		// get min
		if ( totalSubListsSize < m_minTermListSize || m_minTermListIdx == -1 ) {
			m_minTermListSize	= totalSubListsSize;
			m_minTermListIdx	= i;
		}
	}

	// bad! ringbuf[] was not designed for this nonsense!
	if ( m_minTermListIdx >= 255 ) {
		gbshutdownAbort(true);
	}
	
	// set this for caller to use to loop over the queryterminfos
	m_numQueryTermInfos = nrg;

	if(g_conf.m_logTracePosdb) {
		logTrace(g_conf.m_logTracePosdb, "m_numQueryTermInfos=%d", m_numQueryTermInfos);
		for(int i=0; i<m_numQueryTermInfos; i++) {
			const QueryTermInfo *qti = &(m_queryTermInfos[i]);
			logTrace(g_conf.m_logTracePosdb, "  qti[%d]: m_numSubLists=%d m_qtermNum=%d m_qpos=%d m_leftTermIsIgnored=%s", i, qti->m_numSubLists, qti->m_qtermNum, qti->m_qpos, qti->m_leftTermIsIgnored?"true":"false");
			for(int j=0; j<qti->m_numSubLists; j++)
				logTrace(g_conf.m_logTracePosdb, "    sublist %d = %p", j, qti->m_subList[j].m_list);
		}
	}

	//check if any of the terms are not being used. This can happen if query was truncated and some bigrams or synonyms were included while the base word was not.
	std::vector<bool> termUsed(m_q->m_numTerms);
	for(int i=0; i<m_numQueryTermInfos; i++) {
		const QueryTermInfo *qti = &(m_queryTermInfos[i]);
		for(int j=0; j<qti->m_numSubLists; j++) {
			const QueryTerm *qt = qti->m_subList[j].m_qt;
			int qtermNum = qt - m_q->m_qterms;
			termUsed[qtermNum] = true;
		}
	}
	for(int i=0; i<m_q->m_numTerms; i++) {
		const QueryTerm *qt = &m_q->m_qterms[i];
		logTrace(g_conf.m_logTracePosdb,"termUsed[%d]=%s (%.*s)", i, termUsed[i]?"true":"false", qt->m_termLen,qt->m_term);
		if(!termUsed[i] && !qt->m_ignored && !qt->m_isQueryStopWord && qt->m_posdbListPtr && !qt->m_posdbListPtr->isEmpty())
			log(LOG_DEBUG, "posdb: unused term found #%d '%.*s' in query '%.*s'. Was query truncated?", i, qt->m_termLen, qt->m_term, m_q->getQueryLen(), m_q->getQuery());
	}

	// . m_minTermListSize is set in setQueryTermInfo()
	// . how many docids do we have at most in the intersection?
	// . all keys are of same termid, so they are 12 or 6 bytes compressed
	// . assume 12 if each is a different docid
	int32_t maxDocIds = m_minTermListSize / 12;
	// store all interesected docids in here for new algo plus 1 byte vote
	int32_t need = maxDocIds * 6;

	// they could all be OR'd together!
	if ( m_q->m_isBoolean ) need = grand;

	// so we can always cast a int64_t from a ptr in there
	// for setting m_docId when m_booleanQuery is true below
	need += 8;

	// get max # of docids we got in an intersection from all the lists
	if ( ! m_docIdVoteBuf.reserve ( need,"divbuf" ) ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return false;
	}

	// i'm feeling if a boolean query put this in there too, the
	// hashtable that maps each docid to its boolean bit vector
	// where each bit stands for an operand so we can quickly evaluate
	// the bit vector in a truth table.
	// CRAP, can't use min list size because it might be behind a
	// NOT operator!!! then we core trying to realloc m_bt in a thread
	// below when trying to grow it. they could all be OR'd together
	// so alloc the most!
	int32_t maxSlots = (grand/12) * 2;
	// try to speed up. this doesn't *seem* to matter, so i took out:
	//maxSlots *= 2;
	// get total operands we used
	//int32_t numOperands = m_q->m_numWords;//Operands;
	// a quoted phrase counts as a single operand
	// . QueryTerm::m_bitNum <== m_numQueryTermInfos
	// . each queryTermInfo class corresponds to one bit in our bit vec
	// . essentially each queryTermInfo is a query term, but it has
	//   all the synonym and word forms for that query, etc.
	m_vecSize = m_numQueryTermInfos / 8;//numOperands / 8 ;
	// allow an extra byte for remainders
	if ( m_numQueryTermInfos % 8 ) m_vecSize++;
	// now preallocate the hashtable. 0 niceness.
	if ( m_q->m_isBoolean &&  // true = useKeyMagic
	     ! m_bt.set (8,m_vecSize,maxSlots,NULL,0,false,"booltbl",true)) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return false;
	}
	
	// . m_ct maps a boolean "bit vector" to a true/false value
	// . each "bit" in the "bit vector" indicates if docid has that 
	//   particular query term
	if ( m_q->m_isBoolean && // true = useKeyMagic
	     ! m_ct.set (8,1,maxSlots,NULL,0,false,"booltbl",true)) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return false;
	}

#ifdef _VALGRIND_
	//various arrays are over-allocated. tell valgrind what is not defined content
	for(int i=0; i<m_numQueryTermInfos; i++) {
		QueryTermInfo &qti = m_queryTermInfos[i];
		VALGRIND_MAKE_MEM_UNDEFINED(qti.m_subList+qti.m_numSubLists, sizeof(qti.m_subList[0])*(MAX_SUBLISTS-qti.m_numSubLists));
	}
	for(int i=m_numQueryTermInfos; i<m_q->m_numTerms;  i++)
		VALGRIND_MAKE_MEM_UNDEFINED(&(m_queryTermInfos[i]), sizeof(QueryTermInfo));
#endif

	logTrace(g_conf.m_logTracePosdb, "END.");
	return true;
}



bool PosdbTable::findCandidateDocIds() {
	int64_t lastTime = gettimeofdayInMilliseconds();


	//
	// Swap the top 6 bytes of each list.
	//
	// This gives a contiguous list of 12-byte docid/score entries
	// because the first record is always 18 bytes (termid, docid, score) 
	// and the rest are 6 bytes due to our termid compression.
	//
	// This makes the lists much much easier to work with, but we have
	// to remember to swap back when done! (but we dont...)
	//
	for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
		// count
		int64_t total = 0LL;
		// get the list
		RdbList *list = m_q->m_qterms[k].m_posdbListPtr;
		// skip if null or empty
		if(list && !list->isEmpty()) {
			// tally
			total += list->getListSize();
			// point to start
			char *p = list->getList();
			
			// remember to swap back when done!!
			char ttt[12];
			memcpy ( ttt   , p       , 12 );
			memcpy ( p     , p + 12  ,  6 );
			memcpy ( p + 6 , ttt     , 12 );

			// point to the low "hks" bytes now, skipping the termid
			p += 6;
			
			// turn half bit on. first key is now 12 bytes!!
			*p |= 0x02;
			// MANGLE the list
			list->setListSize(list->getListSize() - 6);
			list->setList(p);

			logTrace(g_conf.m_logTracePosdb, "termList #%" PRId32" totalSize=%" PRId64, k, total);

			// print total list sizes
			if ( m_debug ) {
				log(LOG_INFO, "query: termlist #%" PRId32" totalSize=%" PRId64, k, total);
			}
		}
	}


	// setQueryTermInfos() should have set how many we have
	if ( m_numQueryTermInfos == 0 ) {
		log(LOG_DEBUG, "query: NO REQUIRED TERMS IN QUERY!");
		logTrace(g_conf.m_logTracePosdb, "END, m_numQueryTermInfos = 0");
		return false;
	}

	// . if smallest required list is empty, 0 results
	// . also set in setQueryTermInfo
	if ( m_minTermListSize == 0 && ! m_q->m_isBoolean ) {
		logTrace(g_conf.m_logTracePosdb, "END, m_minTermListSize = 0 and not boolean");
		return false;
	}


	int32_t listGroupNum = 0;


	// if all non-negative query terms are in the same wikiphrase then
	// we can apply the WIKI_WEIGHT in getMaxPossibleScore() which
	// should help us speed things up!
	// @@@ BR: Why should this give a boost at all.. ?
	m_allInSameWikiPhrase = true;
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		const QueryTermInfo *qti = &(m_queryTermInfos[i]);

		if(qti->m_numSubLists==0)
			continue; //ignored word or stopword.

		// skip if negative query term
		if ( qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
			continue;
		}
		
		// skip if numeric field like gbsortby:price gbmin:price:1.23
		if ( qti->m_subList[0].m_bigramFlag & BF_NUMBER ) {
			continue;
		}
		
		// set it
		if ( qti->m_wikiPhraseId == 1 ) {
			continue;			//@@@ BR: Why only check for id = 1 ??
		}
		
		// stop
		m_allInSameWikiPhrase = false;
		break;
	}
	logTrace(g_conf.m_logTracePosdb, "m_allInSameWikiPhrase: %s", (m_allInSameWikiPhrase?"true":"false") );


	// for boolean queries we scan every docid in all termlists,
	// then we see what query terms it has, and make a bit vector for it.
	// then use a hashtable to map that bit vector to a true or false
	// as to whether we should include it in the results or not.
	// we use Query::getBitScore(qvec_t ebits) to evaluate a docid's
	// query term explicit term bit vector.
	if ( m_q->m_isBoolean ) {
		logTrace(g_conf.m_logTracePosdb, "makeDocIdVoteBufForBoolQuery");
		// keeping the docids sorted is the challenge here...
		makeDocIdVoteBufForBoolQuery();
	}
	else {
		// create "m_docIdVoteBuf" filled with just the docids from the
		// smallest group of sublists (one term, all variations).
		//
		// m_minTermListIdx is the queryterminfo that had the smallest total
		// sublist sizes of m_minTermListSize. this was set in 
		// setQueryTermInfos()
		//
		// if all these sublist termlists were 50MB i'd day 10-25ms to
		// add their docid votes.
		logTrace(g_conf.m_logTracePosdb, "addDocIdVotes");
		addDocIdVotes ( &m_queryTermInfos[m_minTermListIdx], listGroupNum );
		

		// now repeat the docid scan for successive lists but only
		// inc the docid count for docids we match. docids that are 
		// in m_docIdVoteBuf but not in sublist group #i will be removed
		// from m_docIdVoteBuf. 
		//
		// worst case scenario with termlists limited
		// to 30MB will be about 10MB of docid vote buf, but it should
		// shrink quite a bit every time we call addDocIdVotes() with a 
		// new group of sublists for a query term. but scanning 10MB should
		// be pretty fast since gk0 does like 4GB/s of main memory reads.
		// i would think scanning and docid voting for 200MB of termlists 
		// should take like 50-100ms
		for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
			// skip if we did it above when allocating the vote buffer
			if ( i == m_minTermListIdx ) {
				continue;
			}
			
			// get it
			const QueryTermInfo *qti = &(m_queryTermInfos[i]);
			// do not consider for adding if negative ('my house -home')
			if ( qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
				continue;
			}

			// inc the group number ("score"), which will be set on the docids
			// in addDocIdVotes if they match.
			listGroupNum++;
			
			// if it hits 256 then wrap back down to 1
			if ( listGroupNum >= 256 ) {
				listGroupNum = 1;
			}
			
			// add it
			addDocIdVotes ( qti, listGroupNum );
		}
		logTrace(g_conf.m_logTracePosdb, "Added DocIdVotes");


		//
		// remove the negative query term docids from our docid vote buf
		//
		for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
			// skip if we did it above
			if ( i == m_minTermListIdx ) {
				continue;
			}
			
			// get it
			const QueryTermInfo *qti = &(m_queryTermInfos[i]);
			
			// do not consider for adding if negative ('my house -home')
			if ( ! (qti->m_subList[0].m_bigramFlag & BF_NEGATIVE) ) {
				continue;
			}
			
			// delete the docid from the vote buffer
			delDocIdVotes ( qti );
		}
		logTrace(g_conf.m_logTracePosdb, "Removed DocIdVotes for negative query terms");
	}

	if ( m_debug ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t took = now - lastTime;
		log(LOG_INFO, "posdb: new algo phase (find matching docids) took %" PRId64" ms", took);
		lastTime = now;
	}


	//
	// NOW FILTER EVERY SUBLIST to just the docids in m_docIdVoteBuf.
	// Filter in place so it is destructive. i would think 
	// that doing a filter on 200MB of termlists wouldn't be more than
	// 50-100ms since we can read 4GB/s from main memory.
	//
	delNonMatchingDocIdsFromSubLists();
	logTrace(g_conf.m_logTracePosdb, "Shrunk SubLists");
	if(g_conf.m_logTracePosdb) {
		log(LOG_TRACE,"Shrunk sublists, m_numQueryTermInfos=%d", m_numQueryTermInfos);
		for(int i=0; i<m_numQueryTermInfos; i++) {
			log(LOG_TRACE,"  qti #%d: m_numSubLists=%d", i, m_queryTermInfos[i].m_numSubLists);
			for(int j=0; j<m_queryTermInfos[i].m_numSubLists; j++)
				log(LOG_TRACE,"             list #%d: m_qt=%p (%*.*s) m_list=%p m_bigramFlag=%02x", j, m_queryTermInfos[i].m_subList[j].m_qt, m_queryTermInfos[i].m_subList[j].m_qt->m_termLen, m_queryTermInfos[i].m_subList[j].m_qt->m_termLen, m_queryTermInfos[i].m_subList[j].m_qt->m_term, m_queryTermInfos[i].m_subList[j].m_list, m_queryTermInfos[i].m_subList[j].m_bigramFlag);
			log(LOG_TRACE,"          m_numMatchingSubLists=%d", m_queryTermInfos[i].m_numMatchingSubLists);
			for(int j=0; j<m_queryTermInfos[i].m_numMatchingSubLists; j++)
				log(LOG_TRACE,"             matchlist #%d: %d bytes %p - %p baseidx=%d", j, m_queryTermInfos[i].m_matchingSublist[j].m_size, m_queryTermInfos[i].m_matchingSublist[j].m_start, m_queryTermInfos[i].m_matchingSublist[j].m_end, m_queryTermInfos[i].m_matchingSublist[j].m_baseSubListIndex);
		}
	}

	if ( m_debug ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t took = now - lastTime;
		log(LOG_INFO, "posdb: new algo phase (shrink sublists) took %" PRId64" ms", took);
	}

	return true;
}


bool PosdbTable::genDebugScoreInfo1(int32_t *numProcessed, int32_t *topCursor, bool *docInThisFile) {
	*docInThisFile = false;

	// did we get enough score info?
	if ( *numProcessed >= m_msg39req->m_docsToGet ) {
		logTrace(g_conf.m_logTracePosdb, "Too many docs processed. m_docId=%" PRId64 ". Reached msg39 m_docsToGet: %" PRId32 ", SKIPPED", m_docId, *numProcessed);
		return true;
	}
	
	// loop back up here if the docid is from a previous range
nextNode:
	// this mean top tree empty basically
	if ( *topCursor == -1 ) {
		logTrace(g_conf.m_logTracePosdb, "topCursor is -1, SKIPPED");
		return true;
	}
	
	// get the #1 docid/score node #
	if ( *topCursor == -9 ) {
		// if our query had a quoted phrase, might have had no
		// docids in the top tree! getHighNode() can't handle
		// that so handle it here
		if ( m_topTree->getNumUsedNodes() == 0 ) {
			logTrace(g_conf.m_logTracePosdb, "Num used nodes is 0, SKIPPED");
			return true;
		}
		
		// otherwise, initialize topCursor
		*topCursor = m_topTree->getHighNode();
	}
	
	// get current node
	TopNode *tn = m_topTree->getNode ( *topCursor );
	// advance
	*topCursor = m_topTree->getPrev ( *topCursor );

	// shortcut
	m_docId = tn->m_docId;
	
	// skip if not in our range! the top tree now holds
	// all the winners from previous docid ranges. msg39
	// now does the search result in docid range chunks to avoid
	// OOM conditions.
	if ( m_msg39req->m_minDocId != -1 &&
	     m_msg39req->m_maxDocId != -1 &&
	     ( m_docId < (uint64_t)m_msg39req->m_minDocId || 
	       m_docId >= (uint64_t)m_msg39req->m_maxDocId ) ) {
		logTrace(g_conf.m_logTracePosdb, "DocId %" PRIu64 " does not match docId range %" PRIu64 " - %" PRIu64 ", SKIPPED", m_docId, (uint64_t)m_msg39req->m_minDocId, (uint64_t)m_msg39req->m_maxDocId);
		goto nextNode;
	}

	*docInThisFile = m_documentIndexChecker->exists(m_docId);
	if( !(*docInThisFile) ) {
		logTrace(g_conf.m_logTracePosdb, "DocId %" PRId64 " is not in this file, SKIPPED", m_docId);
		return false;
	}

	logTrace(g_conf.m_logTracePosdb, "DocId %" PRId64 " - setting up score buffer", m_docId);

	(*numProcessed)++;

	// set query termlists in all sublists
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &(m_queryTermInfos[i]);
		// do not advance negative termlist cursor
		if ( qti->m_numSubLists>0 && qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
			continue;
		}
		
		// do each sublist
		for ( int32_t j = 0 ; j < qti->m_numMatchingSubLists ; j++ ) {
			// get termlist for that docid
			const char *xlist    = qti->m_matchingSublist[j].m_start;
			const char *xlistEnd = qti->m_matchingSublist[j].m_end;
			const char *xp = getWordPosList ( m_docId, xlist, xlistEnd - xlist);

			// not there? xlist will be NULL
			qti->m_matchingSublist[j].m_savedCursor = xp;

			// if not there make cursor NULL as well
			if ( ! xp ) {
				qti->m_matchingSublist[j].m_cursor = NULL;
				continue;
			}

			// skip over docid list
			xp += 12;

			for ( ; ; ) {
				// do not breach sublist
				if ( xp >= xlistEnd ) {
					break;
				}
				
				// break if 12 byte key: another docid!
				if ( !(xp[0] & 0x04) ) {
					break;
				}
				
				// skip over 6 byte key
				xp += 6;
			}

			// point to docid sublist end
			qti->m_matchingSublist[j].m_cursor = xp;
		}
	}

	return false;
}


bool PosdbTable::genDebugScoreInfo2(DocIdScore *dcs, int32_t *lastLen, uint64_t *lastDocId, char siteRank, float score, int32_t intScore, char docLang) {

	dcs->m_siteRank   = siteRank;
	dcs->m_finalScore = score;
	
	// a double can capture an int without dropping any bits,
	// inlike a mere float
	if ( m_sortByTermNumInt >= 0 ) {
		dcs->m_finalScore = (double)intScore;
	}
	
	dcs->m_docId      = m_docId;
	dcs->m_numRequiredTerms = m_numQueryTermInfos;
	dcs->m_docLang = docLang;
	logTrace(g_conf.m_logTracePosdb, "m_docId=%" PRId64 ", *lastDocId=%" PRId64 "", m_docId, *lastDocId);
	
	// ensure enough room we can't allocate in a thread!
	if ( m_scoreInfoBuf.getAvail()<(int32_t)sizeof(DocIdScore)+1) {
		logTrace(g_conf.m_logTracePosdb, "END, NO ROOM");
		return true;
	}
	
	// if same as last docid, overwrite it since we have a higher
	// siterank or langid i guess
	if ( m_docId == *lastDocId ) {
		m_scoreInfoBuf.m_length = *lastLen;
	}
	
	// save that
	int32_t len = m_scoreInfoBuf.m_length;
	
	// copy into the safebuf for holding the scoring info
#ifdef _VALGRIND_
VALGRIND_CHECK_MEM_IS_DEFINED(&dcs,sizeof(dcs));
#endif
	m_scoreInfoBuf.safeMemcpy ( (char *)dcs, sizeof(DocIdScore) );
	// save that
	*lastLen = len;
	// save it
	*lastDocId = m_docId;
	// try to fix dup docid problem! it was hitting the
	// overflow check right above here... strange!!!
	//m_docIdTable.removeKey ( &docId );

	/////////////////////////////
	//
	// . docid range split HACK...
	// . if we are doing docid range splitting, we process
	//   the search results separately in disjoint docid ranges.
	// . so because we still use the same m_scoreInfoBuf etc.
	//   for each split we process, we must remove info from
	//   a top docid of a previous split that got supplanted by
	//   a docid of this docid-range split, so we do not breach
	//   the allocated buffer size.
	// . so  find out which docid we replaced
	//   in the top tree, and replace his entry in scoreinfobuf
	//   as well!
	// . his info was already added to m_pairScoreBuf in the
	//   getTermPairScoreForAny() function
	//
	//////////////////////////////

	// the top tree remains persistent between docid ranges.
	// and so does the score buf. so we must replace scores
	// if the docid gets replaced by a better scoring docid
	// from a following docid range of different docids.
	// However, scanning the docid scor buffer each time is 
	// really slow, so we need to get the kicked out docid
	// from the top tree and use that to store its offset
	// into m_scoreInfoBuf so we can remove it.

	DocIdScore *si;

	// only kick out docids from the score buffer when there
	// is no room left...
	if ( m_scoreInfoBuf.getAvail() >= (int)sizeof(DocIdScore ) ) {
		logTrace(g_conf.m_logTracePosdb, "END, OK. m_docId=%" PRId64 ", *lastDocId=%" PRId64 "", m_docId, *lastDocId);
		return true;
	}

	char *sx = m_scoreInfoBuf.getBufStart();
	char *sxEnd = sx + m_scoreInfoBuf.length();
	
	// if we have not supplanted anyone yet, be on our way
	for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
		si = (DocIdScore *)sx;
		// if top tree no longer has this docid, we must
		// remove its associated scoring info so we do not
		// breach our scoring info bufs
		if ( ! m_topTree->hasDocId( si->m_docId ) ) {
			break;
		}
	}
	
	// might not be full yet
	if ( sx >= sxEnd ) {
		logTrace(g_conf.m_logTracePosdb, "END, OK 2");
		return true;
	}
	
	// must be there!
	if ( ! si ) {
		gbshutdownAbort(true);
	}

	// note it because it is slow
	// this is only used if getting score info, which is
	// not default when getting an xml or json feed
	logTrace(g_conf.m_logTracePosdb, "Kicking out docid %" PRId64" from score buf", si->m_docId);

	// get his single and pair offsets
	int32_t pairOffset   = si->m_pairsOffset;
	int32_t pairSize     = si->m_numPairs * sizeof(PairScore);
	int32_t singleOffset = si->m_singlesOffset;
	int32_t singleSize   = si->m_numSingles * sizeof(SingleScore);
	// nuke him
	m_scoreInfoBuf  .removeChunk1 ( sx, sizeof(DocIdScore) );
	// and his related info
	m_pairScoreBuf  .removeChunk2 ( pairOffset   , pairSize   );
	m_singleScoreBuf.removeChunk2 ( singleOffset , singleSize );
	
	// adjust offsets of remaining single scores
	sx = m_scoreInfoBuf.getBufStart();
	for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
		si = (DocIdScore *)sx;
		if ( si->m_pairsOffset > pairOffset ) {
			si->m_pairsOffset -= pairSize;
		}
		
		if ( si->m_singlesOffset > singleOffset ) {
			si->m_singlesOffset -= singleSize;
		}
	}
	
	// adjust this too!
	*lastLen -= sizeof(DocIdScore);

	logTrace(g_conf.m_logTracePosdb, "Returning false");
	return false;
}


void PosdbTable::logDebugScoreInfo(int32_t loglevel) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN");

	const char *sx = m_scoreInfoBuf.getBufStart();
	const char *sxEnd = sx + m_scoreInfoBuf.length();

	log(loglevel, "DocId scores in m_scoreInfoBuf:");
	for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
		const DocIdScore *si = (const DocIdScore *)sx;

		log(loglevel, "  docId: %14" PRIu64 ", score: %f", si->m_docId, si->m_finalScore);

		// if top tree no longer has this docid, we must
		// remove its associated scoring info so we do not
		// breach our scoring info bufs
		if ( ! m_topTree->hasDocId( si->m_docId ) ) {
			log(loglevel, "    ^ NOT in topTree anymore!");
		}
	}

	logTrace(g_conf.m_logTracePosdb, "END");
}


void PosdbTable::removeScoreInfoForDeletedDocIds() {
	logTrace(g_conf.m_logTracePosdb, "BEGIN");

	char *sx = m_scoreInfoBuf.getBufStart();
	char *sxEnd = sx + m_scoreInfoBuf.length();

	for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
		DocIdScore *si = (DocIdScore *)sx;

		// if top tree no longer has this docid, we must
		// remove its associated scoring info so we do not
		// breach our scoring info bufs
		if ( m_topTree->hasDocId( si->m_docId ) ) {
			continue;
		}

		logTrace(g_conf.m_logTracePosdb, "Removing old score info for docId %" PRId64 "", si->m_docId);
		// get his single and pair offsets
		int32_t pairOffset   = si->m_pairsOffset;
		int32_t pairSize     = si->m_numPairs * sizeof(PairScore);
		int32_t singleOffset = si->m_singlesOffset;
		int32_t singleSize   = si->m_numSingles * sizeof(SingleScore);
		// nuke him
		m_scoreInfoBuf  .removeChunk1 ( sx, sizeof(DocIdScore) );
		// and his related info
		m_pairScoreBuf  .removeChunk2 ( pairOffset   , pairSize   );
		m_singleScoreBuf.removeChunk2 ( singleOffset , singleSize );

		// adjust offsets of remaining single scores
		sx = m_scoreInfoBuf.getBufStart();
		for ( ; sx < sxEnd ; sx += sizeof(DocIdScore) ) {
			si = (DocIdScore *)sx;
			if ( si->m_pairsOffset > pairOffset ) {
				si->m_pairsOffset -= pairSize;
			}

			if ( si->m_singlesOffset > singleOffset ) {
				si->m_singlesOffset -= singleSize;
			}
		}
		sxEnd -= sizeof(DocIdScore);
	}

	logTrace(g_conf.m_logTracePosdb, "END");
}



// Pre-advance each termlist's cursor to skip to next docid.
//
// Set QueryTermInfo::m_matchingSubListCursor to NEXT docid
// Set QueryTermInfo::m_matchingSubListSavedCursor to CURRENT docid
// of each termlist so we are ready for a quick skip over this docid.
//
// TODO: use just a single array of termlist ptrs perhaps,
// then we can remove them when they go NULL.  and we'd save a little
// time not having a nested loop.
bool PosdbTable::advanceTermListCursors(const char *docIdPtr) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN");

	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo *qti = &(m_queryTermInfos[i]);
		// do not advance negative termlist cursor
		if ( qti->m_numSubLists>0 && qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
			continue;
		}

		//
		// In first pass, sublists data is initialized by delNonMatchingDocIdsFromSubLists.
		// In second pass (to get detailed scoring info for UI output), they are initialized above
		//
		for ( int32_t j = 0 ; j < qti->m_numMatchingSubLists ; j++ ) {
			// shortcuts
			const char *xc    = qti->m_matchingSublist[j].m_cursor;
			const char *xcEnd = qti->m_matchingSublist[j].m_end;

			// exhausted? (we can't make cursor NULL because
			// getMaxPossibleScore() needs the last ptr)
			// must match docid
			if ( xc >= xcEnd ||
			     *(int32_t *)(xc+8) != *(int32_t *)(docIdPtr+1) ||
			     (*(char *)(xc+7)&0xfc) != (*(char *)(docIdPtr)&0xfc) ) {
				// flag it as not having the docid
				qti->m_matchingSublist[j].m_savedCursor = NULL;
				// skip this sublist if does not have our docid
				continue;
			}

			// save it
			qti->m_matchingSublist[j].m_savedCursor = xc;
			// get new docid
			//log("new docid %" PRId64,Posdb::getDocId(xc) );
			// advance the cursors. skip our 12
			xc += 12;
			// then skip any following 6 byte keys because they
			// share the same docid
			for ( ;  ; xc += 6 ) {
				// end of whole termlist?
				if ( xc >= xcEnd ) {
					break;
				}
				
				// sanity. no 18 byte keys allowed
				if ( (*xc & 0x06) == 0x00 ) {
					// i've seen this triggered on gk28.
					// a dump of posdb for the termlist
					// for 'post' had corruption in it,
					// yet its twin, gk92 did not. the
					// corruption could have occurred
					// anywhere from nov 2012 to may 2013,
					// and the posdb file was never
					// re-merged! must have been blatant
					// disk malfunction?
					log("posdb: encountered corrupt posdb list. bailing.");
					logTrace(g_conf.m_logTracePosdb, "END.");
					return false;
					//gbshutdownAbort(true);
				}
				// the next docid? it will be a 12 byte key.
				if ( ! (*xc & 0x04) ) {
					break;
				}
			}
			// assign to next docid word position list
			qti->m_matchingSublist[j].m_cursor = xc;
		}
	}

	logTrace(g_conf.m_logTracePosdb, "END");
	return true;
}



#define RINGBUFSIZE 4096

//Go through the word positions of the queryterminfo posdb lists and set the corresponding ringBuf[] elements to the qti-index.
//Also find the first position and return that.
static int32_t setRingbufFromQTI(const QueryTermInfo *qti, int32_t listIdx, unsigned char ringBuf[]) {
	int32_t firstPos = -1;
	for(int32_t i = 0 ; i < qti->m_numMatchingSubLists; i++) {
		// scan that sublist and add word positions
		const char *sub = qti->m_matchingSublist[i].m_savedCursor;
		// skip sublist if it's cursor is exhausted
		if(!sub)
			continue;

		const char *end = qti->m_matchingSublist[i].m_cursor;
		// add first key
		int32_t wx = Posdb::getWordPos(sub);
		wx &= (RINGBUFSIZE-1);
		// store it. 0 is legit.
		ringBuf[wx] = listIdx;
		if(firstPos<0 || wx<firstPos)
			firstPos = wx;
		// skip first key
		sub += 12;
		// then 6 byte keys
		for( ; sub < end; sub += 6) {
			// get word position
			wx = Posdb::getWordPos(sub);
			wx &= (RINGBUFSIZE-1);
			// store it. 0 is legit.
			ringBuf[wx] = listIdx;
		}
	}
	return firstPos;
}

//
// TODO: consider skipping this pre-filter if it sucks, as it does
// for 'search engine'. it might save time!
//
// Returns:
//	false - docid does not meet minimum score requirement
//	true - docid can potentially be a top scoring docid
//
bool PosdbTable::prefilterMaxPossibleScoreByDistance(float minWinningScore) {
	unsigned char ringBuf[RINGBUFSIZE+10];

	// reset ring buf. make all slots 0xff. should be 1000 cycles or so.
	memset ( ringBuf, 0xff, sizeof(ringBuf) );

	logTrace(g_conf.m_logTracePosdb, "BEGIN");



	// now to speed up 'time enough for love' query which does not
	// have many super high scoring guys on top we need a more restrictive
	// filter than getMaxPossibleScore() so let's pick one query term,
	// the one with the shortest termlist, and see how close it gets to
	// each of the other query terms. then score each of those pairs.
	// so quickly record the word positions of each query term into
	// a ring buffer of 4096 slots where each slot contains the
	// query term info #.

	logTrace(g_conf.m_logTracePosdb, "Ring buffer generation");
	
	int32_t ourFirstPos = setRingbufFromQTI(&(m_queryTermInfos[m_minTermListIdx]), m_minTermListIdx, ringBuf);
	
	// now get query term closest to query term # m_minTermListIdx which
	// is the query term # with the shortest termlist
	// get closest term to m_minTermListIdx and the distance
	logTrace(g_conf.m_logTracePosdb, "Ring buffer generation 2");
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		if ( i == m_minTermListIdx ) {
			continue;
		}
		
		// get the query term info
		const QueryTermInfo *qti = &(m_queryTermInfos[i]);

		// if we have a negative term, skip it
		if ( qti->m_subList[0].m_bigramFlag & (BF_NEGATIVE) ) {
			continue;
		}

		// store all his word positions into ring buffer AS WELL
		setRingbufFromQTI(qti, i, ringBuf);

		// reset
		int32_t ourLastPos = -1;
		int32_t hisLastPos = -1;
		int32_t bestDist = 0x7fffffff;
		// how far is this guy from the man?
		for ( int32_t x = 0 ; x < (int32_t)RINGBUFSIZE ; ) {
			// skip next 4 slots if all empty. fast?
			if (*(uint32_t *)(ringBuf+x) == 0xffffffff) {
				x+=4;
				continue;
			}

			// skip if nobody
			if ( ringBuf[x] == 0xff ) { 
				x++; 
				continue; 
			}

			// get query term #
			unsigned char qt = ringBuf[x];

			// if it's the man
			if ( qt == m_minTermListIdx ) {
				// record
				hisLastPos = x;
				// skip if we are not there yet
				if ( ourLastPos == -1 ) { 
					x++; 
					continue; 
				}

				// try distance fix
				if ( x - ourLastPos < bestDist ) {
					bestDist = x - ourLastPos;
				}
			}
			// if us
			else 
			if ( qt == i ) {
				// record
				ourLastPos = x;
				// skip if he's not recorded yet
				if ( hisLastPos == -1 ) { 
					x++; 
					continue; 
				}

				// check dist
				if ( x - hisLastPos < bestDist ) {
					bestDist = x - hisLastPos;
				}
			}

			x++;
		}

		// compare last occurence of query term #x with our first occ.
		// since this is a RING buffer
		int32_t wrapDist = ourFirstPos + ((int32_t)RINGBUFSIZE-hisLastPos);
		if ( wrapDist < bestDist ) {
			bestDist = wrapDist;
		}

		// query distance
		int32_t qdist = m_qpos[m_minTermListIdx] - m_qpos[i];
		// compute it
		float maxScore2 = getMaxPossibleScore(&(m_queryTermInfos[i]));
		maxScore2 = modifyMaxScoreByDistance(maxScore2,
						     bestDist,
						     qdist,
						     &(m_queryTermInfos[m_minTermListIdx]));
		// -1 means it has inlink text so do not apply this constraint
		// to this docid because it is too difficult because we
		// sum up the inlink text
		if ( maxScore2 < 0.0 ) {
			continue;
		}

		// if any one of these terms have a max score below the
		// worst score of the 10th result, then it can not win.
		// @todo: BR. Really? ANY of them?
		if ( maxScore2 <= minWinningScore ) {
			logTrace(g_conf.m_logTracePosdb, "END - docid score too low");
			return false;
		}
	}

	logTrace(g_conf.m_logTracePosdb, "END - docid score high enough");
	return true;
}



//
// Data for the current DocID found in sublists of each query term
// is merged into a single list, so we end up with one list per query 
// term. 
//
void PosdbTable::mergeTermSubListsForDocId(MiniMergeBuffer *miniMergeBuffer, int *highestInlinkSiteRank) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN.");
	char *miniMergeBuf = miniMergeBuffer->buffer;
	char *miniMergeBufEnd = miniMergeBuffer->buffer+sizeof(miniMergeBuffer->buffer);

	// we got a docid that has all the query terms, so merge
	// each term's sublists into a single list just for this docid.
	//
	// all posdb keys for this docid should fit in here, the 
	// mini merge buf:
	char *mptr 	= miniMergeBuf;
	char *miniMergeBufSafeEnd = miniMergeBufEnd - 1000; //fragile hack but no worse than the original code
	char *lastMptr = NULL;

	// Merge each set of sublists, like we merge a term's list with 
	// its two associated bigram lists, if there, the left bigram and 
	// right bigram list. Merge all the synonym lists for that term 
	// together as well, so if the term is 'run' we merge it with the 
	// lists for 'running' 'ran' etc.
	logTrace(g_conf.m_logTracePosdb, "Merge sublists into a single list per query term");
	for ( int32_t j = 0 ; j < m_numQueryTermInfos ; j++ ) {
		// get the query term info
		QueryTermInfo *qti = &(m_queryTermInfos[j]);

		// just use the flags from first term i guess
		// NO! this loses the wikihalfstopbigram bit! so we gotta
		// add that in for the key i guess the same way we add in
		// the syn bits below!!!!!
		m_bflags [j] = qti->m_numSubLists>0 ? qti->m_subList[0].m_bigramFlag : 0;
		// if we have a negative term, skip it
		if ( qti->m_numSubLists>0 && qti->m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
			// need to make this NULL for getSiteRank() call below
			miniMergeBuffer->mergedListStart[j] = NULL;
			// if its empty, that's good!
			continue;
		}

		// the merged list for term #j is here:
		miniMergeBuffer->mergedListStart[j] = mptr;
		bool isFirstKey = true;

		const char *nwp[MAX_SUBLISTS];
		const char *nwpEnd[MAX_SUBLISTS];
		char  nwpFlags[MAX_SUBLISTS];
		int baseSubListIndex[MAX_SUBLISTS];
		// populate the nwp[] arrays for merging
		int32_t nsub = 0;
		for ( int32_t k = 0 ; k < qti->m_numMatchingSubLists ; k++ ) {
			baseSubListIndex[nsub] = k;
			// NULL means does not have that docid
			if ( ! qti->m_matchingSublist[k].m_savedCursor ) {
				continue;
			}

			// getMaxPossibleScore() incremented m_matchingSubListCursor to
			// the next docid so use m_matchingSubListSavedCursor.
			nwp[nsub] 	= qti->m_matchingSublist[k].m_savedCursor;
			nwpEnd[nsub]	= qti->m_matchingSublist[k].m_cursor;
			nwpFlags[nsub]	= qti->m_subList[k].m_bigramFlag;
			nsub++;
		}

		// if only one sublist had this docid, no need to merge
		// UNLESS it's a synonym list then we gotta set the
		// synbits on it, below!!! or a half stop wiki bigram like
		// the term "enough for" in the wiki phrase 
		// "time enough for love" because we wanna reward that more!
		// this halfstopwikibigram bit is set in the indivial keys
		// so we'd have to at least do a key cleansing, so we can't
		// do this shortcut right now... mdw oct 10 2015
		if ( nsub == 1 && 
		     (nwpFlags[0] & BF_NUMBER) &&
		     !(nwpFlags[0] & BF_SYNONYM) &&
		     !(nwpFlags[0] & BF_HALFSTOPWIKIBIGRAM) ) {
			miniMergeBuffer->mergedListStart[j] = nwp     [0];
			miniMergeBuffer->mergedListEnd[j]   = nwpEnd  [0];
			m_bflags[j]			= nwpFlags[0];
			continue;
		}

		// Merge the lists into a list in miniMergeBuf.
		// Get the min of each list
		while( mptr < miniMergeBufSafeEnd ) {
			int32_t mink = -1;

			for ( int32_t k = 0 ; k < nsub ; k++ ) {
				// skip if list is exhausted
				if ( ! nwp[k] ) {
					continue;
				}

				// auto winner?
				if ( mink == -1 ) {
					mink = k;
					continue;
				}

				if ( KEYCMP(nwp[k], nwp[mink], 6) < 0 ) {
					mink = k; // a new min...
				}
			}

			// all exhausted? merge next set of sublists then for term #j
			if ( mink == -1 ) {
				// continue outer "j < m_numQueryTermInfos" loop.
				break;
			}

			// get keysize
			char ks = Posdb::getKeySize(nwp[mink]);

			// if the first key in our merged list store the docid crap
			if ( isFirstKey ) {
				// store a 12 byte key in the merged list buffer
				memcpy ( mptr, nwp[mink], 12 );
				int termIndex = qti->m_subList[baseSubListIndex[mink]].m_qt - m_q->m_qterms;
				*(miniMergeBuffer->getTermIndexPtrForBufferPos(mptr)) = termIndex;

				// Detect highest siterank of inlinkers
				if ( Posdb::getHashGroup(mptr) == HASHGROUP_INLINKTEXT) {
					char inlinkerSiteRank = Posdb::getWordSpamRank(mptr);
					if(inlinkerSiteRank > *highestInlinkSiteRank) {
						*highestInlinkSiteRank = inlinkerSiteRank;
					}
				}

				// wipe out its syn bits and re-use our way
				mptr[2] &= 0xfc;
				// set the synbit so we know if its a synonym of term
				if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) {
					mptr[2] |= 0x02;
				}

				// wiki half stop bigram? so for the query
				// 'time enough for love' the phrase term "enough for"
				// is a half stopword wiki bigram, because it is in
				// a phrase in wikipedia ("time enough for love") and
				// one of the two words in the phrase term is a
				// stop word. therefore we give it more weight than
				// just 'enough' by itself.
				if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM ) {
					mptr[2] |= 0x01;
				}

				// make sure its 12 bytes! it might have been
				// the first key for the termid, and 18 bytes.
				mptr[0] &= 0xf9;
				mptr[0] |= 0x02;
				// save it
				lastMptr = mptr;
				mptr += 12;
				isFirstKey = false;
			}
			else {
				// Old, dubious comment: 2dont add if same keypos as previous item ..."
				// Relied on the matchSubList to be in the order: left-bigram, right-bigram, term, synonyms...
				// But setQueryTermInfo() has had a bug there for as long as we know and
				// multiple entries with same position seems to have no harmful effect.
				memcpy ( mptr, nwp[mink], 6 );
				int termIndex = qti->m_subList[qti->m_matchingSublist[mink].m_baseSubListIndex].m_qt - m_q->m_qterms;
				*(miniMergeBuffer->getTermIndexPtrForBufferPos(mptr)) = termIndex;
				
				// Detect highest siterank of inlinkers
				if ( Posdb::getHashGroup(mptr) == HASHGROUP_INLINKTEXT) {
					char inlinkerSiteRank = Posdb::getWordSpamRank(mptr);
					if(inlinkerSiteRank > *highestInlinkSiteRank) {
						*highestInlinkSiteRank = inlinkerSiteRank;
					}
				}
				
				// wipe out its syn bits and re-use our way
				mptr[2] &= 0xfc;
				// set the synbit so we know if its a synonym of term
				if ( nwpFlags[mink] & (BF_BIGRAM|BF_SYNONYM)) {
					mptr[2] |= 0x02;
				}
				
				if ( nwpFlags[mink] & BF_HALFSTOPWIKIBIGRAM ) {
					mptr[2] |= 0x01;
				}
				
				// if it was the first key of its list it may not
				// have its bit set for being 6 bytes now! so turn
				// on the 2 compression bits
				mptr[0] &= 0xf9;
				mptr[0] |= 0x06;
				// save it
				lastMptr = mptr;
				mptr += 6;
			}

			// advance the cursor over the key we used.
			nwp[mink] += ks; // Posdb::getKeySize(nwp[mink]);

			// exhausted?
			if ( nwp[mink] >= nwpEnd[mink] ) {
				nwp[mink] = NULL;
			}
		}

		// wrap it up here since done merging
		miniMergeBuffer->mergedListEnd[j] = mptr;		
		//log(LOG_ERROR,"%s:%d: j=%" PRId32 ": miniMergedListStart[%" PRId32 "]=%p, miniMergedListEnd[%" PRId32 "]=%p, mptr=%p, miniMergeBufEnd=%p, term=[%.*s] - TERM DONE", __func__, __LINE__, j, j, miniMergeBuffer->mergedListStart[j], j, miniMergeBuffer->mergedListEnd[j], mptr, miniMergeBufEnd, qti->m_qt->m_termLen, qti->m_qt->m_term);
	}

	// breach?
	if ( mptr > miniMergeBufEnd ) {
		log(LOG_ERROR,"%s:%s:%d: miniMergeBuf=%p miniMergeBufEnd=%p mptr=%p", __FILE__, __func__, __LINE__, miniMergeBuf, miniMergeBufEnd, mptr);
		gbshutdownAbort(true);
	}

#ifdef _VALGRIND_
	VALGRIND_MAKE_MEM_UNDEFINED(mptr, miniMergeBufEnd-mptr);
#endif


	// Make sure miniMergeList[x] is NULL if it contains no values
	for(int32_t i=0; i < m_numQueryTermInfos; i++) {
		// skip if not part of score
		if ( m_bflags[i] & (BF_PIPED|BF_NEGATIVE) ) {
			continue;
		}

		// get list
		const char *plist    = miniMergeBuffer->mergedListStart[i];
		const char *plistEnd = miniMergeBuffer->mergedListEnd[i];
		int32_t  psize		= plistEnd - plist;
		
		if( !psize ) {
			// BR fix 20160918: Avoid working on empty lists where start and 
			// end pointers are the same. This can happen if no positions are
			// copied to the merged list because they are all synonyms or
			// phrase terms. See "HACK OF CONFUSION" above..
			miniMergeBuffer->mergedListStart[i] = NULL;
		}


		//
		// sanity check for all
		//
		// test it. first key is 12 bytes.
		if ( psize && Posdb::getKeySize(plist) != 12 ) {
			log(LOG_ERROR,"%s:%s:%d: psize=%" PRId32 "", __FILE__, __func__, __LINE__, psize);
			gbshutdownAbort(true);
		}

		// next key is 6
		if ( psize > 12 && Posdb::getKeySize(plist+12) != 6) {
			log(LOG_ERROR,"%s:%s:%d: next key size=%" PRId32 "", __FILE__, __func__, __LINE__, Posdb::getKeySize(plist+12));
			gbshutdownAbort(true);
		}
	}



	logTrace(g_conf.m_logTracePosdb, "END.");
}



//
// Nested for loops to score the term pairs.
//
// Store best scores into the scoreMatrix so the sliding window
// algorithm can use them from there to do sub-outs
//
void PosdbTable::createNonBodyTermPairScoreMatrix(const MiniMergeBuffer *miniMergeBuffer, PairScoreMatrix *scoreMatrix) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN");

	// scan over each query term (its synonyms are part of the
	// QueryTermInfo)
	for(int32_t i=0; i < m_numQueryTermInfos; i++) {
		// skip if not part of score
		if ( m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
			continue;
		}

		// and pair it with each other possible query term
		for ( int32_t j = i+1 ; j < m_numQueryTermInfos ; j++ ) {
			// skip if not part of score
			if ( m_bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
				continue;
			}

			// but if they are in the same wikipedia phrase
			// then try to keep their positions as in the query.
			// so for 'time enough for love' ideally we want
			// 'time' to be 6 units apart from 'love'
			int32_t qdist;
			float score;
			// zero means not in a phrase
			if ( m_wikiPhraseIds[j] == m_wikiPhraseIds[i] && m_wikiPhraseIds[j] ) {
				// . the distance between the terms in the query
				// . ideally we'd like this distance to be reflected
				//   in the matched terms in the body
				qdist = m_qpos[j] - m_qpos[i];
				// wiki weight
				score = (float)WIKI_WEIGHT; // .50;
			}
			else {
				// basically try to get query words as close
				// together as possible
				qdist = 2;
				// this should help fix
				// 'what is an unsecured loan' so we are more likely
				// to get the page that has that exact phrase in it.
				// yes, but hurts how to make a lock pick set.
				//qdist = qpos[j] - qpos[i];
				// wiki weight
				score = 1.0;
			}

			float maxnbtp;
			//
			// get score for term pair from non-body occuring terms
			//
			if ( miniMergeBuffer->mergedListStart[i] && miniMergeBuffer->mergedListStart[j] ) {
				maxnbtp = getMaxScoreForNonBodyTermPair(miniMergeBuffer, i, j, qdist);
			}
			else {
				maxnbtp = -1;
			}

			// it's -1 if one term is in the body/header/menu/etc.
			if ( maxnbtp < 0 ) {
				score = -1.00;
			}
			else {
				score *= maxnbtp;
			}

			// store in matrix for "sub out" algo below
			// when doing sliding window
			scoreMatrix->set(j,i, score);
		}
	}
	logTrace(g_conf.m_logTracePosdb, "END");
}


//
// Finds the highest single term score sum.
// Creates array of highest scoring non-body positions
//
float PosdbTable::getMinSingleTermScoreSum(const MiniMergeBuffer *miniMergeBuffer, const char **highestScoringNonBodyPos, DocIdScore *pdcs) {
	float minSingleScore = 999999999.0;
	bool mergedListFound = false;
	bool allSpecialTerms = true;
	bool scoredTerm = false;

	logTrace(g_conf.m_logTracePosdb, "BEGIN");

	// Now add single word scores.
	//
	// Having inlink text from siterank 15 of max 
	// diversity/density will result in the largest score, 
	// but we add them all up...
	//
	// This should be highly negative if singles[i] has a '-' 
	// termsign...


	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		if ( ! miniMergeBuffer->mergedListStart[i] ) {
			continue;
		}
		mergedListFound = true;

		// skip if to the left of a pipe operator
		if( m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
			continue;
		}

		allSpecialTerms = false;

		// sometimes there is no wordpos subtermlist for this docid
		// because it just has the bigram, like "streetlight" and not
		// the word "light" by itself for the query 'street light'
		//if ( miniMergeBuffer->mergedListStart[i] ) {
		// assume all word positions are in body
		//highestScoringNonBodyPos[i] = NULL;

		// This scans all word positions for this term.
		//
		// This should ignore occurences in the body and only
		// focus on inlink text, etc.
		//
		// Sets "highestScoringNonBodyPos" to point to the winning word 
		// position which does NOT occur in the body.
		//
		// Adds up MAX_TOP top scores and returns that sum.
		//
		// pdcs is NULL if not currPassNum == INTERSECT_DEBUG_INFO
		float sts = getBestScoreSumForSingleTerm(miniMergeBuffer, i, pdcs, &highestScoringNonBodyPos[i]);
		scoredTerm = true;
		//logTrace(g_conf.m_logTracePosdb, "i=%d sts=%f", i,sts);

		// sanity check
		if ( highestScoringNonBodyPos[i] && s_inBody[Posdb::getHashGroup(highestScoringNonBodyPos[i])] ) {
			gbshutdownAbort(true);
		}

		//sts /= 3.0;
		if ( sts < minSingleScore ) {
			minSingleScore = sts;
		}
	}

	if( !mergedListFound || (!scoredTerm && !allSpecialTerms) ) {
		// Fix default value if no single terms were scored, and all terms are not special (e.g. numbers).
		// This returns -1 for documents matching bigrams only, and not single terms. Can happen when searching
		// for "bridget jones" and a document has the text "bridgetjon es" as the only match (bigram).
		//
		// If terms are numbers, do NOT return -1, otherwise gbsortbyint queries do not work.
		minSingleScore = -1;
	}

	logTrace(g_conf.m_logTracePosdb, "END. minSingleScore=%f", minSingleScore);
	return minSingleScore;
}



// Like getTermPairScore, but uses the word positions currently pointed to by ptrs[i].
// Does NOT scan the word position lists.
// Also tries to sub-out each term with the title or linktext wordpos term
//   pointed to by "highestScoringNonBodyPos[i]".
//
// OUTPUT:
//   m_bestMinTermPairWindowScore: The best minimum window score
//   m_bestMinTermPairWindowPtrs : Pointers to query term positions giving the best minimum score
//
void PosdbTable::findMinTermPairScoreInWindow(const MiniMergeBuffer *miniMergeBuffer, const char **ptrs, std::vector<const char *> *bestMinTermPairWindowPtrs, const char **highestScoringNonBodyPos, const PairScoreMatrix &scoreMatrix) {
	float minTermPairScoreInWindow = 999999999.0;
	bool mergedListFound = false;
	bool allSpecialTerms = true;
	bool scoredTerms = false;

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// TODO: only do this loop on the (i,j) pairs where i or j
	// is the term whose position got advanced in the sliding window.

	for ( int32_t i = 0 ; i < m_numQueryTermInfos; i++ ) {
		// skip if to the left of a pipe operator
		if ( m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
			continue;
		}
		allSpecialTerms = false;

		// skip empty list
		if( !ptrs[i] ) {
			continue;
		}
		mergedListFound = true;

		//if ( ptrs[i] ) wpi = ptrs[i];
		// if term does not occur in body, sub-in the best term
		// from the title/linktext/etc.
		//else           wpi = highestScoringNonBodyPos[i];

		const char *wpi = ptrs[i];

		// loop over other terms
		for(int32_t j = i + 1; j < m_numQueryTermInfos; j++) {
			// skip if to the left of a pipe operator
			if ( m_bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
				continue;
			}

			// skip empty list
			if( !ptrs[j] ) {
				continue;
			}


			// TODO: use a cache using wpi/wpj as the key.
			//if ( ptrs[j] ) wpj = ptrs[j];
			// if term does not occur in body, sub-in the best term
			// from the title/linktext/etc.
			//else wpj = highestScoringNonBodyPos[j];

			const char *wpj = ptrs[j];
			int32_t qdist;
			float wikiWeight;

			// in same wikipedia phrase?
			if ( m_wikiPhraseIds[j] == m_wikiPhraseIds[i] &&
			     // zero means not in a phrase
			     m_wikiPhraseIds[j] ) {
				// try to get dist that matches qdist exactly
				qdist = m_qpos[j] - m_qpos[i];
				// wiki weight
				wikiWeight = WIKI_WEIGHT; // .50;
			}
			else {
				// basically try to get query words as close
				// together as possible
				qdist = 2;
				// fix 'what is an unsecured loan' to get the
				// exact phrase with higher score
				//m_qdist = m_qpos[j] - m_qpos[i];
				// wiki weight
				wikiWeight = 1.0;
			}

			// this will be -1 if wpi or wpj is NULL
			float max = getScoreForTermPair(miniMergeBuffer, wpi, wpj, 0, qdist);
			scoredTerms = true;

			// try sub-ing in the best title occurence or best
			// inlink text occurence. cuz if the term is in the title
			// but these two terms are really far apart, we should
			// get a better score
			float score = getScoreForTermPair(miniMergeBuffer, highestScoringNonBodyPos[i], wpj, FIXED_DISTANCE, qdist);
			if ( score > max ) {
				max   = score;
			}

			// a double pair sub should be covered in the
			// getMaxScoreForNonBodyTermPair() function
			score = getScoreForTermPair(miniMergeBuffer, highestScoringNonBodyPos[i], highestScoringNonBodyPos[j], FIXED_DISTANCE, qdist);
			if ( score > max ) {
				max = score;
			}

			score = getScoreForTermPair(miniMergeBuffer, wpi, highestScoringNonBodyPos[j], FIXED_DISTANCE, qdist);
			if ( score > max ) {
				max = score;
			}

			// wikipedia phrase weight
			if ( !almostEqualFloat(wikiWeight, 1.0) ) {
				max *= wikiWeight;
			}

			// term freqweight here
			const int queryTermIndex1 = miniMergeBuffer->getTermIndexForBufferPos(wpi);
			const float termFreqWeight1 = m_q->m_qterms[queryTermIndex1].m_termFreqWeight;
			max *= termFreqWeight1;
			const int queryTermIndex2 = miniMergeBuffer->getTermIndexForBufferPos(wpj);
			const float termFreqWeight2 = m_q->m_qterms[queryTermIndex2].m_termFreqWeight;
			max *= termFreqWeight2;
			//TODO: shouldn't we multiply with userweight here too?

			// use score from scoreMatrix if bigger
			if ( scoreMatrix.get(j,i) > max ) {
				max = scoreMatrix.get(j,i);
			}


			// in same quoted phrase?
			if ( m_quotedStartIds[j] >= 0 && m_quotedStartIds[j] == m_quotedStartIds[i] ) {
				// no subouts allowed i guess
				if( !wpi ) {
					max = -1.0;
				}
				else 
				if( !wpj ) {
					max = -1.0;
				}
				else {
					int32_t qdist = m_qpos[j] - m_qpos[i];
					int32_t p1 = Posdb::getWordPos ( wpi );
					int32_t p2 = Posdb::getWordPos ( wpj );
					int32_t  dist = p2 - p1;

					// must be in right order!
					if(dist < 0) {
						max = -1.0;
					}
					// allow for a discrepancy of 1 unit in case
					// there is a comma? i think we add an extra
					// unit
					else 
					if(dist > qdist && dist - qdist > 1) {
						max = -1.0;
						//log("ddd1: i=%" PRId32" j=%" PRId32" "
						//    "dist=%" PRId32" qdist=%" PRId32,
						//    i,j,dist,qdist);
					}
					else 
					if(dist < qdist && qdist - dist > 1) {
						max = -1.0;
					}
				}
			}

			// now we want the sliding window with the largest min
			// term pair score!
			if(max < minTermPairScoreInWindow) {
				minTermPairScoreInWindow = max;
			}
		}
	}

	if( !mergedListFound || (!scoredTerms && !allSpecialTerms) ) {
		// Similar fix as in getMinSingleTermScoreSum, but should not happen in this function ...
		minTermPairScoreInWindow = -1;
	}

	logTrace(g_conf.m_logTracePosdb, "minTermPairScoreInWindow=%f, m_bestMinTermPairWindowScore=%f", minTermPairScoreInWindow, m_bestMinTermPairWindowScore);

	// Our best minimum score better than current best minimum score?
	if ( minTermPairScoreInWindow <= m_bestMinTermPairWindowScore ) {
		logTrace(g_conf.m_logTracePosdb, "END.");
		return;
	}

	// Yep, our best minimum score is the highest so far
	m_bestMinTermPairWindowScore = minTermPairScoreInWindow;

	// Record term positions in winning window
	for(int32_t i=0; i < m_numQueryTermInfos; i++) {
		if(ptrs[i]!=NULL)
			(*bestMinTermPairWindowPtrs)[i] = ptrs[i];
	}

	logTrace(g_conf.m_logTracePosdb, "END.");
}



// Find first entry that is a body entry (determined by 's_inBody' array).
// Returns NULL if none found
static const char *findFirstBodyPosdbEntry(const char *listStart, const char *listEnd) {
	for(const char *p = listStart; p<listEnd; ) {
		unsigned char hg = Posdb::getHashGroup(p);
		if(s_inBody[hg])
			return p;
		if(*p & 0x04)
			p += 6;
		else
			p += 12; //not really expected
	}
	return NULL;
}



float PosdbTable::getMinTermPairScoreSlidingWindow(const MiniMergeBuffer *miniMergeBuffer, const char **highestScoringNonBodyPos, std::vector<const char *> &bestMinTermPairWindowPtrs, const char **xpos, const PairScoreMatrix &scoreMatrix, DocIdScore *pdcs) {
	logTrace(g_conf.m_logTracePosdb, "Sliding Window algorithm begins");

	// Scan the terms that are in the body in a sliding window
	//
	// Compute the term pair score on just the terms in that
	// sliding window. that way, the term for a word like 'dog'
	// keeps the same word position when it is paired up with the
	// other terms.
	//
	// Compute the score the same way getTermPairScore() works so
	// we are on the same playing field
	//
	// Sub-out each term with its best scoring occurence in the title
	// or link text or meta tag, etc. but it gets a distance penalty
	// of like 100 units or so.
	//
	// If term does not occur in the body, the sub-out approach should
	// fix that.
	//
	// Keep a matrix of the best scores between two terms from the
	// above double nested loop algo, and replace that pair if we
	// got a better score in the sliding window.

	// use special ptrs for the windows so we do not mangle 
	// miniMergeBuffer->mergedListStart[] array because we use that below!

	//
	// init each list ptr to the first wordpos rec in the body
	// for THIS DOCID and if no such rec, make it NULL
	//
	bool allNull = true;
	for(int i = 0; i < m_numQueryTermInfos; i++) {
		if(m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER)) {
			// not a ranking term
			xpos[i] = NULL;
		} else if(miniMergeBuffer->mergedListStart[i]==NULL) {
			//no list
			xpos[i] = NULL;
		} else {
			xpos[i] = findFirstBodyPosdbEntry(miniMergeBuffer->mergedListStart[i], miniMergeBuffer->mergedListEnd[i]);
			if(xpos[i]) {
				allNull = false; //ok, found one entry in body
			} else if(!highestScoringNonBodyPos[i]) {
				// not in nody, not in title?
				gbshutdownLogicError();
			}
		}
	}


	// if no terms in body, no need to do sliding window
	bool doneSliding = allNull ? true : false;

	logTrace(g_conf.m_logTracePosdb, "Run sliding window algo? %s", !doneSliding?"yes":"no, no matches found in body");

	while( !doneSliding ) {
		//
		// Now all xpos point to positions in the document body. Calc the "window" score (score
		// for current term positions).
		//
		// If window score beats m_bestMinTermPairWindowScore we store the term xpos pointers
		// that define this window in the bestMinTermPairWindowPtrs[] array.
		//
		// Will try to substitute either of the two term positions with highestScoringNonBodyPos[i] 
		// if better, but will fix the distance to FIXED_DISTANCE to give a distance penalty.
		//
		// "scoreMatrix" contains the highest scoring non-body term pair score, which will 
		// be used if higher than the calculated score for the terms.
		//
		// Sets m_bestMinTermPairWindowScore and bestMinTermPairWindowPtrs if this window score beats it.
		//
		findMinTermPairScoreInWindow(miniMergeBuffer, xpos, &bestMinTermPairWindowPtrs, highestScoringNonBodyPos, scoreMatrix);

	 	bool advanceMin;

	 	do { 
			advanceMin = false;
			
			//
			// Find the minimum word position in the document for ANY of the query terms.
			// minPosTermIdx will contain the term index, minPos the position.
			//
			int32_t minPosTermIdx = -1;
			int32_t minPos = 0;
			for ( int32_t x = 0 ; x < m_numQueryTermInfos ; x++ ) {
				// skip if to the left of a pipe operator
				// and numeric posdb termlists do not have word positions,
				// they store a float there.
				if ( m_bflags[x] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
					continue;
				}

				if ( ! xpos[x] ) {
					continue;
				}

				if ( xpos[x] && minPosTermIdx == -1 ) {
					minPosTermIdx = x;
					//minRec = xpos[x];
					minPos = Posdb::getWordPos(xpos[x]);
					continue;
				}

				if ( Posdb::getWordPos(xpos[x]) >= minPos ) {
					continue;
				}

				minPosTermIdx = x;
				//minRec = xpos[x];
				minPos = Posdb::getWordPos(xpos[x]);
			}

			// sanity
			if ( minPosTermIdx < 0 ) {
				gbshutdownAbort(true);
			}

		 	do { 
		 		//
		 		// Advance the list pointer of the list containing the current
		 		// minimum position (minPosTermIdx). If no more positions, set list to NULL.
		 		// If all lists are NULL, we are done sliding.
		 		//
				if ( ! (xpos[minPosTermIdx][0] & 0x04) ) {
					xpos[minPosTermIdx] += 12;
				}
				else {
					xpos[minPosTermIdx] +=  6;
				}

				// NULLify list if no more positions for this docid for that term.
				if ( xpos[minPosTermIdx] >= miniMergeBuffer->mergedListEnd[minPosTermIdx] || ! (xpos[minPosTermIdx][0] & 0x04) ) {
					// exhausted list now
					xpos[minPosTermIdx] = NULL;

					// are all null now?
					int32_t k; 
					for ( k = 0 ; k < m_numQueryTermInfos ; k++ ) {
						// skip if to the left of a pipe operator
						if( m_bflags[k] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER)) {
							continue;
						}

						if ( xpos[k] ) {
							break;
						}
					}

					// all lists are now exhausted
					if ( k >= m_numQueryTermInfos ) {
						doneSliding = true;
					}

					// No more positions in current term list. Find new term list with lowest position.
					advanceMin = true;
				}
				// if current term position is not in the document body, advance the pointer and look again.
			} while( !advanceMin && !doneSliding && !s_inBody[Posdb::getHashGroup(xpos[minPosTermIdx])] );

			// if current list is exhausted, find new term list with lowest position.
		} while( advanceMin && !doneSliding );

	} // end of while( !doneSliding )


	float minPairScore = -1.0;
	logTrace(g_conf.m_logTracePosdb, "Zak algorithm begins");

	for(int32_t i=0; i < m_numQueryTermInfos; i++) {
		// skip if to the left of a pipe operator
		if ( m_bflags[i] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
			continue;
		}

		if ( ! miniMergeBuffer->mergedListStart[i] ) {
			continue;
		}


		for ( int32_t j = i+1 ; j < m_numQueryTermInfos ; j++ ) {
			// skip if to the left of a pipe operator
			if ( m_bflags[j] & (BF_PIPED|BF_NEGATIVE|BF_NUMBER) ) {
				continue;
			}

			if ( ! miniMergeBuffer->mergedListStart[j] ) {
				continue;
			}
			// . this limits its scoring to the winning sliding window
			//   as far as the in-body terms are concerned
			// . it will do sub-outs using the score matrix
			// . this will skip over body terms that are not 
			//   in the winning window defined by bestMinTermPairWindowPtrs[]
			//   that we set in findMinTermPairScoreInWindow()
			// . returns the best score for this term
			float tpscore = getTermPairScoreForAny(miniMergeBuffer, i, j, bestMinTermPairWindowPtrs, pdcs);

			// get min of all term pair scores
			if ( tpscore >= minPairScore && minPairScore >= 0.0 ) {
				continue;
			}

			// got a new min
			logTrace(g_conf.m_logTracePosdb, "i=%d, tpscore=%f", i, tpscore);
			minPairScore = tpscore;
		}
	}

	logTrace(g_conf.m_logTracePosdb, "Zak algorithm ends. minPairScore=%f", minPairScore);
	return minPairScore;
}



//simple wrapper around intersectLists_real() just for transforming std::bad_alloca exceptions in ENOMEM
void PosdbTable::intersectLists() {
	try {
		intersectLists_real();
	} catch(std::bad_alloc&) {
		log(LOG_ERROR,"posdb: caught std::bad_alloc - out of memory");
		if(g_errno==0)
			g_errno = ENOMEM;
	}
}


// . compare the output of this to intersectLists9_r()
// . hopefully this will be easier to understand and faster
// . IDEAS:
//   we could also note that if a term was not in the title or
//   inlink text it could never beat the 10th score.
void PosdbTable::intersectLists_real() {
	logTrace(g_conf.m_logTracePosdb, "BEGIN. numTerms: %" PRId32, m_q->m_numTerms);

	if(m_topTree->getNumNodes()==0 && !allocateTopTree()) {
		logTrace(g_conf.m_logTracePosdb, "END. could not allocate toptree");
		g_errno = ENOMEM;
		return;
	}
	if(m_topTree->getNumNodes()==0) {
		logTrace(g_conf.m_logTracePosdb, "END. toptree has zero size");
		return;
	}

	if(!allocateScoringInfo()) {
		logTrace(g_conf.m_logTracePosdb, "END. could not allocate scoring info");
		g_errno = ENOMEM;
		return;
	}

	if(!allocWhiteListTable()) {
		logTrace(g_conf.m_logTracePosdb, "END. could not allocate whitelist table");
		return;
	}
	prepareWhiteListTable();

	if(!setQueryTermInfo()) {
		logTrace(g_conf.m_logTracePosdb, "END. could not allocate query term info");
		return;
	}

	initWeights();
	// assume no-op
	m_t1 = 0LL;

	// set start time
	int64_t t1 = gettimeofdayInMilliseconds();

	// assume we return early
	m_addListsTime = 0;


	if( !findCandidateDocIds() ) {
		logTrace(g_conf.m_logTracePosdb, "END. Found no candidate docids");
		return;
	}

	//
	// The vote buffer now contains the matching docids and each term sublist 
	// has been adjusted to only contain these docids as well. Let the fun begin.
	//


	//
	// TRANSFORM QueryTermInfo::m_* vars into old style arrays

	m_wikiPhraseIds.resize(m_numQueryTermInfos);
	m_quotedStartIds.resize(m_numQueryTermInfos);
	m_qpos.resize(m_numQueryTermInfos);
	m_qtermNums.resize(m_numQueryTermInfos);
	m_bflags.resize(m_numQueryTermInfos);
	std::vector<const char *> highestScoringNonBodyPos(m_numQueryTermInfos);
	std::vector<const char *> bestMinTermPairWindowPtrs(m_numQueryTermInfos);
	std::vector<const char *> xpos(m_numQueryTermInfos);
	PairScoreMatrix           scoreMatrix(m_numQueryTermInfos);
	
	int64_t lastTime = gettimeofdayInMilliseconds();
	int64_t now;
	int64_t took;
	int32_t phase = 3; // 2 first in findCandidateDocIds


	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
		// get it
		QueryTermInfo &qti = m_queryTermInfos[i];
		// set it
		m_wikiPhraseIds [i] = qti.m_wikiPhraseId;
		m_quotedStartIds[i] = qti.m_quotedStartId;
		// query term position
		m_qpos          [i] = qti.m_qpos;
		m_qtermNums     [i] = qti.m_qtermNum;
	}


	//////////
	//
	// OLD MAIN INTERSECTION LOGIC
	//
	/////////

	DocIdScore dcs;
	DocIdScore *pdcs = NULL;
	uint64_t lastDocId = 0LL;
	int32_t lastLen = 0;
	char siteRank;
	int highestInlinkSiteRank;
	char docLang;
	float score;
	int32_t intScore = 0;
	float minScore = 0.0;
	float minSingleScore;
	// scan the posdb keys in the smallest list
	// raised from 200 to 300,000 for 'da da da' query
	MiniMergeBuffer miniMergeBuf(m_numQueryTermInfos);
	const char *docIdPtr;
	char *docIdEnd = m_docIdVoteBuf.getBufStart()+m_docIdVoteBuf.length();
	float minWinningScore = -1.0;
	int32_t topCursor = -9;
	int32_t numProcessed = 0;
	int32_t prefiltMaxPossScoreFail 		= 0;
	int32_t prefiltMaxPossScorePass 		= 0;
	int32_t prefiltBestDistMaxPossScoreFail = 0;
	int32_t prefiltBestDistMaxPossScorePass	= 0;


	// populate the cursors for each sublist

	int32_t numQueryTermsToHandle = m_numQueryTermInfos;
	if ( ! m_msg39req->m_doMaxScoreAlgo ) {
		numQueryTermsToHandle = 0;
	}
	// do not do it if we got a gbsortby: field
	if ( m_sortByTermNum >= 0 ) {
		numQueryTermsToHandle = 0;
	}
	if ( m_sortByTermNumInt >= 0 ) {
		numQueryTermsToHandle = 0;
	}


 	//
 	// Run through the scoring logic once or twice. Two passes needed ONLY if we 
 	// need to create additional (debug) scoring info for visual display.
 	//
 	int numPassesNeeded = m_msg39req->m_getDocIdScoringInfo ? 2 : 1;

	for(int currPassNum=0; currPassNum < numPassesNeeded; currPassNum++) {
		//
		// Pass 0: Find candidate docids and calculate scores
		// Pass 1: Only for creating debug scoring info for visual display
		//
		switch( currPassNum ) {
			case INTERSECT_SCORING:
				logTrace(g_conf.m_logTracePosdb, "Loop 0: Real scoring");
				break;
			case INTERSECT_DEBUG_INFO:
				logTrace(g_conf.m_logTracePosdb, "Loop 1: Data for visual scoring info");
				removeScoreInfoForDeletedDocIds();
				break;
			default:
				log(LOG_LOGIC,"%s:%d: Illegal pass number %d", __FILE__, __LINE__, currPassNum);
				gbshutdownLogicError();
		}

		// reset docid to start!
		docIdPtr = m_docIdVoteBuf.getBufStart();

		if( currPassNum == INTERSECT_DEBUG_INFO ) {
			//
			// reset QueryTermInfo::m_matchingSubListCursor[] to point to start of 
			// term lists for second pass that gets printable scoring info
			//
			for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {
				// get it
				QueryTermInfo &qti = m_queryTermInfos[i];
				// skip negative termlists
				if ( qti.m_numSubLists>0 && qti.m_subList[0].m_bigramFlag & BF_NEGATIVE ) {
					continue;
				}
				
				// do each sublist
				for ( int32_t j = 0 ; j < qti.m_numMatchingSubLists ; j++ ) {
					qti.m_matchingSublist[j].m_cursor      = qti.m_matchingSublist[j].m_start;
					qti.m_matchingSublist[j].m_savedCursor = qti.m_matchingSublist[j].m_start;
				}
			}
		}


		
		//#
		//# MAIN LOOP for looping over each docid
		//#

		bool allDone = false;
		while( !allDone && docIdPtr < docIdEnd ) {
//			logTrace(g_conf.m_logTracePosdb, "Handling next docId");

			bool skipToNextDocId = false;
			siteRank				= 0;
			docLang					= langUnknown;
			highestInlinkSiteRank 	= -1;
			bool docInThisFile;

			if ( currPassNum == INTERSECT_SCORING ) {
				m_docId = *(uint32_t *)(docIdPtr+1);
				m_docId <<= 8;
				m_docId |= (unsigned char)docIdPtr[0];
				m_docId >>= 2;
				docInThisFile = m_documentIndexChecker->exists(m_docId);
			}
			else {
				//
				// second pass? for printing out transparency info.
				// genDebugScoreInfo1 sets m_docId from the top scorer tree
				//
				if( genDebugScoreInfo1(&numProcessed, &topCursor, &docInThisFile) ) {
					logTrace(g_conf.m_logTracePosdb, "Pass #%d for file %" PRId32 " done", currPassNum, m_documentIndexChecker->getFileNum());

					// returns true if no more docids to handle
					allDone = true;
					break;	// break out of docIdPtr < docIdEnd loop
				}
			}

			//bool docInThisFile = m_documentIndexChecker->exists(m_docId);
			logTrace(g_conf.m_logTracePosdb, "Handling next docId: %" PRId64 " - pass #%d - %sfound in this file (%" PRId32 ")", m_docId, currPassNum, docInThisFile?"":"SKIPPING, not ", m_documentIndexChecker->getFileNum());

			if(!docInThisFile) {
				// Only advance cursors in first pass
				if( currPassNum == INTERSECT_SCORING ) {
					if( !advanceTermListCursors(docIdPtr) ) {
						logTrace(g_conf.m_logTracePosdb, "END. advanceTermListCursors failed");
						return;
					}
					docIdPtr += 6;
				}

				continue;
			}

			//calculate complete score multiplier
			float completeScoreMultiplier = 1.0;
			unsigned flags = 0;
			if(g_d2fasm.lookupFlags(m_docId,&flags) && flags) {
				for(int i=0; i<26; i++) {
					if(flags&(1<<i))
						completeScoreMultiplier *= m_baseScoringParameters.m_flagScoreMultiplier[i];
				}
			}


			if( currPassNum == INTERSECT_SCORING ) {
				//
				// Pre-advance each termlist's cursor to skip to next docid.
				//
				if( !advanceTermListCursors(docIdPtr) ) {
					logTrace(g_conf.m_logTracePosdb, "END. advanceTermListCursors failed");
					return;
				}

				if( !m_q->m_isBoolean ) {
					//##
					//## PRE-FILTERS. Discard DocIDs that cannot meet the minimum required
					//## score, before entering the main scoring loop below
					//##

					//
					// TODO: consider skipping this pre-filter if it sucks, as it does
					// for 'time enough for love'. it might save time!
					//
					// Calculate maximum possible score for a document. If the max score
					// is lower than the current minimum winning score, give up already
					// now and skip to the next docid. 
					//
					// minWinningScore is the score of the lowest scoring doc in m_topTree IF
					// the topTree contains the number of docs requested. Otherwise it is -1
					// and the doc is inserted no matter the score.
					//
					// Only go through this if we actually have a minimum score to compare with ...
					// No need if minWinningScore is still -1.
					//
					if ( minWinningScore >= 0.0 ) {
						logTrace(g_conf.m_logTracePosdb, "Compute 'upper bound' for each query term");

						// If there's no way we can break into the winner's circle, give up!
						// This computes an upper bound for each query term
						for ( int32_t i = 0 ; i < numQueryTermsToHandle ; i++ ) {
							// skip negative termlists.
							if ( m_queryTermInfos[i].m_subList[0].m_bigramFlag & (BF_NEGATIVE) ) {
								continue;
							}

							// an upper bound on the score we could get
							float maxScore = getMaxPossibleScore(&(m_queryTermInfos[i]));
							// -1 means it has inlink text so do not apply this constraint
							// to this docid because it is too difficult because we
							// sum up the inlink text
							if ( maxScore < 0.0 ) {
								continue;
							}

							maxScore *= completeScoreMultiplier;
							// logTrace(g_conf.m_logTracePosdb, "maxScore=%f  minWinningScore=%f", maxScore, minWinningScore);
							// if any one of these terms have a max score below the
							// worst score of the 10th result, then it can not win.
							if ( maxScore <= minWinningScore ) {
								docIdPtr += 6;
								prefiltMaxPossScoreFail++;
								skipToNextDocId = true;
								break;	// break out of numQueryTermsToHandle loop
							}
						}
					}

					if( skipToNextDocId ) {
						// continue docIdPtr < docIdEnd loop
						logTrace(g_conf.m_logTracePosdb, "Max possible score for docId %" PRId64 " too low, skipping to next", m_docId);
						continue;
					}

					prefiltMaxPossScorePass++;

					if ( minWinningScore >= 0.0 && m_sortByTermNum < 0 && m_sortByTermNumInt < 0 ) {

						if( !prefilterMaxPossibleScoreByDistance(minWinningScore*completeScoreMultiplier) ) {
							docIdPtr += 6;
							prefiltBestDistMaxPossScoreFail++;
							skipToNextDocId = true;
						}
					} // not m_sortByTermNum or m_sortByTermNumInt

					if( skipToNextDocId ) {
						// Continue docIdPtr < docIdEnd loop
						logTrace(g_conf.m_logTracePosdb, "Max possible score by distance for docId %" PRId64 " too low, skipping to next", m_docId);
						continue;	
					}
					prefiltBestDistMaxPossScorePass++;
				} // !m_q->m_isBoolean
			}	// currPassNum == INTERSECT_SCORING

			if ( m_q->m_isBoolean ) {
				// add one point for each term matched in the bool query
				// this is really just for when the terms are from different
				// fields. if we have unfielded boolean terms we should
				// do proximity matching.
				int32_t slot = m_bt.getSlot ( &m_docId );
				if ( slot >= 0 ) {
					uint8_t *bv = (uint8_t *)m_bt.getValueFromSlot(slot);
					// then a score based on the # of terms that matched
					int16_t bitsOn = getNumBitsOnX ( bv, m_vecSize );
					// but store in hashtable now
					minScore = (float)bitsOn;
				}
				else {
					minScore = 1.0;
				}
			}



			//##
			//## PERFORMANCE HACK: ON-DEMAND MINI MERGES.
			//##
			//## Data for the current DocID found in sublists of each query term
			//## is merged into a single list, so we end up with one list per query 
			//## term. They are all stored in a single buffer (miniMergeBuf), which 
			//## the miniMerged* pointers point into..
			//##

			mergeTermSubListsForDocId(&miniMergeBuf, &highestInlinkSiteRank);

			// clear the counts on this DocIdScore class for this new docid
			pdcs = NULL;
			if ( currPassNum == INTERSECT_DEBUG_INFO ) {
				dcs.reset();
				pdcs = &dcs;
			}

			//##
			//## ACTUAL SCORING BEGINS
			//##

			if ( !m_q->m_isBoolean ) {
				// Used by the various scoring functions called below
				m_bestMinTermPairWindowScore	= -2.0;


				//#
				//# NON-BODY TERM PAIR SCORING LOOP
				//#
				createNonBodyTermPairScoreMatrix(&miniMergeBuf, &scoreMatrix);


				//#
				//# SINGLE TERM SCORE LOOP
				//#
				minSingleScore = getMinSingleTermScoreSum(&miniMergeBuf, &(highestScoringNonBodyPos[0]), pdcs);
				logTrace(g_conf.m_logTracePosdb, "minSingleScore=%f before multiplication for docId %" PRIu64 "", minSingleScore, m_docId);

				minSingleScore *= completeScoreMultiplier;

				//#
				//# DOCID / SITERANK DETECTION
				//#
				for(int32_t k=0; k < m_numQueryTermInfos; k++) {
					if ( ! miniMergeBuf.mergedListStart[k] ) {
						continue;
					}
					
					// siterank/langid is always 0 in numeric
					// termlists so they sort by their number correctly
					if ( m_queryTermInfos[k].m_subList[0].m_bigramFlag & (BF_NUMBER) ) {
						continue;
					}
					
					siteRank = Posdb::getSiteRank ( miniMergeBuf.mergedListStart[k] );
					docLang  = Posdb::getLangId   ( miniMergeBuf.mergedListStart[k] );
					break;
				}
				logTrace(g_conf.m_logTracePosdb, "Got siteRank %d and docLang %d", (int)siteRank, (int)docLang);

				//#
				//# SLIDING WINDOW SCORING ALGORITHM
				//#
				// After calling this, bestMinTermPairWindowPtrs will point to the
				// term positions set ("window") that has the highest minimum score. These
				// pointers are used when determining the minimum term pair score returned
				// by the function.
				float minPairScore = getMinTermPairScoreSlidingWindow(&miniMergeBuf, &(highestScoringNonBodyPos[0]), bestMinTermPairWindowPtrs, &(xpos[0]), scoreMatrix, pdcs);
				logTrace(g_conf.m_logTracePosdb, "minPairScore=%f before multiplication for docId %" PRIu64 "", minPairScore, m_docId);

				minPairScore *= completeScoreMultiplier;

				//#
				//# Find minimum score - either single term or term pair
				//#
				minScore = 999999999.0;
				// get a min score from all the term pairs
				if ( minPairScore < minScore && minPairScore >= 0.0 ) {
					minScore = minPairScore;
				}

				// if we only had one query term
				if ( minSingleScore < minScore ) {
					minScore = minSingleScore;
				}
				logTrace(g_conf.m_logTracePosdb, "minPairScore=%f, minScore=%f for docId %" PRIu64 "", minPairScore, minScore, m_docId);

				
				// No positive score? Then skip the doc
				if ( minScore <= 0.0 ) {

					if( currPassNum == INTERSECT_SCORING ) {
						// advance to next docid
						docIdPtr += 6;
					}

					logTrace(g_conf.m_logTracePosdb, "Skipping docid %" PRIu64 " - no positive score", m_docId);
					// Continue docid loop
					continue;
				}
			} // !m_q->m_isBoolean

			//#
			//# Calculate score and give boost based on siterank and highest inlinking siterank
			//#
			float adjustedSiteRank = siteRank;

			if( highestInlinkSiteRank > siteRank ) {
				//adjust effective siterank because a high-rank site linked to it. Don't adjust it too much though.
				adjustedSiteRank = siteRank + (highestInlinkSiteRank-siteRank) / 3.0;
				logTrace(g_conf.m_logTracePosdb, "Highest inlink siterank %d > siterank %d. Adjusting to %f for docId %" PRIu64 "", highestInlinkSiteRank, (int)siteRank, adjustedSiteRank, m_docId);
			}
			score = minScore * (adjustedSiteRank*m_baseScoringParameters.m_siteRankMultiplier+1.0);
			logTrace(g_conf.m_logTracePosdb, "Score %f for docId %" PRIu64 "", score, m_docId);

			//# 
			//# Give score boost if query and doc language is the same, 
			//# and optionally a different boost if the language of the
			//# page is unknown.
			//#
			//# Use "qlang" parm to set the language. i.e. "&qlang=fr"
			//#
			if ( m_msg39req->m_language != 0 ) {
				if( m_msg39req->m_language == docLang) {
					score *= (m_msg39req->m_baseScoringParameters.m_sameLangWeight);
					logTrace(g_conf.m_logTracePosdb, "Giving score a matching language boost of x%f: %f for docId %" PRIu64 "", m_msg39req->m_baseScoringParameters.m_sameLangWeight, score, m_docId);
				}
				else
				if( docLang == 0 ) {
					score *= (m_msg39req->m_baseScoringParameters.m_unknownLangWeight);
					logTrace(g_conf.m_logTracePosdb, "Giving score an unknown language boost of x%f: %f for docId %" PRIu64 "", m_msg39req->m_baseScoringParameters.m_unknownLangWeight, score, m_docId);
				}
			}

			double page_temperature = 0;
			bool use_page_temperature = false;
			float score_before_page_temp = score;

			if(m_baseScoringParameters.m_usePageTemperatureForRanking) {
				use_page_temperature = true;
				page_temperature = g_pageTemperatureRegistry.query_page_temperature(m_docId, m_baseScoringParameters.m_pageTemperatureWeightMin, m_baseScoringParameters.m_pageTemperatureWeightMax);
				score *= page_temperature;
				logTrace(g_conf.m_logTracePosdb, "Page temperature for docId %" PRIu64 " is %.14f, score %f -> %f", m_docId, page_temperature, score_before_page_temp, score);
			}


			//#
			//# Handle sortby int/float and minimum docid/score pairs
			//#

			if( m_sortByTermNum >= 0 || m_sortByTermNumInt >= 0 || m_hasMaxSerpScore ) {
				// assume filtered out
				if ( currPassNum == INTERSECT_SCORING ) {
					m_filtered++;
				}

				//
				// if we have a gbsortby:price term then score exclusively on that
				//
				if ( m_sortByTermNum >= 0 ) {
					// no term?
					if ( ! miniMergeBuf.mergedListStart[m_sortByTermInfoNum] ) {
						// advance to next docid
						if( currPassNum == INTERSECT_SCORING ) {
							docIdPtr += 6;
						}
						// Continue docIdPtr < docIdEnd loop
						continue;
					}

					score = Posdb::getFloat(miniMergeBuf.mergedListStart[m_sortByTermInfoNum]);
				}

				if ( m_sortByTermNumInt >= 0 ) {
					// no term?
					if ( ! miniMergeBuf.mergedListStart[m_sortByTermInfoNumInt] ) {
						// advance to next docid
						if( currPassNum == INTERSECT_SCORING ) {
							docIdPtr += 6;
						}
						// Continue docIdPtr < docIdEnd loop
						continue;
					}

					intScore = Posdb::getInt(miniMergeBuf.mergedListStart[m_sortByTermInfoNumInt]);
					// do this so hasMaxSerpScore below works, although
					// because of roundoff errors we might lose a docid
					// through the cracks in the widget.
					//score = (float)intScore;
				}


				// now we have a maxscore/maxdocid upper range so the widget
				// can append only new results to an older result set.
				if ( m_hasMaxSerpScore ) {
					bool skipToNext = false;
					// if dealing with an "int" score use the extra precision
					// of the double that m_maxSerpScore is!
					if ( m_sortByTermNumInt >= 0 ) {
						if ( intScore > (int32_t)m_msg39req->m_maxSerpScore ) {
							skipToNext = true;
						}
						else
						if ( intScore == (int32_t)m_msg39req->m_maxSerpScore && (int64_t)m_docId <= m_msg39req->m_minSerpDocId ) {
							skipToNext = true;
						}
					}
					else {
						if ( score > (float)m_msg39req->m_maxSerpScore ) {
							skipToNext = true;
						}
						else
						if ( almostEqualFloat(score, (float)m_msg39req->m_maxSerpScore) && (int64_t)m_docId <= m_msg39req->m_minSerpDocId ) {
							//@todo: Why is  m_msg39req->m_maxSerpScore double and score float?
							skipToNext = true;
						}
					}

					if( skipToNext ) {				
						// advance to next docid
						if( currPassNum == INTERSECT_SCORING ) {
							docIdPtr += 6;
						}
						// Continue docIdPtr < docIdEnd loop
						continue;
					}
				}

				// we did not get filtered out
				if ( currPassNum == INTERSECT_SCORING ) {
					m_filtered--;
				}
			}


			// We only come here if we actually made it into m_topTree - second round only loops through
			// TopTree entries.
			if ( currPassNum == INTERSECT_DEBUG_INFO ) {
				// NEW 20170423
				dcs.m_usePageTemperature = use_page_temperature;
				dcs.m_pageTemperature = page_temperature;
				dcs.m_adjustedSiteRank = adjustedSiteRank;

				if( genDebugScoreInfo2(&dcs, &lastLen, &lastDocId, siteRank, score, intScore, docLang) ) {
					// advance to next docid
					if( currPassNum == INTERSECT_SCORING ) {
						docIdPtr += 6;
					}
					// Continue docIdPtr < docIdEnd loop
					continue;
				}
			}


			if( currPassNum == INTERSECT_SCORING ) {

				//#
				//# SCORING DONE! Add to top-scorer tree.
				//#

				int32_t tn = m_topTree->getEmptyNode();

				// Sanity check
				if( tn < 0 ) {
					log(LOG_LOGIC,"%s:%s:%d: No space left in m_topTree", __FILE__, __func__, __LINE__);
					gbshutdownLogicError();
				}

				TopNode *t  = m_topTree->getNode(tn);

				// set the score and docid ptr
				t->m_score = score;
				t->m_docId = m_docId;
				t->m_flags = flags;
				logTrace(g_conf.m_logTracePosdb, "docid=%15ld score=%f", m_docId, score);

				// use an integer score like lastSpidered timestamp?
				if ( m_sortByTermNumInt >= 0 ) {
					t->m_intScore = intScore;
					t->m_score = 0.0;
					
					if ( ! m_topTree->m_useIntScores) {
						log(LOG_LOGIC,"%s:%s:%d: Got int score, but m_topTree not setup for int scores!", __FILE__, __func__, __LINE__);
						gbshutdownLogicError();
					}
				}

				//
				// This will not add if tree is full and it is less than the m_lowNode in score.
				//
				// If it does get added to a full tree, lowNode will be removed.
				//
				m_topTree->addNode(t, tn);

				// top tree only holds enough docids to satisfy the
				// m_msg39request::m_docsToGet (m_msg39req->m_docsToGet) request 
				// from the searcher. It basically stores m_docsToGet
				// into TopTree::m_docsWanted. TopTree::m_docsWanted is often 
				// double m_docsToGet to compensate for site clustering, and
				// it can be even more than that in order to ensure we get
				// enough domains represented in the search results.
				// See TopTree::addNode(). it will not add the "t" node if
				// its score is not high enough when the top tree is full.
				if ( m_topTree->getNumUsedNodes() > m_topTree->getNumDocsWanted() ) {
					// get the lowest scoring node
					int32_t lowNode = m_topTree->getLowNode();
					// and record its score in "minWinningScore"
					minWinningScore = m_topTree->getNode(lowNode)->m_score;
				}
			}

			// advance to next docid
			if( currPassNum == INTERSECT_SCORING ) {
				docIdPtr += 6;
			}
		} // docIdPtr < docIdEnd loop


		if ( m_debug ) {
			now = gettimeofdayInMilliseconds();
			took = now - lastTime;
			log(LOG_INFO, "posdb: new algo phase %" PRId32" took %" PRId64" ms", phase,took);
			lastTime = now;
			phase++;
		}

	} // for ... currPassNum < numPassesNeeded


	if ( m_debug ) {
		log(LOG_INFO, "posdb: # prefiltMaxPossScoreFail........: %" PRId32" ", prefiltMaxPossScoreFail );
		log(LOG_INFO, "posdb: # prefiltMaxPossScorePass........: %" PRId32" ", prefiltMaxPossScorePass );
		log(LOG_INFO, "posdb: # prefiltBestDistMaxPossScoreFail: %" PRId32" ", prefiltBestDistMaxPossScoreFail );
		log(LOG_INFO, "posdb: # prefiltBestDistMaxPossScorePass: %" PRId32" ", prefiltBestDistMaxPossScorePass );
	}

	if( g_conf.m_logTracePosdb ) {
		m_topTree->logTreeData(LOG_TRACE);
		logDebugScoreInfo(LOG_TRACE);
	}

	// get time now
	now = gettimeofdayInMilliseconds();
	// store the addLists time
	m_addListsTime = now - t1;
	m_t1 = t1;
	m_t2 = now;

	//opportunistic cleanup (memory release)
	m_wikiPhraseIds.clear();
	m_quotedStartIds.clear();
	m_qpos.clear();
	m_qtermNums.clear();
	m_bflags.clear();

	logTrace(g_conf.m_logTracePosdb, "END. Took %" PRId64" msec", m_addListsTime);
}



// . "bestDist" is closest distance to query term # m_minTermListIdx
// . set "bestDist" to 1 to ignore it
float PosdbTable::getMaxPossibleScore(const QueryTermInfo *qti) {
	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// get max score of all sublists
	float bestHashGroupWeight = -1.0;
	float bestTermFreqWeight = -1.0;
	unsigned char bestDensityRank = 0;
	char siteRank = -1;
	char docLang = -1;
	unsigned char hgrp;
	bool hadHalfStopWikiBigram = false;
	
	// scan those sublists to set m_ptrs[] and to get the
	// max possible score of each one
	for ( int32_t j = 0 ; j < qti->m_numMatchingSubLists ; j++ ) {
		// scan backwards up to this
		const char *start = qti->m_matchingSublist[j].m_savedCursor;
		
		// skip if does not have our docid
		if ( ! start ) {
			continue;
		}
		
		// note it if any is a wiki bigram
		if ( qti->m_subList[0].m_bigramFlag & BF_HALFSTOPWIKIBIGRAM ) {
			hadHalfStopWikiBigram = true;
		}
		
		// skip if entire sublist/termlist is exhausted
		if ( start >= qti->m_matchingSublist[j].m_end ) {
			continue;
		}
			
		if ( siteRank == -1 ) {
			siteRank = Posdb::getSiteRank(start);
		}
		if ( docLang == -1 ) {
			docLang = Posdb::getLangId(start);
		}
		
		// skip first key because it is 12 bytes, go right to the
		// 6 byte keys. we deal with it below.
		start += 12;
		// get cursor. this is actually pointing to the next docid
		const char *dc = qti->m_matchingSublist[j].m_cursor;
		// back up into our list
		dc -= 6;
		// reset this
		bool retried = false;
		
		// do not include the beginning 12 byte key in this loop!
		for ( ; dc >= start ; dc -= 6 ) {
			// loop back here for the 12 byte key
		retry:
			// get the best hash group
			hgrp = Posdb::getHashGroup(dc);

			// if not body, do not apply this algo because
			// we end up adding term pairs from each hash group
			if ( hgrp == HASHGROUP_INLINKTEXT ) {
				return -1.0;
			}
			
			float termFreqWeight = qti->m_subList[qti->m_matchingSublist[j].m_baseSubListIndex].m_qt->m_termFreqWeight;
			if(termFreqWeight>bestTermFreqWeight)
				bestTermFreqWeight = termFreqWeight;
			
			//if ( hgrp == HASHGROUP_TITLE      ) return -1.0;
			// loser?
			if ( m_derivedScoringWeights.m_hashGroupWeights[hgrp] < bestHashGroupWeight ) {
				// if in body, it's over for this termlist 
				// because this is the last hash group
				// we will encounter.
				if ( hgrp == HASHGROUP_BODY ) {
					// @@@ BR: Dangerous assumption if we change indexing order in XmlDoc_Indexing ! @@@
					goto nextTermList;
				}
				// otherwise, keep chugging
				continue;
			}
			
			unsigned char dr = Posdb::getDensityRank(dc);
			
			// a clean win?
			if ( m_derivedScoringWeights.m_hashGroupWeights[hgrp] > bestHashGroupWeight ) {
				bestHashGroupWeight = m_derivedScoringWeights.m_hashGroupWeights[hgrp];
				bestDensityRank = dr;
				continue;
			}
			
			// better density rank?
			if ( dr > bestDensityRank ) {
				bestDensityRank = dr;
			}
		}
		
		// handle the beginning 12 byte key
		if ( ! retried ) {
			retried = true;
			dc = qti->m_matchingSublist[j].m_savedCursor;
			goto retry;
		}

	nextTermList:
		;

	}

	// if nothing, then maybe all sublists were empty?
	if ( bestHashGroupWeight < 0 ) {
		return 0.0;
	}

	// assume perfect adjacency and that the other term is perfect
	float score = 100.0;

	score *= bestHashGroupWeight;
	score *= bestHashGroupWeight;
	
	// since adjacent, 2nd term in pair will be in same sentence
	// TODO: fix this for 'qtm' it might have a better density rank and
	//       better hashgroup weight, like being in title!
	score *= m_derivedScoringWeights.m_densityWeights[bestDensityRank];
	score *= m_derivedScoringWeights.m_densityWeights[bestDensityRank];
	
	// wiki bigram?
	if ( hadHalfStopWikiBigram ) {
		score *= WIKI_BIGRAM_WEIGHT;
		score *= WIKI_BIGRAM_WEIGHT;
	}
	
	//score *= perfectWordSpamWeight * perfectWordSpamWeight;
	score *= (((float)siteRank)*m_baseScoringParameters.m_siteRankMultiplier+1.0);

	// language boost if language specified and if page is same language, or unknown language
	if ( m_msg39req->m_language != 0 ) {
		if( m_msg39req->m_language == docLang) {
			score *= (m_msg39req->m_baseScoringParameters.m_sameLangWeight);
		}
		else
		if( docLang == 0 ) {
			score *= (m_msg39req->m_baseScoringParameters.m_unknownLangWeight);
		}
	}
	
	// assume the other term we pair with will be 1.0
	score *= bestTermFreqWeight;
	score *= bestTermFreqWeight;
	//TODO: shouldn't we multiple with userweight here too?
	

	// terms in same wikipedia phrase?
	//if ( wikiWeight != 1.0 ) 
	//	score *= WIKI_WEIGHT;

	// if query is 'time enough for love' it is kinda slow because
	// we were never multiplying by WIKI_WEIGHT, even though all
	// query terms were in the same wikipedia phrase. so see if this
	// speeds it up.
	if ( m_allInSameWikiPhrase ) {
		score *= WIKI_WEIGHT;
	}
	
	logTrace(g_conf.m_logTracePosdb, "END. score=%f", score);
	return score;
}


float PosdbTable::modifyMaxScoreByDistance(float score,
					   int32_t bestDist,
					   int32_t qdist,
					   const QueryTermInfo *qtm)
{
	score *= qtm->m_maxMatchingTermFreqWeight;
	
	// subtract qdist
	bestDist -= qdist;
	
	// make it positive
	bestDist = abs(bestDist);
	
	// avoid 0 division
	if(bestDist > 1)
		score /= (float)bestDist;
	
	return score;
}



////////////////////
// 
// "White list" functions used to find docids from only specific sites
//
////////////////////

// this is separate from allocTopTree() function below because we must
// call it for each iteration in Msg39::doDocIdSplitLoop() which is used
// to avoid reading huge termlists into memory. it breaks the huge lists
// up by smaller docid ranges and gets the search results for each docid
// range separately.
bool PosdbTable::allocWhiteListTable ( ) {
	//
	// the whitetable is for the docids in the whitelist. we have
	// to only show results whose docid is in the whitetable, which
	// is from the "&sites=abc.com+xyz.com..." custom search site list
	// provided by the user.
	//
	if ( m_msg39req->size_whiteList <= 1 ) m_useWhiteTable = false; // inclds \0
	else 		                m_useWhiteTable = true;
	int32_t sum = 0;
	for ( int32_t i = 0 ; i < m_msg2->getNumWhiteLists() ; i++ ) {
		const RdbList *list = m_msg2->getWhiteList(i);
		if(!list->isEmpty()) {
			// assume 12 bytes for all keys but first which is 18
			int32_t size = list->getListSize();
			sum += size / 12 + 1;
		}
	}
	if ( sum ) {
		// making this sum * 3 does not show a speedup... hmmm...
		int32_t numSlots = sum * 2;
		// keep it restricted to 5 byte keys so we do not have to
		// extract the docid, we can just hash the ptr to those
		// 5 bytes (which includes 1 siterank bit as the lowbit,
		// but should be ok since it should be set the same in
		// all termlists that have that docid)
		if (!m_whiteListTable.set(5, 0, numSlots, NULL, 0, false, "wtall", true)) {
			return false;
		}
	}
	return true;
}



void PosdbTable::prepareWhiteListTable()
{
	// hash the docids in the whitelist termlists into a hashtable.
	// every docid in the search results must be in there. the
	// whitelist termlists are from a provided "&sites=abc.com+xyz.com+.."
	// cgi parm. the user only wants search results returned from the
	// specified subdomains. there can be up to MAX_WHITELISTS (500)
	// sites right now. this hash table must have been pre-allocated
	// in Posdb::allocTopTree() above since we might be in a thread.
	if ( m_addedSites )
		return;

	for ( int32_t i = 0 ; i < m_msg2->getNumWhiteLists() ; i++ ) {
		RdbList *list = m_msg2->getWhiteList(i);
		if(!list->isEmpty()) {
			// sanity test
			int64_t d1 = Posdb::getDocId(list->getList());
			if ( d1 > m_msg2->docIdEnd() ) {
				log("posdb: d1=%" PRId64" > %" PRId64,
				    d1,m_msg2->docIdEnd());
				//gbshutdownAbort(true);
			}
			if ( d1 < m_msg2->docIdStart() ) {
				log("posdb: d1=%" PRId64" < %" PRId64,
				    d1,m_msg2->docIdStart());
				//gbshutdownAbort(true);
			}
			// first key is always 18 bytes cuz it has the termid
			// scan recs in the list
			for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
				char *rec = list->getCurrentRec();
				// point to the 5 bytes of docid
				m_whiteListTable.addKey ( rec + 7 );
			}
		}
	}

	m_addedSites = true;
}



bool PosdbTable::allocateTopTree() {
	//If all Msg2 rdblists are empty then don't do anything (there is nothing to rank or score)
	bool allEmpty = true;
	for(int i = 0; i < m_msg2->getNumLists(); i++) {
		const RdbList *list = m_msg2->getList(i);
		
		if(list && !list->isEmpty() && list->getListSize()>=18) {
			allEmpty = false;
			break;
		}
	}
	if(allEmpty) {
		if(m_debug)
			log(LOG_DEBUG,"toptree: all Msg2 lists are empty");
		return true;
	}
	
	// Normally m_msg39req->m_docsToGet is something sensible such as 10 or 50. Some specialized queries or attempts
	// at DOSing can set it to something unreasonable as 1.000.000. Internal functions such as QueryReindex sets
	// m_msg39req->m_docsToGet to 99999999 meaning "all documents".
	// We cannot protect against DOSs here, but the 99999999 value must be handled. The problem is that 99999999
	// would likely cause OOM, so we have to size the toptree to what is actually in the database. And in the case
	// the database-derived size causes OOM then we'd have to use the documentSplit functionality (which is
	// currently defunct after nomerge2 branch was merged to master. See Msg39.cpp for details).
	//
	// Strategy:
	//   - if m_msg39req->m_docsToGet is smallish then accept it. Only adjust as needed by enabled clustering.
	//   - otherwise get an estimated list size from Posdb so we can put an upper (and better) limit on
	//     m_msg39req->m_docsToGet instead of 99999999
	
	int32_t docsWanted;
	if(m_msg39req->m_docsToGet <= 1000) {
		// smallish, accept as-is
		docsWanted = m_msg39req->m_docsToGet;
		if(m_debug)
			log(LOG_DEBUG, "toptree: docsToGet is small (%d), docsWanted = %d", m_msg39req->m_docsToGet, docsWanted);
	} else {
		//not small. Get list size estimates and estimate an upper limit on the number of documents that could possibly match.
		//
		//we cannot calculate the estimate based on m_msg2->getList(...)->getListSize() because m_msg2 only holds the lists
		//from the current fileNumber (eg. posdb0007.dat) and we need the estimate over all the files + bucket
		int64_t totalEstimatedEntries = 0;
		for(int i=0; i<m_q->getNumTerms(); i++) {
			int64_t termId = m_q->getTermId(i);
			int64_t estimatedTermListSize = g_posdb.estimateLocalTermListSize(m_msg39req->m_collnum, termId);
			logTrace(g_conf.m_logTracePosdb, "allocateTopTree: termId=%ld, estimatedTermListSize=%ld bytes", termId, estimatedTermListSize);
			//If we have listsize=54 then due to the double compression of posdb entries it could be one
			//document with 7 entries, or four documents with one entry each
			//The latter is the worst case so we'll use that.
			int32_t estimatedEntries = estimatedTermListSize / (sizeof(posdbkey_t)-6);
			totalEstimatedEntries += estimatedEntries;
		}
		logTrace(g_conf.m_logTracePosdb, "totalEstimatedEntries=%ld", totalEstimatedEntries);
		if(m_msg39req->m_docsToGet < totalEstimatedEntries) {
			//wants less than what is in the DB. excellent
			docsWanted = m_msg39req->m_docsToGet;
		} else {
			//wants as much or more than there is in the DB. Hmmm.
			if(totalEstimatedEntries > INT32_MAX) { //32bit overflow
				log(LOG_ERROR,"toptree: estimated number of documents = %ld. Cannot squeeze that into a TopTree", totalEstimatedEntries);
				return false;
			}
			docsWanted = totalEstimatedEntries;
		}
		if(m_debug)
			log(LOG_DEBUG, "toptree: docsToGet is large (%d), docsWanted = %d", m_msg39req->m_docsToGet, docsWanted);
	}
	
	//Magic multiplication. Original source had no comment on why this was done. TopTree appears to already
	//take care of enlarging the allocation if clustering is enabled, so uhm... ?
	if(m_msg39req->m_doSiteClustering)
		docsWanted *= 2;

	// limit to 2B docids i guess
	docsWanted = gbmin(docsWanted,2000000000);

	if(m_debug)
		log(LOG_INFO, "toptree: toptree: initializing %d nodes",docsWanted);

	if(docsWanted < m_msg39req->m_docsToGet) {
		log("query: warning only getting up to %d docids even though %d requested because termlist sizes are smaller",
		    docsWanted, m_msg39req->m_docsToGet);
	}

	// keep it sane
	if(docsWanted > m_msg39req->m_docsToGet * 2 && docsWanted > 60) {
		docsWanted = m_msg39req->m_docsToGet * 2;
	}

	// this actually sets the # of nodes to MORE than nn!!!
	if(!m_topTree->setNumNodes(docsWanted, m_msg39req->m_doSiteClustering)) {
		log("toptree: toptree: error allocating nodes: %s",
		    mstrerror(g_errno));
		return false;
	}
	
	return true;
}


bool PosdbTable::allocateScoringInfo() {
	// let's use nn*4 to try to get as many score as possible, although
	// it may still not work!
	int32_t xx = m_topTree->getNumDocsWanted();
	
	// try to fix a core of growing this table in a thread when xx == 1
	xx = gbmax(xx,32);
	
	//if ( m_msg39req->m_doSiteClustering ) xx *= 4;
	// for seeing if a docid is in toptree. niceness=0.
	//if ( ! m_docIdTable.set(8,0,xx*4,NULL,0,false,0,"dotb") )
	//	return false;

	if ( m_msg39req->m_getDocIdScoringInfo ) {

		m_scoreInfoBuf.setLabel ("scinfobuf" );

		// . for holding the scoring info
		// . add 1 for the \0 safeMemcpy() likes to put at the end so 
		//   it will not realloc on us
		if ( ! m_scoreInfoBuf.reserve ( xx * sizeof(DocIdScore) +100) ) {
			return false;
		}
		
		// likewise how many query term pair scores should we get?
		int32_t numTerms = m_q->m_numTerms;

		// limit
		numTerms = gbmin(numTerms,10);

		// the pairs. divide by 2 since (x,y) is same as (y,x)
		int32_t numPairs = (numTerms * numTerms) / 2;

		// then for each pair assume no more than MAX_TOP reps, usually
		// it's just 1, but be on the safe side
		numPairs *= m_realMaxTop;//MAX_TOP;

		// now that is how many pairs per docid and can be 500! but we
		// still have to multiply by how many docids we want to 
		// compute. so this could easily get into the megabytes, most 
		// of the time we will not need nearly that much however.
		numPairs *= xx;

		m_pairScoreBuf.setLabel ( "pairbuf" );
		m_singleScoreBuf.setLabel ("snglbuf" );

		// but alloc it just in case
		if ( ! m_pairScoreBuf.reserve (numPairs * sizeof(PairScore) ) ) {
			return false;
		}
		
		// and for singles
		int32_t numSingles = numTerms * m_realMaxTop * xx; // MAX_TOP *xx;
		if ( !m_singleScoreBuf.reserve(numSingles*sizeof(SingleScore))) {
			return false;
		}
	}

	return true;
}



////////////////////
// 
// Functions used to find candidate docids, including "Voting" functions 
// used to find docids with all required term ids and functions to remove
// docids that do not match all required terms
//
////////////////////


//
// Run through each term sublist and remove all docids not
// found in the docid vote buffer
//
void PosdbTable::delNonMatchingDocIdsFromSubLists() {

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	//phase 1: shrink the rdblists for all queryterms (except those with a minus sign)
	std::valarray<char *> newEndPtr(m_q->m_numTerms);
	for(int i=0; i<m_q->m_numTerms; i++) {
		newEndPtr[i] = NULL;
		if(m_q->m_qterms[i].m_termSign=='-')
			continue;
		RdbList *list = m_q->m_qterms[i].m_posdbListPtr;
		if(!list || list->isEmpty())
			continue;
		
		// get that sublist
		char *subListPtr = list->getList();
		char *subListEnd = list->getListEnd();
		char *dst = subListPtr;
		// reset docid list ptrs
		const char *dp    =      m_docIdVoteBuf.getBufStart();
		const char *dpEnd = dp + m_docIdVoteBuf.length();
		//log(LOG_INFO,"@@@@ i#%d subListPtr=%p subListEnd=%p", i, subListPtr, subListEnd);
		
		for(;;) {
			// scan the docid list for the current docid in this termlist
			for ( ; dp < dpEnd; dp += 6 ) {
				// if current docid in docid list is >= the docid
				// in the sublist, stop. docid in list is 6 bytes and
				// subListPtr must be pointing to a 12 byte posdb rec.
				if ( *(uint32_t *)(dp+1) > *(uint32_t *)(subListPtr+8) ) {
					break;
				}
				
				// try to catch up docid if it is behind
				if ( *(uint32_t *)(dp+1) < *(uint32_t *)(subListPtr+8) ) {
					continue;
				}

				// check lower byte if equal
				if ( *(unsigned char *)(dp) > (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
					break;
				}

				if ( *(unsigned char *)(dp) < (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
					continue;
				}

				// copy over the 12 byte key
				*(int64_t *)dst = *(int64_t *)subListPtr;
				*(int32_t *)(dst+8) = *(int32_t *)(subListPtr+8);

				// skip that
				dst    += 12;
				subListPtr += 12;

				// copy over any 6 bytes keys following
				for ( ; ; ) {
					if ( subListPtr >= subListEnd ) {
						// give up on this exhausted term list!
						goto doneWithSubList;
					}

					// next docid willbe next 12 bytekey
					if ( ! ( subListPtr[0] & 0x04 ) ) {
						break;
					}

					// otherwise it's 6 bytes
					*(int32_t *)dst = *(int32_t *)subListPtr;
					*(int16_t *)(dst+4) = *(int16_t *)(subListPtr+4);
					dst += 6;
					subListPtr += 6;
				}
			}

			// skip that docid record in our termlist. it MUST have been
			// 12 bytes, a docid heading record.
			subListPtr += 12;

			// skip any following keys that are 6 bytes, that means they
			// share the same docid
			for ( ; ;  ) {
				// list exhausted?
				if ( subListPtr >= subListEnd ) {
					goto doneWithSubList;
				}
				
				// stop if next key is 12 bytes, that is a new docid
				if ( ! (subListPtr[0] & 0x04) ) {
					break;
				}
				
				// skip it
				subListPtr += 6;
			}
		}

	doneWithSubList:
		//log(LOG_INFO,"@@@ shrunk #%d to %ld (%p-%p)", i, dst - list->getList(), list->getList(), dst);
		newEndPtr[i] = dst;
	}
	
	//phase 2: set the matchingsublist pointers in qti
	for(int i=0; i<m_numQueryTermInfos; i++) {
		QueryTermInfo *qti = &(m_queryTermInfos[i]);;
		qti->m_numMatchingSubLists = 0;
		if(qti->m_numSubLists>0 && qti->m_subList[0].m_bigramFlag & BF_NEGATIVE)
			continue; //don't modify sublist for negative terms
#ifdef _VALGRIND_
		VALGRIND_MAKE_MEM_UNDEFINED(qti->m_matchingSublist, sizeof(qti->m_matchingSublist));
#endif
		for(int j=0; j<qti->m_numSubLists; j++) {
			for(int k=0; k<m_q->m_numTerms; k++) {
				if(qti->m_subList[j].m_list == m_q->m_qterms[k].m_posdbListPtr) {
					char *newStartPtr = m_q->m_qterms[k].m_posdbListPtr->getList(); //same as always
					int32_t x = qti->m_numMatchingSubLists;
					qti->m_matchingSublist[x].m_size  	    = newEndPtr[k] - newStartPtr;
					qti->m_matchingSublist[x].m_start 	    = newStartPtr;
					qti->m_matchingSublist[x].m_end	    = newEndPtr[k];
					qti->m_matchingSublist[x].m_cursor	    = newStartPtr;
					qti->m_matchingSublist[x].m_savedCursor = newStartPtr;
					qti->m_matchingSublist[x].m_baseSubListIndex    = j;
					qti->m_numMatchingSubLists++;
					break;
				}
			}
		}
	}
	
	//calculate qti[].m_maxMatchingTermFreqWeight
	for(int i=0; i<m_numQueryTermInfos; i++) {
		QueryTermInfo *qti = &(m_queryTermInfos[i]);
		if(qti->m_numMatchingSubLists>0) {
			float maxMatchingTermFreqWeight = qti->m_subList[qti->m_matchingSublist[0].m_baseSubListIndex].m_qt->m_termFreqWeight;
			for(int j=1; j<qti->m_numMatchingSubLists; j++) {
				float w = qti->m_subList[qti->m_matchingSublist[j].m_baseSubListIndex].m_qt->m_termFreqWeight;
				if(w>maxMatchingTermFreqWeight)
					maxMatchingTermFreqWeight = w;
			}
			qti->m_maxMatchingTermFreqWeight = maxMatchingTermFreqWeight;
		} else
			qti->m_maxMatchingTermFreqWeight = -1.0;
	}

	logTrace(g_conf.m_logTracePosdb, "END.");
}



//
// Removes docids with vote -1, which is set for docids matching
// negative query terms (e.g. -rock)
//
void PosdbTable::delDocIdVotes ( const QueryTermInfo *qti ) {
	char *bufStart = m_docIdVoteBuf.getBufStart();
	char *voteBufPtr = NULL;
	char *voteBufEnd;

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// just scan each sublist vs. the docid list
	for ( int32_t i = 0 ; i < qti->m_numSubLists  ; i++ ) {
		// get that sublist
		char *subListPtr = qti->m_subList[i].m_list->getList();
		char *subListEnd = qti->m_subList[i].m_list->getListEnd();
		// reset docid list ptrs
		voteBufPtr = m_docIdVoteBuf.getBufStart();
		voteBufEnd = voteBufPtr + m_docIdVoteBuf.length();

		// loop it
		while ( subListPtr < subListEnd ) {
			// scan for his docids and inc the vote
			for ( ;voteBufPtr <voteBufEnd ;voteBufPtr += 6 ) {
				// if current docid in docid list is >= the docid
				// in the sublist, stop. docid in list is 6 bytes and
				// subListPtr must be pointing to a 12 byte posdb rec.
				if ( *(uint32_t *)(voteBufPtr+1) > *(uint32_t *)(subListPtr+8) ) {
					break;
				}
				
				// less than? keep going
				if ( *(uint32_t *)(voteBufPtr+1) < *(uint32_t *)(subListPtr+8) ) {
					continue;
				}
				
				// top 4 bytes are equal. check lower single byte then.
				if ( *(unsigned char *)(voteBufPtr) > (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
					break;
				}
				
				if ( *(unsigned char *)(voteBufPtr) < (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
					continue;
				}
				
				// . equal! mark it as nuked!
				voteBufPtr[5] = -1;
				// skip it
				voteBufPtr += 6;
				// advance subListPtr now
				break;
			}

			// if we've exhausted this docid list go to next sublist
			if ( voteBufPtr >= voteBufEnd ) {
				goto endloop2;
			}

			// skip that docid record in our termlist. it MUST have been
			// 12 bytes, a docid heading record.
			subListPtr += 12;
			// skip any following keys that are 6 bytes, that means they
			// share the same docid
			for ( ; subListPtr < subListEnd && ((*subListPtr)&0x04); ) {
				subListPtr += 6;
			}

			// if we have more posdb recs in this sublist, then keep
			// adding our docid votes into the docid list
		}
	endloop2: ;
		// otherwise, advance to next sublist
	}

	// now remove docids with a -1 vote, they are nuked
	voteBufPtr = m_docIdVoteBuf.getBufStart();
	voteBufEnd = voteBufPtr + m_docIdVoteBuf.length();
	char *dst   = voteBufPtr;
	for ( ; voteBufPtr < voteBufEnd ; voteBufPtr += 6 ) {
		// do not re-copy it if it was in this negative termlist
		if(voteBufPtr[5] != -1) {
			// copy it over. might be the same address!
			*(int32_t *) dst    = *(int32_t *) voteBufPtr;
			*(int16_t *)(dst+4) = *(int16_t *)(voteBufPtr+4);
			dst += 6;
		}
	}
	// shrink the buffer size now
	m_docIdVoteBuf.setLength ( dst - bufStart );
	
	logTrace(g_conf.m_logTracePosdb, "END.");
}



//
// First call will allocate the m_docIdVoteBuf and add docids from the shortest
// term list to the buffer.
//
// Next calls will run through all term sublists (synonyms, term variations) and 
// increase the score in the m_docIdVoteBuf for matching docids. Docids that do
// not match a term is removed, so we end up with list of docids matching all
// query terms.
//
void PosdbTable::addDocIdVotes( const QueryTermInfo *qti, int32_t listGroupNum) {

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// sanity check, we store this in a single byte below for voting
	if ( listGroupNum >= 256 ) {
		gbshutdownAbort(true);
	}


	//
	// add the first sublist's docids into the docid buf
	//
	if( listGroupNum == 0 ) {
		// the first listGroup is not intersecting, just adding to
		// the docid vote buf. that is, if the query is "jump car" we
		// just add all the docids for "jump". Subsequent calls to 
		// this function will intersect those docids with the docids
		// for "car", resulting in a buffer with docids that contain 
		// both terms.

		makeDocIdVoteBufForRarestTerm(qti);
		logTrace(g_conf.m_logTracePosdb, "END.");
		return;
	}


	char *bufStart = m_docIdVoteBuf.getBufStart();
	char *voteBufPtr = NULL;
	char *voteBufEnd;

	// 
	// For each sublist (term variation) loop through all sublist records
	// and compare with docids in the vote buffer. If a match is found, the
	// score in the vote buffer is set to the current ListGroupNum.
	//
	// A sublist is a termlist for a particular query term, for instance
	// the query term "jump" will have sublists for "jump" "jumps"
	// "jumping" "jumped" and maybe even "jumpy", so that could be
	// 5 sublists, and their QueryTermInfo::m_qtermNum should be the
	// same for all 5.
	//
	for ( int32_t i = 0 ; i < qti->m_numSubLists; i++) {
		// get that sublist
		const char *subListPtr = qti->m_subList[i].m_list->getList();
		const char *subListEnd = qti->m_subList[i].m_list->getListEnd();
		// reset docid list ptrs
		voteBufPtr	= m_docIdVoteBuf.getBufStart();
		voteBufEnd	= voteBufPtr + m_docIdVoteBuf.length();
		
		// loop it
	handleNextSubListRecord:
		
		// scan for his docids and inc the vote
		for ( ;voteBufPtr < voteBufEnd ; voteBufPtr += 6 ) {
			// if current docid in docid list is >= the docid
			// in the sublist, stop. docid in list is 6 bytes and
			// subListPtr must be pointing to a 12 byte posdb rec.
			if ( *(uint32_t *)(voteBufPtr+1) > *(uint32_t *)(subListPtr+8) ) {
				break;
			}
				
			// less than? keep going
			if ( *(uint32_t *)(voteBufPtr+1) < *(uint32_t *)(subListPtr+8) ) {
				continue;
			}
			
			// top 4 bytes are equal. check lower single byte then.
			if ( *(unsigned char *)(voteBufPtr) > (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
				break;
			}
			
			if ( *(unsigned char *)(voteBufPtr) < (*(unsigned char *)(subListPtr+7) & 0xfc ) ) {
				continue;
			}

			// . equal! record our vote!
			// . we start at zero for the
			//   first termlist, and go to 1, etc.
			voteBufPtr[5] = listGroupNum;
			// skip it
			voteBufPtr += 6;

			// break out to advance subListPtr
			break;
		}

		// if we've exhausted this docid list go to next sublist
		// since this docid is NOT in the current/ongoing intersection
		// of the docids for each queryterm
		if ( voteBufPtr >= voteBufEnd ) {
			continue;
		}

		// skip that docid record in our termlist. it MUST have been
		// 12 bytes, a docid heading record.
		subListPtr += 12;

		// skip any following keys that are 6 bytes, that means they
		// share the same docid
		for ( ; subListPtr < subListEnd && ((*subListPtr)&0x04); ) {
			subListPtr += 6;
		}
		
		// if we have more posdb recs in this sublist, then keep
		// adding our docid votes into the docid list
		if ( subListPtr < subListEnd ) {
			goto handleNextSubListRecord;
		}
			
		// otherwise, advance to next sublist
	}


	//
	// Shrink the docidbuf by removing docids with not enough 
	// votes which means they are missing a query term
	//
	voteBufPtr	= m_docIdVoteBuf.getBufStart();
	voteBufEnd	= voteBufPtr + m_docIdVoteBuf.length();
	char *dst   = voteBufPtr;
	for ( ; voteBufPtr < voteBufEnd ; voteBufPtr += 6 ) {
		// skip if it has enough votes to be in search 
		// results so far
		if(voteBufPtr[5] == listGroupNum) {
			// copy it over. might be the same address!
			*(int32_t  *) dst   = *(int32_t *) voteBufPtr;
			*(int16_t *)(dst+4) = *(int16_t *)(voteBufPtr+4);
			dst += 6;
		}
	}

	// shrink the buffer size
	m_docIdVoteBuf.setLength ( dst - bufStart );
	
	logTrace(g_conf.m_logTracePosdb, "END.");
}



//
// Initialize the vote buffer with docids from the shortest 
// term (rarest required term) list and initialize the scores to 0.
// Called by addDocIdVotes
//
// The buffer consists of 5-byte docids and 1-byte scores. The score
// is incremented for each term that matches the docid, and after 
// each run, the list is "compacted" and shortened so only the 
// matching docids are left.
//
void PosdbTable::makeDocIdVoteBufForRarestTerm(const QueryTermInfo *qti) {
	char *cursor[MAX_SUBLISTS];
	char *cursorEnd[MAX_SUBLISTS];

	logTrace(g_conf.m_logTracePosdb, "term id [%" PRId64 "] [%.*s]", qti->m_subList[0].m_qt->m_termId, qti->m_subList[0].m_qt->m_termLen, qti->m_subList[0].m_qt->m_term);

	for ( int32_t i = 0 ; i < qti->m_numSubLists ; i++ ) {
		// get that sublist
		cursor    [i] = qti->m_subList[i].m_list->getList();
		cursorEnd [i] = qti->m_subList[i].m_list->getListEnd();
	}

	char * const bufStart = m_docIdVoteBuf.getBufStart();
	char *voteBufPtr = m_docIdVoteBuf.getBufStart();
	char *lastMinRecPtr = NULL;
	int32_t mini = -1;
	
	// get the next min from all the termlists
	for(;;) {

		char *minRecPtr = NULL;

		// just scan each sublist vs. the docid list
		for ( int32_t i = 0 ; i < qti->m_numSubLists ; i++ ) {
			// skip if exhausted
			if ( ! cursor[i] ) {
				continue;
			}

			// shortcut
			char *recPtr = cursor[i];

			// get the min docid
			if ( ! minRecPtr ) {
				minRecPtr = recPtr;
				mini = i;
				continue;
			}

			// compare!
			if ( *(uint32_t *)(recPtr   +8) >
			     *(uint32_t *)(minRecPtr+8) ) {
				continue;
			}

			// a new min
			if ( *(uint32_t *)(recPtr   +8) <
			     *(uint32_t *)(minRecPtr+8) ) {
				minRecPtr = recPtr;
				mini = i;
				continue;
			}

			// check lowest byte
			if ( (*(unsigned char *)(recPtr   +7) & 0xfc ) >
			     (*(unsigned char *)(minRecPtr+7) & 0xfc ) ) {
				continue;
			}

			// a new min
			if ( (*(unsigned char *)(recPtr   +7) & 0xfc ) <
			     (*(unsigned char *)(minRecPtr+7) & 0xfc ) ) {
				minRecPtr = recPtr;
				mini = i;
				continue;
			}
		}

		// if no min then all lists exhausted!
		if ( ! minRecPtr ) {
			// update length
			m_docIdVoteBuf.setLength ( voteBufPtr - bufStart );

			// all done!
			logTrace(g_conf.m_logTracePosdb, "END.");
			return;
		}

		// advance that guy over that docid
		cursor[mini] += 12;
		// 6 byte keys follow?
		for ( ; ; ) {
			// end of list?
			if ( cursor[mini] >= cursorEnd[mini] ) {
				// use NULL to indicate list is exhausted
				cursor[mini] = NULL;
				break;
			}

			// if we hit a new 12 byte key for a new docid, stop
			if ( ! ( cursor[mini][0] & 0x04 ) ) {
				break;
			}

			// otherwise, skip this 6 byte key
			cursor[mini] += 6;
		}

		// is it a docid dup?
		if(lastMinRecPtr &&
		   *(uint32_t *)(lastMinRecPtr+8) ==
		   *(uint32_t *)(minRecPtr+8) &&
		   (*(unsigned char *)(lastMinRecPtr+7)&0xfc) ==
		   (*(unsigned char *)(minRecPtr+7)&0xfc)) {
			continue;
		}

		// . do not store the docid if not in the whitelist
		// . FIX: two lower bits, what are they? at minRecPtrs[7].
		// . well the lowest bit is the siterank upper bit and the
		//   other bit is always 0. we should be ok with just using
		//   the 6 bytes of the docid ptr as is though since the siterank
		//   should be the same for the site: terms we indexed for the same
		//   docid!!
		if ( m_useWhiteTable && ! m_whiteListTable.isInTable(minRecPtr+7) ) {
			continue;
		}

		// only update this if we add the docid... that way there can be
		// a winning "inRange" term in another sublist and the docid will
		// get added.
		lastMinRecPtr = minRecPtr;

		// store our docid. actually it contains two lower bits not
		// part of the docid, so we'll have to shift and mask to get
		// the actual docid!
		// docid is only 5 bytes for now
		*(int32_t  *)(voteBufPtr+1) = *(int32_t  *)(minRecPtr+8);
		// the single lower byte
		voteBufPtr[0] = minRecPtr[7] & 0xfc;
		// 0 vote count
		voteBufPtr[5] = 0;

		// debug
		//	int64_t dd = Posdb::getDocId(minRecPtr);
		//	log(LOG_ERROR, "%s:%s: adding docid %" PRId64 "", __FILE__, __func__, dd);

		// advance
		voteBufPtr += 6;
	}
}



// TODO: do this in docid range phases to save memory and be much faster
// since we could contain to the L1 cache for hashing
bool PosdbTable::makeDocIdVoteBufForBoolQuery( ) {

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");

	// . make a hashtable of all the docids from all the termlists
	// . the value slot will be the operand bit vector i guess
	// . the size of the vector needs one bit per query operand
	// . if the vector is only 1-2 bytes we can just evaluate each
	//   combination we encounter and store it into an array, otherwise,
	//   we can use a another hashtable in order to avoid re-evaluation
	//   on if it passes the boolean query.
	char bitVec[MAX_OVEC_SIZE];
	if ( m_vecSize > MAX_OVEC_SIZE ) {
		m_vecSize = MAX_OVEC_SIZE;
	}

	// . scan each list of docids to a get a new docid, keep a dedup
	//   table to avoid re-processing the same docid.
	// . each posdb list we read corresponds to a query term,
	//   or a synonym of a query term, or bigram of a query term, etc.
	//   but we really want to know what operand, so we associate an
	//   operand bit with each query term, and each list can map to 
	//   the base query term so we can get the operand # from that.
	for ( int32_t i = 0 ; i < m_numQueryTermInfos ; i++ ) {

		// get it
		QueryTermInfo *qti = &(m_queryTermInfos[i]);

		QueryTerm *qt = &m_q->m_qterms[qti->m_qtermNum];
		// get the query word
		//QueryWord *qw = qt->m_qword;

		// just use the word # now
		//int32_t opNum = qw->m_wordNum;//opNum;

		// . make it consistent with Query::isTruth()
		// . m_bitNum is set above to the QueryTermInfo #
		int32_t bitNum = qt->m_bitNum;

		// do not consider for adding if negative ('my house -home')
		//if ( qti->m_bigramFlags[0] & BF_NEGATIVE ) continue;

		// set all to zeroes
		memset ( bitVec, 0, m_vecSize );

		// set bitvec for this query term #
		int32_t byte = bitNum / 8;
		unsigned char mask = 1<<(bitNum % 8);
		bitVec[byte] |= mask;

		// each query term can have synonym lists etc. scan those.
		// this includes the original query termlist as well.
		for ( int32_t j = 0 ; j < qti->m_numSubLists ; j++ ) {

			// scan all docids in this list
			const char *p =    qti->m_subList[j].m_list->getList();
			const char *pend = qti->m_subList[j].m_list->getListEnd();

			//int64_t lastDocId = 0LL;

			// scan the sub termlist #j
			for ( ; p < pend ; ) {
				// place holder
				int64_t docId = Posdb::getDocId(p);

				// sanity
				//if ( d < lastDocId )
				//	gbshutdownAbort(true);
				//lastDocId = d;

				// point to it
				//char *voteBufPtr = p + 8;

				// this was the first key for this docid for 
				// this termid and possible the first key for 
				// this termid, so skip it, either 12 or 18 
				// bytes
				if ( (((char *)p)[0])&0x02 ) {
					p += 12;
				}
				// the first key for this termid?
				else {
					p += 18;
				}

				// then only 6 byte keys would follow from the
				// same docid, so skip those as well
			subloop:
				if( p < pend && (((char *)p)[0]) & 0x04 ) {
					p += 6;
					goto subloop;
				}

				// convert docid into hash key
				//int64_t docId = *(int64_t *)voteBufPtr;
				// shift down 2 bits
				//docId >>= 2;
				// and mask
				//docId &= DOCID_MASK;
				// test it
				//int64_t docId = Posdb::getDocId(voteBufPtr-8);
				//if ( d2 != docId )
				//	gbshutdownAbort(true);
				// store this docid though. treat as int64_t
				// but we mask with keymask
				int32_t slot = m_bt.getSlot ( &docId );
				if ( slot < 0 ) {
					// we can't alloc in a thread, careful
					if ( ! m_bt.addKey(&docId,bitVec) ) {
						gbshutdownAbort(true);
					}
					
					continue;
				}
				// or the bit in otherwise
				char *bv = (char *)m_bt.getValueFromSlot(slot);
				bv[byte] |= mask;
			}
		}
	}


	// debug info
	// int32_t nc = m_bt.getLongestString();
	// log("posdb: string of %" PRId32" filled slots!",nc);

	char *dst = m_docIdVoteBuf.getBufStart();

	// . now our hash table is filled with all the docids
	// . evaluate each bit vector
	for ( int32_t i = 0 ; i < m_bt.getNumSlots() ; i++ ) {
		// skip if empty
		if ( ! m_bt.m_flags[i] ) {
			continue;
		}
			
		// get the bit vector
		const unsigned char *vec = (unsigned char *)m_bt.getValueFromSlot(i);
		
		// hash the vector
		int64_t h64 = 0LL;
		for ( int32_t k = 0 ; k < m_vecSize ; k++ )
		       h64 ^= g_hashtab[(unsigned char)vec[k]][(unsigned char)k];
		       
		// check in hash table
		char *val = (char *)m_ct.getValue ( &h64 );

		// it passes, add the ocid
		if ( m_debug ) {
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			log(LOG_INFO, "query: eval d=%" PRIu64" vec[0]=%" PRIx32" h64=%" PRId64, docId,(int32_t)vec[0],h64);
		}

		// add him to the good table
		if ( val && *val ) {
			// it passes, add the ocid
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			
			// fix it up
			if ( m_debug ) {
				log(LOG_INFO, "query: adding d=%" PRIu64" bitVecSize=%" PRId32" bitvec[0]=0x%" PRIx32" (TRUE)",
				    docId,m_vecSize,(int32_t)vec[0]);
			}
			// shift up
			docId <<= 2;
			// a 6 byte key means you pass
			memcpy ( dst, &docId, 6 );
			dst += 6;
			continue;
		}

		// evaluate the vector
		char include = m_q->matchesBoolQuery ( (unsigned char *)vec, m_vecSize );
		if ( include ) {
			// it passes, add the ocid
			int64_t docId =*(int64_t *)m_bt.getKeyFromSlot(i);
			
			// fix it up
			if ( m_debug ) {
				log(LOG_INFO, "query: adding d=%" PRIu64" vec[0]=0x%" PRIx32, docId,(int32_t)vec[0]);
			}
			
			// shift up
			docId <<= 2;
			// a 6 byte key means you pass
			memcpy ( dst, &docId, 6 );
			
			// test it
			if ( m_debug ) {
				int64_t d2;
				d2 = *(uint32_t *)(dst+1);
				d2 <<= 8;
				d2 |= (unsigned char)dst[0];
				d2 >>= 2;
				docId >>= 2;
				if ( d2 != docId )
					gbshutdownAbort(true);
			}
			// end test
			dst += 6;
		}
		// store in hash table
		m_ct.addKey ( &h64, &include );
	}

	// update SafeBuf::m_length
	m_docIdVoteBuf.setLength ( dst - m_docIdVoteBuf.getBufStart() );

	// now sort the docids. TODO: break makeDocIdVoteBufForBoolQuery()
	// up into docid ranges so we have like 1/100th the # of docids to 
	// sort. that should make this part a lot faster.
	// i.e. 1000*log(1000) > 1000*(10*log(10))) --> 3000 > 1000
	// i.e. it's faster to break it down into 1000 pieces
	// i.e. for log base 2 maybe it's like 10x faster...
	qsort ( m_docIdVoteBuf.getBufStart(),
		m_docIdVoteBuf.length() / 6,
		6,
		docIdVoteBufKeyCompare_desc );

	logTrace(g_conf.m_logTracePosdb, "END.");
	return true;
}



////////////////////
// 
// Global functions
//
////////////////////



// sort vote buf entries in descending order
static int docIdVoteBufKeyCompare_desc ( const void *h1, const void *h2 ) {
	return KEYCMP((const char*)h1, (const char*)h2, 6);
}



// . b-step into list looking for docid "docId"
// . assume p is start of list, excluding 6 byte of termid
static inline const char *getWordPosList(uint64_t docId, const char *list, int32_t listSize) {
	// make step divisible by 6 initially
	int32_t step = (listSize / 12) * 6;
	// shortcut
	const char *listEnd = list + listSize;
	// divide in half
	const char *p = list + step;
	// for detecting not founds
	char count = 0;
	
	for(;;) {
		// save it
		const char *origp = p;
		// scan up to docid. we use this special bit to distinguish between
		// 6-byte and 12-byte posdb keys
		for ( ; p > list && (p[1] & 0x02); ) {
			p -= 6;
		}
		// ok, we hit a 12 byte key i guess, so backup 6 more
		p -= 6;

		// ok, we got a 12-byte key then i guess
		uint64_t d = Posdb::getDocId ( p );
		// we got a match, but it might be a NEGATIVE key so
		// we have to try to find the positive keys in that case
		if ( d == docId ) {
			// if its positive, no need to do anything else
			if ( (p[0] & 0x01) == 0x01 ) return p;
			// ok, it's negative, try to see if the positive is
			// in here, if not then return NULL.
			// save current pos
			const char *current = p;
			// back up to 6 byte key before this 12 byte key
			p -= 6;

			// now go backwards to previous 12 byte key
			for ( ; p > list && (p[1] & 0x02); ) {
				p -= 6;
			}
			// ok, we hit a 12 byte key i guess, so backup 6 more
			p -= 6;

			// is it there?
			if ( p >= list && Posdb::getDocId(p) == docId ) {
				// sanity. return NULL if its negative! wtf????
				if ( (p[0] & 0x01) == 0x00 ) return NULL;
				// got it
				return p;
			}
			// ok, no positive before us, try after us
			p = current;
			// advance over current 12 byte key
			p += 12;
			// now go forwards to next 12 byte key
			for ( ; p < listEnd && (p[1] & 0x02); ) {
				p += 6;
			}

			// is it there?
			if ( p + 12 < listEnd && Posdb::getDocId(p) == docId ) {
				// sanity. return NULL if its negative! wtf????
				if ( (p[0] & 0x01) == 0x00 ) {
					return NULL;
				}

				// got it
				return p;
			}
			// . crap, i guess just had a single negative docid then
			// . return that and the caller will see its negative
			return current;
		}		

		// reduce step
		//step /= 2;
		step >>= 1;
		// . make divisible by 6!
		// . TODO: speed this up!!!
		step = step - (step % 6);

		// sanity
		if ( step % 6 ) {
			gbshutdownAbort(true);
		}

		// ensure never 0
		if ( step <= 0 ) {
			step = 6;
			// return NULL if not found
			if ( count++ >= 2 ) {
				return NULL;
			}
		}

		// go up or down then
		if ( d < docId ) { 
			p = origp + step;
			if ( p >= listEnd ) {
				p = listEnd - 6;
			}
		}
		else {
			p = origp - step;
			if ( p < list ) {
				p = list;
			}
		}
	}
}



// initialize the weights table
static void initWeights ( ) {
	ScopedLock sl(s_mtx_weights);
	if ( s_init ) {
		return;
	}

	logTrace(g_conf.m_logTracePosdb, "BEGIN.");
	
	s_scoringWeights.init(g_conf.m_baseScoringParameters);

	// if two hashgroups are comaptible they can be paired
	for ( int32_t i = 0 ; i < HASHGROUP_END ; i++ ) {
		// set this
		s_inBody[i] = false;
		// is it body?
		if ( i == HASHGROUP_BODY    ||
		     i == HASHGROUP_HEADING ||
		     i == HASHGROUP_INLIST  ||
		     i == HASHGROUP_INMENU   )
			s_inBody[i] = true;

		for ( int32_t j = 0 ; j < HASHGROUP_END ; j++ ) {
			// assume not
			s_isCompatible[i][j] = false;
			// or both in body (and not title)
			bool inBody1 = true;
			if ( i != HASHGROUP_BODY &&
			     i != HASHGROUP_HEADING && 
			     i != HASHGROUP_INLIST &&
			     //i != HASHGROUP_INURL &&
			     i != HASHGROUP_INMENU )
				inBody1 = false;
			bool inBody2 = true;
			if ( j != HASHGROUP_BODY &&
			     j != HASHGROUP_HEADING && 
			     j != HASHGROUP_INLIST &&
			     //j != HASHGROUP_INURL &&
			     j != HASHGROUP_INMENU )
				inBody2 = false;
			// no body allowed now!
			if ( inBody1 || inBody2 ) {
				continue;
			}

			//if ( ! inBody ) continue;
			// now neither can be in the body, because we handle
			// those cases in the new sliding window algo.
			// if one term is only in the link text and the other
			// term is only in the title, ... what then? i guess
			// allow those here, but they will be penalized
			// some with the fixed distance of like 64 units or
			// something...
			s_isCompatible[i][j] = true;
			// if either is in the body then do not allow now
			// and handle in the sliding window algo
			//s_isCompatible[i][j] = 1;
		}
	}

	s_init = true;

#ifdef _VALGRIND_
	//we read from the weight tables without locking. tell helgrind to ignore that
	VALGRIND_HG_DISABLE_CHECKING(&s_scoringWeights,sizeof(s_scoringWeights));
	VALGRIND_HG_DISABLE_CHECKING(s_isCompatible,sizeof(s_isCompatible));
	VALGRIND_HG_DISABLE_CHECKING(s_inBody,sizeof(s_inBody));
#endif

	logTrace(g_conf.m_logTracePosdb, "END.");
}



// Called when ranking settings are changed. Normally called from update-parameter
// broadcast handling (see handleRequest3fLoop() )
void reinitializeRankingSettings()
{
	s_init = false;
	initWeights();
}



float getHashGroupWeight ( unsigned char hg ) {
	initWeights();

	return s_scoringWeights.m_hashGroupWeights[hg];
}


float getDiversityWeight ( unsigned char diversityRank ) {
	initWeights();

	return s_scoringWeights.m_diversityWeights[diversityRank];
}


float getDensityWeight ( unsigned char densityRank ) {
	initWeights();

	return s_scoringWeights.m_densityWeights[densityRank];
}


float getWordSpamWeight ( unsigned char wordSpamRank ) {
	initWeights();

	return s_scoringWeights.m_wordSpamWeights[wordSpamRank];
}


float getLinkerWeight ( unsigned char wordSpamRank ) {
	initWeights();

	return s_scoringWeights.m_linkerWeights[wordSpamRank];
}
