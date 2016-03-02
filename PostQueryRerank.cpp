#include "gb-include.h"
#include "PostQueryRerank.h"
#include "Msg40.h"
#include "LanguageIdentifier.h"
#include "sort.h"
#include "Profiler.h"
#include "CountryCode.h"
#include "Phrases.h"
#include "Linkdb.h"
#include "HashTable.h"

#define TOTAL_RERANKING_TIME_STR  "PostQueryRerank Total Reranking Time" 

// Type for post query reranking weighted sort list
struct M20List {
	Msg20 *m_m20;
	rscore_t m_score;
	int64_t m_docId;
	char m_clusterLevel;
	int32_t m_numCommonInlinks;
	uint32_t m_host;
};

bool PostQueryRerank::init ( ) {
	return true;
}

PostQueryRerank::PostQueryRerank ( ) { 
	//log( LOG_DEBUG, "query:in PQR::PQR() AWL" );
	m_enabled            = false;
	m_maxResultsToRerank = 0;

	m_numToSort    = 0;
	m_m20List      = NULL;
	m_positionList = NULL;

	m_msg40 = NULL;

	//m_querysLoc = 0;

	m_maxUrlLen = 0;
	m_cvtUrl = NULL;
	m_pageUrl   = NULL;

	m_now = time(NULL);
}

PostQueryRerank::~PostQueryRerank ( ) {
	//log( LOG_DEBUG, "query:in PQR::~PQR() AWL" );
	if ( m_m20List ) {
		mfree( m_m20List, sizeof(M20List) * m_maxResultsToRerank, 
		       "PostQueryRerank" );
		m_m20List = NULL;
	}
	if ( m_positionList ) {
		mfree( m_positionList, sizeof(int32_t) * m_maxResultsToRerank,
		       "PQRPosList" );
		m_positionList = NULL;
	}

	if ( m_cvtUrl ) mfree( m_cvtUrl, m_maxUrlLen, "pqrcvtUrl") ;
	if ( m_pageUrl ) mfree( m_pageUrl, sizeof(Url)*m_maxResultsToRerank,
				"pqrpageUrls" );
}

// returns false on error 
bool PostQueryRerank::set1 ( Msg40 *msg40, SearchInput *si ) {
	//log(LOG_DEBUG, "query:in PQR::set1(%p) AWL", msg40);

	m_msg40 = msg40;
	m_si    = si;

	if ( ! m_msg40 ) return false;
	if ( ! m_si ) return false;
	if ( ! m_si->m_cr ) return false;

	m_enabled = (m_si->m_docsToScanForReranking > 1);

	return m_enabled;
}

// must be called sometime after we know numDocIds and before preRerank
// returns false if we shouldn't rerank
bool PostQueryRerank::set2 ( int32_t resultsWanted ) {
	//log(LOG_DEBUG, "query:in PQR::set2() AWL");

	//log( LOG_DEBUG, "query: firstResultNum:%"INT32"; numResults:%"INT32"; "
	//     "wanted:%"INT32" numMsg20s:%"INT32" AWL", 
	//     m_msg40->getFirstResultNum(), m_msg40->getNumResults(),
	//     resultsWanted, m_msg40->m_numMsg20s );

	// we only want to check the lessor of docsToScan and numDocIds
	m_maxResultsToRerank = m_si->m_docsToScanForReranking;
	if ( m_maxResultsToRerank > m_msg40->getNumDocIds() ) {
		m_maxResultsToRerank = m_msg40->getNumDocIds();
		log( LOG_DEBUG, "pqr: request to rerank more results "
		     "than the number of docids, capping number to rerank "
		     "at %"INT32"", m_maxResultsToRerank );
	}

	// If we don't have less results from clustering / deduping or
	// we have less results in docids then ...
	if ( m_msg40->getNumResults() < m_msg40->getNumDocIds() &&
	     m_msg40->getNumResults() < resultsWanted ) 
		return false;

	// are we passed pqr's range?
	if ( m_msg40->getFirstResultNum() > m_maxResultsToRerank ) 
		return false;

	// Safety check, make sure there are less results to rerank
	// than the number of Msg20s
	if ( m_msg40->m_numMsg20s < m_maxResultsToRerank )
		m_maxResultsToRerank = m_msg40->m_numMsg20s;

	//log( LOG_DEBUG, "query: m_maxResultsToRerank:%"INT32" AWL", 
	//     m_maxResultsToRerank );

	if ( m_maxResultsToRerank < 2 ) {
		//log( LOG_INFO, "pqr: too few results to rerank" );
		return false;
	}

	if ( m_maxResultsToRerank > 250 ) {
		log( LOG_INFO, "pqr: too many results to rerank, "
		     "capping at 250" );
		m_maxResultsToRerank = 250;
	}

	// see if we are done
	if ( m_msg40->getFirstResultNum() >= m_maxResultsToRerank ) {
		log( LOG_INFO, "pqr: first result is higher than max "
		     "results to rerank" );
		return false;
	}
	
	// get space for host count table
	m_hostCntTable.set( m_maxResultsToRerank );

	// alloc urls for pqrqttiu, pqrfsh and clustering
	m_pageUrl = (Url *)mcalloc( sizeof(Url)*m_maxResultsToRerank,
				    "pqrpageUrls" );

	if ( ! m_pageUrl ) {
		log("pqr: had out of memory error");
		return false;
	}

	return true;
}

