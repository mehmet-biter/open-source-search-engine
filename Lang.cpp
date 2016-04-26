#include "gb-include.h"

#include "Lang.h"

void languageToString ( unsigned char langId , char *buf ) {
	char *p = getLanguageString ( langId );
	if ( ! p ) p = "ERROR";
	strcpy(buf,p);
}

static char *s_langStrings[] = {
	"Unknown","English","French","Spanish","Russian","Turkish","Japanese",
	"Chinese Traditional","Chinese Simplified","Korean","German","Dutch",
	"Italian","Finnish","Swedish","Norwegian","Portuguese","Vietnamese",
	"Arabic","Hebrew","Indonesian","Greek","Thai","Hindi","Bengala",
	"Polish","Tagalog",

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

char* getLanguageString ( unsigned char langId ) {
	if ( langId >= sizeof(s_langStrings)/sizeof(char *) ) return NULL;
	return s_langStrings[langId];
}

static char *s_langAbbr[] = {
	"xx","en","fr","es","ru","tr","ja","zh_tw","zh_cn","ko","de","nl",
	"it","fi","sv","no","pt","vi","ar","he","id","el","th","hi",
	"bn","pl","tl",

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
	for (int x = 0; x < MAX_LANGUAGES && s_langAbbr[x]; ++x) {
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

char* getLanguageAbbr ( unsigned char langId ) {
	if ( langId >= sizeof(s_langAbbr)/sizeof(char *) ) {
		return NULL;
	}

	return s_langAbbr[langId];
}

/// @todo ALC this is not a good way to check if it's adult or not
// . these are going to be adult, in any language
// . this seems only to be used by Speller.cpp when splitting up words
//   in the url domain. 
// . s/slen is a full word that is found in our "dictionary" so using
//   phrases like biglittlestuff probably should not go here.
bool isAdult( char *s, int32_t slen, char **loc ) {
	char **p = NULL;
	char *a = NULL;
	p = &a;
	if ( loc ) 
		p = loc;
	// check for naughty words
	if ( ( *p = strnstr ( s, "upskirt", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "downblouse", slen ) ) ) return true;
	if ( ( *p = strnstr ( s, "adult", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "shemale", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "spank", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "dildo", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "shaved", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "bdsm", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "voyeur", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "shemale", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "fisting", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "escorts", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "vibrator", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "rgasm", slen     ) ) ) return true; // 0rgasm
	if ( ( *p = strnstr ( s, "orgy", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "orgies", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "orgasm", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "masturbat", slen ) ) ) return true;
	if ( ( *p = strnstr ( s, "stripper", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "lolita", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "softcore", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "whore", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "slut", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "smut", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "tits", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "lesbian", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "swinger", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "housewife", slen ) ) ) return true;
	if ( ( *p = strnstr ( s, "housewive", slen ) ) ) return true;
	if ( ( *p = strnstr ( s, "nude", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "bondage", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "centerfold", slen) ) ) return true;
	if ( ( *p = strnstr ( s, "incest", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "pedophil", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "pedofil", slen   ) ) ) return true;
	// hornyear.com
	if ( ( *p = strnstr ( s, "horny", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "pussy", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "pussies", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "penis", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "vagina", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "phuck", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "blowjob", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "gangbang", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "xxx", slen       ) ) ) return true;
	if ( ( *p = strnstr ( s, "porn", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "felch", slen     ) ) ) return true;
	if ( ( *p = strnstr ( s, "cunt", slen      ) ) ) return true;
	if ( ( *p = strnstr ( s, "bestial", slen   ) ) ) return true;
	if ( ( *p = strnstr ( s, "tranny", slen    ) ) ) return true;
	if ( ( *p = strnstr ( s, "beastial", slen  ) ) ) return true;
	if ( ( *p = strnstr ( s, "crotch", slen    ) ) ) return true;

	// these below may have legit meanings
	if ( ( *p = strnstr ( s, "kink", slen       ) ) ) {
		if ( strnstr ( s, "kinko", slen ) ) return false;// the store
		return true;
	}
	if ( ( *p = strnstr ( s, "sex", slen      ) ) ) {
		// sexton, sextant, sextuplet, sextet
		if ( strnstr ( s, "sext", slen       ) ) return false;
		if ( strnstr ( s, "middlesex", slen  ) ) return false;
		if ( strnstr ( s, "sussex", slen     ) ) return false;
		if ( strnstr ( s, "essex", slen      ) ) return false;
		if ( strnstr ( s, "deusex", slen     ) )
			return false; // video game
		if ( strnstr ( s, "sexchange", slen  ) )
			return false; // businessexh
		if ( strnstr ( s, "sexpress", slen   ) )
			return false; // *express
		if ( strnstr ( s, "sexpert", slen    ) )
			return false; // *expert
		if ( strnstr ( s, "sexcel", slen     ) )
			return false; // *excellence
		if ( strnstr ( s, "sexist", slen     ) )
			return false; // existence
		if ( strnstr ( s, "sexile", slen     ) )
			return false; // existence
		if ( strnstr ( s, "harassm", slen    ) )
			return false; // harassment
		if ( strnstr ( s, "sexperi", slen    ) )
			return false; // experience
		if ( strnstr ( s, "transex", slen    ) )
			return false; // transexual
		if ( strnstr ( s, "sexual", slen     ) )
			return false; // abuse,health
		if ( strnstr ( s, "sexpo", slen      ) )
			return false; // expo,expose
		if ( strnstr ( s, "exoti", slen      ) )
			return false; // exotic(que)
		if ( strnstr ( s, "sexclu", slen     ) )
			return false; // exclusive/de
		return true;
	}
	// www.losAnaLos.de
	// sanalcafe.net
	if ( ( *p = strnstr ( s, "anal", slen ) ) ) {
		if ( strnstr ( s, "analog", slen     ) )
			return false; // analogy
		if ( strnstr ( s, "analy", slen      ) )
			return false; // analysis
		if ( strnstr ( s, "canal", slen      ) )
			return false;
		if ( strnstr ( s, "kanal", slen      ) )
			return false; // german
		if ( strnstr ( s, "banal", slen      ) )
			return false;
		return true;
	}
	if ( ( *p = strnstr ( s, "cum", slen ) ) ) {
		if ( strnstr ( s, "circum", slen     ) )
			return false; // circumvent
		if ( strnstr ( s, "magn", slen       ) )
			return false; // magna cum
		if ( strnstr ( s, "succu", slen      ) )
			return false; // succumb
		if ( strnstr ( s, "cumber", slen     ) )
			return false; // encumber
		if ( strnstr ( s, "docum", slen      ) )
			return false; // document
		if ( strnstr ( s, "cumul", slen      ) )
			return false; // accumulate
		if ( strnstr ( s, "acumen", slen     ) )
			return false; // acumen
		if ( strnstr ( s, "cucum", slen      ) )
			return false; // cucumber
		if ( strnstr ( s, "incum", slen      ) )
			return false; // incumbent
		if ( strnstr ( s, "capsicum", slen   ) ) return false;
		if ( strnstr ( s, "modicum", slen    ) ) return false;
		if ( strnstr ( s, "locum", slen      ) )
			return false; // slocum
		if ( strnstr ( s, "scum", slen       ) ) return false;
		if ( strnstr ( s, "accu", slen       ) )
			return false; // compounds!
		// arcum.de
		// cummingscove.com
		// cumchristo.org
		return true;
	}

	if ( ( *p = strnstr ( s, "clit", slen ) ) &&
	       ! strnstr ( s, "heraclitus", slen ) )
		return true;
	// fuckedcompany.com is ok
	if ( ( *p = strnstr ( s, "fuck", slen ) ) &&
	       ! strnstr ( s, "fuckedcomp", slen ) )
		return true;
	if ( ( *p = strnstr ( s, "boob", slen ) ) &&
	       ! strnstr ( s, "booboo", slen ) )
		return true;
	if ( ( *p = strnstr ( s, "wank", slen ) )&&
	       ! strnstr ( s, "swank", slen ) )
		return true;
	// fick is german for fuck (fornication under consent of the king)
	if ( ( *p = strnstr ( s, "fick", slen ) )&&
	       ! strnstr ( s, "fickle", slen ) &&
	       ! strnstr ( s, "traffick", slen ) )return true;
	// sclerotic
	// buerotipp.de
	if ( ( *p = strnstr ( s, "eroti", slen ) ) &&
	       ! strnstr ( s, "sclero", slen ) )
		return true;
	// albaberlin.com
	// babelfish.altavista.com
	if ( ( *p = strnstr ( s, "babe", slen ) ) &&
	       ! strnstr ( s, "toyland", slen ) &&
	       ! strnstr ( s, "babel", slen ) )
		return true;
	// what is gaya.dk?
	if ( ( *p = strnstr ( s, "gay", slen ) ) &&
	       ! strnstr ( s, "gaylord", slen ) )
		return true;
	// url appears to be ok
	return false;
}
