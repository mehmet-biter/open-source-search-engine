#include <gtest/gtest.h>

#include "Url.h"
#include "SafeBuf.h"
#include <tuple>

TEST(UrlTest, SetNonAsciiValid) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://topbeskæring.dk/velkommen", "http://xn--topbeskring-g9a.dk/velkommen"),
		std::make_tuple("www.Alliancefrançaise.nu", "http://www.xn--alliancefranaise-npb.nu/"),
		std::make_tuple("française.Alliance.nu", "http://xn--franaise-v0a.alliance.nu/"),
		std::make_tuple("française.Alliance.nu/asdf", "http://xn--franaise-v0a.alliance.nu/asdf"),
		std::make_tuple("http://française.Alliance.nu/asdf", "http://xn--franaise-v0a.alliance.nu/asdf"),
		std::make_tuple("http://française.Alliance.nu/", "http://xn--franaise-v0a.alliance.nu/"),
		std::make_tuple("幸运.龍.com", "http://xn--lwt711i.xn--mi7a.com/"),
		std::make_tuple("幸运.龍.com/asdf/运/abc", "http://xn--lwt711i.xn--mi7a.com/asdf/%E8%BF%90/abc"),
		std::make_tuple("幸运.龍.com/asdf", "http://xn--lwt711i.xn--mi7a.com/asdf"),
		std::make_tuple("http://幸运.龍.com/asdf", "http://xn--lwt711i.xn--mi7a.com/asdf"),
		std::make_tuple("http://Беларуская.org/Акадэмічная",
		                "http://xn--d0a6das0ae0bir7j.org/%D0%90%D0%BA%D0%B0%D0%B4%D1%8D%D0%BC%D1%96%D1%87%D0%BD%D0%B0%D1%8F"),
		std::make_tuple("https://hi.Български.com", "https://hi.xn--d0a6divjd1bi0f.com/"),
		std::make_tuple("https://fakedomain.中文.org/asdf", "https://fakedomain.xn--fiq228c.org/asdf"),
		std::make_tuple("https://gigablast.com/abc/文/efg", "https://gigablast.com/abc/%E6%96%87/efg"),
		std::make_tuple("https://gigablast.com/?q=文", "https://gigablast.com/?q=%E6%96%87"),
		std::make_tuple("http://www.example.сайт", "http://www.example.xn--80aswg/"),
		std::make_tuple("http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqués",
		                "http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqu%C3%A9s"),
		std::make_tuple("http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.com/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.xn--80aswg/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://腕時計通販.jp/", "http://xn--kjvp61d69f6wc3zf.jp/"),
		std::make_tuple("http://сацминэнерго.рф/robots.txt", "http://xn--80agflthakqd0d1e.xn--p1ai/robots.txt"),
		std::make_tuple("http://faß.de/", "http://xn--fa-hia.de/"),
		std::make_tuple("http://βόλος.com/", "http://xn--nxasmm1c.com/"),
		std::make_tuple("http://ශ්‍රී.com/", "http://xn--10cl1a0b660p.com/"),
		std::make_tuple("http://نامه‌ای.com/", "http://xn--mgba3gch31f060k.com/")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		Url url;
		url.set(std::get<0>(*it));

		EXPECT_STREQ(std::get<1>(*it), (const char *)url.getUrl());
	}
}

TEST(UrlTest, SetNonAsciiInvalid) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://www.fas.org/blog/ssp/2009/08/securing-venezuela\032s-arsenals.php",
		                "http://www.fas.org/blog/ssp/2009/08/securing-venezuela%1As-arsenals.php"),
		std::make_tuple("https://pypi.python\n\n\t\t\t\t.org/packages/source/p/pyramid/pyramid-1.5.tar.gz",
		                "https://pypi.python/"),
		std::make_tuple("http://undocs.org/ru/A/C.3/68/\vSR.48", "http://undocs.org/ru/A/C.3/68/%0BSR.48")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		Url url;
		url.set(std::get<0>(*it));

		EXPECT_STREQ(std::get<1>(*it), (const char *)url.getUrl());
	}
}

TEST(UrlTest, GetDisplayUrlFromCharArray) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://xn--topbeskring-g9a.dk/velkommen", "http://topbeskæring.dk/velkommen"),
		std::make_tuple("www.xn--Alliancefranaise-npb.nu", "www.Alliancefrançaise.nu"),
		std::make_tuple("xn--franaise-v0a.Alliance.nu", "française.Alliance.nu"),
		std::make_tuple("xn--franaise-v0a.Alliance.nu/asdf", "française.Alliance.nu/asdf"),
		std::make_tuple("http://xn--franaise-v0a.Alliance.nu/asdf", "http://française.Alliance.nu/asdf"),
		std::make_tuple("http://xn--franaise-v0a.Alliance.nu/", "http://française.Alliance.nu/"),
		std::make_tuple("xn--lwt711i.xn--mi7a.com", "幸运.龍.com"),
		std::make_tuple("xn--lwt711i.xn--mi7a.com/asdf/运/abc", "幸运.龍.com/asdf/运/abc"),
		std::make_tuple("xn--lwt711i.xn--mi7a.com/asdf", "幸运.龍.com/asdf"),
		std::make_tuple("http://xn--lwt711i.xn--mi7a.com/asdf", "http://幸运.龍.com/asdf"),
		std::make_tuple("http://xn--d0a6das0ae0bir7j.org/Акадэмічная", "http://Беларуская.org/Акадэмічная"),
		std::make_tuple("https://hi.xn--d0a6divjd1bi0f.com", "https://hi.Български.com"),
		std::make_tuple("https://fakedomain.xn--fiq228c.org/asdf", "https://fakedomain.中文.org/asdf"),
		std::make_tuple("http://www.example.xn--80aswg", "http://www.example.сайт"),
		std::make_tuple("http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.com/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://www.example.xn--80aswg/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://xn--kjvp61d69f6wc3zf.jp/", "http://腕時計通販.jp/"),
		std::make_tuple("http://xn--80agflthakqd0d1e.xn--p1ai/robots.txt", "http://сацминэнерго.рф/robots.txt"),
		std::make_tuple("http://xn--80agflthakqd0d1e.xn--p1ai", "http://сацминэнерго.рф"),
		std::make_tuple("http://сацминэнерго.рф", "http://сацминэнерго.рф"),
		std::make_tuple("http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai",
		                "http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		StackBuf<> tmpBuf;
		EXPECT_STREQ(std::get<1>(*it), (const char *)Url::getDisplayUrl(std::get<0>(*it), &tmpBuf));
	}
}

TEST(UrlTest, GetDisplayUrlFromUrl) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://topbeskæring.dk/velkommen", "http://topbeskæring.dk/velkommen"),
		std::make_tuple("www.Alliancefrançaise.nu", "http://www.alliancefrançaise.nu/"),
		std::make_tuple("française.Alliance.nu", "http://française.alliance.nu/"),
		std::make_tuple("française.Alliance.nu/asdf", "http://française.alliance.nu/asdf"),
		std::make_tuple("http://française.Alliance.nu/asdf", "http://française.alliance.nu/asdf"),
		std::make_tuple("http://française.Alliance.nu/", "http://française.alliance.nu/"),
		std::make_tuple("幸运.龍.com", "http://幸运.龍.com/"),
		std::make_tuple("幸运.龍.com/asdf/运/abc", "http://幸运.龍.com/asdf/%E8%BF%90/abc"),
		std::make_tuple("幸运.龍.com/asdf", "http://幸运.龍.com/asdf"),
		std::make_tuple("http://幸运.龍.com/asdf", "http://幸运.龍.com/asdf"),
		std::make_tuple("http://Беларуская.org/Акадэмічная",
		                "http://Беларуская.org/%D0%90%D0%BA%D0%B0%D0%B4%D1%8D%D0%BC%D1%96%D1%87%D0%BD%D0%B0%D1%8F"),
		std::make_tuple("https://hi.Български.com", "https://hi.Български.com/"),
		std::make_tuple("https://fakedomain.中文.org/asdf", "https://fakedomain.中文.org/asdf"),
		std::make_tuple("https://gigablast.com/abc/文/efg", "https://gigablast.com/abc/%E6%96%87/efg"),
		std::make_tuple("https://gigablast.com/?q=文", "https://gigablast.com/?q=%E6%96%87"),
		std::make_tuple("http://www.example.сайт", "http://www.example.сайт/"),
		std::make_tuple("http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqués",
		                "http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqu%C3%A9s"),
		std::make_tuple("http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.com/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		                "http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this"),
		std::make_tuple("http://腕時計通販.jp/", "http://腕時計通販.jp/"),
		std::make_tuple("http://сацминэнерго.рф/robots.txt", "http://сацминэнерго.рф/robots.txt"),
		std::make_tuple("http://сацминэнерго.рф", "http://сацминэнерго.рф/"),
		std::make_tuple("http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai",
		                "http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai")
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		Url url;
		url.set(std::get<0>(*it));

		StackBuf<> tmpBuf;
		EXPECT_STREQ(std::get<1>(*it), (const char *)Url::getDisplayUrl(url.getUrl(), &tmpBuf));
	}
}

static void strip_param_tests(const std::vector<std::tuple<const char *, const char *>> &test_cases, int32_t titleDbVersion) {
	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		const char *input_url = std::get<0>(*it);

		Url url;
		url.set(input_url, strlen(input_url), false, true, titleDbVersion);

		EXPECT_STREQ(std::get<1>(*it), (const char *)url.getUrl());
	}
}

// make sure we don't break backward compatibility
TEST(UrlTest, StripParamsV122) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://retailer.esignserver2.com/holzboden-direkt/gallery.do;jsessionid=D6C14EE54E6AF0B89885D129D817A505",
		                "http://retailer.esignserver2.com/holzboden-direkt/gallery.do"),
		std::make_tuple("https://scholarships.wisc.edu/Scholarships/recipientDetails;jsessionid=D2DCE4F10608F15CA177E29EB2AB162F?recipId=850",
		                "https://scholarships.wisc.edu/Scholarships/recipientDetails?recipId=850"),
		std::make_tuple("http://staging.ilo.org/gimi/gess/ShowProject.do;jsessionid=759cb78d694bd5a5dd5551c6eb36a1fb66b98f4e786d5ae3c73cee161067be75.e3aTbhuLbNmSe34MchaRahaRaNb0?id=1625",
		                "http://staging.ilo.org/gimi/gess/ShowProject.do?id=1625"),
		std::make_tuple("http://ualberta.intelliresponse.com/index.jsp?requestType=NormalRequest&source=3&id=474&sessionId=f5b80817-fa7e-11e5-9343-5f3e78a954d2&question=How+many+students+are+enrolled",
		                "http://ualberta.intelliresponse.com/index.jsp?requestType=NormalRequest&source=3&id=474&question=How+many+students+are+enrolled"),
		std::make_tuple("http://www.eyecinema.ie/cinemas/film_info_detail.asp?SessionID=78C5F9DFF1B9441EB5ED527AB61BAB5B&cn=1&ci=2&ln=1&fi=7675",
		                "http://www.eyecinema.ie/cinemas/film_info_detail.asp?cn=1&ci=2&ln=1&fi=7675"),
		std::make_tuple("https://jobs.bathspa.ac.uk/wrl/pages/vacancy.jsf;jsessionid=C4882E8D70D04244661C8A8E811D3290?latest=01001967",
		                "https://jobs.bathspa.ac.uk/wrl/pages/vacancy.jsf?latest=01001967"),
		std::make_tuple("https://sa.www4.irs.gov/wmar/start.do;jsessionid=DQnV2P-nFQir0foo7ThxBejZ",
		                "https://sa.www4.irs.gov/wmar/start.do"),
		std::make_tuple("http://www.oracle.com/technetwork/developer-tools/adf/overview/index.html;jsessionid=6R39V8WhqTQ7HMb2vTQTkzbP5XRFgs4RQzyxQ7fqxH9y6p6vKXk4!-460884186",
		                "http://www.oracle.com/technetwork/developer-tools/adf/overview/index.html"),
		std::make_tuple("https://webshop.lic.co.nz/cws001/catalog/productDetail.jsf;jsessionid=0_cVS0dqWe1zHDcxyveGcysJVfbkUwHPDMUe_SAPzI8IIDaGbNUXn59V-PZnbFVZ;saplb_*=%28J2EE516230320%29516230351?sort=TA&wec-appid=WS001&page=B43917ED9DD446288421D9F817EE305E&itemKey=463659D75F2F005D000000000A0A0AF0&show=12&view=grid&wec-locale=en_NZ",
		                "https://webshop.lic.co.nz/cws001/catalog/productDetail.jsfsaplb_*=%28J2EE516230320%29516230351?sort=TA&wec-appid=WS001&page=B43917ED9DD446288421D9F817EE305E&itemKey=463659D75F2F005D000000000A0A0AF0&show=12&view=grid&wec-locale=en_NZ"),
		std::make_tuple("http://www.vineyard2door.com/web/clubs_browse.cfm?CFID=3843950&CFTOKEN=cfd5b9e083fb3e24-03C2F487-DAB8-1365-521658E43AB8A0DC&jsessionid=22D5211D9EB291522DE9A4258ECB94D2.cfusion",
		                "http://www.vineyard2door.com/web/clubs_browse.cfm?CFID=3843950&CFTOKEN=cfd5b9e083fb3e24-03C2F487-DAB8-1365-521658E43AB8A0DC"),
		std::make_tuple("http://tbinternet.ohchr.org/_layouts/treatybodyexternal/SessionDetails1.aspx?SessionID=1016&Lang=en",
		                "http://tbinternet.ohchr.org/_layouts/treatybodyexternal/SessionDetails1.aspx?SessionID=1016&Lang=en"),
		std::make_tuple("https://collab365.conferencehosts.com/SitePages/sessionDetails.aspx?sessionid=C365117",
		                "https://collab365.conferencehosts.com/SitePages/sessionDetails.aspx"),
		std::make_tuple("http://www.urchin.com/download.html?utm_source=newsletter4&utm_medium=email&utm_term=urchin&utm_content=easter&utm_campaign=product",
		                "http://www.urchin.com/download.html?utm_source=newsletter4&utm_medium=email&utm_content=easter&utm_campaign=product"),
		std::make_tuple("http://www.huffingtonpost.com/parker-marie-molloy/todd-kincannon-transgender-camps_b_4100777.html?utm_source=feedburner&utm_medium=feed&utm_campaign=Feed%3A+HP%2FPolitics+%28Politics+on+The+Huffington+Post",
		                "http://www.huffingtonpost.com/parker-marie-molloy/todd-kincannon-transgender-camps_b_4100777.html?utm_medium=feed&utm_campaign=Feed%3A+HP%2FPolitics+%28Politics+on+The+Huffington+Post"),
		std::make_tuple("http://www.staffnet.manchester.ac.uk/news/display/?id=10341&;utm_source=newsletter&utm_medium=email&utm_campaign=eUpdate",
		                "http://www.staffnet.manchester.ac.uk/news/display/?id=10341&utm_medium=email&utm_campaign=eUpdate"),
		std::make_tuple("http://www.nightdivestudios.com/games/turok-dinosaur-hunter/?utm_source=steampowered.com&utm_medium=product&utm_campaign=website%20-%20turok%20dinosaur%20hunter",
		                "http://www.nightdivestudios.com/games/turok-dinosaur-hunter/?utm_medium=product&utm_campaign=website%20-%20turok%20dinosaur%20hunter"),
		std::make_tuple("http://www.mihomes.com/Find-Your-New-Home/Virginia-Homes?utm_source=NewHomesDirectory.com&utm_campaign=referral-division&utm_medium=feed&utm_content=&utm_term=consumer&cookiecheck=true",
		                "http://www.mihomes.com/Find-Your-New-Home/Virginia-Homes?utm_source=NewHomesDirectory.com&utm_campaign=referral-division&utm_medium=feed&utm_content=&cookiecheck=true"),
		std::make_tuple("http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&utm_hp_ref=healthy-living&utm_hp_ref=au-life&adsSiteOverride=au",
		                "http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&utm_hp_ref=au-life&adsSiteOverride=au"),
		std::make_tuple("http://maersklinereefer.com/about/merry-christmas/?elqTrackId=786C9D2AE676DEC435B578D75CB0B4FD&elqaid=2666&elqat=2",
		                "http://maersklinereefer.com/about/merry-christmas/?elqTrackId=786C9D2AE676DEC435B578D75CB0B4FD&elqaid=2666&elqat=2"),
		std::make_tuple("https://community.oracle.com/community/topliners/?elq_mid=21557&elq_cid=1618237&elq=3c0cfe27635443ca9b6410238cc876a9&elqCampaignId=2182&elqaid=21557&elqat=1&elqTrackId=40772b5725924f53bc2c6a6fb04759a3",
		                "https://community.oracle.com/community/topliners/?elq_mid=21557&elq_cid=1618237&elqCampaignId=2182&elqaid=21557&elqat=1&elqTrackId=40772b5725924f53bc2c6a6fb04759a3"),
		std::make_tuple("http://app.reg.techweb.com/e/er?s=2150&lid=25554&elq=00000000000000000000000000000000&elqaid=2294&elqat=2&elqTrackId=3de2badc5d7c4a748bc30253468225fd",
		                "http://app.reg.techweb.com/e/er?s=2150&lid=25554&elqaid=2294&elqat=2&elqTrackId=3de2badc5d7c4a748bc30253468225fd"),
		std::make_tuple("http://www.biography.com/people/louis-armstrong-9188912?elq=7fd0dd577ebf4eafa1e73431feee849f&elqCampaignId=2887",
		                "http://www.biography.com/people/louis-armstrong-9188912?elqCampaignId=2887"),
		std::make_tuple("http://www.thermoscientific.com/en/product/haake-mars-rotational-rheometers.html?elq_mid=1089&elq_cid=107687&wt.mc_id=CAD_MOL_MC_PR_EM1_0815_NewHaakeMars_English_GLB_2647&elq=f17d2c276ed045c0bb391e4c273b789c&elqCampaignId=&elqaid=1089&elqat=1&elqTrackId=ce2a4c4879ee4f6488a14d924fa1f8a5",
		                "http://www.thermoscientific.com/en/product/haake-mars-rotational-rheometers.html?elq_mid=1089&elq_cid=107687&wt.mc_id=CAD_MOL_MC_PR_EM1_0815_NewHaakeMars_English_GLB_2647&elqCampaignId=&elqaid=1089&elqat=1&elqTrackId=ce2a4c4879ee4f6488a14d924fa1f8a5"),
		std::make_tuple("https://astro-report.com/lp2.html?pk_campaign=1%20Natal%20Chart%20-%20RDMs&pk_kwd=astrological%20chart%20free&gclid=CPfkwKfP2LgCFcJc3godgSMAHA",
		                "https://astro-report.com/lp2.html?pk_campaign=1%20Natal%20Chart%20-%20RDMs&gclid=CPfkwKfP2LgCFcJc3godgSMAHA"),
		std::make_tuple("http://lapprussia.lappgroup.com/kontakty.html?pk_campaign=yadirect-crossselling&pk_kwd=olflex&pk_source=yadirect&pk_medium=cpc&pk_content=olflex&rel=bytib",
		                "http://lapprussia.lappgroup.com/kontakty.html?pk_campaign=yadirect-crossselling&pk_source=yadirect&pk_medium=cpc&pk_content=olflex&rel=bytib"),
		std::make_tuple("http://scriptfest.com/session/million-dollar-screenwriting/",
		                "http://scriptfest.com/session/million-dollar-screenwriting/")
	};

	strip_param_tests(test_cases, 122);
}

TEST(UrlTest, StripParamsOsCommerce) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// osCAdminID
		std::make_tuple("http://www.nailcosmetics.pl/?osCAdminID=70b4c843a51204ec897136bc04282462&osCAdminID=70b4c843a51204ec897136bc04282462&osCAdminID=70b4c843a51204ec897136bc04282462&osCAdminID=70b4c843a51204ec897136bc04282462",
		                "http://www.nailcosmetics.pl/"),
		std::make_tuple("http://ezofit.sk/obchod/admin/categories.php?cPath=205&action=new_product&osCAdminID=dogjdaa5ogukr5vdtnld0o80r4",
		                "http://ezofit.sk/obchod/admin/categories.php?cPath=205&action=new_product"),
		std::make_tuple("http://calisonusa.com/specials.html?osCAdminID=a401c1738f8e361728c7f61e9dd23a31",
		                "http://calisonusa.com/specials.html"),
		std::make_tuple("https://springbankcheese.ca/catalog/advanced_search_result.php/search_in_description/1/keywords/chardonnay/osCAdminID/45de8edd68f8bc05e9fde0d2c528a619/sort/3d/page/2",
		                "https://springbankcheese.ca/catalog/advanced_search_result.php/search_in_description/1/keywords/chardonnay/sort/3d/page/2"),

		// osCAdminID (no strip)
		std::make_tuple("https://springbankcheese.ca/catalog/advanced_search_result.php/search_in_description/1/keywords/chardonnay/osCAdminID/sort/3d/page/2",
		                "https://springbankcheese.ca/catalog/advanced_search_result.php/search_in_description/1/keywords/chardonnay/osCAdminID/sort/3d/page/2"),

		// osCsid
		std::make_tuple("http://www.silversites.net/sweetheart-tree.php?osCsid=4c7154c9159ec1aadfc788a3525e61dd",
		                "http://www.silversites.net/sweetheart-tree.php"),
		std::make_tuple("https://www.decent-cigar.com/collectibles.php/osCsid/847ve0olpeu5bs5ujkt9ulrgn0",
		                "https://www.decent-cigar.com/collectibles.php"),
		std::make_tuple("http://www.plat.co.jp/shop/catalog/default/language/en/cPath/22/osCsid/79bdb5fa7557ca04fb46ef1bd706139f/river-lake-fishing-freshwater/",
		                "http://www.plat.co.jp/shop/catalog/default/language/en/cPath/22/river-lake-fishing-freshwater/"),
		std::make_tuple("https://www.12stepcds.com/catalog/product_info/products_id/577/osCsid/",
		                "https://www.12stepcds.com/catalog/product_info/products_id/577/"),

		// osCsid (no strip)
		std::make_tuple("http://www.steviaforyou.com/information.php/info_id/33/stevia-producten/osCsid/language/nl/osCsid/546bb2d065677b8e53747e81309b2660",
		                "http://www.steviaforyou.com/information.php/info_id/33/stevia-producten/osCsid/language/nl")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsXTCommerce) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// XT-commerce
		std::make_tuple("http://www.unitedloneliness.com/index.php/XTCsid/d929e97581813396ed8f360e7f186eab",
		                "http://www.unitedloneliness.com/index.php"),
		std::make_tuple("http://www.extrovert.de/Maitre?XTCsid=fgkp6js6p23gcfhl7u4g223no6",
		                "http://www.extrovert.de/Maitre"),
		std::make_tuple("https://bravisshop.eu/index.php/cPath/1/category/Professional---Hardware/XTCsid/",
		                "https://bravisshop.eu/index.php/cPath/1/category/Professional---Hardware/"),

		// XT-commerce (no strip)
		std::make_tuple("http://www.rfid-webshop.com/product_info.php/products_id/945/XTCsid/templates",
		                "http://www.rfid-webshop.com/product_info.php/products_id/945/XTCsid/templates")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsColdFusion) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// coldfusion
		std::make_tuple("http://www.vineyard2door.com/web/clubs_browse.cfm?CFID=3843950&CFTOKEN=cfd5b9e083fb3e24-03C2F487-DAB8-1365-521658E43AB8A0DC&jsessionid=22D5211D9EB291522DE9A4258ECB94D2.cfusion",
		                "http://www.vineyard2door.com/web/clubs_browse.cfm"),
		std::make_tuple("http://www.liquidhighwaycarwash.com/category/1118&CFID=11366594&CFTOKEN=9178789d30437e83-FD850740-F9A2-39F0-AA850FED06D46D4B/employment.htm",
		                "http://www.liquidhighwaycarwash.com/category/1118/employment.htm"),
		std::make_tuple("http://shop.arslonga.ch/index.cfm/shop/homestyle/site/article/id/16834/CFID/3458787/CFTOKEN/e718cd6cc29050df-8051DC1E-C29B-554E-6DFF6B5D2704A9A5",
		                "http://shop.arslonga.ch/index.cfm/shop/homestyle/site/article/id/16834"),
		std::make_tuple("http://www.lifeguide-augsburg.de/index.cfm/fuseaction/themen/theID/7624/ml1/7624/zg/0/cfid/43537465/cftoken/92566684.html",
		                "http://www.lifeguide-augsburg.de/index.cfm/fuseaction/themen/theID/7624/ml1/7624/zg/0"),
		std::make_tuple("https://www.mutualscrew.com/cart/cart.cfm?cftokenPass=CFID%3D31481352%26CFTOKEN%3D6aac7a0fc9fa6be0%2DBF3514D1%2D155D%2D8226%2D0EF8291F836B567D%26jsessionid%3D175051907615629E4C2CB4BFC8297FF3%2Ecfusion",
		                "https://www.mutualscrew.com/cart/cart.cfm")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSAPLoadBalancer) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://www.impo.ch/home/saplb_*=(J2EE5238120)5238151?layout=7.01-15_176_207_166_177&urtype=cat&xcm=impo_prod&carea=545B63807E3F5230E10080000A050267&citem=545B63807E3F5230E10080000A0502674E426F193F044BCFE10000000A0311BE&key=0/0",
		                "http://www.impo.ch/home?layout=7.01-15_176_207_166_177&urtype=cat&xcm=impo_prod&carea=545B63807E3F5230E10080000A050267&citem=545B63807E3F5230E10080000A0502674E426F193F044BCFE10000000A0311BE&key=0/0"),
		std::make_tuple("http://www.montblanc.com/checkout/b2c/init.do;jsessionid=(J2EE3264100)ID0269691750DB2fe63b612b56acbe8f026bdf4191bcc4fe9e5c8eEnd;saplb_*=(J2EE3264100)3264150",
		                "http://www.montblanc.com/checkout/b2c/init.do")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsAtlassian) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// atlassian
		std::make_tuple("https://track.systrends.com/secure/IssueNavigator.jspa?mode=show&atl_token=CUqRyjtmwj",
		                "https://track.systrends.com/secure/IssueNavigator.jspa?mode=show"),
		std::make_tuple("https://jira.kansalliskirjasto.fi/secure/WorkflowUIDispatcher.jspa?id=76139&action=51&atl_token=B12X-5XYK-TDON-8SC7|9724becbc02f07cdd6217c60b7662fe0b6c6f6d2|lout",
		                "https://jira.kansalliskirjasto.fi/secure/WorkflowUIDispatcher.jspa?id=76139&action=51"),
		std::make_tuple("https://support.highwinds.com/login.action?os_destination=%2Fdisplay%2FDOCS%2FUser%2BAPI&atl_token=56c1bb338d5ad3ac262dd4e97bda482efc151f30",
		                "https://support.highwinds.com/login.action?os_destination=%2Fdisplay%2FDOCS%2FUser%2BAPI"),
		std::make_tuple("https://bugs.dlib.indiana.edu/secure/IssueNavigator.jspa?mode=hide&atl_token=AT3D-YZ9T-9TL1-ICW1%7C06900f3197f333cf03f196af7a36c63767c4e8fb%7Clout&requestId=10606",
		                "https://bugs.dlib.indiana.edu/secure/IssueNavigator.jspa?mode=hide&requestId=10606")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripSessionIdPSession) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// psession
		std::make_tuple("http://ebretsteiner.at/kinderwagen/kinderwagen-Kat97.html?pSession=7d01p6qvcl2e72j8ivmppk12k0",
		                "http://ebretsteiner.at/kinderwagen/kinderwagen-Kat97.html"),
		std::make_tuple("http://kontorlokaler.dk/show_unique/index.asp?Psession=491022863920110420135759",
		                "http://kontorlokaler.dk/show_unique/index.asp"),
		std::make_tuple("http://freespass.de/homepage.php?pSession=XUjuplcPFGlJD2ZF5O26ApqAj5ZNEZwZrUKX5kkA&id=716",
		                "http://freespass.de/homepage.php?id=716")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsGalileoSession) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// galileosession
		std::make_tuple("http://www.tutego.de/blog/javainsel/page/38/?GalileoSession=39387871A4pi84-MI8M",
		                "http://www.tutego.de/blog/javainsel/page/38/"),
		std::make_tuple("https://shop.vierfarben.de/hilfe/Vierfarben/agb?GalileoSession=54933578A7-0S-.kn-A",
		                "https://shop.vierfarben.de/hilfe/Vierfarben/agb"),
		std::make_tuple("https://www.rheinwerk-verlag.de/?titelID=560&GalileoSession=47944076A4-xkQI91C8",
		                "https://www.rheinwerk-verlag.de/?titelID=560")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsPostNuke) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// postnuke
		std::make_tuple("http://eeagrants.org/News?POSTNUKESID=c9965f0db1606c402015743d1cda55f5",
		                "http://eeagrants.org/News"),
		std::make_tuple("http://www.bkamager.dk/modules.php?op=modload&name=News&file=article&sid=166&mode=thread&order=0&thold=0&POSTNUKESID=78ac739940c636f94bf9b3fac3afb4d2",
		                "http://www.bkamager.dk/modules.php?op=modload&name=News&file=article&sid=166&mode=thread&order=0&thold=0"),
		std::make_tuple("http://zspieszyce.nazwa.pl/modules.php?set_albumName=pieszyce-schortens&op=modload&name=gallery&file=index&include=view_album.php&POSTNUKESID=549178d5035b622229a39cd5baf75d2a",
		                "http://zspieszyce.nazwa.pl/modules.php?set_albumName=pieszyce-schortens&op=modload&name=gallery&file=index&include=view_album.php"),
		std::make_tuple("http://myrealms.net/PostNuke/html/print.php?sid=2762&POSTNUKESID=4ed3b0a832d4687020b05ce70",
		                "http://myrealms.net/PostNuke/html/print.php?sid=2762")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsJSessionId) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// jessionid
		std::make_tuple("https://scholarships.wisc.edu/Scholarships/recipientDetails;jsessionid=D2DCE4F10608F15CA177E29EB2AB162F?recipId=850",
		                "https://scholarships.wisc.edu/Scholarships/recipientDetails?recipId=850"),
		std::make_tuple("http://staging.ilo.org/gimi/gess/ShowProject.do;jsessionid=759cb78d694bd5a5dd5551c6eb36a1fb66b98f4e786d5ae3c73cee161067be75.e3aTbhuLbNmSe34MchaRahaRaNb0?id=1625",
		                "http://staging.ilo.org/gimi/gess/ShowProject.do?id=1625"),
		std::make_tuple("https://jobs.bathspa.ac.uk/wrl/pages/vacancy.jsf;jsessionid=C4882E8D70D04244661C8A8E811D3290?latest=01001967",
		                "https://jobs.bathspa.ac.uk/wrl/pages/vacancy.jsf?latest=01001967"),
		std::make_tuple("https://sa.www4.irs.gov/wmar/start.do;jsessionid=DQnV2P-nFQir0foo7ThxBejZ",
		                "https://sa.www4.irs.gov/wmar/start.do"),
		std::make_tuple("https://webshop.lic.co.nz/cws001/catalog/productDetail.jsf;jsessionid=0_cVS0dqWe1zHDcxyveGcysJVfbkUwHPDMUe_SAPzI8IIDaGbNUXn59V-PZnbFVZ;saplb_*=%28J2EE516230320%29516230351?sort=TA&wec-appid=WS001&page=B43917ED9DD446288421D9F817EE305E&itemKey=463659D75F2F005D000000000A0A0AF0&show=12&view=grid&wec-locale=en_NZ",
		                "https://webshop.lic.co.nz/cws001/catalog/productDetail.jsf?sort=TA&wec-appid=WS001&page=B43917ED9DD446288421D9F817EE305E&itemKey=463659D75F2F005D000000000A0A0AF0&show=12&view=grid&wec-locale=en_NZ"),
		std::make_tuple("http://www.oracle.com/technetwork/developer-tools/adf/overview/index.html;jsessionid=6R39V8WhqTQ7HMb2vTQTkzbP5XRFgs4RQzyxQ7fqxH9y6p6vKXk4!-460884186",
		                "http://www.oracle.com/technetwork/developer-tools/adf/overview/index.html"),
		std::make_tuple("http://www.cnpas.org/portal/media-type/html/language/ro/user/anon/page/default.psml/jsessionid/A27DF3C8CF0C66C480EC74FF6A7C837C?action=forum.ScreenAction",
		                "http://www.cnpas.org/portal/media-type/html/language/ro/user/anon/page/default.psml?action=forum.ScreenAction"),
		std::make_tuple("http://www.medienservice-online.de/dyn/epctrl/jsessionid/FA082288A00623E49FCC553D95D484C9/mod/wwpress002431/cat/wwpress005964/pri/wwpress",
		                "http://www.medienservice-online.de/dyn/epctrl/mod/wwpress002431/cat/wwpress005964/pri/wwpress")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsPhpSessId) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// phpsessid
		std::make_tuple("http://www.emeraldgrouppublishing.com/authors/guides/index.htm?PHPSESSID=gan1vvu81as0nnkc08fg38c3i2",
		                "http://www.emeraldgrouppublishing.com/authors/guides/index.htm"),
		std::make_tuple("http://www.buf.fr/philosophy/?PHPSESSID=29ba61ff4f47064d4062e261eeab5d85",
		                "http://www.buf.fr/philosophy/"),
		std::make_tuple("http://www.toz-penkala.hr/proizvodi-skolski-pribor?phpsessid=v5bhoda67mhutnqv382q86l4l4",
		                "http://www.toz-penkala.hr/proizvodi-skolski-pribor"),
		std::make_tuple("http://web.burza.hr/?PHPSESSID",
		                "http://web.burza.hr/"),
		std::make_tuple("http://www.dursthoff.de/book.php?PHPSESSID=068bd453c94c3c4c0b7ccca9a581597d&m=3&aid=28&bid=50",
		                "http://www.dursthoff.de/book.php?m=3&aid=28&bid=50"),
		std::make_tuple("http://www.sapro.cz/ftp/index.php?directory=HD-BOX&PHPSESSID=",
		                "http://www.sapro.cz/ftp/index.php?directory=HD-BOX"),
		std::make_tuple("http://forum.keepemcookin.com/index.php?PHPSESSID=eoturno2s9rsrs6ru3k8j362l5&amp;action=profile;u=58995",
		                "http://forum.keepemcookin.com/index.php?action=profile;u=58995"),
		std::make_tuple("http://www.praxis-jennewein.de/?&sitemap&PHPSESSID_netsh107345=84b58a8e5c56d8d1f9d459311caf18ee",
		                "http://www.praxis-jennewein.de/?sitemap"),

		// phpsessid (no strip)
		std::make_tuple("http://korel.com.au/disable-phpsessid/feed/",
		                "http://korel.com.au/disable-phpsessid/feed/")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsAuthSess) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// auth_sess
		std::make_tuple("http://www.jobxoom.com/location.php?province=Iowa&lid=738&auth_sess=kgq6kd4bl9ma1rap6pbks1c8b2",
		                "http://www.jobxoom.com/location.php?province=Iowa&lid=738"),
		std::make_tuple("http://www.gojobcenter.com/view.php?job_id=61498&auth_sess=cb5f29174d9f5e9fbb4d7ec41cd69112&ref=bc87a09ce74326f40200e7abb",
		                "http://www.gojobcenter.com/view.php?job_id=61498&ref=bc87a09ce74326f40200e7abb")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsPsSessId) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// ps_sess_id
		std::make_tuple("http://cs.kafemlynek.cz/psychostats/player.php?id=312&ps_sess_id=d6a03bb621999e995d09244d2b15b2e5",
		                "http://cs.kafemlynek.cz/psychostats/player.php?id=312"),
		std::make_tuple("http://ph.csh.lv/dd2/awards.php?ps_sess_id=ec7579208365e58c45af9505a7b6c4ec",
		                "http://ph.csh.lv/dd2/awards.php")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsMySid) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// mysid
		std::make_tuple("https://www.worldvision.de/_downloads/allgemein/Haiti_3years_Earthquake%20Response%20Report.pdf?mysid=glwcjvci",
		                "https://www.worldvision.de/_downloads/allgemein/Haiti_3years_Earthquake%20Response%20Report.pdf"),
		std::make_tuple("http://www.nobilia.de/file.php?mySID=80fa669565e6c41006e82e7b87b4d6c4&file=/download/Pressearchiv/Tete-A-Tete-KR%20SO14_nobilia.pdf&type=down&usg=AFQjCNELf61sLLvtPUnOJA9IwI87-ngOvQ",
		                "http://www.nobilia.de/file.php?file=/download/Pressearchiv/Tete-A-Tete-KR%20SO14_nobilia.pdf&type=down&usg=AFQjCNELf61sLLvtPUnOJA9IwI87-ngOvQ"),

		// mysid (no strip)
		std::make_tuple("http://old.evangelskivestnik.net/statia.php?mysid=773",
		                "http://old.evangelskivestnik.net/statia.php?mysid=773"),
		std::make_tuple("http://www.ajusd.org/m/documents.cfm?getfiles=5170|0&mysid=1054",
		                "http://www.ajusd.org/m/documents.cfm?getfiles=5170|0&mysid=1054")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSid) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// sid
		std::make_tuple("http://astraklubpolska.pl/viewtopic.php?f=149&t=829138&sid=1d5e1e9ba356dc2f848f6223d914ca19&start=10",
		                "http://astraklubpolska.pl/viewtopic.php?f=149&t=829138&start=10"),
		std::make_tuple("http://jx3.net/tdg/forum/viewtopic.php?f=74&t=2860&sid=0b721aa1c34b75fcf41e17304537d965&start=0",
		                "http://jx3.net/tdg/forum/viewtopic.php?f=74&t=2860&start=0"),
		std::make_tuple("http://www.classicbabygift.com/cgi-bin/webc.cgi/st_main.html?p_catid=3&sid=3KnGJS3ga7ae891-33115175851.04",
		                "http://www.classicbabygift.com/cgi-bin/webc.cgi/st_main.html?p_catid=3"),
		std::make_tuple("http://order.bephoto.be/?langue=nl&step=2&sid=v0uqho4nv0mnghv4ap3ieeqp94&check=AzEBNVg6BGQBagM",
		                "http://order.bephoto.be/?langue=nl&step=2&check=AzEBNVg6BGQBagM"),
		std::make_tuple("http://www02.ktzhk.com/plugin_midi.php?action=list&midi_type=2&tids=49&sid=K6FYyt",
		                "http://www02.ktzhk.com/plugin_midi.php?action=list&midi_type=2&tids=49"),

		// sid (no strip)
		std::make_tuple("http://www.fibsry.fi/fi/component/sobipro/?pid=146&sid=203:Bank4Hope",
		                "http://www.fibsry.fi/fi/component/sobipro/?pid=146&sid=203:Bank4Hope"),
		std::make_tuple("http://www.bzga.de/?sid=1366",
		                "http://www.bzga.de/?sid=1366"),
		std::make_tuple("http://tw.school.uschoolnet.com/?id=es00000113&mode=news&key=134726811289359&sid=detail&news=141981074080101",
		                "http://tw.school.uschoolnet.com/?id=es00000113&mode=news&key=134726811289359&sid=detail&news=141981074080101")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSes) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// ses
		std::make_tuple("http://hotelres.boulevards.com/hotel/10007179-10192233O.html?ses=95ee88728aa10bc1da9d0120e94373fcht&unps=y",
		                "http://hotelres.boulevards.com/hotel/10007179-10192233O.html?unps=y"),
		std::make_tuple("http://www.statel.lt/index.php?id=karjera&ses=9bi7f6mih4hd2pdgkqjc88rib7",
		                "http://www.statel.lt/index.php?id=karjera"),

		// ses (no strip)
		std::make_tuple("http://www.ccft.ro:8080/gal/index.php?loc=125&ses=128",
		                "http://www.ccft.ro:8080/gal/index.php?loc=125&ses=128")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsS) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// s
		std::make_tuple("http://forum.tuningracers.de/index.php?page=Thread&threadID=5995&s=1951704681f7fd088ef0489a29c5753ea333208b",
		                "http://forum.tuningracers.de/index.php?page=Thread&threadID=5995"),
		std::make_tuple("https://www.futanaripalace.com/member.php?63612-SizeLover&s=6c526e0872ce06f974f456a897ef2b1c",
		                "https://www.futanaripalace.com/member.php?63612-SizeLover"),

		// s (no strip)
		std::make_tuple("http://www.medpharmjobs.pl/job-offers?p=88&s=976&c=8",
		                "http://www.medpharmjobs.pl/job-offers?p=88&s=976&c=8")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSessionId) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// session_id
		std::make_tuple("https://www.insideultrasound.com/mm5/merchant.mvc?Session_ID=1f59af8e2ba36c1239ce5c897e1a90a3&Screen=PROD&Product_Code=IU400CME",
		                "https://www.insideultrasound.com/mm5/merchant.mvc?Screen=PROD&Product_Code=IU400CME"),
		std::make_tuple("http://www.sylt-ferienwohnungen-urlaub.de/objekt_buchungsanfrage.php?session_id=5mt70lh8h19ci2i77h9p4e7gv5&objekt_id=644",
		                "http://www.sylt-ferienwohnungen-urlaub.de/objekt_buchungsanfrage.php?objekt_id=644"),
		std::make_tuple("http://www.zdravotnicke-potreby.cz/main/kosik_insert.php?id_produktu=26418&session_id=gkv1ufp8spdj670c6m66qn4184&ip=",
		                "http://www.zdravotnicke-potreby.cz/main/kosik_insert.php?id_produktu=26418&ip="),

		// session_id (no strip)
		std::make_tuple("https://rms.miamidade.gov/Saturn/Activities/Details.aspx?session_id=53813&back_url=fi9BY3Rpdml0aWVzL1NlYXJjaC5hc3B4",
		                "https://rms.miamidade.gov/Saturn/Activities/Details.aspx?session_id=53813&back_url=fi9BY3Rpdml0aWVzL1NlYXJjaC5hc3B4"),
		std::make_tuple("http://www.pbd-india.com/media-photo-gallery-event.asp?session_id=4",
		                "http://www.pbd-india.com/media-photo-gallery-event.asp?session_id=4"),

		// sessionid
		std::make_tuple("http://ualberta.intelliresponse.com/index.jsp?requestType=NormalRequest&source=3&id=474&sessionId=f5b80817-fa7e-11e5-9343-5f3e78a954d2&question=How+many+students+are+enrolled",
		                "http://ualberta.intelliresponse.com/index.jsp?requestType=NormalRequest&source=3&id=474&question=How+many+students+are+enrolled"),
		std::make_tuple("http://www.eyecinema.ie/cinemas/film_info_detail.asp?SessionID=78C5F9DFF1B9441EB5ED527AB61BAB5B&cn=1&ci=2&ln=1&fi=7675",
		                "http://www.eyecinema.ie/cinemas/film_info_detail.asp?cn=1&ci=2&ln=1&fi=7675"),

		// sessionid (no strip)
		std::make_tuple("http://tbinternet.ohchr.org/_layouts/treatybodyexternal/SessionDetails1.aspx?SessionID=1016&Lang=en",
		                "http://tbinternet.ohchr.org/_layouts/treatybodyexternal/SessionDetails1.aspx?SessionID=1016&Lang=en"),
		std::make_tuple("https://collab365.conferencehosts.com/SitePages/sessionDetails.aspx?sessionid=C365117",
		                "https://collab365.conferencehosts.com/SitePages/sessionDetails.aspx?sessionid=C365117")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSessId) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// vbsessid
		std::make_tuple("https://www.westfalia.de/static/servicebereich/service/serviceangebote/impressum.html?&vbSESSID=50d96959db895a0adbfebd325a4a65e0",
		                "https://www.westfalia.de/static/servicebereich/service/serviceangebote/impressum.html"),
		std::make_tuple("https://www.westfalia.de/static/servicebereich/service/aktionen/banner.html?vbSESSID=f4db3ec33001c9759d095c6432651e39&cHash=5babb7ddd11f5164a9fccc7cbbf42aad",
		                "https://www.westfalia.de/static/servicebereich/service/aktionen/banner.html?cHash=5babb7ddd11f5164a9fccc7cbbf42aad"),

		// asesessid
		std::make_tuple("http://www.aseforums.com/viewtopic.php?topicid=70&asesessid=07d0b0d2dc4162ac01afe5b784940274",
		                "http://www.aseforums.com/viewtopic.php?topicid=70"),
		std::make_tuple("http://hardwarelogic.com/articles.php?id=6441&asesessid=e4c6ee21fc4f3fc6f93a022647bb290b474f1c84",
		                "http://hardwarelogic.com/articles.php?id=6441"),

		// nlsessid
		std::make_tuple("https://www.videobuster.de/?NLSESSID=67ccc49cb2490dcb5f6d53878facf1a8",
		                "https://www.videobuster.de/"),

		// sessid (no strip)
		std::make_tuple("http://foto.ametikool.ee/index.php/oppeprotsessid/Ettev-tluserialade-reis-Saaremaa-ja-Muhu-ettev-tetesse/IMG_2216",
		                "http://foto.ametikool.ee/index.php/oppeprotsessid/Ettev-tluserialade-reis-Saaremaa-ja-Muhu-ettev-tetesse/IMG_2216"),
		std::make_tuple("http://korel.com.au/disable-phpsessid/feed/",
		                "http://korel.com.au/disable-phpsessid/feed/")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSession) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// session
		std::make_tuple("http://forum.fps-power.eu/switch-vermietung/?SESSION=0vvsbvno0mqlk0nfa512tvdo95",
		                "http://forum.fps-power.eu/switch-vermietung/"),
		std::make_tuple("http://www.wfg-guben.de/index.php?c_lang=2&session=43665c1a0295d79062a7854b2fec10a4",
		                "http://www.wfg-guben.de/index.php?c_lang=2"),

		// session (no strip)
		std::make_tuple("http://mokuhankan.com/parties/reservations.php?session=590&date=2015-04-01&time=10:00",
		                "http://mokuhankan.com/parties/reservations.php?session=590&date=2015-04-01&time=10:00")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsSess) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// sess
		std::make_tuple("http://yubirea.com/help.php?SESS=f7abccc07285fdaad29e92a7236201e5",
		                "http://yubirea.com/help.php"),
		std::make_tuple("http://suka2gue.com/index.php?SESS=9006b4724952d3b2ff25a3dd95306d02",
		                "http://suka2gue.com/index.php"),

		// sess (no strip)
		std::make_tuple("http://gac.esd.mun.ca/gac_2001/seven/sub_program.asp?sess=492&form=9",
		                "http://gac.esd.mun.ca/gac_2001/seven/sub_program.asp?sess=492&form=9"),
		std::make_tuple("http://www.cocobongo.com.br/news/news.php?id=11169&sess=12",
		                "http://www.cocobongo.com.br/news/news.php?id=11169&sess=12")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsTs) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// ts
		std::make_tuple("https://m.firstrust.com/mw/05439/moreList.do?ts=1451195648812",
		                "https://m.firstrust.com/mw/05439/moreList.do"),
		std::make_tuple("http://bibliotheque.montreuil.fr/ipac20/ipac.jsp?profile=web&menu=dsiTab&ts=1422473229525",
		                "http://bibliotheque.montreuil.fr/ipac20/ipac.jsp?profile=web&menu=dsiTab"),

		// ts (no strip)
		std::make_tuple("http://www.obeczdiby.cz/uredni-deska/file.asp?id=46931&ts=Qz6FAab5D7jNu0RVw8y8W5FKB0VOvBU%3D&at=1",
		                "http://www.obeczdiby.cz/uredni-deska/file.asp?id=46931&ts=Qz6FAab5D7jNu0RVw8y8W5FKB0VOvBU%3D&at=1")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripApacheDirSort) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://www.amphonesinh.info/poemes/sa/livre/images/tmp/?C=D;O=",
		                "http://www.amphonesinh.info/poemes/sa/livre/images/tmp/"),
		std::make_tuple("http://gda.reconcavo.org.br/gda_repository/pesquisas/pos/?C=D;",
		                "http://gda.reconcavo.org.br/gda_repository/pesquisas/pos/"),
		std::make_tuple("http://mirrors.psychz.net/Centos/2.1/?C=S;O=A",
		                "http://mirrors.psychz.net/Centos/2.1/"),
		std::make_tuple("http://www.3ddx.com/blog/wp-includes/SimplePie/Decode/HTML/?C=N;O=D",
		                "http://www.3ddx.com/blog/wp-includes/SimplePie/Decode/HTML/"),
		std::make_tuple("http://macports.mirror.ac.za/release/ports/www/midori/?C=M&O=A",
		                "http://macports.mirror.ac.za/release/ports/www/midori/"),
		std::make_tuple("http://www.example.com/?C=&O=A",
		                "http://www.example.com/")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsOracleEloqua) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// oracle eloqua
		std::make_tuple("http://maersklinereefer.com/about/merry-christmas/?elqTrackId=786C9D2AE676DEC435B578D75CB0B4FD&elqaid=2666&elqat=2",
		                "http://maersklinereefer.com/about/merry-christmas/"),
		std::make_tuple("https://community.oracle.com/community/topliners/?elq_mid=21557&elq_cid=1618237&elq=3c0cfe27635443ca9b6410238cc876a9&elqCampaignId=2182&elqaid=21557&elqat=1&elqTrackId=40772b5725924f53bc2c6a6fb04759a3",
		                "https://community.oracle.com/community/topliners/"),
		std::make_tuple("http://app.reg.techweb.com/e/er?s=2150&lid=25554&elq=00000000000000000000000000000000&elqaid=2294&elqat=2&elqTrackId=3de2badc5d7c4a748bc30253468225fd",
		                "http://app.reg.techweb.com/e/er?s=2150&lid=25554"),
		std::make_tuple("http://www.biography.com/people/louis-armstrong-9188912?elq=7fd0dd577ebf4eafa1e73431feee849f&elqCampaignId=2887",
		                "http://www.biography.com/people/louis-armstrong-9188912")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsGoogleAnalytics) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// google analytics
		std::make_tuple("http://www.urchin.com/download.html?utm_source=newsletter4&utm_medium=email&utm_term=urchin&utm_content=easter&utm_campaign=product",
		                "http://www.urchin.com/download.html"),
		std::make_tuple("http://www.huffingtonpost.com/parker-marie-molloy/todd-kincannon-transgender-camps_b_4100777.html?utm_source=feedburner&utm_medium=feed&utm_campaign=Feed%3A+HP%2FPolitics+%28Politics+on+The+Huffington+Post",
		                "http://www.huffingtonpost.com/parker-marie-molloy/todd-kincannon-transgender-camps_b_4100777.html"),
		std::make_tuple("http://www.staffnet.manchester.ac.uk/news/display/?id=10341&;utm_source=newsletter&utm_medium=email&utm_campaign=eUpdate",
		                "http://www.staffnet.manchester.ac.uk/news/display/?id=10341"),
		std::make_tuple("http://www.nightdivestudios.com/games/turok-dinosaur-hunter/?utm_source=steampowered.com&utm_medium=product&utm_campaign=website%20-%20turok%20dinosaur%20hunter",
		                "http://www.nightdivestudios.com/games/turok-dinosaur-hunter/"),
		std::make_tuple("http://www.mihomes.com/Find-Your-New-Home/Virginia-Homes?utm_source=NewHomesDirectory.com&utm_campaign=referral-division&utm_medium=feed&utm_content=&utm_term=consumer&cookiecheck=true",
		                "http://www.mihomes.com/Find-Your-New-Home/Virginia-Homes?cookiecheck=true"),
		std::make_tuple("http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&utm_hp_ref=healthy-living&utm_hp_ref=au-life&adsSiteOverride=au",
		                "http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&adsSiteOverride=au")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsPiwik) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// piwik
		std::make_tuple("https://astro-report.com/lp2.html?pk_campaign=1%20Natal%20Chart%20-%20RDMs&pk_kwd=astrological%20chart%20free&gclid=CPfkwKfP2LgCFcJc3godgSMAHA",
		                "https://astro-report.com/lp2.html"),
		std::make_tuple("http://lapprussia.lappgroup.com/kontakty.html?pk_campaign=yadirect-crossselling&pk_kwd=olflex&pk_source=yadirect&pk_medium=cpc&pk_content=olflex&rel=bytib",
		                "http://lapprussia.lappgroup.com/kontakty.html?rel=bytib")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsOpenWebAnalytics) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// owa
		std::make_tuple("http://www.be-forever.at/Affiliate.aspx?banner_id=69&owa_medium=paid&owa_source=affiliate&owa_campaign=sportundfitnessmarathonskyscraper&owa_ad=2516&owa_ad_type=banner",
		                "http://www.be-forever.at/Affiliate.aspx?banner_id=69"),

		// owa (no strip)
		std::make_tuple("http://demo.openwebanalytics.com/owa/index.php?owa_period=today&owa_siteId=&owa_startDate=20150220&owa_endDate=20150220&owa_do=base.reportVisitor&owa_visitor_id=1424456444640459675",
		                "http://demo.openwebanalytics.com/owa/index.php?owa_period=today&owa_siteId=&owa_startDate=20150220&owa_endDate=20150220&owa_do=base.reportVisitor&owa_visitor_id=1424456444640459675")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsWebTrends) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// webtrends
		std::make_tuple("http://www.thermoscientific.com/en/product/haake-mars-rotational-rheometers.html?elq_mid=1089&elq_cid=107687&wt.mc_id=CAD_MOL_MC_PR_EM1_0815_NewHaakeMars_English_GLB_2647&elq=f17d2c276ed045c0bb391e4c273b789c&elqCampaignId=&elqaid=1089&elqat=1&elqTrackId=ce2a4c4879ee4f6488a14d924fa1f8a5",
		                "http://www.thermoscientific.com/en/product/haake-mars-rotational-rheometers.html")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsMarketo) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// marketo
		std::make_tuple("http://info.nexuminc.com/index.php/email/emailWebview?mkt_tok=3RkMMJWWfF9wsRomrfCcI63Em2iQPJWpsrB0B%2FDC18kX3RUsK7Sefkz6htBZF5s8TM3DWVNGXqdI4kENTrA%3D",
		                "http://info.nexuminc.com/index.php/email/emailWebview"),
		std::make_tuple("http://www.sescoil.com/learn-more-modicon-controllers?mkt_tok=3RkMMJWWfF9wsRomrfCcI63Em2iQPJWpsrB0B%2FDC18kX3RUvJb2ffkz6htBZF5s8TM3DVFRHXqpN6EEJQ7M%3D",
		                "http://www.sescoil.com/learn-more-modicon-controllers"),
		std::make_tuple("http://info.nimblestorage.com/dejeuner_avec_NimbleStorage_Asema_30avril.html?mkt_tok=3RkMMJWWfF9wsRomrfCcI63Em2iQPJWpsrB0B%2FDC18kX3RUrK7iYbAfind1SFJk7a8C6XFFGR91D7C0VTLbA",
		                "http://info.nimblestorage.com/dejeuner_avec_NimbleStorage_Asema_30avril.html")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsTrk) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// trk
		std::make_tuple("https://www.nerdwallet.com/investors/?trk=nw_gn_2.0",
		                "https://www.nerdwallet.com/investors/"),
		std::make_tuple("https://www.linkedin.com/company/intel-corporation?trk=ppro_cprof",
		                "https://www.linkedin.com/company/intel-corporation")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsWho) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// who
		std::make_tuple("http://www.bigchurch.com/go/page/privacy.html?who=r,/cu4qLiwvculGvDvGrNzyhKpyhktvMpoVzsk0AGO5LEkHr/CP73pECeMNUNAAnQxhyuVznsP0mN0_gc73W/4TBykmZSBM_dVZJuzeXuBRaskyEzrh1nIpIaeqHAY_dEZ",
		                "http://www.bigchurch.com/go/page/privacy.html"),
		std::make_tuple("https://affiliates.danni.com/p/partners/main.cgi?who=r,VgwuU_i/jKvDCoWe1vEMdEktDgo/UpGf6pX3qsopquP0xCYOlgReamC2S1RnQSdn5DG42QxPixcOP1q67s6_nK0kwqcf8YqW70Sux_iWenV/PNHPK9ddNE88CXGs9s2o&action=faq&trlid=affiliate_navbar_v1_0-15",
		                "https://affiliates.danni.com/p/partners/main.cgi?action=faq&trlid=affiliate_navbar_v1_0-15"),

		// who (no strip)
		std::make_tuple("http://www.pailung.com.tw/tw/Applications_Show2.aspx?idNo=E&typeNo=0&QueryStr=E&Who=Application",
		                "http://www.pailung.com.tw/tw/Applications_Show2.aspx?idNo=E&typeNo=0&QueryStr=E&Who=Application")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripParamsPartnerRef) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// partnerref
		std::make_tuple("http://www.lookfantastic.com/offers/20-off-your-top-20.list?partnerref=ENLF-_EmailExclusive",
		                "http://www.lookfantastic.com/offers/20-off-your-top-20.list")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripAffiliateAmazon) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// tag
		std::make_tuple("http://www.amazon.com/b?node=5174&tag=americanidolnet-20&camp=213769&creative=393921&linkCode=ur1&adid=1XZ1813ZHDE203A2DXNM&",
		                "http://www.amazon.com/b?node=5174&camp=213769&creative=393921&linkCode=ur1&adid=1XZ1813ZHDE203A2DXNM"),
		std::make_tuple("http://www.amazon.co.uk/b/ref=as_li_qf_br_sr_tl?_encoding=UTF8&camp=1634&creative=6738&linkCode=ur2&node=68&tag=emplorescu-21&linkId=YUHGPWBWLGWXUKWN",
		                "http://www.amazon.co.uk/b?_encoding=UTF8&camp=1634&creative=6738&linkCode=ur2&node=68&linkId=YUHGPWBWLGWXUKWN"),

		// tag (no strip)
		std::make_tuple("http://ec2-54-193-91-91.us-west-1.compute.amazonaws.com/index.php?route=product/search&tag=coffee",
		                "http://ec2-54-193-91-91.us-west-1.compute.amazonaws.com/index.php?route=product/search&tag=coffee"),
		std::make_tuple("http://www.exampleamazon.com/index.php?tag=coffee",
		                "http://www.exampleamazon.com/index.php?tag=coffee"),
		std::make_tuple("http://ec2-54-74-58-105.eu-west-1.compute.amazonaws.com/wordpress/tag/comfort/",
		                "http://ec2-54-74-58-105.eu-west-1.compute.amazonaws.com/wordpress/tag/comfort/"),

		// coliid
		std::make_tuple("http://www.amazon.ca/product-reviews/B001OMU6UM/ref=wl_it_v_cm_cr_acr_txt/181-6738123-5655708?ie=UTF8&colid=YI4DNQQXCCEZ&coliid=I2KNXYYL6X9E1W&showViewpoints=1",
		                "http://www.amazon.ca/product-reviews/B001OMU6UM/181-6738123-5655708?ie=UTF8&showViewpoints=1"),
		// colid
		std::make_tuple("http://www.amazon.ca/gp/offer-listing/B001OMU6UM?ie=UTF8&colid=YI4DNQQXCCEZ&coliid=I2KNXYYL6X9E1W",
		                "http://www.amazon.ca/gp/offer-listing/B001OMU6UM?ie=UTF8"),
		// ref
		std::make_tuple("http://www.amazon.com/dp/B00O29DJXU/ref=dm_ws_tlw_trk2",
		                "http://www.amazon.com/dp/B00O29DJXU"),
		std::make_tuple("http://www.amazon.co.uk/s/ref=nb_sb_noss?url=search-alias%3Dstripbooks&field-keywords=Hugh%20Oakeley%20Arnold-Forster%2C%20History%20of%20Engl",
		                "http://www.amazon.co.uk/s?url=search-alias%3Dstripbooks&field-keywords=Hugh%20Oakeley%20Arnold-Forster%2C%20History%20of%20Engl"),
		std::make_tuple("https://aws.amazon.com/marketplace/review/product-reviews/ref=srh_res_product_rating/187-4587074-8841263?ie=UTF8&asin=B00DYW58IA",
		                "https://aws.amazon.com/marketplace/review/product-reviews/187-4587074-8841263?ie=UTF8&asin=B00DYW58IA"),

		// ref (no strip)
		std::make_tuple("http://services.amazon.es/servicios/productos-patrocinados/funcionamiento.html/ref=ASESFooter",
		                "http://services.amazon.es/servicios/productos-patrocinados/funcionamiento.html/ref=ASESFooter")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, StripAffiliateEbay) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		// icep_ff3
		std::make_tuple("http://myworld.ebay.com/virtanen13/?icep_ff3=1&pub=5574719130&toolid=10001&campid=5336443318&customid=&ipn=psmain&icep_vectorid=229466&kwid=902099&mtid=824&kw=lg",
		                "http://myworld.ebay.com/virtanen13/?ipn=psmain&icep_vectorid=229466&kwid=902099&mtid=824&kw=lg"),
		// pub
		std::make_tuple("http://deals.ebay.com/5000583820__50_iTunes_Card_for_ONLY__40?customid=3ajQLCVZEeOu0AqHYiPljw0_eaui3_s9P_0_0&pub=5574652453&afepn=5335869999&campid=5335869999&afepn=5335869999",
		                "http://deals.ebay.com/5000583820__50_iTunes_Card_for_ONLY__40"),
		// toolid
		std::make_tuple("http://stores.ebay.com/Discount-Tire-Direct/Tires-/_i.html?customid=pQaBRKQAEeONq8ZSIP-VjA0_8Btz3_i9S_0_0&pub=5574652453&afepn=5335869999&campid=5335869999&_fsub=7361854",
		                "http://stores.ebay.com/Discount-Tire-Direct/Tires-/_i.html?_fsub=7361854"),

		// campid
		std::make_tuple("http://deals.ebay.com/5002838323_Lenovo_G50_15_6__Touchscreen_Laptop_Intel_Core_i3_5020U_2_2GHz_4GB_RAM_500GB_HDD?customid=3b986ca0ffba11e5a3cd9a2420d4ebaf0INT&pub=5574652453&campid=5335869999&afepn=5335869999&rmvSB=true&afepn=5335869999&rmvSB=true",
		                "http://deals.ebay.com/5002838323_Lenovo_G50_15_6__Touchscreen_Laptop_Intel_Core_i3_5020U_2_2GHz_4GB_RAM_500GB_HDD?rmvSB=true"),

		// customid
		std::make_tuple("http://www.ebay.com/itm/Classic-car-rare-rosewood-convertible-1964-/322088645170?icep_ff3=1&pub=5575157929&toolid=10001&campid=5337827646&customid=&ipn=psmain&icep_vectorid=229466&kwid=902099&mtid=824&kw=lg",
		                "http://www.ebay.com/itm/Classic-car-rare-rosewood-convertible-1964-/322088645170?ipn=psmain&icep_vectorid=229466&kwid=902099&mtid=824&kw=lg"),
		// afepn
		std::make_tuple("http://deals.ebay.com/5002902034_Sony_PlayStation_Plus_1_Year_Membership_Subscription_Card___NEW_?rmvSB=true&afepn=5335869999&afepn=5335869999&rmvSB=true",
		                "http://deals.ebay.com/5002902034_Sony_PlayStation_Plus_1_Year_Membership_Subscription_Card___NEW_?rmvSB=true"),

	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, Normalization) {
	std::vector<std::tuple<const char *, const char *>> test_cases = {
		std::make_tuple("http://puddicatcreationsdigitaldesigns.com/index.php?route=product/product&amp;product_id=1510",
		                "http://puddicatcreationsdigitaldesigns.com/index.php?route=product/product&product_id=1510"),
		std::make_tuple("http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&adsSiteOverride=au&section=australia",
		                "http://www.huffingtonpost.com.au/entry/tiny-moments-happiness_us_56ec1a35e4b084c672200a36?section=australia&adsSiteOverride=au"),
		std::make_tuple("http://www.example.com/%7ejoe/index.html", "http://www.example.com/~joe/index.html"),
		std::make_tuple("http://www.example.com/jo%e9/index.html", "http://www.example.com/jo%E9/index.html"),
		std::make_tuple("http://www.example.com/%7joe/index.html", "http://www.example.com/%257joe/index.html")
	};

	strip_param_tests(test_cases, 123);
}

TEST(UrlTest, GetSubPathLen) {
	Url url;
	url.set("http://s1.t7.example.com:8080/subpath1/sub2/s3/");

	uint8_t i = 0;

	const char *expected = "/subpath1/sub2/s3/";
	size_t expectedLen = strlen(expected);

	EXPECT_EQ(url.getSubPathLen(i++), expectedLen);
	EXPECT_EQ(url.getSubPathLen(i++), expectedLen);

	expected = "/subpath1/sub2/";
	expectedLen = strlen(expected);
	EXPECT_EQ(url.getSubPathLen(i++), expectedLen);

	expected = "/subpath1/";
	expectedLen = strlen(expected);
	EXPECT_EQ(url.getSubPathLen(i++), expectedLen);

	expected = "";
	expectedLen = strlen(expected);
	EXPECT_EQ(url.getSubPathLen(i++), expectedLen);
}

TEST(UrlTest, LongUrl) {
	const char *bad_url = "http://www.amazon.com/s/ref=nb_sb_ss_i_4_15?url=search-alias%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%3Dpopular&field-keywords=kazumi+watanabe+spice+of+life&sprefix=Kazumi+Watanabe";

	Url url;
	url.set(bad_url);
	EXPECT_LT(url.getUrlLen(), MAX_URL_LEN);
}
