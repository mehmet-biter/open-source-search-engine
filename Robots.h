#ifndef GB_ROBOTS_H
#define GB_ROBOTS_H

#include <stdint.h>
#include <list>

class Url;

class RobotRule {
public:
	enum RuleType {
		TYPE_ALLOW,
		TYPE_DISALLOW
	};

	RobotRule( RuleType type, const char *path, int32_t pathLen );

	bool isMatching( Url *url );

	bool isAllow();
	bool isDisallow();

private:
	RuleType m_type;
	const char *m_path;
	int32_t m_pathLen;
};

class Robots {
public:
	Robots( const char* robotsTxt, int32_t robotsTxtLen, const char *userAgent );
	int32_t getCrawlDelay();

	static bool isAllowed( Url *url, const char *userAgent, const char *file, int32_t fileLen,
	                       bool *userAgentFound, bool substringMatch, int32_t *crawlDelay,
	                       bool *hadAllowOrDisallow );

protected:
	bool getNextLine( int32_t *startPos, const char **line, int32_t *lineLen );

	bool getField( const char *line, int32_t lineLen, int32_t *valueStartPos, const char **field, int32_t *fieldLen );
	bool getValue( const char *line, int32_t lineLen, int32_t valueStartPos, const char **value, int32_t *valueLen );

	bool isUserAgentFound();
	bool isDefaultUserAgentFound();

	bool isRulesEmpty();
	bool isDefaultRulesEmpty();

private:
	bool parse();

	const char *m_robotsTxt;
	int32_t m_robotsTxtLen;

	const char *m_userAgent;
	int32_t m_userAgentLen;

	bool m_userAgentFound;
	bool m_defaultUserAgentFound;

	int32_t m_crawlDelay;
	int32_t m_defaultCrawlDelay;

	std::list<RobotRule> m_rules;
	std::list<RobotRule> m_defaultRules;
};

#endif // GB_ROBOTS_H

