#include "Robots.h"
#include "Url.h"
#include "Mime.h"
#include "fctypes.h"
#include "Log.h"
#include "Conf.h"

RobotRule::RobotRule( RuleType type, const char *path, int32_t pathLen )
	: m_type( type )
	, m_path( path )
	, m_pathLen( pathLen ) {
}

Robots::Robots( const char* robotsTxt, int32_t robotsTxtLen, const char *userAgent )
	: m_robotsTxt( robotsTxt )
	, m_robotsTxtLen( robotsTxtLen )
	, m_userAgent( userAgent )
	, m_userAgentLen( strlen( userAgent ) )
	, m_userAgentFound( false )
	, m_defaultUserAgentFound( false )
	, m_crawlDelay( -1 )
	, m_defaultCrawlDelay( -1 ) {
	// parse robots.txt into what we need
	parse();
}

bool Robots::getNextLine( int32_t *lineStartPos, const char **line, int32_t *lineLen ) {
	// clear values
	*line = NULL;
	*lineLen = 0;

	int32_t currentPos = *lineStartPos;

	// strip starting whitespaces
	while ( is_wspace_a( m_robotsTxt[currentPos] ) && ( currentPos < m_robotsTxtLen ) ) {
		++currentPos;
	}

	int32_t linePos = currentPos;
	*line = m_robotsTxt + currentPos;

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
						*lineLen = currentPos - linePos;
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
	if ( currentPos <= *lineStartPos ) {
		return false;
	}

	if ( !foundLineLen ) {
		// set to end of robots.txt
		*lineLen = m_robotsTxtLen - *lineStartPos;
	}

	// store next lineStartPos
	*lineStartPos = currentPos;

	// strip ending whitespaces
	while ( *lineLen > 0 && is_wspace_a( (*line)[*lineLen - 1] ) ) {
		--(*lineLen);
	}

	// at this point, if lineLen is still 0, it means we should get the next line (full line comment)
	if ( *lineLen == 0 ) {
		return getNextLine( lineStartPos, line, lineLen );
	}

	return true;
}

// assume starting whitespace have been striped away
bool Robots::getField( const char *line, int32_t lineLen, int32_t *valueStartPos, const char **field, int32_t *fieldLen ) {
	int32_t currentLinePos = *valueStartPos;

	const char *colonPos = (const char*)memchr( line + currentLinePos, ':', lineLen );

	// no colon
	if ( colonPos == NULL ) {
		return false;
	}

	currentLinePos = colonPos - line;
	*valueStartPos = currentLinePos + 1;

	*field = line;
	*fieldLen = currentLinePos;

	// strip ending whitespaces
	while ( *fieldLen > 0 && is_wspace_a( line[*fieldLen - 1] ) ) {
		--(*fieldLen);
	}

	if ( g_conf.m_logTraceRobots ) {
		log( LOG_TRACE, "Robots::%s: len=%d field='%.*s'", __func__, *fieldLen, *fieldLen, *field );
	}
	return ( *fieldLen > 0 );
}

bool Robots::getValue( const char *line, int32_t lineLen, int32_t valueStartPos, const char **value, int32_t *valueLen ) {
	// strip starting whitespaces
	while ( is_wspace_a( line[valueStartPos] ) && ( valueStartPos < lineLen ) ) {
		++valueStartPos;
	}

	*value = line + valueStartPos;
	*valueLen = lineLen - valueStartPos;

	if ( g_conf.m_logTraceRobots ) {
		logf( LOG_TRACE, "Robots::%s: len=%d value='%.*s'", __func__, *valueLen, *valueLen, *value );
	}
	return ( *valueLen > 0 );
}

bool Robots::parse() {
	static const char *s_userAgent = "user-agent";
	static const int32_t s_userAgentLen = 10;

	static const char *s_disallow = "disallow";
	static const int32_t s_disallowLen = 8;

	// non-standard extension
	static const char *s_allow = "allow";
	static const int32_t s_allowLen = 5;

	static const char *s_crawlDelay = "crawl-delay";
	static const int32_t s_crawlDelayLen = 11;

//	static const char *s_sitemap = "sitemap";
//	static const int32_t s_sitemapLen = 7;
//
//	static const char *s_host = "host";
//	static const int32_t s_hostLen = 4;
//
//  // yandex
//	static const char *s_cleanParam = "clean-param";
//	static const int32_t s_cleanParamLen = 11;

	int32_t currentPos = 0;
	const char *line = NULL;
	int32_t lineLen = 0;

	bool inUserAgentGroup = false;
	bool isUserAgent = false;
	bool hasGroupRecord = false;
	while ( getNextLine( &currentPos, &line, &lineLen ) ) {
		if ( !inUserAgentGroup ) {
			// find group start (user agent)
			const char *field = NULL;
			int32_t fieldLen = 0;
			int32_t valueStartPos = 0;

			if ( getField( line, lineLen, &valueStartPos, &field, &fieldLen ) &&
			     fieldLen == s_userAgentLen && strncasecmp( field, s_userAgent, fieldLen ) == 0 ) {
				// user-agent
				const char *value = NULL;
				int32_t valueLen = 0;

				// prefix match
				if ( getValue( line, lineLen, valueStartPos, &value, &valueLen ) ) {
					if ( valueLen == 1 && *value == '*' ) {
						hasGroupRecord = false;
						m_defaultUserAgentFound = true;
						inUserAgentGroup = true;
					} else if ( strncasecmp( value, m_userAgent, m_userAgentLen ) == 0 ) {
						hasGroupRecord = false;
						m_userAgentFound = true;
						inUserAgentGroup = true;
						isUserAgent = true;
					}
				}
			}
		} else {
			// find group end
			const char *field = NULL;
			int32_t fieldLen = 0;
			int32_t valueStartPos = 0;

			if ( getField( line, lineLen, &valueStartPos, &field, &fieldLen ) ) {
				const char *value = NULL;
				int32_t valueLen = 0;

				switch ( fieldLen ) {
					case s_allowLen:
						if ( strncasecmp( field, s_allow, fieldLen ) == 0 ) {
							hasGroupRecord = true;

							// store allow
							if ( getValue( line, lineLen, valueStartPos, &value, &valueLen ) ) {
								if ( g_conf.m_logTraceRobots ) {
									log( LOG_TRACE, "Robots::%s: isUserAgent=%s allow='%.*s'",
									     __func__, isUserAgent ? "true" : "false", valueLen, value );
								}

								if ( isUserAgent ) {
									m_rules.push_back( RobotRule( RobotRule::TYPE_ALLOW, value, valueLen ) );
								} else {
									m_defaultRules.push_back( RobotRule( RobotRule::TYPE_ALLOW, value, valueLen ) );
								}
							}
						}
						break;
					case s_disallowLen:
						if ( strncasecmp( field, s_disallow, fieldLen ) == 0 ) {
							hasGroupRecord = true;

							// store disallow
							if ( getValue( line, lineLen, valueStartPos, &value, &valueLen ) ) {
								if ( g_conf.m_logTraceRobots ) {
									log( LOG_TRACE, "Robots::%s: isUserAgent=%s disallow='%.*s'",
									     __func__, isUserAgent ? "true" : "false", valueLen, value );
								}

								if ( isUserAgent ) {
									m_rules.push_back( RobotRule( RobotRule::TYPE_DISALLOW, value, valueLen ) );
								} else {
									m_defaultRules.push_back( RobotRule( RobotRule::TYPE_DISALLOW, value, valueLen ) );
								}
							}
						}
						break;
					case s_userAgentLen:
						if ( strncasecmp( field, s_userAgent, fieldLen ) == 0 ) {
							// we should only reset when we have found group records
							// this is to cater for multiple consecutive user-agent lines
							if ( hasGroupRecord ) {
								inUserAgentGroup = false;
								isUserAgent = false;
							}

							// check if we're still in the same group
							if ( getValue( line, lineLen, valueStartPos, &value, &valueLen ) ) {
								if ( valueLen == 1 && *value == '*' ) {
									m_defaultUserAgentFound = true;
									inUserAgentGroup = true;
								} else if ( strncasecmp( value, m_userAgent, m_userAgentLen ) == 0 ) {
									m_userAgentFound = true;
									inUserAgentGroup = true;
									isUserAgent = true;
								}
							}
						}
						break;
					case s_crawlDelayLen:
						if ( strncasecmp( field, s_crawlDelay, fieldLen ) == 0 ) {
							hasGroupRecord = true;

							// store crawl delay
							if ( getValue( line, lineLen, valueStartPos, &value, &valueLen ) ) {
								char *endPtr = NULL;
								double crawlDelay = strtod( value, &endPtr );

								if ( endPtr == ( value + valueLen ) ) {
									if (g_conf.m_logTraceRobots) {
										log( LOG_TRACE, "Robots::%s: isUserAgent=%s crawlDelay='%.4f'",
										     __func__, isUserAgent ? "true" : "false", crawlDelay );
									}

									if (isUserAgent) {
										m_crawlDelay = static_cast<int32_t>(crawlDelay * 1000);
									} else {
										m_defaultCrawlDelay = static_cast<int32_t>(crawlDelay * 1000);
									}
								}
							}
						}
						break;
					default:
						break;
				}
			}
		}
	}

	return m_userAgentFound;
}

int32_t Robots::getCrawlDelay() {
	if (m_userAgentFound) {
		return m_crawlDelay;
	} else if (m_defaultUserAgentFound) {
		return m_defaultCrawlDelay;
	} else {
		return -1;
	}
}

bool Robots::isUserAgentFound() {
	return m_userAgentFound;
}

bool Robots::isDefaultUserAgentFound() {
	return m_defaultUserAgentFound;
}

bool Robots::isRulesEmpty() {
	return m_rules.empty();
}

bool Robots::isDefaultRulesEmpty() {
	return m_defaultRules.empty();
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
