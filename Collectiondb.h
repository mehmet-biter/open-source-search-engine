// Matt Wells, copyright Feb 2001

// maintains a simple array of CollectionRecs

#ifndef GB_COLLECTIONDB_H
#define GB_COLLECTIONDB_H

#include "SafeBuf.h"
#include "rdbid_t.h"
#include "GbMutex.h"


class Collectiondb  {

 public:
	Collectiondb();
	~Collectiondb();

	void reset() ;
	
	// called by main.cpp to fill in our m_recs[] array with
	// all the coll.*.*/coll.conf info
	bool loadAllCollRecs ( );

	// . this will save all conf files back to disk that need it
	// . returns false and sets g_errno on error, true on success
	bool save ( );

	bool isInitializing() const { return m_initializing; }

	// returns i so that m_recs[i].m_coll = coll
	collnum_t getCollnum(const char *coll, int32_t collLen) const;
	collnum_t getCollnum(const char *coll) const; // coll is NULL terminated here

	const char *getCollName(collnum_t collnum) const;

	// get coll rec specified in the HTTP request
	class CollectionRec *getRec ( class HttpRequest *r ,
				      bool useDefaultRec = true );

	// do not support diffbot style token/name style for this one:
	const char *getDefaultColl ( HttpRequest *r ) ;

	// . get collectionRec from name
	// returns NULL if not available
	class CollectionRec *getRec ( const char *coll );

	class CollectionRec *getRec ( const char *coll , int32_t collLen );

	class CollectionRec *getRec ( collnum_t collnum);

	//class CollectionRec *getDefaultRec ( ) ;

	class CollectionRec *getFirstRec      ( ) ;
	const char          *getFirstCollName() const;
	collnum_t            getFirstCollnum() const ;

	int32_t getNumRecs() const { return m_numRecs; }
	int32_t getNumRecsUsed() const { return m_numRecsUsed; }

	// what collnum will be used the next time a coll is added?
	collnum_t reserveCollNum ( ) ;

	bool addExistingColl ( const char *coll, collnum_t collnum );

	bool addNewColl( const char *coll, bool saveIt, collnum_t newCollnum ) ;

	bool addRdbBaseToAllRdbsForEachCollRec ( ) ;
	bool addRdbBasesForCollRec ( CollectionRec *cr ) ;

	// returns false if blocked, true otherwise.
	bool deleteRec2 ( collnum_t collnum );

	//void deleteSpiderColl ( class SpiderColl *sc );

	// returns false if blocked, true otherwise.
	bool resetColl2 ( collnum_t oldCollnum,
			  collnum_t newCollnum,
			  bool purgeSeeds );

	//used by main.cpp for injecting files
	void hackCollectionForInjection(CollectionRec *cr);

private:
	// after main.cpp loads all rdb trees it calls this to remove
	// bogus collnums from the trees i guess
	bool cleanTrees();

	bool registerCollRec(CollectionRec *cr);

	bool growRecPtrBuf(collnum_t collnum);
	bool setRecPtr(collnum_t collnum, CollectionRec *cr);

	bool m_needsSave;

	class CollectionRec  **m_recs;

	// m_recs[] points into a safebuf that is just an array
	// of collectionrec ptrs. so we have to grow that safebuf possibly
	// in order to add a new collection rec ptr to m_recs
	SafeBuf m_recPtrBuf;

	int32_t            m_numRecs;
	int32_t            m_numRecsUsed;

	int32_t m_wrapped;

	int32_t m_numCollsSwappedOut;

	bool m_initializing;
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
// max chars the executable path+name can be
#define MAX_FILTER_LEN 64

#define SUMMARYHIGHLIGHTTAGMAXSIZE 128

#include "max_coll_len.h"
#include "HashTableX.h"

// fake this for now
#define RDB_END2 80


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

	char m_reserved1;
	int32_t m_reserved2;

	// keep separate because when we receive a crawlinfo struct from
	// a host we only add these in if it matches our round #
	int64_t m_pageDownloadSuccessesThisRound;
	int64_t m_pageProcessSuccessesThisRound;


	void reset() { memset ( this , 0 , sizeof(CrawlInfo) ); }
} __attribute__((packed, aligned(4)));


class CollectionRec {

 public:

	// active linked list of collectionrecs used by spider.cpp
	class CollectionRec *m_nextActive;

	// these just set m_xml to NULL
	CollectionRec();
	virtual ~CollectionRec();

	int64_t getNumDocsIndexed();

	// messes with m_spiderColl->m_sendLocalCrawlInfoToHost[MAX_HOSTS]
	// so we do not have to keep sending this huge msg!
	bool shouldSendLocalCrawlInfoToHost ( int32_t hostId );
	void sentLocalCrawlInfoToHost ( int32_t hostId );
	void localCrawlInfoUpdate();

	// . stuff used by Collectiondb
	// . do we need a save or not?
	bool      save ();
	bool      m_needsSave;

	bool      load ( const char *coll , int32_t collNum ) ;
	void reset();

	// Clear memory structures used by URL filters
	void clearUrlFilters();

	// for customcrawls
	bool rebuildUrlFilters();

	// for regular crawls
	bool rebuildUrlFilters2();

	bool rebuildLangRules( const char *lang , const char *tld );

	bool rebuildPrivacoreRules();
	bool rebuildShallowRules();

	bool m_urlFiltersHavePageCounts;

	// the all important collection name, NULL terminated
	char  m_coll [ MAX_COLL_LEN + 1 ] ;
	int32_t  m_collLen;

	// used by SpiderCache.cpp. g_collectiondb.m_recs[m_collnum] = this
	collnum_t m_collnum;

	// for doing DailyMerge.cpp stuff
	int32_t m_dailyMergeStarted; // time_t
	int32_t m_dailyMergeTrigger;

	char m_dailyMergeDOWList[48];

	int32_t m_treeCount;

	int64_t m_spiderCorruptCount;

	// holds ips that have been detected as being throttled and we need
	// to backoff and use proxies on
	HashTableX m_twitchyTable;

	// spider controls for this collection
	bool m_spideringEnabled ;
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
	char  m_doLinkSpamCheck         ; //filters dynamically generated pages
	char  m_tagdbColl [MAX_COLL_LEN+1]; // coll to use for tagdb lookups
	char  m_delete404s              ;
	char  m_siteClusterByDefault    ;
	char  m_doIpLookups             ; // considered iff using proxy
	bool  m_useRobotsTxt            ;
	char  m_obeyRelNoFollowLinks    ;
	char  m_forceUseFloaters        ;
	char  m_automaticallyUseProxies ;
	char  m_automaticallyBackOff    ;
	char  m_recycleContent          ;
	char  m_getLinkInfo             ; // turn off to save seeks
	char  m_computeSiteNumInlinks   ;
	char  m_indexInlinkNeighborhoods;
	char  m_removeBannedPages       ;

	int32_t  m_percentSimilarSummary       ; // Dedup by summary similiarity
	int32_t  m_summDedupNumLines           ;

	int32_t  m_maxQueryTerms;

	char  m_spiderStatus;

	//ranking settings
	float m_sameLangWeight;

	// Language stuff
	char 			m_defaultSortLanguage2[6];

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
	int32_t m_linkdbMinFilesToMerge ;
	int32_t m_tagdbMinFilesToMerge ;
	int32_t m_spiderdbMinFilesToMerge;

	char  m_dedupResultsByDefault   ;
	char  m_doTagdbLookups        ;
	char  m_deleteTimeouts          ; // can delete docs that time out?
	char  m_allowAdultDocs          ;
	char  m_useCanonicalRedirects   ;

	int32_t  m_maxNumSpiders             ; // per local spider host

	int32_t m_lastResetCount;

	// start another set of flags using the old m_spiderTimeShift
	char  m_useCurrentTime          ; // ... for m_spiderTime2

	// controls for query-dependent summary/title generation
	int32_t m_titleMaxLen;
	int32_t m_summaryMaxLen;
	int32_t m_summaryMaxNumLines;
	int32_t m_summaryMaxNumCharsPerLine;

	bool m_getDocIdScoringInfo;

  /*****
   * !! Start Diffbot paramamters !! *
   *****/

	SafeBuf m_diffbotSeeds;

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

	int32_t      m_numRegExs;
	// make this now use g_collectiondb.m_stringBuf safebuf and
	// make Parms.cpp use that stringbuf rather than store into here...
	SafeBuf		m_regExs[ MAX_FILTERS ];

	int32_t		m_numSpiderFreqs;	// useless, just for Parms::setParm()
	float		m_spiderFreqs[ MAX_FILTERS ];

	int32_t		m_numSpiderPriorities;	// useless, just for Parms::setParm()
	char		m_spiderPriorities[ MAX_FILTERS ];

	int32_t		m_numMaxSpidersPerRule;	// useless, just for Parms::setParm()
	int32_t		m_maxSpidersPerRule[ MAX_FILTERS ];

	// same ip waits now here instead of "page priority"
	int32_t		m_numSpiderIpWaits;	// useless, just for Parms::setParm()
	int32_t		m_spiderIpWaits[ MAX_FILTERS ];

	// same goes for max spiders per ip
	int32_t		m_numSpiderIpMaxSpiders;
	int32_t		m_spiderIpMaxSpiders [ MAX_FILTERS ];

	int32_t		m_numHarvestLinks;
	bool		m_harvestLinks[ MAX_FILTERS ];

	int32_t		m_numForceDelete;
	char		m_forceDelete[ MAX_FILTERS ];

	// dummy?
	int32_t      m_numRegExs9;

	char m_doQueryHighlighting;

	char  m_summaryFrontHighlightTag[SUMMARYHIGHLIGHTTAGMAXSIZE] ;
	char  m_summaryBackHighlightTag [SUMMARYHIGHLIGHTTAGMAXSIZE] ;

	SafeBuf m_htmlRoot;
	SafeBuf m_htmlHead;
	SafeBuf m_htmlTail;

	// SPELL CHECK
	bool  m_spellCheck;

	class SpiderColl *m_spiderColl;
	GbMutex m_spiderCollMutex;

	int32_t m_overflow;
	int32_t m_overflow2;

	HashTableX m_seedHashTable;

	int32_t  m_maxAddUrlsPerIpDomPerDay;

	// . max content length of text/html or text/plain document
	// . we will not download, index or store more than this many bytes
	int32_t  m_maxTextDocLen;

	// . max content length of other (pdf, word, xls, ppt, ps)
	// . we will not download, index or store more than this many bytes
	// . if content would be truncated, we will not even download at all
	//   because the html converter needs 100% of the doc otherwise it
	//   will have an error
	int32_t  m_maxOtherDocLen;

	// . puts <br>s in the summary to keep its width below this
	// . but we exceed this width before we would split a word
	int32_t m_summaryMaxWidth;

	// how long a robots.txt can be in the cache (Msg13.cpp/Robotdb.cpp)
	int32_t m_maxRobotsCacheAge;

	int32_t m_crawlDelayDefaultForNoRobotsTxtMS;
	int32_t m_crawlDelayDefaultForRobotsTxtMS;


	// use query expansion for this collection?
	bool m_queryExpansion;

	// read from cache
	bool m_rcache;

	bool m_hideAllClustered;

	// special var to prevent Collectiondb.cpp from copying the crap
	// below here
	char m_END_COPY;

	// use this not m_bases to get the RdbBase
	class RdbBase *getBase(rdbid_t rdbId);

	// Rdb.cpp uses this after deleting an RdbBase and adding new one
	void           setBasePtr(rdbid_t rdbId, class RdbBase *base);
	class RdbBase *getBasePtr(rdbid_t rdbId);

 private:
	// . now chuck this into CollectionRec instead of having a fixed
	//   array of them in Rdb.h called m_bases[]
	// . leave this out of any copy of course
	class RdbBase *m_bases[RDB_END2];

 public:
	// for poulating the sortbydate table
	class Msg5 *m_msg5;

	// used by Parms.cpp
	char m_hackFlag;

	// each Rdb has a tree, so keep the pos/neg key count here so
	// that RdbTree does not have to have its own array limited by
	// MAX_COLLS which we did away with because we made this dynamic.
	int32_t m_numPosKeysInTree[RDB_END2];
	int32_t m_numNegKeysInTree[RDB_END2];
};

#endif // GB_COLLECTIONDB_H
