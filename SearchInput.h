// Copyright Apr 2005 Matt Wells

// . parameters from CollectionRec that can be overriden on a per query basis

// . use m_qbuf1 for the query. it has all the min/plus/quote1/quote2 advanced
//   search cgi parms appended to it. it is the complete query, which is 
//   probably what you are looking for. m_q should also be set to that, too.
//   m_query is simply the "query=" string in the url request.
// . use m_qbuf2 for the spell checker. it is missing url: link: etc fields.
// . use m_displayQuery for the query displayed in the text box. it is missing
//   values from the "sites=" and "site=" cgi parms, since they show up with
//   radio buttons below the search text box on the web page.

#ifndef _SEARCHINPUT_H_
#define _SEARCHINPUT_H_

#include "Query.h" // MAX_QUERY_LEN

class SearchInput {
public:

	// why provide query here, it is in "hr"
	bool set ( class TcpSocket *s , class HttpRequest *hr );

	void  test    ( );
	key_t makeKey ( ) ;

	bool setQueryBuffers ( class HttpRequest *hr ) ;

	//void setToDefaults ( class CollectionRec *cr , int32_t niceness ) ;
	void clear ( int32_t niceness ) ;

	// Msg40 likes to use this to pass the parms to a remote host
	SearchInput      ( );
	~SearchInput     ( );
	void  reset                 ( );
	void  copy                  ( class SearchInput *si ) ;

	///////////
	//
	// BEGIN COMPUTED THINGS
	//
	///////////

	// we basically steal the original HttpRequest buffer and keep
	// here since the original one is on the stack
	HttpRequest  m_hr;

	TcpSocket   *m_sock;

	int32_t   m_niceness;                   // msg40

	// array of collnum_t's to search... usually just one
	SafeBuf m_collnumBuf;
	// first collection # listed in m_collnumBuf
	collnum_t m_firstCollnum;

	char          *m_displayQuery;     // pts into m_qbuf1

	// urlencoded display query. everything is compiled into this.
	SafeBuf m_qe;

	CollectionRec *m_cr;

	// the final compiled query goes here
	Query          m_q;

	char           m_isMasterAdmin;
	char           m_isCollAdmin;

	// these are set from things above
	SafeBuf m_sbuf1;
	SafeBuf m_sbuf2;


	// we convert m_defaultSortLang to this number, like langEnglish
	// or langFrench or langUnknown...
	int32_t m_queryLangId;

	// can be 1 for FORMAT_HTML, 2 = FORMAT_XML, 3=FORMAT_JSON, 4=csv
	int32_t m_format;

	// used as indicator by SearchInput::makeKey() for generating a
	// key by hashing the parms between m_START and m_END
	int32_t   m_START;


	//////
	//
	// BEGIN USER PARMS set from HttpRequest, m_hr
	//
	//////

	const char *m_coll;
	char *m_query;
	
	char *m_prepend;

	char  m_showImages;

	// general parms, not part of makeKey(), but should be serialized
	char   m_useCache;                   // msg40
	char   m_rcache;                     // msg40
	char   m_wcache;                     // msg40

	char   m_debug;                      // msg40

	char   m_spiderResults;
	char   m_spiderResultRoots;

	char   m_spellCheck;

	char  *m_displayMetas;               // msg40


	// do not include these in makeKey()

	char  *m_queryCharset;

	char  *m_gbcountry;
	uint8_t m_country;

	// advanced query parms
	char  *m_url; // for url: search
	char  *m_sites;
	char  *m_plus;
	char  *m_minus;
	char  *m_link;
	char  *m_quote1;
	char  *m_quote2;

	// co-branding parms
	char  *m_imgUrl;
	char  *m_imgLink;
	int32_t   m_imgWidth;
	int32_t   m_imgHeight;

	// for limiting results by score in the widget
	double    m_maxSerpScore;
	int64_t m_minSerpDocId;

	float m_sameLangWeight;

	// prefer what lang in the results. it gets a 20x boost. "en" "xx" "fr"
	char 	      *m_defaultSortLang;
	// prefer what country in the results. currently unused. support later.
	char 	      *m_defaultSortCountry;

	// general parameters
        char   m_dedupURL;
	int32_t   m_percentSimilarSummary;   // msg40
	char   m_showBanned;
	char   m_excludeLinkText;
	char   m_excludeMetaText;
	char   m_doBotDetection;
	int32_t   m_includeCachedCopy;
	char   m_familyFilter;            // msg40
	char   m_showErrors;
	char   m_doSiteClustering;        // msg40
	char   m_doDupContentRemoval;     // msg40
	char   m_getDocIdScoringInfo;

	char   m_hideAllClustered;

	// ranking algos
	char   m_doMaxScoreAlgo;

	// stream results back on socket in streaming mode, usefule when 
	// thousands of results are requested
	char   m_streamResults;

	// limit search results to pages spidered this many seconds ago
	int32_t   m_secsBack;

	// 0 relevance, 1 date, 2 reverse date
	char   m_sortBy;

	char *m_filetype;

	// unused pqr stuff
	int8_t			m_langHint;
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	uint8_t                 m_countryHint;


	// search result knobs
	int32_t   m_realMaxTop;

	// general parameters
	int32_t   m_numLinesInSummary;           // msg40
	int32_t   m_summaryMaxWidth;             // msg40
	int32_t   m_summaryMaxNumCharsPerLine;

	int32_t   m_docsWanted;                  // msg40
	int32_t   m_firstResultNum;              // msg40
	int32_t   m_boolFlag;                    // msg40
	int32_t   m_numResultsToImport;          // msg40
	float  m_importWeight;
	int32_t   m_numLinkerWeight;
	int32_t   m_minLinkersPerImportedResult; // msg40
	char   m_doQueryHighlighting;         // msg40
	char  *m_highlightQuery;
	Query  m_hqq;
	int32_t   m_queryMatchOffsets;
	int32_t   m_summaryMode;

	// are we doing a QA query for quality assurance consistency
	char   m_qa;

	int32_t   m_docsToScanForReranking;
	float  m_pqr_demFactSubPhrase;
	float  m_pqr_demFactCommonInlinks;
	float  m_pqr_demFactProximity;
	float  m_pqr_demFactOrigScore;
	// . buzz stuff (buzz)
	// . these controls the set of results, so should be in the makeKey()
	//   as it is, in between the start and end hash vars
	int32_t   m_displayInlinks;
	int32_t   m_displayOutlinks;

	int32_t   m_maxQueryTerms;

	// we do not do summary deduping, and other filtering with docids
	// only, so can change the result and should be part of the key
	char   m_docIdsOnly;                 // msg40


	char  *m_formatStr;

	// this should be part of the key because it will affect the results!
	char   m_queryExpansion;

	////////
	//
	// END USER PARMS
	//
	////////

	// . end the section we hash in SearchInput::makeKey()
	// . we also hash displayMetas and Query into the key
	int32_t   m_END_HASH;

	// a marker for SearchInput::test()
	int32_t      m_END_TEST;

};

#endif
