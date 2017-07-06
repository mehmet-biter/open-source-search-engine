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

	//regex   badsite.com https?://www\.badsite\.com/
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.badsite.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.badsite.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("httpp://www.badsite.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.badsite.com/page.html"));

	//regex   httponly.com    http://www\.httponly\.com/
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.httponly.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://www.httponly.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://subdomain.httponly.com/"));

	//regex   httpsonly.com   https://www\.httpsonly\.com/
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://subdomain.httpsonly.com/"));

	//domain  allsubdomain.com
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://something.com/sub1.allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://something.com/www.allsubdomain.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://sub1.diffdomain.com/"));

	//regex   onlyroot.com    http://www\.onlyroot\.com/$
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/page.html"));

	//domain  example.com allow=,www
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.sub2.example.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.example.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.sub1.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://example.com/"));

	//host    specific.host.com
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://specific.host.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://specific.host.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://www.host.com/"));

	//tld     my,dk
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://specific.host.dk/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.my/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.com.my/"));

	//host    www.somesite.com    /badpath/
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.somesite.com/badpath/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.somesite.com/badpath/me.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.somesite.com/path/me.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://sub.somesite.com/badpath/"));

	//regex   itsybitsy.com ^https?://(www\.|nursery\.|)itsybitsy\.com/spider/.+
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.itsybitsy.com/spider/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://nursery.itsybitsy.com/spider/waterspout.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://rhyme.itsybitsy.com/spider/waterspout.html"));

	//domain  allowrootdomainrootpages.com allow=, allowrootpages
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowrootdomainrootpages.com"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainrootpages.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowrootdomainrootpages.com/abc.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowrootdomainrootpages.com/def.html?param1=value1"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainrootpages.com/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowrootdomainrootpages.com/d1/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainrootpages.com/d1/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowrootdomainrootpages.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainrootpages.com/d1/d2/jkl.html"));

	//domain  allowdomainrootpages.com allow=,www allowrootpages
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowdomainrootpages.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.allowdomainrootpages.com"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub.allowdomainrootpages.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowdomainrootpages.com/abc.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.allowdomainrootpages.com/def.html"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowdomainrootpages.com/abc.html?param1=value1"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.allowdomainrootpages.com/def.html?param1=value1"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowdomainrootpages.com/d1/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowdomainrootpages.com/d1/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowdomainrootpages.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowdomainrootpages.com/d1/d2/jkl.html"));

	//domain  allowrootdomainindexpage.com allow=, allowindexpage
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowrootdomainindexpage.com"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainindexpage.com"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub.allowrootdomainindexpage.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowrootdomainindexpage.com/?param=value"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowrootdomainindexpage.com/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainindexpage.com/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowrootdomainindexpage.com/d1/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainindexpage.com/d1/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowrootdomainindexpage.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowrootdomainindexpage.com/d1/d2/jkl.html"));

	//domain  allowdomainindexpage.com allow=,www allowindexpage
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowdomainindexpage.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.allowdomainindexpage.com"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub.allowdomainindexpage.com"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://allowdomainindexpage.com/?param=value"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.allowdomainindexpage.com/?param=value"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowdomainindexpage.com/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowdomainindexpage.com/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowdomainindexpage.com/d1/abc.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowdomainindexpage.com/d1/def.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://allowdomainindexpage.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.allowdomainindexpage.com/d1/d2/jkl.html"));
}

TEST(UrlBlockListTest, Path) {
	TestUrlBlockList urlBlockList("blocklist/path.txt");
	urlBlockList.load();

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.example.com/wp-admin/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/tag/wp-admin/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.host.com/file1.html"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.example.com/file1.html"));
}
