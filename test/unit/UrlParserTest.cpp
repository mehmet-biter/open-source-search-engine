#include "gtest/gtest.h"

#include "UrlParser.h"

void checkResult( const char *expected, const char *result, size_t resultLen ) {
	size_t expectedLen = strlen( expected );

	std::stringstream ss;
	ss << "expected='" << expected << "'" << " result='" << result << "' resultLen=" << resultLen;
	SCOPED_TRACE( ss.str() );

	ASSERT_EQ( expectedLen, resultLen );
	EXPECT_EQ( strncmp( expected, result, resultLen ), 0 );
}

TEST( UrlParserTest, ParseScheme ) {
	std::string url( "http://www.example.com/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseSchemeNone ) {
	std::string url( "www.example.com/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseUserInfo ) {
	std::string url( "http://username:password@www.example.com/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "username:password@www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseUserInfoPort ) {
	std::string url( "http://username:password@www.example.com:8080/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "username:password@www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParsePort ) {
	std::string url( "http://www.example.com:8080/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParsePortSchemeNone ) {
	std::string url( "www.example.com:8080/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseIP ) {
	std::string url( "http://127.0.0.1/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "127.0.0.1", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "127.0.0", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseIPPort ) {
	std::string url( "http://127.0.0.1:8080/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "127.0.0.1:8080", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "127.0.0", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseSubdomainNone ) {
	std::string url( "http://example.com/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "example.com", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseSubdomainMultiple ) {
	std::string url( "http://abc.def.ghi.jkl.example.com/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "abc.def.ghi.jkl.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.com", urlParser.getDomain(), urlParser.getDomainLen() );

}

TEST( UrlParserTest, ParseTLDUnknown ) {
	std::string url( "http://subdomain.example.abcde/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "subdomain.example.abcde", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.abcde", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseTLDNone ) {
	std::string url( "http://ok/" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "ok", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseSLD ) {
	std::string url( "http://subdomain.example.co.uk/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "subdomain.example.co.uk", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "example.co.uk", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseSLDUnknown ) {
	std::string url( "http://subdomain.example.fuel.aero/param1=abc-123" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "subdomain.example.fuel.aero", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "fuel.aero", urlParser.getDomain(), urlParser.getDomainLen() );
}

TEST( UrlParserTest, ParseIDN ) {
	std::string url( "http://www.xn--relgeroskilde-5fb0y.dk/" );
	UrlParser urlParser( url.c_str(), url.size() );

	checkResult( "www.xn--relgeroskilde-5fb0y.dk", urlParser.getAuthority(), urlParser.getAuthorityLen() );
	checkResult( "xn--relgeroskilde-5fb0y.dk", urlParser.getDomain(), urlParser.getDomainLen() );
}
