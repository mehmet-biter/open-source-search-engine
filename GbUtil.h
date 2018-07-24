#ifndef GB_UTIL_H_
#define GB_UTIL_H_

//Kitchen sink

#include <stddef.h>
#include <inttypes.h>
#include <time.h>
#include <string.h>
#include <vector>
#include <string>

class SafeBuf;

//Append string into safebuf, replaing all occurences of ]]> with ]]&gt;
bool cdataEncode(SafeBuf *dstBuf, const char *src);
bool cdataEncode(SafeBuf *dstBuf, const char *src, size_t len);

//Encode an URL by escaping certain characters
bool urlEncode(SafeBuf *dstBuf, const char *ssrc);
bool urlEncode(SafeBuf *dstBuf,
               const char *src, size_t slen,
	       bool requestPath = false,
	       bool encodeApostrophes = false);


// like "1 minute ago" "5 hours ago" "3 days ago" etc.
// 'ago' is the delta-t in seconds
bool printTimeAgo(SafeBuf *sb, time_t ago, time_t now, bool shorthand);

std::vector<std::string> split(const std::string &str, char delimiter);
std::vector<std::string> split(const std::string &str, const std::string &delimiter);

bool starts_with(const char *haystack, const char *needle);

uint64_t getCurrentTimeNanoseconds();

const char *formatTime(time_t when, char buf[32]);
const char *formatTimeMs(int64_t when, char buf[32]);

bool has_space(const char *start, const char *end); //space, tab, cr, nl

static inline bool has_char(const char *start, const char *end, char c) {
	return memchr(start,c,end-start)!=0;
}


#endif
