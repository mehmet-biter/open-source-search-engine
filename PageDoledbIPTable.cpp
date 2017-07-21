#include "Pages.h"
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "SafeBuf.h"
#include "Collectiondb.h"
#include "SpiderColl.h"
#include "ip.h"
#include <algorithm>



static void generatePageHtml(std::vector<uint32_t> &doleips, const char *coll, SafeBuf *sb) {
	std::sort(doleips.begin(), doleips.end());
	
	sb->safePrintf("<table cellpadding=5 style=\"max-width:25em\" border=0>"
	               "  <tr>"
	               "    <td>For collection <i>%s</i> (%zu IPs):</td>"
	               "  </tr>"
	               "</table>\n",
	               coll,
		       doleips.size()
      		);
	
	sb->safePrintf("<table %s>\n"
	               "  <tr>\n"
		       "    <td><b>IPs in DoledbIP table</b></td>\n"
		       "  </tr>\n"
		       , TABLE_STYLE);
	
	for(auto ip : doleips) {
		char ipbuf[16];
		sb->safePrintf("  <tr bgcolor=#%s><td>%s</td></tr>\n", LIGHT_BLUE, iptoa(ip,ipbuf));
	}
	
	sb->safePrintf("</table>\n");
}


static void generatePageJSON(std::vector<uint32_t> &doleips, const char *coll, SafeBuf *sb) {
	//order is irrelevant for JSON, but some humans like to look at it.
	std::sort(doleips.begin(), doleips.end());
	
	sb->safePrintf("{\n");                 //object start
	
	sb->safePrintf("  \"ips\": [\n");      //value-name and start of array
	
	bool first=true;
	for(auto ip : doleips) {
		if(!first)
			sb->safePrintf(",\n");
		char ipbuf[16];
		sb->safePrintf("    \"%s\"", iptoa(ip,ipbuf));
		first = false;
	}
	if(!first)
		sb->safePrintf("\n");
	
	sb->safePrintf("  ]\n");               //end array
	
	sb->safePrintf("}\n");                 //end object
}


static bool respondWithError(TcpSocket *s, HttpRequest *r, const char *msg) {
	SafeBuf sb;
	const char *contentType = NULL;
	switch(r->getReplyFormat()) {
		case FORMAT_HTML:
			sb.safePrintf("<html><body><p>%s</p></body></html>",msg);
			contentType = "text/html";
			break;
		case FORMAT_JSON:
			sb.safePrintf("{error_message:\"%s\"}",msg); //todo: safe encode
			contentType = "text/html";
			break;
		default:
			contentType = "application/octet-stream";
			break;
	}
	
	return g_httpServer.sendDynamicPage(s, sb.getBufStart(), sb.length(), -1, false, contentType);
}



bool sendPageDoledbIPTable(TcpSocket *s, HttpRequest *r) {
	const char *coll = r->getString("c", NULL, NULL);
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if(!cr) {
		return respondWithError(s, r, "No collection specified");
	}
	
	SpiderColl *spiderColl = cr->m_spiderColl;
	if(!spiderColl) {
		return respondWithError(s, r, "No spider-collection (?)");
	}
	
	std::vector<uint32_t> doleips = spiderColl->getDoledbIpTable();
	
	SafeBuf sb;
	
	const char *contentType = NULL;
	switch(r->getReplyFormat()) {
		case FORMAT_HTML:
			g_pages.printAdminTop(&sb, s, r, NULL);
			generatePageHtml(doleips, coll, &sb);
			g_pages.printAdminBottom2(&sb);
			contentType = "text/html";
			break;
		case FORMAT_JSON:
			generatePageJSON(doleips, coll, &sb);
			contentType = "application/json";
			break;
		default:
			contentType = "text/html";
			sb.safePrintf("oops!");
			break;
			
	}
	
	return g_httpServer.sendDynamicPage(s, sb.getBufStart(), sb.length(), -1, false, contentType);
}
