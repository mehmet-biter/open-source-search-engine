// Matt Wells, copyright Mar 2001

// . a class for parsing urls
// . used by many other classes

#ifndef GB_URL_H
#define GB_URL_H

#define MAX_URL_LEN 1024

// where should i put this #define? for now i'll keep it here
#define MAX_COLL_LEN  64

#include "ip.h"      // atoip ( s,len)
#include "TitleRecVersion.h"
#include <cstddef>
#include <string.h>

class SafeBuf;

char *getPathFast  ( char *url );
char *getTLDFast   ( char *url , int32_t *tldLen  , bool hasHttp = true ) ;
char *getDomFast   ( char *url , int32_t *domLen  , bool hasHttp = true ) ;
bool  hasSubdomain ( char *url );
char *getHostFast  ( char *url , int32_t *hostLen , int32_t *port = NULL ) ;

// . returns the host of a normalized url pointed to by "s"
// . i.e. "s" must start with the protocol (i.e. http:// or https:// etc.)
// . used by Links.cpp for fast parsing and SiteGetter.cpp too
char *getHost ( char *s , int32_t *hostLen ) ;

// . returns the scheme of a normalized url pointed to by "s"
// . i.e. "s" must start with the protocol (i.e. http:// or https:// etc.)
// . used by SiteGetter.cpp too
char *getScheme( char *s , int32_t *hostLen );


// . get the path end of a normalized url
// . used by SiteGetter.cpp
// . if num==0 just use "www.xyz.com" as site (the hostname)
// . if num==1 just use "www.xyz.com/foo/" as site
char *getPathEnd ( char *s , int32_t num );

int32_t getPathDepth ( char *s , bool hasHttp );

class Url {
public:
	// set from another Url, does a copy
	void set( Url *url ) {
		if ( ! url ) {
			reset();
			return;
		}

		set( url->getUrl(), url->getUrlLen(), false, false );
	}

	void set( const char *s ) {
		set( s, strlen( s ), false, false );
	}

	void set( const char *s, int32_t len ) {
		set( s, len, false, false );
	}

	void set( const char *s, int32_t len, bool addWWW, bool stripParams, int32_t titledbVersion = TITLEREC_CURRENT_VERSION ) {
		set( s, len, addWWW, stripParams, false, false, titledbVersion );
	}

	void set( Url *baseUrl, const char *s, int32_t len ) {
		set( baseUrl, s, len, false, false, false, false );
	}

	// . "s" must be an ENCODED url
	void set( Url *baseUrl, const char *s, int32_t len, bool addWWW, bool stripParams, bool stripPound,
	          bool stripCommonFile, int32_t titledbVersion = TITLEREC_CURRENT_VERSION );

	void print  ();
	void reset  ();

	// compare another url to us
	bool equals ( const Url *u ) const {
		return ( m_ulen == u->m_ulen && strcmp( m_url, u->m_url ) == 0 );
	}

	// is the url's hostname actually in ip in disguise ("a.b.c.d")
	bool isIp() const;

	bool isRoot() const;
	bool isCgi() const { return m_query; }

	//returns True if the extension is in the list of 
	//badExtensions - extensions not to be parsed
	bool hasNonIndexableExtension(int32_t xxx) const;
	bool isDomainUnwantedForIndexing() const;
	bool isPathUnwantedForIndexing() const;

	// is it http://rpc.weblogs.com/shortChanges.xml, etc.?
	bool isPingServer ( ) const;

	int32_t getSubUrlLen(int32_t i) const;
	int32_t getSubPathLen(int32_t i) const;

	int32_t getPort() const { return m_port; }
	int32_t getPortLen() const { return m_portLen; }

	int32_t getDefaultPort() const { return m_defPort; }

	int32_t getIp() const { return m_ip; }

	char       *getUrl()       { return m_url; }
	const char *getUrl() const { return m_url; }
	int32_t getUrlLen() const { return m_ulen; }

	const char *getScheme() { return m_scheme; }
	int32_t getSchemeLen() const { return m_slen; }

	char       *getHost()       { return m_host; }
	const char *getHost() const { return m_host; }
	int32_t getHostLen() const { return m_hlen; }

	char       *getDomain()       { return m_domain; }
	const char *getDomain() const { return m_domain; }
	int32_t getDomainLen() const { return m_dlen; }

	char       *getTLD()       { return m_tld; }
	const char *getTLD() const { return m_tld; }
	int32_t getTLDLen() { return m_tldLen; }

	// w/o the tld
	const char *getMidDomain() { return m_domain; }
	int32_t getMidDomainLen() { return m_mdlen; }

	char       *getPath()       { return m_path; }
	const char *getPath() const { return m_path; }
	int32_t getPathLen() const { return m_plen; }
	char *getPathEnd() { return m_path + m_plen; }

	char       *getFilename()       { return m_filename; }
	const char *getFilename() const { return m_filename; }
	int32_t getFilenameLen() { return m_flen; }

	const char *getExtension() { return m_extension; }
	int32_t getExtensionLen() const { return m_elen; }

	char       *getQuery()       { return m_query; }
	const char *getQuery() const { return m_query; }
	int32_t getQueryLen() const { return m_qlen; }

	int32_t  getPathLenWithCgi () const {
		return m_query ? (m_plen + 1 + m_qlen) : m_plen;
	}

	bool  isHttp() const { return ( m_defPort == 80 ); }
	bool  isHttps() const { return ( m_defPort == 443 ); }

	// used by buzz i guess
	int32_t      getUrlHash32    ( ) const;
	int32_t      getHostHash32   ( ) const;
	int32_t      getDomainHash32 ( ) const;

	// if url is xyz.com then get hash of www.xyz.com
	int32_t getHash32WithWWW ( ) const;

	int64_t getUrlHash64    ( ) const;
	int64_t getHostHash64   ( ) const;
	int64_t getDomainHash64 ( ) const;

	int64_t getUrlHash48    ( ) const {
		return getUrlHash64() & 0x0000ffffffffffffLL;
	}

	bool hasMediaExtension() const;
	bool hasScriptExtension() const;
	bool hasXmlExtension() const;
	bool hasJsonExtension() const;

	// count the path components (root url as 0 path components)
	int32_t  getPathDepth ( bool countFilename ) const;

	// is our hostname "www" ?
	bool isHostWWW ( ) const;

	bool hasSubdomain() const { return (m_dlen != m_hlen); }

	// is it xxx.com/* or www.xxx.com/* (CAUTION: www.xxx.yyy.com)
	bool isSimpleSubdomain() const;

	// is the url a porn/spam url?
	bool isSpam() const;

	// . detects crazy repetetive urls like this:
	//   http://www.pittsburghlive.com:8000/x/tribune-review/opinion/
	//   steigerwald/letters/send/archive/letters/send/archive/bish/
	//   archive/bish/letters/bish/archive/lettes/send/archive/letters/...
	// . The problem is they use a relative href link on the page when they
	//   should us an absolute and the microsoft web server will still
	//   give the content they meant to give!
	// . this is called by Msg14.cpp to not even spider such urls, and we
	//   also have some even better detection logic in Links.cpp which
	//   is probably more accurate than this function.
	bool isLinkLoop() const;

	static char* getDisplayUrl( const char* url, SafeBuf* sb );

private:
	void set( const char *s, int32_t len, bool addWWW, bool stripParams, bool stripPound, bool stripCommonFile,
	          int32_t titledbVersion );

	bool isSpam ( char *s , int32_t slen ) const;

	// the normalized url
	char m_url[MAX_URL_LEN];
	int32_t m_ulen;

	// points into "url" (http, ftp, mailto, ...)(all lowercase)
	char *m_scheme;
	int32_t m_slen;

	// points into "url" (a.com, www.yahoo.com, 192.0.2.1, ...)(allLowercase)
	char *m_host;
	int32_t m_hlen;

	// it's 0 if we don't have one
	int32_t m_ip;

	// points into "url" (/  /~mwells/  /a/b/ ...) (always ends in /)
	char *m_path;
	int32_t m_plen;

	// points into "url" (a=hi+there, ...)
	char *m_query;
	int32_t m_qlen;

	// points into "url" (html, mpg, wav, doc, ...)
	char *m_extension;
	int32_t m_elen;

	// (a.html NULL index.html) (can be NULL)
	char *m_filename;
	int32_t m_flen;

	char *m_domain;
	int32_t m_dlen;

	char *m_tld;
	int32_t m_tldLen;

	// char *m_midDomain equals m_domain
	int32_t m_mdlen;

	// (80, 8080, 8000, ...)
	int32_t m_port;
	int32_t m_defPort;
	int32_t m_portLen;

	// anchor
	char *m_anchor;
	int32_t m_anchorLen;
};

#endif // GB_URL_H
