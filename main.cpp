//
// Matt Wells, copyright Sep 2001
// 

#include "gb-include.h"

#include <sched.h>        // clone()
// declare this stuff up here for call the pread() in our seek test below
//
// maybe we should put this in a common header file so we don't have 
// certain files compiled with the platform default, and some not -partap

#include "Version.h" // getVersion()
#include "Mem.h"
#include "Conf.h"
#include "JobScheduler.h"
#include "Hostdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderLoop.h"
#include "SpiderCache.h"
#include "Doledb.h"
#include "Clusterdb.h"
#include "Collectiondb.h"
#include "Sections.h"
#include "UdpServer.h"
#include "Repair.h"
#include "DailyMerge.h"
#include "MsgC.h"
#include "HttpServer.h"
#include "Loop.h"
#include "HighFrequencyTermShortcuts.h"
#include "PageTemperatureRegistry.h"
#include "Docid2Siteflags.h"
#include "SiteMedianPageTemperatureRegistry.h"
#include "UrlRealtimeClassification.h"
#include "IPAddressChecks.h"
#include <sys/resource.h>  // setrlimit
#include "Stats.h"
#include "Statistics.h"
#include "Speller.h"       // g_speller
#include "Wiki.h"          // g_wiki
#include "Wiktionary.h"    // g_wiktionary
#include "WordVariations.h"
#include "CountryCode.h"
#include "Domains.h"
#include "Pos.h"
#include "Title.h"
#include "Speller.h"
#include "SummaryCache.h"
#include "InstanceInfoExchange.h"
#include "WantedChecker.h"
#include "Dns.h"
#include "DumpSpiderdbSqlite.h"

// include all msgs that have request handlers, cuz we register them with g_udp
#include "Msg0.h"
#include "Msg4In.h"
#include "Msg4Out.h"

#include "Msg13.h"
#include "Msg20.h"
#include "Msg22.h"
#include "Msg25.h"
#include "Msg39.h"
#include "Msg40.h"    // g_resultsCache
#include "Parms.h"
#include "Pages.h"
#include "PageInject.h"
#include "unicode/UCMaps.h"
#include "utf8_convert.h"

#include "Profiler.h"
#include "Proxy.h"

#include "linkspam.h"
#include "Process.h"
#include "sort.h"
#include "RdbBuckets.h"
#include "SpiderProxy.h"
#include "HashTable.h"
#include "GbUtil.h"
#include "Dir.h"
#include "File.h"
#include "DnsBlockList.h"
#include "ContentTypeBlockList.h"
#include "UrlMatchList.h"
#include "UrlBlockCheck.h"
#include "DocDelete.h"
#include "GbDns.h"
#include "ScopedLock.h"
#include "RobotsCheckList.h"
#include "ConvertSpiderdb.h"
#include "RobotsBlockedResultOverride.h"
#include "UrlResultOverride.h"
#include "FxCheckAdult.h"
#include "FxCheckSpam.h"
#include "GbCompress.h"
#include "DocRebuild.h"
#include "DocReindex.h"
#include "FxExplicitKeywords.h"
#include "IpBlockList.h"
#include "SpiderdbSqlite.h"
#include "QueryLanguage.h"
#include "SiteNumInlinks.h"
#include "ContentMatchList.h"


#include <sys/stat.h> //umask()
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#include <valgrind/helgrind.h>
#endif

static bool registerMsgHandlers();
static bool registerMsgHandlers1();
static bool registerMsgHandlers2();

static const int32_t commandLineDumpdbRecSize = 10 * 1024 * 1024; //recSizes parameter for Msg5::getList() while dumping database from the command-line

static void printHelp();

static void dumpTitledb  (const char *coll, int32_t sfn, int32_t numFiles, bool includeTree,
			   int64_t docId , bool justPrintDups );

static void dumpTagdb(const char *coll, int32_t sfn, int32_t numFiles, bool includeTree, char req,
		      const char *site);

//dumpPosdb() is not local becaue it is called directly by unittests
void dumpPosdb(const char *coll, int32_t sfn, int32_t numFiles, bool includeTree, int64_t termId , bool justVerify);
static void dumpWaitingTree(const char *coll);
static void dumpRobotsTxtCache(const char *coll);

static void dumpDoledb(const char *coll, int32_t sfn, int32_t numFiles, bool includeTree);
static void dumpClusterdb(const char *coll, int32_t sfn, int32_t numFiles, bool includeTree);
static void dumpLinkdb(const char *coll, int32_t sfn, int32_t numFiles, bool includeTree, const char *url, bool urlhash);

static void dumpUnwantedTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree);
static void dumpWantedTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree);
static void dumpAdultTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree);
static void dumpSpamTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree);

static int copyFiles(const char *dstDir);

static const char *getAbsoluteGbDir(const char *argv0);

static int32_t checkDirPerms(const char *dir);

static bool hashtest();
// how fast to parse the content of this docId?
static bool parseTest(const char *coll, int64_t docId, const char *query);
static bool summaryTest1(char *rec, int32_t listSize, const char *coll, int64_t docId, const char *query );

static bool cacheTest();
static void countdomains(const char* coll, int32_t numRecs, int32_t output);

static bool argToBoolean(const char *arg);
static bool parseOptionalHostRange(int rangearg, int argc, char **argv, int *h1, int *h2);

static void wvg_log_function(WordVariationGenerator::log_class_t log_class, const char *fmt, va_list ap);

static void wakeupPollLoop() {
	g_loop.wakeupPollLoop();
}

static UdpProtocol g_dp; // Default Proto

// installFlag konstants 
typedef enum {
	ifk_install = 1,
	ifk_installgb ,
	ifk_installconf ,
	ifk_dsh ,
	ifk_dsh2 ,
	ifk_backupcopy ,
	ifk_backupmove ,
	ifk_backuprestore ,
	ifk_installconf2 ,
	ifk_start ,
	ifk_tmpstart ,
	ifk_installtmpgb ,
	ifk_proxy_start
} install_flag_konst_t;

static int install_file(const char *file, int32_t hostId, int32_t hostId2);
static int install ( install_flag_konst_t installFlag, int32_t hostId, char *dir = NULL,
                     int32_t hostId2 = -1, char *cmd = NULL );

bool doCmd ( const char *cmd , int32_t hostId , const char *filename , bool sendToHosts,
	     bool sendToProxies, int32_t hostId2=-1 );

static char unicode_data_dir[2014]; //filled in by main2() when hostdb has been initialized

//void tryMergingWrapper ( int fd , void *state ) ;

//void resetAll ( );
//void spamTest ( ) ;

extern void resetPageAddUrl    ( );
extern void resetHttpMime      ( );
extern void reset_iana_charset ( );
extern void resetAdultBit      ( );
extern void resetEntities      ( );
extern void resetQuery         ( );


extern bool g_recoveryMode; // HostFlags.cpp

static int argc_copy;
static char **argv_copy;
static int rc_copy;

static int main2(int argc, char *argv[]);

static void *main2_trampoline(void *) {
	pthread_setname_np(pthread_self(),"main");
	rc_copy = main2(argc_copy,argv_copy);
	return NULL;
}


int main ( int argc , char *argv[] ) {
	//Run the main thread ... in a thread
	//The reason for this is so that 'htop', 'perf' and other tools show metrics
	//for the main thread instead of lumping it together wide process-wide
	//aggregation (eg. linux kernel 4.4.x claims the main task/process does IO
	//eventhough it provably doesn't)
	argc_copy = argc;
	argv_copy = argv;
	pthread_t tid;
	int rc = pthread_create(&tid,NULL,main2_trampoline,NULL);
	if(rc!=0){
		fprintf(stderr,"pthread_create() failed with error %d (%s)",rc,strerror(rc));
		return 99;
	}
	rc = pthread_join(tid,NULL);
	if(rc!=0) {
		fprintf(stderr,"pthread_join() failed with error %d (%s)",rc,strerror(rc));
		return 99;
	}
	
	if(rc_copy)
		fprintf( stderr, "Failed to start gb. Exiting.\n" );

	return rc_copy;
}

int main2 ( int argc , char *argv[] ) {
	g_conf.m_runAsDaemon = false;
	g_conf.m_logToFile = false;

#ifdef _VALGRIND_
	//threads are incrementing the counters all over the place
	VALGRIND_HG_DISABLE_CHECKING(&g_stats,sizeof(g_stats));
#endif
	// record time for uptime
	g_stats.m_uptimeStart = time(NULL);

	int32_t  cmdarg = 0;

	// get command

	// it might not be there, might be a simple "./gb" 
	const char *cmd = "";
	if ( argc >= 2 ) {
		cmdarg = 1;
		cmd = argv[1];
	}

	const char *cmd2 = "";
	if ( argc >= 3 )
		cmd2 = argv[2];

	int arch = 64;
	if ( sizeof(char *) == 4 ) arch = 32;

	// help
	if ( strcmp ( cmd , "-h" ) == 0 ) {
		printHelp();
		return 0;
	}

	// version
	if ( strcmp ( cmd , "-v" ) == 0 ) {
		printVersion();
		return 0; 
	}

	//send an email on startup for -r, like if we are recovering from an
	//unclean shutdown.
	g_recoveryMode = false;
	if ( strncmp ( cmd , "-r" ,2 ) == 0 || strncmp ( cmd2 , "-r",2 ) == 0 ) {
		g_recoveryMode = true;
	}

	// run as daemon? then we have to fork
	if ( ( strcmp ( cmd , "-d" ) == 0 ) || ( strcmp ( cmd2 , "-d" ) == 0 ) ) {
		g_conf.m_runAsDaemon = true;
	}

	if ( ( strcmp ( cmd , "-l" ) == 0 ) || ( strcmp ( cmd2 , "-l" ) == 0 ) ) {
		g_conf.m_logToFile = true;
	}

	if( (strcmp( cmd, "countdomains" ) == 0) &&  (argc >= (cmdarg + 3)) ) {
		uint32_t tmp = atoi( argv[cmdarg+1] );
		if( (tmp * 10) > g_mem.getMemTableSize() )
			g_mem.setMemTableSize(tmp * 10);
	}

	// these tests do not need a hosts.conf
	if ( strcmp ( cmd , "hashtest" ) == 0 ) {
		if ( argc > cmdarg+1 ) {
			printHelp();
			return 1;
		}
		hashtest();
		return 0;
	}
	// these tests do not need a hosts.conf
	if ( strcmp ( cmd , "cachetest" ) == 0 ) {
		if ( argc > cmdarg+1 ) {
			printHelp();
			return 1;
		}
		cacheTest();
		return 0;
	}
	if ( strcmp ( cmd , "parsetest"  ) == 0 ) {
		if ( cmdarg+1 >= argc ) {
			printHelp();
			return 1;
		}
		// load up hosts.conf
		//if ( ! g_hostdb.init(hostId) ) {
		//	log("db: hostdb init failed." ); return 1; }
		// init our table for doing zobrist hashing
		if ( ! hashinit() ) {
			log("db: Failed to init hashtable." ); return 1; }

		int64_t docid = atoll1(argv[cmdarg+1]);
		const char *coll   = "";
		const char *query  = "";
		if ( cmdarg+3 <= argc ) coll  = argv[cmdarg+2];
		if ( cmdarg+4 == argc ) query = argv[cmdarg+3];
		parseTest( coll, docid, query );
		return 0;
	}

	if ( strcmp ( cmd ,"isportinuse") == 0 ) {
		if ( cmdarg+1 >= argc ) {
			printHelp();
			return 1;
		}
		int port = atol ( argv[cmdarg+1] );
		// make sure port is available. returns false if in use.
		if ( ! g_httpServer.m_tcp.testBind(port,false) )
			// and we should return with 1 so the keep alive
			// script will exit
			exit (1);
		// port is not in use, return 0
		exit(0);
	}

	// need threads here for tests?

	// note the stack size for debug purposes
	struct rlimit rl;
	getrlimit(RLIMIT_STACK, &rl);
	log(LOG_INFO,"db: Stack size is %" PRId64".", (int64_t)rl.rlim_cur);


	struct rlimit lim;
	// limit fds
	// try to prevent core from systems where it is above 1024
	// because our FD_ISSET() libc function will core! (it's older)
	int32_t NOFILE = 1024;
	lim.rlim_cur = lim.rlim_max = NOFILE;
	if ( setrlimit(RLIMIT_NOFILE,&lim)) {
		log("db: setrlimit RLIMIT_NOFILE %" PRId32": %s.",
		    NOFILE,mstrerror(errno) );
	}

	struct rlimit rlim;
	getrlimit ( RLIMIT_NOFILE,&rlim);
	if ( (int32_t)rlim.rlim_max > NOFILE || (int32_t)rlim.rlim_cur > NOFILE ) {
		log("db: setrlimit RLIMIT_NOFILE failed!");
		g_process.shutdownAbort(true);
	}

	// set the s_pages array for print admin pages
	g_pages.init ( );

	bool isProxy = false;
	if ( strcmp( cmd , "proxy" ) == 0 && strcmp( argv[cmdarg+1] , "load" ) == 0 ) {
		isProxy = true;
	}

	// this is just like starting up a gb process, but we add one to
	// each port, we are a dummy machine in the dummy cluster.
	// gb -w <workingdir> tmpstart [hostId]
	bool useTmpCluster = false;
	if ( strcmp ( cmd , "tmpstart" ) == 0 ) {
		useTmpCluster = true;
	}

	// gb -w <workingdir> tmpstop [hostId]
	if ( strcmp ( cmd , "tmpstop" ) == 0 ) {
		useTmpCluster = true;
	}

	// gb -w <workingdir> tmpstarthost
	if ( strcmp ( cmd , "tmpstarthost" ) == 0 ) {
		useTmpCluster = true;
	}

	bool initMyHost = true;
	if (strcmp(cmd, "install") == 0 ||
	    strcmp(cmd, "installfile") == 0) {
		initMyHost = false;
	}

	//
	// get current working dir that the gb binary is in. all the data
	// files should in there too!!
	const char *workingDir = getAbsoluteGbDir ( argv[0] );
	if ( ! workingDir ) {
		fprintf(stderr,"could not get working dir. Exiting.\n");
		return 1;
	}

	//log("host: working directory is %s",workingDir);

	//initialize IP address checks
	initialize_ip_address_checks();

	// Make sure TLD table is initializing before calling any URL handling function
	if(!initializeDomains(g_hostdb.m_dir)) {
		log( LOG_ERROR, "Domains initialization failed!" );
		return 1;
	}
	
	// load up hosts.conf
	// . it will determine our hostid based on the directory path of this
	//   gb binary and the ip address of this server
	if ( ! g_hostdb.init(-1, isProxy, useTmpCluster, initMyHost, workingDir)) {
		log( LOG_ERROR, "db: hostdb init failed." );
		return 1;
	}

	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log( LOG_ERROR, "db: Failed to init hashtable." );
		return 1;
	}

	sprintf(unicode_data_dir,"%s/ucdata/",g_hostdb.m_dir);

	// . hashinit() calls srand() w/ a fixed number
	// . let's mix it up again
	srand ( time(NULL) );

	// do not save conf if any core dump occurs starting here
	// down to where we set this back to true
	g_conf.m_save = false;

	//Put this here so that now we can log messages
  	if ( strcmp ( cmd , "proxy" ) == 0 ) {
		if (argc < 3){
			printHelp();
			return 1;
		}

		int32_t proxyId = -1;
		if ( cmdarg+2 < argc ) proxyId = atoi ( argv[cmdarg+2] );
		
		if ( strcmp ( argv[cmdarg+1] , "start" ) == 0 ) {
			return install ( ifk_proxy_start , proxyId );
		}
		else if ( strcmp ( argv[cmdarg+1] , "stop" ) == 0 ) {
			g_proxy.m_proxyRunning = true;
			return doCmd ( "save=1" , proxyId , "master" , false, true );
		}

		else if ( strcmp ( argv[cmdarg+1] , "replacehost" ) == 0 ) {
			g_proxy.m_proxyRunning = true;
			int32_t hostId = -1;
			int32_t spareId = -1;
			if ( cmdarg + 2 < argc ) {
				hostId = atoi ( argv[cmdarg+2] );
				spareId = atoi ( argv[cmdarg+3] );
			}
			char replaceCmd[256];
			sprintf(replaceCmd, "replacehost=1&rhost=%" PRId32"&rspare=%" PRId32, hostId, spareId);
			return doCmd ( replaceCmd, -1, "admin/hosts", false, true);
		}

		else if ( proxyId == -1 || strcmp ( argv[cmdarg+1] , "load" ) != 0 ) {
			printHelp();
			return 1;
		}

		Host *h = g_hostdb.getProxy( proxyId );
		uint16_t httpPort = h->getInternalHttpPort();
		uint16_t httpsPort = h->getInternalHttpsPort();
		//we need udpserver for addurl
		uint16_t udpPort  = h->m_port;

		if ( ! g_conf.init ( h->m_dir ) ) {
			log( LOG_ERROR, "db: Conf init failed." );
			return 1;
		}

		// init the loop before g_process since g_process
		// registers a sleep callback!
		if ( ! g_loop.init() ) {
			log( LOG_ERROR, "db: Loop init failed." );
			return 1;
		}

		//if ( ! g_jobScheduler.initialize()     ) {
		//	log("db: Threads init failed." ); return 1; }

		g_process.init();

		if ( ! g_process.checkNTPD() ) {
			log( LOG_ERROR, "db: ntpd not running on proxy" );
			return 1;
		}

		const char *errmsg=NULL;
		if ( !UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
			log( LOG_ERROR, "db: Unicode initialization failed! %s", errmsg);
			return 1;
		}
		if(!utf8_convert_initialize()) {
			log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
			return 1;
		}

		// load speller unifiedDict for spider compression proxy
		//if ( g_hostdb.m_myHost->m_type & HT_SCPROXY )
		//	g_speller.init();

		if ( ! g_udpServer.init( g_hostdb.getMyPort() ,
					 &g_dp,
					 20000000 ,   // readBufSIze
					 20000000 ,   // writeBufSize
					 20       ,   // pollTime in ms
					 g_conf.m_udpMaxSockets     , // max udp slots
					 false    )){ // is dns?
			log( LOG_ERROR, "db: UdpServer init failed." );
			return 1;
		}


		if (!g_proxy.initProxy (proxyId, udpPort, 0, &g_dp)) {
			log( LOG_ERROR, "proxy: init failed" );
			return 1;
		}

		// init our table for doing zobrist hashing
		if ( ! hashinit() ) {
			log( LOG_ERROR, "db: Failed to init hashtable." );
			return 1;
		}

		if ( ! g_proxy.initHttpServer( httpPort, httpsPort ) ) {
			log( LOG_ERROR, "db: HttpServer init failed. Another gb "
			    "already running? If not, try editing "
			    "./hosts.conf to "
			    "change the port from %" PRId32" to something bigger. "
			    "Or stop gb by running 'gb stop' or by "
			    "clicking 'save & exit' in the master controls."
			    , (int32_t)httpPort ); 
			// this is dangerous!!! do not do the shutdown thing
			return 1;
		}		
		
		//we should save gb.conf right ?
		g_conf.m_save = true;

		g_loop.runLoop();
	}

	// gb dsh cmd [hostrange]
	if ( strcmp ( cmd , "dsh" ) == 0 ) {	
		if ( cmdarg+1 >= argc ) {
			printHelp();
			return 1;
		}

		char *cmd = argv[cmdarg+1];

		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+2,argc,argv,&h1,&h2))
			return 1;

		return install ( ifk_dsh, h1, NULL, h2, cmd );
	}

	// gb dsh2 cmd [hostrange]
	if ( strcmp ( cmd , "dsh2" ) == 0 ) {
		if ( cmdarg+1 >= argc ) {
			printHelp();
			return 1;
		}
		char *cmd = argv[cmdarg+1];

		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+2,argc,argv,&h1,&h2))
			return 1;

		return install ( ifk_dsh2, h1, NULL, h2, cmd );
	}

	// gb copyfiles, like gb install but takes a dir not a host #
	if ( strcmp ( cmd , "copyfiles" ) == 0 ) {	
		if ( cmdarg + 1 >= argc ) {
			printHelp();
			return 1;
		}
		char *dir = argv[cmdarg+1];
		return copyFiles ( dir );
	}

	// gb install [hostrange]
	if ( strcmp ( cmd , "install" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install ( ifk_install, h1, NULL, h2 );
	}

	// gb installgb [hostrange]
	if ( strcmp ( cmd , "installgb" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_installgb, h1, NULL, h2);
	}

	// gb installfile filename [hostrange]
	if ( strcmp ( cmd , "installfile" ) == 0 ) {
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+2,argc,argv,&h1,&h2))
			return 1;
		return install_file(argv[cmdarg + 1], h1, h2);
	}

	// gb installtmpgb [hostrange]
	if ( strcmp ( cmd , "installtmpgb" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_installtmpgb, h1, NULL, h2);
	}

	// gb installconf [hostrange]
	if ( strcmp ( cmd , "installconf" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_installconf, h1, NULL, h2);
	}

	// gb installconf2 [hostrange]
	if ( strcmp ( cmd , "installconf2" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_installconf2, h1, NULL, h2);
	}

	// gb start [hostId]
	if ( strcmp ( cmd , "start" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_start, h1, NULL, h2);
	}

	// gb tmpstart [hostId]
	if ( strcmp ( cmd , "tmpstart" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return install(ifk_tmpstart, h1, NULL, h2);
	}

	if ( strcmp ( cmd , "tmpstop" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("save=1", h1, "master", true, false, h2);
	}

	if ( strcmp ( cmd , "kstop" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("save=1", h1, "master", true, false, h2);
	}

	// gb backupcopy [hostId] <backupSubdirName>
	if ( strcmp ( cmd , "backupcopy" ) == 0 ) {	
		if ( cmdarg + 1 >= argc ) {
			printHelp();
			return 1;
		}
		return install( ifk_backupcopy , -1 , argv[cmdarg+1] );
	}

	// gb backupmove [hostId] <backupSubdirName>
	if ( strcmp ( cmd , "backupmove" ) == 0 ) {	
		if ( cmdarg + 1 >= argc ) {
			printHelp();
			return 1;
		}
		return install( ifk_backupmove , -1 , argv[cmdarg+1] );
	}

	// gb backupmove [hostId] <backupSubdirName>
	if ( strcmp ( cmd , "backuprestore" ) == 0 ) {	
		if ( cmdarg + 1 >= argc ) {
			printHelp();
			return 1;
		}
		return install( ifk_backuprestore, -1 , argv[cmdarg+1] );
	}

	// gb stop [hostId]
	if ( strcmp ( cmd , "stop" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("save=1" , h1 , "master", true, false, h2);
	}

	// gb save [hostId]
	if ( strcmp ( cmd , "save" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("js=1", h1, "master", true, false, h2);
	}

	// gb spidersoff [hostId]
	if ( strcmp ( cmd , "spidersoff" ) == 0 ) {	
		int32_t hostId = -1;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		return doCmd( "se=0", hostId, "master", true, false );
	}

	// gb spiderson [hostid]
	if ( strcmp ( cmd , "spiderson" ) == 0 ) {	
		int32_t hostId = -1;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		return doCmd( "se=1", hostId, "master", true, false );
	}

	// gb cacheoff [hostId]
	if ( strcmp ( cmd , "cacheoff" ) == 0 ) {	
		int32_t hostId = -1;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		return doCmd( "dpco=1", hostId, "master", true, false );
	}

	// gb ddump [hostId]
	if ( strcmp ( cmd , "ddump" ) == 0 ) {	
		int32_t hostId = -1;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		return doCmd( "dump=1", hostId, "master", true, false );
	}

	// gb pmerge [hostId]
	if ( strcmp ( cmd , "pmerge" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("pmerge=1", h1, "master", true, false, h2);
	}

	// gb spmerge [hostId]
	if ( strcmp ( cmd , "spmerge" ) == 0 ) {
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("spmerge=1", h1, "master", true, false, h2);
	}

	// gb tmerge [hostId]
	if ( strcmp ( cmd , "tmerge" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("tmerge=1", h1, "master", true, false, h2);
	}

	// gb merge [hostId]
	if ( strcmp ( cmd , "merge" ) == 0 ) {	
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+1,argc,argv,&h1,&h2))
			return 1;
		return doCmd("merge=1", h1, "master", true, false, h2);
	}

	// gb setnote <hostid> <note>
	if ( strcmp ( cmd, "setnote" ) == 0 ) {
		int32_t hostId;
		char *note;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		else return 0;
		if ( cmdarg + 2 < argc ) note = argv[cmdarg+2];
		else return 0;
		char urlnote[1024];
		urlEncode(urlnote, 1024, note, strlen(note));
		log ( LOG_INIT, "conf: setnote %" PRId32": %s", hostId, urlnote );
		char setnoteCmd[256];
		sprintf(setnoteCmd, "setnote=1&host=%" PRId32"&note=%s",
				    hostId, urlnote);
		return doCmd( setnoteCmd, -1, "admin/hosts", true, false );
	}

	// gb setsparenote <spareid> <note>
	if ( strcmp ( cmd, "setsparenote" ) == 0 ) {
		int32_t spareId;
		char *note;
		if ( cmdarg + 1 < argc ) spareId = atoi ( argv[cmdarg+1] );
		else return 0;
		if ( cmdarg + 2 < argc ) note = argv[cmdarg+2];
		else return 0;
		char urlnote[1024];
		urlEncode(urlnote, 1024, note, strlen(note));
		log(LOG_INIT, "conf: setsparenote %" PRId32": %s", spareId, urlnote);
		char setnoteCmd[256];
		sprintf(setnoteCmd, "setsparenote=1&spare=%" PRId32"&note=%s",
				    spareId, urlnote);
		return doCmd( setnoteCmd, -1, "admin/hosts" , true, false );
	}

	// gb replacehost <hostid> <spareid>
	if ( strcmp ( cmd, "replacehost" ) == 0 ) {
		int32_t hostId = -1;
		int32_t spareId = -1;
		if ( cmdarg + 1 < argc ) hostId = atoi ( argv[cmdarg+1] );
		if ( cmdarg + 2 < argc ) spareId = atoi ( argv[cmdarg+2] );
		char replaceCmd[256];
		sprintf(replaceCmd, "replacehost=1&rhost=%" PRId32"&rspare=%" PRId32,
				    hostId, spareId);
		return doCmd( replaceCmd, -1, "admin/hosts", true, true );
	}

	// . read in the conf file
	// . this now initializes from a dir and hostId, they should all be
	//   name gbHID.conf
	// . now that hosts.conf has more of the burden, all gbHID.conf files
	//   can be identical
	if ( ! g_conf.init ( g_hostdb.m_myHost->m_dir ) ) {
		log( LOG_ERROR, "db: Conf init failed." );
		return 1;
	}

	if ( ! g_jobScheduler.initialize(g_conf.m_maxCoordinatorThreads, g_conf.m_maxCpuThreads, g_conf.m_maxSummaryThreads, g_conf.m_maxIOThreads, g_conf.m_maxExternalThreads, g_conf.m_maxFileMetaThreads, g_conf.m_maxMergeThreads, wakeupPollLoop)) {
		log( LOG_ERROR, "db: JobScheduler init failed." );
		return 1;
	}
	
	// put in read only mode
	if ( useTmpCluster ) {
		g_conf.m_readOnlyMode = true;
	}

	// init the loop, needs g_conf
	if ( ! g_loop.init() ) {
		log( LOG_ERROR, "db: Loop init failed." );
		return 1;
	}

	// the new way to save all rdbs and conf
	// must call after Loop::init() so it can register its sleep callback
	g_process.init();

	// set up the threads, might need g_conf

	// . gb dump [dbLetter][coll][fileNum] [numFiles] [includeTree][termId]
	// . spiderdb is special:
	//   gb dump s [coll][fileNum] [numFiles] [includeTree] [0=old|1=new]
	//           [priority] [printStats?]
	if ( strcmp ( cmd , "dump" ) == 0 ) {
		//
		// tell Collectiondb, not to verify each rdb's data
		//
		g_dumpMode = true;

		if ( cmdarg+1 >= argc ) {
			printHelp();
			return 1;
		}
		int32_t startFileNum =  0;
		int32_t numFiles     = -1;
		bool includeTree     =  true;
		const char *coll = "";

		// so we do not log every collection coll.conf we load
		g_conf.m_doingCommandLine = true;

		// we have to init collection db because we need to know if 
		// the collnum is legit or not in the tree
		if ( ! g_collectiondb.loadAllCollRecs()   ) {
			log("db: Collectiondb init failed." ); return 1; }

		if ( cmdarg+2 < argc ) coll         = argv[cmdarg+2];
		if ( cmdarg+3 < argc ) startFileNum = atoi(argv[cmdarg+3]);
		if ( cmdarg+4 < argc ) numFiles     = atoi(argv[cmdarg+4]);
		if ( cmdarg+5 < argc ) includeTree  = argToBoolean(argv[cmdarg+5]);


		if ( argv[cmdarg+1][0] == 't' ) {
			int64_t docId = 0LL;
			if ( cmdarg+6 < argc ) {
				docId = atoll1(argv[cmdarg+6]);
			}

			dumpTitledb (coll, startFileNum, numFiles, includeTree, docId, false);

		}
		else if ( argv[cmdarg+1][0] == 'D' ) {
			int64_t docId = 0LL;
			if ( cmdarg+6 < argc ) {
				docId = atoll1(argv[cmdarg+6]);
			}

			dumpTitledb (coll, startFileNum, numFiles, includeTree, docId, true);
		}
		else if (strcmp(argv[cmdarg+1], "w") == 0) {
		       dumpWaitingTree(coll);
		}
		else if (strcmp(argv[cmdarg+1], "rtc") == 0) {
		       dumpRobotsTxtCache(coll);
		}
		else if ( argv[cmdarg+1][0] == 'x' )
			dumpDoledb  (coll,startFileNum,numFiles,includeTree);
 		else if (strcmp(argv[cmdarg+1], "s") == 0) {
 			int32_t firstIp = 0;
 			if(cmdarg+3 < argc) {
			    firstIp = atoip(argv[cmdarg + 3]);
		    }

			dumpSpiderdbSqlite(coll, firstIp);
 		}
		else if ( argv[cmdarg+1][0] == 'S' ) {
			char *site = NULL;
			if ( cmdarg+6 < argc ) {
				site = argv[ cmdarg + 6 ];
			}
			dumpTagdb( coll, startFileNum, numFiles, includeTree, 0, site );
		} else if ( argv[cmdarg+1][0] == 'z' ) {
			char *site = NULL;
			if ( cmdarg+6 < argc ) {
				site = argv[ cmdarg + 6 ];
			}
			dumpTagdb( coll, startFileNum, numFiles, includeTree, 'z', site );
		} else if ( argv[cmdarg+1][0] == 'A' ) {
			dumpTagdb( coll, startFileNum, numFiles, includeTree, 'A', NULL );
		} else if ( argv[cmdarg+1][0] == 'W' ) {
			dumpTagdb( coll, startFileNum, numFiles, includeTree,   0, NULL );
		} else if ( argv[cmdarg+1][0] == 'l' )
			dumpClusterdb (coll,startFileNum,numFiles,includeTree);
		else if (strcmp(argv[cmdarg+1], "Lu") == 0) {
			char *url = NULL;
			if ( cmdarg+6 < argc ) url = argv[cmdarg+6];
			dumpLinkdb(coll,startFileNum,numFiles,includeTree,url,true);
		}
		else if (strcmp(argv[cmdarg+1], "Ls") == 0) {
			char *url = NULL;
			if ( cmdarg+6 < argc ) url = argv[cmdarg+6];
			dumpLinkdb(coll,startFileNum,numFiles,includeTree,url,false);
		}  else if ( argv[cmdarg+1][0] == 'p' ) {
			int64_t termId  = -1;
			if ( cmdarg+6 < argc ) {
				char *targ = argv[cmdarg+6];
				if ( is_alpha_a(targ[0]) ) {
					char *colon = strstr(targ,":");
					int64_t prefix64 = 0LL;
					if ( colon ) {
						*colon = '\0';
						prefix64 = hash64n(targ);
						targ = colon + 1;
					}
					// hash the term itself
					termId = hash64n(targ);
					// hash prefix with termhash
					if ( prefix64 )
						termId = hash64(termId,prefix64);
					termId &= TERMID_MASK;
					printf("termId=%ld\n", termId);
				}
				else {
					termId = atoll1(targ);
				}
			}
			dumpPosdb( coll, startFileNum, numFiles, includeTree, termId, false );
		}  else if (strcmp(argv[cmdarg+1], "u") == 0) {
			dumpUnwantedTitledbRecs(coll, startFileNum, numFiles, includeTree);
		}  else if (strcmp(argv[cmdarg+1], "wt") == 0) {
			dumpWantedTitledbRecs(coll, startFileNum, numFiles, includeTree);
		}  else if (strcmp(argv[cmdarg+1], "at") == 0) {
			dumpAdultTitledbRecs(coll, startFileNum, numFiles, includeTree);
		}  else if (strcmp(argv[cmdarg+1], "st") == 0) {
			dumpSpamTitledbRecs(coll, startFileNum, numFiles, includeTree);
		} else {
			printHelp();
			return 1;
		}
		// disable any further logging so final log msg is clear
		g_log.m_disabled = true;
		g_collectiondb.reset();
		return 0;
	}
	
	// gb sitedeftemp prepare|switch [hostrange]
	if(strcmp(cmd, "sitedeftemp") == 0) {
		int h1,h2;
		if(!parseOptionalHostRange(cmdarg+2,argc,argv,&h1,&h2))
			return 1;
		if(strcmp(argv[cmdarg+1],"prepare")==0)
			return doCmd("sitedeftemp=prepare", h1, "master", true, false, h2);
		else if(strcmp(argv[cmdarg+1],"switch")==0)
			return doCmd("sitedeftemp=switch", h1, "master", true, false, h2);
		else {
			printHelp();
			return 1;
		}
	}

	if(strcmp(cmd, "dumpcsv") == 0) {
		g_conf.m_readOnlyMode = true; //we don't need write access
		g_conf.m_doingCommandLine = true; // so we do not log every collection coll.conf we load
		if( !g_collectiondb.loadAllCollRecs()) {
			log("db: Collectiondb init failed.");
			return 1;
		}
		if(argv[cmdarg+1][0] == 's') {
			bool interpret_values = argc>cmdarg+3 ? argToBoolean(argv[cmdarg+3]) : false;
			dumpSpiderdbSqliteCsv(argv[cmdarg+2],interpret_values);
		}
		g_log.m_disabled = true;
		g_collectiondb.reset();
		return 0;
	}

	if(strcmp(cmd, "convertspiderdb") == 0) {
		g_conf.m_doingCommandLine = true; // so we do not log every collection coll.conf we load
		if( !g_collectiondb.loadAllCollRecs()) {
			log("db: Collectiondb init failed.");
			return 1;
		}
		const char *collname = argc>cmdarg+1 ? argv[cmdarg+1] : "main";
		convertSpiderDb(collname);
		g_log.m_disabled = true;
		g_collectiondb.reset();
		return 0;
	}

	if( strcmp( cmd, "countdomains" ) == 0 && argc >= (cmdarg + 2) ) {
		const char *coll = "";
		int32_t outpt;
		coll = argv[cmdarg+1];

		int32_t numRecs;
		if(argc>cmdarg+2) {
			if(!isdigit(argv[cmdarg+2][0])) {
				printHelp();
				return 1;
			}
			numRecs = atoi( argv[cmdarg+2] );
		} else
			numRecs = 1000000;

		if( argc > (cmdarg + 2) ) outpt = atoi( argv[cmdarg+2] );
		else outpt = 0;

		log( LOG_INFO, "countdomains: Allocated Larger Mem Table for: %" PRId32,
		     g_mem.getMemTableSize() );
		const char *errmsg=NULL;
		if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
			log("Unicode initialization failed! %s", errmsg);
			return 1;
		}
		if(!utf8_convert_initialize()) {
			log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
			return 1;
		}

		if ( ! g_collectiondb.loadAllCollRecs()   ) {
			log("db: Collectiondb init failed." ); return 1; }

		countdomains( coll, numRecs, outpt );
		g_log.m_disabled = true;
		return 0;
	}

	// file creation test, make sure we have dir control
	if ( checkDirPerms ( g_hostdb.m_dir ) < 0 ) {
		return 1;
	}

	// . make sure we have critical files
	if ( ! g_process.checkFiles ( g_hostdb.m_dir ) ) {
		return 1;
	}

	g_errno = 0;

	// make sure port is available, no use loading everything up then
	// failing because another process is already running using this port
	if ( ! g_httpServer.m_tcp.testBind(g_hostdb.getMyHost()->getInternalHttpPort(), true)) {
		// return 0 so keep alive bash loop exits
		exit(0);
	}

	int32_t *ips;

	log("db: Logging to file %s.", g_hostdb.m_logFilename );

	if ( ! g_conf.m_runAsDaemon )
		log("db: Use 'gb -d' to run as daemon. Example: gb -d");

	// start up log file
	if ( ! g_log.init( g_hostdb.m_logFilename ) ) {
		fprintf (stderr,"db: Log file init failed. Exiting.\n" );
		return 1;
	}

	g_log.m_logTimestamps = true;
	g_log.m_logReadableTimestamps = true;	// @todo BR: Should be configurable..

	// in case we do not have one, we need it for Images.cpp
	if ( ! makeTrashDir() ) {
		fprintf (stderr,"db: failed to make trash dir. Exiting.\n" ); 
		return 1; 
	}
		

	g_errno = 0;

	// 
	// run as daemon now
	//
	//fprintf(stderr,"running as daemon\n");
	if ( g_conf.m_runAsDaemon ) {
		pid_t pid, sid;
		pid = fork();
		if ( pid < 0 ) exit(EXIT_FAILURE);
		// seems like we core unless parent sets this to NULL.
		// it does not affect the child.
		//if ( pid > 0 ) g_hostdb.m_myHost = NULL;
		// child gets a 0, parent gets the child's pid, so exit
		if ( pid > 0 ) exit(EXIT_SUCCESS);
		// change file mode mask
		umask(0);
		sid = setsid();
		if ( sid < 0 ) exit(EXIT_FAILURE);
		//fprintf(stderr,"done\n");

		// if we do not do this we don't get sigalarms or quickpolls
		// when running as 'gb -d'
		g_loop.init();
	}

	// we register log rotation here because it's after g_loop is initialized
	g_log.registerLogRotation();

	// log the version
	log(LOG_INIT,"conf: Gigablast Version      : %s", getVersion());
	log(LOG_INIT,"conf: Gigablast Architecture : %d-bit", arch);
	log(LOG_INIT,"conf: Gigablast Build config : %s", getBuildConfig());
	log(LOG_INIT,"conf: Gigablast Git commit   : %s", getCommitId());


	// show current working dir
	log("host: Working directory is %s",workingDir);

	log("host: Using %shosts.conf",g_hostdb.m_dir);

	{
		pid_t pid = getpid();
		log("host: Process ID is %" PRIu64,(int64_t)pid);
	}

	// from Hostdb.cpp
	ips = getLocalIps();
	for ( ; ips && *ips ; ips++ ) {
		char ipbuf[16];
		log("host: Detected local ip %s",iptoa(*ips,ipbuf));
	}

	// show it
	log("host: Running as host id #%" PRId32,g_hostdb.m_myHostId );


	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log( LOG_ERROR, "Unicode initialization failed! %s", errmsg);
		return 1;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return 1;
	}

	// the wiktionary for lang identification and alternate word forms/
	// synonyms
	if ( ! g_wiktionary.load() ) {
		log( LOG_ERROR, "Wiktionary initialization failed!" );
		return 1;
	}

	if ( ! g_wiktionary.test() ) {
		log( LOG_ERROR, "Wiktionary test failed!" );
		return 1;
	}

	WordVariationGenerator::set_log_function(wvg_log_function);
	log(LOG_DEBUG,"main: initializing word variations: Danish");
	if(!initializeWordVariationGenerator_Danish()) {
		log(LOG_WARN, "word-variation-danish initialization failed" );
		//but not fatal
	}
	log(LOG_DEBUG,"main: initialized word variations: Danish");

	// the wiki titles
	if ( ! g_wiki.load() ) {
		log( LOG_ERROR, "Wiki initialization failed!" );
		return 1;
	}
	
	// shout out if we're in read only mode
	if ( g_conf.m_readOnlyMode )
		log("db: -- Read Only Mode Set. Can Not Add New Data. --");

	if (!Rdb::initializeRdbDumpThread()) {
		logError("Unable to initialize rdb dump thread");
		return 1;
	}

	// . collectiondb, does not use rdb, loads directly from disk
	// . do this up here so RdbTree::fixTree_unlocked() can fix RdbTree::m_collnums
	// . this is a fake init, cuz we pass in "true"
	if ( ! g_collectiondb.loadAllCollRecs() ) {
		log( LOG_ERROR, "db: Collectiondb load failed." );
		return 1;
	}

	if(!initialiseAllPrimaryRdbs())
		return 1;

	// the spider cache used by SpiderLoop
	if ( ! g_spiderCache.init() ) {
		log( LOG_ERROR, "db: SpiderCache init failed." );
		return 1;
	}

	// now clean the trees since all rdbs have loaded their rdb trees
	// from disk, we need to remove bogus collection data from teh trees
	// like if a collection was delete but tree never saved right it'll
	// still have the collection's data in it
	if ( ! g_collectiondb.addRdbBaseToAllRdbsForEachCollRec ( ) ) {
		log("db: Collectiondb init failed." );
		_exit(1);
	}

	// make sure the we have spiderdb sqlite if we still have spiderdb rdb files
	for (collnum_t collNum = g_collectiondb.getFirstCollnum(); collNum < g_collectiondb.getNumRecs(); ++collNum) {
		CollectionRec *collRec = g_collectiondb.getRec(collNum);
		if (collRec != nullptr) {
			RdbBase *base = collRec->getBase(RDB_SPIDERDB_DEPRECATED);
			if (base->getNumFiles() != 0 && !g_spiderdb_sqlite.existDb(collNum)) {
				// has rdb files but no sqlite file
				log(LOG_ERROR, "Found spiderdb rdb files but no spiderdb sqlite files.");
				log(LOG_ERROR, "Run ./gb convertspiderdb before starting up gb instances");
				gbshutdownCorrupted();
			}
		}
	}

	//Load the high-frequency term shortcuts (if they exist)
	g_hfts.load();

	//Load the page temperature
	g_pageTemperatureRegistry.load();
	
	//load docid->flags/sitehash map
	g_d2fasm.load();

	//load sitehash32->default page temperature
	g_smptr.open();
	
	// load block lists
	g_dnsBlockList.init();
	g_contentTypeBlockList.init();
	g_ipBlockList.init();
	g_contentRetryProxyList.init();

	g_urlBlackList.init();
	g_urlWhiteList.init();
	g_urlProxyList.init();
	g_urlRetryProxyList.init();

	g_robotsCheckList.init();

	g_robotsBlockedResultOverride.init();
	g_urlResultOverride.init();

	// Initialize adult detection
	g_checkAdultList.init("adultwords.txt", "adultphrases.txt");

	// Initialize spam detection
	g_checkSpamList.init("spamphrases.txt");

	if(!ExplicitKeywords::initialize()) {
		log(LOG_ERROR,"Could not initialize explicit keywords file");
		//but otherwise carry on
	}

	// initialize generate global index thread
	if (!RdbBase::initializeGlobalIndexThread()) {
		logError("Unable to initialize global index thread");
		_exit(1);
	}

	if (!Msg4In::initializeIncomingThread()) {
		logError("Unable to initialize Msg4 incoming thread");
		_exit(1);
	}

	// test all collection dirs for write permission
	int32_t pcount = 0;
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		const CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		if ( ++pcount >= 100 ) {
			log("rdb: not checking directory permission for more than first 100 collections to save time.");
			break;
		}
		char tt[1024 + MAX_COLL_LEN ];
		sprintf ( tt , "%scoll.%s.%" PRId32, g_hostdb.m_dir, cr->m_coll , (int32_t)cr->m_collnum );
		checkDirPerms ( tt ) ;
	}

	//
	// NOTE: ANYTHING THAT USES THE PARSER SHOULD GO BELOW HERE, UCINIT!
	//

	// load the appropriate dictionaries
	if ( ! g_speller.init() && g_conf.m_isLive ) {
		_exit(1);
	}

	// Load the category language table
	g_countryCode.loadHashTable();

	// init minsitenuminlinks buffer
	if ( ! g_tagdb.loadMinSiteInlinksBuffer() ) {
		log("db: failed to load sitelinks.txt data");
		_exit(1);
	}

	// . then our main udp server
	// . must pass defaults since g_dns uses it's own port/instance of it
	// . server should listen to a socket and register with g_loop
	// . sock read/write buf sizes are both 64000
	// . poll time is 60ms
	// . if the read/write bufs are too small it severely degrades
	//   transmission times for big messages. just use ACK_WINDOW *
	//   MAX_DGRAM_SIZE as the size so when sending you don't drop dgrams
	// . the 400k size allows us to cover Sync.cpp's activity well
	if ( ! g_udpServer.init( g_hostdb.getMyPort() ,&g_dp,
				 40000000 ,   // readBufSIze
				 20000000 ,   // writeBufSize
				 20       ,   // pollTime in ms
				 g_conf.m_udpMaxSockets     ,   // max udp slots
				 false    )){ // is dns?
		log("db: UdpServer init failed." ); return 1; }

	// start up repair loop
	if ( ! g_repair.init() ) {
		log("db: Repair init failed." ); return 1; }

	// start up repair loop
	if ( ! g_dailyMerge.init() ) {
		log("db: Daily merge init failed." ); return 1; }

	// . then dns Distributed client
	// . server should listen to a socket and register with g_loop
	// . Only the distributed cache shall call the dns server.
	if ( ! g_dns.init( g_hostdb.m_myHost->m_dnsClientPort ) ) {
		log("db: Dns distributed client init failed." ); return 1; }

	// initialize dns client library
	if (!GbDns::initialize()) {
		log(LOG_ERROR, "Unable to initialize dns client");
		_exit(1);
	}

	g_stable_summary_cache.configure(g_conf.m_stableSummaryCacheMaxAge, g_conf.m_stableSummaryCacheSize);
	g_unstable_summary_cache.configure(g_conf.m_unstableSummaryCacheMaxAge, g_conf.m_unstableSummaryCacheSize);
	
	// . then webserver
	// . server should listen to a socket and register with g_loop
	if ( ! g_httpServer.init( g_hostdb.m_myHost->getInternalHttpPort(), g_hostdb.m_myHost->getInternalHttpsPort() ) ) {
		log("db: HttpServer init failed. Another gb already running?" );
		// this is dangerous!!! do not do the shutdown thing
		_exit(1);
	}

	// . now register all msg handlers with g_udp server
	if ( ! registerMsgHandlers() ) {
		log("db: registerMsgHandlers failed" ); return 1; }

	// gb dictLookupTest
	if ( strcmp ( cmd , "dictlookuptest" ) == 0 ) {	
		if ( argc != cmdarg + 2 ) {
			printHelp();
			return 1;
		}
		g_speller.dictLookupTest ( argv[cmdarg + 1] );
		_exit(0);
	}

	if(cmd[0] && cmd[0]!='-') {
		log(LOG_ERROR, "Unknown command: '%s'", cmd);
		_exit(1);
	}

	// . register a callback to try to merge everything every 60 seconds
	// . do not exit if we couldn't do this, not a huge deal
	// . put this in here instead of Rdb.cpp because we don't want generator commands merging on us
	// . niceness is 1
	// BR: Upped from 2 sec to 60. No need to check for merge every 2 seconds.
	if (!g_loop.registerSleepCallback(60000, NULL, attemptMergeAllCallback, "Rdb::attemptMergeAllCallback", 1)) {
		log( LOG_WARN, "db: Failed to init merge sleep callback." );
	}

	// try to sync parms (and collection recs) with host 0
	if (!g_loop.registerSleepCallback(1000, NULL, Parms::tryToSyncWrapper, "Parms::tryToSyncWrapper", 0)) {
		return 0;
	}

	if ( !Statistics::initialize() ) {
		return 0;
	}

	// initialize clients
	g_urlRealtimeClassification.initialize();
	g_queryLanguage.initialize();
	g_siteNumInlinks.initialize();
	
	if(!WantedChecker::initialize())
		return 0;
	
	if(!InstanceInfoExchange::initialize())
		return 0;

	// initialize doc process
	if (!g_docDelete.init()) {
		logError("Unwable to initialize doc delete");
		return 0;
	}

	if (!g_docDeleteUrl.init()) {
		logError("Unwable to initialize doc delete url");
		return 0;
	}

	if (!g_docRebuild.init()) {
		logError("Unwable to initialize doc rebuild");
		return 0;
	}

	if (!g_docRebuildUrl.init()) {
		logError("Unwable to initialize doc rebuild url");
		return 0;
	}

	if (!g_docReindex.init()) {
		logError("Unwable to initialize doc reindex");
		return 0;
	}

	if (!g_docReindexUrl.init()) {
		logError("Unwable to initialize doc reindex url");
		return 0;
	}

	// . start the spiderloop
	// . comment out when testing SpiderCache
	g_spiderLoop.init();

	// allow saving of conf again
	g_conf.m_save = true;

	if(g_conf.m_mlockAllCurrent || g_conf.m_mlockAllFuture) {
		log(LOG_DEBUG,"Locking memory");
		int rc;
		if(g_conf.m_mlockAllCurrent && g_conf.m_mlockAllFuture)
			rc = mlockall(MCL_CURRENT|MCL_FUTURE);
		else if(g_conf.m_mlockAllCurrent)
			rc = mlockall(MCL_CURRENT);
		else //if(g_conf.m_mlockAllFuture) //doesn't make a lot of sense to me
			rc = mlockall(MCL_FUTURE);
		if(rc!=0)
			log(LOG_WARN, "mlockall() failed with errno=%d (%s)", errno, mstrerror(errno));
	}

	log("db: gb is now ready");

	// . now start g_loops main interrupt handling loop
	// . it should block forever
	// . when it gets a signal it dispatches to a server or db to handle it
	g_loop.runLoop();
}


static void printHelp() {
	SafeBuf sb;
	sb.safePrintf(
		      "\n"
		      "Usage: gb [CMD]\n");
	sb.safePrintf(
		      "\n"
		      "\tgb will first try to load "
		      "the hosts.conf in the same directory as the "
		      "gb binary. "
		      "Then it will determine its hostId based on "
		      "the directory and IP address listed in the "
		      "hosts.conf file it loaded. Things in []'s "
		      "are optional.");
	sb.safePrintf(
		" [CMD] can have the following values:\n\n"

		"-h\tPrint this help.\n\n"
		"-v\tPrint version and exit.\n\n"

		//"<hostId>\n"
		//"\tstart the gb process for this <hostId> locally."
		//" <hostId> is 0 to run as host #0, for instance."
		//"\n\n"


		//"<hostId> -d\n\trun as daemon.\n\n"
		"-d\tRun as daemon.\n\n"

		//"-o\tprint the overview documentation in HTML. "
		//"Contains the format of hosts.conf.\n\n"

		// "<hostId> -r\n\tindicates recovery mode, "
		// "sends email to addresses "
		// "specified in Conf.h upon startup.\n\n"
		// "-r\tindicates recovery mode, "
		// "sends email to addresses "
		// "specified in Conf.h upon startup.\n\n"

		"start [hostId]\n"
		"\tStart the gb process on all hosts or just on "
		"[hostId], if specified, using an ssh command. Runs "
		"each gb process in a keepalive loop under bash.\n\n"

		"start <hostId1-hostId2>\n"
		"\tLike above but just start gb on the supplied "
		"range of hostIds.\n\n"

		"stop [hostId]\n"
		"\tSaves and exits for all gb hosts or "
		"just on [hostId], if specified.\n\n"

		"stop <hostId1-hostId2>\n"
		"\tTell gb to save and exit on the given range of "
		"hostIds.\n\n"

		"save [hostId]\n"
		"\tJust saves for all gb hosts or "
		"just on [hostId], if specified.\n\n"


		/*
		"tmpstart [hostId]\n"
		"\tstart the gb process on all hosts or just on "
		"[hostId] if specified, but "
		"use the ports specified in hosts.conf PLUS one. "
		"Then you can switch the "
		"proxy over to point to those and upgrade the "
		"original cluster's gb. "
		"That can be done in the Master Controls of the "
		"proxy using the 'use "
		"temporary cluster'. Also, this assumes the binary "
		"name is tmpgb not gb.\n\n"

		"tmpstop [hostId]\n"
		"\tsaves and exits for all gb hosts or "
		"just on [hostId] if specified, for the "
		"tmpstart command.\n\n"
		*/

		"spidersoff [hostId]\n"
		"\tDisables spidering for all gb hosts or "
		"just on [hostId], if specified.\n\n"

		"spiderson [hostId]\n"
		"\tEnables spidering for all gb hosts or "
		"just on [hostId], if specified.\n\n"

		/*
		"cacheoff [hostId]\n"
		"\tdisables all disk PAGE caches on all hosts or "
		"just on [hostId] if specified.\n\n"

		"freecache [maxShmid]\n"
		"\tfinds and frees all shared memory up to shmid "
		"maxShmid, default is 3000000.\n\n"
		*/

		/*
		"ddump [hostId]\n"
		"\tdump all b-trees in memory to sorted files on "
		"disk. "
		"Will likely trigger merges on files on disk. "
		"Restrict to just host [hostId] if given.\n\n"
		*/

		/*
		"pmerge [hostId|hostId1-hostId2]\n"
		"\tforce merge of posdb files "
		"just on [hostId] if specified.\n\n"

		"smerge [hostId|hostId1-hostId2]\n"
		"\tforce merge of sectiondb files "
		"just on [hostId] if specified.\n\n"

		"tmerge [hostId|hostId1-hostId2]\n"
		"\tforce merge of titledb files "
		"just on [hostId] if specified.\n\n"

		"merge [hostId|hostId1-hostId2]\n"
		"\tforce merge of all rdb files "
		"just on [hostId] if specified.\n\n"
		*/

		"dsh <CMD>\n"
		"\tRun this command on the primary IPs of "
		"all active hosts in hosts.conf. It will be "
		"executed in the gigablast working directory on "
		"each host. Example: "
		"gb dsh 'ps auxw; uptime'\n\n"

		/*
		"dsh2 <CMD>\n"
		"\trun this command on the secondary IPs of "
		"all active hosts in hosts.conf. Example: "
		"gb dsh2 'ps auxw; uptime'\n\n"
		*/

		"install [hostId]\n"
		"\tInstall all required files for gb from "
		"current working directory of the gb binary "
		"to [hostId]. If no [hostId] is specified, install "
		"to ALL hosts.\n\n"

		/*
		"install2 [hostId]\n"
		"\tlike above, but use the secondary IPs in the "
		"hosts.conf.\n\n"
		*/

		"installgb [hostId]\n"
		"\tLike above, but install just the gb executable.\n\n"

		"installfile <file>\n"
		"\tInstalls the specified file on all hosts\n\n"

		/*
		"installtmpgb [hostId]\n"
		"\tlike above, but install just the gb executable "
		"as tmpgb (for tmpstart).\n\n"
		*/

		"installconf [hostId]\n"
		"\tlike above, but install hosts.conf and gb.conf\n\n"
		/*
		"installconf2 [hostId]\n"
		"\tlike above, but install hosts.conf and gbN.conf "
		"to the secondary IPs.\n\n"

		"backupcopy <backupSubdir>\n"
		"\tsave a copy of all xml, config, data and map files "
		"into <backupSubdir> which is relative "
		"to the working dir. Done for all hosts.\n\n"

		"backupmove <backupSubdir>\n"
		"\tmove all all xml, config, data and map files "
		"into <backupSubdir> which  is relative "
		"to the working dir. Done for all hosts.\n\n"

		"backuprestore <backupSubdir>\n"
		"\tmove all all xml, config, data and map files "
		"in <backupSubdir>,  which is relative "
		"to the working dir, into the working dir. "
		"Will NOT overwrite anything. Done for all "
		"hosts.\n\n"

		"proxy start [proxyId]\n"
		"\tStart a proxy that acts as a frontend to gb "
		"and passes on requests to random machines on "
		"the cluster given in hosts.conf. Helps to "
		"distribute the load evenly across all machines.\n\n"

		"proxy load <proxyId>\n"
		"\tStart a proxy process directly without calling "
		"ssh. Called by 'gb proxy start'.\n\n"

		"proxy stop [proxyId]\n"
		"\tStop a proxy that acts as a frontend to gb.\n\n"
		*/

		/*
		"dictlookuptest <file>\n"
		"\tgets the popularities of the entries in the "
		"<file>. Used to only check performance of "
		"getPhrasePopularity.\n\n"

		// less common things
		"gendict <coll> [numWordsToDump]\n\tgenerate "
		"dictionary used for spellchecker "
		"from titledb files in collection <coll>. Use "
		"first [numWordsToDump] words.\n\n"

		//"update\tupdate titledb0001.dat\n\n"
		"treetest\n\ttree insertion speed test\n\n"

		"hashtest\n\tadd and delete into hashtable test\n\n"

		"parsetest <docIdToTest> [coll] [query]\n\t"
		"parser speed tests\n\n"
		*/

		/*
		// Quality Tests
		"countdomains <coll> <X>\n"
		"\tCounts the domains and IPs in collection coll and "
		"in the first X titledb records.  Results are sorted"
		"by popularity and stored in the log file. \n\n"

		"cachetest\n\t"
		"cache stability and speed tests\n\n"

		"dump e <coll> <UTCtimestamp>\n\tdump all events "
		"as if the time is UTCtimestamp.\n\n"

		"dump es <coll> <UTCtimestamp>\n\tdump stats for "
		"all events as if the time is UTCtimestamp.\n\n"
		*/

		"dump <db> <collection> <fileNum> <numFiles> <includeTree> [other stuff]\n\tDump a db from disk. "
		"Example: gb dump t main\n"
		"\t<collection> is the name of the collection.\n\n"

		"\tclusterdb:\n"
		"\t\tdump l <collection> <fileNum> <numFiles> <includeTree>\n"

		"\tdoledb:\n"
		"\t\tdump x <collection> <fileNum> <numFiles> <includeTree>\n"

		"\tlinkdb (site):\n"
		"\t\tdump Ls <collection> <fileNum> <numFiles> <includeTree> <url>\n"
		"\tlinkdb (url):\n"
		"\t\tdump Lu <collection> <fileNum> <numFiles> <includeTree> <url>\n"

		"\tposdb (the index):\n"
		"\t\tdump p <collection> <fileNum> <numFiles> <includeTree> <term-or-termId>\n"

		"\tspiderdb:\n"
		"\t\tdump s <collection> <firstIp>\n"

		"\ttagdb:\n"
		"\t\tdump S <collection> <fileNum> <numFiles> <includeTree> <site>\n"
		"\ttagdb (for wget):\n"
		"\t\tdump W <collection> <fileNum> <numFiles> <includeTree> <term-or-termId>\n"
		"\ttagdb (make sitelist.txt):\n"
		"\t\tdump z <collection> <fileNum> <numFiles> <includeTree> <site>\n"
		"\ttagdb (output HTTP commands for adding tags):\n"
		"\t\tdump A <collection> <fileNum> <numFiles> <includeTree> <term-or-termId>\n"

		"\ttitledb:\n"
		"\t\tdump t <collection> <fileNum> <numFiles> <includeTree> <docId>\n"
		"\ttitledb (Unwanted documents, checked against blocklist, plugins):\n"
		"\t\tdump u <collection> <fileNum> <numFiles> <includeTree>\n"
		"\ttitledb (Wanted documents, checked against blocklist, plugins):\n"
		"\t\tdump wt <collection> <fileNum> <numFiles> <includeTree>\n"
		"\ttitledb (duplicates only):\n"
		"\t\tdump at <collection> <fileNum> <numFiles> <includeTree>\n"
		"\ttitledb (Adult titlerecs):\n"
		"\t\tdump st <collection> <fileNum> <numFiles> <includeTree>\n"
		"\ttitledb (Spam titlerecs):\n"
		"\t\tdump D <collection> <fileNum> <numFiles> <includeTree> <docId>\n"
		"\twaiting tree:\n"
		"\t\tdump w <collection>\n"

		"\trobots.txt.cache:\n"
		"\t\tdump rtc <url>\n"
		"\n"
		"sitedeftemp\n"
		"\tPrepares or switches to a new site-default-page-temperature generation.\n"
		"\tsitedeftemp prepare\n"
		"\t\tPrepares a new site-default-page-temperature generation\n"
		"\tsitedeftemp switch\n"
		"\t\tSwitches to a new site-default-page-temperature generation previously prepared with 'sitedeftemp prepare'\n"
		"\n"
		);

	//word-wrap to screen width, if known
	struct winsize w;
	if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&w)==0 && w.ws_col>0) {
		SafeBuf sb2;
		sb2.brify2(sb.getBufStart(), w.ws_col, "\n\t", false);
		sb2.safeMemcpy("",1);
		fprintf(stdout,"%s",sb2.getBufStart());
	} else
		fprintf(stdout,"%s",sb.getBufStart());
	// disable printing of used memory
	//g_mem.m_used = 0;
}


/// @todo ALC wouldn't it be faster to actually check the dir permission instead of trying to write a tmp file?
int32_t checkDirPerms(const char *dir) {
	if ( g_conf.m_readOnlyMode ) {
		return 0;
	}

	File f;
	f.set ( dir , "tmpfile" );
	if ( ! f.open ( O_RDWR | O_CREAT | O_TRUNC ) ) {
		log( LOG_ERROR, "disk: Unable to create %stmpfile. Need write permission in this directory.", dir );
		return -1;
	}
	if ( ! f.unlink() ) {
		log( LOG_ERROR, "disk: Unable to delete %stmpfile. Need write permission in this directory.", dir );
		return -1;
	}
	return 0;
}


static bool argToBoolean(const char *arg) {
	return strcmp(arg,"1")==0 ||
	       strcmp(arg,"true")==0;
}

static bool parseOptionalHostRange(int rangearg, int argc, char **argv, int *h1, int *h2) {
	if(rangearg < argc) {
		int n = sscanf(argv[rangearg],"%u-%u", h1, h2);
		if(n==0) {
			fprintf(stderr,"Unrecognized host range: '%s'\n", argv[rangearg]);
			return false;
		} else if(n==1) {
			*h2 = -1;
		} else if(*h2<*h1) {
			fprintf(stderr,"host2<host1 in host range: '%s'\n", argv[rangearg]);
			return false;
		}
	} else {
		*h1 = -1;
		*h2 = -1;
	}
	return true;
}


// save them all
static       void doCmdAll   ( int fd, void *state ) ;
static       bool  s_sendToHosts;
static       bool  s_sendToProxies;
static       int32_t  s_hostId;
static       int32_t  s_hostId2;
static       char  s_buffer[128];
static HttpRequest s_r;
bool doCmd ( const char *cmd , int32_t hostId , const char *filename ,
	     bool sendToHosts , bool sendToProxies , int32_t hostId2 ) {

	//so we don't supporess messages to dead hosts (we're not connected to vagus)
	g_conf.m_doingCommandLine = true;

	// need loop to work
	if ( ! g_loop.init() ) {
		log(LOG_WARN, "db: Loop init failed." );
		return false;
	}

	// pass it on
	s_hostId = hostId;
	s_sendToHosts = sendToHosts;
	s_sendToProxies = sendToProxies;
	s_hostId2 = hostId2;
	// set stuff so http server client-side works right
	g_conf.m_httpMaxSockets = 512;
	sprintf ( g_conf.m_spiderUserAgent ,"GigablastOpenSource/1.0");
	sprintf ( g_conf.m_spiderBotName ,"gigablastopensource");


	// register sleep callback to get started
	if (!g_loop.registerSleepCallback(1, NULL, doCmdAll, "doCmdAll", 0)) {
		log(LOG_WARN, "admin: Loop init failed.");
		return false;
	}
	// not it
	log(LOG_INFO,"admin: broadcasting %s",cmd);
	// make a fake http request
	sprintf ( s_buffer , "GET /%s?%s HTTP/1.0" , filename , cmd );
	TcpSocket sock; 
	// make it local loopback so it passes the permission test in
	// doCmdAll()'s call to convertHttpRequestToParmList
	sock.m_ip = atoip("127.0.0.1");
	s_r.set ( s_buffer , strlen ( s_buffer ) , &sock );
	// do not do sig alarms! for now just set this to null so
	// the sigalarmhandler doesn't core
	//g_hostdb.m_myHost = NULL;

	// run the loop
	g_loop.runLoop();
}

[[ noreturn ]] void doneCmdAll ( void *state ) {
	log("cmd: completed command");
	exit ( 0 );
}


void doCmdAll ( int fd, void *state ) { 

	// do not keep calling it!
	g_loop.unregisterSleepCallback ( NULL, doCmdAll );

	// make port -1 to indicate none to listen on
	if ( ! g_udpServer.init( 18123 , // port to listen on
				 &g_dp,
				 20000000 ,   // readBufSIze
				 20000000 ,   // writeBufSize
				 20       ,   // pollTime in ms
				 3500     ,   // max udp slots
				 false    )){ // is dns?
		log("db: UdpServer init  on port 18123 failed: %s" ,
		    mstrerror(g_errno)); 
		exit(0);
	}

	// udpserver::sendRequest() checks we have a handle for msgs we send!
	// so fake it out with this lest it cores
	Parms::registerHandler3f();
	

	SafeBuf parmList;
	// returns false and sets g_errno on error
	if (!g_parms.convertHttpRequestToParmList(&s_r,&parmList,0,NULL)){
		log("cmd: error converting command: %s",mstrerror(g_errno));
		exit(0);
	}

	if ( parmList.length() <= 0 ) {
		log("cmd: no parmlist to send");
		exit(0);
	}

	// restrict broadcast to this hostid range!

	// returns true with g_errno set on error. uses g_udpServer
	if ( g_parms.broadcastParmList ( &parmList ,
					 NULL , 
					 doneCmdAll , // callback when done
					 s_sendToHosts ,
					 s_sendToProxies ,
					 s_hostId ,  // -1 means all
					 s_hostId2 ) ) { // -1 means all
		log("cmd: error sending command: %s",mstrerror(g_errno));
		exit(0);
	}
	// wait for it
	log("cmd: sent command");
}

static int install_file(const char *dst_host, const char *src_file, const char *dst_file)
{
	char cmd[1024];
	sprintf(cmd, "scp -p %s %s:%s", src_file, dst_host, dst_file);
	log(LOG_INIT,"admin: %s", cmd);
	int rc = system(cmd);
	return rc;
}


static int install_file(const char *file, int32_t hostId, int32_t hostId2) {
	// use hostId2 to indicate the range hostId-hostId2, but if it is -1
	// then it was not given, so restrict to just hostId
	if ( hostId2 == -1 ) {
		hostId2 = hostId;
	}

	for (int32_t i = 0; i < g_hostdb.getNumHosts(); i++) {
		Host *h2 = g_hostdb.getHost(i);
		if (h2 == g_hostdb.getMyHost()) {
			continue; //skip ourselves
		}

		// if doing a range of hostid, hostId2 is >= 0
		if (hostId >= 0 && hostId2 >= 0) {
			if (h2->m_hostId < hostId || h2->m_hostId > hostId2) {
				continue;
			}
		}

		char full_dst_file[1024];
		sprintf(full_dst_file, "%s%s", h2->m_dir, file);

		char ipbuf[16];
		install_file(iptoa(h2->m_ip, ipbuf), file, full_dst_file);
	}
	return 0; //return value is unclear
}


// installFlag is 1 if we are really installing, 2 if just starting up gb's
// installFlag should be a member of the ifk_ enum defined above
static int install ( install_flag_konst_t installFlag, int32_t hostId, char *dir, int32_t hostId2, char *cmd ) {

	// use hostId2 to indicate the range hostId-hostId2, but if it is -1
	// then it was not given, so restrict to just hostId
	if ( hostId2 == -1 ) {
		hostId2 = hostId;
	}

	char tmp[1024];
	if ( installFlag == ifk_proxy_start ) {
		for ( int32_t i = 0; i < g_hostdb.m_numProxyHosts; i++ ) {
			Host *h2 = g_hostdb.getProxy(i);
			// limit install to this hostId if it is >= 0
			if ( hostId >= 0 && h2->m_hostId != hostId ) continue;

			// . assume conf file name gbHID.conf
			// . assume working dir ends in a '/'
			//to test add: ulimit -t 10; to the ssh cmd
			char ipbuf[16];
			sprintf(tmp,
				"ssh %s \"cd %s ; "
				"export MALLOC_CHECK_=0;"
				"cp -f gb gb.oldsave ; "
				"mv -f gb.installed gb ; "
				"ADDARGS='' ; "
				"EXITSTATUS=1 ; "
				"while [ \\$EXITSTATUS != 0 ]; do "
 				"{ "
				"./gb proxy load %" PRId32" " // mdw
				"\\$ADDARGS "
				" >& ./proxylog ;"
				"EXITSTATUS=\\$? ; "
				"ADDARGS='-r' ; "
				"} " 
 				"done >& /dev/null & \" & ",
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir      ,
				h2->m_hostId   );
			// log it
			log(LOG_INIT,"admin: %s", tmp);
			// execute it
			int32_t ret = system ( tmp );
			if ( ret < 0 ) {
				fprintf(stderr,"Error loading proxy: %s\n",
					mstrerror(errno));
				exit(-1);
			}
			fprintf(stderr,"If proxy does not start, make sure "
				"its ip is correct in hosts.conf\n");
		}
		return 0;
	}

	HashTableX iptab;
	char tmpBuf[2048];
	iptab.set(4,4,64,tmpBuf,2048,true,"iptsu");

	int32_t maxOut = 500;

	// this is a big scp so only do two at a time...
	if  ( installFlag == ifk_install ) maxOut = 1;
	if  ( installFlag == ifk_installgb ) maxOut = 4;

	// go through each host
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		Host *h2 = g_hostdb.getHost(i);
		char ipbuf[16];

		const char *amp = " ";

		// if i is NOT multiple of maxOut then use '&'
		// even if all all different machines (IPs) scp chokes and so
		// does rcp a little. so restrict to maxOut at a time.
		if ( (i+1) % maxOut ) {
			amp = "&";
		}

		// if doing a range of hostid, hostId2 is >= 0
		if ( hostId >= 0 && hostId2 >= 0 ) {
			if ( h2->m_hostId < hostId || h2->m_hostId > hostId2 )
				continue;
		}

		// backupcopy
		if ( installFlag == ifk_backupcopy ) {
			sprintf(tmp,
				"ssh %s \"cd %s ; "
				"mkdir %s ; "
				"cp -ai *.dat* *.map gb.conf "
				"hosts.conf %s\" &",
				iptoa(h2->m_ip,ipbuf), h2->m_dir , dir , dir );
			// log it
			log ( "%s", tmp);
			// execute it
			system ( tmp );
			continue;
		}
		// backupmove
		else if ( installFlag == ifk_backupmove ) {
			sprintf(tmp,
				"ssh %s \"cd %s ; "
				"mkdir %s ; "
				"mv -i *.dat* *.map "
				"%s\" &",
				iptoa(h2->m_ip,ipbuf), h2->m_dir , dir , dir );
			// log it
			log ( "%s", tmp);
			// execute it
			system ( tmp );
			continue;
		}
		// backuprestore
		else if ( installFlag == ifk_backuprestore ) {
			sprintf(tmp,
				"ssh %s \"cd %s ; cd %s ; "
				"mv -i *.dat* *.map gb.conf "
				"hosts.conf %s\" &",
				iptoa(h2->m_ip,ipbuf), h2->m_dir , dir , h2->m_dir );
			// log it
			log ( "%s", tmp);
			// execute it
			system ( tmp );
			continue;
		}

		const char *dir = "./";
		// install to it
		if ( installFlag == ifk_install ) {
			const char *srcDir = "./";
			SafeBuf fileListBuf;
			g_process.getFilesToCopy ( srcDir , &fileListBuf );

			fileListBuf.safePrintf(" %shosts.conf",srcDir);
			fileListBuf.safePrintf(" %sgb.conf",srcDir);

			iptoa(h2->m_ip,ipbuf);

			SafeBuf tmpBuf;
			tmpBuf.safePrintf(
					  // ensure directory is there, if
					  // not then make it
					  "ssh %s 'mkdir -p %s' ; "
					  "scp -p -r %s %s:%s"
					  , ipbuf
					  , h2->m_dir

					  , fileListBuf.getBufStart()
					  , ipbuf
					  , h2->m_dir
					  );
			char *tmp = tmpBuf.getBufStart();
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		else if ( installFlag == ifk_installgb ) {
			File f;
			const char *target = "gb.new";
			f.set(g_hostdb.m_myHost->m_dir,target);
			if ( ! f.doesExist() ) target = "gb";

			sprintf(tmp,
				"scp -p " // blowfish is faster
				"%s%s "
				"%s:%s/gb.installed%s",
				dir,
				target,
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir,
				amp);
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		else if ( installFlag == ifk_installtmpgb ) {
			sprintf(tmp,
				"scp -p "
				"%sgb.new "
				"%s:%s/tmpgb.installed &",
				dir,
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir);
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		else if ( installFlag == ifk_installconf ) {
			sprintf(tmp,
				"scp -p %sgb.conf %shosts.conf %s:%s %s",
				dir ,
				dir ,
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir,
				amp);

			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		// start up a dummy cluster using hosts.conf ports + 1
		else if ( installFlag == ifk_tmpstart ) {
			// . assume conf file name gbHID.conf
			// . assume working dir ends in a '/'
			sprintf(tmp,
				"ssh %s \"cd %s ; "
				"cp -f tmpgb tmpgb.oldsave ; "
				"mv -f tmpgb.installed tmpgb ; "
				"%s/tmpgb tmpstarthost "
				"%" PRId32" >& ./tmplog%03" PRId32" &\" &",
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir      ,
				h2->m_dir      ,
				h2->m_hostId   ,
				h2->m_hostId   );
			// log it
			log(LOG_INIT,"admin: %s", tmp);
			// execute it
			system ( tmp );
		}
		else if ( installFlag == ifk_start ) {
			sprintf( tmp, "ssh %s '%sgbstart.sh %" PRId32"' %s", iptoa(h2->m_ip,ipbuf), h2->m_dir, h2->m_hostId, amp );

			// log it
			fprintf(stdout,"admin: %s\n", tmp);

			// execute it
			system ( tmp );
		}
		// dsh
		else if ( installFlag == ifk_dsh ) {
			sprintf(tmp,
				"ssh %s 'cd %s ; %s' %s",
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir,
				cmd ,
				amp );
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		// dsh2
		else if ( installFlag == ifk_dsh2 ) {
			sprintf(tmp,
				"ssh %s 'cd %s ; %s'",
				iptoa(h2->m_ip,ipbuf),
				h2->m_dir,
				cmd );
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
		// installconf2
		else if ( installFlag == ifk_installconf2 ) {
			sprintf(tmp,
				"rcp %sgb.conf %shosts.conf %shosts2.conf "
				"%s:%s &",
				dir ,
				dir ,
				dir ,
				iptoa(h2->m_ipShotgun,ipbuf),
				h2->m_dir);
			log(LOG_INIT,"admin: %s", tmp);
			system ( tmp );
		}
	}
	// return 0 on success
	return 0;
}

static bool registerMsgHandlers() {
	if (! registerMsgHandlers1()) return false;
	if (! registerMsgHandlers2()) return false;

	// in SpiderProxy.cpp...
	initSpiderProxyStuff();
	return true;
}

static bool registerMsgHandlers1() {
	if ( ! Msg20::registerHandler()) return false;
	if ( ! MsgC::registerHandler()) return false;

	if ( ! Msg22::registerHandler() ) return false;

	return true;
}

static bool registerMsgHandlers2() {
	if ( ! Msg0::registerHandler()) return false;

	if ( ! Msg13::registerHandler() ) return false;

	if ( ! Msg39::registerHandler()) return false;

	if ( ! Msg4In::registerHandler() ) return false;
	if ( ! Msg4::initializeOutHandling() ) return false;

	if(! Parms::registerHandler3e()) return false;
	if(! Parms::registerHandler3f()) return false;

	if ( ! g_udpServer.registerHandler(msg_type_25,handleRequest25)) return false;
	if ( ! g_udpServer.registerHandler(msg_type_7,handleRequest7)) return false;

	return true;
}

#include "Rdb.h"
#include "Xml.h"

//
// dump routines here now
//

void dumpTitledb (const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree,
                  int64_t docid , bool justPrintDups) {

	if(startFileNum!=0 && numFiles<0) {
		//this may apply to all files, but I haven't checked into hash-based ones yet
		fprintf(stderr,"If <startFileNum> is specified then <numFiles> must be too\n");
		return;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return;
	}
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); return ; }
	//g_conf.m_spiderdbMaxTreeMem = 1024*1024*30;
	//g_conf.m_spiderdbMaxDiskPageCacheMem   = 0;
	g_titledb.init ();
	//g_collectiondb.init(true);
	g_titledb.getRdb()->addRdbBase1(coll);
	key96_t startKey ;
	key96_t endKey   ;
	key96_t lastKey  ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();
	startKey = Titledb::makeFirstKey ( docid );
	Msg5 msg5;
	RdbList list;
	int64_t prevId = 0LL;
	int32_t count = 0;
	char ttt[2048+MAX_URL_LEN];
	HashTableX dedupTable;
	dedupTable.set(4,0,10000,NULL,0,false,"maintitledb");
	//g_synonyms.init();
	// load the appropriate dictionaries -- why???
	//g_speller.init(); 

	// make this
	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch(std::bad_alloc&) {
		fprintf(stdout,"could not alloc for xmldoc\n");
		exit(-1);
	}
	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if(cr==NULL) {
		fprintf(stderr,"Unknown collection '%s'\n", coll);
		return;
	}

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1            , // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) return;

		// loop over entries in list
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key96_t k       = list.getCurrentKey();
			char *rec     = list.getCurrentRec();
			int32_t  recSize = list.getCurrentRecSize();
			int64_t docId       = Titledb::getDocIdFromKey ( &k );
			if ( k <= lastKey )
				log("key out of order. "
				    "lastKey.n1=%" PRIx32" n0=%" PRIx64" "
				    "currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				    lastKey.n1,lastKey.n0,
				    k.n1,k.n0);
			lastKey = k;
			int32_t shard = g_hostdb.getShardNum ( RDB_TITLEDB , &k );
			// print deletes
			if ( (k.n0 & 0x01) == 0) {
				fprintf(stdout,"n1=%08" PRIx32" n0=%016" PRIx64" docId=%012" PRId64" "
				       "shard=%" PRId32" (del)\n",
					k.n1 , k.n0 , docId , shard );
				continue;
			}
			// free the mem
			xd->reset();
			// uncompress the title rec
			//TitleRec tr;
			if (!xd->set2(rec, recSize, coll, 0)) {
				//set2() may have logged something but not the docid
				log(LOG_WARN, "dbdump: XmlDoc::set2() failed for docid %" PRId64, docId);
				continue;
			}

			// extract the url
			Url *u = xd->getFirstUrl();

			// get ip
			char ipbuf [ 32 ];
			iptoa(u->getIp(),ipbuf);
			// pad with spaces
			int32_t blen = strlen(ipbuf);
			while ( blen < 15 ) ipbuf[blen++]=' ';
			ipbuf[blen]='\0';
			//int32_t nc = xd->size_catIds / 4;//tr.getNumCatids();
			if ( justPrintDups ) {
				// print into buf
				if ( docId != prevId ) {
					time_t ts = xd->m_spideredTime;//tr.getSpiderDa
					struct tm tm_buf;
					struct tm *timeStruct = localtime_r(&ts,&tm_buf);
					//struct tm *timeStruct = gmtime_r(&ts,&tm_buf);
					char ppp[100];
					strftime(ppp,100,"%b-%d-%Y-%H:%M:%S",
						 timeStruct);
					LinkInfo *info = xd->ptr_linkInfo1;//tr.ge
					char foo[1024];
					foo[0] = '\0';
					//if ( tr.getVersion() >= 86 ) 
					sprintf(foo,
						//"tw=%" PRId32" hw=%" PRId32" upw=%" PRId32" "
						"sni=%" PRId32" ",
						//(int32_t)xd->m_titleWeight,
						//(int32_t)xd->m_headerWeight,
						//(int32_t)xd->m_urlPathWeight,
						(int32_t)xd->m_siteNumInlinks);
					const char *ru = xd->ptr_redirUrl;
					if ( ! ru ) ru = "";
					char ipbuf2[16];
					sprintf(ttt,
						"n1=%08" PRIx32" n0=%016" PRIx64" docId=%012" PRId64" "
						//hh=%07" PRIx32" ch=%08" PRIx32" "
						"size=%07" PRId32" "
						"ch32=%010" PRIu32" "
						"clen=%07" PRId32" "
						"cs=%04d "
						"lang=%02d "
						"sni=%03" PRId32" "
						"lastspidered=%s "
						"ip=%s "
						"numLinkTexts=%04" PRId32" "
						"%s"
						"version=%02" PRId32" "
						//"maxLinkTextWeight=%06" PRIu32"%% "
						"redir=%s "
						"url=%s "
						"firstdup=1 "
						"shard=%" PRId32" "
						"\n", 
						k.n1 , k.n0 , 
						//rec[0] , 
						docId ,
						//hostHash ,
						//contentHash ,
						recSize - 16 ,
						(uint32_t)xd->m_contentHash32,
						xd->size_utf8Content,//tr.getContentLen
						xd->m_charset,//tr.getCharset(),
						xd->m_langId,//tr.getLanguage(),
						(int32_t)xd->m_siteNumInlinks,//tr.getDo
						//nc,
						ppp, 
						iptoa(xd->m_ip,ipbuf2),
						info->getNumGoodInlinks(),
						foo,
						(int32_t)xd->m_version,
						//ms,
						ru,
						u->getUrl() ,
						shard );
					prevId = docId;
					count = 0;
					continue;
				}
				// print previous docid that is same as our
				if ( count++ == 0 ) printf ( "\n%s" , ttt );
			}
			// nice, this is never 0 for a titlerec, so we can use 0 to signal
			// that the following bytes are not compressed, and we can store
			// out special checksum vector there for fuzzy deduping.
			//if ( rec[0] != 0 ) continue;
			// print it out
			//printf("n1=%08" PRIx32" n0=%016" PRIx64" b=0x%02hhx docId=%012" PRId64" sh=%07" PRIx32" ch=%08" PRIx32" "
			// date indexed as local time, not GMT/UTC
			time_t ts = xd->m_spideredTime;//tr.getSpiderDate();
			struct tm tm_buf;
			struct tm *timeStruct = localtime_r(&ts,&tm_buf);
			//struct tm *timeStruct = gmtime_r(&ts,&tm_buf);
			char ppp[100];
			strftime(ppp,100,"%b-%d-%Y-%H:%M:%S",timeStruct);

			LinkInfo *info = xd->ptr_linkInfo1;//tr.getLinkInfo();

			char foo[1024];
			foo[0] = '\0';
			sprintf(foo,
				"sni=%" PRId32" ",
				(int32_t)xd->m_siteNumInlinks);

			const char *ru = xd->ptr_redirUrl;
			if ( ! ru ) ru = "";

			char ipbuf2[16];
			fprintf(stdout,
				"n1=%08" PRIx32" n0=%016" PRIx64" docId=%012" PRId64" "
				"size=%07" PRId32" "
				"ch32=%010" PRIu32" "
				"clen=%07" PRId32" "
				"cs=%04d "
				"ctype=%s "
				"lang=%02d "
				"sni=%03" PRId32" "
				"lastspidered=%s "
				"ip=%s "
				"numLinkTexts=%04" PRId32" "
				"%s"
				"version=%02" PRId32" "
				"shard=%" PRId32" "
				"metadatasize=%" PRId32" "
				"redir=%s "
				"url=%s\n", 
				k.n1 , k.n0 , 
				docId ,
				recSize - 16 ,
				(uint32_t)xd->m_contentHash32,
				xd->size_utf8Content,//tr.getContentLen() ,
				xd->m_charset,//tr.getCharset(),
				g_contentTypeStrings[xd->m_contentType],
				xd->m_langId,//tr.getLanguage(),
				(int32_t)xd->m_siteNumInlinks,//tr.getDocQuality(),
				ppp,
				iptoa(xd->m_ip,ipbuf2),
				info->getNumGoodInlinks(),
				foo,
				(int32_t)xd->m_version,
				shard,
				0,
				ru,
				u->getUrl() );
			// free the mem
			xd->reset();
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) return;
	}
}

void dumpWaitingTree (const char *coll ) {
	RdbTree wt;
	if (!wt.set(0, -1, 20000000, true, "waittree2", "waitingtree", sizeof(key96_t))) {
		return;
	}

	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	// make dir
	char dir[500];
	sprintf(dir, "%scoll.%s.%" PRId32, g_hostdb.m_dir, coll, (int32_t)collnum);

	// load in the waiting tree, IPs waiting to get into doledb
	BigFile file;
	file.set(dir, "waitingtree-saved.dat");
	bool treeExists = file.doesExist() > 0;
	// load the table with file named "THISDIR/saved"
	RdbMem wm;
	if ( treeExists && !wt.fastLoad(&file, &wm) ) return;
	ScopedLock sl(wt.getLock());
	// the the waiting tree
	for (int32_t node = wt.getFirstNode_unlocked(); node >= 0; node = wt.getNextNode_unlocked(node)) {
		// get key
		const key96_t *key = reinterpret_cast<const key96_t*>(wt.getKey_unlocked(node));
		// get ip from that
		int32_t firstIp = (key->n0) & 0xffffffff;
		// get the time
		uint64_t spiderTimeMS = key->n1;
		// shift upp
		spiderTimeMS <<= 32;
		// or in
		spiderTimeMS |= (key->n0 >> 32);
		// get the rest of the data
		char ipbuf[16];

		time_t now_t = spiderTimeMS/1000;
		struct tm tm_buf;
		struct tm *stm = gmtime_r(&now_t,&tm_buf);

		fprintf(stdout,"time=%" PRIu64" (%04d-%02d-%02dT%02d:%02d:%02d.%03dZ) firstip=%s\n", spiderTimeMS, stm->tm_year+1900,stm->tm_mon+1,stm->tm_mday,stm->tm_hour,stm->tm_min,stm->tm_sec,(int)(spiderTimeMS%1000), iptoa(firstIp,ipbuf));
	}
}


void dumpDoledb (const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree){
	g_doledb.init ();
	g_doledb.getRdb()->addRdbBase1(coll );
	key96_t startKey ;
	key96_t endKey   ;
	startKey.setMin();
	endKey.setMax();
	Msg5 msg5;
	RdbList list;
	key96_t oldk; oldk.setMin();
	const CollectionRec *cr = g_collectiondb.getRec(coll);

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_DOLEDB    ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1,             // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) return;
		// loop over entries in list
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key96_t k    = list.getCurrentKey();
			if ( oldk > k ) 
				fprintf(stdout,"got bad key order. "
					"%" PRIx32"/%" PRIx64" > %" PRIx32"/%" PRIx64"\n",
					oldk.n1,oldk.n0,k.n1,k.n0);
			oldk = k;
			// get it
			const char *drec = list.getCurrentRec();
			// sanity check
			if ( (drec[0] & 0x01) == 0x00 ) {g_process.shutdownAbort(true); }
			// get spider rec in it
			const char *srec = drec + 12 + 4;

			struct tm *timeStruct ;
			char time[256];
			time_t ts = (time_t)Doledb::getSpiderTime(&k);
			struct tm tm_buf;
			timeStruct = gmtime_r(&ts,&tm_buf);
			strftime ( time , 256 , "%Y%m%d-%H%M%S UTC", timeStruct );

			// print doledb info first then spider request
			fprintf(stdout,"dolekey=%s (n1=%" PRIu32" n0=%" PRIu64") "
				"pri=%" PRId32" "
				"spidertime=%s(%" PRIu32") "
				"uh48=0x%" PRIx64"\n",
				KEYSTR(&k,12),
				k.n1,
				k.n0,
				(int32_t)Doledb::getPriority(&k),
				time,
				(uint32_t)Doledb::getSpiderTime(&k),
				Doledb::getUrlHash48(&k));
			fprintf(stdout,"spiderkey=");
			// print it
			Spiderdb::print ( srec );
			// the \n
			printf("\n");
			// must be a request -- for now, for stats
			if ( ! Spiderdb::isSpiderRequest((key128_t *)srec) ) {
				// error!
				continue;
			}
			// cast it
			const SpiderRequest *sreq = (const SpiderRequest *)srec;
			// skip negatives
			if ( (sreq->m_key.n0 & 0x01) == 0x00 ) { g_process.shutdownAbort(true); }
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) return;
	}
}



void dumpRobotsTxtCache(const char *url) {
	struct HttpCacheData {
		int32_t m_errno;
		char *ptr_reply;
		int32_t size_reply;
	} __attribute__((packed));

	if( !url || strlen(url) <= 0 ) {
		fprintf(stdout, "robots.txt.cache lookup failed, you must supply a url as parameter\n");
		return;
	}

	// Generate robots.txt url
	Url u;
	u.set(url);

	// build robots.txt url
	char urlRobots[MAX_URL_LEN+1];
	char *p = urlRobots;
	if ( ! u.getScheme() )
	{
		p += sprintf ( p , "http://" );
	}
	else
	{
		gbmemcpy ( p , u.getScheme() , u.getSchemeLen() );
		p += u.getSchemeLen();
		p += sprintf(p,"://");
	}

	gbmemcpy ( p , u.getHost() , u.getHostLen() );
	p += u.getHostLen();

	// add port if not default
	if ( u.getPort() != u.getDefaultPort() ) {
		p += sprintf( p, ":%" PRId32, u.getPort() );
	}
	p += sprintf ( p , "/robots.txt" );



	fprintf(stdout, "robots.txt.cache lookup of %s\n", urlRobots);

	RdbCache httpCacheRobots;
	int32_t memRobots = 3000000;
	int32_t maxCacheNodesRobots = memRobots / 106;

	if ( ! httpCacheRobots.init ( memRobots ,
					-1        , // fixedDataSize
					maxCacheNodesRobots ,
					"robots.txt"  , // dbname
					true,          // load from disk
					12,            // cachekeysize
					-1)) {           // numPtrsMax
		fprintf(stdout, "Could not initialize local robots.txt.cache\n");
		return;
	}

	int32_t numElem = httpCacheRobots.getNumUsedNodes();
	fprintf(stdout,"%" PRId32 " elements in cache.\n", numElem);

	char *rec;
	int32_t  recSize;
	key96_t k;
	k.n1 = 0;
	k.n0 = hash64(urlRobots, strlen(urlRobots));
	k.n0 ^= 0xff;	// for compressed keys

	int64_t uh48 = k.n0 & 0x0000ffffffffffffLL;
	fprintf(stdout, "Cache key=%" PRIu64 ", uh48=%" PRIu64 "\n", k.n0, uh48);

	bool inCache = httpCacheRobots.getRecord ( (collnum_t)0     , // share btwn colls
					k                , // cacheKey
					&rec             ,
					&recSize         ,
					true             , // copy?
					9999999,		//r->m_maxCacheAge , // 24*60*60 ,
					false); // stats?
	fprintf(stdout, "Found: %s\n", inCache?"true":"false");

	if( inCache ) {
		HttpCacheData *httpCacheData = reinterpret_cast<HttpCacheData*>(rec);

		if( deserializeMsg(sizeof(*httpCacheData), &httpCacheData->size_reply, &httpCacheData->size_reply, &httpCacheData->ptr_reply, ((char*)httpCacheData + sizeof(*httpCacheData))) != -1) {
			fprintf(stdout, "deserializeMsg OK. errno=%" PRId32 ", size_reply=%" PRId32 "\n", httpCacheData->m_errno, httpCacheData->size_reply);

			// get uncompressed size
			uint32_t unzippedLen = *(int32_t*)httpCacheData->ptr_reply;
			// sanity checks
			if ( unzippedLen > 10000000 ) {
				fprintf(stdout, "Unzipped length appears too big: %" PRId32 "\n", unzippedLen);
				return;
			}
			// make buffer to hold uncompressed data
			char *newBuf = (char*)mmalloc(unzippedLen, "DumpUnzip");
			if( ! newBuf ) {
				fprintf(stdout, "Could not allocate memory for uncompressed document: %" PRId32 "\n", unzippedLen);
				return;
			}
			// make another var to get mangled by gbuncompress
			uint32_t uncompressedLen = unzippedLen;
			// uncompress it
			int zipErr = gbuncompress( (unsigned char*)newBuf, // dst
						   &uncompressedLen, // dstLen
						   (unsigned char*)httpCacheData->ptr_reply+4, // src
						   httpCacheData->size_reply-4); // srcLen

			if(zipErr != Z_OK || uncompressedLen != (uint32_t)unzippedLen) {
				fprintf(stdout, "Error unzipping compressed robots.txt unzipped len should be %" PRId32" but is %" PRId32". ziperr=%" PRId32,
					(int32_t)uncompressedLen, (int32_t)unzippedLen, (int32_t)zipErr);
				mfree(newBuf, unzippedLen, "DumpUnzip");
				return;
			}

			fprintf(stdout,"\n%s\n\n", newBuf);
			mfree(newBuf, unzippedLen, "DumpUnzip");
		}
		else {
			fprintf(stderr,"deserialize failed\n");
		}
	}
}

#if 0
static int32_t dumpSpiderdbCsv(const char *coll) {
	g_spiderdb.init();
	g_spiderdb.getRdb()->addRdbBase1(coll);

	key128_t startKey;
	startKey.setMin();

	Msg5 msg5;
	RdbList list;

	unsigned count = 0;
	const SpiderReply *prevSpiderReply = NULL;
	char prevSpiderReplyBuf[sizeof(SpiderReply)+MAX_URL_LEN+100];
	int64_t prevSpiderReplyUrlHash48 = 0LL;
	int64_t prevRequestUh48 = 0;

	const CollectionRec *cr = g_collectiondb.getRec(coll);

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if( ! msg5.getList(RDB_SPIDERDB,
				   cr->m_collnum,
				   &list,
				   &startKey,
				   KEYMAX(),
				   commandLineDumpdbRecSize,
				   true,           //includeTree
				   0,              //startFileNum
				   -1,             //numFiles
				   NULL,           // state
				   NULL,           // callback
				   0,              // niceness
				   false,          // err correction?
				   -1,             // maxRetries
				   false))         // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return -1;
		}
		// all done if empty
		if(list.isEmpty())
			break;

		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			count++;
			if((count % 100000) == 0) {
				fprintf( stderr, "Processed %u records.\n", count - 1);
			}

			const char *srec = list.getCurrentRec();

			if(Spiderdb::isSpiderReply((const key128_t *)srec)) {
				const SpiderReply *srep = reinterpret_cast<const SpiderReply *>(srec);
				prevSpiderReplyUrlHash48 = srep->getUrlHash48();
				prevSpiderReply = srep;
			} else if(prevRequestUh48==Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec))) {
				//skip duplicate
			} else {
				const SpiderRequest *spiderRequest = reinterpret_cast<const SpiderRequest*>(srec);

				int64_t uh48 = spiderRequest->getUrlHash48();
				// count how many requests had replies and how many did not
				bool hadReply = prevSpiderReply && (uh48 == prevSpiderReplyUrlHash48);

				if( !hadReply ) {
					// Last reply and current request do not belong together
					prevSpiderReply = NULL;
				}
				
				prevRequestUh48 = spiderRequest->getUrlHash48();

				// print it
				printf("%u,",spiderRequest->m_firstIp);
				printf("%lu,",spiderRequest->getUrlHash48());
				printf("%u,",spiderRequest->m_hostHash32);
				printf("%u,",spiderRequest->m_domHash32);
				printf("%u,",spiderRequest->m_siteHash32);
				printf("%d,",spiderRequest->m_siteNumInlinks);
				printf("%d,",spiderRequest->m_addedTime);
				printf("%d,",spiderRequest->m_pageNumInlinks);
				printf("%d,",spiderRequest->m_sameErrCount);
				printf("%d,",spiderRequest->m_version);
				printf("%d,",spiderRequest->m_discoveryTime);
				printf("%d,",spiderRequest->m_contentHash32);
				printf("%d,",spiderRequest->m_hopCount);
				printf("%d,",spiderRequest->m_recycleContent);
				printf("%d,",spiderRequest->m_hopCountValid);
				printf("%d,",spiderRequest->m_isAddUrl);
				printf("%d,",spiderRequest->m_isPageReindex);
				printf("%d,",spiderRequest->m_isUrlCanonical);
				printf("%d,",spiderRequest->m_isPageParser);
				printf("%d,",spiderRequest->m_urlIsDocId);
				printf("%d,",spiderRequest->m_isRSSExt);
				printf("%d,",spiderRequest->m_isUrlPermalinkFormat);
				printf("%d,",spiderRequest->m_forceDelete);
				printf("%d,",spiderRequest->m_isInjecting);
				printf("%d,",spiderRequest->m_hadReply);
				printf("%d,",spiderRequest->m_fakeFirstIp);
				printf("%d,",spiderRequest->m_hasAuthorityInlink);
				printf("%d,",spiderRequest->m_hasAuthorityInlinkValid);
				printf("%d,",spiderRequest->m_siteNumInlinksValid);
				printf("%d,",spiderRequest->m_avoidSpiderLinks);
				printf("%d,",spiderRequest->m_ufn);
				printf("%d,",spiderRequest->m_priority);
				printf("%d,",spiderRequest->m_errCount);
				printf("\"%s\",",spiderRequest->m_url);

				if(prevSpiderReply) {
					// Only dump these values if last reply and current request belong together
					//printf("%d,",prevSpiderReply->m_firstIp);
					//printf("%d,",prevSpiderReply->m_siteHash32);
					//printf("%d,",prevSpiderReply->m_domHash32);
					printf("%f,",prevSpiderReply->m_percentChangedPerDay);
					printf("%d,",prevSpiderReply->m_spideredTime);
					printf("%d,",prevSpiderReply->m_errCode);
					//printf("%d,",prevSpiderReply->m_siteNumInlinks);
					//printf("%d,",prevSpiderReply->m_sameErrCount);
					//printf("%d,",prevSpiderReply->m_version);
					//printf("%d,",prevSpiderReply->m_contentHash32);
					printf("%d,",prevSpiderReply->m_crawlDelayMS);
					printf("%ld,",prevSpiderReply->m_downloadEndTime);
					printf("%d,",prevSpiderReply->m_httpStatus);
					//printf("%d,",prevSpiderReply->m_errCount);
					printf("%d,",prevSpiderReply->m_langId);
					printf("%d,",prevSpiderReply->m_isRSS);
					printf("%d,",prevSpiderReply->m_isPermalink);
					printf("%d,",prevSpiderReply->m_isIndexed);
					printf("%d,",prevSpiderReply->m_isIndexedINValid);
					printf("%d,",prevSpiderReply->m_fromInjectionRequest);
				} else {
					printf(",,,,,,,,,,,");
				}
				printf("\n");
			}
		}
		
		//copy prevspiderreply to tmp buf, so we can rememerb the value to next list
		if(prevSpiderReply && sizeof(key128_t)+prevSpiderReply->m_dataSize < sizeof(prevSpiderReplyBuf)) {
			memcpy(prevSpiderReplyBuf, prevSpiderReply, sizeof(key128_t)+prevSpiderReply->m_dataSize);
			prevSpiderReply = reinterpret_cast<const SpiderReply*>(prevSpiderReplyBuf);
		} else
			prevSpiderReply = NULL;

		const key128_t *listLastKey = reinterpret_cast<const key128_t *>(list.getLastKey());
		startKey = *listLastKey;
		startKey++;

		// watch out for wrap around
		if ( startKey < *listLastKey)
			break;
	}
	return 0;
}
#endif

// time speed of inserts into RdbTree for indexdb
static bool hashtest() {
	// load em up
	int32_t numKeys = 1000000;
	log("db: speedtest: generating %" PRId32" random keys.",numKeys);
	// seed randomizer
	srand ( (int32_t)gettimeofdayInMilliseconds() );
	// make list of one million random keys
	key96_t *k = (key96_t *)mmalloc ( sizeof(key96_t) * numKeys , "main" );
	if ( ! k ) {
		log(LOG_WARN, "hashtest: malloc failed");
		return false;
	}
	int32_t *r = (int32_t *)(void*)k;
	for ( int32_t i = 0 ; i < numKeys * 3 ; i++ ) r[i] = rand();
	// init the tree
	//HashTableT<int32_t,int32_t> ht;
	HashTable ht;
	ht.set ( (int32_t)(1.1 * numKeys) );
	// add to regular tree
	int64_t t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < numKeys ; i++ ) 
		if ( ! ht.addKey ( r[i] , 1 ) ) {
			log(LOG_WARN, "hashtest: add key failed.");
			return false;
		}
	// print time it took
	int64_t e = gettimeofdayInMilliseconds();
	// add times
	log("db: added %" PRId32" keys in %" PRId64" ms",numKeys,e - t);

	// do the delete test
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < numKeys ; i++ ) 
		if ( ! ht.removeKey ( r[i] ) ) {
			log(LOG_WARN, "hashtest: add key failed.");
			return false;
		}
	// print time it took
	e = gettimeofdayInMilliseconds();
	// add times
	log("db: deleted %" PRId32" keys in %" PRId64" ms",numKeys,e - t);

	return true;
}


static void dumpTagdb(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree, char req,
		      const char *siteArg) {
	g_tagdb.init ();

	g_tagdb.getRdb()->addRdbBase1(coll );

	key128_t startKey ;
	key128_t endKey   ;
	startKey.setMin();
	endKey.setMax();
	if ( siteArg ) {
		startKey = Tagdb::makeStartKey ( siteArg );
		endKey = Tagdb::makeEndKey ( siteArg );
		log("gb: using site %s for start key",siteArg );
	}

	Msg5 msg5;
	RdbList list;

	const CollectionRec *cr = g_collectiondb.getRec(coll);

	int64_t hostHash = -1;
	int64_t lastHostHash = -2;
	const char *site = NULL;
	char sbuf[1024*2];
	int32_t siteNumInlinks = -1;
	int32_t typeSite = hash64Lower_a("site",4);
	int32_t typeInlinks = hash64Lower_a("sitenuminlinks",14);

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TAGDB,
				      cr->m_collnum      ,
				      &list         ,
				      (char *)&startKey      ,
				      (char *)&endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1,             // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) return;
		// loop over entries in list
		for(list.resetListPtr();!list.isExhausted(); list.skipCurrentRecord()){
			char *rec  = list.getCurrentRec();
			//key96_t k    = list.getCurrentKey();
			key128_t k;
			list.getCurrentKey ( &k );
			char *data = list.getCurrentData();
			int32_t  size = list.getCurrentDataSize();
			// is it a delete?
			if ( (k.n0 & 0x01) == 0 ) {
				if ( req == 'z' ) continue;
				printf("k.n1=%016" PRIx64" "
				       "k.n0=%016" PRIx64" (delete)\n",
				       k.n1  , k.n0   | 0x01  );  // fix it!
				continue;
			}
			// point to the data
			const char  *p       = data;
			const char  *pend    = data + size;
			// breach check
			if ( p >= pend ) {
				printf("corrupt tagdb rec k.n0=%" PRIu64,k.n0);
				continue;
			}

			// parse it up
			Tag *tag = (Tag *)rec;

			// print the version and site
			StackBuf<1024> sb;

			bool match = false;

			hostHash = tag->m_key.n1;

			if ( hostHash == lastHostHash ) {
				match = true;
			}
			else {
				site = NULL;
				siteNumInlinks = -1;
			}

			lastHostHash = hostHash;

			// making sitelist.txt?
			if ( tag->m_type == typeSite && req == 'z' ) {
				site = tag->getTagData();
				// make it null if too many .'s
				if ( site ) {
					const char *p = site;
					int count = 0;
					int alpha = 0;
					int colons = 0;
					// foo.bar.baz.com is ok
					for ( ; *p ; p++ ) {
						if ( *p == '.' ) count++;
						if ( *p == ':' ) colons++;
						if ( is_alpha_a(*p) || *p=='-' ) 
							alpha++;
					}
					if ( count >= 4 )
						site = NULL;
					if ( colons > 1 )
						site = NULL;
					// no ip addresses allowed, need an alpha char
					if ( alpha == 0 )
						site = NULL;
				}
				// ends in :?
				int slen = 0;
				if ( site ) slen = strlen(site);
				if ( site && site[slen-1] == ':' )
					site = NULL;
				// port bug
				if ( site && site[slen-2] == ':' && site[slen-1]=='/')
					site = NULL;
				// remove heavy spammers to save space
				if ( site && strstr(site,"daily-camshow-report") )
					site = NULL;
				if ( site && strstr(site,".livejasminhd.") )
					site = NULL;
				if ( site && strstr(site,".pornlivenews.") )
					site = NULL;
				if ( site && strstr(site,".isapornblog.") )
					site = NULL;
				if ( site && strstr(site,".teen-model-24.") )
					site = NULL;
				if ( site && ! is_ascii2_a ( site, strlen(site) ) ) {
					site = NULL;
					continue;
				}
				if ( match && siteNumInlinks>=0) {
					// if we ask for 1 or 2 we end up with 100M
					// entries, but with 3+ we get 27M
					if ( siteNumInlinks > 2 && site )
						printf("%i %s\n",siteNumInlinks,site);
					siteNumInlinks = -1;
					site = NULL;
				}
				// save it
				if ( site ) strcpy ( sbuf , site );
				continue;
			}

			if ( tag->m_type == typeInlinks && req == 'z' ) {
				siteNumInlinks = atoi(tag->getTagData());
				if ( match && site ) {
					// if we ask for 1 or 2 we end up with 100M
					// entries, but with 3+ we get 27M
					if ( siteNumInlinks > 2 )
						printf("%i %s\n",siteNumInlinks,sbuf);
					siteNumInlinks = -1;
					site = NULL;
				}
				continue;
			}

			if ( req == 'z' )
				continue;

			// print as an add request or just normal
			if ( req == 'A' ) tag->printToBufAsAddRequest ( &sb );
			else              tag->printToBuf             ( &sb );

			// dump it
			printf("%s\n",sb.getBufStart());

		}

		startKey = *(key128_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key128_t *)list.getLastKey() ){ 
			printf("\n"); return;}
	}
}

static void dumpUnwantedTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree) {

	if(startFileNum!=0 && numFiles<0) {
		//this may apply to all files, but I haven't checked into hash-based ones yet
		fprintf(stderr,"If <startFileNum> is specified then <numFiles> must be too\n");
		return;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return;
	}
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." );
		return;
	}

	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1(coll);
	key96_t startKey ;
	key96_t endKey   ;
	key96_t lastKey  ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();
	startKey = Titledb::makeFirstKey(0);
	Msg5 msg5;
	RdbList list;
	HashTableX dedupTable;
	dedupTable.set(4,0,10000,NULL,0,false,"maintitledb");

	// make this
	XmlDoc *xd;
	try {
		xd = new (XmlDoc);
	}
	catch(std::bad_alloc&) {
		fprintf(stdout,"could not alloc for xmldoc\n");
		exit(-1);
	}

	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if(cr==NULL) {
		fprintf(stderr,"Unknown collection '%s'\n", coll);
		return;
	}

	// initialize shlib & blacklist
	if (!WantedChecker::initialize()) {
		fprintf(stderr, "Unable to initialize WantedChecker");
		return;
	}

	g_urlBlackList.init();
	g_urlWhiteList.init();

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1            , // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) {
			return;
		}

		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			key96_t k = list.getCurrentKey();
			char *rec = list.getCurrentRec();
			int32_t recSize = list.getCurrentRecSize();
			int64_t docId = Titledb::getDocIdFromKey(&k);

			if ( k <= lastKey ) {
				log("key out of order. lastKey.n1=%" PRIx32" n0=%" PRIx64" currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				    lastKey.n1, lastKey.n0, k.n1, k.n0);
			}

			lastKey = k;

			if ( (k.n0 & 0x01) == 0) {
				// delete key
				continue;
			}

			// free the mem
			xd->reset();

			// uncompress the title rec
			if (!xd->set2(rec, recSize, coll, 0)) {
				//set2() may have logged something but not the docid
				log(LOG_WARN, "dbdump: XmlDoc::set2() failed for docid %" PRId64, docId);
				continue;
			}

			// extract the url
			Url *url = xd->getFirstUrl();
			const char *reason = NULL;

			if (isUrlUnwanted(*url, &reason)) {
				fprintf(stdout, "%" PRId64"|%s|%s\n", docId, reason, url->getUrl());
				continue;
			}

			Url **redirUrlPtr = xd->getRedirUrl();
			if (redirUrlPtr && *redirUrlPtr) {
				Url *redirUrl = *redirUrlPtr;
				if (isUrlUnwanted(*redirUrl, &reason)) {
					fprintf(stdout, "%" PRId64"|redir %s|%s|%s\n", docId, reason, url->getUrl(), redirUrl->getUrl());
					continue;
				}
			}

			uint8_t *contentType = xd->getContentType();
			switch (*contentType) {
				case CT_GIF:
				case CT_JPG:
				case CT_PNG:
				case CT_TIFF:
				case CT_BMP:
				case CT_JS:
				case CT_CSS:
				case CT_JSON:
				case CT_IMAGE:
				case CT_GZ:
				case CT_ARC:
				case CT_WARC:
					fprintf(stdout, "%" PRId64"|blocked content type|%s\n", docId, url->getUrl());
					continue;
				default:
					break;
			}

			// check content
			int32_t contentLen = xd->size_utf8Content > 0 ? (xd->size_utf8Content - 1) : 0;
			if (contentLen > 0) {
				if (!WantedChecker::check_single_content(url->getUrl(), xd->ptr_utf8Content, contentLen).wanted) {
					fprintf(stdout, "%" PRId64"|blocked content|%s\n", docId, url->getUrl());
					continue;
				}
			}
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			return;
		}
	}
}


static void dumpWantedTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree) {

	if(startFileNum!=0 && numFiles<0) {
		//this may apply to all files, but I haven't checked into hash-based ones yet
		fprintf(stderr,"If <startFileNum> is specified then <numFiles> must be too\n");
		return;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return;
	}
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." );
		return;
	}

	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1(coll);
	key96_t startKey ;
	key96_t endKey   ;
	key96_t lastKey  ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();
	startKey = Titledb::makeFirstKey(0);
	Msg5 msg5;
	RdbList list;
	HashTableX dedupTable;
	dedupTable.set(4,0,10000,NULL,0,false,"maintitledb");

	// make this
	XmlDoc *xd;
	try {
		xd = new (XmlDoc);
	}
	catch(std::bad_alloc&) {
		fprintf(stdout,"could not alloc for xmldoc\n");
		exit(-1);
	}

	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if(cr==NULL) {
		fprintf(stderr,"Unknown collection '%s'\n", coll);
		return;
	}

	// initialize shlib & blacklist
	if (!WantedChecker::initialize()) {
		fprintf(stderr, "Unable to initialize WantedChecker");
		return;
	}

	g_urlBlackList.init();
	g_urlWhiteList.init();

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1            , // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) {
			return;
		}

		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			key96_t k = list.getCurrentKey();
			char *rec = list.getCurrentRec();
			int32_t recSize = list.getCurrentRecSize();
			int64_t docId = Titledb::getDocIdFromKey(&k);

			if ( k <= lastKey ) {
				log("key out of order. lastKey.n1=%" PRIx32" n0=%" PRIx64" currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				    lastKey.n1, lastKey.n0, k.n1, k.n0);
			}

			lastKey = k;

			if ( (k.n0 & 0x01) == 0) {
				// delete key
				continue;
			}

			// free the mem
			xd->reset();

			// uncompress the title rec
			if (!xd->set2(rec, recSize, coll, 0)) {
				//set2() may have logged something but not the docid
				log(LOG_WARN, "dbdump: XmlDoc::set2() failed for docid %" PRId64, docId);
				continue;
			}

			// extract the url
			Url *url = xd->getFirstUrl();
			const char *reason = NULL;

			if( ! isUrlUnwanted(*url, &reason)) {
				Url **redirUrlPtr = xd->getRedirUrl();
				if (redirUrlPtr && *redirUrlPtr) {
					Url *redirUrl = *redirUrlPtr;
					if (isUrlUnwanted(*redirUrl, &reason)) {
						continue;
					}
				}

				fprintf(stdout, "%" PRId64 "|%s\n", docId, url->getUrl());
			}
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			return;
		}
	}
}


static void dumpAdultTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree) {
	if(startFileNum!=0 && numFiles<0) {
		//this may apply to all files, but I haven't checked into hash-based ones yet
		fprintf(stderr,"If <startFileNum> is specified then <numFiles> must be too\n");
		return;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return;
	}
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." );
		return;
	}

	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1(coll);
	key96_t startKey ;
	key96_t endKey   ;
	key96_t lastKey  ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();
	startKey = Titledb::makeFirstKey(0);
	Msg5 msg5;
	RdbList list;

	// make this
	XmlDoc *xd;
	try {
		xd = new (XmlDoc);
	}
	catch(std::bad_alloc&) {
		fprintf(stdout,"could not alloc for xmldoc\n");
		exit(-1);
	}

	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if(cr==NULL) {
		fprintf(stderr,"Unknown collection '%s'\n", coll);
		return;
	}

	// initialize shlib & blacklist
	if (!WantedChecker::initialize()) {
		fprintf(stderr, "Unable to initialize WantedChecker");
		return;
	}
	g_urlBlackList.init();
	g_urlWhiteList.init();

	g_checkAdultList.init("adultwords.txt", "adultphrases.txt");

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1            , // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) {
			return;
		}

		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			key96_t k = list.getCurrentKey();
			char *rec = list.getCurrentRec();
			int32_t recSize = list.getCurrentRecSize();
			int64_t docId = Titledb::getDocIdFromKey(&k);

			if ( k <= lastKey ) {
				log("key out of order. lastKey.n1=%" PRIx32" n0=%" PRIx64" currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				    lastKey.n1, lastKey.n0, k.n1, k.n0);
			}

			lastKey = k;

			if ( (k.n0 & 0x01) == 0) {
				// delete key
				continue;
			}

			// free the mem
			xd->reset();

			// uncompress the title rec
			if (!xd->set2(rec, recSize, coll, 0)) {
				//set2() may have logged something but not the docid
				log(LOG_WARN, "dbdump: XmlDoc::set2() failed for docid %" PRId64, docId);
				continue;
			}

			// extract the url
			Url *url = xd->getFirstUrl();
			if( url == (void *)-1 ) {
				log(LOG_WARN, "dbdump: XmlDoc::getFirstUrl() failed for docid %" PRId64, docId);
				continue;
			}

			const char *reason = NULL;

			// Don't dump unwanted URLs
			if( ! isUrlUnwanted(*url, &reason)) {
				Url **redirUrlPtr = xd->getRedirUrl();
				if (redirUrlPtr && *redirUrlPtr) {
					Url *redirUrl = *redirUrlPtr;
					if (isUrlUnwanted(*redirUrl, &reason)) {
						continue;
					}
				}

				// Get adult flag including generating debug info.
				// Could just call xd->getIsAdult() to get the simple indicator
				// without debug information.
				CheckAdult achk(xd, true);
				bool newblocked = achk.isDocAdult();

#if 0
				// Sanity check.
				bool gbadult = false;
				char *adultbit = xd->getIsAdult();
				if( adultbit ) {
					if( *adultbit != newblocked ) {
						// Mismatch - should never happen
						log(LOG_ERROR, "Adult check mismatch! docid=%" PRId64 ", url=%s, gbadult=%s, score=%" PRId32 ", newblock=%s",
						docId, url->getUrl(), gbadult?"true":"false", achk.getScore(), newblocked?"true":"false");
						gbshutdownLogicError();
					}
				}
#endif

				if( newblocked ) {
					time_t idxtim = (time_t)xd->getIndexedTime();
					struct tm tm_buf;
					tm *tm1 = gmtime_r(&idxtim,&tm_buf);
					char idxtim_s[64];
					strftime(idxtim_s,64,"%Y%m%d-%H%M%S",tm1);

					fprintf(stdout, "%" PRId64 "\t%s\t%s\t%s\tscore=%" PRId32 "\tunique dw=%" PRId32 "\tunique dp=%" PRId32 "\twords=%" PRId32 "\t%s\t%s\n",
						docId, url->getUrl(), idxtim_s, achk.getReason(),
						achk.getScore(), achk.getNumUniqueMatchedWords(), achk.getNumUniqueMatchedPhrases(),
						achk.getNumWordsChecked(), achk.hasEmptyDocumentBody()?"EMPTYDOC":"HASCONTENT", achk.getDebugInfo());
				}
			}
		}

		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			return;
		}
	}
}



static void dumpSpamTitledbRecs(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree) {
	if(startFileNum!=0 && numFiles<0) {
		//this may apply to all files, but I haven't checked into hash-based ones yet
		fprintf(stderr,"If <startFileNum> is specified then <numFiles> must be too\n");
		return;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return;
	}
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." );
		return;
	}

	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1(coll);
	key96_t startKey ;
	key96_t endKey   ;
	key96_t lastKey  ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();
	startKey = Titledb::makeFirstKey(0);
	Msg5 msg5;
	RdbList list;

	// make this
	XmlDoc *xd;
	try {
		xd = new (XmlDoc);
	}
	catch(std::bad_alloc&) {
		fprintf(stdout,"could not alloc for xmldoc\n");
		exit(-1);
	}

	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if(cr==NULL) {
		fprintf(stderr,"Unknown collection '%s'\n", coll);
		return;
	}

	// initialize shlib & blacklist
	if (!WantedChecker::initialize()) {
		fprintf(stderr, "Unable to initialize WantedChecker");
		return;
	}
	g_urlBlackList.init();
	g_urlWhiteList.init();

	g_checkSpamList.init("spamphrases.txt");

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1            , // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) {
			return;
		}

		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			key96_t k = list.getCurrentKey();
			char *rec = list.getCurrentRec();
			int32_t recSize = list.getCurrentRecSize();
			int64_t docId = Titledb::getDocIdFromKey(&k);

			if ( k <= lastKey ) {
				log("key out of order. lastKey.n1=%" PRIx32" n0=%" PRIx64" currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				    lastKey.n1, lastKey.n0, k.n1, k.n0);
			}

			lastKey = k;

			if ( (k.n0 & 0x01) == 0) {
				// delete key
				continue;
			}

			// free the mem
			xd->reset();

			// uncompress the title rec
			if (!xd->set2(rec, recSize, coll, 0)) {
				//set2() may have logged something but not the docid
				log(LOG_WARN, "dbdump: XmlDoc::set2() failed for docid %" PRId64, docId);
				continue;
			}

			// extract the url
			Url *url = xd->getFirstUrl();
			if( url == (void *)-1 ) {
				log(LOG_WARN, "dbdump: XmlDoc::getFirstUrl() failed for docid %" PRId64, docId);
				continue;
			}

			const char *reason = NULL;

			// Don't dump unwanted URLs
			if( ! isUrlUnwanted(*url, &reason)) {
				Url **redirUrlPtr = xd->getRedirUrl();
				if (redirUrlPtr && *redirUrlPtr) {
					Url *redirUrl = *redirUrlPtr;
					if (isUrlUnwanted(*redirUrl, &reason)) {
						continue;
					}
				}

				// Get adult flag including generating debug info.
				// Could just call xd->getIsAdult() to get the simple indicator
				// without debug information.
				CheckSpam schk(xd, true);
				bool newblocked = schk.isDocSpam();

				if( newblocked ) {
					time_t idxtim = (time_t)xd->getIndexedTime();
					struct tm tm_buf;
					tm *tm1 = gmtime_r(&idxtim,&tm_buf);
					char idxtim_s[64];
					strftime(idxtim_s,64,"%Y%m%d-%H%M%S",tm1);

					fprintf(stdout, "%" PRId64 "\t%s\t%s\t%s\tscore=%" PRId32 "\tunique dw=%" PRId32 "\tunique dp=%" PRId32 "\twords=%" PRId32 "\t%s\t%s\n",
						docId, url->getUrl(), idxtim_s, schk.getReason(),
						schk.getScore(), schk.getNumUniqueMatchedWords(), schk.getNumUniqueMatchedPhrases(),
						schk.getNumWordsChecked(), schk.hasEmptyDocumentBody()?"EMPTYDOC":"HASCONTENT", schk.getDebugInfo());
				}
			}
		}

		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			return;
		}
	}
}

static bool parseTest(const char *coll, int64_t docId, const char *query) {
	g_conf.m_maxMem = 2000000000LL; // 2G
	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1 ( coll );
	log(LOG_INIT, "build: Testing parse speed of html docId %" PRId64".",docId);
	RdbList tlist;
	key96_t startKey = Titledb::makeFirstKey ( docId );
	key96_t endKey   = Titledb::makeLastKey  ( docId );
	// a niceness of 0 tells it to block until it gets results!!
	Msg5 msg5;

	const CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( ! msg5.getList ( RDB_TITLEDB    ,
			      cr->m_collnum        ,
			      &tlist         ,
			      (char *)&startKey       ,
			      (char *)&endKey         , // should be maxed!
			      9999999        , // min rec sizes
			      true           , // include tree?
			      0              , // startFileNum
			      -1             , // m_numFiles   
			      NULL           , // state 
			      NULL           , // callback
			      0              , // niceness
			      false          , // do error correction?
			      -1             , // maxRetries
			      false))         { // isRealMerge
		log(LOG_LOGIC, "build: getList did not block.");
		return false;
	}

	// get the title rec
	if ( tlist.isEmpty() ) {
		log(LOG_WARN, "build: speedtestxml: docId %" PRId64" not found.", docId);
		return false;
	}
	const char *errmsg=NULL;
	if (!UnicodeMaps::load_maps(unicode_data_dir,&errmsg)) {
		log("Unicode initialization failed! %s", errmsg);
		return false;
	}
	if(!utf8_convert_initialize()) {
		log( LOG_ERROR, "db: utf-8 conversion initialization failed!" );
		return false;
	}

	// get raw rec from list
	char *rec      = tlist.getCurrentRec();
	int32_t  listSize = tlist.getListSize ();
	XmlDoc xd;
	if (!xd.set2(rec, listSize, coll, 0)) {
		log(LOG_WARN, "build: speedtestxml: Error setting xml doc.");
		return false;
	}
	log("build: Doc url is %s",xd.ptr_firstUrl);//tr.getUrl()->getUrl());
	log("build: Doc is %" PRId32" bytes long.",xd.size_utf8Content-1);
	log("build: Doc charset is %s",get_charset_str(xd.m_charset));


	// time the summary/title generation code
	log("build: Using query %s",query);
	summaryTest1   ( rec , listSize , coll , docId , query );

	// for a 128k latin1 doc: (access time is probably 15-20ms)
	// 1.18 ms to set title rec (6ms total)
	// 1.58 ms to set Xml
	// 1.71 ms to set Words (~50% from Words::countWords())
	// 0.42 ms to set Pos
	// 0.66 ms to set Bits
	// 0.51 ms to set Scores
	// 0.35 ms to getText()

	// speed test
	int64_t t = gettimeofdayInMilliseconds();
	for ( int32_t k = 0 ; k < 100 ; k++ )
		xd.set2(rec, listSize, coll, 0);
	int64_t e = gettimeofdayInMilliseconds();
	logf(LOG_DEBUG,"build: Took %.3f ms to set title rec.",
	     (float)(e-t)/100.0);

	// speed test
	t = gettimeofdayInMilliseconds();
	for ( int32_t k = 0 ; k < 100 ; k++ ) {
		char *mm = (char *)mmalloc ( 300*1024 , "ztest");
		mfree ( mm , 300*1024 ,"ztest");
	}
	e = gettimeofdayInMilliseconds();
	logf(LOG_DEBUG,"build: Took %.3f ms to do mallocs.",
	     (float)(e-t)/100.0);

	// get content
	char *content    = xd.ptr_utf8Content;//tr.getContent();
	int32_t  contentLen = xd.size_utf8Content-1;//tr.getContentLen();

	// loop parse
	Xml xml;
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		if ( !xml.set( content, contentLen, xd.m_version, CT_HTML ) ) {
			log(LOG_WARN, "build: speedtestxml: xml set: %s", mstrerror(g_errno));
			return false;
		}
	}

	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Xml::set() took %.3f ms to parse docId %" PRId64".",
	    (double)(e - t)/100.0,docId);
	double bpms = contentLen/((double)(e-t)/100.0);
	log("build: %.3f bytes/msec", bpms);
	// get per char and per byte speeds
	xml.reset();

	// loop parse
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		if ( !xml.set( content, contentLen, xd.m_version, CT_HTML ) ) {
			log(LOG_WARN, "build: xml(setparents=false): %s", mstrerror(g_errno));
			return false;
		}
	}

	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Xml::set(setparents=false) took %.3f ms to "
	    "parse docId %" PRId64".", (double)(e - t)/100.0,docId);


	TokenizerResult tr;

	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		xml_tokenizer_phase_1(&xml,&tr);
		tr.clear();
	}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Words::set(xml) took %.3f ms for %zu words"
	    " for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);


	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		plain_tokenizer_phase_1(content,contentLen,&tr);
		tr.clear();
	}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Words::set(content,computeIds=true) "
	    "took %.3f ms for %zu words "
	    "for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);


	Pos pos;
	// computeWordIds from xml
	tr.clear();
	xml_tokenizer_phase_1(&xml,&tr);
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) 
		if ( ! pos.set ( &tr ) ) {
			log(LOG_WARN, "build: speedtestxml: pos set: %s", mstrerror(g_errno));
			return false;
		}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Pos::set() "
	    "took %.3f ms for %zu words "
	    "for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);


	Bits bits;
	// computeWordIds from xml
	tr.clear();
	xml_tokenizer_phase_1(&xml,&tr);
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) 
		if ( ! bits.setForSummary ( &tr ) ) {
			log(LOG_WARN, "build: speedtestxml: Bits set: %s", mstrerror(g_errno));
			return false;
		}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Bits::setForSummary() "
	    "took %.3f ms for %zu words "
	    "for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);


	Sections sections;
	// computeWordIds from xml
	tr.clear();
	xml_tokenizer_phase_1(&xml,&tr);
	bits.set(&tr);
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) 
		// do not supply xd so it will be set from scratch
		if ( !sections.set( &tr, &bits, NULL, 0 ) ) {
			log(LOG_WARN, "build: speedtestxml: sections set: %s", mstrerror(g_errno));
			return false;
		}

	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Scores::set() "
	    "took %.3f ms for %zu words "
	    "for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);

	

	//Phrases phrases;
	Phrases phrases;
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ )
		if ( !phrases.set( tr, bits ) ) {
			log(LOG_WARN, "build: speedtestxml: Phrases set: %s", mstrerror(g_errno));
			return false;
		}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Phrases::set() "
	    "took %.3f ms for %zu words "
	    "for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);

	char *buf = (char *)mmalloc(contentLen*2+1,"main");
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ )
		if ( !xml.getText( buf, contentLen * 2 + 1, 0, 9999999, true ) ) {
			log(LOG_WARN, "build: speedtestxml: getText: %s", mstrerror(g_errno));
			return false;
		}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Xml::getText(computeIds=false) took %.3f ms for docId "
	    "%" PRId64".",(double)(e - t)/100.0,docId);



	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		int32_t bufLen = xml.getText( buf, contentLen * 2 + 1, 0, 9999999, true );
		if ( ! bufLen ) {
			log(LOG_WARN, "build: speedtestxml: getText: %s", mstrerror(g_errno));
			return false;
		}
		plain_tokenizer_phase_1(buf,bufLen,&tr);
		tr.clear();
	}

	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Xml::getText(computeIds=false) w/ word::set() "
	    "took %.3f ms for docId "
	    "%" PRId64".",(double)(e - t)/100.0,docId);



	Matches matches;
	Query q;
	q.set(query, langUnknown, 1.0, 1.0, NULL, false, true, ABS_MAX_QUERY_TERMS);
	matches.setQuery ( &q );
	tr.clear();
	xml_tokenizer_phase_1(&xml,&tr);
	t = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		matches.reset();
		if ( ! matches.addMatches ( &tr ) ) {
			log(LOG_WARN, "build: speedtestxml: matches set: %s", mstrerror(g_errno));
			return false;
		}
	}
	// print time it took
	e = gettimeofdayInMilliseconds();
	log("build: Matches::set() took %.3f ms for %zu words"
	    " for docId %" PRId64".",
	    (double)(e - t)/100.0,tr.size(),docId);



	return true;
}	



static bool summaryTest1(char *rec, int32_t listSize, const char *coll, int64_t docId, const char *query) {

	// start the timer
	int64_t t = gettimeofdayInMilliseconds();

	Query q;
	q.set(query, langUnknown, 1.0, 1.0, NULL, false, true, ABS_MAX_QUERY_TERMS);

	char *content ;
	int32_t  contentLen ;

	// loop parse
	for ( int32_t i = 0 ; i < 100 ; i++ ) {
		XmlDoc xd;
		if (!xd.set2(rec, listSize, coll, 0)) {
			log(LOG_ERROR,"%s:%s: XmlDoc.set2 failed", __FILE__, __func__);
			return false;
		}
		// get content
		content    = xd.ptr_utf8Content;//tr.getContent();
		contentLen = xd.size_utf8Content-1;//tr.getContentLen();

		// now parse into xhtml (takes 15ms on lenny)
		Xml xml;
		xml.set( content, contentLen, xd.m_version, CT_HTML );

		xd.getSummary();
	}

	// print time it took
	int64_t e = gettimeofdayInMilliseconds();
	log("build: V1  Summary/Title/Gigabits generation took %.3f ms for docId "
	    "%" PRId64".",
	    (double)(e - t)/100.0,docId);
	double bpms = contentLen/((double)(e-t)/100.0);
	log("build: %.3f bytes/msec", bpms);
	return true;
}

void dumpPosdb (const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree, int64_t termId , bool justVerify ) {
	if ( ! justVerify ) {
		g_posdb.init ();
		g_posdb.getRdb()->addRdbBase1(coll );
	}

	key144_t startKey ;
	key144_t endKey   ;
	startKey.setMin();
	endKey.setMax();
	if ( termId >= 0 ) {
		Posdb::makeStartKey ( &startKey, termId );
		Posdb::makeEndKey  ( &endKey, termId );
		printf("termid=%" PRIu64"\n", (uint64_t)termId);
		printf("startkey=%s\n",KEYSTR(&startKey,sizeof(posdbkey_t)));
		printf("endkey=%s\n",KEYSTR(&endKey,sizeof(posdbkey_t)));
	}

	// bail if not
	if ( g_posdb.getRdb()->getNumFiles() <= startFileNum && numFiles > 0 ) {
		printf("Request file #%" PRId32" but there are only %" PRId32" "
		       "posdb files\n",startFileNum,
		       g_posdb.getRdb()->getNumFiles());
		return;
	}

	key144_t lastKey;
	lastKey.setMin();

	Msg5 msg5;
	RdbList list;

	// set this flag so Msg5.cpp if it does error correction does not
	// try to get the list from a twin...
	g_isDumpingRdbFromMain = true;
	const CollectionRec *cr = g_collectiondb.getRec(coll);

	for (;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if (!msg5.getList(RDB_POSDB,
		                  cr->m_collnum,
		                  &list,
		                  &startKey,
		                  &endKey,
		                  commandLineDumpdbRecSize,
		                  includeTree,
		                  startFileNum,
		                  numFiles,
		                  NULL, // state
		                  NULL, // callback
		                  0, // niceness
		                  true,           // to debug RdbList::removeBadData_r()
		                  -1,             // maxRetries
		                  false))          // isRealMerge
		{
			log(LOG_LOGIC, "db: getList did not block.");
			return;
		}
		// all done if empty
		if (list.isEmpty()) return;

		// get last key in list
		const char *ek2 = list.getEndKey();
		// print it
		printf("ek=%s\n", KEYSTR(ek2, list.getKeySize()));

		// loop over entries in list
		for (list.resetListPtr(); !list.isExhausted() && !justVerify; list.skipCurrentRecord()) {
			key144_t k;
			list.getCurrentKey(&k);
			// compare to last
			const char *err = "";
			if (KEYCMP((char *)&k, (char *)&lastKey, sizeof(key144_t)) < 0)
				err = " (out of order)";
			lastKey = k;
			// is it a delete?
			const char *dd = "";
			if ((k.n0 & 0x01) == 0x00) dd = " (delete)";
			int64_t d = Posdb::getDocId(&k);
			uint8_t dh = Titledb::getDomHash8FromDocId(d);
			char *rec = list.getCurrentRec();
			int32_t recSize = 18;
			if (rec[0] & 0x04) recSize = 6;
			else if (rec[0] & 0x02) recSize = 12;
			// alignment bits check
			if (recSize == 6 && !(rec[1] & 0x02)) {
				int64_t nd1 = g_posdb.getDocId(rec + 6);
				err = " (alignerror1)";
				if (nd1 < d) err = " (alignordererror1)";
				//g_process.shutdownAbort(true);
			}
			if (recSize == 12 && !(rec[1] & 0x02)) {
				// seems like nd2 is it, so it really is 12 bytes but
				// does not have the alignment bit set...
				int64_t nd2 = g_posdb.getDocId(rec + 12);
				err = " (alignerror2)";
				if (nd2 < d) err = " (alignorderrror2)";
			}
			// if it
			if (recSize == 12 && (rec[7] & 0x02)) {
				// seems like nd2 is it, so it really is 12 bytes but
				// does not have the alignment bit set...
				int64_t nd2 = g_posdb.getDocId(rec + 12);
				err = " (alignerror3)";
				if (nd2 < d) err = " (alignordererror3)";
			}
			if (KEYCMP((char *)&k, (char *)&startKey, list.getKeySize()) < 0 ||
			    KEYCMP((char *)&k, ek2, list.getKeySize()) > 0) {
				err = " (out of range)";
			}

			printf("k=%s"
					       " tid=%015" PRIu64
					       " docId=%012" PRId64
					       " siterank=%02" PRId32
					       " langid=%02" PRId32
					       " pos=%06" PRId32
					       " hgrp=%02" PRId32
					       " spamrank=%02" PRId32
					       " divrank=%02" PRId32
					       " syn=%01" PRId32
					       " densrank=%02" PRId32
					       " mult=%02" PRId32
					       " dh=0x%02" PRIx32
					       " rs=%" PRId32 //recSize
					       "%s" // dd
					       "%s" // err
					       "\n",
			       KEYSTR(&k, sizeof(key144_t)),
			       (uint64_t)Posdb::getTermId(&k),
			       d,
			       (int32_t)Posdb::getSiteRank(&k),
			       (int32_t)Posdb::getLangId(&k),
			       (int32_t)Posdb::getWordPos(&k),
			       (int32_t)Posdb::getHashGroup(&k),
			       (int32_t)Posdb::getWordSpamRank(&k),
			       (int32_t)Posdb::getDiversityRank(&k),
			       (int32_t)Posdb::getIsSynonym(&k),
			       (int32_t)Posdb::getDensityRank(&k),
			       (int32_t)Posdb::getMultiplier(&k),
			       (int32_t)dh,
			       recSize,
			       dd,
			       err);

			continue;
		}

		startKey = *(key144_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if (startKey < *(key144_t *)list.getLastKey()) return;
	}
}

static void dumpClusterdb(const char *coll,
			  int32_t startFileNum,
			  int32_t numFiles,
			  bool includeTree) {
	g_clusterdb.init ();
	g_clusterdb.getRdb()->addRdbBase1(coll );
	key96_t startKey ;
	key96_t endKey   ;
	startKey.setMin();
	endKey.setMax();

	// bail if not
	if ( g_clusterdb.getRdb()->getNumFiles() <= startFileNum ) {
		printf("Request file #%" PRId32" but there are only %" PRId32" "
		       "clusterdb files\n",startFileNum,
		       g_clusterdb.getRdb()->getNumFiles());
		return;
	}

	Msg5 msg5;
	RdbList list;
	const CollectionRec *cr = g_collectiondb.getRec(coll);

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_CLUSTERDB ,
				      cr->m_collnum          ,
				      &list         ,
				      &startKey      ,
				      &endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1,             // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() )
			return;
		// loop over entries in list
		char strLanguage[256];
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key96_t k    = list.getCurrentKey();
			// is it a delete?
			const char *dd = "";
			if ( (k.n0 & 0x01) == 0x00 ) dd = " (delete)";
			// get the language string
			languageToString ( Clusterdb::getLanguage((char*)&k),
					   strLanguage );
			//uint32_t gid = getGroupId ( RDB_CLUSTERDB , &k );
			uint32_t shardNum = getShardNum( RDB_CLUSTERDB , &k );
			Host *grp = g_hostdb.getShard ( shardNum );
			Host *hh = &grp[0];
			// print it
			printf("k.n1=%08" PRIx32" k.n0=%016" PRIx64" "
			       "docId=%012" PRId64" family=%" PRIu32" "
			       "language=%" PRId32" (%s) siteHash26=%" PRIu32"%s "
			       "groupNum=%" PRIu32" "
			       "shardNum=%" PRIu32"\n",
			       k.n1, k.n0,
			       Clusterdb::getDocId((char*)&k) ,
			       Clusterdb::hasAdultContent((char*)&k) ,
			       (int32_t)Clusterdb::getLanguage((char*)&k),
			       strLanguage,
			       Clusterdb::getSiteHash26((char*)&k)    ,
			       dd ,
			       (uint32_t)hh->m_hostId ,
			       shardNum);
			continue;
		}

		startKey = *(key96_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() )
			return;
	}
}

static void dumpLinkdb(const char *coll, int32_t startFileNum, int32_t numFiles, bool includeTree, const char *url, bool urlhash) {
	g_linkdb.init ();
	g_linkdb.getRdb()->addRdbBase1(coll );
	key224_t startKey ;
	key224_t endKey   ;
	startKey.setMin();
	endKey.setMax();

	// set start/end key to url hash
	if ( url ) {
		Url u;
		u.set( url, strlen( url ), false, false );

		SiteGetter sg;
		sg.getSite(url, NULL, 0, 0);

		uint32_t h32 = hash32(sg.getSite(), sg.getSiteLen(), 0);
		if( urlhash ) {
			startKey = Linkdb::makeStartKey_uk(h32, u.getUrlHash64());
			endKey   = Linkdb::makeEndKey_uk  (h32, u.getUrlHash64());
		}
		else {
			startKey = Linkdb::makeStartKey_uk(h32, 0);
			endKey   = Linkdb::makeEndKey_uk  (h32, LDB_MAXURLHASH);
		}


		printf("URL=%.*s, sitehash32=0x%08" PRIx32 ", urlhash=0x%012" PRIx64 "\n",
			u.getUrlLen(), u.getUrl(), h32, u.getUrlHash64());

		printf("Startkey=%s\n", KEYSTR(&startKey,sizeof(key224_t)));
		printf("Endkey  =%s\n", KEYSTR(&endKey,sizeof(key224_t)));
	}

	// bail if not
	if ( g_linkdb.getRdb()->getNumFiles() <= startFileNum  && !includeTree) {
		printf("Request file #%" PRId32" but there are only %" PRId32" "
		       "linkdb files\n",startFileNum,
		       g_linkdb.getRdb()->getNumFiles());
		return;
	}

	Msg5 msg5;
	RdbList list;
	const CollectionRec *cr = g_collectiondb.getRec(coll);

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_LINKDB ,
				      cr->m_collnum      ,
				      &list         ,
				      (char *)&startKey      ,
				      (char *)&endKey        ,
				      commandLineDumpdbRecSize,
				      includeTree   ,
				      startFileNum  ,
				      numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      -1,             // maxRetries
				      false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) return;
		// loop over entries in list
		for ( list.resetListPtr() ; ! list.isExhausted() ;
		      list.skipCurrentRecord() ) {
			key224_t k;
			list.getCurrentKey((char *) &k);
			// is it a delete?
			const char *dd = "";
			if ( (k.n0 & 0x01) == 0x00 ) dd = " (delete)";
			uint64_t docId = (uint64_t)Linkdb::getLinkerDocId_uk(&k);
			int32_t shardNum = getShardNum(RDB_LINKDB,&k);
			char ipbuf[16];
			printf("k=%s "
			       "linkeesitehash32=0x%08" PRIx32" "
			       "linkeeurlhash=0x%012" PRIx64" "
			       "linkspam=%" PRId32" "
			       "siterank=%02" PRId32" "
			       "ip32=%s "
			       "docId=%" PRIu64" "
			       "discovered=%" PRIu32" "
			       "lost=%" PRIu32" "
			       "sitehash32=0x%08" PRIx32" "
			       "shardNum=%" PRIu32" "
			       "%s\n",
			       KEYSTR(&k,sizeof(key224_t)),
			       (int32_t)Linkdb::getLinkeeSiteHash32_uk(&k),
			       (int64_t)Linkdb::getLinkeeUrlHash64_uk(&k),
			       (int32_t)Linkdb::isLinkSpam_uk(&k),
			       (int32_t)Linkdb::getLinkerSiteRank_uk(&k),
			       iptoa((int32_t)Linkdb::getLinkerIp_uk(&k),ipbuf),
			       docId,
			       (uint32_t)Linkdb::getDiscoveryDate_uk(&k),
			       (uint32_t)Linkdb::getLostDate_uk(&k),
			       (int32_t)Linkdb::getLinkerSiteHash32_uk(&k),
			       (uint32_t)shardNum,
			       dd );
		}

		startKey = *(key224_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key224_t *)list.getLastKey() ) return;
	}
}


static bool cacheTest() {

	g_conf.m_maxMem = 2000000000LL; // 2G
	//g_mem.m_maxMem  = 2000000000LL; // 2G

	if ( ! hashinit() ) {
		log( LOG_ERROR, "db: Failed to init hashtable." );
		return 1;
	}

	// use an rdb cache
	RdbCache c;
	// init, 50MB
	int32_t maxMem = 50000000; 
	// . how many nodes in cache tree can we fit?
	// . each rec is key (12) and ip(4)
	// . overhead in cache is 56
	// . that makes 56 + 4 = 60
	// . not correct? stats suggest it's less than 25 bytes each
	int32_t maxCacheNodes = maxMem / 25;
	// set the cache
	if ( ! c.init ( maxMem        ,
			4             ,  // fixed data size of rec
			maxCacheNodes ,
			"cachetest"        ,  // dbname
			false,            // save cache to disk?
			12,               // cachekeysize
			-1)) {            // numptrsmax
		log(LOG_WARN, "test: Cache init failed.");
		return false;
	}

#if 0
	int32_t numRecs = 0 * maxCacheNodes;
	logf(LOG_DEBUG,"test: Adding %" PRId32" recs to cache.",numRecs);

	// timestamp
	int32_t timestamp = 42;
	// keep ring buffer of last 10 keys
	key96_t oldk[10];
	int32_t  oldip[10];
	int32_t  b = 0;
	// fill with random recs
	for ( int32_t i = 0 ; i < numRecs ; i++ ) {
		if ( (i % 100000) == 0 )
			logf(LOG_DEBUG,"test: Added %" PRId32" recs to cache.",i);
		// random key
		key96_t k ;
		k.n1 = rand();
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		int32_t ip = rand();
		// keep ring buffer
		oldk [b] = k;
		oldip[b] = ip;
		if ( ++b >= 10 ) b = 0;
		// make rec,size, like dns, will be 4 byte hash and 4 byte key?
		c.addRecord((collnum_t)0,k,(char *)&ip,4,timestamp);
		// reset g_errno in case it had an error (we don't care)
		g_errno = 0;	
		// get a rec too!
		if ( i < 10 ) continue;
		int32_t next = b + 1;
		if ( next >= 10 ) next = 0;
		key96_t back = oldk[next];
		char *rec;
		int32_t  recSize;
		if ( ! c.getRecord ( (collnum_t)0 ,
				     back         ,
				     &rec     ,
				     &recSize ,
				     false    ,  // do copy?
				     -1       ,  // maxAge   ,
				     true     , // inc count?
				     NULL     , // *cachedTime = NULL,
				     true     )){ // promoteRecord?
			g_process.shutdownAbort(true); }
		if ( ! rec || recSize != 4 || *(int32_t *)rec != oldip[next] ) {
			g_process.shutdownAbort(true); }
	}		     		
#endif

	// now try variable sized recs
	c.reset();

	logf(LOG_DEBUG,"test: Testing variably-sized recs.");

	// init, 300MB
	maxMem = 300000000; 
	// . how many nodes in cache tree can we fit?
	// . each rec is key (12) and ip(4)
	// . overhead in cache is 56
	// . that makes 56 + 4 = 60
	// . not correct? stats suggest it's less than 25 bytes each
	maxCacheNodes = maxMem / 5000;
	//maxCacheNodes = 1200;
	// set the cache
	if ( ! c.init ( maxMem        ,
			-1            ,  // fixed data size of rec
			maxCacheNodes ,
			"cachetest"        ,  // dbname
			false,12,-1         )) { // save cache to disk?
		log(LOG_WARN, "test: Cache init failed.");
		return false;
	}

	int32_t numRecs = 30 * maxCacheNodes;
	logf(LOG_DEBUG,"test: Adding %" PRId32" recs to cache.",numRecs);

	key96_t oldk[10];

	// timestamp
	int32_t timestamp = 42;
	// keep ring buffer of last 10 keys
	int32_t oldrs[10];
	int32_t b = 0;
	// rec to add
	char *rec;
	int32_t  recSize;
	int32_t  maxRecSize = 40000000; // 40MB for termlists
	int32_t  numMisses = 0;
	char *buf = (char *)mmalloc ( maxRecSize + 64 ,"cachetest" );
	if ( ! buf ) return false;
	// fill with random recs
	for ( int32_t i = 0 ; i < numRecs ; i++ ) {
		if ( (i % 100) == 0 )
			logf(LOG_DEBUG,"test: Added %" PRId32" recs to cache. "
			     "Misses=%" PRId32".",i,numMisses);
		// random key
		key96_t k ;
		k.n1 = rand();
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		// random size
		recSize = rand()%maxRecSize;//100000;
		// keep ring buffer
		oldk [b] = k;
		oldrs[b] = recSize;
		if ( ++b >= 10 ) b = 0;
		// make the rec
		rec = buf;
		memset ( rec , (char)k.n1, recSize );
		// make rec,size, like dns, will be 4 byte hash and 4 byte key?
		if ( ! c.addRecord((collnum_t)0,k,rec,recSize,timestamp) ) {
			g_process.shutdownAbort(true); }
		// do a dup add 1% of the time
		if ( (i % 100) == 0 )
			if(!c.addRecord((collnum_t)0,k,rec,recSize,timestamp)){
				g_process.shutdownAbort(true); }
		// reset g_errno in case it had an error (we don't care)
		g_errno = 0;	
		// get a rec too!
		if ( i < 10 ) continue;
		int32_t next = b + 1;
		if ( next >= 10 ) next = 0;
		key96_t back = oldk[next];
		//log("cache: get rec");
		if ( ! c.getRecord ( (collnum_t)0 ,
				     back         ,
				     &rec     ,
				     &recSize ,
				     false    ,  // do copy?
				     -1       ,  // maxAge   ,
				     true     , // inc count?
				     NULL     , // *cachedTime = NULL,
				     true) ) {//true     )){ // promoteRecord?
			numMisses++;
			continue;
		}
		if ( recSize != oldrs[next] ) {
			logf(LOG_DEBUG,"test: bad rec size.");
			g_process.shutdownAbort(true);
		}

		char r = (char)back.n1;
		for ( int32_t j = 0 ; j < recSize ; j++ ) {
			if ( rec[j] == r ) continue;
			logf(LOG_DEBUG,"test: bad char in rec.");
			g_process.shutdownAbort(true);
		}
	}

	c.verify();

	c.reset();

	return true;
}

// CountDomains Structures and function definitions
struct dom_info {
	char          *dom;
	int32_t           domLen;
	int32_t           dHash;
	int32_t           pages;
	struct ip_info 	      **ip_list;
	int32_t           numIp;		
	int32_t 	      *lnk_table;
	int32_t           tableSize;
	int32_t           lnkCnt;
	int32_t	       lnkPages;
};

struct ip_info {
	uint32_t  ip;
	int32_t           pages;
	struct dom_info **dom_list;
	int32_t           numDom;
};

static int ip_fcmp  (const void *p1, const void *p2);
static int ip_dcmp  (const void *p1, const void *p2);

static int dom_fcmp (const void *p1, const void *p2);
static int dom_lcmp (const void *p1, const void *p2);

static void countdomains(const char* coll, int32_t numRecs, int32_t output) {
	struct ip_info **ip_table;
	struct dom_info **dom_table;

	const CollectionRec *cr = g_collectiondb.getRec(coll);

	key96_t startKey;
	key96_t endKey  ;
	key96_t lastKey ;
	startKey.setMin();
	endKey.setMax();
	lastKey.setMin();

	g_titledb.init ();
	g_titledb.getRdb()->addRdbBase1(coll );

	log( LOG_INFO, "countdomains: parms: coll=%s, numrec s=%d", coll, numRecs );
	int64_t time_start = gettimeofdayInMilliseconds();

	Msg5 msg5;
	RdbList list;
	int32_t countDocs = 0;
	int32_t countIp = 0;
	int32_t countDom = 0;
	int32_t attempts = 0;

	ip_table  = (struct ip_info **)mmalloc(sizeof(struct ip_info *) * numRecs, 
					     "main-dcit" );
	dom_table = (struct dom_info **)mmalloc(sizeof(struct dom_info *) * numRecs,
					     "main-dcdt" );

	for( int32_t i = 0; i < numRecs; i++ ) {
		ip_table[i] = NULL;
		dom_table[i] = NULL;
	}

	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if ( ! msg5.getList ( RDB_TITLEDB   ,
				cr->m_collnum       ,
				&list         ,
				&startKey      ,
				&endKey        ,
				commandLineDumpdbRecSize,
				true         , // Do we need to include tree?
				0             ,
				-1            ,
				NULL          , // state
				NULL          , // callback
				0             , // niceness
				false         , // err correction?
				-1            , // maxRetries
				false))          // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return;
		}
		// all done if empty
		if ( list.isEmpty() ) goto freeInfo;
		// loop over entries in list
		for ( list.resetListPtr(); ! list.isExhausted(); list.skipCurrentRecord() ) {
			key96_t k       = list.getCurrentKey();
			char *rec     = list.getCurrentRec();
			int32_t  recSize = list.getCurrentRecSize();
			int64_t docId       = Titledb::getDocId        ( &k );
			attempts++;

			if ( k <= lastKey ) 
				log("key out of order. "
				"lastKey.n1=%" PRIx32" n0=%" PRIx64" "
				"currKey.n1=%" PRIx32" n0=%" PRIx64" ",
				lastKey.n1,lastKey.n0,
				k.n1,k.n0);
			lastKey = k;
			// print deletes
			if ( (k.n0 & 0x01) == 0) {
				fprintf(stderr,"n1=%08" PRIx32" n0=%016" PRIx64" docId=%012" PRId64" "
					"(del)\n",
				k.n1 , k.n0 , docId );
				continue;
			}

			if( (countIp >= numRecs) || (countDom >= numRecs) ) {
				log( LOG_INFO, "countdomains: countIp | countDom, greater than"
				"numRecs requested, should never happen!!!!" );
				goto freeInfo;
			}

			XmlDoc xd;
			if (!xd.set2(rec, recSize, coll, 0))
				continue;

			struct ip_info  *sipi ;
			struct dom_info *sdomi;

			int32_t i;
			for( i = 0; i < countIp; i++ ) {
				if( !ip_table[i] ) continue;
				sipi = (struct ip_info *)ip_table[i];
				if( sipi->ip == (uint32_t)xd.m_ip ) break;
			}

			if( i == countIp ) {
				sipi = (struct ip_info *)mmalloc(sizeof(struct ip_info),
								"main-dcip" );
				if( !sipi ) { g_process.shutdownAbort(true); }
				ip_table[countIp++]  = sipi;
				sipi->ip = xd.m_ip;//u->getIp();
				sipi->pages = 1;
				sipi->numDom = 0;
			} else {
				sipi->pages++; 
			}
			
			char *fu = xd.ptr_firstUrl;
			int32_t dlen;
			const char *dom = getDomFast ( fu , &dlen );
			int32_t dkey = hash32( dom , dlen );

			for( i = 0; i < countDom; i++ ) {
				if( !dom_table[i] ) continue;
				sdomi = (struct dom_info *)dom_table[i];
				if( sdomi->dHash == dkey ) break;
			}

			if( i == countDom ) {
				sdomi =(struct dom_info*)mmalloc(sizeof(struct dom_info),
								"main-dcdm" );
				if( !sdomi ) { g_process.shutdownAbort(true); }
				dom_table[countDom++] = sdomi;
				sdomi->dom = (char *)mmalloc( dlen,"main-dcsdm" );

				strncpy( sdomi->dom, dom , dlen );
				sdomi->domLen = dlen;
				sdomi->dHash = dkey;
				sdomi->pages = 1;
				sdomi->numIp = 0;

				sdomi->tableSize = 0;
				sdomi->lnkCnt = 0;
			}
			else { 
				sdomi->pages++; 
			}

			Links *dlinks = xd.getLinks();

			int32_t size = dlinks->getNumLinks();
			if( !sdomi->tableSize ) {
				sdomi->lnk_table = (int32_t *)mmalloc(size * sizeof(int32_t),
								"main-dclt" );
				sdomi->tableSize = size;
			}
			else {
				if( size > (sdomi->tableSize - sdomi->lnkCnt) ) {
					size += sdomi->lnkCnt;
					sdomi->lnk_table = (int32_t *)
						mrealloc(sdomi->lnk_table,
							sdomi->tableSize*sizeof(int32_t),
							size*sizeof(int32_t),
							"main-dcrlt" );
					sdomi->tableSize = size;
				}
			}
				
			for( int32_t i = 0; i < dlinks->getNumLinks(); i++ ) {
				char *link = dlinks->getLinkPtr(i);
				int32_t dlen;
				const char *dom = getDomFast ( link , &dlen );
				uint32_t lkey = hash32( dom , dlen );
				int32_t j;
				for( j = 0; j < sdomi->lnkCnt; j++ ) {
					if( sdomi->lnk_table[j] == (int32_t)lkey ) break;
				}
				
				sdomi->lnkPages++;
				if( j != sdomi->lnkCnt ) continue;
				sdomi->lnk_table[sdomi->lnkCnt++] = lkey;
				sdomi->lnkPages++;
			}

			// Handle lists
			if( !sipi->numDom || !sdomi->numIp ){
				sdomi->numIp++; sipi->numDom++;
				//Add to IP list for Domain
				sdomi->ip_list = (struct ip_info **)
					mrealloc( sdomi->ip_list,
						(sdomi->numIp-1)*sizeof(char *),
						sdomi->numIp*sizeof(char *),
						"main-dcldm" );
				sdomi->ip_list[sdomi->numIp-1] = sipi;

				//Add to domain list for IP
				sipi->dom_list = (struct dom_info **)
					mrealloc( sipi->dom_list,
						(sipi->numDom-1)*sizeof(char *),
						sipi->numDom*sizeof(char *),
						"main-dclip" );
				sipi->dom_list[sipi->numDom-1] = sdomi;
			}
			else {
				int32_t i;
				for( i = 0; 
				(i < sdomi->numIp) 
					&& (sdomi->ip_list[i] != sipi);
				i++ );
				if( sdomi->numIp != i ) goto updateIp;

				sdomi->numIp++;
				sdomi->ip_list = (struct ip_info **)
					mrealloc( sdomi->ip_list,
						(sdomi->numIp-1)*sizeof(int32_t),
						sdomi->numIp*sizeof(int32_t),
						"main-dcldm" );
				sdomi->ip_list[sdomi->numIp-1] = sipi;

			updateIp:
				for( i = 0; 
				(i < sipi->numDom) 
					&& (sipi->dom_list[i] != sdomi);
				i++ );
				if( sipi->numDom != i ) goto endListUpdate;

				sipi->numDom++;
				sipi->dom_list = (struct dom_info **)
					mrealloc( sipi->dom_list,
						(sipi->numDom-1)*sizeof(int32_t),
						sipi->numDom*sizeof(int32_t),
						"main-dclip" );
				sipi->dom_list[sipi->numDom-1] = sdomi;

			endListUpdate:
				i=0;
			}				
			if( !((++countDocs) % 1000) ) 
				log(LOG_INFO, "countdomains: %" PRId32" records searched.",countDocs);
			if( countDocs == numRecs ) goto freeInfo;
			//else countDocs++;
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			log( LOG_INFO, "countdomains: Keys wrapped around! Exiting." );
			goto freeInfo;
		}
			
		if ( countDocs >= numRecs ) {
		freeInfo:
			char             buf[128];
			//int32_t             value   ;
			int32_t             len     ;
			char             loop    ;
			int32_t             recsDisp;
			struct ip_info  *tmpipi  ;
			struct dom_info *tmpdomi ;
			loop = 0;

			FILE *fhndl;		
			char out[128];
			if( output != 9 ) goto printHtml;
			// Dump raw data to a file to parse later
			snprintf( out, sizeof(out), "%scntdom.xml", g_hostdb.m_dir );
			out[ sizeof(out)-1 ] = '\0';
			if( !(fhndl = fopen( out, "wb" )) ) {
				log( LOG_INFO, "countdomains: File Open Failed." );
				return;
			}

			gbsort( dom_table, countDom, sizeof(struct dom_info *), dom_fcmp );		
			for( int32_t i = 0; i < countDom; i++ ) {
				if( !dom_table[i] ) continue;
				tmpdomi = (struct dom_info *)dom_table[i];
				len = tmpdomi->domLen;
				if( tmpdomi->domLen > 127 ) len = 126;
				strncpy( buf, tmpdomi->dom, len );
				buf[len] = '\0';
				fprintf(fhndl,
					"<rec1>\n\t<domain>%s</domain>\n"
					"\t<pages>%" PRId32"</pages>\n"
					//"\t<quality>%" PRId64"</quality>\n"
					"\t<block>\n",
					buf, tmpdomi->pages
					//,(tmpdomi->quality/tmpdomi->pages)
					);
				gbsort( tmpdomi->ip_list,tmpdomi->numIp, sizeof(int32_t), 
				ip_fcmp );
				for( int32_t j = 0; j < tmpdomi->numIp; j++ ) {
					if( !tmpdomi->ip_list[j] ) continue;
					tmpipi = (struct ip_info *)tmpdomi->ip_list[j];
					iptoa( tmpipi->ip,buf);
					fprintf(fhndl,"\t\t<ip>%s</ip>\n",buf);
				}
				fprintf(fhndl,
					"\t</block>\n"
					"\t<links>\n");
			}
			gbsort( ip_table, countIp, sizeof(struct ip_info *), ip_fcmp );		
			for( int32_t i = 0; i < countIp; i++ ) {
				if( !ip_table[i] ) continue;
				tmpipi = (struct ip_info *)ip_table[i];
				iptoa( tmpipi->ip,buf);
				fprintf(fhndl,
					"<rec2>\n\t<ip>%s</ip>\n"
					"\t<pages>%" PRId32"</pages>\n"
					"\t<block>\n",
					buf, tmpipi->pages);
				for( int32_t j = 0; j < tmpipi->numDom; j++ ) {
					tmpdomi = (struct dom_info *)tmpipi->dom_list[j];
					len = tmpdomi->domLen;
					if( tmpdomi->domLen > 127 ) len = 126;
					strncpy( buf, tmpdomi->dom, len );
					buf[len] = '\0';
					fprintf(fhndl,
						"\t\t<domain>%s</domain>\n",
						buf);
				}
				fprintf(fhndl,
					"\t</block>\n"
					"</rec2>\n");
			}

			if( fclose( fhndl ) < 0 ) {
				log( LOG_INFO, "countdomains: File Close Failed." );
				return;
			}
			fhndl = 0;

		printHtml:
			// HTML file Output
			snprintf( out, sizeof(out), "%scntdom.html", g_hostdb.m_dir );
			out[ sizeof(out)-1 ] = '\0';
			if( !(fhndl = fopen( out, "wb" )) ) {
				log( LOG_INFO, "countdomains: File Open Failed." );
				return;
			}		
			int64_t total = g_titledb.estimateGlobalNumDocs();
			static const char link_ip[]  = "http://www.gigablast.com/search?"
					"code=gbmonitor&q=ip%3A";
			static const char link_dom[] = "http://www.gigablast.com/search?"
					"code=gbmonitor&q=site%3A";
			static const char menu[] = "<table cellpadding=\"2\" cellspacing=\"2\">\n<tr>"
				"<th bgcolor=\"#CCCC66\"><a href=\"#pid\">"
				"Domains Sorted By Pages</a></th>"
				"<th bgcolor=\"#CCCC66\"><a href=\"#lid\">"
				"Domains Sorted By Links</a></th>"
				"<th bgcolor=\"#CCCC66\"><a href=\"#pii\">"
				"IPs Sorted By Pages</a></th>"
				"<th bgcolor=\"#CCCC66\"><a href=\"#dii\">"
				"IPs Sorted By Domains</a></th>"
				"<th bgcolor=\"#CCCC66\"><a href=\"#stats\">"
				"Stats</a></th>"
				"</tr>\n</table>\n<br>\n";

			static const char hdr[] = "<table cellpadding=\"5\" cellspacing=\"2\">"
				"<tr bgcolor=\"AAAAAA\">"
				"<th>Domain</th>"
				"<th>Domains Linked</th>"
				//"<th>Avg Quality</th>"
				"<th># Pages</th>"
				"<th>Extrap # Pages</th>"
				"<th>IP</th>"
				"</tr>\n";

			static const char hdr2[] = "<table cellpadding=\"5\" cellspacing=\"2\">"
				"<tr bgcolor=\"AAAAAA\">"
				"<th>IP</th>"
				"<th>Domain</th>"
				"<th>Domains Linked</th>"
				//"<th>Avg Quality</th>"
				"<th># Pages</th>"
				"<th>Extrap # Pages</th>"
				"</tr>\n";
			
			static const char clr1[] = "#FFFF00";//"yellow";
			static const char clr2[] = "#FFFF66";//"orange";
			const char *color;
				
			fprintf( fhndl, 
				"<html><head><title>Domain/IP Counter</title></head>\n"
				"<body>"
				"<h1>Domain/IP Counter</h1><br><br>"
				"<a name=\"stats\">"
				"<h2>Stats</h2>\n%s", menu );

			// Stats
			fprintf( fhndl, "<br>\n\n<table>\n"
				"<tr><th align=\"left\">Total Number of Domains</th>"
				"<td>%" PRId32"</td></tr>\n"
				"<tr><th align=\"left\">Total Number of Ips</th>"
				"<td>%" PRId32"</td></tr>\n"
				"<tr><th align=\"left\">Number of Documents Searched"
				"</th><td>%" PRId32"</td></tr>\n"
				"<tr><th align=\"left\">Number of Failed Attempts</th>"
				"<td>%" PRId32"</td></tr><tr></tr><tr>\n"
				"<tr><th align=\"left\">Number of Documents in Index"
				"</th><td>%" PRId64"</td></tr>\n"
				"<tr><th align=\"left\">Estimated Domains in index</th>"
				"<td>%" PRId64"</td></tr>"
				"</table><br><br><br>\n"
				,countDom,countIp,
				countDocs, attempts-countDocs,total, 
				countDocs ? ((countDom*total)/countDocs) : 0 );
			
			
			fprintf( fhndl, "<a name=\"pid\">\n"
				"<h2>Domains Sorted By Pages</h2>\n"
				"%s", menu );
			gbsort( dom_table, countDom, sizeof(struct dom_info *), dom_fcmp );
		printDomLp:

			fprintf( fhndl,"%s", hdr );
			recsDisp = countDom;
			if( countDom > 1000 ) recsDisp = 1000;
			for( int32_t i = 0; i < recsDisp; i++ ) {
				char buf[128];
				int32_t len;
				if( !dom_table[i] ) continue;
				if( i%2 ) color = clr2;
				else color = clr1;
				tmpdomi = (struct dom_info *)dom_table[i];
				len = tmpdomi->domLen;
				if( tmpdomi->domLen > 127 ) len = 126;
				strncpy( buf, tmpdomi->dom, len );
				buf[len] = '\0';
				fprintf( fhndl, "<tr bgcolor=\"%s\"><td>"
					"<a href=\"%s%s\" target=\"_blank\">%s</a>"
					"</td><td>%" PRId32"</td>"
					//"<td>%" PRId64"</td>"
					"<td>%" PRId32"</td>"
					"<td>%" PRId64"</td><td>",
					color, link_dom,
					buf, buf, tmpdomi->lnkCnt,
					//(tmpdomi->quality/tmpdomi->pages), 
					tmpdomi->pages,
					((tmpdomi->pages*total)/countDocs) );
				for( int32_t j = 0; j < tmpdomi->numIp; j++ ) {
					tmpipi = (struct ip_info *)tmpdomi->ip_list[j];
					iptoa(tmpipi->ip,buf);
					fprintf( fhndl, "<a href=\"%s%s\""
						"target=\"_blank\">%s</a>\n", 
						link_ip, buf, buf );
				}
				fprintf( fhndl, "</td></tr>\n" );
				fprintf( fhndl, "\n" );
			}

			fprintf( fhndl, "</table>\n<br><br><br>" );
			if( loop == 0 ) {
				loop = 1;
				gbsort( dom_table, countDom, sizeof(struct dom_info *), dom_lcmp );
				fprintf( fhndl, "<a name=\"lid\">"
					"<h2>Domains Sorted By Links</h2>\n%s", menu );

				goto printDomLp;
			}
			loop = 0;

			fprintf( fhndl, "<a name=\"pii\">"
				"<h2>IPs Sorted By Pages</h2>\n%s", menu );


			gbsort( ip_table, countIp, sizeof(struct ip_info *), ip_fcmp );
		printIpLp:
			fprintf( fhndl,"%s", hdr2 );
			recsDisp = countIp;
			if( countIp > 1000 ) recsDisp = 1000;
			for( int32_t i = 0; i < recsDisp; i++ ) {
				char buf[128];
				if( !ip_table[i] ) continue;
				tmpipi = (struct ip_info *)ip_table[i];
				iptoa(tmpipi->ip,buf);
				if( i%2 ) color = clr2;
				else color = clr1;
				int32_t linked = 0;
				for( int32_t j = 0; j < tmpipi->numDom; j++ ) {
					tmpdomi=(struct dom_info *)tmpipi->dom_list[j];
					linked += tmpdomi->lnkCnt;
				}
				fprintf( fhndl, "\t<tr bgcolor=\"%s\"><td>"
					"<a href=\"%s%s\" target=\"_blank\">%s</a>"
					"</td>"
					"<td>%" PRId32"</td>"
					"<td>%" PRId32"</td>"
					//"<td>%" PRId64"</td>"
					"<td>%" PRId32"</td>"
					"<td>%" PRId64"</td></tr>\n",
					color,
					link_ip, buf, buf, tmpipi->numDom, linked,
					//(tmpipi->quality/tmpipi->pages), 
					tmpipi->pages, 
					((tmpipi->pages*total)/countDocs) );
				fprintf( fhndl, "\n" );
			}

			fprintf( fhndl, "</table>\n<br><br><br>" );
			if( loop == 0 ) {
				loop = 1;
				gbsort( ip_table, countIp, sizeof(struct ip_info *), ip_dcmp );
				fprintf( fhndl, "<a name=\"dii\">"
					"<h2>IPs Sorted By Domains</h2>\n%s", menu );
				goto printIpLp;
			}

			if( fclose( fhndl ) < 0 ) {
				log( LOG_INFO, "countdomains: File Close Failed." );
				return;
			}
			fhndl = 0;


			int32_t ima = 0;
			int32_t dma = 0;

			log( LOG_INFO, "countdomains: Freeing ip info struct..." );
			for( int32_t i = 0; i < countIp; i++ ) {
				if( !ip_table[i] ) continue;
				//value = ipHT.getValue( ip_table[i] );
				//if(value == 0) continue;
				tmpipi = (struct ip_info *)ip_table[i]; 
				mfree( tmpipi->dom_list, tmpipi->numDom*sizeof(tmpipi->dom_list[0]),
				"main-dcflip" );
				ima += tmpipi->numDom * sizeof(int32_t);
				mfree( tmpipi, sizeof(struct ip_info), "main-dcfip" );
				ima += sizeof(struct ip_info);
				tmpipi = NULL;
			}
			mfree( ip_table, numRecs * sizeof(struct ip_info *), "main-dcfit" );

			log( LOG_INFO, "countdomains: Freeing domain info struct..." );
			for( int32_t i = 0; i < countDom; i++ ) {
				if( !dom_table[i] ) continue;
				tmpdomi = (struct dom_info *)dom_table[i];
				mfree( tmpdomi->lnk_table,
				tmpdomi->tableSize*sizeof(int32_t), 
				"main-dcfsdlt" );
				dma += tmpdomi->tableSize * sizeof(int32_t);
				mfree( tmpdomi->ip_list, tmpdomi->numIp*sizeof(tmpdomi->ip_list[0]),
				"main-dcfldom" );
				dma += tmpdomi->numIp * sizeof(int32_t);
				mfree( tmpdomi->dom, tmpdomi->domLen, "main-dcfsdom" );
				dma += tmpdomi->domLen;
				mfree( tmpdomi, sizeof(struct dom_info), "main-dcfdom" );
				dma+= sizeof(struct dom_info);
				tmpdomi = NULL;
			}
						
			mfree( dom_table, numRecs * sizeof(struct dom_info *), "main-dcfdt" );

			int64_t time_end = gettimeofdayInMilliseconds();
			log( LOG_INFO, "countdomains: Took %" PRId64"ms to count domains in %" PRId32" recs.",
			time_end-time_start, countDocs );
			log( LOG_INFO, "countdomains: %" PRId32" bytes of Total Memory Used.",
			ima + dma + (8 * numRecs) );
			log( LOG_INFO, "countdomains: %" PRId32" bytes Total for IP.", ima );
			log( LOG_INFO, "countdomains: %" PRId32" bytes Total for Dom.", dma );
			log( LOG_INFO, "countdomains: %" PRId32" bytes Average for IP.", countIp ? ima/countIp : 0 );
			log( LOG_INFO, "countdomains: %" PRId32" bytes Average for Dom.", countDom ? dma/countDom : 0 );
			
			return;
		}	
	}
}

// Sort by IP frequency in pages 9->0
int ip_fcmp (const void *p1, const void *p2) {
	//int32_t n1, n2;
	// break this! need to fix later MDW 11/12/14
	char *n1 ;
	char *n2 ;
	struct ip_info *ii1;
	struct ip_info *ii2;

	*(((unsigned char *)(&n1))+0) = *(((char *)p1)+0);
	*(((unsigned char *)(&n1))+1) = *(((char *)p1)+1);
	*(((unsigned char *)(&n1))+2) = *(((char *)p1)+2);
	*(((unsigned char *)(&n1))+3) = *(((char *)p1)+3);

	*(((unsigned char *)(&n2))+0) = *(((char *)p2)+0);
	*(((unsigned char *)(&n2))+1) = *(((char *)p2)+1);
	*(((unsigned char *)(&n2))+2) = *(((char *)p2)+2);
	*(((unsigned char *)(&n2))+3) = *(((char *)p2)+3);

	ii1 = (struct ip_info *)n1;
	ii2 = (struct ip_info *)n2;
	
	return ii2->pages-ii1->pages;
}

// Sort by number of domains linked to IP, descending
int ip_dcmp (const void *p1, const void *p2) {
	//int32_t n1, n2;
	// break this! need to fix later MDW 11/12/14
	char *n1 ;
	char *n2 ;

	struct ip_info *ii1;
	struct ip_info *ii2;

	*(((unsigned char *)(&n1))+0) = *(((char *)p1)+0);
	*(((unsigned char *)(&n1))+1) = *(((char *)p1)+1);
	*(((unsigned char *)(&n1))+2) = *(((char *)p1)+2);
	*(((unsigned char *)(&n1))+3) = *(((char *)p1)+3);

	*(((unsigned char *)(&n2))+0) = *(((char *)p2)+0);
	*(((unsigned char *)(&n2))+1) = *(((char *)p2)+1);
	*(((unsigned char *)(&n2))+2) = *(((char *)p2)+2);
	*(((unsigned char *)(&n2))+3) = *(((char *)p2)+3);

	ii1 = (struct ip_info *)n1;
	ii2 = (struct ip_info *)n2;
	
	return ii2->numDom-ii1->numDom;
}

// Sort by page frequency in titlerec 9->0
int dom_fcmp (const void *p1, const void *p2) {
	//int32_t n1, n2;
	// break this! need to fix later MDW 11/12/14
	char *n1 ;
	char *n2 ;
	struct dom_info *di1;
	struct dom_info *di2;

	*(((unsigned char *)(&n1))+0) = *(((char *)p1)+0);
	*(((unsigned char *)(&n1))+1) = *(((char *)p1)+1);
	*(((unsigned char *)(&n1))+2) = *(((char *)p1)+2);
	*(((unsigned char *)(&n1))+3) = *(((char *)p1)+3);

	*(((unsigned char *)(&n2))+0) = *(((char *)p2)+0);
	*(((unsigned char *)(&n2))+1) = *(((char *)p2)+1);
	*(((unsigned char *)(&n2))+2) = *(((char *)p2)+2);
	*(((unsigned char *)(&n2))+3) = *(((char *)p2)+3);


	di1 = (struct dom_info *)n1;
	di2 = (struct dom_info *)n2;

	return di2->pages-di1->pages;
}

// Sort by quantity of outgoing links 9-0
int dom_lcmp (const void *p1, const void *p2) {
	//int32_t n1, n2;
	// break this! need to fix later MDW 11/12/14
	char *n1 ;
	char *n2 ;
	struct dom_info *di1;
	struct dom_info *di2;

	*(((unsigned char *)(&n1))+0) = *(((char *)p1)+0);
	*(((unsigned char *)(&n1))+1) = *(((char *)p1)+1);
	*(((unsigned char *)(&n1))+2) = *(((char *)p1)+2);
	*(((unsigned char *)(&n1))+3) = *(((char *)p1)+3);

	*(((unsigned char *)(&n2))+0) = *(((char *)p2)+0);
	*(((unsigned char *)(&n2))+1) = *(((char *)p2)+1);
	*(((unsigned char *)(&n2))+2) = *(((char *)p2)+2);
	*(((unsigned char *)(&n2))+3) = *(((char *)p2)+3);


	di1 = (struct dom_info *)n1;
	di2 = (struct dom_info *)n2;

	return di2->lnkCnt-di1->lnkCnt;
}

static const char *getAbsoluteGbDir(const char *argv0) {
	static char s_buf[1024];
	
	char *s = realpath(argv0, NULL);
	if(!s)
		return NULL;
	if(strlen(s) >= sizeof(s_buf))
		return NULL;
	strcpy(s_buf,s);
	free(s);
	
	//chop off last component, so we hae just the directory (including a trailing / )
	char *slash = strrchr(s_buf, '/');
	if(slash==NULL) {
		//what? this is not supposed to happen that realpath returns an absolute path that doesn't contain a slash
		return NULL;
	}
	slash[1] = '\0';
	return s_buf;
}


///////
//
// used to make package to install files for the package
//
///////
static int copyFiles(const char *dstDir) {

	const char *srcDir = "./";
	SafeBuf fileListBuf;
	g_process.getFilesToCopy ( srcDir , &fileListBuf );

	SafeBuf tmp;
	tmp.safePrintf(
		       "cp -r %s %s"
		       , fileListBuf.getBufStart()
		       , dstDir 
		       );

	//log(LOG_INIT,"admin: %s", tmp.getBufStart());
	fprintf(stderr,"\nRunning cmd: %s\n",tmp.getBufStart());
	system ( tmp.getBufStart() );
	return 0;
}


static void wvg_log_function(WordVariationGenerator::log_class_t log_class, const char *fmt, va_list ap) {
	char buf[2048];
	vsnprintf(buf,sizeof(buf), fmt, ap);
	buf[sizeof(buf)-1]='\0';
	int32_t type;
	switch(log_class) {
		case WordVariationGenerator::log_trace: type = LOG_TRACE; break;
		case WordVariationGenerator::log_debug: type = LOG_DEBUG; break;
		case WordVariationGenerator::log_info: type = LOG_INFO; break;
		case WordVariationGenerator::log_warn: type = LOG_WARN; break;
		case WordVariationGenerator::log_error: type = LOG_ERROR; break;
	}
	log(type,"wordvar:%s",buf);
}
