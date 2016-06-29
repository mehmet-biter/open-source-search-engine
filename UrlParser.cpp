#include "UrlParser.h"
#include "Log.h"
#include "fctypes.h"
#include "Domains.h"
#include "ip.h"
#include <string.h>
#include <iterator>

static const char* strnpbrk( const char *str1, size_t len, const char *str2 ) {
	const char *haystack = str1;
	const char *haystackEnd = str1 + len;

	while ( haystack < haystackEnd && *haystack ) {
		const char *needle = str2;
		while ( *needle ) {
			if ( *haystack == *needle ) {
				return haystack;
			}
			++needle;
		}
		++haystack;
	}

	return NULL;
}

/// @todo ALC we should see if we need to do relative path resolution here
/// @todo ALC we should cater for scheme relative address ( pass in parent scheme )
/// https://tools.ietf.org/html/rfc3986#section-5.2
UrlParser::UrlParser( const char *url, size_t urlLen )
	: m_url( url )
	, m_urlLen( urlLen )
	, m_scheme( NULL )
	, m_schemeLen( 0 )
	, m_authority( NULL )
	, m_authorityLen( 0 )
	, m_domain( NULL )
	, m_domainLen( 0 )
	, m_paths()
	, m_pathEndChar('\0')
	, m_pathsDeleteCount( 0 )
	, m_queries()
	, m_queriesMap()
	, m_queriesDeleteCount( 0 )
	, m_urlParsed() {
	m_urlParsed.reserve( m_urlLen );
	parse();
}

void UrlParser::print() const {
	logf( LOG_DEBUG, "UrlParser::url       : %.*s", static_cast<uint32_t>( m_urlLen ), m_url );
	logf( LOG_DEBUG, "UrlParser::scheme    : %.*s", static_cast<uint32_t>( m_schemeLen ), m_scheme );
	logf( LOG_DEBUG, "UrlParser::authority : %.*s", static_cast<uint32_t>( m_authorityLen ), m_authority );
	logf( LOG_DEBUG, "UrlParser::host      : %.*s", static_cast<uint32_t>( m_hostLen ), m_host );
	logf( LOG_DEBUG, "UrlParser::domain    : %.*s", static_cast<uint32_t>( m_domainLen ), m_domain );

	for ( auto it = m_paths.begin(); it != m_paths.end(); ++it ) {
		logf( LOG_DEBUG, "UrlParser::path[%02zi]  : %s", std::distance( m_paths.begin(), it ), it->getString().c_str() );
	}

	for ( auto it = m_queries.begin(); it != m_queries.end(); ++it ) {
		logf( LOG_DEBUG, "UrlParser::query[%02zi] : %s", std::distance( m_queries.begin(), it ), it->getString().c_str() );
	}
}

void UrlParser::parse() {
	// URI = scheme ":" hier-part [ "?" query ] [ "#" fragment ]

	const char *urlEnd = m_url + m_urlLen;
	const char *currentPos = m_url;

	// hier-part   = "//" authority path-abempty
	//             / path-absolute
	//             / path-rootless
	//             / path-empty

	const char *authorityPos = static_cast<const char*>( memmem( currentPos, urlEnd - currentPos, "//", 2 ) );
	if ( authorityPos != NULL ) {
		if ( authorityPos != currentPos ) {
			m_scheme = currentPos;
			m_schemeLen = authorityPos - currentPos - 1;
		}

		m_authority = authorityPos + 2;
		currentPos = m_authority;
	} else {
		m_authority = currentPos;
	}

	const char *pathPos = static_cast<const char*>( memchr( currentPos, '/', urlEnd - currentPos ) );
	if ( pathPos != NULL ) {
		m_authorityLen = pathPos - m_authority;
		currentPos = pathPos + 1;
	} else {
		m_authorityLen = urlEnd - m_authority;

		// nothing else to process
		return;
	}

	// @todo similar logic in Url.cpp ( merge this )

	// authority   = [ userinfo "@" ] host [ ":" port ]
	const char *userInfoPos = static_cast<const char *>( memchr( m_authority, '@', m_authorityLen ) );
	if ( userInfoPos != NULL ) {
		m_host = userInfoPos + 1;
		m_hostLen = m_authorityLen - ( userInfoPos - m_authority ) - 1;
	} else {
		m_host = m_authority;
		m_hostLen = m_authorityLen;
	}

	const char *portPos = static_cast<const char *>( memrchr( m_host, ':', m_hostLen ) );
	if ( portPos != NULL ) {
		m_hostLen -= ( m_hostLen - ( portPos - m_host ) );
	}

	// host        = IP-literal / IPv4address / reg-name

	/// @todo ALC we should remove the const cast once we fix all the const issue
	int32_t ip = atoip( m_host, m_hostLen );
	if ( ip ) {
		int32_t domainLen = 0;
		m_domain = getDomainOfIp ( const_cast<char *>( m_host ), m_hostLen , &domainLen );
		m_domainLen = domainLen;
	} else {
		const char *tldPos = ::getTLD( const_cast<char *>( m_host ), m_hostLen );
		if ( tldPos ) {
			size_t tldLen = m_host + m_hostLen - tldPos;
			if ( tldLen < m_hostLen ) {
				m_domain = static_cast<const char *>( memrchr( m_host, '.', m_hostLen - tldLen - 1 ) );
				if ( m_domain ) {
					m_domain += 1;
					m_domainLen = m_hostLen - ( m_domain - m_host );
				} else {
					m_domain = m_host;
					m_domainLen = m_hostLen;
				}
			}
		}
	}

	const char *queryPos = static_cast<const char*>( memchr( currentPos, '?', urlEnd - currentPos ) );
	if ( queryPos != NULL ) {
		currentPos = queryPos + 1;
	}

	const char *anchorPos = static_cast<const char*>( memrchr( currentPos, '#', urlEnd - currentPos ) );
//	if ( anchorPos != NULL ) {
//		currentPos = anchorPos + 1;
//	}

	const char *pathEnd = queryPos ?: anchorPos ?: urlEnd;
	m_pathEndChar = *( pathEnd - 1 );

	const char *queryEnd = anchorPos ?: urlEnd;

	// path
	const char *prevPos = pathPos + 1;
	while ( prevPos && ( prevPos <= pathEnd ) ) {
		size_t len = pathEnd - prevPos;
		currentPos = strnpbrk( prevPos, len, "/;&" );
		if ( currentPos ) {
			len = currentPos - prevPos;
		}

		UrlComponent urlPart = UrlComponent( UrlComponent::TYPE_PATH, prevPos, len, *( prevPos - 1 ) );

		m_paths.push_back( urlPart );

		prevPos = currentPos ? currentPos + 1 : NULL;
	}

	// query
	if ( queryPos ) {
		prevPos = queryPos + 1;

		bool isPrevAmpersand = false;
		while ( prevPos && ( prevPos < queryEnd ) ) {
			size_t len = queryEnd - prevPos;
			currentPos = strnpbrk( prevPos, len, "&;" );
			if ( currentPos ) {
				len = currentPos - prevPos;
			}

			UrlComponent urlPart = UrlComponent( UrlComponent::TYPE_QUERY, prevPos, len, *( prevPos - 1 ) );
			std::string key = urlPart.getKey();

			// check previous urlPart
			if ( isPrevAmpersand ) {
				urlPart.setSeparator( '&' );
			}

			bool isAmpersand = ( !urlPart.hasValue() && urlPart.getKey() == "amp" );
			if ( !key.empty() && !isAmpersand ) {
				// we don't cater for case sensitive query parameter (eg: parm, Parm, PARM is assumed to be the same)
				auto it = m_queriesMap.find( key );
				if (it == m_queriesMap.end()) {
					m_queries.push_back( urlPart );
					m_queriesMap[key] = m_queries.size() - 1;
				} else {
					m_queries[it->second] = urlPart;
				}
			}

			prevPos = currentPos ? currentPos + 1 : NULL;
			isPrevAmpersand = isAmpersand;
		}
	}
}

const char* UrlParser::unparse() {
	m_urlParsed.clear();

	// domain
	m_urlParsed.append( m_url, ( m_authority - m_url ) + m_authorityLen );

	bool isFirst = true;
	for ( auto it = m_paths.begin(); it != m_paths.end(); ++it ) {
		if ( !it->isDeleted() ) {
			if ( isFirst ) {
				isFirst = false;
				if ( it->getSeparator() != '/' ) {
					m_urlParsed.append( "/" );
				}
			}

			m_urlParsed += it->getSeparator();
			m_urlParsed.append( it->getString() );
		}
	}

	if ( m_urlParsed[ m_urlParsed.size() - 1 ] != '/' && m_pathEndChar == '/' ) {
		m_urlParsed += m_pathEndChar;
	}

	isFirst = true;
	for ( auto it = m_queries.begin(); it != m_queries.end(); ++it ) {
		if ( !it->isDeleted() ) {
			if ( isFirst ) {
				isFirst = false;
				m_urlParsed.append( "?" );
			} else {
				m_urlParsed += ( it->getSeparator() == '?' ) ? '&' : it->getSeparator();
			}

			m_urlParsed.append( it->getString() );
		}
	}

	return m_urlParsed.c_str();
}

void UrlParser::deleteComponent( UrlComponent *urlComponent ) {
	if ( urlComponent ) {
		urlComponent->setDeleted();

		switch ( urlComponent->getType() ) {
			case UrlComponent::TYPE_PATH:
				++m_pathsDeleteCount;
				break;
			case UrlComponent::TYPE_QUERY:
				++m_queriesDeleteCount;

				// also remove from map
				m_queriesMap.erase( urlComponent->getKey() );
				break;
		}
	}
}

bool UrlParser::removeComponent( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator ) {
	bool hasRemoval = false;

	for ( auto it = urlComponents.begin(); it != urlComponents.end(); ++it ) {
		if ( (*it)->isDeleted() ) {
			continue;
		}

		if ( ( (*it)->hasValue() && validator.isValid( *(*it) ) ) ||
		     ( !(*it)->hasValue() && validator.allowEmptyValue() ) ) {
			hasRemoval = true;
			deleteComponent( *it );
		}
	}

	return hasRemoval;
}

std::vector<std::pair<UrlComponent*, UrlComponent*> > UrlParser::matchPath( const UrlComponent::Matcher &matcher ) {
	std::vector<std::pair<UrlComponent*, UrlComponent*> > result;

	// don't need to loop if it's all deleted
	if ( m_pathsDeleteCount == m_paths.size() ) {
		return result;
	}

	for ( auto it = m_paths.begin(); it != m_paths.end(); ++it ) {
		if ( it->isDeleted() ) {
			continue;
		}

		if ( !it->hasValue() && matcher.isMatching( *it ) ) {
			auto valueIt = std::next( it, 1 );
			result.push_back( std::make_pair( &( *it ), ( valueIt != m_paths.end() ? &( *valueIt ) : NULL ) ) );
		}
	}

	return result;
}

bool UrlParser::removePath( const std::vector<std::pair<UrlComponent*, UrlComponent*> > &urlComponents,
                            const UrlComponent::Validator &validator ) {
	bool hasRemoval = false;
	for ( auto it = urlComponents.begin(); it != urlComponents.end(); ++it ) {
		if ( it->second == NULL ) {
			if ( validator.allowEmptyValue() ) {
				hasRemoval = true;
				deleteComponent( it->first );
			}
		} else {
			if ( validator.isValid( *( it->second ) ) ) {
				hasRemoval = true;
				deleteComponent( it->first );
				deleteComponent( it->second );
			}
		}
	}

	return hasRemoval;
}

bool UrlParser::removePath( const UrlComponent::Matcher &matcher, const UrlComponent::Validator &validator ) {
	std::vector<std::pair<UrlComponent*, UrlComponent*> > matches = matchPath( matcher );

	return removePath( matches, validator );
}

std::vector<UrlComponent*> UrlParser::matchPathParam( const UrlComponent::Matcher &matcher ) {
	std::vector<UrlComponent*> result;

	// don't need to loop if it's all deleted
	if ( m_pathsDeleteCount == m_paths.size() ) {
		return result;
	}

	for ( auto it = m_paths.begin(); it != m_paths.end(); ++it ) {
		if ( it->isDeleted() ) {
			continue;
		}

		if ( it->hasValue() && matcher.isMatching( *it ) ) {
			result.push_back( &( *it ) );
		}
	}

	return result;
}

bool UrlParser::removePathParam( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator ) {
	return removeComponent( urlComponents, validator );
}

bool UrlParser::removePathParam( const UrlComponent::Matcher &matcher, const UrlComponent::Validator &validator ) {
	std::vector<UrlComponent*> matches = matchPathParam( matcher );

	return removeComponent( matches, validator );
}

std::vector<UrlComponent*> UrlParser::matchQueryParam( const UrlComponent::Matcher &matcher ) {
	std::vector<UrlComponent*> result;

	// don't need to loop if it's all deleted
	if ( m_queriesDeleteCount == m_queries.size() ) {
		return result;
	}

	if ( matcher.getMatchCriteria() == MATCH_DEFAULT ) {
		auto it = m_queriesMap.find( matcher.getParam() );
		if ( it != m_queriesMap.end() ) {
			result.push_back( &(m_queries[ it->second ]) );
		}
	} else {
		for ( auto it = m_queries.begin(); it != m_queries.end(); ++it ) {
			if ( it->isDeleted() ) {
				continue;
			}

			if ( matcher.isMatching( *it ) ) {
				result.push_back( &(*it));
			}
		}
	}

	return result;
}

bool UrlParser::removeQueryParam( const char *param ) {
	static const UrlComponent::Validator s_validator( 0, 0, true, ALLOW_ALL, MANDATORY_NONE );

	return removeQueryParam( UrlComponent::Matcher( param ), s_validator );
}

bool UrlParser::removeQueryParam( const std::vector<UrlComponent*> &urlComponents, const UrlComponent::Validator &validator ) {
	return removeComponent( urlComponents, validator );
}

bool UrlParser::removeQueryParam( const UrlComponent::Matcher &matcher, const UrlComponent::Validator &validator ) {
	return removeComponent( matchQueryParam( matcher ), validator );
}
