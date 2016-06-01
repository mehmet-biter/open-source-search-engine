#include "gb-include.h"

#include "Url.h"
#include "UrlParser.h"
#include "Domains.h"
#include "HashTable.h"
#include "Speller.h"
#include "Punycode.h"
#include "Unicode.h"

#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

void Url::reset() {
	m_scheme    = NULL;
	m_host      = NULL;
	m_path      = NULL;
	m_filename  = NULL;
	m_extension = NULL;
	m_query     = NULL;
	m_domain    = NULL;
	m_tld       = NULL;
	m_anchor    = NULL;

	m_url[0]    = '\0';
	m_ulen      = 0;
	m_dlen      = 0;
	m_slen      = 0;
	m_qlen      = 0;
	m_hlen      = 0;
	m_elen      = 0;
	m_mdlen     = 0;
	m_anchorLen = 0;
	// ip related stuff
	m_ip          = 0;
}

void Url::set( const Url *baseUrl, const char *s, int32_t len, bool addWWW, bool stripParams, bool stripPound,
               bool stripCommonFile, int32_t titledbVersion ) {

	reset();

	if ( ! baseUrl ) {
		set( s, len, addWWW, false, false, false, titledbVersion );
		return;
	}

	char *base = (char *) baseUrl->m_url;
	int32_t  blen = baseUrl->m_ulen;

	// don't include cgi crap
	if ( baseUrl->m_query ) {
		blen -= ( baseUrl->m_qlen + 1 );
	}

	// . adjust length of the base url.
	// . if base url does not end in / then it must have a m_filename at
	//   the end, therefore we should strip the m_filename
	if ( blen > 0 && base[blen - 1] != '/' ) {
		while ( blen > 0 && base[blen - 1] != '/' ) {
			blen--;
		}
	}

	// . fix baseurl = "http://xyz.com/poo/all" and s = "?page=3"
	// . if "s" starts with ? then keep the filename in the base url
	if ( s[0] == '?' ) {
		for ( ; base[blen] && base[blen] != '?'; blen++ )
			;
	}

	if ( blen == 0 && len == 0 ) {
		return;
	}

	// skip s over spaces
	const char *send = s + len;
	while ( s < send && is_wspace_a( *s ) ) {
		s++;
		len--;
	}

	// . is s a relative url? search for ://, but break at first /
	// . but break at any non-alnum or non-hyphen
	bool isAbsolute = false;
	int32_t i;
	for ( i = 0; i < len && ( is_alnum_a( s[i] ) || s[i] == '-' ); i++ )
		;

	// for ( i = 0 ; s[i] && (is_alnum_a(s[i]) || s[i]=='-') ; i++ );
	if ( !isAbsolute )
		isAbsolute = ( i + 2 < len && s[i + 0] == ':' && s[i + 1] == '/' ); // some are missing both /'s!

	// s[i+2]=='/'  ) ;
	if ( !isAbsolute )
		isAbsolute = ( i + 2 < len && s[i + 0] == ':' && s[i + 1] == '\\' );

	// or if s starts with // then it's also considered absolute!
	if ( !isAbsolute && len > 1 && s[0] == '/' && s[1] == '/' )
		isAbsolute = true;

	// watch out for idiots
	if ( !isAbsolute && len > 1 && s[0] == '\\' && s[1] == '\\' )
		isAbsolute = true;

	// don't use base if s is not relative
	if ( blen==0 || isAbsolute ) {
		set( s, len, addWWW, stripParams, stripPound, false, titledbVersion );
		return;
	}

	// . if s starts with / then hack of base's m_path
	// . careful not to hack of the port, if any
	// . blen = baseUrl->m_slen + 3 + baseUrl->m_hlen;
	if ( len > 0 && s[0]=='/' )
		blen = baseUrl->m_path - baseUrl->m_url ;

	char temp[MAX_URL_LEN * 2 + 1];
	strncpy( temp, base, blen );

	if ( len > MAX_URL_LEN ) {
		len = MAX_URL_LEN - 2;
	}

	// if s does NOT start with a '/' then add one here in case baseUrl
	// does NOT end in one.
	// fix baseurl = "http://xyz.com/poo/all" and s = "?page=3"
	if ( len > 0 && s[0] != '/' && s[0] != '?' && temp[blen - 1] != '/' ) {
		temp[blen++] = '/';
	}
	strncpy( temp + blen, s, len );
	set( temp, blen + len, addWWW, stripParams, stripPound, stripCommonFile, titledbVersion );
}


static bool isSessionId ( const char *hh ) {
	int32_t count = 0;
	int32_t nonNumCount = 0;

	// do not limit count to 12, the hex numbers may only be
	// after the 12th character! we were not identifying these
	// as sessionids when we shold have been because of that.
	for ( ; *hh ; ++count, ++hh ) {
		if ( *hh >= '0' && *hh <= '9' ) continue;
		nonNumCount++;
		if ( *hh >= 'a' && *hh <= 'f' ) continue;
		// we got an illegal session id character
		return false;
	}

	// if we got at least 12 of em, consider it a valid id
	// make sure it's a hexadecimal number...lots of product
	// ids and dates use only decimal numbers
	return ( nonNumCount > 0 && count >= 12);
}

static void stripParametersv122( char *s, int32_t *len ) {
	// . remove session ids from s
	// . ';' most likely preceeds a session id
	// . http://www.b.com/p.jhtml;jsessionid=J4QMFWBG1SPRVWCKUUXCJ0W?pp=1
	// . http://www.b.com/generic.html;$sessionid$QVBMODQAAAGNA?pid=7
	// . http://www.b.com/?PHPSESSID=737aec14eb7b360983d4fe39395&p=1
	// . http://www.b.com/cat.cgi/process?mv_session_id=xrf2EY3q&p=1
	// . http://www.b.com/default?SID=f320a739cdecb4c3edef67e&p=1

	// CHECK FOR A SESSION ID USING QUERY STRINGS
	char *p = s;
	while ( *p && *p != '?' && *p != ';' ) p++;

	// bail if no ?
	if ( ! *p ) {
		return;
	}

	// now search for severl strings in the cgi query string
	char *tt = NULL;
	int32_t x = 0;

	if ( ! tt ) { tt = gb_strcasestr ( p, "PHPSESSID=" ); x = 10;}
	if ( ! tt ) { tt = strstr        ( p , "SID="       ); x =  4;}
	// . osCsid and XTCsid are new session ids
	// . keep this up here so "sid=" doesn't override it
	if ( ! tt  ) {
		tt = strstr ( p , "osCsid=" );
		x =  7;
		if ( ! tt ) tt = strstr ( p , "XTCsid=" );
		// a hex sequence of at least 10 digits must follow
		if ( tt && ! isSessionId ( tt + x ) )
			tt = NULL;
	}
	if ( ! tt ) {
		tt = strstr ( p , "osCsid/" );
		x =  7;
		// a hex sequence of at least 10 digits must follow
		if ( tt && ! isSessionId ( tt + x ) )
			tt = NULL;
	}
	// this is a new session id thing
	if ( ! tt ) {
		tt = strstr ( p , "sid=" ); x =  4;
		// a hex sequence of at least 10 digits must follow
		if ( tt && ! isSessionId ( tt + x ) )
			tt = NULL;
	}
	// osCsid and XTCsid are new session ids
	if ( ! tt ) {
		tt = strstr ( p , "osCsid=" );
		x =  7;
		if ( ! tt ) tt = strstr ( p , "XTCsid=" );
		// a hex sequence of at least 10 digits must follow
		if ( tt && ! isSessionId ( tt + x ) )
			tt = NULL;
	}

	// fixes for bug of matching plain &sessionid= first and
	// then realizing char before is an alnum...
	if ( ! tt ) { tt = gb_strcasestr ( p, "jsessionid="); x = 11; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "vbsessid="  ); x =  9;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "asesessid=" ); x = 10; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "nlsessid="  ); x =  9; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "psession="  ); x =  9; }

	if ( ! tt ) { tt = gb_strcasestr ( p, "session_id="); x = 11;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "sessionid=" ); x = 10;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "sessid="    ); x =  7;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "session="   ); x =  8;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "session/"   ); x =  8; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "POSTNUKESID=");x = 12;}
	// some new session ids as of Feb 2005
	if ( ! tt ) { tt = gb_strcasestr ( p, "auth_sess=" ); x = 10; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "mysid="     ); x =  6; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "oscsid="    ); x =  7; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "cg_sess="   ); x =  8; }
	if ( ! tt ) { tt = gb_strcasestr ( p, "galileoSession");x=14; }
	// new as of Jan 2006. is hurting news5 collection on gb6
	if ( ! tt ) { tt = gb_strcasestr ( p, "sess="      ); x =  5; }

	// .php?s=8af9d6d0d59e8a3108f3bf3f64166f5a&
	// .php?s=eae5808588c0708d428784a483083734&
	// .php?s=6256dbb2912e517e5952caccdbc534f3&
	if ( ! tt && (tt = strstr ( p-4 , ".php?s=" )) ) {
		// point to the value of the s=
		char *pp = tt + 7;
		int32_t i = 0;
		// ensure we got 32 hexadecimal chars
		while ( pp[i] &&
			( is_digit(pp[i]) ||
			  ( pp[i]>='a' && pp[i]<='f' ) ) ) i++;
		// if not, do not consider it a session id
		if ( i < 32 ) tt = NULL;
		// point to s= for removal
		else { tt += 5; x = 2; }
	}

	// BR 20160117
	// http://br4622.customervoice360.com/about_us.php?SES=652ee78702fe135cd96ae925aa9ec556&frmnd=registration
	if ( ! tt ) { tt = strstr        ( p , "SES="       ); x =  4;}

	// BR 20160117: Skip most common tracking parameters
	// Oracle Eloqua
	// http://app.reg.techweb.com/e/er?s=2150&lid=25554&elq=00000000000000000000000000000000&elqaid=2294&elqat=2&elqTrackId=3de2badc5d7c4a748bc30253468225fd
	if ( ! tt ) { tt = gb_strcasestr ( p, "elq="); x = 4;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "elqat="); x = 6;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "elqaid="); x = 7;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "elq_mid="); x = 8;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "elqTrackId="); x = 11;}

	// Google Analytics
	// http://kikolani.com/blog-post-promotion-ultimate-guide?utm_source=kikolani&utm_medium=320banner&utm_campaign=bpp
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_term="); x = 9;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_hp_ref="); x = 11;}	// Lots on huffingtonpost.com
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_source="); x = 11;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_medium="); x = 11;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_content="); x = 12;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "utm_campaign="); x = 13;}

	// Piwik
	if ( ! tt ) { tt = gb_strcasestr ( p, "pk_kwd="); x = 7;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "pk_source="); x = 10;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "pk_medium="); x = 10;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "pk_campaign="); x = 12;}

	// Misc
	if ( ! tt ) { tt = gb_strcasestr ( p, "trk="); x = 4;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "promoid="); x = 8;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "promCode="); x = 9;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "promoCode="); x = 10;}
	if ( ! tt ) { tt = gb_strcasestr ( p, "partnerref="); x = 11;}

	// bail if none were found
	if ( ! tt ) {
		return;
	}

	// . must not have an alpha char before it!
	// . prevent "DAVESID=" from being labeled as session id
	if ( is_alnum_a ( *(tt-1) ) ) {
		return;
	}

	// start of the shit
	int32_t a = tt - s;

	// get the end of the shit
	int32_t b = a + x;

	// back up until we hit a ? or & or / or ;
	while ( a > 0 && s[a-1] != '?' && s[a-1] != '&' &&
		s[a-1] != '/' && s[a-1] != ';' ) a--;

	// keep the '?'
	if ( s[a]=='?' ) a++;

	// back up over any semicolon
	if ( s[a-1] == ';' ) a--;

	// advance b until we hit & or end or ? or a ';'
	while ( s[b] && s[b] != '&' && s[b] != '?' && s[b] != ';') b++;

	// if we don't have 5+ chars in session id itself, skip it
	if ( b - (a + x) < 5 ) {
		return;
	}

	// go over a & or a ;
	if ( s[b] == '&' || s[b] == ';' ) b++;

	// remove the session id by covering it up
	memmove ( &s[a] , &s[b] , *len - b );

	// reduce length
	*len -= (b-a);

	// if s ends in ? or & or ;, backup
	while ( *len > 0 && (s[*len-1]=='?'||s[*len-1]=='&'||s[*len-1]==';'))
		(*len)--;

	// NULL terminate
	s[*len] = '\0';
}

static void stripParameters( UrlParser *urlParser ) {
	/// @todo ALC reorder parameter?
	/// if we have ?abc=123&def=456
	/// wouldn't it be the same as ?def=456&abc=123

	/// @todo ALC login pages?
	/// should we even spider them?

	static const UrlComponent::Validator s_defaultParamValidator( 0, 0, true, ALLOW_ALL, MANDATORY_NONE );

	// 3 different component that we can remove from
	// - path (we have a much more restrictive criteria on path to avoid removing valid path)
	//   eg: http://www.example.com/search/keywords/chardonnay/osCAdminID/45de8edd68f8bc05e9fde0d2c528a619/sort/3d/
	//
	// - path param
	//   eg: http://www.example.com/search/keywords,chardonnay/osCAdminID,45de8edd68f8bc05e9fde0d2c528a619/sort,3d/
	//   eg: http://www.example.com/search/;keywords=chardonnay;osCAdminID=45de8edd68f8bc05e9fde0d2c528a619;sort=3d/
	//
	// - query string
	//   eg: http://www.example.com/search/?keywords=chardonnay&osCAdminID=45de8edd68f8bc05e9fde0d2c528a619&sort=3d

	// osCommerce (osCsid)
	// eg:
	//   be1566df2284664244ce73ea6bed81fa09d4
	//   b8d15fefe8648f7f77c6e47f7bc0b881
	//   ddtvpkt3rpqdprsagsi52tj5o4
	{
		auto pathMatches = urlParser->matchPath( UrlComponent::Matcher( "osCsid" ) );
		if ( !pathMatches.empty() ) {
			urlParser->removePath( pathMatches, UrlComponent::Validator( 32, 32, true, ALLOW_HEX ) );
			urlParser->removePath( pathMatches, UrlComponent::Validator( 26, 26, true, ( ALLOW_DIGIT | ALLOW_ALPHA ) ) );
		}

		urlParser->removeQueryParam( UrlComponent::Matcher( "osCsid" ), s_defaultParamValidator );
	}

	// osCommerce (osCAdminID)
	// eg:
	//   20d2f836fd203140dc6391b7ba3cdd82
	//   c40fe2ad32efad2e9cc2748a3f1f90cc
	{
		auto pathMatches = urlParser->matchPath( UrlComponent::Matcher( "osCAdminID" ) );
		if ( !pathMatches.empty() ) {
			urlParser->removePath( pathMatches, UrlComponent::Validator( 32, 32, true, ALLOW_HEX ) );
			urlParser->removePath( pathMatches, UrlComponent::Validator( 26, 26, true, ( ALLOW_DIGIT | ALLOW_ALPHA ) ) );
		}

		urlParser->removeQueryParam( UrlComponent::Matcher( "osCAdminID" ), s_defaultParamValidator );
	}

	// XT-commerce
	// eg:
	//   ha6n43ndtnlm53tpqgnclbv7ukkroue9k7m1e2o7t7rr5nb366a1
	//   7ib1soln64vslra70ep2qcvde4s8dsm1
	//   big3ika24atc4j19mlaha6d906
	urlParser->removePath( UrlComponent::Matcher( "XTCsid", MATCH_CASE ), UrlComponent::Validator( 26, 52, true, ( ALLOW_DIGIT | ALLOW_ALPHA ) ) );
	urlParser->removeQueryParam( UrlComponent::Matcher( "XTCsid", MATCH_CASE ), s_defaultParamValidator );

	// ColdFusion
	// http://help.adobe.com/en_US/ColdFusion/9.0/Developing/WSc3ff6d0ea77859461172e0811cbec0c35c-7fef.html#WSc3ff6d0ea77859461172e0811cbec22c24-7cbf

	// ColdFusion (CTOKEN)
	// eg:
	//   e718cd6cc29050df-8051DC1E-C29B-554E-6DFF6B5D2704A9A5
	//   92566684.html
	//   94175176
	//   322257
	{
		auto pathMatches = urlParser->matchPath( UrlComponent::Matcher( "CFTOKEN" ) );
		if ( !pathMatches.empty() ) {
			urlParser->removePath( pathMatches, UrlComponent::Validator( 52, 52, true, ALLOW_ALL ) );
			urlParser->removePath( pathMatches, UrlComponent::Validator( 10, 14, true, ALLOW_ALL, MANDATORY_PUNCTUATION ) );
			urlParser->removePath( pathMatches, UrlComponent::Validator( 6, 0, true, ALLOW_DIGIT ) );
		}

		urlParser->removePathParam( UrlComponent::Matcher( "CFTOKEN" ), s_defaultParamValidator );
		urlParser->removeQueryParam( UrlComponent::Matcher( "CFTOKEN" ), s_defaultParamValidator );
	}

	// ColdFusion (CFID)
	urlParser->removePath( UrlComponent::Matcher( "CFID" ), UrlComponent::Validator( 0, 0, true, ALLOW_DIGIT ) );
	urlParser->removePathParam( UrlComponent::Matcher( "CFID" ), s_defaultParamValidator );
	urlParser->removeQueryParam( UrlComponent::Matcher( "CFID" ), s_defaultParamValidator );

	urlParser->removeQueryParam( UrlComponent::Matcher( "cftokenPass" ), s_defaultParamValidator );

	/// SAP load balancer
	// https://help.sap.com/saphelp_nw70/helpdata/de/f2/d7914b8deb48f090c0343ef1d907f0/content.htm
	urlParser->removePathParam( UrlComponent::Matcher( "saplb_*" ), s_defaultParamValidator );

	// Atlassian
	//   https://developer.atlassian.com/confdev/confluence-plugin-guide/writing-confluence-plugins/form-token-handling
	//   3 different format
	//   eg:
	//     AFP6-ISR2-ZLJY-KBY3|926a76e0017be6a18e889d2ddffb0aaab21865c1|lout
	//     56c1bb338d5ad3ac262dd4e97bda482efc151f30
	//     15BWJdAr0U
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "atl_token" ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 65, 0, true, ALLOW_ALL ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 40, 40, true, ALLOW_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 10, 10, true, ( ALLOW_ALPHA | ALLOW_DIGIT ) ) );
		}
	}

	// psession
	// eg:
	//   491022863920110420135759
	//   7d01p6qvcl2e72j8ivmppk12k0
	//   XUjuplcPFGlJD2ZF5O26ApqAj5ZNEZwZrUKX5kkA
	urlParser->removeQueryParam( UrlComponent::Matcher( "psession" ), UrlComponent::Validator( 24, 0, ( ALLOW_ALPHA | ALLOW_DIGIT ) ) );

	// Galileo
	// eg:
	//   65971783A4.z17ZHFAI
	//   63105032A6BFxgQFfV8
	urlParser->removeQueryParam( UrlComponent::Matcher( "GalileoSession" ), UrlComponent::Validator( 19, 19, ALLOW_ALL ) );

	// postnuke
	//   normally it would be hex string length of 32. but shorter length exist (looks to be chopped off somehow)
	//   eg:
	//     549178d5035b622229a39cd5baf75d2a
	//     4ed3b0a832d4687020b05ce70
	urlParser->removeQueryParam( UrlComponent::Matcher( "POSTNUKESID" ), UrlComponent::Validator( 16, 32 , ( ALLOW_HEX ) ) );

	// jsessionid
	// eg:
	//   C14778D1240A6CFEE5417030DDB37D41
	urlParser->removePath( UrlComponent::Matcher( "jsessionid" ), UrlComponent::Validator( 32, 32, false, ALLOW_HEX ) );
	urlParser->removePathParam( UrlComponent::Matcher( "jsessionid", MATCH_PARTIAL ), UrlComponent::Validator( 20, 0, true ) );
	urlParser->removeQueryParam( UrlComponent::Matcher( "jsessionid", MATCH_PARTIAL ), UrlComponent::Validator( 20, 0, true ) );

	// phpsessid
	// eg:
	//   7711
	//   4g8v6ndp6gnnc4tagn8coam0n7
	//   414c6917961d5b4998973d1613b7926f
	//   qfou95mlih5jjans36kevj2pti7p847v6bl79f03nrvtaadif6u0
	urlParser->removePath( UrlComponent::Matcher( "PHPSESSID" ), UrlComponent::Validator( 26, 32, false, ( ALLOW_ALPHA | ALLOW_DIGIT ) ) );
	urlParser->removeQueryParam( UrlComponent::Matcher( "PHPSESSID", MATCH_PARTIAL ), s_defaultParamValidator );

	// auth_sess
	//   mostly job sites (same group?)
	//   eg:
	//     7ofc7ep3i8g6i2foinq6uks7e0
	//     6ce228460946fc4b3ed154abea1530b8
	urlParser->removeQueryParam( UrlComponent::Matcher( "auth_sess" ), UrlComponent::Validator( 26, 32, true, ( ALLOW_DIGIT | ALLOW_ALPHA ) ) );

	// ps_sess_id
	// eg:
	//   0056c53b03ee56c8b791a5cf061a910d
	urlParser->removeQueryParam( UrlComponent::Matcher( "ps_sess_id" ), UrlComponent::Validator( 32, 32, true, ALLOW_HEX ) );

	// mysid
	// eg:
	//   c357e16d973188ad99cc3e32a059e805
	//   11GeUYNB4fCVXeySSumKM3
	//   hNrnd87gxn9LU0X-N-4TS2
	//   glwcjvci
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "mysid" ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 32, 32, false, ALLOW_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 22, 22, false, ALLOW_ALL, MANDATORY_ALPHA ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 8, 8, false, ALLOW_ALPHA ) );
		}
	}

	// sid
	// eg:
	//   3565de85-0bf0-47d3-8fb3-80120d6b60a6
	//   8E67BB91-5056-9000-2C8C1473A967F273
	//   0b721aa1c34b75fcf41e17304537d965
	//   3KnGJS3ga7ae891-33115175851.04
	//   v0uqho4nv0mnghv4ap3ieeqp94
	//   K6FYyt
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "sid" ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 30, 0, false, ALLOW_ALL ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 26, 26, false, ( ALLOW_ALPHA | ALLOW_DIGIT ) ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 6, 6, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA_LOWER | MANDATORY_ALPHA_UPPER ) ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 6, 6, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA_LOWER | MANDATORY_DIGIT ) ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 6, 6, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA_UPPER | MANDATORY_DIGIT ) ) );
		}
	}

	// SES
	// eg:
	//   74339eda735516fd51ed1c5eb6bc76ceav
	//   39a11261f58150fd4327a80da6daafa0
	//   99cj5cbf6g8irau20h1hkvr8o6
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "ses" ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 34, 34, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA | MANDATORY_DIGIT ) ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 32, 32, false, ALLOW_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 26, 26, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA | MANDATORY_DIGIT ) ) );
		}
	}

	// s
	// eg:
	//   4d9ae8a969305848227e5d6d7d0fb9672bd38d96
	//   81cfba6ed9b66a8ad0df43c2f3d259bd
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "s" ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 40, 40, false, ALLOW_HEX, MANDATORY_ALPHA_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 32, 32, false, ALLOW_HEX, MANDATORY_ALPHA_HEX ) );
		}
	}

	// session_id
	// eg:
	//   NiHhUceSP6At57u0
	//   ospnr7npc97urgoi1p9i9kd1e4
	urlParser->removeQueryParam( UrlComponent::Matcher( "session_id" ), UrlComponent::Validator( 16, 0, false, ALLOW_ALL, MANDATORY_ALPHA ) );

	// sessionid
	// eg:
	//   094104BqFHWLmUCiZAMvgboVyVFiIKDqRPJCxIUMZIPNkMVJVK
	//   1a0d43d9a6753940649bbaeb56f01176
	//   ej3fa4fe7eikfb8ej1fd6
	//   ObUlshp63oxfnZzvCzwe
	//   mN3XmQ{hXgsK8jY7VUm8
	urlParser->removeQueryParam( UrlComponent::Matcher( "sessionid" ), UrlComponent::Validator( 20, 0, false, ALLOW_ALL, MANDATORY_ALPHA ) );

	// other session id variations

	// sessid (vbSESSID, asesessid, nlsessid, GLBSESSID, sessid, etc ...)
	// eg:
	//   91hpb1p3b69bu0vqruar2fpltf3b509bsdeqh1qtj1p8ugb8rpc0
	//   a12cb492ec7bcc9677916f02913587064d4279ed
	//   50d96959db895a0adbfebd325a4a65e0
	//   f4db3ec33001c9759d095c6432651e39
	//   82d0pbm7f6aa55no7p0rqb37r6
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "sessid", MATCH_PARTIAL ) );
		if ( !queryMatches.empty() ) {
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 52, 52, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA | MANDATORY_DIGIT ) ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 40, 40, false, ALLOW_HEX, MANDATORY_ALPHA_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 32, 32, false, ALLOW_HEX, MANDATORY_ALPHA_HEX ) );
			urlParser->removeQueryParam( queryMatches, UrlComponent::Validator( 26, 26, false, ( ALLOW_ALPHA | ALLOW_DIGIT ), ( MANDATORY_ALPHA | MANDATORY_DIGIT ) ) );
		}
	}

	// session
	// eg:
	//   eRbInbLDoNaEr4gkIju0
	//   vfdplav2ske1blvadpv9du54k3
	//   ARC-1454710019-541634862-12401
	//   ARC-1454807400-18472177182-25788
	//   A5C45BC6DC3B436899C43B9D904FC8DE
	//   7b478486-e52c-46aa-aca8-8cd446fcb79e
	//   39663_1455055828_84298238456ba63d42992a
	//   14185_1455099610_106560567456bb0eda9d317
	//   NlG8XCo5MgpctBTMRut4Gq6J5Z5de9foAe4rh3ikLQYWQmFzLR4zSHuieO8
	//   DMQBEXa5Z-aJ7r67ylAJ_y9H8_S2HTUaIjoafUtOjYuGcxwRefR0Q3xXzyS
	//   bGJL_GuP2eDGwJJzoXM9T3_LRgjAsalqaREGEBDoEERJOIMIL8Wh7Q3K3FcgHtYc9hM6CuJmVKlmmCxjmSYEhwVlOdUEX5RnUXycKSHKO5iAz2_ulWoJOZ1d7QCD2Afn9WPkXkvaJaSgjo7hcfYbBnUOXhedzMolha6kfV7hvf4mRAF700MhB350--QV0wQAur9Rz47QiX8SiRXp_vQDdwInUSfO3PqOwXfBu72w4e-JySzUf7Aj9Ks9ouOUPAn1W_GtORLLT4Gho7-Tb_IwyGVYPKF97f3VMXsTfoFqUvs
	//
	urlParser->removeQueryParam( urlParser->matchQueryParam( UrlComponent::Matcher( "session" ) ),
	                             UrlComponent::Validator( 20, 0, false, ALLOW_ALL, ( MANDATORY_ALPHA | MANDATORY_DIGIT) ) );

	// sess
	//   eg: 4be234480736093ba237bc397fb6e32d
	urlParser->removeQueryParam( UrlComponent::Matcher( "sess" ),
	                             UrlComponent::Validator( 20, 0, false, ( ALLOW_ALPHA | ALLOW_DIGIT ) ) );

	// ts
	// eg:
	//   1422344216175
	//   1425080080316
	urlParser->removeQueryParam( UrlComponent::Matcher( "ts" ), UrlComponent::Validator( 13, 13, false, ALLOW_DIGIT ) );

	// apache dir sort
	//   C={N,M,S,D} O={A,D}
	// eg:
	//   ?C=N;O=A
	if ( urlParser->getQueryParamCount() <= 2 ) {
		auto cQueryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "C", MATCH_CASE ) );
		auto oQueryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "O", MATCH_CASE ) );

		UrlComponent *cUrlComponent = ( cQueryMatches.size() == 1 ) ? cQueryMatches[0] : NULL;
		UrlComponent *oUrlComponent = ( oQueryMatches.size() == 1 ) ? oQueryMatches[0] : NULL;

		if ( cUrlComponent ) {
			if ( cUrlComponent->getValueLen() == 0 ) {
				urlParser->deleteComponent( cUrlComponent );
			} else if ( cUrlComponent->getValueLen() == 1 ) {
				char c = *( cUrlComponent->getValue() );
				if ( c == 'N' || c == 'M' || c == 'S' || c == 'D' ) {
					urlParser->deleteComponent( cUrlComponent );
				}
			}
		}

		if ( oUrlComponent ) {
			if ( oUrlComponent->getValueLen() == 0 ) {
				urlParser->deleteComponent( oUrlComponent );
			} else if ( oUrlComponent->getValueLen() == 1 ) {
				char o = *( oUrlComponent->getValue() );
				if ( o == 'A' || o == 'D' ) {
					urlParser->deleteComponent( oUrlComponent );
				}
			}
		}
	}

	/// @todo ALC token?

	// Skip most common tracking parameters

	// Oracle Eloqua
	// http://docs.oracle.com/cloud/latest/marketingcs_gs/OMCAA/index.html#Help/General/EloquaTrackingParameters.htm
	urlParser->removeQueryParam( "elqTrackId" );
	urlParser->removeQueryParam( "elq" );
	urlParser->removeQueryParam( "elqCampaignId" );
	urlParser->removeQueryParam( "elqaid" );
	urlParser->removeQueryParam( "elqat" );
	urlParser->removeQueryParam( "elq_mid" );
	urlParser->removeQueryParam( "elq_cid" );
	urlParser->removeQueryParam( "elq2" ); // others

	// Google Analytics
	// https://support.google.com/analytics/answer/1033867
	urlParser->removeQueryParam( "utm_source" );
	urlParser->removeQueryParam( "utm_medium" );
	urlParser->removeQueryParam( "utm_term" );
	urlParser->removeQueryParam( "utm_content" );
	urlParser->removeQueryParam( "utm_campaign" );
	urlParser->removeQueryParam( "utm_hp_ref" ); // Lots on huffingtonpost.com
	urlParser->removeQueryParam( "utm_rid" ); // others

	// https://support.google.com/analytics/answer/1033981?hl=en
	// https://support.google.com/ds/answer/6292795?hl=en
	urlParser->removeQueryParam( "gclid" );
	urlParser->removeQueryParam( "gclsrc" );

	// Piwik
	// http://piwik.org/docs/tracking-campaigns/
	// https://plugins.piwik.org/AdvancedCampaignReporting
	urlParser->removeQueryParam( "pk_campaign" );
	urlParser->removeQueryParam( "pk_kwd" );
	urlParser->removeQueryParam( "pk_source" );
	urlParser->removeQueryParam( "pk_medium" );
	urlParser->removeQueryParam( "pk_keyword" );
	urlParser->removeQueryParam( "pk_content" );
	urlParser->removeQueryParam( "pk_cid" );

	// Open Web Analytics
	// https://github.com/padams/Open-Web-Analytics/wiki/Campaign-Tracking
	urlParser->removeQueryParam( "owa_medium" );
	urlParser->removeQueryParam( "owa_source" );
	urlParser->removeQueryParam( "owa_campaign" );
	urlParser->removeQueryParam( "owa_ad" );
	urlParser->removeQueryParam( "owa_ad_type" );

	// Webtrends
	// http://help.webtrends.com/en/queryparameters/index.html
	urlParser->removeQueryParam( "wt.mc_id" );

	// Mailchimp
	// https://apidocs.mailchimp.com/api/how-to/ecommerce.php
	urlParser->removeQueryParam( "mc_cid" );
	urlParser->removeQueryParam( "mc_eid" );

	// Marketo
	// http://developers.marketo.com/documentation/websites/lead-tracking-munchkin-js/
	urlParser->removeQueryParam( "mkt_tok" );

	// trk
	// eg:
	//   ppro_cprof
	//   prc-basic
	urlParser->removeQueryParam( UrlComponent::Matcher( "trk" ),
	                             UrlComponent::Validator( 0, 0, false, ALLOW_ALL, ( MANDATORY_ALPHA | MANDATORY_PUNCTUATION ) ) );

	// who
	// eg:
	//   r,Usyg2mo/krON58h7Cqp0HHHvPhsMdK5lNmP76/O/gxQb/ObopGwS3yJwoT241Hf8EMrMDicKKYtMqLKqmtywdZFGbvS6J6jbKbUd5HzTkv_FxyTEsYw1rLJr9LHquA3O
	//   r,yrl2BJY6LMkbtXa9k/lflCwQqzDqf/AF7zFIQoBAhI_t6U_gJztkZ/8ABugLiijm2NRXjt_LYh56mwmTv5cCNuIkgnB2cLFEfL62Gaoyddeh89cXgi9UqjWLP/Y1lD/4watUuyy2WINYipnkSygRLQ--
	//   r,nEBHD2D/_wnDIxXmNMZRjB1wQZikW7uTA8ZXGmCH3a1IvIXSpSv0QicLoCGpTnsBe2QR7xzvq2i2JeKu2AbpgLJaexxw5VON6yG8DP2t5oFhOdoM/kuVnhIt4PEVt1UwqKBNApZk56tTem_r5wqaF4ko65Bo5i7J67PUNHOZs3U-
	//   r,0/asSWWd2MeHwFRbMqZP42yZoh0UlWB2zyP9nAoa3ejKyLPsBjxivhuAY2RH6r94BV2DcmQQYxk6MYZD4Uo6cb30qgNTwVY/_rl_BjRSWosgbpRtPuMytbSX0OmxKuNedtcT27C3fJG/oia/88wI_Ec5PIerpxyPLAgXEsi78vAyuZAXymqhujGGTf6ACryR
	//   r,rW75z4HBqJegN3eAao88RaQcHsIgPXhAP/K1KCbI3x6dMrYllBZLlVfpuL_C0IQed0WspcLWMeT79fzDoAnb0qioGuFSnCHaZXYoH5_GZsWESFdk4CznUlTZuyeTFKsu9xblmYa56ShIKUyILXaFAI8HbNh7dpaXr7q66jIOuo_0r2_GFlbGaSScvbnAWWjH/dMPW8UZsTetZ2a9tqYaHQ--
	{
		auto queryMatches = urlParser->matchQueryParam( UrlComponent::Matcher( "who" ) );
		for ( auto it = queryMatches.begin(); it != queryMatches.end(); ++it ) {
			if ( (*it)->getValueLen() <= 130 && memcmp( (*it)->getValue(), "r,", 2 ) == 0 ) {
				urlParser->deleteComponent( *it );
			}
		}
		urlParser->removeQueryParam( UrlComponent::Matcher( "who" ), UrlComponent::Validator( 130, 0, false, ALLOW_ALL ) );
	}


	// Misc
	urlParser->removeQueryParam( "partnerref" );

	/// @todo ALC redirect ??
	/// redirect_to, redirect, redirect_url

	/// @todo ALC referer??
	/// /referer/, referer=


	/// @todo ALC cater for more affiliate links here

	// only check domain specific logic when we have a domain
	if ( urlParser->getDomain() ) {
		if ( strncmp( urlParser->getDomain(), "amazon.", 7 ) == 0 ) {
			// amazon
			// https://www.reddit.com/r/GameDeals/wiki/affiliate

			// affiliate
			urlParser->removeQueryParam( "tag" );

			// wishlist
			urlParser->removeQueryParam( "coliid" );
			urlParser->removeQueryParam( "colid" );

			// reference
			urlParser->removeQueryParam( "ref" );
			urlParser->removePathParam( UrlComponent::Matcher( "ref" ),
			                            UrlComponent::Validator( 0, 0, false, ALLOW_ALL, MANDATORY_PUNCTUATION ) );
		} else if ( strncmp( urlParser->getDomain(), "ebay.", 5 ) == 0 ) {
			// ebay
			// http://www.ebaypartnernetworkblog.com/en/2009/05/new-link-generator-tool-additional-information/

			urlParser->removeQueryParam( "icep_ff3" );
			urlParser->removeQueryParam( "pub" );
			urlParser->removeQueryParam( "toolid" );
			urlParser->removeQueryParam( "campid" );
			urlParser->removeQueryParam( "customid" );
			urlParser->removeQueryParam( "afepn" );
			urlParser->removeQueryParam( "pid" );
		}
	}
}

// . url rfc = http://www.blooberry.com/indexdot/html/topics/urlencoding.htm
// . "...Only alphanumerics [0-9a-zA-Z], the special characters "$-_.+!*'()," 
//    [not including the quotes - ed], and reserved characters used for their 
//    reserved purposes may be used unencoded within a URL."
// . i know sun.com has urls like "http://sun.com/;$sessionid=123ABC$"
// . url should be ENCODED PROPERLY for this to work properly
void Url::set( const char *t, int32_t tlen, bool addWWW, bool stripParams, bool stripPound, bool stripCommonFile,
               int32_t titledbVersion ) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(t,tlen);
#endif
	reset();

	if ( ! t || tlen == 0 ) {
		return;
	}

	// we may add a "www." a trailing backslash and \0, ...
	if ( tlen > MAX_URL_LEN - 10 ) {
		log( LOG_LIMIT, "db: Encountered url of length %" PRId32 ". Truncating to %i", tlen, MAX_URL_LEN - 10 );
		tlen = MAX_URL_LEN - 10;
	}

	// . skip over non-alnum chars (except - or /) in the beginning
	// . if url begins with // then it's just missing the http: (slashdot)
	// . watch out for hostname like: -dark-.deviantart.com(yes, it's real)
	// . so all protocols are hostnames MUST start with alnum OR hyphen
	while ( tlen > 0 && !is_alnum_a( *t ) && *t != '-' && *t != '/' ) {
		t++;
		tlen--;
	}

	// . stop t at first space or binary char
	// . url should be in encoded form!
	int32_t i;
	int32_t nonAsciiPos = -1;
	for ( i = 0 ; i < tlen ; i++ )	{
		if ( is_wspace_a(t[i]) ) {
			break; // no spaces allowed
		}

		if ( ! is_ascii(t[i]) ) {
			// Sometimes the length with the null is passed in, 
			// so ignore nulls FIXME?
			if ( t[i] ) {
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

#ifdef _VALGRIND_
		char vbits;
		(void)VALGRIND_GET_VBITS(t+tlen,vbits,1);
		VALGRIND_MAKE_MEM_DEFINED(t+tlen,1);
#endif
		char tmp = t[tlen];

		if (t[tlen]) {
			((char*)t)[tlen] = '\0'; //hack
		}
#ifdef _VALGRIND_
		(void)VALGRIND_SET_VBITS(t+tlen,vbits,1);
#endif

		log(LOG_DEBUG, "build: attempting to decode unicode url %s pos at %" PRId32, t, nonAsciiPos);

		if ( tmp ) {
			( (char *)t )[tlen] = tmp;
		}

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

		while ( p < pend && *p != '/' ) {
			const char *labelStart = p;
			uint32_t tmpBuf[MAX_URL_LEN];
			int32_t tmpLen = 0;

			while ( p < pend && *p != '.' && *p != '/' )
				p++;
			int32_t labelLen = p - labelStart;

			bool tryLatin1 = false;
			// For utf8 urls
			p = labelStart;
			bool labelIsAscii = true;

			// Convert the domain to code points and copy it to tmpbuf to be punycoded
			for ( ; p - labelStart < labelLen; p += utf8Size( tmpBuf[tmpLen] ), tmpLen++ ) {
				labelIsAscii &= is_ascii( *p );
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

				// too long?
				if ( newUrlLen + 12 >= MAX_URL_LEN ) {
					break;
				}

				char stored = urlEncode ( &encoded[newUrlLen], 12 , p , cs );
				p += cs;
				newUrlLen += stored;

				continue;
			}

			if (is_wspace_a(*p)) {
				break;
			}

			if (newUrlLen >= MAX_URL_LEN) {
				break;
			}

			encoded[newUrlLen++] = *p++;
		}

		encoded[newUrlLen] = '\0';
		return this->set( encoded, newUrlLen, addWWW, stripParams, stripPound, stripCommonFile, titledbVersion );
	}

	// truncate length to the first occurence of an unacceptable char
	tlen = i;

	// . jump over http:// if it starts with http://http://
	// . a common mistake...
	while ( tlen > 14 && ! strncasecmp ( t , "http://http://" , 14 ) ) {
		t += 7;
		tlen -= 7;
	}

	// strip the "#anchor" from http://www.xyz.com/somepage.html#anchor"
    int32_t anchorPos = 0;
    int32_t anchorLen = 0;
    for ( int32_t i = 0 ; i < tlen ; i++ ) {
		if ( t[i] != '#' ) {
			continue;
		}

		// ignore anchor if a ! follows it. 'google hash bang hack'
		// which breaks the web and is now deprecated, but, there it is
		if ( i+1<tlen && t[i+1] == '!' ) {
			continue;
		}

		anchorPos = i;
		anchorLen = tlen - i;

		if ( stripPound ) {
			tlen = i;
		}
		break;
    }

	// copy to "s" so we can NULL terminate it
	char s [ MAX_URL_LEN ];
	int32_t len = tlen;
	// store filtered url into s
	gbmemcpy ( s , t , tlen );
	s[len]='\0';

	if (titledbVersion <= 122) {
		if ( stripParams ) {
			stripParametersv122( s, &len );
		}
	} else {
		UrlParser urlParser( s, len );

		if ( stripParams ) {
			stripParameters( &urlParser );
		}

		// rebuild url
		strcpy( s, urlParser.unparse() );
		len = strlen(s);
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

	// . set our m_ip if hostname is in a.b.c.d format
	// . this returns 0 if not a valid ip string
	m_ip = atoip ( m_host , m_hlen );

	// advance i to the : for the port, if it exists
	i = j;

	// NULL terminate m_host for getTLD(), getDomain() and strchr() below
	m_host [ m_hlen ] = '\0';

	// use ip as domain if we're just an ip address like 192.0.2.1
	if ( m_ip ) {
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
	else if ( ( m_tld = ::getTLD ( m_host, m_hlen ) ) && m_tld > m_host ) {
		// set m_domain if we had a tld that's not equal to our host
		m_tldLen = gbstrlen ( m_tld  );
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
	if ( ! m_ip && addWWW && m_host == m_domain  && strchr(m_host,'.') ) {
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
	if ( m_slen==3 && strncmp(m_scheme, "ftp"  ,3)==0 ) m_defPort =  21;
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
		// now read our port
		m_port = atol2 ( s + (i + 1) , j - (i + 1) );
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
	if ( s[i]=='\0' || s[i] != '/') {
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
		// deal with current directories in the m_path
		if ( s[i] == '.'  &&  m_url[m_ulen-1] == '/' && 
		     (i+1 == j || s[i+1]=='/'))	continue;
		// . deal with damned ..'s in the m_path
		// . if next 2 chars are .'s and last char we wrote was '/'
		if ( s[i] == '.' && s[i+1]=='.' && m_url[m_ulen-1] == '/' ) {
			// dont back up over first / in path
			if ( m_url + m_ulen - 1 > m_path ) m_ulen--;
			while ( m_url[m_ulen-1] != '/'   ) m_ulen--;
			// skip i to next / after these 2 dots
			while ( s[i] && s[i]!='/' ) i++;
			continue;
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

	// add the anchor after
	m_anchor = NULL;
	m_anchorLen = anchorLen;
	if ( anchorLen > 0 &&
	     m_ulen + anchorLen + 2 < MAX_URL_LEN ) {
		m_anchor = &m_url[m_ulen+1];
		gbmemcpy(&m_url[m_ulen+1], &t[anchorPos], anchorLen);
		m_url[m_ulen+1+anchorLen] = '\0';
	}
}

// hostname must also be www or NULL to be a root url
bool Url::isRoot() const {
	if ( m_plen    != 1              ) return false;
	if ( !m_path || m_path[0] != '/' ) return false;
	if ( m_query                     ) return false;
	// for now we'll let all thos *.deviantart.com names clog us up
	// because i don't want to dis' stuff like espn.go.com
	return true;
}

bool Url::isSimpleSubdomain ( ) const {
	// if hostname is same as domain, it's passes
	if ( m_host == m_domain && m_hlen == m_dlen ) return true;
	// if host is not "www." followed by domain, it's NOT
	if ( m_hlen != m_dlen + 4 ) return false;
	if ( strncmp ( m_host , "www." , 4 ) == 0 ) return true;
	return false;
}

// . get length of sub-url #j
// . basically like adding j /.. to the end of the url
// . sub-url #0 is the full url
// . includes /~ as it's own path
int32_t Url::getSubUrlLen ( int32_t j ) const {

	// assume it's the whole url
	int32_t len = m_ulen;

	// subtract the m_query (cgi) part at the end of the url
	if ( m_query ) len -= m_qlen + 1; //and the ?
	
	// return the full url (without m_query) if j is 0
	if ( j == 0 ) return len;

	// . start right past the http://m_host.domain.com/
	int32_t start = m_slen + 3 + m_hlen + 1 + m_portLen ;
	while ( len > start ) {
		if ( m_url [ len - 1 ] == '/'                            ) j--;
		if ( m_url [ len - 2 ] == '/' && m_url [ len - 1 ] == '~') j--;
		// include this backslash (or ~) in the sub-url
		if ( j == 0 ) return len;
		// shrink by one character
		len--;
	}

	// return 0 if jth sub-url does not exist
	return 0;
}

// . similar to getSubUrlLen() above but only works on the path
// . if j is 0 that's the whole url path!
int32_t Url::getSubPathLen ( int32_t j ) const {
	int32_t subUrlLen = getSubUrlLen ( j );
	if ( subUrlLen <= 0 ) return 0; 
	// . the subPath length includes the root backslash
	// . portLen includes the whole :8080 thing (for non default ports)
	return subUrlLen - m_slen - 3 - m_hlen - m_portLen; 
}

void Url::print() {
	logf( LOG_DEBUG, "Url::url        : %s", m_url );
	logf( LOG_DEBUG, "Url::host       : %.*s", m_hlen, m_host );
	logf( LOG_DEBUG, "Url::ip         : %" PRId32, m_ip );
	logf( LOG_DEBUG, "Url::scheme     : %.*s", m_slen, m_scheme );
	logf( LOG_DEBUG, "Url::path       : %.*s", m_plen, m_path );
	logf( LOG_DEBUG, "Url::query      : %s", m_query );
	logf( LOG_DEBUG, "Url::port       : %" PRId32, m_port );
	logf( LOG_DEBUG, "Url::domain     : %.*s", m_dlen, m_domain );
	logf( LOG_DEBUG, "Url::tld        : %.*s", m_tldLen, m_tld );
	logf( LOG_DEBUG, "Url::mid domain : %.*s", m_mdlen, m_domain );
	logf( LOG_DEBUG, "Url::is root    : %i", isRoot() );
}

int32_t  Url::getPathDepth ( bool countFilename ) const {
	const char *s     = m_path + 1;
	const char *send  = m_url + m_ulen;
	int32_t  count = 0;
	while ( s < send ) if ( *s++ == '/' ) count++;
	// if we're counting the filename as a path component...
	if ( countFilename && *(send-1) != '/' ) count++;
	return count;
}

bool Url::isHostWWW ( ) const {
	if ( m_hlen < 4 ) return false;
	if ( m_host[0] != 'w' ) return false;
	if ( m_host[1] != 'w' ) return false;
	if ( m_host[2] != 'w' ) return false;
	if ( m_host[3] != '.' ) return false;
	return true;
}

// . is the url a porn/spam url?
// . i use /usr/share/dict/words to check for legit words
// . if it's int32_t and has 4+ hyphens, consider it spam
// . if you add a word here, add it to PageResults.cpp:isQueryDirty()
bool Url::isSpam() const {
	// store the hostname in a buf since we strtok it
	char s [ MAX_URL_LEN ];
	// don't store the .com or .org while searching for isSpam
	int32_t  slen = m_hlen - m_tldLen - 1;
	gbmemcpy ( s , m_host , slen );
	if ( ! m_domain ) return false;
	if ( ! m_dlen   ) return false;
	//int32_t  len = m_dlen;
	//gbmemcpy ( s , m_domain , len );
	// if tld is gov or edu or org, not porn
	if ( m_tldLen >= 3 && strncmp ( m_tld , "edu" , 3 )==0 ) return false;
	if ( m_tldLen >= 3 && strncmp ( m_tld , "gov" , 3 )==0 ) return false;
	// NULL terminate for strstr
	s[slen]='\0';
	// . if there is 4 or more hyphens, and hostLen > 30 consider it spam
	// . actually there seems to be a lot of legit sites with many hyphens
	if ( slen > 30 ) {
		int32_t count = 0;
		char *p = s;
		while ( *p ) if ( *p++ == '-' ) count++;
		if ( count >= 4 ) return true;
	}

	//
	// TODO: use getMatch()!!!! +pts -pts system
	// 

	// check each thing separated by periods for porn
	char *send = s + slen;
	char *p    = s;

 loop:
	// if done, return
	if ( p >= send ) return false;
	// find the next period or hyphen
	char *pend = p;
	while ( pend < send && *pend != '.' && *pend !='-' ) pend++;
	// ok NULL terminate it
	*pend = '\0';
	// check that
	if ( isSpam ( p , pend - p ) ) return true;
	// point to next
	p = pend + 1;
	// loop back
	goto loop;
}

bool Url::isSpam ( char *s , int32_t slen ) const {	

	// no need to indent below, keep it clearer
	if ( ! isAdult ( s, slen ) ) return false;

	// check for naughty words. Split words to deep check if we're surely 
	// adult. Required because montanalinux.org is showing up as porn 
	// because it has 'anal' in the hostname.
	// send each phrase seperately to be tested.
	// hotjobs.yahoo.com
	char *a = s;
	char *p = s;
	bool foundCleanSequence = false;
	char splitWords[1024];
	char *splitp = splitWords;
	while ( p < s + slen ){
		while ( p < s + slen && *p != '.' && *p != '-' )
			p++;
		bool isPorn = false;
		// TODO: do not include "ult" in the dictionary, it is
		// always splitting "adult" as "ad ult". i'd say do not
		// allow it to split a dirty word into two words like that.
		if (g_speller.canSplitWords( a, p - a, &isPorn, splitp, langEnglish )){
			if ( isPorn ){
				log(LOG_DEBUG,"build: identified %s as "
				    "porn  after splitting words as "
				    "%s", s, splitp);
				return true;
			}
			foundCleanSequence = true;
			// keep searching for some porn sequence
		}
		p++;
		a = p;
		splitp += gbstrlen(splitp);
	}
	// if we found a clean sequence, its not porn
	if ( foundCleanSequence ) {
		log(LOG_INFO,"build: did not identify url %s "
		    "as porn after splitting words as %s", s, splitWords);
		return false;
	}
	// we tried to get some seq of words but failed. Still report
	// this as porn, since isAdult() was true
	logf ( LOG_DEBUG,"build: failed to find sequence of words to "
	      "prove %s was not porn.", s );
	return true;
}


// . remove any session id
// . i'm sick of these tihngs causing dup problems
// . types:
// http://www.b.com/?PHPSESSID=737aec14eb7b360983d4fe39395
// http://www.b.com/cat.cgi/process?mv_session_id=xrf2EY3q&
// http://www.b.com/default?SID=f320a739cdecb4c3edef67e

// http://www.b.com/generic.html;$sessionid$QVBMODQAAAGNA?pid=7
// http://www.b.com/p.jhtml;jsessionid=J4QMFWBG1SPRVWCKUUXCJ0W?stuff=1
// look for ';'
// look for PHPSESSID, session_id, SID, jsessionid
// followed by string of at least 4 letters/numbers
		
//List of extensions NOT to parse
static const char * const s_badExtensions[] = {
        "ai",
        "aif",
        "aifc",
        "aiff",
        "asc",
        "au",
        "avi",
        "bcpio",
        "bin",
        "bmp",
        "bz2",
        //"c",
        //"cc",// c source code, allow
        "ccad",
        "cdf",
        //"class",// text source code file usually, allow
        "cpio",
        "cpt",
        //"csh",
        "css",
        "dcr",
        "dir",
        "dms",
        //"doc",
        "drw",
        "dvi",
        "dwg",
        "dxf",
        "dxr",
        "eps",
        "etx",
        "exe",
        "ez",
        //"f", // ambigous
        "f90",
        "fli",
        "gif",
        "gtar",
        "gz",
        //"h",
        "hdf",
        "hh",
        "hqx",
        //"htm",
        //"html",
        "ice",
        "ief",
        "iges",
        "igs",
        "ips",
        "ipx",
        "jpe",
        "jpeg",
        "jpg",
        //"js",
        "kar",
        "latex",
        "lha",
        "lsp",
        "lzh",
        //"m", // ambiguous
        "man",
        "me",
        "mesh",
        "mid",
        "midi",
        "mif",
        "mime",
        "mov",
        "movie",
        "mp2",
        "mp3",
        "mpe",
        "mpeg",
        "mpg",
        "mpga",
        "ms",
        "msh",
        "nc",
        "oda",
        "pbm",
        "pdb",
        //"pdf",
        "pgm",
        "pgn",
        "png",
        "pnm",
        "pot",
        "ppm",
        "pps",
	// "ppt",
        "ppz",
        "pre",
        "prt",
	// "ps",
        "qt",
        "ra",
        "ram",
        "ras",
        "rgb",
        "rm",
        "roff",
        "rpm",
		"deb", // debian/ubuntu package file
        "rtf",
        "rtx",
        "scm",
        "set",
        "sgm",
        "sgml",
        //"sh", // shells are text files
        "shar",
        "silo",
        "sit",
        "skd",
        "skm",
        "skp",
        "skt",
        "smi",
        "smil",
        "snd",
        "sol",
        "spl",
        "src",
        "step",
        "stl",
        "stp",
        "sv4cpio",
        "sv4crc",
        "swf",
        //"t", // ambiguous ... Mr.T.
        "tar",
        "tcl",
        "tex",
        "texi",
        "texinfo",
        "tif",
        "tiff",
        "tr",
        "tsi",
        "tsp",
        "tsv",
        //"txt",
        "unv",
        "ustar",
        "vcd",
        "vda",
        "viv",
        "vivo",
        "vrml",
        "wav",
        "wrl",
        "xbm",
        "xlc",
        "xll",
        "xlm",
        //"xls",
        "xlw",
        //"xml",
        "xpm",
        "xwd",
        "xyz",
        "zip",//
};//look below, I added 3 more types for TR version 73



static HashTable s_badExtTable;
static bool s_badExtInitialized;

//returns True if the extension is listed as bad
bool Url::hasNonIndexableExtension( int32_t version ) const {
	if ( ! m_extension || m_elen == 0 ) return false;
	if(!s_badExtInitialized) { //if hash has not been created-create one
		int32_t i=0;
		//version 72 and before.
		do {
			int tlen = gbstrlen(s_badExtensions[i]);
			int64_t swh = hash64Lower_a(s_badExtensions[i],tlen);
			if(!s_badExtTable.addKey(swh,(int32_t)50))
			{
				log(LOG_ERROR,"hasNonIndexableExtension: Could not add hash %" PRId64" to badExtTable.", swh);
				return false;
			}
			i++;

		} while(strcmp(s_badExtensions[i],"zip")!=0);


		//version 73 and after.
		if(!s_badExtTable.addKey(hash64Lower_a("wmv", 3),(int32_t)73) ||
		   !s_badExtTable.addKey(hash64Lower_a("wma", 3),(int32_t)73) ||    
		   !s_badExtTable.addKey(hash64Lower_a("ogg", 3),(int32_t)73))
		{
			log(LOG_ERROR,"hasNonIndexableExtension: Could not add hash to badExtTable (2).");
			return false;
		}
		
		// BR 20160125: More unwanted extensions
		if(
			!s_badExtTable.addKey(hash64Lower_a("7z", 2),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("lz", 2),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("xz", 2),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("apk", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("com", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("dll", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("dmg", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("flv", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("gpx", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("ico", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("iso", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("kmz", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("mp4", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("rar", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("svg", 3),(int32_t)122) ||
			!s_badExtTable.addKey(hash64Lower_a("vcf", 3),(int32_t)122) ||
//			!s_badExtTable.addKey(hash64Lower_a("xls", 3),(int32_t)122) ||		// Should be handled by converter (AbiWord)
		   	!s_badExtTable.addKey(hash64Lower_a("lzma", 4),(int32_t)122) ||    
//			!s_badExtTable.addKey(hash64Lower_a("pptx", 4),(int32_t)122) ||		// Should be handled by converter (AbiWord)
			!s_badExtTable.addKey(hash64Lower_a("thmx", 4),(int32_t)122) ||
		   	!s_badExtTable.addKey(hash64Lower_a("zipx", 4),(int32_t)122) ||
//			!s_badExtTable.addKey(hash64Lower_a("xlsx", 4),(int32_t)122) ||		// Should be handled by converter (AbiWord)
		   	!s_badExtTable.addKey(hash64Lower_a("zsync", 5),(int32_t)122) ||    
		   	!s_badExtTable.addKey(hash64Lower_a("torrent", 7),(int32_t)122) ||
		   	!s_badExtTable.addKey(hash64Lower_a("manifest", 8),(int32_t)122)
		   	)
		{
			log(LOG_ERROR,"hasNonIndexableExtension: Could not add hash to badExtTable (3).");
			return false;
		}
		
		s_badExtInitialized = true;
	}


	int myKey = hash64Lower_a(m_extension,m_elen);
	int32_t badVersion = s_badExtTable.getValue(myKey);

	if( badVersion == 0 || badVersion > version ) 
	{
		return false;
	}
	
	return true;
}



// BR 20160115
// Yes, ugly hardcoded stuff again.. Can likely be optimized a bit too..
// List of domains we do not want to store hashes for in posdb for "link:" entries
bool Url::isDomainUnwantedForIndexing() const {
	const char *domain 	= getDomain();				// top domain only, e.g. googleapis.com
	int32_t dlen 	= getDomainLen();
	const char *host		= getHost();				// domain including subdomain, e.g. fonts.googleapis.com
	int32_t hlen	= getHostLen();
	const char *path		= getPath();				// document path, e.g. /bla/doh/doc.html
	int32_t plen	= getPathLen();

	if ( !domain || dlen <= 0 ) return true;

	switch( dlen )
	{
		case 4:
			if( memcmp(domain, "t.co", 4) == 0 )			// Twitter
			{
				return true;
			}
			break;
		case 5:
			if( memcmp(domain, "ow.ly", 5) == 0 ||
				memcmp(domain, "tr.im", 5) == 0 )
			{
				return true;
			}
			break;
		case 6:
			if( memcmp(domain, "bit.ly", 6) == 0 ||
				memcmp(domain, "goo.gl", 6) == 0 )
			{
				return true;
			}
			break;
		case 8:
			if( memcmp(domain, "yimg.com", 8) == 0 )		// Yahoo CDN
			{
				return true;
			}

			if( memcmp(domain, "imdb.com", 8) == 0 )
			{
				if( strnstr( path, "/imdb/embed?", plen ) )
				{
					// http://www.imdb.com/video/imdb/vi706391833/imdb/embed?autoplay=false&width=480
					return true;
				}
			}
			break;
		case 9:
			if( memcmp(domain, "ytimg.com", 9) == 0 ||		// YouTube images
				memcmp(domain, "atdmt.com", 9) == 0 )		// Facebook tracking
			{
				return true;
			}
			break;
		case 10:
			if( memcmp(domain, "tinyurl.cc", 10) == 0 )
			{
				return true;
			}
			if( memcmp(domain, "tumblr.com", 10) == 0 )
			{
				if( plen >= 6 && memcmp(path, "/share", 6) == 0 )
				{
					// https://www.tumblr.com/share			
					return true;
				}
			}
			
			if( memcmp(domain, "google.com", 10) == 0 )
			{
				if( memcmp(host, "plus.", 5) == 0 )
				{
					if( plen >= 7 && memcmp(path, "/share?", 7) == 0 )
					{
						// http://plus.google.com/share?url=http%3A//on.11alive.com/NEIG1r]
						return true;
					}
				}
				if( memcmp(host, "accounts.", 9) == 0 )
				{
					// https://accounts.google.com/
					return true;
				}
				
			}
			break;
		case 11:
			if( memcmp(domain, "tinyurl.com", 11) == 0 ||
				memcmp(domain, "gstatic.com", 11) == 0 )
			{
				return true;
			}

			if( memcmp(domain, "archive.org", 11) == 0 )
			{
				if( memcmp(host, "web.", 4) == 0 &&
						plen > 5 && memcmp(path, "/web/", 5) == 0 )
				{
					// https://web.archive.org/web/*/dr.dk
					return true;
				}
			}

			if( memcmp(domain, "twitter.com", 11) == 0 )
			{
				if( memcmp(host, "search.", 7) == 0 )
				{
					// http://search.twitter.com/search?q=%23trademark
					return true;
				}
				if( plen >= 7 && memcmp(path, "/share?", 7) == 0 )
				{
					// http://twitter.com/share?text=Im%20Sharing%20on%20Twitter&url=http://stackoverflow.com/users/2943186/youssef-subehi&hashtags=stackoverflow,example,youssefusf
					return true;
				}
				if( plen >= 8 && 
					(	memcmp(path, "/search?", 8) == 0 ||
						memcmp(path, "/intent/", 8) == 0 ) )
				{
					// https://twitter.com/search?q=China
					// https://twitter.com/intent/tweet?text=18%20Cocktails%20That%20Are%20Better%20With%20Butter&url=http%3A%2F%2Fwww.eater.com%2Fdrinks%2F2016%2F1%2F14%2F10710202%2Fbutter-cocktails&via=Eater
					// https://twitter.com/intent/retweet?tweet_id=534860467186171904
					// https://twitter.com/intent/favorite?tweet_id=595310844746969089
					return true;
				}
			}
			break;
		case 12:
			if( memcmp(domain, "akamaihd.net", 12) == 0 ||
				memcmp(domain, "vimeocdn.com", 12) == 0 )
			{
				return true;
			}

			if( memcmp(domain, "facebook.com", 12) == 0 )
			{
				if( plen >=8 && memcmp(path, "/sharer/", 8) == 0 )
				{
					// https://www.facebook.com/sharer/sharer.php?u=http%3A%2F%2Fallthingsd.com%2F20120309%2Fgreen-dot-buys-location-app-loopt-for-43-4m%2F%3Fmod%3Dfb
					return true;
				}
			}
			
			if( memcmp(domain, "linkedin.com", 12) == 0 )
			{
				if( plen >= 13 && memcmp(path, "/shareArticle", 13) == 0 )
				{
					// https://www.linkedin.com/shareArticle?
					return true;
				}
			}
			break;
		case 13:
			if( memcmp(domain, "akamaized.net", 13) == 0 ||
				memcmp(domain, "disquscdn.com", 13) == 0 )
			{
				return true;
			}

			if( memcmp(domain, "pinterest.com", 13) == 0 )
			{
				if( plen >= 12 && memcmp(path, "/pin/create/", 12) == 0 )
				{
					// http://www.pinterest.com/pin/create/button/?description=&media=https%3A%2F%2Fcdn1.vox-cdn.com%2Fthumbor%2FrMA6BPH4ZkdBg2RqB9mmZVhYqUs%3D%2F0x77%3A1000x640%2F1050x591%2Fcdn0.vox-cdn.com%2Fuploads%2Fchorus_image%2Fimage%2F48544087%2Fshutterstock_308548907.0.0.jpg&url=http%3A%2F%2Fwww.eater.com%2Fmaps%2Fbest-coffee-taipei
					return true;
				}
			}
			break;
		case 14:
			if(	memcmp(domain, "googleapis.com", 14) == 0 ||
				memcmp(domain, "netdna-cdn.com", 14) == 0 ||
				memcmp(domain, "cloudfront.net", 14) == 0 )
			{
				return true;
			}
			break;
		case 15:
			if( memcmp(domain, "doubleclick.net", 15) == 0 )
			{
				// If subdomain empty or www, keep it. Otherwise trash it.
				// pubads.g.doubleclick.net
				// ad.doubleclick.net
				// ads.g.doubleclick.net
		
				if( hlen != dlen )	// has subdomain, only OK if www
				{
					if( hlen != dlen+4 ||
							memcmp(host,"www.",4) != 0 )
					{
						return true;
					}
				}
			}
			break;
		case 16:
			if( memcmp(domain, "staticflickr.com", 16) == 0 )
			{
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}



// BR 20160115
// Yes, ugly hardcoded stuff again.. Can likely be optimized a bit too..
// List of paths we do not want to store hashes for in posdb for "link:" entries
bool Url::isPathUnwantedForIndexing() const {
	const char *path = getPath();				// document path, e.g. /bla/doh/doc.html
	int32_t plen	= getPathLen();

	if ( !path|| plen <= 0 ) return false;

	if( plen > 8 )
	{
		if( memcmp(path, "/oembed?", 8) == 0 || 
			memcmp(path, "/oembed/", 8) == 0 || 
			memcmp(path, "/wp-json", 8) == 0 )
		{
			// http://www.youtube.com/oembed?url=https%3A%2F%2Fwww.youtube.com%2Fwatch%3Fv%3DLkcZdhamaew&format=xml
			// http://indavideo.hu/oembed/4ff1e92383
			// https://vine.co/oembed/ODibqgXlQpE.xml
			return true;
		}
	}

	if( plen > 9 )
	{
		if( memcmp(path, "/wp-admin/", 10) == 0 )
		{
			return true;
		}
	}

	if( plen > 10 )
	{
		if( memcmp(path, "/xmlrpc.php", 11) == 0 ||
			memcmp(path, "/wp-content", 11) == 0 ||
			memcmp(path, "/wp-uploads", 11) == 0 )
		{
			
			return true;
		}
	}

	if( plen > 11 )
	{
		if( memcmp(path, "/wp-includes", 12) == 0 )
		{
			
			return true;
		}
	}

	if( plen > 12 )
	{
		if( memcmp(path, "/wp-login.php", 13) == 0 )
		{
			return true;
		}
	}

	return false;
}


bool Url::hasMediaExtension ( ) const {

	if ( ! m_extension || ! m_elen || m_elen > 4 ) return false;

	char ext[5];
	int i;
	for(i=0; i < m_elen; i++)
	{
		ext[i] = to_lower_a(m_extension[i]);
	}
	ext[i] = '\0';
	
	switch( m_elen )
	{
		case 3:
			if( 
				memcmp(ext, "avi", 3) == 0 ||
				memcmp(ext, "css", 3) == 0 ||
				memcmp(ext, "gif", 3) == 0 ||
				memcmp(ext, "ico", 3) == 0 ||
				memcmp(ext, "jpg", 3) == 0 ||
				memcmp(ext, "mov", 3) == 0 ||
				memcmp(ext, "mp2", 3) == 0 ||
				memcmp(ext, "mp3", 3) == 0 ||
				memcmp(ext, "mp4", 3) == 0 ||
				memcmp(ext, "mpg", 3) == 0 ||
				memcmp(ext, "png", 3) == 0 ||
				memcmp(ext, "svg", 3) == 0 ||
				memcmp(ext, "wav", 3) == 0 ||
				memcmp(ext, "wmv", 3) == 0 )
			{
				return true;
			}
			break;
		case 4:
			if( memcmp(ext, "mpeg", 4) == 0 ||
				memcmp(ext, "jpeg", 4) == 0 )
			{
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}


bool Url::hasXmlExtension ( ) const {

	if ( ! m_extension || ! m_elen || m_elen > 3 ) return false;

	char ext[5];
	int i;
	for(i=0; i < m_elen; i++)
	{
		ext[i] = to_lower_a(m_extension[i]);
	}
	ext[i] = '\0';
	
	switch( m_elen )
	{
		case 3:
			if( memcmp(ext, "xml", 3) == 0 )
			{
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}


bool Url::hasJsonExtension ( ) const {

	if ( ! m_extension || ! m_elen || m_elen >= 4 ) return false;

	char ext[5];
	int i;
	for(i=0; i < m_elen; i++)
	{
		ext[i] = to_lower_a(m_extension[i]);
	}
	ext[i] = '\0';
	
	switch( m_elen )
	{
		case 4:
			if( memcmp(ext, "json", 4) == 0 )
			{
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}


bool Url::hasScriptExtension ( ) const {

	if ( ! m_extension || ! m_elen || m_elen > 4 ) return false;

	char ext[5];
	int i;
	for(i=0; i < m_elen; i++)
	{
		ext[i] = to_lower_a(m_extension[i]);
	}
	ext[i] = '\0';
	
	switch( m_elen )
	{
		case 2:
			if( memcmp(ext, "js", 2) == 0 )
			{
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}



// see Url.h for a description of this.
bool Url::isLinkLoop ( ) const {
	const char *s          = m_path ;
	const char *send       = m_url + m_ulen;
	int32_t  count         = 0;
	int32_t  components    = 0;
	bool  prevWasDouble = false;
	const char *last     = NULL;
	if (!s) return false;
	// use this hash table to hash each path component in the url
	char  buf [ 5000 ];
	HashTable t; t.set ( 100 , buf , 5000 );
	// grab each path component
	for ( ; s < send ; s++ ) {
		if ( *s != '/' ) continue;
		// ok, add this guy to the hash table, if we had one
		if ( ! last ) { last = s; continue; }
		// give up after 50 components
		if ( components++ >= 50 ) return false;
		// hash him
		uint32_t h = hash32 ( last , s - last );
		// is he in there?
		int32_t slot = t.getSlot ( h );
		// get his val (count)
		int32_t val = 0;
		if ( slot >= 0 ) val = t.getValueFromSlot ( slot );
		// if not in there put him in a slot
		if ( slot < 0 ) {
			last = s;
			t.addKey ( h , 1 );
			continue;
		}
		// increment it
		val++;
		// does it occur 3 or more times? if so, we have a link loop
		if ( val >= 3 ) return true;
		// is it 2 or more? 
		if ( val == 2 ) count++;
		// if we have two such components, then we are a link loop.
		// BUT, we must be a pair!
		if ( count >= 2 && prevWasDouble ) return true;
		// set this so in case next guy is a double
		if ( val == 2 ) prevWasDouble = true;
		else            prevWasDouble = false;
		// add it back after incrementing
		t.setValue ( slot , val );
		// update "last"
		last = s;
	}
	return false;
}		

//
// here are some examples of link loops in urls:
//
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/letters/send/archive/bish/archive/bish/letters/bish/archive/lette\rs/send/archive/letters/send/bish/letters/archive/bish/letters/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/bish/letters/archive/bish/archive/letters/send/archive/letters/send/archive/le\tters/send/archive/letters/send/bish/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/letters/send/archive/bish/archive/bish/\archive/bish/letters/send/archive/letters/archive/letters/send/archive/bish/let\ters/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/letters/send/archive/letters/archive/bish/archive/bish/archive/bi\sh/letters/send/archive/bish/archive/letters/send/bish/archive/bish/letters/sen\d/archive/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/bish/letters/bish/letters/send/archive/\bish/archive/letters/bish/letters/send/archive/bish/letters/send/bish/archive/l\etters/bish/letters/archive/letters/send/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/bish/letters/send/bish/archive/letters/\send/bish/archive/letters/send/archive/letters/bish/archive/bish/archive/letter\s/


bool Url::isIp() const {
	if(!m_host)            return false;
	if(!is_digit(*m_host)) return false; 
	return atoip ( m_host , m_hlen ); 
}

int32_t Url::getHash32WithWWW ( ) const {
	uint32_t hh = hash32n ( "www." );
	int32_t conti = 4;
	hh = hash32_cont ( m_domain , m_dlen , hh , &conti );
	return hh;
}

int32_t Url::getHostHash32 ( ) const {
	return hash32 ( m_host , m_hlen ); 
}

int64_t Url::getHostHash64 ( ) const {
	return hash64 ( m_host , m_hlen ); 
}

int32_t Url::getDomainHash32 ( ) const {
	return hash32 ( m_domain , m_dlen ); 
}

int64_t Url::getDomainHash64 ( ) const {
	return hash64 ( m_domain , m_dlen ); 
}

int32_t Url::getUrlHash32 ( ) const {
	return hash32(m_url,m_ulen); 
}

int64_t Url::getUrlHash64 ( ) const {
	return hash64(m_url,m_ulen); 
}

const char *getHostFast ( const char *url , int32_t *hostLen , int32_t *port ) {
	// point to the url
	const char *pp = url;
	// skip http(s):// or ftp:// (always there?)
	while ( *pp && *pp != ':' ) pp++;
	// skip ://
	pp += 3;
	// point "uhost" to hostname right away
	const char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// advance "pe" over the port
	const char *pe = pp;
	if ( *pp == ':' ) {
		// if port ptr given, do not treat port as part of hostname
		if ( port ) *port = atoi(pp+1);
		// i think this was including :1234 as part of hostname
		// if port was NULL!
		//else while ( *pe && *pe != '/' ) pe++;
	}
	// set length
	if ( hostLen ) *hostLen = pe - uhost;
	return uhost;
}

char *getPathFast ( char *url ) {
	// point to the url
	char *pp = url;
	// skip http(s):// or ftp:// (always there?)
	while ( *pp && *pp != ':' ) pp++;
	// skip ://
	pp += 3;
	// point "uhost" to hostname right away
	//char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	return pe;
}

char *getTLDFast ( char *url , int32_t *tldLen , bool hasHttp ) {
	// point to the url
	char *pp = url;
	// only do this for some
	if ( hasHttp ) {
		// skip http(s):// or ftp:// (always there?)
		while ( *pp && *pp != ':' ) pp++;
		// skip ://
		pp += 3;
	}
	// point "uhost" to hostname right away
	char *uhost = pp;

	// advance "pp" till we hit a / or :<port> or \0
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;

	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' ) {
		while ( *pe && *pe != '/' ) {
			pe++;
		}
	}

	// set length of host
	int32_t uhostLen = pp - uhost;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	bool isIp = true;
	for ( ; *ss && ss<pp ; ss++ ) {
		if ( is_alpha_a( *ss ) ) {
			isIp = false;
			break;
		}
	}

	// if ip, no tld
	if ( isIp ) {
		return NULL;
	}

	// get the tld
	char *tld = ::getTLD ( uhost , uhostLen );

	// if none, done
	if ( ! tld ) {
		return NULL;
	}

	// set length
	if ( tldLen ) {
		*tldLen = pp - tld;
	}

	// return it
	return tld;
}

bool hasSubdomain ( char *url ) {
	// point to the url
	char *pp = url;
	// skip http if there
	if (      pp[0] == 'h' &&
		  pp[1] == 't' &&
		  pp[2] == 't' &&
		  pp[3] == 'p' &&
		  pp[4] == ':' &&
		  pp[5] == '/' &&
		  pp[6] == '/' )
		pp += 7;
	else if ( pp[0] == 'h' &&
		  pp[1] == 't' &&
		  pp[2] == 't' &&
		  pp[3] == 'p' &&
		  pp[4] == 's' &&
		  pp[5] == ':' &&
		  pp[6] == '/' &&
		  pp[7] == '/' )
		pp += 8;
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// are we a root? assume so.
	//char isRoot = true;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	//if ( *pe == '/' && *(pe+1) ) isRoot = false;
	// set length
	int32_t uhostLen = pp - uhost;
	// get end
	//char *hostEnd = uhost + uhostLen;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	while ( *ss && !is_alpha_a(*ss) && ss<pp ) ss++;
	// if we are an ip, say yes
	if ( ss == pp ) return true;
	// get the tld
	char *utld = ::getTLD ( uhost , uhostLen );
	// no tld, then no domain
	if ( ! utld ) return false;
	// the domain, can only be gotten once we know the TLD
	// back up a couple chars
	char *udom = utld - 2;
	// backup until we hit a '.' or hit the beginning
	while ( udom > uhost && *udom != '.' ) udom--;
	// fix http://ok/
	if ( udom < uhost || *udom =='/' ) return false;
	// if we hit '.' advance 1
	if ( *udom == '.' ) udom++;
	// eqal to host? if not, we do have a subdomain
	if ( udom != uhost ) return true;
	// otherwise the hostname equals the domain name
	return false;
}

// returns NULL if url was in bad format and could not get domain. this
// was happening when a host gave us a bad redir url and xmldoc tried
// to set extra doc's robot.txt url to it "http://2010/robots.txt" where
// the host said "Location: 2010 ...".
char *getDomFast ( char *url , int32_t *domLen , bool hasHttp ) {
	// point to the url
	char *pp = url;
	// skip http if there
	if ( hasHttp ) {
		// skip http(s):// or ftp:// (always there?)
		while ( *pp && *pp != ':' ) pp++;
		// skip ://
		pp += 3;
	}
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;

	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;

	// set length
	int32_t uhostLen = pp - uhost;
	// get end
	char *hostEnd = uhost + uhostLen;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	while ( *ss && !is_alpha_a(*ss) && ss<pp ) ss++;
	//bool isIp = false;
	//if ( ss == pp ) isIp = true;
	// if we are an ip, treat special
	if ( ss == pp ) {
		// . might just be empty! like "\0"
		// . fixes core dump from 
		//   http://www.marcom1.unimelb.edu.au/public/contact.html
		//   parsing host email address
		if ( uhostLen == 0 ) return NULL;
		// to be consistent with how Url::m_domain/m_dlen is set we
		// need to remove the last .X from the ip address
		// skip back over digits
		for ( hostEnd-- ; is_digit(*hostEnd); hostEnd-- );
		// must be a period
		if ( *hostEnd != '.' ) { 
			log("url: getDomFast() could not find period for "
			    "hostname in url");
			return NULL;
		}
		// set length
		*domLen = hostEnd - uhost;
		// that's it
		return uhost;
	}
	// get the tld
	char *utld = ::getTLD ( uhost , uhostLen );
	// no tld, then no domain
	if ( ! utld ) return NULL;
	// the domain, can only be gotten once we know the TLD
	// set utldLen
	//int32_t utldLen = hostEnd - utld;
	// back up a couple chars
	char *udom = utld - 2;
	// backup until we hit a '.' or hit the beginning
	while ( udom > uhost && *udom != '.' ) udom--;
	// fix http://ok/
	if ( udom < uhost || *udom =='/' ) return NULL;
	// if we hit '.' advance 1
	if ( *udom == '.' ) udom++;
	// set domain length
	*domLen = hostEnd - udom;
	return udom;
}

// Is it a ping server? It might respond with huge documents with thousands of
// links, which would normally be detected as link spam. This function is kept
// around until we have a better way of handling it  than hardcoded URLs in a
// source file.
bool Url::isPingServer ( ) const {
	return false;
}


// "s" point to the start of a normalized url (includes http://, etc.)
char *getHost ( char *s , int32_t *hostLen ) {
	// skip proto
	while ( *s != ':' ) s++;
	// skip ://
	s += 3;
	// that is the host
	char *host = s;
	// get length of hostname
	for ( s++; *s && *s != '/' ; s++ );
	// that is it
	*hostLen = s - host;
	// return it
	return host;
}

// "s" point to the start of a normalized url (includes http://, etc.)
const char *getScheme ( const char *s , int32_t *schemeLen )
{
	const char *div = strstr(s, "://");
	
	if( !div )
	{
		*schemeLen=0;
		return "";
	}

	*schemeLen = div - s;
	return s;
}

// . return ptrs to the end
// . the character it points to SHOULD NOT BE part of the site
char *getPathEnd ( char *s , int32_t desiredDepth ) {
	// skip proto
	while ( *s != ':' ) s++;
	// skip ://
	s += 3;
	// get length of hostname
	for ( s++; *s && *s != '/' ; s++ );
	// should always have a /
	if ( *s != '/' ) { char *xx=NULL;*xx=0;}
	// skip that
	s++;
	// init depth
	int32_t depth = 0;
	// do a character loop
	for ( ; depth <= desiredDepth && *s ; s++ ) 
		// count the '/'
		if ( *s == '/' ) depth++;
	// return the end
	return s;
	/*
	// save for below
	int32_t saved = depth;
	// keep going
	while ( depth-- > 0 ) {
		for ( s++; *s && *s != '/' && *s != '?' ; s++ );
		// if not enough path components (or cgi), return NULL
		if ( *s != '/' ) return NULL;
	}
	// include the last '/' if we have path components
	if ( saved > 0 ) s++;
	// . we got it
	// . if depth==0 just use "www.xyz.com" as site
	// . if depth==1 just use "www.xyz.com/foo/" as site
	return s;
	*/
}

// . pathDepth==0 for "www.xyz.com"
// . pathDepth==0 for "www.xyz.com/"
// . pathDepth==0 for "www.xyz.com/foo"
// . pathDepth==1 for "www.xyz.com/foo/"
// . pathDepth==1 for "www.xyz.com/foo/x"
// . pathDepth==2 for "www.xyz.com/foo/x/"
// . pathDepth==2 for "www.xyz.com/foo/x/y"
int32_t getPathDepth ( char *s , bool hasHttp ) {
	// skip http:// if we got it
	if ( hasHttp ) {
		// skip proto
		while ( *s != ':' ) s++;
		// must have it!
		if ( ! *s ) { char *xx=NULL;*xx=0; }
		// skip ://
		s += 3;
	}
	// skip over hostname
	for ( s++; *s && *s != '/' ; s++ );
	// no, might be a site like "xyz.com"
	if ( ! *s ) return 0;
	// should always have a /
	if ( *s != '/' ) { char *xx=NULL;*xx=0;}
	// skip that
	s++;
	// init depth
	int32_t depth = 0;
	// do a character loop
	for ( ; *s ; s++ ) {
		// stop if we hit ? or #
		if ( *s == '?' ) break;
		if ( *s == '#' ) break;
		// count the '/'
		if ( *s == '/' ) depth++;
	}
	return depth;
}

char* Url::getDisplayUrl( const char* url, SafeBuf* sb ) {
	const char *urlEnd = url + strlen(url);
	const char *p = url;
	if ( strncmp( p, "http://", 7 ) == 0 )
		p += 7;
	else if ( strncmp(p, "https://", 8 ) == 0 )
		p += 8;

	const char *domEnd = static_cast<const char*>( memchr( p, '/', urlEnd - p ) ) ?: urlEnd;

	bool firstRun = true;
	const char *found = NULL;
	const char *labelCursor = url;

	while( ( found = strstr( labelCursor, "xn--" ) ) && ( found < domEnd ) ) {
		if ( firstRun ) {
			sb->safeMemcpy( url, found - url );
			firstRun = false;
		}

		const char* encodedStart = found + 4;
		uint32_t decoded [ MAX_URL_LEN];
		size_t decodedLen = MAX_URL_LEN - 1 ;
		const char* labelEnd = encodedStart;
		while( labelEnd < domEnd && *labelEnd != '/' && *labelEnd != '.' ) {
			labelEnd++;
		}

		punycode_status status = punycode_decode(labelEnd - encodedStart, encodedStart, &decodedLen, decoded, NULL);
		if ( status != 0 ) {
			log( "build: Bad Engineer, failed to depunycode international url %s", url );
			sb->safePrintf("%s", labelCursor);
			sb->nullTerm();
			return sb->getBufStart();
		}

		sb->utf32Encode( decoded, decodedLen );

		if ( *labelEnd == '.' ) {
			sb->pushChar( *labelEnd++ );
		}

		labelCursor = labelEnd;
	}

    // Copy in the rest
    sb->safePrintf("%s", labelCursor);
    sb->nullTerm();
    return sb->getBufStart();
}
