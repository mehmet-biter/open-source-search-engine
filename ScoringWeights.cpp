#include "ScoringWeights.h"
#include "ScalingFunctions.h"
#include <math.h>

void ScoringWeights::init(float diversityWeightMin, float diversityWeightMax,
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
			  float hashGroupWeightInMenu)
{
	for(int i = 0; i <= MAXDIVERSITYRANK; i++)
		m_diversityWeights[i] = scale_quadratic(i, 0, MAXDIVERSITYRANK, diversityWeightMin, diversityWeightMax);
	
	for(int i = 0; i <= MAXDENSITYRANK; i++)
		m_densityWeights[i] = scale_quadratic(i, 0, MAXDENSITYRANK, densityWeightMin, densityWeightMax);
	
	// make sure if word spam is 0 that the weight is not 0
	for(int i = 0; i <= MAXWORDSPAMRANK; i++)
		m_wordSpamWeights[i] = scale_linear(i, 0, MAXWORDSPAMRANK, 1.0/MAXWORDSPAMRANK, 1.0);

	// site rank of inlinker
	// to be on the same level as multiplying the final score
	// by the siterank+1 we should make this a sqrt() type thing
	// since we square it so that single term scores are on the same
	// level as term pair scores
	// @@@ BR: Right way to do it? Gives a weight between 1 and 4
	for(int i = 0; i <= MAXWORDSPAMRANK; i++) {
		m_linkerWeights[i] = sqrt(1.0 + i);
	}
	
	for(int i=0; i<HASHGROUP_END; i++)
		m_hashGroupWeights[i] = 1.0;
	
	m_hashGroupWeights[HASHGROUP_BODY              ] = hashGroupWeightBody;
	m_hashGroupWeights[HASHGROUP_TITLE             ] = hashGroupWeightTitle;
	m_hashGroupWeights[HASHGROUP_HEADING           ] = hashGroupWeightHeading;
	m_hashGroupWeights[HASHGROUP_INLIST            ] = hashGroupWeightInlist;
	m_hashGroupWeights[HASHGROUP_INMETATAG         ] = hashGroupWeightInMetaTag;
	m_hashGroupWeights[HASHGROUP_INLINKTEXT        ] = hashGroupWeightInLinkText;
	m_hashGroupWeights[HASHGROUP_INTAG             ] = hashGroupWeightInTag;
	m_hashGroupWeights[HASHGROUP_NEIGHBORHOOD      ] = hashGroupWeightNeighborhood;
	m_hashGroupWeights[HASHGROUP_INTERNALINLINKTEXT] = hashGroupWeightInternalLinkText;
	m_hashGroupWeights[HASHGROUP_INURL             ] = hashGroupWeightInUrl;
	m_hashGroupWeights[HASHGROUP_INMENU            ] = hashGroupWeightInMenu;
}
