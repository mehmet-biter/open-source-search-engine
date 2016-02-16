#include "gb-include.h"

#include "Highlight.h"
#include "Titledb.h" // TITLEREC_CURRENT_VERSION
#include "Phrases.h"
#include "Synonyms.h"
#include "XmlDoc.h"


// use different front tags for matching different term #'s
static char *s_frontTags[] = {
	"<span class=\"gbcnst gbcnst00\">" ,
	"<span class=\"gbcnst gbcnst01\">" ,
	"<span class=\"gbcnst gbcnst02\">" ,
	"<span class=\"gbcnst gbcnst03\">" ,
	"<span class=\"gbcnst gbcnst04\">" ,
	"<span class=\"gbcnst gbcnst05\">" ,
	"<span class=\"gbcnst gbcnst06\">" ,
	"<span class=\"gbcnst gbcnst07\">" ,
	"<span class=\"gbcnst gbcnst08\">" ,
	"<span class=\"gbcnst gbcnst09\">" 
};

int32_t s_frontTagLen=gbstrlen("<span class=\"gbcnst gbcnst00\">");

static char *s_styleSheet =
"<style type=\"text/css\">"
"span.gbcns{font-weight:600}"
"span.gbcnst00{color:black;background-color:#ffff66}"
"span.gbcnst01{color:black;background-color:#a0ffff}"
"span.gbcnst02{color:black;background-color:#99ff99}"
"span.gbcnst03{color:black;background-color:#ff9999}"
"span.gbcnst04{color:black;background-color:#ff66ff}"
"span.gbcnst05{color:white;background-color:#880000}"
"span.gbcnst06{color:white;background-color:#00aa00}"
"span.gbcnst07{color:white;background-color:#886800}"
"span.gbcnst08{color:white;background-color:#004699}"
"span.gbcnst09{color:white;background-color:#990099}"
"span.gbcnst00x{color:white;background-color:black;border:2px solid #ffff66}"
"span.gbcnst01x{color:white;background-color:black;border:2px solid #a0ffff}"
"span.gbcnst02x{color:white;background-color:black;border:2px solid #99ff99}"
"span.gbcnst03x{color:white;background-color:black;border:2px solid #ff9999}"
"span.gbcnst04x{color:white;background-color:black;border:2px solid #ff66ff}"
"span.gbcnst05x{color:white;background-color:black;border:2px solid #880000}"
"span.gbcnst06x{color:white;background-color:black;border:2px solid #00aa00}"
"span.gbcnst07x{color:white;background-color:black;border:2px solid #886800}"
"span.gbcnst08x{color:white;background-color:black;border:2px solid #004699}"
"span.gbcnst09x{color:white;background-color:black;border:2px solid #990099}"
"</style>";
int32_t s_styleSheetLen = gbstrlen( s_styleSheet );

//buffer for writing term list items
char s_termList[1024];

// . return length stored into "buf"
// . content must be NULL terminated
// . if "useAnchors" is true we do click and scroll
// . if "isQueryTerms" is true, we do typical anchors in a special way
int32_t Highlight::set( SafeBuf *sb, char *content, int32_t contentLen, Query *q, const char *frontTag,
						const char *backTag, int32_t niceness ) {
	Words words;
	if ( ! words.set ( content, contentLen, true, true ) ) {
		return -1;
	}

	int32_t version = TITLEREC_CURRENT_VERSION;

	Bits bits;
	if ( !bits.set( &words, version, niceness ) ) {
		return -1;
	}

	Phrases phrases;
	if ( !phrases.set( &words, &bits, true, false, version, niceness ) ) {
		return -1;
	}

	Matches matches;
	matches.setQuery ( q );

	if ( !matches.addMatches( &words, &phrases ) ) {
		return -1;
	}

	// store
	m_numMatches = matches.getNumMatches();

	return set ( sb, &words, &matches, frontTag, backTag, q);
}

// New version
int32_t Highlight::set( SafeBuf *sb, Words *words, Matches *matches, const char *frontTag,
						const char *backTag, Query *q ) {
	// save stuff
	m_frontTag    = frontTag;
	m_backTag     = backTag;

	// set lengths of provided front/back highlight tags
	if ( m_frontTag ) {
		m_frontTagLen = gbstrlen ( frontTag );
	}
	if ( m_backTag  ) {
		m_backTagLen  = gbstrlen ( backTag  );
	}

	m_sb = sb;

	// label it
	m_sb->setLabel ("highw");

	if ( ! highlightWords ( words, matches, q ) ) {
		return -1;
	}

	// null terminate
	m_sb->nullTerm();

	// return the length
	return m_sb->length();
}

bool Highlight::highlightWords ( Words *words , Matches *m, Query *q ) {
	// get num of words
	int32_t numWords = words->getNumWords();
	// some convenience ptrs to word info
	char *w;
	int32_t  wlen;

	// length of our front tag should be constant
	int32_t frontTagLen ;
	if ( m_frontTag ) frontTagLen = m_frontTagLen;
	else              frontTagLen = s_frontTagLen;
	// set the back tag, should be constant
	const char *backTag ;
	int32_t  backTagLen;
	if ( m_backTag ) {
		backTag    = m_backTag;
		backTagLen = m_backTagLen;
	}
	else {
		backTag = "</span>";
		backTagLen = 7;
	}

	// set nexti to the word # of the first word that matches a query word
	int32_t nextm = -1;
	int32_t nexti = -1;
	if ( m->m_numMatches > 0 ) {
		nextm = 0;
		nexti = m->m_matches[0].m_wordNum;
	}

	int32_t backTagi = -1;
	bool inTitle  = false;
	bool endHead  = false;
	bool endHtml  = false;

	for ( int32_t i = 0 ; i < numWords  ; i++ ) {
		// set word's info
		w    = words->getWord(i);
		wlen = words->getWordLen(i);
		endHead = false;
		endHtml = false;

		if ( (words->getTagId(i) ) == TAG_TITLE ) { //<TITLE>
			inTitle = !(words->isBackTag(i));
		} else if ( (words->getTagId(i) ) == TAG_HTML ) { //<HTML>
			if ( words->isBackTag( i ) ) {
				endHtml = true;
			}
		} else if ( (words->getTagId(i) ) == TAG_HEAD ) { //<HEAD>
			if (words->isBackTag(i) ) {
				endHead = true;
			}
		}

		// match class ptr
		Match *mat = NULL;
		// This word is a match...see if we're gonna tag it
		// dont put the same tags around consecutive matches
		if ( i == nexti && ! inTitle && ! endHead && ! endHtml) {
			// get the match class for the match
			mat = &m->m_matches[nextm];
			// discontinue any current font tag we are in
			if ( i < backTagi ) {
				// push backtag ahead if needed
				if ( i + mat->m_numWords > backTagi ) {
					backTagi = i + mat->m_numWords;
				}
			}
			else {
				// now each match is the entire quote, so write the
				// fron tag right now
				const char *frontTag;
				if ( m_frontTag ) {
					frontTag = m_frontTag;
				} else {
					frontTag = s_frontTags[mat->m_colorNum%10];
				}

				m_sb->safeStrcpy ( (char *)frontTag );
				
				// when to write the back tag? add the number of
				// words in the match to i.
				backTagi = i + mat->m_numWords;
			}
		}
		else if ( endHead ) {
			// include the tags style sheet immediately before
			// the closing </TITLE> tag
			m_sb->safeMemcpy( s_styleSheet , s_styleSheetLen );
		}

		if ( i == nexti ) {
			// advance match
			nextm++;
			// set nexti to the word # of the next match
			if ( nextm < m->m_numMatches ) 
				nexti = m->m_matches[nextm].m_wordNum;
			else
				nexti = -1;
		}

		m_sb->safeMemcpy ( w , wlen );

		// back tag
		if ( i == backTagi-1 ) {
			// store the back tag
			m_sb->safeMemcpy ( (char *)backTag , backTagLen );
		}
	}

	return true;
}
