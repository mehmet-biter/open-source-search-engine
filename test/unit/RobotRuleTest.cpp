#include "gtest/gtest.h"
#include "RobotRule.h"
#include "Url.h"
#include "Log.h"

#define TEST_DOMAIN "http://example.com"

//
// test class
//
class TestRobotRule : public RobotRule {
public:
	TestRobotRule( const char *rulePath )
		: RobotRule( false, rulePath, strlen( rulePath ) ) {
	}

	bool isMatching( const char *urlPath ) {
		char urlStr[1024];
		snprintf( urlStr, 1024, TEST_DOMAIN "%s", urlPath );

		Url url;
		url.set( urlStr );

		return RobotRule::isMatching( &url );
	}
};

void expectRobotRule( const char *urlPath, const char *rulePath, bool expectedMatching ) {
	TestRobotRule robotRule( rulePath );

	EXPECT_EQ( expectedMatching, robotRule.isMatching( urlPath ) );
}

//
// Test cases roughly based on Matching Wildcards article on Dr. Dobb's
// http://www.drdobbs.com/architecture-and-design/matching-wildcards-an-empirical-way-to-t/240169123#ListingOne
//
TEST( RobotRuleTest, WildcardCharacter ) {
	expectRobotRule("", "*", true);
	expectRobotRule("/", "/*", true);
	expectRobotRule("/a", "/a*", true);
	expectRobotRule("/a", "/*a", true);
}

TEST( RobotRuleTest, WildcardCharacterRepeat ) {
	// Cases with repeating character sequences.
	expectRobotRule("/abcccd", "*ccd", true);
	expectRobotRule("/mississipissippi", "*issip*ss*", true);
	expectRobotRule("/xxxx*zzzzzzzzy*f", "/xxxx*zzy*fffff", false);
	expectRobotRule("/xxxx*zzzzzzzzy*f", "/xxx*zzy*f", true);
	expectRobotRule("/xxxxzzzzzzzzyf", "/xxxx*zzy*fffff", false);
	expectRobotRule("/xxxxzzzzzzzzyf", "/xxxx*zzy*f", true);
	expectRobotRule("/xyxyxyzyxyz", "/xy*z*xyz", true);
	expectRobotRule("/mississippi", "*sip*", true);
	expectRobotRule("/xyxyxyxyz", "/xy*xyz", true);
	expectRobotRule("/mississippi", "/mi*sip*", true);
	expectRobotRule("/ababac", "/*abac*", true);
	expectRobotRule("/aaazz", "/a*zz*", true);
	expectRobotRule("/a12b12", "*12*23", false);
	expectRobotRule("/a12b12", "*12*12*", true);
}

TEST( RobotRuleTest, WildcardCharacterHaystack ) {
	// Additional cases where the '*' char appears in haystack
	expectRobotRule("/*", "*", true);
	expectRobotRule("/a*abab", "/a*b", true);
	expectRobotRule("/a*r", "/a*", true);
	expectRobotRule("/a*ar", "/a*aar", false);
}

TEST( RobotRuleTest, WildcardDouble ) {
	// More double wildcard scenarios.
	expectRobotRule("/XYXYXYZYXYz", "/XY*Z*XYz", true);
	expectRobotRule("/missisSIPpi", "*SIP*", true);
	expectRobotRule("/mississipPI", "*issip*PI", true);
	expectRobotRule("/xyxyxyxyz", "/xy*xyz", true);
	expectRobotRule("/miSsissippi", "/mi*sip*", true);
	expectRobotRule("/miSsissippi", "/mi*Sip*", false);
	expectRobotRule("/abAbac", "*Abac*", true);
	expectRobotRule("/abAbac", "*Abac*", true);
	expectRobotRule("/aAazz", "/a*zz*", true);
	expectRobotRule("/A12b12", "*12*23", false);
	expectRobotRule("/a12B12", "*12*12*", true);
	expectRobotRule("/oWn", "/*oWn*", true);
}

TEST( RobotRuleTest, WildcardMultiple ) {
	// Many-wildcard scenarios.
	expectRobotRule("/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", "/a*a*a*a*a*a*aa*aaa*a*a*b", true);
	expectRobotRule("/abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffafagaagggagaaaaaaaab", "/*a*b*ba*ca*a*aa*aaa*fa*ga*b*", true);
	expectRobotRule("/abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffafagaagggagaaaaaaaab", "/*a*b*ba*ca*a*x*aaa*fa*ga*b*", false);
	expectRobotRule("/abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffafagaagggagaaaaaaaab", "/*a*b*ba*ca*aaaa*fa*ga*gggg*b*", false);
	expectRobotRule("/abababababababababababababababababababaacacacacacacacadaeafagahaiajakalaaaaaaaaaaaaaaaaaffafagaagggagaaaaaaaab", "/*a*b*ba*ca*aaaa*fa*ga*ggg*b*", true);
	expectRobotRule("/aaabbaabbaab", "*aabbaa*a*", true);
	expectRobotRule("/a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*", "/a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*", true);
	expectRobotRule("/aaaaaaaaaaaaaaaaa", "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*", true);
	expectRobotRule("/aaaaaaaaaaaaaaaa", "*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*", false);
	expectRobotRule("/abc*abcd*abcde*abcdef*abcdefg*abcdefgh*abcdefghi*abcdefghij*abcdefghijk*abcdefghijkl*abcdefghijklm*abcdefghijklmn", "/abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*", false);
	expectRobotRule("/abc*abcd*abcde*abcdef*abcdefg*abcdefgh*abcdefghi*abcdefghij*abcdefghijk*abcdefghijkl*abcdefghijklm*abcdefghijklmn", "/abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*", true);
	expectRobotRule("/abc*abcd*abcd*abc*abcd*abcd*abc*abcd*abc*abc*abcd", "/abc*abc*abc*abc*abc*abc*abc*abc*abc*abc*abcd", true);
	expectRobotRule("/abc", "********a********b********c********", true);
	expectRobotRule("/********a********b********c********", "/abc", false);
	expectRobotRule("/abc", "/********a********b********b********", false);
	expectRobotRule("/*abc*", "/***a*b*c***", true);

	expectRobotRule( "/--------------------------------abc-def-", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-" , true );
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*" , true );

	expectRobotRule( "/acgfhdhbbcfbhchacchigdhfibhcifabnhieahnaaibcafhigbihaihj", "/*a*b*c*d*e*f*g*h*i*j", true );
	expectRobotRule( "/ababcbdccedfdgeheifjfkgghhiijjkkllmmnnop", "/*a*b*c*d*e*f*g*h*i*j*k*l*m*n*o*p", true );
}

TEST( RobotRuleTest, WildcardMultipleLineAnchor ) {
	expectRobotRule("/abc*abcd*abcd*abc*abcd", "/abc*abc*abc*abc*abc$", false);
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-$" , false );
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*f$" , true );
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*c-*f$" , true );
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-c-f$" , false );
	expectRobotRule( "/---------------------------------abc-def", "/-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*z-f$" , false );
	expectRobotRule("/aa", "*$", true);
	expectRobotRule("/abefcdgiescdfimde", "/ab*cd*i*de$", true);
}

