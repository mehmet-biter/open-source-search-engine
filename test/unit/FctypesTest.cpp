#include "gtest/gtest.h"

#include "Mem.h"
#include "fctypes.h"

TEST(FctypesTest, VerifyStrnstrTrue) {
	char* haystack[] = {
		"/abc/def//in.123456788",
		"aabcdefghijkl",
		"abcdefgghijkl",
		"abcdefggghijk",
		"aaaaabcdefggg"
	};

	char* needle[] = {
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
