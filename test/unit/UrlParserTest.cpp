#include <gtest/gtest.h>

#include "UrlParser.h"

void checkResult(const char *expected, const char *result, size_t resultLen) {
	size_t expectedLen = expected ? strlen(expected) : 0;

	std::stringstream ss;
	ss << "expected='" << expected << "'" << " result='" << result << "' resultLen=" << resultLen;
	SCOPED_TRACE(ss.str());

	ASSERT_EQ(expectedLen, resultLen);
	if (expected) {
		EXPECT_EQ(strncmp(expected, result, resultLen), 0);
	} else {
		EXPECT_EQ(expected, result);
	}
}

TEST(UrlParserTest, ParseScheme) {
	std::string url("http://www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSchemeUppercase) {
	std::string url("HTTP://www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("HTTP", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSchemeHttps) {
	std::string url("https://www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("https", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("https://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSchemeNone) {
	std::string url("www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult(NULL, urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSchemeRelative) {
	std::string url("//www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult(NULL, urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseUserInfo) {
	std::string url("http://username:password@www.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("username:password@www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://username:password@www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseUserInfoPort) {
	std::string url("http://username:password@www.example.com:8080/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("username:password@www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://username:password@www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParsePort) {
	std::string url("http://www.example.com:8080/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParsePortSchemeNone) {
	std::string url("www.example.com:8080/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult(NULL, urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseIP) {
	std::string url("http://127.0.0.1/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("127.0.0.1", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("127.0.0", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://127.0.0.1/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseIPPort) {
	std::string url("http://127.0.0.1:8080/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("127.0.0.1:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("127.0.0", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://127.0.0.1:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSubdomainNone) {
	std::string url("http://example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSubdomainMultiple) {
	std::string url("http://abc.def.ghi.jkl.example.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("abc.def.ghi.jkl.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://abc.def.ghi.jkl.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseTLDUnknown) {
	std::string url("http://subdomain.example.abcde/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("subdomain.example.abcde", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.abcde", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://subdomain.example.abcde/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseTLDNone) {
	std::string url("http://ok/");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("ok", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult(NULL, urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://ok/", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSLD) {
	std::string url("http://subdomain.example.co.uk/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("subdomain.example.co.uk", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.co.uk", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://subdomain.example.co.uk/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseSLDUnknown) {
	std::string url("http://subdomain.example.fuel.aero/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("subdomain.example.fuel.aero", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("fuel.aero", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://subdomain.example.fuel.aero/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseHost) {
	std::string url("http://www.example.com");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseHostUppercase) {
	std::string url("http://www.EXAMPLE.com/param1=abc-123");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.EXAMPLE.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("EXAMPLE.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseIDN) {
	std::string url("http://www.xn--relgeroskilde-5fb0y.dk/");
	UrlParser urlParser(url.c_str(), url.size());

	checkResult("http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult("www.xn--relgeroskilde-5fb0y.dk", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult("xn--relgeroskilde-5fb0y.dk", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult("http://www.xn--relgeroskilde-5fb0y.dk/", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}
