#ifndef GB_UNICODE_H
#define GB_UNICODE_H

#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "UnicodeProperties.h"
#include <iconv.h>

// Initialize unicode word parser
bool 	ucInit(const char *path = NULL);
void ucResetMaps();

//////////////////////////////////////////////////////

// table for decoding utf8...says how many bytes in the character
// based on value of first byte.  0 is an illegal value
static const int bytes_in_utf8_code[] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// next two rows are all illegal, so return 1 byte
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// many for loop add this many bytes to iterate, so since the last
	// 8 entries in this table are invalid, assume 1, not 0
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,1,1,1,1,1,1,1,1
};

//how many bytes does this utf8 initial-byte indicate?
inline char getUtf8CharSize(uint8_t c) {
#if 1
	if ( c < 128 ) {
		return 1;
	} else {
		return bytes_in_utf8_code[c];
	}
#else
	if( ! (c & 0x80)) return 1;
	if( ! (c & 0x20)) return 2;
	if( ! (c & 0x10)) return 3;
	if( ! (c & 0x08)) return 4;
	return 1; //illegal
#endif
}

// how many bytes is char pointed to by p?
inline char getUtf8CharSize ( const uint8_t *p ) {
	return getUtf8CharSize(*p);
}

inline char getUtf8CharSize ( const char *p ) {
	return getUtf8CharSize((const uint8_t*)p);
}


// Valid UTF-8 code points
// +--------------------+----------+----------+----------+----------+
// | Code Points        | 1st Byte | 2nd Byte | 3rd Byte | 4th Byte |
// +--------------------+----------+----------+----------+----------+
// | U+0000..U+007F     | 00..7F   |          |          |          |
// | U+0080..U+07FF     | C2..DF   | 80..BF   |          |          |
// | U+0800..U+0FFF     | E0       | A0..BF   | 80..BF   |          |
// | U+1000..U+FFFF     | E1..EF   | 80..BF   | 80..BF   |          |
// | U+10000..U+3FFFF   | F0       | 90..BF   | 80..BF   | 80..BF   |
// | U+40000..U+FFFFF   | F1..F3   | 80..BF   | 80..BF   | 80..BF   |
// | U+100000..U+10FFFF | F4       | 80..8F   | 80..BF   | 80..BF   |
// +--------------------+----------+----------+----------+----------+
bool inline isValidUtf8Char(const char *s) {
	const uint8_t *u = (uint8_t*)s;

	if (  u[0] <= 0x7F) { // U+0000..U+007F
		return true;
	} else if (u[0] >= 0xC2 && u[0] <= 0xDF) { // U+0080..U+07FF
		if (u[1] >= 0x80 && u[1] <= 0xBF) {
			return true;
		}
	} else if (u[0] == 0xE0) { // U+0800..U+0FFF
		if ((u[1] >= 0xA0 && u[1] <= 0xBF) &&
		    (u[2] >= 0x80 && u[2] <=0xBF)) {
			return true;
		}
	} else if (u[0] >= 0xE1 && u[0] <= 0xEF) { // U+1000..U+FFFF
		if ((u[1] >= 0x80 && u[1] <= 0xBF) &&
			(u[2] >= 0x80 && u[2] <= 0xBF)) {
			return true;
		}
	} else if (u[0] == 0xF0) { // U+10000..U+3FFFF
		if ((u[1] >= 0x90 && u[1] <= 0xBF) &&
			(u[2] >= 0x80 && u[2] <=0xBF) &&
		    (u[3] >= 0x80 && u[3] <=0xBF)) {
			return true;
		}
	} else if (u[0] >= 0xF1 && u[0] <= 0xF3) { // U+40000..U+FFFFF
		if ((u[1] >= 0x80 && u[1] <= 0xBF) &&
		    (u[2] >= 0x80 && u[2] <= 0xBF) &&
		    (u[3] >= 0x80 && u[3] <= 0xBF)) {
			return true;
		}
	} else if (u[0] == 0xF4) { // U+100000..U+10FFFF
		if ((u[1] >= 0x80 && u[1] <= 0x8F) &&
			(u[2] >= 0x80 && u[2] <=0xBF) &&
		    (u[3] >= 0x80 && u[3] <=0xBF)) {
			return true;
		}
	}

	return false;
}

// utf8 bytes. up to 4 bytes in a char:
// 0xxxxxxx
// 110yyyxx 10xxxxxx
// 1110yyyy 10yyyyxx 10xxxxxx
// 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx
// TODO: make a table for this as well
inline bool isFirstUtf8Char(const char *p) {
	// non-first chars have the top bit set and next bit unset
	if ( (p[0] & 0xc0) == 0x80 ) return false;
	// we are the first char in a sequence
	return true;
}

// point to the utf8 char BEFORE "p"
inline char *getPrevUtf8Char ( char *p , char *start ) {
	for ( p-- ; p >= start ; p-- )
		if ( isFirstUtf8Char(p) ) return p;
	return NULL;
}

int32_t ucToUtf8(char *outbuf, int32_t outbuflen,
		const char *inbuf, int32_t inbuflen,
		const char *charset, int32_t ignoreBadChars);

// Encode a code point in UTF-8
int32_t	utf8Encode(UChar32 c, char* buf);

// Try to detect the Byte Order Mark of a Unicode Document
const char *	ucDetectBOM(const char *buf, int32_t bufsize);

//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
//		  unsigned char *inbuf, int32_t inbuflen);
int32_t stripAccentMarks(char *outbuf, int32_t outbufsize,
			 const unsigned char *inbuf, int32_t inbuflen);



//////////////////////////////////////////////////////////////
//  Inline functions
//////////////////////////////////////////////////////////////

// . returns length of byte sequence encoded
// . store the unicode character, "c", as a utf8 character
// . return how many bytes were stored into "buf"
inline int32_t utf8Encode(UChar32 c, char* buf) {
	if (!(c & 0xffffff80)){  
		// 1 byte
		buf[0] = (char)c;
		return 1;
	}
	if (!(c & 0xfffff800)){ 
		// 2 byte
		buf[0] = (char)(0xc0 | (c >> 6 & 0x1f));
		buf[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (!(c & 0xffff0000)){ 
		// 3 byte
		buf[0] = (char)(0xe0 | (c >> 12 & 0x0f));
		buf[1] = (char)(0x80 | (c >> 6 & 0x3f));
		buf[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if (!(c & 0xe0000000)) {
		// 4 byte
		buf[0] = (char)(0xf0 | (c >> 18 & 0x07));//5
		buf[1] = (char)(0x80 | (c >> 12 & 0x3f));//5
		buf[2] = (char)(0x80 | (c >> 6 & 0x3f));//5
		buf[3] = (char)(0x80 | (c & 0x3f));//4
		return 4;
	}
	// illegal character
	return 0;
}

// return the utf8 character at "p" as a 32-bit unicode character
inline UChar32 utf8Decode(const char *p) {
	uint8_t c0 = static_cast<uint8_t>(p[0]);
	if((c0&0x80)==0x00) { //single byte character
		return (UChar32)*p;
	} else if((c0&0xe0)==0xc0 && (p[1]&0xc0)==0x80) { //two or more bytes
		return (UChar32)((*p & 0x1f)<<6 | 
				(*(p+1) & 0x3f));
	} else if((c0&0xf0)==0xe0 && (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80) { //three or more bytes
		return (UChar32)((*p & 0x0f)<<12 | 
				(*(p+1) & 0x3f)<<6 |
				(*(p+2) & 0x3f));
	} else if((c0&0xf8)==0xf0 && (p[1]&0xc0)==0x80 && (p[2]&0xc0)==0x80 && (p[3]&0xc0)==0x80) { //three or more bytes
		return (UChar32)((*p & 0x07)<<18 | 
				(*(p+1) & 0x3f)<<12 |
				(*(p+2) & 0x3f)<<6 |
				(*(p+3) & 0x3f));
	} else { //invalid
		return (UChar32)-1;
	}
}



// Return the number of bytes required to encode a codepoint in UTF-8
static inline int32_t utf8Size(UChar32 codepoint) {
	if(__builtin_expect(codepoint<=0x7F,1))
		return 1;
	if(codepoint<=0x7FF)
		return 2;
	if(codepoint<=0xFFFF)
		return 3;
	return 4;
}

#endif // GB_UNICODE_H
