#include "gb-include.h"

#include "Phrases.h"
#include "Mem.h"

Phrases::Phrases ( ) {
	m_buf = NULL;
	m_phraseSpam   = NULL;
}

Phrases::~Phrases ( ) {
	reset();
}

void Phrases::reset() {
	if ( m_buf && m_buf != m_localBuf ) {
		mfree ( m_buf , m_bufSize , "Phrases" );
	}

	m_buf = NULL;
	m_phraseSpam   = NULL;
}

// initialize this token array with the string, "s" of length, "len".
bool Phrases::set( Words    *words, 
		   Bits     *bits ,
		   bool      useStopWords , 
		   bool      useStems     ,
		   int32_t      titleRecVersion,
		   int32_t      niceness) {
	// reset in case being re-used
	reset();

	// now we never use stop words and we just index two-word phrases
	// so that a search for "get a" in quotes will match a doc that has
	// the phrase "get a clue". it might impact performance, but it should
	// be insignificant... but we need to have this level of precision.
	// ok -- but what about 'kick a ball'. we might not have that phrase
	// in the results for "kick a" AND "a ball"!! so we really need to
	// index "kick a ball" as well as "kick a" and "a ball". i don't think
	// that will cause too much bloat.
	//useStopWords = false;

	// ensure we have words
	if ( ! words ) return true;

	// . we have one phrase per word
	// . a phrase #n is "empty" if spam[n] == PSKIP
	m_numPhrases = words->getNumWords();

	// how much mem do we need?
	int32_t need = m_numPhrases * (8+8+1+1+1);

	// alloc if we need to
	if ( need > PHRASE_BUF_SIZE ) 
		m_buf = (char *)mmalloc ( need , "Phrases" );
	else
		m_buf = m_localBuf;

	if ( ! m_buf ) 
		return log("query: Phrases::set: %s",mstrerror(g_errno));
	m_bufSize = need;
	// set up arrays
	char *p = m_buf;

	// phrase not using stop words
	m_phraseIds2     = (int64_t *)p ; p += m_numPhrases * 8;
	m_phraseIds3     = (int64_t *)p ; p += m_numPhrases * 8;
	m_phraseSpam    = (unsigned char *)p ; p += m_numPhrases * 1;
	m_numWordsTotal2= (unsigned char *)p ; p += m_numPhrases * 1;
	m_numWordsTotal3= (unsigned char *)p ; p += m_numPhrases * 1;

	// sanity
	if ( p != m_buf + need ) { char *xx=NULL;*xx=0; }

	// clear this
	memset ( m_numWordsTotal2 , 0 , m_numPhrases );
	memset ( m_numWordsTotal3 , 0 , m_numPhrases );

	// point to this info while we parse
	m_words        = words;
	m_wptrs        = words->getWords();
	m_wlens        = words->getWordLens();
	m_wids         = words->getWordIds();
	m_bits         = bits;
	m_useStopWords = useStopWords;
	m_useStems     = useStems;

	// we now are dependent on this
	m_titleRecVersion = titleRecVersion;

	// . set the phrases
	// . sets m_phraseIds [i]
	// . sets m_phraseSpam[i] to PSKIP if NO phrase exists
	for ( int32_t i = 0 ; i < words->getNumWords() ; i++ ) {
		if ( ! m_wids[i] ) continue;
		setPhrase ( i , niceness);
	}
	// success
	return true;
}

// . add the phrase that starts with the ith word
// . "read Of Mice and Men" should make 3 phrases:
// . read.ofmice
// . ofmice
// . mice.andmen
void Phrases::setPhrase ( int32_t i, int32_t niceness ) {
	// hash of the phrase
	int64_t h   = 0LL; 

	// the hash of the two-word phrase (now we do 3,4 and 5 word phrases)
	int64_t h2  = 0LL; 
	int64_t h3  = 0LL; 

	// reset
	unsigned char pos = 0;

	// now look for other tokens that should follow the ith token
	int32_t          nw               = m_words->getNumWords();
	int32_t          numWordsInPhrase = 1;

	// use the min spam from all words in the phrase as the spam for phrase
	char minSpam = -1;

	// we need to hash "1 / 8" differently from "1.8" from "1,000" etc.
	char isNum = is_digit(m_wptrs[i][0]);

	// do not include punct/tag words in the m_numWordsTotal[j] count
	// of the total words in the phrase. these are just usesless tails.
	int32_t lastWordj = -1;

	// loop over following words
	int32_t j;
	bool hasHyphen ;
	bool hasStopWord2 ;

	// . NOTE: a token can start a phrase but NOT be in it. 
	// . like a large number for example.
	// . wordId is the lower ascii hash of the ith word
	// . NO... this is allowing the query operator PiiPe to start
	//   a phrase but not be in it, then the phrase id ends up just
	//   being the following word's id. causing the synonyms code to
	//   give a synonym which it should not un Synonyms::set()
	if ( ! m_bits->canBeInPhrase(i) )
		// so indeed, skip it then
		goto nophrase;

	h = m_wids[i];

	// set position
	pos = (unsigned char)m_wlens[i];

	hasHyphen = false;
	hasStopWord2 = m_bits->isStopWord(i);

	for ( j = i + 1 ; j < nw ; j++ ) {
		QUICKPOLL(niceness);

		// . do not allow more than 32 alnum/punct "words" in a phrase
		// . this prevents phrases with 100,000 words from slowing
		//   us down. would put us in a huge double-nested for loop
		if ( j > i + 32 ) goto nophrase;
		// deal with punct words
		if ( ! m_wids[j] ) {
			// if we cannot pair across word j then break
			if ( ! m_bits->canPairAcross (j) ) break;

			// does it have a hyphen?
			if (j==i+1 && m_words->hasChar(j,'-')) hasHyphen=true;

			continue;
		}

		// record lastWordj to indicate that word #j was a true word
		lastWordj = j;

		// if word #j can be in phrase then incorporate it's hash
		if ( m_bits->canBeInPhrase (j) ) {
			int32_t conti = pos;

			// hash the jth word into the hash
			h = hash64Lower_utf8_cont(m_wptrs[j], 
						  m_wlens[j],
						  h,
						  &conti );
			pos = conti;

			numWordsInPhrase++;

			// N-word phrases?
			if ( numWordsInPhrase == 2 ) {
				h2 = h;
				m_numWordsTotal2[i] = j-i+1;
				if ( m_bits->isStopWord(j) ) 
					hasStopWord2 = true;
				continue;
			}
			if ( numWordsInPhrase == 3 ) {
				h3 = h;
				m_numWordsTotal3[i] = j-i+1;
				//continue;
				break;
			}
		}

		// if we cannot pair across word j then break
		if ( ! m_bits->canPairAcross (j) ) break;

		// keep chugging?
		if ( numWordsInPhrase >= 5 ) {
			// if we're not using stop words then break
			if ( ! m_useStopWords ) break;
			// if it's not a stop word then break
			if ( ! m_bits->isStopWord (j) ) break;
		}
		// otherwise, get the next word
	}

	// if we had no phrase then use 0 as id (need 2+ words to be a pharse)
	if ( numWordsInPhrase <= 1 ) { 
	nophrase:
		m_phraseSpam[i]      = PSKIP; 
		m_phraseIds2[i]      = 0LL; 
		m_phraseIds3[i]      = 0LL;
		m_numWordsTotal2[i]   = 0;
		m_numWordsTotal3[i]   = 0;
		return;
	}

	// sanity check
	if ( lastWordj == -1 ) { char *xx = NULL; *xx = 0; }

	// sanity check
	if ( lastWordj - i + 1 > 255 ) { char *xx=NULL;*xx=0; }

	// set the phrase spam
	if ( minSpam == -1 ) minSpam = 0;
	m_phraseSpam[i] = minSpam;

	// hyphen between numbers does not count (so 1-2 != 12)
	if ( isNum ) hasHyphen = false;

	// . the two word phrase id
	// . "cd rom"    -> cdrom
	// . "fly paper" -> flypaper
	// . "i-phone"   -> iphone
	// . "e-mail"    -> email
	if ( hasHyphen || ! hasStopWord2 ) {
		//m_phraseIds [i] = h;
		m_phraseIds2[i] = h2;
	}
	// . "st. and"    !-> stand
	// . "the rapist" !-> therapist
	else {
		//m_phraseIds [i] = h  ^ 0x768867;
		m_phraseIds2[i] = h2 ^ 0x768867;
	}

	// forget hyphen logic for these
	m_phraseIds3[i] = h3;
}

// . store phrase that starts with word #i into "printBuf"
// . return bytes stored in "printBuf"
char *Phrases::getPhrase ( int32_t i , int32_t *phrLen , int32_t npw ) {
	// return 0 if no phrase
	if ( m_phraseSpam[i] == PSKIP ) return NULL;
	// store the phrase in here
	static char buf[256];
	// . how many words, including punct words, are in phrase?
	// . this should never be 1 or less
	//int32_t  n     = m_numWordsTotal[i] ;
	int32_t  n ;
	if      ( npw == 2 ) n = m_numWordsTotal2[i] ;
	else if ( npw == 3 ) n = m_numWordsTotal3[i] ;
	else { char *xx=NULL; *xx=0; }

	char *s     = buf;
	char *send  = buf + 255;
	for (int32_t w = i;w<i+n;w++){
		if (!m_words->isAlnum(w)){
			// skip spaces for now since we has altogether now
			*s++ = ' ';
			continue;
		}
		char *w1   = m_words->getWord(w);
		char *wend = w1 + m_words->getWordLen(w);
		for ( int32_t j = 0 ; j < m_words->getWordLen(w) && s<send ; j++){
			// write the lower case char from w1+j into "s"
			int32_t size = to_lower_utf8 ( s , send , w1 + j , wend );
			// advance
			j += size;
			s += size;
		}
	}
	// null terminate
	*s = '\0';
	// set length we wrote into "buf"
	*phrLen = s - buf;

	// return ptr to buf
	return buf;
}

// . word #n is in a phrase if he has [word][punct] or [punct][word]
//   before/after him and you can pair across the punct and include both
//   in a phrase
// . used by SimpleQuery class to see if a word is in a phrase or not
// . if it is then the query may choose not to represent the word by itself
bool Phrases::isInPhrase ( int32_t n ) {
	// returns true if we started a phrase (our phraseSpam is not PSKIP)
	if ( m_phraseSpam[n] != PSKIP ) return true;

	// . see if we were in a phrase started by a word before us
	// . this only words since stop words - whose previous word cannot be
	//   paired across - are able to start phrases
	if ( n < 2                        ) return false;
	if ( ! m_bits->canPairAcross(n-1) ) return false;
	if ( ! m_bits->canBeInPhrase(n-2) ) return false;
	return true;
}


int32_t Phrases::getMaxWordsInPhrase ( int32_t i , int64_t *pid ) { 
	*pid = 0LL;

	if ( m_numWordsTotal3[i] ) {
		*pid = m_phraseIds3[i];
		return m_numWordsTotal3[i];
	}

	if ( m_numWordsTotal2[i] ) {
		*pid = m_phraseIds2[i];
		return m_numWordsTotal2[i];
	}

	return 0;
}


int32_t Phrases::getMinWordsInPhrase ( int32_t i , int64_t *pid ) { 
	*pid = 0LL;

	if ( m_numWordsTotal2[i] ) {
		*pid = m_phraseIds2[i];
		return m_numWordsTotal2[i];
	}

	return 0;
}
