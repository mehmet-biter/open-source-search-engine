// Matt Wells, copyright Feb 2001

// maintains a simple array of CollectionRecs

#ifndef COLLECTIONDB_H
#define COLLECTIONDB_H

// . max # of collections we're serving
// . may have to update if business gets going (or make dynamic)
// . lowered to 16 to save some mem
//#define MAX_COLL_RECS 16 // 256
//#define MAX_COLLS (MAX_COLL_RECS)

#include "SafeBuf.h"

bool addCollToTable ( char *coll , collnum_t collnum ) ;

class WaitEntry {
public:
	void (* m_callback) (void *state);
	void *m_state;
	char *m_coll;
	bool  m_purgeSeeds;
	class CollectionRec *m_cr;
	// ptr to list of parm recs for Parms.cpp
	char *m_parmPtr;
	char *m_parmEnd;
	class UdpSlot *m_slot;
	bool m_doRebuilds;
	bool m_rebuildActiveList;
	bool m_doProxyRebuild;
	bool m_updatedRound;
	collnum_t m_collnum;
	bool m_registered;
	int32_t m_errno;
	bool m_sentReply;
};

class Collectiondb  {

 public:
	Collectiondb();

	// does nothing
	void reset() ;

	// this loads all the recs from host #0 
	//bool load ( bool isDump = false );

	// called by main.cpp to fill in our m_recs[] array with
	// all the coll.*.*/coll.conf info
	bool loadAllCollRecs ( );

	// after main.cpp loads all rdb trees it calls this to remove
	// bogus collnums from the trees i guess
	bool cleanTrees ( ) ;

	// . this will save all conf files back to disk that need it
	// . returns false and sets g_errno on error, true on success
	bool save ( );
	bool m_needsSave;

	// returns i so that m_recs[i].m_coll = coll
	collnum_t getCollnum ( const char *coll , int32_t collLen );
	collnum_t getCollnum ( const char *coll ); // coll is NULL terminated here

	char *getCollName ( collnum_t collnum );
	char *getColl     ( collnum_t collnum ) {return getCollName(collnum);};

	// get coll rec specified in the HTTP request
	class CollectionRec *getRec ( class HttpRequest *r ,
				      bool useDefaultRec = true );

	// do not support diffbot style token/name style for this one:
	char *getDefaultColl ( HttpRequest *r ) ;

	//class CollectionRec *getRec2 ( class HttpRequest *r ,
	//			       bool useDefaultRec = true );
	
	// . get collectionRec from name
	// returns NULL if not available
	class CollectionRec *getRec ( const char *coll );

	class CollectionRec *getRec ( const char *coll , int32_t collLen );

	class CollectionRec *getRec ( collnum_t collnum);

	//class CollectionRec *getDefaultRec ( ) ;

	class CollectionRec *getFirstRec      ( ) ;
	char                *getFirstCollName ( ) ;
	collnum_t            getFirstCollnum  ( ) ;

	// . how many collections we have in here
	// . only counts valid existing collections
	int32_t getNumRecsUsed() { return m_numRecsUsed; };

	// . does this requester have root admin privledges???
	// . uses the root collection record!
	//bool isAdmin ( class HttpRequest *r , class TcpSocket *s );

	//collnum_t getNextCollnum ( collnum_t collnum );

	// what collnum will be used the next time a coll is added?
	collnum_t reserveCollNum ( ) ;

	//int64_t getLastUpdateTime () { return m_lastUpdateTime; };
	// updates m_lastUpdateTime so g_spiderCache know when to reload
	//void     updateTime         ();

	// private:

	// . these are called by handleRequest
	// . based on "action" cgi var, 1-->add,2-->delete,3-->update
	//bool addRec     ( char *coll , char *cc , int32_t cclen , bool isNew ,
	//		  collnum_t collnum , bool isDump , //  = false );
	//		  bool saveRec ); // = true


	bool addExistingColl ( char *coll, collnum_t collnum );

	bool addNewColl ( char *coll , 
			  char customCrawl ,
			  char *cpc , 
			  int32_t cpclen , 
			  bool saveIt ,
			  collnum_t newCollnum ) ;

	bool registerCollRec ( CollectionRec *cr ,  bool isNew ) ;

	bool addRdbBaseToAllRdbsForEachCollRec ( ) ;
	bool addRdbBasesForCollRec ( CollectionRec *cr ) ;

	bool growRecPtrBuf ( collnum_t collnum ) ;
	bool setRecPtr ( collnum_t collnum , CollectionRec *cr ) ;

	// returns false if blocked, true otherwise. 
	//bool deleteRec  ( char *coll , WaitEntry *we );
	bool deleteRec2 ( collnum_t collnum );//, WaitEntry *we ) ;

	//bool updateRec ( CollectionRec *newrec );
	bool deleteRecs ( class HttpRequest *r ) ;

	//void deleteSpiderColl ( class SpiderColl *sc );

	// returns false if blocked, true otherwise. 
	//bool resetColl ( char *coll , WaitEntry *we , bool purgeSeeds );
	bool resetColl2 ( collnum_t oldCollnum, 
			  collnum_t newCollnum,
			  //WaitEntry *we , 
			  bool purgeSeeds );

	// . keep up to 128 of them, these reference into m_list
	// . COllectionRec now includes m_needsSave and m_lastUpdateTime
	class CollectionRec  **m_recs;//           [ MAX_COLLS ];

	// now m_recs[] points into a safebuf that is just an array
	// of collectionrec ptrs. so we have to grow that safebuf possibly
	// in order to add a new collection rec ptr to m_recs
	SafeBuf m_recPtrBuf;

	//bool            m_needsSave      [ MAX_COLLS ];
	//int64_t       m_lastUpdateTime [ MAX_COLLS ];
	int32_t            m_numRecs;
	int32_t            m_numRecsUsed;
	
	int32_t m_wrapped;

	int32_t m_numCollsSwappedOut;

	bool m_initializing;
	//int64_t            m_lastUpdateTime;
};

extern class Collectiondb g_collectiondb;

// Matt Wells, copyright Feb 2002

// . a collection record specifies the spider/index/search parms of a 
//   collection of web pages
// . there's a Msg class to send an update signal to all the hosts once
//   we've used Msg1 to add a new rec or delete an old.  The update signal
//   will make the receiving hosts flush their CollectionRec buf so they
//   have to send out a Msg0 to get it again
// . we have a default collection record, a main collection record and
//   then other collection records
// . the default collection record values override all
// . but the collection record values can override SiteRec values
// . so if spider is disabled in default collection record, then nobody
//   can spider!
// . override the g_conf.* vars where * is in this class to use
//   Collection db's default values
// . then add in the values of the specialzed collection record
// . so change "if ( g_conf.m_spideringEnabled )" to something like
//   Msg33 msg33;
//   if ( ! msg33.getCollectionRec ( m_coll, m_collLen ) ) return false;
//   CollectionRec *r = msg33.getRec();
//   CollectoinRec *d = msg33.getDefaultRec();
//   if ( ! r->m_spideringEnabled || ! d->m_spideringEnabled ) continue;
//   ... otherwise, spider for the m_coll collection
//   ... pass msg33 to Msg14::spiderDoc(), etc...

// how many url filtering patterns?
#define MAX_FILTERS    96  // up to 96 url regular expression patterns
//#define MAX_PRIORITY_QUEUES MAX_SPIDER_PRIORITIES * 2//each can be old or new
#define MAX_REGEX_LEN  256 // each regex can be up to this many bytes
// max html head length
//#define MAX_HTML_LEN (4*1024)
// max chars the executable path+name can be
#define MAX_FILTER_LEN 64
// max length of a tagdb filter, typically just a domain/site
//#define MAX_TAG_FILTER_LEN 128

//#define MAX_SEARCH_PASSWORDS 5
//#define MAX_BANNED_IPS       400
//#define MAX_SEARCH_IPS       32
//#define MAX_SPAM_IPS         5
//#define MAX_ADMIN_IPS        15
//#define MAX_ADMIN_PASSWORDS  10
//#define MAX_SITEDB_FILTERS 256

#define MAX_AD_FEEDS         10
#define MAX_CGI_URL          1024
#define MAX_XML_LEN          256

#define SUMMARYHIGHLIGHTTAGMAXSIZE 128

// max length of a sitedb filter, typically just a domain/site
//#define MAX_SITE_EXPRESSION_LEN 128
//#define MAX_SITE_EXPRESSIONS    256

#include "regex.h"

#include "Url.h"  // MAX_COLL_LEN
//#include "Sync.h"
//#include "Parms.h"       // for MAX_PARMS
//#include "HttpRequest.h"
//#include "Collectiondb.h" // PASSWORD_MAX_LEN
//#include "Spider.h" //MAX_SPIDER_PRIORITIES
//#include "HashTable.h"
#include "HashTableX.h"
//#include "RdbList.h"
//#include "Rdb.h" // for RdbBase

// fake this for now
#define RDB_END2 80

#include "PingServer.h" // EmailInfo

// how many counts are in CrawlInfo below????
#define NUMCRAWLSTATS 8

// used by diffbot to control spidering per collection
class CrawlInfo {
 public:

	//
	// WARNING!! Add NEW stats below the LAST member variable in
	// this class so that it can still load the OLD file on disk
	// which is in the OLD format!
	//

	int64_t m_objectsDeleted;        // 1
	int64_t m_objectsAdded;          // 2
	int64_t m_urlsConsideredNOTUSED; // 3
	int64_t m_pageDownloadAttempts;  // 4
	int64_t m_pageDownloadSuccesses; // 5
	int64_t m_pageProcessAttempts;   // 6
	int64_t m_pageProcessSuccesses;  // 7
	int64_t m_urlsHarvested;         // 8


	int32_t m_lastUpdateTime;

	// this is non-zero if urls are available to be spidered right now.
	int32_t m_hasUrlsReadyToSpider;

	// last time we launched a spider. 0 on startup.
	uint32_t m_lastSpiderAttempt; // time_t
	// time we had or might have had a url available for spidering
	uint32_t m_lastSpiderCouldLaunch; // time_t

	int32_t m_collnum;

	// have we sent out email/webhook notifications crawl has no urls
	// currently in the ready queue (doledb) to spider?
	char m_sentCrawlDoneAlert;

	//int32_t m_numUrlsLaunched;
	int32_t m_dummy1;

	// keep separate because when we receive a crawlinfo struct from
	// a host we only add these in if it matches our round #
	int64_t m_pageDownloadSuccessesThisRound;
	int64_t m_pageProcessSuccessesThisRound;


	void reset() { memset ( this , 0 , sizeof(CrawlInfo) ); };
	//bool print (class SafeBuf *sb ) ;
	//bool setFromSafeBuf (class SafeBuf *sb ) ;
};


class CollectionRec {

 public:

	// active linked list of collectionrecs used by spider.cpp
	class CollectionRec *m_nextActive;

	// these just set m_xml to NULL
	CollectionRec();
	virtual ~CollectionRec();
	
	//char *getDiffbotToken ( int32_t *tokenLen );

	// . set ourselves from serialized raw binary
	// . returns false and sets errno on error
	bool set ( char *data , int32_t dataSize );

	// . set ourselves the cgi parms in an http request
	// . unspecified cgi parms will be assigned default values
	// . returns false and sets errno on error
	bool set ( class HttpRequest *r , class TcpSocket *s );

	// calls hasPermission() below
	bool hasPermission ( class HttpRequest *r , class TcpSocket *s ) ;

	// . does this user have permission for editing this collection?
	// . "p" is the password for this collection in question
	// . "ip" is the connecting ip
	bool hasPermission ( char *p, int32_t plen , int32_t ip ) ;

	// is this ip from a spam assassin?
	bool isAssassin ( int32_t ip );

	int64_t getNumDocsIndexed();

	// messes with m_spiderColl->m_sendLocalCrawlInfoToHost[MAX_HOSTS]
	// so we do not have to keep sending this huge msg!
	bool shouldSendLocalCrawlInfoToHost ( int32_t hostId );
	void sentLocalCrawlInfoToHost ( int32_t hostId );
	void localCrawlInfoUpdate();

	// how many bytes would this record occupy in raw binary format?
	//int32_t getStoredSize () { return m_recSize; };

	// . serialize ourselves into the provided buffer
	// . used by Collectiondb::addRec()
	// . return # of bytes stored
	// . first 4 bytes in "buf" will also be the size of all the data
	//   which should be what is returned - 4
	//int32_t store ( char *buf , int32_t bufMaxSize );

	// . deserialize from a buf
	// . first 4 bytes must be the total size
	// . returns false and sets g_errno on error
	//bool set ( char *buf );

	// . store it in raw binary format
	// . returns # of bytes stored into "buf"
	// . returs -1 and sets errno on error
	//int32_t store ( char *buf , char *bufEnd );

	// reset to default values
	void setToDefaults () ;

	// . stuff used by Collectiondb
	// . do we need a save or not?
	bool      save ();
	bool      m_needsSave;

	bool      load ( char *coll , int32_t collNum ) ;
	void reset();

	//void setUrlFiltersToDefaults();

	// for customcrawls
	bool rebuildUrlFilters();

	// for regular crawls
	bool rebuildUrlFilters2();
  
	// for diffbot crawl or bulk jobs
	bool rebuildUrlFiltersDiffbot();

	// rebuild the regexes related to diffbot, such as the one for the URL pattern
	bool rebuildDiffbotRegexes();

	bool rebuildLangRules( char *lang , char *tld );

	bool rebuildShallowRules();

	bool m_urlFiltersHavePageCounts;

	// moved from SpiderColl so we can load up at startup
	//HashTableX m_pageCountTable;

	// . when was the last time we changed?
	//int64_t m_lastUpdateTime;

	// the all important collection name, NULL terminated
	char  m_coll [ MAX_COLL_LEN + 1 ] ;
	int32_t  m_collLen;

	// used by SpiderCache.cpp. g_collectiondb.m_recs[m_collnum] = this
	collnum_t m_collnum;

	// for doing DailyMerge.cpp stuff
	int32_t m_dailyMergeStarted; // time_t
	int32_t m_dailyMergeTrigger;

	class CollectionRec *m_nextLink;
	class CollectionRec *m_prevLink;

	char m_dailyMergeDOWList[48];

	int32_t m_treeCount;

	bool swapOut();
	bool m_swappedOut;

	int64_t m_spiderCorruptCount;

	// holds ips that have been detected as being throttled and we need
	// to backoff and use proxies on
	HashTableX m_twitchyTable;

	// spider controls for this collection
	char m_spideringEnabled ;
	float m_newSpiderWeight         ;
	int32_t  m_spiderDelayInMilliseconds;

	// is in active list in spider.cpp?
	bool m_isActive;

	// . at what time did the spiders start?
	// . this is incremented when all urls have been spidered and
	//   the next round begins
	uint32_t m_spiderRoundStartTime; // time_t
	// this begins at 0, and increments when all the urls have been
	// spidered and begin the next round
	int32_t   m_spiderRoundNum;

	char  m_makeImageThumbnails;

	int32_t m_thumbnailMaxWidthHeight ;

	char  m_indexSpiderReplies;
	char  m_indexBody;

	char  m_sameHostLinks           ; // spider links from same host only?

	// do not re-add outlinks to spiderdb if less than this many days
	// have elapsed since the last time we added them to spiderdb
	float m_outlinksRecycleFrequencyDays ;

	char  m_dedupingEnabled         ; // dedup content on same hostname
	char  m_dupCheckWWW             ;
	char  m_detectCustomErrorPages  ;
	char  m_useSimplifiedRedirects  ;
	char  m_useIfModifiedSince      ;
	char  m_useTimeAxis             ;
	char  m_buildVecFromCont        ;
	int32_t  m_maxPercentSimilarPublishDate;
	char  m_useSimilarityPublishDate;
	char  m_oneVotePerIpDom         ;
	char  m_doUrlSpamCheck          ; //filter urls w/ naughty hostnames
	int32_t  m_deadWaitMaxAge          ;
	char  m_doLinkSpamCheck         ; //filters dynamically generated pages
	int32_t  m_linkTextAnomalyThresh   ; //filters linktext that is unique
	char  m_tagdbColl [MAX_COLL_LEN+1]; // coll to use for tagdb lookups
	char  m_doChineseDetection      ;
	char  m_delete404s              ;
	char  m_defaultRatForXML        ;
	char  m_defaultRatForHTML       ;
	char  m_siteClusterByDefault    ;
	char  m_doInnerLoopSiteClustering;
	char  m_enforceNewQuotas        ;
	char  m_doIpLookups             ; // considered iff using proxy
	char  m_useRobotsTxt            ;
	char  m_obeyRelNoFollowLinks    ;
	char  m_forceUseFloaters        ;
	char  m_automaticallyUseProxies ;
	char  m_automaticallyBackOff    ;
	char  m_applyFilterToText       ; // speeds us up
	char  m_allowHttps              ; // read HTTPS using SSL
	char  m_recycleContent          ;
	char  m_getLinkInfo             ; // turn off to save seeks
	char  m_computeSiteNumInlinks   ;
	char  m_indexInlinkNeighborhoods;
	char  m_doRobotChecking         ;
	char  m_needDollarSign          ;
	char  m_getNewsTopic            ;
	char  m_newAlgo                 ; // use new links: termlist algo
	char  m_useGigabitVector        ;
	char  m_removeBannedPages       ;

	float m_inlinkNeighborhoodsScoreScalar;

	float m_updateVotesFreq         ; // in days. replaced m_recycleVotes
	float m_sortByDateWeight        ;

        char  m_dedupURLDefault             ;
	int32_t  m_topicSimilarCutoffDefault   ;
	char  m_useNewDeduping              ;
	char  m_doTierJumping               ;
	float m_numDocsMultiplier           ;
	int32_t  m_percentSimilarSummary       ; // Dedup by summary similiarity
	int32_t  m_summDedupNumLines           ;
	int32_t  m_contentLenMaxForSummary     ;

	int32_t  m_maxQueryTerms;

	char  m_spiderStatus;
	//char *m_spiderStatusMsg;

	float m_sameLangWeight;

	// Language stuff
	float			m_languageUnknownWeight;
	float			m_languageWeightFactor;
	char			m_enableLanguageSorting;
	char 			m_defaultSortLanguage2[6];
	char 			m_languageMethodWeights[10];
	int32_t 			m_languageBailout;
	int32_t 			m_languageThreshold;
	int32_t 			m_languageSamples;
	int32_t 			m_langPageLimit;
	char 			m_defaultSortCountry[3];

	int32_t  m_filterTimeout;                // kill filter pid after X secs

	// for Spider.cpp
	int32_t m_updateRoundNum;

	// IMPORT PARMS
	char    m_importEnabled;
	SafeBuf m_importDir;
	int32_t    m_numImportInjects;
	class ImportState *m_importState;

	SafeBuf m_collectionPasswords;
	SafeBuf m_collectionIps;

	// from Conf.h
	int32_t m_posdbMinFilesToMerge ;
	int32_t m_titledbMinFilesToMerge ;
	int32_t m_sectiondbMinFilesToMerge ;
	int32_t m_linkdbMinFilesToMerge ;
	int32_t m_tagdbMinFilesToMerge ;

	int32_t m_numCols; // number of columns for results page
	int32_t m_screenWidth; // screen width to balance columns
	int32_t m_adWidth; // how wide the ad Column is in pixels

	char  m_dedupResultsByDefault   ;
	char  m_doTagdbLookups        ;
	char  m_clusterByTopicDefault    ;
	char  m_restrictTitledbForQuery ; // move this down here
	char  m_useOldIps               ;
	char  m_banDomains              ;
	char  m_requireAllTerms         ;
	int32_t  m_summaryMode		;
	char  m_deleteTimeouts          ; // can delete docs that time out?
	char  m_allowAdultDocs          ;
	char  m_doSerpDetection         ;
	char  m_useCanonicalRedirects   ;

	int32_t  m_maxNumSpiders             ; // per local spider host
	float m_spiderNewPct;             ; // appx. percentage new documents

	int32_t m_lastResetCount;

	// start another set of flags using the old m_spiderTimeShift
	char  m_useCurrentTime          ; // ... for m_spiderTime2

	// max # of pages for this collection
	int64_t  m_maxNumPages;

	//double m_maxPagesPerSecond;
	float m_maxPagesPerSecond;

	int32_t  m_maxSimilarityToIndex;

	// . only the root admin can set the % of spider time this coll. gets
	// . OBSOLETE: this has been replaced by max pages per second var!!
	int32_t m_spiderTimePercent;

	// controls for query-dependent summary/title generation
	int32_t m_titleMaxLen;
	int32_t m_minTitleInLinkers;
	int32_t m_maxTitleInLinkers;
	int32_t m_summaryMaxLen;
	int32_t m_summaryMaxNumLines;
	int32_t m_summaryMaxNumCharsPerLine;
	char m_useNewSummaries;

	char m_getDocIdScoringInfo;

  /***** 
   * !! Start Diffbot paramamters !! *
   *****/
  
  SafeBuf m_diffbotToken;
	SafeBuf m_diffbotCrawlName;
	// email for emailing when crawl limit hit
	SafeBuf m_notifyEmail;
	// fetch this url when crawl limit hit
	SafeBuf m_notifyUrl;
	// the default respider frequency for all rows in url filters
	float   m_collectiveRespiderFrequency;
	float   m_collectiveCrawlDelay;//SpiderWait;
	SafeBuf m_diffbotSeeds;

	// use for all now...
	SafeBuf m_diffbotApiUrl;

	// only process pages whose content matches this pattern
	SafeBuf m_diffbotPageProcessPattern;
	// only process urls that match this pattern
	SafeBuf m_diffbotUrlProcessPattern;
	// only CRAWL urls that match this pattern
	SafeBuf m_diffbotUrlCrawlPattern;
  
	// regex support
	SafeBuf m_diffbotUrlCrawlRegEx;
	SafeBuf m_diffbotUrlProcessRegEx;
	regex_t m_ucr;
	regex_t m_upr;
	int32_t m_hasucr:1;
	int32_t m_hasupr:1;
  
	// only crawl pages within hopcount of a seed. 0 for no limit 
	int32_t m_diffbotMaxHops;

	char    m_diffbotOnlyProcessIfNewUrl;

	char m_isCustomCrawl;
	int64_t m_maxToCrawl;
	int64_t m_maxToProcess;
	int32_t      m_maxCrawlRounds;

	// in seconds now
	uint32_t m_diffbotCrawlStartTime;
	uint32_t m_diffbotCrawlEndTime;

	// our local crawling stats
	CrawlInfo m_localCrawlInfo;
	// total crawling stats summed up from all hosts in network
	CrawlInfo m_globalCrawlInfo;

	// holds the latest CrawlInfo for each host for this collrec
	SafeBuf m_crawlInfoBuf;

  /***** 
   * !! End of Diffbot paramamters !! *
   *****/

	// list of url patterns to be indexed.
	SafeBuf m_siteListBuf;
	char m_spiderToo;

	// can be "web" "english" "romantic" "german" etc.
	SafeBuf m_urlFiltersProfile;

	// . now the url regular expressions
	// . we chain down the regular expressions
	// . if a url matches we use that tagdb rec #
	// . if it doesn't match any of the patterns, we use the default site #
	// . just one regexp per Pattern
	// . all of these arrays should be the same size, but we need to 
	//   include a count because Parms.cpp expects a count before each
	//   array since it handle them each individually
	int32_t      m_numRegExs  ;
	// make this now use g_collectiondb.m_stringBuf safebuf and
	// make Parms.cpp use that stringbuf rather than store into here...
	SafeBuf   m_regExs           [ MAX_FILTERS ];

	int32_t      m_numRegExs2 ; // useless, just for Parms::setParm()
	float     m_spiderFreqs      [ MAX_FILTERS ];

	int32_t      m_numRegExs3 ; // useless, just for Parms::setParm()
	char      m_spiderPriorities [ MAX_FILTERS ];

	int32_t      m_numRegExs10 ; // useless, just for Parms::setParm()
	int32_t      m_maxSpidersPerRule [ MAX_FILTERS ];

	// same ip waits now here instead of "page priority"
	int32_t      m_numRegExs5 ; // useless, just for Parms::setParm()
	int32_t      m_spiderIpWaits    [ MAX_FILTERS ];
	// same goes for max spiders per ip
	int32_t      m_numRegExs6;
	int32_t      m_spiderIpMaxSpiders [ MAX_FILTERS ];

	int32_t      m_numRegExs8;
	char      m_harvestLinks     [ MAX_FILTERS ];

	int32_t      m_numRegExs7;
	char      m_forceDelete  [ MAX_FILTERS ];

	// dummy?
	int32_t      m_numRegExs9;

	char m_doQueryHighlighting;

	char  m_summaryFrontHighlightTag[SUMMARYHIGHLIGHTTAGMAXSIZE] ;
	char  m_summaryBackHighlightTag [SUMMARYHIGHLIGHTTAGMAXSIZE] ;

	SafeBuf m_htmlRoot;
	SafeBuf m_htmlHead;
	SafeBuf m_htmlTail;

	// match this content-type exactly (txt/html/pdf/doc)
	char  m_filter [ MAX_FILTER_LEN + 1 ];

	int32_t  m_compoundListMaxSize;

	// . related topics control
	// . this can all be overridden by passing in your own cgi parms
	//   for the query request
	int32_t  m_numTopics;           // how many do they want by default?
	int32_t  m_minTopicScore;
	int32_t  m_docsToScanForTopics; // how many to scan by default?
	int32_t  m_maxWordsPerTopic;
	int32_t  m_minDocCount;         // min docs that must contain topic
	char  m_ipRestrict;
	int32_t  m_dedupSamplePercent;
	char  m_topicRemoveOverlaps; // this is generally a good thing
	int32_t  m_topicSampleSize;     // sample about 5k per document
	int32_t  m_topicMaxPunctLen;    // keep it set to 1 for speed

	// SPELL CHECK
	char  m_spellCheck;

	char m_sendingAlertInProgress;

	// . reference pages parameters
	// . copied from Parms.cpp
	int32_t  m_refs_numToGenerate;         // total # wanted by default.
	int32_t  m_refs_numToDisplay;          // how many will be displayed?
	int32_t  m_refs_docsToScan;            // how many to scan by default?
	int32_t  m_refs_minQuality;            // min qual(b4 # links factored in)
	int32_t  m_refs_minLinksPerReference;  // links required to be a reference
	int32_t  m_refs_maxLinkers;            // max number of linkers to process
        float m_refs_additionalTRFetch;
        int32_t  m_refs_numLinksCoefficient;
        int32_t  m_refs_qualityCoefficient;
        int32_t  m_refs_linkDensityCoefficient;
        char  m_refs_multiplyRefScore;
	// reference ceilings parameters
	int32_t  m_refs_numToGenerateCeiling;   
	int32_t  m_refs_docsToScanCeiling;
	int32_t  m_refs_maxLinkersCeiling;
	float m_refs_additionalTRFetchCeiling;

	class SpiderColl *m_spiderColl;

	int32_t m_overflow;
	int32_t m_overflow2;

	HashTableX m_seedHashTable;

	// . related pages parameters
	// . copied from Parms.cpp
        int32_t  m_rp_numToGenerate;
        int32_t  m_rp_numToDisplay;
        int32_t  m_rp_numLinksPerDoc;
        int32_t  m_rp_minQuality;
	int32_t  m_rp_minScore;
        int32_t  m_rp_minLinks;
	int32_t  m_rp_numLinksCoeff;
	int32_t  m_rp_avgLnkrQualCoeff;
	int32_t  m_rp_qualCoeff;
	int32_t  m_rp_srpLinkCoeff;
	int32_t  m_rp_numSummaryLines;
	int32_t  m_rp_titleTruncateLimit;
	char  m_rp_useResultsAsReferences;
	char  m_rp_getExternalPages; // from another cluster?
	char  m_rp_externalColl [MAX_COLL_LEN+1]; //coll in cluster
	// related pages ceiling parameters
	int32_t  m_rp_numToGenerateCeiling;
	int32_t  m_rp_numLinksPerDocCeiling;
	int32_t  m_rp_numSummaryLinesCeiling;
	char  m_rp_doRelatedPageSumHighlight;

	char  m_familyFilter;

	int32_t      	m_tagdbRefreshRate;
	int32_t      	m_linkSamplesToGet;	//was 256
	int32_t    	m_linkQualityDivisor;	//was 8
	int32_t    	m_negPointsPerBannedLink;	// was 1
	int32_t    	m_numSitesOnIpToSample;	//100
	int32_t    	m_negPointsPerBannedSiteOnIp;	// was 1
	int32_t    	m_siteOnIpQualityDivisor;	//was 8
	int32_t    	m_maxPenaltyFromIp;	        //was 30
	int32_t            m_minPagesToEvaluate;

	int32_t 		m_siteQualityBanThreshold;
	int32_t 		m_siteQualityReindexThreshold;
	float 		m_maxSitesPerSecond;
	int32_t            m_linkBonusDivisor;
	int32_t            m_penaltyForLinksToDifferentSiteSameIp;

	// only spider urls due to be spidered in this time range
	int32_t  m_spiderTimeMin;
	int32_t  m_spiderTimeMax;

	int32_t  m_maxAddUrlsPerIpDomPerDay;

	float m_maxKbps;

	// . puts <br>s in the summary to keep its width below this
	// . but we exceed this width before we would split a word
	int32_t m_summaryMaxWidth;

	// how long a robots.txt can be in the cache (Msg13.cpp/Robotdb.cpp)
	int32_t m_maxRobotsCacheAge;

	// collection name in the other/external cluster from which we
	// fetch link information. (gov.gigablast.com)
	char m_getExternalLinkInfo;
	// use hosts2.conf (otherwise uses hosts.conf for import)
	char m_importFromHosts2Conf;
	// should we ask the external collection to RECOMPUTE the link info
	// before giving it to us. we are using this to incorporate new info
	// not yet fully soaked through into gk, for slingshot.
	//char m_getExternalLinkInfoFresh;
	char m_externalColl [ MAX_COLL_LEN + 1 ] ;

	// collection name in the other/external cluster from which we
	// fetch related pages titleRecs. (gov.gigablast.com)
	char m_getExternalRelatedPages;
	char m_externalRelatedPagesColl [ MAX_COLL_LEN + 1 ] ;
	// for importing search results from another cluster
	int32_t  m_numResultsToImport ;
	float m_importWeight;
	int32_t  m_numLinkerWeight;
	int32_t  m_minLinkersPerImportedResult ;
	char  m_importColl [ MAX_COLL_LEN + 1 ];

	char m_classificationMode;

	// use query expansion for this collection?
	char m_queryExpansion;

	// read from cache
	char m_rcache;

	char m_hideAllClustered;
	int32_t m_maxRealTimeInlinks;

	// . cut off age in number of days old 
	// . if pub date is older than this we do not add to datedb
	//   and we remove it from datedb during a datedb merge
	int32_t  m_datedbCutoff;
	// date parsing parms
	int32_t  m_datedbDefaultTimezone;

	// display indexed date, last modified date, datedb (published) date
	char m_displayIndexedDate;
	char m_displayLastModDate;
	char m_displayPublishDate;

        // data feed parms
        char m_useDFAcctServer; 
        int32_t m_dfAcctIp;
        int32_t m_dfAcctPort;

	// number of similar results for cluster by topic
	int32_t m_maxClusterByTopicResults;
	int32_t m_numExtraClusterByTopicResults;

	// enable click 'n' scroll
	char m_clickNScrollEnabled;

	// . do not keep in ruleset, store in title rec at time of parsing
	// . allows admin to quickly and easily change how we parse docs
	int32_t  m_titleWeight;
	int32_t  m_headerWeight;
	int32_t  m_urlPathWeight;
	int32_t  m_externalLinkTextWeight;
	int32_t  m_internalLinkTextWeight;
	int32_t  m_conceptWeight;
	float m_siteNumInlinksBoostBase;
	
	// post query reranking
	int32_t  m_pqr_docsToScan; // also for # docs for language
	float m_pqr_demFactCountry; // demotion for foreign countries
	float m_pqr_demFactQTTopicsInUrl; // demotion factor fewer for query terms or gigabits in the url
	int32_t  m_pqr_maxValQTTopicsInUrl; // max value for fewer query terms or gigabits in the url
	float m_pqr_demFactPaths; // demotion factor for more paths
	int32_t  m_pqr_maxValPaths; // max value for more paths
	float m_pqr_demFactCatidHasSupers; // demotion factor for catids with many super topics
	int32_t  m_pqr_maxValCatidHasSupers; // max value for catids with many super topics
	float m_pqr_demFactPageSize; // demotion factor for higher page sizes
	int32_t  m_pqr_maxValPageSize; // max value for higher page sizes
	float m_pqr_demFactLocTitle; // demotion factor for non-location specific queries with location specific results
	float m_pqr_demFactLocSummary; // demotion factor for non-location specific queries with location specific results
	bool  m_pqr_demInTopics; // true to demote if location is in the gigabits, otherwise these locs won't be demoted
	int32_t  m_pqr_maxValLoc; // max value for non-location specific queries with location specific results
	float m_pqr_demFactNonHtml; // demotion factor for non-html content type
	float m_pqr_demFactXml; // demotion factor for xml content type
	float m_pqr_demFactOthFromHost; // demotion factor for no other pages from same host
	int32_t  m_pqr_maxValOthFromHost; // max value for no other pages from same host
	float m_pqr_demFactDmozCatNmNoQT; // demotion factor for dmoz category names that don't contain a query term
	int32_t  m_pqr_maxValDmozCatNmNoQT; // max value for dmoz category names that don't contain a query term
	float m_pqr_demFactDmozCatNmNoGigabits; // demotion factor for dmoz category names that don't contain a gigabit
	int32_t  m_pqr_maxValDmozCatNmNoGigabits; // max value for dmoz category names that don't contain a gigabit
	float m_pqr_demFactDatedbDate; // demotion for datedb date
	int32_t  m_pqr_minValDatedbDate; // dates earlier than this will be demoted to the max
	int32_t  m_pqr_maxValDatedbDate; // dates later than this will not be demoted
	float m_pqr_demFactProximity; // demotion for proximity of query terms
	int32_t  m_pqr_maxValProximity; // max value for proximity of query terms
	float m_pqr_demFactInSection; // demotion for section of query terms
	int32_t  m_pqr_maxValInSection; // max value for section of query terms
	float m_pqr_demFactOrigScore;

	float m_pqr_demFactSubPhrase;
	float m_pqr_demFactCommonInlinks;

        // lookup table for sitedb filter
	char      m_updateSiteRulesTable;

	// special var to prevent Collectiondb.cpp from copying the crap
	// below here
	char m_END_COPY;

	// use this not m_bases to get the RdbBase
	class RdbBase *getBase ( char rdbId );

	// Rdb.cpp uses this after deleting an RdbBase and adding new one
	void           setBasePtr ( char rdbId , class RdbBase *base ) ;
	class RdbBase *getBasePtr ( char rdbId ) ;

 private:
	// . now chuck this into CollectionRec instead of having a fixed
	//   array of them in Rdb.h called m_bases[]
	// . leave this out of any copy of course
	class RdbBase *m_bases[RDB_END2];

 public:

	// this is basically a cache on timedb, one per collection
	HashTableX m_sortByDateTable;
	// are we currently in the midst of updating the sortbydate table?
	bool m_inProgress;
	// last time we updates m_sortByDateTable (start time of the update)
	uint32_t m_lastUpdateTime; // time_t
	// for poulating the sortbydate table
	class Msg5 *m_msg5;
	key128_t m_timedbStartKey;
	key128_t m_timedbEndKey;
	//RdbList  m_timedbList;

	// used by Parms.cpp
	char m_hackFlag;

	// each Rdb has a tree, so keep the pos/neg key count here so
	// that RdbTree does not have to have its own array limited by
	// MAX_COLLS which we did away with because we made this dynamic.
	int32_t m_numPosKeysInTree[RDB_END2];
	int32_t m_numNegKeysInTree[RDB_END2];

	//int32_t m_numEventsOnHost;

	// do we have the doc:quality var in any url filter?
	//bool      m_hasDocQualityFilter;
	// do we have "isindexed" in any url filter?
	//bool      m_hasIsIndexedKeyword;

	// this means someone re-submitted some new filters or changes, so
	// we need to update "m_hasDocQualityFilter"
	//void updateFilters();

        // company name to use for cached page messages
	//char m_cachedPageName[MAX_NAME_LEN];
};

#endif
