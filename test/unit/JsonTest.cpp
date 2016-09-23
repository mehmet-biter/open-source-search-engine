#include <gtest/gtest.h>

#include "Json.h"
#include "SafeBuf.h"

TEST(JsonTest, ParseValid) {
	Json json;

	const char *json_input = "{\"tags\":[\"Apple Inc.\",\"Symbian\",\"IPad\",\"Music\"],\"summary\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"icon\":\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/ico/apple-touch-icon.png\",\"text\":\"How did Apple rise through the ranks to become the world’s most profitable tech company? As it turns out, good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.For example, take the first MP3 player — MPMan, produced by South Korea-based SaeHan Information Systems. MPMan appeared in 1998, three years before the first iPods were released. As the original pioneer of portable MP3 player technology, SaeHan spent a good deal of time in court negotiating terms of use with various record companies. By 2001, a clear legal precedent was set for MP3 access — allowing Apple to focus less on courtroom proceedings and more on cutting-edge marketing campaigns for their new product."
		"When all else fails, they buy it: While iPads had fan boys salivating in the streets –the technology has been around for decades. One of the most obvious precursors to the iPad is FingerWorks, a finger gesture operated keyboard with a mouse very similar to Apple’s iPad controller. Fingerworks was bought in 2005 by none other than Apple – not surprisingly a couple years before the release of the iPhone and later the iPad.		 Of course, this isn’t to say that Apple doesn’t deserve to be the most valuable tech company in the world – just that innovation isn’t always about being first or best, sometimes, it’s just perception.\",\"stats\":{\"fetchTime\":2069,\"confidence\":\"0.780\"},\"type\":\"article\",\"meta\":{\"twitter\":{\"twitter:creator\":\"@germanny\",\"twitter:domain\":\"OnlineMBA.com\",\"twitter:card\":\"summary\",\"twitter:site\":\"@OnlineMBA_com\"},\"microdata\":{\"itemprop:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\"},\"title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"article:publisher\":\"https://www.facebook.com/OnlineMBAcom\",\"fb:app_id\":\"274667389269609\",\"og\":{\"og:type\":\"article\",\"og:title\":\"3 Ways Apple Actually Innovates - OnlineMBA.com\",\"og:description\":\"Good timing and shrewd planning have played as much of a role as innovative thinking for the Silicon Valley juggernaut.\",\"og:site_name\":\"OnlineMBA.com\",\"og:image\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-featured-150x150.png\",\"og:locale\":\"en_US\",\"og:url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\"}},\"human_language\":\"en\",\"url\":\"http://www.onlinemba.com/blog/3-ways-apple-innovates\",\"title\":\"3 Ways Apple Actually Innovates\",\"textAnalysis\":{\"error\":\"Timeout during text analysis\"},\"html\":\"<div><div class=\\\"image_frame\\\"><img data-blend-adjustment=\\\"http://www.onlinemba.com/wp-content/themes/onlinemba/assets/img/backgrounds/bg.gif\\\" data-blend-mode=\\\"screen\\\" src=\\\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\\\"></img></div><p>How did Apple rise"
		"\",\"supertags\":[{\"id\":856,\"positions\":[[7,12],[41,46],[663,668],[776,781],[1188,1193],[1380,1385],[1645,1650],[1841,1848],[2578,2583],[2856,2863],[2931,2936]],\"name\":\"Apple Inc.\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"1752615\":\"Home computer hardware companies\",\"27841529\":\"Technology companies of the United States\",\"33847259\":\"Publicly traded companies of the United States\",\"15168154\":\"Mobile phone manufacturers\",\"732736\":\"Retail companies of the United States\",\"9300270\":\"Apple Inc.\",\"23568549\":\"Companies based in Cupertino, "
		"California\",\"34056227\":\"Article Feedback 5\",\"37595560\":\"1976 establishments in California\",\"7415072\":\"Networking hardware companies\",\"699547\":\"Computer hardware companies\",\"37191508\":\"Software companies based in the San Francisco Bay Area\",\"855278\":\"Electronics companies\",\"5800057\":\"Steve Jobs\",\"7652766\":\"Display technology companies\",\"14698378\":\"Warrants issued in Hong Kong Stock Exchange\",\"4478067\":\"Portable audio player manufacturers\",\"31628257\":\"Multinational companies headquartered in the United States\",\"732825\":\"Electronics companies of the United States\",\"733759\":\"Computer companies of the United States\",\"6307421\":\"Companies established in 1976\"},\"type\":1,\"senseRank\":1,\"variety\":0.21886792452830184,\"depth\":0.6470588235294117},{\"id\":25686223,\"positions\":[[895,902],[2318,2325]],\"name\":\"Symbian\",\"score\":"
		"0.8,\"contentMatch\":0.9162303664921466,\"categories\":{\"33866248\":\"Nokia platforms\",\"20290726\":\"Microkernel-based operating systems\",\"39774425\":\"ARM operating systems\",\"2148723\":\"Real-time operating systems\",\"953043\":\"Smartphones\",\"10817505\":\"History of software\",\"17862682\":\"Mobile phone operating systems\",\"33569166\":\"Accenture\",\"2150815\":\"Embedded operating systems\",\"22533699\":\"Symbian OS\",\"22280474\":\"Mobile operating systems\"},\"type\":1,\"senseRank\":1,\"variety\":0.6566037735849057,\"depth\":0.6470588235294117},{\"id\":25970423,\"positions\":[[2639,2644],[2771,2775],[2864,2868]],\"name\":\"IPad\",\"score\":0.8,\"contentMatch\":1,\"categories\":{\"33578068\":\"Products introduced "
		"in 2010\",\"18083009\":\"Apple personal digital assistants\",\"23475157\":\"Touchscreen portable media players\",\"30107877\":\"IPad\",\"9301031\":\"Apple Inc. hardware\",\"27765345\":\"IOS (Apple)\",\"26588084\":\"Tablet computers\"},\"type\":1,\"senseRank\":1,\"variety\":0.49056603773584906,\"depth\":0.5882352941176471},{\"id\":18839,\"positions\":[[1945,1950],[2204,2209]],\"name\":\"Music\",\"score\":0.7,\"contentMatch\":1,\"categories\":{\"991222\":\"Performing arts\",\"693016\":\"Entertainment\",\"691484\":\"Music\"},\"type\":1,\"senseRank\":1,\"variety\":0.22264150943396221,\"depth\":0.7058823529411764}],\"media\":[{\"pixelHeight\":350,\"link\":\"http://www.onlinemba.com/wp-content/uploads/2013/02/apple-innovates-invert-350x350.png\",\"primary\":\"true\",\"pixelWidth\":350,\"type\":\"image\"}]}";

	JsonItem *ji = json.parseJsonStringIntoJsonItems(json_input , 0);
	ASSERT_TRUE(ji);

	EXPECT_EQ(JT_OBJECT, ji->m_type);

	/// @todo ALC add more validation on JsonItem

	json.reset();
}

TEST(JsonTest, ParseInvalid) {
	const char *json_inputs[] = {
	    "\"too small\"",
	    "\"categories\":[\"shop\""
	};

	size_t len = sizeof(json_inputs) / sizeof(json_inputs[0]);
	for (size_t i = 0; i < len; i++) {
		Json jp;
		jp.parseJsonStringIntoJsonItems(json_inputs[i], 0);
		JsonItem *ji = jp.getFirstItem();
		ASSERT_FALSE(ji);
	}
}

TEST(JsonTest, EncodeValid) {
	const char *input_strs[] = {
	    "hello\tworld",
	    "apple\norange",
	    "a\"b\\c/",
	    "\b\f\r"
	};

	const char *expected_encoded[] = {
	    "hello\\tworld",
	    "apple\\norange",
	    "a\\\"b\\\\c/",
	    "\\b\\f\\r"
	};

	ASSERT_EQ(sizeof(input_strs)/sizeof(input_strs[0]), sizeof(expected_encoded)/sizeof(expected_encoded[0]));

	size_t len = sizeof(input_strs) / sizeof(input_strs[0]);
	for (size_t i = 0; i < len; i++) {
		SafeBuf safe_buf;
		safe_buf.jsonEncode(input_strs[i]);
		EXPECT_STREQ(expected_encoded[i], safe_buf.getBufStart());
	}
}

TEST(JsonTest, EncodeInvalid) {
	SafeBuf safe_buf;
	safe_buf.jsonEncode("\xc3\xbf\xc2\xb3\x51\x74\xc3\xb1\x77\x0e\x20\x5c\x66\x70\x55\xc3\x88\x28\xc3\x89\xc3\x8d\xc2\xb1\xc3\xa3\xc3\xa2\xc3\xa2\xc2\xb2\x20\x32\x6c\x20\xc3\xb2\x53\x2a\xc3\xad\xc2\xb8\x14\x14\xc2\xb8\x20\x31\x1a\x20\x24\xc3\x8c\x65\xc2\xa3\x0f\x36\xc3\x93\x46\x1f\x62\x05\x20\x60\x2b\xc3\x90\x20\x2e\x2e\x2e");
	EXPECT_STREQ("\xc3\xbf\xc2\xb3\x51\x74\xc3\xb1\x77\x20\x5c\x5c\x66\x70\x55\xc3\x88\x28\xc3\x89\xc3\x8d\xc2\xb1\xc3\xa3\xc3\xa2\xc3\xa2\xc2\xb2\x20\x32\x6c\x20\xc3\xb2\x53\x2a\xc3\xad\xc2\xb8\xc2\xb8\x20\x31\x20\x24\xc3\x8c\x65\xc2\xa3\x36\xc3\x93\x46\x62\x20\x60\x2b\xc3\x90\x20\x2e\x2e\x2e", safe_buf.getBufStart());
}
