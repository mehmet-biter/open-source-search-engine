#include "utf8_fast.h"


bool has_alpha_utf8(const char *s, const char *send) {
	char cs = 0;
	for ( ; s < send ; s += cs ) {
		cs = getUtf8CharSize ( s );
		if ( cs == 1 ) {
			if (is_alpha_a(*s)) return true;
			continue;
		}
		if ( is_alpha_utf8(s) ) return true;
	}
	return false;
}

bool is_alnum_utf8_string(const char *s, const char *send) {
	char cs = 0;
	for( ; s < send ; s += cs ) {
		cs = getUtf8CharSize(s);
		if(cs == 1) {
			if(!is_alnum_a(*s))
				return false;
		} else {
			if(!is_alnum_utf8(s) )
				return false;
		}
	}
	return true;
}

bool is_alnum_api_utf8_string(const char *s, const char *send) {
	if(s==send)
		return false; //empty string is not an identifyer
	if(*s<32 || *s>=128)
		return false; //first char must be ascii
	if(*s!='_' && !is_alpha_a(*s)) //first char must be underscore or letter
		return false;
	s++;
	char cs = 0;
	for( ; s < send ; s += cs ) {
		cs = getUtf8CharSize(s);
		if(cs == 1) {
			if(*s>=128 || !is_alnum_a(*s))
				return false;
		} else
			return false; //must be ascii
	}
	return true;
}

// . returns bytes stored into "dst" from "src"
// . just do one character, which may be from 1 to 4 bytes
int32_t to_lower_utf8(char *dst, const char *src) {
	// if in ascii do it quickly
	if(is_ascii3(*src)) {
		*dst = to_lower_a ( *src );
		return 1;
	}
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode(y, dst);
}

int32_t to_lower_utf8(char *dst, char * /*dstEnd*/, const char *src, const char *srcEnd) {
	char *dstart = dst;
	for ( ; src < srcEnd ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

int32_t to_lower_utf8(char *dst, char * /*dstEnd*/, const char *src ) {
	char *dstart = dst;
	for ( ; *src ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

int32_t to_upper_utf8(char *dst, const char *src) {
	if(is_ascii3(*src)) {
		*dst = to_upper_a(*src);
		return 1;
	}
	UChar32 x = utf8Decode(src);
	UChar32 y = ucToUpper(x);
	return utf8Encode(y, dst);
}
