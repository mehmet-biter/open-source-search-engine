#include "gb-include.h"

#include "HttpMime.h"
#include "HashTable.h"
#include "HashTableX.h"
#include "Process.h"
#include "Conf.h"

#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

// . convert these values to strings
// . these must be 1-1 with the #define's in HttpMime.h
const char * const g_contentTypeStrings [] = {
	""     ,
	"html" ,
	"text" ,
	"xml"  ,
	"pdf"  ,
	"doc"  ,
	"xls"  ,
	"ppt"  ,
	"ps"   , // 8
	"gif"  , // 9
	"jpg"  , // 10
	"png"  , // 11
	"tiff" , // 12
	"bmp"  , // 13
	"javascript" , // 14
	"css"  , // 15
	"json" ,  // 16
	"image", // 17
	"spiderstatus", // 18
	"gzip",	// 19
	"arc",	// 20
	"warc"	// 21
};

HttpMime::HttpMime () { 
	// Coverity
	m_content = NULL;
	memset(m_buf, 0, sizeof(m_buf));
	m_mimeLen = 0;
	m_contentEncoding = 0;

	reset(); 
}

void HttpMime::reset ( ) {
	m_mime = NULL;

	// clear values
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;
	m_nextLineStartPos = 0;
	m_attributeStartPos = 0;

	m_currentTime = time(NULL);
	m_fakeCurrentTime = false;

	m_status = -1;
	m_contentLen = -1;
	m_contentType = CT_HTML;
	m_charset = NULL;
	m_charsetLen = 0;
	m_locationField = NULL;
	m_locationFieldLen = 0;
	m_contentEncodingPos = NULL;
	m_contentLengthPos = NULL;
	m_contentTypePos = NULL;

	m_cookies.clear();
}

// . returns false if could not get a valid mime
// . we need the url in case there's a Location: mime that's base-relative
bool HttpMime::set ( char *buf , int32_t bufLen , Url *url ) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf,bufLen);
#endif
	// reset some stuff
	m_mime = NULL;
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;
	m_nextLineStartPos = 0;
	m_attributeStartPos = 0;

	if (!m_fakeCurrentTime) {
		m_currentTime = time(NULL);
	}

	m_contentLen = -1;
	m_content = NULL;
	m_mimeLen = 0;
	m_contentType = CT_HTML;
	m_contentEncoding = ET_IDENTITY;
	m_charset = NULL;
	m_charsetLen = 0;

	m_cookies.clear();

	// at the very least we should have a "HTTP/x.x 404\[nc]"
	if ( bufLen < 13 ) {
		return false;
	}

	// . get the length of the Mime, must end in \r\n\r\n , ...
	// . m_bufLen is used as the mime length
	m_mime = buf;
	m_mimeLen = getMimeLen(buf, bufLen);

	// . return false if we had no mime boundary
	// . but set m_bufLen to 0 so getMimeLen() will return 0 instead of -1
	//   thus avoiding a potential buffer overflow
	if (m_mimeLen == 0) {
		log(LOG_WARN, "mime: no rnrn boundary detected");
		return false; 
	}

	// set this
	m_content = buf + m_mimeLen;

	// . parse out m_status, m_contentLen, m_lastModifiedData, contentType
	// . returns false on bad mime
	return parse ( buf , m_mimeLen , url );
}

// https://tools.ietf.org/html/rfc2616#section-19.3
// The line terminator for message-header fields is the sequence CRLF. However, we recommend that applications,
// when parsing such headers, recognize a single LF as a line terminator and ignore the leading CR.

// . returns 0 if no boundary found
size_t HttpMime::getMimeLen(char *buf, size_t bufLen) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf,bufLen);
#endif
	// the size of the terminating boundary, either 1 or 2 bytes.
	// just the last \n in the case of a \n\n or \r in the case
	// of a \r\r, but it is the full \r\n in the case of a last \r\n\r\n
	size_t bsize = 0;

	// find the boundary
	size_t i;
	for ( i = 0 ; i < bufLen ; i++ ) {
		// continue until we hit a \r or \n
		if ( buf[i] != '\r' && buf[i] != '\n' ) continue;
		// boundary check
		if ( i + 1 >= bufLen ) continue;
		// prepare for a smaller mime size
		bsize = 1;
		// \r\r
		if ( buf[i  ] == '\r' && buf[i+1] == '\r' ) break;
		// \n\n
		if ( buf[i  ] == '\n' && buf[i+1] == '\n' ) break;
		// boundary check
		if ( i + 3 >= bufLen ) continue;
		// prepare for a larger mime size
		bsize = 2;
		// \r\n\r\n
		if ( buf[i  ] == '\r' && buf[i+1] == '\n' &&
		     buf[i+2] == '\r' && buf[i+3] == '\n'  ) break;
		// \n\r\n\r
		if ( buf[i  ] == '\n' && buf[i+1] == '\r' &&
		     buf[i+2] == '\n' && buf[i+3] == '\r'  ) break;
	}

	// return false if could not find the end of the MIME
	if ( i == bufLen ) {
		return 0;
	}

	return i + bsize * 2;
}

bool HttpMime::getNextLine() {
	// clear values
	m_currentLine = NULL;
	m_currentLineLen = 0;
	m_valueStartPos = 0;
	m_attributeStartPos = 0;

	size_t currentPos = m_nextLineStartPos;

	// don't cross limit
	if (currentPos == m_mimeLen) {
		return false;
	}

	m_currentLine = m_mime + currentPos;

	// cater for multiline header
	size_t linePos = currentPos;
	do {
		bool foundLineEnding = false;

		char currentChar = m_mime[currentPos];
		while (currentPos < m_mimeLen) {
			if (!foundLineEnding) {
				if (currentChar == '\r' || currentChar == '\n') {
					foundLineEnding = true;
					m_currentLineLen = (currentPos - linePos);
				}
			} else {
				if (currentChar != '\r' && currentChar != '\n') {
					break;
				}
			}

			currentChar = m_mime[++currentPos];
		}
	} while (m_currentLineLen && currentPos < m_mimeLen && (m_mime[currentPos] == ' ' || m_mime[currentPos] == '\t'));

	if (m_currentLineLen == 0) {
		// set to end of mime
		m_currentLineLen = (m_mime + m_mimeLen) - m_currentLine;
	}

	// store next lineStartPos
	m_nextLineStartPos = currentPos;

	logTrace(g_conf.m_logTraceHttpMime, "line='%.*s'", static_cast<int>(m_currentLineLen), m_currentLine);

	return true;
}

bool HttpMime::getField(const char **field, size_t *fieldLen) {
	size_t currentLinePos = m_valueStartPos;

	const char *colonPos = (const char *)memchr(m_currentLine + currentLinePos, ':', m_currentLineLen);

	// no colon
	if (colonPos == NULL) {
		return false;
	}

	currentLinePos = colonPos - m_currentLine;
	m_valueStartPos = currentLinePos + 1;

	*field = m_currentLine;
	*fieldLen = currentLinePos;

	// strip ending whitespaces
	while (*fieldLen > 0 && is_wspace_a(m_currentLine[*fieldLen - 1])) {
		--(*fieldLen);
	}

	logTrace(g_conf.m_logTraceHttpMime, "field='%.*s'", static_cast<int>(*fieldLen), *field);

	return (*fieldLen > 0);
}

bool HttpMime::getValue(const char **value, size_t *valueLen) {
	// strip starting whitespaces
	while (is_wspace_a(m_currentLine[m_valueStartPos]) && (m_valueStartPos < m_currentLineLen)) {
		++m_valueStartPos;
	}

	*value = m_currentLine + m_valueStartPos;
	*valueLen = m_currentLineLen - m_valueStartPos;

	const char *semicolonPos = (const char *)memchr(*value, ';', *valueLen);
	if (semicolonPos) {
		// value should end at semicolon if present
		*valueLen = semicolonPos - *value;
		m_attributeStartPos = semicolonPos - m_currentLine + 1;
	}

	logTrace(g_conf.m_logTraceHttpMime, "value='%.*s'", static_cast<int>(*valueLen), *value);

	return (*valueLen > 0);
}

bool HttpMime::getAttribute(const char **attribute, size_t *attributeLen, const char **attributeValue, size_t *attributeValueLen) {
	// initialize value
	*attribute = NULL;
	*attributeLen = 0;
	*attributeValue = NULL;
	*attributeValueLen = 0;

	// no attribute
	if (m_attributeStartPos == 0) {
		return false;
	}

	// strip starting whitespaces
	while (is_wspace_a(m_currentLine[m_attributeStartPos]) && (m_attributeStartPos < m_currentLineLen)) {
		++m_attributeStartPos;
	}

	*attribute = m_currentLine + m_attributeStartPos;
	*attributeLen = m_currentLineLen - m_attributeStartPos;

	// next attribute
	const char *semicolonPos = (const char *)memchr(*attribute, ';', *attributeLen);
	if (semicolonPos) {
		*attributeLen = semicolonPos - *attribute;
		m_attributeStartPos = semicolonPos - m_currentLine + 1;
	} else {
		m_attributeStartPos = 0;
	}

	// attribute value
	const char *equalPos = (const char *)memchr(*attribute, '=', *attributeLen);
	if (equalPos) {
		*attributeValueLen = *attributeLen;
		*attributeLen = equalPos - *attribute;
		*attributeValueLen -= *attributeLen + 1;
		*attributeValue = equalPos + 1;

		// strip ending attribute whitespace
		while (is_wspace_a((*attribute)[*attributeLen - 1])) {
			--(*attributeLen);
		}

		// strip starting attribute value whitespace/quote
		while (is_wspace_a((*attributeValue)[0]) || (*attributeValue)[0] == '"' || (*attributeValue)[0] == '\'') {
			++(*attributeValue);
			--(*attributeValueLen);
		}

		// strip ending attribute value whitespace/quote
		while (is_wspace_a((*attributeValue)[*attributeValueLen - 1]) || (*attributeValue)[*attributeValueLen - 1] == '"' || (*attributeValue)[*attributeValueLen - 1] == '\'') {
			--(*attributeValueLen);
		}
	}

	// cater for empty values between semicolon
	// eg: Set-Cookie: name=value; Path=/; ;SECURE; HttpOnly;
	if (*attributeLen == 0 && m_attributeStartPos) {
		return getAttribute(attribute, attributeLen, attributeValue, attributeValueLen);
	}

	logTrace(g_conf.m_logTraceHttpMime, "attribute='%.*s' value='%.*s'", static_cast<int>(*attributeLen), *attribute, static_cast<int>(*attributeValueLen), *attributeValue);

	return (*attributeLen > 0);
}

// Location
bool HttpMime::parseLocation(const char *field, size_t fieldLen, Url *baseUrl) {
	static const char s_location[] = "location";
	static const size_t s_locationLen = strlen(s_location);

	if (fieldLen == s_locationLen && strncasecmp(field, s_location, fieldLen) == 0) {
		const char *value = NULL;
		size_t valueLen = 0;

		if (getValue(&value, &valueLen)) {
			m_locationField = value;
			m_locationFieldLen = valueLen;

			if (baseUrl) {
				m_locUrl.set(baseUrl, m_locationField, m_locationFieldLen);
			}
		}

		return true;
	}

	return false;
}

// https://tools.ietf.org/html/rfc2616#section-3.3.1
// Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
// Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
// Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
bool HttpMime::parseCookieDate(const char *value, size_t valueLen, time_t *time) {
	std::string dateStr(value, valueLen);

	struct tm tm = {};
	// not set by strptime()
	tm.tm_isdst = -1;

	const char *dashPos = (const char*)memchr(value, '-', valueLen);
	const char *commaPos = (const char*)memchr(value, ',', valueLen);

	// Fri, 02 Dec 2016 17:29:41 -0000
	if (dashPos && dashPos + 4 < value + valueLen) {
		if (memcmp(dashPos, "-0000", 5) == 0) {
			dashPos = NULL;
		}
	}

	if (dashPos) {
		if (commaPos) {
			// Sunday, 06-Nov-94 08:49:37 GMT (RFC 850)
			if (strptime(dateStr.c_str(), "%a, %d-%b-%y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}

			// Sun, 27-Nov-2016 22:15:17 GMT
			if (strptime(dateStr.c_str(), "%a, %d-%b-%Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}

			// Sat, 26-11-2026 16:41:30 GMT
			if (strptime(dateStr.c_str(), "%a, %d-%m-%Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}
		} else {
			// Thu 31-Dec-2020 00:00:00 GMT
			if (strptime(dateStr.c_str(), "%a %d-%b-%Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}

			// 2018-03-21 18:43:07
			if (strptime(dateStr.c_str(), "%Y-%m-%d %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}
		}
	} else {
		if (commaPos) {
			// Sun, 06 Nov 1994 08:49:37 GMT (RFC 1123)
			if (strptime(dateStr.c_str(), "%a, %d %b %Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}
		} else {
			// Sun Nov  6 08:49:37 1994 (asctime)
			if (strptime(dateStr.c_str(), "%a %b %d %T %Y", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}

			// Sat 26 Nov 2016 13:38:06 GMT
			if (strptime(dateStr.c_str(), "%a %d %b %Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}

			// 23 Nov 2026 23:34:25 GMT
			if (strptime(dateStr.c_str(), "%d %b %Y %T", &tm) != NULL) {
				*time = timegm(&tm);
				return true;
			}
		}
	}

	logTrace(g_conf.m_logTraceHttpMime, "invalid date format='%.*s'", static_cast<int>(valueLen), value);
	return false;
}

// Set-Cookie
bool HttpMime::parseSetCookie(const char *field, size_t fieldLen) {
	static const char s_setCookie[] = "set-cookie";
	static const size_t s_setCookieLen = strlen(s_setCookie);

	static const char s_expires[] = "expires";
	static const size_t s_expiresLen = strlen(s_expires);

	static const char s_maxAge[] = "max-age";
	static const size_t s_maxAgeLen = strlen(s_maxAge);

	static const char s_domain[] = "domain";
	static const size_t s_domainLen = strlen(s_domain);

	static const char s_path[] = "path";
	static const size_t s_pathLen = strlen(s_path);

	static const char s_secure[] = "secure";
	static const size_t s_secureLen = strlen(s_secure);

	static const char s_httpOnly[] = "httponly";
	static const size_t s_httpOnlyLen = strlen(s_httpOnly);

	if (fieldLen == s_setCookieLen && strncasecmp(field, s_setCookie, fieldLen) == 0) {
		httpcookie_t cookie = {};

		const char *value = NULL;
		size_t valueLen = 0;

		if (getValue(&value, &valueLen)) {
			cookie.m_cookie = value;
			cookie.m_cookieLen = valueLen;

			const char *equalPos = (const char *)memchr(value, '=', valueLen);
			if (!equalPos) {
				// missing '=' character. ignore cookie
				return true;
			}

			cookie.m_nameLen = equalPos - value;

			logTrace(g_conf.m_logTraceHttpMime, "name=%.*s", static_cast<int>(cookie.m_nameLen), cookie.m_cookie);

			// attribute
			// https://tools.ietf.org/html/rfc6265#section-5.2
			const char *attribute = NULL;
			size_t attributeLen = 0;
			const char *attributeValue = NULL;
			size_t attributeValueLen = 0;

			bool foundMaxAge = false;
			while (getAttribute(&attribute, &attributeLen, &attributeValue, &attributeValueLen)) {
				// expires
				if (attributeLen == s_expiresLen && strncasecmp(attribute, s_expires, attributeLen) == 0) {
					// max-age overrides expires
					if (foundMaxAge) {
						continue;
					}

					time_t expiry = 0;
					if (parseCookieDate(attributeValue, attributeValueLen, &expiry) && expiry < m_currentTime) {
						// expired
						logTrace(g_conf.m_logTraceHttpMime, "expires='%.*s'. expiry=%ld currentTime=%ld expired cookie. ignoring", static_cast<int>(attributeValueLen), attributeValue, expiry, m_currentTime);
						cookie.m_expired = true;
					}

					continue;
				}

				// max-age
				// https://tools.ietf.org/html/rfc6265#section-5.2.2
				// If the first character of the attribute-value is not a DIGIT or a "-"
				// character, ignore the cookie-av.
				// If the remainder of attribute-value contains a non-DIGIT character, ignore the cookie-av.
				// Let delta-seconds be the attribute-value converted to an integer.
				// If delta-seconds is less than or equal to zero (0), let expiry-time be the earliest representable date and time.
				// Otherwise, let the expiry-time be the current date and time plus delta-seconds seconds.
				if (attributeLen == s_maxAgeLen && strncasecmp(attribute, s_maxAge, attributeLen) == 0) {
					foundMaxAge = true;
					int32_t maxAge = strtol(attributeValue, NULL, 10);
					if (maxAge == 0) {
						// expired
						logTrace(g_conf.m_logTraceHttpMime, "max-age=%.*s. expired cookie. ignoring", static_cast<int>(attributeValueLen), attributeValue);
						cookie.m_expired = true;
					}
					continue;
				}

				// domain
				// https://tools.ietf.org/html/rfc6265#section-5.2.3
				// If the attribute-value is empty, the behavior is undefined.
				// However, the user agent SHOULD ignore the cookie-av entirely.
				// If the first character of the attribute-value string is %x2E ("."):
				//   Let cookie-domain be the attribute-value without the leading %x2E (".") character.
				// Otherwise:
				//   Let cookie-domain be the entire attribute-value.
				// Convert the cookie-domain to lower case.
				if (attributeLen == s_domainLen && strncasecmp(attribute, s_domain, attributeLen) == 0) {
					// ignore first '.'
					if (attributeValue[0] == '.') {
						++attributeValue;
						--attributeValueLen;
					}

					cookie.m_domain = attributeValue;
					cookie.m_domainLen = attributeValueLen;

					continue;
				}

				// path
				// https://tools.ietf.org/html/rfc6265#section-5.2.4
				// If the attribute-value is empty or if the first character of the attribute-value is not %x2F ("/"):
				//   Let cookie-path be the default-path.
				// Otherwise:
				//   Let cookie-path be the attribute-value.
				if (attributeLen == s_pathLen && strncasecmp(attribute, s_path, attributeLen) == 0) {
					if (attributeValueLen > 0 && attributeValue[0] == '/') {
						cookie.m_path = attributeValue;
						cookie.m_pathLen = attributeValueLen;
					}
					continue;
				}

				// secure
				if (attributeLen == s_secureLen && strncasecmp(attribute, s_secure, attributeLen) == 0) {
					cookie.m_secure = true;
					continue;
				}

				// httpOnly
				if (attributeLen == s_httpOnlyLen && strncasecmp(attribute, s_httpOnly, attributeLen) == 0) {
					cookie.m_httpOnly = true;
					continue;
				}

				// add parsing of other attributes here
			}

			// if we reach here means cookie should be stored
			m_cookies[std::string(cookie.m_cookie, cookie.m_nameLen)] = cookie;
		}

		return true;
	}

	return false;
}

// Content-Type
bool HttpMime::parseContentType(const char *field, size_t fieldLen) {
	static const char s_contentType[] = "content-type";
	static const size_t s_contentTypeLen = strlen(s_contentType);

	if (fieldLen == s_contentTypeLen && strncasecmp(field, s_contentType, fieldLen) == 0) {
		const char *value = NULL;
		size_t valueLen = 0;

		if (getValue(&value, &valueLen)) {
			m_contentTypePos = value;
			m_contentType = getContentTypePrivate(value, valueLen);
		}

		return true;
	}

	return false;
}

// Content-Length
bool HttpMime::parseContentLength(const char *field, size_t fieldLen) {
	static const char s_contentLength[] = "content-length";
	static const size_t s_contentLengthLen = strlen(s_contentLength);

	if (fieldLen == s_contentLengthLen && strncasecmp(field, s_contentLength, fieldLen) == 0) {
		const char *value = NULL;
		size_t valueLen = 0;

		if (getValue(&value, &valueLen)) {
			m_contentLengthPos = value;
			m_contentLen = strtol(m_contentLengthPos, NULL, 10);
		}

		return true;
	}

	return false;
}

// Content-Encoding
bool HttpMime::parseContentEncoding(const char *field, size_t fieldLen) {
	static const char s_contentEncoding[] = "content-encoding";
	static const size_t s_contentEncodingLen = strlen(s_contentEncoding);

	if (fieldLen == s_contentEncodingLen && strncasecmp(field, s_contentEncoding, fieldLen) == 0) {
		const char *value = NULL;
		size_t valueLen = 0;

		if (getValue(&value, &valueLen)) {
			m_contentEncodingPos = value;

			static const char s_gzip[] = "gzip";
			static const size_t s_gzipLen = strlen(s_gzip);

			static const char s_deflate[] = "deflate";
			static const size_t s_deflateLen = strlen(s_deflate);

			if (valueLen == s_gzipLen && strnstr(value, s_gzip, valueLen)) {
				m_contentEncoding = ET_GZIP;
			} else if (valueLen == s_deflateLen && strnstr(value, s_deflate, valueLen)) {
				m_contentEncoding = ET_DEFLATE;
			}
		}

		return true;
	}

	return false;
}

// https://tools.ietf.org/html/rfc2616#section-2.2
// HTTP/1.1 header field values can be folded onto multiple lines if the continuation line begins with a space or
// horizontal tab. All linear white space, including folding, has the same semantics as SP.
// A recipient MAY replace any linear white space with a single SP before interpreting the field value or
// forwarding the message downstream.
//
// LWS            = [CRLF] 1*( SP | HT )

// https://tools.ietf.org/html/rfc2616#section-4.2
// Each header field consists of a name followed by a colon (":") and the field value. Field names are case-insensitive.
// The field value MAY be preceded by any amount of LWS, though a single SP is preferred.
// Header fields can be extended over multiple lines by preceding each extra line with at least one SP or HT.
//
// message-header = field-name ":" [ field-value ]
// field-name     = token
// field-value    = *( field-content | LWS )
// field-content  = <the OCTETs making up the field-value
//                  and consisting of either *TEXT or combinations
//                  of token, separators, and quoted-string>

// https://tools.ietf.org/html/rfc2616#section-19.3
// Clients SHOULD be tolerant in parsing the Status-Line and servers tolerant when parsing the Request-Line.
// In particular, they SHOULD accept any amount of SP or HT characters between fields, even though only a single SP is required.

// https://tools.ietf.org/html/rfc7230#section-3.2.4
// Historically, HTTP header field values could be extended over multiple lines by preceding each extra line with at least
// one space or horizontal tab (obs-fold).  This specification deprecates such line folding except within the message/http media type
//
// A user agent that receives an obs-fold in a response message that is not within a message/http container MUST replace
// each received obs-fold with one or more SP octets prior to interpreting the field value.

/// @todo ALC we currently don't cater for multiple cookie with the same name but different domain (we take the last entry)
/// eg: Set-Cookie: CFID=77593661; Expires=session; domain=tennisexpress.com; Path=/
///     Set-Cookie: CFID=77593661; Expires=session; domain=.tennisexpress.com; Path=/
///     Set-Cookie: CFID=77593661; Expires=session; domain=www.tennisexpress.com; Path=/

// returns false on bad mime
bool HttpMime::parse(char *mime, int32_t mimeLen, Url *url) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(mime,mimeLen);
#endif
	// reset locUrl to 0
	m_locUrl.reset();

	// return if we have no valid complete mime
	if (mimeLen == 0) {
		return false;
	}

	// status is on first line
	m_status = -1;

	// skip HTTP/x.x till we hit a space
	char *p = mime;
	char *pend = mime + mimeLen;
	while (p < pend && !is_wspace_a(*p)) p++;
	// then skip over spaces
	while (p < pend && is_wspace_a(*p)) p++;
	// return false on a problem
	if (p == pend) return false;
	// then read in the http status
	m_status = atol2(p, pend - p);
	// if no Content-Type: mime field was provided, assume html
	m_contentType = CT_HTML;
	// assume default charset
	m_charset = NULL;
	m_charsetLen = 0;

	// skip over first line
	getNextLine();

	while (getNextLine()) {
		const char *field = NULL;
		size_t fieldLen = 0;

		if (getField(&field, &fieldLen)) {
			if (parseContentEncoding(field, fieldLen)) {
				continue;
			}

			if (parseContentLength(field, fieldLen)) {
				continue;
			}

			if (parseContentType(field, fieldLen)) {
				continue;
			}

			if (parseLocation(field, fieldLen, url)) {
				continue;
			}

			if (parseSetCookie(field, fieldLen)) {
				continue;
			}

			// add parsing of other header here
		}
	}

	return true;
}


int32_t getContentTypeFromStr(const char *s, size_t slen) {
	// trim off spaces at the end
	char tmp[64];
	if ( s[slen-1] == ' ' ) {
		strncpy(tmp,s,63);
		tmp[63] = '\0';
		int32_t newLen = strlen(tmp);
		s = tmp;
		char *send = tmp + newLen;
		for ( ; send>s && send[-1] == ' '; send-- );
		*send = '\0';
	}

	int32_t ct = CT_UNKNOWN;
	if ( !strncasecmp(s, "text/", 5) ) {
		if (!strncasecmp(s, "text/html", slen)) {
			ct = CT_HTML;
		} else if (strncasecmp(s, "text/plain", slen) == 0) {
			ct = CT_TEXT;
		} else if (strncasecmp(s, "text/xml", slen) == 0) {
			ct = CT_XML;
		} else if (strncasecmp(s, "text/txt", slen) == 0) {
			ct = CT_TEXT;
		} else if (strncasecmp(s, "text/javascript", slen) == 0) {
			ct = CT_JS;
		} else if (strncasecmp(s, "text/x-js", slen) == 0) {
			ct = CT_JS;
		} else if (strncasecmp(s, "text/js", slen) == 0) {
			ct = CT_JS;
		} else if (strncasecmp(s, "text/css", slen) == 0) {
			ct = CT_CSS;
		} else if (strncasecmp(s, "text/x-vcard", slen) == 0) {
			// . semicolon separated list of info, sometimes an element is html
			// . these might have an address in them...
			ct = CT_HTML;
		} else {
			ct = CT_TEXT;
		}
	}
	else if (!strcasecmp(s,"text"                    ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"txt"                     ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"application/xml"         ) ) ct = CT_XML;
	// we were not able to spider links on an xhtml doc because
	// this was set to CT_XML, so try CT_HTML
	else if (!strcasecmp(s,"application/xhtml+xml"   ) ) ct = CT_HTML;
	else if (!strcasecmp(s,"application/rss+xml"     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"rss"                     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/rdf+xml"     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/atom+xml"    ) ) ct = CT_XML;
	else if (!strcasecmp(s,"atom+xml"                ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/pdf"         ) ) ct = CT_PDF;
	else if (!strcasecmp(s,"application/msword"      ) ) ct = CT_DOC;
	else if (!strcasecmp(s,"application/vnd.ms-excel") ) ct = CT_XLS;
	else if (!strcasecmp(s,"application/vnd.ms-powerpoint")) ct = CT_PPT;
	else if (!strcasecmp(s,"application/mspowerpoint") ) ct = CT_PPT;
	else if (!strcasecmp(s,"application/postscript"  ) ) ct = CT_PS;
	else if (!strcasecmp(s,"application/warc"        ) ) ct = CT_WARC;
	else if (!strcasecmp(s,"application/arc"         ) ) ct = CT_ARC;
	else if (!strcasecmp(s,"image/gif"               ) ) ct = CT_GIF;
	else if (!strcasecmp(s,"image/jpeg"              ) ) ct = CT_JPG;
	else if (!strcasecmp(s,"image/png"               ) ) ct = CT_PNG;
	else if (!strcasecmp(s,"image/tiff"              ) ) ct = CT_TIFF;
	else if (!strncasecmp(s,"image/",6               ) ) ct = CT_IMAGE;
	else if (!strcasecmp(s,"application/javascript"  ) ) ct = CT_JS;
	else if (!strcasecmp(s,"application/x-javascript") ) ct = CT_JS;
	else if (!strcasecmp(s,"application/x-gzip"      ) ) ct = CT_GZ;
	else if (!strcasecmp(s,"application/json"        ) ) ct = CT_JSON;
	// facebook.com:
	else if (!strcasecmp(s,"application/vnd.wap.xhtml+xml") ) ct =CT_HTML;
	else if (!strcasecmp(s,"binary/octet-stream") ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/octet-stream") ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/binary" ) ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/x-tar" ) ) ct = CT_UNKNOWN;
	else if ( !strncmp ( s , "audio/",6)  ) ct = CT_UNKNOWN;

	return ct;
}

// . s is a NULL terminated string like "text/html"
int32_t HttpMime::getContentTypePrivate(const char *s, size_t slen) {
	const char *send = NULL;
	int32_t ct;
	// skip spaces
	while ( *s==' ' || *s=='\t' ) s++;
	// find end of s
	send = s;
	// they can have "text/plain;charset=UTF-8" too
	for ( ; *send && *send !=';' && *send !='\r' && *send !='\n' ; send++);

	//
	// point to possible charset desgination
	//
	const char *t = send ;
	// charset follows the semicolon
	if ( *t == ';' ) {
		// skip semicolon
		t++;
		// skip spaces
		while ( *t==' ' || *t=='\t' ) t++;
		// get charset name "charset=euc-jp"
		if ( strncasecmp ( t , "charset" , 7 ) == 0 ) {
			// skip it
			t += 7;
			// skip spaces, equal, spaces
			while ( *t==' ' || *t=='\t' ) t++;
			if    ( *t=='='             ) t++;
			while ( *t==' ' || *t=='\t' ) t++;
			// get charset
			m_charset = t;
			// get length
			while ( *t && *t!='\r' && *t!='\n' && *t!=' ' && *t!='\t') t++;
			m_charsetLen = t - m_charset;
		}
	}

	// returns CT_UNKNOWN if unknown
	ct = getContentTypeFromStr(s, slen);

	// log it for reference
	//if ( ct == -1 ) { g_process.shutdownAbort(true); }
	if ( ct == CT_UNKNOWN ) {
		log("http: unrecognized content type \"%s\"",s);
	}

	// return 0 for the contentType if unknown
	return ct;
}

// the table that maps a file extension to a content type
static HashTableX s_mimeTable;
static bool s_init = false;

void resetHttpMime ( ) {
	s_mimeTable.reset();
}

const char *extensionToContentTypeStr2 ( const char *ext , int32_t elen ) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return NULL;
	if ( elen <= 0 ) return NULL;
	// get hash for table look up
	int32_t key = hash32 ( ext , elen );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	if ( ! pp ) return NULL;
	return *pp;
}

const char *HttpMime::getContentTypeFromExtension ( const char *ext , int32_t elen) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return "text/html";
	if ( elen <= 0 ) return "text/html";
	// get hash for table look up
	int32_t key = hash32 ( ext , elen );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	// if not found in table, assume text/html
	if ( ! pp ) return "text/html";
	return *pp;
}


// . list of types is on: http://www.duke.edu/websrv/file-extensions.html
// . i copied it to the bottom of this file though
const char *HttpMime::getContentTypeFromExtension ( const char *ext ) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return "text/html";
	// get hash for table look up
	int32_t key = hash32n ( ext );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	// if not found in table, assume text/html
	if ( ! pp ) return "text/html";
	return *pp;
}

const char *HttpMime::getContentEncodingFromExtension ( const char *ext ) {
	if ( ! ext ) return NULL;
	if ( strcasecmp ( ext ,"bz2"  )==0 ) return "x-bzip2";
	if ( strcasecmp ( ext ,"gz"   )==0 ) return "x-gzip";
	//if ( strcasecmp ( ext ,"htm"   ) == 0 ) return "text/html";
	//if ( strcasecmp ( ext ,"html"  ) == 0 ) return "text/html";
	return NULL;
}

// make a redirect mime
void HttpMime::makeRedirMime ( const char *redir , int32_t redirLen ) {
	char *p = m_buf;
	gbmemcpy ( p , "HTTP/1.0 302 RD\r\nLocation: " , 27 );
	p += 27;
	if ( redirLen > 600 ) redirLen = 600;
	gbmemcpy ( p , redir , redirLen );
	p += redirLen;
	*p++ = '\r';
	*p++ = '\n';
	*p++ = '\r';
	*p++ = '\n';
	*p = '\0';
	m_mimeLen = p - m_buf;
	if ( m_mimeLen > 1023 ) { g_process.shutdownAbort(true); }
	// set the mime's length
	//m_bufLen = strlen ( m_buf );
}

// a cacheTime of -1 means browser should not cache at all
void HttpMime::makeMime  ( int32_t    totalContentLen    , 
			   int32_t    cacheTime          ,
			   time_t  lastModified       ,
			   int32_t    offset             , 
			   int32_t    bytesToSend        ,
			   const char   *ext                ,
			   bool    POSTReply          ,
			   const char   *contentType        ,
			   const char   *charset            ,
			   int32_t    httpStatus         ,
			   const char   *cookie             ) {
	// assume UTF-8
	//if ( ! charset ) charset = "utf-8";
	// . make the content type line
	// . uses a static buffer
	if ( ! contentType ) 
		contentType = getContentTypeFromExtension ( ext );

	// do not cache plug ins
	if ( contentType && strcmp(contentType,"application/x-xpinstall")==0)
		cacheTime = -2;

	// assume UTF-8, but only if content type is text
	// . No No No!!!  
	// . This prevents charset specification in html files
	// . -partap

	//if ( ! charset && contentType && strncmp(contentType,"text",4)==0) 
	//	charset = "utf-8";
	// this is used for bz2 and gz files (mp3?)
	const char *contentEncoding = getContentEncodingFromExtension ( ext );
	// the string
	char enc[128];
	if ( contentEncoding ) 
		sprintf ( enc , "Content-Encoding: %s\r\n", contentEncoding );
	else
		enc[0] = '\0';
	// get the time now
	//time_t now = getTimeGlobal();
	time_t now;
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// get the greenwhich mean time (GMT)
	char ns[128];
	struct tm tm_buf;
	struct tm *timeStruct = gmtime_r(&now,&tm_buf);
	// Wed, 20 Mar 2002 16:47:30 GMT
	strftime ( ns , 126 , "%a, %d %b %Y %T GMT" , timeStruct );
	// if lastModified is 0 use now
	if ( lastModified == 0 ) lastModified = now;
	// convert lastModified greenwhich mean time (GMT)
	char lms[128];
	timeStruct = gmtime_r(&lastModified,&tm_buf);
	// Wed, 20 Mar 2002 16:47:30 GMT
	strftime ( lms , 126 , "%a, %d %b %Y %T GMT" , timeStruct );
	// . the pragma no cache string (used just for proxy servers?)
	// . also use cache-control: for the browser itself (HTTP1.1, though)
	// . pns = "Pragma: no-cache\nCache-Control: no-cache\nExpires: -1\n";
	char tmp[128];
	const char *pns;
	// with cache-control on, when you hit the back button, it reloads
	// the page, this is bad for most things... so we only avoid the
	// cache for index.html and PageAddUrl.cpp (the main and addurl page)
	if      ( cacheTime == -2 ) pns =  "Cache-Control: no-cache\r\n"
					   "Pragma: no-cache\r\n"
					   "Expires: -1\r\n";
	// so when we click on a control link, it responds correctly.
	// like turning spiders on.
	else if  ( cacheTime == -1 ) pns = "Pragma: no-cache\r\n"
					   "Expires: -1\r\n";
	// don't specify cache times if it's 0 (let browser regulate it)
	else if ( cacheTime == 0 ) pns = "";
	// otherwise, expire tag: "Expires: Wed, 23 Dec 2001 10:23:01 GMT"
	else {
		time_t  expDate = now + cacheTime;
		timeStruct = gmtime_r(&expDate,&tm_buf);
		strftime ( tmp , 100 , "Expires: %a, %d %b %Y %T GMT\r\n", 
			   timeStruct );
		pns = tmp;
	}
	// . set httpStatus
	// . a reply to a POST (not a GET or HEAD) should be 201
	char *p = m_buf;
	const char *smsg = "";
	if ( POSTReply ) {
		if ( httpStatus == -1 ) httpStatus = 200;
		if ( httpStatus == 200 ) smsg = " OK";
		if ( ! charset ) charset = "utf-8";
		//sprintf ( m_buf , 
		p += sprintf ( p,
			  "HTTP/1.0 %" PRId32"%s\r\n"
			  "Date: %s\r\n"
			       //"P3P: CP=\"CAO PSA OUR\"\r\n"
			  "Access-Control-Allow-Origin: *\r\n"
			  "Server: Gigablast/1.0\r\n"
			  "Content-Length: %" PRId32"\r\n"
			  "Connection: Close\r\n"
			  "%s"
			  "Content-Type: %s\r\n",
			  httpStatus , smsg ,
			  ns , totalContentLen , enc , contentType  );
			  //pns ,
	                  //ns );
			  //lms );
	}
	// . is it partial content?
	// . if bytesToSend is < 0 it means "totalContentLen"
	else if ( offset > 0 || bytesToSend != -1 ) {
		if ( httpStatus == -1 ) httpStatus = 206;
		if ( ! charset ) charset = "utf-8";
		//sprintf ( m_buf , 
		p += sprintf( p,
			      "HTTP/1.0 %" PRId32" Partial content\r\n"
			      "%s"
			      "Content-Length: %" PRId32"\r\n"
			      "Content-Range: %" PRId32"-%" PRId32"(%" PRId32")\r\n"// added "bytes"
			      "Connection: Close\r\n"
			      // for ajax support
			      "Access-Control-Allow-Origin: *\r\n"
			      "Server: Gigablast/1.0\r\n"
			      "%s"
			      "Date: %s\r\n"
			      "Last-Modified: %s\r\n" 
			      "Content-Type: %s\r\n",
			      httpStatus ,
			      enc ,bytesToSend ,
			      offset , offset + bytesToSend , 
			      totalContentLen ,
			      pns ,
			      ns , 
			      lms , contentType );
		// otherwise, do a normal mime
	}
	else {
		char encoding[256];
		if (charset) sprintf(encoding, "; charset=%s", charset);
		else encoding[0] = '\0';
		
		
		if ( httpStatus == -1 ) httpStatus = 200;
		if ( httpStatus == 200 ) smsg = " OK";
		//sprintf ( m_buf , 
		p += sprintf( p,
			      "HTTP/1.0 %" PRId32"%s\r\n"
			      , httpStatus , smsg );
		// if content length is not known, as in diffbot.cpp, then
		// do not print it into the mime
		if ( totalContentLen >= 0 )
			p += sprintf ( p , 
				       // make it at least 4 spaces so we can
				       // change the length of the content 
				       // should we insert a login bar in 
				       // Proxy::storeLoginBar()
				       "Content-Length: %04" PRId32"\r\n"
				       , totalContentLen );
		p += sprintf ( p ,
			      "%s"
			      "Content-Type: %s",
			       enc , contentType );
		if ( charset ) p += sprintf ( p , "; charset=%s", charset );
		p += sprintf ( p , "\r\n");
		p += sprintf ( p ,
			       "Connection: Close\r\n"
			       "Access-Control-Allow-Origin: *\r\n"
			       "Server: Gigablast/1.0\r\n"
			       "%s"
			       "Date: %s\r\n"
			       "Last-Modified: %s\r\n" ,
			       pns ,
			       ns , 
			       lms );
	}
	// write the cookie if we have one
	if (cookie) {
		// now it is a list of Set-Cookie: x=y\r\n lines
		//p += sprintf ( p, "Set-Cookie: %s\r\n", cookie);
		if ( strncmp(cookie, "Set-Cookie", 10) != 0 ) {
			p += sprintf(p,"Set-Cookie: ");
		}
		p += sprintf ( p, "%s", cookie);
		if ( p[-1] != '\n' && p[-2] != '\r' ) {
			*p++ = '\r';
			*p++ = '\n';
		}
	}
			
	// write another line to end the mime
	p += sprintf(p, "\r\n");
	// set the mime's length
	//m_bufLen = strlen ( m_buf );
	m_mimeLen = p - m_buf;
}


//FILE EXTENSIONS to MIME CONTENT-TYPE
//------------------------------------

// set hash table
static const char * const s_ext[] = {
	"ai", "application/postscript",
	"aif", "audio/x-aiff",
	"aifc", "audio/x-aiff",
	"aiff", "audio/x-aiff",
	"asc", "text/plain",
	"au", "audio/basic",
	"avi", "video/x-msvideo",
	"bcpio", "application/x-bcpio",
	"bin", "application/octet-stream",
	"bmp", "image/gif",
	"bz2", "application/x-bzip2",
	"c", "text/plain",
	"cc", "text/plain",
	"ccad", "application/clariscad",
	"cdf", "application/x-netcdf",
	"class", "application/octet-stream",
	"cpio", "application/x-cpio",
	"cpt", "application/mac-compactpro",
	"csh", "application/x-csh",
	"css", "text/css",
	"dcr", "application/x-director",
	"dir", "application/x-director",
	"dms", "application/octet-stream",
	"doc", "application/msword",
	"drw", "application/drafting",
	"dvi", "application/x-dvi",
	"dwg", "application/acad",
	"dxf", "application/dxf",
	"dxr", "application/x-director",
	"eps", "application/postscript",
	"etx", "text/x-setext",
	"exe", "application/octet-stream",
	"ez", "application/andrew-inset",
	"f", "text/plain",
	"f90", "text/plain",
	"fli", "video/x-fli",
	"gif", "image/gif",
	"gtar", "application/x-gtar",
	"gz", "application/x-gzip",
	"h", "text/plain",
	"hdf", "application/x-hdf",
	"hh", "text/plain",
	"hqx", "application/mac-binhex40",
	"htm", "text/html",
	"html", "text/html",
	"ice", "x-conference/x-cooltalk",
	"ief", "image/ief",
	"iges", "model/iges",
	"igs", "model/iges",
	"ips", "application/x-ipscript",
	"ipx", "application/x-ipix",
	"jpe", "image/jpeg",
	"jpeg", "image/jpeg",
	"jpg", "image/jpeg",
	"js", "application/x-javascript",
	"kar", "audio/midi",
	"latex", "application/x-latex",
	"lha", "application/octet-stream",
	"lsp", "application/x-lisp",
	"lzh", "application/octet-stream",
	"m", "text/plain",
	"man", "application/x-troff-man",
	"me", "application/x-troff-me",
	"mesh", "model/mesh",
	"mid", "audio/midi",
	"midi", "audio/midi",
	"mif", "application/vnd.mif",
	"mime", "www/mime",
	"mov", "video/quicktime",
	"movie", "video/x-sgi-movie",
	"mp2", "audio/mpeg",
	"mp3", "audio/mpeg",
	"mpe", "video/mpeg",
	"mpeg", "video/mpeg",
	"mpg", "video/mpeg",
	"mpga", "audio/mpeg",
	"ms", "application/x-troff-ms",
	"msh", "model/mesh",
	"nc", "application/x-netcdf",
	"oda", "application/oda",
	"pbm", "image/x-portable-bitmap",
	"pdb", "chemical/x-pdb",
	"pdf", "application/pdf",
	"pgm", "image/x-portable-graymap",
	"pgn", "application/x-chess-pgn",
	"png", "image/png",
	"ico", "image/x-icon",
	"pnm", "image/x-portable-anymap",
	"pot", "application/mspowerpoint",
	"ppm", "image/x-portable-pixmap",
	"pps", "application/mspowerpoint",
	"ppt", "application/mspowerpoint",
	"ppz", "application/mspowerpoint",
	"pre", "application/x-freelance",
	"prt", "application/pro_eng",
	"ps", "application/postscript",
	"qt", "video/quicktime",
	"ra", "audio/x-realaudio",
	"ram", "audio/x-pn-realaudio",
	"ras", "image/cmu-raster",
	"rgb", "image/x-rgb",
	"rm", "audio/x-pn-realaudio",
	"roff", "application/x-troff",
	"rpm", "audio/x-pn-realaudio-plugin",
	"rtf", "text/rtf",
	"rtx", "text/richtext",
	"scm", "application/x-lotusscreencam",
	"set", "application/set",
	"sgm", "text/sgml",
	"sgml", "text/sgml",
	"sh", "application/x-sh",
	"shar", "application/x-shar",
	"silo", "model/mesh",
	"sit", "application/x-stuffit",
	"skd", "application/x-koan",
	"skm", "application/x-koan",
	"skp", "application/x-koan",
	"skt", "application/x-koan",
	"smi", "application/smil",
	"smil", "application/smil",
	"snd", "audio/basic",
	"sol", "application/solids",
	"spl", "application/x-futuresplash",
	"src", "application/x-wais-source",
	"step", "application/STEP",
	"stl", "application/SLA",
	"stp", "application/STEP",
	"sv4cpio", "application/x-sv4cpio",
	"sv4crc", "application/x-sv4crc",
	"swf", "application/x-shockwave-flash",
	"t", "application/x-troff",
	"tar", "application/x-tar",
	"tcl", "application/x-tcl",
	"tex", "application/x-tex",
	"texi", "application/x-texinfo",
	"texinfo", "application/x-texinfo",
	"tif", "image/tiff",
	"tiff", "image/tiff",
	"tr", "application/x-troff",
	"tsi", "audio/TSP-audio",
	"tsp", "application/dsptype",
	"tsv", "text/tab-separated-values",
	"txt", "text/plain",
	"unv", "application/i-deas",
	"ustar", "application/x-ustar",
	"vcd", "application/x-cdlink",
	"vda", "application/vda",
	"viv", "video/vnd.vivo",
	"vivo", "video/vnd.vivo",
	"vrml", "model/vrml",
	"wav", "audio/x-wav",
	"wrl", "model/vrml",
	"xbm", "image/x-xbitmap",
	"xlc", "application/vnd.ms-excel",
	"xll", "application/vnd.ms-excel",
	"xlm", "application/vnd.ms-excel",
	"xls", "application/vnd.ms-excel",
	"xlw", "application/vnd.ms-excel",
	"xml", "text/xml",
	"xpm", "image/x-xpixmap",
	"xwd", "image/x-xwindowdump",
	"xyz", "chemical/x-pdb",
	"zip", "application/zip",
	"xpi", "application/x-xpinstall",
	// newstuff
	"warc", "application/warc",
	"arc", "application/arc"
};

// . init s_mimeTable in this call
// . called from HttpServer::init
// . returns false and sets g_errno on error
bool HttpMime::init ( ) {
	// only need to call once
	if ( s_init ) return true;
	// make sure only called once
	s_init = true;

	if ( ! s_mimeTable.set(4,sizeof(char *),256,NULL,0,false,"mimetbl"))
		return false;
	// set table from internal list
	for ( uint32_t i = 0 ; i < sizeof(s_ext)/sizeof(char *) ; i+=2 ) {
		int32_t key = hash32n ( s_ext[i] );
		if ( ! s_mimeTable.addKey ( &key , &s_ext[i+1] ) ) {
			log(LOG_WARN, "HttpMime::init: failed to set table.");
			return false;
		}
	}
	// quick text
	const char *tt = getContentTypeFromExtension ( "zip" );
	if ( strcmp(tt,"application/zip") != 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "http: Failed to init mime table correctly.");
		return false;
	}
	// a more thorough test
	for ( uint32_t i = 0 ; i < sizeof(s_ext)/sizeof(char *) ; i+=2) {
		tt = getContentTypeFromExtension ( s_ext[i] );
		if ( strcmp(tt,s_ext[i+1]) == 0 ) continue;
		g_errno = EBADENGINEER;
		log(LOG_WARN, "http: Failed to do mime table correctly. i=%" PRId32,i);
		return false;
	}

	// TODO: set it from a user supplied file here
	return true;
}

void HttpMime::addCookie(const httpcookie_t &cookie, const Url &currentUrl, SafeBuf *cookieJar) {
	// don't add expired cookie into cookie jar
	if (cookie.m_expired) {
		return;
	}

	if (cookie.m_domain) {
		cookieJar->safeMemcpy(cookie.m_domain, cookie.m_domainLen);
		cookieJar->pushChar('\t');
		cookieJar->safeStrcpy(cookie.m_defaultDomain ? "FALSE\t" : "TRUE\t");
	} else {
		cookieJar->safeMemcpy(currentUrl.getHost(), currentUrl.getHostLen());
		cookieJar->pushChar('\t');

		cookieJar->safeStrcpy("FALSE\t");
	}

	if (cookie.m_path) {
		cookieJar->safeMemcpy(cookie.m_path, cookie.m_pathLen);
		cookieJar->pushChar('\t');
	} else {
		if (currentUrl.getPathLen()) {
			cookieJar->safeMemcpy(currentUrl.getPath(), currentUrl.getPathLen());
		} else {
			cookieJar->pushChar('/');
		}
		cookieJar->pushChar('\t');
	}

	if (cookie.m_secure) {
		cookieJar->safeStrcpy("TRUE\t");
	} else {
		cookieJar->safeStrcpy("FALSE\t");
	}

	// we're not using expiration field
	cookieJar->safeStrcpy("0\t");

	int32_t currentLen = cookieJar->length();
	cookieJar->safeMemcpy(cookie.m_cookie, cookie.m_cookieLen);

	// cater for multiline cookie
	const char *currentPos = cookieJar->getBufStart() + currentLen;
	const char *delPosStart = NULL;
	int32_t delLength = 0;
	while (currentPos < cookieJar->getBufPtr() - 1) {
		if (delPosStart) {
			if (is_wspace_a(*currentPos) || *currentPos == '\n' || *currentPos == '\r') {
				++delLength;
			} else {
				break;
			}
		} else {
			if (*currentPos == '\n' || *currentPos == '\r') {
				delPosStart = currentPos;
				++delLength;
			}
		}

		++currentPos;
	}
	cookieJar->removeChunk1(delPosStart, delLength);

	/// @todo ALC handle httpOnly attribute

	cookieJar->pushChar('\n');
}

bool HttpMime::addToCookieJar(Url *currentUrl, SafeBuf *sb) {
	/// @note Slightly modified from Netscape HTTP Cookie File format
	/// Difference is we only have one column for name/value

	// http://www.cookiecentral.com/faq/#3.5
	// The layout of Netscape's cookies.txt file is such that each line contains one name-value pair.
	// An example cookies.txt file may have an entry that looks like this:
	// .netscape.com TRUE / FALSE 946684799 NETSCAPE_ID 100103
	//
	// Each line represents a single piece of stored information. A tab is inserted between each of the fields.
	// From left-to-right, here is what each field represents:
	//
	// domain - The domain that created AND that can read the variable.
	// flag - A TRUE/FALSE value indicating if all machines within a given domain can access the variable. This value is set automatically by the browser, depending on the value you set for domain.
	// path - The path within the domain that the variable is valid for.
	// secure - A TRUE/FALSE value indicating if a secure connection with the domain is needed to access the variable.
	// expiration - The UNIX time that the variable will expire on. UNIX time is defined as the number of seconds since Jan 1, 1970 00:00:00 GMT.
	// name/value - The name/value of the variable.

	/// @todo ALC we should sort cookie-list
	// The user agent SHOULD sort the cookie-list in the following order:
	// *  Cookies with longer paths are listed before cookies with shorter paths.
	// *  Among cookies that have equal-length path fields, cookies with earlier creation-times are listed
	// before cookies with later creation-times.

	// fill in cookies from cookieJar
	std::map<std::string, httpcookie_t> oldCookies;

	const char *cookieJar = sb->getBufStart();
	int32_t cookieJarLen = sb->length();

	const char *lineStartPos = cookieJar;
	const char *lineEndPos = NULL;
	while ((lineEndPos = (const char*)memchr(lineStartPos, '\n', cookieJarLen - (lineStartPos - cookieJar))) != NULL) {
		const char *currentPos = lineStartPos;
		const char *tabPos = NULL;
		unsigned fieldCount = 0;

		httpcookie_t cookie = {};
		while (fieldCount < 5 && (tabPos = (const char*)memchr(currentPos, '\t', lineEndPos - currentPos)) != NULL) {
			switch (fieldCount) {
				case 0:
					// domain
					cookie.m_domain = currentPos;
					cookie.m_domainLen = tabPos - currentPos;
					break;
				case 1:
					// flag
					if (memcmp(currentPos, "TRUE", 4) != 0) {
						cookie.m_defaultDomain = true;
					}
					break;
				case 2: {
					// path
					cookie.m_path = currentPos;
					cookie.m_pathLen = tabPos - currentPos;
				} break;
				case 3:
					// secure
					cookie.m_secure = (memcmp(currentPos, "TRUE", 4) == 0);
					break;
				case 4:
					// expiration
					break;
			}

			currentPos = tabPos + 1;
			++fieldCount;
		}

		cookie.m_cookie = currentPos;
		cookie.m_cookieLen = lineEndPos - currentPos;

		const char *equalPos = (const char *)memchr(cookie.m_cookie, '=', cookie.m_cookieLen);
		if (equalPos) {
			cookie.m_nameLen = equalPos - cookie.m_cookie;

			oldCookies[std::string(cookie.m_cookie, cookie.m_nameLen)] = cookie;
		}

		lineStartPos = lineEndPos + 1;
	}
	// we don't need to care about the last line (we always end on \n)

	SafeBuf newCookieJar;

	// add old cookies
	for (auto &pair : oldCookies) {
		if (m_cookies.find(pair.first) == m_cookies.end()) {
			addCookie(pair.second, *currentUrl, &newCookieJar);
		}
	}

	// add new cookies
	for (auto &pair : m_cookies) {
		addCookie(pair.second, *currentUrl, &newCookieJar);
	}

	newCookieJar.nullTerm();

	// replace old with new
	sb->reset();
	sb->safeMemcpy(&newCookieJar);
	sb->nullTerm();

	return true;
}

bool HttpMime::addCookieHeader(const char *cookieJar, const char *url, SafeBuf *sb) {
	Url tmpUrl;
	tmpUrl.set(url);

	SafeBuf tmpSb;

	size_t cookieJarLen = strlen(cookieJar);

	const char *lineStartPos = cookieJar;
	const char *lineEndPos = NULL;
	while ((lineEndPos = (const char*)memchr(lineStartPos, '\n', cookieJarLen - (lineStartPos - cookieJar))) != NULL) {
		const char *currentPos = lineStartPos;
		const char *tabPos = NULL;
		unsigned fieldCount = 0;

		bool skipCookie = false;
		const char *domain = NULL;
		int32_t domainLen = 0;
		while (fieldCount < 5 && (tabPos = (const char*)memchr(currentPos, '\t', lineEndPos - currentPos)) != NULL) {
			switch (fieldCount) {
				case 0:
					// domain
					domain = currentPos;
					domainLen = tabPos - currentPos;
					break;
				case 1:
					// flag
					if (memcmp(currentPos, "TRUE", 4) == 0) {
						// allow subdomain
						if (tmpUrl.getHostLen() >= domainLen) {
							if (!endsWith(tmpUrl.getHost(), tmpUrl.getHostLen(), domain, domainLen)) {
								// doesn't end with domain - ignore cookie
								skipCookie = true;
								break;
							}
						} else {
							skipCookie = true;
							break;
						}
					} else {
						// only specific domain
						if (tmpUrl.getHostLen() != domainLen || strncasecmp(domain, tmpUrl.getHost(), domainLen) != 0) {
							// non-matching domain - ignore cookie
							skipCookie = true;
							break;
						}
					}
					break;
				case 2: {
					// path
					const char *path = currentPos;
					int32_t pathLen = tabPos - currentPos;
					if (strncasecmp(path, tmpUrl.getPath(), pathLen) == 0) {
						if (tmpUrl.getPathLen() != pathLen) {
							if (path[pathLen - 1] != '/' && tmpUrl.getPath()[tmpUrl.getPathLen() - 1] != '/') {
								// non-matching path - ignore cookie
								skipCookie = true;
								break;
							}
						}
					} else {
						// non-matching path - ignore cookie
						skipCookie = true;
						break;
					}
				} break;
				case 3:
					// secure

					break;
				case 4:
					// expiration

					break;
			}

			currentPos = tabPos + 1;
			++fieldCount;
		}

		if (!skipCookie) {
			tmpSb.safeMemcpy(currentPos, lineEndPos - currentPos);
			tmpSb.pushChar(';');
		}

		lineStartPos = lineEndPos + 1;
	}
	// we don't need to care about the last line (we always end on \n)

	if (tmpSb.length() > 0) {
		sb->safeStrcpy("Cookie: ");
		sb->safeMemcpy(&tmpSb);
		sb->safeStrcpy("\r\n");
	}

	return true;
}

void HttpMime::print() const {
	logf(LOG_TRACE, "HttpMime info");
	logf(LOG_TRACE, "Cookies :");
	int i = 0;
	for (auto &pair : m_cookies) {
		print(pair.second, i++);
	}
}

void HttpMime::print(const httpcookie_t &cookie, int count) {
	logf(LOG_TRACE, "\tcookie #%d :", count);
	logf(LOG_TRACE, "\t\tname     : %.*s", static_cast<int>(cookie.m_nameLen), cookie.m_cookie);
	logf(LOG_TRACE, "\t\tvalue    : %.*s", static_cast<int>(cookie.m_cookieLen - cookie.m_nameLen - 1), cookie.m_cookie + cookie.m_nameLen + 1);
	logf(LOG_TRACE, "\t\tpath     : %.*s", static_cast<int>(cookie.m_pathLen), cookie.m_path);
	logf(LOG_TRACE, "\t\tdomain   : %.*s", static_cast<int>(cookie.m_domainLen), cookie.m_domain);
	logf(LOG_TRACE, "\t\tsecure   : %s", cookie.m_secure ? "true" : "false");
	logf(LOG_TRACE, "\t\thttponly : %s", cookie.m_httpOnly ? "true" : "false");
}
