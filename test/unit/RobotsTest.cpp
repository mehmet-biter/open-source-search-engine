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
// /123*xyz matches /123qwertyxyz and /123/qwerty/xyz/789
// /123$ matches ONLY /123
// /*abc$ matches /123abc and /123/abc but NOT /123/abc/x etc.

/*
[path]	[match]					[no match]
/fish	/fish					/Fish.asp
		/fish.html				/catfish
		/fish/salmon.html		/?id=fish
		/fishheads
		/fishheads/yummy.html
		/fish.php?id=anything
*/
TEST(RobotsTest, DISABLED_PathMatchExample1Disallow) {
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

TEST(RobotsTest, DISABLED_PathMatchExample1Allow) {
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
