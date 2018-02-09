#ifndef UTF8_H_
#define UTF8_H_
#include <inttypes.h>
#include <stddef.h>


//Various functions for examining and manipulating UTF-8 data

typedef uint32_t  UChar32;

bool verifyUtf8(const char *txt);
bool verifyUtf8(const char *txt, int32_t tlen);


extern const int bytes_in_utf8_code[256];

//how many bytes does this utf8 initial-byte indicate?
static inline char getUtf8CharSize(uint8_t c) {
#if 0
	//partially table-driven. Seems to be slower on modern OoO processors
	if(c < 128)
		return 1;
	else
		return bytes_in_utf8_code[c];
#else
	//conditional-jump-driven. Seems to be faster on modern OoO processors
	if((c & 0x80)==0) return 1;
	if((c & 0x20)==0) return 2;
	if((c & 0x10)==0) return 3;
	if((c & 0x08)==0) return 4;
	return 1; //illegal
#endif
}

// how many bytes is char pointed to by p?
static inline char getUtf8CharSize(const uint8_t *p) {
	return getUtf8CharSize(*p);
}

static inline char getUtf8CharSize(const char *p) {
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
static bool inline isValidUtf8Char(const char *s) {
	const uint8_t *u = (uint8_t*)s;

	if(u[0] <= 0x7F) { // U+0000..U+007F
		return true;
	} else if(u[0] >= 0xC2 && u[0] <= 0xDF) { // U+0080..U+07FF
		if(u[1] >= 0x80 && u[1] <= 0xBF) {
			return true;
		}
	} else if(u[0] == 0xE0) { // U+0800..U+0FFF
		if((u[1] >= 0xA0 && u[1] <= 0xBF) &&
		    (u[2] >= 0x80 && u[2] <=0xBF)) {
			return true;
		}
	} else if(u[0] >= 0xE1 && u[0] <= 0xEF) { // U+1000..U+FFFF
		if((u[1] >= 0x80 && u[1] <= 0xBF) &&
			(u[2] >= 0x80 && u[2] <= 0xBF)) {
			return true;
		}
	} else if(u[0] == 0xF0) { // U+10000..U+3FFFF
		if((u[1] >= 0x90 && u[1] <= 0xBF) &&
			(u[2] >= 0x80 && u[2] <=0xBF) &&
		    (u[3] >= 0x80 && u[3] <=0xBF)) {
			return true;
		}
	} else if(u[0] >= 0xF1 && u[0] <= 0xF3) { // U+40000..U+FFFFF
		if((u[1] >= 0x80 && u[1] <= 0xBF) &&
		    (u[2] >= 0x80 && u[2] <= 0xBF) &&
		    (u[3] >= 0x80 && u[3] <= 0xBF)) {
			return true;
		}
	} else if(u[0] == 0xF4) { // U+100000..U+10FFFF
		if((u[1] >= 0x80 && u[1] <= 0x8F) &&
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
static inline bool isFirstUtf8Char(const char *p) {
	// non-first chars have the top bit set and next bit unset
	if((p[0] & 0xc0) == 0x80)
		return false;
	// we are the first char in a sequence
	return true;
}

// point to the utf8 char BEFORE "p"
static inline char *getPrevUtf8Char(char *p, char *start) {
	for(p-- ; p >= start ; p--)
		if(isFirstUtf8Char(p))
			return p;
	return NULL;
}


// Encode the unicode codepint 'c' as utf-8 into 'buf'. Returns length of encoded codepoint in bytes.
static inline int32_t utf8Encode(UChar32 c, char *buf) {
	if(!(c & 0xffffff80)) {  
		// 1 byte
		buf[0] = (char)c;
		return 1;
	}
	if(!(c & 0xfffff800)) { 
		// 2 byte
		buf[0] = (char)(0xc0 | (c >> 6 & 0x1f));
		buf[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if(!(c & 0xffff0000)) { 
		// 3 byte
		buf[0] = (char)(0xe0 | (c >> 12 & 0x0f));
		buf[1] = (char)(0x80 | (c >> 6 & 0x3f));
		buf[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if(!(c & 0xe0000000)) {
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

//Decode the UTF-8-encoded codepoint at 'p'
static inline UChar32 utf8Decode(const char *p) {
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



//Returns the number of bytes required to encode a codepoint in UTF-8
static inline int32_t utf8Size(UChar32 codepoint) {
	if(__builtin_expect(codepoint<=0x7F,1))
		return 1;
	if(codepoint<=0x7FF)
		return 2;
	if(codepoint<=0xFFFF)
		return 3;
	return 4;
}

#endif //UTF8_H_
