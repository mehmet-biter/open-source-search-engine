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

//#define DEBUGGING_LANGUAGE

// Type for post query reranking weighted sort list
struct M20List {
	Msg20 *m_m20;
	//int32_t m_score;
	rscore_t m_score;
	//int m_tier;
	int64_t m_docId;
	char m_clusterLevel;
	//int32_t m_bitScore;
	int32_t m_numCommonInlinks;
	uint32_t m_host;
};

static int32_t s_firstSortFunction( const M20List * a, const M20List * b );
static int32_t s_reSortFunction   ( const M20List * a, const M20List * b );
#ifdef DEBUGGING_LANGUAGE
static void DoDump(char *loc, Msg20 **m20, int32_t num, 
		   score_t *scores, char *tiers);
#endif

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
	//log( LOG_DEBUG, "query:  m_isEnabled:%"INT32"; "
	//     "P_docsToScanForReranking:%"INT32" P_pqr_docsToSan:%"INT32"; AWL",
	//     (int32_t)m_enabled, 
	//     m_si->m_docsToScanForReranking,
	//     m_si->m_cr->m_pqr_docsToScan );

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
	
	// get some space for dmoz table
	m_dmozTable.set( m_maxResultsToRerank << 1 );

	// alloc urls for pqrqttiu, pqrfsh and clustering
	m_pageUrl = (Url *)mcalloc( sizeof(Url)*m_maxResultsToRerank,
				    "pqrpageUrls" );

	if ( ! m_pageUrl ) {
		log("pqr: had out of memory error");
		return false;
	}

	return true;
}

#include "Sanity.h"

// sets up PostQueryRerank for each page in m_maxResultsToRerank
// returns false on error
bool PostQueryRerank::preRerank ( ) {
  //if ( g_conf.m_profilingEnabled ) {
  //	g_profiler
  //		.startTimer((int32_t)(this->*(&PostQueryRerank::rerank)), 
  //			    TOTAL_RERANKING_TIME_STR );
  //}
	//log( LOG_DEBUG, "query:in PQR::preRerank() AWL" );

#ifdef DEBUGGING_LANGUAGE
	DoDump( "Presort", m_msg40->m_msg20, m_maxResultsToRerank, 
		m_msg40->m_msg3a.m_scores, NULL);//m_msg40->m_msg3a.m_tiers );
#endif

	if( m_si->m_enableLanguageSorting 
	    && !m_si->m_langHint ) 
		log( LOG_INFO, "pqr: no language set for sort. "
		"language will not be reranked" );

	GBASSERT( ! m_m20List );
	m_m20List = (M20List*)mcalloc( sizeof(M20List) * m_maxResultsToRerank,
				       "PostQueryRerank" );
	if( ! m_m20List ) {
		log( LOG_INFO, "pqr: Could not allocate PostQueryRerank "
		     "sort memory.\n" );
		g_errno = ENOMEM;
		return(false);
	}
	GBASSERT( ! m_positionList );
	m_positionList = (int32_t *)mcalloc( sizeof(int32_t) * m_maxResultsToRerank,
					  "PQRPosList" );
	if( ! m_positionList ) {
		log( LOG_INFO, "pqr: Could not allocate PostQueryRerank "
		     "postion list memory.\n" );
		g_errno = ENOMEM;
		return(false);
	}

	//log(LOG_DEBUG, "pqr: the query is '%s' AWL", m_si->m_q->m_orig);

	// setup for rerankNonLocationSpecificQueries if enabled
	//if ( ! preRerankNonLocationSpecificQueries() )
	//	return false;

	// . make a temp hash table for iptop
	// . each slot is a int32_t key and a int32_t value
	HashTable ipTable;
	// how many slots
	int32_t numSlots = 5000 / ((4+4)*4);
	char tmp[5000];
	// this should NEVER need to allocate, UNLESS for some reason we got
	// a ton of inlinking ips
	if ( ! ipTable.set ( numSlots , tmp , 5000 ) ) return false;
	// this table maps a docid to the number of search results it links to
	HashTableT <int64_t, int32_t> inlinkTable;
	char tmp2[5000];
	int32_t numSlots2 = 5000 / ((8+4)*4);
	if ( ! inlinkTable.set ( numSlots2 , tmp2 , 5000 ) ) return false;

	// Fill sort array
	int32_t y = 0;
	for( int32_t x = 0; 
	     x < m_msg40->m_numMsg20s && y < m_maxResultsToRerank;
	     x++ ) {
		// skip clustered out results
		char clusterLevel = m_msg40->getClusterLevel( x );
		if ( clusterLevel != CR_OK ) {
			//log( LOG_DEBUG, "pqr: skipping result "
			//     "%"INT32" since cluster level(%"INT32") != "
			//     "CR_OK(%"INT32") AWL",
			//     x, (int32_t)clusterLevel, (int32_t)CR_OK );
			continue;
		}
		// skip results that don't match all query terms
		//int32_t bitScore = m_msg40->getBitScore( x );
		//if ( bitScore == 0x00 ) continue;

		// . save postion of this result so we can fill it in later
		//   with (possibly) a higher ranking result
		m_positionList[y] = x;

		M20List *sortArrItem = &m_m20List [ y ];
		sortArrItem->m_clusterLevel = clusterLevel                  ;
		sortArrItem->m_m20          = m_msg40->m_msg20         [ x ];
		sortArrItem->m_score        = (rscore_t)m_msg40->getScore(x);
		//sortArrItem->m_tier         = m_msg40->getTier         ( x );
		sortArrItem->m_docId        = m_msg40->getDocId        ( x );
		//sortArrItem->m_bitScore     = bitScore                      ;
		sortArrItem->m_host         = 0; // to be filled in later

		Msg20 *msg20 = sortArrItem->m_m20;
		GBASSERT( msg20 && ! msg20->m_errno );

		Msg20Reply *mr = msg20->m_r;

		// set the urls for each page
		// used by pqrqttiu, pqrfsh and clustering
		m_pageUrl[y].set( mr->ptr_ubuf , false );
		// now fill in host without the 'www.' if present
		char *host    = m_pageUrl[y].getHost();
		int32_t  hostLen = m_pageUrl[y].getHostLen();
		if (hostLen > 4 &&
		    host[3] == '.' &&
		    host[0] == 'w' && host[1] == 'w' && host[2] == 'w')
			sortArrItem->m_host = hash32(host+4, hostLen-4);
		else
			sortArrItem->m_host = hash32(host, hostLen);

		// add its inlinking docids into the hash table, inlinkTable
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		//int32_t       n         = msg20->getNumInlinks      ();
		//int64_t *docIds    = msg20->getInlinkDocIds    ();
		//char      *flags     = msg20->getInlinkFlags     ();
		//int32_t      *ips       = msg20->getInlinkIps       ();
		//char      *qualities = msg20->getInlinkQualities ();
		// skip adding the inlinking docids if search result has bad ip
		int32_t ip = mr->m_ip;//msg20->getIp();
		bool good = true;
		if ( ip ==  0 ) good = false;
		if ( ip == -1 ) good = false;
		// . skip inlinker add already did this "ip top"
		// . "ip top" is the most significant 3 bytes of the ip
		// . get the ip of the docid:
		int32_t top = iptop ( ip );
		// if we already encountered a higher-scoring search result
		// with the same iptop, do not count its inlinkers!
		// so that if an inlinker links to two docids in the search 
		// results, where those two docids are from the same 
		// "ip top" then the docid is only "counted" once here.
		if ( ipTable.getSlot ( top ) >= 0 ) good = false;
		// not allowed to be 0
		if ( top == 0 ) top = 1;
		// now add to table so no we do not add the inlinkers from
		// any other search results from the same "ip top"
		if ( ! ipTable.addKey ( top , 1 ) ) return false;
		// now hash all the inlinking docids into inlinkTable
		for ( Inlink *k=NULL; good && (k=info->getNextInlink(k) ) ; ) {
			// lower score if it is link spam though
			if ( k->m_isLinkSpam ) continue;
			// must be quality of 35 or higher to "vote"
			//if ( k->m_docQuality < 35 ) continue;
			if ( k->m_siteNumInlinks < 20 ) continue;
			// skip if bad ip for inlinker
			if ( k->m_ip == 0 || k->m_ip == -1 ) continue;
			// skip if inlinker has same top ip as search result
			if ( iptop(k->m_ip) == top ) continue;
			// get the current slot in table from docid of inlinker
			int32_t slot = inlinkTable.getSlot ( k->m_docId );
			// get the score
			if ( slot >= 0 ) {
				int32_t count=inlinkTable.getValueFromSlot(slot);
				inlinkTable.setValue ( slot , count + 1 );
				continue;
			}
			// add it fresh if not already in there
			if (!inlinkTable.addKey(k->m_docId,1)) return false;
		}

		//log( LOG_DEBUG, "pqr: pre: setting up sort array - "
		//     "mapping x:%"INT32" to y:%"INT32"; "
		//     "url:'%s' (%"INT32"); tier:%d; score:%"INT32"; "
		//     "docId:%lld; clusterLevel:%d; AWL",
		//     x, y, 
		//     msg20->getUrl(), msg20->getUrlLen(), 
		//     sortArrItem->tier, sortArrItem->score,
		//     sortArrItem->docId, sortArrItem->clusterLevel );

		// setup reranking for pages from the same host (pqrfsd)
		if ( ! preRerankOtherPagesFromSameHost( &m_pageUrl[y] ))
			return false;

		// . calculate maximum url length in pages for reranking 
		//   by query terms or topics in a url
		int32_t urlLen = mr->size_ubuf - 1;//msg20->getUrlLen();
		if ( urlLen > m_maxUrlLen )
			m_maxUrlLen = urlLen;

		// update num to rerank and sort
		m_numToSort++;
		y++;
	}

	// get the max
	m_maxCommonInlinks = 0;
	// how many of OUR inlinkers are shared by other results?
	for ( int32_t i = 0; i < m_numToSort; i++ ) {
		// get the item
		M20List *sortArrItem = &m_m20List [ i ];
		Msg20 *msg20 = sortArrItem->m_m20;
		// reset
		sortArrItem->m_numCommonInlinks = 0;
		// lookup its inlinking docids in the hash table
		//int32_t       n      = msg20->getNumInlinks   ();
		//int64_t *docIds = msg20->getInlinkDocIds ();
		LinkInfo *info = (LinkInfo *)msg20->m_r->ptr_linkInfo;
		for ( Inlink *k=NULL;info&&(k=info->getNextInlink(k)) ; ) {
			// how many search results does this inlinker link to?
			int32_t*v=(int32_t *)inlinkTable.getValuePointer(k->m_docId);
			if ( ! v ) continue;
			// if only 1 result had this as an inlinker, skip it
			if ( *v <= 1 ) continue;
			// ok, give us a point
			sortArrItem->m_numCommonInlinks++;
		}
		// get the max
		if ( sortArrItem->m_numCommonInlinks > m_maxCommonInlinks )
			m_maxCommonInlinks = sortArrItem->m_numCommonInlinks;
	}


	// . setup reranking for query terms or topics in url (pqrqttiu)
	// . add space to max url length for terminating NULL and allocate
	//   room for max length
	m_maxUrlLen++;
	m_cvtUrl = (char *)mmalloc( m_maxUrlLen, "pqrcvtUrl" );
	if ( ! m_cvtUrl ) {
		log( LOG_INFO, "pqr: Could not allocate %"INT32" bytes "
		     "for m_cvtUrl.",
		     m_maxUrlLen );
		g_errno = ENOMEM;
		return false;
	}

	// Safety valve, trim sort results
	if ( m_numToSort > m_maxResultsToRerank )
		m_numToSort = m_maxResultsToRerank;

	//log( LOG_DEBUG, "pqr::m_numToSort:%"INT32" AWL", m_numToSort );

	return true;
}

// perform actual reranking of m_numToSort pages
// returns false on error
bool PostQueryRerank::rerank ( ) {
	//log(LOG_DEBUG,"query:in PQR::rerank() AWL");
	if(m_si->m_debug||g_conf.m_logDebugPQR )
		logf( LOG_DEBUG, "pqr: reranking %"INT32" results", 
		     m_numToSort );

	/*
	float maxDiversity = 0;
	if(m_si->m_pqr_demFactSubPhrase > 0) {
		for ( int32_t x = 0; x < m_numToSort; x++ ) {
			M20List *sortArrItem = &m_m20List [ x ];
			Msg20 *msg20 = sortArrItem->m_m20;
			if ( ! msg20 || msg20->m_errno ) continue;
			float d = msg20->m_r->m_diversity;
			if(d > maxDiversity) maxDiversity = d;
		}
	}

	float maxProximityScore = 0;
	float minProximityScore = -1.0;
	//float maxInSectionScore = 0;
	if(m_si->m_pqr_demFactProximity > 0 ||
	   m_si->m_pqr_demFactInSection > 0) {
		//grab the max score so that we know what the max to 
		//demote is.
		for ( int32_t x = 0; x < m_numToSort; x++ ) {
			M20List *sortArrItem = &m_m20List [ x ];
			Msg20 *msg20 = sortArrItem->m_m20;
			if ( ! msg20 || msg20->m_errno ) continue;
			//float d = msg20->m_r->m_inSectionScore;
			//if(d > maxInSectionScore) 
			//	maxInSectionScore = d;
			// handle proximity
			float d = msg20->m_r->m_proximityScore;
			// i think this means it does not have all the query 
			// terms! for 'sylvain segal' we got 
			// www.regalosdirectos.tv/asp2/comparar.asp?cat=36 
			// in results
			if ( d == 0.0 ) continue;
			// . -1 is a bogus proximity
			// . it means we were not able to find all the terms
			//   because they were in anomalous link text or
			//   meta tags or select tags or whatever... so for
			//   now such results will not be demoted to be on the
			//   safe side
			if ( d == -1.0 ) continue;
			if ( d > maxProximityScore ) 
				maxProximityScore = d;
			if ( d < minProximityScore || minProximityScore==-1.0 )
				minProximityScore = d;
		}
	}
	*/


	// rerank weighted sort list
	for ( register int32_t x = 0; x < m_numToSort; x++ ) {
		M20List *sortArrItem = &m_m20List [ x ];
		Msg20 *msg20 = sortArrItem->m_m20;
		char *url = NULL;
		rscore_t score = sortArrItem->m_score;
		rscore_t startScore = score;

		// mwells: what is this?
 		if(m_si->m_pqr_demFactOrigScore < 1) {
 		//turn off the indexed score and just use a uniform start score
 		//because I can't get the proximity pqr to overwhelm the 
 		//preexisting score.
 			score = 1000000 + (m_numToSort - x) + 
				(int32_t)(score * m_si->m_pqr_demFactOrigScore);
			startScore = score;
 		}

		// if don't have a good msg20, skip reranking for this result
		if ( ! msg20 || msg20->m_errno )
			continue;

		url = msg20->m_r->ptr_ubuf;//getUrl();
		if ( ! url ) url = "(none)";
		if(m_si->m_debug||g_conf.m_logDebugPQR )
			logf(LOG_DEBUG, "pqr: result #%"INT32":'%s' has initial "
			     "score of %.02f", 
			     x, url, (float)startScore );

		// resets
		msg20->m_pqr_old_score        = score;
		msg20->m_pqr_factor_quality   = 1.0;
		msg20->m_pqr_factor_diversity = 1.0;
		msg20->m_pqr_factor_inlinkers = 1.0;
		msg20->m_pqr_factor_proximity = 1.0;
		msg20->m_pqr_factor_ctype     = 1.0;
		msg20->m_pqr_factor_lang      = 1.0; // includes country

		Msg20Reply *mr = msg20->m_r;

		// demote for language and country
		score =	rerankLanguageAndCountry( score,
						  mr->m_language ,
						  mr->m_summaryLanguage,
						  mr->m_country, // id
						  msg20 );

		// demote for content-type
		float htmlFactor = m_si->m_cr->m_pqr_demFactNonHtml;
		float xmlFactor  = m_si->m_cr->m_pqr_demFactXml;
		int32_t  contentType= mr->m_contentType;
		if ( contentType == CT_XML && xmlFactor > 0 ) {
			score = score * xmlFactor;
			msg20->m_pqr_factor_ctype = xmlFactor;
		}
		else if ( contentType != CT_HTML && htmlFactor > 0 ) {
			score = score * htmlFactor;
			msg20->m_pqr_factor_ctype = htmlFactor;
		}
		//if ( score == 1 ) goto finishloop;

		// demote for fewer query terms or gigabits in url
		//score =	rerankQueryTermsOrGigabitsInUrl( score,
		//					 &m_pageUrl[x] );
		

		// . demote for not high quality
		// . multiply by "qf" for every quality point below 100
		// . now we basically do this if we have a wiki title
		// . float qf = m_si->m_cr->m_pqr_demFactQual;
		/*
		if ( m_msg40->m_msg3a.m_oneTitle ) {
			//int32_t q = msg20->getQuality();
			int32_t sni = mr->m_siteNumInlinks;
			if ( sni <= 0 ) sni = 1;
			float weight = 1.0;
			for ( ; sni < 100000 ; sni *= 2 ) 
				weight = weight * 0.95;
			// apply the weight to the score
			score = score * weight;
			// store that for print in PageResults.cpp
			msg20->m_pqr_factor_quality = weight;
		}
		*/

		// demote for more paths in url
		score = rerankPathsInUrl( score,
					  msg20->m_r->ptr_ubuf,//getUrl(),
					  msg20->m_r->size_ubuf-1 );

		// demote for larger page sizes
		score = rerankPageSize( score,
					msg20->m_r->m_contentLen );

		// demote for no cat id
		score = rerankNoCatId( score,
				       msg20->m_r->size_catIds/4,
				       msg20->m_r->size_indCatIds/4);

		// demote for no other pages from same host
		score = rerankOtherPagesFromSameHost( score,
						      &m_pageUrl[x] );

		// . demote pages for older datedb dates
		score = rerankDatedbDate( score,
					  msg20->m_r->m_datedbDate );

		/*
		// . demote pages by proximity
		// . a -1 prox implies did not have any query terms
		// . see Summary.cpp proximity algo
		float ps = msg20->m_r->m_proximityScore;//getProximityScore();
		if ( ps > 0.0 && 
		     m_si->m_pqr_demFactProximity > 0 &&
		     minProximityScore != -1.0 ) {
			// what percent were we of the max?
			float factor = minProximityScore / ps ;
			// this can be weighted
			//factor *= m_si->m_pqr_demFactProximity;
			// apply the factor to the score
			score *= factor;
			// this is the factor
			msg20->m_pqr_factor_proximity = factor;
		}

		// . demote pages by the average of the scores of the
		// . terms based upon what section of the doc they are in
		// . mdw: proximity algo should obsolete this
		//if(maxInSectionScore > 0)
		//	score = rerankInSection( score,
		//				 msg20->getInSectionScore(),
		//				 maxInSectionScore);


		// . demote pages which only have the query as a part of a
		// . larger phrase
		if ( maxDiversity != 0 ) {
			float diversity = msg20->m_r->m_diversity;
			float df = (1 - (diversity/maxDiversity)) *
				m_si->m_pqr_demFactSubPhrase;
			score = (rscore_t)(score * (1.0 - df));
			if ( score <= 0.0 ) score = 0.001;
			msg20->m_pqr_factor_diversity = 1.0 - df;
		}
		*/

		// . COMMON INLINKER RERANK
		// . no need to create a superfluous function call here
		// . demote pages that do not share many inlinking docids
		//   with other pages in the search results
		if ( m_maxCommonInlinks>0 && m_si->m_pqr_demFactCommonInlinks){
			int32_t nc = sortArrItem->m_numCommonInlinks ;
			float penalty;
			// the more inlinkers, the less the penalty
			penalty = 1.0 -(((float)nc)/(float)m_maxCommonInlinks);
			// . reduce the penalty for higher quality pages
			// . they are the most likely to have their inlinkers 
			//   truncated
			//char quality = msg20->getQuality();
			float sni = (float)msg20->m_r->m_siteNumInlinks;
			// decrease penalty for really high quality docs
			//while ( quality-- > 60 ) penalty *= .95;
			for ( ; sni > 1000 ; sni *= .80 ) penalty *= .95;
			// if this parm is 0, penalty will become 0
			penalty *= m_si->m_pqr_demFactCommonInlinks;
			// save old score
			score = score * (1.0 - penalty);
			// do not decrease all the way to 0!
			if ( score <= 0.0 ) score = 0.001;
			// store it!
			msg20->m_pqr_factor_inlinkers = 1.0 - penalty;
		}

		//	finishloop:
		if(m_si->m_debug || g_conf.m_logDebugPQR )
			logf( LOG_DEBUG, "pqr: result #%"INT32"'s final "
			     "score is %.02f (-%3.3f%%) ",
			     x, (float)score,100-100*(float)score/startScore );
		sortArrItem->m_score = score;
	}

	return(true);
}

// perform post reranking tasks 
// returns false on error 
bool PostQueryRerank::postRerank ( ) {
	//log( LOG_DEBUG, "query:in PQR::postRerank() AWL" );

	// Hopefully never happen...
	//log( LOG_DEBUG, "query: just before sort: "
	//     "m_maxResultsToRerank:%"INT32" m_numToSort:%"INT32" AWL", 
	//     m_maxResultsToRerank, m_numToSort);
	if ( m_numToSort < 0 ) return false;

	// Sort the array
	gbmergesort( (void *) m_m20List, (size_t) m_numToSort, 
		     (size_t) sizeof(M20List),
		     (int (*)(const void *, const void *))s_firstSortFunction);

	// move 2nd result from a particular domain to just below the first
	// result from that domain if it is within 10 results of the first
	//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX put this back in after debugging summary rerank!
	//XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
	//if (!attemptToCluster()) return false;

	// Fill result arrays with our reranked results
	for( int32_t y = 0; y < m_numToSort; y++ ) {
		M20List *a = &m_m20List     [ y ];
		int32_t     x = m_positionList [ y ];
		m_msg40->m_msg20                 [ x ] = a->m_m20;
		//m_msg40->m_msg3a.m_tiers         [ x ] = a->m_tier;
		m_msg40->m_msg3a.m_scores        [ x ] = a->m_score;
		m_msg40->m_msg3a.m_docIds        [ x ] = a->m_docId;
		m_msg40->m_msg3a.m_clusterLevels [ x ] = a->m_clusterLevel;
		//log( LOG_DEBUG, "pqr: post: mapped y:%"INT32" "
		//     "to x:%"INT32" AWL",
		//     y, x );
	}

#ifdef DEBUGGING_LANGUAGE
	DoDump( "Postsort", m_msg40->m_msg20, m_numToSort, 
	        m_msg40->m_msg3a.m_scores, NULL );//m_msg40->m_msg3a.m_tiers );
#endif

	//if ( ! g_conf.m_profilingEnabled ) return true;
	//if ( ! g_profiler.endTimer( (int32_t)(this->*(&PostQueryRerank::rerank)), 
	//			    TOTAL_RERANKING_TIME_STR) )
	//	log( LOG_WARN,"admin: Couldn't add the fn %"INT32"",
	//	     (int32_t)(this->*(&PostQueryRerank::rerank)) );
	return true;
}

// called if we weren't able to rerank for some reason
void PostQueryRerank::rerankFailed ( ) {
  //if ( g_conf.m_profilingEnabled ) {
  //	if( ! g_profiler
  //	    .endTimer( (int32_t)(this->*(&PostQueryRerank::rerank)), 
  //		       TOTAL_RERANKING_TIME_STR) )
  //		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
  //		    (int32_t)(this->*(&PostQueryRerank::rerank)));
  //}
}

// lsort (pqrlang, pqrlangunk, pqrcntry)
// rerank for language, then country
rscore_t PostQueryRerank::rerankLanguageAndCountry ( rscore_t score, 
						 uint8_t lang,
						 uint8_t summaryLang,
						 uint16_t country ,
						     Msg20 *msg20 ) { 
	//log( LOG_DEBUG, "query:in PQR::rerankLanguageAndCountry("
	//     "score:%"INT32", lang:%"INT32", summLang:%"INT32", country:%"INT32")"
	//     "[langSortingIsOn:%"INT32"; langUnkWeight:%3.3f; langWeight:%3.3f; "
	//     "&qlang=%"INT32"; &lang=%"INT32"; "
	//     "&qcountry=%"INT32"; &gbcountry=%"INT32"; "
	//     "queryLangs:%lld; pageLangs:%lld] AWL",
	//     score, (int32_t)lang, (int32_t)summaryLang, (int32_t)country,
	//     (int32_t)m_si->m_enableLanguageSorting,
	//     m_si->m_languageUnknownWeight,
	//     m_si->m_languageWeightFactor,
	//     (int32_t)m_si->m_langHint,
	//     (int32_t)m_si->m_language,
	//     (int32_t)m_si->m_countryHint,
	//     (int32_t)m_si->m_country,
	//     g_countryCode.getLanguagesWritten( m_si->m_countryHint ),
	//     g_countryCode.getLanguagesWritten( country ) );
	
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
		msg20->m_pqr_factor_lang =m_si->m_languageUnknownWeight;
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
		msg20->m_pqr_factor_lang = m_si->m_languageWeightFactor;
		return rerankAssignPenalty( score, 
					    m_si->m_languageWeightFactor,
					    "pqrlang", 
					    "it's summary/title "
					    "language is foreign" );
	}
	
	// second, apply score factors for non-preferred page languages
	//if ( lang != langWanted )
	//	return rerankAssignPenalty( score, 
	//				    m_si->m_languageWeightFactor,
	//				    "pqrlang", 
	//				    "it's page language is foreign" );
	
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

	msg20->m_pqr_factor_lang = m_si->m_cr->m_pqr_demFactCountry;

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
 
// pqrqttiu
// . look for query terms and gigabits in the url, demote more the fewer 
//   are matched.
/*
rscore_t PostQueryRerank::rerankQueryTermsOrGigabitsInUrl( rscore_t score,
							    Url *pageUrl ) {
	//log( LOG_DEBUG, "query:in PQR::rerankQueryTermsOrGigabitsInUrl("
	//     "score:%"INT32", url:'%s', urlLen:%"INT32")"
	//     "[factor:%3.3f; max:%"INT32"] AWL", 
	//     score, pageUrl->getUrl(), pageUrl->getUrlLen(),
	//     m_si->m_cr->m_pqr_demFactQTTopicsInUrl,
	//     m_si->m_cr->m_pqr_maxValQTTopicsInUrl );

	if ( pageUrl->getUrlLen() == 0 ) return score;

	float factor = m_si->m_cr->m_pqr_demFactQTTopicsInUrl;
	if ( factor <= 0 ) return score; // disables
	int32_t maxQTInUrl = m_si->m_q->getNumTerms(); 
	int32_t maxGigabitsInUrl = m_msg40->getNumTopics();
	int32_t maxVal = m_si->m_cr->m_pqr_maxValQTTopicsInUrl;
	if ( maxVal < 0 ) maxVal = maxQTInUrl+maxGigabitsInUrl; 

	// from original url:
	// . remove scheme
	// . remove 'www' from host
	// . remove tld
	// . remove ext
	// . convert symbols to spaces
	// . remove extra space
	//log( LOG_DEBUG, "query: origurl:'%s' AWL", pageUrl->getUrl() );
	//log( LOG_DEBUG, "query: url: whole:'%s' host:'%s' (%"INT32"); "
	//     "domain:'%s' (%"INT32"); tld:'%s' (%"INT32"); midDom:'%s' (%"INT32"); "
	//     "path:'%s' (%"INT32"); fn:'%s'; ext:'%s'; query:'%s' (%"INT32"); "
	//     "ipStr:'%s' {%"INT32"}; anch:'%s' (%"INT32") "
	//     "site:'%s' (%"INT32") AWL",
	//     pageUrl->getUrl(),
	//     pageUrl->getHost(), pageUrl->getHostLen(),
	//     pageUrl->getDomain(), pageUrl->getDomainLen(),
	//     pageUrl->getTLD(), pageUrl->getTLDLen(),
	//     pageUrl->getMidDomain(),  pageUrl->getMidDomainLen(),
	//     pageUrl->getPath(), pageUrl->getPathLen(),
	//     pageUrl->getFilename(), pageUrl->getExtension(), 
	//     pageUrl->getQuery(), pageUrl->getQueryLen(),
	//     pageUrl->getIpString(), pageUrl->getIp(),
	//     pageUrl->getAnchor(), pageUrl->getAnchorLen(),
	//     pageUrl->getSite(), pageUrl->getSiteLen() );
	m_cvtUrl[0] = '\0';
	int32_t cvtUrlLen = 0;
	char *host = pageUrl->getHost();
	// first, add hostname - "www." iff it is not an ip addr
	if ( pageUrl->getIp() == 0 ) {
		if ( host[0] == 'w' && host[1] == 'w' && host[2] == 'w' && 
		     host[3] == '.' ) {
			// if starts with 'www.', don't add the 'www.'
			if(pageUrl->getHostLen()-pageUrl->getDomainLen() == 4){
				// add domain - 'www.' - tld 
				strncpy( m_cvtUrl, pageUrl->getDomain(), 
					 pageUrl->getDomainLen() - 
					 pageUrl->getTLDLen() );
				cvtUrlLen += pageUrl->getDomainLen() - 
					pageUrl->getTLDLen();
				m_cvtUrl[cvtUrlLen] = '\0';
			}
			else {
				// add host + domain - 'www.' - tld
				strncpy( m_cvtUrl, pageUrl->getHost()+4,
					 pageUrl->getHostLen() - 
					 pageUrl->getTLDLen() - 4 );
				cvtUrlLen += pageUrl->getHostLen() - 
					pageUrl->getTLDLen() - 4;
				m_cvtUrl[cvtUrlLen] = '\0';
			}
		}
		else {
			// add host + domain - tld
			strncpy( m_cvtUrl, pageUrl->getHost(),
				 pageUrl->getHostLen() - 
				 pageUrl->getTLDLen() - 1 );
			cvtUrlLen += pageUrl->getHostLen() - 
				pageUrl->getTLDLen() - 1;
			m_cvtUrl[cvtUrlLen] = '\0';
		}
		
	}
	// next, add path
	if ( pageUrl->getPathLen() > 0 ) {
		strncat( m_cvtUrl, pageUrl->getPath(), 
			 pageUrl->getPathLen()-pageUrl->getExtensionLen() );
		cvtUrlLen += pageUrl->getPathLen()-pageUrl->getExtensionLen();
		m_cvtUrl[cvtUrlLen] = '\0';
	}
	// next, add query
	if ( pageUrl->getQueryLen() > 0 ) {
		strncat( m_cvtUrl, pageUrl->getQuery(), pageUrl->getQueryLen() );
		cvtUrlLen += pageUrl->getQueryLen();
		m_cvtUrl[cvtUrlLen] = '\0';
	}
	// remove all non-alpha-numeric chars
	char *t = m_cvtUrl;
	for ( char *s = m_cvtUrl; *s; s++ ) {
		if ( is_alnum_a(*s) ) *t++ = *s;
		else if ( t>m_cvtUrl && *(t-1) != ' ' ) *t++ = ' ';
	}
	*t = '\0';
	cvtUrlLen = (t-m_cvtUrl);
	//log( LOG_DEBUG, "query:  m_cvtUrl:'%s' (%"INT32") AWL", 
	//     m_cvtUrl, cvtUrlLen );

	// find number of query terms in url
	int32_t numQTInUrl = 0;
	int32_t numQTs = m_si->m_q->getNumTerms();
	for ( int32_t i = 0; i < numQTs; i++ ) {
		char *qtStr = m_si->m_q->getTerm(i);
		int32_t  qtLen = m_si->m_q->getTermLen(i);
		if ( strncasestr(m_cvtUrl, qtStr, cvtUrlLen, qtLen) != NULL ) {
			numQTInUrl++;
			//log( LOG_DEBUG, "query:  qt is in url AWL");
		}
	}

	// find number of gigabits in url
	int32_t numGigabitsInUrl = 0;
	int32_t numTopics = m_msg40->getNumTopics();
	for ( int32_t i = 0; i < numTopics; i++ ) {
		char *topicStr = m_msg40->getTopicPtr(i);
		int32_t  topicLen = m_msg40->getTopicLen(i);
		if ( strncasestr(m_cvtUrl, topicStr, cvtUrlLen, topicLen) ) {
			numGigabitsInUrl++;
			//log( LOG_DEBUG, "query:  topic is in url AWL");
		}
	} 

	//log( LOG_DEBUG, "query:  qts:%"INT32", gigabits:%"INT32"; "
	//     "maxQTInUrl:%"INT32", maxGbInUrl:%"INT32" AWL",
	//     numQTInUrl, numGigabitsInUrl,
	//     maxQTInUrl, maxGigabitsInUrl );
	return rerankLowerDemotesMore( score, 
				       numQTInUrl+numGigabitsInUrl,
				       maxVal,
				       factor,
				       "pqrqttiu", 
				       "query terms or topics in its url" );
}
*/

// pqrqual
// demote pages that are not high quality
/*
rscore_t PostQueryRerank::rerankQuality ( rscore_t score, 
				      unsigned char quality ) {
	//log( LOG_DEBUG, "query:in PQR::rerankQuality("
	//     "score:%"INT32", quality:%d)"
	//     "[P_factor:%3.3f; P_max:%"INT32"] AWL", 
	//     score, (int)quality, 
	//     m_si->m_cr->m_pqr_demFactQual,
	//     m_si->m_cr->m_pqr_maxValQual );

	float factor = m_si->m_cr->m_pqr_demFactQual;
	if ( factor <= 0 ) return score;
	int32_t maxVal = m_si->m_cr->m_pqr_maxValQual;
	if ( maxVal < 0 ) maxVal = 100;

	return rerankLowerDemotesMore( score, quality, maxVal, factor,
				       "pqrqual", "quality" );
}
*/

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

// pqrcatid
// demote page if does not have a catid
rscore_t PostQueryRerank::rerankNoCatId ( rscore_t score, 
				      int32_t numCatIds,
				      int32_t numIndCatIds ) {
	//log( LOG_DEBUG, "AWL:in PQR::rerankNoCatId("
	//     "score:%"INT32", numCatIds:%"INT32", numIndCatIds:%"INT32")"
	//     "[P_factor:%3.3f]",
	//     score, numCatIds, numIndCatIds,
	//     m_si->m_cr->m_pqr_demFactNoCatId );

	float factor = m_si->m_cr->m_pqr_demFactNoCatId;
	if ( factor <= 0 ) return score; // disables

	if ( numCatIds + numIndCatIds > 0 ) return score;
	
	return rerankAssignPenalty( score, factor, 
				    "pqrcatid", "it has no category id" );
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

// pqrprox
// . demote pages by the average distance of query terms from
// . one another in the document.  Lower score is better.
/*
rscore_t PostQueryRerank::rerankProximity( rscore_t score,
				       float proximityScore,
				       float maxScore) {
	// . a -1 implies did not have any query terms
	// . see Summary.cpp proximity algo
	if ( proximityScore == -1 ) return 0;
	if(m_si->m_pqr_demFactProximity <= 0) return score;
	float factor = (// 1 -
			(proximityScore/maxScore)) *
		m_si->m_pqr_demFactProximity;
	if ( factor <= 0 ) return score;
	//return rerankAssignPenalty(score, 
	//			   factor,
	//			   "pqrprox",
	//			   "proximity rerank");
	// just divide the score by the proximityScore now

	// ...new stuff...
	if ( proximityScore == 0.0 ) return score;
	float score2 = (float)score;
	score2 /= proximityScore;
	score2 += 0.5;
	rscore_t newScore = (rscore_t)score2;
	if(m_si->m_debug || g_conf.m_logDebugPQR )
		logf( LOG_DEBUG, "query: pqr: result demoted "
		      "from %.02f to %.02f becaose of proximity rerank",
		      (float)score,(float)newScore);
	return newScore;
}
*/

// pqrinsec
// . demote pages by the average of the score of the sections
// . in which the query terms appear in.  Higher score is better.
rscore_t PostQueryRerank::rerankInSection( rscore_t score,
				       int32_t summaryScore,
				       float maxScore) {
	if(m_si->m_pqr_demFactInSection <= 0) return score;
	float factor = ( 1 -
			 (summaryScore/maxScore)) *
		m_si->m_pqr_demFactInSection;
	if ( factor <= 0 ) return score;
 	return rerankAssignPenalty(score, 
 				   factor,
 				   "pqrsection",
 				   "section rerank");
}


/*
rscore_t PostQueryRerank::rerankSubPhrase( rscore_t score,
				       float diversity,
				       float maxDiversity) {
	if(maxDiversity == 0) return score;
	float factor = (1 - (diversity/maxDiversity)) *
		m_si->m_pqr_demFactSubPhrase;
	if ( factor <= 0 ) return score;
	return rerankAssignPenalty(score, 
				   factor,
				   "pqrspd",
				   "subphrase demotion");

}
*/

bool PostQueryRerank::attemptToCluster ( ) {
	// find results that should be clustered
	bool                       needResort   = false;
	HashTableT<uint32_t, int32_t> hostPosTable;
	hostPosTable.set(m_numToSort);
	for (int32_t i = 0; i < m_numToSort; i++) {
		// look up this hostname to see if it's been clustered
		uint32_t key     = m_m20List[i].m_host;
		if ( key == 0 ) key = 1;
		int32_t     slot    = hostPosTable.getSlot(key);
		if (slot != -1) {
			// see if we are within 10 results of first result
			// from same host
			int32_t firstPos = hostPosTable.getValueFromSlot(slot);
			if (i - firstPos > 1 && i - firstPos < 10) {
				// this result can be clustered
				rscore_t maxNewScore;
				maxNewScore = m_m20List[firstPos].m_score;
				if (maxNewScore <= m_m20List[i].m_score)
					continue;
				needResort = true;
				if(m_si->m_debug||g_conf.m_logDebugPQR )
					logf(LOG_DEBUG, "pqr: re-ranking result "
					     "%"INT32" (%s) from score %.02f to "
					     "score %.02f "
					     "in order to cluster it with "
					     "result "
					     "%"INT32" (%s)",
					     i, 
					     m_m20List[i].m_m20->m_r->ptr_ubuf,
					     (float)m_m20List[i].m_score, 
					     (float)maxNewScore,
					     firstPos, 
					     m_m20List[firstPos].m_m20->m_r->ptr_ubuf);
				// bump up the score to cluster this result
				m_m20List[i].m_score = maxNewScore;
			}
			else {
				hostPosTable.setValue(slot, i);
			}
		}
		else {
			// add the hostname of this result to the table
			if (!hostPosTable.addKey(key, i)) {
				g_errno = ENOMEM;
				return false;
			}
		}
	}

	// re-sort the array if necessary
	if (needResort) {
		log(LOG_DEBUG, "pqr: re-sorting results for clustering");
		gbmergesort( (void *) m_m20List, (size_t) m_numToSort, 
			     (size_t) sizeof(M20List),
			     (int (*)(const void *, const void *))s_reSortFunction);
	}

	return true;
}

// Sort function for post query reranking's M20List
static int32_t s_firstSortFunction(const M20List * a, const M20List * b)
{
	// Sort by tier first, then score
	// When sorting by tier, an explicit match (0x40) in a higher tier 
	// gets precedence over an implicit match (0x20) from a lower tier
	// Note: don't sort by tier, don't consider bitscores
	//if ( a->tier < b->tier && 
	//    (a->bitScore & 0x40 || !b->bitScore & 0x40) ) 
	//	return -1; 
	//if ( a->tier > b->tier && 
	//    (b->bitScore & 0x40 || !a->bitScore & 0x40) )
	//	return 1; 

	// Absolute match proximity
	//if ( a->m20->m_proximityScore > b->m20->proximityScore )
	//	return -1;
	//else if ( a->m20->m_proximityScore < b->m20->proximityScore )
	//	return 1;

	// same tier, same proximity, sort by score
	if ( a->m_score > b->m_score ) 
		return -1;
	if ( a->m_score < b->m_score ) 
		return 1;

	// same tier, same proximity, same score, sort by docid
	//if ( a->docId < b->docId )
	//	return -1;
	//if ( a->docId > b->docId )
	//	return 1;

	// same score, sort by host
	if ( a->m_host > b->m_host )
		return -1;
	if ( a->m_host < b->m_host )
		return 1;

	return 0;
}

// Sort function for post query reranking's M20List
static int32_t s_reSortFunction(const M20List * a, const M20List * b)
{
	// Sort by tier first, then score
	// When sorting by tier, an explicit match (0x40) in a higher tier 
	// gets precedence over an implicit match (0x20) from a lower tier
	// Note: don't sort by tier, don't consider bitscores
	//if ( a->tier < b->tier && 
	//    (a->bitScore & 0x40 || !b->bitScore & 0x40) ) 
	//	return -1; 
	//if ( a->tier > b->tier && 
	//    (b->bitScore & 0x40 || !a->bitScore & 0x40) )
	//	return 1; 

	// Absolute match proximity
	//if ( a->m20->m_proximityScore > b->m20->proximityScore )
	//	return -1;
	//else if ( a->m20->m_proximityScore < b->m20->proximityScore )
	//	return 1;

	// same tier, same proximity, sort by score
	if ( a->m_score > b->m_score ) 
		return -1;
	if ( a->m_score < b->m_score ) 
		return 1;

	// same tier, same proximity, same score, sort by docid
	//if ( a->docId < b->docId )
	//	return -1;
	//if ( a->docId > b->docId )
	//	return 1;

	// same score, sort by host
	if ( a->m_host > b->m_host )
		return -1;
	if ( a->m_host < b->m_host )
		return 1;

	return 0;
}

#ifdef DEBUGGING_LANGUAGE
// Debug stuff, remove before flight
static void DoDump(char *loc, Msg20 **m20, int32_t num, 
		   score_t *scores, char *tiers) {
	int x;
	char *url;
	//log(LOG_DEBUG, "query: DoDump(): checkpoint %s AWL DEBUG", loc);
	for(x = 0; x < num; x++) {
		url = m20[x]->getUrl();
		if(!url) url = "None";
		//log( LOG_DEBUG, "query: DoDump(%d): "
		//     "tier:%d score:%"INT32" [url:'%s'] msg20:%p\n AWL DEBUG",
		//     x, tiers[x], scores[x], url, m20[x] );
	}
}
#endif // DEBUGGING_LANGUAGE

