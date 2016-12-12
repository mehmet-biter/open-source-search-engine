#ifndef GB_ROBOTS_H
#define GB_ROBOTS_H

#include <stdint.h>
#include <vector>
#include "RobotRule.h"

class Url;

class Robots {
public:
	Robots( const char* robotsTxt, int32_t robotsTxtLen, const char *userAgent );

	bool isAllowed( Url *url );
	int32_t getCrawlDelay();

	void print() const;

protected:
	bool getNextLine();

	bool getField( const char **field, int32_t *fieldLen );
	bool getValue( const char **value, int32_t *valueLen );

	const char* getCurrentLine() const { return m_currentLine; }
	int32_t getCurrentLineLen() const { return m_currentLineLen; }

	bool isUserAgentFound() const { return m_userAgentFound; }
	bool isDefaultUserAgentFound() const { return m_defaultUserAgentFound; }

	bool isRulesEmpty() const { return m_rules.empty(); }

	bool isDefaultRulesEmpty() const { return m_defaultRules.empty(); }

private:
	void parse();

	bool parseUserAgent( const char *field, int32_t fieldLen, bool *isUserAgentPtr, bool *isDefaultUserAgentPtr );
	bool parseCrawlDelay( const char *field, int32_t fieldLen, bool isUserAgent );
	void parsePath( bool isAllow, bool isUserAgent );
	bool parseAllow( const char *field, int32_t fieldLen, bool isUserAgent );
	bool parseDisallow( const char *field, int32_t fieldLen, bool isUserAgent );

	const char *m_robotsTxt;
	int32_t m_robotsTxtLen;

	const char *m_currentLine;
	int32_t m_currentLineLen;
	int32_t m_nextLineStartPos;
	int32_t m_valueStartPos;

	const char *m_userAgent;
	int32_t m_userAgentLen;

	bool m_userAgentFound;
	bool m_defaultUserAgentFound;

	int32_t m_crawlDelay;
	int32_t m_defaultCrawlDelay;

	std::vector<RobotRule> m_rules;
	std::vector<RobotRule> m_defaultRules;
};

#endif // GB_ROBOTS_H

