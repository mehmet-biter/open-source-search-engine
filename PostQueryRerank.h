
#ifndef _POSTQUERYRERANK_H_
#define _POSTQUERYRERANK_H_

#include "HashTableT.h"
#include "Msg20.h"
#include "Sanity.h"
#include "fctypes.h"

class Msg40;
#include "SearchInput.h"
struct M20List;


typedef float rscore_t;

#define MINSCORE      1
#define MIN_SAVE_SIZE 100

#define PQR_BUF_SIZE  64

class PostQueryRerank {
public:
	static bool init ( );

	PostQueryRerank  ( );
	~PostQueryRerank ( );

	bool set1 ( Msg40 *, SearchInput * );
	bool set2 ( int32_t resultsNeeded );

private:
	rscore_t rerankLowerDemotesMore ( rscore_t score, 
				      float value, float maxValue,
				      float factor,
				      char *method, char *reason ) {
		if ( value >= maxValue ) return score;

		rscore_t temp = score;
		score = (rscore_t)(score *
				    (1.0 - 
				     ((maxValue-value)*factor/maxValue)));
		if ( score < MINSCORE ) score = MINSCORE;
		if(m_si->m_debug) 
		  logf( LOG_DEBUG, "query: pqr: result demoted "
		     "%3.3f%% because it has %3.1f / %3.1f %s. method: '%s'",
		     100-100*(float)score/temp, 
		     value, maxValue,
		     reason, method );
				
		return score;
	};
	rscore_t rerankHigherDemotesMore ( rscore_t score,
				       float value, float maxValue,
				       float factor,
				       char *method, char *reason ) {
		rscore_t temp = score;
		if ( value >= maxValue ) 
			score = (rscore_t)((1.0-factor)*score);
		else
			score = (rscore_t)(score * 
					    (1.0 - value*factor/maxValue));
		if ( score < MINSCORE ) score = MINSCORE;
		if(m_si->m_debug) 
		  logf( LOG_DEBUG, "query: pqr: result demoted "
		     "%3.3f%% because it has %3.1f / %3.1f %s. method: '%s'",
		     100-100*(float)score/temp, 
		     value, maxValue,
		     reason, method );

		return score;
	};
	rscore_t rerankAssignPenalty ( rscore_t score, float factor,
				   char *method, char *reason ) {
		rscore_t temp = score;
		score = (rscore_t)(score * (1.0 - factor));
		if (score < MINSCORE) score = MINSCORE;
		if(m_si->m_debug || g_conf.m_logDebugPQR ) 
		  logf( LOG_DEBUG, "query: pqr: result demoted "
		     "%3.3f%% because %s. method '%s'",
		     100-100*(float)score/temp, reason, method );

		return score;
	};

private:
	Msg40 *m_msg40;
	SearchInput *m_si;

	bool m_enabled;
	int32_t m_maxResultsToRerank;

	int32_t     m_numToSort;
	M20List *m_m20List;
	int32_t    *m_positionList;

	// Urls for pqrqttiu, pqrfsh and clustering
	Url *m_pageUrl;

	// for rerankOtherPagesFromSameHost
	HashTableT<uint64_t, int32_t> m_hostCntTable;

	// for rerankDatedbDate
	time_t m_now;

	char m_buf[PQR_BUF_SIZE];
	int32_t  m_maxUrlLen;
	char *m_cvtUrl;

	int32_t m_maxCommonInlinks;
};

#endif // _POSTQUERYRERANK_H_
