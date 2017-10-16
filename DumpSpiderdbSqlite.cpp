#include "DumpSpiderdbSqlite.h"
#include "Collectiondb.h"
#include "SpiderdbSqlite.h"
#include "ip.h"
#include <time.h>


static const char *formatTime(time_t when, char buf[32]) {
	struct tm tmp;
	gmtime_r(&when,&tmp);
	strftime(buf,32,"%Y-%m-%dT%H:%M:%SZ",&tmp);
	return buf;
}

static const char *formatRequestFlags(int rf, char *buf) {
	SpiderdbRequestFlags flags(rf);
	sprintf(buf,"%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s",
		flags.m_isAddUrl ? "au":"",
		flags.m_isPageReindex ? "Ri":"",
		flags.m_isUrlCanonical ? "Cn":"",
		flags.m_isPageParser ? "PP":"",
		flags.m_urlIsDocId ? "Di":"",
		flags.m_isRSSExt ? "rs":"",
		flags.m_isUrlPermalinkFormat ? "pl":"",
		flags.m_recycleContent ? "rc":"",
		flags.m_forceDelete ? "fd":"",
		flags.m_forceDelete ? "ij":"",
		flags.m_hadReply ? "rp":"",
		flags.m_fakeFirstIp ? "ff":"",
		flags.m_hasAuthorityInlink ? "ai":"",
		flags.m_hasAuthorityInlinkValid ? "aV":"",
		flags.m_avoidSpiderLinks ? "sl":"");
	return buf;
}


static const char *formatReplyFlags(int rf, char *buf) {
	SpiderdbReplyFlags flags(rf);
	sprintf(buf,"%s|%s|%s|%s|%s",
		flags.m_isRSS ? "rs":"",
		flags.m_isPermalink ? "pl":"",
		flags.m_isIndexed ? "ix":"",
		flags.m_fromInjectionRequest ? "ir":"",
		flags.m_isIndexedINValid ? "iX":"");
	return buf;
}


void dumpSpiderdbSqlite(const char *collname, bool interpret_values) {
	const CollectionRec *cr = g_collectiondb.getRec(collname);
	if(!cr) {
		fprintf(stderr,"Unknown collection: %s\n", collname);
		return;
	}
	
	sqlite3 *db = g_spiderdb_sqlite.getDb(cr->m_collnum);
	if(!db) {
		fprintf(stderr,"Could not open spiderdb for collection: %s (%d)\n", collname, cr->m_collnum);
		return;
	}

	static const char statement_text[] =
		"SELECT m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
		"       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
		"       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url,"
		"       m_percentChangedPerDay, m_spideredTime, m_errCode, m_httpStatus, m_langId,"
		"       m_replyFlags"
		" FROM spiderdb"
		" ORDER BY m_firstIp, m_uh48";
	
	const char *pzTail="";
	sqlite3_stmt *stmt;

	if(sqlite3_prepare_v2(db, statement_text, -1, &stmt, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		g_errno = EBADENGINEER;
		return;
	}
	
	if(interpret_values)
		printf("#%14s,%15s,%10s,%10s,%10s,%5s,%5s,%20s,%20s,%10s,%25s,%3s,%3s,%s,%s,%s,%s,%s,%s,%s,%s\n", "firstip","uh48","hosthash","domhash","sitehash","slink","plink","add-time","discovery-time","chash","reqflags","pri","err","sme","url","pctchange","spidertime","errcode","http","lang","replyflags");
	else
		printf("#%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", "firstip","uh48","hosthash","domhash","sitehash","slink","plink","add-time","discovery-time","chash","reqflags","pri","err","sme","url","pctchange","spidertime","errcode","http","lang","replyflags");
			
	int rc;
	while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
		//fetch all columns. null checks are done later
		int32_t firstIp              = sqlite3_column_int(stmt, 0);
		int64_t uh48                 = sqlite3_column_int64(stmt, 1);
		int32_t hosthash32           = sqlite3_column_int(stmt, 2);
		int32_t domHash32            = sqlite3_column_int(stmt, 3);
		int32_t siteHash32           = sqlite3_column_int(stmt, 4);
		int32_t siteNumInlinks       = sqlite3_column_int(stmt, 5);
		int32_t pageNumInlinks       = sqlite3_column_int(stmt, 6);
		int32_t addedTime            = sqlite3_column_int(stmt, 7);
		int32_t discoveryTime        = sqlite3_column_int(stmt, 8);
		int32_t contentHash32        = sqlite3_column_int(stmt, 9);
		int32_t requestFlags         = sqlite3_column_int(stmt, 10);
		int32_t priority             = sqlite3_column_int(stmt, 11);
		int32_t errCount             = sqlite3_column_int(stmt, 12);
		int32_t sameErrCount         = sqlite3_column_int(stmt, 13);
		const unsigned char *url     = sqlite3_column_text(stmt, 14);
		double percentChangedPerDay  = sqlite3_column_double(stmt, 15);
		int32_t spideredTime         = sqlite3_column_int(stmt, 16);
		int32_t errCode              = sqlite3_column_int(stmt, 17);
		int32_t httpStatus           = sqlite3_column_int(stmt, 18);
		int32_t langId               = sqlite3_column_int(stmt, 19);
		int32_t replyFlags           = sqlite3_column_int(stmt, 20);
		
		char firstIpBuf[16];
		char timebuf[32];
		char requestflagbuf[32];
		char replyflagsbuf[32];
		if(interpret_values)
			printf("%15s,", iptoa(firstIp,firstIpBuf));
		else
			printf("%10u,", firstIp);
		printf("%15lu,", uh48);
		printf("%10u,",hosthash32);
		printf("%10u,",domHash32);
		printf("%10u,",siteHash32);
		printf("%5d,",siteNumInlinks);
		printf("%5u,",pageNumInlinks);
		if(interpret_values)
			printf("%s,",formatTime(addedTime,timebuf));
		else
			printf("%10d,",addedTime);
		if(interpret_values)
			printf("%s,",formatTime(discoveryTime,timebuf));
		else
			printf("%10d,",discoveryTime);
		if(sqlite3_column_type(stmt,9)!=SQLITE_NULL)
			printf("%10u,",contentHash32);
		else
			printf("%10s,","");
		if(interpret_values)
			printf("%-25s,",formatRequestFlags(requestFlags,requestflagbuf));
		else
			printf("%5u,",requestFlags);
		if(sqlite3_column_type(stmt,11)!=SQLITE_NULL)
			printf("%3u,",priority);
		else
			printf("%3s,","");
		printf("%3u,",errCount);
		printf("%3u,",sameErrCount);
		printf("%s,",url);
		if(sqlite3_column_type(stmt,20)!=SQLITE_NULL) {
			printf("%.2f,",percentChangedPerDay);
			if(interpret_values)
				printf("%s,",formatTime(spideredTime,timebuf));
			else
				printf("%u,",spideredTime);
			printf("%u,",errCode);
			printf("%u,",httpStatus);
			printf("%u,",langId);
			if(interpret_values)
				printf("%s",formatReplyFlags(replyFlags,replyflagsbuf));
			else
				printf("%u,",replyFlags);
		} else
			printf(",,,,,,");
		printf("\n");
	}
	sqlite3_finalize(stmt);
}
