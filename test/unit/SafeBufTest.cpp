#include <gtest/gtest.h>
#include "SafeBuf.h"

TEST(SafeBufTest, safeReplace2) {
	std::vector<std::tuple<const char *, const char*, const char*, const char*>> test_cases = {
		std::make_tuple("test<br>", "<br>", "", "test"),
		std::make_tuple("<br>test<br>", "<br>", "", "test"),
		std::make_tuple("<br>test", "<br>", "", "test"),
		std::make_tuple("test<br><br>", "<br>", "", "test"),
		std::make_tuple("test", "<br>", "", "test")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		SafeBuf sb;
		sb.set(std::get<0>(*it));
		sb.safeReplace2(std::get<1>(*it), strlen(std::get<1>(*it)), std::get<2>(*it), strlen(std::get<2>(*it)));
		EXPECT_STREQ(std::get<3>(*it), sb.getBufStart());
	}
}
