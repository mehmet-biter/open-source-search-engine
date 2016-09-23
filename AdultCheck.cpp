#include "AdultCheck.h"
#include "matches2.h"
#include "Log.h"
#include "Conf.h"
#include <stddef.h>

// . an "id" of 2 means very indicative of a dirty doc
// . an "id" of 1 means it must be joined with another dirty word to indicate
// . taken mostly from Url.cpp
// . see matches2.h for Needle class definition
static Needle s_dirtyWords []  = {
	{"upskirt"    ,0,2,0,0,NULL},
	{"downblouse" ,0,2,0,0,NULL},
	{"shemale"    ,0,1,0,0,NULL},
	{"spank"      ,0,1,0,0,NULL},
	{"dildo"      ,0,2,0,0,NULL},
	{"bdsm"       ,0,2,0,0,NULL},
	{"voyeur"     ,0,2,0,0,NULL},
	{"fisting"    ,0,2,0,0,NULL},
	{"vibrator"   ,0,2,0,0,NULL},
	{"ejaculat"   ,0,2,0,0,NULL},
	{"rgasm"      ,0,2,0,0,NULL},
	{"orgy"       ,0,2,0,0,NULL},
	{"orgies"     ,0,2,0,0,NULL},
	{"stripper"   ,0,1,0,0,NULL},
	{"softcore"   ,0,2,0,0,NULL},
	{"whore"      ,0,2,0,0,NULL},
	// gary slutkin on ted.com. make this just 1 point.
	{"slut"       ,0,1,0,0,NULL},
	{"smut"       ,0,2,0,0,NULL},
	{"tits"       ,0,2,0,0,NULL},
	{"lesbian"    ,0,2,0,0,NULL},
	{"swinger"    ,0,2,0,0,NULL},
	{"fetish"     ,0,2,0,0,NULL},
	{"nude"       ,0,1,0,0,NULL},
	{"centerfold" ,0,2,0,0,NULL},
	{"incest"     ,0,2,0,0,NULL},
	{"pedophil"   ,0,2,0,0,NULL},
	{"pedofil"    ,0,2,0,0,NULL},
	{"horny"      ,0,2,0,0,NULL}, // horny toad
	{"pussy"      ,0,2,0,0,NULL}, // pussy willow pussy cat
	{"pussies"    ,0,2,0,0,NULL},
	{"penis"      ,0,2,0,0,NULL},
	{"vagina"     ,0,2,0,0,NULL},
	{"phuck"      ,0,2,0,0,NULL},
	{"blowjob"    ,0,2,0,0,NULL},
	{"blow job"   ,0,2,0,0,NULL},
	{"gangbang"   ,0,2,0,0,NULL},
	{"xxx"        ,0,1,0,0,NULL}, // yahoo.com has class="fz-xxxl"
	{"porn"       ,0,2,0,0,NULL},
	{"felch"      ,0,2,0,0,NULL},
	{"cunt"       ,0,2,0,0,NULL},
	{"bestial"    ,0,2,0,0,NULL},
	{"beastial"   ,0,2,0,0,NULL},
	{"kink"       ,0,2,0,0,NULL},
	// . "sex" is often substring in tagids.
	// . too many false positives, make "1" not "2"
	{"sex"        ,0,1,0,0,NULL},
	{"anal"       ,0,2,0,0,NULL},
	{"cum"        ,0,2,0,0,NULL},  // often used for cumulative
	{"clit"       ,0,2,0,0,NULL},
	{"fuck"       ,0,2,0,0,NULL},
	{"boob"       ,0,1,0,0,NULL},
	{"wank"       ,0,2,0,0,NULL},
	{"fick"       ,0,2,0,0,NULL},
	{"eroti"      ,0,2,0,0,NULL},
	{"gay"        ,0,1,0,0,NULL}, // make 1 pt. 'marvin gay'
	// new stuff not in Url.cpp
	{"thong"      ,0,1,0,0,NULL},
	{"masturbat"  ,0,2,0,0,NULL},
	{"bitch"      ,0,1,0,0,NULL},
	{"hell"       ,0,1,0,0,NULL},
	{"damn"       ,0,1,0,0,NULL},
	{"rimjob"     ,0,2,0,0,NULL},
	{"cunnilingu" ,0,2,0,0,NULL},
	{"felatio"    ,0,2,0,0,NULL},
	{"fellatio"   ,0,2,0,0,NULL},
	{"dick"       ,0,1,0,0,NULL},
	{"cock"       ,0,1,0,0,NULL},
	{"rape"       ,0,2,0,0,NULL},
	{"raping"     ,0,2,0,0,NULL},
	{"bukake"     ,0,2,0,0,NULL},
	{"shit"       ,0,2,0,0,NULL},
	{"naked"      ,0,1,0,0,NULL},
	{"nympho"     ,0,2,0,0,NULL},
	{"hardcore"   ,0,1,0,0,NULL}, // hardcore gamer, count as 1
	{"sodom"      ,0,2,0,0,NULL},
	{"titties"    ,0,2,0,0,NULL}, // re-do
	{"twat"       ,0,2,0,0,NULL},
	{"bastard"    ,0,1,0,0,NULL},
	{"erotik"     ,0,2,0,0,NULL},

	// EXCEPTIONS

	// smut
	{"transmut"    ,0,-2,0,0,NULL},
	{"bismuth"     ,0,-2,0,0,NULL},

	// sex
	{"middlesex"   ,0,-1,0,0,NULL},
	{"sussex"      ,0,-1,0,0,NULL},
	{"essex"       ,0,-1,0,0,NULL},
	{"deusex"      ,0,-1,0,0,NULL},
	{"sexchange"   ,0,-1,0,0,NULL},
	{"sexpress"    ,0,-1,0,0,NULL},
	{"sexpert"     ,0,-1,0,0,NULL},


	// EXCEPTIONS

	// sex
	{"middlesex"   ,0,-1,0,0,NULL},
	{"sussex"      ,0,-1,0,0,NULL},
	{"essex"       ,0,-1,0,0,NULL},
	{"deusex"      ,0,-1,0,0,NULL},
	{"sexchange"   ,0,-1,0,0,NULL},
	{"sexpress"    ,0,-1,0,0,NULL},
	{"sexpert"     ,0,-1,0,0,NULL},
	{"sexcel"      ,0,-1,0,0,NULL},
	{"sexist"      ,0,-1,0,0,NULL},
	{"sexile"      ,0,-1,0,0,NULL},
	{"sexperi"     ,0,-1,0,0,NULL},
	{"sexual"      ,0,-1,0,0,NULL},
	{"sexpose"     ,0,-1,0,0,NULL},
	{"sexclu"      ,0,-1,0,0,NULL},
	{"sexo"        ,0,-1,0,0,NULL},
	{"sexism"      ,0,-1,0,0,NULL},
	{"sexpan"      ,0,-1,0,0,NULL}, // buttonsexpanion
	{"same-sex"    ,0,-1,0,0,NULL},
	{"opposite sex",0,-1,0,0,NULL},

	// anal
	{"analog"      ,0,-2,0,0,NULL},
	{"analy"       ,0,-2,0,0,NULL},
	{"canal"       ,0,-2,0,0,NULL},
	{"kanal"       ,0,-2,0,0,NULL},
	{"banal"       ,0,-2,0,0,NULL},
	{"ianalbert"   ,0,-2,0,0,NULL}, // ian albert

	// cum
	{"circum"      ,0,-2,0,0,NULL},
	{"cum laude"   ,0,-2,0,0,NULL},
	{"succum"      ,0,-2,0,0,NULL},
	{"cumber"      ,0,-2,0,0,NULL},
	{"docum"       ,0,-2,0,0,NULL},
	{"cumul"       ,0,-2,0,0,NULL},
	{"acumen"      ,0,-2,0,0,NULL},
	{"incum"       ,0,-2,0,0,NULL},
	{"capsicum"    ,0,-2,0,0,NULL},
	{"modicum"     ,0,-2,0,0,NULL},
	{"locum"       ,0,-2,0,0,NULL},
	{"scum"        ,0,-2,0,0,NULL},
	{"accum"       ,0,-2,0,0,NULL},
	{"cumbre"      ,0,-2,0,0,NULL},

	{"swank"       ,0,-2,0,0,NULL},
	{"fickle"      ,0,-2,0,0,NULL},
	{"traffick"    ,0,-2,0,0,NULL},
	{"scleroti"    ,0,-2,0,0,NULL},
	{"gaylor"      ,0,-2,0,0,NULL},
	{"gaynor"      ,0,-2,0,0,NULL},
	{"gayner"      ,0,-2,0,0,NULL},
	{"gayton"      ,0,-2,0,0,NULL},
	{"dipthong"    ,0,-1,0,0,NULL},

	// hell
	{"hellen"      ,0,-1,0,0,NULL},
	{"hellman"     ,0,-1,0,0,NULL},
	{"shell"       ,0,-1,0,0,NULL},
	{"mitchell"    ,0,-1,0,0,NULL},
	{"chelle"      ,0,-1,0,0,NULL},  // me/michelle
	{"hello"       ,0,-1,0,0,NULL},
	{"moschella"   ,0,-1,0,0,NULL},
	{"othello"     ,0,-1,0,0,NULL},
	{"schelling"   ,0,-1,0,0,NULL},
	{"seychelles"  ,0,-1,0,0,NULL},
	{"wheller"     ,0,-1,0,0,NULL},
	{"winchell"    ,0,-1,0,0,NULL},

	// dick
	{"dicker"      ,0,-1,0,0,NULL},
	{"dickins"     ,0,-1,0,0,NULL},
	{"dickies"     ,0,-1,0,0,NULL},
	{"dickran"     ,0,-1,0,0,NULL},

	// cock
	{"babcock"     ,0,-1,0,0,NULL},
	{"cocked"      ,0,-1,0,0,NULL},
	{"cocking"     ,0,-1,0,0,NULL},
	{"cockpit"     ,0,-1,0,0,NULL},
	{"cockroach"   ,0,-1,0,0,NULL},
	{"cocktail"    ,0,-1,0,0,NULL},
	{"cocky"       ,0,-1,0,0,NULL},
	{"hancock"     ,0,-1,0,0,NULL},
	{"hitchcock"   ,0,-1,0,0,NULL},
	{"peacock"     ,0,-1,0,0,NULL},
	{"shuttlecock" ,0,-1,0,0,NULL},
	{"stopcock"    ,0,-1,0,0,NULL},
	{"weathercock" ,0,-1,0,0,NULL},
	{"woodcock"    ,0,-1,0,0,NULL},
	{"cockburn"    ,0,-1,0,0,NULL},

	// kink
	{"kinko"       ,0,-2,0,0,NULL},
	{"ukink"       ,0,-2,0,0,NULL},  // ink shop in uk

	// naked
	{"snaked"      ,0,-1,0,0,NULL},

	// rape
	{"drape"       ,0,-2,0,0,NULL},
	{"grape"       ,0,-2,0,0,NULL},
	{"scrape"      ,0,-2,0,0,NULL},
	{"therape"     ,0,-2,0,0,NULL},
	{"trapez"      ,0,-2,0,0,NULL},
	{"parapet"     ,0,-2,0,0,NULL},
	{"scraping"    ,0,-2,0,0,NULL},
	{"draping"     ,0,-2,0,0,NULL},

	// twat
	{"twatch"      ,0,-2,0,0,NULL}, // courtwatch -- cspan.org

	// clit
	{"heraclitus"  ,0,-2,0,0,NULL},

	// boob
	{"booboo"      ,0,-1,0,0,NULL},

	// shit
	{"shitak"      ,0,-2,0,0,NULL},
	
	// scunthorpe (north lincolnshire)
	{"scunthorpe"  ,0,-2,0,0,NULL},
};
static const int32_t numDirty = sizeof(s_dirtyWords) / sizeof(s_dirtyWords[0]);

#if 0
////
//// New stuff from sex.com adult word list
////
////
//// make it a 2nd part because of performance limits on matches2.cpp algo
////
static Needle s_dirtyWordsPart2 []  = {
        {"amateurfoto"  ,0,2,0,0,NULL},
        {"amateurhardcore"      ,0,2,0,0,NULL},
        {"amateurindex" ,0,2,0,0,NULL},
        {"amateurnaked" ,0,2,0,0,NULL},
        {"amatuerhardcore"      ,0,2,0,0,NULL},
        {"ampland"      ,0,2,0,0,NULL},
        //{"animehentai"  ,0,2,0,0,NULL}, dup
        {"anitablonde"  ,0,2,0,0,NULL},
        {"asiacarrera"  ,0,2,0,0,NULL},
        {"asshole"      ,0,2,0,0,NULL},
        {"asslick"      ,0,2,0,0,NULL},
        {"asspic"       ,0,2,0,0,NULL},
        {"assworship"   ,0,2,0,0,NULL},
        //{"badgirl"      ,0,2,0,0,NULL}, not necessarily bad
        {"bareceleb"    ,0,2,0,0,NULL},
        {"barenaked"    ,0,2,0,0,NULL},
        {"beaverboy"    ,0,2,0,0,NULL},
        {"beavershot"  ,0,2,0,0,NULL}, // was beavershots
        //{"bigball"      ,0,2,0,0,NULL}, // not necessarily bad
        {"bigbreast"    ,0,2,0,0,NULL},
        //{"bigbutt"      ,0,2,0,0,NULL}, // not necessarily bad
        {"bigcock"      ,0,2,0,0,NULL},
        {"bigdick"      ,0,2,0,0,NULL},
        {"biggestdick"  ,0,2,0,0,NULL},
        {"biggesttit"   ,0,2,0,0,NULL},
        {"bighairyball" ,0,2,0,0,NULL},
        {"bighooter"    ,0,2,0,0,NULL},
        {"bignipple"    ,0,2,0,0,NULL},
        {"bigtit"       ,0,2,0,0,NULL},
        {"blackbooty"   ,0,2,0,0,NULL},
        {"blackbutt"    ,0,2,0,0,NULL},
        {"blackcock"    ,0,2,0,0,NULL},
        {"blackdick"    ,0,2,0,0,NULL},
        {"blackhardcore"        ,0,2,0,0,NULL},
        {"blackonblonde"        ,0,2,0,0,NULL},
        {"blacksonblonde"       ,0,2,0,0,NULL},
        {"blacktit"     ,0,2,0,0,NULL},
        {"blacktwat"    ,0,2,0,0,NULL},
        {"boner"        ,0,1,0,0,NULL}, // softcore, someone's lastname?
        {"bordello"     ,0,2,0,0,NULL},
        {"braless"      ,0,2,0,0,NULL},
        {"brothel"      ,0,2,0,0,NULL},
        {"bukake"       ,0,2,0,0,NULL},
        {"bukkake"      ,0,2,0,0,NULL},
        {"bustyblonde"  ,0,2,0,0,NULL},
        {"bustyceleb"   ,0,2,0,0,NULL},
        {"butthole"     ,0,2,0,0,NULL},
        {"buttman"      ,0,2,0,0,NULL},
        {"buttpic"      ,0,2,0,0,NULL},
        {"buttplug"     ,0,2,0,0,NULL},
        {"buttthumbnails"       ,0,2,0,0,NULL},
        {"callgirl"     ,0,2,0,0,NULL},
        {"celebritiesnaked"     ,0,2,0,0,NULL},
        {"celebritybush"        ,0,2,0,0,NULL},
        {"celebritybutt"        ,0,2,0,0,NULL},
        {"chaseylain"   ,0,2,0,0,NULL},
        {"chickswithdick"       ,0,2,0,0,NULL},
        {"christycanyon"        ,0,2,0,0,NULL},
        {"cicciolina"   ,0,2,0,0,NULL},
        //{"cunilingus"   ,0,2,0,0,NULL},
        {"cunniling"  ,0,2,0,0,NULL}, // abbreviate
        {"cyberlust"    ,0,2,0,0,NULL},
        {"danniashe"    ,0,2,0,0,NULL},
        {"dicksuck"     ,0,2,0,0,NULL},
        {"dirtymind"    ,0,2,0,0,NULL},
        {"dirtypicture" ,0,2,0,0,NULL},
        {"doggiestyle"  ,0,2,0,0,NULL},
        {"doggystyle"   ,0,2,0,0,NULL},
        {"domatrix"     ,0,2,0,0,NULL},
        {"dominatrix"   ,0,2,0,0,NULL},
        //{"dyke" ,0,2,0,0,NULL}, // dick van dyke!
        {"ejaculation"  ,0,2,0,0,NULL},
        {"erosvillage"  ,0,2,0,0,NULL},
        {"facesit"      ,0,2,0,0,NULL},
        {"fatass"       ,0,2,0,0,NULL},
        {"feetfetish"   ,0,2,0,0,NULL},
        {"felatio"      ,0,2,0,0,NULL},
        {"fellatio"     ,0,2,0,0,NULL},
        {"femdom"       ,0,2,0,0,NULL},
        {"fetishwear"   ,0,2,0,0,NULL},
        {"fettegirl"    ,0,2,0,0,NULL},
        {"fingerbang"   ,0,2,0,0,NULL},
        {"fingering"    ,0,1,0,0,NULL}, // fingering the keyboard? use 1
        {"flesh4free"   ,0,2,0,0,NULL},
        {"footfetish"   ,0,2,0,0,NULL},
        {"footjob"      ,0,2,0,0,NULL},
        {"footlicking"  ,0,2,0,0,NULL},
        {"footworship"  ,0,2,0,0,NULL},
        {"fornication"  ,0,2,0,0,NULL},
        {"freeass"      ,0,2,0,0,NULL},
        {"freebigtit"   ,0,2,0,0,NULL},
        {"freedick"     ,0,2,0,0,NULL},
        {"freehardcore" ,0,2,0,0,NULL},
        //{"freehentai"   ,0,2,0,0,NULL}, dup
        {"freehooter"   ,0,2,0,0,NULL},
        {"freelargehooter"      ,0,2,0,0,NULL},
        {"freenakedpic" ,0,2,0,0,NULL},
        {"freenakedwomen"       ,0,2,0,0,NULL},
        {"freetit"      ,0,2,0,0,NULL},
        {"freevoyeur"   ,0,2,0,0,NULL},
        {"gratishardcoregalerie"        ,0,2,0,0,NULL},
        {"hardcorecelebs"       ,0,2,0,0,NULL},
        {"hardcorefree" ,0,2,0,0,NULL},
        {"hardcorehooter"       ,0,2,0,0,NULL},
        {"hardcorejunkie"       ,0,2,0,0,NULL},
        {"hardcorejunky"        ,0,2,0,0,NULL},
        {"hardcoremovie"        ,0,2,0,0,NULL},
        {"hardcorepic"  ,0,2,0,0,NULL},
        {"hardcorepix"  ,0,2,0,0,NULL},
        {"hardcoresample"       ,0,2,0,0,NULL},
        {"hardcorestories"      ,0,2,0,0,NULL},
        {"hardcorethumb"        ,0,2,0,0,NULL},
        {"hardcorevideo"        ,0,2,0,0,NULL},
        {"harddick"     ,0,2,0,0,NULL},
        {"hardnipple"   ,0,2,0,0,NULL},
        {"hardon"       ,0,2,0,0,NULL},
        {"hentai"       ,0,2,0,0,NULL},
        {"interacialhardcore"   ,0,2,0,0,NULL},
        {"intercourseposition"  ,0,2,0,0,NULL},
        {"interracialhardcore"  ,0,2,0,0,NULL},
        {"ittybittytitty"       ,0,2,0,0,NULL},
        {"jackoff"      ,0,2,0,0,NULL},
        {"jennajameson" ,0,2,0,0,NULL},
        {"jennicam"     ,0,2,0,0,NULL},
        {"jerkoff"      ,0,2,0,0,NULL},
        {"jism" ,0,2,0,0,NULL},
        {"jiz"  ,0,2,0,0,NULL},
        {"justhardcore" ,0,2,0,0,NULL},
        {"karasamateurs"        ,0,2,0,0,NULL},
        {"kascha"       ,0,2,0,0,NULL},
        {"kaylakleevage"        ,0,2,0,0,NULL},
        {"kobetai"      ,0,2,0,0,NULL},
        {"lapdance"     ,0,2,0,0,NULL},
        {"largedick"    ,0,2,0,0,NULL},
        {"largehooter"  ,0,2,0,0,NULL},
        {"largestbreast"        ,0,2,0,0,NULL},
        {"largetit"     ,0,2,0,0,NULL},
        {"lesben"       ,0,2,0,0,NULL},
        {"lesbo"        ,0,2,0,0,NULL},
        {"lickadick"    ,0,2,0,0,NULL},
        {"lindalovelace"        ,0,2,0,0,NULL},
        {"longdick"     ,0,2,0,0,NULL},
        {"lovedoll"     ,0,2,0,0,NULL},
        {"makinglove"   ,0,2,0,0,NULL},
        {"mangax"       ,0,2,0,0,NULL},
        {"manpic"       ,0,2,0,0,NULL},
        {"marilynchambers"      ,0,2,0,0,NULL},
        {"massivecock"  ,0,2,0,0,NULL},
        {"masterbating" ,0,2,0,0,NULL},
        {"mensdick"     ,0,2,0,0,NULL},
        {"milf" ,0,2,0,0,NULL},
        {"minka"        ,0,2,0,0,NULL},
        {"monstercock"  ,0,2,0,0,NULL},
        {"monsterdick"  ,0,2,0,0,NULL},
        {"muffdiving"   ,0,2,0,0,NULL},
        {"nacktfoto"    ,0,2,0,0,NULL},
        {"nakedblackwomen"      ,0,2,0,0,NULL},
        {"nakedceleb"   ,0,2,0,0,NULL},
        {"nakedcelebrity"       ,0,2,0,0,NULL},
        {"nakedcheerleader"     ,0,2,0,0,NULL},
        {"nakedchick"   ,0,2,0,0,NULL},
        {"nakedgirl"    ,0,2,0,0,NULL},
        {"nakedguy"     ,0,2,0,0,NULL},
        {"nakedladies"  ,0,2,0,0,NULL},
        {"nakedlady"    ,0,2,0,0,NULL},
        {"nakedman"     ,0,2,0,0,NULL},
        {"nakedmen"     ,0,2,0,0,NULL},
        {"nakedness"    ,0,2,0,0,NULL},
        {"nakedphoto"   ,0,2,0,0,NULL},
        {"nakedpic"     ,0,2,0,0,NULL},
        {"nakedstar"    ,0,2,0,0,NULL},
        {"nakedwife"    ,0,2,0,0,NULL},
        {"nakedwoman"   ,0,2,0,0,NULL},
        {"nakedwomen"   ,0,2,0,0,NULL},
        {"nastychat"    ,0,2,0,0,NULL},
        {"nastythumb"   ,0,2,0,0,NULL},
        {"naughtylink"  ,0,2,0,0,NULL},
        {"naughtylinx"  ,0,2,0,0,NULL},
        {"naughtylynx"  ,0,2,0,0,NULL},
        {"naughtynurse" ,0,2,0,0,NULL},
        {"niceass"      ,0,2,0,0,NULL},
        {"nikkinova"    ,0,2,0,0,NULL},
        {"nikkityler"   ,0,2,0,0,NULL},
        {"nylonfetish"  ,0,2,0,0,NULL},
        {"nympho"       ,0,2,0,0,NULL},
        {"openleg"      ,0,2,0,0,NULL},
        {"oral4free"    ,0,2,0,0,NULL},
        {"pantyhosefetish"      ,0,2,0,0,NULL},
        {"peepcam"      ,0,2,0,0,NULL},
        {"persiankitty" ,0,2,0,0,NULL},
        {"perverted"    ,0,2,0,0,NULL},
        {"pimpserver"   ,0,2,0,0,NULL},
        {"pissing"      ,0,2,0,0,NULL},
        {"poontang"     ,0,2,0,0,NULL},
        {"privatex"     ,0,2,0,0,NULL},
        {"prono"        ,0,2,0,0,NULL},
        {"publicnudity" ,0,2,0,0,NULL},
        {"puffynipple"  ,0,2,0,0,NULL},
        {"racqueldarrian"       ,0,2,0,0,NULL},
        //{"rape" ,0,2,0,0,NULL}, // dup!
        {"rawlink"      ,0,2,0,0,NULL},
        {"realhardcore" ,0,2,0,0,NULL},
        {"rubberfetish" ,0,2,0,0,NULL},
        {"seka" ,0,2,0,0,NULL},
        {"sheboy"       ,0,2,0,0,NULL},
        {"showcam"      ,0,2,0,0,NULL},
        {"showercam"    ,0,2,0,0,NULL},
        {"smallbreast"  ,0,2,0,0,NULL},
        {"smalldick"    ,0,2,0,0,NULL},
        {"spycamadult"  ,0,2,0,0,NULL},
        {"strapon"      ,0,2,0,0,NULL},
        {"stripclub"    ,0,2,0,0,NULL},
        {"stripshow"    ,0,2,0,0,NULL},
        {"striptease"   ,0,2,0,0,NULL},
        {"strokeit"     ,0,2,0,0,NULL},
        {"strokeme"     ,0,2,0,0,NULL},
        {"suckdick"     ,0,2,0,0,NULL},
        {"sylviasaint"  ,0,2,0,0,NULL},
        {"teenhardcore" ,0,2,0,0,NULL},
        {"teenie"       ,0,2,0,0,NULL},
        {"teenpic"      ,0,2,0,0,NULL},
        {"teensuck"     ,0,2,0,0,NULL},
        {"tgp"  ,0,2,0,0,NULL},
        {"threesome"    ,0,2,0,0,NULL},
        {"thumblord"    ,0,2,0,0,NULL},
        {"thumbzilla"   ,0,2,0,0,NULL},
        {"tiffanytowers"        ,0,2,0,0,NULL},
        {"tinytitties"  ,0,2,0,0,NULL},
        //{"tities"       ,0,2,0,0,NULL}, // entities
        {"titman"       ,0,2,0,0,NULL},
        {"titsandass"   ,0,2,0,0,NULL},
        {"titties"      ,0,2,0,0,NULL},
        {"titts"        ,0,2,0,0,NULL},
        {"titty"        ,0,2,0,0,NULL},
        {"tokyotopless" ,0,2,0,0,NULL},
        {"tommysbookmark"       ,0,2,0,0,NULL},
        {"toplesswomen" ,0,2,0,0,NULL},
        {"trannies"     ,0,2,0,0,NULL},
        {"twinks"       ,0,2,0,0,NULL},
        {"ultradonkey"  ,0,2,0,0,NULL},
        {"ultrahardcore"        ,0,2,0,0,NULL},
        {"uncutcock"    ,0,2,0,0,NULL},
        {"vividtv"      ,0,2,0,0,NULL},
        {"wendywhoppers"        ,0,2,0,0,NULL},
        {"wetdick"      ,0,2,0,0,NULL},
        {"wetpanties"   ,0,2,0,0,NULL},
        {"wifesharing"  ,0,2,0,0,NULL},
        {"wifeswapping" ,0,2,0,0,NULL},
        {"xrated"       ,0,2,0,0,NULL}
};
static const int32_t numDirty2 = sizeof(s_dirtyWordsPart2) / sizeof(s_dirtyWordsPart2[0]);
#endif

int32_t getAdultPoints ( char *s, int32_t slen, int32_t niceness, const char *url ) {
	// . use the matches function to get all the matches
	// . then check each match to see if it is actually a legit word
	// . actually match the dirty words, then match the clean words
	//   then we can subtract counts.
	getMatches2 ( s_dirtyWords ,
		      numDirty     ,
		      s            ,
		      slen         ,
		      NULL         , // linkPos
		      NULL         , // needleNum
		      false        , // stopAtFirstMatch?
		      NULL         , // hadPreMatch ptr
		      true         ); // saveQuickTables?

	int32_t points = 0;
	// each needle has an associated score
	for ( int32_t i = 0 ; i < numDirty ; i++ ) {
		// skip if no match
		if ( s_dirtyWords[i].m_count <= 0 ) continue;
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
