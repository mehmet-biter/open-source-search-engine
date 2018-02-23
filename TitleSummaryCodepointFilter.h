#ifndef TITLE_SUMMARY_CODEPOINT_FILTER_H_
#define TITLE_SUMMARY_CODEPOINT_FILTER_H_
#include <inttypes.h>

//Determine if the utf8-encoded codepoint pointed to by 's' is a codepoint that
//we want in a result title or summary. Some pages try to draw attention to
//themselves by using unusual codepoints (eg. arrows, domino tiles, ...). We
//filter those out when showing the search result to the user.

// Refer to:
// http://www.unicode.org/charts/
// http://www.unicode.org/Public/UNIDATA/Blocks.txt
// http://www.utf8-chartable.de/

// Emoji & Pictographs
// 2600–26FF: Miscellaneous Symbols
// 2700–27BF: Dingbats
// 1F300–1F5FF: Miscellaneous Symbols and Pictographs
// 1F600–1F64F: Emoticons
// 1F650–1F67F: Ornamental Dingbats
// 1F680–1F6FF: Transport and Map Symbols
// 1F900–1F9FF: Supplemental Symbols and Pictographs

// Game Symbols
// 1F000–1F02F: Mahjong Tiles
// 1F030–1F09F: Domino Tiles
// 1F0A0–1F0FF: Playing Cards

// Enclosed Alphanumeric Supplement
// 1F1E6–1F1FF: Regional indicator symbols

// Geometric Shapes
// 25A0–25FF: Geometric Shapes

// Specials
// FFF0-FFFF: Specials

// +--------------------+----------+----------+----------+----------+
// | Code Points        | 1st Byte | 2nd Byte | 3rd Byte | 4th Byte |
// +--------------------+----------+----------+----------+----------+
// | U+25A0..U+25BF     | E2       | 96       | A0..BF   |          |
// | U+25C0..U+27BF     | E2       | 97..9E   | 80..BF   |          |
// | U+FFF0..U+FFFF     | EF       | BF       | B0..BF   |          |
// | U+1F000..U+1F0FF   | F0       | 9F       | 80..83   | 80..BF   |
// | U+1F1E6..U+1F1FF   | F0       | 9F       | 87       | A6..BF   |
// | U+1F300..U+1F6FF   | F0       | 9F       | 8C..9B   | 80..BF   |
// | U+1F900..U+1F9FF   | F0       | 9F       | A4..A7   | 80..BF   |
// +--------------------+----------+----------+----------+----------+
bool inline isUtf8UnwantedSymbols(const char *s) {
	const uint8_t *u = (uint8_t *)s;

	if (u[0] == 0xE2) {
		if ((u[1] == 0x96) &&
		    (u[2] >= 0xA0 && u[2] <= 0xBF)) { // U+25A0..U+25BF
			return true;
		} else if ((u[1] >= 0x97 && u[1] <= 0x9E) &&
		           (u[2] >= 0x80 && u[2] <= 0xBF)) { // U+25C0..U+27BF
			return true;
		}
	} else if (u[0] == 0xEF) {
		if ((u[1] == 0xBF) &&
		    (u[2] >= 0xB0 && u[2] <= 0xBF)) { // U+FFF0..U+FFFF
			return true;
		}
	} else if (u[0] == 0xF0 && u[1] == 0x9F) {
		if ((u[2] >= 0x80 && u[2] <= 0x83) &&
		    (u[3] >= 0x80 && u[3] <= 0xBF)) { // U+1F000..U+1F0FF
			return true;
		} else if ((u[2] == 0x87) &&
		           (u[3] >= 0xA6 && u[3] <= 0xBF)) { // U+1F1E6..U+1F1FF
			return true;
		} else if ((u[2] >= 0x8C && u[2] <= 0x9B) &&
		           (u[3] >= 0x80 && u[3] <= 0xBF)) { // U+1F300..U+1F6FF
			return true;
		} else if ((u[2] >= 0xA4 && u[2] <= 0xA7) &&
		           (u[3] >= 0x80 && u[3] <= 0xBF)) { // U+1F900..U+1F9FF
			return true;
		}
	}

	return false;
}


#endif
