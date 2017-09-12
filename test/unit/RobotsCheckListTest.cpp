#include <gtest/gtest.h>
#include "RobotsCheckList.h"

class TestRobotsCheckList : public RobotsCheckList {
public:
	TestRobotsCheckList(const char *filename)
		: RobotsCheckList() {
		m_filename = filename;
	}

	using RobotsCheckList::load;
};

TEST(RobotsCheckListTest, BlockList) {
	TestRobotsCheckList robotsCheckList("blocklist/robots.txt");
	robotsCheckList.load();

	// full match
	EXPECT_TRUE(robotsCheckList.isHostBlocked("host1.example.com"));

	// partial match
	EXPECT_TRUE(robotsCheckList.isHostBlocked("sub1.host2.example.com"));
	EXPECT_TRUE(robotsCheckList.isHostBlocked("abc.host2.example.com"));
	EXPECT_FALSE(robotsCheckList.isHostBlocked("abchost2.example.com"));

	EXPECT_TRUE(robotsCheckList.isHostBlocked("badexample.com"));
	EXPECT_TRUE(robotsCheckList.isHostBlocked("abadexample.com"));
	EXPECT_TRUE(robotsCheckList.isHostBlocked("averybadexample.com"));
	EXPECT_FALSE(robotsCheckList.isHostBlocked("adexample.com"));
}