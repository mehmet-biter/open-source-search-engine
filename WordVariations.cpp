#include "WordVariations.h"
#include <map>


namespace {

static std::map<lang_t,WordVariationGenerator*> lang_generator_map;
	
class NullGenerator : public WordVariationGenerator {
public:
	NullGenerator() : WordVariationGenerator(langUnknown) {}
	std::vector<Variation> query_variations(const std::vector<std::string> /*source_words*/, const WordVariationWeights& /*weights*/, float /*threshold*/) {
		return std::vector<Variation>();
	}
};

static NullGenerator null_generator;

}



WordVariationGenerator::WordVariationGenerator(lang_t lang) {
	lang_generator_map.emplace(lang,this);
}


WordVariationGenerator::~WordVariationGenerator() {
	//It is tempting to deregister this instance but since we don't know the destruction order then lang_generator_map may already have been destroyed
}



WordVariationGenerator *WordVariationGenerator::get_generator(lang_t lang) {
	auto iter(lang_generator_map.find(lang));
	if(iter!=lang_generator_map.end())
		return iter->second;
	return &null_generator;
}
