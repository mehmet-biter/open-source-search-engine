#include <gtest/gtest.h>
#include "HttpMime.h"
#include "SafeBuf.h"
#include "Log.h"

//
// test class
//
class TestHttpMime : public HttpMime {
public:
	TestHttpMime(const char *httpResponse, const char *url = "", bool parseMime = true)
		: HttpMime() {
		m_url.set(url);

		// 2016-11-25 15:00:00
		setCurrentTime(1480086000);

		if (parseMime) {
			set(const_cast<char*>(httpResponse), httpResponse ? strlen(httpResponse) : 0, &m_url);
		} else {
			auto mimeLen = getMimeLen(const_cast<char *>(httpResponse), strlen(httpResponse));

			setMime(httpResponse);
			setMimeLen(mimeLen);
			setContent(httpResponse + mimeLen);
		}
	}

	~TestHttpMime() {
//		print();
	}

	using HttpMime::getNextLine;
	using HttpMime::getCurrentLine;
	using HttpMime::getCurrentLineLen;

	using HttpMime::getCookies;

	using HttpMime::parseCookieDate;

	void verifyCookie(const char *cookieName, const char *expectedCookie, const char *path = "", const char *domain = "", bool httpOnly = false, bool secure = false);

	bool addCookieHeader(const char *url, SafeBuf *sb) {
		SafeBuf cookieJar;
		if (HttpMime::addToCookieJar(&m_url, &cookieJar)) {
			return HttpMime::addCookieHeader(cookieJar.getBufStart(), url, sb);
		}

		return false;
	}

private:
	Url m_url;
};

TEST(HttpMimeTest, FakeMime) {
	TestHttpMime httpMime(NULL);
}

static void expectLine(TestHttpMime *httpMime, const char *expectedLine = "") {
	size_t expectedLineLen = strlen(expectedLine);
	bool result = httpMime->getNextLine();

	std::stringstream ss;
	ss << __func__ << ":"
	   << " expectedLine='" << expectedLine << "'"
	   << " currentLine='" << httpMime->getCurrentLineLen() << "'";
	SCOPED_TRACE(ss.str());

	if (expectedLineLen == 0) {
		ASSERT_FALSE(result);
	} else {
		ASSERT_TRUE(result);
	}

	EXPECT_EQ(expectedLineLen, httpMime->getCurrentLineLen());
	EXPECT_EQ(0, strncmp(expectedLine, httpMime->getCurrentLine(), httpMime->getCurrentLineLen()));
}

TEST(HttpMimeTest, GetNextLineSingle) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Encoding: gzip\r\n"
		"Accept-Ranges: bytes\r\n"
		"Cache-Control: max-age=604800\r\n"
		"Content-Type: text/html\r\n"
		"Date: Tue, 29 Nov 2016 13:05:20 GMT\r\n"
		"Etag: \"359670651\"\r\n"
		"Expires: Tue, 06 Dec 2016 13:05:20 GMT\r\n"
		"Last-Modified: Fri, 09 Aug 2013 23:54:35 GMT\r\n"
		"Server: ECS (phl/9D2C)\r\n"
		"X-Cache: HIT\r\n"
		"x-ec-custom-error: 1\r\n"
		"Content-Length: 606\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.example.com", false);

	expectLine(&httpMime, "HTTP/1.1 200 OK");
	expectLine(&httpMime, "Content-Encoding: gzip");
	expectLine(&httpMime, "Accept-Ranges: bytes");
	expectLine(&httpMime, "Cache-Control: max-age=604800");
	expectLine(&httpMime, "Content-Type: text/html");
	expectLine(&httpMime, "Date: Tue, 29 Nov 2016 13:05:20 GMT");
	expectLine(&httpMime, "Etag: \"359670651\"");
	expectLine(&httpMime, "Expires: Tue, 06 Dec 2016 13:05:20 GMT");
	expectLine(&httpMime, "Last-Modified: Fri, 09 Aug 2013 23:54:35 GMT");
	expectLine(&httpMime, "Server: ECS (phl/9D2C)");
	expectLine(&httpMime, "X-Cache: HIT");
	expectLine(&httpMime, "x-ec-custom-error: 1");
	expectLine(&httpMime, "Content-Length: 606");
	expectLine(&httpMime);
}

TEST(HttpMimeTest, GetNextLineMulti) {
	char httpResponse[] =
		"HTTP/1.1 301 Moved Permanently\r\n"
		"Server: nginx/1.8.1\r\n"
		"Cache-Control: no-cache, must-revalidate\r\n"
		"Content-Type: text/html\r\n"
		"Date: Sat, 26 Nov 2016 22:09:37 GMT\r\n"
		"Location: https://www.trade.com/en\r\n"
		"Expires: Sun, 19 Nov 1978 05:00:00 GMT\r\n"
		"Transfer-Encoding: chunked\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"Connection: Keep-Alive\r\n"
		"Set-Cookie: visid_incap_648123=OYmc2h/MSqOQqova5JAtYDgIOlgAAAAAQUIPAAAAAADGCaLNyrX7KMWXOkNDka+t; expires=Sun, 26 Nov 2017 07:38:31 GMT; path=/; Domain=.trade.com\r\n"
		"Set-Cookie: incap_ses_275_648123=6+dwWZaKJWrYlZKsRwDRAzgIOlgAAAAA6u7gQCQ+GQkQYcrWkyoFmg==; path=/; Domain=.trade.com\r\n"
		"Set-Cookie: ___utmvmiYupDzs=nJllyfIBpJT; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvaiYupDzs=yIqOmGx; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvbiYupDzs=wZT\r\n"
		"    XhpOJalg: ztf; path=/; Max-Age=900\r\n"
		"X-Iinfo: 9-36407832-36407842 NNNN CT(39 87 0) RT(1480198199938 111) q(0 0 1 0) r(5 5) U5\r\n"
		"X-CDN: Incapsula\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "https://www.trade.com/", false);
	expectLine(&httpMime, "HTTP/1.1 301 Moved Permanently");
	expectLine(&httpMime, "Server: nginx/1.8.1");
	expectLine(&httpMime, "Cache-Control: no-cache, must-revalidate");
	expectLine(&httpMime, "Content-Type: text/html");
	expectLine(&httpMime, "Date: Sat, 26 Nov 2016 22:09:37 GMT");
	expectLine(&httpMime, "Location: https://www.trade.com/en");
	expectLine(&httpMime, "Expires: Sun, 19 Nov 1978 05:00:00 GMT");
	expectLine(&httpMime, "Transfer-Encoding: chunked");
	expectLine(&httpMime, "X-Content-Type-Options: nosniff");
	expectLine(&httpMime, "Connection: Keep-Alive");
	expectLine(&httpMime, "Set-Cookie: visid_incap_648123=OYmc2h/MSqOQqova5JAtYDgIOlgAAAAAQUIPAAAAAADGCaLNyrX7KMWXOkNDka+t; expires=Sun, 26 Nov 2017 07:38:31 GMT; path=/; Domain=.trade.com");
	expectLine(&httpMime, "Set-Cookie: incap_ses_275_648123=6+dwWZaKJWrYlZKsRwDRAzgIOlgAAAAA6u7gQCQ+GQkQYcrWkyoFmg==; path=/; Domain=.trade.com");
	expectLine(&httpMime, "Set-Cookie: ___utmvmiYupDzs=nJllyfIBpJT; path=/; Max-Age=900");
	expectLine(&httpMime, "Set-Cookie: ___utmvaiYupDzs=yIqOmGx; path=/; Max-Age=900");
	expectLine(&httpMime, "Set-Cookie: ___utmvbiYupDzs=wZT\r\n    XhpOJalg: ztf; path=/; Max-Age=900");
	expectLine(&httpMime, "X-Iinfo: 9-36407832-36407842 NNNN CT(39 87 0) RT(1480198199938 111) q(0 0 1 0) r(5 5) U5");
	expectLine(&httpMime, "X-CDN: Incapsula");
	expectLine(&httpMime);
}

TEST(HttpMimeTest, GetNextLineMultiEnd) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Server: Apache\r\n"
		"X-Powered-By: Servlet 2.5; JBoss-5.0/JBossWeb-2.1\r\n"
		"Content-Language: en-US\r\n"
		"P3P: policyref=\"http://www.waters.com/w3c/p3p.xml\", CP=\"CAO DSP COR CUR ADM DEV TAI CONo OUR IND PHY ONL UNI\"\r\n"
		"Content-Type: text/html;charset=UTF-8\r\n"
		"Expires: Sat, 26 Nov 2016 11:50:54 GMT\r\n"
		"Cache-Control: max-age=0, no-cache, no-store\r\n"
		"Pragma: no-cache\r\n"
		"Date: Sat, 26 Nov 2016 11:50:54 GMT\r\n"
		"Connection: keep-alive\r\n"
		"X-Akamai-Edgescape: georegion=61,country_code=DK,region_code=,city=COPENHAGEN,dma=,pmsa\r\n"
		"       =,msa=,areacode=,county=,fips=,lat=55.67,long=12.58,timezone=GMT+1,zip=,continent=EU,throughput=vhigh,bw=5000,asnum=3292\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.waters.com/waters/home.htm?locale=en_US", false);
	expectLine(&httpMime, "HTTP/1.1 200 OK");
	expectLine(&httpMime, "Server: Apache");
	expectLine(&httpMime, "X-Powered-By: Servlet 2.5; JBoss-5.0/JBossWeb-2.1");
	expectLine(&httpMime, "Content-Language: en-US");
	expectLine(&httpMime, "P3P: policyref=\"http://www.waters.com/w3c/p3p.xml\", CP=\"CAO DSP COR CUR ADM DEV TAI CONo OUR IND PHY ONL UNI\"");
	expectLine(&httpMime, "Content-Type: text/html;charset=UTF-8");
	expectLine(&httpMime, "Expires: Sat, 26 Nov 2016 11:50:54 GMT");
	expectLine(&httpMime, "Cache-Control: max-age=0, no-cache, no-store");
	expectLine(&httpMime, "Pragma: no-cache");
	expectLine(&httpMime, "Date: Sat, 26 Nov 2016 11:50:54 GMT");
	expectLine(&httpMime, "Connection: keep-alive");
	expectLine(&httpMime, "X-Akamai-Edgescape: georegion=61,country_code=DK,region_code=,city=COPENHAGEN,dma=,pmsa\r\n       =,msa=,areacode=,county=,fips=,lat=55.67,long=12.58,timezone=GMT+1,zip=,continent=EU,throughput=vhigh,bw=5000,asnum=3292");
	expectLine(&httpMime);
}

static void verifyCookieDate(const char *cookieDateStr, time_t expectedCookieDate) {
	time_t cookieDate = 0;
	bool expectedResult = (expectedCookieDate != 0);
	EXPECT_EQ(expectedResult, TestHttpMime::parseCookieDate(cookieDateStr, strlen(cookieDateStr), &cookieDate));
	EXPECT_EQ(expectedCookieDate, cookieDate);
}

TEST(HttpMimeTest, ParseCookieDateValid) {
	std::vector<const char*> test_cases = {
		"Wed, 30-Nov-2016 21:52:33 GMT",
		"Wed, 30-Nov-16 21:52:33 GMT",
		"Wed, 30-11-2016 21:52:33 GMT",
		"Wed, 30 Nov 2016 21:52:33 GMT",
		"Wed, 30 Nov 2016 21:52:33 -0000",
	    "Wed, 30 November 2016 21:52:33 GMT",
	    "Wed Nov 30 21:52:33 2016",
	    "Wed 30-Nov-2016 21:52:33 GMT",
	    "2016-11-30 21:52:33",
	    "Wed 30 Nov 2016 21:52:33",
	    "30 Nov 2016 21:52:33 GMT"
	};

	for (auto test_case : test_cases) {
		verifyCookieDate(test_case, 1480542753);
	}
}

TEST(HttpMimeTest, ParseCookieDateInvalid) {
	std::vector<const char*> test_cases = {
		" ",
	    "0",
	    "1w",
	    "bad allocation, 27-Nov-2016 16:00:32 GMT",
	    "time()+60*60*24*365"
	};

	for (auto test_case : test_cases) {
		verifyCookieDate(test_case, 0);
	}
}

void TestHttpMime::verifyCookie(const char *cookieName, const char *expectedCookie, const char *path, const char *domain, bool httpOnly, bool secure) {
	std::stringstream ss;
	ss << __func__ << ":"
	   << " expectedCookie='" << expectedCookie << "'";
	SCOPED_TRACE(ss.str());

	auto cookies = getCookies();

	auto result = cookies.find(cookieName);
	ASSERT_NE(cookies.end(), result);

	auto cookie = result->second;
	EXPECT_EQ(strlen(cookieName), cookie.m_nameLen);
	EXPECT_EQ(strlen(expectedCookie), cookie.m_cookieLen);
	EXPECT_EQ(0, strncmp(cookie.m_cookie, expectedCookie, cookie.m_cookieLen));

	EXPECT_EQ(strlen(path), cookie.m_pathLen);
	EXPECT_EQ(0, strncmp(cookie.m_path, path, cookie.m_pathLen));

	EXPECT_EQ(strlen(domain), cookie.m_domainLen);
	EXPECT_EQ(0, strncmp(cookie.m_domain, domain, cookie.m_domainLen));

	EXPECT_EQ(httpOnly, cookie.m_httpOnly);
	EXPECT_EQ(secure, cookie.m_secure);
}

TEST(HttpMimeTest, SetCookieSingle) {
	char httpResponse[] =
		"HTTP/1.1 301 Moved Permanently\r\n"
		"Date: Sat, 26 Nov 2016 03:55:29 GMT\r\n"
		"Server: Apache\r\n"
		"Set-Cookie: dwsid=YOMMOhBA2zfORcqr_X51eDb97ckk73E_h5yW1knfgn_xLLw63c1CQjq6KCbOEPyG5NjByYmtQ7ZQTHmzbsTqeA==; path=/; HttpOnly\r\n"
		"Cache-Control: no-cache,no-store,must-revalidate\r\n"
		"Pragma: no-cache\r\n"
		"Expires: Thu, 01 Dec 1994 16:00:00 GMT\r\n"
		"x-dw-request-base-id: YRJNLVg5B7G_AgAK\r\n"
		"Location: http://www.adidas.ca/\r\n"
		"Accept-Ranges: bytes\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://adidas.ca");

	ASSERT_EQ(1, httpMime.getCookies().size());
	httpMime.verifyCookie("dwsid", "dwsid=YOMMOhBA2zfORcqr_X51eDb97ckk73E_h5yW1knfgn_xLLw63c1CQjq6KCbOEPyG5NjByYmtQ7ZQTHmzbsTqeA==", "/", "", true);
}

TEST(HttpMimeTest, SetCookieMultiple) {
	char httpResponse[] =
		"HTTP/1.0 302 Moved Temporarily\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Location: https://answers.microsoft.com/en-us/site/startsignin?pageUrl=http%3A%2F%2Fanswers.microsoft.com%3A80%2Fen-us%2Fie%3Fauth%3D1&silent=True\r\n"
		"Server: Microsoft-IIS/8.5\r\n"
		"X-FRAME-OPTIONS: SAMEORIGIN\r\n"
		"X-UA-Compatible: IE=edge\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"X-EdgeConnect-MidMile-RTT: 0\r\n"
		"X-EdgeConnect-Origin-MEX-Latency: 31\r\n"
		"X-EdgeConnect-MidMile-RTT: 23\r\n"
		"X-EdgeConnect-Origin-MEX-Latency: 31\r\n"
		"Expires: Thu, 24 Nov 2016 13:31:11 GMT\r\n"
		"Cache-Control: max-age=0, no-cache, no-store\r\n"
		"Pragma: no-cache\r\n"
		"Date: Thu, 24 Nov 2016 13:31:11 GMT\r\n"
		"Connection: close\r\n"
		"Set-Cookie: community.silentsignin=; domain=answers.microsoft.com; path=/; HttpOnly\r\n"
		"Set-Cookie: MS-CACHE=CACHE-true; expires=Thu, 26-Nov-2016 13:31:41 GMT\r\n"
		"Cache-Control: no-transform\r\n"
		"\r\n"
		"<html><head><title>Object moved</title></head><body>\n"
		"<h2>Object moved to <a href=\"https&#58;&#47;&#47;answers.microsoft.com&#47;en-us&#47;site&#47;startsignin&#63;pageUrl&#61;http&#37;3A&#37;2F&#37;2Fanswers.microsoft.com&#37;3A80&#37;2Fen-us&#37;2Fie&#37;3Fauth&#37;3D1&#38;silent&#61;True\">here</a>.</h2>\n"
		"</body></html>\n";

	TestHttpMime httpMime(httpResponse, "http://answers.microsoft.com/en-us/ie");

	ASSERT_EQ(2, httpMime.getCookies().size());
	httpMime.verifyCookie("community.silentsignin", "community.silentsignin=", "/", "answers.microsoft.com", true);
	httpMime.verifyCookie("MS-CACHE", "MS-CACHE=CACHE-true");
}

TEST(HttpMimeTest, SetCookieWithSemiColon) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Date: Fri, 25 Nov 2016 17:56:23 GMT\r\n"
		"Content-Type: text/html;charset=UTF-8\r\n"
		"Content-Length: 87297\r\n"
		"Connection: keep-alive\r\n"
		"Set-Cookie: __cfduid=d525092a577a90956dc2d6f3bc44e013f1480096583; expires=Sat, 25-Nov-17 17:56:23 GMT; path=/; domain=.homeadvisor.com; HttpOnly\r\n"
		"Set-Cookie: JSESSIONID=E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1; Path=/; HttpOnly\r\n"
		"Set-Cookie: cookiesEnabled=1\r\n"
		"Set-Cookie: aff_track=2|*|23116563|*|0; Path=/\r\n"
		"Set-Cookie: originatingSessionID=1480096608592pwspr009E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1; Domain=.homeadvisor.com; Expires=Mon, 25-Nov-2019 17:56:48 GMT; Path=/\r\n"
		"Set-Cookie: csacn=746971; Domain=.homeadvisor.com; Expires=Mon, 25-Nov-2019 17:56:48 GMT; Path=/\r\n"
		"Set-Cookie: csdcn=1480096608592; Domain=.homeadvisor.com; Expires=Mon, 25-Nov-2019 17:56:48 GMT; Path=/\r\n"
		"Set-Cookie: psacn=\"\"; Domain=.homeadvisor.com; Expires=Mon, 25-Nov-2019 17:56:48 GMT; Path=/\r\n"
		"Set-Cookie: psdcn=0; Domain=.homeadvisor.com; Expires=Mon, 25-Nov-2019 17:56:48 GMT; Path=/\r\n"
		"Set-Cookie: sess_log=1480096608592pwspr009E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1; Domain=.homeadvisor.com; Path=/\r\n"
		"Content-Language: en-US\r\n"
		"Set-Cookie: X-ha-bd-sess=1480096608:-1324405922;\r\n"
		"Server: cloudflare-nginx\r\n"
		"CF-RAY: 3076fa1e04c73d67-CPH\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.homeadvisor.com/");

	ASSERT_EQ(11, httpMime.getCookies().size());
	httpMime.verifyCookie("__cfduid", "__cfduid=d525092a577a90956dc2d6f3bc44e013f1480096583", "/", "homeadvisor.com", true);
	httpMime.verifyCookie("JSESSIONID", "JSESSIONID=E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1", "/", "", true);
	httpMime.verifyCookie("cookiesEnabled", "cookiesEnabled=1", "", "");
	httpMime.verifyCookie("aff_track", "aff_track=2|*|23116563|*|0", "/", "");
	httpMime.verifyCookie("originatingSessionID", "originatingSessionID=1480096608592pwspr009E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1", "/", "homeadvisor.com");
	httpMime.verifyCookie("csacn", "csacn=746971", "/", "homeadvisor.com");
	httpMime.verifyCookie("csdcn", "csdcn=1480096608592", "/", "homeadvisor.com");
	httpMime.verifyCookie("psacn", "psacn=\"\"", "/", "homeadvisor.com");
	httpMime.verifyCookie("psdcn", "psdcn=0", "/", "homeadvisor.com");
	httpMime.verifyCookie("sess_log", "sess_log=1480096608592pwspr009E041F9D467FE4F403E0DCBFB3812C937.pwspr009-1", "/", "homeadvisor.com");
	httpMime.verifyCookie("X-ha-bd-sess", "X-ha-bd-sess=1480096608:-1324405922");
}

TEST(HttpMimeTest, SetCookieWithoutSemiColon) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Vary: Accept-Encoding, Cookie\r\n"
		"X-Drupal-Cache: HIT\r\n"
		"Content-Language: es\r\n"
		"X-Frame-Options: SAMEORIGIN\r\n"
		"X-UA-Compatible: IE=edge\r\n"
		"X-Generator: Drupal 7 (http://drupal.org)\r\n"
		"Link: <http://www.webconsultas.com>; rel=\"canonical\",<http://www.webconsultas.com/>; rel=\"shortlink\"\r\n"
		"Cache-Control: public, max-age=900\r\n"
		"Last-Modified: Fri, 25 Nov 2016 19:30:45 GMT\r\n"
		"Date: Fri, 25 Nov 2016 19:46:30 GMT\r\n"
		"Age: 659\r\n"
		"Connection: keep-alive\r\n"
		"X-Cache: HIT\r\n"
		"Set-Cookie: wccountry=DK\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.webconsultas.com/");

	ASSERT_EQ(1, httpMime.getCookies().size());
	httpMime.verifyCookie("wccountry", "wccountry=DK");
}

TEST(HttpMimeTest, SetCookieWithoutEqualSign) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Cache-Control: private,max-age=0\r\n"
		"Content-Length: 5305\r\n"
		"Content-Type: text/html\r\n"
		"Expires: Fri, 11 Nov 2016 03:38:01 GMT\r\n"
		"Last-Modified: Tue, 22 Dec 2015 02:02:44 GMT\r\n"
		"ETag: \"{70B050A9-4AA0-4144-907C-BF2217455640},11\"\r\n"
		"X-SharePointHealthScore: 0\r\n"
		"ResourceTag: rt:70B050A9-4AA0-4144-907C-BF2217455640@00000000011\r\n"
		"Public-Extension: http://schemas.microsoft.com/repl-2\r\n"
		"SPRequestGuid: 2e0ebb9d-736c-d090-8c09-41ab043a0fe5\r\n"
		"request-id: 2e0ebb9d-736c-d090-8c09-41ab043a0fe5\r\n"
		"X-FRAME-OPTIONS: SAMEORIGIN\r\n"
		"X-FRAME-OPTIONS: SAMEORIGIN\r\n"
		"SPRequestDuration: 12\r\n"
		"SPIisLatency: 3\r\n"
		"Set-Cookie: ; HttpOnly; Secure\r\n"
		"X-Frame-Options: SAMEORIGIN\r\n"
		"X-Content-Type-Options: nosniff\r\n"
		"X-MS-InvokeApp: 1; RequireReadOnly\r\n"
		"MicrosoftSharePointTeamServices: 15.0.0.4569\r\n"
		"Date: Sat, 26 Nov 2016 03:38:00 GMT\r\n"
		"X-FRAME-OPTIONS: SAMEORIGIN\r\n"
		"Set-Cookie: BNES_=KeDJ3Ja69/PA3J4h/GCIXRB2qz2D+IbWjtxUsQcP7amrmIxZpOOuFSjJeNFbxXG0; HttpOnly; Secure\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.customs.gov.my/front.html");

	ASSERT_EQ(1, httpMime.getCookies().size());
	httpMime.verifyCookie("BNES_", "BNES_=KeDJ3Ja69/PA3J4h/GCIXRB2qz2D+IbWjtxUsQcP7amrmIxZpOOuFSjJeNFbxXG0", "", "", true, true);
}

TEST(HttpMimeTest, SetCookieWithoutName) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Date: Fri, 25 Nov 2016 17:17:30 GMT\r\n"
		"Server: Apache\r\n"
		"Set-Cookie: ci_session=a%3A4%3A%7Bs%3A10%3A%22session_id%22%3Bs%3A32%3A%22dc95f0310ff19a2ad18e4e8324596100%22%3Bs%3A10%3A%22ip_address%22%3Bs%3A13%3A%22192.168.0.252%22%3Bs%3A10%3A%22user_agent%22%3Bs%3A11%3A%22curl%2F7.47.1%22%3Bs%3A13%3A%22last_activity%22%3Bs%3A10%3A%221480094250%22%3B%7D80d8db7afd9f2879b03e67333d6089f6; expires=Fri, 25-Nov-2016 19:17:30 GMT; Max-Age=7200; path=/; domain=.3bmeteo.com; httponly\r\n"
		"Set-Cookie: =0; path=/; domain=.3bmeteo.com\r\n"
		"Set-Cookie: sn=%7B%22id%22%3A%223a87d966df3fb25d01054ed8b303f17d%22%7D; expires=Mon, 23-Nov-2026 17:17:30 GMT; Max-Age=315360000; path=/; domain=.3bmeteo.com\r\n"
		"Set-Cookie: sn=%7B%22id%22%3A%223a87d966df3fb25d01054ed8b303f17d%22%2C%22fl%22%3A%5B%7B%22key%22%3A4074%2C%22label%22%3A%22Milano%22%7D%2C%7B%22key%22%3A5913%2C%22label%22%3A%22Roma%22%7D%2C%7B%22key%22%3A4579%2C%22label%22%3A%22Napoli%22%7D%5D%7D; expires=Mon, 23-Nov-2026 17:17:30 GMT; Max-Age=315360000; path=/; domain=.3bmeteo.com\r\n"
		"Vary: User-Agent\r\n"
		"Content-Type: text/html; charset=ISO-8859-1\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.3bmeteo.com/");

	ASSERT_EQ(3, httpMime.getCookies().size());
	httpMime.verifyCookie("ci_session", "ci_session=a%3A4%3A%7Bs%3A10%3A%22session_id%22%3Bs%3A32%3A%22dc95f0310ff19a2ad18e4e8324596100%22%3Bs%3A10%3A%22ip_address%22%3Bs%3A13%3A%22192.168.0.252%22%3Bs%3A10%3A%22user_agent%22%3Bs%3A11%3A%22curl%2F7.47.1%22%3Bs%3A13%3A%22last_activity%22%3Bs%3A10%3A%221480094250%22%3B%7D80d8db7afd9f2879b03e67333d6089f6", "/", "3bmeteo.com", true);
	httpMime.verifyCookie("", "=0", "/", "3bmeteo.com");
	httpMime.verifyCookie("sn", "sn=%7B%22id%22%3A%223a87d966df3fb25d01054ed8b303f17d%22%2C%22fl%22%3A%5B%7B%22key%22%3A4074%2C%22label%22%3A%22Milano%22%7D%2C%7B%22key%22%3A5913%2C%22label%22%3A%22Roma%22%7D%2C%7B%22key%22%3A4579%2C%22label%22%3A%22Napoli%22%7D%5D%7D", "/", "3bmeteo.com");
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadColon) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Date: Fri, 25 Nov 2016 16:54:48 GMT\r\n"
		"Server: Apache\r\n"
		"Content-Type: text/html;charset=UTF-8\r\n"
		"Content-Language: en\r\n"
		"Content-Length: 45799\r\n"
		"X-Varnish: 991702\r\n"
		"Vary: Accept-Encoding\r\n"
		"Set-Cookie: RTT_TIMESTAMP=false,1480092888.525,1480092888666,1480092888728,1480092888.873; expires: Session; path=/home\r\n"
		"Connection: close\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.vlive.tv/");

	ASSERT_EQ(1, httpMime.getCookies().size());
	httpMime.verifyCookie("RTT_TIMESTAMP", "RTT_TIMESTAMP=false,1480092888.525,1480092888666,1480092888728,1480092888.873", "/home");
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadTime) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Date: Sat, 26 Nov 2016 22:15:17 GMT\r\n"
		"Server: Apache\r\n"
		"Expires: Thu, 19 Nov 1981 08:52:00 GMT\r\n"
		"Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n"
		"Pragma: no-cache\r\n"
		"Set-Cookie: __cf_mob_redir=0; Expires=time()+60*60*24*365; Domain=m.usadosbr.com; Path=/;\r\n"
		"Set-Cookie: PHPSESSID=8mn9l5hva8enpplmjfjhct91p4; path=/\r\n"
		"Set-Cookie: redirMobile=TRUE; expires=Sun, 27-Nov-2016 22:15:17 GMT; path=/\r\n"
		"Set-Cookie: cookieCliente=081517111626941; path=/; domain=usadosbr.com\r\n"
		"Vary: Accept-Encoding\r\n"
		"cache-control: public\r\n"
		"Connection: close\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.usadosbr.com/");

	ASSERT_EQ(4, httpMime.getCookies().size());
	httpMime.verifyCookie("__cf_mob_redir", "__cf_mob_redir=0", "/", "m.usadosbr.com");
	httpMime.verifyCookie("PHPSESSID", "PHPSESSID=8mn9l5hva8enpplmjfjhct91p4", "/");
	httpMime.verifyCookie("redirMobile", "redirMobile=TRUE", "/");
	httpMime.verifyCookie("cookieCliente", "cookieCliente=081517111626941", "/", "usadosbr.com");
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadInvalid) {
	char httpResponse[] =
		"HTTP/1.1 302 Found\r\n"
		"Server: nginx/1.4.6 (Ubuntu)\r\n"
		"Date: Sat, 26 Nov 2016 01:22:34 GMT\r\n"
		"Content-Type: text/plain; charset=utf-8\r\n"
		"Content-Length: 28\r\n"
		"Connection: keep-alive\r\n"
		"X-Powered-By: Express\r\n"
		"Set-Cookie: furlaCountry=dk; Max-Age=2592; Path=/; Expires=Sat, 26 Nov 2016 02:05:46 GMT; HttpOnly\r\n"
		"Set-Cookie: furlaLoc=55.6761%2C12.5683; Max-Age=90000000000000; Path=/; Expires=Invalid Date\r\n"
		"Set-Cookie: session=eyJwYXNzcG9ydCI6e319; path=/; httponly\r\n"
		"Set-Cookie: session.sig=jQiK_vfGB_ybjp895cSGHtj1MUw; path=/; httponly\r\n"
		"Location: /dk/en\r\n"
		"Vary: Accept, Accept-Encoding\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.furla.com/");

	ASSERT_EQ(4, httpMime.getCookies().size());
	httpMime.verifyCookie("furlaCountry", "furlaCountry=dk", "/", "", true);
	httpMime.verifyCookie("furlaLoc", "furlaLoc=55.6761%2C12.5683", "/", "");
	httpMime.verifyCookie("session", "session=eyJwYXNzcG9ydCI6e319", "/", "", true);
	httpMime.verifyCookie("session.sig", "session.sig=jQiK_vfGB_ybjp895cSGHtj1MUw", "/", "", true);
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadWeek) {
	char httpResponse[] =
		"HTTP/1.1 500 Internal Server Error\r\n"
		"Date: Sat, 26 Nov 2016 07:11:36 GMT\r\n"
		"Content-Type: text/html\r\n"
		"Connection: keep-alive\r\n"
		"Server: Apache\r\n"
		"X-Frame-Options: SAMEORIGIN\r\n"
		"Set-Cookie: SESSION_ID=b88677644df8fbb69da980acc519919a; domain=.mydomain.com; path=/\r\n"
		"Set-Cookie: Currency=USD; domain=mydomain.com; path=/; expires=1w\r\n"
		"Vary: Accept-Encoding\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.mydomain.com/");

	ASSERT_EQ(2, httpMime.getCookies().size());
	httpMime.verifyCookie("SESSION_ID", "SESSION_ID=b88677644df8fbb69da980acc519919a", "/", "mydomain.com");
	httpMime.verifyCookie("Currency", "Currency=USD", "/", "mydomain.com");
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadTimezoneTypo) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Server: Tengine\r\n"
		"Content-Type: text/html;charset=utf-8\r\n"
		"Content-Length: 218375\r\n"
		"Connection: keep-alive\r\n"
		"Vary: Accept-Encoding\r\n"
		"Date: Fri, 25 Nov 2016 23:56:04 GMT\r\n"
		"Vary: Accept-Encoding\r\n"
		"Cache-Control: must-revalidate,no-cache\r\n"
		"Content-Language: zh-CN\r\n"
		"Set-Cookie: CXSESSID=3f6f6601de3e07a56ce4741a56874794;path=/;domain=59pi.com\r\n"
		"Set-Cookie: dom_id=5;expires=Sun, 25-Nov-2018 23:56:04 GTM;path=/;domain=59pi.com\r\n"
		"Set-Cookie: index_ad_close=2;expires=Sat, 26-Nov-2016 00:56:04 GTM;path=/;domain=59pi.com\r\n"
		"Set-Cookie: dom_id=5;expires=Sun, 25-Nov-2018 23:56:04 GTM;path=/;domain=59pi.com\r\n"
		"Set-Cookie: dom_id=5;expires=Sun, 25-Nov-2018 23:56:04 GTM;path=/;domain=59pi.com\r\n"
		"Set-Cookie: dom_id=5;expires=Sun, 25-Nov-2018 23:56:04 GTM;path=/;domain=59pi.com\r\n"
		"Via: cache4.l2et15[163,200-0,M], cache12.l2et15[163,0], kunlun10.cn180[186,200-0,M], kunlun4.cn180[187,0]\r\n"
		"X-Cache: MISS TCP_MISS dirn:-2:-2\r\n"
		"X-Swift-SaveTime: Fri, 25 Nov 2016 23:56:04 GMT\r\n"
		"X-Swift-CacheTime: 0\r\n"
		"Timing-Allow-Origin: *\r\n"
		"EagleId: 3ad8110414801181642343913e\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.59pi.com/");

	ASSERT_EQ(3, httpMime.getCookies().size());
	httpMime.verifyCookie("CXSESSID", "CXSESSID=3f6f6601de3e07a56ce4741a56874794", "/", "59pi.com");
	httpMime.verifyCookie("dom_id", "dom_id=5", "/", "59pi.com");
	httpMime.verifyCookie("index_ad_close", "index_ad_close=2", "/", "59pi.com");
}

TEST(HttpMimeTest, SetCookieExpiresFormatBadTimezoneAZT) {
	char httpResponse[] =
		"HTTP/2.0 200\r\n"
		"date:Sat, 26 Nov 2016 02:59:50 GMT\r\n"
		"content-type:text/html; charset=UTF-8\r\n"
		"set-cookie:__cfduid=d1d22d701704362c9e141a7291e9dc85d1480129189; expires=Sun, 26-Nov-17 02:59:49 GMT; path=/; domain=.from.ae; HttpOnly\r\n"
		"x-frame-options:SAMEORIGIN\r\n"
		"x-content-type-options:nosniff\r\n"
		"x-xss-protection:1; mode=block\r\n"
		"x-ua-compatible:IE=Edge,chrome=1\r\n"
		"x-php-processing-time:0.942\r\n"
		"vary:Accept-Encoding\r\n"
		"age:0\r\n"
		"set-cookie:frontend=63d88f00441944198d9358a15d1a430d; expires=Sat, 26-Nov-2016 04:29:50 AZT; path=/; domain=.www.from.ae; httponly\r\n"
		"x-debug-website:www.from.ae\r\n"
		"x-debug-country:DK\r\n"
		"x-varnish-processing-time:0.990\r\n"
		"server:cloudflare-nginx\r\n"
		"cf-ray:307a162b6edd3c95-CPH\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "https://www.from.ae/en/");

	ASSERT_EQ(2, httpMime.getCookies().size());
	httpMime.verifyCookie("__cfduid", "__cfduid=d1d22d701704362c9e141a7291e9dc85d1480129189", "/", "from.ae", true);
	httpMime.verifyCookie("frontend", "frontend=63d88f00441944198d9358a15d1a430d", "/", "www.from.ae", true);
}

TEST(HttpMimeTest, SetCookieExpiresExpired) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Cache-Control: private,no-cache, no-store, must-revalidate\r\n"
		"Pragma: no-cache\r\n"
		"Content-Length: 0\r\n"
		"Content-Type: text/html; charset=UTF-8\r\n"
		"Server: Microsoft-IIS/8.0\r\n"
		"Set-Cookie: L=US; expires=Mon, 05 Dec 2016 17:22:27 Mountain Standard Time; path=/;\r\n"
		"Set-Cookie: L=US; expires=Sun, 26-Nov-2017 00:22:27 GMT; Max-Age=31536000; path=/\r\n"
		"Set-Cookie: cb_FirstVisit=2016%2F11%2F25+17%3A22%3A27; expires=Thu, 25-Nov-2021 00:22:27 GMT; Max-Age=157680000; path=/\r\n"
		"Set-Cookie: cb_Referrer=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: brand=+; expires=Mon, 26-Dec-2016 00:22:27 GMT; Max-Age=2592000; path=/\r\n"
		"Set-Cookie: ASP.NET_SessionId=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/; domain=www.lawdepot.com\r\n"
		"Set-Cookie: ASP.NET_SessionId=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: UID=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: UID=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/; domain=www.lawdepot.com\r\n"
		"Set-Cookie: LoginID=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: LoginID=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/; domain=www.lawdepot.com\r\n"
		"Set-Cookie: pricing_region=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: pricing_region=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/; domain=www.lawdepot.com\r\n"
		"Set-Cookie: free_account=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/\r\n"
		"Set-Cookie: free_account=deleted; expires=Thu, 01-Jan-1970 00:00:01 GMT; Max-Age=0; path=/; domain=www.lawdepot.com\r\n"
		"X-VsTrckRan: 1\r\n"
		"Set-Cookie: visitorID=86394794; expires = Thu, 25-Nov-2021 00:22:27 GMT; path = / ;\r\n"
		"Set-Cookie: visit=direct:; expires = Sat, 26-Nov-2016 00:52:27 GMT; path = / ;\r\n"
		"P3P: CP=\"IDC DSP COR ADM DEVi TAIi PSA PSD IVAi IVDi CONi HIS OUR IND CNT\"\r\n"
		"CST: 1123\r\n"
		"X-UA-Compatible: IE=edge\r\n"
		"Date: Sat, 26 Nov 2016 00:22:27 GMT\r\n"
		"Set-Cookie: visid_incap_648868=4ynkNt4nQKKNVnETYoefzMLVOFgAAAAAQUIPAAAAAABYx9lSFWUKC0NOTKHuVVN5; expires=Sat, 25 Nov 2017 14:48:38 GMT; path=/; Domain=.lawdepot.com\r\n"
		"Set-Cookie: incap_ses_472_648868=gXdwAOiWPQX7jLx4meGMBsPVOFgAAAAApBQPbRa7OdgZV0WqJWUP2g==; path=/; Domain=.lawdepot.com\r\n"
		"Set-Cookie: ___utmvmXEuIKXs=xscHtJOFkSw; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvaXEuIKXs=cEK^AyUTl; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvbXEuIKXs=mZd\r\n"
		"    XGPOvala: PtX; path=/; Max-Age=900\r\n"
		"X-Iinfo: 9-92505813-92505814 NNNN CT(143 -1 0) RT(1480119746656 0) q(0 0 1 0) r(6 6) U6\r\n"
		"X-CDN: Incapsula\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.lawdepot.com/");

	ASSERT_EQ(10, httpMime.getCookies().size());
	httpMime.verifyCookie("L", "L=US", "/");
	httpMime.verifyCookie("cb_FirstVisit", "cb_FirstVisit=2016%2F11%2F25+17%3A22%3A27", "/");
	httpMime.verifyCookie("brand", "brand=+", "/");
	httpMime.verifyCookie("visitorID", "visitorID=86394794", "/");
	httpMime.verifyCookie("visit", "visit=direct:", "/");
	httpMime.verifyCookie("visid_incap_648868", "visid_incap_648868=4ynkNt4nQKKNVnETYoefzMLVOFgAAAAAQUIPAAAAAABYx9lSFWUKC0NOTKHuVVN5", "/", "lawdepot.com");
	httpMime.verifyCookie("incap_ses_472_648868", "incap_ses_472_648868=gXdwAOiWPQX7jLx4meGMBsPVOFgAAAAApBQPbRa7OdgZV0WqJWUP2g==", "/", "lawdepot.com");
	httpMime.verifyCookie("___utmvmXEuIKXs", "___utmvmXEuIKXs=xscHtJOFkSw", "/");
	httpMime.verifyCookie("___utmvaXEuIKXs", "___utmvaXEuIKXs=cEK^AyUTl", "/");
	httpMime.verifyCookie("___utmvbXEuIKXs", "___utmvbXEuIKXs=mZd\r\n    XGPOvala: PtX", "/");

}

TEST(HttpMimeTest, SetCookieExpiresFormatBadTimezoneOffset) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Date: Sat, 26 Nov 2016 18:55:53 GMT\r\n"
		"Server: Apache/2.2.22\r\n"
		"X-Powered-By: PHP/5.4.41-0+deb7u1\r\n"
		"Set-Cookie: Session=ID=29875323261120164219555318817648254&domain=com;domain=.ldlc-pro.com; expires=Tue, 27 Dec 2016 19:55:53 +0100;path=/\r\n"
		"Vary: Accept-Encoding\r\n"
		"Content-Type: text/html\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.ldlc-pro.com/");

	ASSERT_EQ(1, httpMime.getCookies().size());
	httpMime.verifyCookie("Session", "Session=ID=29875323261120164219555318817648254&domain=com", "/", "ldlc-pro.com");
}

TEST(HttpMimeTest, SetCookieExpiresFormatAscTime) {
	char httpResponse[] =
		"HTTP/1.1 302 Moved Temporarily\r\n"
		"Server: Apache/2.0.64 (Unix)\r\n"
		"Location: http://www.barrons.com/?r=1\r\n"
		"Content-Type: text/html; charset=iso-8859-1\r\n"
		"Content-Length: 0\r\n"
		"Expires: Fri, 25 Nov 2016 19:18:32 GMT\r\n"
		"Cache-Control: max-age=0, no-cache, no-store\r\n"
		"Pragma: no-cache\r\n"
		"Date: Fri, 25 Nov 2016 19:18:32 GMT\r\n"
		"Connection: keep-alive\r\n"
		"Set-Cookie: djcs_route=3a737dc8-f63f-41ee-a6a6-13cbb8439f03; domain=.barrons.com; path=/; Expires=Mon Nov 23 14:18:31 2026; max-age=315360000\r\n"
		"Set-Cookie: djcs_int=Y29udGluZW50PUVVJnNpZz1iYzU0OGNmZGEyMjA1ZTBlY2U5YWNiMzhiOWVkOTQ1YyZpcD0xODguMTc2LjQ4LjI1NCUyQysyLjE2LjIxNy4xMDImY291bnRyeV9jb2RlPURL; domain=.barrons.com; path=/\r\n"
		"Set-Cookie: wsjregion=na%2cus; domain=.barrons.com; path=/; expires=Mon 01 Jan 2035 00:00:00 GMT\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.barrons.com/public/page/geo");

	ASSERT_EQ(3, httpMime.getCookies().size());
	httpMime.verifyCookie("djcs_route", "djcs_route=3a737dc8-f63f-41ee-a6a6-13cbb8439f03", "/", "barrons.com");
	httpMime.verifyCookie("djcs_int", "djcs_int=Y29udGluZW50PUVVJnNpZz1iYzU0OGNmZGEyMjA1ZTBlY2U5YWNiMzhiOWVkOTQ1YyZpcD0xODguMTc2LjQ4LjI1NCUyQysyLjE2LjIxNy4xMDImY291bnRyeV9jb2RlPURL", "/", "barrons.com");
	httpMime.verifyCookie("wsjregion", "wsjregion=na%2cus", "/", "barrons.com");

}

TEST(HttpMimeTest, SetCookieMaxAgeZero) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0\r\n"
		"Pragma: no-cache\r\n"
		"Content-Length: 0\r\n"
		"Content-Type: text/html\r\n"
		"Expires: Thu, 19 Nov 1981 08:52:00 GMT\r\n"
		"Set-Cookie: PHPSESSID=iull787l2g108u2t3gnhrdnp81; path=/; HttpOnly\r\n"
		"Set-Cookie: too_user=%2BfkOkj4w4rI; expires=Sat, 26-Nov-2016 06:16:23 GMT; Max-Age=0\r\n"
		"Server: IIS\r\n"
		"X-Powered-By: WAF/2.0\r\n"
		"Set-Cookie: safedog-flow-item=05291A2AAF3F4BEAD675A8148D837FA5; expires=Sat, 26-Nov-2016 16:00:23 GMT; domain=panelook.com; path=/\r\n"
		"Date: Sat, 26 Nov 2016 06:16:23 GMT\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://www.panelook.com/");

	ASSERT_EQ(2, httpMime.getCookies().size());
	httpMime.verifyCookie("PHPSESSID", "PHPSESSID=iull787l2g108u2t3gnhrdnp81", "/", "", true);
	httpMime.verifyCookie("safedog-flow-item", "safedog-flow-item=05291A2AAF3F4BEAD675A8148D837FA5", "/", "panelook.com");
}

TEST(HttpMimeTest, SetCookieEmptySemicolon) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Content-Type: text/html;charset=UTF-8\r\n"
		"Server: Microsoft-IIS/8.0\r\n"
		"Set-Cookie: CFID=120421232; Expires=Sun, 27-Nov-2016 08:58:32 GMT; Path=/; Secure; HttpOnly\r\n"
		"Set-Cookie: CFTOKEN=1a391de588a50a62-3E92F4DD-155D-A808-00D18B6F3D258A09; Expires=Sun, 27-Nov-2016 08:58:32 GMT; Path=/; Secure; HttpOnly\r\n"
		"Set-Cookie: CFID=120421232; Expires=session; domain=turners.com; Path=/; ;SECURE; HttpOnly;\r\n"
		"Set-Cookie: CFTOKEN=1a391de588a50a62-3E92F4DD-155D-A808-00D18B6F3D258A09; Expires=session; domain=turners.com; Path=/; ;SECURE; HttpOnly;\r\n"
		"Set-Cookie: CFID=120421232; Expires=session; domain=.turners.com; Path=/; ;SECURE; HttpOnly;\r\n"
		"Set-Cookie: CFTOKEN=1a391de588a50a62-3E92F4DD-155D-A808-00D18B6F3D258A09; Expires=session; domain=.turners.com; Path=/; ;SECURE; HttpOnly;\r\n"
		"X-Frame-Options: SAMEORIGIN\r\n"
		"Date: Sat, 26 Nov 2016 08:58:32 GMT\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "https://www.turners.com/");

	ASSERT_EQ(2, httpMime.getCookies().size());
	httpMime.verifyCookie("CFID", "CFID=120421232", "/", "turners.com", true, true);
	httpMime.verifyCookie("CFTOKEN", "CFTOKEN=1a391de588a50a62-3E92F4DD-155D-A808-00D18B6F3D258A09", "/", "turners.com", true, true);
}

TEST(HttpMimeTest, SetCookieDuplicate) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"Content-Type: text/html;charset=UTF-8\r\n"
		"Expires: Thu, 01 Dec 2016 14:25:57 GMT\r\n"
		"Server: Microsoft-IIS/8.5\r\n"
		"Set-Cookie: CFID=77593661; Expires=session; domain=tennisexpress.com; Path=/\r\n"
		"Set-Cookie: CFTOKEN=95321df28f596a4-4A48A47D-163C-D91E-2A64461AD518F8B8; Expires=session; domain=tennisexpress.com; Path=/\r\n"
		"Set-Cookie: CFID=77593661; Expires=session; domain=.tennisexpress.com; Path=/\r\n"
		"Set-Cookie: CFTOKEN=95321df28f596a4-4A48A47D-163C-D91E-2A64461AD518F8B8; Expires=session; domain=.tennisexpress.com; Path=/\r\n"
		"Set-Cookie: CFID=77593661; Expires=session; domain=www.tennisexpress.com; Path=/\r\n"
		"Set-Cookie: CFTOKEN=95321df28f596a4-4A48A47D-163C-D91E-2A64461AD518F8B8; Expires=session; domain=www.tennisexpress.com; Path=/\r\n"
		"Set-Cookie: WEB_MINING_ID=550237228; Expires=Sun, 27-Nov-2016 14:25:57 GMT; Path=/\r\n"
		"Set-Cookie: REF=; Max-Age=86400; Path=/\r\n"
		"Set-Cookie: REF=tennisexpress%2Ecom; Path=/\r\n"
		"Set-Cookie: USERNAME=; Path=/\r\n"
		"Set-Cookie: PASSWORD=; Path=/\r\n"
		"Set-Cookie: CUSTOMER_ID=; Path=/\r\n"
		"Set-Cookie: EMAIL=; Path=/\r\n"
		"Set-Cookie: FIRST_NAME=; Path=/\r\n"
		"Set-Cookie: LAST_NAME=; Path=/\r\n"
		"Set-Cookie: WEB_MINING_ID=550237228; Path=/\r\n"
		"Set-Cookie: WEB_MINING_ID=550237228; Path=/\r\n"
		"Set-Cookie: WEB_MINING_ID=550237228; Path=/\r\n"
		"Set-Cookie: REF=tennisexpress%2Ecom; Path=/\r\n"
		"Set-Cookie: REF=tennisexpress%2Ecom; Path=/\r\n"
		"Set-Cookie: REF=tennisexpress%2Ecom; Path=/\r\n"
		"Set-Cookie: USERNAME=; Path=/\r\n"
		"Set-Cookie: USERNAME=; Path=/\r\n"
		"Set-Cookie: USERNAME=; Path=/\r\n"
		"Set-Cookie: PASSWORD=; Path=/\r\n"
		"Set-Cookie: PASSWORD=; Path=/\r\n"
		"Set-Cookie: PASSWORD=; Path=/\r\n"
		"Set-Cookie: CUSTOMER_ID=; Path=/\r\n"
		"Set-Cookie: CUSTOMER_ID=; Path=/\r\n"
		"Set-Cookie: CUSTOMER_ID=; Path=/\r\n"
		"Set-Cookie: EMAIL=; Path=/\r\n"
		"Set-Cookie: EMAIL=; Path=/\r\n"
		"Set-Cookie: EMAIL=; Path=/\r\n"
		"Set-Cookie: FIRST_NAME=; Path=/\r\n"
		"Set-Cookie: FIRST_NAME=; Path=/\r\n"
		"Set-Cookie: FIRST_NAME=; Path=/\r\n"
		"Set-Cookie: LAST_NAME=; Path=/\r\n"
		"Set-Cookie: LAST_NAME=; Path=/\r\n"
		"Set-Cookie: LAST_NAME=; Path=/\r\n"
		"Date: Sat, 26 Nov 2016 14:25:57 GMT\r\n"
		"Set-Cookie: visid_incap_670380=+U+c8ZWjT4yHQNsmNM8hE3SbOVgAAAAAQUIPAAAAAACqFH+8EPbQn85KN2H8ZsOM; expires=Sun, 26 Nov 2017 10:53:18 GMT; path=/; Domain=.tennisexpress.com\r\n"
		"Set-Cookie: incap_ses_452_670380=DC6aIjek3DS/45N15dNFBnWbOVgAAAAAwj9soGXIGC4M2t1vU/Naxw==; path=/; Domain=.tennisexpress.com\r\n"
		"Set-Cookie: ___utmvmIEulKEi=XIIYDOCCRYQ; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvaIEulKEi=tGrJPck; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvbIEulKEi=bZg\r\n"
		"    XPvOyale: ptN; path=/; Max-Age=900\r\n"
		"X-Iinfo: 9-59114179-59114180 NNNN CT(107 -1 0) RT(1480170356788 1) q(0 0 1 0) r(3 3) U5\r\n"
		"X-CDN: Incapsula\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://tennisexpress.com");

	ASSERT_EQ(15, httpMime.getCookies().size());
	httpMime.verifyCookie("CFID", "CFID=77593661", "/", "www.tennisexpress.com");
	httpMime.verifyCookie("CFTOKEN", "CFTOKEN=95321df28f596a4-4A48A47D-163C-D91E-2A64461AD518F8B8", "/", "www.tennisexpress.com");
	httpMime.verifyCookie("WEB_MINING_ID", "WEB_MINING_ID=550237228", "/");
	httpMime.verifyCookie("REF", "REF=tennisexpress%2Ecom", "/");
	httpMime.verifyCookie("USERNAME", "USERNAME=", "/");
	httpMime.verifyCookie("PASSWORD", "PASSWORD=", "/");
	httpMime.verifyCookie("CUSTOMER_ID", "CUSTOMER_ID=", "/");
	httpMime.verifyCookie("EMAIL", "EMAIL=", "/");
	httpMime.verifyCookie("FIRST_NAME", "FIRST_NAME=", "/");
	httpMime.verifyCookie("LAST_NAME", "LAST_NAME=", "/");
	httpMime.verifyCookie("visid_incap_670380", "visid_incap_670380=+U+c8ZWjT4yHQNsmNM8hE3SbOVgAAAAAQUIPAAAAAACqFH+8EPbQn85KN2H8ZsOM", "/", "tennisexpress.com");
	httpMime.verifyCookie("incap_ses_452_670380", "incap_ses_452_670380=DC6aIjek3DS/45N15dNFBnWbOVgAAAAAwj9soGXIGC4M2t1vU/Naxw==", "/", "tennisexpress.com");
	httpMime.verifyCookie("___utmvmIEulKEi", "___utmvmIEulKEi=XIIYDOCCRYQ", "/");
	httpMime.verifyCookie("___utmvaIEulKEi", "___utmvaIEulKEi=tGrJPck", "/");
	httpMime.verifyCookie("___utmvbIEulKEi", "___utmvbIEulKEi=bZg\r\n    XPvOyale: ptN", "/");
}

TEST(HttpMimeTest, SetCookieMultipleLine) {
	char httpResponse[] =
		"HTTP/1.1 200 OK\r\n"
		"Cache-Control: private\r\n"
		"Content-Length: 6687\r\n"
		"Content-Type: text/html; charset=utf-8\r\n"
		"Server: Microsoft-IIS/8.5\r\n"
		"X-AspNetMvc-Version: 3.0\r\n"
		"X-AspNet-Version: 4.0.30319\r\n"
		"X-Powered-By: ASP.NET\r\n"
		"Date: Sat, 26 Nov 2016 12:28:30 GMT\r\n"
		"Set-Cookie: visid_incap_780362=ummcL4XfSPqPE6kzQWznBwGAOVgAAAAAQUIPAAAAAADFlgKMS3kveaymHYa2wup3; expires=Sat, 25 Nov 2017 13:42:18 GMT; path=/; Domain=.138.com\r\n"
		"Set-Cookie: nlbi_780362=Vxv4X1JzkTQAR/rHbnjLsgAAAADxUw2DWAG4Ije6+uOVG0N8; path=/; Domain=.138.com\r\n"
		"Set-Cookie: incap_ses_543_780362=39xOCBhzFCWSlPLUqB+JBwGAOVgAAAAAkwB11WvDUCsxFSM3MZ5mWQ==; path=/; Domain=.138.com\r\n"
		"Set-Cookie: ___utmvmLouNZtS=RYAMSrtAGMM; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvaLouNZtS=VjM^AZrsi; path=/; Max-Age=900\r\n"
		"Set-Cookie: ___utmvbLouNZtS=ZZQ\r\n"
		"    XZKOgalf: ztw; path=/; Max-Age=900\r\n"
		"X-Iinfo: 10-40738261-40738262 NNNN CT(205 -1 0) RT(1480163329145 0) q(0 0 2 0) r(5 5) U6\r\n"
		"X-CDN: Incapsula\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://138.com");

	ASSERT_EQ(6, httpMime.getCookies().size());
	httpMime.verifyCookie("visid_incap_780362", "visid_incap_780362=ummcL4XfSPqPE6kzQWznBwGAOVgAAAAAQUIPAAAAAADFlgKMS3kveaymHYa2wup3", "/", "138.com");
	httpMime.verifyCookie("nlbi_780362", "nlbi_780362=Vxv4X1JzkTQAR/rHbnjLsgAAAADxUw2DWAG4Ije6+uOVG0N8", "/", "138.com");
	httpMime.verifyCookie("incap_ses_543_780362", "incap_ses_543_780362=39xOCBhzFCWSlPLUqB+JBwGAOVgAAAAAkwB11WvDUCsxFSM3MZ5mWQ==", "/", "138.com");
	httpMime.verifyCookie("___utmvmLouNZtS", "___utmvmLouNZtS=RYAMSrtAGMM", "/");
	httpMime.verifyCookie("___utmvaLouNZtS", "___utmvaLouNZtS=VjM^AZrsi", "/");
	httpMime.verifyCookie("___utmvbLouNZtS", "___utmvbLouNZtS=ZZQ\r\n    XZKOgalf: ztw", "/");

	SafeBuf sb;
	httpMime.addCookieHeader("http://138.com", &sb);
	EXPECT_STREQ("Cookie: "
	             "___utmvaLouNZtS=VjM^AZrsi;"
	             "___utmvbLouNZtS=ZZQXZKOgalf: ztw;"
	             "___utmvmLouNZtS=RYAMSrtAGMM;"
	             "incap_ses_543_780362=39xOCBhzFCWSlPLUqB+JBwGAOVgAAAAAkwB11WvDUCsxFSM3MZ5mWQ==;"
	             "nlbi_780362=Vxv4X1JzkTQAR/rHbnjLsgAAAADxUw2DWAG4Ije6+uOVG0N8;"
	             "visid_incap_780362=ummcL4XfSPqPE6kzQWznBwGAOVgAAAAAQUIPAAAAAADFlgKMS3kveaymHYa2wup3;"
	             "\r\n", sb.getBufStart());

	sb.reset();

	httpMime.addCookieHeader("http://www.138.com", &sb);
	EXPECT_STREQ("Cookie: "
	             "incap_ses_543_780362=39xOCBhzFCWSlPLUqB+JBwGAOVgAAAAAkwB11WvDUCsxFSM3MZ5mWQ==;"
	             "nlbi_780362=Vxv4X1JzkTQAR/rHbnjLsgAAAADxUw2DWAG4Ije6+uOVG0N8;"
	             "visid_incap_780362=ummcL4XfSPqPE6kzQWznBwGAOVgAAAAAQUIPAAAAAADFlgKMS3kveaymHYa2wup3;"
	             "\r\n", sb.getBufStart());
}

TEST(HttpMimeTest, SetCookieMultiplePath) {
	char httpResponse[] =
		"HTTP/1.1 301 Moved Permanently\r\n"
		"Server: nginx/1.9.13\r\n"
		"Date: Fri, 25 Nov 2016 17:56:45 GMT\r\n"
		"Content-Type: text/html; charset=windows-1251\r\n"
		"Connection: keep-alive\r\n"
		"Expires: Thu, 01 Jan 1970 00:00:00 GMT\r\n"
		"X-Elapsed: 0.0465\r\n"
		"Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
		"Pragma: no-cache\r\n"
		"Set-Cookie: advref=typein:; expires=Fri, 27 Jan 2017 17:56:45 GMT; path=/; domain=.220-volt.ru\r\n"
		"Set-Cookie: advref_date=1480096605; expires=Fri, 27 Jan 2017 17:56:45 GMT; path=/order/; domain=.220-volt.ru\r\n"
		"Set-Cookie: advref_link=; expires=Fri, 27 Jan 2017 17:56:45 GMT; path=/order/; domain=.220-volt.ru\r\n"
		"Set-Cookie: advref_first=typein:; expires=Fri, 27 Jan 2017 17:56:45 GMT; path=/; domain=.220-volt.ru\r\n"
		"Set-Cookie: client_timestamp=1480096605; expires=Mon, 25 Nov 2019 17:56:45 GMT; path=/; domain=.220-volt.ru\r\n"
		"Set-Cookie: session=1480096605.66301394; expires=Wed, 24 Nov 2021 17:56:45 GMT; path=/; domain=.220-volt.ru\r\n"
		"Last-Modified: Fri, 25 Nov 2016 17:56:45 GMT\r\n"
		"Location: http://www.220-volt.ru/\r\n"
		"\r\n";

	TestHttpMime httpMime(httpResponse, "http://220-volt.ru/");

	ASSERT_EQ(6, httpMime.getCookies().size());
	httpMime.verifyCookie("advref", "advref=typein:", "/", "220-volt.ru");
	httpMime.verifyCookie("advref_date", "advref_date=1480096605", "/order/", "220-volt.ru");
	httpMime.verifyCookie("advref_link", "advref_link=", "/order/", "220-volt.ru");
	httpMime.verifyCookie("advref_first", "advref_first=typein:", "/", "220-volt.ru");
	httpMime.verifyCookie("client_timestamp", "client_timestamp=1480096605", "/", "220-volt.ru");
	httpMime.verifyCookie("session", "session=1480096605.66301394", "/", "220-volt.ru");

	SafeBuf sb;
	httpMime.addCookieHeader("http://220-volt.ru/", &sb);
	EXPECT_STREQ("Cookie: "
	             "advref=typein:;"
	             "advref_first=typein:;"
	             "client_timestamp=1480096605;"
	             "session=1480096605.66301394;"
	             "\r\n", sb.getBufStart());

	sb.reset();

	httpMime.addCookieHeader("http://220-volt.ru/order/", &sb);
	EXPECT_STREQ("Cookie: "
	             "advref=typein:;"
	             "advref_date=1480096605;"
	             "advref_first=typein:;"
	             "advref_link=;"
	             "client_timestamp=1480096605;"
	             "session=1480096605.66301394;"
	             "\r\n", sb.getBufStart());
}