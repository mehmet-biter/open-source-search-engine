#include "GbUtil.h"
#include "SafeBuf.h"
#include <string.h>
#include <sstream>

bool cdataEncode(SafeBuf *dstBuf, const char *src) {
	return cdataEncode(dstBuf,src,strlen(src));
}


bool cdataEncode(SafeBuf *dstBuf, const char *src, size_t len) {
	if(len<3)
		return dstBuf->safeMemcpy(src,len);
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



bool printTimeAgo(SafeBuf *sb, time_t ago, time_t now, bool shorthand) {
	if(! sb->reserve(200))
		return false;
	
	if(ago<0)
		ago = 0;
	int secs = (int)((ago)/1);
	int mins = (int)((ago)/60);
	int hrs  = (int)((ago)/3600);
	int days = (int)((ago)/(3600*24));
	
	bool printed = true;
	// print the time ago
	if(shorthand) {
		if(mins == 0)
			sb->safePrintf("%d secs ago",secs);
		else if(mins == 1)
			sb->safePrintf("%d min ago",mins);
		else if (mins < 60)
			sb->safePrintf("%d mins ago",mins);
		else if(hrs == 1)
			sb->safePrintf("%d hr ago",hrs);
		else if(hrs < 24)
			sb->safePrintf("%d hrs ago",hrs);
		else if(days == 1)
			sb->safePrintf("%d day ago",days);
		else if (days < 7)
			sb->safePrintf("%d days ago",days);
		else
			printed = false;
	} else {
		if(mins == 0)
			sb->safePrintf("%d seconds ago",secs);
		else if(mins == 1)
			sb->safePrintf("%d minute ago",mins);
		else if (mins < 60)
			sb->safePrintf("%d minutes ago",mins);
		else if(hrs == 1)
			sb->safePrintf("%d hour ago",hrs);
		else if(hrs < 24)
			sb->safePrintf("%d hours ago",hrs);
		else if(days == 1)
			sb->safePrintf("%d day ago",days);
		else if(days < 7)
			sb->safePrintf("%d days ago",days);
		else
			printed = false;
	}

	if(!printed && ago > 0) {
		time_t ts = now - ago;
		struct tm tm_buf;
		struct tm *timeStruct = localtime_r(&ts,&tm_buf);
		char tmp[100];
		strftime(tmp,100,"%B %d %Y",timeStruct);
		sb->safeStrcpy(tmp);
	}
	return true;
}

std::vector<std::string> split(const std::string &str, char delimiter) {
	std::vector<std::string> elements;
	std::stringstream ss(str);
	std::string element;
	while (std::getline(ss, element, delimiter)) {
		elements.push_back(element);
	}
	return elements;
}

bool starts_with(const char *haystack, const char *needle) {
	size_t haystackLen = strlen(haystack);
	size_t needleLen = strlen(needle);
	if (haystackLen < needleLen) {
		return false;
	}

	return (memcmp(haystack, needle, needleLen) == 0);
}