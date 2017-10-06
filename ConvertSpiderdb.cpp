#include "ConvertSpiderdb.h"
#include "sqlite3.h"
#include "Log.h"
#include "Spider.h"
#include "Rdb.h"
#include "types.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "Hostdb.h"


static const char create_table_statmeent[] =
"CREATE TABLE spiderdb ("
"    m_firstIp                       INT NOT NULL,"
"    m_uh48                          INT NOT NULL,"
"    m_hostHash32                    INT NOT NULL,"
"    m_domHash32                     INT NOT NULL,"
"    m_siteHash32                    INT NOT NULL,"
"    m_siteNumInlinks                INT NOT NULL,"
"    m_pageNumInlinks                INT NOT NULL,"
"    m_addedTime                     INT NOT NULL,"
"    m_discoveryTime                 INT NOT NULL,"
"    m_contentHash32                 INT,"
"    m_requestFlags                  INT NOT NULL,"
"    m_priority                      INT,"
"    m_errCount                      INT NOT NULL,"
"    m_sameErrCount                  INT NOT NULL,"
"    m_url                           TEXT NOT NULL,"
"    m_percentChangedPerDay          REAL,"
"    m_spideredTime                  INT,"
"    m_errCode                       INT,"
"    m_httpStatus                    INT,"
"    m_langId                        INT,"
"    m_replyFlags                    INT,"
"    PRIMARY KEY (m_firstIp,m_uh48)"
");"
;

static const char insert_statement_no_reply[] =
"INSERT INTO spiderdb (m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
"		       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
"		       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url)"
"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

static const char insert_statement_with_reply[] =
"INSERT INTO spiderdb (m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
"		       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
"		       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url,"
"                      m_percentChangedPerDay, m_spideredTime, m_errCode, m_httpStatus, m_langId,"
"                      m_replyFlags)"
"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

static const char update_statement_duplicate_request[] =
"UPDATE spiderdb"
"  SET m_siteNumInlinks=MAX(m_siteNumInlinks,?),"
"      m_pageNumInlinks=MAX(m_pageNumInlinks,?),"
"      m_addedTime=MIN(m_addedTime,?),"
"      m_discoveryTime=MIN(m_discoveryTime,?),"
"      m_priority=MAX(m_priority,?)"
"  WHERE m_firstIp=? AND m_uh48=?";


//Convert spiderdb from Rdb format to sqlite3
int convertSpiderDb(const char *collname) {
	if(!g_spiderdb.init())
		return 1;
	if(!g_spiderdb.getRdb()->addRdbBase1(collname))
		return 2;

	collnum_t collnum = g_collectiondb.getRec(collname)->m_collnum;
	
	char collectionDirName[1024];
	sprintf(collectionDirName, "%scoll.%s.%d", g_hostdb.m_dir, collname, (int)collnum);
	
	char sqlitedbName[1024];
	sprintf(sqlitedbName, "%s/spiderdb.sqlite3", collectionDirName);
	if(access(sqlitedbName,F_OK)==0) {
		log(LOG_ERROR,"convert: %s already exists. Aborting conversion", sqlitedbName);
		return 3;
	}
	
	sqlite3 *db = NULL;
	if(sqlite3_open_v2(sqlitedbName,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL) != SQLITE_OK) {
		log(LOG_ERROR,"sqlite3_open_v2: %s",sqlite3_errmsg(db));
		return 4;
	}
	
	char *errmsg = NULL;
	if(sqlite3_exec(db, create_table_statmeent, NULL, NULL, &errmsg) != SQLITE_OK) {
		log(LOG_ERROR,"%s",sqlite3_errmsg(db));
		sqlite3_close(db);
		return 5;
	}
	
	
	sqlite3_stmt *insertStatementNoReply = NULL;
	sqlite3_stmt *insertStatementWithReply = NULL;
	sqlite3_stmt *updateStatementDuplicateRequest = NULL;
	const char *pzTail="";
	
	if(sqlite3_prepare_v2(db, insert_statement_no_reply, sizeof(insert_statement_no_reply)-1, &insertStatementNoReply, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		return 6;
	}
	
	if(sqlite3_prepare_v2(db, insert_statement_with_reply, sizeof(insert_statement_with_reply)-1, &insertStatementWithReply, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		return 7;
	}
	
	if(sqlite3_prepare_v2(db, update_statement_duplicate_request, sizeof(update_statement_duplicate_request)-1, &updateStatementDuplicateRequest, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		return 8;
	}
	
	
	key128_t startKey;
	startKey.setMin();

	Msg5 msg5;
	RdbList list;

	unsigned source_record_count = 0;
	unsigned source_request_count=0, source_reply_count=0, insert_no_reply_count=0, insert_with_reply_count=0, update_duplicate_count=0;
	const SpiderReply *prevSpiderReply = NULL;
	char prevSpiderReplyBuf[sizeof(SpiderReply)+MAX_URL_LEN+100];
	int64_t prevSpiderReplyUrlHash48 = 0LL;
	int64_t prevRequestUh48 = 0;
	int32_t prevRequestFirstip = 0;

	printf("Starting conversion\n");
	for(;;) {
		// use msg5 to get the list, should ALWAYS block since no threads
		if(!msg5.getList(RDB_SPIDERDB,
				 collnum,
				 &list,
				 &startKey,
				 KEYMAX(),
				 10000000,       //minRecSizes
				 true,           //includeTree
				 0,              //startFileNum
				 -1,             //numFiles
				 NULL,           // state
				 NULL,           // callback
				 0,              // niceness
				 false,          // err correction?
				 -1,             // maxRetries
				 false))         // isRealMerge
		{
			log(LOG_LOGIC,"db: getList did not block.");
			return -1;
		}
		// all done if empty
		if(list.isEmpty())
			break;
		
		dedupSpiderdbList(&list);
		
		sqlite3_exec(db,"BEGIN TRANSACTION",NULL,NULL,NULL);
		// loop over entries in list
		for(list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			source_record_count++;
			if((source_record_count%100000) == 0)
				printf("Processed %u records.\n", source_record_count-1);

			const char *srec = list.getCurrentRec();

			if(Spiderdb::isSpiderReply((const key128_t *)srec)) {
				source_reply_count++;
				const SpiderReply *srep = reinterpret_cast<const SpiderReply *>(srec);
				prevSpiderReplyUrlHash48 = srep->getUrlHash48();
				prevSpiderReply = srep;
			} else if(prevRequestFirstip!=Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec)) ||
				  prevRequestUh48!=Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec)))
			{
				source_request_count++;
				const SpiderRequest *spiderRequest = reinterpret_cast<const SpiderRequest*>(srec);

				int64_t uh48 = spiderRequest->getUrlHash48();
				// count how many requests had replies and how many did not
				bool hadReply = prevSpiderReply && (uh48 == prevSpiderReplyUrlHash48);

				if( !hadReply ) {
					// Last reply and current request do not belong together
					prevSpiderReply = NULL;
				}
				
				prevRequestUh48 = spiderRequest->getUrlHash48();
				prevRequestFirstip = Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec));
				
				sqlite3_stmt *stmt = hadReply ? insertStatementWithReply : insertStatementNoReply;
				
				hadReply ? insert_with_reply_count++ : insert_no_reply_count++;
				
				sqlite3_bind_int(stmt, 1, Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int64(stmt, 2, Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int(stmt, 3, spiderRequest->m_hostHash32);
				sqlite3_bind_int(stmt, 4, spiderRequest->m_domHash32);
				sqlite3_bind_int(stmt, 5, spiderRequest->m_siteHash32);
				sqlite3_bind_int(stmt, 6, spiderRequest->m_siteNumInlinks);
				sqlite3_bind_int(stmt, 7, spiderRequest->m_pageNumInlinks);
				sqlite3_bind_int(stmt, 8, spiderRequest->m_addedTime);
				sqlite3_bind_int(stmt, 9, spiderRequest->m_discoveryTime);
				if(spiderRequest->m_contentHash32!=0)
					sqlite3_bind_int(stmt, 10, spiderRequest->m_contentHash32);
				else
					sqlite3_bind_null(stmt, 10);
				int32_t rqf = (spiderRequest->m_recycleContent       ? (1<<0) : 0) |
					      (spiderRequest->m_isAddUrl             ? (1<<1) : 0) |
					      (spiderRequest->m_isPageReindex        ? (1<<2) : 0) |
					      (spiderRequest->m_isUrlCanonical       ? (1<<3) : 0) |
					      (spiderRequest->m_isPageParser         ? (1<<4) : 0) |
					      (spiderRequest->m_urlIsDocId           ? (1<<5) : 0) |
					      (spiderRequest->m_isRSSExt             ? (1<<6) : 0) |
					      (spiderRequest->m_isUrlPermalinkFormat ? (1<<7) : 0) |
					      (spiderRequest->m_forceDelete          ? (1<<8) : 0) |
					      (spiderRequest->m_isInjecting          ? (1<<9) : 0) |
					      (spiderRequest->m_hadReply             ? (1<<10) : 0) |
					      (spiderRequest->m_fakeFirstIp          ? (1<<11) : 0) |
					      (spiderRequest->m_hasAuthorityInlink   ? (1<<12) : 0) |
					      (spiderRequest->m_avoidSpiderLinks     ? (1<<13) : 0);
				sqlite3_bind_int(stmt, 11, rqf);
				if(spiderRequest->m_priority>=0)
					sqlite3_bind_int(stmt, 12, spiderRequest->m_priority);
				else
					sqlite3_bind_null(stmt, 12);
				sqlite3_bind_int(stmt, 13, spiderRequest->m_errCount);
				sqlite3_bind_int(stmt, 14, spiderRequest->m_sameErrCount);
				sqlite3_bind_text(stmt, 15, spiderRequest->m_url,-1,SQLITE_TRANSIENT);
				if(hadReply) {
					sqlite3_bind_double(stmt, 16, prevSpiderReply->m_percentChangedPerDay);
					sqlite3_bind_int(stmt, 17, prevSpiderReply->m_spideredTime);
					sqlite3_bind_int(stmt, 18, prevSpiderReply->m_errCode);
					sqlite3_bind_int(stmt, 19, prevSpiderReply->m_httpStatus);
					sqlite3_bind_int(stmt, 20, prevSpiderReply->m_langId);
					int32_t rpf = (prevSpiderReply->m_isRSS                ? (1<<0) : 0) |
					              (prevSpiderReply->m_isPermalink          ? (1<<1) : 0) |
					              (prevSpiderReply->m_isIndexed            ? (1<<2) : 0) |
					              (prevSpiderReply->m_hasAuthorityInlink   ? (1<<3) : 0) |
					              (prevSpiderReply->m_fromInjectionRequest ? (1<<4) : 0);
					sqlite3_bind_int(stmt, 21, rpf);
				}
				
				if(sqlite3_step(stmt) != SQLITE_DONE) {
					log(LOG_ERROR,"insert error: %s",sqlite3_errmsg(db));
				}
				
				sqlite3_clear_bindings(stmt);
				sqlite3_reset(stmt);
			} else {
				source_request_count++;
				//just update a few fields in the previously inserted row
				update_duplicate_count++;
				const SpiderRequest *spiderRequest = reinterpret_cast<const SpiderRequest*>(srec);
				sqlite3_bind_int(updateStatementDuplicateRequest, 1, spiderRequest->m_siteNumInlinks);
				sqlite3_bind_int(updateStatementDuplicateRequest, 2, spiderRequest->m_pageNumInlinks);
				sqlite3_bind_int(updateStatementDuplicateRequest, 3, spiderRequest->m_addedTime);
				sqlite3_bind_int(updateStatementDuplicateRequest, 4, spiderRequest->m_discoveryTime);
				sqlite3_bind_int(updateStatementDuplicateRequest, 5, spiderRequest->m_priority);
				sqlite3_bind_int(updateStatementDuplicateRequest, 6, Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int64(updateStatementDuplicateRequest, 17, Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec)));
				
				if(sqlite3_step(updateStatementDuplicateRequest) != SQLITE_DONE) {
					log(LOG_ERROR,"update error: %s",sqlite3_errmsg(db));
				}
				
				sqlite3_clear_bindings(updateStatementDuplicateRequest);
				sqlite3_reset(updateStatementDuplicateRequest);
			}
		}
		sqlite3_exec(db,"COMMIT",NULL,NULL,NULL);
		
		//copy prevspiderreply to tmp buf, so we can rememer the value to next list
		if(prevSpiderReply && sizeof(key128_t)+prevSpiderReply->m_dataSize < sizeof(prevSpiderReplyBuf)) {
			memcpy(prevSpiderReplyBuf, prevSpiderReply, sizeof(key128_t)+prevSpiderReply->m_dataSize);
			prevSpiderReply = reinterpret_cast<const SpiderReply*>(prevSpiderReplyBuf);
		} else
			prevSpiderReply = NULL;

		const key128_t *listLastKey = reinterpret_cast<const key128_t *>(list.getLastKey());
		startKey = *listLastKey;
		startKey++;

		// watch out for wrap around
		if ( startKey < *listLastKey)
			break;
	}
	
	printf("source_record_count=%u, source_request_count=%u, source_reply_count=%u, insert_no_reply_count=%u, insert_with_reply_count=%u, update_duplicate_count=%u\n",
	       source_record_count, source_request_count, source_reply_count, insert_no_reply_count, insert_with_reply_count, update_duplicate_count);

	sqlite3_finalize(insertStatementNoReply);
	sqlite3_finalize(insertStatementWithReply);
	sqlite3_finalize(updateStatementDuplicateRequest);
	
	sqlite3_close(db);
	
	return 0;
}
