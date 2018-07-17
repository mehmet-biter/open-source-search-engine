#include <gtest/gtest.h>
#include "UrlMatchList.h"
#include "Url.h"

class TestUrlMatchList : public UrlMatchList {
public:
	TestUrlMatchList(const char *filename)
		: UrlMatchList(filename) {
	}

	bool isUrlMatched(const char *urlStr) {
		Url url;
		url.set(urlStr);

		return UrlMatchList::isUrlMatched(url);
	}
	using UrlMatchList::load;
};

TEST(UrlMatchListTest, Regex) {
	TestUrlMatchList urlMatchList("blocklist/regex.txt");
	urlMatchList.load();

	//regex https?://www\.badsite\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("httpp://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/page.html"));

	//regex http://www\.httponly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.httponly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://subdomain.httponly.com/"));

	//regex https://www\.httpsonly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://subdomain.httpsonly.com/"));

	//regex http://www\.onlyroot\.com/$
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.onlyroot.com/page.html"));

	//regex ^https?://(www\.|nursery\.|)itsybitsy\.com/spider/.+
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://nursery.itsybitsy.com/spider/waterspout.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://rhyme.itsybitsy.com/spider/waterspout.html"));

	//regex https?://[^/]+/file1.html
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.com/file1.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/file1.html"));
}

TEST(UrlMatchListTest, DomainSchemeSubdomain) {
	TestUrlMatchList urlMatchList("blocklist/scheme.txt");
	urlMatchList.load();

	//domain httponly.com AND scheme http AND subdomain www
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.httponly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://subdomain.httponly.com/"));

	//domain httpsonly.com AND scheme https AND subdomain www
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://subdomain.httpsonly.com/"));
}

TEST(UrlMatchListTest, DomainRegex) {
	TestUrlMatchList urlMatchList("blocklist/domain.txt");
	urlMatchList.load();

	//domain badsite.com AND regex https?://www\.badsite\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("httpp://www.badsite.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.badsite.com/page.html"));

	//domain httponly.com AND regex http://www\.httponly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.httponly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.httponly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://subdomain.httponly.com/"));

	//domain httpsonly.com AND regex https://www\.httpsonly\.com/
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.httpsonly.com/page.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.httpsonly.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://subdomain.httpsonly.com/"));

	//domain onlyroot.com AND regex http://www\.onlyroot\.com/$
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.onlyroot.com/page.html"));

	//domain itsybitsy.com AND regex ^https?://(www\.|nursery\.|)itsybitsy\.com/spider/.+
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.itsybitsy.com/spider/waterspout.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://nursery.itsybitsy.com/spider/waterspout.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://rhyme.itsybitsy.com/spider/waterspout.html"));
}

TEST(UrlMatchListTest, DomainDomain) {
	TestUrlMatchList urlMatchList("blocklist/domain.txt");
	urlMatchList.load();

	//domain allsubdomain.com
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub2.allsubdomain.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://something.com/sub1.allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://something.com/www.allsubdomain.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://sub1.diffdomain.com/"));

	//domain example.com AND NOT subdomain ,www
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub2.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.sub1.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://example.com/"));

	//domain allowrootdomainrootpages.com AND NOT subdomain ,
	//domain allowrootdomainrootpages.com AND NOT pathcriteria rootpages
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/abc.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/def.html?param1=value1"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/d1/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/d1/def.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://allowrootdomainrootpages.com/d1/d2/xyz.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.allowrootdomainrootpages.com/d1/d2/jkl.html"));

	//domain allowdomainrootpages.com AND NOT subdomain ,www
	//domain allowdomainrootpages.com AND NOT pathcriteria rootpages
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

	//domain allowrootdomainindexpage.com AND NOT subdomain ,
	//domain allowrootdomainindexpage.com AND NOT pathcriteria indexpage
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

	//domain allowdomainindexpage.com AND NOT subdomain ,www
	//domain allowdomainindexpage.com AND NOT pathcriteria indexpage
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

TEST(UrlMatchListTest, Host) {
	TestUrlMatchList urlMatchList("blocklist/host.txt");
	urlMatchList.load();

	//host    specific.host.com
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://specific.host.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://specific.host.com:3001/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://specific.host.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.host.com/"));
}

TEST(UrlMatchListTest, HostPath) {
	TestUrlMatchList urlMatchList("blocklist/host.txt");
	urlMatchList.load();

	//host www.somesite.com AND path /badpath/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.somesite.com/badpath/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.somesite.com/badpath/me.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.somesite.com/path/me.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://sub.somesite.com/badpath/"));
}

TEST(UrlMatchListTest, HostPort) {
	TestUrlMatchList urlMatchList("blocklist/host.txt");
	urlMatchList.load();

	//host port.host.com AND port 3001
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://port.host.com:3001/example/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://port.host.com:3001/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://port.host.com:3001/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://port.host.com:3002/example/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://port.host.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://port.host.com"));

	//host ssl.host.com AND port 443
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://ssl.host.com/example/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://ssl.host.com:443/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://ssl.host.com:3001/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://ssl.host.com:3001/"));
}

TEST(UrlMatchListTest, DomainTld) {
	TestUrlMatchList urlMatchList("blocklist/domain.txt");
	urlMatchList.load();

	//tld     my,dk
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://specific.host.dk/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.my/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.host.com.my/"));
}

TEST(UrlMatchListTest, HostHostSuffix) {
	TestUrlMatchList urlMatchList("blocklist/host.txt");
	urlMatchList.load();

	//host hostsuffix01.com matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix01.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix01.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://hostsuffix01.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix01.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix01.com"));

	//host .hostsuffix02.com matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix02.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix02.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://hostsuffix02.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix02.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix02.com"));

	//host hostsuffix03.co.uk matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix03.co.uk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix03.co.uk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://hostsuffix03.co.uk"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix03.co.uk"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix03.co.uk"));

	//host .hostsuffix04.co.uk matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix04.co.uk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix04.co.uk"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://hostsuffix04.co.uk"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix04.co.uk"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix04.co.uk"));

	//host hostsuffix05.a.se matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix05.a.se"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix05.a.se"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://hostsuffix05.a.se"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix05.a.se"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix05.a.se"));

	//host .hostsuffix06.a.se matchsuffix
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub.hostsuffix06.a.se"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://sub1.sub.hostsuffix06.a.se"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://hostsuffix06.a.se"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://bhostsuffix06.a.se"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://jostsuffix06.a.se"));
}

TEST(UrlMatchListTest, Middomain) {
	TestUrlMatchList urlMatchList("blocklist/middomain.txt");
	urlMatchList.load();

	//middomain example
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.uk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.dk/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://sub.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.examples.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.aexample.com/"));

	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ad"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ae"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.af"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ag"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ai"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.al"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.am"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ao"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ar"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.as"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.at"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.au"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.az"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ba"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.bd"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.be"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bf"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.bh"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bi"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bj"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.bn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.bo"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.br"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bs"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.bt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.bw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.by"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.bz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ca"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cd"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cf"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ch"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ci"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ck"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.co"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.cr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.cu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cv"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.cy"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.de"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.dj"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.dk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.dm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.do"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.dz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ec"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ee"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.eg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.es"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.et"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.fi"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.fj"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.fm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.fr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ga"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ge"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.gh"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.gi"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gp"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.gt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.gy"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.hk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.hn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.hr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ht"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.hu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.id"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ie"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.il"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.im"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.in"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.iq"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.is"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.it"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.je"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.jm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.jo"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.jp"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ke"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.kh"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ki"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.kg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.kr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.kw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.kz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.la"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.lb"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.li"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.lk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ls"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.lt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.lu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.lv"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ly"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ma"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.md"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.me"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ml"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.mm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ms"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.mt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mv"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.mw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.mx"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.my"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.mz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.na"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.nf"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ng"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ni"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ne"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.nl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.no"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.np"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.nr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.nu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.nz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.om"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.pa"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.pe"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.pg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ph"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.pk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.pl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.pn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.pr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ps"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.pt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.py"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.qa"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ro"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ru"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.rw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.sa"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.sb"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sc"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.se"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.sg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sh"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.si"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.sl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.so"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.sr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.st"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.sv"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.td"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.th"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.tj"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tl"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.to"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.tr"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.tt"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.tw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.tz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.ua"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ug"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.uk"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.uy"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.uz"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.vc"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.ve"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.vg"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.vi"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com.vn"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.vu"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.ws"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.rs"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.za"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.zm"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.co.zw"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.cat"));
}

TEST(UrlMatchListTest, Subdomain) {
	TestUrlMatchList urlMatchList("blocklist/subdomain.txt");
	urlMatchList.load();

	//subdomain da,en
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://da.example.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://da.example.dk/abc.html"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://en.example.com"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://en.example.dk/abc.html"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://dan.example.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://eng.example.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://sub.example.com/"));
}

TEST(UrlMatchListTest, PathPath) {
	TestUrlMatchList urlMatchList("blocklist/path.txt");
	urlMatchList.load();

	//path  /wp-admin/
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/wp-admin/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/wp-admin/example/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/tag/wp-admin/"));
}

TEST(UrlMatchListTest, PathFile) {
	TestUrlMatchList urlMatchList("blocklist/path.txt");
	urlMatchList.load();

	//file  wp-login.php
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/blog/wp-login.php"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/wp-login.php"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/awp-login.php"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://www.example.com/wp-login.php5"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://www.example.com/blog/wp-login.php?param=value&param2=value2"));
}

TEST(UrlMatchListTest, PathParam) {
	TestUrlMatchList urlMatchList("blocklist/path.txt");
	urlMatchList.load();

	//param url
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?URL=abc"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?url=abcde"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?uRl=abcde"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?url=http://www.example.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?urlz=http://www.example.com"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/bogus.html?zurl=http://www.example.com"));

	//param action  buy_now
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?action=buy_now&product_id=123"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?ACTION=buy_now&product_id=123"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?product_id=123&action=buy_now"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?product_id=123&ACTION=buy_now"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?action1=buy_now&product_id=123"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?daction=buy_now&product_id=123"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?action=dbuy_now&product_id=123"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/cart.html?action=buy_nowd&product_id=123"));

	//pathparam index   add
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index=add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/INDEX=add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index1=add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/nindex=add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index=addp/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index=dadd/product/1883/form_key/6VKG76zkMo8FmYXE/"));
}

TEST(UrlMatchListTest, PathPathPartial) {
	TestUrlMatchList urlMatchList("blocklist/path.txt");
	urlMatchList.load();

	//pathpartial /wishlist/index/add
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index/add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/INDEX/add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index1/add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/nindex/add/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index/addp/product/1883/form_key/6VKG76zkMo8FmYXE/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("https://www.example.com/wishlist/index/dadd/product/1883/form_key/6VKG76zkMo8FmYXE/"));
}

TEST(UrlMatchListTest, Multi) {
	TestUrlMatchList urlMatchList("blocklist/multi*.txt");
	urlMatchList.load();

	EXPECT_TRUE(urlMatchList.isUrlMatched("http://multi1.example.com/"));
	EXPECT_TRUE(urlMatchList.isUrlMatched("http://multi2.example.com/"));
	EXPECT_FALSE(urlMatchList.isUrlMatched("http://multi3.example.com/"));
}