#include "gtest/gtest.h"

#include "Pos.h"
#include "Words.h"

#define MAX_BUF_SIZE 1024

TEST(PosTest, FilterAllCaps) {
	char *input_strs[] = {
	    "ALL YOUR BASES ARE BELONG TO US!!!",
	    "The quick brown FOX jumps over THE lazy dog",
	    "PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS",
	    "QUIZDELTAGERNE SPISTE JORDBÆR MED FLØDE, MENS CIRKUSKLOVNEN WALTHER SPILLEDE PÅ XYLOFON"
	};

	const char *expected_output[] = {
	    "All Your Bases Are Belong To Us!!!",
	    "The quick brown FOX jumps over THE lazy dog",
	    "Pack My Box With Five Dozen Liquor Jugs",
	    "QUIZDELTAGERNE SPISTE JORDBÆR MED FLØDE, MENS CIRKUSKLOVNEN WALTHER SPILLEDE PÅ XYLOFON"
	};

	ASSERT_EQ(sizeof(input_strs)/sizeof(input_strs[0]), sizeof(expected_output)/sizeof(expected_output[0]));

	size_t len = sizeof(input_strs) / sizeof(input_strs[0]);
	for (size_t i = 0; i < len; i++) {
		Words words;
		Pos pos;
		char buf[MAX_BUF_SIZE];

		ASSERT_TRUE(words.set(input_strs[i], true, 0));

		int32_t len = pos.filter(buf, buf + MAX_BUF_SIZE, &words);

		EXPECT_EQ(strlen(expected_output[i]), len);
		EXPECT_STREQ(expected_output[i], buf);
	}

}
