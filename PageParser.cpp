#include "PageParser.h"
#include "XmlDoc.h"
#include "Pages.h"
#include "HttpServer.h"
#include "HttpRequest.h"
#include "Process.h"
#include "Conf.h"
#include "Mem.h"
#include "Errno.h"


class State8 {
public:
	SafeBuf m_dbuf;
	const char *m_u;
	int32_t  m_ulen;
	char  m_coll[MAX_COLL_LEN];
	int32_t  m_collLen;

	TcpSocket  *m_s;
	char  m_pwd[32];
	HttpRequest m_r;
	int32_t m_old;
	// recyle the link info from the title rec?
	int32_t m_recycle;
	bool m_render;
	char m_linkInfoColl[11];

	// m_pbuf now points to m_sbuf if we are showing the parsing junk
	SafeBuf  m_xbuf;
	SafeBuf  m_wbuf;

	// these are from rearranging the code
	int32_t      m_indexCode;

	// call Msg16 with a versino of title rec to do
	int32_t m_titleRecVersion;

	XmlDoc m_xd;
};

// TODO: meta redirect tag to host if hostId not ours
static bool processLoop(void *state);
static bool sendErrorReply(void *state, int32_t err);


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
// . returns false if blocked, true otherwise
// . TODO: don't close this socket until httpserver returns!!
bool sendPageParser(TcpSocket *s, HttpRequest *r) {
	// might a simple request to addsomething to validated.*.txt file
	// from XmlDoc::print() or XmlDoc::validateOutput()
	const char *uh64str = r->getString("uh64",NULL);
	if ( uh64str ) {
		// make basic reply
		const char *reply = "HTTP/1.0 200 OK\r\n"
			"Connection: Close\r\n";
		// that is it! send a basic reply ok
		bool status = g_httpServer.sendDynamicPage( s , 
							    reply,
							    strlen(reply),
							    -1, //cachtime
							    false ,//postreply?
							    NULL, //ctype
							    -1 , //httpstatus
							    NULL,//cookie
							    "utf-8");
		return status;
	}

	// make a state
	State8 *st;
	try { st = new (State8); }
	catch(std::bad_alloc&) {
		g_errno = ENOMEM;
		log("PageParser: new(%i): %s",
		    (int)sizeof(State8),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,
					       mstrerror(g_errno));}
	mnew ( st , sizeof(State8) , "PageParser" );

	st->m_indexCode       = 0;
	st->m_u               = NULL;

	// password, too
	int32_t pwdLen = 0;
	const char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	if ( pwdLen > 0 ) strncpy ( st->m_pwd , pwd , pwdLen );
	st->m_pwd[pwdLen]='\0';

	// save socket ptr
	st->m_s = s;
	st->m_r.copy ( r );
	// get the collection
	const char *coll    = r->getString ( "c" , &st->m_collLen ,NULL /*default*/);
	if ( st->m_collLen > MAX_COLL_LEN )
		return sendErrorReply ( st , ENOBUFS );
	if ( ! coll )
		return sendErrorReply ( st , ENOCOLLREC );
	strcpy ( st->m_coll , coll );

	// version to use, if -1 use latest
	st->m_titleRecVersion = r->getLong("version",-1);
	if ( st->m_titleRecVersion == -1 ) 
		st->m_titleRecVersion = TITLEREC_CURRENT_VERSION;

	int32_t  old     = r->getLong   ( "old", 0 );

	// set url in state class (may have length 0)
	st->m_u = st->m_r.getString("u",&st->m_ulen,NULL);
	// should we recycle link info?
	st->m_recycle  = r->getLong("recycle",0);

	st->m_render   = r->getLong("render" ,0) ? true : false;

	int32_t  linkInfoLen  = 0;
	// default is NULL
	const char *linkInfoColl = r->getString ( "oli" , &linkInfoLen, NULL );
	if ( linkInfoColl ) strcpy ( st->m_linkInfoColl , linkInfoColl );
	else st->m_linkInfoColl[0] = '\0';
	
	// should we use the old title rec?
	st->m_old    = old;

	// header
	SafeBuf *xbuf = &st->m_xbuf;
	xbuf->safePrintf("<meta http-equiv=\"Content-Type\" "
			 "content=\"text/html; charset=utf-8\">\n");

	// print standard header
	g_pages.printAdminTop ( xbuf , st->m_s , &st->m_r );


	// print the standard header for admin pages
	const char *dd     = "";
	const char *rr     = "";
	const char *render = "";
	const char *us     = "";
	if ( st->m_u && st->m_u[0] ) us = st->m_u;
	//if ( st->m_sfn != -1 ) sprintf ( rtu , "%" PRId32,st->m_sfn );
	if ( st->m_old ) dd = " checked";
	if ( st->m_recycle            ) rr     = " checked";
	if ( st->m_render             ) render = " checked";

	xbuf->safePrintf(
			 "<style>"
			 ".poo { background-color:#%s;}\n"
			 "</style>\n" ,
			 LIGHT_BLUE );


	int32_t clen;
	const char *contentParm = r->getString("content",&clen,"");
	
	// print the input form
	xbuf->safePrintf (
		       "<style>\n"
		       "h2{font-size: 12px; color: #666666;}\n"

		       ".spam { border: 1px solid gray;"
		       "background: #af0000;"
		       "color: #ffffa0;}"
		       ".hs {color: #009900;}"
		       "</style>\n"
		       "<center>"

		  "<table %s>"

			  "<tr><td colspan=5><center><b>"
			  "Parser"
			  "</b></center></td></tr>\n"

		  "<tr class=poo>"
		  "<td>"
		  "<b>url</b>"
			  "<br><font size=-2>"
			  "Type in <b>FULL</b> url to parse."
			  "</font>"
		  "</td>"

		  "</td>"
		  "<td>"
		  "<input type=text name=u value=\"%s\" size=\"40\">\n"
		  "</td>"
		  "</tr>"

		  "<tr class=poo>"
		  "<td>"
			  "<b>use cached</b>"

			  "<br><font size=-2>"
			  "Load page from cache (titledb)?"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=old value=1%s> "
		  "</td>"
		  "</tr>"

		  "<tr class=poo>"
		  "<td>"
		  "<b>recycle link info</b>"

			  "<br><font size=-2>"
			  "Recycle the link info from the title rec"
			  "Load page from cache (titledb)?"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=recycle value=1%s> "
		  "</td>"
		  "</tr>"

		  "<tr class=poo>"
		  "<td>"
		  "<b>render html</b>"

			  "<br><font size=-2>"
			  "Render document content as HTML"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=render value=1%s> "
		  "</td>"
		  "</tr>"

		  "<tr class=poo>"
		  "<td>"
		  "<b>optional query</b>"

			  "<br><font size=-2>"
			  "Leave empty usually. For title generation only."
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=text name=\"q\" size=\"20\" value=\"\"> "
		  "</td>"
		       "</tr>",

			  TABLE_STYLE,
			  us ,
			   dd,
			   rr, 
			   render
			  );

	xbuf->safePrintf(
		  "<tr class=poo>"
			  "<td>"
			  "<b>content type below is</b>"
			  "<br><font size=-2>"
			  "Is the content below HTML? XML? JSON?"
			  "</font>"
			  "</td>"

		  "<td>"
		       "<select name=ctype>\n"
		       "<option value=%" PRId32" selected>HTML</option>\n"
		       "<option value=%" PRId32">XML</option>\n"
		       "<option value=%" PRId32">JSON</option>\n"
		       "</select>\n"

		  "</td>"
		       "</tr>",
		       (int32_t)CT_HTML,
		       (int32_t)CT_XML,
		       (int32_t)CT_JSON
			  );

	xbuf->safePrintf(

			  "<tr class=poo>"
			  "<td><b>content</b>"
			  "<br><font size=-2>"
			  "Use this content for the provided <i>url</i> "
			  "rather than downloading it from the web."
			  "</td>"

			  "<td>"
			  "<textarea rows=10 cols=80 name=content>"
			  "%s"
			  "</textarea>"
			  "</td>"
			  "</tr>"

		  "</table>"
		  "</center>"
		  "</form>"
		  "<br>",
			  contentParm );

	xbuf->safePrintf(
			 "<center>"
			 "<input type=submit value=Submit>"
			 "</center>"
			 );


	// just print the page if no url given
	if ( ! st->m_u || ! st->m_u[0] ) return processLoop ( st );


	XmlDoc *xd = &st->m_xd;
	// set this up
	SpiderRequest sreq;
	strcpy(sreq.m_url,st->m_u);
	int32_t firstIp = hash32n(st->m_u);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	// parentdocid of 0
	sreq.setKey( firstIp, 0LL, false );
	sreq.m_isPageParser = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp = firstIp;
	Url nu;
	nu.set(sreq.m_url);
	sreq.m_domHash32 = nu.getDomainHash32();
	sreq.m_siteHash32 = nu.getHostHash32();

	// . get provided content if any
	// . will be NULL if none provided
	// . "content" may contain a MIME
	int32_t  contentLen = 0;
	const char *content = r->getString ( "content" , &contentLen , NULL );
	// is the "content" url-encoded? default is true.
	// mark doesn't like to url-encode his content
	if ( ! content ) {
		content    = r->getUnencodedContent    ();
		contentLen = r->getUnencodedContentLen ();
	}
	// ensure null
	if ( contentLen == 0 ) content = NULL;

	uint8_t contentType = CT_HTML;
	if ( r->getBool("xml",0) ) contentType = CT_XML;
	contentType = r->getLong("ctype",contentType);//CT_HTML);

	// hack
	if ( content ) {
		st->m_dbuf.purge();
		st->m_dbuf.safeStrcpy(content);
		content = st->m_dbuf.getBufStart();
	}

	// . use the enormous power of our new XmlDoc class
	// . this returns false if blocked
	if ( ! xd->set4 ( &sreq       ,
			  NULL        ,
			  (char*)st->m_coll  ,
			  &st->m_wbuf        ,
			  0, //niceness
			  (char*)content ,
			  false, // deletefromindex
			  0, // forced ip
			  contentType ))
		// return error reply if g_errno is set
		return sendErrorReply ( st , g_errno );
	// make this our callback in case something blocks
	xd->setCallback ( st , processLoop );
	// . set xd from the old title rec if recycle is true
	// . can also use XmlDoc::m_loadFromOldTitleRec flag
	if ( st->m_recycle ) xd->m_recycleContent = true;

	return processLoop ( st );
}

bool processLoop ( void *state ) {
	// cast it
	State8 *st = (State8 *)state;
	// get the xmldoc
	XmlDoc *xd = &st->m_xd;

	// error?
	if ( g_errno ) return sendErrorReply ( st , g_errno );

	if ( st->m_u && st->m_u[0] ) {
		// now get the meta list, in the process it will print out a 
		// bunch of junk into st->m_xbuf
		char *metalist = xd->getMetaList ( );
		if ( ! metalist ) return sendErrorReply ( st , g_errno );
		// return false if it blocked
		if ( metalist == (void *)-1 ) return false;
		// for debug...
		if ( ! xd->m_indexCode ) xd->doConsistencyTest ( false );
		// print it out
		xd->printDoc( &st->m_xbuf );
	}
	
	// now encapsulate it in html head/tail and send it off
	bool status = g_httpServer.sendDynamicPage( st->m_s , 
						    st->m_xbuf.getBufStart(), 
						    st->m_xbuf.length() ,
						    -1, //cachtime
						    false ,//postreply?
						    NULL, //ctype
						    -1 , //httpstatus
						    NULL,//cookie
						    "utf-8");
	// delete the state now
	mdelete ( st , sizeof(State8) , "PageParser" );
	delete (st);

	// return the status
	return status;
}




// returns true
bool sendErrorReply ( void *state , int32_t err ) {
	// ensure this is set
	if ( ! err ) { g_process.shutdownAbort(true); }
	// get it
	State8 *st = (State8 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_s;

	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "<b>had server-side error: %s</b><br>",
		  mstrerror(g_errno));
	// nuke state8
	mdelete ( st , sizeof(State8) , "PageGet1" );
	delete (st);
	// erase g_errno for sending
	//g_errno = 0;
	// . now encapsulate it in html head/tail and send it off
	//return g_httpServer.sendDynamicPage ( s , tmp , strlen(tmp) );
	return g_httpServer.sendErrorReply ( s, err, mstrerror(err) );
}
