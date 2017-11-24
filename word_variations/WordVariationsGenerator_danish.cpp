#include "WordVariations.h"
#include "STOWordVariationGenerator.h"
#include <string.h>
#include <set>


namespace {

class WordVariationGenerator_danish : public STOWordVariationGenerator {
public:
	WordVariationGenerator_danish()
	  : STOWordVariationGenerator(langDanish)
	  {}
	std::vector<Variation> query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold);

	void find_simple_attribute_difference_wordforms(std::vector<WordVariationGenerator::Variation> &variations,
						        const std::vector<std::string> &source_words,
						        sto::word_form_attribute_t from_attr, sto::word_form_attribute_t to_attr,
						        float weight);
};

static WordVariationGenerator_danish s_WordVariationGenerator_danish;

} //anonymous namespace



bool initializeWordVariationGenerator_Danish() {
	return s_WordVariationGenerator_danish.load_lexicon("lexicon_da.sto");
}



std::vector<WordVariationGenerator::Variation> WordVariationGenerator_danish::query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold) {
	std::vector<WordVariationGenerator::Variation> variations;
	if(weights.noun_indefinite_definite >= threshold) {
		//find indefinite-noun -> definite-noun variations (eg. "kat" -> "katten")
		find_simple_attribute_difference_wordforms(variations,source_words,sto::word_form_attribute_t::definiteness_indefinite,sto::word_form_attribute_t::definiteness_definite, weights.noun_indefinite_definite);
	}
	if(weights.noun_definite_indefinite >= threshold) {
		//find definite-noun -> indefinite-noun variations (eg. "katten" -> "kat")
		find_simple_attribute_difference_wordforms(variations,source_words,sto::word_form_attribute_t::definiteness_definite,sto::word_form_attribute_t::definiteness_indefinite, weights.noun_definite_indefinite);
	}
	
	//filter out duplicates and variations below threshold
	//syn-todo: when filtering out duplicates choose the one with the higest weight
	std::set<std::string> seen_variations;
	for(auto iter = variations.begin(); iter!=variations.end(); ) {
		if(iter->weight < threshold)
			iter = variations.erase(iter);
		else if(seen_variations.find(iter->word)!=seen_variations.end())
			iter = variations.erase(iter);
		else {
			seen_variations.insert(iter->word);
			++iter;
		}
	}
	return variations;
}


static uint64_t wordformattrs2bitmask(const sto::WordForm &wf) {
	uint64_t bitmask = 0;
	for(auto a : wf.attribute)
		bitmask |= 1ULL<<(unsigned)a;
	return bitmask;
}


static bool same_wordform_as_source(const sto::WordForm &wf, const std::string source_word) {
	return wf.written_form_length==source_word.length() &&
	memcmp(wf.written_form,source_word.data(),source_word.length())==0;
}


void WordVariationGenerator_danish::find_simple_attribute_difference_wordforms(std::vector<WordVariationGenerator::Variation> &variations,
									       const std::vector<std::string> &source_words,
									       sto::word_form_attribute_t from_attr, sto::word_form_attribute_t to_attr,
									       float weight)
{
	for(unsigned i=0; i<source_words.size(); i++) {
		auto source_word(source_words[i]);
		auto matches(lexicon.query_matches(source_word));
		for(auto match : matches) {
			auto wordforms(match->query_all_explicit_ford_forms());
			for(auto wordform : wordforms) {
				if(same_wordform_as_source(*wordform,source_word) &&
					wordform->has_attribute(from_attr))
				{
					uint64_t source_word_bitmask = wordformattrs2bitmask(*wordform);
					uint64_t sought_bitmask = source_word_bitmask;
					sought_bitmask &= ~(1ULL<<((unsigned)from_attr));
					sought_bitmask |=   1ULL<<((unsigned)to_attr);
					for(auto definite_wordform : wordforms) {
						if(wordformattrs2bitmask(*definite_wordform)==sought_bitmask) {
							//found the other form of the word.
							//this may match multiple alternative spellings of the wordform, but the STO database cannot distinguish
							Variation v;
							v.word.assign(definite_wordform->written_form,definite_wordform->written_form_length);
							v.weight = weight;
							v.source_word_start = i;
							v.source_word_end = i+1;
							variations.push_back(v);
						}
					}
				}
			}
		}
	}
}
