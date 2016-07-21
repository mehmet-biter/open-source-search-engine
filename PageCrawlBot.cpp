// diffbot api implementaion

//
// WHAT APIs are here?
//
// . 1. the CrawlBot API to start a crawl 
// . 2. To directly process a provided URL (injection)
// . 3. the Cache API so phantomjs can quickly check the cache for files
//      and quickly add files to the cache.
//

// Related pages:
//
// * http://diffbot.com/dev/docs/  (Crawlbot API tab, and others)
// * http://diffbot.com/dev/crawl/

#include "Errno.h"
#include "PageCrawlBot.h"
#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h" // g_msg
#include "PageInject.h" // Msg7
#include "Repair.h"
#include "Parms.h"
#include "SpiderLoop.h"
#include "Process.h"

//////////////////////////////////////////
//
// MAIN API STUFF I GUESS
//
//////////////////////////////////////////

bool printCrawlDetails2 (SafeBuf *sb , CollectionRec *cx , char format ) {

	SafeBuf tmp;
	int32_t crawlStatus = -1;
	getSpiderStatusMsg ( cx , &tmp , &crawlStatus );
	CrawlInfo *ci = &cx->m_localCrawlInfo;
	int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
	if ( sentAlert ) sentAlert = 1;

	// don't print completed time if spidering is going on
	uint32_t completed = cx->m_diffbotCrawlEndTime; // time_t
	// if not yet done, make this zero
	if ( crawlStatus == SP_INITIALIZING ) completed = 0;
	if ( crawlStatus == SP_NOURLS ) completed = 0;
	//if ( crawlStatus == SP_PAUSED ) completed = 0;
	//if ( crawlStatus == SP_ADMIN_PAUSED ) completed = 0;
	if ( crawlStatus == SP_INPROGRESS ) completed = 0;

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("{"
			       "\"response\":{\n"
			       "\t\"statusCode\":%" PRId32",\n"
			       "\t\"statusMsg\":\"%s\",\n"
			       "\t\"jobCreationTimeUTC\":%" PRId32",\n"
			       "\t\"jobCompletionTimeUTC\":%" PRId32",\n"
			       "\t\"sentJobDoneNotification\":%" PRId32",\n"
			       "\t\"urlsHarvested\":%" PRId64",\n"
			       "\t\"pageCrawlAttempts\":%" PRId64",\n"
			       "\t\"pageCrawlSuccesses\":%" PRId64",\n"
			       , crawlStatus
			       , tmp.getBufStart()
			       , cx->m_diffbotCrawlStartTime
			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t\"currentTime\":%" PRIu32",\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t\"currentTimeUTC\":%" PRIu32"\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t}\n");
		sb->safePrintf("}\n");
	}

	if ( format == FORMAT_XML ) {
		sb->safePrintf("<response>\n"
			       "\t<statusCode>%" PRId32"</statusCode>\n"
			       , crawlStatus
			       );
		sb->safePrintf(
			       "\t<statusMsg><![CDATA[%s]]></statusMsg>\n"
			       "\t<jobCreationTimeUTC>%" PRId32
			       "</jobCreationTimeUTC>\n"
			       , (char *)tmp.getBufStart()
			       , (int32_t)cx->m_diffbotCrawlStartTime
			       );
		sb->safePrintf(
			       "\t<jobCompletionTimeUTC>%" PRId32
			       "</jobCompletionTimeUTC>\n"

			       "\t<sentJobDoneNotification>%" PRId32
			       "</sentJobDoneNotification>\n"

			       "\t<urlsHarvested>%" PRId64"</urlsHarvested>\n"

			       "\t<pageCrawlAttempts>%" PRId64
			       "</pageCrawlAttempts>\n"

			       "\t<pageCrawlSuccesses>%" PRId64
			       "</pageCrawlSuccesses>\n"

			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t<currentTime>%" PRIu32"</currentTime>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t<currentTimeUTC>%" PRIu32"</currentTimeUTC>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("</response>\n");
	}

	return true;
}

// . do not add dups into m_diffbotSeeds safebuf
// . return 0 if not in table, 1 if in table. -1 on error adding to table.
int32_t isInSeedBuf ( CollectionRec *cr , const char *url, int len ) {

	HashTableX *ht = &cr->m_seedHashTable;

	// if table is empty, populate it
	if ( ht->m_numSlotsUsed <= 0 ) {
		// initialize the hash table
		if ( ! ht->set(8,0,1024,NULL,0,false,1,"seedtbl") ) 
			return -1;
		// populate it from list of seed urls
		char *p = cr->m_diffbotSeeds.getBufStart();
		for ( ; p && *p ; ) {
			// get url
			char *purl = p;
			// advance to next
			for ( ; *p && !is_wspace_a(*p) ; p++ );
			// make end then
			char *end = p;
			// skip possible white space. might be \0.
			if ( *p ) p++;
			// hash it
			int64_t h64 = hash64 ( purl , end-purl );
			if ( ! ht->addKey ( &h64 ) ) return -1;
		}
	}

	// is this url in the hash table?
	int64_t u64 = hash64 ( url, len );
	
	if ( ht->isInTable ( &u64 ) ) return 1;

	// add it to hashtable
	if ( ! ht->addKey ( &u64 ) ) return -1;

	// WAS not in table
	return 0;
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

	uint32_t now = (uint32_t)getTimeGlobal();

	// a big loop
	while ( true ) {
		// skip white space (\0 is not a whitespace)
		for ( ; is_wspace_a(*p) ; p++ );

		// all done?
		if ( ! *p ) break;

		// save it
		const char *saved = p;

		// advance to next white space
		for ( ; ! is_wspace_a(*p) && *p ; p++ );

		// set end
		const char *end = p;

		// get that url
		Url url;
		url.set( saved, end - saved );

		// if not legit skip
		if ( url.getUrlLen() <= 0 ) continue;

		// need this
		int64_t probDocId = g_titledb.getProbableDocId(&url);

		// make it
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_firstIp = url.getHostHash32(); // fakeip!

		// avoid ips of 0 or -1
		if ( sreq.m_firstIp == 0 || sreq.m_firstIp == -1 ) {
			sreq.m_firstIp = 1;
		}

		sreq.m_hostHash32 = url.getHostHash32();
		sreq.m_domHash32  = url.getDomainHash32();
		sreq.m_siteHash32 = url.getHostHash32();
		//sreq.m_probDocId  = probDocId;
		sreq.m_hopCount   = 0; // we're a seed
		sreq.m_hopCountValid = true;
		sreq.m_addedTime = now;
		sreq.m_isWWWSubdomain = url.isSimpleSubdomain();
		
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
		int32_t need = listBuf->getLength() + 100 + sreq.getRecSize();
		int32_t newBufSize = 0;

		if ( need > oldBufSize ) {
			newBufSize = oldBufSize + 100000;
		}

		if ( newBufSize && ! listBuf->reserve ( newBufSize ) ) {
			// return false with g_errno set
			return false;
		}

		// store rdbid first
		if ( ! listBuf->pushChar(RDB_SPIDERDB) ) {
			// return false with g_errno set
			return false;
		}

		// store it
		if ( ! listBuf->safeMemcpy ( &sreq , sreq.getRecSize() ) ) {
			// return false with g_errno set
			return false;
		}

		if ( ! cr ) {
			continue;
		}

		// do not add dups into m_diffbotSeeds safebuf
		int32_t status = isInSeedBuf ( cr , saved , end - saved );

		// error?
		if ( status == -1 ) {
			log ( LOG_WARN, "crawlbot: error adding seed to table: %s", mstrerror(g_errno) );
			return true;
		}

		// already in buf
		if ( status == 1 ) {
			continue;
		}

		// add url into m_diffbotSeeds, \n separated list
		if ( cr->m_diffbotSeeds.length() ) {
			// make it space not \n so it looks better in the
			// json output i guess
			cr->m_diffbotSeeds.pushChar( ' ' ); // \n
		}

		cr->m_diffbotSeeds.safeMemcpy (url.getUrl(), url.getUrlLen());
		cr->m_diffbotSeeds.nullTerm();
	}

	// all done
	return true;
}

