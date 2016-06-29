#include "gb-include.h"

#include "Bits.h"
#include "StopWords.h"
#include "fctypes.h"
#include "Abbreviations.h"
#include "Mem.h"
#include "Sections.h"
#include "Process.h"


Bits::Bits() {
	m_bits = NULL;
	m_swbits = NULL;
}

Bits::~Bits() {
	reset();
}

void Bits::reset() {
	if ( m_bits && m_needsFree ) // (char *)m_bits != m_localBuf )
		mfree ( m_bits , m_bitsSize , "Bits" );
	if ( m_swbits && m_needsFree )
		mfree ( m_swbits , m_swbitsSize , "Bits" );
	m_bits = NULL;
	m_swbits = NULL;
	m_inLinkBitsSet = false;
	m_inUrlBitsSet = false;
}

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::set(const Words *words, int32_t niceness) {
	reset();

	// save words so printBits works
	m_words = words;
	// save for convenience/speed
	m_niceness = niceness;
	// how many words?
	int32_t numBits = words->getNumWords();
	// how much space do we need?
	int32_t need = numBits * sizeof(wbit_t);
	// assume no malloc
	m_needsFree = false;

	// use local buf?
	if ( need < BITS_LOCALBUFSIZE ) {
		m_bits = (wbit_t *) m_localBuf;
	} else {
		m_bitsSize = need;
		m_bits = (wbit_t *)mmalloc ( need , "Bits1" );
		m_needsFree = true;
	}
	if ( ! m_bits ) {
		log("build: Could not allocate Bits table used to parse words: %s", mstrerror(g_errno));
		return false;
	}

	// breathe
	QUICKPOLL ( m_niceness );

	const nodeid_t *tagIds = words->getTagIds();
	const char *const*w = words->getWords();

	int32_t brcount = 0;

	wbit_t bits;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		if ( tagIds && tagIds[i] ) {
			// shortcut
			nodeid_t tid = tagIds[i] & BACKBITCOMP;
			// count the <br>s, we can't pair across more than 1
			if ( g_nodes[tid].m_isBreaking ) {
				bits = 0;
			} else if ( tid == TAG_BR ) {
				// can only pair across one <br> tag, not two
				if ( brcount > 0 ) {
					bits = 0;
				} else {
					brcount++;
					bits = D_CAN_PAIR_ACROSS;
				}
			} else {
				bits = D_CAN_PAIR_ACROSS;
			}
		}
		else if ( is_alnum_utf8 ( w[i]+0 )) {
			bits = getAlnumBits( i );
			brcount = 0;
		} else {
			// . just allow anything now!
			// . the curved quote in utf8 is 3 bytes long and with
			//   a space before it, was causing issues here!
			bits= D_CAN_PAIR_ACROSS;
		}

		// remember our bits.
		m_bits [ i ] = bits;
	}

	return true;
}

void Bits::setInLinkBits ( Sections *ss ) {

	if ( m_inLinkBitsSet ) return;
	m_inLinkBitsSet = true;
	if ( ss->m_numSections == 0 ) return;
	// sets bits for Bits.cpp for D_IN_LINK for each ALNUM word
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// set boundaries
		int32_t a = si->m_a;
		int32_t b = si->m_b;
		for ( int32_t i = a ; i < b ; i++ )
			m_bits[i] |= D_IN_LINK;
	}
}	

void Bits::setInUrlBits ( int32_t niceness ) {
	if ( m_inUrlBitsSet ) return;
	m_inUrlBitsSet = true;
	const nodeid_t *tids  = m_words->getTagIds();
	const int64_t *wids = m_words->getWordIds();
	const char *const*wptrs    = m_words->getWords();
	int32_t nw = m_words->getNumWords();
	for ( int32_t i = 0 ; i < nw; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// look for protocol
		if ( wids[i] ) continue;
		if ( tids[i] ) continue;
		if ( wptrs[i][0] != ':' ) continue;
		if ( wptrs[i][1] != '/' ) continue;
		if ( wptrs[i][2] != '/' ) continue;
		// set them up
		if ( i<= 0 ) continue;

		// scan for end of it. stop at tag or space
		int32_t j = i - 1;
		for ( ; j < nw; j++ ) {
			// breathe
			QUICKPOLL( niceness );

			// check if end
			if ( m_words->hasSpace( j ) ) {
				break;
			}

			// or tag
			if ( tids[j] ) {
				break;
			}
			// include it
			m_bits[j] |= D_IS_IN_URL;
		}

		// avoid inifinite loop with this if conditional statement
		if ( j > i ) {
			i = j;
		}
	}
}

// . if we're a stop word and previous word was an apostrophe
//   then set D_CAN_APOSTROPHE_PRECEED to true and PERIOD_PRECEED to false
wbit_t Bits::getAlnumBits( int32_t i ) const {
	// this is not case sensitive -- all non-stop words can start phrases
	if ( ! ::isStopWord ( m_words->getWord( i ) , m_words->getWordLen( i ) , m_words->getWordId( i ) ) )
		return (D_CAN_BE_IN_PHRASE) ;

	return (D_CAN_BE_IN_PHRASE | D_CAN_PAIR_ACROSS | D_IS_STOPWORD);
}

//
// Summary.cpp sets its own bits.
//

// this table maps a tagId to a #define'd bit from Bits.h which describes
// the format of the following text in the page. like bold or italics, etc.
static nodeid_t s_bt [ 1000 ];

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::setForSummary ( const Words *words ) {
	// clear the mem
	reset();

	// set our s_bt[] table
	bool s_init = false;
	if ( ! s_init ) {
		// only do this once
		s_init = true;
		// clear table
		if ( 1000 < getNumXmlNodes() ) { g_process.shutdownAbort(true); }
		memset ( s_bt , 0 , 1000 * sizeof(nodeid_t) );
		// set just those that have bits #defined in Bits.h
		s_bt [ TAG_TITLE      ] = D_IN_TITLE;
		s_bt [ TAG_A          ] = D_IN_HYPERLINK;
		s_bt [ TAG_B          ] = D_IN_BOLDORITALICS;
		s_bt [ TAG_I          ] = D_IN_BOLDORITALICS;
		s_bt [ TAG_LI         ] = D_IN_LIST;
		s_bt [ TAG_SUP        ] = D_IN_SUP;
		s_bt [ TAG_P          ] = D_IN_PARAGRAPH;
		s_bt [ TAG_BLOCKQUOTE ] = D_IN_BLOCKQUOTE;
	}

	// save words so printBits works
	m_words = words;

	// how many words?
	int32_t numBits = words->getNumWords();

	// how much space do we need?
	int32_t need = sizeof(swbit_t) * numBits;

	// assume no malloc
	m_needsFree = false;

	// use local buf?
	if ( need < BITS_LOCALBUFSIZE ) {
		m_swbits = (swbit_t *)m_localBuf;
	} else {
		// i guess need to malloc
		m_swbitsSize = need;
		m_swbits = (swbit_t *)mmalloc( need, "BitsW" );
		m_needsFree = true;
	}

	if ( !m_swbits ) {
		log( LOG_WARN, "build: Could not allocate Bits table used to parse words: %s", mstrerror( g_errno ) );
		return false;
	}

	// set
	// D_STRONG_CONNECTOR
	// D_STARTS_SENTENCE
	// D_STARTS_FRAGMENT

	const nodeid_t *tagIds = words->getTagIds();
	const char *const*w = words->getWords();
	const int32_t *wlens = words->getWordLens();
	const int64_t *wids = words->getWordIds();

	char startSent = 1;
	char startFrag = 1;
	char inQuote = 0;
	char inParens = 0;

	int32_t wlen;
	const char *wp;

	// the ongoing accumulation flag we apply to each word
	swbit_t flags = 0;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		// assume none are set
		m_swbits[i] = 0;

		// if a breaking tag, next guy can "start a sentence"
		if ( tagIds && tagIds[i] ) {
			// get the tag id minus the high "back bit"
			int32_t tid = tagIds[i] & BACKBITCOMP;

			// is it a "breaking tag"?
			if ( g_nodes[tid].m_isBreaking ) {
				startSent = 1;
				inQuote   = 0;
			}

			// adjust flags if we should
			if ( s_bt[tid] ) {
				if ( tid != tagIds[i] ) {
					flags &= ~s_bt[tid];
				} else {
					flags |= s_bt[tid];
				}
			}

			// apply flag
			m_swbits[i] |= flags;
			continue;
		}

		// if alnum, might start sentence or fragment
		if ( wids[i] ) {
			if ( startFrag ) {
				m_swbits[i] |= D_STARTS_FRAG;
				startFrag = 0;
			}

			if ( startSent ) {
				m_swbits[i] |= D_STARTS_SENTENCE;
				startSent = 0;
			}

			if ( inQuote ) {
				m_swbits[i] |= D_IN_QUOTES;
				inQuote = 0;
			}

			if ( inParens ) {
				m_swbits[i] |= D_IN_PARENS;
			}

			// apply any other flags we got
			m_swbits[i] |= flags;
			continue;
		}

		// fast ptrs
		wlen = wlens[i];
		wp   = w    [i];
		
		// this is not 100%
		if ( words->hasChar( i, '(' ) ) {
			flags |= D_IN_PARENS;
		} else if ( words->hasChar( i, ')' ) ) {
			flags &= ~D_IN_PARENS;
		}

		// apply curent flags
		m_swbits[i] |= flags;

		// does it END in a quote?
		if ( wp[wlen - 1] == '\"' ) {
			inQuote = 1;
		} else if ( wlen >= 6 && strncmp( wp, "&quot;", 6 ) == 0 ) {
			inQuote = 1;
		}

		// . but double spaces are not starters
		// . MDW: we kinda force ourselves to only use ascii spaces here
		if ( wlen == 2 && is_wspace_a( *wp ) && is_wspace_a( wp[1] ) ) {
			continue;
		}

		// it can start a fragment if not a single space char
		if ( wlen != 1 || !is_wspace_utf8( wp ) ) {
			startFrag = 1;
		}

		// ". " denotes end of sentence
		if ( wlen >= 2 && wp[0] == '.' && is_wspace_utf8( wp + 1 ) ) {
			// but not if preceeded by an initial
			if ( i > 0 && wlens[i - 1] == 1 && wids[i - 1] ) {
				continue;
			}

			// ok, really the end of a sentence
			startSent = 1;
		}

		// are we a "strong connector", meaning that
		// Summary.cpp should not split on us if possible

		// apostrophe html encoded?
		if ( wlen == 6 && strncmp( wp, "&#146;", 6 ) == 0 ) {
			m_swbits[i] |= D_IS_STRONG_CONNECTOR;
			continue;
		}
		if ( wlen == 7 && strncmp( wp, "&#8217;", 7 ) == 0 ) {
			m_swbits[i] |= D_IS_STRONG_CONNECTOR;
			continue;
		}

		// otherwise, strong connectors must be single char
		if ( wlen != 1 ) {
			continue;
		}

		// is it apostrophe? - & . * (M*A*S*H)
		char c = wp[0];
		if ( c == '\'' || c == '-' || c == '&' || c == '.' || c == '*' || c == '/' ) {
			m_swbits[i] |= D_IS_STRONG_CONNECTOR;
		}
	}

	return true;
}
