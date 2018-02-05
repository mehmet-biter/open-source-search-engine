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





// Try to detect the Byte Order Mark of a Unicode Document
const char *	ucDetectBOM(const char *buf, int32_t bufsize);

//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
//		  unsigned char *inbuf, int32_t inbuflen);
int32_t stripAccentMarks(char *outbuf, int32_t outbufsize,
			 const unsigned char *inbuf, int32_t inbuflen);



//////////////////////////////////////////////////////////////
//  Inline functions
//////////////////////////////////////////////////////////////

#endif // GB_UNICODE_H
