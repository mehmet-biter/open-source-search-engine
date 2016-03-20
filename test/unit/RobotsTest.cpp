#include "gtest/gtest.h"
#include "Robots.h"
#include "Url.h"
#include "Log.h"

#define TEST_DOMAIN "http://example.com"

//
// helper method
//

static void logRobotsTxt( const char *robotsTxt ) {
	logf (LOG_INFO, "===== robots.txt =====\n%s", robotsTxt);
}

static void generateRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, int32_t *pos, const char *userAgent = "testbot", const char *allow = "", const char *disallow = "" ) {
	if ( *pos != 0 ) {
		*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "\n" );
	}

	*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "user-agent: %s\n", userAgent );

	if ( allow != "" ) {
		*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "allow: %s\n", allow );
	}

	if ( disallow != "" ) {
		*pos += snprintf (robotsTxt + *pos, robotsTxtSize - *pos, "disallow: %s\n", disallow );
	}
}

static void generateTestRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, const char *allow = "", const char *disallow = "" ) {
	int32_t pos = 0;
	generateRobotsTxt( robotsTxt, robotsTxtSize, &pos, "testbot", allow, disallow);
	logRobotsTxt( robotsTxt );
}

static bool isUrlAllowed( const char *path, const char *robotsTxt, bool *userAgentFound, bool *hadAllowOrDisallow, const char *userAgent = "testbot" ) {
	char urlStr[1024];
	snprintf( urlStr, 1024, TEST_DOMAIN "%s", path );

	Url url;
	url.set( urlStr );

	int32_t crawlDelay = -1;

	return Robots::isAllowed( &url, userAgent, robotsTxt, strlen(robotsTxt), userAgentFound, true, &crawlDelay, hadAllowOrDisallow);
}

static bool isUrlAllowed( const char *path, const char *robotsTxt, const char *userAgent = "testbot" ) {
	bool userAgentFound = false;
	bool hadAllowOrDisallow = false;

	bool isAllowed = isUrlAllowed( path, robotsTxt, &userAgentFound, &hadAllowOrDisallow, userAgent );
	EXPECT_TRUE( userAgentFound );
	EXPECT_TRUE( hadAllowOrDisallow );

	return isAllowed;
}

//
// Test user-agent
//

TEST(RobotsTest, UserAgentSingleUANoMatch) {
	static const char *allow = "";
	static const char *disallow = "/";

	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "abcbot", allow, disallow);
	logRobotsTxt( robotsTxt );

	bool userAgentFound = false;
	bool hadAllowOrDisallow = false;

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );
}

TEST(RobotsTest, DISABLED_UserAgentSingleUAPrefixNoMatch) {
	static const char *allow = "";
	static const char *disallow = "/";

	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbotabc", allow, disallow);
	logRobotsTxt( robotsTxt );

	bool userAgentFound = false;
	bool hadAllowOrDisallow = false;

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );
}

TEST(RobotsTest, UserAgentSingleUAPrefixMatch) {
	static const char *allow = "";
	static const char *disallow = "/";

	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot/1.0", allow, disallow);
	logRobotsTxt( robotsTxt );

	EXPECT_FALSE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/index.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentSingleUAMatch) {
	static const char *allow = "";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow);

	EXPECT_FALSE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/index.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentSeparateUANone) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "atestbot", "", "/test");
	generateRobotsTxt( robotsTxt, 1024, &pos, "abcbot", "", "/abc");
	generateRobotsTxt( robotsTxt, 1024, &pos, "defbot", "", "/def");
	logRobotsTxt( robotsTxt );

	bool userAgentFound = false;
	bool hadAllowOrDisallow = false;

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/test.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );
}

TEST(RobotsTest, UserAgentSeparateUAFirst) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot", "", "/test");
	generateRobotsTxt( robotsTxt, 1024, &pos, "abcbot", "", "/abc");
	generateRobotsTxt( robotsTxt, 1024, &pos, "defbot", "", "/def");
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentSeparateUASecond) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "abcbot", "", "/abc");
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot", "", "/test");
	generateRobotsTxt( robotsTxt, 1024, &pos, "defbot", "", "/def");
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentSeparateUALast) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "abcbot", "", "/abc");
	generateRobotsTxt( robotsTxt, 1024, &pos, "defbot", "", "/def");
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot", "", "/test");
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentMultiUANone) {
	int32_t pos = 0;
	char robotsTxt[1024];
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "abcbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "atestbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "defbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "disallow: %s\n", "/test" );
	logRobotsTxt( robotsTxt );

	bool userAgentFound = false;
	bool hadAllowOrDisallow = false;

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );

	EXPECT_TRUE( isUrlAllowed( "/test.html", robotsTxt, &userAgentFound, &hadAllowOrDisallow ) );
	EXPECT_FALSE ( userAgentFound );
	EXPECT_FALSE ( hadAllowOrDisallow );
}

TEST(RobotsTest, UserAgentMultiUAFirst) {
	int32_t pos = 0;
	char robotsTxt[1024];
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "testbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "abcbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "defbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "disallow: %s\n", "/test" );
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentMultiUASecond) {
	static const char *disallow = "/";

	int32_t pos = 0;
	char robotsTxt[1024];
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "abcbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "testbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "defbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "disallow: %s\n", "/test" );
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

TEST(RobotsTest, UserAgentMultiUALast) {
	static const char *disallow = "/";

	int32_t pos = 0;
	char robotsTxt[1024];
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "abcbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "defbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "user-agent: %s\n", "testbot" );
	pos += snprintf ( robotsTxt + pos, 1024 - pos, "disallow: %s\n", "/test" );
	logRobotsTxt( robotsTxt );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/abc.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/def.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/test.html", robotsTxt ) );
}

//
// Test allow/disallow
//

TEST(RobotsTest, AllowAll) {
	static const char *allow = "";
	static const char *disallow = " ";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
}

TEST(RobotsTest, DisallowAll) {
	static const char *allow = "";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/index.html", robotsTxt ) );
}

// /123 matches /123 and /123/ and /1234 and /123/456
TEST(RobotsTest, DISABLED_PathMatch) {
	static const char *allow = "";
	static const char *disallow = "/123";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed ( "/12", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/1234", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/456", robotsTxt ) );
}

// /123/ matches /123/ and /123/456
TEST(RobotsTest, DISABLED_PathMatchWithEndSlash) {
	static const char *allow = "";
	static const char *disallow = "/123/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed ( "/123", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed ( "/1234", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/456", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/456/", robotsTxt ) );
}

// /*abc matches /123abc and /123/abc and /123abc456 and /123/abc/456
TEST(RobotsTest, DISABLED_PathMatchWildcardStart) {
	static const char *allow = "";
	static const char *disallow = "/*abc";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed ( "/123ab", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123abc", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/abc", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123abc456", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/abc/456", robotsTxt ) );
}

// /123*xyz matches /123qwertyxyz and /123/qwerty/xyz/789
TEST(RobotsTest, DISABLED_PathMatchWildcardMid) {
	static const char *allow = "";
	static const char *disallow = "/123*xyz";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123/qwerty/xy", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123qwertyxyz", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123qwertyxyz/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/qwerty/xyz/789", robotsTxt ) );
}

// /123$ matches ONLY /123
TEST(RobotsTest, DISABLED_PathMatchEnd) {
	static const char *allow = "";
	static const char *disallow = "/123$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123/", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123", robotsTxt ) );
}

// /*abc$ matches /123abc and /123/abc but NOT /123/abc/x etc.
TEST(RobotsTest, DISABLED_PathMatchWildcardEnd) {
	static const char *allow = "";
	static const char *disallow = "/*abc$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123/abc/x", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123abc", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/abc", robotsTxt ) );
}

//
// Test cases based on google's robots.txt specification
// https://developers.google.com/webmasters/control-crawl-index/docs/robots_txt?hl=en#example-path-matches
//

// [path]		[match]								[no match]					[comments]
// /			any valid url													Matches the root and any lower level URL
// /*			equivalent to /						equivalent to /				Equivalent to "/" -- the trailing wildcard is ignored.
TEST(RobotsTest, DISABLED_GPathMatchDisallow) {
}

// [path]		[match]								[no match]					[comments]
// /fish		/fish								/Fish.asp					Note the case-sensitive matching.
// 				/fish.html							/catfish
// 				/fish/salmon.html					/?id=fish
// 				/fishheads
// 				/fishheads/yummy.html
// 				/fish.php?id=anything
TEST(RobotsTest, DISABLED_GPathMatchPrefixDisallow) {
	static const char *allow = "";
	static const char *disallow = "/fish";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish/salmon.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads/yummy.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish.php?id=anything", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/Fish.asp", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/catfish", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/?id=fish", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchPrefixAllow) {
	static const char *allow = "/fish";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish/salmon.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads/yummy.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish.php?id=anything", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/Fish.asp", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/catfish", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/?id=fish", robotsTxt ) );
}

// [path]		[match]								[no match]					[comments]
// /fish*		/fish								/Fish.asp					Equivalent to "/fish" -- the trailing wildcard is ignored.
// 				/fish.html							/catfish
// 				/fish/salmon.html					/?id=fish
// 				/fishheads
// 				/fishheads/yummy.html
// 				/fish.php?id=anything
TEST(RobotsTest, DISABLED_GPathMatchPrefixWildcardDisallow) {
	static const char *allow = "";
	static const char *disallow = "/fish*";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish/salmon.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads/yummy.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish.php?id=anything", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/Fish.asp", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/catfish", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/?id=fish", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchPrefixWildcardAllow) {
	static const char *allow = "/fish*";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish/salmon.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads/yummy.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish.php?id=anything", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/Fish.asp", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/catfish", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/?id=fish", robotsTxt ) );
}

// [path]		[match]								[no match]					[comments]
// /fish/		/fish/								/fish						The trailing slash means this matches anything in this folder.
// 				/fish/?id=anything					/fish.html
// 				/fish/salmon.htm					/Fish/Salmon.php
TEST(RobotsTest, DISABLED_GPathMatchPrefixDirDisallow) {
	static const char *allow = "";
	static const char *disallow = "/fish/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/fish/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish/?id=anything", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish/salmon.htm", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/Fish/Salmon.php", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchPrefixDirAllow) {
	static const char *allow = "/fish/";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/fish/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish/?id=anything", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fish/salmon.htm", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/fish", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fish.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/Fish/Salmon.php", robotsTxt ) );
}

// [path]		[match]								[no match]					[comments]
// *.php		/filename.php						/ 							(even if it maps to /index.php)
// 				/folder/filename.php				/windows.PHP
// 				/folder/filename.php?parameters
// 				/folder/any.php.file.html
// 				/filename.php/
TEST(RobotsTest, DISABLED_GPathMatchWildcardExtDisallow) {
	static const char *allow = "";
	static const char *disallow = "*.php";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/filename.php", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/folter/filename.php", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/folder/filename.php?parameters", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/folder/any.php.file.html", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/filename.php/", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/windows.PHP", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchWildcardExtAllow) {
	static const char *allow = "/*.php";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/filename.php", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/folter/filename.php", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/folder/filename.php?parameters", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/folder/any.php.file.html", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/filename.php/", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/windows.PHP", robotsTxt ) );
}

// [path]		[match]								[no match]					[comments]
// /*.php$		/filename.php						/filename.php?parameters
// 				/folder/filename.php				/filename.php/
// 													/filename.php5
// 													/windows.PHP
TEST(RobotsTest, DISABLED_GPathMatchWildcardExtEndDisallow) {
	static const char *allow = "";
	static const char *disallow = "/*.php$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/filename.php", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/folder/filename.php", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/filename.php?parameters", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/filename.php/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/filename.php5", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/windows.PHP", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchWildcardExtEndAllow) {
	static const char *allow = "/*.php$";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/filename.php", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/folder/filename.php", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/filename.php?parameters", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/filename.php/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/filename.php5", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/windows.PHP", robotsTxt ) );
}

// [path]		[match]								[no match]					[comments]
// /fish*.php	/fish.php							/Fish.PHP
// 				/fishheads/catfish.php?parameters
TEST(RobotsTest, DISABLED_GPathMatchPrefixWildcardExtDisallow) {
	static const char *allow = "";
	static const char *disallow = "/fish*.php";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/fish.php", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads/catfish.php?parameters", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/Fish.PHP", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchPrefixWildcardExtAllow) {
	static const char *allow = "/fish*.php";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/fish.php", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads/catfish.php?parameters", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/Fish.PHP", robotsTxt ) );
}

//
// Test cases based on google's robots.txt specification
// https://developers.google.com/webmasters/control-crawl-index/docs/robots_txt?hl=en#order-of-precedence-for-group-member-records
//

// [url]							[allow]		[disallow]		[verdict]
// http://example.com/page			/p			/				allow
// http://example.com/folder/page	/folder/	/folder			allow
// http://example.com/page.htm		/page		/*.htm			undefined
// http://example.com/				/$			/				allow
// http://example.com/page.htm		/$			/				disallow
TEST(RobotsTest, DISABLED_GPrecedence) {
	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, "/p", "/" );
	EXPECT_TRUE( isUrlAllowed ( "/page", robotsTxt) );

	generateTestRobotsTxt( robotsTxt, 1024, "/folder/", "/folder" );
	EXPECT_TRUE( isUrlAllowed ( "/folder/page", robotsTxt) );

	/// @todo ALC decide what's the result
	generateTestRobotsTxt( robotsTxt, 1024, "/page", "/*.htm" );
	//EXPECT_TRUE( isUrlAllowed ( "/page.htm", robotsTxt) );

	generateTestRobotsTxt( robotsTxt, 1024, "/$", "/" );
	EXPECT_TRUE( isUrlAllowed ( "/", robotsTxt) );
	EXPECT_FALSE( isUrlAllowed ( "/page.htm", robotsTxt) );
}
