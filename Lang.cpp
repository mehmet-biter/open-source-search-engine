#include "Lang.h"
#include "iana_charset.h"
#include <cstring>

void languageToString ( unsigned char langId , char *buf ) {
	const char *p = getLanguageString ( langId );
	if ( ! p ) p = "ERROR";
	strcpy(buf,p);
}

static const char * const s_langStrings[] = {
	"Unknown",
	"English",
	"French",
	"Spanish",
	"Russian",
	"Turkish",
	"Japanese",
	"Chinese Traditional",
	"Chinese Simplified",
	"Korean",
	"German",
	"Dutch",
	"Italian",
	"Finnish",
	"Swedish",
	"Norwegian",
	"Portuguese",
	"Vietnamese",
	"Arabic",
	"Hebrew",
	"Indonesian",
	"Greek",
	"Thai",
	"Hindi",
	"Bengala",
	"Polish",
	"Tagalog",
	"Latin",
	"Esperanto",
	"Catalan",
	"Bulgarian",
	"Translingual",
	"Serbo-Croatian",
	"Hungarian",
	"Danish",
	"Lithuanian",
	"Czech",
	"Galician",
	"Georgian",
	"Scottish Gaelic",
	"Gothic",
	"Romanian",
	"Irish",
	"Latvian",
	"Armenian",
	"Icelandic",
	"Ancient Greek",
	"Manx",
	"Ido",
	"Persian",
	"Telugu",
	"Venetian",
	"Malagasy",
	"Kurdish",
	"Luxembourgish",
	"Estonian",
	"Maltese",
	"Slovak",
	"Slovenian",
	"Basque",
	"Welsh",
	"Greenlandic",
	"Faroese",
	"Unwanted",
	NULL
};

const char* getLanguageString ( unsigned char langId ) {
	if ( langId >= sizeof(s_langStrings)/sizeof(char *) ) return NULL;
	return s_langStrings[langId];
}

static const char * const s_langAbbr[] = {
	"xx", // unknown
	"en", // english
	"fr", // french
	"es", // spanish
	"ru", // russian
	"tr", // turkish
	"ja", // japanese
	"zh_tw", // chinese - traditional
	"zh_cn", // chinese - simplified
	"ko", // korean
	"de", // german
	"nl", // dutch
	"it", // italian
	"fi", // finnish
	"sv", // swedish
	"no", // norwegian
	"pt", // portuguese
	"vi", // vietnamese
	"ar", // arabic
	"he", // hebrew
	"id", // indonesian
	"el", // greek
	"th", // thai
	"hi", // hindi
	"bn", // bengala
	"pl", // polish
	"tl", // tagalog
	"la", // latin
	"eo", // esperanto
	"ca", // catalan
	"bg", // bulgarian
	"tx", // translingual
	"sr", // serbo-crotian
	"hu", // hungarian
	"da", // danish
	"lt", // lithuanian
	"cs", // czech
	"gl", // galician
	"ka", // georgian
	"gd", // scottish gaelic
	"go", // gothic, MADE UP!
	"ro", // romanian
	"ga", // irish
	"lv", // latvian
	"hy", // armenian
	"is", // icelandic
	"ag", // ancient greek, MADE UP!
	"gv", // manx
	"io", // ido
	"fa", // persian
	"te", // telugu
	"vv", // venetian MADE UP!
	"mg", // malagasy
	"ku", // kurdish
	"lb", // luxembourgish
	"et", // estonian
	"mt", // maltese
	"sk", // slovak
	"sl", // slovenian
	"eu", // basque
	"cy", // welsh
	"kl", // greenlandic
	"fo", // faroese
	"zz", // unwanted
	NULL
};

lang_t getLangIdFromAbbr ( const char *abbr ) {
	for (int x = 0; x < langLast && s_langAbbr[x]; ++x) {
		if (!strcasecmp((char*)abbr, s_langAbbr[x])) {
			return (lang_t)x;
		}
	}

	// add overrides below

	if (strcasecmp(abbr, "en_uk") == 0 || strcasecmp(abbr, "en_us") == 0) {
		return langEnglish;
	}

	if (strcasecmp(abbr, "nb") == 0 || strcasecmp(abbr, "nn") == 0) {
		return langNorwegian;
	}

	// croatian
	if (strcasecmp(abbr, "hr") == 0) {
		return langSerboCroatian;
	}

	return langUnknown;
}

lang_t getLangIdFromCharset(uint16_t charset) {
	switch (charset) {
		case csISO58GB231280:
		case csGBK:
		case csGB18030:
		case csGB2312:
			return langChineseSimp;
		case csBig5:
			return langChineseTrad;
		case csHalfWidthKatakana:
		case csJISEncoding:
		case csxsjis:
		case csEUCJP:
		case csEUCFixWidJapanese:
		case csISO2022JP:
		case csISO2022JP2:
		case csISO13JISC6220jp:
			return langJapanese;
		case csKSC56011987:
		case csISO2022KR:
		case csEUCKR:
			return langKorean;
		default:
			return langUnknown;
	}
}

const char* getLanguageAbbr ( unsigned char langId ) {
	if ( langId >= sizeof(s_langAbbr)/sizeof(char *) ) {
		return NULL;
	}

	return s_langAbbr[langId];
}
