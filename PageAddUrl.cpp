#include "gb-include.h"

#include "Pages.h"
#include "Collectiondb.h"
#include "Msg4.h"
#include "Spider.h"
#include "Parms.h"
#include "GigablastRequest.h"
#include "Conf.h"
#include "Mem.h"

static bool sendReply( void *state );

static void addedUrlsToSpiderdbWrapper ( void *state ) {
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	sendReply ( state );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . add url page for admin, users use sendPageAddUrl() in PageRoot.cpp
bool sendPageAddUrl2 ( TcpSocket *sock , HttpRequest *hr ) {

	// or if in read-only mode
	if ( g_conf.m_readOnlyMode ) {
		g_errno = EREADONLYMODE;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,500,msg);
	}

	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  urlLen = 0;
	const char *urls = hr->getString ( "urls" , &urlLen , NULL /*default*/);

	char format = hr->getReplyFormat();

	const char *c = hr->getString("c");
	
	if ( ! c && (format == FORMAT_XML || format == FORMAT_JSON) ) {
		g_errno = EMISSINGINPUT;
		const char *msg = "missing c parm. See /admin/api to see parms.";
		return g_httpServer.sendErrorReply(sock,500,msg);
	}

	if ( ! urls && (format == FORMAT_XML || format == FORMAT_JSON) ) {
		g_errno = EMISSINGINPUT;
		const char *msg = "missing urls parm. See /admin/api to see parms.";
		return g_httpServer.sendErrorReply(sock,500,msg);
	}


	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( hr );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,500,msg);
	}


	// make a new state
	GigablastRequest *gr;
	try { gr = new (GigablastRequest); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log( LOG_WARN, "PageAddUrl: new(%i): %s", (int)sizeof(GigablastRequest),mstrerror(g_errno) );
		return g_httpServer.sendErrorReply(sock, 500, mstrerror(g_errno));
	}
	mnew ( gr , sizeof(GigablastRequest) , "PageAddUrl" );


	// this will fill in GigablastRequest so all the parms we need are set
	// set this. also sets gr->m_hr
	g_parms.setGigablastRequest ( sock , hr , gr );

	// if no url given, just print a blank page
	if ( ! urls ) return sendReply (  gr );

	// do not spider links for spots
	bool status = getSpiderRequestMetaList ( (char*)urls, &gr->m_listBuf , gr->m_harvestLinks, NULL );
	int32_t size = gr->m_listBuf.length();
	
	// error / not list
	if ( ! status || !size ) {
		// nuke it
		if ( !size ) {
			g_errno = EMISSINGINPUT;
		}

		bool rc = g_httpServer.sendErrorReply(gr);
		mdelete ( gr , sizeof(*gr) , "PageAddUrl" );
		delete gr;
		return rc;
	}

	// add to spiderdb
	if (!gr->m_msg4.addMetaList(&(gr->m_listBuf), cr->m_collnum, gr, addedUrlsToSpiderdbWrapper)) {
		// blocked!
		return false;
	}

	// did not block, print page!
	sendReply ( gr );
	return true;
}

bool sendReply ( void *state ) {
	GigablastRequest *gr = (GigablastRequest *)state;

	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	SafeBuf xb;
	if ( gr->m_urlsBuf ) {
		xb.safeTruncateEllipsis ( gr->m_urlsBuf , 200 );
		log( LOG_INFO, "http: add url %s (%s)", xb.getBufStart(), mstrerror( g_errno ) );
	}

	char format = gr->m_hr.getReplyFormat();
	TcpSocket *sock = gr->m_socket;

	if ( format == FORMAT_JSON || format == FORMAT_XML ) {
		bool status = g_httpServer.sendSuccessReply ( gr );
		// nuke state
		mdelete ( gr , sizeof(*gr) , "PageAddUrl" );
		delete (gr);
		return status;
	}

	int32_t ulen = 0;
	const char *url = gr->m_urlsBuf;
	if ( url ) ulen = strlen (url);

	// re-null it out if just http://
	bool printUrl = true;
	if ( ulen == 0 ) printUrl = false;
	if ( ! gr->m_urlsBuf       ) printUrl = false;
	if ( ulen==7 && printUrl && !strncasecmp(gr->m_url,"http://",7))
		printUrl = false;
	if ( ulen==8 && printUrl && !strncasecmp(gr->m_url,"https://",8))
		printUrl = false;

	// page is not more than 32k
	StackBuf<1024*32+MAX_URL_LEN*2> sb;

	g_pages.printAdminTop ( &sb , sock , &gr->m_hr );

	// if there was an error let them know
	SafeBuf mbuf;

	if ( g_errno ) {
		mbuf.safePrintf("<center><font color=red>");
		mbuf.safePrintf("Error adding url(s): <b>%s[%i]</b>", mstrerror(g_errno) , g_errno);
		mbuf.safePrintf("</font></center>");
	} else if ( printUrl ) {
		mbuf.safePrintf("<center><font color=red>");
		mbuf.safePrintf("<b><u>");
		mbuf.safeTruncateEllipsis(gr->m_urlsBuf,200);
		mbuf.safePrintf("</u></b></font> added to spider queue successfully<br><br>");
		mbuf.safePrintf("</font></center>");
	}

	if ( mbuf.length() ) {
		sb.safeStrcpy( mbuf.getBufStart() );
	}

	g_parms.printParmTable ( &sb , sock , &gr->m_hr );

	// print the final tail
	g_pages.printTail ( &sb, true ); // admin?

	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;

	// nuke state
	mdelete ( gr , sizeof(GigablastRequest) , "PageAddUrl" );
	delete (gr);

	return g_httpServer.sendDynamicPage( sock, sb.getBufStart(), sb.length(), -1 ); // cachetime
}
