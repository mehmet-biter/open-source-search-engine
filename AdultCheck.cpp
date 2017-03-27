#include "AdultCheck.h"
#include "matches2.h"
#include "Log.h"
#include "Conf.h"
#include "Speller.h" //g_speller for word split uses
#include "Xml.h"
#include <stddef.h>

// . an "id" of 2 means very indicative of a dirty doc
// . an "id" of 1 means it must be joined with another dirty word to indicate
// . taken mostly from Url.cpp
// . see matches2.h for Needle class definition
static const Needle s_dirtyWords []  = {
	{"upskirt"   ,2,0},
	{"downblouse",2,0},
	{"shemale"   ,1,0},
	{"spank"     ,1,0},
	{"dildo"     ,2,0},
	{"bdsm"      ,2,0},
	{"voyeur"    ,2,0},
	{"fisting"   ,2,0},
	{"vibrator"  ,2,0},
	{"ejaculat"  ,2,0},
	{"rgasm"     ,2,0},
	{"orgy"      ,2,0},
	{"orgies"    ,2,0},
	{"stripper"  ,1,0},
	{"softcore"  ,2,0},
	{"whore"     ,2,0},
	// gary slutkin on ted.com. make this just 1 point.
	{"slut"      ,1,0},
	{"smut"      ,2,0},
	{"tits"      ,2,0},
	{"lesbian"   ,2,0},
	{"swinger"   ,2,0},
	{"fetish"    ,2,0},
	{"nude"      ,1,0},
	{"centerfold",2,0},
	{"incest"    ,2,0},
	{"pedophil"  ,2,0},
	{"pedofil"   ,2,0},
	{"horny"     ,2,0}, // horny toad
	{"pussy"     ,2,0}, // pussy willow pussy cat
	{"pussies"   ,2,0},
	{"penis"     ,2,0},
	{"vagina"    ,2,0},
	{"phuck"     ,2,0},
	{"blowjob"   ,2,0},
	{"blow job"  ,2,0},
	{"gangbang"  ,2,0},
	{"xxx"       ,1,0}, // yahoo.com has class="fz-xxxl"
	{"porn"      ,2,0},
	{"felch"     ,2,0},
	{"cunt"      ,2,0},
	{"bestial"   ,2,0},
	{"beastial"  ,2,0},
	{"kink"      ,2,0},
	// . "sex" is often substring in tagids.
	// . too many false positives, make "1" not "2"
	{"sex"       ,1,0},
	{"anal"      ,2,0},
	{"cum"       ,2,0},  // often used for cumulative
	{"clit"      ,2,0},
	{"fuck"      ,2,0},
	{"boob"      ,1,0},
	{"wank"      ,2,0},
	{"fick"      ,2,0},
	{"eroti"     ,2,0},
	{"gay"       ,1,0}, // make 1 pt. 'marvin gay'
	// new stuff not in Url.cpp
	{"thong"     ,1,0},
	{"masturbat" ,2,0},
	{"bitch"     ,1,0},
	{"hell"      ,1,0},
	{"damn"      ,1,0},
	{"rimjob"    ,2,0},
	{"cunnilingu",2,0},
	{"felatio"   ,2,0},
	{"fellatio"  ,2,0},
	{"dick"      ,1,0},
	{"cock"      ,1,0},
	{"rape"      ,2,0},
	{"raping"    ,2,0},
	{"bukake"    ,2,0},
	{"shit"      ,2,0},
	{"naked"     ,1,0},
	{"nympho"    ,2,0},
	{"hardcore"  ,1,0}, // hardcore gamer, count as 1
	{"sodom"     ,2,0},
	{"titties"   ,2,0}, // re-do
	{"twat"      ,2,0},
	{"bastard"   ,1,0},
	{"erotik"    ,2,0},

	// EXCEPTIONS

	// smut
	{"transmut"   ,-2,0},
	{"bismuth"    ,-2,0},

	// sex
	{"middlesex"  ,-1,0},
	{"sussex"     ,-1,0},
	{"essex"      ,-1,0},
	{"deusex"     ,-1,0},
	{"sexchange"  ,-1,0},
	{"sexpress"   ,-1,0},
	{"sexpert"    ,-1,0},


	// EXCEPTIONS

	// sex
	{"middlesex"  ,-1,0},
	{"sussex"     ,-1,0},
	{"essex"      ,-1,0},
	{"deusex"     ,-1,0},
	{"sexchange"  ,-1,0},
	{"sexpress"   ,-1,0},
	{"sexpert"    ,-1,0},
	{"sexcel"     ,-1,0},
	{"sexist"     ,-1,0},
	{"sexile"     ,-1,0},
	{"sexperi"    ,-1,0},
	{"sexual"     ,-1,0},
	{"sexpose"    ,-1,0},
	{"sexclu"     ,-1,0},
	{"sexo"       ,-1,0},
	{"sexism"     ,-1,0},
	{"sexpan"     ,-1,0}, // buttonsexpanion
	{"same-sex"   ,-1,0},
	{"opposite sex",-1,0},

	// anal
	{"analog"     ,-2,0},
	{"analy"      ,-2,0},
	{"canal"      ,-2,0},
	{"kanal"      ,-2,0},
	{"banal"      ,-2,0},
	{"ianalbert"  ,-2,0}, // ian albert

	// cum
	{"circum"     ,-2,0},
	{"cum laude"  ,-2,0},
	{"succum"     ,-2,0},
	{"cumber"     ,-2,0},
	{"docum"      ,-2,0},
	{"cumul"      ,-2,0},
	{"acumen"     ,-2,0},
	{"incum"      ,-2,0},
	{"capsicum"   ,-2,0},
	{"modicum"    ,-2,0},
	{"locum"      ,-2,0},
	{"scum"       ,-2,0},
	{"accum"      ,-2,0},
	{"cumbre"     ,-2,0},

	{"swank"      ,-2,0},
	{"fickle"     ,-2,0},
	{"traffick"   ,-2,0},
	{"scleroti"   ,-2,0},
	{"gaylor"     ,-2,0},
	{"gaynor"     ,-2,0},
	{"gayner"     ,-2,0},
	{"gayton"     ,-2,0},
	{"dipthong"   ,-1,0},

	// hell
	{"hellen"     ,-1,0},
	{"hellman"    ,-1,0},
	{"shell"      ,-1,0},
	{"mitchell"   ,-1,0},
	{"chelle"     ,-1,0},  // me/michelle
	{"hello"      ,-1,0},
	{"moschella"  ,-1,0},
	{"othello"    ,-1,0},
	{"schelling"  ,-1,0},
	{"seychelles" ,-1,0},
	{"wheller"    ,-1,0},
	{"winchell"   ,-1,0},

	// dick
	{"dicker"     ,-1,0},
	{"dickins"    ,-1,0},
	{"dickies"    ,-1,0},
	{"dickran"    ,-1,0},

	// cock
	{"babcock"    ,-1,0},
	{"cocked"     ,-1,0},
	{"cocking"    ,-1,0},
	{"cockpit"    ,-1,0},
	{"cockroach"  ,-1,0},
	{"cocktail"   ,-1,0},
	{"cocky"      ,-1,0},
	{"hancock"    ,-1,0},
	{"hitchcock"  ,-1,0},
	{"peacock"    ,-1,0},
	{"shuttlecock",-1,0},
	{"stopcock"   ,-1,0},
	{"weathercock",-1,0},
	{"woodcock"   ,-1,0},
	{"cockburn"   ,-1,0},

	// kink
	{"kinko"      ,-2,0},
	{"ukink"      ,-2,0},  // ink shop in uk

	// naked
	{"snaked"     ,-1,0},

	// rape
	{"drape"      ,-2,0},
	{"grape"      ,-2,0},
	{"scrape"     ,-2,0},
	{"therape"    ,-2,0},
	{"trapez"     ,-2,0},
	{"parapet"    ,-2,0},
	{"scraping"   ,-2,0},
	{"draping"    ,-2,0},

	// twat
	{"twatch"     ,-2,0}, // courtwatch -- cspan.org

	// clit
	{"heraclitus" ,-2,0},

	// boob
	{"booboo"     ,-1,0},

	// shit
	{"shitak"     ,-2,0},
	
	// scunthorpe (north lincolnshire)
	{"scunthorpe" ,-2,0}
};
static const int32_t numDirty = sizeof(s_dirtyWords) / sizeof(s_dirtyWords[0]);

#if 0
////
//// New stuff from sex.com adult word list
////
////
//// make it a 2nd part because of performance limits on matches2.cpp algo
////
static const Needle s_dirtyWordsPart2 []  = {
        {"amateurfoto" ,2,0},
        {"amateurhardcore"     ,2,0},
        {"amateurindex",2,0},
        {"amateurnaked",2,0},
        {"amatuerhardcore"     ,2,0},
        {"ampland"     ,2,0},
        //{"animehentai" ,2,0}, dup
        {"anitablonde" ,2,0},
        {"asiacarrera" ,2,0},
        {"asshole"     ,2,0},
        {"asslick"     ,2,0},
        {"asspic"      ,2,0},
        {"assworship"  ,2,0},
        //{"badgirl"     ,2,0}, not necessarily bad
        {"bareceleb"   ,2,0},
        {"barenaked"   ,2,0},
        {"beaverboy"   ,2,0},
        {"beavershot" ,2,0}, // was beavershots
        //{"bigball"     ,2,0}, // not necessarily bad
        {"bigbreast"   ,2,0},
        //{"bigbutt"     ,2,0}, // not necessarily bad
        {"bigcock"     ,2,0},
        {"bigdick"     ,2,0},
        {"biggestdick" ,2,0},
        {"biggesttit"  ,2,0},
        {"bighairyball",2,0},
        {"bighooter"   ,2,0},
        {"bignipple"   ,2,0},
        {"bigtit"      ,2,0},
        {"blackbooty"  ,2,0},
        {"blackbutt"   ,2,0},
        {"blackcock"   ,2,0},
        {"blackdick"   ,2,0},
        {"blackhardcore"       ,2,0},
        {"blackonblonde"       ,2,0},
        {"blacksonblonde"      ,2,0},
        {"blacktit"    ,2,0},
        {"blacktwat"   ,2,0},
        {"boner"       ,1,0}, // softcore, someone's lastname?
        {"bordello"    ,2,0},
        {"braless"     ,2,0},
        {"brothel"     ,2,0},
        {"bukake"      ,2,0},
        {"bukkake"     ,2,0},
        {"bustyblonde" ,2,0},
        {"bustyceleb"  ,2,0},
        {"butthole"    ,2,0},
        {"buttman"     ,2,0},
        {"buttpic"     ,2,0},
        {"buttplug"    ,2,0},
        {"buttthumbnails"      ,2,0},
        {"callgirl"    ,2,0},
        {"celebritiesnaked"    ,2,0},
        {"celebritybush"       ,2,0},
        {"celebritybutt"       ,2,0},
        {"chaseylain"  ,2,0},
        {"chickswithdick"      ,2,0},
        {"christycanyon"       ,2,0},
        {"cicciolina"  ,2,0},
        //{"cunilingus"  ,2,0},
        {"cunniling" ,2,0}, // abbreviate
        {"cyberlust"   ,2,0},
        {"danniashe"   ,2,0},
        {"dicksuck"    ,2,0},
        {"dirtymind"   ,2,0},
        {"dirtypicture",2,0},
        {"doggiestyle" ,2,0},
        {"doggystyle"  ,2,0},
        {"domatrix"    ,2,0},
        {"dominatrix"  ,2,0},
        //{"dyke",2,0}, // dick van dyke!
        {"ejaculation" ,2,0},
        {"erosvillage" ,2,0},
        {"facesit"     ,2,0},
        {"fatass"      ,2,0},
        {"feetfetish"  ,2,0},
        {"felatio"     ,2,0},
        {"fellatio"    ,2,0},
        {"femdom"      ,2,0},
        {"fetishwear"  ,2,0},
        {"fettegirl"   ,2,0},
        {"fingerbang"  ,2,0},
        {"fingering"   ,1,0}, // fingering the keyboard? use 1
        {"flesh4free"  ,2,0},
        {"footfetish"  ,2,0},
        {"footjob"     ,2,0},
        {"footlicking" ,2,0},
        {"footworship" ,2,0},
        {"fornication" ,2,0},
        {"freeass"     ,2,0},
        {"freebigtit"  ,2,0},
        {"freedick"    ,2,0},
        {"freehardcore",2,0},
        //{"freehentai"  ,2,0}, dup
        {"freehooter"  ,2,0},
        {"freelargehooter"     ,2,0},
        {"freenakedpic",2,0},
        {"freenakedwomen"      ,2,0},
        {"freetit"     ,2,0},
        {"freevoyeur"  ,2,0},
        {"gratishardcoregalerie"       ,2,0},
        {"hardcorecelebs"      ,2,0},
        {"hardcorefree",2,0},
        {"hardcorehooter"      ,2,0},
        {"hardcorejunkie"      ,2,0},
        {"hardcorejunky"       ,2,0},
        {"hardcoremovie"       ,2,0},
        {"hardcorepic" ,2,0},
        {"hardcorepix" ,2,0},
        {"hardcoresample"      ,2,0},
        {"hardcorestories"     ,2,0},
        {"hardcorethumb"       ,2,0},
        {"hardcorevideo"       ,2,0},
        {"harddick"    ,2,0},
        {"hardnipple"  ,2,0},
        {"hardon"      ,2,0},
        {"hentai"      ,2,0},
        {"interacialhardcore"  ,2,0},
        {"intercourseposition" ,2,0},
        {"interracialhardcore" ,2,0},
        {"ittybittytitty"      ,2,0},
        {"jackoff"     ,2,0},
        {"jennajameson",2,0},
        {"jennicam"    ,2,0},
        {"jerkoff"     ,2,0},
        {"jism",2,0},
        {"jiz" ,2,0},
        {"justhardcore",2,0},
        {"karasamateurs"       ,2,0},
        {"kascha"      ,2,0},
        {"kaylakleevage"       ,2,0},
        {"kobetai"     ,2,0},
        {"lapdance"    ,2,0},
        {"largedick"   ,2,0},
        {"largehooter" ,2,0},
        {"largestbreast"       ,2,0},
        {"largetit"    ,2,0},
        {"lesben"      ,2,0},
        {"lesbo"       ,2,0},
        {"lickadick"   ,2,0},
        {"lindalovelace"       ,2,0},
        {"longdick"    ,2,0},
        {"lovedoll"    ,2,0},
        {"makinglove"  ,2,0},
        {"mangax"      ,2,0},
        {"manpic"      ,2,0},
        {"marilynchambers"     ,2,0},
        {"massivecock" ,2,0},
        {"masterbating",2,0},
        {"mensdick"    ,2,0},
        {"milf",2,0},
        {"minka"       ,2,0},
        {"monstercock" ,2,0},
        {"monsterdick" ,2,0},
        {"muffdiving"  ,2,0},
        {"nacktfoto"   ,2,0},
        {"nakedblackwomen"     ,2,0},
        {"nakedceleb"  ,2,0},
        {"nakedcelebrity"      ,2,0},
        {"nakedcheerleader"    ,2,0},
        {"nakedchick"  ,2,0},
        {"nakedgirl"   ,2,0},
        {"nakedguy"    ,2,0},
        {"nakedladies" ,2,0},
        {"nakedlady"   ,2,0},
        {"nakedman"    ,2,0},
        {"nakedmen"    ,2,0},
        {"nakedness"   ,2,0},
        {"nakedphoto"  ,2,0},
        {"nakedpic"    ,2,0},
        {"nakedstar"   ,2,0},
        {"nakedwife"   ,2,0},
        {"nakedwoman"  ,2,0},
        {"nakedwomen"  ,2,0},
        {"nastychat"   ,2,0},
        {"nastythumb"  ,2,0},
        {"naughtylink" ,2,0},
        {"naughtylinx" ,2,0},
        {"naughtylynx" ,2,0},
        {"naughtynurse",2,0},
        {"niceass"     ,2,0},
        {"nikkinova"   ,2,0},
        {"nikkityler"  ,2,0},
        {"nylonfetish" ,2,0},
        {"nympho"      ,2,0},
        {"openleg"     ,2,0},
        {"oral4free"   ,2,0},
        {"pantyhosefetish"     ,2,0},
        {"peepcam"     ,2,0},
        {"persiankitty",2,0},
        {"perverted"   ,2,0},
        {"pimpserver"  ,2,0},
        {"pissing"     ,2,0},
        {"poontang"    ,2,0},
        {"privatex"    ,2,0},
        {"prono"       ,2,0},
        {"publicnudity",2,0},
        {"puffynipple" ,2,0},
        {"racqueldarrian"      ,2,0},
        //{"rape",2,0}, // dup!
        {"rawlink"     ,2,0},
        {"realhardcore",2,0},
        {"rubberfetish",2,0},
        {"seka",2,0},
        {"sheboy"      ,2,0},
        {"showcam"     ,2,0},
        {"showercam"   ,2,0},
        {"smallbreast" ,2,0},
        {"smalldick"   ,2,0},
        {"spycamadult" ,2,0},
        {"strapon"     ,2,0},
        {"stripclub"   ,2,0},
        {"stripshow"   ,2,0},
        {"striptease"  ,2,0},
        {"strokeit"    ,2,0},
        {"strokeme"    ,2,0},
        {"suckdick"    ,2,0},
        {"sylviasaint" ,2,0},
        {"teenhardcore",2,0},
        {"teenie"      ,2,0},
        {"teenpic"     ,2,0},
        {"teensuck"    ,2,0},
        {"tgp" ,2,0},
        {"threesome"   ,2,0},
        {"thumblord"   ,2,0},
        {"thumbzilla"  ,2,0},
        {"tiffanytowers"       ,2,0},
        {"tinytitties" ,2,0},
        //{"tities"      ,2,0}, // entities
        {"titman"      ,2,0},
        {"titsandass"  ,2,0},
        {"titties"     ,2,0},
        {"titts"       ,2,0},
        {"titty"       ,2,0},
        {"tokyotopless",2,0},
        {"tommysbookmark"      ,2,0},
        {"toplesswomen",2,0},
        {"trannies"    ,2,0},
        {"twinks"      ,2,0},
        {"ultradonkey" ,2,0},
        {"ultrahardcore"       ,2,0},
        {"uncutcock"   ,2,0},
        {"vividtv"     ,2,0},
        {"wendywhoppers"       ,2,0},
        {"wetdick"     ,2,0},
        {"wetpanties"  ,2,0},
        {"wifesharing" ,2,0},
        {"wifeswapping",2,0},
        {"xrated"      ,2,0}
};
static const int32_t numDirty2 = sizeof(s_dirtyWordsPart2) / sizeof(s_dirtyWordsPart2[0]);
static bool initDirtyWordsPart2 = initializeNeedle(s_dirtyWordsPart2, numDirty2);
#endif

int32_t getAdultPoints ( char *s, int32_t slen, const char *url ) {
	NeedleMatch dirtyWordsMatches[numDirty];

	// . use the matches function to get all the matches
	// . then check each match to see if it is actually a legit word
	// . actually match the dirty words, then match the clean words
	//   then we can subtract counts.
	getMatches2(s_dirtyWords, dirtyWordsMatches, numDirty, s, slen, NULL, NULL);

	int32_t points = 0;
	// each needle has an associated score
	for ( int32_t i = 0 ; i < numDirty ; i++ ) {
		// skip if no match
		if ( dirtyWordsMatches[i].m_count <= 0 ) continue;
		// . the "id", is positive for dirty words, - for clean
		// . uses +2/-2 for really dirty words
		// . uses +1/-1 for borderline dirty words
		points += s_dirtyWords[i].m_id;
		logDebug(g_conf.m_logDebugDirty, "dirty: %s %" PRId32" %s", s_dirtyWords[i].m_string, (int32_t) s_dirtyWords[i].m_id, url);
	}

	////
	//
	// repeat for part2
	//
	// we have to do two separate parts otherwise the algo in
	// matches2.cpp gets really slow. it was not meant to match
	// so many needles in one haystack.
	//
	///

#if 0
	// . disable this for now. most of these are phrases and they
	//   will not be detected.
	// . TODO: hash the dirty words and phrases and just lookup
	//   words in that table like we do for isStopWord(), but use
	//   isDirtyWord(). Then replace the code is Speller.cpp
	//   with isDirtyUrl() which will split the string into words
	//   and call isDirtyWord() on each one. also use bi and tri grams
	//   in the hash table.

	getMatches2 ( s_dirtyWordsPart2 ,
		      numDirty2     ,
		      s            ,
		      slen         ,
		      NULL         , // linkPos
		      NULL         , // needleNum
		      false        , // stopAtFirstMatch?
		      NULL         , // hadPreMatch ptr
		      true         ); // saveQuickTables?


	// each needle has an associated score
	for ( int32_t i = 0 ; i < numDirty2 ; i++ ) {
		// skip if no match
		if ( s_dirtyWordsPart2[i].m_count <= 0 ) continue;
		// . the "id", is positive for dirty words, - for clean
		// . uses +2/-2 for really dirty words
		// . uses +1/-1 for borderline dirty words
		points += s_dirtyWordsPart2[i].m_id;
		// log debug
		if ( ! g_conf.m_logDebugDirty ) continue;
		// show it in the log
		log("dirty: %s %" PRId32" %s"
		    ,s_dirtyWordsPart2[i].m_string
		    ,(int32_t)s_dirtyWordsPart2[i].m_id
		    ,url
		    );
	}
#endif


	return points;
}



/// @todo ALC this is not a good way to check if it's adult or not
// . these are going to be adult, in any language
// . this seems only to be used by Speller.cpp when splitting up words
//   in the url domain.
// . s/slen is a full word that is found in our "dictionary" so using
//   phrases like biglittlestuff probably should not go here.
bool isAdult(const char *s, int32_t slen, const char **loc) {
	const char *a = NULL;
	const char **p = loc ? loc : &a;

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



bool isAdultUrl(const char *s, int32_t slen) {
	if(!isAdult(s,slen))
		return false;

	// check for naughty words. Split words to deep check if we're surely
	// adult. Required because montanalinux.org is showing up as porn
	// because it has 'anal' in the hostname.
	// send each phrase seperately to be tested.
	// hotjobs.yahoo.com
	const char *a = s;
	const char *p = s;
	bool foundCleanSequence = false;
	char splitWords[1024];
	char *splitp = splitWords;
	while(p < s + slen) {
		while(p < s + slen && *p != '.' && *p != '-')
			p++;
		bool isPorn = false;
		// TODO: do not include "ult" in the dictionary, it is
		// always splitting "adult" as "ad ult". i'd say do not
		// allow it to split a dirty word into two words like that.
		if(g_speller.canSplitWords(a, p - a, &isPorn, splitp, langEnglish)) {
			if(isPorn) {
				log(LOG_DEBUG,"build: identified %s as porn  after splitting words as %s",
				    s, splitp);
				return true;
			}
			foundCleanSequence = true;
			// keep searching for some porn sequence
		}
		p++;
		a = p;
		splitp += strlen(splitp);
	}
	// if we found a clean sequence, its not porn
	if(foundCleanSequence) {
		log(LOG_INFO,"build: did not identify url %s as porn after splitting words as %s",
		    s, splitWords);
		return false;
	}
	// we tried to get some seq of words but failed. Still report
	// this as porn, since isAdult() was true
	logf(LOG_DEBUG,"build: failed to find sequence of words to prove %s was not porn.", s );
	return true;
}


bool isAdultTLD(const char *tld, size_t tld_len) {
	if(tld) {
		if((tld_len==5 && memcmp(tld,"adult",5)==0) ||
		   (tld_len==4 && memcmp(tld,"porn",4)==0) ||
		   (tld_len==3 && memcmp(tld,"sex",3)==0) ||
		   (tld_len==4 && memcmp(tld,"sexy",4)==0) ||
		   (tld_len==3 && memcmp(tld,"xxx",3)==0))
			return true;
	}
	return false;
}
