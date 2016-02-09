// Copyright Matt Wells, Apr 2001

// . every host has a config record
// . like tagdb, record in 100% xml
// . allows remote configuration of hosts through Msg4 class
// . remote user sends some xml, we set our member vars using that xml
// . when we save to disk we convert our mem vars to xml
// . is global so everybody can see it
// . conf record can be changed by director OR with the host's priv key
// . use Conf remotely to get setup info about a specific host
// . get your local ip/port/groupMask/etc. from this class not HostMap

#ifndef _CONF_H_
#define _CONF_H_

#include "Xml.h"         // Xml class
#include "File.h"        // File class
#include "ip.h"          // atoip()
#include "Hostdb.h"      // g_hostdb.makeGroupId(),makeGroupMask()
#include "HttpRequest.h"
#include "TcpSocket.h"
#include "Url.h"  // MAX_COLL_LEN
#include "Collectiondb.h"

#define MAX_MASTER_IPS        15
#define MAX_MASTER_PASSWORDS  5

#define USERAGENTMAXSIZE      128

#define PASSWORD_MAX_LEN      12

#define MAX_CONNECT_IPS       128

#define AUTOBAN_TEXT_SIZE     (32*8192)

#define MAX_DNSIPS            16
#define MAX_RNSIPS            13
#define MAX_MX_LEN            128
#define MAX_EMAIL_LEN         64

#define USERS_TEXT_SIZE       500000

#define MAX_GEOCODERS         4

//Publicly accessible and generallyy HA / reachable DNS servers. Use Google's servers - works reasonably well
#define PUBLICLY_AVAILABLE_DNS1 "8.8.8.8"
#define PUBLICLY_AVAILABLE_DNS2 "8.8.4.4"


mode_t getFileCreationFlags();
mode_t getDirCreationFlags ();

class Conf {

  public:

	Conf();

	bool isCollAdmin ( TcpSocket *socket , HttpRequest *hr ) ;
	bool isCollAdminForColl (TcpSocket *sock, HttpRequest *hr,char *coll );
	bool isCollAdmin2 (TcpSocket *socket , HttpRequest *hr,
			   class CollectionRec *cr) ;


	bool isMasterAdmin ( TcpSocket *socket , HttpRequest *hr ) ;
	bool hasMasterPwd ( HttpRequest *hr ) ;
	bool isMasterIp      ( uint32_t ip );
	bool isConnectIp    ( uint32_t ip );

	// loads conf parms from this file "{dir}/gb.conf"
	bool init ( char *dir );

	void setRootIps();

	// set from a buffer of null-terminated xml
	bool add ( char *xml );

	// saves any changes to the conf file
	bool save ( );

	// reset all values to their defaults
	void reset();

	// verify that some values are ok
	bool verify();

	// . get the default collection based on hostname
	//   will look for the hostname in each collection for a match
	//   no match defaults to default collection
	char *getDefaultColl ( char *hostname, int32_t hostnameLen );

	// hold the filename of this conf file
	char        m_confFilename[256];

	// max amount of memory we can use
	int64_t        m_maxMem;

	// if this is false, we do not save, used by dump routines
	// in main.cpp so they can change parms here and not worry about
	// a core dump saving them
	char m_save;

	bool m_runAsDaemon;

	bool m_logToFile;
	
	bool m_isLocal;

	// an additional strip directory on a different drive
	char m_stripeDir[256];

	char m_defaultColl [ MAX_COLL_LEN + 1 ];
	char m_dirColl [ MAX_COLL_LEN + 1];
	char m_dirHost [ MAX_URL_LEN ];

	char m_clusterName[32];

	// . dns parameters
	// . dnsDir should hold our saved cached (TODO: save the dns cache)
	int32_t  m_numDns ;
	int32_t  m_dnsIps[MAX_DNSIPS];
	int16_t m_dnsPorts[MAX_DNSIPS];            

	int32_t  m_dnsMaxCacheMem;
	bool  m_dnsSaveCache;

	int32_t  m_geocoderIps[MAX_GEOCODERS];

	int32_t m_wikiProxyIp;
	int32_t m_wikiProxyPort;

	SafeBuf m_proxyIps;
	SafeBuf m_proxyTestUrl;
	bool    m_useProxyIps;
	bool    m_automaticallyUseProxyIps;
	SafeBuf m_proxyAuth;

	// built-in dns parameters using name servers
	char  m_askRootNameservers;
	int32_t  m_numRns;
	int32_t  m_rnsIps[MAX_RNSIPS];
	int16_t m_rnsPorts[MAX_RNSIPS];

	// used to limit all rdb's to one merge per machine at a time
	int32_t  m_mergeBufSize;

	// tagdb parameters
	int32_t  m_tagdbMaxTreeMem;

	int32_t  m_revdbMaxTreeMem;
	int32_t  m_timedbMaxTreeMem;

	// clusterdb for site clustering, each rec is 16 bytes
	int32_t  m_clusterdbMaxTreeMem; 
	int32_t  m_clusterdbMinFilesToMerge;
	bool  m_clusterdbSaveCache;

	// are we doing a command line thing like 'gb 0 dump s ....' in
	// which case we do not want to log certain things
	bool m_doingCommandLine;

	// linkdb for storing linking relations
	int32_t  m_linkdbMaxTreeMem;
	int32_t  m_linkdbMinFilesToMerge;

	// dup vector cache max mem
	int32_t  m_maxVectorCacheMem;

	int32_t  m_maxCpuThreads;
	int32_t  m_maxCpuMergeThreads;

	int32_t  m_deadHostTimeout;
	int32_t  m_sendEmailTimeout;
	int32_t  m_pingSpacer;

	// the spiderdb holds url records for spidering, when to spider, etc..
	int32_t  m_maxWriteThreads ;
	int32_t  m_spiderMaxDiskThreads    ;

	char m_separateDiskReads;

	int32_t m_statsdbMaxTreeMem;
	int32_t m_statsdbMaxCacheMem;
	bool m_useStatsdb;

	bool  m_spideringEnabled     ;
	bool  m_turkingEnabled     ;
	bool  m_injectionsEnabled     ;
	bool  m_queryingEnabled ;
	bool  m_returnResultsAnyway;
	// qa testing loop going on? uses "test" subdir
	bool  m_testParserEnabled     ;
	bool  m_testSpiderEnabled     ;
	bool  m_testSearchEnabled     ;
	bool  m_addUrlEnabled        ; // TODO: use at http interface level
	bool  m_doStripeBalancing    ;

	// . true if the server is on the production cluster
	// . we enforce the 'elvtune -w 32 /dev/sd?' cmd on all drives because
	//   that yields higher performance when dumping/merging on disk
	bool  m_isLive;
	
	// is this a buzzlogic cluster?
	//bool m_isBuzzLogic;

	// is this a wikipedia cluster?
	bool   m_isWikipedia;

	// for holding robot.txt files for various hostnames
	int32_t  m_robotdbMaxCacheMem  ;
	bool  m_robotdbSaveCache;

	int32_t  m_maxTotalSpiders;

	// indexdb has a max cached age for getting IndexLists (10 mins deflt)
	int32_t  m_indexdbMaxTreeMem   ;
	int32_t  m_indexdbMaxCacheMem;
	//int32_t  m_indexdbMaxDiskPageCacheMem; // for DiskPageCache class only
	int32_t  m_indexdbMaxIndexListAge;
	int32_t  m_indexdbTruncationLimit;
	int32_t  m_indexdbMinFilesToMerge;
	bool  m_indexdbSaveCache;

	int32_t  m_datedbMaxTreeMem   ;
	int32_t  m_datedbMaxCacheMem;
	//int32_t  m_datedbMaxDiskPageCacheMem; // for DiskPageCache class only
	int32_t  m_datedbMaxIndexListAge;
	int32_t  m_datedbTruncationLimit;
	int32_t  m_datedbMinFilesToMerge;
	bool  m_datedbSaveCache;
	// for caching exact quotas in Msg36.cpp

	// used by qa.cpp and Msg13.cpp
	//bool  m_qaBuildMode;

	//int32_t  m_quotaTableMaxMem;

	//bool  m_useBuckets;

	// port of the main udp server
	int16_t m_udpPort;

	// TODO: parse these out!!!!
	//char  m_httpRootDir[256]  ;
	//int16_t m_httpPort           ; now in hosts.conf only
	int32_t  m_httpMaxSockets     ;
	int32_t  m_httpsMaxSockets    ;
	//int32_t  m_httpMaxReadBufSize ;
	int32_t  m_httpMaxSendBufSize ;
	//int32_t  m_httpMaxDownloadSockets ;

	// a search results cache (for Msg40)
	int32_t  m_searchResultsMaxCacheMem    ;
	int32_t  m_searchResultsMaxCacheAge    ; // in seconds
	bool  m_searchResultsSaveCache;

	// a sitelinkinfo cache (for Msg25)
	int32_t  m_siteLinkInfoMaxCacheMem;
	int32_t  m_siteLinkInfoMaxCacheAge;
	bool  m_siteLinkInfoSaveCache;

	// a sitelinkinfo cache (for MsgD)
	int32_t  m_siteQualityMaxCacheMem;
	int32_t  m_siteQualityMaxCacheAge;
	bool  m_siteQualitySaveCache;

	// a sitelinkinfo cache (for Msg25)

	// for downloading an rdb
	//int32_t  m_downloadBufSize; // how big should hosts read buf be?

	// . how many incoming links should we sample?
	// . used for linkText and quality weighting from number of links
	//   and their total base quality
	int32_t  m_maxIncomingLinksToSample;

	// phrase weighting
	float  m_queryPhraseWeight;

	// for Weights.cpp
	int32_t   m_sliderParm;

	//int32_t   m_indexTableIntersectionAlgo;
	// . maxmimum relative weight of a query term (1.0 to inf)
	// . default about 8?
	//float  m_queryMaxMultiplier;

	// use sendmail to forward emails we send out
	char   m_sendmailIp[MAX_MX_LEN];

	// send emails when a host goes down?
	bool   m_sendEmailAlerts;
	//should we delay when only 1 host goes down out of twins till 9 30 am?
	bool   m_delayNonCriticalEmailAlerts;
	//delay emails after
	char   m_delayEmailsAfter[6];
	//delay emails before
	char   m_delayEmailsBefore[6];
	//bool   m_sendEmailAlertsToMattTmobile;
	//bool   m_sendEmailAlertsToMattAlltell;
	//bool   m_sendEmailAlertsToJavier;
	//bool   m_sendEmailAlertsToMelissa;
	//bool   m_sendEmailAlertsToPartap;
	//bool   m_sendEmailAlertsToCinco;
	bool   m_sendEmailAlertsToSysadmin;
	//bool   m_sendEmailAlertsToZak;

	bool   m_sendEmailAlertsToEmail1;
	char   m_email1MX[MAX_MX_LEN]; 
	char   m_email1Addr[MAX_EMAIL_LEN];
	char   m_email1From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail2;
	char   m_email2MX[MAX_MX_LEN];
	char   m_email2Addr[MAX_EMAIL_LEN];
	char   m_email2From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail3;
	char   m_email3MX[MAX_MX_LEN];
	char   m_email3Addr[MAX_EMAIL_LEN];
	char   m_email3From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail4;
	char   m_email4MX[MAX_MX_LEN];
	char   m_email4Addr[MAX_EMAIL_LEN];
	char   m_email4From[MAX_EMAIL_LEN];

	//bool   m_sendEmailAlertsToSabino;

	char   m_errstr1[MAX_URL_LEN];
	char   m_errstr2[MAX_URL_LEN];	
	char   m_errstr3[MAX_URL_LEN];

	char   m_sendParmChangeAlertsToEmail1;
	char   m_sendParmChangeAlertsToEmail2;
	char   m_sendParmChangeAlertsToEmail3;
	char   m_sendParmChangeAlertsToEmail4;

	float m_avgQueryTimeThreshold;
	//float m_maxQueryTime;
	float m_querySuccessThreshold;
	int32_t  m_numQueryTimes;
	int32_t m_maxCorruptLists;

	// limit to how big a serialized query can be before just storing
	// the raw string instead, keeps network traffic down at the expense
	// of processing time, used by Msg serialization
	int32_t  m_maxSerializedQuerySize;

	// the spider won't go if this bandiwdth rate is currently exceeded
	float  m_maxIncomingKbps;

	// max pgs/sec to index and delete from index. guards resources.
	float  m_maxPagesPerSecond;

	float  m_maxLoadAvg;

	// redhat 9's NPTL doesn't like our async signals
	bool   m_allowAsyncSignals;

	bool   m_useCollectionPasswords;

	bool   m_allowCloudUsers;

	// if in read-only mode we do no spidering and load no saved trees
	// so we can use all mem for caching index lists
	bool   m_readOnlyMode;

	// if this is true we use /etc/hosts for hostname lookup before dns
	bool   m_useEtcHosts;

	bool   m_useMergeToken;

	// . should we always read data from local machine if available?
	// . if your network is not gigabit, this may be a good idea
	bool   m_preferLocalReads;

	// should we bypass load balancing and always send titledb record
	// lookup requests to a host to maxmize tfndb page cache hits?
	//bool   m_useBiasedTfndb;

	// calls fsync(fd) if true after each write
	bool   m_flushWrites ; 
	bool   m_verifyWrites;
	int32_t   m_corruptRetries;

	// log unfreed memory on exit
	bool   m_detectMemLeaks;

	// . if false we will not keep spelling information in memory
	// . we will keep the popularity info from dict though, since related
	//   topics requires that
	bool   m_doSpellChecking;

	// are we running in Matt Wells's private data center? if so we
	// use seo tools and control datacenter fans, etc.
	bool   m_isMattWells;

	bool   m_forceIt;

	// maximum number of synonyms/stems to expand a word into
	//int32_t   m_maxSynonyms;

	// default affinity for spelling suggestions/numbers
	//float  m_defaultAffinity;

	// threshold for synonym usage
	//float  m_frequencyThreshold;

	// thesaurus configuration
	//int32_t   m_maxAffinityRequests;
	//int32_t   m_maxAffinityErrors;
	//int32_t   m_maxAffinityAge;
	//int32_t   m_affinityTimeout;
	//char   m_affinityServer[MAX_URL_LEN];
	//char   m_affinityParms[MAX_URL_LEN];

	// new syncing information
	bool   m_syncEnabled;
	bool   m_syncIndexdb;
	bool   m_syncTitledb;
	bool   m_syncSpiderdb;
	bool   m_syncSitedb;
	bool   m_syncLogging;
	bool   m_syncDoUnion;
	bool   m_syncDryRun;
	char   m_syncHostIds [ 256 ]; // restrict syncing to these host ids
	//int32_t   m_syncReadBufSize;     // limit disk activity for syncing
	//int32_t   m_syncSeeksPerSecond;  // limit disk activity for syncing
	int32_t   m_syncBytesPerSecond;  // limit disk activity for syncing

	// if this is true we do not add indexdb keys that *should* already
	// be in indexdb. but if you recently upped the m_truncationLimit
	// then you can set this to false to add all indexdb keys.
	//bool   m_onlyAddUnchangedTermIds;
	bool   m_doIncrementalUpdating;

	// always true unless entire indexdb was deleted and we are rebuilding
	bool   m_indexDeletes;

	bool   m_splitTwins;
	bool   m_useThreads;

	bool   m_useThreadsForDisk;
	bool   m_useThreadsForIndexOps;
	bool   m_useThreadsForSystemCalls;

	bool   m_useSHM;
	bool   m_useQuickpoll;

	int64_t m_posdbFileCacheSize;
	int64_t m_tagdbFileCacheSize;
	int64_t m_clusterdbFileCacheSize;
	int64_t m_titledbFileCacheSize;
	int64_t m_spiderdbFileCacheSize;

	//bool   m_quickpollCoreOnError;
	bool   m_useShotgun;
	bool   m_testMem;
	bool   m_doConsistencyTesting;

	// temporary hack for fixing docid collision resolution bug
	bool   m_hackFixWords;
	bool   m_hackFixPhrases;

	// flags for excluding docs with only linktext or meta text matches
	// for one or more query terms
	//bool   m_excludeLinkText;
	//bool   m_excludeMetaText;

	// deny robots access to the search results
	//bool   m_robotCheck;

	// scan all titledb files if we can't find the rec where it should be
	bool   m_scanAllIfNotFound;
	
	// defaults to "Gigabot/1.0"
	char m_spiderUserAgent [ USERAGENTMAXSIZE ];

	char m_spiderBotName [ USERAGENTMAXSIZE ];

	int32_t m_autoSaveFrequency;

	int32_t m_docCountAdjustment;

	bool m_profilingEnabled;
	bool m_dynamicPerfGraph;
	int32_t m_minProfThreshold;
	bool m_sequentialProfiling;
	int32_t m_realTimeProfilerMinQuickPollDelta;

	//int32_t m_summaryMode;	// JAB: moved to CollectionRec

	// . for query-dependent summary/title generation
	//int32_t  m_titleMaxLen;
	//int32_t  m_summaryMaxLen;
	//int32_t  m_summaryMaxNumLines;
	//int32_t  m_summaryMaxNumCharsPerLine;
	//int32_t  m_summaryDefaultNumLines;
	//char  m_summaryFrontHighlightTag[128];
	//char  m_summaryBackHighlightTag [128];


	//
	// See Log.h for an explanation of the switches below
	//

	// GET and POST requests.
	bool  m_logHttpRequests;
	bool  m_logAutobannedQueries;
	//bool  m_logQueryTimes;
	// if query took this or more milliseconds, log its time
	int32_t  m_logQueryTimeThreshold;
	bool  m_logQueryReply;
	bool  m_logQueryDebug;
	// log what gets into the index
	bool  m_logSpideredUrls;
	// log informational messages, they are not indicative of any error.
	bool  m_logInfo;
	// when out of udp slots
	bool  m_logNetCongestion;
	// doc quota limits, url truncation limits
	bool  m_logLimits;
	// log debug switches
	bool  m_logDebugAddurl  ;
	bool  m_logDebugAdmin   ;
	bool  m_logDebugBuild   ;
	bool  m_logDebugBuildTime ;
	bool  m_logDebugDb      ;
	bool  m_logDebugDirty   ;
	bool  m_logDebugDisk    ;
	bool  m_logDebugDiskPageCache;
	bool  m_logDebugDns     ;
	bool  m_logDebugDownloads;
	bool  m_logDebugFacebook;
	bool  m_logDebugHttp    ;
	bool  m_logDebugImage   ;
	bool  m_logDebugLoop    ;
	bool  m_logDebugLang    ;
	bool  m_logDebugLinkInfo ;
	bool  m_logDebugMem     ;
	bool  m_logDebugMemUsage;
	bool  m_logDebugMerge   ;
	bool  m_logDebugNet     ;
	bool  m_logDebugPQR     ; // post query rerank
	bool  m_logDebugProxies ;
	bool  m_logDebugQuery   ;
	bool  m_logDebugQuota   ;
	bool  m_logDebugRobots	;
	bool  m_logDebugSpcache ; // SpiderCache.cpp debug
	//bool  m_logDebugSpiderWait;
	bool  m_logDebugSpeller ;
	bool  m_logDebugTagdb   ;
	bool  m_logDebugSections;
	bool  m_logDebugSEO;
	bool  m_logDebugSEOInserts;
	bool  m_logDebugStats   ;
	bool  m_logDebugSummary ;
	bool  m_logDebugSpider  ;
	bool  m_logDebugMsg13   ;
	bool  m_logDebugUrlAttempts ;
	bool  m_logDebugTcp     ;
	bool  m_logDebugTcpBuf  ;
	bool  m_logDebugThread  ;
	bool  m_logDebugTimedb  ;
	bool  m_logDebugTitle   ;
	bool  m_logDebugTopics  ;
	bool  m_logDebugTopDocs ;
	bool  m_logDebugUdp     ;
	bool  m_logDebugUnicode ;
	bool  m_logDebugRepair  ;
	bool  m_logDebugDate    ;
	bool  m_logDebugDetailed;
	bool  m_logTraceRdbBase;
	bool  m_logTraceRdbMap;
	bool  m_logTraceBigFile;
	bool  m_logTraceRepairs;
	bool  m_logTraceSpider;
	bool m_logTraceMsg;
	// expensive timing messages
	bool m_logTimingAddurl  ;
	bool m_logTimingAdmin   ;
	bool m_logTimingBuild;
	bool m_logTimingDb;
	bool m_logTimingNet;
	bool m_logTimingQuery;
	bool m_logTimingSpcache;
	bool m_logTimingTopics;
	// programmer reminders.
	bool m_logReminders;

	//int32_t m_numMasterPwds;
	//char m_masterPwds[MAX_MASTER_PASSWORDS][PASSWORD_MAX_LEN];
	SafeBuf m_masterPwds;

	//int32_t m_numMasterIps;
	//int32_t m_masterIps[MAX_MASTER_IPS];

	// these are the new master ips
	//int32_t  m_numConnectIps;
	//int32_t  m_connectIps [ MAX_CONNECT_IPS ];
	SafeBuf m_connectIps;

	// should we generate similarity/content vector for titleRecs lacking?
	// this takes a ~100+ ms, very expensive, so it is just meant for
	// testing.
	bool m_generateVectorAtQueryTime;

	char m_redirect[MAX_URL_LEN];
        char m_useCompressionProxy;
        char m_gzipDownloads;

	// used by proxy to make proxy point to the temp cluster while
	// the original cluster is updated
        char m_useTmpCluster;

        char m_timeSyncProxy;
        // For remote datafeed verification
	//char m_useDFAcctServer; 
        //int32_t m_dfAcctIp;
        //int32_t m_dfAcctPort;
        //char m_dfAcctColl[MAX_COLL_LEN];

	Xml   m_xml;
	char  m_buf[10*1024];
	int32_t  m_bufSize;

	// . for specifying if this is an interface machine
	//   messages are rerouted from this machine to the main
	//   cluster set in the hosts.conf.
	bool m_interfaceMachine;

	// after we take the natural log of each query term's DF (doc freq.)
	// we 
	float m_queryExp;
	//char  m_useDynamicPhraseWeighting;

	float m_minPopForSpeller; // 0% to 100%

	// allow scaling up of hosts by removing recs not in the correct
	// group. otherwise a sanity check will happen.
	char  m_allowScale;
	// . timeout on dead hosts, only set when we know a host is dead and
	//   will not come back online.  Messages will timeout on the dead
	//   host, but not error, allowing outstanding spidering to finish
	//   to the twin
	char  m_giveupOnDeadHosts;
	char  m_bypassValidation;

	int32_t  m_maxHardDriveTemp;

	int32_t  m_maxHeartbeatDelay;
	int32_t  m_maxCallbackDelay;

	// balance value for Msg6, each host can have this many ready domains
	// per global host
	//int32_t m_distributedSpiderBalance;
	//int32_t m_distributedIpWait;

	// parameters for indexdb spitting and tfndb extension bits
	//int32_t  m_indexdbSplit;
	//char  m_fullSplit;
	//char  m_legacyIndexdbSplit;
	//int32_t  m_tfndbExtBits;

	// used by Repair.cpp
	char  m_repairingEnabled  ;
	int32_t  m_maxRepairSpiders  ;
	int64_t  m_repairMem;
	SafeBuf m_collsToRepair;
	char  m_rebuildAllCollections;
	char  m_fullRebuild       ;
	char  m_rebuildAddOutlinks;
	char  m_rebuildRecycleLinkInfo  ;
	//char  m_rebuildRecycleLinkInfo2 ;
	//char  m_removeBadPages    ;
	char  m_rebuildTitledb    ;
	//char  m_rebuildTfndb      ;
	//char  m_rebuildIndexdb    ;
	char  m_rebuildPosdb    ;
	//char  m_rebuildNoSplits   ;
	//char  m_rebuildDatedb     ;
	char  m_rebuildClusterdb  ;
	char  m_rebuildSpiderdb   ;
	//char  m_rebuildSitedb     ;
	char  m_rebuildLinkdb     ;
	//char  m_rebuildTagdb      ;
	//char  m_rebuildPlacedb    ;
	char  m_rebuildTimedb     ;
	char  m_rebuildSectiondb  ;
	//char  m_rebuildRevdb      ;
	char  m_rebuildRoots      ;
	char  m_rebuildNonRoots   ;

	//char  m_rebuildSkipSitedbLookup ;

	// for caching the qualities of urls (see Msg20.cpp)
	int32_t  m_maxQualityCacheAge ;
};

extern class Conf g_conf;

#endif

// old stuff:
// key is the hostId. hostId of -1 is the default conf record.
// here's the recognized fields:
// <dirPubKey>    // default rec only
// <groupMask>    // default rec only
// <rootDir>      // default rec only
// <numPolice>    // default rec only
// <isTrustedNet> // default rec only
// <hostId> -- stored in hostmap
// <ip>     -- stored in hostmap
// <port>   -- stored in hostmap
// <networkName>  // also in default rec
// <maxDiskSpace>
// <maxMem>
// <maxCpu>
// <maxBps>
// <pubKey>
// <isTrustedHost>  // director sealed
