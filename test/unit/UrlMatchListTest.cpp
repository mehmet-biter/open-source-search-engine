#include <gtest/gtest.h>
#include "UrlMatchList.h"
#include "Url.h"

class TestUrlMatchList : public UrlMatchList {
public:
	TestUrlMatchList(const char *filename)
		: UrlMatchList() {
		m_filename = filename;
	}

	bool isUrlMatched(const char *urlStr) {
		Url url;
		url.set(urlStr);

		return UrlMatchList::isUrlBlocked(url);
	}
	using UrlMatchList::load;
};

TEST(UrlMatchListTest, Domain) {
	TestUrlMatchList urlMatchList("blocklist/domain.txt");
	urlMatchList.load();

	//regex   badsite.com https?://www\.badsite\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("httpp://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/page.html"));

	//regex   httponly.com    http://www\.httponly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.httponly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://subdomain.httponly.com/"));

	//regex   httpsonly.com   https://www\.httpsonly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://subdomain.httpsonly.com/"));

	//domain  allsubdomain.com
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://something.com/sub1.allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://something.com/www.allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://sub1.diffdomain.com/"));

	//regex   onlyroot.com    http://www\.onlyroot\.com/$
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.onlyroot.com/page.html"));

	//domain  example.com allow=,www
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub2.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.sub1.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://example.com/"));

	//host    specific.host.com
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://specific.host.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://specific.host.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.host.com/"));

	//tld     my,dk
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://specific.host.dk/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.my/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.com.my/"));

	//host    www.somesite.com    /badpath/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.somesite.com/badpath/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.somesite.com/badpath/me.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.somesite.com/path/me.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://sub.somesite.com/badpath/"));

	//regex   itsybitsy.com ^https?://(www\.|nursery\.|)itsybitsy\.com/spider/.+
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://nursery.itsybitsy.com/spider/waterspout.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://rhyme.itsybitsy.com/spider/waterspout.html"));

	//domain  allowrootdomainrootpages.com allow=, allowrootpages
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/abc.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/def.html?param1=value1"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/d1/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/d1/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/d1/d2/jkl.html"));

	//domain  allowdomainrootpages.com allow=,www allowrootpages
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowdomainrootpages.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.allowdomainrootpages.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.allowdomainrootpages.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowdomainrootpages.com/abc.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.allowdomainrootpages.com/def.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowdomainrootpages.com/abc.html?param1=value1"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.allowdomainrootpages.com/def.html?param1=value1"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowdomainrootpages.com/d1/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowdomainrootpages.com/d1/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowdomainrootpages.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowdomainrootpages.com/d1/d2/jkl.html"));

	//domain  allowrootdomainindexpage.com allow=, allowindexpage
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainindexpage.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainindexpage.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.allowrootdomainindexpage.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainindexpage.com/?param=value"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainindexpage.com/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainindexpage.com/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainindexpage.com/d1/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainindexpage.com/d1/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainindexpage.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainindexpage.com/d1/d2/jkl.html"));

	//domain  allowdomainindexpage.com allow=,www allowindexpage
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowdomainindexpage.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.allowdomainindexpage.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.allowdomainindexpage.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowdomainindexpage.com/?param=value"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.allowdomainindexpage.com/?param=value"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowdomainindexpage.com/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowdomainindexpage.com/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowdomainindexpage.com/d1/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowdomainindexpage.com/d1/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowdomainindexpage.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowdomainindexpage.com/d1/d2/jkl.html"));
}

TEST(UrlMatchListTest, Path) {
	TestUrlMatchList urlMatchList("blocklist/path.txt");
	urlMatchList.load();

	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/wp-admin/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/tag/wp-admin/"));

	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.com/file1.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/file1.html"));
}
