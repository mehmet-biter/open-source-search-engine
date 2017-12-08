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
};

#endif
