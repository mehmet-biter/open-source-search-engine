#include "gtest/gtest.h"

#include "XmlXPath.h"
#include "Xml.h"
#include "HttpMime.h" // CT_HTML
#include <cstdio>

#define MAX_BUF_SIZE 1024

#define HTML_HEAD_FORMAT "<html><head>%s</head><body></body></html>"

TEST ( XmlXPathTest, Xpath ) {
	char* input_strs[] =  {
		"<meta property='og:site_name' content='my site' />\n"
		"<title>my title content</title>",

		"<meta property='og:site_name' content='my site' />\n"
		"<meta property='og:title' content='my meta title content' />"
	};

	const char* input_xpaths[] = {
		"title/text()",
		"meta[@property='og:title']/@content"
	};

	char* expected_outputs[] = {
		"my title content",
		"my meta title content"
	};

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );

	ASSERT_EQ(sizeof(input_strs)/sizeof(input_strs[0]), sizeof(input_xpaths)/sizeof(input_xpaths[0]));
	ASSERT_EQ(sizeof(input_strs)/sizeof(input_strs[0]), sizeof(expected_outputs)/sizeof(expected_outputs[0]));

	for ( size_t i = 0; i < len; i++ ) {
		char *input_str = input_strs[i];
		const char *input_xpath = input_xpaths[i];
		char *output_str = expected_outputs[i];

		char input[MAX_BUF_SIZE];
		std::sprintf(input, HTML_HEAD_FORMAT, input_str);

		Xml xml;
		ASSERT_TRUE(xml.set(input, strlen(input), 0, 0, CT_HTML));

		char buf[MAX_BUF_SIZE] = {};
		int32_t bufLen = MAX_BUF_SIZE;
		int32_t contentLen = 0;

		XmlXPath xmlXPath(input_xpath);
		ASSERT_TRUE(xmlXPath.getContent(&xml, buf, 180, &contentLen));
		EXPECT_EQ(strlen(output_str), contentLen);
		EXPECT_STREQ(output_str, buf);
	}
}

