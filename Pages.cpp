#include "gb-include.h"

#include "Pages.h"
#include "Parms.h"
#include "Collectiondb.h"
#include "Tagdb.h"
#include "Proxy.h"
#include "PageParser.h" // g_inPageParser
#include "Rebalance.h"
#include "Profiler.h"
#include "PageRoot.h"
#include "Process.h"
#include "PingServer.h"
#include "ip.h"
#include "Conf.h"
#include "GbUtil.h"


// a global class extern'd in Pages.h
Pages g_pages;

// error message thingy used by HttpServer.cpp for logging purposes
const char *g_msg;

// . list of all dynamic pages, their path names, permissions and callback
//   functions that generate that page
// . IMPORTANT: these must be in the same order as the PAGE_* enum in Pages.h
//   otherwise you'll get a malformed error when running
static WebPage s_pages[] = {
	// publicly accessible pages
	{ PAGE_ROOT      , "index.html"    , 0 , "root" , 0 , 0 ,
	  "search page to query",
	  sendPageRoot   , 0 ,NULL,NULL,
	  PG_NOAPI|PG_ACTIVE},

	{ PAGE_RESULTS   , "search"        , 0 , "search" , 0 , 0 ,
	  "search results page",
	  sendPageResults, 0 ,NULL,NULL,
	  PG_ACTIVE},

	// this is the public addurl, /addurl, if you are using the 
	// api use PAGE_ADDURL2 which is /admin/addurl. so we set PG_NOAPI here
	{ PAGE_ADDURL    , "addurl"       , 0 , "add url" , 0 , 0 ,
	  "Page where you can add url for spidering",
	  sendPageAddUrl, 0 ,NULL,NULL,
	  PG_NOAPI|PG_ACTIVE},

	{ PAGE_GET       , "get"           , 0 , "get" ,  0 , 0 ,
	  "gets cached web page",
	  sendPageGet  , 0 ,NULL,NULL,
	  PG_ACTIVE},

	{ PAGE_LOGIN     , "login"         , 0 , "login" ,  0 , 0 ,
	 "login",
	 sendPageLogin, 0 ,NULL,NULL,
	  PG_NOAPI|PG_ACTIVE},

	// use post now for the "site list" which can be big
	{ PAGE_BASIC_SETTINGS, "admin/settings", 0 , "settings",1, M_POST , 
	  "basic settings", sendPageGeneric , 0 ,NULL,NULL,
	  PG_NOAPI|PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_BASIC_STATUS, "admin/status", 0 , "status",1, 0 , 
	  "basic status", sendPageBasicStatus  , 0 ,NULL,NULL,
	  PG_STATUS|PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_COLLPASSWORDS,
	  "admin/collectionpasswords", 0,"collection passwords",0,0,
	  "passwords", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_BASIC_SEARCH, "", 0 , "search",1, 0 , 
	  "basic search", sendPageRoot  , 0 ,NULL,NULL,
	  PG_NOAPI|PG_ACTIVE},

	{ PAGE_HOSTS     , "admin/hosts"   , 0 , "Hosts" ,  0 , 0 ,
	  "hosts status", sendPageHosts    , 0 ,NULL,NULL,
	  PG_STATUS|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_MASTER    , "admin/master"  , 0 , "Master controls" ,  1 , 0 ,
	  "master controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_RDB   , "admin/rdb"   , 0 , "Rdb controls" ,1, 0,
	  "rdb controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_ACTIVE},

	// use POST for html head/tail and page root html. might be large.
	{ PAGE_SEARCH    , "admin/search"   , 0 , "Search controls" ,1,M_POST,
	  "search controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_ACTIVE},

	{ PAGE_RANKING   , "admin/ranking"   , 0 , "Ranking controls" ,1, 0,
	  "ranking controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_ACTIVE},

	// use post now for the "site list" which can be big
	{ PAGE_SPIDER    , "admin/spider"   , 0 , "Spider controls" ,1,M_POST,
	  "spider controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_SPIDERPROXIES,"admin/proxies"   , 0 , "Proxies" ,  1 , 0,
	  "proxies", sendPageGeneric  , 0,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE } ,

	{ PAGE_LOG       , "admin/log"     , 0 , "Log controls"     ,  1 , 0 ,
	  "log controls", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_COLLPASSWORDS2,//BASIC_SECURITY, 
	  "admin/collectionpasswords2", 0,"collection passwords",0,0,
	  "passwords", sendPageGeneric  , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_NOAPI|PG_ACTIVE},


	{ PAGE_MASTERPASSWORDS, "admin/masterpasswords", 
	  0 , "master passwords" ,  1 , 0 ,
	  "master passwords", 
	  sendPageGeneric , 0 ,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE},

#ifndef PRIVACORE_SAFE_VERSION
	{ PAGE_ADDCOLL   , "admin/addcoll" , 0 , "add collection"  ,  1 , 0 ,
	  "add a new collection",
	  sendPageAddColl  , 0 ,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_DELCOLL   , "admin/delcoll" , 0 , "delete collections" ,  1 ,0,
	  "delete a collection",
	  sendPageDelColl  , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_CLONECOLL, "admin/clonecoll" , 0 , "clone collection" ,  1 ,0,
	  "clone one collection's settings to another",
	  sendPageCloneColl  , 0 ,NULL,NULL,
	  PG_MASTERADMIN|PG_ACTIVE},
#endif
	// let's replace this with query reindex for the most part
	{ PAGE_REPAIR    , "admin/rebuild"   , 0 , "Rebuild" ,  1 , 0 ,
	  "rebuild data",
	  sendPageGeneric , 0 ,NULL,NULL,
	  PG_MASTERADMIN |PG_ACTIVE},

	{ PAGE_FILTERS   , "admin/filters", 0 , "Url filters" ,  1 ,M_POST,
	  "prioritize urls for spidering",
	  sendPageGeneric  , 0 ,NULL,NULL,
	  PG_NOAPI|PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_INJECT    , "admin/inject"   , 0 , "Inject url" , 0,M_MULTI ,
	  "inject url in the index here",
	  sendPageInject   , 2 ,NULL,NULL,
	  PG_ACTIVE} ,

	// this is the addurl page the the admin!
	{ PAGE_ADDURL2   , "admin/addurl"   , 0 , "Add urls" ,  0 , 0 ,
	  "add url page for admin",
	  sendPageAddUrl2   , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_REINDEX   , "admin/reindex"  , 0 , "Query reindex" ,  0 , 0 ,
	  "query delete/reindex",
	  sendPageReindex  , 0 ,NULL,NULL,
	  PG_COLLADMIN|PG_ACTIVE},

	// master admin pages
	{ PAGE_STATS     , "admin/stats"   , 0 , "Stats" ,  0 , 0 ,
	  "general statistics",
	  sendPageStats    , 0 ,NULL,NULL,
	  PG_STATUS|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_GRAPH , "admin/graph"  , 0 , "Graph"  ,  0 , 0 ,
	  "query stats graph",
	  sendPageGraph  , 2  ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_PERF      , "admin/perf"    , 0 , "Performance"     ,  0 , 0 ,
	  "function performance graph",
	  sendPagePerf     , 0 ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_SOCKETS   , "admin/sockets" , 0 , "Sockets" ,  0 , 0 ,
	  "sockets",
	  sendPageSockets  , 0 ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_LOGVIEW    , "admin/logview"   , 0 , "Log view" ,  0 , 0 ,
	  "logview",
	  sendPageLogView  , 0 ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	// deactivate until works on 64-bit... mdw 12/14/14
	{ PAGE_PROFILER    , "admin/profiler"   , 0 , "Profiler" ,  0 ,M_POST,
	  "profiler",
	  sendPageProfiler   , 0 ,NULL,NULL,
	  PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_THREADS    , "admin/threads"   , 0 , "Threads" ,  0 , 0 ,
	  "threads",
	  sendPageThreads  , 0 ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_IMPORT , "admin/import"         , 0 , "import" , 0 , 0 ,
	  "import documents from another cluster", 
	  sendPageGeneric , 0 ,NULL,NULL,
	  PG_NOAPI|PG_MASTERADMIN},

	{ PAGE_API , "admin/api"         , 0 , "api" , 0 , 0 ,
	  "api",  
	  sendPageAPI , 0 ,NULL,NULL,
	  PG_NOAPI|PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_TITLEDB   , "admin/titledb" , 0 , "titledb"         ,  0 , 0,
	  "titledb",
	  sendPageTitledb  , 2,NULL,NULL,
	  PG_NOAPI|PG_MASTERADMIN},
	// 1 = usePost

	{ PAGE_SPIDERDB  , "admin/spiderdb" , 0 , "Spider queue" ,  0 , 0 ,
	  "spider queue",
	  sendPageSpiderdb , 0 ,NULL,NULL,
	  PG_STATUS|PG_NOAPI|PG_MASTERADMIN|PG_ACTIVE},

	{ PAGE_SEARCHBOX , "admin/searchbox", 0 , "search" ,  0 , 0 ,
	  "search box",
	  sendPageResults  , 0 ,NULL,NULL,
	  PG_NOAPI},

	{ PAGE_PARSER    , "admin/parser"  , 0 , "parser"          , 0,M_POST,
	  "page parser",
	  sendPageParser   , 2 ,NULL,NULL,
	  PG_NOAPI|PG_COLLADMIN|PG_ACTIVE},

	{ PAGE_SITEDB    , "admin/tagdb"  , 0 , "tagdb"  ,  0 , M_POST,
	  "add/remove/get tags for sites/urls",
	  sendPageTagdb ,  0 ,NULL,NULL,
	  PG_NOAPI|PG_COLLADMIN|PG_ACTIVE},	  

	{ PAGE_HEALTHCHECK, "health-check"   , 0 , "healthcheck" ,  0 , 0 ,
	  "health check",
	  sendPageHealthCheck  , 0 ,NULL,NULL,
	  PG_NOAPI|PG_ACTIVE},

};
static const int32_t s_numPages = sizeof(s_pages) / sizeof(WebPage);

const WebPage *Pages::getPage ( int32_t page ) {
	if ( page < PAGE_ROOT || page >= PAGE_NONE ) {
		return NULL;
	}

	return &s_pages[page];
}

const char *Pages::getPath ( int32_t page ) { 
	return s_pages[page].m_filename; 
}

void Pages::init ( ) {
	// sanity check, ensure PAGE_* corresponds to position
	for ( int32_t i = 0 ; i < s_numPages ; i++ ) 
		if ( s_pages[i].m_pageNum != i ) {
			log(LOG_LOGIC,"conf: Bad engineer. WebPage array is "
			    "malformed. It must be 1-1 with the "
			    "WebPage enum in Pages.h.");
			g_process.shutdownAbort(true);
			//exit ( -1 );
		}
	// set the m_flen member
	for ( int32_t i = 0 ; i < s_numPages ; i++ ) 
		s_pages[i].m_flen = strlen ( s_pages[i].m_filename );
}

// return the PAGE_* number thingy
int32_t Pages::getDynamicPageNumber ( HttpRequest *r ) {
	const char *path    = r->getFilename();
	int32_t  pathLen = r->getFilenameLen();
	if ( pathLen > 0 && path[0]=='/' ) {
		path++;
		pathLen--;
	}

	// historical backwards compatibility fix
	if ( pathLen == 9 && strncmp ( path , "cgi/0.cgi" , 9 ) == 0 ) {
		path = "search";
		pathLen = strlen(path);
	}
	if ( pathLen == 9 && strncmp ( path , "cgi/1.cgi" , 9 ) == 0 ) {
		path = "addurl";
		pathLen = strlen(path);
	}
	if ( pathLen == 6 && strncmp ( path , "inject" , 6 ) == 0 ) {
		path = "admin/inject";
		pathLen = strlen(path);
	}
	if ( pathLen == 9 && strncmp ( path , "index.php" , 9 ) == 0 ) {
		path = "search";
		pathLen = strlen(path);
	}
	if ( pathLen == 10 && strncmp ( path , "search.csv" , 10 ) == 0 ) {
		path = "search";
		pathLen = strlen(path);
	}

	// go down the list comparing the pathname to dynamic page names
	for ( int32_t i = 0 ; i < s_numPages ; i++ ) {
		if ( pathLen != s_pages[i].m_flen ) {
			continue;
		}

		if ( strncmp ( path , s_pages[i].m_filename , pathLen ) == 0 ) {
			return i;
		}
	}

	// not found in our list of dynamic page filenames
	return -1;
}

// once all hosts have received the parms, or we've at least tried to send
// them to all hosts, then come here to return the page content back to
// the client browser
static void doneBroadcastingParms(void *state) {
	TcpSocket *sock = (TcpSocket *)state;
	// free this mem
	sock->m_handyBuf.purge();
	// set another http request again
	HttpRequest r;
	//bool status = r.set ( sock->m_readBuf , sock->m_readOffset , sock ) ;
	r.set ( sock->m_readBuf , sock->m_readOffset , sock ) ;
	// we stored the page # below
	WebPage *pg = &s_pages[sock->m_pageNum];
	// call the page specifc function which will send data back on socket
	pg->m_function ( sock , &r );
}

// . returns false if blocked, true otherwise
// . send an error page on error
bool Pages::sendDynamicReply ( TcpSocket *s , HttpRequest *r , int32_t page ) {
	// error out if page number out of range
	if ( page < PAGE_ROOT || page >= s_numPages ) 
		return g_httpServer.sendErrorReply ( s , 505 , "Bad Request");

	// does public have permission?
	bool publicPage = false;
	if ( page == PAGE_ROOT ) publicPage = true;
	// do not deny /NM/Albuquerque urls
	if ( page == PAGE_RESULTS ) publicPage = true;
	if ( page == PAGE_ADDURL ) publicPage = true;
	if ( page == PAGE_GET ) publicPage = true;
	if ( page == PAGE_HEALTHCHECK ) publicPage = true;

	// now use this...
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );


	////////////////////
	////////////////////
	//
	// if it is an administrative page it requires permission!
	//
	////////////////////
	////////////////////

	g_errno = 0;

	WebPage *pg = &s_pages[page];

	// for pages like autoban that no longer show in the menu,
	// just return error right away to avoid having to deal with
	// permissions issues/bugs
	if ( ! publicPage && 
	     ! isMasterAdmin && 
	     ! (pg->m_pgflags & PG_ACTIVE) ) {
		return g_httpServer.sendErrorReply ( s , 505 , "Page not active");
	}

	if ( ! g_conf.m_allowCloudUsers &&
	     ! publicPage &&
	     ! isMasterAdmin &&
	     ! g_conf.isCollAdmin ( s , r ) ) {
		return sendPageLogin ( s , r );
	}

	// get safebuf stored in TcpSocket class
	SafeBuf *parmList = &s->m_handyBuf;

	parmList->reset();

	// chuck this in there
	s->m_pageNum = page;

	////////
	//
	// the new way to set and distribute parm settings
	//
	////////
	
	// . convert http request to list of parmdb records
	// . will only add parm recs we have permission to modify!!!
	// . if no collection supplied will just return true with no g_errno
	if ( //isMasterAdmin &&
	     ! g_parms.convertHttpRequestToParmList ( r, parmList, page, s))
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));
		
	// . add parmList using Parms::m_msg4 to all hosts!
	// . returns true and sets g_errno on error
	// . returns false if would block
	// . just returns true if parmList is empty
	// . so then doneBroadcastingParms() is called when all hosts
	//   have received the updated parms, unless a host is dead,
	//   in which case he should sync up when he comes back up
	if ( //isCollAdmin &&
	     ! g_parms.broadcastParmList ( parmList , 
					   s , // state is socket i guess
					   doneBroadcastingParms ) )
		// this would block, so return false
		return false;

	// free the mem if we didn't block
	s->m_handyBuf.purge();

	// on error from broadcast, bail here
	if ( g_errno )
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));

	// if this is a save & exit request we must log it here because it
	// will never return in order to log it in HttpServer.cpp
	// TODO: make this a function we can call.
	if ( g_conf.m_logHttpRequests && page == PAGE_MASTER ) { 
		//&& pg->m_function==CommandSaveAndExit ) {
		// get time format: 7/23/1971 10:45:32
		time_t tt ;//= getTimeGlobal();
		if ( isClockInSync() ) tt = getTimeGlobal();
		else                   tt = getTimeLocal();
		struct tm tm_buf;
		struct tm *timeStruct = localtime_r(&tt,&tm_buf);
		char buf[100];
		strftime ( buf , 100 , "%b %d %T", timeStruct);
		// what url refered user to this one?
		char *ref = r->getReferer();
		// skip over http:// in the referer
		if ( strncasecmp ( ref , "http://" , 7 ) == 0 ) ref += 7;
		// save ip in case "s" gets destroyed
		int32_t ip = s->m_ip;
		logf (LOG_INFO,"http: %s %s %s %s %s",
		      buf,iptoa(ip),r->getRequest(),ref,
		      r->getUserAgent());
	}

	// if we did not block... maybe there were no parms to broadcast
	return pg->m_function ( s , r );
}

// certain pages are automatically generated by the g_parms class
// because they are menus of configurable parameters for either g_conf
// or for a particular CollectionRec record for a collection.
bool sendPageGeneric ( TcpSocket *s , HttpRequest *r ) {
	//int32_t page = g_pages.getDynamicPageNumber ( r );
	return g_parms.sendPageGeneric ( s , r );//, page );
}

bool Pages::getNiceness ( int32_t page ) {
	// error out if page number out of range
	if ( page < 0 || page >= s_numPages ) 
		return 0;
	return s_pages[page].m_niceness;
}

///////////////////////////////////////////////////////////
//
// Convenient html printing routines
//
//////////////////////////////////////////////////////////

static bool printTopNavButton ( const char *text,
			        const char *link,
			        bool isHighlighted,
			        const char *coll,
			        SafeBuf *sb ) {

	if ( isHighlighted )
		sb->safePrintf(
			       "<a style=text-decoration:none; href=%s?c=%s>"
			       "<div "
			       "style=\""
			       "padding:6px;"
			       "display:inline;"
			       "margin-left:10px;"
			       "background-color:white;"
			       "border-top-left-radius:10px;"
			       "border-top-right-radius:10px;"
			       "border-width:3px;"
			       "border-style:solid;"
			       "border-color:blue;"
			       // fix msie this way:
			       "border-bottom-width:4px;"
			       "border-bottom-color:white;"
			       "\""
			       ">"
			       "<b>%s</b>"
			       "</div>"
			       "</a>"
			       , link
			       , coll
			       , text
			       );

	else
		sb->safePrintf(
			       "<a style=text-decoration:none; href=%s?c=%s>"
			       "<div "

			       " onmouseover=\""
			       "this.style.backgroundColor='lightblue';"
			       "this.style.color='black';\""
			       " onmouseout=\""
			       "this.style.backgroundColor='blue';"
			       "this.style.color='white';\""

			       "style=\""
			       "padding:6px;" // same as TABLE_STYLE
			       "display:inline;"
			       "margin-left:10px;"
			       "background-color:blue;"
			       "border-top-left-radius:10px;"
			       "border-top-right-radius:10px;"
			       "border-color:white;"
			       "border-width:3px;"
			       "border-bottom-width:0px;"
			       "border-style:solid;"
			       "overflow-y:hidden;"
			       "overflow-x:hidden;"
			       "line-height:23px;"
			       "color:white;"
			       "\""
			       ">"
			       "<b>%s</b>"
			       "</div>"
			       "</a>"
			       , link
			       , coll
			       , text
			       );
	return true;
}


bool Pages::printAdminTop (SafeBuf     *sb   ,
			   TcpSocket   *s    ,
			   HttpRequest *r    ,
			   const char  *qs   ,
			   const char* bodyJavascript) {
	int32_t  page   = getDynamicPageNumber ( r );
	
	if( page < 0 ) {
		// should never happen
		logError("invalid page number %" PRId32 "", page);
		return false;
	}
	
	const char *coll = g_collectiondb.getDefaultColl(r);
	bool status = true;

	sb->safePrintf("<html>\n");

	sb->safePrintf(
		     "<head>\n"
		     "<title>%s | gigablast admin</title>\n"
		     "<meta http-equiv=\"Content-Type\" "
		     "content=\"text/html;charset=utf8\" />\n"
		     "</head>\n",  s_pages[page].m_name);

	// print bg colors
	status &= printColors ( sb, bodyJavascript);

	// print form to encompass table now

	////////
	//
	// . the form
	//
	////////
	// . we cannot use the GET method if there is more than a few k of
	//   parameters, like in the case of the Search Controls page. The
	//   browser simply will not send the request if it is that big.
	if ( s_pages[page].m_usePost == M_MULTI )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				// we need this for <input type=file> tags
				"ENCTYPE=\"multipart/form-data\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else if ( s_pages[page].m_usePost == M_POST )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"get\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	// pass on this stuff
	sb->safePrintf ( "<input type=hidden name=c value=\"%s\">\n",coll);

	//
	// DIVIDE INTO TWO PANES, LEFT COLUMN and MAIN COLUMN
	//
	sb->safePrintf("<TABLE border=0 height=100%% cellpadding=0 "
		       "width=100%% "
		       "cellspacing=0>"
		      "\n<TR>\n");

	//
	// first the nav column
	//
	sb->safePrintf("<TD bgcolor=#%s "//f3c714 " // yellow/gold
		      "valign=top "
		      "style=\""
		      "width:210px;"
		       "max-width:210px;"
		      "border-right:3px solid blue;"
		      "\">"

		      "<br style=line-height:14px;>"

		      "<center>"
		      "<a href=/?c=%s>"
		      "<div style=\""
		      "background-color:white;"
		      "padding:10px;"
		      "border-radius:100px;"
		      "border-color:blue;"
		      "border-width:3px;"
		      "border-style:solid;"
		      "width:100px;"
		      "height:100px;"
		      "\">"
		      "<br style=line-height:10px;>"
		      "<img width=54 height=79 alt=HOME border=0 "
		       "src=/rocket.jpg>"
		      "</div>"
		      "</a>"
		      "</center>"

		      "<br>"
		      "<br>"
		       , GOLD
		       ,coll
		      );

        bool isBasic = false;
	if ( page == PAGE_BASIC_SETTINGS ) isBasic = true;
	if ( page == PAGE_BASIC_STATUS ) isBasic = true;
	if ( page == PAGE_COLLPASSWORDS ) isBasic = true;
	if ( page == PAGE_BASIC_SEARCH ) isBasic = true;


	// collections box
	sb->safePrintf(
		       "<div "
		       "style=\""

		       "width:190px;"

		       "padding:4px;" // same as TABLE_STYLE
		       "margin-left:10px;"
		       "background-color:white;"
		       "border-top-left-radius:10px;"
		       "border-bottom-left-radius:10px;"
		       "border-color:blue;"
		       "border-width:3px;"
		       "border-style:solid;"
		       "margin-right:-3px;"
		       "border-right-color:white;"
		       "overflow-y:auto;"
		       "overflow-x:hidden;"
		       "line-height:23px;"
		       "color:black;"
		       "\""
		       ">"
		       );

	// collection navbar
	status&=printCollectionNavBar ( sb, page , coll, qs,s,r);

	// count the statuses
	int32_t emptyCount = 0;
	int32_t doneCount = 0;
	int32_t activeCount = 0;
	int32_t pauseCount = 0;
	int32_t betweenRoundsCount = 0;
	uint32_t nowGlobal = (uint32_t)getTimeGlobal();
	for (int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cc = g_collectiondb.m_recs[i];
		if ( ! cc ) continue;
		CrawlInfo *ci = &cc->m_globalCrawlInfo;
		if      ( cc->m_spideringEnabled &&
			  nowGlobal < cc->m_spiderRoundStartTime )
			betweenRoundsCount++;
		else if ( cc->m_spideringEnabled && 
			  ! ci->m_hasUrlsReadyToSpider &&
			  ci->m_urlsHarvested )
			emptyCount++;
		else if ( ! ci->m_hasUrlsReadyToSpider )
			doneCount++;
		else if (!cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider)
			pauseCount++;
		else if (cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider )
			activeCount++;
	}


	sb->safePrintf("</div>");

	sb->safePrintf("<div style=padding-left:10px;>"
		       "<br>"
		       "<b>Key</b>"
		       "<br>"
		       "<br>"
		       "\n"
		       );
	sb->safePrintf(
		       "<font color=black>"
		       "&#x25cf;</font> spider is done (%" PRId32")"
		       "<br>"
		       "\n"

		       "<font color=orange>"
		       "&#x25cf;</font> spider is paused (%" PRId32")"
		       "<br>"
		       "\n"

		       "<font color=green>"
		       "&#x25cf;</font> spider is active (%" PRId32")"
		       "<br>"
		       "\n"

		       "<font color=gray>"
		       "&#x25cf;</font> spider queue empty (%" PRId32")"
		       "<br>"
		       "\n"

		       "<font color=blue>"
		       "&#x25cf;</font> between rounds (%" PRId32")"
		       "<br>"
		       "\n"


		       "</div>"

		       ,doneCount
		       ,pauseCount
		       ,activeCount
		       ,emptyCount
		       ,betweenRoundsCount

		       );


	sb->safePrintf("</TD>");


	//
	// begin the 2nd column of the display
	//

	// the controls will go here
	sb->safePrintf("<TD valign=top>"
		       "<div style=\"padding-left:20px;"
		       "margin-left:-3px;"
		       "border-color:#%s;"
		       "border-width:3px;"
		       // make this from 3px to 4px for msie
		       "border-left-width:4px;"
		       "border-top-width:0px;"
		       "border-right-width:0px;"
		       "border-bottom-color:blue;"
		       "border-top-width:0px;"
		       "border-style:solid;"
		       "padding:4px;"
		       "background-color:#%s;\" "
		       "id=prepane>"
		       , GOLD
		       , GOLD
		       );

	// logout link on far right
	sb->safePrintf("<div align=right "
		       "style=\""
		       "max-width:100px;"
		       "right:20px;"
		       "position:absolute;"
		       "\">"
		       "<font color=blue>"
		       // clear the cookie
		       "<span "

		       "style=\"cursor:hand;"
		       "cursor:pointer;\" "

		       "onclick=\"document.cookie='pwd=;';"
		       "window.location.href='/';"
		       "\">"
		       "logout"
		       "</span>"
		       "</font>"
		       "</div>"
		       );

	// print the hosts navigation bar
	status &= printHostLinks ( sb, page , coll, s->m_ip, qs );

	sb->safePrintf("<br><br>");

	SafeBuf mb;
	bool added = printRedBox ( &mb , s , r );

	// print emergency msg box
	if ( added )
		sb->safePrintf("%s",mb.getBufStart());

	// print Basic | Advanced links
	printTopNavButton("BASIC",
			  "/admin/settings",
			  isBasic, // highlighted?
			  coll,
			  sb );

	printTopNavButton("ADVANCED",
			  "/admin/master",
			  !isBasic, // highlighted?
			  coll,
			  sb );



	sb->safePrintf("<br>");

	// end that yellow/gold div
	sb->safePrintf("</div>");

	// this div will hold the submenu and forms
	sb->safePrintf(
		       "<div style=padding-left:20px;"
		       "padding-right:20px;"
		       "margin-left:0px;"
		       "background-color:white;"
		       "id=panel2>"
		       
		       "<br>"
		       );

	// print the menu links under that
	status &= printAdminLinks ( sb, page , coll , isBasic );


	sb->safePrintf("<br>");


	if ( page != PAGE_BASIC_SETTINGS )
		return true;

	
	// gigabot helper blurb
	printGigabotAdvice ( sb , page , r , NULL );

	return true;
}

bool printGigabotAdvice(SafeBuf *sb,
			int32_t page,
			const HttpRequest *hr,
			const char *errMsg) {

	char format = hr->getFormat();
	if ( format != FORMAT_HTML ) return true;

	char guide = hr->getLong("guide",0);
	if ( ! guide ) return true;

	sb->safePrintf("<input type=hidden name=guide value=1>\n");

	// gradient class
	// yellow box
	const char *box =
		"<table cellpadding=5 "
		// full width of enclosing div
		"width=100%% "
		"style=\""
		"background-color:lightblue;"
		"border:3px blue solid;"
		"border-radius:8px;"
		"\" "
		"border=0"
		">"
		"<tr><td>";
	const char *boxEnd =
		"</td></tr></table>";

	const char *advice = NULL;
#ifndef PRIVACORE_SAFE_VERSION
	if ( page == PAGE_ADDCOLL )
		advice =
			"STEP 1 of 3. "
			"<br>"
			"<br>"
			"Enter the name of your collection "
			"(search engine) in the box below then hit "
			"submit. You can only use alphanumeric characters, "
			"hyphens or underscores."
			"<br>"
			"<br>"
			"Remember this name so you can access the controls "
			"later."
			;
#endif
	if ( page == PAGE_BASIC_SETTINGS )
		advice = 
			"STEP 2 of 3. "
			"<br>"
			"<br>"
			"Enter the list of websites you want to be in your "
			"search engine into the box marked <i>site list</i> "
			"then click the <i>submit</i> button."
			// "<br>"
			// "<br>"
			// "Do not deviate from this path, or, as is always "
			// "the case, you may "
			// "be blasted."
			;
	if ( page == PAGE_BASIC_STATUS )
		advice = 
			"STEP 3 of 3. "
			"<br>"
			"<br>"
			"Ensure you see search results appearing in "
			"the box below. If not, then you have spider "
			"problems."
			"<br>"
			"<br>"
			"Click on the links in the lower right to expose "
			"the source code. Copy and paste this code "
			"into your website to make a search box that "
			"connects to the search engine you have created. "
			;

	if ( ! advice ) return true;

	sb->safePrintf("<div style=max-width:490px;"
		       "padding-right:10px;>");

	sb->safePrintf("%s",box);

	// the mean looking robot
	sb->safePrintf("<img style=float:left;padding-right:15px; "
		       "height=141px width=75px src=/robot3.png>"
		       "</td><td>"
		       "<b>"
		       );

	if ( errMsg )
		sb->safePrintf("%s",errMsg);
	
	sb->safePrintf("%s"
		       "</b>"
		       , advice
		       );
	sb->safePrintf("%s",boxEnd);
	sb->safePrintf("<br><br></div>");
	return true;
}

void Pages::printFormTop( SafeBuf *sb, HttpRequest *r ) {
	int32_t  page   = getDynamicPageNumber ( r );

	if( page < 0 ) {
		logError("getDynamicPageNumber returned negative index!");
		return;
	}

	// . the form
	// . we cannot use the GET method if there is more than a few k of
	//   parameters, like in the case of the Search Controls page. The
	//   browser simply will not send the request if it is that big.
	if ( s_pages[page].m_usePost )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"get\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
}

void Pages::printFormData( SafeBuf *sb, TcpSocket *s, HttpRequest *r ) {

	int32_t  page   = getDynamicPageNumber ( r );
	const char *coll   = r->getString ( "c"   );
	if ( ! coll ) coll = "";
	sb->safePrintf ( "<input type=\"hidden\" name=\"c\" "
			 "value=\"%s\" />\n", coll);

	// should any changes be broadcasted to all hosts?
	sb->safePrintf ("<input type=\"hidden\" name=\"cast\" value=\"%" PRId32"\" "
			"/>\n",
			page >= 0 ? (int32_t)s_pages[page].m_cast : 0);

}

bool Pages::printAdminBottom ( SafeBuf *sb, HttpRequest *r ) {
	return printAdminBottom ( sb );
}

bool Pages::printSubmit ( SafeBuf *sb ) {
	// update button
	return sb->safePrintf ( 
			       //"<br>"
				"<center>"
				"<input type=submit name=action value=submit>"
				"</center>"
				"<br>"
				"\n" ) ;
}

bool Pages::printAdminBottom ( SafeBuf *sb ) {
	bool status = true;
	// update button
	if ( !sb->safePrintf ( "<center>"
			       "<input type=submit name=action value=submit>"
			       "</center>"
			       "<br>\n" ) )
		status = false;
	if ( ! sb->safePrintf(
			      "</div>" // id=pane2
			      "</TD>"
			      "</TR>"
			      "</TABLE>\n"
			      "</form>"
			      //"</DIV>\n"
			      ) )
		status = false;
	// end form
	if ( ! sb->safePrintf ( "</body>\n</html>\n" ) )
		status = false;
	return status;
}

bool Pages::printAdminBottom2 ( SafeBuf *sb ) {
	bool status = true;
	sb->safePrintf ( "</div>\n</body>\n</html>\n" );
	return status;
}

bool Pages::printTail ( SafeBuf* sb, bool isLocal ) {
	// now print the tail
	sb->safePrintf (
		  "\n<center><b>"
		  "<p class=nav>");

	if ( g_conf.m_addUrlEnabled ) {
		sb->safePrintf("<a href=\"/addurl\">"
			"Add a Url</a> &nbsp; &nbsp; ");
	}

	sb->safePrintf (
		  "<a href=\"/help.html\">Help</a> &nbsp; &nbsp;");

	if ( isLocal )
		sb->safePrintf ( "[<a href=\"/master\">Admin"
			  "</a>] &nbsp; &nbsp; " );

	sb->safePrintf ( "</p></b></center></body></html>" );
	// return length of bytes we stored
	return true ;
}

bool Pages::printColors ( SafeBuf *sb, const char* bodyJavascript ) {
	// print font and color stuff
	sb->safePrintf (
		  "<body text=#000000 bgcolor=#ffffff"
		  " link=#000000 vlink=#000000 alink=#000000 "
		  "style=margin:0px;padding:0px; "
		  "%s>\n" 
		  "<style>"
		  "body,td,p,.h{font-family:"
		  "arial,"
		  "helvetica-neue"
		  "; "
		  "font-size: 15px;} "
		  "</style>\n",
		  bodyJavascript);
	return true;
}

bool Pages::printLogo ( SafeBuf *sb, const char *coll ) {
	// print the logo in upper right corner
	if ( ! coll ) coll = "";
	sb->safePrintf (
		  "<a href=\"/?c=%s\">"
		  "<img width=\"200\" height=\"40\" border=\"0\" "
		  "alt=\"Gigablast\" src=\"/logo-small.png\" />"
		  "</a>\n",coll);
	return true;
}

bool Pages::printHostLinks ( SafeBuf* sb     ,
			     int32_t     page   ,
			     const char    *coll   ,
			     int32_t     fromIp ,
			     const char    *qs     ) {
	bool status = true;

	int32_t total = 0;
	// add in hosts
	total += g_hostdb.getNumHosts();
	// and proxies
	total += g_hostdb.m_numProxyHosts;	

	sb->safePrintf (  //"&nbsp; &nbsp; &nbsp; "
			  "<a style=text-decoration:none; href=/admin/hosts>"
			  "<b><u>hosts in cluster</u></b></a>: ");

	if ( ! qs   ) qs   = "";
	//if ( ! pwd  ) pwd  = "";
	if ( ! coll ) coll = "";

	// print the 64 hosts before and after us
	int32_t radius = 512;//64;
	int32_t hid = g_hostdb.m_hostId;
	int32_t a = hid - radius;
	int32_t b = hid + radius;
	int32_t diff ;
	if ( a < 0 ) { 
		diff = -1 * a; 
		a += diff; 
		b += diff; 
	}
	if ( b > g_hostdb.getNumHosts() ) {
		diff = b - g_hostdb.getNumHosts();
		a -= diff; if ( a < 0 ) a = 0;
	}
	for ( int32_t i = a ; i < b ; i++ ) {
		// skip if negative
		if ( i < 0 ) continue;
		if ( i >= g_hostdb.getNumHosts() ) continue;
		// get it
		Host *h = g_hostdb.getHost ( i );
		uint16_t port = h->getInternalHttpPort();
		// use the ip that is not dead, prefer eth0
		uint32_t ip = g_hostdb.getBestIp ( h );
		// convert our current page number to a path
		const char *path = s_pages[page].m_filename;
		// highlight itself
		const char *ft = "";
		const char *bt = "";
		if ( i == hid && ! g_proxy.isProxy() ) {
			ft = "<b><font color=red>";
			bt = "</font></b>";
		}
		// print the link to it
		sb->safePrintf("%s<a href=\"http://%s:%hu/%s?"
			       "c=%s%s\">"
			       "%" PRId32"</a>%s ",
			       ft,iptoa(ip),port,path,
			       coll,qs,i,bt);
	}		

	// print the proxies
	for ( int32_t i = 0; i < g_hostdb.m_numProxyHosts; i++ ) {
		const char *ft = "";
		const char *bt = "";
		if ( i == hid && g_proxy.isProxy() ) {
			ft = "<b><font color=red>";
			bt = "</font></b>";
		}
		Host *h = g_hostdb.getProxy( i );
		uint16_t port = h->getInternalHttpPort();
		// use the ip that is not dead, prefer eth0
		uint32_t ip = g_hostdb.getBestIp ( h );
		const char *path = s_pages[page].m_filename;
		sb->safePrintf("%s<a href=\"http://%s:%hu/%s?"
			       "c=%s%s\">"
			       "proxy%" PRId32"</a>%s ",
			       ft,iptoa(ip),port,path,
			       coll,qs,i,bt);
	}

	return status;
}


// . print the master     admin links if "user" is USER_MASTER 
// . print the collection admin links if "user" is USER_ADMIN
bool  Pages::printAdminLinks ( SafeBuf *sb,
			       int32_t  page ,
			       const char *coll ,
			       bool  isBasic ) {

	bool status = true;

	// soemtimes we do not want to be USER_MASTER for testing
	char buf [ 64 ];
	buf[0] = '\0';

	// unfortunately width:100% is percent of the virtual window, not the
	// visible window... so just try 1000px max
	sb->safePrintf("<div style=max-width:800px;>");

	for ( int32_t i = PAGE_BASIC_SETTINGS ; i < s_numPages ; i++ ) {
		// is this page basic?
		bool pageBasic = false;
		if ( i >= PAGE_BASIC_SETTINGS &&
		     i <= PAGE_BASIC_SEARCH )
			pageBasic = true;

		// print basic pages under the basic menu, advanced pages
		// under the advanced menu...
		if ( isBasic != pageBasic ) continue;

		// ignore these for now
		if ( i == PAGE_API ) continue;
		if ( i == PAGE_SEARCHBOX ) continue;
		if ( i == PAGE_TITLEDB ) continue;
		if ( i == PAGE_IMPORT ) continue;
		if ( i == PAGE_HEALTHCHECK ) continue;
		


		// move these links to the coll nav bar on the left
#ifndef PRIVACORE_SAFE_VERSION
		if ( i == PAGE_ADDCOLL ) continue;
		if ( i == PAGE_DELCOLL ) continue;
		if ( i == PAGE_CLONECOLL ) continue;
#endif

		// ignore collection passwords if 
		// g_conf.m_useCollectionPasswords is false
		if ( ! g_conf.m_useCollectionPasswords &&
		     (i == PAGE_COLLPASSWORDS||i == PAGE_COLLPASSWORDS2) )
			continue;

		// print it out
		if ( i == PAGE_LOGIN )
			sb->safePrintf(
				       //"<span style=\"white-space:nowrap\">"
				       "<a href=\"/%s?"
				       //"user=%s&pwd=%s&"
				       "c=%s%s\">%s</a>"
				       //"</span>"
				       " &nbsp; \n",s_pages[i].m_filename,
				       coll,
				       buf,s_pages[i].m_name);
		else if ( page == i )
			sb->safePrintf(
				       "<b>"
				       "<a style=text-decoration:none; "
				       "href=\"/%s?c=%s%s\">"
				       "<font color=red>"
				       "<nobr>"
				       "%s"
				       "</nobr>"
				       "</font>"
				       "</a>"
				       "</b>"
				       " &nbsp; "
				       "\n"
				       ,s_pages[i].m_filename
				       ,coll
				       ,buf
				       ,s_pages[i].m_name
				       );
		else
			sb->safePrintf(
				       "<b>"
				       "<a style=text-decoration:none; "
				       "href=\"/%s?c=%s%s\">"
				       "<nobr>"
				       "%s"
				       "</nobr>"
				       "</a>"
				       "</b>"
				       " &nbsp; \n"
				       ,s_pages[i].m_filename
				       ,coll
				       ,buf
				       ,s_pages[i].m_name);
	}
	
	sb->safePrintf("</div>");

	return status;
}

bool Pages::printCollectionNavBar ( SafeBuf *sb, int32_t page, const char *coll, const char *qs,
                                    TcpSocket *sock, HttpRequest *hr ) {
	bool status = true;

	if ( ! qs  ) qs  = "";

	// if not admin just print collection name
	if ( g_collectiondb.m_numRecsUsed == 0 ) {
		sb->safePrintf ( "<center>"
			  "<br/><b><font color=red>No collections found. "
			  "Click <i>add collection</i> to add one."
			  "</font></b><br/><br/></center>\n");
		return status;
	}
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	bool highlight = true;
	if ( collnum < (collnum_t)0) {
		highlight = false; collnum=g_collectiondb.getFirstCollnum(); }
	if ( collnum < (collnum_t)0) return status;
	
	int32_t a = collnum;
	int32_t counta = 1;
	while ( a > 0 && counta < 15 ) 
		if ( g_collectiondb.m_recs[--a] ) counta++;
	int32_t b = collnum + 1;
	int32_t countb = 0;
	while ( b < g_collectiondb.m_numRecs && countb < 16 )
		if ( g_collectiondb.m_recs[b++] ) countb++;

	const char *s = "s";
	if ( g_collectiondb.m_numRecsUsed == 1 ) s = "";

	bool isMasterAdmin = g_conf.isMasterAdmin ( sock , hr );


	if ( isMasterAdmin )
		sb->safePrintf ( "<center><nobr><b>%" PRId32" Collection%s</b></nobr>"
				 "</center>\n",
				 g_collectiondb.m_numRecsUsed , s );
	else
		sb->safePrintf ( "<center><nobr><b>Collections</b></nobr>"
				 "</center>\n");

#ifndef PRIVACORE_SAFE_VERSION
	sb->safePrintf( "<center>"
			"<nobr>"
			"<font size=-1>"
			"<a href=/admin/addcoll?c=%s>add</a> &nbsp; &nbsp; "
			"<a href=/admin/delcoll?c=%s>delete</a> &nbsp; &nbsp; "
			"<a href=/admin/clonecoll?c=%s>clone</a>"
			"</font>"
			"</nobr>"
			"</center>"
			, coll
			, coll
			, coll
			);
#else

	sb->safePrintf(	"<center>"
			"<font size=-1>"
			"Privacore production version. Unsafe features disabled."
			"</font>"
			"</center>"
			);
			


#endif

	const char *color = "red";

	// style for printing collection names
	sb->safePrintf("<style>.x{text-decoration:none;font-weight:bold;}"
		       ".e{background-color:#e0e0e0;}"
		       "</style>\n");

	int32_t showAll = hr->getLong("showall",0);

	int32_t row = 0;
	uint32_t nowGlobal = (uint32_t)getTimeGlobal();
	int32_t numPrinted = 0;
	bool printMsg = false;

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cc = g_collectiondb.m_recs[i];
		if ( ! cc ) continue;

		if ( numPrinted >= 20 && ! showAll ) {
			printMsg = true;
			break;
		}

		// count it
		numPrinted++;

		const char *cname = cc->m_coll;

		row++;

		// every other coll in a darker div
		if ( (row % 2) == 0 )
			sb->safePrintf("<div class=e>");

		sb->safePrintf("<nobr>");

		// print color bullet
		// green = active
		// yellow = paused
		// black = done
		// gray = empty
		// red = going but has > 50% errors in last 100 sample.
		//       like timeouts etc.

		CrawlInfo *ci = &cc->m_globalCrawlInfo;
		const char *bcolor = "";
		if ( ! cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider )
			bcolor = "orange";// yellow is too hard to see
		if (   cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider )
			bcolor = "green";
		if ( ! ci->m_hasUrlsReadyToSpider )
			bcolor = "black";
		// when we first add a url via addurl or inject it will
		// set hasUrlsReadyToSpider on all hosts to true i think
		// and Spider.cpp increments urlsharvested.
		if (   cc->m_spideringEnabled && 
		     ! ci->m_hasUrlsReadyToSpider &&
		       ci->m_urlsHarvested )
			bcolor = "gray";

		if ( cc->m_spideringEnabled &&
		     nowGlobal < cc->m_spiderRoundStartTime )
			bcolor = "blue";


		sb->safePrintf("<font color=%s>&#x25cf;</font> ",bcolor);

		if ( i != collnum || ! highlight )// || ! coll || ! coll[0])
			sb->safePrintf ( "<a title=\"%s\" "
					 "class=x "
					 "href=\"/%s?c=%s%s\">%s"
				  "</a> &nbsp;",
					 cname,
					 s_pages[page].m_filename,
					 cname ,
					 qs, cname );
		else
			sb->safePrintf ( "<b><font title=\"%s\" "
					 "color=%s>%s</font></b> "
					 "&nbsp; ",  
					 cname, color , cname );
		sb->safePrintf("</nobr>");

		// every other coll in a darker div
		if ( (row % 2) == 0 )
			sb->safePrintf("</div>\n");
		else
			sb->safePrintf("<br>\n");

	}

	if ( showAll ) return status;

	// convert our current page number to a path
	if ( printMsg ) {
		const char *path = s_pages[page].m_filename;
		sb->safePrintf("<a href=\"/%s?c=%s&showall=1\">"
			       "...show all...</a><br>"
			       , path , coll );
	}


	//sb->safePrintf ( "</center><br/>" );

	return status;
}

// let's use a separate section for each "page"
// then have 3 tables, the input parms,
// the xml output table and the json output table
bool sendPageAPI ( TcpSocket *s , HttpRequest *r ) {
	char pbuf[32768];
	SafeBuf p(pbuf, 32768);

	CollectionRec *cr = g_collectiondb.getRec ( r , true );
	const char *coll = "";
	if ( cr ) coll = cr->m_coll;

	p.safePrintf("<html><head><title>Gigablast API</title></head><body>");


	// new stuff
	printFrontPageShell ( &p , "api" , cr , true );

	p.safePrintf("<br><br>\n");


	p.safePrintf("NOTE: All APIs support both GET and POST method. "
		     "If the size of your request is more than 2K you "
		     "should use POST.");
	p.safePrintf("<br><br>");

	p.safePrintf("NOTE: All APIs support both http and https "
		     "protocols.");

	p.safePrintf("<br><br>");

	p.safePrintf(
		     "<font size=+2><b>API by pages</b></font>"
		     "<ul>"
		     );

	for ( int32_t i = 0 ; i < s_numPages ; i++ ) {
		if ( s_pages[i].m_pgflags & PG_NOAPI ) continue;
		const char *pageStr = s_pages[i].m_filename;
		// unknown?
		if ( ! pageStr ) pageStr = "???";
		p.safePrintf("<li> <a href=#/%s>/%s</a>"
			     " - %s"
			     "</li>\n",
			     pageStr,
			     pageStr,
			     // description of page
			     s_pages[i].m_desc
			     );
	}

	p.safePrintf("</ul>");

	p.safePrintf("<hr>\n");

	bool printed = false;
	for ( int32_t i = 0 ; i < s_numPages ; i++ ) {
		if ( s_pages[i].m_pgflags & PG_NOAPI ) continue;
		if ( printed )
			p.safePrintf("<hr><br>\n");
		printApiForPage ( &p , i , cr );
		printed = true;
	}

	p.safePrintf("</table></center></body></html>");

	char* sbuf = p.getBufStart();
	int32_t sbufLen = p.length();

	bool retval = g_httpServer.sendDynamicPage(s,
						   sbuf,
						   sbufLen,
						   -1/*cachetime*/);
	return 	retval;
}


bool printApiForPage ( SafeBuf *sb , int32_t PAGENUM , CollectionRec *cr ) {

	if ( PAGENUM == PAGE_NONE ) return true;

	if ( ! cr ) {
		log("api: no collection provided");
		return true;
	}

	const char *pageStr = s_pages[PAGENUM].m_filename;
	
	// unknown?
	if ( ! pageStr ) pageStr = "???";

	sb->safePrintf("<a name=/%s>",pageStr);//PAGENUM);


	sb->safePrintf(
		       "<font size=+2><b><a href=/%s?c=%s>/%s</a></b></font>"
		       ,pageStr,cr->m_coll,pageStr);
	sb->safePrintf("</a>");

	sb->safePrintf("<font size=-0> - %s "
	               " &nbsp; "
	               "[ <b>show parms in</b> "
	               "<a href=/%s?showinput=1&format=xml>"
	               "xml</a> "
	               "or "
	               "<a href=/%s?showinput=1&format=json>"
	               "json</a> "
	               " ] "
	               "</font>",
	               s_pages[PAGENUM].m_desc,
	               pageStr,
	               pageStr
	               );

	// status pages. if its a status page with no input parms
	if ( s_pages[PAGENUM].m_pgflags & PG_STATUS )
		sb->safePrintf("<font size=-0>"
			       " &nbsp; "
			       "[ <b>show status in</b> "
			       "<a href=/%s?c=%s&format=xml>"
			       "xml</a> "
			       "or "
			       "<a href=/%s?format=json>"
			       "json</a> "
			       //"or <a href=/%s>html</a> ] "  
			       " ] "
			       "</font>",
			       pageStr,
			       cr->m_coll,
			       pageStr
			       //pageStr
			       );

	
	sb->safePrintf("<br>");
	sb->safePrintf(//"</div>"
		       "<br>");

	// and the start of the input parms table
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>Input</b>"

			"</td>"
			"</tr>"
			"<tr bgcolor=#%s>"
			"<td><b>#</b></td>"
			"<td><b>Parm</b></td>"
			"<td><b>Type</b></td>"
			"<td><b>Title</b></td>"
			"<td><b>Default Value</b></td>"
			"<td><b>Description</b></td></tr>\n"
			, TABLE_STYLE
			, DARK_BLUE );
	
	static const char * const blues[] = {DARK_BLUE,LIGHT_BLUE};
	int32_t count = 1;

	//
	// every page supports the:
	// 1) &format=xml|html|json 
	// 2) &showsettings=0|1
	// 3) &c=<collectionName>
	// parms. we support them in sendPageGeneric() for pages like
	// /admin/master /admin/search /admin/spider so you can see
	// the settings.
	// put these in Parms.cpp, but use PF_DISPLAY flag so we ignore them
	// in convertHttpRequestToParmList() and we do not show them on the
	// page itself.
	//

	// page display/output parms
	sb->safePrintf("<tr bgcolor=%s>"
		       "<td>%" PRId32"</td>\n"
		       "<td><b>format</b></td>"
		       "<td>STRING</td>"
		       "<td>output format</td>"
		       "<td>html</td>"
		       "<td>Display output in this format. Can be "
		       "<i>html</i>, <i>json</i> or <i>xml</i>.</td>"
		       "</tr>"
		       , blues[count%2]
		       , count
		       );
	count++;

	sb->safePrintf("<tr bgcolor=%s>"
		       "<td>%" PRId32"</td>\n"
		       "<td><b>showinput</b></td>"
		       "<td>BOOL (0 or 1)</td>"
		       "<td>show input and settings</td>"
		       "<td>1</td>"
		       "<td>Display possible input and the values of all "
		       "settings on "
		       "this page.</td>"
		       "</tr>"
		       , blues[count%2]
		       , count
		       );
	count++;


	for ( int32_t i = 0; i < g_parms.m_numParms; i++ ) {
		Parm *parm = &g_parms.m_parms[i];

		if ( parm->m_flags & PF_HIDDEN ) continue;
		if ( parm->m_type == TYPE_COMMENT ) continue;

		if ( parm->m_flags & PF_DUP ) continue;
		if ( parm->m_flags & PF_NOAPI ) continue;
		if ( parm->m_flags & PF_DIFFBOT ) continue;

		int32_t pageNum = parm->m_page;

		// these have PAGE_NONE for some reason
		if ( parm->m_obj == OBJ_SI ) pageNum = PAGE_RESULTS;

		if ( pageNum != PAGENUM ) continue;

		SafeBuf tmp;
		tmp.setLabel("apisb");
		char diff = 0;
		bool printVal = false;
		if ( parm->m_type != TYPE_CMD &&
		     ((parm->m_obj == OBJ_COLL && cr) ||
		      parm->m_obj==OBJ_CONF) ) {
			printVal = true;
			parm->printVal ( &tmp , cr->m_collnum , 0 );
			const char *def = parm->m_def;
			if ( ! def && parm->m_type == TYPE_IP) 
				def = "0.0.0.0";
			if ( ! def ) def = "";

			if ( strcmp(tmp.getBufStart(),def) != 0 ) {
				diff=1;
			}
		}

		// do not show passwords in this!
		if ( parm->m_flags & PF_PRIVATE )
			printVal = false;

		// print the parm
		if ( diff == 1 ) 
			sb->safePrintf ( "<tr bgcolor=orange>");
		else
			sb->safePrintf ( "<tr bgcolor=#%s>",blues[count%2]);

		sb->safePrintf("<td>%" PRId32"</td>",count++);

		// use m_cgi if no m_scgi
		const char *cgi = parm->m_cgi;

		sb->safePrintf("<td><b>%s</b></td>", cgi);

		//sb->safePrintf("<td><nobr><a href=/%s?c=%s>/%s"
		//"</a></nobr></td>",
		//page,coll,page);

		sb->safePrintf("<td nowrap=1>");
		switch ( parm->m_type ) {
		case TYPE_CMD: sb->safePrintf("UNARY CMD (set to 1)"); break;
		case TYPE_BOOL: sb->safePrintf ( "BOOL (0 or 1)" ); break;
		case TYPE_CHECKBOX: sb->safePrintf ( "BOOL (0 or 1)" ); break;
		case TYPE_CHAR: sb->safePrintf ( "CHAR" ); break;
		case TYPE_CHAR2: sb->safePrintf ( "CHAR" ); break;
		case TYPE_FLOAT: sb->safePrintf ( "FLOAT32" ); break;
		case TYPE_DOUBLE: sb->safePrintf ( "FLOAT64" ); break;
		case TYPE_IP: sb->safePrintf ( "IP" ); break;
		case TYPE_LONG: sb->safePrintf ( " PRId32" ); break;
		case TYPE_LONG_LONG: sb->safePrintf ( " PRId64" ); break;
		case TYPE_CHARPTR: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRING: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRINGBOX: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRINGNONEMPTY:sb->safePrintf ( "STRING" ); break;
		case TYPE_SAFEBUF: sb->safePrintf ( "STRING" ); break;
		case TYPE_FILEUPLOADBUTTON: sb->safePrintf ( "STRING" ); break;
		default: sb->safePrintf("<b><font color=red>UNKNOWN</font></b>");
		}
		sb->safePrintf ( "</td><td>%s</td>",parm->m_title);
		const char *def = parm->m_def;
		if ( ! def ) def = "";
		sb->safePrintf ( "<td>%s</td>",  def );
		sb->safePrintf ( "<td>%s",  parm->m_desc );
		if ( parm->m_flags & PF_REQUIRED )
			sb->safePrintf(" <b><font color=green>REQUIRED"
				     "</font></b>");

		if ( printVal ) {
			sb->safePrintf("<br><b><nobr>Current value:</nobr> ");
			// print in red if not default value
			if ( diff ) sb->safePrintf("<font color=red>");
			// truncate to 80 chars
			sb->htmlEncode(tmp.getBufStart(),tmp.length(),
					   false,80);
			if ( diff ) sb->safePrintf("</font>");
			sb->safePrintf("</b>");
		}
		sb->safePrintf("</td>");
		sb->safePrintf ( "</tr>\n" );

	}
	
	// end input parm table we started below
	sb->safePrintf("</table><br>\n\n");

	if ( PAGENUM != PAGE_GET &&
	     PAGENUM != PAGE_RESULTS )
		return true;

	//
	// done printing parm table
	//

	//
	// print output in xml
	//
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>Example XML Output</b> "
			"(&format=xml)</tr></tr>"
			"<tr><td bgcolor=%s>"
			, TABLE_STYLE
			, LIGHT_BLUE
			);

	sb->safePrintf("<pre style=max-width:500px;>\n");

	const char *get = "<html><title>Some web page title</title>"
		"<head>My first web page</head></html>";

	// example output in xml
	if ( PAGENUM == PAGE_GET ) {
		SafeBuf xb;
		xb.safePrintf("<response>\n"
			      "\t<statusCode>0</statusCode>\n"
			      "\t<statusMsg>Success</statusMsg>\n"
			      "\t<url><![CDATA[http://www.doi.gov/]]></url>\n"
			      "\t<docId>34111603247</docId>\n"
			      "\t<cachedTimeUTC>1404512549</cachedTimeUTC>\n"
			      "\t<cachedTimeStr>Jul 04, 2014 UTC"
			      "</cachedTimeStr>\n"
			      "\t<content><![CDATA[");
		cdataEncode(&xb,get);
		xb.safePrintf("]]></content>\n");
		xb.safePrintf("</response>\n");
		sb->htmlEncode ( xb.getBufStart() );
	}

	if ( PAGENUM == PAGE_RESULTS ) {
		SafeBuf xb;
		xb.safePrintf("<response>\n"
			      "\t<statusCode>0</statusCode>\n"
			      "\t<statusMsg>Success</statusMsg>\n"
			      "\t<currentTimeUTC>1404513734</currentTimeUTC>\n"
			      "\t<responseTimeMS>284</responseTimeMS>\n"
			      "\t<docsInCollection>226</docsInCollection>\n"
			      "\t<hits>193</hits>\n"
			      "\t<moreResultsFollow>1</moreResultsFollow>\n"

			      "\t<result>\n"
			      "\t\t<imageBase64>/9j/4AAQSkZJRgABAQAAAQABA..."
			      "</imageBase64>\n"
			      "\t\t<imageHeight>350</imageHeight>\n"
			      "\t\t<imageWidth>223</imageWidth>\n"
			      "\t\t<origImageHeight>470</origImageHeight>\n"
			      "\t\t<origImageWidth>300</origImageWidth>\n"
			      "\t\t<title><![CDATA[U.S....]]></title>\n"
			      "\t\t<sum>Department of the Interior protects "
			      "America's natural resources and</sum>\n"
			      "\t\t<url><![CDATA[www.doi.gov]]></url>\n"
			      "\t\t<size>  64k</size>\n"
			      "\t\t<docId>34111603247</docId>\n"
			      "\t\t<site>www.doi.gov</site>\n"
			      "\t\t<spidered>1404512549</spidered>\n"
			      "\t\t<firstIndexedDateUTC>1404512549"
			      "</firstIndexedDateUTC>\n"
			      "\t\t<contentHash32>2680492249</contentHash32>\n"
			      "\t\t<language>English</language>\n"
			      "\t</result>\n"

			      "</response>\n");
		sb->htmlEncode ( xb.getBufStart() );
	}


	sb->safePrintf("</pre>");
	sb->safePrintf ( "</td></tr></table><br>\n\n" );
	
	//
	// print output in json
	//
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>Example JSON Output</b> "
			"(&format=json)</tr></tr>"
			"<tr><td bgcolor=%s>"
			, TABLE_STYLE
			, LIGHT_BLUE
			);
	sb->safePrintf("<pre>\n");


	// example output in xml
	if ( PAGENUM == PAGE_GET ) {
		sb->safePrintf(
			       "{ \"response:\"{\n"
			       "\t\"statusCode\":0,\n"
			       "\t\"statusMsg\":\"Success\",\n"
			       "\t\"url\":\"http://www.doi.gov/\",\n"
			       "\t\"docId\":34111603247,\n"
			       "\t\"cachedTimeUTC\":1404512549,\n"
			       "\t\"cachedTimeStr\":\"Jul 04, 2014 UTC\",\n"
			       "\t\"content\":\"");
		SafeBuf js;
		js.jsonEncode(get);
		sb->htmlEncode(js.getBufStart());
		sb->safePrintf("\"\n"
			       "}\n"
			       "}\n");
	}

	int32_t cols = 40;

	if ( PAGENUM == PAGE_RESULTS ) {
		sb->safePrintf(
			       "<b>{ \"response:\"{\n</b>" );


		sb->brify2 ( "\n"
			     "\t# This is zero on a successful query. "
			     "Otherwise "
			     "it will be a non-zero number indicating the "
			     "error code.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf ( "<b>\t\"statusCode\":0,\n\n</b>" );


		sb->brify2 ( "\t# Similar to above, this is \"Success\" "
			     "on a successful query. Otherwise "
			     "it will indicate an error message "
			     "corresponding to the statusCode above.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf ( "<b>\t\"statusMsg\":\"Success\",\n\n</b>");

		sb->brify2 ( "\t# This is the current time in UTC in unix "
			     "timestamp format (seconds since the epoch) "
			     "that the "
			     "server has when generating this JSON response.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf ("<b>\t\"currentTimeUTC\":1404588231,\n\n</b>");

		sb->brify2 ( "\t# This is how long it took in milliseconds "
			     "to generate "
			     "the JSON response from reception of the "
			     "request.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"responseTimeMS\":312,\n\n</b>");


		sb->brify2 ( "\t# This is how many matches were excluded "
			     "from the search results because they "
			     "were considered duplicates, banned, "
			     "had errors generating the summary, or where "
			     "from an over-represented site. To show "
			     "them use the &sc &dr &pss "
			     "&sb and &showerrors "
			     "input parameters described above.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"numResultsOmitted\":3,\n\n</b>");


		sb->brify2 ( "\t# This is how many shards failed to return "
			     "results. Gigablast gets results from "
			     "multiple shards (computers) and merges them "
			     "to get the final result set. Some times a "
			     "shard is down or malfunctioning so it will "
			     "not contribute to the results. So If this "
			     "number is non-zero then you had such a shard.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"numShardsSkipped\":0,\n\n</b>");

		sb->brify2 ( "\t# This is how much of the index we managed to "
		             "search. Normally 100.0, but dead shards and "
			     "deadline-cutoffs reduce this."
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"pctSearched\":98.4,\n\n</b>");


		sb->brify2 ( "\t# This is how many shards are "
			     "ideally in use by Gigablast to generate "
			     "search results.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"totalShards\":159,\n\n</b>");

		sb->brify2 ( "\t# This is how many total documents are in "
			     "the collection being searched.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"docsInCollection\":226,\n\n</b>");

		sb->brify2 ( "\t# This is how many of those documents "
			     "matched the query.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf( "<b>\t\"hits\":193,\n\n</b>");
		
		sb->brify2 ( "\t# This is 1 if more search results are "
			     "available, otherwise it is 0.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"moreResultsFollow\":1,\n\n</b>");


		// queryInfo:
		sb->brify2 ( "\t# Start of query-based information.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"queryInfo\":{\n</b>\n");

		sb->brify2 ( "\t\t# The entire query that was received, "
			     "represented as a single string.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"fullQuery\":\"test\",\n\n</b>");


		sb->brify2 ( 
			"\t\t# The language of the query. "
			"This is the 'preferred' language of the "
			"search results. It is reflecting the "
			"&qlang input parameter described above. "
			"Search results in this language (or an unknown "
		        "language) will receive a large boost. The "
			"boost is multiplicative. The default boost size "
		        "can be "
			"overridden using the &langw input parameter "
			"described above. This language abbreviation here "
			"is usually 2 letter, but can be more, like in "
			"the case of zh-cn, for example.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"queryLanguageAbbr\":\"en\",\n\n</b>");


		sb->brify2 ( 
			"\t\t# The language of the query. Just like "
			"above but the language is spelled out. It may "
			"be multiple words.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"queryLanguage\":\"English\",\n\n"
			       "</b>");


		sb->brify2 ( 
			"\t\t# List of space separated words in the "
			"query that were ignored for the most part. "
			"Because they were common words for the "
			"query language they are in.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"ignoredWords\":\"to the\",\n\n"
			       "</b>");

		sb->brify2 ( 
			"\t\t# There is a maximum limit placed on the "
			"number of query terms we search on to keep things "
			"fast. This can "
			"be changed in the search controls.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"queryNumTermsTotal\":52,\n</b>");
		sb->safePrintf("<b>\t\t\"queryNumTermsUsed\":20,\n</b>");
		sb->safePrintf("<b>\t\t\"queryWasTruncated\":1,\n\n</b>");

		sb->brify2 ( 
			"\t\t# The start of the terms array. Each query "
			"is broken down into a list of terms. Each "
			"term is described here.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"terms\":[\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# The first query term in the JSON "
			"terms array.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t{\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# The term number, starting at 0.\n"
			, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termNum\":0,\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# The term as a string.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termStr\":\"test\",\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# The term frequency. An estimate of how "
			"many pages in the collection contain the term. "
			"Helps us weight terms by popularity when "
			"scoring the results.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termFreq\":425239458,\n\n</b>");

		sb->brify2 ( 
			"\t\t\t# A 48-bit hash of the term. Used to represent "
			"the term in the index.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termHash48\":"
			       "67259736306430,\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# A 64-bit hash of the term.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termHash64\":"
			       "9448336835959712000,\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# If the term has a field, like the term "
			"title:cat, then what is the hash of the field. In "
			"this example it would be the hash of 'title'. But "
			"for the query 'test' there is no field so it is "
			"0.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"prefixHash64\":"
			       "0\n\n</b>");

		sb->safePrintf("\t\t\t},\n\n");


		sb->brify2 ( 
			"\t\t\t# The second "
			"query term in the JSON terms array.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t{\n\n</b>");


		sb->safePrintf("<b>\t\t\t\"termNum\":1,\n</b>");
		sb->safePrintf("<b>\t\t\t\"termStr\":\"tested\",\n\n</b>");


		sb->brify2 ( 
			"\t\t\t# The language the term is from, in the case "
			"of query expansion on the original query term. "
			"Gigablast tries to find multiple forms of the word "
			"that have the same essential meaning. It uses the "
			"specified query language (&qlang), however, if "
			"a query term is from a different language, then "
			"that language will be implied for query expansion.\n"
			, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"termLang\":\"en\",\n\n</b>");

		sb->brify2 ( 
			    "\t\t\t# The query term that this term is a "
			    "form of.\n"
		, cols , "\n\t\t\t# " , false );
		sb->safePrintf("<b>\t\t\t\"synonymOf\":\"test\",\n\n</b>");


		sb->safePrintf("<b>\t\t\t\"termFreq\":73338909,\n</b>");
		sb->safePrintf("<b>\t\t\t\"termHash48\":"
			       "66292713121321,\n</b>");
		sb->safePrintf("<b>\t\t\t\"termHash64\":"
			       "9448336835959712000,\n</b>");
		sb->safePrintf("<b>\t\t\t\"prefixHash64\":"
			       "0\n</b>");
		sb->safePrintf("\t\t\t},\n\n");
		sb->safePrintf("\t\t\t...\n\n");


		// end terms array
		sb->brify2 ( 
			    "\t\t# End of the JSON terms array.\n"
		, cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t]\n\n</b>");


		// end queryInfo array
		sb->brify2 ( 
			    "\t# End of the queryInfo JSON structure.\n"
		, cols , "\n\t# " , false );
		sb->safePrintf("<b>\t},\n</b>\n");


		// results:
		sb->brify2 ( "\t# Start of the JSON array of "
			     "individual search results.\n"
			     , cols , "\n\t# " , false );
		sb->safePrintf("<b>\t\"results\":[\n</b>\n");


		sb->brify2 ( "\t\t# The first result in the array.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t{\n\n</b>");


		sb->brify2 ( "\t\t# The title of the result. In UTF-8.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"title\":"
			       "\"This is the title.\",\n\n</b>");



		sb->brify2 ( "\t\t# The content type of the url. "
			     "Can be html, pdf, text, xml, json, doc, xls "
			     "or ps.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"contentType\":\"html\",\n\n</b>");

		sb->brify2 ( "\t\t# The summary excerpt of the result. "
			     "In UTF-8.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"sum\":\"Department of the Interior ");
		sb->safePrintf("protects America's natural "
			       "resources.\",\n\n</b>");

		sb->brify2 ( "\t\t# The url of the result. If it starts "
			     "with http:// then that is omitted. Also "
			     "omits the trailing / if the urls is just "
			     "a domain or subdomain on the root path.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"url\":\"www.doi.gov\",\n\n</b>");

		sb->brify2 ( "\t\t# The hopcount of the url. The minimum "
			     "number of links we would have to click to get "
			     "to it from a root url. If this is 0 that means "
			     "the url is a root url, like "
			     "http://www.root.com/.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"hopCount\":0,\n\n</b>");

		sb->brify2 ( "\t\t# The size of the result's content. "
			     "Always in kilobytes. k stands for kilobytes. "
			     "Could be a floating point number or and "
			     "integer.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"size\":\"  64k\",\n\n</b>");


		sb->brify2 ( "\t\t# The exact size of the result's content "
			     "in bytes.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"sizeInBytes\":64560,\n\n</b>");

		sb->brify2 ( "\t\t# The unique document identifier of the "
			     "result. Used for getting the cached content "
			     "of the url.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"docId\":34111603247,\n\n</b>");

		sb->brify2 ( "\t\t# The site the result comes from. "
			     "Usually a subdomain, but can also include "
			     "part of the URL path, like, "
			     "abc.com/users/brad/. A site is a set of "
			     "web pages controlled by the same entity.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"site\":\"www.doi.gov\",\n\n</b>");

		sb->brify2 ( "\t\t# The time the url was last INDEXED. "
			     "If there was an error or the "
			     "url's content was unchanged since last "
			     "download, then this time will remain unchanged "
			     "because the document is not reindexed in those "
			     "cases. "
			     "Time is in unix timestamp format and is in "
			     "UTC.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"spidered\":1404512549,\n\n</b>");


		sb->brify2 ( "\t\t# The first time the url was "
			     "successfully INDEXED. "
			     "Time is in unix timestamp format and is in "
			     "UTC.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"firstIndexedDateUTC\":1404512549,"
			       "\n\n</b>");

		sb->brify2 ( "\t\t# A 32-bit hash of the url's content. It "
			     "is used to determine if the content changes "
			     "the next time we download it.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"contentHash32\":2680492249,\n\n</b>");

		sb->brify2 ( "\t\t# The dominant language that the url's "
			     "content is in. The language name is spelled "
			     "out in its entirety.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"language\":\"English\"\n\n</b>");

		sb->brify2 ( "\t\t# A convenient abbreviation of "
			     "the above language. Most are two characters, "
			     "but some, like zh-cn, are more.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"langAbbr\":\"en\"\n\n</b>");


		sb->brify2 ( "\t\t# If the result has an associated image "
			     "then the image thumbnail is encoded in "
			     "base64 format here. It is a jpg image.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"imageBase64\":\"/9j/4AAQSkZJR...\","
			       "\n\n</b>");

		sb->brify2 ( "\t\t# If the result has an associated image "
			     "then what is its height and width of the "
			     "above jpg thumbnail image in pixels?\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"imageHeight\":223,\n");
		sb->safePrintf("\t\t\"imageWidth\":350,\n\n</b>");

		sb->brify2 ( "\t\t# If the result has an associated image "
			     "then what are the dimensions of the original "
			     "image in pixels?\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("<b>\t\t\"origImageHeight\":300,\n");
		sb->safePrintf("\t\t\"origImageWidth\":470\n\n</b>");


		sb->brify2 ( "\t\t# End of the first result.\n"
			     , cols , "\n\t\t# " , false );
		sb->safePrintf("\t\t<b>},</b>\n");

		sb->safePrintf("\n\t\t...\n");

		
		sb->brify2 ( 
			    "\n\t# End of the JSON results array.\n"
		, cols , "\n\t# " , false );
		sb->safePrintf("<b>\t]</b>\n\n");

		sb->brify2 ( "# End of the response.\n"
		, cols , "\n\t# " , false );
		sb->safePrintf("}\n\n");

		sb->safePrintf("}\n");

	}


	sb->safePrintf("</pre>");
	sb->safePrintf ( "</td></tr></table><br>\n\n" );

	return true;
}



// any admin page calls cr->hasPermission ( hr ) and if that returns false
// then we call this function to give users a chance to login
bool sendPageLogin ( TcpSocket *socket , HttpRequest *hr ) {

	// get the collection
	int32_t  collLen = 0;
	const char *coll    = hr->getString("c",&collLen);

	// default to main collection. if you can login to main then you
	// are considered the root admin here...
	if ( ! coll ) coll = "main";

	SafeBuf emsg;

	// does collection exist? ...who cares, proxy doesn't have coll data.
	CollectionRec *cr = g_collectiondb.getRec ( hr );
	if ( ! cr )
		emsg.safePrintf("Collection \"%s\" does not exist.",coll);

	// just make cookie same format as an http request for ez parsing
	//char cookieData[2024];

	SafeBuf sb;

	// print colors
	g_pages.printColors ( &sb );
	// start table
	sb.safePrintf( "<table><tr><td>");
	// print logo
	g_pages.printLogo   ( &sb , coll );

	// get password from cgi parms OR cookie
	const char *pwd = hr->getString("pwd");
	if ( ! pwd ) pwd = hr->getStringFromCookie("pwd");
	// fix "pwd=" cookie (from logout) issue
	if ( pwd && ! pwd[0] ) pwd = NULL;

	bool hasPermission = false;

	// this password applies to ALL collections. it's the root admin pwd
	if ( cr && pwd && g_conf.isMasterAdmin ( socket , hr ) ) 
		hasPermission = true;

	if ( emsg.length() == 0 && ! hasPermission && pwd )
		emsg.safePrintf("Master password incorrect");


	// sanity
	if ( hasPermission && emsg.length() ) { g_process.shutdownAbort(true); }

	// what page are they originally trying to get to?
	int32_t page = g_pages.getDynamicPageNumber(hr);

	// try to the get reference Page
	int32_t refPage = hr->getLong("ref",-1);
	// if they cam to login page directly... to to basic page then
	if ( refPage == PAGE_LOGIN || refPage < 0 )
		refPage = PAGE_BASIC_SETTINGS;

	// if they had an original destination, redirect there NOW
	const WebPage *pagePtr = g_pages.getPage(refPage);

	const char *ep = emsg.getBufStart();
	if ( !ep ) ep = "";

	const char *ff = "admin/settings";
	if ( pagePtr ) ff = pagePtr->m_filename;
	
	sb.safePrintf(
		      "&nbsp; &nbsp; "
		      "</td><td><font size=+1><b>Login</b></font></td></tr>"
		      "</table>" 
		      "<form method=post action=\"/%s\" name=f>"
		      , ff );

	sb.safePrintf(
		  "<input type=hidden name=ref value=\"%" PRId32"\">"
		  "<center>"
		  "<br><br>"
		  "<font color=ff0000><b>%s</b></font>"
		  "<br><br>"
		  "<br>"

		  "<table cellpadding=2><tr><td>"

		  //"<b>Collection</td><td>"
		  "<input type=hidden name=c size=30 value=\"%s\">"
		  //"</td><td></td></tr>"
		  //"<tr><td>"

		  "<b>Master Password : &nbsp; </td>"
		  "<td><input id=ppp type=password name=pwd size=30>"
		  "</td><td>"
		  "<input type=submit value=ok border=0 onclick=\""
		  "document.cookie='pwd='+document.getElementById('ppp')"
		  ".value+"
		  // fix so cookies work for msie. expires= is wrong i guess.
		  //"';expires=9999999';"
		  "';max-age=9999999';"
		  "\"></td>"
		  "</tr></table>"
		  "</center>"
		  "<br><br>"
		  , page, ep , coll );

	// send the page
	return g_httpServer.sendDynamicPage ( socket , 
					      sb.getBufStart(),
					      sb.length(),
					      -1    , // cacheTime
					      false , // POSTReply?
					      NULL  , // contentType
					      -1   ,
					      NULL);// cookie
}

bool printRedBox2 ( SafeBuf *sb , TcpSocket *sock , HttpRequest *hr ) {
	SafeBuf mb;
	// return false if no red box
	if ( ! printRedBox ( &mb , sock , hr ) ) return false;
	// otherwise, print it
	sb->safeStrcpy ( mb.getBufStart() );
	// return true since we printed one
	return true;
}

// emergency message box
bool printRedBox ( SafeBuf *mb , TcpSocket *sock , HttpRequest *hr ) {

	PingServer *ps = &g_pingServer;

	const char *box = 
		"<table cellpadding=5 "
		// full width of enclosing div
		"width=100%% "
		"style=\""
		"background-color:#ff6666;"
		"border:2px #8f0000 solid;"
		"border-radius:5px;"
		//"max-width:500px;"
		"\" "
		"border=0"
		">"
		"<tr><td>";
	const char *boxEnd =
		"</td></tr></table>";

	int32_t adds = 0;


	mb->safePrintf("<div style=max-width:500px;>");

	int32_t page = g_pages.getDynamicPageNumber ( hr );

	// are we just starting off? give them a little help.
	CollectionRec *crm = g_collectiondb.getRec("main");
	if ( g_collectiondb.m_numRecs == 1 && 
	     crm &&
	     page == PAGE_ROOT && // isRootWebPage &&
	     crm->m_globalCrawlInfo.m_pageDownloadAttempts == 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Welcome to Gigablast. The most powerful "
			       "search engine you can legally download. "
			       "Please add the websites you want to spider "
			       "<a href=/admin/settings?c=main>here</a>."
			       );
		mb->safePrintf("%s",boxEnd);
	}

	if ( page == PAGE_ROOT ) { // isRootWebPage ) {
		mb->safePrintf("</div>");
		return (bool)adds;
	}

	bool printedMaster = false;
	if ( g_conf.m_masterPwds.length() == 0 &&
	     g_conf.m_connectIps.length() == 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("URGENT. Please specify a MASTER password "
			       "or IP address in the "
			       "<a href=/admin/masterpasswords>master "
			       "password</a> "
			       "table. Right now anybody might be able "
			       "to access the Gigablast admin controls.");
		mb->safePrintf("%s",boxEnd);
		printedMaster = true;
	}

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	const char *coll = "";
	if ( cr ) coll = cr->m_coll;

	if ( cr &&
	     ! printedMaster &&
	     g_conf.m_useCollectionPasswords &&
	     cr->m_collectionPasswords.length() == 0 && 
	     cr->m_collectionIps.length() == 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("URGENT. Please specify a COLLECTION password "
			       "or IP address in the "
			       "<a href=/admin/collectionpasswords?c=%s>"
			       "password</a> "
			       "table. Right now anybody might be able "
			       "to access the Gigablast admin controls "
			       "for the <b>%s</b> collection."
			       , cr->m_coll
			       , cr->m_coll
			       );
		mb->safePrintf("%s",boxEnd);
	}

	// out of disk space?
	int32_t out = 0;
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];
		if ( h->m_pingInfo.m_diskUsage < 98.0 ) continue;
		out++;
	}
	if ( out > 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		const char *s = "s are";
		if ( out == 1 ) s = " is";
		mb->safePrintf("%s",box);
		mb->safePrintf("%" PRId32" host%s over 98%% disk usage. "
			       "See the <a href=/admin/hosts?c=%s>"
			       "hosts</a> table.",out,s,coll);
		mb->safePrintf("%s",boxEnd);
	}

	// injections disabled?
	if ( ! g_conf.m_injectionsEnabled ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Injections are disabled in the "
			       "<a href=/admin/master?c=%s>"
			       "master controls</a>."
			       ,coll);
		mb->safePrintf("%s",boxEnd);
	}

	// querying disabled?
	if ( ! g_conf.m_queryingEnabled ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Querying is disabled in the "
			       "<a href=/admin/master?c=%s>"
			       "master controls</a>."
			       ,coll);
		mb->safePrintf("%s",boxEnd);
	}


	bool sameVersions = true;
	for ( int32_t i = 1 ; i < g_hostdb.getNumHosts() ; i++ ) {
		// count if not dead
		Host *h1 = &g_hostdb.m_hosts[i-1];
		Host *h2 = &g_hostdb.m_hosts[i];
		if (!strcmp(h1->m_pingInfo.m_gbVersionStr,
			    h2->m_pingInfo.m_gbVersionStr))
			continue;
		sameVersions = false;
		break;
	}
	if ( ! sameVersions ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("One or more hosts have different gb versions. "
			       "See the <a href=/admin/hosts?c=%s>hosts</a> "
			       "table.",coll);
		mb->safePrintf("%s",boxEnd);
	}

	
	int jammedHosts = 0;
	for ( int32_t i = 1 ; i < g_hostdb.getNumHosts() ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];
		if ( g_hostdb.isDead( h ) ) continue;
		if ( h->m_pingInfo.m_udpSlotsInUseIncoming>= 400)jammedHosts++;
	}
	if ( jammedHosts > 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		const char *s = "s are";
		if ( out == 1 ) s = " is";
		mb->safePrintf("%s",box);
		mb->safePrintf("%" PRId32" host%s jammed with "
			       "over %" PRId32" unhandled "
			       "incoming udp requests. "
			       "See <a href=/admin/sockets?c=%s>sockets</a>"
			       " table.",jammedHosts,s,400,coll);
		mb->safePrintf("%s",boxEnd);
	}

	if ( g_profiler.m_realTimeProfilerRunning ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Profiler is running. Performance is "
			       "somewhat compromised. Disable on the "
			       "profiler page.");
		mb->safePrintf("%s",boxEnd);
	}

	if ( g_pingServer.m_hostsConfInDisagreement ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("The hosts.conf file "
			      "is not the same over all hosts.");
		mb->safePrintf("%s",boxEnd);
	}

	if ( g_rebalance.m_isScanning ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Rebalancer is currently running.");
		mb->safePrintf("%s",boxEnd);
	}
	// if any host had foreign recs, not that
	const char *needsRebalance = g_rebalance.getNeedsRebalance();
	if ( ! g_rebalance.m_isScanning &&
	     needsRebalance &&
	     *needsRebalance ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("A host requires a shard rebalance. "
			       "Click 'rebalance shards' in the "
			       "<a href=/admin/master?c=%s>"
			       "master controls</a> "
			       "to rebalance all hosts.",coll);
		mb->safePrintf("%s",boxEnd);
	}

	const WebPage *wp = g_pages.getPage(page);

	if ( wp && 
	     (wp->m_pgflags & (PG_MASTERADMIN|PG_COLLADMIN)) &&
	     ! g_conf.isMasterAdmin(sock,hr) &&
	     ! g_conf.isCollAdmin(sock,hr) ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);

		const char *ff = "admin/settings";
		if ( wp ) ff = wp->m_filename;

		mb->safePrintf("You have no write access to these "
			       "controls. Please enter the collection or "
			       "master password to get access: "

			       "<form method=GET action=\"/%s\" name=xyz>"

			       "<input type=password id=ppp name=xpwd size=20>"

			       "<input type=submit value=ok "
			       "border=0 onclick=\""
			       "document.cookie='pwd='+"
			       "document.getElementById('ppp')"
			       ".value+"
			       "';max-age=9999999';"
			       "\">"

			       "</form>"
			       , ff );

		mb->safePrintf("%s",boxEnd);
	}


	if ( ps->m_numHostsDead ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		const char *s = "hosts are";
		if ( ps->m_numHostsDead == 1 ) s = "host is";
		mb->safePrintf("%s",box);
		mb->safePrintf("%" PRId32" %s dead and not responding to "
			      "pings. See the "
			       "<a href=/admin/hosts?c=%s>hosts table</a>.",
			       ps->m_numHostsDead ,s ,coll);
		mb->safePrintf("%s",boxEnd);
	}


	mb->safePrintf("</div>");

	return (bool)adds;
}
