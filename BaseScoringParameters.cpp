#include "BaseScoringParameters.h"
#include "Log.h"
#include <stddef.h>


void BaseScoringParameters::clear() {
	for(size_t i=0; i<sizeof(m_languageWeights)/sizeof(m_languageWeights[0]); i++)
		m_languageWeights[i] = 1.0;
	m_siteRankMultiplier = 0.0;
	m_termFreqWeightFreqMin = m_termFreqWeightFreqMax = 0.0;
	m_termFreqWeightMin = m_termFreqWeightMax = 0.0;
	m_diversityWeightMin = m_diversityWeightMax = 0.0;
	m_densityWeightMin = m_densityWeightMax = 0.0;
	m_hashGroupWeightBody = 0.0;
	m_hashGroupWeightTitle = 0.0;
	m_hashGroupWeightHeading = 0.0;
	m_hashGroupWeightInlist = 0.0;
	m_hashGroupWeightInMetaTag = 0.0;
	m_hashGroupWeightInLinkText = 0.0;
	m_hashGroupWeightInTag = 0.0;
	m_hashGroupWeightNeighborhood = 0.0;
	m_hashGroupWeightInternalLinkText = 0.0;
	m_hashGroupWeightInUrl = 0.0;
	m_hashGroupWeightInMenu = 0.0;
	m_hashGroupWeightExplicitKeywords = 0.0;
	m_synonymWeight = 0.0;
	m_bigramWeight = 0.0;
	m_pageTemperatureWeightMin = 0.0;
	m_pageTemperatureWeightMax = 0.0;
	m_usePageTemperatureForRanking = false;
	for(int i=0; i<26;i++)
		m_flagScoreMultiplier[i] = 0.0;
	for(int i=0; i<26;i++)
		m_flagRankAdjustment[i] = 0;
}


void BaseScoringParameters::traceToLog(const char *prefix) {
	log(LOG_TRACE,"%s:BaseScoringParameters:",prefix);
	for(size_t i=0; i<sizeof(m_languageWeights)/sizeof(m_languageWeights[0]); i++)
		log(LOG_TRACE,"%s:  m_languageWeights[%zu]=%.3f",prefix,i,m_languageWeights[i]);
	log(LOG_TRACE,"%s:  m_siteRankMultiplier=%.3f",prefix,m_siteRankMultiplier);
	log(LOG_TRACE,"%s:  m_termFreqWeightFreqMin=%.3f",prefix,m_termFreqWeightFreqMin);
	log(LOG_TRACE,"%s:  m_termFreqWeightFreqMax=%.3f",prefix,m_termFreqWeightFreqMax);
	log(LOG_TRACE,"%s:  m_termFreqWeightMin=%.3f",prefix,m_termFreqWeightMin);
	log(LOG_TRACE,"%s:  m_termFreqWeightMax=%.3f",prefix,m_termFreqWeightMax);

	log(LOG_TRACE,"%s:  m_diversityWeightMin=%.3f",prefix,m_diversityWeightMin);
	log(LOG_TRACE,"%s:  m_diversityWeightMax=%.3f",prefix,m_diversityWeightMax);
	log(LOG_TRACE,"%s:  m_densityWeightMin=%.3f",prefix,m_densityWeightMin);
	log(LOG_TRACE,"%s:  m_densityWeightMax=%.3f",prefix,m_densityWeightMax);
	log(LOG_TRACE,"%s:  m_hashGroupWeightBody=%.3f",prefix,m_hashGroupWeightBody);
	log(LOG_TRACE,"%s:  m_hashGroupWeightTitle=%.3f",prefix,m_hashGroupWeightTitle);
	log(LOG_TRACE,"%s:  m_hashGroupWeightHeading=%.3f",prefix,m_hashGroupWeightHeading);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInlist=%.3f",prefix,m_hashGroupWeightInlist);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInMetaTag=%.3f",prefix,m_hashGroupWeightInMetaTag);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInLinkText=%.3f",prefix,m_hashGroupWeightInLinkText);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInTag=%.3f",prefix,m_hashGroupWeightInTag);
	log(LOG_TRACE,"%s:  m_hashGroupWeightNeighborhood=%.3f",prefix,m_hashGroupWeightNeighborhood);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInternalLinkText=%.3f",prefix,m_hashGroupWeightInternalLinkText);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInUrl=%.3f",prefix,m_hashGroupWeightInUrl);
	log(LOG_TRACE,"%s:  m_hashGroupWeightInMenu=%.3f",prefix,m_hashGroupWeightInMenu);
	log(LOG_TRACE,"%s:  m_synonymWeight=%.3f",prefix,m_synonymWeight);
	log(LOG_TRACE,"%s:  m_bigramWeight=%.3f",prefix,m_bigramWeight);
	log(LOG_TRACE,"%s:  m_pageTemperatureWeightMin=%.3f",prefix,m_pageTemperatureWeightMin);
	log(LOG_TRACE,"%s:  m_pageTemperatureWeightMax=%.3f",prefix,m_pageTemperatureWeightMax);
	log(LOG_TRACE,"%s:  m_usePageTemperatureForRanking=%s",prefix,m_usePageTemperatureForRanking?"true":"false");
	for(int i=0; i<26; i++) {
		log(LOG_TRACE,"%s:  m_flagScoreMultiplier[%d]=%.3f",prefix,i,m_flagScoreMultiplier[i]);
		log(LOG_TRACE,"%s:  m_flagRankAdjustment[%d]=%d",prefix,i,m_flagRankAdjustment[i]);
	}
}


bool BaseScoringParameters::allLanguageWeightsAreTheSame() const {
	for(unsigned i=0; i<64; i++)
		if(m_languageWeights[i]!=m_languageWeights[0])
			return false;
	return true;
}
