#include "gb-include.h"

#include "Mem.h"
#include "Conf.h"
#include "Dns.h"
#include "HttpServer.h"
#include "Loop.h"

#include "Speller.h"
#include <stdio.h>
#include <ctype.h>

Speller g_speller;

Speller::Speller(){
}

Speller::~Speller(){
	reset();
}

bool Speller::init(){
	static bool s_init = false;
	if ( s_init ) return true;
	s_init = true;

	if ( !loadUnifiedDict() )
		return log("spell: Could not load unified dict from "
			   "unifiedDict-buf.txt and unifiedDict-map.dat");

	// this seems to slow our startup way down!!!
	log("speller: turning off spell checking for now");
	return true;
}

void Speller::reset(){
	m_unifiedBuf.purge();
	m_unifiedDict.reset();
}

// test it.
void Speller::test ( char *ff ) {
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd ) {
		log("speller: test: Could not open %s for "
		    "reading: %s.", ff,strerror(errno));
		return;
	}

	char buf[1026];
	//char dst[1026];
	// go through the words in dict/words
	while ( fgets ( buf , MAX_FRAG_SIZE , fd ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		Query q;
		q.set2 ( buf , langUnknown , false );
	}
	fclose(fd);
}

bool Speller::generateDicts ( int32_t numWordsToDump , char *coll ){
	//m_language[2].setLang(2);
	//m_language[2].generateDicts ( numWordsToDump, coll );
	return false;
}

// The unified dict is the combination of the word list, title rec and the top
// query dict of all languages. It has to be created by loading each languages
// dict into memory using Language.loadWordList(), loadTitleRecDict(), etc
bool Speller::loadUnifiedDict() {

	bool building = false;

 reload:

	bool needRebuild = false;

	m_unifiedBuf.purge();
	m_unifiedBuf.setLabel("unibuf");

	// this MUST be there
	if ( m_unifiedBuf.fillFromFile(g_hostdb.m_dir,
				       "unifiedDict-buf.txt" ) == 0 ) 
		needRebuild = true;

	// . give it a million slots
	// . unified dict currently has 1340223 entries
	m_unifiedDict.set ( 8,4, 2*1024*1024,NULL,0,false,0,"udictht");	

	// try to load in the hashtable and the buffer directly
	if ( ! m_unifiedDict.load(g_hostdb.m_dir,"unifiedDict-map.dat"))
		needRebuild = true;

	if ( ! needRebuild ) {
		// convert unifiedBuf \n's to \0's
		char *start = m_unifiedBuf.getBufStart();
		char *end   = start + m_unifiedBuf.length();
		for ( char *p = start ; p < end ; p++ )
			if ( *p == '\n' ) *p = '\0';
		log(LOG_DEBUG,"speller: done loading successfully");

		// a quick little checksum
		if ( ! g_conf.m_isLive ) return true;

		// the size
		int64_t h1 = m_unifiedDict.getNumSlotsUsed();
		int64_t h2 = m_unifiedBuf .length();
		int64_t h = hash64 ( h1 , h2 );
		char *tail1 = (char *)m_unifiedDict.m_keys;
		char *tail2 = m_unifiedBuf.getBufStart()+h2-1000;
		h = hash64 ( tail1 , 1000 , h );
		h = hash64 ( tail2 , 1000 , h );
		//int64_t n = 8346765853685546681LL;
		int64_t n = -14450509118443930LL;
		if ( h != n ) {
			log("gb: unifiedDict-buf.txt or "
			    "unifiedDict-map.dat "
			    "checksum is not approved for "
			    "live service (%" PRId64" != %" PRId64")" ,h,n);
			//return false;
		}

		return true;
	}

	if ( building ) {
		log("gb: rebuild failed. exiting.");
		exit(0);
	}

	building = true;

	log("gb: REBUILDING unifiedDict-buf.txt and unifiedDict-map.dat");

	// just in case that was there and the buf wasn't
	m_unifiedDict.clear();
	// or vice versa
	m_unifiedBuf.purge();

	// load the .txt file. this is REQUIRED for rebuild
	SafeBuf ub;
	if ( ub.fillFromFile (g_hostdb.m_dir,"unifiedDict.txt") <= 0 )
		return false;

	//
	// change \n to \0
	// TODO: filter out the first word from each line?
	//
	char *start = ub.getBufStart();
	char *end   = start + ub.length();
	for ( char *p = start ; p < end ; p++ )
		if ( *p == '\n' ) *p = '\0';


	// now scan wikitionary file wiktionary-lang.txt to get even
	// more words! this file is generated from Wiktionary.cpp when
	// it scans the wiktionary xml dump to generate the other
	// wiktionary-syns.dat and wiktionary-buf.txt files. it also
	// cranks this file out because we can use it since we do not
	// have czech in the unifiedDict.txt file.
	SafeBuf wkfBuf;
	if ( wkfBuf.fillFromFile ( g_hostdb.m_dir,"wiktionary-lang.txt") <= 0 )
		return false;

	// scan each line
	char *p = wkfBuf.getBufStart();
	char *pend = p + wkfBuf.length();
	HashTableX wkfMap;
	// true = allow dups. because same word can appear in multiple langs
	if ( ! wkfMap.set ( 8,1,1000000,NULL,0,true,0,"wkfmap") )
		return false;

	// "fr|livre" is how it's formatted
	for ( ; p && p < pend ; p = wkfBuf.getNextLine(p) ) {
		char *start = p;
		// skip til |
		for ( ; *p && *p != '|' ; p++ );
		// sanity check
		if ( *p != '|' ) { char *xx=NULL;*xx=0; }
		// tmp NULL that
		*p = '\0';
		char langId = getLangIdFromAbbr(start);
		// revert
		*p = '|';
		if ( langId == langUnknown )
			continue;
		if ( langId == langTranslingual )
			continue;
		// skip |
		p++;
		// that's the word
		char *word = p;
		// find end
		char *end = p;
		for ( ; *end && *end != '\n' ; end++ ) ;
		// so hash it up
		int64_t wid = hash64d ( word , end - word );
		// debug point
		//if ( wid == 5000864073612302341LL )
		//	log("download");
		// add it to map
		if ( ! wkfMap.addKey ( &wid , &langId ) ) return false;
	}



	//
	// scan unifiedDict.txt file
	//
	int32_t totalCollisions = 0;
	uint64_t atline = 0;
	p = start;
	while ( p < end ) {
		atline++;
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase
		while ( *p != '\t' )
			p++;
		// Null end the phrase
		*p = '\0';

		// skip empty phrases
		if(gbstrlen(phrase) < 1) {
			log(LOG_WARN,
				"spell: Got zero length entry in unifiedDict "
			    "at line %" PRIu64", skipping\n",
				atline);
			p += gbstrlen(p) + 1;
			continue;
		}

		// skip single byte words that are not alphabetic
		// Anything over 'Z' is likely unicode, so don't bother
		if(gbstrlen(phrase) == 1 && (phrase[0] < 'a')) {
			log(LOG_WARN,
				"spell: Got questionable entry in "
			    "unifiedDict at line %" PRIu64", skipping: %s\n",
				atline,p);
			p += gbstrlen(p) + 1;
			continue;
		}
		// . i need to move everything over to utf8!!!
		// . this is the same hash function used by Words.cpp so that
		p++;
		// phonet
		char *phonet = p;
		// next is the phonet
		while ( *p != '\t' )
			p++;
		// Null end the phonet
		*p = '\0';
		p++;

		uint64_t key = hash64d(phrase,gbstrlen(phrase));

		// make sure we haven't added this word/phrase yet
		if ( m_unifiedDict.isInTable ( &key ) ) {
			totalCollisions++;
			p += gbstrlen(p) + 1;
			continue;
		}

		// reset lang vector
		int64_t pops[MAX_LANGUAGES];
		memset ( pops , 0 , MAX_LANGUAGES * 8 );

		// see how many langs this key is in in unifiedDict.txt file
		char *phraseRec = p;
		getPhraseLanguages2 ( phraseRec , pops );

		// make all pops positive if it has > 1 lang already
		//int32_t count = 0;
		//for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ )
		//	if ( pops[i] ) count++;

		int32_t imax = MAX_LANGUAGES;
		//if ( count <= 1 ) imax = 0;
		// assume none are in official dict
		// seems like nanny messed things up, so undo that
		// and set it negative if in wiktionary in loop below
		for ( int32_t i = 0 ; i < imax ; i++ )
			// HOWEVER, if it is -1 leave it be, i think it
			// was probably correct in that case for some reason.
			// Wiktionary fails to get a TON of forms for
			// many foreign languages in the english dict.
			// so nanny got these from some dict, so try to
			// keep them.
			// like 'abelhudo'
			// http://pt.wiktionary.org/wiki/abelhudo
			// and is not in en.wiktionary.org
			// . NO! because it has "ein" as english with
			//   a -1 popularity as well as "ist"! reconsider
			if ( pops[i] < -1 ) pops[i] *= -1;

		// now add in from wiktionary
		int32_t slot = wkfMap.getSlot ( &key );
		for ( ; slot >= 0 ; slot = wkfMap.getNextSlot(slot,&key) ) {
			uint8_t langId = *(char *)wkfMap.getDataFromSlot(slot);
			if ( langId == langUnknown ) continue;
			if ( langId == langTranslingual ) continue;
			// if it marked as already in that dictionary, cont
			if ( pops[langId] < 0 ) continue;
			// if it is positive, make it negative to mark
			// it as being in the official dictionary
			// -1 means pop unknown but in dictionary
			if ( pops[langId] == 0 ) pops[langId]  = -1;
			else                     pops[langId] *= -1;
		}

		// save the offset
		int32_t offset = m_unifiedBuf.length();

		// print the word/phrase and its phonet, if any
		m_unifiedBuf.safePrintf("%s\t%s\t",phrase,phonet);

		int32_t count = 0;
		// print the languages and their popularity scores
		for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ ) {
			if ( pops[i] == 0 ) continue;
			// skip "unknown" what does that really mean?
			if ( i == 0 ) continue;
			m_unifiedBuf.safePrintf("%" PRId32"\t%" PRId32"\t",
						i,(int32_t)pops[i]);
			count++;
		}
		// if none, revert
		if ( count == 0 ) {
			m_unifiedBuf.setLength(offset);
			// skip "p" to next line in unifiedBuf.txt
			p += gbstrlen(p) + 1;
			continue;
		}

		// trim final tab i guess
		m_unifiedBuf.incrementLength(-1);
		// end line
		m_unifiedBuf.pushChar('\n');

		// directly point to the (lang, score) tuples
		m_unifiedDict.addKey(&key, &offset);

		// skip "p" to next line in unifiedBuf.txt
		p += gbstrlen(p) + 1;
	}

	log (LOG_WARN,"spell: got %" PRId32" TOTAL collisions in unified dict",
	     totalCollisions);

	HashTableX dedup;
	dedup.set(8,0,1000000,NULL,0,false,0,"dmdm");

	// . now add entries from wkfBuf that were not also in "ub"
	// . format is "<langAbbr>|<word>\n"
	p = wkfBuf.getBufStart();
	end = p + wkfBuf.length();
	for ( ; p ; p = wkfBuf.getNextLine(p) ) {
		//char *langAbbr = p;
		for ( ; *p && *p !='\n' && *p !='|' ; p++ );
		if ( *p != '|' ) {
			log("speller: bad format in wiktionary-lang.txt");
			char *xx=NULL;*xx=0;
		}
		//*p = '\0';
		//uint8_t langId = getLangIdFromAbbr ( langAbbr );
		//*p = '|';
		// get word
		char *word = p + 1;
		// get end of it
		for ( ; *p && *p !='\n' ; p++ );
		if ( *p != '\n' ) {
			log("speller: bad format in wiktionary-lang.txt");
			char *xx=NULL;*xx=0;
		}
		int32_t wordLen = p - word;
		// wiktinary has like prefixes ending in minus. skip!
		if ( word[wordLen-1] == '-' ) continue;
		// suffix in wiktionary? skip
		if ( word[0] == '-' ) continue;
		// .zr .dd
		if ( word[0] == '.' ) continue;

		// hash the word
		int64_t key = hash64d ( word , wordLen );

		// skip if we did it in the above loop
		if ( m_unifiedDict.isInTable ( &key ) ) continue;

		// skip if already did it in this loop
		if ( dedup.isInTable ( &key ) ) continue;
		if ( ! dedup.addKey ( &key ) ) return false;

		// reset lang vector
		int64_t pops[MAX_LANGUAGES];
		memset ( pops , 0 , MAX_LANGUAGES * 8 );

		// now add in from wiktionary map
		int32_t slot = wkfMap.getSlot ( &key );
		for ( ; slot >= 0 ; slot = wkfMap.getNextSlot(slot,&key) ) {
			uint8_t langId = *(char *)wkfMap.getDataFromSlot(slot);
			if ( langId == langUnknown ) continue;
			if ( langId == langTranslingual ) continue;
			if ( pops[langId] ) continue;
			// -1 means pop unknown but in dictionary
			pops[langId] = -1;
		}

		
		// save the offset
		int32_t offset = m_unifiedBuf.length();

		// . print the word/phrase and its phonet, if any
		// . phonet is unknown here...
		//char *phonet = "";
		m_unifiedBuf.safeMemcpy ( word, wordLen );
		m_unifiedBuf.safePrintf("\t\t");//word,phonet); 

		int32_t count = 0;
		// print the languages and their popularity scores
		for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ ) {
			if ( pops[i] == 0 ) continue;
			// skip "unknown" what does that really mean?
			if ( i == 0 ) continue;
			m_unifiedBuf.safePrintf("%" PRId32"\t%" PRId32"\t",
						i,(int32_t)pops[i]);
			count++;
		}
		// if none, revert
		if ( count == 0 ) {
			m_unifiedBuf.setLength(offset);
			continue;
		}

		// trim final tab i guess
		m_unifiedBuf.incrementLength(-1);
		// end line
		m_unifiedBuf.pushChar('\n');

		// directly point to the (lang, score) tuples
		m_unifiedDict.addKey(&key, &offset);

	}

	// save the text too! a merge of unifiedDict.txt and
	// wiktionary-lang.txt!!!
	if ( m_unifiedBuf.saveToFile(g_hostdb.m_dir,"unifiedDict-buf.txt") <=0)
		return false;

	// save it
	if ( m_unifiedDict.save(g_hostdb.m_dir,"unifiedDict-map.dat")<=0 )
		return false;

	// start over and load what we created
	goto reload;

}

// in case the language is unknown, just give the pop of the
// first found language
int32_t Speller::getPhrasePopularity( const char *str, uint64_t h, unsigned char langId ) {
	//char *xx=NULL;*xx=0;

	// hack fixes. 
	// common word like "and"?
	if ( isCommonWord(h) ) return MAX_PHRASE_POP;
	// another common word check
	if ( isQueryStopWord(NULL,0,h,langId) ) return MAX_PHRASE_POP;
	// single letter?
	if ( str && str[0] && str[1] == '\0' ) return MAX_PHRASE_POP;
	// 0-99 only
	if ( str && is_digit(*str) ) {
		if ( !str[1]) return MAX_PHRASE_POP;
		if ( is_digit(str[1])&& !str[2]) return MAX_PHRASE_POP;
	}

	// what up with this?
	//if ( !s ) return 0;
	int32_t slot = m_unifiedDict.getSlot(&h);
	// if not in dictionary assume 0 popularity
	if ( slot == -1 ) return 0;
	//char *p = *(char **)m_unifiedDict.getValueFromSlot(slot);
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	char *pend = p + gbstrlen(p);

	// skip word itself
	while ( *p != '\t' ) p++;
	p++;
	// skip phonet, if any
	while ( *p != '\t' ) p++;
	p++;

	int32_t max = 0;

	// the tuples are in ascending order of the langid
	// get to the right language
	while ( p < pend ){

		int32_t currLang = atoi(p);

		// the the pops are sorted by langId, return 0 if the lang
		// was not found
		if ( langId != langUnknown && currLang > langId )
			return 0;
			
		// skip language
		while ( *p != '\t' ) p++;
		p++;

		int32_t score = atoi(p);

		// i think negative scores mean it is only from titlerec and
		// not in any of the dictionaries.
		if ( score < 0 )
			score *= -1;

		if ( currLang == langId && langId != langUnknown )
			return score;

		// if lang is unknown get max
		if ( score > max ) max = score;

		// skip that score and go to the next <lang> <pop> tuple
		while ( *p != '\t' && *p != '\0' ) p++;
		p++;

	}
	return max;
}

// splits words and checks if they form a porn word or not. montanalinux.org 
// is showing up as porn because it has 'anal' in the hostname. So try to
// find a combination of words such that they are NOT porn.
// try this only after isAdult() succeeds.
// Always tries to find longer words first. so 'montanalinux' is split as
// 'montana' and 'linux' and not as 'mont', 'analinux'
// if it finds a seq of words leading upto a porn word, then it returns true
// eg. shall split montanalinux into 'mont', 'anal', and return true without
// checking if 'inux' is a word. Need to do this because isAdult() cannot
// define where an adult word has ended. 
// TODO: chatswingers.com NOT identified as porn because it is split as 
// 'chats' and 'wingers'.

bool Speller::canSplitWords( char *s, int32_t slen, bool *isPorn, char *splitWords, unsigned char langId ) {
	*isPorn = false;
	char *index[1024];
	if ( slen == 0 )
		return true;
	*splitWords = '\0';

	// this is the current word we're on
	int32_t curr = 0;
	index[curr++] = s;
	index[curr] = s + slen;
	while ( curr > 0 ){
		char *nextWord = NULL;
		while (findNext( index[curr - 1], index[curr], &nextWord, isPorn, langId )){
			// next word in chain
			index[curr++] = nextWord;
			index[curr] = s + slen;
			// found a porn word OR 
			// finished making a sequence of words
			if ( *isPorn || nextWord == s + slen ){
				char *p = splitWords;
				for ( int32_t k = 1; k < curr; k++ ){
					gbmemcpy (p, index[k - 1], 
						index[k] - index[k - 1]);
					p += index[k] - index[k - 1];
					*p = ' ';
					p++;
				}
				*p = '\0';
				return true;
			}
		}

		// did not find any word. reduce the current position
		while ( --curr > 0 ){
			if ( curr > 0 && index[curr] > index[curr-1] ){
				index[curr]--;
				break;
			}
		}
	}
	return false;
}

bool Speller::findNext( char *s, char *send, char **nextWord, bool *isPorn, unsigned char langId ) {
	const char *loc = NULL;
	int32_t slen = send - s;
	// check if there is an adult word in there
	// NOTE: The word 'adult' gives a lot of false positives, so even 
	// though it is in the isAdult() list, skip it.
	// s/slen constitues an individual word.
	if ( isAdult ( s, slen, &loc ) && strncmp ( s, "adult", 5 ) != 0 ){
		// if this string starts with the adult word, don't check 
		// further
		if ( loc == s ){
			*isPorn = true;
			*nextWord = send;
			return true;
		}
	}
	for ( char *a = send; a > s; a-- ){
		// a hack, if the word is only one letter long, check if it
		// is 'a' or 'i'. If not then continue
		if ( a - s == 1 && *s != 'a' && *s != 'i')
			continue;
		// another hack, the end word of the string cannot be 2 letters
		// or less. freesex was being split as 'frees ex'
		if ( a == send && a - s <= 2 )
			continue;

		// do not allow "ult" to be a word because it always will
		// split "adult" into "ad+ult"
		if ( a - s == 3 && s[0]=='u' && s[1]=='l' && s[2]=='t' )
			continue;
		// adultsiteratings = "ad ul ts it era tings" 
		if ( a - s == 2 && s[0]=='u' && s[1]=='l' )
			continue;
		// lashaxxxnothing = "lash ax xx nothing"
		if ( a - s == 2 && s[0]=='u' && s[1]=='l' )
			continue;
		// livesexasian = "lives ex asian"
		if ( a - s == 2 && s[0]=='e' && s[1]=='x' )
			continue;
		// fuckedtits = "fu ck edt its"
		if ( a - s == 2 && s[0]=='c' && s[1]=='k' )
			continue;
		// blogsexe = "blogs exe" ... many others
		// any 3 letter fucking word starting with "ex"
		if ( a - s == 3 && s[0]=='e' && s[1]=='x' )
			continue;
		// shemales = "*s hem ales"
		if ( a - s == 4 && s[0]=='a' &&s[1]=='l'&&s[2]=='e'&&s[3]=='s')
			continue;
		// grooverotica = "groove rot ica"
		if ( a - s == 3 && s[0]=='i' && s[1]=='c' && s[2]=='a' )
			continue;
		// dinerotik = dinero tik
		if ( a - s == 3 && s[0]=='t' && s[1]=='i' && s[2]=='k' )
			continue;
		// nudeslutpics = "nud esl ut pics"
		if ( a - s == 3 && s[0]=='n' && s[1]=='u' && s[2]=='d' )
			continue;
		// seepornos = "seep or nos"
		if ( a - s == 3 && s[0]=='n' && s[1]=='o' && s[2]=='s' )
			continue;
		// bookslut = "books lut"
		if ( a - s == 3 && s[0]=='l' && s[1]=='u' && s[2]=='t' )
			continue;
		// lesexegratuit = "lese xe gratuit"
		if ( a - s == 2 && s[0]=='x' && s[1]=='e' )
			continue;
		// mooiemensensexdating = "mens ense xd a ting"
		if ( a - s == 2 && s[0]=='x' && s[1]=='d' )
			continue;
		// mpornlinks = mpo rn links
		if ( a - s == 2 && s[0]=='r' && s[1]=='n' )
			continue;
		// ukpornbases = ukp or nba bes
		if ( a - s == 2 && s[0]=='o' && s[1]=='r' )
			continue;
		// slut
		if ( a - s == 2 && s[0]=='l' && s[1]=='u' )
			continue;
		// independentstockholmescorts = "tock holme sco rts"
		if ( a - s == 3 && s[0]=='s' && s[1]=='c' && s[2]=='o' )
			continue;
		// relatosexcitantes = relat ose xci tan tes 
		if ( a - s == 3 && s[0]=='x' && s[1]=='c' && s[2]=='i' )
			continue;
		// babe = * bes
		if ( a - s == 3 && s[0]=='b' && s[1]=='e' && s[2]=='s' )
			continue;
		// xpornreviews "xp orn reviews "
		if ( a - s == 3 && s[0]=='o' && s[1]=='r' && s[2]=='n' )
			continue;
		// shemal fix
		if ( a - s == 3 && s[0]=='h' && s[1]=='e' && s[2]=='m' )
			continue;
		// adultswim = adults wim
		if ( a - s == 3 && s[0]=='w' && s[1]=='i' && s[2]=='m' )
			continue;
		// bdsm
		if ( a - s == 3 && s[0]=='d' && s[1]=='s' && s[2]=='m' )
			continue;
		// anal
		if ( a - s == 3 && s[0]=='n' && s[1]=='a' && s[2]=='l' )
			continue;
		// vibrator = bra 
		if ( a - s == 3 && s[0]=='b' && s[1]=='r' && s[2]=='a' )
			continue;
		// sitiospornox = sitio spor nox
		if ( a - s == 4 && s[0]=='s' && s[1]=='p' && s[2]=='o' &&
		     s[3] == 'r' )
			continue;
		// orn*
		if ( a - s == 4 && s[0]=='o' && s[1]=='r' && s[2]=='n' )
			continue;
		// hotescorts = hote scor
		if ( a - s == 4 && s[0]=='s' && s[1]=='c' && s[2]=='o' &&
		     s[3] == 'r' )
			continue;
		// uniformsluts = uniformts lutz
		if ( a - s == 4 && s[0]=='l' && s[1]=='u' && s[2]=='t' &&
		     s[3] == 'z' )
			continue;
		// free porn login = freep ornl
		if ( a - s == 5 && s[0]=='f' && s[1]=='r' && s[2]=='e' &&
		     s[3] == 'e' && s[4] == 'p' )
			continue;
		// shemal fix
		if ( a - s == 5 && s[0]=='h' && s[1]=='e' && s[2]=='m' &&
		     s[3] == 'a' && s[4] == 'l' )
			continue;
		// inbondage = inbond age
		if ( a - s == 6 && 
		     s[0]=='i' && s[1]=='n' && s[2]=='b' &&
		     s[3]=='o' && s[4]=='n' && s[5]=='d' )
			continue;
		// swingers = wingers
		if ( a - s == 7 && 
		     s[0]=='w' && s[1]=='i' && s[2]=='n' &&
		     s[3]=='g' && s[4]=='e' && s[5]=='r' &&
		     s[6]=='s' )
			continue;
		// free sex contents = freese xc ont ents
		if ( a - s == 2 && s[0]=='x' && s[1]=='c' )
			continue;
		// mosexstore = mose xs tore
		if ( a - s == 2 && s[0]=='x' && s[1]=='s' )
			continue;
		// phonesexfootsies
		if ( a - s == 8 && 
		     s[0]=='p' && s[1]=='h' && s[2]=='o' &&
		     s[3]=='n' && s[4]=='e' && s[5]=='s' &&
		     s[6]=='e' && s[7]=='x' )
			continue;
		// cybersex
		if ( a - s == 8 && 
		     s[0]=='c' && s[1]=='y' && s[2]=='b' &&
		     s[3]=='e' && s[4]=='r' && s[5]=='s' &&
		     s[6]=='e' && s[7]=='x' )
			continue;
		// hotescorts
		

		// check if the word has popularity. if it is in the 
		// unifiedDict, then it is considered to be a word
		uint64_t h = hash64d(s, a-s);//a - s, encodeType);
		int32_t pop = getPhrasePopularity( s, h, langId );

		// continue if did not find it
		if ( pop <= 0 )
			continue;
		// this is our next word
		*nextWord = a;
		return true;
	}
	return false;
}	

// This isn't really much use except for the spider
// language detection to keep from making 32 sequential
// calls for the same phrase to isolate the language.
char *Speller::getPhraseRecord(char *phrase, int len ) {
	//char *xx=NULL;*xx=0;
	if ( !phrase ) return NULL;
	//char *rv = NULL;
	int64_t h = hash64d(phrase, len);
	int32_t slot = m_unifiedDict.getSlot(&h);
	//log("speller: h=%" PRIu64" len=%i slot=%" PRId32,h,len,slot);
	if ( slot < 0 ) return NULL;
	//rv = *(char **)m_unifiedDict.getValueFromSlot(slot);
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	return p;
}

int64_t Speller::getLangBits64 ( int64_t wid ) {
	int32_t slot = m_unifiedDict.getSlot(&wid);
	if (slot < 0) return 0LL;
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	// skip over word
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return 0LL;
	// skip tab
	p++;
	// skip over phonet
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return 0LL;
	// skip tab
	p++;
	// init
	int64_t bits = 0LL;
	// loop over langid/pop pairs
	while ( *p ) {
		// get langid
		uint8_t langId = atoi(p);
		// skip to next delimiter
		for ( ; *p && *p != '\t' ; p++ );
		// error?
		if ( ! *p ) break;
		// skip tab
		p++;
		// error?
		if ( ! *p ) break;
		// . if pop is zero ignore it
		// . we now set pops to zero when generating
		//   unifiedDict-buf.txt if they are not in the wiktionary
		//   map for that language. seems like to many bad entries
		//   were put in there by john nanny.
		//char pop = 1;
		// if not official, cancel it?
		if ( *p != '-' ) langId = langUnknown;
		// skip pop
		for ( ; *p && *p != '\t' ; p++ );
		// multi lang count
		//if ( langId != langUnknown ) langCount++;
		// no unique lang
		//if ( langCount >= 2 ) return langTranslingual;
		if ( langId != langTranslingual &&
		     langId != langUnknown )
			// make english "1"
			bits |= 1LL << (langId-1);
		// done?
		if ( ! *p ) break;
		// skip tab
		p++;
	}
	return bits;
}

bool Speller::getPhraseLanguages(char *phrase, int len,
				 int64_t *array) {
	char *phraseRec = getPhraseRecord(phrase, len);
	if(!phraseRec || !array) return false;
	return getPhraseLanguages2 ( phraseRec,array );
}

bool Speller::getPhraseLanguages2 (char *phraseRec , int64_t *array) {

	int64_t l = 0;
	memset(array, 0, sizeof(int64_t)*MAX_LANGUAGES);

	while(*phraseRec) {
		l = 0;
		// skip leading whitespace
		while(*phraseRec && (*phraseRec == ' ' ||
				     *phraseRec == '\t'))
			phraseRec++;

		if(!*phraseRec) break;

		int64_t l = atoi(phraseRec);
		// l = abs(l); // not using score method anymore, so this is moot.

		// skip to next delimiter
		// while(*phraseRec && *phraseRec != '\t') phraseRec++;
		if(!(phraseRec = strchr(phraseRec, '\t'))) break;

		// skip tab
		phraseRec++;

		if(!*phraseRec) break;

		// wtf?
		if ( *phraseRec == '\t' ) return true;

		// Save score
		array[l] = atoi(phraseRec);

		// skip to next delimiter
		// while(*phraseRec && *phraseRec != '\t') phraseRec++;
		if(!(phraseRec = strchr(phraseRec, '\t'))) break;

		// skip over tab
		if(*phraseRec == '\t') phraseRec++;
	}
	return(true);
}

void Speller::dictLookupTest ( char *ff ){
	//char *ff = "/tmp/sctest";
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd ) {
		log("speller: test: Could not open %s for "
		    "reading: %s.", ff,strerror(errno));
		return;
	}
	int64_t start = gettimeofdayInMilliseconds();
	char buf[1026];
	int32_t count = 0;
	// go through the words
	while ( fgets ( buf , MAX_FRAG_SIZE , fd ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		uint64_t h = hash64d ( buf, gbstrlen(buf));
		int32_t pop = g_speller.getPhrasePopularity( buf, h, 0 );
		if ( pop < 0 ){
			char *xx = NULL; *xx = 0;
		}
		count++;
	}
	log ( LOG_WARN,"speller: dictLookupTest took %" PRId64" ms to do "
	      "%" PRId32" words. Compare against 46-66ms taken for dict/words file.",
	      gettimeofdayInMilliseconds() - start, count );
	fclose(fd);
}
