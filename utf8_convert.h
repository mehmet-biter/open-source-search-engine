#ifndef UTF8_CONVERT_H_
#define UTF8_CONVERT_H_
#include <inttypes.h>

//functions for converting variaous encodings into UTF-8
//ok, one function.

int32_t ucToUtf8(char *outbuf, int32_t outbuflen,
		 const char *inbuf, int32_t inbuflen,
		 const char *charset, int32_t ignoreBadChars);

bool utf8_convert_initialize();
void utf8_convert_finalize();

#endif
