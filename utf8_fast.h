#ifndef UTF8_FAST_H_
#define UTF8_FAST_H_
#include "utf8.h"
#include "unicode/UCMaps.h"

//Various functions for examining and manipulating UTF8 chars/strings and
//unicode codepoints.
//Optimized for the expectation that the text we normally encounter has most
//of the codepoints in the range 0..128


bool has_alpha_utf8(const char *s, const char *send);

int32_t to_lower_utf8(char *dst, const char *src);
int32_t to_lower_utf8(char *dst, char *dstEnd, const char *src);
int32_t to_lower_utf8(char *dst, char *dstEnd, const char *src, const char *srcEnd);
int32_t to_upper_utf8(char *dst, const char *src);                                          //one codepoint only


extern const unsigned char g_map_to_lower[256];
extern const unsigned char g_map_to_upper[256];
extern const char g_map_is_upper[256];
extern const char g_map_is_binary[256];
extern const char g_map_is_lower[256];
extern const char g_map_is_ascii[256];
extern const char g_map_is_punct[256];
extern const char g_map_is_alnum[256];
extern const char g_map_is_alpha[256];
extern const char g_map_is_digit[256];
extern const char g_map_is_hex[256];
extern const char g_map_is_tagname_char[256];

//Table lookups
static inline bool is_lower_a(char c)      { return g_map_is_lower[(unsigned char)c]; }
static inline char to_lower_a(char c)      { return g_map_to_lower[(unsigned char)c]; }
static inline bool is_upper_a(char c)      { return g_map_is_upper[(unsigned char)c]; }
static inline char to_upper_a(char c)      { return g_map_to_upper[(unsigned char)c]; }
// c is latin1 in this case:
static inline bool is_binary_a(char c)     { return g_map_is_binary[(unsigned char)c]; }
static inline bool is_wspace_a(char c)     { return (((c)==32) || ((c)==9) || ((c)==10) || ((c)==13)); }
static inline bool is_ascii(char c)        { return (((c)>=32) && ((c)<=126)); }
static inline bool is_ascii3(char c)       { return ((unsigned char)(c)<128); }
static inline bool is_punct_a(char c)      { return g_map_is_punct[(unsigned char)c]; }
static inline bool is_alnum_a(char c)      { return g_map_is_alnum[(unsigned char)c]; }
static inline bool is_alpha_a(char c)      { return g_map_is_alpha[(unsigned char)c]; }
static inline bool is_digit(char c)        { return g_map_is_digit[(unsigned char)c]; }
static inline bool is_hex(char c)          { return g_map_is_hex[(unsigned char)c]; }
static inline bool is_tagname_char(char c) { return g_map_is_tagname_char[(unsigned char)c]; }


static inline bool is_ascii2_a(const char *s, int32_t len) {
	for(int32_t i=0;i<len;i++)
		if(!is_ascii(s[i]))
			return false;
	return true;
}

static inline void to_lower3_a(const char *s, int32_t len, char *buf) {
	for(int32_t i=0;i<len ;i++)
		buf[i]=to_lower_a((unsigned char)s[i]);
}

static inline bool is_binary_utf8(const char *p) {
	if(getUtf8CharSize((uint8_t *)p) != 1)
		return false;
	return is_binary_a(*p);
}

static inline bool is_lower_utf8(const char *src) {
	if(is_ascii3(*src))
		return is_lower_a(*src);
	return UnicodeMaps::is_lowercase(utf8Decode(src));
}

static inline bool is_upper_utf8(const char *src) {
	if(is_ascii3(*src))
		return is_upper_a(*src);
	return UnicodeMaps::is_uppercase(utf8Decode(src));
}

static inline bool is_alnum_utf8(const char *src) {
	if(is_ascii3(*src))
		return is_alnum_a(*src);
	return UnicodeMaps::is_wordchar(utf8Decode(src));
	//todo/bug: the call to is_wordchar() in is_alnum_utf8() looks suspicious. Shouldn't it be is_alfanumeric() ?
}

static inline bool is_alnum_utf8(const uint8_t *src) {
	return is_alnum_utf8((const char*)src);
}


bool is_alnum_utf8_string(const char *s, const char *send);
bool is_upper_utf8_string(const char *s, const char *send); //string does not contain any lowercase letters

bool is_alnum_api_utf8_string(const char *s, const char *send); //starts with letter or underscore, contains only ascii letters/digits and underscore


static inline bool is_alpha_utf8(const char *src) {
	if(is_ascii3(*src))
		return is_alpha_a(*src);
	return UnicodeMaps::is_alphabetic(utf8Decode(src));
}


static inline bool is_punct_utf8(const char *src) {
	if(is_ascii3(*src)) return is_punct_a(*src);
	UChar32 x = utf8Decode(src);
	if(UnicodeMaps::is_wordchar(x)) //todo/bug: should it call is_alfanumeric()?
		return false;
	else
		return true;
}


static inline bool is_wspace_utf8(const char *src) {
	if(is_ascii3(*src))
		return is_wspace_a(*src);
	return UnicodeMaps::is_whitespace(utf8Decode(src));
}

static inline bool is_wspace_utf8(const uint8_t *src) {
	return is_wspace_utf8((const char*)src);
}


static inline bool ucIsWordChar_fast(UChar32 c) {
	if (!(c & 0xffffff80))
		return is_alnum_a(c);
	return UnicodeMaps::is_wordchar(c);
}

static inline bool is_ignorable_fast(UChar32 c) {
	if(c<0x034F) {
		return c==0x00AD; //soft-hyphen is the only ignorable codepoint in the low range
	} else
		return UnicodeMaps::is_ignorable(c);
}

#endif //UTF8_FAST_H_
