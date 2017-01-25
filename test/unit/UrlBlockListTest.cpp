#include <gtest/gtest.h>
#include "UrlBlockList.h"

class TestUrlBlockList : public UrlBlockList {
public:
	TestUrlBlockList(const char *filename)
		: UrlBlockList() {
		m_filename = filename;
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
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://sub1.diffdomain.com/"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.onlyroot.com/page.html"));

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.sub2.example.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://sub1.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://example.com/"));
}

TEST(UrlBlockListTest, Path) {
	TestUrlBlockList urlBlockList("blocklist/path.txt");
	urlBlockList.load();

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.example.com/wp-admin/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.example.com/tag/wp-admin/"));
}

TEST(UrlBlockListTest, Real) {
	TestUrlBlockList urlBlockList("blocklist/real.txt");
	urlBlockList.load();

	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://t.co/0TVhTBSaxD"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://at.co/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://ow.ly/2U6rh6"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://ow.ly/i/8kNCn"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://tr.im.cfg1.pl/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://tr.im/CFIA"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.imdb.com/video/imdb/vi706391833/imdb/embed?autoplay=false&width=480"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.tumblr.com/share"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://plus.google.com/share?url=http%3A//on.11alive.com/NEIG1r]"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("https://plus.google.com/+MrStargazerNation/posts/DTDJ55odWuF"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://accounts.google.com/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://web.archive.org/web/*/dr.dk"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://search.twitter.com/search?q=%23trademark"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://twitter.com/share?text=Im%20Sharing%20on%20Twitter&url=http://stackoverflow.com/users/2943186/youssef-subehi&hashtags=stackoverflow,example,youssefusf"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://twitter.com/search?q=China"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://twitter.com/intent/tweet?text=18%20Cocktails%20That%20Are%20Better%20With%20Butter&url=http%3A%2F%2Fwww.eater.com%2Fdrinks%2F2016%2F1%2F14%2F10710202%2Fbutter-cocktails&via=Eater"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://twitter.com/intent/retweet?tweet_id=534860467186171904"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://twitter.com/intent/favorite?tweet_id=595310844746969089"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.facebook.com/sharer/sharer.php?u=http%3A%2F%2Fallthingsd.com%2F20120309%2Fgreen-dot-buys-location-app-loopt-for-43-4m%2F%3Fmod%3Dfb"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("https://www.linkedin.com/shareArticle?"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://www.pinterest.com/pin/create/button/?description=&media=https%3A%2F%2Fcdn1.vox-cdn.com%2Fthumbor%2FrMA6BPH4ZkdBg2RqB9mmZVhYqUs%3D%2F0x77%3A1000x640%2F1050x591%2Fcdn0.vox-cdn.com%2Fuploads%2Fchorus_image%2Fimage%2F48544087%2Fshutterstock_308548907.0.0.jpg&url=http%3A%2F%2Fwww.eater.com%2Fmaps%2Fbest-coffee-taipei"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://pubads.g.doubleclick.net/"));
	EXPECT_TRUE(urlBlockList.isUrlBlocked("http://ads.doubleclick.net/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://doubleclick.net/"));
	EXPECT_FALSE(urlBlockList.isUrlBlocked("http://www.doubleclick.net/"));
}
