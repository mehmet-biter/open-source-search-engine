#include "UrlComponent.h"
#include "fctypes.h"
#include "Log.h"
#include <algorithm>

void UrlComponent::normalize( std::string *component ) {
	// normalize string
	size_t percentPos = 0;
	while ( ( percentPos = component->find( '%', percentPos ) ) != std::string::npos ) {
		if ( percentPos + 2 < component->size() ) {
			std::string encoded = component->substr( percentPos + 1, 2);
			char *endPtr = NULL;
			uint8_t value = static_cast<uint8_t>( strtol( encoded.c_str(), &endPtr, 16 ) );

			size_t hexLen = endPtr - encoded.c_str();
			if ( hexLen == 2 ) {
				// list is based on RFC 3986
				// https://tools.ietf.org/html/rfc3986#section-2.3
				if ( ( value >= 0x41 && value <= 0x5A ) ||
				     ( value >= 0x61 && value <= 0x7A ) ||
				     ( value >= 0x30 && value <= 0x39 ) ||
				     ( value == 0x2D ) || ( value == 0x2E ) || ( value == 0x5F ) || ( value == 0x7E ) ) {
					component->erase( percentPos, 2 );
					(*component)[ percentPos ] = value;
				} else {
					// change to upper case
					if ( is_lower_a( encoded[0] ) ) {
						(*component)[ percentPos + 1 ] = to_upper_a( encoded[0] );
					}

					if ( is_lower_a( encoded[1] ) ) {
						(*component)[ percentPos + 2 ] = to_upper_a( encoded[1] );
					}
				}
			} else {
				// invalid url encoded (encode percent)
				component->insert( percentPos + 1, "25" );
			}
		}
		++percentPos;
	}
}

UrlComponent::UrlComponent( UrlComponent::Type type, const char *pos, size_t len, char separator )
	: m_type ( type )
	, m_componentStr( pos, len )
	, m_separator( separator )
	, m_keyLen( len )
	, m_deleted( false) {
	// normalize string
	normalize( &m_componentStr );

	size_t separatorPos = m_componentStr.find( '=' );
	if ( separatorPos == std::string::npos ) {
		// try again with comma
		separatorPos = m_componentStr.find( ',' );
	}

	if ( separatorPos != std::string::npos ) {
		m_keyLen = separatorPos;
	}

	m_key = m_componentStr.substr( 0, m_keyLen );
	std::transform( m_key.begin(), m_key.end(), m_key.begin(), ::tolower );
}

void UrlComponent::print() const {
	logf( LOG_DEBUG, "UrlComponent::type         : %s", m_type == TYPE_PATH ? "path" : m_type == TYPE_QUERY ? "query" : "unknown" );
	logf( LOG_DEBUG, "UrlComponent::componentStr : %s", m_componentStr.c_str() );
	logf( LOG_DEBUG, "UrlComponent::separator    : %c", m_separator );
	logf( LOG_DEBUG, "UrlComponent::key          : %s", m_key.c_str() );
	logf( LOG_DEBUG, "UrlComponent::keyLen       : %zu", m_keyLen );
	logf( LOG_DEBUG, "UrlComponent::deleted      : %s", m_deleted ? "true" : "false" );
}

UrlComponent::Matcher::Matcher( const char *param, MatchCriteria matchCriteria )
	: m_param( param )
	, m_matchCriteria( matchCriteria )
	, m_matchPartial( matchCriteria & MATCH_PARTIAL )
	, m_matchCase( matchCriteria & MATCH_CASE ) {
	if ( !m_matchCase ) {
		std::transform( m_param.begin(), m_param.end(), m_param.begin(), ::tolower );
	}
}

void UrlComponent::Matcher::print() const {
	logf( LOG_DEBUG, "UrlComponent::Matcher::param         : %s", m_param.c_str() );
	logf( LOG_DEBUG, "UrlComponent::Matcher::matchCriteria : %d", m_matchCriteria );
	logf( LOG_DEBUG, "UrlComponent::Matcher::matchPartial  : %s", m_matchPartial ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Matcher::matchCase     : %s", m_matchCase ? "true" : "false" );
}

bool UrlComponent::Matcher::isMatching( const UrlComponent &urlPart ) const {
	if ( m_matchCase ) {
		if ( ( m_matchPartial && m_param.size() <= urlPart.m_keyLen ) || ( m_param.size() == urlPart.m_keyLen ) ) {
			return ( std::equal( m_param.begin(), m_param.end(), urlPart.m_componentStr.begin() ) );
		}

		return false;
	}

	if ( m_matchPartial ) {
		return ( urlPart.m_key.find(m_param) != std::string::npos );
	}

	return ( urlPart.m_key == m_param );
}

UrlComponent::Validator::Validator( size_t minLength, size_t maxLength, bool allowEmpty,
                                    AllowCriteria allowCriteria, MandatoryCriteria mandatoryCriteria )
	: m_minLength( minLength )
	, m_maxLength( maxLength )
	, m_allowEmpty( allowEmpty )
	, m_allowCriteria( allowCriteria )
	, m_allowAlpha ( allowCriteria & ( ALLOW_HEX | ALLOW_ALPHA | ALLOW_ALPHA_LOWER | ALLOW_ALPHA_UPPER ) )
	, m_allowAlphaLower( allowCriteria & ALLOW_ALPHA_LOWER )
	, m_allowAlphaUpper( allowCriteria & ALLOW_ALPHA_UPPER )
	, m_allowAlphaHex( allowCriteria & ( ALLOW_HEX | ALLOW_ALPHA_LOWER | ALLOW_ALPHA_UPPER ) )
	, m_allowDigit( allowCriteria & ( ALLOW_DIGIT | ALLOW_HEX ) )
	, m_allowPunctuation( allowCriteria & ( ALLOW_PUNCTUATION ) )
	, m_mandatoryCriteria( mandatoryCriteria )
	, m_mandatoryAlpha( mandatoryCriteria & ( MANDATORY_ALPHA_HEX | MANDATORY_ALPHA | MANDATORY_ALPHA_LOWER | MANDATORY_ALPHA_UPPER ) )
	, m_mandatoryAlphaLower( mandatoryCriteria & MANDATORY_ALPHA_LOWER )
	, m_mandatoryAlphaUpper( mandatoryCriteria & MANDATORY_ALPHA_UPPER )
	, m_mandatoryAlphaHex( mandatoryCriteria & MANDATORY_ALPHA_HEX )
	, m_mandatoryDigit( mandatoryCriteria & MANDATORY_DIGIT )
	, m_mandatoryPunctuation( mandatoryCriteria & MANDATORY_PUNCTUATION ) {
}

void UrlComponent::Validator::print() const {
	logf( LOG_DEBUG, "UrlComponent::Validator::minLength            : %zu", m_minLength );
	logf( LOG_DEBUG, "UrlComponent::Validator::maxLength            : %zu", m_maxLength );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowEmpty           : %s", m_allowEmpty ? "true" : "false" );

	logf( LOG_DEBUG, "UrlComponent::Validator::allowCriteria        : %d", m_allowCriteria );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowAlpha           : %s", m_allowAlpha ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowAlphaLower      : %s", m_allowAlphaLower ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowAlphaUpper      : %s", m_allowAlphaUpper ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowAlphaHex        : %s", m_allowAlphaHex ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowDigit           : %s", m_allowDigit ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::allowPunctuation     : %s", m_allowPunctuation ? "true" : "false" );

	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryCriteria    : %d", m_mandatoryCriteria );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryAlpha       : %s", m_mandatoryAlpha ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryAlphaLower  : %s", m_mandatoryAlphaLower ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryAlphaUpper  : %s", m_mandatoryAlphaUpper ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryAlphaHex    : %s", m_mandatoryAlphaHex ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryDigit       : %s", m_mandatoryDigit ? "true" : "false" );
	logf( LOG_DEBUG, "UrlComponent::Validator::mandatoryPunctuation : %s", m_mandatoryPunctuation ? "true" : "false" );
}

bool UrlComponent::Validator::isValid( const UrlComponent &urlPart ) const {
	size_t valueLen = urlPart.getValueLen();

	// allow empty value
	if ( valueLen == 0 && m_allowEmpty ) {
		return true;
	}

	// check length
	if ( ( m_minLength && m_minLength > valueLen ) || ( m_maxLength && m_maxLength < valueLen ) ) {
		return false;
	}

	// no other checks required
	if ( m_allowCriteria == ALLOW_ALL && m_mandatoryCriteria == MANDATORY_NONE ) {
		return true;
	}

	bool hasAlpha = false;
	bool hasAlphaNoHexLower = false;
	bool hasAlphaNoHexUpper = false;
	bool hasAlphaHexUpper = false;
	bool hasAlphaHexLower = false;
	bool hasDigit = false;
	bool hasPunctuation = false;

	const char *value = urlPart.getValue();
	for ( size_t i = 0; i < valueLen; ++i ) {
		char c = value[i];

		if ( !hasAlphaNoHexLower || !hasAlphaNoHexUpper || !hasAlphaHexLower || !hasAlphaHexUpper ) {
			if ( is_alpha_a( c ) ) {
				hasAlpha = true;

				if ( c >= 'a' && c <= 'f' ) {
					hasAlphaHexLower = true;
					continue;
				}

				if ( c >= 'A' && c <= 'F' ) {
					hasAlphaHexUpper = true;
					continue;
				}

				if ( !hasAlphaNoHexLower && ( hasAlphaNoHexLower = is_lower_a( c ) ) ) {
					continue;
				}

				if ( !hasAlphaNoHexUpper && ( hasAlphaNoHexUpper = is_upper_a( c ) ) ) {
					continue;
				}

				continue;
			}
		}

		if ( !hasDigit && ( hasDigit = is_digit( c ) ) ) {
			continue;
		}

		if ( !hasPunctuation && ( hasPunctuation = is_punct_a( c ) ) ) {
			continue;
		}
	}

	bool validAllow = true;
	bool validMandatory = true;

	if ( m_allowCriteria != ALLOW_ALL ) {
		validAllow = !( ( !m_allowAlpha && hasAlpha ) &&
		                ( !m_allowAlphaLower && hasAlphaNoHexLower ) &&
		                ( !m_allowAlphaUpper && hasAlphaNoHexUpper ) &&
		                ( !m_allowAlphaHex && ( hasAlphaHexLower || hasAlphaHexUpper ) ) &&
		                ( !m_allowDigit && hasDigit ) &&
		                ( !m_allowPunctuation && hasPunctuation ) );
	}

	if ( m_mandatoryCriteria != MANDATORY_NONE ) {
		validMandatory = ( !m_mandatoryAlpha || ( m_mandatoryAlpha && hasAlpha ) ) &&
		                 ( !m_mandatoryAlphaLower || ( m_mandatoryAlphaLower && ( hasAlphaHexLower || hasAlphaNoHexLower ) ) ) &&
		                 ( !m_mandatoryAlphaUpper || ( m_mandatoryAlphaUpper && ( hasAlphaHexUpper || hasAlphaNoHexUpper ) ) ) &&
		                 ( !m_mandatoryAlphaHex || ( m_mandatoryAlphaHex && ( hasAlphaHexLower || hasAlphaHexUpper ) ) ) &&
		                 ( !m_mandatoryDigit || ( m_mandatoryDigit && hasDigit ) ) &&
		                 ( !m_mandatoryPunctuation || ( m_mandatoryPunctuation && hasPunctuation ) );
	}

	return ( validAllow && validMandatory );
}

