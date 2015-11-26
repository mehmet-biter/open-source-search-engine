#include "Summary.h"
#include "Speller.h"
#include "Words.h"
//#include "AppendingWordsWindow.h"
#include "Sections.h"
#include "Msg20.h"

Summary::Summary()
            : m_summaryLocs(m_summaryLocBuf, 
			    MAX_SUMMARY_LOCS*sizeof(uint64_t)),
	      m_summaryLocsPops(m_summaryLocPopsBuf, 
				MAX_SUMMARY_LOCS*sizeof(int32_t)) {
	//m_buf = NULL;
	m_bitScoresBuf = NULL;
	m_bitScoresBufSize = 0;
	m_wordWeights = NULL;
	m_buf4 = NULL;
	reset();
}

Summary::~Summary() { reset(); }

void Summary::reset() {
	//if ( m_buf && m_freeBuf )
	//	mfree ( m_buf, m_bufMaxLen, "Summary" );
	if ( m_bitScoresBuf ){
		mfree ( m_bitScoresBuf, m_bitScoresBufSize,
			"SummaryBitScore" );
		m_bitScoresBuf = NULL;
		m_bitScoresBufSize = 0;
	}
	m_summaryLen = 0;
	m_displayLen = 0;
	//m_bufMaxLen = 0;
	//m_bufLen = 0;
	//m_buf = NULL;
	m_isNormalized = false;
	//m_freeBuf = true;
	m_numExcerpts = 0;
	m_summaryLocs.reset();
	m_summaryLocsPops.reset();
	if ( m_wordWeights && m_wordWeights != (float *)m_tmpBuf ) {
		mfree ( m_wordWeights , m_wordWeightSize , "sumww");
		m_wordWeights = NULL;
	}
	m_wordWeights = NULL;
	if ( m_buf4 && m_buf4 != m_tmpBuf4 ) {
		mfree ( m_buf4 , m_buf4Size , "ssstkb" );
		m_buf4 = NULL;
	}
}


//////////////////////////////////////////////////////////////////
//
// THE NEW SUMMARY GENERATOR
//
//////////////////////////////////////////////////////////////////

// returns false and sets g_errno on error
bool Summary::set2 ( Xml      *xml                ,
		     Words    *words              ,
		     Bits     *bits               ,
		     Sections *sections           ,
		     Pos      *pos                ,
		     Query    *q                  ,
		     int64_t *termFreqs         ,
		     float    *affWeights         , // 1-1 with qterms
		     //char     *coll               ,
		     //int32_t      collLen            ,
		     bool      doStemming         ,
		     int32_t      maxSummaryLen      , 
		     int32_t      maxNumLines        ,
		     int32_t      numDisplayLines    ,
		     int32_t      maxNumCharsPerLine ,
		     //int32_t      bigSampleRadius    ,
		     //int32_t      bigSampleMaxLen    ,
		     bool      ratInSummary       ,
		     //TitleRec *tr                 ,
		     Url      *f                  ,
		     //bool     allowPunctInPhrase  ,
		     //bool      excludeLinkText    ,
		     //bool      excludeMetaText    ,
		     //bool      hackFixWords       ,
		     //bool      hackFixPhrases     ,
		     //float    *queryProximityScore,
		     Matches  *matches            ,
		     char     *titleBuf           ,
		     int32_t      titleBufLen        ) {

	//m_proximityScore = -1;

	// pointless, possibly caller in Msg20 is just interested in
	// Msg20Request::m_computeLinkInfo or m_setLinkInfo. NO! we need
	// to see if it has all the query terms...
	//if ( maxNumLines <= 0 ) return true;

	m_numDisplayLines = numDisplayLines;
	m_displayLen      = 0;

	//m_useDateLists   = useDateLists;
	//m_exclDateList   = exclDateList;
	//m_begPubDateList = begPubDateList;
	//m_endPubDateList = endPubDateList;
	//m_diversity      = 1.0;
	// int64_t start = gettimeofdayInMilliseconds();
	// assume we got maxnumlines of summary
	if ( (maxNumCharsPerLine+6)*maxNumLines > maxSummaryLen ) {
		//maxNumCharsPerLine = (maxSummaryLen-10)/maxNumLines;
		if ( maxNumCharsPerLine < 10 ) maxNumCharsPerLine = 10;
		static char s_flag = 1;
		if ( s_flag ) {
			s_flag = 0;
			log("query: Warning. "
			    "Max summary excerpt length decreased to "
			    "%"INT32" chars because max summary excerpts and "
			    "max summary length are too big.",
			    maxNumCharsPerLine);
		}
	}

	// . sanity check
	// . summary must fit in m_summary[]
	// . leave room for tailing \0
	if ( maxSummaryLen >= MAX_SUMMARY_LEN ) {
		g_errno = EBUFTOOSMALL;
		return log("query: Summary too big to hold in buffer of %"INT32" "
			   "bytes.",(int32_t)MAX_SUMMARY_LEN);
	}

	// . hash query word ids into a small hash table
	// . we use this to see what words in the document are query terms
	//int32_t qscores [ MAX_QUERY_TERMS ];

	// and if we found each query term or not
	//int32_t nt  = q->getNumNonFieldedSingletonTerms();
	//int32_t nqt = q->getNumTerms();

	// do not overrun the final*[] buffers
	if ( maxNumLines > 256 ) { 
		g_errno = EBUFTOOSMALL; 
		return log("query: More than 256 summary lines requested.");
	}

	// . MORE BIG HACK
	// . since we're working with fielded query terms we must check BIG
	//   HACK here in case the fielded query term is the ONLY query 
	//   term.
	// . LOGIC MOVED INTO MATCHES.CPP

	// Nothing to match...print beginning of content as summary
	if ( matches->m_numMatches == 0 && maxNumLines > 0 )
		return getDefaultSummary ( xml,
					   words,
					   sections, // scores,
					   pos,
					   //bigSampleRadius,
					   maxSummaryLen );
	
	/*int64_t end = gettimeofdayInMilliseconds();
	if ( end - start > 2 )
		log ( LOG_WARN,"summary: took %"INT64" ms to finish big hack",
		      end - start );
		      start = gettimeofdayInMilliseconds();*/
	//
	int32_t need1 = q->m_numWords * sizeof(float);
	m_wordWeightSize = need1;
	if ( need1 < 128 )
		m_wordWeights = (float *)m_tmpBuf;
	else
		m_wordWeights = (float *)mmalloc ( need1 , "wwsum" );
	if ( ! m_wordWeights ) return false;



	// zero out all word weights
	for ( int32_t i = 0 ; i < q->m_numWords; i++ )
		m_wordWeights[i] = 0.0;

	// query terms
	int32_t numTerms = q->getNumTerms();

	// . compute our word weights wrt each query. words which are more rare
	//   have a higher weight. We use this to weight the terms importance 
	//   when generating the summary.
	// . used by the proximity algo
	// . used in setSummaryScores() for scoring summaries
	if ( termFreqs && q->m_numWords > 1 ) {
		float maxTermFreq = 0;
		for ( int32_t i = 0 ; i < numTerms ; i++ ) {
			// www.abc.com --> treat www.abc as same term freq
			// 'www.infonavit.gob.mx do de carne? mxa'
			//if(q->m_qterms[i].m_isPhrase) continue;
			if(termFreqs[i] > maxTermFreq)
				maxTermFreq = termFreqs[i];
		}
		maxTermFreq++; //don't div by 0!

		for ( int32_t i = 0 ; i < numTerms ; i++ ) {
			//if(q->m_qterms[i].m_isPhrase) continue;
			// if this is a phrase the other words following
			// the first word will have a word weight of 0
			// so should be ignored for that...
			int32_t ndx = q->m_qterms[i].m_qword - q->m_qwords;
			// oh it is already complemented up here
			m_wordWeights[ndx] = 1.0 -
				((float)termFreqs[i] / maxTermFreq);
			//make sure everything has a little weight:
			if(m_wordWeights[ndx] < .10) m_wordWeights[ndx] = .10;
			//log(LOG_WARN,
			//"query word num %"INT32" termnum %"INT32" freq %f max %f",
			//ndx,i,m_wordWeights[ndx],maxTermFreq);
		}
	} 
	else {
		for ( int32_t i = 0 ; i < q->m_numWords; i++ )
			m_wordWeights[i] = 1.0;
	}

	if ( g_conf.m_logDebugSummary ) {
		for ( int32_t i = 0 ; i < q->m_numWords; i++ ) {
			int64_t tf = -1;
			if ( termFreqs ) tf = termFreqs[i];
			log("sum: u=%s wordWeights[%"INT32"]=%f tf=%"INT64"",
			    f->m_url,i,m_wordWeights[i],tf);
		}
	}

	// convenience
	m_maxNumCharsPerLine = maxNumCharsPerLine;
	//m_qscores            = qscores;
	m_q                  = q;

	//m_proximityScore = 0;

	bool hadEllipsis = false;

	// set the max excerpt len to the max summary excerpt len
	int32_t maxExcerptLen = m_maxNumCharsPerLine;

	int32_t lastNumFinal = 0;
	int32_t maxLoops = 1024;
	char *p, *pend;

	// if just computing absScore2...
	if ( maxNumLines <= 0 )//&& bigSampleRadius <= 0 )
		return true;//return matches->m_hasAllQueryTerms;

	p    = m_summary;
	pend = m_summary + maxSummaryLen;
	m_numExcerpts = 0;

	int32_t need2 = (1+1+1) * m_q->m_numWords;
	m_buf4Size = need2;
	if ( need2 < 128 )
		m_buf4 = m_tmpBuf4;
	else
		m_buf4 = (char *)mmalloc ( need2 , "stkbuf" );
	if ( ! m_buf4 ) return false;
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
	for ( int32_t i = 0 ; i < m_q->m_numWords ; i++ )
		if ( matches->m_qwordFlags[i] & MF_TITLEGEN )
			retired [ i ] = 1;

	// 
	// Loop over all words that match a query term. The matching words
	// could be from any one of the 3 Words arrays above. Find the
	// highest scoring window around each term. And then find the highest
	// of those over all the matching terms.
	//
	int32_t numFinal;
	for ( numFinal = 0; numFinal < maxNumLines; numFinal++ ){

		if ( numFinal == m_numDisplayLines )
			m_displayLen = p - m_summary;

		// reset these at the top of each loop
		Match     *maxm;
		int64_t  maxScore = 0;
		int32_t       maxa = 0;
		int32_t       maxb = 0;
		int32_t       maxi  = -1;
		int32_t       lasta = -1;
		//char       maxGotIt [ MAX_QUERY_WORDS ];

		if(lastNumFinal == numFinal) {
			if(maxLoops-- <= 0) {
				log(LOG_WARN, "query: got infinite loop "
				    "bug, query is %s url is %s",
				    m_q->m_orig,
				    f->getUrl());
				break;
			}
		}
		lastNumFinal = numFinal;
		// int64_t stget = gettimeofdayInMilliseconds();
		// does the max that we found have a new query word that was
		// not already in the summary?
		//int32_t maxFoundNew = 0;
		// loop through all the matches and see which is best
		for ( int32_t i = 0 ; i < matches->m_numMatches ; i++ ) {
			int32_t       a , b;
			// reset lasta if we changed words class
			if ( i > 0 && matches->m_matches[i-1].m_words !=
			     matches->m_matches[i].m_words )
				lasta = -1;

			// only use matches in title, etc.
			mf_t flags = matches->m_matches[i].m_flags;

			bool skip = true;
			if ( flags & MF_METASUMM ) skip = false;
			if ( flags & MF_METADESC ) skip = false;
			if ( flags & MF_BODY     ) skip = false;
			if ( flags & MF_DMOZSUMM ) skip = false;
			if ( flags & MF_RSSDESC  ) skip = false;
			if ( skip ) continue;

			// ask him for the query words he matched
			//char gotIt [ MAX_QUERY_WORDS ];
			// clear it for him
			memset ( gotIt, 0, m_q->m_numWords * sizeof(char) );

			// . get score of best window around this match
			// . do not allow left post of window to be <= lasta to
			//   avoid repeating the same window.
			int64_t score = getBestWindow (matches, 
							 i, 
							 &lasta,
							 &a, &b, 
							 gotIt ,
							 retired ,
							 maxExcerptLen);
			
			// USE THIS BUF BELOW TO DEBUG THE ABOVE CODE. 
			// PRINTS OUT THE SUMMARY
			/*
			//if ( score >=12000 ) {
			char buf[10*1024];
			char *xp = buf;
			if ( i == 0 )
				log (LOG_WARN,"=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
			sprintf(xp, "score=%08"INT32" a=%05"INT32" b=%05"INT32" ",
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
				sprintf ( xp ,"(%"INT32")",s);
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
			  //log(LOG_WARN,"summary: %"INT32") %s - %"INT64"",i,bufPtr, 
			  //score);
			  log(LOG_WARN,"summary: %"INT32") %s - %"INT64"",i,bufPtr, 
			  score);
			*/

			// skip if was in title or something
			if ( score <= 0 ) continue;
			// skip if not a winner
			if ( maxi >= 0 && score <= maxScore ) continue;

			// we got a new winner
			maxi     = i;
			maxa     = a;
			maxb     = b;
			maxScore = score;
			// save this too
			gbmemcpy ( maxGotIt , gotIt , m_q->m_numWords );

		}
	
		// retire the query words in the winning summary

		
		//log( LOG_WARN,"summary: took %"INT64" ms to finish getbestwindo",
		//    gettimeofdayInMilliseconds() - stget );


		// all done if no winner was made
		if ( maxi == -1 ) break;

		// sanity check
		//if ( maxa == -1 || maxb == -1 ) { char *xx = NULL; *xx = 0; }
		if ( maxa == -1 ) break;
		if ( maxb == -1 ) break;

		// who is the winning match?
		maxm = &matches->m_matches[maxi];
		Words         *ww      = maxm->m_words;
		// we now use "m_swbits" for the summary bits since they are
		// of size sizeof(swbit_t), a int16_t at this point
		swbit_t       *bb      = maxm->m_bits->m_swbits;

		// this should be impossible
		if ( maxa > ww->m_numWords || maxb > ww->m_numWords ){
			log ( LOG_WARN,"query: summary starts or ends after "
			      "document is over! maxa=%"INT32" maxb=%"INT32" nw=%"INT32"",
			      maxa, maxb, ww->m_numWords );
			maxa = ww->m_numWords - 1;
			maxb = ww->m_numWords;
			//char *xx = NULL; *xx = 0;
		}

		// assume we do not preceed with ellipsis "..."
		bool needEllipsis = true;
		
		// rule of thumb, don't use ellipsis if the first letter is 
		// capital, or a non letter
		char *c = ww->m_words[maxa]+0;
		if      ( ! is_alpha_utf8(c) ) needEllipsis = false;
		else if (   is_upper_utf8(c) ) needEllipsis = false;

		// is punct word before us pair acrossable? if so then we
		// probably are not the start of a sentence.
		if ( bb[maxa] & D_STARTS_SENTENCE ) needEllipsis = false;

		// or if into the sample and previous excerpt had an ellipsis
		// do not bother using one for us.
		if ( p > m_summary && hadEllipsis ) needEllipsis = false;

		if ( needEllipsis ) {
			// break out if no room for "..."
			//int32_t elen;
			if ( p + 4 + 2 > pend ) break;
			// space first?
			if ( p > m_summary ) *p++ = ' ';
			gbmemcpy ( p , "... " , 4 );
			p += 4;
		}

		// separate summary excerpts with a single space.
		if ( p > m_summary ) {
			if ( p + 2 > pend ) break;
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
		int32_t len = pos->filter(p, pend, ww, maxa, maxb);

		// break out if did not fit
		if ( len == 0 ) break;
		// don't consider it if it is a substring of the title
		if ( len == titleBufLen &&
		     strncasestr(titleBuf, p, titleBufLen, len) ) {
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
			if ( retired[i] >= 100 ) continue;
			retired [ i ] += maxGotIt [ i ];
		}
	
		// add all the scores of the excerpts to the doc summary score.
		// zero out scores of the winning sample so we don't get them 
		// again. use negative one billion to ensure that we don't get
		// them again
		for ( int32_t j = maxa ; j < maxb ; j++ )
			// mark it as used
			bb[j] |= D_USED;

		// if we ended on punct that can be paired across we need
		// to add an ellipsis
		if ( needEllipsis ) {
			if ( p + 4 + 2 > pend ) break;
			gbmemcpy ( p , " ..." , 4 );
			p += 4;
		}

		// try to put in a small summary excerpt if we have atleast
		// half of the normal excerpt length left
		if ( maxExcerptLen == m_maxNumCharsPerLine && 
		     //pos->m_pos[maxb] - pos->m_pos[maxa] 
		     len <= ( m_maxNumCharsPerLine / 2 + 1 ) ){
			maxExcerptLen = m_maxNumCharsPerLine / 2;
			// don't count it in the finals since we try to get a
			// small excerpt
			numFinal--;
		}
		else if ( m_numExcerpts < MAX_SUMMARY_EXCERPTS &&
			  m_numExcerpts >= 0 ) {
			m_summaryExcerptLen[m_numExcerpts] = p - m_summary;
			m_numExcerpts++;
			// also reset maxExcerptLen
			maxExcerptLen = m_maxNumCharsPerLine;
		}
	
	skip:
		// zero out the scores so they will not be used in others
		for ( int32_t j = maxa ; j < maxb ; j++ )
			// mark it
			bb[j] |= D_USED;
	}

	if ( numFinal <= m_numDisplayLines )
		m_displayLen = p - m_summary;

	/*end = gettimeofdayInMilliseconds();
	if ( end - start > 10 )
		log ( LOG_WARN,"summary: took %"INT64"ms to finish doing summary "
		      "numMatches=%"INT32" maxNumLines=%"INT32" url=%s", end - start,
		      matches.m_numMatches, maxNumLines, f->m_url );
		      start = gettimeofdayInMilliseconds();*/

	// If we still didn't find a summary, directly use whats given in the
	// meta summary or description.
	if ( p == m_summary ){
		Words    *wp;
		Pos      *pp;
		Sections *ss;
		// get it from the summary
		if      ( matches->getMatchGroup(MF_METASUMM ,&wp,&pp,&ss) )
			p += pp->filter(p,pend, wp, 0, wp->m_numWords );
		else if ( matches->getMatchGroup(MF_METADESC,&wp,&pp,&ss) )
			p += pp->filter(p,pend, wp, 0, wp->m_numWords );
		if ( p != m_summary ){
			m_summaryExcerptLen[0] = p - m_summary;
			m_numExcerpts = 1;
		}
		// in this case we only have one summary line
		if ( m_numDisplayLines > 0 )
			m_displayLen = p - m_summary;
	}

	// free the mem we used if we allocated it
	if ( m_buf4 && m_buf4 != m_tmpBuf4 ) {
		mfree ( m_buf4 , m_buf4Size , "ssstkb" );
		m_buf4 = NULL;
	}


	// If we still didn't find a summary, get the default summary
	if ( p == m_summary ) {
		// then return the default summary
		bool status = getDefaultSummary ( xml,
						  words,
						  sections,
						  pos,
						  //bigSampleRadius,
						  maxSummaryLen );
		if ( m_numDisplayLines > 0 )
			m_displayLen = m_summaryLen;
		
		return status;
	}

	// if we don't find a summary, theres no need to NULL terminate
	if ( p != m_summary ) *p++ = '\0';

	// set length
	m_summaryLen = p - m_summary;

	if ( m_summaryLen > 50000 ) { char*xx=NULL;*xx=0; }

	// it may not have all query terms if rat=0 (Require All Terms=false)
	// so use Matches::m_matchesQuery instead of Matches::m_hasAllQTerms
	//if ( ! matches->m_matchesQuery )
	//	log("query: msg20: doc %s missing query terms for q=%s",
	//	    f->getUrl(),m_q->m_orig );

	return true;
}

// . usually we get more summary lines than displayed so that the summary
//   deduped, XmlDoc::getSummaryVector(), has adequate sample space
// . "max excerpts". we truncate the summary if we need to.
//   XmlDoc.cpp::getSummary(), likes to request more excerpts than are 
//   actually displayed so it has a bigger summary for deduping purposes.
int32_t Summary::getSummaryLen ( int32_t maxLines ) {
	int32_t len = 0;
	for ( int32_t i = 0 ; i < m_numExcerpts && i < maxLines ; i++ ) 
		len += m_summaryExcerptLen[i];
	return len;
}

// MDW: this logic moved mostly to Bits::setForSummary() and
// Summary::set2(). See the gigawiki url to see the rules for summary
// generation: http://10.5.1.202:237/eng_wiki/index.php/Eng:Projects
// i removed this whole function so use git diff to see it later if you
// need to. setSummaryScores() is obsoleted.

// . return the score of the highest-scoring window containing match #m
// . window is defined by the half-open interval [a,b) where a and b are 
//   word #'s in the Words array indicated by match #m
// . return -1 and set g_errno on error
int64_t Summary::getBestWindow ( Matches *matches       ,
				   int32_t     mm            ,
				   int32_t    *lasta         ,
				   int32_t    *besta         ,
				   int32_t    *bestb         ,
				   char    *gotIt         ,
				   char    *retired       ,
				   int32_t     maxExcerptLen ) {


	// get the window around match #mm
	Match *m = &matches->m_matches[mm];
	// what is the word # of match #mm?
	int32_t matchWordNum = m->m_wordNum;

	// what Words/Pos/Bits classes is this match in?
	Words         *words   = m->m_words;
	Section      **sp      = NULL;
	int32_t          *pos     = m->m_pos->m_pos;
	// use "m_swbits" not "m_bits", that is what Bits::setForSummary() uses
	swbit_t       *bb      = m->m_bits->m_swbits;

	// int16_tcut
	if ( m->m_sections ) sp = m->m_sections->m_sectionPtrs;

	int32_t            nw        = words->getNumWords();
	int64_t      *wids      = words->getWordIds();
	nodeid_t       *tids      = words->getTagIds();

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

	// . we NULLify the section ptrs if we already used the word in another
	//   summary.
	// . google seems to index SEC_MARQUEE, so i took that out of here
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE;
	if ( (bb[matchWordNum] & D_USED) || 
	     ( sp && (sp[matchWordNum]->m_flags & badFlags) ) ) {
		// assume no best window
		*besta = -1;
		*bestb = -1;
		*lasta = matchWordNum;
		return 0;
	}

	// . "a" is the left fence post of the window (it is a word # in Words)
	// . go to the left as far as we can 
	// . thus we decrement "a"
	int32_t a    = matchWordNum;
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
		//if ( wscores[a-1] == -1000000000 || 
		if ( (bb[a-1]&D_USED) ||
		     // stop on a title word as well
		     //wscores[a-1] == -20000000 || 
		     // stop if its the start of a sentence, too
		     bb[a] & D_STARTS_SENTENCE ){
			goodStart = true;
			break;
		}
		// stop before title word
		if ( bb[a-1] & D_IN_TITLE ) {
			goodStart = true;
			break;
		}
		// don't go beyond an LI, TR, P tag
		if ( tids && ( tids[a-1] == TAG_LI ||
			       tids[a-1] == TAG_TR ||
			       tids[a-1] == TAG_P  ||
			       tids[a-1] == TAG_DIV ) ){
			goodStart = true;
			break;
		}
		// stop if its the start of a quoted sentence
		if ( a+1<nw && (bb[a+1] & D_IN_QUOTES) && 
		     words->m_words[a][0] == '\"' ){
			startOnQuote = true;
			goodStart    = true;
			break;
		}
		// find out the first instance of a fragment (comma, etc)
		// watch out! because frag also means 's' in there's
		if ( ( bb[a] & D_STARTS_FRAG ) && 
		     !(bb[a-1] & D_IS_STRONG_CONNECTOR) && firstFrag == -1 )
			firstFrag = a;
		if ( wids[a] ) wordCount++;
	}

	// if didn't find a good start, then start at the start of the frag
	if ( !goodStart && firstFrag != -1 )
		a = firstFrag;

	// don't let punct or tag word start a line, unless a quote
	if ( a < matchWordNum && !wids[a] && words->m_words[a][0] != '\"' ){
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
	for ( ; b <= nw; b++ ){
		if ( b == nw ) break;
		if ( pos[b+1] - pos[a] >= maxExcerptLen ) break;
		
		if ( startOnQuote && words->m_words[b][0] == '\"' )
			endQuoteWordNum = b;
		// don't include any dead zone, those are already-used samples
		//if ( wscores[b] == -1000000000 ) break;
		if ( bb[b]&D_USED ) break;
		// stop on a title word
		//if ( wscores[b] == -20000000   ) break;
		// stop on a title word
		if ( bb[b] & D_IN_TITLE ) break;
		if ( wids[b] ) wordCount++;
		// don't go beyond an LI or TR backtag
		if ( tids && ( tids[b] == (BACKBIT|TAG_LI) ||
			       tids[b] == (BACKBIT|TAG_TR) ) ){
			numTagsCrossed++;
			// try to have atleast 10 words in the summary
			if ( wordCount > 10 )
				break;
		}
		// go beyond a P or DIV backtag in case the earlier char is a
		// ':'. This came from a special case for wikipedia pages 
		// eg. http://en.wikipedia.org/wiki/Flyover
		if ( tids && ( tids[b] == (BACKBIT|TAG_P)  ||
			       tids[b] == (BACKBIT|TAG_DIV) )){
			numTagsCrossed++;
			// try to have atleast 10 words in the summary
			if ( wordCount > 10 && words->m_words[b-1][0] != ':' )
				break;
		}
	}
	
	// don't end on a lot of punct words
	if ( b > matchWordNum && !wids[b-1]){
		// remove more than one punct words. if we're ending on a quote
		// keep it
		while ( b > matchWordNum && !wids[b-2] && 
			endQuoteWordNum != -1 && b > endQuoteWordNum )
			b--;
		
		// do not break right after a "strong connector", like 
		// apostrophe
		while ( b > matchWordNum && (bb[b-2] & D_IS_STRONG_CONNECTOR) )
			b--;
		
	}

	// a int16_tcut
	Match *ms = matches->m_matches;
	// make m_matches.m_matches[mi] the first match in our [a,b) window
	int32_t mi ;
	// . the match at the center of the window is match #"mm", so that
	//   matches->m_matches[mm] is the Match class
	// . set "mi" to it and back up "mi" as int32_t as >= a
	for ( mi = mm ; mi >= 0 && ms[mi-1].m_wordNum >=a ; mi-- ) ;

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
	if ( b > nw ) b = nw;

	// first score from the starting match down to a, including match
	for ( int32_t i = a ; i < b ; i++ ) {

		// debug print out
		if ( g_conf.m_logDebugSummary ) {
			int32_t len = words->getWordLen(i);
			char cs;
			for(int32_t k=0;k<len; k+=cs ) {
				char *c = words->m_words[i]+k;
				cs = getUtf8CharSize(c);
				if ( is_binary_utf8 ( c ) ) continue;
				xp.safeMemcpy ( c , cs );
				xp.nullTerm();
			}
		}

		//if ( wscores[i] < 0 ) continue;
		// skip if in bad section, marquee, select, script, style
		if ( sp && (sp[i]->m_flags & badFlags) ) continue;
		// don't count just numeric words
		if ( words->isNum(i) ) continue;
		// check if there is a url. best way to check for '://'
		if ( wids && !wids[i] ){
			char *wrd = words->m_words[i];
			int32_t  wrdLen = words->m_wordLens[i];
			if ( wrdLen == 3 &&
			     wrd[0] == ':' && wrd[1] == '/' &&  wrd[2] == '/' )
				hasUrl = true;
		}
		// get the score
		//int32_t t = wscores[i];
		// just make every word 100 pts
		int32_t t = 100;
		// penalize it if in one of these sections
		if ( bb[i] & ( D_IN_PARENS     | 
			       D_IN_HYPERLINK  | 
			       D_IN_LIST       |
			       D_IN_SUP        | 
			       D_IN_BLOCKQUOTE ) )
			//t /= 3;
			// backoff since posbd has best window
			// in some links, etc.
			//t *= .85;
			t *= 1;
		// boost it if in bold or italics
		if ( bb[i] & D_IN_BOLDORITALICS ) t *= 2;
		// add the score for this word
		score += t;

		// print the score, "t"
		if ( g_conf.m_logDebugSummary ) {
			xp.safePrintf("(%"INT32")",t);
		}

		// skip if not wid
		if ( ! wids[i] ) continue;
		// count the alpha words we got
		wordCount++;
		// if no matches left, skip
		if ( mi >= matches->m_numMatches ) continue;
		// get the match
		Match *next = &ms[mi];
		// skip if not a match
		if ( i != next->m_wordNum ) continue;
		// must be a match in this class
		if ( next->m_words != words ) continue;
		// advance it
		mi++;
		// which query word # does it match
		int32_t qwn = next->m_qwordNum;

		if ( qwn < 0 || qwn >= m_q->m_numWords ){char*xx=NULL;*xx=0;}

		// undo old score
		score -= t;
		// add 100000 per match
		t = 100000;
		// weight based on tf, goes from 0.1 to 1.0
		t = (int32_t)((float)t * m_wordWeights [ qwn ]);
		// if it is a query stop word, make it 10000 pts
		if ( m_q->m_qwords[qwn].m_isQueryStopWord ) t = 0;//10000;

		// have we matched it in this [a,b) already?
		if ( gotIt[qwn] > 0 ) t /= 15;
		// have we matched it already in a winning window?
		else if ( retired [qwn] > 0 ) t /= 12;

		// add it back
		score += t;

		if ( g_conf.m_logDebugSummary ) {
			xp.safePrintf ("[%"INT32"]{qwn=%"INT32",ww=%f}",t,qwn,
				       m_wordWeights[qwn]);
		}

		// inc the query word count for this window
		if ( gotIt[qwn] < 100 ) gotIt[qwn]++;
	}

	int32_t oldScore = score;
	
	// apply the bonus if it starts or a sentence
	// only apply if the score is positive and if the wordcount is decent
	if ( score > 0 && wordCount > 7 ){
		// a match can give us 10k to 100k pts based on the tf weights
		// so we don't want to overwhelm that too much, so let's make
		// this a 20k bonus if it starts a sentence
		if ( bb[a] & D_STARTS_SENTENCE ) score += 8000;
		// likewise, a fragment, like after a comma
		else if ( bb[a] & D_STARTS_FRAG ) score += 4000;
		// 1k if the match word is very close to the
		// start of a sentence, lets say 3 alphawords
		if ( matchWordNum - a < 7 ) score += 1000;
		// 20M in case of meta stuff, and rss description, which 
		// should be the best summary. so give a huge boost
		if ( ! tids ) score += 20000000;
	}

	// a summary isn't really a summary if its less than 7 words.
	// reduce the score, but still give it a decent score.
	// minus 5M.
	if ( wordCount < 7 ) score -= 20000;

	// summaries that cross a lot of tags are usually bad, penalize them
	if ( numTagsCrossed > 1 ) score -= (numTagsCrossed * 20000);

	if ( hasUrl ) score -= 8000;

	// show it
	if ( g_conf.m_logDebugSummary )
		logf(LOG_DEBUG,"score=%08"INT32" prescore=%08"INT32" a=%05"INT32" b=%05"INT32" %s",
		     (int32_t)score,oldScore,(int32_t)a,(int32_t)b,
		     xp.getBufStart());

	// set lasta, besta, bestb
	*lasta = a;
	*besta = a;
	*bestb = b;

	return score;
}

// get summary when no search terms could be found
bool Summary::getDefaultSummary ( Xml    *xml,
				  Words  *words,
				  Sections *sections,
				  Pos    *pos,
				  int32_t    maxSummaryLen ){

	char *p    = m_summary;
	if (MAX_SUMMARY_LEN < maxSummaryLen) 
		maxSummaryLen = MAX_SUMMARY_LEN;

	// null it out
	m_summaryLen = 0;

	// try the meta summary tag
	if ( m_summaryLen <= 0 )
		m_summaryLen = xml->getMetaContent ( p , maxSummaryLen , 
						     "summary",7);

	// the meta descr
	if ( m_summaryLen <= 0 )
		m_summaryLen = xml->getMetaContent(p,maxSummaryLen,
						   "description",11);


	if ( m_numDisplayLines > 0 )
		m_displayLen = m_summaryLen;

	if ( m_summaryLen > 0 ) {
		m_summaryExcerptLen[0] = m_summaryLen;
		m_numExcerpts = 1;
		return true;
	}

	bool inLink   = false;
	char *pend = m_summary + maxSummaryLen - 2;
	int32_t start = -1,  numConsecutive = 0;
	int32_t bestStart = -1;
	int32_t bestEnd = -1;
	int32_t longestConsecutive = 0;
	int32_t lastAlnum = -1;
	// google seems to index SEC_MARQUEE, so i took that out of here
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE;
	// int16_tcut
	nodeid_t  *tids = words->m_tagIds;
	int64_t *wids = words->getWordIds();

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
		    ( words->m_words[i] - words->m_words[start] ) >= 
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
		int32_t len = pos->filter(p, pend-10, words, bestStart, bestEnd);
		p += len;
		if ( len > 0 && p + 3 + 2 < pend ){
			// space first?
			if ( p > m_summary ) *p++ = ' ';
			gbmemcpy ( p , "..." , 3 );
			p += 3;
		}
		// NULL terminate
		*p++ = '\0';
		// set length
		m_summaryLen = p - m_summary;

		if ( m_numDisplayLines > 0 )
			m_displayLen = m_summaryLen;

		if ( m_summaryLen > 50000 ) { char*xx=NULL;*xx=0; }
		return true;
	}
	return true;
}	
