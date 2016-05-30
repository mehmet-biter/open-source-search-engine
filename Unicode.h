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

//////////////////////////////////////////////////////
// Converters
iconv_t gbiconv_open(const char *tocode, const char *fromcode) ;
int gbiconv_close(iconv_t cd) ;

int32_t 	ucToAny(char *outbuf, int32_t outbuflen, const char *charset_out,
		 char *inbuf, int32_t inbuflen, const char *charset_in,
		 int32_t ignoreBadChars,int32_t niceness);

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

// how many bytes is char pointed to by p?
inline char getUtf8CharSize ( const uint8_t *p ) {
	uint8_t c = *p;
	if ( c < 128 ) {
		return 1;
	} else {
		return bytes_in_utf8_code[c];
	}
}

inline char getUtf8CharSize ( const char *p ) {
	uint8_t c = (uint8_t)*p;
	if ( c < 128 ) {
		return 1;
	} else {
		return bytes_in_utf8_code[c];
	}
}

inline char getUtf8CharSize ( uint8_t c ) {
	if ( c < 128 ) {
		return 1;
	} else {
		return bytes_in_utf8_code[c];
	}
}

inline char getUtf8CharSize2 ( const uint8_t *p ) {
	if ( ! (p[0] & 0x80) ) return 1;
	if ( ! (p[0] & 0x20) ) return 2;
	if ( ! (p[0] & 0x10) ) return 3;
	if ( ! (p[0] & 0x08) ) return 4;
	// crazy!!!
	return 1;
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

	if (u[0] >= 0x00 && u[0] <= 0x7F) { // U+0000..U+007F
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

// Refer to:
// http://www.unicode.org/charts/
// http://www.unicode.org/Public/UNIDATA/Blocks.txt
// http://www.utf8-chartable.de/

// Emoji & Pictographs
// 2600–26FF: Miscellaneous Symbols
// 2700–27BF: Dingbats
// 1F300–1F5FF: Miscellaneous Symbols and Pictographs
// 1F600–1F64F: Emoticons
// 1F650–1F67F: Ornamental Dingbats
// 1F680–1F6FF: Transport and Map Symbols
// 1F900–1F9FF: Supplemental Symbols and Pictographs

// Game Symbols
// 1F000–1F02F: Mahjong Tiles
// 1F030–1F09F: Domino Tiles
// 1F0A0–1F0FF: Playing Cards

// Enclosed Alphanumeric Supplement
// 1F1E6–1F1FF: Regional indicator symbols

// Geometric Shapes
// 25A0–25FF: Geometric Shapes

// +--------------------+----------+----------+----------+----------+
// | Code Points        | 1st Byte | 2nd Byte | 3rd Byte | 4th Byte |
// +--------------------+----------+----------+----------+----------+
// | U+25A0..U+25BF     | E2       | 96       | A0..BF   |          |
// | U+25C0..U+27BF     | E2       | 97..9E   | 80..BF   |          |
// | U+1F000..U+1F0FF   | F0       | 9F       | 80..83   | 80..BF   |
// | U+1F1E6..U+1F1FF   | F0       | 9F       | 87       | A6..BF   |
// | U+1F300..U+1F6FF   | F0       | 9F       | 8C..9B   | 80..BF   |
// | U+1F900..U+1F9FF   | F0       | 9F       | A4..A7   | 80..BF   |
// +--------------------+----------+----------+----------+----------+
bool inline isUtf8UnwantedSymbols(const char *s) {
	const uint8_t *u = (uint8_t *)s;

	if ( u[0] == 0xE2 ) {
		if ( ( u[1] == 0x96 ) &&
		     ( u[2] >= 0xA0 && u[2] <= 0xBF ) ) {
			return true;
		} else if ( ( u[1] >= 0x97 && u[1] <= 0x9E ) &&
		            ( u[2] >= 0x80 && u[2] <= 0xBF ) ) { // U+25C0..U+27BF
			return true;
		}
	} else if ( u[0] == 0xF0 && u[1] == 0x9F ) {
		if ( ( u[2] >= 0x80 && u[2] <= 0x83 ) &&
		     ( u[3] >= 0x80 && u[3] <= 0xBF ) ) { // U+1F000..U+1F0FF
			return true;
		} else if ( ( u[2] == 0x87 ) &&
		            ( u[3] >= 0xA6 && u[3] <= 0xBF ) ) { // U+1F1E6..U+1F1FF
			return true;
		} else if ( ( u[2] >= 0x8C && u[2] <= 0x9B ) &&
					( u[3] >= 0x80 && u[3] <= 0xBF ) ) { // U+1F300..U+1F6FF
			return true;
		} else if ( ( u[2] >= 0xA4 && u[2] <= 0xA7 ) &&
					( u[3] >= 0x80 && u[3] <= 0xBF ) ) { // U+1F900..U+1F9FF
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
inline char isFirstUtf8Char ( char *p ) {
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

inline int32_t ucToUtf8(char *outbuf, int32_t outbuflen, 
			 char *inbuf, int32_t inbuflen, 
			 const char *charset, int32_t ignoreBadChars,
		     int32_t niceness) {
  return ucToAny(outbuf, outbuflen, (char *)"UTF-8",
		 inbuf, inbuflen, charset, ignoreBadChars,niceness);
}

// Encode a code point in UTF-8
int32_t	utf8Encode(UChar32 c, char* buf);

// Try to detect the Byte Order Mark of a Unicode Document
const char *	ucDetectBOM(char *buf, int32_t bufsize);

//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
//		  unsigned char *inbuf, int32_t inbuflen);
int32_t stripAccentMarks(char *outbuf, int32_t outbufsize,
		      unsigned char *inbuf, int32_t inbuflen);



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
	if (!(c & 0xe0)){ 
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
inline UChar32 utf8Decode(const char *p){
	// single byte character
	if (!(*p & 0x80)){
		//*next = (char*) p + 1;
		return (UChar32)*p;
	}
	// 2 bytes
	else if (!(*p & 0x20)){
		//*next = (char*) p + 2;
		return (UChar32)((*p & 0x1f)<<6 | 
				(*(p+1) & 0x3f));
	}
	// 3 bytes
	else if (!(*p & 0x10)){
		//*next = (char*) p + 3;
		return (UChar32)((*p & 0x0f)<<12 | 
				(*(p+1) & 0x3f)<<6 |
				(*(p+2) & 0x3f));
	}
	// 4 bytes
	else if (!(*p & 0x08)){
		//*next = (char*) p + 4;
		return (UChar32)((*p & 0x07)<<18 | 
				(*(p+1) & 0x3f)<<12 |
				(*(p+2) & 0x3f)<<6 |
				(*(p+3) & 0x3f));
	}
	// invalid
	else{
		//*next = (char*) p + 1;
		return (UChar32)-1;
	}
}

////////////////////////////////////////////////////

// JAB: returns the number of bytes required to encode character c in UTF-8
inline int32_t utf8Size(UChar32 c){
  if ((c & 0xFFFFFF80) == 0) return 1;
  if ((c & 0xFFFFF800) == 0) return 2;
  if ((c & 0xFFFF0000) == 0) return 3;
  if ((c & 0xFFE00000) == 0) return 4;
  if ((c & 0xFC000000) == 0) return 5;
	return 6;
}

#endif // GB_UNICODE_H
