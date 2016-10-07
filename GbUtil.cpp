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
