#include "Robots.h"
#include "Url.h"
#include "fctypes.h"
#include "Log.h"
#include "Conf.h"
#include <functional>
#include <algorithm>

Robots::Robots( const char *robotsTxt, int32_t robotsTxtLen, const char *userAgent )
	: m_robotsTxt( robotsTxt )
	, m_robotsTxtLen( robotsTxtLen )
	, m_currentLine ( NULL )
	, m_currentLineLen ( 0 )
	, m_nextLineStartPos( 0 )
	, m_valueStartPos( 0 )
	, m_userAgent( userAgent )
	, m_userAgentLen( strlen( userAgent ) )
	, m_userAgentFound( false )
	, m_defaultUserAgentFound( false )
	, m_crawlDelay( -1 )
	, m_defaultCrawlDelay( -1 ) {
	if ( m_robotsTxt && m_robotsTxtLen > 0 ) {
		// parse robots.txt into what we need
		parse();
	}
}

bool Robots::getNextLine() {
	// clear values
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;

	int32_t currentPos = m_nextLineStartPos;

	// strip starting whitespaces
	while ( is_wspace_a( m_robotsTxt[currentPos] ) && ( currentPos < m_robotsTxtLen ) ) {
		++currentPos;
	}

	// don't cross limit
	if ( currentPos == m_robotsTxtLen ) {
		return false;
	}

	int32_t linePos = currentPos;
	m_currentLine = m_robotsTxt + currentPos;

	bool foundLineLen = false;
	// get line ending
	{
		bool foundLineEnding = false;
		char currentChar = m_robotsTxt[currentPos];
		while ( currentPos < m_robotsTxtLen ) {
			if ( !foundLineEnding ) {
				if ( currentChar == '#' || currentChar == '\r' || currentChar == '\n' ) {
					if ( !foundLineLen ) {
						foundLineLen = true;
						m_currentLineLen = currentPos - linePos;
					}

					if ( currentChar != '#' ) {
						foundLineEnding = true;
					}
				}
			} else {
				if ( currentChar != '\r' && currentChar != '\n' ) {
					break;
				}
			}

			currentChar = m_robotsTxt[++currentPos];
		}
	}

	// sanity check
	if ( currentPos <= m_nextLineStartPos ) {
		return false;
	}

	if ( !foundLineLen ) {
		// set to end of robots.txt
		m_currentLineLen = static_cast<int32_t>( ( m_robotsTxt + m_robotsTxtLen ) - m_currentLine );
	}

	// store next lineStartPos
	m_nextLineStartPos = currentPos;

	// strip ending whitespaces
	while ( m_currentLineLen > 0 && is_wspace_a( m_currentLine[m_currentLineLen - 1] ) ) {
		--m_currentLineLen;
	}

	// at this point, if lineLen is still 0, it means we should get the next line (full line comment)
	if ( m_currentLineLen == 0 ) {
		return getNextLine();
	}

	return true;
}

// assume starting whitespace have been striped away
bool Robots::getField( const char **field, int32_t *fieldLen ) {
	int32_t currentLinePos = m_valueStartPos;

	const char *colonPos = (const char*)memchr( m_currentLine + currentLinePos, ':', m_currentLineLen );

	// no colon
	if ( colonPos == NULL ) {
		return false;
	}

	currentLinePos = colonPos - m_currentLine;
	m_valueStartPos = currentLinePos + 1;

	*field = m_currentLine;
	*fieldLen = currentLinePos;

	// strip ending whitespaces
	while ( *fieldLen > 0 && is_wspace_a( m_currentLine[*fieldLen - 1] ) ) {
		--(*fieldLen);
	}

	logTrace( g_conf.m_logTraceRobots, "len=%d field='%.*s'", *fieldLen, *fieldLen, *field );

	return ( *fieldLen > 0 );
}

bool Robots::getValue( const char **value, int32_t *valueLen ) {
	// strip starting whitespaces
	while ( is_wspace_a( m_currentLine[m_valueStartPos] ) && ( m_valueStartPos < m_currentLineLen ) ) {
		++m_valueStartPos;
	}

	*value = m_currentLine + m_valueStartPos;
	*valueLen = m_currentLineLen - m_valueStartPos;

	logTrace( g_conf.m_logTraceRobots, "len=%d value='%.*s'", *valueLen, *valueLen, *value );

	return ( *valueLen > 0 );
}

bool Robots::parseUserAgent( const char *field, int32_t fieldLen, bool *isUserAgentPtr, bool *isDefaultUserAgentPtr ) {
	static const char s_userAgent[] = "user-agent";
	static const int32_t s_userAgentLen = 10;

	if ( fieldLen == s_userAgentLen && strncasecmp( field, s_userAgent, fieldLen ) == 0 ) {
		const char *value = NULL;
		int32_t valueLen = 0;

		// prefix match
		if ( getValue( &value, &valueLen ) ) {
			if ( valueLen == 1 && *value == '*' ) {
				m_defaultUserAgentFound = true;

				*isDefaultUserAgentPtr = true;
			} else if ( strncasecmp( value, m_userAgent, m_userAgentLen ) == 0 ) {
				m_userAgentFound = true;

				*isUserAgentPtr = true;
			}
		}

		return true;
	}

	return false;
}

bool Robots::parseCrawlDelay( const char *field, int32_t fieldLen, bool isUserAgent ) {
	static const char s_crawlDelay[] = "crawl-delay";
	static const int32_t s_crawlDelayLen = 11;

	if ( fieldLen == s_crawlDelayLen && strncasecmp( field, s_crawlDelay, fieldLen ) == 0 ) {
		const char *value = NULL;
		int32_t valueLen = 0;

		if ( getValue( &value, &valueLen ) ) {
			char *endPtr = NULL;
			double crawlDelay = strtod( value, &endPtr );

			if ( endPtr == ( value + valueLen ) ) {
				logTrace( g_conf.m_logTraceRobots, "isUserAgent=%s crawlDelay='%.4f'",
				          isUserAgent ? "true" : "false", crawlDelay );

				if ( isUserAgent ) {
					m_crawlDelay = static_cast<int32_t>(crawlDelay * 1000);
				} else {
					m_defaultCrawlDelay = static_cast<int32_t>(crawlDelay * 1000);
				}
			}
		}

		return true;
	}

	return false;
}

void Robots::parsePath( bool isAllow, bool isUserAgent ) {
	const char *value = NULL;
	int32_t valueLen = 0;

	if ( getValue( &value, &valueLen ) ) {
		logTrace( g_conf.m_logTraceRobots, "isAllow=%s isUserAgent=%s path='%.*s'",
		          isAllow ? "true" : "false", isUserAgent ? "true" : "false", valueLen, value );

		if ( isUserAgent ) {
			m_rules.push_back( RobotRule( isAllow, value, valueLen ));
		} else {
			m_defaultRules.push_back( RobotRule( isAllow, value, valueLen ));
		}
	}
}

bool Robots::parseAllow( const char *field, int32_t fieldLen, bool isUserAgent ) {
	static const char s_allow[] = "allow";
	static const int32_t s_allowLen = 5;

	if ( fieldLen == s_allowLen && strncasecmp( field, s_allow, fieldLen ) == 0 ) {
		parsePath( true, isUserAgent );

		return true;
	}

	return false;
}

bool Robots::parseDisallow( const char *field, int32_t fieldLen, bool isUserAgent ) {
	static const char s_disallow[] = "disallow";
	static const int32_t s_disallowLen = 8;

	if ( fieldLen == s_disallowLen && strncasecmp( field, s_disallow, fieldLen ) == 0 ) {
		parsePath( false, isUserAgent );
		return true;
	}

	return false;
}

void Robots::parse() {
	int64_t startTime = 0;
	if ( g_conf.m_logTimingRobots ) {
		startTime = gettimeofdayInMilliseconds();
	}

	bool inUserAgentGroup = false;
	bool isUserAgent = false;

	bool hasGroupRecord = false;
	while ( getNextLine() ) {
		if ( !inUserAgentGroup ) {
			// find group start (user agent)
			const char *field = NULL;
			int32_t fieldLen = 0;

			if ( getField( &field, &fieldLen ) ) {
				bool matchUserAgent = false;
				bool matchDefaultUserAgent = false;

				if ( parseUserAgent( field, fieldLen, &matchUserAgent, &matchDefaultUserAgent ) ) {
					hasGroupRecord = false;

					if ( matchUserAgent || matchDefaultUserAgent ) {
						isUserAgent = matchUserAgent;
						inUserAgentGroup = true;
					}
				}
				// add parsing of other non-group record here
			}
		} else {
			// find group end
			const char *field = NULL;
			int32_t fieldLen = 0;

			if ( getField( &field, &fieldLen ) ) {
				bool matchUserAgent = false;
				bool matchDefaultUserAgent = false;

				if ( parseUserAgent( field, fieldLen, &matchUserAgent, &matchDefaultUserAgent )) {
					if ( hasGroupRecord ) {
						hasGroupRecord = false;

						// we should only reset when we have found group records
						// this is to cater for multiple consecutive user-agent lines
						inUserAgentGroup = false;
						isUserAgent = false;
					}

					if ( matchUserAgent || matchDefaultUserAgent ) {
						isUserAgent = matchUserAgent;
						inUserAgentGroup = true;
					}
				} else if ( parseDisallow( field, fieldLen, isUserAgent ) ) {
					hasGroupRecord = true;
				} else if ( parseAllow( field, fieldLen, isUserAgent ) ) {
					hasGroupRecord = true;
				} else if ( parseCrawlDelay( field, fieldLen, isUserAgent ) ) {
					hasGroupRecord = true;
				}
				// add parsing of other group/non-group record here
			}
		}
	}

	// sort rules
	if ( m_userAgentFound ) {
		std::sort( m_rules.begin(), m_rules.end(), std::greater<RobotRule>() );
	} else if ( m_defaultUserAgentFound ) {
		std::sort( m_defaultRules.begin(), m_defaultRules.end(), std::greater<RobotRule>() );
	}

	// clear values
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;
	m_nextLineStartPos = 0;

	if ( g_conf.m_logTimingRobots ) {
		log( LOG_TIMING, "robots: %s: took %" PRId64 " ms", __func__, ( gettimeofdayInMilliseconds() - startTime ) );
	}
}

bool Robots::isAllowed( Url *url ) {
	int64_t startTime = 0;
	if ( g_conf.m_logTimingRobots ) {
		startTime = gettimeofdayInMilliseconds();
	}

	std::vector<RobotRule> *rules = NULL;

	if ( m_userAgentFound ) {
		rules = &m_rules;
	} else if ( m_defaultUserAgentFound ) {
		rules = &m_defaultRules;
	}

	// default allow
	bool isAllowed = true;

	if ( rules ) {
		for ( std::vector<RobotRule>::const_iterator it = rules->begin(); it != rules->end(); ++it ) {
			if ( it->isMatching( url ) ) {
				isAllowed = it->isAllow();
				break;
			}
		}
	}

	if ( g_conf.m_logDebugRobots ) {
		log( LOG_DEBUG, "robots: %s: isAllowed='%s' with %s user-agent for url='%.*s'",
		     __func__, isAllowed ? "true" : "false", m_userAgentFound ? "configured" : m_defaultUserAgentFound ? "default" : "no",
			url->getUrlLen(), url->getUrl() );
	}

	if ( g_conf.m_logTimingRobots ) {
		log( LOG_TIMING, "robots: %s: took %" PRId64 " ms", __func__, ( gettimeofdayInMilliseconds() - startTime ) );
	}

	return isAllowed;
}

int32_t Robots::getCrawlDelay() {
	int32_t crawlDelay = -1;

	if ( m_userAgentFound ) {
		crawlDelay = m_crawlDelay;
	} else if ( m_defaultUserAgentFound ) {
		crawlDelay = m_defaultCrawlDelay;
	}

	if ( g_conf.m_logDebugRobots ) {
		log( LOG_DEBUG, "robots: %s: crawl-delay='%d' with %s user-agent", __func__, crawlDelay, m_userAgentFound ? "configured" : m_defaultUserAgentFound ? "default" : "no" );
	}

	return crawlDelay;
}

void Robots::print() const {
	logf( LOG_DEBUG, "############ Robots ############");
	logf( LOG_DEBUG, "Robots::m_robotsTxt\n%.*s", m_robotsTxtLen, m_robotsTxt );
	logf( LOG_DEBUG, "Robots::m_userAgent='%.*s'", m_userAgentLen, m_userAgent );
	logf( LOG_DEBUG, "Robots::m_userAgentFound=%s", m_userAgentFound ? "true" : "false" );
	logf( LOG_DEBUG, "Robots::m_crawlDelay=%d", m_crawlDelay );
	logf( LOG_DEBUG, "Robots::m_defaultUserAgentFound=%s", m_defaultUserAgentFound ? "true" : "false" );
	logf( LOG_DEBUG, "Robots::m_defaultCrawlDelay=%d", m_defaultCrawlDelay );
	logf( LOG_DEBUG, "Robots::m_rules.size=%zu", m_rules.size() );
	for ( std::vector<RobotRule>::const_iterator it = m_rules.begin(); it != m_rules.end(); ++it ) {
		it->print( 1 );
	}

	logf( LOG_DEBUG, "Robots::m_defaultRules.size=%zu", m_defaultRules.size() );
	for ( std::vector<RobotRule>::const_iterator it = m_defaultRules.begin(); it != m_defaultRules.end(); ++it ) {
		it->print( 1 );
	}

	logf( LOG_DEBUG, "################################");
}
