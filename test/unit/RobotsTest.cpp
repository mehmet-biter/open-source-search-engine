#include "gtest/gtest.h"
#include "Robots.h"
#include "Url.h"
#include "Log.h"

#define TEST_DOMAIN "http://example.com"

static void generateRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, const char *allow = "", const char *disallow = "" ) {
	int32_t pos = snprintf( robotsTxt, 1024, "user-agent: ua-test\n" );

	if (allow != "") {
		pos += snprintf ( robotsTxt + pos, 1024 - pos, "allow: %s\n", allow );
	}

	pos += snprintf (robotsTxt + pos, 1024 - pos, "disallow: %s\n", disallow );

	logf (LOG_INFO, "===== robots.txt =====\n%s", robotsTxt);
}

// helper method
static bool isUrlAllowed( const char *path, const char *robotsTxt ) {
	char urlStr[1024];
	snprintf( urlStr, 1024, TEST_DOMAIN "%s", path );

	Url url;
	url.set( urlStr );

	bool userAgentFound = false;
	int32_t crawlDelay = -1;
	bool hadAllowOrDisallow = false;

	bool isAllowed = Robots::isAllowed( &url, "ua-test", robotsTxt, strlen(robotsTxt), &userAgentFound, true, &crawlDelay, &hadAllowOrDisallow);
	EXPECT_TRUE( userAgentFound );
	EXPECT_TRUE( hadAllowOrDisallow );

	return isAllowed;
}

TEST(RobotsTest, AllowAll) {
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024 );

	EXPECT_TRUE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/index.html", robotsTxt ) );
}

TEST(RobotsTest, DisallowAll) {
	static const char *allow = "";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/index.html", robotsTxt ) );
}

// /123 matches /123 and /123/ and /1234 and /123/456
TEST(RobotsTest, DISABLED_PathMatch) {
	static const char *allow = "";
	static const char *disallow = "/123";

	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123/", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123", robotsTxt ) );
}

// /*abc$ matches /123abc and /123/abc but NOT /123/abc/x etc.
TEST(RobotsTest, DISABLED_PathMatchWildcardEnd) {
	static const char *allow = "";
	static const char *disallow = "/*abc$";

	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed ( "/123/abc/x", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed ( "/123abc", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed ( "/123/abc", robotsTxt ) );
}

// test cases based on google's robots.txt specification
// https://developers.google.com/webmasters/control-crawl-index/docs/robots_txt?hl=en#example-path-matches

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

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
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_FALSE( isUrlAllowed( "/fish.php", robotsTxt ) );
	EXPECT_FALSE( isUrlAllowed( "/fishheads/catfish.php?parameters", robotsTxt ) );

	EXPECT_TRUE( isUrlAllowed( "/Fish.PHP", robotsTxt ) );
}

TEST(RobotsTest, DISABLED_GPathMatchPrefixWildcardExtAllow) {
	static const char *allow = "/fish*.php";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, allow, disallow );

	EXPECT_TRUE( isUrlAllowed( "/fish.php", robotsTxt ) );
	EXPECT_TRUE( isUrlAllowed( "/fishheads/catfish.php?parameters", robotsTxt ) );

	EXPECT_FALSE( isUrlAllowed( "/Fish.PHP", robotsTxt ) );
}

