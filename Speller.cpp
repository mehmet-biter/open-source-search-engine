#include "Speller.h"
#include "Query.h"
#include "StopWords.h"
#include "Hostdb.h"
#include "Process.h"
#include "Conf.h"
#include "Lang.h"
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

	log(LOG_INFO,"Loading unified dict");
	bool loaded = loadUnifiedDict();
	log(LOG_INFO,"Loaded unified dict");
	if (!loaded) {
		log(LOG_WARN, "spell: Could not load unified dict from unifiedDict-buf.txt and unifiedDict-map.dat");
		return false;
	}

	// this seems to slow our startup way down!!!
	log("speller: turning off spell checking for now");
	return true;
}

void Speller::reset(){
	m_unifiedBuf.purge();
	m_unifiedDict.reset();
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
	m_unifiedDict.set ( 8,4, 2*1024*1024,NULL,0,false,"udictht");

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
	if ( ! wkfMap.set ( 8,1,1000000,NULL,0,true,"wkfmap") )
		return false;

	// "fr|livre" is how it's formatted
	for ( ; p && p < pend ; p = wkfBuf.getNextLine(p) ) {
		char *start = p;
		// skip til |
		for ( ; *p && *p != '|' ; p++ );
		// sanity check
		if ( *p != '|' ) { g_process.shutdownAbort(true); }
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
			p += strlen(p) + 1;
			continue;
		}
		// skip phrase
		while ( *p != '\t' )
			p++;
		// Null end the phrase
		*p = '\0';

		// skip empty phrases
		if(strlen(phrase) < 1) {
			log(LOG_WARN,
				"spell: Got zero length entry in unifiedDict "
			    "at line %" PRIu64", skipping\n",
				atline);
			p += strlen(p) + 1;
			continue;
		}

		// skip single byte words that are not alphabetic
		// Anything over 'Z' is likely unicode, so don't bother
		if(strlen(phrase) == 1 && (phrase[0] < 'a')) {
			log(LOG_WARN,
				"spell: Got questionable entry in "
			    "unifiedDict at line %" PRIu64", skipping: %s\n",
				atline,p);
			p += strlen(p) + 1;
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

		uint64_t key = hash64d(phrase,strlen(phrase));

		// make sure we haven't added this word/phrase yet
		if ( m_unifiedDict.isInTable ( &key ) ) {
			totalCollisions++;
			p += strlen(p) + 1;
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
			uint8_t langId = *(char *)wkfMap.getValueFromSlot(slot);
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
			p += strlen(p) + 1;
			continue;
		}

		// trim final tab i guess
		m_unifiedBuf.incrementLength(-1);
		// end line
		m_unifiedBuf.pushChar('\n');

		// directly point to the (lang, score) tuples
		m_unifiedDict.addKey(&key, &offset);

		// skip "p" to next line in unifiedBuf.txt
		p += strlen(p) + 1;
	}

	log (LOG_WARN,"spell: got %" PRId32" TOTAL collisions in unified dict",
	     totalCollisions);

	HashTableX dedup;
	dedup.set(8,0,1000000,NULL,0,false,"dmdm");

	// . now add entries from wkfBuf that were not also in "ub"
	// . format is "<langAbbr>|<word>\n"
	p = wkfBuf.getBufStart();
	end = p + wkfBuf.length();
	for ( ; p ; p = wkfBuf.getNextLine(p) ) {
		//char *langAbbr = p;
		for ( ; *p && *p !='\n' && *p !='|' ; p++ );
		if ( *p != '|' ) {
			log("speller: bad format in wiktionary-lang.txt");
			g_process.shutdownAbort(true);
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
			g_process.shutdownAbort(true);
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
			uint8_t langId = *(char *)wkfMap.getValueFromSlot(slot);
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
	if ( !m_unifiedDict.save(g_hostdb.m_dir,"unifiedDict-map.dat") )
		return false;

	// start over and load what we created
	goto reload;

}

// in case the language is unknown, just give the pop of the
// first found language
int32_t Speller::getPhrasePopularity( const char *str, uint64_t h, unsigned char langId ) {
	//g_process.shutdownAbort(true);

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
	char *pend = p + strlen(p);

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


// This isn't really much use except for the spider
// language detection to keep from making 32 sequential
// calls for the same phrase to isolate the language.
const char *Speller::getPhraseRecord(const char *phrase, int len ) {
	//g_process.shutdownAbort(true);
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

bool Speller::getPhraseLanguages(const char *phrase, int len,
				 int64_t *array) {
	const char *phraseRec = getPhraseRecord(phrase, len);
	if(!phraseRec || !array) return false;
	return getPhraseLanguages2 ( phraseRec,array );
}

bool Speller::getPhraseLanguages2(const char *phraseRec , int64_t *array) {

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
		int32_t wlen = strlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		uint64_t h = hash64d ( buf, strlen(buf));
		int32_t pop = g_speller.getPhrasePopularity( buf, h, 0 );
		if ( pop < 0 ){
			g_process.shutdownAbort(true);
		}
		count++;
	}
	log ( LOG_WARN,"speller: dictLookupTest took %" PRId64" ms to do "
	      "%" PRId32" words. Compare against 46-66ms taken for dict/words file.",
	      gettimeofdayInMilliseconds() - start, count );
	fclose(fd);
}
