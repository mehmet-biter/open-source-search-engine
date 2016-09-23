#include <gtest/gtest.h>

#include "Xml.h"
#include "HttpMime.h" // CT_HTML
#include <cstdio>

#define MAX_BUF_SIZE 1024

#define HTML_HEAD_FORMAT "<html><head>%s</head><body></body></html>"

TEST( XmlTest, MetaDescription) {
	char* input_strs[] =  {
	    // valid
	    "totally valid description",
	    "“inside special quotes” and outside",

	    // invalid
	    "my \"invalid\" double quote description",
	    "\"someone has quotes\", and nobody else has it"
	    "'my 'invalid' single quote description'",
	    "it's a description",
	    "what is this quote \" doing here?"
	};

	char* format_strs[] = {
	    "<meta name=\"description\" content=\"%s\">",
	    "<meta name=\"description\" content='%s'>",
	    "<meta name=\"description\" content=\"%s\" ng-attr-content=\"{{meta.description}}\">",
	    "<meta name=\"description\" content='%s' ng-attr-content=\"{{meta.description}}\" >",
	    "<meta name=\"description\" ng-attr-content=\"{{meta.description}}\" content=\"%s\">",
	    "<meta name=\"description\" ng-attr-content=\"{{meta.description}}\" content='%s'>",
	    "<meta name=\"description\" content=\"%s\" other-content=\"%s\">",
	    "<meta name=\"description\" content='%s' other-content='%s'>",
	    "<meta content=\"%s\" name=\"description\">",
	    "<meta content='%s' name=\"description\">",
	    "<meta name=\"description\" other-content=\"%s\" content=\"%s\">",
	    "<meta name=\"description\" other-content='%s' content='%s'>"
	};

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	size_t format_len = sizeof( format_strs ) / sizeof( format_strs[0] );

	for ( size_t i = 0; i < len; i++ ) {
		for (size_t j = 0; j < format_len; j++) {
			char *input_str = input_strs[i];

			char desc[MAX_BUF_SIZE];
			std::sprintf(desc, format_strs[j], input_str, input_str);

			char input[MAX_BUF_SIZE];
			std::sprintf(input, HTML_HEAD_FORMAT, desc);

			Xml xml;
			ASSERT_TRUE(xml.set(input, strlen(input), 0, CT_HTML));

			char buf[MAX_BUF_SIZE];
			int32_t bufLen = MAX_BUF_SIZE;
			int32_t contentLen = 0;

			ASSERT_TRUE(xml.getTagContent("name", "description", buf, bufLen, 0, bufLen, &contentLen, false, TAG_META));
			EXPECT_EQ(strlen(input_str), contentLen);
			EXPECT_STREQ(input_str, buf);
		}
	}
}

TEST( XmlTest, MetaDescriptionStripTags) {
	char* input_strs[] =  {
	    "my title<br> my <b>very important</b> text",
	    "Lesser than (<) and greater than (>).",
	    "We shouldn't strip <3 out",
	    "123 < 1234; 1234 > 123",
	    "<p style='text-align: center;'>A color cartoon drawing of a clapping cod fish ( rebus in the danish language for klaptorsk )</p>"
	};

	char* expected_outputs[] = {
	    "my title. my very important text",
	    "Lesser than (<) and greater than (>).",
	    "We shouldn't strip <3 out",
	    "123 < 1234; 1234 > 123",
	    "A color cartoon drawing of a clapping cod fish ( rebus in the danish language for klaptorsk ). "
	};

	char* format_str = "<meta name=\"description\" content=\"%s\">";

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );

	ASSERT_EQ(sizeof(input_strs)/sizeof(input_strs[0]), sizeof(expected_outputs)/sizeof(expected_outputs[0]));

	for ( size_t i = 0; i < len; i++ ) {
		char *input_str = input_strs[i];
		char *output_str = expected_outputs[i];

		char desc[MAX_BUF_SIZE];
		std::sprintf(desc, format_str, input_str, input_str);

		char input[MAX_BUF_SIZE];
		std::sprintf(input, HTML_HEAD_FORMAT, desc);

		Xml xml;
		ASSERT_TRUE(xml.set(input, strlen(input), 0, CT_HTML));

		char buf[MAX_BUF_SIZE];
		int32_t bufLen = MAX_BUF_SIZE;
		int32_t contentLen = 0;

		ASSERT_TRUE(xml.getTagContent("name", "description", buf, bufLen, 0, bufLen, &contentLen, false, TAG_META));
		EXPECT_EQ(strlen(output_str), contentLen);
		EXPECT_STREQ(output_str, buf);
	}
}
