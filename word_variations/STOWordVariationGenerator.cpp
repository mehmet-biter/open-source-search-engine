#include "STOWordVariationGenerator.h"
#include "fctypes.h"  //to_lower_utf8


bool STOWordVariationGenerator::load_lexicon(const char *filename) {
	return lexicon.load(filename);
}


//STO lexicons are all-lowercase so this function is useful
std::vector<std::string> STOWordVariationGenerator::lower_words(const std::vector<std::string> &source_words) {
	std::vector<std::string> dst_words;
	for(auto src : source_words) {
		char buffer[128];
		if(src.length()<sizeof(buffer)) {
			int dst_len = to_lower_utf8(buffer, buffer+sizeof(buffer), src.data(), src.data()+src.length());
			dst_words.emplace_back(buffer,dst_len);
		} else
			dst_words.push_back(src);
	}
	return dst_words;
}
