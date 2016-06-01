#include "gtest/gtest.h"

#include "Unicode.h"
#include <sstream>

#include "Log.h"

// Test is based on: http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt

TEST(UnicodeTest, ValidUtf8) {
	const char* inputs[] = {
	    // 1.  correct UTF-8 text
	    "κ",
	    "ό",
	    "σ",
	    "μ",
	    "ε",
	    "幸",
	    "运",
	    "Б",
	    "ъ",
	    "л",
	    "г",
	    "а",
	    "р",
	    "с",
	    "к",
	    "и",

	    // 2.  boundary condition test cases
	    // 2.1 first possible sequence of range
	    "\x00",
	    "\xc2\x80",
	    "\xe0\xa0\x80",
	    "\xe1\x80\x80",
	    "\xf0\x90\x80\x80",
	    "\xf1\x80\x80\x80",
	    "\xf4\x80\x80\x80",

	    // 2.2 last possible sequence of range
	    "\x7f",
	    "\xdf\xbf"
	    "\xe0\xbf\xbf",
	    "\xef\xbf\xbf",
	    "\xf0\xbf\xbf\xbf",
	    "\xf3\xbf\xbf\xbf",
	    "\xf4\x8f\xbf\xbf",

	    // 2.3 other boundry conditions
	    "\xed\x9f\xbf",
	    "\xee\x80\x80",
	    "\xef\xbf\xbd",
	    "\xf4\x8f\x80\x80"
	};

	size_t len = sizeof(inputs) / sizeof(inputs[0]);
	for (size_t i = 0; i < len; i++) {
		std::stringstream ss;
		ss << "inputs[" << i << "]";
		SCOPED_TRACE(ss.str());

		EXPECT_TRUE(isValidUtf8Char(inputs[i]));
	}
}

TEST(UnicodeTest, InvalidUtf8) {
	const char* inputs[] = {
	    // 3   malformed sequences
	    // 3.1 unexpected continuation bytes
	    "\x80",
	    "\xbf",

	    // 3.2 lonely start characters
	    "\xc0 ",
	    "\xdf ",
	    "\xe0 ",
	    "\xef ",
	    "\xf0 ",
	    "\xf7 ",
	    "\xf8 ",
	    "\xfb ",
	    "\xfc ",
	    "\xfd ",

	    // 3.3 sequences with last continuation byte missing
	    "\xc2",
	    "\xe0\xa0",
	    "\xe1\x80",
	    "\xf0\x90\x80",
	    "\xf1\x80\x80",
	    "\xf4\x80\x80",
	    "\xdf",
	    "\xe0\xbf",
	    "\xef\xbf",
	    "\xf0\xbf\xbf",
	    "\xf3\xbf\xbf",
	    "\xf4\x8f\xbf",

	    // 3.5 impossible bytes
	    "\xfe",
	    "\xff",
	    "\xfe\xfe\xff\xff",

	    // 4.1 examples of an overlong ascii character
	    "\xc0\xaf",
	    "\xe0\x80\xaf",
	    "\xf0\x80\x80\xaf",

	    // 4.2 maximum overlong sequences
	    "\xc1\xbf",
	    "\xe0\x9f\xbf",
	    "\xf0\x8f\xbf\xbf",

	    // 4.3 overlong representation of the NUL character
	    "\xc0\x80",
	    "\xe0\x80\x80",
	    "\xf0\x80\x80",

	    /// @todo ALC support for the rest of the test cases

	};

	size_t len = sizeof(inputs) / sizeof(inputs[0]);
	for (size_t i = 0; i < len; i++) {
		std::stringstream ss;
		ss << "inputs[" << i << "]";
		SCOPED_TRACE(ss.str());
		EXPECT_FALSE(isValidUtf8Char(inputs[i]));
	}
}

TEST(UnicodeTest, UnwantedSymbols) {
	const char* inputs[] = {
	    // Emoji & Pictographs
		// 2600–26FF: Miscellaneous Symbols
	    "☀",
	    "⛿",

		// 2700–27BF: Dingbats
	    "✀",
	    "➿",

		// 1F300–1F5FF: Miscellaneous Symbols and Pictographs
	    "🌀",
	    "🗿",

		// 1F600–1F64F: Emoticons
	    "😀",
	    "🙏",

		// 1F650–1F67F: Ornamental Dingbats
	    "🙐",
	    "🙿",

		// 1F680–1F6FF: Transport and Map Symbols
	    "🚀",
	    "🛰",

		// 1F900–1F9FF: Supplemental Symbols and Pictographs
		"🤀",
	    "🧿",

		// Game Symbols
		// 1F000–1F02F: Mahjong Tiles
	    "🀀",
	    "🀯",

		// 1F030–1F09F: Domino Tiles
	    "🀰",
	    "🂟",

		// 1F0A0–1F0FF: Playing Cards
	    "🂠",
	    "🃿",

	    // Enclosed Alphanumeric Supplement
		// 1F1E6–1F1FF: Regional indicator symbols
	    "🇦",
	    "🇿",

	    // Geometric Shapes
		// 25A0–25FF: Geometric Shapes
	    "■",
	    "◿",
	};

	size_t len = sizeof(inputs) / sizeof(inputs[0]);
	for (size_t i = 0; i < len; i++) {
		std::stringstream ss;
		ss << "inputs[" << i << "]";
		SCOPED_TRACE(ss.str());

		EXPECT_TRUE(isUtf8UnwantedSymbols(inputs[i]));
	}
}
