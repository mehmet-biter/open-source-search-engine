#ifndef UNICODEH
#define UNICODEH

#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include "UnicodeProperties.h"
#include "iconv.h"

// Initialize unicode word parser
bool 	ucInit(char *path = NULL, bool verifyFiles = false);

//////////////////////////////////////////////////////
// Converters
iconv_t gbiconv_open(char *tocode, char *fromcode) ;
int gbiconv_close(iconv_t cd) ;

int32_t 	ucToAny(char *outbuf, int32_t outbuflen, char *charset_out,
		 char *inbuf, int32_t inbuflen, char *charset_in,
		 int32_t ignoreBadChars,int32_t niceness);

// table for decoding utf8...says how many bytes in the character
// based on value of first byte.  0 is an illegal value
static int bytes_in_utf8_code[] = {
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

static int utf8_sane[] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// next two rows are all illegal, so return 1 byte
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

	// many for loop add this many bytes to iterate, so since the last
	// 8 entries in this table are invalid, assume 1, not 0
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
};

// how many bytes is char pointed to by p?
inline char getUtf8CharSize ( uint8_t *p ) {
	uint8_t c = *p;
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize ( char *p ) {
	uint8_t c = (uint8_t)*p;
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize ( uint8_t c ) {
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize2 ( uint8_t *p ) {
        if ( ! (p[0] & 0x80) ) return 1;
	if ( ! (p[0] & 0x20) ) return 2;
	if ( ! (p[0] & 0x10) ) return 3;
	if ( ! (p[0] & 0x08) ) return 4;
	// crazy!!!
	return 1;
}

inline char isSaneUtf8Char ( uint8_t *p ) {
	if(p[0]<128)
		return 1;
	else
		return utf8_sane[p[0]];
}

inline char isSaneUtf8Char ( char *p ) {
	uint8_t c = (uint8_t)*p;
	if(c<128)
		return 1;
	else
		return utf8_sane[c];
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
			 char *charset, int32_t ignoreBadChars,
		     int32_t niceness) {
  return ucToAny(outbuf, outbuflen, (char *)"UTF-8",
		 inbuf, inbuflen, charset, ignoreBadChars,niceness);
}

// Encode a code point into latin-1, return 0 if not able to
uint8_t latin1Encode ( UChar32 c );

// Encode a code point in UTF-8
int32_t	utf8Encode(UChar32 c, char* buf);

// Try to detect the Byte Order Mark of a Unicode Document
char *	ucDetectBOM(char *buf, int32_t bufsize);

// Special case converter...for web page output
int32_t latin1ToUtf8(char *outbuf, int32_t outbufsize,
		  char *inbuf, int32_t inbuflen);

//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
//		  unsigned char *inbuf, int32_t inbuflen);
int32_t stripAccentMarks(char *outbuf, int32_t outbufsize,
		      unsigned char *inbuf, int32_t inbuflen);



//////////////////////////////////////////////////////////////
//  Inline functions
//////////////////////////////////////////////////////////////

// . convert a unicode char into latin1
// . returns 0 if could not do it
// . see UNIDATA/NamesList.txt for explanation of all UChar32 values
// . seems like Unicode is conventiently 1-1 with latin1 for the first 256 vals
inline uint8_t latin1Encode ( UChar32 c ) {
	// keep ascii chars as ascii
	if ( c <= 255 ) return (uint8_t)c;
	// that ain't latin-1!
	return 0;
}

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
inline UChar32 utf8Decode(char *p){//, char **next){
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

#endif
