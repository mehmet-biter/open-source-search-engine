// . TODO: do not cache if less than the 20k thing again.

// . TODO: nuke doledb every couple hours.
//   CollectionRec::m_doledbRefreshRateInSecs. but how would this work
//   for crawlbot jobs where we got 10,000 collections? i'd turn this off.
//   we could selectively update certain firstips in doledb that have
//   been in doledb for a long time.
//   i'd like to see how many collections are actually active
//   for diffbot first though.



// TODO: add m_downloadTimeTable to measure download speed of an IP
// TODO: consider a "latestpubdateage" in url filters for pages that are
//       adding new dates (not clocks) all the time

#include "gb-include.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "Test.h" // g_test
#include "Threads.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"

void testWinnerTreeKey ( ) ;

// . this was 10 but cpu is getting pegged, so i set to 45
// . we consider the collection done spidering when no urls to spider
//   for this many seconds
// . i'd like to set back to 10 for speed... maybe even 5 or less
// . back to 30 from 20 to try to fix crawls thinking they are done
//   maybe because of the empty doledb logic taking too long?
//#define SPIDER_DONE_TIMER 30
// try 45 to prevent false revivals
//#define SPIDER_DONE_TIMER 45
// try 30 again since we have new localcrawlinfo update logic much faster
//#define SPIDER_DONE_TIMER 30
// neo under heavy load go to 60
//#define SPIDER_DONE_TIMER 60
// super overloaded
//#define SPIDER_DONE_TIMER 90
#define SPIDER_DONE_TIMER 20


Doledb g_doledb;

RdbTree *g_tree = NULL;

SpiderRequest *g_sreq = NULL;

int32_t g_corruptCount = 0;

char s_countsAreValid = 1;

/////////////////////////
/////////////////////////      SPIDEREC
/////////////////////////

void SpiderRequest::setKey (int32_t firstIp,
			    int64_t parentDocId,
			    int64_t uh48,
			    bool isDel) {

	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { char *xx=NULL;*xx=0; }

	m_key = g_spiderdb.makeKey ( firstIp,uh48,true,parentDocId , isDel );
	// set dataSize too!
	setDataSize();
}

void SpiderRequest::setDataSize ( ) {
	m_dataSize = (m_url - (char *)this) + gbstrlen(m_url) + 1 
		// subtract m_key and m_dataSize
		- sizeof(key128_t) - 4 ;
}

int32_t SpiderRequest::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	//sb->safePrintf("k.n1=0x%"XINT64" ",m_key.n1);
	//sb->safePrintf("k.n0=0x%"XINT64" ",m_key.n0);
	sb->safePrintf("k=%s ",KEYSTR(this,
				      getKeySizeFromRdbId(RDB_SPIDERDB)));

	// indicate it's a request not a reply
	sb->safePrintf("REQ ");
	sb->safePrintf("uh48=%"UINT64" ",getUrlHash48());
	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("recsize=%"INT32" ",getRecSize());
	sb->safePrintf("parentDocId=%"UINT64" ",getParentDocId());

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("hostHash32=0x%"XINT32" ",m_hostHash32 );
	sb->safePrintf("domHash32=0x%"XINT32" ",m_domHash32 );
	sb->safePrintf("siteHash32=0x%"XINT32" ",m_siteHash32 );
	sb->safePrintf("siteNumInlinks=%"INT32" ",m_siteNumInlinks );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf("addedTime=%s(%"UINT32") ",time,(uint32_t)m_addedTime );

	//sb->safePrintf("parentFirstIp=%s ",iptoa(m_parentFirstIp) );
	sb->safePrintf("pageNumInlinks=%i ",(int)m_pageNumInlinks);
	sb->safePrintf("parentHostHash32=0x%"XINT32" ",m_parentHostHash32 );
	sb->safePrintf("parentDomHash32=0x%"XINT32" ",m_parentDomHash32 );
	sb->safePrintf("parentSiteHash32=0x%"XINT32" ",m_parentSiteHash32 );

	sb->safePrintf("hopCount=%"INT32" ",(int32_t)m_hopCount );

	//timeStruct = gmtime ( &m_spiderTime );
	//time[0] = 0;
	//if ( m_spiderTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("spiderTime=%s(%"UINT32") ",time,m_spiderTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("pubDate=%s(%"UINT32") ",time,m_pubDate );

	sb->safePrintf("ufn=%"INT32" ", (int32_t)m_ufn);
	// why was this unsigned?
	sb->safePrintf("priority=%"INT32" ", (int32_t)m_priority);

	//sb->safePrintf("errCode=%s(%"UINT32") ",mstrerror(m_errCode),m_errCode );
	//sb->safePrintf("crawlDelay=%"INT32"ms ",m_crawlDelay );
	//sb->safePrintf("httpStatus=%"INT32" ",(int32_t)m_httpStatus );
	//sb->safePrintf("retryNum=%"INT32" ",(int32_t)m_retryNum );
	//sb->safePrintf("langId=%s(%"INT32") ",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf("percentChanged=%"INT32"%% ",(int32_t)m_percentChanged );

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_fakeFirstIp ) sb->safePrintf("ISFAKEFIRSTIP ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");

	if ( m_isWWWSubdomain  ) sb->safePrintf("WWWSUBDOMAIN ");
	if ( m_avoidSpiderLinks ) sb->safePrintf("AVOIDSPIDERLINKS ");

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");

	//uint32_t gid = g_spiderdb.getGroupId(m_firstIp);
	int32_t shardNum = g_hostdb.getShardNum(RDB_SPIDERDB,this);
	sb->safePrintf("shardnum=%"UINT32" ",(uint32_t)shardNum);

	sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}

void SpiderReply::setKey (int32_t firstIp,
			  int64_t parentDocId,
			  int64_t uh48,
			  bool isDel) {
	m_key = g_spiderdb.makeKey ( firstIp,uh48,false,parentDocId , isDel );
	// set dataSize too!
	m_dataSize = sizeof(SpiderReply) - sizeof(key128_t) - 4;
}

int32_t SpiderReply::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	//sb->safePrintf("k.n1=0x%llx ",m_key.n1);
	//sb->safePrintf("k.n0=0x%llx ",m_key.n0);
	sb->safePrintf("k=%s ",KEYSTR(this,sizeof(SPIDERDBKEY)));

	// indicate it's a reply
	sb->safePrintf("REP ");

	sb->safePrintf("uh48=%"UINT64" ",getUrlHash48());
	sb->safePrintf("parentDocId=%"UINT64" ",getParentDocId());


	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("percentChangedPerDay=%.02f%% ",m_percentChangedPerDay);

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];
	time_t ts = (time_t)m_spideredTime;
	timeStruct = gmtime ( &ts );
	time[0] = 0;
	if ( m_spideredTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("spideredTime=%s(%"UINT32") ",time,
		       (uint32_t)m_spideredTime);

	sb->safePrintf("siteNumInlinks=%"INT32" ",m_siteNumInlinks );

	time_t ts2 = (time_t)m_pubDate;
	timeStruct = gmtime ( &ts2 );
	time[0] = 0;
	if ( m_pubDate != 0 && m_pubDate != -1 ) 
		strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("pubDate=%s(%"INT32") ",time,m_pubDate );

	//sb->safePrintf("newRequests=%"INT32" ",m_newRequests );
	sb->safePrintf("ch32=%"UINT32" ",(uint32_t)m_contentHash32);

	sb->safePrintf("crawldelayms=%"INT32"ms ",m_crawlDelayMS );
	sb->safePrintf("httpStatus=%"INT32" ",(int32_t)m_httpStatus );
	sb->safePrintf("langId=%s(%"INT32") ",
		       getLanguageString(m_langId),(int32_t)m_langId );

	if ( m_errCount )
		sb->safePrintf("errCount=%"INT32" ",(int32_t)m_errCount);

	sb->safePrintf("errCode=%s(%"UINT32") ",mstrerror(m_errCode),
		       (uint32_t)m_errCode );

	//if ( m_isSpam ) sb->safePrintf("ISSPAM ");
	if ( m_isRSS ) sb->safePrintf("ISRSS ");
	if ( m_isPermalink ) sb->safePrintf("ISPERMALINK ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	//if ( m_deleted ) sb->safePrintf("DELETED ");
	if ( ! m_isIndexedINValid && m_isIndexed ) sb->safePrintf("ISINDEXED ");


	//sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}



int32_t SpiderRequest::printToTable ( SafeBuf *sb , char *status ,
				   XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%"INT32"</td>\n",row);
		sb->safePrintf(" <td>%"INT64"ms</td>\n",elapsed);
		collnum_t collnum = xd->m_collnum;
		CollectionRec *cr = g_collectiondb.getRec(collnum);
		char *cs = ""; if ( cr ) cs = cr->m_coll;
		// sb->safePrintf(" <td><a href=/crawlbot?c=%s>%"INT32"</a></td>\n",
		// 	       cs,(int32_t)collnum);
		//sb->safePrintf(" <td><a href=/crawlbot?c=%s>%s</a></td>\n",
		//	       cs,cs);
		sb->safePrintf(" <td><a href=/search?c=%s&q=url%%3A%s>%s</a>"
			       "</td>\n",cs,m_url,cs);
	}

	sb->safePrintf(" <td><a href=%s><nobr>",m_url);
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></a></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_priority);
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_ufn);

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp) );
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%"UINT64"</td>\n",getUrlHash48());

	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_hostHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_domHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_siteHash32 );

	sb->safePrintf(" <td>%"INT32"</td>\n",m_siteNumInlinks );
	//sb->safePrintf(" <td>%"INT32"</td>\n",m_pageNumInlinks );
	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts3 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts3 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%"UINT32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf(" <td>%s(%"UINT32")</td>\n",time,m_pubDate );

	//sb->safePrintf(" <td>%s(%"UINT32")</td>\n",mstrerror(m_errCode),m_errCode);
	//sb->safePrintf(" <td>%"INT32"ms</td>\n",m_crawlDelay );
	sb->safePrintf(" <td>%i</td>\n",(int)m_pageNumInlinks);
	sb->safePrintf(" <td>%"UINT64"</td>\n",getParentDocId() );

	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentHostHash32);
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentDomHash32 );
	//sb->safePrintf(" <td>0x%"XINT32"</td>\n",m_parentSiteHash32 );

	//sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_httpStatus );
	//sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_retryNum );
	//sb->safePrintf(" <td>%s(%"INT32")</td>\n",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf(" <td>%"INT32"%%</td>\n",(int32_t)m_percentChanged );

	sb->safePrintf(" <td><nobr>");

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");


	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");



	sb->safePrintf("</nobr></td>\n");

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeaderSimple ( SafeBuf *sb , 
					     bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");
	sb->safePrintf(" <td><b>first IP</b></td>\n");
	sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}

int32_t SpiderRequest::printToTableSimple ( SafeBuf *sb , char *status ,
					 XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%"INT32"</td>\n",row);
		sb->safePrintf(" <td>%"INT64"ms</td>\n",elapsed);
		// print collection
		CollectionRec *cr = g_collectiondb.getRec ( xd->m_collnum );
		char *coll = "";
		if ( cr ) coll = cr->m_coll;
		sb->safePrintf("<td>%s</td>",coll);
	}

	sb->safePrintf(" <td><nobr>");
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp));

	if ( xd->m_crawlDelayValid && xd->m_crawlDelay >= 0 )
		sb->safePrintf(" <td>%"INT32" ms</td>\n",xd->m_crawlDelay);
	else
		sb->safePrintf(" <td>--</td>\n");

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_priority);

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%"INT32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts4 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts4 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%"UINT32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeader ( SafeBuf *sb , bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");

	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>ufn</b></td>\n");

	sb->safePrintf(" <td><b>firstIp</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>urlHash48</b></td>\n");
	//sb->safePrintf(" <td><b>hostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>domHash32</b></td>\n");
	//sb->safePrintf(" <td><b>siteHash32</b></td>\n");
	sb->safePrintf(" <td><b>siteInlinks</b></td>\n");
	//sb->safePrintf(" <td><b>pageNumInlinks</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>lastAttempt</b></td>\n");
	//sb->safePrintf(" <td><b>pubDate</b></td>\n");
	//sb->safePrintf(" <td><b>errCode</b></td>\n");
	//sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>parentIp</b></td>\n");
	sb->safePrintf(" <td><b>parentDocId</b></td>\n");
	//sb->safePrintf(" <td><b>parentHostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentDomHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentSiteHash32</b></td>\n");
	//sb->safePrintf(" <td><b>httpStatus</b></td>\n");
	//sb->safePrintf(" <td><b>retryNum</b></td>\n");
	//sb->safePrintf(" <td><b>langId</b></td>\n");
	//sb->safePrintf(" <td><b>percentChanged</b></td>\n");
	sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}


/////////////////////////
/////////////////////////      SPIDERDB
/////////////////////////


// a global class extern'd in .h file
Spiderdb g_spiderdb;
Spiderdb g_spiderdb2;

// reset rdb
void Spiderdb::reset() { m_rdb.reset(); }

// print the spider rec
int32_t Spiderdb::print( char *srec , SafeBuf *sb ) {
	// get if request or reply and print it
	if ( isSpiderRequest ( (key128_t *)srec ) )
		((SpiderRequest *)srec)->print(sb);
	else
		((SpiderReply *)srec)->print(sb);
	return 0;
}


bool Spiderdb::init ( ) {

	int32_t maxMem = 200000000;
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = maxMem  / 78;
	// . really we just cache the first 64k of each priority list
	// . used only by SpiderLoop
	//int32_t maxCacheNodes = 32;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, 100MB on gk0 now
	//int32_t pcmem = 20000000;//g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	//if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// turn off to prevent blocking up cpu
	//pcmem = 0;
	// key parser checks
	//int32_t      ip         = 0x1234;
	char      priority   = 12;
	int32_t      spiderTime = 0x3fe96610;
	int64_t urlHash48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;
	//int64_t pdocid     = 0x567834222LL;
	//key192_t k = makeOrderKey ( ip,priority,spiderTime,urlHash48,pdocid);
	//if (getOrderKeyUrlHash48  (&k)!=urlHash48 ){char*xx=NULL;*xx=0;}
	//if (getOrderKeySpiderTime (&k)!=spiderTime){char*xx=NULL;*xx=0;}
	//if (getOrderKeyPriority   (&k)!=priority  ){char*xx=NULL;*xx=0;}
	//if (getOrderKeyIp         (&k)!=ip        ){char*xx=NULL;*xx=0;}
	//if (getOrderKeyParentDocId(&k)!=pdocid    ){char*xx=NULL;*xx=0;}

	// doledb key test
	key_t dk = g_doledb.makeKey(priority,spiderTime,urlHash48,false);
	if(g_doledb.getPriority(&dk)!=priority){char*xx=NULL;*xx=0;}
	if(g_doledb.getSpiderTime(&dk)!=spiderTime){char*xx=NULL;*xx=0;}
	if(g_doledb.getUrlHash48(&dk)!=urlHash48){char*xx=NULL;*xx=0;}
	if(g_doledb.getIsDel(&dk)!= 0){char*xx=NULL;*xx=0;}

	// spiderdb key test
	int64_t docId = 123456789;
	int32_t firstIp = 0x23991688;
	key128_t sk = g_spiderdb.makeKey ( firstIp,urlHash48,1,docId,false);
	if ( ! g_spiderdb.isSpiderRequest (&sk) ) { char *xx=NULL;*xx=0; }
	if ( g_spiderdb.getUrlHash48(&sk) != urlHash48){char *xx=NULL;*xx=0;}
	if ( g_spiderdb.getFirstIp(&sk) != firstIp) {char *xx=NULL;*xx=0;}

	testWinnerTreeKey();

	// we now use a page cache
	// if ( ! m_pc.init ( "spiderdb", 
	// 		   RDB_SPIDERDB ,
	// 		   pcmem     ,
	// 		   pageSize  ))
	// 	return log(LOG_INIT,"spiderdb: Init failed.");

	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdb"   ,
			    true    , // dedup
			    -1      , // fixedDataSize
			    // now that we have MAX_WINNER_NODES allowed in doledb
			    // we don't have to keep spiderdb so tightly merged i guess..
			    // MDW: it seems to slow performance when not tightly merged
			    // so put this back to "2"...
			    2,//g_conf.m_spiderdbMinFilesToMerge , mintomerge
			    maxMem,//g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0,//g_conf.m_spiderdbMaxCacheMem,
			    0,//maxCacheNodes               ,
			    false                       , // half keys?
			    false                       , // save cache?
			    NULL,//&m_pc                       ,
			    false                       ,
			    false                       ,
			    sizeof(key128_t)            );
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Spiderdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = treeMem  / 78;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdbRebuild"   ,
			    true          , // dedup
			    -1            , // fixedDataSize
			    200           , // g_conf.m_spiderdbMinFilesToMerge
			    treeMem       , // g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // m_spiderdbMaxCacheMem,
			    0             , // maxCacheNodes               ,
			    false         , // half keys?
			    false         , // save cache?
			    NULL          , // &m_pc 
			    false         , // isTitledb?
			    false         , // preload diskpagecache
			    sizeof(key128_t));
}

/*
bool Spiderdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

bool Spiderdb::verify ( char *coll ) {
	//return true;
	log ( LOG_DEBUG, "db: Verifying Spiderdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      cr->m_collnum  ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		char *k = list.getCurrentRec();
		//key_t k = list.getCurrentKey();
		count++;
		// what group's spiderdb should hold this rec
		//uint32_t groupId = g_hostdb.getGroupId ( RDB_SPIDERDB , k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		int32_t shardNum = g_hostdb.getShardNum(RDB_SPIDERDB,k);
		if ( shardNum == g_hostdb.getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %"INT32" records in spiderdb, "
		     "only %"INT32" belong to our shard.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Spiderdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log (LOG_DEBUG,"db: Spiderdb passed verification successfully for %"INT32" "
	      "recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

key128_t Spiderdb::makeKey ( int32_t      firstIp     ,
			     int64_t urlHash48   , 
			     bool      isRequest   ,
			     // MDW: now we use timestamp instead of parentdocid
			     // for spider replies. so they do not dedup...
			     int64_t parentDocId ,
			     bool      isDel       ) {
	key128_t k;
	k.n1 = (uint32_t)firstIp;
	// push ip to top 32 bits
	k.n1 <<= 32;
	// . top 32 bits of url hash are in the lower 32 bits of k.n1
	// . often the urlhash48 has top bits set that shouldn't be so mask
	//   it to 48 bits
	k.n1 |= (urlHash48 >> 16) & 0xffffffff;
	// remaining 16 bits
	k.n0 = urlHash48 & 0xffff;
	// room for isRequest
	k.n0 <<= 1;
	if ( isRequest ) k.n0 |= 0x01;
	// parent docid
	k.n0 <<= 38;
	// if we are making a spider reply key just leave the parentdocid as 0
	// so we only store one reply per url. the last reply we got.
	// if ( isRequest ) k.n0 |= parentDocId & DOCID_MASK;
	k.n0 |= parentDocId & DOCID_MASK;
	// reserved (padding)
	k.n0 <<= 8;
	// del bit
	k.n0 <<= 1;
	if ( ! isDel ) k.n0 |= 0x01;
	return k;
}

/////////////////////////
/////////////////////////      DOLEDB
/////////////////////////

// reset rdb
void Doledb::reset() { m_rdb.reset(); }

bool Doledb::init ( ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	// . use 5MB for the tree
	int32_t maxTreeMem    = 150000000; // 150MB
	int32_t maxTreeNodes  = maxTreeMem / 78;
	// we use the same disk page size as indexdb (for rdbmap.cpp)
	//int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// disk page cache mem, hard code to 5MB
	// int32_t pcmem = 5000000; // g_conf.m_spiderdbMaxDiskPageCacheMem;
	// keep this low if we are the tmp cluster
	// if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// we no longer dump doledb to disk, Rdb::dumpTree() does not allow it
	// pcmem = 0;
	// we now use a page cache
	// if ( ! m_pc.init ( "doledb"  , 
	// 		   RDB_DOLEDB ,
	// 		   pcmem     ,
	// 		   pageSize  ))
	// 	return log(LOG_INIT,"doledb: Init failed.");

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "doledb"                    ,
			    true                        , // dedup
			    -1                          , // fixedDataSize
			    2                           , // MinFilesToMerge
			    maxTreeMem                  ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // spiderdbMaxCacheMe
			    0                           , // maxCacheNodes 
			    false                       , // half keys?
			    false                       , // save cache?
			    NULL))//&m_pc                       ))
		return false;
	return true;
}
/*
bool Doledb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	//if ( ! doVerify ) return true;
	// verify
	//if ( verify(coll) ) return true;
	// if not allowing scale, return false
	//if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	//log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

/////////////////////////
/////////////////////////      SpiderCache
/////////////////////////


// . reload everything this many seconds
// . this was originally done to as a lazy compensation for a bug but
//   now i do not add too many of the same domain if the same domain wait
//   is ample and we know we'll be refreshed in X seconds anyway
//#define DEFAULT_SPIDER_RELOAD_RATE (3*60*60)


// for caching in s_ufnTree
//#define MAX_NODES (30)

// a global class extern'd in .h file
SpiderCache g_spiderCache;

SpiderCache::SpiderCache ( ) {
	//m_numSpiderColls   = 0;
	//m_isSaving = false;
}

// returns false and set g_errno on error
bool SpiderCache::init ( ) {

	//for ( int32_t i = 0 ; i < MAX_COLL_RECS ; i++ )
	//	m_spiderColls[i] = NULL;

	// success
	return true;
}

/*
static void doneSavingWrapper ( void *state ) {
	SpiderCache *THIS = (SpiderCache *)state;
	log("spcache: done saving something");
	//THIS->doneSaving();
	// . call the callback if any
	// . this let's PageMaster.cpp know when we're closed
	//if (THIS->m_closeCallback) THIS->m_closeCallback(THIS->m_closeState);
}
void SpiderCache::doneSaving ( ) {
	// bail if g_errno was set
	if ( g_errno ) {
		log("spider: Had error saving waitingtree.dat or doleiptable: "
		    "%s.",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	else {
		// display any error, if any, otherwise prints "Success"
		logf(LOG_INFO,"db: Successfully saved waitingtree and "
		     "doleiptable");
	}
	// if still more need to save, not done yet
	if ( needsSave  ( ) ) return;
	// ok, call callback that initiaed the save
	if ( m_callback ) m_callback ( THIS->m_state );
	// ok, we are done!
	//m_isSaving = false;
}
*/


// return false if any tree save blocked
void SpiderCache::save ( bool useThread ) {
	// bail if already saving
	//if ( m_isSaving ) return true;
	// assume saving
	//m_isSaving = true;
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		RdbTree *tree = &sc->m_waitingTree;
		if ( ! tree->m_needsSave ) continue;
		// if already saving from a thread
		if ( tree->m_isSaving ) continue;
		char *filename = "waitingtree";
		char dir[1024];
		sprintf(dir,"%scoll.%s.%"INT32"",g_hostdb.m_dir,
			sc->m_coll,(int32_t)sc->m_collnum);
		// log it for now
		log("spider: saving waiting tree for cn=%"INT32"",(int32_t)i);
		// returns false if it blocked, callback will be called
		tree->fastSave ( dir, // g_hostdb.m_dir ,
				 filename ,
				 useThread ,
				 NULL,//this ,
				 NULL);//doneSavingWrapper );
		// also the doleIpTable
		/*
		filename = "doleiptable.dat";
		sc->m_doleIpTable.fastSave(useThread,
					   dir,
					   filename,
					   NULL,
					   0,
					   NULL,//this,
					   NULL);//doneSavingWrapper );
		*/
		// . crap, this is made at startup from waitintree!
		/*
		// waiting table
		filename = "waitingtable.dat";
		if ( sc->m_waitingTable.m_needsSave )
			logf(LOG_INFO,"db: Saving %s/%s",dir,
			     filename);
		sc->m_waitingTable.fastSave(useThread,
					    dir,
					    filename,
					    NULL,
					    0,
					    NULL,//this,
					    NULL );//doneSavingWrapper );
		*/
	}
	// if still needs save, not done yet, return false to indicate blocked
	//if ( blocked ) return false;
	// all done
	//m_isSaving = false;
	// did not block
	//return true;
}

bool SpiderCache::needsSave ( ) {
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		if ( sc->m_waitingTree.m_needsSave ) return true;
		// also the doleIpTable
		//if ( sc->m_doleIpTable.m_needsSave ) return true;
	}
	return false;
}

void SpiderCache::reset ( ) {
	log(LOG_DEBUG,"spider: resetting spidercache");
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->reset();
		mdelete ( sc , sizeof(SpiderColl) , "SpiderCache" );
		delete ( sc );
		//m_spiderColls[i] = NULL;
		CollectionRec *cr = g_collectiondb.getRec(i);
		cr->m_spiderColl = NULL;
	}
	//m_numSpiderColls = 0;
}

SpiderColl *SpiderCache::getSpiderCollIffNonNull ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	if ( collnum >= g_collectiondb.m_numRecs ) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// empty?
	if ( ! cr ) return NULL;
	// return it if non-NULL
	return cr->m_spiderColl;
}

bool tryToDeleteSpiderColl ( SpiderColl *sc , char *msg ) {
	// if not being deleted return false
	if ( ! sc->m_deleteMyself ) return false;
	// otherwise always return true
	if ( sc->m_msg5b.m_waitingForList ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting1",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_msg1.m_mcast.m_inUse ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
	// 	    "waiting2",
	// 	    (PTRTYPE)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	if ( sc->m_isLoading ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting3",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// this means msg5 is out
	if ( sc->m_msg5.m_waitingForList ) {
		log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" "
		    "waiting4",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_gettingList1 ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" 
	//"waiting5",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// if ( sc->m_gettingList2 ) {
	// 	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" 
	//"waiting6",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// there's still a core of someone trying to write to someting
	// in "sc" so we have to try to fix that. somewhere in xmldoc.cpp
	// or spider.cpp. everyone should get sc from cr everytime i'd think
	log("spider: deleting sc=0x%"PTRFMT" for collnum=%"INT32" (msg=%s)",
	    (PTRTYPE)sc,(int32_t)sc->m_collnum,msg);
	// . make sure nobody has it
	// . cr might be NULL because Collectiondb.cpp::deleteRec2() might
	//   have nuked it
	//CollectionRec *cr = sc->m_cr;
	// use fake ptrs for easier debugging
	//if ( cr ) cr->m_spiderColl = (SpiderColl *)0x987654;//NULL;
	mdelete ( sc , sizeof(SpiderColl),"postdel1");
	delete ( sc );
	return true;
}

// . get SpiderColl for a collection
// . if it is NULL for that collection then make a new one
SpiderColl *SpiderCache::getSpiderColl ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	// return it if non-NULL
	//if ( m_spiderColls [ collnum ] ) return m_spiderColls [ collnum ];
	// if spidering disabled, do not bother creating this!
	//if ( ! g_conf.m_spideringEnabled ) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// collection might have been reset in which case collnum changes
	if ( ! cr ) return NULL;
	// return it if non-NULL
	SpiderColl *sc = cr->m_spiderColl;
	if ( sc ) return sc;
	// if spidering disabled, do not bother creating this!
	//if ( ! cr->m_spideringEnabled ) return NULL;
	// cast it
	//SpiderColl *sc;
	// make it
	try { sc = new(SpiderColl); }
	catch ( ... ) {
		log("spider: failed to make SpiderColl for collnum=%"INT32"",
		    (int32_t)collnum);
		return NULL;
	}
	// register it
	mnew ( sc , sizeof(SpiderColl), "spcoll" );
	// store it
	//m_spiderColls [ collnum ] = sc;
	cr->m_spiderColl = sc;
	// note it
	logf(LOG_DEBUG,"spider: made spidercoll=%"PTRFMT" for cr=%"PTRFMT"",
	    (PTRTYPE)sc,(PTRTYPE)cr);
	// update this
	//if ( m_numSpiderColls < collnum + 1 )
	//	m_numSpiderColls = collnum + 1;
	// set this
	sc->m_collnum = collnum;
	// save this
	strcpy ( sc->m_coll , cr->m_coll );
	// set this
	if ( ! strcmp ( cr->m_coll,"qatest123" ) ) sc->m_isTestColl = true;
	else                                  sc->m_isTestColl = false;
	
	// set this
	sc->setCollectionRec ( cr ); // sc->m_cr = cr;

	// set first doledb scan key
	sc->m_nextDoledbKey.setMin();

	// turn off quickpolling while loading incase a parm update comes in
	bool saved = g_conf.m_useQuickpoll;
	g_conf.m_useQuickpoll = false;

	// mark it as loading so it can't be deleted while loading
	sc->m_isLoading = true;
	// . load its tables from disk
	// . crap i think this might call quickpoll and we get a parm
	//   update to delete this spider coll!
	sc->load();
	// mark it as loading
	sc->m_isLoading = false;

	// restore
	g_conf.m_useQuickpoll = saved;

	// did crawlbottesting delete it right away?
	if ( tryToDeleteSpiderColl( sc ,"1") ) return NULL;
	// sanity check
	//if ( ! cr ) { char *xx=NULL;*xx=0; }
	// deleted right away?
	//if ( sc->getCollectionRec() == NULL ) { char *xx=NULL;*xx=0; }
	// note it!
	log(LOG_DEBUG,"spider: adding new spider collection for %s",
	    cr->m_coll);
	// that was it
	return sc;
}



key_t makeWaitingTreeKey ( uint64_t spiderTimeMS , int32_t firstIp ) {
	// sanity
	if ( ((int64_t)spiderTimeMS) < 0 ) { char *xx=NULL;*xx=0; }
	// make the wait tree key
	key_t wk;
	wk.n1 = (spiderTimeMS>>32);
	wk.n0 = (spiderTimeMS&0xffffffff);
	wk.n0 <<= 32;
	wk.n0 |= (uint32_t)firstIp;
	// sanity
	if ( wk.n1 & 0x8000000000000000LL ) { char *xx=NULL;*xx=0; }
	return wk;
}


//
// remove all recs from doledb for the given collection
//
static void nukeDoledbWrapper ( int fd , void *state ) {
	g_loop.unregisterSleepCallback ( state , nukeDoledbWrapper );
	collnum_t collnum = *(collnum_t *)state;
	nukeDoledb ( collnum );
}

void nukeDoledb ( collnum_t collnum ) {

	//g_spiderLoop.m_winnerListCache.verify();	
	// in case we changed url filters for this collection #
	g_spiderLoop.m_winnerListCache.clear ( collnum );

	//g_spiderLoop.m_winnerListCache.verify();	

	//WaitEntry *we = (WaitEntry *)state;

	//if ( we->m_registered )
	//	g_loop.unregisterSleepCallback ( we , doDoledbNuke );

	// . nuke doledb for this collnum
	// . it will unlink the files and maps for doledb for this collnum
	// . it will remove all recs of this collnum from its tree too
	if ( g_doledb.getRdb()->isSavingTree () ) {
		g_loop.registerSleepCallback(100,&collnum,nukeDoledbWrapper);
		//we->m_registered = true;
		return;
	}

	// . ok, tree is not saving, it should complete entirely from this call
	g_doledb.getRdb()->deleteAllRecs ( collnum );

	// re-add it back so the RdbBase is new'd
	//g_doledb.getRdb()->addColl2 ( we->m_collnum );

	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull ( collnum );

	if ( sc ) {
		sc->m_lastUrlFiltersUpdate = getTimeLocal();//GlobalNoCore();
		// . make sure to nuke m_doleIpTable as well
		sc->m_doleIpTable.clear();
		// need to recompute this!
		//sc->m_ufnMapValid = false;
		// reset this cache
		//clearUfnTable();
		// log it
		log("spider: rebuilding %s from doledb nuke",
		    sc->getCollName());
		// activate a scan if not already activated
		sc->m_waitingTreeNeedsRebuild = true;
		// if a scan is ongoing, this will re-set it
		sc->m_nextKey2.setMin();
		// clear it?
		sc->m_waitingTree.clear();
		sc->m_waitingTable.clear();
		// kick off the spiderdb scan to rep waiting tree and doledb
		sc->populateWaitingTreeFromSpiderdb(false);
	}

	// nuke this state
	//mfree ( we , sizeof(WaitEntry) , "waitet" );

	// note it
	log("spider: finished nuking doledb for coll (%"INT32")",
	    (int32_t)collnum);
}




////////
//
// winner tree key. holds the top/best spider requests for a firstIp
// for spidering purposes.
//
////////

// key bitmap (192 bits):
//
// ffffffff ffffffff ffffffff ffffffff  f=firstIp
// pppppppp pppppppp HHHHHHHH HHHHHHHH  p=255-priority  H=hopcount
// tttttttt tttttttt tttttttt tttttttt  t=spiderTimeMS
// tttttttt tttttttt tttttttt tttttttt  h=urlHash48
// hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh 
// hhhhhhhh hhhhhhhh 00000000 00000000

key192_t makeWinnerTreeKey ( int32_t firstIp ,
			     int32_t priority ,
			     int32_t hopCount,
			     int64_t spiderTimeMS ,
			     int64_t uh48 ) {
	key192_t k;
	k.n2 = firstIp;
	k.n2 <<= 16;
	k.n2 |= (255-priority);
	k.n2 <<= 16;
	// query reindex is still using hopcount -1...
	if ( hopCount == -1 ) hopCount = 0;
	if ( hopCount < 0 ) { char *xx=NULL;*xx=0; }
	if ( hopCount > 0xffff ) hopCount = 0xffff;
	k.n2 |= hopCount;

	k.n1 = spiderTimeMS;

	k.n0 = uh48;
	k.n0 <<= 16;

	return k;
}

void parseWinnerTreeKey ( key192_t  *k ,
			  int32_t      *firstIp ,
			  int32_t      *priority ,
			  int32_t *hopCount,
			  int64_t  *spiderTimeMS ,
			  int64_t *uh48 ) {
	*firstIp = (k->n2) >> 32;
	*priority = 255 - ((k->n2 >> 16) & 0xffff);
	*hopCount = (k->n2 & 0xffff);

	*spiderTimeMS = k->n1;

	*uh48 = (k->n0 >> 16);
}

void testWinnerTreeKey ( ) {
	int32_t firstIp = 1234567;
	int32_t priority = 123;
	int64_t spiderTimeMS = 456789123LL;
	int64_t uh48 = 987654321888LL;
	int32_t hc = 4321;
	key192_t k = makeWinnerTreeKey (firstIp,priority,hc,spiderTimeMS,uh48);
	int32_t firstIp2;
	int32_t priority2;
	int64_t spiderTimeMS2;
	int64_t uh482;
	int32_t hc2;
	parseWinnerTreeKey(&k,&firstIp2,&priority2,&hc2,&spiderTimeMS2,&uh482);
	if ( firstIp != firstIp2 ) { char *xx=NULL;*xx=0; }
	if ( priority != priority2 ) { char *xx=NULL;*xx=0; }
	if ( spiderTimeMS != spiderTimeMS2 ) { char *xx=NULL;*xx=0; }
	if ( uh48 != uh482 ) { char *xx=NULL;*xx=0; }
	if ( hc != hc2 ) { char *xx=NULL;*xx=0; }
}

void removeExpiredLocks ( int32_t hostId );





/////////////////////////
/////////////////////////      UTILITY FUNCTIONS
/////////////////////////

// . map a spiderdb rec to the shard # that should spider it
// . "sr" can be a SpiderRequest or SpiderReply
// . shouldn't this use Hostdb::getShardNum()?
/*
uint32_t getShardToSpider ( char *sr ) {
	// use the url hash
	int64_t uh48 = g_spiderdb.getUrlHash48 ( (key128_t *)sr );
	// host to dole it based on ip
	int32_t hostId = uh48 % g_hostdb.m_numHosts ;
	// get it
	Host *h = g_hostdb.getHost ( hostId ) ;
	// and return groupid
	return h->m_groupId;
}
*/

// does this belong in our spider cache?
bool isAssignedToUs ( int32_t firstIp ) {
	// sanity check... must be in our group.. we assume this much
	//if ( g_spiderdb.getGroupId(firstIp) != g_hostdb.m_myHost->m_groupId){
	//	char *xx=NULL;*xx=0; }
	// . host to dole it based on ip
	// . ignore lower 8 bits of ip since one guy often owns a whole block!
	//int32_t hostId=(((uint32_t)firstIp) >> 8) % g_hostdb.getNumHosts();

	if( !g_hostdb.getMyHost()->m_spiderEnabled ) return false;
	
	// get our group
	//Host *group = g_hostdb.getMyGroup();
	Host *shard = g_hostdb.getMyShard();
	// pick a host in our group

	// if not dead return it
	//if ( ! g_hostdb.isDead(hostId) ) return hostId;
	// get that host
	//Host *h = g_hostdb.getHost(hostId);
	// get the group
	//Host *group = g_hostdb.getGroup ( h->m_groupId );
	// and number of hosts in the group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// let's mix it up since spider shard was selected using this
	// same mod on the firstIp method!!
	uint64_t h64 = firstIp;
	unsigned char c = firstIp & 0xff;
	h64 ^= g_hashtab[c][0];
	// select the next host number to try
	//int32_t next = (((uint32_t)firstIp) >> 16) % hpg ;
	// hash to a host
	int32_t i = ((uint32_t)h64) % hpg;
	Host *h = &shard[i];
	// return that if alive
	if ( ! g_hostdb.isDead(h) && h->m_spiderEnabled) {
		return (h->m_hostId == g_hostdb.m_hostId);
	}
	// . select another otherwise
	// . put all alive in an array now
	Host *alive[64];
	int32_t upc = 0;

	for ( int32_t j = 0 ; j < hpg ; j++ ) {
		Host *h = &shard[j];
		if ( g_hostdb.isDead(h) ) continue;
		if( ! h->m_spiderEnabled ) continue;
		alive[upc++] = h;
	}
	// if none, that is bad! return the first one that we wanted to
	if ( upc == 0 ) {
		log("spider: no hosts can handle spider request for ip=%s", iptoa(firstIp));
		return false;
		//return (h->m_hostId == g_hostdb.m_hostId);
	}
	// select from the good ones now
	i  = ((uint32_t)firstIp) % upc;
	// get that
	h = alive[i]; //&shard[i];
	// guaranteed to be alive... kinda
	return (h->m_hostId == g_hostdb.m_hostId);
}



void doneSendingNotification ( void *state ) {
	EmailInfo *ei = (EmailInfo *)state;
	collnum_t collnum = ei->m_collnum;
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( cr != ei->m_collRec ) cr = NULL;
	char *coll = "lostcoll";
	if ( cr ) coll = cr->m_coll;
	log(LOG_INFO,"spider: done sending notifications for coll=%s (%i)", 
	    coll,(int)ei->m_collnum);

	// all done if collection was deleted from under us
	if ( ! cr ) return;

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = false;

	// we can re-use the EmailInfo class now
	// pingserver.cpp sets this
	//ei->m_inUse = false;

	// so we do not send for this status again, mark it as sent for
	// but we reset sentCrawlDoneAlert to 0 on round increment below
	//log("spider: setting sentCrawlDoneAlert status to %"INT32"",
	//    (int32_t)cr->m_spiderStatus);

	// mark it as sent. anytime a new url is spidered will mark this
	// as false again! use LOCAL crawlInfo, since global is reset often.
	cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 1;//cr->m_spiderStatus;//1;

	// be sure to save state so we do not re-send emails
	cr->m_needsSave = 1;

	// sanity
	if ( cr->m_spiderStatus == 0 ) { char *xx=NULL;*xx=0; }

	// i guess each host advances its own round... so take this out
	// sanity check
	//if ( g_hostdb.m_myHost->m_hostId != 0 ) { char *xx=NULL;*xx=0; }

	//float respiderFreq = -1.0;
	float respiderFreq = cr->m_collectiveRespiderFrequency;

	// if not REcrawling, set this to 0 so we at least update our
	// round # and round start time...
	if ( respiderFreq < 0.0 ) //== -1.0 ) 
		respiderFreq = 0.0;

	//if ( respiderFreq < 0.0 ) {
	//	log("spider: bad respiderFreq of %f. making 0.",
	//	    respiderFreq);
	//	respiderFreq = 0.0;
	//}

	// advance round if that round has completed, or there are no
	// more urls to spider. if we hit maxToProcess/maxToCrawl then 
	// do not increment the round #. otherwise we should increment it.
	// do allow maxtocrawl guys through if they repeat, however!
	//if(cr->m_spiderStatus == SP_MAXTOCRAWL && respiderFreq <= 0.0)return;
	//if(cr->m_spiderStatus == SP_MAXTOPROCESS && respiderFreq<=0.0)return;

	
	////////
	//
	// . we are here because hasUrlsReadyToSpider is false
	// . we just got done sending an email alert
	// . now increment the round only if doing rounds!
	//
	///////


	// if not doing rounds, keep the round 0. they might want to up
	// their maxToCrawl limit or something.
	if ( respiderFreq <= 0.0 ) return;


	// if we hit the max to crawl rounds, then stop!!! do not
	// increment the round...
	if ( cr->m_spiderRoundNum >= cr->m_maxCrawlRounds &&
	     // there was a bug when maxCrawlRounds was 0, which should
	     // mean NO max, so fix that here:
	     cr->m_maxCrawlRounds > 0 ) return;

	// this should have been set below
	//if ( cr->m_spiderRoundStartTime == 0 ) { char *xx=NULL;*xx=0; }

	// find the "respider frequency" from the first line in the url
	// filters table whose expressions contains "{roundstart}" i guess
	//for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
	//	// get it
	//	char *ex = cr->m_regExs[i].getBufStart();
	//	// compare
	//	if ( ! strstr ( ex , "roundstart" ) ) continue;
	//	// that's good enough
	//	respiderFreq = cr->m_spiderFreqs[i];
	//	break;
	//}

	int32_t seconds = (int32_t)(respiderFreq * 24*3600);
	// add 1 for lastspidertime round off errors so we can be assured
	// all spiders have a lastspidertime LESS than the new
	// m_spiderRoundStartTime we set below.
	if ( seconds <= 0 ) seconds = 1;

	// now update this round start time. all the other hosts should
	// sync with us using the parm sync code, msg3e, every 13.5 seconds.
	//cr->m_spiderRoundStartTime += respiderFreq;
	char roundTime[128];
	sprintf(roundTime,"%"UINT32"", (uint32_t)(getTimeGlobal() + seconds));
	// roundNum++ round++
	char roundStr[128];
	sprintf(roundStr,"%"INT32"", cr->m_spiderRoundNum + 1);

	// waiting tree will usually be empty for this coll since no
	// spider requests had a valid spider priority, so let's rebuild!
	// this is not necessary because PF_REBUILD is set for the
	// "spiderRoundStart" parm in Parms.cpp so it will rebuild if that parm
	// changes already.
	//if ( cr->m_spiderColl )
	//	cr->m_spiderColl->m_waitingTreeNeedsRebuild = true;

	// we have to send these two parms to all in cluster now INCLUDING
	// ourselves, when we get it in Parms.cpp there's special
	// code to set this *ThisRound counts to 0!!!
	SafeBuf parmList;
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundStr,-1 ,
				    "spiderRoundNum");
	g_parms.addNewParmToList1 ( &parmList,cr->m_collnum,roundTime, -1 ,
				    "spiderRoundStart");

	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundNum" ); 
	//g_parms.addParmToList1 ( &parmList , cr , "spiderRoundStart" ); 
	// this uses msg4 so parm ordering is guaranteed
	g_parms.broadcastParmList ( &parmList , NULL , NULL );

	// log it
	log("spider: new round #%"INT32" starttime = %"UINT32" for %s"
	    , cr->m_spiderRoundNum
	    , (uint32_t)cr->m_spiderRoundStartTime
	    , cr->m_coll
	    );
}

bool sendNotificationForCollRec ( CollectionRec *cr )  {

	// do not send email for maxrounds hit, it will send a round done
	// email for that. otherwise we end up calling doneSendingEmail()
	// twice and increment the round twice
	//if ( cr->m_spiderStatus == SP_MAXROUNDS ) {
	//	log("spider: not sending email for max rounds limit "
	//	    "since already sent for round done.");
	//	return true;
	//}

	// wtf? caller must set this
	if ( ! cr->m_spiderStatus ) { char *xx=NULL; *xx=0; }

	log(LOG_INFO,
	    "spider: sending notification for crawl status %"INT32" in "
	    "coll %s. "
	    //"current sent state is %"INT32""
	    ,(int32_t)cr->m_spiderStatus
	    ,cr->m_coll
	    //cr->m_spiderStatusMsg,
	    //,(int32_t)cr->m_localCrawlInfo.m_sentCrawlDoneAlert);
	    );

	// if we already sent it return now. we set this to false everytime
	// we spider a url, which resets it. use local crawlinfo for this
	// since we reset global.
	//if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) return true;

	if ( cr->m_sendingAlertInProgress ) return true;

	// ok, send it
	//EmailInfo *ei = &cr->m_emailInfo;
	EmailInfo *ei = (EmailInfo *)mcalloc ( sizeof(EmailInfo),"eialrt");
	if ( ! ei ) {
		log("spider: could not send email alert: %s",
		    mstrerror(g_errno));
		return true;
	}

	// in use already?
	//if ( ei->m_inUse ) return true;

	// pingserver.cpp sets this
	//ei->m_inUse = true;

	// set it up
	ei->m_finalCallback = doneSendingNotification;
	ei->m_finalState    = ei;
	ei->m_collnum       = cr->m_collnum;
	// collnums can be recycled, so ensure collection with the ptr
	ei->m_collRec       = cr;

	SafeBuf *buf = &ei->m_spiderStatusMsg;
	// stop it from accumulating status msgs
	buf->reset();
	int32_t status = -1;
	getSpiderStatusMsg ( cr , buf , &status );
					 
	// if no email address or webhook provided this will not block!
	// DISABLE THIS UNTIL FIXED

	//log("spider: SENDING EMAIL NOT");

	// do not re-call this stuff
	cr->m_sendingAlertInProgress = true;

	// ok, put it back...
	if ( ! sendNotification ( ei ) ) return false;

	// so handle this ourselves in that case:
	doneSendingNotification ( ei );
	
	mfree ( ei , sizeof(EmailInfo) ,"eialrt" );

	return true;
}

// we need to update crawl info for collections that
// have urls ready to spider



SpiderColl *getNextSpiderColl ( int32_t *cri ) ;




void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// process the doledb list and try to launch a spider
	g_spiderLoop.gotDoledbList2();
	// regardless of whether that blocked or not try to launch another 
	// and try to get the next SpiderRequest from doledb
	g_spiderLoop.spiderDoledUrls();
}




void gotLockReplyWrapper ( void *state , UdpSlot *slot ) {
	// cast it
	Msg12 *msg12 = (Msg12 *)state;
	// . call handler
	// . returns false if waiting for more replies to come in
	if ( ! msg12->gotLockReply ( slot ) ) return;
	// if had callback, maybe from PageReindex.cpp
	if ( msg12->m_callback ) msg12->m_callback ( msg12->m_state );
	// ok, try to get another url to spider
	else                     g_spiderLoop.spiderDoledUrls();
}

Msg12::Msg12 () {
	m_numRequests = 0;
	m_numReplies  = 0;
}

// . returns false if blocked, true otherwise.
// . returns true and sets g_errno on error
// . before we can spider for a SpiderRequest we must be granted the lock
// . each group shares the same doledb and each host in the group competes
//   for spidering all those urls. 
// . that way if a host goes down is load is taken over
bool Msg12::getLocks ( int64_t uh48, // probDocId , 
		       char *url ,
		       DOLEDBKEY *doledbKey,
		       collnum_t collnum,
		       int32_t sameIpWaitTime,
		       int32_t maxSpidersOutPerIp,
		       int32_t firstIp,
		       void *state ,
		       void (* callback)(void *state) ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// do not use locks for injections
	//if ( m_sreq->m_isInjecting ) return true;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset
	m_numRequests = 0;
	m_numReplies  = 0;
	m_grants   = 0;
	m_removing = false;
	m_confirming = false;
	// make sure is really docid
	//if ( probDocId & ~DOCID_MASK ) { char *xx=NULL;*xx=0; }
	// . mask out the lower bits that may change if there is a collision
	// . in this way a url has the same m_probDocId as the same url
	//   in the index. i.e. if we add a new spider request for url X and
	//   url X is already indexed, then they will share the same lock 
	//   even though the indexed url X may have a different actual docid
	//   than its probable docid.
	// . we now use probable docids instead of uh48 because query reindex
	//   in PageReindex.cpp adds docid based spider requests and we
	//   only know the docid, not the uh48 because it is creating
	//   SpiderRequests from docid-only search results. having to look
	//   up the msg20 summary for like 1M search results is too painful!
	//m_lockKey = g_titledb.getFirstProbableDocId(probDocId);
	// . use this for locking now, and let the docid-only requests just use
	//   the docid
	m_lockKeyUh48 = makeLockTableKey ( uh48 , firstIp );
	m_url = url;
	m_callback = callback;
	m_state = state;
	m_hasLock = false;
	m_origUh48 = uh48;
	// support ability to spider multiple urls from same ip
	m_doledbKey = *doledbKey;
	m_collnum = collnum;
	m_sameIpWaitTime = sameIpWaitTime;
	m_maxSpidersOutPerIp = maxSpidersOutPerIp;
	m_firstIp = firstIp;

	// sanity check, just 6 bytes! (48 bits)
	if ( uh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	if ( m_lockKeyUh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	// cache time
	int32_t ct = 120;

	// if docid based assume it was a query reindex and keep it short!
	// otherwise we end up waiting 120 seconds for a query reindex to
	// go through on a docid we just spidered. TODO: use m_urlIsDocId
	// MDW: check this out
	if ( url && is_digit(url[0]) ) ct = 2;

	// . this seems to be messing us up and preventing us from adding new
	//   requests into doledb when only spidering a few IPs.
	// . make it random in the case of twin contention
	ct = rand() % 10;

	// . check our cache to avoid repetitive asking
	// . use -1 for maxAge to indicate no max age
	// . returns -1 if not in cache
	// . use maxage of two minutes, 120 seconds
	int32_t lockTime ;
	lockTime = g_spiderLoop.m_lockCache.getLong(0,m_lockKeyUh48,ct,true);
	// if it was in the cache and less than 2 minutes old then return
	// true now with m_hasLock set to false.
	if ( lockTime >= 0 ) {
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: cached missed lock for %s "
			     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
		return true;
	}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending lock request for %s "
		     "lockkey=%"UINT64"",  m_url,m_lockKeyUh48);

	// now the locking group is based on the probable docid
	//m_lockGroupId = g_hostdb.getGroupIdFromDocId(m_lockKey);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();

	// shortcut
	UdpServer *us = &g_udpServer;


	static int32_t s_lockSequence = 0;
	// remember the lock sequence # in case we have to call remove locks
	m_lockSequence = s_lockSequence++;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 0;
	lr->m_lockSequence = m_lockSequence;
	lr->m_collnum = collnum;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// point to start of the 12 byte request buffer
	char *request = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead (h) ) continue;
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: sent lock "
			     "request #%"INT32" for lockkey=%"UINT64" %s to "
			     "hid=%"INT32"",m_numRequests,m_lockKeyUh48,
			     m_url,h->m_hostId);
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// i guess nothing... hmmm... all dead?
	//char *xx=NULL; *xx=0; 
	// m_hasLock should be false... all lock hosts seem dead... wait
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: all lock hosts seem dead for %s "
		     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
	return true;
}

// after adding the negative doledb recs to remove the url we are spidering
// from doledb, and adding the fake titledb rec to add a new entry into
// waiting tree so that our ip can have more than one outstanding spider,
// call the callback. usually msg4::addMetaList() will not block i'd guess.
void rejuvenateIPWrapper ( void *state ) {
	Msg12 *THIS = (Msg12 *)state;
	THIS->m_callback ( THIS->m_state );
}

// returns true if all done, false if waiting for more replies
bool Msg12::gotLockReply ( UdpSlot *slot ) {

	// no longer use this
	char *xx=NULL;*xx=0;

	// got reply
	m_numReplies++;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;
	// check for a hammer reply
	char *reply     = slot->m_readBuf;
	int32_t  replySize = slot->m_readBufSize;
	// if error, treat as a not grant
	if ( g_errno ) {
		bool logIt = true;
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: got msg12 reply error = %s",
			    mstrerror(g_errno));
		// if we got an ETRYAGAIN when trying to confirm our lock
		// that means doledb was saving/dumping to disk and we 
		// could not remove the record from doledb and add an
		// entry to the waiting tree, so we need to keep trying
		if ( g_errno == ETRYAGAIN && m_confirming ) {
			// c ount it again
			m_numRequests++;
			// use what we were using
			char *request     = (char *)&m_confirmRequest;
			int32_t  requestSize = sizeof(ConfirmRequest);
			Host *h = g_hostdb.getHost(slot->m_hostId);
			// send request to him
			UdpServer *us = &g_udpServer;
			if ( ! us->sendRequest ( request      ,
						 requestSize  ,
						 0x12         , // msgType
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL         , // retSlotPtrPt
						 this         , // state data
						 gotLockReplyWrapper ,
						 60*60*24*365    ) ) 
				return false;
			// error?
			// don't spam the log!
			static int32_t s_last = 0;
			int32_t now = getTimeLocal();
			if ( now - s_last >= 1 ) {
				s_last = now;
				log("spider: error re-sending confirm "
				    "request: %s",  mstrerror(g_errno));
			}
		}
		// only log every 10 seconds for ETRYAGAIN
		if ( g_errno == ETRYAGAIN ) {
			static time_t s_lastTime = 0;
			time_t now = getTimeLocal();
			logIt = false;
			if ( now - s_lastTime >= 3 ) {
				logIt = true;
				s_lastTime = now;
			}
		}
		if ( logIt )
			log ( "sploop: host had error getting lock url=%s"
			      ": %s" ,
			      m_url,mstrerror(g_errno) );
	}
	// grant or not
	if ( replySize == 1 && ! g_errno && *reply == 1 ) m_grants++;
	// wait for all to get back
	if ( m_numReplies < m_numRequests ) return false;
	// all done if we were removing
	if ( m_removing ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done removing all locks "
			   "(replies=%"INT32") for %s",
			   m_numReplies,m_url);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		return true;
	}
	// all done if we were confirming
	if ( m_confirming ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done confirming all locks "
			   "for %s uh48=%"INT64"",m_url,m_origUh48);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		// . keep processing
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) return g_spiderLoop.spiderUrl2();
		// if we had a callback let our parent call it
		return true;
	}

	// if got ALL locks, spider it
	if ( m_grants == m_numReplies ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: got lock for docid=lockkey=%"UINT64"",
			   m_lockKeyUh48);
		// flag this
		m_hasLock = true;
		// we are done
		//m_gettingLocks = false;


		///////
		//
		// now tell our group (shard) to remove from doledb
		// and re-add to waiting tree. the evalIpLoop() function
		// should skip this probable docid because it is in the 
		// LOCK TABLE!
		//
		// This logic should allow us to spider multiple urls
		// from the same IP at the same time.
		//
		///////

		// returns false if would block
		if ( ! confirmLockAcquisition ( ) ) return false;
		// . we did it without blocking, maybe cuz we are a single node
		// . ok, they are all back, resume loop
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;

	}
	// note it
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: missed lock for %s lockkey=%"UINT64" "
		     "(grants=%"INT32")",   m_url,m_lockKeyUh48,m_grants);

	// . if it was locked by another then add to our lock cache so we do
	//   not try to lock it again
	// . if grants is not 0 then one host granted us the lock, but not
	//   all hosts, so we should probably keep trying on it until it is
	//   locked up by one host
	if ( m_grants == 0 ) {
		int32_t now = getTimeGlobal();
		g_spiderLoop.m_lockCache.addLong(0,m_lockKeyUh48,now,NULL);
	}

	// reset again
	m_numRequests = 0;
	m_numReplies  = 0;
	// no need to remove them if none were granted because another
	// host in our group might have it 100% locked. 
	if ( m_grants == 0 ) {
		// no longer in locks operation mode
		m_gettingLocks = false;
		// ok, they are all back, resume loop
		//if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;
	}
	// note that
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending request to all in shard to "
		     "remove lock uh48=%"UINT64". grants=%"INT32"",
		     m_lockKeyUh48,(int32_t)m_grants);
	// remove all locks we tried to get, BUT only if from our hostid!
	// no no! that doesn't quite work right... we might be the ones
	// locking it! i.e. another one of our spiders has it locked...
	if ( ! removeAllLocks ( ) ) return false; // true;
	// if did not block, how'd that happen?
	log("sploop: did not block in removeAllLocks: %s",mstrerror(g_errno));
	return true;
}

bool Msg12::removeAllLocks ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// skip if injecting
	//if ( m_sreq->m_isInjecting ) return true;
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing all locks for %s %"UINT64"",
		     m_url,m_lockKeyUh48);
	// we are now removing 
	m_removing = true;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_lockSequence = m_lockSequence;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 1;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	char *request     = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// now the locking group is based on the probable docid
	//uint32_t groupId = g_hostdb.getGroupIdFromDocId(m_lockKeyUh48);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( groupId );
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same group that is going to spider it!
	// i.e. our group! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyGroup();
	// shortcut
	UdpServer *us = &g_udpServer;
	// set the hi bit though for this one
	//m_lockKey |= 0x8000000000000000LL;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}

bool Msg12::confirmLockAcquisition ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// we are now removing 
	m_confirming = true;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	ConfirmRequest *cq = &m_confirmRequest;
	char *request     = (char *)cq;
	int32_t  requestSize = sizeof(ConfirmRequest);
	// sanity
	if ( requestSize == sizeof(LockRequest)){ char *xx=NULL;*xx=0; }
	// set it
	cq->m_collnum   = m_collnum;
	cq->m_doledbKey = m_doledbKey;
	cq->m_firstIp   = m_firstIp;
	cq->m_lockKeyUh48 = m_lockKeyUh48;
	cq->m_maxSpidersOutPerIp = m_maxSpidersOutPerIp;
	// . use the locking group from when we sent the lock request
	// . get ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same shard that is going to spider it!
	// i.e. our shard! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyShard();
	// shortcut
	UdpServer *us = &g_udpServer;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: confirming lock for uh48=%"UINT64" firstip=%s",
		    m_lockKeyUh48,iptoa(m_firstIp));
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 // a size of 2 should mean confirm
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 60*60*24*365    ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}


void handleRequest12 ( UdpSlot *udpSlot , int32_t niceness ) {
	// get request
	char *request = udpSlot->m_readBuf;
	int32_t  reqSize = udpSlot->m_readBufSize;
	// shortcut
	UdpServer *us = &g_udpServer;
	// breathe
	QUICKPOLL ( niceness );

	// shortcut
	char *reply = udpSlot->m_tmpBuf;

	//
	// . is it confirming that he got all the locks?
	// . if so, remove the doledb record and dock the doleiptable count
	//   before adding a waiting tree entry to re-pop the doledb record
	//
	if ( reqSize == sizeof(ConfirmRequest) ) {
		char *msg = NULL;
		ConfirmRequest *cq = (ConfirmRequest *)request;

		// confirm the lock
		HashTableX *ht = &g_spiderLoop.m_lockTable;
		int32_t slot = ht->getSlot ( &cq->m_lockKeyUh48 );
		if ( slot < 0 ) { 
			log("spider: got a confirm request for a key not "
			    "in the table! coll must have been deleted "
			    " or reset "
			    "while lock request was outstanding.");
			g_errno = EBADENGINEER;
			us->sendErrorReply ( udpSlot , g_errno );
			return;
			//char *xx=NULL;*xx=0; }
		}
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot ( slot );
		lock->m_confirmed = true;

		// note that
		if ( g_conf.m_logDebugSpider ) // Wait )
			log("spider: got confirm lock request for ip=%s",
			    iptoa(lock->m_firstIp));

		// get it
		SpiderColl *sc = g_spiderCache.getSpiderColl(cq->m_collnum);
		// make it negative
		cq->m_doledbKey.n0 &= 0xfffffffffffffffeLL;
		// and add the negative rec to doledb (deletion operation)
		Rdb *rdb = &g_doledb.m_rdb;
		if ( ! rdb->addRecord ( cq->m_collnum,
					(char *)&cq->m_doledbKey,
					NULL , // data
					0    , //dataSize
					1 )){ // niceness
			// tree is dumping or something, probably ETRYAGAIN
			if ( g_errno != ETRYAGAIN ) {msg = "error adding neg rec to doledb";	log("spider: %s %s",msg,mstrerror(g_errno));
			}
			//char *xx=NULL;*xx=0;
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// now remove from doleiptable since we removed from doledb
		if ( sc ) sc->removeFromDoledbTable ( cq->m_firstIp );

		// how many spiders outstanding for this coll and IP?
		//int32_t out=g_spiderLoop.getNumSpidersOutPerIp ( cq->m_firstIp);

		// DO NOT add back to waiting tree if max spiders
		// out per ip was 1 OR there was a crawldelay. but better
		// yet, take care of that in the winReq code above.

		// . now add to waiting tree so we add another spiderdb
		//   record for this firstip to doledb
		// . true = callForScan
		// . do not add to waiting tree if we have enough outstanding
		//   spiders for this ip. we will add to waiting tree when
		//   we receive a SpiderReply in addSpiderReply()
		if ( sc && //out < cq->m_maxSpidersOutPerIp &&
		     // this will just return true if we are not the 
		     // responsible host for this firstip
		    // DO NOT populate from this!!! say "false" here...
		     ! sc->addToWaitingTree ( 0 , cq->m_firstIp, false ) &&
		     // must be an error...
		     g_errno ) {
			msg = "FAILED TO ADD TO WAITING TREE";
			log("spider: %s %s",msg,mstrerror(g_errno));
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// success!!
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}



	// sanity check
	if ( reqSize != sizeof(LockRequest) ) {
		log("spider: bad msg12 request size of %"INT32"",reqSize);
		us->sendErrorReply ( udpSlot , EBADREQUEST );
		return;
	}
	// deny it if we are not synced yet! otherwise we core in 
	// getTimeGlobal() below
	if ( ! isClockInSync() ) { 
		// log it so we can debug it
		//log("spider: clock not in sync with host #0. so "
		//    "returning etryagain for lock reply");
		// let admin know why we are not spidering
		us->sendErrorReply ( udpSlot , ETRYAGAIN );
		return;
	}

	LockRequest *lr = (LockRequest *)request;
	//uint64_t lockKey = *(int64_t *)request;
	//int32_t lockSequence = *(int32_t *)(request+8);
	// is this a remove operation? assume not
	//bool remove = false;
	// get top bit
	//if ( lockKey & 0x8000000000000000LL ) remove = true;

	// mask it out
	//lockKey &= 0x7fffffffffffffffLL;
	// sanity check, just 6 bytes! (48 bits)
	if ( lr->m_lockKeyUh48 &0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: got msg12 request uh48=%"INT64" remove=%"INT32"",
		    lr->m_lockKeyUh48, (int32_t)lr->m_removeLock);
	// get time
	int32_t nowGlobal = getTimeGlobal();
	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

	int32_t hostId = g_hostdb.getHostId ( udpSlot->m_ip , udpSlot->m_port );
	// this must be legit - sanity check
	if ( hostId < 0 ) { char *xx=NULL;*xx=0; }

	// remove expired locks from locktable
	removeExpiredLocks ( hostId );

	int64_t lockKey = lr->m_lockKeyUh48;

	// check tree
	int32_t slot = ht->getSlot ( &lockKey ); // lr->m_lockKeyUh48 );
	// put it here
	UrlLock *lock = NULL;
	// if there say no no
	if ( slot >= 0 ) lock = (UrlLock *)ht->getValueFromSlot ( slot );

	// if doing a remove operation and that was our hostid then unlock it
	if ( lr->m_removeLock && 
	     lock && 
	     lock->m_hostId == hostId &&
	     lock->m_lockSequence == lr->m_lockSequence ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// ok, at this point all remove ops return
	if ( lr->m_removeLock ) {
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}

	/////////
	//
	// add new lock
	//
	/////////


	// if lock > 1 hour old then remove it automatically!!
	if ( lock && nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
		// note it for now
		log("spider: removing lock after %"INT32" seconds "
		    "for lockKey=%"UINT64" hid=%"INT32"",
		    (nowGlobal - lock->m_timestamp),
		    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// if lock still there, do not grant another lock
	if ( lock ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: refusing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		reply[0] = 0;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}
	// make the new lock
	UrlLock tmp;
	tmp.m_hostId       = hostId;
	tmp.m_lockSequence = lr->m_lockSequence;
	tmp.m_timestamp    = nowGlobal;
	tmp.m_expires      = 0;
	tmp.m_firstIp      = lr->m_firstIp;
	tmp.m_collnum      = lr->m_collnum;

	// when the spider returns we remove its lock on reception of the
	// spiderReply, however, we actually just set the m_expires time
	// to 5 seconds into the future in case there is a current request
	// to get a lock for that url in progress. but, we do need to
	// indicate that the spider has indeed completed by setting
	// m_spiderOutstanding to true. this way, addToWaitingTree() will
	// not count it towards a "max spiders per IP" quota when deciding
	// on if it should add a new entry for this IP.
	tmp.m_spiderOutstanding = true;
	// this is set when all hosts in the group (shard) have granted the
	// lock and the host sends out a confirmLockAcquisition() request.
	// until then we do not know if the lock will be granted by all hosts
	// in the group (shard)
	tmp.m_confirmed    = false;

	// put it into the table
	if ( ! ht->addKey ( &lockKey , &tmp ) ) {
		// return error if that failed!
		us->sendErrorReply ( udpSlot , g_errno );
		return;
	}
	// note it for now
	if ( g_conf.m_logDebugSpider )
		log("spider: granting lock for lockKey=%"UINT64" hid=%"INT32"",
		    lr->m_lockKeyUh48,hostId);
	// grant the lock
	reply[0] = 1;
	us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
	return;
}

// hostId is the remote hostid sending us the lock request
void removeExpiredLocks ( int32_t hostId ) {
	// when we last cleaned them out
	static time_t s_lastTime = 0;

	int32_t nowGlobal = getTimeGlobalNoCore();

	// only do this once per second at the most
	if ( nowGlobal <= s_lastTime ) return;

	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

 restart:

	// scan the slots
	int32_t ns = ht->m_numSlots;
	// . clean out expired locks...
	// . if lock was there and m_expired is up, then nuke it!
	// . when Rdb.cpp receives the "fake" title rec it removes the
	//   lock, only it just sets the m_expired to a few seconds in the
	//   future to give the negative doledb key time to be absorbed.
	//   that way we don't repeat the same url we just got done spidering.
	// . this happens when we launch our lock request on a url that we
	//   or a twin is spidering or has just finished spidering, and
	//   we get the lock, but we avoided the negative doledb key.
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// if collnum got deleted or reset
		collnum_t collnum = lock->m_collnum;
		if ( collnum >= g_collectiondb.m_numRecs ||
		     ! g_collectiondb.m_recs[collnum] ) {
			log("spider: removing lock from missing collnum "
			    "%"INT32"",(int32_t)collnum);
			goto nuke;
		}
		// skip if not yet expired
		if ( lock->m_expires == 0 ) continue;
		if ( lock->m_expires >= nowGlobal ) continue;
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock after waiting. elapsed=%"INT32"."
			    " lockKey=%"UINT64" hid=%"INT32" expires=%"UINT32" "
			    "nowGlobal=%"UINT32"",
			    (nowGlobal - lock->m_timestamp),
			    lockKey,hostId,
			    (uint32_t)lock->m_expires,
			    (uint32_t)nowGlobal);
	nuke:
		// nuke the slot and possibly re-chain
		ht->removeSlot ( i );
		// gotta restart from the top since table may have shrunk
		goto restart;
	}
	// store it
	s_lastTime = nowGlobal;
}		

/////////////////////////
/////////////////////////      PAGESPIDER
/////////////////////////

// don't change name to "State" cuz that might conflict with another
class State11 {
public:
	int32_t          m_numRecs;
	Msg5          m_msg5;
	RdbList       m_list;
	TcpSocket    *m_socket;
	HttpRequest   m_r;
	collnum_t     m_collnum;
	char         *m_coll;
	int32_t          m_count;
	key_t         m_startKey;
	key_t         m_endKey;
	int32_t          m_minRecSizes;
	bool          m_done;
	SafeBuf       m_safeBuf;
	int32_t          m_priority;
};

static bool loadLoop ( class State11 *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the urls we got in doledb
// . doledb is sorted by priority complement then spider time
// . do not show urls in doledb whose spider time has not yet been reached,
//   so only show the urls spiderable now
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSpiderdb ( TcpSocket *s , HttpRequest *r ) {
	// set up a msg5 and RdbLists to get the urls from spider queue
	State11 *st ;
	try { st = new (State11); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageSpiderdb: new(%i): %s", 
		    (int)sizeof(State11),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State11) , "PageSpiderdb" );
	// get the priority/#ofRecs from the cgi vars
	st->m_numRecs  = r->getLong ("n", 20  );
	st->m_r.copy ( r );
	// get collection name
	char *coll = st->m_r.getString ( "c" , NULL , NULL );
	// get the collection record to see if they have permission
	//CollectionRec *cr = g_collectiondb.getRec ( coll );

	// the socket read buffer will remain until the socket is destroyed
	// and "coll" points into that
	st->m_coll = coll;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;
	// set socket for replying in case we block
	st->m_socket = s;
	st->m_count = 0;
	st->m_priority = MAX_SPIDER_PRIORITIES - 1;
	// get startKeys/endKeys/minRecSizes
	st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
	st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
	st->m_minRecSizes = 20000;
	st->m_done        = false;
	// returns false if blocked, true otherwise
	return loadLoop ( st ) ;
}

static void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) ;
static bool sendPage        ( State11 *st );
static bool printList       ( State11 *st );

bool loadLoop ( State11 *st ) {
 loop:
	// let's get the local list for THIS machine (use msg5)
	if ( ! st->m_msg5.getList  ( RDB_DOLEDB          ,
				     st->m_collnum       ,
				     &st->m_list         ,
				     st->m_startKey      ,
				     st->m_endKey        ,
				     st->m_minRecSizes   ,
				     true                , // include tree
				     false               , // add to cache
				     0                   , // max age
				     0                   , // start file #
				     -1                  , // # files
				     st                  , // callback state
				     gotListWrapper3     ,
				     0                   , // niceness
				     true               )) // do err correction
		return false;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return true;
	}
	// otherwise, load more
	goto loop;
}

void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// cast it
	State11 *st = (State11 *)state;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return;
	}
	// otherwise, load more
	loadLoop( (State11 *)state );
}


// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool printList ( State11 *st ) {
	// useful
	time_t nowGlobal ;
	if ( isClockInSync() ) nowGlobal = getTimeGlobal();
	else                   nowGlobal = getTimeLocal();
	// print the spider recs we got
	SafeBuf *sbTable = &st->m_safeBuf;
	// shorcuts
	RdbList *list = &st->m_list;
	// row count
	int32_t j = 0;
	// put it in there
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// stop if we got enough
		if ( st->m_count >= st->m_numRecs )  break;
		// get the doledb key
		key_t dk = list->getCurrentKey();
		// update to that
		st->m_startKey = dk;
		// inc by one
		st->m_startKey += 1;
		// get spider time from that
		int32_t spiderTime = g_doledb.getSpiderTime ( &dk );
		// skip if in future
		if ( spiderTime > nowGlobal ) continue;
		// point to the spider request *RECORD*
		char *rec = list->getCurrentData();
		// skip negatives
		if ( (dk.n0 & 0x01) == 0 ) continue;
		// count it
		st->m_count++;
		// what is this?
		if ( list->getCurrentRecSize() <= 16 ) { char *xx=NULL;*xx=0;}
		// sanity check. requests ONLY in doledb
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec )) {
			log("spider: not printing spiderreply");
			continue;
			//char*xx=NULL;*xx=0;
		}
		// get the spider rec, encapsed in the data of the doledb rec
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// print it into sbTable
		if ( ! sreq->printToTable ( sbTable,"ready",NULL,j))
			return false;
		// count row
		j++;
	}
	// need to load more?
	if ( st->m_count >= st->m_numRecs ||
	     // if list was a partial, this priority is short then
	     list->getListSize() < st->m_minRecSizes ) {
		// . try next priority
		// . if below 0 we are done
		if ( --st->m_priority < 0 ) st->m_done = true;
		// get startKeys/endKeys/minRecSizes
		st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
		st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
		// if we printed something, print a blank line after it
		if ( st->m_count > 0 )
			sbTable->safePrintf("<tr><td colspan=30>..."
					    "</td></tr>\n");
		// reset for each priority
		st->m_count = 0;
	}


	return true;
}

bool sendPage ( State11 *st ) {
	// sanity check
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	//SafeBuf sb; sb.safePrintf("Error = %s",mstrerror(g_errno));

	// shortcut
	SafeBuf *sbTable = &st->m_safeBuf;

	// generate a query string to pass to host bar
	char qs[64]; sprintf ( qs , "&n=%"INT32"", st->m_numRecs );

	// store the page in here!
	SafeBuf sb;
	sb.reserve ( 64*1024 );

	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r , qs );


	// get spider coll
	collnum_t collnum = g_collectiondb.getCollnum ( st->m_coll );
	// and coll rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );

	if ( ! cr ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	// print reason why spiders are not active for this collection
	int32_t tmp2;
	SafeBuf mb;
	if ( cr ) getSpiderStatusMsg ( cr , &mb , &tmp2 );
	if ( mb.length() && tmp2 != SP_INITIALIZING )
		sb.safePrintf(//"<center>"
			      "<table cellpadding=5 "
			      //"style=\""
			      //"border:2px solid black;"
			      "max-width:600px\" "
			      "border=0"
			      ">"
			      "<tr>"
			      //"<td bgcolor=#ff6666>"
			      "<td>"
			      "For collection <i>%s</i>: "
			      "<b><font color=red>%s</font></b>"
			      "</td>"
			      "</tr>"
			      "</table>\n"
			      , cr->m_coll
			      , mb.getBufStart() );


	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			//"<center>"
			"<b>Currently Spidering on This Host</b>"
			" (%"INT32" spiders)"
			//" (%"INT32" locks)"
			//"</center>"
			"</td></tr>\n"
			, TABLE_STYLE
			, (int32_t)g_spiderLoop.m_numSpidersOut
			//, g_spiderLoop.m_lockTable.m_numSlotsUsed
			);
	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb , true ) ) return false;
	// shortcut
	XmlDoc **docs = g_spiderLoop.m_docs;
	// count # of spiders out
	int32_t j = 0;
	// first print the spider recs we are spidering
	for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
		// get it
		XmlDoc *xd = docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// sanity check
		if ( ! xd->m_sreqValid ) { char *xx=NULL;*xx=0; }
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		char *status = xd->m_statusMsg;
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}
	// now print the injections as well!
	XmlDoc *xd = getInjectHead ( ) ;
	for ( ; xd ; xd = xd->m_nextInject ) {
		// how does this happen?
		if ( ! xd->m_sreqValid ) continue;
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		SafeBuf xb;
		xb.safePrintf("[<font color=red><b>injecting</b></font>] %s",
			      xd->m_statusMsg);
		char *status = xb.getBufStart();
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}

	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// then spider collection
	//SpiderColl *sc = g_spiderCache.m_spiderColls[collnum];
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);


	//
	// spiderdb rec stats, from scanning spiderdb
	//

	// if not there, forget about it
	if ( sc ) sc->printStats ( sb );

	//
	// Spiders Table
	//
	int64_t totalPoints = g_stats.m_totalSpiderSuccessNew +
				g_stats.m_totalSpiderErrorsNew +
				g_stats.m_totalSpiderSuccessOld +
				g_stats.m_totalSpiderErrorsOld;
	int64_t totalNew = g_stats.m_totalSpiderSuccessNew +
			     g_stats.m_totalSpiderErrorsNew;
	int64_t totalOld = g_stats.m_totalSpiderSuccessOld +
			     g_stats.m_totalSpiderErrorsOld;
	double tsr = 100.00;
	double nsr = 100.00;
	double osr = 100.00;
	if ( totalPoints > 0 ) {
		tsr = 100.00*
			(double)(g_stats.m_totalSpiderSuccessNew +
				 g_stats.m_totalSpiderSuccessOld) /
			(double)totalPoints;
		if ( totalNew > 0 )
			nsr= 100.00*(double)(g_stats.m_totalSpiderSuccessNew) /
				     (double)(totalNew);
		if ( totalOld > 0 )
			osr= 100.00*(double)(g_stats.m_totalSpiderSuccessOld) /
				     (double)(totalOld);
	}
	int32_t points = g_stats.m_spiderSample;
	if ( points > 1000 ) points = 1000;
	int32_t sampleNew = g_stats.m_spiderNew;
	int32_t sampleOld = points - g_stats.m_spiderNew;
	double tssr = 100.00;
	double nssr = 100.00;
	double ossr = 100.00;
	if ( points > 0 ) {
		tssr = 100.00*
			(double)(points -
				 g_stats.m_spiderErrors) / (double)points ;
		if ( sampleNew > 0 )
			nssr = 100.00*(double)(sampleNew -
					       g_stats.m_spiderErrorsNew) /
				      (double)(sampleNew);
		if ( sampleOld > 0 )
			ossr = 100.00*(double)(sampleOld -
					       (g_stats.m_spiderErrors -
						g_stats.m_spiderErrorsNew)) /
				      (double)(sampleOld);
	}

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );

	sb.safePrintf (

		       "<table %s>"
		       "<tr>"
		       "<td colspan=7>"
		       "<center><b>Spider Stats</b></td></tr>\n"
		       "<tr bgcolor=#%s><td>"
		       "</td><td><b>Total</b></td>"
		       "<td><b>Total New</b></td>"
		       "<td><b>Total Old</b></td>"
		       "<td><b>Sample</b></td>"
		       "<td><b>Sample New</b></td>"
		       "<td><b>Sample Old</b></b>"
		       "</td></tr>"

		       "<tr class=poo><td><b>Total Spiders</n>"
		       "</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       "</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       //"<tr class=poo><td><b>Successful Spiders</n>"
		       //"</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       //"</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       //"<tr class=poo><td><b>Failed Spiders</n>"
		       //"</td><td>%"INT64"</td><td>%"INT64"</td><td>%"INT64"</td>\n"
		       //"</td><td>%"INT32"</td><td>%"INT32"</td><td>%"INT32"</td></tr>\n"
		       "<tr class=poo><td><b>Success Rate</b>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td></tr>",
		       TABLE_STYLE,  
		       DARK_BLUE,
		       totalPoints,
		       totalNew,
		       totalOld,
		       points,
		       sampleNew,
		       sampleOld,

		       //g_stats.m_totalSpiderSuccessNew +
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_totalSpiderSuccessNew,
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_spiderSuccessNew +
		       //g_stats.m_spiderSuccessOld,
		       //g_stats.m_spiderSuccessNew,
		       //g_stats.m_spiderSuccessOld,

		       //g_stats.m_totalSpiderErrorsNew +
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_totalSpiderErrorsNew,
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_spiderErrorsNew +
		       //g_stats.m_spiderErrorsOld,
		       //g_stats.m_spiderErrorsNew,
		       //g_stats.m_spiderErrorsOld,

		       tsr, nsr, osr, tssr, nssr, ossr );

	int32_t bucketsNew[65536];
	int32_t bucketsOld[65536];
	memset ( bucketsNew , 0 , 65536*4 );
	memset ( bucketsOld , 0 , 65536*4 );
	for ( int32_t i = 0 ; i < points; i++ ) {
		int32_t n = g_stats.m_errCodes[i];
		if ( n < 0 || n > 65535 ) {
			log("admin: Bad spider error code.");
			continue;
		}
		if ( g_stats.m_isSampleNew[i] )
			bucketsNew[n]++;
		else
			bucketsOld[n]++;
	}
	for ( int32_t i = 0 ; i < 65536 ; i++ ) {
		if ( g_stats.m_allErrorsNew[i] == 0 &&
		     g_stats.m_allErrorsOld[i] == 0 &&
		     bucketsNew[i] == 0 && bucketsOld[i] == 0 ) continue;
		sb.safePrintf (
			       "<tr bgcolor=#%s>"
			       "<td><b><a href=/search?c=%s&q=gbstatusmsg%%3A"
			       "%%22"
			       ,
			       LIGHT_BLUE , cr->m_coll );
		sb.urlEncode(mstrerror(i));
		sb.safePrintf ("%%22>"
			       "%s"
			       "</a>"
			       "</b></td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT64"</td>"
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "<td>%"INT32"</td>"
			       "</tr>\n" ,
			       mstrerror(i),
			       g_stats.m_allErrorsNew[i] +
			       g_stats.m_allErrorsOld[i],
			       g_stats.m_allErrorsNew[i],
			       g_stats.m_allErrorsOld[i],
			       bucketsNew[i] + bucketsOld[i] ,
			       bucketsNew[i] ,
			       bucketsOld[i] );
	}

	sb.safePrintf ( "</table><br>\n" );



	// describe the various parms
	/*
	sb.safePrintf ( 
		       "<table width=100%% bgcolor=#%s "
		       "cellpadding=4 border=1>"
		       "<tr class=poo>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<b>Field descriptions</b>"
		       "</td>"
		       "</tr>\n"
		       "<tr class=poo>"
		       "<td>hits</td><td>The number of  attempts that were "
		       "made by the spider to read a url from the spider "
		       "queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>misses</td><td>The number of those attempts that "
		       "failed to get a url to spider.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>cached</td><td>The number of urls that are "
		       "currently in the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>water</td><td>The number of urls that were in the "
		       "spider queue cache at any one time, since the start "
		       "of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>kicked</td><td>The number of urls that were "
		       "replaced in the spider queue cache with urls loaded "
		       "from disk, since the start of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>added</td><td>The number of urls that were added "
		       "to the spider queue cache since the start of the last "
		       "disk scan. After a document is spidered its url "
		       "if often added again to the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>attempted</td><td>The number of urls that "
		       "Gigablast attempted to add to the spider queue cache "
		       "since the start of the last disk scan. In "
		       "a distributed environment, urls are distributed "
		       "between twins so not all urls read will "
		       "make it into the spider queue cache. Also includes "
		       "spider recs attempted to be re-added to spiderdb "
		       "after being spidering, but usually with a different "
		       "spider time.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>nl</td><td>This is 1 iff Gigablast currently "
		       "needs to reload the spider queue cache from disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>rnl</td><td>This is 1 iff Gigablast currently "
		       "really needs to reload the spider queue cache from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>more</td><td>This is 1 iff there are urls on "
		       "the disk that are not in the spider queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>loading</td><td>This is 1 iff Gigablast is "
		       "currently loading this spider cache queue from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>scanned</td><td>The number of bytes that were "
		       "read from disk since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>reads</td><td>The number of disk read "
		       "operations since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>elapsed</td><td>The time in seconds that has "
		       "elapsed since the start or end of the last disk "
		       "scan, depending on if a scan is currently in "
		       "progress.</td>"
		       "</tr>\n"

		       "</table>\n",

		       LIGHT_BLUE ,
		       DARK_BLUE  );
	*/

	// done if no sc
	if ( ! sc ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	/////
	//
	// READY TO SPIDER table
	//
	/////

	int32_t ns = 0;
	if ( sc ) ns = sc->m_doleIpTable.getNumSlotsUsed();

	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>URLs Ready to Spider for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			" (%"INT32" ips in doleiptable)"
			,
			TABLE_STYLE,
			st->m_coll ,
			ns );

	// print time format: 7/23/1971 10:45:32
	time_t nowUTC = getTimeGlobal();
	struct tm *timeStruct ;
	char time[256];
	timeStruct = gmtime ( &nowUTC );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb.safePrintf("</b>" //  (current time = %s = %"UINT32") "
		      "</td></tr>\n" 
		      //,time,nowUTC
		      );

	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb ,false ) ) return false;
	// the the doledb spider recs
	char *bs = sbTable->getBufStart();
	if ( bs && ! sb.safePrintf("%s",bs) ) return false;
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );



	/////////////////
	//
	// PRINT WAITING TREE
	//
	// each row is an ip. print the next url to spider for that ip.
	//
	/////////////////
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>IPs Waiting for Selection Scan for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			,
			TABLE_STYLE,
			st->m_coll );
	// print time format: 7/23/1971 10:45:32
	int64_t timems = gettimeofdayInMillisecondsGlobal();
	sb.safePrintf("</b> (current time = %"UINT64")(totalcount=%"INT32")"
		      "(waittablecount=%"INT32")",
		      timems,
		      sc->m_waitingTree.getNumUsedNodes(),
		      sc->m_waitingTable.getNumUsedSlots());

	double a = (double)g_spiderdb.getUrlHash48 ( &sc->m_firstKey );
	double b = (double)g_spiderdb.getUrlHash48 ( &sc->m_endKey );
	double c = (double)g_spiderdb.getUrlHash48 ( &sc->m_nextKey );
	double percent = (100.0 * (c-a)) ;
	if ( b-a > 0 ) percent /= (b-a);
	if ( percent > 100.0 ) percent = 100.0;
	if ( percent < 0.0 ) percent = 0.0;
	sb.safePrintf("(spiderdb scan for ip %s is %.2f%% complete)",
		      iptoa(sc->m_scanningIp),
		      (float)percent );

	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("<tr bgcolor=#%s>",DARK_BLUE);
	sb.safePrintf("<td><b>spidertime (MS)</b></td>\n");
	sb.safePrintf("<td><b>firstip</b></td>\n");
	sb.safePrintf("</tr>\n");
	// the the waiting tree
	int32_t node = sc->m_waitingTree.getFirstNode();
	int32_t count = 0;
	//uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
	for ( ; node >= 0 ; node = sc->m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)sc->m_waitingTree.getKey(node);
		// get ip from that
		int32_t firstIp = (key->n0) & 0xffffffff;
		// get the time
		uint64_t spiderTimeMS = key->n1;
		// shift upp
		spiderTimeMS <<= 32;
		// or in
		spiderTimeMS |= (key->n0 >> 32);
		char *note = "";
		// if a day more in the future -- complain
		// no! we set the repeat crawl to 3000 days for crawl jobs that
		// do not repeat...
		// if ( spiderTimeMS > nowMS + 1000 * 86400 )
		// 	note = " (<b><font color=red>This should not be "
		// 		"this far into the future. Probably a corrupt "
		// 		"SpiderRequest?</font></b>)";
		// get the rest of the data
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td>%"INT64"%s</td>"
			      "<td>%s</td>"
			      "</tr>\n",
			      LIGHT_BLUE,
			      (int64_t)spiderTimeMS,
			      note,
			      iptoa(firstIp));
		// stop after 20
		if ( ++count == 20 ) break;
	}
	// ...
	if ( count ) 
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td colspan=10>...</td></tr>\n",
			      LIGHT_BLUE);
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// get the socket
	TcpSocket *s = st->m_socket;
	// then we can nuke the state
	mdelete ( st , sizeof(State11) , "PageSpiderdb" );
	delete (st);
	// erase g_errno for sending
	g_errno = 0;
	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(),sb.length() );
}

///////////////////////////////////
//
// URLFILTERS
//
///////////////////////////////////

/*
// assign these a value of 1 in s_table hashtable
static char *s_ypSites[] = {
	"www.yellow.com",
	"www.yellowpages.com",
	"www.dexknows.com",
	"yellowpages.aol.com",
	"www.superpages.com",
	"citysearch.com",
	"www.yellowbook.com",
	"www.magicyellow.com",
	"home.digitalcity.com",
	"www.switchboard.com",
	"cityguide.aol.com",
	"www.bizrate.com",
	"www.restaurantica.com",
	"www.insiderpages.com",
	"local.yahoo.com"
};

// . assign these a value of 2 in s_table hashtable
// . mwells@g0:/y$ cat gobyout  | awk '{print $4}' | grep -v goby.com | grep -vi goby | grep -v google.com | grep -v mappoint | urlinfo | grep "host: " | awk '{print $2}' | sort | uniq > foo
// . then take the top linked to sites on goby and print out for direct
//   insertion into this file:

// then get the popular domains from THAT list:
// mwells@g0:/y$ cat foo | awk '{print $2}' | urlinfo | grep "dom: " | awk '{print $2}' | sort | uniq -c | sort > foodom

static char *s_aggSites[] = {
	"isuwmsrugby.tripod.com",
	"meyerlemon.eventbrite.com",
	"miami.tourcorp.com",
	"valentinesdaydatenightcoupleschi.eventbrite.com",
	"volcano.si.edu",
	"webpages.csus.edu",
	"weddingextravaganza.eventbrite.com",
	"www.alliancerugby.org",
	"www.asuwrfc.com",
	"www.btpd.org",
	"www.chicagodragons.org",
	"www.chsgeorgia.org",
	"www.derugbyfoundation.org",
	"www.foxborosportscenter.com",
	"www.lynn.edu",
	"www.owensboroparks.org",
	"www.scitrek.org",
	"www.southcarolinaparks.com",
	"www.usbr.gov",
	"dummil.eventbrite.com",
	"jacksonvilleantiqueshow.eventbrite.com",
	"kidsfest.eventbrite.com",
	"piuvalentine.eventbrite.com",
	"www.anytimefitness.com",
	"www.dumbartonhouse.org",
	"www.lsurugby.com",
	"www.maliburugby.com",
	"www.pitsrugby.com",
	"www.renegaderugby.org",
	"www.rotor.com",
	"www.rugbyrats.com",
	"www.sanjoserugby.com",
	"www.seattleartists.com",
	"www.sixflags.com",
	"www.vacavillesports.com",
	"atlcomedyfest.eventbrite.com",
	"easyweekdaycooking.eventbrite.com",
	"hartford.citysearch.com",
	"healthythaicooking.eventbrite.com",
	"hicaregiversconference.eventbrite.com",
	"skiing.alpinezone.com",
	"spirit.lib.uconn.edu",
	"springfield.ettractions.com",
	"tomatofest2011.eventbrite.com",
	"www.abc-of-meditation.com",
	"www.amf.com",
	"www.atlantaharlequins.com",
	"www.chicagoparkdistrict.com",
	"www.denverwildfirerfc.org",
	"www.gowaterfalling.com",
	"www.harlequins.org",
	"www.ignatius.org",
	"www.masmacon.com",
	"www.palmbeachrugby.org",
	"www.riversiderugby.com",
	"www.rmne.org",
	"www.thehilliard.org",
	"www.woodsmenrugby.com",
	"devildoll.eventbrite.com",
	"iexpectcrabfeedfundraiser.eventbrite.com",
	"sports.groups.yahoo.com",
	"valentinesdaycookingwithlove.eventbrite.com",
	"www.agisamazing.com",
	"www.ascendinglotus.com",
	"www.auduboninstitute.org",
	"www.azrugbyref.com",
	"www.blackicerugby.com",
	"www.bluegrassmuseum.org",
	"www.krewerugby.com",
	"www.lamorugby.com",
	"www.lsue.edu",
	"www.norwichrink.com",
	"www.ombac.org",
	"www.sdarmada.org",
	"www.sirensrugby.com",
	"www.tampabarbarians.org",
	"www.travellanecounty.org",
	"www.visit-newhampshire.com",
	"hawaii.tourcorp.com",
	"tasteofkorea.eventbrite.com",
	"www.ballyfitness.com",
	"www.calpolyrugby.com",
	"www.destateparks.com",
	"www.eaa.org",
	"www.goldsgym.com",
	"www.gonzagarugby.com",
	"www.greatexplorations.org",
	"www.heparks.org",
	"www.imagisphere.org",
	"www.jeffdavis.org",
	"www.park.granitecity.com",
	"www.poets.org",
	"www.regis.edu",
	"www.verizoncenter.com",
	"mybridalsale.eventbrite.com",
	"pigandsausagetoo.eventbrite.com",
	"www.gaelrugby.com",
	"www.independent.com",
	"www.kohlchildrensmuseum.org",
	"www.operaamerica.org",
	"www.recration.du.edu",
	"www.symmetricalskatingschool.org",
	"www.telcomhistory.org",
	"www.texasoutside.com",
	"reagan.eureka.edu",
	"stampede2011.eventbrite.com",
	"synergy2011.eventbrite.com",
	"theexperience2011.eventbrite.com",
	"www.24hourfitness.com",
	"www.dematha.org",
	"www.facebook.com",
	"www.iaapa.org",
	"www.icelandrestoration.com",
	"www.louisvillewomensrugby.com",
	"www.manchesterrunningcompany.com",
	"www.moaonline.org",
	"www.pvicechalet.com",
	"www.rendlake.com",
	"attinuptown.eventbrite.com",
	"chocolateanddessertfantasy.eventbrite.com",
	"colorado.ettractions.com",
	"longbeachstaterugby.webs.com",
	"volcano.oregonstate.edu",
	"www.columbiaspacescience.org",
	"www.eventful.com",
	"eventful.com",
	"www.newmexico.org",
	"www.rmparks.org",
	"www.sbyouthrugby.org",
	"www.venturacountyrugbyclub.com",
	"www.wheatonicearena.com",
	"faithorigins.eventbrite.com",
	"jerseyshore.metromix.com",
	"stlouis.citysearch.com",
	"valentinesdaydatenightcooking.eventbrite.com",
	"www.floridarugbyunion.com",
	"www.rugbyatucf.com",
	"www.stingrayrugby.com",
	"www.usfbullsrugby.com",
	"atlanta.going.com",
	"klsnzwineday.eventbrite.com",
	"losangeles.citysearch.com",
	"sourdough.eventbrite.com",
	"valentinesdaygourmetdating.eventbrite.com",
	"web.mit.edu",
	"www.airmuseum.org",
	"www.eparugby.org",
	"www.navicache.com",
	"www.siliconvalleyrugby.org",
	"www.yale.edu",
	"rhodeisland.ettractions.com",
	"studentorgs.vanderbilt.edu",
	"www.jaxrugby.org",
	"www.orlandomagazine.com",
	"www.plnurugby.com",
	"www.recreation.du.edu",
	"www.riversideraptors.com",
	"www.usarchery.org",
	"cacspringfling.eventbrite.com",
	"dallas.going.com",
	"groups.northwestern.edu",
	"hpualumniiphonelaunchparty.eventbrite.com",
	"juliachild.eventbrite.com",
	"southbaysciencesymposium2011.eventbrite.com",
	"www.curugby.com",
	"www.everyoneruns.net",
	"www.glendalerugby.com",
	"www.phantomsyouthrugby.org",
	"www.usdrugby.com",
	"10000expo-sponsoship-nec.eventbrite.com",
	"greenville.metromix.com",
	"spssan.eventbrite.com",
	"www.cmaathletics.org",
	"www.csulb.edu",
	"www.doralrugby.com",
	"www.neworleansrugbyclub.com",
	"www.sos.louisiana.gov",
	"www.southbayrugby.org",
	"www.travelnevada.com",
	"www.uicrugbyclub.org",
	"www.atlantabucksrugby.org",
	"www.dinodatabase.com",
	"www.fest21.com",
	"www.georgiatechrugby.com",
	"www.gsuwomensrugby.com",
	"www.siuwomensrugby.com",
	"www.snowtracks.com",
	"www.trainweb.com",
	"www.visitnebraska.gov",
	"www.visitsanantonio.com",
	"hometown.aol.com",
	"next2normal.eventbrite.com",
	"sixmonthpassatlanta2011.eventbrite.com",
	"winejazz2.eventbrite.com",
	"www.amityrugby.org",
	"www.meetandplay.com",
	"www.miami.edu",
	"www.miamirugby.com",
	"www.phillipscollection.org",
	"www.tridentsrugby.com",
	"wwwbloggybootcampsandiego.eventbrite.com",
	"whale-watching.gordonsguide.com",
	"www.culturemob.com",
	"www.denver-rugby.com",
	"www.hillwoodmuseum.org",
	"www.peabody.yale.edu",
	"www.yoursciencecenter.com",
	"newyorkcity.ettractions.com",
	"rawfoodcert.eventbrite.com",
	"www.discoverydepot.org",
	"www.dukecityrugbyclub.com",
	"www.jazztimes.com",
	"www.kissimmeeairmuseum.com",
	"www.southstreetseaportmuseum.org",
	"www.wsbarbariansrugby.com",
	"beerunch2011.eventbrite.com",
	"milwaukee.ettractions.com",
	"seminoletampa.casinocity.com",
	"silveroak.eventbrite.com",
	"tsunamifitclub.eventbrite.com",
	"walking-tours.gordonsguide.com",
	"www.alamedarugby.com",
	"www.atshelicopters.com",
	"www.camelbackrugby.com",
	"www.dlshs.org",
	"www.eteamz.com",
	"newyork.ettractions.com",
	"www.allaboutrivers.com",
	"www.childrensmuseumatl.org",
	"www.hartfordroses.org",
	"www.nationalparks.org",
	"www.seahawkyouthrugby.com",
	"www.skiingthebackcountry.com",
	"epcontinental.eventbrite.com",
	"healthandwellnessshow.eventbrite.com",
	"www.apopkamuseum.org",
	"www.condorsrugby.com",
	"www.dcr.virginia.gov",
	"www.diabloyouthrugby.org",
	"www.rockandice.com",
	"honolulu.metromix.com",
	"mowcrabfeed2011.eventbrite.com",
	"ptt-superbowl.eventbrite.com",
	"whitewater-rafting.gordonsguide.com",
	"winearomatraining.eventbrite.com",
	"www.broadway.com",
	"www.usc.edu",
	"www.gatorrugby.com",
	"www.iumudsharks.net",
	"www.scrrs.net",
	"www.sfggrugby.com",
	"www.unco.edu",
	"hctmspring2011conference.eventbrite.com",
	"sandiego.going.com",
	"www.crt.state.la.us",
	"www.foodhistorynews.com",
	"www.lancerrugbyclub.org",
	"www.littlerockrugby.com",
	"www.sharksrugbyclub.com",
	"www.channelislandsice.com",
	"www.idealist.org",
	"www.mbtykesrugby.com",
	"katahdicon.eventbrite.com",
	"foodwineloversfestival.eventbrite.com",
	"maristeveningseries2011.eventbrite.com",
	"philadelphia.ettractions.com",
	"sugarrushla.eventbrite.com",
	"www.chicagolions.com",
	"www.skatingsafe.com",
	"www.themeparkinsider.com",
	"fremdcraftfairspring2011.eventbrite.com",
	"gorptravel.away.com",
	"minnesota.ettractions.com",
	"www.chicagohopeacademy.org",
	"www.fmcicesports.com",
	"www.kitebeaches.com",
	"www.mixedmartialarts.com",
	"www.slatermill.org",
	"www.sunnysideoflouisville.org",
	"www.visitrochester.com",
	"careshow.eventbrite.com",
	"massachusetts.ettractions.com",
	"edwardianla2011.eventbrite.com",
	"indianapolis.metromix.com",
	"www.pasadenamarathon.org",
	"washington.going.com",
	"www.sjquiltmuseum.org",
	"www.wannakitesurf.com",
	"fauwomensrugby.sports.officelive.com",
	"newhampshire.ettractions.com",
	"www.vcmha.org",
	"milwaukee.going.com",
	"phoenix.going.com",
	"www.anrdoezrs.net",
	"www.temperugby.com",
	"pampermefabulous2011.eventbrite.com",
	"www.napavalleyvineyards.org",
	"r4k11.eventbrite.com",
	"ramonamusicfest.eventbrite.com",
	"www.abc-of-rockclimbing.com",
	"www.geocities.com",
	"jackson.metromix.com",
	"www.santamonicarugby.com",
	"cleveland.metromix.com",
	"lancaster.ettractions.com",
	"www.fortnet.org",
	"www.horseandtravel.com",
	"www.pubcrawler.com",
	"kdwp.state.ks.us",
	"www.berkeleyallblues.com",
	"www.liferugby.com",
	"www.socalmedicalmuseum.org",
	"www.dcsm.org",
	"www.sutler.net",
	"desmoines.metromix.com",
	"www.cavern.com",
	"www.dotoledo.org",
	"www.fws.gov",
	"www.ghosttowngallery.com",
	"www.museumamericas.org",
	"www.museumsofboston.org",
	"www.northshorerugby.com",
	"geocaching.gpsgames.org",
	"www.americaeast.com",
	"www.cwrfc.org",
	"www.jewelryshowguide.com",
	"www.livelytimes.com",
	"www.pascorugbyclub.com",
	"www.westminsterice.com",
	"www.claremontrugby.org",
	"www.jugglingdb.com",
	"www.metalblade.com",
	"www.preservationnation.org",
	"sofla2011.eventbrite.com",
	"www.belmonticeland.com",
	"www.dropzone.com",
	"www.smecc.org",
	"www.studentgroups.ucla.edu",
	"www.visitdetroit.com",
	"honolulu.going.com",
	"sippingandsaving5.eventbrite.com",
	"www.connecticutsar.org",
	"www.guestranches.com",
	"www.nvtrailmaps.com",
	"www.visitnh.gov",
	"illinois.ettractions.com",
	"www.spymuseum.org",
	"www.ci.riverside.ca.us",
	"www.hbnews.us",
	"www.santaclarayouthrugby.com",
	"www.thestranger.com",
	"www.freewebs.com",
	"www.miamirugbykids.com",
	"www.mtwashingtonvalley.org",
	"www.ocbucksrugby.com",
	"bridalpaloozala.eventbrite.com",
	"maps.yahoo.com",
	"www.azstateparks.com",
	"www.paywindowpro.com",
	"www.rowadventures.com",
	"parksandrecreation.idaho.gov",
	"www.artsmemphis.org",
	"www.lasvegasweekly.com",
	"www.redmountainrugby.org",
	"san-francisco.tourcorp.com",
	"www.khsice.com",
	"www.vansenusauto.com",
	"quinceanerasmagazineoc.eventbrite.com",
	"www.mvc-sports.com",
	"www.tbsa.com",
	"www.travelportland.com",
	"rtnpilgrim.eventbrite.com",
	"www.bigfishtackle.com",
	"www.centralmass.org",
	"cpca2011.eventbrite.com",
	"www.matadorrecords.com",
	"www.sebabluegrass.org",
	"prescott.showup.com",
	"vintagevoltage2011.eventbrite.com",
	"www.seattleperforms.com",
	"www.valleyskating.com",
	"resetbootcamp.eventbrite.com",
	"www.abc-of-mountaineering.com",
	"www.snocountry.com",
	"events.nytimes.com",
	"www.icecenter.net",
	"www.livefrommemphis.com",
	"www.pasadenarfc.com",
	"www.ucsdrugby.com",
	"uclaccim.eventbrite.com",
	"www.visitchesapeake.com",
	"www.natureali.org",
	"www.nordicskiracer.com",
	"www.nowplayingva.org",
	"www.sbcounty.gov",
	"www.seedesmoines.com",
	"www.world-waterfalls.com",
	"denver.going.com",
	"hearstmuseum.berkeley.edu",
	"www.lmurugby.com",
	"www.ftlrugby.com",
	"www.pelicanrugby.com",
	"rtnharthighschool.eventbrite.com",
	"www.visitri.com",
	"www.aba.org",
	"www.americaonice.us",
	"www.thecontemporary.org",
	"www.wherigo.com",
	"www.drtopo.com",
	"www.visitseattle.org",
	"calendar.dancemedia.com",
	"trips.outdoors.org",
	"www.chs.org",
	"www.myneworleans.com",
	"www.oaklandice.com",
	"nashville.metromix.com",
	"www.americangolf.com",
	"www.fossilmuseum.net",
	"www.oakparkparks.com",
	"www.visit-maine.com",
	"www.oregonlive.com",
	"www.allwashingtondctours.com",
	"www.wannadive.net",
	"www.sportsheritage.org",
	"hudsonvalley.metromix.com",
	"www.scificonventions.com",
	"www.wildernessvolunteers.org",
	"essencemusicfestival.eventbrite.com",
	"www.kitesurfatlas.com",
	"www.ndtourism.com",
	"valentinesgourmetdatingchicago.eventbrite.com",
	"www.fingerlakeswinecountry.com",
	"www.dmnh.org",
	"www.ticketnetwork.com",
	"partystroll.eventbrite.com",
	"www.bedandbreakfastnetwork.com",
	"www.sternmass.org",
	"www.visitnh.com",
	"www.places2ride.com",
	"www.hawaiieventsonline.com",
	"www.ucirugby.com",
	"www.gohawaii.com",
	"www.writersforum.org",
	"www.roadracingworld.com",
	"www.bigisland.org",
	"www.boatbookings.com",
	"www.lhs.berkeley.edu",
	"www.dnr.state.mn.us",
	"www.mostateparks.com",
	"www.historicnewengland.org",
	"www.waza.org",
	"www.backbayrfc.com",
	"newyork.metromix.com",
	"www.larebellion.org",
	"teetimes.golfhub.com",
	"10000expo-sponsoship-ceg.eventbrite.com",
	"10000expo-sponsor-bjm.eventbrite.com",
	"parks.ky.gov",
	"www.bostonusa.com",
	"www.visitbuffaloniagara.com",
	"www.sharksice.com",
	"2011burbankapprentice.eventbrite.com",
	"kansascity.ettractions.com",
	"www.bicycling.com",
	"www.cityofchino.org",
	"www.ridingworld.com",
	"www.whittierrugby.com",
	"10000bestjobsam.eventbrite.com",
	"www.adventurecentral.com",
	"www.earlymusic.org",
	"www.upcomingevents.com",
	"www.sleddogcentral.com",
	"www.capecodkidz.com",
	"www.collectorsguide.com",
	"www.cougarrugby.org",
	"www.sfvrugby.com",
	"strivetothrivepabcconf.eventbrite.com",
	"www.visithoustontexas.com",
	"www.authorstrack.com",
	"www.aboutgolfschools.org",
	"www.huntingspotz.com",
	"www.lib.az.us",
	"members.aol.com",
	"www.fs.fed.us",
	"www.ncarts.org",
	"www.vermonttravelplanner.org",
	//"www.scubadiving.com",
	"www.waterfallsnorthwest.com",
	"www.philadelphiausa.travel",
	"www.usgolfschoolguide.com",
	"njgin.state.nj.us",
	"www.artcards.cc",
	"www.rimonthly.com",
	"www.atlanta.net",
	"www.glacialgardens.com",
	"2011superbowlcruise.eventbrite.com",
	"swimming-with-dolphins.gordonsguide.com",
	"www.trackpedia.com",
	// why was this in there?
	//"www.dailyherald.com",
	"www.nhm.org",
	"boston.ettractions.com",
	"www.geneseefun.com",
	"www.travelsd.com",
	"www.golfbuzz.com",
	"www.in.gov",
	"cincinnati.metromix.com",
	"www.sanjose.com",
	"brevard.metromix.com",
	"www.dogsledrides.com",
	"www.orvis.com",
	"philadelphia.going.com",
	"twincities.metromix.com",
	"www.orlandorugby.com",
	"www.csufrugby.com",
	"www.larugby.com",
	"www.washingtonwine.org",
	"calendar.gardenweb.com",
	"gulfcoast.metromix.com",
	"florida.ettractions.com",
	"www.northeastwaterfalls.com",
	"www.computerhistory.org",
	"www.ct.gov",
	"www.hosteltraveler.com",
	"www.thinkrentals.com",
	"www.4x4trailhunters.com",
	"www.cityweekly.net",
	"www.yourrunning.com",
	"www.spasofamerica.com",
	"www.indoorclimbing.com",
	"www.utah.com",
	"boston.going.com",
	"minneapolisstpaul.ettractions.com",
	"www.coolrunning.com",
	"www.greensboronc.org",
	"www.michigan.org",
	"www.artfestival.com",
	"www.divespots.com",
	"www.oregonstateparks.org",
	"www.virginiawine.org",
	"www.morebeach.com",
	"www.minnesotamonthly.com",
	"www.texasescapes.com",
	"www.usatf.org",
	"www.findrentals.com",
	"www.hachettebookgroup.com",
	"www.racesonline.com",
	"www.usace.army.mil",
	"web.georgia.org",
	"detroit.metromix.com",
	"www.homebrewersassociation.org",
	"www.baltimore.org",
	"www.gastateparks.org",
	"www.arkansasstateparks.com",
	"www.visitlasvegas.com",
	"www.whenwerv.com",
	"www.chilicookoff.com",
	"www.bikeride.com",
	"www.eaglerockrugby.com",
	"www.pickwickgardens.com",
	"flagstaff.showup.com",
	"miami.going.com",
	"www.anchorage.net",
	"www.wlra.us",
	"www.thetrustees.org",
	"www.artnet.com",
	"www.mthoodterritory.com",
	"www.hihostels.com",
	"www.bfa.net",
	"www.flyins.com",
	"www.stepintohistory.com",
	"www.festing.com",
	"www.pursuetheoutdoors.com",
	"newyork.going.com",
	"www.fishingguidenetwork.com",
	"www.visit-massachusetts.com",
	"www.visitindy.com",
	"www.washingtonpost.com",
	"www.greatamericandays.com",
	"www.washingtonian.com",
	"national.citysearch.com",
	"www.infohub.com",
	"www.productionhub.com",
	"www.events.org",
	"www.traveliowa.com",
	"www.findmyadventure.com",
	"delaware.metromix.com",
	"www.marinmagazine.com",
	"us.penguingroup.com",
	"www.bicycletour.com",
	"www.travelok.com",
	"www.scububble.com",
	"www.childrensmuseums.org",
	"www.conventionscene.com",
	"www.scubaspots.com",
	"www.tnvacation.com",
	"stlouis.ettractions.com",
	"www.mxparks.com",
	"florida.greatestdivesites.com",
	"www.nowplayingaustin.com",
	"www.skinnyski.com",
	"www.sportoften.com",
	"www.zvents.com",
	"www.visitphoenix.com",
	"palmsprings.metromix.com",
	"upcoming.yahoo.com",
	"www.washington.org",
	"www.balloonridesacrossamerica.com",
	"www.playbill.com",
	"palmbeach.ettractions.com",
	"louisville.metromix.com",
	"www.animecons.com",
	"www.findanartshow.com",
	"www.usef.org",
	"www.villagevoice.com",
	"www.discovergold.org",
	"www.georgiaoffroad.com",
	"www.memphistravel.com",
	"dc.metromix.com",
	"www.aplf-planetariums.info",
	"www.skateisi.com",
	"www.usacycling.org",
	"www.wine-compass.com",
	"www.visitdelaware.com",
	"tucson.metromix.com",
	"www.happycow.net",
	"www.indiecraftshows.com",
	"www.gethep.net",
	"www.agritourismworld.com",
	"stlouis.metromix.com",
	"phoenix.metromix.com",
	"stream-flow.allaboutrivers.com",
	"www.festivalsandevents.com",
	"www.winemcgee.com",
	"www.aurcade.com",
	"www.visitjacksonville.com",
	"www.nashvillescene.com",
	"www.4x4trails.net",
	"www.americancraftmag.org",
	"blog.danceruniverse.com",
	"www.vacationrealty.com",
	"www.californiasciencecenter.org",
	"www.rollerhome.com",
	"www.atvsource.com",
	"www.hotairballooning.com",
	"www.freeskateparks.com",
	"www.ruralbounty.com",
	"connecticut.ettractions.com",
	"www.localattractions.com",
	"www.skategroove.com",
	"www.hawaiitours.com",
	"www.visitrhodeisland.com",
	"www.swac.org",
	"www.swimmingholes.org",
	"www.roadfood.com",
	"www.gotriadscene.com",
	"www.runnersworld.com",
	"www.outerquest.com",
	"www.seattleweekly.com",
	"www.onlyinsanfrancisco.com",
	"www.bikereg.com",
	"www.artslant.com",
	"www.louisianatravel.com",
	"www.operabase.com",
	"www.stepintoplaces.com",
	"www.vinarium-usa.com",
	"www.visitconnecticut.com",
	"www.abc-of-mountainbiking.com",
	"www.wannask8.com",
	"www.xcski.org",
	"www.active-days.org",
	"www.hawaiiactivities.com",
	"www.massvacation.com",
	"www.uspa.org",
	"miami.ettractions.com",
	"www.abc-of-hiking.com",
	"www.bestofneworleans.com",
	"www.phillyfunguide.com",
	"www.beermonthclub.com",
	"www.newenglandwaterfalls.com",
	"www.lake-link.com",
	"www.festivalfinder.com",
	"www.visitmississippi.org",
	"www.lanierbb.com",
	"www.thepmga.com",
	"www.skitown.com",
	"www.fairsandfestivals.net",
	"sanfrancisco.going.com",
	"www.koa.com",
	"www.wildlifeviewingareas.com",
	"www.boatrenting.com",
	"www.nowplayingutah.com",
	"www.ultimaterollercoaster.com",
	"www.findacraftfair.com",
	"www.ababmx.com",
	"www.abc-of-skiing.com",
	"www.pw.org",
	"tampabay.metromix.com",
	"www.onthesnow.com",
	"www.sunny.org",
	"www.visitnewengland.com",
	"atlanta.metromix.com",
	"www.allaboutapples.com",
	"www.monsterjam.com",
	"www.bnbfinder.com",
	"www.sandiego.org",
	"www.worldcasinodirectory.com",
	"www.yoga.com",
	"www.1-800-volunteer.org",
	"www.visitkc.com",
	"www.theskichannel.com",
	"www.thephoenix.com",
	"www.virginia.org",
	"www.avclub.com",
	"www.orlandoinfo.com",
	"www.trustedtours.com",
	"www.peakradar.com",
	"web.minorleaguebaseball.com",
	"www.artshound.com",
	"www.daytonabeach.com",
	"chicago.going.com",
	"www.cetaceanwatching.com",
	"www.citypages.com",
	"www.nowplayingnashville.com",
	"www.discoverlosangeles.com",
	"www.ratebeer.com",
	"www.harpercollins.com",
	"www.seenewengland.com",
	"www.visitmt.com",
	"www.goldstar.com",
	"www.caverbob.com",
	"www.sanjose.org",
	"www.backcountrysecrets.com",
	"authors.simonandschuster.com",
	"rafting.allaboutrivers.com",
	"chicago.ettractions.com",
	"iweb.aam-us.org",
	"www.theputtingpenguin.com",
	"www.festivals.com",
	"www.artsboston.org",
	"www.aboutskischools.com",
	"tucson.showup.com",
	"www.thiswaytothe.net",
	"www.rei.com",
	"www.magicseaweed.com",
	"www.waterfallswest.com",
	"fortlauderdale.ettractions.com",
	"www.foodreference.com",
	"www.californiawineryadvisor.com",
	"www.teamap.com",
	"www.neworleanscvb.com",
	"www.skatetheory.com",
	"www.visitmaine.com",
	"www.rollerskating.org",
	"www.culturecapital.com",
	"www.delawarescene.com",
	"www.nyc-arts.org",
	"www.huntingoutfitters.net",
	"www.showcaves.com",
	"www.soccerbars.com",
	"www.visitnewportbeach.com",
	"www.beerme.com",
	"www.pitch.com",
	"www.museum.com",
	"www.hauntworld.com",
	"www.forestcamping.com",
	"www.dogpark.com",
	"www.critterplaces.com",
	"www.visitnj.org",
	"www.findagrave.com",
	"www.arcadefly.com",
	"www.winerybound.com",
	"www.usms.org",
	"www.zipscene.com",
	"www.horsetraildirectory.com",
	"www.coaster-net.com",
	"www.anaheimoc.org",
	"www.visitpa.com",
	"www.antiquetrader.com",
	"www.dallasobserver.com",
	"www.eventsetter.com",
	"www.goingoutside.com",
	"www.sightseeingworld.com",
	"www.artlog.com",
	"www.bnbstar.com",
	"www.hostels.com",
	"www.theartnewspaper.com",
	"consumer.discoverohio.com",
	"www.nssio.org",
	"www.wingshootingusa.org",
	"www.shootata.com",
	"www.randomhouse.com",
	"www.artforum.com",
	"www.bachtrack.com",
	"www.wayspa.com",
	"www.visitidaho.org",
	"www.exploreminnesota.com",
	"chicago.metromix.com",
	"www.worldgolf.com",
	"nysparks.state.ny.us",
	"www.meetup.com",
	"www.skateboardparks.com",
	"www.downtownjacksonville.org",
	"www.lighthousefriends.com",
	"www.strikespots.com",
	"ww2.americancanoe.org",
	"www.inlandarts.com",
	"www.horseshowcentral.com",
	"www.ridingresource.com",
	"www.experiencewa.com",
	"database.thrillnetwork.com",
	"denver.metromix.com",
	"www.bostoncentral.com",
	"www.segwayguidedtours.com",
	"www.colorado.com",
	"www.artandseek.org",
	"www.floridastateparks.org",
	"www.sparkoc.com",
	"losangeles.going.com",
	"www.motorcycleevents.com",
	"www.destination-store.com",
	"www.scubadviser.com",
	"www.booktour.com",
	"www.cloud9living.com",
	"www.allaboutjazz.com",
	"www.sacramento365.com",
	"www.discoversouthcarolina.com",
	"www.riverfronttimes.com",
	"www.hauntedhouses.com",
	"www.arenamaps.com",
	"www.artsnwct.org",
	"www.eventbrite.com",
	"animal.discovery.com",
	"www.eatfeats.com",
	"www.1001seafoods.com",
	"www.malletin.com",
	"www.yelp.com",
	"www.wannasurf.com",
	"www.clubplanet.com",
	"www.dupagecvb.com",
	"www.smartdestinations.com",
	"www.artfaircalendar.com",
	"www.excitations.com",
	"www.balloonrideus.com",
	"www.extravagift.com",
	"www.skisite.com",
	"www.orlandoweekly.com",
	"www.iloveny.com",
	"www.sandiegoreader.com",
	"web.usarugby.org",
	"www.artscalendar.com",
	"www.sfweekly.com",
	"store-locator.barnesandnoble.com",
	"www.realhaunts.com",
	"trails.mtbr.com",
	"www.bbonline.com",
	"www.pickyourownchristmastree.org",
	"events.myspace.com",
	"www.alabama.travel",
	"www.ctvisit.com",
	"freepages.history.rootsweb.com",
	"www.waterparks.com",
	"www.flavorpill.com",
	"www.marinasdirectory.org",
	"www.publicgardens.org",
	"www.alwaysonvacation.com",
	"www.infosports.com",
	"www.summitpost.org",
	"www.exploregeorgia.org",
	"www.brewerysearch.com",
	"www.phoenixnewtimes.com",
	"www.marinas.com",
	"www.arestravel.com",
	"www.gamebirdhunts.com",
	"www.cbssports.com",
	"tutsan.forest.net",
	"www.azcentral.com",
	"www.tennispulse.org",
	"www.westword.com",
	"www.factorytoursusa.com",
	"www.americanwhitewater.org",
	"www.spamagazine.com",
	"www.dogparkusa.com",
	"tps.cr.nps.gov",
	"www.sfstation.com",
	"www.abc-of-yoga.com",
	"www.worldeventsguide.com",
	"www.active.com",
	"www.beerexpedition.com",
	"www.iloveinns.com",
	"www.warpig.com",
	"www.artsopolis.com",
	"www.skatepark.com",
	"www.offroadnorthamerica.com",
	"www.visitflorida.com",
	"www.last.fm",
	"www.pbplanet.com",
	"www.traveltex.com",
	"phoenix.showup.com",
	"www.travelandleisure.com",
	"www.kentuckytourism.com",
	"www.gospelgigs.com",
	"www.whenwegetthere.com",
	"www.surfline.com",
	"www.stubhub.com",
	"www.centerstagechicago.com",
	"www.sunshineartist.com",
	"www.reserveamerica.com",
	"www.clubzone.com",
	"www.paddling.net",
	"www.xperiencedays.com",
	"www.razorgator.com",
	"www.dalejtravis.com",
	"www.pickyourown.org",
	"www.localhikes.com",
	"www.parks.ca.gov",
	"www.casinocity.com",
	"www.nofouls.com",
	"www.laweekly.com",
	"www.denver.org",
	"www.enjoyillinois.com",
	"www.livenation.com",
	"www.viator.com",
	"members.bikeleague.org",
	"www.skatespotter.com",
	"family.go.com",
	"www.myspace.com",
	"www.takemefishing.org",
	"www.localwineevents.com",
	"www.rinkdirectory.com",
	"www.walkjogrun.net",
	"www.nps.gov",
	"www.ghosttowns.com",
	"www.theatermania.com",
	"www.skateboardpark.com",
	"www.miaminewtimes.com",
	"www.explorechicago.org",
	"www.ocweekly.com",
	"www.ustasearch.com",
	"www.rateclubs.com",
	"www.tennismetro.com",
	"www.motorcyclemonster.com",
	"www.hauntedhouse.com",
	"www.pumpkinpatchesandmore.org",
	"www.courtsoftheworld.com",
	"www.ecoanimal.com",
	"www.yogafinder.com",
	"www.traillink.com",
	"www.equinenow.com",
	"www.jambase.com",
	"www.spaemergency.com",
	//"www.vacationhomerentals.com",
	"www.ava.org",
	"affiliate.isango.com",
	"www.museumland.net",
	"www.dirtworld.com",
	"www.rockclimbing.com",
	"www.kijubi.com",
	"www.outdoortrips.info",
	"www.visitcalifornia.com",
	"www.heritagesites.com",
	"www.bedandbreakfast.com",
	"www.discoveramerica.com",
	"www.singletracks.com",
	"www.museumstuff.com",
	"www.opentable.com",
	"www.homeaway.com",
	"www.thegolfcourses.net",
	"www.golflink.com",
	"www.trekaroo.com",
	"gocitykids.parentsconnect.com",
	"www.wildernet.com",
	"www.10best.com",
	"swim.isport.com",
	"www.wheretoshoot.org",
	"www.hostelworld.com",
	"www.landbigfish.com",
	"www.recreation.gov",
	"www.healthclubdirectory.com",
	"www.spafinder.com",
	"www.nationalregisterofhistoricplaces.com",
	"www.americantowns.com",
	"www.hmdb.org",
	"www.golfnow.com",
	"www.grandparents.com",
	"www.swimmersguide.com",
	"www.luxergy.com",
	"activities.wildernet.com",
	"events.mapchannels.com",
	"www.museumsusa.org",
	"www.rinktime.com",
	"www.rentandorbuy.com",
	"www.mytravelguide.com",
	"playspacefinder.kaboom.org",
	"www.famplosion.com",
	"www.eviesays.com",
	"www.anglerweb.com",
	"www.trails.com",
	"www.waymarking.com",
	"www.priceline.com",
	"local.yahoo.com",

	"ticketmaster.com",

	// rss feeds
	"trumba.com",

	// movie times:
	"cinemark.com",

	// domains (hand selected from above list filtered with urlinfo)
	"patch.com",
	"gordonsguide.com",
	"tourcorp.com",
	"americangolf.com",
	"casinocity.com",
	"going.com",
	"metromix.com",
	"ettractions.com",
	"citysearch.com",
	"eventbrite.com"
};
*/

/*
static HashTableX s_table;
static bool s_init = false;
static char s_buf[25000];
static int32_t s_craigsList;

bool initAggregatorTable ( ) {
	// this hashtable is used for "isyellowpages" and "iseventaggregator"
	if ( s_init ) return true;
	// use niceness 0
	s_table.set(4,1,4096,s_buf,25000,false,0,"spsitetbl");
	// now stock it with yellow pages sites
	int32_t n = (int32_t)sizeof(s_ypSites)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *s    = s_ypSites[i];
		int32_t  slen = gbstrlen ( s );
		int32_t h32 = hash32 ( s , slen );
		char val = 1;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// then stock with event aggregator sites
	n = (int32_t)sizeof(s_aggSites)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *s    = s_aggSites[i];
		int32_t  slen = gbstrlen ( s );
		int32_t h32 = hash32 ( s , slen );
		char val = 2;
		if ( ! s_table.addKey(&h32,&val)) {char*xx=NULL;*xx=0;}
	}
	// do not repeat this
	s_init = true;
	s_craigsList = hash32n("craigslist.org");
	return true;
}

bool isAggregator ( int32_t siteHash32,int32_t domHash32,char *url,int32_t urlLen ) {
	// make sure its stocked
	initAggregatorTable();
	// is site a hit?
	char *v = (char *)s_table.getValue ( &siteHash32 );
	// hit?
	if ( v && *v ) return true;
	// try domain?
	v = (char *)s_table.getValue ( &domHash32 );
	// hit?
	if ( v && *v ) return true;
	// these guys mirror eventful.com's db so let's grab it...
	// abcd.com
	if ( urlLen>30 &&
	     url[11]=='t' &&
	     url[18]=='o' &&
	     strncmp(url,"http://www.thingstodoin",23) == 0 ) 
		return true;
	// craigslist
	if ( domHash32 == s_craigsList && strstr(url,".com/cal/") )
		return true;
	// otherwise, no
	return false;
}
*/

#define SIGN_EQ 1
#define SIGN_NE 2
#define SIGN_GT 3
#define SIGN_LT 4
#define SIGN_GE 5
#define SIGN_LE 6

// from PageBasic.cpp
char *getMatchingUrlPattern ( SpiderColl *sc , SpiderRequest *sreq, char *tag);

// . this is called by SpiderCache.cpp for every url it scans in spiderdb
// . we must skip certain rules in getUrlFilterNum() when doing to for Msg20
//   because things like "parentIsRSS" can be both true or false since a url
//   can have multiple spider recs associated with it!
int32_t getUrlFilterNum2 ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       int32_t           nowGlobal  ,
		       bool           isForMsg20 ,
		       int32_t           niceness   ,
		       CollectionRec *cr         ,
			bool           isOutlink  ,
			   HashTableX   *quotaTable ,
			   int32_t langIdArg ) {

	if ( ! sreq ) {
		log("spider: sreq is NULL!");
	}

	int32_t langId = langIdArg;
	if ( srep ) langId = srep->m_langId;

	// convert lang to string
	char *lang    = NULL;
	int32_t  langLen = 0;
	if ( langId >= 0 ) { // if ( srep ) {
		// this is NULL on corruption
		lang = getLanguageAbbr ( langId );//srep->m_langId );	
		if (lang) langLen = gbstrlen(lang);
	}

	// . get parent language in the request
	// . primarpy language of the parent page that linked to this url
	char *plang = NULL;
	int32_t  plangLen = 0;
	plang = getLanguageAbbr(sreq->m_parentLangId);
	if ( plang ) plangLen = gbstrlen(plang);

	char *tld = (char *)-1;
	int32_t  tldLen;

	int32_t  urlLen = sreq->getUrlLen();
	char *url    = sreq->m_url;

	char *row;
	bool checkedRow = false;
	//SpiderColl *sc = cr->m_spiderColl;
	SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);

	if ( ! quotaTable ) quotaTable = &sc->m_localTable;

	//if ( strstr(url,"http://www.vault.com/rankings-reviews/company-rankings/law/vault-law-100/.aspx?pg=2" ))
	//	log("hey");

	//initAggregatorTable();

	//int32_t tldlen2;
	//char *tld2 = getTLDFast ( sreq->m_url , &tldlen2);
	//bool bad = true;
	//if ( tld2[0] == 'c' && tld2[1] == 'o' && tld2[2]=='m' ) bad = false;
	//if ( tld2[0] == 'o' && tld2[1] == 'r' && tld2[2]=='g' ) bad = false;
	//if ( tld2[0] == 'u' && tld2[1] == 's' ) bad = false;
	//if ( tld2[0] == 'g' && tld2[1] == 'o' && tld2[2]=='v' ) bad = false;
	//if ( tld2[0] == 'e' && tld2[1] == 'd' && tld2[2]=='u' ) bad = false;
	//if ( tld2[0] == 'i' && tld2[1] == 'n' && tld2[2]=='f' ) bad = false;
	//if ( bad ) 
	//	log("hey");

	// shortcut
	char *ucp = cr->m_diffbotUrlCrawlPattern.getBufStart();
	char *upp = cr->m_diffbotUrlProcessPattern.getBufStart();

	if ( upp && ! upp[0] ) upp = NULL;
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// get the compiled regular expressions
	regex_t *ucr = &cr->m_ucr;
	regex_t *upr = &cr->m_upr;
	if ( ! cr->m_hasucr ) ucr = NULL;
	if ( ! cr->m_hasupr ) upr = NULL;


	char *ext;
	//char *special;

	// CONSIDER COMPILING FOR SPEED:
	// 1) each command can be combined into a bitmask on the spiderRequest
	//    bits, or an access to m_siteNumInlinks, or a substring match
	// 2) put all the strings we got into the list of Needles
	// 3) then generate the list of needles the SpiderRequest/url matches
	// 4) then reduce each line to a list of needles to have, a
	//    min/max/equal siteNumInlinks, min/max/equal hopCount,
	//    and a bitMask to match the bit flags in the SpiderRequest

	// stop at first regular expression it matches
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// get the ith rule
		SafeBuf *sb = &cr->m_regExs[i];
		//char *p = cr->m_regExs[i];
		char *p = sb->getBufStart();

	checkNextRule:

		// skip leading whitespace
		while ( *p && isspace(*p) ) p++;

		// do we have a leading '!'
		bool val = 0;
		if ( *p == '!' ) { val = 1; p++; }
		// skip whitespace after the '!'
		while ( *p && isspace(*p) ) p++;

		// new rules for when to download (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'c' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "ucp" is a ||-separated list of substrings
			// . "ucr" is a regex
			// . regexec returns 0 for a match
			if ( ucr && regexec(ucr,url,0,NULL,0) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			// do not require a match on ucp if ucr is given
			if ( ucp && ! ucr &&
			     ! doesStringContainPattern(url,ucp) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// new rules for when to "process" (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'p' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "upp" is a ||-separated list of substrings
			// . "upr" is a regex
			// . regexec returns 0 for a match
			if ( upr && regexec(upr,url,0,NULL,0) ) 
				continue;
			if ( upp && !upr &&!doesStringContainPattern(url,upp))
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='h' && strncmp(p,"hasauthorityinlink",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasAuthorityInlinkValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasAuthorityInlink==val)continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hasreply",8) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			//if ( (bool)srep == (bool)val ) continue;
			if ( (bool)(sreq->m_hadReply) == (bool)val ) continue;
			// skip it for speed
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// hastmperror, if while spidering, the last reply was
		// like EDNSTIMEDOUT or ETCPTIMEDOUT or some kind of
		// usually temporary condition that warrants a retry
		if ( *p=='h' && strncmp(p,"hastmperror",11) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// get our error code
			int32_t errCode = srep->m_errCode;
			// . make it zero if not tmp error
			// . now have EDOCUNCHANGED and EDOCNOGOODDATE from
			//   Msg13.cpp, so don't count those here...
			if ( errCode != EDNSTIMEDOUT &&
			     errCode != ETCPTIMEDOUT &&
			     errCode != EDNSDEAD &&
			     // add this here too now because we had some
			     // seeds that failed one time and the crawl
			     // never repeated after that!
			     errCode != EBADIP &&
			     // assume diffbot is temporarily experiencing errs
			     // but the crawl, if recurring, should retry these
			     // at a later point
			     errCode != EDIFFBOTUNABLETOAPPLYRULES &&
			     errCode != EDIFFBOTCOULDNOTPARSE &&
			     errCode != EDIFFBOTCOULDNOTDOWNLOAD &&
			     errCode != EDIFFBOTINVALIDAPI &&
			     errCode != EDIFFBOTVERSIONREQ &&
			     errCode != EDIFFBOTURLPROCESSERROR &&
			     errCode != EDIFFBOTTOKENEXPIRED &&
			     errCode != EDIFFBOTUNKNOWNERROR &&
			     errCode != EDIFFBOTINTERNALERROR &&
			     // if diffbot received empty content when d'lding
			     errCode != EDIFFBOTEMPTYCONTENT &&
			     // or diffbot tcp timed out when d'lding the url
			     errCode != EDIFFBOTREQUESTTIMEDOUT &&
			     // if diffbot closed the socket on us...
			     errCode != EDIFFBOTMIMEERROR &&
			     // of the diffbot reply itself was not 200 (OK)
			     errCode != EDIFFBOTBADHTTPSTATUS &&
			     // out of memory while crawling?
			     errCode != ENOMEM &&
			     errCode != ENETUNREACH &&
			     errCode != EHOSTUNREACH )
				errCode = 0;
			// if no match continue
			if ( (bool)errCode == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( *p != 'i' ) goto skipi;

		if ( strncmp(p,"isinjected",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isInjecting==val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isdocidbased",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isreindex",9) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// is it in the big list of sites?
		if ( strncmp(p,"insitelist",10) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// if only seeds in the sitelist and no

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmptyValid &&
			     sc->m_siteListIsEmpty ) {
				// use a dummy row match
				row = (char *)1;
			}
			else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				row = getMatchingUrlPattern ( sc, sreq ,NULL);
			}
			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// . was it submitted from PageAddUrl.cpp?
		// . replaces the "add url priority" parm
		if ( strncmp(p,"isaddurl",8) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we are not submitted from the add url api, skip
			if ( (bool)sreq->m_isAddUrl == val ) continue;
			// skip
			p += 8;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( p[0]=='i' && strncmp(p,"ismanualadd",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// . if we are not submitted from the add url api, skip
			// . if we have '!' then val is 1
			if ( sreq->m_isAddUrl    || 
			     sreq->m_isInjecting ||
			     sreq->m_isPageReindex ||
			     sreq->m_isPageParser ) {
				if ( val ) continue;
			}
			else {
				if ( ! val ) continue;
			}
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isparentrss",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we have no such inlink
			if ( (bool)sreq->m_parentIsRSS == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isparentsitemap",15) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsSiteMap == val) continue;
			// skip
			p += 15;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isroot",6) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// this is a docid only url, no actual url, so skip
			if ( sreq->m_isPageReindex ) continue;
			// a fast check
			char *u = sreq->m_url;
			// skip http
			u += 4;
			// then optional s for https
			if ( *u == 's' ) u++;
			// then ://
			u += 3;
			// scan until \0 or /
			for ( ; *u && *u !='/' ; u++ );
			// if \0 we are root
			bool isRoot = true;
			if ( *u == '/' ) {
				u++;
				if ( *u ) isRoot = false;
			}
			// if we are not root
			if ( isRoot == val ) continue;
			// skip
			p += 6;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		/*
		if ( strncmp(p,"isparentindexed",16) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we have no such inlink
			if ( (bool)sreq->m_wasParentIndexed == val ) continue;
			// skip
			p += 16;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}
		*/

		// we can now handle this guy since we have the latest
		// SpiderReply, pretty much guaranteed
		if ( strncmp(p,"isindexed",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if reply does not KNOW because of an error
			// since XmDoc::indexDoc() called
			// XmlDoc::getNewSpiderReply() and did not have this
			// info...
			if ( srep && (bool)srep->m_isIndexedINValid ) continue;
			// if no match continue
			if ( srep && (bool)srep->m_isIndexed==val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			if ( ! srep && val == 0 ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		// . check to see if a page is linked to by
		//   www.weblogs.com/shortChanges.xml and if it is we put
		//   it into a queue that has a respider rate no faster than
		//   30 days, because we don't need to spider it quick since
		//   it is in the ping server!
		if ( strncmp(p,"isparentpingserver",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsPingServer == val) continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"ispingserver",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isPingServer == val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamesubdomain",17 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentHostHash32 != sreq->m_hostHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentHostHash32 == sreq->m_hostHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isfakeip",8 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_fakeFirstIp == val ) continue;
			p += 8;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamedomain",14 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentDomHash32 != sreq->m_domHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentDomHash32 == sreq->m_domHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}

		// jpg JPG gif GIF wmv mpg css etc.
		if ( strncmp ( p , "ismedia",7 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;

			// the new way is much faster, but support the
			// old way below for a while since this bit is new
			if ( sreq->m_hasMediaExtension )
				goto gotOne;
			// if that bit is valid, and zero, then we do not match
			if ( sreq->m_hasMediaExtensionValid )
				continue;

			// check the extension
			if ( urlLen<=5 ) continue;
			ext = url + urlLen - 4;
			if ( ext[0] == '.' ) {
				if ( to_lower_a(ext[1]) == 'c' &&
				     to_lower_a(ext[2]) == 's' &&
				     to_lower_a(ext[3]) == 's' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'n' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'm' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'a' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'j' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'g' &&
				     to_lower_a(ext[2]) == 'i' &&
				     to_lower_a(ext[3]) == 'f' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'i' &&
				     to_lower_a(ext[2]) == 'c' &&
				     to_lower_a(ext[3]) == 'o' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '3' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '4' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'o' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'a' &&
				     to_lower_a(ext[2]) == 'v' &&
				     to_lower_a(ext[3]) == 'i' )
					goto gotOne;
			}
			else if ( ext[-1] == '.' ) {
				if ( to_lower_a(ext[0]) == 'm' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[0]) == 'j' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
			}
			// two letter extensions
			// .warc.gz and .arc.gz is ok
			// take this out for now
			// else if ( ext[1] == '.' ) {
			// 	if ( to_lower_a(ext[2]) == 'g' &&
			// 	     to_lower_a(ext[3]) == 'z' )
			// 		goto gotOne;
			// }

			// check for ".css?" substring
			// these two suck up a lot of time:
			// take them out for now. MDW 2/21/2015
			//special = strstr(url,".css?");
			//if ( special ) goto gotOne;
			//special = strstr(url,"/print/");
			// try to make detecting .css? super fast
			if ( ext[0] != '.' &&
			     ext[1] != '.' &&
			     urlLen > 10 ) {
				for(register int32_t k=urlLen-10;k<urlLen;k++){
					if ( url[k] != '.' ) continue;
					if ( url[k+1] == 'c' &&
					     url[k+2] == 's' &&
					     url[k+3] == 's' &&
					     url[k+4] == '?' )
						goto gotOne;
				}
			}
			//if ( special ) goto gotOne;
			// no match, try the next rule
			continue;
		gotOne:
			p += 7;
			p = strstr(p, "&&");
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}


		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrss",5) == 0 ) {
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isRSS == val ) continue; 
			// skip it
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrssext",8) == 0 ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isRSSExt == val ) continue; 
			// skip it
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for permalinks. for new outlinks we *guess* if its
		// a permalink by calling isPermalink() function.
		if (!strncmp(p,"ispermalink",11) ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isPermalink == val ) continue; 
			// skip it
			p += 11;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// supports LF_ISPERMALINK bit for outlinks that *seem* to
		// be permalinks but might not
		if (!strncmp(p,"ispermalinkformat",17) ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isUrlPermalinkFormat ==val)continue;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewoutlink",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we do not match this rule
			if ( (bool)sreq->m_isNewOutlink == val ) continue;
			// skip it
			p += 10;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewrequest",12) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if ( ! srep && val ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if(srep&&sreq->m_addedTime>srep->m_spideredTime &&val)
				continue;
			// skip if we are old and val is 0 (does not have '!')
			if(srep&&sreq->m_addedTime<=srep->m_spideredTime&&!val)
				continue;
			// skip it for speed
			p += 12;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// kinda like isnewrequest, but has no reply. use hasreply?
		if ( strncmp(p,"isnew",5) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			if ( (bool)sreq->m_hadReply != (bool)val ) continue;
			// skip it for speed
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		// iswww, means url is like www.xyz.com/...
		if ( strncmp(p,"iswww", 5) == 0 ) {
			// now this is a bit - doesn't seem to be working yet
			//if ( (bool)sreq->m_isWWWSubdomain == (bool)val ) 
			//	continue;
			// skip "iswww"
			p += 5;
			// skip over http:// or https://
			char *u = sreq->m_url;
			if ( u[4] == ':' ) u += 7;
			if ( u[5] == ':' ) u += 8;
			// url MUST be a www url
			char isWWW = 0;
			if( u[0] == 'w' &&
			    u[1] == 'w' &&
			    u[2] == 'w' ) isWWW = 1;
			// skip if no match
			if ( isWWW == val ) continue;
			// TODO: fix www.knightstown.skepter.com
			// maybe just have a bit in the spider request
			// another rule?
			p = strstr(p,"&&");
			if ( ! p ) return i;
			// skip the '&&'
			p += 2;
			goto checkNextRule;
		}

		// non-boolen junk
 skipi:

		// . we always match the "default" reg ex
		// . this line must ALWAYS exist!
		if ( *p=='d' && ! strcmp(p,"default" ) )
			return i;

		// is it in the big list of sites?
		if ( *p == 't' && strncmp(p,"tag:",4) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// if only seeds in the sitelist and no

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmpty &&
			     sc->m_siteListIsEmptyValid ) {
				row = NULL;// no row
			}
			else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				// . it also has to match "tag" at (p+4)
				row = getMatchingUrlPattern ( sc, sreq ,p+4);
			}
			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) continue;
			// skip tag:
			p += 4;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) return i;
			p += 2;
			goto checkNextRule;
		}
		



		// set the sign
		char *s = p;
		// skip s to after
		while ( *s && is_alpha_a(*s) ) s++;

		// skip white space before the operator
		//char *saved = s;
		while ( *s && is_wspace_a(*s) ) s++;

		char sign = 0;
		if ( *s == '=' ) {
			s++;
			if ( *s == '=' ) s++;
			sign = SIGN_EQ;
		}
		else if ( *s == '!' && s[1] == '=' ) {
			s += 2;
			sign = SIGN_NE;
		}
		else if ( *s == '<' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_LE; s++; }
			else               sign = SIGN_LT; 
		} 
		else if ( *s == '>' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_GE; s++; }
			else               sign = SIGN_GT; 
		} 

		// skip whitespace after the operator
		while ( *s && is_wspace_a(*s) ) s++;


		// seed counts. how many seeds this subdomain has. 'siteadds'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'a' &&
		     p[5] == 'd' &&
		     p[6] == 'd' &&
		     p[7] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_siteHash32 ^ 0x123456;
			int32_t *valPtr =(int32_t *)quotaTable->getValue(&h32);
			int32_t a;
			// if no count in table, that is strange, i guess
			// skip for now???
			// this happens if INJECTING a url from the
			// "add url" function on homepage
			if ( ! valPtr ) a=0;//continue;//{char *xx=NULL;*xx=0;}
			// shortcut
			else a = *valPtr;
			//log("siteadds=%"INT32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain seeds. 'domainadds'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'a' &&
		     p[7] == 'd' &&
		     p[8] == 'd' &&
		     p[9] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_domHash32 ^ 0x123456;
			int32_t *valPtr ;
			valPtr = (int32_t *)quotaTable->getValue(&h32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}



		// new quotas. 'sitepages' = pages from site.
		// 'sitepages > 20 && seedcount <= 1 --> FILTERED'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'p' &&
		     p[5] == 'a' &&
		     p[6] == 'g' &&
		     p[7] == 'e' &&
		     p[8] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			int32_t *valPtr ;
		       valPtr=(int32_t*)quotaTable->getValue(&sreq->m_siteHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// shortcut
			//log("sitepgs=%"INT32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain quotas. 'domainpages > 10 && hopcount >= 1 --> FILTERED'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'p' &&
		     p[7] == 'a' &&
		     p[8] == 'g' &&
		     p[9] == 'e' &&
		     p[10] == 's' ) {
			// need a quota table for this. this only happens
			// when trying to shortcut things to avoid adding
			// urls to spiderdb... like XmlDoc.cpp calls
			// getUrlFtilerNum() to see if doc is banned or
			// if it should harvest links.
			if ( ! quotaTable )
				return -1;
			int32_t *valPtr;
			valPtr=(int32_t*)quotaTable->getValue(&sreq->m_domHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// tld:cn 
		if ( *p=='t' && strncmp(p,"tld",3)==0){
			// set it on demand
			if ( tld == (char *)-1 )
				tld = getTLDFast ( sreq->m_url , &tldLen );
			// no match if we have no tld. might be an IP only url,
			// or not in our list in Domains.cpp::isTLD()
			if ( ! tld || tldLen == 0 ) continue;
			// set these up
			//char *a    = tld;
			//int32_t  alen = tldLen;
			char *b    = s;
			// loop for the comma-separated list of tlds
			// like tld:us,uk,fr,it,de
		subloop1:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had tld==com,org,...
			if ( sign == SIGN_EQ &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// if we matched any, that's great
				goto matched1;
			// if its tld!=com,org,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// we do not match this rule if we matched
				// and of the tlds in the != list
				continue;
			// might have another tld in a comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched1;
				// otherwise, bad sign?
				continue;
			}
			// advance to next tld if there was a comma after us
			b++;
			// and try again
			goto subloop1;
			// otherwise
			// do we match, if not, try next regex
			//sm = strncasecmp(a,b,blen);
			//if ( sm != 0 && sign == SIGN_EQ ) goto miss1;
			//if ( sm == 0 && sign == SIGN_NE ) goto miss1;
			// come here on a match
		matched1:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// lang:en,zh_cn
		if ( *p=='l' && strncmp(p,"lang",4)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( langId == -1 ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like lang==en,es,...
		subloop2:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had lang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// if we matched any, that's great
				goto matched2;
			// if its lang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2;
			// come here on a match
		matched2:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// parentlang=en,zh_cn
		if ( *p=='p' && strncmp(p,"parentlang",10)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			//if ( ! srep ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like parentlang==en,es,...
		subloop2b:
			// get length of it in the expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had parentlang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// if we matched any, that's great
				goto matched2b;
			// if its parentlang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2b;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2b;
			// come here on a match
		matched2b:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) return i;
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// hopcount == 20 [&&]
		if ( *p=='h' && strncmp(p, "hopcount", 8) == 0){
			// skip if not valid
			if ( ! sreq->m_hopCountValid ) continue;
			// shortcut
			int32_t a = sreq->m_hopCount;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// the last time it was spidered
		if ( *p=='l' && strncmp(p,"lastspidertime",14) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			int32_t a = 0;
			// if no spider reply we can't match this rule!
			if ( ! srep ) continue;
			// shortcut
			if ( srep ) a = srep->m_spideredTime;
			// make it point to the retry count
			int32_t b ;
			// now "s" can be "{roundstart}"
			if ( s[0]=='{' && strncmp(s,"{roundstart}",12)==0)
				b = cr->m_spiderRoundStartTime;//Num;
			else
				b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// selector using the first time it was added to the Spiderdb
		// added by Sam, May 5th 2015
		if ( *p=='u' && strncmp(p,"urlage",6) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) {
				//log("was for message 20");
				continue;

			}
			// get the age of the spider_request. 
			// (substraction of uint with int, hope
			// every thing goes well there)
			int32_t sreq_age = 0;

			// if m_discoveryTime is available, we use it. Otherwise we use m_addedTime
			if ( sreq && sreq->m_discoveryTime!=0) sreq_age = nowGlobal-sreq->m_discoveryTime;
			if ( sreq && sreq->m_discoveryTime==0) sreq_age = nowGlobal-sreq->m_addedTime;
			//log("spiderage=%d",sreq_age);
			// the argument entered by user
			int32_t argument_age=atoi(s) ;
			if ( sign == SIGN_EQ && sreq_age != argument_age ) continue;
			if ( sign == SIGN_NE && sreq_age == argument_age ) continue;
			if ( sign == SIGN_GT && sreq_age <= argument_age ) continue;
			if ( sign == SIGN_LT && sreq_age >= argument_age ) continue;
			if ( sign == SIGN_GE && sreq_age <  argument_age ) continue;
			if ( sign == SIGN_LE && sreq_age >  argument_age ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='e' && strncmp(p,"errorcount",10) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// shortcut
			int32_t a = srep->m_errCount;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// EBADURL malformed url is ... 32880
		if ( *p=='e' && strncmp(p,"errorcode",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// shortcut
			int32_t a = srep->m_errCode;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 9;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		if ( *p == 'n' && strncmp(p,"numinlinks",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// these are -1 if they are NOT valid
			int32_t a = sreq->m_pageNumInlinks;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// siteNumInlinks >= 300 [&&]
		if ( *p=='s' && strncmp(p, "sitenuminlinks", 14) == 0){
			// these are -1 if they are NOT valid
			int32_t a1 = sreq->m_siteNumInlinks;
			// only assign if valid
			int32_t a2 = -1; 
			if ( srep ) a2 = srep->m_siteNumInlinks;
			// assume a1 is the best
			int32_t a ;
			// assign to the first valid one
			if      ( a1 != -1 ) a = a1;
			else if ( a2 != -1 ) a = a2;
			// swap if both are valid, but srep is more recent
			if ( a1 != -1 && a2 != -1 &&
			     srep->m_spideredTime > sreq->m_addedTime )
				a = a2;
			// skip if nothing valid
			if ( a == -1 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 14;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		/*
		// retryNum >= 2 [&&] ...
		if ( *p=='r' && strncmp(p, "retrynum", 8) == 0){
			// shortcut
			int32_t a = sr->m_retryNum;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		*/

		// how many days have passed since it was last attempted
		// to be spidered? used in conjunction with percentchanged
		// to assign when to re-spider it next
		if ( *p=='s' && strncmp(p, "spiderwaited", 12) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// do not match rule if never attempted
			// if ( srep->m_spideredTime ==  0 ) {
			// 	char*xx=NULL;*xx=0;}
			// if ( srep->m_spideredTime == (uint32_t)-1){
			// 	char*xx=NULL;*xx=0;}
			// shortcut
			int32_t a = nowGlobal - srep->m_spideredTime;
			// make into days
			//af /= (3600.0*24.0);
			// back to a int32_t, round it
			//int32_t a = (int32_t)(af + 0.5);
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// percentchanged >= 50 [&&] ...
		if ( *p=='p' && strncmp(p, "percentchangedperday", 20) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// shortcut
			float a = srep->m_percentChangedPerDay;
			// make it point to the priority
			float b = atof(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// httpStatus == 400
		if ( *p=='h' && strncmp(p, "httpstatus", 10) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// shortcut (errCode doubles as g_errno)
			int32_t a = srep->m_errCode;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// how old is the doc in seconds? age is the pubDate age
		if ( *p =='a' && strncmp(p, "age", 3) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// shortcut
			int32_t age;
			if ( srep->m_pubDate <= 0 ) age = -1;
			else age = nowGlobal - srep->m_pubDate;
			// we can not match if invalid
			if ( age <= 0 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && age != b ) continue;
			if ( sign == SIGN_NE && age == b ) continue;
			if ( sign == SIGN_GT && age <= b ) continue;
			if ( sign == SIGN_LT && age >= b ) continue;
			if ( sign == SIGN_GE && age <  b ) continue;
			if ( sign == SIGN_LE && age >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		/*
		  MDW: i replaced this with 
		  m_contentHash32 to make spiders faster/smarter so let's
		  take this out for now

		// how many new inlinkers we got since last spidered time?
		if ( *p =='n' && strncmp(p, "newinlinks", 10) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) return -1;
			// must have a reply
			if ( ! srep ) continue;
			// . make it point to the newinlinks.
			// . # of new SpiderRequests added since 
			//   srep->m_spideredTime
			// . m_dupCache insures that the same ip/hostHash
			//   does not add more than 1 SpiderRequest for the
			//   same url/outlink
			int32_t a = srep->m_newRequests;
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// quick
			p += 10;
			// look for more
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		*/

		// our own regex thing (match front of url)
		if ( *p=='^' ) {
			// advance over caret
			p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) return i;
				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// our own regex thing (match end of url)
		if ( *p=='$' ) {
			// advance over dollar sign
			p++;
			// a hack for $\.css, skip over the backslash too
			if ( *p=='\\' && *(p+1)=='.' ) p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			// . do we match it?
			// . url has to be at least as big
			// . match our tail
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url+urlLen-plen,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) return i;
				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// . by default a substring match
		// . action=edit
		// . action=history

		// now pstart pts to the string we will match
		char *pstart = p;
		// make "p" point to one past the last char in string
		while ( *p && ! is_wspace_a(*p) ) p++;
		// how long is the string to match?
		int32_t plen = p - pstart;
		// need something...
		if ( plen <= 0 ) continue;
		// must be at least as big
		//if ( urlLen < plen ) continue;
		// nullilfy it temporarily
		char c = *p;
		*p     = '\0';
		// does url contain it? haystack=u needle=p
		char *found = strstr ( url , pstart );
		// put char back
		*p     = c;

		// kinda of a hack fix. if they inject a filtered url
		// into test coll, do not filter it! fixes the fact that
		// we filtered facebook, but still add it in our test
		// collection injection in urls.txt
		if ( found && 
		     sreq->m_isInjecting &&
		     cr->m_coll[0]=='t' &&
		     cr->m_coll[1]=='e' &&
		     cr->m_coll[2]=='s' &&
		     cr->m_coll[3]=='t' &&
		     cr->m_coll[4]=='\0' &&
		     cr->m_spiderPriorities[i] < 0 )
			continue;

		// support "!company" meaning if it does NOT match
		// then do this ...
		if ( ( found && val == 0 ) ||
		     // if they used the '!' operator and we
		     // did not match the string, that's a 
		     // row match
		     ( ! found && val == 1 ) ) {
			// another expression follows?
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) return i;
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

	}
	// sanity check ... must be a default rule!
	//char *xx=NULL;*xx=0;
	// return -1 if no match, caller should use a default
	return -1;
}

//static bool s_ufnInit = false;
//static HashTableX s_ufnTable;

//void clearUfnTable ( ) { 
//	s_ufnTable.clear(); 
//	s_ufnTree.clear();
//}

int32_t getUrlFilterNum ( SpiderRequest *sreq       ,
		       SpiderReply   *srep       ,
		       int32_t           nowGlobal  ,
		       bool           isForMsg20 ,
		       int32_t           niceness   ,
		       CollectionRec *cr         ,
		       bool           isOutlink  ,
			  HashTableX    *quotaTable ,
			  int32_t langId ) {

	/*
	  turn this off for now to save memory on the g0 cluster.
	  we should nuke this anyway with rankdb

	// init table?
	if ( ! s_ufnInit ) {
		s_ufnInit = true;
		if ( ! s_ufnTable.set(8,
				      1,
				      1024*1024*5,
				      NULL,0,
				      false,
				      MAX_NICENESS,
				      "ufntab") ) { char *xx=NULL;*xx=0; } 
	}

	// check in cache using date of request and reply and uh48 as the key
	int64_t key64 = sreq->getUrlHash48();
	key64 ^= (int64_t)sreq->m_addedTime;
	if ( srep ) key64 ^= ((int64_t)srep->m_spideredTime)<<32;
	char *uv = (char *)s_ufnTable.getValue(&key64);
	if ( uv ) 
		return *uv;
	*/
	char ufn = getUrlFilterNum2 ( sreq,
				      srep,
				      nowGlobal,
				      isForMsg20,
				      niceness,
				      cr,
				      isOutlink,
				      quotaTable ,
				      langId );

	/*
	// is table full? clear it if so
	if ( s_ufnTable.getNumSlotsUsed() > 2000000 ) {
		log("spider: resetting ufn table");
		s_ufnTable.clear();
	}
	// cache it
	s_ufnTable.addKey ( &key64 , &ufn );
	*/

	return (int32_t)ufn;
}



// . dedup for spiderdb
// . TODO: we can still have spider request dups in this if they are
//   sandwiched together just right because we only compare to the previous
//   SpiderRequest we added when looking for dups. just need to hash the
//   relevant input bits and use that for deduping.
// . TODO: we can store ufn/priority/spiderTime in the SpiderRequest along
//   with the date now, so if url filters do not change then 
//   gotSpiderdbList() can assume those to be valid and save time. BUT it does
//   have siteNumInlinks...
void dedupSpiderdbList ( RdbList *list , int32_t niceness , bool removeNegRecs ) {

	//int32_t  need = list->m_listSize;
	char *newList = list->m_list;//(char *)mmalloc (need,"dslist");
	//if ( ! newList ) {
	//	log("spider: could not dedup spiderdb list: %s",
	//	    mstrerror(g_errno));
	//	return;
	//}
	char *dst          = newList;
	char *restorePoint = newList;
	int64_t reqUh48  = 0LL;
	int64_t repUh48  = 0LL;
	SpiderReply   *oldRep = NULL;
	SpiderRequest *oldReq = NULL;
	char *lastKey     = NULL;
	char *prevLastKey = NULL;

	// save list ptr in case of re-read?
	//char *saved = list->m_listPtr;
	// reset it
	list->resetListPtr();

	for ( ; ! list->isExhausted() ; ) {
		// breathe. NO! assume in thread!!
		//QUICKPOLL(niceness);
		// get rec
		char *rec = list->getCurrentRec();

		// pre skip it
		list->skipCurrentRec();

		// skip if negative, just copy over
		if ( ( rec[0] & 0x01 ) == 0x00 ) {
			// should not be in here if this was true...
			if ( removeNegRecs ) {
				log("spider: filter got negative key");
				char *xx=NULL;*xx=0;
			}
			// save this
			prevLastKey = lastKey;
			lastKey     = dst;
			// otherwise, keep it
			memmove ( dst , rec , sizeof(key128_t) );
			dst += sizeof(key128_t);
			continue;
		}

		// is it a reply?
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			// cast it
			SpiderReply *srep = (SpiderReply *)rec;
			// shortcut
			int64_t uh48 = srep->getUrlHash48();
			// crazy?
			if ( ! uh48 ) { 
				//uh48 = hash64b ( srep->m_url );
				uh48 = 12345678;
				log("spider: got uh48 of zero for spider req. "
				    "computing now.");
			}
			// does match last reply?
			if ( repUh48 == uh48 ) {
				// if he's a later date than us, skip us!
				if ( oldRep->m_spideredTime >=
				     srep->m_spideredTime )
					// skip us!
					continue;
				// otherwise, erase him
				dst     = restorePoint;
				lastKey = prevLastKey;
			}
			// save in case we get erased
			restorePoint = dst;
			prevLastKey  = lastKey;
			lastKey      = dst;
			// get our size
			int32_t recSize = srep->getRecSize();
			// and add us
			memmove ( dst , rec , recSize );
			// advance
			dst += recSize;
			// update this crap for comparing to next reply
			repUh48 = uh48;
			oldRep  = srep;
			// get next spiderdb record
			continue;
		}

		// shortcut
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// shortcut
		int64_t uh48 = sreq->getUrlHash48();

		// crazy?
		if ( ! uh48 ) { 
			//uh48 = hash64b ( sreq->m_url );
			uh48 = 12345678;
			log("spider: got uh48 of zero for spider req. "
			    "computing now.");
		}

		// update request with SpiderReply if newer, because ultimately
		// ::getUrlFilterNum() will just look at SpiderRequest's 
		// version of these bits!
		if ( oldRep && repUh48 == uh48 &&
		     oldRep->m_spideredTime > sreq->m_addedTime ) {

			// if request was a page reindex docid based request 
			// and url has since been spidered, nuke it!
			//if ( sreq->m_urlIsDocId ) continue;
			if ( sreq->m_isPageReindex ) continue;

			// same if indexcode was EFAKEFIRSTIP which XmlDoc.cpp
			// re-adds to spiderdb with the right firstip. once
			// those guys have a reply we can ignore them.
			// TODO: what about diffbotxyz spider requests? those
			// have a fakefirstip... they should not have requests
			// though, since their parent url has that.
			if ( sreq->m_fakeFirstIp ) continue;

			SpiderReply *old = oldRep;
			sreq->m_hasAuthorityInlink = old->m_hasAuthorityInlink;
		}

		// if we are not the same url as last request, add it
		if ( uh48 != reqUh48 ) {
			// a nice hook in
		addIt:
			// save in case we get erased
			restorePoint = dst;
			prevLastKey  = lastKey;
			// get our size
			int32_t recSize = sreq->getRecSize();
			// save this
			lastKey = dst;
			// and add us
			memmove ( dst , rec , recSize );
			// advance
			dst += recSize;
			// update this crap for comparing to next reply
			reqUh48  = uh48;
			oldReq   = sreq;
			// get next spiderdb record
			continue;
		}

		// try to kinda grab the min hop count as well
		// do not alter spiderdb!
		// if ( sreq->m_hopCountValid && oldReq->m_hopCountValid ) {
		// 	if ( oldReq->m_hopCount < sreq->m_hopCount )
		// 		sreq->m_hopCount = oldReq->m_hopCount;
		// 	else
		// 		oldReq->m_hopCount = sreq->m_hopCount;
		// }

		// if he's essentially different input parms but for the
		// same url, we want to keep him because he might map the
		// url to a different url priority!
		if ( oldReq->m_siteHash32    != sreq->m_siteHash32    ||
		     oldReq->m_isNewOutlink  != sreq->m_isNewOutlink  ||
		     //  use hopcount now too!
		     oldReq->m_hopCount      != sreq->m_hopCount      ||
		     // makes a difference as far a m_minPubDate goes, because
		     // we want to make sure not to delete that request that
		     // has m_parentPrevSpiderTime
		     // no no, we prefer the most recent spider request
		     // from thsi site in the logic above, so this is not
		     // necessary. mdw commented out.
		     //oldReq->m_wasParentIndexed != sreq->m_wasParentIndexed||
		     oldReq->m_isInjecting   != sreq->m_isInjecting   ||
		     oldReq->m_isAddUrl      != sreq->m_isAddUrl      ||
		     oldReq->m_isPageReindex != sreq->m_isPageReindex ||
		     oldReq->m_forceDelete   != sreq->m_forceDelete    )
			// we are different enough to coexist
			goto addIt;
		// . if the same check who has the most recent added time
		// . if we are not the most recent, just do not add us
		// . no, now i want the oldest so we can do gbssDiscoveryTime
		//   and set sreq->m_discoveryTime accurately, above
		if ( sreq->m_addedTime >= oldReq->m_addedTime ) continue;
		// otherwise, erase over him
		dst     = restorePoint;
		lastKey = prevLastKey;
		// and add us over top of him
		goto addIt;
	}

	// free the old list
	//char *oldbuf  = list->m_alloc;
	//int32_t  oldSize = list->m_allocSize;

	// sanity check
	if ( dst < list->m_list || dst > list->m_list + list->m_listSize ) {
		char *xx=NULL;*xx=0; }

	// and stick our newly filtered list in there
	//list->m_list      = newList;
	list->m_listSize  = dst - newList;
	// set to end i guess
	list->m_listPtr   = dst;
	//list->m_allocSize = need;
	//list->m_alloc     = newList;
	list->m_listEnd   = list->m_list + list->m_listSize;
	list->m_listPtrHi = NULL;
	//KEYSET(list->m_lastKey,lastKey,list->m_ks);

	if ( lastKey ) KEYSET(list->m_lastKey,lastKey,list->m_ks);

	//mfree ( oldbuf , oldSize, "oldspbuf");
}

///////
//
// diffbot uses these for limiting crawls in a collection
//
///////

void gotCrawlInfoReply ( void *state , UdpSlot *slot);

static int32_t s_requests = 0;
static int32_t s_replies  = 0;
static int32_t s_validReplies  = 0;
static bool s_inUse = false;
// we initialize CollectionRec::m_updateRoundNum to 0 so make this 1
static int32_t s_updateRoundNum = 1;

// . just call this once per second for all collections
// . figure out how to backoff on collections that don't need it so much
// . ask every host for their crawl infos for each collection rec
void updateAllCrawlInfosSleepWrapper ( int fd , void *state ) {

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__, __func__, __LINE__);
	
	// debug test
	//int32_t mr = g_collectiondb.m_recs[0]->m_maxCrawlRounds;
	//log("mcr: %"INT32"",mr);

	// i don't know why we have locks in the lock table that are not
	// getting removed... so log when we remove an expired locks and see.
	// piggyback on this sleep wrapper call i guess...
	// perhaps the collection was deleted or reset before the spider
	// reply could be generated. in that case we'd have a dangling lock.
	removeExpiredLocks ( -1 );

	if ( s_inUse ) return;

	// "i" means to get incremental updates since last round
	// "f" means to get all stats
	char *request = "i";
	int32_t requestSize = 1;

	static bool s_firstCall = true;
	if ( s_firstCall ) {
		s_firstCall = false;
		request = "f";
	}

	s_inUse = true;

	// reset tmp crawlinfo classes to hold the ones returned to us
	//for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
	//	CollectionRec *cr = g_collectiondb.m_recs[i];
	//	if ( ! cr ) continue;
	//	cr->m_tmpCrawlInfo.reset();
	//}

	// send out the msg request
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		// skip if dead. no! we need replies from all hosts
		// otherwise our counts could be short and we might end up
		// re-spidering stuff even though we've really hit maxToCrawl
		//if ( g_hostdb.isDead(i) ) {
		//	if ( g_conf.m_logDebugSpider )
		//		log("spider: skipping dead host #%"INT32" "
		//		    "when getting "
		//		    "crawl info",i);
		//	continue;
		//}
		// count it as launched
		s_requests++;
		// launch it
		if ( ! g_udpServer.sendRequest ( request,
						 requestSize,
						 0xc1 , // msgtype
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL, // retslot
						 NULL, // state
						 gotCrawlInfoReply ) ) {
			log("spider: error sending c1 request: %s",
			    mstrerror(g_errno));
			s_replies++;
		}
	}
	
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: Sent %"INT32" requests, got %"INT32" replies", __FILE__, __func__, __LINE__, s_requests, s_replies);
	// return false if we blocked awaiting replies
	if ( s_replies < s_requests )
	{
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END. requests/replies mismatch", __FILE__, __func__, __LINE__);
		return;
	}

	// how did this happen?
	log("spider: got bogus crawl info replies!");
	s_inUse = false;
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END", __FILE__, __func__, __LINE__);
	return;
}



// . Parms.cpp calls this when it receives our "spiderRoundNum" increment above
// . all hosts should get it at *about* the same time
void spiderRoundIncremented ( CollectionRec *cr ) {

	log("spider: incrementing spider round for coll %s to %"INT32" (%"UINT32")",
	    cr->m_coll,cr->m_spiderRoundNum,
	    (uint32_t)cr->m_spiderRoundStartTime);

	// . need to send a notification for this round
	// . we are only here because the round was incremented and
	//   Parms.cpp just called us... and that only happens in 
	//   doneSending... so do not send again!!!
	//cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;

	// . if we set sentCrawlDoneALert to 0 it will immediately
	//   trigger another round increment !! so we have to set these
	//   to true to prevent that.
	// . if we learnt that there really are no more urls ready to spider
	//   then we'll go to the next round. but that can take like
	//   SPIDER_DONE_TIMER seconds of getting nothing.
	cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = true;
	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;

	cr->m_localCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_localCrawlInfo.m_pageProcessSuccessesThisRound  = 0;
	cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound = 0;
	cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound  = 0;

	cr->localCrawlInfoUpdate();

	cr->m_needsSave = true;
}

void gotCrawlInfoReply ( void *state , UdpSlot *slot ) {

	// loop over each LOCAL crawlinfo we received from this host
	CrawlInfo *ptr   = (CrawlInfo *)(slot->m_readBuf);
	CrawlInfo *end   = (CrawlInfo *)(slot->m_readBuf+ slot->m_readBufSize);
	//int32_t       allocSize           = slot->m_readBufMaxSize;

	// host sending us this reply
	Host *h = slot->m_host;

	// assume it is a valid reply, not an error, like a udptimedout
	s_validReplies++;

	// reply is error? then use the last known good reply we had from him
	// assuming udp reply timed out. empty buf just means no update now!
	if ( ! slot->m_readBuf && g_errno ) {
		log("spider: got crawlinfo reply error from host %"INT32": %s. "
		    "spidering will be paused.",
		    h->m_hostId,mstrerror(g_errno));
		// just clear it
		g_errno = 0;
		// if never had any reply... can't be valid then
		if ( ! ptr ) s_validReplies--;
		/*
		// just use his last known good reply
		ptr = (CrawlInfo *)h->m_lastKnownGoodCrawlInfoReply;
		end = (CrawlInfo *)h->m_lastKnownGoodCrawlInfoReplyEnd;
		*/
	}
	// otherwise, if reply was good it is the last known good now!
	/*
	else {
		
		// free the old good one and replace it with the new one
		if ( h->m_lastKnownGoodCrawlInfoReply ) {
			//log("spider: skiipping possible bad free!!!! until we fix");
			mfree ( h->m_lastKnownGoodCrawlInfoReply , 
				h->m_replyAllocSize , 
				"lknown" );
		}
		// add in the new good in case he goes down in the future
		h->m_lastKnownGoodCrawlInfoReply    = (char *)ptr;
		h->m_lastKnownGoodCrawlInfoReplyEnd = (char *)end;
		// set new alloc size
		h->m_replyAllocSize = allocSize;
		// if valid, don't let him free it now!
		slot->m_readBuf = NULL;
	}
	*/

	// inc it
	s_replies++;

	if ( s_replies > s_requests ) { char *xx=NULL;*xx=0; }


	// crap, if any host is dead and not reporting it's number then
	// that seriously fucks us up because our global count will drop
	// and something that had hit a max limit, like maxToCrawl, will
	// now be under the limit and the crawl will resume.
	// what's the best way to fix this?
	//
	// perhaps, let's just keep the dead host's counts the same
	// as the last time we got them. or maybe the simplest way is to
	// just not allow spidering if a host is dead 

	// the sendbuf should never be freed! it points into collrec
	// it is 'i' or 'f' right now
	slot->m_sendBufAlloc = NULL;

	/////
	//  SCAN the list of CrawlInfos we received from this host, 
	//  one for each non-null collection
	/////

	// . add the LOCAL stats we got from the remote into the GLOBAL stats
	// . readBuf is null on an error, so check for that...
	// . TODO: do not update on error???
	for ( ; ptr < end ; ptr++ ) {

		QUICKPOLL ( slot->m_niceness );

		// get collnum
		collnum_t collnum = (collnum_t)(ptr->m_collnum);

		CollectionRec *cr = g_collectiondb.getRec ( collnum );
		if ( ! cr ) {
			log("spider: updatecrawlinfo collnum %"INT32" "
			    "not found",(int32_t)collnum);
			continue;
		}
		
		//CrawlInfo *stats = ptr;

		// just copy into the stats buf
		if ( ! cr->m_crawlInfoBuf.getBufStart() ) {
			int32_t need = sizeof(CrawlInfo) * g_hostdb.m_numHosts;
			cr->m_crawlInfoBuf.setLabel("cibuf");
			cr->m_crawlInfoBuf.reserve(need);
			// in case one was udp server timed out or something
			cr->m_crawlInfoBuf.zeroOut();
		}

		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		if ( cia )
			gbmemcpy ( &cia[h->m_hostId] , ptr , sizeof(CrawlInfo));
		
		// debug
		// log("spd: got ci from host %"INT32" downloads=%"INT64", replies=%"INT32"",
		//     h->m_hostId,
		//     ptr->m_pageDownloadSuccessesThisRound,
		//     s_replies
		//     );

		// mark it for computation once we got all replies
		cr->m_updateRoundNum = s_updateRoundNum;
	}

	// keep going until we get all replies
	if ( s_replies < s_requests ) return;
	
	// if it's the last reply we are to receive, and 1 or more 
	// hosts did not have a valid reply, and not even a
	// "last known good reply" then then we can't do
	// much, so do not spider then because our counts could be
	// way off and cause us to start spidering again even though
	// we hit a maxtocrawl limit!!!!!
	if ( s_validReplies < s_replies ) {
		// this will tell us to halt all spidering
		// because a host is essentially down!
		s_countsAreValid = false;
		// might as well stop the loop here since we are
		// not updating our crawlinfo states.
		//break;
	}
	else {
		if ( ! s_countsAreValid )
			log("spider: got all crawlinfo replies. all shards "
			    "up. spidering back on.");
		s_countsAreValid = true;
	}


	// loop over 
	for ( int32_t x = 0 ; x < g_collectiondb.m_numRecs ; x++ ) {

		QUICKPOLL ( slot->m_niceness );

		// a niceness 0 routine could have nuked it?
		if ( x >= g_collectiondb.m_numRecs )
			break;

		CollectionRec *cr = g_collectiondb.m_recs[x];
		if ( ! cr ) continue;

		// must be in need of computation
		if ( cr->m_updateRoundNum != s_updateRoundNum ) continue;

		//log("spider: processing c=%s",cr->m_coll);

		CrawlInfo *gi = &cr->m_globalCrawlInfo;

		int32_t hadUrlsReady = gi->m_hasUrlsReadyToSpider;

		// clear it out
		gi->reset();

		// retrieve stats for this collection and scan all hosts
		CrawlInfo *cia = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();

		// if empty for all hosts, i guess no stats...
		if ( ! cia ) continue;

		for ( int32_t k = 0 ; k < g_hostdb.m_numHosts; k++ ) {
			QUICKPOLL ( slot->m_niceness );
			// get the CrawlInfo for the ith host
			CrawlInfo *stats = &cia[k];
			// point to the stats for that host
			int64_t *ss = (int64_t *)stats;
			int64_t *gs = (int64_t *)gi;
			// add each hosts counts into the global accumulators
			for ( int32_t j = 0 ; j < NUMCRAWLSTATS ; j++ ) {
				*gs = *gs + *ss;
				// crazy stat?
				if ( *ss > 1000000000LL ||
				     *ss < -1000000000LL ) 
					log("spider: crazy stats %"INT64" "
					    "from host #%"INT32" coll=%s",
					    *ss,k,cr->m_coll);
				gs++;
				ss++;
			}
			// . special counts
			// . assume round #'s match!
			//if ( ss->m_spiderRoundNum == 
			//     cr->m_localCrawlInfo.m_spiderRoundNum ) {
			gi->m_pageDownloadSuccessesThisRound +=
				stats->m_pageDownloadSuccessesThisRound;
			gi->m_pageProcessSuccessesThisRound +=
				stats->m_pageProcessSuccessesThisRound;
			//}

			if ( ! stats->m_hasUrlsReadyToSpider ) continue;
			// inc the count otherwise
			gi->m_hasUrlsReadyToSpider++;
			// . no longer initializing?
			// . sometimes other shards get the spider 
			//  requests and not us!!!
			if ( cr->m_spiderStatus == SP_INITIALIZING )
				cr->m_spiderStatus = SP_INPROGRESS;
			// i guess we are back in business even if
			// m_spiderStatus was SP_MAXTOCRAWL or 
			// SP_ROUNDDONE...
			cr->m_spiderStatus = SP_INPROGRESS;
			// unflag the sent flag if we had sent an alert
			// but only if it was a crawl round done alert,
			// not a maxToCrawl or maxToProcess or 
			// maxRounds alert.
			// we can't do this because on startup we end 
			// up setting hasUrlsReadyToSpider to true and
			// we may have already sent an email, and it 
			// gets RESET here when it shouldn't be
			//if(cr->m_localCrawlInfo.m_sentCrawlDoneAlert
			//== SP_ROUNDDONE )
			//cr->m_localCrawlInfo.m_sentCrawlDoneAlert=0;
			// revival?
			if ( ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider )
				log("spider: reviving crawl %s "
				    "from host %"INT32"", cr->m_coll,k);
		} // end loop over hosts


		// log("spider: %"INT64" (%"INT64") total downloads for c=%s",
		//     gi->m_pageDownloadSuccessesThisRound,
		//     gi->m_pageDownloadSuccesses,
		//     cr->m_coll);

		// revival?
		//if ( cr->m_tmpCrawlInfo.m_hasUrlsReadyToSpider &&
		//     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		//	log("spider: reviving crawl %s (%"INT32")",cr->m_coll,
		//	    cr->m_tmpCrawlInfo.m_hasUrlsReadyToSpider);
		//}

		//bool has = cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider;
		if ( hadUrlsReady &&
		     // and it no longer does now...
		     ! cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
			log(LOG_INFO,
			    "spider: all %"INT32" hosts report "
			    "%s (%"INT32") has no "
			    "more urls ready to spider",
			    s_replies,cr->m_coll,(int32_t)cr->m_collnum);
			// set crawl end time
			cr->m_diffbotCrawlEndTime = getTimeGlobalNoCore();
		}


		// now copy over to global crawl info so things are not
		// half ass should we try to read globalcrawlinfo
		// in between packets received.
		//gbmemcpy ( &cr->m_globalCrawlInfo , 
		//	 &cr->m_tmpCrawlInfo ,
		//	 sizeof(CrawlInfo) );

		// turn not assume we are out of urls just yet if a host
		// in the network has not reported...
		//if ( g_hostdb.hasDeadHost() && has )
		//	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = true;
		     

		// should we reset our "sent email" flag?
		bool reset = false;

		// can't reset if we've never sent an email out yet
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) reset = true;
		    
		// must have some urls ready to spider now so we can send
		// another email after another round of spidering
		if (!cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider) reset=false;

		// . if we have urls ready to be spidered then prepare to send
		//   another email/webhook notification.
		// . do not reset this flag if SP_MAXTOCRAWL etc otherwise we 
		//   end up sending multiple notifications, so this logic here
		//   is only for when we are done spidering a round, which 
		//   happens when hasUrlsReadyToSpider goes false for all 
		//   shards.
		if ( reset ) {
			log("spider: resetting sent crawl done alert to 0 "
			    "for coll %s",cr->m_coll);
			cr->m_localCrawlInfo.m_sentCrawlDoneAlert = 0;
		}



		// update cache time
		cr->m_globalCrawlInfo.m_lastUpdateTime = getTime();
		
		// make it save to disk i guess
		cr->m_needsSave = true;

		// if spidering disabled in master controls then send no
		// notifications
		// crap, but then we can not update the round start time
		// because that is done in doneSendingNotification().
		// but why does it say all 32 report done, but then
		// it has urls ready to spider?
		if ( ! g_conf.m_spideringEnabled )
			continue;

		// and we've examined at least one url. to prevent us from
		// sending a notification if we haven't spidered anything
		// because no seed urls have been added/injected.
		//if ( cr->m_globalCrawlInfo.m_urlsConsidered == 0 ) return;
		if ( cr->m_globalCrawlInfo.m_pageDownloadAttempts == 0 &&
		     // if we don't have this here we may not get the
		     // pageDownloadAttempts in time from the host that
		     // did the spidering.
		     ! hadUrlsReady ) 
			continue;

		// if urls were considered and roundstarttime is still 0 then
		// set it to the current time...
		//if ( cr->m_spiderRoundStartTime == 0 )
		//	// all hosts in the network should sync with host #0 
		//      // on this
		//	cr->m_spiderRoundStartTime = getTimeGlobal();

		// but of course if it has urls ready to spider, do not send 
		// alert... or if this is -1, indicating "unknown".
		if ( cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) 
			continue;

		// update status if nto already SP_MAXTOCRAWL, etc. we might
		// just be flat out of urls
		if ( ! cr->m_spiderStatus || 
		     cr->m_spiderStatus == SP_INPROGRESS ||
		     cr->m_spiderStatus == SP_INITIALIZING )
			cr->m_spiderStatus = SP_ROUNDDONE;

		//
		// TODO: set the spiderstatus outright here...
		// maxtocrawl, maxtoprocess, etc. based on the counts.
		//


		// only host #0 sends emails
		if ( g_hostdb.m_myHost->m_hostId != 0 )
			continue;

		// . if already sent email for this, skip
		// . localCrawlInfo stores this value on disk so persistent
		// . we do it this way so SP_ROUNDDONE can be emailed and then
		//   we'd email SP_MAXROUNDS to indicate we've hit the maximum
		//   round count. 
		if ( cr->m_localCrawlInfo.m_sentCrawlDoneAlert ) {
			// debug
			logf(LOG_DEBUG,"spider: already sent alert for %s"
			     , cr->m_coll);
			continue;
		}

		
		// do email and web hook...
		sendNotificationForCollRec ( cr );

		// deal with next collection rec
	}

	// wait for more replies to come in
	//if ( s_replies < s_requests ) return;

	// initialize
	s_replies  = 0;
	s_requests = 0;
	s_validReplies = 0;
	s_inUse    = false;
	s_updateRoundNum++;
}

void handleRequestc1 ( UdpSlot *slot , int32_t niceness ) {
	//char *request = slot->m_readBuf;
	// just a single collnum
	if ( slot->m_readBufSize != 1 ) { char *xx=NULL;*xx=0;}

	char *req = slot->m_readBuf;

	if ( ! slot->m_host ) {
		log("handc1: no slot->m_host from ip=%s udpport=%i",
		    iptoa(slot->m_ip),(int)slot->m_port);
		g_errno = ENOHOSTS;
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	//if ( ! isClockSynced() ) {
	//}

	//collnum_t collnum = *(collnum_t *)request;
	//CollectionRec *cr = g_collectiondb.getRec(collnum);

	// deleted from under us? i've seen this happen
	//if ( ! cr ) {
	//	log("spider: c1: coll deleted returning empty reply");
	//	g_udpServer.sendReply_ass ( "", // reply
	//				    0, 
	//				    0 , // alloc
	//				    0 , //alloc size
	//				    slot );
	//	return;
	//}


	// while we are here update CrawlInfo::m_nextSpiderTime
	// to the time of the next spider request to spider.
	// if doledb is empty and the next rec in the waiting tree
	// does not have a time of zero, but rather, in the future, then
	// return that future time. so if a crawl is enabled we should
	// actively call updateCrawlInfo a collection every minute or
	// so.
	//cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 1;

	//int64_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();
	//int64_t nextSpiderTimeMS;
	// this will be 0 for ip's which have not had their SpiderRequests
	// in spiderdb scanned yet to get the best SpiderRequest, so we
	// just have to wait for that.
	/*
	nextSpiderTimeMS = sc->getEarliestSpiderTimeFromWaitingTree(0); 
	if ( ! sc->m_waitingTreeNeedsRebuild &&
	     sc->m_lastDoledbReadEmpty && 
	     cr->m_spideringEnabled &&
	     g_conf.m_spideringEnabled &&
	     nextSpiderTimeMS > nowGlobalMS +10*60*1000 ) 
		// turn off this flag, "ready queue" is empty
		cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 0;

	// but send back a -1 if we do not know yet because we haven't
	// read the doledblists from disk from all priorities for this coll
	if ( sc->m_numRoundsDone == 0 )
		cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = -1;
	*/

	//int32_t now = getTimeGlobal();
	SafeBuf replyBuf;

	uint32_t now = (uint32_t)getTimeGlobalNoCore();

	uint64_t nowMS = gettimeofdayInMillisecondsGlobalNoCore();

	//SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {

		QUICKPOLL(slot->m_niceness);

		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;

		// shortcut
		CrawlInfo *ci = &cr->m_localCrawlInfo;

		// this is now needed for alignment by the receiver
		ci->m_collnum = i;

		SpiderColl *sc = cr->m_spiderColl;

		/////////
		//
		// ARE WE DONE SPIDERING?????
		//
		/////////

		// speed up qa pipeline
		uint32_t spiderDoneTimer = (uint32_t)SPIDER_DONE_TIMER;
		if ( cr->m_coll[0] == 'q' &&
		     cr->m_coll[1] == 'a' &&
		     strcmp(cr->m_coll,"qatest123")==0)
			spiderDoneTimer = 10;

		// if we haven't spidered anything in 1 min assume the
		// queue is basically empty...
		if ( ci->m_lastSpiderAttempt &&
		     ci->m_lastSpiderCouldLaunch &&
		     ci->m_hasUrlsReadyToSpider &&
		     // the next round we are waiting for, if any, must
		     // have had some time to get urls! otherwise we
		     // will increment the round # and wait just
		     // SPIDER_DONE_TIMER seconds and end up setting
		     // hasUrlsReadyToSpider to false!
		     now > cr->m_spiderRoundStartTime + spiderDoneTimer &&
		     // no spiders currently out. i've seen a couple out
		     // waiting for a diffbot reply. wait for them to
		     // return before ending the round...
		     sc && sc->m_spidersOut == 0 &&
		     // it must have launched at least one url! this should
		     // prevent us from incrementing the round # at the gb
		     // process startup
		     //ci->m_numUrlsLaunched > 0 &&
		     //cr->m_spideringEnabled &&
		     //g_conf.m_spideringEnabled &&
		     ci->m_lastSpiderAttempt - ci->m_lastSpiderCouldLaunch > 
		     spiderDoneTimer ) {

			// break it here for our collnum to see if
			// doledb was just lagging or not.
			bool printIt = true;
			if ( now < sc->m_lastPrinted ) printIt = false;
			if ( printIt ) sc->m_lastPrinted = now + 5;

			// doledb must be empty
			if ( ! sc->m_doleIpTable.isEmpty() ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "doledb not empty for coll=%s",cr->m_coll);
				goto doNotEnd;
			}

			uint64_t nextTimeMS ;
			nextTimeMS = sc->getNextSpiderTimeFromWaitingTree ( );

			// and no ips awaiting scans to get into doledb
			// except for ips needing scans 60+ seconds from now
			if ( nextTimeMS &&  nextTimeMS < nowMS + 60000 ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree key is ready for scan "
				    "%"INT64" ms from now for coll=%s",
				    nextTimeMS - nowMS,cr->m_coll );
				goto doNotEnd;
			}

			// maybe wait for waiting tree population to finish
			if ( sc->m_waitingTreeNeedsRebuild ) {
				if ( printIt )
				log("spider: not ending crawl because "
				    "waiting tree is building for coll=%s",
				    cr->m_coll );
				goto doNotEnd;
			}

			// this is the MOST IMPORTANT variable so note it
			log(LOG_INFO,
			    "spider: coll %s has no more urls to spider",
			    cr->m_coll);
			// assume our crawl on this host is completed i guess
			ci->m_hasUrlsReadyToSpider = 0;
			// if changing status, resend local crawl info to all
			cr->localCrawlInfoUpdate();
			// save that!
			cr->m_needsSave = true;
		}

	doNotEnd:

		int32_t hostId = slot->m_host->m_hostId;

		bool sendIt = false;

		// . if not sent to host yet, send
		// . this will be true when WE startup, not them...
		// . but once we send it we set flag to false
		// . and if we update anything we send we set flag to true
		//   again for all hosts
		if ( cr->shouldSendLocalCrawlInfoToHost(hostId) ) 
			sendIt = true;

		// they can override. if host crashed and came back up
		// it might not have saved the global crawl info for a coll
		// perhaps, at the very least it does not have
		// the correct CollectionRec::m_crawlInfoBuf because we do
		// not save the array of crawlinfos for each host for
		// all collections.
		if ( req && req[0] == 'f' )
			sendIt = true;

		if ( ! sendIt ) continue;

		// note it
		// log("spider: sending ci for coll %s to host %"INT32"",
		//     cr->m_coll,hostId);
		
		// save it
		replyBuf.safeMemcpy ( ci , sizeof(CrawlInfo) );

		// do not re-do it unless it gets update here or in XmlDoc.cpp
		cr->sentLocalCrawlInfoToHost ( hostId );
	}

	g_udpServer.sendReply_ass( replyBuf.getBufStart(), replyBuf.length(), replyBuf.getBufStart(),
							   replyBuf.getCapacity(), slot );

	// udp server will free this
	replyBuf.detachBuf();
}

bool getSpiderStatusMsg ( CollectionRec *cx , SafeBuf *msg , int32_t *status ) {

	if ( ! g_conf.m_spideringEnabled && ! cx->m_isCustomCrawl ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Spidering disabled in "
				       "master controls. You can turn it "
				       "back on there.");
	}

	if ( g_conf.m_readOnlyMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In read-only mode. Spidering off.");
	}

	if ( g_dailyMerge.m_mergeMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Daily merge engaged, spidering "
				       "paused.");
	}

	// if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) {
	// 	*status = SP_ADMIN_PAUSED;
	// 	return msg->safePrintf("Too many UDP slots in use, "
	// 			       "spidering paused.");
	// }

	if ( g_repairMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In repair mode, spidering paused.");
	}

	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Parms not in sync with host #0, "
				       "spidering paused");
	}

	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Hosts.conf discrepancy, "
				       "spidering paused.");
	}

	uint32_t now = (uint32_t)getTimeGlobal();

	// try to fix crawlbot nightly test complaining about job status
	// for TestRepeatCrawlWithMaxToCrawl
	if ( (cx->m_spiderStatus == SP_MAXTOCRAWL ||
	      cx->m_spiderStatus == SP_MAXTOPROCESS ) &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime &&
	     cx->m_spiderRoundNum >= cx->m_maxCrawlRounds ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}		

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( cx->m_spiderStatus == SP_MAXTOCRAWL &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToCrawl limit. "
				       "Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}

	if ( cx->m_spiderStatus == SP_MAXTOPROCESS &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToProcess limit. "
				       "Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}


	if ( cx->m_spiderStatus == SP_MAXTOCRAWL ) {
		*status = SP_MAXTOCRAWL;
		return msg->safePrintf ( "Job has reached maxToCrawl "
					 "limit." );
	}

	if ( cx->m_spiderStatus == SP_MAXTOPROCESS ) {
		*status = SP_MAXTOPROCESS;
		return msg->safePrintf ( "Job has reached maxToProcess "
					 "limit." );
	}

	if ( cx->m_spiderStatus == SP_MAXROUNDS ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}

	if ( ! cx->m_spideringEnabled ) {
		*status = SP_PAUSED;
		if ( cx->m_isCustomCrawl )
			return msg->safePrintf("Job paused.");
		else
			return msg->safePrintf("Spidering disabled "
					       "in spider controls.");
	}

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Next crawl round to start "
				       "in %"INT32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now) );
	}

	// if spiderdb is empty for this coll, then no url
	// has been added to spiderdb yet.. either seed or spot
	//CrawlInfo *cg = &cx->m_globalCrawlInfo;
	//if ( cg->m_pageDownloadAttempts == 0 ) {
	//	*status = SP_NOURLS;
	//	return msg->safePrintf("Crawl is waiting for urls.");
	//}

	if ( cx->m_spiderStatus == SP_INITIALIZING ) {
		*status = SP_INITIALIZING;
		return msg->safePrintf("Job is initializing.");
	}

	// if we had seeds and none were successfully crawled, do not just
	// print that the crawl completed.
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider &&
	     cx->m_globalCrawlInfo.m_pageDownloadAttempts > 0 &&
	     cx->m_globalCrawlInfo.m_pageDownloadSuccesses == 0 ) {
		*status = SP_SEEDSERROR;
		return msg->safePrintf("Failed to crawl any seed.");
	}

	// if we sent an email simply because no urls
	// were left and we are not recrawling!
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		*status = SP_COMPLETED;
		return msg->safePrintf("Job has completed and no "
			"repeat is scheduled.");
	}

	if ( cx->m_spiderStatus == SP_ROUNDDONE && ! cx->m_isCustomCrawl ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}

	// let's pass the qareindex() test in qa.cpp... it wasn't updating
	// the status to done. it kept saying in progress.
	if ( ! cx->m_isCustomCrawl && 
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		//*status = SP_COMPLETED;
		*status = SP_INPROGRESS;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}
		

	if ( cx->m_spiderStatus == SP_ROUNDDONE ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Job round completed.");
	}


	if ( ! g_conf.m_spideringEnabled ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "by root administrator for "
				       "maintenance.");
	}

	// out CollectionRec::m_globalCrawlInfo counts do not have a dead
	// host's counts tallied into it, which could make a difference on
	// whether we have exceed a maxtocrawl limit or some such, so wait...
	if ( ! s_countsAreValid && g_hostdb.hasDeadHost() ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "because a shard is down.");
	}



	// otherwise in progress?
	*status = SP_INPROGRESS;
	if ( cx->m_isCustomCrawl )
		return msg->safePrintf("Job is in progress.");
	else
		return msg->safePrintf("Spider is in progress.");
}

bool hasPositivePattern ( char *pattern ) {
	char *p = pattern;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// skip if negative pattern
		if ( start[0] == '!' && start[1] && start[1]!='|' )
			continue;
		// otherwise it's a positive pattern
		return true;
	}
	return false;
}


// pattern is a ||-separted list of substrings
bool doesStringContainPattern ( char *content , char *pattern ) {
				//bool checkForNegatives ) {

	char *p = pattern;

	int32_t matchedOne = 0;
	bool hadPositive = false;

	int32_t count = 0;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// temp null this
		char c = *end;
		*end = '\0';
		// count it as an attempt
		count++;

		bool matchFront = false;
		if ( start[0] == '^' ) { start++; matchFront = true; }

		// if pattern is NOT/NEGATIVE...
		bool negative = false;
		if ( start[0] == '!' && start[1] && start[1]!='|' ) {
			start++;
			negative = true;
		}
		else
			hadPositive = true;

		// . is this substring anywhere in the document
		// . check the rawest content before converting to utf8 i guess
		// . suuport the ^ operator
		char *foundPtr = NULL;
		if ( matchFront ) {
			// if we match the front, set to bogus 0x01
			if ( strncmp(content,start,end-start)==0 ) 
				foundPtr =(char *)0x01;
		}
		else {
			foundPtr = strstr ( content , start ) ;
		}

		// debug log statement
		//if ( foundPtr )
		//	log("build: page %s matches ppp of \"%s\"",
		//	    m_firstUrl.m_url,start);
		// revert \0
		*end = c;

		// negative mean we should NOT match it
		if ( negative ) {
			// so if its matched, that is bad
			if ( foundPtr ) return false;
			continue;
		}

		// skip if not found
		if ( ! foundPtr ) continue;
		// did we find it?
		matchedOne++;
		// if no negatives, done
		//if ( ! checkForNegatives )
		//return true;
	}
	// if we had no attempts, it is ok
	if ( count == 0 ) return true;
	// must have matched one at least
	if ( matchedOne ) return true;
	// if all negative? i.e. !category||!author
	if ( ! hadPositive ) return true;
	// if we had an unfound substring...
	return false;
}

int32_t getFakeIpForUrl1 ( char *url1 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url1 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}

int32_t getFakeIpForUrl2 ( Url *url2 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url2 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}

// returns false and sets g_errno on error
bool SpiderRequest::setFromAddUrl ( char *url ) {
	
	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: BEGIN. url [%s]", __FILE__, __func__, __LINE__, url);
		
	// reset it
	reset();
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url );

	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	//int32_t firstIp = getFakeIpForUrl1 ( url );

	// ensure not crazy
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;

	// . now fill it up
	// . TODO: calculate the other values... lazy!!! (m_isRSSExt, 
	//         m_siteNumInlinks,...)
	m_isNewOutlink = 1;
	m_isAddUrl     = 1;
	m_addedTime    = (uint32_t)getTimeGlobal();//now;
	m_fakeFirstIp   = 1;
	//m_probDocId     = probDocId;
	m_firstIp       = firstIp;
	m_hopCount      = 0;

	// new: validate it?
	m_hopCountValid = 1;

	// its valid if root
	Url uu; uu.set ( url );
	if ( uu.isRoot() ) m_hopCountValid = true;
	// too big?
	if ( gbstrlen(url) > MAX_URL_LEN ) {
		g_errno = EURLTOOLONG;
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, EURLTOOLONG", __FILE__, __func__, __LINE__);
		return false;
	}
	// the url! includes \0
	strcpy ( m_url , url );
	// call this to set m_dataSize now
	setDataSize();
	// make the key dude -- after setting url
	setKey ( firstIp , 0LL, false );
	// need a fake first ip lest we core!
	//m_firstIp = (pdocId & 0xffffffff);
	// how to set m_firstIp? i guess addurl can be throttled independently
	// of the other urls???  use the hash of the domain for it!
	int32_t  dlen;
	char *dom = getDomFast ( url , &dlen );
	// fake it for this...
	//m_firstIp = hash32 ( dom , dlen );
	// sanity
	if ( ! dom ) {
		g_errno = EBADURL;
		if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, EBADURL", __FILE__, __func__, __LINE__);
		return false;
		//return sendReply ( st1 , true );
	}

	m_domHash32 = hash32 ( dom , dlen );
	// and "site"
	int32_t hlen = 0;
	char *host = getHostFast ( url , &hlen );
	m_siteHash32 = hash32 ( host , hlen );
	m_hostHash32 = m_siteHash32;

	if( g_conf.m_logTraceSpider ) log(LOG_TRACE,"%s:%s:%d: END, done.", __FILE__, __func__, __LINE__);
	return true;
}

bool SpiderRequest::setFromInject ( char *url ) {
	// just like add url
	if ( ! setFromAddUrl ( url ) ) return false;
	// but fix this
	m_isAddUrl = 0;
	m_isInjecting = 1;
	return true;
}


bool SpiderRequest::isCorrupt ( ) {

	// more corruption detection
	if ( m_hopCount < -1 ) {
		log("spider: got corrupt 5 spiderRequest");
		return true;
	}

	if ( m_dataSize > (int32_t)sizeof(SpiderRequest) ) {
		log("spider: got corrupt oversize spiderrequest");
		return true;
	}

	// sanity check. check for http(s)://
	if ( m_url[0] == 'h' && m_url[1]=='t' && m_url[2]=='t' &&
	     m_url[3] == 'p' ) 
		return false;
	// might be a docid from a pagereindex.cpp
	if ( ! is_digit(m_url[0]) ) { 
		log("spider: got corrupt 1 spiderRequest");
		return true;
	}
	// if it is a digit\0 it is ok, not corrupt
	if ( ! m_url[1] )
		return false;
	// if it is not a digit after the first digit, that is bad
	if ( ! is_digit(m_url[1]) ) { 
		log("spider: got corrupt 2 spiderRequest");
		return true;
	}

	return false;
}

