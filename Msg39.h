// Matt Wells, copyright Jul 2001

// . gets the resulting docIds from a query
// . TODO: use our own facility to replace Msg2? hash a list as it comes.

#ifndef GB_MSG39_H
#define GB_MSG39_H

#include "Query.h"          // Query::set()
#include "Msg2.h"           // getLists()
#include "PosdbTable.h"
#include "TopTree.h"
#include "Msg51.h"
#include "JobScheduler.h"


class UdpSlot;

class Msg39Request {

 public:

	Msg39Request () { reset(); }

	void reset();

	// we are requesting that this many docids be returned. Msg40 requests
	// of Msg3a a little more docids than it needs because it assumes
	// some will be de-duped at summary gen time.
	int32_t    m_docsToGet;

	int32_t    m_nqt; // # of query terms
	char    m_niceness;
	int32_t    m_maxAge;
	int32_t    m_maxQueryTerms;
	int32_t    m_numDocIdSplits;
	float   m_sameLangWeight;

	//int32_t    m_compoundListMaxSize;
	uint8_t m_language;

	// flags
	char    m_queryExpansion;
	char    m_debug;
	char    m_doSiteClustering;
	char    m_hideAllClustered;
	//char    m_doIpClustering;
	char    m_doDupContentRemoval;
	char    m_addToCache;
	char    m_familyFilter;
	char    m_getDocIdScoringInfo;
	char    m_realMaxTop;
	char    m_stripe;
	char    m_useQueryStopWords;
	char    m_allowHighFrequencyTermCache;
	char    m_doMaxScoreAlgo;

	collnum_t m_collnum;

	int64_t m_minDocId;
	int64_t m_maxDocId;

	// for widget, to only get results to append to last docid
	double    m_maxSerpScore;
	int64_t m_minSerpDocId;

	// msg3a stuff
	int64_t    m_timeout; // in milliseconds

	char       m_queryId[32];

	// do not add new string parms before ptr_readSizes or
	// after ptr_whiteList so serializeMsg() calls still work
	char   *ptr_termFreqWeights;
	char   *ptr_query; // in utf8?
	char   *ptr_whiteList;
	//char   *ptr_coll;
	
	// do not add new string parms before size_readSizes or
	// after size_whiteList so serializeMsg() calls still work
	int32_t    size_termFreqWeights;
	int32_t    size_query;
	int32_t    size_whiteList;
	//int32_t    size_coll;

	// variable data comes here
};


class Msg39Reply {

public:

	// zero ourselves out
	void reset() { memset ( (char *)this,0,sizeof(Msg39Reply) ); }

	int32_t   m_numDocIds;
	// # of "unignored" query terms
	int32_t   m_nqt;
	// # of estimated hits we had
	int32_t   m_estimatedHits;
	// estimated percentage of index searched of the desired scope
	double    m_pctSearched;
	// error code
	int32_t   m_errno;

	// do not add new string parms before ptr_docIds or
	// after ptr_clusterRecs so serializeMsg() calls still work
	char  *ptr_docIds         ; // the results, int64_t
	char  *ptr_scores         ; // now doubles! so we can have intScores
	char  *ptr_scoreInfo      ; // transparency info
	char  *ptr_pairScoreBuf   ; // transparency info
	char  *ptr_singleScoreBuf ; // transparency info
	char  *ptr_clusterRecs    ; // key_t (might be empty)
	
	// do not add new string parms before size_docIds or
	// after size_clusterRecs so serializeMsg() calls still work
	int32_t   size_docIds;
	int32_t   size_scores;
	int32_t   size_scoreInfo;
	int32_t   size_pairScoreBuf  ;
	int32_t   size_singleScoreBuf;
	int32_t   size_clusterRecs;

	// variable data comes here
};


class Msg39 {
public:

	Msg39();
	~Msg39();

	// register our request handler for Msg39's
	static bool registerHandler();

private:
	static void handleRequest39(UdpSlot *slot, int32_t netnice);
	// called by handler when a request for docids arrives
	void getDocIds ( UdpSlot *slot ) ;

	void reset();
	void reset2();
	void getDocIds2();
	// retrieves the lists needed as specified by termIds and PosdbTable
	bool getLists () ;
	// called when lists have been retrieved, uses PosdbTable to hash lists
	bool intersectLists ( );

	// . this is used by handler to reconstruct the incoming Query class
	// . TODO: have a serialize/deserialize for Query class
	Query       m_query;

	// used to get IndexLists all at once
	Msg2        m_msg2;

	// holds slot after we create this Msg39 to handle a request for docIds
	UdpSlot    *m_slot;

	// . used for getting IndexList startKey/endKey/minNumRecs for each 
	//   termId we got from the query
	// . used for hashing our retrieved IndexLists
	PosdbTable m_posdbTable;

	// keep a ptr to the request
	Msg39Request *m_msg39req;

	// always use top tree now
	bool m_allocedTree;
	TopTree    m_toptree;

	//subrange chunking controls / variables
	int64_t m_ddd;
	int64_t m_dddEnd;
	
	// . we hold our IndexLists here for passing to PosdbTable
	// . one array for each of the tiers
	RdbList *m_lists;
	
	// used for timing
	int64_t  m_startTime;
	int64_t  m_startTimeQuery; //when the getDocIds2() was first called

	// this is set if PosdbTable::addLists() had an error
	int32_t       m_errno;

	int64_t  m_numTotalHits;

	int32_t        m_clusterBufSize;
	char       *m_clusterBuf;
	int64_t  *m_clusterDocIds;
	char       *m_clusterLevels;
	key_t      *m_clusterRecs;
	int32_t        m_numClusterDocIds;
	int32_t        m_numVisible;
	Msg51       m_msg51;
	bool        m_gotClusterRecs;

	static void intersectionFinishedCallback(void *state, job_exit_t exit_type);
	static void controlLoopWrapper(void *state);
	bool        controlLoop();
	static void intersectListsThreadFunction(void *state);

	int32_t m_phase;
	int32_t m_docIdSplitNumber; //next split range to do
	
	void        estimateHitsAndSendReply   ();
	bool        getClusterRecs();
	bool        gotClusterRecs ();

public:
	//debugging aid
	bool    m_inUse;
	bool    m_debug;
};		

#endif // GB_MSG39_H
