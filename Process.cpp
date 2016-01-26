#include "gb-include.h"

#include "Process.h"
#include "Rdb.h"
#include "Clusterdb.h"
#include "Hostdb.h"
#include "Tagdb.h"
#include "Posdb.h"
#include "Cachedb.h"
#include "Datedb.h"
#include "Titledb.h"
#include "Sections.h"
#include "Spider.h"
#include "Statsdb.h"
#include "Threads.h"
#include "PingServer.h"
#include "Dns.h"
#include "Repair.h"
#include "RdbCache.h"
#include "Spider.h"
#include "HttpServer.h"
#include "Speller.h"
#include "Spider.h"
#include "Profiler.h"
#include "Msg4.h"
#include "Msg5.h"
#include "Wiki.h"
#include "Wiktionary.h"
#include "Proxy.h"
#include "Rebalance.h"
#include "SpiderProxy.h"
#include "PageInject.h"
#include "Timezone.h"
#include "CountryCode.h"

// normally in seo.cpp, but here so it compiles
SafeBuf    g_qbuf;
int32_t       g_qbufNeedSave = 0;
bool g_inAutoSave;

// for resetAll()
extern void resetPageAddUrl    ( );
extern void resetHttpMime      ( );
extern void reset_iana_charset ( );
extern void resetDomains       ( );
extern void resetEntities      ( );
extern void resetQuery         ( );
extern void resetStopWords     ( );
extern void resetAbbrTable     ( );
extern void resetUnicode       ( );


// our global instance
Process g_process;

//static int32_t s_flag = 1;
static int32_t s_nextTime = 0;

char *g_files[] = {
	//"gb.conf",

	// might have localhosts.conf
	//"hosts.conf",
	
	"catcountry.dat",
	"badcattable.dat",

	"ucdata/kd_data.dat",
	"ucdata/kdmap.dat",
	"ucdata/lowermap.dat",
	"ucdata/properties.dat",
	"ucdata/scripts.dat",
	"ucdata/uppermap.dat",
	
	"gbcheck.sh",
	"gbconvert.sh",

	"antiword" ,  // msword
	"pstotext" ,  // postscript

	// required for SSL server support for both getting web pages
	// on https:// sites and for serving https:// pages
	"gb.pem",

	// the main binary!
	"gb",
	
	"antiword-dir/8859-1.txt",
	"antiword-dir/8859-10.txt",
	"antiword-dir/8859-13.txt",
	"antiword-dir/8859-14.txt",
	"antiword-dir/8859-15.txt",
	"antiword-dir/8859-16.txt",
	"antiword-dir/8859-2.txt",
	"antiword-dir/8859-3.txt",
	"antiword-dir/8859-4.txt",
	"antiword-dir/8859-5.txt",
	"antiword-dir/8859-6.txt",
	"antiword-dir/8859-7.txt",
	"antiword-dir/8859-8.txt",
	"antiword-dir/8859-9.txt",
	"antiword-dir/Default",
	"antiword-dir/Example",
	"antiword-dir/MacRoman.txt",
	"antiword-dir/UTF-8.txt",
	"antiword-dir/Unicode",
	"antiword-dir/cp1250.txt",
	"antiword-dir/cp1251.txt",
	"antiword-dir/cp1252.txt",
	"antiword-dir/cp437.txt",
	"antiword-dir/cp850.txt",
	"antiword-dir/cp852.txt",
	"antiword-dir/fontnames",
	"antiword-dir/fontnames.russian",
	"antiword-dir/koi8-r.txt",
	"antiword-dir/koi8-u.txt",
	"antiword-dir/roman.txt",

	// . thumbnail generation
	// . i used 'apt-get install netpbm' to install
	"bmptopnm",
	"giftopnm",
	"jpegtopnm",
	"libjpeg.so.62",
	"libnetpbm.so.10",
	"libpng12.so.0",
	"libtiff.so.4",

	"libcld2_full.so",

	"LICENSE",
	"pngtopnm",
	"pnmscale",
	"ppmtojpeg",
	"tifftopnm",

	"mysynonyms.txt",

	"wikititles.txt.part1",
	"wikititles.txt.part2",

	"wiktionary-buf.txt",
	"wiktionary-lang.txt",
	"wiktionary-syns.dat",

	// gives us siteranks for the most popular sites:
	"sitelinks.txt",

	"unifiedDict.txt",
	
	NULL
};


///////
//
// used to make package to install files for the package.
// so do not include hosts.conf or gb.conf
//
///////
bool Process::getFilesToCopy ( char *srcDir , SafeBuf *buf ) {

	// sanirty
	int32_t slen = gbstrlen(srcDir);
	if ( srcDir[slen-1] != '/' ) { char *xx=NULL;*xx=0; }

	for ( int32_t i = 0 ; i < (int32_t)sizeof(g_files)/4 ; i++ ) {
		// terminate?
		if ( ! g_files[i] ) break;
		// skip subdir shit it won't work
		if ( strstr(g_files[i],"/") ) continue;
		// if not first
		if ( i > 0 ) buf->pushChar(' ');
		// append it
		buf->safePrintf("%s%s"
				, srcDir
				, g_files[i] );
	}

	// and the required runtime subdirs
	buf->safePrintf(" %santiword-dir",srcDir);
	buf->safePrintf(" %sucdata",srcDir);
	buf->safePrintf(" %shtml",srcDir);

	return true;
}


bool Process::checkFiles ( const char *dir ) {
	// make sure we got all the files
	bool needsFiles = false;

	for ( int32_t i = 0 ; i < (int32_t)sizeof(g_files)/4 ; i++ ) {
		// terminate?
		if ( ! g_files[i] ) {
			break;
		}

		File f;
		const char *dd = dir;
		if ( g_files[i][0] != '/' ) {
			f.set ( dir , g_files[i] );
		} else {
			f.set ( g_files[i] );
			dd = "";
		}

		if ( ! f.doesExist() ) {
			log(LOG_ERROR, "db: %s%s file missing.", dd, g_files[i]);
			needsFiles = true;
		}
	}

	if ( needsFiles ) {
		log(LOG_ERROR, "db: Missing files. See above. Exiting.");
		return false;
	}

	{
		char cmd[2048] = {};
		snprintf(cmd, 2047, "%sgbcheck.sh", dir);
		int ret = gbsystem ( cmd );
		if ( WEXITSTATUS(ret) != 0 ) {
			log(LOG_ERROR, "db: gbcheck failed with status=%d", WEXITSTATUS(ret));
			return false;
		}
	}

	return true;
}

static void powerMonitorWrapper ( int fd , void *state ) ;
static void fanSwitchCheckWrapper ( int fd , void *state ) ;
static void gotPowerWrapper ( void *state , TcpSocket *s ) ;
static void doneCmdWrapper        ( void *state ) ;
static void  hdtempWrapper        ( int fd , void *state ) ;
static void  hdtempDoneWrapper    ( void *state , ThreadEntry *t ) ;
static void *hdtempStartWrapper_r ( void *state , ThreadEntry *t ) ;
static void heartbeatWrapper    ( int fd , void *state ) ;
//static void diskHeartbeatWrapper ( int fd , void *state ) ;
static void processSleepWrapper ( int fd , void *state ) ;

Process::Process ( ) {
	m_mode = NO_MODE;
	m_exiting = false;
	m_powerIsOn = true;
	m_totalDocsIndexed = -1LL;
}

bool Process::init ( ) {
	g_inAutoSave = false;
	// -1 means unknown
	m_diskUsage = -1.0;
	m_diskAvail = -1LL;
	// we do not know if the fans are turned off or on
	m_currentFanState = -1;
	m_threadOut = false;
	m_powerReqOut = false;
	m_powerIsOn = true;
	m_numRdbs = 0;
	m_suspendAutoSave = false;
	// . init the array of rdbs
	// . primary rdbs
	// . let's try to save tfndb first, that is the most important,
	//   followed by titledb perhaps...
	m_rdbs[m_numRdbs++] = g_titledb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_sectiondb.getRdb   ();
	m_rdbs[m_numRdbs++] = g_posdb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_spiderdb.getRdb    ();
	m_rdbs[m_numRdbs++] = g_clusterdb.getRdb   (); 
	m_rdbs[m_numRdbs++] = g_tagdb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_statsdb.getRdb     ();
	m_rdbs[m_numRdbs++] = g_linkdb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_cachedb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_serpdb.getRdb      ();
	// save what urls we have been doled
	m_rdbs[m_numRdbs++] = g_doledb.getRdb      ();
	m_rdbs[m_numRdbs++] = g_titledb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_sectiondb2.getRdb  ();
	m_rdbs[m_numRdbs++] = g_posdb2.getRdb    ();
	m_rdbs[m_numRdbs++] = g_spiderdb2.getRdb   ();
	m_rdbs[m_numRdbs++] = g_clusterdb2.getRdb  ();
	m_rdbs[m_numRdbs++] = g_linkdb2.getRdb     ();
	m_rdbs[m_numRdbs++] = g_tagdb2.getRdb      ();
	/////////////////
	// CAUTION!!!
	/////////////////
	// Add any new rdbs to the END of the list above so 
	// it doesn't screw up Rebalance.cpp which uses this list too!!!!
	/////////////////


	//call these back right before we shutdown the
	//httpserver.
	m_callbackState = NULL;
	m_callback      = NULL;

	// do not do an autosave right away
	m_lastSaveTime = 0;//gettimeofdayInMillisecondsLocal();
	// reset this
	m_sentShutdownNote = false;
	// this is used for shutting down as well
	m_blockersNeedSave = true;
	m_repairNeedsSave  = true;
	// count tries
	m_try = 0;
	// reset this timestamp
	m_firstShutdownTime = 0;
	// set the start time, local time
	m_processStartTime = gettimeofdayInMillisecondsLocal();
	// reset this
	m_lastHeartbeatApprox = 0;
	m_calledSave = false;

	// heartbeat check
	if ( ! g_loop.registerSleepCallback(100,NULL,heartbeatWrapper,0))
		return false;
	// we use SSDs now so comment this out
	//if ( !g_loop.registerSleepCallback(500,NULL,diskHeartbeatWrapper,0))
	//	return false;

	// get first snapshot of load average...
	//update_load_average(gettimeofdayInMillisecondsLocal());
	// . continually call this once per second
	// . once every half second now so that autosaves are closer together
	//   in time between all hosts
	if ( ! g_loop.registerSleepCallback(500,NULL,processSleepWrapper))
		return false;

	// . hard drive temperature
	// . now that we use intel ssds that do not support smart, ignore this
	// . well use it for disk usage i guess
	if ( ! g_loop.registerSleepCallback(10000,NULL,hdtempWrapper,0))
		return false;

	// power monitor, every 30 seconds
	if ( ! g_loop.registerSleepCallback(30000,NULL,powerMonitorWrapper,0))
		return false;

	// check temps to possible turn fans on/off every 60 seconds
	if ( !g_loop.registerSleepCallback(60000,NULL,fanSwitchCheckWrapper,0))
		return false;

	// -99 means unknown
	m_dataCtrTemp = -99;
	m_roofTemp    = -99;

	// success
	return true;
}

bool Process::isAnyTreeSaving ( ) {
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->m_isCollectionLess ) continue;
		if ( rdb->isSavingTree() ) return true;
		// we also just disable writing below in Process.cpp
		// while saving other files. so hafta check that as well
		// since we use isAnyTreeSaving() to determine if we can
		// write to the tree or not.
		if ( ! rdb->isWritable() ) return true;
	}
	return false;
}

void powerMonitorWrapper ( int fd , void *state ) {
	// only if in matt wells datacenter
	if ( ! g_conf.m_isMattWells ) 
		return;

	// are we in group #0
	bool checkPower = false;
	// get our host
	Host *me = g_hostdb.m_myHost;
	// if we are not host #0 and host #0 is dead, we check it
	if ( me->m_shardNum == 0 && g_hostdb.isDead((int32_t)0) ) 
		checkPower = true;
	// if we are host #0 we always check it
	if ( me->m_hostId == 0 ) checkPower = true;
	// proxy never checks power
	if ( me->m_isProxy ) checkPower = false;
	// if not checking, all done
	if ( ! checkPower ) return;
	// only if live
	//if ( ! g_conf.m_isLive ) return;
	// skip if request out already
	if ( g_process.m_powerReqOut ) return;
	// the url
	char *url = "http://10.5.0.9/getData.htm";
	// download it
	//log(LOG_INFO,"powermo: getting %s",url);
	// for httpserver
	//Url u; u.set ( url , gbstrlen(url) );
	// mark the request as outstanding so we do not overlap it
	g_process.m_powerReqOut = true;
	// get it
	bool status = g_httpServer.
		getDoc ( url             , // url to download
			 0               , // ip
			 0               , // offset
			 -1              , // size
			 0               , // ifModifiedSince
			 NULL            , // state
			 gotPowerWrapper , // callback
			 30*1000         , // timeout
			 0               , // proxy ip
			 0               , // proxy port
			 1*1024*1024     , // maxLen
			 1*1024*1024     , // maxOtherLen
			 "Mozilla/4.0 "
			 "(compatible; MSIE 6.0; Windows 98; "
			 "Win 9x 4.90)"  ,
			 //false           , // respect download limit?
			 "HTTP/1.1"      );// fake 1.1 otherwise we get error!
	// wait for it
	if ( ! status ) return;
	// i guess it is back!
	g_process.m_powerReqOut = false;
	// call this to wrap things up
	g_process.gotPower ( NULL );
}

void gotPowerWrapper ( void *state , TcpSocket *s ) {
	g_process.gotPower ( s );
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Process::gotPower ( TcpSocket *s ) {

	// i guess it is back!
	g_process.m_powerReqOut = false;

	if ( ! s ) {
		log("powermo: got NULL socket");
		return true;
	}

	// point into buffer
	char *buf     ;
	int32_t  bufSize ;

	// assume power is on
	int32_t val = 0;
	HttpMime mime;
	char *content;
	int32_t  contentLen;
	char *p;
	char *dataCtrTempStr;
	char *roofTempStr;
	char *tag1,*tag2;
	float newTemp;

	if ( g_errno ) {
		log("powermo: had error getting power state: %s. assuming "
		    "power on.",
		    mstrerror(g_errno));
		//return true;
		// assume power went off
		//val = 1;
		goto skip;
	}
	// point into buffer
	buf     = s->m_readBuf;
	bufSize = s->m_readOffset;

	// note it
	//log(LOG_INFO,"powermo: got power reply");

	if ( ! buf ) {
		log(LOG_INFO,"powermo: got empty reply. assuming power on.");
		// return true;
		// assume power went off
		//val = 1;
		goto skip;
	}

	mime.set ( buf , bufSize , NULL );
	content    = buf     + mime.getMimeLen();
	contentLen = bufSize - mime.getMimeLen();
	content[contentLen]='\0';

	// get the state of the power!
	p = strstr ( content ,"\"power\",status:" );
	// panic?
	if ( ! p ) {
		log("powermo: could not parse out power from room alert. "
		    "assuming power on. "
		    "content = %s",content);
		//return true;
		// assume power went off
		//val = 1;
		goto skip;
	}
 
	// . get the value
	// . val is 0 if the power is ON!!!!
	// . val is non-zero if the power is OFF!!!
	val = atoi ( p + 15 );
	// random values for testing!!
	//val = rand()%2;


	// 
	// . now get the temperature in the data ctr and the roof
	// . log it every hour i guess for shits and giggles
	// . if the roof temp is less than the data ctr temp then
	//   we want to keep the fans on, otherwise we need to send an
	//   http request to the power strip control to turn the fans off
	//
	tag1 = "\"Exit Temp\",tempf:\"" ;
	dataCtrTempStr = strstr( content, tag1 );
	if ( ! dataCtrTempStr ) {
		log("powermo: could not parse our data ctr temp from "
		    "room alert.");
		goto skip;
	}
	newTemp = atof ( dataCtrTempStr+ gbstrlen(tag1) );
	if ( newTemp != m_dataCtrTemp )
		log("powermo: data ctr temp changed from %0.1f to %.01f",
		    m_dataCtrTemp,newTemp);
	m_dataCtrTemp = newTemp;


	tag2 = "\"Roof Temp\",tempf:\"";
	roofTempStr = strstr( content, tag2 );
	if ( ! dataCtrTempStr ) {
		log("powermo: could not parse out roof temp from "
		    "room alert.");
		goto skip;
	}

	newTemp = atof ( roofTempStr+gbstrlen(tag2));
	if ( newTemp != m_roofTemp )
		log("powermo: roof     temp changed from %0.1f to %.01f",
		    m_roofTemp,newTemp);
	m_roofTemp = newTemp;


 skip:

	// 0 means the alert is not triggered and power is on
	if ( val == 0 && m_powerIsOn == true ) {
		//log("powermo: power is still ON.");
		return true;
	}
	// if it is off and was off before, don't do anything
	if ( val && m_powerIsOn == false ) {
		log("powermo: power is still OFF.");
		return true;
	}

	char *up = NULL;

	// if it was off before, tell everyone it is back on
	if ( val == 0 && m_powerIsOn == false ) {
		log("powermo: power is back ON!");
		up = "/master?haspower=1&username=msg28&cast=0";
		// update ourselves to prevent sending these multiple times
		//m_powerIsOn = true;
	}
	else if ( val && m_powerIsOn == true ) {
		log("powermo: power is OFF!");
		up = "/master?haspower=0&username=msg28&cast=0";
		// . update ourselves to prevent sending these multiple times
		// . no, we need to make sure to save in Parms.cpp::
		//   CmdPower
		//m_powerIsOn = false;
	}

	// how did this happen?
	if ( m_powerReqOut ) return true;

	// the request url
	//Url ru; ru.set ( up , gbstrlen(up) );
	// set the http reqeust
	if ( ! m_r.set ( up ) ) {
		log("powermo: got httpreqeust set error: %s",
		    mstrerror(g_errno));
		return true;
	}

	// we are out again...
	g_process.m_powerReqOut = true;

	log("powermo: sending notice to all hosts.");

	SafeBuf parmList;

	// add the parm rec as a parm cmd
	if ( ! g_parms.addNewParmToList1 ( &parmList,
					   (collnum_t)-1,
					   NULL, // parmval (argument)
					   -1, // collnum (-1 -> globalconf)
					   "poweron") ) // CommandPowerOn()!
		return true;

	// . use the broadcast call here so things keep their order!
	// . we do not need a callback when they have been completely
	//   broadcasted to all hosts so use NULL for that
	g_parms.broadcastParmList ( &parmList , NULL , NULL );

	// . turn off spiders
	// . also show that power is off now!
	//if ( ! m_msg28.massConfig ( m_r.getRequest() ,
	//			    NULL           , // state
	//			    doneCmdWrapper ) )
	//	// return false if this blocked
	//	return false;

	// . hmmm.. it did not block
	// . this does not block either
	doneCmdWrapper ( NULL );
	return true;
}

void doneCmdWrapper ( void *state ) {
	// we are back
	g_process.m_powerReqOut = false;
	// note it
	log("powermo: DONE sending notice to all hosts.");
}

void hdtempWrapper ( int fd , void *state ) {

	// current local time
	int32_t now = getTime();

	// from SpiderProxy.h
	static int32_t s_lastTime = 0;
	if ( ! s_lastTime ) s_lastTime = now;
	// reset spider proxy stats every hour to alleviate false positives
	if ( now - s_lastTime >= 3600 ) {
		s_lastTime = now;
		resetProxyStats();
	}

	// also download test urls from spider proxies to ensure they
	// are up and running properly
	downloadTestUrlFromProxies();

	// reset this... why?
	g_errno = 0;
	// do not get if already getting
	if ( g_process.m_threadOut ) return;
	// skip if exiting
	if ( g_process.m_mode == EXIT_MODE ) return;
	// or if haven't waited int32_t enough
	if ( now < s_nextTime ) return;

	// set it
	g_process.m_threadOut = true;
	// . call thread to call popen
	// . callThread returns true on success, in which case we block
	if ( g_threads.call ( FILTER_THREAD        ,
			      MAX_NICENESS         ,
			      NULL                 , // this
			      hdtempDoneWrapper    ,
			      hdtempStartWrapper_r ) ) return;
	// back
	g_process.m_threadOut = false;
	// . call it directly
	// . only mention once to avoid log spam
	static bool s_first = true;
	if ( s_first ) {
		s_first = false;
		log("build: Could not spawn thread for call to get hd temps. "
		    "Ignoring hd temps. Only logging once.");
	}
	// MDW: comment these two guys out to avoid calling it for now
	// get the data
	//hdtempStartWrapper_r ( false , NULL ); // am thread?
	// and finish it off
	//hdtempDoneWrapper ( NULL , NULL );
}

// come back here
void hdtempDoneWrapper ( void *state , ThreadEntry *t ) {
	// we are back
	g_process.m_threadOut = false;
	// current local time
	int32_t now = getTime();
	// if we had an error, do not schedule again for an hour
	//if ( s_flag ) s_nextTime = now + 3600;
	// reset it
	//s_flag = 0;
	// send email alert if too hot
	Host *h = g_hostdb.m_myHost;
	// get max temp
	int32_t max = 0;
	for ( int32_t i = 0 ; i < 4 ; i++ ) {
		int16_t t = h->m_pingInfo.m_hdtemps[i];
		if ( t > max ) max = t;
	}
	// . leave if ok
	// . the seagates tend to have a max CASE TEMP of 69 C
	// . it says the operating temps are 0 to 60 though, so
	//   i am assuming that is ambient?
	// . but this temp is probably the case temp that we are measuring
	if ( max <= g_conf.m_maxHardDriveTemp ) return;
	// leave if we already sent and alert within 5 mins
	static int32_t s_lasttime = 0;
	if ( now - s_lasttime < 5*60 ) return;
	// prepare msg to send
	char msgbuf[1024];
	Host *h0 = g_hostdb.getHost ( 0 );
	snprintf(msgbuf, 1024,
		 "hostid %"INT32" has overheated HD at %"INT32" C "
		 "cluster=%s (%s). Disabling spiders.",
		 h->m_hostId,
		 (int32_t)max,
		 g_conf.m_clusterName,
		 iptoa(h0->m_ip));
	// send it, force it, so even if email alerts off, it sends it
	g_pingServer.sendEmail ( NULL   , // Host *h
				 msgbuf , // char *errmsg = NULL , 
				 true   , // bool sendToAdmin = true ,
				 false  , // bool oom = false ,
				 false  , // bool kernelErrors = false ,
				 false  , // bool parmChanged  = false ,
				 true   );// bool forceIt      = false );

	s_lasttime = now;
}


// set Process::m_diskUsage
float getDiskUsage ( int64_t *diskAvail ) {

	// first get disk usage now
	char cmd[10048];
	char out[1024];
	sprintf(out,"%sdiskusage",g_hostdb.m_dir);
	snprintf(cmd,10000,"df -ka %s | tail -1 | "
		 "awk '{print $4\" \"$5}' > %s",
		 g_hostdb.m_dir,
		 out);
	errno = 0;
	// time it to see how long it took. could it be causing load spikes?
	//log("process: begin df -ka");
	int err = system ( cmd );
	//log("process: end   df -ka");
	if ( err == 127 ) {
		log("build: /bin/sh does not exist. can not get disk usage.");
		return -1.0; // unknown
	}
	// this will happen if you don't upgrade glibc to 2.2.4-32 or above
	if ( err != 0 ) {
		log("build: Call to system(\"%s\") had error: %s",
		    cmd,mstrerror(errno));
		return -1.0; // unknown
	}

	// read in temperatures from file
	int fd = open ( out , O_RDONLY );
	if ( fd < 0 ) {
		//m_errno = errno;
		log("build: Could not open %s for reading: %s.",
		    out,mstrerror(errno));
		return -1.0; // unknown
	}
	char buf[2000];
	int32_t r = read ( fd , buf , sizeof(buf)-1 );
	// did we get an error
	if ( r <= 0 ) {
		//m_errno = errno;
		log("build: Error reading %s: %s.",out,mstrerror(errno));
		close ( fd );
		return -1.0; // unknown
	}
	buf[(size_t)r] = '\0';
	// clean up shop
	close ( fd );

	float usage;
	int64_t avail;
	sscanf(buf,"%"INT64" %f",&avail,&usage);
	// it is in KB so make it into bytes
	if ( diskAvail ) *diskAvail = avail * 1000LL;
	return usage;
}

// . sets m_errno on error
// . taken from Msg16.cpp
void *hdtempStartWrapper_r ( void *state , ThreadEntry *t ) {

	// run the df -ka cmd
	g_process.m_diskUsage = getDiskUsage( &g_process.m_diskAvail );

	// ignore temps now. ssds don't have it
	return NULL;
}

void Process::callHeartbeat () {
	heartbeatWrapper ( 0 , NULL );
}

void heartbeatWrapper ( int fd , void *state ) {
	static int64_t s_last = 0LL;
	static int64_t s_lastNumAlarms = 0LL;
	int64_t now = gettimeofdayInMilliseconds();
	if ( s_last == 0LL ) {
		s_last = now;
		s_lastNumAlarms = g_numAlarms;
		return;
	}
	// . log when we've gone 100+ ms over our scheduled beat
	// . this is a sign things are jammed up
	int64_t elapsed = now - s_last;
	if ( elapsed > 200 ) 
		// now we print the # of elapsed alarms. that way we will
		// know if the alarms were going off or not...
		// this happens if the rt sig queue is overflowed.
		// check the "cat /proc/<pid>/status | grep SigQ" output
		// to see if its overflowed. hopefully i will fix this by
		// queue the signals myself in Loop.cpp.
		log("db: missed calling niceness 0 heartbeatWrapper "
		    "function by %"INT64" ms. Either you need a quickpoll "
		    "somewhere or a niceness 0 function is taking too long. "
		    "Num elapsed alarms = "
		    "%"INT32"", elapsed-100,(int32_t)(g_numAlarms - 
						      s_lastNumAlarms));
	s_last = now;
	s_lastNumAlarms = g_numAlarms;

	// save this time so the sig alarm handler can see how long
	// it has been since we've been called, so after 10000 ms it
	// can dump core and we can see what is holding things up
	g_process.m_lastHeartbeatApprox = gettimeofdayInMilliseconds();
}


/*
void diskHeartbeatWrapper ( int fd , void *state ) {

	// skip this now that we use SSDs
	return;

	bool stuck = false;

	// do we have reads waiting?
	bool isWaiting = 
		( g_threads.m_threadQueues[DISK_THREAD].m_hiReturned <
		  g_threads.m_threadQueues[DISK_THREAD].m_hiLaunched ) ;

	// . must have been more than 1.5 secs since last read finished
	// . if the disk read queue is empty when we add a new read thread
	//   request in BigFile.cpp, we set g_diskRequestAdded to g_now
	if ( isWaiting && 
	     g_now - g_lastDiskReadCompleted >= 1500 &&
	     g_now - g_lastDiskReadStarted   >= 1500  )
		stuck = true;

	// return if not stuck
	if ( ! stuck ) {
		// if we just got unstuck, log that
		if ( g_diskIsStuck )
			log("gb: disk is now unstuck.");
		g_diskIsStuck = false;
		return;
	}

	// if first time, log that
	if ( ! g_diskIsStuck )
		log("gb: disk appears to be stuck.");

	// flag it so BigFile.cpp and File.cpp just return EDISKSTUCK and so
	// we do not kill all disk read threads again
	g_diskIsStuck = true;

	// now call the callback of all disk read threads that have niceness
	// 0 but set g_errno to EDISKSTUCK. when the actual read finally does
	// complete it should just basically stop...
	//
	// take this out now that we have solid states!!!!!!!!!!!!!
	//
	//g_threads.bailOnReads();
}
*/

// called by PingServer.cpp only as of now
int64_t Process::getTotalDocsIndexed() {
	if ( m_totalDocsIndexed == -1LL ) {
		Rdb *rdb = g_clusterdb.getRdb();
		// useCache = true
		m_totalDocsIndexed = rdb->getNumTotalRecs(true);
	}
	return m_totalDocsIndexed;
}

void processSleepWrapper ( int fd , void *state ) {

	if ( g_process.m_mode == EXIT_MODE ) {
		g_process.shutdown2();
		return;
	}

	if ( g_process.m_mode == SAVE_MODE ) {
		g_process.save2();
		return;
	}

	if ( g_process.m_mode == LOCK_MODE ) {
		g_process.save2();
		return;
	}

	if ( g_process.m_mode != NO_MODE ) {
		return;
	}

	// update global rec count
	static int32_t s_rcount = 0;

	// every 2 seconds
	if ( ++s_rcount >= 4 ) {
		s_rcount = 0;
		// PingServer.cpp uses this
		Rdb *rdb = g_clusterdb.getRdb();
		g_process.m_totalDocsIndexed = rdb->getNumTotalRecs();
	}

	// do not do autosave if no power
	if ( ! g_process.m_powerIsOn ) return;

	// . i guess try to autoscale the cluster in cast hosts.conf changed
	// . if all pings came in and all hosts have the same hosts.conf
	//   and if we detected any shard imbalance at startup we have to
	//   scan all rdbs for records that don't belong to us and send them
	//   where they should go
	// . returns right away in most cases
	g_rebalance.rebalanceLoop();

	// in PageInject.cpp startup up any imports that might have been
	// going on before we shutdown last time. 
	resumeImports();

	// if doing the final part of a repair.cpp loop where we convert
	// titledb2 files to titledb etc. then do not save!
	if ( g_repairMode == 7 ) return;

	// autosave? override this if power is off, we need to save the data!
	//if (g_conf.m_autoSaveFrequency <= 0 && g_process.m_powerIsOn) return;
	if ( g_conf.m_autoSaveFrequency <= 0 ) return;
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return;

	// skip autosave while sync in progress!
	if ( g_process.m_suspendAutoSave ) return;

	// need to have a clock unified with host #0. i guess proxy
	// does not sync with host #0 though
	//if ( ! isClockInSync() && ! g_hostdb.m_myHost->m_isProxy ) return;

	// get time the day started
	int32_t now;
	if ( g_hostdb.m_myHost->m_isProxy ) now = getTimeLocal();
	else {
		// need to be in sync with host #0's clock
		if ( ! isClockInSync() ) return;
		// that way autosaves all happen at about the same time
		now = getTimeGlobal();
	}

	// set this for the first time
	if ( g_process.m_lastSaveTime == 0 )
		g_process.m_lastSaveTime = now;
	
	//
	// we now try to align our autosaves with start of the day so that
	// all hosts autosave at the exact same time!! this should keep
	// performance somewhat consistent.
	//
	
	// get frequency in minutes
	int32_t freq = (int32_t)g_conf.m_autoSaveFrequency ;
	// convert into seconds
	freq *= 60;
	// how many seconds into the day has it been?
	int32_t offset   = now % (24*3600);
	int32_t dayStart = now - offset;
	// how many times should we have autosaved so far for this day?
	int32_t autosaveCount = offset / freq;
	// convert to when it should have been last autosaved
	int32_t nextLastSaveTime = (autosaveCount * freq) + dayStart;
	
	// if we already saved it for that time, bail
	if ( g_process.m_lastSaveTime >= nextLastSaveTime ) return;
	
	//int64_t now = gettimeofdayInMillisecondsLocal();
	// . get a snapshot of the load average...
	// . MDW: disable for now. not really used...
	//update_load_average(now);
	// convert from minutes in milliseconds
	//int64_t delta = (int64_t)g_conf.m_autoSaveFrequency * 60000LL;
	// if power is off make this every 30 seconds temporarily!
	//if ( ! g_process.m_powerIsOn ) delta = 30000;
	// return if we have not waited int32_t enough
	//if ( now - g_process.m_lastSaveTime < delta ) return;
	// update
	g_process.m_lastSaveTime = nextLastSaveTime;//now;
	// save everything
	logf(LOG_INFO,"db: Autosaving.");
	g_inAutoSave = 1;
	g_process.save();
	g_inAutoSave = 0;
}

bool Process::save ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	// bail if doing something already
	if ( m_mode != 0 ) return true;
	// log it
	logf(LOG_INFO,"db: Entering lock mode for saving.");
	m_mode   = LOCK_MODE; // SAVE_MODE;
	m_urgent = false;
	m_calledSave = false;
	return save2();
}

void Process::shutdownAbort ( bool save_on_abort ) {
	bool force_abort = false;

	// write fatal_error file
	int fd = open(getAbortFileName(), O_WRONLY | O_CREAT, getFileCreationFlags() );
	if ( !fd ) {
		log(LOG_ERROR, "process: Unable to create file '%s'", getAbortFileName());

		// we can't create file, so make sure we exit gb restart loop
		force_abort = true;
	}

	printStackTrace(true);

	// follow usual segfault logic, only when required & we can create file
	// if we can't create file, and we follow the usual logic, we risk not blocking startup
	if ( save_on_abort && !force_abort ) {
		shutdown(true);
	}

	// let's ensure our core file can dump
	rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE, &lim) ) {
		log(LOG_ERROR, "gb: setrlimit: %s", mstrerror(errno) );
	}

	abort();
}

bool Process::shutdown ( bool urgent, void  *state, void (*callback) (void *state )) {
	// bail if doing something already
	if ( m_mode != 0 ) {
		// if already in exit mode, just return
		if ( m_mode == EXIT_MODE ) {
			return true;
		}

		// otherwise, log it!
		log("process: shutdown called, but mode is %"INT32"", (int32_t)m_mode);

		return true;
	}

	m_mode   = EXIT_MODE;
	m_urgent = urgent;

	m_calledSave = false;

	// check memory buffers for overruns/underrunds to see if that caused this core
	if ( urgent ) {
		g_mem.printBreeches(false);
	}

	if (!shutdown2()) {
		m_callbackState = state;
		m_callback = callback;
		return false;
	}

	return true;
}

// return false if blocked/waiting
bool Process::save2 ( ) {

	// MDW: why was this here? i commented it out. we need to do 
	//      quickpolls when autosaving for sure.
	//g_loop.disableTimer();

	// only the main process can call this
	if ( g_threads.amThread() ) return true;

	// . wait for any dump to complete
	// . when merging titldb, it sets Rdb::m_dump.m_isDumping to true
	//   because it is dumping the results of the merge to a file.
	//   occasionally it will initiate a dump of tfndb which will not be 
	//   possible because Rdb/RdbDump checks g_process.m_mode == SAVE_MODE,
	//   and do not allow dumps to begin if that is true! so we end up in 
	//   deadlock! the save can not complete 
	if ( isRdbDumping() ) return false;

	// ok, now nobody is dumping, etc. make it so no dumps can start.
	// Rdb.cpp/RdbDump.cpp check for this and will not dump if it is 
	// set to SAVE_MODE
	m_mode = SAVE_MODE;

	logf(LOG_INFO,"gb: Saving data to disk. Disabling writes.");

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ETRYAGAIN error replies
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites( false );

	bool useThreads = true;

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if ( ! saveRdbTrees ( useThreads , false ) ) return false;

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	// . save these last since maps can be auto-regenerated at startup
	if ( ! saveRdbMaps() ) {
		return false;
	}

	// . save the conf files and caches. these block the cpu.
	// . save these first since more important than the stuff below
	// . no, to avoid saving multiple times, put this last since the
	//   stuff above may block and we have to re-call this function      
	if ( ! saveBlockingFiles1() ) return false;

	// save addsInProgress.dat etc. if power goes off. this should be the
	// one time we are called from power going off... since we do not
	// do autosave when the power is off. this just blocks and never
	// returns false, so call it with checking the return value.
	if ( ! g_process.m_powerIsOn ) saveBlockingFiles2() ;
	// for Test.cpp parser test we want to save the waitingtree.dat
	else if ( g_threads.m_disabled ) saveBlockingFiles2() ;

	// until all caches have saved, disable them
	g_cacheWritesEnabled = false;

	// . save caches
	// . returns true if NO cache needs to be saved
	//if ( ! saveRdbCaches ( useThreads ) ) return false;

	// bring them back
	g_cacheWritesEnabled = true;

	// reenable tree writes since saves were completed
	enableTreeWrites( false );

	log(LOG_INFO,"gb: Saved data to disk. Re-enabling Writes.");

	// update
	//m_lastSaveTime = gettimeofdayInMillisecondsLocal();

	// unlock
	m_mode = NO_MODE;

	return true;
}



// . return false if blocked/waiting
// . this is the SAVE BEFORE EXITING
bool Process::shutdown2 ( ) {
	g_loop.disableQuickpollTimer();

	// only the main process can call this
	if ( g_threads.amThread() ) return true;

	if ( m_urgent ) {
		log(LOG_INFO,"gb: Shutting down urgently. Timed try #%"INT32".", m_try++);
	} else {
		log(LOG_INFO,"gb: Shutting down. Timed try #%"INT32".", m_try++);
	}

	// switch to urgent if having problems
	if ( m_try >= 10 ) {
		m_urgent = true;
	}

	// turn off statsdb so it does not try to add records for these writes
	g_statsdb.m_disabled = true;

	if ( g_threads.areThreadsEnabled () ) {
		log("gb: disabling threads");
		// now disable threads so we don't exit while threads are 
		// outstanding
		g_threads.disableThreads();
	}

	// . suspend all merges
	g_merge.suspendMerge () ;
	g_merge2.suspendMerge() ;

	// assume we will use threads
	// no, not now that we disabled them
	bool useThreads = false;//true;

	// if urgent do not allow any further threads to be spawned unless
	// they were already queued
	if ( m_urgent ) {
		// do not use thread spawning
		useThreads = false;
		// turn off all threads just in case
		if ( ! useThreads ) g_threads.disableThreads();
	}

	static bool s_printed = false;

	// wait for all threads to return
	//int32_t n = g_threads.getNumThreadsOutOrQueued() ;
	int32_t n = g_threads.getNumWriteThreadsOut();
	if ( n != 0 && ! m_urgent ) {
		log(LOG_INFO,"gb: Has %"INT32" write threads out. Waiting for them to finish.", n);
		return false;
	} else if ( ! s_printed && ! m_urgent ) {
		s_printed = true;
		log(LOG_INFO,"gb: No write threads out.");
	}

	// disable all spidering
	// we can exit while spiders are in the queue because
	// if they are in the middle of being added they will be
	// saved by spider restore
	// wait for all spiders to clear

	// don't shut the crawler down on a core
	//g_conf.m_spideringEnabled = false;

	//g_conf.m_injectionEnabled = false;

	// make sure they are in a saveable state. we need to make sure
	// they have dumped out the latest merged list and updated the 
	// appropriate RdbMap so we can save it below.
	bool wait = false;
	if ( g_merge.m_isMerging  && ! g_merge.m_isReadyToSave  ) wait = true;
	if ( g_merge2.m_isMerging && ! g_merge2.m_isReadyToSave ) wait = true;
	// wait for any dump to complete
	if ( isRdbDumping() ) wait = true;
	// . wait for the merge or dump to complete
	// . but NOT if urgent...
	// . this stuff holds everything up too long, take out, we already
	//   wait for write threads to complete, that should be good enough
	//if ( wait && ! m_urgent ) return false;

	// . disable adds/deletes on all rdb trees
	// . Msg1 requests will get ECLOSING error msgs
	// . this is instantaneous because all tree mods happen in this
	//   main process, not in a thread
	disableTreeWrites( true );

	// . tell all rdbs to save trees
	// . will return true if no rdb tree needs a save
	if ( ! saveRdbTrees ( useThreads , true ) ) 
		if ( ! m_urgent ) return false;

	// save this right after the trees in case we core
	// in saveRdbMaps() again due to the core we are
	// handling now corrupting memory
	if ( m_repairNeedsSave ) {
		m_repairNeedsSave = false;
		g_repair.save();
	}

	// . save all rdb maps if they need it
	// . will return true if no rdb map needs a save
	if ( ! saveRdbMaps() ) {
		if ( ! m_urgent ) {
			return false;
		}
	}

	int64_t now = gettimeofdayInMillisecondsLocal();
	if ( m_firstShutdownTime == 0 ) m_firstShutdownTime = now;

	// these udp servers will not read in new requests or allow
	// new requests to be sent. they will timeout any outstanding
	// UdpSlots, and when empty they will return true here. they will
	// close their m_sock and set it to -1 which should force their
	// thread to exit.
	// if not urgent, they will wait for a while for the 
	// sockets/slots to clear up.
	// however, if 5 seconds or more have elapsed then force it
	bool udpUrgent = m_urgent;
	if ( now - m_firstShutdownTime >= 3000 ) udpUrgent = true;

	if ( ! g_dns.m_udpServer.shutdown ( udpUrgent ) )
		if ( ! udpUrgent ) return false;

	// . send notes to all the hosts in the network telling them we're
	//   shutting down
	// . this returns false if it blocks
	// . we don't care if it blocks or not
	// . don't bother asking the hosts to send an email alert for us
	//   since we're going down gracefully by letting everyone know
	// . don't send this unless we are very sure we can shutdown NOW
	// . i.e. no blocking after this call!
	if ( ! m_sentShutdownNote && ! m_urgent ) {
		log(LOG_INFO,"gb: Broadcasting shutdown notice.");
		m_sentShutdownNote = true;
		g_pingServer.broadcastShutdownNotes ( false , //sendEmailAlert?
						      NULL  , 
						      NULL  );
	}
	//broadcastShutdownNotes uses g_udpServer so we do this last.
	if ( ! g_udpServer.shutdown ( udpUrgent ) )
		if ( ! udpUrgent ) return false;


	g_profiler.stopRealTimeProfiler(false);
	g_profiler.cleanup();

	// save the conf files and caches. these block the cpu.
	if ( m_blockersNeedSave ) {
		m_blockersNeedSave = false;
		if (!g_conf.m_readOnlyMode)
			logf(LOG_INFO,"gb: Saving miscellaneous data files.");
		saveBlockingFiles1() ;
		saveBlockingFiles2() ;
	}

	// . save all rdb caches if they need it
	// . do this AFTER udp server is shut down so cache should not
	//   be accessed any more
	// . will return true if no rdb cache needs a save
	//if ( ! saveRdbCaches ( useThreads ) ) return false;

	// always diable threads at this point so g_threads.call() will
	// always return false and we do not queue any new threads for
	// spawning
	g_threads.disableThreads();

	// urgent means we need to dump core, SEGV or something
	if ( m_urgent ) {
		// log it
		log("gb: Dumping core after saving.");

		// at least destroy the page caches that have shared memory
		// because they seem to not clean it up
		//resetPageCaches();

		// let's ensure our core file can dump
		struct rlimit lim;
		lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
		if ( setrlimit(RLIMIT_CORE, &lim) ) {
			log("gb: setrlimit: %s.", mstrerror(errno) );
		}

		// this is the trick: it will trigger the core dump by
		// calling the original SIGSEGV handler.
		int signum = SIGSEGV;
		signal(signum, SIG_DFL);
		kill(getpid(), signum);
		// default handler should be called after we return now

		// . force an abnormal termination which will cause a core dump
		// . do not dump core on SIGHUP signals any more though
		//abort();
		// keep compiler happy
		return true;
	}



	// cleanup threads, this also launches them too
	g_threads.timedCleanUp(0x7fffffff,MAX_NICENESS);

	// wait for all threads to complete...
	//int32_t n = g_threads.getNumThreadsOutOrQueued() ;
	//if ( n > 0 )
	//	return log(LOG_INFO,
	//		   "gb: Waiting for %"INT32" threads to complete.",n);

	//log(LOG_INFO,"gb: Has %"INT32" threads out.",n);


	//ok, resetAll will close httpServer's socket so now is the time to 
	//call the callback.
	if(m_callbackState) (*m_callback)(m_callbackState);

	// tell Mutlicast::reset() not to destroy all the slots! that cores!
	m_exiting = true;

	// let everyone free their mem
	resetAll();

	// show what mem was not freed
	g_mem.printMem();

	// kill any outstanding hd temp thread?
	if ( g_process.m_threadOut ) 
		log(LOG_INFO,"gb: still has hdtemp thread");


	log("gb. EXITING.");

	// exit abruptly
	exit(0);

	// keep compiler happy
	return true;
}

void Process::disableTreeWrites ( bool shuttingDown ) {
	// loop over all Rdbs
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		// if we save doledb while spidering it screws us up
		// because Spider.cpp can not directly write into the
		// rdb tree and it expects that to always be available!
		if ( ! shuttingDown && rdb->m_rdbId == RDB_DOLEDB )
			continue;
		rdb->disableWrites();
	}
	// don't save spider related trees if not shutting down
	if ( ! shuttingDown ) return;
	// disable all spider trees and tables
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->m_waitingTree .disableWrites();
		sc->m_waitingTable.disableWrites();
		sc->m_doleIpTable .disableWrites();
	}
	
}

void Process::enableTreeWrites ( bool shuttingDown ) {
	// loop over all Rdbs
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->enableWrites();
	}
	// don't save spider related trees if not shutting down
	if ( ! shuttingDown ) return;
	// enable all waiting trees
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->m_waitingTree .enableWrites();
		sc->m_waitingTable.enableWrites();
		sc->m_doleIpTable .enableWrites();
	}
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::isRdbDumping ( ) {
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->m_dump.m_isDumping ) return true;
	}
	return false;
}

bool Process::isRdbMerging ( ) {
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		if ( rdb->isMerging() ) return true;
	}
	return false;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbTrees ( bool useThread , bool shuttingDown ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	// no thread if shutting down
	if ( shuttingDown ) useThread = false;
	// debug note
	if ( shuttingDown ) log("gb: trying to shutdown");
	// turn off statsdb until everyone is done
	//g_statsdb.m_disabled = true;
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		if ( m_calledSave ) {
			log("gb: already saved trees, skipping.");
			break;
		}
		Rdb *rdb = m_rdbs[i];
		// if we save doledb while spidering it screws us up
		// because Spider.cpp can not directly write into the
		// rdb tree and it expects that to always be available!
		if ( ! shuttingDown && rdb->m_rdbId == RDB_DOLEDB )
			continue;
		// note it
		if ( ! rdb->m_dbname || ! rdb->m_dbname[0] )
			log("gb: calling save tree for rdbid %i",
			    (int)rdb->m_rdbId);
		else
			log("gb: calling save tree for %s",
			    rdb->m_dbname);

		rdb->saveTree ( useThread );
	}

	// . save waitingtrees for each collection, blocks.
	// . can we make this non-blocking?
	// . true = "usethreads"
	// . all writes have been disabled, so should be cleanly saved
	// . if this did not block that means it does not need any saving
	// . this just launched all the write threads for the trees/tables
	//   that need to be saved. it sets m_isSaving once they are all 
	//   launched.
	// . and sets m_isSaving=false on SpiderCache::doneSaving when they
	//   are all done.
	if ( shuttingDown ) g_spiderCache.save ( useThread );

	// do not re-save the stuff we just did this round
	m_calledSave = true;
	// quickly re-enable if statsdb tree does not need save any more
	//if ( ! g_statsdb.m_rdb.needsSave() ) g_statsdb.m_disabled = false;
	// check if any need to finish saving
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		// do not return until all saved if we are shutting down
		if ( shuttingDown ) break;
		//if ( rdb->needsSave ( ) ) return false;
		// we disable the tree while saving so we can't really add recs
		// to one rdb tree while saving, but for crawlbot
		// we might have added or deleted collections.
		if ( rdb->isSavingTree ( ) ) return false;
	}

	// only save spiderdb based trees if shutting down so we can
	// still write to them without writes being disabled
	if ( ! shuttingDown ) return true;

	// . check spider cache files (doleiptable waitingtree etc.)
	// . this should return true if it still has some files that haven't
	//   saved to disk yet... so if it returns true we return false 
	//   indicating that we are still waiting!
	if ( ! shuttingDown && g_spiderCache.needsSave () ) return false;

	// reset for next call
	m_calledSave = false;
	// everyone is done saving
	return true;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
bool Process::saveRdbMaps() {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->saveMaps();
	}

	// everyone is done saving
	return true;
}

// . returns false if blocked, true otherwise
// . calls callback when done saving
/*
bool Process::saveRdbCaches ( bool useThread ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;
	//useThread = false;
	// loop over all Rdbs and save them
	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		// . returns true if cache does not need save
		// . returns false if blocked and is saving
		// . returns true if useThreads is false
		// . we return false if it blocks
		if ( ! rdb->saveCache ( useThread ) ) return false;
	}
	// everyone is done saving
	return true;
}
*/

bool Process::saveBlockingFiles1 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) return true;

	// save the gb.conf file now
	g_conf.save();
	// save the conf files
	// if autosave and we have over 20 colls, just make host #0 do it
        g_collectiondb.save();
	// . save repair state
	// . this is repeated above too
	// . keep it here for auto-save
	g_repair.save();

	// save our place during a rebalance
	g_rebalance.saveRebalanceFile();

	// save stats on spider proxies if any
	saveSpiderProxyStats();

	// save the query log buffer if it was modified by the 
	// runSeoQueryLoop() in seo.cpp which updates its
	// QueryLogEntry::m_minTop50Score member and corresponding timestamp
	if ( g_qbufNeedSave ) {
		char fname[1024];
		sprintf(fname,"querylog.host%"INT32".dat",g_hostdb.m_hostId);
		g_qbuf.saveToFile(g_hostdb.m_dir,fname);
		log("process: saving changes to %s",fname);
		g_qbufNeedSave = false;
	}
	
	// . save the add state from Msg4.cpp
	// . these are records in the middle of being added to rdbs across
	//   the cluster
	// . saves to "addsinprogress.saving" and moves to .saved
	// . eventually this may replace "spiderrestore.dat"
	if ( g_repair.isRepairActive() ) saveAddsInProgress ( "repair-" );
	else                             saveAddsInProgress ( NULL      );

	// in fctypes.cpp. save the clock offset from host #0's clock so
	// our startup is fast again
	saveTimeAdjustment();

	return true;
}

bool Process::saveBlockingFiles2 ( ) {
	// never if in read only mode
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	// the spider dup request cache
	//g_spiderCache.m_dupCache.save( false ); // use threads?

	// save waitingtrees for each collection, blocks.
	//if ( ! g_spiderCache.save() ) return false;

	// save what templates each turk has turked
	//g_templateTable.save( g_hostdb.m_dir , "turkedtemplates.dat" );

	// the robots.txt cache
	Msg13::getHttpCacheRobots()->save( false ); // use threads?

	// save our caches
	for ( int32_t i = 0; i < MAX_GENERIC_CACHES; i++ ) {
		if ( g_genericCache[i].useDisk() ) {
			// do not use threads
			g_genericCache[i].save( false );
		}
	}

	// save dead wait cache
	//if ( g_deadWaitCache.useDisk     () ) 
	//	g_deadWaitCache    .save ();
	//if ( g_forcedCache.useDisk       () ) 
	//	g_forcedCache      .save ( false ); // use threads?
	//if ( g_alreadyAddedCache.useDisk () ) 
	//	g_alreadyAddedCache.save ( false ); // use threads?

	// save dns caches
	RdbCache *c = g_dns.getCache();
	if (c && c->useDisk()) {
		c->save( false ); // use threads?
	}

	// save quota cache
	//c = &g_qtable;
        //if ( c->useDisk() ) c->save( false ); // use threads?
	// save current spidering process, "spiderrestore.dat"
	//g_spiderLoop.saveCurrentSpidering();

	// if doing titlerec imports in PageInject.cpp, save cursors,
	// i.e. file offsets
	saveImportStates();

	// this one too
	//      g_classifier.save();
	
	// save the turk url cache, urls and user states
	//g_pageTurk.saveCache();

	return true;
}

void Process::resetAll ( ) {
	g_log             .reset();
	g_hostdb          .reset();
	g_spiderLoop      .reset();

	for ( int32_t i = 0 ; i < m_numRdbs ; i++ ) {
		Rdb *rdb = m_rdbs[i];
		rdb->reset();
	}

	g_collectiondb    .reset();
	g_dns             .reset();
	g_udpServer       .reset();
	g_httpServer      .reset();
	g_loop            .reset();
	g_speller         .reset();
	g_spiderCache     .reset();
	g_threads         .reset();
	g_ucUpperMap      .reset();
	g_ucLowerMap      .reset();
	g_ucProps         .reset();
	g_ucScripts       .reset();
	g_profiler        .reset();

	for ( int32_t i = 0; i < MAX_GENERIC_CACHES; i++ )
		g_genericCache[i].reset();

	// reset disk page caches
	resetPageCaches();

	// termfreq cache in Posdb.cpp
	g_termFreqCache.reset();

	g_wiktionary.reset();

	g_countryCode.reset();

	s_clusterdbQuickCache.reset();
	s_hammerCache.reset();

	resetDecompTables();
	resetPageAddUrl();
	resetHttpMime();
	reset_iana_charset();
	resetDomains();
	resetEntities();
	resetQuery();
	resetStopWords();
	resetAbbrTable();
	resetUnicode();

	// reset other caches
	g_dns.reset();
	g_spiderCache.reset();
	g_spiderLoop.reset();
	g_wiki.reset();
	// query log buffer
	g_qbuf.reset();
	g_profiler.reset();
	resetMsg13Caches();
	resetStopWordTables();
	resetTimezoneTables();
	resetTestIpTable();
}

#include "Msg3.h"

void Process::resetPageCaches ( ) {
	log("gb: Resetting page caches.");
	for ( int32_t i = 0 ; i < RDB_END ; i++ ) {
		RdbCache *rpc = getDiskPageCache ( i ); // rdbid = i
		if ( ! rpc ) continue;
		rpc->reset();
	}
}

// ============================================================================
// load average shedding via /proc/loadavg and an async BigFile
typedef struct {
	char		buf[20];		// read buffer
	double		load_average;		// last parsed load avg.
	int64_t		time_req;		// time of last parse
	int64_t		time_parse;
	bool		waiting;		// waiting on async result?
	bool		closing;		// shutting down...
	BigFile		bigfile;
	FileState	filestate;
} loadavg_state;

static loadavg_state		s_st_lavg;
/*
static void loadavg_callback(loadavg_state* state) {
	if (state == NULL)
		return;
	if (s_st_lavg.closing)
		return;
	// MDW: stop doing it for now, it is not accurate
	state->load_average = 0.00;
	return;
	if (s_st_lavg.filestate.m_errno != 0) {
		// do not thrash!
		// leave time_req alone so next open will occur in 5 seconds...
		// do not deadlock!
		// set load_average=0 until file can be successfully re-read.
		s_st_lavg.load_average = 0.0;
		s_st_lavg.bigfile.close();
		s_st_lavg.bigfile.setNonBlocking();
		s_st_lavg.bigfile.open(O_RDONLY);
		log(LOG_INFO, "build: errno %"INT32" reading /proc/loadavg",
			s_st_lavg.filestate.m_errno);
		s_st_lavg.filestate.m_errno = 0;
		return;
	}
	state->time_parse = gettimeofdayInMilliseconds();
	state->waiting = false;
	state->load_average = atof(state->buf);
	log(LOG_DEBUG, "build: loadavg currently: %.2f latency %lld ms",
		state->load_average, state->time_parse - state->time_req);
}
*/

static loadavg_state*		s_state_ptr	=	NULL;
/*
static void update_load_average(int64_t now) {
	// initialize loadavg collection...
	if (s_state_ptr == NULL) {
		s_st_lavg.load_average = 0.0;
		s_st_lavg.time_req = 0;
		s_st_lavg.time_parse = 0;
		s_st_lavg.waiting = false;
		s_st_lavg.closing = false;
		s_st_lavg.bigfile.set("/proc", "loadavg");
		s_st_lavg.bigfile.setNonBlocking();
		s_st_lavg.bigfile.open(O_RDONLY);
		s_state_ptr = &s_st_lavg;
	}
	if (s_st_lavg.closing)
		return;
	if (s_st_lavg.waiting)
		return;
	// the 2.4 kernel updates /proc/loadavg on a 5-second interval
	if (s_st_lavg.waiting == false && now - s_st_lavg.time_req < (5 * 1000))
		return;

	s_st_lavg.time_req = now;
	s_st_lavg.waiting = true;
	s_st_lavg.filestate.m_errno = 0;
	if (!s_st_lavg.bigfile.read(	s_st_lavg.buf,
					sizeof(s_st_lavg.buf),
					0,
					&s_st_lavg.filestate))
		return;
	// if we did not block (as is normal for _this_ file), then
	// call callback directly and update state struct.
	loadavg_callback(s_state_ptr);
	return;
}
*/

double Process::getLoadAvg() {
	return s_st_lavg.load_average;
}
void Process::resetLoadAvg() {
	if (s_state_ptr == NULL)
		return;
	s_st_lavg.closing = true;
	s_state_ptr = NULL;
	s_st_lavg.bigfile.close();
}
//
// ============================================================================

static void gotFanReplyWrapper ( void *state , TcpSocket *s ) {
	g_process.gotFanReply ( s );
}

//
// FAN SWITCH CHECKER
//
void fanSwitchCheckWrapper ( int fd , void *state ) {
	g_process.checkFanSwitch ();
}

void Process::checkFanSwitch ( ) {

	// skip if you are not me, because this controls my custom fan
	if ( ! g_conf.m_isMattWells )
		return;

	// are we in group #0
	bool check = false;
	// get our host
	Host *me = g_hostdb.m_myHost;
	// if we are not host #0 and host #0 is dead, we check it
	if ( me->m_shardNum == 0 && g_hostdb.isDead((int32_t)0) ) 
		check = true;
	// if we are host #0 we always check it
	if ( me->m_hostId == 0 ) check = true;
	// proxy never checks power
	if ( me->m_isProxy ) check = false;
	// if not checking, all done
	if ( ! check ) return;
	// only if live
	//if ( ! g_conf.m_isLive ) return;
	// skip if request out already
	if ( m_fanReqOut ) return;
	// both must be legit
	if ( m_roofTemp    <= -99.0 ) return;
	if ( m_dataCtrTemp <= -99.0 ) return;

	// for shits and giggles log it every 10 minutes
	int32_t now = getTimeLocal();
	static int32_t s_lastLogTime = 0;
	if ( s_lastLogTime - now > 60*10 ) {
		s_lastLogTime = now;
		log("powermo: dataCtrTemp=%.1f roofTemp=%.1f",
		    m_dataCtrTemp, 
		    m_roofTemp );
	}

	// what is the desired state? assume fans on.
	m_desiredFanState = 1;
	// if roof is hotter then fans off! we don't want hotter air.
	if ( //m_roofTemp > m_dataCtrTemp &&
	     // even if roof temp is slightly cooler, turn off fans. it 
	     // needs to be more than 5 degrees cooler.
	     m_roofTemp + 5.0 > m_dataCtrTemp ) 
		// 0 means we want fans to be off
		m_desiredFanState = 0;
	// if matches, leave alone
	if ( m_currentFanState == m_desiredFanState ) return;

	// ok change! the url
	// . the IP9258 power controller
	// . default ip=192.168.1.100
	// . default user=admin
	// . default pwd=12345678
	// . default mac=00:92:00:00:00:3D
	// . the instruction sheet says to run IPEDIT on the cd with your
	//   computer directly connected to the IP9258 via the eth port in 
	//   order to get the default ip address of it.
	// . i changed the ip to 10.5.0.8 since the roomalert is at 10.5.0.9
	// . turn all 4 ports on or off so we can plug the fans into two
	//   separate ports
	/*
	char *url ;
	if ( m_desiredFanState ) 
		url = "http://10.5.0.8/tgi/iocontrol.tgi?"
			"pw1Name=&"
			"P60=On&"     // THIS IS WHAT CHANGES!
			"P60_TS=0&"   // timer seconds?
			"P60_TC=Off&" // timer control?
			"pw2Name=&"
			"P61=On&"
			"P61_TS=0&"
			"P61_TC=Off&"
			"pw3Name=&"
			"P62=On&"
			"P62_TS=0&"
			"P62_TC=Off&"
			"pw4Name=&"
			"P63=On&"
			"P63_TS=0&"
			"P63_TC=Off&"
			"Apply=Apply"
			;
	else 
		url = "http://10.5.0.8/tgi/iocontrol.tgi?"
			"pw1Name=&"
			"P60=Off&"    // THIS IS WHAT CHANGES!
			"P60_TS=0&"   // timer seconds?
			"P60_TC=Off&" // timer control?
			"pw2Name=&"
			"P61=Off&"
			"P61_TS=0&"
			"P61_TC=Off&"
			"pw3Name=&"
			"P62=Off&"
			"P62_TS=0&"
			"P62_TC=Off&"
			"pw4Name=&"
			"P63=Off&"
			"P63_TS=0&"
			"P63_TC=Off&"
			"Apply=Apply"
			;
	// . make a cookie with the login info
	// . on chrome open the console and click "Network" tab 
	//   to view the http network requests and replies
	char *cookie = "admin=12345678; Taifatech=yes";
	*/

	//
	// the new power switch is hopefully less flaky!
	//
	SafeBuf urlBuf;

	if ( m_desiredFanState ) {
		// this turns it on
		if ( !urlBuf.safePrintf("http://10.5.0.10/outlet.cgi?outlet=1&"
				  "command=1&time=%"UINT32"",
					(uint32_t)getTimeGlobal()) )
			return;
	}
	else {
		// this turns it off
		if ( !urlBuf.safePrintf("http://10.5.0.10/outlet.cgi?outlet=1&"
				  "command=0&time=%"UINT32"",
					(uint32_t)getTimeGlobal()) )
			return;
	}

	// . make a cookie with the login info
	// . on chrome open the console and click "Network" tab 
	//   to view the http network requests and replies
	//char *cookie = "admin=12345678; Taifatech=yes";
	char *cookie = NULL;
	

	// mark the request as outstanding so we do not overlap it
	m_fanReqOut = true;

	log("process: trying to set fan state to %"INT32"",m_desiredFanState);

	// get it
	bool status = g_httpServer.
		getDoc ( urlBuf.getBufStart() , // url to download
			 0               , // ip
			 0               , // offset
			 -1              , // size
			 0               , // ifModifiedSince
			 NULL            , // state
			 gotFanReplyWrapper , // callback
			 30*1000         , // timeout
			 0               , // proxy ip
			 0               , // proxy port
			 1*1024*1024     , // maxLen
			 1*1024*1024     , // maxOtherLen
			 "Mozilla/4.0 "
			 "(compatible; MSIE 6.0; Windows 98; "
			 "Win 9x 4.90)"  ,
			 //false           , // respect download limit?
			 "HTTP/1.1"      ,// fake 1.1 otherwise we get error!
			 true , // doPost? converts cgi str to post
			 cookie ,
			 // additional mime headers
			 "Authorization: Basic YWRtaW46^C");
	// wait for it
	if ( ! status ) return;
	// i guess it is back!
	m_fanReqOut = false;
	// call this to wrap things up
	gotFanReply ( NULL );
}


// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Process::gotFanReply ( TcpSocket *s ) {

	// i guess it is back!
	m_fanReqOut = false;

	if ( ! s ) {
		log("powermo: got NULL socket in fan reply");
		return true;
	}

	if ( g_errno ) {
		log("powermo: had error getting fan state: %s.",
		    mstrerror(g_errno));
		return true;
	}
	// point into buffer
	char *buf     = s->m_readBuf;
	int32_t  bufSize = s->m_readOffset;

	if ( ! buf ) {
		log(LOG_INFO,"powermo: got empty fan state reply.");
		return true;
	}

	HttpMime mime;
	mime.set ( buf , bufSize , NULL );
	char *content    = buf     + mime.getMimeLen();
	int32_t  contentLen = bufSize - mime.getMimeLen();
	content[contentLen]='\0';

	// get the state of the power! (from old power switch)
	//char *p = strstr ( content ,"\"power\",status:" );

	// get the state of the power! (from new power switch)
	char *tag = "<outlet1_status>";
	int32_t tagLen = gbstrlen(tag);
	char *p = strstr ( content, tag );
	// panic?
	if ( ! p ) {
		log("powermo: could not parse out fan power state "
		    "from power strip. "
		    "content = %s",content);
		return true;
	}
 
	// . get the value
	// . val is 0 if the fan power off, 1 if on?
	int32_t val = atoi ( p + tagLen );

	m_currentFanState = val;

	if ( m_currentFanState == m_desiredFanState ) 
		log("powermo: desired fan state, %"INT32", achieved",
		    m_currentFanState);
	else
		log("powermo: fan state is %"INT32", but needs to be %"INT32"",
		    m_currentFanState, 
		    m_desiredFanState);

	return true;
}
	



// make sure ntpd is running, we can't afford to get our clock
// out of sync for credit card transactions
bool Process::checkNTPD ( ) {

	if ( ! g_conf.m_isLive ) return true;

	FILE *pd = popen("ps auxww | grep ntpd | grep -v grep","r");
	if ( ! pd ) {
		log("gb: failed to ps auxww ntpd");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	char tmp[1024];
	char *ss = fgets ( tmp , 1000 , pd );
	if ( ! ss ) {
		log("gb: failed to ps auxww ntpd 2");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	// must be there
	if ( ! strstr ( tmp,"ntpd") ) {
		log("gb: all proxies must have ntpd running! this "
		    "one does not!");
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return false;
	}
	return true;
}

