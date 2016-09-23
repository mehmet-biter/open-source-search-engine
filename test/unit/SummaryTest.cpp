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

static void generateSummary( Summary &summary, char *htmlInput, const char *queryStr, const char *urlStr ) {
	Xml xml;
	ASSERT_TRUE(xml.set(htmlInput, strlen(htmlInput), 0, CT_HTML));

	Words words;
	ASSERT_TRUE(words.set(&xml, true));

	Bits bits;
	ASSERT_TRUE(bits.set(&words));

	Url url;
	url.set(urlStr);

	Sections sections;
	ASSERT_TRUE(sections.set(&words, &bits, &url, "", CT_HTML));

	Query query;
	ASSERT_TRUE(query.set2(queryStr, langEnglish, true));

	LinkInfo linkInfo;
	memset ( &linkInfo , 0 , sizeof(LinkInfo) );
	linkInfo.m_lisize = sizeof(LinkInfo);

	Title title;
	ASSERT_TRUE(title.setTitle(&xml, &words, 80, &query, &linkInfo, &url, NULL, 0, CT_HTML, langEnglish));

	Pos pos;
	ASSERT_TRUE(pos.set(&words));

	Bits bitsForSummary;
	ASSERT_TRUE(bitsForSummary.setForSummary(&words));

	Phrases phrases;
	ASSERT_TRUE(phrases.set(&words, &bits));

	Matches matches;
	matches.setQuery(&query);
	ASSERT_TRUE(matches.set(&words, &phrases, &sections, &bitsForSummary, &pos, &xml, &title, &url, &linkInfo));

	summary.setSummary(&xml, &words, &sections, &pos, &query, 180, 3, 3, 180, &url, &matches, title.getTitle(), title.getTitleLen());
}

TEST( SummaryTest, StripSamePunct ) {
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

TEST( SummaryTest, BUGNoEllipsisAdded ) {
	const char *head =
		"<title>Instrument prices by Acme Inc.</title>\n"
		"<meta name=\"description\" content=\"Unorthodox musical instrument value estimation\">\n";

	const char *body =
		"<h1>Unusual saxophone valuation</h1>\n"
		"<p>Looking for knowing how much your saxophone is worth and what an appropriate insurance should be?. We provide that and other relevant information such as procedures, locations and time tables</p>\n"
		"<p>We also provide valuation for other musical instrucments.</p>\n";

	char input[MAX_BUF_SIZE];
	std::sprintf(input, HTML_FORMAT, head, body);

	Summary summary;
	generateSummary(summary, input, "saxophone", "http://www.example.com/");

	/// @todo ALC we're not adding ellipsis here due to lack of space. we should take one less word instead and add ellipsis.
	EXPECT_STREQ( "Unusual saxophone valuation. Looking for knowing how much your saxophone is worth and what an appropriate insurance should be?. We provide that and other relevant information such", summary.getSummary() );
}

TEST( SummaryTest, BUGEllipsisAdded ) {
	const char *body = "Giraffe on rollerblades. Penguin on skateboard. The giraffe is way faster than that plumb bird with pathetic wings.\n";

	char input[MAX_BUF_SIZE];
	std::sprintf(input, "%s", body);

	Summary summary;
	generateSummary(summary, input, "giraffe", "http://www.example.com/");

	/// @todo ALC we're adding ellipsis even with a full sentence.
	EXPECT_STREQ( "Giraffe on rollerblades. Penguin on skateboard. The giraffe is way faster than that plumb bird with pathetic wings.  …", summary.getSummary() );
}

TEST( SummaryTest, DefaultSummary ) {
	const char *head = "<qtitle>f1 doc</qtitle>";
	const char *body = "<p>cucumber</p>\n"
	                   "<a href=\"f3.html\">snegl</a>\n"
	                   "snegl\n";

	char input[MAX_BUF_SIZE];
	std::sprintf(input, HTML_FORMAT, head, body);

	Summary summary;
	generateSummary(summary, input, "banana", "http://www.example.com/");

	EXPECT_STREQ( "cucumber. snegl snegl", summary.getSummary() );
}
