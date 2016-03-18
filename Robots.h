#ifndef GB_ROBOTS_H
#define GB_ROBOTS_H

#include <stdint.h>

class Url;

class Robots {
public:
	static bool isAllowed ( Url *url, const char *userAgent, const char *file, int32_t fileLen, bool *userAgentFound, bool substringMatch,
	                        int32_t *crawlDelay, bool *hadAllowOrDisallow );
};

#endif // GB_ROBOTS_H

