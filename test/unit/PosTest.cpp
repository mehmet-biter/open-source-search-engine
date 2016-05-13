#include "gtest/gtest.h"

#include "Pos.h"
#include "Words.h"
#include "Xml.h"
#include "HttpMime.h"
#include <cstdio>

#define MAX_BUF_SIZE 1024

TEST( PosTest, FilterAllCaps ) {
	char *input_strs[] = {
		"ALL YOUR BASES ARE BELONG TO US!!!", "The quick brown FOX jumps over THE lazy dog",
		"PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS",
		"QUIZDELTAGERNE SPISTE JORDBÆR MED FLØDE, MENS CIRKUSKLOVNEN WALTHER SPILLEDE PÅ XYLOFON"
	};

	const char *expected_output[] = {
		"All Your Bases Are Belong To Us!!!", "The quick brown FOX jumps over THE lazy dog",
		"Pack My Box With Five Dozen Liquor Jugs",
		"QUIZDELTAGERNE SPISTE JORDBÆR MED FLØDE, MENS CIRKUSKLOVNEN WALTHER SPILLEDE PÅ XYLOFON"
	};

	ASSERT_EQ( sizeof( input_strs ) / sizeof( input_strs[0] ),
			   sizeof( expected_output ) / sizeof( expected_output[0] ) );

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Words words;
		Pos pos;
		char buf[MAX_BUF_SIZE];

		ASSERT_TRUE( words.set( input_strs[i], true, 0 ) );

		int32_t len = pos.filter( &words, 0, words.getNumWords(), false, buf, buf + MAX_BUF_SIZE );

		EXPECT_STREQ( expected_output[i], buf );
		EXPECT_EQ( strlen( expected_output[i] ), len );
	}
}

TEST( PosTest, FilterEnding ) {
	char *input_strs[] = {
		"\"So computers are tools of the devil?\" thought Newt. He had no problem believing it. Computers "
		"had to be the tools of somebody, and all he knew for certain w...",

		"Computers make excellent and efficient servants, but I have no wish to serve under them. Captain, a "
		"starship also runs on loyalty to one man, and nothing can ...",

		"Applications programming is a race between software engineers, who strive to produce idiot-proof "
		"programs, and the universe which strives to produce bigger idiots. ...",

		"Computer programming is tremendous fun. Like music, it is a skill that derives from an unknown "
		"blend of innate talent and constant practice. Like drawing,...",

		"No matter how slick the demo is in rehearsal, when you do it in front of a live audience, ...",

		"The best book on programming for the layman is Alice in Wonderland, but that's because it's the "
		"best book on anything for the layman.  ...",

		"Present-day computers are designed primarily to solve preformulated problems or to process data "
		"according to predetermined procedures. Th...",

		"Premature optimization is the root of all evil.",

		"As soon as we started programming, we found to our surprise that it wasn't as easy to get programs "
		"right as we had thought. Debugging had to be discovered. I can remember the exact instant when I "
		"realized that a large part of my life from then on was going to be spent in finding mistakes in my "
		"own programs. ",

		"Preparations for the Titan mission have the unintended consequence of turning the local Eureka "
		"environment into Titan's. Meanwhile, Eureka couples struggle to come to terms with each other ..."
	};

	const char *expected_output[] = {
	    "\"So computers are tools of the devil?\" thought Newt. He had no problem believing it. Computers "
		"had to be the tools of somebody, and all he knew for certain …",

	    "Computers make excellent and efficient servants, but I have no wish to serve under them. Captain, a "
		"starship also runs on loyalty to one man, and nothing can …",

	    "Applications programming is a race between software engineers, who strive to produce idiot-proof "
		"programs, and the universe which strives to produce bigger idiots.",

	    "Computer programming is tremendous fun. Like music, it is a skill that derives from an unknown "
		"blend of innate talent and constant practice. Like drawing, …",

		"No matter how slick the demo is in rehearsal, when you do it in front of a live audience, …",

	    "The best book on programming for the layman is Alice in Wonderland, but that's because it's the "
		"best book on anything for the layman.",

	    "Present-day computers are designed primarily to solve preformulated problems or to process data "
		"according to predetermined procedures.",

		"Premature optimization is the root of all evil.",

		"As soon as we started programming, we found to our surprise that it wasn't as easy to get programs "
		"right as we had thought. Debugging had to be discovered. I can remember the …",

		"Preparations for the Titan mission have the unintended consequence of turning the local Eureka "
		"environment into Titan's. Meanwhile, Eureka couples struggle to come to terms …"
	};

	ASSERT_EQ( sizeof( input_strs ) / sizeof( input_strs[0] ),
			   sizeof( expected_output ) / sizeof( expected_output[0] ) );

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Words words;
		Pos pos;
		char buf[MAX_BUF_SIZE];

		ASSERT_TRUE( words.set( input_strs[i], true, 0 ) );

		int32_t len = pos.filter( &words, 0, -1, true, buf, buf + 180 );

		EXPECT_STREQ( expected_output[i], buf );
		EXPECT_EQ( strlen( expected_output[i] ), len );
	}
}

TEST( PosTest, FilterTags ) {
	char *input_strs[] = {
		"First line.<br>Second line.<br/>Third line.<br />Fourth line.",
	    "<li>item 1</li><li>item 2</li><li>item 3</li>"
	};

	const char *expected_output[] = {
		"First line. Second line. Third line. Fourth line.",
	    "*item 1. *item 2. *item 3. "
	};

	ASSERT_EQ( sizeof( input_strs ) / sizeof( input_strs[0] ),
			   sizeof( expected_output ) / sizeof( expected_output[0] ) );

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Xml xml;
		Words words;
		Pos pos;
		char input[MAX_BUF_SIZE];
		char buf[MAX_BUF_SIZE];

		std::sprintf(input, input_strs[i]);

		ASSERT_TRUE( xml.set( input, strlen( input ), TITLEREC_CURRENT_VERSION, 0, CT_HTML ) );
		ASSERT_TRUE( words.set( &xml, true ) );

		int32_t len = pos.filter( &words, 0, words.getNumWords(), false, buf, buf + MAX_BUF_SIZE );

		EXPECT_STREQ( expected_output[i], buf );
		EXPECT_EQ( strlen( expected_output[i] ), len );
	}
}

TEST( PosTest, FilterSamePunct ) {
	char *input_strs[] = {
		"| ADAM RASMUSSON                                 |                       $40.00 |\n"
		"---------------------------------------------------------------------------------\n"
		"| BRIAN AUSTIN                                   |                       $40.00 |\n"
		"---------------------------------------------------------------------------------\n"
		"| DAN IALACCI                                    |                       $40.00 |\n",

	    "| ADAM RASMUSSON                                 |                       $40.00 |"
		"+------------------------------------------------+------------------------------+"
		"| BRIAN AUSTIN                                   |                       $40.00 |"
		"+------------------------------------------------+------------------------------+"
		"| DAN IALACCI                                    |                       $40.00 |"
	};

	const char *expected_output[] = {
	    "| ADAM RASMUSSON | $40.00 | … | BRIAN AUSTIN | $40.00 | … | DAN IALACCI | $40.00 | ",
	    "| ADAM RASMUSSON | $40.00 |+ … +| BRIAN AUSTIN | $40.00 |+ … +| DAN IALACCI | $40.00 |"
	};

	ASSERT_EQ( sizeof( input_strs ) / sizeof( input_strs[0] ),
			   sizeof( expected_output ) / sizeof( expected_output[0] ) );

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Words words;
		Pos pos;
		char buf[MAX_BUF_SIZE];

		ASSERT_TRUE( words.set( input_strs[i], true, 0 ) );

		int32_t len = pos.filter( &words, 0, -1, true, buf, buf + 180 );

		EXPECT_STREQ( expected_output[i], buf );
		EXPECT_EQ( strlen( expected_output[i] ), len );
	}
}

TEST( PosTest, DecodeHTMLEntities ) {
	char *input_strs[] = {
		"abc &gt; efg",
	    "abc&gt;efg",
	    "abc &amp; efg",
	    "abc&amp;efg",
	    "&amp;&amp;",
	    "&gt;&gt;",
	    "abc &my; def",
	    "&gt&gt;"
	};

	const char *expected_output[] = {
		"abc > efg",
	    "abc>efg",
	    "abc & efg",
	    "abc&efg",
	    "&&",
	    ">>",
	    "abc &my; def",
	    "&gt>"
	};

	ASSERT_EQ( sizeof( input_strs ) / sizeof( input_strs[0] ),
			   sizeof( expected_output ) / sizeof( expected_output[0] ) );

	size_t len = sizeof( input_strs ) / sizeof( input_strs[0] );
	for ( size_t i = 0; i < len; i++ ) {
		Words words;
		Pos pos;
		char buf[MAX_BUF_SIZE];

		ASSERT_TRUE( words.set( input_strs[i], true, 0 ) );

		int32_t len = pos.filter( &words, 0, -1, true, buf, buf + 180 );

		EXPECT_STREQ( expected_output[i], buf );
		EXPECT_EQ( strlen( expected_output[i] ), len );
	}
}
