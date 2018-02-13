#include "ConvertSpiderdb.h"
#include "sqlite3.h"
#include "Log.h"
#include "Spider.h"
#include "Rdb.h"
#include "types.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "Hostdb.h"
#include "SpiderdbSqlite.h"


static const char create_table_statmeent[] =
"CREATE TABLE spiderdb ("
"    m_firstIp                       INT NOT NULL,"
"    m_uh48                          INT NOT NULL,"
"    m_hostHash32                    INT NOT NULL,"
"    m_domHash32                     INT NOT NULL,"
"    m_siteHash32                    INT NOT NULL,"
"    m_siteNumInlinks                INT,"
"    m_pageNumInlinks                INT NOT NULL,"
"    m_addedTime                     INT NOT NULL,"
"    m_discoveryTime                 INT NOT NULL,"
"    m_contentHash32                 INT,"
"    m_requestFlags                  INT NOT NULL,"
"    m_priority                      INT,"
"    m_errCount                      INT,"
"    m_sameErrCount                  INT,"
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
"                      m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
"                      m_requestFlags, m_priority, m_url)"
"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)";

static const char insert_statement_with_reply[] =
"INSERT INTO spiderdb (m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
"                      m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
"                      m_requestFlags, m_priority, m_url,"
"                      m_errCount, m_sameErrCount, m_percentChangedPerDay, m_spideredTime, m_errCode, m_httpStatus, m_langId,"
"                      m_replyFlags)"
"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";

static const char update_statement_duplicate_request[] =
"UPDATE spiderdb"
"  SET m_siteNumInlinks=FX_MAX(m_siteNumInlinks,?),"
"      m_pageNumInlinks=FX_MAX(m_pageNumInlinks,?),"
"      m_addedTime=MIN(m_addedTime,?),"
"      m_discoveryTime=MIN(m_discoveryTime,?),"
"      m_priority=FX_MAX(m_priority,?)"
"  WHERE m_firstIp=? AND m_uh48=?";

static void fx_max(sqlite3_context *context, int argc, sqlite3_value **argv) {
	int bestIdx = 0;
	int bestIdxValueType = sqlite3_value_type(argv[bestIdx]);
	for (int i = 1; i < argc; ++i) {
		switch (sqlite3_value_type(argv[i])) {
			case SQLITE_INTEGER:
				if (bestIdxValueType == SQLITE_NULL || sqlite3_value_int64(argv[bestIdx]) < sqlite3_value_int64(argv[i])) {
					bestIdx = i;
				}
				break;
			case SQLITE_FLOAT:
				if (bestIdxValueType == SQLITE_NULL || sqlite3_value_double(argv[bestIdx]) < sqlite3_value_double(argv[i])) {
					bestIdx = i;
				}
				break;
			case SQLITE_NULL:
				// do nothing
				break;
			default:
				gbshutdownLogicError();
		}
	}

	sqlite3_result_value(context, argv[bestIdx]);
}

//Convert spiderdb from Rdb format to sqlite3
int convertSpiderDb(const char *collname) {
	if(!g_spiderdb.init())
		return 1;
	if(!g_spiderdb.getRdb_deprecated()->addRdbBase1(collname))
		return 2;

	collnum_t collnum = g_collectiondb.getRec(collname)->m_collnum;
	
	char collectionDirName[1024];
	sprintf(collectionDirName, "%scoll.%s.%d", g_hostdb.m_dir, collname, (int)collnum);
	
	char temporarySqlitedbName[1024];
	char finalSqlitedbName[1024];
	sprintf(temporarySqlitedbName, "%s/spiderdb.sqlite3.new", collectionDirName);
	sprintf(finalSqlitedbName, "%s/spiderdb.sqlite3", collectionDirName);
	if(access(finalSqlitedbName,F_OK)==0) {
		log(LOG_ERROR,"convert: %s already exists. Aborting conversion", finalSqlitedbName);
		return 3;
	}
	if(::unlink(temporarySqlitedbName)!=0 && errno!=ENOENT) {
		log(LOG_ERROR,"convert: %s exists and could not be removed. Aborting conversion", temporarySqlitedbName);
		return 3;
	}
	
	
	sqlite3 *db = NULL;
	if(sqlite3_open_v2(temporarySqlitedbName,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL) != SQLITE_OK) {
		log(LOG_ERROR,"convertsqlite3_open_v2: %s: %s", temporarySqlitedbName, sqlite3_errmsg(db));
		return 4;
	}
	
	char *errmsg = NULL;
	if(sqlite3_exec(db, create_table_statmeent, NULL, NULL, &errmsg) != SQLITE_OK) {
		log(LOG_ERROR,"convert: sqlite3_exec: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 5;
	}

	if (sqlite3_create_function(db, "fx_max", -1, (SQLITE_UTF8 | SQLITE_DETERMINISTIC), nullptr, &fx_max, nullptr, nullptr) != SQLITE_OK) {
		sqlite3_close(db);
		return 12;
	}
	
	sqlite3_stmt *insertStatementNoReply = NULL;
	sqlite3_stmt *insertStatementWithReply = NULL;
	sqlite3_stmt *updateStatementDuplicateRequest = NULL;
	const char *pzTail="";
	
	if(sqlite3_prepare_v2(db, insert_statement_no_reply, sizeof(insert_statement_no_reply)-1, &insertStatementNoReply, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		(void)::unlink(temporarySqlitedbName);
		return 6;
	}
	
	if(sqlite3_prepare_v2(db, insert_statement_with_reply, sizeof(insert_statement_with_reply)-1, &insertStatementWithReply, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		(void)::unlink(temporarySqlitedbName);
		return 7;
	}
	
	if(sqlite3_prepare_v2(db, update_statement_duplicate_request, sizeof(update_statement_duplicate_request)-1, &updateStatementDuplicateRequest, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		(void)::unlink(temporarySqlitedbName);
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
		if(!msg5.getList(RDB_SPIDERDB_DEPRECATED,
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
			goto abort_conversion;
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
				
				sqlite3_bind_int64(stmt, 1, (uint32_t)Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int64(stmt, 2, Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int(stmt, 3, spiderRequest->m_hostHash32);
				sqlite3_bind_int(stmt, 4, spiderRequest->m_domHash32);
				sqlite3_bind_int(stmt, 5, spiderRequest->m_siteHash32);
				if (spiderRequest->m_siteNumInlinksValid) {
					sqlite3_bind_int(stmt, 6, spiderRequest->m_siteNumInlinks);
				} else {
					sqlite3_bind_null(stmt, 6);
				}
				sqlite3_bind_int(stmt, 7, spiderRequest->m_pageNumInlinks);
				sqlite3_bind_int(stmt, 8, spiderRequest->m_addedTime);
				sqlite3_bind_int(stmt, 9, spiderRequest->m_discoveryTime);
				if(spiderRequest->m_contentHash32!=0)
					sqlite3_bind_int(stmt, 10, spiderRequest->m_contentHash32);
				else
					sqlite3_bind_null(stmt, 10);
				SpiderdbRequestFlags rqf;
				rqf.m_recycleContent = spiderRequest->m_recycleContent;
				rqf.m_isAddUrl = spiderRequest->m_isAddUrl;
				rqf.m_isPageReindex = spiderRequest->m_isPageReindex;
				rqf.m_isUrlCanonical = spiderRequest->m_isUrlCanonical;
				rqf.m_isPageParser = spiderRequest->m_isPageParser;
				rqf.m_urlIsDocId = spiderRequest->m_urlIsDocId;
				rqf.m_isRSSExt = spiderRequest->m_isRSSExt;
				rqf.m_isUrlPermalinkFormat = spiderRequest->m_isUrlPermalinkFormat;
				rqf.m_forceDelete = spiderRequest->m_forceDelete;
				rqf.m_isInjecting = spiderRequest->m_isInjecting;
				rqf.m_hadReply = spiderRequest->m_hadReply;
				rqf.m_fakeFirstIp = spiderRequest->m_fakeFirstIp;
				rqf.m_hasAuthorityInlink = spiderRequest->m_hasAuthorityInlink;
				rqf.m_hasAuthorityInlinkValid = spiderRequest->m_hasAuthorityInlinkValid;
				rqf.m_avoidSpiderLinks = spiderRequest->m_avoidSpiderLinks;
				sqlite3_bind_int(stmt, 11, (int)rqf);
				if(spiderRequest->m_priority>=0)
					sqlite3_bind_int(stmt, 12, spiderRequest->m_priority);
				else
					sqlite3_bind_null(stmt, 12);
				sqlite3_bind_text(stmt, 13, spiderRequest->m_url,-1,SQLITE_TRANSIENT);
				if(hadReply) {
					sqlite3_bind_int(stmt, 14, prevSpiderReply->m_errCount);
					sqlite3_bind_int(stmt, 15, prevSpiderReply->m_sameErrCount);
					sqlite3_bind_double(stmt, 16, prevSpiderReply->m_percentChangedPerDay);
					sqlite3_bind_int(stmt, 17, prevSpiderReply->m_spideredTime);
					sqlite3_bind_int(stmt, 18, prevSpiderReply->m_errCode);
					sqlite3_bind_int(stmt, 19, prevSpiderReply->m_httpStatus);
					sqlite3_bind_int(stmt, 20, prevSpiderReply->m_langId);
					SpiderdbReplyFlags rpf;
					rpf.m_isRSS = prevSpiderReply->m_isRSS;
					rpf.m_isPermalink = prevSpiderReply->m_isPermalink;
					rpf.m_isIndexed = prevSpiderReply->m_isIndexed;
					rpf.m_fromInjectionRequest = prevSpiderReply->m_fromInjectionRequest;
					rpf.m_isIndexedINValid = prevSpiderReply->m_isIndexedINValid;
					sqlite3_bind_int(stmt, 21, (int)rpf);
				}
				
				if(sqlite3_step(stmt) != SQLITE_DONE) {
					int err = sqlite3_errcode(db);
					log(LOG_ERROR,"insert error, err=%d: %s",err,sqlite3_errstr(err));
					goto abort_conversion;
				}
				
				sqlite3_clear_bindings(stmt);
				sqlite3_reset(stmt);
			} else {
				source_request_count++;
				//just update a few fields in the previously inserted row
				update_duplicate_count++;
				const SpiderRequest *spiderRequest = reinterpret_cast<const SpiderRequest*>(srec);
				if (spiderRequest->m_siteNumInlinksValid) {
					sqlite3_bind_int(updateStatementDuplicateRequest, 1, spiderRequest->m_siteNumInlinks);
				} else {
					sqlite3_bind_null(updateStatementDuplicateRequest, 1);
				}
				sqlite3_bind_int(updateStatementDuplicateRequest, 2, spiderRequest->m_pageNumInlinks);
				sqlite3_bind_int(updateStatementDuplicateRequest, 3, spiderRequest->m_addedTime);
				sqlite3_bind_int(updateStatementDuplicateRequest, 4, spiderRequest->m_discoveryTime);
				sqlite3_bind_int(updateStatementDuplicateRequest, 5, spiderRequest->m_priority);
				sqlite3_bind_int64(updateStatementDuplicateRequest, 6, (uint32_t)Spiderdb::getFirstIp(reinterpret_cast<const key128_t*>(srec)));
				sqlite3_bind_int64(updateStatementDuplicateRequest, 17, Spiderdb::getUrlHash48(reinterpret_cast<const key128_t*>(srec)));
				
				if(sqlite3_step(updateStatementDuplicateRequest) != SQLITE_DONE) {
					int err = sqlite3_errcode(db);
					log(LOG_ERROR,"update error, err=%d: %s",err,sqlite3_errstr(err));
					goto abort_conversion;
				}
				
				sqlite3_clear_bindings(updateStatementDuplicateRequest);
				sqlite3_reset(updateStatementDuplicateRequest);
			}
		}
		if(sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)!=SQLITE_OK) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"commit error, err=%d: %s",err,sqlite3_errstr(err));
			goto abort_conversion;
		}
		
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
	
	if(::rename(temporarySqlitedbName,finalSqlitedbName)!=0) {
		log(LOG_ERROR,"Could not rename %s to %s: %d (%s)", temporarySqlitedbName, finalSqlitedbName, errno, strerror(errno));
		(void)::unlink(temporarySqlitedbName);
		return 10;
	}
	
	return 0;

abort_conversion:
	sqlite3_finalize(insertStatementNoReply);
	sqlite3_finalize(insertStatementWithReply);
	sqlite3_finalize(updateStatementDuplicateRequest);
	sqlite3_close(db);
	(void)::unlink(temporarySqlitedbName);
	return 11;
}
