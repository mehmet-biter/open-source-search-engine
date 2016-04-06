// Matt Wells, copyright Mar 2001

// . a class for parsing urls
// . used by many other classes

#ifndef GB_URL_H
#define GB_URL_H

#define MAX_URL_LEN 1024

// where should i put this #define? for now i'll keep it here
#define MAX_COLL_LEN  64

#include "ip.h"      // atoip ( s,len)
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
	void set( Url *url );

	void set( const char *s ) {
		return set( s, strlen( s ), false, false, false );
	}

	void set( const char *s, int32_t len ) {
		return set( s, len, false, false, false );
	}

	void set( const char *s, int32_t len, bool addWWW, bool stripSessionIds, bool stripTrackingParams ) {
		return set( s, len, addWWW, stripSessionIds, false, false, stripTrackingParams );
	}

	void set( Url *baseUrl, const char *s, int32_t len ) {
		return set( baseUrl, s, len, false, false, false, false, false);
	}

	// . "s" must be an ENCODED url
	void set( Url *baseUrl, const char *s, int32_t len, bool addWWW, bool stripSessionIds, bool stripPound,
	          bool stripCommonFile, bool stripTrackingParams );

	void print  ();
	void reset  ();

	char isSessionId ( const char *hh ) ;

	// compare another url to us
	bool equals ( Url *u ) {
		return ( m_ulen == u->m_ulen && strcmp( m_url, u->m_url ) == 0 );
	};

	// is the url's hostname actually in ip in disguise ("a.b.c.d")
	bool isIp   (); 

	bool isRoot              ();
	bool isCgi               () { return m_query ; };

	//returns True if the extension is in the list of 
	//badExtensions - extensions not to be parsed
	bool hasNonIndexableExtension(int32_t xxx);
	bool isDomainUnwantedForIndexing();
	bool isPathUnwantedForIndexing();

	// is it http://rpc.weblogs.com/shortChanges.xml, etc.?
	bool isPingServer ( ) ;

	int32_t getSubUrlLen        (int32_t i);
	int32_t getSubPathLen       (int32_t i);

	int32_t getPort             () { return m_port;};
	int32_t getIp               () { return m_ip; };

	char *getUrl         () { return m_url;};
	char *getScheme      () { return m_scheme;};
	char *getHost        () { return m_host;};
	char *getDomain      () { return m_domain;};
	char *getTLD         () { return m_tld; };
	char *getMidDomain   () { return m_domain; }; // w/o the tld
	char *getPath        () { return m_path;};
	char *getFilename    () { return m_filename;};
	char *getExtension   () { return m_extension;};
	char *getQuery       () { return m_query;};

	int32_t  getUrlLen         () { return m_ulen;};
	int32_t  getSchemeLen      () { return m_slen;};
	int32_t  getHostLen        () { return m_hlen;};
	int32_t  getDomainLen      () { return m_dlen;};
	int32_t  getPathLen        () { return m_plen;};
	char *getPathEnd        () { return m_path + m_plen; };
	int32_t  getFilenameLen    () { return m_flen;};
	int32_t  getExtensionLen   () { return m_elen;};
	int32_t  getQueryLen       () { return m_qlen;};
	int32_t  getTLDLen         () { return m_tldLen; };
	int32_t  getMidDomainLen   () { return m_mdlen;};
	int32_t  getPortLen        () { return m_portLen;};

	int32_t  getPathLenWithCgi () {
		if ( ! m_query )
			return m_plen;

		return m_plen + 1 + m_qlen;
	}

	bool  isHttp            () { 
		if ( m_ulen  < 4 ) return false;
		if ( m_slen != 4 ) return false;
		if ( m_scheme[0] != 'h' ) return false;
		if ( m_scheme[1] != 't' ) return false;
		if ( m_scheme[2] != 't' ) return false;
		if ( m_scheme[3] != 'p' ) return false;
		return true;
	}

	bool  isHttps           () { 
		if ( m_ulen  < 5 ) return false;
		if ( m_slen != 5 ) return false;
		if ( m_scheme[0] != 'h' ) return false;
		if ( m_scheme[1] != 't' ) return false;
		if ( m_scheme[2] != 't' ) return false;
		if ( m_scheme[3] != 'p' ) return false;
		if ( m_scheme[4] != 's' ) return false;
		return true;
	}

	// used by buzz i guess
	int32_t      getUrlHash32    ( ) ;
	int32_t      getHostHash32   ( ) ;
	int32_t      getDomainHash32 ( ) ;

	// if url is xyz.com then get hash of www.xyz.com
	int32_t getHash32WithWWW ( );

	int64_t getUrlHash64    ( ) ;
	int64_t getHostHash64   ( ) ;
	int64_t getDomainHash64   ( ) ;

	int64_t getUrlHash48    ( ) {
		return getUrlHash64() & 0x0000ffffffffffffLL;
	}

	bool hasMediaExtension();
	bool hasScriptExtension();
	bool hasXmlExtension();
	bool hasJsonExtension();

	// count the path components (root url as 0 path components)
	int32_t  getPathDepth ( bool countFilename ); // = false );

	// is our hostname "www" ?
	bool isHostWWW ( ) ;

	bool hasSubdomain() { return (m_dlen != m_hlen); };

	// is it xxx.com/* or www.xxx.com/* (CAUTION: www.xxx.yyy.com)
	bool isSimpleSubdomain();

	// is the url a porn/spam url?
	bool isSpam();

	// this is private
	bool isSpam ( char *s , int32_t slen ) ;

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
	bool isLinkLoop();

	static char* getDisplayUrl( const char* url, SafeBuf* sb );

private:
	void set( const char *s, int32_t len, bool addWWW, bool stripSessionIds, bool stripPound, bool stripCommonFile,
	          bool stripTrackingParams );

	char    m_url[MAX_URL_LEN]; // the normalized url
	int32_t    m_ulen;

	// points into "url" (http, ftp, mailto, ...)(all lowercase)
	char   *m_scheme;           
	int32_t    m_slen;

	// points into "url" (a.com, www.yahoo.com, 192.0.2.1, ...)(allLowercase)
	char   *m_host;             
	int32_t    m_hlen;

	// it's 0 if we don't have one
	int32_t    m_ip;  

	// points into "url" (/  /~mwells/  /a/b/ ...) (always ends in /)
	char   *m_path;             
	int32_t    m_plen;

	// points into "url" (a=hi+there, ...)
	char   *m_query;            
	int32_t    m_qlen;

	// points into "url" (html, mpg, wav, doc, ...)
	char   *m_extension;        
	int32_t    m_elen;

	// (a.html NULL index.html) (can be NULL)
	char   *m_filename;         
	int32_t    m_flen;

	char   *m_domain;
	int32_t    m_dlen;

	char   *m_tld;
	int32_t    m_tldLen;

	// char *m_midDomain equals m_domain
	int32_t    m_mdlen;

	// (80, 8080, 8000, ...)
	int32_t    m_port;             
	int32_t    m_defPort;
	int32_t    m_portLen;

	// anchor
	char   *m_anchor;
	int32_t    m_anchorLen;
};

#endif // GB_URL_H
