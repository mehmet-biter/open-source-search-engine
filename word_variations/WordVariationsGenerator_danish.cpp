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

	void transliterate_proper_noun_aring_and_aa(std::vector<WordVariationGenerator::Variation> &variations,
						    const std::vector<std::string> &source_words,
						    const std::vector<std::string> &lower_source_words,
						    float weight);
	void transliterate_proper_noun_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
						    const std::vector<std::string> &source_words,
						    const std::vector<std::string> &lower_source_words,
						    float weight);
};

static WordVariationGenerator_danish s_WordVariationGenerator_danish;

} //anonymous namespace



bool initializeWordVariationGenerator_Danish() {
	return s_WordVariationGenerator_danish.load_lexicon("lexicon_da.sto");
}



std::vector<WordVariationGenerator::Variation> WordVariationGenerator_danish::query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold) {
	std::vector<std::string> lower_source_words(lower_words(source_words));
	std::vector<WordVariationGenerator::Variation> variations;
	if(weights.noun_indefinite_definite >= threshold) {
		//find indefinite-noun -> definite-noun variations (eg. "kat" -> "katten")
		find_simple_attribute_difference_wordforms(variations,lower_source_words,sto::word_form_attribute_t::definiteness_indefinite,sto::word_form_attribute_t::definiteness_definite, weights.noun_indefinite_definite);
	}
	if(weights.noun_definite_indefinite >= threshold) {
		//find definite-noun -> indefinite-noun variations (eg. "katten" -> "kat")
		find_simple_attribute_difference_wordforms(variations,lower_source_words,sto::word_form_attribute_t::definiteness_definite,sto::word_form_attribute_t::definiteness_indefinite, weights.noun_definite_indefinite);
	}
	if(weights.noun_singular_plural >= threshold) {
		//find singular->plural variations (eg. "kat" -> "katte")
		find_simple_attribute_difference_wordforms(variations,lower_source_words,sto::word_form_attribute_t::grammaticalNumber_singular,sto::word_form_attribute_t::grammaticalNumber_plural, weights.noun_singular_plural);
	}
	if(weights.noun_plural_singular >= threshold) {
		//find plural -> singular variations (eg. "kattene" -> "katten")
		find_simple_attribute_difference_wordforms(variations,lower_source_words,sto::word_form_attribute_t::grammaticalNumber_plural,sto::word_form_attribute_t::grammaticalNumber_singular, weights.noun_plural_singular);
	}
	
	if(weights.proper_noun_spelling_variants >= threshold) {
		transliterate_proper_noun_aring_and_aa(variations,source_words,lower_source_words,weights.proper_noun_spelling_variants);
		transliterate_proper_noun_acute_accent(variations,source_words,lower_source_words,weights.proper_noun_spelling_variants);
	}
	
	//filter out duplicates and variations below threshold
	//syn-todo: when filtering out duplicates choose the one with the highest weight
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


void WordVariationGenerator_danish::transliterate_proper_noun_aring_and_aa(std::vector<WordVariationGenerator::Variation> &variations,
									   const std::vector<std::string> &/*source_words*/,
									   const std::vector<std::string> &lower_source_words,
									   float weight)
{
	//In 1948 the "bolle-å" was introduced (Unicode U+00C5 and U+00E5). Prior to that the letter was written as two a's, eg "vestergaard".
	//Some people kept using double-a in their names, mostly in surnames. All place names changed to bolle-å. For various reasons some cities
	//and towns insisted on using double-a. In 1984 double-a was allowed in place names. Some cities switched immediately to double-a (eg. Aabenraa),
	//others kept using bolle-å, some changed back to double-a much later. Some people keep using bolle-å even if a name's official spelling is
	//with double-a.
	//Result:
	//  - First names are "usually" with bolle-å
	//  - Surnames can be either.
	//  - Town and city names: Either and both are used
	//  - Other place names: typically bolle-å is used
	
	//Limitation: We don't have a complete list of proper nouns (town, cities, places, first names, surnames, ...). Users typically don't write
	//proper nouns using capitalization, so we can't even use that as a hint.
	//So we transliterate all occurrences and hope for the best. It will result in some hilarity, eg the middle-eastern name Aamin where the
	//two a's represent a long a-sound will generate the variation "Åmin". Oh well.
	
	std::string unicode_00E5("å",2);
	std::string double_aa("aa",2);
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word.length()>=3 && source_word.find(unicode_00E5)!=source_word.npos) {
			//do å -> aa transliteration
			std::string tmp(source_word);
			for(std::string::size_type p=tmp.find(unicode_00E5); p!=tmp.npos; p=tmp.find(unicode_00E5)) {
				tmp.replace(p,unicode_00E5.length(), double_aa);
			}
			WordVariationGenerator::Variation v;
			v.word = tmp;
			v.weight = weight;
			v.source_word_start = i;
			v.source_word_end = i+1;
			variations.push_back(v);
		}
		if(source_word.length()>=4 && source_word.find(double_aa)!=source_word.npos) {
			//do aa -> å transliteration
			std::string tmp(source_word);
			for(std::string::size_type p=tmp.find(double_aa); p!=tmp.npos; p=tmp.find(double_aa)) {
				tmp.replace(p,double_aa.length(), unicode_00E5);
			}
			WordVariationGenerator::Variation v;
			v.word = tmp;
			v.weight = weight;
			v.source_word_start = i;
			v.source_word_end = i+1;
			variations.push_back(v);
		}
	}
}


void WordVariationGenerator_danish::transliterate_proper_noun_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
									   const std::vector<std::string> &/*source_words*/,
									   const std::vector<std::string> &lower_source_words,
									   float weight)
{
	//Acute accent / accent aigu is the only accent used in Danish and it is always optional.
	//It is used for indicating where the stress in the word is and for disambiguation and reading help.
	//It is always optional and rare. Examples:
	//  - ...alle vs. ...allé          (...-street)
	//  - Rene vs. René                (name, originally from French)
	//  - Implementer vs. implementér  (imperative)
	//But since it is optional we can always strip the accent away
	
	std::string unicode_00E9("é",2);
	std::string plain_e("e",1);
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word.find(unicode_00E9)!=source_word.npos) {
			//do å -> aa transliteration
			std::string tmp(source_word);
			for(std::string::size_type p=tmp.find(unicode_00E9); p!=tmp.npos; p=tmp.find(unicode_00E9)) {
				tmp.replace(p,unicode_00E9.length(), plain_e);
			}
			WordVariationGenerator::Variation v;
			v.word = tmp;
			v.weight = weight;
			v.source_word_start = i;
			v.source_word_end = i+1;
			variations.push_back(v);
		}
	}
}
