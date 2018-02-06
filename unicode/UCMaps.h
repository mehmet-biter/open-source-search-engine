#ifndef UCMAPS_H_
#define UCMAPS_H_
#include "UCMap.h"
#include "UCEnums.h"

namespace UnicodeMaps {

extern FullMap<Unicode::script_t>           g_unicode_script_map;
extern FullMap<Unicode::general_category_t> g_unicode_general_category_map;
extern FullMap<uint32_t>                    g_unicode_properties_map;
extern FullMap<bool>                        g_unicode_is_alphabetic_map;
extern FullMap<bool>                        g_unicode_is_uppercase_map;
extern FullMap<bool>                        g_unicode_is_lowercase_map;
extern FullMap<bool>                        g_unicode_wordchars_map;
extern SparseMap<UChar32>                   g_unicode_uppercase_map;
extern SparseMap<UChar32>                   g_unicode_lowercase_map;
extern SparseMap<UChar32>                   g_unicode_canonical_decomposition_map;

bool load_maps(const char *dir, const char **errstr);
void unload_maps();


//convenience functions

static inline Unicode::script_t query_script(UChar32 c) {
	return g_unicode_script_map.lookup2(c);
}

static inline uint32_t query_properties(UChar32 c) {
	return g_unicode_properties_map.lookup2(c);
}

static inline bool is_lowercase(UChar32 c) {
	return g_unicode_is_lowercase_map.lookup2(c);
}

static inline bool is_uppercase(UChar32 c) {
	return g_unicode_is_uppercase_map.lookup2(c);
}

static inline bool is_alphabetic(UChar32 c) {
	return g_unicode_is_alphabetic_map.lookup2(c);
}

static inline bool is_whitespace(UChar32 c) {
	return g_unicode_properties_map.lookup2(c)&Unicode::White_Space;
}

static inline UChar32 to_upper(UChar32 c) { //assumes the mapping only produces one codepoint (which is not the case for ß)
	auto e = g_unicode_uppercase_map.lookup(c);
	if(e)
		return e->values[0];
	else
		return c;
}

static inline UChar32 to_lower(UChar32 c) {
	auto e = g_unicode_lowercase_map.lookup(c);
	//currently all lowercase mappings are to one (1) codepoint
	if(e)
		return e->values[0];
	else
		return c;
}

static inline bool is_wordchar(UChar32 c) {
	return g_unicode_wordchars_map.lookup(c);
}

static inline bool is_alfanumeric(UChar32 c) {
	auto gc = g_unicode_general_category_map.lookup2(c);
	return gc==Unicode::general_category_t::Lu ||
	       gc==Unicode::general_category_t::Ll ||
	       gc==Unicode::general_category_t::Lt ||
	       gc==Unicode::general_category_t::Lm ||
	       gc==Unicode::general_category_t::Lo ||
	       gc==Unicode::general_category_t::Nl ||
	       gc==Unicode::general_category_t::Nd || //decimal digits only
	       g_unicode_properties_map.lookup2(c)&Unicode::Other_Alphabetic;
}

} //namespace

#endif
