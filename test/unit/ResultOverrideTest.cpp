#include <gtest/gtest.h>

#include "ResultOverride.h"
#include "Url.h"

TEST(ResultOverrideTest, Simple) {
	Url url;
	ResultOverride resultOverride("title", "summary");
	EXPECT_STREQ("title", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("summary", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceHostStart) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("{HOST} is at the title start", "{HOST} is at the summary start");
	EXPECT_STREQ("www.example.com is at the title start", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("www.example.com is at the summary start", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceHostMiddle) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("sometimes we want {HOST} at the title middle", "sometimes we want {HOST} at the summary middle");
	EXPECT_STREQ("sometimes we want www.example.com at the title middle", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("sometimes we want www.example.com at the summary middle", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceHostEnd) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("we want title end {HOST}", "we want summary end {HOST}");
	EXPECT_STREQ("we want title end www.example.com", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("we want summary end www.example.com", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceDomainStart) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("{DOMAIN} is at the title start", "{DOMAIN} is at the summary start");
	EXPECT_STREQ("example.com is at the title start", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("example.com is at the summary start", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceDomainMiddle) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("sometimes we want {DOMAIN} at the title middle", "sometimes we want {DOMAIN} at the summary middle");
	EXPECT_STREQ("sometimes we want example.com at the title middle", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("sometimes we want example.com at the summary middle", resultOverride.getSummary(url).c_str());
}

TEST(ResultOverrideTest, ReplaceDomainEnd) {
	Url url;
	url.set("http://www.example.com/");

	ResultOverride resultOverride("we want title end {DOMAIN}", "we want summary end {DOMAIN}");
	EXPECT_STREQ("we want title end example.com", resultOverride.getTitle(url).c_str());
	EXPECT_STREQ("we want summary end example.com", resultOverride.getSummary(url).c_str());
}
