#include "gb-include.h"

#include "Conf.h"
#include "Parms.h"
#include "File.h"
#include "Proxy.h"
#include "Msg3a.h" // MAX_SHARDS
#include "Collectiondb.h"
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "Process.h"
#include "Mem.h"
#include "ip.h"
#include <sys/stat.h> //umask()


Conf g_conf;

static bool s_setUmask = false;

mode_t getFileCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}

	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH ;
}

mode_t getDirCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}

	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IXUSR | S_IXGRP;
}

Conf::Conf ( ) {
	m_save = true;
	m_doingCommandLine = false;

	// set max mem to 16GB at least until we load on disk
	m_maxMem = 16000000000;

	// Coverity.. a bit overkill, but wth...
	m_runAsDaemon = false;
	m_logToFile = false;
	m_isLocal = false;
	memset(m_defaultColl, 0, sizeof(m_defaultColl));
	memset(m_clusterName, 0, sizeof(m_clusterName));
	m_numDns = 0;
	memset(m_dnsIps, 0, sizeof(m_dnsIps));
	memset(m_dnsPorts, 0, sizeof(m_dnsPorts));
	m_dnsMaxCacheMem = 0;
	m_useProxyIps = false;
	m_automaticallyUseProxyIps = false;
	m_askRootNameservers = false;
	m_numRns = 0;
	memset(m_rnsIps, 0, sizeof(m_rnsIps));
	m_urlClassificationServerName[0] = '\0';
	m_urlClassificationServerPort = 0;
	m_maxOutstandingUrlClassifications = 0;
	m_urlClassificationTimeout = 0;
	m_mergeBufSize = 0;
	m_posdbFileCacheSize = 0;
	m_posdbMaxTreeMem = 0;
	m_tagdbFileCacheSize = 0;
	m_tagdbMaxTreeMem = 0;
	m_mergespaceLockDirectory[0] = '\0';
	m_mergespaceMinLockFiles = 0;
	m_mergespaceDirectory[0] = '\0';
	m_clusterdbFileCacheSize = 0;
	m_clusterdbMaxTreeMem = 0;
	m_clusterdbMinFilesToMerge = 0;
	m_titledbFileCacheSize = 0;
	m_titledbMaxTreeMem = 0;
	m_spiderdbFileCacheSize = 0;
	m_spiderdbMaxTreeMem = 0;
	m_linkdbMaxTreeMem = 0;
	m_linkdbMinFilesToMerge = 0;
	m_statsdbMaxTreeMem = 0;
	m_useStatsdb = false;
	m_maxCpuThreads = 0;
	m_maxIOThreads = 0;
	m_maxExternalThreads = 0;
	m_maxJobCleanupTime = 0;
	m_deadHostTimeout = 0;
	m_sendEmailTimeout = 0;
	m_pingSpacer = 0;
	m_maxDocsWanted = 0;
	m_maxFirstResultNum = 0;
	min_docid_splits = 0;
	max_docid_splits = 0;
	m_msg40_msg39_timeout = 0;
	m_msg3a_msg39_network_overhead = 0;
	m_useHighFrequencyTermCache = false;
	m_spideringEnabled = false;
	m_injectionsEnabled = false;
	m_queryingEnabled = false;
	m_returnResultsAnyway = false;
	m_addUrlEnabled = false;
	m_doStripeBalancing = false;
	m_isLive = false;
	m_maxTotalSpiders = 0;
	m_indexdbMaxIndexListAge = 0;
	m_httpMaxSockets = 0;
	m_httpsMaxSockets = 0;
	m_httpMaxSendBufSize = 0;
	m_docSummaryWithDescriptionMaxCacheAge = 0;
	m_sliderParm = 0;
	m_termFreqWeightFreqMin = 0.0;
	m_termFreqWeightFreqMax = 0.0;
	m_termFreqWeightMin = 0.0;
	m_termFreqWeightMax = 0.0;
	m_densityWeightMin = 0.0;
	m_densityWeightMax = 0.0;
	m_diversityWeightMin = 0.0;
	m_diversityWeightMax = 0.0;
	m_hashGroupWeightBody = 0.0;
	m_hashGroupWeightTitle = 0.0;
	m_hashGroupWeightHeading = 0.0;
	m_hashGroupWeightInlist = 0.0;
	m_hashGroupWeightInMetaTag = 0.0;
	m_hashGroupWeightInLinkText = 0.0;
	m_hashGroupWeightInTag = 0.0;
	m_hashGroupWeightNeighborhood = 0.0;
	m_hashGroupWeightInternalLinkText = 0.0;
	m_hashGroupWeightInUrl = 0.0;
	m_hashGroupWeightInMenu = 0.0;
	m_synonymWeight = 0.0;
	m_usePageTemperatureForRanking = true;
	m_numFlagScoreMultipliers = 26;
	m_numFlagRankAdjustments = 26;
	for(int i=0; i<26; i++) {
		m_flagScoreMultiplier[i] = 1.0;
		m_flagRankAdjustment[i] = 0;
	}
	m_sendEmailAlerts = false;
	m_delayNonCriticalEmailAlerts = false;
	m_sendEmailAlertsToSysadmin = false;
	m_sendEmailAlertsToEmail1 = false;
	memset(m_email1MX, 0, sizeof(m_email1MX));
	memset(m_email1Addr, 0, sizeof(m_email1Addr));
	memset(m_email1From, 0, sizeof(m_email1From));
	m_sendEmailAlertsToEmail2 = false;
	memset(m_email2MX, 0, sizeof(m_email2MX));
	memset(m_email2Addr, 0, sizeof(m_email2Addr));
	memset(m_email2From, 0, sizeof(m_email2From));
	m_sendEmailAlertsToEmail3 = false;
	memset(m_email3MX, 0, sizeof(m_email3MX));
	memset(m_email3Addr, 0, sizeof(m_email3Addr));
	memset(m_email3From, 0, sizeof(m_email3From));
	m_sendEmailAlertsToEmail4 = false;
	memset(m_email4MX, 0, sizeof(m_email4MX));
	memset(m_email4Addr, 0, sizeof(m_email4Addr));
	memset(m_email4From, 0, sizeof(m_email4From));
	m_sendParmChangeAlertsToEmail1 = false;
	m_sendParmChangeAlertsToEmail2 = false;
	m_sendParmChangeAlertsToEmail3 = false;
	m_sendParmChangeAlertsToEmail4 = false;
	m_avgQueryTimeThreshold = 0.0;
	m_querySuccessThreshold = 0.0;
	m_numQueryTimes = 0;
	m_maxCorruptLists = 0;
	m_defaultQueryResultsValidityTime = 0;
	m_useCollectionPasswords = false;
	m_allowCloudUsers = false;
	m_readOnlyMode = false;
	m_useEtcHosts = false;
	m_verifyTreeIntegrity = false;
	m_verifyDumpedLists = false;
	m_flushWrites = false;
	m_verifyWrites = false;
	m_corruptRetries = 0;
	m_detectMemLeaks = false;
	m_doSpellChecking = false;
	m_forceIt = false;
	m_doIncrementalUpdating = false;
	m_useQuickpoll = false;
	m_stableSummaryCacheSize = 0;
	m_stableSummaryCacheMaxAge = 0;
	m_unstableSummaryCacheSize = 0;
	m_unstableSummaryCacheMaxAge = 0;
	m_tagRecCacheSize = 0;
	m_tagRecCacheMaxAge = 0;
	m_useShotgun = false;
	m_testMem = false;
	m_doConsistencyTesting = false;
	memset(m_spiderUserAgent, 0, sizeof(m_spiderUserAgent));
	memset(m_spiderBotName, 0, sizeof(m_spiderBotName));
	m_autoSaveFrequency = 0;
	m_docCountAdjustment = 0;
	m_profilingEnabled = false;
	m_dynamicPerfGraph = false;
	m_minProfThreshold = 0;
	m_sequentialProfiling = false;
	m_realTimeProfilerMinQuickPollDelta = 0;
	m_logHttpRequests = false;
	m_logAutobannedQueries = false;
	m_logQueryTimeThreshold = 0;
	m_logDiskReadTimeThreshold = 0;
	m_logQueryReply = false;
	m_logSpideredUrls = false;
	m_logInfo = false;
	m_logNetCongestion = false;
	m_logLimits = false;
	m_logDebugAddurl = false;
	m_logDebugAdmin = false;
	m_logDebugBuild = false;
	m_logDebugBuildTime = false;
	m_logDebugDb = false;
	m_logDebugDirty = false;
	m_logDebugDisk = false;
	m_logDebugDns = false;
	m_logDebugDownloads = false;
	m_logDebugHttp = false;
	m_logDebugImage = false;
	m_logDebugLoop = false;
	m_logDebugLang = false;
	m_logDebugLinkInfo = false;
	m_logDebugMem = false;
	m_logDebugMemUsage = false;
	m_logDebugMerge = false;
	m_logDebugNet = false;
	m_logDebugProxies = false;
	m_logDebugQuery = false;
	m_logDebugQuota = false;
	m_logDebugRobots = false;
	m_logDebugSpcache = false;
	m_logDebugSpeller = false;
	m_logDebugTagdb = false;
	m_logDebugSections = false;
	m_logDebugSEO = false;
	m_logDebugStats = false;
	m_logDebugSummary = false;
	m_logDebugSpider = false;
	m_logDebugMsg13 = false;
	m_logDebugUrlAttempts = false;
	m_logDebugTcp = false;
	m_logDebugTcpBuf = false;
	m_logDebugThread = false;
	m_logDebugTitle = false;
	m_logDebugTopDocs = false;
	m_logDebugUdp = false;
	m_logDebugUnicode = false;
	m_logDebugRepair = false;
	m_logDebugDate = false;
	m_logDebugDetailed = false;
	m_logTraceBigFile = false;
	m_logTraceDns = false;
	m_logTraceFile = false;
	m_logTraceHttpMime = false;
	m_logTraceMem = false;
	m_logTraceMsg0 = false;
	m_logTraceMsg4 = false;
	m_logTracePos = false;
	m_logTracePosdb = false;
	m_logTraceRdb = false;
	m_logTraceRdbBase = false;
	m_logTraceRdbBuckets = false;
	m_logTraceRdbDump = false;
	m_logTraceRdbIndex = false;
	m_logTraceRdbList = false;
	m_logTraceRdbMap = false;
	m_logTraceRdbTree = false;
	m_logTraceRepairs = false;
	m_logTraceRobots = false;
	m_logTraceSpider = false;
	m_logTraceSummary = false;
	m_logTraceXmlDoc = false;
	m_logTracePhrases= false;
	m_logTraceUrlBlockList = false;
	m_logTraceWordSpam=false;
	m_logTraceUrlClassification = false;
	m_logTimingAddurl = false;
	m_logTimingAdmin = false;
	m_logTimingBuild = false;
	m_logTimingDb = false;
	m_logTimingNet = false;
	m_logTimingQuery = false;
	m_logTimingSpcache = false;
	m_logTimingRobots = false;
	m_logReminders = false;
	m_generateVectorAtQueryTime = false;
	memset(m_redirect, 0, sizeof(m_redirect));
	m_useCompressionProxy = false;
	m_gzipDownloads = false;
	m_useTmpCluster = false;
	m_interfaceMachine = false;
	m_allowScale = true;
	m_giveupOnDeadHosts = false;
	m_bypassValidation = false;
	m_maxCallbackDelay = 0;
	m_repairingEnabled = false;
	m_maxRepairinjections = 0;
	m_rebuildHost = -1;
	m_repairMem = 0;
	m_fullRebuild = true;
	m_rebuildAddOutlinks = false;
	m_rebuildRecycleLinkInfo = true;
	m_rebuildTitledb = false;
	m_rebuildPosdb = false;
	m_rebuildClusterdb = false;
	m_rebuildSpiderdb = false;
	m_rebuildLinkdb = false;
	m_rebuildRoots = true;
	m_rebuildNonRoots = true;
}

static bool isInWhiteSpaceList ( const char *p , const char *buf ) {
	if ( ! p ) return false;

	const char *match = strstr ( buf , p );
	if ( ! match ) return false;
	
	int32_t len = strlen(p);

	// ensure book-ended by whitespace
	if (  match && 
	      (match == buf || is_wspace_a(match[-1])) &&
	      (!match[len] || is_wspace_a(match[len])) )
		return true;

	// no match
	return false;
}

bool Conf::isCollAdmin ( TcpSocket *sock , HttpRequest *hr ) {
	// master always does
	if ( isMasterAdmin ( sock , hr ) ) return true;

	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );

}

bool Conf::isCollAdminForColl ( TcpSocket *sock, HttpRequest *hr, const char *coll ) {
	CollectionRec *cr = g_collectiondb.getRec ( coll );

	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );
}

bool Conf::isCollAdmin2 ( TcpSocket *sock, HttpRequest *hr, CollectionRec *cr ) {
	if ( ! cr ) return false;

	// never for main! must be root!
	if ( strcmp(cr->m_coll,"main")==0 ) return false;

	if ( ! g_conf.m_useCollectionPasswords) return false;

	// empty password field? then allow them through
	if ( cr->m_collectionPasswords.length() <= 0 &&
	     cr->m_collectionIps      .length() <= 0 )
		return true;

	// a good ip?
	const char *p   = iptoa(sock->m_ip);
	char *buf = cr->m_collectionIps.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	// if they got the password, let them in
	p = hr->getString("pwd");
	if ( ! p ) p = hr->getString("password");
	if ( ! p ) p = hr->getStringFromCookie("pwd");
	if ( ! p ) return false;
	buf = cr->m_collectionPasswords.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	return false;
}
	

// . is user a root administrator?
// . only need to be from root IP *OR* have password, not both
bool Conf::isMasterAdmin ( TcpSocket *socket , HttpRequest *hr ) {
	bool isAdmin = false;

	// totally open access?
	//if ( m_numConnectIps  <= 0 && m_numMasterPwds <= 0 )
	if ( m_connectIps.length() <= 0 &&
	     m_masterPwds.length() <= 0 )
		isAdmin = true;

	// coming from root gets you in
	if ( socket && isMasterIp ( socket->m_ip ) ) 
		isAdmin = true;

	if ( hasMasterPwd ( hr ) ) 
		isAdmin = true;

	if ( ! isAdmin )
		return false;

	// default this to true so if user specifies &admin=0 then it 
	// cancels our admin view
	if ( hr && ! hr->getLong("admin",1) )
		return false;

	return true;
}


bool Conf::hasMasterPwd ( HttpRequest *hr ) {
	if ( m_masterPwds.length() <= 0 )
		return false;

	const char *p = hr->getString("pwd");

	if ( ! p ) p = hr->getString("password");

	if ( ! p ) p = hr->getStringFromCookie("pwd");

	if ( ! p ) return false;

	const char *buf = m_masterPwds.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

// . check this ip in the list of admin ips
bool Conf::isMasterIp ( uint32_t ip ) {
	if ( m_connectIps.length() <= 0 ) return false;

	char *p = iptoa(ip);
	char *buf = m_connectIps.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

bool Conf::isConnectIp ( uint32_t ip ) {
	return isMasterIp(ip);
}

// . set all member vars to their default values
void Conf::reset ( ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;
}

bool Conf::init ( char *dir ) { // , int32_t hostId ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;

	char fname[1024];
	File f;

	m_isLocal = false;

	if ( dir ) sprintf ( fname , "%sgb.conf", dir );
	else       sprintf ( fname , "./gb.conf" );

	// try regular gb.conf then
	f.set ( fname );

	// make sure g_mem.maxMem is big enough temporarily
	g_conf.m_maxMem = 8000000000; // 8gb temp

	bool status = g_parms.setFromFile ( this , fname , NULL , OBJ_CONF );

	if ( g_conf.m_maxMem < 10000000 ) g_conf.m_maxMem = 10000000;

	// if not there, create it!
	if ( ! status ) {
		log("gb: Creating %s from defaults.",fname);
		g_errno = 0;
		// set to defaults
		g_conf.reset();
		// and save it
		m_save = true;
		save();
		// clear errors
		g_errno = 0;
		status = true;
	}

	if ( ! g_mem.init ( ) ) return false;

	// always turn this off
	g_conf.m_testMem      = false;

	// and this, in case you forgot to turn it off
	if ( g_conf.m_isLive ) g_conf.m_doConsistencyTesting = false;

	// this off
	g_conf.m_repairingEnabled = false;

	// force on for now
	g_conf.m_useStatsdb = true;

	// hard-code disable this -- could be dangerous
	g_conf.m_bypassValidation = true;

	// this could too! (need this)
	g_conf.m_allowScale = true;//false;

	// . until we fix spell checker
	// . the hosts splitting count isn't right and it just sends to like
	//   host #0 or something...
	g_conf.m_doSpellChecking = false;

	g_conf.m_forceIt = false;

	// sanity check
	if ( g_hostdb.m_indexSplits > MAX_SHARDS ) {
		log("db: Increase MAX_SHARDS");
		g_process.shutdownAbort(true); 
	}

	// HACK: set this now
	setRootIps();

	return status;
}

void Conf::setRootIps ( ) {
	// set m_numDns based on Conf::m_dnsIps[] array
	int32_t i; for ( i = 0; i < MAX_DNSIPS ; i++ ) {
		m_dnsPorts[i] = 53;
		if ( ! g_conf.m_dnsIps[i] ) break;
	}
	m_numDns = i;

	// fail back to public dns
	const char *ipStr = PUBLICLY_AVAILABLE_DNS1;
	if ( m_numDns == 0 ) {
		m_dnsIps[0] = atoip( ipStr , strlen(ipStr) );
		m_dnsPorts[0] = 53;
		m_numDns = 1;
	}

	// default this to off on startup for now until it works better
	m_askRootNameservers = false;

	// and return as well
	return;
}

// . parameters can be changed on the fly so we must save Conf
bool Conf::save ( ) {
	if ( ! m_save ) {
		return true;
	}

	// fix so if we core in malloc/free we can still save conf
	StackBuf<1024> fn;
	fn.safePrintf("%sgb.conf",g_hostdb.m_dir);
	bool status = g_parms.saveToXml ( (char *)this , fn.getBufStart(), OBJ_CONF );

	return status;
}

// . get the default collection based on hostname
//   will look for the hostname in each collection for a match
//   no match defaults to default collection
const char *Conf::getDefaultColl ( ) {
	if ( ! m_defaultColl[0] ) {
		return "main";
	}

	// just use default coll for now to keep things simple
	return m_defaultColl;
}
