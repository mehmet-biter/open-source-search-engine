#include "gb-include.h"

#include "Stats.h"
#include "Pages.h"
#include "Hostdb.h"

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPagePerf ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	StackBuf<64*1024> p;
	p.setLabel ( "perfgrph" );

	// print standard header
	g_pages.printAdminTop ( &p , s , r );



	// password, too
	//int32_t pwdLen = 0;
	//char *pwd = r->getString ( "pwd" , &pwdLen );

	int32_t autoRefresh = r->getLong("rr", 0);
	if(autoRefresh > 0) {
		p.safePrintf("<script language=\"JavaScript\"><!--\n ");

		p.safePrintf( "\nfunction timeit() {\n"
			     " setTimeout('loadXMLDoc(\"%s&refresh=1"
			     "&dontlog=1\")',\n"
			     " 500);\n }\n ",  
			     r->getRequest() + 4/*skip over GET*/); 
		p.safePrintf(
		     "var req;"
		     "function loadXMLDoc(url) {"
		     "    if (window.XMLHttpRequest) {"
		     "        req = new XMLHttpRequest();" 
		     "        req.onreadystatechange = processReqChange;"
		     "        req.open(\"GET\", url, true);"
		     "        req.send(null);"
		     "    } else if (window.ActiveXObject) {"
		     "        req = new ActiveXObject(\"Microsoft.XMLHTTP\");"
		     "        if (req) {"
		     "            req.onreadystatechange = processReqChange;"
		     "            req.open(\"GET\", url, true);"
		     "            req.send();"
		     "        }"
		     "    }"
		     "}"
		     "function processReqChange() {"
		     "    if (req.readyState == 4) {"
		     "        if (req.status == 200) {"
		     "        var uniq = new Date();"
		     "        uniq.getTime();"
		     "   document.diskgraph.src=\"/diskGraph%" PRId32".gif?\" + uniq;"
		     "        timeit();"
		     "    } else {"
	     //   "            alert(\"There was a problem retrieving \");"
		     "        }"
		     "    }"
		     "} \n ",g_hostdb.m_hostId);

		p.safePrintf( "// --></script>");

		p.safePrintf("<body onLoad=\"timeit();\">"); 
	}


	//get the 'path' part of the request.
	char rbuf[1024];
	if(r->getRequestLen() > 1023) {
		gbmemcpy( rbuf, r->getRequest(), 1023);
	}
	else {
		gbmemcpy( rbuf, r->getRequest(), r->getRequestLen());
	}
	char* rbufEnd = rbuf;
	//skip GET
	while (!isspace(*rbufEnd)) rbufEnd++;
	//skip space(s)
	while (isspace(*rbufEnd)) rbufEnd++;
	//skip request path
	while (!isspace(*rbufEnd)) rbufEnd++;
	*rbufEnd = '\0';

	// print resource table
	// columns are the dbs
	p.safePrintf("<center>");

	// now try using absolute divs instead of a GIF
	g_stats.printGraphInHtml ( p );

	// print the key
	p.safePrintf (
		      "<br>"
		       "<center>"

		       "<style>"
		       ".poo { background-color:#%s;}\n"
		       "</style>\n"


		       "<table %s>"

		       // black
		       "<tr class=poo>"
		       "<td bgcolor=#000000>&nbsp; &nbsp;</td>"
		       "<td> High priority disk read. "
		       "Thicker lines for bigger reads.</td>"

		       // grey
		       "<td bgcolor=#808080>&nbsp; &nbsp;</td>"
		       "<td> Low priority disk read. "
		       "Thicker lines for bigger reads.</td>"
		       "</tr>"


		       // red
		       "<tr class=poo>"
		       "<td bgcolor=#ff0000>&nbsp; &nbsp;</td>"
		       "<td> Disk write. "
		       "Thicker lines for bigger writes.</td>"

		       // blue
		       "<td bgcolor=#0000ff>&nbsp; &nbsp;</td>"
		       "<td> Summary extraction for one document.</td>"
		       "</tr>"


		       // pinkish purple
		       "<tr class=poo>"
		       "<td bgcolor=#aa00aa>&nbsp; &nbsp;</td>"
		       "<td> Send data over network. (low priority)"
		       "Thicker lines for bigger sends.</td>"

		       // pinkish purple
		       "<td bgcolor=#ff00ff>&nbsp; &nbsp;</td>"
		       "<td> Send data over network.  (high priority)"
		       "Thicker lines for bigger sends.</td>"
		       "</tr>"

		       // dark purple
		       "<tr class=poo>"
		       "<td bgcolor=#8220ff>&nbsp; &nbsp;</td>"
		       "<td> Get all summaries for results.</td>"

		       // turquoise
		       "<td bgcolor=#00ffff>&nbsp; &nbsp;</td>"
		       "<td> Merge multiple disk reads. Real-time searching. "
		       "Thicker lines for bigger merges.</td>"
		       "</tr>"

		       // white
		       "<tr class=poo>"
		       "<td bgcolor=#ffffff>&nbsp; &nbsp;</td>"
		       "<td> Uncompress cached document.</td>"

		       // bright green
		       "<td bgcolor=#00ff00>&nbsp; &nbsp;</td>"
		       "<td> Compute search results. </td>"
		       "</tr>"

		       "<tr class=poo>"
		       "<td bgcolor=#0000b0>&nbsp; &nbsp;</td>"
		       "<td> \"Summary\" extraction (low priority) "
		       "</td>"

		       "<td>&nbsp; &nbsp;</td>"
		       "<td> &nbsp; "
		       "</td>"
		       "</tr>"
		       

		       "</table>"
		       "</center>"
		       , LIGHT_BLUE 
		       , TABLE_STYLE
		       );

	if(autoRefresh > 0) p.safePrintf("</body>"); 

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	int32_t bufLen = p.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s, p.getBufStart(), bufLen );
}
