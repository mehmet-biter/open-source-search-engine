#ifndef GB_ROBOTS_H
#define GB_ROBOTS_H

#include <stdint.h>
#include <list>

class Url;

class Robots {
public:
	Robots( const char* robotsTxt, int32_t robotsTxtLen, const char *userAgent );

	static bool isAllowed( Url *url, const char *userAgent, const char *file, int32_t fileLen,
	                       bool *userAgentFound, bool substringMatch, int32_t *crawlDelay,
	                       bool *hadAllowOrDisallow );

protected:
	bool getNextLine( int32_t *startPos, const char **line, int32_t *lineLen );

	bool getField( const char *line, int32_t lineLen, int32_t *valueStartPos, const char **field, int32_t *fieldLen );
	bool getValue( const char *line, int32_t lineLen, int32_t valueStartPos, const char **value, int32_t *valueLen );

	bool parse();

private:
	const char *m_robotsTxt;
	int32_t m_robotsTxtLen;

	const char *m_userAgent;
	int32_t m_userAgentLen;

	std::list<std::pair<int32_t, const char*> > m_allow;
	std::list<std::pair<int32_t, const char*> > m_disallow;

	std::list<std::pair<int32_t, const char*> > m_defaultAllow;
	std::list<std::pair<int32_t, const char*> > m_defaultDisallow;
};

#endif // GB_ROBOTS_H

