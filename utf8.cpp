#include "utf8.h"
#include <string.h>

bool verifyUtf8(const char *txt, int32_t tlen) {
	if( ! txt  || tlen <= 0)
		return true;
	char size;
	const char *p = txt;
	const char *pend = txt + tlen;
	for( ; p < pend; p += size) {
		size = getUtf8CharSize(p);
		// skip if ascii
		if( ! (p[0] & 0x80))
			continue;
		// ok, it's a utf8 char, it must have both hi bits set
		if( (p[0] & 0xc0) != 0xc0)
			return false;
		// if only one byte, we are done..  how can that be?
		if( size == 1)
			return false;
		//if ( ! utf8IsSane ( p[0] ) ) return false;
		// successive utf8 chars must have & 0xc0 be equal to 0x80
		// but the first char it must equal 0xc0, both set
		if( (p[1] & 0xc0) != 0x80)
			return false;
		if( size == 2)
			continue;
		if( (p[2] & 0xc0) != 0x80)
			return false;
		if( size == 3)
			continue;
		if( (p[3] & 0xc0) != 0x80)
			return false;
	}
	if(p != pend)
		return false;
	return true;
}


bool verifyUtf8 ( const char *txt ) {
	int32_t tlen = strlen(txt);
	return verifyUtf8(txt,tlen);
}


// table for decoding utf8...says how many bytes in the character
// based on value of first byte.  0 is an illegal value
const int bytes_in_utf8_code[] = {
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


int decode_utf8_string(const char *utf8, size_t utf8len, UChar32 uc[]) {
	const char *p = utf8;
	const char *end = utf8+utf8len;
	int codepoints = 0;
	while(p<end) {
		int cs = getUtf8CharSize(p);
		if(p+cs>end)
			return -1; //decode error
		uc[codepoints++] = utf8Decode(p);
		p += cs;
	}
	return codepoints;
}

size_t encode_utf8_string(UChar32 uc[], unsigned codepoints, char *utf8) {
	size_t utf8len = 0;
	for(unsigned i=0; i<codepoints; i++)
		utf8len += utf8Encode(uc[i], utf8+utf8len);
	return utf8len;
}
