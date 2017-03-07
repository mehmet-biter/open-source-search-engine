#include <gtest/gtest.h>
#include "UrlBlockList.h"
#include "Url.h"

class TestUrlBlockList : public UrlBlockList {
public:
	TestUrlBlockList(const char *filename)
		: UrlBlockList() {
		m_filename = filename;
	}

	bool isUrlBlocked(const char *urlStr) {
		Url url;
		url.set(urlStr);

		return UrlBlockList::isUrlBlocked(url);
	}
	using UrlBlockList::load;
};

TEST(UrlBlockListTest, Domain) {
	TestUrlBlockList urlBlockList("blocklist/domain.txt");
	urlBlockList.load();

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.badsite.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.badsite.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("httpp://www.badsite.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.badsite.com/page.html"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.httponly.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://www.httponly.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://subdomain.httponly.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://subdomain.httpsonly.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://something.com/sub1.allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://something.com/www.allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://sub1.diffdomain.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/page.html"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.sub2.example.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.example.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.sub1.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://example.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://specific.host.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://specific.host.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://www.host.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://specific.host.dk/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.my/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.com.my/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.somesite.com/badpath/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.somesite.com/badpath/me.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.somesite.com/path/me.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://sub.somesite.com/badpath/"));

	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.itsybitsy.com/spider/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.itsybitsy.com/spider/waterspout.html"));
}

TEST(UrlBlockListTest, Path) {
	TestUrlBlockList urlBlockList("blocklist/path.txt");
	urlBlockList.load();

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.example.com/wp-admin/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/tag/wp-admin/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.com/file1.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.example.com/file1.html"));
}
