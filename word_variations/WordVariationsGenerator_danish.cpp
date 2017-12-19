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
	void find_simple_attribute_match_wordforms(std::vector<WordVariationGenerator::Variation> &variations,
						   const std::vector<std::string> &source_words,
						   float weight);

	void transliterate_proper_noun_aring_and_aa(std::vector<WordVariationGenerator::Variation> &variations,
						    const std::vector<std::string> &source_words,
						    const std::vector<std::string> &lower_source_words,
						    float weight);
	void transliterate_proper_noun_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
						    const std::vector<std::string> &source_words,
						    const std::vector<std::string> &lower_source_words,
						    float weight);
	void transliterate_verb_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
					     const std::vector<std::string> &source_words,
					     const std::vector<std::string> &lower_source_words,
					     float weight);
	void make_verb_past_past_variants(std::vector<WordVariationGenerator::Variation> &variations,
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
	if(weights.verb_spelling_variants >= threshold) {
		transliterate_verb_acute_accent(variations,source_words,lower_source_words,weights.verb_spelling_variants);
	}
	
	if(weights.verb_past_past_variants >= threshold) {
		make_verb_past_past_variants(variations,source_words,lower_source_words,weights.verb_past_past_variants);
	}
	
	if(weights.simple_spelling_variants >= threshold) {
		find_simple_attribute_match_wordforms(variations,lower_source_words,weights.simple_spelling_variants);
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
			auto wordforms(match->query_all_explicit_word_forms());
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


static bool has_same_attributes(const sto::WordForm *wf1, const sto::WordForm *wf2) {
	//quick and dirty check. The source sto file currently has the attributes in the same order.
	return memcmp(wf1->attribute,wf2->attribute,sizeof(wf2->attribute))==0;
}


void WordVariationGenerator_danish::find_simple_attribute_match_wordforms(std::vector<WordVariationGenerator::Variation> &variations,
									  const std::vector<std::string> &source_words,
									  float weight)
{
	for(unsigned i=0; i<source_words.size(); i++) {
		auto source_word(source_words[i]);
		auto matches(lexicon.query_matches(source_word));
		for(auto match : matches) {
			auto wordforms(match->query_all_explicit_word_forms());
			for(auto wordform : wordforms) {
				if(same_wordform_as_source(*wordform,source_word)) {
					//found the word form match. Now look for other wordforms with exactly the same attributes. Those are alternate spellings.
					//so first find all lexical entries with the same morphological unit id, and check all wordforms of those, looking for an attribute match
					auto same_morph_entries = lexicon.query_lexical_entries_with_same_morphological_unit_id(match);
					for(auto same_morph_entry : same_morph_entries) {
						auto wordforms2(same_morph_entry->query_all_explicit_word_forms());
						for(auto wordform2 : wordforms2) {
							if(wordform2!=wordform && has_same_attributes(wordform,wordform2)) {
								//found an alternative spelling of the word
								Variation v;
								v.word.assign(wordform2->written_form,wordform2->written_form_length);
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


//Acute accent / accent aigu is the only accent used in Danish.
//It is used for indicating where the stress in the word is and for disambiguation and reading help.
//It is always optional and rare. Examples:
//  - ...alle vs. ...allé          (...-street)
//  - Rene vs. René                (name, originally from French)
//  - implementer vs. implementér  (imperative)
//It is very rare for the accent to be used for anything except the letter e.
//When using foreign words (esp. proper nouns) there may be other accents (eg Citroën, Genève, Granö, München, ...)

void WordVariationGenerator_danish::transliterate_proper_noun_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
									   const std::vector<std::string> &/*source_words*/,
									   const std::vector<std::string> &lower_source_words,
									   float weight)
{
	//because the accent is optional we can always strip it away
	std::string unicode_00E9("é",2);
	std::string plain_e("e",1);
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word.find(unicode_00E9)!=source_word.npos) {
			//do é -> e transliteration
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
	
	//The reverse is not true: we cannot add the accent to all instances of the letter e because then it starts getting silly.
	//Eg. for "Rene" we would have to generate "Réne", "René" and "Réné".
	//The accent is usually only used on the last syllable. So we use a bit hackish approach: We hardcode some common suffixes.
	//We could use the fact that the accent is rarely put on any other syllable than the last, but that is mostly the verb
	//imperative form which is handled by transliterate_verb_acute_accent() so it would gain very little.
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word.length()>=4 && source_word.substr(source_word.length()-4)=="alle") {
			//possibly a street name
			WordVariationGenerator::Variation v;
			v.word = source_word.substr(0,source_word.length()-4)+"allé";
			v.weight = weight;
			v.source_word_start = i;
			v.source_word_end = i+1;
			variations.push_back(v);
			continue;
		}
		if(source_word=="rene") {
			//possibly the first name René (could also be the adjective "rene")
			WordVariationGenerator::Variation v;
			v.word = "rené";
			v.weight = weight;
			v.source_word_start = i;
			v.source_word_end = i+1;
			variations.push_back(v);
			continue;
		}
	}
}

void WordVariationGenerator_danish::transliterate_verb_acute_accent(std::vector<WordVariationGenerator::Variation> &variations,
								    const std::vector<std::string> &source_words,
								    const std::vector<std::string> &lower_source_words,
								    float weight)
{
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word.length()>4 && source_word.substr(source_word.length()-2)=="er") {
			//possibly a verb in imperative
			bool is_imperative = false;
			auto matches(lexicon.query_matches(source_word));
			for(auto match : matches) {
				auto wordforms(match->query_all_explicit_word_forms());
				for(auto wordform : wordforms) {
					if(same_wordform_as_source(*wordform,source_word) &&
						wordform->has_attribute(sto::word_form_attribute_t::verbFormMood_imperative))
					{
						is_imperative = true;
					}
				}
			}
			if(is_imperative) {
				WordVariationGenerator::Variation v;
				v.word = source_word.substr(0,source_word.length()-2)+"ér";
				v.weight = weight;
				v.source_word_start = i;
				v.source_word_end = i+1;
				variations.push_back(v);
			}
		}
	}
}


void WordVariationGenerator_danish::make_verb_past_past_variants(std::vector<WordVariationGenerator::Variation> &variations,
								 const std::vector<std::string> &source_words,
								 const std::vector<std::string> &lower_source_words,
								 float weight)
{
	//In Danish changing one past tense to another past tense can be a bit sketchy because we really should know the context.
	//There isn't much difference between written and colloquial forms so the variations don't give us that much in that respect.
	//But since you can change the weight of these variations there is no harm in having this possibility.
	//Danish tenses
	//  perfect (førnutid)		auxiliary verb in present tense + past participle	"har købt"
	//  preterite (datid)		past tense						"købte"
	//  pluperfect (førdatid)	auxiliary verb in past tense + past participle		"havde købt"
	//the auxilliary verbs are "være" ("er"/"var") and "have" ("har"/"havde"). We blatantly ignore the two other auxilliary verbs "blive" and "få"

	bool prev_was_er = false,
	     prev_was_var = false,
	     prev_was_har = false,
	     prev_was_havde = false;
	unsigned prev_word_idx=0;
	for(unsigned i=0; i<lower_source_words.size(); i++) {
		auto source_word(lower_source_words[i]);
		if(source_word==" ")
			continue;
		auto matches(lexicon.query_matches(source_word));
		if(prev_was_er || prev_was_var || prev_was_har || prev_was_havde) {
			//check if this word is the past participle
			const sto::WordForm *wordform_past_participle = NULL;
			const sto::WordForm *wordform_preterite = NULL;
			for(auto match : matches) {
				auto wordforms(match->query_all_explicit_word_forms());
				for(auto wordform : wordforms) {
					if(same_wordform_as_source(*wordform,source_word) &&
					   wordform->has_attribute(sto::word_form_attribute_t::tense_past) &&
					   wordform->has_attribute(sto::word_form_attribute_t::verbFormMood_participle))
					{
						wordform_past_participle = wordform;
					}
					if(wordform->has_attribute(sto::word_form_attribute_t::tense_past) &&
					   wordform->has_attribute(sto::word_form_attribute_t::verbFormMood_indicative) &&
					   wordform->has_attribute(sto::word_form_attribute_t::voice_activeVoice)) //we'll ignore this complication for now
						wordform_preterite = wordform;
				}
			}
			if(wordform_past_participle!=NULL) {
				//word is past participle and previous word was one of the auxilliary verbs
				if(prev_was_er) {
					//generate preterite
					if(wordform_preterite) {
						WordVariationGenerator::Variation v0;
						v0.word.assign(wordform_preterite->written_form,wordform_preterite->written_form_length);
						v0.weight = weight;
						v0.source_word_start = prev_word_idx;
						v0.source_word_end = i+1;
						variations.push_back(v0);
					}
					//generate pluperfect
					WordVariationGenerator::Variation v;
					v.word = "var";
					v.weight = weight;
					v.source_word_start = prev_word_idx;
					v.source_word_end = i;
					variations.push_back(v);
				}
				if(prev_was_var) {
					//generate preterite
					if(wordform_preterite) {
						WordVariationGenerator::Variation v0;
						v0.word.assign(wordform_preterite->written_form,wordform_preterite->written_form_length);
						v0.weight = weight;
						v0.source_word_start = prev_word_idx;
						v0.source_word_end = i+1;
						variations.push_back(v0);
					}
					//generate perfect
					WordVariationGenerator::Variation v;
					v.word = "er";
					v.weight = weight;
					v.source_word_start = prev_word_idx;
					v.source_word_end = i;
					variations.push_back(v);
				}
				if(prev_was_har) {
					//generate preterite
					if(wordform_preterite) {
						WordVariationGenerator::Variation v0;
						v0.word.assign(wordform_preterite->written_form,wordform_preterite->written_form_length);
						v0.weight = weight;
						v0.source_word_start = prev_word_idx;
						v0.source_word_end = i+1;
						variations.push_back(v0);
					}
					//generate pluperfect
					WordVariationGenerator::Variation v;
					v.word = "havde";
					v.weight = weight;
					v.source_word_start = prev_word_idx;
					v.source_word_end = i;
					variations.push_back(v);
				}
				if(prev_was_havde) {
					//generate preterite
					if(wordform_preterite) {
						WordVariationGenerator::Variation v0;
						v0.word.assign(wordform_preterite->written_form,wordform_preterite->written_form_length);
						v0.weight = weight;
						v0.source_word_start = prev_word_idx;
						v0.source_word_end = i+1;
						variations.push_back(v0);
					}
					//generate perfect
					WordVariationGenerator::Variation v;
					v.word = "har";
					v.weight = weight;
					v.source_word_start = prev_word_idx;
					v.source_word_end = i;
					variations.push_back(v);
				}
			}
		} else {
			//check if word is preterite (and also look for past participle)
			const sto::WordForm *wordform_past_participle = NULL;
			bool is_preterite = false;
			for(auto match : matches) {
				auto wordforms(match->query_all_explicit_word_forms());
				for(auto wordform : wordforms) {
					if(same_wordform_as_source(*wordform,source_word) &&
					   wordform->has_attribute(sto::word_form_attribute_t::tense_past) &&
					   wordform->has_attribute(sto::word_form_attribute_t::verbFormMood_indicative) &&
					   wordform->has_attribute(sto::word_form_attribute_t::voice_activeVoice)) //we'll ignore this complication for now
					{
						is_preterite = true;
					}
					if(wordform->has_attribute(sto::word_form_attribute_t::tense_past) &&
					   wordform->has_attribute(sto::word_form_attribute_t::verbFormMood_participle) &&
					   !wordform->has_attribute(sto::word_form_attribute_t::transcategorization_transadjectival) &&
					   !wordform->has_attribute(sto::word_form_attribute_t::transcategorization_transadverbial) &&
					   !wordform->has_attribute(sto::word_form_attribute_t::transcategorization_transnominal))
					{
						wordform_past_participle = wordform;
					}
				}
			}
			if(is_preterite && wordform_past_participle!=NULL) {
				//complication: we don't know which auxilliary verb is the proper one. We would need to analyze the sentece to see
				//if the main verb was used as transitive or intransitive and if the voice was passive. We ignore this problem and generate both forms and hope for the best. Except for "er"/"var" which takes the auxilliary verb "har"/"havde"
				//generate perfect
				if(source_word!="var") {
					WordVariationGenerator::Variation v0_0;
					v0_0.word = "har "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v0_0.weight = weight;
					v0_0.source_word_start = i;
					v0_0.source_word_end = i+1;
					variations.push_back(v0_0);
					WordVariationGenerator::Variation v0_1;
					v0_1.word = "er "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v0_1.weight = weight;
					v0_1.source_word_start = i;
					v0_1.source_word_end = i+1;
					variations.push_back(v0_1);
					//generate pluperfect
					WordVariationGenerator::Variation v1_0;
					v1_0.word = "havde "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v1_0.weight = weight;
					v1_0.source_word_start = i;
					v1_0.source_word_end = i+1;
					variations.push_back(v1_0);
					WordVariationGenerator::Variation v1_1;
					v1_1.word = "var "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v1_1.weight = weight;
					v1_1.source_word_start = i;
					v1_1.source_word_end = i+1;
					variations.push_back(v1_1);
				} else {
					//"at være" takes the auxilliary verb "have"
					WordVariationGenerator::Variation v0_0;
					v0_0.word = "har "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v0_0.weight = weight;
					v0_0.source_word_start = i;
					v0_0.source_word_end = i+1;
					variations.push_back(v0_0);
					WordVariationGenerator::Variation v1_0;
					v1_0.word = "havde "+std::string(wordform_past_participle->written_form,wordform_past_participle->written_form_length);
					v1_0.weight = weight;
					v1_0.source_word_start = i;
					v1_0.source_word_end = i+1;
					variations.push_back(v1_0);
				}
			}
		}
		prev_was_er = source_word=="er";
		prev_was_var = source_word=="var";
		prev_was_har = source_word=="har";
		prev_was_havde = source_word=="havde";
		prev_word_idx = i;
	}
}
