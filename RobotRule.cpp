#include "RobotRule.h"
#include "Url.h"
#include "Log.h"
#include <algorithm>

RobotRule::RobotRule( bool isAllow, const char *path, int32_t pathLen )
	: m_isAllow( isAllow )
	, m_path( path )
	, m_pathLen( pathLen )
	, m_wildcardFound( false )
	, m_wildcardCount( 0 )
	, m_lineAnchorFound( ( m_path[ m_pathLen - 1] == '$' ) ) {
	if ( !m_lineAnchorFound ) {
		// strip ending asterisk
		while ( m_pathLen > 0 && m_path[m_pathLen - 1] == '*' ) {
			--m_pathLen;
		}
	}

	const char *asteriskPos = static_cast<const char*>( memchr( m_path, '*', m_pathLen ) );
	if ( asteriskPos != NULL ) {
		m_wildcardFound = true;
		m_wildcardCount = std::count( asteriskPos, m_path + m_pathLen, '*');
	}
}

static bool matchWildcard( const char *haystack, int32_t haystackLen, const char *needle, int32_t needleLen, bool fullMatch ) {
//	logf( LOG_INFO, "%s: ==========================================================================", __func__ );
//	logf( LOG_DEBUG, "haystack='%.*s' haystackLen=%d needle='%.*s' needleLen=%d fullMatch=%d",
//	      haystackLen, haystack, haystackLen, needleLen, needle, needleLen, fullMatch );

	bool isInWildcard = false;
	int32_t haystackPos = 0;
	int32_t savedHaystackPos = 0;
	int32_t needlePos = 0;
	int32_t savedNeedlePos = 0;

	while ( haystackPos < haystackLen && needlePos < needleLen ) {
//		logf( LOG_DEBUG, "while start haystack[%d]=%c needle[%d]=%c savedHaystackPos=%d savedNeedlePos=%d",
//		      haystackPos, haystack[haystackPos], needlePos, needle[needlePos], savedHaystackPos, savedNeedlePos);

		if ( needle[needlePos] != '*' ) {
//			logf( LOG_DEBUG, "not asterisk");
			if ( isInWildcard ) {
//				logf( LOG_DEBUG, "not asterisk: is in wildcard");
				// fast forward
				while ( haystackPos < haystackLen && needle[needlePos] != haystack[haystackPos] ) {
//					logf( LOG_DEBUG, "not asterisk: is in wildcard: fast-forward haystackPos=%d", haystackPos);
					++haystackPos;
				}

				if ( haystackPos == haystackLen ) {
//					logf( LOG_DEBUG, "not asterisk: is in wildcard return false");
					return false;
				}

				isInWildcard = false;
				continue;
			} else {
//				logf( LOG_DEBUG, "not asterisk: not in wildcard");
				if ( needle[needlePos++] == haystack[haystackPos++] ) {
//					logf( LOG_DEBUG, "not asterisk: not in wildcard: equals");
					continue;
				}
//				logf( LOG_DEBUG, "not asterisk: not in wildcard: not equals");
			}

			if ( savedHaystackPos || savedNeedlePos ) {
//				logf( LOG_DEBUG, "not asterisk: saved haystack/needle");
				isInWildcard = true;
				haystackPos = ++savedHaystackPos;
				needlePos = savedNeedlePos;

				continue;
			}

//			logf( LOG_DEBUG, "not asterisk: is in wildcard end");
			return false;
		} else {
//			logf( LOG_DEBUG, "asterisk");
			isInWildcard = true;

			// ignore multiple asterisk
			while ( needle[needlePos] == '*' && ( needlePos < needleLen ) ) {
				++needlePos;
			}
			savedHaystackPos = haystackPos;
			savedNeedlePos = needlePos;
		}
//		logf( LOG_DEBUG, "while end");
	}

//	logf( LOG_DEBUG, "while outside haystackLen=%d haystackPos=%d needleLen=%d needlePos=%d",
//	      haystackLen, haystackPos, needleLen, needlePos );

	if ( fullMatch ) {
		return ( haystackPos == haystackLen && ( needlePos + 1 ) == needleLen );
	}

	return ( needlePos == needleLen );
}

bool RobotRule::isMatching( Url *url ) const {
	if ( m_wildcardFound ) {
		return matchWildcard( url->getPath(), url->getPathLenWithCgi(), m_path, m_pathLen, m_lineAnchorFound );
	} else {
		if ( m_lineAnchorFound ) {
			// full match
			if ( url->getPathLenWithCgi() == ( m_pathLen - 1 ) &&
			     memcmp( url->getPath(), m_path, ( m_pathLen - 1 ) ) == 0 ) {
				return true;
			}
		} else {
			// simple prefix match
			if ( url->getPathLenWithCgi() >= m_pathLen &&
			     memcmp( url->getPath(), m_path, m_pathLen ) == 0 ) {
				return true;
			}
		}
	}

	return false;
}

void RobotRule::print( int level ) const {
	// 2 space indentation per level
	level *= 2;

	logf( LOG_DEBUG, "%*s RobotRule: type=%s wildcardFound=%d wildcardCount=%d lineAnchorFound=%d path=%.*s", level, "",
	      m_isAllow ? "allow" : "disallow", m_wildcardFound, m_wildcardCount, m_lineAnchorFound, m_pathLen, m_path );
}