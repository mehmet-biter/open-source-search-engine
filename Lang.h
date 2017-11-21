// Matt Wells, copyright Jul 2001

// . language detector
// . TODO: use stopwords in doc to determine the language

#ifndef GB_LANG_H
#define GB_LANG_H

#include <inttypes.h>

#define MAX_LANGUAGES 64
// for langs 1-55, exclude translingual
// 64 - 8 is 56, then minus 1 is 55 bits
// translingual is the 31st bit, english is the first bit
//#define LANG_BIT_MASK 0x007fffffffffffffLL
#define LANG_BIT_MASK 0x7fffffffffffffffLL

enum lang_t {
	langUnknown     = 0,
	langEnglish     = 1,
	langFrench      = 2,
	langSpanish     = 3,
	langRussian     = 4,
	langTurkish     = 5,
	langJapanese    = 6,
	langChineseTrad = 7,
	langChineseSimp = 8,
	langKorean      = 9,
	langGerman      = 10,
	langDutch       = 11,
	langItalian     = 12,
	langFinnish     = 13,
	langSwedish     = 14,
	langNorwegian   = 15,
	langPortuguese  = 16,
	langVietnamese  = 17,
	langArabic      = 18,
	langHebrew      = 19,
	langIndonesian  = 20,
	langGreek       = 21,
	langThai        = 22,
	langHindi       = 23,
	langBengala     = 24,
	langPolish      = 25,
	langTagalog     = 26,
	// added for wiktionary
	langLatin          = 27,
	langEsperanto      = 28,
	langCatalan        = 29,
	langBulgarian      = 30,
	langTranslingual   = 31, // used by multiple langs in wiktionary
	langSerboCroatian  = 32,
	langHungarian      = 33,
	langDanish         = 34,
	langLithuanian     = 35,
	langCzech          = 36,
	langGalician       = 37,
	langGeorgian       = 38,
	langScottishGaelic = 39,
	langGothic         = 40,
	langRomanian       = 41,
	langIrish          = 42,
	langLatvian        = 43,
	langArmenian       = 44,
	langIcelandic      = 45,
	langAncientGreek   = 46,
	langManx           = 47,
	langIdo            = 48,
	langPersian        = 49,
	langTelugu         = 50,
	langVenetian       = 51,
	langMalgasy        = 52,
	langKurdish        = 53,
	langLuxembourgish  = 54,
	langEstonian       = 55,
	langMaltese        = 56,
	langSlovak         = 57,
	langSlovenian      = 58,
	langBasque         = 59,
	langWelsh          = 60,
	langGreenlandic    = 61,
	langFaroese        = 62,
	langUnwanted       = 63,
	langLast
};

lang_t getLangIdFromAbbr ( const char *abbr ) ;
lang_t getLangIdFromCharset(uint16_t charset);

void        languageToString ( unsigned char lang , char *buf );
const char* getLanguageString ( unsigned char lang);
const char* getLanguageAbbr ( unsigned char langId);

#endif // GB_LANG_H
