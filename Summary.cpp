#include "Summary.h"

#include "Words.h"
#include "Sections.h"
#include "Query.h"
#include "Xml.h"
#include "Pos.h"
#include "Matches.h"
#include "Process.h"


Summary::Summary()
    : m_summaryLen(0)
    , m_numExcerpts(0)
    , m_numDisplayLines(0)
    , m_displayLen(0)
    , m_maxNumCharsPerLine(0)
	, m_isSetFromTags(false)
    , m_q(NULL)
    , m_wordWeights(NULL)
    , m_wordWeightSize(0)
    , m_buf4(NULL)
    , m_buf4Size(0) {
}

Summary::~Summary() {
	if ( m_wordWeights && m_wordWeights != (float *)m_tmpWordWeightsBuf ) {
		mfree ( m_wordWeights , m_wordWeightSize , "sumww");
		m_wordWeights = NULL;
	}

	if ( m_buf4 && m_buf4 != m_tmpBuf4 ) {
		mfree ( m_buf4 , m_buf4Size , "ssstkb" );
		m_buf4 = NULL;
	}
}

char* Summary::getSummary() {
	return m_summary;
}

int32_t Summary::getSummaryDisplayLen() {
	return m_displayLen;
}

int32_t Summary::getSummaryLen() {
	return m_summaryLen;
}

bool Summary::isSetFromTags() {
	return m_isSetFromTags;
}

bool Summary::verifySummary( char *titleBuf, int32_t titleBufLen ) {
	if ( m_summaryLen > 0 ) {
		// trim elipsis
		if ( ( titleBufLen > 4 ) && ( memcmp( (titleBuf + titleBufLen - 4), " ...", 4 ) == 0 ) ) {
			titleBufLen -= 4;
		}

		// verify that it's not the same with title
		if ( strncasestr( m_summary, titleBuf, m_summaryLen, titleBufLen ) ) {
			m_summaryLen = 0;
			m_summary[0] = '\0';

			return false;
		}

		m_summaryExcerptLen[0] = m_summaryLen;
		m_numExcerpts = 1;
		m_displayLen = m_summaryLen;

		return true;
	}

	return false;
}

// let's try to get a nicer summary by using what the website set as description
// Use the following in priority order (highest first)
// - itemprop = "description"
// - meta name = "og:description"
// - meta name = "description"
bool Summary::setSummaryFromTags( Xml *xml, int32_t maxSummaryLen, char *titleBuf, int32_t titleBufLen ) {
	// sanity check
	if ( maxSummaryLen >= MAX_SUMMARY_LEN ) {
		g_errno = EBUFTOOSMALL;
		log("query: Summary too big to hold in buffer of %" PRId32" bytes.",(int32_t)MAX_SUMMARY_LEN);
		return false;
	}

	/// @todo ALC configurable minSummaryLen so we can tweak this as needed
	const int minSummaryLen = (maxSummaryLen / 3);

	// itemprop = "description"
	if ( xml->getTagContent("itemprop", "description", m_summary, MAX_SUMMARY_LEN, minSummaryLen, maxSummaryLen, &m_summaryLen) ) {
		if ( verifySummary( titleBuf, titleBufLen ) ) {
			m_isSetFromTags = true;

			if ( g_conf.m_logDebugSummary ) {
				log(LOG_DEBUG, "sum: generated from itemprop description. summary='%.*s'", m_summaryLen, m_summary);
			}

			return true;
		}
	}

	// meta property = "og:description"
	if ( xml->getTagContent("property", "og:description", m_summary, MAX_SUMMARY_LEN, minSummaryLen, maxSummaryLen, &m_summaryLen, true, TAG_META ) ) {
		if ( verifySummary( titleBuf, titleBufLen ) ) {
			m_isSetFromTags = true;

			if ( g_conf.m_logDebugSummary ) {
				log(LOG_DEBUG, "sum: generated from meta property og:description. summary='%.*s'", m_summaryLen, m_summary);
			}

			return true;
		}
	}

	// meta name = "description"
	if ( xml->getTagContent("name", "description", m_summary, MAX_SUMMARY_LEN, minSummaryLen, maxSummaryLen, &m_summaryLen, true, TAG_META ) ) {
		if ( verifySummary( titleBuf, titleBufLen ) ) {
			m_isSetFromTags = true;

			if ( g_conf.m_logDebugSummary ) {
				log(LOG_DEBUG, "sum: generated from meta name description. summary='%.*s'", m_summaryLen, m_summary);
			}

			return true;
		}
	}

	if ( g_conf.m_logDebugSummary ) {
		log(LOG_DEBUG, "sum: unable to generate summary from itemprop/meta tags");
	}

	return false;
}

// returns false and sets g_errno on error
bool Summary::setSummary ( Xml *xml, Words *words, Sections *sections, Pos *pos, Query *q, int32_t maxSummaryLen,
						   int32_t maxNumLines, int32_t numDisplayLines, int32_t maxNumCharsPerLine, Url *f,
                           Matches *matches, char *titleBuf, int32_t titleBufLen ) {
	m_numDisplayLines = numDisplayLines;
	m_displayLen      = 0;

	// assume we got maxnumlines of summary
	if ( (maxNumCharsPerLine + 6) * maxNumLines > maxSummaryLen ) {
		if ( maxNumCharsPerLine < 10 ) {
			maxNumCharsPerLine = 10;
		}

		static char s_flag = 1;
		if ( s_flag ) {
			s_flag = 0;
			log("query: Warning. "
			    "Max summary excerpt length decreased to "
			    "%" PRId32" chars because max summary excerpts and "
			    "max summary length are too big.",
			    maxNumCharsPerLine);
		}
	}

	// . sanity check
	// . summary must fit in m_summary[]
	// . leave room for tailing \0
	if ( maxSummaryLen >= MAX_SUMMARY_LEN ) {
		g_errno = EBUFTOOSMALL;
		return log("query: Summary too big to hold in buffer of %" PRId32" bytes.",(int32_t)MAX_SUMMARY_LEN);
	}

	// do not overrun the final*[] buffers
	if ( maxNumLines > 256 ) { 
		g_errno = EBUFTOOSMALL; 
		return log("query: More than 256 summary lines requested.");
	}

	// Nothing to match...print beginning of content as summary
	if ( matches->m_numMatches == 0 && maxNumLines > 0 ) {
		return getDefaultSummary ( xml, words, sections, pos, maxSummaryLen );
	}

	int32_t need1 = q->m_numWords * sizeof(float);
	m_wordWeightSize = need1;
	if ( need1 < 128 ) {
		m_wordWeights = (float *)m_tmpWordWeightsBuf;
	} else {
		m_wordWeights = (float *)mmalloc ( need1 , "wwsum" );
	}

	if ( ! m_wordWeights ) {
		return false;
	}

	/// @todo ALC fix word weights
	/// non-working logic is removed in commit 5eacee9063861e859b54ec62035a600aa8af25df

	// . compute our word weights wrt each query. words which are more rare
	//   have a higher weight. We use this to weight the terms importance
	//   when generating the summary.
	// . used by the proximity algo
	// . used in setSummaryScores() for scoring summaries


	for ( int32_t i = 0 ; i < q->m_numWords; i++ ) {
		m_wordWeights[i] = 1.0;
	}

	// convenience
	m_maxNumCharsPerLine = maxNumCharsPerLine;
	m_q = q;

	// set the max excerpt len to the max summary excerpt len
	int32_t maxExcerptLen = m_maxNumCharsPerLine;

	int32_t lastNumFinal = 0;
	int32_t maxLoops = 1024;

	// if just computing absScore2...
	if ( maxNumLines <= 0 ) {
		return true;
	}

	char *p = m_summary;
	char *pend = m_summary + maxSummaryLen;

	m_numExcerpts = 0;

	int32_t need2 = (1+1+1) * m_q->m_numWords;
	m_buf4Size = need2;
	if ( need2 < 128 ) {
		m_buf4 = m_tmpBuf4;
	} else {
		m_buf4 = (char *)mmalloc ( need2 , "stkbuf" );
	}

	if ( ! m_buf4 ) {
		return false;
	}

	char *x = m_buf4;
	char *retired = x;
	x += m_q->m_numWords;
	char *maxGotIt = x;
	x += m_q->m_numWords;
	char *gotIt = x;

	// . the "maxGotIt" count vector accumulates into "retired"
	// . that is how we keep track of what query words we used for previous
	//   summary excerpts so we try to get diversified excerpts with 
	//   different query terms/words in them
	//char retired  [ MAX_QUERY_WORDS ];
	memset ( retired, 0, m_q->m_numWords * sizeof(char) );

	// some query words are already matched in the title
	for ( int32_t i = 0 ; i < m_q->m_numWords ; i++ ) {
		if ( matches->m_qwordFlags[i] & MF_TITLEGEN ) {
			retired [ i ] = 1;
		}
	}

	bool hadEllipsis = false;

	// 
	// Loop over all words that match a query term. The matching words
	// could be from any one of the 3 Words arrays above. Find the
	// highest scoring window around each term. And then find the highest
	// of those over all the matching terms.
	//
	int32_t numFinal;
	for ( numFinal = 0; numFinal < maxNumLines; numFinal++ ) {
		if ( numFinal == m_numDisplayLines ) {
			m_displayLen = p - m_summary;
		}

		// reset these at the top of each loop
		Match     *maxm;
		int64_t  maxScore = 0;
		int32_t       maxa = 0;
		int32_t       maxb = 0;
		int32_t       maxi  = -1;
		int32_t       lasta = -1;

		if(lastNumFinal == numFinal) {
			if(maxLoops-- <= 0) {
				log(LOG_WARN, "query: got infinite loop bug, query is %s url is %s", m_q->m_orig, f->getUrl());
				break;
			}
		}
		lastNumFinal = numFinal;

		// loop through all the matches and see which is best
		for ( int32_t i = 0 ; i < matches->m_numMatches ; i++ ) {
			int32_t       a , b;
			// reset lasta if we changed words class
			if ( i > 0 && matches->m_matches[i-1].m_words != matches->m_matches[i].m_words ) {
				lasta = -1;
			}

			// only use matches in title, etc.
			mf_t flags = matches->m_matches[i].m_flags;

			bool skip = true;
			if ( flags & MF_METASUMM ) {
				skip = false;
			}
			if ( flags & MF_METADESC ) {
				skip = false;
			}
			if ( flags & MF_BODY     ) {
				skip = false;
			}
			if ( flags & MF_RSSDESC  ) {
				skip = false;
			}

			if ( skip ) {
				continue;
			}

			// ask him for the query words he matched
			//char gotIt [ MAX_QUERY_WORDS ];
			// clear it for him
			memset ( gotIt, 0, m_q->m_numWords * sizeof(char) );

			// . get score of best window around this match
			// . do not allow left post of window to be <= lasta to
			//   avoid repeating the same window.
			int64_t score = getBestWindow (matches, i, &lasta, &a, &b, gotIt, retired, maxExcerptLen);
			
			// USE THIS BUF BELOW TO DEBUG THE ABOVE CODE. 
			// PRINTS OUT THE SUMMARY
			/*
			//if ( score >=12000 ) {
			char buf[10*1024];
			char *xp = buf;
			if ( i == 0 )
				log (LOG_WARN,"=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
			sprintf(xp, "score=%08" PRId32" a=%05" PRId32" b=%05" PRId32" ",
				(int32_t)score,(int32_t)a,(int32_t)b);
			xp += gbstrlen(xp);
			for ( int32_t j = a; j < b; j++ ){
				//int32_t s = scores->m_scores[j];
				int32_t s = 0;
				if ( s < 0 ) continue;
				char e = 1;
				int32_t len = words->getWordLen(j);
				for(int32_t k=0;k<len;k +=e){
					char c = words->m_words[j][k];
					//if ( is_binary( c ) ) continue;
					*xp = c;
					xp++;
				}
				//p += gbstrlen(p);
				if ( s == 0 ) continue;
				sprintf ( xp ,"(%" PRId32")",s);
				xp += gbstrlen(xp);
			}
			log (LOG_WARN,"query: summary: %s", buf);
			//}
			*/

			// prints out the best window with the score
			/*
			char buf[MAX_SUMMARY_LEN];
			  char *bufPtr = buf;
			  char *bufPtrEnd = p + MAX_SUMMARY_LEN;
			  if ( i == 0 )
			  log (LOG_WARN,"=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
			  int32_t len = 0;
			  Words *ww  = matches->m_matches[i].m_words;
			  //Sections *ss = matches->m_matches[i].m_sections;
			  //if ( ss->m_numSections <= 0 ) ss = NULL;
			  //len=pos->filter(bufPtr, bufPtrEnd, ww, a, b, NULL);
			  //log(LOG_WARN,"summary: %" PRId32") %s - %" PRId64,i,bufPtr,
			  //score);
			  log(LOG_WARN,"summary: %" PRId32") %s - %" PRId64,i,bufPtr,
			  score);
			*/

			// skip if was in title or something
			if ( score <= 0 ) {
				continue;
			}

			// skip if not a winner
			if ( maxi >= 0 && score <= maxScore ) {
				continue;
			}

			// we got a new winner
			maxi     = i;
			maxa     = a;
			maxb     = b;
			maxScore = score;

			// save this too
			gbmemcpy ( maxGotIt , gotIt , m_q->m_numWords );

		}
	
		// retire the query words in the winning summary

		
		//log( LOG_WARN,"summary: took %" PRId64" ms to finish getbestwindo",
		//    gettimeofdayInMilliseconds() - stget );


		// all done if no winner was made
		if ( maxi == -1 || maxa == -1 || maxb == -1) {
			break;
		}

		// who is the winning match?
		maxm = &matches->m_matches[maxi];
		Words *ww = maxm->m_words;

		// we now use "m_swbits" for the summary bits since they are
		// of size sizeof(swbit_t), a int16_t at this point
		swbit_t *bb = maxm->m_bits->m_swbits;

		// this should be impossible
		if ( maxa > ww->getNumWords() || maxb > ww->getNumWords() ) {
			log ( LOG_WARN,"query: summary starts or ends after "
			      "document is over! maxa=%" PRId32" maxb=%" PRId32" nw=%" PRId32,
			      maxa, maxb, ww->getNumWords() );
			maxa = ww->getNumWords() - 1;
			maxb = ww->getNumWords();
		}

		// assume we do not preceed with ellipsis "..."
		bool needEllipsis = true;
		
		const char *c = ww->getWord(maxa)+0;

		// rule of thumb, don't use ellipsis if the first letter is capital, or a non letter
		// is punct word before us pair acrossable? if so then we probably are not the start of a sentence.
		// or if into the sample and previous excerpt had an ellipsis do not bother using one for us.
		if ( !is_alpha_utf8(c) || is_upper_utf8(c) ||
		     (bb[maxa] & D_STARTS_SENTENCE) ||
		     (p > m_summary && hadEllipsis)) {
			needEllipsis = false;
		}

		if ( needEllipsis ) {
			// break out if no room for "..."
			if ( p + 4 + 2 > pend ) {
				break;
			}

			// space first?
			if ( p > m_summary ) {
				*p++ = ' ';
			}

			memcpy ( p, "\342\200\246 ", 4 ); //horizontal ellipsis, code point 0x2026
			p += 4;
		}

		// separate summary excerpts with a single space.
		if ( p > m_summary ) {
			if ( p + 2 > pend ) {
				break;
			}

			*p++ = ' ';
		}

		// assume we need a trailing ellipsis
		needEllipsis = true;

		// so next excerpt does not need to have an ellipsis if we 
		// have one at the end of this excerpt
		hadEllipsis = needEllipsis;

		// start with quote?
		if ( (bb[maxa] & D_IN_QUOTES) && p + 1 < pend ) {
			// preceed with quote
			*p++ = '\"';
		}
	
		// . filter the words into p
		// . removes back to back spaces
		// . converts html entities
		// . filters in stores words in [a,b) interval
		int32_t len = pos->filter( ww, maxa, maxb, false, p, pend, xml->getVersion() );

		// break out if did not fit
		if ( len == 0 ) {
			break;
		}

		// don't consider it if it is a substring of the title
		if ( len == titleBufLen && strncasestr(titleBuf, p, titleBufLen, len) ) {
			// don't consider this one
			numFinal--;
			goto skip;
		}
	
		// don't consider it if the length wasn't anything nice
		if ( len < 5 ){
			numFinal--;
			goto skip;
		}

		// otherwise, keep going
		p += len;

		// now we just indicate which query terms we got
		for ( int32_t i = 0 ; i < m_q->m_numWords ; i++ ) {
			// do not breach
			if ( retired[i] >= 100 ) {
				continue;
			}
			retired [ i ] += maxGotIt [ i ];
		}
	
		// add all the scores of the excerpts to the doc summary score.
		// zero out scores of the winning sample so we don't get them 
		// again. use negative one billion to ensure that we don't get
		// them again
		for ( int32_t j = maxa ; j < maxb ; j++ ) {
			// mark it as used
			bb[j] |= D_USED;
		}

		// if we ended on punct that can be paired across we need
		// to add an ellipsis
		if ( needEllipsis ) {
			if ( p + 4 + 2 > pend ) {
				break;
			}
			memcpy ( p, " \342\200\246", 4 ); //horizontal ellipsis, code point 0x2026
			p += 4;
		}

		// try to put in a small summary excerpt if we have atleast
		// half of the normal excerpt length left
		if ( maxExcerptLen == m_maxNumCharsPerLine && len <= ( m_maxNumCharsPerLine / 2 + 1 ) ) {
			maxExcerptLen = m_maxNumCharsPerLine / 2;

			// don't count it in the finals since we try to get a small excerpt
			numFinal--;
		} else if ( m_numExcerpts < MAX_SUMMARY_EXCERPTS && m_numExcerpts >= 0 ) {
			m_summaryExcerptLen[m_numExcerpts] = p - m_summary;
			m_numExcerpts++;

			// also reset maxExcerptLen
			maxExcerptLen = m_maxNumCharsPerLine;
		}
	
	skip:
		// zero out the scores so they will not be used in others
		for ( int32_t j = maxa ; j < maxb ; j++ ) {
			// mark it
			bb[j] |= D_USED;
		}
	}

	if ( numFinal <= m_numDisplayLines ) {
		m_displayLen = p - m_summary;
	}

	// free the mem we used if we allocated it
	if ( m_buf4 && m_buf4 != m_tmpBuf4 ) {
		mfree ( m_buf4 , m_buf4Size , "ssstkb" );
		m_buf4 = NULL;
	}

	// If we still didn't find a summary, get the default summary
	if ( p == m_summary ) {
		bool status = getDefaultSummary ( xml, words, sections, pos, maxSummaryLen );
		if ( m_numDisplayLines > 0 ) {
			m_displayLen = m_summaryLen;
		}
		
		return status;
	}

	// if we don't find a summary, theres no need to NULL terminate
	*p++ = '\0';

	// set length
	m_summaryLen = p - m_summary;

	if ( m_summaryLen > 50000 ) { g_process.shutdownAbort(true); }

	return true;
}

// . return the score of the highest-scoring window containing match #m
// . window is defined by the half-open interval [a,b) where a and b are 
//   word #'s in the Words array indicated by match #m
// . return -1 and set g_errno on error
int64_t Summary::getBestWindow ( Matches *matches, int32_t mm, int32_t *lasta,
                                 int32_t *besta, int32_t *bestb, char *gotIt,
                                 char *retired, int32_t maxExcerptLen ) {
	// get the window around match #mm
	Match *m = &matches->m_matches[mm];

	// what is the word # of match #mm?
	int32_t matchWordNum = m->m_wordNum;

	// what Words/Pos/Bits classes is this match in?
	Words *words = m->m_words;
	Section **sp = NULL;
	int32_t *pos = m->m_pos->m_pos;

	// use "m_swbits" not "m_bits", that is what Bits::setForSummary() uses
	const swbit_t *bb = m->m_bits->m_swbits;

	// shortcut
	if ( m->m_sections ) {
		sp = m->m_sections->m_sectionPtrs;
	}

	int32_t nw = words->getNumWords();
	int64_t *wids = words->getWordIds();
	nodeid_t *tids = words->getTagIds();

	// . sanity check
	// . this prevents a core i've seen
	if ( matchWordNum >= nw ) {
		log("summary: got overflow condition for q=%s",m_q->m_orig);

		// assume no best window
		*besta = -1;
		*bestb = -1;
		*lasta = matchWordNum;
		return 0;
	}

	// . we NULLify the section ptrs if we already used the word in another summary.
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE;
	if ( (bb[matchWordNum] & D_USED) || ( sp && (sp[matchWordNum]->m_flags & badFlags) ) ) {
		// assume no best window
		*besta = -1;
		*bestb = -1;
		*lasta = matchWordNum;
		return 0;
	}

	// . "a" is the left fence post of the window (it is a word # in Words)
	// . go to the left as far as we can 
	// . thus we decrement "a"
	int32_t a = matchWordNum;

	// "posa" is the character position of the END of word #a
	int32_t posa = pos[a+1];
	int32_t firstFrag = -1;
	bool startOnQuote = false;
	bool goodStart = false;
	int32_t wordCount = 0;

	// . decrease "a" as int32_t as we stay within maxNumCharsPerLine
	// . avoid duplicating windows by using "lasta", the last "a" of the
	//   previous call to getBestWindow(). This can happen if our last
	//   central query term was close to this one.
	for ( ; a > 0 && posa - pos[a-1] < maxExcerptLen && a > *lasta; a-- ) {
		// . don't include any "dead zone", 
		// . dead zones have already been used for the summary, and
		//   we are getting a second/third/... excerpt here now then
		// stop if its the start of a sentence, too
		// stop before title word
		if ( (bb[a-1] & D_USED) || (bb[a] & D_STARTS_SENTENCE) || ( bb[a-1] & D_IN_TITLE )) {
			goodStart = true;
			break;
		}

		// don't go beyond an LI, TR, P tag
		if ( tids && ( tids[a-1] == TAG_LI ||
		               tids[a-1] == TAG_TR ||
		               tids[a-1] == TAG_P  ||
		               tids[a-1] == TAG_DIV ) ) {
			goodStart = true;
			break;
		}

		// stop if its the start of a quoted sentence
		if ( a+1<nw && (bb[a+1] & D_IN_QUOTES) && 
		     words->getWord(a)[0] == '\"' ){
			startOnQuote = true;
			goodStart    = true;
			break;
		}

		// find out the first instance of a fragment (comma, etc)
		// watch out! because frag also means 's' in there's
		if ( ( bb[a] & D_STARTS_FRAG ) && !(bb[a-1] & D_IS_STRONG_CONNECTOR) && firstFrag == -1 ) {
			firstFrag = a;
		}

		if ( wids[a] ) {
			wordCount++;
		}
	}

	// if didn't find a good start, then start at the start of the frag
	if ( !goodStart && firstFrag != -1 ) {
		a = firstFrag;
	}

	// don't let punct or tag word start a line, unless a quote
	if ( a < matchWordNum && !wids[a] && words->getWord(a)[0] != '\"' ){
		while ( a < matchWordNum && !wids[a] ) a++;
		
		// do not break right after a "strong connector", like 
		// apostrophe
		while ( a < matchWordNum && a > 0 && 
			( bb[a-1] & D_IS_STRONG_CONNECTOR ) )
			a++;
		
		// don't let punct or tag word start a line
		while ( a < matchWordNum && !wids[a] ) a++;
	}

	// remember, b is not included in the summary, the summary is [a,b-1]
	// remember to include all words in a matched phrase
	int32_t b = matchWordNum + m->m_numWords ;
	int32_t endQuoteWordNum = -1;
	int32_t numTagsCrossed = 0;

	for ( ; b <= nw; b++ ) {
		if ( b == nw ) {
			break;
		}

		if ( pos[b+1] - pos[a] >= maxExcerptLen ) {
			break;
		}
		
		if ( startOnQuote && words->getWord(b)[0] == '\"' ) {
			endQuoteWordNum = b;
		}

		// don't include any dead zone, those are already-used samples
		if ( bb[b] & D_USED ) {
			break;
		}

		// stop on a title word
		if ( bb[b] & D_IN_TITLE ) {
			break;
		}

		if ( wids[b] ) {
			wordCount++;
		}

		// don't go beyond an LI or TR backtag
		if ( tids && ( tids[b] == (BACKBIT|TAG_LI) ||
		               tids[b] == (BACKBIT|TAG_TR) ) ) {
			numTagsCrossed++;

			// try to have atleast 10 words in the summary
			if ( wordCount > 10 ) {
				break;
			}
		}

		// go beyond a P or DIV backtag in case the earlier char is a
		// ':'. This came from a special case for wikipedia pages 
		// eg. http://en.wikipedia.org/wiki/Flyover
		if ( tids && ( tids[b] == (BACKBIT|TAG_P)  ||
		               tids[b] == (BACKBIT|TAG_DIV) )) {
			numTagsCrossed++;

			// try to have atleast 10 words in the summary
			if ( wordCount > 10 && words->getWord(b-1)[0] != ':' ) {
				break;
			}
		}
	}

	// don't end on a lot of punct words
	if ( b > matchWordNum && !wids[b-1]){
		// remove more than one punct words. if we're ending on a quote
		// keep it
		while ( b > matchWordNum && !wids[b-2] && endQuoteWordNum != -1 && b > endQuoteWordNum ) {
			b--;
		}
		
		// do not break right after a "strong connector", like apostrophe
		while ( b > matchWordNum && (bb[b-2] & D_IS_STRONG_CONNECTOR) ) {
			b--;
		}
	}

	Match *ms = matches->m_matches;

	// make m_matches.m_matches[mi] the first match in our [a,b) window
	int32_t mi ;

	// . the match at the center of the window is match #"mm", so that
	//   matches->m_matches[mm] is the Match class
	// . set "mi" to it and back up "mi" as int32_t as >= a
	for ( mi = mm ; mi > 0 && ms[mi-1].m_wordNum >=a ; mi-- )
		;

	// now get the score of this excerpt. Also mark all the represented 
	// query words. Mark the represented query words in the array that
	// comes to us. also mark how many times the same word is repeated in
	// this summary.
	int64_t score = 0LL;

	// is a url contained in the summary, that looks bad! punish!
	bool hasUrl = false;

	// the word count we did above was just an approximate. count it right
	wordCount = 0;

	// for debug
	//char buf[5000];
	//char *xp = buf;
	SafeBuf xp;

	// wtf?
	if ( b > nw ) {
		b = nw;
	}

	// first score from the starting match down to a, including match
	for ( int32_t i = a ; i < b ; i++ ) {
		// debug print out
		if ( g_conf.m_logDebugSummary ) {
			int32_t len = words->getWordLen(i);
			char cs;
			for (int32_t k=0;k<len; k+=cs ) {
				const char *c = words->getWord(i)+k;
				cs = getUtf8CharSize(c);
				if ( is_binary_utf8 ( c ) ) {
					continue;
				}
				xp.safeMemcpy ( c , cs );
				xp.nullTerm();
			}
		}

		// skip if in bad section, marquee, select, script, style
		if ( sp && (sp[i]->m_flags & badFlags) ) {
			continue;
		}

		// don't count just numeric words
		if ( words->isNum(i) ) {
			continue;
		}

		// check if there is a url. best way to check for '://'
		if ( wids && !wids[i] ) {
			const char *wrd = words->getWord(i);
			int32_t  wrdLen = words->getWordLen(i);
			if ( wrdLen == 3 && wrd[0] == ':' && wrd[1] == '/' &&  wrd[2] == '/' ) {
				hasUrl = true;
			}
		}

		// skip if not wid
		if ( ! wids[i] ) {
			continue;
		}

		// just make every word 100 pts
		int32_t t = 100;

		// penalize it if in one of these sections
		if ( bb[i] & ( D_IN_PARENS | D_IN_SUP | D_IN_LIST ) ) {
			t /= 2;
		}

		// boost it if in bold or italics
		if ( bb[i] & D_IN_BOLDORITALICS ) {
			t *= 2;
		}

		// add the score for this word
		score += t;

		// print the score, "t"
		if ( g_conf.m_logDebugSummary ) {
			xp.safePrintf("(%" PRId32")",t);
		}

		// count the alpha words we got
		wordCount++;

		// if no matches left, skip
		if ( mi >= matches->m_numMatches ) {
			continue;
		}

		// get the match
		Match *next = &ms[mi];

		// skip if not a match
		if ( i != next->m_wordNum ) {
			continue;
		}

		// must be a match in this class
		if ( next->m_words != words ) {
			continue;
		}

		// advance it
		mi++;

		// which query word # does it match
		int32_t qwn = next->m_qwordNum;

		if ( qwn < 0 || qwn >= m_q->m_numWords ){g_process.shutdownAbort(true);}

		// undo old score
		score -= t;

		// add 100000 per match
		t = 100000;

		// weight based on tf, goes from 0.1 to 1.0
		t = (int32_t)((float)t * m_wordWeights [ qwn ]);

		// if it is a query stop word, make it 10000 pts
		if ( m_q->m_qwords[qwn].m_isQueryStopWord ) {
			t = 0;//10000;
		}

		// penalize it if in one of these sections
		if ( bb[i] & ( D_IN_PARENS | D_IN_SUP | D_IN_LIST ) ) {
			t /= 2;
		}

		if ( gotIt[qwn] > 0 ) {
			// have we matched it in this [a,b) already?
			if ( gotIt[qwn] == 1 ) {
				t /= 15;
			} else {
				// if we have more than 2 matches in the same window,
				// it may not give a good summary. give a heavy penalty
				t -= 200000;
			}
		} else if ( retired [qwn] > 0 ) {
			// have we matched it already in a winning window?
			t /= 12;
		}

		// add it back
		score += t;

		if ( g_conf.m_logDebugSummary ) {
			xp.safePrintf ("[%" PRId32"]{qwn=%" PRId32",ww=%f}",t,qwn,
				       m_wordWeights[qwn]);
		}

		// inc the query word count for this window
		if ( gotIt[qwn] < 100 ) {
			gotIt[qwn]++;
		}
	}

	int32_t oldScore = score;
	
	// apply the bonus if it starts or a sentence
	// only apply if the score is positive and if the wordcount is decent
	if ( score > 0 && wordCount > 7 ){
		// a match can give us 10k to 100k pts based on the tf weights
		// so we don't want to overwhelm that too much, so let's make
		// this a 20k bonus if it starts a sentence
		if ( bb[a] & D_STARTS_SENTENCE ) {
			score += 8000;
		} else if ( bb[a] & D_STARTS_FRAG ) {
			// likewise, a fragment, like after a comma
			score += 4000;
		}

		// 1k if the match word is very close to the
		// start of a sentence, lets say 3 alphawords
		if ( matchWordNum - a < 7 ) {
			score += 1000;
		}
	}

	// a summary isn't really a summary if its less than 7 words.
	// reduce the score, but still give it a decent score.
	// minus 5M.
	if ( wordCount < 7 ) {
		score -= 20000;
	}

	// summaries that cross a lot of tags are usually bad, penalize them
	if ( numTagsCrossed > 1 ) {
		score -= (numTagsCrossed * 20000);
	}

	if ( hasUrl ) {
		score -= 8000;
	}

	// show it
	if ( g_conf.m_logDebugSummary ) {
		log(LOG_DEBUG, "sum: score=%08" PRId32" prescore=%08" PRId32" a=%05" PRId32" b=%05" PRId32" %s",
		     (int32_t)score,oldScore,(int32_t)a,(int32_t)b,
		     xp.getBufStart());
	}

	// set lasta, besta, bestb
	*lasta = a;
	*besta = a;
	*bestb = b;

	return score;
}

// get summary when no search terms could be found
bool Summary::getDefaultSummary ( Xml *xml, Words *words, Sections *sections, Pos *pos, int32_t maxSummaryLen ) {
	char *p    = m_summary;

	if (MAX_SUMMARY_LEN < maxSummaryLen) {
		maxSummaryLen = MAX_SUMMARY_LEN;
	}

	// null it out
	m_summaryLen = 0;

	bool inLink   = false;
	char *pend = m_summary + maxSummaryLen - 2;
	int32_t start = -1,  numConsecutive = 0;
	int32_t bestStart = -1;
	int32_t bestEnd = -1;
	int32_t longestConsecutive = 0;
	int32_t lastAlnum = -1;
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE|SEC_IN_HEAD;
	// shortcut
	const nodeid_t  *tids = words->getTagIds();
	const int64_t *wids = words->getWordIds();

	// get the section ptr array 1-1 with the words, "sp"
	Section **sp = NULL;
	if ( sections ) {
		sp = sections->m_sectionPtrs;
	}

	for (int32_t i = 0;i < words->getNumWords(); i++){
		// skip if in bad section
		if ( sp && (sp[i]->m_flags & badFlags) ) {
			continue;
		}

		if (start > 0 && bestStart == start &&
		    ( words->getWord(i) - words->getWord(start) ) >=
		    ( maxSummaryLen - 8 )){
			longestConsecutive = numConsecutive;
			bestStart = start;
			bestEnd = lastAlnum;//i-1;
			break;
		}
		if (words->isAlnum(i) ) {
			if (!inLink) {
				numConsecutive++;
			}
			lastAlnum = i;
			if (start < 0) start = i;
			continue;
		}
		nodeid_t tid = tids[i] & BACKBITCOMP;
		// we gotta tag?
		if ( tid ) {
			// ignore <p> tags
			if ( tid == TAG_P ) {
				continue;
			}

			// is it a front tag?
			if ( tid && ! (tids[i] & BACKBIT) ) {
				if ( tid == TAG_A ) {
					inLink = true;
				}
			}
			else if ( tid ) {
				if ( tid == TAG_A ) {
					inLink = false;
				}
			}

			if ( ! isBreakingTagId(tid) )	
				continue;
		} else if ( ! wids[i] ) {
			continue;
		}
			
		// end of consecutive words
		if ( numConsecutive > longestConsecutive ) {
			longestConsecutive = numConsecutive;
			bestStart = start;
			bestEnd = i-1;
		}
		start = -1;
		numConsecutive = 0;
	}

	if (bestStart >= 0 && bestEnd > bestStart){
		p += pos->filter( words, bestStart, bestEnd, true, p, pend - 10, xml->getVersion() );

		// NULL terminate
		*p++ = '\0';

		// set length
		m_summaryLen = p - m_summary;

		if ( m_numDisplayLines > 0 ) {
			m_displayLen = m_summaryLen;
		}

		if ( m_summaryLen > 50000 ) { g_process.shutdownAbort(true); }
		return true;
	}

	return true;
}	
