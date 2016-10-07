#include "GbUtil.h"
#include "SafeBuf.h"
#include <string.h>

bool cdataEncode(SafeBuf *dstBuf, const char *src) {
	return cdataEncode(dstBuf,src,strlen(src));
}


bool cdataEncode(SafeBuf *dstBuf, const char *src, size_t len) {
	if(!dstBuf->reserve(len))
		return false;
	const char *endptr = src+len;
	const char *early_endptr = endptr-2;
	for(const char *s = src; s<early_endptr; ) {
		bool b;
		if(s[0]!=']' || s[1]!=']' || s[2]!='>') {
			b = dstBuf->pushChar(*s);
			s += 1;
		} else {
			//turn ]]> into ]]&gt;
			b = dstBuf->pushChar(']') &&
			    dstBuf->pushChar(']') &&
			    dstBuf->pushChar('&') &&
			    dstBuf->pushChar('g') &&
			    dstBuf->pushChar('t') &&
			    dstBuf->pushChar(';');
			s += 3;
		}
		if(!b) return false;
	}
	//handle 2-byte tail
	for(const char *s=early_endptr; s<endptr; s++)
		if(!dstBuf->pushChar(*s))
			return false;
	return true;
}



bool urlEncode(SafeBuf *dstBuf, const char *src) {
	return urlEncode(dstBuf, src, strlen(src), false, false);
}


// non-ascii (<32, >127): requires encoding
// space, ampersand, quote, plus, percent, hash, less, greater, question, colon, slash: requires encoding.
// rest: no encoding required
static const bool s_charRequiresUrlEncoding[256] = {
	//0    1      2      3      4      5      6      7      8      9      A      B      C      D      E      F
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , false, true , true , false, true , true , false, false, false, false, true , false, false, false, true ,
	false, false, false, false, false, false, false, false, false, false, true , false, true , false, true , true ,
	false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
	false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true ,
	true , true , true , true , true , true , true , true , true , true , true , true , true , true , true , true
};

//s is a url, make it safe for printing to html
bool urlEncode(SafeBuf *dstBuf,
               const char *src, size_t slen,
	       bool requestPath,
	       bool encodeApostrophes)
{
	const char *send = src + slen;
	for(const char *s=src ; s < send ; s++ ) {
		if(*s == '\0' && requestPath) {
			dstBuf->pushChar(*s);
			continue;
		}
		if(*s == '\'' && encodeApostrophes) {
			dstBuf->safeMemcpy("%27",3);
			continue;
		}

		// skip if no encoding required
		if(!s_charRequiresUrlEncoding[(unsigned char)*s]) {
			dstBuf->pushChar(*s);
			continue;
		}
		// special case for question-mark
		if(*s == '?' && requestPath) {
			dstBuf->pushChar(*s);
			continue;
		}

		// space to +
		if(*s == ' ') {
			dstBuf->pushChar('+');
			continue;
		}
		
		dstBuf->pushChar('%');
		unsigned char v0 = ((unsigned char)*s)/16 ;
		unsigned char v1 = ((unsigned char)*s) & 0x0f ;
		dstBuf->pushChar("0123456789ABCDEF"[v0]);
		dstBuf->pushChar("0123456789ABCDEF"[v1]);
	}
	dstBuf->nullTerm();
	return true; //todo: uhm... aren't we supposed to return success/failure?
}
