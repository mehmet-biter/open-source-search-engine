#include "Timezone.h"
#include "HashTableX.h"
#include "Mem.h"

TimeZone tzs[] = {
	{ "acdt"    ,  10,  30, 1 }, //  ACDT, +10:30
	{ "acst"    ,   9,  30, 1 }, //  ACST, +9:30
	{ "adt"     ,  -3,   0, 1 }, //  ADT, -3:00
	{ "aedt"    ,  11,   0, 1 }, //  AEDT, +11:00
	{ "aest"    ,  10,   0, 1 }, //  AEST, +10:00
	{ "aft"     ,   4,  30, 1 }, //  AFT, +4:30
	{ "ahdt"    ,  -9,   0, 1 }, //  AHDT, -9:00 - historical?
	{ "ahst"    , -10,   0, 1 }, //  AHST, -10:00 - historical?
	{ "akdt"    ,  -8,   0, 1 }, //  AKDT, -8:00
	{ "akst"    ,  -9,   0, 1 }, //  AKST, -9:00
	{ "amst"    ,   4,   0, 1 }, //  AMST, +4:00
	{ "amt"     ,   4,   0, 1 }, //  AMT, +4:00
	{ "anast"   ,  13,   0, 1 }, //  ANAST, +13:00
	{ "anat"    ,  12,   0, 1 }, //  ANAT, +12:00
	{ "art"     ,  -3,   0, 1 }, //  ART, -3:00
	{ "ast"     ,  -4,   0, 1 }, //  AST, -4:00
	{ "at"      ,  -1,   0, 1 }, //  AT, -1:00
	{ "awst"    ,   8,   0, 1 }, //  AWST, +8:00
	{ "azost"   ,   0,   0, 1 }, //  AZOST, 0:00
	{ "azot"    ,  -1,   0, 1 }, //  AZOT, -1:00
	{ "azst"    ,   5,   0, 1 }, //  AZST, +5:00
	{ "azt"     ,   4,   0, 1 }, //  AZT, +4:00
	{ "badt"    ,   4,   0, 1 }, //  BADT, +4:00
	{ "bat"     ,   6,   0, 1 }, //  BAT, +6:00
	{ "bdst"    ,   2,   0, 1 }, //  BDST, +2:00
	{ "bdt"     ,   6,   0, 1 }, //  BDT, +6:00
	{ "bet"     , -11,   0, 1 }, //  BET, -11:00
	{ "bnt"     ,   8,   0, 1 }, //  BNT, +8:00
	{ "bort"    ,   8,   0, 1 }, //  BORT, +8:00
	{ "bot"     ,  -4,   0, 1 }, //  BOT, -4:00
	{ "bra"     ,  -3,   0, 1 }, //  BRA, -3:00
	{ "bst"     ,   1,   0, 1 }, //  BST, +1:00
	{ "bt"      ,   6,   0, 1 }, //  BT, +6:00
	{ "btt"     ,   6,   0, 1 }, //  BTT, +6:00
	{ "cat"     ,   2,   0, 1 }, //  CAT, +2:00
	{ "cct"     ,   8,   0, 1 }, //  CCT, +8:00
	{ "cdt"     ,  -5,   0, 1 }, //  CDT, -5:00
	{ "cest"    ,   2,   0, 1 }, //  CEST, +2:00
	{ "cet"     ,   1,   0, 1 }, //  CET, +1:00
	{ "chadt"   ,  13,  45, 1 }, //  CHADT, +13:45
	{ "chast"   ,  12,  45, 1 }, //  CHAST, +12:45
	{ "chst"    ,  10,   0, 1 }, //  CHST, +10:00
	{ "ckt"     , -10,   0, 1 }, //  CKT, -10:00
	{ "clst"    ,  -3,   0, 1 }, //  CLST, -3:00
	{ "clt"     ,  -4,   0, 1 }, //  CLT, -4:00
	{ "cot"     ,  -5,   0, 1 }, //  COT, -5:00
	{ "cst"     ,  -6,   0, 1 }, //  CST, -6:00
	{ "ct"      ,  -6,   0, 1 }, //  CT, -6:00
	{ "cut"     ,   0,   0, 2 }, //  CUT, 0:00
	{ "cxt"     ,   7,   0, 1 }, //  CXT, +7:00
	{ "davt"    ,   7,   0, 1 }, //  DAVT, +7:00
	{ "ddut"    ,  10,   0, 1 }, //  DDUT, +10:00
	{ "dnt"     ,   1,   0, 1 }, //  DNT, +1:00
	{ "dst"     ,   2,   0, 1 }, //  DST, +2:00
	{ "easst"   ,  -5,   0, 1 }, //  EASST -5:00
	{ "east"    ,  -6,   0, 1 }, //  EAST, -6:00
	{ "eat"     ,   3,   0, 1 }, //  EAT, +3:00
	{ "ect"     ,  -5,   0, 1 }, //  ECT, -5:00
	{ "edt"     ,  -4,   0, 1 }, //  EDT, -4:00
	{ "eest"    ,   3,   0, 1 }, //  EEST, +3:00
	{ "eet"     ,   2,   0, 1 }, //  EET, +2:00
	{ "egst"    ,   0,   0, 1 }, //  EGST, 0:00
	{ "egt"     ,  -1,   0, 1 }, //  EGT, -1:00
	{ "emt"     ,   1,   0, 1 }, //  EMT, +1:00
	{ "est"     ,  -5,   0, 1 }, //  EST, -5:00
	{ "et"      ,  -5,   0, 1 }, //  ET, -5:00
	{ "fdt"     ,  -1,   0, 1 }, //  FDT, -1:00
	{ "fjst"    ,  13,   0, 1 }, //  FJST, +13:00
	{ "fjt"     ,  12,   0, 1 }, //  FJT, +12:00
	{ "fkst"    ,  -3,   0, 1 }, //  FKST, -3:00
	{ "fkt"     ,  -4,   0, 1 }, //  FKT, -4:00
	{ "fst"     ,   2,   0, 1 }, //  FST, +2:00
	{ "fwt"     ,   1,   0, 1 }, //  FWT, +1:00
	{ "galt"    ,  -6,   0, 1 }, //  GALT, -6:00
	{ "gamt"    ,  -9,   0, 1 }, //  GAMT, -9:00
	{ "gest"    ,   5,   0, 1 }, //  GEST, +5:00
	{ "get"     ,   4,   0, 1 }, //  GET, +4:00
	{ "gft"     ,  -3,   0, 1 }, //  GFT, -3:00
	{ "gilt"    ,  12,   0, 1 }, //  GILT, +12:00
	{ "gmt"     ,   0,   0, 2 }, //  GMT, 0:00
	{ "gst"     ,  10,   0, 1 }, //  GST, +10:00
	{ "gt"      ,   0,   0, 2 }, //  GT, 0:00
	{ "gyt"     ,  -4,   0, 1 }, //  GYT, -4:00
	{ "gz"      ,   0,   0, 2 }, //  GZ, 0:00
	{ "haa"     ,  -3,   0, 1 }, //  HAA, -3:00
	{ "hac"     ,  -5,   0, 1 }, //  HAC, -5:00
	{ "hae"     ,  -4,   0, 1 }, //  HAE, -4:00
	{ "hap"     ,  -7,   0, 1 }, //  HAP, -7:00
	{ "har"     ,  -6,   0, 1 }, //  HAR, -6:00
	{ "hat"     ,  -2, -30, 1 }, //  HAT, -2:30
	{ "hay"     ,  -8,   0, 1 }, //  HAY, -8:00
	{ "hdt"     ,  -9, -30, 1 }, //  HDT, -9:30
	{ "hfe"     ,   2,   0, 1 }, //  HFE, +2:00
	{ "hfh"     ,   1,   0, 1 }, //  HFH, +1:00
	{ "hg"      ,   0,   0, 2 }, //  HG, 0:00
	{ "hkt"     ,   8,   0, 1 }, //  HKT, +8:00
	{ "hna"     ,  -4,   0, 1 }, //  HNA, -4:00
	{ "hnc"     ,  -6,   0, 1 }, //  HNC, -6:00
	{ "hne"     ,  -5,   0, 1 }, //  HNE, -5:00
	{ "hnp"     ,  -8,   0, 1 }, //  HNP, -8:00
	{ "hnr"     ,  -7,   0, 1 }, //  HNR, -7:00
	{ "hnt"     ,  -3, -30, 1 }, //  HNT, -3:30
	{ "hny"     ,  -9,   0, 1 }, //  HNY, -9:00
	{ "hoe"     ,   1,   0, 1 }, //  HOE, +1:00
	{ "hours"   ,   0,   0, 2 }, //  HOURS, no change, but indicates time
	{ "hrs"     ,   0,   0, 2 }, //  HRS, no change, but indicates time
	{ "hst"     , -10,   0, 1 }, //  HST, -10:00
	{ "ict"     ,   7,   0, 1 }, //  ICT, +7:00
	{ "idle"    ,  12,   0, 1 }, //  IDLE, +12:00
	{ "idlw"    , -12,   0, 1 }, //  IDLW, -12:00
	{ "idt"     ,   3,   0, 1 }, //  IDT, +3:00
	{ "iot"     ,   5,   0, 1 }, //  IOT, +5:00
	{ "irdt"    ,   4,  30, 1 }, //  IRDT, +4:30
	{ "irkst"   ,   9,   0, 1 }, //  IRKST, +9:00
	{ "irkt"    ,   8,   0, 1 }, //  IRKT, +8:00
	{ "irst"    ,   4,  30, 1 }, //  IRST, +4:30
	{ "irt"     ,   3,  30, 1 }, //  IRT, +3:30
	{ "ist"     ,   1,   0, 1 }, //  IST, +1:00
	{ "it"      ,   3,  30, 1 }, //  IT, +3:30
	{ "ita"     ,   1,   0, 1 }, //  ITA, +1:00
	{ "javt"    ,   7,   0, 1 }, //  JAVT, +7:00
	{ "jayt"    ,   9,   0, 1 }, //  JAYT, +9:00
	{ "jst"     ,   9,   0, 1 }, //  JST, +9:00
	{ "jt"      ,   7,   0, 1 }, //  JT, +7:00
	{ "kdt"     ,  10,   0, 1 }, //  KDT, +10:00
	{ "kgst"    ,   6,   0, 1 }, //  KGST, +6:00
	{ "kgt"     ,   5,   0, 1 }, //  KGT, +5:00
	{ "kost"    ,  12,   0, 1 }, //  KOST, +12:00
	{ "krast"   ,   8,   0, 1 }, //  KRAST, +8:00
	{ "krat"    ,   7,   0, 1 }, //  KRAT, +7:00
	{ "kst"     ,   9,   0, 1 }, //  KST, +9:00
	{ "lhdt"    ,  11,   0, 1 }, //  LHDT, +11:00
	{ "lhst"    ,  10,  30, 1 }, //  LHST, +10:30
	{ "ligt"    ,  10,   0, 1 }, //  LIGT, +10:00
	{ "lint"    ,  14,   0, 1 }, //  LINT, +14:00
	{ "lkt"     ,   6,   0, 1 }, //  LKT, +6:00
	{ "magst"   ,  12,   0, 1 }, //  MAGST, +12:00
	{ "magt"    ,  11,   0, 1 }, //  MAGT, +11:00
	{ "mal"     ,   8,   0, 1 }, //  MAL, +8:00
	{ "mart"    ,  -9, -30, 1 }, //  MART, -9:30
	{ "mat"     ,   3,   0, 1 }, //  MAT, +3:00
	{ "mawt"    ,   6,   0, 1 }, //  MAWT, +6:00
	{ "mdt"     ,  -6,   0, 1 }, //  MDT, -6:00
	{ "med"     ,   2,   0, 1 }, //  MED, +2:00
	{ "medst"   ,   2,   0, 1 }, //  MEDST, +2:00
	{ "mest"    ,   2,   0, 1 }, //  MEST, +2:00
	{ "mesz"    ,   2,   0, 1 }, //  MESZ, +2:00
	{ "met"     ,   1,   0, 1 }, //  MEZ, +1:00
	{ "mewt"    ,   1,   0, 1 }, //  MEWT, +1:00
	{ "mex"     ,  -6,   0, 1 }, //  MEX, -6:00
	{ "mht"     ,  12,   0, 1 }, //  MHT, +12
	{ "mmt"     ,   6,  30, 1 }, //  MMT, +6:30
	{ "mpt"     ,  10,   0, 1 }, //  MPT, +10:00
	{ "msd"     ,   4,   0, 1 }, //  MSD, +4:00
	{ "msk"     ,   3,   0, 1 }, //  MSK, +3:00
	{ "msks"    ,   4,   0, 1 }, //  MSKS, +4:00
	{ "mst"     ,  -7,   0, 1 }, //  MST, -7:00
	//{ "mt"      ,   8,  30, 1 }, // MT, +8:30
	{ "mt"      ,  -7,   0, 1 }, // MORE LIKELY MOUNTAIN TIME, -7:00
	{ "mut"     ,   4,   0, 1 }, //  MUT, +4:00
	{ "mvt"     ,   5,   0, 1 }, //  MVT, +5:00
	{ "myt"     ,   8,   0, 1 }, //  MYT, +8:00
	{ "nct"     ,  11,   0, 1 }, //  NCT, +11:00
	{ "ndt"     ,   2,  30, 1 }, //  NDT, +2:30
	{ "nft"     ,  11,  30, 1 }, //  NFT, +11:30
	{ "nor"     ,   1,   0, 1 }, //  NOR, +1:00
	{ "novst"   ,   7,   0, 1 }, //  NOVST, +7:00
	{ "novt"    ,   6,   0, 1 }, //  NOVT, +6:00
	{ "npt"     ,   5,  45, 1 }, //  NPT, +5:45
	{ "nrt"     ,  12,   0, 1 }, //  NRT, +12:00
	{ "nst"     ,  -3, -30, 1 }, //  NST, -3:30
	{ "nsut"    ,   6,  30, 1 }, //  NSUT, +6:30
	{ "nt"      , -11,   0, 1 }, //  NT, -11:00
	{ "nut"     , -11,   0, 1 }, //  NUT, -11:00
	{ "nzdt"    ,  13,   0, 1 }, //  NZDT, +13:00
	{ "nzst"    ,  12,   0, 1 }, //  NZST, +12:00
	{ "nzt"     ,  12,   0, 1 }, //  NZT, +12:00
	{ "oesz"    ,   3,   0, 1 }, //  OESZ, +3:00
	{ "oez"     ,   2,   0, 1 }, //  OEZ, +2:00
	{ "omsst"   ,   7,   0, 1 }, //  OMSST, +7:00
	{ "omst"    ,   6,   0, 1 }, //  OMST, +6:00
	{ "pdt"     ,  -7,   0, 1 }, //  PDT, -7:00
	{ "pet"     ,  -5,   0, 1 }, //  PET, -5:00
	{ "petst"   ,  13,   0, 1 }, //  PETST, +13:00
	{ "pett"    ,  12,   0, 1 }, //  PETT, +12:00
	{ "pgt"     ,  10,   0, 1 }, //  PGT, +10:00
	{ "phot"    ,  13,   0, 1 }, //  PHOT, +13:00
	{ "pht"     ,   8,   0, 1 }, //  PHT, +8:00
	{ "pkt"     ,   5,   0, 1 }, //  PKT, +5:00
	{ "pmdt"    ,  -2,   0, 1 }, //  PMDT, -2:00
	{ "pmt"     ,  -3,   0, 1 }, //  PMT, -3:00
	{ "pnt"     ,  -8, -30, 1 }, //  PNT, -8:30
	{ "pont"    ,  11,   0, 1 }, //  PONT, +11:00
	{ "pst"     ,  -8,   0, 1 }, //  PST, -8:00
	{ "pt"      ,  -8,   0, 1 }, //  PT, -8:00
	{ "pwt"     ,   9,   0, 1 }, //  PWT, +9:00
	{ "pyst"    ,  -3,   0, 1 }, //  PYST, -3:00
	{ "pyt"     ,  -4,   0, 1 }, //  PYT, -4:00
	{ "r1t"     ,   2,   0, 1 }, //  R1T, +2:00
	{ "r2t"     ,   3,   0, 1 }, //  R2T, +3:00
	{ "ret"     ,   4,   0, 1 }, //  RET, +4:00
	{ "rok"     ,   9,   0, 1 }, //  ROK, +9:00
	{ "sadt"    ,  10,  30, 1 }, //  SADT, +10:30
	{ "sast"    ,   2,   0, 1 }, //  SAST, +2:00
	{ "sbt"     ,  11,   0, 1 }, //  SBT, +11:00
	{ "sct"     ,   4,   0, 1 }, //  SCT, +4:00
	{ "set"     ,   1,   0, 1 }, //  SET, +1:00
	{ "sgt"     ,   8,   0, 1 }, //  SGT, +8:00
	{ "srt"     ,  -3,   0, 1 }, //  SRT, -3:00
	{ "sst"     ,   2,   0, 1 }, //  SST, +2:00
	{ "swt"     ,   1,   0, 1 }, //  SWT, +1:00
	{ "tft"     ,   5,   0, 1 }, //  TFT,  +5:00
	{ "tha"     ,   7,   0, 1 }, //  THA, +7:00
	{ "that"    , -10,   0, 1 }, //  THAT, -10:00
	{ "tjt"     ,   5,   0, 1 }, //  TJT, +5:00
	{ "tkt"     , -10,   0, 1 }, //  TKT, -10:00
	{ "tmt"     ,   5,   0, 1 }, //  TMT, +5:00
	{ "tot"     ,  13,   0, 1 }, //  TOT, +13:00
	{ "truk"    ,  10,   0, 1 }, //  TRUK, +10:00
	{ "tst"     ,   3,   0, 1 }, //  TST, +3:00
	{ "tuc"     ,   0,   0, 1 }, //  TUC, 0:00
	{ "tvt"     ,  12,   0, 1 }, //  TVT, 12:00
	{ "ulast"   ,   9,   0, 1 }, //  ULAST, +9:00
	{ "ulat"    ,   8,   0, 1 }, //  ULAT, +8:00
	{ "usz1"    ,   2,   0, 1 }, //  USZ1, +2:00
	{ "usz1s"   ,   3,   0, 1 }, //  USZ1S, +3:00
	{ "usz2"    ,   3,   0, 1 }, //  USZ2, +3:00
	{ "usz2s"   ,   4,   0, 1 }, //  USZ2S, +4:00
	{ "usz3"    ,   4,   0, 1 }, //  USZ3, +4:00
	{ "usz3s"   ,   5,   0, 1 }, //  USZ3S, +5:00
	{ "usz4"    ,   5,   0, 1 }, //  USZ4, +5:00
	{ "usz4s"   ,   6,   0, 1 }, //  USZ4S, +6:00
	{ "usz5"    ,   6,   0, 1 }, //  USZ5, +6:00
	{ "usz5s"   ,   7,   0, 1 }, //  USZ5S, +7:00
	{ "usz6"    ,   7,   0, 1 }, //  USZ6, +7:00
	{ "usz6s"   ,   8,   0, 1 }, //  USZ6S, +8:00
	{ "usz7"    ,   8,   0, 1 }, //  USZ7, +8:00
	{ "usz7s"   ,   9,   0, 1 }, //  USZ7S, +9:00
	{ "usz8"    ,   9,   0, 1 }, //  USZ8, +9:00
	{ "usz8s"   ,  10,   0, 1 }, //  USZ8S, +10:00
	{ "usz9"    ,  10,   0, 1 }, //  USZ9, +10:00
	{ "usz9s"   ,  11,   0, 1 }, //  USZ9S, +11:00
	{ "utc"     ,   0,   0, 2 }, //  UTC, 0:00
	{ "utz"     ,  -3,   0, 1 }, //  UTZ, -3:00
	{ "uyt"     ,  -3,   0, 1 }, //  UYT, -3:00
	{ "uz10"    ,  11,   0, 1 }, //  UZ10, +11:00
	{ "uz10s"   ,  12,   0, 1 }, //  UZ10S, +12:00
	{ "uz11"    ,  12,   0, 1 }, //  UZ11, +12:00
	{ "uz11s"   ,  13,   0, 1 }, //  UZ11S, +13:00
	{ "uz12"    ,  13,   0, 1 }, //  UZ12, +13:00
	{ "uz12s"   ,  14,   0, 1 }, //  UZ12S, +14:00
	{ "uzt"     ,   5,   0, 1 }, //  UZT, +5:00
	{ "vet"     ,  -4,   0, 1 }, //  VET, -4:00
	{ "vlast"   ,  11,   0, 1 }, //  VLAST, +11:00
	{ "vlat"    ,  10,   0, 1 }, //  VLAT, +10:00
	{ "vtz"     ,  -2,   0, 1 }, //  VTZ, -2:00
	{ "vut"     ,  11,   0, 1 }, //  VUT, +11:00
	{ "wakt"    ,  12,   0, 1 }, //  WAKT, +12:00
	{ "wast"    ,   2,   0, 1 }, //  WAST, +2:00
	{ "wat"     ,   1,   0, 1 }, //  WAT, +1:00
	{ "west"    ,   1,   0, 1 }, //  WEST, +1:00
	{ "wesz"    ,   1,   0, 1 }, //  WESZ, +1:00
	{ "wet"     ,   0,   0, 1 }, //  WET, 0:00
	{ "wez"     ,   0,   0, 1 }, //  WEZ, 0:00
	{ "wft"     ,  12,   0, 1 }, //  WFT, +12:00
	{ "wgst"    ,  -2,   0, 1 }, //  WGST, -2:00
	{ "wgt"     ,  -3,   0, 1 }, //  WGT, -3:00
	{ "wib"     ,   7,   0, 1 }, //  WIB, +7:00
	{ "wit"     ,   9,   0, 1 }, //  WIT, +9:00
	{ "wita"    ,   8,   0, 1 }, //  WITA, +8:00
	{ "wst"     ,   8,   0, 1 }, //  WST, +8:00
	{ "wtz"     ,  -1,   0, 1 }, //  WTZ, -1:00
	{ "wut"     ,   1,   0, 1 }, //  WUT, 1:00
	{ "yakst"   ,  10,   0, 1 }, //  YAKST, +10:00
	{ "yakt"    ,   9,   0, 1 }, //  YAKT, +9:00
	{ "yapt"    ,  10,   0, 1 }, //  YAPT, +10:00
	{ "ydt"     ,  -8,   0, 1 }, //  YDT, -8:00
	{ "yekst"   ,   6,   0, 1 }, //  YEKST, +6:00
	{ "yst"     ,  -9,   0, 1 }, //  YST, -9:00
	{ "\0"      ,   0,   0, 0 } };

// hash table of timezone information
static HashTableX s_tzt;

static int64_t h_mountain;
static int64_t h_eastern;
static int64_t h_central;
static int64_t h_pacific;
static int64_t h_time2;
static int64_t h_mdt;
static int64_t h_at2;

static bool initTimeZoneTable ( ) {

	// if already initalized return true
	if ( s_tzt.m_numSlotsUsed ) return true;

	// init static wids
	h_mountain = hash64n("mountain");
	h_eastern  = hash64n("eastern");
	h_central  = hash64n("central");
	h_pacific  = hash64n("pacific");
	h_time2    = hash64n("time");
	h_mdt      = hash64n("mdt");
	h_at2      = hash64n("at");
	// set up the time zone hashtable
	if ( ! s_tzt.set( 8,4, 300,NULL,0,false,0,"tzts"))
		return false;
	// load time zone names and their modifiers into hashtable
	for ( int32_t i = 0 ; *tzs[i].m_name ; i++ ) {
		char *t    = tzs[i].m_name;
		int32_t  tlen = gbstrlen(t);
		// hash like Words.cpp computeWordIds
		uint64_t h    = hash64Lower_utf8( t , tlen );
		// use the ptr as the value
		if ( ! s_tzt.addKey ( &h, &tzs[i] ) )
			return false;
	}
	return true;
}

// return what we have to add to UTC to get time in locale specified by "s"
// where "s" is like "PDT" "MST" "EST" etc. if unknown return 999999
int32_t getTimeZone ( char *s ) {
	if ( ! s ) return BADTIMEZONE;
	char *send = s;
	// point to end of the potential timezone
	for ( ; *send && isalnum(*send) ; send++ );
	// hash it
	uint64_t h = hash64Lower_utf8( s , send -s );
	// make sure table is ready
	initTimeZoneTable();
	// look it up
	int32_t slot = s_tzt.getSlot( &h );
	if ( slot < 0 ) return 999999;
	// did we find it in the table?
	TimeZone *tzptr = (TimeZone *)s_tzt.getValueFromSlot ( slot );
	// no error, return true
	int32_t secs = tzptr->m_hourMod * 3600;
	secs += tzptr->m_minMod * 60;
	return secs;
}

// . returns how many words starting at i are in the time zone
// . 0 means not a timezone
int32_t getTimeZoneWord ( int32_t i ,
		       int64_t *wids, 
		       int32_t nw ,
		       TimeZone **tzptr , 
		       int32_t niceness ) {

	// no ptr
	*tzptr = NULL;
	// only init table once
	bool s_init16 = false;
	// init the hash table of month names
	if ( ! s_init16 ) {
		// on error we return -1 from here
		if ( ! initTimeZoneTable() ) return -1;
		s_init16 = true;
	}
	// this is too common of a word!
	if ( wids[i] == h_at2 ) return 0;

	int32_t slot = s_tzt.getSlot( &wids[i] );
	// return this, assume just one word
	int32_t tznw = 1;
	// . "mountain time"
	// . this removes the event title "M-F 8:30 AM-5:30 PM Mountain Time"
	//   from the event (horus) on http://www.sfreporter.com/contact_us/
	if ( slot<0 && i+2<nw && wids[i+2] == h_time2 ) {
		if ( wids[i] == h_mountain ) {
			slot = s_tzt.getSlot (&h_mdt);
			tznw = 3;
		}
		if ( wids[i] == h_eastern ) {
			slot = s_tzt.getSlot (&h_eastern);
			tznw = 3;
		}
		if ( wids[i] == h_central ) {
			slot = s_tzt.getSlot (&h_central);
			tznw = 3;
		}
		if ( wids[i] == h_pacific ) {
			slot = s_tzt.getSlot (&h_pacific);
			tznw = 3;
		}
	}
	// if nothing return 0
	if ( slot <0 ) return 0;
	// did we find it in the table?
	*tzptr = (TimeZone *)s_tzt.getValueFromSlot ( slot );
	// no error, return true
	return tznw;
}


void resetTimezoneTables() {
	s_tzt.reset();
}
