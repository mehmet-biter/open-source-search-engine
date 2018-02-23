#include "SafeBuf.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "SearchInput.h"
#include "PageCrawlBot.h"
#include "Collectiondb.h"
#include "Pages.h"
#include "Parms.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "SpiderLoop.h"
#include "PageResults.h" // for RESULT_HEIGHT
#include "Stats.h"
#include "PageRoot.h"


// 5 seconds
#define DEFAULT_WIDGET_RELOAD 1000

///////////
//
// main > Basic > Settings
//
///////////

bool printSitePatternExamples ( SafeBuf *sb , HttpRequest *hr ) {

	// true = useDefault?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) return true;

	/*
	// it is a safebuf parm
	char *siteList = cr->m_siteListBuf.getBufStart();
	if ( ! siteList ) siteList = "";

	SafeBuf msgBuf;
	char *status = "";
	int32_t max = 1000000;
	if ( cr->m_siteListBuf.length() > max ) {
		msgBuf.safePrintf( "<font color=red><b>"
				   "Site list is over %" PRId32" bytes large, "
				   "too many to "
				   "display on this web page. Please use the "
				   "file upload feature only for now."
				   "</b></font>"
				   , max );
		status = " disabled";
	}
	*/


	/*
	sb->safePrintf(
		       "On the command like you can issue a command like "

		       "<i>"
		       "gb addurls &lt; fileofurls.txt"
		       "</i> or "

		       "<i>"
		       "gb addfile &lt; *.html"
		       "</i> or "

		       "<i>"
		       "gb injecturls &lt; fileofurls.txt"
		       "</i> or "

		       "<i>"
		       "gb injectfile &lt; *.html"
		       "</i> or "

		       "to schedule downloads or inject content directly "
		       "into Gigablast."

		       "</td><td>"

		       "<input "
		       "size=20 "
		       "type=file "
		       "name=urls>"
		       "</td></tr>"

		       );
	*/	      

	// example table
	sb->safePrintf ( "<a name=examples></a>"
			 "<table %s>"
			 "<tr class=hdrow><td colspan=2>"
			 "<center><b>Site List Examples</b></tr></tr>"
			 //"<tr bgcolor=#%s>"
			 //"<td>"
			 ,TABLE_STYLE );//, DARK_BLUE);
			 

	sb->safePrintf(
		       //"*"
		       //"</td>"
		       //"<td>Spider all urls encountered. If you just submit "
		       //"this by itself, then Gigablast will initiate spidering "
		       //"automatically at dmoz.org, an internet "
		      //"directory of good sites.</td>"
		       //"</tr>"

		      "<tr>"
		      "<td>goodstuff.com</td>"
		      "<td>"
		      "Spider the url <i>goodstuff.com/</i> and spider "
		      "any links we harvest that have the domain "
		      "<i>goodstuff.com</i>"
		      "</td>"
		      "</tr>"

		      // protocol and subdomain match
		      "<tr>"
		      "<td>http://www.goodstuff.com/</td>"
		      "<td>"
		      "Spider the url "
		      "<i>http://www.goodstuff.com/</i> and spider "
		      "any links we harvest that start with "
		      "<i>http://www.goodstuff.com/</i>. NOTE: if the url "
		      "www.goodstuff.com redirects to foo.goodstuff.com then "
		      "foo.goodstuff.com still gets spidered "
		      "because it is considered to be manually added, but "
		      "no other urls from foo.goodstuff.com will be spidered."
		      "</td>"
		      "</tr>"

		      // protocol and subdomain match
		      "<tr>"
		      "<td>http://justdomain.com/foo/</td>"
		      "<td>"
		      "Spider the url "
		      "<i>http://justdomain.com/foo/</i> and spider "
		      "any links we harvest that start with "
		      "<i>http://justdomain.com/foo/</i>. "
		      "Urls that start with "
		      "<i>http://<b>www.</b>justdomain.com/</i>, for example, "
		      "will NOT match this."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>seed:www.goodstuff.com/myurl.html</td>"
		      "<td>"
		      "Spider the url <i>www.goodstuff.com/myurl.html</i>. "
		      "Add any outlinks we find into the "
		      "spider queue, but those outlinks will only be "
		      "spidered if they "
		      "match ANOTHER line in this site list."
		      "</td>"
		      "</tr>"


		      // protocol and subdomain match
		      "<tr>"
		      "<td>site:http://www.goodstuff.com/</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>http://www.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // subdomain match
		      "<tr>"
		      "<td>site:www.goodstuff.com</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>www.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-site:bad.goodstuff.com</td>"
		      "<td>"
		      "Do not spider any urls starting with "
		      "<i>bad.goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // domain match
		      "<tr>"
		      "<td>site:goodstuff.com</td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>goodstuff.com/</i> to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"

		      // spider this subdir
		      "<tr>"
		      "<td><nobr>site:"
		      "http://www.goodstuff.com/goodir/anotherdir/</nobr></td>"
		      "<td>"
		      "Allow any urls starting with "
		      "<i>http://www.goodstuff.com/goodir/anotherdir/</i> "
		      "to be spidered "
		      "if encountered."
		      "</td>"
		      "</tr>"


		      // exact match
		      
		      //"<tr>"
		      //"<td>exact:http://xyz.goodstuff.com/myurl.html</td>"
		      //"<td>"
		      //"Allow this specific url."
		      //"</td>"
		      //"</tr>"

		      /*
		      // local subdir match
		      "<tr>"
		      "<td>file://C/mydir/mysubdir/"
		      "<td>"
		      "Spider all files in the given subdirectory or lower. "
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-file://C/mydir/mysubdir/baddir/"
		      "<td>"
		      "Do not spider files in this subdirectory."
		      "</td>"
		      "</tr>"
		      */

		      // connect to a device and index it as a stream
		      //"<tr>"
		      //"<td>stream:/dev/eth0"
		      //"<td>"
		      //"Connect to a device and index it as a stream. "
		      //"It will be treated like a single huge document for "
		      //"searching purposes with chunks being indexed in "
		      //"realtime. Or chunk it up into individual document "
		      //"chunks, but proximity term searching will have to "
		      //"be adjusted to compute query term distances "
		      //"inter-document."
		      //"</td>"
		      //"</tr>"

		      // negative subdomain match
		      "<tr>"
		      "<td>contains:goodtuff</td>"
		      "<td>Spider any url containing <i>goodstuff</i>."
		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td>-contains:badstuff</td>"
		      "<td>Do not spider any url containing <i>badstuff</i>."
		      "</td>"
		      "</tr>"

		      /*
		      "<tr>"
		      "<td>regexp:-pid=[0-9A-Z]+/</td>"
		      "<td>Url must match this regular expression. "
		      "Try to avoid using these if possible; they can slow "
		      "things down and are confusing to use."
		      "</td>"
		      "</tr>"
		      */

		      // tag match
		      "<tr><td>"
		      //"<td>tag:boots contains:boots<br>"
		      "<nobr>tag:boots site:www.westernfootwear."
		      "</nobr>com<br>"
		      "tag:boots cowboyshop.com<br>"
		      "tag:boots contains:/boots<br>"
		      "tag:boots site:www.moreboots.com<br>"
		      "<nobr>tag:boots http://lotsoffootwear.com/"
		      "</nobr><br>"
		      //"<td>t:boots -contains:www.cowboyshop.com/shoes/</td>"
		      "</td><td>"
		      "Advance users only. "
		      "Tag any urls matching these 5 url patterns "
		      "so we can use "
		      "the expression <i>tag:boots</i> in the "
		      "<a href=\"/admin/filters\">url filters</a> and perhaps "
		      "give such urls higher spider priority. "
		      "For more "
		      "precise spidering control over url subsets. "
		      "Preceed any pattern with the tagname followed by "
		      "space to tag it."
		      "</td>"
		      "</tr>"


		      "<tr>"
		      "<td># This line is a comment.</td>"
		      "<td>Empty lines and lines starting with # are "
		      "ignored."
		      "</td>"
		      "</tr>"

		      "</table>"
		      );

	return true;
}

///////////
//
// main > Basic > Status
//
///////////
bool sendPageBasicStatus ( TcpSocket *socket , HttpRequest *hr ) {
	StackBuf<128000> sb;

	char format = hr->getReplyFormat();


	// true = usedefault coll?
	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) {
		g_httpServer.sendErrorReply(socket,500,"invalid collection");
		return true;
	}

	if ( format == FORMAT_JSON || format == FORMAT_XML) {
		// this is in PageCrawlBot.cpp
		printCrawlDetails2 ( &sb , cr , format );
		const char *ct = "text/xml";
		if ( format == FORMAT_JSON ) ct = "application/json";
		return g_httpServer.sendDynamicPage (socket,
						     sb.getBufStart(),
						     sb.length(),
						     0, // cachetime
						     false,//POSTReply        ,
						     ct);
	}

	// print standard header
	if ( format == FORMAT_HTML ) {
		// this prints the <form tag as well
		g_pages.printAdminTop ( &sb , socket , hr );

		// table to split between widget and stats in left and right panes
		sb.safePrintf("<TABLE id=pane>"
			      "<TR><TD valign=top>");
	}

	int32_t savedLen1, savedLen2;

	//
	// widget
	//
	// put the widget in here, just sort results by spidered date
	//
	// the scripts do "infinite" scrolling both up and down.
	// but if you are at the top then new results will load above
	// you and we try to maintain your current visual state even though
	// the scrollbar position will change.
	//
	if ( format == FORMAT_HTML ) {

		// save position so we can output the widget code
		// so user can embed it into their own web page
		savedLen1 = sb.length();
		
		savedLen2 = sb.length();

		// the right table pane is the crawl stats
		sb.safePrintf("</TD><TD valign=top>");

		//
		// show stats
		//
		const char *crawlMsg;
		spider_status_t crawlStatus;
		getSpiderStatusMsg ( cr , &crawlMsg, &crawlStatus );

		sb.safePrintf(
			      "<table id=stats border=0 cellpadding=5>"

			      "<tr>"
			      "<td><b>Crawl Status Code:</td>"
			      "<td>%" PRId32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status Msg:</td>"
			      "<td>%s</td>"
			      "</tr>"
			      , (int)crawlStatus
			      , crawlMsg);

		sb.safePrintf("</table>\n\n");

		// end the right table pane
		sb.safePrintf("</TD></TR></TABLE>");
	}


	//if ( format != FORMAT_JSON )
	//	// wrap up the form, print a submit button
	//	g_pages.printAdminBottom ( &sb );

	return g_httpServer.sendDynamicPage (socket,
					     sb.getBufStart(),
					     sb.length(),
					     0); // cachetime
}
