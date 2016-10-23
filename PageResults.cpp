#include "gb-include.h"

#include "Collectiondb.h"
#include "Stats.h"
#include "Statsdb.h"
#include "Statistics.h"
#include "Query.h"
#include "Speller.h"
#include "Msg40.h"
#include "Pages.h"
#include "Highlight.h"
#include "SearchInput.h"
#include <math.h>
#include "SafeBuf.h"
#include "Pos.h"
#include "Bits.h"
#include "sort.h"
#include "CountryCode.h"
#include "Unicode.h"
#include "Posdb.h"
#include "PosdbTable.h"
#include "PageResults.h"
#include "PageRoot.h"
#include "Proxy.h"
#include "Json.h"
#include "Images.h" //Thumbnail*
#include "HttpMime.h"
#include "Process.h"
#include "Linkdb.h"
#include "XmlDoc.h"
#include "GbUtil.h"

static bool printSearchFiltersBar ( SafeBuf *sb , HttpRequest *hr ) ;
static bool printMenu ( SafeBuf *sb , int32_t menuNum , HttpRequest *hr ) ;

static void gotResultsWrapper  ( void *state ) ;
static void gotState           ( void *state ) ;
static bool gotResults         ( void *state ) ;

static bool replaceParm ( const char *cgi , SafeBuf *newUrl , HttpRequest *hr ) ;
static bool replaceParm2 ( const char *cgi , SafeBuf *newUrl ,
			   const char *oldUrl , int32_t oldUrlLen ) ;


static bool printPairScore (SafeBuf *sb , SearchInput *si , PairScore *ps , Msg20Reply *mr ) ;

static bool printScoresHeader ( SafeBuf *sb ) ;

static bool printMetaContent ( Msg40 *msg40 , int32_t i ,State0 *st, SafeBuf *sb );

static bool printSingleScore (SafeBuf *sb , SearchInput *si , SingleScore *ss ,
			Msg20Reply *mr ) ;

static bool printSingleTerm ( SafeBuf *sb , class Query *q , class SingleScore *ss );
static bool printTermPairs ( SafeBuf *sb , class Query *q , class PairScore *ps ) ;

static bool printLogoAndSearchBox (SafeBuf *sb , class HttpRequest *hr, SearchInput *si );


bool sendReply ( State0 *st , char *reply ) {

	int32_t savedErr = g_errno;

	TcpSocket *sock = st->m_socket;
	if ( ! sock ) { 
		log("results: not sending back results on an empty socket."
		    "socket must have closed on us abruptly.");
		//g_process.shutdownAbort(true); }
	}
	SearchInput *si = &st->m_si;
	const char *ct = "text/html";
	if ( si->m_format == FORMAT_XML ) ct = "text/xml"; 
	if ( si->m_format == FORMAT_JSON ) ct = "application/json";

	const char *charset = "utf-8";
	char format = si->m_format;

	// . filter anything < 0x20 to 0x20 to keep XML legal
	// . except \t, \n and \r, they're ok
	// . gotta set "f" down here in case it realloc'd the buf
	if ( format == FORMAT_XML && reply ) {
		unsigned char *f = (unsigned char *)reply;
		for ( ; *f ; f++ ) 
			if ( *f < 0x20 && *f!='\t' && *f!='\n' && *f!='\r' ) 
				*f = 0x20;
	}


	int32_t rlen = 0;
	if ( reply ) rlen = strlen(reply);
	logf(LOG_DEBUG,"gb: sending back %" PRId32" bytes",rlen);

	// . use light brown if coming directly from an end user
	// . use darker brown if xml feed
	int32_t color = 0x00b58869;
	if ( si->m_format != FORMAT_HTML ) {
		color = 0x00753d30 ;
	}

	int64_t nowms = gettimeofdayInMilliseconds();
	int64_t took  = nowms - st->m_startTime ;
	g_stats.addStat_r ( took            ,
			    st->m_startTime , 
			    nowms,
			    color ,
			    STAT_QUERY );

	// add to statsdb, use # of qterms as the value/qty
	g_statsdb.addStat ( 0,
			    "query",
			    st->m_startTime,
			    nowms,
			    si->m_q.m_numTerms);

	Statistics::register_query_time(si->m_q.m_numWords, si->m_queryLangId, took);
	// . log the time
	// . do not do this if g_errno is set lest m_sbuf1 be bogus b/c
	//   it failed to allocate its buf to hold terminating \0 in
	//   SearchInput::setQueryBuffers()
	if ( ! g_errno && st->m_took >= g_conf.m_logQueryTimeThreshold ) {
		logf(LOG_WARN,"Query took %" PRId64" ms for %s. results=%" PRId32,
		     st->m_took,
		     si->m_sbuf1.getBufStart(),
		     st->m_msg40.getNumResults());
	}

	//bool xml = si->m_xml;

	g_stats.logAvgQueryTime(st->m_startTime);

	//log("results: debug: in sendReply deleting st=%" PTRFMT,(PTRTYPE)st);

	if ( ! savedErr ) { // g_errno ) {
		g_stats.m_numSuccess++;
		// . one hour cache time... no 1000 hours, basically infinite
		// . no because if we redo the query the results are cached
		//int32_t cacheTime = 3600;//*1000;
		// no... do not use cache
		int32_t cacheTime = -1;
		// the "Check it" link on add url uses &usecache=0 to tell
		// the browser not to use its cache...
		//if ( hr->getLong("usecache",-1) == 0 ) cacheTime = 0;
		//
		// send back the actual search results
		//
		if ( sock )
		g_httpServer.sendDynamicPage(sock,
					     reply,
					     rlen,//strlen(reply),
					     // don't let the ajax re-gen
					     // if they hit the back button!
					     // so make this 1 hour, not 0
					     cacheTime, // cachetime in secs
					     false, // POSTReply?
					     ct,
					     -1, // httpstatus -1 -> 200
					     NULL, // cookieptr
					     charset );

		// free st after sending reply since "st->m_sb" = "reply"
		mdelete(st, sizeof(State0), "PageResults2");
		delete st;
		return true;
	}
	// error otherwise
	if ( savedErr != ENOPERM && savedErr != EQUERYINGDISABLED ) 
		g_stats.m_numFails++;

	mdelete(st, sizeof(State0), "PageResults2");
	delete st;

	// if we had a broken pipe from the browser while sending
	// them the search results, then we end up closing the socket fd
	// in TcpServer::sendChunk() > sendMsg() > destroySocket()
	if ( sock && sock->m_numDestroys ) {
		log("results: not sending back error on destroyed socket "
		    "sd=%" PRId32,sock->m_sd);
		return true;
	}

	int32_t status = 500;
	if (savedErr == EBADREQUEST ||
	    savedErr == ENOPERM ||
	    savedErr == ENOCOLLREC) 
		status = 400;

	if ( sock )
	g_httpServer.sendQueryErrorReply(sock,
					 status,
					 mstrerror(savedErr),
					 format,//xml,
					 savedErr, 
					 "There was an error!");
	return true;
}

static bool printCSSHead ( SafeBuf *sb , char format ) {
	sb->safePrintf(
			      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML "
			      "4.01 Transitional//EN\">\n"
			      //"<meta http-equiv=\"Content-Type\" "
			      //"content=\"text/html; charset=utf-8\">\n"
			      "<html>\n"
			      "<head>\n"
			      "<title>Gigablast Search Results</title>\n"
			      "<style><!--"
			      "body {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      );

	sb->safePrintf(	      "color: #000000;"
			      "font-size: 12px;"
			      //"margin: 20px 5px;"
			      "}"
			      "a:link {color:#00c}"
			      "a:visited {color:#551a8b}"
			      "a:active {color:#f00}"
			      ".bold {font-weight: bold;}"
			      ".bluetable {background:#d1e1ff;"
			      "margin-bottom:15px;font-size:12px;}"
			      ".url {color:#008000;}"
			      ".cached, .cached a {font-size: 10px;"
			      "color: #666666;"
			      "}"
			      "table {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      "color: #000000;"
			      "font-size: 12px;"
			      "}"
			      ".directory {font-size: 16px;}"
			      "-->\n"
			      "</style>\n"
			      "</head>\n"
			      );
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "msg" will be inserted into the access log for this request
bool sendPageResults ( TcpSocket *s , HttpRequest *hr ) {
	// . check for sdirt=4, this a site search on the given directory id
	// . need to pre-query the directory first to get the sites to search
	//   this will likely have just been cached so it should be quick
	// . then need to construct a site search query
	//int32_t xml = hr->getLong("xml",0);

	// what format should search results be in? default is html
	char format = hr->getReplyFormat();

	// make a new state
	State0 *st;
	try {
		st = new (State0);
	} catch ( ... ) {
		g_errno = ENOMEM;
		log("query: Query failed. "
		    "Could not allocate %" PRId32" bytes for query. "
		    "Returning HTTP status of 500.",(int32_t)sizeof(State0));
		g_stats.m_numFails++;

		return g_httpServer.sendQueryErrorReply
			(s,500,mstrerror(g_errno),
			 format, g_errno, "Query failed.  "
			 "Could not allocate memory to execute a search.  "
			 "Please try later." );
	}

	mnew ( st , sizeof(State0) , "PageResults2" );

	// init some stuff
	st->m_didRedownload    = false;
	st->m_xd               = NULL;
	st->m_oldContentHash32 = 0;

	// copy yhits
	if ( ! st->m_hr.copy ( hr ) )
		return sendReply ( st , NULL );

	// set this in case SearchInput::set fails!
	st->m_socket = s;

	// record timestamp so we know if we got our socket closed and swapped
	st->m_socketStartTimeHack = s->m_startTime;

	// save this count so we know if TcpServer.cpp calls destroySocket(s)
	st->m_numDestroys = s->m_numDestroys;

	// . parse it up
	// . this returns false and sets g_errno and, maybe, g_msg on error
	SearchInput *si = &st->m_si;

	// si just copies the ptr into the httprequest
    // into stuff like SearchInput::m_defaultSortLanguage
    // so do not use the "hr" on the stack. SearchInput::
    // m_hr points to the hr we pass into
    // SearchInput::set
	if ( ! si->set ( s , &st->m_hr ) ) {
		log("query: set search input: %s",mstrerror(g_errno));
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return sendReply ( st, NULL );
	}

	// allow up to 1000 results per query for paying clients
	CollectionRec *cr = si->m_cr;

	// save collnum now
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;

	int32_t defHdr = 1;

	// you have to say "&header=1" to get back the header for json now.
	// later on maybe it will default to on.
	st->m_header = hr->getLong("header",defHdr);

	st->m_numDocIds = si->m_docsWanted;

	// save state in TcpSocket's m_tmp ptr for debugging. in case 
	// we lose our string of control and Msg40::getResults() never 
	// comes back.
	s->m_tmp = (char *)st;
	// add query stat
	st->m_startTime = gettimeofdayInMilliseconds();
	// reset
	st->m_errno = 0;

	// debug msg
	log ( LOG_DEBUG , "query: Getting search results for q=%s",
	      st->m_si.m_displayQuery);

	// assume we'll block
	st->m_gotResults = false;
	st->m_gotSpell   = false;

	// reset
	st->m_printedHeaderRow = false;

	// for now disable queries
	if ( ! g_conf.m_queryingEnabled ) {
		g_errno = EQUERYINGDISABLED;
		return sendReply(st,NULL);
	}

	// LAUNCH SPELLER
	// get our spelling correction if we should (spell checker)
	st->m_gotSpell = true;
	st->m_spell[0] = '\0';

	// LAUNCH RESULTS

	// . get some results from it
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . use a niceness of 0 for all queries so they take precedence
	//   over the indexing process
	// . this will copy our passed "query" and "coll" to it's own buffer
	// . we print out matching docIds to int32_t if m_isDebug is true
	// . no longer forward this, since proxy will take care of evenly
	//   distributing its msg 0xfd "forward" requests now
	st->m_gotResults=st->m_msg40.getResults(si,false,st,gotResultsWrapper);
	// save error
	st->m_errno = g_errno;

	//log("results: debug: new state=%" PTRFMT,(PTRTYPE)st);

	// wait for spellcheck and results?
	if ( !st->m_gotSpell || !st->m_gotResults )
		return false;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	bool status2 = gotResults ( st );

	return status2;
}

static void gotResultsWrapper ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	// save error
	st->m_errno = g_errno;
	// mark as gotten
	st->m_gotResults = true;
	gotState (st);
}

static void gotState ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	if ( !st->m_gotSpell || !st->m_gotResults )
		return;
	// we're ready to go
	gotResults ( state );
}

// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
static bool gotResults ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;

	int64_t nowMS = gettimeofdayInMilliseconds();
	// log the time
	int64_t took = nowMS - st->m_startTime;
	// record that
	st->m_took = took;

	//log("results: debug: in gotResults state=%" PTRFMT,(PTRTYPE)st);

	// grab the query
	Msg40 *msg40 = &(st->m_msg40);
	//char  *q    = msg40->getQuery();
	//int32_t   qlen = msg40->getQueryLen();

	SearchInput *si = &st->m_si;

	// if we lost the socket because we were streaming and it
	// got closed from a broken pipe or something, then Msg40.cpp
	// will set st->m_socket to NULL if the fd ends up ending closed
	// because someone else might be using it and we do not want to
	// mess with their TcpSocket settings.
	if ( ! st->m_socket ) {
		log("results: socket is NULL. sending failed.");
		return sendReply(st,NULL);
	}

	// if in streaming mode and we never sent anything and we had
	// an error, then send that back. we never really entered streaming
	// mode in that case. this happens when someone deletes a coll
	// and queries it immediately, then each shard reports ENOCOLLREC.
	// it was causing a socket to be permanently stuck open.
	if ( g_errno &&
	     si->m_streamResults &&
	     st->m_socket->m_totalSent == 0 )
	       return sendReply(st,NULL);


	// if we skipped a shard because it was dead, usually we provide
	// the results anyway, but if this switch is true then return an
	// error code instead. this is the 'all or nothing' switch.
	if ( msg40->m_msg3a.m_skippedShards > 0 &&
	     ! g_conf.m_returnResultsAnyway ) {
	       char reply[256];
	       sprintf ( reply , 
			 "%" PRId32" shard(s) out of %" PRId32" did not "
			 "respond to query."
			 , msg40->m_msg3a.m_skippedShards
			 , g_hostdb.m_numShards );
	       g_errno = ESHARDDOWN;
	       return sendReply(st,reply);
	}


	// if already printed from Msg40.cpp, bail out now
	if ( si->m_streamResults ) {
		// this will be our final send
		if ( st->m_socket->m_streamingMode ) {
			log("res: socket still in streaming mode. wtf? err=%s",
			    mstrerror(g_errno));
			st->m_socket->m_streamingMode = false;
		}
		log("msg40: done streaming. nuking state=0x%" PTRFMT" "
		    "tcpsock=0x%" PTRFMT" "
		    "sd=%i "
		    "msg40=0x%" PTRFMT" q=%s. "
		    "msg20sin=%i msg20sout=%i sendsin=%i sendsout=%i "
		    "numrequests=%i numreplies=%i "
		    ,(PTRTYPE)st
		    ,(PTRTYPE)st->m_socket
		    ,(int)st->m_socket->m_sd
		    ,(PTRTYPE)msg40
		    ,si->m_q.m_orig

		    , msg40->m_numMsg20sIn
		    , msg40->m_numMsg20sOut
		    , msg40->m_sendsIn
		    , msg40->m_sendsOut
		    , msg40->m_numRequests
		    , msg40->m_numReplies

		    );

		// just let tcpserver nuke it, but don't double call
		// the callback, doneSendingWrapper9()... because msg40
		// will have been deleted!
		st->m_socket->m_callback = NULL;

		// fix this to try to fix double close i guess
		// if ( st->m_socket->m_sd > 0 )
		// 	st->m_socket->m_sd *= -1;

		mdelete(st, sizeof(State0), "PageResults2");
		delete st;
		return true;
	}

	// collection rec must still be there since SearchInput references 
	// into it, and it must be the SAME ptr too!
	CollectionRec *cr = si->m_cr;//g_collectiondb.getRec ( collnum );
	if ( ! cr ) { // || cr != si->m_cr ) {
		g_errno = ENOCOLLREC;
		return sendReply(st,NULL);
	}

	// this causes ooms everywhere, not a good fix
	if ( ! msg40->m_msg20 && ! si->m_docIdsOnly && msg40->m_errno ) {
	 	log("msg40: failed to get results q=%s",si->m_q.m_orig);
	 	//g_errno = ENOMEM;
		g_errno = msg40->m_errno;
	 	return sendReply(st,NULL);
	}


 	int32_t numResults = msg40->getNumResults();

	SafeBuf *sb = &st->m_sb;

	// print logo, search box, results x-y, ... into st->m_sb
	printSearchResultsHeader ( st );

	// then print each result
	// don't display more than docsWanted results
	int32_t count = msg40->getDocsWanted();
	bool hadPrintError = false;
	int32_t numPrintedSoFar = 0;

	for ( int32_t i = 0 ; count > 0 && i < numResults ; i++ ) {
		//////////
		//
		// prints in xml or html
		//
		//////////
		if ( ! printResult ( st , i , &numPrintedSoFar ) ) {
			hadPrintError = true;
			break;
		}

		// limit it
		count--;
	}


	if ( hadPrintError ) {
		if ( ! g_errno ) g_errno = EBADENGINEER;
		log("query: had error: %s",mstrerror(g_errno));
		//return sendReply ( st , sb.getBufStart() );
	}

	// wrap it up with Next 10 etc.
	printSearchResultsTail ( st );

	// END SERP DIV
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX )
		sb->safePrintf("</div>");

	// send it off
	sendReply ( st , st->m_sb.getBufStart() );

	return true;
}


static bool printLeftNavColumn ( SafeBuf &sb, State0 *st ) {

	SearchInput *si = &st->m_si;
	CollectionRec *cr = si->m_cr;

	char format = si->m_format;

	if ( format == FORMAT_HTML ) {
		const char *title = "Search Results";
		sb.safePrintf("<title>Gigablast - %s</title>\n",title);
		sb.safePrintf("<style><!--\n");
		sb.safePrintf("body {\n");
		sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
		sb.safePrintf("color: #000000;\n");
		sb.safePrintf("font-size: 12px;\n");
		sb.safePrintf("margin: 0px 0px;\n");
		sb.safePrintf("letter-spacing: 0.04em;\n");
		sb.safePrintf("}\n");
		sb.safePrintf("a {text-decoration:none;}\n");
		sb.safePrintf(".bold {font-weight: bold;}\n");
		sb.safePrintf(".bluetable {background:#d1e1ff;"
			      "margin-bottom:15px;font-size:12px;}\n");
		sb.safePrintf(".url {color:#008000;}\n");
		sb.safePrintf(".cached, .cached a {font-size: 10px;"
			      "color: #666666;\n");
		sb.safePrintf("}\n");
		sb.safePrintf("table {\n");
		sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
		sb.safePrintf("color: #000000;\n");
		sb.safePrintf("font-size: 12px;\n");
		sb.safePrintf("}\n");
		sb.safePrintf(".directory {font-size: 16px;}\n"
			      ".nav {font-size:20px;align:right;}\n"
			      );
		sb.safePrintf("-->\n");
		sb.safePrintf("</style>\n");
		sb.safePrintf("\n");
		sb.safePrintf("</head>\n");
		sb.safePrintf("<script>\n");
		sb.safePrintf("<!--\n");
		sb.safePrintf("var openmenu=''; var inmenuclick=0;");
		sb.safePrintf("function x(){document.f.q.focus();}\n");
		sb.safePrintf("// --></script>\n");
		sb.safePrintf("<body "

			      "onmousedown=\""

			      "if (openmenu != '' && inmenuclick==0) {"
			      "document.getElementById(openmenu)."
			      "style.display='none'; openmenu='';"
			      "}"

			      "inmenuclick=0;"
			      "\" "

			      "onload=\"x()\">\n");

		//
		// DIVIDE INTO TWO PANES, LEFT COLUMN and MAIN COLUMN
		//
		sb.safePrintf("<TABLE border=0 height=100%% cellpadding=0 "
			      "cellspacing=0>"
			      "\n<TR>\n");

		//
		// first the nav column
		//

		// . also prints <TD>...</TD>. true=isSearchresults
		// . tabName = "search"
		printLeftColumnRocketAndTabs ( &sb , true , cr , "search" );

		//
		// now the MAIN column
		//
		sb.safePrintf("\n</TD>"
			      "<TD valign=top style=padding-left:30px;>\n");
	}

	return true;
}

static bool printIgnoredWords ( SafeBuf *sb , const SearchInput *si ) {
	// mention ignored query terms
	// we need to set another Query with "keepAllSingles" set to false
	const Query *qq2 = &si->m_q;
	bool firstIgnored = true;
	for ( int32_t i = 0 ; i < qq2->m_numWords ; i++ ) {
		//if ( si->m_xml ) break;
		QueryWord *qw = &qq2->m_qwords[i];
		// only print out words ignored cuz they were stop words
		if ( qw->m_ignoreWord != IGNORE_QSTOP ) continue;
		// print header -- we got one
		if ( firstIgnored ) {
			if ( si->m_format == FORMAT_XML )
				sb->safePrintf ("\t<ignoredWords><![CDATA[");
			else if ( si->m_format == FORMAT_JSON )
				sb->safePrintf ("\t\"ignoredWords\":\"");
			else if ( si->m_format == FORMAT_HTML )
				sb->safePrintf ("<br><font "
					       "color=\"#707070\">The "
					       "following query words "
					       "were ignored: "
					       "<b>");
			firstIgnored = false;
		}
		// print the word
		char *t    = qw->m_word; 
		int32_t  tlen = qw->m_wordLen;
		sb->utf8Encode2 ( t , tlen );
		sb->safePrintf (" ");
	}
	// print tail if we had ignored terms
	if ( ! firstIgnored ) {
		sb->incrementLength(-1);
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("]]></ignoredWords>\n");
		else if ( si->m_format == FORMAT_JSON )
			sb->safePrintf("\",\n");
		else if ( si->m_format == FORMAT_HTML )
			sb->safePrintf ("</b>. Preceed each with a '+' or "
				       "wrap in "
				       "quotes to not ignore.</font>");
	}
	return true;
}

bool printSearchResultsHeader ( State0 *st ) {

	SearchInput *si = &st->m_si;

	// grab the query
	Msg40 *msg40 = &(st->m_msg40);
	const char *q    = msg40->getQuery();
	int32_t     qlen = msg40->getQueryLen();

  	//char  local[ 128000 ];
	//SafeBuf sb(local, 128000);
	SafeBuf *sb = &st->m_sb;
	// reserve 1.5MB now!
	if ( ! sb->reserve(1500000 ,"pgresbuf" ) ) // 128000) )
		return false;
	// just in case it is empty, make it null terminated
	sb->nullTerm();

	// print first [ for json
	if ( si->m_format == FORMAT_JSON ) {
		if ( st->m_header ) sb->safePrintf("{\n");
		// this is just for diffbot really...
		else                sb->safePrintf("[\n");
	}

	CollectionRec *cr = si->m_cr;
	HttpRequest *hr = &st->m_hr;

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	const char *method = "GET";
	if ( si->m_sites && strlen(si->m_sites)>800 ) method = "POST";


	if ( si->m_format == FORMAT_HTML &&
	     cr->m_htmlHead.length() ) {
		return expandHtml ( *sb ,
				    cr->m_htmlHead.getBufStart(),
				    cr->m_htmlHead.length(),
				    q,
				    qlen,
				    hr,
				    si,
				    method,
				    cr);
	}
				 

	if ( si->m_format == FORMAT_HTML ) {
		printCSSHead ( sb ,si->m_format );
		sb->safePrintf("<body>");
	}

	if ( si->m_format==FORMAT_WIDGET_IFRAME ) {
		printCSSHead ( sb ,si->m_format );
		sb->safePrintf("<body style=padding:0px;margin:0px;>");

		int32_t refresh = hr->getLong("refresh",0);
		if ( refresh )
			sb->safePrintf("<meta http-equiv=\"refresh\" "
				       "content=%" PRId32">",refresh);
	}

	// lead with user's widget header which usually has custom style tags
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		const char *header = hr->getString("header",NULL);
		if ( header ) sb->safeStrcpy ( header );
	}

	// if we are xml/json we call this below otherwise we lose
	// the header of <?xml...> or whatever
	if ( si->m_format == FORMAT_HTML ) {
		printLeftNavColumn ( *sb,st );

		printLogoAndSearchBox ( sb, &st->m_hr, si );
	}

	// the calling function checked this so it should be non-null
	const char *coll = cr->m_coll;
	int32_t collLen = strlen(coll);

	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		const char *pos = "relative";
		if ( si->m_format == FORMAT_WIDGET_IFRAME ) pos = "absolute";
		int32_t widgetwidth = hr->getLong("widgetwidth",150);
		int32_t widgetHeight = hr->getLong("widgetheight",400);
		//int32_t iconWidth = 25;

		// put image in this div which will have top:0px JUST like
		// the div holding the search results we print out below
		// so that the image does not scroll when you use the
		// scrollbar. holds the magifying glass img and searchbox.
		sb->safePrintf("<div class=magglassdiv "
			       "style=\"position:absolute;"
			       "right:15px;"
			       "z-index:10;"
			       "top:0px;\">");

		//int32_t refresh = hr->getLong("refresh",15);
		const char *oq = hr->getString("q",NULL);
		if ( ! oq ) oq = "";
		const char *prepend = hr->getString("prepend");
		if ( ! prepend ) prepend = "";
		const char *displayStr = "none";
		if ( prepend[0] ) displayStr = "";
		// to do a search we need to re-call the ajax,
		// just call reload like the one that is called every 15s or so
		sb->safePrintf("<form "//method=get action=/search "
			       // use "1" as arg to force reload
			       "onsubmit=\"widget123_reload(1);"

			       // let user know we are loading
			       "var w=document.getElementById("
			       "'widget123_scrolldiv');"
			       // just set the widget content to the reply
			       "if (w) "
			       "w.innerHTML='<br><br><b>Loading Results..."
			       "</b>';"

			       // prevent it from actually submitting
			       "return false;\">");

		sb->safePrintf("<img "
			       "style=\""
			       //"position:absolute;" // absolute or relative?
			       // put it on TOP of the other stuff
			       "z-index:10;"
			       "margin-top:3px;"
			       //"right:10px;"
			       //"right:2px;"
			       //"width:%" PRId32"px;"
			       // so we are to the right of the searchbox
			       "float:right;"
			       "\" "
			       "onclick=\""
			       "var e=document.getElementById('sbox');"
			       "if(e.style.display == 'none') {"
			       "e.style.display = '';"
			       // give it focus
			       "var qb=document.getElementById('qbox');"
			       "qb.focus();"
			       "} else {"
			       "e.style.display = 'none';"
			       "}"
			       "\" " // end function
			       " "
			       "width=35 "
			       "height=31 "
			       "src=\"/magglass.png\">"
			       );

		//char *origq = hr->getString("q");
		// we sort all results by spider date now so PREPEND
		// the actual user query 
		const char *origq = hr->getString("prepend");
		if ( ! origq ) origq = "";
		sb->safePrintf("<div id=sbox style=\"float:left;"
			       "display:%s;"
			       "opacity:0.83;"
			       //"background-color:gray;"
			       //"padding:5px;"
			       "\">"
			       // the box that holds the query
			       "<input type=text id=qbox name=qbox "
			       "size=%" PRId32" " //name=prepend "
			       "value=\"%s\"  "
			       "style=\"z-index:10;"
			       "font-weight:bold;"
			       "font-size:18px;"
			       "border:4px solid black;"
			       "margin:3px;"
			       "\">"
			       , displayStr
			       , widgetwidth / 23 
			       , origq
			       );
		sb->safePrintf("</div>"
			       "</form>\n"
			       );

		// . BEGIN SERP DIV
		// . div to hold the search results
		// . this will have the scrollbar to just scroll the serps
		//   and not the magnifying glass
		sb->safePrintf("</div>"
			       "<div id=widget123_scrolldiv "
			       "onscroll=widget123_append(); "
			       "style=\"position:absolute;"
			       "top:0px;"
			       "overflow-y:auto;"
			       "overflow-x:hidden;"
			       "width:%" PRId32"px;"
			       "height:%" PRId32"px;\">"
			       , widgetwidth
			       , widgetHeight);
	}

	// xml
	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"UTF-8\" ?>\n"
			      "<response>\n" );

	int64_t nowMS = gettimeofdayInMillisecondsLocal();

	// show current time
	if ( si->m_format == FORMAT_XML ) {
		int64_t globalNowMS = localToGlobalTimeMilliseconds(nowMS);
		sb->safePrintf("\t<currentTimeUTC>%" PRIu32"</currentTimeUTC>\n",
			      (uint32_t)(globalNowMS/1000));
	} 
	else if ( st->m_header && si->m_format == FORMAT_JSON ) {
	    int64_t globalNowMS = localToGlobalTimeMilliseconds(nowMS);
	    sb->safePrintf("\"currentTimeUTC\":%" PRIu32",\n",
			   (uint32_t)(globalNowMS/1000));
	}

	// show result validity time
	if ( si->m_format == FORMAT_XML ) {
		int32_t expireTimeUTC = nowMS/1000 + g_conf.m_defaultQueryResultsValidityTime;
		sb->safePrintf("\t<expireTimeUTC>%" PRIu32"</expireTimeUTC>\n",
			      expireTimeUTC);
	} else if ( st->m_header && si->m_format == FORMAT_JSON ) {
		int32_t expireTimeUTC = nowMS/1000 + g_conf.m_defaultQueryResultsValidityTime;
		sb->safePrintf("\"expireTimeUTC\":%" PRIu32",\n",
			   expireTimeUTC);
	}

	// show response time if not doing Quality Assurance
	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t<responseTimeMS>%" PRId64"</responseTimeMS>\n",
			      st->m_took);
	else if ( st->m_header && si->m_format == FORMAT_JSON )
	    sb->safePrintf("\"responseTimeMS\":%" PRId64",\n", st->m_took);

	// out of memory allocating msg20s?
	if ( st->m_errno ) {
		log("query: Query failed. Had error processing query: %s",
		    mstrerror(st->m_errno));
		g_errno = st->m_errno;
		//return sendReply(st,sb->getBufStart());
		return false;
	}


	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t<numResultsOmitted>%" PRId32
			       "</numResultsOmitted>\n",
			       msg40->m_omitCount);
		sb->safePrintf("\t<numShardsSkipped>%" PRId32"</numShardsSkipped>\n",
			       msg40->m_msg3a.m_skippedShards);
		sb->safePrintf("\t<totalShards>%" PRId32"</totalShards>\n",
			       g_hostdb.m_numShards );
		sb->safePrintf("\t<pctSearched>%f</pctSearched>\n",
			       msg40->m_msg3a.m_pctSearched);
	}

	if ( st->m_header && si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\"numResultsOmitted\":%" PRId32",\n",
			       msg40->m_omitCount);
		sb->safePrintf("\"numShardsSkipped\":%" PRId32",\n",
			       msg40->m_msg3a.m_skippedShards);
		sb->safePrintf("\"totalShards\":%" PRId32",\n",
			       g_hostdb.m_numShards );
		sb->safePrintf("\"pctSearched\":%f,\n",
			       msg40->m_msg3a.m_pctSearched);
	}

	// save how many docs are in this collection
	int64_t docsInColl = -1;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB , st->m_collnum );

	// estimate it
	if ( base ) {
		docsInColl = base->getNumGlobalRecs();
	}

	// include number of docs in the collection corpus
	if ( docsInColl >= 0LL ) {
		if ( si->m_format == FORMAT_XML) {
			sb->safePrintf ( "\t<docsInCollection>%" PRId64"</docsInCollection>\n", docsInColl );
		} else if ( st->m_header && si->m_format == FORMAT_JSON) {
			sb->safePrintf("\"docsInCollection\":%" PRId64",\n", docsInColl);
		}
	}

 	int32_t numResults = msg40->getNumResults();
	bool moreFollow = msg40->moreResultsFollow();
	// an estimate of the # of total hits
	int64_t totalHits = msg40->getNumTotalHits();
	// only adjust upwards for first page now so it doesn't keep chaning
	if ( totalHits < numResults ) totalHits = numResults;

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t<hits>%" PRId64"</hits>\n",(int64_t)totalHits);
	else if ( st->m_header && si->m_format == FORMAT_JSON )
		sb->safePrintf("\"hits\":%" PRId64",\n", (int64_t)totalHits);

	// if streaming results we just don't know if we will require
	// a "Next 10" link or not! we can print that after we print out
	// the results i guess...
	if ( ! si->m_streamResults ) {
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t<moreResultsFollow>%" PRId32
				       "</moreResultsFollow>\n"
				       ,(int32_t)moreFollow);
		else if ( st->m_header && si->m_format == FORMAT_JSON )
			sb->safePrintf("\"moreResultsFollow\":%" PRId32",\n",
				       (int32_t)moreFollow);
	}

	// . did he get a spelling recommendation?
	// . do not use htmlEncode() on this anymore since receiver
	//   of the XML feed usually does not want that.
	if ( si->m_format == FORMAT_XML && st->m_spell[0] ) {
		sb->safePrintf ("\t<spell><![CDATA[");
		sb->safeStrcpy(st->m_spell);
		sb->safePrintf ("]]></spell>\n");
	}

	if ( si->m_format == FORMAT_JSON && st->m_spell[0] ) {
		sb->safePrintf ("\t\"spell\":\"");
		sb->jsonEncode(st->m_spell);
		sb->safePrintf ("\"\n,");
	}

	// print individual query term info
	if ( si->m_format == FORMAT_XML ) {
		Query *q = &si->m_q;
		sb->safePrintf("\t<queryInfo>\n");
		sb->safePrintf("\t\t<fullQuery><![CDATA[");
		cdataEncode(sb, q->m_orig);
		sb->safePrintf("]]></fullQuery>\n");
		sb->safePrintf("\t\t<queryLanguageAbbr>"
			       "<![CDATA[%s]]>"
			       "</queryLanguageAbbr>\n"
			       , getLanguageAbbr(si->m_queryLangId) );
		sb->safePrintf("\t\t<queryLanguage>"
			       "<![CDATA[%s]]>"
			       "</queryLanguage>\n"
			       , getLanguageString(si->m_queryLangId) );
		// print query words we ignored, like stop words
		printIgnoredWords ( sb , si );

		sb->safePrintf("\t\t<queryNumTermsTotal>"
			       "%" PRId32
			       "</queryNumTermsTotal>\n"
			       , q->m_numTermsUntruncated );
		sb->safePrintf("\t\t<queryNumTermsUsed>"
			       "%" PRId32
			       "</queryNumTermsUsed>\n"
			       , q->m_numTerms );
		int32_t tval = 0;
		if ( q->m_numTerms < q->m_numTermsUntruncated ) tval = 1;
		sb->safePrintf("\t\t<queryWasTruncated>"
			       "%" PRId32
			       "</queryWasTruncated>\n"
			       , tval );

		for ( int i = 0 ; i < q->m_numTerms ; i++ ) {
			sb->safePrintf("\t\t<term>\n");
			QueryTerm *qt = &q->m_qterms[i];
			sb->safePrintf("\t\t\t<termNum>%i</termNum>\n",i);
			char *term = qt->m_term;
			char c = term[qt->m_termLen];
			term[qt->m_termLen] = '\0';
			sb->safePrintf("\t\t\t<termStr><![CDATA[");
			const char *printTerm = qt->m_term;
			if ( is_wspace_a(term[0])) printTerm++;
			cdataEncode(sb, printTerm);
			sb->safePrintf("]]>"
				       "</termStr>\n");
			term[qt->m_termLen] = c;
			// syn?
			QueryTerm *sq = qt->m_synonymOf;
			// what language did synonym come from?
			if ( sq ) {
				// language map from wiktionary
				sb->safePrintf("\t\t\t<termLang>"
					       "<![CDATA[");
				bool first = true;
				for ( int i = 0 ; i < langLast ; i++ ) {
					uint64_t bit = (uint64_t)1 << i;
					if ( ! (qt->m_langIdBits&bit))continue;
					const char *str = getLanguageAbbr(i);
					if ( ! first ) sb->pushChar(',');
					first = false;
					sb->safeStrcpy ( str );
				}
				sb->safePrintf("]]></termLang>\n");
			}

			if ( sq ) {
				char *term = sq->m_term;
				char c = term[sq->m_termLen];
				term[sq->m_termLen] = '\0';
				const char *printTerm = term;
				if ( is_wspace_a(term[0])) printTerm++;
				sb->safePrintf("\t\t\t<synonymOf>"
					       "<![CDATA[%s]]>"
					       "</synonymOf>\n"
					       ,printTerm);
				term[sq->m_termLen] = c;
			}				
			//int64_t tf = msg40->m_msg3a.m_termFreqs[i];
			int64_t tf = qt->m_termFreq;
			sb->safePrintf("\t\t\t<termFreq>%" PRId64"</termFreq>\n"
				       ,tf);
			sb->safePrintf("\t\t\t<termHash48>%" PRId64"</termHash48>\n"
				       ,qt->m_termId);
			sb->safePrintf("\t\t\t<termHash64>%" PRIu64"</termHash64>\n"
				       ,qt->m_rawTermId);
			QueryWord *qw = qt->m_qword;
			sb->safePrintf("\t\t\t<prefixHash64>%" PRIu64"</prefixHash64>\n"
				       ,qw->m_prefixHash);
			sb->safePrintf("\t\t</term>\n");
		}
		sb->safePrintf("\t</queryInfo>\n");
	}			

	// print individual query term info
	if ( si->m_format == FORMAT_JSON && st->m_header ) {
		Query *q = &si->m_q;
		sb->safePrintf("\"queryInfo\":{\n");
		sb->safePrintf("\t\"fullQuery\":\"");
		sb->jsonEncode(q->m_orig);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\"queryLanguageAbbr\":\"");
		sb->jsonEncode ( getLanguageAbbr(si->m_queryLangId) );
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\"queryLanguage\":\"");
		sb->jsonEncode ( getLanguageString(si->m_queryLangId) );
		sb->safePrintf("\",\n");
		// print query words we ignored, like stop words
		printIgnoredWords ( sb , si );

		sb->safePrintf("\t\"queryNumTermsTotal\":"
			       "%" PRId32",\n"
			       , q->m_numTermsUntruncated );
		sb->safePrintf("\t\"queryNumTermsUsed\":"
			       "%" PRId32",\n"
			       , q->m_numTerms );
		int32_t tval = 0;
		if ( q->m_numTerms < q->m_numTermsUntruncated ) tval = 1;
		sb->safePrintf("\t\"queryWasTruncated\":"
			       "%" PRId32",\n"
			       , tval );
			
		sb->safePrintf("\t\"terms\":[\n");
		for ( int i = 0 ; i < q->m_numTerms ; i++ ) {
			sb->safePrintf("\t\t{\n");
			QueryTerm *qt = &q->m_qterms[i];
			sb->safePrintf("\t\t\"termNum\":%i,\n",i);
			char *term = qt->m_term;
			char c = term[qt->m_termLen];
			term[qt->m_termLen] = '\0';
			sb->safePrintf("\t\t\"termStr\":\"");
			sb->jsonEncode (qt->m_term);
			sb->safePrintf("\",\n");
			term[qt->m_termLen] = c;
			// syn?
			QueryTerm *sq = qt->m_synonymOf;
			// what language did synonym come from?
			if ( sq ) {
				// language map from wiktionary
				sb->safePrintf("\t\t\"termLang\":\"");
				bool first = true;
				for ( int i = 0 ; i < langLast ; i++ ) {
					uint64_t bit = (uint64_t)1 << i;
					if ( ! (qt->m_langIdBits&bit))continue;
					const char *str = getLanguageAbbr(i);
					if ( ! first ) sb->pushChar(',');
					first = false;
					sb->jsonEncode ( str );
				}
				sb->safePrintf("\",\n");
			}

			if ( sq ) {
				char *term = sq->m_term;
				char c = term[sq->m_termLen];
				term[sq->m_termLen] = '\0';
				sb->safePrintf("\t\t\"synonymOf\":\"");
				sb->jsonEncode(sq->m_term);
				sb->safePrintf("\",\n");
				term[sq->m_termLen] = c;
			}				
			//int64_t tf = msg40->m_msg3a.m_termFreqs[i];
			int64_t tf = qt->m_termFreq;
			sb->safePrintf("\t\t\"termFreq\":%" PRId64",\n"
				       ,tf);

			sb->safePrintf("\t\t\"termHash48\":%" PRId64",\n"
				       ,qt->m_termId);
			sb->safePrintf("\t\t\"termHash64\":%" PRIu64",\n"
				       ,qt->m_rawTermId);

			// don't end last query term attr on a omma
			QueryWord *qw = qt->m_qword;
			sb->safePrintf("\t\t\"prefixHash64\":%" PRIu64"\n"
				       ,qw->m_prefixHash);

			sb->safePrintf("\t}");
			if ( i + 1 < q->m_numTerms )
				sb->pushChar(',');
			sb->pushChar('\n');
		}
		sb->safePrintf("\t]\n"); // end "terms":[]
		sb->safePrintf("},\n");
	}

	if ( si->m_format == FORMAT_JSON && st->m_header ) {
		sb->safePrintf("\"results\":[\n");
		return true;
	}

	// debug
	if ( si->m_debug ) {
		logf(LOG_DEBUG,"query: Displaying up to %" PRId32" results.", numResults);
	}

	// get some result info from msg40
	int32_t firstNum   = msg40->getFirstResultNum() ;

	// numResults may be more than we requested now!
	int32_t n = msg40->getDocsWanted();
	if ( n > numResults )  n = numResults;

	// . make the query class here for highlighting
	// . keepAllSingles means to convert all individual words into
	//   QueryTerms even if they're in quotes or in a connection (cd-rom).
	//   we use this for highlighting purposes
	Query qq;
	qq.set2 ( si->m_displayQuery, langUnknown , si->m_queryExpansion );

	if ( g_errno ) return false;//sendReply (st,NULL);

	DocIdScore *dpx = NULL;
	if ( numResults > 0 ) dpx = msg40->getScoreInfo(0);

	if ( si->m_format == FORMAT_XML && dpx ) {
		float max = 0.0;

		// max pairwise
		float lw = getHashGroupWeight(HASHGROUP_INLINKTEXT);

		// square that location weight
		lw *= lw;

		// assume its an inlinker's text, who has rank 15!!!
		lw *= getLinkerWeight(MAXSITERANK);

		// single weights
		float maxtfw1 = 0.0;
		int32_t maxi1;

		// now we can have multiple SingleScores for the same term!
		// because we take the top MAX_TOP now and add them to
		// get the term's final score.
		for ( int32_t i = 0 ; i< dpx->m_numSingles ; i++ ) {
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = ssi->m_tfWeight;
			if ( tfwi <= maxtfw1 ) continue;
			maxtfw1 = tfwi;
			maxi1 = i;
		}
		float maxtfw2 = 0.0;
		int32_t maxi2;
		for ( int32_t i = 0 ; i< dpx->m_numSingles ; i++ ) {
			if ( i == maxi1 ) continue;
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = ssi->m_tfWeight;
			if ( tfwi <= maxtfw2 ) continue;
			maxtfw2 = tfwi;
			maxi2 = i;
		}
		// only 1 term?
		if ( almostEqualFloat(maxtfw2, 0.0) ) maxtfw2 = maxtfw1;
		// best term freqs
		max *= maxtfw1 * maxtfw2;
		// site rank effect
		max *= MAXSITERANK/SITERANKDIVISOR + 1;
		sb->safePrintf ("\t\t<theoreticalMaxFinalScore>%f"
			       "</theoreticalMaxFinalScore>\n",
			       max );
	}
	


	// debug msg
	log ( LOG_TIMING ,
	     "query: Got %" PRId32" search results in %" PRId64" ms for q=%s",
	      numResults,gettimeofdayInMilliseconds()-st->m_startTime,
	      qq.getQuery());

	st->m_qesb.nullTerm();

	// encode query buf
	const char *dq    = si->m_displayQuery;
	if ( dq ) {
		urlEncode(&st->m_qesb,dq);
	}

	// print it with commas into "thbuf" and null terminate it
	char thbuf[64];
	ulltoa ( thbuf , totalHits );

	char inbuf[128];
	ulltoa ( inbuf , docsInColl );

	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format != FORMAT_HTML ) isAdmin = false;

	// otherwise, we had no error
	if ( numResults == 0 && si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( "No results found in <b>%s</b> collection.",
				cr->m_coll);
	}
	// the token is currently in the collection name so do not show that
	else if ( numResults == 0 && 
		  ( si->m_format == FORMAT_WIDGET_IFRAME ||
		    si->m_format == FORMAT_WIDGET_AJAX ) ) {
		sb->safePrintf ( "No results found. Wait for spider to "
				 "kick in.");
	}
	else if ( moreFollow && si->m_format == FORMAT_HTML ) {
		sb->safePrintf ("Results <b>%" PRId32"</b> to <b>%" PRId32"</b> of "
			       "exactly <b>%s</b> from an index "
			       "of about %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}
	// otherwise, we didn't get enough results to show this page
	else if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ("Results <b>%" PRId32"</b> to <b>%" PRId32"</b> of "
			       "exactly <b>%s</b> from an index "
			       "of about %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}

	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf(" in %.02f seconds",((float)st->m_took)/1000.0);


	//
	// if query was a url print add url msg
	//
	const char *url = NULL;
	if ( !strncmp(q,"url:"    ,4) && qlen > 4 ) url = q+4;
	if ( !strncmp(q,"http://" ,7) && qlen > 7 ) url = q;
	if ( !strncmp(q,"https://",8) && qlen > 8 ) url = q;
	if ( !strncmp(q,"www."    ,4) && qlen > 4 ) url = q;
	// find end of url
	const char *ue = url;
	for ( ; ue && *ue && ! is_wspace_a(*ue) ; ue++ ) ;
	if ( numResults == 0 && si->m_format == FORMAT_HTML && url ) {
		sb->safePrintf("<br><br>"
			      "Could not find that url in the "
			      "index. Try <a href=/addurl?u=");
		urlEncode(sb,url,ue-url,false,false);
		sb->safePrintf(">Adding it.</a>");
	}

	// sometimes ppl search for "www.whatever.com" so ask them if they
	// want to search for url:www.whatever.com
	if ( numResults > 0  && si->m_format == FORMAT_HTML && url && url == q){
		sb->safePrintf("<br><br>"
			      "Did you mean to "
			      "search for the url "
			      "<a href=/search?q=url%%3A");
		urlEncode(sb,url,ue-url,false,false);
		sb->safePrintf(">");
		sb->safeMemcpy(url,ue-url);
		sb->safePrintf("</a> itself?");
	}


	// is it the main collection?
	bool isMain = false;
	if ( collLen == 4 && strncmp ( coll, "main", 4) == 0 ) isMain = true;

	// print "in collection ***" if we had a collection
	if (collLen>0 && numResults>0 && !isMain && si->m_format==FORMAT_HTML )
		sb->safePrintf (" in collection <b>%s</b>",coll);


	printIgnoredWords ( sb , si );


	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf("<br><br>");

		sb->safePrintf("<table cellpadding=0 cellspacing=0>"
			      "<tr><td valign=top>");
	}

	// two pane table
	//if ( si->m_format == FORMAT_HTML ) 
	//	sb->safePrintf("</td><td valign=top>");

	// did we get a spelling recommendation?
	if ( si->m_format == FORMAT_HTML && st->m_spell[0] ) {
		// encode the spelling recommendation
		int32_t len = strlen ( st->m_spell );
		char qe2[MAX_FRAG_SIZE];
		urlEncode(qe2, MAX_FRAG_SIZE, st->m_spell, len);
		sb->safePrintf ("<font size=+0 color=\"#c62939\">Did you mean:"
			       "</font> <font size=+0>"
			       "<a href=\"/search?q=%s",
			       qe2 );
		// close it up
		sb->safePrintf ("\"><i><b>");
		sb->utf8Encode2(st->m_spell, len);
		// then finish it off
		sb->safePrintf ("</b></i></a></font>\n<br><br>\n");
	}

	// . Wrap results in a table if we are using ads. Easier to display.
	//Ads *ads = &st->m_ads;
	//if ( ads->hasAds() )
        //        sb->safePrintf("<table width=\"100%%\">\n"
        //                    "<tr><td style=\"vertical-align:top;\">\n");

	// debug
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: Printing up to %" PRId32" results. "
		     "bufStart=0x%" PTRFMT"",
		     numResults,
		     (PTRTYPE)sb->getBufPtr());

	return true;
}


bool printSearchResultsTail ( State0 *st ) {
	SafeBuf *sb = &st->m_sb;

	SearchInput *si = &st->m_si;	

	Msg40 *msg40 = &(st->m_msg40);

	CollectionRec *cr = si->m_cr;
	const char *coll = cr->m_coll;

	if ( si->m_format == FORMAT_JSON ) {	
		// remove last },\n if there and replace with just \n
		const char *e = sb->getBufPtr() - 2;
		if ( sb->length()>=2 &&
		     e[0]==',' && e[1]=='\n') {
			sb->m_length -= 2;
			sb->safePrintf("\n");
		}
		// print ending ] for json search results
		sb->safePrintf("]\n");

		if ( st->m_header ) sb->safePrintf("}\n");

		//////////////////////
		// for some reason if we take too long to write out this
		// tail we get a SIGPIPE on a firefox browser.
		//////////////////////

		// all done for json
		return true;
	}

	// grab the query
	const char  *q    = msg40->getQuery();
	int32_t   qlen = msg40->getQueryLen();

	HttpRequest *hr = &st->m_hr;

	// get some result info from msg40
	int32_t firstNum   = msg40->getFirstResultNum() ;

	// end the two-pane table
	if ( si->m_format == FORMAT_HTML) sb->safePrintf("</td></tr></table>");

	// for storing a list of all of the sites we displayed, now we print a 
	// link at the bottom of the page to ban all of the sites displayed 
	// with one click
	SafeBuf banSites;

	//
	// PRINT PREV 10 NEXT 10 links!
	// 

	// center everything below here
	if ( si->m_format == FORMAT_HTML ) sb->safePrintf ( "<br><center>" );

	int32_t remember = sb->length();

	// now print "Prev X Results" if we need to
	if ( firstNum < 0 ) firstNum = 0;

	char abuf[300];
	SafeBuf args(abuf,300);
	// show banned?
	if ( si->m_showBanned && ! si->m_isMasterAdmin )
		args.safePrintf("&sb=1");
	if ( ! si->m_showBanned && si->m_isMasterAdmin )
		args.safePrintf("&sb=0");

	// collection
	args.safePrintf("&c=%s",coll);
	// formatting info
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		args.safePrintf("&format=widget");
		int32_t widgetwidth = hr->getLong("widgetwidth",250);
		args.safePrintf("&widgetwidth=%" PRId32,widgetwidth);
	}

	// carry over the sites we are restricting the search results to
	if ( si->m_sites )
		//whiteListBuf.getBufStart());
		args.safePrintf("&sites=%s",si->m_sites);


	if ( si->m_format == FORMAT_HTML &&
	     msg40->m_omitCount ) { // && firstNum == 0 ) { 
		// . add our cgi to the original url
		// . so if it has &qlang=de and they select &qlang=en
		//   we have to replace it... etc.
		StackBuf(newUrl);
		// show banned results
		replaceParm2 ("sb=1",
			      &newUrl,
			      hr->m_origUrlRequest,
			      hr->m_origUrlRequestLen );
		// no deduping by summary or content hash etc.
		StackBuf(newUrl2);
		replaceParm2("dr=0",&newUrl2,newUrl.getBufStart(),
			     newUrl.length());
		// and no site clustering
		StackBuf( newUrl3 );
		replaceParm2 ( "sc=0", &newUrl3 , newUrl2.getBufStart(),
			     newUrl2.length());
		// start at results #0 again
		StackBuf( newUrl4 );
		replaceParm2 ( "s=0", &newUrl4 , newUrl3.getBufStart(),
			     newUrl3.length());
		// show errors
		StackBuf( newUrl5 );
		replaceParm2 ( "showerrors=1", 
			       &newUrl5 , 
			       newUrl4.getBufStart(),
			       newUrl4.length());
		
		
		sb->safePrintf("<center>"
			       "<i>"
			       "%" PRId32" results were omitted because they "
			       "were considered duplicates, banned, errors "
			       "<br>"
			       "or "
			       "from the same site as other results. "
			       "<a href=%s>Click here to show all results</a>."
			       "</i>"
			       "</center>"
			       "<br><br>"
			       , msg40->m_omitCount
			       , newUrl5.getBufStart() );
	}


	if ( firstNum > 0 && 
	     (si->m_format == FORMAT_HTML || 
	      si->m_format == FORMAT_WIDGET_IFRAME //||
	      //si->m_format == FORMAT_WIDGET_AJAX
	      ) ) {
		int32_t ss = firstNum - msg40->getDocsWanted();
		
		//sb->safePrintf("<a href=\"/search?s=%" PRId32"&q=",ss);
		// our current query parameters
		//sb->safeStrcpy ( st->m_qe );
		// print other args if not zero
		//sb->safeMemcpy ( &args );

		// make the cgi parm to add to the original url
		char nsbuf[128];
		sprintf(nsbuf,"s=%" PRId32,ss);
		// get the original url and add/replace in &s=xxx
		StackBuf ( newUrl );
		replaceParm ( nsbuf , &newUrl , hr );


		// close it up
		sb->safePrintf ("<a href=\"%s\"><b>"
			       "<font size=+0>Prev %" PRId32" Results</font>"
			       "</b></a>"
				, newUrl.getBufStart()
				, msg40->getDocsWanted() );
	}

	// now print "Next X Results"
	if ( msg40->moreResultsFollow() && 
	     (si->m_format == FORMAT_HTML || 
	      si->m_format == FORMAT_WIDGET_IFRAME 
	      //si->m_format == FORMAT_WIDGET_AJAX 
	      )) {
		int32_t ss = firstNum + msg40->getDocsWanted();
		// print a separator first if we had a prev results before us
		if ( sb->length() > remember ) sb->safePrintf ( " &nbsp; " );
		// add the query
		//sb->safePrintf ("<a href=\"/search?s=%" PRId32"&q=",ss);
		// our current query parameters
		//sb->safeStrcpy ( st->m_qe );
		// print other args if not zero
		//sb->safeMemcpy ( &args );

		// make the cgi parm to add to the original url
		char nsbuf[128];
		sprintf(nsbuf,"s=%" PRId32,ss);
		// get the original url and add/replace in &s=xxx
		StackBuf(newUrl);
		replaceParm ( nsbuf , &newUrl , hr );

		// close it up
		sb->safePrintf("<a href=\"%s\"><b>"
			      "<font size=+0>Next %" PRId32" Results</font>"
			      "</b></a>"
			       , newUrl.getBufStart()
			       , msg40->getDocsWanted() );
	}


	// print try this search on...
	// an additional <br> if we had a Next or Prev results link
	if ( sb->length() > remember &&
	     si->m_format == FORMAT_HTML ) 
		sb->safeMemcpy ("<br>" , 4 ); 

	//
	// END PRINT PREV 10 NEXT 10 links!
	// 

	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf("<input name=c type=hidden value=\"%s\">",coll);
	}

	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format != FORMAT_HTML ) isAdmin = false;

	if ( isAdmin && banSites.length() > 0 )
		sb->safePrintf ("<br><br><div align=right><b>"
			       "<a style=color:green; href=\"/admin/tagdb?"
			       //"tagid0=%" PRId32"&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s&uenc=1&u=%s\">"
			       "[ban all of these domains]</a></b></div>"
			       "<br>\n ", 
			       coll, banSites.getBufStart());


	// TODO: print cache line in light gray here
	// TODO: "these results were cached X minutes ago"
	if ( msg40->getCachedTime() > 0 && si->m_format == FORMAT_HTML ) {
		sb->safePrintf("<br><br><font size=1 color=707070>"
			       "<b><center>");
		sb->safePrintf ( " These results were cached " );
		// this cached time is this local cpu's time
		int32_t diff = getTime() - msg40->getCachedTime();
		if      ( diff < 60   ) sb->safePrintf ("%" PRId32" seconds", diff );
		else if ( diff < 2*60 ) sb->safePrintf ("1 minute");
		else                    sb->safePrintf ("%" PRId32" minutes",diff/60);
		sb->safePrintf ( " ago. [<a href=\"/pageCache.html\">"
				"<font color=707070>Info</font></a>]");
		sb->safePrintf ( "</center></font>");
	}

	

	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("</response>\n");
	}

	if ( si->m_format == FORMAT_HTML && 
	     cr->m_htmlTail.length() == 0 ) {
		sb->safePrintf ( "<br>"
				 "<center>"
				 "<font color=gray>"
				 "Copyright &copy; 2014. All Rights "
				 "Reserved.<br/>"
				 "Powered by the <a href=\"http://www."
				 "gigablast.com/\">GigaBlast</a> open source "
				 "search engine."
				 "</font>"
				 "</center>\n"
				 "<br>\n"
				 );
	}


	// if we did not use ajax, print this tail here now
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf( "</body>\n"
				"</html>\n"
				);
	}

	// ajax widgets will have this outside the downloaded content
	if ( si->m_format == FORMAT_WIDGET_IFRAME ) {
		sb->safePrintf ( "<br>"
				"<center>"
				"<font color=gray>"
				 // link to edit the list of widget sites
				 // or various other widget content properties
				 // because we can't edit the width/height
				 // of the widget like this.
				 "<a href=/widget?inlineedit=1>edit</a> "
				 "&bull; "
				 //"Copyright &copy; 2014. All Rights "
				 //"Reserved.<br/>"
				"Powered by <a href=http://www.diffbot.com>"
				 "Diffbot</a>."
				"</font>"
				"</center>\n"
				"</body>\n"
				"</html>\n"
				 );
	}

	if ( sb->length() == 0 && si->m_format == FORMAT_JSON )
		sb->safePrintf("[]\n");

	if ( sb->length() == 0 ) {
		sb->pushChar('\n');
		sb->nullTerm();
	}

	if ( si->m_format == FORMAT_HTML &&
	     cr->m_htmlTail.length() &&
	     ! expandHtml ( *sb ,
			    cr->m_htmlTail.getBufStart(),
			    cr->m_htmlTail.length(),
			    q,
			    qlen,
			    hr,
			    si,
			    NULL, // method,
			    cr) )
			return false;

	return true;
}

static bool printTimeAgo ( SafeBuf *sb, time_t ts , const char *prefix , SearchInput *si ) {
	// Jul 23, 1971
	sb->reserve2x(200);
	int32_t now = getTimeGlobal();
	// for printing
	int32_t mins = 1000;
	int32_t hrs  = 1000;
	int32_t days = 1000;
	if ( ts > 0 ) {
		mins = (int32_t)((now - ts)/60);
		hrs  = (int32_t)((now - ts)/3600);
		days = (int32_t)((now - ts)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	// print the time ago
	if      ( mins ==1)
		sb->safePrintf(" - %s: %" PRId32" minute ago",prefix,mins);
	else if (mins<60)
		sb->safePrintf ( " - %s: %" PRId32" minutes ago",prefix,mins);
	else if ( hrs == 1 )
		sb->safePrintf ( " - %s: %" PRId32" hour ago",prefix,hrs);
	else if ( hrs < 24 )
		sb->safePrintf ( " - %s: %" PRId32" hours ago",prefix,hrs);
	else if ( days == 1 )
		sb->safePrintf ( " - %s: %" PRId32" day ago",prefix,days);
	else if (days< 7 )
		sb->safePrintf ( " - %s: %" PRId32" days ago",prefix,days);
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible
	else if ( ts > 0 ) { // && si->m_isMasterAdmin ) {
		struct tm tm_buf;
		struct tm *timeStruct = localtime_r(&ts,&tm_buf);
		sb->safePrintf(" - %s: ",prefix);
		char tmp[100];
		strftime(tmp,100,"%b %d %Y",timeStruct);
		sb->safeStrcpy(tmp);
	}
	return true;
}

static int linkSiteRankCmp (const void *v1, const void *v2) {
	Inlink *i1 = *(Inlink **)v1;
	Inlink *i2 = *(Inlink **)v2;
	return i2->m_siteRank - i1->m_siteRank;
}

static bool printInlinkText ( SafeBuf *sb , Msg20Reply *mr , SearchInput *si ,
		       int32_t *numPrinted ) {
	*numPrinted = 0;
	// . show the "LinkInfo"
	// . Msg20.cpp will have "computed" the LinkInfo if we set
	//   Msg20Request::m_computeLinkInfo to true, but if we set
	//   Msg20Request::m_getLinkInfo to true it will just get it
	//   from the TitleRec, which is much faster but more stale.
	// . "&inlinks=1" is slow and fresh, "&inlinks=2" is fast
	//   and stale. Both are really only for BuzzLogic.
	LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
	// sanity
	if ( info && mr->size_linkInfo!=info->m_lisize ){g_process.shutdownAbort(true); }
	// NULLify if empty
	if ( mr->size_linkInfo <= 0 ) info = NULL;
	// do not both if none
	if ( info && ! info->m_numStoredInlinks ) info = NULL;
	// bail?
	if ( ! info ) return true;
	// now sort them up
	Inlink *k = info->getNextInlink(NULL);
	// #define from Linkdb.h
	Inlink *ptrs[MAX_LINKERS];
	int32_t numLinks = 0;
	for ( ; k ; k = info->getNextInlink(k) ) {
		ptrs[numLinks++] = k;
		if ( numLinks >= MAX_LINKERS ) break;
	}
	// sort them
	gbsort ( ptrs , numLinks , sizeof(Inlink *) , linkSiteRankCmp );
	// print xml starter
	if ( si->m_format == FORMAT_XML ) sb->safePrintf("\t\t<inlinks>\n");
	// loop through the inlinks
	bool printedInlinkText = false;
	bool firstTime = true;
	int32_t inlinkId = 0;
	int64_t  starttime = gettimeofdayInMillisecondsLocal();

	for ( int32_t i = 0 ; i < numLinks ; i++ ) {
		k = ptrs[i];
		if ( ! k->getLinkText() ) continue;
		if ( ! si->m_doQueryHighlighting && 
		     si->m_format == FORMAT_HTML ) 
			continue;
		char *str   = k->getLinkText();//ptr_linkText;
		int32_t strLen = k->size_linkText;

		const char *frontTag =
		     "<font style=\"color:black;background-color:yellow\">" ;
		const char *backTag = "</font>";
		if ( si->m_format == FORMAT_XML ) {
			frontTag = "<b>";
			backTag  = "</b>";
		}
		if ( si->m_format == FORMAT_WIDGET_IFRAME ||
		     si->m_format == FORMAT_WIDGET_AJAX ) {
			frontTag = "<font style=\"background-color:yellow\">" ;
		}

		Highlight hi;
		SafeBuf hb;
		int32_t hlen = hi.set ( &hb, str, strLen , &si->m_hqq, frontTag, backTag );
		if ( hlen <= 0 ) {
			continue;
		}

		// skip it if nothing highlighted
		if ( hi.getNumMatches() == 0 ) continue;

		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t\t<inlink "
				      "docId=\"%" PRId64"\" "
				      "url=\"",
				      k->m_docId );
			// encode it for xml
			sb->htmlEncode ( k->getUrl(),//ptr_urlBuf,
					k->size_urlBuf - 1 , false );
			sb->safePrintf("\" "
				      //"hostId=\"%" PRIu32"\" "
				      "firstindexed=\"%" PRIu32"\" "
				      // not accurate!
				      //"lastspidered=\"%" PRIu32"\" "
				      "wordposstart=\"%" PRId32"\" "
				      "id=\"%" PRId32"\" "
				      "siterank=\"%" PRId32"\" "
				      "text=\"",
				      //hh ,
				      //(int32_t)k->m_datedbDate,
				      (uint32_t)k->m_firstIndexedDate,
				      //(uint32_t)k->m_lastSpidered,
				      (int32_t)k->m_wordPosStart,
				      inlinkId,
				      //linkScore);
				      (int32_t)k->m_siteRank
				      );
			// HACK!!!
			k->m_siteHash = inlinkId;
			// inc it
			inlinkId++;
			// encode it for xml
			if ( !sb->htmlEncode ( hb.getBufStart(),
					      hb.length(),
					      false)) 
				return false;
			sb->safePrintf("\"/>\n");
			continue;
		}


		if ( firstTime ) {
			sb->safePrintf("<font size=-1>");
			sb->safePrintf("<table border=1>"
				      "<tr><td colspan=10>"
				      "<center>"
				      "<b>Inlinks with Query Terms</b>"
				      "</center>"
				      "</td></tr>"
				      "<tr>"
				      "<td>Inlink Text</td>"
				      "<td>From Site</td>"
				      "<td>Site IP</td>"
				      "<td>Site Rank</td>"
				      "</tr>"
				      );
		}
		firstTime = false;
		sb->safePrintf("<tr><td>"
			      "<a href=/get?c=%s&d=%" PRId64"&cnsp=0>"
			      //"<a href=\"/print?"
			      //"page=7&"
			      //"c=%s&"
			      //"d=%" PRId64"\">"
			      //k->getUrl());
			      ,si->m_cr->m_coll
			      ,k->m_docId);
		if ( ! sb->safeMemcpy(&hb) ) return false;
		int32_t hostLen = 0;
		const char *host = getHostFast(k->getUrl(),&hostLen,NULL);
		sb->safePrintf("</td><td>");
		if ( host ) sb->safeMemcpy(host,hostLen);
		sb->safePrintf("</td><td>");
		sb->safePrintf("<a href=/search?c=%s&q=ip%%3A%s"
			       "+gbsortbyint%%3Agbsitenuminlinks&n=100>"
			       ,si->m_cr->m_coll,iptoa(k->m_ip));
		sb->safePrintf("%s</a>",iptoa(k->m_ip));
		sb->safePrintf("</td><td>%" PRId32"</td></tr>"
			       ,(int32_t)k->m_siteRank);
		//sb->safePrintf("<br>");
		printedInlinkText = true;
		*numPrinted = *numPrinted + 1;
	}

	int64_t took = gettimeofdayInMillisecondsLocal() - starttime;
        if ( took > 2 )
                log("timing: took %" PRId64" ms to highlight %" PRId32" links."
                    ,took,numLinks);


	// closer for xml
	if ( si->m_format == FORMAT_XML ) sb->safePrintf("\t\t</inlinks>\n");
	//if ( printedInlinkText ) sb->safePrintf("<br>\n");
	if ( printedInlinkText )
		sb->safePrintf("</font>"
			      "</table>"
			      "<br>");
	return true;
}

// use this for xml as well as html
bool printResult ( State0 *st, int32_t ix , int32_t *numPrintedSoFar ) {
	SafeBuf *sb = &st->m_sb;

	HttpRequest *hr = &st->m_hr;

	CollectionRec *cr = g_collectiondb.getRec ( st->m_collnum );
	if ( ! cr ) {
		log("query: printResult: collnum %" PRId32" gone",
		    (int32_t)st->m_collnum);
		return true;
	}


	// shortcuts
	SearchInput *si    = &st->m_si;
	Msg40       *msg40 = &st->m_msg40;

	// ensure not all cluster levels are invisible
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: result #%" PRId32" clusterlevel=%" PRId32,
		     ix, (int32_t)msg40->getClusterLevel(ix));

	int64_t d = msg40->getDocId(ix);
	// this is normally a double, but cast to float
	float docScore = (float)msg40->getScore(ix);

	if ( si->m_docIdsOnly ) {
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t<result>\n"
				       "\t\t<docId>%" PRId64"</docId>\n"
				       "\t</result>\n", 
				       d );
		else if ( si->m_format == FORMAT_JSON )
			sb->safePrintf("\t{\n"
				       "\t\t\"docId\":%" PRId64"\n"
				       "\t},\n",
				       d );
		else
			sb->safePrintf("%" PRId64"<br/>\n", d );
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		return true;
	}

	Msg20      *m20 ;
	if ( si->m_streamResults )
		m20 = msg40->getCompletedSummary(ix);
	else
		m20 = msg40->m_msg20[ix];

	// get the reply
	Msg20Reply *mr = m20->m_r;
		

	// . sometimes the msg20reply is NULL so prevent it coring
	// . i think this happens if all hosts in a shard are down or timeout
	//   or something
	if ( ! mr ) {
		sb->safePrintf("<i>getting summary for docid %" PRId64" had "
			       "error: %s</i><br><br>"
			       ,d,mstrerror(m20->m_errno));
		return true;
	}

	// just print cached web page?
	if ( mr->ptr_content && 
	     si->m_format == FORMAT_JSON &&
	     strstr(mr->ptr_ubuf,"-diffbotxyz") ) {

		// for json items separate with \n,\n
		if ( si->m_format != FORMAT_HTML && *numPrintedSoFar > 0 )
			sb->safePrintf(",\n");

		// a dud? just print empty {}'s
		if ( mr->size_content == 1 ) 
			sb->safePrintf("{}");
		// must have an ending } otherwise it was truncated json.
		// i'm seeing this happen sometimes, i do not know if diffbot
		// or gigablast is truncating the json
		else if ( ! endsInCurly ( mr->ptr_content, mr->size_content )){
			sb->safePrintf("{"
				       "\"error\":"
				       "\"Bad JSON. "
				       "Diffbot reply was missing final "
				       "curly bracket. Truncated JSON.\""
				       "}");
			// make a note of it
			log("results: omitting diffbot reply missing curly "
			    "for %s",mr->ptr_ubuf);
		}
		// if it's a diffbot object just print it out directly
		// into the json. it is already json.
		else
			sb->safeStrcpy ( mr->ptr_content );
			

		// . let's hack the spidertime onto the end
		// . so when we sort by that using gbsortby:spiderdate
		//   we can ensure it is ordered correctly
		// As of the update on 5/13/2014, the end of sb may have whitespace, so first move away from that
		int distance; // distance from end to first non-whitespace char
		const char *end;
		for (distance = 1; distance < sb->length(); distance++) {
		    end = sb->getBufPtr() - distance;
		    if (!is_wspace_a(*end))
		        break;
		}
		if ( si->m_format == FORMAT_JSON &&
		     end > sb->getBufStart() &&
		     *end == '}' ) {
			// replace trailing } with spidertime}
			sb->incrementLength(-distance);
			// comma?
			if ( mr->size_content>1 ) sb->pushChar(',');
			sb->safePrintf("\"docId\":%" PRId64, mr->m_docId);
			sb->safePrintf(",\"gburl\":\"");
			sb->jsonEncode(mr->ptr_ubuf);
			sb->safePrintf("\"");
			// for deduping
			//sb->safePrintf(",\"crc\":%" PRIu32,mr->m_contentHash32);
			// crap, we lose resolution storing as a float
			// so fix that shit here...
			//float f = mr->m_lastSpidered;
			//sb->safePrintf(",\"lastCrawlTimeUTC\":%.0f}",f);
			// MDW: this is VERY convenient for debugging pls
			// leave in. we can easily see if a result 
			// should be there for a query like 
			// gbmin:gbspiderdate:12345678
			sb->safePrintf(",\"lastCrawlTimeUTC\":%" PRId32,
				       mr->m_lastSpidered);
			// also include a timestamp field with an RFC 1123 formatted date
			char timestamp[50];
			time_t lastSpidered = mr->m_lastSpidered;
			struct tm tm_buf;
			struct tm *ptm =gmtime_r(&lastSpidered,&tm_buf);
			strftime(timestamp, 50, "%a, %d %b %Y %X %Z", ptm);
			sb->safePrintf(",\"timestamp\":\"%s\"}\n", timestamp);
		}

		//mr->size_content );
		if ( si->m_format == FORMAT_HTML )
			sb->safePrintf("\n\n<br><br>\n\n");
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		// just in case
		sb->nullTerm();
		return true;
	}

	int32_t cursor = -1;
	if ( si->m_format == FORMAT_XML  ) cursor = sb->length();
	if ( si->m_format == FORMAT_JSON ) cursor = sb->length();

	if ( si->m_format == FORMAT_XML ) 
		sb->safePrintf("\t<result>\n" );

	if ( si->m_format == FORMAT_JSON ) {
		if ( *numPrintedSoFar != 0 ) sb->safePrintf(",\n");
		sb->safePrintf("\t{\n" );
	}


	if ( mr->ptr_content && si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<content><![CDATA[" );
		cdataEncode(sb, mr->ptr_content);
		sb->safePrintf("]]></content>\n");
	}
		
	if ( mr->ptr_content && si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"content\":\"" );
		sb->jsonEncode ( mr->ptr_content );
		sb->safePrintf("\",\n");
	}

	// print spider status pages special
	if ( mr->ptr_content && 
	     si->m_format == FORMAT_HTML &&
	     mr->m_contentType == CT_STATUS ) {
		if ( *numPrintedSoFar )
			sb->safePrintf("<br><hr><br>\n");
		// skip to gbssurl
		const char *s = strstr ( mr->ptr_content,"\"gbssUrl\":");
		if ( ! s ) {
			log("results: missing gbssUrl");
			goto badformat;
		}
		// then do two columns after the two urls
		const char *e = strstr ( s , "\"gbssStatusCode\":" );
		if ( ! e ) {
			log("results: missing gbssStatusCode");
			goto badformat;
		}
		const char *m = strstr ( e , "\"gbssConsecutiveErrors\":");
		if ( ! m ) {
			log("results: missing gbssConsecutiveErrors");
			goto badformat;
		}
		// exclude \0
		const char *end = mr->ptr_content + mr->size_content - 1;
		// use a table with 2 columns
		// so we can use \n to separate lines and don't have to add brs
		// and boldify just the main url, not the redir url!
		sb->safePrintf("<pre style=display:inline;>"
			       "\"gbssUrl\":\""
			       "<b style=color:blue;><a href=/get?"
			       "c=%s&"
			       "d=%" PRId64">"
			       , cr->m_coll
			       , mr->m_docId
			       );
		const char *s2 = strstr ( s , "\"gbssFinalRedirectUrl\":");
		const char *bend = e - 3;
		if ( s2 ) bend = s2 - 3;
		sb->safeMemcpy ( s+11 , bend - (s+11));
		sb->safePrintf("</a></b></pre>\",<br>");
		// now print redir url if there
		if ( s2 ) {
			sb->safePrintf("<pre style=display:inline;>");
			sb->safeMemcpy ( s2 , e-s2 );
			sb->removeLastChar('\n');
			sb->safePrintf("</pre>");
		}
		sb->safePrintf("<table border=0 cellpadding=0 cellspacing=0>"
			       "<tr><td>");
		sb->safePrintf("<pre>");
		//int32_t off = sb->length();
		sb->safeMemcpy ( e , m - e );
		sb->safePrintf("</pre>");
		sb->safePrintf("</td><td>");
		sb->safePrintf("<pre>");
		sb->safeMemcpy ( m , end - m );
		// remove last \n
		sb->removeLastChar('\n');
		sb->removeLastChar('}');
		sb->removeLastChar('\n');
		sb->safePrintf("</pre>\n");
		sb->safePrintf("</td></tr></table>");

		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		// just in case
		sb->nullTerm();
		return true;
	}

	badformat:

	Highlight hi;

	// get the url
	const char *url    = mr->ptr_ubuf      ;
	int32_t  urlLen = mr->size_ubuf - 1 ;
	int32_t  err    = mr->m_errno       ;

	// . remove any session ids from the url
	// . for speed reasons, only check if its a cgi url
	Url uu;
	uu.set( url, urlLen, false, true );
	url    = uu.getUrl();
	urlLen = uu.getUrlLen();

	// get my site hash
	uint64_t siteHash = 0;
	if ( uu.getHostLen() > 0 ) 
		siteHash = hash64(uu.getHost(),uu.getHostLen());

	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format == FORMAT_XML ) isAdmin = false;

	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf("<table><tr><td>");
	}

	if ( si->m_showBanned ) {
		if ( err == EDOCBANNED   ) err = 0;
		if ( err == EDOCFILTERED ) err = 0;
	}

	// if this msg20 had an error print "had error"
	if ( err || urlLen <= 0 || ! url ) {
		// revert back so we do not break the json/xml
		if ( cursor >= 0 ) sb->m_length = cursor;
		// it's unprofessional to display this in browser
		// so just let admin see it
		if ( isAdmin && si->m_format == FORMAT_HTML ) {
			sb->safePrintf("<i>docId %" PRId64" had error: "
				      "%s</i><br><br>",
				      mr->m_docId,//msg40->getDocId(i),
				      mstrerror(err));
		}
		// log it too!
		log("query: docId %" PRId64" had error: %s.",
		    mr->m_docId,mstrerror(err));
		// wrap it up if clustered
		// DO NOT inc it otherwise puts a comma in there and
		// screws up the json
		//*numPrintedSoFar = *numPrintedSoFar + 1;
		return true;
	}

	// if we have a thumbnail show it next to the search result,
	// base64 encoded. do NOT do this for the WIDGET, only for search
	// results in html/xml.
	if ( (si->m_format == FORMAT_HTML || si->m_format == FORMAT_XML ) &&
	     si->m_showImages && mr->ptr_imgData ) {
		ThumbnailArray *ta = (ThumbnailArray *)mr->ptr_imgData;
		ThumbnailInfo *ti = ta->getThumbnailInfo(0);
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t\t");
		ti->printThumbnailInHtml ( sb , 
					   100 ,  // max width
					   100 ,  // max height
					   true ,  // add <a href>
					   NULL ,
					   " style=\"margin:10px;\" ",
					   si->m_format );
		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t<imageHeight>%" PRId32"</imageHeight>\n",
				       ti->m_dy);
			sb->safePrintf("\t\t<imageWidth>%" PRId32"</imageWidth>\n",
				       ti->m_dx);
			sb->safePrintf("\t\t<origImageHeight>%" PRId32
				       "</origImageHeight>\n",
				       ti->m_origDY);
			sb->safePrintf("\t\t<origImageWidth>%" PRId32
				       "</origImageWidth>\n",
				       ti->m_origDX);
			sb->safePrintf("\t\t<imageUrl><![CDATA[");
			cdataEncode(sb, ti->getUrl());
			sb->safePrintf("]]></imageUrl>\n");
		}
		if ( si->m_format == FORMAT_JSON ) {
			sb->safePrintf("\t\t\"imageHeight\":%" PRId32",\n",
				       ti->m_dy);
			sb->safePrintf("\t\t\"imageWidth\":%" PRId32",\n",
				       ti->m_dx);
			sb->safePrintf("\t\t\"origImageHeight\":%" PRId32",\n",
				       ti->m_origDY);
			sb->safePrintf("\t\t\"origImageWidth\":%" PRId32",\n",
				       ti->m_origDX);
			sb->safePrintf("\t\t\"imageUrl\":\"");
			sb->jsonEncode(ti->getUrl());
			sb->safePrintf("\",\n");
		}
	}

	bool isWide = false;
	int32_t newdx = 0;

	// print image for widget
	if ( ( si->m_format == FORMAT_WIDGET_IFRAME ||
	       si->m_format == FORMAT_WIDGET_AJAX ||
	       si->m_format == FORMAT_WIDGET_APPEND ) ) {

		int32_t widgetWidth = hr->getLong("widgetwidth",200);

		// prevent coring
		if ( widgetWidth < 1 ) widgetWidth = 1;

		// each search result in widget has a div around it
		sb->safePrintf("<div "
			       "class=result "
			       // we need the docid and score of last result
			       // when we append new results to the end
			       // of the widget for infinite scrolling
			       // using the scripts in PageBasic.cpp
			       "docid=%" PRId64" "
			       "score=%f " // double
			       "style=\""
			       "width:%" PRId32"px;"
			       "min-height:%" PRId32"px;"
			       "height:%" PRId32"px;"
			       "padding:%" PRId32"px;"
			       "position:relative;"
			       // summary overflows w/o this!
			       "overflow-y:hidden;"
			       "overflow-x:hidden;"
			       "\""
			       ">"
			       , mr->m_docId
			       // this is a double now. this won't work
			       // for streaming...
			       , msg40->m_msg3a.getScores()[ix]
			       // subtract 8 for scrollbar on right
			       , widgetWidth - 2*8 - 8 // padding is 8px
			       , (int32_t)RESULT_HEIGHT
			       , (int32_t)RESULT_HEIGHT
			       , (int32_t)PADDING
			       );
		if ( mr->ptr_imgData ) {
			ThumbnailArray *ta = (ThumbnailArray *)mr->ptr_imgData;
			ThumbnailInfo *ti = ta->getThumbnailInfo(0);
			// account for scrollbar on the right
			int32_t maxWidth = widgetWidth - (int32_t)SCROLLBAR_WIDTH;
			int32_t maxHeight = (int32_t)RESULT_HEIGHT;
			// false = do not print <a href> link on image
			ti->printThumbnailInHtml ( sb , 
						   maxWidth ,
						   maxHeight , 
						   false , // add <a href>
						   &newdx );
		}

		sb->safePrintf ( "<a "
				 "target=_blank "
				 "style=\"text-decoration:none;"
				 // don't let scroll bar obscure text
				 "margin-right:%" PRId32"px;"
				 ,(int32_t)SCROLLBAR_WIDTH
				 );

		// if thumbnail is wide enough put text on top of it, otherwise
		// image is to the left and text is to the right of image
		if ( newdx > .5 * widgetWidth ) {
			isWide = true;
			sb->safePrintf("position:absolute;"
				       "bottom:%" PRId32";"
				       "left:%" PRId32";"
				       , (int32_t) PADDING 
				       , (int32_t) PADDING 
				       );
		}
		// to align the text verticall we gotta make a textbox div
		// otherwise it wraps below image! mdw
		//else
		//	sb->safePrintf("vertical-align:middle;");
		else
			sb->safePrintf("position:absolute;"
				       "bottom:%" PRId32";"
				       "left:%" PRId32";"
				       , (int32_t) PADDING
				       , (int32_t) PADDING + newdx + 10 );

		// close the style and begin the url
		sb->safePrintf( "\" "
				"href=\"" 
				 );

		// truncate off -diffbotxyz%" PRId32"
		int32_t newLen = urlLen;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( "\">");
		
		sb->safePrintf("<b style=\""
			       "text-decoration:none;"
			       "font-size: 15px;"
			       "font-weight:bold;"
			       "background-color:rgba(0,0,0,.5);"
			       "color:white;"
			       "font-family:arial;"
			       "text-shadow: 2px 2px 0 #000 "
			       ",-2px -2px 0 #000 "
			       ",-2px  2px 0 #000 "
			       ", 2px -2px 0 #000 "
			       ", 2px -2px 0 #000 "
			       ", 0px -2px 0 #000 "
			       ",  0px 2px 0 #000 "
			       ", -2px 0px 0 #000 "
			       ",  2px 0px 0 #000 "
			       ";"
			       "\">");
		// then title over image
	}

	// only do link here if we have no thumbnail so no bg image
	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX   ) &&
	     ! mr->ptr_imgData ) {
		sb->safePrintf ( "<a style=text-decoration:none;"
				 "color:white; "
				 "href=" );
		// truncate off -diffbotxyz%" PRId32"
		int32_t newLen = urlLen;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( ">");
	}


	// the a href tag
	if ( si->m_format == FORMAT_HTML ) sb->safePrintf ( "\n\n" );

	// then if it is banned 
	if ( mr->m_isBanned && si->m_format == FORMAT_HTML )
		sb->safePrintf("<font color=red><b>BANNED</b></font> ");


	///////
	//
	// PRINT THE TITLE
	//
	///////

	// the a href tag
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( "<a href=" );
		// truncate off -diffbotxyz%" PRId32"
		int32_t newLen = urlLen;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( ">");
	}


	// . then the title  (should be NULL terminated)
	// . the title can be NULL
	// . highlight it first
	// . the title itself should not have any tags in it!
	const char  *str  = mr->ptr_tbuf;
	int32_t strLen = mr->size_tbuf - 1;
	if ( ! str || strLen < 0 ) {
		strLen = 0;
	}
	
	int32_t hlen;

	const char *frontTag =
		"<font style=\"color:black;background-color:yellow\">" ;
	const char *backTag = "</font>";
	if ( si->m_format == FORMAT_XML ) {
		frontTag = "<b>";
		backTag  = "</b>";
	}
	if ( si->m_format == FORMAT_WIDGET_IFRAME || 
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		frontTag = "<font style=\"background-color:yellow\">" ;
	}
	int32_t cols = si->m_summaryMaxWidth;

	// url encode title
	StackBuf(tmpTitle);
	if ( str && strLen ) {
		tmpTitle.htmlEncode(str, strLen, false);
	}

	StackBuf(hb);
	if ( str && strLen && si->m_doQueryHighlighting ) {
		hlen = hi.set ( &hb, tmpTitle.getBufStart(), tmpTitle.length(), &si->m_hqq, frontTag, backTag);

		// reassign!
		str = hb.getBufStart();
		strLen = hb.length();
	}

	// . use "UNTITLED" if no title
	// . msg20 should supply the dmoz title if it can
	if ( strLen == 0 &&  si->m_format != FORMAT_XML &&  si->m_format != FORMAT_JSON ) {
		str = "<i>UNTITLED</i>";
		strLen = strlen(str);
	}

	if ( str &&  strLen &&
	     ( si->m_format == FORMAT_HTML ||
	       si->m_format == FORMAT_WIDGET_IFRAME ||
	       si->m_format == FORMAT_WIDGET_APPEND ||
	       si->m_format == FORMAT_WIDGET_AJAX ) 
	     ) {
		// determine if TiTle wraps, if it does add a <br> count for each wrap
		if ( !sb->brify( str, strLen, cols ) ) {
			return false;
		}
	}

	// close up the title tag
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<title><![CDATA[");
		if ( str ) {
			cdataEncode(sb, str);
		}
		sb->safePrintf("]]></title>\n");
	}

	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"title\":\"");
		if ( str ) {
			sb->jsonEncode(str);
		}
		sb->safePrintf("\",\n");
	}


	if ( si->m_format == FORMAT_HTML ) 
		sb->safePrintf ("</a><br>\n" ) ;


	// close the title tag stuf
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX ) 
		sb->safePrintf("</b></a>\n");

	//
	// print <h1> tag contents. hack for client.
	//
	char *hp = mr->ptr_htag;
	char *hpend = hp + mr->size_htag;
	for ( ; hp && hp < hpend ; ) {
		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t<h1Tag><![CDATA[");
			cdataEncode(sb, hp);
			sb->safePrintf("]]></h1Tag>\n");
		}
		if ( si->m_format == FORMAT_JSON ) {
			sb->safePrintf("\t\t\"h1Tag\":\"");
			sb->jsonEncode(hp);
			sb->safePrintf("\",\n");
		}
		// it is a \0 separated list of headers generated from XmlDoc::getHeaderTagBuf()
		hp += strlen(hp) + 1;
	}

	// print the [cached] link?
	bool printCached;
	if ( mr->m_contentLen <= 0 )
		printCached = false; //nothing to show
	else if ( isAdmin )
		printCached = true; //admin can bypass noarchive tag
	else if( mr->m_noArchive )
		printCached = false; //page doesn't want to be archived. honour that.
	else
		printCached = true;

	/////
	//
	// print content type after title
	//
	/////
	unsigned char ctype = mr->m_contentType;
	const char *cs = g_contentTypeStrings[ctype];

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t\t<contentType>"
			       "<![CDATA["
			       "%s"
			       "]]>"
			       "</contentType>\n",
			       cs);

	if ( si->m_format == FORMAT_JSON )
		sb->safePrintf("\t\t\"contentType\":\"%s\",\n",cs);

	if ( si->m_format == FORMAT_HTML &&
	     ctype != CT_HTML && 
	     ctype != CT_UNKNOWN ){
		sb->safePrintf(" <b><font style=color:white;"
			       "background-color:maroon;>");
		const char *p = cs;
		for ( ; *p ; p++ ) {
			char c = to_upper_a(*p);
			sb->pushChar(c);
		}
		sb->safePrintf("</font></b> &nbsp;");
	}
	

	////////////
	//
	// print the summary
	//
	////////////

	// do the normal summary
	str    = mr->ptr_displaySum;

	// sometimes the summary is longer than requested because for
	// summary deduping purposes (see "pss" parm in Parms.cpp) we do not
	// get it as short as requested. so use mr->m_sumPrintSize here
	// not mr->size_sum
	strLen = mr->size_displaySum - 1;

	// this includes the terminating \0 or \0\0 so back up
	if ( strLen < 0 ) {
		strLen  = 0;
	}

	bool printSummary = true;

	// do not print summaries for widgets by default unless overridden with &summary=1
	int32_t defSum = 0;

	// if no image then default the summary to on
	if ( ! mr->ptr_imgData ) {
		defSum = 1;
	}

	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) && 
	     hr->getLong("summaries",defSum) == 0 ) {
		printSummary = false;
	}

	if ( printSummary &&
	     (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) ) {
		int32_t sumLen = strLen;
		if ( sumLen > 150 ) sumLen = 150;
		if ( sumLen ) {
			sb->safePrintf("<br>");
			sb->safeTruncateEllipsis ( str , sumLen );
		}
	}

	if ( si->m_format == FORMAT_HTML ) {
		if ( printSummary ) {
			sb->brify( str, strLen, cols );
		}

		// new line if not xml. even summary is empty we need it too like
		// when showing xml docs - MDW 9/28/2014
		sb->safePrintf( "<br>\n" );
	} else if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf( "\t\t<sum><![CDATA[" );
		cdataEncode(sb, str);
		sb->safePrintf( "]]></sum>\n" );
	} else if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf( "\t\t\"sum\":\"" );
		sb->jsonEncode( str );
		sb->safePrintf( "\",\n" );
	}

	/////////
	// 
	// meta tag values for &dt=keywords ...
	//
	/////////
	if ( mr->ptr_dbuf && mr->size_dbuf>1 )
		printMetaContent ( msg40 , ix,st,sb);

	////////////
	//
	// print the URL
	//
	////////////

	StackBuf(tmpBuf);
	char* displayUrl = Url::getDisplayUrl(url, &tmpBuf);
	uint32_t displayUrlLen = tmpBuf.length();

	// hack off the http:// if any for displaying it on screen
	if ( displayUrlLen > 8 && strncmp ( displayUrl , "http://" , 7 )==0 ) {
		displayUrl += 7; displayUrlLen -= 7; }
	// . remove trailing /
	// . only remove from root urls in case user cuts and 
	//   pastes it for link: search
	if ( displayUrl [ displayUrlLen - 1 ] == '/' ) {
		// see if any other slash before us
		int32_t j;
		for ( j = displayUrlLen - 2 ; j >= 0 ; j-- )
			if ( displayUrl[j] == '/' ) break;
		// if there wasn't, we must have been a root url
		// so hack off the last slash
		if ( j < 0 ) displayUrlLen--;
	}
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ("<font color=gray>" );
		//sb->htmlEncode ( url , strlen(url) , false );
		// 20 for the date after it
		sb->safeTruncateEllipsis ( displayUrl , 50 ); // cols - 30 );
		// turn off the color
		sb->safePrintf ( "</font>\n" );
	}

	// print url for widgets now
	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) ) {
		//sb->safePrintf ("<br><font color=gray size=-1>" );
		// print url for widgets in top left if we have a wide image
		// otherwise it gets truncated below the title for some reason
		if ( isWide )
			sb->safePrintf ("<br><font color=white size=-1 "
					"style=position:absolute;left:10px;"
					"top:10px;background-color:black;>" );
		else if ( mr->ptr_imgData )
			sb->safePrintf ("<br><font color=gray size=-1 "
					"style=position:absolute;left:%" PRId32"px;"
					"top:10px;>"
				       , (int32_t) PADDING + newdx + 10 );
		else
			sb->safePrintf ("<br><font color=gray size=-1>");
		// print the url now, truncated to 50 chars
		sb->safeTruncateEllipsis ( url , 50 ); // cols - 30 );
		sb->safePrintf ( "</font>\n" );
	}


	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<url><![CDATA[");
		sb->safeMemcpy ( displayUrl , displayUrlLen );
		sb->safePrintf("]]></url>\n");
	}
	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"url\":\"");
		sb->jsonEncode ( displayUrl , displayUrlLen );
		sb->safePrintf("\",\n");
	}

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t\t<hopCount>%" PRId32"</hopCount>\n",
			       (int32_t)mr->m_hopcount);

	if ( si->m_format == FORMAT_JSON )
		sb->safePrintf("\t\t\"hopCount\":%" PRId32",\n",(int32_t)mr->m_hopcount);

	// now the last spidered date of the document
	time_t ts = mr->m_lastSpidered;
	if ( si->m_format == FORMAT_HTML ) 
		printTimeAgo ( sb , ts , "indexed" , si );

	// the date it was last modified
	ts = mr->m_lastModified;
	if ( si->m_format == FORMAT_HTML ) 
		printTimeAgo ( sb , ts , "modified" , si );

	//
	// more xml stuff
	//
	if ( si->m_format == FORMAT_XML ) {
		// doc size in Kilobytes
		sb->safePrintf ( "\t\t<size><![CDATA[%4.0fk]]></size>\n",
				(float)mr->m_contentLen/1024.0);
		sb->safePrintf ( "\t\t<sizeInBytes>%" PRId32"</sizeInBytes>\n",
				 mr->m_contentLen);
		// . docId for possible cached link
		// . might have merged a bunch together
		sb->safePrintf("\t\t<docId>%" PRId64"</docId>\n",mr->m_docId );
		sb->safePrintf("\t\t<docScore>%f</docScore>\n",docScore);
	}

	if ( si->m_format == FORMAT_XML && mr->m_contentType != CT_STATUS ) {
		// . show the site root
		// . for hompages.com/users/fred/mypage.html this will be
		//   homepages.com/users/fred/
		// . for www.xyz.edu/~foo/burp/ this will be
		//   www.xyz.edu/~foo/ etc.
		// seems like this isn't the way to do it, cuz Tagdb.cpp
		// adds the "site" tag itself and we do not always have it
		// in the XmlDoc::ptr_tagRec... so do it this way:
		const char *site    = mr->ptr_site;
		int32_t siteLen = mr->size_site-1;
		//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
		sb->safePrintf("\t\t<site><![CDATA[");
		if ( site && siteLen > 0 ) sb->safeMemcpy ( site , siteLen );
		sb->safePrintf("]]></site>\n");
		//int32_t sh = hash32 ( site , siteLen );
		//sb->safePrintf ("\t\t<siteHash32>%" PRIu32"</siteHash32>\n",sh);
		//int32_t dh = uu.getDomainHash32 ();
		//sb->safePrintf ("\t\t<domainHash32>%" PRIu32"</domainHash32>\n",dh);
		// spider date
		sb->safePrintf ( "\t\t<spidered>%" PRIu32"</spidered>\n",
				 (uint32_t)mr->m_lastSpidered);
		// backwards compatibility for buzz
		sb->safePrintf ( "\t\t<firstIndexedDateUTC>%" PRIu32
				"</firstIndexedDateUTC>\n",
				 (uint32_t)mr->m_firstIndexedDate);
		sb->safePrintf( "\t\t<contentHash32>%" PRIu32
				"</contentHash32>\n",
				(uint32_t)mr->m_contentHash32);
		// pub date
		int32_t datedbDate = mr->m_datedbDate;
		// show the datedb date as "<pubDate>" for now
		if ( datedbDate != -1 )
			sb->safePrintf ( "\t\t<pubdate>%" PRIu32"</pubdate>\n",
					 (uint32_t)datedbDate);
	}

	if ( si->m_format == FORMAT_JSON ) {
		// doc size in Kilobytes
		sb->safePrintf ( "\t\t\"size\":\"%4.0fk\",\n",
				(float)mr->m_contentLen/1024.0);
		sb->safePrintf ( "\t\t\"sizeInBytes\":%" PRId32",\n",
				 mr->m_contentLen);
		// . docId for possible cached link
		// . might have merged a bunch together
		sb->safePrintf("\t\t\"docId\":%" PRId64",\n",mr->m_docId );
		sb->safePrintf("\t\t\"docScore\":%f,\n",docScore);
		sb->safePrintf("\t\t\"cacheAvailable\":%s,\n", printCached?"true":"false");
		sb->safePrintf("\t\t\"isAdult\":%s,\n", mr->m_isAdult?"true":"false");
	}

	if ( si->m_format == FORMAT_JSON && mr->m_contentType != CT_STATUS ) {
		// . show the site root
		// . for hompages.com/users/fred/mypage.html this will be
		//   homepages.com/users/fred/
		// . for www.xyz.edu/~foo/burp/ this will be
		//   www.xyz.edu/~foo/ etc.
		// seems like this isn't the way to do it, cuz Tagdb.cpp
		// adds the "site" tag itself and we do not always have it
		// in the XmlDoc::ptr_tagRec... so do it this way:
		const char *site = mr->ptr_site;
		int32_t siteLen = mr->size_site-1;
		//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
		sb->safePrintf("\t\t\"site\":\"");
		if ( site && siteLen > 0 ) sb->safeMemcpy ( site , siteLen );
		sb->safePrintf("\",\n");
		// spider date
		sb->safePrintf ( "\t\t\"spidered\":%" PRIu32",\n",
				 (uint32_t)mr->m_lastSpidered);
		// backwards compatibility for buzz
		sb->safePrintf ( "\t\t\"firstIndexedDateUTC\":%" PRIu32",\n"
				 , (uint32_t) mr->m_firstIndexedDate);
		sb->safePrintf( "\t\t\"contentHash32\":%" PRIu32",\n"
				, (uint32_t)mr->m_contentHash32);
		// pub date
		int32_t datedbDate = mr->m_datedbDate;
		// show the datedb date as "<pubDate>" for now
		if ( datedbDate != -1 )
			sb->safePrintf ( "\t\t\"pubdate\":%" PRIu32",\n",
					 (uint32_t)datedbDate);
	}



	// . we also store the outlinks in a linkInfo structure
	// . we can call LinkInfo::set ( Links *outlinks ) to set it
	//   in the msg20
	LinkInfo *outlinks = (LinkInfo *)mr->ptr_outlinks;
	// NULLify if empty
	if ( mr->size_outlinks <= 0 ) outlinks = NULL;
	// only for xml for now
	if ( si->m_format == FORMAT_HTML ) outlinks = NULL;
	Inlink *k;
	// do we need absScore2 for outlinks?
	//k = NULL;
	while ( outlinks &&
		(k =outlinks->getNextInlink(k))) 
		// print it out
		sb->safePrintf("\t\t<outlink "
			      "docId=\"%" PRId64"\" "
			      "hostId=\"%" PRIu32"\" "
			      "indexed=\"%" PRId32"\" "
			      "pubdate=\"%" PRId32"\" ",
			      k->m_docId ,
			       (uint32_t)k->m_ip,//hostHash, but use ip for now
			      (int32_t)k->m_firstIndexedDate ,
			      (int32_t)k->m_datedbDate );

	if ( si->m_format == FORMAT_XML ) {
		// result
		sb->safePrintf("\t\t<language><![CDATA[%s]]>"
			      "</language>\n", 
			      getLanguageString(mr->m_language));
		sb->safePrintf("\t\t<langAbbr>%s</langAbbr>\n", 
			      getLanguageAbbr(mr->m_language));
	}

	if ( si->m_format == FORMAT_JSON ) {
		// result
		sb->safePrintf("\t\t\"language\":\"%s\",\n",
			      getLanguageString(mr->m_language));
		sb->safePrintf("\t\t\"langAbbr\":\"%s\",\n",
			      getLanguageAbbr(mr->m_language));
	}

	//
	// end more xml stuff
	//


	
	if ( si->m_format == FORMAT_HTML ) {
		int32_t lang = mr->m_language;
		if ( lang ) sb->safePrintf(" - %s",getLanguageString(lang));

		sb->safePrintf("<br>\n");
	}

	//char *coll = si->m_cr->m_coll;

	// get collnum result is from
	//collnum_t collnum = si->m_cr->m_collnum;
	// if searching multiple collections  - federated search
	CollectionRec *scr = g_collectiondb.getRec ( mr->m_collnum );
	const char *coll = "UNKNOWN";
	if ( scr ) coll = scr->m_coll;

	if ( si->m_format == FORMAT_HTML  && printCached ) {
		sb->safePrintf ( "<a href=\""
				"/get?"
				"q=%s&"
				"qlang=%s&"
				"c=%s&d=%" PRId64"&cnsp=0\">"
				"cached</a>\n",
				 st->m_qesb.getBufStart() ,
				// "qlang" parm
				si->m_defaultSortLang,
				coll ,
				mr->m_docId );
	}

	// unhide the divs on click
	int32_t placeHolder = -1;
	int32_t placeHolderLen = 0;
	if ( si->m_format == FORMAT_HTML && si->m_getDocIdScoringInfo ) {
		// place holder for backlink table link
		placeHolder = sb->length();
		sb->safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('bl%" PRId32"');"
			       "if ( e.style.display == 'none' ){"
			       "e.style.display = '';"
			       "}"
			       "else {"
			       "e.style.display = 'none';"
			       "}"
			       "\""
			       " "

			       "style="
			       "cursor:hand;"
			       "cursor:pointer;"
			       "color:blue;>"
			       "<u>00000 backlinks</u>"
			       "</a>\n"
			       , ix 
			       );
		placeHolderLen = sb->length() - placeHolder;

		// unhide the scoring table on click
		sb->safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('sc%" PRId32"');"
			       "if ( e.style.display == 'none' ){"
			       "e.style.display = '';"
			       "}"
			       "else {"
			       "e.style.display = 'none';"
			       "}"
			       "\""
			       " "

			       "style="
			       "cursor:hand;"
			       "cursor:pointer;"
			       "color:blue;>"
			       "scoring"
			       "</a>\n"
			       ,ix
			       );
	}

	if ( si->m_format == FORMAT_HTML ) {
		// reindex
		sb->safePrintf(" - <a style=color:blue; href=\"/addurl?"
			       "urls=");
		urlEncode(sb, url, strlen(url), false);
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		sb->safePrintf("&c=%s&rand64=%" PRIu64"\">respider</a>\n",
			       coll,rand64);


		sb->safePrintf (" - "
				"<a style=color:blue; "
				"href=\"/search?sb=1&c=%s&"
				//"q=url2%%3A" 
				"q=gbfieldmatch%%3AgbssUrl%%3A"
				, coll 
				);
		// do not include ending \0
		urlEncode(sb, mr->ptr_ubuf, mr->size_ubuf-1, false);
		sb->safePrintf ( "\">"
				 "spider info</a>\n"
			       );


		//
		// show rainbow sections link
		//

		sb->safePrintf ( " - <a style=color:blue; href=\""
				 "/get?"
				 // show rainbow sections
				 "page=4&"
				 "q=%s&"
				 "qlang=%s&"
				 "c=%s&"
				 "d=%" PRId64"&"
				 "cnsp=0\">"
				 "sections</a>\n", 
				 st->m_qesb.getBufStart() , 
				 // "qlang" parm
				 si->m_defaultSortLang,
				 coll , 
				 mr->m_docId ); 


		sb->safePrintf ( " - <a style=color:blue; href=\""
				 "/get?"
				 // show rainbow sections
				 "page=1&"
				 //"q=%s&"
				 //"qlang=%s&"
				 "c=%s&"
				 "d=%" PRId64"&"
				 "cnsp=0\">"
				 "page info</a>\n", 
				 //st->m_qe , 
				 // "qlang" parm
				 //si->m_defaultSortLang,
				 coll , 
				 mr->m_docId ); 


		sb->safePrintf ( " - <a style=color:blue; href=\""
				 "/get?"
				 // show rainbow sections
				 "page=5&"
				 //"q=%s&"
				 //"qlang=%s&"
				 "c=%s&"
				 "d=%" PRId64"&"
				 "cnsp=0\">"
				 "term info</a>\n", 
				 //st->m_qe , 
				 // "qlang" parm
				 //si->m_defaultSortLang,
				 coll , 
				 mr->m_docId ); 

		// this stuff is secret just for local guys! not any more

		// now the ip of url
		//int32_t urlip = msg40->getIp(i);
		// don't combine this with the sprintf above cuz
		// iptoa uses a static local buffer like ctime()
		sb->safePrintf(//"<br>"
			      " - <a style=color:blue; href=\"/search?"
			      "c=%s&sc=1&dr=0&q=ip:%s&"
			      "n=100&usecache=0\">%s</a>\n",
			      coll,iptoa(mr->m_ip), iptoa(mr->m_ip) );
		// ip domain link
		unsigned char *us = (unsigned char *)&mr->m_ip;//urlip;
		sb->safePrintf (" - <a style=color:blue; "
				"href=\"/search?c=%s&sc=1&dr=0&n=100&"
				"q=ip:%" PRId32".%" PRId32".%" PRId32"&"
				"usecache=0\">%" PRId32".%" PRId32".%" PRId32"</a>\n",
				coll,
				(int32_t)us[0],(int32_t)us[1],(int32_t)us[2],
				(int32_t)us[0],(int32_t)us[1],(int32_t)us[2]);
	}

	char dbuf [ MAX_URL_LEN ];
	int32_t dlen = uu.getDomainLen();

	if ( si->m_format == FORMAT_HTML ) {
		gbmemcpy ( dbuf , uu.getDomain() , dlen );
		dbuf [ dlen ] = '\0';
		// newspaperarchive urls have no domain
		if ( dlen == 0 ) {
			dlen = uu.getHostLen();
			gbmemcpy ( dbuf , uu.getHost() , dlen );
			dbuf [ dlen ] = '\0';
		}

		// admin always gets the site: option so he can ban
		sb->safePrintf (" - "
			       " <a style=color:blue; href=\"/search?"
			       "q=site%%3A%s&sc=0&c=%s\">"
			       "domain</a>\n" ,
				dbuf ,
				coll );//, dbuf );
	}


	if ( si->m_format == FORMAT_HTML && si->m_doSiteClustering ) {
		char hbuf [ MAX_URL_LEN ];
		int32_t hlen = uu.getHostLen();
		gbmemcpy ( hbuf , uu.getHost() , hlen );
		hbuf [ hlen ] = '\0';

		// make the cgi parm to add to the original url
		char tmp[512];
		SafeBuf qq (tmp,512);
		qq.safePrintf("q=");
		urlEncode(&qq, "site:");
		urlEncode(&qq, hbuf);
		urlEncode(&qq, " | ");
		qq.safeStrcpy(st->m_qesb.getBufStart());
		qq.nullTerm();
		// get the original url and add/replace in query
		char tmp2[512];
		SafeBuf newUrl(tmp2, 512);
		replaceParm ( qq.getBufStart() , &newUrl , hr );
		// put show more results from this site link
		sb->safePrintf (" - <nobr><a href=\"%s\">"
			       "more from this site</a></nobr>"
				, newUrl.getBufStart()
				);
		//else sb->safePrintf ( "<br><br>\n");
	}


	if (si->m_format == FORMAT_HTML && isAdmin ){
		const char *un = "";
		int32_t  banVal = 1;
		if ( mr->m_isBanned ) {
			un = "UN";
			banVal = 0;
		}
		// don't put on a separate line because then it is too
		// easy to mis-click on it
		sb->safePrintf(//"<br>"
			       " - "
			      " <a style=color:green; href=\"/admin/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%" PRId32"&"
			      "u=%s&c=%s\">"
			      "<nobr>%sBAN %s"
			      "</nobr></a>\n"
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf );
		//banSites->safePrintf("%s+", dbuf);
		dlen = uu.getHostLen();
		gbmemcpy ( dbuf , uu.getHost() , dlen );
		dbuf [ dlen ] = '\0';
		sb->safePrintf(" - "
			      " <a style=color:green; href=\"/admin/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%" PRId32"&"
			      "u=%s&c=%s\">"
			      "<nobr>%sBAN %s</nobr></a>\n"
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf
			      );
	}

	if ( mr->size_metadataBuf && si->m_format == FORMAT_JSON) {
		sb->safePrintf("\t\t\"metadata\":[");
		//sb->safeMemcpy(mr->ptr_metadataBuf, mr->size_metadataBuf);
		sb->safeStrcpy(mr->ptr_metadataBuf);
		// without this \n we seem to lose our ] i guess it gets
		// backed up over
		sb->safePrintf("],\n");
	}


	if ( mr->size_metadataBuf && si->m_format == FORMAT_HTML) {
		sb->safePrintf("<br>");

		Json md;
		JsonItem *ji = md.parseJsonStringIntoJsonItems(mr->ptr_metadataBuf);
		char tmpBuf1[1024];
		char tmpBuf2[1024];
		SafeBuf nameBuf(tmpBuf1, 1024);
		for ( ; ji ; ji = ji->m_next ) {
			if(ji->isInArray()) continue;
			if(ji->m_type == JT_ARRAY) continue;
			ji->getCompoundName ( nameBuf ) ;
			if(nameBuf.length() == 0) {
				continue;
			}
			//nameBuf.replaceChar('-', '_');
			nameBuf.nullTerm();

			int32_t valLen;
			const char* valBuf = ji->getValueAsString(&valLen);
			SafeBuf queryBuf(tmpBuf2, 1024);
			// log("compound name is %s %d %d",nameBuf.getBufStart(),
			// nameBuf.length(), valLen);

			queryBuf.safePrintf("/search?q=%s:%%22",nameBuf.getBufStart());
			urlEncode(&queryBuf, valBuf, valLen);
			queryBuf.safePrintf("%%22&c=%s",coll);
			queryBuf.nullTerm();
			sb->safePrintf(" - <a href=\"%s\">%s:\"", queryBuf.getBufStart(),
						   nameBuf.getBufStart());
			sb->safeMemcpy(valBuf, valLen);
			sb->safeStrcpy("\"</a>");
		}
	}


	// end serp div
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX )
		sb->safePrintf("</div><hr>");
	

	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf ( "<br><br>\n");

	// search result spacer
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX   )
		sb->safePrintf("<div style=line-height:%" PRId32"px;><br></div>",
			       (int32_t)SERP_SPACER);


	// inc it
	*numPrintedSoFar = *numPrintedSoFar + 1;

	// done?
	DocIdScore *dp = msg40->getScoreInfo(ix);
	if ( ! dp ) {
		if ( si->m_format == FORMAT_XML ) 
			sb->safePrintf ("\t</result>\n\n");
		if ( si->m_format == FORMAT_JSON ) {
			// remove last ,\n
			sb->m_length -= 2;
			sb->safePrintf ("\n\t}\n\n");
		}
		// wtf?
		//g_process.shutdownAbort(true);
		// at least close up the table
		if ( si->m_format != FORMAT_HTML ) return true;

		sb->safePrintf("</table>\n");

		return true;
	}


	//
	// scoring info tables
	//

	int32_t nr = dp->m_numRequiredTerms;
	if ( nr == 1 ) nr = 0;
	// print breakout tables here for distance matrix
	// final score calc
	char tmp[1024];
	SafeBuf ft(tmp, 1024);;

	// put in a hidden div so you can unhide it
	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf("<div id=bl%" PRId32" style=display:none;>\n", ix );

	// print xml and html inlinks
	int32_t numInlinks = 0;
	printInlinkText ( sb , mr , si , &numInlinks );


	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf("</div>");
		sb->safePrintf("<div id=sc%" PRId32" style=display:none;>\n", ix );
	}


	// if pair changes then display the sum
	int32_t lastTermNum1 = -1;
	int32_t lastTermNum2 = -1;

	float minScore = -1;

	// display all the PairScores
	for ( int32_t i = 0 ; i < dp->m_numPairs ; i++ ) {
		float totalPairScore = 0.0;
		// print all the top winners for this pair
		PairScore *fps = &dp->m_pairScores[i];
		// if same combo as last time skip
		if ( fps->m_qtermNum1 == lastTermNum1 &&
		     fps->m_qtermNum2 == lastTermNum2 )
			continue;
		lastTermNum1 = fps->m_qtermNum1;
		lastTermNum2 = fps->m_qtermNum2;
		bool firstTime = true;
		// print all pairs for this combo
		for ( int32_t j = i ; j < dp->m_numPairs ; j++ ) {
			// get it
			PairScore *ps = &dp->m_pairScores[j];
			// stop if different pair now
			if ( ps->m_qtermNum1 != fps->m_qtermNum1 ) break;
			if ( ps->m_qtermNum2 != fps->m_qtermNum2 ) break;
			// skip if 0. neighborhood terms have weight of 0 now
			if ( almostEqualFloat(ps->m_finalScore, 0.0) ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = &si->m_q;
				printTermPairs ( sb , q , ps );
				printScoresHeader ( sb );
				firstTime = false;
			}
			// print it
			printPairScore ( sb , si , ps , mr );

			// add it up
			totalPairScore += ps->m_finalScore;
		}
		if ( ft.length() ) ft.safePrintf(" , ");
		ft.safePrintf("%f",totalPairScore);
		// min?
		if ( minScore < 0.0 || totalPairScore < minScore )
			minScore = totalPairScore;
		// we need to set "ft" for xml stuff below
		if ( si->m_format != FORMAT_HTML ) continue;

		sb->safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalPairScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb->safePrintf("</table><br>");
	}

	int32_t lastTermNum = -1;

	int32_t numSingles = dp->m_numSingles;
	// do not print this if we got pairs
	if ( dp->m_numPairs ) numSingles = 0;

	for ( int32_t i = 0 ; i < numSingles ; i++ ) {
		float totalSingleScore = 0.0;
		// print all the top winners for this single
		SingleScore *fss = &dp->m_singleScores[i];
		// if same combo as last time skip
		if ( fss->m_qtermNum == lastTermNum ) continue;
		// do not reprint for this query term num
		lastTermNum = fss->m_qtermNum;
		bool firstTime = true;
		// print all singles for this combo
		for ( int32_t j = i ; j < dp->m_numSingles ; j++ ) {
			// get it
			SingleScore *ss = &dp->m_singleScores[j];
			// stop if different single now
			if ( ss->m_qtermNum != fss->m_qtermNum ) break;
			// skip if 0. skip neighborhoods i guess
			if ( almostEqualFloat(ss->m_finalScore, 0.0) ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = &si->m_q;
				printSingleTerm ( sb , q , ss );
				printScoresHeader ( sb );
				firstTime = false;
			}

			// print it
			printSingleScore ( sb , si , ss , mr );
			if(si->m_format == FORMAT_JSON && j+1<dp->m_numSingles)
				sb->safePrintf(",\n");
			else
				sb->safePrintf("\n");

			// add up
			totalSingleScore += ss->m_finalScore;
		}
		
		if ( ft.length() ) ft.safePrintf(" , ");
		ft.safePrintf("%f",totalSingleScore);
		// min?
		if ( minScore < 0.0 || totalSingleScore < minScore )
			minScore = totalSingleScore;
		// we need to set "ft" for xml stuff below
		if ( si->m_format != FORMAT_HTML ) continue;

		sb->safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalSingleScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb->safePrintf("</table><br>");
	}

	

	const char *ff = "";
	const char *ff2 = "sum";

	// final score!!!
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf ("\t\t<siteRank>%" PRId32"</siteRank>\n",
			       (int32_t)dp->m_siteRank );

		sb->safePrintf ("\t\t<numGoodSiteInlinks>%" PRId32
			       "</numGoodSiteInlinks>\n",
			       (int32_t)mr->m_siteNumInlinks );

		struct tm *timeStruct3;
		time_t pageInlinksLastUpdated = mr->m_pageInlinksLastUpdated;
		struct tm tm_buf;
		timeStruct3 = gmtime_r(&pageInlinksLastUpdated,&tm_buf);
		char tmp3[64];
		strftime ( tmp3 , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct3 );
		// -1 means unknown
		if ( mr->m_pageNumInlinks >= 0 )
			// how many inlinks, external and internal, we have
			// to this page not filtered in any way!!!
			sb->safePrintf("\t\t<numTotalPageInlinks>%" PRId32
				      "</numTotalPageInlinks>\n"
				      ,mr->m_pageNumInlinks
				      );
		// how many inlinking ips we got, including our own if
		// we link to ourself
		sb->safePrintf("\t\t<numUniqueIpsLinkingToPage>%" PRId32
			      "</numUniqueIpsLinkingToPage>\n"
			      ,mr->m_pageNumUniqueIps
			      );
		// how many inlinking cblocks we got, including our own if
		// we link to ourself
		sb->safePrintf("\t\t<numUniqueCBlocksLinkingToPage>%" PRId32
			      "</numUniqueCBlocksLinkingToPage>\n"
			      ,mr->m_pageNumUniqueCBlocks
			      );
		
		// how many "good" inlinks. i.e. inlinks whose linktext we
		// count and index.
		sb->safePrintf("\t\t<numGoodPageInlinks>%" PRId32
			      "</numGoodPageInlinks>\n"
			      "\t\t<pageInlinksLastComputedUTC>%" PRIu32
			      "</pageInlinksLastComputedUTC>\n"
			      ,mr->m_pageNumGoodInlinks
			       ,(uint32_t)mr->m_pageInlinksLastUpdated
			      );


		float score    = msg40->getScore   (ix);
		sb->safePrintf("\t\t<finalScore>%f</finalScore>\n", score );
		sb->safePrintf ("\t\t<finalScoreEquationCanonical>"
			       "<![CDATA["
			       "Final Score = (siteRank/%.01f+1) * "
			       "(%.01f [if not foreign language]) * "
			       "(%s of above matrix scores)"
			       "]]>"
			       "</finalScoreEquationCanonical>\n"
			       , SITERANKDIVISOR
				, si->m_sameLangWeight //SAMELANGMULT
			       , ff2 
			       );
		sb->safePrintf ("\t\t<finalScoreEquation>"
			       "<![CDATA["
			       "<b>%.03f</b> = (%" PRId32"/%.01f+1) " // * %s("
			       , dp->m_finalScore
			       , (int32_t)dp->m_siteRank
			       , SITERANKDIVISOR
			       //, ff
			       );
		// then language weight
		if ( si->m_queryLangId == 0 || 
		     mr->m_language    == 0 ||
		     si->m_queryLangId == mr->m_language )
			sb->safePrintf(" * %.01f",
				       si->m_sameLangWeight);//SAMELANGMULT);
		// the actual min then
		sb->safePrintf(" * %.03f",minScore);
		// no longer list all the scores
		//sb->safeMemcpy ( &ft );
		sb->safePrintf(//")"
			      "]]>"
			      "</finalScoreEquation>\n");
		sb->safePrintf ("\t</result>\n\n");
		return true;
	}

	if ( si->m_format != FORMAT_HTML ) return true;

	const char *cc = getCountryCode ( mr->m_country );
	if ( mr->m_country == 0 ) cc = "Unknown";

	sb->safePrintf("<table border=1>"

		      "<tr><td colspan=10><b><center>"
		      "final score</center></b>"
		      "</td></tr>"

		      "<tr>"
		      "<td>docId</td>"
		      "<td>%" PRId64"</td>"
		      "</tr>"

		      "<tr>"
		      "<td>site</td>"
		      "<td>%s</td>"
		      "</tr>"

		      "<tr>"
		      "<td>hopcount</td>"
		      "<td>%" PRId32"</td>"
		      "</tr>"

		      "<tr>"
		      "<td>language</td>"
		      "<td><font color=green><b>%s</b></font></td>"
		      "</tr>"

		      "<tr>"
		      "<td>country</td>"
		      "<td>%s</td>"
		      "</tr>"

		      "<tr>"
		      "<td>siteRank</td>"
		      "<td><font color=blue>%" PRId32"</font></td>"
		      "</tr>"

		      "<tr><td colspan=100>"
		      , dp->m_docId
		      , mr->ptr_site
		      , (int32_t)mr->m_hopcount
		      //, getLanguageString(mr->m_summaryLanguage)
		      , getLanguageString(mr->m_language) // use page language
		      , cc
		      , (int32_t)dp->m_siteRank
		      );

	// list all final scores starting with pairs
	sb->safePrintf("<b>%f</b> = "
		      "(<font color=blue>%" PRId32"</font>/%.01f+1)"
		      , dp->m_finalScore
		      , (int32_t)dp->m_siteRank
		      , SITERANKDIVISOR
		      );

	// if lang is different
	if ( si->m_queryLangId == 0 || 
	     mr->m_language    == 0 ||
	     si->m_queryLangId == mr->m_language )
		sb->safePrintf(" * <font color=green><b>%.01f</b></font>",
			       si->m_sameLangWeight);//SAMELANGMULT);

	// list all final scores starting with pairs
	sb->safePrintf(" * %s("
		      , ff
		      );
	sb->safeMemcpy ( &ft );
	sb->safePrintf(")</td></tr></table><br>");

	// put in a hidden div so you can unhide it
	sb->safePrintf("</div>\n");

	// result is in a table so we can put the result # in its own column
	sb->safePrintf("</td></tr></table>");



	// space out 0000 backlinks
	char *p = sb->getBufStart() + placeHolder;
	int32_t plen = placeHolderLen;
	if ( numInlinks == 0 ) 
		memset ( p , ' ' , plen );
	if ( numInlinks > 0 && numInlinks < 99999 ) {
		char *ss = strstr ( p, "00000" );
		if ( ss ) {
			char c = ss[5];
			sprintf(ss,"%5" PRId32,numInlinks);
			ss[5] = c;
		}
	}
	// print "1 backlink" not "1 backlinks"
	if ( numInlinks == 1 ) {
		char *xx = strstr(p,"backlinks");
		if ( xx ) xx[8] = ' ';
	}

	return true;
}




static bool printPairScore ( SafeBuf *sb , SearchInput *si , PairScore *ps , Msg20Reply *mr) {

	// shortcut
	Query *q = &si->m_q;

	int32_t qtn1 = ps->m_qtermNum1;
	int32_t qtn2 = ps->m_qtermNum2;
	
	unsigned char de1 = ps->m_densityRank1;
	unsigned char de2 = ps->m_densityRank2;
	float dnw1 = getDensityWeight(de1);
	float dnw2 = getDensityWeight(de2);
	
	int32_t hg1 = ps->m_hashGroup1;
	int32_t hg2 = ps->m_hashGroup2;
	
	
	float hgw1 = getHashGroupWeight(hg1);
	float hgw2 = getHashGroupWeight(hg2);
	
	int32_t wp1 = ps->m_wordPos1;
	int32_t wp2 = ps->m_wordPos2;
	
	unsigned char wr1 = ps->m_wordSpamRank1;
	float wsw1 = getWordSpamWeight(wr1);
	unsigned char wr2 = ps->m_wordSpamRank2;
	float wsw2 = getWordSpamWeight(wr2);
	
	// HACK for inlink text!
	if ( hg1 == HASHGROUP_INLINKTEXT )
		wsw1 = getLinkerWeight(wr1);
	if ( hg2 == HASHGROUP_INLINKTEXT )
		wsw2 = getLinkerWeight(wr2);
	
	const char *syn1 = "no";
	const char *syn2 = "no";
	float sw1 = 1.0;
	float sw2 = 1.0;
	if ( ps->m_isSynonym1 ) {
		syn1 = "yes";
		sw1  = g_conf.m_synonymWeight;
	}
	if ( ps->m_isSynonym2 ) {
		syn2 = "yes";
		sw2  = g_conf.m_synonymWeight;
	}

	const char *bs1  = "no";
	const char *bs2  = "no";
	if ( ps->m_isHalfStopWikiBigram1 ) bs1 = "yes";
	if ( ps->m_isHalfStopWikiBigram2 ) bs2 = "yes";
	float wbw1 = 1.0;
	float wbw2 = 1.0;
	if ( ps->m_isHalfStopWikiBigram1 ) wbw1 = WIKI_BIGRAM_WEIGHT;
	if ( ps->m_isHalfStopWikiBigram2 ) wbw2 = WIKI_BIGRAM_WEIGHT;

	QueryTerm *qt1 = &q->m_qterms[qtn1];
	QueryTerm *qt2 = &q->m_qterms[qtn2];

	int64_t tf1 = qt1->m_termFreq;
	int64_t tf2 = qt2->m_termFreq;
	float tfw1 = ps->m_tfWeight1;
	float tfw2 = ps->m_tfWeight2;
	
	const char *wp = "no";
	float wiw = 1.0;
	if ( ps->m_inSameWikiPhrase ) {
		wp = "yes";
		wiw = WIKI_WEIGHT; // 0.50;
	}
	int32_t a = ps->m_wordPos2;
	int32_t b = ps->m_wordPos1;
	const char *es = "";
	const char *bes = "";
	if ( a < b ) {
		a = ps->m_wordPos1;
		b = ps->m_wordPos2;
		// out of query order penalty!
		es = "+ 1.0";
		bes = "+ <b>1.0</b>";
	}
	
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<pairInfo>\n");
		
		sb->safePrintf("\t\t\t<densityRank1>%" PRId32
			      "</densityRank1>\n",
			      (int32_t)de1);
		sb->safePrintf("\t\t\t<densityRank2>%" PRId32
			      "</densityRank2>\n",
			      (int32_t)de2);
		sb->safePrintf("\t\t\t<densityWeight1>%f"
			      "</densityWeight1>\n",
			      dnw1);
		sb->safePrintf("\t\t\t<densityWeight2>%f"
			      "</densityWeight2>\n",
			      dnw2);
		
		sb->safePrintf("\t\t\t<term1><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
				q->m_qterms[qtn1].m_termLen );
		sb->safePrintf("]]></term1>\n");
		sb->safePrintf("\t\t\t<term2><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
				q->m_qterms[qtn2].m_termLen );
		sb->safePrintf("]]></term2>\n");
		
		sb->safePrintf("\t\t\t<location1><![CDATA[%s]]>"
			      "</location1>\n",
			      getHashGroupString(hg1));
		sb->safePrintf("\t\t\t<location2><![CDATA[%s]]>"
			      "</location2>\n",
			      getHashGroupString(hg2));
		sb->safePrintf("\t\t\t<locationWeight1>%.01f"
			      "</locationWeight1>\n",
			      hgw1 );
		sb->safePrintf("\t\t\t<locationWeight2>%.01f"
			      "</locationWeight2>\n",
			      hgw2 );
		
		sb->safePrintf("\t\t\t<wordPos1>%" PRId32
			      "</wordPos1>\n", wp1 );
		sb->safePrintf("\t\t\t<wordPos2>%" PRId32
			      "</wordPos2>\n", wp2 );

		sb->safePrintf("\t\t\t<isSynonym1>"
			      "<![CDATA[%s]]>"
			      "</isSynonym1>\n",
			      syn1);
		sb->safePrintf("\t\t\t<isSynonym2>"
			      "<![CDATA[%s]]>"
			      "</isSynonym2>\n",
			      syn2);
		sb->safePrintf("\t\t\t<synonymWeight1>%.01f"
			      "</synonymWeight1>\n",
			      sw1);
		sb->safePrintf("\t\t\t<synonymWeight2>%.01f"
			      "</synonymWeight2>\n",
			      sw2);
		
		// word spam / link text weight
		const char *r1 = "wordSpamRank1";
		const char *r2 = "wordSpamRank2";
		const char *t1 = "wordSpamWeight1";
		const char *t2 = "wordSpamWeight2";
		if ( hg1 == HASHGROUP_INLINKTEXT ) {
			r1 = "inlinkSiteRank1";
			t1 = "inlinkTextWeight1";
		}
		if ( hg2 == HASHGROUP_INLINKTEXT ) {
			r2 = "inlinkSiteRank2";
			t2 = "inlinkTextWeight2";
		}
		sb->safePrintf("\t\t\t<%s>%" PRId32"</%s>\n",
			      r1,(int32_t)wr1,r1);
		sb->safePrintf("\t\t\t<%s>%" PRId32"</%s>\n",
			      r2,(int32_t)wr2,r2);
		sb->safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t1,wsw1,t1);
		sb->safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t2,wsw2,t2);
		

		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for (;k&&hg1==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > wp1 ) continue;
			if ( k->m_wordPosStart + 50 < wp1 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId1>%" PRId32
				      "</inlinkId1>\n",
				      k->m_siteHash);
		}

		k = info->getNextInlink(NULL);
		for (;k&&hg2==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > wp2 ) continue;
			if ( k->m_wordPosStart + 50 < wp2 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId2>%" PRId32
				      "</inlinkId2>\n",
				      k->m_siteHash);
		}

		// term freq
		sb->safePrintf("\t\t\t<termFreq1>%" PRId64
			      "</termFreq1>\n",tf1);
		sb->safePrintf("\t\t\t<termFreq2>%" PRId64
			      "</termFreq2>\n",tf2);
		sb->safePrintf("\t\t\t<termFreqWeight1>%f"
			      "</termFreqWeight1>\n",tfw1);
		sb->safePrintf("\t\t\t<termFreqWeight2>%f"
			      "</termFreqWeight2>\n",tfw2);
		
		sb->safePrintf("\t\t\t<isWikiBigram1>"
			      "%" PRId32"</isWikiBigram1>\n",
			      (int32_t)(ps->m_isHalfStopWikiBigram1));
		sb->safePrintf("\t\t\t<isWikiBigram2>"
			      "%" PRId32"</isWikiBigram2>\n",
			      (int32_t)(ps->m_isHalfStopWikiBigram2));
		
		sb->safePrintf("\t\t\t<wikiBigramWeight1>%.01f"
			      "</wikiBigramWeight1>\n",
			      wbw1);
		sb->safePrintf("\t\t\t<wikiBigramWeight2>%.01f"
			      "</wikiBigramWeight2>\n",
			      wbw2);
		
		sb->safePrintf("\t\t\t<inSameWikiPhrase>"
			      "<![CDATA[%s]]>"
			      "</inSameWikiPhrase>\n",
			      wp);
		
		sb->safePrintf("\t\t\t<queryDist>"
			      "%" PRId32
			      "</queryDist>\n",
			      ps->m_qdist );
		
		sb->safePrintf("\t\t\t<wikiWeight>"
			      "%.01f"
			      "</wikiWeight>\n",
			      wiw );
		
		sb->safePrintf("\t\t\t<score>%f</score>\n",
			      ps->m_finalScore);
		
		sb->safePrintf("\t\t\t<equationCanonical>"
			      "<![CDATA["
			      "score = "
			      " 100 * "
			      " locationWeight1" // hgw
			      " * "
			      " locationWeight2" // hgw
			      " * "
			      " synonymWeight1" // synweight
			      " * "
			      " synonymWeight2" // synweight
			      " * "
			      
			      " wikiBigramWeight1"
			      " * "
			      " wikiBigramWeight2"
			      " * "
			      
			      //"diversityWeight1"
			      //" * "
			      //"diversityWeight2"
			      //" * "
			      "densityWeight1" //density weight
			      " * "
			      "densityWeight2" //density weight
			      " * "
			      "%s" // wordspam weight
			      " * "
			      "%s" // wordspam weight
			      " * "
			      "termFreqWeight1" // tfw
			      " * "
			      "termFreqWeight2" // tfw
			      " / ( ||wordPos1 - wordPos2| "
			      " - queryDist| + 1.0 ) * "
			      "wikiWeight"
			      "]]>"
			      "</equationCanonical>\n"
			      , t1
			      , t2
			      );
		
		sb->safePrintf("\t\t\t<equation>"
			      "<![CDATA["
			      "%f="
			      "100*"
			      "<font color=orange>%.1f</font>"//hashgroupweight
			      "*"
			      "<font color=orange>%.1f</font>"//hashgroupweight
			      "*"
			      "<font color=blue>%.1f</font>" // syn weight
			      "*"
			      "<font color=blue>%.1f</font>" // syn weight
			      "*"
			      
			      "<font color=green>%.1f</font>"//wikibigramweight
			      "*"
			      "<font color=green>%.1f</font>"//wikibigramweight
			      "*"
			      
			      "<font color=purple>%.02f</font>"//density weight
			      "*"
			      "<font color=purple>%.02f</font>"//density weight
			      "*"
			      "<font color=red>%.02f</font>" // wordspam weight
			      "*"
			      "<font color=red>%.02f</font>" // wordspam weight
			      "*"
			      "<font color=magenta>%.02f</font>"//tf weight
			      "*"
			      "<font color=magenta>%.02f</font>"//tf weight
			      , ps->m_finalScore
			      , hgw1
			      , hgw2
			      , sw1
			      , sw2
			      , wbw1
			      , wbw2
			      , dnw1
			      , dnw2
			      , wsw1
			      , wsw2
			      , tfw1
			      , tfw2
			      );
		
		if ( ps->m_fixedDistance )
			sb->safePrintf(
				      "/<b>%" PRId32"</b> "
				      , (int32_t)FIXED_DISTANCE );
		else
			sb->safePrintf(
				      "/"
				      "(((<font color=darkgreen>%" PRId32"</font>"
				      "-<font color=darkgreen>%" PRId32"</font>"
				      ")-<font color=lime>%" PRId32"</font>)+1.0%s)"
				      ,
				      a,b,ps->m_qdist,bes);
		// wikipedia weight
		if ( !almostEqualFloat(wiw, 1.0) )
			sb->safePrintf("*%.01f", wiw );
		sb->safePrintf("]]>"
			      "</equation>\n" );
		sb->safePrintf("\t\t</pairInfo>\n");
		return true; // continue;
	}

	//
	// print first term in first row
	//
	sb->safePrintf("<tr><td rowspan=3>");

	sb->safePrintf("<a onclick=\""
		      "var e = document.getElementById('poo');"
		      "if ( e.style.display == 'none' ){"
		      "e.style.display = '';"
		      "}"
		      "else {"
		      "e.style.display = 'none';"
		      "}"
		      "\">"
		      );
	sb->safePrintf("%.04f</a></td>",ps->m_finalScore);

	sb->safePrintf("<td>"
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg1)
		      , hgw1 );

	// the word position
	sb->safePrintf("<td>");

	sb->safePrintf("<a href=\"/get?d=");

	sb->safePrintf("<a href=\"/get?d=");

	sb->safePrintf("%" PRId64
		       "&page=4"
		       //"&page=sections&"
		       "&hipos=%" PRId32
		       "&c=%s#hipos\">"
		       "%" PRId32"</a></td>"
		       "</a></td>"
		       ,mr->m_docId
		       ,(int32_t)ps->m_wordPos1
		       ,si->m_cr->m_coll
		       ,(int32_t)ps->m_wordPos1);

	sb->safePrintf("<td>%s <font color=blue>%.02f</font></td>",syn1,sw1);

	sb->safePrintf("<td>%s <font color=green>%.02f</font></td>",bs1,wbw1);


	// density
	sb->safePrintf("<td>%" PRId32" <font color=purple>"
		      "%.02f</font></td>",
		      (int32_t)de1,dnw1);
	// word spam
	if ( hg1 == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>");
		sb->safePrintf("<td>%" PRId32" <font color=red>"
			      "%.02f</font></td>",
			      (int32_t)wr1,wsw1);
	}
	else {
		sb->safePrintf("<td>%" PRId32, (int32_t)wr1);
		//if ( wsw1 != 1.0 )
			sb->safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw1);
		sb->safePrintf("</td>");
		sb->safePrintf("<td>&nbsp;</td>");
	}
	
	// term freq
	sb->safePrintf("<td id=tf>%" PRId64" <font color=magenta>"
		      "%.02f</font></td>",
		      tf1,tfw1);
	// inSamePhraseId distInQuery phraseWeight
	sb->safePrintf("<td>%s</td><td>%" PRId32"</td><td>%.01f</td>"
		       ,wp,ps->m_qdist,wiw);
	// end the row
	sb->safePrintf("</tr>");
	//
	// print 2nd term in 2nd row
	//
	sb->safePrintf("<tr><td>");

	sb->safePrintf(
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg2)
		      , hgw2 );

	// the word position
	sb->safePrintf("<td>");

	sb->safePrintf("<a href=\"/get?d=");

	sb->safePrintf("%" PRId64
		      "&page=4&"
		      "hipos=%" PRId32"&c=%s#hipos\">"
		      "%" PRId32"</a></td>"
		      "</a></td>"
		      ,mr->m_docId
		      ,(int32_t)ps->m_wordPos2
		      ,si->m_cr->m_coll
		      ,(int32_t)ps->m_wordPos2);

	sb->safePrintf("<td>%s <font color=blue>%.02f</font></td>",syn2,sw2);

	sb->safePrintf("<td>%s <font color=green>%.02f</font></td>",bs2,wbw2);
	
	// density
	sb->safePrintf("<td>%" PRId32" <font color=purple>"
		      "%.02f</font></td>",
		      (int32_t)de2,dnw2);
	// word spam
	if ( hg2 == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>");
		sb->safePrintf("<td>%" PRId32" <font color=red>"
			      "%.02f</font></td>",
			      (int32_t)wr2,wsw2);
	}
	else {
		sb->safePrintf("<td>%" PRId32, (int32_t)wr2);
		//if ( wsw2 != 1.0 )
			sb->safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw2);
		sb->safePrintf("</td>");
		sb->safePrintf("<td>&nbsp;</td>");
	}
	// term freq
	sb->safePrintf("<td id=tf>%" PRId64" <font color=magenta>"
		      "%.02f</font></td>",
		      tf2,tfw2);
	// inSamePhraseId distInQuery phraseWeight
	sb->safePrintf("<td>%s</td><td>%" PRId32"</td><td>%.01f</td>"
		       ,wp,ps->m_qdist,wiw);
	// end the row
	sb->safePrintf("</tr>");
	sb->safePrintf("<tr><td ");

	sb->safePrintf("colspan=50>" //  style=\"display:none\">"
		      "%.03f "
		      "= "
		      //" ( "
		      "100*"
		      "<font color=orange>%.1f"
		      "</font>"
		      "*"
		      "<font color=orange>%.1f"
		      "</font>"
		      "*"
		      //"(%" PRId32" - "
		      , ps->m_finalScore
		      //, idstr
		      , hgw1
		      , hgw2
		      //, (int32_t)MAXWORDPOS+1
		      );
	sb->safePrintf("<font color=blue>%.1f</font>"
		      "*"
		      " <font color=blue>%.1f</font>"
		      "*"
		      
		      // wiki bigram weight
		      "<font color=green>%.02f</font>"
		      "*"
		      "<font color=green>%.02f</font>"
		      "*"
		      
		      "<font color=purple>%.02f</font>"
		      "*"
		      "<font color=purple>%.02f</font>"
		      "*"
		      "<font color=red>%.02f</font>"
		      "*"
		      " <font color=red>%.02f</font>"
		      "*"
		      "<font color=magenta>%.02f</font>"
		      "*"
		      "<font color=magenta>%.02f</font>"
		      , sw1
		      , sw2
		      , wbw1
		      , wbw2
		      , dnw1
		      , dnw2
		      , wsw1
		      , wsw2
		      , tfw1
		      , tfw2
		      );
	if ( ps->m_fixedDistance )
		sb->safePrintf(
			      "/<b>%" PRId32"</b> "
			      , (int32_t)FIXED_DISTANCE );
	else
		sb->safePrintf(
			      "/"
			      "(((<font color=darkgreen>%" PRId32"</font>"
			      "-<font color=darkgreen>%" PRId32"</font>)-"
			      "<font color=lime>%" PRId32"</font>) + 1.0%s)"
			      ,
			      a,b,ps->m_qdist,bes);
	// wikipedia weight
	if ( !almostEqualFloat(wiw, 1.0) )
		sb->safePrintf("*%.01f", wiw );
	sb->safePrintf( // end formula
		      "</td></tr>"
		      //"</table>"
		      //"<br>");
		      );
	return true;
}

static bool printSingleTerm ( SafeBuf *sb , Query *q , SingleScore *ss ) {

	int32_t qtn = ss->m_qtermNum;

	sb->safePrintf("<table border=1 cellpadding=3>");
	sb->safePrintf("<tr><td colspan=50><center><b>");
	// link to rainbow page
	//sb->safePrintf("<a href=\"/print?u=");
	//sb->urlEncode( mr->ptr_ubuf );
	//sb->safePrintf("&page=4&recycle=1&c=%s\">",coll);
	if ( q->m_qterms[qtn].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn].m_term ,
			q->m_qterms[qtn].m_termLen );
	if ( q->m_qterms[qtn].m_isPhrase )
		sb->pushChar('\"');
	//sb->safePrintf("</a>");
	sb->safePrintf("</b></center></td></tr>");
	return true;
}

static bool printTermPairs ( SafeBuf *sb , Query *q , PairScore *ps ) {
	// print pair text
	int32_t qtn1 = ps->m_qtermNum1;
	int32_t qtn2 = ps->m_qtermNum2;
	sb->safePrintf("<table cellpadding=3 border=1>"
		      "<tr><td colspan=20><center><b>");
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
			q->m_qterms[qtn1].m_termLen );
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb->pushChar('\"');
	sb->safePrintf("</b> vs <b>");
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
			q->m_qterms[qtn2].m_termLen );
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb->pushChar('\"');
	return true;
}

static bool printScoresHeader ( SafeBuf *sb ) {

	sb->safePrintf("<tr>"
		      "<td>score</td>"
		      "<td>location</td>"
		      "<td>wordPos</td>"
		      "<td>synonym</td>"
		      "<td>wikibigram</td>"
		      //"<td>diversityRank</td>"
		      "<td>density</td>"
		      "<td>spam</td>"
		      "<td>inlinkPR</td>" // nlinkSiteRank</td>"
		      "<td>termFreq</td>"
		       "<td>inSamePhrase</td>"
		       "<td>distInQuery</td>"
		       "<td>phraseWeight</td>"
		      "</tr>\n" 
		      );
	return true;
}

static bool printSingleScore ( SafeBuf *sb, SearchInput *si, SingleScore *ss, Msg20Reply *mr ) {

	// shortcut
	Query *q = &si->m_q;

	//SafeBuf ft;
	// store in final score calc
	//if ( ft.length() ) ft.safePrintf(" + ");
	//ft.safePrintf("%f",ss->m_finalScore);
	const char *syn = "no";
	float sw = 1.0;
	if ( ss->m_isSynonym ) {
		syn = "yes";
		sw = g_conf.m_synonymWeight;
	}
	//char bf = ss->m_bflags;
	float wbw = 1.0;
	const char *bs = "no";
	if ( ss->m_isHalfStopWikiBigram ) {
		bs = "yes";
		wbw = WIKI_BIGRAM_WEIGHT;
	}
	float hgw = getHashGroupWeight(ss->m_hashGroup);
	float dnw = getDensityWeight(ss->m_densityRank);
	float wsw = getWordSpamWeight(ss->m_wordSpamRank);
	// HACK for inlink text!
	if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT )
		wsw = getLinkerWeight(ss->m_wordSpamRank);
	
	//int64_t tf = ss->m_termFreq;//ss->m_listSize;
	int32_t qtn = ss->m_qtermNum;
	//int64_t tf = msg40->m_msg3a.m_termFreqs[qtn];
	QueryTerm *qt = &q->m_qterms[qtn];
	int64_t tf = qt->m_termFreq;
	float tfw = ss->m_tfWeight;
	
	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"terminfo\": {\n");

		sb->safePrintf("\t\t\t\"densityRank\": %" PRId32",\n", (int32_t)ss->m_densityRank);
		sb->safePrintf("\t\t\t\"densityWeight\": %f,\n", dnw);
		sb->safePrintf("\t\t\t\"term\": \"%*.*s\",\n", (int)q->m_qterms[qtn].m_termLen, (int)q->m_qterms[qtn].m_termLen,q->m_qterms[qtn].m_term);
		
		sb->safePrintf("\t\t\t\"location\": \"%s\",\n", getHashGroupString(ss->m_hashGroup));
		sb->safePrintf("\t\t\t\"locationWeight\": %.01f,\n", hgw );
		sb->safePrintf("\t\t\t\"wordPos\": %" PRId32",\n", (int32_t)ss->m_wordPos );
		sb->safePrintf("\t\t\t\"isSynonym\": \"%s\",\n", syn);
		sb->safePrintf("\t\t\t\"synonymWeight\": %.01f,\n", sw);
		sb->safePrintf("\t\t\t\"isWikiBigram\": %" PRId32",\n", (int32_t)(ss->m_isHalfStopWikiBigram) );
		sb->safePrintf("\t\t\t\"wikiBigramWeight\": %.01f,\n", (float)WIKI_BIGRAM_WEIGHT);
		// word spam
		if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
			sb->safePrintf("\t\t\t\"inlinkSiteRank>%" PRId32",\n", (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t\"inlinkTextWeight\": %.02f,\n", wsw);
		}
		else {
			sb->safePrintf("\t\t\t\"wordSpamRank\": %" PRId32",\n", (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t\"wordSpamWeight\": %.02f,\n", wsw);
		}


		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		sb->safePrintf("\t\t\t\"inlinkIds\": [,\n");
		for ( ; k && ss->m_hashGroup==HASHGROUP_INLINKTEXT ;
		      k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > ss->m_wordPos ) continue;
			if ( k->m_wordPosStart + 50 < ss->m_wordPos ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t\t\"%" PRId32",\n", k->m_siteHash);
		}
		sb->safePrintf("\t\t\t],\n");

		// term freq
		sb->safePrintf("\t\t\t\"termFreq\": %" PRId64",\n", tf);
		sb->safePrintf("\t\t\t\"termFreqWeight\": %f,\n", tfw);
		
		sb->safePrintf("\t\t\t\"score\": %f,\n", ss->m_finalScore);
		
		sb->safePrintf("\t\t\t\"equationCanonical\": \""
			      "score = "
			      " 100 * "
			      " locationWeight" // hgw
			      " * "
			      " locationWeight" // hgw
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      " wikiBigramWeight"
			      " * "
			      " wikiBigramWeight"
			      " * "
			      //" diversityWeight" // divweight
			      //" * "
			      //" diversityWeight" // divweight
			      //" * "
			      "densityWeight" // density weight
			      " * "
			      "densityWeight" // density weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "termFreqWeight" // tfw
			      " * "
			      "termFreqWeight" // tfw
			      "\",\n"
			      );
		
		sb->safePrintf("\t\t\t\"equation>\": \""
			      "%f="
			      "100*"
			      "%.1f" // hgw
			      "*"
			      "%.1f" // hgw
			      "*"
			      "%.1f" // synweight
			      "*"
			      "%.1f" // synweight
			      "*"
			      "%.02f" // wikibigram weight
			      "*"
			      "%.02f" // wikibigram weight
			      "*"
			      "%.02f" // density weight
			      "*"
			      "%.02f" // density weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // tfw
			      "*"
			      "%.02f" // tfw
			      "\"\n"
			      , ss->m_finalScore
			      , hgw
			      , hgw
			      , sw
			      , sw
			      , wbw
			      , wbw
			      , dnw
			      , dnw
			      , wsw
			      , wsw
			      , tfw
			      , tfw
			      );
		sb->safePrintf("\t\t}");
		return true;
	}

	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<termInfo>\n");

		sb->safePrintf("\t\t\t<densityRank>%" PRId32
			      "</densityRank>\n",
			      (int32_t)ss->m_densityRank);
		sb->safePrintf("\t\t\t<densityWeight>%f"
			      "</densityWeight>\n",
			      dnw);
		sb->safePrintf("\t\t\t<term><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn].m_term ,
				q->m_qterms[qtn].m_termLen );
		sb->safePrintf("]]></term>\n");
		
		sb->safePrintf("\t\t\t<location><![CDATA[%s]]>"
			      "</location>\n",
			      getHashGroupString(ss->m_hashGroup));
		sb->safePrintf("\t\t\t<locationWeight>%.01f"
			      "</locationWeight>\n",
			      hgw );
		sb->safePrintf("\t\t\t<wordPos>%" PRId32
			      "</wordPos>\n", (int32_t)ss->m_wordPos );
		sb->safePrintf("\t\t\t<isSynonym>"
			      "<![CDATA[%s]]>"
			      "</isSynonym>\n",
			      syn);
		sb->safePrintf("\t\t\t<synonymWeight>%.01f"
			      "</synonymWeight>\n",
			      sw);
		sb->safePrintf("\t\t\t<isWikiBigram>%" PRId32
			      "</isWikiBigram>\n",
			      (int32_t)(ss->m_isHalfStopWikiBigram) );
		sb->safePrintf("\t\t\t<wikiBigramWeight>%.01f"
			      "</wikiBigramWeight>\n",
			      (float)WIKI_BIGRAM_WEIGHT);
		// word spam
		if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
			sb->safePrintf("\t\t\t<inlinkSiteRank>%" PRId32
				      "</inlinkSiteRank>\n",
				      (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t<inlinkTextWeight>%.02f"
				      "</inlinkTextWeight>\n",
				      wsw);
		}
		else {
			sb->safePrintf("\t\t\t<wordSpamRank>%" PRId32
				      "</wordSpamRank>\n",
				      (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t<wordSpamWeight>%.02f"
				      "</wordSpamWeight>\n",
				      wsw);
		}


		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for ( ; k && ss->m_hashGroup==HASHGROUP_INLINKTEXT ; 
		      k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > ss->m_wordPos ) continue;
			if ( k->m_wordPosStart + 50 < ss->m_wordPos ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId>%" PRId32
				      "</inlinkId>\n",
				      k->m_siteHash);
		}

		// term freq
		sb->safePrintf("\t\t\t<termFreq>%" PRId64
			      "</termFreq>\n",tf);
		sb->safePrintf("\t\t\t<termFreqWeight>%f"
			      "</termFreqWeight>\n",tfw);
		
		sb->safePrintf("\t\t\t<score>%f</score>\n",
			      ss->m_finalScore);
		
		sb->safePrintf("\t\t\t<equationCanonical>"
			      "<![CDATA["
			      "score = "
			      " 100 * "
			      " locationWeight" // hgw
			      " * "
			      " locationWeight" // hgw
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      
			      " wikiBigramWeight"
			      " * "
			      " wikiBigramWeight"
			      " * "
			      
			      //" diversityWeight" // divweight
			      //" * "
			      //" diversityWeight" // divweight
			      //" * "
			      "densityWeight" // density weight
			      " * "
			      "densityWeight" // density weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "termFreqWeight" // tfw
			      " * "
			      "termFreqWeight" // tfw
			      //" / ( 3.0 )"
			      "]]>"
			      "</equationCanonical>\n"
			      );
		
		sb->safePrintf("\t\t\t<equation>"
			      "<![CDATA["
			      "%f="
			      "100*"
			      "%.1f" // hgw
			      "*"
			      "%.1f" // hgw
			      "*"
			      
			      "%.1f" // synweight
			      "*"
			      "%.1f" // synweight
			      "*"
			      
			      
			      "%.02f" // wikibigram weight
			      "*"
			      "%.02f" // wikibigram weight
			      "*"
			      
			      "%.02f" // density weight
			      "*"
			      "%.02f" // density weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // tfw
			      "*"
			      "%.02f" // tfw
			      //" / ( 3.0 )"
			      "]]>"
			      "</equation>\n"
			      , ss->m_finalScore
			      , hgw
			      , hgw
			      , sw
			      , sw
			      , wbw
			      , wbw
			      , dnw
			      , dnw
			      , wsw
			      , wsw
			      , tfw
			      , tfw
			      );
		sb->safePrintf("\t\t</termInfo>\n");
		return true;
	}



	sb->safePrintf("<tr>"
		      "<td rowspan=2>%.03f</td>\n"
		      "<td>%s <font color=orange>%.1f"
		      "</font></td\n>"
		      // wordpos
		      "<td>"
		      "<a href=\"/get?d=" 
		      , ss->m_finalScore
		      , getHashGroupString(ss->m_hashGroup)
		      , hgw
		      );
	//sb->urlEncode( mr->ptr_ubuf );
	sb->safePrintf("%" PRId64,mr->m_docId );
	sb->safePrintf("&page=4&"
		      "hipos=%" PRId32"&c=%s#hipos\">"
		      ,(int32_t)ss->m_wordPos
		      ,si->m_cr->m_coll);
	sb->safePrintf("%" PRId32"</a></td>\n"
		      "<td>%s <font color=blue>%.1f"
		      "</font></td>\n" // syn
		      
		      // wikibigram?/weight
		      "<td>%s <font color=green>%.02f</font></td>\n"
		      
		      //"<td>%" PRId32"/<font color=green>%f"
		      //"</font></td>" // diversity
		      "<td>%" PRId32" <font color=purple>"
		      "%.02f</font></td>\n" // density
		      , (int32_t)ss->m_wordPos
		      , syn
		      , sw // synonym weight
		      , bs
		      , wbw
		      //, (int32_t)ss->m_diversityRank
		      //, dvw
		      , (int32_t)ss->m_densityRank
		      , dnw
		      );
	if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>"
			      "<td>%" PRId32" <font color=red>%.02f"
			      "</font></td>\n" // wordspam
			      , (int32_t)ss->m_wordSpamRank
			      , wsw
			      );
	}
	else {
		sb->safePrintf("<td>%" PRId32" <font color=red>%.02f"
			      "</font></td>" // wordspam
			      "<td>&nbsp;</td>\n"
			      , (int32_t)ss->m_wordSpamRank
			      , wsw
			      );
		
	}
	
	sb->safePrintf("<td id=tf>%" PRId64" <font color=magenta>"
		      "%.02f</font></td>\n" // termfreq
		      "</tr>\n"
		      , tf
		      , tfw
		      );
	// last row is the computation of score
	sb->safePrintf("<tr><td colspan=50>"
		      "%.03f "
		      " = "
		      //" %" PRId32" * "
		      "100 * "
		      " <font color=orange>%.1f</font>"
		      " * "
		      " <font color=orange>%.1f</font>"
		      " * "
		      " <font color=blue>%.1f</font>"
		      " * "
		      " <font color=blue>%.1f</font>"
		      " * "
		      " <font color=green>%.02f</font>"//wikibigramwght
		      " * "
		      " <font color=green>%.02f</font>"
		      " * "
		      "<font color=purple>%.02f</font>"
		      " * "
		      "<font color=purple>%.02f</font>"
		      " * "
		      "<font color=red>%.02f</font>"
		      " * "
		      "<font color=red>%.02f</font>"
		      " * "
		      "<font color=magenta>%.02f</font>"
		      " * "
		      "<font color=magenta>%.02f</font>"
		      //" / ( 3.0 )"
		      // end formula
		      "</td></tr>\n"
		      , ss->m_finalScore
		      //, (int32_t)MAXWORDPOS+1
		      , hgw
		      , hgw
		      , sw
		      , sw
		      , wbw
		      , wbw
		      //, dvw
		      //, dvw
		      , dnw
		      , dnw
		      , wsw
		      , wsw
		      , tfw
		      , tfw
		      );
	//sb->safePrintf("</table>"
	//	      "<br>");
	return true;
}

// if catId >= 1 then print the dmoz radio button
static bool printLogoAndSearchBox ( SafeBuf *sb, HttpRequest *hr, SearchInput *si ) {
	const char *coll = hr->getString("c");
	if ( ! coll ) coll = "";

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	const char *method = "GET";
	if ( si && si->m_sites && strlen(si->m_sites)>800 ) {
		method = "POST";
	}

	sb->safePrintf(
		      //
		      // search box
		      //
		      "<form name=f method=%s action=/search>\n\n" 

		      // propagate the collection if they re-search
		      "<input name=c type=hidden value=\"%s\">"
		       , method
		      , coll
		      );

	// propagate prepend
	const char *prepend = hr->getString("prepend");
	if ( prepend ) {
		sb->safePrintf("<input name=prepend type=hidden value=\"");
		sb->htmlEncode ( prepend, strlen(prepend), false);
		sb->safePrintf("\">");
	}
	

	// put search box in a box
	sb->safePrintf(
		       "<br>"
		       "<br>"
		       "<br>"
		       "<div style="
		       "background-color:#fcc714;"
		       "border-style:solid;"
		       "border-width:3px;"
		       "border-color:blue;"
		       //"background-color:blue;"
		       "padding:20px;"
		       "border-radius:20px;"
		       ">");


	sb->safePrintf (
			//"<div style=margin-left:5px;margin-right:5px;>
			"<input size=40 type=text name=q "

			"style=\""
			//"width:%" PRId32"px;"
			"height:26px;"
			"padding:0px;"
			"font-weight:bold;"
			"padding-left:5px;"
			//"border-radius:10px;"
			"margin:0px;"
			"border:1px inset lightgray;"
			"background-color:#ffffff;"
			"font-size:18px;"
			"\" "


			"value=\""
			);

	// contents of search box
	int32_t  qlen;
	const char *qstr = hr->getString("q",&qlen,"",NULL);
	sb->htmlEncode ( qstr , qlen , false );

	// if it was an advanced search, this can be empty
	if ( qlen == 0 && si && si->m_displayQuery )
		sb->htmlEncode ( si->m_displayQuery );

	sb->safePrintf ("\">"
			"&nbsp; &nbsp;"

			"<div onclick=document.f.submit(); "

			" onmouseover=\""
			"this.style.backgroundColor='lightgreen';"
			"this.style.color='black';\""
			" onmouseout=\""
			"this.style.backgroundColor='green';"
			"this.style.color='white';\" "

			"style=border-radius:28px;"
			"cursor:pointer;"
			"cursor:hand;"
			"border-color:white;"
			"border-style:solid;"
			"border-width:3px;"
			"padding:12px;"
			"width:20px;"
			"height:20px;"
			"display:inline-block;"
			"background-color:green;color:white;>"
			"<b style=margin-left:-5px;font-size:18px;"
			">GO</b>"
			"</div>"
			);

	sb->safePrintf(	"</div>"
			"<br>"
			"<br>"
		       );


	printSearchFiltersBar ( sb , hr );
	

	sb->safePrintf( "</form>\n" );
	return true;
}


#include "Json.h"


class MenuItem {
public:
	int32_t  m_menuNum;
	const char *m_title;
	// we append this to the url
	const char *m_cgi;
	char  m_tmp[25];
};


const static int MAX_MENU_ENTRIES = 200;

static MenuItem s_mi[MAX_MENU_ENTRIES];
static int32_t s_num = 0;

static int getNextMenuEntryIndex(int curr_index)
{
	int i = curr_index+1;
	if( i >= MAX_MENU_ENTRIES )
	{
		g_process.shutdownAbort(true);
	}
	return i;
}

static bool printSearchFiltersBar ( SafeBuf *sb , HttpRequest *hr ) {
	SafeBuf cu;
	hr->getCurrentUrl ( cu );


	sb->safePrintf("<script>"
		       "function show(id){"
		       "var e = document.getElementById(id);"
		       "if ( e.style.display == 'none' ){"
		       "e.style.display = '';"
		       "}"
		       "else {"
		       "e.style.display = 'none';"
		       "}"
		       "}"
		       "</script>"
		       );


	static bool s_init = false;

	if ( ! s_init ) {

		int32_t menuNum = 0;
		int32_t n = 0;

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Any time";
		s_mi[n].m_cgi      = "secsback=0";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Past 24 hours";
		s_mi[n].m_cgi      = "secsback=86400";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Past week";
		s_mi[n].m_cgi      = "secsback=604800";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Past month";
		s_mi[n].m_cgi      = "secsback=2592000";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Past year";
		s_mi[n].m_cgi      = "secsback=31536000";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// sort by
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Sorted by relevance";
		s_mi[n].m_cgi      = "sortby=0";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Sorted by date";
		s_mi[n].m_cgi      = "sortby=1";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Reverse sorted by date";
		s_mi[n].m_cgi      = "sortby=2";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// languages
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Any language";
		s_mi[n].m_cgi      = "qlang=xx";
		n = getNextMenuEntryIndex(n);

		for ( int32_t i = 0 ; i < langLast ; i++ ) {
			s_mi[n].m_menuNum  = menuNum;
			s_mi[n].m_title    = getLanguageString(i);
			const char *abbr = getLanguageAbbr(i);
			snprintf(s_mi[n].m_tmp,10,"qlang=%s",abbr);
			s_mi[n].m_cgi      = s_mi[n].m_tmp;
			n = getNextMenuEntryIndex(n);
		}

		++menuNum;

		// filetypes
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Any filetype";
		s_mi[n].m_cgi      = "filetype=any";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "HTML";
		s_mi[n].m_cgi      = "filetype=html";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "TEXT";
		s_mi[n].m_cgi      = "filetype=txt";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "PDF";
		s_mi[n].m_cgi      = "filetype=pdf";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Microsoft Word";
		s_mi[n].m_cgi      = "filetype=doc";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "XML";
		s_mi[n].m_cgi      = "filetype=xml";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "JSON";
		s_mi[n].m_cgi      = "filetype=json";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Excel";
		s_mi[n].m_cgi      = "filetype=xls";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "PostScript";
		s_mi[n].m_cgi      = "filetype=ps";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Spider Status";
		s_mi[n].m_cgi      = "filetype=status";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// output
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Output HTML";
		s_mi[n].m_cgi      = "format=html";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Output XML";
		s_mi[n].m_cgi      = "format=xml";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Output JSON";
		s_mi[n].m_cgi      = "format=json";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Output CSV";
		s_mi[n].m_cgi      = "format=csv";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// show/hide banned
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Hide banned results";
		s_mi[n].m_cgi      = "sb=0";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Show banned results";
		s_mi[n].m_cgi      = "sb=1";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// spider status
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Hide Spider Log";
		s_mi[n].m_cgi      = "splog=0";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Show Spider Log";
		s_mi[n].m_cgi      = "q=type:status";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// family filter
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Family Filter Off";
		s_mi[n].m_cgi      = "ff=0";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Family Filter On";
		s_mi[n].m_cgi      = "ff=1";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// META TAGS
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "No Meta Tags";
		s_mi[n].m_cgi      = "dt=";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Show Meta Tags";
		s_mi[n].m_cgi      = "dt=keywords+description";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// ADMIN
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Show Admin View";
		s_mi[n].m_cgi      = "admin=1";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "Show User View";
		s_mi[n].m_cgi      = "admin=0";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// fx_country
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "fx_country (none)";
		s_mi[n].m_cgi      = "fx_country=";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "de";
		s_mi[n].m_cgi      = "fx_country=de";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "dk";
		s_mi[n].m_cgi      = "fx_country=dk";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "fr";
		s_mi[n].m_cgi      = "fx_country=fr";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "no";
		s_mi[n].m_cgi      = "fx_country=no";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "se";
		s_mi[n].m_cgi      = "fx_country=se";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// fx_blang
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "fx_blang (none)";
		s_mi[n].m_cgi      = "fx_blang=";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "da";
		s_mi[n].m_cgi      = "fx_blang=da";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "de";
		s_mi[n].m_cgi      = "fx_blang=de";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "en";
		s_mi[n].m_cgi      = "fx_blang=en";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "en-US";
		s_mi[n].m_cgi      = "fx_blang=en-US";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "no";
		s_mi[n].m_cgi      = "fx_blang=no";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "se";
		s_mi[n].m_cgi      = "fx_blang=se";
		n = getNextMenuEntryIndex(n);

		++menuNum;

		// fx_fetld
		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "fx_fetld (none)";
		s_mi[n].m_cgi      = "fx_fetld=";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "com";
		s_mi[n].m_cgi      = "fx_fetld=com";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "de";
		s_mi[n].m_cgi      = "fx_fetld=de";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "dk";
		s_mi[n].m_cgi      = "fx_fetld=dk";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "no";
		s_mi[n].m_cgi      = "fx_fetld=no";
		n = getNextMenuEntryIndex(n);

		s_mi[n].m_menuNum  = menuNum;
		s_mi[n].m_title    = "se";
		s_mi[n].m_cgi      = "fx_fetld=se";
		n = getNextMenuEntryIndex(n);

		s_num = n;
		s_init = true;
	}


	// we'll print the admin menu custom since it's mostly off-page links

	// bar of drop down menus
	sb->safePrintf("<div style=color:gray;>");

	for ( int32_t i = 0 ; i <= s_mi[s_num-1].m_menuNum ; i++ ) {
		// after 5 make a new line
		if ( i % 5 == 0 ) sb->safePrintf("<br><br>");

		printMenu ( sb , i , hr );
	}

	sb->safePrintf("</div>\n");
	sb->safePrintf("<br>\n");

	return true;
}

static bool printMenu ( SafeBuf *sb , int32_t menuNum , HttpRequest *hr ) {

	bool firstOne = true;

	MenuItem *first = NULL;

	char *src    = hr->m_origUrlRequest;
	int32_t  srcLen = hr->m_origUrlRequestLen;

	const char *frontTag = "";
	const char *backTag = "";

	bool isDefaultHeader = true;

	// try to set first based on what's in the url
	for ( int32_t i = 0 ; i < s_num ; i++ ) {
		// shortcut
		MenuItem *mi = &s_mi[i];
		// skip if not our item
		if ( mi->m_menuNum != menuNum ) continue;

		// is it in the url
		const char *match = strnstr ( src, mi->m_cgi, srcLen );

		// or if empty quotes it is the true header like
		// for 'hide spider log' option
		if ( ! match ) {
			isDefaultHeader = false;
			continue;
		}
		// ensure ? or & preceeds
		if ( match > src && match[-1] != '?' && match[-1] != '&' )
			continue;
		// and \0 or & follows
		int32_t milen = strlen(mi->m_cgi);
		if ( match+milen > src+srcLen ) continue;
		if ( ! is_wspace_a(match[milen]) && match[milen] != '&' ) 
			continue;
		// got it
		first = mi;
		// do not highlight the orig header
		if ( isDefaultHeader ) break;
		frontTag = "<b style=color:maroon;>";
		backTag = "</b>";
		break;
	}
	

	for ( int32_t i = 0 ; i < s_num ; i++ ) {

		// shortcut
		MenuItem *mi = &s_mi[i];

		// skip if not our item
		if ( mi->m_menuNum != menuNum ) continue;

		if ( ! first ) first = mi;

		if ( ! firstOne ) goto skip;

		firstOne = false;

		// for centering the dropdown
		sb->safePrintf("<span style=position:relative;></span>");

		// print hidden drop down menu
		sb->safePrintf(
			       "<span id=menu%" PRId32" style=\"display:none;"
			       "position:absolute;"
			       //"margin-left:-20px;"
			       "margin-top:15px;"
			       "width:150px;"
			       "max-height:300px;"
			       "overflow-y:auto;"
			       "background-color:white;"
			       "padding:10px;"
			       "width=80px;border-width:1px;"
			       "border-color:lightgray;"
			       "box-shadow: -.5px 1px 1px gray;"
			       "border-style:solid;color:gray;\" "

			       //" onmouseout=\""
			       //"this.style.display='none';\""

			       // if clicking on scrollbar do not hide menu!
			       " onmousedown=\"inmenuclick=1;\" "

			       ">"
			       , mi->m_menuNum
			       );

	skip:

		
		// . add our cgi to the original url
		// . so if it has &qlang=de and they select &qlang=en
		//   we have to replace it... etc.
		char tmp2[512];
		SafeBuf newUrl(tmp2, 512);
		replaceParm ( mi->m_cgi , &newUrl , hr );
		newUrl += '\0';

		// print each item in there
		sb->safePrintf("<a href=%s>"
			       "<div style=cursor:pointer;cursor:hand;"
			       "padding-top:10px;"
			       "padding-bottom:10px;"
			       "color:gray;"

			       " onmouseover=\""
			       "this.style.backgroundColor='#e0e0e0';\" "
			       " onmouseout=\""
			       "this.style.backgroundColor='white';\" "

			       // prevent the body onmousedown from 
			       // hiding the menu
			       " onmousedown=\"inmenuclick=1;\" "

			       ">"
			       "<nobr>"
			       , newUrl.getBufStart()
			       );

		// print checkmark (check mark) next to selected one
		// if not the default (trueHeader)
		if ( mi == first ) // ! isDefaultHeader && mi == first )
			sb->safePrintf("<b style=color:black;>%c%c%c</b>",
				       0xe2,0x9c,0x93);
		else 
			sb->safePrintf("&nbsp; &nbsp; ");

		sb->safePrintf(" %s</nobr>"
			       "</div>"
			       "</a>"
			       , mi->m_title );

		//sb->safePrintf("<br><br>");
	}

	// wrap up the drop down
	sb->safePrintf("</span>");
	


	// print heading or current selection i guess
	sb->safePrintf(
		       // separate menus with these two spaces
		       " &nbsp; &nbsp; "
		       // print the menu header that when clicked
		       // will show the drop down
		       "<span style=cursor:pointer;"
		       "cursor:hand; "

		       "onmousedown=\"this.style.color='red';"
		       "inmenuclick=1;"
		       "\" "

		       "onmouseup=\"this.style.color='gray';"

		       // close any other open menu
		       "if ( openmenu !='') {"
		       "document.getElementById(openmenu)."
		       "style.display='none'; "
		       "var saved=openmenu;"
		       "openmenu='';"
		       // don't reopen our same menu below!
		       "if ( saved=='menu%" PRId32"') return;"
		       "}"

		       // show our menu
		       "show('menu%" PRId32"'); "
		       // we are now open
		       "openmenu='menu%" PRId32"'; "

		       "\""
		       ">"

		       "%s%s%s %c%c%c" 
		       "</span>"
		       , first->m_menuNum
		       , first->m_menuNum
		       , first->m_menuNum
		       , frontTag
		       , first->m_title
		       , backTag
		       // print triangle
		       ,0xe2
		       ,0x96
		       ,0xbc
		       );


	return true;
}

static bool replaceParm ( const char *cgi , SafeBuf *newUrl , HttpRequest *hr ) { 
	if ( ! cgi[0] ) return true;
	// get original request url. this is not \0 terminated
	char *src    = hr->m_origUrlRequest;
	int32_t  srcLen = hr->m_origUrlRequestLen;
	return replaceParm2 ( cgi ,newUrl, src, srcLen );
}

static bool replaceParm2 ( const char *cgi , SafeBuf *newUrl , 
			   const char *oldUrl , int32_t oldUrlLen ) {

	const char *src    = oldUrl;
	int32_t  srcLen = oldUrlLen;

	const char *srcEnd = src + srcLen;

	const char *equal = strstr(cgi,"=");
	if ( ! equal ) {
		log(LOG_WARN, "results: %s has no equal sign", cgi);
		return false;
	}
	int32_t cgiLen = equal - cgi;

	const char *p = src;

 tryagain:

	const char *found = strncasestr ( p , cgi , srcEnd - p , cgiLen );

	// if no ? or & before it it is bogus!
	if ( found && found[-1] != '&' && found[-1] != '?' ) {
		// try again
		p = found + 1;
		goto tryagain;
	}
		
	// fix &s= replaceing &sb=
	if ( found && found[cgiLen] != '=' ) {
		// try again
		p = found + 1;
		goto tryagain;
	}


	// if no collision, just append it
	if ( ! found ) {
		if ( ! newUrl->safeMemcpy ( src , srcLen ) ) return false;
		if ( ! newUrl->pushChar('&') ) return false;
		if ( ! newUrl->safeStrcpy ( cgi ) ) return false;
		if ( ! newUrl->nullTerm() ) return false;
		return true;
	}

	// . otherwise we have to replace it
	// . copy up to where it starts
	if ( ! newUrl->safeMemcpy ( src , found-src ) ) return false;
	// then insert our new cgi there
	if ( ! newUrl->safeStrcpy ( cgi ) ) return false;
	// then resume it
	const char *foundEnd = strncasestr ( found , "&" , srcEnd - found );
	// if nothing came after...
	if ( ! foundEnd ) {
		if ( ! newUrl->nullTerm() ) return false;
		return true;
	}
	// copy over what came after
	if ( ! newUrl->safeMemcpy ( foundEnd, srcEnd-foundEnd ) ) return false;
	if ( ! newUrl->nullTerm() ) return false;
	return true;
}

static bool printMetaContent ( Msg40 *msg40 , int32_t i , State0 *st, SafeBuf *sb ) {
	// store the user-requested meta tags content
	SearchInput *si = &st->m_si;
	char *pp      =      si->m_displayMetas;
	char *ppend   = pp + strlen(si->m_displayMetas);
	Msg20 *m = msg40->m_msg20[i];//getMsg20(i);
	Msg20Reply *mr = m->m_r;
	char *dbuf    = mr->ptr_dbuf;//msg40->getDisplayBuf(i);
	int32_t  dbufLen = mr->size_dbuf-1;//msg40->getDisplayBufLen(i);
	char *dbufEnd = dbuf + (dbufLen-1);
	char *dptr    = dbuf;
	//bool  printedSomething = false;
	// loop over the names of the requested meta tags
	while ( pp < ppend && dptr < dbufEnd ) {
		// . assure last byte of dbuf is \0
		//   provided dbufLen > 0
		// . this insures sprintf and strlen won't
		//   crash on dbuf/dptr
		if ( dbuf [ dbufLen ] != '\0' ) {
			log(LOG_LOGIC,"query: Meta tag buffer has no \\0.");
			break;
		}
		// skip initial spaces
		while ( pp < ppend && is_wspace_a(*pp) ) pp++;
		// break if done
		if ( ! *pp ) break;
		// that's the start of the meta tag name
		char *ss = pp;
		// . find end of that meta tag name
		// . can end in :<integer> -- specifies max len
		while ( pp < ppend && ! is_wspace_a(*pp) && 
			*pp != ':' ) pp++;
		// save current char
		char  c  = *pp;
		char *cp = pp;
		// NULL terminate the name
		*pp++ = '\0';
		// if ':' was specified, skip the rest
		if ( c == ':' ) while ( pp < ppend && ! is_wspace_a(*pp)) pp++;
		// print the name
		//int32_t sslen = strlen ( ss   );
		//int32_t ddlen = strlen ( dptr );
		int32_t ddlen = dbufLen;
		//if ( p + sslen + ddlen + 100 > pend ) continue;
		// newspaperarchive wants tags printed even if no value
		// make sure the meta tag isn't fucked up
		for ( int32_t ti = 0; ti < ddlen; ti++ ) {
			if ( dptr[ti] == '"' ||
			     dptr[ti] == '>' ||
			     dptr[ti] == '<' ||
			     dptr[ti] == '\r' ||
			     dptr[ti] == '\n' ||
			     dptr[ti] == '\0' ) {
				ddlen = ti;
				break;
			}
		}

		if ( ddlen > 0 ) {
			// ship it out
			if ( si->m_format == FORMAT_XML ) {
				sb->safePrintf ( "\t\t<display name=\"%s\">"
					  	"<![CDATA[", ss );
				cdataEncode(sb, dptr, ddlen);
				sb->safePrintf ( "]]></display>\n" );
			}
			else if ( si->m_format == FORMAT_JSON ) {
				sb->safePrintf ( "\t\t\"display.%s\":\"",ss);
				sb->jsonEncode ( dptr, ddlen );
				sb->safePrintf ( "\",\n");
			}
			// otherwise, print in light gray
			else {
				sb->safePrintf("<font color=#c62939>"
					      "<b>%s</b>: ", ss );
				sb->safeMemcpy ( dptr, ddlen );
				sb->safePrintf ( "</font><br>" );
			}
		}
		// restore tag name buffer
		*cp = c;
		// point to next content of tag to display
		dptr += ddlen + 1;
	}
	return true;
}
