#include "UrlComponent.h"
#include "fctypes.h"
#include <algorithm>
#include "Log.h"

UrlComponent::UrlComponent( const char *pos, size_t len, char separator )
	: m_pos( pos )
	, m_len( len )
	, m_separator( separator )
	, m_keyLen( len )
	, m_deleted( false) {
	const char *separatorPos = static_cast<const char*>( memchr( m_pos, '=', m_len ) );
	if ( !separatorPos ) {
		// try again with comma
		separatorPos = static_cast<const char*>( memchr( m_pos, ',', m_len ) );
	}

	if ( separatorPos ) {
		m_keyLen = separatorPos - m_pos;
	}

	m_key = std::string( m_pos, m_keyLen );
	std::transform( m_key.begin(), m_key.end(), m_key.begin(), ::tolower );
}

UrlComponent::Matcher::Matcher( const char *param, MatchCriteria matchCriteria )
	: m_param( param )
	, m_matchPartial( matchCriteria & MATCH_PARTIAL )
	, m_matchCase( matchCriteria & MATCH_CASE ) {
	if ( !m_matchCase ) {
		std::transform( m_param.begin(), m_param.end(), m_param.begin(), ::tolower );
	}
}

bool UrlComponent::Matcher::isMatching( const UrlComponent &urlPart ) const {
	if ( m_matchCase ) {
		// m_param -> p_urlPart->m_pos, p_urlPart->m_keyLen
		if ( m_matchPartial ) {
			return ( memmem( urlPart.m_pos, urlPart.m_keyLen, m_param.c_str(), m_param.size() ) != NULL );
		}

		return ( urlPart.m_keyLen == m_param.size() && memcmp( urlPart.m_pos, m_param.c_str(), m_param.size() ) == 0 );
	}

	if ( m_matchPartial ) {
		return ( urlPart.getKey().find(m_param) != std::string::npos );
	}

	return ( urlPart.getKey() == m_param );
}

UrlComponent::Validator::Validator( size_t minLength, size_t maxLength, bool allowEmpty,
                                    AllowCriteria allowCriteria, MandatoryCriteria mandatoryCriteria )
	: m_minLength( minLength )
	, m_maxLength( maxLength )
	, m_allowEmpty( allowEmpty )
	, m_allowCriteria( allowCriteria )
	, m_mandatoryCriteria( mandatoryCriteria )
	, m_allowAlpha ( allowCriteria & ( ALLOW_HEX | ALLOW_ALPHA | ALLOW_ALPHA_LOWER | ALLOW_ALPHA_UPPER ) )
	, m_allowAlphaLower( allowCriteria & ALLOW_ALPHA_LOWER )
	, m_allowAlphaUpper( allowCriteria & ALLOW_ALPHA_UPPER )
	, m_allowAlphaHex( allowCriteria & ( ALLOW_HEX | ALLOW_ALPHA_LOWER | ALLOW_ALPHA_UPPER ) )
	, m_allowDigit( allowCriteria & ( ALLOW_DIGIT | ALLOW_HEX ) )
	, m_allowPunctuation( allowCriteria & ( ALLOW_PUNCTUATION ) ) {
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
	bool hasAlphaHex = false;
	bool hasDigit = false;
	bool hasPunctuation = false;

	const char *value = urlPart.getValue();
	for ( size_t i = 0; i < valueLen; ++i ) {
		char c = value[i];

		if ( !hasAlphaNoHexLower || !hasAlphaNoHexUpper || !hasAlphaHex ) {
			if ( is_alpha_a( c ) ) {
				hasAlpha = true;

				if ( ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ) ) {
					hasAlphaHex = true;
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
		                ( !m_allowAlphaHex && hasAlphaHex ) &&
		                ( !m_allowDigit && hasDigit ) &&
		                ( !m_allowPunctuation && hasPunctuation ) );
	}

	if ( m_mandatoryCriteria != MANDATORY_NONE ) {
		/// @todo
		//validMandatory
	}

	return ( validAllow && validMandatory );
}

