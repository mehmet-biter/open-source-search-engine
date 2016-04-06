#include "gtest/gtest.h"

#include "Url.h"
#include "SafeBuf.h"

TEST( UrlTest, SetNonAsciiValid ) {
	const char* input_urls[] = {
	    "http://topbeskæring.dk/velkommen",
	    "www.Alliancefrançaise.nu",
	    "française.Alliance.nu",
	    "française.Alliance.nu/asdf",
	    "http://française.Alliance.nu/asdf",
	    "http://française.Alliance.nu/",
	    "幸运.龍.com",
	    "幸运.龍.com/asdf/运/abc",
	    "幸运.龍.com/asdf",
	    "http://幸运.龍.com/asdf",
	    "http://Беларуская.org/Акадэмічная",
	    "https://hi.Български.com",
	    "https://fakedomain.中文.org/asdf",
	    "https://gigablast.com/abc/文/efg",
	    "https://gigablast.com/?q=文",
	    "http://www.example.сайт",
	    "http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqués",
	    "http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
	    "http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
	    "http://腕時計通販.jp/",
	    "http://сацминэнерго.рф/robots.txt",
	    "http://faß.de/",
	    "http://βόλος.com/",
	    "http://ශ්‍රී.com/",
	    "http://نامه‌ای.com/"
	};

	const char *expected_normalized[] = {
		"http://xn--topbeskring-g9a.dk/velkommen",
		"http://www.xn--alliancefranaise-npb.nu/",
		"http://xn--franaise-v0a.alliance.nu/",
		"http://xn--franaise-v0a.alliance.nu/asdf",
		"http://xn--franaise-v0a.alliance.nu/asdf",
		"http://xn--franaise-v0a.alliance.nu/",
		"http://xn--lwt711i.xn--mi7a.com/",
		"http://xn--lwt711i.xn--mi7a.com/asdf/%E8%BF%90/abc",
		"http://xn--lwt711i.xn--mi7a.com/asdf",
		"http://xn--lwt711i.xn--mi7a.com/asdf",
		"http://xn--d0a6das0ae0bir7j.org/%D0%90%D0%BA%D0%B0%D0%B4%D1%8D%D0%BC%D1%96%D1%87%D0%BD%D0%B0%D1%8F",
		"https://hi.xn--d0a6divjd1bi0f.com/",
		"https://fakedomain.xn--fiq228c.org/asdf",
		"https://gigablast.com/abc/%E6%96%87/efg",
		"https://gigablast.com/?q=%E6%96%87",
		"http://www.example.xn--80aswg/",
		"http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqu%C3%A9s",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.xn--80aswg/xn--fooled-you-into-trying-to-decode-this",
		"http://xn--kjvp61d69f6wc3zf.jp/",
	    "http://xn--80agflthakqd0d1e.xn--p1ai/robots.txt",
		"http://xn--fa-hia.de/",
		"http://xn--nxasmm1c.com/",
		"http://xn--10cl1a0b660p.com/",
		"http://xn--mgba3gch31f060k.com/"
	};

	ASSERT_EQ( sizeof( input_urls ) / sizeof( input_urls[0] ),
			   sizeof( expected_normalized ) / sizeof( expected_normalized[0] ) );

	size_t len = sizeof( input_urls ) / sizeof( input_urls[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Url url;
		url.set( input_urls[i] );

		EXPECT_STREQ(expected_normalized[i], (const char*)url.getUrl());
	}
}

TEST( UrlTest, SetNonAsciiInValid ) {
	const char* input_urls[] = {
		"http://www.fas.org/blog/ssp/2009/08/securing-venezuela\032s-arsenals.php",
		"https://pypi.python\n\n\t\t\t\t.org/packages/source/p/pyramid/pyramid-1.5.tar.gz",
		"http://undocs.org/ru/A/C.3/68/\vSR.48"
	};

	const char *expected_normalized[] = {
		"http://www.fas.org/blog/ssp/2009/08/securing-venezuela%1As-arsenals.php",
		"https://pypi.python/",
		"http://undocs.org/ru/A/C.3/68/%0BSR.48"
	};

	ASSERT_EQ( sizeof( input_urls ) / sizeof( input_urls[0] ),
			   sizeof( expected_normalized ) / sizeof( expected_normalized[0] ) );

	size_t len = sizeof( input_urls ) / sizeof( input_urls[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Url url;
		url.set( input_urls[i] );

		EXPECT_STREQ(expected_normalized[i], (const char*)url.getUrl());
	}
}

TEST( UrlTest, GetDisplayUrlFromCharArray ) {
	const char* input_urls[] = {
		"http://xn--topbeskring-g9a.dk/velkommen",
		"www.xn--Alliancefranaise-npb.nu",
		"xn--franaise-v0a.Alliance.nu",
		"xn--franaise-v0a.Alliance.nu/asdf",
		"http://xn--franaise-v0a.Alliance.nu/asdf",
		"http://xn--franaise-v0a.Alliance.nu/",
		"xn--lwt711i.xn--mi7a.com",
		"xn--lwt711i.xn--mi7a.com/asdf/运/abc",
		"xn--lwt711i.xn--mi7a.com/asdf",
		"http://xn--lwt711i.xn--mi7a.com/asdf",
		"http://xn--d0a6das0ae0bir7j.org/Акадэмічная",
		"https://hi.xn--d0a6divjd1bi0f.com",
		"https://fakedomain.xn--fiq228c.org/asdf",
		"http://www.example.xn--80aswg",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.xn--80aswg/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://xn--kjvp61d69f6wc3zf.jp/",
		"http://xn--80agflthakqd0d1e.xn--p1ai/robots.txt",
		"http://xn--80agflthakqd0d1e.xn--p1ai",
		"http://сацминэнерго.рф",
		"http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai"
	};

	const char *expected_display[] = {
		"http://topbeskæring.dk/velkommen",
		"www.Alliancefrançaise.nu",
		"française.Alliance.nu",
		"française.Alliance.nu/asdf",
		"http://française.Alliance.nu/asdf",
		"http://française.Alliance.nu/",
		"幸运.龍.com",
		"幸运.龍.com/asdf/运/abc",
		"幸运.龍.com/asdf",
		"http://幸运.龍.com/asdf",
		"http://Беларуская.org/Акадэмічная",
		"https://hi.Български.com",
		"https://fakedomain.中文.org/asdf",
		"http://www.example.сайт",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://腕時計通販.jp/",
		"http://сацминэнерго.рф/robots.txt",
		"http://сацминэнерго.рф",
		"http://сацминэнерго.рф",
		"http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai"
	};

	ASSERT_EQ( sizeof( input_urls ) / sizeof( input_urls[0] ),
	           sizeof( expected_display ) / sizeof( expected_display[0] ) );

	size_t len = sizeof( input_urls ) / sizeof( input_urls[0] );
	for ( size_t i = 0; i < len; i++ ) {
		StackBuf( tmpBuf );
		EXPECT_STREQ( expected_display[i], (const char *) Url::getDisplayUrl( input_urls[i], &tmpBuf ));
	}
}

TEST( UrlTest, GetDisplayUrlFromUrl ) {
	const char* input_urls[] = {
		"http://topbeskæring.dk/velkommen",
		"www.Alliancefrançaise.nu",
		"française.Alliance.nu",
		"française.Alliance.nu/asdf",
		"http://française.Alliance.nu/asdf",
		"http://française.Alliance.nu/",
		"幸运.龍.com",
		"幸运.龍.com/asdf/运/abc",
		"幸运.龍.com/asdf",
		"http://幸运.龍.com/asdf",
		"http://Беларуская.org/Акадэмічная",
		"https://hi.Български.com",
		"https://fakedomain.中文.org/asdf",
		"https://gigablast.com/abc/文/efg",
		"https://gigablast.com/?q=文",
		"http://www.example.сайт",
		"http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqués",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://腕時計通販.jp/",
		"http://сацминэнерго.рф/robots.txt",
		"http://сацминэнерго.рф",
	    "http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai"
	};

	const char *expected_display[] = {
		"http://topbeskæring.dk/velkommen",
		"http://www.alliancefrançaise.nu/",
		"http://française.alliance.nu/",
		"http://française.alliance.nu/asdf",
		"http://française.alliance.nu/asdf",
		"http://française.alliance.nu/",
		"http://幸运.龍.com/",
		"http://幸运.龍.com/asdf/%E8%BF%90/abc",
		"http://幸运.龍.com/asdf",
		"http://幸运.龍.com/asdf",
		"http://Беларуская.org/%D0%90%D0%BA%D0%B0%D0%B4%D1%8D%D0%BC%D1%96%D1%87%D0%BD%D0%B0%D1%8F",
		"https://hi.Български.com/",
		"https://fakedomain.中文.org/asdf",
		"https://gigablast.com/abc/%E6%96%87/efg",
		"https://gigablast.com/?q=%E6%96%87",
		"http://www.example.сайт/",
		"http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqu%C3%A9s",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://腕時計通販.jp/",
		"http://сацминэнерго.рф/robots.txt",
		"http://сацминэнерго.рф/",
		"http://mct.verisign-grs.com/convertServlet?input=r7d.xn--g1a8ac.xn--p1ai"
	};

	ASSERT_EQ( sizeof( input_urls ) / sizeof( input_urls[0] ),
	           sizeof( expected_display ) / sizeof( expected_display[0] ) );

	size_t len = sizeof( input_urls ) / sizeof( input_urls[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Url url;
		url.set( input_urls[i] );

		StackBuf( tmpBuf );
		EXPECT_STREQ( expected_display[i], (const char*)Url::getDisplayUrl( url.getUrl(), &tmpBuf ) );
	}
}

