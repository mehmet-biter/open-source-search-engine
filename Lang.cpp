#include "gb-include.h"

#include "Lang.h"

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
	NULL
};

const char* getLanguageString ( unsigned char langId ) {
	if ( langId >= sizeof(s_langStrings)/sizeof(char *) ) return NULL;
	return s_langStrings[langId];
}

static const char * const s_langAbbr[] = {
	"xx",
	"en",
	"fr",
	"es",
	"ru",
	"tr",
	"ja",
	"zh_tw",
	"zh_cn",
	"ko",
	"de",
	"nl",
	"it",
	"fi",
	"sv",
	"no",
	"pt",
	"vi",
	"ar",
	"he",
	"id",
	"el",
	"th",
	"hi",
	"bn",
	"pl",
	"tl",
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
	"ag", // ancient gree, MADE UP!
	"gv", // manx
	"io", // ido
	"fa", // persian
	"te", // telugu
	"vv", // venetian MADE UP!
	"mg", // malagasy
	"ku", // kurdish
	"lb", // luxembourgish
	"et", // estonian
	NULL
};

uint8_t getLangIdFromAbbr ( const char *abbr ) {
	for (int x = 0; x < langLast && s_langAbbr[x]; ++x) {
		if (!strcasecmp((char*)abbr, s_langAbbr[x])) {
			return x;
		}
	}

	// english?
	if ( ! strcasecmp((char *)abbr,"en_uk")) {
		return langEnglish;
	}

	if ( ! strcasecmp((char *)abbr,"en_us")) {
		return langEnglish;
	}

	return langUnknown;
}

const char* getLanguageAbbr ( unsigned char langId ) {
	if ( langId >= sizeof(s_langAbbr)/sizeof(char *) ) {
		return NULL;
	}

	return s_langAbbr[langId];
}
