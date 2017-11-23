#include "WordVariations.h"
#include "STOWordVariationGenerator.h"
#include <string.h>


namespace {

class WordVariationGenerator_danish : public STOWordVariationGenerator {
public:
	WordVariationGenerator_danish()
	  : STOWordVariationGenerator(langDanish)
	  {}
	std::vector<Variation> query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold);
};

static WordVariationGenerator_danish s_WordVariationGenerator_danish;

} //anonymous namespace



bool initializeWordVariationGenerator_Danish() {
	return s_WordVariationGenerator_danish.load_lexicon("lexicon_da.sto");
}



std::vector<WordVariationGenerator::Variation> WordVariationGenerator_danish::query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold) {
	//very crude implementation. We're skipping the analyze-pos etc. completely and just do a simple lexicon lookup
	std::vector<WordVariationGenerator::Variation> variations;
	for(unsigned i=0; i<source_words.size(); i++) {
		auto source_word(source_words[i]);
		auto matches(lexicon.query_matches(source_word));
		for(auto match : matches) {
			auto wordforms(match->query_all_explicit_ford_forms());
			for(auto wordform : wordforms) {
				if(wordform->written_form_length!=source_word.length() ||
				   memcmp(wordform->written_form,source_word.data(),source_word.length())!=0)
				{
					Variation v;
					v.word.assign(wordform->written_form,wordform->written_form_length);
					v.weight = 1.0;
					v.source_word_start = i;
					v.source_word_end = i+1;
					variations.push_back(v);
				}
			}
		}
	}
	return variations;
}
