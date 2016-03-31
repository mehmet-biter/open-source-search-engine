#include "Robots.h"
#include "Url.h"
#include "Mime.h"
#include "fctypes.h"
#include "Log.h"
#include "Conf.h"
#include <functional>

Robots::Robots( const char* robotsTxt, int32_t robotsTxtLen, const char *userAgent )
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
	// parse robots.txt into what we need
	parse();
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
		m_currentLineLen = m_robotsTxtLen - m_nextLineStartPos;
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

	if ( g_conf.m_logTraceRobots ) {
		log( LOG_TRACE, "Robots::%s: len=%d field='%.*s'", __func__, *fieldLen, *fieldLen, *field );
	}
	return ( *fieldLen > 0 );
}

bool Robots::getValue( const char **value, int32_t *valueLen ) {
	// strip starting whitespaces
	while ( is_wspace_a( m_currentLine[m_valueStartPos] ) && ( m_valueStartPos < m_currentLineLen ) ) {
		++m_valueStartPos;
	}

	*value = m_currentLine + m_valueStartPos;
	*valueLen = m_currentLineLen - m_valueStartPos;

	if ( g_conf.m_logTraceRobots ) {
		logf( LOG_TRACE, "Robots::%s: len=%d value='%.*s'", __func__, *valueLen, *valueLen, *value );
	}
	return ( *valueLen > 0 );
}

bool Robots::parseUserAgent( const char *field, int32_t fieldLen, bool *isUserAgentPtr, bool *isDefaultUserAgentPtr ) {
	static const char *s_userAgent = "user-agent";
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
	static const char *s_crawlDelay = "crawl-delay";
	static const int32_t s_crawlDelayLen = 11;

	if ( fieldLen == s_crawlDelayLen && strncasecmp( field, s_crawlDelay, fieldLen ) == 0 ) {
		const char *value = NULL;
		int32_t valueLen = 0;

		if ( getValue( &value, &valueLen ) ) {
			char *endPtr = NULL;
			double crawlDelay = strtod( value, &endPtr );

			if ( endPtr == ( value + valueLen ) ) {
				if (g_conf.m_logTraceRobots) {
					log( LOG_TRACE, "Robots::%s: isUserAgent=%s crawlDelay='%.4f'",
					     __func__, isUserAgent ? "true" : "false", crawlDelay );
				}

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
		if ( g_conf.m_logTraceRobots ) {
			log( LOG_TRACE, "Robots::%s: isAllow=%s isUserAgent=%s path='%.*s'",
			     __func__, isAllow ? "true" : "false", isUserAgent ? "true" : "false", valueLen, value );
		}

		if ( isUserAgent ) {
			m_rules.push_back( RobotRule( isAllow, value, valueLen ));
		} else {
			m_defaultRules.push_back( RobotRule( isAllow, value, valueLen ));
		}
	}
}

bool Robots::parseAllow( const char *field, int32_t fieldLen, bool isUserAgent ) {
	static const char *s_allow = "allow";
	static const int32_t s_allowLen = 5;

	if ( fieldLen == s_allowLen && strncasecmp( field, s_allow, fieldLen ) == 0 ) {
		parsePath( true, isUserAgent );

		return true;
	}

	return false;
}

bool Robots::parseDisallow( const char *field, int32_t fieldLen, bool isUserAgent ) {
	static const char *s_disallow = "disallow";
	static const int32_t s_disallowLen = 8;

	if ( fieldLen == s_disallowLen && strncasecmp( field, s_disallow, fieldLen ) == 0 ) {
		parsePath( false, isUserAgent );
		return true;
	}

	return false;
}

void Robots::parse() {
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
		m_rules.sort( std::greater<RobotRule>() );
	} else if ( m_defaultUserAgentFound ) {
		m_defaultRules.sort( std::greater<RobotRule>() );
	}

	// clear values
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;
	m_nextLineStartPos = 0;
}

bool Robots::isAllowed( Url *url ) {
	std::list<RobotRule> *rules = NULL;

	if ( !m_rules.empty() ) {
		rules = &m_rules;
	} else if ( !m_defaultRules.empty() ) {
		rules = &m_defaultRules;
	}

	if ( rules ) {
		for ( std::list<RobotRule>::const_iterator it = rules->begin(); it != rules->end(); ++it ) {
			if ( it->isMatching( url ) ) {
				return it->isAllow();
			}
		}
	}

	// default allow
	return true;
}

int32_t Robots::getCrawlDelay() {
	if ( m_userAgentFound ) {
		return m_crawlDelay;
	} else if ( m_defaultUserAgentFound ) {
		return m_defaultCrawlDelay;
	} else {
		return -1;
	}
}

void Robots::print() const {
	logf( LOG_DEBUG, "############ Robots ############");
	logf( LOG_DEBUG, "Robots::m_robotsTxt\n%.*s", m_robotsTxtLen, m_robotsTxt );
	logf( LOG_DEBUG, "Robots::m_userAgent='%.*s'", m_userAgentLen, m_userAgent );
	logf( LOG_DEBUG, "Robots::m_userAgentFound=%s", m_userAgentFound ? "true" : "false" );
	logf( LOG_DEBUG, "Robots::m_crawlDelay=%d", m_crawlDelay );
	logf( LOG_DEBUG, "Robots::m_defaultUserAgentFound=%s", m_defaultUserAgentFound ? "true" : "false" );
	logf( LOG_DEBUG, "Robots::m_defaultCrawlDelay=%d", m_defaultCrawlDelay );
	logf( LOG_DEBUG, "Robots::m_rules.size=%lu", m_rules.size() );
	for ( std::list<RobotRule>::const_iterator it = m_rules.begin(); it != m_rules.end(); ++it ) {
		it->print( 1 );
	}

	logf( LOG_DEBUG, "Robots::m_defaultRules.size=%lu", m_defaultRules.size() );
	for ( std::list<RobotRule>::const_iterator it = m_defaultRules.begin(); it != m_defaultRules.end(); ++it ) {
		it->print( 1 );
	}

	logf( LOG_DEBUG, "################################");
}

bool Robots::isAllowed ( Url *url, const char *userAgent, const char *file, int32_t fileLen, bool *userAgentFound,
	                     bool substringMatch, int32_t *crawlDelay, bool  *hadAllowOrDisallow ) {
	// assume user agent is not in the file
	*userAgentFound = false;
	*hadAllowOrDisallow = false;

	// assume no crawl delay (-1)
	// *crawlDelay = -1;

	// if fileLen is 0 it is allowed
	if ( fileLen <= 0 ) {
		return true;
	}

	// get path from url, include cgi stuff
	char *path = url->getPath();
	int32_t pathLen = url->getPathLenWithCgi();

	// set the Mime class to this Mime file
	Mime mime;
	mime.set ( file , fileLen );

	// get a line of Mime
	const char *f = NULL;
	int32_t flen = 0;
	const char *v = NULL;
	int32_t vlen = 0;

	// user agent length
	int32_t uaLen = strlen (userAgent);

	// ptr into "file"
	const char *p = file;
	char flag;
	bool allowed = true;

 loop:
	// if p is NULL now we're done
	if ( ! p ) {
		return allowed;
	}

	// get the next Mime line
	p = mime.getLine ( p , &f , &flen , &v , &vlen );

	// if this field is NOT "user-agent" skip it
	if ( flen != 10 || strncasecmp ( f , "user-agent" , 10 ) != 0 ) {
		goto loop;
	}

 gotAgent:
	//some webmasters put comments at the end of their lines,
	//because they think this is a shell script or something.
	const char* vv = v;
	while ( vv - v < vlen && *vv != '#' ) {
		vv++;
	}

	vlen = vv - v;

	// decrement vlen to hack off spaces after the user-agent so that vlen
	// is really the length of the user agent
	while ( vlen > 0 && is_wspace_a(v[vlen-1]) ) {
		vlen--;
	}

	// now match the user agent
	if ( ! substringMatch && vlen != uaLen ) {
		goto loop;
	}

	// otherwise take the min of the lengths
	if ( uaLen < vlen ) {
		vlen = uaLen;
	}

	// is it the right user-agent?
	if ( strncasecmp ( v , userAgent , vlen ) != 0 ) {
		goto loop;
	}

	*userAgentFound = true;
	flag = 0;

 urlLoop:
	// if p is NULL now there is no more lines
	if ( ! p ) {
		return allowed;
	}

	// now loop over lines until we hit another user-agent line
	p = mime.getLine ( p , &f , &flen , &v , &vlen );

	// if it's another user-agent line ... ignore it unless we already
	// have seen another line (not user-agent), in which case we got another set of group
	if ( flag && flen==10 && strncasecmp( f, "user-agent", 10 ) == 0 ) {
		goto gotAgent;
	}

	// if a crawl delay, get the delay
	if ( flen == 11 && strncasecmp ( f, "crawl-delay", 11 ) == 0 ) {
		// set flag
		flag = 1;

		// skip if invalid. it could be ".5" seconds
		if ( ! is_digit ( *v ) && *v != '.' ) {
			goto urlLoop;
		}

		// get this. multiply crawl delay by x1000 to be in milliseconds/ms
		int64_t vv = (int64_t)(atof(v) * 1000LL);

		// truncate to 0x7fffffff
		if ( vv > 0x7fffffff ) {
			*crawlDelay = 0x7fffffff;
		} else if ( vv < 0 ) {
			*crawlDelay = -1;
		} else {
			*crawlDelay = (int32_t)vv;
		}

		goto urlLoop;
	}

	// if already disallowed, just goto the next line
	if ( !allowed ) {
		goto urlLoop;
	}

	// if we have an allow line or sitemap: line, then set flag to 1
	// so we can go to another user-agent line.
	// fixes romwebermarketplace.com/robots.txt
	// (doc.156447320458030317.txt)
	if ( flen == 5 && strncasecmp( f, "allow", 5 ) == 0 ) {
		*hadAllowOrDisallow = true;
		flag = 1;
	}

	if ( flen == 7 && strncasecmp( f, "sitemap", 7 ) == 0 ) {
		flag = 1;
	}

	// if not disallow go to loop at top
	if ( flen != 8 || strncasecmp ( f , "disallow" , 8 ) != 0 ) {
		goto urlLoop;
	}

	// we had a disallow
	*hadAllowOrDisallow = true;

	// set flag
	flag = 1;

	// now stop at first space after url or end of line
	const char *s    = v;
	const char *send = v + vlen;

	// skip all non-space chars
	while ( s < send && ! is_wspace_a(*s) ) {
		s++;
	}

	// stop there
	vlen = s - v;

	// check for match
	char *tmpPath = path;
	int32_t tmpPathLen = pathLen;

	// assume path begins with /
	if ( vlen > 0 && v[0] != '/'){
		tmpPath++;
		tmpPathLen--;
	}

	if ( vlen > tmpPathLen ) {
		goto urlLoop;
	}

	if ( strncasecmp( tmpPath, v, vlen ) != 0 ) {
		goto urlLoop;
	}

	// an exact match
	if ( vlen == tmpPathLen ) {
		allowed = false;
		goto urlLoop;
	}

	// must be something
	if ( vlen <= 0 ) {
		goto urlLoop;
	}

	// "v" may or may not end in a /, it really should end in a / though
	if ( v[vlen-1] == '/' && tmpPath[vlen-1] == '/' ) {
		allowed = false;
		goto urlLoop;
	}

	if ( v[vlen-1] != '/' && tmpPath[vlen  ] == '/' ) {
		allowed = false;
		goto urlLoop;
	}

	// let's be stronger. just do the substring match. if the webmaster
	// does not want us splitting path or file names then they should end
	// all of their robots.txt entries in a '/'. this also fixes the
	// problem of the "Disallow: index.htm?" line.
	allowed = false;

	// get another url path
	goto urlLoop;
}
