//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#include "Collectiondb.h"
#include "HttpServer.h"
#include "HttpRequest.h"
#include "Msg0.h"
#include "Pages.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Mem.h"
#include "Conf.h"
#include "ip.h"
#include "Linkdb.h"
#include "SiteGetter.h"


namespace {

	struct State {
		TcpSocket   *m_socket;
		HttpRequest  m_r;

		collnum_t    m_collnum;
		char         m_url_str[MAX_URL_LEN];    // the url we are working on. Empty if none

		Url          m_url;
		Msg0         m_msg0;        // for getting linkdb records
		RdbList      m_rdbList;     // spiderdb records
	};

}

static bool getLinkdbRecs(State *st);
static void gotLinkdbRecs(void *state);
static bool sendResult(State *st);

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the linkdb entries for url given via cgi
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageLinkdbLookup(TcpSocket *s, HttpRequest *r) {
	// get the url from the cgi vars
	int url_len;
	const char *url = r->getString("url", &url_len);
	if(url && !url[0])
		url = NULL;

	//set up state
	State *st;
	try {
		st = new State;
	} catch(std::bad_alloc&) {
		g_errno = ENOMEM;
		log(LOG_ERROR, "PageLinkdbLookup: new(%i): %s", (int)sizeof(State), mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s, 500, mstrerror(g_errno));
	}

	mnew(st, sizeof(*st), "pglinkdblookup");
	st->m_socket = s;
	st->m_r.copy(r);

	int32_t collLen = 0;
	const char *coll = st->m_r.getString("c", &collLen);
	if (!coll || !coll[0]) {
		coll = g_conf.getDefaultColl();
		collLen = strlen(coll);
	}

	const CollectionRec *cr = g_collectiondb.getRec(coll, collLen);
	if (!cr) {
		g_errno = ENOCOLLREC;
		return sendResult(st);
	}
	st->m_collnum = cr->m_collnum;

	if (url) {
		memcpy(st->m_url_str, url, url_len);
		st->m_url_str[url_len] = '\0';
		st->m_url.set(st->m_url_str);
	} else
		st->m_url_str[0] = '\0';

	//if an URL has been specified the start working on that. Otherwise just show the initial page
	if (st->m_url_str[0])
		return getLinkdbRecs(st);
	else
		return sendResult(st);
}

static bool getLinkdbRecs(State *st) {
	logTrace(g_conf.m_logTracePageLinkdbLookup, "(%p)",st);

	Url u;
	u.set(st->m_url_str, strlen(st->m_url_str), false, false);

	SiteGetter sg;
	sg.getSite(st->m_url_str, NULL, 0, 0, 0);

	uint32_t h32 = hash32(sg.getSite(), sg.getSiteLen(), 0);
	key224_t startKey = Linkdb::makeStartKey_uk(h32, u.getUrlHash64());
	key224_t endKey = Linkdb::makeEndKey_uk(h32, u.getUrlHash64());

	logTrace(g_conf.m_logTracePageLinkdbLookup, "(%p): Calling Msg0::getList()", st);

	if(!st->m_msg0.getList(-1, //hostId
	                       RDB_LINKDB,
	                       st->m_collnum,
	                       &st->m_rdbList,
	                       (const char*)&startKey,
	                       (const char*)&endKey,
	                       1000000,  //minRecSizes, -1 is not supported. We don't expect the two expected records to exceed 1MB
	                       st,
	                       gotLinkdbRecs,
	                       0, //niceness,
	                       true, //doErrorCorrection
	                       true, //includeTree,
	                       -1, //firstHostId
	                       0, //startFileNum,
	                       -1, //numFiles,
	                       10000, //timeout in msecs
	                       false, //isRealMerge
	                       false, //noSplit (?)
	                       -1)) {//forceParitySplit
		return false;
	}

	logTrace(g_conf.m_logTracePageLinkdbLookup, "msg0.getlist didn't block");
	gotLinkdbRecs(st);
	return true;
}



static void gotLinkdbRecs(void *state) {
	logTrace(g_conf.m_logTracePageLinkdbLookup, "(%p)", state);
	State *st = reinterpret_cast<State*>(state);
	sendResult(st);
}

static bool respondWithError(State *st, int32_t error, const char *errmsg) {
	// get the socket
	TcpSocket *s = st->m_socket;

	SafeBuf sb;
	const char *contentType = NULL;
	switch(st->m_r.getReplyFormat()) {
		case FORMAT_HTML:
			g_pages.printAdminTop(&sb, s, &st->m_r, NULL);
			sb.safePrintf("<p>%s</p>", errmsg);
			g_pages.printAdminBottom2(&sb);
			contentType = "text/html";
			break;
		case FORMAT_JSON:
			sb.safePrintf("{\"response\":{\n"
				              "\t\"statusCode\":%" PRId32",\n"
				              "\t\"statusMsg\":\"", error);
			sb.jsonEncode(errmsg);
			sb.safePrintf("\"\n"
				              "}\n"
				              "}\n");
			contentType = "application/json";
			break;
		default:
			contentType = "application/octet-stream";
			break;
	}

	mdelete (st, sizeof(State) , "pglinkdblookup");
	delete st;

	return g_httpServer.sendDynamicPage(s, sb.getBufStart(), sb.length(), -1, false, contentType);
}


static void generatePageHtml(int32_t shardNum, const char *url, RdbList *list, SafeBuf *sb) {
	// print URL in box
	sb->safePrintf("<br>\n"
		               "Enter URL: "
		               "<input type=text name=url value=\"%s\" size=60>", url);
	sb->safePrintf("</form><br/><br/>\n");

	if (shardNum >= 0) {
		sb->safePrintf("<table class=\"main\" width=100%%>\n");
		sb->safePrintf("<tr class=\"level1\"><th colspan=50>Host information</th></tr>\n");
		sb->safePrintf("<tr><td>Shard:</td><td>%u</td></tr>\n", static_cast<uint32_t>(shardNum));
		sb->safePrintf("</table>\n");
		sb->safePrintf("<br/>\n");
	}

	sb->safePrintf("<table class=\"main\" width=100%%>\n");
	sb->safePrintf("  <tr class=\"level1\"><th colspan=50>Linkdb records</th></tr>\n");
	sb->safePrintf("  <tr class=\"level2\">");
	sb->safePrintf("<th>linkeesitehash32</th>");
	sb->safePrintf("<th>linkeeurlhash</th>");
	sb->safePrintf("<th>islinkspam</th>");
	sb->safePrintf("<th>siterank</th>");
	sb->safePrintf("<th>ip32</th>");
	sb->safePrintf("<th>docid</th>");
	sb->safePrintf("<th>discovered</th>");
	sb->safePrintf("<th>sitehash32</th>");
	sb->safePrintf("<th>isdel</th>");
	sb->safePrintf("</tr>\n");

	for (list->resetListPtr(); !list->isExhausted(); list->skipCurrentRecord()) {
		key224_t k;
		list->getCurrentKey((char *) &k);

		char ipbuf[16];

		sb->safePrintf("  <tr>");
		sb->safePrintf("<td>0x%08" PRIx32"</td>", Linkdb::getLinkeeSiteHash32_uk(&k));
		sb->safePrintf("<td>0x%12" PRIx64"</td>", Linkdb::getLinkeeUrlHash64_uk(&k));
		sb->safePrintf("<td>%s</td>", Linkdb::isLinkSpam_uk(&k) ? "true" : "false");
		sb->safePrintf("<td>%" PRId32"</td>", Linkdb::getLinkerSiteRank_uk(&k));
		sb->safePrintf("<td>%s</td>", iptoa((int32_t)Linkdb::getLinkerIp_uk(&k),ipbuf));
		sb->safePrintf("<td>%" PRIu64"</td>", Linkdb::getLinkerDocId_uk(&k));
		sb->safePrintf("<td>%" PRIu32"</td>", Linkdb::getDiscoveryDate_uk(&k));
		sb->safePrintf("<td>0x%08" PRIx32"</td>", Linkdb::getLinkerSiteHash32_uk(&k));
		sb->safePrintf("<td>%s</td>", KEYNEG((const char*)&k) ? "true" : "false");
		sb->safePrintf("</tr>\n");
	}
	sb->safePrintf("</table>\n");
}

static void generatePageJSON(int32_t shardNum, RdbList *list, SafeBuf *sb) {
	sb->safePrintf("{\n");
	if (shardNum >= 0) {
		sb->safePrintf("\"shard\": %u,\n", static_cast<uint32_t>(shardNum));
	}

	sb->safePrintf("\"results\": [\n");
	for (list->resetListPtr(); !list->isExhausted(); list->skipCurrentRecord()) {
		key224_t k;
		list->getCurrentKey((char *) &k);

		char ipbuf[16];

		sb->safePrintf("{\n");

		sb->safePrintf("\t\"linkeesitehash32\": %" PRIu32",\n", Linkdb::getLinkeeSiteHash32_uk(&k));
		sb->safePrintf("\t\"linkeeurlhash\": %" PRIu64",\n", Linkdb::getLinkeeUrlHash64_uk(&k));
		sb->safePrintf("\t\"islinkspam\": %s,\n", Linkdb::isLinkSpam_uk(&k) ? "true" : "false");
		sb->safePrintf("\t\"siterank\": %hhu,\n", Linkdb::getLinkerSiteRank_uk(&k));
		sb->safePrintf("\t\"ip32\": \"%s\",\n", iptoa((int32_t)Linkdb::getLinkerIp_uk(&k),ipbuf));
		sb->safePrintf("\t\"docid\": %" PRId64",\n", Linkdb::getLinkerDocId_uk(&k));
		sb->safePrintf("\t\"discovered\": %d,\n", Linkdb::getDiscoveryDate_uk(&k));
		sb->safePrintf("\t\"sitehash32\": %" PRIu32",\n", Linkdb::getLinkerSiteHash32_uk(&k));
		sb->safePrintf("\t\"isdel\": %s\n", KEYNEG((const char*)&k) ? "true" : "false");

		sb->safePrintf("},\n");
	}

	sb->removeLastChar('\n');
	sb->removeLastChar(',');
	sb->safePrintf("\n]");

	sb->safePrintf("\n}");
}

static bool sendResult(State *st) {
	logTrace(g_conf.m_logTracePageLinkdbLookup, "st(%p): sendResult: g_errno=%d", st, g_errno);

	// get the socket
	TcpSocket *s = st->m_socket;

	SafeBuf sb;
	// print standard header
	sb.reserve2x ( 32768 );

	if(g_errno) {
		return respondWithError(st, g_errno, mstrerror(g_errno));
	}

	int32_t shardNum = -1;
	if(st->m_url_str[0]) {
		Url u;
		u.set(st->m_url_str, strlen(st->m_url_str), false, false);

		uint32_t h32 = u.getHostHash32();
		uint64_t uh64 = hash64n(u.getUrl(), u.getUrlLen());
		key224_t startKey = Linkdb::makeStartKey_uk(h32, uh64);
		shardNum = g_hostdb.getShardNum(RDB_LINKDB, &startKey);
	}

	const char *contentType = NULL;
	switch(st->m_r.getReplyFormat()) {
		case FORMAT_HTML:
			g_pages.printAdminTop(&sb, s, &st->m_r, NULL);
			generatePageHtml(shardNum, st->m_url_str, &st->m_rdbList, &sb);
			g_pages.printAdminBottom2(&sb);
			contentType = "text/html";
			break;
		case FORMAT_JSON:
			generatePageJSON(shardNum, &st->m_rdbList, &sb);
			contentType = "application/json";
			break;
		default:
			contentType = "text/html";
			sb.safePrintf("oops!");
			break;

	}

	// don't forget to cleanup
	mdelete(st, sizeof(State) , "pglinkdblookup");
	delete st;

	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), sb.length(), -1, false, contentType);
}
