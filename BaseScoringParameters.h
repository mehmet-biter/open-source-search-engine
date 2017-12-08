#ifndef BASESCORINGPARAMETERS_H_
#define BASESCORINGPARAMETERS_H_


//base scoring/ranking parameters. Mostly weights but also a few on/off
struct BaseScoringParameters {
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
	
	BaseScoringParameters() { clear(); }
	void clear() {
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
	}
};

#endif
