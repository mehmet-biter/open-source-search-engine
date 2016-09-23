#include <gtest/gtest.h>

#include "Words.h"

TEST(WordsTest, VerifySize) {
	// set c to a curling quote in unicode
	int32_t c = 0x201c; // 0x235e;

	// encode it into utf8
	char dst[5];

	// point to it
	char *p = dst;

	// put space in there
	*p++ = ' ';

	// "numBytes" is how many bytes it stored into 'dst"
	int32_t numBytes = utf8Encode ( c , p );

	// must be 3 bytes
	EXPECT_EQ(3, numBytes);

	// check it
	int32_t size = getUtf8CharSize(p);
	EXPECT_EQ(3, size);

	// is that punct
	EXPECT_TRUE(is_punct_utf8(p));
}
