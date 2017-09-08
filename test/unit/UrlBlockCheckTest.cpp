#include <gtest/gtest.h>

#include "UrlBlockCheck.h"
#include "Url.h"
#include "Conf.h"
#include "Log.h"

TEST(UrlBlockCheckTest, BlockIPUrl) {
	std::vector<std::tuple<const char *, bool, int>> test_cases = {
		std::make_tuple("http://127.0.0.1/", true, EDOCBLOCKEDURLIP),
		std::make_tuple("http://192.168.1.1:3000/", true, EDOCBLOCKEDURLIP),
		/// @todo ALC should these be detected as IP url as well?
//		std::make_tuple("http://2130706433/", true, EDOCBLOCKEDURLIP),
//		std::make_tuple("http://0x7F000001/", true, EDOCBLOCKEDURLIP),
		std::make_tuple("http://www.example.com:3000/", false, 0)
	};

	// store original conf
	Conf ori_conf = g_conf;

	g_conf.m_spiderIPUrl = false;

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		Url url;
		url.set(std::get<0>(*it));

		SCOPED_TRACE(url.getUrl());

		int p_errno = 0;
		EXPECT_EQ(std::get<1>(*it), isUrlBlocked(url, &p_errno));
		EXPECT_EQ(std::get<2>(*it), p_errno);
	}

	// restore g_conf
	g_conf = ori_conf;
}

TEST(UrlBlockCheckTest, BlockCorrupted) {
	std::vector<std::tuple<const char *, bool, int>> test_cases = {
		std::make_tuple("http://www.fineartprinter.de/layout/set/print/content/keyword/Sensorbezogen%20S%01", true, EDOCBLOCKEDURLCORRUPT),
		std::make_tuple("http://charles.forsythe.name/cs/home/-/blogs/spring-mvc-portlet-f%01", true, EDOCBLOCKEDURLCORRUPT),
	    std::make_tuple("http://www.artif-orange.de/beratung.html?L=%2Fproc%2Fself%2Fenviron%00", true, EDOCBLOCKEDURLCORRUPT),
	    std::make_tuple("http://www.iam.kit.edu/cms/emailform.php?id=Q%9B%9Cb%23%C7%95%B3%C3S%0C%A3%BF%13~%B4C4%E3%1F%C1%1F%EA%01%A4%B93%21%ADS%FD%E7D%92%E7-%08%AB%7B%11%F7%A3%DAmKS%17%1F", true, EDOCBLOCKEDURLCORRUPT),
	    std::make_tuple("http://www.hplibrary.org/onebookhp%20", false, 0)
	};

	for (auto it = test_cases.begin(); it != test_cases.end(); ++it) {
		Url url;
		url.set(std::get<0>(*it));

		SCOPED_TRACE(url.getUrl());

		int p_errno = 0;
		EXPECT_EQ(std::get<1>(*it), isUrlBlocked(url, &p_errno));
		EXPECT_EQ(std::get<2>(*it), p_errno);
	}
}