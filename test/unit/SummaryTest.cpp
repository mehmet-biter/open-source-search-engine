#include "gtest/gtest.h"

#include "Summary.h"
#include "HttpMime.h" // CT_HTML
#include <cstdio>

#include "Xml.h"
#include "Words.h"
#include "Phrases.h"
#include "Sections.h"
#include "Pos.h"
#include "Query.h"
#include "Url.h"
#include "Matches.h"
#include "Linkdb.h"
#include "Title.h"

#define MAX_BUF_SIZE 1024
#define HTML_FORMAT "<html><head>%s</head><body>%s</body></html>"

static void generateSummary(Summary &summary, char *htmlInput, char *queryStr, char *urlStr) {
	Xml xml;
	ASSERT_TRUE(xml.set(htmlInput, strlen(htmlInput), 0, 0, CT_HTML));

	Words words;
	ASSERT_TRUE(words.set(&xml, true));

	Bits bits;
	ASSERT_TRUE(bits.set(&words, 0));

	Url url;
	url.set(urlStr);

	Sections sections;
	ASSERT_TRUE(sections.set(&words, &bits, &url, "", 0, CT_HTML));

	Query query;
	ASSERT_TRUE(query.set2(queryStr, langEnglish, true));

	LinkInfo linkInfo;
	memset ( &linkInfo , 0 , sizeof(LinkInfo) );
	linkInfo.m_lisize = sizeof(LinkInfo);

	Title title;
	ASSERT_TRUE(title.setTitle(&xml, &words, 80, &query, &linkInfo, &url, NULL, 0, CT_HTML, langEnglish, 0));

	Pos pos;
	ASSERT_TRUE(pos.set(&words));

	Bits bitsForSummary;
	ASSERT_TRUE(bitsForSummary.setForSummary(&words));

	Phrases phrases;
	ASSERT_TRUE(phrases.set(&words, &bits, 0));

	Matches matches;
	matches.setQuery(&query);
	ASSERT_TRUE(matches.set(&words, &phrases, &sections, &bitsForSummary, &pos, &xml, &title, &url, &linkInfo, 0));

	summary.setSummary(&xml, &words, &sections, &pos, &query, 180, 3, 3, 180, &url, &matches, title.getTitle(), title.getTitleLen());
}

TEST (SummaryTest, StripSamePunct) {
	const char *body =
	   "<pre>"
	   "---------------------------------------------------------------------------------\n"
	   "|                      Name                      |       Total Donations        |\n"
	   "---------------------------------------------------------------------------------\n"
	   "| JENNI STANLEY                                  |                       $10.00 |\n"
	   "---------------------------------------------------------------------------------\n"
	   "| CANDRA BUDGE                                   |                       $22.00 |\n"
	   "---------------------------------------------------------------------------------\n"
	   "| JESSE NICLEY                                   |                       $34.00 |\n"
	   "---------------------------------------------------------------------------------\n"
	   "| SHARON YOKLEY                                  |                       $45.00 |\n"
	   "---------------------------------------------------------------------------------\n"
	   "</pre>";

	char input[MAX_BUF_SIZE];
	std::sprintf(input, HTML_FORMAT, "", body);

	Summary summary;
	generateSummary(summary, input, "jesse budge", "http://www.example.com/");

	EXPECT_STREQ("CANDRA BUDGE | $22.00 | … | JESSE NICLEY | $34.00 …", summary.getSummary());
}
