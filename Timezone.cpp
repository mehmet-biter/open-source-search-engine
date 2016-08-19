#include "Timezone.h"
#include "HashTableX.h"
#include "hash.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include <ctype.h>

// now time zones
struct TimeZone {
	char m_name[16];
	// tzinfo:
        int32_t m_hourMod;
        int32_t m_minMod;
};


static const TimeZone tzs[] = {
	{ "acdt"    ,  10,  30 }, //  ACDT, +10:30
	{ "acst"    ,   9,  30 }, //  ACST, +9:30
	{ "adt"     ,  -3,   0 }, //  ADT, -3:00
	{ "aedt"    ,  11,   0 }, //  AEDT, +11:00
	{ "aest"    ,  10,   0 }, //  AEST, +10:00
	{ "aft"     ,   4,  30 }, //  AFT, +4:30
	{ "ahdt"    ,  -9,   0 }, //  AHDT, -9:00 - historical?
	{ "ahst"    , -10,   0 }, //  AHST, -10:00 - historical?
	{ "akdt"    ,  -8,   0 }, //  AKDT, -8:00
	{ "akst"    ,  -9,   0 }, //  AKST, -9:00
	{ "amst"    ,   4,   0 }, //  AMST, +4:00
	{ "amt"     ,   4,   0 }, //  AMT, +4:00
	{ "anast"   ,  13,   0 }, //  ANAST, +13:00
	{ "anat"    ,  12,   0 }, //  ANAT, +12:00
	{ "art"     ,  -3,   0 }, //  ART, -3:00
	{ "ast"     ,  -4,   0 }, //  AST, -4:00
	{ "at"      ,  -1,   0 }, //  AT, -1:00
	{ "awst"    ,   8,   0 }, //  AWST, +8:00
	{ "azost"   ,   0,   0 }, //  AZOST, 0:00
	{ "azot"    ,  -1,   0 }, //  AZOT, -1:00
	{ "azst"    ,   5,   0 }, //  AZST, +5:00
	{ "azt"     ,   4,   0 }, //  AZT, +4:00
	{ "badt"    ,   4,   0 }, //  BADT, +4:00
	{ "bat"     ,   6,   0 }, //  BAT, +6:00
	{ "bdst"    ,   2,   0 }, //  BDST, +2:00
	{ "bdt"     ,   6,   0 }, //  BDT, +6:00
	{ "bet"     , -11,   0 }, //  BET, -11:00
	{ "bnt"     ,   8,   0 }, //  BNT, +8:00
	{ "bort"    ,   8,   0 }, //  BORT, +8:00
	{ "bot"     ,  -4,   0 }, //  BOT, -4:00
	{ "bra"     ,  -3,   0 }, //  BRA, -3:00
	{ "bst"     ,   1,   0 }, //  BST, +1:00
	{ "bt"      ,   6,   0 }, //  BT, +6:00
	{ "btt"     ,   6,   0 }, //  BTT, +6:00
	{ "cat"     ,   2,   0 }, //  CAT, +2:00
	{ "cct"     ,   8,   0 }, //  CCT, +8:00
	{ "cdt"     ,  -5,   0 }, //  CDT, -5:00
	{ "cest"    ,   2,   0 }, //  CEST, +2:00
	{ "cet"     ,   1,   0 }, //  CET, +1:00
	{ "chadt"   ,  13,  45 }, //  CHADT, +13:45
	{ "chast"   ,  12,  45 }, //  CHAST, +12:45
	{ "chst"    ,  10,   0 }, //  CHST, +10:00
	{ "ckt"     , -10,   0 }, //  CKT, -10:00
	{ "clst"    ,  -3,   0 }, //  CLST, -3:00
	{ "clt"     ,  -4,   0 }, //  CLT, -4:00
	{ "cot"     ,  -5,   0 }, //  COT, -5:00
	{ "cst"     ,  -6,   0 }, //  CST, -6:00
	{ "ct"      ,  -6,   0 }, //  CT, -6:00
	{ "cut"     ,   0,   0 }, //  CUT, 0:00
	{ "cxt"     ,   7,   0 }, //  CXT, +7:00
	{ "davt"    ,   7,   0 }, //  DAVT, +7:00
	{ "ddut"    ,  10,   0 }, //  DDUT, +10:00
	{ "dnt"     ,   1,   0 }, //  DNT, +1:00
	{ "dst"     ,   2,   0 }, //  DST, +2:00
	{ "easst"   ,  -5,   0 }, //  EASST -5:00
	{ "east"    ,  -6,   0 }, //  EAST, -6:00
	{ "eat"     ,   3,   0 }, //  EAT, +3:00
	{ "ect"     ,  -5,   0 }, //  ECT, -5:00
	{ "edt"     ,  -4,   0 }, //  EDT, -4:00
	{ "eest"    ,   3,   0 }, //  EEST, +3:00
	{ "eet"     ,   2,   0 }, //  EET, +2:00
	{ "egst"    ,   0,   0 }, //  EGST, 0:00
	{ "egt"     ,  -1,   0 }, //  EGT, -1:00
	{ "emt"     ,   1,   0 }, //  EMT, +1:00
	{ "est"     ,  -5,   0 }, //  EST, -5:00
	{ "et"      ,  -5,   0 }, //  ET, -5:00
	{ "fdt"     ,  -1,   0 }, //  FDT, -1:00
	{ "fjst"    ,  13,   0 }, //  FJST, +13:00
	{ "fjt"     ,  12,   0 }, //  FJT, +12:00
	{ "fkst"    ,  -3,   0 }, //  FKST, -3:00
	{ "fkt"     ,  -4,   0 }, //  FKT, -4:00
	{ "fst"     ,   2,   0 }, //  FST, +2:00
	{ "fwt"     ,   1,   0 }, //  FWT, +1:00
	{ "galt"    ,  -6,   0 }, //  GALT, -6:00
	{ "gamt"    ,  -9,   0 }, //  GAMT, -9:00
	{ "gest"    ,   5,   0 }, //  GEST, +5:00
	{ "get"     ,   4,   0 }, //  GET, +4:00
	{ "gft"     ,  -3,   0 }, //  GFT, -3:00
	{ "gilt"    ,  12,   0 }, //  GILT, +12:00
	{ "gmt"     ,   0,   0 }, //  GMT, 0:00
	{ "gst"     ,  10,   0 }, //  GST, +10:00
	{ "gyt"     ,  -4,   0 }, //  GYT, -4:00
	{ "haa"     ,  -3,   0 }, //  HAA, -3:00
	{ "hac"     ,  -5,   0 }, //  HAC, -5:00
	{ "hae"     ,  -4,   0 }, //  HAE, -4:00
	{ "hap"     ,  -7,   0 }, //  HAP, -7:00
	{ "har"     ,  -6,   0 }, //  HAR, -6:00
	{ "hat"     ,  -2, -30 }, //  HAT, -2:30
	{ "hay"     ,  -8,   0 }, //  HAY, -8:00
	{ "hdt"     ,  -9, -30 }, //  HDT, -9:30
	{ "hfe"     ,   2,   0 }, //  HFE, +2:00
	{ "hfh"     ,   1,   0 }, //  HFH, +1:00
	{ "hkt"     ,   8,   0 }, //  HKT, +8:00
	{ "hna"     ,  -4,   0 }, //  HNA, -4:00
	{ "hnc"     ,  -6,   0 }, //  HNC, -6:00
	{ "hne"     ,  -5,   0 }, //  HNE, -5:00
	{ "hnp"     ,  -8,   0 }, //  HNP, -8:00
	{ "hnr"     ,  -7,   0 }, //  HNR, -7:00
	{ "hnt"     ,  -3, -30 }, //  HNT, -3:30
	{ "hny"     ,  -9,   0 }, //  HNY, -9:00
	{ "hoe"     ,   1,   0 }, //  HOE, +1:00
	{ "hst"     , -10,   0 }, //  HST, -10:00
	{ "ict"     ,   7,   0 }, //  ICT, +7:00
	{ "idle"    ,  12,   0 }, //  IDLE, +12:00
	{ "idlw"    , -12,   0 }, //  IDLW, -12:00
	{ "idt"     ,   3,   0 }, //  IDT, +3:00
	{ "iot"     ,   5,   0 }, //  IOT, +5:00
	{ "irdt"    ,   4,  30 }, //  IRDT, +4:30
	{ "irkst"   ,   9,   0 }, //  IRKST, +9:00
	{ "irkt"    ,   8,   0 }, //  IRKT, +8:00
	{ "irst"    ,   4,  30 }, //  IRST, +4:30
	{ "irt"     ,   3,  30 }, //  IRT, +3:30
	{ "ist"     ,   1,   0 }, //  IST, +1:00
	{ "it"      ,   3,  30 }, //  IT, +3:30
	{ "ita"     ,   1,   0 }, //  ITA, +1:00
	{ "javt"    ,   7,   0 }, //  JAVT, +7:00
	{ "jayt"    ,   9,   0 }, //  JAYT, +9:00
	{ "jst"     ,   9,   0 }, //  JST, +9:00
	{ "jt"      ,   7,   0 }, //  JT, +7:00
	{ "kdt"     ,  10,   0 }, //  KDT, +10:00
	{ "kgst"    ,   6,   0 }, //  KGST, +6:00
	{ "kgt"     ,   5,   0 }, //  KGT, +5:00
	{ "kost"    ,  12,   0 }, //  KOST, +12:00
	{ "krast"   ,   8,   0 }, //  KRAST, +8:00
	{ "krat"    ,   7,   0 }, //  KRAT, +7:00
	{ "kst"     ,   9,   0 }, //  KST, +9:00
	{ "lhdt"    ,  11,   0 }, //  LHDT, +11:00
	{ "lhst"    ,  10,  30 }, //  LHST, +10:30
	{ "ligt"    ,  10,   0 }, //  LIGT, +10:00
	{ "lint"    ,  14,   0 }, //  LINT, +14:00
	{ "lkt"     ,   6,   0 }, //  LKT, +6:00
	{ "magst"   ,  12,   0 }, //  MAGST, +12:00
	{ "magt"    ,  11,   0 }, //  MAGT, +11:00
	{ "mal"     ,   8,   0 }, //  MAL, +8:00
	{ "mart"    ,  -9, -30 }, //  MART, -9:30
	{ "mat"     ,   3,   0 }, //  MAT, +3:00
	{ "mawt"    ,   6,   0 }, //  MAWT, +6:00
	{ "mdt"     ,  -6,   0 }, //  MDT, -6:00
	{ "med"     ,   2,   0 }, //  MED, +2:00
	{ "medst"   ,   2,   0 }, //  MEDST, +2:00
	{ "mest"    ,   2,   0 }, //  MEST, +2:00
	{ "mesz"    ,   2,   0 }, //  MESZ, +2:00
	{ "met"     ,   1,   0 }, //  MEZ, +1:00
	{ "mewt"    ,   1,   0 }, //  MEWT, +1:00
	{ "mex"     ,  -6,   0 }, //  MEX, -6:00
	{ "mht"     ,  12,   0 }, //  MHT, +12
	{ "mmt"     ,   6,  30 }, //  MMT, +6:30
	{ "mpt"     ,  10,   0 }, //  MPT, +10:00
	{ "msd"     ,   4,   0 }, //  MSD, +4:00
	{ "msk"     ,   3,   0 }, //  MSK, +3:00
	{ "msks"    ,   4,   0 }, //  MSKS, +4:00
	{ "mst"     ,  -7,   0 }, //  MST, -7:00
	//{ "mt"      ,   8,   0 }, // MT, +8:30
	{ "mt"      ,  -7,   0 }, // MORE LIKELY MOUNTAIN TIME, -7:00
	{ "mut"     ,   4,   0 }, //  MUT, +4:00
	{ "mvt"     ,   5,   0 }, //  MVT, +5:00
	{ "myt"     ,   8,   0 }, //  MYT, +8:00
	{ "nct"     ,  11,   0 }, //  NCT, +11:00
	{ "ndt"     ,   2,  30 }, //  NDT, +2:30
	{ "nft"     ,  11,  30 }, //  NFT, +11:30
	{ "nor"     ,   1,   0 }, //  NOR, +1:00
	{ "novst"   ,   7,   0 }, //  NOVST, +7:00
	{ "novt"    ,   6,   0 }, //  NOVT, +6:00
	{ "npt"     ,   5,  45 }, //  NPT, +5:45
	{ "nrt"     ,  12,   0 }, //  NRT, +12:00
	{ "nst"     ,  -3, -30 }, //  NST, -3:30
	{ "nsut"    ,   6,  30 }, //  NSUT, +6:30
	{ "nt"      , -11,   0 }, //  NT, -11:00
	{ "nut"     , -11,   0 }, //  NUT, -11:00
	{ "nzdt"    ,  13,   0 }, //  NZDT, +13:00
	{ "nzst"    ,  12,   0 }, //  NZST, +12:00
	{ "nzt"     ,  12,   0 }, //  NZT, +12:00
	{ "oesz"    ,   3,   0 }, //  OESZ, +3:00
	{ "oez"     ,   2,   0 }, //  OEZ, +2:00
	{ "omsst"   ,   7,   0 }, //  OMSST, +7:00
	{ "omst"    ,   6,   0 }, //  OMST, +6:00
	{ "pdt"     ,  -7,   0 }, //  PDT, -7:00
	{ "pet"     ,  -5,   0 }, //  PET, -5:00
	{ "petst"   ,  13,   0 }, //  PETST, +13:00
	{ "pett"    ,  12,   0 }, //  PETT, +12:00
	{ "pgt"     ,  10,   0 }, //  PGT, +10:00
	{ "phot"    ,  13,   0 }, //  PHOT, +13:00
	{ "pht"     ,   8,   0 }, //  PHT, +8:00
	{ "pkt"     ,   5,   0 }, //  PKT, +5:00
	{ "pmdt"    ,  -2,   0 }, //  PMDT, -2:00
	{ "pmt"     ,  -3,   0 }, //  PMT, -3:00
	{ "pnt"     ,  -8, -30 }, //  PNT, -8:30
	{ "pont"    ,  11,   0 }, //  PONT, +11:00
	{ "pst"     ,  -8,   0 }, //  PST, -8:00
	{ "pt"      ,  -8,   0 }, //  PT, -8:00
	{ "pwt"     ,   9,   0 }, //  PWT, +9:00
	{ "pyst"    ,  -3,   0 }, //  PYST, -3:00
	{ "pyt"     ,  -4,   0 }, //  PYT, -4:00
	{ "r1t"     ,   2,   0 }, //  R1T, +2:00
	{ "r2t"     ,   3,   0 }, //  R2T, +3:00
	{ "ret"     ,   4,   0 }, //  RET, +4:00
	{ "rok"     ,   9,   0 }, //  ROK, +9:00
	{ "sadt"    ,  10,  30 }, //  SADT, +10:30
	{ "sast"    ,   2,   0 }, //  SAST, +2:00
	{ "sbt"     ,  11,   0 }, //  SBT, +11:00
	{ "sct"     ,   4,   0 }, //  SCT, +4:00
	{ "set"     ,   1,   0 }, //  SET, +1:00
	{ "sgt"     ,   8,   0 }, //  SGT, +8:00
	{ "srt"     ,  -3,   0 }, //  SRT, -3:00
	{ "sst"     ,   2,   0 }, //  SST, +2:00
	{ "swt"     ,   1,   0 }, //  SWT, +1:00
	{ "tft"     ,   5,   0 }, //  TFT,  +5:00
	{ "tha"     ,   7,   0 }, //  THA, +7:00
	{ "that"    , -10,   0 }, //  THAT, -10:00
	{ "tjt"     ,   5,   0 }, //  TJT, +5:00
	{ "tkt"     , -10,   0 }, //  TKT, -10:00
	{ "tmt"     ,   5,   0 }, //  TMT, +5:00
	{ "tot"     ,  13,   0 }, //  TOT, +13:00
	{ "truk"    ,  10,   0 }, //  TRUK, +10:00
	{ "tst"     ,   3,   0 }, //  TST, +3:00
	{ "tuc"     ,   0,   0 }, //  TUC, 0:00
	{ "tvt"     ,  12,   0 }, //  TVT, 12:00
	{ "ulast"   ,   9,   0 }, //  ULAST, +9:00
	{ "ulat"    ,   8,   0 }, //  ULAT, +8:00
	{ "usz1"    ,   2,   0 }, //  USZ1, +2:00
	{ "usz1s"   ,   3,   0 }, //  USZ1S, +3:00
	{ "usz2"    ,   3,   0 }, //  USZ2, +3:00
	{ "usz2s"   ,   4,   0 }, //  USZ2S, +4:00
	{ "usz3"    ,   4,   0 }, //  USZ3, +4:00
	{ "usz3s"   ,   5,   0 }, //  USZ3S, +5:00
	{ "usz4"    ,   5,   0 }, //  USZ4, +5:00
	{ "usz4s"   ,   6,   0 }, //  USZ4S, +6:00
	{ "usz5"    ,   6,   0 }, //  USZ5, +6:00
	{ "usz5s"   ,   7,   0 }, //  USZ5S, +7:00
	{ "usz6"    ,   7,   0 }, //  USZ6, +7:00
	{ "usz6s"   ,   8,   0 }, //  USZ6S, +8:00
	{ "usz7"    ,   8,   0 }, //  USZ7, +8:00
	{ "usz7s"   ,   9,   0 }, //  USZ7S, +9:00
	{ "usz8"    ,   9,   0 }, //  USZ8, +9:00
	{ "usz8s"   ,  10,   0 }, //  USZ8S, +10:00
	{ "usz9"    ,  10,   0 }, //  USZ9, +10:00
	{ "usz9s"   ,  11,   0 }, //  USZ9S, +11:00
	{ "utc"     ,   0,   0 }, //  UTC, 0:00
	{ "utz"     ,  -3,   0 }, //  UTZ, -3:00
	{ "uyt"     ,  -3,   0 }, //  UYT, -3:00
	{ "uz10"    ,  11,   0 }, //  UZ10, +11:00
	{ "uz10s"   ,  12,   0 }, //  UZ10S, +12:00
	{ "uz11"    ,  12,   0 }, //  UZ11, +12:00
	{ "uz11s"   ,  13,   0 }, //  UZ11S, +13:00
	{ "uz12"    ,  13,   0 }, //  UZ12, +13:00
	{ "uz12s"   ,  14,   0 }, //  UZ12S, +14:00
	{ "uzt"     ,   5,   0 }, //  UZT, +5:00
	{ "vet"     ,  -4,   0 }, //  VET, -4:00
	{ "vlast"   ,  11,   0 }, //  VLAST, +11:00
	{ "vlat"    ,  10,   0 }, //  VLAT, +10:00
	{ "vtz"     ,  -2,   0 }, //  VTZ, -2:00
	{ "vut"     ,  11,   0 }, //  VUT, +11:00
	{ "wakt"    ,  12,   0 }, //  WAKT, +12:00
	{ "wast"    ,   2,   0 }, //  WAST, +2:00
	{ "wat"     ,   1,   0 }, //  WAT, +1:00
	{ "west"    ,   1,   0 }, //  WEST, +1:00
	{ "wesz"    ,   1,   0 }, //  WESZ, +1:00
	{ "wet"     ,   0,   0 }, //  WET, 0:00
	{ "wez"     ,   0,   0 }, //  WEZ, 0:00
	{ "wft"     ,  12,   0 }, //  WFT, +12:00
	{ "wgst"    ,  -2,   0 }, //  WGST, -2:00
	{ "wgt"     ,  -3,   0 }, //  WGT, -3:00
	{ "wib"     ,   7,   0 }, //  WIB, +7:00
	{ "wit"     ,   9,   0 }, //  WIT, +9:00
	{ "wita"    ,   8,   0 }, //  WITA, +8:00
	{ "wst"     ,   8,   0 }, //  WST, +8:00
	{ "wtz"     ,  -1,   0 }, //  WTZ, -1:00
	{ "wut"     ,   1,   0 }, //  WUT, 1:00
	{ "yakst"   ,  10,   0 }, //  YAKST, +10:00
	{ "yakt"    ,   9,   0 }, //  YAKT, +9:00
	{ "yapt"    ,  10,   0 }, //  YAPT, +10:00
	{ "ydt"     ,  -8,   0 }, //  YDT, -8:00
	{ "yekst"   ,   6,   0 }, //  YEKST, +6:00
	{ "yst"     ,  -9,   0 }, //  YST, -9:00
	{ "\0"      ,   0,   0 }
};

// hash table of timezone information
static HashTableX s_tzt;
static GbMutex s_mtx;


static bool initTimeZoneTable ( ) {
	ScopedLock sl(s_mtx);
	// if already initalized return true
	if ( s_tzt.m_numSlotsUsed ) return true;

	// set up the time zone hashtable
	if ( ! s_tzt.set( 8,sizeof(TimeZone*), 300,NULL,0,false,0,"tzts"))
		return false;
	// load time zone names and their modifiers into hashtable
	for ( int32_t i = 0 ; *tzs[i].m_name ; i++ ) {
		const char *t    = tzs[i].m_name;
		int32_t  tlen = strlen(t);
		// hash like Words.cpp computeWordIds
		uint64_t h    = hash64Lower_utf8( t , tlen );
		// use the ptr as the value
		const TimeZone *tmp_ptr = tzs+i;
		if ( ! s_tzt.addKey ( &h, &tmp_ptr ) )
			return false;
	}
	return true;
}


// return what we have to add to UTC to get time in locale specified by "s"
// where "s" is like "PDT" "MST" "EST" etc. if unknown return 999999
int32_t getTimeZone ( const char *s ) {

	if ( ! s ) return BADTIMEZONE;
	const char *send = s;
	// point to end of the potential timezone
	for ( ; *send && isalnum(*send) ; send++ );
	// hash it
	uint64_t h = hash64Lower_utf8( s , send -s );
	// make sure table is ready
	initTimeZoneTable();
	// look it up
	int32_t slot = s_tzt.getSlot( &h );
	if ( slot < 0 )
		return BADTIMEZONE;
	// did we find it in the table?
	const TimeZone *tzptr = *(TimeZone **)s_tzt.getValueFromSlot ( slot );
	// no error, return true
	int32_t secs = tzptr->m_hourMod * 3600;
	secs += tzptr->m_minMod * 60;
	return secs;
}


void resetTimezoneTables() {
	ScopedLock sl(s_mtx);
	s_tzt.reset();
}
