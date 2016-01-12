#include "gb-include.h"

#include "Pos.h"
#include "Words.h"
#include "Sections.h"

Pos::Pos() {
	m_buf = NULL;
	m_needsFree = false;
}

Pos::~Pos () {
	reset();
}

void Pos::reset() {
	if ( m_buf && m_needsFree )
		mfree ( m_buf , m_bufSize , "Pos" );
	m_buf = NULL;
}	

// . the interval is half-open [a,b)
// . do not print out any alnum word with negative score
int32_t Pos::filter( char *p, char *pend, Words *words, int32_t a, int32_t b, bool *isTruncated ) {
	int32_t plen = 0;
	set ( words , isTruncated, p , pend, &plen , a , b );
	return plen;
}

// . set the filtered position of each word
// . used by Summary.cpp to determine how many chars are in the summary,
//   be those chars single byte or utf8 chars that are 4 bytes 
// . returns false and sets g_errno on error
// . if f is non-NULL store filtered words into there. back to back spaces
//   are eliminated.
bool Pos::set( Words *words, bool *isTruncated, char *f, char *fend, int32_t *len, int32_t a, int32_t b ) {
	// free m_buf in case this is a second call
	if ( ! f ) {
		reset();
	}

	int32_t nw = words->getNumWords();
	int32_t *wlens = words->m_wordLens;
	nodeid_t *tids = words->getTagIds(); // m_tagIds;
	char **wp = words->m_words;

	// save start point for filtering
	char *fstart = f;

	// -1 is the default value
	if ( b == -1 ) {
		b = nw;
	}

	// alloc array if need to
	int32_t need = (nw+1) * 4;

	// do not destroy m_pos/m_numWords if only filtering into a buffer
	if ( !f ) {
		m_needsFree = false;

		m_buf = m_localBuf;
		if ( need > POS_LOCALBUFSIZE ) {
			m_buf = (char *)mmalloc(need,"Pos");
			m_needsFree = true;
		}
		// bail on error
		if ( ! m_buf ) return false;
		m_bufSize = need;
		m_pos      = (int32_t *)m_buf;
		m_numWords = nw;
	}

	// this is the CHARACTER count. 
	int32_t pos = 0;
	bool trunc = false;
	char *p , *pend;

	char* lastBreak = NULL;

	// flag for stopping back-to-back spaces. only count those as one char.
	bool lastSpace = false;
	static const int32_t maxCharSize = 4; // we are utf8
	int in_bad_tags = 0;

	for ( int32_t i = a ; i < b ; i++ ) {
		if (trunc) {
			break;
		}

		// set pos for the ith word to "pos"
		if ( ! f ) {
			m_pos[i] = pos;
		}

		// is tag?
		if ( tids && tids[i] ) {
			// filtering into buffer (when generating summaries)
			if ( f ) {
				// let's not get from bad tags
				if ( ( tids[i] == TAG_STYLE ) || ( tids[i] == TAG_SCRIPT ) ) {
					++in_bad_tags;
					continue;
				}

				if ( in_bad_tags ) {
					if ( ( ( tids[i] & BACKBITCOMP ) == TAG_STYLE ) ||
					     ( ( tids[i] & BACKBITCOMP ) == TAG_SCRIPT ) ) {
						--in_bad_tags;
					}
				}
			}

			// if not breaking, does nothing
			if ( !g_nodes[tids[i] & 0x7f].m_isBreaking ) {
				continue;
			}

			// list tag? <li>
			if ( tids[i] == TAG_LI ) {
				if ( f ) {
					if ( ( fend - f > maxCharSize ) ) {
						*f++ = '*';
					} else {
						trunc = true;
					}
				}
				pos++;
				lastSpace = false;
				continue;
			}

			// if had a previous breaking tag and no non-tag
			// word after it, do not count back-to-back spaces
			if ( lastSpace ) {
				continue;
			}

			// if had a br tag count it as a '.'
			if ( tids[i] ) { // <br>
				// are we filtering?
				if ( f && f != fstart ) {
					if ( ( fend - f > 2 * maxCharSize ) ) {
						*f++ = '.';
						*f++ = ' ';
					} else {
						trunc = true;
					}
				}

				// no, just single period.
				pos += 2;
				lastSpace = true;

				continue;
			}

			// are we filtering?
			if ( f ) {
				if ( ( fend - f > maxCharSize ) ) {
					*f++ = ' ';
				} else {
					trunc = true;
				}
			}

			// count as a single space
			pos++;

			// do not allow back-to-back spaces
			lastSpace = true;

			continue;
		}
		
		// skip words if we're in 'bad' tags
		if ( in_bad_tags ) {
			continue;
		}

		// scan through all chars discounting back-to-back spaces
		
		// assume filters out to the same # of chars
		p    = wp[i] ;
		pend = p + wlens[i];
		unsigned char cs = 0;
		for ( ; p < pend ; p += cs ) {
			// get size
			cs = getUtf8CharSize(p);

			// filtering into buffer (when generating summaries)
			if ( f ) {
				// skip unwanted character
				if ( isUtf8UnwantedSymbols( p ) ) {
					continue;
				}
			}

			// do not count space if one before
			if ( is_wspace_utf8 (p) ) {
				if ( lastSpace ) {
					continue;
				}

				lastSpace = true;

				// are we filtering?
				if ( f ) {
					if ( fend - f > 1 ) {
						lastBreak = f;
						*f++ = ' ';
					} else {
						trunc = true;
					}
				}

				pos++;
				continue;
			}

			if ( f ) {
				if ( fend - f > cs ) {
					// change '|' to commas
					if ( *p == '|' ) {
						*f++ = ',';
					} else if ( cs == 1 ) {
						*f++ = *p;
					} else {
						gbmemcpy( f, p, cs );
						f += cs;
					}
				} else {
					trunc = true;
				}
			}

			pos++; 
			lastSpace = false;
		}
	}

	if ( isTruncated ) {
		*isTruncated = trunc;
	}

	if ( trunc ) {
		if ( lastBreak == NULL ) {
			*len = 0;
			return false;
		} else if ( f ) {
			f = lastBreak;
		}
	}

	// set pos for the END of the last word here (used in Summary.cpp)
	if ( !f ) {
		m_pos[nw] = pos;
	} else { // NULL terminate f
		*len = f - fstart;
	}

	if ( fend - f > maxCharSize ) {
		*f = '\0';
	}

	// Success
	return true;
}
