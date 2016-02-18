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

static void generateSummary(Summary *summary, char *htmlInput, char *queryStr, char *urlStr) {
	Xml xml;
	ASSERT_TRUE(xml.set(html, strlen(htmlInput), 0, 0, CT_HTML));

	Words words;
	ASSERT_TRUE(words.set(&xml, true));

	Bits bits;
	ASSERT_TRUE(bits.set(&words, TITLEREC_CURRENT_VERSION, 0));

	Phrases phrases;
	ASSERT_TRUE(phrases.set(&words, &bits, true, false, TITLEREC_CURRENT_VERSION, 0));

	Url url;
	url.set(urlStr);

	Sections sections;
	ASSERT_TRUE(sections.set(&words, &phrases, &bits, &url, 0, "", 0, CT_HTML));

	Query query;
	ASSERT_TRUE(query.set2(queryStr, langEnglish, true));

	LinkInfo linkInfo;

	Title title;
	ASSERT_TRUE(title.setTitle(&xml, &words, 80, &query, &linkInfo, &url, NULL, 0, CT_HTML, langEnglish, 0));

	Pos pos;
	ASSERT_TRUE(pos.set(&words));

	Bits bitsForSummary;
	ASSERT_TRUE(bitsForSummary.setForSummary(&words));

	Matches matches;
	matches.setQuery(&query);
	ASSERT_TRUE(matches.set(&words, &phrases, &sections, &bitsForSummary, &pos, &xml, &title, &url, &linkInfo, 0));

	summary.set(&xml, &words, &sections, &pos, &query, NULL, 180, 3, 3, 180, &url, &matches, title.getTitle(), title.getTitleLen());
}

TEST (SummaryTest, Punctuation) {
	const char *body =
	   "<pre>"
	   "---------------------------------------------------------------------------------"
	   "|                      Name                      |       Total Donations        |"
	   "---------------------------------------------------------------------------------"
	   "| JENNI STANLEY                                  |                       $10.00 |"
	   "---------------------------------------------------------------------------------"
	   "| CANDRA BUDGE                                   |                       $22.00 |"
	   "---------------------------------------------------------------------------------"
	   "| JESSE NICLEY                                   |                       $34.00 |"
	   "---------------------------------------------------------------------------------"
	   "| SHARON YOKLEY                                  |                       $45.00 |"
	   "---------------------------------------------------------------------------------"
	   "</pre>";

	char input[MAX_BUF_SIZE];
	std::sprintf(input, HTML_FORMAT, "", body);

	generateSummary(summary, input, "jesse budge", "http://www.example.com/");

	logf(LOG_INFO, "summary='%.*s'", summary.getSummaryLen(), summary.getSummary());
}
