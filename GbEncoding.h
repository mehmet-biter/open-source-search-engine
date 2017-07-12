#ifndef GB_GBENCODING_H
#define GB_GBENCODING_H

#include <stdint.h>

class HttpMime;

namespace GbEncoding {
	uint16_t getCharset(HttpMime *mime, const char *url, const char *s, int32_t slen);
};


#endif //GB_GBENCODING_H
