#include <gtest/gtest.h>
#include <TitleRecVersion.h>

#include "UrlParser.h"

void checkResult(int version, const char *expected, const char *result, size_t resultLen) {
	size_t expectedLen = expected ? strlen(expected) : 0;

	std::stringstream ss;
	ss << "version=" << version << " expected='" << expected << "'" << " result='" << result << "' resultLen=" << resultLen;
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
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSchemeUppercase) {
	std::string url("HTTP://www.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "HTTP", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSchemeHttps) {
	std::string url("https://www.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "https", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "https://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSchemeNone) {
	std::string url("www.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, NULL, urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSchemeRelative) {
	std::string url("//www.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, NULL, urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseUserInfo) {
	std::string url("http://username:password@www.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "username:password@www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://username:password@www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseUserInfoPort) {
	std::string url("http://username:password@www.example.com:8080/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "username:password@www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://username:password@www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParsePort) {
	std::string url("http://www.example.com:8080/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParsePortSchemeNone) {
	std::string url("www.example.com:8080/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, NULL, urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseIP) {
	std::string url("http://127.0.0.1/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "127.0.0.1", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "127.0.0", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://127.0.0.1/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseIPPort) {
	std::string url("http://127.0.0.1:8080/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "127.0.0.1:8080", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "127.0.0", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://127.0.0.1:8080/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSubdomainNone) {
	std::string url("http://example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSubdomainMultiple) {
	std::string url("http://abc.def.ghi.jkl.example.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "abc.def.ghi.jkl.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://abc.def.ghi.jkl.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseTLDUnknown) {
	std::string url("http://subdomain.example.abcde/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "subdomain.example.abcde", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.abcde", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://subdomain.example.abcde/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseTLDNone) {
	std::string url("http://ok/");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "ok", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, NULL, urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://ok/", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSLD) {
	std::string url("http://subdomain.example.co.uk/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "subdomain.example.co.uk", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.co.uk", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://subdomain.example.co.uk/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseSLDUnknown) {
	std::string url("http://subdomain.example.fuel.aero/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "subdomain.example.fuel.aero", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "fuel.aero", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://subdomain.example.fuel.aero/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseHostV123) {
	std::string url("http://www.example.com");

	int version = 123;
	UrlParser urlParser(url.c_str(), url.size(), version);

	checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
	checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
	checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

	urlParser.unparse();
	checkResult(version, "http://www.example.com", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
}

TEST(UrlParserTest, ParseHost) {
	std::string url("http://www.example.com");
	for (int version = 124; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.example.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "example.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();

		// bug fix in v124
		checkResult(version, "http://www.example.com/", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseHostUppercase) {
	std::string url("http://www.EXAMPLE.com/param1=abc-123");
	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.EXAMPLE.com", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "EXAMPLE.com", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.example.com/param1=abc-123", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}

TEST(UrlParserTest, ParseIDN) {
	std::string url("http://www.xn--relgeroskilde-5fb0y.dk/");

	for (int version = 123; version <= TITLEREC_CURRENT_VERSION; ++version) {
		UrlParser urlParser(url.c_str(), url.size(), version);

		checkResult(version, "http", urlParser.getScheme(), urlParser.getSchemeLen());
		checkResult(version, "www.xn--relgeroskilde-5fb0y.dk", urlParser.getAuthority(), urlParser.getAuthorityLen());
		checkResult(version, "xn--relgeroskilde-5fb0y.dk", urlParser.getDomain(), urlParser.getDomainLen());

		urlParser.unparse();
		checkResult(version, "http://www.xn--relgeroskilde-5fb0y.dk/", urlParser.getUrlParsed(), urlParser.getUrlParsedLen());
	}
}
