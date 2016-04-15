#ifndef GB_URLCOMPONENT_H
#define GB_URLCOMPONENT_H

#include <string>

class UrlComponent {
public:
	UrlComponent( const char *pos, size_t len, char separator );

	std::string getKey() const {
		return m_key;
	}

	bool hasValue() const {
		return ( m_keyLen != m_len );
	}

	const char* getValue() const {
		if ( m_keyLen == m_len ) {
			return m_pos;
		}

		return m_pos + m_keyLen + 1;
	}

	size_t getValueLen() const {
		if ( m_keyLen == m_len ) {
			return m_len;
		}

		return m_len - m_keyLen - 1;
	}

	void setDeleted() {
		m_deleted = true;
	}

	class Matcher;
	class Validator;

//private:
	const char *m_pos;
	size_t m_len;
	char m_separator;

	std::string m_key;
	size_t m_keyLen;

	bool m_deleted;
};

enum MatchCriteria {
	MATCH_DEFAULT = 0,
	MATCH_CASE = 1,
	MATCH_PARTIAL = 2
};

inline MatchCriteria operator|( MatchCriteria a, MatchCriteria b ) {
	return static_cast<MatchCriteria>( static_cast<int>(a) | static_cast<int>(b) );
}

inline MatchCriteria operator&( MatchCriteria a, MatchCriteria b ) {
	return static_cast<MatchCriteria>( static_cast<int>(a) & static_cast<int>(b) );
}

class UrlComponent::Matcher {
public:
	Matcher( const char *param, MatchCriteria matchCriteria = MATCH_DEFAULT );

	bool isMatching( const UrlComponent &urlPart ) const; 

//private:
	std::string m_param;
	bool m_matchPartial;
	bool m_matchCase;
};

// allowed characters
enum AllowCriteria {
	ALLOW_ALL = 0,
	ALLOW_DIGIT = 1, // allow digit
	ALLOW_HEX = 2, // allow hex
	ALLOW_ALPHA = 4, // allow alpha (lower/upper)
	ALLOW_ALPHA_LOWER = 8, // allow alpha lower
	ALLOW_ALPHA_UPPER = 16, // allow alpha upper
	ALLOW_PUNCTUATION = 32 // allow punctuation
};

inline AllowCriteria operator|( AllowCriteria a, AllowCriteria b ) {
	return static_cast<AllowCriteria>( static_cast<int>(a) | static_cast<int>(b) );
}

inline AllowCriteria operator&( AllowCriteria a, AllowCriteria b ) {
	return static_cast<AllowCriteria>( static_cast<int>(a) & static_cast<int>(b) );
}

// mandatory characters
enum MandatoryCriteria {
	MANDATORY_NONE = 0,
	MANDATORY_DIGIT = 1, // must have digit
	MANDATORY_HEX = 2, // must have hex
	MANDATORY_ALPHA = 4, // must have alpha (lower/upper)
	MANDATORY_ALPHA_LOWER = 8, // must have alpha lower
	MANDATORY_ALPHA_UPPER = 16, // must have alpha upper
	MANDATORY_PUNCTUATION = 32 // must have punctuation
};

inline MandatoryCriteria operator|( MandatoryCriteria a, MandatoryCriteria b ) {
	return static_cast<MandatoryCriteria>( static_cast<int>(a) | static_cast<int>(b) );
}

inline MandatoryCriteria operator&( MandatoryCriteria a, MandatoryCriteria b ) {
	return static_cast<MandatoryCriteria>( static_cast<int>(a) & static_cast<int>(b) );
}

class UrlComponent::Validator {
public:
	Validator( size_t minLength = 0, size_t maxLength = 0, bool allowEmpty = false, AllowCriteria allowCriteria = ALLOW_ALL, MandatoryCriteria mandatoryCriteria = MANDATORY_NONE );

	bool allowEmptyValue() const {
		return m_allowEmpty;
	}

	bool isValid( const UrlComponent &urlPart ) const;

private:
	size_t m_minLength;
	size_t m_maxLength;
	bool m_allowEmpty;

	AllowCriteria m_allowCriteria;
	MandatoryCriteria m_mandatoryCriteria;

	bool m_allowAlpha;
	bool m_allowAlphaLower;
	bool m_allowAlphaUpper;
	bool m_allowAlphaHex;
	bool m_allowDigit;
	bool m_allowPunctuation;
};

#endif // GB_URLCOMPONENT_H
