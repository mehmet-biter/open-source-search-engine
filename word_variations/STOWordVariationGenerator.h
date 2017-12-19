#ifndef STOWORDVARIATIONS_H_
#define STOWORDVARIATIONS_H_
#include "WordVariations.h"
#include "sto/sto.h"

//A word variation generator that can use a STO database
class STOWordVariationGenerator : public WordVariationGenerator {
protected:
	sto::Lexicon lexicon;
public:
	using WordVariationGenerator::WordVariationGenerator;
	bool load_lexicon(const char *filename);
	void unload_lexicon() { lexicon.unload(); }

	std::vector<std::string> lower_words(const std::vector<std::string> &source_words);
};

#endif
