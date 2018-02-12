#include "Collectiondb.h"
#include "HttpServer.h"
#include "HttpRequest.h"
#include "Msg0.h"
#include "Pages.h"
#include "SafeBuf.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Mem.h"
#include "Conf.h"
#include "ip.h"
#include "max_url_len.h"
#include "GbUtil.h"



namespace {

struct State {
	TcpSocket   *m_socket;
	HttpRequest  m_r;
	
	const char  *m_coll;
	int32_t      m_collLen;
	collnum_t    m_collnum;
	char         m_url_str[MAX_URL_LEN];	//the url we are working on. Empty if none
	
	Url          m_url;
	int32_t      m_firstip;
	Msg8a        m_msg8a;			//for getting tagrec for firstip
	TagRec       m_tagrec;			//tagrec
	Msg0         m_msg0;			//for getting spiderdb records
	RdbList      m_rdbList;			//spiderdb records
};

}

static bool getTagRec(State *st);
static void gotTagRec(void *state);
static bool getSpiderRecs(State *st);
static void gotSpiderRecs(void *state);
static bool gotSpiderRecs2(State *st);
static bool sendResult(State *st);

//Normal flow (assuming no errors):
//	sendPageSpiderdbLookup
//	initiateProcessing
//	<m_msg8a.getTagRec>
//	gotTagRec
//	<msg0.getList>
//	gotSpiderRecs
//	gotSpiderRecs2
//	sendResult



// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the spider requests and replies for url given via cgi
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSpiderdbLookup(TcpSocket *s, HttpRequest *r) {
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
		log(LOG_ERROR, "PageSpiderdbLookup: new(%i): %s", (int)sizeof(State), mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s, 500, mstrerror(g_errno));
	}
	mnew(st, sizeof(*st), "pgspdrdblookup");
	st->m_socket = s;
	st->m_r.copy(r);
	
	int32_t  collLen = 0;
	const char *coll    = st->m_r.getString("c",&collLen);
	if(!coll || !coll[0]) {
		coll = g_conf.getDefaultColl( );
		collLen = strlen(coll);
	}
	st->m_coll    = coll;
	st->m_collLen = collLen;
	
	const CollectionRec *cr = g_collectiondb.getRec(st->m_coll,st->m_collLen);
	if(!cr) {
		g_errno = ENOCOLLREC;
		return sendResult(st);
	}
	st->m_collnum = cr->m_collnum;
	
	if(url) {
		memcpy(st->m_url_str, url, url_len);
		st->m_url_str[url_len] = '\0';
		st->m_url.set(st->m_url_str);
	} else
		st->m_url_str[0] = '\0';
	
	//if an URL has been specified the start working on that. Otherwise just show the initial page
	if(st->m_url_str[0])
		return getTagRec(st);
	else
		return sendResult(st);
}


static bool getTagRec(State *st) {
	if(!st->m_msg8a.getTagRec(&st->m_url, st->m_collnum, 0, st, gotTagRec, &st->m_tagrec))
		return false;
	if(g_errno)
		return sendResult(st);
	gotTagRec(st);
	return true;
}


static void gotTagRec(void *state) {
	State *st = reinterpret_cast<State*>(state);
	if(g_errno) {
		sendResult(st);
		return;
	}
	
	//find firstip in tag sand set state
	const Tag *tag = st->m_tagrec.getTag("firstip");
	if(tag) {
		st->m_firstip = atoip(tag->getTagData());
		log(LOG_DEBUG,"PageSpiderdbLookup: Found tag, firstip=0x%08x", st->m_firstip);
	} else {
		st->m_firstip = 0;
		log(LOG_INFO,"PageSpiderdbLookup: Didn't find firstip tag for %s", st->m_url_str);
		g_errno = ENOFIRSTIPFOUND;
		sendResult(st);
		return;
	}
	
	if(getSpiderRecs(st))
		gotSpiderRecs2(st);
}


static bool getSpiderRecs(State *st) {
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "getSpiderRecs(%p)",st);
	int64_t uh48 = hash64b(st->m_url_str);
	key128_t startKey = Spiderdb::makeFirstKey(st->m_firstip, uh48);
	key128_t endKey = Spiderdb::makeLastKey(st->m_firstip, uh48);
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "getSpiderRecs(%p): Calling Msg0::getList()", st);
	if(!st->m_msg0.getList(-1, //hostId
		               RDB_SPIDERDB_DEPRECATED, //TODO: use rdb_spiderdb_sqlite and new record format (also much simpler)
		               st->m_collnum,
		               &st->m_rdbList,
		               (const char*)&startKey,
		               (const char*)&endKey,
		               1000000,  //minRecSizes, -1 is not supported. We don't expect the two expected records to exceed 1MB
		               st,
		               gotSpiderRecs,
		               0, //niceness,
		               true, //doErrorCorrection
		               true, //includeTree,
		               -1, //firstHostId
		               0, //startFileNum,
		               -1, //numFiles,
		               10000, //timeout in msecs
		               false, //isRealMerge
		               false, //noSplit (?)
		               -1))//forceParitySplit
		return false;
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "getSpiderRecs: msg0.getlist didn't block");
	return true;
}



static void gotSpiderRecs(void *state) {
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "gotSpiderRecs(%p)", state);
	State *st = reinterpret_cast<State*>(state);
	gotSpiderRecs2(st);
}

static bool gotSpiderRecs2(State *st) {
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "gotSpiderRecs2(%p)", st);
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "gotSpiderRecs2: g_errno=%d", g_errno);
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "gotSpiderRecs2: st->m_rdbList.getListSize()=%d", st->m_rdbList.getListSize());
	return sendResult(st);
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

	mdelete (st, sizeof(State) , "pgspdrdblookup");
	delete st;

	return g_httpServer.sendDynamicPage(s, sb.getBufStart(), sb.length(), -1, false, contentType);
}


static void generatePageHtml(int32_t shardNum, int32_t firstIp, int32_t robotsShardNum, const char *url, const SpiderRequest *spiderRequest, const SpiderReply *spiderReply, SafeBuf *sb) {
	// print URL in box
	sb->safePrintf("<br>\n"
	              "Enter URL: "
	              "<input type=text name=url value=\"%s\" size=60>", url);
	sb->safePrintf("</form><br>\n");

	if (shardNum >= 0) {
		sb->safePrintf("<table class=\"main\" width=100%%>\n");
		sb->safePrintf("<tr class=\"level1\"><th colspan=50>Host information</th></tr>\n");
		sb->safePrintf("<tr><td>Shard:</td><td>%u</td></tr>\n", static_cast<uint32_t>(shardNum));
		int32_t numHosts;
		const Host *host = g_hostdb.getShard(shardNum, &numHosts);
		if(host) {
			sb->safePrintf("<tr><td>Host:</td><td>");
			while (numHosts--) {
				if (host->m_spiderEnabled) {
					sb->safePrintf(" %u", host->m_hostId);
				}
				host++;
			}
			sb->safePrintf("</td></tr>\n");
		}
	}

	if (robotsShardNum >= 0) {
		int32_t numHosts;
		const Host *host = g_hostdb.getShard(robotsShardNum, &numHosts);
		if(host) {
			sb->safePrintf("<tr><td>Robots.txt host:</td><td>");
			while (numHosts--) {
				if (host->m_spiderEnabled) {
					sb->safePrintf(" %u", host->m_hostId);
				}
				host++;
			}
			sb->safePrintf("</td></tr>\n");
		}
	}

	if (shardNum >= 0) {
		char ipbuf[16];
		iptoa(firstIp,ipbuf);
		sb->safePrintf("<tr><td>FirstIP:</td><td>%s</td></tr>\n", ipbuf);
		sb->safePrintf("</table>\n");
		sb->safePrintf("<br/>\n");
	}


	if (spiderRequest) {
		char ipbuf[16];
		char timebuf[32];
		sb->safePrintf("<table class=\"main\" width=100%%>\n");
		sb->safePrintf("  <tr class=\"level1\"><th colspan=50>Spider request</th></tr>\n");
		sb->safePrintf("  <tr class=\"level2\"><th>Field</th><th>Value</th></tr>\n");
		sb->safePrintf("  <tr><td>m_firstIp</td><td>%s</td></tr>\n", iptoa(spiderRequest->m_firstIp, ipbuf));
		sb->safePrintf("  <tr><td>m_addedTime</td><td>%s (%d)</td></tr>\n", formatTime(spiderRequest->m_addedTime, timebuf), spiderRequest->m_addedTime);
		sb->safePrintf("  <tr><td>m_prevErrCode</td><td>%d</td></tr>\n", spiderRequest->m_prevErrCode);
		sb->safePrintf("  <tr><td>m_priority</td><td>%d</td></tr>\n", spiderRequest->m_priority);
		sb->safePrintf("  <tr><td>m_errCount</td><td>%d</td></tr>\n", spiderRequest->m_errCount);
		sb->safePrintf("  <tr><td>m_isAddUrl</td><td>%s</td></tr>\n", spiderRequest->m_isAddUrl ? "true" : "false");
		sb->safePrintf("  <tr><td>m_isPageReindex</td><td>%s</td></tr>\n", spiderRequest->m_isPageReindex ? "true" : "false");
		sb->safePrintf("  <tr><td>m_isUrlCanonical</td><td>%s</td></tr>\n", spiderRequest->m_isUrlCanonical ? "true" : "false");
		sb->safePrintf("  <tr><td>m_isPageParser</td><td>%s</td></tr>\n", spiderRequest->m_isPageParser ? "true" : "false");
		sb->safePrintf("  <tr><td>m_urlIsDocId</td><td>%s</td></tr>\n", spiderRequest->m_urlIsDocId ? "true" : "false");
		sb->safePrintf("  <tr><td>m_forceDelete</td><td>%s</td></tr>\n", spiderRequest->m_forceDelete ? "true" : "false");
		sb->safePrintf("  <tr><td>m_fakeFirstIp</td><td>%s</td></tr>\n", spiderRequest->m_fakeFirstIp ? "true" : "false");
		sb->safePrintf("</table>\n");
	}

	if (spiderRequest && spiderReply) {
		sb->safePrintf("<br/>\n");
	}

	if(spiderReply) {
		char timebuf[32];
		sb->safePrintf("<table class=\"main\" width=100%%>\n");
		sb->safePrintf("  <tr class=\"level1\"><th colspan=50>Spider reply</th><tr>\n");
		sb->safePrintf("  <tr class=\"level2\"><th>Field</th><th>Value</th></tr>\n");
		sb->safePrintf("  <tr><td>m_spideredTime</td><td>%s (%d)</td></tr>\n", formatTime(spiderReply->m_spideredTime, timebuf), spiderReply->m_spideredTime);
		sb->safePrintf("  <tr><td>m_errCode</td><td>%d</td></tr>\n", spiderReply->m_errCode);
		sb->safePrintf("  <tr><td>m_percentChangedPerDay</td><td>%f</td></tr>\n", spiderReply->m_percentChangedPerDay);
		sb->safePrintf("  <tr><td>m_contentHash32</td><td>%u</td></tr>\n", spiderReply->m_contentHash32);
		sb->safePrintf("  <tr><td>m_crawlDelayMS</td><td>%d</td></tr>\n", spiderReply->m_crawlDelayMS);
		sb->safePrintf("  <tr><td>m_downloadEndTime</td><td>%s (%ld)</td></tr>\n", formatTimeMs(spiderReply->m_downloadEndTime, timebuf), spiderReply->m_downloadEndTime);
		sb->safePrintf("  <tr><td>m_httpStatus</td><td>%d</td></tr>\n", spiderReply->m_httpStatus);
		sb->safePrintf(" <tr><td>m_errCount</td><td>%d</td></tr>\n", spiderReply->m_errCount);
		sb->safePrintf(" <tr><td>m_langId</td><td>%d</td></tr>\n", spiderReply->m_langId);
		sb->safePrintf(" <tr><td>m_isIndexed</td><td>%s</td></tr>\n", spiderReply->m_isIndexed ? "true" : "false");
		sb->safePrintf("</table>\n");
	}

	if(!spiderRequest && !spiderReply) {
		sb->safePrintf("<strong>No request, no reply.</strong>\n");
	}
}

static void generatePageJSON(int32_t shardNum, int32_t robotsShardNum, const SpiderRequest *spiderRequest, const SpiderReply *spiderReply, SafeBuf *sb) {
	sb->safePrintf("{\n");
	if (shardNum >= 0) {
		sb->safePrintf("\"shard\": %u,\n", static_cast<uint32_t>(shardNum));

		int32_t numHosts;
		const Host *host = g_hostdb.getShard(shardNum, &numHosts);
		if(host) {
			sb->safePrintf("\"host\": [");

			bool isFirst = true;
			while (numHosts--) {
				if (host->m_spiderEnabled) {
					if (!isFirst) {
						sb->safePrintf(", ");
					}
					sb->safePrintf("%u", host->m_hostId);
					isFirst = false;
				}
				host++;
			}

			sb->safePrintf("]");
		}
	}

	if (robotsShardNum >= 0) {
		int32_t numHosts;
		const Host *host = g_hostdb.getShard(robotsShardNum, &numHosts);
		if(host) {
			sb->safePrintf(",\n\"robotsTxtHost\": [");

			bool isFirst = true;
			while (numHosts--) {
				if (host->m_spiderEnabled) {
					if (!isFirst) {
						sb->safePrintf(", ");
					}
					sb->safePrintf("%u", host->m_hostId);
					isFirst = false;
				}
				host++;
			}

			sb->safePrintf("]");
		}
	}

	if (spiderRequest) {
		sb->safePrintf(",\n\"spiderRequest\": {\n");

		char ipbuf[16];
		sb->safePrintf("\t\"firstIp\": \"%s\",\n", iptoa(spiderRequest->m_firstIp, ipbuf));
		sb->safePrintf("\t\"hostHash32\": %u,\n", spiderRequest->m_hostHash32);
		sb->safePrintf("\t\"domHash32\": %u,\n", spiderRequest->m_domHash32);
		sb->safePrintf("\t\"siteHash32\": %u,\n", spiderRequest->m_siteHash32);
		sb->safePrintf("\t\"siteNumInlinks\": %d,\n", spiderRequest->m_siteNumInlinks);
		sb->safePrintf("\t\"addedTime\": %u,\n", spiderRequest->m_addedTime);
		sb->safePrintf("\t\"pageNumInlinks\": %hhu,\n", spiderRequest->m_pageNumInlinks);
		sb->safePrintf("\t\"sameErrCount\": %hhu,\n", spiderRequest->m_sameErrCount);
		sb->safePrintf("\t\"version\": %hhu,\n", spiderRequest->m_version);
		sb->safePrintf("\t\"discoveryTime\": %u,\n", spiderRequest->m_discoveryTime);
		sb->safePrintf("\t\"prevErrCode\": %d,\n", spiderRequest->m_prevErrCode);
		sb->safePrintf("\t\"contentHash32\": %u,\n", spiderRequest->m_contentHash32);
		sb->safePrintf("\t\"recycleContent\": %s,\n", spiderRequest->m_recycleContent ? "true" : "false");
		sb->safePrintf("\t\"isAddUrl\": %s,\n", spiderRequest->m_isAddUrl ? "true" : "false");
		sb->safePrintf("\t\"isPageReindex\": %s,\n", spiderRequest->m_isPageReindex ? "true" : "false");
		sb->safePrintf("\t\"isUrlCanonical\": %s,\n", spiderRequest->m_isUrlCanonical ? "true" : "false");
		sb->safePrintf("\t\"isPageParser\": %s,\n", spiderRequest->m_isPageParser ? "true" : "false");
		sb->safePrintf("\t\"urlIsDocId\": %s,\n", spiderRequest->m_urlIsDocId ? "true" : "false");
		sb->safePrintf("\t\"isRSSExt\": %s,\n", spiderRequest->m_isRSSExt ? "true" : "false");
		sb->safePrintf("\t\"isUrlPermalinkFormat\": %s,\n", spiderRequest->m_isUrlPermalinkFormat ? "true" : "false");
		sb->safePrintf("\t\"forceDelete\": %s,\n", spiderRequest->m_forceDelete ? "true" : "false");
		sb->safePrintf("\t\"isInjecting\": %s,\n", spiderRequest->m_isInjecting ? "true" : "false");
		sb->safePrintf("\t\"hadReply\": %s,\n", spiderRequest->m_hadReply ? "true" : "false");
		sb->safePrintf("\t\"fakeFirstIp\": %s,\n", spiderRequest->m_fakeFirstIp ? "true" : "false");
		sb->safePrintf("\t\"hasAuthorityInlink\": %s,\n", spiderRequest->m_hasAuthorityInlink ? "true" : "false");
		sb->safePrintf("\t\"hasAuthorityInlinkValid\": %s,\n", spiderRequest->m_hasAuthorityInlinkValid ? "true" : "false");
		sb->safePrintf("\t\"siteNumInlinksValid\": %s,\n", spiderRequest->m_siteNumInlinksValid ? "true" : "false");
		sb->safePrintf("\t\"avoidSpiderLinks\": %s,\n", spiderRequest->m_avoidSpiderLinks ? "true" : "false");
		sb->safePrintf("\t\"ufn\": %hd,\n", spiderRequest->m_ufn);
		sb->safePrintf("\t\"priority\": %d,\n", spiderRequest->m_priority);
		sb->safePrintf("\t\"errCount\": %d\n", spiderRequest->m_errCount);
		sb->safePrintf("}");
	}

	if(spiderReply) {
		sb->safePrintf(",\n\"spiderReply\": {\n");

		char ipbuf[16];
		sb->safePrintf("\t\"firstIp\": \"%s\",\n", iptoa(spiderReply->m_firstIp, ipbuf));
		sb->safePrintf("\t\"siteHash32\": %u,\n", spiderReply->m_siteHash32);
		sb->safePrintf("\t\"domHash32\": %u,\n", spiderReply->m_domHash32);
		sb->safePrintf("\t\"percentChangedPerDay\": %f,\n", spiderReply->m_percentChangedPerDay);
		sb->safePrintf("\t\"spideredTime\": %d,\n", spiderReply->m_spideredTime);
		sb->safePrintf("\t\"errCode\": %d,\n", spiderReply->m_errCode);
		sb->safePrintf("\t\"siteNumInlinks\": %d,\n", spiderReply->m_siteNumInlinks);
		sb->safePrintf("\t\"sameErrCount\": %hhu,\n", spiderReply->m_sameErrCount);
		sb->safePrintf("\t\"version\": %hhu,\n", spiderReply->m_version);
		sb->safePrintf("\t\"contentHash32\": %u,\n", spiderReply->m_contentHash32);
		sb->safePrintf("\t\"crawlDelayMS\": %d,\n", spiderReply->m_crawlDelayMS);
		sb->safePrintf("\t\"downloadEndTime\": %ld,\n", spiderReply->m_downloadEndTime);
		sb->safePrintf("\t\"httpStatus\": %d,\n", spiderReply->m_httpStatus);
		sb->safePrintf("\t\"errCount\": %d,\n", spiderReply->m_errCount);
		sb->safePrintf("\t\"langId\": %d,\n", spiderReply->m_langId);
		sb->safePrintf("\t\"isRSS\": %s,\n", spiderReply->m_isRSS ? "true" : "false");
		sb->safePrintf("\t\"isPermalink\": %s,\n", spiderReply->m_isPermalink ? "true" : "false");
		sb->safePrintf("\t\"isIndexed\": %s,\n", spiderReply->m_isIndexed ? "true" : "false");
		sb->safePrintf("\t\"hasAuthorityInlink\": %s,\n", spiderReply->m_hasAuthorityInlink ? "true" : "false");
		sb->safePrintf("\t\"isIndexedInvalid\": %s,\n", spiderReply->m_isIndexedINValid ? "true" : "false");
		sb->safePrintf("\t\"hasAuthorityInlinkValid\": %s,\n", spiderReply->m_hasAuthorityInlinkValid ? "true" : "false");
		sb->safePrintf("\t\"siteNumInlinksValid\": %s,\n", spiderReply->m_siteNumInlinksValid ? "true" : "false");
		sb->safePrintf("\t\"fromInjectionRequest\": %s\n", spiderReply->m_fromInjectionRequest ? "true" : "false");
		sb->safePrintf("}\n");
	}

	sb->safePrintf("}");
}

static bool sendResult(State *st) {
	logTrace(g_conf.m_logTracePageSpiderdbLookup, "st(%p): sendResult: g_errno=%d", st, g_errno);
	// get the socket
	TcpSocket *s = st->m_socket;

	SafeBuf sb;
	// print standard header
	sb.reserve2x ( 32768 );

	if(g_errno) {
		return respondWithError(st, g_errno, mstrerror(g_errno));
	}

	int32_t shardNum = -1;
	int32_t robotsShardNum = -1;
	if(st->m_url_str[0]) {
		int64_t uh48 = hash64b(st->m_url_str);
		key128_t startKey = Spiderdb::makeFirstKey(st->m_firstip, uh48);
		shardNum = g_hostdb.getShardNum(RDB_SPIDERDB_SQLITE, &startKey);

		//
		// locate host that caches robots.txt
		//
		Url u;
		u.set(st->m_url_str);

		// build robots.txt url
		char urlRobots[MAX_URL_LEN+1];
		char *p = urlRobots;
		if ( ! u.getScheme() )
		{
			p += sprintf ( p , "http://" );
		}
		else
		{
			gbmemcpy ( p , u.getScheme() , u.getSchemeLen() );
			p += u.getSchemeLen();
			p += sprintf(p,"://");
		}

		gbmemcpy ( p , u.getHost() , u.getHostLen() );
		p += u.getHostLen();

		// add port if not default
		if ( u.getPort() != u.getDefaultPort() ) {
			p += sprintf( p, ":%" PRId32, u.getPort() );
		}
		p += sprintf ( p , "/robots.txt" );

		// find host based on firstip and robots.txt url
		int32_t nh     = g_hostdb.getNumHosts();
		robotsShardNum = hash32h(((uint32_t)st->m_firstip >> 8), 0) % nh;
		if((uint32_t)st->m_firstip >> 8 == 0) {
			// If the first IP is not set for the request then we don't
			// want to hammer the first host with spidering enabled.
			robotsShardNum = hash32n ( urlRobots ) % nh;
		}
		robotsShardNum = robotsShardNum % g_hostdb.getNumShards();
	}

	//locate spider request and reply
	const SpiderRequest *spiderRequest = NULL;
	const SpiderReply *spiderReply = NULL;
	if(st->m_rdbList.getListSize()>0) {
		logTrace(g_conf.m_logTracePageSpiderdbLookup, "st(%p): sendResult: st->m_rdbList.getListSize()=%d", st, st->m_rdbList.getListSize());
		for(st->m_rdbList.resetListPtr(); !st->m_rdbList.isExhausted(); st->m_rdbList.skipCurrentRecord()) {
			const char *currentRec = st->m_rdbList.getCurrentRec();
			logHexTrace(g_conf.m_logTracePageSpiderdbLookup, currentRec, st->m_rdbList.getCurrentRecSize(), "st(%p): ", st);
			if (KEYNEG(currentRec)) {
				continue; //skip negative records (which should even be there)
			}

			if (Spiderdb::isSpiderRequest((const key128_t *)currentRec)) {
				logTrace(g_conf.m_logTracePageSpiderdbLookup, "it's a request");
				spiderRequest = reinterpret_cast<const SpiderRequest*>(currentRec);
			} else  {
				logTrace(g_conf.m_logTracePageSpiderdbLookup, "it's a reply");
				spiderReply = reinterpret_cast<const SpiderReply*>(currentRec);
			}
		}
	}

	const char *contentType = NULL;
	switch(st->m_r.getReplyFormat()) {
		case FORMAT_HTML:
			g_pages.printAdminTop(&sb, s, &st->m_r, NULL);
			generatePageHtml(shardNum, st->m_firstip, robotsShardNum, st->m_url_str, spiderRequest, spiderReply, &sb);
			g_pages.printAdminBottom2(&sb);
			contentType = "text/html";
			break;
		case FORMAT_JSON:
			generatePageJSON(shardNum, robotsShardNum, spiderRequest, spiderReply, &sb);
			contentType = "application/json";
			break;
		default:
			contentType = "text/html";
			sb.safePrintf("oops!");
			break;

	}

	// don't forget to cleanup
	mdelete(st, sizeof(State) , "pgspdrdblookup");
	delete st;

	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), sb.length(), -1, false, contentType);
}
