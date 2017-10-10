#include "SpiderdbRdbSqliteBridge.h"
#include "Spider.h"
#include "SpiderdbSqlite.h"
#include "types.h"
#include "Sanity.h"
#include "Log.h"
#include "IOBuffer.h"
#include "Mem.h"
#include "Conf.h"


static bool addRequestRecord(sqlite3 *db, const void *record, size_t record_len);
static bool addReplyRecord(sqlite3 *db, const void *record, size_t record_len);


bool SpiderdbRdbSqliteBridge::addRecord(collnum_t collnum, const void *record, size_t record_len) {
	if(KEYNEG((const char*)record)) {
		log(LOG_ERROR,"sqlitespider: Got negative spiderrecord");
		gbshutdownCorrupted();
	}
	sqlite3 *db = g_spiderdb_sqlite.getOrCreateDb(collnum);
	if(!db) {
		log(LOG_ERROR,"sqlitespider: Could not get sqlite db for collection %d", collnum);
		return false;
	}
	if(Spiderdb::isSpiderRequest(reinterpret_cast<const key128_t *>(record)))
		return addRequestRecord(db,record,record_len);
	else
		return addReplyRecord(db,record,record_len);
}


static bool addRequestRecord(sqlite3 *db, const void *record, size_t record_len) {
	if(record_len<(unsigned)SpiderRequest::getNeededSize(0)) {
		log(LOG_ERROR,"sqlitespider: Got spiderrequest with record_len=%zu and SpiderRequest::getNeededSize(0)=%d", record_len, SpiderRequest::getNeededSize(0));
		gbshutdownCorrupted();
	}
	//last byte should be the terminating NUL in m_url
	if(reinterpret_cast<const char*>(record)[record_len-1] != '\0') {
		log(LOG_ERROR,"sqlitespider: Got spiderrequest where last byte was not ascii-nul");
		gbshutdownCorrupted();
	}
	
	const SpiderRequest *sreq = reinterpret_cast<const SpiderRequest*>(record);
	int32_t firstIp = Spiderdb::getFirstIp(&sreq->m_key);
	int64_t uh48 = Spiderdb::getUrlHash48(&sreq->m_key);
	
	//Create or update record. Possible streategies:
	//  insert-then-detect-unique-key-violatione-and-update
	//  select-then-insert-or-update
	//We go for select-then-insert-or-update
	const char *pzTail="";
	sqlite3_stmt *selectStatement = NULL;
	if(sqlite3_prepare_v2(db, "select 1 from spiderdb where m_firstIp=? and m_uh48=?", -1, &selectStatement, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		sqlite3_close(db);
		return false;
	}
	
	sqlite3_bind_int(selectStatement, 1, firstIp);
	sqlite3_bind_int64(selectStatement, 2, uh48);
	int select_rc = sqlite3_step(selectStatement);
	if(select_rc==SQLITE_DONE) {
		//statement is finished - so the record currently doesn't exist
		static const char insert_statement[] =
			"INSERT INTO spiderdb (m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
			"		       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
			"		       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url)"
			"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
		sqlite3_stmt *insertStatement = NULL;
		if(sqlite3_prepare_v2(db, insert_statement, -1, &insertStatement, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			sqlite3_finalize(selectStatement);
			return false;
		}
		
		sqlite3_bind_int(insertStatement, 1, firstIp);
		sqlite3_bind_int64(insertStatement, 2, uh48);
		sqlite3_bind_int(insertStatement, 3, sreq->m_hostHash32);
		sqlite3_bind_int(insertStatement, 4, sreq->m_domHash32);
		sqlite3_bind_int(insertStatement, 5, sreq->m_siteHash32);
		sqlite3_bind_int(insertStatement, 6, sreq->m_siteNumInlinks);
		sqlite3_bind_int(insertStatement, 7, sreq->m_pageNumInlinks);
		sqlite3_bind_int(insertStatement, 8, sreq->m_addedTime);
		sqlite3_bind_int(insertStatement, 9, sreq->m_discoveryTime);
		if(sreq->m_contentHash32!=0)
			sqlite3_bind_int(insertStatement, 10, sreq->m_contentHash32);
		else
			sqlite3_bind_null(insertStatement, 10);
		int32_t rqf = (sreq->m_recycleContent       ? (1<<0) : 0) |
				(sreq->m_isAddUrl             ? (1<<1) : 0) |
				(sreq->m_isPageReindex        ? (1<<2) : 0) |
				(sreq->m_isUrlCanonical       ? (1<<3) : 0) |
				(sreq->m_isPageParser         ? (1<<4) : 0) |
				(sreq->m_urlIsDocId           ? (1<<5) : 0) |
				(sreq->m_isRSSExt             ? (1<<6) : 0) |
				(sreq->m_isUrlPermalinkFormat ? (1<<7) : 0) |
				(sreq->m_forceDelete          ? (1<<8) : 0) |
				(sreq->m_isInjecting          ? (1<<9) : 0) |
				(sreq->m_hadReply             ? (1<<10) : 0) |
				(sreq->m_fakeFirstIp          ? (1<<11) : 0) |
				(sreq->m_hasAuthorityInlink   ? (1<<12) : 0) |
				(sreq->m_avoidSpiderLinks     ? (1<<13) : 0);
		sqlite3_bind_int(insertStatement, 11, rqf);
		if(sreq->m_priority>=0)
			sqlite3_bind_int(insertStatement, 12, sreq->m_priority);
		else
			sqlite3_bind_null(insertStatement, 12);
		sqlite3_bind_int(insertStatement, 13, sreq->m_errCount);
		sqlite3_bind_int(insertStatement, 14, sreq->m_sameErrCount);
		sqlite3_bind_text(insertStatement, 15, sreq->m_url,-1,SQLITE_TRANSIENT);
		
		if(sqlite3_step(insertStatement) != SQLITE_DONE) {
			log(LOG_ERROR,"sqlitespider: Insert error: %s",sqlite3_errmsg(db));
			sqlite3_finalize(insertStatement);
			sqlite3_finalize(selectStatement);
			return false;
		}
		sqlite3_finalize(insertStatement);
		sqlite3_finalize(selectStatement);
		return true;
	} else if(select_rc==SQLITE_ROW) {
		//at least one result, so the record must already be there
		static const char update_statement[] =
			"UPDATE spiderdb"
			"  SET m_siteNumInlinks=MAX(m_siteNumInlinks,?),"
			"      m_pageNumInlinks=MAX(m_pageNumInlinks,?),"
			"      m_addedTime=MIN(m_addedTime,?),"
			"      m_discoveryTime=MIN(m_discoveryTime,?),"
			"      m_priority=MAX(m_priority,?)"
			"  WHERE m_firstIp=? AND m_uh48=?";
		
		sqlite3_stmt *updateStatement = NULL;
		if(sqlite3_prepare_v2(db, update_statement, -1, &updateStatement, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			sqlite3_finalize(selectStatement);
			return false;
		}
		
		sqlite3_bind_int(updateStatement, 1, sreq->m_siteNumInlinks);
		sqlite3_bind_int(updateStatement, 2, sreq->m_pageNumInlinks);
		sqlite3_bind_int(updateStatement, 3, sreq->m_addedTime);
		sqlite3_bind_int(updateStatement, 4, sreq->m_discoveryTime);
		sqlite3_bind_int(updateStatement, 5, sreq->m_priority);
		sqlite3_bind_int(updateStatement, 6, firstIp);
		sqlite3_bind_int64(updateStatement, 17, uh48);
		
		if(sqlite3_step(updateStatement) != SQLITE_DONE) {
			log(LOG_ERROR,"sqlitespider: Update error: %s",sqlite3_errmsg(db));
			sqlite3_finalize(updateStatement);
			sqlite3_finalize(selectStatement);
			return false;
		}
		return true;
	} else {
		log(LOG_WARN,"sqlitespider: sqlite3_step(...select...) failed with %s", sqlite3_errmsg(db));
		sqlite3_finalize(selectStatement);
		return false;
	}
}


static bool addReplyRecord(sqlite3 *db, const void *record, size_t record_len) {
	if(record_len!=sizeof(SpiderReply)) {
		log(LOG_ERROR,"sqlitespider: Got spiderreply with record_len=%zu and sizeof(SpiderReply)=%zu", record_len, sizeof(SpiderReply));
		gbshutdownCorrupted();
	}
	
	//assumption: the record is already there

	const SpiderReply *srep = reinterpret_cast<const SpiderReply*>(record);
	int32_t firstIp = Spiderdb::getFirstIp(&srep->m_key);
	int64_t uh48 = Spiderdb::getUrlHash48(&srep->m_key);

	const char *pzTail="";
	if(srep->m_errCode==0) {
		static const char update_statement[] =
			"UPDATE spiderdb"
			"  SET m_percentChangedPerDay = ?,"
			"      m_spideredTime = ?,"
			"      m_errCode = ?,"
			"      m_httpStatus = ?,"
			"      m_langId = ?,"
			"      m_replyFlags = ?,"
			"      m_errCount = 0,"
			"      m_sameErrCount = 0,"
			"      m_contentHash32 = ?"
			"  WHERE m_firstIp=? and m_uh48=?";
		sqlite3_stmt *updateStatement = NULL;
		if(sqlite3_prepare_v2(db, update_statement, -1, &updateStatement, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			return false;
		}
		
		sqlite3_bind_double(updateStatement, 1, srep->m_percentChangedPerDay);
		sqlite3_bind_int(updateStatement, 2, srep->m_spideredTime);
		sqlite3_bind_int(updateStatement, 3, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 4, srep->m_httpStatus);
		sqlite3_bind_int(updateStatement, 5, srep->m_langId);
		int32_t rpf = (srep->m_isRSS                ? (1<<0) : 0) |
			      (srep->m_isPermalink          ? (1<<1) : 0) |
			      (srep->m_isIndexed            ? (1<<2) : 0) |
			      (srep->m_hasAuthorityInlink   ? (1<<3) : 0) |
			      (srep->m_fromInjectionRequest ? (1<<4) : 0);
		sqlite3_bind_int(updateStatement, 6, rpf);
		sqlite3_bind_int(updateStatement, 7, srep->m_contentHash32);
		sqlite3_bind_int(updateStatement, 8, firstIp);
		sqlite3_bind_int64(updateStatement, 9, uh48);
		
		if(sqlite3_step(updateStatement) != SQLITE_DONE) {
			log(LOG_ERROR,"sqlitespider: Update error: %s",sqlite3_errmsg(db));
			sqlite3_finalize(updateStatement);
			return false;
		}
		sqlite3_finalize(updateStatement);
		return true;
	} else {
		static const char update_statement[] =
			"UPDATE spiderdb"
			"  SET m_spideredTime = ?,"
			"      m_errCode = ?,"
			"      m_httpStatus = ?,"
			"      m_errCount = m_errCount + 1,"
			"      m_sameErrCount = CASE WHEN m_errCode=? THEN IFNULL(m_sameErrCount,0) + 1 ELSE 0 END"
			"  WHERE m_firstIp=? and m_uh48=?";
		sqlite3_stmt *updateStatement = NULL;
		if(sqlite3_prepare_v2(db, update_statement, -1, &updateStatement, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			return false;
		}
		
		sqlite3_bind_int(updateStatement, 1, srep->m_spideredTime);
		sqlite3_bind_int(updateStatement, 2, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 3, srep->m_httpStatus);
		sqlite3_bind_int(updateStatement, 4, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 5, firstIp);
		sqlite3_bind_int64(updateStatement, 6, uh48);
		
		if(sqlite3_step(updateStatement) != SQLITE_DONE) {
			log(LOG_ERROR,"sqlitespider: Update error: %s",sqlite3_errmsg(db));
			sqlite3_finalize(updateStatement);
			return false;
		}
		sqlite3_finalize(updateStatement);
		return true;
	}
}



bool SpiderdbRdbSqliteBridge::getList(collnum_t       collnum,
				      RdbList        *list,
				      const key128_t &startKey,
				      const key128_t &endKey,
				      int32_t         minRecSizes)
{
	sqlite3 *db = g_conf.m_readOnlyMode ? g_spiderdb_sqlite.getDb(collnum) : g_spiderdb_sqlite.getOrCreateDb(collnum);
	if(!db) {
		log(LOG_ERROR,"sqlitespider: Could not get sqlite db for collection %d", collnum);
		g_errno = ENOCOLLREC;
		return false;
	}
	
	int32_t firstIpStart = Spiderdb::getFirstIp(&startKey);
	int32_t firstIpEnd = Spiderdb::getFirstIp(&endKey);
	int64_t uh48Start = Spiderdb::getUrlHash48(&startKey);
	int64_t uh48End = Spiderdb::getUrlHash48(&endKey);
	
	
	bool breakMidIPAddressAllowed;
	const char *pzTail="";
	sqlite3_stmt *stmt;
	if(firstIpStart==firstIpEnd) {
		breakMidIPAddressAllowed = true;
		static const char statement_text[] =
			"SELECT m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
			"       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
			"       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url,"
			"       m_percentChangedPerDay, m_spideredTime, m_errCode, m_httpStatus, m_langId,"
			"       m_replyFlags"
			" FROM spiderdb"
			" WHERE m_firstIp=? and m_uh48>=? and m_uh48<=?"
			" ORDER BY m_firstIp, m_uh48";
		if(sqlite3_prepare_v2(db, statement_text, -1, &stmt, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			g_errno = EBADENGINEER;
			return false;
		}
		sqlite3_bind_int64(stmt, 1, (uint32_t)firstIpStart);
		sqlite3_bind_int64(stmt, 2, uh48Start);
		sqlite3_bind_int64(stmt, 3, uh48End);
	} else {
		if(uh48Start!=0) {
			log(LOG_ERROR, " SpiderdbRdbSqliteBridge::getList(): startip!=endip, and uh48Start!=0");
			gbshutdownLogicError();
		}
		breakMidIPAddressAllowed = false;
		static const char statement_text[] =
			"SELECT m_firstIp, m_uh48, m_hostHash32, m_domHash32, m_siteHash32,"
			"       m_siteNumInlinks, m_pageNumInlinks, m_addedTime, m_discoveryTime, m_contentHash32,"
			"       m_requestFlags, m_priority, m_errCount, m_sameErrCount, m_url,"
			"       m_percentChangedPerDay, m_spideredTime, m_errCode, m_httpStatus, m_langId,"
			"       m_replyFlags"
			" FROM spiderdb"
			" WHERE m_firstIp>=? and m_firstIp<=?"
			" ORDER BY m_firstIp, m_uh48";
		if(sqlite3_prepare_v2(db, statement_text, -1, &stmt, &pzTail) != SQLITE_OK) {
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
			g_errno = EBADENGINEER;
			return false;
		}
		sqlite3_bind_int64(stmt, 1, (uint32_t)firstIpStart);
		sqlite3_bind_int64(stmt, 2, (uint32_t)firstIpEnd);
	}
	
	key128_t listLastKey;
	IOBuffer io_buffer;
	int rc;
	while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
		//fetch all columns. null checks are done later
		int32_t firstIp              = sqlite3_column_int(stmt, 1);
		int64_t uh48                 = sqlite3_column_int64(stmt, 2);
		int32_t hosthash32           = sqlite3_column_int(stmt, 3);
		int32_t domHash32            = sqlite3_column_int(stmt, 4);
		int32_t siteHash32           = sqlite3_column_int(stmt, 5);
		int32_t siteNumInlinks       = sqlite3_column_int(stmt, 6);
		int32_t pageNumInlinks       = sqlite3_column_int(stmt, 7);
		int32_t addedTime            = sqlite3_column_int(stmt, 8);
		int32_t discoveryTime        = sqlite3_column_int(stmt, 9);
		int32_t contentHash32        = sqlite3_column_int(stmt, 10);
		int32_t requestFlags         = sqlite3_column_int(stmt, 11);
		int32_t priority             = sqlite3_column_int(stmt, 12);
		int32_t errCount             = sqlite3_column_int(stmt, 13);
		int32_t sameErrCount         = sqlite3_column_int(stmt, 14);
		const unsigned char *url     = sqlite3_column_text(stmt, 15);
		double percentChangedPerDay  = sqlite3_column_double(stmt, 16);
		int32_t spideredTime         = sqlite3_column_int(stmt, 17);
		int32_t errCode              = sqlite3_column_int(stmt, 18);
		int32_t httpStatus           = sqlite3_column_int(stmt, 19);
		int32_t langId               = sqlite3_column_int(stmt, 20);
		int32_t replyFlags           = sqlite3_column_int(stmt, 21);
		
		
		if(breakMidIPAddressAllowed) {
			if(minRecSizes>0 && io_buffer.used() >= (size_t)minRecSizes)
				break;
		} else {
			
		}
			
		if(sqlite3_column_type(stmt,21)!=SQLITE_NULL) {
			//replyflags are non-null so there must be a reply
			SpiderReply srep;
			srep.reset();
			srep.m_key = Spiderdb::makeKey(firstIp,uh48,false,0,false);
			srep.m_dataSize = sizeof(srep) - sizeof(srep.m_key) - sizeof(srep.m_dataSize);
			srep.m_firstIp                  = firstIp;
			srep.m_siteHash32               = siteHash32;
			srep.m_domHash32                = domHash32;
			srep.m_percentChangedPerDay     = percentChangedPerDay;
			srep.m_spideredTime             = spideredTime;
			srep.m_errCode                  = errCode;
			srep.m_siteNumInlinks           = siteNumInlinks;
			srep.m_sameErrCount             = sameErrCount;
			srep.m_contentHash32            = contentHash32;
			srep.m_crawlDelayMS             = 1; //probably only used in-memory.
			srep.m_downloadEndTime          = 0; //probably only used in-memory.
			srep.m_httpStatus               = httpStatus;
			srep.m_errCount                 = errCount;
			srep.m_langId                   = langId;
			srep.m_isRSS                    = (replyFlags&(1<<0))!=0;
			srep.m_isPermalink              = (replyFlags&(1<<1))!=0;
			srep.m_isIndexed                = (replyFlags&(1<<2))!=0;
			srep.m_hasAuthorityInlink       = (replyFlags&(1<<3))!=0;
			srep.m_fromInjectionRequest     = (replyFlags&(1<<4))!=0; 
			srep.m_isIndexedINValid         = (replyFlags&(1<<4))!=0;
			srep.m_hasAuthorityInlinkValid  = (requestFlags&(1<<15))!=0;
			srep.m_siteNumInlinksValid      = sqlite3_column_type(stmt,6)!=SQLITE_NULL;
			
			io_buffer.reserve_extra(sizeof(srep));
			memcpy(io_buffer.end(), &srep, sizeof(srep));
			io_buffer.push_back(sizeof(srep));
		} else
			replyFlags = 0;
		
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_key = Spiderdb::makeKey(firstIp,uh48,false,0,false);
		//sreq.m_dataSize
		sreq.m_firstIp                  = firstIp;
		sreq.m_hostHash32               = hosthash32;
		sreq.m_domHash32                = domHash32;
		sreq.m_siteHash32               = siteHash32;
		sreq.m_siteNumInlinks           = siteNumInlinks;
		sreq.m_addedTime                = addedTime;
		sreq.m_pageNumInlinks           = pageNumInlinks;
		sreq.m_sameErrCount             = sameErrCount;
		sreq.m_discoveryTime            = discoveryTime;
		sreq.m_prevErrCode              = 0; //done differently now.
		sreq.m_contentHash32            = contentHash32;
		sreq.m_hopCount                 = 0;
		sreq.m_hopCountValid            = 0;
		sreq.m_isAddUrl                 = (requestFlags&(1<<1))!=0;
		sreq.m_isPageReindex            = (requestFlags&(1<<2))!=0;
		sreq.m_isUrlCanonical           = (requestFlags&(1<<3))!=0;
		sreq.m_isPageParser             = (requestFlags&(1<<4))!=0;
		sreq.m_urlIsDocId               = (requestFlags&(1<<5))!=0;
		sreq.m_isRSSExt                 = (requestFlags&(1<<6))!=0;
		sreq.m_isUrlPermalinkFormat     = (requestFlags&(1<<7))!=0;
		sreq.m_recycleContent           = (requestFlags&(1<<0))!=0;
		sreq.m_forceDelete              = (requestFlags&(1<<8))!=0;
		sreq.m_isInjecting              = (requestFlags&(1<<9))!=0;
		sreq.m_hadReply                 = (requestFlags&(1<<10))!=0;
		sreq.m_fakeFirstIp              = (requestFlags&(1<<11))!=0;
		sreq.m_hasAuthorityInlink       = (requestFlags&(1<<12))!=0;
		sreq.m_hasAuthorityInlinkValid  = (requestFlags&(1<<13))!=0;
		sreq.m_siteNumInlinksValid      = sqlite3_column_type(stmt,6)!=SQLITE_NULL;
		sreq.m_avoidSpiderLinks         = (requestFlags&(1<<14))!=0;
		sreq.m_ufn                      = 0; //only used in-memory
		sreq.m_priority                 = priority;
		sreq.m_errCount                 = errCount;
		strcpy(sreq.m_url,(const char*)url);
		sreq.setDataSize();

		io_buffer.reserve_extra(sreq.getRecSize());
		memcpy(io_buffer.end(), &sreq, sreq.getRecSize());
		io_buffer.push_back(sreq.getRecSize());
		
		listLastKey = sreq.m_key;
	}
	if(rc!=SQLITE_DONE && rc!=SQLITE_ROW) {
		log(LOG_ERROR,"sqlitespider: Fetch error: %s",sqlite3_errmsg(db));
		g_errno = EBADENGINEER; //TODO
		return false;
	}
	sqlite3_finalize(stmt);

	
	int32_t listSize = io_buffer.used();
	char *listMemory;
	if(listSize>0) {
		if(!listMemory) {
			log(LOG_ERROR,"sqlitespider: OOM allocating spiderdb rdblist (%d bytes)", listSize);
			return false;
		}
		listMemory = (char*)mmalloc(listSize, "sqliterdblist");
		memcpy(listMemory, io_buffer.begin(), io_buffer.used());
	} else
		listMemory = NULL;
	key128_t listFirstKey = Spiderdb::makeFirstKey(firstIpStart, uh48Start);
	if(rc==SQLITE_ROW) {
		//early break
	} else {
		//select exhaustion, so jump to last specified key
		listLastKey = Spiderdb::makeFirstKey(firstIpEnd, uh48End);
	}
	list->set(listMemory, listSize,
		  listMemory, listSize,
		  (const char*)&listFirstKey, (const char*)&listLastKey,
		  -1,                   //datasize(variable)
		  true,                 //owndata
		  false,                //halfkeys
		  sizeof(key128_t));    //keysize

	return true;
}
