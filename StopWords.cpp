// Matt Wells, copyright Jul 2001

#include "StopWords.h"
#include "gb-include.h"
#include "HashTableX.h"
#include "Speller.h"
#include "Loop.h"
#include "Lang.h"
#include "Posdb.h" // MAXLANGID
#include "GbMutex.h"
#include "ScopedLock.h"

// . h is the lower ascii 64bit hash of a word
// . this returns true if h is the hash of an ENGLISH stop word
// . list taken from www.superjournal.ac.uk/sj/application/demo/stopword.htm 
// . stop words with "mdw" next to them are ones I added


// . i shrunk this list a lot
// . see backups for the hold list
static const char * const s_stopWords[] = {
	"a",
	"b",
	"c",
	"d",
	"e",
	"f",
	"g",
	"h",
	"i",
	"j",
	"k",
	"l",
	"m",
	"n",
	"o",
	"p",
	"q",
	"r",
	"s",
	"t",
	"u",
	"v",
	"w",
	"x",
	"y",
	"z",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"an",
	"as",
	"at",
	"be",
	"by",
	"of",
	"on",
	"or",
	"do",
	"he",
	"if",
	"is",
	"it",
	"in",
	"me",
	"my",
	"re",
	"so",
	"to",
	"us",
	"vs",
	"we",
	"the",
	"and",
	"are",
	"can",
	"did",
	"per",
	"for",
//		"get",
	"had",
	"has",
	"her",
	"him",
	"its",
//              "may",	  // like the month
	// wikipedia has this in lower case in the title so we need
	// not to be a stopword
	"not", // fix 'to be or not to be'... no, revert
	"our",
	"she",
	"you",
	"also",
	"been",
	"from",
	"have",
	"here",
	"hers",
//	        "mine",   // land mine
	"ours",
	"that",
	"them",
	"then",
	"they",
	"this",
	"were",
	"will",
	"with",
	"your",
	"about",
	"above",
	//"ain",   // ain't
	"could",
	//"isn",   // isn't
	"their",
	"there",
	"these",
	"those",
	"through", // fix ceder.net "Mainstream thru A1 Dance" event title
	"thru",    // fix ceder.net "Mainstream thru A1 Dance" event title
	"until", // fix event title for blackbirdbuvette.com
	"under", // fix title for http://www.harwoodmuseum.org/press_detail.php?ID=44
	"would",
	"yours",
	"theirs",
	//"aren",  // aren't
	//"hadn",  // hadn't
	//"didn",  // didn't
	//"hasn",  // hasn'y
	//"ll",    // they'll this'll that'll you'll
	//"ve",    // would've should've
	//"should",
	//"shouldn", // shouldn't
	NULL
};
static HashTableX s_stopWordTable;
static bool       s_stopWordsInitialized = false;
static GbMutex    s_stopWordTableMutex;

static bool initWordTable(HashTableX *table, const char * const words[], const char *label) {
	// count them
	int32_t count; for ( count = 0 ; words[count] ; count++ );
	// set up the hash table
	if ( ! table->set ( 8,4,count * 2,NULL,0,false,label ) ) {
		log(LOG_INIT, "build: Could not init stop words table.");
		return false;
	}
	// now add in all the stop words
	int32_t n = count;//(int32_t)size/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		const char      *sw    = words[i];
		if ( ! sw ) break;
		int32_t       swlen = strlen ( sw );
		int64_t  swh   = hash64Lower_utf8 ( sw , swlen );
		//log("ii: #%" PRId32"  %s",i,sw);
		if ( ! table->addTerm(swh,i+1) ) return false;
	}
	return true;
}

bool isStopWord ( const char *s , int32_t len , int64_t h ) {
	ScopedLock sl(s_stopWordTableMutex);
	if ( ! s_stopWordsInitialized ) {
		s_stopWordsInitialized = 
			initWordTable(&s_stopWordTable, s_stopWords, 
				      //sizeof(s_stopWords),
				      "stopwords");
		if (!s_stopWordsInitialized) return false;
	} 
	sl.unlock();

	// . all 1 char letter words are stop words
	// . good for initials and some contractions
	if ( len == 1 && is_alpha_a(*s) ) return true;

	// get from table
	return s_stopWordTable.getScore(h);
}		

// . damn i forgot to include these above
// . i need these so m_bitScores in IndexTable.cpp doesn't have to require
//   them! Otherwise, it's like all queries have quotes around them again...

// . these have the stop words above plus some foreign stop words
// . these aren't
// . i shrunk this list a lot
// . see backups for the hold list
// . i shrunk this list a lot
// . see backups for the hold list

// langid 0 is for all languages, or when it lang is unknown, 'xx'
static const char * const s_queryStopWordsUnknown[] = {
	"at",
	//"be",
	"by",
	"of",
	"on",
	"or",
	"over",
	//"do",
	//"he",
	"if",
	"is",
	"it",
	"in",
	"into",
//		"me", this is a real stop word!
//		"my", this is a real stop word!
	"re",
//		"so", this is a real stop word!
	"to",
//		"us", this is a real stop word!
//		"vs", this is a real stop word, but i took it off down here
	//"we",
	"the",
	"and",
	//"are",
//		"can", this is a real stop word
	//"did",
	//"per", hurts 'cost per line of source code'
	"for",
//		"get",
	//"had",
	//"has",
	//"her",
	//"him",
	//"its",
//              "may",	  // like the month
//	"not", // try taking this off the list
	//"our",
	//"she",
	//"you",
	"also",
	//"been",
	"from",
	//"have",
	//"here",
	//"hers",
//	        "mine",   // land mine
	//"ours",
	//"that",
	//"them",
	//"then",
	//"they",
	//"this",
	//"were",
	//"will",
	"with",
	//"your",
	"about",
	"above",
	//"ain",   // ain't
	//"could",
	//"isn",   // isn't
	"their",
	//"there",
	//"these",
	//"those",
	//"would",
	//"yours",
	//"theirs",
	//"aren",  // aren't
	//"hadn",  // hadn't
	//"didn",  // didn't
	//"hasn",  // hasn'y
	//"ll",    // they'll this'll that'll you'll
	//"ve",    // would've should've
	//"should",
	//"shouldn", // shouldn't

	// . additional english stop words for queries
	// . we don't want to require any of these words
	// . 'second hand smoke and how it affects children' 
	//   should essentially reduce to 5 words instead of 8
	"i",		// subject,
	//"it",		// subject
	//"what",// what is the speed of sound needs this. hmmm. try removing
	//"which",	// 
	// 'who is lookup' is bad if we ignore this
	// BUT now we should reconize it as a wikipedia phrase and require it!
	//"who",		// who is president of iraq needs this i guess
	//"this",		// 
	//"that",		// 
	//"is",		// -s
	//"are",		// present
	//"was",		// 1st
	//"be",		// infinitive
	//"will",	        // 
	"a",		// 
	"an",		// 
	"the",		// 
	"and",		// 
	"or",		// 
	"as",		// 
	"of",		// 
	"at",		// 
	"by",		// 
	"for",		// 
	"with",		// 
	"about",	// 
	"to",		// 
	"from",		// 
	"in",		// 
	"on",		// 
	//"when",	        // when did martin luther king die?
	//"where",	//  // hurts 'where do tsunami occur?'
	// it was ignoring why for 'why you come to japan' and 
	// 'why is the sky blue', so take this out
	//"why",	      
	// . test 'how to make a lock pick set' if we ignore this!
	// . it should still give bonus for trigram "howtomake"
	//"how",		//  hurts 'how to tattoo' query!!

	// danish stop words (in project/stopwords)
	// cat danish.txt | awk '{print "\t\t\""$1"\",\t\t// "$3}'
	"i",		// in
	"jeg",		// I
	//"det",		// that
	//"at",		// that
	"en",		// a/an
	//"den",		// it
	"til",		// to/at/for/until/against/by/of/into,
	//"er",		// present
	//"som",		// who,
	"på",		// on/upon/in/on/at/to/after/of/with/for,
	//"de",		// they
	//"med",		// with/by/in,
	//"han",		// he
	"af",		// of/by/from/off/for/in/with/on,
	"for",		// at/for/to/from/by/of/ago,
	"ikke",		// not
	//"der",		// who/which,
	//"var",		// past
	//"mig",		// me/myself
	//"sig",		// oneself/himself/herself/itself/themselves
	//"men",		// but
	//"et",		// a/an/one,
	//"har",		// present
	//"om",		// round/about/for/in/a,
	//"vi",		// we
	//"min",		// my
	//"havde",	// past
	//"ham",		// him
	//"hun",		// she
	//"nu",		// now
	//"over",		// over/above/across/by/beyond/past/on/about,
	//"da",		// then,
	"fra",		// from/off/since,
	//"du",		// you
	//"ud",		// out
	//"sin",		// his/her/its/one's
	//"dem",		// them
	//"os",		// us/ourselves
	//"op",		// up
	//"man",		// you/one
	//"hans",		// his
	//"hvor",		// where
	"eller",	// or
	//"hvad",		// what
	//"skal",		// must/shall
	//"selv",		// myself/youself/herself/ourselves
	//"her",		// here
	//"alle",		// all/everyone/everybody
	//"vil",		// will
	//"blev",		// past
	//"kunne",	// could
	//"ind",		// in
	//"når",	// when
	//"være",	// present
	//"dog",	// however/yet/after
	//"noget",	// something
	//"ville",	// would
	//"jo",		// you
	//"deres",	// their/theirs
	//"efter",	// after/behind/according
	//"ned",	// down
	//"skulle",	// should
	//"denne",	// this
	//"end",	// than
	//"dette",	// this
	//"mit",	// my/mine
	//"også",		// also
	//"ogsa",		// also
	//"under",	// under/beneath/below/during,
	//"have",		// have
	//"dig",	// you
	//"anden",	// other
	//"hende",	// her
	//"mine",		// my
	//"alt",	// everything
	//"meget",	// much/very,
	//"sit",	// his,
	//"sine",	// his,
	//"vor",		// our
	//"mod",	// against
	//"disse",	// these
	//"hvis",	// if
	//"din",		// your/yours
	//"nogle",	// some
	"hos",		// by/at
	//"blive",	// be/become
	//"mange",	// many
	//"ad",		// by/through
	//"bliver",	// present
	//"hendes",	// her/hers
	//"været",	// be
	//"vaeret",	// be
	"thi",		// for
	//"jer",		// you
	//"sådan",	// such,

	// dutch stop words
	"de",		// the
	"en",		// and
	//"van",		// of,
	"ik",		// I,
	"te",		// (1)
	//"dat",		// that,
	//"die",		// that,
	"in",		// in,
	"een",		// a,
	//"hij",		// he
	"het",		// the,
	//"niet",		// not,
	//"zijn",		// (1)
	//"is",		// is
	//"was",		// (1)
	//"op",		// on,
	"aan",		// on,
	//"met",		// with,
	//"als",		// like,
	//"voor",		// (1)
	//"had",		// had,
	//"er",		// there
	//"maar",		// but,
	//"om",		// round,
	//"hem",		// him
	//"dan",		// then
	//"zou",		// should/would,
	"of",		// or,
	//"wat",		// what,
	//"mijn",		// possessive
	//"men",		// people,
	//"dit",		// this
	//"zo",		// so,
	//"door",		// through
	//"over",		// over,
	//"ze",		// she,
	//"zich",		// oneself
	//"bij",		// (1)
	//"ook",		// also,
	//"tot",		// till,
	//"je",		// you
	//"mij",		// me
	//"uit",		// out
	//"der",		// Old
	//"daar",		// (1)
	//"haar",		// (1)
	//"naar",		// (1)
	//"heb",		// present
	//"hoe",		// how,
	//"heeft",	// present
	"hebben",	// 'to
	//"deze",		// this
	//"u",		// you
	//"want",		// (1)
	//"nog",		// yet,
	//"zal",		// 'shall',
	//"me",		// me
	//"zij",		// she,
	//"nu",		// now
	//"ge",		// 'thou',
	//"geen",		// none
	//"omdat",	// because
	//"iets",		// something,
	"worden",	// to
	//"toch",		// yet,
	//"al",		// all,
	//"waren",	// (1)
	//"veel",		// much,
	//"meer",		// (1)
	"doen",		// to
	//"toen",		// then,
	//"moet",		// noun
	//"ben",		// (1)
	//"zonder",	// without
	//"kan",		// noun
	//"hun",		// their,
	//"dus",		// so,
	//"alles",	// all,
	//"onder",	// under,
	//"ja",		// yes,
	//"eens",		// once,
	//"hier",		// here
	//"wie",		// who
	//"werd",		// imperfect
	//"altijd",	// always
	//"doch",		// yet,
	//"wordt",	// present
	//"wezen",	// (1)
	"kunnen",	// to
	//"ons",		// us/our
	//"zelf",		// self
	//"tegen",	// against,
	//"na",		// after,
	//"reeds",	// already
	//"wil",		// (1)
	//"kon",		// could;
	//"niets",	// nothing
	//"uw",		// your
	//"iemand",	// somebody
	//"geweest",	// been;
	//"andere",	// other


	// french stop words
	//"au",		// a
	//"aux",		// a
	"avec",		// with
	//"ce",		// this
	//"ces",		// these
	"dans",		// with
	"de",		// of
	"des",		// de
	"du",		// de
	//"elle",		// she
	"en",		// `of
	//"et",		// and
	//"eux",		// them
	//"il",		// he
	"je",		// I
	//"la",		// the
	"le",		// the
	//"leur",		// their
	//"lui",		// him
	//"ma",		// my
	//"mais",		// but
	//"me",		// me
	//"même",		// same;
	//"mes",		// me
	//"moi",		// me
	//"mon",		// my
	//"ne",		// not
	//"nos",		// our
	//"notre",	// our
	//"nous",		// we
	//"on",		// one
	//"ou",		// where
	//"par",		// by
	//"pas",		// not
	//"pour",		// for
	//"qu",		// que
	//"que",		// that
	//"qui",		// who
	//"sa",		// his,
	//"se",		// oneself
	//"ses",		// his
	//"son",		// his,
	"sur",		// on
	//"ta",		// thy
	//"te",		// thee
	//"tes",		// thy
	//"toi",		// thee
	//"ton",		// thy
	//"tu",		// thou
	//"un",		// a
	"une",		// a
	//"vos",		// your
	//"votre",	// your
	//"vous",		// you

	// german stop words
	//"aber",		// but
	//"alle",		// all
	//"allem",	// 
	//"allen",	// 
	//"aller",	// 
	//"alles",	// 
	//"als",		// than,
	//"also",		// so
	"am",		// an
	"an",		// at
	//"ander",	// other
	//"andere",	// 
	//"anderem",	// 
	//"anderen",	// 
	//"anderer",	// 
	//"anderes",	// 
	//"anderm",	// 
	//"andern",	// 
	//"anderr",	// 
	//"anders",	// 
	//"auch",		// also
	"auf",		// on
	//"aus",		// out
	"bei",		// by
	//"bin",		// am
	//"bis",		// until
	//"bist",		// art
	//"da",		// there
	"damit",	// with
	//"dann",		// then
	"der",		// the
	//"den",		// 
	"des",		// 
	//"dem",		// 
	//"die",		// 
	"das",		// 
	//"daß",		// that
	"derselbe",	// the
	"derselben",	// 
	"denselben",	// 
	"desselben",	// 
	"demselben",	// 
	"dieselbe",	// 
	"dieselben",	// 
	"dasselbe",	// 
	"dazu",		// to
	//"dein",		// thy
	//"deine",	// 
	//"deinem",	// 
	//"deinen",	// 
	//"deiner",	// 
	//"deines",	// 
	//"denn",		// because
	"derer",	// of
	"dessen",	// of
	//"dich",		// thee
	//"dir",		// to
	//"du",		// thou
	//"dies",		// this
	//"diese",	// 
	//"diesem",	// 
	//"diesen",	// 
	//"dieser",	// 
	//"dieses",	// 
	//"doch",		// (several
	//"dort",		// (over)
	//"durch",	// through
	"ein",		// a
	"eine",		// 
	"einem",	// 
	"einen",	// 
	"einer",	// 
	"eines",	// 
	//"einig",	// some
	//"einige",	// 
	//"einigem",	// 
	//"einigen",	// 
	//"einiger",	// 
	//"einiges",	// 
	//"einmal",	// once
	//"er",		// he
	//"ihn",		// him
	"ihm",		// to
	"es",		// it
	//"etwas",	// something
	//"euer",		// your
	//"eure",		// 
	//"eurem",	// 
	//"euren",	// 
	//"eurer",	// 
	//"eures",	// 
	"für",		// for
	//"gegen",	// towards
	//"gewesen",	// p.p.
	//"hab",		// have
	//"habe",		// have
	//"haben",	// have
	//"hat",		// has
	//"hatte",	// had
	//"hatten",	// had
	//"hier",		// here
	//"hin",		// there
	//"hinter",	// behind
	"ich",		// I
	//"mich",		// me
	"mir",		// to
	//"ihr",		// you,
	//"ihre",		// 
	//"ihrem",	// 
	//"ihren",	// 
	//"ihrer",	// 
	//"ihres",	// 
	"euch",		// to
	//"im",		// in
	"in",		// in
	//"indem",	// while
	//"ins",		// in
	"ist",		// is
	//"jede",		// each,
	//"jedem",	// 
	//"jeden",	// 
	//"jeder",	// 
	//"jedes",	// 
	//"jene",		// that
	//"jenem",	// 
	//"jenen",	// 
	//"jener",	// 
	//"jenes",	// 
	//"jetzt",	// now
	//"kann",		// can
	//"kein",		// no
	//"keine",	// 
	//"keinem",	// 
	//"keinen",	// 
	//"keiner",	// 
	//"keines",	// 
	//"können",	// can
	//"könnte",	// could
	//"machen",	// do
	//"man",		// one
	//"manche",	// some,
	//"manchem",	// 
	//"manchen",	// 
	//"mancher",	// 
	//"manches",	// 
	//"mein",		// my
	//"meine",	// 
	//"meinem",	// 
	//"meinen",	// 
	//"meiner",	// 
	//"meines",	// 
	//"mit",		// with
	//"muss",		// must
	//"musste",	// had
	//"nach",		// to(wards)
	//"nicht",	// not
	//"nichts",	// nothing
	//"noch",		// still,
	//"nun",		// now
	//"nur",		// only
	//"ob",		// whether
	"oder",		// or
	//"ohne",		// without
	//"sehr",		// very
	//"sein",		// his
	//"seine",	// 
	//"seinem",	// 
	//"seinen",	// 
	//"seiner",	// 
	//"seines",	// 
	//"selbst",	// self
	//"sich",		// herself
	//"sie",		// they,
	"ihnen",	// to
	//"sind",		// are
	//"so",		// so
	//"solche",	// such
	//"solchem",	// 
	//"solchen",	// 
	//"solcher",	// 
	//"solches",	// 
	//"soll",		// shall
	//"sollte",	// should
	//"sondern",	// but
	//"sonst",	// else
	//"über",		// over
	//"um",		// about,
	//"und",		// and
	//"uns",		// us
	//"unse",		// 
	//"unsem",	// 
	//"unsen",	// 
	//"unser",	// 
	//"unses",	// 
	//"unter",	// under
	//"viel",		// much
	//"vom",		// von
	"von",		// from
	//"vor",		// before
	//"während",	// while
//		"war",		// was
	//"waren",	// were
	//"warst",	// wast
	//"was",		// what
	//"weg",		// away,
	//"weil",		// because
	//"weiter",	// further
	//"welche",	// which
	//"welchem",	// 
	//"welchen",	// 
	//"welcher",	// 
	//"welches",	// 
	//"wenn",		// when
	//"werde",	// will
	//"werden",	// will
	//"wie",		// how
	//"wieder",	// again
	//"will",		// want
	//"wir",		// we
	//"wird",		// will
	//"wirst",	// willst
	//"wo",		// where
	//"wollen",	// want
	//"wollte",	// wanted
	//"würde",	// would
	//"würden",	// would
	"zu",		// to
	"zum",		// zu
	"zur",		// zu
	//"zwar",		// indeed
	//"zwischen",	// between
		
	// italian stop words
	//"ad",		// a
	//"al",		// a
	//"allo",		// a
	//"ai",		// a
	//"agli",		// a
	//"all",		// a
	//"agl",		// a
	//"alla",		// a
	//"alle",		// a
	//"con",		// with
	"col",		// con
	"coi",		// con
	//"da",		// from
	"dal",		// da
	//"dallo",	// da
	"dai",		// da
	//"dagli",	// da
	//"dall",		// da
	//"dagl",		// da
	//"dalla",	// da
	//"dalle",	// da
	//"di",		// of
	"del",		// di
	//"dello",	// di
	"dei",		// di
	//"degli",	// di
	//"dell",		// di
	"degl",		// di
	//"della",	// di
	//"delle",	// di
	"in",		// in
	"nel",		// in
	//"nello",	// in
	"nei",		// in
	//"negli",	// in
	"nell",		// in
	//"negl",		// in
	//"nella",	// in
	//"nelle",	// in
	//"su",		// on
	//"sul",		// su
	//"sullo",	// su
	//"sui",		// su
	//"sugli",	// su
	//"sull",		// su
	//"sugl",		// su
	//"sulla",	// su
	//"sulle",	// su
	//"per",		// through,
	//"tra",		// among
	//"contro",	// against
	//"io",		// I
	//"tu",		// thou
	//"lui",		// he
	//"lei",		// she
	//"noi",		// we
	//"voi",		// you
	//"loro",		// they
	//"mio",		// my
	//"mia",		// 
	//"miei",		// 
	//"mie",		// 
	//"tuo",		// 
	//"tua",		// 
	//"tuoi",		// thy
	//"tue",		// 
	//"suo",		// 
	//"sua",		// 
	//"suoi",		// his,
	//"sue",		// 
	//"nostro",	// our
	//"nostra",	// 
	//"nostri",	// 
	//"nostre",	// 
	//"vostro",	// your
	//"vostra",	// 
	//"vostri",	// 
	//"vostre",	// 
	//"mi",		// me
	//"ti",		// thee
	//"ci",		// us,
	//"vi",		// you,
	//"lo",		// him,
	//"la",		// her,
	//"li",		// them
	//"le",		// them,
	"gli",		// to
	"ne",		// from
	"il",		// the
	//"un",		// a
	//"uno",		// a
	//"una",		// a
	//"ma",		// but
	"ed",		// and
	//"se",		// if
	//"perché",	// why,
	//"anche",	// also
//		"come",		// how
	//"dov",		// where
	//"dove",		// where
	//"che",		// who,
	//"chi",		// who
	//"cui",		// whom
	//"non",		// not
	//"più",		// more
	//"quale",	// who,
	//"quanto",	// how
	//"quanti",	// 
	//"quanta",	// 
	//"quante",	// 
	//"quello",	// that
	//"quelli",	// 
	//"quella",	// 
	//"quelle",	// 
	//"questo",	// this
	//"questi",	// 
	//"questa",	// 
	//"queste",	// 
	//"si",		// yes
	//"tutto",	// all
	//"tutti",	// all
	"a",		// at
	//"c",		// as
	"e",		// and
	"i",		// the
	"l",		// as
	"o",		// or
		
	// norwegian stop words
	"og",		// and
	"i",		// in
	"jeg",		// I
	//"det",		// it/this/that
	"at",		// to
	"en",		// a
	//"den",		// it/this/that
	"til",		// to
	//"er",		// is
	//"som",		// who/that
	"på",		// on
	//"de",		// they
	//"med",		// with
	//"han",		// he
	"av",		// of
	//"ikke",		// not
	//"inte",		// not
	//"der",		// there
	//"så",		// so
	//"var",		// was
	//"meg",		// me
	//"seg",		// you
	//"men",		// but
	"ett",		// a
	//"har",		// have
	//"om",		// about
	//"vi",		// we
	//"min",		// my
	//"mitt",		// my
	//"ha",		// have
	//"hade",		// had
	//"hu",		// she
	//"hun",		// she
	//"nå",		// now
	//"over",		// over
	//"da",		// when/as
	//"ved",		// by/know
	//"fra",		// from
	//"du",		// you
	//"ut",		// out
	//"sin",		// your
	//"dem",		// them
	//"oss",		// us
	//"opp",		// up
	//"man",		// you/one
	//"kan",		// can
	//"hans",		// his
	//"hvor",		// where
	"eller",	// or
	//"hva",		// what
	//"skal",		// shall/must
	//"selv",		// self
	//"sjøl",		// self
	//"her",		// here
	//"alle",		// all
	//"vil",		// will
	//"bli",		// become
	//"ble",		// became
	//"blei",		// became
	//"blitt",	// have
	//"kunne",	// could
	//"inn",		// in
	//"når",		// when
	//"være",		// be
	//"kom",		// come
	//"noen",		// some
	//"noe",		// some
	//"ville",	// would
	//"dere",		// you
	//"de",		// you
	//"som",		// who/which/that
	//"deres",	// their/theirs
	//"kun",		// only/just
	//"ja",		// yes
	//"etter",	// after
	//"ned",		// down
	//"skulle",	// should
	//"denne",	// this
	"for",		// for/because
	//"deg",		// you
	//"si",		// hers/his
	//"sine",		// hers/his
	//"sitt",		// hers/his
	//"mot",		// against
	"å",		// to
	//"meget",	// much
	//"hvorfor",	// why
	//"sia",		// since
	//"sidan",	// since
	//"dette",	// this
	//"desse",	// these/those
	//"disse",	// these/those
	//"uden",		// uten
	//"hvordan",	// how
	//"ingen",	// noone
	//"inga",		// noone
	//"din",		// your
	//"ditt",		// your
	//"blir",		// become
	//"samme",	// same
	//"hvilken",	// which
	//"hvilke",	// which
	//"sånn",		// such
	//"inni",		// inside/within
	//"mellom",	// between
	//"vår",		// our
	//"hver",		// each
	//"hvem",		// who
	//"vors",		// us/ours
	//"dere",		// their
	//"deres",	// theirs
	//"hvis",		// whose
	//"både",		// both
	//"båe",		// both
	//"begge",	// both
	//"siden",	// since
	//"dykk",		// your
	//"dykkar",	// yours
	//"dei",		// they
	//"deira",	// them
	//"deires",	// theirs
	//"deim",		// them
	//"di",		// your
	//"då",		// as/when
	"eg",		// I
	"ein",		// a/an
	"ei",		// a/an
	"eit",		// a/an
	"eitt",		// a/an
	"elles",	// or
	//"honom",	// he
	"hjå",		// at
	//"ho",		// she
	//"hoe",		// she
	//"henne",	// her
	//"hennar",	// her/hers
	//"hennes",	// hers
	//"hoss",		// how
	//"hossen",	// how
	//"ikkje",	// not
	//"ingi",		// noone
	//"inkje",	// noone
	//"korleis",	// how
	//"korso",	// how
	//"kva",		// what/which
	//"kvar",		// where
	//"kvarhelst",	// where
	//"kven",		// who/whom
	//"kvi",		// why
	//"kvifor",	// why
	//"me",		// we
	//"medan",	// while
	//"mi",		// my
	//"mine",		// my
	//"mykje",	// much
	//"no",		// now
	//"nokon",	// some
	//"noka",		// some
	//"nokor",	// some
	//"noko",		// some
	//"nokre",	// some
	//"si",		// his/hers
	//"sia",		// since
	//"sidan",	// since
	//"so",		// so
	//"somt",		// some
	//"somme",	// some
	//"um",		// about*
	//"upp",		// up
	//"vere",		// be
	//"er",		// am
	//"var",		// was
	//"vore",		// was
	//"verte",	// become
	//"vort",		// become
	//"varte",	// became
	//"vart",		// became
	//"er",		// am
	"være",		// to
	//"var",		// was
	"å",		// on


	// portuguese stop words
	"de",		// of,
	"a",		// the;
	"o",		// the;
	//"que",		// who,
	"e",		// and
	"do",		// de
	//"da",		// de 'In da club lyrics'
	"em",		// in
	//"um",		// a
	"para",		// for
	"com",		// with
	//"não",		// not,
	"uma",		// a
	//"os",		// the;
	//"no",		// em  "hurts us too much in queries"
	//"se",		// himself
	//"na",		// em
	"por",		// for
	//"mais",		// more
	"as",		// the;
	"dos",		// de
	//"como",		// how,as
	//"mas",		// but
	"ao",		// a
	//"ele",		// he
	"das",		// de
	//"à",		// a
	//"seu",		// his
	//"sua",		// her
	//"ou",		// or
	//"quando",	// when
	//"muito",	// much
	"nos",		// em
	//"já",		// already,
	//"eu",		// I
	//"também",	// also
	//"sá",		// only,
	//"pelo",		// per
	//"pela",		// per
	//"até",		// up
	//"isso",		// that
	//"ela",		// he
	//"entre",	// between
	//"depois",	// after
	//"sem",		// without
	//"mesmo",	// same
	"aos",		// a
	//"seus",		// his
	//"quem",		// whom
	"nas",		// em
	//"me",		// me
	//"esse",		// that
	//"eles",		// they
	//"você",		// you
	//"essa",		// that
	"num",		// em
	//"nem",		// nor
	//"suas",		// her
	//"meu",		// my
	"às",		// a
	//"minha",	// my
	"numa",		// em
	//"pelos",	// per
	//"elas",		// they
	//"qual",		// which
	//"nós",		// we
	"lhe",		// to
	//"deles",	// of them
	//"essas",	// those
	//"esses",	// those
	//"pelas",	// per
	//"este",		// this
	"dele",		// of
	//"tu",		// thou
	//"te",		// thee
	//"vocês",	// you
	//"vos",		// you
	"lhes",		// to
	//"meus",		// my
	//"minhas",	// 
	//"teu",		// thy
	//"tua",		// 
	//"teus",		// 
	//"tuas",		// 
	//"nosso",	// our
	//"nossa",	// 
	//"nossos",	// 
	//"nossas",	// 
	"dela",		// of
	"delas",	// of
	//"esta",		// this
	//"estes",	// these
	//"estas",	// these
	//"aquele",	// that
	//"aquela",	// that
	//"aqueles",	// those
	//"aquelas",	// those
	//"isto",		// this
	//"aquilo",	// that
	//"estou",	// 
	//"está",		// 
	//"estamos",	// 
	//"estão",	// 
	//"estive",	// 
	//"esteve",	// 
	//"estivemos",	// 
	//"estiveram",	// 
	//"estava",	// 
	//"estávamos",	// 
	//"estavam",	// 
	//"estivera",	// 
	//"estivéramos",	// 
	//"esteja",	// 
	//"estejamos",	// 
	//"estejam",	// 
	//"estivesse",	// 
	//"estivéssemos",	// 
	//"estivessem",	// 
	//"estiver",	// 
	//"estivermos",	// 
	//"estiverem",	// 

	// russian stop words
	"и",		// and
	"в",		// in/into
	"во",		// alternative
	//"не",		// not
	//"что",		// what/that
	//"он",		// he
	"на",		// on/onto
	"я",		// i
	"с",		// from
	"со",		// alternative
	//"как",		// how
	//"а",		// milder
	"то",		// conjunction
	//"все",		// all
	//"она",		// she
	//"так",		// so,
	//"его",		// him
	//"но",		// but
	//"да",		// yes/and
	//"ты",		// thou
	"к",		// towards,
	"у",		// around,
	//"же",		// intensifier
	//"вы",		// you
	//"за",		// beyond,
	//"бы",		// conditional/subj.
	//"по",		// up
	//"только",	// only
	//"ее",		// her
	"мне",		// to
	"было",		// it
	//"вот",		// here
	//"от",		// away
	//"меня",		// me
	//"еще",		// still,
	//"нет",		// no,
	"о",		// about
	//"из",		// out
	"ему",		// to
	//"теперь",	// now
	//"когда",	// when
	//"даже",		// even
	//"ну",		// so,
	//"вдруг",	// suddenly
	//"ли",		// interrogative
	//"если",		// if
	//"уже",		// already,
	"или",		// or
	//"ни",		// neither
	"быть",		// to
	//"был",		// he
	//"него",		// prepositional
	//"до",		// up
	//"вас",		// you
	//"нибудь",	// indef.
	//"опять",	// again
	//"уж",		// already,
	"вам",		// to
	//"сказал",	// he
	//"ведь",		// particle
	//"там",		// there
	//"потом",	// then
	//"себя",		// oneself
	//"ничего",	// nothing
	"ей",		// to
	//"может",	// usually
	//"они",		// they
	//"тут",		// here
	//"где",		// where
	//"есть",		// there
	//"надо",		// got
	//"ней",		// prepositional
	"для",		// for
	//"мы",		// we
	//"тебя",		// thee
	//"их",		// them,
	//"чем",		// than
	//"была",		// she
	//"сам",		// self
	"чтоб",		// in
	//"без",		// without
	//"будто",	// as
	//"человек",	// man,
	//"чего",		// genitive
	//"раз",		// once
	//"тоже",		// also
	"себе",		// to
	//"под",		// beneath
	//"жизнь",	// life
	//"будет",	// will
	//"ж",		// int16_t
	//"тогда",	// then
	//"кто",		// who
	//"этот",		// this
	//"говорил",	// was
	//"того",		// genitive
	"потому",	// for
	//"этого",	// genitive
	//"какой",	// which
	//"совсем",	// altogether
	//"ним",		// prepositional
	//"здесь",	// here
	//"этом",		// prepositional
	//"один",		// one
	//"почти",	// almost
	//"мой",		// my
	//"тем",		// instrumental/dative
	//"чтобы",	// full
	//"нее",		// her
	"кажется",	// it
	//"сейчас",	// now
	//"были",		// they
	//"куда",		// where
	//"зачем",	// why
	"сказать",	// to
	//"всех",		// all
	//"никогда",	// never
	//"сегодня",	// today
	//"можно",	// possible,
	"при",		// by
	//"наконец",	// finally
	//"два",		// two
	//"об",		// alternative
	//"другой",	// another
	//"хоть",		// even
	//"после",	// after
	//"над",		// above
	//"больше",	// more
	//"тот",		// that
	//"через",	// across,
	//"эти",		// these
	//"нас",		// us
	//"про",		// about
	"всего",	// in
	//"них",		// prepositional
	//"какая",	// which,
	//"много",	// lots
	//"разве",	// interrogative
	//"сказала",	// she
	//"три",		// three
	//"эту",		// this,
	//"моя",		// my,
	//"впрочем",	// moreover,
	//"хорошо",	// good
	//"свою",		// ones
	//"этой",		// oblique
	"перед",	// in
	//"иногда",	// sometimes
	//"лучше",	// better
	"чуть",		// a
	//"том",		// preposn.
	//"нельзя",	// one
	//"такой",	// such
	"им",		// to
	//"более",	// more
	//"всегда",	// always
	"конечно",	// of
	//"всю",		// acc.
	//"между",	// between

	// spanish stop words
	"de",		// from,
	//"la",		// the,
	//"que",		// who,
	"el",		// the
	"en",		// in
	"y",		// and
	"a",		// to
	"los",		// the,
	"del",		// de
	//"se",		// himself,
	"las",		// the,
	"por",		// for,
	//"un",		// a
	"para",		// for
	//"con",		// with
	//"no",		// no
	//"una",		// a
	//"su",		// his,
	//"al",		// a
	//"lo",		// him
	//"como",		// how
	//"más",		// more
	//"pero",		// pero
	//"sus",		// su
	"le",		// to
	//"ya",		// already
	"o",		// or
	//"este",		// this
	//"sí",		// himself
	//"porque",	// because
	//"esta",		// this
	//"entre",	// between
	//"cuando",	// when
	//"muy",		// very
	//"sin",		// without
	//"sobre",	// on
	//"también",	// also
	//"me",		// me
	//"hasta",	// until
	//"hay",		// there
	//"donde",	// where
	//"quien",	// whom,
	"desde",	// from
	//"todo",		// all
	//"nos",		// us
	//"durante",	// during
	//"todos",	// all
	//"uno",		// a
	"les",		// to
	//"ni",		// nor
	//"contra",	// against
	//"otros",	// other
	"ese",		// that
	"eso",		// that
	//"ante",		// before
	//"ellos",	// they
	"e",		// and
	//"esto",		// this
	//"mí",		// me
	//"antes",	// before
	//"algunos",	// some
	//"qué",		// what?
	"unos",		// a
	"yo",		// I
	//"otro",		// other
	//"otras",	// other
	//"otra",		// other
	//"él",		// he
	//"tanto",	// so
	//"esa",		// that
	//"estos",	// these
	//"mucho",	// much,
	//"quienes",	// who
	//"nada",		// nothing
	//"muchos",	// many
	//"cual",		// who
	//"poco",		// few
	//"ella",		// she
	"estar",	// to
	//"estas",	// these
	//"algunas",	// some
	//"algo",		// something
	//"nosotros",	// we
	//"mi",		// me
	//"mis",		// mi
	//"tú",		// thou
	//"te",		// thee
	//"ti",		// thee
	//"tu",		// thy
	//"tus",		// tu
	//"ellas",	// they
	//"nosotras",	// we
	//"vosostros",	// you
	//"vosostras",	// you
	//"os",		// you
	//"mío",		// mine
	//"mía",		// 
	//"míos",		// 
	//"mías",		// 
	//"tuyo",		// thine
	//"tuya",		// 
	//"tuyos",	// 
	//"tuyas",	// 
	//"suyo",		// his,
	//"suya",		// 
	//"suyos",	// 
	//"suyas",	// 
	//"nuestro",	// ours
	//"nuestra",	// 
	//"nuestros",	// 
	//"nuestras",	// 
	//"vuestro",	// yours
	//"vuestra",	// 
	//"vuestros",	// 
	//"vuestras",	// 
	//"esos",		// those
	//"esas",		// those
	//"estoy",	// 
	//"estás",	// 
	//"está",		// 
	//"estamos",	// 
	//"estАis",	// 
	//"están",	// 
	//"esté",		// 
	//"estés",	// 
	//"estemos",	// 
	//"estéis",	// 
	//"estén",	// 
	//"estaré",	// 
	//"estarás",	// 
	//"estará",	// 
	//"estaremos",	// 
	//"estaréis",	// 
	//"estarán",	// 
	//"estaría",	// 
	//"estarías",	// 
	//"estaríamos",	// 
	//"estaríais",	// 
	//"estarían",	// 
	//"estaba",	// 
	//"estabas",	// 
	//"estábamos",	// 
	//"estabais",	// 
	//"estaban",	// 
	//"estuve",	// 
	//"estuviste",	// 
	//"estuvo",	// 
	//"estuvimos",	// 
	//"estuvisteis",	// 
	//"estuvieron",	// 
	//"estuviera",	// 
	//"estuvieras",	// 
	//"estuviéramos",	// 
	//"estuvierais",	// 
	//"estuvieran",	// 
	//"estuviese",	// 
	//"estuvieses",	// 
	//"estuviésemos",	// 
	//"estuvieseis",	// 
	//"estuviesen",	// 
	//"estando",	// 
	//"estado",	// 
	//"estada",	// 
	//"estados",	// 
	//"estadas",	// 
	//"estad",	// 

	// swedish stop words
	"och",		// and
	//"det",		// it,
	//"att",		// to
	"i",		// in,
	"en",		// a
	//"jag",		// I
	//"hon",		// she
	//"som",		// who,
	//"han",		// he
	"på",		// on
	//"den",		// it,
	//"med",		// with
	//"var",		// where,
	//"sig",		// him(self)
	//"för",		// for (valgrind does not like, bad utf8?)
	//"så",		// so
	"till",		// to
	"är",		// is
	//"men",		// but
	"ett",		// a
	//"om",		// if;
	//"hade",		// had
	//"de",		// they,
	//"av",		// of
	//"icke",		// not,
	//"mig",		// me
	//"du",		// you
	//"henne",	// her
	//"då",		// then,
	//"sin",		// his
	//"nu",		// now
	//"har",		// have
	//"inte",		// inte
	//"hans",		// his
	//"honom",	// him
	//"skulle",	// 'sake'
	//"hennes",	// her
	//"där",		// there
	//"min",		// my
	//"man",		// one
	//"ej",		// nor
	"vid",		// at,
	//"kunde",	// could
	//"något",	// some
	"från",		// from,
	//"ut",		// out
	//"när",		// when
	//"efter",	// after,
	//"upp",		// up
	//"vi",		// we
	//"dem",		// them
	//"vara",		// be
	//"vad",		// what
	//"över",		// over
	//"än",		// than
	//"dig",		// you
	//"kan",		// can
	//"sina",		// his
	//"här",		// here
	//"ha",		// have
	//"mot",		// towards
	//"alla",		// all
	//"under",	// under
	//"någon",	// some
	"eller",	// or
	//"allt",		// all
	//"mycket",	// much
	//"sedan",	// since
	//"ju",		// why
	//"denna",	// this/that
	//"själv",	// myself,
	//"detta",	// this/that
	"åt",		// to
	//"utan",		// without
	//"varit",	// was
	//"hur",		// how
	//"ingen",	// no
	//"mitt",		// my
	//"ni",		// you
	"bli",		// to
	"blev",		// from
	//"oss",		// us
	//"din",		// thy
	//"dessa",	// these/those
	//"några",	// some
	//"deras",	// their
	"blir",		// from
	//"mina",		// my
	"samma",	// (the)
	//"vilken",	// who,
	//"er",		// you,
	//"sådan",	// such
	//"vår",		// our
	"blivit", 	// from
	//"dess",		// its
	//"inom",		// within
	//"mellan",	// between
	//"sådant",	// such
	//"varför",	// why (valgrind does not like, bad utf8?)
	//"varje",	// each
	//"vilka",	// who,
	//"ditt",		// thy
	//"vem",		// who
	//"vilket",	// who,
	//"sitta",	// his
	//"sådana",	// such
	//"vart",		// each
	//"dina",		// thy
	//"vars",		// whose
	//"vårt",		// our
	//"våra",		// our
	//"ert",		// your
	//"era",		// your
	//"vilkas",	// whose

	// internet stop words
	//"www",
	//"com"

	// additional stop words
	//"san"           // like san francisco
	NULL
};


// english is 1
static const char *s_queryStopWordsEnglish[] = {
	"at",
	"by",
	"of",
	"on",
	"or",
	"over",
	"if",
	"is",
	"it",
	"in",
	"into",
	"re",
	"to",
	"the",
	"and",
	"for",
	"also",
	"from",
	"with",
	"about",
	"above",
	"their",
	"i",
	"a",
	"an",
	"the",
	"and",
	"or",
	"as",
	"of",
	"at",
	"by",
	"for",
	"with",
	"about",
	"to",
	"from",
	"in",
	"on",
	NULL
};


static const char *s_queryStopWordsGerman[] = {
	// german stop words
	//"aber",		// but
	//"alle",		// all
	//"allem",	// 
	//"allen",	// 
	//"aller",	// 
	//"alles",	// 
	//"als",		// than,
	//"also",		// so
	"am",		// an
	"an",		// at
	//"ander",	// other
	//"andere",	// 
	//"anderem",	// 
	//"anderen",	// 
	//"anderer",	// 
	//"anderes",	// 
	//"anderm",	// 
	//"andern",	// 
	//"anderr",	// 
	//"anders",	// 
	//"auch",		// also
	"auf",		// on
	//"aus",		// out
	"bei",		// by
	//"bin",		// am
	//"bis",		// until
	//"bist",		// art
	//"da",		// there
	"damit",	// with
	//"dann",		// then
	"der",		// the
	"den",		// 
	"des",		// 
	"dem",		// 
	"die",		// 
	"das",		// 
	//"daъ",		// that
	"derselbe",	// the
	"derselben",	// 
	"denselben",	// 
	"desselben",	// 
	"demselben",	// 
	"dieselbe",	// 
	"dieselben",	// 
	"dasselbe",	// 
	"dazu",		// to
	//"dein",		// thy
	//"deine",	// 
	//"deinem",	// 
	//"deinen",	// 
	//"deiner",	// 
	//"deines",	// 
	//"denn",		// because
	"derer",	// of
	"dessen",	// of
	//"dich",		// thee
	//"dir",		// to
	//"du",		// thou
	//"dies",		// this
	//"diese",	// 
	//"diesem",	// 
	//"diesen",	// 
	//"dieser",	// 
	//"dieses",	// 
	//"doch",		// (several
	//"dort",		// (over)
	//"durch",	// through
	"ein",		// a
	"eine",		// 
	"einem",	// 
	"einen",	// 
	"einer",	// 
	"eines",	// 
	//"einig",	// some
	//"einige",	// 
	//"einigem",	// 
	//"einigen",	// 
	//"einiger",	// 
	//"einiges",	// 
	//"einmal",	// once
	//"er",		// he
	//"ihn",		// him
	"ihm",		// to
	"es",		// it
	//"etwas",	// something
	//"euer",		// your
	//"eure",		// 
	//"eurem",	// 
	//"euren",	// 
	//"eurer",	// 
	//"eures",	// 
	"fЭr",		// for
	//"gegen",	// towards
	//"gewesen",	// p.p.
	//"hab",		// have
	//"habe",		// have
	//"haben",	// have
	//"hat",		// has
	//"hatte",	// had
	//"hatten",	// had
	//"hier",		// here
	//"hin",		// there
	//"hinter",	// behind
	"ich",		// I
	//"mich",		// me
	"mir",		// to
	//"ihr",		// you,
	//"ihre",		// 
	//"ihrem",	// 
	//"ihren",	// 
	//"ihrer",	// 
	//"ihres",	// 
	"euch",		// to
	"im",		// in
	"in",		// in
	//"indem",	// while
	"ins",		// in
	"ist",		// is
	//"jede",		// each,
	//"jedem",	// 
	//"jeden",	// 
	//"jeder",	// 
	//"jedes",	// 
	//"jene",		// that
	//"jenem",	// 
	//"jenen",	// 
	//"jener",	// 
	//"jenes",	// 
	//"jetzt",	// now
	//"kann",		// can
	//"kein",		// no
	//"keine",	// 
	//"keinem",	// 
	//"keinen",	// 
	//"keiner",	// 
	//"keines",	// 
	//"kЖnnen",	// can
	//"kЖnnte",	// could
	//"machen",	// do
	//"man",		// one
	//"manche",	// some,
	//"manchem",	// 
	//"manchen",	// 
	//"mancher",	// 
	//"manches",	// 
	//"mein",		// my
	//"meine",	// 
	//"meinem",	// 
	//"meinen",	// 
	//"meiner",	// 
	//"meines",	// 
	"mit",		// with
	//"muss",		// must
	//"musste",	// had
	//"nach",		// to(wards)
	//"nicht",	// not
	//"nichts",	// nothing
	//"noch",		// still,
	//"nun",		// now
	//"nur",		// only
	//"ob",		// whether
	"oder",		// or
	//"ohne",		// without
	//"sehr",		// very
	//"sein",		// his
	//"seine",	// 
	//"seinem",	// 
	//"seinen",	// 
	//"seiner",	// 
	//"seines",	// 
	//"selbst",	// self
	//"sich",		// herself
	//"sie",		// they,
	"ihnen",	// to
	//"sind",		// are
	//"so",		// so
	//"solche",	// such
	//"solchem",	// 
	//"solchen",	// 
	//"solcher",	// 
	//"solches",	// 
	//"soll",		// shall
	//"sollte",	// should
	//"sondern",	// but
	//"sonst",	// else
	"Эber",		// over
	//"um",		// about,
	"und",		// and
	//"uns",		// us
	//"unse",		// 
	//"unsem",	// 
	//"unsen",	// 
	//"unser",	// 
	//"unses",	// 
	//"unter",	// under
	//"viel",		// much
	//"vom",		// von
	"von",		// from
	//"vor",		// before
	//"wДhrend",	// while
	//"war",		// was
	//"waren",	// were
	//"warst",	// wast
	//"was",		// what
	//"weg",		// away,
	//"weil",		// because
	//"weiter",	// further
	//"welche",	// which
	//"welchem",	// 
	//"welchen",	// 
	//"welcher",	// 
	//"welches",	// 
	//"wenn",		// when
	//"werde",	// will
	//"werden",	// will
	//"wie",		// how
	//"wieder",	// again
	//"will",		// want
	//"wir",		// we
	//"wird",		// will
	//"wirst",	// willst
	//"wo",		// where
	//"wollen",	// want
	//"wollte",	// wanted
	//"wЭrde",	// would
	//"wЭrden",	// would
	"zu",		// to
	"zum",		// zu
	"zur",	// zu
	//"zwar",		// indeed
	//"zwischen",	// between
	NULL
};


static HashTableX s_queryStopWordTables[MAXLANGID+1];
static bool       s_queryStopWordsInitialized = false;
static GbMutex    s_queryStopWordsMutex;

static const char * const * s_queryStopWords2[MAXLANGID+1];

bool isQueryStopWord ( const char *s , int32_t len , int64_t h , int32_t langId ) {

	// include a bunch of foreign prepositions so they don't get required
	// by the bitScores in IndexTable.cpp
	ScopedLock sl(s_queryStopWordsMutex);
	if ( ! s_queryStopWordsInitialized ) {
		// reset these
		for ( int32_t i = 0 ; i <= MAXLANGID ; i++ )
			s_queryStopWords2[i] = NULL;
		// now set to what we got
		s_queryStopWords2[langUnknown] = s_queryStopWordsUnknown;
		s_queryStopWords2[langEnglish] = s_queryStopWordsEnglish;
		s_queryStopWords2[langGerman ] = s_queryStopWordsGerman;
		// set up the hash table
		for ( int32_t i = 0 ; i <= MAXLANGID ; i++ ) {
			HashTableX *ht = &s_queryStopWordTables[i];
			const char * const *words = s_queryStopWords2[i];
			if ( ! words ) continue;
			if ( ! initWordTable ( ht,//&s_queryStopWordTable, 
					       words,
					       //sizeof(words),
					       "qrystops") )
				return false;
		}
		s_queryStopWordsInitialized = true;
	} 
	sl.unlock();

	// . all 1 char letter words are stop words
	// . good for initials and some contractions
	// . fix for 'j. w. eagan' .. return FALSE now
	// . let 'a' remain a query stop word i guess... (mdw 7/16/12)
	//if ( len == 1 && is_alpha_a(*s) ) return false;

	if ( langId < 0 ) langId = langUnknown;
	if ( langId > MAXLANGID ) langId = langUnknown;

	// if empty, use default table
	if ( ! s_queryStopWords2[langId] ) langId = langUnknown;

	// get from table
	return s_queryStopWordTables[langId].getScore(h);
}

// is it a stop word?
// . these have the stop words above plus some foreign stop words
// . these aren't
// . i shrunk this list a lot
// . see backups for the hold list
// . i shrunk this list a lot
// . see backups for the hold list
static const char      *s_commonWords[] = {
	"to",   // score = 1
	"and",  // score = 2
	"of",   // score = 3
	"the",  // score = 4
	"this", // score = 5
	"between",
	"onto",
	"now",
	"during",
	"after",
	"before",
	"since",
	"his",
	"more",
	"all",
	"most",
	"each",
	"other",
	"others",
	"same",
	"throughout",
	"through",
	"part",
	"being",
	"any",
	"many",
	"than",
	"within",
	"without",
	"since",
	"because",
	"whether",
	"both",
	"able",
	"prior",
	"under",
	"beneath",
	"shall",
	"around",
	"while",
	"must",
	"inside",
	"just",
	"until",
	"behind",
	"my",
	"inc",  // incorporated
	"one",
	"two",
	"three",
	"four",
	"1",
	"2",
	"3",
	"4",
	"et",
	"est",
	"against",
	"mr",
	"mrs",
	"miss",
	"out",
	"outside",
	"well",
	"only",
	"some",
	"even",
	"may",
	"still",
	"such",
	"much",
	"ever",
	"every",
	"become",
	"along",
		
	"tion", // broken words
	"ture", // broken words

	"use",
	"used",
	"using",
	"following", // the following
	"home"     ,
	"copyright",
	"tm",          // trademark
	"information",
	"info",
	"number",    // number of
	"welcome",
	"online",
	//"contact",
	"today",
	"said",
	"says",
	"say",
	"told",
	"became",
	"again",
	"later",
	"began",
	"gotta",
	"yet",
	"maybe",
	"someone",
	"something",
	"oh",
	"thanks",
	"co.uk",
	"first",
	"takes",
	"rest",
	"might",
	"never",
	"ever",
	"ok",
	"himself",
	"herself",
	"southern",
	"northern",
	"beyond",
	"saw",
	"truly",
	"turns",
	"tonight",
	"took",
	"came",
	"seeing",
	"expect",
	"arrives",
	"arrive",
	"starts",
	"recently",
	"land",
	"born",
	"ah",
	"attack",
	"kill",
	"states",
	"down",
	"up",
	"shit",
	"fuck",
	"damn",
	"wait",
	"leave",
	"exit",
	"sleep",
	"anymore",
	"presents",
	"shares",
	"wrote",
	"pleasure",
	"mention",
	"gets",
	"get",
	"feels",
	"feeling",
	"across",
	"entirely",
	"really",
	// until we add the rule to allow month/day names only
	// if adjacent to an alpha word with only a space between
	// let's get this out of there
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
	"january",
	"february",
	"march",
	"april",
	"may",
	"june",
	"july",
	"august",
	"september",
	"october",
	"november",
	"december",
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
	"sunday",
	"monday",
	"tuesday",
	"wednesday",
	"thursday",
	"friday",
	"saturday",
	// unfortunately com is portuguese for with.
	//"org",
	//"com", 
	//"central",
	//"click", chops off pay per click
	//"website",
	//"site",
	//"place",
	//"web",
	"best",
	"does",
	"see",
	"2003",
	"2004",
	"2005",
	"2006",
	"2007",
	"2008",
	"2009",
	"2010",
	"2011",
	"2012",
	"2013",
	"2014",
	"2015",

	"at",
	"be",
	"by",
	"on",
	"or",
	"do",
	"he",
	"if",
	"is",
	"it",
	"it's",
	"don't",
	"doesn't",
	"can't",
	"won't,"
	"shouldn't",
	"wouldn't",
	"couldn't",
	"should've",
	"would've",
	"could've",
	"wasn't",
	"hasn't",
	"hadn't",
	"like", // in too many gigabits
	"know", // in too many gigabits
	"you'd",
	"we'd",
	"i'd",
	"haven't",
	"he'd",
	"she'd",
	"they'd",
	"dont",
	"won't",
	"you're",
	"very",
	"seem",
	"seems",
	"thats",
	"aren't",
	"arent",
	"let's",
	"let",
	"you've",
	"they're",
	"you'll",
	"didn't",
	"i've",
	"we've",
	"they've",
	"we'll",
	"they'll",
	"i'll",
	"he'll",
	"she'll",
	"he's",
	"she's",
	"we're",
	"i'm",
	"though",
	"isn't",
	"in",
	"into",
	"me", 
	"my", 
	"re",
	"so",
	"us",
	"vs",
	"we",
	"are",
	"but",
	"over",
	"can",
	"did",
	"per",
	"for",
	"get",
	"had",
	"has",
	"her",
	"him",
	"its",
	"may",
	//	"not",
	"our",
	"she",
	"you",
	"also",
	"been",
	"from",
	"have",
	"here",
	"here's",
	"there's",
	"that's",
	"hers",
	//"mine",
	"ours",
	"that",
	"them",
	"then",
	"they",
	"were",
	"will",
	"with",
	"your",
	"about",
	"above",
	"ain",   // ain't
	"could",
	"isn",   // isn't
	"their",
	"there",
	"these",
	"those",
	"would",
	"yours",
	"theirs",
	"aren",  // aren't
	"hadn",  // hadn't
	"didn",  // didn't
	"hasn",  // hasn'y
	"ll",    // they'll this'll that'll you'll
	"ve",    // would've should've
	"should",
	"shouldn", // shouldn't

	// . additional english stop words for queries
	// . we don't want to require any of these words
	// . 'second hand smoke and how it affects children' 
	//   should essentially reduce to 5 words instead of 8
	"i",		// subject,
	"it",		// subject
	"what",		// 
	"what's",
	"which",	// 
	"who",		// common word
	"that",		// 
	"is",		// -s
	"are",		// present
	"was",		// 1st
	"be",		// infinitive
	"will",	        // 
	"a",		// 
	"an",		// 
	"or",		// 
	"as",		// 
	"at",		// 
	"by",		// 
	"for",		// 
	"with",		// 
	"about",	// 
	"from",		// 
	"in",		// 
	"on",		// 
	"when",		// 
	"where",	// 
	"why",		// common word
	"how",		// 

	"finally",
	"own",

	// danish stop words (in project/stopwords)
	// cat danish.txt | awk '{print "\t\t\""$1"\",\t\t// "$3}'
	"i",		// in
	"jeg",		// I
	"det",		// that
	"at",		// that
	"en",		// a/an
	"den",		// it
	"til",		// to/at/for/until/against/by/of/into,
	"er",		// present
	"som",		// who,
	"pЕ",		// on/upon/in/on/at/to/after/of/with/for,
	"de",		// they
	"med",		// with/by/in,
	"han",		// he
	"af",		// of/by/from/off/for/in/with/on,
	"for",		// at/for/to/from/by/of/ago,
	"ikke",		// not
	"der",		// who/which,
	"var",		// past
	"mig",		// me/myself
	"sig",		// oneself/himself/herself/itself/themselves
	"men",		// but
	"et",		// a/an/one,
	"har",		// present
	"om",		// round/about/for/in/a,
	"vi",		// we
	"min",		// my
	"havde",	// past
	"ham",		// him
	"hun",		// she
	"nu",		// now
	"over",		// over/above/across/by/beyond/past/on/about,
	"da",		// then,
	"fra",		// from/off/since,
	"du",		// you
	"ud",		// out
	"sin",		// his/her/its/one's
	"dem",		// them
	"os",		// us/ourselves
	"op",		// up
	"man",		// you/one
	"hans",		// his
	"hvor",		// where
	"eller",	// or
	"hvad",		// what
	"skal",		// must/shall
	"selv",		// myself/youself/herself/ourselves
	"her",		// here
	"alle",		// all/everyone/everybody
	"vil",		// will
	"blev",		// past
	"kunne",	// could
	"ind",		// in
	"nЕr",	// when
	"vФre",	// present
	"dog",	// however/yet/after
	"noget",	// something
	"ville",	// would
	"jo",		// you
	"deres",	// their/theirs
	"efter",	// after/behind/according
	"ned",	// down
	"skulle",	// should
	"denne",	// this
	"end",	// than
	"dette",	// this
	"mit",	// my/mine
	"ogsЕ",		// also
	"ogsa",		// also
	"under",	// under/beneath/below/during,
	"have",		// have
	"dig",	// you
	"anden",	// other
	"hende",	// her
	"mine",		// my
	"alt",	// everything
	"meget",	// much/very,
	"sit",	// his,
	"sine",	// his,
	"vor",		// our
	"mod",	// against
	"disse",	// these
	"hvis",	// if
	"din",		// your/yours
	"nogle",	// some
	"hos",		// by/at
	"blive",	// be/become
	"mange",	// many
	"ad",		// by/through
	"bliver",	// present
	"hendes",	// her/hers
	"vФret",	// be
	"vaeret",	// be
	"thi",		// for
	"jer",		// you
	"sЕdan",	// such,

	// dutch stop words
	"de",		// the
	"en",		// and
	"van",		// of,
	"ik",		// I,
	"te",		// (1)
	"dat",		// that,
	"die",		// that,
	"in",		// in,
	"een",		// a,
	"hij",		// he
	"het",		// the,
	"niet",		// not,
	"zijn",		// (1)
	"is",		// is
	"was",		// (1)
	"op",		// on,
	"aan",		// on,
	"met",		// with,
	"als",		// like,
	"voor",		// (1)
	"had",		// had,
	"er",		// there
	"maar",		// but,
	"om",		// round,
	"hem",		// him
	"dan",		// then
	"zou",		// should/would,
	"wat",		// what,
	"mijn",		// possessive
	"men",		// people,
	"dit",		// this
	"zo",		// so,
	"door",		// through
	"over",		// over,
	"ze",		// she,
	"zich",		// oneself
	"bij",		// (1)
	"ook",		// also,
	"tot",		// till,
	"je",		// you
	"mij",		// me
	"uit",		// out
	"der",		// Old
	"daar",		// (1)
	"haar",		// (1)
	"naar",		// (1)
	"heb",		// present
	"hoe",		// how,
	"heeft",	// present
	"hebben",	// 'to
	"deze",		// this
	"u",		// you
	"want",		// (1)
	"nog",		// yet,
	"zal",		// 'shall',
	"me",		// me
	"zij",		// she,
	"nu",		// now
	"ge",		// 'thou',
	"geen",		// none
	"omdat",	// because
	"iets",		// something,
	"worden",	// to
	"toch",		// yet,
	"al",		// all,
	"waren",	// (1)
	"veel",		// much,
	"meer",		// (1)
	"doen",		// to
	"toen",		// then,
	"moet",		// noun
	"ben",		// (1)
	"zonder",	// without
	"kan",		// noun
	"hun",		// their,
	"dus",		// so,
	"alles",	// all,
	"onder",	// under,
	"ja",		// yes,
	"eens",		// once,
	"hier",		// here
	"wie",		// who
	"werd",		// imperfect
	"altijd",	// always
	"doch",		// yet,
	"wordt",	// present
	"wezen",	// (1)
	"kunnen",	// to
	"ons",		// us/our
	"zelf",		// self
	"tegen",	// against,
	"na",		// after,
	"reeds",	// already
	"wil",		// (1)
	"kon",		// could;
	"niets",	// nothing
	"uw",		// your
	"iemand",	// somebody
	"geweest",	// been;
	"andere",	// other


	// french stop words
	"au",		// a
	"aux",		// a
	"avec",		// with
	"ce",		// this
	"ces",		// these
	"dans",		// with
	"de",		// of
	"des",		// de
	"du",		// de
	"elle",		// she
	"en",		// `of
	"et",		// and
	"eux",		// them
	"il",		// he
	"je",		// I
	"la",		// the
	"le",		// the
	"leur",		// their
	"lui",		// him
	"ma",		// my
	"mais",		// but
	"me",		// me
	"mЙme",		// same;
	"mes",		// me
	"moi",		// me
	"mon",		// my
	"ne",		// not
	"nos",		// our
	"notre",	// our
	"nous",		// we
	"on",		// one
	"ou",		// where
	"par",		// by
	"pas",		// not
	"pour",		// for
	"qu",		// que
	"que",		// that
	"qui",		// who
	"sa",		// his,
	"se",		// oneself
	"ses",		// his
	"son",		// his,
	"sur",		// on
	"ta",		// thy
	"te",		// thee
	"tes",		// thy
	"toi",		// thee
	"ton",		// thy
	"tu",		// thou
	"un",		// a
	"une",		// a
	"vos",		// your
	"votre",	// your
	"vous",		// you

	// german stop words
	"aber",		// but
	"alle",		// all
	"allem",	// 
	"allen",	// 
	"aller",	// 
	"alles",	// 
	"als",		// than,
	"also",		// so
	"am",		// an
	"an",		// at
	"ander",	// other
	"andere",	// 
	"anderem",	// 
	"anderen",	// 
	"anderer",	// 
	"anderes",	// 
	"anderm",	// 
	"andern",	// 
	"anderr",	// 
	"anders",	// 
	"auch",		// also
	"auf",		// on
	"aus",		// out
	"bei",		// by
	"bin",		// am
	"bis",		// until
	"bist",		// art
	"da",		// there
	"damit",	// with
	"dann",		// then
	"der",		// the
	"den",		// 
	"des",		// 
	"dem",		// 
	"die",		// 
	"das",		// 
	"daъ",		// that
	"derselbe",	// the
	"derselben",	// 
	"denselben",	// 
	"desselben",	// 
	"demselben",	// 
	"dieselbe",	// 
	"dieselben",	// 
	"dasselbe",	// 
	"dazu",		// to
	"dein",		// thy
	"deine",	// 
	"deinem",	// 
	"deinen",	// 
	"deiner",	// 
	"deines",	// 
	"denn",		// because
	"derer",	// of
	"dessen",	// of
	"dich",		// thee
	"dir",		// to
	"du",		// thou
	"dies",		// this
	"diese",	// 
	"diesem",	// 
	"diesen",	// 
	"dieser",	// 
	"dieses",	// 
	"doch",		// (several
	"dort",		// (over)
	"durch",	// through
	"ein",		// a
	"eine",		// 
	"einem",	// 
	"einen",	// 
	"einer",	// 
	"eines",	// 
	"einig",	// some
	"einige",	// 
	"einigem",	// 
	"einigen",	// 
	"einiger",	// 
	"einiges",	// 
	"einmal",	// once
	"er",		// he
	"ihn",		// him
	"ihm",		// to
	"es",		// it
	"etwas",	// something
	"euer",		// your
	"eure",		// 
	"eurem",	// 
	"euren",	// 
	"eurer",	// 
	"eures",	// 
	"fЭr",		// for
	"gegen",	// towards
	"gewesen",	// p.p.
	"hab",		// have
	"habe",		// have
	"haben",	// have
	"hat",		// has
	"hatte",	// had
	"hatten",	// had
	"hier",		// here
	"hin",		// there
	"hinter",	// behind
	"ich",		// I
	"mich",		// me
	"mir",		// to
	"ihr",		// you,
	"ihre",		// 
	"ihrem",	// 
	"ihren",	// 
	"ihrer",	// 
	"ihres",	// 
	"euch",		// to
	"im",		// in
	"in",		// in
	"indem",	// while
	"ins",		// in
	"ist",		// is
	"jede",		// each,
	"jedem",	// 
	"jeden",	// 
	"jeder",	// 
	"jedes",	// 
	"jene",		// that
	"jenem",	// 
	"jenen",	// 
	"jener",	// 
	"jenes",	// 
	"jetzt",	// now
	"kann",		// can
	"kein",		// no
	"keine",	// 
	"keinem",	// 
	"keinen",	// 
	"keiner",	// 
	"keines",	// 
	"kЖnnen",	// can
	"kЖnnte",	// could
	"machen",	// do
	"man",		// one
	"manche",	// some,
	"manchem",	// 
	"manchen",	// 
	"mancher",	// 
	"manches",	// 
	"mein",		// my
	"meine",	// 
	"meinem",	// 
	"meinen",	// 
	"meiner",	// 
	"meines",	// 
	"mit",		// with
	"muss",		// must
	"musste",	// had
	"nach",		// to(wards)
	"nicht",	// not
	"nichts",	// nothing
	"noch",		// still,
	"nun",		// now
	"nur",		// only
	"ob",		// whether
	"oder",		// or
	"ohne",		// without
	"sehr",		// very
	"sein",		// his
	"seine",	// 
	"seinem",	// 
	"seinen",	// 
	"seiner",	// 
	"seines",	// 
	"selbst",	// self
	"sich",		// herself
	"sie",		// they,
	"ihnen",	// to
	"sind",		// are
	"so",		// so
	"solche",	// such
	"solchem",	// 
	"solchen",	// 
	"solcher",	// 
	"solches",	// 
	"soll",		// shall
	"sollte",	// should
	"sondern",	// but
	"sonst",	// else
	"Эber",		// over
	"um",		// about,
	"und",		// and
	"uns",		// us
	"unse",		// 
	"unsem",	// 
	"unsen",	// 
	"unser",	// 
	"unses",	// 
	"unter",	// under
	"viel",		// much
	"vom",		// von
	"von",		// from
	"vor",		// before
	"wДhrend",	// while
//		"war",		// was
	"waren",	// were
	"warst",	// wast
	"was",		// what
	"weg",		// away,
	"weil",		// because
	"weiter",	// further
	"welche",	// which
	"welchem",	// 
	"welchen",	// 
	"welcher",	// 
	"welches",	// 
	"wenn",		// when
	"werde",	// will
	"werden",	// will
	"wie",		// how
	"wieder",	// again
	"will",		// want
	"wir",		// we
	"wird",		// will
	"wirst",	// willst
	"wo",		// where
	"wollen",	// want
	"wollte",	// wanted
	"wЭrde",	// would
	"wЭrden",	// would
	"zu",		// to
	"zum",		// zu
	"zur",		// zu
	"zwar",		// indeed
	"zwischen",	// between
		
	// italian stop words
	"ad",		// a
	"al",		// a
	"allo",		// a
	"ai",		// a
	"agli",		// a
	"all",		// a
	"agl",		// a
	"alla",		// a
	"alle",		// a
	"con",		// with
	"col",		// con
	"coi",		// con
	"da",		// from
	"dal",		// da
	"dallo",	// da
	"dai",		// da
	"dagli",	// da
	"dall",		// da
	"dagl",		// da
	"dalla",	// da
	"dalle",	// da
	"di",		// of
	"del",		// di
	"dello",	// di
	"dei",		// di
	"degli",	// di
	//"dell",		// di
	"degl",		// di
	"della",	// di
	"delle",	// di
	"in",		// in
	"nel",		// in
	"nello",	// in
	"nei",		// in
	"negli",	// in
	"nell",		// in
	"negl",		// in
	"nella",	// in
	"nelle",	// in
	"su",		// on
	"sul",		// su
	"sullo",	// su
	"sui",		// su
	"sugli",	// su
	"sull",		// su
	"sugl",		// su
	"sulla",	// su
	"sulle",	// su
	"per",		// through,
	"tra",		// among
	"contro",	// against
	"io",		// I
	"tu",		// thou
	"lui",		// he
	"lei",		// she
	"noi",		// we
	"voi",		// you
	"loro",		// they
	"mio",		// my
	"mia",		// 
	"miei",		// 
	"mie",		// 
	"tuo",		// 
	"tua",		// 
	"tuoi",		// thy
	"tue",		// 
	"suo",		// 
	"sua",		// 
	"suoi",		// his,
	"sue",		// 
	"nostro",	// our
	"nostra",	// 
	"nostri",	// 
	"nostre",	// 
	"vostro",	// your
	"vostra",	// 
	"vostri",	// 
	"vostre",	// 
	"mi",		// me
	"ti",		// thee
	"ci",		// us,
	"vi",		// you,
	"lo",		// him,
	"la",		// her,
	"li",		// them
	"le",		// them,
	"gli",		// to
	"ne",		// from
	"il",		// the
	"un",		// a
	"uno",		// a
	"una",		// a
	"ma",		// but
	"ed",		// and
	"se",		// if
	"perchИ",	// why,
	"anche",	// also
//		"come",		// how
	"dov",		// where
	"dove",		// where
	"che",		// who,
	"chi",		// who
	"cui",		// whom
	"non",		// not
	"piЫ",		// more
	"quale",	// who,
	"quanto",	// how
	"quanti",	// 
	"quanta",	// 
	"quante",	// 
	"quello",	// that
	"quelli",	// 
	"quella",	// 
	"quelle",	// 
	"questo",	// this
	"questi",	// 
	"questa",	// 
	"queste",	// 
	"si",		// yes
	"tutto",	// all
	"tutti",	// all
	"a",		// at
	"c",		// as
	"e",		// and
	"i",		// the
	"l",		// as
	"o",		// or
		
	// norwegian stop words
	"og",		// and
	"i",		// in
	"jeg",		// I
	"det",		// it/this/that
	"at",		// to
	"en",		// a
	"den",		// it/this/that
	"til",		// to
	"er",		// is
	"som",		// who/that
	"pЕ",		// on
	"de",		// they
	"med",		// with
	"han",		// he
	"av",		// of
	"ikke",		// not
	"inte",		// not
	"der",		// there
	"sЕ",		// so
	"var",		// was
	"meg",		// me
	"seg",		// you
	"men",		// but
	"ett",		// a
	"har",		// have
	"om",		// about
	"vi",		// we
	"min",		// my
	"mitt",		// my
	"ha",		// have
	"hade",		// had
	"hu",		// she
	"hun",		// she
	"nЕ",		// now
	"over",		// over
	"da",		// when/as
	"ved",		// by/know
	"fra",		// from
	"du",		// you
	"ut",		// out
	"sin",		// your
	"dem",		// them
	"oss",		// us
	"opp",		// up
	"man",		// you/one
	"kan",		// can
	"hans",		// his
	"hvor",		// where
	"eller",	// or
	"hva",		// what
	"skal",		// shall/must
	"selv",		// self
	"sjЬl",		// self
	"her",		// here
	"alle",		// all
	"vil",		// will
	"bli",		// become
	"ble",		// became
	"blei",		// became
	"blitt",	// have
	"kunne",	// could
	"inn",		// in
	"nЕr",		// when
	"vФre",		// be
	"kom",		// come
	"noen",		// some
	"noe",		// some
	"ville",	// would
	"dere",		// you
	"de",		// you
	"som",		// who/which/that
	"deres",	// their/theirs
	"kun",		// only/just
	"ja",		// yes
	"etter",	// after
	"ned",		// down
	"skulle",	// should
	"denne",	// this
	"for",		// for/because
	"deg",		// you
	"si",		// hers/his
	"sine",		// hers/his
	"sitt",		// hers/his
	"mot",		// against
	"Е",		// to
	"meget",	// much
	"hvorfor",	// why
	"sia",		// since
	"sidan",	// since
	"dette",	// this
	"desse",	// these/those
	"disse",	// these/those
	"uden",		// uten
	"hvordan",	// how
	"ingen",	// noone
	"inga",		// noone
	"din",		// your
	"ditt",		// your
	"blir",		// become
	"samme",	// same
	"hvilken",	// which
	"hvilke",	// which
	"sЕnn",		// such
	"inni",		// inside/within
	"mellom",	// between
	"vЕr",		// our
	"hver",		// each
	"hvem",		// who
	"vors",		// us/ours
	"dere",		// their
	"deres",	// theirs
	"hvis",		// whose
	"bЕde",		// both
	"bЕe",		// both
	"begge",	// both
	"siden",	// since
	"dykk",		// your
	"dykkar",	// yours
	"dei",		// they
	"deira",	// them
	"deires",	// theirs
	"deim",		// them
	"di",		// your
	"dЕ",		// as/when
	"eg",		// I
	"ein",		// a/an
	"ei",		// a/an
	"eit",		// a/an
	"eitt",		// a/an
	"elles",	// or
	"honom",	// he
	"hjЕ",		// at
	"ho",		// she
	"hoe",		// she
	"henne",	// her
	"hennar",	// her/hers
	"hennes",	// hers
	"hoss",		// how
	"hossen",	// how
	"ikkje",	// not
	"ingi",		// noone
	"inkje",	// noone
	"korleis",	// how
	"korso",	// how
	"kva",		// what/which
	"kvar",		// where
	"kvarhelst",	// where
	"kven",		// who/whom
	"kvi",		// why
	"kvifor",	// why
	"me",		// we
	"medan",	// while
	"mi",		// my
	"mine",		// my
	"mykje",	// much
	"no",		// now
	"nokon",	// some
	"noka",		// some
	"nokor",	// some
	"noko",		// some
	"nokre",	// some
	"si",		// his/hers
	"sia",		// since
	"sidan",	// since
	"so",		// so
	"somt",		// some
	"somme",	// some
	"um",		// about*
	"upp",		// up
	"vere",		// be
	"er",		// am
	"var",		// was
	"vore",		// was
	"verte",	// become
	"vort",		// become
	"varte",	// became
	"vart",		// became
	"er",		// am
	"vФre",		// to
	"var",		// was
	"Е",		// on


	// portuguese stop words
	"de",		// of,
	"a",		// the;
	"o",		// the;
	"que",		// who,
	"e",		// and
	"do",		// de
	"da",		// de
	"em",		// in
	"um",		// a
	"para",		// for
	//"com",		// with
	"nЦo",		// not,
	"uma",		// a
	"os",		// the;
	"no",		// em
	"se",		// himself
	"na",		// em
	"por",		// for
	"mais",		// more
	"as",		// the;
	"dos",		// de
	"como",		// how,as
	"mas",		// but
	"ao",		// a
	"ele",		// he
	"das",		// de
	//"Ю",		// a
	"seu",		// his
	"sua",		// her
	"ou",		// or
	"quando",	// when
	"muito",	// much
	"nos",		// em
	"jА",		// already,
	"eu",		// I
	"tambИm",	// also
	"sС",		// only,
	"pelo",		// per
	"pela",		// per
	"atИ",		// up
	"isso",		// that
	"ela",		// he
	"entre",	// between
	"depois",	// after
	"sem",		// without
	"mesmo",	// same
	"aos",		// a
	"seus",		// his
	"quem",		// whom
	"nas",		// em
	"me",		// me
	"esse",		// that
	"eles",		// they
	"vocЙ",		// you
	"essa",		// that
	"num",		// em
	"nem",		// nor
	"suas",		// her
	"meu",		// my
	"Юs",		// a
	"minha",	// my
	"numa",		// em
	"pelos",	// per
	"elas",		// they
	"qual",		// which
	"nСs",		// we
	"lhe",		// to
	"deles",	// of them
	"essas",	// those
	"esses",	// those
	"pelas",	// per
	"este",		// this
	"dele",		// of
	"tu",		// thou
	"te",		// thee
	"vocЙs",	// you
	"vos",		// you
	"lhes",		// to
	"meus",		// my
	"minhas",	// 
	"teu",		// thy
	"tua",		// 
	"teus",		// 
	"tuas",		// 
	"nosso",	// our
	"nossa",	// 
	"nossos",	// 
	"nossas",	// 
	"dela",		// of
	"delas",	// of
	"esta",		// this
	"estes",	// these
	"estas",	// these
	"aquele",	// that
	"aquela",	// that
	"aqueles",	// those
	"aquelas",	// those
	"isto",		// this
	"aquilo",	// that
	"estou",	// 
	"estА",		// 
	"estamos",	// 
	"estЦo",	// 
	"estive",	// 
	"esteve",	// 
	"estivemos",	// 
	"estiveram",	// 
	"estava",	// 
	"estАvamos",	// 
	"estavam",	// 
	"estivera",	// 
	"estivИramos",	// 
	"esteja",	// 
	"estejamos",	// 
	"estejam",	// 
	"estivesse",	// 
	"estivИssemos",	// 
	"estivessem",	// 
	"estiver",	// 
	"estivermos",	// 
	"estiverem",	// 

	// russian stop words
	"и",		// and
	"в",		// in/into
	"во",		// alternative
	"не",		// not
	"что",		// what/that
	"он",		// he
	"на",		// on/onto
	"я",		// i
	"с",		// from
	"со",		// alternative
	"как",		// how
	"а",		// milder
	"то",		// conjunction
	"все",		// all
	"она",		// she
	"так",		// so,
	"его",		// him
	"но",		// but
	"да",		// yes/and
	"ты",		// thou
	"к",		// towards,
	"у",		// around,
	"же",		// intensifier
	"вы",		// you
	"за",		// beyond,
	"бы",		// conditional/subj.
	"по",		// up
	"только",	// only
	"ее",		// her
	"мне",		// to
	"было",		// it
	"вот",		// here
	"от",		// away
	"меня",		// me
	"еще",		// still,
	"нет",		// no,
	"о",		// about
	"из",		// out
	"ему",		// to
	"теперь",	// now
	"когда",	// when
	"даже",		// even
	"ну",		// so,
	"вдруг",	// suddenly
	"ли",		// interrogative
	"если",		// if
	"уже",		// already,
	"или",		// or
	"ни",		// neither
	"быть",		// to
	"был",		// he
	"него",		// prepositional
	"до",		// up
	"вас",		// you
	"нибудь",	// indef.
	"опять",	// again
	"уж",		// already,
	"вам",		// to
	"сказал",	// he
	"ведь",		// particle
	"там",		// there
	"потом",	// then
	"себя",		// oneself
	"ничего",	// nothing
	"ей",		// to
	"может",	// usually
	"они",		// they
	"тут",		// here
	"где",		// where
	"есть",		// there
	"надо",		// got
	"ней",		// prepositional
	"для",		// for
	"мы",		// we
	"тебя",		// thee
	"их",		// them,
	"чем",		// than
	"была",		// she
	"сам",		// self
	"чтоб",		// in
	"без",		// without
	"будто",	// as
	"человек",	// man,
	"чего",		// genitive
	"раз",		// once
	"тоже",		// also
	"себе",		// to
	"под",		// beneath
	"жизнь",	// life
	"будет",	// will
	"ж",		// int16_t
	"тогда",	// then
	"кто",		// who
	"этот",		// this
	"говорил",	// was
	"того",		// genitive
	"потому",	// for
	"этого",	// genitive
	"какой",	// which
	"совсем",	// altogether
	"ним",		// prepositional
	"здесь",	// here
	"этом",		// prepositional
	"один",		// one
	"почти",	// almost
	"мой",		// my
	"тем",		// instrumental/dative
	"чтобы",	// full
	"нее",		// her
	"кажется",	// it
	"сейчас",	// now
	"были",		// they
	"куда",		// where
	"зачем",	// why
	"сказать",	// to
	"всех",		// all
	"никогда",	// never
	"сегодня",	// today
	"можно",	// possible,
	"при",		// by
	"наконец",	// finally
	"два",		// two
	"об",		// alternative
	"другой",	// another
	"хоть",		// even
	"после",	// after
	"над",		// above
	"больше",	// more
	"тот",		// that
	"через",	// across,
	"эти",		// these
	"нас",		// us
	"про",		// about
	"всего",	// in
	"них",		// prepositional
	"какая",	// which,
	"много",	// lots
	"разве",	// interrogative
	"сказала",	// she
	"три",		// three
	"эту",		// this,
	"моя",		// my,
	"впрочем",	// moreover,
	"хорошо",	// good
	"свою",		// ones
	"этой",		// oblique
	"перед",	// in
	"иногда",	// sometimes
	"лучше",	// better
	"чуть",		// a
	"том",		// preposn.
	"нельзя",	// one
	"такой",	// such
	"им",		// to
	"более",	// more
	"всегда",	// always
	"конечно",	// of
	"всю",		// acc.
	"между",	// between

	// spanish stop words
	"de",		// from,
	"la",		// the,
	"que",		// who,
	"el",		// the
	"en",		// in
	"y",		// and
	"a",		// to
	//"los",		// the,
	"del",		// de
	"se",		// himself,
	"las",		// the,
	"por",		// for,
	"un",		// a
	"para",		// for
	"con",		// with
	"no",		// no
	"una",		// a
	"su",		// his,
	"al",		// a
	"lo",		// him
	"como",		// how
	"mАs",		// more
	"pero",		// pero
	"sus",		// su
	"le",		// to
	"ya",		// already
	"o",		// or
	"este",		// this
	"sМ",		// himself
	"porque",	// because
	"esta",		// this
	"entre",	// between
	"cuando",	// when
	"muy",		// very
	"sin",		// without
	"sobre",	// on
	"tambiИn",	// also
	"me",		// me
	"hasta",	// until
	"hay",		// there
	"donde",	// where
	"quien",	// whom,
	"desde",	// from
	"todo",		// all
	"nos",		// us
	"durante",	// during
	"todos",	// all
	"uno",		// a
	"les",		// to
	"ni",		// nor
	"contra",	// against
	"otros",	// other
	"ese",		// that
	"eso",		// that
	"ante",		// before
	"ellos",	// they
	"e",		// and
	"esto",		// this
	"mМ",		// me
	"antes",	// before
	"algunos",	// some
	"quИ",		// what?
	"unos",		// a
	"yo",		// I
	"otro",		// other
	"otras",	// other
	"otra",		// other
	"Иl",		// he
	"tanto",	// so
	"esa",		// that
	"estos",	// these
	"mucho",	// much,
	"quienes",	// who
	"nada",		// nothing
	"muchos",	// many
	"cual",		// who
	"poco",		// few
	"ella",		// she
	"estar",	// to
	"estas",	// these
	"algunas",	// some
	"algo",		// something
	"nosotros",	// we
	"mi",		// me
	"mis",		// mi
	"tЗ",		// thou
	"te",		// thee
	"ti",		// thee
	"tu",		// thy
	"tus",		// tu
	"ellas",	// they
	"nosotras",	// we
	"vosostros",	// you
	"vosostras",	// you
	"os",		// you
	"mМo",		// mine
	"mМa",		// 
	"mМos",		// 
	"mМas",		// 
	"tuyo",		// thine
	"tuya",		// 
	"tuyos",	// 
	"tuyas",	// 
	"suyo",		// his,
	"suya",		// 
	"suyos",	// 
	"suyas",	// 
	"nuestro",	// ours
	"nuestra",	// 
	"nuestros",	// 
	"nuestras",	// 
	"vuestro",	// yours
	"vuestra",	// 
	"vuestros",	// 
	"vuestras",	// 
	"esos",		// those
	"esas",		// those
	"estoy",	// 
	"estАs",	// 
	"estА",		// 
	"estamos",	// 
	"estАis",	// 
	"estАn",	// 
	"estИ",		// 
	"estИs",	// 
	"estemos",	// 
	"estИis",	// 
	"estИn",	// 
	"estarИ",	// 
	"estarАs",	// 
	"estarА",	// 
	"estaremos",	// 
	"estarИis",	// 
	"estarАn",	// 
	"estarМa",	// 
	"estarМas",	// 
	"estarМamos",	// 
	"estarМais",	// 
	"estarМan",	// 
	"estaba",	// 
	"estabas",	// 
	"estАbamos",	// 
	"estabais",	// 
	"estaban",	// 
	"estuve",	// 
	"estuviste",	// 
	"estuvo",	// 
	"estuvimos",	// 
	"estuvisteis",	// 
	"estuvieron",	// 
	"estuviera",	// 
	"estuvieras",	// 
	"estuviИramos",	// 
	"estuvierais",	// 
	"estuvieran",	// 
	"estuviese",	// 
	"estuvieses",	// 
	"estuviИsemos",	// 
	"estuvieseis",	// 
	"estuviesen",	// 
	"estando",	// 
	"estado",	// 
	"estada",	// 
	"estados",	// 
	"estadas",	// 
	"estad",	// 

	// swedish stop words
	"och",		// and
	"det",		// it,
	"att",		// to
	"i",		// in,
	"en",		// a
	"jag",		// I
	"hon",		// she
	"som",		// who,
	"han",		// he
	"pЕ",		// on
	"den",		// it,
	"med",		// with
	"var",		// where,
	"sig",		// him(self)
	//"fЖr",		// for
	"sЕ",		// so
	"till",		// to
	"Дr",		// is
	"men",		// but
	"ett",		// a
	"om",		// if;
	"hade",		// had
	"de",		// they,
	"av",		// of
	"icke",		// not,
	"mig",		// me
	"du",		// you
	"henne",	// her
	"dЕ",		// then,
	"sin",		// his
	"nu",		// now
	"har",		// have
	"inte",		// inte
	"hans",		// his
	"honom",	// him
	"skulle",	// 'sake'
	"hennes",	// her
	"dДr",		// there
	"min",		// my
	"man",		// one
	"ej",		// nor
	"vid",		// at,
	"kunde",	// could
	"nЕgot",	// some
	"frЕn",		// from,
	"ut",		// out
	"nДr",		// when
	"efter",	// after,
	"upp",		// up
	"vi",		// we
	"dem",		// them
	"vara",		// be
	"vad",		// what
	"Жver",		// over
	"Дn",		// than
	"dig",		// you
	"kan",		// can
	"sina",		// his
	"hДr",		// here
	"ha",		// have
	"mot",		// towards
	"alla",		// all
	"under",	// under
	"nЕgon",	// some
	"eller",	// or
	"allt",		// all
	"mycket",	// much
	"sedan",	// since
	"ju",		// why
	"denna",	// this/that
	"sjДlv",	// myself,
	"detta",	// this/that
	"Еt",		// to
	"utan",		// without
	"varit",	// was
	"hur",		// how
	"ingen",	// no
	"mitt",		// my
	"ni",		// you
	"bli",		// to
	"blev",		// from
	"oss",		// us
	"din",		// thy
	"dessa",	// these/those
	"nЕgra",	// some
	"deras",	// their
	"blir",		// from
	"mina",		// my
	"samma",	// (the)
	"vilken",	// who,
	"er",		// you,
	"sЕdan",	// such
	"vЕr",		// our
	"blivit",	// from
	"dess",		// its
	"inom",		// within
	"mellan",	// between
	"sЕdant",	// such
	//"varfЖr",	// why
	"varje",	// each
	"vilka",	// who,
	"ditt",		// thy
	"vem",		// who
	"vilket",	// who,
	"sitta",	// his
	"sЕdana",	// such
	"vart",		// each
	"dina",		// thy
	"vars",		// whose
	"vЕrt",		// our
	"vЕra",		// our
	"ert",		// your
	"era",		// your
	"vilkas",	// whose

	// internet stop words
	"www",
	//"com",

	// additional stop words
	//"san"           // like san francisco
};
static HashTableX s_commonWordTable;
static bool       s_commonWordsInitialized = false;
static GbMutex s_commonWordtableMutex;

// for Process.cpp::resetAll() to call when exiting to free all mem
void resetStopWordTables() {
	s_stopWordTable.reset();
	for ( int i = 0 ; i <= MAXLANGID ; i++ )
		s_queryStopWordTables[i].reset();
	s_commonWordTable.reset();
}

// used by Msg24.cpp for gigabits generation
int32_t isCommonWord ( int64_t h ) {
	
	ScopedLock sl(s_commonWordtableMutex);
	// include a bunch of foreign prepositions so they don't get required
	// by the bitScores in IndexTable.cpp
	if ( ! s_commonWordsInitialized ) {
		// set up the hash table
		if ( ! s_commonWordTable.set (8,4,sizeof(s_commonWords)*2, NULL,0,false,"commonwrds") ) {
			log(LOG_INIT, "query: Could not init common words table.");
			return 0;
		}
		// now add in all the stop words
		int32_t n = (int32_t)sizeof(s_commonWords)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			const char *sw    = s_commonWords[i];
			int32_t  swlen = strlen ( sw );
			// use the same algo that Words.cpp computeWordIds does
			int64_t swh = hash64Lower_utf8 ( sw , swlen );
			if ( ! s_commonWordTable.addTerm(swh,i+1 ) )
				return 0;
			// . add w/o accent marks too!
			// . skip "fЭr" though because fur is an eng. word
			//if ( *sw=='f' && *(sw+1)=='Э' &&
			//     *(sw+2)=='r' && swlen == 3 ) continue;
			//swh   = hash64AsciiLower ( sw , swlen );
			//s_commonWordTable.addTerm(swh,i+1,i+1,true);
		}
		s_commonWordsInitialized = true;
	} 
	sl.unlock();

	// . all 1 char letter words are stop words
	// . good for initials and some contractions
	//if ( len == 1 && is_alpha_a(*s) ) return true;

	// get from table
	return s_commonWordTable.getScore(h);
}

