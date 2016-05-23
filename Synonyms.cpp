#include "gb-include.h"

#include "Synonyms.h"
#include "HttpServer.h"
#include "Dns.h"
#include "StopWords.h"
#include "Speller.h"
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "sort.h"
#include "Wiktionary.h"
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

Synonyms::Synonyms() {
	m_synWordBuf.setLabel("syswbuf");
}

Synonyms::~Synonyms() {
	reset();
}

void Synonyms::reset() {
	m_synWordBuf.purge();
}

// . so now this adds a list of Synonyms to the m_pools[] and returns a ptr
//   to the first one.
// . then the parent caller can store that ptr in the m_wordToSyn[] array
//   which we pre-alloc upon calling the set() function based on the # of
//   words we got
// . returns # of synonyms stored into "tmpBuf"
int32_t Synonyms::getSynonyms ( const Words *words , 
			     int32_t wordNum , 
			     uint8_t langId ,
			     char *tmpBuf ,
			     int32_t niceness ) {

	// punct words have no synoyms
	if ( ! words->m_wordIds[wordNum] ) return 0;

	// store these
	m_words     = words;

	// sanity check
	if ( wordNum > m_words->m_numWords ) { char *xx=NULL;*xx=0; }

	// init the dedup table to dedup wordIds
	HashTableX dt;
	char dbuf[512];
	dt.set(8,0,12,dbuf,512,false,niceness,"altwrds");


	int32_t maxSyns = (int32_t)MAX_SYNS;

	char *bufPtr = tmpBuf;

	// point into buffer
	m_aids = (int64_t *)bufPtr;
	bufPtr += maxSyns * 8;

	// then the word ids
	m_wids0 = (int64_t *)bufPtr;
	bufPtr += maxSyns * 8;

	// second word ids, for multi alnum word synonyms, i.e. "New Jersey"
	m_wids1 = (int64_t *)bufPtr;
	bufPtr += maxSyns * 8;

	m_termPtrs = (char **)bufPtr;
	bufPtr += maxSyns * sizeof(char *);

	// we can't use m_termPtrs when we store a transformed word as the
	// synonym into m_synWordBuf, because it can grow dynamically
	// so we have to use offsets into that. so when m_termPtrs is
	// NULL for a syn, use m_termOffs to get it
	m_termOffs = (int32_t *)bufPtr;
	bufPtr += maxSyns * 4;

	m_termLens = (int32_t *)bufPtr;
	bufPtr += maxSyns * 4;

	m_numAlnumWords = (int32_t *)bufPtr;
	bufPtr += maxSyns * 4;

	m_numAlnumWordsInBase = (int32_t *)bufPtr;
	bufPtr += maxSyns * 4;

	// source
	m_src = bufPtr;
	bufPtr += maxSyns;

	// langid bit vector. 64 bits means up to 64 langs
	m_langIds = (uint8_t *)bufPtr;
	bufPtr += maxSyns ;

	if ( bufPtr > tmpBuf + TMPSYNBUFSIZE ) { char *xx=NULL;*xx=0; }

	// cursors
	m_aidsPtr  = m_aids;
	m_wids0Ptr = m_wids0;
	m_wids1Ptr = m_wids1;
	m_srcPtr   = m_src;
	m_termPtrsPtr = m_termPtrs;
	m_termOffsPtr = m_termOffs;
	m_termLensPtr = m_termLens;
	m_numAlnumWordsPtr = m_numAlnumWords;
	m_numAlnumWordsInBasePtr = m_numAlnumWordsInBase;
	m_langIdsPtr = m_langIds;

	
	char *w    = m_words->m_words   [wordNum];
	int32_t  wlen = m_words->m_wordLens[wordNum];

	//
	// NOW hit wiktionary
	// Trust this less then our s_exceptions above, but more than
	// our morph computations below
	//

	char sourceId = SOURCE_WIKTIONARY;
	char *ss = NULL;
	char *savedss = NULL;
	int64_t bwid;
	char wikiLangId = langId;
	bool hadSpace ;
	int32_t klen ;
	int32_t baseNumAlnumWords;
	char origLangId = wikiLangId;
	int32_t synSetCount = 0;
	bool doLangLoop = false;

 tryOtherLang:

	/*
	// if word only exists in one language, assume that language for word
	// even if m_queryLangId is langUnknown (0)
	if ( ! ss &&
	     ! m_queryLangId &&
	     ! wikiLangId ) {
		// get raw word id
		bwid = m_words->m_wordIds[wordNum];
		// each lang has its own bit
		int64_t bits = g_speller.getLangBits64 ( &bwid );
		// skip if not unique
		char count = getNumBitsOn64 ( bits ) ;
		// if we only got one lang we could be, assume that
		if ( count == 1 )
			// get it. bit #0 is english, so add 1
			wikiLangId = getBitPosLL((uint8_t *)&bits) + 1;
		// try setting based on script. greek. russian. etc.
		// if the word was not in the wiktionary.
		// this will be langUnknown if not definitive.
		else
			wikiLangId = getCharacterLanguage(w);
	}
	*/

	// try looking up bigram so "new jersey" gets "nj" as synonym
	if ( wikiLangId && 
	     wordNum+2< m_words->m_numWords &&
	     m_words->m_wordIds[wordNum+2]) {
		// get phrase id bigram then
		int32_t conti = 0;
		bwid = hash64Lower_utf8_cont(w,wlen,0,&conti);
		// then the next word
		char *wp2 = m_words->m_words[wordNum+2];
		int32_t  wlen2 = m_words->m_wordLens[wordNum+2];
		bwid = hash64Lower_utf8_cont(wp2,wlen2,bwid,&conti);
		baseNumAlnumWords = 2;
		ss = g_wiktionary.getSynSet( bwid, wikiLangId );
	}

	// need a language for wiktionary to work with
	if ( wikiLangId && ! ss ) {
		// get raw word id
		bwid = m_words->m_wordIds[wordNum];
		baseNumAlnumWords = 1;
		//if ( bwid == 1424622907102375150LL)
		//	log("a");
		ss = g_wiktionary.getSynSet( bwid, wikiLangId );
		// if that failed try removing 's from word if there
		if ( ! ss && 
		     wlen >= 3 &&
		     w[wlen-2]=='\'' && 
		     w[wlen-1]=='s' ) {
			int64_t cwid = hash64Lower_utf8(w,wlen-2);
			ss = g_wiktionary.getSynSet( cwid, wikiLangId );
		}
	}

	// loop over all the other langids if no synset found in this langid
	if ( ! ss && ! doLangLoop ) {
		wikiLangId = langUnknown; // start at 0
		doLangLoop = true;
	}

	// loop through all languages if no luck
	if ( doLangLoop ) {

		// save it. english is #1 so prefer that in case of
		// multiple matches i guess...
		if ( ss && ! savedss ) savedss = ss;

		// can only have one match to avoid ambiguity when doing
		// a loop over all the langids
		if ( ss && ++synSetCount >= 2 ) {
			// no, don't do this, just keep the first one.
			// like 'sport' is in english and french, so keep
			// the english one i guess. so do not NULL out "ss".
			// only NULL it out orig langid is unknown
			if ( origLangId != langUnknown ) ss = NULL;
			goto skip;
		}

		// advance langid of synset attempt
		wikiLangId++;

		// advance over original we tried first
		if ( wikiLangId == origLangId )
			wikiLangId++;
		// all done?
		if ( wikiLangId < langLast ) { // the last langid
			ss = NULL;
			goto tryOtherLang;
		}
	}

	// use the one single synset we found for some language
	if ( ! ss ) ss = savedss;

 skip:

	// even though a document may be in german it often has some
	// english words "pdf download" "copyright" etc. so if the word
	// has no synset in german, try it in english
	/*
	if ( //numPresets == 0 &&
	     ! ss &&
	     m_queryLangId != langEnglish &&
	     wikiLangId  != langEnglish &&
	     m_queryLangId &&
	     g_speller.getSynsInEnglish(w,wlen,m_queryLangId,langEnglish) ) {
		// try english
		wikiLangId = langEnglish;
		sourceId   = SOURCE_WIKTIONARY_EN;
		goto tryOtherLang;
	}
	*/


	// if it was in wiktionary, just use that synset
	if ( ss ) {
		// prepare th
		HashTableX dedup;
		HashTableX *dd = NULL;
		char dbuf[512];
		int32_t count = 0;
	addSynSet:
		// do we have another set following this
		char *next = g_wiktionary.getNextSynSet(bwid,langId,ss);
		// if so, init the dedup table then
		if ( next && ! dd ) {
			dd = &dedup;
			dd->set ( 8,0,8,dbuf,512,false,niceness,"sddbuf");
		}
		// get lang, 2 chars, unless zh_ch
		char *synLangAbbr = ss;
		// skip over the pipe i guess
		char *pipe = ss + 2;
		// zh_ch?
		if ( *pipe == '_' ) pipe += 3;
		// sanity
		if ( *pipe != '|' ) { char *xx=NULL;*xx=0; }

		// is it "en" or "zh_ch" etc.
		int synLangAbbrLen = pipe - ss;

		// point to word list
		char *p = pipe + 1;
		// hash up the list of words, they are in utf8 and
		char *e = p + 1;


		char tmp[32];
		int langId;

		// save count in case we need to undo
		//int32_t saved = m_numAlts[wordNum];
	hashLoop:


		// skip synonyms that are anagrams because its to ambiguous
		// the are mappings like
		// "PC" -> "PC,Personal Computer" 
		// "PC" -> "PC,Probable Cause" ... (lots more!)
		//bool isAnagram = true;
		for ( ; *e !='\n' && *e != ',' ; e++ ) ;
		//	if ( ! is_upper_a(*e) ) isAnagram = false;

		// get it
		int64_t h = hash64Lower_utf8_nospaces ( p , e - p );

		// skip if same as base word
		if ( h == bwid ) goto getNextSyn;

		// should we check for dups?
		if ( dd ) {
			// skip dups
			if ( dd->isInTable(&h) ) goto getNextSyn;
			// dedup. return false with g_errno set on error
			if ( ! dd->addKey(&h) ) return m_aidsPtr - m_aids;
		}
		// store it
		*m_aidsPtr++ = h;

		// store source
		*m_srcPtr++ = sourceId;

		// store the lang as a bit in a bit vector for the query term
		// so it can be from multiple langs.
		if ( synLangAbbrLen > 30 ) { char *xx=NULL;*xx=0; }
		gbmemcpy ( tmp , synLangAbbr , synLangAbbrLen );
		tmp[synLangAbbrLen] = '\0';
		langId = getLangIdFromAbbr ( tmp ); // order is linear
		if ( langId < 0 ) langId = 0;
		*m_langIdsPtr = langId;


		hadSpace = false;
		klen = e - p;
		for ( int32_t k = 0 ; k < klen ; k++ )
			if ( is_wspace_a(p[k]) ) hadSpace = true;

		*m_termPtrsPtr++ = p;
		*m_termLensPtr++ = e-p;

		// increment the dummies to keep in sync with synonym index
		// this is only for when m_termPtrs[x] is NULL because
		// we store the term into m_synWordBuf() because it is not
		// in out wiktionary file in memory.
		*m_termOffsPtr++ = -1;

		// only for multi-word synonyms like "New Jersey"...
		*m_wids0Ptr = 0LL;
		*m_wids1Ptr = 0LL;
		*m_numAlnumWordsPtr = 1;

		// and for multi alnum word synonyms
		if ( hadSpace ) {
			Words sw;
			sw.set ( p , e - p , true, niceness );

			*(int64_t *)m_wids0Ptr = sw.m_wordIds[0];
			*(int64_t *)m_wids1Ptr = sw.m_wordIds[2];
			*(int32_t  *)m_numAlnumWordsPtr = sw.getNumAlnumWords();
		}

		m_wids0Ptr++;
		m_wids1Ptr++;
		m_langIdsPtr++;
		m_numAlnumWordsPtr++;

		// how many words did we have to hash to find a synset?
		// i.e. "new jersey" would be 2, to get "nj"
		*m_numAlnumWordsInBasePtr++ = baseNumAlnumWords;

		// do not breach
		if ( ++count >= maxSyns ) return m_aidsPtr - m_aids;
	getNextSyn:
		// loop for more
		if ( *e == ',' ) { e++; p = e; goto hashLoop; }
		// add in the next syn set, deduped
		if ( next ) { ss = next; goto addSynSet; }
		// wrap it up
		//done:
		// all done
		//return m_aidsPtr - m_aids;
	}

	// strip marks from THIS word, return -1 w/ g_errno set on error
	if ( ! addStripped ( w , wlen,&dt ) ) return m_aidsPtr - m_aids;

	// do not breach
	if ( m_aidsPtr - m_aids > maxSyns ) return m_aidsPtr - m_aids;

	// returns false with g_errno set
	if ( ! addAmpPhrase ( wordNum, &dt ) ) return m_aidsPtr - m_aids;

	// do not breach
	if ( m_aidsPtr - m_aids > maxSyns ) return m_aidsPtr - m_aids;

	// if we end in apostrophe, strip and add
	if ( wlen>= 3 &&
	     w[wlen-1] == 's' && 
	     w[wlen-2]=='\'' &&
	     ! addWithoutApostrophe ( wordNum, &dt ) )
		return m_aidsPtr - m_aids;

	return m_aidsPtr - m_aids;
}


bool Synonyms::addWithoutApostrophe ( int32_t wordNum , HashTableX *dt ) {

	int32_t  wlen = m_words->m_wordLens[wordNum];
	char *w    = m_words->m_words[wordNum];

	wlen -= 2;
	
	uint64_t h = hash64Lower_utf8 ( w, wlen );
	
	// do not add dups
	if ( dt->isInTable ( &h ) ) return true;
	// add to dedup table. return false with g_errno set on error
	if ( ! dt->addKey ( &h ) ) return false;

	// store that
	*m_aidsPtr++ = h;
	*m_wids0Ptr++ = 0LL;
	*m_wids1Ptr++ = 0LL;
	*m_termPtrsPtr++ = NULL;
	*m_termLensPtr++ = wlen;

	*m_termOffsPtr++ = m_synWordBuf.length();

	m_synWordBuf.safeMemcpy(w,wlen);
	m_synWordBuf.pushChar('\0');

	*m_numAlnumWordsPtr++ = 1;
	*m_numAlnumWordsInBasePtr++ = 1;
	*m_srcPtr++ = SOURCE_GENERATED;

	// no langs
	*m_langIdsPtr++ = 0;

	return true;
}


// just index the first bigram for now to give a little bonus
bool Synonyms::addAmpPhrase ( int32_t wordNum , HashTableX *dt ) {
	// . "D & B" --> dandb
	// . make the "andb" a suffix
	//char tbuf[100];
	if ( wordNum +2 >= m_words->m_numWords   ) return true;
	if ( ! m_words->m_wordIds [wordNum+2]    ) return true;
	if ( m_words->m_wordLens[wordNum+2] > 50 ) return true;
	if ( ! m_words->hasChar(wordNum+1,'&')   ) return true;

	int32_t  wlen = m_words->m_wordLens[wordNum];
	char *w    = m_words->m_words[wordNum];

	// need this for hash continuation procedure
	int32_t conti = 0;
	// hack for "d & b" -> "dandb"
	uint64_t h = hash64Lower_utf8_cont ( w , wlen,0LL,&conti );
	// just make it a bigram with the word "and" after it
	// . we usually ignore stop words like and when someone does the query
	//   but we give out bonus points if the query term's left or right
	//   bigram has that stop word where it should be.
	// . so Dave & Barry will index "daveand" as a bigram and the
	//   search for 'Dave and Barry' will give bonus points for that
	//   bigram.
	h = hash64Lower_utf8_cont ( "and", 3,h,&conti);
	// logic in Phrases.cpp will xor it with 0x768867 
	// because it contains a stop word. this prevents "st.
	// and" from matching "stand".
	h ^= 0x768867;
	
	// do not add dups
	if ( dt->isInTable ( &h ) ) return true;
	// add to dedup table. return false with g_errno set on error
	if ( ! dt->addKey ( &h ) ) return false;

	// store that
	*m_aidsPtr++ = h;
	*m_wids0Ptr++ = 0LL;
	*m_wids1Ptr++ = 0LL;
	*m_termPtrsPtr++ = NULL;

	*m_termOffsPtr++ = m_synWordBuf.length();
	*m_termLensPtr++ = wlen+4;
	m_synWordBuf.safeMemcpy ( w , wlen );
	m_synWordBuf.safeStrcpy (" and");
	m_synWordBuf.pushChar('\0');

	*m_numAlnumWordsPtr++ = 1;
	*m_numAlnumWordsInBasePtr++ = 1;
	*m_srcPtr++ = SOURCE_GENERATED;

	// no langs
	*m_langIdsPtr++ = 0;

	return true;
}

// return false and set g_errno on error
bool Synonyms::addStripped ( const char *w , int32_t wlen , HashTableX *dt ) {
	// avoid overflow
	if ( wlen > 200 ) return true;

	// require utf8
	bool hadUtf8 = false;
	char size;
	for ( int32_t i = 0 ; i < wlen ; i += size ) {
		size = getUtf8CharSize(w+i);
		if ( size == 1 ) continue;
		hadUtf8 = true;
		break;
	}
	if ( ! hadUtf8 ) return true;

	// filter out accent marks
	char abuf[256];
	//int32_t alen = utf8ToAscii(abuf,256,(unsigned char *)w,wlen);
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(w,wlen);
#endif
	int32_t alen = stripAccentMarks(abuf,sizeof(abuf)-1,(unsigned char *)w,wlen);
	// skip if can't convert to ascii... (unsupported letter)
	if ( alen < 0 ) return true;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(abuf,alen);
#endif

	// if same as original word, skip
	if ( wlen==alen && strncmp(abuf,w,wlen) == 0 ) return true;

	// hash it
	uint64_t h2 = hash64Lower_utf8(abuf,alen);
	// do not add dups
	if ( dt->isInTable ( &h2 ) ) return true;
	// add to dedup table. return false with g_errno set
	if ( ! dt->addKey ( &h2 ) ) return false;



	// store that
	*m_aidsPtr++ = h2;
	*m_wids0Ptr++ = 0LL;
	*m_wids1Ptr++ = 0LL;
	*m_termPtrsPtr++ = NULL;
	*m_termOffsPtr++ = m_synWordBuf.length();
	*m_termLensPtr++ = alen;
	*m_numAlnumWordsPtr++ = 1;
	*m_numAlnumWordsInBasePtr++ = 1;
	*m_srcPtr++ = SOURCE_GENERATED;

	// no langs
	*m_langIdsPtr++ = 0;

	m_synWordBuf.safeMemcpy(abuf,alen);
	m_synWordBuf.pushChar('\0');

	return true;
}

const char *getSourceString ( char source ) {
	if ( source == SOURCE_NONE ) return "none";
	if ( source == SOURCE_PRESET ) return "preset";
	if ( source == SOURCE_WIKTIONARY ) return "wiktionary";
	if ( source == SOURCE_GENERATED ) return "generated";
	if ( source == SOURCE_BIGRAM ) return "bigram";
	if ( source == SOURCE_TRIGRAM ) return "trigram";
	if ( source == SOURCE_WIKTIONARY_EN ) return "wiktionary-en";
	// the thing we are hashing is a "number"
	if ( source == SOURCE_NUMBER ) return "number";
	return "unknown";
}
