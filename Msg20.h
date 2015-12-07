// Matt Wells, copyright Nov 2007

// get various information from a query and a docId, like summary, title, etc.

#ifndef _MSG20_H_
#define _MSG20_H_

#include "UdpServer.h"
#include "Hostdb.h"
#include "Multicast.h"
#include "Xml.h"
#include "Summary.h"
#include "Titledb.h"
#include "Query.h"
//#include "LinkInfo.h"
#include "Tagdb.h" // TagRec

// values for SummaryLine::m_flags
//#define SL_TRUNCATED 0x0100
//#define SL_IS_TITLE  0x0080
//#define SL_HAS_DATE  0x0040
//#define SL_HAS_QTERM 0x0020
//#define SL_BULLET    0x0001

#define MSG20_CURRENT_VERSION 0


class Msg20Request {
 public:

	Msg20Request() { reset(); };

	// zero ourselves out
	void reset() { 
		memset ( (char *)this,0,sizeof(Msg20Request) ); 
		// these are the only non-zero defaults
		m_version            = MSG20_CURRENT_VERSION;
		m_maxNumCharsPerLine = 50;
		m_numSummaryLines    = 2;
		m_expected           = false;
		m_docId              = -1LL; // set docid to "invalid"
		m_boolFlag           = 2   ; // autodetect if query boolean
		m_titleMaxLen        = 64  ;
		m_summaryMaxLen      = 512 ;
		// reset ptr sizes
		int32_t size = m_buf - (char *)&size_qbuf;
		memset ( &size_qbuf , 0 , size );
	};

	int32_t  getStoredSize ( );
	char *serialize     ( int32_t *sizePtr );
	int32_t  deserialize   ( );

	char       m_version                   ; // non-zero default
	char       m_numSummaryLines           ; // non-zero default
	char       m_expected                  ; // non-zero default
	bool       m_getHeaderTag              ;
	void      *m_state                     ;
	void      *m_state2                    ; // used by Msg25.cpp
	int32_t       m_j                         ; // used by Msg25.cpp
	bool    (* m_callback)( void *m_state );
	void    (* m_callback2)( void *m_state );
	int64_t  m_docId                     ;
	int32_t       m_niceness                  ;
	char       m_boolFlag                  ;
	int32_t       m_titleMaxLen               ;
	int32_t       m_summaryMaxLen             ;
	int32_t       m_summaryMaxNumCharsPerLine ;
	int32_t       m_maxNumCharsPerLine        ;
	int32_t       m_bigSampleRadius           ;
	int32_t       m_bigSampleMaxLen           ;
	int32_t       m_maxCacheAge               ;
	int32_t       m_maxLinks                  ;
	int32_t       m_discoveryDate             ;

	// special shit so we can remove an inlinker to a related docid
	// if they also link to the main url we are processing seo for.
	// set both of these to 0 to disregard.
	int32_t m_ourHostHash32;
	int32_t m_ourDomHash32;
	
	FacetValHash_t m_facetValHash;

	unsigned char       m_justGetFacets : 1         ;

	// for sending msg20 request to another network
	//int32_t       m_hostIP;
	//int32_t       m_hostUDPPort;

	// if titleRec not from this ruleset, return g_errno = EDOCFILTERED
	//int32_t       m_rulesetFilter             ;
	// language the query is in (ptr_qbuf)
	uint8_t    m_langId;
	// we now use the numeric collection # and not the ptr_coll
	collnum_t  m_collnum;

	unsigned char       m_highlightQueryTerms       :1;
	unsigned char       m_highlightDates            :1; // for event dates
	unsigned char       m_wcache                    :1;
	//unsigned char     m_checkSitedb               :1;
	unsigned char       m_getImageUrl               :1;
	unsigned char       m_ratInSummary              :1;
	unsigned char       m_countOutlinks             :1;
	unsigned char       m_considerTitlesFromBody    :1;
	unsigned char       m_getSummaryVector          :1;
	unsigned char       m_showBanned                :1;
	//unsigned char       m_excludeLinkText           :1;
	//unsigned char       m_excludeMetaText           :1;
	//unsigned char       m_hackFixWords              :1;
	//unsigned char       m_hackFixPhrases            :1;
	unsigned char       m_includeCachedCopy         :1;
	unsigned char       m_getSectionVotingInfo      :1; // in JSON for now
	unsigned char       m_getMatches                :1;
	unsigned char       m_useLinkdbForInlinks       :1;
	unsigned char       m_getTermListBuf            :1;
	//unsigned char     m_getInlinks                :1; // use m_getLinkInfo!
	unsigned char       m_getOutlinks               :1;
	unsigned char       m_getTitleRec               :1; // sets ptr_tr in reply
	unsigned char       m_maxInlinks                :1;
	unsigned char       m_getGigabitVector          :1;
	unsigned char       m_doLinkSpamCheck           :1;
	unsigned char       m_isLinkSpam                :1; // Msg25 uses for storage
	unsigned char       m_isSiteLinkInfo            :1; // site link info?
	unsigned char       m_isDebug                   :1;
	// if true, calls Msg25 and fills in ptr_linkInfo/size_linkInfo
	unsigned char       m_computeLinkInfo           :1;
	// if true, just calls TitleRec::getLinkInfo() to set ptr_linkInfo
	unsigned char       m_getLinkInfo               :1;
	// if this is true we will not compute the title, etc. of BAD inlinks
	// deemed link spam
	unsigned char       m_onlyNeedGoodInlinks       :1;
	// if true, sets ptr_linkText, etc.
	unsigned char       m_getLinkText               :1;
	// if this is true then we set ptr_turkForm to be an input form
	// for turking this event summary and title
	unsigned char       m_getTurkForm               :1;
	unsigned char       m_showTurkInstructions      :1;
	unsigned char       m_isTurkSpecialQuery        :1;
	unsigned char       m_isMasterAdmin                   :1;
	// . this is for buzz.
	// . this says to compute the <absScore2> tag in their xml feed.
	// . the document receives a score of 0 if it does not match the query
	// . can we just keep it a binary score? let's try that. 
	unsigned char       m_checkForQueryMatch        :1;

	// serialize() converts these ptrs into offsets in m_buf[]
	// and deserialize() converts them back into ptrs on the receiver's end
	char      *ptr_qbuf          ;
	char      *ptr_hqbuf         ;
	//char      *ptr_q2buf         ;
	char      *ptr_turkUser      ;
	char      *ptr_ubuf          ; // url buffer
	char      *ptr_rubuf         ; // redirect url buffer
	char      *ptr_termFreqs     ;
	char      *ptr_affWeights    ;
	char      *ptr_linkee        ; // used by Msg25 for getting link text
	//char      *ptr_coll          ;
	//char      *ptr_imgUrl        ;
	char      *ptr_displayMetas  ;

	// . from here down: automatically set in Msg20Request::serialize() 
	//   from the above parms
	// . add new size_* parms after size_qbuf and before size_displayMetas
	//   so that serialize()/deserialize() still work
	int32_t       size_qbuf         ;
	int32_t       size_hqbuf        ;
	//int32_t       size_q2buf        ;
	int32_t       size_turkUser     ;
	int32_t       size_ubuf         ; // url buffer
	int32_t       size_rubuf        ; // redirect url buffer
	int32_t       size_termFreqs    ;
	int32_t       size_affWeights   ;
	int32_t       size_linkee       ; // size includes terminating \0
	//int32_t       size_coll         ; // size includes terminating \0
	//int32_t       size_imgUrl       ;
	int32_t       size_displayMetas ; // size includes terminating \0

	char       m_buf[0] ;
};

class Msg20Reply {
public:

	Msg20Reply();
	// free the merge buf from Msg40.cpp merging event summaries
	~Msg20Reply();
	void destructor();

	// zero ourselves out
	void reset() { memset ( (char *)this,0,sizeof(Msg20Reply) ); };

	// how many bytes if we had to serialize it?
	int32_t getStoredSize() ;

	int32_t  deserialize ( ) ;
	int32_t  serialize ( char *buf , int32_t bufSize );


	bool  sendReply ( class XmlDoc *xd ) ;

	// after calling these, when serialize() is called again it will 
	// exclude these strings which were "cleared". Used by Msg40 to 
	// reduce the memory required for caching the Msg40 which includes an
	// array of Msg20s.
	//void clearBigSample ( ) { size_sbuf = 0; };
	void clearOutlinks  ( ) { 
		size_obuf = 0; 
		size_linkText = 0;
		size_surroundingText = 0;
		//size_linkInfo = 0;
		size_outlinks = 0;
	};
	void clearVectors   ( ) { size_vbuf = 0; };

	// a new one for getting the display contents sequentially used 
	// by Msg24.cpp. this routine is the exclusive user of the "next"
	// variable which must be set to "ptr_dbuf" when first called.
	char *getNextDisplayBuf ( int32_t *len , char **next ) { 
		if ( ! *next                       ) return NULL;
		if ( *next >= (char *)ptr_dbuf + size_dbuf ) return NULL;
		char *s = *next;
		*len = gbstrlen(*next);
		*next += *len + 1;
		return s;
	};

	char       m_version             ;
	int32_t       m_ip                  ;
	int32_t       m_firstIp             ;
	int32_t       m_wordPosStart        ;
	int64_t  m_domHash             ;
	int64_t  m_docId               ;
	int32_t       m_firstSpidered       ;
	int32_t       m_lastSpidered        ;
	int32_t       m_lastModified        ;
	int32_t       m_datedbDate          ;
	int32_t       m_firstIndexedDate    ; // for the url/document as a whole
	int32_t       m_discoveryDate       ; // for the inlink in question...
	int32_t       m_errno               ; // LinkInfo uses it for LinkTextRepl
	collnum_t  m_collnum             ; // collection # we came from
	char       m_sumFromDmoz         ; // unused
	char       m_noArchive           ;
	char       m_contentType         ;
	//char       m_docQuality          ;
	char       m_siteRank            ;
	char       m_isBanned            ;
	char       m_isFiltered          ;
	char       m_eventExpired        ;
	char       m_hasLinkToOurDomOrHost;
	char       m_urlFilterNum        ;
	char       m_hopcount            ;
	//char       m_flags3              ;
	char       m_recycled            ;
	uint8_t    m_language            ;
	uint8_t    m_summaryLanguage     ;
	uint16_t   m_country             ;
	uint16_t   m_computedCountry     ;
	int16_t      m_charset             ;
	// for use by caller
	class Msg20Reply *m_nextMerged   ;
	//int32_t     m_numCatIds           ; // use size_catIds
	//int32_t     m_numIndCatIds        ; // use size_indCatIds
	int32_t       m_contentLen          ; // was m_docLen
	int32_t       m_contentHash32       ;  // for deduping diffbot json objects streaming
	//int32_t     m_docSummaryScore     ;
	//int32_t     m_inSectionScore      ;
	//float      m_proximityScore      ;
	//int32_t       m_ruleset             ;
	int32_t       m_pageNumInlinks      ;
	int32_t       m_pageNumGoodInlinks  ;
	int32_t       m_pageNumUniqueIps    ; // includes our own inlinks
	int32_t       m_pageNumUniqueCBlocks; // includes our own inlinks
	int32_t       m_pageInlinksLastUpdated;
	
	int32_t       m_siteNumInlinks      ; // GOOD inlinks!
	//int32_t       m_siteNumInlinksTotal ; // TOTAL inlinks
	//int32_t       m_siteNumUniqueIps    ;
	//int32_t       m_siteNumUniqueCBlocks;

	int32_t       m_numOutlinks         ; // replaced m_linkCount

	// these are just storage for LinkInfo::set() to use
	//int32_t       m_linkTextScoreWeight ;
	int32_t       m_linkTextNumWords    ;
	//int32_t       m_linkTextLinkerQualityBoost ;
	//int32_t       m_linkTextNumWordsBoost    ;
	//int32_t       m_linkTextBaseScore   ;
	char      *m_linkTextNote        ;

	//int32_t       m_pagePop             ; // set for m_computeLinkInfo
	//int32_t     m_siteRootPagePop     ; // set for m_computeLinkInfo
	//int32_t     m_siteRootNumInlinks  ; // set for m_computeLinkInfo
	//int32_t       m_sitePop             ; // set for m_computeLinkInfo
	int32_t       m_midDomHash          ; // set for m_getLinkText
	int32_t       m_adIdHash            ; // set for m_getLinkText
	int32_t       m_timeLinkSpam        ; // set for m_getLinkText
	void         *m_parentOwner;
	char          m_constructorId;

	char       m_inlinkWeight        ; // set for m_getLinkText
	char       m_isLinkSpam          ; // set for m_getLinkText
	char       m_isAnomaly           ; // set for m_getLinkText
	char       m_outlinkInContent    ; // set for m_getLinkText
	char       m_outlinkInComment    ; // set for m_getLinkText
	char       m_hasAllQueryTerms    ; // set for m_getLinkText (buzz)
	char       m_isPermalink         ; // set for m_getLinkText (buzz)
	
	// . serialize() converts these ptrs into offsets in m_buf[] and
	//   deserialize() converts them back into ptrs on the receiver's end
	// . note: there must be an associated size_* for each ptr_* in the
	//   same relative position to the members surrounding it
	// . if a ptr_* is added above ptr_tbuf or underneath 
	//   ptr_outlinkRulesets, then the serialize() and deserialize() 
	//   methods must be changed
	// . also, all ptr_* should be char* and all size_* should be in bytes
	char       *ptr_tbuf                 ; // title buffer
	char       *ptr_htag                 ; // h1 tag buf
	char       *ptr_ubuf                 ; // url buffer
	char       *ptr_rubuf                ; // redirect url buffer
	char       *ptr_displaySum           ; // summary for displaying
	char       *ptr_dedupSum             ; // summary for deduping
	char       *ptr_dbuf                 ; // display metas \0 separated
	//char     *ptr_sbuf                 ; // big sample buf for gigabits
	char       *ptr_gigabitSample        ;
	char       *ptr_obuf                 ; // outlinks buf, \0 separated
	char       *ptr_mbuf                 ; // match offsets
	char       *ptr_vbuf                 ; // summary vector
	char       *ptr_tvbuf                ; // title vector
	char       *ptr_gbvecbuf             ; // gigabit vector
	char       *ptr_imgUrl               ; // youtube/metacafe vid thumb
	char       *ptr_imgData              ; // for encoded images
	char       *ptr_facetBuf             ;
	//char       *ptr_eventEnglishTime     ; // "every saturday [[]] jan"
	//char       *ptr_eventDateIntervals   ;
	char       *ptr_likedbList           ;

	char       *ptr_matchedQueryWords    ;
	char       *ptr_numMatchedQueryWords ;
	char       *ptr_matchedTypes         ;

	int32_t       *ptr_catIds               ;
	int32_t       *ptr_indCatIds            ;
	//char     *ptr_dmozTitleLens        ;
	char       *ptr_dmozTitles           ;
	//char     *ptr_dmozSummLens         ;
	char       *ptr_dmozSumms            ;
	//char     *ptr_dmozAnchorLens       ;
	char       *ptr_dmozAnchors          ;
	//char     *ptr_tagRec               ;
	char       *ptr_site                 ;
	char       *ptr_gbAdIds              ;
	char       *ptr_summLocs             ;
	char       *ptr_summLocsPops         ;

	// . if m_computeLinkInfo is true this is computed using Msg25 (fresh)
	// . if m_setLinkInfo is true this is just set from the titleRec
	// . this is a serialized LinkInfo class
	char       *ptr_linkInfo; // inlinks              ;
	// . made using LinkInfo::set ( Msg20Reply **ptrs )
	// . this is a serialized LinkInfo class
	char       *ptr_outlinks             ;

	// . these are used only by Msg25 to compute LinkInfo
	// . Msg25 will call Msg20 on the docid of a potentially good inlinker
	//   instead of calling the now obsolete Msg23::getLinkText()
	int32_t       *ptr_vector1              ; // set for m_getLinkText
	int32_t       *ptr_vector2              ; // set for m_getLinkText
	int32_t       *ptr_vector3              ; // set for m_getLinkText
	char       *ptr_linkText             ; // set for m_getLinkText
	char       *ptr_surroundingText      ; // set for m_getLinkText
	char       *ptr_linkUrl              ; // what we link to
	char       *ptr_rssItem              ; // set for m_getLinkText
	char       *ptr_categories           ;
	char       *ptr_gigabitQuery         ; // , separated list of gigabits
	int32_t    *ptr_gigabitScores        ; // 1-1 with the terms in query
	char       *ptr_content              ; // page content in utf8
	char       *ptr_sectionVotingInfo    ; // in JSON
	char       *ptr_tr                   ; // like just using msg22
	char       *ptr_tlistBuf             ;
	char       *ptr_tiBuf                ; // terminfobuf
	char       *ptr_templateVector       ;
	char       *ptr_metadataBuf;

	// . for eventIds include the title and text of the event, and the addr
	//   serialized using Address::serialize(), and all the start dates
	//   from now onward
	// . contains serialized EventReply classes
	// . usually just one, but if multiple events that had different
	//   addresses from this same docid matched the query, then we will 
	//   have multiple EventReply classes in this buf
	//char       *ptr_eventSummaryLines    ;
	//char       *ptr_eventAddr            ;
	//char       *ptr_eventTagsFromContent ;
	//char       *ptr_eventTagsFromTagdb   ;
	//char       *ptr_eventBestPlaceName   ;

	// . if Msg20Request::m_forTurk is true then the ptr_turkForm will
	//   be a little input form that lists every line in the title and
	//   description of the event along with controls that allow the turk
	//   to turn descriptions on/off and pick different titles.
	// . when they submit their changes then it should basically add
	//   the turk tag hashes of each line to tagdb, but only if changed
	//   by the turk.
	// . i guess it should submit directly to tagdb...
	// . then we should do a query reindex on all docs with that 
	//   tagformathash
	//char       *ptr_turkForm;

	char       *ptr_note                 ; // reason why it cannot vote

	// . add new size_* parms after size_tbuf and before
	//   size_outlinkRulesets
	//   so that serialize()/deserialize() still work
	// . string sizes of the strings we store into m_buf[]
	// . wordCountBuf is an exact word count 1-1 with each "range"
	int32_t       size_tbuf                 ;
	int32_t       size_htag                 ;
	int32_t       size_ubuf                 ;
	int32_t       size_rubuf                ;
	int32_t       size_displaySum           ;
	int32_t       size_dedupSum             ;
	int32_t       size_dbuf                 ;
	//int32_t     size_sbuf                 ;
	int32_t       size_gigabitSample        ; // includes \0
	int32_t       size_obuf                 ;
	int32_t       size_mbuf                 ;
	int32_t       size_vbuf                 ;
	int32_t       size_tvbuf                ;
	int32_t       size_gbvecbuf             ;
	int32_t       size_imgUrl               ; // youtube/metacafe vid thumb
	int32_t       size_imgData              ;
	int32_t       size_facetBuf             ;
	//int32_t       size_eventEnglishTime     ;
	//int32_t       size_eventDateIntervals   ;
	int32_t       size_likedbList           ;

	int32_t       size_matchedQueryWords    ;
	int32_t       size_numMatchedQueryWords ;
	int32_t       size_matchedTypes         ;

	int32_t       size_catIds               ;
	int32_t       size_indCatIds            ;
	//int32_t     size_dmozTitleLens        ;
	int32_t       size_dmozTitles           ;
	//int32_t     size_dmozSummLens         ;
	int32_t       size_dmozSumms            ;
	//int32_t     size_dmozAnchorLens       ;
	int32_t       size_dmozAnchors          ;
	//int32_t     size_tagRec               ;
	int32_t       size_site                 ;
	int32_t       size_gbAdIds              ;
	int32_t       size_summLocs             ;
	int32_t       size_summLocsPops         ;

	int32_t       size_linkInfo;//inlinks              ;
	int32_t       size_outlinks             ;

	int32_t       size_vector1              ;
	int32_t       size_vector2              ;
	int32_t       size_vector3              ;
	int32_t       size_linkText             ;
	int32_t       size_surroundingText      ;
	int32_t       size_linkUrl              ;
	int32_t       size_rssItem              ;
	int32_t       size_categories           ;
	int32_t       size_gigabitQuery         ;
	int32_t       size_gigabitScores        ;
	int32_t       size_content              ; // page content in utf8
	int32_t       size_sectionVotingInfo    ; // in json, includes \0
	int32_t       size_tr                   ;
	int32_t       size_tlistBuf             ;
	int32_t       size_tiBuf                ;
	int32_t       size_templateVector       ;
	int32_t       size_metadataBuf          ;

	//int32_t       size_eventSummaryLines    ;
	//int32_t       size_eventAddr            ;
	//int32_t       size_eventTagsFromContent ;
	//int32_t       size_eventTagsFromTagdb   ;
	//int32_t       size_eventBestPlaceName   ;

	//int32_t       size_turkForm             ;

	// CAUTION: do not add any parms below size_note!!!
	int32_t       size_note                 ;

	// . this is the "string buffer" and it is a variable size
	// . this whole class is cast to a udp reply, so the size of "buf"
	//   depends on the size of that udp reply
	char       m_buf[0];

	int32_t      getNumCatIds    (){return size_catIds/4; };
	int32_t      getNumIndCatIds (){return size_indCatIds/4; };
	int32_t      getCatId        (int32_t i){return ((int32_t *)ptr_catIds)[i]; };
	int32_t      getIndCatId     (int32_t i){return ((int32_t *)ptr_indCatIds)[i];};

	//int32_t      getDmozTitleLen    (int32_t i){
	//	return ((int32_t *)ptr_dmozTitleLens)[i];};
	//int32_t      getDmozSummLen     (int32_t i){
	//	return ((int32_t *)ptr_dmozSummLens)[i]; };
	//int32_t      getDmozAnchorLen   (int32_t i){
	//	return (int32_t)((uint8_t *)ptr_dmozAnchorLens)[i];};
	//int32_t   *getCatIds       (){return (int32_t *)ptr_catIds; };
	//int32_t   *getIndCatIds    (){return (int32_t *)ptr_indCatIds; };
	//int32_t   *getTitleLens    (){return (int32_t *)ptr_dmozTitleLens;};
	//int32_t   *getSummLens     (){return (int32_t *)ptr_dmozSummLens; };
	//uint8_t*getAnchorLens   (){return(uint8_t *)ptr_dmozAnchorLens;};
};

class Msg20 {

 public:

	// . this should only be called once
	// . should also register our get record handlers with the udpServer
	bool registerHandler ( );

	// see definition of Msg20Request below
	bool getSummary ( class Msg20Request *r );

	// "m_request = r->serialize(&m_requestSize,m_requestBuf)"
	char  *m_request;
	int32_t   m_requestSize;

	// this is cast to m_replyPtr
	Msg20Reply *m_r ;
	// m_replyPtr pts to either m_replyBuf or to mem allocated from the
	// udp server to hold the reply.
	//char  *m_replyPtr;
	int32_t   m_replySize;
	int32_t   m_replyMaxSize;
	// i guess Msg40.cpp looks at this flag
	char   m_gotReply;
	// set if we had an error
	int32_t   m_errno;

	int64_t getRequestDocId () { return m_requestDocId; };
	int64_t m_requestDocId;

	int32_t getStoredSize ( ) { 
		if ( ! m_r ) return 0; 
		return m_r->getStoredSize(); };
	// . return how many bytes we serialize into "buf"
	// . sets g_errno and returns -1 on error
	int32_t serialize ( char *buf , int32_t bufSize ) {
		if ( ! m_r ) return 0;
		return m_r->serialize ( buf , bufSize ); };
	// . this is destructive on the "buf". it converts offs to ptrs
	// . sets m_r to the modified "buf" when done
	// . sets g_errno and returns -1 on error, otherwise # of bytes deseril
	int32_t deserialize ( char *buf , int32_t bufSize ) ;
	// Msg40 caches each Msg20Reply when it caches the page of results, so,
	// to keep the size of the cached Msg40 down, we do not cache certain
	// things. so we have to "clear" these guys out before caching.
	//void clearBigSample () { m_r->clearBigSample(); };
	void clearOutlinks  () { if ( m_r ) m_r->clearOutlinks (); };
	void clearLinks     () { if ( m_r ) m_r->clearOutlinks (); };
	void clearVectors   () { if ( m_r ) m_r->clearVectors  (); };
	// copy "src" to ourselves
	void copyFrom ( class Msg20 *src ) ;

	// inlinker information, used by PostQueryRerank.cpp
	//class LinkInfo *getInlinks  () { 
	//	return (class LinkInfo *)m_r->ptr_inlinks ; };
	//class LinkInfo *getOutlinks () { 
	//	return (class LinkInfo *)m_r->ptr_outlinks; };

	// just let caller parse it up
	Msg20Reply *getReply () { return m_r; };

	static int32_t getApproxLinkCount(char* content, int32_t contentLen);
	//static int32_t getLinkHashes(Links& ln, char* buf, int32_t bufSize);

	//char *getNextDisplayBuf ( int32_t *len , char **next ) { 
	//	return m_r->getNextDisplayBuf(len,next); };

	// for sending the request
	Multicast m_mcast;

	void gotReply ( class UdpSlot *slot );

	// general purpose routines
	Msg20();
	~Msg20();
	// so we can alloc arrays of these using mmalloc()
	void constructor ();
	void destructor  ();
	void freeReply   ();
	void reset       ();

	void *m_hack;
	int32_t m_hack2;
	int32_t m_ii;

	// is the reply in progress? if msg20 has not launched a request
	// this is false. if msg20 received its reply, this is false. 
	// otherwise this is true.
	bool m_inProgress;
	bool m_launched;

	char       m_ownReply;
	char       m_expected;

	bool     (*m_callback ) ( void *state );
	void     (*m_callback2) ( void *state );
	void      *m_state;

	// used by MsgE to store its data
	void *m_state2;
	void *m_state3;

	void *m_owningParent;

	// PostQueryRerank storage area for printing out in PageResults.cpp
	float m_pqr_old_score        ;
	float m_pqr_factor_diversity ;
	float m_pqr_factor_quality   ;
	float m_pqr_factor_inlinkers ;
	float m_pqr_factor_proximity ;
	float m_pqr_factor_ctype     ;
	float m_pqr_factor_lang      ; // includes country
};

#endif

