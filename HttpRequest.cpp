#include "gb-include.h"

#include "HttpRequest.h"
#include "ip.h"
#include "Conf.h"
#include "Process.h"
#include "Hostdb.h"
#include <stdlib.h>
#include <float.h>


HttpRequest::HttpRequest () {
	m_cgiBuf = NULL;
	m_cgiBuf2 = NULL;
	reset();
}


HttpRequest::~HttpRequest() { 
	reset();
}


char HttpRequest::getReplyFormat() const {
	if ( m_replyFormatValid ) return m_replyFormat;

	const char *formatStr = getString("format");

	char format = -1;//FORMAT_HTML;

	// what format should search results be in? default is html
	if ( formatStr ) {
		if ( strcmp( formatStr, "html" ) == 0 ) {
			format = FORMAT_HTML;
		} else if ( strcmp( formatStr, "json" ) == 0 ) {
			format = FORMAT_JSON;
		} else if ( strcmp( formatStr, "xml" ) == 0 ) {
			format = FORMAT_XML;
		} else if ( strcmp( formatStr, "csv" ) == 0 ) {
			format = FORMAT_CSV;
		} else if ( strcmp( formatStr, "iframe" ) == 0 ) {
			format = FORMAT_WIDGET_IFRAME;
		} else if ( strcmp( formatStr, "ajax" ) == 0 ) {
			format = FORMAT_WIDGET_AJAX;
		} else if ( strcmp( formatStr, "append" ) == 0 ) {
			format = FORMAT_WIDGET_APPEND;
		}
	} else {
		// support old api &xml=1 to mean &format=xml
		if (getLong( "xml", 0 )) {
			format = FORMAT_XML;
		} else if (getLong( "json", 0 )) {
			format = FORMAT_JSON;
		} else if (getLong( "csv", 0 )) {
			format = FORMAT_CSV;
		} else if (getLong( "iframe", 0 )) {
			format = FORMAT_WIDGET_IFRAME;
		} else if (getLong( "ajax", 0 )) {
			format = FORMAT_WIDGET_AJAX;
		} else if (getLong( "append", 0 )) {
			format = FORMAT_WIDGET_APPEND;
		}
	}

	// default to html
	if ( format == -1 ) {
		format = FORMAT_HTML;
	}

	m_replyFormat = format;
	m_replyFormatValid = true;

	return format;
}


void HttpRequest::reset() {
	m_numFields = 0;
	m_replyFormatValid = false;
	//if ( m_cgiBuf ) mfree ( m_cgiBuf , m_cgiBufMaxLen , "HttpRequest");
	m_cgiBufLen    = 0;
	m_cgiBuf       = NULL;
	m_cgiBufMaxLen = 0;
	//m_buf[0] = '\0';
	//m_bufLen = 0;
	m_path   = NULL;
	m_plen   = 0;
	m_ucontent    = NULL;
	m_ucontentLen = 0;
	m_cookiePtr = NULL;
	m_cookieLen = 0;
	m_userIP = 0;
	m_reqBufValid = false;
	m_reqBuf.purge();

	if (m_cgiBuf2) {
		mfree(m_cgiBuf2, m_cgiBuf2Size, "extraParms");
		m_cgiBuf2 = NULL;
	}
	m_cgiBuf2Size = 0;

	// Coverity
	m_replyFormat = 0;
	m_isSquidProxyRequest = false;
	m_squidProxiedUrl = NULL;
	m_squidProxiedUrlLen = 0;
	m_requestType = 0;
	memset(m_filename, 0, sizeof(m_filename));
	m_filenameLen = 0;
	m_origUrlRequest = NULL;
	m_origUrlRequestLen = 0;
	memset(m_host, 0, sizeof(m_host));
	m_hostLen = 0;
	m_isLocal = false;
	m_fileOffset = 0;
	m_fileSize = 0;
	memset(m_fields, 0, sizeof(m_fields));
	memset(&m_fieldLens, 0, sizeof(m_fieldLens));
	memset(m_fieldValues, 0, sizeof(m_fieldValues));
	m_isSSL = false;
	memset(m_redir, 0, sizeof(m_redir));
	m_redirLen = 0;
	memset(m_ref, 0, sizeof(m_ref));
	m_refLen = 0;
	memset(m_userAgent, 0, sizeof(m_userAgent));
	m_metaCookie = NULL;
}


// returns false with g_errno set on error
bool HttpRequest::copy(const HttpRequest *r) {
	//highly suspect code here!
	//todo: make a normal and much safer operator=()
	SafeBuf raw_tmp_copy;
	memcpy(&raw_tmp_copy, &m_reqBuf, sizeof(raw_tmp_copy));
	gbmemcpy ( this , r , sizeof(HttpRequest) );
	memcpy(&m_reqBuf,&raw_tmp_copy,sizeof(m_reqBuf));
	
	// we copy it and update the ptrs below
	m_reqBuf.purge();
	if ( ! m_reqBuf.safeMemcpy ( &r->m_reqBuf ) )
		return false;

	// fix ptrs
	const char *sbuf = r->m_reqBuf.getBufStart();
	char *dbuf =    m_reqBuf.getBufStart();
	for ( int32_t i = 0 ; i < m_numFields ; i++ ) {
		m_fields     [i] = dbuf + (r->m_fields     [i] - sbuf);
		m_fieldValues[i] = NULL;
		if ( r->m_fieldValues[i] )
			m_fieldValues[i] = dbuf + (r->m_fieldValues[i] - sbuf);
	}
	m_cookiePtr  = dbuf + (r->m_cookiePtr  - sbuf );
	m_metaCookie = dbuf + (r->m_metaCookie - sbuf );
	m_ucontent   = dbuf + (r->m_ucontent   - sbuf );
	m_path       = dbuf + (r->m_path       - sbuf );
	m_cgiBuf     = dbuf + (r->m_cgiBuf     - sbuf );
	// not supported yet. we'd have to allocate it
	if ( m_cgiBuf2 ) { g_process.shutdownAbort(true); }
	return true;
}

#define RT_GET 0
#define RT_HEAD 1
#define RT_POST 2
#define RT_CONNECT 3

// TODO: ensure not sent to a proxy server since it will expect us to close it
// TODO: use chunked transfer encodings to do HTTP/1.1

// . form an HTTP request 
// . use size 0 for HEAD requests
// . use size -1 for GET whole doc requests
// . fill in your own offset/size for partial GET requests
// . returns false and sets g_errno on error
// . NOTE: http 1.1 uses Keep-Alive by default (use Connection: close to not)
bool HttpRequest::set (char *url,int32_t offset,int32_t size,time_t ifModifiedSince,
		       const char *userAgent, const char *proto, bool doPost,
		       const char *cookie, const char *additionalHeader,
		       // if posting something, how many bytes is it?
		       int32_t postContentLen ,
		       // are we sending the request through an http proxy?
		       // if so this will be non-zero
		       int32_t proxyIp ,
		       const char *proxyUsernamePwd ) {

	m_reqBufValid = false;

	int32_t hlen ;
	int32_t port = 80;
	const char *hptr = getHostFast ( url , &hlen , &port );
	char *path = getPathFast ( url );

	// . use the full url if sending to an http proxy
	// . HACK: do NOT do this if it is httpS because we end up
	//   using the http tunnel using the CONNECT cmd and the squid proxy
	//   will just forward/proxy just the entire tcp packets.
	if ( proxyIp && strncmp(url,"https://",8) != 0 ) path = url;

	char *pathEnd  = NULL;
	const char *postData = NULL;
	if ( doPost ) {
		pathEnd  = strstr(path,"?");
		if ( pathEnd ) {
			*pathEnd = '\0';
			postData = pathEnd + 1;
		}
	}

	// if no legit host
	if ( hlen <= 0 || ! hptr ) { g_errno = EBADURL; return false; }
	// sanity check. port is only 16 bits
	if ( port > (int32_t)0xffff ) { g_errno = EBADURL; return false; }
	// return false and set g_errno if url too big
	//if ( url->getUrlLen() + 400 >= MAX_REQ_LEN ) { 
	//	g_errno = EURLTOOBIG; return false;}
	// assume request type is a GET
	m_requestType = RT_GET;//0;
	// get the host NULL terminated
	char host[1024+8];
	//int32_t hlen = url->getHostLen();
	strncpy ( host , hptr , hlen );
	host [ hlen ] = '\0';
	// then port
	//uint16_t port = url->getPort();
	if ( port != 80 ) {
		sprintf ( host + hlen , ":%" PRIu32 , (uint32_t)port );
		hlen += strlen ( host + hlen );
	}
	// the if-modified-since field
	const char *ims = "";
#if 0
	char  ibuf[64];
	if ( ifModifiedSince ) {
		struct tm tm_buf;
		char buf[64];
		// NOTE: ctime appends a \n 
		snprintf(ibuf, sizeof(ibuf), "If-Modified-Since: %s UTC",
			asctime_r(gmtime_r(&ifModifiedSince,&tm_buf),buf));
		// get the length
		int32_t ilen = strlen(ibuf);
		if( ilen && ilen < (int32_t)sizeof(ibuf)-1 ) {
			// hack off \n from ctime - replace with \r\n\0
			ibuf [ ilen - 1 ] = '\r';
			ibuf [ ilen     ] = '\n';
			ibuf [ ilen + 1 ] = '\0';
			// set ims to this string
			ims = ibuf;
		}
	}
	// . until we fix if-modified-since, take it out
	// . seems like we are being called with it as true when should not be
	ims="";
#endif

	// . use one in conf file if caller did not provide
	// . this is usually Gigabot/1.0
	if ( ! userAgent ) userAgent = g_conf.m_spiderUserAgent;
	// accept only these
	const char *accept = "*/*";
	/*
		 "text/html, "
		 "text/plain, "
		 "text/xml, "
		 "application/pdf, "
		 "application/msword, "
		 "application/vnd.ms-excel, "
		 "application/mspowerpoint, "
		 "application/postscript";
	*/

	const char *cmd = "GET";
	if ( size == 0 ) cmd = "HEAD";
	if ( doPost    ) cmd = "POST";

	// crap, can't spider nyt.com if we are 1.0, so use 1.0 but also
	// note Connection: Close\r\n when making requests
	//proto = "HTTP/1.1";

	SafeBuf tmp;
	const char *up = "";
	if ( proxyUsernamePwd && proxyUsernamePwd[0] ) {
		tmp.safePrintf("Proxy-Authorization: Basic ");
		tmp.base64Encode (proxyUsernamePwd,strlen(proxyUsernamePwd));
		tmp.safePrintf("\r\n");
		up = tmp.getBufStart();
	}

	 // . now use "Accept-Language: en" to tell servers we prefer english
	 // . i removed keep-alive connection since some connections close on
	 //   non-200 ok http statuses and we think they're open since close
	 //   signal (read 0 bytes) may have been delayed
	 const char* acceptEncoding = "";
	 // the scraper is getting back gzipped search results from goog,
	 // so disable this for now
	 // i am re-enabling now for testing...
	 if(g_conf.m_gzipDownloads)
	 	 acceptEncoding = "Accept-Encoding: gzip;q=1.0\r\n";
	 // i thought this might stop wikipedia from forcing gzip on us
	 // but it did not!
	 // else
	 //	 acceptEncoding = "Accept-Encoding:\r\n";

	 // char *p = m_buf;
	 // init the safebuf to point to this buffer in our class to avoid
	 // a potential alloc
	 // m_reqBuf.setBuf ( m_buf , MAX_REQ_LEN , 0 , false, csUTF8 );
	 m_reqBuf.purge();
	 // indicate this is good
	 m_reqBufValid = true;

	 if ( size == 0 ) {
		 // 1 for HEAD requests
		 m_requestType = RT_HEAD; 
		 m_reqBuf.safePrintf (
			   "%s %s %s\r\n" 
			   "Host: %s\r\n"
			   "%s"
			   "User-Agent: %s\r\n"
			   "Connection: Close\r\n"
			   //"Connection: Keep-Alive\r\n" 
			   "Accept-Language: en\r\n"
			   //"Accept: */*\r\n\r\n" ,
			   "Accept: %s\r\n" 
			   "%s"
			   ,
				 cmd,
			   path , proto, host , 
			   ims , userAgent , accept , up );
	 }
	 else 
	 if ( size != -1 ) 
		 m_reqBuf.safePrintf (
			   "%s %s %s\r\n" 
			   "Host: %s\r\n"
			   "%s"
			   "User-Agent: %s\r\n"
			   "Connection: Close\r\n"
			   //"Connection: Keep-Alive\r\n"
			   "Accept-Language: en\r\n"
			   //"Accept: */*\r\n"
			   "Accept: %s\r\n"
			   "Range: bytes=%" PRId32"-%" PRId32"\r\n" 
			   "%s"
			   ,
				cmd,
			   path ,
			   proto ,
			   host ,
			   ims  ,
			   userAgent ,
			   accept ,
			   offset ,
			   offset + size ,
				      up);
	 else 
	 if ( offset > 0 ) 	// size is -1
		 m_reqBuf.safePrintf (
			   "%s %s %s\r\n" 
			   "Host: %s\r\n"
			   "%s"
			   "User-Agent: %s\r\n"
			   "Connection: Close\r\n"
			   //"Connection: Keep-Alive\r\n"
			   "Accept-Language: en\r\n"
			   //"Accept: */*\r\n"
			   "Accept: %s\r\n"
			   "Range: bytes=%" PRId32"-\r\n" 
			   "%s"
			   ,
				cmd,
			   path ,
			   proto ,
			   host ,
			   ims  ,
			   userAgent ,
			   accept ,
			   offset ,
				      up );
	 // Wget's request:
	 // GET / HTTP/1.0\r\nUser-Agent: Wget/1.10.2\r\nAccept: */*\r\nHost: 127.0.0.1:8000\r\nConnection: Keep-Alive\r\n\r\n
	 // firefox's request:
	 // GET /master?c=main HTTP/1.1\r\nHost: 10.5.1.203:8000\r\nUser-Agent: Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.7) Gecko/20100715 Ubuntu/10.04 (lucid) Firefox/3.6.7\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Language: en-us,en;q=0.5\r\nAccept-Encoding: gzip,deflate\r\nAccept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7\r\nKeep-Alive: 115\r\nConnection: keep-alive\r\nReferer: http://10.5.0.2:8002/qpmdw.html\r\nCookie: __utma=267617550.1103353528.1269214594.1273256655.1276103782.12; __utmz=267617550.1269214594.1.1.utmcsr=(direct)|utmccn=(direct)|utmcmd=(none); _incvi=qCffL7N8chFyJLwWrBDMbNz2Q3EWmAnf4uA; s_lastvisit=1269900225815; s_pers=%20s_getnr%3D1276103782254-New%7C1339175782254%3B%20s_nrgvo%3DNew%7C1339175782258%3B\r\n\r\n
	 else {
		 // until we fix if-modified-since, take it out
		 //ims="";
		 //userAgent = "Wget/1.10.2";
		 //userAgent = "Mozilla/5.0 (X11; U; Linux i686; en-US; rv:1.9.2.7) Gecko/20100715 Ubuntu/10.04 (lucid) Firefox/3.6.7";
		 //proto = "HTTP/1.0";
		 m_reqBuf.safePrintf (
			   "%s %s %s\r\n" 
			   "User-Agent: %s\r\n"
			   "Accept: */*\r\n" 
			   "Host: %s\r\n"
			   "%s"
			   "Connection: Close\r\n"
			   //"Connection: Keep-Alive\r\n"
			   //"Accept-Language: en\r\n"
				"%s"
			   "%s"
			   ,
			   //"Accept: %s\r\n\r\n" ,
				//"\r\n",
				cmd,
			   path ,
			   proto ,
			   userAgent ,
			   host ,
			   ims ,
			   acceptEncoding,
				      up );
			   //accept );
	 }

	 if ( additionalHeader )
		 m_reqBuf.safePrintf("%s\r\n",additionalHeader );

	 // cookie here
	 if ( cookie ) 
		 m_reqBuf.safePrintf("Cookie: %s\r\n",cookie );

	 // print content-length: if post
	 if ( postData ) {
		 // dammit... recaptcha does not work without this!!!!
		 m_reqBuf.safePrintf (
			      "Content-Type: "
			      "application/x-www-form-urlencoded\r\n");
	 }

	 // we need this if doing a post even if postData is NULL
	 if ( doPost ) {
		 int32_t contentLen = 0;
		 if ( postData ) contentLen = strlen(postData);
		 // this overrides if provided. -1 is default
		 if ( postContentLen >= 0 ) contentLen = postContentLen;
		 m_reqBuf.safePrintf ("Content-Length: %" PRId32"\r\n", contentLen );
		 m_reqBuf.safePrintf("\r\n");
		 if ( postData ) m_reqBuf.safePrintf("%s",postData);
		 // log it for debug
		 //log("captch: %s",m_buf);
	 }

	 if ( ! doPost ) { // ! postData ) {
		 m_reqBuf.safePrintf("\r\n");
	 }

	 // set m_bufLen
	 //m_bufLen = p - m_buf;//strlen ( m_buf );
	 // sanity check
	 // if ( m_bufLen + 1 > MAX_REQ_LEN ) {
	 //	 log("build: HttpRequest buf is too small.");
	 //	 g_process.shutdownAbort(true);
	 // }

	 // restore url buffer
	 if ( pathEnd ) *pathEnd = '?';

	 return true;
 }

// . parse an incoming request
// . return false and set g_errno on error
// . CAUTION: we destroy "req" by replacing it's last char with a \0
// . last char must be \n or \r for it to be a proper request anyway
bool HttpRequest::set ( char *origReq , int32_t origReqLen , TcpSocket *sock ) {
	// reset number of cgi field terms
	reset();

	 if ( ! m_reqBuf.reserve ( origReqLen + 1 ) ) {
		 log("http: failed to copy request: %s",mstrerror(g_errno));
		 return false;
	 }

	 // copy it to avoid mangling it
	 m_reqBuf.safeMemcpy ( origReq , origReqLen );
	 // NULL term
	 m_reqBuf.pushChar('\0');

	 m_reqBufValid = true;

	 // and point to that
	 char *req    = m_reqBuf.getBufStart();

	 if( !req ) { 
		 log(LOG_ERROR, "http: req is NULL");
		 g_errno = EBADREQUEST; 
		 return false; 
	 }


	 int32_t  reqLen = m_reqBuf.length() - 1;

	 // save this
	 m_userIP = 0; if ( sock ) m_userIP = sock->m_ip;
	 m_isSSL  = 0; if ( sock ) m_isSSL = (bool)sock->m_ssl;

	 // TcpServer should always give us a NULL terminated request
	 if ( req[reqLen] != '\0' ) { g_process.shutdownAbort(true); }
	 
	 // how long is the first line, the primary request
	 // int32_t i;
	 // for ( i = 0 ; i<reqLen && i<MAX_REQ_LEN && 
	 //	       req[i]!='\n' && req[i]!='\r'; i++);
	 // . now fill up m_buf, used to log the request
	 // . make sure the url was encoded correctly
	 // . we don't want assholes encoding every char so we can't see what
	 //   url they are submitting to be spidered/indexed
	 // . also, don't de-code encoded ' ' '+' '?' '=' '&' because that would
	 //   change the meaning of the url
	 // . and finally, non-ascii chars that don't display correctly
	 // . this should NULL terminate m_buf, too
	 // . turn this off for now, just try to log a different way
	 // m_bufLen = urlNormCode ( m_buf , MAX_REQ_LEN - 1 , req , i );
	 // ensure it's big enough to be a valid request
	 if ( reqLen < 5 ) { 
		 log(LOG_WARN, "http: got reqlen %" PRId32"<5 = %s",reqLen,req);
		 g_errno = EBADREQUEST; 
		 return false; 
	 }

	 int32_t cmdLen = 0;

	 // or if first line too long
	 //if ( i >= 1024 )  { g_errno = EBADREQUEST; return false; }
	 // get the type, must be GET or HEAD
	 if      ( strncmp ( req , "GET "  , 4 ) == 0 ) {
		 m_requestType = RT_GET;
		 cmdLen = 3;
	 }
	 // these means a compressed reply was requested. use by query
	 // compression proxies.
	 else if ( strncmp ( req , "ZET "  , 4 ) == 0 ) {
		 m_requestType = RT_GET;
		 cmdLen = 3;
	 }
	 else if ( strncmp ( req , "HEAD " , 5 ) == 0 ) {
		 m_requestType = RT_HEAD;
		 cmdLen = 4;
	 }
	 else if ( strncmp ( req , "POST " , 5 ) == 0 )  {
		 m_requestType = RT_POST;
		 cmdLen = 4;
	 }
	 else if ( strncmp ( req , "CONNECT " , 8 ) == 0 ) {
		 // take this out until it stops losing descriptors and works
		 //m_requestType = RT_CONNECT;
		 //cmdLen = 7;
		 // we no longer insert section info. emmanuel gets section
		 // info when injecting a doc now i think in PageInject.cpp.
		 // we do not proxy https requests because we can't
		 // decrypt the page contents to cache them or to insert
		 // the sectiondb voting markup, so it's kinda pointless...
		 // and i'm not aiming to be a full-fledge squid proxy.
		 log("http: CONNECT request not supported because we "
		   "can't insert section markup and we can't cache: %s",req);
		 g_errno = EBADREQUEST; 
		 return false; 
	 }
	 else { 
		 log("http: got bad request cmd: %s",req);
		 g_errno = EBADREQUEST; 
		 return false; 
	 }
	 // . NULL terminate the request (a destructive operation!)
	 // . this removes the last \n in the trailing \r\n 
	 // . shit, but it fucks up POST requests
	 if ( m_requestType != RT_POST ) { 
		 req [ reqLen - 1 ] = '\0'; 
		 reqLen--; 
	 }

	 // POST requests can be absolutely huge if you are injecting a 100MB
	 // file, so limit our strstrs to the end of the mime
	 char *d = NULL;
	 char  dc;
	 // check for body if it was a POST request
	 if ( m_requestType == RT_POST ) {
		 d = strstr ( req , "\r\n\r\n" );
		 if ( d ) { dc = *d; *d = '\0'; }
		 else log("http: Got POST request without \\r\\n\\r\\n.");
	 }

	 // is it a proxy request?
	 m_isSquidProxyRequest = false;
	 if ( strncmp ( req + cmdLen + 1, "http://" ,7) == 0 ||
	      strncmp ( req + cmdLen + 1, "https://",8) == 0 ) {
		 m_isSquidProxyRequest = true;
		 // set url parms for it
		 m_squidProxiedUrl = req + cmdLen + 1;
		 char *p = m_squidProxiedUrl + 7;
		 if ( *p == '/' ) p++; // https:// ?
		 // stop at whitespace or \0
		 for ( ; *p && ! is_wspace_a(*p) ; p++ );
		 // that's the length of it
		 m_squidProxiedUrlLen = p - m_squidProxiedUrl;
	 }
	 else if ( m_requestType == RT_CONNECT ) {
		 m_isSquidProxyRequest = true;
		 // set url parms for it
		 m_squidProxiedUrl = req + cmdLen + 1;
		 // usually its like CONNECT diffbot.com:443
		 char *p = m_squidProxiedUrl;
		 // stop at whitespace or \0
		 for ( ; *p && ! is_wspace_a(*p) ; p++ );
		 // that's the length of it
		 m_squidProxiedUrlLen = p - m_squidProxiedUrl;
	 }

	 // check authentication
	 char *auth = NULL;
	 if ( m_isSquidProxyRequest && req )
		 auth = strstr(req,"Proxy-authorization: Basic ");

	 //if ( m_isSquidProxyRequest && ! auth ) {
	 //	 log("http: no auth in proxy request %s",req);
	 //	 g_errno = EBADREQUEST; 
	 //	 return false; 
	 //}

	 SafeBuf tmp;
	 if ( auth ) {
		 // find end of it
		 char *p = auth;
		 for ( ; *p && *p != '\r' && *p != '\n' ; p++ );
		 tmp.base64Decode ( auth , p - auth );
	 }

	 // assume incorrect username/password
	 bool matched = false;
	 if ( m_isSquidProxyRequest ) {
		 // now try to match in g_conf.m_proxyAuth safebuf of
		 // username:password space-separated list
		 char *p = g_conf.m_proxyAuth.getBufStart();
		 // loop over those
		 for ( ; p && *p ; ) {
			 // skip initial white space
			 for ( ; *p && is_wspace_a(*p); p++ );
			 // skip to end of username:password thing
			 char *end = p;
			 for ( ; *end && !is_wspace_a(*end); end++);
			 // save
			 char *start = p;
			 // advance
			 p = end;
			 // this is always a match
			 if ( end-start == 3 && strncmp(start,"*:*",3) == 0 ) {
				 matched = true;
				 break;
			 }
			 // compare now
			 if ( tmp.length() != end-start ) 
				 continue;
			 if ( strncmp(tmp.getBufStart(),start,end-start) != 0 )
				 continue;
			 // we got a match
			 matched = true;
			 break;
		 }
	 }

	 // incorrect username:passwrod?
	 if ( m_isSquidProxyRequest && ! matched ) {
		 log("http: bad username:password in proxy request %s",req);
		 g_errno = EPERMDENIED;
		 return false; 
	 }

	 // if proxy request to download a url through us, we are done
	 if ( m_isSquidProxyRequest ) return true;

	 bool multipart = false;
	 if ( m_requestType == 2 ) { // is POST?
		 char *cd ;
		 cd = gb_strcasestr(req,"Content-Type: multipart/form-data");
		 if ( cd ) multipart = true;
	 }

	 // . point to the file path 
	 // . skip over the "GET "
	 int32_t filenameStart = 4 ;
	 // skip over extra char if it's a "HEAD " request
	 if ( m_requestType == RT_HEAD || m_requestType == RT_POST ) 
		 filenameStart++;

	 // are we a redirect?
	 int32_t i = filenameStart;
	 m_redirLen = 0;
	 if ( strncmp ( &req[i] , "/?redir=" , 8 ) == 0 ) {
		 for ( int32_t k = i+8; k<reqLen && m_redirLen<126 ; k++) {
			 if ( req[k] == '\r' ) break;
			 if ( req[k] == '\n' ) break;
			 if ( req[k] == '\t' ) break;
			 if ( req[k] ==  ' ' ) break;
			 m_redir[m_redirLen++] = req[k];
		 }
	 }
	 m_redir[m_redirLen] = '\0';

	 // find a \n space \r or ? that delimits the filename
	 for ( i = filenameStart ; i < reqLen ; i++ ) {
		 if ( is_wspace_a ( req [ i ] ) ) break;
		 if ( req [ i ] == '?' ) break;
	 }

	 // now calc the filename length
	 m_filenameLen = i - filenameStart;
	 // return false and set g_errno if it's 0
	 if ( m_filenameLen <= 0  ) { 
		 log("http: got filenameLen<=0: %s",req);
		 g_errno = EBADREQUEST; 
		 return false; 
	 }
	 // . bitch if too big
	 // . leave room for strcatting "index.html" below
	 if ( m_filenameLen >= MAX_HTTP_FILENAME_LEN - 10 ) { 
		 log("http: got filenameLen>=max");
		 g_errno = EBADREQUEST; 
		 return false; 
	 }
	 // . decode the filename into m_filename and reassign it's length
	 // . decode %2F to / , etc...
	 m_filenameLen = urlDecode(m_filename,req+filenameStart,m_filenameLen);
	 // NULL terminate m_filename
	 m_filename [ m_filenameLen ] = '\0';
	 // does it have a file extension AFTER the last / in the filename?
	 bool hasExtension = false;
	 for ( int32_t j = m_filenameLen-1 ; j >= 0 ; j-- ) {
		 if ( m_filename[j] == '.' ) { hasExtension = true; break; }
		 if ( m_filename[j] == '/' ) break;
	 }
	 // if it has no file extension append a /index.html
	 if ( ! hasExtension && m_filename [ m_filenameLen - 1 ] == '/' ) {
		 strcat ( m_filename , "index.html" );
		 m_filenameLen = strlen ( m_filename );
	 }


	 // . uses the TcpSocket::m_readBuf
	 // . if *p was ? then keep going
	 m_origUrlRequest = origReq + filenameStart;
	 char *p = origReq + m_filenameLen;
	 for ( ; *p && ! is_wspace_a(*p) ; p++ );
	 m_origUrlRequestLen = p - m_origUrlRequest;

	 // set file offset/size defaults
	 m_fileOffset = 0;
	 // -1 means ALL the file from m_fileOffset onwards
	 m_fileSize   = -1;  
	 // "e" points to where the range actually starts, if any
	 //char *e;
	 // . TODO: speed up by doing one strstr for Range: and maybe range:
	 // . do they have a Range: 0-100\n in the mime denoting a partial get?
	 //char *s = strstr ( req ,"Range:bytes=" );
	 //e = s + 12;
	 // try alternate formats
	 //if ( ! s ) { s = strstr ( req ,"Range: bytes=" ); e = s + 13; }
	 //if ( ! s ) { s = strstr ( req ,"Range: "       ); e = s +  7; }
	 // parse out the range if we got one
	 //if ( s ) {
	 //	int32_t x = 0;
	 //	sscanf ( e ,"%" PRId32"-%" PRId32 , &m_fileOffset , &x );
	 //	// get all file if range's 2nd number is non-existant
	 //	if ( x == 0 ) m_fileSize = -1;
	 //	else          m_fileSize = x - m_fileOffset;
	 //	// ensure legitimacy
	 //	if ( m_fileOffset < 0 ) m_fileOffset = 0;
	 //}
	 // reset our hostname
	 m_hostLen = 0;
	 // assume request is NOT from local network
	 //m_isMasterAdmin = false;
	 m_isLocal = false;
	 // get the virtual hostname they want to use
	 char *s = strstr ( req ,"Host:" );
	 // try alternate formats
	 if ( ! s ) s = strstr ( req , "host:" ); 
	 // must be on its own line, otherwise it's not valid
	 if ( s && s > req && *(s-1) !='\n' ) s = NULL;
	 // parse out the host if we got one
	 if ( s ) {
		 // skip field name, host:
		 s += 5;
		 // skip e to beginning of the host name after "host:"
		 while ( *s==' ' || *s=='\t' ) s++;
		 // find end of the host name
		 char *end = s;
		 while ( *end && !is_wspace_a(*end) ) end++;
		 // . now *end should be \0, \n, \r, ' ', ...
		 // . get host len
		 m_hostLen = end - s;
		 // truncate if too big
		 if ( m_hostLen >= 255 ) m_hostLen = 254;
		 // copy into hostname
		 gbmemcpy ( m_host , s , m_hostLen );
	 }
	 // NULL terminate it
	 m_host [ m_hostLen ] = '\0';

	 // get Referer: field
	 s = strstr ( req ,"Referer:" );
	 // find another
	 if ( ! s ) s = strstr ( req ,"referer:" );
	 // must be on its own line, otherwise it's not valid
	 if ( s && s > req && *(s-1) !='\n' ) s = NULL;
	 // assume no referer
	 m_refLen = 0;
	 // parse out the referer if we got one
	 if ( s ) {
		 // skip field name, referer:
		 s += 8;
		 // skip e to beginning of the host name after ':'
		 while ( *s==' ' || *s=='\t' ) s++;
		 // find end of the host name
		 char *end = s;
		 while ( *end && !is_wspace_a(*end) ) end++;
		 // . now *end should be \0, \n, \r, ' ', ...
		 // . get len
		 m_refLen = end - s;
		 // truncate if too big
		 if ( m_refLen >= 255 ) m_refLen = 254;
		 // copy into m_ref
		 gbmemcpy ( m_ref , s , m_refLen );
	 }
	 // NULL terminate it
	 m_ref [ m_refLen ] = '\0';

	 // get User-Agent: field
	 s = strstr ( req ,"User-Agent:" );
	 // find another
	 if ( ! s ) s = strstr ( req ,"user-agent:" );
	 // must be on its own line, otherwise it's not valid
	 if ( s && s > req && *(s-1) !='\n' ) s = NULL;
	 // assume empty
	 int32_t len = 0;
	 // parse out the referer if we got one
	 if ( s ) {
		 // skip field name, referer:
		 s += 11;
		 // skip e to beginning of the host name after ':'
		 while ( *s==' ' || *s=='\t' ) s++;
		 // find end of the agent name
		 char *end = s;
		 while ( *end && *end!='\n' && *end!='\r' ) end++;
		 // . now *end should be \0, \n, \r, ' ', ...
		 // . get agent len
		 len = end - s;
		 // truncate if too big
		 if ( len > 127 ) len = 127;
		 // copy into m_userAgent
		 gbmemcpy ( m_userAgent , s , len );
	 }
	 // NULL terminate it
	 m_userAgent [ len ] = '\0';

	 // get Cookie: field
	 s = strstr ( req, "Cookie:" );
	 // find another
	 if ( !s ) s = strstr ( req, "cookie:" );
	 // must be on its own line, otherwise it's not valid
	 if ( s && s > req && *(s-1) != '\n' ) s = NULL;
	 // assume empty
	 // m_cookieBufLen = 0;
	 m_cookiePtr = s;
	 // parse out the cookie if we got one
	 if ( s ) {
		 // skip field name, Cookie:
		 s += 7;
		 // skip s to beginning of cookie after ':'
		 while ( *s == ' ' || *s == '\t' ) s++;
		 // find end of the cookie
		 char *end = s;
		 while ( *end && *end != '\n' && *end != '\r' ) end++;
		 // save length
		 m_cookieLen = end - m_cookiePtr;
		 // get cookie len
		 //m_cookieBufLen = end - s;
		 // trunc if too big
		 //if (m_cookieBufLen > 1023) m_cookieBufLen = 1023;
		 // copy into m_cookieBuf
		 //gbmemcpy(m_cookieBuf, s, m_cookieBufLen);
	 }
	 // NULL terminate it
	 if ( m_cookiePtr ) m_cookiePtr[m_cookieLen] = '\0';
	 //m_cookieBuf[m_cookieBufLen] = '\0';
	 // convert every '&' in cookie to a \0 for parsing the fields
	 // for ( int32_t j = 0 ; j < m_cookieBufLen ; j++ ) 
	 //	 if ( m_cookieBuf[j] == '&' ) m_cookieBuf[j] = '\0';

	 // mark it as cgi if it has a ?
	 bool isCgi = ( req [ i ] == '?' ) ;
	 // reset m_filename length to exclude the ?* stuff
	 if ( isCgi ) {
		 // skip over the '?'
		 i++;
		 // find a space the delmits end of cgi
		 int32_t j;
		 for ( j = i; j < reqLen; j++) if (is_wspace_a(req[j])) break;
		 // now add it
		 if ( ! addCgi ( &req[i] , j-i ) ) return false;
		 // update i
		 i = j;
	 }

	 // . set path ptrs
	 // . the whole /cgi/14.cgi?coll=xxx&..... thang
	 m_path = req + filenameStart;
	 m_plen = i - filenameStart;
	 // we're local if hostname is 192.168.[0|1].y
	 //if ( strncmp(iptoa(sock->m_ip),"192.168.1.",10) == 0) {
	 //	m_isMasterAdmin = true; m_isLocal = true; }
	 //if ( strncmp(iptoa(sock->m_ip),"192.168.0.",10) == 0) {
	 //	m_isMasterAdmin = true; m_isLocal = true; }
	 //if(strncmp(iptoa(sock->m_ip),"192.168.1.",10) == 0) m_isLocal = true;
	 //if(strncmp(iptoa(sock->m_ip),"192.168.0.",10) == 0) m_isLocal = true;
	 if ( sock && strncmp(iptoa(sock->m_ip),"192.168.",8) == 0) 
		 m_isLocal = true;
	 if ( sock && strncmp(iptoa(sock->m_ip),"10.",3) == 0) 
		 m_isLocal = true;

	 // gotta scan all ips in hosts.conf as well...
	 // if we are coming from any of our own hosts.conf c blocks
	 // consider ourselves local
	 uint32_t last = 0;
	 for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		 Host *h = g_hostdb.getHost(i);
		 // save time with this check
		 if ( h->m_ip == last ) continue;
		 // update it
		 last = h->m_ip;
		 // returns number of top bytes in comon
		 int32_t nt = sock ? ipCmp ( sock->m_ip , h->m_ip ) : 0;
		 // at least be in the same c-block as a host in hosts.conf
		 if ( nt < 3 ) continue;
		 m_isLocal = true;
		 break;
	 }
		 

	 // connectips/adminips
	 // for ( int32_t i = 0 ; i < g_conf.m_numConnectIps ; i++ ) {
	 // 	 if ( sock->m_ip != g_conf.m_connectIps[i] ) continue;
	 // 	 m_isLocal = true;
	 // 	 break;
	 // }

	 // 127.0.0.1
	 if ( sock && sock->m_ip == 16777343 )
		 m_isLocal = true;

	 // . TODO: now add any cgi data from a POST.....
	 // . look after the mime
	 //char *d = NULL;
	 // check for body if it was a POST request
	 //if ( m_requestType == RT_POST ) d = strstr ( req , "\r\n\r\n" );

	 // return true now if no cgi stuff to parse
	 if ( d ) {
	 	// now put d's char back, just in case... does it really matter?
		*d = dc;

		 char *post    = d + 4;
		 int32_t  postLen = reqLen-(d+4-req) ;
		 // post sometimes has a \r or\n after it
		 while ( postLen > 0 && post[postLen-1]=='\r' ) postLen--;
		 // add it to m_cgiBuf, filter and everything
		 if ( ! addCgi ( post , postLen ) ) return false;
	 }

	 // Put '\0' back into the HttpRequest buffer...
	 // crap, not if we are multi-part unencoded stuff...
	 if ( m_cgiBuf && ! multipart ) {
		 // do not mangle the "ucontent"!
		 int32_t cgiBufLen = m_cgiBufLen;
		 cgiBufLen -= m_ucontentLen;
		 char *buf = m_cgiBuf;
		 for (int32_t i = 0; i < cgiBufLen ; i++) 
			 if (buf[i] == '&') buf[i] = '\0';
		 // don't decode the ucontent= field!
		 int32_t decodeLen = m_cgiBufLen;
		 // so subtract that
		 if ( m_ucontent ) decodeLen -= m_ucontentLen;
		 // decode everything. fixed for %00 in &content= so it
		 // doesn't set our parms when injecting.
		 int32_t len = urlDecodeNoZeroes(m_cgiBuf,m_cgiBuf,decodeLen);
		 // we're parsing crap after the null if the last parm 
		 // has no value
		 //memset(m_cgiBuf+len, '\0', m_cgiBufLen-len);
		 m_cgiBufLen = len;
		 // ensure that is null i guess
		 if ( ! m_ucontent ) m_cgiBuf[len] = '\0';
	 }
	
	 if (m_cgiBuf2){
		 char *buf = m_cgiBuf2;
		 for (int32_t i = 0; i < m_cgiBuf2Size-1 ; i++) 
			 if (buf[i] == '&') buf[i] = '\0';
		 // decode everything. fixed for %00 in &content= so it
		 // doesn't set our parms when injecting.
		 int32_t len = urlDecodeNoZeroes ( m_cgiBuf2 , 
						   m_cgiBuf2 , 
						   m_cgiBuf2Size);
		 memset(m_cgiBuf2+len, '\0', m_cgiBuf2Size-len);
	 }
	 // . parse the fields after the ? in a cgi filename
	 // . or fields in the content if it's a POST
	 // . m_cgiBuf must be and is NULL terminated for this
	 parseFields ( m_cgiBuf , m_cgiBufLen );
	 // Add extra parms to the request.  
	 if (m_cgiBuf2Size){
		 parseFields(m_cgiBuf2, m_cgiBuf2Size);
	 }

	 // urldecode the cookie buf too!!
	 if ( m_cookiePtr ) {
		 char *p = m_cookiePtr;
		 for (int32_t i = 0; i < m_cookieLen ; i++) {
			 //if (p[i] == '&') p[i] = '\0';
			 // cookies are separated with ';' in the request only
			 if (p[i] == ';') p[i] = '\0';
			 // a hack for the metacookie=....
			 // which uses &'s to separate its subcookies
			 // this is a hack for msie's limit of 50 cookies
			 if ( p[i] == '&' ) p[i] = '\0';
			 // set m_metaCookie to start of meta cookie
			 if ( p[i] == 'm' && p[i+1] == 'e' &&
			      strncmp(p,"metacookie",10) == 0 )
				 m_metaCookie = p;
		 }
		 int32_t len = urlDecode ( m_cookiePtr , 
					m_cookiePtr,
					m_cookieLen );
		 // we're parsing crap after the null if the last parm 
		 // has no value
		 memset(m_cookiePtr+len, '\0', m_cookieLen-len);
		 m_cookieLen = len;
	 }

	 return true;
 }

 // s must be NULL terminated
 bool HttpRequest::addCgi ( char *s , int32_t slen ) {
	 // calculate the length of the cgi data w/o the ?
	 //m_cgiBufMaxLen = slen + 1;
	 // alloc space for it, including a \0 at the end
	 //m_cgiBuf = (char *) mmalloc ( m_cgiBufMaxLen , "HttpRequest");
	 //if ( ! m_cgiBuf ) return false;
	 m_cgiBuf = s;
	 // set the length of the cgi string
	 m_cgiBufLen = slen;
	 // ensure no overflow (add 1 cuz we NULL terminate it)
	 //if ( m_cgiBufLen>=1023) { g_errno = EBUFTOOSMALL;return false;}
	 // copy cgi string into m_cgiBuf
	 //gbmemcpy ( m_cgiBuf , s , slen );
	 // NULL terminate and include it in the length
	 //m_cgiBuf [ m_cgiBufLen++ ] = '\0';
	 m_cgiBuf [ slen ] = '\0';
	 // change &'s to \0's
	 char *p = m_cgiBuf;
	 char *pend = m_cgiBuf + m_cgiBufLen;
	 for ( ; p < pend ; p++ ) {
		 // but the [&|?]ucontent=%s is an exception because that is
		 // unencoded to make it easier to interface with
		 if ( p[0] == 'u' && 
		      p[1] == 'c' && 
		      p[2] == 'o' &&
		      p > m_cgiBuf &&
		      // the new cgi buf is not mangled, so check for '&'
		      (p[-1] == '\0' || p[-1] == '?' ||p[-1]=='&') && 
		      strncmp ( p , "ucontent=" , 9 ) == 0 ) {
			 // store it
			 m_ucontent    = p + 9;
			 m_ucontentLen = m_cgiBufLen - (m_ucontent - m_cgiBuf);
			 // this includes the terminating \0, so remove it
			 //m_ucontentLen--;
			 break;
		 }
		 // put these in later, after we check for extra parms
		 //if ( *p =='&' ) *p = '\0';
	 }
	 // . decode it -- from m_cgiBuf back into m_cgiBuf
	 // . m_cgiBufLen should only decrease, never increase
	 // . only decode up to "s", keep m_unencodedContent the way it is

	 // save this until after we insert nulls
	 //m_cgiBufLen = urlDecode ( m_cgiBuf , m_cgiBuf , p - m_cgiBuf );

	 // update the null termination
	 m_cgiBuf [ m_cgiBufLen ] = '\0';
	 return true;
 }

float HttpRequest::getFloatFromCookie    ( const char *field, float def ) {
	int32_t flen;
	const char *cs = getStringFromCookie ( field , &flen , NULL );
	if ( ! cs ) return def;
	float cv = atof(cs);
	return cv;
}

int32_t HttpRequest::getLongFromCookie    ( const char *field, int32_t def ) {
	int32_t flen;
	const char *cs = getStringFromCookie ( field , &flen , NULL );
	if ( ! cs ) return def;
	int64_t cv = atoll(cs);
	// convert
	return (int32_t)cv;
}

int64_t HttpRequest::getLongLongFromCookie ( const char *field, int64_t def ) {
	int32_t flen;
	const char *cs = getStringFromCookie ( field , &flen , NULL );
	if ( ! cs ) return def;
	int64_t cv = strtoull(cs,NULL,10);
	return cv;
}

bool HttpRequest::getBoolFromCookie    ( const char *field, bool def ) {
	int32_t flen;
	const char *cs = getStringFromCookie ( field , &flen , NULL );
	if ( ! cs ) return def;
	if ( cs[0] == '0' ) return false;
	return true;
}

// *next is set to ptr into m_cgiBuf so that the next successive call to
// getString with the SAME "field" will start at *next. that way you
// can use the same cgi parameter multiple times. (like strstr kind of)
const char *HttpRequest::getStringFromCookie ( const char *field      ,
                                               int32_t *len        ,
                                               const char *defaultStr ,
                                               int32_t *next       ) {
	// get field len
	int32_t flen = strlen(field);
	// assume none
	if ( len ) *len = 0;
	// if no cookie, forget it
	if ( ! m_cookiePtr ) return defaultStr;
	// the end of the cookie
	//char *pend = m_cookieBuf + m_cookieBufLen;
	char *pend = m_cookiePtr + m_cookieLen;
	char *p    = m_cookiePtr;
	// skip over spaces and punct
	for ( ; p && p < pend ; p++ ) 
		if ( is_alnum_a(*p) ) break;
	// skip "Cookie:"
	if ( p + 7 < pend && ! strncasecmp(p,"cookie:",7) ) p += 7;
	// skip spaces after that
	for ( ; p && p < pend ; p++ ) 
		if ( is_alnum_a(*p) ) break;
	// crazy?
	if ( p >= pend ) return defaultStr;

	char *savedVal = NULL;
	// so we do not skip the first cookie, jump right in!
	// otherwise we lose the calendar cookie for msie
	goto entryPoint;
	// . loop over all xxx=yyy\0 thingies in the cookie
	// . we converted every '&' to a \0 when the cookiebuf was set above
	//for ( char *p = m_cookieBuf ; *p ; p += strlen(p) + 1 ) {
	// . no, we just keep them as &'s because seems like cookies use ;'s
	//   as delimeters not so much &'s. and when we log the cookie in the
	//   log, i wanted to see the whole cookie, so having \0's in the
	//   cookie was messing that up.
	for ( ; p < pend ; p++ ) { 
		// need a \0
		// fixes "display=0&map=0&calendar=0;" that is only one cookie.
		// so do not grap value of map or calendar from that!!
		if ( *p ) continue;
		// back to back \0's? be careful how we skip over them!
		if ( ! p[1] ) continue;
		// skip that
		if ( ++p >= pend ) break;
		// skip whitespace that follows
		for ( ; p < pend ; p++ ) 
			if ( ! is_wspace_a(*p) ) break;
		// end of cookie?
		if ( p >= pend ) break;
	entryPoint:
		// check first char
		if ( *p != *field ) continue;
		// does it match? continue if not a match
		if ( strncmp ( p , field , flen ) != 0 ) continue;
		// point to value
		char *val = p + flen;
		// must be an equal sign
		if ( *val != '=' ) continue;
		// skip that sign
		val++;
		// . cookies terminate fields by space or ; or &
		// . skip to end of cookie value for this field
		char *e = val;
		// skip over alnum. might also be \0 if this function
		// was already called somewhere else!
		// we NULL separated each cookie and then urldecoded each
		// cookie above in the m_cookieBuf logic. cookies can contain
		// encoded ;'s and &'s so i took this checks out of this while
		// loop. like the widgetHeader has semicolons in it and it
		// stores in the cookie.
		while ( e < pend && *e ) e++;
		// that is the length
		if ( len ) *len = e - val;
		// NULL terminate it, we should have already logged the cookie
		// so it should be ok to NULL terminate now. we already
		// call urlDecode() now above... and make the &'s into \0's
		*e = '\0';
		// if we were in the meta cookie, return that...
		// otherwise if you visited this site before metacookies
		// were used you might have the cookie outside the meta
		// cookie AND inside the metacookie, and only the value
		// inside the metacookie is legit...
		if ( val > m_metaCookie ) return val;
		// otherwise, save it and try to get from meta cookie
		savedVal = val;
		// length
		//if ( len ) *len = strlen(val);
		// this is the value!
		//return val;
	}
	// did we save something?
	if ( savedVal ) return savedVal;
	// no match
	return defaultStr;
}

// *next is set to ptr into m_cgiBuf so that the next successive call to
// getString with the SAME "field" will start at *next. that way you
// can use the same cgi parameter multiple times. (like strstr kind of)
const char *HttpRequest::getString ( const char *field, int32_t *len, const char *defaultStr,
				int32_t *next ) const {
	 const char *value = getValue ( field , len, next );
	 // return default if no match
	 if ( ! value ) { 
		 if ( ! len ) return defaultStr;
		 if ( defaultStr ) *len = strlen ( defaultStr );
		 else              *len = 0;
		 return defaultStr; 
	 }
	 // otherwise, it's a match

	 return value;
 }

bool HttpRequest::getBool ( const char *field, bool defaultBool ) const {
	int32_t flen;
	const char *cs = getString ( field , &flen , NULL );
	if ( ! cs ) return defaultBool;
	if ( cs[0] == '0' ) return false;
	return true;
}

int32_t HttpRequest::getLong ( const char *field, int32_t defaultLong ) const {
	int32_t len;
	const char *value = getValue ( field, &len, NULL );
	// return default if no match
	if ( ! value || len <= 0 )
		return defaultLong;
	// otherwise, it's a match
	char tmpbuf[32];
	if((size_t)len>=sizeof(tmpbuf) || len==0)
		return defaultLong;
	memcpy(tmpbuf,value,len);
	tmpbuf[len] = '\0';
	char *endptr = NULL;
	int32_t res = strtol ( value, &endptr, 10 );
	if(endptr && *endptr)
		return defaultLong;
	return res;
}

int64_t HttpRequest::getLongLong ( const char *field,
				   int64_t defaultLongLong ) const {
	int32_t len;
	const char *value = getValue ( field, &len, NULL );
	// return default if no match
	if ( ! value || len <= 0 )
		return defaultLongLong;
	// otherwise, it's a match
	char tmpbuf[32];
	if((size_t)len>=sizeof(tmpbuf) || len==0)
		return defaultLongLong;
	memcpy(tmpbuf,value,len);
	tmpbuf[len] = '\0';
	char *endptr = NULL;
	int64_t res = strtoull ( value, &endptr, 10 );
	if(endptr && *endptr)
		return defaultLongLong;
	return res;
}

float HttpRequest::getFloat ( const char *field, double defaultFloat ) const {
	int32_t len;
	const char *value = getValue ( field, &len, NULL );
	// return default if no match
	if ( ! value || len <= 0 )
		return defaultFloat;
	// otherwise, it's a match
	char tmpbuf[32];
	if((size_t)len>=sizeof(tmpbuf) || len==0)
		return defaultFloat;
	memcpy(tmpbuf,value,len);
	tmpbuf[len] = '\0';
	char *endptr = NULL;
	double res = strtod ( tmpbuf, &endptr );
	if(endptr && *endptr)
		return defaultFloat;
	if(res>FLT_MAX || res<-FLT_MAX)
		return defaultFloat; //would overflow precision of a float
	//precision underflow silently ignored
	return res;
}

double HttpRequest::getDouble ( const char *field, double defaultDouble ) const {
	int32_t len;
	const char *value = getValue ( field, &len, NULL );
	// return default if no match
	if ( ! value || len <= 0 )
		return defaultDouble;
	// otherwise, it's a match
	char tmpbuf[32];
	if((size_t)len>=sizeof(tmpbuf) || len==0)
		return defaultDouble;
	memcpy(tmpbuf,value,len);
	tmpbuf[len] = '\0';
	char *endptr = NULL;
	double res = strtod ( tmpbuf, &endptr );
	if(endptr && *endptr)
		return defaultDouble;
	return res;
}


bool HttpRequest::hasField ( const char *field ) const {
	// how long is it?
	int32_t fieldLen = strlen ( field );
	// scan the field table directly
	int32_t i = 0;
	for (  ; i < m_numFields ; i++ ) {
		if ( fieldLen != m_fieldLens[i]                    ) continue; 
		if ( strncmp ( field, m_fields[i], fieldLen ) != 0 ) continue;
		// got a match return the true
		return true;
	}
	return false;
}


const char *HttpRequest::getValue ( const char *field , int32_t *len, int32_t *next ) const {
	// how long is it?
	int32_t fieldLen = strlen ( field );
	// scan the field table directly
	int32_t i = 0;
	if ( next ) i = *next ;
	for (  ; i < m_numFields ; i++ ) {
		if ( fieldLen != m_fieldLens[i]                    ) continue; 
		if ( strncmp ( field, m_fields[i], fieldLen ) != 0 ) continue;
		// got a match return the value
		if ( next ) *next = i + 1;
		if ( len ) *len = strlen(m_fieldValues[i]);
		return m_fieldValues[i];
	}
	// return NULL if no match
	if ( next ) *next = 0;
	return NULL;
}

const char *HttpRequest::getValue ( int32_t i, int32_t *len ) const {
	if ( i >= m_numFields ) return NULL;
	if (len) *len = strlen(m_fieldValues[i]);
	return m_fieldValues[i];
}

// . parse cgi fields contained in s
// . s points to the stuff immediately after the ?
// . we should have already replaced all &'s in s with /0's
// . we also replace the last \r with a \0
void HttpRequest::parseFields ( char *s , int32_t slen ) {

	// . are we a multipart/form-data?
	// . many of form tags for event submission forms are this
	//   <form enctype="multipart/form-data" ...>
	char *cd = strncasestr ( s , "\r\nContent-Disposition:", slen );
	if ( cd ) {
		parseFieldsMultipart ( s , slen );
		return;
	}

	// should be NULL terminated since we replaced &'s w/ 0's in set()
	char *send   = s + slen ;
	// reset field count
	int32_t n = m_numFields;
	while ( s && s < send ) {
		// watch out for overflow
		if ( n >= MAX_CGI_PARMS ) {
			log("http: Received more than %" PRId32" CGI parms. "
			    "Truncating.",(int32_t)MAX_CGI_PARMS);
			break;
		}
		// set the nth field name in this cgi string
		m_fields [ n ] = s;
		// point to = sign
		char *equal = strchr ( s , '=' );
		// try next field if none here
		if ( ! equal ) { s += strlen ( s ) + 1; continue; }
		// if no equal sign, maybe it is one of diffbot's valueless
		// fields, so support that now
		if ( ! equal ) { 
			// just set value to NULL
			char *end = strchr(s,'&');
			int32_t len = end - s;
			if ( ! end ) len = strlen(s);
			m_fieldLens[n] = len;
			s[len] = '\0';
			m_fieldValues[n] = NULL;
			n++;
			// skip over the '&' too
			s += len + 1; 
			continue; 
		}
		// set field len
		m_fieldLens [ n ] = equal - s;
		// set = to \0 so getField() returns NULL terminated field name
		*equal = '\0';
		// set value (may be \0)
		m_fieldValues [ n ] = equal + 1;
		// count the number of field/value pairs we get
		n++;
		//	skip:
		// point to next field
		s = equal + 1 + strlen ( equal + 1 ) + 1 ;
	}
	m_numFields = n;
}

void HttpRequest::parseFieldsMultipart ( char *s , int32_t slen ) {

	// should be NULL terminated since we replaced &'s w/ 0's in set()
	char *send   = s + slen ;
	// reset field count
	int32_t n = m_numFields;

 loop:
	// watch out for overflow
	if ( n >= MAX_CGI_PARMS ) {
		log("http: Received more than %" PRId32" CGI parms. "
		    "Truncating.",(int32_t)MAX_CGI_PARMS);
		return;
	}
	
	s = strncasestr ( s , "\r\nContent-Disposition:", send - s );
	if ( ! s ) return;

	// get the line end
	s += 2;
	char *lineEnd = strstr ( s , "\r\n" );
	if ( ! lineEnd ) return;

	// get the name
	char *name = strncasestr ( s , "name=\"" , lineEnd - s );
	if ( ! name ) goto loop;

	// point to name
	s = name + 6;
	// set the nth field name in this cgi string
	m_fields[n] = s;

	// point to = sign, use this for multiparts though
	char *equal = strstr ( s , "\"\r\n\r\n" );
	// for uploading a file it looks like
	// Content-Disposition: form-data; name=\"file\"; filename=\"poo.txt\"\r\nContent-Type: text/plain\r\n\r\nsomething here\n=====\nagain we do it...
	char *equal2 = strstr ( s , "\"" );

	// so if we had that then we had an uploaded file
	bool uploadedFile = false;
	if ( equal2 && equal && equal2 <  equal ) {
		uploadedFile = true;
		equal = equal2;
	}
	// try next field if none here
	if ( ! equal ) goto loop;
	// set field len
	m_fieldLens [ n ] = equal - s;
	// point to field value
	s = equal + 5;
	// unless we had an uploaded file, then skip more
	if ( uploadedFile ) {
		char *fileStart = strstr(equal,"\r\n\r\n");
		if ( fileStart ) fileStart += 4;
		s = fileStart;
	}
	// set = to \0 so getField() returns NULL terminated field name
	*equal = '\0';
	// set value (may be \0)
	m_fieldValues [ n ] = s;
	// force to \0 at end
	char *vend = strstr ( s , "\r\n----------"); // 29 -'s then a #
	if ( ! vend ) return;
	// null terminate the value as well
	*vend = '\0';
	// count the number of field/value pairs we get
	n++;
	// remember it
	m_numFields = n;
	// point to next field
	goto loop;

}

// . the url being reuqested
// . removes &code= facebook cruft
bool HttpRequest::getCurrentUrl ( SafeBuf &cu ) {
	// makre sure we got enough room
	if ( ! cu.reserve ( m_hostLen + 64 + m_plen + 1 + 1 ) ) return false;

	// need a "Host: "
	char *host = m_host;

	cu.safePrintf("http");
	if ( m_isSSL ) cu.pushChar('s');
	cu.safePrintf("://%s",host);
	const char *path = m_path;
	int32_t  plen = m_plen;
	if ( ! path ) {
		path = "/";
		plen = 1;
	}
	// . scan path and change \0 back to = or &
	// . similar logic in HttpServer.cpp for logging!
	char *dst = cu.getBufPtr();
	const char *src = path;
	const char *srcEnd = path + plen;
	char dd = '=';
	for ( ; src < srcEnd ; src++ , dst++ ) {
		*dst = *src;
		if ( *src ) continue;
		*dst = dd;
		if ( dd == '=' ) dd = '&';
		else             dd = '=';
	}
	*dst = '\0';
	// cut it off at facebook's &code=
	char *buf = cu.getBufStart();
	char *code = strstr( buf,"&code=");
	if ( ! code ) code = strstr(buf,"?code=");
	// hack that off if there
	if ( code ) {
		*code = '\0';
		dst = code;
	}
	// update length
	cu.setLength( dst - cu.getBufStart() );
	return true;
}

bool HttpRequest::getCurrentUrlPath ( SafeBuf &cup ) {
	// makre sure we got enough room
	if ( ! cup.reserve ( m_plen + 1 + 1 ) ) return false;
	const char *path = m_path;
	int32_t  plen = m_plen;
	if ( ! path ) {
		path = "/";
		plen = 1;
	}
	// . scan path and change \0 back to = or &
	// . similar logic in HttpServer.cpp for logging!
	char *dst = cup.getBufPtr();
	char *start = dst;
	const char *src = path;
	const char *srcEnd = path + plen;
	// stop if we hit '?'
	for ( ; src < srcEnd && *src != '?' ; src++ , dst++ ) {
		*dst = *src;
	}
	cup.incrementLength(dst - start);
	*dst = '\0';
	return true;
}

int getVersionFromRequest ( HttpRequest *r ) {
    char *path    = r->getFilename();
    int32_t  pathLen = r->getFilenameLen();
    bool requestHasVersion = pathLen > 2 && strncmp( path, "/v", 2 ) == 0;
    if (!requestHasVersion)
        return HTTP_REQUEST_DEFAULT_REQUEST_VERSION;
    char *rest = path + 2;
    char *end = strstr(rest, "/");
    if (!end)
        return HTTP_REQUEST_DEFAULT_REQUEST_VERSION;
    char version[3];
    int digits = end - rest;
    if (digits <= 0 || digits > 2)
        return HTTP_REQUEST_DEFAULT_REQUEST_VERSION;
    strncpy(version, rest, digits);
    version[digits] = '\0';
    int v = atoi(version);
    if (v <= 0 || v > 99)
        return HTTP_REQUEST_DEFAULT_REQUEST_VERSION;
    return v;
}
