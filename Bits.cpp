#include "Bits.h"
#include "StopWords.h"
#include "fctypes.h"
#include "Abbreviations.h"
#include "XmlNode.h"
#include "Mem.h"
#include "Sections.h"
#include "Process.h"
#include "tokenizer.h"
#include "GbUtil.h"
#include "Errno.h"
#include "Log.h"

Bits::Bits() {
	m_bits = NULL;
	m_swbits = NULL;
	memset(m_localBuf, 0, sizeof(m_localBuf));
	reset();
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

	// Coverity
	m_bitsSize = 0;
	m_swbitsSize = 0;
	m_tr = NULL;
	m_needsFree = false;
}

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::set(const TokenizerResult *tr) {
	reset();

	// save words so printBits works
	m_tr = tr;
	// how many words?
	int32_t numBits = tr->size();
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

	int32_t brcount = 0;

	wbit_t bits;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		const auto &token = (*m_tr)[i];
		if ( token.nodeid ) {
			nodeid_t tid = token.nodeid & BACKBITCOMP;
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
		else if ( token.is_alfanum ) {
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
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// set boundaries
		int32_t a = si->m_a;
		int32_t b = si->m_b;
		for ( int32_t i = a ; i < b ; i++ )
			m_bits[i] |= D_IN_LINK;
	}
}	

void Bits::setInUrlBits () {
	if ( m_inUrlBitsSet ) return;
	m_inUrlBitsSet = true;
	for ( unsigned i = 0 ; i < m_tr->size(); i++ ) {
		const auto &token = (*m_tr)[i];
		// look for protocol
		if(token.token_len!=3) continue;
		if(token.is_alfanum) continue;
		if(token.nodeid) continue;
		if(token.token_start[0] != ':' ) continue;
		if(token.token_start[1] != '/' ) continue;
		if(token.token_start[2] != '/' ) continue;
		// set them up
		if ( i<= 0 ) continue;

		// scan for end of it. stop at tag or space
		unsigned j = i - 1;
		for ( ; j <  m_tr->size(); j++ ) {
			const auto &token2 = (*m_tr)[j];
			// check if end
			if ( has_space(token2.token_start,token2.token_end()) ) {
				break;
			}

			// or tag
			if ( token2.nodeid ) {
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
	const auto &token = (*m_tr)[i];
	// this is not case sensitive -- all non-stop words can start phrases
	if ( ! ::isStopWord ( token.token_start, token.token_len, token.token_hash ) )
		return (D_CAN_BE_IN_PHRASE) ;

	return (D_CAN_BE_IN_PHRASE | D_CAN_PAIR_ACROSS | D_IS_STOPWORD);
}

//
// Summary.cpp sets its own bits.
//

// this table maps a tagId to a #define'd bit from Bits.h which describes
// the format of the following text in the page. like bold or italics, etc.
static nodeid_t s_bt [ 512 ];
static bool s_init = false;

// . set bits for each word
// . these bits are used for phrasing and by spam detector
// . returns false and sets errno on error
bool Bits::setForSummary ( const TokenizerResult *tr ) {
	// clear the mem
	reset();

	// set our s_bt[] table
	if ( ! s_init ) {
		// only do this once
		s_init = true;

		// clear table
		if ( getNumXmlNodes() > 512 ) {
			g_process.shutdownAbort(true);
		}
		memset ( s_bt, 0, sizeof(s_bt) );
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
	m_tr = tr;

	// how many words?
	int32_t numBits = tr->size();

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

	bool startSentence = true;
	bool startFragment = true;
	bool inQuote = false;

	// the ongoing accumulation flag we apply to each word
	swbit_t flags = 0;

	for ( int32_t i = 0 ; i < numBits ; i++ ) {
		// assume none are set
		m_swbits[i] = 0;
		const auto &token = (*m_tr)[i];
		if ( token.nodeid ) {
			nodeid_t tid = token.nodeid & BACKBITCOMP;

			// is it a "breaking tag"?
			if ( g_nodes[tid].m_isBreaking ) {
				startSentence = true;
				inQuote   = false;
			}

			// adjust flags if we should
			if ( s_bt[tid] ) {
				if ( tid != token.nodeid ) {
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
		if ( token.is_alfanum ) {
			if ( startFragment ) {
				m_swbits[i] |= D_STARTS_FRAGMENT;
				startFragment = false;
			}

			if ( startSentence ) {
				m_swbits[i] |= D_STARTS_SENTENCE;
				startSentence = false;
			}

			if ( inQuote ) {
				m_swbits[i] |= D_IN_QUOTES;
				inQuote = false;
			}

			// apply any other flags we got
			m_swbits[i] |= flags;
			continue;
		}

		// fast ptrs
		int32_t wlen = token.token_len;
		const char *wp = token.token_start;
		
		// this is not 100%
		if ( has_char(wp,wp+wlen, '(' ) ) {
			flags |= D_IN_PARENTHESES;
		} else if ( has_char(wp,wp+wlen, ')' ) ) {
			flags &= ~D_IN_PARENTHESES;
		}

		// apply curent flags
		m_swbits[i] |= flags;

		// does it END in a quote?
		if ( wp[wlen - 1] == '\"' ) {
			inQuote = true;
		} else if ( wlen >= 6 && strncmp( wp, "&quot;", 6 ) == 0 ) {
			inQuote = true;
		}

		// . but double spaces are not starters
		// . MDW: we kinda force ourselves to only use ascii spaces here
		if ( wlen == 2 && is_wspace_a( *wp ) && is_wspace_a( wp[1] ) ) {
			continue;
		}

		// it can start a fragment if not a single space char
		if ( wlen != 1 || !is_wspace_utf8( wp ) ) {
			startFragment = true;
		}

		// Detect end of sentences so we can set the start-sentence flag on the next word.
		// ". " denotes end of sentence
		if ( wlen >= 2 && wp[0] == '.' && is_wspace_utf8( wp + 1 ) ) {
			// but not if preceeded by an initial
			if ( i > 0 && (*tr)[i-1].token_len == 1 && (*tr)[i-1].is_alfanum ) {
				continue;
			}

			// ok, really the end of a sentence
			startSentence = true;
		} else {
			//other punctuations marking end of sentences, that aren't overloaded for abbreviation indication as period/fullstop is.
			int cs = getUtf8CharSize(wp);
			UChar32 cp = utf8Decode(wp);
			if(wlen >= cs+1 && is_wspace_utf8(wp+cs) && (cp=='?' ||
				                                     cp=='!' ||
				                                     cp==0x037E ||  //greek question mark (although the regular 0x003b is preferred)
				                                     cp==0x203D ||  //interrobang
				                                     cp==0x06D4))   //arabic full stop
			{
				startSentence = true;
			}
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
