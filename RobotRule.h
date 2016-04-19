#ifndef GB_ROBOTRULE_H
#define GB_ROBOTRULE_H

#include <stdint.h>
#include <string>

class Url;

class RobotRule {
public:
	RobotRule( bool isAllow, const char *path, int32_t pathLen );

	bool isMatching( Url *url ) const;

	bool isAllow() const {
		return m_isAllow;
	}

	int32_t getPathLen() const {
		return m_pathLen;
	}

	void print( int level = 0 ) const;

private:
	bool m_isAllow;

	const char *m_path;
	int32_t m_pathLen;
	std::string m_pathNormalized;

	bool m_wildcardFound;
	int32_t m_wildcardCount;

	bool m_lineAnchorFound;
};

inline bool operator> ( const RobotRule& lhs, const RobotRule& rhs ) {
	return ( lhs.getPathLen() > rhs.getPathLen() );
}

#endif // GB_ROBOTRULE_H
