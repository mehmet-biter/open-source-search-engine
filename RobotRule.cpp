#include "RobotRule.h"
#include "Url.h"
#include "Log.h"

RobotRule::RobotRule( bool isAllow, const char *path, int32_t pathLen )
		: m_isAllow( isAllow )
		, m_path( path )
		, m_pathLen( pathLen )
		, m_wildcardFound( false )
		, m_lineAnchorFound( false ) {
	m_wildcardFound = ( memchr( path, '*', pathLen ) != NULL );
	m_lineAnchorFound = ( path[ pathLen - 1] == '$' );
}

bool RobotRule::isMatching( Url *url ) const {
	if ( m_wildcardFound ) {
		if ( m_lineAnchorFound ) {
			logf(LOG_INFO, "%s: @@@todo wildcard + anchor", __func__);
		} else {
			logf(LOG_INFO, "%s: @@@todo wildcard", __func__);
		}
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

	logf( LOG_DEBUG, "%*s RobotRule: type=%s wildcardFound=%d lineAnchorFound=%d path=%.*s", level, " ",
	      m_isAllow ? "allow" : "disallow", m_wildcardFound, m_lineAnchorFound, m_pathLen, m_path );
}