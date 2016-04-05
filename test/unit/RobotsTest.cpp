#include "gtest/gtest.h"
#include "Robots.h"
#include "Url.h"
#include "Log.h"

#include <fstream>
#include <sstream>
#include <string>

#define TEST_DOMAIN "http://example.com"

//
// test class
//
class TestRobots : public Robots {
public:
	TestRobots( const char *robotsTxt, int32_t robotsTxtLen, const char *userAgent = "testbot" )
		: Robots (robotsTxt, robotsTxtLen, userAgent ) {
	}

	using Robots::getNextLine;
	using Robots::getField;
	using Robots::getValue;

	using Robots::getCurrentLine;
	using Robots::getCurrentLineLen;

	using Robots::isUserAgentFound;
	using Robots::isDefaultUserAgentFound;

	using Robots::isRulesEmpty;
	using Robots::isDefaultRulesEmpty;


	bool isAllowed( const char *path ) {
		char urlStr[1024];
		snprintf( urlStr, 1024, TEST_DOMAIN "%s", path );

		Url url;
		url.set( urlStr );

		return Robots::isAllowed( &url );
	}
private:
	std::string m_fileRobotsTxt;
};

static void expectRobotsNoNextLine( TestRobots *robots ) {
	EXPECT_FALSE( robots->getNextLine() );
}

static void expectRobots( TestRobots *robots, const char *expectedLine, const char *expectedField = "", const char *expectedValue = "" ) {
	{
		std::stringstream ss;
		ss << __func__ << ":"
				<< " expectedLine='" << expectedLine << "'"
				<< " currentLine='" << robots->getCurrentLineLen() << "'";
		SCOPED_TRACE(ss.str());

		EXPECT_TRUE( robots->getNextLine() );
		EXPECT_EQ( strlen( expectedLine ), robots->getCurrentLineLen() );
		EXPECT_EQ( 0, memcmp( expectedLine, robots->getCurrentLine(), robots->getCurrentLineLen() ) );
	}

	if ( expectedField != "" ) {
		const char *field = NULL;
		int32_t fieldLen = 0;

		EXPECT_TRUE( robots->getField( &field, &fieldLen ) );
		std::stringstream ss;
		ss << __func__ << ":"
				<< " expectedField='" << expectedField << "'"
				<< " currentField='" << std::string( field, fieldLen ) << "'";
		SCOPED_TRACE(ss.str());

		EXPECT_EQ( strlen( expectedField ), fieldLen );
		EXPECT_EQ( 0, memcmp( expectedField, field, fieldLen ) );

		if ( expectedValue != "" ) {
			const char *value = NULL;
			int32_t valueLen = 0;

			EXPECT_TRUE( robots->getValue( &value, &valueLen ) );
			std::stringstream ss;
			ss << __func__ << ":"
					<< " expectedValue='" << expectedValue << "'"
					<< " currentValue='" << std::string( value, valueLen ) << "'";
			SCOPED_TRACE(ss.str());

			EXPECT_EQ( strlen( expectedValue ), valueLen );
			EXPECT_EQ( 0, memcmp( expectedValue, value, valueLen ) );
		}
	}
}

TEST( RobotsTest, RobotsGetNextLineLineEndings ) {
	const char *robotsTxt = "line 1\n"
							"line 2\r"
							"line 3\r\n"
							"line 4\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	expectRobots( &robots, "line 1" );
	expectRobots( &robots, "line 2" );
	expectRobots( &robots, "line 3" );
	expectRobots( &robots, "line 4" );

	expectRobotsNoNextLine( &robots);
}

TEST( RobotsTest, RobotsGetNextLineWhitespaces ) {
	const char *robotsTxt = "   line 1  \n"
							"  line 2    \r"
							"       \n"
							"\tline 3\t\r\n"
							"\t\tline 4   \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	expectRobots( &robots, "line 1" );
	expectRobots( &robots, "line 2" );
	expectRobots( &robots, "line 3" );
	expectRobots( &robots, "line 4" );

	expectRobotsNoNextLine( &robots);
}

TEST( RobotsTest, RobotsGetNextLineComments ) {
	const char *robotsTxt = "   line 1  # comment \n"
							"  line 2#comment    \r"
							"    # line 2a \n"
							"\tline 3\t#\tcomment\r\n"
							"\t\t#line 4\t\t\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	expectRobots( &robots, "line 1" );
	expectRobots( &robots, "line 2" );
	expectRobots( &robots, "line 3" );

	expectRobotsNoNextLine( &robots);
}

TEST( RobotsTest, RobotsGetFieldValue ) {
	const char *robotsTxt = "   field1: value1  # comment \n"
							"  field2   : value2#comment    \r"
							"    # line 2a \n"
							"\tfield3\t\t:\tvalue3\t#\tcomment\r\n"
							"\t\t#line 4\t\t\n"
							"\tfield4\t\t:\tvalue four#comment\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	expectRobots( &robots, "field1: value1", "field1", "value1" );
	expectRobots( &robots, "field2   : value2", "field2", "value2" );
	expectRobots( &robots, "field3\t\t:\tvalue3", "field3", "value3" );
	expectRobots( &robots, "field4\t\t:\tvalue four", "field4", "value four" );

	expectRobotsNoNextLine( &robots);
}

//
// helper method
//

static void generateRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, int32_t *pos, const char *userAgent = "testbot", const char *allow = "", const char *disallow = "", bool reversed = false ) {
	if ( *pos != 0 ) {
		*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "\n" );
	}

	*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "user-agent: %s\n", userAgent );

	if ( reversed && disallow != "" ) {
		*pos += snprintf (robotsTxt + *pos, robotsTxtSize - *pos, "disallow: %s\n", disallow );
	}

	if ( allow != "" ) {
		*pos += snprintf ( robotsTxt + *pos, robotsTxtSize - *pos, "allow: %s\n", allow );
	}

	if ( !reversed && disallow != "" ) {
		*pos += snprintf (robotsTxt + *pos, robotsTxtSize - *pos, "disallow: %s\n", disallow );
	}
}

static void generateTestRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, const char *allow = "", const char *disallow = "" ) {
	int32_t pos = 0;
	generateRobotsTxt( robotsTxt, robotsTxtSize, &pos, "testbot", allow, disallow);
}

static void generateTestReversedRobotsTxt ( char *robotsTxt, size_t robotsTxtSize, const char *allow = "", const char *disallow = "" ) {
	int32_t pos = 0;
	generateRobotsTxt( robotsTxt, robotsTxtSize, &pos, "testbot", allow, disallow, true);
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test user-agent                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, UserAgentSingleUANoMatch ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSingleUAPrefixMatch ) {
	char robotsTxt[1024] = "user-agent: testbotabc\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSingleUAPrefixVersionMatch ) {
	char robotsTxt[1024] = "user-agent: testbot/1.0\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSingleUAIgnoreCase ) {
	char robotsTxt[1024] = "user-agent: TestBot/1.0\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSingleUAMatch ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSeparateUANone ) {
	char robotsTxt[1024] = "user-agent: atestbot\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: abcbot\n"
	                       "crawl-delay: 2\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 3\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSeparateUAFirst ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: abcbot\n"
	                       "crawl-delay: 2\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 3\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSeparateUASecond ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: testbot\n"
	                       "crawl-delay: 2\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 3\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentSeparateUALast ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 2\n"
	                       "user-agent: testbot\n"
	                       "crawl-delay: 3\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 3000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiUANone ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: atestbot\n"
	                       "crawl-delay: 2\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 3\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiUAFirst ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiUASecond ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "user-agent: testbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiUALast ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "user-agent: testbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentDefaultMultiUAFirst ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: testbot\n"
	                       "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 2\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentDefaultMultiUASecond ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: abcbot\n"
	                       "user-agent: testbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 2\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentDefaultMultiUALast ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "crawl-delay: 1\n"
	                       "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "user-agent: testbot\n"
	                       "crawl-delay: 2\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}


TEST( RobotsTest, UserAgentMultiDefaultUAFirst ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiDefaultUASecond ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "user-agent: *\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentMultiDefaultUALast ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "user-agent: defbot\n"
	                       "user-agent: *\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, UserAgentFieldCaseInsensitive ) {
	char robotsTxt[1024] = "User-Agent: testbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test comments                                                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, CommentsFullLine ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "#user-agent: testbot\n"
	                       "user-agent: defbot\n"
	                       "crawl-delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CommentsAfterWithSpace ) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot #user-agent", "/test #allow", "/ #disallow");

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, CommentsAfterNoSpace ) {
	int32_t pos = 0;
	char robotsTxt[1024];
	generateRobotsTxt( robotsTxt, 1024, &pos, "testbot#user-agent", "/test#allow", "/#disallow");

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/test.html" ) );
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test whitespace                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, WhitespaceSpaceDirectiveBefore ) {
	char robotsTxt[1024] = "    user-agent:testbot\n"
	                       "        disallow:/test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, WhitespaceSpaceDirectiveAfter ) {
	char robotsTxt[1024] = "user-agent:   testbot\n"
	                       "disallow:     /test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, WhitespaceSpaceDirectiveBoth ) {
	char robotsTxt[1024] = "    user-agent:    testbot\n"
	                       "        disallow:  /test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, WhitespaceTabsDirectiveBefore ) {
	char robotsTxt[1024] = "\tuser-agent:testbot\n"
	                       "\t\tdisallow:/test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, WhitespaceTabsDirectiveAfter ) {
	char robotsTxt[1024] = "user-agent:\ttestbot\n"
	                       "disallow:\t/test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

TEST( RobotsTest, WhitespaceTabsDirectiveBoth ) {
	char robotsTxt[1024] = "\tuser-agent:\ttestbot\n"
	                       "\t\tdisallow:\t/test\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/test.html" ) );
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test allow/disallow                                                        //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, AllowAll ) {
	static const char *allow = "";
	static const char *disallow = " ";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
}

TEST( RobotsTest, DisallowAll ) {
	static const char *allow = "";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/index.html" ) );
}

// /123 matches /123 and /123/ and /1234 and /123/456
TEST( RobotsTest, PathMatch ) {
	static const char *allow = "";
	static const char *disallow = "/123";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/12" ) );

	EXPECT_FALSE( robots.isAllowed( "/123" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/" ) );
	EXPECT_FALSE( robots.isAllowed( "/1234" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456" ) );
}

// treat /123* as /123
TEST( RobotsTest, PathMatchWildcardEnd ) {
	static const char *allow = "";
	static const char *disallow = "/123*";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/12" ) );

	EXPECT_FALSE( robots.isAllowed( "/123" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/" ) );
	EXPECT_FALSE( robots.isAllowed( "/1234" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456" ) );
}

// treat /123*** as /123
TEST( RobotsTest, PathMatchMultipleWildcardEnd ) {
	static const char *allow = "";
	static const char *disallow = "/123***";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/12" ) );

	EXPECT_FALSE( robots.isAllowed( "/123" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/" ) );
	EXPECT_FALSE( robots.isAllowed( "/1234" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456" ) );
}

// /123/ matches /123/ and /123/456
TEST( RobotsTest, PathMatchDir ) {
	static const char *allow = "";
	static const char *disallow = "/123/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/123" ) );
	EXPECT_TRUE( robots.isAllowed( "/1234" ) );

	EXPECT_FALSE( robots.isAllowed( "/123/" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456/" ) );
}

// treat /123/* as /123/
TEST( RobotsTest, PathMatchDirWildcardEnd ) {
	static const char *allow = "";
	static const char *disallow = "/123/*";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/123" ) );
	EXPECT_TRUE( robots.isAllowed( "/1234" ) );

	EXPECT_FALSE( robots.isAllowed( "/123/" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/456/" ) );
}

// /*abc matches /123abc and /123/abc and /123abc456 and /123/abc/456
TEST( RobotsTest, PathMatchWildcardStart ) {
	static const char *allow = "";
	static const char *disallow = "/*abc";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/123" ) );
	EXPECT_TRUE( robots.isAllowed( "/123ab" ) );

	EXPECT_FALSE( robots.isAllowed( "/123abc" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/abc" ) );
	EXPECT_FALSE( robots.isAllowed( "/123abc456" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/abc/456" ) );
}

// /123*xyz matches /123qwertyxyz and /123/qwerty/xyz/789
TEST( RobotsTest, PathMatchWildcardMid ) {
	static const char *allow = "";
	static const char *disallow = "/123*xyz";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/123/qwerty/xy" ) );

	EXPECT_FALSE( robots.isAllowed( "/123qwertyxyz" ) );
	EXPECT_FALSE( robots.isAllowed( "/123qwertyxyz/" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/qwerty/xyz/789" ) );
}

// /123$ matches ONLY /123
TEST( RobotsTest, PathMatchEndAnchor ) {
	static const char *allow = "";
	static const char *disallow = "/123$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/123/" ) );

	EXPECT_FALSE( robots.isAllowed( "/123" ) );
}

// /*abc$ matches /123abc and /123/abc but NOT /123/abc/x etc.
TEST( RobotsTest, PathMatchWildcardEndAnchor ) {
	static const char *allow = "";
	static const char *disallow = "/*abc$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/123/abc/x" ) );

	EXPECT_FALSE( robots.isAllowed( "/123abc" ) );
	EXPECT_FALSE( robots.isAllowed( "/123/abc" ) );
}

/// @todo ALC test multiple wildcard

/// @todo ALC test multiple wildcard end (line anchor)

/// @todo ALC test _escaped_fragment_

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test crawl delay                                                           //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, CrawlDelayValueNone ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay:";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueInvalid ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: abc";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayNoMatch ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayMissing ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "disallow: /";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_FALSE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueFractionPartial ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: .5";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 500, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueFractionFull ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 1.5\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1500, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueIntegerValid ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 30 \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 30000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueIntegerInvalid ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 60abc \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( -1, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayValueComment ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 60#abc \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 60000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayDefaultFirstNoMatch ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "crawl-delay: 1 \n"
	                       "user-agent: testbot\n"
	                       "crawl-delay: 2 \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayDefaultLastNoMatch ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "crawl-delay: 1 \n"
	                       "user-agent: * \n"
	                       "crawl-delay: 2 \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayDefaultFirstMatch ) {
	char robotsTxt[1024] = "user-agent: *\n"
	                       "crawl-delay: 1 \n"
	                       "user-agent: abcbot\n"
	                       "crawl-delay: 2 \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayDefaultLastMatch ) {
	char robotsTxt[1024] = "user-agent: abcbot\n"
	                       "crawl-delay: 1 \n"
	                       "user-agent: *\n"
	                       "crawl-delay: 2 \n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 2000, robots.getCrawlDelay() );
}

TEST( RobotsTest, CrawlDelayFieldCaseInsensitive ) {
	char robotsTxt[1024] = "user-agent: testbot\n"
	                       "Crawl-Delay: 1\n";

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );
	EXPECT_EQ( 1000, robots.getCrawlDelay() );
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test site map                                                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// @todo ALC

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test line endings                                                          //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// @todo ALC

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test utf-8 encoding (non-ascii)                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// @todo ALC

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test url encoded path                                                      //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

/// @todo ALC

//////////////////////////////////////////////////////////////////////////////////////////
//                                                                                      //
// Test cases based on google's robots.txt specification                                //
// https://developers.google.com/webmasters/control-crawl-index/docs/robots_txt         //
//     #example-path-matches                                                            //
//                                                                                      //
//////////////////////////////////////////////////////////////////////////////////////////

// [path]		[match]								[no match]					[comments]
// /			any valid url													Matches the root and any lower level URL
// /*			equivalent to /						equivalent to /				Equivalent to "/" -- the trailing wildcard is ignored.
TEST( RobotsTest, GPathMatchDisallowAll ) {
	static const char *allow = "";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/index.html" ) );
}

TEST( RobotsTest, GPathMatchDisallowAllWildcard ) {
	static const char *allow = "";
	static const char *disallow = "/*";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/index.html" ) );
}

// [path]		[match]								[no match]					[comments]
// /fish		/fish								/Fish.asp					Note the case-sensitive matching.
// 				/fish.html							/catfish
// 				/fish/salmon.html					/?id=fish
// 				/fishheads
// 				/fishheads/yummy.html
// 				/fish.php?id=anything
TEST( RobotsTest, GPathMatchPrefixDisallow ) {
	static const char *allow = "";
	static const char *disallow = "/fish";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/fish" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish/salmon.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fishheads" ) );
	EXPECT_FALSE( robots.isAllowed( "/fishheads/yummy.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish.php?id=anything" ) );

	EXPECT_TRUE( robots.isAllowed( "/Fish.asp" ) );
	EXPECT_TRUE( robots.isAllowed( "/catfish" ) );
	EXPECT_TRUE( robots.isAllowed( "/?id=fish" ) );
}

TEST( RobotsTest, GPathMatchPrefixAllow ) {
	static const char *allow = "/fish";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/fish" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish/salmon.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fishheads" ) );
	EXPECT_TRUE( robots.isAllowed( "/fishheads/yummy.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish.php?id=anything" ) );

	EXPECT_FALSE( robots.isAllowed( "/Fish.asp" ) );
	EXPECT_FALSE( robots.isAllowed( "/catfish" ) );
	EXPECT_FALSE( robots.isAllowed( "/?id=fish" ) );
}

// [path]		[match]								[no match]					[comments]
// /fish*		/fish								/Fish.asp					Equivalent to "/fish" -- the trailing wildcard is ignored.
// 				/fish.html							/catfish
// 				/fish/salmon.html					/?id=fish
// 				/fishheads
// 				/fishheads/yummy.html
// 				/fish.php?id=anything
TEST( RobotsTest, GPathMatchPrefixWildcardDisallow ) {
	static const char *allow = "";
	static const char *disallow = "/fish*";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/fish" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish/salmon.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fishheads" ) );
	EXPECT_FALSE( robots.isAllowed( "/fishheads/yummy.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish.php?id=anything" ) );

	EXPECT_TRUE( robots.isAllowed( "/Fish.asp" ) );
	EXPECT_TRUE( robots.isAllowed( "/catfish" ) );
	EXPECT_TRUE( robots.isAllowed( "/?id=fish" ) );
}

TEST( RobotsTest, GPathMatchPrefixWildcardAllow ) {
	static const char *allow = "/fish*";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/fish" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish/salmon.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fishheads" ) );
	EXPECT_TRUE( robots.isAllowed( "/fishheads/yummy.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish.php?id=anything" ) );

	EXPECT_FALSE( robots.isAllowed( "/Fish.asp" ) );
	EXPECT_FALSE( robots.isAllowed( "/catfish" ) );
	EXPECT_FALSE( robots.isAllowed( "/?id=fish" ) );
}

// [path]		[match]								[no match]					[comments]
// /fish/		/fish/								/fish						The trailing slash means this matches anything in this folder.
// 				/fish/?id=anything					/fish.html
// 				/fish/salmon.htm					/Fish/Salmon.php
TEST( RobotsTest, GPathMatchPrefixDirDisallow ) {
	static const char *allow = "";
	static const char *disallow = "/fish/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/fish/" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish/?id=anything" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish/salmon.htm" ) );

	EXPECT_TRUE( robots.isAllowed( "/fish" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/Fish/Salmon.php" ) );
}

TEST( RobotsTest, GPathMatchPrefixDirAllow ) {
	static const char *allow = "/fish/";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/fish/" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish/?id=anything" ) );
	EXPECT_TRUE( robots.isAllowed( "/fish/salmon.htm" ) );

	EXPECT_FALSE( robots.isAllowed( "/fish" ) );
	EXPECT_FALSE( robots.isAllowed( "/fish.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/Fish/Salmon.php" ) );
}

// [path]		[match]								[no match]					[comments]
// *.php		/filename.php						/ 							(even if it maps to /index.php)
// 				/folder/filename.php				/windows.PHP
// 				/folder/filename.php?parameters
// 				/folder/any.php.file.html
// 				/filename.php/
TEST( RobotsTest, GPathMatchWildcardExtDisallow ) {
	static const char *allow = "";
	static const char *disallow = "*.php";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/filename.php" ) );
	EXPECT_FALSE( robots.isAllowed( "/folter/filename.php" ) );
	EXPECT_FALSE( robots.isAllowed( "/folder/filename.php?parameters" ) );
	EXPECT_FALSE( robots.isAllowed( "/folder/any.php.file.html" ) );
	EXPECT_FALSE( robots.isAllowed( "/filename.php/" ) );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/windows.PHP" ) );
}

TEST( RobotsTest, GPathMatchWildcardExtAllow ) {
	static const char *allow = "/*.php";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/filename.php" ) );
	EXPECT_TRUE( robots.isAllowed( "/folter/filename.php" ) );
	EXPECT_TRUE( robots.isAllowed( "/folder/filename.php?parameters" ) );
	EXPECT_TRUE( robots.isAllowed( "/folder/any.php.file.html" ) );
	EXPECT_TRUE( robots.isAllowed( "/filename.php/" ) );

	EXPECT_FALSE( robots.isAllowed( "/" ) );
	EXPECT_FALSE( robots.isAllowed( "/windows.PHP" ) );
}

// [path]		[match]								[no match]					[comments]
// /*.php$		/filename.php						/filename.php?parameters
// 				/folder/filename.php				/filename.php/
// 													/filename.php5
// 													/windows.PHP
TEST( RobotsTest, GPathMatchWildcardExtEndDisallow ) {
	static const char *allow = "";
	static const char *disallow = "/*.php$";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/filename.php" ) );
	EXPECT_FALSE( robots.isAllowed( "/folder/filename.php" ) );

	EXPECT_TRUE( robots.isAllowed( "/filename.php?parameters" ) );
	EXPECT_TRUE( robots.isAllowed( "/filename.php/" ) );
	EXPECT_TRUE( robots.isAllowed( "/filename.php5" ) );
	EXPECT_TRUE( robots.isAllowed( "/windows.PHP" ) );
}

TEST( RobotsTest, GPathMatchWildcardExtEndAllow ) {
	static const char *allow = "/*.php$";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/filename.php" ) );
	EXPECT_TRUE( robots.isAllowed( "/folder/filename.php" ) );

	EXPECT_FALSE( robots.isAllowed( "/filename.php?parameters" ) );
	EXPECT_FALSE( robots.isAllowed( "/filename.php/" ) );
	EXPECT_FALSE( robots.isAllowed( "/filename.php5" ) );
	EXPECT_FALSE( robots.isAllowed( "/windows.PHP" ) );
}

// [path]		[match]								[no match]					[comments]
// /fish*.php	/fish.php							/Fish.PHP
// 				/fishheads/catfish.php?parameters
TEST( RobotsTest, GPathMatchPrefixWildcardExtDisallow ) {
	static const char *allow = "";
	static const char *disallow = "/fish*.php";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/fish.php" ) );
	EXPECT_FALSE( robots.isAllowed( "/fishheads/catfish.php?parameters" ) );

	EXPECT_TRUE( robots.isAllowed( "/Fish.PHP" ) );
}

TEST( RobotsTest, GPathMatchPrefixWildcardExtAllow ) {
	static const char *allow = "/fish*.php";
	static const char *disallow = "/";

	char robotsTxt[1024];
	generateTestRobotsTxt( robotsTxt, 1024, allow, disallow );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/fish.php" ) );
	EXPECT_TRUE( robots.isAllowed( "/fishheads/catfish.php?parameters" ) );

	EXPECT_FALSE( robots.isAllowed( "/Fish.PHP" ) );
}

//////////////////////////////////////////////////////////////////////////////////////////
//                                                                                      //
// Test cases based on google's robots.txt specification                                //
// https://developers.google.com/webmasters/control-crawl-index/docs/robots_txt         //
//     #order-of-precedence-for-group-member-records                                    //
//                                                                                      //
//////////////////////////////////////////////////////////////////////////////////////////

// [url]							[allow]		[disallow]		[verdict]
// http://example.com/page			/p			/				allow
// http://example.com/folder/page	/folder/	/folder			allow
// http://example.com/page.htm		/page		/*.htm			undefined
// http://example.com/				/$			/				allow
// http://example.com/page.htm		/$			/				disallow
TEST( RobotsTest, GPrecedenceAllowDisallow ) {
	char robotsTxt[1024];

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/p", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/page" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/folder/", "/folder" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/folder/page" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/page.", "/*.htm" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed ( "/page.htm" ) );
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/$", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/" ));
		EXPECT_FALSE( robots.isAllowed( "/page.htm" ));
	}
}

TEST( RobotsTest, GPrecedenceDisallowAllow ) {
	char robotsTxt[1024];

	{
		generateTestReversedRobotsTxt( robotsTxt, 1024, "/p", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/page" ));
	}

	{
		generateTestReversedRobotsTxt( robotsTxt, 1024, "/folder/", "/folder" );

		TestRobots robots( robotsTxt, strlen( robotsTxt ));

		EXPECT_TRUE( robots.isAllowed( "/folder/page" ));
	}

	{
		generateTestReversedRobotsTxt( robotsTxt, 1024, "/page.", "/*.htm" );

		TestRobots robots( robotsTxt, strlen( robotsTxt ));

		EXPECT_FALSE( robots.isAllowed ( "/page.htm" ) );
	}

	{
		generateTestReversedRobotsTxt( robotsTxt, 1024, "/$", "/" );

		TestRobots robots( robotsTxt, strlen( robotsTxt ));

		EXPECT_TRUE( robots.isAllowed( "/" ));
		EXPECT_FALSE( robots.isAllowed( "/page.htm" ));
	}
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test cases based on RFC                                                    //
// http://www.robotstxt.org/norobots-rfc.txt                                  //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

TEST( RobotsTest, RFCPathMatchPrefixDisallow ) {
	char robotsTxt[1024];

	generateTestRobotsTxt( robotsTxt, 1024, "/", "/tmp" );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/tmp" ));
	EXPECT_FALSE( robots.isAllowed( "/tmp.html" ));
	EXPECT_FALSE( robots.isAllowed( "/tmp/a.html" ));
}

TEST( RobotsTest, RFCPathMatchPrefixAllow ) {
	char robotsTxt[1024];

	generateTestRobotsTxt( robotsTxt, 1024, "/tmp", "/" );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/tmp" ));
	EXPECT_TRUE( robots.isAllowed( "/tmp.html" ));
	EXPECT_TRUE( robots.isAllowed( "/tmp/a.html" ));
}

TEST( RobotsTest, RFCPathMatchPrefixDirDisallow ) {
	char robotsTxt[1024];

	generateTestRobotsTxt( robotsTxt, 1024, "/", "/tmp/" );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_TRUE( robots.isAllowed( "/tmp" ));
	EXPECT_FALSE( robots.isAllowed( "/tmp/" ));
	EXPECT_FALSE( robots.isAllowed( "/tmp/a.html" ));
}

TEST( RobotsTest, RFCPathMatchPrefixDirAllow ) {
	char robotsTxt[1024];

	generateTestRobotsTxt( robotsTxt, 1024, "/tmp/", "/" );

	TestRobots robots( robotsTxt, strlen(robotsTxt) );

	EXPECT_FALSE( robots.isAllowed( "/tmp" ));
	EXPECT_TRUE( robots.isAllowed( "/tmp/" ));
	EXPECT_TRUE( robots.isAllowed( "/tmp/a.html" ));
}

TEST( RobotsTest, DISABLED_RFCPathMatchUrlEncodeDisallow ) {
	char robotsTxt[1024];

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/a%3cd.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/a%3cd.html" ));
		EXPECT_FALSE( robots.isAllowed( "/a%3Cd.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/a%3Cd.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/a%3cd.html" ));
		EXPECT_FALSE( robots.isAllowed( "/a%3Cd.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/a%2fb.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/a%2fb.html" ));
		EXPECT_TRUE( robots.isAllowed( "/a/b.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/a/b.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/a%2fb.html" ));
		EXPECT_FALSE( robots.isAllowed( "/a/b.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/%7ejoe/index.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/~joe/index.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/", "/~joe/index.html" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/%7ejoe/index.html" ));
	}
}

TEST( RobotsTest, DISABLED_RFCPathMatchUrlEncodeAllow ) {
	char robotsTxt[1024];

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/a%3cd.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/a%3cd.html" ));
		EXPECT_TRUE( robots.isAllowed( "/a%3Cd.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/a%3Cd.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/a%3cd.html" ));
		EXPECT_TRUE( robots.isAllowed( "/a%3Cd.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/a%2fb.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/a%2fb.html" ));
		EXPECT_FALSE( robots.isAllowed( "/a/b.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/a/b.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_FALSE( robots.isAllowed( "/a%2fb.html" ));
		EXPECT_TRUE( robots.isAllowed( "/a/b.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/%7ejoe/index.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/~joe/index.html" ));
	}

	{
		generateTestRobotsTxt( robotsTxt, 1024, "/~joe/index.html", "/" );

		TestRobots robots( robotsTxt, strlen(robotsTxt) );

		EXPECT_TRUE( robots.isAllowed( "/%7ejoe/index.html" ));
	}
}

TEST( RobotsTest, DISABLED_RFCExample ) {
	char robotsTxt[1024] = "# /robots.txt for http://www.fict.org/\n"
	                       "# comments to webmaster@fict.org\n"
	                       "\n"
	                       "User-agent: unhipbot\n"
	                       "Disallow: /\n"
	                       "\n"
	                       "User-agent: webcrawler\n"
	                       "User-agent: excite\n"
	                       "Disallow: \n"
	                       "\n"
	                       "User-agent: *\n"
	                       "Disallow: /org/plans.html\n"
	                       "Allow: /org/\n"
	                       "Allow: /serv\n"
	                       "Allow: /~mak\n"
	                       "Disallow: /";
	{
		// unhipbot
		TestRobots robots( robotsTxt, strlen(robotsTxt), "unhipbot" );

		EXPECT_FALSE( robots.isAllowed( "/" ));
		EXPECT_FALSE( robots.isAllowed( "/index.html" ));
		//EXPECT_FALSE( robots.isAllowed( "/robots.txt" ));
		EXPECT_FALSE( robots.isAllowed( "/server.html" ));
		EXPECT_FALSE( robots.isAllowed( "/services/fast.html" ));
		EXPECT_FALSE( robots.isAllowed( "/services/slow.html" ));
		EXPECT_FALSE( robots.isAllowed( "/orgo.gif" ));
		EXPECT_FALSE( robots.isAllowed( "/org/about.html" ));
		EXPECT_FALSE( robots.isAllowed( "/org/plans.html" ));
		EXPECT_FALSE( robots.isAllowed( "/%7Ejim/jim.html" ));
		EXPECT_FALSE( robots.isAllowed( "/%7Emak/mak.html" ));
	}

	{
		// webcrawler
		TestRobots robots( robotsTxt, strlen(robotsTxt), "webcrawler" );

		EXPECT_TRUE( robots.isAllowed( "/" ));
		EXPECT_TRUE( robots.isAllowed( "/index.html" ));
		//EXPECT_TRUE( robots.isAllowed( "/robots.txt" ));
		EXPECT_TRUE( robots.isAllowed( "/server.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/fast.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/slow.html" ));
		EXPECT_TRUE( robots.isAllowed( "/orgo.gif" ));
		EXPECT_TRUE( robots.isAllowed( "/org/about.html" ));
		EXPECT_TRUE( robots.isAllowed( "/org/plans.html" ));
		EXPECT_TRUE( robots.isAllowed( "/%7Ejim/jim.html" ));
		EXPECT_TRUE( robots.isAllowed( "/%7Emak/mak.html" ));
	}

	{
		// excite
		TestRobots robots( robotsTxt, strlen(robotsTxt), "excite" );

		EXPECT_TRUE( robots.isAllowed( "/" ));
		EXPECT_TRUE( robots.isAllowed( "/index.html" ));
		//EXPECT_TRUE( robots.isAllowed( "/robots.txt" ));
		EXPECT_TRUE( robots.isAllowed( "/server.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/fast.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/slow.html" ));
		EXPECT_TRUE( robots.isAllowed( "/orgo.gif" ));
		EXPECT_TRUE( robots.isAllowed( "/org/about.html" ));
		EXPECT_TRUE( robots.isAllowed( "/org/plans.html" ));
		EXPECT_TRUE( robots.isAllowed( "/%7Ejim/jim.html" ));
		EXPECT_TRUE( robots.isAllowed( "/%7Emak/mak.html" ));
	}

	{
		// other
		TestRobots robots( robotsTxt, strlen(robotsTxt), "testbot" );

		EXPECT_FALSE( robots.isAllowed( "/" ));
		EXPECT_FALSE( robots.isAllowed( "/index.html" ));
		//EXPECT_TRUE( robots.isAllowed( "/robots.txt" ));
		EXPECT_TRUE( robots.isAllowed( "/server.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/fast.html" ));
		EXPECT_TRUE( robots.isAllowed( "/services/slow.html" ));
		EXPECT_FALSE( robots.isAllowed( "/orgo.gif" ));
		EXPECT_TRUE( robots.isAllowed( "/org/about.html" ));
		EXPECT_FALSE( robots.isAllowed( "/org/plans.html" ));
		EXPECT_FALSE( robots.isAllowed( "/%7Ejim/jim.html" ));
		EXPECT_TRUE( robots.isAllowed( "/%7Emak/mak.html" ));
	}
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Test real robots.txt                                                       //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

static std::string loadRobotsFile( const char *fileName ) {
	std::ifstream file( fileName );
	if ( file.is_open() ) {
		std::stringstream contents;
		contents << file.rdbuf();
		file.close();
		return contents.str();
	}

	return "";
}

// empty file
TEST( RobotsTest, RRobotsSpeedtestNet ) {
	std::string robotsTxt = loadRobotsFile( "robots/speedtest.net" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
}

// comments only
TEST( RobotsTest, RRobotsThekitchnCom ) {
	std::string robotsTxt = loadRobotsFile( "robots/thekitchn.com" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_FALSE( robots.isDefaultUserAgentFound() );
	EXPECT_TRUE( robots.isDefaultRulesEmpty() );

	EXPECT_TRUE( robots.isAllowed( "/" ) );
	EXPECT_TRUE( robots.isAllowed( "/index.html" ) );
}

// wildcard use
TEST( RobotsTest, RRobotsRedditCom ) {
	std::string robotsTxt = loadRobotsFile( "robots/reddit.com" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_FALSE( robots.isDefaultRulesEmpty() );

	EXPECT_FALSE( robots.isAllowed( "/r/GameDeals/comments/4csg7b/steam_baldurs_gate_enhanced_edition_75_off_499/?sort=top") );
	EXPECT_FALSE( robots.isAllowed( "/r/GameDeals/search?q=humble+bundle&restrict_sr=on") );
	EXPECT_TRUE( robots.isAllowed( "/r/GameDeals/hot/") );
	EXPECT_FALSE( robots.isAllowed( "/r/GameDeals.json" ) );
}

// many user agents
TEST( RobotsTest, RRobotsNeedromCom ) {
	std::string robotsTxt = loadRobotsFile( "robots/needrom.com" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_FALSE( robots.isDefaultRulesEmpty() );

	EXPECT_FALSE( robots.isAllowed( "/wp-admin/" ) );
	EXPECT_TRUE( robots.isAllowed( "/download/galaxy-ace-duos-s6802/" ) );
}

// many disallow (no wildcard)
TEST( RobotsTest, RRobotsStateGov ) {
	std::string robotsTxt = loadRobotsFile( "robots/state.gov" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_FALSE( robots.isDefaultRulesEmpty() );

	EXPECT_TRUE( robots.isAllowed( "/documents/organization/81807.pdf" ) );
	EXPECT_FALSE( robots.isAllowed( "/g/abc") );
}

// many disallow (with wildcard)
TEST( RobotsTest, RRobotsBoeEs ) {
	std::string robotsTxt = loadRobotsFile( "robots/boe.es" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_FALSE( robots.isDefaultRulesEmpty() );

	EXPECT_TRUE( robots.isAllowed( "/buscar/" ) );
	EXPECT_FALSE( robots.isAllowed( "/buscar/doc.php?id=BOE-B-2015-14008") );
}

// url encoded / utf-8
TEST( RobotsTest, DISABLED_RRobotsWikipediaOrg ) {
	std::string robotsTxt = loadRobotsFile( "robots/wikipedia.org" );

	TestRobots robots( robotsTxt.c_str(), robotsTxt.length() );

	EXPECT_FALSE( robots.isUserAgentFound() );
	EXPECT_TRUE( robots.isRulesEmpty() );
	EXPECT_TRUE( robots.isDefaultUserAgentFound() );
	EXPECT_FALSE( robots.isDefaultRulesEmpty() );

}
