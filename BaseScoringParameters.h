#ifndef BASESCORINGPARAMETERS_H_
#define BASESCORINGPARAMETERS_H_


//base scoring/ranking parameters. Mostly weights but also a few on/off and other adjustments
struct BaseScoringParameters {
	float m_sameLangWeight;
	float m_unknownLangWeight;
	
	float m_siteRankMultiplier;
	
	float m_termFreqWeightFreqMin;
	float m_termFreqWeightFreqMax;
	float m_termFreqWeightMin;
	float m_termFreqWeightMax;

	float m_diversityWeightMin,  m_diversityWeightMax;
	float m_densityWeightMin,    m_densityWeightMax;
	float m_hashGroupWeightBody,
	      m_hashGroupWeightTitle,
	      m_hashGroupWeightHeading,
	      m_hashGroupWeightInlist,
	      m_hashGroupWeightInMetaTag,
	      m_hashGroupWeightInLinkText,
	      m_hashGroupWeightInTag,
	      m_hashGroupWeightNeighborhood,
	      m_hashGroupWeightInternalLinkText,
	      m_hashGroupWeightInUrl,
	      m_hashGroupWeightInMenu;
	
	float m_synonymWeight;
	float m_bigramWeight;
	float m_pageTemperatureWeightMin;
	float m_pageTemperatureWeightMax;
	bool m_usePageTemperatureForRanking;
	
	float m_flagScoreMultiplier[26];
	int m_flagRankAdjustment[26];
	
	BaseScoringParameters() { clear(); }
	void clear();
	void traceToLog(const char *prefix);
};

#endif
