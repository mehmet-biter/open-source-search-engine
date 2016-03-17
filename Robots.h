#ifndef GB_ROBOTS_H
#define GB_ROBOTS_H

#include <stdint.h>

class Url;

class Robots {
public:
	static bool isAllowed ( Url *url, char *userAgent, char *file, int32_t fileLen, bool *userAgentFound, bool substringMatch,
	                        int32_t *crawlDelay, char **cacheStart, int32_t *cacheLen, bool *hadAllowOrDisallow );
};

#endif // GB_ROBOTS_H

