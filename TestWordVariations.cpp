#include "WordVariations.h"


//Implementation of test word variation generator. Words ending on a vowel gets a variant with a final 'z'.
//Words ending on a consonant gets a variant with a final 'y'. other words get no variants.

class TestWordVariationGenerator : public WordVariationGenerator {
public:
	TestWordVariationGenerator() : WordVariationGenerator(langEnglish) {}
	std::vector<Variation> query_variations(const std::vector<std::string> source_words, const WordVariationWeights& weights, float threshold);
};

TestWordVariationGenerator TestWordVariationGenerator_instance;


std::vector<WordVariationGenerator::Variation> TestWordVariationGenerator::query_variations(const std::vector<std::string> source_words, const WordVariationWeights& /*weights*/, float /*threshold*/) {
	std::vector<Variation> v;
	for(unsigned i=0; i<source_words.size(); i++) {
		auto word(source_words[i]);
		if(word.length()>0) {
			auto last_char(word[word.length()-1]);
			if(last_char=='a' || last_char=='e' || last_char=='i' || last_char=='o' || last_char=='u') {
				WordVariationGenerator::Variation variation;
				variation.word = word+'z';
				variation.weight = 1.0;
				variation.source_word_start = i;
				variation.source_word_end = i+1;
				v.push_back(variation);
			} else if(last_char>='a' || last_char<='z') {
				WordVariationGenerator::Variation variation;
				variation.word = word+'y';
				variation.weight = 1.0;
				variation.source_word_start = i;
				variation.source_word_end = i+1;
				v.push_back(variation);
			}
		}
	}
	return v;
}
