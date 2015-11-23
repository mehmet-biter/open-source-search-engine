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

	return langUnknown;;
}

char* getLanguageAbbr ( unsigned char langId ) {
	if ( langId >= sizeof(s_langAbbr)/sizeof(char *) ) {
		return NULL;
	}

	return s_langAbbr[langId];
}

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
	if ( ( *p = strnstr2 ( s , slen , "upskirt"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "downblouse" ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "adult"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "shemale"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "spank"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "dildo"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "shaved"     ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "bdsm"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "voyeur"     ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "shemale"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "fisting"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "escorts"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "vibrator"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "rgasm"      ) ) ) return true; // 0rgasm
	if ( ( *p = strnstr2 ( s , slen , "orgy"       ) ) ) return true; 
	if ( ( *p = strnstr2 ( s , slen , "orgies"     ) ) ) return true; 
	if ( ( *p = strnstr2 ( s , slen , "orgasm"     ) ) ) return true; 
	if ( ( *p = strnstr2 ( s , slen , "masturbat"  ) ) ) return true; 
	if ( ( *p = strnstr2 ( s , slen , "stripper"   ) ) ) return true; 
	if ( ( *p = strnstr2 ( s , slen , "lolita"     ) ) ) return true; 
	//if ( ( *p = strnstr2 ( s , slen , "hardcore"   ) ) ) return true; ps2hardcore.co.uk
	if ( ( *p = strnstr2 ( s , slen , "softcore"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "whore"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "slut"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "smut"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "tits"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "lesbian"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "swinger"    ) ) ) return true;
	// fetish is not necessarily a porn word, like native americans
	// make "fetishes" and it was catching cakefetish.com, a food site
	//if ( ( *p = strnstr2 ( s , slen , "fetish"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "housewife"  ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "housewive"  ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "nude"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "bondage"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "centerfold" ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "incest"     ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "pedophil"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "pedofil"    ) ) ) return true;
	// hornyear.com
	if ( ( *p = strnstr2 ( s , slen , "horny"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "pussy"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "pussies"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "penis"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "vagina"     ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "phuck"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "blowjob"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "gangbang"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "xxx"        ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "porn"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "felch"      ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "cunt"       ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "bestial"    ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "tranny"     ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "beastial"   ) ) ) return true;
	if ( ( *p = strnstr2 ( s , slen , "crotch"     ) ) ) return true;
	//if ( ( *p = strnstr2 ( s , slen , "oral"     ) ) ) return true; // moral, doctorial, ..
	// these below may have legit meanings
	if ( ( *p = strnstr2 ( s , slen , "kink"       ) ) ) {
		if ( strnstr2 ( s , slen , "kinko" ) ) return false;// the store
		return true;
	}
	if ( ( *p = strnstr2 ( s , slen , "sex"      ) ) ) {
		// sexton, sextant, sextuplet, sextet
		if ( strnstr2 ( s , slen , "sext"       ) ) return false; 
		if ( strnstr2 ( s , slen , "middlesex"  ) ) return false;
		if ( strnstr2 ( s , slen , "sussex"     ) ) return false;
		if ( strnstr2 ( s , slen , "essex"      ) ) return false;
		if ( strnstr2 ( s , slen , "deusex"     ) ) 
			return false; // video game
		if ( strnstr2 ( s , slen , "sexchange"  ) ) 
			return false; // businessexh
		if ( strnstr2 ( s , slen , "sexpress"   ) ) 
			return false; // *express
		if ( strnstr2 ( s , slen , "sexpert"    ) ) 
			return false; // *expert
		if ( strnstr2 ( s , slen , "sexcel"     ) ) 
			return false; // *excellence
		if ( strnstr2 ( s , slen , "sexist"     ) ) 
			return false; // existence
		if ( strnstr2 ( s , slen , "sexile"     ) ) 
			return false; // existence
		if ( strnstr2 ( s , slen , "harassm"    ) ) 
			return false; // harassment
		if ( strnstr2 ( s , slen , "sexperi"    ) ) 
			return false; // experience
		if ( strnstr2 ( s , slen , "transex"    ) ) 
			return false; // transexual
		if ( strnstr2 ( s , slen , "sexual"     ) ) 
			return false; // abuse,health
		if ( strnstr2 ( s , slen , "sexpo"      ) ) 
			return false; // expo,expose
		if ( strnstr2 ( s , slen , "exoti"      ) ) 
			return false; // exotic(que)
		if ( strnstr2 ( s , slen , "sexclu"     ) ) 
			return false; // exclusive/de
		return true;
	}
	// www.losAnaLos.de
	// sanalcafe.net
	if ( ( *p = strnstr2 ( s , slen , "anal" ) ) ) {
		if ( strnstr2 ( s , slen , "analog"     ) ) 
			return false; // analogy
		if ( strnstr2 ( s , slen , "analy"      ) ) 
			return false; // analysis
		if ( strnstr2 ( s , slen , "canal"      ) ) 
			return false;
		if ( strnstr2 ( s , slen , "kanal"      ) ) 
			return false; // german
		if ( strnstr2 ( s , slen , "banal"      ) ) 
			return false;
		return true;
	}
	if ( ( *p = strnstr2 ( s , slen , "cum" ) ) ) {
		if ( strnstr2 ( s , slen , "circum"     ) ) 
			return false; // circumvent
		if ( strnstr2 ( s , slen , "magn"       ) ) 
			return false; // magna cum
		if ( strnstr2 ( s , slen , "succu"      ) ) 
			return false; // succumb
		if ( strnstr2 ( s , slen , "cumber"     ) ) 
			return false; // encumber
		if ( strnstr2 ( s , slen , "docum"      ) ) 
			return false; // document
		if ( strnstr2 ( s , slen , "cumul"      ) ) 
			return false; // accumulate
		if ( strnstr2 ( s , slen , "acumen"     ) ) 
			return false; // acumen
		if ( strnstr2 ( s , slen , "cucum"      ) ) 
			return false; // cucumber
		if ( strnstr2 ( s , slen , "incum"      ) ) 
			return false; // incumbent
		if ( strnstr2 ( s , slen , "capsicum"   ) ) return false; 
		if ( strnstr2 ( s , slen , "modicum"    ) ) return false; 
		if ( strnstr2 ( s , slen , "locum"      ) ) 
			return false; // slocum
		if ( strnstr2 ( s , slen , "scum"       ) ) return false; 
		if ( strnstr2 ( s , slen , "accu"       ) ) 
			return false; // compounds!
		// arcum.de
		// cummingscove.com
		// cumchristo.org
		return true;
	}
	//if ( ( *p = strnstr2 ( s , slen , "lust"        ) ) ) {
	//	if ( strnstr2 ( s , slen , "illust"   ) ) return false; // illustrated
	//	if ( strnstr2 ( s , slen , "clust"    ) ) return false; // cluster
	//	if ( strnstr2 ( s , slen , "blust"    ) ) return false; // bluster
	//	if ( strnstr2 ( s , slen , "lustrad"  ) ) return false; // balustrade
	//	// TODO: plusthemes.com wanderlust
	//	return true;
	//}
	// brettwatt.com
	//if ( ( *p = strnstr2 ( s , slen , "twat"       ) ) ) {
	//	if ( strnstr2 ( s , slen , "watch"    ) ) return false; // wristwatch
	//	if ( strnstr2 ( s , slen , "atwater"  ) ) return false;
	//	if ( strnstr2 ( s , slen , "water"    ) ) return false; // sweetwater
	//	return true;
	//}
	if ( ( *p = strnstr2 ( s , slen , "clit" ) ) && 
	       ! strnstr2 ( s , slen , "heraclitus" ) )
		return true;
	// fuckedcompany.com is ok
	if ( ( *p = strnstr2 ( s , slen , "fuck" ) ) && 
	       ! strnstr2 ( s , slen , "fuckedcomp" ) )
		return true;
	if ( ( *p = strnstr2 ( s , slen , "boob" ) ) && 
	       ! strnstr2 ( s , slen , "booboo"     ) )
		return true;
	if ( ( *p = strnstr2 ( s , slen , "wank" ) )&& 
	       ! strnstr2 ( s , slen , "swank"      ) )
		return true;
	// fick is german for fuck (fornication under consent of the king)
	if ( ( *p = strnstr2 ( s , slen , "fick" ) )&& 
	       ! strnstr2 ( s , slen , "fickle" ) &&
	       ! strnstr2 ( s , slen , "traffick"   ) )return true;
	// sclerotic
	// buerotipp.de
	if ( ( *p = strnstr2 ( s , slen , "eroti") ) && 
	       ! strnstr2 ( s , slen , "sclero"     ) )
		return true;
	// albaberlin.com
	// babelfish.altavista.com
	if ( ( *p = strnstr2 ( s , slen , "babe" ) ) && 
	       ! strnstr2 ( s , slen , "toyland"   ) &&
	       ! strnstr2 ( s , slen , "babel"      ) )
		return true;
	// what is gaya.dk?
	if ( ( *p = strnstr2 ( s , slen , "gay" ) ) && 
	       ! strnstr2 ( s , slen , "gaylord"    ) )
		return true;
	// url appears to be ok
	return false;
}
