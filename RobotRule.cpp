#include "RobotRule.h"
#include "Url.h"
#include "UrlComponent.h"
#include "Log.h"
#include <algorithm>

RobotRule::RobotRule( bool isAllow, const char *path, int32_t pathLen )
	: m_isAllow( isAllow )
	, m_path( path )
	, m_pathLen( pathLen )
	, m_pathNormalized()
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

	const char *percentPos = static_cast<const char*>( memchr( m_path, '%', m_pathLen ) );
	if ( percentPos != NULL ) {
		m_pathNormalized = std::string( m_path, m_pathLen );
		UrlComponent::normalize( &m_pathNormalized );
	}
}

static bool matchWildcard( const char *haystack, int32_t haystackLen, const char *needle, int32_t needleLen, bool fullMatch ) {
	bool isInWildcard = false;
	int32_t haystackPos = 0;
	int32_t savedHaystackPos = 0;
	int32_t needlePos = 0;
	int32_t savedNeedlePos = 0;

	while ( haystackPos < haystackLen && needlePos < needleLen ) {
		if ( needle[needlePos] != '*' ) {
			if ( isInWildcard ) {
				// fast forward
				while ( haystackPos < haystackLen && needle[needlePos] != haystack[haystackPos] ) {
					++haystackPos;
				}

				if ( haystackPos == haystackLen ) {
					return ( fullMatch && ( needlePos + 1 ) == needleLen );
				}

				isInWildcard = false;
				continue;
			} else {
				if ( needle[needlePos++] == haystack[haystackPos++] ) {
					continue;
				}
			}

			if ( savedHaystackPos || savedNeedlePos ) {
				isInWildcard = true;
				haystackPos = ++savedHaystackPos;
				needlePos = savedNeedlePos;

				continue;
			}

			return false;
		} else {
			isInWildcard = true;

			// ignore multiple asterisk
			while ((needlePos < needleLen) && needle[needlePos] == '*') {
				++needlePos;
			}
			savedHaystackPos = haystackPos;
			savedNeedlePos = needlePos;
		}
	}

	if ( fullMatch ) {
		return ( haystackPos == haystackLen && ( needlePos + 1 ) == needleLen );
	}

	return ( needlePos == needleLen );
}

bool RobotRule::isMatching( Url *url ) const {
	const char *path = m_pathNormalized.empty() ? m_path : m_pathNormalized.c_str();
	int32_t pathLen = m_pathNormalized.empty() ? m_pathLen : m_pathNormalized.size();

	if ( m_wildcardFound ) {
		return matchWildcard( url->getPath(), url->getPathLenWithCgi(), path, pathLen, m_lineAnchorFound );
	} else {
		if ( m_lineAnchorFound ) {
			// full match
			if ( url->getPathLenWithCgi() == ( pathLen - 1 ) &&
			     memcmp( url->getPath(), path, ( pathLen - 1 ) ) == 0 ) {
				return true;
			}
		} else {
			// simple prefix match
			if ( url->getPathLenWithCgi() >= pathLen &&
			     memcmp( url->getPath(), path, pathLen ) == 0 ) {
				return true;
			}
		}
	}

	return false;
}

void RobotRule::print( int level ) const {
	// 2 space indentation per level
	level *= 2;

	const char *path = m_pathNormalized.empty() ? m_path : m_pathNormalized.c_str();
	int32_t pathLen = m_pathNormalized.empty() ? m_pathLen : m_pathNormalized.size();

	logf( LOG_DEBUG, "%*s RobotRule: type=%s wildcardFound=%d wildcardCount=%d lineAnchorFound=%d path=%.*s", level, "",
	      m_isAllow ? "allow" : "disallow", m_wildcardFound, m_wildcardCount, m_lineAnchorFound, pathLen, path );
}

