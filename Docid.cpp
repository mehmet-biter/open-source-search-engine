#include "Docid.h"
#include "Url.h"
#include "hash.h"
#include "Titledb.h" //DOCID_MASK
#include "Punycode.h"
#include "UrlParser.h"
#include "gbmemcpy.h"
#include "Domains.h"
#include "ip.h"      // atoip ( s,len)
#include <algorithm>
#include <stdio.h>

#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif


namespace {

//trimmed-down version of Url which calls the static getTLD()
struct CompatibleUrl {
	char m_url[MAX_URL_LEN];
	int32_t m_ulen;
	char *m_host;
	int32_t m_hlen;
	const char *m_domain;
	int32_t m_dlen;

	const char *m_tld;
	int32_t m_tldLen;

	void set(const char *t, int32_t tlen);
};

} //anonymous namespace


static uint64_t getProbableDocId(const char *url, const char *dom, int32_t domLen) {
	uint64_t probableDocId = hash64b(url,0) & DOCID_MASK;
	// clear bits 6-13 because we want to put the domain hash there
	// dddddddd dddddddd ddhhhhhh hhdddddd
	probableDocId &= 0xffffffffffffc03fULL;
	uint32_t h = hash8(dom,domLen);
	//shift the hash by 6
	h <<= 6;
	// OR in the hash
	probableDocId |= h;
	return probableDocId;
}

static uint64_t getProbableDocId(const CompatibleUrl *url) {
	return ::getProbableDocId(url->m_url, url->m_domain, url->m_dlen);
}

uint64_t Docid::getProbableDocId(const Url *url) {
	CompatibleUrl u;
	u.set(url->getUrl(),url->getUrlLen());
	return ::getProbableDocId(u.m_url, u.m_domain, u.m_dlen);
}


uint64_t Docid::getProbableDocId(const char *url) {
	CompatibleUrl u;
	u.set(url,strlen(url));
	return ::getProbableDocId(&u);
}



uint64_t Docid::getFirstProbableDocId(int64_t d) {
	return d & 0xffffffffffffffc0ULL;
}

uint64_t Docid::getLastProbableDocId(int64_t d) {
	return d | 0x000000000000003fULL;
}


uint8_t Docid::getDomHash8FromDocId (int64_t d) {
	return (d & ~0xffffffffffffc03fULL) >> 6;
}






//Copied from Url::set()
//Lots of quiestionable code. Not much we can do.
//The purpose is to keep this code calling the static-list getTLD() so we have freedom to modify the normal Url class
void CompatibleUrl::set(const char *t, int32_t tlen) {
	bool addWWW = false;
	bool stripParams=false;
	bool stripCommonFile=false;
	int32_t titledbVersion = 129;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(t,tlen);
#endif
	char *m_scheme    = NULL;
	m_host      = NULL;
	char *m_path      = NULL;
	char *m_filename  = NULL;
	char *m_extension = NULL;
	char *m_query     = NULL;
	m_domain    = NULL;
	m_tld       = NULL;

	m_url[0]    = '\0';
	m_ulen      = 0;
	m_dlen      = 0;
	int32_t m_slen      = 0;
	int32_t m_qlen      = 0;
	m_hlen      = 0;
	int32_t m_elen      = 0;
	int32_t m_mdlen     = 0;

	// Coverity
	int32_t m_plen = 0;
	int32_t m_flen = 0;
	m_tldLen = 0;

	int32_t m_port = 0;
	int32_t m_defPort = 0;
	int32_t m_portLen = 0;

	char *m_portPtr = nullptr;
	int32_t m_portPtrLen = 0;

	if (!t || tlen == 0) {
		return;
	}

	// we may add a "www." a trailing backslash and \0, ...
	if (tlen > MAX_URL_LEN - 10) {
		log( LOG_LIMIT, "db: Encountered url of length %" PRId32 ". Truncating to %i", tlen, MAX_URL_LEN - 10 );
		tlen = MAX_URL_LEN - 10;
	}

	char stripped[MAX_URL_LEN];

	if (titledbVersion >= 125) {
		// skip starting spaces
		while (tlen > 0 && is_wspace_a(*t)) {
			++t;
			--tlen;
		}

		// remove tab/cr/lf
		std::string url(t, tlen);
		url.erase(std::remove_if(url.begin(), url.end(), [](char c) { return c == 0x09 || c == 0x0A || c == 0x0D; }), url.end());
		memcpy(stripped, url.c_str(), url.size());
		stripped[url.size()] = '\0';
		t = stripped;
		tlen = url.size();

		// skip ending spaces
		while (tlen > 0 && is_wspace_a(t[tlen - 1])) {
			--tlen;
		}
	}

	// . skip over non-alnum chars (except - or /) in the beginning
	// . if url begins with // then it's just missing the http: (slashdot)
	// . watch out for hostname like: -dark-.deviantart.com(yes, it's real)
	// . so all protocols are hostnames MUST start with alnum OR hyphen
	while (tlen > 0 && !is_alnum_a(*t) && *t != '-' && *t != '/') {
		t++;
		tlen--;
	}

	// . stop t at first space or binary char
	// . url should be in encoded form!
	int32_t i;
	int32_t nonAsciiPos = -1;
	for ( i = 0 ; i < tlen ; i++ ) {
		if (titledbVersion < 125 && is_wspace_a(t[i])) {
			break;
		}

		if (!is_ascii(t[i])) {
			// Sometimes the length with the null is passed in,
			// so ignore nulls FIXME?
			if (t[i]) {
				nonAsciiPos = i;
			}

			break; // no non-ascii chars allowed
		}
	}

	if ( nonAsciiPos != -1 ) {
		// Try turning utf8 and latin1 encodings into punycode.
		// All labels(between dots) in the domain are encoded
		// separately.  We don't support encoded tlds, but they are
		// not widespread yet.
		// If it is a non ascii domain it needs to take the form
		// xn--<punycoded label>.xn--<punycoded label>.../

		log(LOG_DEBUG, "build: attempting to decode unicode url %*.*s pos at %" PRId32, (int)tlen, (int)tlen, t, nonAsciiPos);

		char encoded [ MAX_URL_LEN ];
		size_t encodedLen = MAX_URL_LEN;
		char *encodedDomStart = encoded;
		const char *p = t;
		const char *pend = t+tlen;

		// Find the start of the domain
		if ( tlen > 7 && strncmp( p, "http://", 7 ) == 0 ) {
			p += 7;
		} else if ( tlen > 8 && strncmp( p, "https://", 8 ) == 0 ) {
			p += 8;
		}

		gbmemcpy(encodedDomStart, t, p-t);
		encodedDomStart += p-t;

		while (p < pend && *p != '/' && *p != ':') {
			const char *labelStart = p;
			uint32_t tmpBuf[MAX_URL_LEN];
			int32_t tmpLen = 0;

			while (p < pend && *p != '.' && *p != '/' &&
			       (titledbVersion < 125 || (titledbVersion >= 125 && *p != ':'))) {
				p++;
			}

			int32_t labelLen = p - labelStart;

			bool tryLatin1 = false;
			// For utf8 urls
			p = labelStart;
			bool labelIsAscii = true;

			// Convert the domain to code points and copy it to tmpbuf to be punycoded
			for ( ; p - labelStart < labelLen; p += utf8Size( tmpBuf[tmpLen] ), tmpLen++ ) {
				labelIsAscii = labelIsAscii && is_ascii( *p );
				tmpBuf[tmpLen] = utf8Decode( p );
				if ( !tmpBuf[tmpLen] ) { // invalid char?
					tryLatin1 = true;
					break;
				}
			}

			if ( labelIsAscii ) {
				if ( labelStart[labelLen] == '.' ) {
					labelLen++;
					p++;
				}
				gbmemcpy( encodedDomStart, labelStart, labelLen );
				encodedDomStart += labelLen;
				continue;
			}

			if ( tryLatin1 ) {
				// For latin1 urls
				tmpLen = 0;
				for ( ; tmpLen < labelLen; tmpLen++ ) {
					tmpBuf[tmpLen] = labelStart[tmpLen];
				}
			}

			gbmemcpy( encodedDomStart, "xn--", 4 );
			encodedDomStart += 4;

			encodedLen = MAX_URL_LEN - (encodedDomStart - encoded);
			punycode_status status = punycode_encode( tmpLen, tmpBuf, NULL, &encodedLen, encodedDomStart );

			if ( status != 0 ) {
				// Give up? try again?
				log("build: Bad Engineer, failed to "
				    "punycode international url %s (%" PRId32 ")",
				    t, (int32_t)status);
				return;
			}

			// We should check if what we encoded were valid url characters, no spaces, etc
			// FIXME: should we exclude just the bad chars? I've seen plenty of urls with
			// a newline in the middle.  Just discard the whole chunk for now
			bool badUrlChars = false;
			for ( uint32_t i = 0; i < encodedLen; i++ ) {
				if ( is_wspace_a( encodedDomStart[i] ) ) {
					badUrlChars = true;
					break;
				}
			}

			if ( encodedLen == 0 || badUrlChars ) {
				encodedDomStart -= 4; // don't need the xn--
				p++;
			} else {
				encodedDomStart += encodedLen;
				*encodedDomStart++ = *p++; // Copy in the . or the /
			}
		}

		// p now points to the end of the domain
		// encodedDomStart now points to the first free space in encoded string

		// Now copy the rest of the url in.  Watch out for non-ascii chars
		// truncate the url, and keep it under max url length
		uint32_t newUrlLen = encodedDomStart - encoded;

		while (p < pend) {
			if ( ! *p ) {
				break; // null?
			}

			if (!is_ascii(*p)) {
				// url encode utf8 characters now
				char cs = getUtf8CharSize(p);

				// bad utf8 char?
				if ( !isValidUtf8Char(p) ) {
					break;
				}

				int maxDestLen = (cs * 3) + 1; // %XX + \0

				// too long?
				if ( newUrlLen + maxDestLen >= MAX_URL_LEN ) {
					break;
				}

				char stored = urlEncode(&encoded[newUrlLen], maxDestLen, p, cs);
				p += cs;
				newUrlLen += stored;

				continue;
			}

			if (is_wspace_a(*p)) {
				break;
			}

			if (newUrlLen + 1 >= MAX_URL_LEN) {
				break;
			}

			encoded[newUrlLen++] = *p++;
		}

		encoded[newUrlLen] = '\0';
		return this->set( encoded, newUrlLen );
	}

	// truncate length to the first occurence of an unacceptable char
	tlen = i;

	// . jump over http:// if it starts with http://http://
	// . a common mistake...
	while ( tlen > 14 && ! strncasecmp ( t , "http://http://" , 14 ) ) {
		t += 7;
		tlen -= 7;
	}

	// only strip anchor for version <= 122 (we're stripping anchor in UrlParser)
	if (titledbVersion <= 122) {
		// strip the "#anchor" from http://www.xyz.com/somepage.html#anchor"
		for (int32_t i = 0; i < tlen; i++) {
			if (t[i] == '#') {
				// ignore anchor if a ! follows it. 'google hash bang hack'
				// which breaks the web and is now deprecated, but, there it is
				if (i + 1 < tlen && t[i + 1] == '!') {
					continue;
				}

				tlen = i;
				break;
			}
		}
	}

	// copy to "s" so we can NULL terminate it
	char s[MAX_URL_LEN];
	int32_t len = tlen;

	if (titledbVersion <= 122) {
		// store filtered url into s
		memcpy(s, t, tlen);
		s[len] = '\0';

		if (stripParams) {
			//stripParametersv122(s, &len);
		}
	} else {
		UrlParser urlParser(t, tlen, titledbVersion);

		if (stripParams) {
			//stripParameters(&urlParser);
		}

		// rebuild url
		urlParser.unparse();

		len = urlParser.getUrlParsedLen();

		if (len > MAX_URL_LEN - 10) {
			len = MAX_URL_LEN - 10;
		}
		strncpy(s, urlParser.getUrlParsed(), len);
		s[len] = '\0';
	}

	// remove common filenames like index.html
	if ( stripCommonFile ) {
		if ( len - 14 > 0 &&
		     strncasecmp(&s[len-14], "/default.xhtml", 14) == 0 )
			len -= 13;
		else if ( len - 13 > 0 &&
			( strncasecmp(&s[len-13], "/default.html", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.ascx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.ashx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.asmx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.xhtm", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.aspx", 13) == 0 ) )
			len -= 12;
		else if ( len - 12 > 0 &&
		        ( strncasecmp(&s[len-12], "/default.htm", 12) == 0 ||
		          strncasecmp(&s[len-12], "/default.php", 12) == 0 ||
		          strncasecmp(&s[len-12], "/default.asp", 12) == 0 ||
		          strncasecmp(&s[len-12], "/index.xhtml", 12) == 0 ) )
			len -= 11;
		else if ( len - 11 > 0 &&
		        ( strncasecmp(&s[len-11], "/index.html", 11) == 0 ||
		          strncasecmp(&s[len-11], "/index.aspx", 11) == 0 ||
		          strncasecmp(&s[len-11], "/index.xhtm", 11) == 0 ||
		          strncasecmp(&s[len-11], "/default.pl", 11) == 0 ||
		          strncasecmp(&s[len-11], "/default.cs", 11) == 0 ) )
			len -= 10;
		else if ( len - 10 > 0 &&
			( strncasecmp(&s[len-10], "/index.htm", 10) == 0 ||
			  strncasecmp(&s[len-10], "/index.php", 10) == 0 ||
			  strncasecmp(&s[len-10], "/index.asp", 10) == 0 ||
			  strncasecmp(&s[len-10], "/main.html", 10) == 0 ||
			  strncasecmp(&s[len-10], "/main.aspx", 10) == 0 ) )
			len -= 9;
		else if ( len - 9 > 0 &&
			( strncasecmp(&s[len-9], "/index.pl", 9) == 0 ||
			  strncasecmp(&s[len-9], "/main.htm", 9) == 0 ||
			  strncasecmp(&s[len-9], "/main.php", 9) == 0 ) )
			len -= 8;
		else if ( len - 8 > 0 &&
			( strncasecmp(&s[len-8], "/main.pl", 8) == 0 ) )
			len -= 7;
		s[len] = '\0';
	}
	

	// replace the "\" with "/" -- a common mistake
	int32_t j;
	for ( j = 0 ; s[j] ; j++)
	{
		if (s[j]=='\\')
		{
			s[j]='/';
		}
	}
		
	// . dig out the protocol/scheme for this s (check for ://)
	// . protocol may only have alnums and hyphens in it
	for ( i = 0 ; s[i] && (is_alnum_a(s[i]) || s[i]=='-') ; i++ );
	
	// if we have a legal protocol, then set "m_scheme", "slen" and "sch"
	// and advance i to the m_host
	if ( i + 2 < len && s[i]==':' && s[i+1]=='/' && s[i+2]=='/')
	{
		// copy lowercase protocol to "m_url"
		to_lower3_a ( s , i + 3 , m_url );
		m_scheme = m_url;
		m_slen   = i;
		m_ulen   = i + 3;
		i += 3;
	}
	else
	if (i + 2 < len && s[i]==':' && s[i+1]=='/'&& is_alnum_a(s[i+2]))
	{
		// copy lowercase protocol to "m_url"
		to_lower3_a ( s , i + 2 , m_url );
		// add in needed /
		m_url[i+2]='/';
		m_scheme = m_url;
		m_slen   = i;
		m_ulen   = i + 3;
		i += 2;
	}
	else
	{
		gbmemcpy ( m_url,"http://" , 7 );
		m_scheme = m_url;
		m_slen   = 4;
		m_ulen   = 7;
		i        = 0;
		// if s started with // then skip that (slashdot)
		if ( s[0]=='/' && s[1]=='/' ) i = 2;
	}
	// . now &s[i] should point to the m_host name
	// . chars allowed in hostname = period,alnum,hyphen,underscore
	// . stops at '/' or ':' or any other disallowed character
	j = i;
	while (s[j] && (is_alnum_a(s[j]) || s[j]=='.' || s[j]=='-'||s[j]=='_'))
		j++;
	// copy m_host into "s" (make it lower case, too)
	to_lower3_a ( s + i, j - i, m_url + m_ulen );
	m_host    = m_url + m_ulen;
	m_hlen    = j - i;
	// common mistake: if hostname ends in a . then back up
	while ( m_hlen > 0 && m_host[m_hlen-1]=='.' ) m_hlen--;
	// NULL terminate for strchr()
	m_host [ m_hlen ] = '\0';

	// advance m_ulen to end of hostname
	m_ulen += m_hlen;

	// . Test if hostname is in a.b.c.d format
	// . this returns 0 if not a valid ip string
	int32_t ip = atoip ( m_host , m_hlen );

	// advance i to the : for the port, if it exists
	i = j;

	// NULL terminate m_host for getTLD(), getDomain() and strchr() below
	m_host [ m_hlen ] = '\0';

	// use ip as domain if we're just an ip address like 192.0.2.1
	if ( ip ) {
		// ip address has no tld, or mid domain
		m_tld    = NULL;
		m_tldLen = 0;
		// but it does have a domain (1.2.3)
		m_domain = getDomainOfIp ( m_host , m_hlen , &m_dlen );
		// just use the domain as the mid domain for ip-based urls
		m_mdlen  = m_dlen;
	}
	// . otherwise, get the tld
	// . uses thorough list of tlds in Domains.cpp
	else if ( ( m_tld = ::getTLD_static ( m_host, m_hlen ) ) && m_tld > m_host ) {
		// set m_domain if we had a tld that's not equal to our host
		m_tldLen = strlen ( m_tld  );
		m_domain = ::getDomain ( m_host , m_hlen , m_tld , &m_dlen );
		// set the mid domain length (-1 for the '.')
		m_mdlen  = m_dlen - m_tldLen - 1;
	}
	// otherwise, we're no ip and we have no valid domain
	else {
		m_domain = NULL;
		m_dlen   = 0;
		m_tldLen = 0;
		m_mdlen  = 0;
	}

	// . if domain same as host then we might insert a "www." server name
	// . however, must have a period in domain name
	// . otherwise a domain name of "xxx" would become "www.xxx" and if
	//   Url::set() is called on that it would be "www.www.xxx" (bad bad)
	// . let's only add "www." if there's only 1 period, ok?
	if ( ! ip && addWWW && m_host == m_domain  && strchr(m_host,'.') ) {
		memmove ( m_host + 4 , m_host , m_hlen );
		gbmemcpy ( m_host , "www." , 4 );
		if ( m_domain ) m_domain += 4;
		if ( m_tld    ) m_tld    += 4;
		m_ulen += 4;
		m_hlen += 4;
	}
	// set the default port based on the protocol
	m_defPort = 80;
	if ( m_slen==5 && strncmp(m_scheme, "https",5)==0 ) m_defPort = 443;
	// assume we're using the default port for this scheme/protocol
	m_port = m_defPort;
	// see if a port was provided in the hostname after a colon
	if ( s[i] == ':' ) {
		// remember the ptr so far
		int32_t savedLen = m_ulen;
		// add a colon to our m_url
		m_url [ m_ulen++ ] = ':';
		// scan for a '/'
		j = i + 1;
		while ( s[j] && s[j]!='/') m_url[m_ulen++] = s[j++];

		m_portPtr = s + i + 1;
		m_portPtrLen = j - (i + 1);

		// now read our port
		m_port = atol2(m_portPtr, m_portPtrLen);

		// if it's the default port, then remove what we copied
		if ( m_port == m_defPort ) m_ulen = savedLen;
		// make i point to the root / in the m_path, if any
		i = j;
	}
	// how many chars is taken up by a specified port?
	m_portLen = 0;
	if ( m_port != m_defPort ) {
		m_portLen += 2; // :3
		if ( m_port >= 10    ) m_portLen += 1;
		if ( m_port >= 100   ) m_portLen += 1;
		if ( m_port >= 1000  ) m_portLen += 1;
		if ( m_port >= 10000 ) m_portLen += 1;
	}

	// append a '/' to m_url then bail if there is no m_path after the port
	if ( s[i] != '/') {
		m_path    = m_url + m_ulen;
		m_path[0] = '/';
		m_plen    = 1;
		m_url[ ++m_ulen ]='\0';
		return;
	}

	// . get the m_path and m_path length
	// . j,i should point to start of path slash '/'
	// . scan so it points to end or a ? or #
	j = i;
	
	// now we include # as part of the path if it is a hash bang '#!'
	// which was the web-breaking google hack that is now deprecated
	while ( s[j] && s[j]!='?' ) {
		if ( s[j] == '#' && s[j+1] != '!' )
			break;
		j++;
	}

	// point the path inside m_url even though we haven't written it yet
	m_path = m_url + m_ulen;
	m_plen = m_ulen;
	// . deal with wierd things in the path
	// . i points to start of path (should be /)
	for (; i < j ; i++ ) {
		// dedup double backslashes
		// ensure m_ulen >= m_plen so we don't hurt "http:///" ...
		// but people sometimes put http:// in the *path*
		if ( s[i] == '/'  &&  m_url[m_ulen-1] == '/' &&
		     m_ulen-1 >= m_plen &&
		     m_ulen >= 2 && m_url[m_ulen-2] != ':' ) continue;

		// handled by UrlParser for version 123 and above
		if (titledbVersion <= 122) {
			// deal with current directories in the m_path
			if ( s[i] == '.'  &&  m_url[m_ulen-1] == '/' &&
			     (i+1 == j || s[i+1]=='/'))	continue;
			// . deal with damned ..'s in the m_path
			// . if next 2 chars are .'s and last char we wrote was '/'
			if ( s[i] == '.' && s[i+1]=='.' && (s[i+2] == '/' || s[i+2] == '\0') && m_url[m_ulen-1] == '/' ) {
				// dont back up over first / in path
				if ( m_url + m_ulen - 1 > m_path ) m_ulen--;
				while ( m_url[m_ulen-1] != '/'   ) m_ulen--;
				// skip i to next / after these 2 dots
				while ( s[i] && s[i]!='/' ) i++;
				continue;
			}
		}

		// don't allow ; before the ?...probably because of stripped
		// sessionId...
		// I was going to add other possible dup separators, but now
		// it seems as though it might cause problems
		if (s[i] == ';' && s[i+1] == '?') continue;

		// store char and advance to next
		m_url[m_ulen++] = s[i];
	}
	// reset the path length in case we had to remove some wierd stuff
	m_plen = m_ulen - m_plen;

	// . get the m_query
	// . the query is anything after the path that starts with ?
	// . NOTE: we ignore strings beginning with '#' (page relative anchors)
	if ( i < len && s[i] != '#' ) {
		//remove back to back &'s in the cgi query
		//http://www.nyasatimes.com/national/politics/160.html?print&&&
		char *kstart = s + i;
		char *kend   = s + i + (len - i);
		char *dst    = m_url + m_ulen;
		for ( char *k = kstart ; k < kend ;  k++ ) {
			// skip & if we just did one
			if ( *k == '&' && k > kstart && *(k-1)=='&' ) continue;
			// copy over one char at a time
			*dst++ = *k;
		}
		// point after the '?' i guess
		m_query   = m_url + m_ulen + 1;
		m_qlen    = dst - m_query;
		m_ulen += m_qlen + 1;
	}
	// get the m_filename from the m_path (m_flen might be 0)
	m_flen = 0;
	while (m_path[m_plen-1-m_flen]!='/' && m_flen<m_plen) m_flen++;
	m_filename = m_path + m_plen - m_flen;

	// get the m_extension from the m_path
	m_elen = 0;
	while (is_alnum_a(m_path[m_plen-1-m_elen]) && m_elen < m_plen)m_elen++;
	if ( m_path[ m_plen-1-m_elen] != '.' ) m_elen = 0; // no m_extension
	m_extension = m_path + m_plen - m_elen;

	// null terminate our s
	m_url[ m_ulen ]='\0';
}
