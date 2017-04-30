#ifndef GB_MSG3A_H_
#define GB_MSG3A_H_

#include "Msg39.h"
#include "Multicast.h"

class SearchInput;
class Query;

void setTermFreqWeights ( collnum_t collnum , Query *q, float termFreqWeightFreqMin, float termFreqWeightFreqMax, float termFreqWeightMin, float termFreqWeightMax);

#define MAX_SHARDS 1024

// ALWAYS get at least 20 docids so we can do better ranking
#define MIN_DOCS_TO_GET 20

#define RBUF_SIZE 2048

class DocIdScore;


class Msg3a {
public:
	Msg3a();
	~Msg3a();
	void constructor();

	void reset ( );

	// . returns false if blocked, true otherwise
        // . sets errno on error
        // . "query/coll/docIds" should NOT be on the stack in case we block
	// . uses Query class to parse query
	// . uses Indexdb class to intersect the lists to get results
	// . fills docIds buf with the resulting docIds
	// . sets *numDocIds to the # of resulting docIds
	// . if restrictindexdbForQuery is true we only read docIds from 
	//   indexdb root file
	// . this might ADJUST m_si->m_q.m_termFreqs[] to be more accurate
	// . NOTE: Msg39Request MUST NOT BE ON THE STACK! keep it persistent!
	bool getDocIds ( const SearchInput *si,
			 Query        *q          ,
			 void         *state      ,
			 void        (* callback) ( void *state ));

	// Msg40 calls this to get Query m_q to pass to Summary class
	Query       *getQuery()       { return m_q ; }
	const Query *getQuery() const { return m_q ; }

	// Msg40 calls these to get the data pointing into the reply
	int64_t       *getDocIds()       { return m_docIds; }
	const int64_t *getDocIds() const { return m_docIds; }
	char       *getClusterLevels()       { return m_clusterLevels; }
	const char *getClusterLevels() const { return m_clusterLevels; }
	// we basically turn the scores we get from each msg39 split into
	// floats (rscore_t) and store them as floats so that PostQueryRerank
	// has an easier time
	double       *getScores()       { return m_scores; }
	const double *getScores() const { return m_scores; }
	int32_t   getNumDocIds() const { return m_numDocIds; }
	const unsigned *getFlags() const { return m_flags; }
	DocIdScore       * const * getScoreInfos()       { return (DocIdScore * const *)m_scoreInfos; }
	const DocIdScore * const * getScoreInfos() const { return (DocIdScore * const *)m_scoreInfos; }

	void printTerms ( ) ;

	// . estimates based on m_termFreqs, m_termSigns and m_numTerms
	// . received in reply
	int64_t  getNumTotalEstimatedHits() const {
		return m_numTotalEstimatedHits; }

	// called when we got a reply of docIds
	bool gotAllShardReplies ( );

	bool mergeLists ( );

	// incoming parameters passed to Msg39::getDocIds() function
	Query     *m_q;
	int32_t       m_docsToGet;
	void      *m_state;
	void     (*m_callback ) ( void *state );

	// set by Msg3a initially
	//int32_t       m_indexdbSplit;
//	int32_t m_numHosts;
	int32_t m_numQueriedHosts;

	bool m_moreDocIdsAvail;

	// this is set if IndexTable::addLists() had an error
	int32_t       m_errno;

	// this is now in here so Msg40 can send out one Msg3a per
	// collection if it wants to search an entire token
	Msg39Request m_msg39req;

	// a multicast class to send the request, one for each split
	Multicast  m_mcast[MAX_SHARDS];

	// for timing how long things take
	int64_t  m_startTime;

	// this buffer should be big enough to hold all requests
	//char       m_request [MAX_MSG39_REQUEST_SIZE * MAX_SHARDS];
	int32_t       m_numReplies;

	int32_t m_skippedShards;

	// . # estimated total hits
	int64_t  m_numTotalEstimatedHits;

	// estimated percentage of index searched of the desired scope
	// unresponsive shards count as 0.0 toward the global estimate
	double m_pctSearched;

	// we have one request that we send to each split
	char               *m_rbufPtr;
	int32_t                m_rbufSize;
	char                m_rbuf [ RBUF_SIZE ];

	// now we send to the twin as well
	SafeBuf m_rbuf2;

	// each split gives us a reply
	class Msg39Reply   *m_reply       [MAX_SHARDS];
	int32_t                m_replyMaxSize[MAX_SHARDS];

	bool m_debug;

	// final merged lists go here
	int64_t      *m_docIds        ;
	double         *m_scores        ;
	unsigned       *m_flags;
	class DocIdScore **m_scoreInfos ;
	key96_t          *m_clusterRecs   ;
	char           *m_clusterLevels ;
	// this is new
	collnum_t      *m_collnums;
	int32_t            m_numDocIds     ;
	// the above ptrs point into this buffer
	char           *m_finalBuf;
	int32_t            m_finalBufSize;

	// when merging this list of docids into a final list keep
	// track of the cursor into m_docIds[]
	int32_t m_cursor;
};

#endif // GB_MSG3A_H
