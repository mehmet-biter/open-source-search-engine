// Matt Wells, copyright Jun 2000

// . class to parse an html MIME header

#ifndef GB_HTTPMIME_H
#define GB_HTTPMIME_H

// convert text/html to CT_HTML for instance
// convert application/json to CT_JSON for instance
int32_t getContentTypeFromStr ( const char *s ) ;

const char *extensionToContentTypeStr2 ( const char *ext , int32_t elen ) ;

#include <time.h>

time_t atotime    ( const char *s ) ;
time_t atotime1   ( const char *s ) ;

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
#include "Url.h"

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

	char *getContent() { return m_content; }
	int32_t getContentLen() const { return m_contentLen; }

	int32_t getContentType() { return m_contentType; }

	Url *getLocationUrl() { return &m_locUrl; }

	// new stuff for Msg13.cpp to use
	char *getLocationField() { return m_locationField; }
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

	bool addCookiesIntoBuffer ( class SafeBuf *sb ) ;

	char *getMime() { return m_buf; }
	// does this include the last \r\n\r\n? yes!
	int32_t getMimeLen() const { return m_mimeLen; }

	char *getCharset() { return m_charset; }
	int32_t getCharsetLen() const { return m_charsetLen; }

	int32_t getContentEncoding() const { return m_contentEncoding; }
	char *getContentEncodingPos() { return m_contentEncodingPos; }
	char *getContentLengthPos() { return m_contentLengthPos; }
	char *getContentTypePos() { return m_contentTypePos; }

	// convert a file extension like "gif" to "images/gif"
	const char *getContentTypeFromExtension ( const char *ext ) ;
	const char *getContentTypeFromExtension ( const char *ext , int32_t elen ) ;

private:
	// . sets m_status, m_contentLen , ...
	// . we need "url" to set m_locUrl if it's a relative redirect
	bool parse ( char *mime , int32_t mimeLen , Url *url );

	// compute length of a possible mime starting at "buf"
	int32_t getMimeLen(char *buf, int32_t bufLen);

	// converts a string contentType like "text/html" to a int32_t
	int32_t   getContentTypePrivate ( char *s ) ;

	// used for bz2, gz files
	const char *getContentEncodingFromExtension ( const char *ext ) ;

	// these are set by calling set() above
	int32_t m_status;
	char *m_content;
	int32_t m_contentLen;
	int32_t m_contentType;
	Url m_locUrl;

	char *m_locationField;
	int32_t m_locationFieldLen;

	const char *m_mime;

	// buf used to hold a mime we create
	char m_buf[1024];
	int32_t m_mimeLen;

	int32_t m_contentEncoding;
	char *m_contentEncodingPos;
	char *m_contentLengthPos;
	char *m_contentTypePos;

	// Content-Type: text/html;charset=euc-jp  // japanese (euc-jp)
	// Content-Type: text/html;charset=gb2312  // chinese (gb2312)
	char *m_charset;
	int32_t  m_charsetLen;

	char *m_firstCookie;
	
	const char *m_cookie;
	int32_t  m_cookieLen;
};

#endif // GB_HTTPMIME_H
