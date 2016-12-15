// Matt Wells, copyright Jun 2000

// . class to parse an html MIME header

#ifndef GB_HTTPMIME_H
#define GB_HTTPMIME_H

// convert text/html to CT_HTML for instance
// convert application/json to CT_JSON for instance
int32_t getContentTypeFromStr(const char *s, size_t slen);

const char *extensionToContentTypeStr2 ( const char *ext , int32_t elen ) ;

// the various content types
#define CT_UNKNOWN 0
#define CT_HTML    1
#define CT_TEXT    2
#define CT_XML     3
#define CT_PDF     4
#define CT_DOC     5
#define CT_XLS     6
#define CT_PPT     7
#define CT_PS      8
// images
#define CT_GIF     9
#define CT_JPG    10
#define CT_PNG    11
#define CT_TIFF   12
#define CT_BMP    13
#define CT_JS     14
#define CT_CSS    15
#define CT_JSON   16
#define CT_IMAGE  17
#define CT_STATUS 18 // an internal type indicating spider reply
#define CT_GZ     19
#define CT_ARC    20
#define CT_WARC   21

#define ET_IDENTITY 0
#define ET_GZIP 1
#define ET_COMPRESS 2
#define ET_DEFLATE 3


extern const char * const g_contentTypeStrings[];

#include <time.h>   // time_t mktime()
#include <map>
#include <string>
#include "Url.h"

class SafeBuf;

class HttpMime {
public:
	HttpMime();

	bool init();
	void reset();

	// . returns false and sets errno if could not get a valid mime
	// . just copies bits and pieces so you can free "mime" whenever
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool set ( char *httpReply , int32_t replyLen , Url *url );

	void setContentType(int32_t t) { m_contentType = t; }
	void setHttpStatus(int32_t status) { m_status = status; }
	void setBufLen(int32_t bufLen) { m_mimeLen = bufLen; }

	// http status: 404, 200, etc.
	int32_t getHttpStatus() const { return m_status; }

	char *getContent() { return const_cast<char*>(m_content); }
	int32_t getContentLen() const { return m_contentLen; }

	int32_t getContentType() { return m_contentType; }

	Url *getLocationUrl() { return &m_locUrl; }

	// new stuff for Msg13.cpp to use
	const char *getLocationField() { return m_locationField; }
	int32_t getLocationFieldLen() const { return m_locationFieldLen; }

	// . used to create a mime
	// . if bytesToSend is < 0 that means send totalContentLen (all doc)
	// . if lastModified is 0 we take the current time and use that
	// . cacheTime is how long for browser to cache this page in seconds
	// . a cacheTime of -2 means do not cache at all
	// . a cacheTime of -1 means do not cache when moving forward,
	//   but when hitting the back button, serve cached results
	// . a cache time of 0 means use local caching rules
	// . any other cacheTime is an explicit time to cache the page for
	// . httpStatus of -1 means to auto determine
	void makeMime(int32_t totalContentLen,
	              int32_t cacheTime,
	              time_t lastModified,
	              int32_t offset,
	              int32_t bytesToSend,
	              const char *ext,
	              bool POSTReply,
	              const char *contentType,
	              const char *charset,
	              int32_t httpStatus,
	              const char *cookie);

	// make a redirect mime
	void makeRedirMime ( const char *redirUrl , int32_t redirUrlLen );

	bool addToCookieJar(Url *currentUrl, SafeBuf *sb);

	static bool addCookieHeader(const char *cookieJar, const char *url, SafeBuf *sb);

	char *getMime() { return m_buf; }
	// does this include the last \r\n\r\n? yes!
	int32_t getMimeLen() const { return m_mimeLen; }

	const char *getCharset() { return m_charset; }
	int32_t getCharsetLen() const { return m_charsetLen; }

	int32_t getContentEncoding() const { return m_contentEncoding; }
	const char *getContentEncodingPos() { return m_contentEncodingPos; }
	const char *getContentLengthPos() { return m_contentLengthPos; }
	const char *getContentTypePos() { return m_contentTypePos; }

	// convert a file extension like "gif" to "images/gif"
	const char *getContentTypeFromExtension ( const char *ext ) ;
	const char *getContentTypeFromExtension ( const char *ext , int32_t elen ) ;

	void print() const;

protected:
	struct httpcookie_t {
		const char *m_cookie;
		size_t m_cookieLen;

		size_t m_nameLen;

		bool m_defaultDomain;
		const char *m_domain;
		size_t m_domainLen;

		const char *m_path;
		size_t m_pathLen;

		bool m_secure;
		bool m_httpOnly;

		bool m_expired;
	};

	bool getNextLine();
	bool getField(const char **field, size_t *fieldLen);
	bool getValue(const char **value, size_t *valueLen);
	bool getAttribute(const char **attribute, size_t *attributeLen, const char **attributeValue, size_t *attributeValueLen);

	const char* getCurrentLine() const { return m_currentLine; }
	int32_t getCurrentLineLen() const { return m_currentLineLen; }

	// compute length of a possible mime starting at "buf"
	size_t getMimeLen(char *buf, size_t bufLen);

	void setMime(const char *mime) { m_mime = mime; }
	void setMimeLen(int32_t mimeLen) { m_mimeLen = mimeLen; }
	void setContent(const char *content) { m_content = content; }

	void setCurrentTime(time_t currentTime) {
		m_fakeCurrentTime = true;
		m_currentTime = currentTime;
	}

	const std::map<std::string, httpcookie_t>& getCookies() { return m_cookies; }

	static bool parseCookieDate(const char *value, size_t valueLen, time_t *time);

private:
	// . sets m_status, m_contentLen , ...
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool parse(char *mime, int32_t mimeLen, Url *url);

	bool parseLocation(const char *field, size_t fieldLen, Url *baseUrl);
	bool parseSetCookie(const char *field, size_t fieldLen);
	bool parseContentType(const char *field, size_t fieldLen);
	bool parseContentLength(const char *field, size_t fieldLen);
	bool parseContentEncoding(const char *field, size_t fieldLen);

	// converts a string contentType like "text/html" to a int32_t
	int32_t getContentTypePrivate(const char *s, size_t slen);

	// used for bz2, gz files
	const char *getContentEncodingFromExtension ( const char *ext ) ;

	static void addCookie(const httpcookie_t &cookie, const Url &currentUrl, SafeBuf *cookieJar);

	static void print(const httpcookie_t &cookie, int count = 0);

	const char *m_currentLine;
	size_t m_currentLineLen;
	size_t m_nextLineStartPos;
	size_t m_valueStartPos;
	size_t m_attributeStartPos;

	time_t m_currentTime;
	bool m_fakeCurrentTime;

	// these are set by calling set() above
	int32_t m_status;
	const char *m_content;
	int32_t m_contentLen;
	int32_t m_contentType;
	Url m_locUrl;

	const char *m_locationField;
	int32_t m_locationFieldLen;

	const char *m_mime;

	// buf used to hold a mime we create
	char m_buf[1024];
	size_t m_mimeLen;

	int32_t m_contentEncoding;
	const char *m_contentEncodingPos;
	const char *m_contentLengthPos;
	const char *m_contentTypePos;

	// Content-Type: text/html;charset=euc-jp  // japanese (euc-jp)
	// Content-Type: text/html;charset=gb2312  // chinese (gb2312)
	const char *m_charset;
	int32_t  m_charsetLen;

	std::map<std::string, httpcookie_t> m_cookies;
};

#endif // GB_HTTPMIME_H
