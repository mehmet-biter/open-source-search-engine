// Matt Wells, copyright Nov 2007

// get various information from a query and a docId, like summary, title, etc.

#ifndef GB_MSG20_H
#define GB_MSG20_H

#include "Multicast.h"


class Msg20Request {
 public:

	Msg20Request() { reset(); }

	// zero ourselves out
	void reset() { 
		memset(this,0,sizeof(*this));
		// these are the only non-zero defaults
		m_numSummaryLines    = 1;
		m_docId              = -1LL; // set docid to "invalid"
		m_titleMaxLen        = 80  ;
		m_summaryMaxLen      = 180 ;
	}

	int32_t getStoredSize() const;
	char *serialize(int32_t *sizePtr) const;
	int32_t  deserialize   ( );
	int64_t makeCacheKey() const;

	char       m_numSummaryLines           ; // non-zero default
	bool       m_getHeaderTag              ;
	void      *m_state                     ;
	void      *m_state2                    ; // used by Msg25.cpp
	int32_t       m_j                         ; // used by Msg25.cpp
	bool    (* m_callback)( void *m_state );
	int64_t  m_docId                     ;
	int32_t       m_niceness                  ;
	int32_t       m_titleMaxLen               ;
	int32_t       m_summaryMaxLen             ;
	int32_t       m_summaryMaxNumCharsPerLine ;
	int64_t       m_maxCacheAge               ;
	int32_t       m_discoveryDate             ;

	// special shit so we can remove an inlinker to a related docid
	// if they also link to the main url we are processing seo for.
	// set both of these to 0 to disregard.
	int32_t m_ourHostHash32;
	int32_t m_ourDomHash32;

	// language the query is in (ptr_qbuf)
	uint8_t    m_langId;
	// we now use the numeric collection # and not the ptr_coll
	collnum_t  m_collnum;

	unsigned char       m_queryExpansion            :1;
	unsigned char       m_useQueryStopWords         :1;
	unsigned char       m_highlightQueryTerms       :1;
	unsigned char       m_getSummaryVector          :1;
	unsigned char       m_showBanned                :1;
	unsigned char       m_includeCachedCopy         :1;
	unsigned char       m_doLinkSpamCheck           :1;
	unsigned char       m_isLinkSpam                :1; // Msg25 uses for storage
	unsigned char       m_isSiteLinkInfo            :1; // site link info?
	unsigned char       m_isDebug                   :1;
	// if true, just calls TitleRec::getLinkInfo() to set ptr_linkInfo
	unsigned char       m_getLinkInfo               :1;
	// if this is true we will not compute the title, etc. of BAD inlinks
	// deemed link spam
	unsigned char       m_onlyNeedGoodInlinks       :1;
	// if true, sets ptr_linkText, etc.
	unsigned char       m_getLinkText               :1;

	// pointer+size variable section
	char      *ptr_qbuf          ;
	char      *ptr_ubuf          ; // url buffer
	char      *ptr_linkee        ; // used by Msg25 for getting link text
	char      *ptr_displayMetas  ;

	int32_t       size_qbuf         ;
	int32_t       size_ubuf         ; // url buffer
	int32_t       size_linkee       ; // size includes terminating \0
	int32_t       size_displayMetas ; // size includes terminating \0

	// variable data comes here
};


struct Msg20State;

class Msg20Reply {
public:

	Msg20Reply();
	// free the merge buf from Msg40.cpp merging event summaries
	~Msg20Reply();
	void destructor();

	// zero ourselves out
	void reset() { memset(this,0,sizeof(*this)); }

	// how many bytes if we had to serialize it?
	int32_t getStoredSize() const;

	int32_t  deserialize ( ) ;
	int32_t  serialize(char *buf, int32_t bufSize) const;


	bool  sendReply(Msg20State *state);

	// after calling these, when serialize() is called again it will 
	// exclude these strings which were "cleared". Used by Msg40 to 
	// reduce the memory required for caching the Msg40 which includes an
	// array of Msg20s.
	void clearOutlinks  ( ) { 
		size_linkText = 0;
		size_surroundingText = 0;
		size_outlinks = 0;
	}

	void clearVectors() {
		size_vbuf = 0;
	}

	int32_t       m_ip                  ;
	int32_t       m_firstIp             ;
	int32_t       m_wordPosStart        ;
	int64_t  m_docId               ;
	int32_t       m_firstSpidered       ;
	int32_t       m_lastSpidered        ;
	int32_t       m_lastModified        ;
	int32_t       m_datedbDate          ;
	int32_t       m_firstIndexedDate    ; // for the url/document as a whole
	int32_t       m_discoveryDate       ; // for the inlink in question...
	int32_t       m_errno               ; // LinkInfo uses it for LinkTextRepl
	collnum_t  m_collnum             ; // collection # we came from
	char       m_noArchive           ;
	char       m_contentType         ;
	char       m_siteRank            ;
	bool       m_isBanned            ;
	char       m_hopcount            ;
	char       m_recycled            ;
	uint8_t    m_language            ;
	uint16_t   m_country             ;
	bool       m_isAdult             ;

	int32_t       m_contentLen          ; // was m_docLen
	int32_t       m_contentHash32       ;  // for deduping diffbot json objects streaming
	int32_t       m_pageNumInlinks      ;
	int32_t       m_pageNumGoodInlinks  ;
	int32_t       m_pageNumUniqueIps    ; // includes our own inlinks
	int32_t       m_pageNumUniqueCBlocks; // includes our own inlinks
	int32_t       m_pageInlinksLastUpdated;
	
	int32_t       m_siteNumInlinks      ; // GOOD inlinks!

	int32_t       m_numOutlinks         ; // replaced m_linkCount

	// these are just storage for LinkInfo::set() to use
	int32_t       m_linkTextNumWords    ;

	int32_t       m_midDomHash          ; // set for m_getLinkText

	char       m_isLinkSpam          ; // set for m_getLinkText
	char       m_outlinkInContent    ; // set for m_getLinkText
	char       m_outlinkInComment    ; // set for m_getLinkText
	char       m_isPermalink         ; // set for m_getLinkText (buzz)

	bool m_isDisplaySumSetFromTags;
	
	// pointer+size variable section
	char       *ptr_tbuf                 ; // title buffer
	char       *ptr_htag                 ; // h1 tag buf
	char       *ptr_ubuf                 ; // url buffer
	char       *ptr_rubuf                ; // redirect url buffer
	char       *ptr_displaySum           ; // summary for displaying
	char       *ptr_dbuf                 ; // display metas \0 separated
	char       *ptr_vbuf                 ; // summary vector
	char       *ptr_imgData              ; // for encoded images
	char       *ptr_site                 ;

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
	const char       *ptr_categories           ;
	char       *ptr_content              ; // page content in utf8
	char       *ptr_templateVector       ;
	char       *ptr_metadataBuf;

	const char       *ptr_note           ; // reason why it cannot vote

	int32_t       size_tbuf;
	int32_t       size_htag;
	int32_t       size_ubuf;
	int32_t       size_rubuf;
	int32_t       size_displaySum;
	int32_t       size_dbuf;
	int32_t       size_vbuf;
	int32_t       size_imgData;
	int32_t       size_site;
	int32_t       size_linkInfo;
	int32_t       size_outlinks;
	int32_t       size_vector1;
	int32_t       size_vector2;
	int32_t       size_vector3;
	int32_t       size_linkText;
	int32_t       size_surroundingText;
	int32_t       size_linkUrl;
	int32_t       size_rssItem;
	int32_t       size_categories;
	int32_t       size_content; // page content in utf8
	int32_t       size_templateVector;
	int32_t       size_metadataBuf;
	int32_t       size_note;

	// variable data comes here
};

class Msg20 {
public:

	// . this should only be called once
	// . should also register our get record handlers with the udpServer
	static bool registerHandler();

	// see definition of Msg20Request below
	bool getSummary ( class Msg20Request *r );

	// this is cast to m_replyPtr
	Msg20Reply *m_r ;
	int32_t   m_replySize;
	int32_t   m_replyMaxSize;

	// i guess Msg40.cpp looks at this flag
	bool m_gotReply;

	// set if we had an error
	int32_t   m_errno;

	int64_t getRequestDocId () const { return m_requestDocId; }

	int32_t getStoredSize() const {
		if ( ! m_r ) return 0; 
		return m_r->getStoredSize();
	}

	// . return how many bytes we serialize into "buf"
	// . sets g_errno and returns -1 on error
	int32_t serialize ( char *buf , int32_t bufSize ) {
		if ( ! m_r ) return 0;
		return m_r->serialize ( buf , bufSize );
	}

	// . this is destructive on the "buf". it converts offs to ptrs
	// . sets m_r to the modified "buf" when done
	// . sets g_errno and returns -1 on error, otherwise # of bytes deseril
	int32_t deserialize ( char *buf , int32_t bufSize ) ;

	// Msg40 caches each Msg20Reply when it caches the page of results, so,
	// to keep the size of the cached Msg40 down, we do not cache certain
	// things. so we have to "clear" these guys out before caching.
	void clearLinks     () { if ( m_r ) m_r->clearOutlinks (); }
	void clearVectors   () { if ( m_r ) m_r->clearVectors  (); }
	// copy "src" to ourselves
	void moveFrom(Msg20 *src);

	void gotReply ( class UdpSlot *slot );

	// general purpose routines
	Msg20();
	~Msg20();
	// so we can alloc arrays of these using mmalloc()
	void constructor ();
	void destructor  ();
	void freeReply   ();
	void reset       ();

	int32_t m_ii;

	// is the reply in progress? if msg20 has not launched a request
	// this is false. if msg20 received its reply, this is false. 
	// otherwise this is true.
	bool m_inProgress;
	bool m_launched;

private:
	char  *m_request;
	int32_t   m_requestSize;

	int64_t m_requestDocId;

	// for sending the request
	Multicast m_mcast;
	
	bool	m_ownReply;

	bool     (*m_callback ) ( void *state );
	void     (*m_callback2) ( void *state );
	void      *m_state;

	static void gotReplyWrapper20(void *state, void *state20);
};

#endif // GB_MSG20_H
