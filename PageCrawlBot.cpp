// diffbot api implementaion

//
// WHAT APIs are here?
//
// . 1. the CrawlBot API to start a crawl 
// . 2. To directly process a provided URL (injection)
// . 3. the Cache API so phantomjs can quickly check the cache for files
//      and quickly add files to the cache.

#include "Errno.h"
#include "PageCrawlBot.h"
#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h" // g_msg
#include "PageInject.h" // Msg7
#include "Collectiondb.h"
#include "Repair.h"
#include "Parms.h"
#include "SpiderLoop.h"
#include "Process.h"
#include "Docid.h"

//////////////////////////////////////////
//
// MAIN API STUFF I GUESS
//
//////////////////////////////////////////

bool printCrawlDetails2 (SafeBuf *sb , CollectionRec *cx , char format ) {
	const char *crawlMsg;
	spider_status_t crawlStatus;
	getSpiderStatusMsg ( cx , &crawlMsg, &crawlStatus );

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("{"
			       "\"response\":{\n"
			       "\t\"statusCode\":%" PRId32",\n"
			       "\t\"statusMsg\":\"%s\",\n"
			       , (int)crawlStatus, crawlMsg);
		sb->safePrintf("\t\"processStartTime\":%" PRId64",\n", (g_process.m_processStartTime / 1000));
		sb->safePrintf("\t\"currentTime\":%" PRIu32"\n", (uint32_t)getTime() );
		sb->safePrintf("\t}\n");
		sb->safePrintf("}\n");
	}

	if ( format == FORMAT_XML ) {
		sb->safePrintf("<response>\n\t<statusCode>%" PRId32"</statusCode>\n", (int)crawlStatus);
		sb->safePrintf("\t<statusMsg><![CDATA[%s]]></statusMsg>\n", crawlMsg);
		sb->safePrintf("\t<currentTime>%" PRIu32"</currentTime>\n", (uint32_t)getTime() );
		sb->safePrintf("\t<currentTimeUTC>%" PRIu32"</currentTimeUTC>\n", (uint32_t)getTime() );
		sb->safePrintf("</response>\n");
	}

	return true;
}

// just use "fakeips" based on the hash of each url hostname/subdomain
// so we don't waste time doing ip lookups.
bool getSpiderRequestMetaList ( const char *doc, SafeBuf *listBuf, bool spiderLinks, CollectionRec *cr ) {
	if ( ! doc ) {
		return true;
	}

	// . scan the list of urls
	// . assume separated by white space \n \t or space
	const char *p = doc;

	uint32_t now = (uint32_t)getTime();

	for(;;) {
		// skip white space (\0 is not a whitespace)
		while(is_wspace_a(*p))
			p++;

		// all done?
		if ( ! *p ) break;

		// save it
		const char *saved = p;

		// advance to next white space
		while(!is_wspace_a(*p) && *p)
			p++;

		// set end
		const char *end = p;

		// get that url
		Url url;
		url.set( saved, end - saved );

		// if not legit skip
		if ( url.getUrlLen() <= 0 ) continue;

		// need this
		int64_t probDocId = Docid::getProbableDocId(&url);

		// make it
		SpiderRequest sreq;
		sreq.m_firstIp = url.getHostHash32(); // fakeip!

		// avoid ips of 0 or -1
		if ( sreq.m_firstIp == 0 || sreq.m_firstIp == -1 ) {
			sreq.m_firstIp = 1;
		}

		sreq.m_hostHash32 = url.getHostHash32();
		sreq.m_domHash32  = url.getDomainHash32();
		sreq.m_siteHash32 = url.getHostHash32();
		sreq.m_addedTime = now;
		
		sreq.m_fakeFirstIp = 1;
		sreq.m_isAddUrl = 1;

		// spider links?
		if ( ! spiderLinks ) {
			sreq.m_avoidSpiderLinks = 1;
		}

		// save the url!
		strcpy ( sreq.m_url , url.getUrl() );
		// finally, we can set the key. isDel = false
		sreq.setKey ( sreq.m_firstIp , probDocId , false );

		int32_t oldBufSize = listBuf->getCapacity();
		int32_t need = listBuf->length() + 100 + sreq.getRecSize();
		int32_t newBufSize = 0;

		if ( need > oldBufSize ) {
			newBufSize = oldBufSize + 100000;
		}

		if ( newBufSize && ! listBuf->reserve ( newBufSize ) ) {
			// return false with g_errno set
			return false;
		}

		// store rdbid first
		if ( ! listBuf->pushChar(RDB_SPIDERDB_DEPRECATED) ) {
			// return false with g_errno set
			return false;
		}

		// store it
		if ( ! listBuf->safeMemcpy ( &sreq , sreq.getRecSize() ) ) {
			// return false with g_errno set
			return false;
		}
	}

	// all done
	return true;
}

