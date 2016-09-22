#include "gb-include.h"

#include "Matches.h"
#include "Query.h"
#include "Titledb.h"  // for getting total # of docs in db
#include "StopWords.h"
#include "Phrases.h"
#include "Title.h"
#include "Domains.h"
#include "Sections.h"
#include "Linkdb.h"
#include "BitOperations.h"
#include "Process.h"


// TODO: have Matches set itself from all the meta tags, titles, link text,
//       neighborhoods and body. then proximity algo can utilize that info
//       as well as the summary generator, Summary.cpp. right now prox algo
//       was setting all those different classes itself.

Matches::Matches()
  : m_qwordFlags(NULL),
    m_numMatches(0),
    m_numSlots(0),
    m_q(NULL),
    m_numAlnums(0),
    m_qwordAllocSize(0),
    m_numMatchGroups(0)
{
}


Matches::~Matches() {
	reset();
}

void Matches::reset() { 
	reset2();
	if ( m_qwordFlags && m_qwordFlags != (mf_t *)m_tmpBuf ) {
		mfree ( m_qwordFlags , m_qwordAllocSize , "mmqw" );
		m_qwordFlags = NULL;
	}
}

void Matches::reset2() {
	m_numMatches = 0;
	m_numAlnums  = 0;
	// free all the classes' buffers
	for ( int32_t i = 0 ; i < m_numMatchGroups ; i++ ) {
		m_wordsArray   [i].reset();
		m_posArray     [i].reset();
		m_bitsArray    [i].reset();
	}
	m_numMatchGroups = 0;
}

bool Matches::isMatchableTerm(const QueryTerm *qt) const {
	QueryWord *qw = qt->m_qword;
	// not derived from  a query word? how?
	if ( ! qw ) return false;
	if ( qw->m_ignoreWord == IGNORE_DEFAULT        ) return false;
	if ( qw->m_ignoreWord == IGNORE_FIELDNAME      ) return false;
	if ( qw->m_ignoreWord == IGNORE_BOOLOP         ) return false;
	// take this out for now so we highlight for title: terms
	if ( qw->m_fieldCode && qw->m_fieldCode != FIELD_TITLE ) return false;
	// what word # are we?
	int32_t qwn = qw - m_q->m_qwords;
	// do not include if in a quote and does not start it!!
	if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != qwn ) return false;
	// if query is too long, a query word can be truncated!
	// this happens for some words if they are ignored, too!
	if ( ! qw->m_queryWordTerm && ! qw->m_queryPhraseTerm ) return false;
	// after a NOT operator?
	if ( qw->m_underNOT ) 
		return false;
	return true;
}

void Matches::setQuery ( Query *q ) { 
	reset();
	// save it
	m_q       = q;

	if ( m_qwordFlags ) {
		g_process.shutdownAbort(true);
	}

	int32_t need = m_q->m_numWords * sizeof(mf_t) ;
	m_qwordAllocSize = need;

	if ( need < 128 ) 
		m_qwordFlags = (mf_t *)m_tmpBuf;
	else
		m_qwordFlags = (mf_t *)mmalloc ( need , "mmqf" );

	if ( ! m_qwordFlags ) {
		log("matches: alloc failed for query %s",q->m_orig);
		return;
	}

	// this is word based. these are each 1 byte
	memset ( m_qwordFlags  , 0 , m_q->m_numWords * sizeof(mf_t));

	// # of WORDS in the query
	int32_t nqt = m_q->m_numTerms;

	// how many query words do we have that can be matched?
	int32_t numToMatch = 0;
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		// get query word #i
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if ignored *in certain ways only*
		if ( ! isMatchableTerm ( qt ) ) {
			continue;
		}
		// count it
		numToMatch++;
		// don't breach. MDW: i made this >= from > (2/11/09)
		if ( numToMatch < MAX_QUERY_WORDS_TO_MATCH ) continue;
		// note it
		log("matches: hit %" PRId32" max query words to match limit",
		    (int32_t)MAX_QUERY_WORDS_TO_MATCH);
		break;
	}

	// fix a core the hack way for now!
	if ( numToMatch < 256 ) numToMatch = 256;

	// keep number of slots in hash table a power of two for fast hashing
	m_numSlots = getHighestLitBitValue ( (uint32_t)(numToMatch * 3));
	// make the hash mask
	uint32_t mask = m_numSlots - 1;
	int32_t          n;
	// sanity check
	if ( m_numSlots > MAX_QUERY_WORDS_TO_MATCH * 3 ) {
		g_process.shutdownAbort(true); }

	// clear hash table
	memset ( m_qtableIds   , 0 , m_numSlots * 8 );
	memset ( m_qtableFlags , 0 , m_numSlots     );

	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		// get query word #i
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if ignored *in certain ways only*
		if ( ! isMatchableTerm ( qt ) ) {
			continue;
		}

		// get the word it is from
		QueryWord *qw = qt->m_qword;

		// get word #
		int32_t qwn = qw - q->m_qwords;

		// do not overfill table
		if ( i >= MAX_QUERY_WORDS_TO_MATCH ) {
			break;
		}

		// this should be equivalent to the word id
		int64_t qid = qt->m_rawTermId;//qw->m_rawWordId;

		// but NOT for 'cheatcodes.com'
		if ( qt->m_isPhrase ) qid = qw->m_rawWordId;

		// if its a multi-word synonym, like "new jersey" we must
		// index the individual words... or compute the phrase ids
		// for all the words in the doc. right now the qid is
		// the phrase hash for this guy i think...
		if ( qt->m_synonymOf && qt->m_numAlnumWordsInSynonym == 2 )
			qid = qt->m_synWids0;

		// put in hash table
		n = ((uint32_t)qid) & mask;

		// chain to an empty slot
		while ( m_qtableIds[n] && m_qtableIds[n] != qid ) 
			if ( ++n >= m_numSlots ) n = 0;

		// . if already occupied, do not overwrite this, keep this
		//   first word, the other is often ignored as IGNORE_REPEAT
		// . what word # in the query are we. save this.
		if ( ! m_qtableIds[n] ) m_qtableWordNums[n] = qwn;

		// store it
		m_qtableIds[n] = qid;

		// in quotes? this term may appear multiple times in the
		// query, in some cases in quotes, and in some cases not.
		// we need to know either way for logic below.
		if ( qw->m_inQuotes ) m_qtableFlags[n] |= 0x02;
		else                  m_qtableFlags[n] |= 0x01;

		// this is basically a quoted synonym
		if ( qt->m_numAlnumWordsInSynonym == 2 )
			m_qtableFlags[n] |=  0x08;

		//QueryTerm *qt = qw->m_queryWordTerm;
		if ( qt && qt->m_termSign == '+' ) m_qtableFlags[n] |= 0x04;

		//
		// if query has e-mail, then index phrase id "email" so
		// it matches "email" in the doc.
		// we need this for the 'cheat codes' query as well so it
		// highlights 'cheatcodes'
		//
		int64_t pid = qw->m_rawPhraseId;
		if ( pid == 0 ) continue;
		// put in hash table
		n = ((uint32_t)pid) & mask;
		// chain to an empty slot
		while ( m_qtableIds[n] && m_qtableIds[n] != pid ) 
			if ( ++n >= m_numSlots ) n = 0;
		// this too?
		if ( ! m_qtableIds[n] ) m_qtableWordNums[n] = qwn;
		// store it
		m_qtableIds[n] = pid;

	}
}

// . this was in Summary.cpp, but is more useful here
// . we can also use this to replace the proximity algo setup where it
//   fills in the matrix for title, link text, etc.
// . returns false and sets g_errno on error
bool Matches::set( Words *bodyWords, Phrases *bodyPhrases, Sections *bodySections, Bits *bodyBits,
				   Pos *bodyPos, Xml *bodyXml, Title *tt, Url *firstUrl, LinkInfo *linkInfo, int32_t niceness ) {
	// don't reset query info!
	reset2();

	// . first add all the matches in the body of the doc
	// . add it first since it will kick out early if too many matches
	//   and we get all the explicit bits matched
	if ( !addMatches( bodyWords, bodyPhrases, bodySections, bodyBits, bodyPos, MF_BODY ) ) {
		return false;
	}

	// add the title in
	if ( !addMatches( tt->getTitle(), tt->getTitleLen(), MF_TITLEGEN, niceness ) ) {
		return false;
	}

	// add in the url terms
	if ( !addMatches( firstUrl->getUrl(), firstUrl->getUrlLen(), MF_URL, niceness ) ) {
		return false;
	}

	// also use the title from the title tag, because sometimes it does not equal "tt->getTitle()"
	int32_t  a     = tt->getTitleTagStart();
	int32_t  b     = tt->getTitleTagEnd();

	char *start = NULL;
	char *end   = NULL;
	if ( a >= 0 && b >= 0 && b>a ) {
		start = bodyWords->getWord(a);
		end   = bodyWords->getWord(b-1) + bodyWords->getWordLen(b-1);
		if ( !addMatches( start, end - start, MF_TITLETAG, niceness ) ) {
			return false;
		}
	}

	// now add in the meta tags
	int32_t n = bodyXml->getNumNodes();
	XmlNode *nodes = bodyXml->getNodes();

	// find the first meta summary node
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// continue if not a meta tag
		if ( nodes[i].m_nodeId != TAG_META ) continue;
		// only get content for <meta name=..> not <meta http-equiv=..>
		int32_t tagLen;
		char *tag = bodyXml->getString ( i , "name" , &tagLen );
		// is it an accepted meta tag?
		int32_t flag = 0;
		if (tagLen== 7&&strncasecmp(tag,"keyword"    , 7)== 0)
			flag = MF_METAKEYW;
		if (tagLen== 7&&strncasecmp(tag,"summary"    , 7)== 0)
			flag = MF_METASUMM;
		if (tagLen== 8&&strncasecmp(tag,"keywords"   , 8)== 0)
			flag = MF_METAKEYW;
		if (tagLen==11&&strncasecmp(tag,"description",11)== 0)
			flag = MF_METADESC;
		if ( ! flag ) continue;
		// get the content
		int32_t len;
		char *s = bodyXml->getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// wordify
		if ( !addMatches( s, len, flag, niceness ) ) {
			return false;
		}
	}

	// . now the link text
	// . loop through each link text and it its matches

	// loop through the Inlinks
	Inlink *k = NULL;
	for ( ; (k = linkInfo->getNextInlink(k)) ; ) {
		// does it have link text? skip if not.
		if ( k->size_linkText <= 1 ) {
			continue;
		}

		// set the flag, the type of match
		mf_t flags = MF_LINK;

		// add it in
		if ( !addMatches( k->getLinkText(), k->size_linkText - 1, flags, niceness ) ) {
			return false;
		}

		// set flag for that
		flags = MF_HOOD;

		// add it in
		if ( !addMatches( k->getSurroundingText(), k->size_surroundingText - 1, flags, niceness ) ) {
			return false;
		}

		// parse the rss up into xml
		Xml rxml;
		if ( ! k->setXmlFromRSS ( &rxml , niceness ) ) {
			return false;
		}

		// add rss description
		bool isHtmlEncoded;
		int32_t rdlen;
		char *rd = rxml.getRSSDescription( &rdlen, &isHtmlEncoded );
		if ( !addMatches( rd, rdlen, MF_RSSDESC, niceness ) ) {
			return false;
		}

		// add rss title
		int32_t rtlen;
		char *rt = rxml.getRSSTitle( &rtlen, &isHtmlEncoded );
		if ( !addMatches( rt, rtlen, MF_RSSTITLE, niceness ) ) {
			return false;
		}
	}

	// that should be it
	return true;
}

bool Matches::addMatches( char *s, int32_t slen, mf_t flags, int32_t niceness ) {
	// . do not breach
	// . happens a lot with a lot of link info text
	if ( m_numMatchGroups >= MAX_MATCHGROUPS ) {
		return true;
	}

	// get some new ptrs for this match group
	Words    *wp = &m_wordsArray    [ m_numMatchGroups ];
	Sections *sp = NULL;
	Bits     *bp = &m_bitsArray     [ m_numMatchGroups ];
	Pos      *pb = &m_posArray      [ m_numMatchGroups ];

	// set the words class for this match group
	if ( !wp->set( s, slen, true, niceness ) ) {
		return false;
	}

	// bits vector
	if ( ! bp->setForSummary ( wp ) ) {
		return false;
	}

	// position vector
	if ( ! pb->set ( wp ) ) {
		return false;
	}

	// record the start
	int32_t startNumMatches = m_numMatches;
	// sometimes it returns true w/o incrementing this
	int32_t n = m_numMatchGroups;
	// . add all the Match classes from this match group
	// . this increments m_numMatchGroups on success
	bool status = addMatches( wp, NULL, sp, bp, pb, flags );

	// if this matchgroup had some, matches, then keep it
	if ( m_numMatches > startNumMatches ) {
		return status;
	}

	// otherwise, reset it, useless
	wp->reset();
	if ( sp ) sp->reset();
	bp->reset();
	pb->reset();

	// do not decrement the counter if we never incremented it
	if ( n == m_numMatchGroups ) {
		return status;
	}

	// ok, remove it
	m_numMatchGroups--;

	return status;
}

// . TODO: support stemming later. each word should then have multiple ids.
// . add to our m_matches[] array iff addToMatches is true, otherwise we just
//   set the m_foundTermVector for doing the BIG HACK described in Summary.cpp
bool Matches::addMatches(Words *words, Phrases *phrases, Sections *sections, Bits *bits, Pos *pos, mf_t flags ) {
	// if no query term, bail.
	if ( m_numSlots <= 0 ) {
		return true;
	}

	// . do not breach
	// . happens a lot with a lot of link info text
	if ( m_numMatchGroups >= MAX_MATCHGROUPS ) {
		return true;
	}

	// shortcut
	Section *sp = NULL;
	if ( sections ) {
		sp = sections->m_sections;
	}

	mf_t eflag = 0;

	m_numMatchGroups++;

	const int64_t *pids = NULL;
	if ( phrases ) {
		pids = phrases->getPhraseIds2();
	}

	// set convenience vars
	uint32_t mask = m_numSlots - 1;
	const int64_t *wids = words->getWordIds();
	const int32_t *wlens = words->getWordLens();
	const char * const *wptrs = words->getWords();
	nodeid_t *tids = words->getTagIds();
	int32_t nw = words->getNumWords();
	int32_t n;
	int32_t matchStack = 0;
	int64_t nextMatchWordIdMustBeThis = 0;
	int32_t nextMatchWordPos = 0;
	int32_t lasti = -3;

	if ( getNumXmlNodes() > 512 ) { g_process.shutdownAbort(true); }

	int32_t badFlags =SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_TITLE;

	int32_t qwn;
	int32_t numQWords;
	int32_t numWords;

	//
	// . set m_matches[] array
	// . loop over all words in the document
	//
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		//if      (tids && (tids[i]            ) == TAG_A) 
		//	inAnchTag = true;
		//else if (tids && (tids[i]&BACKBITCOMP) == TAG_A) 
		//	inAnchTag = false;

		if ( tids && tids[i] ){
			// tagIds don't have wids and are skipped
			continue;
		}

		// skip if wid is 0, it is not an alnum word then
		if ( ! wids[i] ) {
			continue;
		}

		// count the number of alnum words
		m_numAlnums++;

		// clear this
		eflag = 0;

		// NO NO, a score of -1 means in a select tag, and
		// we do index that!! so only skip if wscores is 0 now.
		// -1 means in script, style, select or marquee. it is
		// indexed but with very little weight... this is really
		// a hack in Scores.cpp and should be fixed.
		// in Scores.cpp we set even the select tag stuff to -1...
		//if ( wscores && wscores[i] == -1 ) continue;
		if ( sp && (sp->m_flags & badFlags) ) continue;


		// . does it match a query term?
		// . hash to the slot in the hash table
		n = ((uint32_t)wids[i]) & mask;
		//n2 = swids[i]?((uint32_t)swids[i]) & mask:n;
	chain1:
		// skip if slot is empty (doesn't match query term)
		//if ( ! m_qtableIds[n] && ! m_qtableIds[n2]) continue;
		if ( ! m_qtableIds[n] ) goto tryPhrase;
		// otherwise chain
		if ( (m_qtableIds[n] != wids[i]) ) {
			if ( m_qtableIds[n] && ++n >= m_numSlots ) n = 0;
			goto chain1;
		}
		// we got one!
		goto gotMatch;


		//
		// fix so we hihglight "woman's" when query term is "woman"
		// for 'spiritual books for women' query
		// 
	tryPhrase:
		// try without 's if it had it
		if ( wlens[i] >= 3 && 
		     wptrs[i][wlens[i]-2] == '\'' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 's' ) {
			// move 's from word hash... very tricky
			int64_t nwid = wids[i];
			// undo hash64Lower_utf8 in hash.h
			nwid ^= g_hashtab[wlens[i]-1][(uint8_t)'s'];
			nwid ^= g_hashtab[wlens[i]-2][(uint8_t)'\''];
			n = ((uint32_t)nwid) & mask;
		chain2:
			if ( ! m_qtableIds[n] ) goto tryPhrase2;
			if ( (m_qtableIds[n] != nwid) ) {
				if ( m_qtableIds[n] && ++n >= m_numSlots ) n=0;
				goto chain2;
			}
			qwn = m_qtableWordNums[n];
			numWords = 1;
			numQWords = 1;
			// we got one!
			goto gotMatch2;
		}

	tryPhrase2:
		// try phrase first
		if ( pids && pids[i] ) {
			n = ((uint32_t)pids[i]) & mask;
		chain3:
			if ( ! m_qtableIds[n] ) continue;
			if ( (m_qtableIds[n] != pids[i]) ) {
				if ( m_qtableIds[n] && ++n >= m_numSlots)n = 0;
				goto chain3;
			}
			// what query word # do we match?
			qwn = m_qtableWordNums[n];
			// get that query word #
			QueryWord *qw = &m_q->m_qwords[qwn];
			// . do we match it as a single word?
			// . did they search for "bluetribe" ...?
			if ( qw->m_rawWordId == pids[i] ) {
				// set our # of words basically to 3
				numWords = 3;
				// matching a single query word
				numQWords = 1;
				// got a match
				goto gotMatch2;
			}
			if ( qw->m_phraseId == pids[i] ) {
				// might match more if we had more query
				// terms in the quote
				numWords = getNumWordsInMatch( words, i, n, &numQWords, &qwn, true );

				// this is 0 if we were an unmatched quote
				if ( numWords <= 0 ) continue;

				// got a match
				goto gotMatch2;
			}
			// otherwise we are matching a query phrase id
			log("matches: wtf? query word not matched for "
			    "highlighting... strange.");
			// assume one word for now
			numWords = 1;
			numQWords = 1;
			goto gotMatch2;
		}

		//
		// shucks, no match
		//
		continue;

	gotMatch:
		// what query word # do we match?
		qwn = m_qtableWordNums[n];
		

		// . how many words are in this match?
		// . it may match a single word or a phrase or both
		// . this will be 1 for just matching a single word, and 
		//   multiple words for quotes/phrases. The number of words
		//   in both cases will included unmatched punctuation words
		//   and tags in between matching words.
		numQWords = 0;
		numWords = getNumWordsInMatch( words, i, n, &numQWords, &qwn, true );
		// this is 0 if we were an unmatched quote
		if ( numWords <= 0 ) continue;

	gotMatch2:
		// get query word
		QueryWord *qw = &m_q->m_qwords[qwn];
		// point to next word in the query
		QueryWord *nq = NULL;
		if ( qwn+2 < m_q->m_numWords ) nq = &m_q->m_qwords[qwn+2];

		// . if only one word matches and its a stop word, make sure
		//   it's next to the correct words in the query
		// . if phraseId is 0, that means we do not start a phrase,
		//   because stop words can start phrases if they are the
		//   first word, are capitalized, or have breaking punct before
		//   them.
		if ( numWords == 1 && 
		     ! qw->m_inQuotes &&
		     m_q->m_numWords > 2 &&
		     qw->m_wordSign == '\0' &&
		     (nq && nq->m_wordId) && // no field names can follow
		     //(qw->m_isQueryStopWord || qw->m_isStopWord ) ) {
		     // we no longer consider single alnum chars to be
		     // query stop words as stated in StopWords.cpp to fix
		     // the query 'j. w. eagan'
		     qw->m_isQueryStopWord ) {
			// if stop word does not start a phrase in the query 
			// then he must have a matched word before him in the
			// document. if he doesn't then do not count as a match
			if ( qw->m_phraseId == 0LL && i-2 != lasti ) {
				// peel off anybody before us
				m_numMatches -= matchStack;
				if ( m_numMatches < 0 ) m_numMatches = 0;
				// don't forget to reset the match stack
				matchStack = 0;	

				continue; 
			}
			// if we already have a match stack, we must
			// be in nextMatchWordPos
			if ( matchStack && nextMatchWordPos != i ) {
				// peel off anybody before us 
				m_numMatches -= matchStack;
				if ( m_numMatches < 0 ) m_numMatches = 0;
				// don't forget to reset the match stack
				matchStack = 0;	
				//continue; 
			}
			// if the phraseId is 0 and the previous word
			// is a match, then we're ok, but put us on a stack
			// so if we lose a match, we'll be erased
			QueryWord *nq = &m_q->m_qwords[qwn+2];
			// next match is only required if next word in query
			// is indeed valid.
			if ( nq->m_wordId && nq->m_fieldCode == 0 ) {
				nextMatchWordIdMustBeThis = nq->m_rawWordId;
				nextMatchWordPos          = i + 2;
				matchStack++;
			}
		}
		else if ( matchStack ) {
			// if the last word matched was a stop word, we have to
			// match otherwise we have to remove the whole stack.
			if ( qw->m_rawWordId != nextMatchWordIdMustBeThis ||
			     i                > nextMatchWordPos ) {
				m_numMatches -= matchStack;
				// ensure we never go negative like for 
				// www.experian.com query
				if ( m_numMatches < 0 ) m_numMatches = 0;
			}
			// always reset this here if we're not a stop word
			matchStack = 0;
		}

		// record word # of last match
		lasti = i;

		// otherwise, store it in our m_matches[] array
		Match *m = &m_matches[m_numMatches];

		// the word # in the doc, and how many of 'em are in the match
		m->m_wordNum  = i;
		m->m_numWords = numWords;

		// the word # in the query, and how many of 'em we match
		m->m_qwordNum  = qwn;
		m->m_numQWords = numQWords;

		// get the first query word # of this match
		qw = &m_q->m_qwords[qwn];

		// convenience, used by Summary.cpp
		m->m_words    = words;
		m->m_sections = sections;
		m->m_bits     = bits;
		m->m_pos      = pos;
		m->m_flags    = flags | eflag ;

		// add to our vector. we want to know where each QueryWord
		// is. i.e. in the title, link text, meta tag, etc. so
		// the proximity algo in Summary.cpp can use that info.
		m_qwordFlags[qwn] |= flags;

		// advance
		m_numMatches++;

		// we get atleast MAX_MATCHES
		if ( m_numMatches < MAX_MATCHES ) {
			continue;
		}

		break;
	}

	// peel off anybody before us
	m_numMatches -= matchStack;
	if ( m_numMatches < 0 ) m_numMatches = 0;

	return true;
}

// . word #i in the doc matches slot #n in the hash table
int32_t Matches::getNumWordsInMatch(Words *words, int32_t wn, int32_t n, int32_t *numQWords, int32_t *qwn,
				    bool allowPunctInPhrase) {
	// is it a two-word synonym?
	if ( m_qtableFlags[n] & 0x08 ) {
		// get the word following this
		int64_t wid2 = 0LL;
		if ( wn+2 < words->getNumWords() )
			wid2 = words->getWordId(wn+2);
		// scan the synonyms...
		const int64_t *wids = words->getWordIds();
		for ( int32_t k = 0 ; k < m_q->m_numTerms ; k++ ) {
			QueryTerm *qt = &m_q->m_qterms[k];
			if ( ! qt->m_synonymOf ) continue;
			if ( qt->m_synWids0 != wids[wn] ) continue;
			if ( qt->m_synWids1 != wid2 ) continue;
			*numQWords = 3; 
			return 3;
		}
	}

	// save the first word in the doc that we match first
	int32_t wn0 = wn;

	// CAUTION: the query "business development center" (in quotes)
	// would match a doc with "business development" and 
	// "development center" as two separate phrases.

	// if query word never appears in quotes, it's a single word match
	if ( ! (m_qtableFlags[n] & 0x02) ) { *numQWords = 1; return 1; }

	// get word ids array for the doc
	int64_t  *wids   = words->getWordIds();
	//int64_t  *swids  = words->getStripWordIds();
	char      **ws     = words->getWords();
	int32_t       *wl     = words->getWordLens();
	//the word we match in the query appears in quotes in the query
	int32_t k     = -1;
	int32_t count = 0;
	int32_t nw    = words->getNumWords();

	// loop through all the quotes in the query and find
	// which one we match, if any. we will have to advance the
	// query word and doc word simultaneously and make sure they
	// match as we advance. 
	int32_t nqw = m_q->m_numWords;
	int32_t j;
	for ( j = 0 ; j < nqw ; j++ ) {
		// get ith query word
		QueryWord *qw = &m_q->m_qwords[j];
		if ( !qw->m_rawWordId ) continue;
		// query word must match wid of first word in quote
		if ( (qw->m_rawWordId != wids[wn]) ) continue;
		//     (qw->m_rawWordId != swids[wn])) continue;
		// skip if in field
		// . we were doing an intitle:"fight club" query and
		//   needed to match that in the title...
		//if ( qw->m_fieldCode         ) continue;
		// query word must be in quotes
		if ( ! qw->m_inQuotes        ) continue;
		// skip it if it does NOT start the quote. quoteStart
		// is actually the query word # that contains the quote
		//if ( qw->m_quoteStart != j-1 ) continue;
		// not any more it isn't...
		if ( qw->m_quoteStart != j ) continue;
		// save the first word # in the query of the quote
		k = j; // -1;
		// count number of words we match in the quote, we've
		// already matched the first one
		count = 0;
	subloop:
		// query word must match wid of first word in phrase
		if ( (qw->m_rawWordId != wids[wn]) ) {
		//     (qw->m_rawWordId != swids[wn])) {
			// reset and try another quote in the query
			count = 0;
			wn    = wn0;
			continue;
		}
		// up the count of query words matched in the quote
		count++;
		// ADVANCE QUERY WORD
		j++;
		// if no more, we got a match
		if ( j >= nqw ) break;
		// skip punct words
		if ( m_q->m_qwords[j].m_isPunct ) j++;
		// if no more, we got a match
		if ( j >= nqw ) break;
		// now we should point to the next query word in quote
		qw = &m_q->m_qwords[j];
		// if not in quotes, we're done, we got a match
		if ( ! qw->m_inQuotes      ) break;
		// or if in a different set of quotes, we got a match
		if ( qw->m_quoteStart != k ) break;
		// . ADVANCE DOCUMENT WORD
		// . tags and punctuation words have 0 for their wid
		for ( wn++ ; wn < nw ; wn++ ) {
			// . if NO PUNCT, IN QUOTES, AND word id is zero
			//   then check for punctuation
			if(!allowPunctInPhrase && qw->m_inQuotes && !wids[wn]) {
				// . check if its a space [0x20, 0x00]
				if( (wl[wn] == 2) && (ws[wn][0] == ' ') ) 
					continue;
				// . if the length is greater than a space
				else if( wl[wn] > 2 ) {
					// . increment until we find no space
					// . increment by 2 since its utf16
					for( int32_t i = 0; i < wl[wn]; i+=2 )
						// . if its not a space, its punc
						if( ws[wn][i] != ' ' ) {
							count=0; break;
						}
					// . if count is 0, punc found break
					if( count == 0 ) break;
				}
				// . otherwise its solo punc, set count and break
				else { count=0; break; }
			}
			// . we incremented to a new word break and check
			if ( wids[wn] ) break;
		}
		// there was a following query word in the quote
		// so there must be a following word, if not, continue
		// to try to find another quote in the query we match
		if ( wn >= nw ) { 
			// reset and try another quote in the query
			count = 0; 
			wn    = wn0;
			continue;
		}
		// see if the next word and query term match
		goto subloop;
	}

	// if we did not match any quote in the query
	// check if we did match a single word. e.g.
	// Hello World "HelloWorld" "Hello World Example"
	if ( count <= 0 ) {
		if ( m_qtableFlags[n] & 0x01 ) {
			*numQWords = 1;
			// we did match a single word. m_qtableWordNums[n] may
			// not be pointing to the right qword. Set it to a 
			// qword that is the single word
			for ( j = 0 ; j < nqw ; j++ ) {
				// get ith query word
				QueryWord *qw = &m_q->m_qwords[j];
				if ( !qw->m_rawWordId ) continue;
				// query word must match wid of word
				if ( (qw->m_rawWordId != wids[wn]) ) continue;
				//   (qw->m_rawWordId != swids[wn])) continue;
				// skip if in field
				// . fix intitle:"fight club"
				//if ( qw->m_fieldCode         ) continue;
				// query word must NOT be in quotes
				if ( qw->m_inQuotes        ) continue;
				*qwn = j;
			}
			return 1;
		}
		else
			return 0;
	}
	// sanity check
	if ( k < 0 ) { g_process.shutdownAbort(true); }
	// skip punct words
	if ( j-1>=0 && m_q->m_qwords[j-1].m_isPunct ) j--;
	// . ok, we got a quote match
	// . it had this man query words in it
	//*numQWords = j - (k+1);
	*numQWords = j - k;
	// fix the start word
	*qwn = k ;
	if (m_q->m_qwords[k].m_isPunct) *qwn = k+1;

	return wn - wn0 + 1;
}

