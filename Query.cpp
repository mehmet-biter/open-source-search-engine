#include "gb-include.h"

#include "Query.h"
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "Url.h"
#include "Clusterdb.h" // g_clusterdb.getNumGlobalRecs()
#include "StopWords.h" // isQueryStopWord()
#include "Sections.h"
#include "Msg1.h"
#include "Speller.h"
#include "Mem.h"
#include "Msg3a.h"
#include "HashTableX.h"
#include "Synonyms.h"
#include "HighFrequencyTermShortcuts.h"
#include "Wiki.h"
#include "RdbList.h"
#include "Process.h"
#include "termid_mask.h"


Query::Query()
  : m_filteredQuery("qrystk"),
    m_originalQuery("oqbuf")
{
	constructor();
}

void Query::constructor ( ) {
	m_qwords      = NULL;
	m_numWords = 0;
	m_qwordsAllocSize      = 0;
	m_qwords               = NULL;
	m_numTerms = 0;

	// Coverity
	m_requiredBits = 0;
	m_matchRequiredBits = 0;
	m_negativeBits = 0;
	m_forcedBits = 0;
	m_synonymBits = 0;
	m_numRequired = 0;
	m_langId = 0;
	m_useQueryStopWords = false;
	m_numTermsUntruncated = 0;
	m_isBoolean = false;
	m_maxQueryTerms = 0;
	m_queryExpansion = false;

	memset(m_expressions, 0, sizeof(m_expressions));
	memset(m_gbuf, 0, sizeof(m_gbuf));

	reset ( );
}

void Query::destructor ( ) {
	reset();
}

Query::~Query ( ) {
	reset ( );
}

void Query::reset ( ) {

	// if Query::constructor() was called explicitly then we have to
	// call destructors explicitly as well...
	// essentially call QueryTerm::reset() on each query term
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		qw->destructor();
	}

	m_queryTermBuf.purge();
	m_qterms = NULL;

	m_filteredQuery.purge();
	m_originalQuery.purge();
	m_docIdRestriction = 0LL;
	m_numWords    = 0;
	m_numTerms    = 0;

	if ( m_qwordsAllocSize )
		mfree ( m_qwords      , m_qwordsAllocSize      , "Query4" );
	m_qwordsAllocSize      = 0;
	m_qwords               = NULL;
	m_numExpressions       = 0;
	m_gnext                = m_gbuf;
	m_hasUOR               = false;
	// the site: and ip: query terms will disable site clustering & caching
	m_hasPositiveSiteField         = false;
	m_hasIpField           = false;
	m_hasUrlField          = false;
	m_hasSubUrlField       = false;
	m_hasQuotaField        = false;
	m_truncated            = false;
}

// . returns false and sets g_errno on error
// . "query" must be NULL terminated
// . if boolFlag is 0 we ignore all boolean operators
// . if boolFlag is 1  we assume query is boolen
// . if boolFlag is 2  we attempt to detect if query is boolean or not
// . if "keepAllSingles" is true we do not ignore any single word UNLESS
//   it is a boolean operator (IGNORE_BOOLOP), fieldname (IGNORE_FIELDNAME)
//   a punct word (IGNORE_DEFAULT) or part of one field value (IGNORE_DEFAULT)
//   This is used for term highlighting (Highlight.cpp and Summary.cpp)
bool Query::set2 ( const char *query        , 
		   // need language for doing synonyms
		   uint8_t  langId ,
		   bool     queryExpansion ,
		   bool     useQueryStopWords ,
		   int32_t  maxQueryTerms  ) {

	reset();

	m_langId = langId;
	m_useQueryStopWords = useQueryStopWords;
	// fix summary rerank and highlighting.
	bool keepAllSingles = true;

	m_maxQueryTerms = maxQueryTerms;

	// assume  boolean auto-detect.
	char boolFlag = 2;


	if ( ! query ) return true;

	m_queryExpansion = queryExpansion;

	int32_t queryLen = strlen(query);

	// truncate query if too big
	if ( queryLen >= ABS_MAX_QUERY_LEN ) {
		log("query: Query length of %" PRId32" must be "
		    "less than %" PRId32". "
		    "Truncating.",queryLen,(int32_t)ABS_MAX_QUERY_LEN);
		queryLen = ABS_MAX_QUERY_LEN - 1;
		m_truncated = true;
	}
	// save original query
	if( !m_originalQuery.reserve ( queryLen + 1 ) ) {
		logError("Failed to reserve %" PRId32 " bytes, bailing", queryLen+1);
		return true;
	}
	m_originalQuery.safeMemcpy(query, queryLen);
	m_originalQuery.nullTerm();
	
	log(LOG_DEBUG, "query: set called = %s", m_originalQuery.getBufStart());

	const char *q = query;
	// see if it should be boolean...
	for ( int32_t i = 0 ; i < queryLen ; i++ ) {
		// but if bool flag is 0 that means it is NOT boolean!
		// it must be one for autodetection. so do not autodetect
		// unless this is 2.
		if ( boolFlag != 2 ) break;
		if ( q[i]=='A' && q[i+1]=='N' && q[i+2]=='D' &&
		     (q[i+3]==' ' || q[i+3]=='(') )
			boolFlag = 1;
		if ( q[i]=='O' && q[i+1]=='R' && 
		     (q[i+2]==' ' || q[i+2]=='(') )
			boolFlag = 1;
		if ( q[i]=='N' && q[i+1]=='O' && q[i+2]=='T' &&
		     (q[i+3]==' ' || q[i+3]=='(') )
			boolFlag = 1;		
	}

	// if we did not set the flag to 1 set it to 0. force to non-bool
	if ( boolFlag == 2 ) boolFlag = 0;
	
	// reserve some space, guessing how much we'd need
	int32_t need = queryLen * 2 + 32;
	if ( ! m_filteredQuery.reserve ( need ) )
		return false;

	bool inQuotesFlag = false;
	// . copy query into m_buf
	// . translate ( and ) to special query operators so Words class
	//   can parse them as their own word to make parsing bool queries ez
	//   for parsing out the boolean operators in setBitScoresBoolean()
	for ( int32_t i = 0 ; i < queryLen ; i++ ) {

		// gotta count quotes! we ignore operators in quotes
		// so you can search for diffbotUri:"article|0|123456"
		if ( query[i] == '\"' ) inQuotesFlag = !inQuotesFlag;

		if ( inQuotesFlag ) {
			//*p = query [i];
			//p++;
			m_filteredQuery.pushChar(query[i]);
			continue;
		}

		// translate ( and )
		if ( boolFlag == 1 && query[i] == '(' ) {
			m_filteredQuery.safeMemcpy ( " LeFtP " , 7 );
			continue;
		}
		if ( boolFlag == 1 && query[i] == ')' ) {
			m_filteredQuery.safeMemcpy ( " RiGhP " , 7 );
			continue;
		}
		if ( query[i] == '|' ) {
			m_filteredQuery.safeMemcpy ( " PiiPE " , 7 );
			continue;
		}
		// translate [#a] [#r] [#ap] [#rp] [] [p] to operators
		if ( query[i] == '[' && is_digit(query[i+1])) {
			int32_t j = i+2;
			int32_t val = atol ( &query[i+1] );
			while ( is_digit(query[j]) ) j++;
			char c = query[j];
			if ( (c == 'a' || c == 'r') && query[j+1]==']' ) {
				m_filteredQuery.safePrintf(" LeFtB %" PRId32" %c RiGhB ",
					  val,c);
				i = j + 1;
				continue;
			}
			else if ( (c == 'a' || c == 'r') && 
				  query[j+1]=='p' && query[j+2]==']') {
				m_filteredQuery.safePrintf(" LeFtB %" PRId32" %cp RiGhB ",
				val,c);
				i = j + 2;
				continue;
			}
		}
		if ( query[i] == '[' && query[i+1] == ']' ) {
			m_filteredQuery.safePrintf ( " LeFtB RiGhB ");
			i = i + 1;
			continue;
		}
		if ( query[i] == '[' && query[i+1] == 'p' && query[i+2]==']') {
			m_filteredQuery.safePrintf ( " LeFtB RiGhB ");
			i = i + 2;
			continue;
		}
 
		// TODO: copy altavista's operators here? & | !
		// otherwise, just a plain copy
		m_filteredQuery.pushChar ( query[i] );
	}
	// NULL terminate
	m_filteredQuery.nullTerm();

	Words words;
	Phrases phrases;

	// set m_qwords[] array from m_buf
	if ( ! setQWords ( boolFlag , keepAllSingles , words , phrases ) ) 
		return false;

	// set m_qterms from m_qwords, always succeeds
	setQTerms ( words );

	// disable stuff for site:, ip: and url: queries
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord  ) continue;
		if      ( qw->m_fieldCode == FIELD_SITE &&
			  qw->m_wordSign != '-' ) 
			m_hasPositiveSiteField = true;
		else if ( qw->m_fieldCode == FIELD_IP ) 
			m_hasIpField   = true;
		else if ( qw->m_fieldCode == FIELD_URL )
			m_hasUrlField  = true;
		else if ( qw->m_fieldCode == FIELD_QUOTA )
			m_hasQuotaField = true;
		else if ( qw->m_fieldCode == FIELD_SUBURL )
			m_hasSubUrlField = true;
		else if ( qw->m_fieldCode == FIELD_SUBURL2 )
			m_hasSubUrlField = true;
	}

	// set m_docIdRestriction if a term is gbdocid:
	for ( int32_t i = 0 ; i < m_numTerms && ! m_isBoolean ; i++ ) {
		// get it
		QueryTerm *qt = &m_qterms[i];

		if( qt->m_fieldCode == FIELD_GBTERMID ) {
			const char *ds = m_qterms[i].m_term + 9; // strlen("gbtermid:")
			qt->m_termId = atoll(ds);
		}

		// gbdocid:?
		if ( qt->m_fieldCode != FIELD_GBDOCID ) continue;
		// get docid
		const char *ds = m_qterms[i].m_term + 8;
		m_docIdRestriction = atoll(ds);
		break;
	}

	// . keep it simple for now
	// . we limit to MAX_EXRESSIONS to like 10 now i guess
	if ( m_isBoolean ) {
		m_numExpressions = 1;
		if ( ! m_expressions[0].addExpression ( 0 , 
						      m_numWords ,
						      this , // Query
						      0 ) ) // level
			// return false with g_errno set on error
			return false;
	}


	// . if it is not truncated, no need to use hard counts
	// . comment this line and the next one out for testing hard counts
	if ( ! m_truncated ) return true;
	// if got truncated AND under the HARD max, nothing we can do, it
	// got cut off due to m_maxQueryTerms limit in Parms.cpp
	if ( m_numTerms < (int32_t)MAX_EXPLICIT_BITS ) return true;
	// if they just hit the admin's ceiling, there's nothing we can do
	if ( m_numTerms >= m_maxQueryTerms ) return true;
	// a temp log message
	log(LOG_DEBUG,"query: Encountered %" PRId32" query terms.",m_numTerms);

	// otherwise, we're below m_maxQueryTerms BUT above MAX_QUERY_TERMS
	// so we can use hard counts to get more power...

	// . use the hard count for excessive query terms to save explicit bits
	// . just look for operands on the first level that are not OR'ed
	char redo = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		// get the ith word
		QueryWord *qw = &m_qwords[i];
		// mark him as NOT hard required
		qw->m_hardCount = 0;
		// skip if not on first level
		if ( qw->m_level != 0 ) continue;
		// stop at first OR on this level
		if ( qw->m_opcode == OP_OR ) break;
		// skip all punct
		if (  qw->m_isPunct ) continue;
		// if we are a boolean query,the next operator can NOT be OP_OR
		// because we can not used terms that are involved in an OR
		// as a hard count term, because they are not required terms
		for ( int32_t j=i+1 ; m_isBoolean && j<m_numWords; j++ ) {
			// stop at previous operator
			char opcode = m_qwords[j].m_opcode;
			if ( ! opcode          ) continue;
			if (   opcode != OP_OR ) break;
			// otherwise, the next operator is an OR, so do not
			// use a hard count for this term
			goto stop;
		}
		// mark him as required, so he won't use an explicit bit now
		qw->m_hardCount = 1;
		// mark it so we can reduce our number of explicit bits used
		redo = 1;
	}

 stop:
	// if nothing changed, return now
	if ( ! redo ) return true;

	// . set the query terms again if we have a int32_t query
	// . if QueryWords has m_hardCount set, ensure the explicit bit is 0
	// . non-quoted phrases that contain a "required" single word should
	//   themselves have 0 for their implicit bits, BUT 0x8000 for their
	//   explicit bit
	if ( ! setQTerms ( words ) )
		return false;

	return true;
}

// returns false and sets g_errno on error
bool Query::setQTerms ( const Words &words ) {

	// . set m_qptrs/m_qtermIds/m_qbits
	// . use one bit position for each phraseId and wordId
	// . first set phrases
	int32_t n = 0;
	// what is the max value for "shift"?
	int32_t max = (int32_t)MAX_EXPLICIT_BITS;
	if ( max > m_maxQueryTerms ) max = m_maxQueryTerms;

	// count phrases first for allocating
	int32_t nqt = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw  = &m_qwords[i];
		// skip if ignored... mdw...
		if ( ! qw->m_phraseId ) continue;
		if (   qw->m_ignorePhrase ) continue; // could be a repeat
		// none if weight is absolute zero
		if ( almostEqualFloat(qw->m_userWeightPhrase, 0) && 
		     qw->m_userTypePhrase   == 'a'  ) continue;
		nqt++;
	}
	// count single terms
	for ( int32_t i = 0 ; i < m_numWords; i++ ) {
		QueryWord *qw  = &m_qwords[i];
 		if ( qw->m_ignoreWord && 
 		     qw->m_ignoreWord != IGNORE_QSTOP) continue;
		// ignore if in quotes and part of phrase, watch out
		// for things like "word", a single word in quotes.
		if ( qw->m_quoteStart >= 0 && qw->m_phraseId ) continue;
		// if we are not start of quote and NOT in a phrase we
		// must be the tailing word i guess.
		// fixes '"john smith" -"bob dole"' from having
		// smith and dole as query terms.
		if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != i )
			continue;
		// ignore if weight is absolute zero
		if ( qw->m_userWeight == 0   && 
		     qw->m_userType   == 'a'  ) continue;
		nqt++;
	}
	// thirdly, count synonyms
	Synonyms syn;
	if ( m_queryExpansion ) {
		int64_t to = hash64n("to");
		for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
			// get query word
			QueryWord *qw  = &m_qwords[i];
			// skip if in quotes, we will not get synonyms for it
			if ( qw->m_inQuotes ) continue;
			// skip if has plus sign in front
			if ( qw->m_wordSign == '+' ) continue;
			// not '-' either i guess
			if ( qw->m_wordSign == '-' ) continue;
			// no url: stuff, maybe only title
			if ( qw->m_fieldCode &&
			qw->m_fieldCode != FIELD_TITLE &&
			qw->m_fieldCode != FIELD_GENERIC )
				continue;
			// ignore title: etc. words, they are field names
			if ( qw->m_ignoreWord == IGNORE_FIELDNAME ) continue;
			// ignore boolean operators
			if ( qw->m_ignoreWord ) continue;// IGNORE_BOOLOP
			// no, hurts 'Greencastle IN economic development'
			if ( qw->m_wordId == to ) continue;
			// single letters...
			if ( qw->m_wordLen == 1 ) continue;
			// set the synonyms for this word
			char tmpBuf [ TMPSYNBUFSIZE ];
			int32_t naids = syn.getSynonyms ( &words ,
							  i ,
							  // language of the query.
							  // 0 means unknown. if this
							  // is 0 we sample synonyms
							  // from all languages.
							  m_langId ,
							  tmpBuf );
			// if no synonyms, all done
			if ( naids <= 0 ) continue;
			nqt += naids;
		}
	}

	m_numTermsUntruncated = nqt;

	if ( nqt > m_maxQueryTerms ) nqt = m_maxQueryTerms;

	// allocate the stack buf
	if ( nqt ) {
		int32_t need = nqt * sizeof(QueryTerm) ;
		if ( ! m_queryTermBuf.reserve ( need ) )
			return false;
		m_queryTermBuf.setLabel("stkbuf3");
		const char *pp = m_queryTermBuf.getBufStart();
		m_qterms = (QueryTerm *)pp;
		pp += sizeof(QueryTerm);
		if ( pp > m_queryTermBuf.getBufEnd() ) { g_process.shutdownAbort(true); }
	}

	// call constructor on each one here
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		QueryTerm *qt = &m_qterms[i];
		qt->constructor();
	}


	// count phrase terms
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw  = &m_qwords[i];
		// skip if ignored... mdw...
		if ( ! qw->m_phraseId ) continue;
		if (   qw->m_ignorePhrase ) continue; // could be a repeat
		// none if weight is absolute zero
		if ( almostEqualFloat(qw->m_userWeightPhrase, 0) && 
		     qw->m_userTypePhrase   == 'a'  ) continue;

		// stop breach
		if ( n >= ABS_MAX_QUERY_TERMS ) {
			log("query: lost query phrase terms to max term "
			    "limit of %" PRId32,(int32_t)ABS_MAX_QUERY_TERMS );
			break;
		}
		if ( n >= m_maxQueryTerms ) {
			log("query: lost query phrase terms to max term cr "
			    "limit of %" PRId32,(int32_t)m_maxQueryTerms);
			break;
		}

		if(n>=nqt)
			break;

		QueryTerm *qt = &m_qterms[n];
		qt->m_qword     = qw ;
		qt->m_piped     = qw->m_piped;
		qt->m_isPhrase  = true ;
		qt->m_isUORed   = false;
		qt->m_UORedTerm   = NULL;
		qt->m_synonymOf = NULL;
		qt->m_ignored   = false;
		qt->m_term      = NULL;
		qt->m_termLen   = 0;
		qt->m_langIdBitsValid = false;
		qt->m_langIdBits      = 0;
		// stop word? no, we're a phrase term
		qt->m_isQueryStopWord = false;
		// change in both places
		qt->m_termId    = qw->m_phraseId & TERMID_MASK;
		qt->m_rawTermId = qw->m_rawPhraseId;
		// assume explicit bit is 0
		qt->m_explicitBit = 0;
		qt->m_matchesExplicitBits = 0;
		// boolean queries are not allowed term signs for phrases
		// UNLESS it is a '*' soft require sign which we need for
		// phrases like: "cat dog" AND pig
		if ( m_isBoolean && qw->m_phraseSign != '*' ) {
			qt->m_termSign = '\0';
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_phraseSign;
		}

		// do not use an explicit bit up if we have a hard count
		qt->m_hardCount = qw->m_hardCount;
		qw->m_queryWordTerm = NULL;
		// IndexTable.cpp uses this one
		qt->m_inQuotes  = qw->m_inQuotes;
		// point to the string itself that is the phrase
		qt->m_term      = qw->m_word;
		qt->m_termLen   = qw->m_phraseLen;

		// the QueryWord should have a direct link to the QueryTerm,
		// at least for phrase, so we can OR in the bits of its
		// constituents in the for loop below
		qw->m_queryPhraseTerm = qt ;
		// doh! gotta reset to 0
		qt->m_implicitBits = 0;
		// assign score weight, we're a phrase here
		qt->m_userWeight = qw->m_userWeightPhrase ;
		qt->m_userType   = qw->m_userTypePhrase   ;
		qt->m_fieldCode  = qw->m_fieldCode  ;
		// stuff before a pipe always has a weight of 1
		if ( qt->m_piped ) {
			qt->m_userWeight = 1;
			qt->m_userType   = 'a';
		}
		n++;
	}

	// now if we have enough room, do the singles
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw  = &m_qwords[i];

 		if ( qw->m_ignoreWord && 
 		     qw->m_ignoreWord != IGNORE_QSTOP) continue;

		// ignore if in quotes and part of phrase, watch out
		// for things like "word", a single word in quotes.
		if ( qw->m_quoteStart >= 0 && qw->m_phraseId ) continue;

		// if we are not start of quote and NOT in a phrase we
		// must be the tailing word i guess.
		// fixes '"john smith" -"bob dole"' from having
		// smith and dole as query terms.
		if ( qw->m_quoteStart >= 0 && qw->m_quoteStart != i )
			continue;

		// ignore if weight is absolute zero
		if ( qw->m_userWeight == 0   && 
		     qw->m_userType   == 'a'  ) continue;

		// stop breach
		if ( n >= ABS_MAX_QUERY_TERMS ) {
			log("query: lost query terms to max term "
			    "limit of %" PRId32,(int32_t)ABS_MAX_QUERY_TERMS );
			break;
		}
		if ( n >= m_maxQueryTerms ) {
			log("query: lost query terms to max term cr "
			    "limit of %" PRId32,(int32_t)m_maxQueryTerms);
			break;
		}

		if(n>=nqt)
			break;

		QueryTerm *qt = &m_qterms[n];
		qt->m_qword     = qw ;
		qt->m_piped     = qw->m_piped;
		qt->m_isPhrase  = false ;
		qt->m_isUORed   = false;
		qt->m_UORedTerm   = NULL;
		qt->m_synonymOf = NULL;
		// ignore some synonym terms if tf is too low
		qt->m_ignored = qw->m_ignoreWord;
		// stop word? no, we're a phrase term
		qt->m_isQueryStopWord = qw->m_isQueryStopWord;
		// change in both places
		qt->m_termId    = qw->m_wordId & TERMID_MASK;
		qt->m_rawTermId = qw->m_rawWordId;
		// assume explicit bit is 0
		qt->m_explicitBit = 0;
		qt->m_matchesExplicitBits = 0;
		// boolean queries are not allowed term signs
		if ( m_isBoolean ) {
			qt->m_termSign = '\0';
			// boolean fix for "health OR +sports" because
			// the + there means exact word match, no synonyms.
			if ( qw->m_wordSign == '+' ) {
				qt->m_termSign  = qw->m_wordSign;
			}
		}
		// if not boolean, ensure to change signs in both places
		else {
			qt->m_termSign  = qw->m_wordSign;
		}
 		int32_t pw = i-1;
 		// . back up until word that contains quote if in a quoted 
 		//   phrase
 		// . UOR can only support two word phrases really...
 		if (m_qwords[i].m_quoteStart >= 0)
			pw = m_qwords[i].m_quoteStart ;
		if ( pw > 0 ) pw--;

 		// back two more if field
		int32_t fieldStart=-1;
		int32_t fieldLen=0;

		if ( pw == 0 && m_qwords[pw].m_ignoreWord==IGNORE_FIELDNAME)
			fieldStart = pw;

  		if ( pw > 0&& m_qwords[pw-1].m_ignoreWord==IGNORE_FIELDNAME ){
  			pw -= 1;
 			fieldStart = pw;
 		}
 		while (pw>0 && 
 		       ((m_qwords[pw].m_ignoreWord == IGNORE_FIELDNAME))) {
			pw--;
			fieldStart = pw;
		}


		// skip if it is punct. fixes queries like
		// "(this OR that)" from including '(' or from including
		// a space.
		if ( fieldStart >-1 &&
		     m_qwords[fieldStart].m_isPunct && 
		     fieldStart+1<m_numWords )
			fieldStart++;

		if (fieldStart > -1) {
			pw = i;
			while (pw < m_numWords && m_qwords[pw].m_fieldCode)
				pw++;

			fieldLen = m_qwords[pw-1].m_word + 
				m_qwords[pw-1].m_wordLen -
				m_qwords[fieldStart].m_word;
		}
		// do not use an explicit bit up if we have a hard count
		qt->m_hardCount = qw->m_hardCount;
		qw->m_queryWordTerm   = qt;
		// IndexTable.cpp uses this one
		qt->m_inQuotes  = qw->m_inQuotes;
		// point to the string itself that is the word

		if (fieldLen > 0) {
			qt->m_term    = m_qwords[fieldStart].m_word;
			qt->m_termLen = fieldLen;
			// fix for query
			// text:""  foo bar   ""
			if ( pw-1 < i ) {
				log("query: bad query %s",m_originalQuery.getBufStart());
				g_errno = EMALFORMEDQUERY;
				return false;
			}
			// skip past the end of the field value
			i = pw-1;
		}
		else {
			qt->m_termLen   = qw->m_wordLen;
			qt->m_term      = qw->m_word;
		}
					  
		// reset our implicit bits to 0
		qt->m_implicitBits = 0;

		// assign score weight, we're a phrase here
		qt->m_userWeight = qw->m_userWeight ;
		qt->m_userType   = qw->m_userType   ;
		qt->m_fieldCode  = qw->m_fieldCode  ;
		// stuff before a pipe always has a weight of 1
		if ( qt->m_piped ) {
			qt->m_userWeight = 1;
			qt->m_userType   = 'a';
		}
		n++;
	}
	
	// Handle shared explicit bits
	for ( int32_t i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		// assume not in a phrase
		qt->m_rightPhraseTermNum = -1;
		qt->m_leftPhraseTermNum  = -1;
		qt->m_rightPhraseTerm    = NULL;
		qt->m_leftPhraseTerm     = NULL;
		QueryTerm *qt2 = qt->m_UORedTerm;
		if (!qt2) continue;
		// chase down first term in UOR chain
		while (qt2->m_UORedTerm) qt2 = qt2->m_UORedTerm;
	}

	// . set implicit bits, m_implicitBits
	// . set m_inPhrase
	for (int32_t i = 0; i < m_numWords ; i++ ){
		QueryWord *qw = &m_qwords[i];
		QueryTerm *qt = qw->m_queryWordTerm;
		if (!qt) continue;
 		if ( qw->m_queryPhraseTerm )
 			qw->m_queryPhraseTerm->m_implicitBits |=
				qt->m_explicitBit;
		// set flag if in a a phrase, and set phrase term num
		if ( qw->m_queryPhraseTerm  ) {
			QueryTerm *pt = qw->m_queryPhraseTerm;
			qt->m_rightPhraseTermNum = pt - m_qterms;
			qt->m_rightPhraseTerm    = pt;
		}
		// if we're in the middle of the phrase
		int32_t pn = qw->m_leftPhraseStart;
		// convert word to its phrase QueryTerm ptr, if any
		QueryTerm *tt = NULL;
		if ( pn >= 0 ) tt = m_qwords[pn].m_queryPhraseTerm;
		if ( tt      ) tt->m_implicitBits |= qt->m_explicitBit;
		if ( tt      ) {
			qt->m_leftPhraseTermNum = tt - m_qterms;
			qt->m_leftPhraseTerm    = tt;
		}
		// . there might be some phrase term that actually contains
		//   the same word as we are, but a different occurence
		// . like '"knowledge management" AND NOT management' query
		// . made it from "j < i" into "j < m_numWords" because
		//   'test "test bed"' was not working but '"test bed" test'
		//   was working.
		for ( int32_t j = 0 ; j < m_numWords ; j++ ) {
			// must be our same wordId (same word, different occ.)
			QueryWord *qw2 = &m_qwords[j];
			if ( qw2->m_wordId != qw->m_wordId ) continue;
			// get first word in the phrase that jth word is in
			int32_t pn2 = qw2->m_leftPhraseStart;
			// we might be the guy that starts it!
			if ( pn2 < 0 && qw2->m_quoteStart != -1 ) pn2 = j;
			// if neither is the case, skip this query word
			if ( pn2 < 0 ) continue;
			// he implies us!
			QueryTerm *tt2 = m_qwords[pn2].m_queryPhraseTerm;
			if ( tt2 ) tt2->m_implicitBits |= qt->m_explicitBit;
			if ( tt2 ) {
				qt->m_leftPhraseTermNum = tt2 - m_qterms;
				qt->m_leftPhraseTerm    = tt2;
			}
			break;
		}
	}

	////////////
	//
	// . add synonym query terms now
	// . skip this part if language is unknown i guess
	//
	////////////

	if(m_queryExpansion) {
		int64_t to = hash64n("to");
		for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
			// get query word
			QueryWord *qw  = &m_qwords[i];
			// skip if in quotes, we will not get synonyms for it
			if ( qw->m_inQuotes ) continue;
			// skip if has plus sign in front
			if ( qw->m_wordSign == '+' ) continue;
			// not '-' either i guess
			if ( qw->m_wordSign == '-' ) continue;
			// no url: stuff, maybe only title
			if ( qw->m_fieldCode &&
			qw->m_fieldCode != FIELD_TITLE &&
			qw->m_fieldCode != FIELD_GENERIC )
				continue;
			// skip if ignored like a stopword (stop to->too)
			//if ( qw->m_ignoreWord ) continue;
			// ignore title: etc. words, they are field names
			if ( qw->m_ignoreWord == IGNORE_FIELDNAME ) continue;
			// ignore boolean operators
			if ( qw->m_ignoreWord ) continue;// IGNORE_BOOLOP
			// no, hurts 'Greencastle IN economic development'
			if ( qw->m_wordId == to ) continue;
			// single letters...
			if ( qw->m_wordLen == 1 ) continue;
			// set the synonyms for this word
			char tmpBuf [ TMPSYNBUFSIZE ];
			int32_t naids = syn.getSynonyms ( &words ,
							  i ,
							  // language of the query.
							  // 0 means unknown. if this
							  // is 0 we sample synonyms
							  // from all languages.
							  m_langId ,
							  tmpBuf );
			// if no synonyms, all done
			if ( naids <= 0 ) continue;
			// sanity
			if ( naids > MAX_SYNS ) { g_process.shutdownAbort(true); }
			// now make the buffer to hold them for us
			qw->m_synWordBuf.setLabel("qswbuf");
			qw->m_synWordBuf.safeMemcpy ( &syn.m_synWordBuf );
			// get the term for this word
			QueryTerm *origTerm = qw->m_queryWordTerm;
			// loop over synonyms for word #i now
			for ( int32_t j = 0 ; j < naids ; j++ ) {
				// stop breach
				if ( n >= ABS_MAX_QUERY_TERMS ) {
					log("query: lost synonyms due to max term "
					"limit of %" PRId32,
					(int32_t)ABS_MAX_QUERY_TERMS );
					break;
				}
				// this happens for 'da da da'
				if ( ! origTerm ) continue;
				
				if ( n >= m_maxQueryTerms ) {
					log("query: lost synonyms due to max cr term "
					"limit of %" PRId32,
					(int32_t)m_maxQueryTerms);
					break;
				}
				
				if(n>=nqt)
					break;
				
				// add that query term
				QueryTerm *qt   = &m_qterms[n];
				qt->m_qword     = qw; // NULL;
				qt->m_piped     = qw->m_piped;
				qt->m_isPhrase  = false ;
				qt->m_isUORed   = false;
				qt->m_UORedTerm = NULL;
				qt->m_langIdBits = 0;
				// synonym of this term...
				qt->m_synonymOf = origTerm;
				// nuke this crap since it was done above and we
				// missed out!
				qt->m_rightPhraseTermNum = -1;
				qt->m_leftPhraseTermNum  = -1;
				qt->m_rightPhraseTerm    = NULL;
				qt->m_leftPhraseTerm     = NULL;
				// need this for displaying language of syn in
				// the json/xml feed in PageResults.cpp
				qt->m_langIdBitsValid = true;
				int langId = syn.m_langIds[j];
				uint64_t langBit = (uint64_t)1 << langId;
				if ( langId >= 64 ) langBit = 0;
				qt->m_langIdBits |= langBit;
				// need this for Matches.cpp
				qt->m_synWids0 = syn.m_wids0[j];
				qt->m_synWids1 = syn.m_wids1[j];
				int32_t na        = syn.m_numAlnumWords[j];
				// how many words were in the base we used to
				// get the synonym. i.e. if the base is "new jersey"
				// then it's 2! and the synonym "nj" has one alnum
				// word.
				int32_t ba        = syn.m_numAlnumWordsInBase[j];
				qt->m_numAlnumWordsInSynonym = na;
				qt->m_numAlnumWordsInBase    = ba;

				// crap, "nj" is a synonym of the PHRASE TERM
				// bigram "new jersey" not of the single word term
				// "new" so fix that.
				if ( ba == 2 && origTerm->m_rightPhraseTerm )
					qt->m_synonymOf = origTerm->m_rightPhraseTerm;

				// ignore some synonym terms if tf is too low
				qt->m_ignored = qw->m_ignoreWord;
				// stop word? no, we're a phrase term
				qt->m_isQueryStopWord = qw->m_isQueryStopWord;
				// change in both places
				int64_t wid = syn.m_aids[j];
				// might be in a title: field or something
				if ( qw->m_prefixHash ) {
					int64_t ph = qw->m_prefixHash;
					wid= hash64h(wid,ph);
				}
				qt->m_termId    = wid & TERMID_MASK;
				qt->m_rawTermId = syn.m_aids[j];
				// assume explicit bit is 0
				qt->m_explicitBit = 0;
				qt->m_matchesExplicitBits = 0;
				// boolean queries are not allowed term signs
				if ( m_isBoolean ) {
					qt->m_termSign = '\0';
					// boolean fix for "health OR +sports" because
					// the + there means exact word match, no syns
					if ( qw->m_wordSign == '+' ) {
						qt->m_termSign  = qw->m_wordSign;
					}
				}
				// if not bool, ensure to change signs in both places
				else {
					qt->m_termSign  = qw->m_wordSign;
				}
				// do not use an explicit bit up if we got a hard count
				qt->m_hardCount = qw->m_hardCount;
				// IndexTable.cpp uses this one
				qt->m_inQuotes  = qw->m_inQuotes;
				// usually this is right
				char *ptr = syn.m_termPtrs[j];
				// buf if it is NULL that means we transformed the
				// word by like removing accent marks and stored
				// it in m_synWordBuf, as opposed to just pointing
				// to a line in memory of wiktionary-buf.txt.
				if ( ! ptr ) {
					int32_t off = syn.m_termOffs[j];
					if ( off < 0 ) {
						g_process.shutdownAbort(true); }
					if ( off > qw->m_synWordBuf.length() ) {
						g_process.shutdownAbort(true); }
					// use QueryWord::m_synWordBuf which should
					// be persistent and not disappear like
					// syn.m_synWordBuf.
					ptr = qw->m_synWordBuf.getBufStart() + off;
				}
				// point to the string itself that is the word
				qt->m_term     = ptr;
				qt->m_termLen  = syn.m_termLens[j];
				// reset our implicit bits to 0
				qt->m_implicitBits = 0;
				// assign score weight, we're a phrase here
				qt->m_userWeight = qw->m_userWeight ;
				qt->m_userType   = qw->m_userType   ;
				qt->m_fieldCode  = qw->m_fieldCode  ;
				// stuff before a pipe always has a weight of 1
				if ( qt->m_piped ) {
					qt->m_userWeight = 1;
					qt->m_userType   = 'a';
				}
				// otherwise, add it
				n++;
			}
		}
	}

	m_numTerms = n;
	
	if ( n > ABS_MAX_QUERY_TERMS ) { g_process.shutdownAbort(true); }

	// . repeated terms have the same termbits!!
	// . this is only for bool queries since regular queries ignore
	//   repeated terms in setWords()
	// . we need to support: "trains AND (perl OR python) NOT python"
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// BUT NOT IF in a UOR'd list!!!
		if ( m_qterms[i].m_isUORed ) continue;
		// that didn't seem to fix it right, for dup terms that
		// are the FIRST term in a UOR sequence... they don't seem
		// to have m_isUORed set
		if ( m_hasUOR ) continue;
		for ( int32_t j = 0 ; j < i ; j++ ) {
			// skip if not a termid match
			if(m_qterms[i].m_termId!=m_qterms[j].m_termId)continue;
			m_qterms[i].m_explicitBit = m_qterms[j].m_explicitBit;
			// if doing phrases, ignore the unrequired phrase
			if ( m_qterms[i].m_isPhrase ) {
				continue;
			}
		}
	}

	// . if only have one term and it is a signless phrase, make it signed
	// . don't forget to set m_termSigns too!
	if ( n == 1 && m_qterms[0].m_isPhrase && ! m_qterms[0].m_termSign ) {
		m_qterms[0].m_termSign = '*';
	}

	// . now set m_phrasePart for Summary.cpp's hackfix filter
	// . only set this for the non-phrase terms, since keepAllSingles is
	//   set to true when setting the Query for Summary.cpp::set in order
	//   to match the singles
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// skip cd-rom too, if not in quotes
		if ( ! m_qterms[i].m_inQuotes ) continue;
		// is next term also in a quoted phrase?
		if ( i - 1 < 0 ) continue;
		//if ( ! m_qterms[i+1].m_isPhrase ) continue;
		if ( ! m_qterms[i-1].m_inQuotes ) continue;
		// are we in the same quoted phrase?
		if ( m_qterms[i+0].m_qword->m_quoteStart !=
		     m_qterms[i-1].m_qword->m_quoteStart  ) continue;
	}

	// . set m_requiredBits
	// . these are 1-1 with m_qterms (QueryTerms)
	// . required terms have no - sign and have no signless phrases
	// . these are what terms doc would NEED to have if we were default AND
	//   BUT for boolean queries that doesn't apply
	m_requiredBits = 0; // no - signs, no signless phrases
	m_negativeBits = 0; // terms with - signs
	m_forcedBits   = 0; // terms with + signs
	m_synonymBits  = 0;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) {
			m_negativeBits |= qt->m_explicitBit; // (1 << i );
			continue;
		}
		// forced bits
		if ( qt->m_termSign == '+' && ! m_isBoolean ) 
			m_forcedBits |= qt->m_explicitBit; //(1 << i);
		// skip signless phrases
		if ( qt->m_isPhrase && qt->m_termSign == '\0' ) continue;
		if ( qt->m_synonymOf ) {
			m_synonymBits |= qt->m_explicitBit; 
			continue;
		}
		// fix gbhastitleindicator:1 where "1" is a stop word
		if ( qt->m_isQueryStopWord && ! m_qterms[i].m_fieldCode ) 
			continue;
		// OR it all up
		m_requiredBits |= qt->m_explicitBit; // (1 << i);
	}

	// set m_matchRequiredBits which we use for Matches.cpp
	m_matchRequiredBits = 0;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// skip all phrase terms
		if ( qt->m_isPhrase ) continue;
		// OR it all up
		m_matchRequiredBits |= qt->m_explicitBit;
	}

	// if we have '+test -test':
	if ( m_negativeBits & m_requiredBits ) 
		m_numTerms = 0;

        // now set m_matches,ExplicitBits, used only by Matches.cpp so far
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// set it up
		m_qterms[i].m_matchesExplicitBits = m_qterms[i].m_explicitBit;
		// or in the repeats
		for ( int32_t j = 0 ; j < m_numTerms ; j++ ) {
			// skip if termid mismatch
			if ( m_qterms[i].m_termId != m_qterms[j].m_termId ) 
				continue;
			m_qterms[i].m_matchesExplicitBits |= 
				m_qterms[j].m_explicitBit;
		}
	}

	m_numRequired = 0;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// assume not required
		qt->m_isRequired = false;
		// skip signless phrases
		if ( qt->m_isPhrase && qt->m_termSign == '\0' ) continue;
		if ( qt->m_isPhrase && qt->m_termSign == '*'  ) continue;
		if ( qt->m_synonymOf ) continue;
		// IGNORE_QSTOP?
		if ( qt->m_ignored ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}


	// required quoted phrase terms
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// quoted phrase?
		if ( ! qt->m_isPhrase ) continue;
		if ( ! qt->m_inQuotes ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}


	// . for query 'to be or not to be shakespeare'
	//   require 'tobe' 'beor' 'tobe' because
	//   they are bigrams in the wikipedia phrase 'to be or not to be'
	//   and they all consist solely of query stop words. as of
	//   8/20/2012 i took 'not' off the query stop word list.
	// . require bigrams that consist of 2 query stop words and
	//   are in a wikipedia phrase. set termSign to '+' i guess?
	// . for 'in the nick' , a wiki phrase, make "in the" required
	//   and give a big bonus for "the nick" below.
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// only check bigrams here
		if ( ! qt->m_isPhrase ) continue;
		// get the query word that starts this phrase
		QueryWord *qw1 = qt->m_qword;
		// must be in a wikiphrase
		if ( qw1->m_wikiPhraseId <= 0 ) continue;
		// what query word # is that?
		int32_t qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId != qw1->m_wikiPhraseId ) continue;
		// must be two stop words
		if ( ! qw1->m_isQueryStopWord ) continue;
		if ( ! qw2->m_isQueryStopWord ) continue;
		// mark it
		qt->m_isRequired = true;
		// count them
		m_numRequired++;
	}

	//
	// new logic for XmlDoc::setRelatedDocIdWeight() to use
	//
	int32_t shift = 0;
	m_requiredBits = 0;
	for ( int32_t i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		qt->m_explicitBit = 0;
		if ( ! qt->m_isRequired ) continue;
		// negative terms are "negative required", but we ignore here
		if ( qt->m_termSign == '-' ) continue;
		qt->m_explicitBit = 1<<shift;
		m_requiredBits |= qt->m_explicitBit;
		shift++;
		if ( shift >= (int32_t)(sizeof(qvec_t)*8) ) break;
	}
	// now implicit bits
	for ( int32_t i = 0; i < n ; i++ ){
		QueryTerm *qt = &m_qterms[i];
		// make it explicit bit at least
		qt->m_implicitBits = qt->m_explicitBit;
		if ( qt->m_isRequired ) continue;
		// synonym?
		if ( qt->m_synonymOf )
			qt->m_implicitBits |= qt->m_synonymOf->m_explicitBit;
		// skip if not bigram
		if ( ! qt->m_isPhrase ) continue;
		// get sides
		QueryTerm *t1 = qt->m_leftPhraseTerm;
		QueryTerm *t2 = qt->m_rightPhraseTerm;
		if ( ! t1 || ! t2 ) continue;
		qt->m_implicitBits |= t1->m_explicitBit;
		qt->m_implicitBits |= t2->m_explicitBit;
	}



	// . for query 'to be or not to be shakespeare'
	//   give big bonus for 'ornot' and 'notto' bigram terms because
	//   the single terms 'or' and 'to' are ignored and because
	//   'to be or not to be' is a wikipedia phrase
	// . on 8/20/2012 i took 'not' off the query stop word list.
	// . now give a big bonus for bigrams whose two terms are in the
	//   same wikipedia phrase and one and only one of the terms in
	//   the bigram is a query stop word
	// . in general 'ornot' is considered a "synonym" of 'not' and
	//   gets hit with a .90 score factor, but that should never
	//   happen, it should be 1.00 and in this special case it should
	//   be 1.20
	// . so for 'time enough for love' the phrase term "enough for"
	//   gets its m_isWikiHalfStopBigram set AND that phrase term
	//   is a synonym term of the single word term "enough" and is treated
	//   as such in the Posdb.cpp logic.
	for ( int32_t i = 0 ; i < m_numTerms ; i++ ) {
		// QueryTerms are derived from QueryWords
		QueryTerm *qt = &m_qterms[i];
		// assume not!
		qt->m_isWikiHalfStopBigram = 0;
		// don't require if negative
		if ( qt->m_termSign == '-' ) continue;
		// only check bigrams here
		if ( ! qt->m_isPhrase ) continue;
		// get the query word that starts this phrase
		QueryWord *qw1 = qt->m_qword;
		// must be in a wikiphrase
		if ( qw1->m_wikiPhraseId <= 0 ) continue;
		// what query word # is that?
		int32_t qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId != qw1->m_wikiPhraseId ) continue;
		// if both query stop words, should have been handled above
		// we need one to be a query stop word and the other not
		// for this algo
		if ( qw1->m_isQueryStopWord && qw2->m_isQueryStopWord )
			continue;
		// skip if neither is a query stop word
		if ( ! qw1->m_isQueryStopWord&& ! qw2->m_isQueryStopWord )
			continue;
		// one must be a stop word i guess
		// so for 'the time machine' we do not count 'time machine'
		// as a halfstopwikibigram
		if ( ! qw1->m_isQueryStopWord && ! qw2->m_isQueryStopWord )
			continue;

		// special flag
		qt->m_isWikiHalfStopBigram = true;
	}
	
	return true;
}

bool Query::setQWords ( char boolFlag , 
			bool keepAllSingles ,
			Words &words ,
			Phrases &phrases ) {

	// . break query up into Words and phrases
	// . because we now deal with boolean queries, we make parentheses
	//   their own separate Word, so tell "words" we're setting a query
	if ( !words.set( m_filteredQuery.getBufStart(), m_filteredQuery.length(), true ) ) {
		log(LOG_WARN, "query: Had error parsing query: %s.", mstrerror(g_errno));
		return false;
	}
	int32_t numWords = words.getNumWords();
	// truncate it
	if ( numWords > ABS_MAX_QUERY_WORDS ) {
		log("query: Had %" PRId32" words. Max is %" PRId32". Truncating.",
		    numWords,(int32_t)ABS_MAX_QUERY_WORDS);
		numWords = ABS_MAX_QUERY_WORDS;
		m_truncated = true;
	}
	m_numWords = numWords;
	// alloc the mem if we need to (mdw left off here)
	int32_t need = m_numWords * sizeof(QueryWord);
	// sanity check
	if ( m_qwords || m_qwordsAllocSize ) { g_process.shutdownAbort(true); }
	// point m_qwords to our generic buffer if it will fit
	if ( m_gnext + need < m_gbuf + GBUF_SIZE && 
	     // it can wrap so watch out with this:
	     need < GBUF_SIZE ) {
		m_qwords = (QueryWord *)m_gnext;
		m_gnext += need;
	}
	// otherwise, we must allocate memory for it
	else {
		m_qwords = (QueryWord *)mmalloc ( need , "Query4" );
		if ( ! m_qwords ) {
			log(LOG_WARN, "query: Could not allocate mem for query.");
			return false;
		}
		m_qwordsAllocSize = need;
	}
	// reset safebuf in there
	for ( int32_t i = 0 ; i < m_numWords ; i++ )
		m_qwords[i].constructor();

	// is all alpha chars in query in upper case? caps lock on?
	bool allUpper = true;
	const char *p    = m_filteredQuery.getBufStart();
	const char *pend = m_filteredQuery.getBufPtr();
	for ( ; p < pend ; p += getUtf8CharSize(p) )
		if ( is_alpha_utf8 ( p ) && ! is_upper_utf8 ( p ) ) {
			allUpper = false; break; }

	// . come back here from below when we detect dat query is not boolean
	// . we need to redo the bits cuz they may have been messed with below
	// redo:
	// field code we are in
	char  fieldCode = 0;
	char  fieldSign = 0;
	const char *field     = NULL;
	int32_t  fieldLen  = 0;
	// keep track of the start of different chunks of quotes
	int32_t quoteStart = -1;
	bool inQuotes   = false;
	//bool inVQuotes   = false;
	char quoteSign  = 0;
	// the current little sign
	char wordSign   = 0;
	// when reading first word in link: ... field we skip the following
	// words until we hit a space because we hash them all together
	bool ignoreTilSpace = false;
	// assume we're NOT a boolean query
	m_isBoolean = false;
	// used to not respect the bool operator if it is the first word
	bool firstWord = true;

	// the query processing is broken into 3 stages.

	// . STAGE #1
	// . reset all query words to default
	//   set all m_ignoreWord and m_ignorePhrase to IGNORE_DEFAULT
	// . set m_isFieldName, m_fieldCode and m_quoteStart for query words.
	//   no field names in quotes. +title:"hey there". 
	//   set m_quoteStart to -1 if not in quotes.
	// . if quotes immediately follow field code's ':' then distribute
	//   the field code to all words in the quotes
	// . distribute +/- signs across quotes and fields to m_wordSigns.
	//   support -title:"hey there".
	// . set m_quoteStart to -1 if only one alnum word is
	//   in quotes, what's the point of that?
	// . set boolean op codes (m_opcode). cannot be in quotes.
	//   cannot have a field code. cannot have a word sign (+/-).
	// . set m_wordId of FIELD_LINK, _URL, _SITE, _IP  fields.
	//   m_wordId of first should be hash of the whole field value.
	//   only set its m_ignoreWord to 0, keep it's m_ignorePhrase to DEF.
	// . set m_ignore of non-op codes, non-fieldname, alnum words to 0.
	// . set m_wordId of each non-ignored alnum word.

	// . STAGE #2
	// . customize Bits class:
	//   first alnum word can start phrase.
	//   first alnum word in quotes (m_quoteStart >= 0 ) can start phrase.
	//   connected on the right but not on the left.. can start phrase.
	//   no pair across any double quote
	//   no pair across ".." --- UNLESS in quotes!
	//   no pair across any change of field code.
	//   field names may not be part of any phrase or paired across.
	//   boolean ops may not be part of any phrase or paired across.
	//   ignored words may not be part of any phrase or paired across.

	// . STAGE #3
	// . set phrases class w/ custom Bits class mods.
	// . set m_phraseId and m_rawPhraseId of all QueryWords. if phraseId
	//   is not 0 (phrase exists) then set m_ignorePhrase to 0.
	// . set m_leftConnected, m_rightConnected. word you are connecting
	//   to must not be ignored. (no field names or op codes).
	//   ensure you are in a phrase with the connected word, too, to
	//   really be connected.
	// . set m_leftPhraseStart and m_rightPhraseEnd for all
	//   m_inQuotePhrase is not needed since if m_quoteStart is >= 0
	//   we MUST be in a quoted phrase!
	// . if word is Connected then set m_ignoreWord to IGNORE_CONNECTED.
	//   set his m_phraseSign to m_wordSign (if not 0) or '*' (if it is 0).
	//   m_wordSign may have inherited quote or field sign.
	// . if word's m_quoteStart is >= 0 set m_ignoreWord to IGNORE_QUOTED
	//   set his m_phraseSign to m_wordSign (if not 0) or '*' (if it is 0)
	//   m_wordSign may have inherited quote or field sign.
	// . if one word in a phrase is negative, then set m_phraseSign to '-'

	// set the Bits used for making phrases from the Words class
	Bits bits;
	if ( !bits.set(&words)) {
		log(LOG_WARN, "query: Had error processing query: %s.", mstrerror(g_errno));
		return false;
	}

	int32_t userWeight       = 1;
	char userType         = 'r';
	int32_t userWeightPhrase = 1;
	char userTypePhrase   = 'r';
	int32_t ignorei          = -1;

	// assume we contain no pipe operator
	int32_t pi = -1;

	int32_t posNum = 0;
	const char *ignoreTill = NULL;

	// loop over all words, these QueryWords are 1-1 with "words"
	for ( int32_t i = 0 ; i < numWords && i < ABS_MAX_QUERY_WORDS ; i++ ) {
		// convenience var, these are 1-1 with "words"
		QueryWord *qw = &m_qwords[i];
		// set to defaults?
		memset ( qw , 0 , sizeof(QueryWord) );
		// but quotestart should be -1
		qw->m_quoteStart = -1;
		qw->m_leftPhraseStart = -1;
		// assume QueryWord is ignored by default
		qw->m_ignoreWord   = IGNORE_DEFAULT;
		qw->m_ignorePhrase = IGNORE_DEFAULT;
		qw->m_ignoreWordInBoolQuery = false;
		qw->m_word    = words.getWord(i);
		qw->m_wordLen = words.getWordLen(i);
		qw->m_isPunct = words.isPunct(i);

		qw->m_posNum = posNum;

		// count 1 unit for it
		posNum++;

		// we ignore the facet value range list...
		if ( ignoreTill && qw->m_word < ignoreTill ) 
			continue;

		// . we duplicated this code from XmlDoc.cpp's
		//   getWordPosVec() function
		if ( qw->m_isPunct ) { // ! wids[i] ) {
			const char *wp = qw->m_word;
			int32_t  wplen = qw->m_wordLen;
			// simple space or sequence of just white space
			if ( words.isSpaces(i) ) 
				posNum += 0;
			// 'cd-rom'
			else if ( wp[0]=='-' && wplen==1 ) 
				posNum += 0;
			// 'mr. x'
			else if ( wp[0]=='.' && words.isSpaces(i,1))
				posNum += 0;
			// animal (dog)
			else 
				posNum++;
		}

		const char *w = words.getWord(i);
		int32_t wlen = words.getWordLen(i);
		// assume it is a query weight operator
		qw->m_queryOp = true;
		// ignore it? (this is for query weight operators)
		if ( i <= ignorei ) continue;
		// deal with pipe operators
		if ( wlen == 5 &&
		     w[0]=='P'&&w[1]=='i'&&w[2]=='i'&&w[3]=='P'&&w[4]=='E') {
			pi = i;
			qw->m_opcode = OP_PIPE;
			continue;
		}
		// [133.0r]
		// is it the bracket operator?
		// " LeFtB 113 rp RiGhB "
		if ( wlen == 5 &&
		     w[0]=='L'&&w[1]=='e'&&w[2]=='F'&&w[3]=='t'&&w[4]=='B'&& 
		     i+4 < numWords ) {
			// s MUST point to a number
			const char *s = words.getWord(i+2);
			int32_t slen = words.getWordLen(i+2);
			// if no number, it must be
			// " leFtB RiGhB " or " leFtB p RiGhB "
			if ( ! is_digit(s[0]) ) {
				// phrase weight reset
				if ( s[0] == 'p' ) {
					userWeightPhrase = 1;
					userTypePhrase   = 'r';
					ignorei = i + 4;
				}
				// word reset
				else {
					userWeight = 1;
					userType   = 'r';
					ignorei = i + 2;
				}
				continue;
			}
			// get the number
			float fval = atof2 (s, slen);
			// s2 MUST point to the a,r,ap,rp string
			const char *s2 = words.getWord(i+4);
			// is it a phrase?
			if ( s2[1] == 'p' ) {
				userWeightPhrase = fval;
				userTypePhrase   = s2[0]; // a or r
			}
			else {
				userWeight = fval;
				userType   = s2[0]; // a or r
			}
			// ignore all following words up and inc. i+6
			ignorei = i + 6;
			continue;
		}
					
		// assign score weight, if any for this guy
		qw->m_userWeight       = userWeight       ;
		qw->m_userType         = userType         ;
		qw->m_userWeightPhrase = userWeightPhrase ;
		qw->m_userTypePhrase   = userTypePhrase   ;
		qw->m_queryOp          = false;
		// does word #i have a space in it? that will cancel fieldCode
		// if we were in a field
		bool endField = false;
		if ( words.hasSpace(i) && ! inQuotes ) endField = true;
		// TODO: fix title:" hey there" (space in quotes is ok)
		// if there's a quote before the first space then
		// it's ok!!!
		if ( endField ) {
			const char *s = words.getWord(i);
			const char *send = s + words.getWordLen(i);
			for ( ; s < send ; s++ ) {
				// if the space is inside the quotes then it
				// doesn't count!
				if ( *s == '\"' ) { endField = false; break;}
				if ( is_wspace_a(*s) ) break;
			}
		}
		// cancel the field if we hit a space (not in quotes)
		if ( endField ) {
			// cancel the field
			fieldCode = 0;
			fieldLen  = 0;
			field     = NULL;
			// we no longer have to ignore for link: et al
			ignoreTilSpace = false;
		}
		// . maintain inQuotes and quoteStart
		// . quoteStart is the word # that starts the current quote
		int32_t nq = words.getNumQuotes(i) ;

		if ( nq > 0 ) { // && ! ignoreQuotes ) {
			// toggle quotes if we need to
			if ( nq & 0x01 ) inQuotes   = ! inQuotes;
			// set quote sign to sign before the quote
			if ( inQuotes ) {
				quoteSign = '\0';
				for ( const char *p = w + wlen - 1 ; p > w ; p--){
					if ( *p != '\"' ) continue;
					if ( *(p-1) == '-' ) quoteSign = '-';
					if ( *(p-1) == '+' ) quoteSign = '+';
					break;
				}
			}
			// . quoteStart is the word # the quotes started at
			// . it is -1 if not in quotes
			// . now we set it to the alnum word AFTER us!!
			if   ( inQuotes && i+1< numWords ) quoteStart =  i+1;
			else                               quoteStart = -1;
		}
		//log(LOG_DEBUG, "Query: nq: %" PRId32" inQuotes: %d,quoteStart: %" PRId32,
		//    nq, inQuotes, quoteStart);
		// does word #i have a space in it? that will cancel fieldCode
		// if we were in a field
		// TODO: fix title:" hey there" (space in quotes is ok)
		bool cancelField = false;
		if ( words.hasSpace(i) && ! inQuotes ) cancelField = true;
		// fix title:"foo bar" "another quote" so "another quote"
		// is not in the title: field
		if ( words.hasSpace(i) && inQuotes && nq>= 2 ) 
			cancelField = true;

		// likewise for gbsortby operators watch out for boolean
		// operators at the end of the field. we also check for 
		// parens below when computing the hash of the value.
		if ( (fieldCode == FIELD_GBSORTBYINT ||
		      fieldCode == FIELD_GBSORTBYFLOAT ) &&
		     ( w[0] == '(' || w[0] == ')' ) )
			cancelField = true;

		// BUT if we have a quote, and they just got turned off,
		// and the space is not after the quote, do not cancel field!
		if ( nq == 1 && cancelField ) {
			// if we hit the space BEFORE the quote, do NOT cancel
			// the field
			for ( const char *p = w + wlen - 1 ; p > w ; p--) {
				// hey, we got the quote first, keep field
				if ( *p == '\"' ) {cancelField = false; break;}
				// otherwise, we got space first? cancel it!
				if ( is_wspace_a(*p) ) break;
			}
		}
		if ( cancelField ) {
			// cancel the field
			fieldCode = 0;
			fieldLen  = 0;
			field     = NULL;
			// we no longer have to ignore for link: et al
			ignoreTilSpace = false;
		}
		// skip if we should
		if ( ignoreTilSpace ){
			if (m_qwords[i-1].m_fieldCode){
				qw->m_fieldCode = m_qwords[i-1].m_fieldCode;
			}
			continue;
		}
		// . is this word potentially a field? 
		// . it cannot be another field name in a field
		if ( i < (m_numWords-2) &&
		     w[wlen]   == ':' && ! is_wspace_utf8(w+wlen+1) && 
		     //w[wlen+1] != '/' &&  // as in http://
		     (! is_punct_utf8(w+wlen+1) || w[wlen+1]=='\"' ||
		      // for gblatrange2:-106.940994to-106.361282
		      w[wlen+1]=='-') &&  
		     ! fieldCode      && ! inQuotes                ) {
			// field name may have started before though if it
			// was a compound field name containing hyphens,
			// underscores or periods
			int32_t  j = i-1 ;
			while ( j > 0 && 
				((m_qwords[j].m_rawWordId != 0) ||
				 (  m_qwords[j].m_wordLen ==1 &&
				  ((m_qwords[j].m_word)[0]=='-' || 
				   (m_qwords[j].m_word)[0]=='_' || 
				   (m_qwords[j].m_word)[0]=='.')))) {
				j--;
			}

			if ( j < 0 ) {
				j = 0;
			}

			// advance j to a non-punct word
			while (words.isPunct(j)) j++;

			// ignore all of these words then, 
			// they're part of field name
			int32_t tlen = 0;
			for ( int32_t k = j ; k <= i ; k++ )
				tlen += words.getWordLen(k);
			// set field name to the compound name if it is
			field     = words.getWord (j);
			fieldLen  = tlen;
			if ( j == i ) fieldSign = wordSign;
			else          fieldSign = m_qwords[j].m_wordSign;

			// . is it recognized field name,like "title" or "url"?
			// . does it officially end in a colon? incl. in hash?
			bool hasColon;
			fieldCode = getFieldCode (field, fieldLen, &hasColon) ;

			// if so, it does NOT get its own QueryWord,
			// but its sign can be inherited by its members
			if ( fieldCode ) {
				for ( int32_t k = j ; k <= i ; k++ )
					m_qwords[k].m_ignoreWord = 
						IGNORE_FIELDNAME;
				continue;
			}
		}

		// what quote chunk are we in? this is 0 if we're not in quotes
		if ( inQuotes ) qw->m_quoteStart = quoteStart ;
		else            qw->m_quoteStart = -1;
		qw->m_inQuotes = inQuotes;

		// ptr to field, if any
		qw->m_fieldCode = fieldCode;
		// if we are a punct word, see if we end in a sign that can
		// be applied to the next word, a non-punct word
		if ( words.isPunct(i) ) {
			wordSign = w[wlen-1];
			if ( wordSign != '-' && wordSign != '+') wordSign = 0; 
			if ( wlen>1 &&!is_wspace_a (w[wlen-2]) ) wordSign = 0;
			if ( i > 0 && wlen == 1                ) wordSign = 0;

			// don't add any QueryWord for a punctuation word
			continue;
		}

		// what is the sign of our term? +, -, *, ...
		char mysign;
		if      ( fieldCode ) mysign = fieldSign;
		else if ( inQuotes  ) mysign = quoteSign;
		else                  mysign = wordSign;
		// are we doing default AND?
		//if ( forcePlus && ! *mysign ) mysign = '+';
		// store the sign
		qw->m_wordSign = mysign;
		// what quote chunk are we in? this is 0 if we're not in quotes
		if ( inQuotes ) qw->m_quoteStart = quoteStart ;
		else            qw->m_quoteStart = -1;

		// . get prefix hash of collection name and field
		// . but first convert field to lower case
		uint64_t ph;
		int32_t fflen = fieldLen;
		if ( fflen > 62 ) fflen = 62;
		char ff[64];
		to_lower3_a ( field , fflen , ff );

		ph = hash64 ( ff , fflen );
		// map "intitle" map to "title"
		if ( fieldCode == FIELD_TITLE )
			ph = hash64 ( "title", 5 );
		// make "suburl" map to "inurl"
		if ( fieldCode == FIELD_SUBURL )
			ph = hash64 ( "inurl", 5 );

		// fix for filetype:pdf queries
		if ( fieldCode == FIELD_TYPE )
			ph = hash64 ("type",4);

		// these are range constraints on the gbsortby: termlist
		// which sorts numbers in a field from low to high
		if ( fieldCode == FIELD_GBNUMBERMIN )
			ph = hash64 ("gbsortby", 8);
		if ( fieldCode == FIELD_GBNUMBERMAX )
			ph = hash64 ("gbsortby", 8);
		if ( fieldCode == FIELD_GBNUMBEREQUALFLOAT )
			ph = hash64 ("gbsortby", 8);

		// fix for gbsortbyfloat:product.price
		if ( fieldCode == FIELD_GBSORTBYFLOAT )
			ph = hash64 ("gbsortby", 8);

		if ( fieldCode == FIELD_GBNUMBERMININT )
			ph = hash64 ("gbsortbyint", 11);
		if ( fieldCode == FIELD_GBNUMBERMAXINT )
			ph = hash64 ("gbsortbyint", 11);
		if ( fieldCode == FIELD_GBNUMBEREQUALINT )
			ph = hash64 ("gbsortbyint", 11);

		// ptr to field, if any
		qw->m_fieldCode = fieldCode;

		// prefix hash
		qw->m_prefixHash = ph;

		// if we're hashing a url:, link:, site: or ip: term, 
		// then we need to hash ALL up to the first space
		if ( fieldCode == FIELD_URL  || 
		     fieldCode == FIELD_GBPARENTURL ||
		     fieldCode == FIELD_EXT  || 
		     fieldCode == FIELD_LINK ||
		     fieldCode == FIELD_ILINK||
		     fieldCode == FIELD_SITELINK||
		     fieldCode == FIELD_LINKS||
		     fieldCode == FIELD_SITE ||
		     fieldCode == FIELD_IP   ||
		     fieldCode == FIELD_ISCLEAN ||
		     fieldCode == FIELD_QUOTA ||
		     fieldCode == FIELD_GBSORTBYFLOAT ||
		     fieldCode == FIELD_GBREVSORTBYFLOAT ||
		     // gbmin:price:1.23
		     fieldCode == FIELD_GBNUMBERMIN ||
		     fieldCode == FIELD_GBNUMBERMAX ||
		     fieldCode == FIELD_GBNUMBEREQUALFLOAT ||

		     fieldCode == FIELD_GBSORTBYINT ||
		     fieldCode == FIELD_GBREVSORTBYINT ||
		     fieldCode == FIELD_GBNUMBERMININT ||
		     fieldCode == FIELD_GBNUMBERMAXINT ||
		     fieldCode == FIELD_GBNUMBEREQUALINT ||

		     fieldCode == FIELD_GBFIELDMATCH ) {
			// . find 1st space -- that terminates the field value
			// . make "end" point to the end of the entire query
			const char *end =
					(words.getWord(words.getNumWords() - 1) +
					 words.getWordLen(words.getNumWords() - 1));
			// use this for gbmin:price:1.99 etc.
			int32_t firstColonLen = -1;
			int32_t lastColonLen = -1;
			int32_t colonCount = 0;

			// "w" points to the first alnumword after the field,
			// so for site:xyz.com "w" points to the 'x' and wlen 
			// would be 3 in that case sinze xyz is a word of 3 
			// chars. so advance
			// wlen until we hit a space.
			while (w + wlen < end) {
				// stop at first white space
				if (is_wspace_utf8(w + wlen)) break;
				// in case of gbmin:price:1.99 record first ':'
				if (w[wlen] == ':') {
					lastColonLen = wlen;
					if (firstColonLen == -1)
						firstColonLen = wlen;
					colonCount++;
				}
				// fix "gbsortbyint:date)"
				// these are used as boolean operators
				// so do not include them in the value.
				// we also did this above to set cancelField
				// to true.
				if (w[wlen] == '(' || w[wlen] == ')')
					break;

				wlen++;
			}
			// ignore following words until we hit a space
			ignoreTilSpace = true;
			// the hash. keep it case insensitive. only
			// the fieldmatch stuff should be case-sensitive. 
			// this may change later.
			uint64_t wid = hash64Lower_utf8(w, wlen, 0LL);

			// i've decided not to make 
			// gbsortby:products.offerPrice 
			// gbmin:price:1.23 case insensitive
			// too late... we have to support what we have
			if (fieldCode == FIELD_GBSORTBYFLOAT ||
				fieldCode == FIELD_GBREVSORTBYFLOAT ||
				fieldCode == FIELD_GBSORTBYINT ||
				fieldCode == FIELD_GBREVSORTBYINT) {
				wid = hash64Lower_utf8(w, wlen, 0LL);
				// do not include this word as part of
				// any boolean expression, so
				// Expression::isTruth() will ignore it and we
				// fix '(A OR B) gbsortby:offperice' query
				qw->m_ignoreWordInBoolQuery = true;
			}

			if (fieldCode == FIELD_GBFIELDMATCH) {
				// hash the json field name. (i.e. tag.uri)
				// make it case sensitive as 
				// seen in XmlDoc.cpp::hashFacet2().
				// the other fields are hashed in 
				// XmlDoc.cpp::hashNumber3().
				// CASE SENSITIVE!!!!
				wid = hash64(w, firstColonLen, 0LL);
				// if it is like
				// gbfieldmatch:tag.uri:"http://xyz.com/poo"
				// then we should hash the string into
				// an int just like how the field value would
				// be hashed when adding gbfacetstr: terms
				// in XmlDoc.cpp:hashFacet2(). the hash of
				// the tag.uri field, for example, is set
				// in hashFacet1() and set to "val32". so
				// hash it just like that does here.
				const char *a = w + firstColonLen + 1;
				// . skip over colon at start
				if (a[0] == ':') a++;
				// . skip over quotes at start/end
				bool inQuotes = false;
				if (a[0] == '\"') {
					inQuotes = true;
					a++;
				}
				// end of field
				const char *b = a;
				// if not in quotes advance until
				// we hit whitespace
				char cs;
				for (; !inQuotes && *b; b += cs) {
					cs = getUtf8CharSize(b);
					if (is_wspace_utf8(b)) break;
				}
				// if in quotes, go until we hit quote
				for (; inQuotes && *b != '\"'; b++);
				// now hash that up. this must be 64 bit
				// to match in XmlDoc.cpp::hashFieldMatch()
				uint64_t val64 = hash64(a, b - a);
				// make a composite of tag.uri and http://...
				// just like XmlDoc.cpp::hashFacet2() does
				wid = hash64(val64, wid);
			}

			// gbmin:price:1.23
			if (lastColonLen > 0 &&
				(fieldCode == FIELD_GBNUMBERMIN ||
				 fieldCode == FIELD_GBNUMBERMAX ||
				 fieldCode == FIELD_GBNUMBEREQUALFLOAT ||
				 fieldCode == FIELD_GBNUMBEREQUALINT ||
				 fieldCode == FIELD_GBNUMBERMININT ||
				 fieldCode == FIELD_GBNUMBERMAXINT)) {

				// record the field
				wid = hash64Lower_utf8(w, lastColonLen, 0LL);

				// fix gbminint:gbfacetstr:gbxpath...:165004297
				if (colonCount == 2) {
					int64_t wid1;
					int64_t wid2;
					const char *a = w;
					const char *b = w + firstColonLen;
					wid1 = hash64Lower_utf8(a, b - a);
					a = w + firstColonLen + 1;
					b = w + lastColonLen;
					wid2 = hash64Lower_utf8(a, b - a);
					// keep prefix as 2nd arg to this
					wid = hash64(wid2, wid1);
					// we need this for it to work
					ph = 0LL;
				}
				// and also the floating point after that
				qw->m_float = atof(w + lastColonLen + 1);
				qw->m_int = (int32_t) atoll(w + lastColonLen + 1);
			}


			// should we have normalized before hashing?
			if (fieldCode == FIELD_URL ||
				fieldCode == FIELD_GBPARENTURL ||
				fieldCode == FIELD_LINK ||
				fieldCode == FIELD_ILINK ||
				fieldCode == FIELD_SITELINK ||
				fieldCode == FIELD_LINKS ||
				fieldCode == FIELD_SITE) {
				Url url;
				url.set( w, wlen, ( fieldCode != FIELD_SITE ), false );

				if (fieldCode == FIELD_SITELINK) {
					wid = hash64(url.getHost(), url.getHostLen());
				} else {
					wid = hash64(url.getUrl(), url.getUrlLen());
				}
			}

			// like we do it in XmlDoc.cpp's hashString()
			if (ph) {
				qw->m_wordId = hash64h(wid, ph);
			} else {
				qw->m_wordId = wid;
			}

			qw->m_rawWordId   = 0LL; // only for highlighting?
			qw->m_phraseId    = 0LL;
			qw->m_rawPhraseId = 0LL;
			qw->m_opcode      = 0;

			// definitely not a query stop word
			qw->m_isQueryStopWord = false;

			// do not ignore the wordId
			qw->m_ignoreWord = IGNORE_NO_IGNORE;

			// we are the first word?
			firstWord = false;

			// we're done with this one
			continue;
		}
		

		char opcode = 0;
		// if query is all in upper case and we're doing boolean 
		// DETECT, then assume not boolean
		if ( allUpper && boolFlag == 2 ) boolFlag = 0;
		// . having the UOR opcode does not mean we are boolean because
		//   we want to keep it fast.
		// . we need to set this opcode so the UOR logic in setQTerms()
		//   works, because it checks the m_opcode value. otherwise
		//   Msg20 won't think we are a boolean query and set boolFlag
		//   to 0 when setting the query for summary generation and
		//   will not recognize the UOR word as being an operator
		if ( wlen==3 && w[0]=='U' && w[1]=='O' && w[2]=='R' &&
		     ! firstWord ) {
			opcode = OP_UOR; m_hasUOR = true; goto skipin; }
		// . is this word a boolean operator?
		// . cannot be in quotes or field
		if ( boolFlag >= 1 && ! inQuotes && ! fieldCode ) {
			// are we an operator?
			if      ( ! firstWord && wlen==2 && 
				  w[0]=='O' && w[1]=='R') 
				opcode = OP_OR;
			else if ( ! firstWord && wlen==3 && 
				  w[0]=='A' && w[1]=='N' && w[2]=='D') 
				opcode = OP_AND;
			else if ( ! firstWord && wlen==3 && 
				  w[0]=='N' && w[1]=='O' && w[2]=='T') 
				opcode = OP_NOT;
			else if ( wlen==5 && w[0]=='L' && w[1]=='e' &&
				  w[2]=='F' && w[3]=='t' && w[4]=='P' )
				opcode = OP_LEFTPAREN;
			else if ( wlen==5 && w[0]=='R' && w[1]=='i' &&
				  w[2]=='G' && w[3]=='h' && w[4]=='P' )
				opcode = OP_RIGHTPAREN;
		skipin:
			// no pair across or even include any boolean op phrs
			if ( opcode ) {
				bits.m_bits[i] &= ~D_CAN_PAIR_ACROSS;
				bits.m_bits[i] &= ~D_CAN_BE_IN_PHRASE;
				qw->m_ignoreWord = IGNORE_BOOLOP;
				qw->m_opcode     = opcode;
				if ( opcode == OP_LEFTPAREN  ) continue;
				if ( opcode == OP_RIGHTPAREN ) continue;
				// if this is uncommented all of our operators
				// become actual query terms (mdw)
				if ( opcode == OP_UOR        ) continue;
				// if you just have ANDs and ()'s that does
				// not make you a boolean query! we are bool
				// by default!!
				if ( opcode == OP_AND        ) continue;
				m_isBoolean = true;
				continue;
			}
		}

		// . add single-word term id
		// . this is computed by hash64AsciiLower() 
		// . but only hash64Lower_a if _HASHWITHACCENTS_ is true
		uint64_t wid = 0LL;
		if (fieldCode == FIELD_CHARSET){
			// find first space -- that terminates the field value
			const char* end =
				(words.getWord(words.getNumWords()-1) +
				 words.getWordLen(words.getNumWords()-1));
			while ( w+wlen<end && 
				! is_wspace_utf8(w+wlen) ) wlen++;
			// ignore following words until we hit a space
			ignoreTilSpace = true;
			// convert to enum value
			int16_t csenum = get_iana_charset(w,wlen);
			// convert back to string
			char astr[128];
			int32_t alen = sprintf(astr, "%d", csenum);
			wid = hash64(astr, alen, 0LL);
		}
		else{
 			wid = words.getWordId(i);
		}
		qw->m_rawWordId = wid;
		// we now have a first word already set
		firstWord = false;
		// . are we a QUERY stop word?
		// . NEVER count as stop word if it's in all CAPS and 
		//   not all letters in the whole query is NOT in all CAPS
		// . It's probably an acronym
		if ( words.isUpper(i) && words.getWordLen(i)>1 && ! allUpper ){
			qw->m_isQueryStopWord = false;
			qw->m_isStopWord      = false;
		}
		else {
			qw->m_isQueryStopWord =::isQueryStopWord (w,wlen,wid,
								  m_langId);
			// . BUT, if it is a single letter contraction thing
			// . ninad: make this == 1 if in utf8! TODO!! it is!
			if ( i>0 && wlen == 1 && w[-1] == '\'' )
				qw->m_isQueryStopWord = true;
			qw->m_isStopWord =::isStopWord (w,wlen,wid);
		}
		// . do not count as query stop word if it is the last in query
		// . like the query: 'baby names that start with j'
		if ( i + 2 > numWords ) {
			qw->m_isQueryStopWord = false;
		}

		// like we do it in XmlDoc.cpp's hashString()
		if ( ph ) {
			qw->m_wordId = hash64(wid, ph);
		} else {
			qw->m_wordId = wid;
		}

		// do not ignore the word
		qw->m_ignoreWord = IGNORE_NO_IGNORE;
		
		//except if it is a high-frequency-term and expensive to look up. In that case ignore the word but keep the phrases/bigrams thereof
		uint64_t termId = (wid & TERMID_MASK);
		if(g_hfts.is_registered_term(termId)) {
			log(LOG_DEBUG, "query: termId %lu is a highfreq term. Marking it for ignoring",termId);
			qw->m_ignoreWord = IGNORE_HIGHFREMTERM;
		}
	}

	//If there's only one alphanumerical word and it was ignored due to high-freq-term then the query is treated as 0 terms and will return an empty
	//result. Therefore un-ignore the single word and let it fetch (best-efort) results from the high-freq-term-cache
	int numAlfanumWords = 0;
	int numAlfanumWordsHighFreqTerms = 0;
	int alfanumWordIndex = -1;
	for(int i=0; i<numWords; i++) {
		if(words.isAlnum(i)) {
			alfanumWordIndex = i;
			numAlfanumWords++;
			if(m_qwords[i].m_ignoreWord==IGNORE_HIGHFREMTERM)
				numAlfanumWordsHighFreqTerms++;
		
		}
	}
	if(numAlfanumWords == 1 && numAlfanumWordsHighFreqTerms==1)
		m_qwords[alfanumWordIndex].m_ignoreWord = IGNORE_NO_IGNORE;
	
	// pipe those that should be piped
	for ( int32_t i = 0 ; i < pi ; i++ ) m_qwords[i].m_piped = true;

	// . set m_leftConnected and m_rightConnected
	// . we are connected to the first non-punct word on our left 
	//   if we are separated by a small $ of defined punctuation
	// . see getIsConnection() for that definition
	// . this allows us to just lookup the phrase for things like
	//   "cd-rom" rather than lookup "cd" , "rom" and "cd-rom"
	// . skip if prev word is IGNORE_BOOLOP, IGNORE_FIELDNAME or
	//   IGNORE_DEFAULT
	// . we have to set outside the main loop above since we check
	//   the m_ignoreWord member of the i+2nd word
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord ) continue;
		if ( i + 2 < numWords && ! m_qwords[i+2].m_ignoreWord&&
		     isConnection(words.getWord(i+1),words.getWordLen(i+1)) )
			qw->m_rightConnected = true;
		if ( i - 2 >= 0 && ! m_qwords[i-2].m_ignoreWord && 
		     isConnection(words.getWord(i-1),words.getWordLen(i-1) ) )
			qw->m_leftConnected  = true;
	}
	
	// now modify the Bits class before generating phrases
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		// get default bits
		unsigned char b = bits.m_bits[i];
		// allow pairing across anything by default
		b |= D_CAN_PAIR_ACROSS;
		// get Query Word
		QueryWord *qw = &m_qwords[i];
		// . skip if part of a query weight operator
		// . cannot be in a phrase, or anything
		if ( qw->m_queryOp && !qw->m_opcode) { 
			b = D_CAN_PAIR_ACROSS;
		}
		// is this word a sequence of punctuation and spaces?
		else if ( words.isPunct(i) ) {
			// pair across ANY punct, even double spaces by default
			b |= D_CAN_PAIR_ACROSS;
			// but do not pair across anything with a quote in it
			if ( words.getNumQuotes(i) >0) b &= ~D_CAN_PAIR_ACROSS;
			// continue if we're in quotes
			else if ( qw->m_quoteStart >= 0 ) goto next;
			// continue if we're in a field
			else if ( qw->m_fieldCode > 0 ) goto next;
			// if guy on left is in field, do not pair across
			if ( i > 0 && m_qwords[i-1].m_fieldCode > 0 )
				b &= ~D_CAN_PAIR_ACROSS;
			// or if guy on right in field
			if ( i +1 < numWords && m_qwords[i+1].m_fieldCode > 0 )
				b &= ~D_CAN_PAIR_ACROSS;
			// do not pair across ".." when not in quotes/field
			const char *w = words.getWord(i);
			int32_t  wlen = words.getWordLen(i);
			for ( int32_t j = 0 ; j < wlen-1 ; j++ ) {
				if ( w[j  ]!='.' ) continue;
				if ( w[j+1]!='.' ) continue;
				b &= ~D_CAN_PAIR_ACROSS;
				break;
			}
		}
		else {
			// . no field names, bool operators, cruft in fields
			//   can be any part of a phrase
			// . no pair across any change of field code
			// . 'girl title:boy' --> no "girl title" phrase!
			if ( qw->m_ignoreWord && qw->m_ignoreWord!=IGNORE_HIGHFREMTERM ) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
			}
			// . no boolean ops
			// . 'this OR that' --> no "this OR that" phrase
			if ( qw->m_opcode ) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
			}
			if ( qw->m_wordSign == '-' && qw->m_quoteStart < 0) {
				b &= ~D_CAN_PAIR_ACROSS;
				b &= ~D_CAN_BE_IN_PHRASE;
			}

		}
	next:
		// set it back all tweaked
		bits.m_bits[i] = b;
	}

	// treat strongly connected phrases like cd-rom and 3.2.0.3 as being
	// in quotes for the most part, therefore, set m_quoteStart for them
	int32_t j;
	int32_t qs = -1;
	for ( j = 0 ; j < numWords ; j++ ) {
		// skip all but strongly connected words
		if ( m_qwords[j].m_ignoreWord != IGNORE_CONNECTED &&
		     // must also be non punct word OR a space
		     ( !words.isPunct(j) || words.getWord(j)[0] == ' ' ) ) {
			// break the "quote", if any
			qs = -1; continue; }
		// if he is punctuation and qs is -1, skip him,
		// punctuation words can no longer start a quote
		if ( words.isPunct(j) && qs == -1 ) continue;
		// uningore him if we should
		if ( keepAllSingles ) m_qwords[j].m_ignoreWord = IGNORE_NO_IGNORE;
		// if already in quotes, don't bother!
		if ( m_qwords[j].m_quoteStart >= 0 ) continue;
		// remember him
		if ( qs == -1 ) qs = j;
		// he starts the phrase
		m_qwords[j].m_quoteStart = qs;
		// force him into a quoted phrase
		m_qwords[j].m_inQuotes   = true;
		//m_qwords[j].m_inQuotedPhrase = true;
	}

	// fix for tags.uri:http://foo.com/bar so it works like
	// tags.uri:"http://foo.com/bar" like it should
	int32_t first = -1;
	for ( j = 0 ; j < numWords ; j++ ) {
		// stop when we hit spaces
		if ( words.hasSpace(j) ) {
			first = -1;
			continue;
		}
		// skip if not in field
		if ( ! m_qwords[j].m_fieldCode ) continue;
		// must be in a generic field, the other fields like site:
		// will be messed up by this logic
		if ( m_qwords[j].m_fieldCode != FIELD_GENERIC ) continue;
		// first alnumword in field?
		if ( first == -1 ) {
			// must be alnum
			if ( m_qwords[j].m_isPunct ) continue;
			// must have punct then another alnum word
			if ( j+2 >= numWords ) break;
			// spaces screw it up
			if ( words.hasSpace(j+1) ) continue;
			// then an alnum word after
			first = j;
		}
		// we are in fake quoted phrase
		m_qwords[j].m_inQuotes = true;
		m_qwords[j].m_quoteStart = first;
	}

	// make the phrases from the words and the tweaked Bits class
	if ( !phrases.set( &words, &bits ) )
		return false;

	int64_t *wids = words.getWordIds();

	// do phrases stuff
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		// get the ith QueryWord
		QueryWord *qw = &m_qwords[i];

		//if word is ignored (and it is not due to high-freq-term) then don't generate a phrase/bigram query term
		if(qw->m_ignoreWord && qw->m_ignoreWord!=IGNORE_HIGHFREMTERM)
			continue;
		if ( qw->m_fieldCode && qw->m_quoteStart < 0) continue;
		// get the first word # to our left that starts a phrase
		// of which we are a member
		qw->m_leftPhraseStart = -1;
		for ( int32_t j = i - 1 ; j >= 0 ; j-- ) {
			if ( ! bits.canPairAcross(j+1) ) break;
			if ( ! wids[j] ) continue;

			qw->m_leftPhraseStart = j;
			// we can't pair across alnum words now, we just want bigrams
			if ( wids[j] ) break;
			// now we do bigrams so only allow two words even
			// if they are stop words
			break;
		}
		// . is this word in a quoted phrase?
		// . the whole phrase must be in the same set of quotes
		// . if we're in a left phrase, he must be in our quotes
		if ( qw->m_leftPhraseStart >= 0 &&
		     qw->m_quoteStart      >= 0 &&
		     qw->m_leftPhraseStart >= qw->m_quoteStart ) 
			qw->m_inQuotedPhrase = true;
		// if we start a phrase, ensure next guy is in our quote
		if ( ! qw->m_ignorePhrase && i+1 < numWords &&
		     m_qwords[i+1].m_quoteStart >= 0 &&
		     m_qwords[i+1].m_quoteStart <= i )
			qw->m_inQuotedPhrase = true;
		// are we the first word in the quote?
		if ( i-1>=0 && qw->m_quoteStart == i )
			qw->m_inQuotedPhrase = true;
		// ignore single words that are in a quoted phrase
		if ( ! keepAllSingles && qw->m_inQuotedPhrase ) 
			qw->m_ignoreWord = IGNORE_QUOTED;

		// . get phrase info for this term
		// . a pid (phraseId)of 0 indicates it does not start a phrase
		// . raw phrase termId
		uint64_t pid = 0LL;

		phrases.getMinWordsInPhrase(i,(int64_t *)&pid);;

		// store it
		qw->m_rawPhraseId = pid;

		// does word #i start a phrase?
		if ( pid != 0 ) {
			uint64_t ph = qw->m_prefixHash ;

			// like we do it in XmlDoc.cpp's hashString()
			if ( ph ) qw->m_phraseId = hash64 ( pid , ph );
			else      qw->m_phraseId = pid;

			// how many regular words int32_t is the bigram?
			int32_t plen2;
			char buf[256];
			phrases.getPhrase(i, buf, sizeof(buf), &plen2);

			// get just the bigram for now
			qw->m_phraseLen = plen2;

			// do not ignore the phrase, it's valid
			qw->m_ignorePhrase = IGNORE_NO_IGNORE;
		}


		// . phrase sign is inherited from word's sign if it's a minus
		// . word sign is inherited from field, quote or right before
		//   the word
		// . that is, all words in -"to be or not" will have a '-' sign
		// . phraseId may or may not be 0 at this point
		if ( qw->m_wordSign == '-' ) qw->m_phraseSign = '-';

		// . dist word signs to others in the same connected string
		// . use "-cd-rom x-box" w/ no connector in between
		// . test queries:
		// . +cd-rom +x-box
		// . -cd-rom +x-box
		// . -m-o-n
		// . who was the first   (was is a query stop word)
		// . www.xxx.com
		// . welcome to har.com
		// . hezekiah walker the love family affair ii live at radio 
		//   city music hall
		// . fotostudio +m-o-n-a-r-t
		// . fotostudio -m-o-n-a-r-t
		// . i'm home
		if ( qw->m_leftConnected && qw->m_leftPhraseStart >= 0 )
			qw->m_wordSign = m_qwords[i-2].m_wordSign;

		// . if we connected to the alnum word on our right then
		//   soft require the phrase (i.e. treat like a single term)
		// . example: cd-rom or www.xxx.com
		// . 'welcome to har.com' should get a '*' for "har.com" sign
		if ( qw->m_rightConnected ) {
			if ( qw->m_wordSign) qw->m_phraseSign = qw->m_wordSign;
			else                 qw->m_phraseSign = '*';
		}

		// . if we're in quotes then any phrase we have should be
		//   soft required (i.e. treated like a single term)
		// . we do not allow phrases in queries to pair across
		//   quotes. See where we tweak the Bits class above.
		if ( qw->m_quoteStart >= 0 ) {
			qw->m_phraseSign = '*';
		}

		// . if we are the last word in a phrase that consists of all
		//   PLAIN stop words then make the phrase have a '*'
		// . 'to be or not to be .. test' (cannot pair across "..")
		// . don't use QUERY stop words cuz of "who was the first?" qry
		if ( pid ) {
			int32_t nw = phrases.getNumWordsInPhrase2(i);
			int32_t j;
			// search up to this far
			int32_t maxj = i + nw;
			// but not past our truncated limit
			if ( maxj > ABS_MAX_QUERY_WORDS ) 
				maxj = ABS_MAX_QUERY_WORDS;

			for ( j = i ; j < maxj ; j++ ) {
				// skip punct
				if ( words.isPunct(j)         ) continue;
				// break out if not a stop word
				if ( ! bits.isStopWord(j)     ) break;
				// break out if has a term sign
				if (   m_qwords[j].m_wordSign ) break;
			}
			// if everybody in phrase #i was a signless stopword
			// and the phrase was signless, make it have a '*' sign
			if ( j >= maxj && m_qwords[i].m_phraseSign == '\0' ) 
				m_qwords[i].m_phraseSign = '*';
			// . if a constituent has a - sign, then the whole 
			//   phrase becomes negative, too
			// . fixes 'apple -computer' truncation problem
			for ( int32_t j = i ; j < maxj ; j++ )
				if ( m_qwords[j].m_wordSign == '-' )
					qw->m_phraseSign = '-';
		}

		// . ignore unsigned QUERY stop words that are not yet ignored 
		//   and are in unignored phrases
		// . 'who was the first taiwanese president' should not get 
		//   "who was" term sign changed to '*' because "was" is a
		//   QUERY stop word. So ignore singles query stop words
		//   in phrases now
		if ( //! keepAllSingles && 
		     (qw->m_isQueryStopWord && !m_isBoolean) &&
		     m_useQueryStopWords &&
		     ! qw->m_fieldCode &&
		     // fix 'the tigers'
		     //(qw->m_leftPhraseStart >= 0 || qw->m_phraseId > 0 ) && 
		     ! qw->m_wordSign && 
		     ! qw->m_ignoreWord )
			qw->m_ignoreWord = IGNORE_QSTOP;

		// . ignore and/or between quoted phrases, save user from 
		//   themselves (they meant AND/OR)
		if ( ! keepAllSingles && qw->m_isQueryStopWord &&
		     ! qw->m_fieldCode &&
		     m_useQueryStopWords &&
		     ! qw->m_phraseId && ! qw->m_inQuotes &&
		     ((qw->m_wordId == 255176654160863LL) ||
		      (qw->m_wordId ==  46196171999655LL))        )
			qw->m_ignoreWord = IGNORE_QSTOP;
		// . ignore repeated single words and phrases
		// . look at the old termIds for this, too
		// . should ignore 2nd 'time' in 'time after time' then
		// . but boolean queries often need to repeat terms

		// . NEW - words much be same sign and not in different
		// . quoted phrases to be ignored -partap
		if ( ! m_isBoolean && !qw->m_ignoreWord ) {
			for ( int32_t j = 0 ; j < i ; j++ ) {
				if ( m_qwords[j].m_ignoreWord   ) continue;
				if ( m_qwords[j].m_wordId == qw->m_wordId &&
				     m_qwords[j].m_wordSign ==qw->m_wordSign &&
				     (!keepAllSingles || 
				      (m_qwords[j].m_quoteStart 
				       == qw->m_quoteStart))){
					qw->m_ignoreWord = IGNORE_REPEAT;
				}
			}
		}
		if ( ! m_isBoolean && !qw->m_ignorePhrase ) {
			// ignore repeated phrases too!
			for ( int32_t j = 0 ; j < i ; j++ ) {
				if ( m_qwords[j].m_ignorePhrase ) continue;
				if ( m_qwords[j].m_phraseId == qw->m_phraseId &&
				     m_qwords[j].m_phraseSign 
				     == qw->m_phraseSign)
					qw->m_ignorePhrase = IGNORE_REPEAT;
			}
		}
	}

	// . if we only have one quoted query then force its sign to be '+'
	// . '"get the phrase" the' --> +"get the phrase" (last the is ignored)
	// . "time enough for love" --> +"time enough" +"enough for love"
	// . if all unignored words are in the same set of quotes then change
	//   all '*' (soft-required) phrase signs to '+'
	for ( j= 0 ; j < numWords ; j++ ) {
		if ( words.isPunct(j)) continue;
		if ( m_qwords[j].m_quoteStart < 0 ) break;
		if ( m_qwords[j].m_ignoreWord ) continue;
		if ( j < 2 ) continue;
		if ( m_qwords[j-2].m_quoteStart != m_qwords[j].m_quoteStart )
			break;
	}
	if ( j >= numWords ) {
		for ( j= 0 ; j < numWords ; j++ ) {
			if ( m_qwords[j].m_phraseSign == '*' )
				m_qwords[j].m_phraseSign = '+';
		}
	}
		
	// . force a plus on any site: or ip: query terms
	// . also disable site clustering if we have either of these terms
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord ) continue;
		if ( qw->m_wordSign   ) continue;
		if ( qw->m_fieldCode != FIELD_SITE &&
		     qw->m_fieldCode != FIELD_IP     ) continue;
		qw->m_wordSign = '+';
	}

	// . if one or more of a phrase's constituent terms exceeded 
	//   term #MAX_QUERY_TERMS then we should also soft require that phrase
	// . fixes 'hezekiah walker the love family affair ii live at 
	//          radio city music hall'
	// . how many non-ignored phrases?
	int32_t count = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {	
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase ) continue;
		if ( ! qw->m_phraseId   ) continue;
		count++;
	}
	for ( int32_t i = 0 ; i < numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		// count non-ignored words
		if ( qw->m_ignoreWord ) continue;
		// if under limit, continue
		if ( count++ < ABS_MAX_QUERY_TERMS ) continue;
		// . otherwise, ignore
		// . if we set this for our UOR'ed terms from SearchInput.cpp's
		//   UOR'ed facebook interests then it causes us to get no results!
		//   so make sure that MAX_QUERY_TERMS is big enough with respect to
		//   the opCount in SearchInput.cpp
		qw->m_ignoreWord = IGNORE_BREECH;
		// left phrase should get a '*'
		int32_t left = qw->m_leftPhraseStart;
		if ( left >= 0 && ! m_qwords[left].m_phraseSign )
			m_qwords[left].m_phraseSign = '*';
		// our phrase should get a '*'
		if ( qw->m_phraseId && ! qw->m_phraseSign )
			qw->m_phraseSign = '*';
	}

	// . fix the 'x -50a' query so it returns results
	// . how many non-negative, non-ignored words/phrases do we have?
	count = 0;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignoreWord      ) continue;
		if ( qw->m_wordSign == '-' ) continue;
		count++;
	}
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		if ( qw->m_ignorePhrase      ) continue;
		if ( qw->m_phraseSign == '-' ) continue;
		if ( qw->m_phraseId == 0LL   ) continue;
		count++;
	}
	// if everybody is ignored or negative UNignore first query stop word
	if ( count == 0 ) {
		for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
			QueryWord *qw = &m_qwords[i];
			if ( qw->m_ignoreWord != IGNORE_QSTOP ) continue;
			qw->m_ignoreWord = IGNORE_NO_IGNORE;
			count++;
			break;
		}
	}

	quoteStart = -1;
	int32_t quoteEnd = -1;
	// set m_quoteENd
	for ( int32_t i = m_numWords - 1 ; i >= 0 ; i-- ) {
		// get ith word
		QueryWord *qw = &m_qwords[i];
		// skip if ignored
		if ( qw->m_ignoreWord ) continue;
		// skip if not in quotes
		if ( qw->m_quoteStart < 0 ) continue;
		// if match previous guy...
		if ( qw->m_quoteStart == quoteStart ) {
			// inherit the end
			qw->m_quoteEnd = quoteEnd;
			// all done
			continue;
		}
		// ok, we are the end then
		quoteEnd   = i;
		quoteStart = qw->m_quoteStart;
	}		


	int32_t wkid = 0;
	int32_t upTo = -1;

	//
	// set the wiki phrase ids
	//
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		// get ith word
		QueryWord *qw = &m_qwords[i];
		// in a phrase from before?
		if ( i < upTo ) {
			qw->m_wikiPhraseId = wkid;
			continue;
		}
		// assume none
		qw->m_wikiPhraseId = 0;
		// skip if punct
		if ( ! wids[i] ) continue;
		// get word
		int32_t nwk ;
		nwk = g_wiki.getNumWordsInWikiPhrase ( i , &words );
		// bail if none
		if ( nwk <= 1 ) continue;

		// inc it
		wkid++;
		// store it
		qw->m_wikiPhraseId = wkid;
		// set loop parm
		upTo = i + nwk;
	}

	// consider terms strongly connected like wikipedia title phrases
	for ( int32_t i = 0 ; i + 2 < m_numWords ; i++ ) {
		// get ith word
		QueryWord *qw1 = &m_qwords[i];
		// must not already be in a wikiphrase
		//if ( qw1->m_wikiPhraseId > 0 ) continue;
		// what query word # is that?
		int32_t qwn = qw1 - m_qwords;
		// get the next alnum word after that
		// assume its the last word in our bigram phrase
		QueryWord *qw2 = &m_qwords[qwn+2];
		// must be in same wikiphrase
		if ( qw2->m_wikiPhraseId > 0 ) continue;

		// if there is a strong connector like the . in 'dmoz.org'
		// then consider it a wiki bigram too
		if ( ! qw1->m_rightConnected ) continue;
		if ( ! qw2->m_leftConnected  ) continue;

		// fix 'rdf.org.dumps' so org.dumps gets same
		// wikiphraseid as rdf.org
		int id;
		if ( qw1->m_wikiPhraseId ) id = qw1->m_wikiPhraseId;
		else id = ++wkid;

		// store it
		qw1->m_wikiPhraseId = id;

		qw2->m_wikiPhraseId = id;
	}

	// all done
	return true;
}

// return -1 if does not exist in query, otherwise return the query word num
int32_t Query::getWordNum(int64_t wordId) const {
	// skip if punct or whatever
	if ( wordId == 0LL || wordId == -1LL ) return -1;
	for ( int32_t i = 0 ; i < m_numWords ; i++ ) {
		QueryWord *qw = &m_qwords[i];
		// the non-raw word id includes a hash with "0", which
		// signifies an empty field term
		if ( qw->m_rawWordId == wordId ) return i;
	}
	// otherwise, not found
	return -1;
}

static HashTableX s_table;
static bool       s_isInitialized = false;

// 3rd field = m_hasColon
struct QueryField g_fields[] = {
	{"url",
	 FIELD_URL, 
	 true,
	 "url:www.example.com/page.html",
	 "Matches the page with that exact url. Uses the first url, not "
	 "the url it redirects to, if any." , 
	 NULL,
	 0 },

	{"ext", 
	 FIELD_EXT, 
	 true,
	 "ext:doc",
	 "Match documents whose url ends in the <i>.doc</i> file extension.",
	 NULL,
	 0 },


	{"link", 
	 FIELD_LINK, 
	 true,
	 "link:www.example.com/foo.html",
	 "Matches all the documents that have a link to "
	 "http://www.example.com/foobar.html",
	 NULL,
	 0 },

	{"sitelink", 
	 FIELD_SITELINK, 
	 true,
	 "sitelink:abc.foobar.com",
	 "Matches all documents that link to any page on the "
	 "<i>abc.foobar.com</i> site.",
	 NULL,
	 0 },

	{"site", 
	 FIELD_SITE, 
	 true,
	 "site:example.com",
	 "Matches all documents on the example.com domain.",
	 NULL,
	 0 },

	{"site", 
	 FIELD_SITE, 
	 true,
	 "site:www.example.com/dir1/dir2/",
	 "Matches all documents whose url starts with "
	 "www.example.com/dir1/dir2/",
	 NULL,
	 QTF_DUP },

	{"ip", 
	 FIELD_IP, 
	 true,
	 "ip:192.0.2.1",
	 "Matches all documents whose IP is 192.0.2.1.",
	 NULL,
	 0 },


	{"ip", 
	 FIELD_IP, 
	 true,
	 "ip:192.0.2",
	 "Matches all documents whose IP STARTS with 192.0.2.",
	 NULL,
	 QTF_DUP },


	{"inurl", 
	 FIELD_SUBURL, 
	 true,
	 "inurl:dog",
	 "Matches all documents that have the word dog in their url, like "
	 "http://www.example.com/dog/food.html. However will not match "
	 "http://www.example.com/dogfood.html because it is not an "
	 "individual word. It must be delineated by punctuation.",
	 NULL,
	 0 },


	{"suburl", 
	 FIELD_SUBURL, 
	 true,
	 "suburl:dog",
	 "Same as inurl.",
	 NULL,
	0},

	{"intitle", 
	 FIELD_TITLE, 
	 false,
	 "title:cat",
	 "Matches all the documents that have the word cat in their "
	 "title.",
	 NULL,
	 0 },


	{"intitle", 
	 FIELD_TITLE, 
	 false,
	 "title:\"cat food\"",
	 "Matches all the documents that have the phrase \"cat food\" "
	 "in their title.",
	 NULL,
	 QTF_DUP },
	

	{"title", 
	 FIELD_TITLE, 
	 false,
	 "title:cat",
	 "Same as intitle:",
	 NULL,
	0},

	{"type", 
	 FIELD_TYPE, 
	 false,
	 "type:json",
	 "Matches all documents that are in JSON format. "
	 "Other possible types include "
	 "<i>html, text, xml, pdf, doc, xls, ppt, ps, css, json, status.</i> "
	 "<i>status</i> matches special documents that are stored every time "
	 "a url is spidered so you can see all the spider attempts and when "
	 "they occurred as well as the outcome.",
	 NULL,
	 0},

	{"filetype", 
	 FIELD_TYPE, 
	 false,
	 "filetype:json",
	 "Same as type: above.",
	 NULL,
	0},

	{"gbisadult",
	 FIELD_GENERIC,
	 false,
	 "gbisadult:1",
	 "Matches all documents that have been detected as adult documents "
	 "and may be unsuitable for children. Likewise, use "
	 "<i>gbisadult:0</i> to match all documents that were NOT detected "
	 "as adult documents.",
	 NULL,
	 0},

	{"gbzipcode", 
	 FIELD_ZIP, 
	 false,
	 "gbzip:90210",
	 "Matches all documents that have the specified zip code "
	 "in their meta zip code tag.",
	 NULL,
	 0},

	{"gblang",
	 FIELD_GBLANG,
	 false,
	 "gblang:de",
	 "Matches all documents in german. "
	 "The supported language abbreviations "
	 "are at the bottom of the <a href=/admin/filters>url filters</a> "
	 "page. Some more "
	 "common ones are <i>gblang:en, gblang:es, gblang:fr, "
	 // need quotes for this one!!
	 "gblang:\"zh_cn\"</i> (note the quotes for zh_cn!).",
	 NULL,
	 0},

	// diffbot only
	{"gbparenturl", 
	 FIELD_GBPARENTURL, 
	 true,
	 "gbparenturl:www.example.com/foo.html",
	 "Diffbot only. Match the json urls that "
	 "were extract from this parent url. Example: "
	 "gbparenturl:www.example.com/addurl.htm",
	 NULL,
	 0},

	{"gbcountry",
	 FIELD_GBCOUNTRY,
	 false,
	 "gbcountry:us",
	 "Matches documents determined by Gigablast to be from the United "
	 "States. See the country abbreviations in the CountryCode.cpp "
	 "open source distribution. Some more popular examples include: "
	 "de, fr, uk, ca, cn.",
	 NULL,
	 0} ,

// mdw

	{"gbpermalink",
	 FIELD_GBPERMALINK,
	 false,
	 "gbpermalink:1",
	 "Matches documents that are permalinks. Use <i>gbpermalink:0</i> "
	 "to match documents that are NOT permalinks.",
	 NULL,
	0},

	{"gbdocid",
	 FIELD_GBDOCID,
	 false,
	 "gbdocid:123456",
	 "Matches the document with the docid 123456",
	 NULL,
	 0},

	{"gbtermid",
	 FIELD_GBTERMID,
	 false,
	 "gbtermid:123456",
	 "Matches the documents for the term with termid 123456",
	 NULL,
	 0},

	//
	// for content type CT_STATUS documents (Spider status docs)
	//

	{"gbsortbyint", 
	 FIELD_GBSORTBYINT, 
	 false,
	 "gbsortbyint:gbdocspiderdate",
	 "Sort all documents by the date they were spidered/downloaded.",
	 NULL,
	 0},

	{"gbrevsortbyint", 
	 FIELD_GBREVSORTBYINT, 
	 false,
	 "gbrevsortbyint:gbdocspiderdate",
	 "Sort all documents by the date they were spidered/downloaded "
	 "but with the oldest on top.",
	 NULL,
	 0},

	{"gbdocspiderdate",
	 FIELD_GENERIC,
	 false,
	 "gbdocspiderdate:1400081479",
	 "Matches documents that have "
	 "that spider date timestamp (UTC). "
	 //"Does not include the "
	 //"special spider status documents. "
	 "This is the time the document "
	 "completed downloading.",
	 "Date Related Query Operators",
	 QTF_BEGINNEWTABLE},


	{"gbspiderdate",
	 FIELD_GENERIC,
	 false,
	 "gbspiderdate:1400081479",
	 "Like above.",
	 //, but DOES include the special spider status documents.",
	 NULL,
	 0},

	{"gbdocindexdate",
	 FIELD_GENERIC,
	 false,
	 "gbdocindexdate:1400081479",
	 "Like above, but is the time the document was last indexed. "
	 "This time is "
	 "slightly greater than or equal to the spider date.",//Does not "
	 //"include the special spider status documents.",
	 NULL,
	 0},


	{"gbindexdate",
	 FIELD_GENERIC,
	 false,
	 "gbindexdate:1400081479",
	 "Like above.",//, but it does include the special spider status "
	 //"documents.",
	 NULL,
	 0},

	//
	// spider status docs queries
	//

	{"gbssUrl",
	 FIELD_GENERIC,
	 false,
	 "gbssUrl:com",
	 "Query the url of a spider status document.",
	 "Spider Status Documents", // title
	 QTF_BEGINNEWTABLE},


	{"gbssFinalRedirectUrl",
	 FIELD_GENERIC,
	 false,
	 "gbssFinalRedirectUrl:example.com/page2.html",
	 "Query on the last url redirect to, if any.",
	 NULL, // title
	 0},

	{"gbssStatusCode",
	 FIELD_GENERIC,
	 false,
	 "gbssStatusCode:0",
	 "Query on the status code of the index attempt. 0 means no error.",
	 NULL,
	 0},

	{"gbssStatusMsg",
	 FIELD_GENERIC,
	 false,
	 "gbssStatusMsg:\"Tcp timed\"",
	 "Like gbssStatusCode but a textual representation.",
	 NULL,
	 0},

	{"gbssHttpStatus",
	 FIELD_GENERIC,
	 false,
	 "gbssHttpStatus:200",
	 "Query on the HTTP status returned from the web server.",
	 NULL,
	 0},

	{"gbssWasIndexed",
	 FIELD_GENERIC,
	 false,
	 "gbssWasIndexed:0",
	 "Was the document in the index before attempting to index? Use 0 "
	 " or 1 to find all documents that were not or were, respectively.",
	 NULL,
	 0},

	{"gbssIsDiffbotObject",
	 FIELD_GENERIC,
	 false,
	 "gbssIsDiffbotObject:1",
	 "This field is only present if the document was an object from "
	 "a diffbot reply. Use gbssIsDiffbotObject:0 to find the non-diffbot "
	 "objects.",
	 NULL,
	 0},

	{"gbssAgeInIndex",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssAgeInIndex",
	 "If the document was in the index at the time we attempted to "
	 "reindex it, how long has it been since it was last indexed?",
	 NULL,
	 0},

	{"gbssDomain",
	 FIELD_GENERIC,
	 false,
	 "gbssDomain:yahoo.com",
	 "Query on the domain of the url.",
	 NULL,
	 0},

	{"gbssSubdomain",
	 FIELD_GENERIC,
	 false,
	 "gbssSubdomain:www.yahoo.com",
	 "Query on the subdomain of the url.",
	 NULL,
	 0},

	{"gbssDocId",
	 FIELD_GENERIC,
	 false,
	 "gbssDocId:1234567",
	 "Show all the spider status docs for the document with this docId.",
	 NULL,
	 0},

	{"gbssDupOfDocId",
	 FIELD_GENERIC,
	 false,
	 "gbssDupOfDocId:123456",
	 "Show all the documents that were considered dups of this docId.",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexAttempts",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexAttempts:1",
	 "Before this index attempt, how many attempts were there?",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexSuccesses",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexSuccesses:1",
	 "Before this index attempt, how many successful attempts were there?",
	 NULL,
	 0},

	{"gbssPrevTotalNumIndexFailures",
	 FIELD_GENERIC,
	 false,
	 "gbssPrevTotalNumIndexFailures:1",
	 "Before this index attempt, how many failed attempts were there?",
	 NULL,
	 0},

	{"gbssFirstIndexed",
	 FIELD_GENERIC,
	 false,
	 "gbrevsortbyint:gbssFirsIndexed",
	 "The date in utc that the document was first indexed.",
	 NULL,
	 0},

	{"gbssDownloadDurationMS",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadDurationMS",
	 "How long it took in millisecons to download the document.",
	 NULL,
	 0},

	{"gbssDownloadStartTime",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadStartTime",
	 "When the download started, in seconds since the epoch, UTC.",
	 NULL,
	 0},

	{"gbssDownloadEndTime",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssDownloadEndTime",
	 "When the download ended, in seconds since the epoch, UTC.",
	 NULL,
	 0},

	{"gbssIp",
	 FIELD_GENERIC,
	 false,
	 "gbssIp:192.0.2.1",
	 "The IP address of the document being indexed. Is 0.0.0.0 "
	 "if unknown.",
	 NULL,
	 0},

	{"gbssIpLookupTimeMS",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssIpLookupTimeMS",
	 "How long it took to lookup the IP of the document. Might have been "
	 "in the cache.",
	 NULL,
	 0},

	{"gbssSiteNumInlinks",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssSiteNumInlinks",
	 "How many good inlinks the document's site had.",
	 NULL,
	 0},

	{"gbssSiteRank",
	 FIELD_GENERIC,
	 false,
	 "gbsortby:gbssSiteRank",
	 "The site rank of the document. Based directly "
	 "on the number of inlinks the site had.",
	 NULL,
	 0},

	{"gbssContentLen",
	 FIELD_GENERIC,
	 false,
	 "gbsortbyint:gbssContentLen",
	 "The content length of the document. 0 if empty or not downloaded.",
	 NULL,
	 0},

	// they don't need to know about this
	{"gbcontenthash", FIELD_GBCONTENTHASH, false,"","",NULL,QTF_HIDE}
};

void resetQuery ( ) {
	s_table.reset();
}



int32_t getNumFieldCodes ( ) { 
	return (int32_t)sizeof(g_fields) / (int32_t)sizeof(QueryField); 
}

static bool initFieldTable(){

	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( 8 , 4 , 255,NULL,0,false,"qryfldtbl" ) ) {
			log(LOG_WARN, "build: Could not init table of query fields.");
			return false;
		}
		// now add in all the stop words
		int32_t n = getNumFieldCodes();
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// skip if dup
			int64_t h = hash64b ( g_fields[i].text );

			// if already in there it is a dup
			if ( s_table.isInTable ( &h ) ) continue;

			// store the entity index in the hash table as score
			if ( ! s_table.addTerm(h, i+1) ) return false;
		}
		s_isInitialized = true;
	} 
	return true;
}


char getFieldCode ( const char *s , int32_t len , bool *hasColon ) {
	// default
	if ( hasColon ) {
		*hasColon = false;
	}

	if ( !initFieldTable() ) {
		return FIELD_GENERIC;
	}

	int64_t h = hash64Lower_a( s, len );
	int32_t i = (int32_t) s_table.getScore(h);

	if ( i == 0 ) {
		return FIELD_GENERIC;
	}

	return g_fields[i-1].field;
}

// guaranteed to be punctuation
bool Query::isConnection(const char *s, int32_t len) const {
	if ( len == 1 ) {
		switch (*s) {
			// . only allow apostrophe if it's NOT a 's
			// . so contractions are ok, and names too
		case '\'': 
			// no, i think we should require it. google seems to,
			// and msn and yahoo do. 'john's room -"john's" gives
			// no result son yahoo and msn.
			return true;
		case ':': return true;
		case '-': return true;
		case '.': return true;
		case '@': return true;
		case '#': return true;
		case '/': return true;
		case '_': return true;
		case '&': return true;
		case '=': return true;
		case '\\': return true;
		default: return false;
		}
	}
	//if ( len == 3 && s[0]==' ' && s[1]=='&' && s[2]==' ' ) return true;
	if ( len == 3 && s[0]==':' && s[1]=='/' && s[2]=='/' ) return true;
	return false;
}


void Query::dumpToLog() const
{
	log(LOG_DEBUG, "Query:setQTerms: dumping %d query-words:", m_numWords);
	for(int i=0; i<m_numWords; i++) {
		const QueryWord &qw = m_qwords[i];
		log("  %d",i);
		log("    word='%*.*s'", (int)qw.m_wordLen, (int)qw.m_wordLen, qw.m_word);
		log("    phrase='%*.*s'", (int)qw.m_phraseLen, (int)qw.m_phraseLen, qw.m_word);
		log("    m_wordId=%" PRId64, qw.m_wordId);
		log("    m_phraseId=%" PRId64, qw.m_phraseId);
	}
	log("Query:setQTerms: dumping %d query-terms:", m_numTerms);
	for(int i=0; i<m_numTerms; i++) {
		const QueryTerm &qt = m_qterms[i];
		log("%d",i);
		log("  m_isPhrase=%s", qt.m_isPhrase?"true":"false");
		log("  m_termId=%" PRId64, qt.m_termId);
		log("  m_rawTermId=%" PRId64, qt.m_rawTermId);
		log("  m_term='%*.*s'", (int)qt.m_termLen, (int)qt.m_termLen, qt.m_term);
		log("  m_isWikiHalfStopBigram=%s", qt.m_isWikiHalfStopBigram?"true":"false");
		log("  m_leftPhraseTermNum=%d, m_leftPhraseTerm=%p", qt.m_leftPhraseTermNum, (void*)qt.m_leftPhraseTerm);
		log("  m_rightPhraseTermNum=%d, m_rightPhraseTerm=%p", qt.m_rightPhraseTermNum, (void*)qt.m_rightPhraseTerm);
	}
}


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////
//////////   ONLY BOOLEAN STUFF BELOW HERE  /////////////
////////////////////////////////////////////////////////
////////////////////////////////////////////////////////

// return false and set g_errno on error
// returns how many words expression was
bool Expression::addExpression (int32_t start, 
				int32_t end, 
				Query *q,
				int32_t              level
				) {

	if ( level >= MAX_EXPRESSIONS ) { 
		g_errno = ETOOMANYPARENS;
		return false;
	}

	// the # of the first alnumpunct word in the expression
	m_expressionStartWord = start;
	m_q = q;

	int32_t i = m_expressionStartWord;

	// "start" is the current alnumpunct word we are parsing out
	for ( ; i<end ; i++ ) {

		QueryWord *qwords = q->m_qwords;

		QueryWord * qw = &qwords[i];

		// set leaf node if not an opcode like "AND" and not punct.
		if ( ! qw->m_opcode && qw->isAlphaWord()){
			continue;
		}
		if (qw->m_opcode == OP_NOT){
			continue;
		}
		else if (qw->m_opcode == OP_LEFTPAREN){
			// this is expression
			// . it should advance "i" to end of expression
			// point to next...
			q->m_numExpressions++;
			// make a new one:
			Expression *e=&q->m_expressions[q->m_numExpressions-1];
			// now set it
			if ( ! e->addExpression ( i+1, // skip over (
						  end ,
						  q ,
						  level + 1)  )
				return false;
			// skip over it. pt to ')'
			i += e->m_numWordsInExpression;
			qw->m_expressionPtr = e;
		}
		else if (qw->m_opcode == OP_RIGHTPAREN){
			// return size i guess, include )
			m_numWordsInExpression = i - m_expressionStartWord+1;
			return true;
		}
		else if (qw->m_opcode) {
			continue;
		}
		// white space?
		continue;
	}

	m_numWordsInExpression = i - m_expressionStartWord;

	return true;
}

// each bit is 1-1 with the explicit terms in the boolean query
bool Query::matchesBoolQuery(const unsigned char *bitVec, int32_t vecSize) const {
	return m_expressions[0].isTruth ( bitVec , vecSize );
}


static bool isBitNumSet(int32_t opBitNum, const unsigned char *bitVec, int32_t vecSize) {
	int32_t byte = opBitNum / 8;
	int32_t mask = 1<<(opBitNum % 8);
	if ( byte >= vecSize ) { g_process.shutdownAbort(true); }
	return bitVec[byte] & mask;
}

// . "bits" are 1-1 with the query words in Query::m_qwords[] array
//   including ignored words and spaces i guess since Expression::add()
//   seems to do that.
bool Expression::isTruth(const unsigned char *bitVec, int32_t vecSize) const {

	//
	// operand1 operand2 operator1 operand3 operator2 ....
	//

	// result: -1 means unknown at this point
	int32_t result = -1;

	char prevOpCode = 0;
	int32_t prevResult ;
	// result of current operand
	int32_t opResult = -1;

	int32_t i    =     m_expressionStartWord;
	int32_t iend = i + m_numWordsInExpression;

	bool hasNot = false;

	for ( ; i < iend ; i++ ) {

		const QueryWord *qw = &m_q->m_qwords[i];

		// ignore parentheses, aren't real opcodes.
		// we just want OP_AND/OP_OR/OP_NOT
		int32_t opcode = qw->m_opcode;
		if ( opcode != OP_AND && 
		     opcode != OP_OR  && 
		     opcode != OP_NOT )
			opcode = 0;

		if ( opcode == OP_NOT ) {
			hasNot = true;
			continue;
		}


		// so operands are expressions as well
		Expression *e = (Expression *)qw->m_expressionPtr;
		if ( e ) {
			// save prev one. -1 means no prev.
			prevResult = opResult;
			// set new onw
			opResult = e->isTruth ( bitVec , vecSize );
			// skip over that expression. point to ')'
			i += e->m_numWordsInExpression;
			// flip?
			if ( hasNot ) {
				if ( opResult == 1 ) opResult = 0;
				else                 opResult = 1;
				hasNot = false;
			}
		}

		if ( opcode && ! e ) {
			prevOpCode = opcode;//m_opSlots[i];
			continue;
		}

		// simple operand
		if ( ! opcode && ! e ) {
			// for regular word operands
			// ignore it like a space?
			if ( qw->m_ignoreWord ) continue;
			// ignore gbsortby:offerprice in bool queries
			// at least for evaluating them
			if ( qw->m_ignoreWordInBoolQuery ) continue;
			// save old one
			prevResult = opResult;
			// convert word to term #
			QueryTerm *qt = qw->m_queryWordTerm;
			// fix title:"notre dame" AND NOT irish
			if ( ! qt ) qt = qw->m_queryPhraseTerm;
			if ( ! qt ) continue;
			// phrase terms are not required and therefore
			// do not have a v alid qt->m_bitNum set, so dont core
			if ( ! qt->m_isRequired ) continue;
			// . m_bitNum is set in Posdb.cpp when it sets its
			//   QueryTermInfo array
			// . it is basically the query term #
			// . see iff that bit is set in this docid's vec
			opResult = isBitNumSet ( qt->m_bitNum,bitVec,vecSize );
			// flip?
			if ( hasNot ) {
				if ( opResult == 1 ) opResult = 0;
				else                 opResult = 1;
				hasNot = false;
			}
		}

		// need two to tango. i.e. (true OR false)
		if ( prevResult == -1 ) continue;

		// if this is not the first time... we got two
		if ( prevOpCode == OP_AND ) {
			// if first operation we encount is A AND B then
			// default result to on. only allow an AND operation
			// to turn if off.
			if ( result == -1 ) result = 1;
			if ( ! prevResult ) result = 0;
			if ( !    opResult ) result = 0;
		}
		else if ( prevOpCode == OP_OR ) {
			// if first operation we encount is A OR B then
			// default result to off
			if ( result == -1 ) result = 0;
			if ( prevResult ) result = 1;
			if (   opResult ) result = 1;
		}
	}

	// if we never set result, then it was probably a single
	// argument expression like something in double parens like
	// ((site:xyz.com OR site:abc.com)). so set it to value of
	// first operand, opResult.
	if ( prevOpCode == 0 && result == -1 ) result = opResult;

	if ( result == -1 ) return true;
	if ( result ==  0 ) return false;
	return true;
}

// if any one query term is split, msg3a has to split the query
bool Query::isSplit() const {
	for(int32_t i = 0; i < m_numTerms; i++) 
		if(m_qterms[i].isSplit()) return true;
	return false;
}

void QueryTerm::constructor ( ) {
	m_qword = NULL;
	m_isPhrase = false;
	m_termId = 0;
	m_rawTermId = 0;
	m_termSign = 0;
	m_explicitBit = 0;
	m_matchesExplicitBits = 0;
	m_hardCount = 0;
	m_bitNum = 0;
	m_term = NULL;
	m_termLen = 0;
	m_posdbListPtr = NULL;
	m_langIdBits = 0;
	m_langIdBitsValid = false;
	m_termFreq = 0;
	m_termFreqWeight = 0.0;
	m_implicitBits = 0;
	m_isQueryStopWord = false;
	m_inQuotes = false;
	m_userWeight = 0;
	m_userType = 0; //?
	m_piped = false;
	m_ignored = false;
	m_isUORed = false;
	m_UORedTerm = NULL;
	m_synonymOf = NULL;
	m_synWids0 = 0;
	m_synWids1 = 0;
	m_numAlnumWordsInSynonym = 1;
	m_fieldCode = 0;
	m_isRequired = false;
	m_isWikiHalfStopBigram = 0;
	m_leftPhraseTermNum = 0;
	m_rightPhraseTermNum = 0;
	m_leftPhraseTerm = NULL;
	m_rightPhraseTerm = NULL;
	memset(m_startKey,0,sizeof(m_startKey));
	memset(m_endKey,0,sizeof(m_endKey));
	m_ks = 0;
}

bool QueryTerm::isSplit() const {
	if(!m_fieldCode) return true;
	if(m_fieldCode == FIELD_QUOTA)           return false;
	if(m_fieldCode == FIELD_GBSECTIONHASH)  return false;
	if(m_fieldCode == FIELD_GBCONTENTHASH)  return false;
	return true;
}

// hash of all the query terms
int64_t Query::getQueryHash() const {
	int64_t qh = 0LL;
	for ( int32_t i = 0 ; i < m_numTerms ; i++ )  {
		const QueryTerm *qt = &m_qterms[i];
		qh = hash64 ( qt->m_termId , qh );
	}
	return qh;
}

void QueryWord::constructor () {
	m_synWordBuf.constructor();
}

void QueryWord::destructor () {
	m_synWordBuf.purge();
}
