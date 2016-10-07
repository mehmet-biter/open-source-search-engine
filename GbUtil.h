#ifndef GB_UTIL_H_
#define GB_UTIL_H_

//Kitchen sink

#include <stddef.h>

class SafeBuf;

//Append string into safebuf, replaing all occurences of ]]> with ]]&gt;
bool cdataEncode(SafeBuf *dstBuf, const char *src);
bool cdataEncode(SafeBuf *dstBuf, const char *src, size_t len);

#endif
