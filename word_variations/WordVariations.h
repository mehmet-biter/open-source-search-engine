#ifndef WORDVARIATIONS_H_
#define WORDVARIATIONS_H_
#include "Lang.h"
#include <string>
#include <vector>
#include <stdarg.h>


//Weight to use when generating variations. Values are in the range [0.0 .. 1.0]
struct WordVariationWeights {
	float noun_indefinite_definite;
	float noun_definite_indefinite;
	float noun_singular_plural;
	float noun_plural_singular;
	float proper_noun_spelling_variants;
	float verb_spelling_variants;
	float verb_past_past_variants;
	float simple_spelling_variants;       //simple variants, eg "cyklen" vs. "cykelen"
	//todo: more configurable weights in WordVariationWeights
	WordVariationWeights()
	  : noun_indefinite_definite(1.0),
	    noun_definite_indefinite(1.0),
	    noun_singular_plural(1.0),
	    noun_plural_singular(1.0),
	    proper_noun_spelling_variants(1.0),
	    verb_spelling_variants(1.0),
	    verb_past_past_variants(1.0),
	    simple_spelling_variants(1.0)
	  {}
};


//Class to generate word variations. Some variations can be based on lookups in dictionaries
//and lexicons while other can be generated on-the-fly. Use get_generator() to get the generator
//for a language.
class WordVariationGenerator {
	WordVariationGenerator(const WordVariationGenerator&) = delete;
	WordVariationGenerator& operator=(const WordVariationGenerator&) = delete;

protected:
	WordVariationGenerator(lang_t lang);
	virtual ~WordVariationGenerator();

public:
	struct Variation {
		std::string     word;               //The word (or words)
		float           weight;             //The weight of this variation
		unsigned        source_word_start;  //start index of the source words that this variations covers
		unsigned        source_word_end;    //end (non-inclusive) index of the source words that this variations covers
	};
	
	virtual std::vector<Variation> query_variations(const std::vector<std::string> &source_words, const WordVariationWeights& weights, float threshold) = 0;
	//todo: change API so query_variations() method has access to full query and is also allowed to provide variations of bigrams etc.
	
	static WordVariationGenerator *get_generator(lang_t lang);
	
	enum log_class_t {
		log_trace,
		log_debug,
		log_info,
		log_warn,
		log_error
	};
	typedef void (*log_pfn_t)(log_class_t log_class, const char *fmt, va_list ap);
	static void set_log_function(log_pfn_t pfn);

protected:
	static void log(log_class_t log_class, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));
private:
	static log_pfn_t log_pfn;
};

bool initializeWordVariationGenerator_Danish();

#endif
