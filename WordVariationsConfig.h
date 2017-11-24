#ifndef WORDVARIATIONSCONFIG_H_
#define WORDVARIATIONSCONFIG_H_
#include "WordVariations.h"

struct WordVariationsConfig {
	bool m_wiktionaryWordVariations;
	bool m_languageSpecificWordVariations;
	float m_word_variations_threshold;
	WordVariationWeights m_word_variations_weights;
	
	WordVariationsConfig()
	  : m_wiktionaryWordVariations(false),
	    m_languageSpecificWordVariations(false),
	    m_word_variations_threshold(1.0),
	    m_word_variations_weights()
	    {}
};

#endif
