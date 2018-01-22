#include "STOWordVariationGenerator.h"
#include "fctypes.h"  //to_lower_utf8
#include "Unicode.h" //getUtf8CharSize etc


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

//....except for proper nouns which are present in capitalized form, but users rarely bother typing it correctly so these functions are useful too
std::string STOWordVariationGenerator::capitalize_word(const std::string &lower_src) {
	//todo: we don't handle o'Brien and other Irish names properly
	//todo: we don't handle correct capitalization of 'i' in Turkish locale (which should be 'İ')
	if(lower_src.length()==0)
		return lower_src;
	size_t sz = getUtf8CharSize(lower_src.data());
	if(sz>lower_src.length())
		return lower_src; //invalid/truncated utf8
	char tmp_src[6], tmp_dst[6];
	if(sz>=sizeof(tmp_src))
		return lower_src; //invalid/truncated utf8
	memcpy(tmp_src,lower_src.data(),sz);
	tmp_src[sz]='\0';
	size_t dstsz = to_upper_utf8(tmp_dst,tmp_src);
	tmp_dst[dstsz]='\0';
	return tmp_dst + lower_src.substr(sz);
}

std::vector<std::string> STOWordVariationGenerator::capitalize_words(const std::vector<std::string> &lower_words) {
	std::vector<std::string> dst_words;
	for(auto src : lower_words) {
		dst_words.push_back(capitalize_word(src));
	}
	return dst_words;
}
