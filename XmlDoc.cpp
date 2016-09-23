#include "gb-include.h"
#include "hash.h"
#include "XmlDoc.h"
#include "Conf.h"
#include "Query.h"     // getFieldCode()
#include "Clusterdb.h" // g_clusterdb
#include "iana_charset.h"
#include "Stats.h"
#include "Sanity.h"
#include "Speller.h"
#include "CountryCode.h"
#include "linkspam.h"
#include "Tagdb.h"
#include "Repair.h"
#include "HashTableX.h"
#include "LanguageIdentifier.h" // g_langId
#include "CountryCode.h" // g_countryCode
#include "sort.h"
#include "Wiki.h"
#include "Speller.h"
#include "SiteGetter.h"
#include "Synonyms.h"
#include "PageInject.h"
#include "HttpServer.h"
#include "Posdb.h"
#include "Highlight.h"
#include "Wiktionary.h"
#include "PingServer.h"
#include "Parms.h"
#include "Domains.h"
#include "AdultCheck.h"
#include "Doledb.h"
#include "IPAddressChecks.h"
#include "PageRoot.h"
#include "BitOperations.h"
#include "Robots.h"
#include <pthread.h>
#include "JobScheduler.h"
#include "Process.h"
#include "Statistics.h"


#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif


#define SENT_UNITS 30

#define NUMTERMIDBITS 48 // was in RdbList but only used in XmlDoc

static void getWordToPhraseRatioWeights ( int64_t   pid1 , // pre phrase
					  int64_t   wid1 ,
					  int64_t   pid2 ,
					  int64_t   wid2 , // post word
					  float      *ww   ,
					  const HashTableX *tt1);

static void getMetaListWrapper ( void *state ) ;


#if 0
static void doneReadingArchiveFileWrapper ( int fd, void *state );
#endif

XmlDoc::XmlDoc() {
	//clear all fields in the titledb structure (which are the first fileds in this class)
	memset(&m_headerSize, 0, (size_t)((char*)&ptr_firstUrl-(char*)&m_headerSize));
	
	m_esbuf.setLabel("exputfbuf");
	m_freed = false;
	m_contentInjected = false;
	m_wasContentInjected = false;

	// warc parsing stuff

	//m_coll  = NULL;
	m_ubuf = NULL;
	m_pbuf = NULL;
	m_rootDoc    = NULL;
	m_oldDoc     = NULL;
	m_printedMenu = false;
	// reset all *valid* flags to false
	void *p    = &m_VALIDSTART;
	void *pend = &m_VALIDEND;
	memset ( p , 0 , (char *)pend - (char *)p );//(int32_t)pend-(int32_t)p
	m_msg22Request.m_inUse = 0;
	m_msg4Waiting = false;
	m_msg4Launched = false;
	m_dupTrPtr = NULL;
	m_oldTitleRec = NULL;
	m_filteredContent = NULL;
	m_filteredContentAllocSize = 0;
	m_metaList = NULL;
	m_metaListSize = 0;
	m_metaListAllocSize = 0;
	m_rootTitleRec = NULL;
	m_isIndexed = false;
	m_isInIndex = false;
	m_wasInIndex = false;
	m_outlinkHopCountVector = NULL;
	m_extraDoc = NULL;
	m_statusMsg = NULL;

	reset();
}

XmlDoc::~XmlDoc() {
	setStatus("freeing this xmldoc");
	reset();
	m_freed = true;
}

static int64_t s_lastTimeStart = 0LL;


void XmlDoc::reset ( ) {
	m_redirUrl.reset();

	m_updatedMetaData = false;

	m_ipStartTime = 0;
	m_ipEndTime   = 0;

	m_isImporting = false;

	m_printedMenu = false;

	m_bodyStartPos = 0;

	m_indexedTime = 0;

	m_metaList2.purge();

	m_mySiteLinkInfoBuf.purge();
	m_myPageLinkInfoBuf.purge();

	// we need to reset this to false
	m_useTimeAxis = false;

	m_loaded = false;

	m_msg4Launched = false;

	//m_downloadAttempted = false;
	m_incrementedAttemptsCount = false;
	m_incrementedDownloadCount = false;

	m_fakeIpBuf.purge();
	m_fakeTagRecPtrBuf.purge();

	m_doConsistencyTesting = g_conf.m_doConsistencyTesting;

	m_computedMetaListCheckSum = false;

	m_allHashed = false;

	m_doledbKey.n0 = 0LL;
	m_doledbKey.n1 = 0;

	m_wordSpamBuf.purge();
	m_fragBuf.purge();

	s_lastTimeStart = 0LL;

	m_req = NULL;

	m_storeTermListInfo = false;

	// for limiting # of iframe tag expansions
	m_numExpansions = 0;

	// . are not allowed to exit if waiting for msg4 to complete
	// . yes we are, it should be saved as addsinprogress.dat
	if ( m_msg4Waiting ) {
		if(m_docIdValid)
			log("doc: resetting xmldoc with outstanding msg4. should "
			    "be saved in addsinprogress.dat. docid=%" PRIu64,m_docId);
		else
			log("doc: resetting xmldoc with outstanding msg4. should "
			    "be saved in addsinprogress.dat.");
	}

	m_pbuf = NULL;
	m_wts  = NULL;

	m_deleteFromIndex = false;

	if ( m_rootDocValid    ) nukeDoc ( m_rootDoc    );
	if ( m_oldDocValid     ) nukeDoc ( m_oldDoc     );
	if ( m_extraDocValid   ) nukeDoc ( m_extraDoc   );

	if ( m_linkInfo1Valid && ptr_linkInfo1 && m_freeLinkInfo1 ) {
		// it now points into m_myPageLinkInfoBuf !
		//mfree ( ptr_linkInfo1 , size_linkInfo1, "LinkInfo1");
		ptr_linkInfo1    = NULL;
		m_linkInfo1Valid = false;
	}

	if ( m_rawUtf8ContentValid && m_rawUtf8Content && !m_setFromTitleRec
	     // was content supplied by pageInject.cpp?
	     //! m_contentInjected ) {
	     ) {
		mfree ( m_rawUtf8Content, m_rawUtf8ContentAllocSize,"Xml3");
	}

	// reset this
	m_contentInjected = false;
	m_rawUtf8ContentValid = false;
	m_wasContentInjected = false;

	m_rootDoc = NULL;

	// if this is true, then only index if new
	m_newOnly = 0;

	if ( m_httpReplyValid && m_httpReply ) {
		mfree(m_httpReply,m_httpReplyAllocSize,"httprep");
		m_httpReply = NULL;
		m_httpReplyValid = false;
	}

	if ( m_filteredContentAllocSize ) {
		mfree (m_filteredContent,m_filteredContentAllocSize,"xdfc");
		m_filteredContent = NULL;
		m_filteredContentAllocSize = 0;
	}

	if ( m_metaList ) { // m_metaListValid && m_metaList ) {
		mfree ( m_metaList , m_metaListAllocSize , "metalist");
		m_metaList          = NULL;
		m_metaListSize      = 0;
		m_metaListAllocSize = 0;
	}

	if ( m_ubuf ) {
		mfree ( m_ubuf     , m_ubufAlloc         , "ubuf");
		m_ubuf = NULL;
	}

	m_titleRecBuf.purge();

	if ( m_dupTrPtr ) {
		mfree ( m_dupTrPtr , m_dupTrSize , "trecd" );
		m_dupTrPtr = NULL;
	}

	if ( m_oldTitleRecValid && m_oldTitleRec ) {
		mfree ( m_oldTitleRec , m_oldTitleRecSize , "treca" );
		m_oldTitleRec = NULL;
		m_oldTitleRecValid = false;
	}

	if ( m_rootTitleRecValid && m_rootTitleRec ) {
		mfree ( m_rootTitleRec , m_rootTitleRecSize , "treca" );
		m_rootTitleRec = NULL;
		m_rootTitleRecValid = false;
	}


	if ( m_outlinkHopCountVectorValid && m_outlinkHopCountVector ) {
		int32_t sz = m_outlinkHopCountVectorSize;
		mfree ( m_outlinkHopCountVector,sz,"ohv");
	}
	m_outlinkHopCountVector = NULL;

	// reset all *valid* flags to false
	void *p    = &m_VALIDSTART;
	void *pend = &m_VALIDEND;
	memset ( p , 0 , (char *)pend - (char *)p );

	m_hashedMetas = false;

	// Doc.cpp:
	m_mime.reset();
	m_words.reset();
	m_phrases.reset();
	m_bits.reset();
	m_sections.reset();
	m_countTable.reset();

	// other crap
	m_xml.reset();
	m_links.reset();
	m_bits2.reset();
	m_pos.reset();
	m_synBuf.reset();
	m_images.reset();
	m_countTable.reset();
	m_mime.reset();
	m_tagRec.reset();
	m_newTagBuf.reset();
	m_dupList.reset();
	m_msg8a.reset();
	m_msg13.reset();
	m_msge0.reset();
	m_msge1.reset();
	m_reply.reset();
	// mroe stuff skipped

	m_wtsTable.reset();
	m_wbuf.reset();
	m_pageLinkBuf.reset();
	m_siteLinkBuf.reset();
	m_esbuf.reset();
	m_tagRecBuf.reset();

	// origin of this XmlDoc
	m_setFromTitleRec    = false;
	m_setFromUrl         = false;
	m_setFromDocId       = false;
	m_setFromSpiderRec   = false;
	m_freeLinkInfo1      = false;

	m_checkedUrlFilters  = false;

	m_indexCode   = 0;
	m_masterLoop  = NULL;
	m_masterState = NULL;

	//m_isAddUrl = false;
	m_isInjecting = false;
	m_useFakeMime = false;
	m_useSiteLinkBuf = false;
	m_usePageLinkBuf = false;
	m_printInXml = false;

	m_check1   = false;
	m_check2   = false;
	m_prepared = false;

	// keep track of updates to the rdbs we have done, so we do not re-do
	m_listAdded                = false;
	m_listFlushed              = false;
	m_copied1                  = false;
	m_updatingSiteLinkInfoTags = false;
	m_hashedTitle              = false;

	m_registeredSleepCallback  = false;

	m_numRedirects             = 0;
	m_numOutlinksAdded         = 0;
	m_useRobotsTxt             = true;

	m_allowSimplifiedRedirs    = false;

	m_didDelay                 = false;
	m_didDelayUnregister       = false;
	m_calledMsg22e             = false;
	m_calledMsg22f             = false;
	m_calledMsg25              = false;
	m_calledSections           = false;
	m_calledThread             = false;
	m_alreadyRegistered        = false;
	m_loaded                   = false;

	m_setTr                    = false;

	m_recycleContent           = false;
	m_callback1                = NULL;
	m_callback2                = NULL;
	m_state                    = NULL;

	m_doingConsistencyCheck    = false;

	m_isChildDoc = false;

	// for utf8 content functions
	m_savedp       = NULL;
	m_oldp         = NULL;
	m_didExpansion = false;

	// Repair.cpp now explicitly sets these to false if needs to
	m_usePosdb     = true;
	m_useClusterdb = true;
	m_useLinkdb    = true;
	m_useSpiderdb  = true;
	m_useTitledb   = true;
	m_useTagdb     = true;
	m_useSecondaryRdbs = false;

	// used by Msg13.cpp only. kinda a hack.
	m_isSpiderProxy = false;

	// do not cache the http reply in msg13 etc.
	m_maxCacheAge = 0;

	// reset these ptrs too!
	void *px    = &ptr_firstUrl;
	void *pxend = &m_dummyEnd;
	memset ( px , 0 , (char *)pxend - (char *)px );
}

int64_t XmlDoc::logQueryTimingStart() {
	if ( !g_conf.m_logTimingQuery ) {
		return 0;
	}

	return gettimeofdayInMilliseconds();
}

void XmlDoc::logQueryTimingEnd(const char* function, int64_t startTime) {
	if ( !g_conf.m_logTimingQuery ) {
		return;
	}

	int64_t endTime = gettimeofdayInMilliseconds();
	int64_t diff = endTime - startTime;

	//if (diff > 5) {
		log( LOG_TIMING, "query: XmlDoc::%s took %" PRId64 " ms for docId=%" PRId64, function, diff, m_docId );
	//}
}

int32_t XmlDoc::getSpideredTime ( ) {
	// stop if already set
	if ( m_spideredTimeValid ) return m_spideredTime;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return 0;

	// . set spider time to current time
	// . this might already be valid if we set it in
	//   getTestSpideredDate()
	m_spideredTime      = getTimeGlobal();
	m_spideredTimeValid = true;
	return m_spideredTime;
}

// . we need this so PageGet.cpp can get the cached web page
// . but not for Msg20::getSummary(), that uses XmlDoc::set(Msg20Request*)
// . returns false and sets g_errno on error
bool XmlDoc::set3 ( int64_t  docId       ,
		    const char   *coll        ,
		    int32_t       niceness    ) {

	reset();

	// this is true
	m_setFromDocId = true;

	m_docId       = docId;
	m_docIdValid  = true;
	m_niceness    = niceness;

	if ( ! setCollNum ( coll ) ) return false;

	return true;
}

static void loadFromOldTitleRecWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// note it
	THIS->setStatus ( "loading from old title rec wrapper" );
	// return if it blocked
	if ( ! THIS->loadFromOldTitleRec ( ) ) return;

	const char *coll = "";
	CollectionRec *cr = THIS->getCollRec();
	if ( cr ) coll = cr->m_coll;

	// error?
	if ( g_errno ) log("doc: loadfromtitlerec coll=%s: %s",
			   coll,
			   mstrerror(g_errno));
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}

// returns false if blocked, returns true and sets g_errno on error otherwise
bool XmlDoc::loadFromOldTitleRec ( ) {
	// . we are an entry point.
	// . if anything blocks, this will be called when it comes back
	if ( ! m_masterLoop ) {
		m_masterLoop  = loadFromOldTitleRecWrapper;
		m_masterState = this;
	}
	// if we already loaded!
	if ( m_loaded ) return true;
	// if set from a docid, use msg22 for this!
	char **otr = getOldTitleRec ( );
	// error?
	if ( ! otr ) return true;
	// blocked?
	if ( otr == (void *)-1 ) return false;
	// this is a not found
	if ( ! *otr ) {
		// so we do not retry
		m_loaded = true;
		// make it an error
		g_errno = ENOTFOUND;
		return true;
	}
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return true;
	// use that. decompress it! this will also set
	// m_setFromTitleRec to true
	if ( ! set2 ( m_oldTitleRec     ,
		      m_oldTitleRecSize , // maxSize
		      cr->m_coll            ,
		      NULL              , // pbuf
		      m_niceness        )) {
		// we are now loaded, do not re-call
		m_loaded = true;
		// return true with g_errno set on error uncompressing
		return true;
	}
	// we are now loaded, do not re-call
	m_loaded = true;
	// sanity check
	if ( ! m_titleRecBufValid ) { g_process.shutdownAbort(true); }
	// good to go
	return true;
}

bool XmlDoc::setCollNum ( const char *coll ) {
	CollectionRec *cr;
	cr = g_collectiondb.getRec ( coll , strlen(coll) );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log(LOG_WARN, "build: collrec not found for %s",coll);
		return false;
	}
	// we can store this safely:
	m_collnum = cr->m_collnum;
	m_collnumValid = true;
	return true;
}

CollectionRec *XmlDoc::getCollRec ( ) {
	if ( ! m_collnumValid ) { g_process.shutdownAbort(true); }
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
	if ( ! cr ) {
		log("build: got NULL collection rec for collnum=%" PRId32".",
		    (int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return NULL;
	}
	// was it reset since we started spidering this url?
	// we don't do it this way, when resetting a coll when delete it and
	// re-add under a different collnum to avoid getting msg4 adds to it.
	//if ( cr->m_lastResetCount != m_lastCollRecResetCount ) {
	//	log("build: collection rec was reset. returning null.");
	//	g_errno = ENOCOLLREC;
	//	return NULL;
	//}
	return cr;
}

// returns false and sets g_errno on error
bool XmlDoc::set4 ( SpiderRequest *sreq      ,
		    key96_t         *doledbKey ,
		    const char     *coll      ,
		    SafeBuf       *pbuf      ,
		    int32_t        niceness  ,
		    char          *utf8ContentArg ,
		    bool           deleteFromIndex ,
		    int32_t        forcedIp ,
		    uint8_t        contentType ,
		    uint32_t       spideredTime ,
		    bool           contentHasMimeArg
) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// sanity check
	if ( sreq->m_dataSize == 0 ) { g_process.shutdownAbort(true); }

	reset();

	if ( g_conf.m_logDebugSpider )
		log("xmldoc: set4 uh48=%" PRIu64" parentdocid=%" PRIu64,
		    sreq->getUrlHash48(),sreq->getParentDocId());

	// used by PageSpiderdb.cpp
	m_startTime      = gettimeofdayInMilliseconds();
	m_startTimeValid = true;

	// this is true
	m_setFromSpiderRec = true;

	// did page inject (pageinject) request to delete it?
	m_deleteFromIndex = deleteFromIndex;

	// PageReindex.cpp will set this in the spider request
	if ( sreq->m_forceDelete ) {
		m_deleteFromIndex = true;
	}

	char *utf8Content = utf8ContentArg;

	if ( utf8Content ) {
		// get length of it all
		int32_t clen = strlen(utf8Content);
		// return true on error with g_errno set
		if ( ! m_mime.set ( utf8ContentArg , clen , NULL ) ) {
			if ( ! g_errno ) g_errno = EBADMIME;
			log("xmldoc: could not set mime: %s", mstrerror(g_errno));
			logTrace( g_conf.m_logTraceXmlDoc, "END, returning false. Mime problem." );
			return false;
		}
		// it's valid
		m_mimeValid = true;
		// advance
		utf8Content = m_mime.getContent();
	}

	// use this to avoid ip lookup if it is not zero
	if ( forcedIp ) {
		m_ip = forcedIp;
		m_ipValid = true;
	}

	// sometimes they supply the content they want! like when zaks'
	// injects pages from PageInject.cpp
	if ( utf8Content ) {
		// . this is the most basic content from the http reply
		// . only set this since sometimes it is facebook xml and
		//   contains encoded html which needs to be decoded.
		//   like <name>Ben &amp; Jerry's</name> otherwise are
		//   sentence formation stops at the ';' in the "&amp;" and
		//   we also index "amp" which is bad.
		m_content             = utf8Content;
		if ( m_mimeValid && m_mime.m_contentLen > 0) {
			m_contentLen = m_mime.m_contentLen;
		} else {
			m_contentLen = strlen(utf8Content);
		}

		m_contentValid        = true;

		//m_rawUtf8Content      = utf8Content;
		//m_expandedUtf8Content = utf8Content;

		//ptr_utf8Content       = utf8Content;
		//size_utf8Content      = slen+1;

		//m_rawUtf8ContentValid      = true;
		//m_expandedUtf8ContentValid = true;
		//m_utf8ContentValid         = true;

		m_contentInjected     = true;
		m_wasContentInjected  = true;
		m_contentType         = contentType;
		m_contentTypeValid    = true;
		// use this ip as well for now to avoid ip lookup
		//m_ip      = atoip("127.0.0.1");
		//m_ipValid = true;
		// do not need robots.txt then
		m_isAllowed      = true;
		m_isAllowedValid = true;
		// nor mime
		m_httpStatus      = 200;
		m_httpStatusValid = true;
		// this too
		m_downloadStatus      = 0;
		m_downloadStatusValid = true;
		// assume this is the download time since the content
		// was pushed/provided to us
		if ( spideredTime )
			m_downloadEndTime = spideredTime;
		else
			m_downloadEndTime = gettimeofdayInMillisecondsGlobal();
		// either way, validate it
		m_downloadEndTimeValid = true;
		// and need a legit mime
		if ( ! m_mimeValid ) {
			m_mime.m_bufLen  = 1;
			m_mimeValid      = true;
			m_mime.m_contentType = contentType;
		}
		m_isContentTruncated      = false;
		m_isContentTruncatedValid = true;
		// no redir
		ptr_redirUrl    = NULL;
		size_redirUrl   = 0;
		m_redirUrl.reset();
		m_redirUrlPtr   = NULL;//&m_redirUrl;
		m_redirUrlValid = true;
		m_redirErrorValid = true;
		m_redirError      = 0;
		m_crawlDelay      = -1;
		m_crawlDelayValid = true;
	}

	// override content type based on mime for application/json
	if ( m_mimeValid ) {
		m_contentType = m_mime.m_contentType;
		m_contentTypeValid = true;
	}


	//m_coll      = coll;
	m_pbuf      = pbuf;
	m_niceness  = niceness;
	m_version   = TITLEREC_CURRENT_VERSION;
	m_versionValid = true;

	/*
	// set min/max pub dates right away
	m_minPubDate = -1;
	m_maxPubDate = -1;
	// parentPrevSpiderTime is 0 if that was the first time that the
	// parent was spidered, in which case isNewOutlink will always be set
	// for every outlink it had!
	if ( sreq->m_isNewOutlink && sreq->m_parentPrevSpiderTime ) {
		// sanity check
		if ( ! sreq->m_parentPrevSpiderTime ) {g_process.shutdownAbort(true);}
		// pub date is somewhere between these two times
		m_minPubDate = sreq->m_parentPrevSpiderTime;
		m_maxPubDate = sreq->m_addedTime;
	}
	*/

	// this is used to removing the rec from doledb after we spider it
	m_doledbKey.setMin();
	if ( doledbKey ) m_doledbKey = *doledbKey;

	// . sanity check
	// . we really don't want the parser holding up the query pipeline
	//   even if this page is being turked!
	//if ( m_niceness == 0 &&
	//     // spider proxy uses xmldoc class to expand iframe tags and
	//     // sometimes the initiating msg13 class was re-niced to 0
	//     // in the niceness converstion logic.
	//     ! g_hostdb.m_myHost->m_isProxy ) {
	//	g_process.shutdownAbort(true); }

	m_sreqValid    = true;

	// store the whole rec, key+dataSize+data, in case it disappears.
	gbmemcpy ( &m_sreq , sreq , sreq->getRecSize() );

	// set m_collnum etc.
	if ( ! setCollNum ( coll ) )
	{
		log("XmlDoc: set4() coll %s invalid",coll);
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning false. Collection invalid" );
		return false;
	}

	// it should be valid since we just set it
	CollectionRec *cr = getCollRec();

	m_useRobotsTxt = cr->m_useRobotsTxt;

	// solidify some parms
	//m_eliminateMenus       = cr->m_eliminateMenus;
	//m_eliminateMenusValid  = true;

	// validate these here too
	/*
	m_titleWeight            = cr->m_titleWeight;
	m_headerWeight           = cr->m_headerWeight;
	m_urlPathWeight          = cr->m_urlPathWeight;
	m_externalLinkTextWeight = cr->m_externalLinkTextWeight;
	m_internalLinkTextWeight = cr->m_internalLinkTextWeight;
	m_conceptWeight          = cr->m_conceptWeight;

	m_titleWeightValid            = true;
	m_headerWeightValid           = true;
	m_urlPathWeightValid          = true;
	m_externalLinkTextWeightValid = true;
	m_internalLinkTextWeightValid = true;
	m_conceptWeightValid          = true;
	*/

	// fix some corruption i've seen
	if ( m_sreq.m_urlIsDocId && ! is_digit(m_sreq.m_url[0]) ) {
		log("xmldoc: fixing sreq %s to non docid",m_sreq.m_url);
		m_sreq.m_urlIsDocId = 0;
	}

	// if url is a docid... we are from pagereindex.cpp
	//if ( sreq->m_isPageReindex ) {
	// now we can have url-based page reindex requests because
	// if we have a diffbot json object fake url reindex request
	// we add a spider request of the PARENT url for it as page reindex
	//if ( is_digit ( sreq->m_url[0] ) ) {
	// watch out for 0.r.msn.com!!
	if ( m_sreq.m_urlIsDocId ) {
		m_docId          = atoll(m_sreq.m_url);
		// assume its good
		m_docIdValid     = true;
		// similar to set3() above
		m_setFromDocId   = true;
		// use content and ip from old title rec to save time
		// . crap this is making the query reindex not actually
		//   re-download the content.
		// . we already check the m_deleteFromIndex flag below
		//   in getUtf8Content() and use the old content in that case
		//   so i'm not sure why we are recycling here, so take
		//   this out. MDW 9/25/2014.
		//m_recycleContent = true;
		// sanity
		if ( m_docId == 0LL ) { g_process.shutdownAbort(true); }
	}
	else {
		logTrace( g_conf.m_logTraceXmlDoc, "Calling setFirstUrl with [%s]", m_sreq.m_url);
		setFirstUrl ( m_sreq.m_url );
		// you can't call this from a docid based url until you
		// know the uh48
		//setSpideredTime();
	}

	// now query reindex can specify a recycle content option so it
	// can replace the rebuild tool. try to recycle on global index.
	if ( m_sreqValid )
		m_recycleContent = m_sreq.m_recycleContent;

	logTrace( g_conf.m_logTraceXmlDoc, "END, returning true" );
	return true;
}



// . set our stuff from the TitleRec (from titledb)
// . returns false and sets g_errno on error
bool XmlDoc::set2 ( char    *titleRec ,
		    int32_t     maxSize  ,
		    const char    *coll     ,
		    SafeBuf *pbuf     ,
		    int32_t     niceness ,
		    SpiderRequest *sreq ) {

	// NO! can't do this. see below
	//reset();

	setStatus ( "setting xml doc from title rec");

	// . it resets us, so save this
	// . we only save these for set2() not the other sets()!
	//void (*cb1)(void *state) = m_callback1;
	//bool (*cb2)(void *state) = m_callback2;
	//void *state = m_state;

	// . clear it all out
	// . no! this is clearing our msg20/msg22 reply...
	// . ok, but repair.cpp needs it so do it there then
	//reset();

	// restore callbacks
	//m_callback1 = cb1;
	//m_callback2 = cb2;
	//m_state     = state;

	// sanity check - since we do not reset
	if ( m_contentValid ) { g_process.shutdownAbort(true); }

	// this is true
	m_setFromTitleRec = true;

	// this is valid i guess. includes key, etc.
	//m_titleRec      = titleRec;
	//m_titleRecSize  = *(int32_t *)(titleRec+12) + sizeof(key96_t) + 4;
	//m_titleRecValid = true;
	// . should we free m_cbuf on our reset/destruction?
	// . no because doCOnsistencyCheck calls XmlDoc::set2 with a titleRec
	//   that should not be freed, besides the alloc size is not known!
	//m_freeTitleRec  = false;

	// it must be there!
	if ( !titleRec ) { g_errno=ENOTFOUND; return false; }

	int32_t titleRecSize = *(int32_t *)(titleRec+12) + sizeof(key96_t) + 4;

	// it must be there!
	if ( titleRecSize==0 ) { g_errno=ENOTFOUND; return false; }

	// . should we free m_cbuf on our reset/destruction?
	// . no because doCOnsistencyCheck calls XmlDoc::set2 with a titleRec
	//   that should not be freed, besides the alloc size is not known!
	m_titleRecBuf.setBuf ( titleRec ,
			       titleRecSize , // bufmax
			       titleRecSize ,  // bytes in use
			       false, // ownData?
			       csUTF8); // encoding
	m_titleRecBufValid = true;


	//m_coll               = coll;
	m_pbuf               = pbuf;
	m_niceness           = niceness;

	// set our collection number
	if ( ! setCollNum ( coll ) ) return false;

	// store the whole rec, key+dataSize+data, in case it disappears.
	if ( sreq ) {
		gbmemcpy ( &m_sreq , sreq , sreq->getRecSize() );
		m_sreqValid = true;
	}

	m_hashedTitle        = false;
	m_hashedMetas        = false;

	// save the compressed buffer in case we should free it when done
	//m_titleRec = titleRec;
	// should we free m_cbuf on our reset/destruction?
	//m_freeTitleRec = true;
	// our record may not occupy all of m_cbuf, careful
	//m_titleRecAllocSize = maxSize;

	// get a parse ptr
	char *p = titleRec ;
	// . this is just like a serialized RdbList key/dataSize/data of 1 rec
	// . first thing is the key
	// . key should have docId embedded in it
	m_titleRecKey =  *(key96_t *) p ;
	//m_titleRecKeyValid = true;
	p += sizeof(key96_t);
	// bail on error
	if ( (m_titleRecKey.n0 & 0x01) == 0x00 ) {
		g_errno = EBADTITLEREC;
		log("db: Titledb record is a negative key.");
		g_process.shutdownAbort(true);
	}

	// set m_docId from key
	m_docId = Titledb::getDocIdFromKey ( &m_titleRecKey );
	// validate that
	m_docIdValid = true;
	// then the size of the data that follows this
	int32_t dataSize =  *(int32_t *) p ;
	p += 4;
	// bail on error
	if ( dataSize < 4 ) {
		g_errno = EBADTITLEREC;
		log(LOG_WARN, "db: Titledb record has size of %" PRId32" which is less then 4. Probable disk corruption in a "
			"titledb file.", dataSize);
		return false;
	}
	// what is the size of cbuf/titleRec in bytes?
	int32_t cbufSize = dataSize + 4 + sizeof(key96_t);
	// . the actual data follows "dataSize"
	// . what's the size of the uncompressed compressed stuff below here?
	m_ubufSize = *(int32_t  *) p ; p += 4;
	// . because of disk/network data corruption this may be wrong!
	// . we can now have absolutely huge titlerecs...
	if ( m_ubufSize <= 0 ) { //m_ubufSize > 2*1024*1024 || m_ubufSize < 0 )
		g_errno = EBADTITLEREC;
		log(LOG_WARN, "db: TitleRec::set: uncompress uncompressed size=%" PRId32".",m_ubufSize );
		return false;
	}
	// trying to uncompress corrupt titlerecs sometimes results in
	// a seg fault... watch out
	if ( m_ubufSize > 100*1024*1024 ) {
		g_errno = EBADTITLEREC;
		log(LOG_WARN, "db: TitleRec::set: uncompress uncompressed size=%" PRId32" > 100MB. unacceptable, probable "
			"corruption.", m_ubufSize);
		return false;
	}
	// make buf space for holding the uncompressed stuff
	m_ubufAlloc = m_ubufSize;
	m_ubuf = (char *) mmalloc ( m_ubufAlloc ,"TitleRecu1");
	// log("xmldoc: m_ubuf=%" PTRFMT" this=%" PTRFMT
	//     , (PTRTYPE) m_ubuf
	//     , (PTRTYPE) this
	//     );
	if ( ! m_ubuf ) {
		// we had bad ubufsizes on gb6, like > 1GB print out key
		// so we can manually make a titledb.dat file to delete these
		// bad keys
		log("build: alloc failed ubufsize=%" PRId32" key.n1=%" PRIu32" "
		    "n0=%" PRIu64,
		    m_ubufAlloc,m_titleRecKey.n1,m_titleRecKey.n0);
		return false;
	}
	// we need to loop since uncompress is wierd, sometimes it needs more
	// space then it should. see how much it actually took.
	int32_t realSize = m_ubufSize;
	// time it
	int64_t startTime = gettimeofdayInMilliseconds();
	// debug msg

	setStatus( "Uncompressing title rec." );
	// . uncompress the data into m_ubuf
	// . m_ubufSize should remain unchanged since we stored it
	int err = gbuncompress ( (unsigned char *)  m_ubuf ,
				 (uint32_t *) &realSize   ,
				 (unsigned char *)  p ,
				 (uint32_t  ) (dataSize - 4) );
	// hmmmm...
	if ( err == Z_BUF_ERROR ) {
		log(LOG_WARN, "db: Buffer is too small to hold uncompressed document. Probable disk corruption in a titledb file.");
		g_errno = EUNCOMPRESSERROR;
		return false;
	}
	// set g_errno and return false on error
	if ( err != Z_OK ) {
		g_errno = EUNCOMPRESSERROR;
		log(LOG_WARN, "db: Uncompress of document failed. ZG_ERRNO=%i. cbufSize=%" PRId32" ubufsize=%" PRId32" realSize=%" PRId32,
		    err , cbufSize , m_ubufSize , realSize );
		return false;
	}
	if ( realSize != m_ubufSize ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "db: Uncompressed document size is not what we recorded it to be. Probable disk corruption in "
			   "a titledb file.");
		return false;
	}
	// . add the stat
	// . use white for the stat
	g_stats.addStat_r ( 0          ,
			    startTime  ,
			    gettimeofdayInMilliseconds(),
			    0x00ffffff );

	// first 2 bytes in m_ubuf is the header size
	int32_t headerSize = *(uint16_t *)m_ubuf;

	int32_t shouldbe = (char *)&ptr_firstUrl - (char *)&m_headerSize;

	if ( headerSize != shouldbe ) {
		g_errno = ECORRUPTDATA;
		log(LOG_WARN, "doc: bad header size in title rec");
		return false;
	}

	// set our easy stuff
	gbmemcpy ( (void *)this , m_ubuf , headerSize );

	// NOW set the XmlDoc::ptr_* and XmlDoc::size_* members
	// like in Msg.cpp and Msg20Reply.cpp

	if ( m_pbuf ) {
		int32_t crc = hash32(m_ubuf,headerSize);
		m_pbuf->safePrintf("crchdr=0x%" PRIx32" sizehdr=%" PRId32", ",
				   crc,headerSize);
	}


	// point to the string data
	char *up = m_ubuf + headerSize;

	// end of the rec
	char *upend = m_ubuf + m_ubufSize;

	// how many XmlDoc::ptr_* members do we have? set "np" to that
	int32_t np = ((char *)&size_firstUrl  - (char *)&ptr_firstUrl) ;
	np /= sizeof(char *);

	// point to the first ptr
	char **pd = (char **)&ptr_firstUrl;
	// point to the first size
	int32_t *ps = (int32_t *)&size_firstUrl;

	// loop over them
	for ( int32_t i = 0 ; i < np ; i++ , pd++ , ps++ ) {
		// zero out the ith ptr_ and size_ member
		*pd = 0;
		*ps = 0;
		// make the mask
		uint32_t mask = 1 << i ;
		// do we have this member? skip if not.
		if ( ! (m_internalFlags1 & mask) ) continue;
		// watch out for corruption
		if ( up > upend ) {
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "doc: corrupt titlerec.");
			return false;
		}
		// get the size
		*ps = *(int32_t *)up;
		// this should never be 0, otherwise, why was its flag set?
		if ( *ps <= 0 ) { g_process.shutdownAbort(true); }
		// skip over to point to data
		up += 4;
		// point to the data. could be 64-bit ptr.
		*pd = up;//(int32_t)up;
		// debug
		if ( m_pbuf ) {
			int32_t crc = hash32(up,*ps);
			m_pbuf->safePrintf("crc%" PRId32"=0x%" PRIx32" size%" PRId32"=%" PRId32", ",
					   i,crc,i,*ps);
		}
		// skip over data
		up += *ps;
		// watch out for corruption
		if ( up > upend ) {
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "doc: corrupt titlerec.");
			return false;
		}
	}
	// cap it
	char *pend = m_ubuf + m_ubufSize;
	// sanity check. must match exactly.
	if ( up != pend ) { g_process.shutdownAbort(true); }

	// set the urls i guess
	m_firstUrl.set   ( ptr_firstUrl );
	if ( ptr_redirUrl ) {
		m_redirUrl.set   ( ptr_redirUrl );
		m_currentUrl.set ( ptr_redirUrl );
		m_currentUrlValid = true;
		m_redirUrlPtr     = &m_redirUrl;
	}
	else {
		m_currentUrl.set ( ptr_firstUrl );
		m_currentUrlValid = true;
		m_redirUrlPtr     = NULL;
	}
	m_firstUrlValid               = true;
	m_redirUrlValid               = true;

	// validate *shadow* members since bit flags cannot be returned
	m_isRSS2              = m_isRSS;
	m_isPermalink2        = m_isPermalink;
	m_isAdult2            = m_isAdult;
	m_spiderLinks2        = m_spiderLinks;
	m_isContentTruncated2 = m_isContentTruncated;
	m_isLinkSpam2         = m_isLinkSpam;
	m_isSiteRoot2         = m_isSiteRoot;

	// these members are automatically validated
	m_ipValid                     = true;
	m_spideredTimeValid           = true;
	m_indexedTimeValid            = true;

	m_outlinksAddedDateValid      = true;
	m_charsetValid                = true;
	m_countryIdValid              = true;

	// new stuff
	m_siteNumInlinksValid         = true;
	m_metaListCheckSum8Valid      = true;

	m_hopCountValid               = true;
	m_langIdValid                 = true;
	m_contentTypeValid            = true;
	m_isRSSValid                  = true;
	m_isPermalinkValid            = true;
	m_isAdultValid                = true;
	m_spiderLinksValid            = true;
	m_isContentTruncatedValid     = true;
	m_isLinkSpamValid             = true;
	m_tagRecDataValid             = true;
	m_contentHash32Valid          = true;
	m_tagPairHash32Valid          = true;
	m_imageDataValid              = true;
	m_utf8ContentValid            = true;
	m_siteValid                   = true;
	m_linkInfo1Valid              = true;
	m_versionValid                = true;
	m_httpStatusValid             = true;
	m_crawlDelayValid             = true;
	m_isSiteRootValid             = true;

	// there was no issue indexing it...
	m_indexCode       = 0;
	m_indexCodeValid  = true;
	m_redirError      = 0;
	m_redirErrorValid = true;

	// stop core when importing and calling getNewSpiderReply()
	m_downloadEndTime = m_spideredTime;
	m_downloadEndTimeValid = true;

	// must not be negative
	if ( m_siteNumInlinks < 0 ) { g_process.shutdownAbort(true); }


	// sanity check. if m_siteValid is true, this must be there
	if ( ! ptr_site ) {
		log("set2: ptr_site is null for docid %" PRId64,m_docId);
		//g_process.shutdownAbort(true); }
		g_errno = ECORRUPTDATA;
		return false;
	}

	// success, return true then
	return true;
}


bool XmlDoc::setFirstUrl ( char *u ) {
	m_firstUrl.reset();
	m_currentUrl.reset();

	m_firstUrlValid = true;

	// assume url is not correct format
	ptr_firstUrl  = NULL;
	size_firstUrl = 0;

	if ( ! u || ! u[0] ) {
		//if ( ! m_indexCode ) m_indexCode = EBADURL;
		return true;
	}

	//if ( strlen (u) + 1 > MAX_URL_LEN )
	//	m_indexCode = EURLTOOLONG;

	m_firstUrl.set( u );

	// it is the active url
	m_currentUrl.set ( &m_firstUrl );
	m_currentUrlValid = true;

	// set this to the normalized url
	ptr_firstUrl  = m_firstUrl.getUrl();
	size_firstUrl = m_firstUrl.getUrlLen() + 1;

	return true;
}



void XmlDoc::setStatus ( const char *s ) {
	bool timeIt = false;
	if ( g_conf.m_logDebugBuildTime )
		timeIt = true;

	// log times to detect slowness
	if ( timeIt  && m_statusMsgValid ) {
		int64_t now = gettimeofdayInMillisecondsLocal();
		if ( s_lastTimeStart == 0LL ) s_lastTimeStart = now;
		int32_t took = now - s_lastTimeStart;
		//if ( took > 100 )
			log("xmldoc: %s (xd=0x%" PTRFMT" "
			    "u=%s) took %" PRId32"ms",
			    m_statusMsg,
			    (PTRTYPE)this,
			    m_firstUrl.getUrl(),
			    took);
		s_lastTimeStart = now;
	}

	m_statusMsg = s;
	m_statusMsgValid = true;

	bool logIt = g_conf.m_logDebugBuild;
	if ( ! logIt ) return;

	if ( m_firstUrlValid )
		logf(LOG_DEBUG,"build: status = %s for %s (this=0x%" PTRFMT")",
		     s,m_firstUrl.getUrl(),(PTRTYPE)this);
	else
		logf(LOG_DEBUG,"build: status = %s for docId %" PRId64" "
		     "(this=0x%" PTRFMT")",
		     s,m_docId, (PTRTYPE)this);
}

// caller must now call XmlDoc::setCallback()
void XmlDoc::setCallback ( void *state, void (* callback) (void *state) ) {
	m_state     = state;
	m_callback1 = callback;
	// add this additional state==this constraint to prevent core when
	// doing a page parser
	if ( state == this &&
	     // i don't remember why i added this sanity check...
	     callback == getMetaListWrapper ) { g_process.shutdownAbort(true); }
}

void XmlDoc::setCallback ( void *state, bool (*callback) (void *state) ) {
	m_state     = state;
	m_callback2 = callback;
}



static void indexDocWrapper ( void *state ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// note it
	THIS->setStatus ( "in index doc wrapper" );

	logTrace( g_conf.m_logTraceXmlDoc, "Calling XmlDoc::indexDoc" );
	// return if it blocked
	if ( ! THIS->indexDoc( ) )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, indexDoc blocked" );
		return;
	}

	// otherwise, all done, call the caller callback

	// g_statsdb.addStat ( MAX_NICENESS,
	// 		    "docs_indexed",
	// 		    20,
	// 		    21,
	// 		    );


	logTrace( g_conf.m_logTraceXmlDoc, "Calling callback" );
	THIS->callCallback();

	logTrace( g_conf.m_logTraceXmlDoc, "END" );
}



// for registerSleepCallback
static void indexDocWrapper2 ( int fd , void *state ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	indexDocWrapper ( state );

	logTrace( g_conf.m_logTraceXmlDoc, "END" );
}

// . the highest level function in here
// . user is requesting to inject this url
// . returns false if blocked and your callback will be called when done
// . returns true and sets g_errno on error
bool XmlDoc::injectDoc ( char *url ,
			 CollectionRec *cr ,
			 char *content ,
			 bool contentHasMimeArg ,
			 int32_t hopCount,
			 int32_t charset,

			 bool deleteUrl,
			 char *contentTypeStr, // text/html application/json
			 bool spiderLinks ,
			 char newOnly, // index iff new

			 void *state,
			 void (*callback)(void *state) ,

			 uint32_t firstIndexed,
			 uint32_t lastSpidered ,
			 int32_t injectDocIp
			 ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );


	// wait until we are synced with host #0
	if ( ! isClockInSync() ) {
		log("xmldocl: got injection request but clock not yet "
		    "synced with host #0");
		g_errno = ETRYAGAIN;//CLOCKNOTSYNCED;
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning true" );
		return true;
	}

	// normalize url
	Url uu;
	uu.set( url );

	// remove >'s i guess and store in st1->m_url[] buffer
	char cleanUrl[MAX_URL_LEN+1];
	cleanInput ( cleanUrl,
		     MAX_URL_LEN,
		     uu.getUrl(),
		     uu.getUrlLen() );


	int32_t contentType = CT_UNKNOWN;
	if ( contentTypeStr && contentTypeStr[0] )
		contentType = getContentTypeFromStr(contentTypeStr);

	// use CT_HTML if contentTypeStr is empty or blank. default
	if ( ! contentTypeStr || ! contentTypeStr[0] )
		contentType = CT_HTML;

	// this can go on the stack since set4() copies it
	SpiderRequest sreq;
	sreq.setFromInject ( cleanUrl );

	if ( lastSpidered )
		sreq.m_addedTime = lastSpidered;

	if ( deleteUrl )
		sreq.m_forceDelete = 1;

	//static char s_dummy[3];
	// sometims the content is indeed NULL...
	//if ( newOnly && ! content ) {
	//	// don't let it be NULL because then xmldoc will
	//	// try to download the page!
	//	s_dummy[0] = '\0';
	//	content = s_dummy;
	//	//g_process.shutdownAbort(true); }
	//}

	// . use the enormous power of our new XmlDoc class
	// . this returns false with g_errno set on error
	if ( ! set4 ( &sreq       ,
		      NULL        ,
		      cr->m_coll  ,
		      NULL        , // pbuf
		      // from PageInject.cpp:
		      // give it a niceness of 1, we have to be
		      // careful since we are a niceness of 0!!!!
		      1, // niceness, // 1 ,
		      // inject this content
		      content ,
		      deleteUrl, // false, // deleteFromIndex ,
		      injectDocIp, // 0,//forcedIp ,
		      contentType ,
		      lastSpidered,//lastSpidered overide
		      contentHasMimeArg )) {
		// g_errno should be set if that returned false
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning true. set4 returned false" );
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		return true;
	}

	//m_doConsistencyTesting = doConsistencyTesting;

	// . set xd from the old title rec if recycle is true
	// . can also use XmlDoc::m_loadFromOldTitleRec flag
	//if ( recycleContent ) m_recycleContent = true;

	// othercrap. used for importing from titledb of another coll/cluster.
	if ( firstIndexed ) {
		m_firstIndexedDate = firstIndexed;
		m_firstIndexedDateValid = true;
	}

	if ( lastSpidered ) {
		m_spideredTime      = lastSpidered;
		m_spideredTimeValid = true;
	}

	if ( hopCount != -1 ) {
		m_hopCount = hopCount;
		m_hopCountValid = true;
	}

	// PageInject calls memset on gigablastrequest so add '!= 0' here
	if ( charset != -1 && charset != csUnknown && charset != 0 ) {
		m_charset = charset;
		m_charsetValid = true;
	}

	// avoid looking up ip of each outlink to add "firstip" tag to tagdb
	// because that can be slow!!!!!!!
	m_spiderLinks = spiderLinks;
	m_spiderLinks2 = spiderLinks;
	m_spiderLinksValid = true;

	// . newOnly is true --> do not inject if document is already indexed!
	// . maybe just set indexCode
	m_newOnly = newOnly;

	// do not re-lookup the robots.txt
	m_isAllowed      = true;
	m_isAllowedValid = true;
	m_crawlDelay     = -1; // unknown
	m_crawlDelayValid = true;


	m_isInjecting = true;
	m_isInjectingValid = true;

	// set this now
	//g_inPageInject = true;

	// log it now
	//log("inject: indexing injected doc %s",cleanUrl);

	// make this our callback in case something blocks
	setCallback ( state , callback );

	// . now tell it to index
	// . this returns false if blocked
	// . eventually it will call "callback" when done if it blocks
	logTrace( g_conf.m_logTraceXmlDoc, "Calling indexDoc" );
	bool status = indexDoc ( );
	if ( ! status )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning false. indexDoc returned false" );
		return false;
	}

	// log it. i guess only for errors when it does not block?
	// because xmldoc.cpp::indexDoc calls logIt()
	if ( status ) logIt();



	// undo it
	//g_inPageInject = false;

	logTrace( g_conf.m_logTraceXmlDoc, "END, returning true. indexDoc returned true" );
	return true;
}

// XmlDoc::injectDoc uses a fake spider request so we have to add
// a real spider request into spiderdb so that the injected doc can
// be spidered again in the future by the spidering process, otherwise,
// injected docs can never be re-spidered. they would end up having
// a SpiderReply in spiderdb but no matching SpiderRequest as well.
void XmlDoc::getRevisedSpiderRequest ( SpiderRequest *revisedReq ) {

	if ( ! m_sreqValid ) { g_process.shutdownAbort(true); }

	// we are doing this because it has a fake first ip
	if ( ! m_sreq.m_fakeFirstIp ) { g_process.shutdownAbort(true); }

	// copy it over from our current spiderrequest
	gbmemcpy ( revisedReq , &m_sreq , m_sreq.getRecSize() );

	// this must be valid for us of course
	if ( ! m_firstIpValid ) { g_process.shutdownAbort(true); }

	// wtf? it might be invalid!!! parent caller will handle it...
	//if ( m_firstIp == 0 || m_firstIp == -1 ) { g_process.shutdownAbort(true); }

	// store the real ip in there now
	revisedReq->m_firstIp = m_firstIp;

	// but turn off this flag! the whole point of all this...
	revisedReq->m_fakeFirstIp = 0;

	// re-make the key since it contains m_firstIp
	int64_t uh48 = m_sreq.getUrlHash48();
	int64_t parentDocId = m_sreq.getParentDocId();

	// set the key properly to reflect the new "first ip" since
	// we shard spiderdb by that.
	revisedReq->m_key = g_spiderdb.makeKey ( m_firstIp, uh48, true, parentDocId, false );
	revisedReq->setDataSize();
}

void XmlDoc::getRebuiltSpiderRequest ( SpiderRequest *sreq ) {

	// memset 0
	sreq->reset();

	// assume not valid
	sreq->m_siteNumInlinks = -1;

	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }

	// how many site inlinks?
	sreq->m_siteNumInlinks       = m_siteNumInlinks;
	sreq->m_siteNumInlinksValid  = true;

	if ( ! m_firstIpValid ) { g_process.shutdownAbort(true); }

	// set other fields besides key
	sreq->m_firstIp              = m_firstIp;
	sreq->m_hostHash32           = m_hostHash32a;
	//sreq->m_domHash32            = m_domHash32;
	//sreq->m_siteNumInlinks       = m_siteNumInlinks;
	//sreq->m_pageNumInlinks     = m_pageNumInlinks;
	sreq->m_hopCount             = m_hopCount;

	sreq->m_pageNumInlinks       = 0;//m_sreq.m_parentFirstIp;

	Url *fu = getFirstUrl();

	sreq->m_isAddUrl             = 0;//m_isAddUrl;
	sreq->m_isPingServer         = fu->isPingServer();
	//sreq->m_isUrlPermalinkFormat = m_isUrlPermalinkFormat;

	// transcribe from old spider rec, stuff should be the same
	sreq->m_addedTime            = m_firstIndexedDate;

	// validate the stuff so getUrlFilterNum() acks it
	sreq->m_hopCountValid = 1;

	// we need this now for ucp ucr upp upr new url filters that do
	// substring matching on the url
	if ( m_firstUrlValid )
		strcpy(sreq->m_url,m_firstUrl.getUrl());

	// re-make the key since it contains m_firstIp
	long long uh48 = fu->getUrlHash48();
	// set the key properly to reflect the new "first ip"
	// since we shard spiderdb by that.
	sreq->m_key = g_spiderdb.makeKey ( m_firstIp, uh48, true, 0LL, false );
	sreq->setDataSize();
}

////////////////////////////////////////////////////////////////////
//   THIS IS THE HEART OF HOW THE PARSER ADDS TO THE RDBS
////////////////////////////////////////////////////////////////////

// . returns false if blocked, true otherwise
// . sets g_errno on error and returns true
// . this is now a WRAPPER for indexDoc2() and it will deal with
//   g_errnos by adding an error spider reply so we offload the
//   logic to the url filters table
bool XmlDoc::indexDoc ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// return from the msg4.addMetaList() below?
	if ( m_msg4Launched ) {
		// must have been waiting
		if ( ! m_msg4Waiting ) {
			g_process.shutdownAbort(true);
		}

		logTrace( g_conf.m_logTraceXmlDoc, "END, returning true. m_msg4Launched" );
		return true;
	}

	// return true with g_errno set on error
	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning true. Could not get collection." );
		return true;
	}

	if ( ! m_masterLoop ) {
		m_masterLoop  = indexDocWrapper;
		m_masterState = this;
	}

	// do not index if already indexed and we are importing
	// from the code in PageInject.cpp from a foreign titledb file
	if ( m_isImporting && m_isImportingValid ) {
		char *isIndexed = getIsIndexed();
		if ( ! isIndexed ) {
			log("import: import had error: %s",mstrerror(g_errno));
			logTrace( g_conf.m_logTraceXmlDoc, "END, returning true. Import error." );
			return true;
		}
		if ( isIndexed == (char *)-1)
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, returning false. isIndex = -1" );
			return false;
		}
		if ( *isIndexed ) {
			log("import: skipping import for %s. already indexed.",
			    m_firstUrl.getUrl());
			logTrace( g_conf.m_logTraceXmlDoc, "END, returning true." );
			return true;
		}
	}

	// . even if not using diffbot, keep track of these counts
	// . even if we had something like EFAKEFIRSTIP, OOM, or whatever
	//   it was an attempt we made to crawl this url
	if ( ! m_incrementedAttemptsCount ) {
		// do not repeat
		m_incrementedAttemptsCount = true;
		// log debug
		//log("build: attempted %s count=%" PRId64,m_firstUrl.getUrl(),
		//    cr->m_localCrawlInfo.m_pageDownloadAttempts);
		// this is just how many urls we tried to index
		//cr->m_localCrawlInfo.m_urlsConsidered++;
		// avoid counting if it is a fake first ip
		bool countIt = true;
		// pagereindex.cpp sets this as does any add url (bulk job)
		if ( m_sreqValid && m_sreq.m_fakeFirstIp )
			countIt = false;
		if ( countIt ) {
			cr->m_localCrawlInfo.m_pageDownloadAttempts++;
			cr->m_globalCrawlInfo.m_pageDownloadAttempts++;
			// changing status, resend local crawl info to all
			cr->localCrawlInfoUpdate();
		}
		// need to save collection rec now during auto save
		cr->m_needsSave = true;
		// update this just in case we are the last url crawled
		//int64_t now = gettimeofdayInMillisecondsGlobal();
		//cr->m_diffbotCrawlEndTime = now;
	}


	bool status = true;

	if ( ! g_errno ) status = indexDoc2 ( );

	// blocked?
	if ( ! status )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return false, indexDoc2 blocked" );
		return false;
	}

	// done with no error?
	bool success = true;
	if ( g_errno ) success = false;
	// if we were trying to spider a fakefirstip request then
	// pass through because we lookup the real firstip below and
	// add a new request as well as a reply for this one
	if ( m_indexCodeValid && m_indexCode == EFAKEFIRSTIP ) success = false;

	if ( success )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true, success!" );
		return true;
	}

	// . ignore failed child docs like diffbot pages
	// . they are getting EMALFORMEDSECTIONS
	if ( m_isChildDoc ) {
		log("build: done indexing child doc. error=%s. not adding "
		    "spider reply for %s",
		    mstrerror(g_errno),
		    m_firstUrl.getUrl());
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true, indexed child doc" );
		return true;
	}

	///
	// otherwise, an internal error. we must add a SpiderReply
	// to spiderdb to release the lock.
	///

 logErr:

	if ( m_firstUrlValid && g_errno )
		log("build: %s had internal error = %s. adding spider "
		    "error reply.",
		    m_firstUrl.getUrl(),mstrerror(g_errno));
	else if ( g_errno )
		log("build: docid=%" PRId64" had internal error = %s. "
		    "adding spider error reply.",
		    m_docId,mstrerror(g_errno));

	// seems like this was causing a core somehow...
	if ( g_errno == ENOMEM )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true, ENOMEM" );
		return true;
	}

	// and do not add spider reply if shutting down the server
	if ( g_errno == ESHUTTINGDOWN )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true, ESHUTTINGDOWN" );
		return true;
	}

	// i saw this on shard 9, how is it happening
	if ( g_errno == EBADRDBID )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true, EBADRDBID" );
		return true;
	}

	// if docid not found when trying to do a query reindex...
	// this really shouldn't happen but i think we were adding
	// additional SpiderRequests since we were using a fake first ip.
	// but i have since fixed that code. so if the titlerec was not
	// found when trying to do a force delete... it's not a temporary
	// error and should not be retried. if we set indexCode to
	// EINTERNALERROR it seems to be retried.
	if ( g_errno == ENOTFOUND ) {
		m_indexCode = g_errno;
		m_indexCodeValid = true;
	}

	// this should not be retired either. i am seeing it excessively
	// retried from a
	// "TitleRec::set: uncompress uncompressed size=-2119348471"
	// error condition. it also said
	// "Error spidering for doc http://www.... : Bad cached document"
	if ( g_errno == EBADTITLEREC ) {
		m_indexCode = g_errno;
		m_indexCodeValid = true;
	}

	// i've seen Multicast got error in reply from hostId 19 (msgType=0x22
	// transId=496026 nice=1 net=default): Buf too small.
	// so fix that with this
	if ( g_errno == EBUFTOOSMALL ) {
		m_indexCode = g_errno;
		m_indexCodeValid = true;
	}

	if ( g_errno == EBADURL ) {
		m_indexCode = g_errno;
		m_indexCodeValid = true;
	}

	if ( g_errno == ENOTITLEREC ) {
		m_indexCode = g_errno;
		m_indexCodeValid = true;
	}

	// default to internal error which will be retried forever otherwise
	if ( ! m_indexCodeValid ) {
		m_indexCode = EINTERNALERROR;//g_errno;
		m_indexCodeValid = true;
	}

	// if our spiderrequest had a fake "firstip" so that it could be
	// injected quickly into spiderdb, then do the firstip lookup here
	// and re-add the new spider request with that, and add the reply
	// to the fake firstip request below.
	if ( m_indexCodeValid && m_indexCode == EFAKEFIRSTIP ) {
		// at least get this if possible
		int32_t *fip = getFirstIp();
		if ( fip == (void *) -1 ) return false;
		// error? g_errno will be changed if this is NULL
		if ( ! fip ) {
			log("build: error getting real firstip: %s",
			    mstrerror(g_errno));
			m_indexCode = EINTERNALERROR;
			m_indexCodeValid = true;
			goto logErr;
		}
		// sanity log
		if ( ! m_firstIpValid ) { g_process.shutdownAbort(true); }
		// sanity log
		if ( *fip == 0 || *fip == -1 ) {
			//
			// now add a spider status doc for this so we know
			// why a crawl might have failed to start
			//
			// save this
			int32_t saved = m_indexCode;
			// make it the real reason for the spider status doc
			m_indexCode = EDNSERROR;
			// get the spiderreply ready to be added. false=del
			SafeBuf *ssDocMetaList =getSpiderStatusDocMetaList(NULL ,false);
			// revert
			m_indexCode = saved;
			// error?
			if ( ! ssDocMetaList ) return true;
			// blocked?
			if ( ssDocMetaList == (void *)-1 ) return false;
			// need to alloc space for it too
			char *list = ssDocMetaList->getBufStart();
			int32_t len = ssDocMetaList->length();
			//needx += len;
			// this too
			m_addedStatusDocSize = len;
			m_addedStatusDocSizeValid = true;

			const char *url = "unknown";
			if ( m_sreqValid ) url = m_sreq.m_url;
			log("build: error2 getting real firstip of "
			    "%" PRId32" for "
			    "%s. Not adding new spider req. "
			    "spiderstatusdocsize=%" PRId32, (int32_t)*fip,url,
			    m_addedStatusDocSize);
			// also count it as a crawl attempt
			cr->m_localCrawlInfo.m_pageDownloadAttempts++;
			cr->m_globalCrawlInfo.m_pageDownloadAttempts++;

			if ( ! m_metaList2.safeMemcpy ( list , len ) )
			{
				logTrace( g_conf.m_logTraceXmlDoc, "END, return true, metaList2 safeMemcpy returned false" );
				return true;
			}

			goto skipNewAdd1;
		}
		// store the new request (store reply for this below)
		rdbid_t rd = RDB_SPIDERDB;
		if ( m_useSecondaryRdbs ) rd = RDB2_SPIDERDB2;
		if ( ! m_metaList2.pushChar(rd) )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return true, metaList2 pushChar returned false" );
			return true;
		}
		// store it here
		SpiderRequest revisedReq;
		// this fills it in
		getRevisedSpiderRequest ( &revisedReq );
		// and store that new request for adding
		if ( ! m_metaList2.safeMemcpy (&revisedReq,revisedReq.getRecSize()))
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return true, metaList2 safeMemcpy returned false" );
			return true;
		}
		// make sure to log the size of the spider request
		m_addedSpiderRequestSize = revisedReq.getRecSize();
		m_addedSpiderRequestSizeValid = true;
	}

 skipNewAdd1:

	SpiderReply *nsr = NULL;

	// if only rebuilding posdb do not rebuild spiderdb
	if ( m_useSpiderdb ) {

		////
		//
		// make these fake so getNewSpiderReply() below does not block
		//
		////
		nsr = getFakeSpiderReply (  );
		// this can be NULL and g_errno set to ENOCOLLREC or something
		if ( ! nsr )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return true, getFakeSpiderReply returned false" );
			return true;
		}

		//SafeBuf metaList;

		rdbid_t rd = RDB_SPIDERDB;
		if ( m_useSecondaryRdbs ) rd = RDB2_SPIDERDB2;
		if ( ! m_metaList2.pushChar( rd ) )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return true, metaList2 pushChar returned false" );
			return true;
		}

		if ( ! m_metaList2.safeMemcpy ( (char *)nsr,nsr->getRecSize()))
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return true, metaList2 safeMemcpy returned false" );
			return true;
		}

		m_addedSpiderReplySize = nsr->getRecSize();
		m_addedSpiderReplySizeValid = true;
	}


	m_msg4Launched = true;

	// display the url that had the error
	logIt();

	// log this for debug now
	if ( nsr ) {
#ifdef _VALGRIND_
		VALGRIND_CHECK_MEM_IS_DEFINED(nsr, sizeof(*nsr));
#endif
		SafeBuf tmp;
		nsr->print(&tmp);
		log("xmldoc: added reply %s",tmp.getBufStart());
	}

	// clear g_errno
	g_errno = 0;

	// "cr" might have been deleted by calling indexDoc() above i think
	// so use collnum here, not "cr"
	if (!m_msg4.addMetaList(&m_metaList2, m_collnum, m_masterState, m_masterLoop)) {
		m_msg4Waiting = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, return false, m_msg4.addMetaList returned false" );
		return false;
	}

	//logf(LOG_DEBUG,"build: msg4 meta add3 did NOT block" );

	m_msg4Launched = false;

	logTrace( g_conf.m_logTraceXmlDoc, "END, return true, all done" );
	// all done
	return true;
}



// . returns false if blocked, true otherwise
// . sets g_errno on error and returns true
bool XmlDoc::indexDoc2 ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// if anything blocks, this will be called when it comes back
	if ( ! m_masterLoop ) {
		m_masterLoop  = indexDocWrapper;
		m_masterState = this;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true. Could not get collection." );
		return true;
	}


	// do this before we increment pageDownloadAttempts below so that
	// john's smoke tests, which use those counts, are not affected
	if ( m_sreqValid && m_sreq.m_fakeFirstIp &&
	     // only do for add url, not for injects. injects expect
	     // the doc to be indexed while the browser waits. add url
	     // is really just adding the spider request and returning
	     // to the browser without delay.
	     ! m_sreq.m_isInjecting &&
	     // not for page reindexes either!
	     ! m_sreq.m_isPageReindex &&
	     // just add url
	     m_sreq.m_isAddUrl ) {
		m_indexCodeValid = true;
		m_indexCode = EFAKEFIRSTIP;

		logTrace( g_conf.m_logTraceXmlDoc, "END, return true. Set indexCode EFAKEFIRSTIP" );
		return true;
	}

	setStatus("indexing doc");

	// maybe a callback had g_errno set?
	if ( g_errno )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END. return true, g_errno set (%" PRId32")",g_errno);
		return true;
	}

	// . now get the meta list from it to add
	// . returns NULL and sets g_errno on error
	char *metaList = getMetaList ( );

	// error?
	if ( ! metaList ) {
		// sanity check. g_errno must be set
		if ( ! g_errno ) {
			log("build: Error UNKNOWN error spidering. setting "
			    "to bad engineer.");
			g_errno = EBADENGINEER;
			//g_process.shutdownAbort(true); }
		}
		log("build: Error spidering for doc %s: %s",
		    m_firstUrl.getUrl(),mstrerror(g_errno));
		logTrace( g_conf.m_logTraceXmlDoc, "END, return true. getMetaList returned false" );
		return true;
	}
	// did it block? return false if so, we will be recalled since
	// we set m_masterLoop to indexDoc
	if ( metaList == (char *) -1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return false. metaList = -1" );
		return false;
	}


	// must be valid
	int32_t *indexCode = getIndexCode();
	if (! indexCode || indexCode == (void *)-1)
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return %s based on indexCode", (bool*)indexCode?"true":"false");
		return (char *)indexCode;
	}

	// . check to make sure the parser is consistent so we can cleanly
	//   delete the various rdb records if we need to in the future solely
	//   based on the titleRec.
	// . force = false
	// . unless we force it, the test is only done at random intervals
	//   for performance reasons
	if ( ! *indexCode ) doConsistencyTest ( false );
	// ignore errors from that
	g_errno = 0;


	// unregister any sleep callback
	if ( m_registeredSleepCallback ) {
		g_loop.unregisterSleepCallback(m_masterState,indexDocWrapper2);
		m_registeredSleepCallback = false;
	}

	// now add it
	if ( ! m_listAdded && m_metaListSize ) {
		// only call this once
		m_listAdded = true;
		// show it for now
		//printMetaList(m_metaList , m_metaList + m_metaListSize,NULL);
		// test it
		verifyMetaList ( m_metaList , m_metaList + m_metaListSize , false );

		// do it
		if (!m_msg4.addMetaList(m_metaList, m_metaListSize, m_collnum, m_masterState, m_masterLoop)) {
			m_msg4Waiting = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, return false. addMetaList blocked" );
			return false;
		}

		// error with msg4? bail
		if ( g_errno ) {
			bool rc= logIt();
			logTrace( g_conf.m_logTraceXmlDoc, "END, return %s. g_errno %" PRId32" after addMetaList", rc?"true":"false", g_errno);
			return rc;
		}

	}

	// make sure our msg4 is no longer in the linked list!
	if (m_msg4Waiting && Msg4::isInLinkedList(&m_msg4)){
		g_process.shutdownAbort(true);
	}

	// we are not waiting for the msg4 to return
	m_msg4Waiting = false;

	// there used to be logic here to flush injections, but it was disabled to make things faster
	// flush it if we are injecting it in case the next thing we spider is dependent on this one
	// remove in commit d23858c92d0d715d493a358ea69ecf77a5cc00fc

	bool rc2 = logIt();
	logTrace( g_conf.m_logTraceXmlDoc, "END, all done. Returning %s", rc2?"true":"false");

	return rc2;
}

#if 0
static void doneReadingArchiveFileWrapper ( int fd, void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// . go back to the main entry function
	// . make sure g_errno is clear from a msg3a g_errno before calling
	//   this lest it abandon the loop

	THIS->m_masterLoop ( THIS->m_masterState );
}
#endif


static void getTitleRecBufWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// note it
	THIS->setStatus ( "in get title rec wrapper" );
	// return if it blocked
	if ( THIS->getTitleRecBuf() == (void *)-1 ) return;
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}

key96_t *XmlDoc::getTitleRecKey() {
	if ( m_titleRecBufValid ) return &m_titleRecKey;
	SafeBuf *tr = getTitleRecBuf();
	if ( ! tr || tr == (void *)-1 ) return (key96_t *)tr;
	return &m_titleRecKey;
}

// . return NULL and sets g_errno on error
// . returns -1 if blocked
int32_t *XmlDoc::getIndexCode ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	// return it now if we got it already
	if ( m_indexCodeValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid: %" PRId32, m_indexCode);
		return &m_indexCode;
	}

	setStatus ( "getting index code");

	// page inject can set deletefromindex to true
	if ( m_deleteFromIndex ) {
		m_indexCode = EDOCFORCEDELETE;
		m_indexCodeValid = true;

		logTrace( g_conf.m_logTraceXmlDoc, "END, delete operation. Returning EDOCFORCEDELETE" );;
		return &m_indexCode;
	}

	if ( ! m_firstUrlValid ) { g_process.shutdownAbort(true); }

	if ( m_firstUrl.getUrlLen() <= 5 ) {
		m_indexCode = EBADURL;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADURL. FirstURL len too short" );;
		return &m_indexCode;
	}

	if ( m_firstUrl.getUrlLen() + 1 >= MAX_URL_LEN ) {
		m_indexCode      = EURLTOOLONG;
		m_indexCodeValid = true;

		logTrace( g_conf.m_logTraceXmlDoc, "END, EURLTOOLONG" );;
		return &m_indexCode;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. Could not get collection." );;
		return NULL;
	}

	// "url is repeating path components" error?
	if ( ! m_check1 ) {
		m_check1         = true;
		if ( m_firstUrl.isLinkLoop() ) {
			m_indexCode      = ELINKLOOP;
			m_indexCodeValid = true;

			logTrace( g_conf.m_logTraceXmlDoc, "END, ELINKLOOP" );;
			return &m_indexCode;
		}
	}

	// fix for "http://.xyz.com/...."
	if ( m_firstUrl.getHost() && m_firstUrl.getHost()[0] == '.' ) {
		m_indexCode      = EBADURL;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADURL (URL first char is .)" );;
		return &m_indexCode;
	}

	if ( cr->m_doUrlSpamCheck && ! m_check2 ) {
		m_check2         = true;
		if ( m_firstUrl.isSpam() ) {
			m_indexCode      = EDOCURLSPAM;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCURLSPAM" );;
			return &m_indexCode;
		}
	}

	// . don't spider robots.txt urls for indexing!
	// . quickly see if we are a robots.txt url originally
	int32_t fulen = getFirstUrl()->getUrlLen();
	char *fu   = getFirstUrl()->getUrl();
	char *fp   = fu + fulen - 11;
	if ( fulen > 12 &&
	     fp[1] == 'r' &&
	     ! strncmp ( fu + fulen - 11 , "/robots.txt" , 11 )) {
		m_indexCode = EBADURL;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADURL (robots.txt)" );;
		return &m_indexCode;
	}

	// if this is an injection and "newonly" is not zero then we
	// only want to do the injection if the url is "new", meaning not
	// already indexed. "m_wasContentInjected" will be true if this is
	// an injection. "m_newOnly" will be true if the injector only
	// wants to proceed with the injection if this url is not already
	// indexed.
	if ( m_wasContentInjected && m_newOnly ) {
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (XmlDoc **)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END return error, getOldXmlDoc failed" );;
			return (int32_t *)pod;
		}
		XmlDoc *od = *pod;
		// if the old doc does exist and WAS NOT INJECTED itself
		// then abandon this injection. it was spidered the old
		// fashioned way and we want to preserve it and NOT overwrite
		// it with this injection.
		if ( od && ! od->m_wasContentInjected ) {
			m_indexCode = EABANDONED;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EABANDONED" );;
			return &m_indexCode;
		}
		// if it was injected itself, only abandon this injection
		// in the special case that m_newOnly is "1". otherwise
		// if m_newOnly is 2 then we will overwrite any existing
		// titlerecs that were not injected themselves.
		if ( od && od->m_wasContentInjected && m_newOnly == 1 ) {
			m_indexCode = EABANDONED;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EABANDONED (2)" );;
			return &m_indexCode;
		}

	}

	// need tagrec to see if banned
	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 ) return (int32_t *)gr;
	// this is an automatic ban!
	if ( gr->getLong("manualban",0) ) {
		m_indexCode = EDOCBANNED;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCBANNED" );;
		return &m_indexCode;
	}


	// get the ip of the current url
	int32_t *ip = getIp ( );
	if ( ! ip || ip == (int32_t *)-1 ) return (int32_t *)ip;
	if ( *ip == 0 ) {
		m_indexCode      = EBADIP;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADIP" );;
		return &m_indexCode;
	}

	// . check robots.txt
	// . uses the curernt url
	// . if we end in /robots.txt then this quickly returns true
	// . no, we still might want to index if we got link text, so just
	//   check this again below
	bool *isAllowed = getIsAllowed();
	if ( ! isAllowed || isAllowed == (void *)-1) return (int32_t *)isAllowed;

	// . TCPTIMEDOUT, NOROUTETOHOST, EDOCUNCHANGED, etc.
	// . this will be the reply from diffbot.com if using diffbot
	int32_t *dstatus = getDownloadStatus();
	if ( ! dstatus || dstatus == (void *)-1 ) return (int32_t *)dstatus;
	if ( *dstatus ) {
		m_indexCode      = *dstatus;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, %" PRId32" (getDownloadStatus)", m_indexCode);
		return &m_indexCode;
	}

	// check the mime
	HttpMime *mime = getMime();
	if ( ! mime || mime == (HttpMime *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, error. Could not getMime" );;
		return (int32_t *)mime;
	}

	// check redir url
	Url **redirp = getRedirUrl();
	if ( ! redirp || redirp == (void *)-1 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, could not getRedirUrl" );;
		return (int32_t *)redirp;
	}

	// this must be valid now
	if ( ! m_redirErrorValid ) { g_process.shutdownAbort(true); }
	if ( m_redirError ) {
		m_indexCode      = m_redirError;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, redirError (%" PRId32")", m_indexCode);
		return &m_indexCode;
	}

	int64_t *d = getDocId();
	if ( ! d || d == (void *)-1 ) return (int32_t *)d;
	if ( *d == 0LL ) {
		m_indexCode      = ENODOCID;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, ENODOCID" );;
		return &m_indexCode;
	}

	// . is the same url but with a www. present already in titledb?
	// . example: if we are xyz.com and www.xyz.com is already in titledb
	//   then nuke ourselves by setting m_indexCode to EDOCDUPWWW
	char *isWWWDup = getIsWWWDup ();
	if ( ! isWWWDup || isWWWDup == (char *)-1) return (int32_t *)isWWWDup;
	if ( *isWWWDup ) {
		m_indexCode      = EDOCDUPWWW;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCDUPWWW" );;
		return &m_indexCode;
	}


	uint16_t *charset = getCharset();
	if ( ! charset && g_errno == EBADCHARSET ) {
		g_errno = 0;
		m_indexCode      = EBADCHARSET;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADCHARSET" );;
		return &m_indexCode;
	}

	if ( ! charset || charset == (void *)-1) return (int32_t *)charset;
	// we had a 2024 for charset come back and that had a NULL
	// get_charset_str() but it was not supported
	if ( ! supportedCharset(*charset) ) { //&&get_charset_str(*charset) ) {
		m_indexCode      = EBADCHARSET;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADCHARSET (2)" );;
		return &m_indexCode;
	}

	// get local link info
	LinkInfo   *info1 = getLinkInfo1();
	if ( ! info1 || info1 == (LinkInfo *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinkInfo1 failed" );;
		return (int32_t *)info1;
	}

	// if robots.txt said no, and if we had no link text, then give up
	bool disallowed = !( *isAllowed );
	if ( info1 && info1->hasLinkText() ) {
		disallowed = false;
	}
	// if we generated a new sitenuminlinks to store in tagdb, we might
	// want to add this for that only reason... consider!
	if ( disallowed ) {
		m_indexCode      = EDOCDISALLOWED;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCDISALLOWED" );;
		return &m_indexCode;
	}

	// check for bad url extension, like .jpg
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, error getCurrentUrl" );;
		return (int32_t *)cu;
	}

	// take this check out because it is hurting
	// http://community.spiceworks.com/profile/show/Mr.T
	// because 't' was in the list of bad extensions.
	// now we use the url filters table to exclude the extensions we want.
	// and we use the 'ismedia' directive to exclude common media
	// extensions. having this check here is no longer needed and confusing
	// BUT on the otherhand stuff like .exe .rpm .deb is good to avoid!
	// so i'll just edit the list to remove more ambiguous extensions
	// like .f and .t
	//
	bool badExt = cu->hasNonIndexableExtension(TITLEREC_CURRENT_VERSION);	// @todo BR: For now ignore actual TitleDB version. // m_version);
	if ( badExt && ! info1->hasLinkText() ) {
	 	m_indexCode      = EDOCBADCONTENTTYPE;
	 	m_indexCodeValid = true;
	 	logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCBADCONTENTTYPE" );;
	 	return &m_indexCode;
	}

	int16_t *hstatus = getHttpStatus();
	if ( ! hstatus || hstatus == (void *)-1 ) return (int32_t *)hstatus;
	if ( *hstatus != 200 ) {
		m_indexCode      = EDOCBADHTTPSTATUS;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADHTTPSTATUS (%d)", *hstatus);
		return &m_indexCode;
	}

	// check for EDOCISERRPG (custom error pages)
	char *isErrorPage = getIsErrorPage();
	if ( !isErrorPage||isErrorPage==(void *)-1)
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getIsErrorPage failed" );;
		return (int32_t *)isErrorPage;
	}

	if ( *isErrorPage ) {
		m_indexCode      = EDOCISERRPG;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCISERRPG" );;
		return &m_indexCode;
	}

	// . i moved this up to perhaps fix problems of two dup pages being
	//   downloaded at about the same time
	// . are we a dup of another doc from any other site already indexed?
	char *isDup = getIsDup();
	if ( ! isDup || isDup == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getIsDup failed" );;
		return (int32_t *)isDup;
	}
	if ( *isDup ) {
		m_indexCode      = EDOCDUP;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCDUP" );;
		return &m_indexCode;
	}

	// . is a non-canonical page that have <link ahref=xxx rel=canonical>
	// . also sets m_canonicanlUrl.m_url to it if we are not
	// . returns NULL if we are the canonical url
	// . do not do this check if the page was injected
	bool checkCanonical = true;
	if ( m_wasContentInjected ) checkCanonical = false;
	if ( m_isInjecting && m_isInjectingValid ) checkCanonical = false;
	// do not do canonical deletion if recycling content either i guess
	if ( m_sreqValid && m_sreq.m_recycleContent ) checkCanonical = false;
	// do not delete from being canonical if doing a query reindex
	if ( m_sreqValid && m_sreq.m_isPageReindex ) checkCanonical = false;
	if ( checkCanonical ) {
		Url **canon = getCanonicalRedirUrl();
		if ( ! canon || canon == (void *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, getCanonicalRedirUrl failed" );;
			return (int32_t *)canon;
		}

		// if there is one then we are it's leaf, it is the primary
		// page so we should not index ourselves
		if ( *canon ) {
			m_indexCode = EDOCNONCANONICAL;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCNONCANONICAL" );;
			return &m_indexCode;
		}
	}

	// was page unchanged since last time we downloaded it?
	XmlDoc **pod = getOldXmlDoc ( );
	if ( ! pod || pod == (XmlDoc **)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getOldXmlDoc failed" );;
		return (int32_t *)pod;
	}

	XmlDoc *od = NULL;
	if ( *pod ) od = *pod;

	// if recycling content is true you gotta have an old title rec.
	if ( ! od && m_recycleContent ) {
		m_indexCode = ENOTITLEREC;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, ENOTITLEREC" );;
		return &m_indexCode;
	}

	bool check = true;
	if ( ! od ) check = false;

	// or if recycling content turn this off as well! otherwise
	// it will always be 100% the same
	if ( m_recycleContent )
		check = false;

	if ( check ) {
		// check inlinks now too!
		LinkInfo  *info1 = getLinkInfo1 ();
		if ( ! info1 || info1 == (LinkInfo *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END error, getLinkInfo1 failed" );;
			return (int32_t *)info1;
		}

		LinkInfo  *info2 = od->getLinkInfo1 ();
		if ( ! info2 || info2 == (LinkInfo *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END error, getLinkInfo1 (od) failed" );;
			return (int32_t *)info2;
		}

		Inlink *k1 = NULL;
		Inlink *k2 = NULL;
		char *s1, *s2;
		int32_t len1,len2;
		if ( info1->getNumGoodInlinks() !=
		     info2->getNumGoodInlinks() )
			goto changed;
		for ( ; k1=info1->getNextInlink(k1) ,
			      k2=info2->getNextInlink(k2); ) {
			if ( ! k1 )
				break;
			if ( ! k2 )
				break;
			if ( k1->m_siteNumInlinks != k2->m_siteNumInlinks )
				goto changed;
			s1   = k1->getLinkText();
			len1 = k1->size_linkText - 1; // exclude \0
			s2   = k2->getLinkText();
			len2 = k2->size_linkText - 1; // exclude \0
			if ( len1 != len2 )
				goto changed;
			if ( len1 > 0 && memcmp(s1,s2,len1) != 0 )
				goto changed;
		}
		// no change in link text, look for change in page content now
		int32_t *ch32 = getContentHash32();
		if ( ! ch32 || ch32 == (void *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END error, getContentHash32 failed" );;
			return (int32_t *)ch32;
		}

		if ( *ch32 == od->m_contentHash32 ) {
			m_indexCode = EDOCUNCHANGED;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCUNCHANGED" );;
			return &m_indexCode;
		}
	}

 changed:
	// words
	Words *words = getWords();
	if ( ! words || words == (Words *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END error, getWords failed" );;
		return (int32_t *)words;
	}

	// we set the D_IS_IN_DATE flag for these bits
	Bits *bits = getBits();
	if ( ! bits )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END error, getBits failed" );;
		return NULL;
	}


	// bad sections? fixes http://www.beerexpedition.com/northamerica.shtml
	// being continuously respidered when its lock expires every
	// MAX_LOCK_AGE seconds
	Sections *sections = getSections();
	// on EBUFOVERFLOW we will NEVER be able to parse this url
	// correctly so do not retry!
	if ( ! sections && g_errno == EBUFOVERFLOW ) {
		g_errno = 0;
		m_indexCode      = EBUFOVERFLOW;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBUFOVERFLOW (Sections)" );;
		return &m_indexCode;
	}
	if (!sections||sections==(Sections *)-1)
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END error, getSections failed" );;
		return (int32_t *)sections;
	}

	if ( sections->m_numSections == 0 && words->getNumWords() > 0 ) {
		m_indexCode      = EDOCBADSECTIONS;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCBADSECTIONS" );;
		return &m_indexCode;
	}

	// i think an oom error is not being caught by Sections.cpp properly
	if ( g_errno ) { g_process.shutdownAbort(true); }


	// are we a root?
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END error, getIsSiteRoot failed" );;
		return (int32_t *)isRoot;
	}

	bool spamCheck = true;

	// if we are a root, allow repeat spam
	if ( *isRoot ) spamCheck = false;

	// if we are being spidered deep, allow repeat spam
	if ( gr->getLong("deep",0) ) {
		spamCheck = false;
	}

	// only html for now
	if ( m_contentTypeValid && m_contentType != CT_HTML ) spamCheck =false;

	// turn this off for now
	spamCheck = false;

	// otherwise, check the weights
	if ( spamCheck ) {
		char *ws = getWordSpamVec();
		if ( ! ws || ws == (void *)-1 ) return (int32_t *)ws;
		if ( m_isRepeatSpammer ) {
			m_indexCode      = EDOCREPEATSPAMMER;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCREPEATSPAMMER" );;
			return &m_indexCode;
		}
	}

	// validate this here so getSpiderPriority(), which calls
	// getUrlFilterNum(), which calls getNewSpiderReply(), which calls
	// us, getIndexCode() does not repeat all this junk
	//m_indexCodeValid = true;
	//m_indexCode      = 0;

	// this needs to be last!
	int32_t *priority = getSpiderPriority();
	if ( ! priority || priority == (void *)-1) {
		// allow this though
		if ( g_errno == EBUFOVERFLOW ) {
			g_errno = 0;
			m_indexCode      = EBUFOVERFLOW;
			m_indexCodeValid = true;
			logTrace( g_conf.m_logTraceXmlDoc, "END, EBUFOVERFLOW (getSpiderPriority)" );;
			return &m_indexCode;
		}
		// but if it blocked, then un-validate it
		m_indexCodeValid = false;
		// and return to be called again i hope
		logTrace( g_conf.m_logTraceXmlDoc, "END, getSpiderPriority blocked" );;
		return (int32_t *)priority;
	}

	if ( *priority  == -3 ) { // SPIDER_PRIORITY_FILTERED ) {
		m_indexCode      = EDOCFILTERED;
		m_indexCodeValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCFILTERED" );;
		return &m_indexCode;
	}

	// if ( *priority  == SPIDER_PRIORITY_BANNED ) {
	// 	m_indexCode      = EDOCBANNED;
	// 	m_indexCodeValid = true;
	// 	return &m_indexCode;
	// }

	// no error otherwise
	m_indexCode      = 0;
	m_indexCodeValid = true;
	logTrace( g_conf.m_logTraceXmlDoc, "END." );;
	return &m_indexCode;
}



char *XmlDoc::prepareToMakeTitleRec ( ) {
	// do not re-call this for speed
	if ( m_prepared ) return (char *)1;

	int32_t *indexCode = getIndexCode();
	if (! indexCode || indexCode == (void *)-1) return (char *)indexCode;
	if ( *indexCode ) { m_prepared = true; return (char *)1; }

	//
	// do all the sets here
	//

	// . this gets our old doc from titledb, if we got it
	// . TODO: make sure this is cached in the event of a backoff, we
	//   will redo this again!!! IMPORTANT!!!
	char *isIndexed = getIsIndexed();
	if ( ! isIndexed || isIndexed == (char *)-1) return (char *)isIndexed;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// get our site root
	char *mysite = getSite();
	if ( ! mysite || mysite == (void *)-1 ) return (char *)mysite;

	uint8_t *langId = getLangId();
	if ( ! langId || langId == (uint8_t *)-1 ) return (char *) langId;

	getHostHash32a();
	getContentHash32();

	char **id = getThumbnailData();
	if ( ! id || id == (void *)-1 ) return (char *)id;

	int8_t *hopCount = getHopCount();
	if ( ! hopCount || hopCount == (void *)-1 ) return (char *)hopCount;

	char *spiderLinks = getSpiderLinks();
	if ( ! spiderLinks || spiderLinks == (char *)-1 )
		return (char *)spiderLinks;


	int32_t *firstIndexedDate = getFirstIndexedDate();
	if ( ! firstIndexedDate || firstIndexedDate == (int32_t *)-1 )
		return (char *)firstIndexedDate;

	int32_t *outlinksAddedDate = getOutlinksAddedDate();
	if ( ! outlinksAddedDate || outlinksAddedDate == (int32_t *)-1 )
		return (char *)outlinksAddedDate;

	uint16_t *countryId = getCountryId();
	if ( ! countryId||countryId==(uint16_t *)-1) return (char *)countryId;

	char *trunc = getIsContentTruncated();
	if ( ! trunc || trunc == (char *)-1 ) return (char *)trunc;

	char *pl = getIsPermalink();
	if ( ! pl || pl == (char *)-1 ) return (char *)pl;

	//int32_t *numBannedOutlinks = getNumBannedOutlinks();
	// set this
	//m_numBannedOutlinks8 = score32to8 ( *numBannedOutlinks );

	// . before storing this into title Rec, make sure all tags
	//   are valid and tagRec is up to date
	// . like we might need to update the siteNumInlinks,
	//   or other tags because, for instance, contact info might not
	//   be in there because isSpam() never required it.
	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 ) return (char *)sni;
	char *ict = getIsContentTruncated();
	if ( ! ict || ict == (char *)-1 ) return (char *)ict;
	char *at = getIsAdult();
	if ( ! at || at == (void *)-1 ) return (char *)at;
	char *ls = getIsLinkSpam();
	if ( ! ls || ls == (void *)-1 ) return (char *)ls;
	uint32_t *tph = getTagPairHash32();
	if ( ! tph || tph == (uint32_t *)-1 ) return (char *)tph;

	m_prepared = true;
	return (char *)1;
}

// . create and store the titlerec into "buf".
// . it is basically the header part of all the member vars in this XmlDoc.
// . it has a key,dataSize,compressedData so it can be a record in an Rdb
// . return true on success, false on failure
bool XmlDoc::setTitleRecBuf ( SafeBuf *tbuf, int64_t docId, int64_t uh48 ){

	//setStatus ( "making title rec");

	// assume could not make one because we were banned or something
	tbuf->purge(); // m_titleRec = NULL;

	// start seting members in THIS's header before compression
	m_version           = TITLEREC_CURRENT_VERSION;

	// set this
	m_headerSize = (char *)&ptr_firstUrl - (char *)&m_headerSize;

	// add in variable length data
	int32_t *ps = (int32_t *)&size_firstUrl;
	// data ptr, consider a NULL to mean empty too!
	char **pd = (char **)&ptr_firstUrl;
	// how many XmlDoc::ptr_* members do we have? set "np" to that
	int32_t np = ((char *)&size_firstUrl  - (char *)&ptr_firstUrl) ;
	np /= sizeof(char *);
	// count up total we need to alloc
	int32_t need1 = m_headerSize;
	// clear these
	m_internalFlags1 = 0;
	// loop over em
	for ( int32_t i = 0 ; i < np ; i++ , ps++ , pd++ ) {
		// skip if empty
		if ( *ps <= 0 ) continue;
		// or empty string ptr
		if ( ! *pd ) continue;
		// 4 bytes for the size
		need1 += 4;
		// add it up
		need1 += *ps;
		// make the mask
		uint32_t mask = 1 << i ;
		// add it in
		m_internalFlags1 |= mask;
	}
	// alloc the buffer
	char *ubuf = (char *) mmalloc ( need1 , "xdtrb" );
	// return NULL with g_errno set on error
	if ( ! ubuf ) return false;
	// serialize into it
	char *p = ubuf;
	// copy our crap into there
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&m_headerSize,(size_t)((char*)&ptr_firstUrl-(char*)&m_headerSize));
#endif
	gbmemcpy ( p , &m_headerSize , m_headerSize );
	// skip it
	p += m_headerSize;
	// reset data ptrs
	pd = (char **)&ptr_firstUrl;
	// reset data sizes
	ps = (int32_t *)&size_firstUrl;

	// then variable length data
	for ( int32_t i = 0 ; i < np ; i++ , ps++ , pd++ ) {
		// skip if empty, do not serialize
		if ( ! *ps ) continue;
		// or empty string ptr
		if ( ! *pd ) continue;

		// store size first
		*(int32_t *)p = *ps;
		p += 4;
		// then the data
#ifdef _VALGRIND_
		VALGRIND_CHECK_MEM_IS_DEFINED(*pd,*ps);
#endif
		gbmemcpy ( p , *pd , *ps );
		// skip *ps bytes we wrote. should include a \0
		p += *ps;
	}
	// sanity check
	if ( p != ubuf + need1 ) { g_process.shutdownAbort(true); }

	// . make a buf big enough to hold compressed, we'll realloc afterwards
	// . according to zlib.h line 613 compress buffer must be .1% larger
	//   than source plus 12 bytes. (i add one for round off error)
	// . now i added another extra 12 bytes cuz compress seemed to want it
	int32_t need2 = ((int64_t)need1 * 1001LL) / 1000LL + 13 + 12;

	// we also need to store a key then regular dataSize then
	// the uncompressed size in cbuf before the compression of m_ubuf
	int32_t hdrSize = sizeof(key96_t) + 4 + 4;

	// . now i add 12 bytes more so Msg14.cpp can also squeeze in a
	//   negative key to delete the old titleRec, cuz we use this cbuf
	//   to set our list that we add to our twins with
	// . we now store the negative rec before the positive rec in Msg14.cpp
	//hdrSize += sizeof(key96_t) + 4;
	need2 += hdrSize;

	// return false on error
	if ( ! tbuf->reserve ( need2 ,"titbuf" ) ) return false;

	// shortcut
	char *cbuf = tbuf->getBufStart();

	// . how big is the buf we're passing to ::compress()?
	// . don't include the last 12 byte, save for del key in Msg14.cpp
	int32_t size = need2 - hdrSize ;

	// . uncompress the data into ubuf
	// . this will reset cbufSize to a smaller value probably
	// . "size" is set to how many bytes we wrote into "cbuf + hdrSize"
	int err = gbcompress ( (unsigned char *)cbuf + hdrSize,
			       (uint32_t *)&size,
			       (unsigned char *)ubuf ,
			       (uint32_t  )need1 );

	// free the buf we were trying to compress now
	mfree ( ubuf , need1 , "trub" );

	// we should check ourselves
	if ( err == Z_OK && size > (need2 - hdrSize ) ) {
		tbuf->purge();
		g_errno = ECOMPRESSFAILED;
		log("db: Failed to compress document of %" PRId32" bytes. "
		    "Provided buffer of %" PRId32" bytes.",
		    size, (need2 - hdrSize ) );
		return false;
	}

	// check for error
	if ( err != Z_OK ) {
		tbuf->purge();
		g_errno = ECOMPRESSFAILED;
		log("db: Failed to compress document.");
		return false;
	}

	key96_t tkey = Titledb::makeKey (docId,uh48,false);//delkey?

	// get a ptr to the Rdb record at start of the header
	p = cbuf;

	// . store key in header of cbuf
	// . store in our host byte ordering so we can be a rec in an RdbList
	*(key96_t *) p = tkey;
	p += sizeof(key96_t);

	// store total dataSize in header (excluding itself and key only)
	int32_t dataSize = size + 4;
	*(int32_t  *) p = dataSize ;
	p += 4;

	// store uncompressed size in header
	*(int32_t  *) p = need1 ; p += 4;

	// sanity check
	if ( p != cbuf + hdrSize ) { g_process.shutdownAbort(true); }

	// sanity check
	if ( need1 <= 0 ) { g_process.shutdownAbort(true); }

	// advance over data
	p += size;

	// update safebuf::m_length so it is correct
	tbuf->setLength ( p - cbuf );

	return true;
}

// . return NULL and sets g_errno on error
// . returns -1 if blocked
SafeBuf *XmlDoc::getTitleRecBuf ( ) {

	// return it now if we got it already
	if ( m_titleRecBufValid ) return &m_titleRecBuf;

	setStatus ( "making title rec");

	// did one of our many blocking function calls have an error?
	if ( g_errno ) return NULL;

	// . HACK so that TitleRec::isEmpty() return true
	// . faster than calling m_titleRec.reset()
	//m_titleRec.m_url.m_ulen = 0;

	int32_t *indexCode = getIndexCode();
	// not allowed to block here
	if ( indexCode == (void *)-1) { g_process.shutdownAbort(true); }
	// return on errors with g_errno set
	if ( ! indexCode ) return NULL;
	// force delete? EDOCFORCEDELETE
	if ( *indexCode ) { m_titleRecBufValid = true; return &m_titleRecBuf; }

	// . internal callback
	// . so if any of the functions we end up calling directly or
	//   indirectly block and return -1, we will be re-called from the top
	if ( ! m_masterLoop ) {
		m_masterLoop  = getTitleRecBufWrapper;
		m_masterState = this;
	}


	/////////
	//
	// IF ANY of these validation sanity checks fail then update
	// prepareToMakeTitleRec() so it makes them valid!!!
	//
	/////////

	// verify key parts
	if ( ! m_docIdValid                  ) { g_process.shutdownAbort(true); }

	// verify record parts
	//if ( ! m_versionValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_ipValid                     ) { g_process.shutdownAbort(true); }
	if ( ! m_spideredTimeValid           ) { g_process.shutdownAbort(true); }
	if ( ! m_firstIndexedDateValid       ) { g_process.shutdownAbort(true); }
	if ( ! m_outlinksAddedDateValid      ) { g_process.shutdownAbort(true); }
	if ( ! m_charsetValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_countryIdValid              ) { g_process.shutdownAbort(true); }
	if ( ! m_httpStatusValid             ) { g_process.shutdownAbort(true); }

	if ( ! m_siteNumInlinksValid         ) { g_process.shutdownAbort(true); }

	if ( ! m_hopCountValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_metaListCheckSum8Valid      ) { g_process.shutdownAbort(true); }
	if ( ! m_langIdValid                 ) { g_process.shutdownAbort(true); }
	if ( ! m_contentTypeValid            ) { g_process.shutdownAbort(true); }

	if ( ! m_isRSSValid                  ) { g_process.shutdownAbort(true); }
	if ( ! m_isPermalinkValid            ) { g_process.shutdownAbort(true); }
	if ( ! m_isAdultValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_spiderLinksValid            ) { g_process.shutdownAbort(true); }
	if ( ! m_isContentTruncatedValid     ) { g_process.shutdownAbort(true); }
	if ( ! m_isLinkSpamValid             ) { g_process.shutdownAbort(true); }

	// buffers
	if ( ! m_firstUrlValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_redirUrlValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_tagRecValid                 ) { g_process.shutdownAbort(true); }
	if ( ! m_imageDataValid              ) { g_process.shutdownAbort(true); }
	if ( ! m_recycleContent ) {
		if ( ! m_rawUtf8ContentValid         ) { g_process.shutdownAbort(true); }
		if ( ! m_expandedUtf8ContentValid    ) { g_process.shutdownAbort(true); }
	}
	if ( ! m_utf8ContentValid            ) { g_process.shutdownAbort(true); }
	if ( ! m_siteValid                   ) { g_process.shutdownAbort(true); }
	if ( ! m_linkInfo1Valid              ) { g_process.shutdownAbort(true); }

	// do we need these?
	if ( ! m_hostHash32aValid            ) { g_process.shutdownAbort(true); }
	if ( ! m_contentHash32Valid          ) { g_process.shutdownAbort(true); }
	if ( ! m_tagPairHash32Valid          ) { g_process.shutdownAbort(true); }

	setStatus ( "compressing into final title rec");

	int64_t uh48 = getFirstUrlHash48();

	int64_t *docId = getDocId();

	// time it
	int64_t startTime = gettimeofdayInMilliseconds();

	//////
	//
	// fill in m_titleRecBuf
	//
	//////

	// we need docid and uh48 for making the key of the titleRec
	if ( ! setTitleRecBuf ( &m_titleRecBuf , *docId , uh48 ) )
		return NULL;

	// set this member down here because we can't set it in "xd"
	// because it is too short of an xmldoc stub
	m_versionValid = true;

	// . add the stat
	// . use white for the stat
	g_stats.addStat_r ( 0               ,
			    startTime ,
			    gettimeofdayInMilliseconds(),
			    0x00ffffff );

	char *cbuf = m_titleRecBuf.getBufStart();
	m_titleRecKey = *(key96_t *)cbuf;
	m_titleRecKeyValid = true;

	// now valid. congratulations!
	m_titleRecBufValid   = true;
	return &m_titleRecBuf;
}


// . store this in clusterdb rec so family filter works!
// . check content for adult words
char *XmlDoc::getIsAdult ( ) {

	if ( m_isAdultValid ) return &m_isAdult2;

	// call that
	setStatus ("getting is adult bit");


	// need the content
	char **u8 = getUtf8Content();
	if ( ! u8 || u8 == (char **)-1) return (char *)u8;

	// time it
	int64_t start = gettimeofdayInMilliseconds();

	// score that up
	int32_t total = getAdultPoints ( ptr_utf8Content, size_utf8Content - 1 , m_firstUrl.getUrl() );


	// debug msg
	int64_t took = gettimeofdayInMilliseconds() - start;
	if ( took > 10 )
		logf(LOG_DEBUG,
		     "build: Took %" PRId64" ms to check doc of %" PRId32" bytes for "
		     "dirty words.",took,size_utf8Content-1);

	m_isAdult  = false;
	// adult?
	if ( total >= 2 ) m_isAdult = true;
	// set shadow member
	m_isAdult2 = (bool)m_isAdult;
	// validate
	m_isAdultValid = true;

	// note it
	if ( m_isAdult2 && g_conf.m_logDebugDirty )
		log("dirty: %s points = %" PRId32,m_firstUrl.getUrl(),total);

	// no dirty words found
	return &m_isAdult2;
}



// . sets g_errno on error and returns NULL
// . now returns a ptr to it so we can return NULL to signify error, that way
//   all accessors have equivalent return values
// . an acessor function returns (char *)-1 if it blocked!
char *XmlDoc::getIsPermalink ( ) {
	if ( m_isPermalinkValid ) return &m_isPermalink2;
	Url *url      = getCurrentUrl();
	if ( ! url      ) return NULL;
	char *isRSS   = getIsRSS();
	// return NULL with g_errno set, -1 if blocked
	if ( ! isRSS || isRSS == (char  *)-1 ) return isRSS;
	Links *links  = getLinks();
	// return NULL with g_errno set, -1 if blocked
	if ( ! links || links == (Links *)-1 ) return (char *)links;
	uint8_t  *ct     = getContentType();
	// return NULL with g_errno set, -1 if blocked
	if ( ! ct    || ct    == (uint8_t  *)-1 ) return (char *)ct;
	// GUESS if it is a permalink by the format of the url
	int32_t p = ::isPermalink ( links  , // Links ptr
				 url    ,
				 *ct    , // CT_HTML default?
				 NULL   , // LinkInfo ptr
				 *isRSS );// isRSS?
	m_isPermalink      = p;
	m_isPermalink2     = p;
	m_isPermalinkValid = true;
	return &m_isPermalink2;
}

// guess based on the format of the url if this is a permalink
//@todo BR: FLAKY at best...
char *XmlDoc::getIsUrlPermalinkFormat ( ) {
	if ( m_isUrlPermalinkFormatValid ) return &m_isUrlPermalinkFormat;

	setStatus ( "getting is url permalink format" );

	Url *url      = getCurrentUrl();
	if ( ! url      ) return NULL;
	// just guess if we are rss here since we most likely do not have
	// access to the url's content...
	bool isRSS = false;
	const char *ext = url->getExtension();
	if ( ext && strcasecmp(ext,"rss") == 0 ) isRSS = true;
	// GUESS if it is a permalink by the format of the url
	int32_t p = ::isPermalink ( NULL    , // Links ptr
				 url     ,
				 CT_HTML ,
				 NULL    , // LinkInfo ptr
				 isRSS   );// we guess this...
	m_isUrlPermalinkFormat      = p;
	m_isUrlPermalinkFormatValid = true;
	return &m_isUrlPermalinkFormat;
}

char *XmlDoc::getIsRSS ( ) {
	if ( m_isRSSValid ) return &m_isRSS2;
	// the xml tells us for sure
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	m_isRSS      = xml->isRSSFeed();
	m_isRSS2     = (bool)m_isRSS;
	m_isRSSValid = true;
	return &m_isRSS2;
}

bool *XmlDoc::getIsSiteMap ( ) {
	if ( m_isSiteMapValid ) return &m_isSiteMap;
	uint8_t  *ct = getContentType();
	if ( ! ct    || ct    == (uint8_t  *)-1 ) return (bool *)ct;
	char *uf = m_firstUrl.getFilename();
	int32_t ulen = m_firstUrl.getFilenameLen();
	// sitemap.xml
	m_isSiteMap = false;
	// must be xml to be a sitemap
	if ( *ct == CT_XML &&
	     ulen == 11 &&
	     strncmp(uf,"sitemap.xml",11) == 0 )
		m_isSiteMap = true;
	m_isSiteMapValid = true;
	return &m_isSiteMap;
}

// . this function should really be called getTagTokens() because it mostly
//   works on HTML documents, not XML, and just sets an array of ptrs to
//   the tags in the document, including ptrs to the text in between
//   tags.
Xml *XmlDoc::getXml ( ) {

	// return it if it is set
	if ( m_xmlValid ) {
		return &m_xml;
	}

	// note it
	setStatus ( "parsing html");

	// get the filtered content
	char **u8 = getUtf8Content();
	if ( ! u8 || u8 == (char **)-1 ) return (Xml *)u8;
	int32_t u8len = size_utf8Content - 1;

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) return (Xml *)ct;

	int64_t start = logQueryTimingStart();

	// set it
	if ( !m_xml.set( *u8, u8len, m_version, *ct ) ) {
		// return NULL on error with g_errno set
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	m_xmlValid = true;
	return &m_xml;
}

static bool setLangVec ( Words *words ,
			 SafeBuf *langBuf ,
			 Sections *ss ) {

	const int64_t *wids       = words->getWordIds();
	const char * const *wptrs = words->getWords();
	int32_t nw                = words->getNumWords();

	// allocate
	if ( ! langBuf->reserve ( nw ) ) return false;

	uint8_t *langVector = (uint8_t *)langBuf->getBufStart();

	// now set the langid
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// default
		langVector[i] = langUnknown;
		// add the word
		if ( wids[i] == 0LL ) continue;
		// skip if number
		if ( is_digit(wptrs[i][0]) ) {
			langVector[i] = langTranslingual;
			continue;
		}
		// get the lang bits. does not include langTranslingual
		// or langUnknown
		int64_t bits = g_speller.getLangBits64 ( wids[i] );
		// skip if not unique
		char count = getNumBitsOn64 ( bits ) ;
		// if we only got one lang we could be, assume that
		if ( count == 1 ) {
			// get it. bit #0 is english, so add 1
			char langId = getBitPosLL((uint8_t *)&bits) + 1;
			//langVector[i] = g_wiktionary.getLangId(&wids[i]);
			langVector[i] = langId;
			continue;
		}
		// ambiguous? set it to unknown then
		if ( count >= 2 ) {
			langVector[i] = langUnknown;
			continue;
		}
		// try setting based on script. greek. russian. etc.
		// if the word was not in the wiktionary.
		// this will be langUnknown if not definitive.
		langVector[i] = getCharacterLanguage(wptrs[i]);
	}

	// . now go sentence by sentence
	// . get the 64 bit vector for each word in the sentence
	// . then intersect them all
	// . if the result is a unique langid, assign that langid to
	//   all words in the sentence

	// get first sentence in doc
	Section *si = NULL;
	if ( ss ) si = ss->m_firstSent;
	// scan the sentence sections and or in the bits we should
	for ( ; si ; si = si->m_nextSent ) {
		// reset vec
		int64_t bits = LANG_BIT_MASK;
		// get lang 64 bit vec for each wid in sentence
		for ( int32_t j = si->m_senta ; j < si->m_sentb ; j++ ) {
			// skip if not alnum word
			if ( ! wids[j] ) continue;
			// skip if starts with digit
			if ( is_digit(wptrs[j][0]) ) continue;
			// get 64 bit lang vec. does not include
			// langUnknown or langTransligual bits
			bits &= g_speller.getLangBits64 ( wids[j] );
		}
		// bail if none
		if ( ! bits ) continue;
		// skip if more than one language in intersection
		if ( getNumBitsOn64(bits) != 1 ) continue;
		// get it. bit #0 is english, so add 1
		char langId = getBitPosLL((uint8_t *)&bits) + 1;
		// ok, must be this language i guess
		for ( int32_t j = si->m_senta ; j < si->m_sentb ; j++ ) {
			// skip if not alnum word
			if ( ! wids[j] ) continue;
			// skip if starts with digit
			if ( is_digit(wptrs[j][0]) ) continue;
			// set it
			langVector[j] = langId;
		}
	}

	// try the same thing but do not use sentences. use windows of
	// 5 words. this will pick up pages that have an english menu
	// where each menu item is an individual sentence and only
	// one word.
	// http://www.topicexchange.com/
	int64_t window[5];
	int32_t wpos[5];
	memset ( window , 0 , 8*5 );
	int32_t wp = 0;
	int32_t total = 0;
	// now set the langid
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// must be alnum
		if ( ! wids[i] ) continue;
		// skip if starts with digit
		if ( is_digit(wptrs[i][0]) ) continue;
		// skip if lang already set to a language
		//if ( langVector[i] != langUnknown &&
		//     langVector[i] != langTranslingual )
		//	continue;
		// get last 5
		window[wp] = g_speller.getLangBits64 ( wids[i] );
		// skip if not in dictionary!
		if ( window[wp] == 0 ) continue;
		// otherwise, store it
		wpos  [wp] = i;
		if ( ++wp >= 5 ) wp = 0;
		// need at least 3 samples
		if ( ++total <= 2 ) continue;
		// intersect them all together
		int64_t bits = LANG_BIT_MASK;
		for ( int32_t j = 0 ; j < 5 ; j++ ) {
			// skip if uninitialized, like if we have 3
			// or only 4 samples
			if ( ! window[j] ) continue;
			// otherwise, toss it in the intersection
			bits &= window[j];
		}
		// skip if intersection empty
		if ( ! bits ) continue;
		// skip if more than one language in intersection
		if ( getNumBitsOn64(bits) != 1 ) continue;
		// get it. bit #0 is english, so add 1
		char langId = getBitPosLL((uint8_t *)&bits) + 1;
		// set all in window to this language
		for ( int32_t j = 0 ; j < 5 ; j++ ) {
			// skip if unitialized
			if ( ! window[j] ) continue;
			// otherwise, set it
			langVector[wpos[j]] = langId;
		}
	}


	return true;
}

// 1-1 with the words!
uint8_t *XmlDoc::getLangVector ( ) {

	if ( m_langVectorValid ) {
		// can't return NULL, that means error!
		uint8_t *v = (uint8_t *)m_langVec.getBufStart();
		if ( ! v ) return (uint8_t *)0x01;
		return v;
	}

	// words
	Words *words = getWords();
	if ( ! words || words == (Words *)-1 ) return (uint8_t *)words;

	// get the sections
	Sections *ss = getSections();
	if ( ! ss || ss==(void *)-1) return (uint8_t *)ss;


	if ( ! setLangVec ( words , &m_langVec , ss ) )
		return NULL;

	m_langVectorValid = true;
	// can't return NULL, that means error!
	uint8_t *v = (uint8_t *)m_langVec.getBufStart();
	if ( ! v ) return (uint8_t *)0x01;
	return v;
}

// returns -1 and sets g_errno on error
uint8_t *XmlDoc::getLangId ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	if ( m_langIdValid ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );
		return &m_langId;
	}
	setStatus ( "getting lang id");

	// get the stuff we need
	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1 ) return (uint8_t *)ip;

	// . if we got no ip, we can't get the page...
	// . also getLinks() will call getSiteNumInlinks() which will
	//   call getSiteLinkInfo() and will core if ip is 0 or -1
	if ( *ip == 0 || *ip == -1 ) {
		m_langId = langUnknown;
		m_langIdValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, IP unknown" );
		return &m_langId;
	}

	Words    *words    = getWords   ();
	if ( ! words || words == (Words *)-1 ) {
		return (uint8_t *)words;
	}

	Sections *sections = getSections();
	// did it block?
	if ( sections==(Sections *)-1) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, invalid section" );
		return(uint8_t *)sections;
	}

	// well, it still calls Dates::parseDates which can return g_errno
	// set to EBUFOVERFLOW...
	if ( ! sections && g_errno != EBUFOVERFLOW ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, invalid section" );
		return NULL;
	}

	// if sectinos is still NULL - try lang id without sections then,
	// reset g_errno
	g_errno = 0;

	uint8_t *lv = getLangVector();
	if ( ! lv || lv == (void *)-1 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, invalid lang vector" );
		return (uint8_t *)lv;
	}

	setStatus ( "getting lang id");

	// compute langid from vector
	m_langId = computeLangId ( sections , words, (char *)lv );
	if ( m_langId != langUnknown ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning langid=%s from langVector", getLanguageAbbr(m_langId) );
		m_langIdValid = true;
		return &m_langId;
	}

	// . try the meta description i guess
	// . 99% of the time we don't need this because the above code
	//   captures the language
	int32_t mdlen;
	char *md = getMetaDescription( &mdlen );
	Words mdw;
	mdw.set ( md , mdlen , true );

	SafeBuf langBuf;
	setLangVec ( &mdw,&langBuf,NULL);
	char *tmpLangVec = langBuf.getBufStart();
	m_langId = computeLangId ( NULL , &mdw , tmpLangVec );
	if ( m_langId != langUnknown ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning langid=%s from metaDescription", getLanguageAbbr(m_langId) );
		m_langIdValid = true;
		return &m_langId;
	}

	// try meta keywords
	md = getMetaKeywords( &mdlen );
	mdw.set ( md , mdlen , true );

	langBuf.purge();
	setLangVec ( &mdw,&langBuf,NULL);
	tmpLangVec = langBuf.getBufStart();
	m_langId = computeLangId ( NULL , &mdw , tmpLangVec );
	logTrace( g_conf.m_logTraceXmlDoc, "END, returning langid=%s from metaKeywords", getLanguageAbbr(m_langId) );
	m_langIdValid = true;
	return &m_langId;
}


// lv = langVec
char XmlDoc::computeLangId ( Sections *sections , Words *words, char *lv ) {

	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;
	// this means null too
	if ( sections && sections->m_numSections == 0 ) sp = NULL;
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE;//|SEC_SELECT;

	int32_t counts [ MAX_LANGUAGES ];
	memset ( counts , 0 , MAX_LANGUAGES * 4);



	int32_t             nw    = words->getNumWords();
	const char * const *wptrs = words->getWords();


	// now set the langid
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if in script or style section
		if ( sp && (sp[i]->m_flags & badFlags) ) continue;
		//
		// skip if in a url
		//
		// blah/
		int32_t wlen = words->getWordLen(i);
		if ( wptrs[i][wlen] == '/' ) continue;
		// blah.blah or blah?blah
		if ( (wptrs[i][wlen] == '.' ||
		      wptrs[i][wlen] == '?' ) &&
		     is_alnum_a(wptrs[i][wlen+1]) )
			continue;
		// /blah or ?blah
		if ( (i>0 && wptrs[i][-1] == '/') ||
		     (i>0 && wptrs[i][-1] == '?')    )
			continue;
		// add it up
		counts[(unsigned char)lv[i]]++;
	}

	// get the majority count
	int32_t max = 0;
	int32_t maxi = 0;
	// skip langUnknown by starting at 1, langEnglish
	for ( int32_t i = 1 ; i < MAX_LANGUAGES ; i++ ) {
		// skip translingual
		if ( i == langTranslingual ) {
			continue;
		}
		if ( counts[i] <= max ) {
			continue;
		}
		max = counts[i];
		maxi = i;
	}

	return maxi;
}



Words *XmlDoc::getWords ( ) {
	// return it if it is set
	if ( m_wordsValid ) {
		return &m_words;
	}

	// this will set it if necessary
	Xml *xml = getXml();
	// returns NULL on error, -1 if blocked
	if ( ! xml || xml == (Xml *)-1 ) return (Words *)xml;

	// note it
	setStatus ( "getting words");

	int64_t start = logQueryTimingStart();

	// now set what we need
	if ( !m_words.set( xml, true ) ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	m_wordsValid = true;
	return &m_words;
}

Bits *XmlDoc::getBits ( ) {
	// return it if it is set
	if ( m_bitsValid ) return &m_bits;

	// this will set it if necessary
	Words *words = getWords();
	// returns NULL on error, -1 if blocked
	if ( ! words || words == (Words *)-1 ) return (Bits *)words;

	int64_t start = logQueryTimingStart();

	// now set what we need
	if ( !m_bits.set(words))
		return NULL;

	logQueryTimingEnd( __func__, start );

	// we got it
	m_bitsValid = true;
	return &m_bits;
}

Bits *XmlDoc::getBitsForSummary ( ) {
	// return it if it is set
	if ( m_bits2Valid ) return &m_bits2;

	// this will set it if necessary
	Words *words = getWords();
	// returns NULL on error, -1 if blocked
	if ( ! words || words == (Words *)-1 ) return (Bits *)words;

	int64_t start = logQueryTimingStart();

	// now set what we need
	if ( ! m_bits2.setForSummary ( words ) ) return NULL;

	logQueryTimingEnd( __func__, start );

	// we got it
	m_bits2Valid = true;
	return &m_bits2;
}

Pos *XmlDoc::getPos ( ) {
	// return it if it is set
	if ( m_posValid ) return &m_pos;

	// this will set it if necessary
	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (Pos *)ww;

	int64_t start = logQueryTimingStart();

	if ( ! m_pos.set ( ww ) ) return NULL;

	logQueryTimingEnd( __func__, start );

	// we got it
	m_posValid = true;
	return &m_pos;
}

Phrases *XmlDoc::getPhrases ( ) {
	// return it if it is set
	if ( m_phrasesValid ) {
		return &m_phrases;
	}

	// this will set it if necessary
	Words *words = getWords();
	// returns NULL on error, -1 if blocked
	if ( ! words || words == (Words *)-1 ) return (Phrases *)words;

	// get this
	Bits *bits = getBits();
	// bail on error
	if ( ! bits ) return NULL;

	int64_t start = logQueryTimingStart();

	// now set what we need
	if ( !m_phrases.set( words, bits ) ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	// we got it
	m_phrasesValid = true;
	return &m_phrases;
}


Sections *XmlDoc::getSections ( ) {
	// return it if it is set
	if ( m_sectionsValid ) return &m_sections;

	setStatus ( "getting sections" );

	// use the old title rec to make sure we parse consistently!
	XmlDoc **pod = getOldXmlDoc ( );
	if ( ! pod || pod == (XmlDoc **)-1 ) return (Sections *)pod;

	Words *words = getWords();
	// returns NULL on error, -1 if blocked
	if ( ! words || words == (Words *)-1 ) return (Sections *)words;

	// get this
	Bits *bits = getBits();
	// bail on error
	if ( ! bits ) return NULL;

	// the docid
	int64_t *d = getDocId();
	if ( ! d || d == (int64_t *)-1 ) return (Sections *)d;

	// get the content type
	uint8_t *ct = getContentType();
	if ( ! ct ) return NULL;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	setStatus ( "getting sections");

	int64_t start = logQueryTimingStart();

	// this uses the sectionsReply to see which sections are "text", etc.
	// rather than compute it expensively
	if ( !m_calledSections &&
		 !m_sections.set( &m_words, bits, getFirstUrl(), cr->m_coll, *ct ) ) {
		m_calledSections = true;
		// it blocked, return -1
		return (Sections *) -1;
	}

	// error? maybe ENOMEM
	if ( g_errno ) return NULL;

	// set inlink bits
	m_bits.setInLinkBits ( &m_sections );

	logQueryTimingEnd( __func__, start );

	// we got it
	m_sectionsValid = true;
	return &m_sections;
}

int32_t *XmlDoc::getLinkSiteHashes ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	if ( m_linkSiteHashesValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );
		return (int32_t *)m_linkSiteHashBuf.getBufStart();
	}

	// get the outlinks
	Links *links = getLinks();
	if ( ! links || links == (Links *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinks returned -1" );
		return (int32_t *)links;
	}

	// . get the outlink tag rec vector
	// . each link's tagrec may have a "site" tag that is basically
	//   the cached SiteGetter::getSite() computation
	TagRec ***grv = NULL;
	if ( ! m_setFromTitleRec ) {
		logTrace( g_conf.m_logTraceXmlDoc, "!m_setFromTitleRec, calling getOutlinkTagRecVector" );
		grv = getOutlinkTagRecVector();
		if ( ! grv || grv == (void *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, getOutlinkTagRecVector returned -1" );
			return (int32_t *)grv;
		}
	}

	// how many outlinks do we have on this page?
	int32_t n = links->getNumLinks();
	logTrace( g_conf.m_logTraceXmlDoc, "%" PRId32" outlinks found on page", n);

	// reserve space
	m_linkSiteHashBuf.purge();
	if ( ! m_linkSiteHashBuf.reserve ( n * 4 ) )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, m_linkSiteHashBuf.reserve failed" );
		return NULL;
	}

	if ( n == 0 ) {
		ptr_linkdbData = NULL;
		size_linkdbData = 0;
		logTrace( g_conf.m_logTraceXmlDoc, "END, no outlinks" );
		return (int32_t *)0x1234;
	}

	// if set from titlerec then assume each site is the full hostname
	// of the link, unless its specified explicitly in the hashtablex
	// serialized in ptr_linkdbData
	if ( m_setFromTitleRec ) {
		// this holds the sites that are not just the hostname
		int32_t *p = (int32_t *)ptr_linkdbData;
		int32_t *pend = (int32_t *)(ptr_linkdbData + size_linkdbData);
		// loop over links
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// get the link
			char *u = links->getLinkPtr(i);
			// assume site is just the host
			int32_t hostLen = 0;
			const char *host = ::getHost ( u , &hostLen );
			int32_t siteHash32 = hash32 ( host , hostLen , 0 );
			// unless give as otherwise
			if ( p < pend && *p == i ) {
				p++;
				siteHash32 = *p;
				p++;
			}
			// store that then. should not fail since we allocated
			// right above
			if ( ! m_linkSiteHashBuf.pushLong(siteHash32) ) {
				g_process.shutdownAbort(true); }
		}
		// return ptr of array, which is a safebuf
		logTrace( g_conf.m_logTraceXmlDoc, "END, m_setFromTitleRec. Returning link list" );
		return (int32_t *)m_linkSiteHashBuf.getBufStart();
	}

	// ptr_linkdbData will point into this buf
	m_linkdbDataBuf.purge();

	// loop through them
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the link
		char *u = links->getLinkPtr(i);
		// get full host from link
		int32_t hostLen = 0;
		const char *host = ::getHost ( u , &hostLen );
		int32_t hostHash32 = hash32 ( host , hostLen , 0 );
		// get the site
		TagRec *gr = (*grv)[i];
		const char *site = NULL;
		int32_t  siteLen = 0;
		if ( gr ) {
			int32_t dataSize = 0;
			site = gr->getString("site",NULL,&dataSize);
			if ( dataSize ) siteLen = dataSize - 1;
		}
		// otherwise, make it the host or make it cut off at
		// a "/user/" or "/~xxxx" or whatever path component
		if ( ! site ) {
			// GUESS link site... like /~xxx
			site    = host;
			siteLen = hostLen;
		}
		int32_t linkeeSiteHash32 = hash32 ( site , siteLen , 0 );
		// only store if different form host itself
		if ( linkeeSiteHash32 != hostHash32 ) {
			if ( ! m_linkdbDataBuf.pushLong(i) )
			{
				logTrace( g_conf.m_logTraceXmlDoc, "END, could not store in buffer (1)" );
				return NULL;
			}
			if ( ! m_linkdbDataBuf.pushLong(linkeeSiteHash32) )
			{
				logTrace( g_conf.m_logTraceXmlDoc, "END, could not store in buffer (2)" );
				return NULL;
			}
		}
		// store it always in this buf
		if ( ! m_linkSiteHashBuf.pushLong(linkeeSiteHash32) ) {
			// space should have been reserved above!
			g_process.shutdownAbort(true); }
	}
	// set ptr_linkdbData
	ptr_linkdbData  = m_linkdbDataBuf.getBufStart();
	size_linkdbData = m_linkdbDataBuf.length();
	m_linkSiteHashesValid = true;

	logTrace( g_conf.m_logTraceXmlDoc, "END, returning list" );
	return (int32_t *)m_linkSiteHashBuf.getBufStart();
}



Links *XmlDoc::getLinks ( bool doQuickSet ) {
	if ( m_linksValid ) return &m_links;
	// set status
	setStatus ( "getting outlinks");

	// this will set it if necessary
	Xml *xml = getXml();

	// bail on error
	if ( ! xml || xml == (Xml *)-1 ) return (Links *)xml;

	// can't call getIsPermalink() here without entering a dependency loop
	char *pp = getIsUrlPermalinkFormat();
	if ( !pp || pp == (char *)-1 ) return (Links *)pp;

	// use the old xml doc
	XmlDoc **od = getOldXmlDoc ( );
	if ( ! od || od == (XmlDoc **)-1 ) return (Links *)od;

	// get Links class of the old title rec
	Links *oldLinks = NULL;

	// if we were set from a title rec, do not do this
	if ( *od ) {
		oldLinks = (*od)->getLinks();
		if (!oldLinks||oldLinks==(Links *)-1) return (Links *)oldLinks;
	}

	Url *baseUrl = getBaseUrl();
	if ( ! baseUrl || baseUrl==(Url *)-1) return (Links *)baseUrl;

	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1 ) return (Links *)ip;

	// this ensures m_contentLen is set
	//char **content = getContent();
	//if ( ! content || content == (char **)-1 ) return (Links *)content;

	char *ict = getIsContentTruncated();
	if ( ! ict || ict == (char *)-1 ) return (Links *)ict;

	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 ) return (Links *)sni;

	// get the latest url we are on
	Url *u = getCurrentUrl();

	//
	// if we had a EDOCSIMPLIFIEDREDIR error, pretend it is a link
	// so addOutlinkSpiderRecsToMetaList() will add it to spiderdb
	//
	if ( m_indexCodeValid && m_indexCode == EDOCSIMPLIFIEDREDIR ) {
		m_links.set ( m_redirUrl.getUrl(), m_redirUrl.getUrlLen() );
		m_linksValid = true;
		return &m_links;
	}

	if ( m_indexCodeValid && m_indexCode == EDOCNONCANONICAL ) {
		m_links.set(m_canonicalRedirUrl.getUrl(), m_canonicalRedirUrl.getUrlLen());
		m_linksValid = true;
		return &m_links;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		return NULL;
	}

	bool useRelNoFollow = true;
	if ( ! cr->m_obeyRelNoFollowLinks ) {
		useRelNoFollow = false;
	}

	// . set it
	// . if parent is a permalink we can avoid its suburl outlinks
	//   containing "comment" from being classified as permalinks
	if ( ! m_links.set ( useRelNoFollow ,
			     xml         ,
			     u           ,
			     true        , // setLinkHashes?
			     baseUrl     ,
			     m_version   ,
			     m_niceness  ,
			     *pp         , // parent url in permalink format?
			     oldLinks    ,// oldLinks, might be NULL!
			     doQuickSet ) )
		return NULL;

	m_linksValid = true;

	// do not bother setting that bit if we are being called for link
	// text because that bit was already in the linkdb key, and it
	// was set to zero! so if getting msg20 reply.... bail now
	if ( m_req ) return &m_links;

	// . apply link spam settings
	// . set the "spam bits" in the Links class
	setLinkSpam ( *ip                ,
		      u                  , // linker url
		      *sni               ,
		      xml                ,
		      &m_links           ,
		      *ict               );
	// we got it
	return &m_links;
}


HashTableX *XmlDoc::getCountTable ( ) {
	// return it if we got it
	if ( m_countTableValid ) return &m_countTable;

	setStatus ("getting count table");

	// get the stuff we need
	Xml      *xml      = getXml     ();
	if ( ! xml || xml == (Xml *)-1 ) return (HashTableX *)xml;
	Words    *words    = getWords   ();
	if ( ! words || words == (Words *)-1 ) return (HashTableX *)words;
	Phrases  *phrases  = getPhrases ();
	if ( ! phrases || phrases==(Phrases *)-1) return (HashTableX *)phrases;
	Bits     *bits     = getBits    ();
	if ( ! bits || bits == (Bits *)-1 ) return (HashTableX *)bits;
	Sections *sections = getSections();
	if ( !sections||sections==(Sections *)-1) return(HashTableX *)sections;
	LinkInfo *info1    = getLinkInfo1();
	if ( ! info1 || info1 == (LinkInfo *)-1 ) return (HashTableX *)info1;

	// . reduce score of words in badly repeated fragments to 0 so we do
	//   not count them here!
	// . ff[i] will have score of 0 if in repeated frag
	// . make sure this is stored for whole doc... since we only use it
	//   for the body
	char *fv = getFragVec();
	if ( ! fv || fv == (void *)-1 ) return (HashTableX *)fv;

	//
	// this was in Weights.cpp, but now it is here...
	//

	// shortcut
	HashTableX *ct = &m_countTable;

	// ez var
	const nodeid_t *tids  = words->getTagIds();
	int32_t   nw    = words->getNumWords   ();
	const int64_t  *pids  = phrases->getPhraseIds2();

	// add 5000 slots for inlink text in hashString_ct() calls below
	int32_t numSlots = nw * 3 + 5000;
	// only alloc for this one if not provided
	if (!ct->set(8,4,numSlots,NULL,0,false,"xmlct"))
	  return (HashTableX *)NULL;

	// . now hash all the phrase ids we have in order to see if the phrase
	//   is unique or not. if phrase is repeated a lot we punish the scores
	//   of the individual words in the phrase and boost the score of the
	//   phrase itself. We check for uniqueness down below.
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// add the word
		int64_t wid = words->getWordId(i);
		if ( wid == 0LL )
			continue;

		// . skip if in repeated fragment
		// . unfortunately we truncate the frag vec to like
		//   the first 80,000 words for performance reasons
		if ( i < MAXFRAGWORDS && fv[i] == 0 ) continue;
		// accumulate the wid with a score of 1 each time it occurs
		if ( ! ct->addTerm ( &wid ) ) return (HashTableX *)NULL;
		// skip if word #i does not start a phrase
		if ( ! pids [i] ) continue;
		// if phrase score is less than 100% do not consider as a
		// phrase so that we do not phrase "albuquerque, NM" and stuff
		// like that... in fact, we can only have a space here...
		const char *wptr = words->getWord(i+1);
		if ( wptr[0] == ',' ) continue;
		if ( wptr[1] == ',' ) continue;
		if ( wptr[2] == ',' ) continue;
		// put it in, accumulate, max score is 0x7fffffff
		if ( ! ct->addTerm ( &pids[i] ) ) return (HashTableX *)NULL;
	}

	// now add each meta tag to the pot
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not a meta tag
		if ( tids[i] != TAG_META ) continue;
		// find the "content=" word
		const char *w    = words->getWord(i);
		int32_t  wlen = words->getWordLen(i);
		const char *wend = w + wlen;
		const char *p = strncasestr  (w,wlen,"content=");
		// skip if we did not have any content in this meta tag
		if ( ! p ) continue;
		// skip the "content="
		p += 8;
		// skip if empty meta content
		if ( wend - p <= 0 ) continue;

		// our ouw hash
		//const_cast because hashString_ct calls Words::set and that is still not const-sane
		if ( ! hashString_ct ( ct , const_cast<char*>(p) , wend - p ) )
			return (HashTableX *)NULL;
	}
	// add each incoming link text
	for ( Inlink *k=NULL ; info1 && (k=info1->getNextInlink(k)) ; ) {
		// shortcuts
		char *p;
		int32_t  plen;
		// hash link text (was hashPwids())
		p    = k-> getLinkText();
		plen = k->size_linkText - 1;
		if ( ! verifyUtf8 ( p , plen ) ) {
			log("xmldoc: bad link text 3 from url=%s for %s",
			    k->getUrl(),m_firstUrl.getUrl());
			continue;
		}
		if ( ! hashString_ct ( ct , p , plen ) )
		  return (HashTableX *)NULL;
		// hash this stuff (was hashPwids())
		p    = k->getSurroundingText();
		plen = k->size_surroundingText - 1;
		if ( ! hashString_ct ( ct , p , plen ) )
		  return (HashTableX *)NULL;
	}

	// we got it
	m_countTableValid = true;
	return &m_countTable;
}

// . a special function used by XmlDoc::getCountTable() above
// . kinda similar to XmlDoc::hashString()
bool XmlDoc::hashString_ct ( HashTableX *ct , char *s , int32_t slen ) {

	Words   words;
	Bits    bits;
	Phrases phrases;
	if ( ! words.set   ( s , slen , true ) )
		return false;
	if ( !bits.set(&words))
		return false;
	if ( !phrases.set( &words, &bits ) )
		return false;
	int32_t nw = words.getNumWords();
	const int64_t  *pids  = phrases.getPhraseIds2();

	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// add the word
		int64_t wid = words.getWordId(i);
		if ( wid == 0LL ) continue;
		// skip if in repeated fragment
		// . NO, we do not use this for these short strings
		//if ( ww[i] == 0 ) continue;
		// accumulate the wid with a score of 1 each time it occurs
		if ( ! ct->addTerm ( &wid ) ) return false;
		// skip if word #i does not start a phrase
		if ( ! pids [i] ) continue;
		// if phrase score is less than 100% do not consider as a
		// phrase so that we do not phrase "albuquerque, NM" and stuff
		// like that... in fact, we can only have a space here...
		if ( i+1<nw ) {
			const char *wptr = words.getWord(i+1);
			if ( wptr[0] == ',' ) continue;
			int32_t wlen = words.getWordLen(i);
			if ( wlen>=2 && wptr[1] == ',' ) continue;
			if ( wlen>=3 && wptr[2] == ',' ) continue;
		}
		// put it in, accumulate, max score is 0x7fffffff
		if ( ! ct->addTerm ( &pids[i] ) ) return false;
	}
	return true;
}

int cmp ( const void *h1 , const void *h2 ) ;

// vector components are 32-bit hashes
int32_t *XmlDoc::getTagPairHashVector ( ) {

	if ( m_tagPairHashVecValid ) return m_tagPairHashVec;

	Xml      *xml      = getXml     ();
	if ( ! xml || xml == (Xml *)-1 ) return (int32_t *)xml;

	// store the hashes here
	uint32_t hashes [ 2000 ];
	int32_t          nh = 0;
	// go through each node
	XmlNode *nodes = xml->getNodes    ();
	int32_t   n       = xml->getNumNodes ();

	// start with the ith node
	int32_t i = 0;

	uint32_t saved = 0;
	uint32_t lastHash = 0;
	// loop over the nodes
	for ( ; i < n ; i++ ) {
		// skip NON tags
		if ( ! nodes[i].isTag() ) continue;
		// use the tag id as the hash, its unique
		uint32_t h = hash32h ( nodes[i].getNodeId() , 0 );
		// ensure hash is not 0, that has special meaning
		if ( h == 0 ) h = 1;
		// store in case we have only one hash
		saved = h;

		// if we are the first, set this
		if ( ! lastHash ) {
			lastHash = h;
			continue;
		}

		// if they were the same do not xor, they will zero out
		if ( h == lastHash ) hashes[nh++] = h;
		// incorporate it into the last hash
		else                 hashes[nh++] = h ^ lastHash;

		// we are the new last hash
		lastHash = h;
		// bust out if no room
		if ( nh >= 2000 ) break;
	}
	// if only had one tag after, use that
	if ( nh == 0 && saved ) hashes[nh++] = saved;

	// . TODO: remove the link text hashes here?
	// . because will probably be identical..
	// . now sort hashes to get the top MAX_PAIR_HASHES
	gbsort ( hashes , nh , 4 , cmp );

	// uniquify them
	int32_t d = 0;
	for ( int32_t j = 1 ; j < nh ; j++ ) {
		if ( hashes[j] == hashes[d] ) continue;
		hashes[++d] = hashes[j];
	}

	// how many do we got?
	nh = d;
	// truncate to MAX_PAIR_HASHES MINUS 1 so we can put a 0 at the end
	if ( nh > MAX_TAG_PAIR_HASHES-1 ) nh = MAX_TAG_PAIR_HASHES-1;
	// store the top MAX_PAIR_HASHES
	gbmemcpy ( m_tagPairHashVec , hashes , nh * 4 );
	// null term it. all vectors need this so computeSimilarity() works
	m_tagPairHashVec [ nh++ ] = 0;
	m_tagPairHashVecValid = true;
	m_tagPairHashVecSize = nh * 4;
	return m_tagPairHashVec;
}

// sort in descending order
int cmp ( const void *h1 , const void *h2 ) {
	return *(uint32_t *)h2 - *(uint32_t *)h1;
}

// . m_tagVector.setTagPairHashes(&m_xml, niceness);
// . Sections.cpp and getIsDup() both use this hash
// . returns NULL and sets g_errno on error
// . xors all the unique adjacent tag hashes together
// . kind of represents the template the web pages uses
// . we add this to sectiondb as a vote in Sections::addVotes()
uint32_t *XmlDoc::getTagPairHash32 ( ) {

	// only compute once
	if ( m_tagPairHash32Valid ) return &m_tagPairHash32;

	Words *words = getWords();
	if ( ! words || words == (Words *)-1 ) return (uint32_t *)words;

        // shortcuts
	//int64_t *wids  = words->getWordIds  ();
	const nodeid_t *tids  = words->getTagIds();
	int32_t           nw  = words->getNumWords();
	int32_t           nt  = words->getNumTags();

	// . get the hash of all the tag pair hashes!
	// . we then combine that with our site hash to get our site specific
	//   html template termid
	// . put all tag pairs into a hash table
	// . similar to Vector::setTagPairHashes() but we do not compute a
	//   vector, just a single scalar/hash of 32 bits, m_termId
	HashTableX tp; // T<int64_t,char> tp;
	if ( ! tp.set ( 4 , 1 , nt * 4  , NULL , 0 , true,"xmltp"))
		return 0LL;
	uint32_t lastTid = 0;
	char val = 1;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not tag
		if ( tids[i] == 0LL ) continue;
		// skip if back tag
		if ( tids[i] & BACKBIT ) continue;
		// get last tid
		uint32_t h = hash32h ( tids[i] , lastTid );
		//logf(LOG_DEBUG,"build: tph %" PRId32" h=%" PRIu64,i,(int64_t)h);
		// . add to table (skip if 0, means empty bucket)
		// . return NULL and set g_errno on error
		if ( h && ! tp.addKey ( &h , &val ) ) return NULL;
		// update this
		lastTid = h;
	}
	// linear scan on hash table to get all the hash, XOR together
	uint32_t hx = 0;
	int32_t nb = tp.getNumSlots();
	char *flags = tp.m_flags;
	// get keys
	uint32_t *keys = (uint32_t *)tp.m_keys;
	for ( int32_t i = 0 ; i < nb ; i++ ) {
		// skip if empty
		if ( flags[i] == 0 ) continue;
		// skip if empty
		//if ( keys[i] == 0LL ) continue;
		// incorporate
		hx ^= keys[i];
	}
	// never return 0, make it 1. 0 means an error
	if ( hx == 0 ) hx = 1;
	// set the hash
	m_tagPairHash32 = hx ;
	// it is now valid
	m_tagPairHash32Valid = true;
	return &m_tagPairHash32;
}

// . used for deduping search results
// . also uses the title
int32_t *XmlDoc::getSummaryVector ( ) {
	if ( m_summaryVecValid ) return (int32_t *)m_summaryVec;
	Summary *s = getSummary();
	if ( ! s || s == (Summary *)-1 ) return (int32_t *)s;
	Title *ti = getTitle();
	if ( ! ti || ti == (Title *)-1 ) return (int32_t *)ti;

	int64_t start = logQueryTimingStart();

	// store title and summary into "buf" so we can call words.set()
	SafeBuf sb;

	// put title into there
	int32_t tlen = ti->getTitleLen() - 1;
	if ( tlen < 0 ) tlen = 0;

	// put summary into there
	int32_t slen = s->getSummaryLen();

	// allocate space
	int32_t need = tlen + 1 + slen + 1;
	if ( ! sb.reserve ( need ) ) return NULL;

	sb.safeMemcpy ( ti->getTitle() , tlen );

	// space separting the title from summary
	if ( tlen > 0 ) sb.pushChar(' ');

	sb.safeMemcpy ( s->getSummary() , slen );

	// null terminate it
	sb.nullTerm();
	//workaround for truncation causing a multibyte utf8 character to be
	//split and then text parsing traversing past the defined bytes.
	sb.nullTerm();
	sb.nullTerm();
	sb.nullTerm();
	sb.nullTerm();

	// word-ify it
	Words words;
	if ( ! words.set ( sb.getBufStart() , true ) ) {
		return NULL;
	}

	// . now set the dedup vector from big summary and title
	// . store sample vector in here
	// . returns size in bytes including null terminating int32_t
	m_summaryVecSize = computeVector ( &words , (uint32_t *)m_summaryVec );

    logQueryTimingEnd(__func__, start);

	m_summaryVecValid = true;
	return m_summaryVec;
}


// used by getIsDup() and Dates.cpp for detecting dups and for
// seeing if the content changed respectively
int32_t *XmlDoc::getPageSampleVector ( ) {
	if ( m_pageSampleVecValid ) return m_pageSampleVec;
	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (int32_t *)ww;
	m_pageSampleVecSize = computeVector( ww, (uint32_t *)m_pageSampleVec );
	m_pageSampleVecValid = true;
	return m_pageSampleVec;
}

// . this is the vector of the words right after the hypertext for the link
//   we are voting on.
// . it is used to dedup voters in Msg25.cpp
int32_t *XmlDoc::getPostLinkTextVector ( int32_t linkNode ) {

	if ( m_postVecValid ) return m_postVec;
	// assume none
	m_postVecSize = 0;

	// set up
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (int32_t *)xml;
	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (int32_t *)ww;

	// sanity check
	if ( linkNode < 0 ) { g_process.shutdownAbort(true); }

	// linkNode starts pointing to a <a> tag so skip over that!
	linkNode++;
	// limit
	int32_t     nn    = xml->getNumNodes();
	XmlNode *nodes = xml->getNodes();
	// and advance i to the next anchor tag thereafter, we do not
	// want to include link text in this vector because it is usually
	// repeated and will skew our "similarities"
	for ( ; linkNode < nn ; linkNode++ ) {
		// stop if we hit </a> or <a>
		if ( (nodes[linkNode].m_nodeId & BACKBITCOMP) != TAG_A ) continue;
		// advance over the </a> or <a>
		linkNode++;
		// then stop, we will start gathering link text here
		break;
	}
	// if we hit end of the doc, we got not vector then
	if ( linkNode >= nn ) return m_postVec;

	// now convert the linkNode # to a word #, "start"
	int32_t          nw   = ww->getNumWords();
	const int64_t   *wids = ww->getWordIds();
	const nodeid_t  *tids = ww->getTagIds();
	const int32_t   *wn   = ww->getNodes();
	int32_t       i    = 0;
	for ( ; i < nw ; i++ ) {
		// stop when we got the first word in this node #
		if ( wn[i] == linkNode ) break;
	}
	// if none, bail now, size is 0
	if ( i >= nw ) return m_postVec;
	// save that
	int32_t start = i;

	// likewise, set the end of it
	int32_t end = nw;
	// count alnum words
	int32_t count = 0;
	// limit it
	for ( i = start ; i < nw && count < 35 ; i++ ) {
		// get tag id
		nodeid_t tid = tids[i] & BACKBITCOMP;
		// stop if certain ones
		if ( tid     == TAG_TABLE ) break;
		if ( tid     == TAG_UL    ) break;
		// <a>, </a> is ok
		if ( tids[i] == TAG_A     ) break;
		// only up to 35 words allowed in the hash
		if ( wids[i] ) count++;
	}
	// set the end of the words to hash
	end = i;

	// specify starting node # now
	m_postVecSize = computeVector( ww, (uint32_t *)m_postVec, start, end );

	// return what we got
	return m_postVec;
}

// . was kinda like "m_tagVector.setTagPairHashes(&m_xml, niceness);"
// . this is used by getIsDup() (below)
// . this is used by Dates.cpp to see how much a doc has changed
// . this is also now used for getting the title/summary vector for deduping
//   search results
// . if we couldn't extract a good pub date for the doc, and it has changed
//   since last spidered, use the bisection method to come up with our own
//   "last modified date" which we use as the pub date.
// . this replaces the clusterdb.getSimilarity() logic in Msg14.cpp used
//   to do the same thing. but we call Vector::setForDates() from
//   Dates.cpp. that way the logic is more contained in Dates!
// . doesn't Msg14 already do that?
// . yes, but it uses two TermTables and calls Clusterdb::getSimilarity()
// . returns false and sets g_errno on error
// . these words classes should have been set by a call to Words::set(Xml *...)
//   so that we have "tids1" and "tids2"

// . returns NULL and sets g_errno on error
// . TODO: if our title rec is non-empty consider getting it from that
// . we use this vector to compare two docs to see how similar they are
int32_t XmlDoc::computeVector( Words *words, uint32_t *vec, int32_t start, int32_t end ) {
	// assume empty vector
	vec[0] = 0;

	// shortcuts
	int32_t       nw     = words->getNumWords();
	const int64_t *wids  = words->getWordIds();

	// set the end to the real end if it was specified as less than zero
	if ( end < 0 ) end = nw;

	// # of alnum words, about... minus the tags, then the punct words
	// are half of what remains...
	int32_t count = words->getNumAlnumWords();

	// . Get sample vector from content section only.
	// . This helps remove duplicate menu/ad from vector

	// 4 bytes per hash, save the last one for a NULL terminator, 0 hash
	int32_t maxTerms = SAMPLE_VECTOR_SIZE / 4  - 1;
	// what portion of them do we want to mask out from the rest?
	int32_t ratio = count / maxTerms ;
	// a mask of 0 means to get them all
	unsigned char mask = 0x00;
	// if we got twice as many terms as we need, then set mask to 0x01
	// to filter out half of them! but actually, let's aim for twice
	// as many as we need to ensure we really get as many as we need.
	// so if we got 4 or more than we need then cut in half...
	while ( ratio >= 4 ) {
		// shift the mask down, ensure hi bit is set
		mask >>= 1;
		mask |= 0x80;
		ratio >>= 1; // /2
	}

	// store vector into "d" for now. will sort below
	uint32_t d [ 3000 ];

	// dedup our vector using this hashtable, "ht"
	char hbuf[3000*6*2];
	HashTableX ht;
	if ( ! ht.set(4,0,3000,hbuf,3000*6*2,false,"xmlvecdedup")){
		g_process.shutdownAbort(true);}

 again:
	// a buffer to hold the top termIds
	int32_t nd = 0;
	// count how many we mask out
	int32_t mo = 0;
	// . buffer should have at least "maxTerms" in it
	// . these should all be 12 byte keys
	for ( int32_t i = start ; i < end ; i++ ) {
		// skip if not alnum word
		if ( wids[i] == 0 ) continue;

		// skip if mask filters it
		if ( ((wids[i]>>(NUMTERMIDBITS-8)) & mask)!=0) {mo++;continue;}

		// make 32 bit
		uint32_t wid32 = (uint32_t)wids[i];

		// do not add if we already got it
		if ( ht.getSlot ( &wid32 ) >= 0 ) continue;

		// add to hash table. return NULL and set g_errno on error
		if ( ! ht.addKey (&wid32 )){g_process.shutdownAbort(true); }

		// add it to our vector
		d[nd] = (uint32_t)wids[i];

		// stop after 3000 for sure
		if ( ++nd < 3000 ) continue;

		// bitch and break out on error
		log(LOG_INFO,"build: Sample vector overflow. Slight performance hit.");
		break;
	}

	// . if nd was too small, don't use a mask to save time
	// . well just make the mask less restrictive
	if ( nd < maxTerms && mask && mo ) {
		// shift the mask UP, allow more termIds to pass through
		mask <<= 1;
		// reset hash table since we are starting over
		ht.clear();
		goto again;
	}

	// bubble sort them
	bool flag = true;
	while ( flag ) {
		flag = false;
		for ( int32_t i = 1 ; i < nd ; i++ ) {
			if ( d[i-1] <= d[i] ) continue;
			uint32_t tmp = d[i-1];
			d[i-1] = d[i];
			d[i]   = tmp;
			flag   = true;
		}
	}

	// truncate
	if ( nd > maxTerms ) nd = maxTerms;
	// null terminate
	d [ nd++ ] = 0;
	// store in our sample vector
	gbmemcpy ( vec , d , nd * 4 );
	// return size in bytes
	return nd * 4;
}

float *XmlDoc::getPageSimilarity ( XmlDoc *xd2 ) {
	int32_t *sv1 = getPageSampleVector();
	if ( ! sv1 || sv1 == (int32_t *)-1 ) return (float *)sv1;
	int32_t *sv2 = xd2->getPageSampleVector();
	if ( ! sv2 || sv2 == (int32_t *)-1 ) return (float *)sv2;
	m_pageSimilarity = computeSimilarity ( sv1, sv2, NULL, NULL, NULL);
	// this means error, g_errno should be set
	if ( m_pageSimilarity == -1.0 ) return NULL;
	return &m_pageSimilarity;
}

// . compare old page vector with new
// . returns ptr to a float from 0.0 to 100.0
float *XmlDoc::getPercentChanged ( ) {
	// if we got it
	if ( m_percentChangedValid ) return &m_percentChanged;
	// get the old doc
	XmlDoc **od = getOldXmlDoc ( );
	if ( ! od || od == (XmlDoc **)-1 ) return (float *)od;
	// if empty, assume 0% changed
	if ( ! *od ) {
		m_percentChanged      = 0;
		m_percentChangedValid = true;
		return &m_percentChanged;
	}
	// get its page c
	float *ps = getPageSimilarity    ( *od );
	if ( ! ps || ps == (float *)-1 ) return (float *)ps;
	// got it
	m_percentChanged      = *ps;
	m_percentChangedValid = true;
	// just return it
	return &m_percentChanged;
}

// . compare two vectors
// . components in vectors are int32_ts
// . last component is a zero, to mark EOV = end of vector
// . discount any termIds that are in the query vector, qvec, which may be NULL
// . returns -1 and sets g_errno on error
// . vector components are 32-bit hashes of the words (hash32())???
//   i would say they should be the lower 32 bits of the 64-bit hashes!
// . replaces:
//   m_tagVec->getLinkBrotherProbability()
//   g_clusterdb.getSampleSimilarity()
float computeSimilarity ( int32_t   *vec0 ,
			  int32_t   *vec1 ,
			  int32_t   *s0   , // corresponding scores vector
			  int32_t   *s1   , // corresponding scores vector
			  Query  *q    ,
			  bool    dedupVectors ) {
	static int32_t s_tmp = 0;
	if ( ! vec0 ) vec0 = &s_tmp;
	if ( ! vec1 ) vec1 = &s_tmp;
	// if both empty, assume not similar at all
	if ( *vec0 == 0 && *vec1 == 0 ) return 0;
	// if either is empty, return 0 to be on the safe side
	if ( *vec0 == 0 ) return 0;
	if ( *vec1 == 0 ) return 0;


	// flag if from query vector
	HashTableX qt;
	char qbuf[5000];
	if ( q ) {
		// init hash table
		if ( ! qt.set ( 4,0,512,qbuf,5000,false,"xmlqvtbl") )
			return -1;
		// . stock the query term hash table
		// . use the lower 32 bits of the termids to make compatible
		//   with the other vectors we use
		//int64_t *qtids = q->getTermIds ();
		int32_t       nt    = q->getNumTerms();
		for ( int32_t i = 0 ; i < nt ; i++ ) {
			// get query term
			QueryTerm *QT = &q->m_qterms[i];
			// get the termid
			int64_t termId = QT->m_termId;
			// get it
			uint32_t h = (uint32_t)(termId & 0xffffffff);
			// hash it
			if ( ! qt.addKey ( &h ) ) return -1;
		}
	}

	// if we ignore cardinality then it only matters if both vectors
	// have a particular value, and not how many times they each have it.
	// so we essentially dedup each vector if dedupVectors is true.
	// but we do total up the score and put it behind the one unique
	// occurence though. we do this only for
	// Sections::addDateBasedImpliedSections() right now
	bool allowDups = true;
	if ( dedupVectors ) allowDups = false;

	HashTableX ht;
	char  hbuf[10000];
	if ( ! ht.set ( 4,4,-1,hbuf,10000,allowDups,"xmlqvtbl2"))
		return -1;

	bool useScores  = (bool)s0;

	int32_t matches    = 0;
	int32_t total      = 0;

	int32_t matchScore = 0;
	int32_t totalScore = 0;

	// hash first vector. accumulating score total and total count
	for ( int32_t *p = vec0; *p ; p++ , s0++ ) {
		// skip if matches a query term
		if ( q && qt.getSlot ( p ) ) continue;
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s0;
		// total it up
		totalScore += score;
		// add it
		if ( dedupVectors ) {
			// accumulate all the scores into this one bucket
			// in the case of p being a dup
			if ( ! ht.addTerm32 ( p , score ) ) return -1;
		}
		else {
			// otherwise, add each into its own bucket since
			// ht.m_allowDups should be true
			if ( ! ht.addKey ( p , &score ) ) return -1;
		}
	}

	int32_t zero = 0;

	// see what components of this vector match
	for ( int32_t *p = vec1; *p ; p++ , s1++ ) {
		// skip if matches a query term
		if ( q && qt.getSlot ( p ) ) continue;
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s1;
		// and total scores
		totalScore += score;
		// is it in there?
		int32_t slot = ht.getSlot ( p );
		// skip if unmatched
		if ( slot < 0 ) continue;
		// otherwise, it is a match!
		matches++;
		// and scores
		matchScore += score;
		// and score of what we matched
		uint32_t *val = (uint32_t *)ht.getValueFromSlot ( slot );
		// he is hit too
		matchScore += *val;

		// remove it as we match it to deal with dups
		if ( allowDups ) {
			// once we match it once, do not match again, score was
			// already accumulated
			ht.setValue ( slot , &zero );
		}
		else {
			// otherwise, remove this dup and try to match any
			// remaining dups in the table
			ht.removeSlot ( slot );
		}
	}

	// if after subtracting query terms we got no hits, return 0.framesets?
	if ( useScores && totalScore == 0 ) return 0;
	if ( total                   == 0 ) return 0;
	// . what is the max possible score we coulda had?
	// . subtract the vector components that matched a query term
	float percent = 100 * (float)matchScore / (float)totalScore;
	//if ( useScores)percent = 100 * (float)matchScore / (float)totalScore;
	//else           percent = 100 * (float)matches    / (float)total;
	// sanity
	//if ( percent > 100 ) percent = 100;
	if ( percent > 100 ) { g_process.shutdownAbort(true); }

	return percent;
}

// this returns true if the two vecs are "percentSimilar" or more similar
bool isSimilar_sorted ( int32_t   *vec0 ,
			int32_t   *vec1 ,
			int32_t nv0 , // how many int32_ts in vec?
			int32_t nv1 , // how many int32_ts in vec?
			// they must be this similar or more to return true
			int32_t percentSimilar) {
	// if both empty, assume not similar at all
	if ( *vec0 == 0 && *vec1 == 0 ) return 0;
	// if either is empty, return 0 to be on the safe side
	if ( *vec0 == 0 ) return 0;
	if ( *vec1 == 0 ) return 0;

	// do not include last 0
	nv0--;
	nv1--;
	int32_t total = nv0 + nv1;

	// so if the "noMatched" count ever EXCEEDS (not equals) this
	// "brink" we can bail early because there's no chance of getting
	// the similarity "percentSimilar" provided. should save some time.
	int32_t brink = ((100-percentSimilar) * total) / 100;

	// scan each like doing a merge
	int32_t *p0 = vec0;
	int32_t *p1 = vec1;
	int32_t yesMatched = 0;
	int32_t noMatched  = 0;

 mergeLoop:

	// stop if both exhausted. we didn't bail on brink, so it's a match
	if ( *p0 == 0 && *p1 == 0 )
		return true;

	if ( *p0 < *p1 || *p1 == 0 ) {
		p0++;
		if ( ++noMatched > brink ) return false;
		goto mergeLoop;
	}

	if ( *p1 < *p0 || *p0 == 0 ) {
		p1++;
		if ( ++noMatched > brink ) return false;
		goto mergeLoop;
	}

	yesMatched += 2;
	p1++;
	p0++;
	goto mergeLoop;
}

int64_t *XmlDoc::getExactContentHash64 ( ) {

	if ( m_exactContentHash64Valid )
		return &m_exactContentHash64;

	char **u8 = getUtf8Content();
	if ( ! u8 || u8 == (char **)-1) return (int64_t *)u8;


	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;


	unsigned char *p = (unsigned char *)*u8;

	int32_t plen = size_utf8Content;
	if ( plen > 0 ) plen--;

	// sanity
	//if ( ! p ) return 0LL;
	//if ( p[plen] != '\0' ) { g_process.shutdownAbort(true); }

	unsigned char *pend = (unsigned char *)p + plen;
	uint64_t h64 = 0LL;
	unsigned char pos = 0;
	bool lastWasSpace = true;
	for ( ; p < pend ; p++ ) {
		// treat sequences of white space as a single ' ' (space)
		if ( is_wspace_a(*p) ) {
			if ( lastWasSpace ) continue;
			lastWasSpace = true;
			// treat all white space as a space
			h64 ^= g_hashtab[pos][(unsigned char)' '];
			pos++;
			continue;
		}
		lastWasSpace = false;
		// xor this in right
		h64 ^= g_hashtab[pos][p[0]];
		pos++;
	}

	m_exactContentHash64Valid = true;
	m_exactContentHash64 = h64;
	return &m_exactContentHash64;
}



RdbList *XmlDoc::getDupList ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	if ( m_dupListValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );;
		return &m_dupList;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, could not get collection" );;
		return NULL;
	}

	int64_t *ph64 = getExactContentHash64();
	if ( ! ph64 || ph64 == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getExactContentHash64 returned -1" );;
		return (RdbList *)ph64;
	}

	// must match term in XmlDoc::hashVectors()
	char qbuf[256];
	snprintf(qbuf, 256, "%" PRIu64,*ph64);
	int64_t pre     = hash64b ( "gbcontenthash" , 0LL );
	int64_t rawHash = hash64b ( qbuf , 0LL );
	int64_t termId  = hash64 ( rawHash , pre );
	// get the startkey, endkey for termlist
	key144_t sk ;
	key144_t ek ;
	Posdb::makeStartKey ( &sk,termId ,0);
	Posdb::makeEndKey   ( &ek,termId ,MAX_DOCID);
	// note it
	log(LOG_DEBUG,"build: check termid=%" PRIu64" for docid %" PRIu64
	    ,(uint64_t)(termId&TERMID_MASK)
	    ,m_docId);
	// assume valid now
	m_dupListValid = true;
	// this is a no-split lookup by default now
	if ( ! m_msg0.getList ( -1    , // hostId
				0     , // ip
				0     , // port
				0     , // maxCacheAge
				false , // add to cache?
				RDB_POSDB, // INDEXDB ,
				cr->m_collnum,
				&m_dupList  ,
				(char *)&sk          ,
				(char *)&ek          ,
				606006        , // minRecSizes in bytes
				m_masterState , // state
				m_masterLoop  ,
				m_niceness    ,
				true , // error correction?
				true , // include tree?
				true , // domerge?
				-1 , // firsthosti
				0 , // startfilenum
				-1, // # files
				// never timeout when spidering in case
				// a host is down.
				msg0_getlist_infinite_timeout , // timeout
				-1 , // syncpoint
				-1 , // preferlocal reads
				NULL, // msg5
				false , // isRealMerge
				true , // allow page cache
				false , // forcelocalindexdb
				true ) ) // shardByTermId? THIS IS DIFFERENT!!!
	{
		// return -1 if this blocks
		logTrace( g_conf.m_logTraceXmlDoc, "END, return -1. msg0.getList blocked." );;
		return (RdbList *)-1;
	}

	// assume valid!
	m_dupListValid = true;
	logTrace( g_conf.m_logTraceXmlDoc, "END, done." );;
	return &m_dupList;
}


// moved DupDetector.cpp into here...
char *XmlDoc::getIsDup ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	if ( m_isDupValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );;
		return &m_isDup;
	}

	// assume we are not a dup
	m_isDup = false;
	// get it
	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, could not get collection" );;
		return NULL;
	}

	// skip if we should
	if ( ! cr->m_dedupingEnabled ) {
		m_isDupValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, deduping not enabled" );;
		return &m_isDup;
	}

	setStatus ( "checking for dups" );

	// get our docid
	int64_t *mydocid = getDocId();
	if ( ! mydocid || mydocid == (int64_t *)-1)
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getDocId returned -1" );;
		return (char *)mydocid;
	}

	// get the duplist!
	RdbList *list = getDupList();
	if ( ! list || list == (RdbList *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getDupList returned -1" );;
		return (char *)list;
	}

	// sanity. must be posdb list.
	if ( ! list->isEmpty() && list->getKeySize() != 18 ) { g_process.shutdownAbort(true);}

	// so getSiteRank() does not core
	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getSiteNumInlinks returned -1" );;
		return (char *)sni;
	}

	int32_t myRank = getSiteRank ( );

	// assume not a dup
	m_isDup = false;
	// get the docid that we are a dup of
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		char *rec = list->getCurrentRec();

		// get the docid
		int64_t d = Posdb::getDocId ( rec );

		// just let the best site rank win i guess?
		// even though one page may have more inlinks???
		char sr = (char )Posdb::getSiteRank ( rec );

		// skip if us
		if ( d == m_docId ) continue;

		// if his rank is <= ours then he was here first and we
		// are the dup i guess...
		if ( sr >= myRank ) {
			log("build: doc %s is dup of docid %" PRId64,
			    m_firstUrl.getUrl(),d);
			m_isDup = true;
			m_isDupValid = true;
			m_docIdWeAreADupOf = d;
			logTrace( g_conf.m_logTraceXmlDoc, "END, we are a duplicate" );;
			return &m_isDup;
		}

	}

	m_isDup = false;
	m_isDupValid = true;
	logTrace( g_conf.m_logTraceXmlDoc, "END, done. Not dup." );;
	return &m_isDup;
}

char *XmlDoc::getMetaDescription( int32_t *mdlen ) {
	if ( m_metaDescValid ) {
		*mdlen = m_metaDescLen;
		return m_metaDesc;
	}
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;

	// we need to point to it in the html source so our WordPosInfo algo works right.
	m_metaDesc = xml->getMetaContentPointer( "description", 11, "name", &m_metaDescLen );
	*mdlen = m_metaDescLen;
	m_metaDescValid = true;
	return m_metaDesc;
}


char *XmlDoc::getMetaSummary ( int32_t *mslen ) {
	if ( m_metaSummaryValid ) {
		*mslen = m_metaSummaryLen;
		return m_metaSummary;
	}
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;

	m_metaSummary = xml->getMetaContentPointer( "summary", 7, "name", &m_metaSummaryLen );
	*mslen = m_metaSummaryLen;
	m_metaSummaryValid = true;
	return m_metaSummary;
}


char *XmlDoc::getMetaKeywords( int32_t *mklen ) {
	if ( m_metaKeywordsValid ) {
		*mklen = m_metaKeywordsLen;
		return m_metaKeywords;
	}
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;

	// we need to point to it in the html source so our WordPosInfo algo works right.
	m_metaKeywords = xml->getMetaContentPointer( "keywords", 8, "name", &m_metaKeywordsLen );
	*mklen = m_metaKeywordsLen;
	m_metaKeywordsValid = true;
	return m_metaKeywords;
}


char *XmlDoc::getMetaGeoPlacename( int32_t *mgplen ) {
	if ( m_metaGeoPlacenameValid ) {
		*mgplen = m_metaGeoPlacenameLen;
		return m_metaGeoPlacename;
	}
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;

	// we need to point to it in the html source so our WordPosInfo algo works right.
	m_metaGeoPlacename = xml->getMetaContentPointer( "geo.placename", 13, "name", &m_metaGeoPlacenameLen );
	*mgplen = m_metaGeoPlacenameLen;
	m_metaGeoPlacenameValid = true;
	return m_metaGeoPlacename;
}

Url *XmlDoc::getCurrentUrl ( ) {
	if ( m_currentUrlValid ) return &m_currentUrl;
	// otherwise, get first url
	Url *fu = getFirstUrl();
	if ( ! fu || fu == (void *)-1 ) return (Url *)fu;
	// make that current url
	m_currentUrl.set ( &m_firstUrl );
	m_currentUrlValid = true;
	return &m_currentUrl;
}

Url *XmlDoc::getFirstUrl() {
	if ( m_firstUrlValid ) return &m_firstUrl;
	// we might have a title rec
	if ( m_setFromTitleRec ) {
		setFirstUrl ( ptr_firstUrl );
		m_firstUrlValid = true;
		return &m_firstUrl;
	}
	// must be this otherwise
	if ( ! m_setFromDocId ) { g_process.shutdownAbort(true); }
	// this must be valid
	if ( ! m_docIdValid ) { g_process.shutdownAbort(true); }

	// get the old xml doc from the old title rec
	XmlDoc **pod = getOldXmlDoc ( );
	if ( ! pod || pod == (void *)-1 ) return (Url *)pod;
	// shortcut
	XmlDoc *od = *pod;
	// now set it
	setFirstUrl ( od->ptr_firstUrl );
	m_firstUrlValid = true;
	return &m_firstUrl;
}


int64_t XmlDoc::getFirstUrlHash48() {
	if ( m_firstUrlHash48Valid ) return m_firstUrlHash48;
	// this must work
	if ( ! m_firstUrlValid ) { g_process.shutdownAbort(true); }
	if ( getUseTimeAxis() ) {
		m_firstUrlHash48 = hash64b ( getTimeAxisUrl()->getBufStart() ) & 0x0000ffffffffffffLL;
		m_firstUrlHash48Valid = true;
		return m_firstUrlHash48;
	}

	m_firstUrlHash48 = hash64b ( m_firstUrl.getUrl() ) & 0x0000ffffffffffffLL;
	m_firstUrlHash48Valid = true;
	return m_firstUrlHash48;
}

int64_t XmlDoc::getFirstUrlHash64() {
	if ( m_firstUrlHash64Valid ) return m_firstUrlHash64;
	// this must work
	if ( ! m_firstUrlValid ) { g_process.shutdownAbort(true); }

	if ( getUseTimeAxis() ) {
		m_firstUrlHash64 = hash64b ( getTimeAxisUrl()->getBufStart() );
		m_firstUrlHash64Valid = true;
		return m_firstUrlHash64;
	}

	m_firstUrlHash64 = hash64b ( m_firstUrl.getUrl() );
	m_firstUrlHash64Valid = true;
	return m_firstUrlHash64;
}

Url **XmlDoc::getLastRedirUrl() {

	Url **ru = getRedirUrl();
	if ( ! ru || ru == (void *)-1 ) return ru;

	// m_redirUrlPtr will be NULL in all cases, however, the
	// last redir url we actually got will be set in
	// m_redirUrl.m_url so return that.
	m_lastRedirUrlPtr = &m_redirUrl;
	return &m_lastRedirUrlPtr;
}

// . operates on the latest m_httpReply
Url **XmlDoc::getRedirUrl() {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	if ( m_redirUrlValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning already valid redirUrl" );;
		return &m_redirUrlPtr;
	}

	setStatus ( "getting redir url" );

	// assume no redirect
	m_redirUrlPtr = NULL;

	// we might have a title rec
	if ( m_setFromTitleRec ) { g_process.shutdownAbort(true); }

	// or recycling content from old title rec
	if ( m_recycleContent ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, return redirUrl from old TitleRec" );;
		m_redirError = 0;
		m_redirErrorValid = true;
		m_redirUrlValid = true;
		return &m_redirUrlPtr;
	}

	// get the current http reply, not the final http reply necessarily
	if ( ! m_httpReplyValid ) { g_process.shutdownAbort(true); }

	// set a mime on the stack
	HttpMime mime;

	// shortcut
	int32_t httpReplyLen = m_httpReplySize - 1;

	// sanity check
	if ( httpReplyLen > 0 && ! m_httpReply ) { g_process.shutdownAbort(true); }

	// empty reply, no redir
	if ( httpReplyLen == 0 ) {
		// bad mime, but i guess valid empty redir url
		m_redirUrlValid = true;
		// no error
		m_redirError = 0;
		m_redirErrorValid = true;

		logTrace( g_conf.m_logTraceXmlDoc, "END, returning fake. Length is 0" );;
		// return a fake thing. content length is 0.
		return &m_redirUrlPtr;
	}

	// set it
	if ( httpReplyLen<0 || ! mime.set ( m_httpReply, httpReplyLen, getCurrentUrl() ) ) {
		// bad mime, but i guess valid empty redir url
		m_redirUrlValid = true;

		// return nothing, no redirect url was there
		m_redirUrlPtr = NULL;

		// no error
		m_redirError = 0;
		m_redirErrorValid = true;

		// return a fake thing. content length is 0.
		logTrace( g_conf.m_logTraceXmlDoc, "END, returning fake. Bad mime." );;

		return &m_redirUrlPtr;
	}

	int32_t httpStatus = mime.getHttpStatus();

	Url *loc = NULL;

	// quickly see if we are a robots.txt url originally
	bool isRobotsTxt = isFirstUrlRobotsTxt ( );

	//
	// check for <meta http-equiv="Refresh" content="1; URL=contact.htm">
	// if httpStatus is not a redirect
	//
	if ( httpStatus < 300 || httpStatus > 399 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "Checking meta for redirect, if not robot.txt" );;

		// ok, crap, i was getting the xml here to get the meta
		// http-equiv refresh tag, but that added an element of
		// recursion that is just too confusing to deal with. so
		// let's just parse out the meta tag by hand
		if ( !isRobotsTxt ) {
			Url **mrup = getMetaRedirUrl();
			if ( ! mrup || mrup == (void *)-1) {
				logTrace( g_conf.m_logTraceXmlDoc, "END, bad meta?" );;
				return (Url **)mrup;
			}

			// set it. might be NULL if not there.
			loc = *mrup;
		}
	} else {
		logTrace( g_conf.m_logTraceXmlDoc, "call mime.getLocationUrl" );;
		// get Location: url (the redirect url) from the http mime
		loc = mime.getLocationUrl();
	}

	// get current url
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, error, could not get current url" );;
		return (Url **)cu;
	}

	// get local link info
	LinkInfo *info1 = getLinkInfo1();
	// error or blocked
	if ( ! info1 || info1 == (LinkInfo *)-1 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, error, could not get LinkInfo1" );;
		return (Url **)info1;
	}

	// did we send a cookie with our last request?
	bool sentCookieLastTime = false;
	if ( m_redirCookieBuf.length() ) {
		sentCookieLastTime = true;
	}

	// get cookie for redirect to fix nyt.com/nytimes.com
	// for gap.com it uses multiple Set-Cookie:\r\n lines so we have
	// to accumulate all of them into a buffer now
	m_redirCookieBuf.reset();
	mime.addCookiesIntoBuffer ( &m_redirCookieBuf );
	m_redirCookieBufValid = true;

	// a hack for removing session ids already in there
	// must not have an actual redirect url in there & must be a valid http status
	if ( !loc && httpStatus == 200 ) {
		Url *tt = &m_redirUrl;
		tt->set( cu->getUrl(), cu->getUrlLen(), false, true );

		// if url changes, force redirect it
		if ( strcmp ( cu->getUrl(), tt->getUrl() ) != 0 ) {
			m_redirUrlValid = true;
			m_redirUrlPtr   = &m_redirUrl;

			m_redirUrlValid = true;
			ptr_redirUrl    = m_redirUrl.getUrl();
			size_redirUrl   = m_redirUrl.getUrlLen()+1;

			/// @todo ALC should we use EDOCSIMPLIFIEDREDIR
			// m_redirError = EDOCSIMPLIFIEDREDIR;

			// no error
			m_redirError = 0;

			m_redirErrorValid = true;

			logTrace( g_conf.m_logTraceXmlDoc, "END, Forced redirect from '%s' to '%s'", cu->getUrl(),m_redirUrl.getUrl() );
			return &m_redirUrlPtr;
		}
	}

	// if no location url, then no redirect a NULL redir url
	if ( ! loc || loc->getUrl()[0] == '\0' ) {
		// validate it
		m_redirUrlValid = true;
		// no error
		m_redirError = 0;
		m_redirErrorValid = true;
		// and return an empty one
		logTrace( g_conf.m_logTraceXmlDoc, "END, no redir url (no loc)" );;
		return &m_redirUrlPtr;
	}

	bool keep = false;
	if ( info1->hasLinkText() ) keep = true;

	// at this point we do not block anywhere
	m_redirUrlValid = true;

	// store the redir error
	m_redirError      = 0;
	m_redirErrorValid = true;

	// i've seen a "Location: 2010..." bogus url as well, so make sure
	// we got a legit url
	if ( ! loc->getDomain() || loc->getDomainLen() <= 0 ) {
		if ( ! keep ) m_redirError = EDOCBADREDIRECTURL;

		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCBADREDIRECTURL" );;
		return &m_redirUrlPtr;
	}

	// . if redirect url is nothing new, then bail (infinite loop)
	// . www.xbox.com/SiteRequirements.htm redirects to itself
	//   until you send a cookie!!
	// . www.twomileborris.com does the cookie thing, too
	if ( strcmp ( cu->getUrl(), loc->getUrl() ) == 0 ) {
		// try sending the cookie if we got one now and didn't have
		// one for this last request
		if ( ! sentCookieLastTime && m_redirCookieBuf.length() ) {
		        m_redirUrl.set ( loc->getUrl() );
		        m_redirUrlPtr = &m_redirUrl;
		        return &m_redirUrlPtr;
		}
		if ( ! keep ) m_redirError = EDOCREDIRECTSTOSELF;

		logTrace( g_conf.m_logTraceXmlDoc, "END, redir err" );;
		return &m_redirUrlPtr;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. getCollRec returned false" );;
		return NULL;
	}

	// . don't allow redirects when injecting!
	// . otherwise, we would mfree(m_buf) which would free our
	//   injected reply... yet m_injectedReplyLen would still be
	//   positive! can you say 'seg fault'?
	// . hmmm... seems to have worked though
	if ( cr->m_recycleContent || m_recycleContent ) {
		if ( ! keep ) m_redirError = EDOCTOOMANYREDIRECTS;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCTOOMANYREDIRECTS (recycled)" );;
		return &m_redirUrlPtr;
	}

	// . if we followed too many then bail
	// . www.motorolamobility.com www.outlook.com ... failed when we
	//   had >= 4 here
	if ( ++m_numRedirects >= 10 ) {
		if ( ! keep ) m_redirError = EDOCTOOMANYREDIRECTS;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCTOOMANYREDIRECTS" );;
		return &m_redirUrlPtr;
	}

	// sometimes idiots don't supply us with a Location: mime
	if ( loc->getUrlLen() == 0 ) {
		if ( ! keep ) m_redirError = EDOCBADREDIRECTURL;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EDOCBADREDIRECTURL" );;
		return &m_redirUrlPtr;
	}

	// . protocol of url must be http or https
	// . we had one url redirect to an ihttp:// protocol and caused
	//   spider to core dump when it saw that SpiderRequest record
	const char *proto = loc->getScheme();
	if ( strncmp(proto,"http://" ,7) && strncmp(proto,"https://",8) ) {
		m_redirError = EDOCBADREDIRECTURL;
		logTrace( g_conf.m_logTraceXmlDoc, "END, EBADREDIRECTURL (wrong scheme)" );;
		return &m_redirUrlPtr;
	}

	// log a msg
	if ( g_conf.m_logSpideredUrls ) {
		logf( LOG_INFO, "build: %s redirected to %s", cu->getUrl(), loc->getUrl());
	}

	// if not same Domain, it is not a simplified redirect
	bool sameDom = true;
	int32_t dlen = loc->getDomainLen();
	if ( cu->getDomainLen() != dlen  || ( strncmp(cu->getDomain(), loc->getDomain(), dlen) ) ) {
		sameDom = false;
	}

	if ( ! sameDom ) {
		m_redirUrl.set ( loc );
		m_redirUrlPtr   = &m_redirUrl;
		ptr_redirUrl    = m_redirUrl.getUrl();
		size_redirUrl    = m_redirUrl.getUrlLen()+1;
		logTrace( g_conf.m_logTraceXmlDoc, "END, return redirUrl. Not same domain [%s]", m_redirUrlPtr->getUrl());
		return &m_redirUrlPtr;
	}

	// get first url ever
	Url *f = getFirstUrl();

	// set this to true if the redirected urls is much preferred
	bool simplifiedRedir = false;

	// . if it redirected to a simpler url then stop spidering now
	//   and add the simpler url to the spider queue
	// . by simpler, i mean one w/ fewer path components
	// . or one with a www for hostname
	// . or could be same as firstUrl but with a / appended
	char *r   = loc->getUrl();
	char *u   = f->getUrl();
	int32_t rlen = loc->getUrlLen();
	int32_t ulen = f->getUrlLen();

	// simpler if new path depth is shorter
	if ( !simplifiedRedir && loc->getPathDepth( true ) < f->getPathDepth( true ) ) {
		simplifiedRedir = true;
	}

	// simpler if old has cgi and new does not
	if ( !simplifiedRedir && f->isCgi() && ! loc->isCgi() ) {
		simplifiedRedir = true;
	}

	// simpler if new one is same as old but has a '/' at the end
	if ( !simplifiedRedir && rlen == ulen+1 && r[rlen-1]=='/' && strncmp(r, u, ulen) == 0 ) {
		simplifiedRedir = true;
	}

	// . if new url does not have semicolon but old one does
	// . http://news.yahoo.com/i/738;_ylt=AoL4eFRYKEdXbfDh6W2cF
	//   redirected to http://news.yahoo.com/i/738
	if ( !simplifiedRedir && strchr ( u, ';' ) &&  ! strchr ( r, ';' ) ) {
		simplifiedRedir = true;
	}

	// simpler is new host is www and old is not
	if ( !simplifiedRedir && loc->isHostWWW() && ! f->isHostWWW() ) {
		simplifiedRedir = true;
	}

	// if redirect is to different domain, set simplified
	// this helps locks from bunching on one domain
	if ( !simplifiedRedir && ( loc->getDomainLen() != f->getDomainLen() ||
	     strncasecmp ( loc->getDomain(), f->getDomain(), loc->getDomainLen() ) != 0 ) ) {
		// crap, but www.hotmail.com redirects to live.msn.com
		// login page ... so add this check here
		if ( !f->isRoot() ) {
			simplifiedRedir = true;
		}
	}

	bool allowSimplifiedRedirs = m_allowSimplifiedRedirs;

	// follow redirects if injecting so we do not return
	// EDOCSIMPLIFIEDREDIR
	if ( getIsInjecting ( ) ) {
		allowSimplifiedRedirs = true;
	}

	// or if disabled then follow the redirect
	if ( ! cr->m_useSimplifiedRedirects ) {
		allowSimplifiedRedirs = true;
	}

	// special hack for nytimes.com. do not consider simplified redirs
	// because it uses a cookie along with redirs to get to the final
	// page.
	const char *dom2 = m_firstUrl.getDomain();
	int32_t  dlen2 = m_firstUrl.getDomainLen();

	//@todo BR: EEK! Improve this.
	if ( dlen2 == 11 && strncmp(dom2,"nytimes.com",dlen2)==0 ) {
		allowSimplifiedRedirs = true;
	}

	// same for bananarepublic.gap.com ?
//	if ( dlen2 == 7 && strncmp(dom2,"gap.com",dlen2)==0 )
//		allowSimplifiedRedirs = true;

	// if redirect is setting cookies we have to follow the redirect
	// all the way through so we can stop now.
	if ( m_redirCookieBufValid && m_redirCookieBuf.getLength() ) {
		allowSimplifiedRedirs = true;
	}

	// . don't bother indexing this url if the redir is better
	// . 301 means moved PERMANENTLY...
	// . many people use 301 on their root pages though, so treat
	//   it like a temporary redirect, like exclusivelyequine.com
	if ( simplifiedRedir && ! allowSimplifiedRedirs ) {
		m_redirError = EDOCSIMPLIFIEDREDIR;

		// set this because getLinks() treats this redirUrl
		// as a link now, it will add a SpiderRequest for it:
		m_redirUrl.set ( loc );
		m_redirUrlPtr = &m_redirUrl;

		// mdw: let this path through so contactXmlDoc gets a proper
		// redirect that we can follow. for the base xml doc at
		// least the m_indexCode will be set
		logTrace( g_conf.m_logTraceXmlDoc, "END, return [%s]. Simplified, but not allowed.", m_redirUrlPtr->getUrl());
		return &m_redirUrlPtr;
	}

	// good to go
	m_redirUrl.set ( loc );
	m_redirUrlPtr = &m_redirUrl;
	ptr_redirUrl = m_redirUrl.getUrl();
	size_redirUrl = m_redirUrl.getUrlLen()+1;
	logTrace( g_conf.m_logTraceXmlDoc, "END, return [%s]", m_redirUrlPtr->getUrl());
	return &m_redirUrlPtr;
}



int32_t *XmlDoc::getFirstIndexedDate ( ) {
	if ( m_firstIndexedDateValid ) return (int32_t *)&m_firstIndexedDate;
	XmlDoc **od = getOldXmlDoc ( );
	if ( ! od || od == (XmlDoc **)-1 ) return (int32_t *)od;
	// valid
	m_firstIndexedDateValid = true;
	// must be downloaded
	//if ( ! m_spideredTimeValid ) { g_process.shutdownAbort(true); }
	// assume now is the first time
	m_firstIndexedDate = getSpideredTime();//m_spideredTime;
	// inherit from our old title rec
	if ( *od ) m_firstIndexedDate = (*od)->m_firstIndexedDate;
	// return it
	return (int32_t *)&m_firstIndexedDate;
}


int32_t *XmlDoc::getOutlinksAddedDate ( ) {
	if ( m_outlinksAddedDateValid ) return (int32_t *)&m_outlinksAddedDate;
	XmlDoc **od = getOldXmlDoc ( );
	if ( ! od || od == (XmlDoc **)-1 ) return (int32_t *)od;
	// valid
	m_outlinksAddedDateValid = true;
	// must be downloaded
	//if ( ! m_spideredTimeValid ) { g_process.shutdownAbort(true); }
	// assume we are doing it now
	m_outlinksAddedDate = getSpideredTime();//m_spideredTime;
	// get that
	if ( *od ) m_outlinksAddedDate = (*od)->m_outlinksAddedDate;
	// return it
	return (int32_t *)&m_outlinksAddedDate;
}

uint16_t *XmlDoc::getCountryId ( ) {
	if ( m_countryIdValid ) return &m_countryId;

	setStatus ( "getting country id" );

	// get it
	Url *u = getCurrentUrl();
	if ( ! u || u == (void *)-1) return (uint16_t *)u;

	// use the url's tld to guess the country
	uint16_t country = LanguageIdentifier::guessCountryTLD ( u->getUrl ( ) );

	m_countryIdValid = true;
	m_countryId      = country;

	return &m_countryId;
}

XmlDoc **XmlDoc::getOldXmlDoc ( ) {

	if ( m_oldDocValid ) return &m_oldDoc;

	// note it
	setStatus ( "getting old xml doc");

	// if we are set from a title rec, we are the old doc
	if ( m_setFromTitleRec ) {
		m_oldDocValid = true;
		m_oldDoc      = NULL;//this;
		return &m_oldDoc;
	}

	// . cache age is 0... super fresh
	// . returns NULL w/ g_errno if not found unless isIndexed is false
	//   and valid, and it is not valid for pagereindexes.
	char **otr = getOldTitleRec ( );
	if ( ! otr || otr == (char **)-1 ) return (XmlDoc **)otr;
	// if no title rec, return ptr to a null
	m_oldDoc = NULL;
	if ( ! *otr ) { m_oldDocValid = true; return &m_oldDoc; }

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// if provided title rec matches our docid but not uh48 then there
	// was a docid collision and we should null out our title rec
	// and return with an error and no index this puppy!
	// crap, we can't call getFirstUrl() because it might not be
	// valid if we are a docid based doc and THIS function was called
	// from getFirstUrl() -- we end up in a recursive loop.
	if ( ! m_setFromDocId ) {
		//int64_t uh48 = getFirstUrl()->getUrlHash48();
		int64_t uh48 = getFirstUrlHash48();
		int64_t tuh48 = Titledb::getUrlHash48 ( (key96_t *)*otr );
		if ( uh48 != tuh48 ) {
			log("xmldoc: docid collision uh48 mismatch. cannot "
				"index "
				"%s",getFirstUrl()->getUrl() );
			g_errno = EDOCIDCOLLISION;
			return NULL;
		}
	}

	// . if *otr is NULL that means not found
	// . return a NULL old XmlDoc in that case as well?
	// . make a new one
	// . this will uncompress it and set ourselves!
	try { m_oldDoc = new ( XmlDoc ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return NULL;
	}
	mnew ( m_oldDoc , sizeof(XmlDoc),"xmldoc1");
	// debug the mem leak
	// log("xmldoc: xmldoc1=%" PTRFMT" u=%s"
	//     ,(PTRTYPE)m_oldDoc
	//     ,m_firstUrl.getUrl());
	// if title rec is corrupted data uncompress will fail and this
	// will return false!
	if ( ! m_oldDoc->set2 ( m_oldTitleRec ,
				m_oldTitleRecSize , // maxSize
				cr->m_coll     ,
				NULL       , // pbuf
				m_niceness ) ) {
		log("build: failed to set old doc for %s",m_firstUrl.getUrl());
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		//int32_t saved = g_errno;
		// ok, fix the memleak here
		mdelete ( m_oldDoc , sizeof(XmlDoc), "odnuke" );
		delete ( m_oldDoc );

		//m_oldDocExistedButHadError = true;
		//log("xmldoc: nuke xmldoc1=%" PTRFMT"",(PTRTYPE)m_oldDoc);
		m_oldDoc = NULL;
		// g_errno = saved;
		// MDW: i removed this on 2/8/2016 again so the code below
		// would execute.
		//return NULL; //mdwmdwmdw
		// if it is data corruption, just assume empty so
		// we don't stop spidering a url because of this. so we'll
		// think this is the first time indexing it. otherwise
		// we get "Bad cached document" in the logs and the
		// SpiderReply and it never gets re-spidered because it is
		// not a 'temporary' error according to the url filters.
		log("build: treating corrupted titlerec as not found");
		g_errno = 0;
		m_oldDoc = NULL;
		m_oldDocValid = true;
		return &m_oldDoc;
	}
	m_oldDocValid = true;
	// share our masterloop and state!
	m_oldDoc->m_masterLoop  = m_masterLoop;
	m_oldDoc->m_masterState = m_masterState;
	return &m_oldDoc;
}

void XmlDoc::nukeDoc ( XmlDoc *nd ) {
	// skip if empty
	if ( ! nd ) return;
	// debug the mem leak
	// if ( nd == m_oldDoc )
	// 	log("xmldoc: nuke xmldoc1=%" PTRFMT" u=%s this=%" PTRFMT""
	// 	    ,(PTRTYPE)m_oldDoc
	// 	    ,m_firstUrl.getUrl()
	// 	    ,(PTRTYPE)this
	// 	    );
	// do not nuke yerself!
	if ( nd == this ) return;
	// or root doc!
	//if ( nd == m_rootDoc ) return;
	// invalidate
	if ( nd == m_extraDoc ) {
		m_extraDocValid = false;
		m_extraDoc      = NULL;
	}
	if ( nd == m_rootDoc    ) {
		m_rootDocValid    = false;
		m_rootDoc         = NULL;
	}
	if ( nd == m_oldDoc     ) {
		m_oldDocValid     = false;
		m_oldDoc          = NULL;
	}
	// nuke it
	mdelete ( nd , sizeof(XmlDoc) , "xdnuke");
	delete ( nd );
}

static LinkInfo s_dummy;

XmlDoc **XmlDoc::getExtraDoc ( char *u , int32_t maxCacheAge ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN [%s]", u);

	if ( m_extraDocValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END. m_extraDocValid is true" );
		return &m_extraDoc;
	}

	// note that
	setStatus ( "getting new doc" );
	// we need a valid first ip first!
	//int32_t *pfip = getFirstIp();
	//if ( ! pfip || pfip == (void *)-1 ) return (XmlDoc **)pfip;
	// must be NULL
	if ( m_extraDoc ) { g_process.shutdownAbort(true); }
	// sanity check
	if ( ! u || ! u[0] ) { g_process.shutdownAbort(true); }//return &m_extraDoc;
	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END - collection not found" );
		return NULL;
	}

	// . if *otr is NULL that means not found
	// . return a NULL old XmlDoc in that case as well?
	// . make a new one
	// . this will uncompress it and set ourselves!
	try { m_extraDoc = new ( XmlDoc ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		logTrace( g_conf.m_logTraceXmlDoc, "END - out of memory" );
		return NULL;
	}
	mnew ( m_extraDoc , sizeof(XmlDoc),"xmldoc2");

	// . if we did not have it in titledb then download it!
	// . or if titleRec was too old!

	// a spider rec for the extra doc to use
	SpiderRequest sreq;
	// clear it
	sreq.reset();
	// spider the url "u"
	strcpy ( sreq.m_url , u );
	// inherit page parser
	sreq.m_isPageParser = getIsPageParser();
	// set the data size right
	sreq.setDataSize();
	// . prepare to download it, set it up
	// . returns false and sets g_errno on error
	if ( ! m_extraDoc->set4 ( &sreq        ,
				  NULL         , // doledbkey ptr
				  cr->m_coll       ,
				  NULL         , // SafeBuf
				  m_niceness   ))
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END. set4 failed" );
		return NULL;
	}

	// share our masterloop and state!
	m_extraDoc->m_masterLoop  = m_masterLoop;
	m_extraDoc->m_masterState = m_masterState;

	// carry this forward always!
	m_extraDoc->m_isSpiderProxy = m_isSpiderProxy;

	// tell msg13 to get this from it robots.txt cache if it can. it also
	// keeps a separate html page cache for the root pages, etc. in case
	m_extraDoc->m_maxCacheAge = maxCacheAge;

	// a dummy thing
	s_dummy.m_numStoredInlinks = 0;
	s_dummy.m_numGoodInlinks  = 0;

	// we indirectly call m_extraDoc->getHttpReply() which calls
	// m_extraDoc->getRedirectUrl(), which checks the linkInfo and
	// dmoz catids of the original url to see if we should set m_indexCode
	// to something bad or not. to avoid these unnecessary lookups we
	// set these to NULL and validate them
	m_extraDoc->ptr_linkInfo1           = &s_dummy;
	m_extraDoc->size_linkInfo1          = 0;
	m_extraDoc->m_linkInfo1Valid        = true;
	m_extraDoc->m_urlFilterNumValid     = true;
	m_extraDoc->m_urlFilterNum          = 0;
	// for redirects
	m_extraDoc->m_allowSimplifiedRedirs = true;
	// set this flag so msg13.cpp doesn't print the "hammering ip" msg
	m_extraDoc->m_isChildDoc = true;

	// and inherit test dir so getTestDir() doesn't core on us
	bool isPageParser = getIsPageParser();
	m_extraDoc->m_isPageParser      = isPageParser;
	m_extraDoc->m_isPageParserValid = true;

	// without this we send all the msg13 requests to host #3! because
	// Msg13 uses it to determine what host to handle it
	if ( ! m_firstIpValid ) { g_process.shutdownAbort(true); }
	m_extraDoc->m_firstIp = m_firstIp;
	m_extraDoc->m_firstIpValid = true;

	// i guess we are valid now
	m_extraDocValid = true;

	logTrace( g_conf.m_logTraceXmlDoc, "END." );
	return &m_extraDoc;
}


bool XmlDoc::getIsPageParser ( ) {
	if ( m_isPageParserValid ) return m_isPageParser;
	// assume not
	m_isPageParser = false;
	// and set otherwise
	if ( m_sreqValid && m_sreq.m_isPageParser ) m_isPageParser = true;
	// and validate
	m_isPageParserValid = true;
	return m_isPageParser;
}

XmlDoc **XmlDoc::getRootXmlDoc ( int32_t maxCacheAge ) {
	if ( m_rootDocValid ) return &m_rootDoc;
	// help avoid mem leaks
	if ( m_rootDoc ) { g_process.shutdownAbort(true); }
	// note it

	setStatus ( "getting root doc");
	// are we a root?
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (XmlDoc **)isRoot;
	// if we are root use us!!!!!
	if ( *isRoot ) {
		m_rootDoc = this;
		m_rootDocValid = true;
		return &m_rootDoc;
	}
	// get our site root
	char *_mysite = getSite();
	if ( ! _mysite || _mysite == (void *)-1 ) return (XmlDoc **)_mysite;


	// BR 20151215: Prefix domain with the scheme, otherwise it will later
	// prefix with http:// in Url::set even for https sites.
	char sitebuf[MAX_SITE_LEN + MAX_SCHEME_LEN+4];	// +4 = :// + 0-terminator
	char *mysite 	= sitebuf;
	const char *myscheme 	= getScheme();
	if( myscheme ) {
		mysite += sprintf(mysite, "%s://", myscheme);
	}
	sprintf(mysite, "%s", _mysite);
	mysite = sitebuf;


	// otherwise, we gotta get it!
	char **rtr = getRootTitleRec ( );
	if ( ! rtr || rtr == (char **)-1 ) return (XmlDoc **)rtr;
	// if no title rec, return ptr to a null
	//m_rootDoc = NULL;
	//if ( ! *rtr ) {
	//	// damn, not in titledb, i guess download it then
	//	m_rootDocValid = true; return &m_rootDoc; }
	// note it
	setStatus ( "getting root doc");

	// to keep injections fast, do not download the root page!
	if ( ! *rtr && m_contentInjected ) {
		// assume none
		m_rootDoc = NULL;
		m_rootDocValid = true;
		return &m_rootDoc;
	}

	// likewise, if doing a rebuild
	if ( ! *rtr && m_useSecondaryRdbs ) {
		// assume none
		m_rootDoc = NULL;
		m_rootDocValid = true;
		return &m_rootDoc;
	}

	// or recycling content like for query reindex. keep it fast.
	if ( ! *rtr && m_recycleContent ) {
		m_rootDoc = NULL;
		m_rootDocValid = true;
		return &m_rootDoc;
	}


	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// . if *otr is NULL that means not found
	// . return a NULL root XmlDoc in that case as well?
	// . make a new one
	// . this will uncompress it and set ourselves!
	try { m_rootDoc = new ( XmlDoc ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return NULL;
	}
	mnew ( m_rootDoc , sizeof(XmlDoc),"xmldoc3");
	// if we had the title rec, set from that
	if ( *rtr ) {
		if ( ! m_rootDoc->set2 ( m_rootTitleRec     ,
					 m_rootTitleRecSize , // maxSize    ,
					 cr->m_coll             ,
					 NULL               , // pbuf
					 m_niceness         ) ) {
			// it was corrupted... delete this
			// possibly printed
			// " uncompress uncompressed size=..." bad uncompress
			log("build: rootdoc set2 failed");
			mdelete ( m_rootDoc , sizeof(XmlDoc) , "xdnuke");
			delete ( m_rootDoc );
			// call it empty for now, we don't want to return
			// NULL with g_errno set because it could stop
			// the whole indexing pipeline
			m_rootDoc = NULL;
			m_rootDocValid = true;
			return &m_rootDoc;
			//return NULL;
		}
	}
	// . otherwise, set the url and download it on demand
	// . this junk copied from the contactDoc->* stuff below
	else {
		// a spider rec for the contact doc
		SpiderRequest sreq;
		// clear it
		sreq.reset();
		// spider the url "u"
		strcpy ( sreq.m_url , mysite );
		// set this
		if ( m_sreqValid ) {
			// this will avoid it adding to tagdb!
			sreq.m_isPageParser = m_sreq.m_isPageParser;
		}
		// reset the data size
	        sreq.setDataSize ();
		// . prepare to download it, set it up
		// . returns false and sets g_errno on error
		if ( ! m_rootDoc->set4 ( &sreq        ,
					 NULL         , // doledbkey ptr
					 cr->m_coll       ,
					 NULL         , // SafeBuf
					 m_niceness   )) {
			mdelete ( m_rootDoc , sizeof(XmlDoc) , "xdnuke");
			delete ( m_rootDoc );
			m_rootDoc = NULL;
			return NULL;
		}
		// do not throttle it!
		//m_rootDoc->m_throttleDownload = false;
		// . do not do robots check for it
		// . no we must to avoid triggering a bot trap & getting banned
		//m_rootDoc->m_isAllowed      = m_isAllowed;
		//m_rootDoc->m_isAllowedValid = true;
	}

	// share our masterloop and state!
	m_rootDoc->m_masterLoop  = m_masterLoop;
	m_rootDoc->m_masterState = m_masterState;

	// msg13 caches the pages it downloads
	m_rootDoc->m_maxCacheAge = maxCacheAge;

	// like m_contactDoc we avoid unnecessary lookups in call to
	// getRedirUrl() by validating these empty members
	m_rootDoc->ptr_linkInfo1           = &s_dummy;
	m_rootDoc->size_linkInfo1          = 0;
	m_rootDoc->m_linkInfo1Valid        = true;
	m_rootDoc->m_urlFilterNumValid     = true;
	m_rootDoc->m_urlFilterNum          = 0;
	// for redirects
	m_rootDoc->m_allowSimplifiedRedirs = true;
	// set this flag so msg13.cpp doesn't print the "hammering ip" msg
	m_rootDoc->m_isChildDoc = true;

	// validate it
	m_rootDocValid = true;
	return &m_rootDoc;
}

SafeBuf *XmlDoc::getTimeAxisUrl ( ) {
	if ( m_timeAxisUrlValid ) return &m_timeAxisUrl;
	if ( m_setFromDocId ) return &m_timeAxisUrl;
	m_timeAxisUrlValid = true;
	Url *fu = getFirstUrl();
	m_timeAxisUrl.reset();
	m_timeAxisUrl.safePrintf("%s.%u",fu->getUrl(),m_contentHash32);
	return &m_timeAxisUrl;
}

// . look up TitleRec using Msg22 if we need to
// . set our m_titleRec member from titledb
// . the twin brother of XmlDoc::getTitleRecBuf() which makes the title rec
//   from scratch. this loads it from titledb.
// . NULL is a valid value (EDOCNOTFOUND) so return a char **
char **XmlDoc::getOldTitleRec ( ) {
	// clear if we blocked
	//if ( g_errno == ENOTFOUND ) g_errno = 0;
	// if valid return that
	if ( m_oldTitleRecValid ) return &m_oldTitleRec;
	// update status msg
	setStatus ( "getting old title rec");
	// if we are set from a title rec, we are the old doc
	if ( m_setFromTitleRec ) {
		m_oldTitleRecValid = true;
		m_oldTitleRec      = NULL;//m_titleRec;
		return &m_oldTitleRec;
	}
	// sanity check
	if ( m_oldTitleRecValid && m_msg22a.isOutstanding() ) {
		g_process.shutdownAbort(true); }
	// point to url
	//char *u = getCurrentUrl()->getUrl();
	//char *u = getFirstUrl()->getUrl();

	// assume its valid
	m_oldTitleRecValid = true;
	//if ( maxCacheAge > 0 ) addToCache = true;

	// not if new! no we need to do this so XmlDoc::getDocId() works!
	// this logic prevents us from setting g_errno to ENOTFOUND
	// when m_msg22a below calls indexDocWrapper(). however, for
	// doing a query delete on a not found docid will succumb to
	// the g_errno because m_isIndexed is not valid i think...
	if ( m_isIndexedValid && ! m_isIndexed && m_docIdValid ) {
		m_oldTitleRec      = NULL;
		m_oldTitleRecValid = true;
		return &m_oldTitleRec;
	}
	// sanity check. if we have no url or docid ...
	if ( ! m_firstUrlValid && ! m_docIdValid ) { g_process.shutdownAbort(true); }
	// use docid if first url not valid
	int64_t docId = 0;
	if ( ! m_firstUrlValid ) docId = m_docId;
	// if url not valid, use NULL
	char *u = NULL;
	if ( docId == 0LL && ptr_firstUrl ) u = getFirstUrl()->getUrl();
	// if both are not given that is a problem
	if ( docId == 0LL && ! u ) {
		log("doc: no url or docid provided to get old doc");
		g_errno = EBADENGINEER;
		return NULL;
	}
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// if using time axis then append the timestamp to the end of
	// the url. this way Msg22::getAvailDocId() will return a docid
	// based on that so we don't collide with other instances of this
	// same url.
	if ( u && getUseTimeAxis() ) { // g_conf.m_useTimeAxis ) {
		SafeBuf *tau = getTimeAxisUrl();
		u = tau->getBufStart();
	}

	// the title must be local since we're spidering it
	if ( ! m_msg22a.getTitleRec ( &m_msg22Request      ,
				      u                    ,
				      docId                , // probable docid
				      cr->m_coll               ,
				      // . msg22 will set this to point to it!
				      // . if NULL that means NOT FOUND
				      &m_oldTitleRec       ,
				      &m_oldTitleRecSize   ,
				      false                , // just chk tfndb?
				      false , // getAvailDocIdOnly
				      m_masterState        ,
				      m_masterLoop         ,
				      m_niceness           , // niceness
				      999999               )) // timeout seconds
		// return -1 if we blocked
		return (char **)-1;
	// not really an error
	if ( g_errno == ENOTFOUND ) g_errno = 0;
	// error?
	if ( g_errno ) return NULL;
	// got it
	return &m_oldTitleRec;
}

// . look up TitleRec using Msg22 if we need to
// . set our m_titleRec member from titledb
// . the twin brother of XmlDoc::getTitleRecBuf() which makes the title rec
//   from scratch. this loads it from titledb.
// . NULL is a valid value (EDOCNOTFOUND) so return a char **
char **XmlDoc::getRootTitleRec ( ) {
	// if valid return that
	if ( m_rootTitleRecValid ) return &m_rootTitleRec;
	// are we a root?
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (char **)isRoot;
	// if we are root use us!!!!! well, the old us...
	if ( *isRoot ) {
		char **otr = getOldTitleRec ( );
		if ( ! otr || otr == (char **)-1 ) return (char **)otr;
		m_rootTitleRec     = m_oldTitleRec;
		m_rootTitleRecSize = m_oldTitleRecSize;
		return &m_rootTitleRec;
	}
	// get our site root
	char *mysite = getSite();
	if ( ! mysite || mysite == (char *)-1 ) return (char **)mysite;
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;
	// make it a url. keep it on stack since msg22 copies it into its
	// url request buffer anyway! (m_msg22Request.m_url[])
	Url site; site.set ( mysite );
	// assume its valid
	m_rootTitleRecValid = true;
	//if ( maxCacheAge > 0 ) addToCache = true;
	// update status msg
	setStatus ( "getting root title rec");
	// the title must be local since we're spidering it
        if ( ! m_msg22b.getTitleRec ( &m_msg22Request      ,
				      site.getUrl()        ,
				      0                    , // probable docid
				      cr->m_coll               ,
				      // . msg22 will set this to point to it!
				      // . if NULL that means NOT FOUND
				      &m_rootTitleRec      ,
				      &m_rootTitleRecSize  ,
				      false                , // just chk tfndb?
				      false , // getAvailDocIdOnly
				      m_masterState        ,
				      m_masterLoop         ,
				      m_niceness           , // niceness
				      999999               )) // timeout seconds
		// return -1 if we blocked
		return (char **)-1;
	// not really an error
	if ( g_errno == ENOTFOUND ) g_errno = 0;
	// error?
	if ( g_errno ) return NULL;
	// got it
	return &m_rootTitleRec;
}



// used for indexing spider replies. we need a unique docid because it
// is treated as a different document even though its url will be the same.
// and there is never an "older" version of it because each reply is treated
// as a brand new document.
int64_t *XmlDoc::getAvailDocIdOnly ( int64_t preferredDocId ) {
	if ( m_availDocIdValid && g_errno ) {
		log("xmldoc: error getting availdocid: %s",
		    mstrerror(g_errno));
		return NULL;
	}
	if ( m_availDocIdValid )
		// this is 0 or -1 if no avail docid was found
		return &m_msg22c.m_availDocId;
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;
	// pre-validate it
	m_availDocIdValid = true;
	if ( ! m_msg22c.getAvailDocIdOnly ( &m_msg22Requestc ,
					    preferredDocId ,
					    cr->m_coll ,
					    m_masterState ,
					    m_masterLoop ,
					    m_niceness ) )
		return (int64_t *)-1;
	// error?
	log("xmldoc: error getting availdocid2: %s",mstrerror(g_errno));
	return NULL;
}


int64_t *XmlDoc::getDocId ( ) {
	if ( m_docIdValid ) return &m_docId;
	setStatus ("getting docid");
	XmlDoc **od = getOldXmlDoc( );
	if ( ! od || od == (XmlDoc **)-1 ) return (int64_t *)od;
	setStatus ("getting docid");

	// . set our docid
	// . *od is NULL if no title rec found with that docid in titledb
	if ( *od ) {
		m_docId = *(*od)->getDocId();
		m_docIdValid = true;
		return &m_docId;
	}

	m_docId = m_msg22a.getAvailDocId();

	// if titlerec was there but not od it had an error uncompressing
	// because of the corruption bug in RdbMem.cpp when dumping to disk.
	if ( m_docId == 0 && m_oldTitleRec && m_oldTitleRecSize > 12 ) {
		m_docId = Titledb::getDocIdFromKey ( (key96_t *)m_oldTitleRec );
		log("build: salvaged docid %" PRId64" from corrupt title rec "
			"for %s",m_docId,m_firstUrl.getUrl());
	}

	if ( m_docId == 0 ) {
		log("build: docid is 0 for %s",m_firstUrl.getUrl());
		g_errno = ENODOCID;
		return NULL;
	}

	// ensure it is within probable range
	if ( ! getUseTimeAxis () ) {
		char *u = getFirstUrl()->getUrl();
		int64_t pd = Titledb::getProbableDocId(u);
		int64_t d1 = Titledb::getFirstProbableDocId ( pd );
		int64_t d2 = Titledb::getLastProbableDocId  ( pd );
		if ( m_docId < d1 || m_docId > d2 ) {
			g_process.shutdownAbort(true); }
	}

	// if docid is zero, none is a vailable!!!
	//if ( m_docId == 0LL ) m_indexCode = ENODOCID;
	m_docIdValid = true;
	return &m_docId;
}

// . is our docid on disk? i.e. do we exist in the index already?
// . TODO: just check tfndb?
char *XmlDoc::getIsIndexed ( ) {
	if ( m_isIndexedValid ) return &m_isIndexed;

	setStatus ( "getting is indexed" );

	// we must be old if this is true
	//if ( m_setFromTitleRec ) {
	//	m_isNew      = false;
	//	m_isNewValid = true;
	//	return &m_isNew;
	//}
	// get the url
	//char *u = getFirstUrl()->getUrl();

	if ( m_oldDocValid ) {
		m_isIndexedValid = true;
		if ( m_oldDoc ) m_isIndexed = true;
		else            m_isIndexed = false;
		return &m_isIndexed;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// sanity check. if we have no url or docid ...
	if ( ! m_firstUrlValid && ! m_docIdValid ) { g_process.shutdownAbort(true); }
	// use docid if first url not valid
	int64_t docId = 0;
	char      *url  = NULL;
	// use docid if its valid, otherwise use url
	if ( m_docIdValid ) docId = m_docId;
	else                url   = ptr_firstUrl;

	// note it
	if ( ! m_calledMsg22e )
		setStatus ( "checking titledb for old title rec");
	else
		setStatus ( "back from msg22e call");

	// . consult the title rec tree!
	// . "justCheckTfndb" is set to true here!
        if ( ! m_calledMsg22e &&
	     ! m_msg22e.getTitleRec ( &m_msg22Request      ,
				      url                  ,
				      docId                , // probable docid
				      cr->m_coll               ,
				      // . msg22 will set this to point to it!
				      // . if NULL that means NOT FOUND
				      NULL                 , // tr ptr
				      NULL                 , // tr size ptr
				      true                 , // just chk tfndb?
				      false, // getavaildocidonly
				      m_masterState        ,
				      m_masterLoop         ,
				      m_niceness           , // niceness
				      999999               )){ // timeout seconds
		// validate
		m_calledMsg22e = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, called msg22e.getTitleRec, which blocked. Return -1" );
		// return -1 if we blocked
		return (char *)-1;
	}

	logTrace( g_conf.m_logTraceXmlDoc, "msg22e.getTitleRec did not block" );

	// got it
	m_calledMsg22e = true;
	// error?
	if ( g_errno ) return NULL;
	// get it
	m_isIndexed = m_msg22e.wasFound();

	// validate
	m_isIndexedValid = true;
	logTrace( g_conf.m_logTraceXmlDoc, "END, returning isIndexed [%s]", m_isIndexed?"true":"false");
	return &m_isIndexed;
}

void gotTagRecWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// note it
	THIS->setStatus ( "in got tag rec wrapper" );
	// set these
	if ( ! g_errno ) {
		THIS->m_tagRec.serialize ( THIS->m_tagRecBuf );
		THIS->ptr_tagRecData =  THIS->m_tagRecBuf.getBufStart();
		THIS->size_tagRecData = THIS->m_tagRecBuf.length();
		// validate
		THIS->m_tagRecValid = true;
	}
	// continue
	THIS->m_masterLoop ( THIS->m_masterState );
}

// . returns NULL and sets g_errno on error
// . returns -1 if blocked, will re-call m_callback
TagRec *XmlDoc::getTagRec ( ) {
	// if we got it give it
	if ( m_tagRecValid ) return &m_tagRec;

	// do we got a title rec?
	if ( m_setFromTitleRec && m_tagRecDataValid ) {
		// we set m_tagRecValid and m_tagRecDataValid to false in Repair.cpp
		// if rebuilding titledb!! otherwise, we have to use what is in titlerec
	    // to avoid parsing inconsistencies that would result in undeletable posdb data.

	    // lookup the tagdb rec fresh if setting for a summary. that way
		// we can see if it is banned or not

		// all done
		m_tagRecValid = true;

		// just return empty otherwise
		m_tagRec.setFromBuf ( ptr_tagRecData , size_tagRecData );

		return &m_tagRec;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// update status msg
	setStatus ( "getting tagdb record" );

	// nah, try this
	Url *u = getFirstUrl();

	// get it, user our collection for lookups, not m_tagdbColl[] yet!
	if ( !m_msg8a.getTagRec( u, cr->m_collnum, m_niceness, this, gotTagRecWrapper, &m_tagRec ) ) {
		// we blocked, return -1
		return (TagRec *) -1;
	}

	// error? ENOCOLLREC?
	if ( g_errno ) {
		return NULL;
	}

	// assign it
	m_tagRec.serialize ( m_tagRecBuf );
	ptr_tagRecData =  m_tagRecBuf.getBufStart();
	size_tagRecData = m_tagRecBuf.length();

	// our tag rec should be all valid now
	m_tagRecValid = true;
	return &m_tagRec;
}




// we need this for setting SpiderRequest::m_parentFirstIp of each outlink
int32_t *XmlDoc::getFirstIp ( ) {
	// return it if we got it
	if ( m_firstIpValid ) return &m_firstIp;
	// note it
	setStatus ( "getting first ip");
	// get tag rec
	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 ) return (int32_t *)gr;
	// got it
	Tag *tag = gr->getTag ( "firstip" );
	// get from tag
	m_firstIp = 0;
	if ( tag ) m_firstIp = atoip(tag->getTagData());
	// if no tag, or is bogus in tag... set from ip
	if ( m_firstIp == 0 || m_firstIp == -1 ) {
		// need ip then!
		int32_t *ip = getIp();
		if ( ! ip || ip == (int32_t *)-1) return (int32_t *)ip;
		// set that
		m_firstIp = *ip;
	}
	m_firstIpValid = true;
	return &m_firstIp;
	// must be 4 bytes - no now its a string
	//if ( tag->getTagDataSize() != 4 ) { g_process.shutdownAbort(true); }
}

uint8_t *XmlDoc::getSiteNumInlinks8 () {
	if ( m_siteNumInlinks8Valid ) return &m_siteNumInlinks8;
	// get the full count
	int32_t *si = getSiteNumInlinks();
	if ( ! si || si == (int32_t *)-1 ) return (uint8_t *)si;
	// convert to 8
	m_siteNumInlinks8 = score32to8 ( *si );
	// validate
	m_siteNumInlinks8Valid = true;
	return &m_siteNumInlinks8;
}

// this is the # of GOOD INLINKS to the site. so it is no more than
// 1 per c block, and it has to pass link spam detection. this is the
// highest-level count of inlinks to the site. use it a lot.
int32_t *XmlDoc::getSiteNumInlinks ( ) {

	if ( m_siteNumInlinksValid ) return &m_siteNumInlinks;

	// sanity check
	if ( m_setFromTitleRec && ! m_useSecondaryRdbs) {g_process.shutdownAbort(true);}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// hacks of speed. computeSiteNumInlinks is true by default
	// but if the user turns it off the just use sitelinks.txt
	if ( cr && ! cr->m_computeSiteNumInlinks ) {
		int32_t hostHash32 = getHostHash32a();
		int32_t min = g_tagdb.getMinSiteInlinks ( hostHash32 );
		// try with www if not there
		if ( min < 0 && ! m_firstUrl.hasSubdomain() ) {
			int32_t wwwHash32 = m_firstUrl.getHash32WithWWW();
			min = g_tagdb.getMinSiteInlinks ( wwwHash32 );
		}
		// fix core by setting these
		//a nd this
		m_siteNumInlinksValid = true;
		m_siteNumInlinks      = 0;
		// if still not in sitelinks.txt, just use 0
		if ( min < 0 ) {
			return &m_siteNumInlinks;
		}
		m_siteNumInlinks = min;
		return &m_siteNumInlinks;
	}

	setStatus ( "getting site num inlinks");

	// get it from the tag rec if we can
	TagRec *gr = getTagRec ();
	if ( ! gr || gr == (void *)-1 ) return (int32_t *)gr;

	// the current top ip address
	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1) return (int32_t *)ip;
	//int32_t top = *ip & 0x00ffffff;

	// this happens when its NXDOMAIN reply from dns so assume
	// no site inlinks
	if ( *ip == 0 ) {
		m_siteNumInlinks             = 0;
		m_siteNumInlinksValid             = true;
		return &m_siteNumInlinks;
	}

	if ( *ip == -1 ) {
		log("xmldoc: ip is %" PRId32", can not get site inlinks",*ip);
		g_errno = EBADIP;
		return NULL;
	}

	// wait for clock to sync before calling getTimeGlobal
	int32_t wfts = waitForTimeSync();
	// 0 means error, i guess g_errno should be set, -1 means blocked
	if ( ! wfts ) return NULL;
	if ( wfts == -1 ) return (int32_t *)-1;

	setStatus ( "getting site num inlinks");
	// check the tag first
	Tag *tag = gr->getTag ("sitenuminlinks");
	// is it valid?
	bool valid = true;
	// current time
	int32_t now = getTimeGlobal();

	// get tag age in days
	int32_t age = 0; if ( tag ) age = (now - tag->m_timestamp) ;
	// add in some flutter to avoid having all hsots in the network
	// calling msg25 for this site at the same time.
	// a 10,000 second jitter. 3 hours.
	int32_t flutter = rand() % 10000;
	// add it in
	age += flutter;
	// . if site changes ip then toss the contact info out the window,
	//   but give it a two week grace period
	// . well now we use the "ownershipchanged" tag to indicate that
	//if (tag && age>14*3600*24) valid=false;
	// . we also expire it periodically to keep the info uptodate
	// . the higher quality the site, the longer the expiration date
	int32_t ns = 0;
	int32_t maxAge = 0;
	int32_t sni = -1;
	if ( tag ) {
		// how many site inlinks?
		ns = atol(tag->getTagData());
		// for less popular sites use smaller maxAges
		maxAge = 90;
		if      ( ns <  10 ) maxAge = 10;
		else if ( ns <  30 ) maxAge = 15;
		else if ( ns <  50 ) maxAge = 30;
		else if ( ns < 100 ) maxAge = 60;
		// if index size is tiny then maybe we are just starting to
		// build something massive, so reduce the cached max age
		int64_t nt = g_titledb.getRdb()->getCollNumTotalRecs(m_collnum);
		if ( nt < 100000000 ) //100M
			maxAge = 3;
		if ( nt < 10000000 ) //10M
			maxAge = 1;
		// for every 100 urls you already got, add a day!
		sni = atol(tag->getTagData());

		/// @note if we need to force an update in tagdb for sitenuminlinks, add it here

		// convert into seconds
		maxAge *= 3600*24;
		// so youtube which has 2997 links will add an extra 29 days
		maxAge += (sni / 100) * 86400;
		// hack for global index. never affect siteinlinks i imported
		if ( strcmp(cr->m_coll,"GLOBAL-INDEX") == 0 ) age = 0;
		// invalidate for that as wel
		if ( age > maxAge ) valid = false;
	}

	// if we have already been through this
	if ( m_updatingSiteLinkInfoTags ) valid = false;
	// if rebuilding linkdb assume we have no links to sample from!
	if ( tag && m_useSecondaryRdbs && g_repair.m_rebuildLinkdb )
		valid = true;

	// debug log
	if ( g_conf.m_logDebugLinkInfo )
		log("xmldoc: valid=%" PRId32" "
		    "age=%" PRId32" ns=%" PRId32" sni=%" PRId32" "
		    "maxage=%" PRId32" "
		    "tag=%" PTRFMT" "
		    // "tag2=%" PTRFMT" "
		    // "tag3=%" PTRFMT" "
		    "url=%s",
		    (int32_t)valid,age,ns,sni,
		    maxAge,
		    (PTRTYPE)tag,
		    // (PTRTYPE)tag2,
		    // (PTRTYPE)tag3,
		    m_firstUrl.getUrl());

	LinkInfo *sinfo = NULL;
	char *mysite = NULL;

	// if we are good return it
	if ( tag && valid ) {
		// set it
		m_siteNumInlinks = atol(tag->getTagData());
		m_siteNumInlinksValid = true;

		// . consult our sitelinks.txt file
		// . returns -1 if not found
		goto updateToMin;
	}

	// set this flag so when we are re-called, "valid" will be set to false
	// so we can come down here and continue this. "flutter" might
	// otherwise cause us to not make it down here.
	m_updatingSiteLinkInfoTags = true;

	// we need to re-get both if either is NULL
	sinfo = getSiteLinkInfo();
	// block or error?
	if ( ! sinfo || sinfo == (LinkInfo *)-1) return (int32_t *)sinfo;

	//
	// now update tagdb!
	//

	mysite = getSite();
	if ( ! mysite || mysite == (void *)-1 ) return (int32_t *)mysite;

	setStatus ( "adding site info tags to tagdb 1");

	// why are we adding tag again! should already be in tagdb!!!
	if ( m_doingConsistencyCheck ) {g_process.shutdownAbort(true);}

	// do not re-call at this point
	m_siteNumInlinks      = (int32_t)sinfo->m_numGoodInlinks;
	m_siteNumInlinksValid      = true;

 updateToMin:

	// . consult our sitelinks.txt file
	// . returns -1 if not found
	int32_t hostHash32 = getHostHash32a();
	int32_t min = g_tagdb.getMinSiteInlinks ( hostHash32 );

	// try with www if not there
	if ( min < 0 && ! m_firstUrl.hasSubdomain() ) {
		int32_t wwwHash32 = m_firstUrl.getHash32WithWWW();
		min = g_tagdb.getMinSiteInlinks ( wwwHash32 );
	}

	if ( min >= 0 ) {
		if ( m_siteNumInlinks < min ||
		     ! m_siteNumInlinksValid ) {
			m_siteNumInlinks = min;
			m_siteNumInlinksValid = true;
		}
	}

	// deal with it
	return &m_siteNumInlinks;
}

// TODO: can we have a NULL LinkInfo without having had an error?
LinkInfo *XmlDoc::getSiteLinkInfo() {
	// lookup problem?
	if ( g_errno ) {
		log("build: error getting link info: %s",
		    mstrerror(g_errno));
		return NULL;
	}

	setStatus ( "getting site link info" );

	if ( m_siteLinkInfoValid )
	{
		//return msg25.m_linkInfo;
		return (LinkInfo *)m_mySiteLinkInfoBuf.getBufStart();
	}

	char *mysite = getSite();
	if ( ! mysite || mysite == (void *)-1 )
	{
		return (LinkInfo *)mysite;
	}

	int32_t *fip = getFirstIp();
	if ( ! fip || fip == (int32_t *)-1)
	{
		return (LinkInfo *)fip;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		return NULL;
	}

	// can we be cancelled?
	bool canBeCancelled = true;
	// not if pageparser though
	if ( m_pbuf ) canBeCancelled = false;
	// not if injecting
	if ( ! m_sreqValid ) canBeCancelled = false;
	// assume valid when it returns
	m_siteLinkInfoValid = true;
	// use this buffer so XmlDoc::print() can display it where it wants
	SafeBuf *sb = NULL;
	if ( m_pbuf ) sb = &m_siteLinkBuf;
	// only do this for showing them!!!
	if ( m_useSiteLinkBuf ) sb = &m_siteLinkBuf;
	//bool onlyGetGoodInlinks = true;
	//if ( m_useSiteLinkBuf ) onlyGetGoodInlinks = false;
	// get this
	int32_t lastUpdateTime = getTimeGlobal();
	// get from spider request if there
	//bool injected = false;
	//if ( m_sreqValid && m_sreq.m_isInjecting ) injected = true;

	bool onlyNeedGoodInlinks = true;
	// so if steve wants to display all links then set this
	// to false so we get titles of bad inlinks
	// seems like pageparser.cpp just sets m_pbuf and not
	// m_usePageLinkBuf any more
	if ( sb ) onlyNeedGoodInlinks = false;

	// shortcut
	//Msg25 *m = &m_msg25;
	if ( ! getLinkInfo ( &m_tmpBuf11,
			     &m_mcast11,
			     mysite , // site
				mysite , // url
				true , // isSiteLinkInfo?
				*fip                 ,
				0 , // docId
				cr->m_collnum           , //linkInfoColl
				m_masterState       ,
				m_masterLoop        ,
				m_contentInjected ,// isInjecting?
				sb                  ,
			     m_printInXml        ,
				0 , // sitenuminlinks -- dunno!
				NULL , // oldLinkInfo1        ,
				m_niceness          ,
				cr->m_doLinkSpamCheck ,
				cr->m_oneVotePerIpDom ,
				canBeCancelled        ,
				lastUpdateTime ,
				onlyNeedGoodInlinks ,
				false,
				0,
				0,
				// it will store the linkinfo into this safebuf
				&m_mySiteLinkInfoBuf) )
		// return -1 if it blocked
		return (LinkInfo *)-1;

	// getLinkInfo() now calls multicast so it returns true on errors only
	log("build: error making link info: %s",mstrerror(g_errno));
	return NULL;
}

static void gotIpWrapper ( void *state , int32_t ip ) ;

static void delayWrapper ( int fd , void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	THIS->m_masterLoop ( THIS->m_masterState );
}

// . returns NULL and sets g_errno on error
// . returns -1 if blocked, will re-call m_callback
int32_t *XmlDoc::getIp ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// return if we got it
	if ( m_ipValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid [%s]", iptoa(m_ip));
		return &m_ip;
	}

	// update status msg
	setStatus ( "getting ip" );

	m_ipStartTime = 0;
	// assume the same in case we get it right away
	m_ipEndTime = 0;

	// if set from docid and recycling
	if ( m_recycleContent ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, return -1. getOldXmlDoc failed" );
			return (int32_t *)pod;
		}

		// shortcut
		XmlDoc *od = *pod;
		// set it
		if ( od ) {
			m_ip      = od->m_ip;
			m_ipValid = true;

			logTrace( g_conf.m_logTraceXmlDoc, "END, got it from old XmlDoc [%s]", iptoa(m_ip));
			return &m_ip;
		}
	}


	// get the best url
	Url *u = getCurrentUrl();
	if ( ! u || u == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return -1. getCurrentUrl failed." );
		return (int32_t *)u;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. getCollRec failed" );
		return NULL;
	}

	// we need the ip before we download the page, but before we get
	// the IP and download the page, wait for this many milliseconds.
	// this basically slows the spider down.
	int32_t delay = cr->m_spiderDelayInMilliseconds;
	// injected?
	if ( m_sreqValid && m_sreq.m_isInjecting  ) delay = 0;
	if ( m_sreqValid && m_sreq.m_isPageParser ) delay = 0;
	if ( m_sreqValid && m_sreq.m_fakeFirstIp  ) delay = 0;
	// . don't do the delay when downloading extra doc, robots.txt etc.
	// . this also reports a status msg of "getting new doc" when it
	//   really means "delaying spider"
	if ( m_isChildDoc ) delay = 0;

	if ( delay > 0 && ! m_didDelay ) {
		// we did it
		m_didDelay = true;
		m_statusMsg = "delaying spider";
		// random fuzz so we don't get everyone being unleashed at once
		int32_t radius = (int32_t)(.20 * (double)delay);
		int32_t fuzz = (rand() % (radius * 2)) - radius;
		delay += fuzz;
		logTrace( g_conf.m_logTraceXmlDoc, "SLEEPING %" PRId32" msecs", delay);
		// make a callback wrapper.
		// this returns false and sets g_errno on error
		if ( g_loop.registerSleepCallback ( delay         ,
						    m_masterState ,
						    delayWrapper,//m_masterLoop
						    m_niceness    ))
			// wait for it, return -1 since we blocked
                        return (int32_t *)-1;
                // if was not able to register, ignore delay
        }

	if ( m_didDelay && ! m_didDelayUnregister ) {
		g_loop.unregisterSleepCallback(m_masterState,delayWrapper);
		m_didDelayUnregister = true;
	}

	// update status msg
	setStatus ( "getting ip" );

	m_ipStartTime = gettimeofdayInMillisecondsGlobal();

	// assume valid! if reply handler gets g_errno set then m_masterLoop
	// should see that and call the final callback
	//m_ipValid = true;
	// get it
	logTrace( g_conf.m_logTraceXmlDoc, "Calling MsgC.getIp [%s]", u->getHost());
	if (!m_msgc.getIp(u->getHost(), u->getHostLen(), &m_ip, this, gotIpWrapper)) {
		// we blocked
		logTrace( g_conf.m_logTraceXmlDoc, "END, return -1. Blocked." );
    	return (int32_t *)-1;
	}

	// wrap it up
	int32_t *rval2 = gotIp ( true );
	logTrace( g_conf.m_logTraceXmlDoc, "END, return [%s]", rval2 ? iptoa(*rval2) : "NULL");
	return rval2;
}



void gotIpWrapper ( void *state , int32_t ip ) {
	// point to us
	XmlDoc *THIS = (XmlDoc *)state;

	THIS->m_ipEndTime = gettimeofdayInMillisecondsGlobal();

	logTrace( g_conf.m_logTraceXmlDoc, "Got IP [%s]. Took %" PRId64" msec", iptoa(ip), THIS->m_ipEndTime - THIS->m_ipStartTime);

	// wrap it up
	THIS->gotIp ( true );
	// . call the master callback
	// . m_masterState usually equals THIS, unless THIS is the
	//   Xml::m_contactDoc or something...
	THIS->m_masterLoop ( THIS->m_masterState );
}


int32_t *XmlDoc::gotIp ( bool save ) {
	// return NULL on error
	if ( g_errno ) return NULL;
	// this is bad too
	//if ( m_ip == 0 || m_ip == -1 ) m_indexCode = EBADIP;
	//log("db: got ip %s for %s",iptoa(m_ip),getCurrentUrl()->getUrl());

	setStatus ("got ip");

	// we got it
	m_ipValid = true;
	// give it to them
	return &m_ip;
}

// when doing a custom crawl we have to decide between the provided crawl
// delay, and the one in the robots.txt...
int32_t *XmlDoc::getFinalCrawlDelay() {

	if ( m_finalCrawlDelayValid )
		return &m_finalCrawlDelay;

	bool *isAllowed = getIsAllowed();
	if ( ! isAllowed || isAllowed == (void *)-1 ) return (int32_t *)isAllowed;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	m_finalCrawlDelayValid = true;

	// getIsAllowed already sets m_crawlDelayValid to true
	m_finalCrawlDelay = m_crawlDelay;
	// default to 250ms i guess if none specified in robots
	// just to be somewhat nice by default
	if ( m_crawlDelay < 0 )	m_finalCrawlDelay = 250;
	return &m_finalCrawlDelay;
}


bool XmlDoc::isFirstUrlRobotsTxt ( ) {
	if ( m_isRobotsTxtUrlValid )
		return m_isRobotsTxtUrl;
	Url *fu = getFirstUrl();

	m_isRobotsTxtUrl = ( fu->getUrlLen() > 12 && ! strncmp ( fu->getUrl() + fu->getUrlLen() - 11 , "/robots.txt" , 11 ) );
	m_isRobotsTxtUrlValid = true;
	return m_isRobotsTxtUrl;
}


// . get the Robots.txt and see if we are allowed
// . returns NULL and sets g_errno on error
// . returns -1 if blocked, will re-call m_callback
// . getting a robots.txt is not trivial since we need to follow redirects,
//   so we make use of the powerful XmlDoc class for this
bool *XmlDoc::getIsAllowed ( ) {

	logTrace( g_conf.m_logTraceSpider, "BEGIN" );;

	// return if we got it
	if ( m_isAllowedValid )
	{
		logTrace( g_conf.m_logTraceSpider, "END. Valid. Allowed=%s",(m_isAllowed?"true":"false"));
		return &m_isAllowed;
	}

	// could be turned off for everyone
	if ( ! m_useRobotsTxt ) {
		m_isAllowed      = true;
		m_isAllowedValid = true;
		m_crawlDelayValid = true;
		m_crawlDelay      = -1;
		//log("xmldoc: skipping robots.txt lookup for %s",
		//    m_firstUrl.m_url);
		logTrace( g_conf.m_logTraceSpider, "END. !m_useRobotsTxt" );;
		return &m_isAllowed;
	}

	// . if setting from a title rec, assume allowed
	// . this avoids doConsistencyCheck() from blocking and coring
	if ( m_setFromTitleRec ) {
		m_isAllowed      = true;
		m_isAllowedValid = true;
		logTrace( g_conf.m_logTraceSpider, "END. Allowed, m_setFromTitleRec" );;
		return &m_isAllowed;
	}

	if ( m_recycleContent ) {
		m_isAllowed      = true;
		m_isAllowedValid = true;
		logTrace( g_conf.m_logTraceSpider, "END. Allowed, m_recycleContent" );;
		return &m_isAllowed;
	}

	// double get?
	if ( m_crawlDelayValid ) { g_process.shutdownAbort(true); }

	// . if WE are robots.txt that is always allowed!!!
	// . check the *first* url since these often redirect to wierd things
	if ( isFirstUrlRobotsTxt() ) {
		m_isAllowed      = true;
		m_isAllowedValid = true;
		m_crawlDelayValid = true;
		// make it super fast...
		m_crawlDelay      = 0;
		logTrace( g_conf.m_logTraceSpider, "END. Allowed, WE are robots.txt" );;
		return &m_isAllowed;
	}

	// update status msg
	setStatus ( "getting robots.txt" );
	// sanity
	int32_t *ip = getIp();
	// error? or blocked?
	if ( ! ip || ip == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. getIp failed" );;
		return (bool *)ip;
	}

	Url *fu = getFirstUrl();
	// if ip does not exist on the dns, do not try to download robots.txt
	// it is pointless... this can happen in the dir coll and we basically
	// have "m_siteInCatdb" set to true
	logTrace( g_conf.m_logTraceSpider, "IP=%s", iptoa(*ip));

	if ( *ip == 1 || *ip == 0 || *ip == -1 ) {
		// note this
		log("build: robots.txt ip is %s for url=%s. allowing for now.", iptoa(*ip), fu->getUrl());
		// just core for now
		//g_process.shutdownAbort(true);

		//@todo BR: WHY allow when we couldn't get IP??
		m_isAllowed      = true;
		m_isAllowedValid = true;
		// since ENOMIME is no longer causing the indexCode
		// to be set, we are getting a core because crawlDelay
		// is invalid in getNewSpiderReply()
		m_crawlDelayValid = true;
		m_crawlDelay      = -1;
		logTrace( g_conf.m_logTraceSpider, "END. We allow it. FIX?" );;
		return &m_isAllowed;
	}

	// we need this so getExtraDoc does not core
	int32_t *pfip = getFirstIp();
	if ( ! pfip || pfip == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. No first IP, return %s", ((bool *)pfip?"true":"false"));
		return (bool *)pfip;
	}


	// get the current url after redirects
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. No current URL, return %s", ((bool *)cu?"true":"false"));
		return (bool *)cu;
	}


	// set m_extraUrl to the robots.txt url
	char buf[MAX_URL_LEN+1];
	char *p = buf;
	if ( ! cu->getScheme() )
	{
		p += sprintf ( p , "http://" );
	}
	else
	{
		gbmemcpy ( p , cu->getScheme() , cu->getSchemeLen() );
		p += cu->getSchemeLen();
		p += sprintf(p,"://");
	}



	// sanity
	if ( ! cu->getHost() ) { g_process.shutdownAbort(true); }
	gbmemcpy ( p , cu->getHost() , cu->getHostLen() );
	p += cu->getHostLen();

	// add port if not default
	if ( cu->getPort() != cu->getDefaultPort() ) {
		p += sprintf( p, ":%" PRId32, cu->getPort() );
	}

	p += sprintf ( p , "/robots.txt" );
	m_extraUrl.set ( buf );

	logTrace( g_conf.m_logTraceSpider, "m_extraUrl [%s]", buf);


	// . maxCacheAge = 3600 seconds = 1 hour for robots.txt
	// . if this is non-zero then msg13 should store it as well!
	// . for robots.txt it should only cache the portion of the doc
	//   relevant to our user agent!
	// . getHttpReply() should use msg13 to get cached reply!
	XmlDoc **ped = getExtraDoc ( m_extraUrl.getUrl() , 3600 );
	if ( ! ped || ped == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. getExtraDoc (ped) failed, return %s", ((bool *)ped?"true":"false"));
		return (bool *)ped;
	}

	// assign it
	XmlDoc *ed = *ped;
	// return NULL on error with g_errno set
	if ( ! ed ) {
		// sanity check, g_errno must be set
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		// log it -- should be rare?
		log("doc: had error getting robots.txt: %s",
		    mstrerror(g_errno));

		logTrace( g_conf.m_logTraceSpider, "END. Return NULL, ed failed" );;

		return NULL;
	}

	// . now try the content
	// . should call getHttpReply
	char **pcontent = ed->getContent();
	if ( ! pcontent || pcontent == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. pcontent failed, return %s", ((bool *)pcontent?"true":"false"));
		return (bool *)pcontent;
	}

	// get the mime
	HttpMime *mime = ed->getMime();
	if ( ! mime || mime == (HttpMime *)-1 )
	{
		logTrace( g_conf.m_logTraceSpider, "END. mime failed, return %s", ((bool *)mime?"true":"false"));
		return (bool *)mime;
	}

	// get this
	int32_t contentLen = ed->m_contentLen;

	// save this
	m_robotsTxtLen = contentLen;
	m_robotsTxtLenValid = true;

	// get content
	char *content = *pcontent;

	// sanity check
	if ( content && contentLen>0 && content[contentLen] != '\0'){
		g_process.shutdownAbort(true);}

	// reset this. -1 means unknown or none found.
	m_crawlDelay = -1;
	m_crawlDelayValid = true;

	// assume valid and ok to spider
	m_isAllowed      = true;
	m_isAllowedValid = true;


	if ( mime->getHttpStatus() != 200 )
	{
		/// @todo ALC we should allow more error codes
		/// 2xx (successful) : allow
		/// 3xx (redirection) : follow
		/// 4xx (client errors) : allow
		/// 5xx (server errors) : disallow

		// BR 20151215: Do not allow spidering if we cannot read robots.txt EXCEPT if the error code is 404 (Not Found).
		if( mime->getHttpStatus() != 404 )
		{
			m_isAllowed = false;
		}

		logTrace( g_conf.m_logTraceSpider, "END. httpStatus != 200. Return %s", (m_isAllowed?"true":"false"));

		// nuke it to save mem
		nukeDoc ( ed );
		return &m_isAllowed;
	}


	/// @todo ALC cache robots instead of robots.txt
	// initialize robots
	Robots robots( content, contentLen, g_conf.m_spiderBotName );

	m_isAllowed = robots.isAllowed( cu );
	m_crawlDelay = robots.getCrawlDelay();
	m_isAllowedValid = true;

	// nuke it to save mem
	nukeDoc ( ed );

	logTrace( g_conf.m_logTraceSpider, "END. Returning %s", (m_isAllowed?"true":"false") );

	return &m_isAllowed;
}


// . lookup the title rec with the "www." if we do not have that in the url
// . returns NULL and sets g_errno on error
// . returns -1 if blocked, will re-call m_callback
char *XmlDoc::getIsWWWDup ( ) {
	// this is not a real error really
	//if ( g_errno == ENOTFOUND ) g_errno = 0;
	// return if we got it
	if ( m_isWWWDupValid ) return &m_isWWWDup;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// could be turned off for everyone
	if ( ! cr->m_dupCheckWWW ) {
		m_isWWWDup      = false;
		m_isWWWDupValid = true;
		return &m_isWWWDup;
	}
	// get the FIRST URL... (no longer current url after redirects)
	Url *u = getFirstUrl(); // CurrentUrl();
	// if we are NOT a DOMAIN-ONLY url, then no need to do this dup check
	if ( u->getDomainLen() != u->getHostLen() ) {
		m_isWWWDup      = false;
		m_isWWWDupValid = true;
		return &m_isWWWDup;
	}

	// must NOT have a www
	if ( ! u->isHostWWW() ) {
		m_isWWWDup      = false;
		m_isWWWDupValid = true;
		return &m_isWWWDup;
	}

	// watch out for idiot urls like www.gov.uk and www.gov.za
	// treat them as though the TLD is uk/za and the domain
	// is gov.uk and gov.za
	if ( u->getDomain() &&
	     strncmp ( u->getDomain() , "www." , 4 ) == 0 ) {
		m_isWWWDup      = false;
		m_isWWWDupValid = true;
		return &m_isWWWDup;
	}

	// make it without the www
	char withoutWWW[MAX_URL_LEN+1];
	const char *proto = "http";
	if ( u->isHttps() ) proto = "https";
	sprintf(withoutWWW,"%s://%s",proto,u->getDomain());

	// assume yes
	m_isWWWDup = true;

	if ( ! m_calledMsg22f )
		setStatus ( "getting possible www dup title rec" );

	// . does this title rec exist in titledb?
	// . "justCheckTfndb" is set to true here!
	if ( ! m_calledMsg22f &&
	     ! m_msg22f.getTitleRec ( &m_msg22Request      ,
				      withoutWWW           ,
				      0                    , // probable docid
				      cr->m_coll               ,
				      // . msg22 will set this to point to it!
				      // . if NULL that means NOT FOUND
				      NULL                 , // tr ptr
				      NULL                 , // tr size ptr
				      true                 , // just chk tfndb?
				      false, // getavaildocidonly
				      m_masterState        ,
				      m_masterLoop         ,
				      m_niceness           , // niceness
				      999999               )){ // timeout seconds
		// validate
		m_calledMsg22f = true;
		// return -1 if we blocked
		return (char *)-1;
	}
	// got it
	m_calledMsg22f = true;
	// valid now
	m_isWWWDupValid = true;
	// found?
	if(!g_errno && m_msg22f.wasFound()) {
		// crap we are a dup
		m_isWWWDup = true;
		// set the index code
		//m_indexCode = EDOCDUPWWW;
	}
	// return us
	return &m_isWWWDup;
}







static LinkInfo s_dummy2;

// . returns NULL and sets g_errno on error
// . returns -1 if blocked, will re-call m_callback
LinkInfo *XmlDoc::getLinkInfo1 ( ) {

	if ( m_linkInfo1Valid && ptr_linkInfo1 )
		return ptr_linkInfo1;

	// do not generate in real-time from a msg20 request for a summary,
	// because if this falls through then getFirstIp() below can return -1
	// and we return -1, causing all kinds of bad things to happen for
	// handling the msg20 request
	if ( m_setFromTitleRec && m_req && ! ptr_linkInfo1 ) {
		memset ( &s_dummy2 , 0 , sizeof(s_dummy2) );
		s_dummy2.m_lisize = sizeof(s_dummy2);
		ptr_linkInfo1  = &s_dummy2;
		size_linkInfo1 = sizeof(s_dummy2);
		return ptr_linkInfo1;
	}

	// at least get our firstip so if cr->m_getLinkInfo is false
	// then getRevisedSpiderReq() will not core because it is invalid
	int32_t *ip = getFirstIp();
	if ( ! ip || ip == (int32_t *)-1 ) return (LinkInfo *)ip;


	// just return nothing if not doing link voting
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;
	// to keep things fast we avoid getting link info for some collections
	if ( ! m_linkInfo1Valid && ! cr->m_getLinkInfo ) {
		ptr_linkInfo1 = NULL;
		m_linkInfo1Valid = true;
	}

	// sometimes it is NULL in title rec when setting from title rec
	if ( m_linkInfo1Valid && ! ptr_linkInfo1 ) {
		memset ( &s_dummy2 , 0 , sizeof(s_dummy2) );
		s_dummy2.m_lisize = sizeof(s_dummy2);
		ptr_linkInfo1  = &s_dummy2;
		size_linkInfo1 = sizeof(s_dummy2);
		return ptr_linkInfo1;
	}

	// return if we got it
	if ( m_linkInfo1Valid )
		return ptr_linkInfo1;

	// change status
	setStatus ( "getting local inlinkers" );

	XmlDoc **od = getOldXmlDoc ( );
	if ( ! od || od == (XmlDoc **)-1 ) return (LinkInfo *)od;
	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 ) return (LinkInfo *)sni;
	//int32_t *fip = getFirstIp();
	//if ( ! fip || fip == (int32_t *)-1 ) return (LinkInfo *)fip;
	int64_t *d = getDocId();
	if ( ! d || d == (int64_t *)-1 ) return (LinkInfo *)d;
	// sanity check. error?
	if ( *d == 0LL ) {
		log("xmldoc: crap no g_errno");
		g_errno = EBADENGINEER;
		return NULL;
	}
	char *mysite = getSite();
	if ( ! mysite || mysite == (void *)-1 ) return (LinkInfo *)mysite;

	// grab a ptr to the LinkInfo contained in our Doc class
	LinkInfo  *oldLinkInfo1 = NULL;
	if ( *od ) oldLinkInfo1 = (*od)->getLinkInfo1();

	// if ip does not exist, make it 0
	if ( *ip == 0 || *ip == -1 ) {
		m_linkInfo1Valid = true;
		memset ( &s_dummy2 , 0 , sizeof(LinkInfo) );
		s_dummy2.m_lisize = sizeof(LinkInfo);
		ptr_linkInfo1  = &s_dummy2;
		size_linkInfo1 = sizeof(LinkInfo);
		return ptr_linkInfo1;
	}

	//link info generation requires an IP for internal/external computation
	// UNLESS we are from getSpiderStatusDocMetaList2() ... so handle
	// -1 above!
	//if ( *ip == -1 || *ip == 0 ) { g_process.shutdownAbort(true); }

	// . error getting linkers?
	// . on udp timeout we were coring below because msg25.m_linkInfo
	//   was NULL
	if ( g_errno && m_calledMsg25 ) return NULL;
	// prevent core as well
	//if ( m_calledMsg25 && ! size_linkInfo1 ) { // m_msg25.m_linkInfo ) {
	//	log("xmldoc: msg25 had null link info");
	//	g_errno = EBADENGINEER;
	//	return NULL;
	//}

	// . now search for some link info for this url/doc
	// . this queries the search engine to get linking docIds along
	//   with their termIds/scores from anchor text and then compiles
	//   it all into one IndexList
	// . if we have no linkers to this url then we set siteHash, etc.
	//   for this linkInfo class
	// . this is my google algorithm
	// . let's use the first url (before redirects) for this
	// . m_newDocId is used for classifying doc under predefined news topic
	// . catSiteRec is used for classifying pages under a predefined
	//   newstopic. this is currently for news search only.
	// . use the rootTitleRecPtr if there and we are doing our link info
	//   stuff in this collection, but if doing it in another collection
	//   the msg25 will look up the root in that collection...
	if ( ! m_calledMsg25 ) {
		// get this
		int32_t lastUpdateTime = getTimeGlobal();

		// do not redo it
		m_calledMsg25 = true;
		// shortcut
		//Msg25 *m = &m_msg25;
		// can we be cancelled?
		bool canBeCancelled = true;
		// not if pageparser though
		if ( m_pbuf ) canBeCancelled = false;
		// not if injecting
		if ( ! m_sreqValid ) canBeCancelled = false;
		// use this buffer so XmlDoc::print() can display wherever
		SafeBuf *sb = NULL;
		if ( m_pbuf ) sb = &m_pageLinkBuf;
		// only do this for showing them!!!
		if ( m_usePageLinkBuf ) sb = &m_pageLinkBuf;
		// get from spider request if there
		//bool injected = false;
		//if ( m_sreqValid && m_sreq.m_isInjecting ) injected = true;
		// we do not want to waste time computing the page title
		// of bad inlinks if we only want the good inlinks, because
		// as of oct 25, 2012 we only store the "good" inlinks
		// in the titlerec
		bool onlyNeedGoodInlinks = true;
		// so if steve wants to display all links then set this
		// to false so we get titles of bad inlinks
		if ( m_usePageLinkBuf ) onlyNeedGoodInlinks = false;
		// seems like pageparser.cpp just sets m_pbuf and not
		// m_usePageLinkBuf any more
		if ( m_pbuf           ) onlyNeedGoodInlinks = false;
		// status update
		setStatus ( "calling msg25 for url" );
		CollectionRec *cr = getCollRec();
		if ( ! cr ) return NULL;

		// we want to get all inlinks if doing a custom crawlbot crawl
		// because we need the anchor text to pass in to diffbot
		bool doLinkSpamCheck = cr->m_doLinkSpamCheck;
		bool oneVotePerIpDom = cr->m_oneVotePerIpDom;

		// call it. this is defined in Linkdb.cpp
		char *url = getFirstUrl()->getUrl();
		if ( ! getLinkInfo ( &m_tmpBuf12,
				     &m_mcast12,
				        mysite ,
					url ,
					false , // isSiteLinkInfo?
					*ip                 ,
					*d                  ,
					cr->m_collnum       , //linkInfoColl
					m_masterState       ,
					m_masterLoop        ,
					m_contentInjected ,//m_injectedReply ,
					sb                  ,
					m_printInXml        ,
					*sni                ,
					oldLinkInfo1        ,
					m_niceness          ,
					doLinkSpamCheck ,
					oneVotePerIpDom ,
					canBeCancelled        ,
					lastUpdateTime ,
					onlyNeedGoodInlinks ,
					false, // getlinkertitles
					0, // ourhosthash32 (special)
					0,  // ourdomhash32 (special)
					&m_myPageLinkInfoBuf
					) )
			// blocked
			return (LinkInfo *)-1;
		// error?
		if ( g_errno ) return NULL;
		// panic! what the fuck? why did it return true and then
		// call our callback???
		//if ( g_conf.m_logDebugBuild ) {
		log("build: xmldoc call to msg25 did not block");
		// must now block since it uses multicast now to
		// send the request onto the network
		g_process.shutdownAbort(true);
		//}
	}

	// at this point assume its valid
	m_linkInfo1Valid = true;
	// . get the link info we got set
	// . this ptr references into m_myPageLinkInfoBuf safebuf
	//ptr_linkInfo1  = m_msg25.m_linkInfo;
	//size_linkInfo1 = m_msg25.m_linkInfo->getSize();
	ptr_linkInfo1  = (LinkInfo *)m_myPageLinkInfoBuf.getBufStart();
	size_linkInfo1 = m_myPageLinkInfoBuf.length();
	// we should free it
	m_freeLinkInfo1 = true;
	// this can not be NULL!
	if ( ! ptr_linkInfo1 || size_linkInfo1 <= 0 ) {
		log("build: error getting linkinfo1: %s",mstrerror(g_errno));
		g_process.shutdownAbort(true);
		return NULL;
	}
	// take it from msg25 permanently
	//m_msg25.m_linkInfo = NULL;
	// set flag
	m_linkInfo1Valid = true;
	// . validate the hop count thing too
	// . i took hopcount out of linkdb to put in lower ip byte for steve
	//m_minInlinkerHopCount = -1;//m_msg25.getMinInlinkerHopCount();
	// return it
	return ptr_linkInfo1;
}

static void gotSiteWrapper ( void *state ) ;

// . we should store the site in the title rec because site getter might
//   change what it thinks the site is!
char *XmlDoc::getSite ( ) {
	// was there a problem getting site?
	if ( m_siteValid && m_siteGetter.m_errno ) {
		g_errno = m_siteGetter.m_errno;
		return NULL;
	}
	// ok, return it
	if ( m_siteValid ) {
		return ptr_site;
	}

	// note it
	setStatus ( "getting site");
	// need this
	TagRec *gr = getTagRec();
	// sanity check
	if ( ! gr && ! g_errno ) { g_process.shutdownAbort(true); }
	// blocked or error?
	if ( ! gr || gr == (TagRec *)-1 ) return (char *)gr;
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;
	// get url
	Url *f = getFirstUrl();
	// bogus first url? prevent core in getIsSiteRoot().
	if ( f->getUrlLen() <= 1 ) {
		log("xmldoc: getSite: got bogus first url.");
		g_errno = EBADURL;
		return NULL;
	}

	int32_t timestamp = getSpideredTime();

	// do it
	if ( ! m_siteGetter.getSite ( f->getUrl(), gr, timestamp, cr->m_collnum, m_niceness, this, gotSiteWrapper )) {
		// return -1 if we blocked
		return (char *) -1;
	}

	// error?
	if ( g_errno ) {
		return NULL;
	}

	// set these then
	gotSite();
	return ptr_site;
}

// set it
void gotSiteWrapper ( void *state ) {
	// point to us
	XmlDoc *THIS = (XmlDoc *)state;
	THIS->gotSite ();
	// resume. this checks g_errno for being set.
	THIS->m_masterLoop ( THIS->m_masterState );
}

void XmlDoc::gotSite ( ) {
	// sanity check
	if ( ! m_siteGetter.m_allDone && ! g_errno ) { g_process.shutdownAbort(true); }

	// this sets g_errno on error
	ptr_site    = m_siteGetter.m_site;
	size_site   = m_siteGetter.m_siteLen+1; // include \0

	// sanity check -- must have a site
	if ( ! g_errno && size_site <= 1 ) { g_process.shutdownAbort(true); }

	// BR 20151215: Part of fix that avoids defaultint to http:// when getting
	// robots.txt and root document of a https:// site.
	ptr_scheme    = m_siteGetter.m_scheme;
	size_scheme   = m_siteGetter.m_schemeLen+1; // include \0

	// sitegetter.m_errno might be set!
	m_siteValid = true;

	// must be valid
	if ( ! m_tagRecValid ) { g_process.shutdownAbort(true); }
}


int32_t *XmlDoc::getSiteHash32 ( ) {
	if ( m_siteHash32Valid ) return &m_siteHash32;
	char *site = getSite();
	if ( ! site || site == (void *)-1) return (int32_t *)site;
	m_siteHash32 = hash32 ( site , strlen(site) );
	m_siteHash32Valid = true;
	return &m_siteHash32;
}


const char *XmlDoc::getScheme ( ) {
	// was there a problem getting site?
	if ( m_siteValid && m_siteGetter.m_errno ) {
		g_errno = m_siteGetter.m_errno;
		return NULL;
	}
	// ok, return it
	if ( m_siteValid ) return ptr_scheme;//m_siteGetter.m_scheme;

	return "";
}



char **XmlDoc::getHttpReply ( ) {
	// both must be valid now
	if ( m_redirUrlValid && m_httpReplyValid ) {
		// might have been a download error of ECORRUPTDATA
		if ( m_downloadStatus == ECORRUPTDATA ) {
			// set g_errno so caller knows
			g_errno = m_downloadStatus;
			// null means error
			return NULL;
		}
		// otherwise, assume reply is valid
		return &m_httpReply;
	}

	setStatus("getting http reply");

	// come back up here if a redirect invalidates it
	for ( ; ; ) {
		// get the http reply
		char **replyPtr = getHttpReply2();
		if ( ! replyPtr || replyPtr == (void *)-1 ) return (char **)replyPtr;

		// . now if the reply was a redirect we should set m_redirUrl to it
		//   and re-do all this code
		// . this often sets m_indexCode to stuff like ESIMPLIFIEDREDIR, etc.
		Url **redirp = getRedirUrl();

		// we often lookup the assocaited linkInfo on the original url to
		// see if it is worth keeping and indexing just to take advantage of
		// the incoming link text it has, so we may block on that!
		// but in the case of a contactDoc, getContactDoc() sets these things
		// to NULL to avoid unnecessary lookups.
		if ( ! redirp || redirp == (void *)-1 ) return (char **)redirp;

		// sanity check
		if ( *redirp && ! m_redirUrlValid ) { g_process.shutdownAbort(true); }

		// if NULL, we are done
		if ( ! *redirp ) return &m_httpReply;

		// . also, hang it up if we got a simplified redir url now
		// . we set m_redirUrl so that getLinks() can add a spiderRequest
		//   for it, but we do not want to actually redirect to it to get
		//   the content for THIS document
		if ( m_redirError ) return &m_httpReply;

		// and invalidate the redir url because we do not know if the
		// current url will redirect or not (mdwmdw)
		m_redirUrlValid           = false;
		m_metaRedirUrlValid       = false;

		// free it
		mfree ( m_httpReply , m_httpReplyAllocSize, "freehr" );

		// always nullify if we free so we do not re-use freed mem
		m_httpReply = NULL;

		// otherwise, we had a redirect, so invalidate what we had set
		m_httpReplyValid          = false;

		m_isContentTruncatedValid = false;

		// do not redo robots.txt lookup if the redir url just changed from
		// http to https or vice versa
		Url *ru = *redirp;
		Url *cu = getCurrentUrl();
		if ( ! cu || cu == (void *)-1) return (char **)cu;
		if ( strcmp ( ru->getUrl() + ru->getSchemeLen(), cu->getUrl() + cu->getSchemeLen() ) ) {
			// redo robots.txt lookup. might be cached.
			m_isAllowedValid  = false;
			m_crawlDelayValid = false;
		}

		// keep the same ip if hostname is unchanged
		if ( ru->getHostLen() != cu->getHostLen() || strncmp ( ru->getHost() , cu->getHost(), cu->getHostLen() ) ) {
			// ip is supposed to be that of the current url, which changed
			m_ipValid = false;
		}

		// we set our m_xml to the http reply to check for meta redirects
		// in the html sometimes in getRedirUrl() so since we are redirecting,
		// invalidate that xml
		m_xmlValid                = false;
		m_wordsValid              = false;
		m_rawUtf8ContentValid     = false;
		m_expandedUtf8ContentValid= false;
		m_utf8ContentValid        = false;
		m_filteredContentValid    = false;
		m_contentValid            = false;
		m_mimeValid               = false;

		// update our current url now to be the redirected url
		m_currentUrl.set ( *redirp );
		m_currentUrlValid = true;
	}
}

static void gotHttpReplyWrapper ( void *state ) {
	// point to us
	XmlDoc *THIS = (XmlDoc *)state;
	// this sets g_errno on error
	THIS->gotHttpReply ( );
	// resume. this checks g_errno for being set.
	THIS->m_masterLoop ( THIS->m_masterState );
}

// "NULL" can be a valid http reply (empty page) so we need to use "char **"
char **XmlDoc::getHttpReply2 ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	if ( m_httpReplyValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already has valid reply" );;
		return &m_httpReply;
	}

	setStatus("getting http reply2");


	// if recycle is set then NEVER download if doing query reindex
	// but if doing an injection then i guess we can download.
	// do not even do ip lookup if no old titlerec, which is how we
	// ended up here...
	if ( m_recycleContent && m_sreqValid && m_sreq.m_isPageReindex ) {
		g_errno = ENOTITLEREC;
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. ENOTITLEREC (1)" );;
		return NULL;
	}

	// get ip
	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. no IP" );;
		return (char **)ip;
	}

	// reset
	m_httpReplySize = 0;
	m_httpReply     = NULL;

	// if ip is bogus, we are done
	if ( *ip == 0 || *ip == -1 ) {
		log("xmldoc: ip is bogus 0 or -1 for %s. skipping download",
		    m_firstUrl.getUrl());
		m_httpReplyValid          = true;
		m_isContentTruncated      = false;
		m_isContentTruncatedValid = true;
		// need this now too. but don't hurt a nonzero val if we have
		if ( ! m_downloadEndTimeValid ) {
			m_downloadEndTime      = 0;
			m_downloadEndTimeValid = true;
		}

		logTrace( g_conf.m_logTraceXmlDoc, "END, return empty reply, IP is bogus" );;
		return &m_httpReply;
		//return gotHttpReply ( );
	}

	// get this. should operate on current url (i.e. redir url if there)
	bool *isAllowed = getIsAllowed();

	// error or blocked
	if ( ! isAllowed || isAllowed == (void *)-1)
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return, not allowed." );;
		return (char **)isAllowed;
	}

	// this must be valid, since we share m_msg13 with it
	if ( ! m_isAllowedValid ) { g_process.shutdownAbort(true); }

	int32_t *cd = getFinalCrawlDelay();
	if ( ! cd || cd == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. could not get crawl delay" );;
		return (char **)cd;
	}

	// we might bail
	if ( ! *isAllowed ) {
		m_httpReplyValid          = true;
		m_isContentTruncated      = false;
		m_isContentTruncatedValid = true;
		// need this now too. but don't hurt a nonzero val if we have
		if ( ! m_downloadEndTimeValid ) {
			m_downloadEndTime      = 0;
			m_downloadEndTimeValid = true;
		}
		m_downloadStatusValid = true;
		// forbidden? assume we downloaded it and it was empty
		m_downloadStatus = 0; // EDOCDISALLOWED;//403;

		logTrace( g_conf.m_logTraceXmlDoc, "END, return empty reply, download not allowed" );;
		return &m_httpReply;
		//return gotHttpReply ( );
	}

	// are we site root page?
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return, error calling getIsSiteRoot" );;
		return (char **)isRoot;
	}

	XmlDoc *od = NULL;
	if ( ! m_isSpiderProxy &&
		 // don't lookup xyz.com/robots.txt in titledb
		 ! isFirstUrlRobotsTxt() ) {
		    XmlDoc **pod = getOldXmlDoc ( );
		    if ( ! pod || pod == (XmlDoc **)-1 ) {
		    	logTrace( g_conf.m_logTraceXmlDoc, "END, return, error calling getOldXmlDoc" );;
		    	return (char **)pod;
		    }
		    // get ptr to old xml doc, could be NULL if non exists
		    od = *pod;
	}



	// sanity check
	if ( od && m_recycleContent ) {g_process.shutdownAbort(true); }

	// validate m_firstIpValid
	int32_t *pfip = getFirstIp();
	if ( ! pfip || pfip == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return, error calling getFirstIp" );;
		return (char **)pfip;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. getCollRec returned false" );;
		return NULL;
	}

	// if we didn't block getting the lock, keep going
	setStatus ( "getting web page" );

	// sanity check
	if ( ! m_masterLoop ) { g_process.shutdownAbort(true); }

	// shortcut. this will return the redirUrl if it is non-empty.
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return, getCurrentUrl returned false" );;
		return (char **)cu;
	}

	// set parms
	Msg13Request *r = &m_msg13Request;
	// clear it first
	r->reset();
	// and set the url
	r->ptr_url  = cu->getUrl();
	r->size_url = cu->getUrlLen()+1;

	// sanity check
	if ( ! m_firstIpValid ) { g_process.shutdownAbort(true); }

	// max to download in bytes.
	r->m_maxTextDocLen          = cr->m_maxTextDocLen;
	r->m_maxOtherDocLen         = cr->m_maxOtherDocLen;

	// but if url is on the intranet/internal nets
	if ( m_ipValid && is_internal_net_ip(m_ip) ) {
		// . if local then make web page download max size unlimited
		// . this is for adding the gbdmoz.urls.txt.* files to
		//   populate dmoz. those files are about 25MB each.
		r->m_maxTextDocLen  = -1;
		r->m_maxOtherDocLen = -1;
	}
	// m_maxCacheAge is set for getting contact or root docs in
	// getContactDoc() and getRootDoc() and it only applies to
	// titleRecs in titledb i guess... but still... for Msg13 it applies
	// to its cache ... for robots.txt files too
	r->m_maxCacheAge            = m_maxCacheAge;
	r->m_urlIp                  = *ip;
	r->m_firstIp                = m_firstIp;
	r->m_urlHash48              = getFirstUrlHash48();
 	if ( r->m_maxTextDocLen  < 100000 ) r->m_maxTextDocLen  = 100000;
	if ( r->m_maxOtherDocLen < 200000 ) r->m_maxOtherDocLen = 200000;
	r->m_spideredTime           = getSpideredTime();//m_spideredTime;
	r->m_ifModifiedSince        = 0;
	r->m_skipHammerCheck        = 0;

	if ( m_redirCookieBufValid && m_redirCookieBuf.length() ) {
		r->ptr_cookie  = m_redirCookieBuf.getBufStart();
		r->size_cookie = m_redirCookieBuf.length() + 1;
		// . only do once per redirect
		// . do not invalidate because we might have to carry it
		//   through to the next redir... unless we change domain
		// . this fixes the nyt.com/nytimes.com bug some more
		//m_redirCookieBufValid = false;
	}

	// . this is -1 if unknown. none found in robots.txt or provided
	//   in the custom crawl parms.
	// . it should also be 0 for the robots.txt file itself
	r->m_crawlDelayMS = *cd;

	// let's time our crawl delay from the initiation of the download
	// not from the end of the download. this will make things a little
	// faster but could slam servers more.
	r->m_crawlDelayFromEnd = false;

	// new stuff
	r->m_contentHash32 = 0;
	// if valid in SpiderRequest, use it. if spider compression proxy
	// sees the content is unchanged it will not send it back! it will
	// send back g_errno = EDOCUNCHANGED or something
	if ( m_sreqValid )
		r->m_contentHash32 = m_sreq.m_contentHash32;

	// if we have the old doc already set use that
	if ( od )
		r->m_contentHash32 = od->m_contentHash32;

	// for beta testing, make it a collection specific parm for diffbot
	// so we can turn on manually
	if ( cr->m_forceUseFloaters )
		r->m_forceUseFloaters = true;


	// turn this off too
	r->m_attemptedIframeExpansion = false;

	r->m_collnum = (collnum_t)-1;
	if ( m_collnumValid )r->m_collnum = m_collnum;

	// turn off
	r->m_useCompressionProxy = false;
	r->m_compressReply       = false;

	// set it for this too
	if ( g_conf.m_useCompressionProxy ) {
		r->m_useCompressionProxy = true;
		r->m_compressReply       = true;
	}

	logTrace( g_conf.m_logTraceXmlDoc, "cu->m_url [%s]", cu->getUrl());
	logTrace( g_conf.m_logTraceXmlDoc, "m_firstUrl.m_url [%s]", m_firstUrl.getUrl());

	// if current url IS NOT EQUAL to first url then set redir flag
	if ( strcmp(cu->getUrl(),m_firstUrl.getUrl()) )
		r->m_skipHammerCheck = 1;
	// or if this an m_extraDoc or m_rootDoc for another url then
	// do not bother printing the hammer ip msg in msg13.cpp either
	if ( m_isChildDoc )
		r->m_skipHammerCheck = 1;

	if ( m_contentInjected ) // oldsrValid && m_sreq.m_isInjecting )
		r->m_skipHammerCheck = 1;

	if ( r->m_skipHammerCheck )
		log(LOG_DEBUG,"build: skipping hammer check");

	// if we had already spidered it... try to save bandwidth and time
	if ( od ) {
		// sanity check
		if ( ! od->m_spideredTimeValid ) { g_process.shutdownAbort(true); }
		// only get it if modified since last spider time
		r->m_ifModifiedSince = od->m_spideredTime;
	}

	// if doing frame expansion on a doc we just downloaded as the
	// spider proxy, we are asking ourselves now to download the url
	// from an <iframe src=...> tag. so definitely use msg13 again
	// so it can use the robots.txt cache, and regular html page cache.
	if ( m_isSpiderProxy ) {
		r->m_useCompressionProxy = false;
		r->m_compressReply       = false;
		r->m_skipHammerCheck = 1;
		// no frames within frames
		r->m_attemptedIframeExpansion = 1;
		log(LOG_DEBUG,"build: skipping hammer check 2");

	}

	// . use msg13 to download the file, robots.txt
	// . msg13 will ensure only one download of that url w/ locks
	// . msg13 can use the compress the http reply before
	//   sending it back to you via udp (compression proxy)
	// . msg13 uses XmlDoc::getHttpReply() function to handle
	//   redirects, etc.? no...

	// sanity check. keep injections fast. no downloading!
	if ( m_wasContentInjected ) {
		log("xmldoc: url injection failed! error!");
		g_process.shutdownAbort(true);
	}

	// sanity check
	if ( m_deleteFromIndex ) {
		log("xmldoc: trying to download page to delete");
		g_process.shutdownAbort(true);
	}

	m_downloadStartTimeValid = true;
	m_downloadStartTime = gettimeofdayInMillisecondsGlobal();

	logTrace( g_conf.m_logTraceXmlDoc, "Calling msg13.getDoc" );;

	if ( ! m_msg13.getDoc ( r,this , gotHttpReplyWrapper ) )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return -1. msg13.getDoc blocked" );;
		// return -1 if blocked
		return (char **)-1;
	}

	logTrace( g_conf.m_logTraceXmlDoc, "END, calling gotHttpReply and returning result" );;
	return gotHttpReply ( );
}

// . this returns false if blocked, true otherwise
// . sets g_errno on error
char **XmlDoc::gotHttpReply ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	// save it
	int32_t saved = g_errno;
	// note it
	setStatus ( "got web page" );

	// sanity check. are we already valid?
	if ( m_httpReply && m_httpReplyValid ) { g_process.shutdownAbort(true); }

	// do not re-call
	m_httpReplyValid = true;

	// assume none
	m_httpReply = NULL;

	// . get the HTTP reply
	// . TODO: free it on reset/destruction, we own it now
	// . this is now NULL terminated thanks to changes in
	//   Msg13.cpp, but watch the buf size, need to subtract 1
	// . therefore, we can set the Xml class with it
	m_httpReply     = m_msg13.m_replyBuf;
	m_httpReplySize = m_msg13.m_replyBufSize;
	// how much to free?
	m_httpReplyAllocSize = m_msg13.m_replyBufAllocSize;

	// sanity check
	if ( m_httpReplySize > 0 && ! m_httpReply ) { g_process.shutdownAbort(true); }

	// . don't let UdpServer free m_buf when socket is
	//   recycled/closed
	// . we own it now and are responsible for freeing it
	m_msg13.m_replyBuf = NULL;

	// relabel mem so we know where it came from
	relabel( m_httpReply, m_httpReplyAllocSize, "XmlDocHR" );

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. Could not get collection" );;
		return NULL;
	}

	// . sanity test
	// . i.e. what are you doing downloading the page if there was
	//   a problem with the page we already know about
	if ( m_indexCode && m_indexCodeValid ) {
		g_process.shutdownAbort(true);
	}

	// fix this
	if ( saved == EDOCUNCHANGED ) {
		logTrace( g_conf.m_logTraceXmlDoc, "EDOCUNCHANGED" );;
		// assign content from it since unchanged
		m_recycleContent = true;
		// clear the error
		saved = 0;
		g_errno = 0;
	}

	// . save the error in download status
	// . could now be EDOCUNCHANGED or EDOCNOGOODDATE (w/ tod)
	m_downloadStatus = saved; // g_errno;
	// validate
	m_downloadStatusValid = true;

	// update m_downloadEndTime if we should, used for sameIpWait
	m_downloadEndTime      = gettimeofdayInMillisecondsGlobal();
	m_downloadEndTimeValid = true;

	// make it so
	g_errno = saved;

	bool doIncrement = true;
	if ( m_isChildDoc ) doIncrement = false;
	if ( m_incrementedDownloadCount ) doIncrement = false;


	// . do not count bad http status in mime as failure i guess
	// . do not inc this count for robots.txt and root page downloads, etc.
	if ( doIncrement ) {
		cr->m_localCrawlInfo.m_pageDownloadSuccesses++;
		cr->m_globalCrawlInfo.m_pageDownloadSuccesses++;
		cr->m_localCrawlInfo.m_pageDownloadSuccessesThisRound++;
		cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound++;
		m_incrementedDownloadCount = true;
		cr->m_needsSave = true;
		// changing status, resend local crawl info to all
		cr->localCrawlInfoUpdate();
	}

	// this means the spider compression proxy's reply got corrupted
	// over roadrunner's crappy wireless internet connection
	if ( saved == ECORRUPTDATA )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. ECORRUPTDATA" );;
		return NULL;
	}

	// this one happens too! for the same reason...
	if ( saved == EBADREPLYSIZE )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. EBADREPLYSIZE" );;
		return NULL;
	}

	// might as well check this too while we're at it
	if ( saved == ENOMEM )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, return NULL. ENOMEM" );;
		return NULL;
	}

	// sanity check -- check after bailing on corruption because
	// corrupted replies do not end in NULLs
	if ( m_httpReplySize > 0 && m_httpReply[m_httpReplySize-1] ) {
		log("http: httpReplySize=%" PRId32" http reply does not end in \\0 "
		    "for %s in collnum=%" PRId32". blanking out reply."
		    ,m_httpReplySize
		    ,m_firstUrl.getUrl()
		    ,(int32_t)m_collnum
		    );
		// free it i guess
		mfree ( m_httpReply, m_httpReplyAllocSize, "XmlDocHR" );
		// and reset it
		m_httpReplySize      = 0;
		m_httpReply          = NULL;
		m_httpReplyAllocSize = 0;
		// call it data corruption i guess for now
		g_errno = ECORRUPTDATA;
		//g_process.shutdownAbort(true);
		logTrace( g_conf.m_logTraceXmlDoc, "Clearing data, detected corruption" );;
	}

	// if its a bad gzip reply, a compressed http reply, then
	// make the whole thing empty? some websites return compressed replies
	// even though we do not ask for them. and then the compression
	// is corrupt.
	if ( saved == ECORRUPTHTTPGZIP ||
	     // if somehow we got a page too big for MAX_DGRAMS... treat
	     // it like an empty page...
	     saved == EMSGTOOBIG ) {

	     logTrace( g_conf.m_logTraceXmlDoc, "Clearing data, ECORRUPTHTTPGZIP or EMSGTOOBIG" );;
		// free it i guess
		mfree ( m_httpReply, m_httpReplyAllocSize, "XmlDocHR" );
		// and reset it
		m_httpReplySize      = 0;
		m_httpReply          = NULL;
		m_httpReplyAllocSize = 0;
	}

	// clear this i guess
	g_errno = 0;

	logTrace( g_conf.m_logTraceXmlDoc, "END, returning reply." );;
	return &m_httpReply;
}



char *XmlDoc::getIsContentTruncated ( ) {
	if ( m_isContentTruncatedValid ) return &m_isContentTruncated2;

	setStatus ( "getting is content truncated" );

	// if recycling content use its download end time
	if ( m_recycleContent ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 ) return (char *)pod;
		// shortcut
		XmlDoc *od = *pod;
		// this is non-NULL if it existed
		if ( od ) {
			m_isContentTruncated  = od->m_isContentTruncated;
			m_isContentTruncated2 = (bool)m_isContentTruncated;
			m_isContentTruncatedValid = true;
			return &m_isContentTruncated2;
		}
	}

	// need a valid reply
	char **replyPtr = getHttpReply ();
	if ( ! replyPtr || replyPtr == (void *)-1 ) return (char *)replyPtr;

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) return (char *)ct;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// shortcut - convert size to length
	int32_t LEN = m_httpReplySize - 1;

	m_isContentTruncated  = false;
	// was the content truncated? these might label a doc is truncated
	// when it really is not... but we only use this for link spam stuff,
	// so it should not matter too much. it should only happen rarely.
	if ( cr->m_maxTextDocLen >= 0 &&
		LEN >= cr->m_maxTextDocLen-1  &&
		*ct == CT_HTML )
			m_isContentTruncated = true;

	if ( cr->m_maxOtherDocLen >= 0 &&
		LEN >= cr->m_maxOtherDocLen-1 &&
		*ct != CT_HTML )
			m_isContentTruncated = true;

	//if ( LEN > MAXDOCLEN ) m_isContentTruncated = true;
	// set this
	m_isContentTruncated2 = (bool)m_isContentTruncated;
	// validate it
	m_isContentTruncatedValid = true;

	return &m_isContentTruncated2;
}



int32_t *XmlDoc::getDownloadStatus ( ) {
	if ( m_downloadStatusValid ) return &m_downloadStatus;
	// log it
	setStatus ( "getting download status");
	// if recycling content, we're 200!
	if ( m_recycleContent ) {
		m_downloadStatus = 0;
		m_downloadStatusValid = true;
		return &m_downloadStatus;
	}
	// get ip
	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1 ) return (int32_t *)ip;
	// . first try ip
	// . this means the dns lookup timed out
	if ( *ip == -1 ) {
		m_downloadStatus = EDNSTIMEDOUT;
		m_downloadStatusValid = true;
		return &m_downloadStatus;
	}
	// this means ip does not exist
	if ( *ip == 0 ) {
		m_downloadStatus = EBADIP;
		m_downloadStatusValid = true;
		return &m_downloadStatus;
	}
	// need a valid reply
	char **reply = getHttpReply ();
	if ( ! reply || reply == (void *)-1 ) return (int32_t *)reply;
	// must be valid now
	if ( ! m_downloadStatusValid ) { g_process.shutdownAbort(true); }
	// return it
	return &m_downloadStatus;
}



int64_t *XmlDoc::getDownloadEndTime ( ) {
	if ( m_downloadEndTimeValid ) return &m_downloadEndTime;
	// log it
	setStatus ( "getting download end time");

	// do not cause us to core in getHttpReply2() because m_deleteFromIndex
	// is set to true...
	if ( m_deleteFromIndex ) {
		m_downloadEndTime = 0;
		m_downloadEndTimeValid = true;
		return &m_downloadEndTime;
	}

	// if recycling content use its download end time
	if ( m_recycleContent ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 ) return (int64_t *)pod;
		// shortcut
		XmlDoc *od = *pod;
		// this is non-NULL if it existed
		if ( od ) {
			m_downloadEndTime      = od->m_downloadEndTime;
			m_downloadEndTimeValid = true;
			return &m_downloadEndTime;
		}
	}

	// need a valid reply
	char **reply = getHttpReply ();
	if ( ! reply || reply == (void *)-1 ) return (int64_t *)reply;
	// must be valid now
	if ( ! m_downloadEndTimeValid ) { g_process.shutdownAbort(true);}
	// return it
	return &m_downloadEndTime;
}



int16_t *XmlDoc::getHttpStatus ( ) {
	// if we got a title rec then return that
	if ( m_httpStatusValid ) return &m_httpStatus;
	// get mime otherwise
	HttpMime *mime = getMime();
	if ( ! mime || mime == (HttpMime *)-1 ) return (int16_t *)mime;
	// get from that
	m_httpStatus = mime->getHttpStatus();
	m_httpStatusValid = true;
	return &m_httpStatus;
}



HttpMime *XmlDoc::getMime () {
	if ( m_mimeValid ) return &m_mime;

	// log debug
	setStatus("getting http mime");

	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1) return (HttpMime *)cu;

	// injection from SpiderLoop.cpp sets this to true
	if ( m_useFakeMime ) {
	usefake:
		m_mime.set            ( NULL , 0 , cu );
		m_mime.setHttpStatus  ( 200 );
		m_mime.setContentType ( CT_HTML );
		m_mimeValid = true;
		return &m_mime;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// if recycling content, fake this mime
	if ( cr->m_recycleContent || m_recycleContent ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 ) return (HttpMime *)pod;
		// shortcut
		XmlDoc *od = *pod;
		// . this is non-NULL if it existed
		// . fake it for now
		if ( od ) goto usefake;
	}

	// need a valid reply
	char **reply = getHttpReply ();
	if ( ! reply || reply == (void *)-1 ) return (HttpMime *)reply;

	// fake it for now
	m_mime.set ( NULL , 0 , cu );
	m_mime.setHttpStatus ( 200 );
	m_mime.setContentType ( CT_HTML );

	// shortcut
	int32_t LEN = m_httpReplySize - 1;

	// validate it
	m_mimeValid = true;

	// TODO: try again on failures because server may have been overloaded
	// and closed the connection w/o sending anything
	if ( LEN>0 && ! m_mime.set ( m_httpReply , LEN , cu ) ) {
		// set this on mime error
		//m_indexCode = EBADMIME;
		// return a fake thing. content length is 0.
		return &m_mime;
	}

	return &m_mime;
}



// need to use "char **" since content might be NULL itself, if none
char **XmlDoc::getContent ( ) {
	if ( m_contentValid ) return &m_content;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// recycle?
	if ( cr->m_recycleContent || m_recycleContent ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 ) return (char **)pod;
		// shortcut
		XmlDoc *od = *pod;
		// this is non-NULL if it existed
		if ( od ) {
			m_content      = od-> ptr_utf8Content;
			m_contentLen   = od->size_utf8Content - 1;
			m_contentValid = true;
			return &m_content;
		}
		if ( m_recycleContent )
			log("xmldoc: failed to load old title rec "
			    "when recycle content was true and url = "
			    "%s",ptr_firstUrl);
		// if could not find title rec and we are docid-based then
		// we can't go any further!!
		if ( m_setFromDocId ) {
			log("xmldoc: null content for docid-based titlerec "
			    "lookup which was not found");
			m_content = NULL;
			m_contentLen = 0;
			m_contentValid = true;
			return &m_content;
		}
	}

	if ( m_recycleContent ) {
		if ( m_firstUrlValid )
			log("xmldoc: failed to recycle content for %s. could "
			    "not load title rec",m_firstUrl.getUrl());
		else if ( m_docIdValid )
			log("xmldoc: failed to recycle content for %" PRIu64". "
			    "could "
			    "not load title rec",m_docId );
		else
			log("xmldoc: failed to recycle content. "
			    "could not load title rec" );
		// let's let it pass and just download i guess, then
		// we can get page stats for urls not in the index
		//g_errno = EBADENGINEER;
		//return NULL;
	}


	// if we were set from a title rec use that we do not have the original
	// content, and caller should be calling getUtf8Content() anyway!!
	if ( m_setFromTitleRec ) { g_process.shutdownAbort(true); }

	// get the mime first
	HttpMime *mime = getMime();
	if ( ! mime || mime == (HttpMime *)-1 ) return (char **)mime;

	// http reply must be valid
	if ( ! m_httpReplyValid ) { g_process.shutdownAbort(true); }

	// make it valid
	m_contentValid = true;

	// assume none
	m_content    = NULL;
	m_contentLen = 0;

	// all done if no reply
	if ( ! m_httpReply ) return &m_content;

	// set the content, account for mime header
	m_content    = m_httpReply     + mime->getMimeLen() ;
	m_contentLen = m_httpReplySize - mime->getMimeLen() ;

	// watch out for this!
	if ( m_useFakeMime ) {
		m_content    = m_httpReply;
		m_contentLen = m_httpReplySize;
	}

	// why is this not really the size???
	m_contentLen--;

	// sanity check
	if ( m_contentLen < 0 ) { g_process.shutdownAbort(true); }
	return &m_content;
}

char getContentTypeFromContent ( char *p ) {
	char ctype = 0;
	// max
	char *pmax = p + 100;
	// check that out
	for ( ; p && *p && p < pmax ; p++ ) {
		if ( p[0] != '<' ) continue;
		if ( p[1] != '!' ) continue;
		if ( to_lower_a(p[2]) != 'd' ) continue;
		if ( strncasecmp(p,"<!doctype ",10) ) continue;
		char *dt = p + 10;
		// skip spaces
		for ( ; *dt ; dt++ ) {
			if ( ! is_wspace_a ( *dt ) ) break;
		}
		// point to that
		if ( ! strncasecmp(dt,"html"     ,4) ) ctype = CT_HTML;
		if ( ! strncasecmp(dt,"xml"      ,3) ) ctype = CT_XML;
		if ( ! strncasecmp(dt,"text/html",9) ) ctype = CT_HTML;
		if ( ! strncasecmp(dt,"text/xml" ,8) ) ctype = CT_XML;
		break;
	}
	return ctype;
}

uint8_t *XmlDoc::getContentType ( ) {
	if ( m_contentTypeValid ) return &m_contentType;
	// log debug
	setStatus("getting content type");
	// get the mime first
	HttpMime *mime = getMime();
	if ( ! mime || mime == (HttpMime *)-1 ) return (uint8_t *)mime;
	// then get mime
	m_contentType = mime->getContentType();
	// but if they specify <!DOCTYPE html> in the document that overrides
	// the content type in the mime! fixes planet.mozilla.org
	char **pp = getContent();
	if ( ! pp || pp == (void *)-1 ) return (uint8_t *)pp;
	char *p = *pp;
	// scan content for content type. returns 0 if none found.
	char ctype2 = getContentTypeFromContent ( p );
	// valid?
	if ( ctype2 != 0 ) m_contentType = ctype2;
	// it is valid now
	m_contentTypeValid = true;
	// give to to them
	return &m_contentType;
}



// . similar to getMetaRedirUrl but look for different strings
// . rel="canonical" or rel=canonical in a link tag.
Url **XmlDoc::getCanonicalRedirUrl ( ) {
	// return if we got it
	if ( m_canonicalRedirUrlValid ) return &m_canonicalRedirUrlPtr;

	// assume none in doc
	m_canonicalRedirUrlPtr = NULL;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	if ( ! cr->m_useCanonicalRedirects ) {
		m_canonicalRedirUrlValid = true;
		return &m_canonicalRedirUrlPtr;
	}

	// are we site root page? don't follow canonical url then.
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (Url **)isRoot;
	if ( *isRoot ) {
		m_canonicalRedirUrlValid = true;
		return &m_canonicalRedirUrlPtr;
	}

	// if this page has an inlink, then let it stand
	LinkInfo  *info1 = getLinkInfo1 ();
	if ( ! info1 || info1 == (LinkInfo *)-1 ) return (Url **)info1;
	if ( info1->getNumGoodInlinks() > 0 ) {
		m_canonicalRedirUrlValid = true;
		return &m_canonicalRedirUrlPtr;
	}

	uint8_t *ct = getContentType();
	if ( ! ct ) return NULL;

	// these canonical links only supported in xml/html i think
	if ( *ct != CT_HTML && *ct != CT_XML ) {
		m_canonicalRedirUrlValid = true;
		return &m_canonicalRedirUrlPtr;
	}

	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (Url **)xml;

	// scan nodes looking for a <link> node. like getBaseUrl()
	for ( int32_t i=0 ; i < xml->getNumNodes() ; i++ ) {
		// 12 is the <base href> tag id
		if ( xml->getNodeId ( i ) != TAG_LINK ) continue;
		// get the href field of this base tag
		int32_t linkLen;
		char *link = (char *) xml->getString ( i, "href", &linkLen );
		// skip if not valid
		if ( ! link || linkLen == 0 ) continue;
		// must also have rel=canoncial
		int32_t relLen;
		char *rel = xml->getString(i,"rel",&relLen);
		if ( ! rel ) continue;
		// skip if does not match "canonical"
		if ( strncasecmp(rel,"canonical",relLen) ) continue;
		// allow for relative urls
		Url *cu = getCurrentUrl();
		// set base to it
		m_canonicalRedirUrl.set( cu, link, linkLen );
		// assume it is not our url
		bool isMe = false;
		// if it is us, then skip!
		if(strcmp(m_canonicalRedirUrl.getUrl(),m_firstUrl.getUrl())==0)
			isMe = true;
		// might also be our redir url i guess
		if(strcmp(m_canonicalRedirUrl.getUrl(),m_redirUrl.getUrl())==0)
			isMe = true;
		// if it is us, keep it NULL, it's not a redirect. we are
		// the canonical url.
		if ( isMe ) break;
		// ignore if in an expanded iframe (<gbrame>) tag
		char *pstart = xml->getContent();
		char *p      = link;
		// scan backwards
		if ( ! m_didExpansion ) p = pstart;
		bool skip = false;
		for ( ; p > pstart ; p-- ) {
			if ( p[0] != '<' )
				continue;
			if ( p[1] == '/' &&
			     p[2] == 'g' &&
			     p[3] == 'b' &&
			     p[4] == 'f' &&
			     p[5] == 'r' &&
			     p[6] == 'a' &&
			     p[7] == 'm' &&
			     p[8] == 'e' &&
			     p[9] == '>' )
				break;
			if ( p[1] == 'g' &&
			     p[2] == 'b' &&
			     p[3] == 'f' &&
			     p[4] == 'r' &&
			     p[5] == 'a' &&
			     p[6] == 'm' &&
			     p[7] == 'e' &&
			     p[8] == '>' ) {
				skip = true;
				break;
			}
		}
		if ( skip ) continue;
		// otherwise, it is not us, we are NOT the canonical url
		// and we should not be indexed, but just ass the canonical
		// url as a spiderrequest into spiderdb, just like
		// simplified meta redirect does.
		m_canonicalRedirUrlPtr = &m_canonicalRedirUrl;
		break;
	}

	m_canonicalRedirUrlValid = true;
	return &m_canonicalRedirUrlPtr;
}



// returns false if none found
static bool setMetaRedirUrlFromTag ( char *p , Url *metaRedirUrl , char niceness ,
				     Url *cu ) {
	// limit scan
	char *limit = p + 30;
	// skip whitespace
	for ( ; *p && p < limit && is_wspace_a(*p) ; p++ );
	// must be a num
	if ( ! is_digit(*p) ) return false;
	// init delay
	int32_t delay = atol ( p );
	// ignore long delays
	if ( delay >= 10 ) return false;
	// now find the semicolon, if any
	for ( ; *p && p < limit && *p != ';' ; p++ );
	// must have semicolon
	if ( *p != ';' ) return false;
	// skip it
	p++;
	// skip whitespace some more
	for ( ; *p && p < limit && is_wspace_a(*p) ; p++ );
	// must have URL
	if ( strncasecmp(p,"URL",3) ) return false;
	// skip that
	p += 3;
	// skip white space
	for ( ; *p && p < limit && is_wspace_a(*p) ; p++ );
	// then an equal sign
	if ( *p != '=' ) return false;
	// skip equal sign
	p++;
	// them maybe more whitespace
	for ( ; *p && p < limit && is_wspace_a(*p) ; p++ );
	// an optional quote
	if ( *p == '\"' ) p++;
	// can also be a single quote!
	if ( *p == '\'' ) p++;
	// set the url start
	char *url = p;
	// now advance to next quote or space or >
	for ( ; *p && !is_wspace_a(*p) &&
		      *p !='\'' &&
		      *p !='\"' &&
		      *p !='>' ;
	      p++);
	// that is the end
	char *urlEnd = p;
	// get size
	int32_t usize = urlEnd - url;
	// skip if too big
	if ( usize > 1024 ) {
		log("build: meta redirurl of %" PRId32" bytes too big",usize);
		return false;
	}
	// get our current utl
	//Url *cu = getCurrentUrl();
	// decode what we got
	char decoded[MAX_URL_LEN];

	// convert &amp; to "&"
	int32_t decBytes = htmlDecode( decoded, url, usize, false );
	decoded[decBytes]='\0';

	// . then the url
	// . set the url to the one in the redirect tag
	// . but if the http-equiv meta redirect url starts with a '?'
	//   then just replace our cgi with that one
	if ( *url == '?' ) {
		char foob[MAX_URL_LEN*2];
		char *pf = foob;
		int32_t cuBytes = cu->getPathEnd() - cu->getUrl();
		gbmemcpy(foob,cu->getUrl(),cuBytes);
		pf += cuBytes;
		gbmemcpy ( pf , decoded , decBytes );
		pf += decBytes;
		*pf = '\0';
		metaRedirUrl->set(foob);
	}
	// . otherwise, append it right on
	// . use "url" as the base Url
	// . it may be the original url or the one we redirected to
	// . redirUrl is set to the original at the top
	else {
		// addWWW = false, stripSessId=true
		metaRedirUrl->set( cu, decoded, decBytes, false, true, false, false );
	}

	return true;
}



// scan document for <meta http-equiv="refresh" content="0;URL=xxx">
Url **XmlDoc::getMetaRedirUrl ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );;

	if ( m_metaRedirUrlValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );;
		return &m_metaRedirUrlPtr;
	}


	// get ptr to utf8 content
	if ( ! m_httpReplyValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "DIE, reply not valid." );;
		g_process.shutdownAbort(true);
	}

	char *p    = m_httpReply;
	// subtract one since this is a size not a length
	char *pend = p + m_httpReplySize - 1;//size_utf8Content;

	// assume no meta refresh url
	m_metaRedirUrlPtr = NULL;
	// make it valid regardless i guess
	m_metaRedirUrlValid = true;

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getCollRec failed" );;
		return NULL;
	}

	// if we are recycling or injecting, do not consider meta redirects
	if ( cr->m_recycleContent || m_recycleContent )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, recycleContent - do not consider meta redirects" );;
		return &m_metaRedirUrlPtr;
	}

	Url *cu = getCurrentUrl();

	bool gotOne = false;

	// advance a bit, we are initially looking for the 'v' char
	p += 10;
	// begin the string matching loop
	for ( ; p < pend ; p++ ) {
		// fix <!--[if lte IE 6]>
		// <meta http-equiv="refresh" content="0; url=/error-ie6/" />
		if ( *p == '!' &&
			 p[-1]=='<' &&
			 p[1] == '-' &&
			 p[2] == '-' ) {
				// find end of comment
				for ( ; p < pend ; p++ ) {
					if (p[0] == '-' &&
						p[1] == '-' &&
						p[2] == '>' )
							break;
				}
				// if found no end of comment, then stop
				if ( p >= pend )
					break;
				// resume looking for meta redirect tags
				continue;
		}

		// base everything off the equal sign
		if ( *p != '=' ) continue;
		// did we match "http-equiv="?
		if ( to_lower_a(p[-1]) != 'v' ) continue;
		if ( to_lower_a(p[-2]) != 'i' ) continue;
		if ( to_lower_a(p[-3]) != 'u' ) continue;
		if ( to_lower_a(p[-4]) != 'q' ) continue;
		if ( to_lower_a(p[-5]) != 'e' ) continue;
		if (            p[-6]  != '-' ) continue;
		if ( to_lower_a(p[-7]) != 'p' ) continue;
		if ( to_lower_a(p[-8]) != 't' ) continue;
		if ( to_lower_a(p[-9]) != 't' ) continue;
		if ( to_lower_a(p[-10])!= 'h' ) continue;

		// BR 20160306: Fix comparison where we have spaces before and/or after =
		// limit the # of white spaces
		char *limit = p + 20;
		// skip white spaces
		while ( *p && p < limit && is_wspace_a(*p) ) p++;

		// skip the equal sign
		// skip =
		if ( *p != '=' )
		{
			continue;
		}
		p++;

		// limit the # of white spaces
		limit = p + 20;
		// skip white spaces
		while ( *p && p < limit && is_wspace_a(*p) ) p++;

		// skip quote if there
		if ( *p == '\"' || *p == '\'' ) p++;
		// must be "refresh", continue if not
		if ( strncasecmp(p,"refresh",7) ) continue;
		// skip that
		p += 7;
		// skip another quote if there
		if ( *p == '\"' || *p == '\'' ) p++;

		// limit the # of white spaces
		limit = p + 20;
		// skip white spaces
		while ( *p && p < limit && is_wspace_a(*p) ) p++;

		// must be content now
		if ( strncasecmp(p,"content",7) ) continue;
		// skip that
		p += 7;


		// BR 20160306: Fix comparison where we have spaces before and/or after =
		// e.g. http://dnr.state.il.us/

		// limit the # of white spaces
		limit = p + 20;
		// skip white spaces
		while ( *p && p < limit && is_wspace_a(*p) ) p++;

		// skip =
		if ( *p != '=' )
		{
			continue;
		}
		p++;

		// limit the # of white spaces
		limit = p + 20;
		// skip white spaces
		while ( *p && p < limit && is_wspace_a(*p) ) p++;


		// skip possible quote
		if ( *p == '\"' || *p == '\'' ) p++;
		// PARSE OUT THE URL
		logTrace( g_conf.m_logTraceXmlDoc, "Possible redirect URL [%s]", p);

		Url dummy;
		if ( ! setMetaRedirUrlFromTag ( p , &dummy , m_niceness ,cu))
		{
			logTrace( g_conf.m_logTraceXmlDoc, "Failed to set redirect URL" );;
			continue;
		}

		gotOne = true;
		break;
	}


	if ( ! gotOne )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, none found" );;
		return &m_metaRedirUrlPtr;
	}

	// to fix issue with scripts containing
	// document.write('<meta http-equiv="Refresh" content="0;URL=http://ww
	// we have to get the Xml. we can't call getXml() because of
	// recursion bugs so just do it directly here

	Xml xml;
	// assume html since getContentType() is recursive on us.
	if ( !xml.set( m_httpReply, m_httpReplySize - 1, m_version, CT_HTML ) ) {
		// return NULL on error with g_errno set
		logTrace( g_conf.m_logTraceXmlDoc, "END, xml.set failed" );;
		return NULL;
	}

	XmlNode *nodes = xml.getNodes();
	int32_t     n  = xml.getNumNodes();
	// find the first meta summary node
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// continue if not a meta tag
		if ( nodes[i].m_nodeId != TAG_META ) continue;
		// only get content for <meta http-equiv=..>
		int32_t tagLen;
		char *tag ;
		tag = xml.getString ( i , "http-equiv" , &tagLen );
		// skip if empty
		if ( ! tag || tagLen <= 0 ) continue;
		// if not a refresh, skip it
		if ( strncasecmp ( tag , "refresh", 7 ) ) continue;
		// get the content
		tag = xml.getString ( i ,"content", &tagLen );
		// skip if empty
		if ( ! tag || tagLen <= 0 ) continue;

		logTrace( g_conf.m_logTraceXmlDoc, "Found possible URL in XmlNode" );;
		// PARSE OUT THE URL
		if (!setMetaRedirUrlFromTag(p,&m_metaRedirUrl,m_niceness,cu) )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "Failed to set URL from XmlNode data" );;
			continue;
		}

		// set it
		m_metaRedirUrlPtr = &m_metaRedirUrl;

		logTrace( g_conf.m_logTraceXmlDoc, "END, got redirect URL from XmlNode data" );;
		// return it
		return &m_metaRedirUrlPtr;
	}

	// nothing found
	logTrace( g_conf.m_logTraceXmlDoc, "END, nothing found" );;
	return &m_metaRedirUrlPtr;
}



uint16_t getCharsetFast ( HttpMime *mime,
			  char *url,
			  char *s ,
			  int32_t slen ){

	int16_t charset = csUnknown;

	if ( slen < 0 ) slen = 0;

	char *pstart = s;
	char *pend   = s + slen;

	const char *cs    = mime->getCharset();
	int32_t  cslen = mime->getCharsetLen();
	if ( cslen > 31 ) cslen = 31;
	if ( cs && cslen > 0 ) {
		charset = get_iana_charset ( cs , cslen );
	}

	// look for Unicode BOM first though
	cs = ucDetectBOM ( pstart , pend - pstart );
	if ( cs && charset == csUnknown ) {
		log(LOG_DEBUG, "build: Unicode BOM signature detected: %s",cs);
		int32_t len = strlen(cs);	if ( len > 31 ) len = 31;
		charset = get_iana_charset ( cs , len );
	}

	// prepare to scan doc
	char *p = pstart;

	// if the doc claims it is utf-8 let's double check because
	// newmexicomusic.org says its utf-8 in the mime header and it says
	// it is another charset in a meta content tag, and it is NOT in
	// utf-8, so don't trust that!
	if ( charset == csUTF8 ) {
		// loop over every char
		for ( char *s = pstart ; s < pend ; s += getUtf8CharSize(s) ) {
			// sanity check
			if ( ! isFirstUtf8Char ( s ) ) {
				// note it
				log(LOG_DEBUG,
				    "build: mime says UTF8 but does not "
				    "seem to be for url %s",url);
				// reset it back to unknown then
				charset = csUnknown;
				break;
			}
		}
	}

	// do not scan the doc if we already got it set
	if ( charset != csUnknown ) p = pend;

	//
	// it is inefficient to set xml just to get the charset.
	// so let's put in some quick string matching for this!
	//

	// . how big is one char? usually this is 1 unless we are in utf16...
	// . if we are in utf16 natively then this code needs to know that and
	//   set oneChar to 2! TODO!!
	//char oneChar = 1;
	// advance a bit, we are initially looking for the = sign
	if ( p ) p += 10;
	// begin the string matching loop
	for ( ; p < pend ; p++ ) {
		// base everything off the equal sign
		if ( *p != '=' ) continue;
		// must have a 't' or 'g' before the equal sign
		char c = to_lower_a(p[-1]);
		// did we match "charset="?
		if ( c == 't' ) {
			if ( to_lower_a(p[-2]) != 'e' ) continue;
			if ( to_lower_a(p[-3]) != 's' ) continue;
			if ( to_lower_a(p[-4]) != 'r' ) continue;
			if ( to_lower_a(p[-5]) != 'a' ) continue;
			if ( to_lower_a(p[-6]) != 'h' ) continue;
			if ( to_lower_a(p[-7]) != 'c' ) continue;
		}
		// did we match "encoding="?
		else if ( c == 'g' ) {
			if ( to_lower_a(p[-2]) != 'n' ) continue;
			if ( to_lower_a(p[-3]) != 'i' ) continue;
			if ( to_lower_a(p[-4]) != 'd' ) continue;
			if ( to_lower_a(p[-5]) != 'o' ) continue;
			if ( to_lower_a(p[-6]) != 'c' ) continue;
			if ( to_lower_a(p[-7]) != 'n' ) continue;
			if ( to_lower_a(p[-8]) != 'e' ) continue;
		}
		// if not either, go to next char
		else
			continue;
		// . make sure a <xml or a <meta preceeds us
		// . do not look back more than 500 chars
		char *limit = p - 500;
		// assume charset= or encoding= did NOT occur in a tag
		bool inTag = false;
		if ( limit <  pstart ) limit = pstart;
		for ( char *s = p ; s >= limit ; s -= 1 ) { // oneChar ) {
			// break at > or <
			if ( *s == '>' ) break;
			if ( *s != '<' ) continue;
			// . TODO: this could be in a quoted string too! fix!!
			// . is it in a <meta> tag?
			if ( to_lower_a(s[1]) == 'm' &&
			     to_lower_a(s[2]) == 'e' &&
			     to_lower_a(s[3]) == 't' &&
			     to_lower_a(s[4]) == 'a' ) {
				inTag = true;
				break;
			}
			// is it in an <xml> tag?
			if ( to_lower_a(s[1]) == 'x' &&
			     to_lower_a(s[2]) == 'm' &&
			     to_lower_a(s[3]) == 'l' ) {
				inTag = true;
				break;
			}
			// is it in an <?xml> tag?
			if ( to_lower_a(s[1]) == '?' &&
			     to_lower_a(s[2]) == 'x' &&
			     to_lower_a(s[3]) == 'm' &&
			     to_lower_a(s[4]) == 'l' ) {
				inTag = true;
				break;
			}
		}
		// if not in a tag proper, it is useless
		if ( ! inTag ) continue;
		// skip over equal sign
		p += 1;//oneChar;
		// skip over ' or "
		if ( *p == '\'' ) p += 1;//oneChar;
		if ( *p == '\"' ) p += 1;//oneChar;
		// keep start ptr
		char *csString = p;
		// set a limit
		limit = p + 50;
		if ( limit > pend ) limit = pend;
		if ( limit < p    ) limit = pend;
		// stop at first special character
		while ( p < limit &&
			*p &&
			*p !='\"' &&
			*p !='\'' &&
			! is_wspace_a(*p) &&
			*p !='>' &&
			*p != '<' &&
			*p !='?' &&
			*p !='/' &&
			// fix yaya.pro-street.us which has
			// charset=windows-1251;charset=windows-1"
			*p !=';' &&
			*p !='\\' )
			p += 1;//oneChar;
		// save it
		char d = *p;
		// do the actual NULL termination
		*p = 0;
		// get the character set
		int16_t metaCs = get_iana_charset(csString, strlen(csString));
		// put it back
		*p = d;
		// update "charset" to "metaCs" if known, it overrides all
		if (metaCs != csUnknown ) charset = metaCs;
		// all done, only if we got a known char set though!
		if ( charset != csUnknown ) break;
	}

        // alias these charsets so iconv understands
	if ( charset == csISO58GB231280 ||
	     charset == csHZGB2312      ||
	     charset == csGB2312         )
		charset = csGB18030;

	if ( charset == csEUCKR )
		charset = csKSC56011987; //x-windows-949

	// use utf8 if still unknown
	if ( charset == csUnknown ) {
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"doc: forcing utf8 charset");
		charset = csUTF8;
	}

	// once again, if the doc is claiming utf8 let's double check it!
	if ( charset == csUTF8 ) {
		// use this for iterating
		char size;
		// loop over every char
		for ( char *s = pstart ; s < pend ; s += size ) {
			// set
			size = getUtf8CharSize(s);
			// sanity check
			if ( ! isFirstUtf8Char ( s ) ) {
				// but let 0x80 slide? it is for the
				// 0x80 0x99 apostrophe i've seen for
				// eventvibe.com. it did have a first byte,
				// 0xe2 that led that sequece but it was
				// converted into &acirc; by something that
				// thought it was a latin1 byte.
				if ( s[0] == (char)0x80 &&
				     s[1] == (char)0x99 ) {
					s += 2;
					size = 0;
					continue;
				}
				// note it
				log(LOG_DEBUG,
				    "build: says UTF8 (2) but does not "
				    "seem to be for url %s"
				    " Resetting to ISOLatin1.",url);
				// reset it to ISO then! that's pretty common
				// no! was causing problems for
				// eventvibe.com/...Yacht because it had
				// some messed up utf8 in it but it really
				// was utf8. CRAP, but really messes up
				// sunsetpromotions.com and washingtonia
				// if we do not have this here
				charset = csISOLatin1;
				break;
			}
		}
	}

	//char *csName = get_charset_str(charset);

	// if we are not supported, set m_indexCode
	//if ( csName && ! supportedCharset(charset) ) {
	//	log("build: xml: Unsupported charset: %s", csName);
	//	g_errno = EBADCHARSET;
	//	return NULL;
	//	//charset = csUnknown;
	//	// i guess do not risk it
	//	//m_indexCode = EBADCHARSET;
	//}

	// all done
	return charset;
}


uint16_t *XmlDoc::getCharset ( ) {
	if ( m_charsetValid ) {
		return &m_charset;
	}

	// . get ptr to filtered content
	// . we can't get utf8 content yet until we know what charset this
	//   junk is so we can convert it!
	char **fc = getFilteredContent();
	if ( ! fc || fc == (void *)-1 ) {
		return (uint16_t *)fc;
	}

	// scan document for two things:
	// 1.  charset=  (in a <meta> tag)
	// 2. encoding=  (in an <?xml> tag)
	char *pstart = *fc;
	//char *pend   = *fc + m_filteredContentLen;

	// assume known charset
	m_charset = csUnknown;
	// make it valid regardless i guess
	m_charsetValid = true;

	// check in http mime for charset
	HttpMime *mime = getMime();
	if (mime && mime->getContentType() == CT_PDF) {
		// assume UTF-8
		m_charset = csUTF8;
		m_charsetValid = true;

		return &m_charset;
	}

	m_charset = getCharsetFast ( mime ,
				     m_firstUrl.getUrl(),
				     pstart ,
				     m_filteredContentLen );
	m_charsetValid = true;
	return &m_charset;
}



// declare these two routines for using threads
static void filterDoneWrapper    ( void *state, job_exit_t exit_type );
static void filterStartWrapper_r ( void *state );

// filters m_content if its pdf, word doc, etc.
char **XmlDoc::getFilteredContent ( ) {
	// return it if we got it already
	if ( m_filteredContentValid ) return &m_filteredContent;

	// this must be valid
	char **content = getContent();
	if ( ! content || content == (void *)-1 ) return content;
	// get the content type
	uint8_t *ct = getContentType();
	if ( ! ct ) return NULL;
	// it needs this
	HttpMime *mime = getMime();
	if ( ! mime || mime == (void *)-1 ) return (char **)mime;

	// make sure NULL terminated always
	// Why? pdfs can have nulls embedded
	// if ( m_content &&
	//      m_contentValid &&
	//      m_content[m_contentLen] ) {
	// 	g_process.shutdownAbort(true); }

	int32_t max , max2;
	CollectionRec *cr;
	bool filterable = false;

	if ( m_calledThread ) goto skip;

	// assume we do not need filtering by default
	m_filteredContent      = m_content;
	m_filteredContentLen   = m_contentLen;
	m_filteredContentValid = true;
	m_filteredContentAllocSize = 0;

	// empty content?
	if ( ! m_content ) return &m_filteredContent;

	if ( *ct == CT_HTML    ) return &m_filteredContent;
	if ( *ct == CT_TEXT    ) return &m_filteredContent;
	if ( *ct == CT_XML     ) return &m_filteredContent;
	// javascript - sometimes has address information in it, so keep it!
	if ( *ct == CT_JS      ) return &m_filteredContent;
	if ( m_contentLen == 0 ) return &m_filteredContent;

	// we now support JSON for diffbot
	if ( *ct == CT_JSON    ) return &m_filteredContent;

	if ( *ct == CT_ARC     ) return &m_filteredContent;
	if ( *ct == CT_WARC    ) return &m_filteredContent;

	// unknown content types are 0 since it is probably binary... and
	// we do not want to parse it!!
	if ( *ct == CT_PDF ) filterable = true;
	if ( *ct == CT_DOC ) filterable = true;
	if ( *ct == CT_XLS ) filterable = true;
	if ( *ct == CT_PPT ) filterable = true;
	if ( *ct == CT_PS  ) filterable = true;

	// if its a jpeg, gif, text/css etc. bail now
	if ( ! filterable ) {
		m_filteredContent      = NULL;
		m_filteredContentLen   = 0;
		m_filteredContentValid = true;
		return &m_filteredContent;
	}

	// invalidate
	m_filteredContentValid = false;

	cr = getCollRec();
	if ( ! cr ) return NULL;

	// if not text/html or text/plain, use the other max
	//max = MAXDOCLEN; // cr->m_maxOtherDocLen;
	max = cr->m_maxOtherDocLen;

	// if not text/html or text/plain, use the other max
	// max = MAXDOCLEN; // cr->m_maxOtherDocLen;

	// now we base this on the pre-filtered length to save memory because
	// our maxOtherDocLen can be 30M and when we have a lot of injections
	// at the same time we lose all our memory quickly
	max2 = 5 * m_contentLen + 10*1024;
	if ( max > max2 ) max = max2;
	// user uses -1 to specify no maxTextDocLen or maxOtherDocLen
	if ( max < 0    ) max = max2;
	// make a buf to hold filtered reply
	m_filteredContentAllocSize = max;
	m_filteredContent = (char *)mmalloc(m_filteredContentAllocSize,"xdfc");
	if ( ! m_filteredContent ) {
		log("build: Could not allocate %" PRId32" bytes for call to "
		    "content filter.",m_filteredContentMaxSize);
		return NULL;
	}

	// reset this here in case thread gets killed by the kill() call below
	m_filteredContentLen = 0;
	// update status msg so its visible in the spider gui
	setStatus ( "filtering content" );
	// reset this... why?
	g_errno = 0;
	// . call thread to call popen
	// . callThread returns true on success, in which case we block
	// . do not repeat
	m_calledThread = true;
	// reset this since filterStart_r() will set it on error
	m_errno = 0;

	// how can this be? don't core like this in thread, because it
	// does not save our files!!
	if ( ! m_mimeValid ) { g_process.shutdownAbort(true); }

	// do it
	if ( g_jobScheduler.submit(filterStartWrapper_r, filterDoneWrapper, this, thread_type_spider_filter, MAX_NICENESS) ) {
		// return -1 if blocked
		return (char **) -1;
	}

	// clear error!
	g_errno = 0;

	// note it
	log(LOG_INFO, "build: Could not spawn thread for call to content filter.");
	// get the data
	filterStart_r ( false ); // am thread?

	// skip down here if thread has returned and we got re-called
skip:

	// if size is 0, free the buf
	if ( m_filteredContentLen <= 0 ) {
		mfree ( m_filteredContent , m_filteredContentAllocSize,"fcas");
		m_filteredContent          = NULL;
		m_filteredContentLen       = 0;
		m_filteredContentAllocSize = 0;
	}

	// did we have an error from the thread?
	if ( m_errno ) g_errno = m_errno;
	// but bail out if it set g_errno
	if ( g_errno ) return NULL;
	// must be valid now - sanity check
	if ( ! m_filteredContentValid ) { g_process.shutdownAbort(true); }
	// return it
	return &m_filteredContent;
}

// come back here
// Use of ThreadEntry parameter is NOT thread safe
static void filterDoneWrapper ( void *state, job_exit_t /*exit_type*/ ) {
	// jump back into the brawl
	XmlDoc *THIS = (XmlDoc *)state;

	// if size is 0, free the buf. have to do this outside the thread
	// since malloc/free cannot be called in thread
	if ( THIS->m_filteredContentLen <= 0 ) {
		mfree ( THIS->m_filteredContent, THIS->m_filteredContentAllocSize,"fcas");
		THIS->m_filteredContent          = NULL;
		THIS->m_filteredContentLen       = 0;
		THIS->m_filteredContentAllocSize = 0;
	}

	// . call the master callback
	// . it will ultimately re-call getFilteredContent()
	THIS->m_masterLoop ( THIS->m_masterState );
}

// thread starts here
// Use of ThreadEntry parameter is NOT thread safe
static void filterStartWrapper_r ( void *state ) {
	XmlDoc *that = (XmlDoc *)state;

	// assume no error since we're at the start of thread call
	that->m_errno = 0;

	that->filterStart_r ( true ); // am thread?

	if (g_errno && !that->m_errno) {
		that->m_errno = g_errno;
	}
}


// sets m_errno on error
void XmlDoc::filterStart_r ( bool amThread ) {
	// get thread id
	pthread_t id = pthread_self();
	// sanity check
	if ( ! m_contentTypeValid ) { g_process.shutdownAbort(true); }
	// shortcut
	int32_t ctype = m_contentType;

	// assume none
	m_filteredContentLen = 0;

	// pass the input to the program through this file
	// rather than a pipe, since popen() seems broken
	char in[1024];
	snprintf(in,1023,"%sin.%" PRId64, g_hostdb.m_dir , (int64_t)id );
	unlink ( in );
	// collect the output from the filter from this file
	char out[1024];
	snprintf ( out , 1023,"%sout.%" PRId64, g_hostdb.m_dir, (int64_t)id );
	unlink ( out );
	// ignore errno from those unlinks
	errno = 0;
	// open the input file
 retry11:
	int fd = open ( in , O_WRONLY | O_CREAT , getFileCreationFlags() );
	if ( fd < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry11;
		m_errno = errno;
		log(LOG_WARN, "build: Could not open file %s for writing: %s.", in,mstrerror(m_errno));
		return;
	}
	// we are in a thread, this must be valid!
	if ( ! m_mimeValid ) { g_process.shutdownAbort(true);}

 retry12:
	// write the content into the input file
	int32_t w = write ( fd , m_content , m_contentLen );
	// valgrind
	if ( w < 0 && errno == EINTR ) goto retry12;
	// did we get an error
	if ( w != m_contentLen ) {
		//int32_t w = fwrite ( m_buf , 1 , m_bufLen , pd );
		//if ( w != m_bufLen ) {
		m_errno = errno;
		log(LOG_WARN, "build: Error writing to %s: %s.",in, mstrerror(m_errno));
		close(fd);
		return;
	}
	// close the file
	close ( fd );

	// shortcut
	char *wdir = g_hostdb.m_dir;

	char cmd[2048] = {};

	if (ctype == CT_PDF) {
		snprintf(cmd, 2047, "%sgbconvert.sh %s %s %s", wdir, g_contentTypeStrings[ctype], in, out);
	} else if ( ctype == CT_DOC ) {
		// "wdir" include trailing '/'? not sure
		snprintf(cmd, 2047, "ulimit -v 25000 ; ulimit -t 30 ; export ANTIWORDHOME=%s/antiword-dir ; timeout 30s nice -n 19 %s/antiword %s> %s" , wdir , wdir , in , out );
	} else if ( ctype == CT_XLS ) {
		snprintf(cmd, 2047, "ulimit -v 25000 ; ulimit -t 30 ; timeout 10s nice -n 19 %s/xlhtml %s > %s" , wdir , in , out );
	// this is too buggy for now... causes hanging threads because it
	// hangs, so i added 'timeout 10s' but that only works on newer
	// linux version, so it'll just error out otherwise.
	} else if ( ctype == CT_PPT ) {
		snprintf(cmd, 2047, "ulimit -v 25000 ; ulimit -t 30 ; timeout 10s nice -n 19 %s/ppthtml %s > %s" , wdir , in , out );
	} else if ( ctype == CT_PS  ) {
		snprintf(cmd, 2047, "ulimit -v 25000 ; ulimit -t 30; timeout 10s nice -n 19 %s/pstotext %s > %s" , wdir , in , out );
	} else {
		gbshutdownLogicError();
	}

	// breach sanity check
	//if ( strlen(cmd) > 2040 ) { g_process.shutdownAbort(true); }

	// execute it
	int retVal = gbsystem ( cmd );
	if ( retVal == -1 ) {
		log( LOG_WARN, "gb: system(%s) : %s", cmd, mstrerror( g_errno ) );
	}

	// all done with input file
	// clean up the binary input file from disk
	if ( unlink ( in ) != 0 ) {
		// log error
		log( LOG_WARN, "gbfilter: unlink(%s): %s",in, strerror(errno));

		// ignore it, since it was not a processing error per se
		errno = 0;
	}

retry13:
	fd = open ( out , O_RDONLY );
	if ( fd < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry13;
		m_errno = errno;
		log( LOG_WARN, "gbfilter: Could not open file %s for reading: %s.", out,mstrerror(m_errno));
		return;
	}
	// sanity -- need room to store a \0
	if ( m_filteredContentAllocSize < 2 ) { g_process.shutdownAbort(true); }
	// to read - leave room for \0
	int32_t toRead = m_filteredContentAllocSize - 1;
retry14:
	// read right from pipe descriptor
	int32_t r = read (fd, m_filteredContent,toRead);
	// note errors
	if ( r < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry14;
		log( LOG_WARN, "gbfilter: reading output: %s",mstrerror(errno));
		// this is often bad fd from an oom error, so ignore it
		//m_errno = errno;
		errno = 0;
		r = 0;
	}
	// clean up shop
	close ( fd );
	// delete output file
	unlink ( out );

	// validate now
	m_filteredContentValid = 1;
	// save the new buf len
	m_filteredContentLen = r;
	// ensure enough room for null term
	if ( r >= m_filteredContentAllocSize ) {
		g_process.shutdownAbort(true);
	}
	// ensure filtered stuff is NULL terminated so we can set the Xml class
	m_filteredContent [ m_filteredContentLen ] = '\0';
	// it is good
	m_filteredContentValid = true;

	// . at this point we got the filtered content
	// . bitch if we didn't allocate enough space
	if ( r > 0 && r == toRead )
		log(LOG_LOGIC,"build: Had to truncate document to %" PRId32" bytes "
		    "because did not allocate enough space for filter. "
		    "This should never happen. It is a hack that should be "
		    "fixed right.", toRead );
}


// return downloaded content as utf8
char **XmlDoc::getRawUtf8Content ( ) {
	// if we already computed it, return that
	if ( m_rawUtf8ContentValid ) return &m_rawUtf8Content;

	// . get our characterset
	// . crap! this can be recursive. it calls getXml() which calls
	//   getUtf8Content() which is us!
	uint16_t *charset = getCharset ( );
	if ( ! charset || charset == (uint16_t *)-1 ) return (char **)charset;

	const char *csName = get_charset_str(*charset);

	// . if not supported fix that!
	// . m_indexCode should be set to EBADCHARSET ultimately, but not here
	if ( ! supportedCharset(*charset) && csName ) {
		m_rawUtf8Content          = NULL;
		m_rawUtf8ContentSize      = 0;
		m_rawUtf8ContentAllocSize = 0;
		m_rawUtf8ContentValid     = true;
		return &m_rawUtf8Content;
	}

	// get ptr to filtered content
	char **fc = getFilteredContent();
	if ( ! fc || fc == (void *)-1 ) return (char **)fc;

	// make sure NULL terminated always
	if ( m_filteredContent &&
	     m_filteredContentValid &&
	     m_filteredContent[m_filteredContentLen] ) {
		g_process.shutdownAbort(true); }

	// NULL out if no content
	if ( ! m_filteredContent ) {
		m_rawUtf8Content          = NULL;
		m_rawUtf8ContentSize      = 0;
		m_rawUtf8ContentAllocSize = 0;
		m_rawUtf8ContentValid     = true;
		return &m_rawUtf8Content;
	}

	// assume already utf8
	m_rawUtf8Content          = m_filteredContent;
	m_rawUtf8ContentSize      = m_filteredContentLen + 1;
	m_rawUtf8ContentAllocSize = 0;

	// if we are not ascii or utf8 already, encode it into utf8
	if ( m_rawUtf8ContentSize > 1 &&
	     csName &&
	     *charset != csASCII &&
	     *charset != csUTF8 ) {
		// ok, no-go
		//ptr_utf8Content = NULL;
		m_rawUtf8Content = NULL;
		// assume utf8 will be twice the size ... then add a little
		int32_t  need = (m_filteredContentLen * 2) + 4096;
		char *buf  = (char *) mmalloc(need, "Xml3");
		// log oom error
		if ( ! buf ) {
			log("build: xml: not enough memory for utf8 buffer");
			return NULL;
		}
		// note it
		setStatus ( "converting doc to utf8" );
		// returns # of bytes i guess
		int32_t used = ucToUtf8 ( buf                  ,
				       // fix core dump by subtracting 10!
				       need - 10,
				       m_filteredContent    ,
				       m_filteredContentLen ,
				       csName               ,
				       -1                   ,//allowBadChars
				       m_niceness           );
		// clear this if successful, otherwise, it sets errno
		if ( used > 0 ) g_errno = 0;
		// unrecoverable error? bad charset is g_errno == E2BIG
		// which is like argument list too long or something
		// error from Unicode.cpp's call to iconv()
		if ( g_errno )
			log(LOG_INFO, "build: xml: failed parsing buffer: %s "
			    "(cs=%d)", mstrerror(g_errno), *charset);
		if ( g_errno && g_errno != E2BIG ) {
			mfree ( buf, need, "Xml3");
			// do not index this doc, delete from spiderdb/tfndb
			//if ( g_errno != ENOMEM ) m_indexCode = g_errno;
			// if conversion failed NOT because of bad charset
			// then return NULL now and bail out. probably ENOMEM
			return NULL;
		}
		// if bad charset... just make doc empty as a utf8 doc
		if ( g_errno == E2BIG ) {
			used = 0;
			buf[0] = '\0';
			buf[1] = '\0';
			// clear g_errno
			g_errno = 0;
			// and make a note for getIndexCode() so it will not
			// bother indexing the doc! nah, just index it
			// but with no content...
		}
		// crazy? this is pretty important...
		if ( used + 10 >= need )
			log("build: utf8 using too much buf space!!! u=%s",
			    getFirstUrl()->getUrl());
		// re-assign
		//ptr_utf8Content        = buf;
		//size_utf8Content       = used + 1;
		//m_utf8ContentAllocSize = need;
		m_rawUtf8Content          = buf;
		m_rawUtf8ContentSize      = used + 1;
		m_rawUtf8ContentAllocSize = need;
	}

	// convert \0's to spaces. why do we see these in some pages?
	// http://www.golflink.com/golf-courses/ has one in the middle after
	// about 32k of content.
	char *p    =     m_rawUtf8Content;
	char *pend = p + m_rawUtf8ContentSize - 1;
	for ( ; p < pend ; p++ ) {
		if ( ! *p ) *p = ' ';
	}


	//
	// VALIDATE the UTF-8
	//

	// . make a buffer to hold the decoded content now
	// . we were just using the m_expandedUtf8Content buf itself, but "n"
	//   ended up equalling m_expadedUtf8ContentSize one time for a
	//   doc, http://ediso.net/, which probably had corrupt utf8 in it,
	//   and that breached our buffer! so verify that this is good
	//   utf8, and that we can parse it without breaching our buffer!
	p = m_rawUtf8Content;
	// make sure NULL terminated always
	if ( p[m_rawUtf8ContentSize-1]) { g_process.shutdownAbort(true);}
	// make sure we don't breach the buffer when parsing it
	char size;
	char *lastp = NULL;
	for ( ; ; p += size ) {
		if ( p >= pend ) break;
		lastp = p;
		size = getUtf8CharSize(p);
	}
	// overflow?
	if ( p > pend && lastp ) {
		// back up to the bad utf8 char that made us overshoot
		p = lastp;
		// space it out
		for ( ; p < pend ; p++ ) *p = ' ';
		// log it maybe due to us not being keep alive http server?
		log("doc: fix bad utf8 overflow (because we are not "
		    "keepalive?) in doc %s",m_firstUrl.getUrl());
	}
	// overflow?
	if ( p != pend ) { g_process.shutdownAbort(true); }
	// sanity check for breach. or underrun in case we encountered a
	// premature \0
	if (p-m_rawUtf8Content!=m_rawUtf8ContentSize-1) {g_process.shutdownAbort(true);}

	// sanity -- must be \0 terminated
	if ( m_rawUtf8Content[m_rawUtf8ContentSize-1] ) {g_process.shutdownAbort(true); }

        // it might have shrunk us
	//m_rawUtf8ContentSize = n + 1;
	// we are good to go
	m_rawUtf8ContentValid = true;

	//return &ptr_utf8Content;
	return &m_rawUtf8Content;
}

// this is so Msg13.cpp can call getExpandedUtf8Content() to do its
// iframe expansion logic
static void getExpandedUtf8ContentWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	char **retVal = THIS->getExpandedUtf8Content();
	// return if blocked again
	if ( retVal == (void *)-1 ) return;
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}

// now if there are any <iframe> tags let's substitute them for
// the html source they represent here. that way we will get all the
// information you see on the page. this is somewhat critical since
// a lot of pages have their content in the frame.
char **XmlDoc::getExpandedUtf8Content ( ) {
	// if we already computed it, return that
	if ( m_expandedUtf8ContentValid ) return &m_expandedUtf8Content;

	// if called from spider compression proxy we need to set
	// masterLoop here now
	if ( ! m_masterLoop ) {
		m_masterLoop  = getExpandedUtf8ContentWrapper;
		m_masterState = this;
	}

	// get the unexpanded cpontent first
	char **up = getRawUtf8Content ();
	if ( ! up || up == (void *)-1 ) return up;

	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 ) return (char **)cu;

	// NULL out if no content
	if ( ! *up ) {
		m_expandedUtf8Content          = NULL;
		m_expandedUtf8ContentSize      = 0;
		m_expandedUtf8ContentValid     = true;
		return &m_expandedUtf8Content;
	}

	// do not do iframe expansion in order to keep injections fast
	if ( m_wasContentInjected ) {
		m_expandedUtf8Content     = m_rawUtf8Content;
		m_expandedUtf8ContentSize = m_rawUtf8ContentSize;
		m_expandedUtf8ContentValid = true;
		return &m_expandedUtf8Content;
	}

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) return (char **)ct;

	// if we have a json reply, leave it alone... do not expand iframes
	// in json, it will mess up the json
	if ( *ct == CT_JSON ) {
		m_expandedUtf8Content     = m_rawUtf8Content;
		m_expandedUtf8ContentSize = m_rawUtf8ContentSize;
		m_expandedUtf8ContentValid = true;
		return &m_expandedUtf8Content;
	}

	// we need this so getExtraDoc does not core
	int32_t *pfip = getFirstIp();
	if ( ! pfip || pfip == (void *)-1 ) return (char **)pfip;

	// point to it
	char *p    = *up;
	char *pend = *up + m_rawUtf8ContentSize; // includes \0
	// declare crap up here so we can jump into the for loop
	int32_t urlLen;
	char *url;
	char *fend;
	Url furl;
	XmlDoc **ped;
	XmlDoc *ed;
	bool inScript = false;
	bool match;
	// assign saved value if we got that
	if ( m_savedp ) {
		// restore "p"
		p = m_savedp;
		// update this
		ed = m_extraDoc;
		// and see if we got the mime now
		goto gotMime;
	}
	// now loop for frame and iframe tags
	for ( ; p < pend ; p += getUtf8CharSize(p) ) {
		// if never found a frame tag, just keep on chugging
		if ( *p != '<' ) continue;
		// <script>?
		if ( to_lower_a(p[1]) == 's' &&
		     to_lower_a(p[2]) == 'c' &&
		     to_lower_a(p[3]) == 'r' &&
		     to_lower_a(p[4]) == 'i' &&
		     to_lower_a(p[5]) == 'p' &&
		     to_lower_a(p[6]) == 't' )
			inScript = 1;
		// </script>?
		if ( p[1]=='/' &&
		     to_lower_a(p[2]) == 's' &&
		     to_lower_a(p[3]) == 'c' &&
		     to_lower_a(p[4]) == 'r' &&
		     to_lower_a(p[5]) == 'i' &&
		     to_lower_a(p[6]) == 'p' &&
		     to_lower_a(p[7]) == 't' )
			inScript = 0;
		// . skip if in script
		// . fixes guysndollsllc.com which has an iframe tag in
		//   a script section, "document.write ('<iframe..."
		if ( inScript ) continue;
		// iframe or frame?
		match = false;
		if ( to_lower_a(p[1]) == 'f' &&
		     to_lower_a(p[2]) == 'r' &&
		     to_lower_a(p[3]) == 'a' &&
		     to_lower_a(p[4]) == 'm' &&
		     to_lower_a(p[5]) == 'e' )
			match = true;
		if ( to_lower_a(p[1]) == 'i' &&
		     to_lower_a(p[2]) == 'f' &&
		     to_lower_a(p[3]) == 'r' &&
		     to_lower_a(p[4]) == 'a' &&
		     to_lower_a(p[5]) == 'm' &&
		     to_lower_a(p[6]) == 'e' )
			match = true;
		// skip tag if not iframe or frame
		if ( ! match ) continue;
		// check for frame or iframe
		//if ( strncasecmp(p+1,"frame " , 6) &&
		//     strncasecmp(p+1,"iframe ", 7) )
		//	continue;
		// get src tag (function in Words.h)
		url = getFieldValue ( p , pend - p ,"src" , &urlLen );
		// needs a src field
		if ( ! url ) continue;
		// "" is not acceptable either. techcrunch.com has
		// <iframe src=""> which ends up embedding the root url.
		if ( urlLen == 0 )
			continue;
		// skip if "about:blank"
		if ( urlLen==11 && strncmp(url,"about:blank",11) == 0 )
			continue;
		// get our current url
		//cu = getCurrentUrl();
		// set our frame url
		furl.set( cu, url, urlLen );
		// no recursion
		if ( strcmp(furl.getUrl(),m_firstUrl.getUrl()) == 0 )
			continue;
		// must be http or https, not ftp! ftp was causing us to
		// core in Msg22.cpp where it checks the url's protocol
		// when trying to lookup the old title rec.
		// http://sweetaub.ipower.com/ had an iframe with a ftp url.
		if ( ! furl.isHttp() && ! furl.isHttps() ) continue;
		/// @todo why are we ignoring specific domains here?
		// ignore google.com/ assholes for now
		if ( strstr(furl.getUrl(),"google.com/" ) ) continue;
		// and bing just to be safe
		if ( strstr(furl.getUrl(),"bing.com/" ) ) continue;
		// save it in case we have to return and come back later
		m_savedp = p;
		// break here
		//log("mdw: breakpoing here");
		// . download that. get as a doc. use 0 for max cache time
		// . no, use 5 seconds since we often have the same iframe
		//   in the root doc that we have in the  main doc, like a
		//   facebook iframe or something.
		// . use a m_maxCacheAge of 5 seconds now!
		ped = getExtraDoc ( furl.getUrl() , 5 );
		// should never block
		if ( ! ped ) {
			log("xmldoc: getExpandedutf8content = %s",
			    mstrerror(g_errno));
			return NULL;
		}
		// . return -1 if it blocked???
		// . no, this is not supported right now
		// . it will mess up our for loop
		if ( ped == (void *)-1 ) {g_process.shutdownAbort(true);}
		// cast it
		ed = *ped;
		// sanity
		if ( ! ed ) { g_process.shutdownAbort(true); }
		// jump in here from above
	gotMime:
		// make it not use the ips.txt cache
		//ed->m_useIpsTxtFile     = false;
		// get the mime
		HttpMime *mime = ed->getMime();
		if ( ! mime || mime == (void *)-1 ) return (char **)mime;
		// if not success, do not expand it i guess...
		if ( mime->getHttpStatus() != 200 ) {
			// free it
			nukeDoc ( ed );
			// and continue
			continue;
		}
		// update m_downloadEndTime if we should
		if ( ed->m_downloadEndTimeValid ) {
			// we must already be valid
			if ( ! m_downloadEndTimeValid ) {g_process.shutdownAbort(true);}
			// only replace it if it had ip and robots.txt allowed
			if ( ed->m_downloadEndTime )
				m_downloadEndTime = ed->m_downloadEndTime;
		}

		// re-write that extra doc into the content
		char **puc = ed->getRawUtf8Content();
		// this should not block
		//if ( puc == (void *)-1 ) { g_process.shutdownAbort(true); }
		// it blocked before! because the charset was not known!
		if ( puc == (void *)-1 ) return (char **)puc;
		// error?
		if ( ! puc ) return (char **)puc;
		// cast it
		char *uc = *puc;
		// or if no content, and no mime (like if robots.txt disallows)
		if ( ! uc || ed->m_rawUtf8ContentSize == 1 ) {
			// free it
			nukeDoc ( ed );
			// and continue
			continue;
		}
		// size includes terminating \0
		if ( uc[ed->m_rawUtf8ContentSize-1] ) { g_process.shutdownAbort(true);}

		// if first time we are expanding, set this
		if ( ! m_oldp ) m_oldp = *up;

		// find end of frame tag
		fend = p;
		for ( ; fend < pend ; fend += getUtf8CharSize(fend) ) {
			// if never found a frame tag, just keep on chugging
			if ( *fend == '>' ) break;
		}
		// if no end to the iframe tag was found, bail then...
		if ( fend >= pend ) continue;
		// skip the >
		fend++;

		// insert the non-frame crap first AND the frame/iframe tag
		m_esbuf.safeMemcpy ( m_oldp , fend - m_oldp );
		// end the frame
		//m_esbuf.safeMemcpy ( "</iframe>", 9 );
		// use our own special tag so Sections.cpp can set
		// Section::m_gbFrameNum which it uses internally
		m_esbuf.safePrintf("<gbframe>"); // gbiframe
		// identify javascript
		bool javascript = false;
		if ( *ed->getContentType() == CT_JS ) javascript = true;
		// so we do not mine javascript for cities and states etc.
		// in Address.cpp
		if ( javascript ) m_esbuf.safePrintf("<script>");
		// store that
		m_esbuf.safeMemcpy ( uc , ed->m_rawUtf8ContentSize - 1 );
		// our special tag has an end tag as well
		if ( javascript ) m_esbuf.safePrintf("</script>");
		m_esbuf.safePrintf("</gbframe>");
		// free up ed
		nukeDoc ( ed );

		// end of frame tag, skip over whole thing
		m_oldp = fend ;
		// sanity check
		if ( m_oldp > pend ) { g_process.shutdownAbort(true); }
		// another flag
		m_didExpansion = true;
		// count how many we did
		if ( ++m_numExpansions >= 5 ) break;
	}
	// default
	m_expandedUtf8Content     = m_rawUtf8Content;
	m_expandedUtf8ContentSize = m_rawUtf8ContentSize;
	// point to expansion buffer if we did any expanding
	if ( m_didExpansion ) {
		// copy over the rest
		m_esbuf.safeMemcpy ( m_oldp , pend - m_oldp );
		// null term it
		m_esbuf.pushChar('\0');
		// and point to that buffer
		m_expandedUtf8Content     = m_esbuf.getBufStart();//m_buf;
		// include the \0 as part of the size
		m_expandedUtf8ContentSize = m_esbuf.m_length; // + 1;
	}
	// sanity -- must be \0 terminated
	if ( m_expandedUtf8Content[m_expandedUtf8ContentSize-1] ) {
		g_process.shutdownAbort(true); }

	m_expandedUtf8ContentValid = true;
	return &m_expandedUtf8Content;
}





// . get the final utf8 content of the document
// . all html entities are replaced with utf8 chars
// . all iframes are expanded
// . if we are using diffbot then getting the utf8 content should return
//   the json which is the output from the diffbot api. UNLESS we are getting
//   the webpage itself for harvesting outlinks to spider later.
char **XmlDoc::getUtf8Content ( ) {

	// if we already computed it, return that
	if ( m_utf8ContentValid ) return &ptr_utf8Content;

	if ( m_setFromTitleRec ) {
		m_utf8ContentValid = true;
		return &ptr_utf8Content;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	setStatus("getting utf8 content");

	// recycle?
	if ( cr->m_recycleContent || m_recycleContent ||
	     // if trying to delete from index, load from old titlerec
	     m_deleteFromIndex ) {
		// get the old xml doc from the old title rec
		XmlDoc **pod = getOldXmlDoc ( );
		if ( ! pod || pod == (void *)-1 ) return (char **)pod;
		// shortcut
		XmlDoc *od = *pod;
		// this is non-NULL if it existed
		if ( od ) {
			ptr_utf8Content    = od-> ptr_utf8Content;
			size_utf8Content   = od->size_utf8Content;
			m_utf8ContentValid = true;
			m_contentType      = od->m_contentType;
			m_contentTypeValid = true;
			// sanity check
			if ( ptr_utf8Content &&
			     ptr_utf8Content[size_utf8Content-1] ) {
				g_process.shutdownAbort(true); }
			return &ptr_utf8Content;
		}
		// if could not find title rec and we are docid-based then
		// we can't go any further!!
		if ( m_setFromDocId ||
		     // it should be there if trying to delete as well!
		     m_deleteFromIndex ) {
			log("xmldoc: null utf8 content for docid-based "
			    "titlerec (d=%" PRId64") lookup which was not found",
			    m_docId);
			ptr_utf8Content = NULL;
			size_utf8Content = 0;
			m_utf8ContentValid = true;
			m_contentType = CT_HTML;
			m_contentTypeValid = true;
			return &ptr_utf8Content;
		}

	}

	char **ep = getExpandedUtf8Content();
	if ( ! ep || ep == (void *)-1 ) return ep;

	// NULL out if no content
	if ( ! *ep ) {
		ptr_utf8Content    = NULL;
		size_utf8Content   = 0;
		m_utf8ContentValid = true;
		return &ptr_utf8Content;
	}

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) return (char **)ct;

	// if we have a json reply, leave it alone... expanding a &quot;
	// into a double quote will mess up the JSON!
	if ( *ct == CT_JSON ) {
		ptr_utf8Content  = (char *)m_expandedUtf8Content;
		size_utf8Content = m_expandedUtf8ContentSize;
		m_utf8ContentValid = true;
		return &ptr_utf8Content;
	}

	// why would the spider proxy, who use msg13.cpp to call
	// XmlDoc::getExpandedUtf8Content() want to call this??? it seems
	// to destroy expandedutf8content with a call to htmldecode
	if ( m_isSpiderProxy ) { g_process.shutdownAbort(true); }

	// sabnity check
	if ( m_xmlValid   ) { g_process.shutdownAbort(true); }
	if ( m_wordsValid ) { g_process.shutdownAbort(true); }

	//
	// convert illegal utf8 characters into spaces
	//
	// fixes santaclarachorale.vbotickets.com/tickets/g.f._handels_israel_in_egypt/1062
	// which has a 228,0x80,& sequence (3 chars, last is ascii)
	char *x = m_expandedUtf8Content;
	char size;
	for ( ; *x ; x += size ) {
		size = getUtf8CharSize(x);
		// ok, make it a space i guess if it is a bad utf8 char
		if ( ! isValidUtf8Char(x) ) {
			*x = ' ';
			size = 1;
			continue;
		}
	}

	// sanity
	if ( ! m_contentTypeValid ) { g_process.shutdownAbort(true); }

	// richmondspca.org has &quot; in some tags and we do not like
	// expanding that to " because it messes up XmlNode::getTagLen()
	// and creates big problems. same for www.first-avenue.com. so
	// by setting doSpecial to try we change &lt; &gt and &quot; to
	// [ ] and ' which have no meaning in html per se.
	bool doSpecial = ( m_contentType != CT_XML );

	// . now decode those html entites into utf8 so that we never have to
	//   check for html entities anywhere else in the code. a big win!!
	// . doSpecial = true, so that &lt, &gt, &amp; and &quot; are
	//   encoded into high value
	//   utf8 chars so that Xml::set(), etc. still work properly and don't
	//   add any more html tags than it should
	// . this will decode in place
	// . MDW: 9/28/2014. no longer do for xml docs since i added
	//   hashXmlFields()
	int32_t n = m_expandedUtf8ContentSize - 1;
	if ( m_contentType != CT_XML ) {
		n = htmlDecode( m_expandedUtf8Content, m_expandedUtf8Content, m_expandedUtf8ContentSize - 1,
						doSpecial );
	}

	// can't exceed this! n does not include the final \0 even though
	// we do right it out.
	if ( n > m_expandedUtf8ContentSize-1 ) {g_process.shutdownAbort(true); }

	// sanity
	if ( m_expandedUtf8Content[n] != '\0' ) { g_process.shutdownAbort(true); }

	// sanity
	if ( n > m_expandedUtf8ContentSize-1 ) {g_process.shutdownAbort(true); }
	// sanity
	if ( m_expandedUtf8Content[n] != '\0' ) { g_process.shutdownAbort(true); }

	// finally transform utf8 apostrophe's into regular apostrophes
	// to make parsing easier
	uint8_t *p   = (uint8_t *)m_expandedUtf8Content;
	uint8_t *dst = (uint8_t *)m_expandedUtf8Content;
	uint8_t *pend = p + n;

	for ( ; *p ; p += size ) {
		size = getUtf8CharSize(p);

		// quick copy
		if ( size == 1 ) {
			*dst++ = *p;
			continue;
		}

		// check for crazy apostrophes
		if ( p[0] == 0xe2 && p[1] == 0x80 &&
		     ( p[2] == 0x98 ||    // U+2018 LEFT SINGLE QUOTATION MARK
		       p[2] == 0x99 ||    // U+2019 RIGHT SINGLE QUOTATION MARK
		       p[2] == 0x9b ) ) { // U+201B SINGLE HIGH-REVERSED-9 QUOTATION MARK
			*dst++ = '\'';
			continue;
		}

		// utf8 control character?
		if ( p[0] == 0xc2 &&
		     p[1] >= 0x80 && p[1] <= 0x9f ) {
			*dst++ = ' ';
			continue;
		}

		// double quotes in utf8
		// DO NOT do this if type JSON!! json uses quotes as control characters
		if (m_contentType != CT_JSON) {
			if ( p[0] == 0xe2 && p[1] == 0x80 ) {
				if ( p[2] == 0x9c ) {
					*dst++ = '\"';
					continue;
				}
				if ( p[2] == 0x9d ) {
					*dst++ = '\"';
					continue;
				}
			}
		}

		// and crazy hyphens (8 - 10pm)
		if ( ( p[0] == 0xc2 && p[1] == 0xad ) ||                  // U+00AD SOFT HYPHEN
		     ( p[0] == 0xe2 && p[1] == 0x80 && p[2] == 0x93 ) ||  // U+2013 EN DASH
			 ( p[0] == 0xe2 && p[1] == 0x80 && p[2] == 0x94 ) ) { // U+2014 EM DASH
			*dst++ = '-';
			continue;
		}

		// . convert all utf8 white space to ascii white space
		// . should benefit the string matching algo in
		//   XmlDoc::getEventSummary() which needs to skip spaces
		if ( ! g_map_is_ascii[(unsigned char)*p]  &&
		     is_wspace_utf8(p) ) {
			*dst++ = ' ';
			continue;
		}

		// otherwise, just copy it
		gbmemcpy(dst,p,size);
		dst += size;
	}

	// null term
	*dst++ = '\0';

	// now set it up
	ptr_utf8Content  = (char *)m_expandedUtf8Content;
	size_utf8Content = (char *)dst - m_expandedUtf8Content;

	// sanity -- skipped over the \0???
	if ( p > pend ) { g_process.shutdownAbort(true); }

	// sanity check
	if ( ptr_utf8Content && ptr_utf8Content[size_utf8Content-1] ) {
		g_process.shutdownAbort(true); }

	m_utf8ContentValid = true;
	return &ptr_utf8Content;
}

// *pend should be \0
int32_t getContentHash32Fast ( unsigned char *p , int32_t plen ) {
	// sanity
	if ( ! p ) return 0;
	if ( plen <= 0 ) return 0;
	if ( p[plen] != '\0' ) { g_process.shutdownAbort(true); }
	unsigned char *pend = p + plen;

	static bool s_init = false;
	static char s_qtab0[256];
	static char s_qtab1[256];
	static char s_qtab2[256];
	static const char * const s_skips[] = {
		"jan",
		"feb",
		"mar",
		"apr",
		"may",
		"jun",
		"jul",
		"aug",
		"sep",
		"oct",
		"nov",
		"dec",
		"sun",
		"mon",
		"tue",
		"wed",
		"thu",
		"fri",
		"sat" };
	if ( ! s_init ) {
		// only call this crap once
		s_init = true;
		// clear up
		memset(s_qtab0,0,256);
		memset(s_qtab1,0,256);
		memset(s_qtab2,0,256);
		for ( int32_t i = 0 ; i < 19  ; i++ ) {
			unsigned char *s = (unsigned char *)s_skips[i];
			s_qtab0[(unsigned char)to_lower_a(s[0])] = 1;
			s_qtab0[(unsigned char)to_upper_a(s[0])] = 1;
			// do the quick hash
			unsigned char qh = to_lower_a(s[0]);
			qh ^= to_lower_a(s[1]);
			qh <<= 1;
			qh ^= to_lower_a(s[2]);
			s_qtab1[qh] = 1;
			// try another hash, the swift hash
			unsigned char sh = to_lower_a(s[0]);
			sh <<= 1;
			sh ^= to_lower_a(s[1]);
			sh <<= 1;
			sh ^= to_lower_a(s[2]);
			s_qtab2[sh] = 1;
		}
	}

	bool lastWasDigit = false;
	bool lastWasPunct = true;
	uint32_t h = 0LL;
	//char  size = 0;
	unsigned char pos = 0;
	for ( ; p < pend ; p++ ) { //  += size ) {
		if ( ! is_alnum_a ( *p ) ) {
			lastWasDigit = false;
			lastWasPunct = true;
			continue;
		}

		// if its a digit, call it 1
		if ( is_digit(*p) ) {
			// skip consecutive digits
			if ( lastWasDigit ) continue;
			// xor in a '1'
			h ^= g_hashtab[pos][(unsigned char)'1'];
			pos++;
			lastWasDigit = true;
			continue;
		}

		// reset
		lastWasDigit = false;

		// exclude days of the month or week so clocks do
		// not affect this hash
		if ( s_qtab0[p[0]] && lastWasPunct && p[1] && p[2] ) {
			// quick hash
			unsigned char qh = to_lower_a(p[0]);
			qh ^= to_lower_a(p[1]);
			qh <<= 1;
			qh ^= to_lower_a(p[2]);
			// look that up
			if ( ! s_qtab1[qh] ) goto skip;
			// try another hash, the swift hash
			unsigned char sh = to_lower_a(p[0]);
			sh <<= 1;
			sh ^= to_lower_a(p[1]);
			sh <<= 1;
			sh ^= to_lower_a(p[2]);
			if ( ! s_qtab2[sh] ) goto skip;
			// ok, probably a match..
			unsigned char *s = p + 3;
			// skip to end of word
			for ( ; s < pend ; s++ ) {
				if ( ! is_alnum_a ( *s ) )
					break;
			}

			// advance p now
			p = s;

			// hash as one type of thing...
			h ^= g_hashtab[pos][(unsigned char)'X'];

			pos++;
			continue;
		}

	skip:
		// reset this
		lastWasPunct = false;
		// xor this in right
		h ^= g_hashtab[pos][p[0]];
		pos++;
		// assume ascii or latin1
		continue;
	}
	return h;
}

int32_t *XmlDoc::getContentHash32 ( ) {
	// return it if we got it
	if ( m_contentHash32Valid ) return &m_contentHash32;
	setStatus ( "getting contenthash32" );

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) return (int32_t *)ct;

	// we do not hash the url/resolved_url/html fields in diffbot json
	// because the url field is a mirror of the url and the html field
	// is redundant and would slow us down
	if ( *ct == CT_JSON )
		return getContentHashJson32();

	// . get the content. get the pure untouched content!!!
	// . gotta be pure since that is what Msg13.cpp computes right
	//   after it downloads the doc...
	// . if iframes are present, msg13 gives up
	char **pure = getContent();
	if ( ! pure || pure == (char **)-1 ) return (int32_t *)pure;

	unsigned char *p = (unsigned char *)(*pure);
	int32_t plen = m_contentLen;//size_utf8Content - 1;

	// no content means no hash32
	if ( plen <= 0 ) {//ptr_utf8Content ) {
		m_contentHash32 = 0;
		m_contentHash32Valid = true;
		return &m_contentHash32;
	}

	// we set m_contentHash32 in ::hashJSON() below because it is special
	// for diffbot since it ignores certain json fields like url: and the
	// fields are independent, and numbers matter, like prices

	// *pend should be \0
	m_contentHash32 = getContentHash32Fast ( p , plen );
	// validate
	m_contentHash32Valid = true;
	return &m_contentHash32;
}

// we do not hash the url/resolved_url/html fields in diffbot json
// because the url field is a mirror of the url and the html field
// is redundant and would slow us down
int32_t *XmlDoc::getContentHashJson32 ( ) {

	if ( m_contentHash32Valid ) return &m_contentHash32;

	// use new json parser
	Json *jp = getParsedJson();
	if ( ! jp || jp == (void *)-1 ) return (int32_t *)jp;

	JsonItem *ji = jp->getFirstItem();
	int32_t totalHash32 = 0;

	//logf(LOG_DEBUG,"ch32: url=%s",m_firstUrl.m_url);

	for ( ; ji ; ji = ji->m_next ) {
		// skip if not number or string
		if ( ji->m_type != JT_NUMBER && ji->m_type != JT_STRING )
			continue;

		char *topName = NULL;

		// what name level are we?
		int32_t numNames = 1;
		JsonItem *pi = ji->m_parent;
		for ( ; pi ; pi = pi->m_parent ) {
			// empty name?
			if ( ! pi->m_name ) continue;
			if ( ! pi->m_name[0] ) continue;
			topName = pi->m_name;
			numNames++;
		}

		// if we are the diffbot reply "html" field do not hash this
		// because it is redundant and it hashes html tags etc.!
		// plus it slows us down a lot and bloats the index.
		if ( ji->m_name && numNames==1 &&
		     strcmp(ji->m_name,"html") == 0 )
			continue;

		if ( ji->m_name && numNames==1 &&
		     strcmp(ji->m_name,"url") == 0 )
			continue;

		if ( ji->m_name && numNames==1 &&
		     strcmp(ji->m_name,"pageUrl") == 0 )
			continue;

		if ( ji->m_name && numNames==1 &&
		     strcmp(ji->m_name,"resolved_url") == 0 )
			continue;

		if ( topName && strcmp(topName,"stats") == 0 )
			continue;

		if ( topName && strcmp(topName,"queryString") == 0 )
			continue;

		if ( topName && strcmp(topName,"nextPages") == 0 )
			continue;

		if ( topName && strcmp(topName,"textAnalysis") == 0 )
			continue;

		if ( topName && strcmp(topName,"links") == 0 )
			continue;


		// hash the fully compound name
		int32_t nameHash32 = 0;
		JsonItem *p = ji;
		char *lastName = NULL;
		for ( ; p ; p = p->m_parent ) {
			// empty name?
			if ( ! p->m_name ) continue;
			if ( ! p->m_name[0] ) continue;
			// dup? can happen with arrays. parent of string
			// in object, has same name as his parent, the
			// name of the array. "dupname":[{"a":"b"},{"c":"d"}]
			if ( p->m_name == lastName ) continue;
			// update
			lastName = p->m_name;
			// hash it up
			nameHash32 = hash32(p->m_name,p->m_nameLen,nameHash32);
		}

		//
		// now Json.cpp decodes and stores the value into
		// a buffer, so ji->getValue() should be decoded completely
		//

		// . get the value of the json field
		// . if it's a number or bool it converts into a string
		int32_t vlen;
		char *val = ji->getValueAsString( &vlen );

		//
		// for deduping search results we set m_contentHash32 here for
		// diffbot json objects.
		//
		// we use this hash for setting EDOCUNCHANGED when reindexing
		// a diffbot reply. we also use to see if the diffbot reply
		// is a dup with another page in the index. thirdly, we use
		// to dedup search results, which could be redundant because
		// of our spider-time deduping.
		//
		// make the content hash so we can set m_contentHash32
		// for deduping. do an exact hash for now...
		int32_t vh32 = hash32 ( val , vlen , m_niceness );
		// combine
		int32_t combined32 = hash32h ( nameHash32 , vh32 );
		// accumulate field/val pairs order independently
		totalHash32 ^= combined32;
		// debug note
		//logf(LOG_DEBUG,"ch32: field=%s nh32=%" PRIu32" vallen=%" PRId32,
		//     ji->m_name,
		//     nameHash32,
		//     vlen);
	}

	m_contentHash32 = totalHash32;
	m_contentHash32Valid = true;
	return &m_contentHash32;
}

int32_t XmlDoc::getHostHash32a ( ) {
	if ( m_hostHash32aValid ) return m_hostHash32a;
	m_hostHash32aValid = true;
	Url *f = getFirstUrl();
	m_hostHash32a = f->getHostHash32();
	return m_hostHash32a;
}

int32_t XmlDoc::getDomHash32( ) {
	if ( m_domHash32Valid ) return m_domHash32;
	m_domHash32Valid = true;
	Url *f = getFirstUrl();
	m_domHash32 = hash32 ( f->getDomain(), f->getDomainLen() );
	return m_domHash32;
}

// . this will be the actual pnm data of the image thumbnail
// . you can inline it in an image tag like
//   <img src="data:image/png;base64,iVBORw0...."/>
//   background-image:url(data:image/png;base64,iVBORw0...);
// . FORMAT of ptr_imageData:
//   <origimageUrl>\0<4bytethumbwidth><4bytethumbheight><thumbnaildatajpg>
char **XmlDoc::getThumbnailData ( ) {
	if ( m_imageDataValid ) return &ptr_imageData;
	Images *images = getImages();
	if ( ! images || images == (Images *)-1 ) return (char **)images;
	ptr_imageData  = NULL;
	size_imageData = 0;
	m_imageDataValid = true;
	if ( ! images || ! images->m_imageBufValid ) return &ptr_imageData;
	if ( images->m_imageBuf.length() <= 0 ) return &ptr_imageData;
	// this buffer is a ThumbnailArray
	ptr_imageData  = images->m_imageBuf.getBufStart();
	size_imageData = images->m_imageBuf.length();
	return &ptr_imageData;
}

Images *XmlDoc::getImages ( ) {
	if ( m_imagesValid ) return &m_images;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	if ( ! cr->m_makeImageThumbnails ) {
		m_images.reset();
		m_imagesValid = true;
		return &m_images;
	}

	setStatus ( "getting thumbnail" );

	Words *words = getWords();
	if ( ! words || words == (Words *)-1 ) return (Images *)words;
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (Images *)xml;
	Sections *sections = getSections();
	if ( ! sections || sections==(Sections *)-1) return (Images *)sections;
	char *site = getSite ();
	if ( ! site || site == (char *)-1 ) return (Images *)site;
	int64_t *d = getDocId();
	if ( ! d || d == (int64_t *)-1 ) return (Images *)d;
	int8_t *hc = getHopCount();
	if ( ! hc || hc == (void *)-1 ) return (Images *)hc;
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 ) return (Images *)cu;

	// . this does not block or anything
	// . if we are a diffbot json reply it should just use the primary
	//   image, if any, as the only candidate
	m_images.setCandidates ( cu , words , xml , sections , this );

	setStatus ("getting thumbnail");

	// assume valid
	m_imagesValid = true;

	// now get the thumbnail
	if ( ! m_images.getThumbnail ( site         ,
				       strlen(site) ,
				       *d           ,
				       this         ,
				       cr->m_collnum       ,
				       //NULL         , // statusPtr ptr
				       *hc          ,
				       m_masterState,
				       m_masterLoop ) )
		return (Images *)-1;

	return &m_images;
}


// . get different attributes of the Links as vectors
// . these are 1-1 with the Links::m_linkPtrs[] array
TagRec ***XmlDoc::getOutlinkTagRecVector () {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// if page has a <meta name=usefakeips content=1> tag
	// then use the hash of the links host as the firstip.
	// this will speed things up when adding a gbdmoz.urls.txt.*
	// file to index every url in dmoz.
	char *useFakeIps = hasFakeIpsMetaTag();
	if ( ! useFakeIps || useFakeIps == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, using fake IPs" );
		return (TagRec ***)useFakeIps;
	}

	// no error and valid, return quick
	if ( m_outlinkTagRecVectorValid && *useFakeIps )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid (using fake IPs)" );
		return &m_outlinkTagRecVector;
	}

	// error?
	if ( m_outlinkTagRecVectorValid && m_msge0.m_errno ) {
		g_errno = m_msge0.m_errno;
		logTrace( g_conf.m_logTraceXmlDoc, "END, g_errno %" PRId32, g_errno);
		return NULL;
	}

	// if not using fake ips, give them the real tag rec vector
	if ( m_outlinkTagRecVectorValid )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid (and not fake IPs)" );
		return &m_msge0.m_tagRecPtrs;
	}

	Links *links = getLinks();
	if ( ! links || links == (void *) -1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinks returned -1" );
		return (TagRec ***)links;
	}

	if ( *useFakeIps ) {
		// set to those
		m_fakeTagRec.reset();
		// just make a bunch ptr to empty tag rec
		int32_t need = links->m_numLinks * sizeof(TagRec *);
		if ( ! m_fakeTagRecPtrBuf.reserve ( need ) )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "END, could not reserve fake IP buffer" );
			return NULL;
		}

		// make them all point to the fake empty tag rec
		TagRec **grv = (TagRec **)m_fakeTagRecPtrBuf.getBufStart();
		for ( int32_t i = 0 ; i < links->m_numLinks ; i++ )
			grv[i] = &m_fakeTagRec;
		// set it
		m_outlinkTagRecVector = grv;
		m_outlinkTagRecVectorValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END, point to empty buffer (using fake IPs)" );
		return &m_outlinkTagRecVector;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getCollRec failed" );
		return NULL;
	}


	// update status msg
	setStatus ( "getting outlink tag rec vector" );
	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getTagRec returned -1" );
		return (TagRec ***)gr;
	}

	// assume valid
	m_outlinkTagRecVectorValid = true;
	// go get it
	if ( ! m_msge0.getTagRecs ( links->m_linkPtrs  ,
				    links->m_linkFlags ,
				    links->m_numLinks  ,
				    false              , // skip old?
				    // make it point to this basetagrec if
				    // the LF_SAMEHOST flag is set for the link
				    gr ,
				    cr->m_collnum             ,
				    m_niceness         ,
				    m_masterState      ,
				    m_masterLoop       )) {
		// sanity check
		if ( m_doingConsistencyCheck ) { g_process.shutdownAbort(true); }

		// we blocked
		logTrace( g_conf.m_logTraceXmlDoc, "END, msge0.getTagRecs blocked" );
		return (TagRec ***)-1;
	}

	// error?
	if ( g_errno )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, g_errno %" PRId32" after msge0.getTagRecs", g_errno);
		return NULL;
	}

	// or this?
	if ( m_msge0.m_errno ) {
		g_errno = m_msge0.m_errno;
		logTrace( g_conf.m_logTraceXmlDoc, "END, m_msge0.m_errno=%" PRId32, g_errno);
		return NULL;
	}
	// set it
	//m_outlinkTagRecVector = m_msge0.m_tagRecPtrs;
	// ptr to a list of ptrs to tag recs
	logTrace( g_conf.m_logTraceXmlDoc, "END, got list" );
	return &m_msge0.m_tagRecPtrs;
}



char *XmlDoc::hasNoIndexMetaTag() {
	if ( m_hasNoIndexMetaTagValid )
		return &m_hasNoIndexMetaTag;
	// assume none
	m_hasNoIndexMetaTag = false;
	// store value/content of meta tag in here
	char mbuf[16];
	mbuf[0] = '\0';
	const char *tag = "noindex";
	int32_t tlen = strlen(tag);
	// check the xml for a meta tag
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	xml->getMetaContent ( mbuf, 16 , tag , tlen );
	if ( mbuf[0] == '1' ) m_hasNoIndexMetaTag = true;
	m_hasNoIndexMetaTagValid = true;
	return &m_hasNoIndexMetaTag;
}


char *XmlDoc::hasFakeIpsMetaTag ( ) {
	if ( m_hasUseFakeIpsMetaTagValid ) return &m_hasUseFakeIpsMetaTag;

	char mbuf[16];
	mbuf[0] = '\0';
	const char *tag = "usefakeips";
	int32_t tlen = strlen(tag);

	// check the xml for a meta tag
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	xml->getMetaContent ( mbuf, 16 , tag , tlen );

	m_hasUseFakeIpsMetaTag = false;
	if ( mbuf[0] == '1' ) m_hasUseFakeIpsMetaTag = true;
	m_hasUseFakeIpsMetaTagValid = true;
	return &m_hasUseFakeIpsMetaTag;
}


int32_t **XmlDoc::getOutlinkFirstIpVector () {

	Links *links = getLinks();
	if ( ! links ) return NULL;

	// if page has a <meta name=usefakeips content=1> tag
	// then use the hash of the links host as the firstip.
	// this will speed things up when adding a gbdmoz.urls.txt.*
	// file to index every url in dmoz.
	char *useFakeIps = hasFakeIpsMetaTag();
	if ( ! useFakeIps || useFakeIps == (void *)-1 )
		return (int32_t **)useFakeIps;

	if ( *useFakeIps && m_outlinkIpVectorValid )
		return &m_outlinkIpVector;

	if ( *useFakeIps ) {
		int32_t need = links->m_numLinks * 4;
		m_fakeIpBuf.reserve ( need );
		for ( int32_t i = 0 ; i < links->m_numLinks ; i++ ) {
			uint64_t h64 = links->getHostHash64(i);
			int32_t ip = h64 & 0xffffffff;
			m_fakeIpBuf.pushLong(ip);
		}
		int32_t *ipBuf = (int32_t *)m_fakeIpBuf.getBufStart();
		m_outlinkIpVector = ipBuf;
		m_outlinkIpVectorValid = true;
		return &m_outlinkIpVector;
	}

	// return msge1's buf otherwise
	if ( m_outlinkIpVectorValid )
		return &m_msge1.m_ipBuf;

	// should we have some kinda error for msge1?
	//if ( m_outlinkIpVectorValid && m_msge1.m_errno ) {
	//	g_errno = m_msge1.m_errno;
	//	return NULL;
	//}

	// . we now scrounge them from TagRec's "firstip" tag if there!
	// . that way even if a domain changes its ip we still use the
	//   original ip, because the only reason we need this ip is for
	//   deciding which group of hosts will store this SpiderRequest and
	//   we use that for throttling, so we have to be consistent!!!
	// . we never add -1 or 0 ips to tagdb though.... (NXDOMAIN,error...)
	// . uses m_msgeForTagRecs for this one
	TagRec ***grv = getOutlinkTagRecVector();
	if ( ! grv || grv == (void *)-1 ) return (int32_t **)grv;
	// note it
	setStatus ( "getting outlink first ip vector" );
	// assume valid
	m_outlinkIpVectorValid = true;
	// sanity check
	//if ( ! m_spideredTimeValid ) { g_process.shutdownAbort(true); }
	// use this
	int32_t nowGlobal = getSpideredTime();//m_spideredTime;
	// add tags to tagdb?
	bool addTags = true;
	//if ( m_sreqValid && m_sreq.m_isPageParser ) addTags = false;
	if ( getIsPageParser() ) addTags = false;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// . go get it
	// . this will now update Tagdb with the "firstip" tags if it should!!
	// . this just dns looks up the DOMAINS of each outlink because these
	//   are *first* ips and ONLY used by Spider.cpp for throttling!!!
	if ( ! m_msge1.getFirstIps ( *grv               ,
				     links->m_linkPtrs  ,
				     links->m_linkFlags ,
				     links->m_numLinks  ,
				     false              , // skip old?
				     cr->m_coll             ,
				     m_niceness         ,
				     m_masterState      ,
				     m_masterLoop       ,
				     nowGlobal          ,
				     addTags            )) {
		// sanity check
		if ( m_doingConsistencyCheck ) { g_process.shutdownAbort(true); }
		// we blocked
		return (int32_t **)-1;
	}
	// error?
       	if ( g_errno ) return NULL;
	// . ptr to a list of ptrs to tag recs
	// . ip will be -1 on error
	return &m_msge1.m_ipBuf;
}

int32_t *XmlDoc::getUrlFilterNum ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	// return it if already set
	if ( m_urlFilterNumValid ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END. already valid: %" PRId32, m_urlFilterNum );
		return &m_urlFilterNum;
	}

	// note that
	setStatus ( "getting url filter row num");

	// . make the partial new spider rec
	// . we need this for matching filters like lang==zh_cn
	// . crap, but then it matches "hasReply" when it should not
	// . PROBLEM! this is the new reply not the OLD reply, so it may
	//   end up matching a DIFFERENT url filter num then what it did
	//   before we started spidering it...
	//SpiderReply *newsr = getNewSpiderReply ( );
	// note it
	//if ( ! newsr )
	//	log("doc: getNewSpiderReply: %s",mstrerror(g_errno));
	//if ( ! newsr || newsr == (void *)-1 ) return (int32_t *)newsr;

	// need language i guess
	uint8_t *langId = getLangId();
	if ( ! langId || langId == (uint8_t *)-1 ) {
//		log("build: failed to get url filter for xmldoc %s - could not get language id",
//		    m_firstUrl.getUrl());
		logTrace( g_conf.m_logTraceXmlDoc, "END. unable to get langId" );
		return (int32_t *)langId;
	}


	// make a fake one for now
	// SpiderReply fakeReply;
	// // fix errors
	// fakeReply.reset();
	// fakeReply.m_isIndexedINValid = true;
	// // just language for now, so we can FILTER by language
	// if ( m_langIdValid ) fakeReply.m_langId = m_langId;

	int32_t langIdArg = -1;
	if ( m_langIdValid ) langIdArg = m_langId;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;
	// this must be valid
	//if ( ! m_spideredTimeValid ) { g_process.shutdownAbort(true); }
	int32_t spideredTime = getSpideredTime();
	// get the spider request
	SpiderRequest *oldsr = &m_sreq;
	// null it out if invalid...
	if ( ! m_sreqValid ) oldsr = NULL;
	// do not set the spideredTime in the spiderReply to 0
	// so we do not trigger the lastSpiderTime
	//int32_t saved = newsr->m_spideredTime;
	//newsr->m_spideredTime = 0;
	//
	// PROBLEM: we end up matching "isIndexed" in the url filters
	// even if this is a NEW document because we pass it in the spider
	// reply that we generate now even though another spider reply
	// may not exist.
	//
	// SOLUTION: just do not supply a spider reply, we only seem to
	// use the urlfilternum to get a diffbot api url OR to see if the
	// document is banned/filtered so we should delete it. otherwise
	// we were supplying "newsr" above...

	// . look it up
	// . use the old spidered date for "nowGlobal" so we can be consistent
	//   for injecting into the "qatest123" coll
	int32_t ufn = ::getUrlFilterNum ( oldsr,
					  NULL,//&fakeReply,
					  spideredTime,false,
					  cr,
					  false, // isOutlink?
					  NULL,
					  langIdArg);

	// put it back
	//newsr->m_spideredTime = saved;

	// bad news?
	if ( ufn < 0 ) {
		log("build: failed to get url filter for xmldoc %s",
		    m_firstUrl.getUrl());
		//g_errno = EBADENGINEER;
		//return NULL;
	}


	// store it
	m_urlFilterNum      = ufn;
	m_urlFilterNumValid = true;

	logTrace( g_conf.m_logTraceXmlDoc, "END. returning %" PRId32, m_urlFilterNum );

	return &m_urlFilterNum;
}

// . both "u" and "site" must not start with http:// or https:// or protocol
static bool isSiteRootFunc ( const char *u , const char *site ) {
	// get length of each
	int32_t slen = strlen(site);//m_siteLen;
	int32_t ulen = strlen(u);
	// "site" may or may not end in /, so remove that
	if ( site[slen-1] == '/' ) slen--;
	// same for url
	if ( u[ulen-1] == '/' ) ulen--;
	// skip http:// or https://
	if ( strncmp(u,"http://" ,7)==0 ) { u += 7; ulen -= 7; }
	if ( strncmp(u,"https://",8)==0 ) { u += 8; ulen -= 8; }
	if ( strncmp(site,"http://" ,7)==0 ) { site += 7; slen -= 7; }
	if ( strncmp(site,"https://",8)==0 ) { site += 8; slen -= 8; }
	// subtract default.asp etc. from "u"
	//if ( ulen > 15 && strncasecmp(u+ulen-11,"default.asp",11)==0 )
	//	ulen -= 11;
	//if ( ulen > 15 && strncasecmp(u+ulen-11,"default.html",12)==0 )
	//	ulen -= 12;
	//if ( ulen > 15 && strncasecmp(u+ulen-11,"index.html",10)==0 )
	//	ulen -= 10;
	// now they must match exactly
	if ( slen == ulen && ! strncmp ( site, u, ulen ) ) return true;
	// all done
	return false;
}

static bool isSiteRootFunc3 ( const char *u , int32_t siteRootHash32 ) {
	// get length of each
	int32_t ulen = strlen(u);
	// remove trailing /
	if ( u[ulen-1] == '/' ) ulen--;
	// skip http:// or https://
	if ( strncmp(u,"http://" ,7)==0 ) { u += 7; ulen -= 7; }
	if ( strncmp(u,"https://",8)==0 ) { u += 8; ulen -= 8; }
	// now they must match exactly
	int32_t sh32 = hash32(u,ulen);
	return ( sh32 == siteRootHash32 );
}

char *XmlDoc::getIsSiteRoot ( ) {
	if ( m_isSiteRootValid ) return &m_isSiteRoot2;
	// get our site
	char *site = getSite ();
	if ( ! site || site == (char *)-1 ) return (char *)site;
	// get our url without the http:// or https://
	const char *u = getFirstUrl()->getHost();
	if ( ! u ) {
		g_errno = EBADURL;
		return NULL;
	}
	// assume valid now
	m_isSiteRootValid = true;
	// get it
	bool isRoot = isSiteRootFunc ( u , site );
	// seems like https:://twitter.com/ is not getting set to root
	if ( m_firstUrl.getPathDepth(true) == 0  && ! m_firstUrl.isCgi() )
		isRoot = true;
	m_isSiteRoot2 = m_isSiteRoot = isRoot;
	return &m_isSiteRoot2;
}

int8_t *XmlDoc::getHopCount ( ) {
	// return now if valid
	if ( m_hopCountValid ) return &m_hopCount;

	setStatus ( "getting hop count" );

	// the unredirected url
	Url *f = getFirstUrl();
	// get url as string, skip "http://" or "https://"
	//char *u = f->getHost();
	// if we match site, we are a site root, so hop count is 0
	//char *isr = getIsSiteRoot();
	//if ( ! isr || isr == (char *)-1 ) return (int8_t *)isr;
	//if ( *isr ) {
	//	m_hopCount      = 0;
	//	m_hopCountValid = true;
	//	return &m_hopCount;
	//}
	// ping servers have 0 hop counts
	if ( f->isPingServer() ) {
		// log("xmldoc: hc2 is 0 (pingserver) %s",m_firstUrl.m_url);
		m_hopCount      = 0;
		m_hopCountValid = true;
		return &m_hopCount;
	}
	char *isRSS = getIsRSS();
	if ( ! isRSS || isRSS == (char *)-1) return (int8_t *)isRSS;
	// check for site root
	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 ) return (int8_t *)gr;
	// and site roots
	char *isSiteRoot = getIsSiteRoot();
	if (!isSiteRoot ||isSiteRoot==(char *)-1) return (int8_t *)isSiteRoot;
	if ( *isSiteRoot ) {
		// log("xmldoc: hc1 is 0 (siteroot) %s",m_firstUrl.m_url);
		m_hopCount      = 0;
		m_hopCountValid = true;
		return &m_hopCount;
	}
	// make sure m_minInlinkerHopCount is valid
	LinkInfo *info1 = getLinkInfo1();
	if ( ! info1 || info1 == (LinkInfo *)-1 ) return (int8_t *)info1;
	// . fix bad original hop counts
	// . assign this hop count from the spider rec
	int32_t origHopCount = -1;
	if ( m_sreqValid ) origHopCount = m_sreq.m_hopCount;
	// derive our hop count from our parent hop count
	int32_t hc = -1;
	// . BUT use inlinker if better
	// . if m_linkInfo1Valid is true, then m_minInlinkerHopCount is valid
	// if ( m_minInlinkerHopCount + 1 < hc && m_minInlinkerHopCount >= 0 )
	// 	hc = m_minInlinkerHopCount + 1;
	// or if parent is unknown, but we have a known inlinker with a
	// valid hop count, use the inlinker hop count then
	// if ( hc == -1 && m_minInlinkerHopCount >= 0 )
	// 	hc = m_minInlinkerHopCount + 1;
	// if ( origHopCount == 0 )
	// 	log("xmldoc: hc3 is 0 (spiderreq) %s",m_firstUrl.m_url);
	// or use our hop count from the spider rec if better
	if ( origHopCount < hc && origHopCount >= 0 )
		hc = origHopCount;
	// or if neither parent or inlinker was valid hop count
	if ( hc == -1 && origHopCount >= 0 )
		hc = origHopCount;
	// if we have no hop count at this point, i guess just pick 1!
	if ( hc == -1 )
		hc = 1;
	// truncate, hop count is only one byte in the TitleRec.h::m_hopCount
	if ( hc > 0x7f ) hc = 0x7f;

	// and now so do rss urls.
	if ( *isRSS && hc > 1 ) {
		// force it to one, not zero, otherwise it gets pounded
		// too hard on the aggregator sites. spider priority
		// is too high
		m_hopCount      = 1;
		m_hopCountValid = true;
		return &m_hopCount;
	}

	// unknown hop counts (-1) are propogated, except for root urls
	m_hopCountValid = true;
	m_hopCount      = hc;
	return &m_hopCount;
}

//set to false fo rinjecting and validate it... if &spiderlinks=0
// should we spider links?
char *XmlDoc::getSpiderLinks ( ) {
	// set it to false on issues
	//if ( m_indexCode ) {
	//	m_spiderLinks      = false;
	//	m_spiderLinks2     = false;
	//	m_spiderLinksValid = true ; }

	// this slows importing down because we end up doing ip lookups
	// for every outlink if "firstip" not in tagdb.
	// shoot. set2() already sets m_spiderLinksValid to true so we
	// have to override if importing.
	if ( m_isImporting && m_isImportingValid ) {
		m_spiderLinks  = false;
		m_spiderLinks2 = false;
		m_spiderLinksValid = true;
		return &m_spiderLinks2;
	}

	// return the valid value
	if ( m_spiderLinksValid ) return &m_spiderLinks2;

	setStatus ( "getting spider links flag");

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return (char *)cr;

	int32_t *ufn = getUrlFilterNum();
	if ( ! ufn || ufn == (void *)-1 ) return (char *)ufn;

	// if url filters forbids it
	if ( ! cr->m_harvestLinks[*ufn] ) {
		m_spiderLinksValid = true;
		m_spiderLinks2 = false;
		m_spiderLinks  = false;
		return &m_spiderLinks2;
	}

	// check the xml for a meta robots tag
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;

	// assume true
	m_spiderLinks = true;

	// or if meta tag says not to
	char buf1 [256];
	char buf2 [256];
	buf1[0] = '\0';
	buf2[0] = '\0';
	xml->getMetaContent ( buf1, 255 , "robots" , 6 );
	xml->getMetaContent ( buf2, 255 , g_conf.m_spiderBotName, strlen(g_conf.m_spiderBotName) );

	if ( strstr ( buf1 , "nofollow" ) ||
	     strstr ( buf2 , "nofollow" ) ||
	     strstr ( buf1 , "none"     ) ||
	     strstr ( buf2 , "none"     ) )
		m_spiderLinks = false;

	// spider links if not using robots.txt
	if ( ! m_useRobotsTxt )
		m_spiderLinks = true;

	// spider request forbade it? diffbot.cpp crawlbot api when
	// specifying urldata (list of urls to add to spiderdb) usually
	// they do not want the links crawled i'd imagine.
	if ( m_sreqValid && m_sreq.m_avoidSpiderLinks )
		m_spiderLinks = false;


	// also check in url filters now too


	// set shadow member
	m_spiderLinks2 = m_spiderLinks;
	// validate
	m_spiderLinksValid = true;
	return &m_spiderLinks2;
}

int32_t *XmlDoc::getSpiderPriority ( ) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	if ( m_priorityValid ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END. already valid: %" PRId32, m_priority );
		return &m_priority;
	}

	setStatus ("getting spider priority");
	// need tagrec to see if banned
	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 ) return (int32_t *)gr;
	// this is an automatic ban!
	if ( gr->getLong("manualban",0) ) {
		m_priority      = -3;//SPIDER_PRIORITY_BANNED;
		m_priorityValid = true;
		logTrace( g_conf.m_logTraceXmlDoc, "END. Manual ban" );
		return &m_priority;
	}
	int32_t *ufn = getUrlFilterNum();
	if ( ! ufn || ufn == (void *)-1 ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END. Invalid ufn" );
		return (int32_t *)ufn;
	}

	// sanity check
	if ( *ufn < 0 ) { g_process.shutdownAbort(true); }
	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END. No collection" );
		return NULL;
	}

	m_priority = cr->m_spiderPriorities[*ufn];

	// continue to use -3 to indicate SPIDER_PRIORITY_FILTERED for now
	if ( cr->m_forceDelete[*ufn] ) {
		logTrace( g_conf.m_logTraceXmlDoc, "force delete" );
		m_priority = -3;
	}

	logTrace( g_conf.m_logTraceXmlDoc, "END. ufn=%" PRId32" priority=%" PRId32, *ufn, m_priority );
	m_priorityValid = true;
	return &m_priority;
}

bool XmlDoc::logIt (SafeBuf *bb ) {
	// set errCode
	int32_t errCode = m_indexCode;
	if ( ! errCode && g_errno ) {
		errCode = g_errno;
	}

	// were we new?
	bool isNew = true;
	if ( m_sreqValid && m_sreq.m_hadReply ) isNew = false;

	// download time
	unsigned took = 0;
	if ( m_downloadStartTimeValid ) {
		if ( m_downloadEndTimeValid ) {
			took = static_cast<unsigned>( m_downloadEndTime - m_downloadStartTime );
		} else {
			took = static_cast<unsigned>( gettimeofdayInMilliseconds() - m_downloadStartTime );
		}
	}

	// keep track of stats
	Statistics::register_spider_time( isNew, errCode, m_httpStatus, took );

	// do not log if we should not, saves some time
	if ( ! g_conf.m_logSpideredUrls ) return true;

	const char *coll = "nuked";
	CollectionRec *cr = getCollRec();
	if ( cr ) coll = cr->m_coll;

	SafeBuf tmpsb;

	// print into this now
	SafeBuf *sb = &tmpsb;
	// log into provided safebuf if not null
	if ( bb ) sb = bb;

	//
	// coll
	//
	sb->safePrintf("coll=%s ",coll);
	sb->safePrintf("collnum=%" PRId32" ",(int32_t)m_collnum);

	//
	// print ip
	//
	if ( m_ipValid )
		sb->safePrintf("ip=%s ",iptoa(m_ip) );

	if ( m_firstIpValid )
		sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );

	// . first ip from spider req if it is fake
	// . we end up spidering the same url twice because it will have
	//   different "firstips" in the SpiderRequest key. maybe just
	//   use domain hash instead of firstip, and then let msg13
	//   make queues in the case of hammering an ip, which i think
	//   it already does...
#ifdef _VALGRIND_
	if(m_sreqValid)
		VALGRIND_CHECK_MEM_IS_DEFINED(&m_sreq.m_firstIp,sizeof(m_sreq.m_firstIp));
	if(m_firstIpValid)
		VALGRIND_CHECK_MEM_IS_DEFINED(&m_firstIp,sizeof(m_firstIp));
#endif

	if ( m_sreqValid && m_firstIpValid && m_sreq.m_firstIp != m_firstIp )
		sb->safePrintf("fakesreqfirstip=%s ",iptoa(m_sreq.m_firstIp) );

	//
	// print when this spider request was added
	//
	//if ( m_sreqValid && m_sreq.m_addedTime ) {
	//	struct tm *timeStruct = gmtime_r( &m_sreq.m_addedTime );
	//	char tmp[64];
	//	strftime(tmp,64,"requestadded=%b-%d-%Y(%H:%M:%S)", timeStruct);
	//	sb->safePrintf("%s(%" PRIu32") ",tmp,m_sreq.m_addedTime);
	//}

	//
	// print spidered time
	//
	//if ( m_spideredTimeValid ) {
	time_t spideredTime = (time_t)getSpideredTime();
	struct tm tm_buf;
	struct tm *timeStruct = gmtime_r(&spideredTime,&tm_buf);
	char tmp[64];
	strftime(tmp,64,"spidered=%b-%d-%Y(%H:%M:%S)", timeStruct );
	sb->safePrintf("%s(%" PRIu32") ",tmp,(uint32_t)spideredTime);

	// when it was scheduled to be spidered
	if ( m_sreqValid && m_sreq.m_addedTime ) {
		time_t ts = m_sreq.m_addedTime;
		struct tm *timeStruct = gmtime_r(&ts,&tm_buf);
		char tmp[64];
		strftime ( tmp , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct );
		sb->safePrintf("scheduledtime=%s(%" PRIu32") ",
			       tmp,(uint32_t)m_sreq.m_addedTime);
	}

	// discovery date, first time spiderrequest was added to spiderdb
	if ( m_sreqValid && m_sreq.m_discoveryTime ) {
		time_t ts = m_sreq.m_discoveryTime;
		struct tm *timeStruct = gmtime_r(&ts,&tm_buf);
		char tmp[64];
		strftime ( tmp , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct );
		sb->safePrintf("discoverydate=%s(%" PRIu32") ",
			       tmp,(uint32_t)m_sreq.m_discoveryTime);
	}

	// print first indexed time
	if ( m_firstIndexedDateValid ) {
		time_t ts = m_firstIndexedDate;
		timeStruct = gmtime_r(&ts,&tm_buf);//m_firstIndexedDate );
		strftime(tmp,64,"firstindexed=%b-%d-%Y(%H:%M:%S)", timeStruct);
		sb->safePrintf("%s(%" PRIu32") ",tmp,
			       (uint32_t)m_firstIndexedDate);
	}


	//if ( ! m_isIndexedValid ) { g_process.shutdownAbort(true); }

	// just use the oldurlfilternum for grepping i guess
	//if ( m_oldDocValid && m_oldDoc )

	// when injecting a request we have no idea if it had a reply or not
	if ( m_sreqValid && m_sreq.m_isInjecting )
		sb->safePrintf("firsttime=? ");
	else if ( m_sreqValid && m_sreq.m_hadReply )
		sb->safePrintf("firsttime=0 ");
	else if ( m_sreqValid )
		sb->safePrintf("firsttime=1 ");
	else
		sb->safePrintf("firsttime=? ");

	//
	// print # of link texts
	//
	if ( m_linkInfo1Valid && ptr_linkInfo1 ) {
		LinkInfo *info = ptr_linkInfo1;
		int32_t nt = info->getNumLinkTexts();
		sb->safePrintf("goodinlinks=%" PRId32" ",nt );
		// new stuff. includes ourselves i think.
		//sb->safePrintf("ipinlinks=%" PRId32" ",info->m_numUniqueIps);
		//sb->safePrintf("cblockinlinks=%" PRId32" ",
		//info->m_numUniqueCBlocks);
	}

	if (  m_docIdValid )
		sb->safePrintf("docid=%" PRIu64" ",m_docId);

	char *u = getFirstUrl()->getUrl();
	int64_t pd = Titledb::getProbableDocId(u);
	int64_t d1 = Titledb::getFirstProbableDocId ( pd );
	int64_t d2 = Titledb::getLastProbableDocId  ( pd );
	sb->safePrintf("probdocid=%" PRIu64" ",pd);
	sb->safePrintf("probdocidmin=%" PRIu64" ",d1);
	sb->safePrintf("probdocidmax=%" PRIu64" ",d2);
	sb->safePrintf("usetimeaxis=%i ",(int)m_useTimeAxis);

	if ( m_siteNumInlinksValid ) {
		sb->safePrintf("siteinlinks=%04" PRId32" ",m_siteNumInlinks );
		int32_t sr = ::getSiteRank ( m_siteNumInlinks );
		sb->safePrintf("siterank=%" PRId32" ", sr );
	}

	if ( m_sreqValid )
		sb->safePrintf("pageinlinks=%04" PRId32" ",
			       m_sreq.m_pageNumInlinks);

	// shortcut
	int64_t uh48 = hash64b ( m_firstUrl.getUrl() );
	// mask it
	uh48 &= 0x0000ffffffffffffLL;
	sb->safePrintf ("uh48=%" PRIu64" ",uh48 );


	if ( m_charsetValid )
		sb->safePrintf("charset=%s ",get_charset_str(m_charset));

	if ( m_contentTypeValid )
		sb->safePrintf("ctype=%s ",
			      g_contentTypeStrings [m_contentType]);

	if ( m_langIdValid ) {
		sb->safePrintf( "lang=%02" PRId32"(%s) ", ( int32_t ) m_langId, getLanguageAbbr( m_langId ) );
	}

	if ( m_countryIdValid )
		sb->safePrintf("country=%02" PRId32"(%s) ",(int32_t)m_countryId,
			      g_countryCode.getAbbr(m_countryId));

	if ( m_hopCountValid )
		sb->safePrintf("hopcount=%02" PRId32" ",(int32_t)m_hopCount);


	if ( m_contentValid )
		sb->safePrintf("contentlen=%06" PRId32" ",m_contentLen);

    if ( m_isContentTruncatedValid )
		sb->safePrintf("contenttruncated=%" PRId32" ",(int32_t)m_isContentTruncated);

	if ( m_robotsTxtLenValid )
		sb->safePrintf("robotstxtlen=%04" PRId32" ",m_robotsTxtLen );

	if ( m_isAllowedValid )
		sb->safePrintf("robotsallowed=%i ", (int)m_isAllowed);
	else
		sb->safePrintf("robotsallowed=? " );

	if ( m_contentHash32Valid )
		sb->safePrintf("ch32=%010" PRIu32" ",m_contentHash32);

	if ( m_domHash32Valid )
		sb->safePrintf("dh32=%010" PRIu32" ",m_domHash32);

	if ( m_siteHash32Valid )
		sb->safePrintf("sh32=%010" PRIu32" ",m_siteHash32);

	if ( m_isPermalinkValid )
		sb->safePrintf("ispermalink=%" PRId32" ",(int32_t)m_isPermalink);

	if ( m_isRSSValid )
		sb->safePrintf("isrss=%" PRId32" ",(int32_t)m_isRSS);

	if ( m_linksValid )
		sb->safePrintf("hasrssoutlink=%" PRId32" ",
			      (int32_t)m_links.hasRSSOutlink() );

	if ( m_numOutlinksAddedValid ) {
		sb->safePrintf("outlinksadded=%04" PRId32" ",
			       (int32_t)m_numOutlinksAdded);
	}

	if ( m_metaListValid )
		sb->safePrintf("addlistsize=%05" PRId32" ",
			       (int32_t)m_metaListSize);
	else
		sb->safePrintf("addlistsize=%05" PRId32" ",(int32_t)0);

	if ( m_addedSpiderRequestSizeValid )
		sb->safePrintf("addspiderreqsize=%05" PRId32" ",
			       m_addedSpiderRequestSize);
	else
		sb->safePrintf("addspiderreqsize=%05" PRId32" ",0);


	if ( m_addedSpiderReplySizeValid )
		sb->safePrintf("addspiderrepsize=%05" PRId32" ",
			       m_addedSpiderReplySize);
	else
		sb->safePrintf("addspiderrepsize=%05" PRId32" ",0);


	if ( m_addedStatusDocSizeValid )
		sb->safePrintf("addstatusdocsize=%05" PRId32" ",
			       m_addedStatusDocSize);
	else
		sb->safePrintf("addstatusdocsize=%05" PRId32" ",0);


	if ( m_useSecondaryRdbs ) {
		sb->safePrintf("useposdb=%i ",(int)m_usePosdb);
		sb->safePrintf("usetitledb=%i ",(int)m_useTitledb);
		sb->safePrintf("useclusterdb=%i ",(int)m_useClusterdb);
		sb->safePrintf("usespiderdb=%i ",(int)m_useSpiderdb);
		sb->safePrintf("uselinkdb=%i ",(int)m_useLinkdb);
		if ( cr )
			sb->safePrintf("indexspiderreplies=%i ",(int)
				       cr->m_indexSpiderReplies);
	}

	if ( m_imageDataValid && size_imageData ) {
		// url is in data now
		ThumbnailArray *ta = (ThumbnailArray *)ptr_imageData;
		int32_t nt = ta->getNumThumbnails();
		ThumbnailInfo *ti = ta->getThumbnailInfo(0);
		sb->safePrintf("thumbnail=%s,%" PRId32"bytes,%" PRId32"x%" PRId32",(%" PRId32") ",
			      ti->getUrl(),
			      ti->m_dataSize,
			      ti->m_dx,
			      ti->m_dy,
			      nt);
	}
	else
		sb->safePrintf("thumbnail=none ");


	if ( m_rawUtf8ContentValid )
		sb->safePrintf("utf8size=%" PRId32" ",
			       m_rawUtf8ContentSize);
	if ( m_utf8ContentValid )
		sb->safePrintf("rawutf8size=%" PRId32" ",
			       size_utf8Content);

	// get the content type
	uint8_t ct = CT_UNKNOWN;
	if ( m_contentTypeValid ) ct = m_contentType;

	bool isRoot = false;
	if ( m_isSiteRootValid ) isRoot = m_isSiteRoot;

	// make sure m_minInlinkerHopCount is valid
	LinkInfo *info1 = NULL;
	if ( m_linkInfo1Valid ) info1 = ptr_linkInfo1;


	// hack this kinda
	// . in PageInject.cpp we do not have a valid priority without
	//   blocking because we did a direct injection!
	//   so ignore this!!
	// . a diffbot json object, an xmldoc we set from a json object
	//   in a diffbot reply, is a childDoc (m_isChildDoc) is true
	//   and does not have a spider priority. only the parent doc
	//   that we used to get the diffbot reply (array of json objects)
	//   will have the spider priority
	if ( ! getIsInjecting() ) {
		//int32_t *priority = getSpiderPriority();
		//if ( ! priority ||priority==(void *)-1){g_process.shutdownAbort(true);}
		if ( m_priorityValid )
			sb->safePrintf("priority=%" PRId32" ",
				      (int32_t)m_priority);
	}

	// should be valid since we call getSpiderPriority()
	if ( m_urlFilterNumValid )
		sb->safePrintf("urlfilternum=%" PRId32" ",(int32_t)m_urlFilterNum);


	if ( m_siteValid )
		sb->safePrintf("site=%s ",ptr_site);

	if ( m_isSiteRootValid )
		sb->safePrintf("siteroot=%" PRId32" ",m_isSiteRoot );
	else
		sb->safePrintf("siteroot=? ");

	// like how we index it, do not include the filename. so we can
	// have a bunch of pathdepth 0 urls with filenames like xyz.com/abc.htm
	if ( m_firstUrlValid && m_firstUrl.getUrl() && m_firstUrl.getUrlLen() >= 3 ) {
		int32_t pd = m_firstUrl.getPathDepth(false);
		sb->safePrintf("pathdepth=%" PRId32" ",pd);
	}
	else {
		sb->safePrintf("pathdepth=? ");
	}

	//
	// . sometimes we print these sometimes we do not
	// . put this at the end so we can awk out the above fields reliably
	//

	// print when it was last spidered
	if ( m_oldDocValid && m_oldDoc ) {
		time_t spideredTime = m_oldDoc->getSpideredTime();
		struct tm *timeStruct = gmtime_r(&spideredTime,&tm_buf);
		char tmp[64];
		strftime(tmp,64,"lastindexed=%b-%d-%Y(%H:%M:%S)",timeStruct);
		sb->safePrintf("%s(%" PRIu32") ", tmp,(uint32_t)spideredTime);
	}

	if ( m_linkInfo1Valid && ptr_linkInfo1 && ptr_linkInfo1->hasRSSItem())
		sb->safePrintf("hasrssitem=1 ");

	// was the content itself injected?
	if ( m_wasContentInjected )
		sb->safePrintf("contentinjected=1 ");
	else
		sb->safePrintf("contentinjected=0 ");

	// might have just injected the url and downloaded the content?
	if ( (m_sreqValid && m_sreq.m_isInjecting) ||
	     (m_isInjecting && m_isInjectingValid) )
		sb->safePrintf("urlinjected=1 ");
	else
		sb->safePrintf("urlinjected=0 ");

	if ( m_sreqValid && m_sreq.m_isAddUrl )
		sb->safePrintf("isaddurl=1 ");
	else
		sb->safePrintf("isaddurl=0 ");

	if ( m_sreqValid && m_sreq.m_isPageReindex )
		sb->safePrintf("pagereindex=1 ");

	if ( m_spiderLinksValid && m_spiderLinks )
		sb->safePrintf("spiderlinks=1 ");
	if ( m_spiderLinksValid && ! m_spiderLinks )
		sb->safePrintf("spiderlinks=0 ");


	if ( m_crawlDelayValid && m_crawlDelay != -1 )
		sb->safePrintf("crawldelayms=%" PRId32" ",(int32_t)m_crawlDelay);

	if ( m_recycleContent )
		sb->safePrintf("recycleContent=1 ");

	if ( m_exactContentHash64Valid )
		sb->safePrintf("exactcontenthash=%" PRIu64" ",
			      m_exactContentHash64 );

	// . print percent changed
	// . only print if non-zero!
	if ( m_percentChangedValid && m_oldDocValid && m_oldDoc &&
	     m_percentChanged )
		sb->safePrintf("changed=%.00f%% ",m_percentChanged);

	// only print if different now! good for grepping changes
	if ( m_oldDocValid && m_oldDoc && m_oldDoc->m_docId != m_docId )
		sb->safePrintf("olddocid=%" PRIu64" ",m_oldDoc->m_docId);

	// only print if different now! good for grepping changes
	if ( m_sreqValid && m_sreq.m_ufn >= 0 &&
	     (!m_urlFilterNumValid || m_sreq.m_ufn != m_urlFilterNum) )
		sb->safePrintf("oldurlfilternum=%" PRId32" ",
			      (int32_t)m_sreq.m_ufn);

	if ( m_sreqValid && m_sreq.m_priority >= 0 &&
	     (!m_priorityValid || m_sreq.m_priority != m_priority) )
		sb->safePrintf("oldpriority=%" PRId32" ",
			      (int32_t)m_sreq.m_priority);

	if ( m_oldDoc && m_oldDoc->m_langIdValid &&
	     (!m_langIdValid || m_oldDoc->m_langId != m_langId) )
		sb->safePrintf("oldlang=%02" PRId32"(%s) ",(int32_t)m_oldDoc->m_langId,
			      getLanguageAbbr(m_oldDoc->m_langId));

	if ( m_useSecondaryRdbs &&
	     m_useTitledb &&
	     (!m_langIdValid || m_logLangId != m_langId) )
		sb->safePrintf("oldlang=%02" PRId32"(%s) ",(int32_t)m_logLangId,
			      getLanguageAbbr(m_logLangId));

	if ( m_useSecondaryRdbs &&
	     m_useTitledb &&
	     m_logSiteNumInlinks != m_siteNumInlinks )
		sb->safePrintf("oldsiteinlinks=%04" PRId32" ",m_logSiteNumInlinks);

	if ( m_useSecondaryRdbs &&
	     m_useTitledb &&
	     m_oldDocValid &&
	     m_oldDoc &&
	     strcmp(ptr_site,m_oldDoc->ptr_site) )
		sb->safePrintf("oldsite=%s ",m_oldDoc->ptr_site);

	if ( m_isAdultValid )
		sb->safePrintf("isadult=%" PRId32" ",(int32_t)m_isAdult);

	// only print if different now! good for grepping changes
	if ( m_oldDocValid && m_oldDoc &&
	     m_oldDoc->m_siteNumInlinks >= 0 &&
	     m_oldDoc->m_siteNumInlinks != m_siteNumInlinks ) {
		int32_t sni = -1;
		if  ( m_oldDoc ) sni = m_oldDoc->m_siteNumInlinks;
		sb->safePrintf("oldsiteinlinks=%04" PRId32" ",sni);
	}


	// Spider.cpp sets m_sreq.m_errCount before adding it to doledb
	if ( m_sreqValid ) // && m_sreq.m_errCount )
		sb->safePrintf("errcnt=%" PRId32" ",(int32_t)m_sreq.m_errCount );
	else
		sb->safePrintf("errcnt=? ");

	if ( ptr_redirUrl ) { // m_redirUrlValid && m_redirUrlPtr ) {
		sb->safePrintf("redir=%s ",ptr_redirUrl);//m_redirUrl.getUrl());
		if ( m_numRedirects > 2 )
			sb->safePrintf("numredirs=%" PRId32" ",m_numRedirects);
	}

	if ( m_canonicalRedirUrlValid && m_canonicalRedirUrlPtr )
		sb->safePrintf("canonredir=%s ",
			       m_canonicalRedirUrlPtr->getUrl());

	if ( m_httpStatusValid && m_httpStatus != 200 )
		sb->safePrintf("httpstatus=%" PRId32" ",(int32_t)m_httpStatus);

	if ( m_updatedMetaData )
		sb->safePrintf("updatedmetadata=1 ");

	if ( m_isDupValid && m_isDup )
		sb->safePrintf("dupofdocid=%" PRId64" ",m_docIdWeAreADupOf);

	if ( m_firstUrlValid )
		sb->safePrintf("url=%s ",m_firstUrl.getUrl());
	else
		sb->safePrintf("urldocid=%" PRId64" ",m_docId);

	//
	// print error/status
	//
	sb->safePrintf(": %s",mstrerror(m_indexCode));

	// if safebuf provided, do not log to log
	if ( bb ) return true;

	// log it out
	logf ( LOG_INFO ,
	       "build: %s",
	       //getFirstUrl()->getUrl(),
	       sb->getBufStart() );

	return true;
}


// . returns false and sets g_errno on error
// . make sure that the title rec we generated creates the exact same
//   meta list as what we got
bool XmlDoc::doConsistencyTest ( bool forceTest ) {

	// skip for now it was coring on a json doc test
	return true;
#if 0
	CollectionRec *cr = getCollRec();
	if ( ! cr )
		return true;

	if ( ! m_doConsistencyTesting )
		return true;

	// if we had an old doc then our meta list will have removed
	// stuff already in the database from indexing the old doc.
	// so it will fail the parsing consistency check... because of
	// the 'incremental indexing' algo above
	// disable for now... just a secondfor testing cheatcc.com
	if ( m_oldDoc && m_oldDocValid && g_conf.m_doIncrementalUpdating )
		return true;

	// if not test coll skip this
	//if ( strcmp(cr->m_coll,"qatest123") ) return true;

	// title rec is null if we are reindexing an old doc
	// and "unchanged" was true.
	if ( m_unchangedValid && m_unchanged ) {
		if ( ! m_titleRecBufValid ) return true;
		if ( m_titleRecBuf.length()==0 ) return true;
	}

	// leave this uncommented so we can see if we are doing it
	setStatus ( "doing consistency check" );

	// log debug
	log("spider: doing consistency check for %s",ptr_firstUrl);

	// . set another doc from that title rec
	// . do not keep on stack since so huge!
	XmlDoc *doc ;
	try { doc = new ( XmlDoc ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return false;
	}
	mnew ( doc , sizeof(XmlDoc),"xmldcs");


	if ( ! doc->set2 ( m_titleRecBuf.getBufStart() ,
			   -1 , cr->m_coll , NULL , m_niceness ,
			  // no we provide the same SpiderRequest so that
			  // it can add the same SpiderReply to the metaList
			   &m_sreq ) ) {
		mdelete ( doc , sizeof(XmlDoc) , "xdnuke");
		delete ( doc );
		return false;
	}

	// . some hacks
	// . do not look up title rec in titledb, assume it is new
	doc->m_isIndexed      = false;
	doc->m_isIndexedValid = true;

	// so we don't core in getRevisedSpiderRequest()
	doc->m_firstIp = m_firstIp;
	doc->m_firstIpValid = true;

	// getNewSpiderReply() calls getDownloadEndTime() which is not valid
	// and causes the page to be re-downloaded, so stop that..!
	doc->m_downloadEndTime      = m_downloadEndTime;
	doc->m_downloadEndTimeValid = true;

	// inherit doledb key as well to avoid a core there
	doc->m_doledbKey = m_doledbKey;

	// flag it
	doc->m_doingConsistencyCheck = true;

	// get get its metalist. rv = return value
	char *rv = doc->getMetaList ( );

	// sanity check - compare urls
	if ( doc->m_firstUrl.getUrlLen() != m_firstUrl.getUrlLen()){g_process.shutdownAbort(true);}

	// error setting it?
	if ( ! rv ) {
		// sanity check
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		// free it
		mdelete ( doc , sizeof(XmlDoc) , "xdnuke");
		delete ( doc );
		// error
		return false;
	}
	// blocked? that is not allowed
	if ( rv == (void *)-1 ) { g_process.shutdownAbort(true); }

	// compare with the old list
	char *list1     = m_metaList;
	int32_t  listSize1 = m_metaListSize;

	char *list2     = doc->m_metaList;
	int32_t  listSize2 = doc->m_metaListSize;

	// do a compare
	HashTableX ht1;
	HashTableX ht2;

	ht1.set ( sizeof(key224_t),sizeof(char *),
		  262144,NULL,0,false,m_niceness,"xmlht1");
	ht2.set ( sizeof(key224_t),sizeof(char *),
		  262144,NULL,0,false,m_niceness,"xmlht2");

	// format of a metalist... see XmlDoc::addTable() where it adds keys
	// from a table into the metalist
	// <nosplitflag|rdbId><key><dataSize><data>
	// where nosplitflag is 0x80
	char *p1    = list1;
	char *p2    = list2;
	char *pend1 = list1 + listSize1;
	char *pend2 = list2 + listSize2;

	// see if each key in list1 is in list2
	if ( ! hashMetaList ( &ht1 , p1 , pend1 , false ) ) {
		g_process.shutdownAbort(true);
		mdelete ( doc , sizeof(XmlDoc) , "xdnuke");
		delete ( doc );
		log(LOG_WARN, "doc: failed consistency test for %s",ptr_firstUrl);
		return false;
	}
	if ( ! hashMetaList ( &ht2 , p2 , pend2 , false ) ) {
		g_process.shutdownAbort(true);
		mdelete ( doc , sizeof(XmlDoc) , "xdnuke");
		delete ( doc );
		log(LOG_WARN, "doc: failed consistency test for %s",ptr_firstUrl);
		return false;
	}

	// . now make sure each list matches the other
	// . first scan the guys in "p1" and make sure in "ht2"
	hashMetaList ( &ht2 , p1 , pend1 , true );
	// . second scan the guys in "p2" and make sure in "ht1"
	hashMetaList ( &ht1 , p2 , pend2 , true );

	mdelete ( doc , sizeof(XmlDoc) , "xdnuke");
	delete ( doc );

	log ("spider: passed consistency test for %s",ptr_firstUrl );

	// no serious error, although there might be an inconsistency
	return true;
#endif
}

#define TABLE_ROWS 25

// print this also for page parser output!
void XmlDoc::printMetaList ( char *p , char *pend , SafeBuf *sb ) {

	verifyMetaList ( p , pend , false );

	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	const char *hdr =
		"<table border=1>\n"
		"<tr>"
		"<td><b>rdb</b></td>"
		"<td><b>del?</b></td>"
		"<td><b>shardByTermId?</b></td>"
		// illustrates key size
		"<td><b>key</b></td>"
		// break it down. based on rdb, of course.
		"<td><b>desc</b></td>"
		"</tr>\n" ;

	sb->safePrintf("%s",hdr);

	int32_t recSize = 0;
	int32_t rcount = 0;
	for ( ; p < pend ; p += recSize ) {
		// get rdbid
		rdbid_t rdbId = (rdbid_t)(*p & 0x7f);
		// skip
		p++;
		// get key size
		int32_t ks = getKeySizeFromRdbId ( rdbId );
		// point to it
		char *rec = p;
		// init this
		int32_t recSize = ks;
		// convert into a key128_t, the biggest possible key
		//key224_t k ;
		char k[MAX_KEY_BYTES];
		if ( ks > MAX_KEY_BYTES ) { g_process.shutdownAbort(true); }
		//k.setMin();
		gbmemcpy ( &k , p , ks );
		// is it a negative key?
		char neg = false;
		if ( ! ( p[0] & 0x01 ) ) neg = true;
		// this is now a bit in the posdb key so we can rebalance
		char shardByTermId = false;
		if ( rdbId==RDB_POSDB && Posdb::isShardedByTermId(k))
			shardByTermId = true;
		// skip it
		p += ks;
		// get datasize
		int32_t dataSize = getDataSizeFromRdbId ( rdbId );
		// . always zero if key is negative
		// . this is not the case unfortunately...
		if ( neg ) dataSize = 0;
		// if -1, read it in
		if ( dataSize == -1 ) {
			dataSize = *(int32_t *)p;
			// inc this
			recSize += 4;
			// sanity check
                        if ( dataSize < 0 ) { g_process.shutdownAbort(true); }
			p += 4;
                }
		// point to it
		char *data = p;
		// skip the data
		p += dataSize;
		// inc it
		recSize += dataSize;
		// NULL it for negative keys
		if ( dataSize == 0 ) data = NULL;

		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS) == 0 )
			sb->safePrintf("<!--ignore--></table>%s",hdr);


		//if ( rdbId != RDB_LINKDB ) continue;

		// print dbname
		sb->safePrintf("<tr>");
		const char *dn = getDbnameFromId ( rdbId );
		sb->safePrintf("<td>%s</td>",dn);

		if ( neg ) sb->safePrintf("<td>D</td>");
		else       sb->safePrintf("<td>&nbsp;</td>");

		if ( shardByTermId ) sb->safePrintf("<td>shardByTermId</td>");
		else           sb->safePrintf("<td>&nbsp;</td>");

		sb->safePrintf("<td><nobr>%s</nobr></td>", KEYSTR(k,ks));



		if ( rdbId == RDB_POSDB ) {
			// get termid et al
			key144_t *k2 = (key144_t *)k;
			int64_t tid = Posdb::getTermId(k2);
			//uint8_t score8 = Posdb::getScore ( *k2 );
			//uint32_t score32 = score8to32 ( score8 );
			// sanity check
			if(dataSize!=0){g_process.shutdownAbort(true);}
			sb->safePrintf("<td>"
				       "termId=%020" PRIu64" "
				       //"score8=%03" PRIu32" "
				       //"score32=%010" PRIu32
				       "</td>"
				       ,(uint64_t)tid
				       //(int32_t)score8,
				       //(int32_t)score32
				       );
		}
		else if ( rdbId == RDB_LINKDB ) {
			key224_t *k2 = (key224_t *)k;
			int64_t linkHash=Linkdb::getLinkeeUrlHash64_uk(k2);
			int32_t linkeeSiteHash  = Linkdb::getLinkeeSiteHash32_uk(k2);
			int32_t linkerSiteHash  = Linkdb::getLinkerSiteHash32_uk(k2);
			char linkSpam   = Linkdb::isLinkSpam_uk    (k2);
			int32_t siteRank = Linkdb::getLinkerSiteRank_uk (k2);
			//int32_t hopCount = Linkdb::getLinkerHopCount_uk   (k2);
			//int32_t ip24     = Linkdb::getLinkerIp24_uk       (k2);
			int32_t ip32       = Linkdb::getLinkerIp_uk       (k2);
			int64_t docId = Linkdb::getLinkerDocId_uk      (k2);
			// sanity check
			if(dataSize!=0){g_process.shutdownAbort(true);}
			sb->safePrintf("<td>"
				       "<nobr>"
				       "linkeeSiteHash32=0x%08" PRIx32" "
				       "linkeeUrlHash=0x%016" PRIx64" "
				       "linkSpam=%" PRId32" "
				       "siteRank=%" PRId32" "
				       //"hopCount=%03" PRId32" "
				       "sitehash32=0x%" PRIx32" "
				       "IP32=%s "
				       "docId=%" PRIu64
				       "</nobr>"
				       "</td>",
				       linkeeSiteHash,
				       linkHash,
				       (int32_t)linkSpam,
				       siteRank,
				       //hopCount,
				       linkerSiteHash,
				       iptoa(ip32),
				       docId);

		}
		else if ( rdbId == RDB_CLUSTERDB ) {
			key128_t *k2 = (key128_t *)k;
			char *r = (char *)k2;
			int32_t siteHash26 = g_clusterdb.getSiteHash26   ( r );
			char lang       = g_clusterdb.getLanguage     ( r );
			int64_t docId = g_clusterdb.getDocId        ( r );
			char ff         = g_clusterdb.getFamilyFilter ( r );
			// sanity check
			if(dataSize!=0){g_process.shutdownAbort(true);}
			sb->safePrintf("<td>"
				       // 26 bit site hash
				       "siteHash26=0x%08" PRIx32" "
				       "family=%" PRId32" "
				       "lang=%03" PRId32" "
				       "docId=%" PRIu64
				       "</td>",
				       siteHash26 ,
				       (int32_t)ff,
				       (int32_t)lang,
				       docId );
		}
		// key parsing logic taken from Address::makePlacedbKey
		else if ( rdbId == RDB_SPIDERDB ) {
			sb->safePrintf("<td><nobr>");
			key128_t *k2 = (key128_t *)k;
			if ( g_spiderdb.isSpiderRequest(k2) ) {
				SpiderRequest *sreq = (SpiderRequest *)rec;
				sreq->print ( sb );
			}
			else {
				SpiderReply *srep = (SpiderReply *)rec;
				srep->print ( sb );
			}
			sb->safePrintf("</nobr></td>");
		}
		else if ( rdbId == RDB_DOLEDB ) {
			key96_t *k2 = (key96_t *)k;
			sb->safePrintf("<td><nobr>");
			sb->safePrintf("priority=%" PRId32" "
				       "spidertime=%" PRIu32" "
				       "uh48=%" PRIx64" "
				       "isdel=%" PRId32,
				       g_doledb.getPriority(k2),
				       (uint32_t)g_doledb.getSpiderTime(k2),
				       g_doledb.getUrlHash48(k2),
				       g_doledb.getIsDel(k2));
			sb->safePrintf("</nobr></td>");
		}
		else if ( rdbId == RDB_TITLEDB ) {
			// print each offset and size for the variable crap
			sb->safePrintf("<td><nobr>titlerec datasize=%" PRId32" "
				       //"sizeofxmldoc=%" PRId32" "
				       //"hdrSize=%" PRId32" "
				       //"version=%" PRId32" "
				       //"%s"
				       "</nobr></td>",
				       dataSize
				       //(int32_t)sizeof(XmlDoc),
				       //(int32_t)tr.m_headerSize,
				       //(int32_t)tr.m_version,
				       //tmp.getBufStart());
				       );
		}
		else if ( rdbId == RDB_TAGDB ) {
			Tag *tag = (Tag *)rec;
			sb->safePrintf("<td><nobr>");
			if ( rec[0] & 0x01 ) tag->printToBuf(sb);
			else sb->safePrintf("negativeTagKey");
			sb->safePrintf("</nobr></td>");
		}
		else {
			g_process.shutdownAbort(true);
		}

		// close it up
		sb->safePrintf("</tr>\n");
	}
	sb->safePrintf("</table>\n");

	if ( sb == &tmp )
		sb->print();
}


bool XmlDoc::verifyMetaList ( char *p , char *pend , bool forDelete ) {
	return true;

#if 0
	CollectionRec *cr = getCollRec();
	if ( ! cr ) return true;

	// do not do this if not test collection for now
	if ( strcmp(cr->m_coll,"qatest123") ) return true;


	log(LOG_DEBUG, "xmldoc: VERIFYING METALIST");

	// store each record in the list into the send buffers
	for ( ; p < pend ; ) {
		// first is rdbId
		//char rdbId = -1; // m_rdbId;
		//if ( rdbId < 0 ) rdbId = *p++;
		rdbid_t rdbId = (rdbid_t)(*p++ & 0x7f);

		// negative key?
		bool del = !( *p & 0x01 );

		// must always be negative if deleteing
		// spiderdb is exempt because we add a spiderreply that is
		// positive and a spiderdoc
		// no, this is no longer the case because we add spider
		// replies to the index when deleting or rejecting a doc.
		//if ( m_deleteFromIndex && ! del && rdbId != RDB_SPIDERDB) {
		//	g_process.shutdownAbort(true); }

		// get the key size. a table lookup in Rdb.cpp.
		int32_t ks = getKeySizeFromRdbId ( rdbId );
		if ( rdbId == RDB_POSDB || rdbId == RDB2_POSDB2 ) {
			// no compress bits set!
			if ( p[0] & 0x06 ) { g_process.shutdownAbort(true); }
			// alignment bit set or cleared
			if ( ! ( p[1] & 0x02 ) ) { g_process.shutdownAbort(true); }
			if (   ( p[7] & 0x02 ) ) { g_process.shutdownAbort(true); }
			int64_t docId = Posdb::getDocId(p);
			if ( docId != m_docId && !cr->m_indexSpiderReplies) {
				log( LOG_WARN, "xmldoc: %" PRId64" != %" PRId64, docId, m_docId );
				g_process.shutdownAbort(true);
			}
		}

		// sanity
		if ( ks < 12 ) { g_process.shutdownAbort(true); }
		if ( ks > MAX_KEY_BYTES ) { g_process.shutdownAbort(true); }
		// another check
		Rdb *rdb = getRdbFromId(rdbId);
		if ( ! rdb ) { g_process.shutdownAbort(true); }
		if ( rdb->m_ks < 12 || rdb->m_ks > MAX_KEY_BYTES ) {
			g_process.shutdownAbort(true);}

		char *rec = p;

		// set this
		//bool split = true;
		//if(rdbId == RDB_POSDB && Posdb::isShardedByTermId(p) )
		// split =false;
		// skip key
		p += ks;
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		//uint32_t gid = getGroupId ( rdbId , key , split );
		// get the record, is -1 if variable. a table lookup.
		int32_t dataSize = getDataSizeFromRdbId ( rdbId );

		// . for delete never stores the data
		// . you can have positive keys without any dataSize member
		//   when they normally should have one, like titledb
		if ( forDelete ) dataSize = 0;
		// . negative keys have no data
		// . this is not the case unfortunately
		if ( del ) dataSize = 0;

		// ensure spiderdb request recs have data/url in them
		if ( (rdbId == RDB_SPIDERDB || rdbId == RDB2_SPIDERDB2) &&
		     g_spiderdb.isSpiderRequest ( (SPIDERDBKEY *)rec ) &&
		     ! forDelete &&
		     ! del &&
		     dataSize == 0 ) {
			g_process.shutdownAbort(true); }

		// if variable read that in
		if ( dataSize == -1 ) {
			// -1 means to read it in
			dataSize = *(int32_t *)p;
			// sanity check
			if ( dataSize < 0 ) { g_process.shutdownAbort(true); }
			// skip dataSize
			p += 4;
		}
		// skip over the data, if any
		p += dataSize;
		// breach us?
		if ( p > pend ) { g_process.shutdownAbort(true); }
	}
	// must be exactly equal to end
	if ( p != pend ) return false;
	return true;
#endif
}

bool XmlDoc::hashMetaList ( HashTableX *ht        ,
			    char       *p         ,
			    char       *pend      ,
			    bool        checkList ) {
	int32_t recSize = 0;
	int32_t count = 0;
	for ( ; p < pend ; p += recSize , count++ ) {
		// get rdbid
		rdbid_t rdbId = (rdbid_t)(*p & 0x7f);
		// skip rdb id
		p++;
		// save that
		char *rec = p;
		// get key size
		int32_t ks = getKeySizeFromRdbId ( rdbId );
		// sanity check
		if ( ks > 28 ) { g_process.shutdownAbort(true); }
		// is it a delete key?
		char del ;
		if ( ( p[0] & 0x01 ) == 0x00 ) del = true;
		else                           del = false;
		// convert into a key128_t, the biggest possible key
		char k[MAX_KEY_BYTES];//key128_t k ;
		// zero out
		KEYMIN(k,MAX_KEY_BYTES);
		//k.setMin();
		gbmemcpy ( k , p , ks );
		// skip it
		p += ks;
		// if negative, no data size allowed -- no
		if ( del ) continue;
		// get datasize
		int32_t dataSize = getDataSizeFromRdbId ( rdbId );
		// if -1, read it in
		if ( dataSize == -1 ) {
			dataSize = *(int32_t *)p;
			// sanity check
                        if ( dataSize < 0 ) { g_process.shutdownAbort(true); }
			p += 4;
                }

		// skip the data
		p += dataSize;
		// ignore spiderdb recs for parsing consistency check
		if ( rdbId == RDB_SPIDERDB ) continue;
		if ( rdbId == RDB2_SPIDERDB2 ) continue;
		// ignore tagdb as well!
		if ( rdbId == RDB_TAGDB || rdbId == RDB2_TAGDB2 ) continue;

		// set our rec size, includes key/dataSize/data
		int32_t recSize = p - rec;

		// if just adding, do it
		if ( ! checkList ) {
			// we now store ptr to the rec, not hash!
			if ( ! ht->addKey ( k , &rec ) ) return false;
			continue;
		}
		// check to see if this rec is in the provided hash table
		int32_t slot = ht->getSlot ( k );
		// bitch if not found
		if ( slot < 0 && ks==12 ) {
			key144_t *k2 = (key144_t *)k;
			int64_t tid = Posdb::getTermId(k2);
			char shardByTermId = Posdb::isShardedByTermId(k2);
			log("build: missing key #%" PRId32" rdb=%s ks=%" PRId32" ds=%" PRId32" "
			    "tid=%" PRIu64" "
			    "key=%s "
			    //"score8=%" PRIu32" score32=%" PRIu32" "
			    "shardByTermId=%" PRId32,
			    count,getDbnameFromId(rdbId),(int32_t)ks,
			    (int32_t)dataSize,tid ,
			    //(int32_t)score8,(int32_t)score32,
			    KEYSTR(k2,ks),
			    (int32_t)shardByTermId);
			// look it up


			// shortcut
			HashTableX *wt = m_wts;

			// now print the table we stored all we hashed into
			for ( int32_t i = 0 ; i < wt->m_numSlots ; i++ ) {
				// skip if empty
				if ( wt->m_flags[i] == 0 ) continue;
				// get the TermInfo
				TermDebugInfo *ti;
				ti = (TermDebugInfo *)wt->getValueFromSlot(i);
				// skip if not us
				if((ti->m_termId & TERMID_MASK)!=tid)continue;
				// got us
				char *start = m_wbuf.getBufStart();
				char *term = start + ti->m_termOff;
				const char *prefix = "";
				if ( ti->m_prefixOff >= 0 ) {
					prefix = start + ti->m_prefixOff;
					//prefix[ti->m_prefixLen] = '\0';
				}
				// NULL term it
				term[ti->m_termLen] = '\0';
				// print it
				log("parser: term=%s prefix=%s",//score32=%" PRId32,
				    term,prefix);//,(int32_t)ti->m_score32);
			}

			g_process.shutdownAbort(true);
		}
		if ( slot < 0 && ks != 12 ) {
			log("build: missing key #%" PRId32" rdb=%s ks=%" PRId32" ds=%" PRId32" "
			    "ks=%s "
			    ,count,getDbnameFromId(rdbId),(int32_t)ks,
			    (int32_t)dataSize,KEYSTR(k,ks));
			g_process.shutdownAbort(true);
		}
		// if in there, check the hashes
		//int32_t h2 = *(int32_t *)ht->getValueFromSlot ( slot );
		char *rec2 = *(char **)ht->getValueFromSlot ( slot );
		// get his dataSize
		int32_t dataSize2 = getDataSizeFromRdbId(rdbId);
		// his keysize
		int32_t ks2 = getKeySizeFromRdbId(rdbId);
		// get his recsize
		int32_t recSize2 = ks2 ;
		// if -1 that is variable
		if ( dataSize2 == -1 ) {
			dataSize2 = *(int32_t *)(rec2+ks2);
			recSize2 += 4;
		}
		// add it up
		recSize2 += dataSize2;
		// keep on chugging if they match
		if ( recSize2==recSize && !memcmp(rec,rec2,recSize) ) continue;
		// otherwise, bitch
		char shardByTermId = false;
		if ( rdbId == RDB_POSDB )
			shardByTermId = Posdb::isShardedByTermId(rec2);
		log("build: data not equal for key=%s "
		    "rdb=%s splitbytermid=%" PRId32" dataSize=%" PRId32,
		    KEYSTR(k,ks2),
		    getDbnameFromId(rdbId),(int32_t)shardByTermId,dataSize);

		// print into here
		SafeBuf sb1;
		SafeBuf sb2;

		// print it out
		if ( rdbId == RDB_SPIDERDB ) {
			// get rec
			if ( g_spiderdb.isSpiderRequest((key128_t *)rec) ) {
				SpiderRequest *sreq1 = (SpiderRequest *)rec;
				SpiderRequest *sreq2 = (SpiderRequest *)rec2;
				sreq1->print(&sb1);
				sreq2->print(&sb2);
			}
			else {
				SpiderReply *srep1 = (SpiderReply *)rec;
				SpiderReply *srep2 = (SpiderReply *)rec2;
				srep1->print(&sb1);
				srep2->print(&sb2);
			}
			log("build: rec1=%s",sb1.getBufStart());
			log("build: rec2=%s",sb2.getBufStart());

		}
		g_process.shutdownAbort(true);
	}
	return true;
}

void getMetaListWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// note it
	THIS->setStatus ( "in get meta list wrapper" );
	// get it
	char *ml = THIS->getMetaList ( );
	// sanity check
	if ( ! ml && ! g_errno ) {
		log(LOG_ERROR, "doc: getMetaList() returned NULL without g_errno");
		g_process.shutdownAbort(true);
	}
	// return if it blocked
	if ( ml == (void *)-1 ) return;
	// sanityh check
	if ( THIS->m_callback1 == getMetaListWrapper ) { g_process.shutdownAbort(true);}
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}


// . returns NULL and sets g_errno on error
// . make a meta list to call Msg4::addMetaList() with
// . called by Msg14.cpp
// . a meta list is just a buffer of Rdb records of the following format:
//   rdbid | rdbRecord
// . meta list does not include title rec since Msg14 adds that using Msg1
// . returns false and sets g_errno on error
// . sets m_metaList ptr and m_metaListSize
// . if "deleteIt" is true, we are a delete op on "old"
// . returns (char *)-1 if it blocks and will call your callback when done
// . generally only Repair.cpp changes these use* args to false
char *XmlDoc::getMetaList(bool forDelete) {
	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	if (m_metaListValid) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, already valid" );
		return m_metaList;
	}

	setStatus("getting meta list");

	// force it true?
	// "forDelete" means we want the metalist to consist of "negative"
	// keys that will annihilate with the positive keys in the index,
	// posdb and the other rdbs, in order to delete them. "deleteFromIndex"
	// means to just call getMetaList(tre) on the m_oldDoc (old XmlDoc)
	// which is built from the titlerec in Titledb. so don't confuse
	// these two things. otherwise when i add this we were not adding
	// the spiderreply of "Doc Force Deleted" from doing a query reindex
	// and it kept repeating everytime we started gb up.
	//if ( m_deleteFromIndex ) forDelete = true;

	// assume valid
	m_metaList     = "";
	m_metaListSize = 0;


	// . internal callback
	// . so if any of the functions we end up calling directly or
	//   indirectly block, this callback will be called
	if ( ! m_masterLoop ) {
		m_masterLoop  = getMetaListWrapper;
		m_masterState = this;
	}

	// returning from a handler that had an error?
	if (g_errno) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, g_errno=%" PRId32, g_errno);
		return NULL;
	}

	// if we are a spider status doc/titlerec and we are doing a rebuild
	// operation, then keep it simple
	if (m_setFromTitleRec && m_useSecondaryRdbs && m_contentTypeValid && m_contentType == CT_STATUS) {
		// if not rebuilding posdb then done, list is empty since
		// spider status docs do not contribute to linkdb, clusterdb,..
		if (!m_usePosdb && !m_useTitledb) {
			m_metaListValid = true;
			logTrace(g_conf.m_logTraceXmlDoc, "END, CT_STATUS");
			return m_metaList;
		}

		/////////////
		//
		// if user disabled spider status docs then delete the titlerec
		// AND the posdb index list from our dbs for this ss doc
		//
		/////////////
		CollectionRec *cr = getCollRec();
		if (!cr) {
			return NULL;
		}

		if (!cr->m_indexSpiderReplies) {
			logTrace(g_conf.m_logTraceXmlDoc, "Not indexing spider replies. Delete titlerec for this doc");

			int64_t uh48 = m_firstUrl.getUrlHash48();

			// delete title rec. true = delete?
			key96_t tkey = Titledb::makeKey (m_docId,uh48,true);

			// shortcut
			SafeBuf *ssb = &m_spiderStatusDocMetaList;

			// add to list. and we do not add the spider status
			// doc to posdb since we deleted its titlerec.
			ssb->pushChar(RDB_TITLEDB); // RDB2_TITLEDB2
			ssb->safeMemcpy(&tkey, sizeof(key96_t));
			m_metaList = ssb->getBufStart();
			m_metaListSize = ssb->getLength();
			m_metaListValid = true;

			logTrace( g_conf.m_logTraceXmlDoc, "END" );
			return m_metaList;
		}

		// set safebuf to the json of the spider status doc
		SafeBuf jd;
		if (!jd.safeMemcpy(ptr_utf8Content, size_utf8Content)) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, jd.safeMemcpy failed");
			return NULL;
		}

		// set m_spiderStatusDocMetaList from the json
		if (!setSpiderStatusDocMetaList(&jd, m_docId)) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, setSpiderStatusDocMetaList failed");
			return NULL;
		}

		// TODO: support titledb rebuild as well
		m_metaList = m_spiderStatusDocMetaList.getBufStart();
		m_metaListSize = m_spiderStatusDocMetaList.getLength();
		m_metaListValid = true;

		logTrace( g_conf.m_logTraceXmlDoc, "END, OK" );
		return m_metaList;
	}

	// if "rejecting" from index fake all this stuff
	if (m_deleteFromIndex) {
		logTrace(g_conf.m_logTraceXmlDoc, "deleteFromIndex true");

		// set these things to bogus values since we don't need them
		m_contentHash32Valid = true;
		m_contentHash32 = 0;
		m_httpStatusValid = true;
		m_httpStatus = 200;
		m_siteValid = true;
		ptr_site = "";
		size_site = strlen(ptr_site) + 1;
		m_isSiteRootValid = true;
		m_isSiteRoot2 = 1;
		m_tagPairHash32Valid = true;
		m_tagPairHash32 = 0;
		m_spiderLinksValid = true;
		m_spiderLinks2 = 1;
		m_langIdValid = true;
		m_langId = 1;
		m_siteNumInlinksValid = true;
		m_siteNumInlinks = 0;
		m_isIndexed = true;
		m_isIndexedValid = true;
		m_ipValid = true;
		m_ip = 123456;
	}

	CollectionRec *cr = getCollRec();
	if (!cr) {
		logTrace(g_conf.m_logTraceXmlDoc, "getCollRec failed");
		return NULL;
	}

	// get our checksum
	int32_t *plainch32 = getContentHash32();
	if (!plainch32 || plainch32 == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getContentHash32 failed");
		return (char *)plainch32;
	}

	// get this too
	int16_t *hs = getHttpStatus();
	if (!hs || hs == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getHttpStatus failed");
		return (char *)hs;
	}

	// make sure site is valid
	char *site = getSite();
	if (!site || site == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getSite failed");
		return (char *)site;
	}

	// this seems to be an issue as well for "unchanged" block below
	char *isr = getIsSiteRoot();
	if (!isr || isr == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getIsSiteRoot failed");
		return (char *)isr;
	}

	// make sure docid valid
	int64_t *mydocid = getDocId();
	if (!mydocid || mydocid == (int64_t *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getDocId failed");
		return (char *)mydocid;
	}

	// . get the old version of our XmlDoc from the previous spider time
	// . set using the old title rec in titledb
	// . should really not do any more than set m_titleRec...
	// . should not even uncompress it!
	// . getNewSpiderReply() will use this to set the reply if
	//   m_indexCode == EDOCUNCHANGED...
	XmlDoc **pod = getOldXmlDoc();
	if (!pod || pod == (XmlDoc **)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getOldXmlDoc failed");
		return (char *)pod;
	}

	// point to the old xml doc if no error, etc.
	XmlDoc *od = *pod;

	// check if we are already indexed
	char *isIndexed = getIsIndexed();
	if (!isIndexed || isIndexed == (char *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getIsIndexed failed");
		return (char *)isIndexed;
	}

	// why call this way down here? it ends up downloading the doc!
	// @todo: BR: Eh, what? ^^^
	int32_t *indexCode = getIndexCode();
	if (!indexCode || indexCode == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getIndexCode failed");
		return (char *)indexCode;
	}

	// sanity check
	if (!m_indexCodeValid) {
		g_process.shutdownAbort(true);
	}

	// this means to abandon the injection
	if (*indexCode == EABANDONED) {
		m_metaList = (char *)0x123456;
		m_metaListSize = 0;
		m_metaListValid = true;
		logTrace(g_conf.m_logTraceXmlDoc, "END, abandoned");
		return m_metaList;
	}

	// . some index code warrant retries, like EDNSTIMEDOUT, ETCPTIMEDOUT,
	//   etc. these are deemed temporary errors. other errors basically
	//   indicate a document that will never be indexable and should,
	//   if currently indexed, be deleted.
	// . just add the spider reply and we're done
	if (    *indexCode == EDNSTIMEDOUT
	     || *indexCode == ETCPTIMEDOUT
	     || *indexCode == EUDPTIMEDOUT
	     || *indexCode == EDNSDEAD
	     || *indexCode == ENETUNREACH
	     || *indexCode == EHOSTUNREACH
		// . treat this as a temporary error i guess
		// . getNewSpiderReply() below will clear the error in it and
		//   copy stuff over from m_sreq and m_oldDoc for this case
		|| *indexCode == EDOCUNCHANGED
		) {
		// sanity - in repair mode?
		if (m_useSecondaryRdbs) {
			g_process.shutdownAbort(true);
		}

		logTrace(g_conf.m_logTraceXmlDoc, "Temporary error state: %" PRId32, *indexCode);

		// . this seems to be an issue for blocking
		// . if we do not have a valid ip, we can't compute this,
		//   in which case it will not be valid in the spider reply
		// . why do we need this for timeouts etc? if the doc is
		//   unchanged
		//   we should probably update its siteinlinks in tagdb
		//   periodically and reindex the whole thing...
		// . i think we were getting the sitenuminlinks for
		//   getNewSpiderReply()
		if (m_ipValid && m_ip != 0 && m_ip != -1) {
			int32_t *sni = getSiteNumInlinks();
			if (!sni || sni == (int32_t *)-1) {
				logTrace(g_conf.m_logTraceXmlDoc, "getSiteNumInlinks failed");
				return (char *)sni;
			}
		}

		// all done!
		bool addReply = true;
		// page parser calls set4 and sometimes gets a dns time out!
		if (m_sreqValid && m_sreq.m_isPageParser) {
			addReply = false;
		}

		// return nothing if done
		if (!addReply) {
			m_metaListSize = 0;
			m_metaList = (char *)0x1;
			logTrace(g_conf.m_logTraceXmlDoc, "END, m_isPageParser and valid");
			return m_metaList;
		}

		// save this
		int32_t savedCode = *indexCode;

		// before getting our spider reply, assign crap from the old
		// doc to us since we are unchanged! this will allow us to
		// call getNewSpiderReply() without doing any processing, like
		// setting the Xml or Words classes, etc.
		copyFromOldDoc(od);

		// need this though! i don't want to print out "Success"
		// in the log in the logIt() function
		m_indexCode = savedCode;
		m_indexCodeValid = true;

		// but set our m_contentHash32 from the spider request
		// which got it from the spiderreply in the case of
		// EDOCUNCHANGED. this way ch32=xxx will log correctly.
		// I think this is only when EDOCUNCHANGED is set in the
		// Msg13.cpp code, when we have a spider compression proxy.
		if (*indexCode == EDOCUNCHANGED && m_sreqValid && !m_contentHash32Valid) {
			m_contentHash32 = m_sreq.m_contentHash32;
			m_contentHash32Valid = true;
		}

		// we need these got getNewSpiderReply()
		m_wasInIndex = (od != NULL);
		m_isInIndex = m_wasInIndex;
		m_wasInIndexValid = true;
		m_isInIndexValid = true;

		// unset our ptr_linkInfo1 so we do not free it and core
		// since we might have set it in copyFromOldDoc() above
		ptr_linkInfo1 = NULL;
		size_linkInfo1 = 0;
		m_linkInfo1Valid = false;

		// . if not using spiderdb we are done at this point
		// . this happens for diffbot json replies (m_dx)
		if (!m_useSpiderdb) {
			m_metaList = NULL;
			m_metaListSize = 0;
			logTrace(g_conf.m_logTraceXmlDoc, "END, not using spiderdb");
			return (char *)0x01;
		}

		// get our spider reply
		SpiderReply *newsr = getNewSpiderReply();
		// return on error
		if (!newsr) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, could not get spider reply");
			return (char *)newsr;
		}

		// . panic on blocking! this is supposed to be fast!
		// . it might still have to lookup the tagdb rec?????
		if (newsr == (void *)-1) {
			g_process.shutdownAbort(true);
		}

		// how much we need
		int32_t needx = sizeof(SpiderReply) + 1;


		// . INDEX SPIDER REPLY (1a)
		// . index ALL spider replies as separate doc. error or not.
		// . then print out error histograms.
		// . we should also hash this stuff when indexing the
		//   doc as a whole

		// i guess it is safe to do this after getting the spiderreply
		// get the spiderreply ready to be added
		SafeBuf *spiderStatusDocMetaList = getSpiderStatusDocMetaList(newsr, forDelete);
		// error?
		if (!spiderStatusDocMetaList) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getSpiderStatusDocMetaList failed");
			return NULL;
		}

		// blocked?
		if (spiderStatusDocMetaList==(void *)-1) {
			logTrace( g_conf.m_logTraceXmlDoc, "END, getSpiderStatusDocMetaList blocked" );
			return (char *)-1;
		}

		// need to alloc space for it too
		int32_t len = spiderStatusDocMetaList->length();
		needx += len;

		// this too
		m_addedStatusDocSize = len;
		m_addedStatusDocSizeValid = true;

		// make the buffer
		m_metaList = (char *)mmalloc(needx, "metalist");
		if (!m_metaList) {
			return NULL;
		}

		// save size for freeing later
		m_metaListAllocSize = needx;

		// ptr and boundary
		m_p = m_metaList;
		m_pend = m_metaList + needx;

		// save it
		char *saved = m_p;

		// first store spider reply "document"
		if (spiderStatusDocMetaList) {
			gbmemcpy (m_p, spiderStatusDocMetaList->getBufStart(), spiderStatusDocMetaList->length());
			m_p += spiderStatusDocMetaList->length();
		}

		// sanity check
		if (!m_docIdValid) {
			g_process.shutdownAbort(true);
		}

		// now add the new rescheduled time
		setStatus("adding SpiderReply to spiderdb");
		logTrace(g_conf.m_logTraceXmlDoc, "Adding spider reply to spiderdb");

		// rdbid first
		rdbid_t rd = m_useSecondaryRdbs ? RDB2_SPIDERDB2 : RDB_SPIDERDB;
		*m_p++ = (char)rd;

		// get this
		if (!m_srepValid) {
			g_process.shutdownAbort(true);
		}

		// store the spider rec
		int32_t newsrSize = newsr->getRecSize();
		gbmemcpy (m_p, newsr, newsrSize);
		m_p += newsrSize;
		m_addedSpiderReplySize = newsrSize;
		m_addedSpiderReplySizeValid = true;

		// sanity check
		if (m_p - saved != needx) {
			g_process.shutdownAbort(true);
		}

		// sanity check
		verifyMetaList(m_metaList, m_p, forDelete);

		// verify it
		m_metaListValid = true;

		// set size
		m_metaListSize = m_p - m_metaList;

		// all done
		logTrace(g_conf.m_logTraceXmlDoc, "END, all done");
		return m_metaList;
	}


	// get the old meta list if we had an old doc
	char *oldList = NULL;
	int32_t oldListSize = 0;
	if (od) {
		od->m_useSpiderdb = false;
		od->m_useTagdb = false;

		// if we are doing diffbot stuff, we are still indexing this
		// page, so we need to get the old doc meta list
		oldList = od->getMetaList(true);
		oldListSize = od->m_metaListSize;
		if (!oldList || oldList == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, get old meta list failed");
			return oldList;
		}
	}

	// . set whether we should add recs to titledb, posdb, linkdb, etc.
	// . if this doc is set by titlerec we won't change these
	// . we only turn off m_usePosdb, etc. if there is a
	//   <meta name=noindex content=1>
	// . we will still add to spiderdb, but not posdb, linkdb, titledb
	//   and clusterdb.
	// . so we'll add the spiderreply for this doc and the spiderrequests
	//   for all outlinks and "firstIp" tagrecs to tagdb for those outlinks
	// . we use this for adding the url seed file gbdmoz.urls.txt
	//   which contains a list of all the dmoz urls we want to spider.
	//   gbdmoz.urls.txt is generated by dmozparse.cpp. we spider all
	//   these dmoz urls so we can search the CONTENT of the pages in dmoz,
	//   something dmoz won't let you do.
	char *mt = hasNoIndexMetaTag();
	if (!mt || mt == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hasNoIndexMetaTag failed");
		return mt;
	}

	if (*mt) {
		m_usePosdb = false;
		m_useLinkdb = false;
		m_useTitledb = false;
		m_useClusterdb = false;
		// do not add the "firstIp" tagrecs of the outlinks any more
		// because it might hurt us?
		m_useTagdb = false;
	}

	// . need this if useTitledb is true
	// . otherwise XmlDoc::getTitleRecBuf() cores because its invalid
	// . this cores if rebuilding just posdb because hashAll() needs
	//   the inlink texts for hashing
	LinkInfo *info1 = getLinkInfo1();
	if (!info1 || info1 == (LinkInfo *)-1) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinkInfo1 failed" );
		return (char *)info1;
	}

	// so getSiteRank() works
	int32_t *sni = getSiteNumInlinks();
	if (!sni || sni == (int32_t *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getSiteNumInlinks failed");
		return (char *)sni;
	}

	// so addTable144 works
	uint8_t *langId = getLangId();
	if (!langId || langId == (uint8_t *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getLangId failed");
		return (char *)langId;
	}

	// . before making the title rec we need to set all the ptrs!
	// . so at least now set all the data members we will need to
	//   seriazlize into the title rec because we can't be blocking further
	//   down below after we set all the hashtables and XmlDoc::ptr_ stuff
	if (!m_setFromTitleRec || m_useSecondaryRdbs) {
		// all member vars should already be valid if set from titlerec
		char *ptg = prepareToMakeTitleRec();

		// return NULL with g_errno set on error
		if (!ptg || ptg == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, prepareToMakeTitleRec failed");
			return ptg;
		}
	}

	// our next slated spider priority
	char *spiderLinks3 = getSpiderLinks();
	if (!spiderLinks3 || spiderLinks3 == (char *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getSpiderLinks failed");
		return spiderLinks3;
	}

	bool spideringLinks = *spiderLinks3;

	// shortcut
	XmlDoc *nd = this;

	///////////////////////////////////
	///////////////////////////////////
	//
	// if we had an error, do not add us regardless to the index
	// although we might add SOME things depending on the error.
	// Like add the redirecting url if we had a ESIMPLIFIEDREDIR error.
	// So what we had to the Rdbs depends on the indexCode.
	//
	// OR if deleting from index, we just want to get the metalist
	// directly from "od"
	//

	if (m_indexCode || m_deleteFromIndex) {
		nd = NULL;
	}

	//
	///////////////////////////////////
	///////////////////////////////////

	if (!nd) {
		spideringLinks = false;
	}

	// set these for getNewSpiderReply() so it can set
	// SpiderReply::m_wasIndexed and m_isIndexed...
	m_wasInIndex = (od != NULL);
	m_isInIndex  = (nd != NULL);
	m_wasInIndexValid = true;
	m_isInIndexValid  = true;

	// if we are adding a simplified redirect as a link to spiderdb
	// likewise if the error was ENONCANONICAL treat it like that
	if (m_indexCode == EDOCSIMPLIFIEDREDIR || m_indexCode == EDOCNONCANONICAL) {
		spideringLinks = true;
	}

	//
	// . prepare the outlink info if we are adding links to spiderdb!
	// . do this before we start hashing so we do not block and re-hash!!
	//
	if (m_useSpiderdb && spideringLinks && !m_doingConsistencyCheck) {
		setStatus("getting outlink info");
		logTrace(g_conf.m_logTraceXmlDoc, "call getOutlinkTagRecVector");

		TagRec ***grv = getOutlinkTagRecVector();
		if (!grv || grv == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getOutlinkTagRecVector returned -1");
			return (char *)grv;
		}

		logTrace(g_conf.m_logTraceXmlDoc, "call getOutlinkFirstIpVector");

		int32_t **ipv = getOutlinkFirstIpVector();
		if (!ipv || ipv == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getOutlinkFirstIpVector returned -1");
			return (char *)ipv;
		}
	}

	// get the tag buf to add to tagdb
	SafeBuf *ntb = NULL;
	if (m_useTagdb && !m_deleteFromIndex) {
		logTrace(g_conf.m_logTraceXmlDoc, "call getNewTagBuf");
		ntb = getNewTagBuf();
		if (!ntb || ntb == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getNewTagBuf failed");
			return (char *)ntb;
		}
	}

	logTrace(g_conf.m_logTraceXmlDoc, "call getIsSiteRoot");
	char *isRoot = getIsSiteRoot();
	if (!isRoot || isRoot == (char *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getIsSiteRoot returned -1");
		return isRoot;
	}

	Words *ww = getWords();
	if (!ww || ww == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getWords returned -1");
		return (char *)ww;
	}

	int64_t *pch64 = getExactContentHash64();
	if (!pch64 || pch64 == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getExactContentHash64 returned -1");
		return (char *)pch64;
	}

	// need firstip if adding a rebuilt spider request
	if (m_useSpiderdb && m_useSecondaryRdbs) {
		int32_t *fip = getFirstIp();
		if (!fip || fip == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getFirstIp returned -1");
			return (char *)fip;
		}
	}

	// shit, we need a spider reply so that it will not re-add the
	// spider request to waiting tree, we ignore docid-based
	// recs that have spiderreplies in Spider.cpp
	SpiderReply *newsr = NULL;
	if (m_useSpiderdb) {
		newsr = getNewSpiderReply();
		if (!newsr || newsr == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getNewSpiderReply failed");
			return (char *)newsr;
		}
	}

	// the site hash for hashing
	int32_t *sh32 = getSiteHash32();
	if (!sh32 || sh32 == (int32_t *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getSiteHash32 failed");
		return (char *)sh32;
	}

	if (m_useLinkdb && !m_deleteFromIndex) {
		int32_t *linkSiteHashes = getLinkSiteHashes();
		if (!linkSiteHashes || linkSiteHashes == (void *)-1) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getLinkSiteHashes failed");
			return (char *)linkSiteHashes;
		}
	}


	///////////
	//
	// BEGIN the diffbot json object index hack
	//
	// if we are using diffbot, then each json object in the diffbot reply
	// should be indexed as its own document.
	//
	///////////


	// i guess it is safe to do this after getting the spiderreply
	SafeBuf *spiderStatusDocMetaList = NULL;

	// get the spiderreply ready to be added to the rdbs w/ msg4
	// but if doing a rebuild operation then do not get it, we'll rebuild
	// it since it will have its own titlerec
	if (!m_useSecondaryRdbs) {
		spiderStatusDocMetaList = getSpiderStatusDocMetaList(newsr, forDelete);
		if (!spiderStatusDocMetaList) {
			log("build: ss doc metalist null. bad!");

			logTrace(g_conf.m_logTraceXmlDoc, "END, getSpiderStatusDocMetaList failed");
			return NULL;
		}
	}

	if (spiderStatusDocMetaList == (void *)-1) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getSpiderStatusDocMetaList failed");
		return (char *)spiderStatusDocMetaList;
	}

	//
	// CAUTION
	//
	// We should never "block" after this point, lest the hashtables
	// we create get messed up.
	//

	//
	//
	// START HASHING
	//
	//

	// store what we hash into this table
	if ((m_pbuf || m_storeTermListInfo) && !m_wts) {
		// init it. the value is a TermInfo class. allowDups=true!
		m_wtsTable.set(12, sizeof(TermDebugInfo), 0, NULL, 0, true, "wts-tab");

		// point to it, make it active
		m_wts = &m_wtsTable;
	}

	// how much to alloc? compute an upper bound
	int32_t need = 0;

	setStatus("hashing posdb terms");

	// . hash our documents terms into "tt1"
	// . hash the old document's terms into "tt2"
	// . by old, we mean the older versioned doc of this url spidered b4
	HashTableX tt1;

	// . prepare it, 5000 initial terms
	// . make it nw*8 to avoid have to re-alloc the table!!!
	// . i guess we can have link and neighborhood text too! we don't
	//   count it here though... but add 5k for it...
	int32_t need4 = m_words.getNumWords() * 4 + 5000;
	if (m_usePosdb && nd) {
		if (!tt1.set(18, 4, need4, NULL, 0, false, "posdb-indx")) {
			logTrace(g_conf.m_logTraceXmlDoc, "tt1.set failed");
			return NULL;
		}

		int32_t did = tt1.m_numSlots;
		// . hash the document terms into "tt1"
		// . this is a biggie!!!
		// . only hash ourselves if m_indexCode is false
		// . m_indexCode is non-zero if we should delete the doc from
		//   index
		// . i think this only adds to posdb
		// shit, this blocks which is bad!!!
		char *nod = hashAll(&tt1);

		// you can't block here because if we are re-called we lose tt1
		if (nod == (char *)-1) {
			g_process.shutdownAbort(true);
		}

		// error?
		if (!nod) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, hashAll failed");
			return NULL;
		}

		int32_t done = tt1.m_numSlots;
		if (done != did) {
			log(LOG_WARN, "xmldoc: reallocated big table! bad. old=%" PRId32" new=%" PRId32" nw=%" PRId32, did, done, m_words.getNumWords());
		}
	}

	// if indexing the spider reply as well under a different docid
	// there is no reason we can't toss it into our meta list here
	if (spiderStatusDocMetaList) {
		need += spiderStatusDocMetaList->length();
	}

	// space for indexdb AND DATEDB! +2 for rdbids
	int32_t needIndexdb = tt1.m_numSlotsUsed * (sizeof(key144_t) + 2 + sizeof(key128_t));
	need += needIndexdb;

	// clusterdb keys. plus one for rdbId
	int32_t needClusterdb = nd ? 13 : 0;
	need += needClusterdb;

	// . LINKDB
	// . linkdb records. assume one per outlink
	// . we may index 2 16-byte keys for each outlink
	// if injecting, spideringLinks is false, but then we don't
	// add the links to linkdb, which causes the qainlinks() test to fail
	Links *nl2 = &m_links;

	// do not bother if deleting. but we do add simplified redirects
	// to spiderdb as SpiderRequests now.
	int32_t code = m_indexCode;
	if (code == EDOCSIMPLIFIEDREDIR || code == EDOCNONCANONICAL) {
		code = 0;
	}

	if (code) {
		nl2 = NULL;
	}

	// . set key/data size
	// . use a 16 byte key, not the usual 12
	// . use 0 for the data, since these are pure keys, which have no
	//   scores to accumulate
	HashTableX kt1;

	int32_t nis = 0;
	if (m_useLinkdb && nl2) {
		nis = nl2->getNumLinks() * 4;
	}

	// pre-grow table based on # outlinks
	kt1.set(sizeof(key224_t), 0, nis, NULL, 0, false, "link-indx");

	// use magic to make fast
	kt1.m_useKeyMagic = true;

	// linkdb keys will have the same lower 4 bytes, so make hashing fast.
	// they are 28 byte keys. bytes 20-23 are the hash of the linkEE
	// so that will be the most random.
	kt1.m_maskKeyOffset = 20;

	// . we already have a Links::hash into the Termtable for links: terms,
	//   but this will have to be for adding to Linkdb. basically take a
	//   lot of it from Linkdb::fillLinkdbList()
	// . these return false with g_errno set on error
	if (m_useLinkdb && nl2 && !hashLinksForLinkdb(&kt1)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashLinksForLinkdb failed");
		return NULL;
	}

	// add up what we need. +1 for rdbId
	int32_t needLinkdb = kt1.m_numSlotsUsed * (sizeof(key224_t)+1);
	need += needLinkdb;

	// we add a negative key to doledb usually (include datasize now)
	int32_t needDoledb = forDelete ? 0 : (sizeof(key96_t) + 1);
	need += needDoledb;

	// for adding the SpiderReply to spiderdb (+1 for rdbId)
	int32_t needSpiderdb1 = forDelete ? 0 : (sizeof(SpiderReply) + 1);
	need += needSpiderdb1;

	// if injecting we add a spiderrequest to be able to update it
	// but don't do this if it is pagereindex. why is pagereindex
	// setting the injecting flag anyway?
	int32_t needSpiderdbRequest = 0;
	if (m_sreqValid && m_sreq.m_isInjecting && m_sreq.m_fakeFirstIp && !m_sreq.m_forceDelete) {
		// NO! because when injecting a warc and the subdocs
		// it contains, gb then tries to spider all of them !!! sux...
		needSpiderdbRequest = 0;
	} else if (m_useSpiderdb && m_useSecondaryRdbs) {
		// or if we are rebuilding spiderdb
		needSpiderdbRequest = sizeof(SpiderRequest) + m_firstUrl.getUrlLen() + 1;
	}
	need += needSpiderdbRequest;

	// . for adding our outlinks to spiderdb
	// . see SpiderRequest::getRecSize() for description
	// . SpiderRequest::getNeededSize() will include the null terminator
	int32_t needSpiderdb2 = 0;

	// don't need this if doing consistecy check
	// nor for generating the delete meta list for incremental indexing
	// and the url buffer of outlinks. includes \0 terminators i think
	if (!m_doingConsistencyCheck && !forDelete) {
		needSpiderdb2 = (SpiderRequest::getNeededSize(0) * m_links.getNumLinks()) + m_links.getLinkBufLen();
	}

	need += needSpiderdb2;

	// the new tags for tagdb
	int32_t needTagdb = ntb ? ntb->length() : 0;
	need += needTagdb;

	//
	// . CHECKSUM PARSING CONSISTENCY TEST
	//
	// . set m_metaListChecksum member (will be stored in titleRec header)
	// . gotta set m_metaListCheckSum8 before making titleRec below
	// . also, if set from titleRec, verify metalist is the same!
	//
	if (!m_computedMetaListCheckSum) {
		// do not call twice!
		m_computedMetaListCheckSum = true;
		// all keys in tt1, ns1, kt1 and pt1
		int32_t ck32 = tt1.getKeyChecksum32();

		// set this before calling getTitleRecBuf() below
		uint8_t currentMetaListCheckSum8 = (uint8_t)ck32;
		// see if matches what was in old titlerec
		if (m_metaListCheckSum8Valid &&
		    // if we were set from a titleRec, see if we got
		    // a different hash of terms to index this time around...
		    m_setFromTitleRec &&
		    // fix for import log spam
		    !m_isImporting &&
		    m_metaListCheckSum8 != currentMetaListCheckSum8) {
			log(LOG_WARN, "xmldoc: checksum parsing inconsistency for %s (old)%i != %i(new). ",
			    m_firstUrl.getUrl(), (int)m_metaListCheckSum8, (int)currentMetaListCheckSum8);
			//tt1.print();
		}

		// assign the new one, getTitleRecBuf() call below needs this
		m_metaListCheckSum8 = currentMetaListCheckSum8;
		m_metaListCheckSum8Valid = true;
	}


	//
	// now that we've set all the ptr_* members vars, we can make
	// the title rec
	//

	// . add in title rec size
	// . should be valid because we called getTitleRecBuf() above
	// . this should include the key
	// . add in possible negative key for deleting old title rec
	// +1 for rdbId
	int32_t needTitledb = sizeof(key96_t) + 1;

	// . MAKE the title rec from scratch, that is all we need at this point
	// . if repairing and not rebuilding titledb, we do not need the titlerec
	if (m_useTitledb) {
		// this buf includes key/datasize/compressdata
		SafeBuf *tr = getTitleRecBuf();

		// panic if this blocks! it should not at this point because
		// we'd have to re-hash the crap above
		if (tr == (void *)-1) {
			g_process.shutdownAbort(true);
		}

		// return NULL with g_errno set on error
		if (!tr) {
			return (char *)tr;
		}

		// sanity check - if the valid title rec is null,
		// m_indexCode is set!
		if (tr->length() == 0 && !m_indexCode) {
			g_process.shutdownAbort(true);
		}

		if (nd && !forDelete) {
			needTitledb += m_titleRecBuf.length();
		}

		// then add it in
		need += needTitledb;

		// the titledb unlock key for msg12 in spider.cpp
		need += sizeof(key96_t);
	}

	// . alloc mem for metalist
	// . sanity
	if (m_metaListSize > 0) {
		g_process.shutdownAbort(true);
	}

	// make the buffer
	m_metaList = (char *)mmalloc(need, "metalist");
	if (!m_metaList) {
		return NULL;
	}

	// save size for freeing later
	m_metaListAllocSize = need;

	// ptr and boundary
	m_p = m_metaList;
	m_pend = m_metaList + need;

	//
	// TITLEDB
	//
	setStatus ("adding titledb recs");

	// checkpoint
	char *saved = m_p;

	// . store title rec
	// . Repair.cpp might set useTitledb to false!
	if (m_useTitledb && nd) {
		// rdbId
		*m_p++ = m_useSecondaryRdbs ? RDB2_TITLEDB2 : RDB_TITLEDB;

		// sanity
		if (!nd->m_titleRecBufValid) {
			g_process.shutdownAbort(true);
		}

		// key, dataSize, data is the whole rec
		// if getting an "oldList" to do incremental posdb updates
		// then do not include the data portion of the title rec
		int32_t tsize = (forDelete) ? sizeof(key96_t) : nd->m_titleRecBuf.length();
		gbmemcpy ( m_p , nd->m_titleRecBuf.getBufStart() , tsize );

		m_p += tsize;
	}

	// sanity check
	if (m_p - saved > needTitledb) {
		g_process.shutdownAbort(true);
	}

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);

	//
	// ADD BASIC POSDB TERMS
	//
	setStatus("adding posdb terms");

	// checkpoint
	saved = m_p;

	// store indexdb terms into m_metaList[]
	if (m_usePosdb && !addTable144(&tt1, m_docId)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, addTable144 failed");
		return NULL;
	}

	// sanity check
	if (m_p - saved > needIndexdb) {
		g_process.shutdownAbort(true);
	}

	// free all mem
	tt1.reset();

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);


	//
	// ADD CLUSTERDB KEYS
	//
	setStatus("adding clusterdb keys");

	// checkpoint
	saved = m_p;

	// . do we have adult content?
	// . should already be valid!
	if (nd && !m_isAdultValid) {
		g_process.shutdownAbort(true);
	}

	// . store old only if new tr is good and keys are different from old
	// . now we store even if skipIndexing is true because i'd like to
	//   see how many titlerecs we have and count them towards the
	//   docsIndexed count...
	if (m_useClusterdb && nd) {
		// . get new clusterdb key
		// . we use the host hash for the site hash! hey, this is only 26 bits!
		key96_t newk = g_clusterdb.makeClusterRecKey(*nd->getDocId(), *nd->getIsAdult(), *nd->getLangId(), nd->getHostHash32a(), false);

		// store rdbid
		*m_p = RDB_CLUSTERDB;

		// use secondary if we should
		if (m_useSecondaryRdbs) {
			*m_p = RDB2_CLUSTERDB2;
		}

		// skip
		m_p++;

		// and key
		*(key96_t *)m_p = newk;

		// skip it
		m_p += sizeof(key96_t);
	}

	// sanity check
	if (m_p - saved > needClusterdb) {
		g_process.shutdownAbort(true);
	}

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);


	//
	// ADD LINKDB KEYS
	//
	setStatus("adding linkdb keys");

	// checkpoint
	saved = m_p;

	// add that table to the metalist (LINKDB)
	if (m_useLinkdb && !addTable224(&kt1)) {
		logTrace(g_conf.m_logTraceXmlDoc, "addTable224 failed");
		return NULL;
	}

	// sanity check
	if (m_p - saved > needLinkdb) {
		g_process.shutdownAbort(true);
	}

	// all done
	kt1.reset();

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);


	//////
	//
	// add SPIDERREPLY BEFORE and SPIDERREQUEST!!!
	//
	// add spider reply first so we do not immediately respider
	// this same url if we were injecting it because no SpiderRequest
	// may have existed, and SpiderColl::addSpiderRequest() will
	// spawn a spider of this url again unless there is already a REPLY
	// in spiderdb!!! crazy...
	bool addReply = true;

	// save it
	saved = m_p;

	// now add the new rescheduled time
	if (m_useSpiderdb && addReply && !forDelete) {
		// note it
		setStatus("adding SpiderReply to spiderdb");

		// rdbid first
		*m_p++ = (m_useSecondaryRdbs) ? RDB2_SPIDERDB2 : RDB_SPIDERDB;

		// get this
		if (!m_srepValid) {
			g_process.shutdownAbort(true);
		}

		// store the spider rec
		int32_t newsrSize = newsr->getRecSize();
		gbmemcpy (m_p, newsr, newsrSize);
		m_p += newsrSize;

		m_addedSpiderReplySize = newsrSize;
		m_addedSpiderReplySizeValid = true;

		// sanity check - must not be a request, this is a reply
		if (g_spiderdb.isSpiderRequest(&newsr->m_key)) {
			g_process.shutdownAbort(true);
		}

		// sanity check
		if (m_p - saved != needSpiderdb1) {
			g_process.shutdownAbort(true);
		}

		// sanity check
		verifyMetaList(m_metaList, m_p, forDelete);
	}


	// if we are injecting we must add the spider request
	// we are injecting from so the url can be scheduled to be
	// spidered again.
	// NO! because when injecting a warc and the subdocs
	// it contains, gb then tries to spider all of them !!! sux...
	if (needSpiderdbRequest) {
		// note it
		setStatus("adding spider request");

		// checkpoint
		saved = m_p;

		// store it here
		SpiderRequest revisedReq;

 		// if doing a repair/rebuild of spiderdb...
		if (m_useSecondaryRdbs) {
			getRebuiltSpiderRequest(&revisedReq);
		} else {
			// this fills it in for doing injections
			getRevisedSpiderRequest(&revisedReq);

			// sanity log
			if (!m_firstIpValid) {
				g_process.shutdownAbort(true);
			}

			// sanity log
			if (m_firstIp == 0 || m_firstIp == -1) {
				const char *url = m_sreqValid ? m_sreq.m_url : "unknown";
				log(LOG_WARN, "build: error3 getting real firstip of %" PRId32" for %s. not adding new request.",
				    (int32_t)m_firstIp,url);
				goto skipNewAdd2;
			}
		}

		// copy it
		*m_p++ = (m_useSecondaryRdbs) ? RDB2_SPIDERDB2 : RDB_SPIDERDB;

		// store it back
		gbmemcpy (m_p, &revisedReq, revisedReq.getRecSize());

		// skip over it
		m_p += revisedReq.getRecSize();

		// sanity check
		if (m_p - saved > needSpiderdbRequest) {
			g_process.shutdownAbort(true);
		}

		m_addedSpiderRequestSize = revisedReq.getRecSize();
		m_addedSpiderRequestSizeValid = true;
	}

skipNewAdd2:

	//
	// ADD SPIDERDB RECORDS of outlinks
	//
	// - do this AFTER computing revdb since we do not want spiderdb recs
	//   to be in revdb.
	//
	setStatus("adding spiderdb keys");

	// sanity check. cannot spider until in sync
	if (!isClockInSync()) {
		g_process.shutdownAbort(true);
	}

	// checkpoint
	saved = m_p;

	// . should be fixed from Links::setRdbList
	// . we should contain the msge that msg16 uses!
	// . we were checking m_msg16.m_recycleContent, but i have not done
	//   that in years!!! MDW
	// . we were also checking if the # of banned outlinks >= 2, then
	//   we would not do this...
	// . should also add with a time of now plus 5 seconds to that if
	//   we spider an outlink linkdb should be update with this doc
	//   pointing to it so it can get link text then!!
	if (m_useSpiderdb && spideringLinks && nl2 && !m_doingConsistencyCheck && !forDelete) {
		logTrace( g_conf.m_logTraceXmlDoc, "Adding spiderdb records of outlinks" );

		// returns NULL and sets g_errno on error
		char *ret = addOutlinkSpiderRecsToMetaList();

		// sanity check
		if (!ret && !g_errno) {
			g_process.shutdownAbort(true);
		}

		// return NULL on error
		if (!ret) {
			logTrace(g_conf.m_logTraceXmlDoc, "addOutlinkSpiderRecsToMetaList failed");
			return NULL;
		}

		// this MUST not block down here, to avoid re-hashing above
		if (ret == (void *)-1) {
			g_process.shutdownAbort(true);
		}
	}

	// sanity check
	if (m_p - saved > needSpiderdb2) {
		g_process.shutdownAbort(true);
	}

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);

	//
	// ADD TAG RECORDS TO TAGDB
	//

	// checkpoint
	saved = m_p;

	// . only do this if NOT setting from a title rec
	// . it might add a bunch of forced spider recs to spiderdb
	// . store into tagdb even if indexCode is set!
	if (m_useTagdb && ntb && !forDelete) {
		// ntb is a safebuf of Tags, which are already Rdb records
		// so just gbmemcpy them directly over
		gbmemcpy (m_p, ntb->getBufStart(), ntb->length());
		m_p += ntb->length();
	}

	// sanity check
	if (m_p - saved > needTagdb) {
		g_process.shutdownAbort(true);
	}

	// sanity check
	verifyMetaList(m_metaList, m_p, forDelete);

	//
	// ADD INDEXED SPIDER REPLY with different docid so we can
	// search index of spider replies! (NEW!)
	//
	// . index spider reply with separate docid so they are all searchable.
	// . see getSpiderStatusDocMetaList() function to see what we index
	//   and the titlerec we create for it
	if (spiderStatusDocMetaList) {
		gbmemcpy (m_p, spiderStatusDocMetaList->getBufStart(), spiderStatusDocMetaList->length());
		m_p += spiderStatusDocMetaList->length();
		m_addedStatusDocSize = spiderStatusDocMetaList->length();
		m_addedStatusDocSizeValid = true;
	}

	// shortcut
	saved = m_p;

	// sanity check
	if (m_p > m_pend || m_p < m_metaList) {
		g_process.shutdownAbort(true);
	}

	int32_t now = getTimeGlobal();

	/////////////////
	//
	// INCREMENTAL INDEXING / INCREMENTAL UPDATING
	//
	// now prune/manicure the metalist to remove records that
	// were already added, and insert deletes for records that
	// changed since the last time. this is how we do deletes
	// now that we have revdb. this allows us to avoid
	// parsing inconsistency errors.
	//
	/////////////////

	if (oldList) {
		// point to start of the old meta list, the first and only
		// record in the oldList
		char *om = oldList;

		// the size
		int32_t osize = oldListSize;

		// the end
		char *omend = om + osize;
		int32_t needx = 0;

		HashTableX dt8;
		char dbuf8[34900];

		// value is the ptr to the rdbId/key in the oldList
		dt8.set(8, sizeof(char *), 2048, dbuf8, 34900, false, "dt8-tab");

		// scan recs in that and hash them
		for (char *p = om; p < omend;) {
			// save this
			char byte = *p;
			char *rec = p;

			// get the rdbid for this rec
			rdbid_t rdbId = (rdbid_t)(byte & 0x7f);
			p++;

			// get the key size
			int32_t ks = getKeySizeFromRdbId(rdbId);

			// get that
			char *k = p;

			// unlike a real meta list, this meta list has
			// no data field, just rdbIds and keys only! because
			// we only use it for deleting, which only requires
			// a key and not the data
			p += ks;

			// tally this up in case we have to add the delete
			// version of this key back (add 1 for rdbId)
			needx += ks + 1;

			// for linkdb, sometimes we also add a "lost" link
			// key in addition to deleting the old key! see below
			if (rdbId == RDB_LINKDB) {
				needx += ks + 1;
			}

			// do not add it if datasize > 0
			// do not include discovery or lost dates in the linkdb key...
			uint64_t hk = (rdbId == RDB_LINKDB) ? hash64(k + 12, ks - 12) : hash64(k, ks);

			// sanity check
			if (rdbId == RDB_LINKDB && Linkdb::getLinkerDocId_uk((key224_t *)k) != m_docId) {
				g_process.shutdownAbort(true);
			}

			/// @todo ALC we're allocating too much here, we can only have 1 del key per doc when index is used

			Rdb *rdb = getRdbFromId(rdbId);
			if (rdb->isUseIndexFile()) {
				// Do not store records in the hash table of old values when we're using index file.
				// This makes sure that no delete records are stored in rdb for existing records,
				// which is needed for the new no-merge feature.
				/// @todo ALC verify that we need to cater for secondary rdb use for rebuild
				if (rdbId == RDB_POSDB || rdbId == RDB2_POSDB2) {
					continue;
				}

				/// @todo ALC we need to cater for other rdb file down below
				gbshutdownLogicError();
			}

			if (!dt8.addKey(&hk, &rec)) {
				logTrace(g_conf.m_logTraceXmlDoc, "addKey failed");
				return NULL;
			}
		}

		// also need all the new keys just to be sure, in case none
		// are already in the rdbs
		needx += (m_p - m_metaList);

		// now alloc for our new manicured metalist
		char *nm = (char *)mmalloc(needx, "newmeta");
		if (!nm) {
			logTrace(g_conf.m_logTraceXmlDoc, "mmalloc failed");
			return NULL;
		}

		char *nptr = nm;
		char *nmax = nm + needx;

		// scan each rec in the current meta list, see if its in either
		// the dt12 or dt16 hash table, if it already is, then
		// do NOT add it to the new metalist, nm, because there is
		// no need to.
		char *p    = m_metaList;
		char *pend = p + (m_p - m_metaList);
		for (; p < pend;) {
			// save it with the flag
			char byte = *p;

			// get rdbId
			rdbid_t rdbId = (rdbid_t)(byte & 0x7f);
			p++;

			// key size
			int32_t ks = getKeySizeFromRdbId(rdbId);

			// get key
			char *key = p;
			p += ks;

			// . if key is negative, no data is present
			// . the doledb key is negative for us here
			bool isDel = ((key[0] & 0x01) == 0x00);
			int32_t ds = isDel ? 0 : getDataSizeFromRdbId(rdbId);

			// if datasize variable, read it in
			if (ds == -1) {
				// get data size
				ds = *(int32_t *)p;

				// skip data size int32_t
				p += 4;
			}

			// point to data
			char *data = p;

			// skip data if not zero
			p += ds;

			// mix it up for hashtable speed
			// skip if for linkdb, we do that below
			uint64_t hk = (rdbId == RDB_LINKDB) ? hash64(key + 12, ks - 12) : hash64(key, ks);

			// was this key already in the "old" list?
			int32_t slot = dt8.getSlot(&hk);

			// see if already in an rdb, IFF dataless, otherwise
			// the keys might be the same but with different data!
			if (slot >= 0) {
				// remove from hashtable so we do not add it
				// as a delete key below
				dt8.removeSlot(slot);

				if (rdbId == RDB_LINKDB) {
					// all done for this key
					continue;
				}

				// but do add like a titledb rec that has the
				// same key, because its data is probably
				// different...
				// HACK: enable for now since we lost
				// the url:www.geico.com term somehow!!!
				// geico got deleted but not the title rec!!
				// MAKE SURE TITLEREC gets deleted then!!!
				if (ds == 0 && g_conf.m_doIncrementalUpdating) {
					continue;
				}
			}

			// ok, it is not already in an rdb, so add it
			*nptr++ = byte;

			// store key
			gbmemcpy ( nptr, key , ks );

			// skip over it
			nptr += ks;

			// store data
			if (ds) {
				// store data size
				*(int32_t *)nptr = ds;
				nptr += 4;

				gbmemcpy (nptr, data, ds);
				nptr += ds;
			}
		}

		// now scan dt8 and add their keys as del keys
		for ( int32_t i = 0 ; i < dt8.m_numSlots ; i++ ) {
			// skip if empty
			if (!dt8.m_flags[i]) {
				continue;
			}

			// store rdbid first
			char *rec = *(char **)dt8.getValueFromSlot(i);

			// get rdbId with hi bit possibly set
			rdbid_t rdbId = (rdbid_t)(rec[0] & 0x7f);

			// key size
			int32_t ks = getKeySizeFromRdbId(rdbId);

			// sanity test - no negative keys
			if ((rec[1] & 0x01) == 0x00) {
				g_process.shutdownAbort(true);
			}

			// copy the rdbId byte and key
			gbmemcpy ( nptr , rec , 1 + ks );

			// skip over rdbid
			nptr++;

			// make it a negative key by clearing lsb
			*nptr = *nptr & 0xfe;

			// skip it
			nptr += ks;

			// if it is from linkdb, and unmet, then it is a
			// lost link, so set the lost date of it. we keep
			// these so we can graph lost links
			if (rdbId == RDB_LINKDB) {
				// the real linkdb rec is at rec+1
				int32_t lost = Linkdb::getLostDate_uk( rec+1 );

				// how can it be non-zero? it should have
				// been freshly made from the old titlerec...
				if (lost) {
					g_process.shutdownAbort(true);
				}

				// copy the rdbId byte and key
				gbmemcpy ( nptr , rec , 1 + ks );

				// set it in there now
				Linkdb::setLostDate_uk(nptr+1,now);

				// carry it through on revdb, do not delete
				// it! we want a linkdb history for seomasters
				nptr += 1 + ks;

				// and go on to delete the old linkdb key that
				// did not have a lost date
				//continue;
			}
		}

		/// @todo ALC we need to handle delete keys for other rdb types

		// we need to add delete key per document when it's deleted (with term 0)
		if (g_conf.m_noInMemoryPosdbMerge && !m_isInIndex) {
			char key[MAX_KEY_BYTES];

			// add posdb delete key
			Posdb::makeStartKey(&key, 0, *od->getDocId());
			*nptr++ = RDB_POSDB;
			memcpy(nptr, &key, sizeof(posdbkey_t));
			nptr += sizeof(posdbkey_t);
		}

		// sanity. check for metalist breach
		if (nptr > nmax) {
			g_process.shutdownAbort(true);
		}

		// free the old meta list
		mfree(m_metaList, m_metaListAllocSize, "fm");

		// now switch over to the new one
		m_metaList = nm;
		m_metaListAllocSize = needx;
		m_p = nptr;
	}

	//
	// repeat this logic special for linkdb since we keep lost links
	// and may update the discovery date or lost date in the keys
	//
	// 1. hash keys of old linkdb keys into dt9 here
	// 2. do not hash the discovery/lost dates when making key hash for dt9
	// 3. scan keys in meta list and add directly into new meta list
	//    if not in dt9
	// 4. if in dt9 then add dt9 key instead
	// 5. remove dt9 keys as we add them
	// 6. then add remaining dt9 keys into meta list but with lost date
	//    set to now UNLESS it's already set
	//

	//
	// validate us!
	//
	m_metaListValid = true;

	// set the list size, different from the alloc size
	m_metaListSize = m_p - m_metaList;

	// sanity check
	verifyMetaList(m_metaList, m_metaList + m_metaListSize, forDelete);

	// all done
	logTrace(g_conf.m_logTraceXmlDoc, "END, all done");
	return m_metaList;
}

// . copy from old title rec to us to speed things up!
// . returns NULL and set g_errno on error
// . returns -1 if blocked
// . returns 1 otherwise
// . when to doc content is unchanged, just inherit crap from the old title
//   rec so we can make the spider reply in getNewSpiderReply()
void XmlDoc::copyFromOldDoc ( XmlDoc *od ) {
	// skip if none
	if ( ! od ) return;
	// skip if already did it
	if ( m_copied1 ) return;
	// do not repeat
	m_copied1 = true;
	// set these
	m_percentChanged      = 0;
	m_percentChangedValid = true;

	// copy over bit members
	m_contentHash32 = od->m_contentHash32;
	//m_tagHash32     = od->m_tagHash32;
	m_tagPairHash32 = od->m_tagPairHash32;
	m_httpStatus    = od->m_httpStatus;
	m_isRSS         = od->m_isRSS;
	m_isPermalink   = od->m_isPermalink;
	m_hopCount      = od->m_hopCount;
	m_crawlDelay    = od->m_crawlDelay;

	// do not forget the shadow members of the bit members
	m_isRSS2         = m_isRSS;
	m_isPermalink2   = m_isPermalink;

	// validate them
	m_contentHash32Valid = true;
	//m_tagHash32Valid     = true;
	m_tagPairHash32Valid = true;
	m_httpStatusValid    = true;
	m_isRSSValid         = true;
	m_isPermalinkValid   = true;
	m_hopCountValid      = true;
	m_crawlDelayValid    = true;

	m_langId        = od->m_langId;

	m_langIdValid       = true;

	// so get sitenuminlinks doesn't crash when called by getNewSpiderReply
	// because dns timed out. it timed out with EDNSTIMEDOUT before.
	// so overwrite it here...
	if ( m_ip == -1 || m_ip == 0 || ! m_ipValid ) {
		m_ip                  = od->m_ip;
		m_ipValid             = true;
		m_siteNumInlinks            = od->m_siteNumInlinks;
		m_siteNumInlinksValid = od->m_siteNumInlinksValid;
	}

	m_indexCode      = 0;//od->m_indexCode;
	m_indexCodeValid = true;

	// we need the link info too!
	ptr_linkInfo1  = od->ptr_linkInfo1;
	size_linkInfo1 = od->size_linkInfo1;
	if ( ptr_linkInfo1 && size_linkInfo1 ) m_linkInfo1Valid = true;
	else m_linkInfo1Valid = false;
}

// for adding a quick reply for EFAKEIP and for diffbot query reindex requests
SpiderReply *XmlDoc::getFakeSpiderReply ( ) {

	if ( ! m_tagRecValid ) {
		m_tagRec.reset();
		m_tagRecValid = true;
	}

	if ( ! m_siteHash32Valid ) {
		m_siteHash32 = 1;
		m_siteHash32Valid = true;
	}

	if ( ! m_downloadEndTimeValid ) {
		m_downloadEndTime = 0;
		m_downloadEndTimeValid = true;
	}

	if ( ! m_ipValid ) {
		m_ipValid = true;
		m_ip = atoip("1.2.3.4");
	}

	if ( ! m_spideredTimeValid ) {
		m_spideredTimeValid = true;
		m_spideredTime = getTimeGlobal();//0; use now!
	}

	// if doing diffbot query reindex
	// TODO: does this shard the request somewhere else???
	if ( ! m_firstIpValid ) {
		m_firstIp = m_ip;//atoip("1.2.3.4");
		m_firstIpValid = true;
	}

	// this was causing nsr to block and core below on a bad engineer
	// error loading the old title rec
	if ( ! m_isPermalinkValid ) {
		m_isPermalink = false;
		m_isPermalinkValid = true;
	}

	//if ( ! m_sreqValid ) {
	// 	m_sreqValid = true;
	// 	m_sreq.m_parentDocId = 0LL;
	// }


	// if error is EFAKEFIRSTIP, do not core
	//if ( ! m_isIndexedValid ) {
	//	m_isIndexed = false;
	//	m_isIndexedValid = true;
	//}

	// if this is EABANDONED or ECORRUPTDATA (corrupt gzip reply)
	// then this should not block. we need a spiderReply to release the
	// url spider lock in SpiderLoop::m_lockTable.
	// if m_isChildDoc is true, like for diffbot url, this should be
	// a bogus one.
	SpiderReply *nsr = getNewSpiderReply ();
	if ( nsr == (void *)-1) { g_process.shutdownAbort(true); }
	if ( ! nsr ) {
		log("doc: crap, could not even add spider reply "
		    "to indicate internal error: %s",mstrerror(g_errno));
		if ( ! g_errno ) g_errno = EBADENGINEER;
		//return true;
		return NULL;
	}

	return nsr;

	//if ( nsr->getRecSize() <= 1) { g_process.shutdownAbort(true); }

	//CollectionRec *cr = getCollRec();
	//if ( ! cr ) return true;
}

// getSpiderReply()
SpiderReply *XmlDoc::getNewSpiderReply ( ) {

	if ( m_srepValid ) return &m_srep;

	setStatus ( "getting spider reply" );

	// diffbot guys, robots.txt, frames, sshould not be here
	if ( m_isChildDoc ) { g_process.shutdownAbort(true); }

	// . get the mime first
	// . if we are setting XmlDoc from a titleRec, this causes
	//   doConsistencyCheck() to block and core
	//HttpMime *mime = getMime();
	//if ( ! mime || mime == (HttpMime *)-1 ) return (SpiderReply *)mime;

	// if we had a critical error, do not do this
	int32_t *indexCode = getIndexCode();
	if (! indexCode || indexCode == (void *)-1)
		return (SpiderReply *)indexCode;

	TagRec *gr = getTagRec();
	if ( ! gr || gr == (TagRec *)-1 ) return (SpiderReply *)gr;

	// can't call getIsPermalink() here without entering a dependency loop
	//char *pp = getIsUrlPermalinkFormat();
	//if ( !pp || pp == (char *)-1 ) return (SpiderReply *)pp;

	// the site hash
	int32_t *sh32 = getSiteHash32();
	if ( ! sh32 || sh32 == (int32_t *)-1 ) return (SpiderReply *)sh32;

	int64_t *de = getDownloadEndTime();
	if ( ! de || de == (void *)-1 ) return (SpiderReply *)de;

	// shortcut
	Url *fu = NULL;
	// watch out for titlerec lookup errors for docid based spider reqs
	if ( m_firstUrlValid ) fu = getFirstUrl();

	// reset
	m_srep.reset();

	int32_t firstIp = -1;
	// inherit firstIp
	Tag *tag = m_tagRec.getTag("firstip");
	// tag must be there?
	if ( tag ) firstIp = atoip(tag->getTagData());

	// this is usually the authority
	if ( m_firstIpValid )
		firstIp = m_firstIp;

	// otherwise, inherit from oldsr to be safe
	// BUT NOT if it was a fakeip and we were injecting because
	// the SpiderRequest was manufactured and not actually taken
	// from spiderdb! see XmlDoc::injectDoc() because that is where
	// it came from!! if it has m_sreq.m_isAddUrl and
	// m_sreq.m_fakeFirstIp then we actually do add the reply with that
	// fake ip so that they will exist in the same shard.
	// BUT if it is docid pased from PageReindex.cpp (a query reindex)
	// we set the injection bit and the pagereindex bit, we should let
	// thise guys keep the firstip because the docid-based spider request
	// is in spiderdb. it needs to match up.
	if ( m_sreqValid && (!m_sreq.m_isInjecting||m_sreq.m_isPageReindex) )
		firstIp = m_sreq.m_firstIp;

	// sanity
	if ( firstIp == 0 || firstIp == -1 ) {
		if ( m_firstUrlValid )
			log("xmldoc: BAD FIRST IP for %s",m_firstUrl.getUrl());
		else
			log("xmldoc: BAD FIRST IP for %" PRId64,m_docId);
		firstIp = 12345;
		//g_process.shutdownAbort(true); }
	}
	// store it
	m_srep.m_firstIp = firstIp;
	// assume no error
	// MDW: not right...
	m_srep.m_errCount = 0;
	// otherwise, inherit from oldsr to be safe
	//if ( m_sreqValid )
	//	m_srep.m_firstIp = m_sreq.m_firstIp;

	// do not inherit this one, it MIGHT HAVE CHANGE!
	m_srep.m_siteHash32 = m_siteHash32;

	// need this for updating crawl delay table, m_cdTable in Spider.cpp
	if ( fu ) m_srep.m_domHash32  = getDomHash32();
	else      m_srep.m_domHash32  = 0;

	if ( ! m_tagRecValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_ipValid                   ) { g_process.shutdownAbort(true); }
	if ( ! m_siteHash32Valid           ) { g_process.shutdownAbort(true); }
	//if ( ! m_spideredTimeValid         ) { g_process.shutdownAbort(true); }

	// . set other fields besides key
	// . crap! if we are the "qatest123" collection then m_spideredTime
	//   was read from disk usually and is way in the past! watch out!!
	m_srep.m_spideredTime = getSpideredTime();//m_spideredTime;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;


	// TODO: expire these when "ownershipchanged" tag is newer!!

	if ( gr->getTag ( "authorityinlink" ) )
		m_srep.m_hasAuthorityInlink = 1;
	// automatically valid either way
	m_srep.m_hasAuthorityInlinkValid = 1;

	int64_t uh48        = 0LL;
	// we might be a docid based spider request so fu could be invalid
	// if the titlerec lookup failed
	if ( fu ) uh48 = hash64b(fu->getUrl()) & 0x0000ffffffffffffLL;
	int64_t parentDocId = 0LL;
	if ( m_sreqValid )
		parentDocId = m_sreq.getParentDocId();

	// for docid based urls from PageReindex.cpp we have to make
	// sure to set the urlhash48 correctly from that.
	if ( m_sreqValid ) uh48 = m_sreq.getUrlHash48();

	// note it
	logDebug( g_conf.m_logDebugSpider, "xmldoc: uh48=%" PRIu64" parentdocid=%" PRIu64, uh48, parentDocId );

	// set the key, m_srep.m_key
	m_srep.setKey (  firstIp, parentDocId, uh48, false );

	// . did we download a page? even if indexcode is set we might have
	// . if this is non-zero that means its valid
	if ( m_contentHash32Valid )
		m_srep.m_contentHash32 = m_contentHash32;

	// injecting the content (url implied)
	if ( m_contentInjected ) // m_sreqValid && m_sreq.m_isInjecting )
		m_srep.m_fromInjectionRequest = 1;

	// can be injecting a url too, content not necessarily implied
	if ( m_sreqValid && m_sreq.m_isInjecting )
		m_srep.m_fromInjectionRequest = 1;

	// were we already in titledb before we started spidering?
	m_srep.m_wasIndexed = m_wasInIndex;

	// note whether m_wasIndexed is valid because if it isn't then
	// we shouldn't be counting this reply towards the page counts.
	// if we never made it this far i guess we should not forcibly call
	// getIsIndexed() at this point so our performance is fast in case
	// this is an EFAKEFIRSTIP error or something similar where we
	// basically just add this reply and we're done.
	// NOTE: this also pertains to SpiderReply::m_isIndexed.
	m_srep.m_wasIndexedValid = m_wasInIndexValid;

	// assume no change
	m_srep.m_isIndexed = m_isInIndex;

	// we need to know if the m_isIndexed bit is valid or not
	// because sometimes like if we are being called directly from
	// indexDoc() because of an error situation, we do not know!
	if ( m_isInIndexValid ) m_srep.m_isIndexedINValid = false;
	else                    m_srep.m_isIndexedINValid = true;

	// likewise, we need to know if we deleted it so we can decrement the
	// quota count for this subdomain/host in SpiderColl::m_quotaTable
	//if ( m_srep.m_wasIndexed ) m_srep.m_isIndexed = true;

	// treat error replies special i guess, since langId, etc. will be
	// invalid
	if ( m_indexCode ) {
		// validate
		m_srepValid = true;
		// set these items if valid already, but don't bother
		// trying to compute them, since we are not indexing.
		if ( m_siteNumInlinksValid       ) {
			m_srep.m_siteNumInlinks = m_siteNumInlinks;
			m_srep.m_siteNumInlinksValid = true;
		}
		//if ( m_percentChangedValid )
		//	m_srep.m_percentChangedPerDay = m_percentChanged;
		if ( m_crawlDelayValid && m_crawlDelay >= 0 )
			// we already multiply x1000 in isAllowed2()
			m_srep.m_crawlDelayMS = m_crawlDelay;// * 1000;
		else
			m_srep.m_crawlDelayMS = -1;
		//if ( m_pubDateValid     ) m_srep.m_pubDate = m_pubDate;
		                          m_srep.m_pubDate = 0;
		if ( m_langIdValid      ) m_srep.m_langId = m_langId;
		if ( m_isRSSValid       ) m_srep.m_isRSS = m_isRSS;
		if ( m_isPermalinkValid ) m_srep.m_isPermalink =m_isPermalink;
		if ( m_httpStatusValid  ) m_srep.m_httpStatus = m_httpStatus;
		// stuff that is automatically valid
		m_srep.m_isPingServer = 0;
		if ( fu ) m_srep.m_isPingServer = (bool)fu->isPingServer();
		// this was replaced by m_contentHash32
		//m_srep.m_newRequests  = 0;
		m_srep.m_errCode      = m_indexCode;
		if ( m_downloadEndTimeValid )
			m_srep.m_downloadEndTime = m_downloadEndTime;
		else
			m_srep.m_downloadEndTime = 0;
		// is the original spider request valid?
		if ( m_sreqValid ) {
			// preserve the content hash in case m_indexCode is
			// EDOCUNCHANGED. so we can continue to get that
			// in the future. also, if we had the doc indexed,
			// just carry the contentHash32 forward for the other
			// errors like EDNSTIMEDOUT or whatever.
			m_srep.m_contentHash32 = m_sreq.m_contentHash32;
			// shortcuts
			SpiderReply   *n = &m_srep;
			SpiderRequest *o = &m_sreq;
			// more stuff
			n->m_hasAuthorityInlink = o->m_hasAuthorityInlink;
			n->m_isPingServer       = o->m_isPingServer;
			// the validator flags
			n->m_hasAuthorityInlinkValid =
				o->m_hasAuthorityInlinkValid;
			// get error count from original spider request
			int32_t newc = m_sreq.m_errCount;
			// inc for us, since we had an error
			newc++;
			// contain to one byte
			if ( newc > 255 ) newc = 255;
			// store in our spiderreply
			m_srep.m_errCount = newc;
		}
		// . and do not really consider this an error
		// . i don't want the url filters treating it as an error reply
		// . m_contentHash32 should have been carried forward from
		//   the block of code right above
		if ( m_indexCode == EDOCUNCHANGED ) {
			// we should have had a spider request, because that's
			// where we got the m_contentHash32 we passed to
			// Msg13Request.
			if ( ! m_sreqValid ) { g_process.shutdownAbort(true); }
			// make it a success
			m_srep.m_errCode = 0;
			// and no error count, it wasn't an error per se
			m_srep.m_errCount = 0;
			// call it 200
			m_srep.m_httpStatus = 200;
		}
		// copy flags and data from old doc...
		if ( m_indexCode == EDOCUNCHANGED &&
		     m_oldDocValid &&
		     m_oldDoc ) {
			//m_srep.m_pubDate        = m_oldDoc->m_pubDate;
			m_srep.m_pubDate        = 0;
			m_srep.m_langId         = m_oldDoc->m_langId;
			m_srep.m_isRSS          = m_oldDoc->m_isRSS;
			m_srep.m_isPermalink    = m_oldDoc->m_isPermalink;
			m_srep.m_siteNumInlinks = m_oldDoc->m_siteNumInlinks;
			// they're all valid
			m_srep.m_siteNumInlinksValid = true;
		}
		// do special things if
		return &m_srep;
	}

	// this will help us avoid hammering ips & respect same ip wait
	if ( ! m_downloadEndTimeValid ) { g_process.shutdownAbort(true); }
	m_srep.m_downloadEndTime      = m_downloadEndTime;

	// . if m_indexCode was 0, we are indexed then...
	// . this logic is now above
	//m_srep.m_isIndexed = 1;

	// get ptr to old doc/titlerec
	XmlDoc **pod = getOldXmlDoc ( );
	if ( ! pod || pod == (XmlDoc **)-1 ) return (SpiderReply *)pod;
	// this is non-NULL if it existed
	XmlDoc *od = *pod;

	// status is -1 if not found
	int16_t *hs = getHttpStatus ();
	if ( ! hs || hs == (void *)-1 ) return (SpiderReply *)hs;

	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 ) return (SpiderReply *)sni;

	float *pc = getPercentChanged();
	if ( ! pc || pc == (void *)-1 ) return (SpiderReply *)pc;

	// get the content type
	uint8_t *ct = getContentType();
	if ( ! ct ) return NULL;
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (SpiderReply *)isRoot;



	uint8_t *langId = getLangId();
	if ( ! langId || langId == (uint8_t *)-1 )
		return (SpiderReply *)langId;

	char *isRSS   = getIsRSS();
	if ( ! isRSS || isRSS == (char  *)-1 )
		return (SpiderReply *)isRSS;

	char *pl = getIsPermalink();
	if ( ! pl || pl == (char *)-1 )
		return (SpiderReply *)pl;

	// this is only know if we download the robots.tt...
	if ( od && m_recycleContent ) {
		m_crawlDelay = od->m_crawlDelay;
		m_crawlDelayValid = true;
	}

	// sanity checks
	//if(! m_sreqValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_siteNumInlinksValid       ) { g_process.shutdownAbort(true); }
	if ( ! m_hopCountValid             ) { g_process.shutdownAbort(true); }
	if ( ! m_langIdValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_isRSSValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_isPermalinkValid          ) { g_process.shutdownAbort(true); }
	//if ( ! m_pageNumInlinksValid     ) { g_process.shutdownAbort(true); }
	if ( ! m_percentChangedValid       ) { g_process.shutdownAbort(true); }
	//if ( ! m_isSpamValid               ) { g_process.shutdownAbort(true); }
	//if ( ! m_crawlDelayValid           ) { g_process.shutdownAbort(true); }

	// httpStatus is -1 if not found (like for empty http replies)
	m_srep.m_httpStatus = *hs;

	// zero if none
	//m_srep.m_percentChangedPerDay = 0;
	// . only if had old one
	// . we use this in url filters to set the respider wait time usually
	if ( od ) {
		int32_t spideredTime = getSpideredTime();
		int32_t oldSpideredTime = od->getSpideredTime();
		float numDays = spideredTime - oldSpideredTime;
		m_srep.m_percentChangedPerDay = (m_percentChanged+.5)/numDays;
	}

	// . update crawl delay, but we must store now as milliseconds
	//   because Spider.cpp like it better that way
	// . -1 implies crawl delay unknown or not found
	if ( m_crawlDelay >= 0 && m_crawlDelayValid )
		// we already multiply x1000 in isAllowed2()
		m_srep.m_crawlDelayMS = m_crawlDelay;// * 1000;
	else
		// -1 means invalid/unknown
		m_srep.m_crawlDelayMS = -1;

	// . we use this to store "bad" spider recs to keep from respidering
	//   a "bad" url over and over again
	// . it is up to the url filters whether they want to retry this
	//   again or not!
	// . TODO: how to represent "ETCPTIMEDOUT"????
	// . EUDPTIMEDOUT, EDNSTIMEDOUT, ETCPTIMEDOUT, EDNSDEAD, EBADIP,
	//   ENETUNREACH,EBADMIME,ECONNREFUED,ECHOSTUNREACH
	m_srep.m_siteNumInlinks       = m_siteNumInlinks;
	//m_srep.m_pubDate              = *pubDate;
	m_srep.m_pubDate              = 0;
	// this was replaced by m_contentHash32
	//m_srep.m_newRequests          = 0;
	m_srep.m_langId               = *langId;
	m_srep.m_isRSS                = (bool)*isRSS;
	m_srep.m_isPermalink          = (bool)*pl;
        m_srep.m_isPingServer         = (bool)fu->isPingServer();
	//m_srep.m_isSpam             = m_isSpam;

	m_srep.m_siteNumInlinksValid = true;

	// validate all
	m_srep.m_hasAuthorityInlinkValid = 1;

	// a quick validation. reply must unlock the url from the lock table.
	// so the locks must be equal.
	if ( m_sreqValid &&
	     // we create a new spiderrequest if injecting with a fake firstip
	     // so it will fail this test...
	     ! m_sreq.m_isInjecting ) {
		int64_t lock1 = makeLockTableKey(&m_sreq);
		int64_t lock2 = makeLockTableKey(&m_srep);
		if ( lock1 != lock2 ) {
			log("build: lock1 != lock2 lock mismatch for %s",
			    m_firstUrl.getUrl());
			g_process.shutdownAbort(true);
		}
	}

	// validate
	m_srepValid = true;

	return &m_srep;
}

// . so Msg20 can see if we are banned now or not...
// . we must skip certain rules in getUrlFilterNum() when doing to for Msg20
//   because things like "parentIsRSS" can be both true or false since a url
//   can have multiple spider recs associated with it!
void XmlDoc::setSpiderReqForMsg20 ( SpiderRequest *sreq   ,
				    SpiderReply   *srep   ) {

	// sanity checks
	if ( ! m_ipValid                   ) { g_process.shutdownAbort(true); }
	if ( ! m_hopCountValid             ) { g_process.shutdownAbort(true); }
	if ( ! m_langIdValid               ) { g_process.shutdownAbort(true); }
	if ( ! m_isRSSValid                ) { g_process.shutdownAbort(true); }
	if ( ! m_isPermalinkValid          ) { g_process.shutdownAbort(true); }

	Url *fu = getFirstUrl();

	// reset
	sreq->reset();
	// assume not valid
	sreq->m_siteNumInlinks = -1;

	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }
	// how many site inlinks?
	sreq->m_siteNumInlinks       = m_siteNumInlinks;
	sreq->m_siteNumInlinksValid  = true;

	// set other fields besides key
	sreq->m_firstIp              = m_ip;
	sreq->m_hostHash32           = m_hostHash32a;
	sreq->m_hopCount             = m_hopCount;

	sreq->m_pageNumInlinks       = 0;//m_sreq.m_parentFirstIp;

	sreq->m_isAddUrl             = 0;//m_isAddUrl;
	sreq->m_isPingServer         = fu->isPingServer();
	//sreq->m_isUrlPermalinkFormat = m_isUrlPermalinkFormat;

	// transcribe from old spider rec, stuff should be the same
	sreq->m_addedTime          = m_firstIndexedDate;

	// validate the stuff so getUrlFilterNum() acks it
	sreq->m_hopCountValid = 1;

	srep->reset();

	srep->m_spideredTime         = getSpideredTime();//m_spideredTime;
	//srep->m_isSpam             = isSpam; // real-time update this!!!
	srep->m_isRSS                = m_isRSS;
	srep->m_isPermalink          = m_isPermalink;
	srep->m_httpStatus           = 200;
	//srep->m_retryNum           = 0;
	srep->m_langId               = m_langId;
	srep->m_percentChangedPerDay = 0;//m_percentChanged;

	// we need this now for ucp ucr upp upr new url filters that do
	// substring matching on the url
	if ( m_firstUrlValid )
		strcpy(sreq->m_url,m_firstUrl.getUrl());
}


// . add the spiderdb recs to the meta list
// . used by XmlDoc::setMetaList()
// . returns NULL and sets g_errno on error
// . otherwise returns the "new p"
// . if Scraper.cpp or PageAddUrl.cpp and Msg7.cpp should all use the XmlDoc
//   class even if just adding links. they should make a fake html page and
//   "inject" it, with only m_useSpiderdb set to true...
char *XmlDoc::addOutlinkSpiderRecsToMetaList ( ) {

	logTrace( g_conf.m_logTraceXmlDoc, "BEGIN" );

	if ( m_doingConsistencyCheck ) { g_process.shutdownAbort(true); }

	// do not do this if recycling content
	// UNLESS REBUILDING...
	if ( m_recycleContent && ! m_useSecondaryRdbs )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, rebuilding" );
		return (char *)0x01;
	}


	// for now skip in repair tool
	if ( m_useSecondaryRdbs && ! g_conf.m_rebuildAddOutlinks )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, in repair mode" );
		return (char *)0x01;
	}


	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getXml failed" );
		return (char *)xml;
	}

	Links *links = getLinks();
	if ( ! links || links == (Links *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinks failed" );
		return (char *)links;
	}

	char *spiderLinks = getSpiderLinks();
	if ( ! spiderLinks || spiderLinks == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getSpiderLinks failed" );
		return (char *)spiderLinks;
	}

	TagRec ***grv = getOutlinkTagRecVector();
	if ( ! grv || grv == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getOutlinkTagRecVector failed" );
		return (char *)grv;
	}

	int32_t    **ipv = getOutlinkFirstIpVector();
	if ( ! ipv || ipv == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "getOutlinkFirstIpVector failed" );
		return (char *)ipv;
	}

	char     *ipi = getIsIndexed(); // is the parent indexed?
	if ( ! ipi || ipi == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getIsIndexed failed" );
		return (char *)ipi;
	}

	// need this
	int32_t parentDomHash32 = getDomHash32();
	if ( parentDomHash32 != m_domHash32 ) { g_process.shutdownAbort(true); }

	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getIsSiteRoot failed" );
		return (char *)isRoot;
	}

	int32_t *psni = getSiteNumInlinks();
	if ( ! psni || psni == (int32_t *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getSiteNumInlinks failed" );
		return (char *)psni;
	}

	int32_t *pfip = getFirstIp();
	if ( ! pfip || pfip == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getFirstIp failed" );
		return (char *)pfip;
	}

	int64_t *d = getDocId();
	if ( ! d || d == (int64_t *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getDocId failed" );
		return (char *)d;
	}

	Url *fu = getFirstUrl();
	if ( ! fu || fu == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getFirstUrl failed" );
		return (char *)fu;
	}

	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getCurrentUrl failed" );
		return (char *)cu;
	}

	uint8_t *langId = getLangId();
	if ( ! langId || langId == (uint8_t *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLangId failed" );
		return (char *)langId;
	}

	// so linkSites[i] is site for link #i in Links.cpp class
	int32_t *linkSiteHashes = getLinkSiteHashes ( );
	if ( ! linkSiteHashes || linkSiteHashes == (void *)-1 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, getLinkSiteHashes failed" );
		return (char *)linkSiteHashes;
	}


	XmlDoc  *nd  = this;

	// set "od". will be NULL if no old xml doc, i.e. no old title rec
	//XmlDoc **pod = getOldXmlDoc ( );
	//if ( ! pod || pod == (void *)-1 ) return (char *)pod;
	//XmlDoc  *od  = *pod;

	//	onlyInternal = true;

	bool    isParentRSS       = false;
	// PageAddUrl.cpp does not supply a valid new doc, so this is NULL
	if ( nd ) {
		isParentRSS       = *nd->getIsRSS() ;
	}

	int32_t n = links->m_numLinks;
	// return early if nothing to do. do not return NULL though cuz we
	// do not have g_errno set!
	if ( n <= 0 )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, no links to add (%" PRId32").", n);
		return (char *)0x01;
	}

	// sanity checks
	if ( ! m_ipValid             ) { g_process.shutdownAbort(true); }
	if ( ! m_domHash32Valid      ) { g_process.shutdownAbort(true); }
	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }
	if ( ! m_hostHash32aValid    ) { g_process.shutdownAbort(true); }
	if ( ! m_siteHash32Valid     ) { g_process.shutdownAbort(true); }
	if ( ! m_hopCountValid       ) { g_process.shutdownAbort(true); }
	//if ( ! m_spideredTimeValid   ) { g_process.shutdownAbort(true); }

	int64_t myUh48 = m_firstUrl.getUrlHash48();

	// . pre-allocate a buffer to hold the spider recs
	// . taken from SpiderRequest::store()
	int32_t size = 0;
	for ( int32_t i = 0 ; i < n ; i++ )
		size += SpiderRequest::getNeededSize ( links->getLinkLen(i) );

	// append spider recs to this list ptr
	char *p = m_p;

	// hash table to avoid dups
	HashTableX ht;
	char buf2[8192];
	if ( ! ht.set ( 4,0,1000,buf2 , 8192,false,"linkdedup" ) )
	{
		logTrace( g_conf.m_logTraceXmlDoc, "END, ht.set failed" );
		return NULL;
	}

	// count how many we add
	int32_t numAdded = 0;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, getCollRec failed" );
		return NULL;
	}

	bool avoid = false;

	// if this is a simplified redir and we should not be spidering
	// links then turn it off as well! because we now add simplified
	// redirects back into spiderdb using this function.
	if ( m_spiderLinksValid && ! m_spiderLinks )
		avoid = true;

	// for diffbot crawlbot, if we are a seed url and redirected to a
	// different domain... like bn.com --> barnesandnoble.com
	int32_t redirDomHash32  = 0;
	int32_t redirHostHash32 = 0;
	//int32_t redirSiteHash32 = 0;
	if ( m_hopCount == 0 &&
	     m_redirUrlValid &&
	     ptr_redirUrl &&
	     //m_redirUrlPtr && (this gets reset to NULL as being LAST redir)
	     // this is the last non-empty redir here:
	     m_redirUrl.getUrlLen() > 0 ) {
		log("build: seed REDIR: %s",m_redirUrl.getUrl());
		redirDomHash32  = m_redirUrl.getDomainHash32();
		redirHostHash32 = m_redirUrl.getHostHash32();
	}

	logTrace( g_conf.m_logTraceXmlDoc, "Handling %" PRId32" links", n);

	bool is_privacore = (strcmp(cr->m_urlFiltersProfile.getBufStart(), "privacore") == 0);

	//
	// serialize each link into the metalist now
	//
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// grab our info
		TagRec *gr        = (*grv)[i];
		int32_t    firstIp   = (*ipv)[i];

		// ip lookup failed? do not add to spiderdb then
		if ( firstIp == 0 || firstIp == -1 ) continue;

		// get flags
		linkflags_t flags = links->m_linkFlags[i];

		// . skip if we are rss page and this link is an <a href> link
		// . we only harvest <link> urls from rss feeds, not href links
		// . or in the case of feedburner, those orig tags
		if ( isParentRSS && (flags & LF_AHREFTAG) ) continue;

		// if we have a <feedburner:origLink> tag, then ignore <link>
		// tags and only get the links from the original links
		if ( links->m_isFeedBurner && !(flags & LF_FBTAG) ) continue;

		// do not add self links, pointless
		if ( flags & LF_SELFLINK ) continue;

		// do not add if no follow
		if ( flags & LF_NOFOLLOW ) continue;

		// point to url
		char *s    = links->getLink   (i);
		int32_t  slen = links->getLinkLen(i);

		// get hash
		int32_t uh = hash32 ( s , slen );

		// it does not like keys of 0, that means empty slot
		if ( uh == 0 ) uh = 1;

		// skip if dup
		if ( ht.isInTable ( &uh ) ) continue;

		// add it, returns false and sets g_errno on error
		if ( ! ht.addKey ( &uh ) ) return NULL;

		// we now supports HTTPS
		if ( strncmp(s,"http://",7) && strncmp(s,"https://",8) )
			continue;

		// . do not add if "old"
		// . Links::set() calls flagOldOutlinks()
		// . that just means we probably added it the last time
		//   we spidered this page
		// . no cuz we might have a different siteNumInlinks now
		//   and maybe this next hop count is now allowed where as
		//   before it was not!
		//if ( flags & LF_OLDLINK ) continue;

		Url url;
		url.set( s, slen );

		// if hostname length is <= 2 then SILENTLY reject it
		if ( url.getHostLen() <= 2 ) continue;

		// BR 20160125: Do not create spiderdb entries for media URLs etc.
		if(	url.hasNonIndexableExtension(TITLEREC_CURRENT_VERSION) ||
			url.hasScriptExtension() ||
			url.hasJsonExtension() ||
//			url.hasXmlExtension() ||
			url.isDomainUnwantedForIndexing() ||
			url.isPathUnwantedForIndexing() )
		{
			logTrace( g_conf.m_logTraceXmlDoc, "Unwanted for indexing [%s]", url.getUrl());
			continue;
		}

		if (is_privacore) {
			// tld
			if (url.isTLDInPrivacoreBlacklist()) {
				logTrace( g_conf.m_logTraceXmlDoc, "Unwanted for indexing [%s]", url.getUrl());
				continue;
			}
		}

		// get # of inlinks to this site... if recorded...
		int32_t ksni = -1;
		Tag *st = NULL;
		if ( gr ) st = gr->getTag ("sitenuminlinks");
		if ( st ) ksni = atol(st->getTagData());

		int32_t hostHash32   = url.getHostHash32();
		// . consult our sitelinks.txt file
		// . returns -1 if not found
		int32_t min = g_tagdb.getMinSiteInlinks ( hostHash32 );

		// try with www if not there
		if ( min < 0 && ! url.hasSubdomain() ) {
			int32_t wwwHash32 = url.getHash32WithWWW();
			min = g_tagdb.getMinSiteInlinks ( wwwHash32 );
		}

		if ( min >= 0 && ksni < min )
			ksni = min;

		// get this
		bool issiteroot = isSiteRootFunc3 ( s , linkSiteHashes[i] );

		// get it quick
		bool ispingserver = url.isPingServer();
		int32_t domHash32    = url.getDomainHash32();

		// is link rss?
		bool isRSSExt = false;
		const char *ext = url.getExtension();
		if ( ext ) {
			if ( strcasecmp( ext, "rss" ) == 0 ) {
				isRSSExt = true;
			} else if ( strcasecmp( ext, "xml" ) == 0 ) {
				isRSSExt = true;
			} else if ( strcasecmp( ext, "atom" ) == 0 ) {
				isRSSExt = true;
			}
		}

		logTrace( g_conf.m_logTraceXmlDoc, "link is RSS [%s]", isRSSExt?"true":"false");

		// make the spider request rec for it
		SpiderRequest ksr;

		// to defaults (zero out)
		ksr.reset();

		// set other fields besides key
		ksr.m_firstIp          = firstIp;
		ksr.m_hostHash32       = hostHash32;
		ksr.m_domHash32        = domHash32;
		ksr.m_siteHash32       = linkSiteHashes[i];//siteHash32;
		ksr.m_siteNumInlinks   = ksni;
		ksr.m_siteNumInlinksValid = true;
		ksr.m_isRSSExt            = isRSSExt;

		// hop count is now 16 bits so do not wrap that around
		int32_t hc = m_hopCount + 1;
		if ( hc > 65535 ) hc = 65535;
		ksr.m_hopCount         = hc;

		// keep hopcount the same for redirs
		if ( m_indexCodeValid &&
		     ( m_indexCode == EDOCSIMPLIFIEDREDIR ||
		       m_indexCode == EDOCNONCANONICAL ) )
			ksr.m_hopCount = m_hopCount;

		if ( issiteroot   ) ksr.m_hopCount = 0;
		if ( ispingserver ) ksr.m_hopCount = 0;

		// validate it
		ksr.m_hopCountValid = true;

		ksr.m_addedTime        = getSpideredTime();//m_spideredTime;
		//ksr.m_lastAttempt    = 0;
		//ksr.m_errCode        = 0;

		ksr.m_pageNumInlinks   = 0;

		// get this
		bool isupf = ::isPermalink(NULL,&url,CT_HTML,NULL,isRSSExt);
		// set some bit flags. the rest are 0 since we call reset()
		if ( isupf        ) ksr.m_isUrlPermalinkFormat = 1;
		//if ( isIndexed    ) ksr.m_isIndexed          = 1;
		if ( ispingserver ) ksr.m_isPingServer         = 1;

		// is it like www.xxx.com/* (does not include www.xxx.yyy.com)
		// includes xxx.com/* however
		ksr.m_isWWWSubdomain = url.isSimpleSubdomain();


		// if parent is a root of a popular site, then it is considered
		// an authority linker.  (see updateTagdb() function above)

		//@todo BR: This is how site authority is decided. Improve?
		// the mere existence of authorityinlink tag is good
		if ( ( *isRoot && *psni >= 500 ) || ( gr->getTag("authorityinlink") ) ) {
			ksr.m_hasAuthorityInlink = 1;
		}
		ksr.m_hasAuthorityInlinkValid = true;

		// this is used for building dmoz. we just want to index
		// the urls in dmoz, not their outlinks.
		if ( avoid  ) ksr.m_avoidSpiderLinks = 1;

		// . if this is the 2nd+ time we were spidered and this outlink
		//   wasn't there last time, then set this!
		// . if this is the first time spidering this doc then set it
		//   to zero so that m_minPubDate is set to -1 when the outlink
		//   defined by "ksr" is spidered.
		if ( m_oldDocValid && m_oldDoc ) {
			int32_t oldSpideredTime = m_oldDoc->getSpideredTime();
			ksr.m_parentPrevSpiderTime = oldSpideredTime;
		} else {
			ksr.m_parentPrevSpiderTime = 0;
		}

		//
		// . inherit manual add bit if redirecting to simplified url
		// . so we always spider seed url even if prohibited by
		//   the regex, and even if it simplified redirects
		//
		if ( m_indexCodeValid &&
		     ( m_indexCode == EDOCSIMPLIFIEDREDIR ||
		       m_indexCode == EDOCNONCANONICAL ) &&
		     m_sreqValid ) {
			if ( m_sreq.m_isInjecting )
				ksr.m_isInjecting = 1;
			if ( m_sreq.m_isAddUrl )
				ksr.m_isAddUrl = 1;
		}

		// copy the url into SpiderRequest::m_url buffer
		strcpy(ksr.m_url,s);

		// this must be valid
		if ( ! m_docIdValid ) { g_process.shutdownAbort(true); }

		// set the key, ksr.m_key. isDel = false
		ksr.setKey ( firstIp, *d , false );

		// we were hopcount 0, so if we link to ourselves we override
		// our original hopcount of 0 with this guy that has a
		// hopcount of 1. that sux... so don't do it.
		if ( ksr.getUrlHash48() == myUh48 ) continue;

		// . technically speaking we do not have any reply so we
		//   should not be calling this! cuz we don't have all the info
		// . see if banned or filtered, etc.
		// . at least try to call it. getUrlFilterNum() should
		//   break out and return -1 if it encounters a filter rule
		//   that it does not have enough info to answer.
		//   so if your first X filters all map to a "FILTERED"
		//   priority and this url matches one of them we can
		//   confidently toss this guy out.
		// . show this for debugging!
		// int32_t ufn = ::getUrlFilterNum ( &ksr , NULL, m_spideredTime ,
		// 			       false, m_niceness, cr,
		// 			       false,//true , // outlink?
		// 			       NULL ); // quotatable
		// logf(LOG_DEBUG,"build: ufn=%" PRId32" for %s",
		//      ufn,ksr.m_url);

		// bad?
		//if ( ufn < 0 ) {
		//	log("build: link %s had bad url filter."
		//	    , ksr.m_url );
		//	g_errno = EBADENGINEER;
		//	return NULL;
		//}

		// debug
		if ( g_conf.m_logDebugUrlAttempts ) {
			// print the tag rec out into sb2
			SafeBuf sb2;
			if ( gr ) gr->printToBuf ( &sb2 );
			// get it
			//SafeBuf sb1;
			const char *action = "add";
			logf(LOG_DEBUG,
			     "spider: attempting to %s link. "
			     "%s "
			     "tags=%s "
			     "onpage=%s"
			     ,
			     action ,
			     ksr.m_url,
			     //sb1.getBufStart(),
			     sb2.getBufStart(),
			     m_firstUrl.getUrl());
		}

		// serialize into the buffer
		int32_t need = ksr.getRecSize();

		// sanity check
		if ( p + 1 + need > m_pend ) { g_process.shutdownAbort(true); }
		// store the rdbId
		if ( m_useSecondaryRdbs ) *p++ = RDB2_SPIDERDB2;
		else                      *p++ = RDB_SPIDERDB;

		// store the spider rec
		gbmemcpy ( p , &ksr , need );
		// skip it
		p += need;
		// count it
		numAdded++;
	}

	logTrace( g_conf.m_logTraceXmlDoc, "Added %" PRId32" links", numAdded);

	// save it
	m_numOutlinksAdded      = numAdded;
	m_numOutlinksAddedValid = true;

	// update end of list once we have successfully added all spider recs
	m_p = p;

	// return current ptr
	logTrace( g_conf.m_logTraceXmlDoc, "END, all done." );

	return m_p ;
}

int32_t XmlDoc::getSiteRank ( ) {
	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }
	return ::getSiteRank ( m_siteNumInlinks );
}

// . add keys/recs from the table into the metalist
// . we store the keys into "m_p" unless "buf" is given
bool XmlDoc::addTable144 ( HashTableX *tt1 , int64_t docId , SafeBuf *buf ) {

	// sanity check
	if ( tt1->m_numSlots ) {
		if ( tt1->m_ks != sizeof(key144_t) ) {g_process.shutdownAbort(true);}
		if ( tt1->m_ds != 4                ) {g_process.shutdownAbort(true);}
	}

	// assume we are storing into m_p
	char *p = m_p;

	// reserve space if we had a safebuf and point into it if there
	if ( buf ) {
		int32_t slotSize = (sizeof(key144_t)+2+sizeof(key128_t));
		int32_t need = tt1->getNumSlotsUsed() * slotSize;
		if ( ! buf->reserve ( need ) ) return false;
		// get cursor into buf, NOT START of buf
		p = buf->getBufStart();
	}

	int32_t siteRank = getSiteRank ();

	if ( ! m_langIdValid ) { g_process.shutdownAbort(true); }

	rdbid_t rdbId = RDB_POSDB;
	if ( m_useSecondaryRdbs ) rdbId = RDB2_POSDB2;

	// store terms from "tt1" table
	for ( int32_t i = 0 ; i < tt1->m_numSlots ; i++ ) {
		// skip if empty
		if ( tt1->m_flags[i] == 0 ) continue;
		// get its key
		char *kp = (char *)tt1->getKey ( i );
		// store rdbid
		*p++ = rdbId; // (rdbId | f);
		// store it as is
		gbmemcpy ( p , kp , sizeof(key144_t) );
		// this was zero when we added these keys to zero, so fix it
		Posdb::setDocIdBits ( p , docId );
		// if this is a numeric field we do not want to set
		// the siterank or langid bits because it will mess up
		// sorting by the float which is basically in the position
		// of the word position bits.
		if ( Posdb::isAlignmentBitClear ( p ) ) {
			// make sure it is set again. it was just cleared
			// to indicate that this key contains a float
			// like a price or something, and we should not
			// set siterank or langid so that its termlist
			// remains sorted just by that float
			Posdb::setAlignmentBit ( p , 1 );
		}
		// otherwise, set the siterank and langid
		else {
			// this too
			Posdb::setSiteRankBits ( p , siteRank );
			// set language here too
			Posdb::setLangIdBits ( p , m_langId );
		}
		// advance over it
		p += sizeof(key144_t);
	}

	// all done
	if ( ! buf ) { m_p = p; return true; }

	// update safebuf otherwise
	char *start = buf->getBufStart();
	// fix SafeBuf::m_length
	buf->setLength ( p - start );
	// sanity
	if ( buf->length() > buf->getCapacity() ) { g_process.shutdownAbort(true); }

	return true;
}

// add keys/recs from the table into the metalist
bool XmlDoc::addTable224 ( HashTableX *tt1 ) {

	// sanity check
	if ( tt1->m_numSlots ) {
		if ( tt1->m_ks != sizeof(key224_t) ) {g_process.shutdownAbort(true);}
		if ( tt1->m_ds != 0                ) {g_process.shutdownAbort(true);}
	}

	rdbid_t rdbId = RDB_LINKDB;
	if ( m_useSecondaryRdbs ) rdbId = RDB2_LINKDB2;

	// store terms from "tt1" table
	for ( int32_t i = 0 ; i < tt1->m_numSlots ; i++ ) {
		// skip if empty
		if ( tt1->m_flags[i] == 0 ) continue;
		// get its key
		char *kp = (char *)tt1->getKey ( i );
		// store rdbid
		*m_p++ = rdbId; // (rdbId | f);
		// store it as is
		gbmemcpy ( m_p , kp , sizeof(key224_t) );
		// advance over it
		m_p += sizeof(key224_t);
	}
	return true;
}

// . this is kinda hacky because it uses a short XmlDoc on the stack
// . no need to hash this stuff for regular documents since all the terms
//   are fielded by gberrorstr, gberrornum or gbisreply.
// . normally we might use a separate xmldoc class for this but i wanted
//   something more lightweight
SafeBuf *XmlDoc::getSpiderStatusDocMetaList ( SpiderReply *reply, bool forDelete ) {

	// set status for this
	setStatus ( "getting spider reply meta list");

	if ( m_spiderStatusDocMetaListValid )
		return &m_spiderStatusDocMetaList;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	if ( ! cr->m_indexSpiderReplies || forDelete ) {
		m_spiderStatusDocMetaListValid = true;
		return &m_spiderStatusDocMetaList;
	}

	// if docid based do not hash a spider reply. docid-based spider
	// requests are added to spiderdb from the query reindex tool.
	// do not do for diffbot subdocuments either, usespiderdb should be
	// false for those.
	// MDW: i disagree, i want to see when these get updated! 9/6/2014
	// ok, let's index for diffbot objects so we can see if they are
	// a dup of another diffbot object, or so we can see when they get
	// revisted, etc.
	//if ( m_setFromDocId || ! m_useSpiderdb ) {
	if ( ! m_useSpiderdb ) {
		m_spiderStatusDocMetaListValid = true;
		return &m_spiderStatusDocMetaList;
	}

	// do not add a status doc if doing a query delete on a status doc
	if ( m_contentTypeValid && m_contentType == CT_STATUS ) {
		m_spiderStatusDocMetaListValid = true;
		return &m_spiderStatusDocMetaList;
	}

	// doing it for diffbot throws off smoketests
	// ok, smoketests are updated now, so remove this
	// if ( strncmp(cr->m_coll,"crawlbottesting-",16) == 0 ) {
	// 	m_spiderStatusDocMetaListValid = true;
	// 	return &m_spiderStatusDocMetaList;
	// }

	// we double add regular html urls in a query reindex because the
	// json url adds the parent, so the parent gets added twice sometimes,
	// and for some reason it is adding a spider status doc the 2nd time
	// so cut that out. this is kinda a hack b/c i'm not sure what's
	// going on. but you can set a break point here and see what's up if
	// you want.
	// MDW: likewise, take this out, i want these recorded as well..
	// if ( m_indexCodeValid && m_indexCode == EDOCFORCEDELETE ) {
	// 	m_spiderStatusDocMetaListValid = true;
	// 	return &m_spiderStatusDocMetaList;
	// }

	// . fake this out so we do not core
	// . hashWords3() uses it i guess
	bool forcedLangId = false;
	if ( ! m_langIdValid ) {
		forcedLangId = true;
		m_langIdValid = true;
		m_langId = langUnknown;
	}

	// prevent more cores
	bool forcedSiteNumInlinks = false;
	if ( ! m_siteNumInlinksValid ) {
		forcedSiteNumInlinks = true;
		m_siteNumInlinks = 0;
		m_siteNumInlinksValid = true;
	}

	SafeBuf *mbuf = getSpiderStatusDocMetaList2 ( reply );

	if ( forcedLangId )
		m_langIdValid = false;

	if ( forcedSiteNumInlinks ) {
		m_siteNumInlinksValid = false;
	}

	return mbuf;
}

// . the spider status doc
// . TODO:
//   usedProxy:1
//   proxyIp:1.2.3.4
SafeBuf *XmlDoc::getSpiderStatusDocMetaList2 ( SpiderReply *reply1 ) {

	setStatus ( "making spider reply meta list");

	// . we also need a unique docid for indexing the spider *reply*
	//   as a separate document
	// . use the same url, but use a different docid.
	// . use now to mix it up
	//int32_t now = getTimeGlobal();
	//int64_t h = hash64(m_docId, now );
	// to keep qa test consistent this docid should be consistent
	// so base it on spidertime of parent doc.
	// if doc is being force deleted then this is invalid!
	//if ( ! m_spideredTimeValid ) { g_process.shutdownAbort(true); }
	int64_t h = hash64(m_docId, m_spideredTime );
	// mask it out
	int64_t d = h & DOCID_MASK;
	// try to get an available docid, preferring "d" if available
	int64_t *uqd = getAvailDocIdOnly ( d );
	if ( ! uqd || uqd == (void *)-1 ) return  (SafeBuf *)uqd;

	// unsigned char *hc = (unsigned char *)getHopCount();
	// if ( ! hc || hc == (void *)-1 ) return (SafeBuf *)hc;

	int32_t tmpVal = -1;
	int32_t *priority = &tmpVal;
	int32_t *ufn = &tmpVal;

	// prevent a core if sreq is not valid, these will freak out
	// diffbot replies may not have a valid m_sreq
	if ( m_sreqValid ) {
		priority = getSpiderPriority();
		if ( ! priority || priority == (void *)-1 )
			return (SafeBuf *)priority;

		ufn = getUrlFilterNum();
		if ( ! ufn || ufn == (void *)-1 )
			return (SafeBuf *)ufn;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;


	// sanity
	if ( ! m_indexCodeValid ) { g_process.shutdownAbort(true); }

	// why isn't gbhopcount: being indexed consistently?
	//if ( ! m_hopCountValid )  { g_process.shutdownAbort(true); }

	// reset just in case
	m_spiderStatusDocMetaList.reset();

	// sanity
	if ( *uqd <= 0 || *uqd > MAX_DOCID ) {
		log("xmldoc: avail docid = %" PRId64". could not index spider "
		    "reply or %s",*uqd,m_firstUrl.getUrl());
		//g_process.shutdownAbort(true); }
		m_spiderStatusDocMetaListValid = true;
		return &m_spiderStatusDocMetaList;
	}

	// the old doc
	XmlDoc *od = NULL;
	if ( m_oldDocValid && m_oldDoc ) od = m_oldDoc;

	Url *fu = &m_firstUrl;

	// . make a little json doc that we'll hash up
	// . only index the fields in this doc, no extra gbdocid: inurl:
	//   hash terms
	SafeBuf jd;
	jd.safePrintf("{\n");

	// so type:status query works
	jd.safePrintf("\"type\":\"status\",\n");

	jd.safePrintf("\"gbssUrl\":\"%s\",\n" , fu->getUrl()  );

	if ( ptr_redirUrl )
		jd.safePrintf("\"gbssFinalRedirectUrl\":\"%s\",\n",
			      ptr_redirUrl);

	if ( m_indexCodeValid ) {
		jd.safePrintf("\"gbssStatusCode\":%i,\n",(int)m_indexCode);
		jd.safePrintf("\"gbssStatusMsg\":\"");
		jd.jsonEncode (mstrerror(m_indexCode));
		jd.safePrintf("\",\n");
	}
	else {
		jd.safePrintf("\"gbssStatusCode\":-1,\n");
		jd.safePrintf("\"gbssStatusMsg\":\"???\",\n");
	}


	if ( m_httpStatusValid )
		jd.safePrintf("\"gbssHttpStatus\":%" PRId32",\n",
			      (int32_t)m_httpStatus);

	// do not index gbssIsSeedUrl:0 because there will be too many usually
	bool isSeed = ( m_sreqValid && m_sreq.m_isAddUrl );
	if ( isSeed )
		jd.safePrintf("\"gbssIsSeedUrl\":1,\n");

	if ( od )
		jd.safePrintf("\"gbssWasIndexed\":1,\n");
	else
		jd.safePrintf("\"gbssWasIndexed\":0,\n");

	int32_t now = getTimeGlobal();
	if ( od )
		jd.safePrintf("\"gbssAgeInIndex\":"
			      "%" PRIu32",\n",now - od->m_spideredTime);

	jd.safePrintf("\"gbssIsDiffbotObject\":0,\n");

	jd.safePrintf("\"gbssDomain\":\"");
	jd.safeMemcpy(fu->getDomain(), fu->getDomainLen() );
	jd.safePrintf("\",\n");

	jd.safePrintf("\"gbssSubdomain\":\"");
	jd.safeMemcpy(fu->getHost(), fu->getHostLen() );
	jd.safePrintf("\",\n");

	//if ( m_redirUrlPtr && m_redirUrlValid )
	//if ( m_numRedirectsValid )
	jd.safePrintf("\"gbssNumRedirects\":%" PRId32",\n",m_numRedirects);

	if ( m_docIdValid )
		jd.safePrintf("\"gbssDocId\":%" PRId64",\n", m_docId);//*uqd);

	if ( m_hopCountValid )
		//jd.safePrintf("\"gbssHopCount\":%" PRId32",\n",(int32_t)*hc);
		jd.safePrintf("\"gbssHopCount\":%" PRId32",\n",(int32_t)m_hopCount);

	// for -diffbotxyz fake docs addedtime is 0
	if ( m_sreqValid && m_sreq.m_discoveryTime != 0 ) {
		// in Spider.cpp we try to set m_sreq's m_addedTime to the
		// min of all the spider requests, and we try to ensure
		// that in the case of deduping we preserve the one with
		// the oldest time. no, now we actually use
		// m_discoveryTime since we were using m_addedTime in
		// the url filters as it was originally intended.
		jd.safePrintf("\"gbssDiscoveredTime\":%" PRId32",\n",
			      m_sreq.m_discoveryTime);
	}

	if ( m_isDupValid && m_isDup )
		jd.safePrintf("\"gbssDupOfDocId\":%" PRId64",\n",
			      m_docIdWeAreADupOf);

	// how many spiderings were successful vs. failed
	// these don't work because we only store one reply
	// which overwrites any older reply. that's how the
	// key is. we can change the key to use the timestamp
	// and not parent docid in makeKey() for spider
	// replies later.
	// if ( m_sreqValid ) {
	// 	jd.safePrintf("\"gbssPrevTotalNumIndexAttempts\":%" PRId32",\n",
	// 		      m_sreq.m_reservedc1 + m_sreq.m_reservedc2 );
	// 	jd.safePrintf("\"gbssPrevTotalNumIndexSuccesses\":%" PRId32",\n",
	// 		      m_sreq.m_reservedc1);
	// 	jd.safePrintf("\"gbssPrevTotalNumIndexFailures\":%" PRId32",\n",
	// 		      m_sreq.m_reservedc2);
	// }

	if ( m_spideredTimeValid )
		jd.safePrintf("\"gbssSpiderTime\":%" PRId32",\n",
			      m_spideredTime);
	else
		jd.safePrintf("\"gbssSpiderTime\":%" PRId32",\n",0);


	if ( m_firstIndexedDateValid )
		jd.safePrintf("\"gbssFirstIndexed\":%" PRIu32",\n",
			      m_firstIndexedDate);

	if ( m_contentHash32Valid )
		jd.safePrintf("\"gbssContentHash32\":%" PRIu32",\n",
			      m_contentHash32);

	if ( m_downloadStartTimeValid && m_downloadEndTimeValid ) {
		jd.safePrintf("\"gbssDownloadStartTimeMS\":%" PRId64",\n",
			      m_downloadStartTime);
		jd.safePrintf("\"gbssDownloadEndTimeMS\":%" PRId64",\n",
			      m_downloadEndTime);

		int64_t took = m_downloadEndTime - m_downloadStartTime;
		jd.safePrintf("\"gbssDownloadDurationMS\":%" PRId64",\n",took);

		jd.safePrintf("\"gbssDownloadStartTime\":%" PRIu32",\n",
			      (uint32_t)(m_downloadStartTime/1000));

		jd.safePrintf("\"gbssDownloadEndTime\":%" PRIu32",\n",
			      (uint32_t)(m_downloadEndTime/1000));
	}


	jd.safePrintf("\"gbssUsedRobotsTxt\":%" PRId32",\n",
		      m_useRobotsTxt);

	//if ( m_numOutlinksAddedValid )
	// crap, this is not right because we only call addOutlinksToMetaList()
	// after we call this function.
	// jd.safePrintf("\"gbssNumOutlinksAdded\":%" PRId32",\n",
	// 	      (int32_t)m_numOutlinksAdded);

	// how many download/indexing errors we've had, including this one
	// if applicable.
	if ( m_srepValid )
		jd.safePrintf("\"gbssConsecutiveErrors\":%" PRId32",\n",
			      m_srep.m_errCount);
	else
		jd.safePrintf("\"gbssConsecutiveErrors\":%" PRId32",\n",0);


	if ( m_ipValid )
		jd.safePrintf("\"gbssIp\":\"%s\",\n",iptoa(m_ip));
	else
		jd.safePrintf("\"gbssIp\":\"0.0.0.0\",\n");

	if ( m_ipEndTime ) {
		int64_t took = m_ipEndTime - m_ipStartTime;
		jd.safePrintf("\"gbssIpLookupTimeMS\":%" PRId64",\n",took);
	}

	if ( m_siteNumInlinksValid ) {
		jd.safePrintf("\"gbssSiteNumInlinks\":%" PRId32",\n",
			      (int32_t)m_siteNumInlinks);
		char siteRank = getSiteRank();
		jd.safePrintf("\"gbssSiteRank\":%" PRId32",\n",
			      (int32_t)siteRank);
	}

	jd.safePrintf("\"gbssContentInjected\":%" PRId32",\n",
		      (int32_t)m_contentInjected);

	if ( m_percentChangedValid && od )
		jd.safePrintf("\"gbssPercentContentChanged\""
			      ":%.01f,\n",
			      m_percentChanged);

	jd.safePrintf("\"gbssSpiderPriority\":%" PRId32",\n",
		      *priority);

	// this could be -1, careful
	if ( *ufn >= 0 )
		jd.safePrintf("\"gbssMatchingUrlFilter\":\"%s\",\n",
			      cr->m_regExs[*ufn].getBufStart());

	// we forced the langid valid above
	if ( m_langIdValid && m_contentLen )
		jd.safePrintf("\"gbssLanguage\":\"%s\",\n",
			      getLanguageAbbr(m_langId));

	if ( m_contentTypeValid && m_contentLen )
		jd.safePrintf("\"gbssContentType\":\"%s\",\n",
			      g_contentTypeStrings[m_contentType]);

	if ( m_contentValid )
		jd.safePrintf("\"gbssContentLen\":%" PRId32",\n",
			      m_contentLen);

	// do not show the -1 any more, just leave it out then
	// to make things look prettier
	if (  m_crawlDelayValid && m_crawlDelay >= 0 )
		// -1 if none?
		jd.safePrintf("\"gbssCrawlDelayMS\":%" PRId32",\n",
			      (int32_t)m_crawlDelay);


	// remove last ,\n
	jd.incrementLength(-2);
	// end the json spider status doc
	jd.safePrintf("\n}\n");

	// BEFORE ANY HASHING
	int32_t savedDist = m_dist;

	// add the index list for it. it returns false and sets g_errno on err
	// otherwise it sets m_spiderStatusDocMetaList
	if ( ! setSpiderStatusDocMetaList ( &jd , *uqd ) )
		return NULL;

	// now make the titlerec
	char xdhead[2048];
	// just the head of it. this is the hacky part.
	XmlDoc *xd = (XmlDoc *)xdhead;
	// clear it out
	memset ( xdhead, 0 , 2048);

	// copy stuff from THIS so the spider reply "document" has the same
	// header info stuff
	int32_t hsize = (char *)&ptr_firstUrl - (char *)this;
	if ( hsize > 2048 ) { g_process.shutdownAbort(true); }
	gbmemcpy ( xdhead , (char *)this , hsize );

	// override spider time in case we had error to be consistent
	// with the actual SpiderReply record
	//xd->m_spideredTime = reply->m_spideredTime;
	//xd->m_spideredTimeValid = true;
	// sanity
	//if ( reply->m_spideredTime != m_spideredTime ) {g_process.shutdownAbort(true);}

	// this will cause the maroon box next to the search result to
	// say "STATUS" similar to "PDF" "DOC" etc.
	xd->m_contentType  = CT_STATUS;

	int32_t fullsize = &m_dummyEnd - (char *)this;
	if ( fullsize > 2048 ) { g_process.shutdownAbort(true); }

	/*
	// the ptr_* were all zero'd out, put the ones we want to keep back in
	SafeBuf tmp;
	// was "Spider Status: %s" but that is unnecessary
	tmp.safePrintf("<title>%s</title>",
		       mstrerror(m_indexCode));

	// if we are a dup...
	if ( m_indexCode == EDOCDUP )
		tmp.safePrintf("Dup of docid %" PRId64"<br>", m_docIdWeAreADupOf );

	if ( m_redirUrlPtr && m_redirUrlValid )
		tmp.safePrintf("Redirected to %s<br>",m_redirUrlPtr->getUrl());
	*/

	// put stats like we log out from logIt
	//tmp.safePrintf("<div style=max-width:800px;>\n");
	// store log output into doc
	//logIt(&tmp);
	//tmp.safePrintf("\n</div>");

	// the content is just the title tag above
	// xd->ptr_utf8Content = tmp.getBufStart();
	// xd->size_utf8Content = tmp.length()+1;
	xd->ptr_utf8Content = jd.getBufStart();
	xd->size_utf8Content = jd.length()+1;

	// keep the same url as the doc we are the spider reply for
	xd->ptr_firstUrl = ptr_firstUrl;
	xd->size_firstUrl = size_firstUrl;

	// serps need site, otherwise search results core
	xd->ptr_site = ptr_site;
	xd->size_site = size_site;

	// if this is null then ip lookup failed i guess so just use
	// the subdomain
	if ( ! ptr_site && m_firstUrlValid ) {
		xd->ptr_site  = m_firstUrl.getHost();
		xd->size_site = m_firstUrl.getHostLen();
	}

	// use the same uh48 of our parent
	int64_t uh48 = m_firstUrl.getUrlHash48();
	// then make into a titlerec but store in metalistbuf, not m_titleRec
	SafeBuf titleRecBuf;
	// this should not include ptrs that are NULL when compressing
	// using its m_internalFlags1
	if ( ! xd->setTitleRecBuf( &titleRecBuf,*uqd,uh48 ) )
		return NULL;

	// concat titleRec to our posdb key records
	if ( ! m_spiderStatusDocMetaList.pushChar((char)RDB_TITLEDB) )
		return NULL;
	if ( ! m_spiderStatusDocMetaList.cat(titleRecBuf) )
		return NULL;

	// return the right val
	m_dist = savedDist;

	// ok, good to go, ready to add to posdb and titledb
	m_spiderStatusDocMetaListValid = true;
	return &m_spiderStatusDocMetaList;
}




// slightly greater than m_spideredTime, which is the download time.
// we use this for sorting as well, like for the widget so things
// don't really get added out of order and not show up in the top spot
// of the widget list.
int32_t XmlDoc::getIndexedTime() {
	if ( m_indexedTimeValid ) return m_indexedTime;
	m_indexedTime = getTimeGlobal();
	return m_indexedTime;
}




Url *XmlDoc::getBaseUrl ( ) {
	if ( m_baseUrlValid ) return &m_baseUrl;
	// need this
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (Url *)xml;
	Url *cu = getCurrentUrl();
	if ( ! cu || cu == (void *)-1 ) return (Url *)cu;

	m_baseUrl.set ( cu );
	// look for base url
	for ( int32_t i=0 ; i < xml->getNumNodes() ; i++ ) {
		// 12 is the <base href> tag id
		if ( xml->getNodeId ( i ) != TAG_BASE ) continue;
		// get the href field of this base tag
		int32_t linkLen;
		char *link = (char *) xml->getString ( i, "href", &linkLen );
		// skip if not valid
		if ( ! link || linkLen == 0 ) continue;
		// set base to it
		m_baseUrl.set( link, linkLen );
		break;
	}

	// fix invalid <base href="/" target="_self"/> tag
	if ( m_baseUrl.getHostLen  () <= 0 || m_baseUrl.getDomainLen() <= 0 )
		m_baseUrl.set ( cu );

	m_baseUrlValid = true;
	return &m_baseUrl;
}



//
// STUFF IMPORTED FROM INDEXLIST.CPP
//

// we also assume all scores are above 256, too
uint8_t score32to8 ( uint32_t score ) {
	// ensure score is > 0... no! not any more
	if ( score <= 0  ) return (unsigned char) 0;
	// extremely large scores need an adjustment to avoid wrapping
	if ( score < (uint32_t)0xffffffff - 128 )
		score += 128;
	// scores are multiplied by 256 to preserve fractions, so undo that
	score /= 256;
	// ensure score is > 0
	if ( score <= 0  ) return (unsigned char) 1;
	// if score < 128 return it now
	if ( score < 128 ) return (unsigned char) score;
	// now shrink it so it's now from 1 upwards
	score -= 127;

	// . take NATURAL log of score now
	// . PROBLEM: for low scores logscore may increase by close to 1.0
	//   for a score increase of 1.0. and since s_maxscore is about 22.0
	//   we end up moving 1.0/22.0 of 128 total pts causing a jump of
	//   2 or more score points!! oops!!! to fix, let's add 10 pts
	//   to the score
	score += 10;
	double logscore = ::log ( (double)score );
	// now the max it can be
	//double maxscore = ::log ( (double)(0x00ffffff - 127));
	static double s_maxscore = -1.0;
	static double s_minscore = -1.0;
	if ( s_maxscore == -1.0 ) {
		uint32_t max = ((0xffffffff +   0)/256) - 127 + 10;
		uint32_t min = (  128                 ) - 127 + 10;
		s_maxscore = ::log((double)max);
		s_minscore = ::log((double)min);
		// adjust
		s_maxscore -= s_minscore;
	}
	// adjust it
	logscore -= s_minscore;
	// scale it into [126,0] (add .5 for rounding)
	double scaled   = (logscore* 127.0) / s_maxscore  + .5;
	// sanity check
	if ( (unsigned char)scaled >= 128 ) { g_process.shutdownAbort(true); }
	// . go into the 8 bit score now
	// . set the hi bit so they know we took its log
	unsigned char score8 = (unsigned char)scaled | 128;
	return score8;
}

// for score8to32() below
static const uint32_t s_scoreMap[] = {
	0UL,
	1UL,
        385UL,
        641UL,
        897UL,
        1153UL,
        1409UL,
        1665UL,
        1921UL,
        2177UL,
        2433UL,
        2689UL,
        2945UL,
        3201UL,
        3457UL,
        3713UL,
        3969UL,
        4225UL,
        4481UL,
        4737UL,
        4993UL,
        5249UL,
        5505UL,
        5761UL,
        6017UL,
        6273UL,
        6529UL,
        6785UL,
        7041UL,
        7297UL,
        7553UL,
        7809UL,
        8065UL,
        8321UL,
        8577UL,
        8833UL,
        9089UL,
        9345UL,
        9601UL,
        9857UL,
        10113UL,
        10369UL,
        10625UL,
        10881UL,
        11137UL,
        11393UL,
        11649UL,
        11905UL,
        12161UL,
        12417UL,
        12673UL,
        12929UL,
        13185UL,
        13441UL,
        13697UL,
        13953UL,
        14209UL,
        14465UL,
        14721UL,
        14977UL,
        15233UL,
        15489UL,
        15745UL,
        16001UL,
        16257UL,
        16513UL,
        16769UL,
        17025UL,
        17281UL,
        17537UL,
        17793UL,
        18049UL,
        18305UL,
        18561UL,
        18817UL,
        19073UL,
        19329UL,
        19585UL,
        19841UL,
        20097UL,
        20353UL,
        20609UL,
        20865UL,
        21121UL,
        21377UL,
        21633UL,
        21889UL,
        22145UL,
        22401UL,
        22657UL,
        22913UL,
        23169UL,
        23425UL,
        23681UL,
        23937UL,
        24193UL,
        24449UL,
        24705UL,
        24961UL,
        25217UL,
        25473UL,
        25729UL,
        25985UL,
        26241UL,
        26497UL,
        26753UL,
        27009UL,
        27265UL,
        27521UL,
        27777UL,
        28033UL,
        28289UL,
        28545UL,
        28801UL,
        29057UL,
        29313UL,
        29569UL,
        29825UL,
        30081UL,
        30337UL,
        30593UL,
        30849UL,
        31105UL,
        31361UL,
        31617UL,
        31873UL,
        32129UL,
        32385UL,
        32641UL,
        32897UL,
        33488UL,
        33842UL,
        34230UL,
        34901UL,
        35415UL,
        35979UL,
        36598UL,
        37278UL,
        38025UL,
        39319UL,
        40312UL,
        41404UL,
        43296UL,
        44747UL,
        46343UL,
        48098UL,
        51138UL,
        53471UL,
        56037UL,
        58859UL,
        61962UL,
        65374UL,
        71287UL,
        75825UL,
        80816UL,
        86305UL,
        92342UL,
        98982UL,
        110492UL,
        119326UL,
        129042UL,
        139728UL,
        151481UL,
        171856UL,
        187496UL,
        204699UL,
        223622UL,
        244437UL,
        267333UL,
        307029UL,
        337502UL,
        371022UL,
        407893UL,
        448450UL,
        493062UL,
        570408UL,
        629783UL,
        695095UL,
        766938UL,
        845965UL,
        982981UL,
        1088163UL,
        1203862UL,
        1331130UL,
        1471124UL,
        1625117UL,
        1892110UL,
        2097072UL,
        2322530UL,
        2570533UL,
        2843335UL,
        3143416UL,
        3663697UL,
        4063102UL,
        4502447UL,
        4985726UL,
        5517332UL,
        6439034UL,
        7146599UL,
        7924919UL,
        8781070UL,
        9722836UL,
        10758778UL,
        12554901UL,
        13933735UL,
        15450451UL,
        17118838UL,
        18954063UL,
        20972809UL,
        24472927UL,
        27159874UL,
        30115514UL,
        33366717UL,
        36943040UL,
        43143702UL,
        47903786UL,
        53139877UL,
        58899576UL,
        65235244UL,
        72204478UL,
        84287801UL,
        93563849UL,
        103767501UL,
        114991518UL,
        127337936UL,
        140918995UL,
        164465962UL,
        182542348UL,
        202426372UL,
        224298798UL,
        248358466UL,
        290073346UL,
        322096762UL,
        357322519UL,
        396070851UL,
        438694015UL,
        485579494UL,
        566869982UL,
        629274552UL,
        697919578UL,
        773429105UL,
        856489583UL,
        947856107UL,
        1106268254UL,
        1227877095UL,
        1361646819UL,
        1508793514UL,
        1670654878UL,
        1951291651UL,
        2166729124UL,
        2403710344UL,
        2664389686UL,
        2951136962UL,
        3266558965UL,
        3813440635UL,
        4233267317UL
};

uint32_t score8to32 ( uint8_t score8 ) {
	return(s_scoreMap[score8]);
}

////////////////////////////////////////////////////////////
//
// Summary/Title generation for Msg20
//
////////////////////////////////////////////////////////////

void XmlDoc::setMsg20Request(Msg20Request *req) {
	// clear it all out
	reset();
	// this too
	m_reply.reset();

	m_pbuf     = NULL;//pbuf;
	m_niceness = req->m_niceness;
	// remember this
	m_req = req;
	m_collnum = req->m_collnum;
	m_collnumValid = true;
	// make this stuff valid
	if ( m_req->m_docId > 0 ) {
		m_docId      = m_req->m_docId;
		m_docIdValid = true;
	}
	// set url too if we should
	if ( m_req->size_ubuf > 1 )
		setFirstUrl ( m_req->ptr_ubuf );
}


static void getMsg20ReplyWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// return if it blocked
	if ( THIS->getMsg20Reply ( ) == (void *)-1 ) return;
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}

// . returns NULL with g_errno set on error
// . returns -1 if blocked
Msg20Reply *XmlDoc::getMsg20Reply ( ) {
	// return it right away if valid
	if ( m_replyValid ) return &m_reply;

	// . internal callback
	// . so if any of the functions we end up calling directly or
	//   indirectly block, this callback will be called
	if ( ! m_masterLoop ) {
		m_masterLoop  = getMsg20ReplyWrapper;
		m_masterState = this;
	}

	// used by Msg20.cpp to time this XmlDoc::getMsg20Reply() function
	if ( ! m_startTimeValid ) {
		m_startTime      = gettimeofdayInMilliseconds();
		m_startTimeValid = true;
	}

	// caller shouldhave the callback set
	if ( ! m_callback1 && ! m_callback2 ) { g_process.shutdownAbort(true); }

	m_niceness = m_req->m_niceness;

	m_collnum = m_req->m_collnum;//cr->m_collnum;
	m_collnumValid = true;

	//char *coll = m_req->ptr_coll;
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) { g_errno = ENOCOLLREC; return NULL; }

	// . cache it for one hour
	// . this will set our ptr_ and size_ member vars
	char **otr = getOldTitleRec ( );
	if ( ! otr || otr == (void *)-1 ) return (Msg20Reply *)otr;

	// must have a title rec in titledb
	if ( ! *otr ) { g_errno = ENOTFOUND; return NULL; }

	// sanity
	if ( *otr != m_oldTitleRec ) { g_process.shutdownAbort(true); }

	// . set our ptr_ and size_ member vars from it after uncompressing
	// . returns false and sets g_errno on error
	if ( ! m_setTr ) {
		// . this completely resets us
		// . this returns false with g_errno set on error
		bool status = set2( *otr, 0, cr->m_coll, NULL, m_niceness);

		// sanity check
		if ( ! status && ! g_errno ) { g_process.shutdownAbort(true); }

		// if there was an error, g_errno should be set.
		if ( ! status ) return NULL;

		m_setTr = true;
	}

	m_reply.m_collnum = m_collnum;

	// lookup the tagdb rec fresh if setting for a summary. that way we
	// can see if it is banned or not. but for getting m_getTermListBuf
	// and stuff above, skip the tagrec lookup!
	// save some time when SPIDERING/BUILDING by skipping fresh
	// tagdb lookup and using tags in titlerec
	if ( m_req && ! m_req->m_getLinkText && ! m_checkedUrlFilters )
		m_tagRecDataValid = false;

	// if  shard responsible for tagrec is dead, then
	// just recycle!
	if ( m_req && ! m_checkedUrlFilters && ! m_tagRecDataValid ) {
		char *site = getSite();
		TAGDB_KEY tk1 = g_tagdb.makeStartKey ( site );
		TAGDB_KEY tk2 = g_tagdb.makeDomainStartKey ( &m_firstUrl );
		uint32_t shardNum1 = g_hostdb.getShardNum(RDB_TAGDB,&tk1);
		uint32_t shardNum2 = g_hostdb.getShardNum(RDB_TAGDB,&tk2);
		// shardnum1 and shardnum2 are often different!
		// log("db: s1=%i s2=%i",(int)shardNum1,(int)shardNum2);
		if ( g_hostdb.isShardDead ( shardNum1 ) ) {
			log("query: skipping tagrec lookup for dead shard "
			    "# %" PRId32
			    ,shardNum1);
			m_tagRecDataValid = true;
		}
		if ( g_hostdb.isShardDead ( shardNum2 ) && m_firstUrlValid ) {
			log("query: skipping tagrec lookup for dead shard "
			    "# %" PRId32
			    ,shardNum2);
			m_tagRecDataValid = true;
		}
	}

	// if we are showing sites that have been banned in tagdb, we dont
	// have to do a tagdb lookup. that should speed things up.
	TagRec *gr = NULL;
	if ( cr && cr->m_doTagdbLookups ) {
		gr = getTagRec();
		if ( ! gr || gr == (void *)-1 ) return (Msg20Reply *)gr;
	}

	// this should be valid, it is stored in title rec
	if ( m_contentHash32Valid ) m_reply.m_contentHash32 = m_contentHash32;
	else                        m_reply.m_contentHash32 = 0;

	if ( ! m_checkedUrlFilters ) {
		// do not re-check
		m_checkedUrlFilters = true;

		// get this
		SpiderRequest sreq;
		SpiderReply   srep;
		setSpiderReqForMsg20 ( &sreq , &srep );

		int32_t spideredTime = getSpideredTime();
		int32_t langIdArg = -1;
		if ( m_langIdValid ) {
			langIdArg = m_langId;
		}

		// get it
		int32_t ufn;
		ufn=::getUrlFilterNum(&sreq,&srep,spideredTime,true,
				      cr,
				      false, // isOutlink?
				      NULL ,
				      langIdArg);


		// get spider priority if ufn is valid
		int32_t pr = 0;

		// sanity check
		if ( ufn < 0 ) {
			log("msg20: bad url filter for url [%s], langIdArg=%" PRId32, sreq.m_url, langIdArg);
		}
		else {
			if ( cr->m_forceDelete[ufn] ) {
				pr = -3;
			}
		}


		// this is an automatic ban!
		if ( gr && gr->getLong("manualban",0))
			pr=-3;//SPIDER_PRIORITY_BANNED;

		// is it banned
		if ( pr == -3 ) { // SPIDER_PRIORITY_BANNED ) { // -2
			// set m_errno
			m_reply.m_errno = EDOCBANNED;
			// and this
			m_reply.m_isBanned = true;
		}

		//
		// for now always allow it until we can fix this better
		// we probably should assume NOT filtered unless it matches
		// a string match only url filter... but at least we will
		// allow it to match "BANNED" filters for now...
		//
		pr = 0;

		// done if we are
		if ( m_reply.m_errno && ! m_req->m_showBanned ) {
			// give back the url at least
			m_reply.ptr_ubuf = getFirstUrl()->getUrl();
			m_reply.size_ubuf = getFirstUrl()->getUrlLen() + 1;
			m_replyValid = true;
			return &m_reply;
		}
	}

	// a special hack for XmlDoc::getRecommendedLinksBuf() so we exclude
	// links that link to the main url's site/domain as well as a
	// competitor url (aka related docid)
	Links *links = NULL;
	if ( m_req->m_ourHostHash32 || m_req->m_ourDomHash32 ) {
		links = getLinks();
		if ( ! links || links==(Links *)-1) return (Msg20Reply *)links;
	}

	// do they want a summary?
	if ( m_req->m_numSummaryLines>0 && ! m_reply.ptr_displaySum ) {
		char *hsum = getHighlightedSummary( &(m_reply.m_isDisplaySumSetFromTags) );

		if ( ! hsum || hsum == (void *)-1 ) {
			return (Msg20Reply *)hsum;
		}

		// is it size and not length?
		int32_t hsumLen = 0;

		// seems like it can return 0x01 if none...
		if ( hsum == (char *)0x01 ) hsum = NULL;

		// get len. this is the HIGHLIGHTED summary so it is ok.
		if ( hsum ) hsumLen = strlen(hsum);

		// must be \0 terminated. not any more, it can be a subset
		// of a larger summary used for deduping
		if ( hsumLen > 0 && hsum[hsumLen] ) { g_process.shutdownAbort(true); }

		// grab stuff from it!
		m_reply.ptr_displaySum = hsum;
		m_reply.size_displaySum = hsumLen+1;
	}

	// copy the link info stuff?
	if ( ! m_req->m_getLinkText ) {
		m_reply.ptr_linkInfo  = (char *)ptr_linkInfo1;
		m_reply.size_linkInfo = size_linkInfo1;
	}

	bool getThatTitle = true;
	if ( m_req->m_titleMaxLen <= 0 ) getThatTitle = false;
	if ( m_reply.ptr_tbuf           ) getThatTitle = false;
	// if steve's requesting the inlink summary we will want to get
	// the title of each linker even if they are spammy!
	// only get title here if NOT getting link text otherwise
	// we only get it down below if not a spammy voter, because
	// this sets the damn slow sections class
	if ( m_req->m_getLinkText &&
	     ! m_useSiteLinkBuf &&
	     ! m_usePageLinkBuf &&
	     // m_pbuf is used by pageparser.cpp now, not the other two things
	     // above this.
	     ! m_pbuf )
		getThatTitle = false;

	// if steve is getting the inlinks, bad and good, for displaying
	// then get the title here now... otherwise, if we are just spidering
	// and getting the inlinks, do not bother getting the title because
	// the inlink might be linkspam... and we check down below...
	if ( ! m_req->m_onlyNeedGoodInlinks )
		getThatTitle = true;

	// ... no more seo so stop it... disable this for sp
	if ( m_req->m_getLinkText )
	        getThatTitle = false;

	if ( getThatTitle ) {
		Title *ti = getTitle();
		if ( ! ti || ti == (Title *)-1 ) return (Msg20Reply *)ti;
		char *tit = ti->getTitle();
		int32_t  titLen = ti->getTitleLen();
		m_reply.ptr_tbuf = tit;
		m_reply.size_tbuf = titLen + 1; // include \0
		// sanity
		if ( tit && tit[titLen] != '\0' ) { g_process.shutdownAbort(true); }
		if ( ! tit || titLen <= 0 ) {
			m_reply.ptr_tbuf = NULL;
			m_reply.size_tbuf = 0;
		}
	}

	// this is not documented because i don't think it will be popular
	if ( m_req->m_getHeaderTag ) {
		SafeBuf *htb = getHeaderTagBuf();
		if ( ! htb || htb == (SafeBuf *)-1 ) return (Msg20Reply *)htb;
		// . it should be null terminated
		// . actually now it is a \0 separated list of the first
		//   few h1 tags
		// . we call SafeBuf::pushChar(0) to add each one
		m_reply.ptr_htag = htb->getBufStart();
		m_reply.size_htag = htb->getLength();
	}

	// get site
	m_reply.ptr_site  = ptr_site;
	m_reply.size_site = size_site;

	// assume unknown
	m_reply.m_noArchive = 0;
	// are we noarchive? only check this if not getting link text
	if ( ! m_req->m_getLinkText ) {
		char *na = getIsNoArchive();
		if ( ! na || na == (char *)-1 ) return (Msg20Reply *)na;
		m_reply.m_noArchive = *na;
	}

	// . summary vector for deduping
	// . does not compute anything if we should not! (svSize will be 0)
	if ( ! m_reply.ptr_vbuf &&
	     m_req->m_getSummaryVector &&
	     cr->m_percentSimilarSummary >   0 &&
	     cr->m_percentSimilarSummary < 100   ) {
		int32_t *sv = getSummaryVector ( );
		if ( ! sv || sv == (void *)-1 ) return (Msg20Reply *)sv;
		m_reply.ptr_vbuf = (char *)m_summaryVec;
		m_reply.size_vbuf = m_summaryVecSize;
	}

	// returns values of specified meta tags
	if ( ! m_reply.ptr_dbuf && m_req->size_displayMetas > 1 ) {
		int32_t dsize;  char *d;
		d = getDescriptionBuf(m_req->ptr_displayMetas,&dsize);
		if ( ! d || d == (char *)-1 ) return (Msg20Reply *)d;
		m_reply.ptr_dbuf  = d;
		m_reply.size_dbuf = dsize; // includes \0
	}

	// get thumbnail image DATA
	if ( ! m_reply.ptr_imgData && ! m_req->m_getLinkText ) {
		m_reply.ptr_imgData = ptr_imageData;
		m_reply.size_imgData = size_imageData;
	}

	// get firstip
	int32_t *fip = getFirstIp();
	if ( ! fip || fip == (void *)-1 ) return (Msg20Reply *)fip;

	char *ru = ptr_redirUrl;
	int32_t  rulen = 0;
	if ( ru ) rulen = strlen(ru)+1;

	// need full cached page of each search result?
	// include it always for spider status docs.
	if ( m_req->m_includeCachedCopy || m_contentType == CT_STATUS ) {
		m_reply.ptr_content =  ptr_utf8Content;
		m_reply.size_content = size_utf8Content;
	}

	// do they want to know if this doc has an outlink to a url
	// that has the provided site and domain hash, Msg20Request::
	// m_ourHostHash32 and m_ourDomHash32?
	int32_t nl = 0;
	if ( links ) nl = links->getNumLinks();
	// scan all outlinks we have on this page
	int32_t i ; for ( i = 0 ; i < nl ; i++ ) {
		// get the normalized url
		//char *url = links->getLinkPtr(i);
		// get the site. this will not block or have an error.
		int32_t hh32 = (int32_t)((uint32_t)links->getHostHash64(i));
		if ( hh32 == m_req->m_ourHostHash32 ) break;
		int32_t dh32 = links->getDomHash32(i);
		if ( dh32 == m_req->m_ourDomHash32 ) break;
	}

	// easy ones
	m_reply.m_isPermalink      = m_isPermalink;
	m_reply.m_ip               = m_ip;
	m_reply.m_firstIp          = *fip;
	m_reply.m_docId            = m_docId;
	m_reply.m_contentLen       = size_utf8Content;
	m_reply.m_lastSpidered     = getSpideredTime();//m_spideredTime;
	m_reply.m_datedbDate       = 0;
	m_reply.m_firstIndexedDate = m_firstIndexedDate;
	m_reply.m_firstSpidered    = m_firstIndexedDate;
	m_reply.m_contentType      = m_contentType;
	m_reply.m_language         = m_langId;
	m_reply.m_country          = *getCountryId();
	m_reply.m_hopcount         = m_hopCount;
	m_reply.m_siteRank         = getSiteRank();
	m_reply.m_isAdult          = m_isAdult; //QQQ getIsAdult()? hmmm

	m_reply.ptr_ubuf             = getFirstUrl()->getUrl();
	m_reply.ptr_rubuf            = ru;
	m_reply.ptr_metadataBuf      = NULL;


	m_reply.size_ubuf             = getFirstUrl()->getUrlLen() + 1;
	m_reply.size_rubuf            = rulen;
	m_reply.size_metadataBuf      = 0;

	// check the tag first
	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }

	m_reply.m_siteNumInlinks       = m_siteNumInlinks;

	// . get stuff from link info
	// . this is so fast, just do it for all Msg20 requests
	// . no! think about it -- this can be huge for pages like
	//   google.com!!!
	LinkInfo *info1 = ptr_linkInfo1;
	if ( info1 ) {
		m_reply.m_pageNumInlinks        = info1->m_totalInlinkingDocIds;
		m_reply.m_pageNumGoodInlinks     = info1->m_numGoodInlinks;
		m_reply.m_pageNumUniqueIps       = info1->m_numUniqueIps;
		m_reply.m_pageNumUniqueCBlocks   = info1->m_numUniqueCBlocks;
		m_reply.m_pageInlinksLastUpdated = info1->m_lastUpdated;
	}

	// getLinkText is true if we are getting the anchor text for a
	// supplied url as part of the SPIDER process..
	// this was done by Msg23 before
	if ( ! m_req->m_getLinkText ) {
		m_replyValid = true;
		return &m_reply;
	}

	// use the first url of the linker by default
	Url *linker = &m_firstUrl;

	// the base url, used for doing links: terms, is the final url,
	// just in case there were any redirects
	Url redir;
	if ( ru ) {
		redir.set ( ru );
		linker = &redir;
	}

	// . we need the mid doma hash in addition to the ip domain because
	//   chat.yahoo.com has different ip domain than www.yahoo.com , ...
	//   and we don't want them both to be able to vote
	// . the reply is zeroed out in call the m_reply.reset() above so
	//   if this is not yet set it will be 0
	if ( m_reply.m_midDomHash == 0 ) {
		m_reply.m_midDomHash = hash32 ( linker->getMidDomain(), linker->getMidDomainLen() );
	}

	int64_t start = gettimeofdayInMilliseconds();

	// if not set from above, set it here
	if ( ! links ) links = getLinks ( true ); // do quick set?
	if ( ! links || links == (Links *)-1 ) return (Msg20Reply *)links;
	Pos *pos = getPos();
	if ( ! pos || pos == (Pos *)-1 ) return (Msg20Reply *)pos;
	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (Msg20Reply *)ww;
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (Msg20Reply *)xml;

	// get a ptr to the link in the content. will point to the
	// stuff in the href field of the anchor tag. used for seeing if
	// we have bad links or not.
	int32_t linkNode = -1;
	int32_t linkNum  = -1;
	// . get associated link text from the linker's document for our "url"
	// . only gets from FIRST link to us
	// . TODO: allow more link text from better quality pages?
	// . TODO: limit score based on link text length?
	// . should always be NULL terminated
	// . should not break in the middle of a word
	// . this will return the item/entry if we are extracting from an
	//   rss/atom feed
	char  *rssItem    = NULL;
	int32_t   rssItemLen = 0;

	//
	// TODO: for getting siteinlinks just match the site in the url
	// not the full url... and maybe match the one with the shortest path.
	//

	//workaround for truncation causeing a multibyte utf8 character to be
	//split and then text parsing traversing past the defined bytes.
	m_linkTextBuf[sizeof(m_linkTextBuf)-3] = '\0';
	m_linkTextBuf[sizeof(m_linkTextBuf)-2] = '\0';
	m_linkTextBuf[sizeof(m_linkTextBuf)-1] = '\0';

	// . get the link text
	// . linkee might be a site if m_isSiteLinkInfo is true in which
	//   case we get the best inlink to that site, and linkee is
	//   something like blogspot.com/mary/ or some other site.
	int32_t blen = links->getLinkText ( m_req->ptr_linkee  ,//&linkee,
					 m_req->m_isSiteLinkInfo ,
					 m_linkTextBuf       ,
					 sizeof(m_linkTextBuf)-2,
					 &rssItem            ,
					 &rssItemLen         ,
					 &linkNode           ,
					 &linkNum            ,
					 m_niceness          );


	// . BUT this skips the news topic stuff too. bad?
	// . THIS HAPPENED before because we were truncating the xml(see above)
	if ( linkNode < 0 ) {

		int64_t took = gettimeofdayInMilliseconds() - start;
		if ( took > 100 )
			log("build: took %" PRId64" ms to get link text for "
			    "%s from linker %s",
			    took,
			    m_req->ptr_linkee,
			    m_firstUrl.getUrl() );

		logf(LOG_DEBUG,"build: Got linknode = %" PRId32" < 0. Cached "
		     "linker %s does not have outlink to %s like linkdb "
		     "says it should. page is probably too big and the "
		     "outlink is past our limit. contentLen=%" PRId32". or "
		     "a sitehash collision, or an area tag link.",
		     linkNode,getFirstUrl()->getUrl(),m_req->ptr_linkee,
		     m_xml.getContentLen());
		//g_errno = ECORRUPTDATA;
		// do not let multicast forward to a twin! so use this instead
		// of ECORRUTPDATA
		g_errno = EBADENGINEER;
		//g_process.shutdownAbort(true);
		return NULL;
	}

	if ( ! verifyUtf8 ( m_linkTextBuf , blen ) ) {
		log("xmldoc: bad OUT link text from url=%s for %s",
		    m_req->ptr_linkee,m_firstUrl.getUrl());
		m_linkTextBuf[0] = '\0';
		blen = 0;
	}

	// verify for rss as well. seems like we end up coring because
	// length/size is not in cahoots and [size-1] != '\0' sometimes
	if ( ! verifyUtf8 ( rssItem , rssItemLen ) ) {
		log("xmldoc: bad RSS ITEM text from url=%s for %s",
		    m_req->ptr_linkee,m_firstUrl.getUrl());
		rssItem[0] = '\0';
		rssItemLen = 0;
	}

	// point to it, include the \0.
	if ( blen > 0 ) {
		m_reply.ptr_linkText  = m_linkTextBuf;
		// save the size into the reply, include the \0
		m_reply.size_linkText = blen + 1;
		// sanity check
		if ( (size_t)blen + 2 > sizeof(m_linkTextBuf) ) { g_process.shutdownAbort(true); }
		// sanity check. null termination required.
		if ( m_linkTextBuf[blen] ) { g_process.shutdownAbort(true); }
	}

	// . the link we link to
	// . important when getting site info because the link url
	//   can be different than the root url!
	m_reply. ptr_linkUrl = links->getLink   (linkNum);
	m_reply.size_linkUrl = links->getLinkLen(linkNum)+1;

	// save the rss item in our state so we can point to it, include \0
	if ( (size_t)rssItemLen > sizeof(m_rssItemBuf)-2)
		rssItemLen = sizeof(m_rssItemBuf)-2;
	if ( rssItemLen > 0) {
		gbmemcpy ( m_rssItemBuf, rssItem , rssItemLen );
		// NULL terminate it
		m_rssItemBuf[rssItemLen] = 0;
	}

	// point to it, include the \0
	if ( rssItemLen > 0 ) {
		m_reply.ptr_rssItem  = m_rssItemBuf;
		m_reply.size_rssItem = rssItemLen + 1;
	}

	if ( ! m_req->m_doLinkSpamCheck )
		m_reply.m_isLinkSpam = false;

	if ( m_req->m_doLinkSpamCheck ) {
		// reset to NULL to avoid strlen segfault
		const char *note = NULL;
		// need this
		if ( ! m_xmlValid ) { g_process.shutdownAbort(true); }

		Url linkeeUrl;
		linkeeUrl.set ( m_req->ptr_linkee );

		// get it. does not block.
		m_reply.m_isLinkSpam = ::isLinkSpam ( linker ,
						     m_ip ,
						     m_siteNumInlinks,
						     &m_xml,
						     links,
						     // if doc length more
						     // than 150k then consider
						     // it linkspam
						     // automatically so it
						     // can't vote
						     150000,//MAXDOCLEN//150000
						     &note ,
						     &linkeeUrl , // url ,
						     linkNode );
		// store it
		if ( note ) {
			// include the \0
			m_reply.ptr_note  = note;
			m_reply.size_note = strlen(note)+1;
		}
		// log the reason why it is a log page
		if ( m_reply.m_isLinkSpam )
			log(LOG_DEBUG,"build: linker %s: %s.",
			    linker->getUrl(),note);
		// sanity
		if ( m_reply.m_isLinkSpam && ! note )
			log("linkspam: missing note for d=%" PRId64"!",m_docId);
	}

	// sanity check
	if ( m_reply.ptr_rssItem &&
	     m_reply.size_rssItem>0 &&
	     m_reply.ptr_rssItem[m_reply.size_rssItem-1]!=0) {
		g_process.shutdownAbort(true); }

	// . skip all this junk if we are a spammy voter
	// . we get the title above in "getThatTitle"
	if ( m_reply.m_isLinkSpam ) {
		m_replyValid = true; return &m_reply;
	}

	// . this vector is set from a sample of the entire doc
	// . it is used to dedup voters in Msg25.cpp
	// . this has pretty much been replaced by vector2, it was
	//   also saying a doc was a dup if all its words were
	//   contained by another, like if it was a small subset, which
	//   wasn't the best behaviour.
	// . yeah neighborhood text is much better and this is setting
	//   the slow sections class, so i took it out
	getPageSampleVector ();
	// must not block or error out. sanity check
	if ( ! m_pageSampleVecValid ) { g_process.shutdownAbort(true); }
	//st->m_v1.setPairHashes    ( ww , -1    , m_niceness );

	// . this vector is set from the text after the link text
	// . it terminates at at a breaking tag
	// . check it out in ~/fff/src/Msg20.cpp
	getPostLinkTextVector ( linkNode );

	// get it
	getTagPairHashVector();
	// must not block or error out. sanity check
	if ( ! m_tagPairHashVecValid ) { g_process.shutdownAbort(true); }

	// reference the vectors in our reply
	m_reply. ptr_vector1 = m_pageSampleVec;
	m_reply.size_vector1 = m_pageSampleVecSize;
	m_reply. ptr_vector2 = m_postVec;
	m_reply.size_vector2 = m_postVecSize;
	m_reply. ptr_vector3 = m_tagPairHashVec;
	m_reply.size_vector3 = m_tagPairHashVecSize;

	// crap, we gotta bubble sort these i think
	// but only tag pair hash vec
	bool flag = true;
	uint32_t *d = (uint32_t *)m_tagPairHashVec;
	// exclude the terminating 0 int32_t
	int32_t nd = (m_tagPairHashVecSize / 4) - 1;
	while ( flag ) {
		flag = false;
		for ( int32_t i = 1 ; i < nd ; i++ ) {
			if ( d[i-1] <= d[i] ) continue;
			uint32_t tmp = d[i-1];
			d[i-1] = d[i];
			d[i]   = tmp;
			flag   = true;
		}
	}

	// convert "linkNode" into a string ptr into the document
	char *node = xml->getNodePtr(linkNode)->m_node;
	// . find the word index, "n" for this node
	// . this is INEFFICIENT!!
	char **wp = ww->getWords();
	int32_t   nw = ww->getNumWords();
	int32_t   n;

	for ( n = 0; n < nw && wp[n] < node ; n++ ) {
	}

	// sanity check
	if ( n >= nw ) {
		log("links: crazy! could not get word before linknode");
		g_errno = EBADENGINEER;
		return NULL;
	}

	//
	// get the surrounding link text, around "linkNode"
	//
	// radius of 80 characters around n
	int32_t  radius = 80;
	char *p      = m_surroundingTextBuf;
	char *pend   = m_surroundingTextBuf + sizeof(m_surroundingTextBuf)/2;
	// . make a neighborhood in the "words" space [a,b]
	// . radius is in characters, so "convert" into words by dividing by 5
	int32_t a = n - radius / 5;
	int32_t b = n + radius / 5;
	if ( a <     0 ) a =     0;
	if ( b >    nw ) b =    nw;
	int32_t *pp  = pos->m_pos;
	int32_t  len;
	// if too big shring the biggest, a or b?
	while ( (len=pp[b]-pp[a]) >= 2 * radius + 1 ) {
		// decrease the largest, a or b
		if ( a<n && (pp[n]-pp[a])>(pp[b]-pp[n])) a++;
		else if ( b>n )                          b--;
	}
	// only store it if we can
	if ( p + len + 1 < pend ) {
		// store it
		// FILTER the html entities!!
		int32_t len2 = pos->filter( ww, a, b, false, p, pend, m_version );

		// ensure NULL terminated
		p[len2] = '\0';
		// store in reply. it will be serialized when sent.
		m_reply.ptr_surroundingText  = p;
		m_reply.size_surroundingText = len2 + 1;
	}

	// get title? its slow because it sets the sections class
	if ( m_req->m_titleMaxLen > 0 && ! m_reply.ptr_tbuf &&
	     // don't get it anymore if getting link info because it
	     // is slow...
	     getThatTitle ) {
		Title *ti = getTitle();
		if ( ! ti || ti == (Title *)-1 ) return (Msg20Reply *)ti;
		char *tit = ti->getTitle();
		int32_t  titLen = ti->getTitleLen();
		m_reply. ptr_tbuf = tit;
		m_reply.size_tbuf = titLen + 1; // include \0
		if ( ! tit || titLen <= 0 ) {
			m_reply.ptr_tbuf = NULL;
			m_reply.size_tbuf = 0;
		}
	}

	int64_t took = gettimeofdayInMilliseconds() - start;
	if ( took > 100 )
		log("build: took %" PRId64" ms to get link text for "
		    "%s from linker %s",
		    took,
		    m_req->ptr_linkee,
		    m_firstUrl.getUrl() );


	m_replyValid = true;
	return &m_reply;
}


Query *XmlDoc::getQuery() {
	if ( m_queryValid ) return &m_query;

	// bail if no query
	if ( ! m_req || ! m_req->ptr_qbuf ) {
		m_queryValid = true;
		return &m_query;
	}

	int64_t start = logQueryTimingStart();

	// return NULL with g_errno set on error
	if ( !m_query.set2( m_req->ptr_qbuf, m_req->m_langId, true ) ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	m_queryValid = true;
	return &m_query;
}

Matches *XmlDoc::getMatches () {
	// return it if it is set
	if ( m_matchesValid ) return &m_matches;

	// if no query, matches are empty
	if ( ! m_req || ! m_req->ptr_qbuf ) {
		m_matchesValid = true;
		return &m_matches;
	}

	// need a buncha crap
	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (Matches *)ww;
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (Matches *)xml;
	Bits *bits = getBitsForSummary();
	if ( ! bits || bits == (Bits *)-1 ) return (Matches *)bits;
	Sections *ss = getSections();
	if ( ! ss || ss == (void *)-1) return (Matches *)ss;
	Pos *pos = getPos();
	if ( ! pos || pos == (Pos *)-1 ) return (Matches *)pos;
	Title *ti = getTitle();
	if ( ! ti || ti == (Title *)-1 ) return (Matches *)ti;
	Phrases *phrases = getPhrases();
	if ( ! phrases || phrases == (void *)-1 ) return (Matches *)phrases;

	Query *q = getQuery();
	if ( ! q ) return (Matches *)q;

	int64_t start = logQueryTimingStart();

	// set it up
	m_matches.setQuery ( q );

	LinkInfo *linkInfo = getLinkInfo1();
	if(linkInfo==(LinkInfo*)-1)
		linkInfo = NULL;
	// returns false and sets g_errno on error
	if ( !m_matches.set( ww, phrases, ss, bits, pos, xml, ti, getFirstUrl(), linkInfo ) ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	// we got it
	m_matchesValid = true;
	return &m_matches;
}

// sender wants meta description, custom tags, etc.
char *XmlDoc::getDescriptionBuf ( char *displayMetas , int32_t *dsize ) {
	// return the buffer if we got it
	if ( m_dbufValid ) { *dsize = m_dbufSize; return m_dbuf; }
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	// now get the content of the requested display meta tags
	//char  dbuf [ 1024*64 ];
	char *dbufEnd = m_dbuf + 1024;//1024*64;
	char *dptr    = m_dbuf;
	char *pp      = displayMetas;
	char *ppend   = pp + strlen(displayMetas);
	// loop over the list of requested meta tag names
	while ( pp < ppend && dptr < dbufEnd ) {
		// skip initial spaces. meta tag names are ascii always i guess
		while ( *pp && is_wspace_a(*pp) ) pp++;
		// that's the start of the meta tag name
		char *s = pp;
		// . find end of that meta tag name
		// . can end in :<integer> which specifies max len
		while ( *pp && ! is_wspace_a(*pp) && *pp != ':' ) pp++;
		// assume no max length to the content of this meta tag
		int32_t maxLen = 0x7fffffff;
		// save current char
		char c = *pp;
		// . NULL terminate the name
		// . before, overflowed the request buffer and caused core!
		// . seems like it is already NULL terminated
		if ( *pp ) *pp = '\0';
		// always advance regardless though
		pp++;
		// if ':' was specified, get the max length
		if ( c == ':' ) {
			if ( is_digit(*pp) ) maxLen = atoi ( pp );
			// skip over the digits
			while ( *pp && ! is_wspace_a (*pp) ) pp++;
		}
		// don't exceed our total buffer size (save room for \0 at end)
		int32_t avail = dbufEnd - dptr - 1;
		if ( maxLen > avail ) maxLen = avail;
		// store the content at "dptr" (do not exceed "maxLen" bytes)
		int32_t wlen = xml->getMetaContent( dptr, maxLen, s, strlen( s ) );
		dptr[wlen] = '\0';

		// test it out
		if ( ! verifyUtf8 ( dptr ) ) {
			log("xmldoc: invalid utf8 content for meta tag %s.",s);
			continue;
		}

		// advance and NULL terminate
		dptr    += wlen;
		*dptr++  = '\0';
		// bitch if we truncated
		if ( dptr >= dbufEnd )
			log("query: More than %" PRId32" bytes of meta tag "
			    "content "
			    "was encountered. Truncating.",
			    (int32_t)(dbufEnd-m_dbuf));
	}
	// what is the size of the content of displayed meta tags?
	m_dbufSize   = dptr - m_dbuf;
	m_dbufValid = true;
	*dsize = m_dbufSize;
	return m_dbuf;
}

SafeBuf *XmlDoc::getHeaderTagBuf() {
	if ( m_htbValid ) return &m_htb;

	Sections *ss = getSections();
	if ( ! ss || ss == (void *)-1) return (SafeBuf *)ss;

	int32_t count = 0;

	// scan sections
	Section *si = ss->m_rootSection;

 moreloop:

	for ( ; si ; si = si->m_next ) {
		if ( si->m_tagId != TAG_H1 ) continue;
		// if it contains now text, this will be -1
		// so give up on it
		if ( si->m_firstWordPos < 0 ) continue;
		if ( si->m_lastWordPos  < 0 ) continue;
		// ok, it works, get it
		break;
	}
	// if no h1 tag then make buf empty
	if ( ! si ) {
		m_htb.nullTerm();
		m_htbValid = true;
		return &m_htb;
	}
	// otherwise, set it
	const char *a = m_words.getWord(si->m_firstWordPos);
	const char *b = m_words.getWord(si->m_lastWordPos);
	b += m_words.getWordLen(si->m_lastWordPos);

	// copy it
	m_htb.safeMemcpy ( a , b - a );
	m_htb.pushChar('\0');

	si = si->m_next;

	// add more?
	if ( count++ < 3 ) goto moreloop;

	m_htbValid = true;
	return &m_htb;
}

Title *XmlDoc::getTitle() {
	if ( m_titleValid ) {
		return &m_title;
	}

	uint8_t *contentTypePtr = getContentType();
	if ( ! contentTypePtr || contentTypePtr == (void *)-1 ) {
		return (Title *)contentTypePtr;
	}

	// xml and json docs have empty title
	if ( *contentTypePtr == CT_JSON || *contentTypePtr == CT_XML ) {
		m_titleValid = true;
		return &m_title;
	}

	int32_t titleMaxLen = 80;
	if ( m_req ) {
		titleMaxLen = m_req->m_titleMaxLen;
	} else {
		CollectionRec *cr = getCollRec();
		if (cr) {
			titleMaxLen = cr->m_titleMaxLen;
		}
	}

	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) {
		return (Title *)xml;
	}

	int64_t start = logQueryTimingStart();

	// we try to set from tags to avoid initializing everything else
	if ( m_title.setTitleFromTags( xml, titleMaxLen, *contentTypePtr ) ) {
		m_titleValid = true;

		logQueryTimingEnd( __func__, start );

		return &m_title;
	}

	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) {
		return (Title *)ww;
	}

	Query *query = getQuery();
	if ( ! query ) {
		return (Title *)query;
	}

	m_titleValid = true;

	char *filteredRootTitleBuf = getFilteredRootTitleBuf();
	if ( filteredRootTitleBuf == (char*) -1) {
		filteredRootTitleBuf = NULL;
	}

	start = logQueryTimingStart();

	if ( !m_title.setTitle( xml, ww, titleMaxLen, query, getLinkInfo1(), getFirstUrl(), filteredRootTitleBuf,
							m_filteredRootTitleBufSize, *contentTypePtr, m_langId ) ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	return &m_title;
}


Summary *XmlDoc::getSummary () {
	if ( m_summaryValid ) {
		return &m_summary;
	}

	// time cpu set time
	m_cpuSummaryStartTime = gettimeofdayInMilliseconds();

	uint8_t *ct = getContentType();
	if ( ! ct || ct == (void *)-1 ) {
		return (Summary *)ct;
	}
	// xml and json docs have empty summaries
	if ( *ct == CT_JSON || *ct == CT_XML ) {
		m_summaryValid = true;
		return &m_summary;
	}

	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) {
		return (Summary *)xml;
	}

	Title *ti = getTitle();
	if ( ! ti || ti == (Title *)-1 ) {
		return (Summary *)ti;
	}

	int64_t start = logQueryTimingStart();
    if ( m_summary.setSummaryFromTags( xml, m_req->m_summaryMaxLen, ti->getTitle(), ti->getTitleLen() ) ) {
		logQueryTimingEnd( __func__, start );

		m_summaryValid = true;
		return &m_summary;
	}

	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) {
		return (Summary *)ww;
	}

	Sections *sections = getSections();
	if ( ! sections ||sections==(Sections *)-1) {
		return (Summary *)sections;
	}

	Pos *pos = getPos();
	if ( ! pos || pos == (Pos *)-1 ) {
		return (Summary *)pos;
	}

	char *site = getSite();
	if ( ! site || site == (char *)-1 ) {
		return (Summary *)site;
	}

	int64_t *d = getDocId();
	if ( ! d || d == (int64_t *)-1 ) {
		return (Summary *)d;
	}

	Matches *mm = getMatches();
	if ( ! mm || mm == (Matches *)-1 ) {
		return (Summary *)mm;
	}

	Query *q = getQuery();
	if ( ! q ) {
		return (Summary *)q;
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		return NULL;
	}

	start = logQueryTimingStart();

	// . get the highest number of summary lines that we need
	// . the summary vector we generate for doing summary-based deduping
	//   typically has more lines in it than the summary we generate for
	//   displaying to the user
	int32_t numLines = m_req->m_numSummaryLines;
	if ( cr->m_percentSimilarSummary >   0  &&
	     cr->m_percentSimilarSummary < 100  &&
	     m_req->m_getSummaryVector            &&
	     cr->m_summDedupNumLines > numLines   ) {
		// request more lines than we will display
		numLines = cr->m_summDedupNumLines;
	}

	// compute the summary
	bool status = m_summary.setSummary( xml, ww, sections, pos, q, m_req->m_summaryMaxLen, numLines,
	                                    m_req->m_numSummaryLines, m_req->m_summaryMaxNumCharsPerLine, getFirstUrl(), mm,
	                                    ti->getTitle(), ti->getTitleLen() );

	// error, g_errno should be set!
	if ( ! status ) {
		return NULL;
	}

	logQueryTimingEnd( __func__, start );

	m_summaryValid = true;
	return &m_summary;
}

char *XmlDoc::getHighlightedSummary ( bool *isSetFromTagsPtr ) {
	if ( m_finalSummaryBufValid ) {
		if ( isSetFromTagsPtr ) {
			*isSetFromTagsPtr = m_isFinalSummarySetFromTags;
		}

		return m_finalSummaryBuf.getBufStart();
	}

	Summary *s = getSummary();
	if ( ! s || s == (void *)-1 ) {
		return (char *)s;
	}

	Query *q = getQuery();
	if ( ! q ) {
		return (char *)q;
	}

	// get the summary
	char *sum    = s->getSummary();
	int32_t sumLen = s->getSummaryDisplayLen();
	m_isFinalSummarySetFromTags = s->isSetFromTags();

	// assume no highlighting?
	if ( ! m_req->m_highlightQueryTerms || sumLen == 0 ) {
		m_finalSummaryBuf.safeMemcpy ( sum , sumLen );
		m_finalSummaryBuf.nullTerm();
		m_finalSummaryBufValid = true;

		if ( isSetFromTagsPtr ) {
			*isSetFromTagsPtr = m_isFinalSummarySetFromTags;
		}

		return m_finalSummaryBuf.getBufStart();
	}

	if ( ! m_langIdValid ) { g_process.shutdownAbort(true); }

	// url encode summary
	StackBuf(tmpSum);
	tmpSum.htmlEncode(sum, sumLen, false);

	Highlight hi;
	StackBuf(hb);

	// highlight the query in it
	int32_t hlen = hi.set ( &hb, tmpSum.getBufStart(), tmpSum.getLength(), q, "<b>", "</b>" );

	// highlight::set() returns 0 on error
	if ( hlen < 0 ) {
		log("build: highlight class error = %s",mstrerror(g_errno));
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		return NULL;
	}

	// store into our safebuf then
	m_finalSummaryBuf.safeMemcpy ( &hb );
	m_finalSummaryBufValid = true;
	m_finalSummaryBuf.nullTerm();

	if ( isSetFromTagsPtr ) {
		*isSetFromTagsPtr = m_isFinalSummarySetFromTags;
	}

	return m_finalSummaryBuf.getBufStart();
}

// <meta name=robots value=noarchive>
// <meta name=<configured botname> value=noarchive>
char *XmlDoc::getIsNoArchive ( ) {
	if ( m_isNoArchiveValid ) return &m_isNoArchive;
	Xml *xml = getXml();
	if ( ! xml || xml == (void *)-1 ) return (char *)xml;
	m_isNoArchive      = false;
	m_isNoArchiveValid = true;
	int32_t     n     = xml->getNumNodes();
	XmlNode *nodes = xml->getNodes();
	// find the meta tags
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// continue if not a meta tag
		if ( nodes[i].m_nodeId != TAG_META ) continue;
		// get robots attribute
		int32_t alen; char *att;
		// <meta name=robots value=noarchive>
		att = nodes[i].getFieldValue ( "name" , &alen );
		// need a name!
		if ( ! att ) continue;
		// get end
		char *end = att + alen;
		// skip leading spaces
		while ( att < end && *att && is_wspace_a(*att) ) att++;
		// must be robots or <configured botname>. skip if not
		if ( strncasecmp(att,"robots" ,6) &&
		     strncasecmp(att,g_conf.m_spiderBotName,strlen(g_conf.m_spiderBotName))   ) continue;

		// get the content vaue
		att = nodes[i].getFieldValue("content",&alen);
		// skip if none
		if ( ! att ) continue;
		// get end
		end = att + alen;
		// skip leading spaces
		while ( att < end && *att && is_wspace_a(*att) ) att++;
		// is is noarchive? skip if no such match
		if ( strncasecmp(att,"noarchive",9) ) continue;
		// ok, we got it
		m_isNoArchive = true;
		break;
	}
	// return what we got
	return &m_isNoArchive;
}

char *XmlDoc::getIsLinkSpam ( ) {
	if ( m_isLinkSpamValid ) return &m_isLinkSpam2;

	setStatus ( "checking if linkspam" );

	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	Links *links = getLinks();
	if ( ! links || links == (Links *)-1 ) return (char *)links;
	int32_t *ip = getIp();
	if ( ! ip || ip == (int32_t *)-1 ) return (char *)ip;
	//LinkInfo *info1 = getLinkInfo1();
	//if ( ! info1 || info1 == (LinkInfo *)-1 ) return (char *)info1;
	int32_t *sni = getSiteNumInlinks();
	if ( ! sni || sni == (int32_t *)-1 ) return (char *)sni;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// reset note
	m_note = NULL;

	// . if a doc is "link spam" then it cannot vote, or its
	//   voting power is reduced
	// . look for indications that the link is from a guestbook
	// . doc length over 100,000 bytes consider it link spam
	m_isLinkSpamValid = true;
	m_isLinkSpam = ::isLinkSpam ( getFirstUrl(), // linker
				      *ip ,
				      *sni ,
				      xml,
				      links,
				      150000,//MAXDOCLEN,//maxDocLen ,
				      &m_note ,
				      NULL , // &linkee , // url ,
				      -1 ); // linkNode ,
	// set shadow
	m_isLinkSpam2 = (bool)m_isLinkSpam;
	return &m_isLinkSpam2;
}

static void *malloc_replace (void *pf , unsigned int nitems , unsigned int size ) {
	return g_mem.gbmalloc(size*nitems,"malloc_replace");
}

static void free_replace   ( void *pf , void *s ) {
	g_mem.gbfree(s,"free_replace", 0, false);
}

int gbuncompress ( unsigned char *dest      ,
		   uint32_t *destLen   ,
		   unsigned char *source    ,
		   uint32_t  sourceLen ) {
	z_stream stream;
	int err;

	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
	// Check for source > 64K on 16-bit machine:
	if ((uLong)stream.avail_in != sourceLen) return Z_BUF_ERROR;

	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

	//stream.zalloc = (alloc_func)0;
	//stream.zfree = (free_func)0;
	stream.zalloc = malloc_replace;//zliballoc;
	stream.zfree  = free_replace;//zlibfree;

	//we can be gzip or deflate
	err = inflateInit2(&stream, 47);

	if (err != Z_OK) return err;

	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		if (err == Z_NEED_DICT ||
		    (err == Z_BUF_ERROR && stream.avail_in == 0))
			return Z_DATA_ERROR;
		return err;
	}
	*destLen = stream.total_out;

	err = inflateEnd(&stream);
	return err;
}

int gbcompress ( unsigned char *dest      ,
		 uint32_t *destLen   ,
		 unsigned char *source    ,
		 uint32_t  sourceLen ,
		 int32_t encoding            ) {

	int level = Z_DEFAULT_COMPRESSION;
	z_stream stream;
	int err;
	int method     = Z_DEFLATED;
	//lots of mem, faster, more compressed, see zlib.h
	int windowBits = 31;
	int memLevel   = 8;
	int strategy   = Z_DEFAULT_STRATEGY;

	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
#ifdef MAXSEG_64K
	// Check for source > 64K on 16-bit machine:
	if ((uLong)stream.avail_in != sourceLen) return Z_BUF_ERROR;
#endif
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

	//stream.zalloc = (alloc_func)0;
	//stream.zfree = (free_func)0;
	stream.zalloc = malloc_replace;//zliballoc;
	stream.zfree  = free_replace;//zlibfree;

	stream.opaque = (voidpf)0;

	//we can be gzip or deflate
	if(encoding == ET_DEFLATE) err = deflateInit (&stream, level);
	else                       err = deflateInit2(&stream, level,
						      method, windowBits,
						      memLevel, strategy);
	if (err != Z_OK) {
		// zlib's incompatible version error?
		if ( err == -6 ) {
			log("zlib: zlib did you forget to add #pragma pack(4) to "
			    "zlib.h when compiling libz.a so it aligns on 4-byte "
			    "boundaries because we have that pragma in "
			    "gb-include.h so its used when including zlib.h");
		}
		return err;
	}

	err = deflate(&stream, Z_FINISH);

	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = deflateEnd(&stream);
	return err;
}

// is it a custom error page? ppl do not always use status 404!
char *XmlDoc::getIsErrorPage ( ) {
	if ( m_isErrorPageValid ) {
		return &m_isErrorPage;
	}

	setStatus ( "getting is error page");

	// need a buncha crap
	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (char *)xml;
	// get local link info
	LinkInfo   *info1  = getLinkInfo1();
	// error or blocked
	if ( ! info1 || info1 == (LinkInfo *)-1 ) return (char *)info1;

	// default
	LinkInfo  *li = info1;

	//we have to be more sophisticated with longer pages because they
	//are could actually be talking about an error message.
	//if(xml->getContentLen() > 4096) return false;

	// assume not
	m_isErrorPage      = false;
	m_isErrorPageValid = true;

	int32_t nn = xml->getNumNodes();
	int32_t i;

	char* s;
	int32_t len;
	int32_t len2;

	const char* errMsg = NULL;

	int32_t numChecked = 0;
	// check the first header and title tag
	// limit it to first 32 nodes
	if(nn > 32) nn = 32;
	for ( i = 0 ; i < nn ; i++ ) {
		switch(xml->getNodeId(i)) {
		case TAG_TITLE:
		case TAG_H1:
		case TAG_H2:
		case TAG_H3:
		case TAG_SPAN:
			char* p = xml->getString(i,true,&len);
			if(len == 0 || len > 1024) continue;
			char* pend = p + len;
			errMsg = matchErrorMsg(p, pend );
			++numChecked;
			break;
		}
		if(errMsg || numChecked > 1) break;
	}
	if(!errMsg) return &m_isErrorPage;
	len = strlen(errMsg);

	// make sure the error message was not present in the link text
	if ( li && li->getNumGoodInlinks() > 5 ) return &m_isErrorPage;
	for (Inlink *k=NULL;li && (k=li->getNextInlink(k)); ) {
		//int32_t nli = li->getNumLinkTexts();
		//if we can index some link text from the page, then do it
		//if(nli > 5) return false;
		//for ( int32_t i = 0 ; i < nli ; i++ ) {
		s    = k->getLinkText();
		len2 = k->size_linkText - 1; // exclude \0
		//if(!s) break;
		//allow error msg to contain link text or vice versa
		if(len < len2) {
			if(strncasestr(errMsg, s,len,len2) != NULL)
				return &m_isErrorPage;
		}
		else {
			if(strncasestr(s, errMsg,len2,len) != NULL)
				return &m_isErrorPage;
		}
	}

	m_isErrorPage = true;
	return &m_isErrorPage;
}


const char* XmlDoc::matchErrorMsg(char* p, char* pend ) {
 	char utf8Buf[1024];
	//	int32_t utf8Len = 0;
	int32_t len = pend - p;

	if(len > 1024) len = 1024;
	pend = p + len;
	char* tmp = utf8Buf;
	while(p < pend) {
		*tmp = to_lower_a(*p);
		tmp++; p++;
	}

	p = utf8Buf;
	pend = p + len;

	const char* errMsg = NULL;

	while(p < pend) {
		int32_t r = pend - p;
		switch (*p) { //sorted by first letter, then by frequency
		case '4':
			errMsg = "404 error";
			if(r>=9&&strncmp(p, errMsg, 9) == 0) return errMsg;
			errMsg = "403 forbidden";
			if(r>=13&&strncmp(p, errMsg, 13) == 0) return errMsg;
			break;

		case 'd':
			errMsg = "detailed error information follows";
			if(r>=34&&strncmp(p, errMsg, 34) == 0) return errMsg;
			break;

		case 'e':
			errMsg = "error 404";
			if(r>=9&&strncmp(p, errMsg, 9) == 0) return errMsg;
			errMsg = "error was encountered while processing "
				"your request";
			if(r>=51&&strncmp(p, errMsg,51) == 0) return errMsg;

			errMsg = "error occurred while processing request";
			if(r>=39&&strncmp(p, errMsg, 39) == 0) return errMsg;
			errMsg = "exception error has occurred";
			if(r>=28&&strncmp(p, errMsg,28) == 0) return errMsg;
			errMsg = "error occurred";
			if(r>=14&&strncmp(p, errMsg,14) == 0) return errMsg;
			//http://www.gnu.org/fun/jokes/unix.errors.html
			//errMsg = "error message";
			//if(strncmp(p, errMsg, 13) == 0) return errMsg;
			break;

		case 'f':
			errMsg = "file not found";
			if(r>=14&&strncmp(p, errMsg, 14) == 0) return errMsg;
			break;

		case 'h':
			errMsg = "has moved";
			if(r>=9&&strncmp(p, errMsg, 9) == 0) return errMsg;
			break;

		case 'n':
			errMsg = "no referrer";
			if(r>=12&&strncmp(p, errMsg,12) == 0) return errMsg;
			break;

		case 'o':
			errMsg = "odbc error code = ";
			if(r>=18&&strncmp(p, errMsg,18) == 0) return errMsg;
			errMsg = "object not found";
			if(r>=16&&strncmp(p, errMsg,16) == 0) return errMsg;
			break;

		case 'p':
			errMsg = "page not found";
			if(r>=14&&strncmp(p, errMsg,14) == 0) return errMsg;
			break;

		case 's':
			errMsg = "system error";
			if(r>=12&&strncmp(p, errMsg, 12) == 0) return errMsg;
			break;
		case 't':
			errMsg = "the application encountered an "
				"unexpected problem";
			if(r>=49&&strncmp(p, errMsg, 49) == 0) return errMsg;
			errMsg = "the page you requested has moved";
			if(r>=32&&strncmp(p, errMsg, 32) == 0) return errMsg;
			errMsg = "this page has moved";
			if(r>=19&&strncmp(p, errMsg, 19) == 0) return errMsg;
			break;

		case 'u':
			errMsg = "unexpected problem has occurred";
			if(r>=31&&strncmp(p, errMsg, 31) == 0) return errMsg;
			errMsg = "unexpected error has occurred";
			if(r>=29&&strncmp(p, errMsg, 29) == 0) return errMsg;
			errMsg = "unexpected problem occurred";
			if(r>=27&&strncmp(p, errMsg, 27) == 0) return errMsg;
			errMsg ="unexpected error occurred";
			if(r>=25&&strncmp(p, errMsg, 25) == 0) return errMsg;
			errMsg ="unexpected result has occurred";
			if(r>=33&&strncmp(p, errMsg, 33) == 0) return errMsg;
			errMsg ="unhandled exception";
			if(r>=19&&strncmp(p, errMsg, 19) == 0) return errMsg;

			break;

		case 'y':
			errMsg = "you have been blocked";
			if(r>=21&&strncmp(p, errMsg, 21) == 0) return errMsg;
			break;
		}
		//skip to the beginning of the next word
		while(p < pend && !is_wspace_a(*p)) p++;
		while(p < pend && is_wspace_a(*p)) p++;
	}
	return NULL;
}

#include "Spider.h"

static SafeBuf *s_wbuf = NULL;

// . this is used by gbsort() above
// . sorts TermInfos alphabetically by their TermInfo::m_term member
static int cmptp (const void *v1, const void *v2) {
	TermDebugInfo *t1 = *(TermDebugInfo **)v1;
	TermDebugInfo *t2 = *(TermDebugInfo **)v2;

	char *start = s_wbuf->getBufStart();

	// prefix first
	char *ps1 = start + t1->m_prefixOff;
	char *ps2 = start + t2->m_prefixOff;
	if ( t1->m_prefixOff < 0 ) ps1 = NULL;
	if ( t2->m_prefixOff < 0 ) ps2 = NULL;
	int32_t plen1 = 0; if ( ps1 ) plen1 = strlen(ps1);
	int32_t plen2 = 0; if ( ps2 ) plen2 = strlen(ps2);
	int32_t pmin = plen1;
	if ( plen2 < pmin ) pmin = plen2;
	int32_t pn = strncmp ( ps1 , ps2 , pmin );
	if ( pn ) return pn;
	if ( plen1 != plen2 ) return ( plen1 - plen2 );

	// return if groups differ
	int32_t len1 = t1->m_termLen;
	int32_t len2 = t2->m_termLen;
	int32_t min = len1;
	if ( len2 < min ) min = len2;
	char *s1    = start + t1->m_termOff;
	char *s2    = start + t2->m_termOff;
	int32_t n = strncasecmp ( s1 , s2 , min );
	if ( n ) return n;
	// . if length same, we are tied
	// . otherwise, prefer the shorter
	return ( len1 - len2 );
}

// . this is used by gbsort() above
// . sorts TermDebugInfos by their TermDebugInfo::m_wordPos member
static int cmptp2 (const void *v1, const void *v2) {
	TermDebugInfo *t1 = *(TermDebugInfo **)v1;
	TermDebugInfo *t2 = *(TermDebugInfo **)v2;
	// word position first
	int32_t d = t1->m_wordPos - t2->m_wordPos;
	if ( d ) return d;
	// secondly drop back to hashgroup i guess
	//d = t1->m_hashGroup - t2->m_hashGroup;
	d = t1->m_synSrc - t2->m_synSrc;
	if ( d ) return d;
	// word len
	d = t1->m_termLen - t2->m_termLen;
	if ( d ) return d;
	return 0;
}

static bool printLangBits ( SafeBuf *sb , TermDebugInfo *tp ) {

	char printed = false;
	if ( tp->m_synSrc ) {
		sb->safePrintf("&nbsp;");
		printed = true;
	}
	int32_t j = 0;
	if ( printed ) j = MAX_LANGUAGES;
	for ( ; j < MAX_LANGUAGES ; j++ ) {
		int64_t mask = 1LL << j;
		//if ( j == tp->m_langId )
		//	sb->safePrintf("[%s]",
		//		       getLanguageAbbr(tp->m_langId));
		if ( ! (tp->m_langBitVec64 & mask) ) continue;
		char langId = j+1;
		// match in langvec? that means even if the
		// word is in multiple languages we put it in
		// this language because we interesect its lang bit
		// vec with its neighbors in the sliding window
		// algo in setLangVector.
		if ( langId == tp->m_langId )
			sb->safePrintf("<b>");
		sb->safePrintf("%s ", getLanguageAbbr(langId) );
		if ( langId == tp->m_langId )
			sb->safePrintf("</b>");
		printed = true;
	}
	if ( ! printed ) {
		sb->safePrintf("??");
	}
	return true;
}

bool XmlDoc::printDoc ( SafeBuf *sb ) {

	if ( ! sb ) return true;


	// shortcut
	char *fu = ptr_firstUrl;

	const char *allowed = "???";
	if      ( m_isAllowedValid && m_isAllowed )  allowed = "yes";
	else if ( m_isAllowedValid                )  allowed = "no";

	int32_t ufn = -1;
	if ( m_urlFilterNumValid ) ufn = m_urlFilterNum;
	time_t spideredTime = getSpideredTime();

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;

	sb->safePrintf ("<meta http-equiv=\"Content-Type\" "
			"content=\"text/html; charset=utf-8\">"

			"<table cellpadding=3 border=0>\n"

			"<tr>"
			"<td width=\"25%%\">docId</td>"
			"<td><a href=/get?c=%s&d=%" PRIu64">%" PRIu64"</a></td>"
			"</tr>\n"

			"<tr>"
			"<td width=\"25%%\">uh48</td>"
			"<td>%" PRIu64"</td>"
			"</tr>\n"

			"<tr>"
			"<td width=\"25%%\">uh64</td>"
			"<td>%" PRIu64"</td>"
			"</tr>\n"

			"<tr>"
			"<td>index error code</td>"
			"<td>%s</td>"
			"</tr>\n"

			"<tr>"
			"<td>http status</td>"
			"<td>%i</td>"
			"</tr>\n"

			"<tr>"
			"<td>url filter num</td>"
			"<td>%" PRId32"</td>"
			"</tr>\n"


			"<tr>"
			"<td>other - errno</td>"
			"<td>%s</td>"
			"</tr>\n"

			"<tr>"
			"<td>robots.txt allows</td>"
			"<td>%s</td>"
			"</tr>\n"

			"<tr>"
			"<td>metalist size</td>"
			"<td>%" PRId32"</td>"
			"</tr>\n"


			"<tr>"
			"<td>url</td>"
			"<td><a href=\"%s\">%s</a></td>"
			"</tr>\n"

			,
			cr->m_coll,
			m_docId ,
			m_docId ,
			getFirstUrlHash48(), // uh48
			getFirstUrlHash64(), // uh48

			mstrerror(m_indexCode),
			m_httpStatus,
			ufn,
			mstrerror(g_errno),
			allowed,

			m_metaListSize,

			fu,
			fu

			);

	if ( ptr_redirUrl )
		sb->safePrintf(
			       "<tr>"
			       "<td>redir url</td>"
			       "<td><a href=\"%s\">%s</a></td>"
			       "</tr>\n"
			       ,ptr_redirUrl
			       ,ptr_redirUrl
			       );
	else
		sb->safePrintf(
			       "<tr>"
			       "<td>redir url</td>"
			       "<td>--</td>"
			       "</tr>\n"
			       );


	sb->safePrintf("<tr><td>hostHash64</td><td>0x%" PRIx64"</td></tr>",
		       (uint64_t)getHostHash32a());
	sb->safePrintf("<tr><td>site</td><td>");
	sb->safeMemcpy(ptr_site,size_site-1);
	sb->safePrintf("</td></tr>\n");
	if ( m_siteHash32Valid )
		sb->safePrintf("<tr><td>siteHash32</td><td>0x%" PRIx32"</td></tr>\n",
			       m_siteHash32);
	if ( m_domHash32Valid )
		sb->safePrintf("<tr><td>domainHash32</td><td>0x%" PRIx32"</td></tr>\n",
			       m_domHash32);
	sb->safePrintf ( "<tr>"
			 "<td>domainHash8</td>"
			 "<td>0x%" PRIx32"</td>"
			 "</tr>\n"
			 ,
			 (int32_t)Titledb::getDomHash8FromDocId(m_docId)
			 );

	struct tm tm_buf;
	char buf[64];
	sb->safePrintf(
			"<tr>"
			"<td>coll</td>"
			"<td>%s</td>"
			"</tr>\n"

			"<tr>"
			"<td>spidered date</td>"
			"<td>%s UTC</td>"
			"</tr>\n"
			,
			cr->m_coll,
			asctime_r(gmtime_r(&spideredTime,&tm_buf),buf)
			);


	/*
	char *ms = "-1";
	if ( m_minPubDate != -1 ) ms = asctime_r(gmtime_r( &m_minPubDate ));
	sb->safePrintf (
			"<tr>"
			"<td>min pub date</td>"
			"<td>%s UTC</td>"
			"</tr>\n" , ms );

	ms = "-1";
	if ( m_maxPubDate != -1 ) ms = asctime_r(gmtime_r( &m_maxPubDate ));
	sb->safePrintf (
			"<tr>"
			"<td>max pub date</td>"
			"<td>%s UTC</td>"
			"</tr>\n" , ms );
	*/

	// our html template fingerprint
	sb->safePrintf ("<tr><td>tag pair hash 32</td><td>");
	if ( m_tagPairHash32Valid )sb->safePrintf("%" PRIu32,
						  (uint32_t)m_tagPairHash32);
	else                       sb->safePrintf("invalid");
	sb->safePrintf("</td></tr>\n" );


	// print list we added to delete stuff
	if ( m_indexCode && m_oldDocValid && m_oldDoc ) {
		// skip debug printing for now...
		//return true;
		sb->safePrintf("</table><br>\n");
		sb->safePrintf("<h2>Delete Meta List</h2>");
		printMetaList ( m_metaList , m_metaList + m_metaListSize ,sb);
	}


	if ( m_indexCode || g_errno ) {
		printMetaList ( m_metaList , m_metaList + m_metaListSize, sb );
	}

	if ( m_indexCode ) return true;
	if ( g_errno     ) return true;


	// sanity check
	//if ( ! m_sreqValid ) { g_process.shutdownAbort(true); }

	/*
	sb->safePrintf("<tr><td>next spider date</td>"
		       "<td>%s UTC</td></tr>\n"

		       "<tr><td>next spider priority</td>"
		       "<td>%" PRId32"</td></tr>\n" ,
		       asctime_r(gmtime_r( &m_nextSpiderTime )) ,
		       (int32_t)m_nextSpiderPriority );
	*/

	// must always start with http i guess!
	if ( strncmp ( fu , "http" , 4 ) ) { g_process.shutdownAbort(true); }
	// show the host that should spider it
	//int32_t domLen ; char *dom = getDomFast ( fu , &domLen , true );
	//int32_t hostId;
	if ( m_sreqValid ) {
		// must not block
		SpiderRequest *oldsr = &m_sreq;
		uint32_t shard = g_hostdb.getShardNum(RDB_SPIDERDB,oldsr);
		sb->safePrintf ("<tr><td><b>assigned spider shard</b>"
				"</td>\n"
				"<td><b>%" PRIu32"</b></td></tr>\n",shard);
	}

	time_t ts = m_firstIndexedDate;
	sb->safePrintf("<tr><td>first indexed date</td>"
		       "<td>%s UTC</td></tr>\n" ,
		       asctime_r(gmtime_r(&ts,&tm_buf),buf) );

	ts = m_outlinksAddedDate;
	sb->safePrintf("<tr><td>outlinks last added date</td>"
		       "<td>%s UTC</td></tr>\n" ,
		       asctime_r(gmtime_r(&ts,&tm_buf),buf) );

	// hop count
	sb->safePrintf("<tr><td>hop count</td><td>%" PRId32"</td></tr>\n",
		      (int32_t)m_hopCount);

	// thumbnails
	ThumbnailArray *ta = (ThumbnailArray *) ptr_imageData;
	if ( ta ) {
		int32_t nt = ta->getNumThumbnails();
		sb->safePrintf("<tr><td># thumbnails</td>"
			       "<td>%" PRId32"</td></tr>\n",nt);
		for ( int32_t i = 0 ; i < nt ; i++ ) {
			ThumbnailInfo *ti = ta->getThumbnailInfo(i);
			sb->safePrintf("<tr><td>thumb #%" PRId32"</td>"
				       "<td>%s (%" PRId32"x%" PRId32",%" PRId32"x%" PRId32") "
				       , i
				       , ti->getUrl()
				       , ti->m_origDX
				       , ti->m_origDY
				       , ti->m_dx
				       , ti->m_dy
				       );
			ti->printThumbnailInHtml ( sb , 100,100,true,NULL) ;
			// end the row for this thumbnail
			sb->safePrintf("</td></tr>\n");
		}
	}



	const char *ddd = "---";

	char strLanguage[128];
	languageToString(m_langId, strLanguage);

	SafeBuf tb;

	TagRec *ogr = NULL;
	if ( m_tagRecValid ) ogr = &m_tagRec;
	if ( ogr ) ogr->printToBufAsHtml ( &tb , "old tag" );

	SafeBuf *ntb = NULL;
	if ( m_newTagBufValid ) ntb = getNewTagBuf();
	if ( ntb ) {
		// this is just a sequence of tags like an rdblist
		char *pt    = ntb->getBufStart();
		char *ptend = pt + ntb->length();
		for ( ; pt < ptend ; ) {
			// skip rdbid
			pt++;
			// cast it
			Tag *tag = (Tag *)pt;
			// skip it
			pt += tag->getRecSize();
			// print tag out
			tag->printToBufAsHtml ( &tb, "new tag");
		}
	}


	// prevent (null) from being displayed
	tb.pushChar('\0');

	int32_t sni  = m_siteNumInlinks;

	LinkInfo *info1 = ptr_linkInfo1;

	char *ipString = iptoa(m_ip);
	const char *estimated = "";

	//char *ls = getIsLinkSpam();
	Links *links = getLinks();
	// sanity check. should NEVER block!
	if ( links == (void *)-1 ) { g_process.shutdownAbort(true); }

	// this is all to get "note"
	//char *note = NULL;
	// make it a URL
	Url uu; uu.set ( ptr_firstUrl );
	// sanity check
	Xml *xml = getXml();
	// sanity check
	if ( xml == (void *)-1 ) { g_process.shutdownAbort(true); }

	sb->safePrintf (
		  "<tr><td>datedb date</td><td>%s UTC (%" PRIu32")%s"
		  "</td></tr>\n"

		  "<tr><td>compressed size</td><td>%" PRId32" bytes</td></tr>\n"

		  "<tr><td>original charset</td><td>%s</td></tr>\n"

		  //"<tr><td>site num inlinks</td><td><b>%" PRId32"%</b></td></tr>\n"

		  //"<tr><td>total extrapolated linkers</td><td>%" PRId32"</td></tr>\n"

		  "<tr><td><b>title rec version</b></td><td><b>%" PRId32"</b>"
		  "</td></tr>\n"

		  "<tr><td>adult bit</td><td>%" PRId32"</td></tr>\n"

		  //"<tr><td>is link spam?</td><td>%" PRId32" <b>%s</b></td></tr>\n"

		  "<tr><td>is permalink?</td><td>%" PRId32"</td></tr>\n"
		  "<tr><td>is RSS feed?</td><td>%" PRId32"</td></tr>\n"
		  //"<tr><td>index article only?</td><td>%" PRId32"</td></tr>\n"
		  "%s\n"
		  "<tr><td>ip</td><td><a href=\"/search?q=ip%%3A%s&c=%s&n=100\">"
		  "%s</td></tr>\n"
		  "<tr><td>content len</td><td>%" PRId32" bytes</td></tr>\n"
		  "<tr><td>content truncated</td><td>%" PRId32"</td></tr>\n"

		  "<tr><td>content type</td><td>%" PRId32" (%s)</td></tr>\n"
		  "<tr><td>language</td><td>%" PRId32" (%s)</td></tr>\n"
		  "<tr><td>country</td><td>%" PRId32" (%s)</td></tr>\n"
		  "<tr><td>time axis used</td><td>%" PRId32"</td></tr>\n"
		  "<tr><td>metadata</td><td>%s</td></tr>\n"
		  "</td></tr>\n",

		  ddd ,
		  0 ,
		  estimated ,

		  m_oldTitleRecSize,

		  get_charset_str(m_charset),

		  //sni ,

		  //ptr_linkInfo1->m_numInlinksExtrapolated,

		  (int32_t)m_version ,

		  (int32_t)m_isAdult,

		  //(int32_t)m_isLinkSpam,
		  //m_note,

		  (int32_t)m_isPermalink,

		  (int32_t)m_isRSS,


		  //(int32_t)m_eliminateMenus,


		  // tag rec
		  tb.getBufStart(),

		  ipString,
		  cr->m_coll,
		  ipString,
		  size_utf8Content - 1,
		  (int32_t)m_isContentTruncated,

		  (int32_t)m_contentType,
		  g_contentTypeStrings[(int)m_contentType] ,

		  (int32_t)m_langId,
		  strLanguage,

		  (int32_t)m_countryId,
		  g_countryCode.getName(m_countryId),
		  m_useTimeAxis,
		  "");

	if ( info1 ) {
		sb->safePrintf("<tr><td>num GOOD links to whole site</td>"
			       "<td>%" PRId32"</td></tr>\n",
			       sni );
	}

	// close the table
	sb->safePrintf ( "</table></center><br>\n" );

	// print outlinks
	links->print( sb );

	//
	// PRINT SECTIONS
	//
	Sections *sections = getSections();
	if ( ! sections ||sections==(Sections *)-1) {g_process.shutdownAbort(true);}

	printRainbowSections ( sb , NULL );

	//
	// PRINT LINKINFO
	//

	char *p    = m_pageLinkBuf.getBufStart();
	int32_t  plen = m_pageLinkBuf.length();
	sb->safeMemcpy ( p , plen );


	//
	// PRINT SITE LINKINFO
	//
	p    = m_siteLinkBuf.getBufStart();
	plen = m_siteLinkBuf.length();
	sb->safeMemcpy ( p , plen );


	// note this
	sb->safePrintf("<h2>NEW Meta List</h2>");

	printMetaList ( m_metaList , m_metaList + m_metaListSize , sb );

	// all done if no term table to print out
	if ( ! m_wts ) return true;


	//
	// BEGIN PRINT HASHES TERMS
	//

	// shortcut
	HashTableX *wt = m_wts;

	// use the keys to hold our list of ptrs to TermDebugInfos for sorting!
	TermDebugInfo **tp = NULL;
	// add them with this counter
	int32_t nt = 0;

	int32_t nwt = 0;
	if ( wt ) {
		nwt = wt->m_numSlots;
		tp = (TermDebugInfo **)wt->m_keys;
	}

	// now print the table we stored all we hashed into
	for ( int32_t i = 0 ; i < nwt ; i++ ) {
		// skip if empty
		if ( wt->m_flags[i] == 0 ) continue;
		// get its key, date=32bits termid=64bits
		//key96_t *k = (key96_t *)wt->getKey ( i );
		// get the TermDebugInfo
		TermDebugInfo *ti = (TermDebugInfo *)wt->getValueFromSlot ( i );
		// point to it for sorting
		tp[nt++] = ti;
	}

	// set this for cmptp
	s_wbuf = &m_wbuf;

	// sort them alphabetically by Term
	gbsort ( tp , nt , sizeof(TermDebugInfo *), cmptp );

	// print them out in a table
	char hdr[1000];
	sprintf(hdr,
		"<table border=1 cellpadding=0>"
		"<tr>"
		"<td><b>Prefix</b></td>"
		"<td><b>WordNum</b></td>"
		"<td><b>Lang</b></td>"
		"<td><b>Term</b></td>"
		"<td><b>Desc</b></td>"
		"<td><b>TermId/TermHash48</b></td>"
		"<td><b>ShardByTermId?</b></td>"
		"</tr>\n"
		);

	sb->safePrintf("%s",hdr);

	char *start = m_wbuf.getBufStart();
	int32_t rcount = 0;

	for ( int32_t i = 0 ; i < nt ; i++ ) {


		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS) == 0 )
			sb->safePrintf("</table>%s",hdr);

		const char *prefix = "&nbsp;";
		if ( tp[i]->m_prefixOff >= 0 )
			prefix = start + tp[i]->m_prefixOff;

		sb->safePrintf ( "<tr><td>%s</td>", prefix);

		sb->safePrintf( "<td>%" PRId32 "</td>", tp[i]->m_wordNum );

		// print out all langs word is in if it's not clear
		// what language it is. we use a sliding window to
		// resolve some ambiguity, but not all, so print out
		// the possible langs here
		sb->safePrintf("<td>");
		printLangBits ( sb , tp[i] );
		sb->safePrintf("</td>");

		// print the term
		sb->safePrintf("<td><nobr>");

		if ( tp[i]->m_synSrc ) {
			sb->pushChar('*');
		}

		char *term = start + tp[i]->m_termOff;
		int32_t  termLen = tp[i]->m_termLen;
		sb->safeMemcpy ( term , termLen );

		sb->safePrintf ( "</nobr></td>");

		sb->safePrintf( "<td><nobr>%s</nobr></td>", getHashGroupString( tp[i]->m_hashGroup ) );

		sb->safePrintf ( "<td>%016" PRIu64"</td>", (uint64_t)(tp[i]->m_termId & TERMID_MASK) );

		if ( tp[i]->m_shardByTermId ) {
			sb->safePrintf( "<td><b>1</b></td>" );
		} else {
			sb->safePrintf( "<td>0</td>" );
		}

		sb->safePrintf("</tr>\n");
	}


	sb->safePrintf("</table><br>\n");

	//
	// END PRINT HASHES TERMS
	//

	return true;
}

bool XmlDoc::printMenu ( SafeBuf *sb ) {

	// encode it
	SafeBuf ue;
	ue.urlEncode ( ptr_firstUrl );

	// get
	sb->safePrintf ("<meta http-equiv=\"Content-Type\" "
			"content=\"text/html; charset=utf-8\">" );

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;

	return true;
}

// if printDocForProCog, an entry function, blocks, we gotta re-call it
static void printDocForProCogWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	// make sure has not been freed from under us!
	if ( THIS->m_freed ) { g_process.shutdownAbort(true);}
	// note it
	THIS->setStatus ( "in print doc for pro cog wrapper" );
	// get it
	bool status = THIS->printDocForProCog ( THIS->m_savedSb ,
						THIS->m_savedHr );
	// return if it blocked
	if ( ! status ) return;
	// otherwise, all done, call the caller callback
	THIS->callCallback();
}


// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool XmlDoc::printDocForProCog ( SafeBuf *sb , HttpRequest *hr ) {

	if ( ! sb ) return true;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return true;

	m_masterLoop = printDocForProCogWrapper;
	m_masterState = this;

	m_savedSb = sb;
	m_savedHr = hr;

	// if we are generating site or page inlinks info for a
	// non docid based url, then store that info in the respective
	// safe bufs
	m_useSiteLinkBuf = true;
	m_usePageLinkBuf = true;


	int32_t page = hr->getLong("page",1);


	// for some reason sections page blocks forever in browser
	if ( page != 7 && ! m_printedMenu ) {
		printFrontPageShell ( sb , "search" , cr , false );
		m_printedMenu = true;
		//printMenu ( sb );
	}


	if ( page == 1 )
		return printGeneralInfo(sb,hr);

	if ( page == 2 )
		return printPageInlinks(sb,hr);

	if ( page == 3 )
		return printSiteInlinks(sb,hr);

	if ( page == 4 )
		return printRainbowSections(sb,hr);

	if ( page == 5 )
		return printTermList(sb,hr);

	if ( page == 6 )
		return printSpiderStats(sb,hr);

	if ( page == 7 )
		return printCachedPage(sb,hr);

	return true;
}

bool XmlDoc::printGeneralInfo ( SafeBuf *sb , HttpRequest *hr ) {

	// shortcut
	char *fu = ptr_firstUrl;

	// sanity check
	Xml *xml = getXml();
	// blocked?
	if ( xml == (void *)-1 ) return false;
	// error?
	if ( ! xml ) return true;

	char *ict = getIsContentTruncated();
	if ( ! ict ) return true;
	if ( ict == (char *)-1 ) return false;

	char *at = getIsAdult();
	if ( ! at ) return true;
	if ( at == (void *)-1 ) return false;

	char *ls = getIsLinkSpam();
	if ( ! ls ) return true;
	if ( ls == (void *)-1 ) return false;

	uint8_t *ct = getContentType();
	if ( ! ct ) return true;
	if ( ct == (void *)-1 ) return false;

	uint16_t *cs = getCharset ( );
	if ( ! cs ) return true;
	if ( cs == (uint16_t *)-1 ) return false;

	char *pl = getIsPermalink();
	if ( ! pl ) return true;
	if ( pl == (char *)-1 ) return false;

	char *isRSS   = getIsRSS();
	if ( ! isRSS ) return true;
	if ( isRSS == (char  *)-1 )  return false;

	int32_t *ip = getIp();
	if ( ! ip ) return true;
	if ( ip == (int32_t *)-1 ) return false;

	uint8_t *li = getLangId();
	if ( ! li ) return true;
	if ( li == (uint8_t *)-1 ) return false;

	uint16_t *cid = getCountryId();
	if ( ! cid ) return true;
	if ( cid == (uint16_t *)-1 ) return false;

	LinkInfo   *info1  = getLinkInfo1();
	if ( ! info1 ) return true;
	if ( info1 == (void *)-1 ) return false;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return true;

	// make it a URL
	Url uu;
	uu.set ( fu );

	const char *allowed = "???";
	int32_t allowedInt = 1;
	if      ( m_isAllowedValid && m_isAllowed )  {
		allowed = "yes";
		allowedInt = 1;
	}
	else if ( m_isAllowedValid                )  {
		allowed = "no";
		allowedInt = 0;
	}

	int32_t ufn = -1;
	if ( m_urlFilterNumValid ) ufn = m_urlFilterNum;

	const char *es = mstrerror(m_indexCode);
	if ( ! m_indexCode ) es = mstrerror(g_errno);

	int32_t isXml = hr->getLong("xml",0);

	if ( ! isXml ) printMenu ( sb );

	//int32_t groupId = g_hostdb.getGroupIdFromDocId(m_docId);
	//Host *group = g_hostdb.getGroup(groupId);
	int32_t shardNum = getShardNumFromDocId ( m_docId );
	Host *hosts = g_hostdb.getShard ( shardNum );
	Host *h = &hosts[0];

	if ( ! isXml )
		sb->safePrintf (
				"<table cellpadding=3 border=0>\n"

				"<tr>"
				"<td width=\"25%%\">docId</td>"
				"<td><a href=/get?c=%s&d=%" PRIu64">%" PRIu64"</a></td>"
				"</tr>\n"

				"<tr>"
				"<td width=\"25%%\">on host #</td>"
				"<td>%" PRId32"</td>"
				"</tr>\n"

				"<tr>"
				"<td>index error code</td>"
				"<td>%s</td>"
				"</tr>\n"


				"<tr>"
				"<td>robots.txt allows</td>"
				"<td>%s</td>"
				"</tr>\n"


				"<tr>"
				"<td>url</td>"
				"<td><a href=\"%s\">%s</a></td>"
				"</tr>\n"

				,
				cr->m_coll,
				m_docId ,
				m_docId ,

				h->m_hostId,

				es,
				allowed,

				fu,
				fu

				);
	else
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<coll><![CDATA[%s]]></coll>\n"
				"\t<docId>%" PRId64"</docId>\n"
				"\t<indexError><![CDATA[%s]]></indexError>\n"
				"\t<robotsTxtAllows>%" PRId32
				"</robotsTxtAllows>\n"
				"\t<url><![CDATA[%s]]></url>\n"
				,
				cr->m_coll,
				m_docId ,
				es,
				allowedInt,//(int32_t)m_isAllowed,
				fu
				);

	char *redir = ptr_redirUrl;
	if ( redir && ! isXml ) {
		sb->safePrintf(
			       "<tr>"
			       "<td>redir url</td>"
			       "<td><a href=\"%s\">%s</a></td>"
			       "</tr>\n"
			       ,redir
			       ,redir );
	}
	else if ( redir ) {
		sb->safePrintf("\t<redirectUrl><![CDATA[%s]]>"
			       "</redirectUrl>\n" ,redir );
	}


	if ( m_indexCode || g_errno ) {
		if ( ! isXml ) sb->safePrintf("</table><br>\n");
		else           sb->safePrintf("</response>\n");
		return true;
	}


	// must always start with http i guess!
	if ( strncmp ( fu , "http" , 4 ) ) { g_process.shutdownAbort(true); }

	struct tm tm_buf;
	char buf[64];
	time_t ts = (time_t)m_firstIndexedDate;

	if ( ! isXml )
		sb->safePrintf("<tr><td>first indexed date</td>"
			       "<td>%s UTC</td></tr>\n" ,
			       asctime_r(gmtime_r(&ts,&tm_buf),buf) );
	else
		sb->safePrintf("\t<firstIndexedDateUTC>%" PRIu32
			       "</firstIndexedDateUTC>\n",
			       (uint32_t)m_firstIndexedDate );

	ts = m_spideredTime;

	if ( ! isXml )
		sb->safePrintf("<tr><td>last indexed date</td>"
			       "<td>%s UTC</td></tr>\n" ,
			       asctime_r(gmtime_r(&ts,&tm_buf),buf) );
	else
		sb->safePrintf("\t<lastIndexedDateUTC>%" PRIu32
			       "</lastIndexedDateUTC>\n",
			       (uint32_t)m_spideredTime );

	ts = m_outlinksAddedDate;

	if ( ! isXml )
		sb->safePrintf("<tr><td>outlinks last added date</td>"
			       "<td>%s UTC</td></tr>\n" ,
			       asctime_r(gmtime_r(&ts,&tm_buf),buf) );
	else
		sb->safePrintf("\t<outlinksLastAddedUTC>%" PRIu32
			       "</outlinksLastAddedUTC>\n",
			       (uint32_t)m_outlinksAddedDate );

	// hop count
	if ( ! isXml )
		sb->safePrintf("<tr><td>hop count</td><td>%" PRId32"</td>"
			       "</tr>\n",
			       (int32_t)m_hopCount);
	else
		sb->safePrintf("\t<hopCount>%" PRId32"</hopCount>\n",
			       (int32_t)m_hopCount);


	char strLanguage[128];
	languageToString(m_langId, strLanguage);

	// print tags
	//SafeBuf tb;
	int32_t sni  = m_siteNumInlinks;

	char *ipString = iptoa(m_ip);

	//int32_t sni = info1->getNumGoodInlinks();

	time_t tlu = info1->getLastUpdated();
	struct tm *timeStruct3 = gmtime_r(&tlu,&tm_buf);//info1->m_lastUpdated );
	char tmp3[64];
	strftime ( tmp3 , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct3 );


	if ( ! isXml )
		sb->safePrintf (
			"<tr><td>original charset</td><td>%s</td></tr>\n"
			"<tr><td>adult bit</td><td>%" PRId32"</td></tr>\n"
			//"<tr><td>is link spam?</td><td>%" PRId32" <b>%s</b></td></tr>\n"
			"<tr><td>is permalink?</td><td>%" PRId32"</td></tr>\n"
			"<tr><td>is RSS feed?</td><td>%" PRId32"</td></tr>\n"
			"<tr><td>ip</td><td><a href=\"/search?q=ip%%3A%s&c=%s&n=100\">"
			"%s</td></tr>\n"
			"<tr><td>content len</td><td>%" PRId32" bytes</td></tr>\n"
			"<tr><td>content truncated</td><td>%" PRId32"</td></tr>\n"
			"<tr><td>content type</td><td>%s</td></tr>\n"
			"<tr><td>language</td><td>%s</td></tr>\n"
			"<tr><td>country</td><td>%s</td></tr>\n"

			"<tr><td><b>good inlinks to site</b>"
			"</td><td>%" PRId32"</td></tr>\n"

			"<tr><td><b>site rank</b></td><td>%" PRId32"</td></tr>\n"

			"<tr><td>good inlinks to page"
			"</td><td>%" PRId32"</td></tr>\n"

			"<tr><td><nobr>page inlinks last computed</nobr></td>"
			"<td>%s</td></tr>\n"
			"</td></tr>\n",
			get_charset_str(m_charset),
			(int32_t)m_isAdult,
			(int32_t)m_isPermalink,
			(int32_t)m_isRSS,
			ipString,
			cr->m_coll,
			ipString,
			size_utf8Content - 1,
			(int32_t)m_isContentTruncated,
			g_contentTypeStrings[(int)m_contentType] ,
			strLanguage,
			g_countryCode.getName(m_countryId) ,
			sni,
			::getSiteRank(sni),
			info1->getNumGoodInlinks(),

			tmp3
			);
	else {
		sb->safePrintf (
			"\t<charset><![CDATA[%s]]></charset>\n"
			"\t<isAdult>%" PRId32"</isAdult>\n"
			"\t<isLinkSpam>%" PRId32"</isLinkSpam>\n"
			"\t<siteRank>%" PRId32"</siteRank>\n"

			"\t<numGoodSiteInlinks>%" PRId32"</numGoodSiteInlinks>\n"

			"\t<numGoodPageInlinks>%" PRId32"</numGoodPageInlinks>\n"
			"\t<pageInlinksLastComputed>%" PRId32
			"</pageInlinksLastComputed>\n"

			,get_charset_str(m_charset)
			,(int32_t)m_isAdult
			,(int32_t)m_isLinkSpam
			,::getSiteRank(sni)
			,sni

			,info1->getNumGoodInlinks()
			,(int32_t)info1->m_lastUpdated
			);
		sb->safePrintf("\t<isPermalink>%" PRId32"</isPermalink>\n"
			       "\t<isRSSFeed>%" PRId32"</isRSSFeed>\n"
			       "\t<ipAddress><![CDATA[%s]]></ipAddress>\n"
			       "\t<contentLenInBytes>%" PRId32
			       "</contentLenInBytes>\n"
			       "\t<isContentTruncated>%" PRId32
			       "</isContentTruncated>\n"
			       "\t<contentType><![CDATA[%s]]></contentType>\n"
			       "\t<language><![CDATA[%s]]></language>\n"
			       "\t<country><![CDATA[%s]]></country>\n",
			       (int32_t)m_isPermalink,
			       (int32_t)m_isRSS,
			       ipString,
			       size_utf8Content - 1,
			       (int32_t)m_isContentTruncated,
			       g_contentTypeStrings[(int)m_contentType] ,
			       strLanguage,
			       g_countryCode.getName(m_countryId) );
	}

	TagRec *ogr = NULL;
	if ( m_tagRecDataValid ) {
		ogr = getTagRec(); // &m_tagRec;
		// sanity. should be set from titlerec, so no blocking!
		if ( ! ogr || ogr == (void *)-1 ) { g_process.shutdownAbort(true); }
	}
	if ( ogr && ! isXml ) ogr->printToBufAsHtml ( sb , "tag" );
	else if ( ogr )       ogr->printToBufAsXml  ( sb  );

	// show the good inlinks we used when indexing this
	if ( ! isXml )
		info1->print(sb,cr->m_coll);

	// close the table
	if ( ! isXml )
		sb->safePrintf ( "</table></center><br>\n" );
	else
		sb->safePrintf("</response>\n");

	return true;
}

bool XmlDoc::printSiteInlinks ( SafeBuf *sb , HttpRequest *hr ) {

	// use msg25 to hit linkdb and give us a link info class i guess
	// but we need paging functionality so we can page through like
	// 100 links at a time. clustered by c-class ip.

	// do we need to mention how many from each ip c-class then? because
	// then we'd have to read the whole termlist, might be several
	// separate disk reads.

	// we need to re-get both if either is NULL
	LinkInfo *sinfo = getSiteLinkInfo();
	// block or error?
	if ( ! sinfo ) return true;
	if ( sinfo == (LinkInfo *)-1) return false;

	int32_t isXml = hr->getLong("xml",0);

	if ( ! isXml ) printMenu ( sb );

	if ( isXml )
		sb->safePrintf ("<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				);


	sb->safeMemcpy ( &m_siteLinkBuf );

	if ( isXml )
		sb->safePrintf ("</response>\n"	);

	// just print that
	//sinfo->print ( sb , cr->m_coll );

	return true;
}

bool XmlDoc::printPageInlinks ( SafeBuf *sb , HttpRequest *hr ) {

	// we need to re-get both if either is NULL
	LinkInfo *info1 = getLinkInfo1();
	// block or error?
	if ( ! info1 ) return true;
	if ( info1 == (LinkInfo *)-1) return false;

	int32_t isXml = hr->getLong("xml",0);

	if ( ! isXml ) printMenu ( sb );

	if ( isXml )
		sb->safePrintf ("<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				);

	int32_t recompute = hr->getLong("recompute",0);

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;

	// i guess we need this
	if ( ! recompute ) // m_setFromTitleRec )
		info1->print ( sb , cr->m_coll );
	else
		sb->safeMemcpy ( &m_pageLinkBuf );

	if ( isXml )
		sb->safePrintf ("</response>\n"	);

	return true;
}

bool XmlDoc::printRainbowSections ( SafeBuf *sb , HttpRequest *hr ) {

	// what wordposition to scroll to and blink?
	int32_t hiPos = -1;
	if ( hr ) hiPos = hr->getLong("hipos",-1);

	//
	// PRINT SECTIONS
	//
	Sections *sections = getSections();
	if ( ! sections) return true;
	if (sections==(Sections *)-1)return false;

	Words *words = getWords();
	if ( ! words ) return true;
	if ( words == (Words *)-1 ) return false;

	Phrases *phrases = getPhrases();
	if ( ! phrases ) return true;
	if (phrases == (void *)-1 ) return false;

	HashTableX *cnt = getCountTable();
	if ( ! cnt ) return true;
	if ( cnt == (void *)-1 ) return false;


	int32_t nw = words->getNumWords();
	int64_t *wids = words->getWordIds();

	int32_t isXml = false;
	if ( hr ) isXml = (bool)hr->getLong("xml",0);

	// now complement, cuz bigger is better in the ranking world
	SafeBuf densBuf;

	// returns false and sets g_errno on error
	if ( ! getDensityRanks((int64_t *)wids,
			       nw,
			       HASHGROUP_BODY,//hi->m_hashGroup,
			       &densBuf,
			       sections))
		return true;
	// a handy ptr
	char *densityVec = (char *)densBuf.getBufStart();
	char *wordSpamVec = getWordSpamVec();
	char *fragVec = m_fragBuf.getBufStart();

	SafeBuf wpos;
	if ( ! getWordPosVec ( words ,
			       sections,
			       // we save this in the titlerec, when we
			       // start hashing the body. we have the url
			       // terms before the body, so this is necessary.
			       m_bodyStartPos,
			       fragVec,
			       &wpos) ) return true;
	// a handy ptr
	int32_t *wposVec = (int32_t *)wpos.getBufStart();

	if ( ! isXml ) {
		// put url in for steve to parse out
		sb->safePrintf("%s\n",
			       m_firstUrl.getUrl());
		sb->safePrintf("<font color=black>w</font>"
			       "/"
			       "<font color=purple>x</font>"
			       //"/"
			       //"<font color=green>y</font>"
			       "/"
			       "<font color=red>z</font>"
			       ": "
			       "w=wordPosition "
			       "x=densityRank "
			       //"y=diversityRank "
			       "z=wordSpamRank "
			       "<br>"
			       "<br>"
			       ""
			       );
	}

	if ( ! isXml ) {
		// try the new print function
		sections->print( sb, hiPos, wposVec, densityVec, wordSpamVec, fragVec );
		return true;
	}

	// at this point, xml only
	sb->safePrintf ("<?xml version=\"1.0\" "
	                "encoding=\"UTF-8\" ?>\n"
	                "<response>\n"
	                );

	Section *si = sections->m_rootSection;

	sec_t mflags = SEC_SENTENCE | SEC_MENU;

	for ( ; si ; si = si->m_next ) {
		// print it out
		sb->safePrintf("\t<section>\n");
		// get our offset in the array of sections
		int32_t num = si - sections->m_sections;
		sb->safePrintf("\t\t<id>%" PRId32"</id>\n",num);
		Section *parent = si->m_parent;
		if ( parent ) {
			int32_t pnum = parent - sections->m_sections;
			sb->safePrintf("\t\t<parent>%" PRId32"</parent>\n",pnum);
		}
		const char *byte1 = words->getWord(si->m_a);
		const char *byte2 = words->getWord(si->m_b-1) +
				    words->getWordLen(si->m_b-1);
		int32_t off1 = byte1 - words->getWord(0);
		int32_t size = byte2 - byte1;
		sb->safePrintf("\t\t<byteOffset>%" PRId32"</byteOffset>\n",off1);
		sb->safePrintf("\t\t<numBytes>%" PRId32"</numBytes>\n",size);
		if ( si->m_flags & mflags ) {
			sb->safePrintf("\t\t<flags><![CDATA[");
			bool printed = false;
			if ( si->m_flags & SEC_SENTENCE ) {
				sb->safePrintf("sentence");
				printed = true;
			}
			if ( si->m_flags & SEC_MENU ) {
				if ( printed ) sb->pushChar(' ');
				sb->safePrintf("ismenu");
				printed = true;
			}
			sb->safePrintf("]]></flags>\n");
		}
		int32_t bcolor = (int32_t)si->m_colorHash& 0x00ffffff;
		int32_t fcolor = 0x000000;
		//int32_t rcolor = 0x000000;
		uint8_t *bp = (uint8_t *)&bcolor;
		bool dark = false;
		if ( bp[0]<128 && bp[1]<128 && bp[2]<128 )
			dark = true;
		// or if two are less than 50
		if ( bp[0]<100 && bp[1]<100 ) dark = true;
		if ( bp[1]<100 && bp[2]<100 ) dark = true;
		if ( bp[0]<100 && bp[2]<100 ) dark = true;
		// if bg color is dark, make font color light
		if ( dark ) {
			fcolor = 0x00ffffff;
			//rcolor = 0x00ffffff;
		}
		sb->safePrintf("\t\t<bgColor>%06" PRIx32"</bgColor>\n",bcolor);
		sb->safePrintf("\t\t<textColor>%06" PRIx32"</textColor>\n",fcolor);
		sb->safePrintf("\t</section>\n");
	}

	// now print out the entire page content so the offsets make sense!
	sb->safePrintf("\t<utf8Content><![CDATA[");
	if ( ptr_utf8Content )
		sb->htmlEncode ( ptr_utf8Content ,size_utf8Content-1,false);
	sb->safePrintf("]]></utf8Content>\n");

	// end xml response
	sb->safePrintf("</response>\n");

	return true;
}

bool XmlDoc::printTermList ( SafeBuf *sb , HttpRequest *hr ) {

	// set debug buffer
	m_storeTermListInfo = true;

	// default to sorting by wordpos
	m_sortTermListBy = hr->getLong("sortby",1);

	// cores in getNewSpiderReply() if we do not have this and provide
	// the docid...
	m_useSpiderdb = false;

	char *metaList = getMetaList ( );
	if ( ! metaList ) return true;
	if (metaList==(char *) -1) return false;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;


	int32_t isXml = hr->getLong("xml",0);

	if ( isXml ) {
		sb->safePrintf ("<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				);
		sb->safePrintf(
			       "\t<maxDens>%" PRId32"</maxDens>\n"
			       //"\t<maxDiv>%" PRId32"</maxDiv>\n"
			       "\t<maxSpam>%" PRId32"</maxSpam>\n"
			       , (int32_t)MAXDENSITYRANK
			       //, (int32_t)MAXDIVERSITYRANK
			       , (int32_t)MAXWORDSPAMRANK
			       );
	}

	if ( ! m_langIdValid ) { g_process.shutdownAbort(true); }

	if ( ! isXml ) {
		//printMenu ( sb );
		//sb->safePrintf("<i>* indicates word is a synonym or "
		//	       "alternative word form<br><br>");
		sb->safePrintf("N column = DensityRank (0-%" PRId32")<br>"
			       //"V column = DiversityRank (0-%" PRId32")<br>"
			       "S column = WordSpamRank  (0-%" PRId32") "
			       "[or linker "
			       "siterank if its offsite link text]<br>"

			       "Lang column = language used for purposes "
			       "of detecting the document's primary language "
			       "using a simple majority vote"
			       "<br>"

			       "</i>"
			       "<br>"
			       "Document Primary Language: <b>%s</b> (%s)"
			       "<br>"
			       "<br>"
			       , (int32_t)MAXDENSITYRANK
			       //, (int32_t)MAXDIVERSITYRANK
			       , (int32_t)MAXWORDSPAMRANK
			       , getLanguageString (m_langId)
			       , getLanguageAbbr(m_langId)
			       );
		// encode it
		SafeBuf ue;
		ue.urlEncode ( ptr_firstUrl );

		sb->safePrintf("Sort by: " );
		if ( m_sortTermListBy == 0 )
			sb->safePrintf("<b>Term</b>");
		else
			sb->safePrintf("<a href=/print?c=%s&page=5&u=%s&"
				       "sortby=0>"
				       "Term</a>"
				       , cr->m_coll
				       , ue.getBufStart()
				       );
		sb->safePrintf(" | ");
		if ( m_sortTermListBy == 1 )
			sb->safePrintf("<b>WordPos</b>");
		else
			sb->safePrintf("<a href=/print?c=%s&page=5&u=%s&"
				       "sortby=1>"
				       "WordPos</a>"
				       , cr->m_coll
				       , ue.getBufStart()
				       );
		sb->safePrintf("<br>"
			       "<br>"
			       );
	}


	//
	// BEGIN PRINT HASHES TERMS (JUST POSDB)
	//

	// shortcut
	HashTableX *wt = m_wts;

	// use the keys to hold our list of ptrs to TermDebugInfos for sorting!
	TermDebugInfo **tp = NULL;
	// add them with this counter
	int32_t nt = 0;

	int32_t nwt = 0;
	if ( wt ) {
		nwt = wt->m_numSlots;
		tp = (TermDebugInfo **)wt->m_keys;
	}

	// now print the table we stored all we hashed into
	for ( int32_t i = 0 ; i < nwt ; i++ ) {
		// skip if empty
		if ( wt->m_flags[i] == 0 ) continue;
		// get its key, date=32bits termid=64bits
		//key96_t *k = (key96_t *)wt->getKey ( i );
		// get the TermDebugInfo
		TermDebugInfo *ti = (TermDebugInfo *)wt->getValueFromSlot ( i );
		// point to it for sorting
		tp[nt++] = ti;
	}

	// set this for cmptp
	s_wbuf = &m_wbuf;

	if ( m_sortTermListBy == 0 )
		// sort them alphabetically
		gbsort ( tp , nt , sizeof(TermDebugInfo *), cmptp );
	else
		// sort by word pos
		gbsort ( tp , nt , sizeof(TermDebugInfo *), cmptp2 );


	// print the weight tables
	//printLocationWeightsTable(sb,isXml);
	//printDiversityWeightsTable(sb,isXml);
	//printDensityWeightsTable(sb,isXml);
	//printWordSpamWeightsTable(sb,isXml);


	// print them out in a table
	char hdr[1000];
	sprintf(hdr,
		"<table border=1 cellpadding=0>"
		"<tr>"

		"<td><b>Term ID</b></td>"

		"<td><b>Prefix</b></td>"
		"<td><b>WordPos</b></td>"
		"<td><b>Lang</b></td>"

		"<td><b>Term</b></td>"

		//"%s"

		//"<td><b>Weight</b></td>"
		//"<td><b>Spam</b></td>"

		"<td><b>Desc</b></td>"

		"<td><b>N</b></td>"
		//"<td><b>V</b></td>" // diversityRank
		"<td><b>S</b></td>"
		"<td><b>Score</b></td>"

		//"<td><b>Date</b></td>"
		//"<td><b>Desc</b></td>"
		//"<td><b>TermId</b></td>"
		"</tr>\n"
		//,fbuf
		);

	if ( ! isXml )
		sb->safePrintf("%s",hdr);

	char *start = m_wbuf.getBufStart();
	int32_t rcount = 0;

	for ( int32_t i = 0 ; i < nt ; i++ ) {

		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS) == 0 && ! isXml )
			sb->safePrintf("<!--ignore--></table>%s",hdr);

		char *prefix = NULL;//"&nbsp;";
		if ( tp[i]->m_prefixOff >= 0 )
			prefix = start + tp[i]->m_prefixOff;

		if ( isXml ) sb->safePrintf("\t<term>\n");

		if ( isXml && prefix )
			sb->safePrintf("\t\t<prefix><![CDATA[%s]]>"
				       "</prefix>\n",prefix);

		if ( ! isXml )
		{
			sb->safePrintf ( "<tr>");
		}


		if ( ! isXml )
		{
			// Show termId in decimal, masked as it would be stored in posdb
			sb->safePrintf("<td align=\"right\">%" PRId64"</td>", (int64_t)(tp[i]->m_termId & TERMID_MASK));
		}


		if( ! isXml )
		{
			if ( prefix )
				sb->safePrintf("<td>%s:</td>",prefix);
			else
				sb->safePrintf("<td>&nbsp;</td>");
		}

		if ( ! isXml )
		{
			sb->safePrintf("<td>%" PRId32
				       "/%" PRId32
				       "</td>" ,
				       tp[i]->m_wordPos
				       ,tp[i]->m_wordNum
				       );
		}

		//char *abbr = getLanguageAbbr(tp[i]->m_langId);
		//if ( tp[i]->m_langId == langTranslingual ) abbr ="??";
		//if ( tp[i]->m_langId == langUnknown      ) abbr ="--";
		//if ( tp[i]->m_synSrc ) abbr = "";


		// print out all langs word is in if it's not clear
		// what language it is. we use a sliding window to
		// resolve some ambiguity, but not all, so print out
		// the possible langs here
		if ( ! isXml ) {
			sb->safePrintf("<td>");
			printLangBits ( sb , tp[i] );
			sb->safePrintf("</td>");
		}


		//if ( ! isXml && abbr[0] )
		//	sb->safePrintf("<td>%s</td>", abbr );
		//else if ( ! isXml )
		//	sb->safePrintf("<td>&nbsp;</td>" );
		//else if ( abbr[0] )
		//	sb->safePrintf("\t\t<lang><![CDATA["
		//		       "]]>%s</lang>\n", abbr );


		if ( isXml )
			sb->safePrintf("\t\t<s><![CDATA[");

		if ( ! isXml )
			sb->safePrintf ("<td><nobr>" );

		//if ( tp[i]->m_synSrc )
		//	sb->pushChar('*');

		sb->safeMemcpy_nospaces ( start + tp[i]->m_termOff ,
					  tp[i]->m_termLen );

		/*
		char *dateStr = "&nbsp;";
		int32_t ddd = tp[i]->m_date;
		uint8_t *tddd = (uint8_t *)&ddd;
		char tbbb[32];
		if ( ddd && tddd[2] == 0 && tddd[3] == 0 &&
		     tddd[0] && tddd[1] && tddd[1] <= tddd[0] ) {
			sprintf(tbbb,"evIds %" PRId32"-%" PRId32,
				(int32_t)tddd[1],(int32_t)tddd[0]);
			dateStr = tbbb;
		}
		else if ( ddd )
			dateStr = asctime_r(gmtime_r(&ddd ));

		char tmp[20];
		if ( tp[i]->m_noSplit ) sprintf ( tmp,"<b>1</b>" );
		else                    sprintf ( tmp,"0" );
		*/

		if ( isXml )
			sb->safePrintf("]]></s>\n");
		else
			sb->safePrintf ( "</nobr></td>" );


		if ( isXml )
			sb->safePrintf("\t\t<wordPos>%" PRId32"</wordPos>\n",
				       tp[i]->m_wordPos);

		const char *desc = NULL;
		if ( tp[i]->m_descOff >= 0 )
			desc = start + tp[i]->m_descOff;

		// use hashgroup
		int32_t hg = tp[i]->m_hashGroup;
		if ( ! desc || ! strcmp(desc,"body") )
			desc = getHashGroupString(hg);

		if ( isXml && desc )
			sb->safePrintf("\t\t<loc>%s</loc>\n", desc);
		else if ( ! isXml ) {
			if ( ! desc ) desc = "&nbsp;";
			sb->safePrintf ( "<td>%s", desc );
			char ss = tp[i]->m_synSrc;
			if ( ss )
				sb->safePrintf(" - %s",
					       getSourceString(ss));
			sb->safePrintf("</td>");
		}

		int32_t dn = (int32_t)tp[i]->m_densityRank;
		if ( isXml )
			sb->safePrintf("\t\t<dens>%" PRId32"</dens>\n",dn);

		if ( ! isXml && dn >= MAXDENSITYRANK )
			sb->safePrintf("<td>%" PRId32"</td>\n",dn);
		else if ( ! isXml )
			sb->safePrintf("<td><font color=purple>%" PRId32"</font>"
				       "</td>",dn);

		// the diversityrank/wordspamrank
		/*
		int32_t ds = (int32_t)tp[i]->m_diversityRank;
		if ( isXml )
			sb->safePrintf("\t\t<div>%" PRId32"</div>\n",ds);
		if ( ! isXml && ds >= MAXDIVERSITYRANK )
			sb->safePrintf("<td>%" PRId32"</td>\n",ds);
		else if ( ! isXml )
			sb->safePrintf("<td><font color=green>%" PRId32"</font>"
				       "</td>",ds);
		*/

		int32_t ws = (int32_t)tp[i]->m_wordSpamRank;

		if ( isXml && hg == HASHGROUP_INLINKTEXT )
			sb->safePrintf("\t\t<linkerSiteRank>%" PRId32
				       "</linkerSiteRank>\n",ws);
		else if ( isXml )
			sb->safePrintf("\t\t<spam>%" PRId32"</spam>\n",ws);

		if ( ! isXml && ws >= MAXWORDSPAMRANK )
			sb->safePrintf("<td>%" PRId32"</td>",ws);
		else if ( ! isXml )
			sb->safePrintf("<td><font color=red>%" PRId32"</font></td>",
				       ws);

		float score = 1.0;
		// square this like we do in the query ranking algo
		score *= getHashGroupWeight(hg) * getHashGroupWeight(hg);
		score *= getDensityWeight(tp[i]->m_densityRank);
		if ( tp[i]->m_synSrc ) score *= g_conf.m_synonymWeight;
		if ( hg == HASHGROUP_INLINKTEXT ) score *= getLinkerWeight(ws);
		else                           score *= getWordSpamWeight(ws);
		if ( isXml )
			sb->safePrintf("\t\t<score>%.02f</score>\n",score);
		else
			sb->safePrintf("<td>%.02f</td>\n",score);

		if ( isXml )
			sb->safePrintf("\t</term>\n");
		else
			sb->safePrintf("</tr>\n");
	}


	if ( isXml )
		sb->safePrintf ("</response>\n"	);
	else
		sb->safePrintf("</table><br>\n");

	//
	// END PRINT HASHES TERMS
	//

	return true;
}

bool XmlDoc::printSpiderStats ( SafeBuf *sb , HttpRequest *hr ) {

	int32_t isXml = hr->getLong("xml",0);

	if ( ! isXml ) printMenu ( sb );

	sb->safePrintf("<b>Coming Soon</b>");

	return true;
}

bool XmlDoc::printCachedPage ( SafeBuf *sb , HttpRequest *hr ) {

	char **c = getUtf8Content();
	if ( ! c ) return true;
	if ( c==(void *)-1) return false;

	int32_t isXml = hr->getLong("xml",0);

	if ( ! isXml ) {
		printMenu ( sb );
		// just copy it otherwise
		if ( ptr_utf8Content )
			sb->safeMemcpy ( ptr_utf8Content ,size_utf8Content -1);
		return true;
	}

	sb->safePrintf ("<?xml version=\"1.0\" "
			"encoding=\"UTF-8\" ?>\n"
			"<response>\n"
			);
	sb->safePrintf("\t<utf8Content><![CDATA[");
	if ( ptr_utf8Content )
		sb->htmlEncode ( ptr_utf8Content ,size_utf8Content-1,
				 false);
	sb->safePrintf("]]></utf8Content>\n");
	// end xml response
	sb->safePrintf("</response>\n");
	return true;
}


// . get the possible titles of the root page
// . includes the title tag text
// . includes various inlink text
// . used to match the VERIFIED place name 1 or 2 of addresses on this
//   site in order to set Address::m_flags's AF_VENUE_DEFAULT bit which
//   indicates the address is the address of the website (a venue website)
char *XmlDoc::getRootTitleBuf ( ) {

	// return if valid
	if ( m_rootTitleBufValid ) return m_rootTitleBuf;

	// get it from the tag rec first
	setStatus ( "getting root title buf");

	// get it from the tag rec if we can
	TagRec *gr = getTagRec ();
	if ( ! gr || gr == (void *)-1 ) return (char *)(void*)gr;

	// PROBLEM: new title rec is the only thing which has sitetitles tag
	// sometimes and we do not store that in the title rec. in this case
	// we should maybe store ptr_siteTitleBuf/size_siteTitleBuf in the
	// title rec?
	Tag *tag = gr->getTag("roottitles");

	char *src     = NULL;
	int32_t  srcSize = 0;

	if ( ptr_rootTitleBuf || m_setFromTitleRec ) {
		src    =  ptr_rootTitleBuf;
		srcSize = size_rootTitleBuf;
	}
	else if ( tag ) {
		src     = tag->getTagData();
		srcSize = tag->getTagDataSize();
		// no need to add to title rec since already in the tag so
		// make sure we did not double add
		if ( ptr_rootTitleBuf ) { g_process.shutdownAbort(true); }
	}
	else {
		// . get the root doc
 		// . allow for a one hour cache of the titleRec
		XmlDoc **prd = getRootXmlDoc( 3600 );
		if ( ! prd || prd == (void *)-1 ) return (char*)(void*)prd;
		// shortcut
		XmlDoc *rd = *prd;
		// . if no root doc, then assume no root title
		// . this happens if we are injecting because we do not want
		//   to download the root page for speed purposes
		if ( ! rd ) {
			m_rootTitleBuf[0] = '\0';
			m_rootTitleBufSize = 0;
			m_rootTitleBufValid = true;
			return m_rootTitleBuf;
		}

		// a \0 separated list
		char *rtl = rd->getTitleBuf();
		if ( ! rtl || rtl == (void *)-1 ) return rtl;

		// ptr
		src     = rd->m_titleBuf;
		srcSize = rd->m_titleBufSize;
	}

	int32_t max = (int32_t)ROOT_TITLE_BUF_MAX - 5;
	// sanity
	if ( srcSize >= max ) {
		// truncate
		srcSize = max;
		// back up so we split on a space
		for ( ; srcSize>0 && ! is_wspace_a(src[srcSize]); srcSize--);
		// null term
		src[srcSize] = '\0';
		// include it
		srcSize++;
	}

	// copy that over in case root is destroyed
	gbmemcpy ( m_rootTitleBuf , src , srcSize );
	m_rootTitleBufSize = srcSize;

	// sanity check, must include the null ni the size
	if ( m_rootTitleBufSize > 0 &&
	     m_rootTitleBuf [ m_rootTitleBufSize - 1 ] ) {
		log("build: bad root titlebuf size not end in null char for "
		    "collnum=%i",(int)m_collnum);
		ptr_rootTitleBuf = NULL;
		size_rootTitleBuf = 0;
		m_rootTitleBufValid = true;
		return m_rootTitleBuf;
	}

	// sanity check - breach check
	if ( m_rootTitleBufSize > ROOT_TITLE_BUF_MAX ) { g_process.shutdownAbort(true);}

	// serialize into our titlerec
	ptr_rootTitleBuf  = m_rootTitleBuf;
	size_rootTitleBuf = m_rootTitleBufSize;

	m_rootTitleBufValid = true;

	return m_rootTitleBuf;
}


char *XmlDoc::getFilteredRootTitleBuf ( ) {

	if ( m_filteredRootTitleBufValid )
		return m_filteredRootTitleBuf;

	// get unfiltered. m_rootTitleBuf should be set from this call.
	char *rtbp = getRootTitleBuf();
	if ( ! rtbp || rtbp == (void *)-1 ) return rtbp;

	// filter all the punct to \0 so that something like
	// "walmart.com : live better" is reduced to 3 potential
	// names, "walmart", "com" and "live better"
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(m_rootTitleBuf,m_rootTitleBufSize);
#endif
	char *src    =       m_rootTitleBuf;
	char *srcEnd = src + m_rootTitleBufSize;
	char *dst    =       m_filteredRootTitleBuf;

	// save some room to add a \0, so subtract 5
	char *dstEnd = dst + ROOT_TITLE_BUF_MAX - 5;

	int32_t  size = 0;
	bool lastWasPunct = true;
	for ( ; src < srcEnd && dst < dstEnd ; src += size ) {
		// set the char size
		size = getUtf8CharSize(src);
		// space?
		if ( is_wspace_a (*src) ||
		     // allow periods too
		     *src=='.' ) {
			// no back to back punct
			if ( lastWasPunct ) continue;
			// flag it
			lastWasPunct = true;
			// add it in
			*dst++ = '.';
			// that's it
			continue;
		}
		// x'y  or x-y
		if ( ( *src == '\'' ||
		       *src == '.'  ||
		       *src == '-'  ) &&
		     ! lastWasPunct &&
		     is_alnum_a(src[1]) ) {
			// add it in
			*dst++ = *src;
			// that's it
			continue;
		}
		// x & y is ok
		if ( *src == '&' ) {
			// assume not punct (stands for and)
			lastWasPunct = false;
			// add it in
			*dst++ = *src;
			// that's it
			continue;
		}
		// store alnums right in
		if ( is_alnum_a(*src) ) {
			// flag it
			lastWasPunct = false;
			// copy it over
			gbmemcpy ( dst , src , size );
			// skip what we copied
			dst += size;
			continue;
		}
		// if punct and haven't stored anything, just skip it
		if ( lastWasPunct ) dst[-1] = '\0';
		// store it
		else *dst++ = '\0';
	}
	// make sure we end on a \0
	if ( dst > m_filteredRootTitleBuf && dst[-1] != '\0' )
		*dst++ = '\0';

	// shortcut
	char *str     = m_filteredRootTitleBuf;
	int32_t  strSize = dst - m_filteredRootTitleBuf;

	// copy that over in case root is destroyed
	gbmemcpy ( m_filteredRootTitleBuf , str , strSize );
	m_filteredRootTitleBufSize = strSize;

	// sanity check, must include the null ni the size
	if ( m_filteredRootTitleBufSize > 0 &&
	     m_filteredRootTitleBuf [ m_filteredRootTitleBufSize - 1 ] ) {
		g_process.shutdownAbort(true);
	}

	// sanity check - breach check
	if ( m_filteredRootTitleBufSize > ROOT_TITLE_BUF_MAX ) {
		g_process.shutdownAbort(true);}

	m_filteredRootTitleBufValid = true;

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(m_filteredRootTitleBuf,m_filteredRootTitleBufSize);
#endif

	return m_filteredRootTitleBuf;
}

//static bool s_dummyBool = 1;

class Binky {
public:
	char      *m_text;
	int32_t       m_textLen;
	int32_t       m_score;
	int64_t  m_hash;
};


// static int cmpbk ( const void *v1, const void *v2 ) {
// 	Binky *b1 = (Binky *)v1;
// 	Binky *b2 = (Binky *)v2;
// 	return b1->m_score - b2->m_score;
// }

char *XmlDoc::getTitleBuf ( ) {
	if ( m_titleBufValid ) return m_titleBuf;

	// recalc this everytime the root page is indexed
	setStatus ( "getting title buf on root");

	// are we a root?
	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (char*)isRoot;
	// this should only be called on the root!
	// . if the site changed for us, but the title rec of what we
	//   think is now the root thinks that it is not the root because
	//   it is using the old site, then it cores here!
	// . i.e. if the new root is www.xyz.com/user/ted/ and the old root
	//   is www.xyz.com then and the old root is stored in ptr_site for
	//   the title rec for www.xyz.com/user/ted/ then we core here,
	// . so take this sanity check out
	// . but if the title rec does not think he is the site root yet
	//   then just wait until he does so we can get his
	//   ptr_rootTitleBuf below
	if ( ! *isRoot ) {
		m_titleBuf[0] = '\0';
		m_titleBufSize = 0;
		m_titleBufValid = true;
		return m_titleBuf;
	}

	// sanity check
	if ( m_setFromTitleRec ) {
		gbmemcpy(m_titleBuf, ptr_rootTitleBuf, size_rootTitleBuf );
		m_titleBufSize  = size_rootTitleBuf;
		m_titleBufValid = true;
		return m_titleBuf;
	}

	char *mysite = getSite();
	if ( ! mysite || mysite == (char *)-1 ) return mysite;
	// get link info first
	LinkInfo   *info1  = getLinkInfo1();
	// error or blocked
	if ( ! info1 || info1 == (LinkInfo *)-1 ) return (char*)(void*)info1;

	// sanity check
	Xml *xml = getXml();
	// return -1 if it blocked
	if ( xml == (void *)-1 ) return (char*)-1;
	// set up for title
	int32_t tlen ;
	char *title ;
	// on error, ignore it to avoid hammering the root!
	if ( xml == (void *)NULL ) {
		// log it
		log("build: error downloading root xml: %s",
		    mstrerror(g_errno));
		// clear it
		g_errno = 0;
		// make it 0
		tlen  = 0;
		title = NULL;
	}
	else {
		// get the title
		title = m_xml.getTextForXmlTag ( 0,
						 999999 ,
						 "title" ,
						 &tlen ,
						 true ); // skip leading spaces
	}

	// truncate to 100 chars
	//for ( ; tlen>0 && (tlen > 100 || is_alnum_a(title[tlen])) ; tlen-- )
	//	if ( tlen == 0 ) break;
	if ( tlen > 100 ) {
		char *tpend = title + 100;
		char *prev  = getPrevUtf8Char ( tpend , title );
		// make that the end so we don't split a utf8 char
		tlen = prev - title;
	}

	// store tag in here
	char tmp[1024];
	// point to it
	char *ptmp = tmp;
	// set this
	char *pend = tmp + 1024;
	// add that in
	gbmemcpy ( ptmp, title, tlen); ptmp += tlen;
	// null terminate it
	*ptmp++ = '\0';

	// two votes per internal inlink
	int32_t internalCount = 0;
	// count inlinkers
	int32_t linkNum = 0;
	Binky bk[1000];
	// init this
	//char stbuf[2000];
	//HashTableX scoreTable;
	//scoreTable.set(8,4,64,stbuf,2000,false,m_niceness,"xmlscores");
	// scan each link in the link info
	for ( Inlink *k = NULL; (k = info1->getNextInlink(k)) ; ) {
		// do not breach
		if ( linkNum >= 1000 ) break;
		// is this inlinker internal?
		bool internal=((m_ip&0x0000ffff)==(k->m_ip&0x0000ffff));
		// get length of link text
		int32_t tlen = k->size_linkText;
		if ( tlen > 0 ) tlen--;
		// get the text
		char *txt = k->getLinkText();
		// skip corrupted
		if ( ! verifyUtf8 ( txt , tlen ) ) {
			log("xmldoc: bad link text 4 from url=%s for %s",
			    k->getUrl(),m_firstUrl.getUrl());
			continue;
		}
		// store these
		// zero out hash
		bk[linkNum].m_hash    = 0;
		bk[linkNum].m_text    = txt;
		bk[linkNum].m_textLen = tlen;
		bk[linkNum].m_score   = 0;
		// internal count
		if ( internal && ++internalCount >= 3 ) continue;
		// it's good
		bk[linkNum].m_score = 1;
		linkNum++;
	}
	// init this
	char dtbuf[1000];
	HashTableX dupTable;
	dupTable.set(8,0,64,dtbuf,1000,false,"xmldup");
	// now set the scores and isdup
	for ( int32_t i = 0 ; i < linkNum ; i++ ) {
		// skip if ignored
		if ( bk[i].m_score == 0 ) continue;
		// get hash
		int64_t h = bk[i].m_hash;
		// assume a dup
		bk[i].m_score = 0;
		// skip if zero'ed out
		if ( ! h ) continue;
		// only do each hash once!
		if ( dupTable.isInTable(&h) ) continue;
		// add to it. return NULL with g_errno set on error
		if ( ! dupTable.addKey(&h) ) return NULL;
		// is it in there?
		bk[i].m_score = 1; // scoreTable.getScore ( &h );
	}
	// now sort the bk array by m_score
	//gbsort ( bk , linkNum , sizeof(Binky), cmpbk );

	// sanity check - make sure sorted right
	//if ( linkNum >= 2 && bk[0].m_score < bk[1].m_score ) {
	//	g_process.shutdownAbort(true); }

	// . now add the winners to the buffer
	// . skip if score is 0
	for ( int32_t i = 0 ; i < linkNum ; i++ ) {
		// skip if score is zero
		if ( bk[i].m_score == 0 ) continue;
		// skip if too big
		if ( bk[i].m_textLen + 1 > pend - ptmp ) continue;
		// store it
		gbmemcpy ( ptmp , bk[i].m_text , bk[i].m_textLen );
		// advance
		ptmp += bk[i].m_textLen;
		// null terminate it
		*ptmp++ = '\0';
	}

	// sanity
	int32_t size = ptmp - tmp;
	if ( size > ROOT_TITLE_BUF_MAX ) { g_process.shutdownAbort(true); }

	gbmemcpy ( m_titleBuf , tmp , ptmp - tmp );
	m_titleBufSize = size;
	m_titleBufValid = true;
	// ensure null terminated
	if ( size > 0 && m_titleBuf[size-1] ) { g_process.shutdownAbort(true); }
	//ptr_siteTitleBuf = m_siteTitleBuf;
	//size_siteTitleBuf = m_siteTitleBufSize;
	return m_titleBuf;
}


// . now we just get all the tagdb rdb recs to add using this function
// . then we just use the metalist to update tagdb
SafeBuf *XmlDoc::getNewTagBuf ( ) {
	if ( m_newTagBufValid ) return &m_newTagBuf;

	setStatus ( "getting new tags");

	int32_t *ic = getIndexCode();
	if ( ic == (void *)-1 ) { g_process.shutdownAbort(true); }

	// get our ip
	int32_t *ip = getIp();
	// this must not block to avoid re-computing "addme" above
	if ( ip == (void *)-1 ) { g_process.shutdownAbort(true); }
	if ( ! ip || ip == (int32_t *)-1) return (SafeBuf *)ip;

	// . do not both if there is a problem
	// . otherwise if our ip is invalid (0 or 1) we core in
	//   getNumSiteInlinks() which requires a valid ip
	// . if its robots.txt disallowed, then indexCode will be set, but we
	//   still want to cache our sitenuminlinks in tagdb! delicious.com was
	//   recomputing the sitelinkinfo each time because we were not storing
	//   these tags in tagdb!!
	if ( ! *ip || *ip == -1 ) { // *ic ) {
		m_newTagBuf.reset();
		m_newTagBufValid = true;
		return &m_newTagBuf;
	}

	// get the tags already in tagdb
	TagRec *gr = getTagRec ( );
	if ( ! gr || gr == (void *)-1 ) return (SafeBuf *)gr;

	// get our site
	char *mysite = getSite();
	// this must not block to avoid re-computing "addme" above
	if ( mysite == (void *)-1 ) { g_process.shutdownAbort(true); }
	if ( ! mysite || mysite == (char *)-1 ) return (SafeBuf *)mysite;

	// age of tag in seconds
	int32_t timestamp;

	// always just use the primary tagdb so we can cache our sitenuminlinks
	rdbid_t rdbId = RDB_TAGDB;

	// sitenuminlinks special for repair
	if ( m_useSecondaryRdbs &&
	     // and not rebuilding titledb
	     ! m_useTitledb ) {
		m_newTagBuf.reset();
		m_newTagBufValid = true;
		int32_t old1 = gr->getLong("sitenuminlinks",-1,&timestamp);
		if ( old1 == m_siteNumInlinks &&
		     old1 != -1 &&
		     ! m_updatingSiteLinkInfoTags )
			return &m_newTagBuf;
		int32_t now = getTimeGlobal();
		if ( g_conf.m_logDebugLinkInfo )
			log("xmldoc: adding tag site=%s sitenuminlinks=%" PRId32,
			    mysite,m_siteNumInlinks);
		if ( ! m_newTagBuf.addTag2(mysite,"sitenuminlinks",now,
					   "xmldoc",
					   *ip,m_siteNumInlinks,rdbId) )
			return NULL;
		return &m_newTagBuf;
	}

	// if doing consistency check, this buf is for adding to tagdb
	// so just ignore those. we use ptr_tagRecData in getTagRec() function
	// but this is really for updating tagdb.
	if ( m_doingConsistencyCheck ) {
		m_newTagBuf.reset();
		m_newTagBufValid = true;
		return &m_newTagBuf;
	}

	Xml *xml = getXml();
	if ( ! xml || xml == (Xml *)-1 ) return (SafeBuf *)xml;

	Words *ww = getWords();
	if ( ! ww || ww == (Words *)-1 ) return (SafeBuf *)ww;

	char *isIndexed = getIsIndexed();
	if ( !isIndexed || isIndexed==(char *)-1 ) return (SafeBuf *)isIndexed;

	char *isRoot = getIsSiteRoot();
	if ( ! isRoot || isRoot == (char *)-1 ) return (SafeBuf *)isRoot;

	int32_t *siteNumInlinks = getSiteNumInlinks();
	if ( ! siteNumInlinks ) return NULL;
	if (   siteNumInlinks == (int32_t *)-1) return (SafeBuf *)-1;

	// ok, get the sites of the external outlinks and they must
	// also be NEW outlinks, added to the page since the last time
	// we spidered it...
	Links *links = getLinks ();
	if ( ! links || links == (Links *)-1 ) return (SafeBuf *)links;

	// our next slated spider priority
	char *spiderLinks = getSpiderLinks();
	if ( ! spiderLinks  || spiderLinks == (char *)-1 )
		return (SafeBuf *)spiderLinks;

	// . get ips of all outlinks.
	// . use m_msgeForIps class just for that
	// . it sucks if the outlink's ip is a dns timeout, then we never
	//   end up being able to store it in tagdb, that is why when
	//   rebuilding we need to skip adding firstip tags for the outlinks
	int32_t **ipv = NULL;
	TagRec ***grv = NULL;
	bool addLinkTags = true;
	if ( ! *spiderLinks ) addLinkTags = false;
	if ( ! m_useSpiderdb ) addLinkTags = false;
	if ( addLinkTags ) {
		ipv = getOutlinkFirstIpVector ();
		if ( ! ipv || ipv == (void *)-1 ) return (SafeBuf *)ipv;
		// . uses m_msgeForTagRecs for this one
		grv = getOutlinkTagRecVector();
		if ( ! grv || grv == (void *)-1 ) return (SafeBuf *)grv;
	}

	//
	// init stuff
	//

	// . this gets the root doc and and parses titles out of it
	// . sets our m_rootTitleBuf/m_rootTitleBufSize
	char *rtbufp = getRootTitleBuf();
	if ( ! rtbufp || rtbufp == (void *)-1) return (SafeBuf*)(void*)rtbufp;

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return NULL;

	// overwrite "getting root title buf" status
	setStatus ("computing new tags");

	if ( g_conf.m_logDebugLinkInfo )
		log("xmldoc: adding tags for mysite=%s",mysite);

	// shortcut
	//TagRec *tr = &m_newTagRec;
	// current time
	int32_t now = getTimeGlobal();

	// store tags into here
	SafeBuf *tbuf = &m_newTagBuf;
	// allocate space to hold the tags we will add
	int32_t need = 512;
	// add in root title buf in case we add it too
	need += m_rootTitleBufSize;
	// reserve it all now
	if ( ! tbuf->reserve(need) ) return NULL;

	//
	// add "site" tag
	//
	const char *oldsite = gr->getString( "site", NULL, NULL, &timestamp );
	if ( ! oldsite || strcmp(oldsite,mysite) || now-timestamp > 10*86400)
		tbuf->addTag3(mysite,"site",now,"xmldoc",*ip,mysite,rdbId);

	//
	// add firstip if not there at all
	//
	const char *oldfip = gr->getString("firstip",NULL);
	// convert it
	int32_t ip3 = 0;
	if ( oldfip ) ip3 = atoip(oldfip);
	// if not there or if bogus, add it!! should override bogus firstips
	if ( ! ip3 || ip3 == -1 ) {
		char *ipstr = iptoa(m_ip);
		tbuf->addTag3(mysite,"firstip",now,"xmldoc",*ip,ipstr,
			     rdbId);
	}

	// sitenuminlinks
	int32_t old1 = gr->getLong("sitenuminlinks",-1,&timestamp);
	if ( old1 == -1 || old1 != m_siteNumInlinks || m_updatingSiteLinkInfoTags ) {
		if ( g_conf.m_logDebugLinkInfo )
			log("xmldoc: adding tag site=%s sitenuminlinks=%" PRId32,
			    mysite,m_siteNumInlinks);
		if ( ! tbuf->addTag2(mysite,"sitenuminlinks",now,"xmldoc",
				    *ip,m_siteNumInlinks,rdbId) )
			return NULL;
	}

	// get root title buf from old tag
	char *data  = NULL;
	int32_t  dsize = 0;
	Tag *rt = gr->getTag("roottitles");
	if ( rt ) {
		data  = rt->getTagData();
		dsize = rt->getTagDataSize();
	}

	bool addRootTitle = false;
	// store the root title buf if we need to. if we had no tag yet...
	if ( ! rt )
		addRootTitle = true;
	// or if differs in size
	else if ( dsize != m_rootTitleBufSize )
		addRootTitle = true;
	// or if differs in content
	else if ( memcmp(data,m_rootTitleBuf,m_rootTitleBufSize))
		addRootTitle =true;
	// or if it is 10 days old or more
	if ( old1!=-1 && now-timestamp > 10*86400 ) addRootTitle = true;
	// but not if injected
	if ( m_wasContentInjected && ! *isRoot ) addRootTitle = false;
	// add it then
	if ( addRootTitle &&
	     ! tbuf->addTag(mysite,"roottitles",now,"xmldoc",
			    *ip,m_rootTitleBuf,m_rootTitleBufSize,
			    rdbId,true) )
		return NULL;


	//
	//
	// NOW add tags for our outlinks
	//
	//

	bool oldHighQualityRoot = true;

	// if we are new, do not add anything, because we only add a tagdb
	// rec entry for "new" outlinks  that were added to the page since
	// the last time we spidered it
	if ( ! *isIndexed ) oldHighQualityRoot = false;

	// no updating if we are not root
	if ( ! *isRoot ) oldHighQualityRoot = false;

	// must be high quality, too
	if ( *siteNumInlinks < 500 ) oldHighQualityRoot = false;

	// only do once per site
	char buf[1000];
	HashTableX ht; ht.set (4,0,-1 , buf , 1000 ,false,"sg-tab");
	// get site of outlink
	SiteGetter siteGetter;
	// . must be from an EXTERNAL DOMAIN and must be new
	// . we should already have its tag rec, if any, since we have msge
	int32_t n = links->getNumLinks();
	// not if not spidering links
	if ( ! addLinkTags ) n = 0;
	// get the flags
	linkflags_t *flags = links->m_linkFlags;
	// scan all outlinks we have on this page
	for ( int32_t i = 0 ; i < n ; i++ ) {

		// get its tag rec
		TagRec *gr = (*grv)[i];

		// does this hostname have a "firstIp" tag?
		const char *ips = gr->getString("firstip",NULL);

		bool skip = false;
		// skip if we are not "old" high quality root
		if ( ! oldHighQualityRoot ) skip = true;
		// . skip if not external domain
		// . we added this above, so just "continue"
		if ( flags[i] & LF_SAMEDOM ) continue;//skip = true;
		// skip links in the old title rec
		if ( flags[i] & LF_OLDLINK ) skip = true;
		// skip if determined to be link spam! should help us
		// with the text ads we hate so much
		if ( links->m_spamNotes[i] ) skip = true;

		// if we should skip, and they have firstip already...
		if ( skip && ips ) continue;

		// get the normalized url
		char *url = links->getLinkPtr(i);
		// get the site. this will not block or have an error.
		siteGetter.getSite(url,gr,timestamp,cr->m_collnum,m_niceness);
		// these are now valid and should reference into
		// Links::m_buf[]
		char *site    = siteGetter.m_site;
		int32_t  siteLen = siteGetter.m_siteLen;

		int32_t linkIp  = (*ipv)[i];

		// get site hash
		uint32_t sh = hash32 ( site , siteLen );
		// ensure site is unique
		if ( ht.getSlot ( &sh ) >= 0 ) continue;
		// add it. returns false and sets g_errno on error
		if ( ! ht.addKey ( &sh ) ) return NULL;

		// . need to add firstip tag for this link's subdomain?
		// . this was in Msge1.cpp but now we do it here
		if ( ! ips && linkIp && linkIp != -1 ) {
			// make it
			char *ips = iptoa(linkIp);
			if (!tbuf->addTag3(site,"firstip",now,"xmldoc",*ip,ips,
					  rdbId))
				return NULL;
		}

		if ( skip ) continue;

		// how much avail for adding tags?
		int32_t avail = tbuf->getAvail();
		// reserve space
		int32_t need = 512;
		// make sure enough
		if ( need > avail && ! tbuf->reserve ( need ) ) return NULL;

		// add tag for this outlink
		// link is linked to by a high quality site! 500+ inlinks.
		if ( gr->getNumTagTypes("authorityinlink") < 5 &&
		     ! tbuf->addTag(site,"authorityinlink",now,"xmldoc",
				    *ip,"1",2,rdbId,true) )
			return NULL;
	}

	m_newTagBufValid = true;
	return &m_newTagBuf;
}


//
//
// BEGIN OLD SPAM.CPP class
//
//

#define WTMPBUFSIZE (MAX_WORDS *21*3)

// . RULE #28, repetitive word/phrase spam detector
// . set's the "spam" member of each word from 0(no spam) to 100(100% spam)
// . "bits" describe each word in phrasing terminology
// . if more than maxPercent of the words are spammed to some degree then we
//   consider all of the words to be spammed, and give each word the minimum
//   score possible when indexing the document.
// . returns false and sets g_errno on error
char *XmlDoc::getWordSpamVec ( ) {

	if ( m_wordSpamBufValid ) {
		char *wbuf = m_wordSpamBuf.getBufStart();
		if ( ! wbuf ) return (char *)0x01;
		return wbuf;
	}

	setStatus("getting word spam vec");

	// assume not the repeat spammer
	m_isRepeatSpammer = false;

	Words *words = getWords();
	if ( ! words || words == (Words *)-1 ) return (char *)words;

	m_wordSpamBuf.purge();

	int32_t nw = words->getNumWords();
	if ( nw <= 0 ) {
		m_wordSpamBufValid = true;
		return (char *)0x01;
	}

	Phrases *phrases = getPhrases ();
	if ( ! phrases || phrases == (void *)-1 ) return (char *)phrases;
	Bits *bits = getBits();
	if ( ! bits ) return (char *)NULL;

	m_wordSpamBufValid = true;

	//if ( m_isLinkText   ) return true;
	//if ( m_isCountTable ) return true;

	// shortcuts
	//Words *words = m_words;
	//Bits  *bits  = m_bits;

	// if 20 words totally spammed, call it all spam?
	m_numRepeatSpam = 20;

	// shortcut
	int32_t sni = m_siteNumInlinks;
	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }

	// set "m_maxPercent"
	int32_t maxPercent = 6;
	if ( sni > 10  ) maxPercent = 8;
        if ( sni > 30  ) maxPercent = 10;
        if ( sni > 100 ) maxPercent = 20;
        if ( sni > 500 ) maxPercent = 30;
	// fix this a bit so we're not always totally spammed
	maxPercent = 25;

	// get # of words we have to set spam for
	int32_t numWords = words->getNumWords();

	// set up the size of the hash table (number of buckets)
	int32_t  size = numWords * 3;

	// . add a tmp buf as a scratch pad -- will be freed right after
	// . allocate this second to avoid mem fragmentation more
	// . * 2 for double the buckets
	char  tmpBuf [ WTMPBUFSIZE ];
	char *tmp     = tmpBuf;
	int32_t  need    = (numWords * 21) * 3 + numWords;
	if ( need > WTMPBUFSIZE ) {
		tmp = (char *) mmalloc ( need , "Spam" );
		if ( ! tmp ) {
			log("build: Failed to allocate %" PRId32" more "
			    "bytes for spam detection:  %s.",
			    need,mstrerror(g_errno));
			return NULL;
		}
	}

	// set up ptrs
	char *p = tmp;
	// first this
	unsigned char *spam      = (unsigned char *)p; p += numWords ;
	// . this allows us to make linked lists of indices of words
	// . i.e. next[13] = 23--> word #23 FOLLOWS word #13 in the linked list
	int32_t      *next          = (int32_t      *)p;  p += size * 4;
	// hash of this word's stem (or word itself if useStem if false)
	int64_t *bucketHash    = (int64_t *)p;  p += size * 8;
	// that word's position in document
	int32_t      *bucketWordPos = (int32_t      *)p;  p += size * 4;
	// profile of a word
	int32_t      *profile       = (int32_t      *)p;  p += size * 4;
	// is it a common word?
	char      *commonWords   = (char      *)p;  p += size * 1;

	// sanity check
	if ( p - tmp > need ) { g_process.shutdownAbort(true); }

	// clear all our spam percentages for these words
	memset ( spam , 0 , numWords );

	int32_t np;
        // clear the hash table
        int32_t i;
        for ( i = 0 ; i < size ; i++ ) {
                bucketHash   [i] =  0;
                bucketWordPos[i] = -1;
		commonWords  [i] =  0;
        }

	// count position since Words class can now have tags in it
	//
	//int32_t pos = 0;
	//bool usePos = false;
	//if ( words->m_tagIds ) usePos = true;

	int64_t *wids = words->getWordIds();

	// . loop through each word
	// . hash their stems and place in linked list
	// . if no stemming then don't do stemming
	for ( i = 0 ; i < numWords ; i++ ) {
		// . skip punctuation
		// . this includes tags now , too i guess
		if ( wids[i] == 0 ) continue;

		// get the hash of the ith word
		int64_t h = words->getWordId(i);

		// "j" is the bucket index
		int32_t j = (uint64_t)h % size;

		// make sure j points to the right bucket
		while (bucketHash[j]) {
			if ( h == bucketHash[j] ) break;
			if (++j == size) j = 0;
		}
		// if this bucket is occupied by a word then replace it but
		// make sure it adds onto the "linked list"
		if (bucketHash[j])  {
			// add onto linked list for the ith word
			next[i]  = bucketWordPos[j];

			// replace bucket with index to this word
			bucketWordPos[j] = i;
		}
		// otherwise, we have a new occurence of this word
		else {
			bucketHash  [j] = h;

			// store our position # (i) in bucket
			bucketWordPos[j] = i;

			// no next occurence of the ith word yet
			next[i] = -1;
		}
		// if stop word or number then mark it
		if ( bits->isStopWord(i) ) commonWords[j] = 1;
		if ( words->isNum ( i )  ) commonWords[j] = 1;
	}
	// count distinct candidates that had spam and did not have spam
	int32_t spamWords = 0;
	int32_t goodWords = 0;
	// . now cruise down the hash table looking for filled buckets
	// . grab the linked list of indices and make a "profile"
	for ( i = 0 ; i < size ; i++ ) {
		// skip empty buckets
		if (bucketHash[i] == 0) continue;
		np=0;
		// word #j is in bucket #i
		int32_t j = bucketWordPos[i];
		// . cruise down the linked list for this word
		while ( j!=-1) {
			// store position of occurence of this word in profile
			profile [ np++ ] = j;
			// get the position of next occurence of this word
			j = next[ j ];
		}
		// if 2 or less occurences of this word, don't check for spam
		if ( np < 3 ) { goodWords++; continue; }

		//
		// set m_isRepeatSpammer
		//
		// look for a word repeated in phrases, in a big list,
		// where each phrase is different
		//
		int32_t max = 0;
		int32_t count = 0;
		int32_t knp = np;
		// must be 3+ letters, not a stop word, not a number
		if ( words->getWordLen(profile[0]) <= 2 || commonWords[i] )
			knp = 0;
		// scan to see if they are a tight list
		for ( int32_t k = 1 ; k < knp ; k++ ) {
			// are they close together? if not, bail
			if ( profile[k-1] - profile[k] >= 25 ) {
				count = 0;
				continue;
			}
			// otherwise inc it
			count++;
			// must have another word in between or tag
			int32_t a = profile[k];
			int32_t b = profile[k-1];
			bool gotSep = false;
			bool inLink = false;
			for ( int32_t j = a+1 ; j <b ; j++ ) {
				// if in link do not count, chinese spammer
				// does not have his crap in links
				if ( words->getWord(j)[0] == '<' &&
				     words->getWordLen(j)>=3 ) {
					// get the next char after the <
					char nc;
					nc=to_lower_a(words->getWord(j)[1]);
					// now check it for anchor tag
					if ( nc == 'a' ) {
						inLink = true; break; }
				}
				if ( words->getWord(j)[0] == '<' )
					gotSep = true;
				if ( is_alnum_a(words->getWord(j)[0]) )
					gotSep = true;
			}
			// . the chinese spammer always has a separator,
			//   usually another tag
			// . and fix "BOW BOW BOW..." which has no separators
			if      ( ! gotSep  ) count--;
			else if (   inLink  ) count--;
			// get the max
			if ( count > max ) max = count;
		}
		// a count of 50 such monsters indicates the chinese spammer
		if ( max >= 50 )
			m_isRepeatSpammer = true;
		//
		// end m_isRepeatSpammer detection
		//

		// . determine the probability this word was spammed by looking
		//   at the distribution of it's positions in the document
		// . sets "spam" member of each word in this profile
		// . don't check if word occurred 2 or less times
		// . TODO: what about TORA! TORA! TORA!
		// . returns true if 1+ occurences were considered spam
		bool isSpam = setSpam ( profile , np , numWords , spam );
		// don't count stop words or numbers towards this threshold
		if ( commonWords[i] ) continue;
		// tally them up
		if ( isSpam ) spamWords++;
		else          goodWords++;
	}
	// what percent of distinct cadidate words were spammed?
	int32_t totalWords = spamWords + goodWords;
	// if no or ver few words return true
	int32_t percent;
	if ( totalWords <= 10 ) goto done;
	percent    = ( spamWords * 100 ) / totalWords;
	// if 20% of words we're spammed punish everybody now to 100% spam
	// if we had < 100 candidates and < 20% spam, don't bother
	//if ( percent < 5 ) goto done;
	if ( percent <= maxPercent ) goto done;

	// now only set to 99 so each singleton usually gets hashed
	for ( i = 0 ; i < numWords ; i++ )
		if ( words->getWordId(i) && spam[i] < 99 )
			spam[i] = 99;
 done:

	// update the weights for the words
	//for ( i = 0 ; i < numWords ; i++ ) {
	//	m_ww[i] = ( m_ww[i] * (100 - spam[i]) ) / 100;
	//}

	// TODO: use the min word spam algo as in Phrases.cpp for this!
	//for ( i = 0 ; i < numWords ; i++ ) {
	//	m_pw[i] = ( m_pw[i] * (100 - spam[i]) ) / 100;
	//}

	// convert from percent spammed into rank.. from 0 to 10 i guess
	for ( i = 0 ; i < numWords ; i++ )
		spam[i] = (MAXWORDSPAMRANK * (100 - spam[i])) / 100;

	// copy into our buffer
	if ( ! m_wordSpamBuf.safeMemcpy ( (char *)spam , numWords ) )
		return NULL;

	// free our temporary table stuff
	if ( tmp != tmpBuf ) mfree ( tmp , need , "Spam" );

	return m_wordSpamBuf.getBufStart();
}


// . a "profile" is an array of all the positions of a word in the document
// . a "position" is just the word #, like first word, word #8, etc...
// . we map "each" subProfile to a probability of spam (from 0 to 100)
// . if the profile is really big we get really slow (O(n^2)) iterating through
//   many subProfiles
// . so after the first 25 words, it's automatically considered spam
// . return true if one word was spammed w/ probability > 20%
bool XmlDoc::setSpam ( const int32_t *profile, int32_t plen , int32_t numWords ,
			unsigned char *spam ) {
	// don't bother detecting spam if 2 or less occurences of the word
	if ( plen < 3 ) return false;
	// if we have more than 10 words and this word is 20% or more of
	// them then all but the first occurence is spammed
	//log(LOG_INFO,"setSpam numRepeatSpam = %f", m_numRepeatSpam);
	if (numWords > 10 && (plen*100)/numWords >= m_numRepeatSpam) {
		for (int32_t i=1; i<plen; i++)
			spam[profile[i]] = 100;
		return true ;
	}

	// we have to do this otherwise it takes FOREVER to do for plens in
	// the thousands, like i saw a plen of 8338!
	if ( plen > 50 ) { // && m_version >= 93 ) {
		// . set all but the last 50 to a spam of 100%
		// . the last 50 actually occur as the first 50 in the doc
		for (int32_t i=0; i<plen-50;i++)
			spam[profile[i]] = 100;
		// we now have only 50 occurences
		plen = 50;
		// we want to skip the first plen-50 because they actually
		// occur at the END of the document
		profile += plen - 50;
	}

	// just use 40% "quality"
	int32_t off = 3;

	// . now the nitty-gritty part
	// . compute all sub sequences of the profile
	// . similar to a compression scheme (wavelets?)
	// . TODO: word positions should count by two's since punctuation is
	//         not included so start step @ 2 instead of 1
	// . if "step" is 1 we look at every       word position in the profile
	// . if "step" is 2 we look at every other word position
	// . if "step" is 3 we look at every 3rd   word position, etc...
	int32_t maxStep = plen / 4;
	if ( maxStep > 4 ) maxStep = 4;
	// . loop through all possible tuples
	for ( int32_t step = 1 ; step <= maxStep ; step++ ) {
		for ( int32_t window = 0 ; window + 3 < plen ; window+=1) {
			for (int32_t wlen = 3; window+wlen <= plen ; wlen+=1) {
				// continue if step isn't aligned with window
				// length
				if (wlen % step != 0) continue;
				// . get probability that this tuple is spam
				// . returns 0 to 100
				int32_t prob = getProbSpam ( profile + window ,
						             wlen , step);
				// printf("(%i,%i,%i)=%i\n",step,window,
				// wlen,prob);
				// . if the probability is too low continue
				// . was == 100
				if ( prob <= 20 ) continue;
				// set the spammed words spam to "prob"
				// only if it's bigger than their current spam
				for (int32_t i=window; i<window+wlen;i++) {
					// first occurences can have immunity
					// due to doc quality being high
					if ( i >= plen - off ) break;
					if (spam[profile[i]] < prob)
						spam[profile[i]] = prob;
				}
			}

		}
	 }
	 // was this word spammed at all?
	 bool hadSpam = false;
	 for (int32_t i=0; i<plen; i++)
		 if ( spam[profile[i]] > 20 )
			 hadSpam = true;
	 // make sure at least one word survives
	 for (int32_t i=0; i<plen; i++)
		 if ( spam[profile[i]] == 0)
			return hadSpam;
	 // clear the spam level on this guy
	 spam[profile[0]] = 0;
	 // return true if we had spam, false if not
	 return hadSpam;
}


// . returns 0 to 100 , the probability of spam for this subprofile
// . a "profile" is an array of all the positions of a word in the document
// . a "position" is just the word #, like first word, word #8, etc...
// . we are passed a subprofile, "profile", of the actual profile
//   because some of the document may be more "spammy" than other parts
// . inlined to speed things up because this may be called multiple times
//   for each word in the document
// . if "step" is 1 we look at every       word position in the profile
// . if "step" is 2 we look at every other word position
// . if "step" is 3 we look at every 3rd   word position, etc...
int32_t XmlDoc::getProbSpam(const int32_t *profile, int32_t plen, int32_t step) {

	// you can spam 2 or 1 letter words all you want to
	if ( plen <= 2 ) return 0;

	// if our step is bigger than the profile return 0
	if ( step == plen ) return 0;

	int32_t dev=0;
	
	for (int32_t j = 0; j < step; j++) {

		// find avg. of gaps between consecutive tokens in subprofile
		// TODO: isn't profile[i] < profile[i+1]??
		int32_t istop = plen-1;
		int32_t avgSpacing = 0;
		for (int32_t i=0; i < istop; i += step )
			avgSpacing += ( profile[i] - profile[i+1] );
		// there's 1 less spacing than positions in the profile
		// so we divide by plen-1
		avgSpacing = (avgSpacing * 256) / istop;

		// compute standard deviation of the gaps in this sequence
		int32_t stdDevSpacing = 0;
		for (int32_t i = 0 ; i < istop; i += step ) {
			int32_t d = (( profile[i] - profile[i+1]) * 256 ) - avgSpacing;
			if ( d < 0 ) stdDevSpacing -= d;
			else         stdDevSpacing += d;
		}

		// TODO: should we divide by istop-1 for stdDev??
		stdDevSpacing /= istop;

		// average of the stddevs for all sequences
		dev += stdDevSpacing;
	}

	dev /= step;
	
	// if the plen is big we should expect dev to be big
	// here's some interpolation points:
	// plen >=  2  and  dev<= 0.2  --> 100%
	// plen  =  7  and  dev = 1.0  --> 100%
	// plen  = 14  and  dev = 2.0  --> 100%
	// plen  = 21  and  dev = 3.0  --> 100%
	// plen  = 7   and  dev = 2.0  -->  50%

	// NOTE: dev has been multiplied by 256 to avoid using floats
	if ( dev <= 51.2 ) return 100;  // (.2 * 256)
	int32_t prob = ( (256*100/7) * plen ) / dev;

	if (prob>100) prob=100;

	return prob;
}


bool getWordPosVec ( const Words *words ,
		     const Sections *sections,
		     int32_t startDist,
		     const char *fragVec,
		     SafeBuf *wpos ) {

	int32_t dist = startDist; // 0;
	const Section *lastsx = NULL;
	int32_t tagDist = 0;
	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;
	const nodeid_t *tids = words->getTagIds();
	const int32_t *wlens = words->getWordLens();
	const char *const*wptrs = words->getWords();
	int32_t nw = words->getNumWords();

	if ( ! wpos->reserve ( nw * sizeof(int32_t) ) ) return false;
	int32_t *wposvec = (int32_t *)wpos->getBufStart();


	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// save it
		wposvec[i] = dist;

		// tags affect the distance/wordposition cursor
		if ( tids && tids[i] ) {
			// tag distance affects
			nodeid_t tid = tids[i] & BACKBITCOMP;
			if ( isBreakingTagId ( tid ) ) tagDist += SENT_UNITS;
			dist++;
			continue;
		}
		// . and so do sequences of punct
		// . must duplicate this code in Query.cpp for setting
		//   QueryWord::m_posNum
		if ( ! words->getWordId(i) ) {
			// simple space or sequence of just white space
			if ( words->isSpaces(i) )
				dist++;
			// 'cd-rom'
			else if ( wptrs[i][0]=='-' && wlens[i]==1 )
				dist++;
			// 'mr. x'
			else if ( wptrs[i][0]=='.' && words->isSpaces(i,1))
				dist++;
			// animal (dog)
			else
				dist += 2;
			continue;
		}
		// ignore if in repeated fragment
		if ( fragVec && i<MAXFRAGWORDS && fragVec[i] == 0 ) {
			dist++; continue; }

		const Section *sx = NULL;
		if ( sp ) {
			sx = sp[i];
			// ignore if in style tag, etc. and do not
			// increment the distance
			if ( sx->m_flags & NOINDEXFLAGS )
				continue;
		}

		// different sentence?
		if ( sx &&
		     ( ! lastsx ||
		     sx->m_sentenceSection != lastsx->m_sentenceSection ) ) {
			// separate different sentences with 30 units
			dist += SENT_UNITS; // 30;
			// limit this!
			if ( tagDist > 120 ) tagDist = 120;
			// and add in tag distances as well here, otherwise
			// we do not want "<br>" to really increase the
			// distance if the separated words are in the same
			// sentence!
			dist += tagDist;
			// new last then
			lastsx = sx;
			// store the vector AGAIN
			wposvec[i] = dist;
		}

		tagDist = 0;

		dist++;
	}
	return true;
}

bool getDensityRanks ( const int64_t *wids ,
		       int32_t nw ,
		       int32_t hashGroup ,
		       SafeBuf *densBuf ,
		       const Sections *sections ) {

	//int32_t nw = wordEnd - wordStart;

	// make the vector
	if ( ! densBuf->reserve ( nw ) ) return false;

	// convenience
	char *densVec = densBuf->getBufStart();

	// clear i guess
	memset ( densVec , 0 , nw );

	if ( hashGroup != HASHGROUP_BODY &&
	     hashGroup != HASHGROUP_HEADING )
		sections = NULL;

	// scan the sentences if we got those
	Section *ss = NULL;
	if ( sections ) ss = sections->m_firstSent;
	// sanity
	//if ( sections && wordStart != 0 ) { g_process.shutdownAbort(true); }
	for ( ; ss ; ss = ss->m_nextSent ) {
		// count of the alnum words in sentence
		int32_t count = ss->m_alnumPosB - ss->m_alnumPosA;
		// start with one word!
		count--;
		// how can it be less than one alnum word
		if ( count < 0 ) continue;
		// . base density rank on that
		// . count is 0 for one alnum word now
		int32_t dr = MAXDENSITYRANK - count;
		// ensure not negative. make it at least 1. zero means un-set.
		if ( dr < 1 ) dr = 1;
		// mark all in sentence then
		for ( int32_t i = ss->m_senta ; i < ss->m_sentb ; i++ ) {
			// assign
			densVec[i] = dr;
		}
	}
	// all done if using sections
	if ( sections ) return true;


	// count # of alphanumeric words in this string
	int32_t na = 0;
	for ( int32_t i = 0 ; i < nw ; i++ ) if ( wids[i] ) na++;
	// a single alnum should map to 0 "na"
	na--;
	// wtf?
	if ( na < 0 ) return true;
	// compute density rank
	int32_t dr  = MAXDENSITYRANK - na ;
	// at least 1 to not be confused with 0 which means un-set
	if ( dr < 1 ) dr = 1;
	// assign
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// assign
		densVec[i] = dr;
	}
	return true;
}

// . called by hashString() for hashing purposes, i.e. creating posdb keys
// . string is usually the document body or inlink text of an inlinker or
//   perhaps meta keywords. it could be anything. so we need to create this
//   vector based on that string, which is represented by words/phrases here.
bool getDiversityVec( const Words *words, const Phrases *phrases, HashTableX *countTable, SafeBuf *sbWordVec ) {
	const int64_t  *wids  = words->getWordIds ();
	int32_t        nw    = words->getNumWords();
	const int64_t  *pids  = phrases->getPhraseIds2();

	// . make the vector
	// . it will be diversity ranks, so one float per word for now
	//   cuz we convert to rank below though, one byte rank
	if ( ! sbWordVec  ->reserve ( nw*sizeof(float) ) ) return false;

	// get it
	float *ww = (float *)sbWordVec  ->getBufStart();

	int32_t      nexti        = -10;
	int64_t pidLast      = 0;

	// . now consider ourselves the last word in a phrase
	// . adjust the score of the first word in the phrase to be
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) { ww[i] = 0.0; continue; }
		// try to inline this
		int64_t nextWid = 0;
		int64_t lastPid = 0;
		// how many words in the bigram?
		int32_t      nwp = phrases->getNumWordsInPhrase2(i);
		if ( nwp > 0 ) nextWid = wids [i + nwp - 1] ;
		if ( i == nexti ) lastPid = pidLast;
		// get current pid
		int64_t pid = pids[i];
		// get the word and phrase weights for term #i
		float ww2;

		getWordToPhraseRatioWeights ( lastPid  ,
					      wids[i]  ,
					      pid      ,
					      nextWid  ,
					      &ww2     ,
					      countTable);
		// 0 to 1.0
		if ( ww2 < 0 || ww2 > 1.0 ) { g_process.shutdownAbort(true); }
		// save the last phrase id
		if ( nwp > 0 ) {
			nexti        = i + nwp - 1;
			pidLast      = pid;
		}
		// . apply the weights
		// . do not hit all the way down to zero though...
		// . Words.cpp::hash() will not index it then...
		ww[i] = ww2;
	}

	// overwrite the array of floats with an array of chars (ranks)
	char *nww = (char *)ww;

	// convert from float into a rank from 0-15
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		if ( ! ww[i] ) { nww[i] = 0; continue; }
		// 2.50 is max in getWordToPhraseRatioWeights() function
		char wrank = (char) ((ww[i] * ((float)MAXDIVERSITYRANK))/.55);
		// sanity
		if ( wrank > MAXDIVERSITYRANK ) wrank = MAXDIVERSITYRANK;
		if ( wrank < 0 ) { g_process.shutdownAbort(true); }
		// assign now
		nww[i] = wrank;
	}

	return true;
}

// match word sequences of NUMWORDS or more words
#define NUMWORDS 5

// . repeated sentence frags
// . 1-1 with words in body of doc
char *XmlDoc::getFragVec ( ) {

	if ( m_fragBufValid ) {
		char *fb = m_fragBuf.getBufStart();
		if ( ! fb ) return (char *)0x01;
		return fb;
	}

	setStatus("getting frag vec");

	const Words *words = getWords();
	if ( ! words || words == (Words *)-1 ) return (char *)words;
	Bits *bits = getBits();
	if ( ! bits ) return NULL;

	m_fragBuf.purge();

	// ez vars
	const int64_t *wids  = words->getWordIds ();
	int32_t        nw    = words->getNumWords();

	// if no words, nothing to do
	if ( nw == 0 ) {
		m_fragBufValid = true;
		return (char *)0x01;//true;
	}

	// truncate for performance reasons. i've seen this be over 4M
	// and it was VERY VERY SLOW... over 10 minutes...
	// - i saw this tak over 200MB for an alloc for
	//   WeightsSet3 below, so lower from 200k to 50k. this will probably
	//   make parsing inconsistencies for really large docs...
	if ( nw > MAXFRAGWORDS ) nw = MAXFRAGWORDS;

	int64_t   ringWids [ NUMWORDS ];
	int32_t        ringPos  [ NUMWORDS ];
	int32_t        ringi = 0;
	int32_t        count = 0;
	uint64_t   h     = 0;

	// . make the hash table
	// . make it big enough so there are gaps, so chains are not too long
	int32_t       minBuckets = (int32_t)(nw * 1.5);
	uint32_t  nb     = 2 * getHighestLitBitValue ( minBuckets ) ;
	int32_t       need       = nb * (8+4+4);
	char      *buf        = NULL;
	char       tmpBuf[50000];
	if ( need < 50000 ) buf = tmpBuf;
	else                buf = (char *)mmalloc ( need , "WeightsSet3" );
	char      *ptr        = buf;
	uint64_t *hashes = (uint64_t *)ptr; ptr += nb * 8;
	int32_t      *vals       = (int32_t      *)ptr; ptr += nb * 4;
	float     *ww         = (float     *)ptr; ptr += nb * 4;
	if ( ! buf ) return NULL;

	for ( int32_t i = 0 ; i < nw ; i++ ) ww[i] = 1.0;

	if ( ptr != buf + need ) { g_process.shutdownAbort(true); }

	// make the mask
	uint32_t mask = nb - 1;

	// clear the hash table
	memset ( hashes , 0 , nb * 8 );

	// clear ring of hashes
	memset ( ringWids , 0 , NUMWORDS * 8 );

	// for sanity check
	int32_t lastStart = -1;

	// . hash EVERY NUMWORDS-word sequence in the document
	// . if we get a match look and see what sequences it matches
	// . we allow multiple instances of the same hash to be stored in
	//   the hash table, so keep checking for a matching hash until you
	//   chain to a 0 hash, indicating the chain ends
	// . check each matching hash to see if more than NUMWORDS words match
	// . get the max words that matched from all of the candidates
	// . demote the word and phrase weights based on the total/max
	//   number of words matching
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) continue;

		// add new to the 5 word hash
		h ^= wids[i];
		// . remove old from 5 word hash before adding new...
		// . initial ring wids are 0, so should be benign at startup
		h ^= ringWids[ringi];
		// add to ring
		ringWids[ringi] = wids[i];
		// save our position
		ringPos[ringi] = i;
		// wrap the ring ptr if we need to, that is why we are a ring
		if ( ++ringi >= NUMWORDS ) ringi = 0;
		// this 5-word sequence starts with word # "start"
		int32_t start = ringPos[ringi];
		// need at least NUMWORDS words in ring buffer to do analysis
		if ( ++count < NUMWORDS ) continue;
		// . skip if it starts with a word which can not start phrases
		// . that way "a new car" being repeated a lot will not
		//   decrease the weight of the phrase term "new car"
		// . setCountTable() calls set3() with this set to NULL
		//if ( bits && ! bits->canStartPhrase(start) ) continue;
		// sanity check
		if ( start <= lastStart ) { g_process.shutdownAbort(true); }
		// reset max matched
		int32_t max = 0;
		// look up in the hash table
		uint32_t n = h & mask;
		// sanity breach check
		if ( n >= nb ) { g_process.shutdownAbort(true); }
	loop:
		// all done if empty
		if ( ! hashes[n] ) {
			// sanity check
			//if ( n >= nb ) { g_process.shutdownAbort(true); }
			// add ourselves to the hash table now
			hashes[n] = h;
			// sanity check
			//if ( wids[start] == 0 ) { g_process.shutdownAbort(true); }
			// this is where the 5-word sequence starts
			vals  [n] = start;
			// save it
			lastStart = start;
			// debug point
			//if ( start == 7948 )
			//	log("heystart");
			// do not demote words if less than NUMWORDS matched
			if ( max < NUMWORDS ) continue;
			// . how much we should we demote
			// . 10 matching words pretty much means 0 weights
			float demote = 1.0 - ((max-5)*.10);
			if ( demote >= 1.0 ) continue;
			if ( demote <  0.0 ) demote = 0.0;

			// . RULE #26 ("long" phrases)
			// . if we got 3, 4 or 5 in our matching sequence
			// . basically divide by the # of *phrase* terms
			// . multiply by 1/(N-1)
			// . HOWEVER, should we also look at HOW MANY other
			//   sequences matches this too!???
			//float demote = 1.0 / ((float)max-1.0);
			// set3() is still called from setCountTable() to
			// discount the effects of repeated fragments, and
			// the count table only understands score or no score
			//if ( max >= 15 ) demote = 0.0;

			// demote the next "max" words
			int32_t mc = 0;
			int32_t j;
			for ( j = start ; mc < max ; j++ ) {
				// sanity
				if ( j >= nw ) { g_process.shutdownAbort(true); }
				if ( j <  0 ) { g_process.shutdownAbort(true); }
				// skip if not an alnum word
				if ( ! wids[j] ) continue;
				// count it
				mc++;
				// demote it
				ww[j] = (int32_t)(ww[j] * demote);
				if ( ww[j] <= 0 ) ww[j] = 2;
			}
			// save the original i
			int32_t mini = i;
			// advance i, it will be incremented by 1 immediately
			// after hitting the "continue" statement
			i = j - 1;
			// must be at least the original i, we are monotinic
			// otherwise ringPos[] will not be monotonic and core
			// dump ultimately cuz j and k will be equal below
			// and we increment matched++ forever.
			if ( i < mini ) i = mini;
			// get next word
			continue;
		}
		// get next in chain if hash does not match
		if ( hashes[n] != h ) {
			// wrap around the hash table if we hit the end
			if ( ++n >= nb ) n = 0;
			// check out bucket #n now
			goto loop;
		}
		// how many words match so far
		int32_t matched = 0;
		// . we have to check starting at the beginning of each word
		//   sequence since the XOR compositional hash is order
		//   independent
		// . see what word offset this guy has
		int32_t j = vals[n] ;
		// k becomes the start of the current 5-word sequence
		int32_t k = start;
		// sanity check
		if ( j == k ) { g_process.shutdownAbort(true); }
		// skip to next in chain to check later
		if ( ++n >= nb ) n = 0;
		// keep advancing k and j as int32_t as the words match
	matchLoop:
		// get next wid for k and j
		while ( k < nw && ! wids[k] ) k++;
		while ( j < nw && ! wids[j] ) j++;
		if ( k < nw && wids[k] == wids[j] ) {
			matched++;
			k++;
			j++;
			goto matchLoop;
		}
		// keep track of the max matched for i0
		if ( matched > max ) max = matched;
		// get another matching string of words, if possible
		goto loop;
	}

	if ( nw <= 0 ) { g_process.shutdownAbort(true);}

	// make space
	if ( ! m_fragBuf.reserve ( nw ) ) {
		// save it
		int32_t saved = g_errno;
		if ( buf != tmpBuf ) mfree ( buf , need , "WeightsSet3" );
		// reinstate it
		g_errno = saved;
		return NULL;
	}
	// validate
	m_fragBufValid = true;
	// handy ptr
	char *ff = m_fragBuf.getBufStart();

	// convert from floats into frag score, 0 or 1 really
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		if ( ww[i] <= 0.0 ) ff[i] = 0;
		else                ff[i] = 1;
	}

	if ( buf != tmpBuf ) mfree ( buf , need , "WeightsSet3" );

	// wtf?
	if ( ! ff ) { g_process.shutdownAbort(true); }

	return ff;
}


// . inline this for speed
// . if a word repeats in different phrases, promote the word
//   and  demote the phrase
// . if a word repeats in pretty much the same phrase, promote
//   the  phrase and demote the word
// . if you have the window of text "new mexico good times"
//   and word #i is mexico, then:
//   pid1 is "new mexico"
//   wid1 is "mexico"
//   pid2 is "mexico good"
//   wid2 is "good"
// . we store sliderParm in titleRec so we can update it along
//   with title and header weights on the fly from the spider controls
static void getWordToPhraseRatioWeights ( int64_t   pid1 , // pre phrase
					  int64_t   wid1 ,
					  int64_t   pid2 ,
					  int64_t   wid2 , // post word
					  float      *retww   ,
					  const HashTableX *tt1) {

	static float s_wtab[30][30];
	static float s_fsp;
	// from 0 to 100
	char sliderParm = g_conf.m_sliderParm;

	// . to support RULE #15 (word to phrase ratio)
	// . these weights are based on the ratio of word to phrase count
	//   for a particular word
	static char s_sp = -1;
	if ( s_sp != sliderParm ) {
		// . set it to the newly updated value
		// . should range from 0 up to 100
		s_sp = sliderParm;
		// the float version
		s_fsp = (float)sliderParm / 100.0;
		// sanity test
		if ( s_fsp < 0.0 || s_fsp > 1.0 ) { g_process.shutdownAbort(true); }
		// i is the word count, how many times a particular word
		// occurs in the document
		for ( int32_t i = 0 ; i < 30 ; i++ ) {
			// . k is the phrase count, how many times a particular phrase
			//   occurs in the document
			// . k can be GREATER than i because we index only phrase terms
			//   sometimes when indexing neighborhoods, and not the
			//   single words that compose them
			for ( int32_t k = 0 ; k < 30 ; k++ ) {
				// do not allow phrase count to be greater than
				// word count, even though it can happen since we
				// add imported neighborhood pwids to the count table
				int32_t j = k;
				if ( k > i ) j = i;
				// get ratio
				//float ratio = (float)phrcount / (float)wrdcount;
				float ratio = (float)j/(float)i;
				// it should be impossible that this can be over 1.0
				// but might happen due to hash collisions
				if ( ratio > 1.0 ) ratio = 1.0;
				// restrict the range we can weight a word or phrase
				// based on the word count
				//float r = 1.0;
				//if      ( i >= 20 ) r = 2.1;
				//else if ( i >= 10 ) r = 1.8;
				//else if ( i >=  4 ) r = 1.5;
				//else                r = 1.3;
				//g_ptab[i][k] = 1.00;
				s_wtab[i][k] = 1.00;
				if ( i <= 1 ) continue;
				// . we used to have a sliding bar between 0.0 and 1.0.
				//   word is weighted (1.0 - x) and phrase is weighted
				//   by (x). however, x could go all the way to 1.0
				//   even when i = 2, so we need to restrict x.
				// . x is actually "ratio"
				// . when we have 8 or less word occurences, do not
				//   remove more than 80% of its score, a 1/5 penalty
				//   is good enough for now. but for words that occur
				//   a lot in the link text or pwids, go to town...
				if      ( i <=  2 && ratio >= .50 ) ratio = .50;
				else if ( i <=  4 && ratio >= .60 ) ratio = .60;
				else if ( i <=  8 && ratio >= .80 ) ratio = .80;
				else if ( i <= 12 && ratio >= .95 ) ratio = .95;
				// round up, so many "new mexico" phrases but only
				// make it up to 95%...
				if ( ratio >= .95 ) ratio = 1.00;
				// if word's phrase is repeated 3 times or more then
				// is a pretty good indication that we should weight
				// the phrase more and the word itself less
				//if ( k >= 3 && ratio < .90 ) ratio = .90;
				// compute the weights
				//float pw = 2.0 * ratio;
				//float ww = 2.0 * (1.0 - ratio);
				float ww = (1.0 - ratio);

				// . punish words a little more
				// . if we got 50% ratio, words should not get as much
				//   weight as the phrase
				//ww *= .45;
				// do not weight to 0, no less than .15
				if ( ww < 0.0001 ) ww = 0.0001;
				//if ( pw < 0.0001 ) pw = 0.0001;
				// do not overpromote either
				//if ( ww > 2.50 ) ww = 2.50;
				//if ( pw > 2.50 ) pw = 2.50;
				// . do a sliding weight of the weight
				// . a "ww" of 1.0 means to do no weight
				// . can't do this for ww cuz we use "mod" below
				//float newWW = s_fsp*ww + (1.0-s_fsp)*1.00;
				//float newPW = s_fsp*pw + (1.0-s_fsp)*1.00;
				// limit how much we promote a word because it
				// may occur 30 times total, but have a phrase count
				// of only 1. however, the other 29 times it occurs it
				// is in the same phrase, just not this particular
				// phrase.
				//if ( ww > 2.0 ) ww = 2.0;
				s_wtab[i][k] = ww;
				//g_ptab[i][k] = newPW;
				//logf(LOG_DEBUG,"build: wc=%" PRId32" pc=%" PRId32" ww=%.2f "
				//"pw=%.2f",i,k,s_wtab[i][k],g_ptab[i][k]);
			}
		}
	}

	int32_t phrcount1 = 0;
	int32_t phrcount2 = 0;
	int32_t wrdcount1 = 0;
	int32_t wrdcount2 = 0;
	if ( !tt1->isTableEmpty() ) {
		if (pid1) phrcount1 = tt1->getScore(&pid1);
		if (pid2) phrcount2 = tt1->getScore(&pid2);
		if (wid1) wrdcount1 = tt1->getScore(&wid1);
		if (wid2) wrdcount2 = tt1->getScore(&wid2);
	}
	// if we are always ending the same phrase, like "Mexico"
	// in "New Mexico"... get the most popular phrase this word is
	// in...
	int32_t phrcountMax = phrcount1;
	int32_t wrdcountMin = wrdcount1;
	// these must actually exist to be part of the selection
	if ( pid2 && phrcount2 > phrcountMax ) phrcountMax = phrcount2;
	if ( wid2 && wrdcount2 < wrdcountMin ) wrdcountMin = wrdcount2;


	// . but if we are 'beds' and in a popular phrase like 'dog beds'
	//   there maybe a lot of other phrases mentioned that have 'beds'
	//   in them like 'pillow beds', 'pet beds', but we need to assume
	//   that is phrcountMax is high enough, do not give much weight to
	//   the word... otherwise you can subvert this algorithm by just
	//   adding other random phrases with the word 'bed' in them.
	// . BUT, if a page has 'X beds' with a lot of different X's then you
	//   still want to index 'beds' with a high score!!! we are trying to
	//   balance those 2 things.
	// . do this up here before you truncate phrcountMax below!!
	float mod = 1.0;
	if      ( phrcountMax <=  6 ) mod = 0.50;
	else if ( phrcountMax <=  8 ) mod = 0.20;
	else if ( phrcountMax <= 10 ) mod = 0.05;
	else if ( phrcountMax <= 15 ) mod = 0.03;
	else                          mod = 0.01;

	// scale wrdcount1/phrcountMax down for the s_wtab table
	if ( wrdcount1 > 29 ) {
		float ratio = (float)phrcountMax / (float)wrdcount1;
		phrcountMax = (int32_t)((29.0 * ratio) + 0.5);
		wrdcount1   = 29;
	}
	if ( phrcountMax > 29 ) {
		float ratio = (float)wrdcount1 / (float)phrcountMax;
		wrdcount1   = (int32_t)((29.0 * ratio) + 0.5);
		phrcountMax = 29;
	}

	// . sanity check
	// . neighborhood.cpp does not always have wid/pid pairs
	//   that match up right for some reason... so we can't do this
	//if ( phrcount1 > wrdcount1 ) { g_process.shutdownAbort(true); }
	//if ( phrcount2 > wrdcount2 ) { g_process.shutdownAbort(true); }

	// apply the weights from the table we computed above
	*retww = mod   *   s_wtab[wrdcount1][phrcountMax];

	// slide it
	*retww = s_fsp*(*retww) + (1.0-s_fsp)*1.00;

	// ensure we do not punish too hard
	if ( *retww <= 0.0 ) *retww = 0.01;

	if ( *retww > 1.0 ) { g_process.shutdownAbort(true); }

	// . if the word is Mexico in 'New Mexico good times' then
	//   phrase term #i which is, say, "Mexico good" needs to
	//   get the min word count when doings its word to phrase
	//   ratio.
	// . it has two choices, it can use the word count of
	//   "Mexico" or it can use the word count of "good".
	// . say, each is pretty high in the document so the phrase
	//   ends up getting penalized heavily, which is good because
	//   it is a nonsense phrase.
	// . if we had "united socialist soviet republic" repeated
	//   a lot, the phrase "socialist soviet" would score high
	//   and the individual words would score low. that is good.
	// . try to seek the highest weight possible for this phrase
	//   by choosing the lowest word count possible
	// . NO LONGER AFFECT phrase weights because just because the
	//   words occur a lot in the document and this may be the only
	//   occurence of this phrase, does not mean we should punish
	//   the phrase.  -- MDW
	//*retpw = 1.0;
	return;
}

// for registerSleepCallback
static void clockSyncWaitWrapper ( int fd , void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	THIS->m_masterLoop ( THIS->m_masterState );
}

// . a special call
// . returns -1 if blocked, 1 otherwise, 0 on error
char XmlDoc::waitForTimeSync ( ) {
	// unregister?
	if ( isClockInSync() && m_alreadyRegistered ) {
		// note it
		log("build: clock now synced for %s",m_firstUrl.getUrl());
		g_loop.unregisterSleepCallback(m_masterState,
					       clockSyncWaitWrapper);
	}
	// return 1 if synced!
	if ( isClockInSync() ) return 1;
	// already registered? wait another 1000ms
	if ( m_alreadyRegistered ) return -1;
	// flag it
	m_alreadyRegistered = true;
	// note it
	log("build: waiting for clock to sync for %s",m_firstUrl.getUrl());
	// this should mean it is re-called later
	if ( g_loop.registerSleepCallback ( 1000 , // 1000 ms
					    m_masterState ,
					    clockSyncWaitWrapper ,
					    m_niceness    ))
		// wait for it, return -1 since we blocked
		return -1;
	// if was not able to register, ignore delay
	log("doc: failed to register clock wait callback");
	return 0;
}

bool XmlDoc::getIsInjecting ( ) {
	bool isInjecting = false;
	//if ( g_inPageInject ) isInjecting = true;
	if ( m_sreqValid && m_sreq.m_isInjecting ) isInjecting = true;
	if ( m_isInjecting && m_isInjectingValid ) isInjecting = true;
	return isInjecting;
}

Json *XmlDoc::getParsedJson ( ) {

	if ( m_jpValid ) return &m_jp;

	// core if not a json object
	if ( m_contentTypeValid && m_contentType != CT_JSON &&
	     // spider status docs are now really json
	     m_contentType != CT_STATUS ) {
		g_process.shutdownAbort(true); }

	// \0 terminated
	char **pp = getUtf8Content();
	if ( ! pp || pp == (void *)-1 ) return (Json *)pp;

	// point to the json
	char *p = *pp;

	// empty? all done then.
	//if ( ! p ) return (char *)pp;

	// . returns NULL and sets g_errno on error
	// . if p is NULL i guess this should still be ok and be empty
	if ( ! m_jp.parseJsonStringIntoJsonItems ( p , m_niceness ) ) {
		g_errno = EBADJSONPARSER;
		return NULL;
	}

	m_jpValid = true;
	return &m_jp;
}


void XmlDoc::callCallback() {
	if(m_callback1 )
		m_callback1(m_state);
	else
		m_callback2(m_state);
}
