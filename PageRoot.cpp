#include "gb-include.h"

#include "PageRoot.h"
#include "Titledb.h"
#include "Spider.h"
#include "Tagdb.h"
#include "Dns.h"
#include "Collectiondb.h"
#include "Clusterdb.h"    // for getting # of docs indexed
#include "Pages.h"
#include "Query.h"        // MAX_QUERY_LEN
#include "SafeBuf.h"
#include "Proxy.h"
#include "HashTable.h"
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

bool sendPageRoot ( TcpSocket *s, HttpRequest *r ){
	return sendPageRoot ( s, r, NULL );
}

static bool printNav ( SafeBuf &sb , HttpRequest *r ) {
	sb.safePrintf("</TD></TR></TABLE>"
		      "</body></html>");
	return true;
}

//////////////
//
// BEGIN expandHtml() helper functions
//
//////////////

static bool printFamilyFilter ( SafeBuf& sb , bool familyFilterOn ) {
	const char *s1 = "";
	const char *s2 = "";
	if ( familyFilterOn ) s1 = " checked";
	else                  s2 = " checked";
	//p += sprintf ( p ,
	return sb.safePrintf (
		       "Family filter: "
		       "<input type=radio name=ff value=1%s>On &nbsp; "
		       "<input type=radio name=ff value=0%s>Off &nbsp; " ,
		       s1 , s2 );
	//return p;
}

#include "SearchInput.h"

static bool printRadioButtons ( SafeBuf& sb , SearchInput *si ) {
	// don't display this for directory search
	// look it up. returns catId <= 0 if dmoz not setup yet.
	// From PageDirectory.cpp
	//int32_t catId= g_categories->getIdFromPath(decodedPath, decodedPathLen);
	// if /Top print the directory homepage
	//if ( catId == 1 || catId <= 0 ) 
	//	return true;

	// site
	/*
	if ( si->m_siteLen > 0 ) {
		// . print rest of search box etc.
		// . print cobranding radio buttons
		//if ( p + si->m_siteLen + 1 >= pend ) return p;
		//p += sprintf ( p , 
		return sb.safePrintf (
			  //" &nbsp; "
			  //"<font size=-1>"
			  //"<b><a href=\"/\"><font color=red>"
			  //"Powered by Gigablast</font></a></b>"
			  //"<br>"
			  //"<tr align=center><td></td><td>"
			  "<input type=radio name=site value=\"\">"
			  "Search the Web "
			  "<input type=radio name=site "
			  "value=\"%s\"  checked>Search %s" ,
			  //"</td></tr></table><br>"
			  //"</td></tr>"
			  //"<font size=-1>" ,
			  si->m_site , si->m_site );
	}
	else if ( si->m_sitesLen > 0 ) {
	*/
	if ( si->m_sites && si->m_sites[0] ) {
		// . print rest of search box etc.
		// . print cobranding radio buttons
		//if ( p + si->m_sitesLen + 1 >= pend ) return p;
		// if not explicitly instructed to print all sites
		// and they are a int32_t list, do not print all
		/*
		char tmp[1000];
		char *x = si->m_sites;
		if ( si->m_sitesLen > 255){//&&!st->m_printAllSites){
			// copy what's there
			strncpy ( tmp , si->m_sites , 255 );
			x = tmp + 254 ;
			// do not hack off in the middle of a site
			while ( is_alnum(*x) && x > tmp ) x--;
			// overwrite it with [more] link
			//x += sprintf ( x , "<a href=\"/search?" );
			// our current query parameters
			//if ( x + uclen + 10 >= xend ) goto skipit;
			sprintf ( x , " ..." );
			x = tmp;
		}
		*/
		//p += sprintf ( p , 
		sb.safePrintf (
			  //" &nbsp; "
			  //"<font size=-1>"
			  //"<b><a href=\"/\"><font color=red>"
			  //"Powered by Gigablast</font></a></b>"
			  //"<br>"
			  //"<tr align=center><td></td><td>"
			  "<input type=radio name=sites value=\"\">"
			  "Search the Web "
			  "<input type=radio name=sites "
			  "value=\"%s\"  checked>Search ",
			  //"</td></tr></table><br>"
			  //"</td></tr>"
			  //"<font size=-1>" ,
			  si->m_sites );
		sb.safeTruncateEllipsis ( si->m_sites, 255 );
	}
	return true;
}

static bool printLogo ( SafeBuf& sb , SearchInput *si ) {
	// if an image was provided...
	if ( ! si->m_imgUrl || ! si->m_imgUrl[0] ) {
		// no, now we default to our logo
		//return true;
		//p += sprintf ( p ,
		return sb.safePrintf (
			  "<a href=\"/\">"
			  "<img valign=top width=250 height=61 border=0 "
			  // avoid https for this, so make it absolute
			  "src=\"/logo-med.jpg\"></a>" );
		//return p;
	}
	// do we have a link?
	if ( si->m_imgLink && si->m_imgLink[0])
		//p += sprintf ( p , "<a href=\"%s\">",si->m_imgLink);
		sb.safePrintf ( "<a href=\"%s\">", si->m_imgLink );
	// print image width and length
	if ( si->m_imgWidth >= 0 && si->m_imgHeight >= 0 ) 
		//p += sprintf ( p , "<img width=%" PRId32" height=%" PRId32" ",
		sb.safePrintf( "<img width=%" PRId32" height=%" PRId32" ",
			       si->m_imgWidth , si->m_imgHeight );
	else
		//p += sprintf ( p , "<img " );
		sb.safePrintf ( "<img " );

	//p += sprintf ( p , "border=0 src=\"%s\">",
	sb.safePrintf( "border=0 src=\"%s\">",
		       si->m_imgUrl );
	// end the link if we had one
	if ( si->m_imgLink && si->m_imgLink[0] ) 
		//p += sprintf ( p , "</a>");
		sb.safePrintf ( "</a>");

	return true;
}

/////////////
//
// END expandHtml() helper functions
//
/////////////


bool expandHtml (  SafeBuf& sb,
		   const char *head , 
		   int32_t hlen ,
		   const char *q    , 
		   int32_t qlen ,
		   HttpRequest *r ,
		   SearchInput *si,
		   const char *method ,
		   CollectionRec *cr ) {
	//char *pend = p + plen;
	// store custom header into buf now
	//for ( int32_t i = 0 ; i < hlen && p+10 < pend ; i++ ) {
	for ( int32_t i = 0 ; i < hlen; i++ ) {
		if ( head[i] != '%'   ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( i + 1 >= hlen    ) {
			// *p++ = head[i];
			sb.safeMemcpy((char*)&head[i], 1);
			continue;
		}
		if ( head[i+1] == 'S' ) { 
			// now we got the %S, insert "spiders are [on/off]"
			bool spidersOn = true;
			if ( ! g_conf.m_spideringEnabled ) spidersOn = false;
			if ( ! cr->m_spideringEnabled ) spidersOn = false;
			if ( spidersOn ) 
				sb.safePrintf("Spiders are on");
			else
				sb.safePrintf("Spiders are off");
			// skip over %S
			i += 1;
			continue;
		}

		if ( head[i+1] == 'q' ) { 
			// now we got the %q, insert the query
			char *p    = (char*) sb.getBuf();
			char *pend = (char*) sb.getBufEnd();
			int32_t eqlen = dequote ( p , pend , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %q
			i += 1;
			continue;
		}

		if ( head[i+1] == 'c' ) { 
			// now we got the %q, insert the query
			if ( cr ) sb.safeStrcpy(cr->m_coll);
			// skip over %c
			i += 1;
			continue;
		}

		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'r' &&
		     head[i+5] == 'e' ) {
			// insert the location
			int32_t whereLen;
			const char *where = r->getString("where",&whereLen);
			// get it from cookie as well!
			if ( ! where ) 
				where = r->getStringFromCookie("where",
							       &whereLen);
			// fix for getStringFromCookie
			if ( where && ! where[0] ) where = NULL;
			// skip over the %where
			i += 5;

			if (where) {
				sb.dequote (where,whereLen);
			}
			continue;
		}
		if ( head[i+1] == 'w' &&
		     head[i+2] == 'h' &&
		     head[i+3] == 'e' &&
		     head[i+4] == 'n' ) {
			// insert the location
			int32_t whenLen;
			const char *when = r->getString("when",&whenLen);
			// skip over the %when
			i += 4;
			if ( ! when ) continue;
			sb.dequote (when,whenLen);
			continue;
		}
		// %sortby
		if ( head[i+1] == 's' &&
		     head[i+2] == 'o' &&
		     head[i+3] == 'r' &&
		     head[i+4] == 't' &&
		     head[i+5] == 'b' &&
		     head[i+6] == 'y' ) {
			// insert the location
			int32_t sortBy = r->getLong("sortby",1);
			// print the radio buttons
			const char *cs[5];
			cs[0]="";
			cs[1]="";
			cs[2]="";
			cs[3]="";
			cs[4]="";
			if ( sortBy >=1 && sortBy <=4 )
				cs[sortBy] = " checked";
			sb.safePrintf(
			 "<input type=radio name=sortby value=1%s>date "
			 "<input type=radio name=sortby value=2%s>distance "
			 "<input type=radio name=sortby value=3%s>relevancy "
			 "<input type=radio name=sortby value=4%s>popularity",
			 cs[1],cs[2],cs[3],cs[4]);
			// skip over the %sortby
			i += 6;
			continue;
		}
		if ( head[i+1] == 'e' ) { 
			// now we got the %e, insert the query
			char *p    = (char*) sb.getBuf();
			int32_t  plen = sb.getAvail();
			int32_t eqlen = urlEncode ( p , plen , q , qlen );
			//p += eqlen;
			sb.incrementLength(eqlen);
			// skip over %e
			i += 1;
			continue;
		}
		if ( head[i+1] == 'N' ) { 
			//now each host tells us how many docs it has in itsping
			int64_t c = g_hostdb.getNumGlobalRecs();
			c += g_conf.m_docCountAdjustment;
			// never allow to go negative
			if ( c < 0 ) c = 0;
			//p+=ulltoa(p,c);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			int32_t len = ulltoa(p, c);
			sb.incrementLength(len);
			// skip over %N
			i += 1;
			continue;
		}
		if ( head[i+1] == 'n' ) { 
			// now we got the %n, insert the collection doc count
			//p+=ulltoa(p,docsInColl);
			char *p = (char*) sb.getBuf();
			sb.reserve2x(16);
			int64_t docsInColl = 0;
			if ( cr ) docsInColl = cr->getNumDocsIndexed();
			int32_t len = ulltoa(p, docsInColl);
			sb.incrementLength(len);
			// skip over %n
			i += 1;
			continue;
		}
		// print the drop down menu for selecting the # of reslts
		if ( head[i+1] == 'D' ) {
			// skip over %D
			i += 1;
			continue;
		}
		if ( head[i+1] == 'H' ) { 
			// . insert the secret key here, to stop seo bots
			// . TODO: randomize its position to make parsing more 
			//         difficult
			// . this secret key is for submitting a new query
			// int32_t key;
			// char kname[4];
			// g_httpServer.getKey (&key,kname,NULL,0,time(NULL),0,
			// 		     10);
			//sprintf (p , "<input type=hidden name=%s value=%" PRId32">",
			//	  kname,key);
			//p += strlen ( p );
			// sb.safePrintf( "<input type=hidden name=%s "
			//"value=%" PRId32">",
			// 	       kname,key);

			//adds param for default screen size
			//if(cr)
			//	sb.safePrintf("<input type=hidden "
			//"id='screenWidth' name='ws' value=%" PRId32">",
			//cr->m_screenWidth);

			// insert collection name too
			int32_t collLen;
			const char *coll = r->getString ( "c" , &collLen );
			if ( collLen > 0 && collLen < MAX_COLL_LEN ) {
			        //sprintf (p,"<input type=hidden name=c "
				//	 "value=\"");
				//p += strlen ( p );	
				sb.safePrintf("<input type=hidden name=c "
					      "value=\"");
				//gbmemcpy ( p , coll , collLen );
				//p += collLen;
				sb.safeMemcpy(coll, collLen);
				//sprintf ( p , "\">\n");
				//p += strlen ( p );	
				sb.safePrintf("\">\n");
			}

			// skip over %H
			i += 1;
			continue;
		}

		// MDW

		if ( head[i+1] == 'F' ) {
			i += 1;
			if ( ! method ) method = "GET";
			sb.safePrintf("<form method=%s action=\"/search\" "
				      "name=\"f\">\n",method);
			continue;
		}

		if ( head[i+1] == 'L' ) {
			i += 1;
			printLogo ( sb , si );
			continue;
		}

		if ( head[i+1] == 'f' ) {
			i += 1;
			printFamilyFilter ( sb , si->m_familyFilter );
			continue;
		}

		if ( head[i+1] == 'R' ) {
			i += 1;
			printRadioButtons ( sb , si );
			continue;
		}

		sb.safeMemcpy((char*)&head[i], 1);
		continue;
	}
	//return p;
	return true;
}


bool printLeftColumnRocketAndTabs ( SafeBuf *sb , 
				    bool isSearchResultsPage ,
				    CollectionRec *cr ,
				    const char *tabName ) {

	struct MenuItem {
		const char *m_text;
		const char *m_url;
	};

	static const MenuItem mi[] = {
		{"SEARCH","/"},
		{"ADVANCED","/adv.html"},
		{"ADD URL","/addurl"},
		{"WIDGETS","/widgets.html"},
		{"SYNTAX","/syntax.html"},
		{"FAQ","/faq.html"},
		{"API","/api.html"}
	};
	static const int32_t n = sizeof(mi) / sizeof(MenuItem);

	const char *coll = "";
	if ( cr ) coll = cr->m_coll;

	//
	// first the nav column
	//
	sb->safePrintf(
		       "<TD bgcolor=#%s " // f3c714 " // yellow/gold
		      "valign=top "
		      "style=\"width:210px;"
		      "border-right:3px solid blue;"
		      "\">"

		      "<br>"

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
		       , GOLD
		       , coll
		       );

	sb->safePrintf("<br style=line-height:10px;>"
		       "<img border=0 "
		       "width=54 height=79 src=/rocket.jpg>"
		       );

	sb->safePrintf ( "</div>"
			 "</a>"
			 "</center>"

			 "<br>"
			 "<br>"
		      );



	for ( int32_t i = 0 ; i < n ; i++ ) {

		// just show search, directort and advanced tab in serps
		if ( isSearchResultsPage && i >= 3 ) break;

		char delim = '?';
		if ( strstr ( mi[i].m_url,"?") ) delim = '&';

		sb->safePrintf(
			      "<a href=%s%cc=%s>"
			      "<div style=\""
			      "padding:5px;"
			      "position:relative;"
			      "text-align:right;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "font-size:14px;"
			      "x-overflow:;"
			      , mi[i].m_url
			      , delim
			      , coll
			      );
		//if ( i == pageNum )
		bool matched = false;
		if ( strcasecmp(mi[i].m_text,tabName) == 0 )
			matched = true;

		if ( matched )
			sb->safePrintf(
				      "border-color:blue;"
				      "color:black;"
				      "background-color:white;\" ");
		else
			sb->safePrintf("border-color:white;"
				      "color:white;"
				      "background-color:blue;\" "
				      " onmouseover=\""
				      "this.style.backgroundColor='lightblue';"
				      "this.style.color='black';\""
				      " onmouseout=\""
				      "this.style.backgroundColor='blue';"
				      "this.style.color='white';\""
				      );

		sb->safePrintf(">"
			      // make button wider
			      "<nobr>"
			      "&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; "
			      "<b>%s</b> &nbsp; &nbsp;</nobr>"
			      , mi[i].m_text
			      );
		//
		// begin hack: white out the blue border line!!
		//
		if ( matched )
			sb->safePrintf(
				      "<div style=padding:5px;top:0;"
				      "background-color:white;"
				      "display:inline-block;"
				      "position:absolute;>"
				      "&nbsp;"
				      "</div>"
				      );
		// end hack
		sb->safePrintf(
			      "</div>"
			      "</a>"
			      "<br>"
			      );
	}



	// admin link
	if ( isSearchResultsPage ) return true;

	sb->safePrintf(
		      "<a href=/admin/settings?c=%s>"
		      "<div style=\"background-color:green;"
		      // for try it out bubble:
		      //"position:relative;"
		      "padding:5px;"
		      "text-align:right;"
		      "border-width:3px;"
		      "border-right-width:0px;"
		      "border-style:solid;"
		      "margin-left:10px;"
		      "border-color:white;"
		      "border-top-left-radius:10px;"
		      "border-bottom-left-radius:10px;"
		      "font-size:14px;"
		      "color:white;"
		      "cursor:hand;"
		      "cursor:pointer;\" "
		      " onmouseover=\""
		      "this.style.backgroundColor='lightgreen';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='green';"
		      "this.style.color='white';\""
		      ">"

		      /*
		      // try it out bubble div
		      "<div "

		      " onmouseover=\""
		      "this.style.box-shadow='10px 10px 5px #888888';"
		      "\""
		      " onmouseout=\""
		      "this.style.box-shadow='';"
		      "\""

		      "style=\""
		      "vertical-align:middle;"
		      "text-align:left;"
		      "cursor:pointer;"
		      "cursor:hand;"
		      //"border-color:black;"
		      //"border-style:solid;"
		      //"border-width:2px;"
		      "padding:3px;"
		      //"width:30px;"
		      //"height:20px;"
		      //"margin-top:-20px;"
		      "margin-left:-120px;"
		      "position:absolute;"
		      //"top:-20px;"
		      //"left:10px;"
		      "display:inline-block;"
		      "\""
		      ">"
		      "<b style=font-size:11px;>"
		      "Click for demo"
		      "</b>"
		      "</div>"
		      */
		      // end try it out bubble div




		      "<b>ADMIN</b> &nbsp; &nbsp;"
		      "</div>"
		      "</a>"
		      "<br>"

		      "</TD>"
		      , coll
		      );

	return true;
}

bool printFrontPageShell ( SafeBuf *sb , const char *tabName , CollectionRec *cr ,
			   bool printGigablast ) {

	sb->safePrintf("<html>\n");
	sb->safePrintf("<head>\n");

	sb->safePrintf("<meta name=\"description\" content=\"A powerful, new search engine that does real-time indexing!\">\n");
	sb->safePrintf("<meta name=\"keywords\" content=\"search, search engine, search engines, search the web, fresh index, green search engine, green search, clean search engine, clean search\">\n");

	const char *title = "An Alternative Open Source Search Engine";
	if ( strcasecmp(tabName,"search") ) {
		title = tabName;
	}

	sb->safePrintf("<title>Gigablast - %s</title>\n",title);
	sb->safePrintf("<style><!--\n");
	sb->safePrintf("body {\n");
	sb->safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb->safePrintf("color: #000000;\n");
	sb->safePrintf("font-size: 12px;\n");
	sb->safePrintf("margin: 0px 0px;\n");
	sb->safePrintf("letter-spacing: 0.04em;\n");
	sb->safePrintf("}\n");
	sb->safePrintf("a {text-decoration:none;}\n");
	sb->safePrintf(".bold {font-weight: bold;}\n");
	sb->safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb->safePrintf(".url {color:#008000;}\n");
	sb->safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb->safePrintf("}\n");
	sb->safePrintf("table {\n");
	sb->safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb->safePrintf("color: #000000;\n");
	sb->safePrintf("font-size: 12px;\n");
	sb->safePrintf("}\n");
	sb->safePrintf(".directory {font-size: 16px;}\n"
		      ".nav {font-size:20px;align:right;}\n"
		      );
	sb->safePrintf("-->\n");
	sb->safePrintf("</style>\n");
	sb->safePrintf("\n");
	sb->safePrintf("</head>\n");
	sb->safePrintf("<script>\n");
	sb->safePrintf("<!--\n");
	sb->safePrintf("function x(){document.f.q.focus();}\n");
	sb->safePrintf("// --></script>\n");
	sb->safePrintf("<body onload=\"x()\">\n");

	//
	// DIVIDE INTO TWO PANES, LEFT COLUMN and MAIN COLUMN
	//


	sb->safePrintf("<TABLE border=0 height=100%% cellspacing=0 "
		      "cellpadding=0>"
		      "\n<TR>\n");

	// . also prints <TD>...</TD>
	// . false = isSearchResultsPage?
	printLeftColumnRocketAndTabs ( sb , false , cr , tabName );

	//
	// now the MAIN column
	//
	sb->safePrintf("\n<TD valign=top style=padding-left:30px;>\n");

	sb->safePrintf("<br><br>");

	if ( printGigablast ) {
		sb->safePrintf("<a href=/><img border=0 width=470 "
			      "height=44 src=/gigablast.jpg></a>\n");
	}

	return true;
}

static bool printWebHomePage ( SafeBuf &sb , HttpRequest *r , TcpSocket *sock ) {
	SearchInput si;
	si.set ( sock , r );

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	const char *method = "GET";
	if ( si.m_sites && strlen(si.m_sites)>800 ) {
		method = "POST";
	}

	// if the provided their own
	CollectionRec *cr = g_collectiondb.getRec ( r );
	if ( cr && cr->m_htmlRoot.length() ) {
		return expandHtml (  sb ,
				     cr->m_htmlRoot.getBufStart(),
				     cr->m_htmlRoot.length(),
				     NULL,
				     0,
				     r ,
				     &si,
				     //TcpSocket   *s ,
				     method , // "GET" or "POST"
				     cr );//CollectionRec *cr ) {
	}

	// . search special types
	// . defaults to web which is "search"
	// . can be like "images" "products" "articles"
	const char *searchType = r->getString("searchtype",NULL,"search",NULL);
	log("searchtype=%s",searchType);

	// pass searchType in as tabName
	printFrontPageShell ( &sb , searchType , cr , true );


	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	sb.safePrintf("<form method=%s action=/search name=f>\n", method);

	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	// put search box in a box
	sb.safePrintf("<div style="
		      "background-color:#%s;"//fcc714;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "border-color:blue;"
		      //"background-color:blue;"
		      "padding:20px;"
		      "border-radius:20px;"
		      ">"
		      ,GOLD
		      );


	sb.safePrintf("<input name=q type=text "
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

		      "size=40 value=\"\">&nbsp; &nbsp;"

		      //"<input type=\"submit\" value=\"Search\">"

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
		      "\n"
		      );

	sb.safePrintf("</div>\n");

	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	if ( cr ) { // && strcmp(cr->m_coll,"main") ) {
		sb.safePrintf("<center>"
			      "Searching the <b>%s</b> collection."
			      "</center>",
			      cr->m_coll);
		sb.safePrintf("<br>\n");
		sb.safePrintf("\n");
	}

	// print any red boxes we might need to
	if ( printRedBox2 ( &sb , sock , r ) )
		sb.safePrintf("<br>\n");

	sb.safePrintf("\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	printNav ( sb , r );
	return true;
}

static bool printAddUrlHomePage ( SafeBuf &sb , const char *url , HttpRequest *r ) {

	CollectionRec *cr = g_collectiondb.getRec ( r );

	printFrontPageShell ( &sb , "add url" , cr , true );

	sb.safePrintf("<br><br><br><br>\n");
	sb.safePrintf("<b>WARNING</b>: Adding URLs this way DOES NOT handle redirects.");
	sb.safePrintf("<br><br>");
	sb.safePrintf("If you add somesite.com and it redirects to www.somesite.com, it will be indexed as somesite.com, NOT www.somesite.com!<br>");
	sb.safePrintf("Use Admin -> Add Urls instead if you want redirects handled correctly.");

	sb.safePrintf("<script type=\"text/javascript\">\n"
		      "function handler() {\n" 
		      "if(this.readyState == 4 ) {\n"
		      "document.getElementById('msgbox').innerHTML="
		      "this.responseText;\n"
		      //"alert(this.status+this.statusText+"
		      //"this.responseXML+this.responseText);\n"
		      "}}\n"
		      "</script>\n");


	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	sb.safePrintf("<form method=GET "
		      "action=/addurl name=f>\n" );

	const char *coll = "";
	if ( cr ) coll = cr->m_coll;
	if ( cr )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">",
			      cr->m_coll);


	// put search box in a box
	sb.safePrintf("<div style="
		      "background-color:#%s;" // fcc714;"
		      "border-style:solid;"
		      "border-width:3px;"
		      "border-color:blue;"
		      //"background-color:blue;"
		      "padding:20px;"
		      "border-radius:20px;"
		      ">"
		      , GOLD
		      );


	sb.safePrintf("<input name=urls type=text "
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

		      "size=40 value=\""
		      );



	if ( url ) {
		SafeBuf tmp;
		tmp.safePrintf("%s",url);
		// don't let double quotes in the url close our val attribute
		tmp.replace("\"","%22");
		sb.safeMemcpy(&tmp);
	}
	else
		sb.safePrintf("http://");
	sb.safePrintf("\">&nbsp; &nbsp;"
		      //"<input type=\"submit\" value=\"Add Url\">\n"
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
		      "<b style=margin-left:-5px;font-size:18px;>GO</b>"
		      "</div>"
		      "\n"
		      );
	sb.safePrintf("\n");


	sb.safePrintf("</div>\n");

	sb.safePrintf("\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");


	// if addurl is turned off, just print "disabled" msg
	const char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	//if ( ! cr->m_addUrlEnabled    ) 
	//	msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";

	sb.safePrintf("<br><center>"
		      "Add a url to the <b>%s</b> collection</center>",coll);

	// if url is non-empty the ajax will receive this identical msg
	// and display it in the div, so do not duplicate the msg!
	if ( msg && ! url )
		sb.safePrintf("<br><br>%s",msg);


	// . the ajax msgbox div
	// . when loaded with the main page for the first time it will
	//   immediately replace its content...
	if ( url ) {
		const char *root = "";

		sb.safePrintf("<br>"
			      "<br>"
			      "<div id=msgbox>"
			      //"<b>Injecting your url. Please wait...</b>"
			      "<center>"
			      "<img src=%s/gears.gif width=50 height=50>"
			      "</center>"
			      "<script type=text/javascript>"
			      //"alert('shit');"
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      "var url='/addurl?urls="
			      , root );
		sb.urlEncode ( url );
		// propagate "admin" if set
		//int32_t admin = hr->getLong("admin",-1);
		//if ( admin != -1 ) sb.safePrintf("&admin=%" PRId32,admin);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		uint32_t h32 = hash32n(url);
		if ( h32 == 0 ) h32 = 1;
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		// msg7 needs an explicit collection for /addurl for injecting
		// in PageInject.cpp. it does not use defaults for safety.
		sb.safePrintf("&id=%" PRIu32"&c=%s&rand=%" PRIu64"';\n"
			      "client.open('GET', url );\n"
			      "client.send();\n"
			      "</script>\n"
			      , h32
			      , coll
			      , rand64
			      );
		sb.safePrintf("</div>\n");
	}

	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , r );
	return true;
}


// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageRoot ( TcpSocket *s , HttpRequest *r, char *cookie ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 10*1024 ];//+ MAX_QUERY_LEN ];
	// a ptr into "buf"
	//char *p    = buf;
	//char *pend = buf + 10*1024 + MAX_QUERY_LEN - 100 ;
	SafeBuf sb(buf, 10*1024 );//+ MAX_QUERY_LEN);
	// print bgcolors, set focus, set font style
	//p = g_httpServer.printFocus  ( p , pend );
	//p = g_httpServer.printColors ( p , pend );
	//int32_t  qlen;
	//char *q = r->getString ( "q" , &qlen , NULL );
	// insert collection name too
	CollectionRec *cr = g_collectiondb.getRec(r);
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	}

	printWebHomePage(sb,r,s);


	// . print last 5 queries
	// . put 'em in a table
	// . disable for now, impossible to monitor/control
	//p += printLastQueries ( p , pend );
	// are we the admin?
	//bool isAdmin = g_collectiondb.isAdmin ( r , s );

	// calculate bufLen
	//int32_t bufLen = p - buf;
	// . now encapsulate it in html head/tail and send it off
	// . the 0 means browser caches for however int32_t it's set for
	// . but we don't use 0 anymore, use -2 so it never gets cached so
	//   our display of the # of pages in the index is fresh
	// . no, but that will piss people off, its faster to keep it cached
	//return g_httpServer.sendDynamicPage ( s , buf , bufLen , -1 );
	return g_httpServer.sendDynamicPage ( s,
					      (char*) sb.getBufStart(),
					      sb.length(),
					      // 120 seconds cachetime
					      // don't cache anymore since
					      // we have the login bar at
					      // the top of the page
					      0,//120, // cachetime
					      false,// post?
					      "text/html",
					      200,
					      NULL, // cookie
					      "UTF-8",
					      r);
}

/////////////////
//
// ADD URL PAGE
//
/////////////////

#include "PageInject.h"
#include "Spider.h"

static bool canSubmit        (uint32_t h, int32_t now, int32_t maxUrlsPerIpDom);

void resetPageAddUrl ( ) ;


class State1i {
public:
	Msg7       m_msg7;
	TcpSocket *m_socket;
        bool       m_isMasterAdmin;
	char       m_coll[MAX_COLL_LEN+1];
	bool       m_goodAnswer;
	int32_t       m_ufuLen;
	char       m_ufu[MAX_URL_LEN];

	int32_t    m_urlLen;
	char       m_url[MAX_URL_LEN];

	//char       m_username[MAX_USER_SIZE];
	bool       m_strip;
	bool       m_spiderLinks;
	bool       m_forceRespider;
 	// buf filled by the links coming from google, msn, yahoo, etc
	//State2     m_state2[5]; // gb, goog, yahoo, msn, ask
	//int32_t       m_raw;
	//SpiderRequest m_sreq;
};

static void doneInjectingWrapper3 ( void *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageAddUrl ( TcpSocket *sock , HttpRequest *hr ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  urlLen = 0;
	const char *url = hr->getString ( "urls" , &urlLen , NULL /*default*/);

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly
	bool isAdmin = g_conf.isCollAdmin ( sock , hr );
	int32_t  ufuLen = 0;
	char *ufu = NULL;
	//if ( isAdmin )
	//	// get the url of a file of urls (ufu)
	//	ufu = hr->getString ( "ufu" , &ufuLen , NULL );

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN || ufuLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(sock,500,"url too long");
	}
	// get collection rec

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		g_msg = " (error: no collection)";
		return g_httpServer.sendErrorReply(sock,500,"no coll rec");
	}

	//
	// if no url, print the main homepage page
	//
	if ( ! url ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , NULL , hr );
		return g_httpServer.sendDynamicPage(sock, 
						    sb.getBufStart(), 
						    sb.length(),
						    // 120 secs cachetime
						    // don't cache any more
						    // since we have the
						    // login bar at top of page
						    0,//120 ,// cachetime
						    false,// post?
						    "text/html",
						    200,
						    NULL, // cookie
						    "UTF-8",
						    hr);
	}

	//
	// run the ajax script on load to submit the url now 
	//
	int32_t id = hr->getLong("id",0);
	// if we are not being called by the ajax loader, the put the
	// ajax loader script into the html now
	if ( id == 0 ) {
		SafeBuf sb;
		printAddUrlHomePage ( sb , url , hr );
		return g_httpServer.sendDynamicPage ( sock, 
						      sb.getBufStart(), 
						      sb.length(),
						      // don't cache any more
						      // since we have the
						      // login bar at top of 
						      //page
						      0,//3600,// cachetime
						      false,// post?
						      "text/html",
						      200,
						      NULL, // cookie
						      "UTF-8",
						      hr);
	}

	//
	// ok, inject the provided url!!
	//

	//
	// check for errors first
	//

	// if addurl is turned off, just print "disabled" msg
	const char *msg = NULL;
	if ( ! g_conf.m_addUrlEnabled ) 
		msg = "Add url is temporarily disabled";
	// can also be turned off in the collection rec
	//if ( ! cr->m_addUrlEnabled    ) 
	//	msg = "Add url is temporarily disabled";
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) 
		msg = "Add url is temporarily disabled";

	// . send msg back to the ajax request
	// . use cachetime of 3600 so it does not re-inject if you hit the
	//   back button!
	if  ( msg ) {
		SafeBuf sb;
		sb.safePrintf("%s",msg);
		g_httpServer.sendDynamicPage (sock, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}




	// make a new state
	State1i *st1 ;
	try { st1 = new (State1i); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddUrl: new(%i): %s", 
		    (int)sizeof(State1i),mstrerror(g_errno));
	    return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno)); }
	mnew ( st1 , sizeof(State1i) , "PageAddUrl" );
	// save socket and isAdmin
	st1->m_socket  = sock;
	st1->m_isMasterAdmin = isAdmin;

	// save the "ufu" (url of file of urls)
	st1->m_ufu[0] = '\0';
	st1->m_ufuLen  = ufuLen;
	gbmemcpy ( st1->m_ufu , ufu , ufuLen );
	st1->m_ufu[ufuLen] = '\0';

	st1->m_spiderLinks = true;
	st1->m_strip   = true;

	// save the collection name in the State1i class
	//if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	//strncpy ( st1->m_coll , coll , collLen );
	//st1->m_coll [ collLen ] = '\0';

	strcpy ( st1->m_coll , cr->m_coll );

	// assume they answered turing test correctly
	st1->m_goodAnswer = true;

	st1->m_strip = hr->getLong("strip",0);
	// . Remember, for cgi, if the box is not checked, then it is not 
	//   reported in the request, so set default return value to 0
	// . support both camel case and all lower-cases
	st1->m_spiderLinks = hr->getLong("spiderLinks",0);
	st1->m_spiderLinks = hr->getLong("spiderlinks",st1->m_spiderLinks);

	// . should we force it into spiderdb even if already in there
	// . use to manually update spider times for a url
	// . however, will not remove old scheduled spider times
	// . mdw: made force on the default
	st1->m_forceRespider = hr->getLong("force",1); // 0);


	uint32_t h = iptop ( sock->m_ip );
	int32_t now = getTimeGlobal();
	// . allow 1 submit every 1 hour
	// . restrict by submitter domain ip
	if ( ! st1->m_isMasterAdmin &&
	     ! canSubmit ( h , now , cr->m_maxAddUrlsPerIpDomPerDay ) ) {
		// return error page
		SafeBuf sb;
		sb.safePrintf("You breached your add url quota.");
		mdelete ( st1 , sizeof(State1i) , "PageAddUrl" );
		delete (st1);
		// use cachetime of 3600 so it does not re-inject if you hit 
		// the back button!
		g_httpServer.sendDynamicPage (sock, 
					      sb.getBufStart(), 
					      sb.length(),
					      3600,//-1, // cachetime
					      false,// post?
					      "text/html",
					      200, // http status
					      NULL, // cookie
					      "UTF-8");
		return true;
	}

	Msg7 *msg7 = &st1->m_msg7;
	// set this.
	InjectionRequest *ir = &msg7->m_injectionRequest;

	// default to zero
	memset ( ir , 0 , sizeof(InjectionRequest) );

	// this will fill in GigablastRequest so all the parms we need are set
	//setInjectionRequestFromParms ( sock , hr , cr , ir );

	int32_t collLen;
	const char *coll = hr->getString ( "c" , &collLen ,NULL );
	if ( ! coll ) coll = g_conf.m_defaultColl;
	ir->m_collnum = g_collectiondb.getCollnum ( coll );

	//save the URL
	st1->m_urlLen = strlen(url);
	memcpy(st1->m_url, url, st1->m_urlLen+1);
	
	
	ir->ptr_url = st1->m_url;

	// include \0 in size
	ir->size_url = strlen(ir->ptr_url)+1;

	// get back a short reply so we can show the status code easily
	ir->m_shortReply = 1;

	ir->m_spiderLinks = st1->m_spiderLinks;

	// this is really an injection, not add url, so make
	// GigablastRequest::m_url point to Gigablast::m_urlsBuf because
	// the PAGE_ADDURLS2 parms in Parms.cpp fill in the m_urlsBuf.
	// HACK!
	//gr->m_url = gr->m_urlsBuf;
	//ir->ptr_url = gr->m_urlsBuf;

	//
	// inject using msg7
	//

	// . pass in the cleaned url
	// . returns false if blocked, true otherwise
	
	if ( ! msg7->sendInjectionRequestToHost ( ir, st1 , 
						  doneInjectingWrapper3 ) ) {
		// there was an error
		log("http: error sending injection request: %s"
		    ,mstrerror(g_errno));
		// we did not block, but had an error
		return true;
	}

	//log("http: injection did not block");

	// some kinda error, g_errno should be set i guess
	//doneInjectingWrapper3  ( st1 );
	// we did not block
	//return true;
	// wait for the reply, this 'blocked'
	return false;

}


static void doneInjectingWrapper3 ( void *st ) {
	State1i *st1 = (State1i *)st;
	// get the state properly
	//State1i *st1 = (State1i *) state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	//char *url = st1->m_msg7.m_xd.m_firstUrl.m_url;
	Msg7 *msg7 = &st1->m_msg7;
	InjectionRequest *ir = &msg7->m_injectionRequest;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(ir,sizeof(*ir));
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_url,ir->size_url);
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_contentDelim,ir->size_contentDelim);
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_contentFile,ir->size_contentFile);
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_contentTypeStr,ir->size_contentTypeStr);
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_content,ir->size_content);
	VALGRIND_CHECK_MEM_IS_DEFINED(ir->ptr_metadata,ir->size_metadata);
#endif
	char *url = ir->ptr_url;
	log(LOG_INFO,"http: add url %s (%s)",url ,mstrerror(g_errno));
	// extract info from state
	TcpSocket *sock    = st1->m_socket;
	
	//bool       isAdmin = st1->m_isMasterAdmin;
	//char      *url     = NULL;
	//if ( st1->m_urlLen ) url = st1->m_url;
	// re-null it out if just http://
	//bool printUrl = true;
	//if ( st1->m_urlLen == 0 ) printUrl = false;
	//if ( ! st1->m_url       ) printUrl = false;
	//if(st1->m_urlLen==7&&st1->m_url&&!strncasecmp(st1->m_url,"http://",7)
	//	printUrl = false;

	// page is not more than 32k
	char buf[1024*32+MAX_URL_LEN*2];
	SafeBuf sb(buf, 1024*32+MAX_URL_LEN*2);
	
	//char rawbuf[1024*8];
	//SafeBuf rb(rawbuf, 1024*8);	
	//rb.safePrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	//rb.safePrintf("<status>\n");
	//CollectionRec *cr = g_collectiondb.getRec ( st1->m_coll );
	
	// collection name
	const char *coll = st1->m_coll;

	//char tt [ 128 ];
	//tt[0] = '\0';
	//if ( st1->m_coll[0] != '\0' && ! isAdmin ) 
	//	sprintf ( tt , " for %s", st1->m_coll );


	//
	// what we print here will just be the error msg, because the
	// ajax will fill the text we print here into the div below
	// the add url box
	//

	// if there was an error let them know
	//char msg[MAX_URL_LEN + 1024];
	const char *pm = "";
	if ( g_errno ) {
		sb.safePrintf("Error adding url(s): <b>%s[%i]</b>", mstrerror(g_errno) , g_errno);
	} else {
		if ( ! g_conf.m_addUrlEnabled ) {
			pm = "<font color=#ff0000>"
				"Sorry, this feature is temporarily disabled. "
				"Please try again later.</font>";
			if ( url )
				log("addurls: failed for user at %s: "
				    "add url is disabled. "
				    "Enable add url on the "
				    "Master Controls page and "
				    "on the Spider Controls page for "
				    "this collection.", 
				    iptoa(sock->m_ip));

			sb.safePrintf("%s",pm);
			//rb.safePrintf("Sorry, this feature is temporarily "
			//	      "disabled. Please try again later.");
		}
		// did they fail the turing test?
		else if ( ! st1->m_goodAnswer ) {
			pm = "<font color=#ff0000>"
				"Oops, you did not enter the 4 large letters "
				"you see below. Please try again.</font>";
			//rb.safePrintf("could not add the url"
			//	      " because the turing test"
			//	      " is enabled.");
			sb.safePrintf("%s",pm);
		}
		else if ( msg7->m_replyIndexCode ) { 
			//st1->m_msg7.m_xd.m_indexCodeValid &&
			//  st1->m_msg7.m_xd.m_indexCode ) {
			//int32_t ic = st1->m_msg7.m_xd.m_indexCode;
			sb.safePrintf("<b>Had error injecting url: %s</b>",
				      mstrerror(msg7->m_replyIndexCode));
		}
		/*
		if ( url && ! st1->m_ufu[0] && url[0] && printUrl ) {
				sprintf ( msg ,"<u>%s</u> added to spider "
					  "queue "
					  "successfully", url );
				//rb.safePrintf("%s added to spider "
				//	      "queue successfully", url );
		}
		else if ( st1->m_ufu[0] ) {
			sprintf ( msg ,"urls in <u>%s</u> "
				  "added to spider queue "
				  "successfully", st1->m_ufu );

			//rb.safePrintf("urls in %s added to spider "
			//	      "queue successfully", url );

		}
		*/
		else {
			//rb.safePrintf("Add the url you want:");
			// avoid hitting browser page cache
			uint32_t rand32 = rand();
			// in the mime to 0 seconds!
			sb.safePrintf("<b>Url successfully added. "
				      "<a href=/search?rand=%" PRIu32"&"
				      "c=%s&q=url%%3A",
				      rand32,
				      coll);
			sb.urlEncode(url);
			sb.safePrintf(">Check it</a>"// or "
				      //"<a href=http://www.gigablast."
				      //"com/seo?u=");
				      //sb.urlEncode(url);
				      //sb.safePrintf(">SEO it</a>"
				      "."
				      "</b>");
		}
			
		//pm = msg;
		//url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}

	// store it
	sb.safePrintf("<b>%s</b>",pm );

	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;


	// nuke state
	mdelete ( st1 , sizeof(State1i) , "PageAddUrl" );
	delete (st1);

	// this reply should be loaded from the ajax loader so use a cache
	// time of 1 hour so it does not re-inject the url if you hit the
	// back button
	g_httpServer.sendDynamicPage (sock, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");
}


// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static int32_t      s_lastTime = 0;
static bool canSubmit ( uint32_t h , int32_t now , int32_t maxAddUrlsPerIpDomPerDay ) {
	// . sometimes no limit
	// . 0 means no limit because if they don't want any submission they
	//   can just turn off add url and we want to avoid excess 
	//   troubleshooting for why a url can't be added
	if ( maxAddUrlsPerIpDomPerDay <= 0 ) return true;
	// init the table
	if ( ! s_init ) {
		s_htable.set ( 50000 );
		s_init = true;
	}
	// clean out table every 24 hours
	if ( now - s_lastTime > 24*60*60 ) {
		s_lastTime = now;
		s_htable.clear();
	}
	// . if table almost full clean out ALL slots
	// . TODO: just clean out oldest slots
	if ( s_htable.getNumSlotsUsed() > 47000 ) s_htable.clear ();
	// . how many times has this IP domain submitted?
	// . allow 10 times per day
	int32_t n = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( n >= maxAddUrlsPerIpDomPerDay ) return false;
	// otherwise, inc it
	n++;
	// add to table, will replace old values
	s_htable.addKey ( h , n );
	return true;
}


void resetPageAddUrl ( ) {
	s_htable.reset();
}

bool sendPageHelp ( TcpSocket *sock , HttpRequest *hr ) {

	SafeBuf sb;

	CollectionRec *cr = g_collectiondb.getRec ( hr );

	printFrontPageShell ( &sb , "syntax" , cr , true );

	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");

	// submit to https now
	//sb.safePrintf("<form method=GET "
	//	      "action=/addurl name=f>\n" );

	// char *coll = "";
	// if ( cr ) coll = cr->m_coll;
	// if ( cr )
	// 	sb.safePrintf("<input type=hidden name=c value=\"%s\">",
	// 		      cr->m_coll);


	const char *qc = "demo";
	const char *host = ""; // for debug make it local on laptop

	sb.safePrintf(
	"<br>"
	"<table width=650px cellpadding=5 cellspacing=0 border=0>"
	""

	// yellow/gold bar
	"<tr>"
	"<td colspan=2 bgcolor=#%s>" // f3c714>"
	"<b>"
	"Basic Query Syntax"
	"</b>"
	"</td>"
	"</tr>\n"

	"<tr bgcolor=#0340fd>"
	""
	"<th><font color=33dcff>Example Query</font></th>"
	"<th><font color=33dcff>Description</font></th>"
	"</tr>"
	"<tr> "
	"<td><a href=%s/search?c=%s&q=cat+dog>cat dog</a></td>"
	"            <td>Search results have the word <em>cat</em> and the word <em>dog</em> "
	"              in them. They could also have <i>cats</i> and <i>dogs</i>.</td>"
	"          </tr>"
	""
	""
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%2Bcat>+cat</a></td>"
	"            <td>Search results have the word <em>cat</em> in them. If the search results has the word <i>cats</i> then it will not be included. The plus sign indicates an exact match and not to use synonyms, hypernyms or hyponyms or any other form of the word.</td>"
	"          </tr>"
	""
	""
	"          <tr> "
	"            <td height=10><a href=%s/search?c=%s&q=mp3+%%22take+five%%22>mp3&nbsp;\"take&nbsp;five\"</a></td>"
	"            <td>Search results have the word <em>mp3</em> and the exact phrase <em>take "
	"              five</em> in them.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%22john+smith%%22+-%%22bob+dole%%22>\"john&nbsp;smith\"&nbsp;-\"bob&nbsp;dole\"</a></td>"
	"            <td>Search results have the phrase <em>john smith</em> but NOT the "
	"              phrase <em>bob dole</em> in them.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=bmx+-game>bmx&nbsp;-game</a></td>"
	"            <td>Search results have the word <em>bmx</em> but not <em>game</em>.</td>"
	"          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=inurl%%3Aedu+title%%3Auniversity><b>inurl:</b></a><a href=/search?q=inurl%%3Aedu+title%%3Auniversity>edu <b>title:</b>university</a></td>"
	// "            <td>Search results have <em>university</em> in their title and <em>edu</em> "
	// "              in their url.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=site%%3Awww.ibm.com+%%22big+blue%%22><b>site:</b></a><a href=/search?q=site%%3Awww.ibm.com+%%22big+blue%%22>www.ibm.com&nbsp;\"big&nbsp;blue\"</a></td>"
	// "            <td>Search results are from the site <em>www.ibm.com</em> and have the phrase "
	// "              <em>big blue</em> in them.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=url%%3Awww.yahoo.com><b>url:</b></a><a href=/search?q=url%%3Awww.yahoo.com&n=10>www.yahoo.com</a></td>"
	// "            <td>Search result is the single URL www.yahoo.com, if it is indexed.</td>"
	// "          </tr>"

	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><nobr><a href=/search?q=title%%3A%%22the+news%%22+-%%22weather+report%%22><b>title:</b>\"the "
	// "              news\" -\"weather report\"</a></nobr></td>"
	// "            <td>Search results have the phrase <em>the news</em> in their title, "
	// "              and do NOT have the phrase <em>weather report</em> anywhere in their "
	// "              content.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=ip%%3A216.32.120+cars><b>ip:</b></a><a href=/search?q=ip%%3A216.32.120>216.32.120</a></td>"
	// "            <td>Search results are from the the ip 216.32.120.*.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Apdf+nutrition><b>type:</b>pdf nutrition</a></td>"
	// "            <td>Search results are PDF (Portable Document Format) documents that "
	// "              contain the word <em>nutrition</em>.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Adoc><b>type:</b>doc</a></td>"
	// "            <td>Search results are Microsoft Word documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Axls><b>type:</b>xls</a></td>"
	// "            <td>Search results are Microsoft Excel documents.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Appt><b>type:</b>ppt</a></td>"
	// "            <td>Search results are Microsoft Power Point documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=type%%3Aps><b>type:</b>ps</a></td>"
	// "            <td>Search results are Postscript documents.</td>"
	// "          </tr>"
	// "          <tr> "
	// "            <td><a href=/search?q=type%%3Atext><b>type:</b>text</a></td>"
	// "            <td>Search results are plain text documents.</td>"
	// "          </tr>"
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=filetype%%3Apdf><b>filetype:</b>pdf</a></td>"
	// "            <td>Search results are PDF documents.</td>"
	// "          </tr>"
	// ""
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=link%%3Awww.yahoo.com><b>link:</b>www.yahoo.com</a></td>"
	// "            <td>All the pages that link to www.yahoo.com.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=sitelink%%3Awww.yahoo.com><b>sitelink:</b>www.yahoo.com</a></td>"
	// "            <td>All the pages that link to any page on www.yahoo.com.</td>"
	// "          </tr>"
	// ""
	// "          <tr bgcolor=#E1FFFF> "
	// "            <td><a href=/search?q=ext%%3Atxt><b>ext:</b>txt</a></td>"
	// "            <td>All the pages whose url ends in the .txt extension.</td>"
	// "          </tr>"
	// ""
	// ""
	, GOLD

	, host
	, qc
	, host
	, qc
	, host
	, qc
	, host
	, qc
	, host
	, qc
		      );


	sb.safePrintf(
		      // spacer
		      //"<tr><td><br></td><td></td></tr>"

		      //"<tr bgcolor=#0340fd>"
		      // "<td><font color=33dcff><b>Special Query</b>"
		      // "</font></td>"
		      //"<td><font color=33dcff><b>Description</b></font></td>"
		      // "</tr>"
		      "<tr bgcolor=#E1FFFF>"
		      "<td><a href=%s/search?c=%s&q=cat|dog>cat | dog</a>"
		      "</td><td>"
		      "Match documents that have cat and dog in them, but "
		      "do not allow cat to affect the ranking score, only "
		      "dog. This is called a <i>query refinement</i>."
		      "</td></tr>\n"

		      "<tr bgcolor=#ffFFFF>"
		      "<td><a href=%s/search?c=%s&q=document.title:paper>"
		      "document.title:paper</a></td><td>"
		      "That query will match a JSON document like "
		      "<i>"
		      "{ \"document\":{\"title\":\"This is a good paper.\" "
		      "}}</i> or, alternatively, an XML document like <i>"

		      , host
		      , qc
		      , host
		      , qc

		      );
	sb.htmlEncode("<document><title>This is a good paper"
		      "</title></document>" );
	sb.safePrintf("</i></td></tr>\n");


	const char *bg1 = "#E1FFFF";
	const char *bg2 = "#ffffff";
	const char *bgcolor = bg1;

	// table of the query keywords
	int32_t n = getNumFieldCodes();
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get field #i
		QueryField *f = &g_fields[i];

		if ( g_fields[i].m_flag & QTF_HIDE ) continue;
		

		// new table?
		if ( g_fields[i].m_flag & QTF_BEGINNEWTABLE ) {
			sb.safePrintf("</table>"
				      "<br>"
				      "<br>"
				      "<br>"
				      "<table width=650px "
				      "cellpadding=5 cellspacing=0 border=0>"
				      // yellow/gold bar
				      "<tr>"
				      "<td colspan=2 bgcolor=#%s>"//f3c714>"
				      "<b>"
				      "%s"
				      "</b>"
				      "</td>"
				      "</tr>\n"
				      "<tr bgcolor=#0340fd>"
				      "<th><font color=33dcff>"
				      "Example Query</font></th>"
				      "<th><font color=33dcff>"
				      "Description</font></th>"
				      "</tr>\n"
				      , GOLD
				      , g_fields[i].m_title
				      );
		}

		// print it out
		const char *d = f->desc;
		// fix table internal cell bordering
		if ( ! d || d[0] == '\0' ) d = "&nbsp;";
		sb.safePrintf("<tr bgcolor=%s>"
			      "<td><nobr><a href=\"%s/search?c=%s&q="
			      , bgcolor
			      , host
			      , qc
			      );
		sb.urlEncode ( f->example );
		sb.safePrintf("\">");
		sb.safePrintf("%s</a></nobr></td>"
			      "<td>%s</td></tr>\n",
			      f->example,
			      d);

		if ( bgcolor == bg1 ) bgcolor = bg2;
		else                  bgcolor = bg1;
	}




	sb.safePrintf(
	// "          <tr> "
	// "            <td style=padding-bottom:12px;>&nbsp;</td>"
	// "            <td style=padding-bottom:12px;>&nbsp;</td>"
	// "          </tr>"
	// ""

		      "</table>"
		      
		      "<br><br><br>"

		      "<table width=650px "
		      "cellpadding=5 cellspacing=0 border=0>"

	// yellow/gold bar
	"<tr>"
		      "<td colspan=2 bgcolor=#%s>" // f3c714>"
	"<b>"
	"Boolean Queries"
	"</b>"
	"</td>"
	"</tr>\n"


	"<tr bgcolor=#0340fd>"
	""
	"            <th><font color=33dcff>Example Query</font></th>"
	"            <th><font color=33dcff>Description</font></th>"
	""
	"          </tr>"
	""
	"          <tr> "
	"            <td colspan=2 bgcolor=#FFFFCC><center>"
	"                Note: boolean operators must be in UPPER CASE. "
	"              </td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+dog>cat&nbsp;AND&nbsp;dog</a></td>"
	"            <td>Search results have the word <em>cat</em> AND the word <em>dog</em> "
	"              in them.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog>cat&nbsp;OR&nbsp;dog</a></td>"
	"            <td>Search results have the word <em>cat</em> OR the word <em>dog</em> "
	"              in them, but preference is given to results that have both words.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+dog+OR+pig>cat&nbsp;dog&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results have the two words <em>cat</em> and <em>dog</em> "
	"              OR search results have the word <em>pig</em>, but preference is "
	"              given to results that have all three words. This illustrates how "
	"              the individual words of one operand are all required for that operand "
	"              to be true.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%22cat+dog%%22+OR+pig>\"cat&nbsp;dog\"&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results have the phrase <em>\"cat dog\"</em> in them OR they "
	"              have the word <em>pig</em>, but preference is given to results that "
	"              have both.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=title%%3A%%22cat+dog%%22+OR+pig>title</a><a href=%s/search?c=%s&q=title%%3A%%22cat+dog%%22+OR+pig>:\"cat "
	"              dog\" OR pig</a></td>"
	"            <td>Search results have the phrase <em>\"cat dog\"</em> in their title "
	"              OR they have the word <em>pig</em>, but preference is given to results "
	"              that have both.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog+OR+pig>cat&nbsp;OR&nbsp;dog&nbsp;OR&nbsp;pig</a></td>"
	"            <td>Search results need only have one word, <em>cat</em> or <em>dog</em> "
	"              or <em>pig</em>, but preference is given to results that have the "
	"              most of the words.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+OR+dog+AND+pig>cat&nbsp;OR&nbsp;dog&nbsp;AND&nbsp;pig</a></td>"
	"            <td>Search results have <em>dog</em> and <em>pig</em>, but they may "
	"              or may not have <em>cat</em>. Preference is given to results that "
	"              have all three. To evaluate expressions with more than two operands, "
	"              as in this case where we have three, you can divide the expression "
	"              up into sub-expressions that consist of only one operator each. "
	"              In this case we would have the following two sub-expressions: <em>cat "
	"              OR dog</em> and <em>dog AND pig</em>. Then, for the original expression "
	"              to be true, at least one of the sub-expressions that have an OR "
	"              operator must be true, and, in addition, all of the sub-expressions "
	"              that have AND operators must be true. Using this logic you can evaluate "
	"              expressions with more than one boolean operator.</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+NOT+dog>cat&nbsp;AND&nbsp;NOT&nbsp;dog</a></td>"
	"            <td>Search results have <em>cat</em> but do not have <em>dog</em>.</td>"
	"          </tr>"
	"          <tr> "
	"            <td><a href=%s/search?c=%s&q=cat+AND+NOT+%%28dog+OR+pig%%29>cat&nbsp;AND&nbsp;NOT&nbsp;(dog&nbsp;OR&nbsp;pig)</a></td>"
	"            <td>Search results have <em>cat</em> but do not have <em>dog</em> "
	"              and do not have <em>pig</em>. When evaluating a boolean expression "
	"              that contains ()'s you can evaluate the sub-expression in the ()'s "
	"              first. So if a document has <em>dog</em> or it has <em>pig</em> "
	"              or it has both, then the expression, <em>(dog OR pig)</em> would "
	"              be true. So you could, in this case, substitute <em>true</em> for "
	"              that expression to get the following: <em>cat AND NOT (true) = cat "
	"              AND false = false</em>. Does anyone actually read this far?</td>"
	"          </tr>"
	"          <tr bgcolor=#E1FFFF> "
	"            <td><a href=%s/search?c=%s&q=%%28cat+OR+dog%%29+AND+NOT+%%28cat+AND+dog%%29>(cat&nbsp;OR&nbsp;dog)&nbsp;AND&nbsp;NOT&nbsp;(cat&nbsp;AND&nbsp;dog)</a></td>"
	"            <td>Search results have <em>cat</em> or <em>dog</em> but not both.</td>"
	"          </tr>"
	"          <tr> "
	"            <td>left-operand&nbsp;&nbsp;OPERATOR&nbsp;&nbsp;right-operand</td>"
	"            <td>This is the general format of a boolean expression. The possible "
	"              operators are: OR and AND. The operands can themselves be boolean "
	"              expressions and can be optionally enclosed in parentheses. A NOT "
	"              operator can optionally preceed the left or the right operand.</td>"
	"          </tr>"
	""
	//"        </table>"
	""
	""
	""
	//"</td></tr>"
	//"</table>"
	//"<br>"
		      , GOLD
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      , host
		      , qc
		      );



	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");
	//sb.safePrintf("<tr><td></td><td></td></tr>\n");

	
	sb.safePrintf("</table>");


	//sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");

	printNav ( sb , hr );

	g_httpServer.sendDynamicPage (sock, 
				      sb.getBufStart(), 
				      sb.length(),
				      3600, // cachetime
				      false,// post?
				      "text/html",
				      200, // http status
				      NULL, // cookie
				      "UTF-8");

	return true;
}

