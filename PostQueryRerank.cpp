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

// lsort (pqrlang, pqrlangunk, pqrcntry)
// rerank for language, then country
rscore_t PostQueryRerank::rerankLanguageAndCountry ( rscore_t score, 
						 uint8_t lang,
						 uint8_t summaryLang,
						 uint16_t country ,
						     Msg20 *msg20 ) { 
	// if lsort is off, skip
	if ( ! m_si->m_enableLanguageSorting ) return score;

	// . use query lanaguage (si->m_langHint) or restricted search 
	//   language (si->m_language)
	// . if both are 0, don't rerank by language
	uint8_t langWanted = m_si->m_langHint;
	if ( langWanted == langUnknown ) langWanted = m_si->m_queryLangId;
	if ( langWanted == langUnknown ) return score;

	// . apply score factors for unknown languages, iff reranking unknown
	//   languages
	if ( lang == langUnknown &&
	     m_si->m_languageUnknownWeight > 0 ) {
		return rerankAssignPenalty(score, 
					   m_si->m_languageUnknownWeight,
					   "pqrlangunk", 
					   "it's language is unknown" );
	}

	// . if computed lanaguage is unknown, don't penalize
	// . no, what if from a different country?
	if ( summaryLang == langUnknown ) return score;
		
	// . first, apply score factors for non-preferred summary languages 
	//   that don't match the page language
	if ( summaryLang != langUnknown && summaryLang != langWanted ) {
		return rerankAssignPenalty( score, 
					    m_si->m_languageWeightFactor,
					    "pqrlang", 
					    "it's summary/title "
					    "language is foreign" );
	}
	
	// . if we got here languages of query and page match and are not
	//   unknown, so rerank based on country
	// . don't demote if countries match or either the search country
	//   or page country is unknown (0)
	// . default country wanted to gbcountry parm if not specified
	uint8_t countryWanted = m_si->m_countryHint;
	// SearchInput sets m_country based on the IP address of the incoming
	// query, which is often wrong, especially for internal 10.x.y.z ips.
	// so just fallback to countryHint for now bcause that uses teh default
	// country... right now set to "us" in search controls page.
	if ( countryWanted == 0 ) countryWanted = m_si->m_country;
	if ( country == 0 || countryWanted == 0 ||
	     country == countryWanted )
		return score;

	// . now, languages match and are not unknown and countries don't 
	//   match and neither is unknown
	// . so, demote if country of query speaks the same language as 
	//   country of page, ie US query and UK or AUS page (since all 3
	//   places speak english), but not US query and IT page
	uint64_t qLangs = g_countryCode.getLanguagesWritten( countryWanted );
	uint64_t pLangs = g_countryCode.getLanguagesWritten( country );
	// . if no language written by query country is written by page 
	//   country, don't penalize
	if ( (uint64_t)(qLangs & pLangs) == (uint64_t)0LL ) return score;

	// countries do share at least one language - demote!
	return rerankAssignPenalty( score,
				    m_si->m_cr->m_pqr_demFactCountry,
				    "pqrcntry",
				    "it's language is the same as that of "
				    "of the query, but it is from a country "
				    "foreign to that of the query which "
				    "writes in at least one of the same "
				    "languages" );
}
 
// pqrpaths
// demote pages that are not root or have many paths in the url
rscore_t PostQueryRerank::rerankPathsInUrl ( rscore_t score,
					 char *url,
					 int32_t urlLen ) {
	//log( LOG_DEBUG, "query:in PQR::rerankPathsInUrl("
	//     "score:%"INT32", url:%s)"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, url, 
	//     m_si->m_cr->m_pqr_demFactPaths,
	//     m_si->m_cr->m_pqr_maxValPaths );

	if ( urlLen == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactPaths;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValPaths;
	
	// bypass scheme and "://"
	url = strstr( url, "://" );
	if ( ! url ) return score;
	url += 3;

	// count '/'s to get number of paths
	int32_t numPaths = -1; // don't count first path
	for ( url = strchr(url, '/') ; url ; url = strchr(url, '/') ) {
		numPaths++;
		url++;
	}

	return rerankHigherDemotesMore( score, numPaths, maxVal, factor,
					"pqrpaths", "paths in its url" );
}

// pqrpgsz
// . demote page based on size. (number of words) The bigger, the 
//   more it should be demoted.
rscore_t PostQueryRerank::rerankPageSize ( rscore_t score,
				       int32_t docLen ) {
	//log( LOG_DEBUG, "query:in PQR::rerankPageSize("
	//     "score:%"INT32", docLen:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, docLen, 
	//     m_si->m_cr->m_pqr_demFactPageSize,
	//     m_si->m_cr->m_pqr_maxValPageSize );

	float factor = m_si->m_cr->m_pqr_demFactPageSize;
	if ( factor <= 0 ) return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValPageSize;

	// safety check
	if ( docLen <= 0 ) docLen = maxVal;

	return rerankHigherDemotesMore( score, docLen, maxVal, factor,
					"pqrpgsz", "page size" );
}

// pqrfsd
// setup 
bool PostQueryRerank::preRerankOtherPagesFromSameHost( Url *pageUrl ) {
	// don't do anything if this method is disabled
	if ( m_si->m_cr->m_pqr_demFactOthFromHost <= 0 ) return true;

	// don't add if no url
	if ( pageUrl->getUrlLen() == 0 ) return true;

	//log( LOG_DEBUG, "query:in PQR::preRerankOtherPagesFromSameHost() AWL");
	//log( LOG_DEBUG, "query: u:'%s' host:'%s' (%"INT32"); "
	//     "domain:'%s' (%"INT32") AWL",
	//     pageUrl->m_url,
	//     pageUrl->getHost(), pageUrl->getHostLen(),
	//     pageUrl->getDomain(), pageUrl->getDomainLen() );
	char *host = pageUrl->getDomain();
	int32_t  hostLen = pageUrl->getDomainLen();
	uint64_t key = hash64Lower_a( host, hostLen );
	if ( key == 0 ) key = 1;
	int32_t slot = m_hostCntTable.getSlot( key );
	if ( slot == -1 ) {
		m_hostCntTable.addKey( key, 0 ); // first page doesn't cnt
	}
	else {
		int32_t *cnt = m_hostCntTable.getValuePointerFromSlot( slot );
		(*cnt)++;
	}

	return true;
}

// pqrfsd
// . if page does not have any other pages from its same hostname in the
//   search results (clustered or not) then demote it. demote based on 
//   how many pages occur in the results from the same hostname. (tends 
//   to promote pages from hostnames that occur a lot in the unclustered 
//   results, they tend to be authorities) If it has pages from the same
//   hostname, they must have the query terms in different contexts, so 
//   we must get the summaries for 5 of the results, and just cluster the rest.
rscore_t PostQueryRerank::rerankOtherPagesFromSameHost ( rscore_t score,
							  Url *pageUrl ) {
	//log( LOG_DEBUG, "query:in PQR::rerankOtherPagesFromSameHost("
	//     "score:%"INT32", url:'%s', urlLen:%"INT32")"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL",
	//     score, pageUrl->getUrl(), pageUrl->getUrlLen(),
	//     m_si->m_cr->m_pqr_demFactOthFromHost,
	//     m_si->m_cr->m_pqr_maxValOthFromHost );

	if ( pageUrl->getUrlLen() == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactOthFromHost;
	if ( factor <= 0 ) return score; // disables
	int32_t maxVal = m_si->m_cr->m_pqr_maxValOthFromHost;
	if ( maxVal < 0 ) maxVal = m_numToSort-1; // all but this one

	// . lookup host for this page in hash table to get number of other 
	//   pages from the same host
	char *host = pageUrl->getDomain(); 
	int32_t  hostLen = pageUrl->getDomainLen();
	uint64_t key = hash64Lower_a( host, hostLen );
	int32_t slot = m_hostCntTable.getSlot( key );
	int32_t numFromSameHost = m_hostCntTable.getValueFromSlot( slot );
	
	//log( LOG_DEBUG, "query:  numFromSameHost:%"INT32" AWL", numFromSameHost );

	return rerankLowerDemotesMore( score, 
				       numFromSameHost, maxVal, 
				       factor,
				       "pqrfsd", 
				       "other pages from the same host" );
}

// pqrdate
// . demote pages by datedb date
rscore_t PostQueryRerank::rerankDatedbDate( rscore_t score,
					time_t datedbDate ) {
	float factor = m_si->m_cr->m_pqr_demFactDatedbDate;
	if ( factor <= 0 ) return score;
	int32_t minVal = m_si->m_cr->m_pqr_minValDatedbDate;
	if ( minVal <= 0 ) minVal = 0;
	minVal *= 1000;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValDatedbDate;
	if ( maxVal <= 0 ) maxVal = 0;
	maxVal = m_now - maxVal*1000;

	//log( LOG_DEBUG, "query:in PQR::rerankDatedbDate("
	//     "score:%"INT32", datedbDate:%"INT32")"
	//     "[P_factor:%3.3f; maxVal:%"INT32"] AWL",
	//     score, datedbDate,
	//     factor, maxVal );

	// don't penalize results whose publish date is unknown
	if ( datedbDate == -1 ) return score;
	if ( datedbDate <= minVal ) 
		return rerankAssignPenalty( score,
					    factor,
					    "pqrdate",
					    "publish date is older then "
					    "minimum value" );

	return rerankLowerDemotesMore( score,
				       datedbDate-minVal, maxVal-minVal,
				       factor,
				       "pqrdate",
				       "publish date" );
}

