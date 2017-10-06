#include <gtest/gtest.h>
#include "fctypes.h"

TEST(FctypesTest, VerifyStrnstrTrue) {
	const char *haystack[] = {
		"/abc/def//in.123456788",
		"aabcdefghijkl",
		"abcdefgghijkl",
		"abcdefggghijk",
		"aaaaabcdefggg"
	};

	const char *needle[] = {
		"/in.",
		"abc",
		"gh",
		"ggh",
		"aaaab"
	};

	ASSERT_EQ( sizeof( haystack ) / sizeof( haystack[0] ), sizeof( needle ) / sizeof( needle[0] ) );

	size_t len = sizeof( haystack ) / sizeof( haystack[0] );
	for ( size_t i = 0; i < len; ++i ) {
		SCOPED_TRACE( haystack[i] );
		EXPECT_TRUE( ( strnstr( haystack[i], needle[i], strlen( haystack[i] ) ) != NULL ) );
	}
}

TEST(FctypesTest, UrlEncode) {
	std::vector<std::tuple<const char *, const char*>> test_cases = {
		std::make_tuple("💩", "%F0%9F%92%A9")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		size_t inputLen = strlen(std::get<0>(*it));
		char dest[1024];
		EXPECT_EQ(strlen(std::get<1>(*it)), urlEncode(dest, 1024, std::get<0>(*it), inputLen));
		EXPECT_STREQ(std::get<1>(*it), dest);
	}
}