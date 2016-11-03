//#include "gb-include.h"

#include "Phrases.h"
#include "Words.h"
#include "Bits.h"
#include "Mem.h"
#include "Conf.h"
#include "Sanity.h"


Phrases::Phrases() : m_buf(NULL) {

	memset(m_localBuf, 0, sizeof(m_localBuf));

	// Coverity
	m_bufSize = 0;
	m_phraseIds2 = NULL;
	m_numWordsTotal2 = NULL;
	m_numPhrases = 0;
	m_words = NULL;
	m_wids = NULL;
	m_wptrs = NULL;
	m_wlens = NULL;
	m_bits = NULL;

	reset();
}

Phrases::~Phrases ( ) {
	reset();
}

void Phrases::reset() {
	if ( m_buf && m_buf != m_localBuf ) {
		mfree ( m_buf , m_bufSize , "Phrases" );
	}
	m_buf = NULL;
}


// initialize this token array with the string, "s" of length, "len".
bool Phrases::set( const Words *words, const Bits *bits ) {
	// reset in case being re-used
	reset();

	// ensure we have words
	if ( ! words ) return true;

	// . we have one phrase per word
	// . a phrase #n is "empty" if spam[n] == PSKIP
	m_numPhrases = words->getNumWords();

	// how much mem do we need?
	int32_t need = m_numPhrases * (8+1);

	// alloc if we need to
	if ( (unsigned)need > sizeof(m_localBuf) )
		m_buf = (char *)mmalloc ( need , "Phrases" );
	else
		m_buf = m_localBuf;

	if ( ! m_buf ) {
		log(LOG_WARN, "query: Phrases::set: %s",mstrerror(g_errno));
		return false;
	}

	m_bufSize = need;

	// set up arrays
	char *p = m_buf;

	// phrase not using stop words
	m_phraseIds2 = (int64_t *)p;
	p += m_numPhrases * 8;

	m_numWordsTotal2 = (unsigned char *)p;
	p += m_numPhrases * 1;

	// sanity
	if ( p != m_buf + need ) gbshutdownLogicError();

	// point to this info while we parse
	m_words        = words;
	m_wptrs        = words->getWordPtrs();
	m_wlens        = words->getWordLens();
	m_wids         = words->getWordIds();
	m_bits         = bits;

	// . set the phrases
	// . sets m_phraseIds [i]
	// . sets m_phraseSpam[i] to PSKIP if NO phrase exists
	for ( int32_t i = 0 ; i < words->getNumWords() ; ++i ) {
		if ( ! m_wids[i] ) {
			continue;
		}

		setPhrase ( i );
	}

	// success
	return true;
}

// . add the phrase that starts with the ith word
// . "read Of Mice and Men" should make 3 phrases:
// . read.ofmice
// . ofmice
// . mice.andmen
void Phrases::setPhrase ( int32_t i ) {
	logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 " BEGIN", i);

	// hash of the phrase
	int64_t h   = 0LL; 

	// the hash of the two-word phrase
	int64_t h2  = 0LL; 

	// reset
	unsigned char pos = 0;

	// now look for other tokens that should follow the ith token
	int32_t nw = m_words->getNumWords();
	int32_t numWordsInPhrase = 1;

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
	if ( ! m_bits->canBeInPhrase(i) ) {
		// so indeed, skip it then
		goto nophrase;
	}

	h = m_wids[i];

	// set position
	pos = (unsigned char)m_wlens[i];

	hasHyphen = false;
	hasStopWord2 = m_bits->isStopWord(i);

	for ( j = i + 1 ; j < nw ; j++ ) {
		logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". LOOP START", i, j, m_wids[i], m_wids[j] );

		// Do not allow more than 32 alnum/punct "words" in a phrase.
		// Tthis prevents phrases with 100,000 words from slowing
		// us down. would put us in a huge double-nested for loop
		// BR: But it will never happen? It breaks out of the loop
		//     when the phrase contains 2 (real) words?
		if ( j > i + 32 ) {
			logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". j > i+32. no phrase", i, j, m_wids[i], m_wids[j] );
			goto nophrase;
		}

		// deal with punct words
		if ( ! m_wids[j] ) {
			// if we cannot pair across word j then break
			if ( !m_bits->canPairAcross( j ) ) {
				logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". Pair cannot cross. Breaking.", i, j, m_wids[i], m_wids[j] );
				break;
			}

			// does it have a hyphen?
			if ( j == i + 1 && m_words->hasChar( j, '-' ) ) {
				hasHyphen = true;
				logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64 ". j is hyphen, NOT adding to phrase", i, j, m_wids[i], m_wids[j] );
			}
			else {
				logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64 ". j is space, NOT adding to phrase", i, j, m_wids[i], m_wids[j] );
			}
			continue;
		}

		// record lastWordj to indicate that word #j was a true word
		lastWordj = j;

		// if word #j can be in phrase then incorporate it's hash
		if ( m_bits->canBeInPhrase (j) ) {
			int32_t conti = pos;

			// hash the jth word into the hash
			h = hash64Lower_utf8_cont( m_wptrs[j], m_wlens[j], h, &conti );

			pos = conti;

			++numWordsInPhrase;

			logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". CAN be in phrase. Adding j's hash. numWordsInPhrase=%" PRId32 "", i, j, m_wids[i], m_wids[j], numWordsInPhrase);


			// N-word phrases?
			if ( numWordsInPhrase == 2 ) {
				h2 = h;
				m_numWordsTotal2[i] = j - i + 1;
				hasStopWord2 = m_bits->isStopWord(j);

				logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". Words in phrase is 2. Breaking.", i, j, m_wids[i], m_wids[j] );
				break;
			}
		}
		else {
			logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". j cannot be in a phrase.", i, j, m_wids[i], m_wids[j] );
		}
			

		// if we cannot pair across word j then break
		if ( ! m_bits->canPairAcross (j) ) {
			logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". Cannot pair across. Breaking.", i, j, m_wids[i], m_wids[j] );
			break;
		}

		// otherwise, get the next word
		logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". Get next word", i, j, m_wids[i], m_wids[j] );
	}

	// if we had no phrase then use 0 as id (need 2+ words to be a phrase)
	if ( numWordsInPhrase <= 1 ) { 
	nophrase:
		m_phraseIds2[i]      = 0LL; 
		m_numWordsTotal2[i]   = 0;
		logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". END. Not a phrase. m_phraseIds2[i]=%" PRIu64 "", i, j, m_wids[i], m_wids[j], m_phraseIds2[i]);
		return;
	}

	// sanity check
	if ( lastWordj == -1 ) gbshutdownLogicError();

	// sanity check
	if ( lastWordj - i + 1 > 255 ) gbshutdownLogicError();

	// hyphen between numbers does not count (so 1-2 != 12)
	if ( isNum ) hasHyphen = false;

	// . the two word phrase id
	// . "cd rom"    -> cdrom
	// . "fly paper" -> flypaper
	// . "i-phone"   -> iphone
	// . "e-mail"    -> email
	if ( hasHyphen || ! hasStopWord2 ) {
		m_phraseIds2[i] = h2;
		logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". END. Has hyphen or no stopword. m_phraseIds2[i]=%" PRIu64 "", i, j, m_wids[i], m_wids[j], m_phraseIds2[i] );
	}
	// . "st. and"    !-> stand
	// . "the rapist" !-> therapist
	else {
		m_phraseIds2[i] = h2 ^ 0x768867;
		logTrace( g_conf.m_logTracePhrases, "i=%3" PRId32 ", j=%3" PRId32 ", wids[i]=%20" PRIu64", wids[j]=%20" PRIu64". END. either no hyphen or a stopword. m_phraseIds2[i]=%" PRIu64 "", i, j, m_wids[i], m_wids[j], m_phraseIds2[i] );
	}
}



// . store phrase that starts with word #i into "printBuf"
// . return bytes stored in "printBuf"
void Phrases::getPhrase(int32_t i, char *buf, size_t bufsize, int32_t *phrLen) const {
	// return 0 if no phrase
	if ( m_phraseIds2[i] == 0LL ) {
		*buf='\0';
		return;
	}

	// . how many words, including punct words, are in phrase?
	// . this should never be 1 or less
	int32_t  n = m_numWordsTotal2[i] ;

	char *s     = buf;
	char *send  = buf + bufsize - 1;
	for (int32_t w = i;w<i+n;w++){
		if (!m_words->isAlnum(w)){
			// skip spaces for now since we has altogether now
			*s++ = ' ';
			continue;
		}
		const char *w1   = m_words->getWord(w);
		const char *wend = w1 + m_words->getWordLen(w);
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
}

int32_t Phrases::getMinWordsInPhrase ( int32_t i , int64_t *pid ) const {
	*pid = 0LL;

	if ( m_numWordsTotal2[i] ) {
		*pid = m_phraseIds2[i];
		return m_numWordsTotal2[i];
	}

	return 0;
}

