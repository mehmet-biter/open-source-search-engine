#ifndef SCORING_WEIGHTS_H_
#define SCORING_WEIGHTS_H_

#include "Posdb.h"

//misc. rank-to-weight, group-to-weight, shoesize-to-weight lookups

struct ScoringWeights {
	float m_diversityWeights [MAXDIVERSITYRANK+1];
	float m_densityWeights   [MAXDENSITYRANK+1];
	float m_wordSpamWeights  [MAXWORDSPAMRANK+1]; // wordspam
	float m_linkerWeights	 [MAXWORDSPAMRANK+1]; // siterank of inlinker for link text
	float m_hashGroupWeights [HASHGROUP_END];

	void init(float diversityWeightMin, float diversityWeightMax,
	          float densityWeightMin,   float densityWeightMax,
	          float hashGroupWeightBody,
	          float hashGroupWeightTitle,
	          float hashGroupWeightHeading,
	          float hashGroupWeightInlist,
	          float hashGroupWeightInMetaTag,
	          float hashGroupWeightInLinkText,
	          float hashGroupWeightInTag,
	          float hashGroupWeightNeighborhood,
	          float hashGroupWeightInternalLinkText,
	          float hashGroupWeightInUrl,
	          float hashGroupWeightInMenu);
};

#endif
