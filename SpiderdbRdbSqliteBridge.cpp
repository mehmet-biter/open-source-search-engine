#include "SpiderdbRdbSqliteBridge.h"
#include "Spider.h"
#include "SpiderdbSqlite.h"
#include "types.h"
#include "Sanity.h"
#include "Log.h"
#include "IOBuffer.h"
#include "Mem.h"
#include "SpiderCache.h"
#include "SpiderColl.h"
#include "Conf.h"
#include <algorithm>


static bool addRecords(SpiderdbSqlite &spiderdb, const std::vector<SpiderdbRdbSqliteBridge::BatchedRecord> &records);
static bool addRecords(SpiderdbSqlite &spiderdb, collnum_t collnum, std::vector<SpiderdbRdbSqliteBridge::BatchedRecord>::const_iterator begin, std::vector<SpiderdbRdbSqliteBridge::BatchedRecord>::const_iterator end);
static bool addRecord(collnum_t collnum, sqlite3 *db, const void *record, size_t record_len);
static bool addRequestRecord(sqlite3 *db, const void *record, size_t record_len);
static bool addReplyRecord(sqlite3 *db, const void *record, size_t record_len);
static int map_sqlite_error_to_gb_errno(int err);

namespace {
class DbTimerLogger {
	const char *name;
	int64_t timing_lock_start;
public:
	DbTimerLogger(const char *name_)
	  : name(name_),
	    timing_lock_start(gettimeofdayInMillisecondsGlobal())
	{}
	~DbTimerLogger() {
		finish();
	}
	void finish() {
		if(name && g_conf.m_logTimingDb) {
			int64_t timing_lock_end = gettimeofdayInMillisecondsGlobal();
			int64_t duration = timing_lock_end-timing_lock_start;
			log(LOG_TIMING,"db:%s: lock: %ld ms", name, duration);
			name = NULL;
		}
	}
};
}


bool SpiderdbRdbSqliteBridge::addRecords(const std::vector<BatchedRecord> &records) {
	return addRecords(g_spiderdb_sqlite, records);
}

bool SpiderdbRdbSqliteBridge::addRecords2(const std::vector<BatchedRecord> &records) {
	return addRecords(g_spiderdb_sqlite2, records);
}

static bool addRecords(SpiderdbSqlite &spiderdb, const std::vector<SpiderdbRdbSqliteBridge::BatchedRecord> &records) {
	//copy&sort
	auto records_copy(records);
	std::sort(records_copy.begin(), records_copy.end(), [](const SpiderdbRdbSqliteBridge::BatchedRecord &a, const SpiderdbRdbSqliteBridge::BatchedRecord &b) {
		return a.collnum < b.collnum;
	});
	
	//find ranges of same collnum, do each range at a time
	auto range_begin = records_copy.begin();
	while(range_begin != records_copy.end()) {
		auto range_end = range_begin;
		while(range_end != records_copy.end()) {
			if(range_end->collnum == range_begin->collnum)
				++range_end;
			else
				break;
		}
		if(!::addRecords(spiderdb, range_begin->collnum, range_begin, range_end))
			return false;
		range_begin = range_end;
	}
	return true;
}


static bool addRecords(SpiderdbSqlite &spiderdb, collnum_t collnum, std::vector<SpiderdbRdbSqliteBridge::BatchedRecord>::const_iterator begin, std::vector<SpiderdbRdbSqliteBridge::BatchedRecord>::const_iterator end) {
	sqlite3 *db = spiderdb.getDb(collnum);
	if(!db) {
		log(LOG_ERROR,"sqlitespider: Could not get sqlite db for collection %d", collnum);
		return false;
	}
	
	DbTimerLogger lock_timer("sqlite-add:lock");
	ScopedSqlitedbLock ssl(db);
	lock_timer.finish();
	
	DbTimerLogger transaction_timer("sqlite-add-trans");
	char *errmsg = NULL;
	int rc = sqlite3_exec(db, "begin transaction", NULL, NULL, &errmsg);
	if(rc!=SQLITE_OK) {
		log(LOG_ERROR,"sqlitespider: could not start transaction: %s", errmsg);
		return false;
	}
	
	long records = 0;
	for(auto iter = begin; iter!=end; ++iter) {
		if(!addRecord(collnum, db, iter->record, iter->record_len)) {
			sqlite3_exec(db, "rollback", NULL, NULL, &errmsg);
			return false;
		}
		records++;
	}
	
	if(sqlite3_exec(db, "commit", NULL, NULL, &errmsg) != SQLITE_OK) {
		int err = sqlite3_errcode(db);
		log(LOG_ERROR,"sqlitespider: commit errror: %s", sqlite3_errstr(err));
		g_errno = map_sqlite_error_to_gb_errno(err);
	}
	transaction_timer.finish();
	if(g_conf.m_logTimingDb)
		log(LOG_TIMING,"db:sqlite-add:record count=%ld",records);
	
	return true;
}


bool SpiderdbRdbSqliteBridge::addRecord(collnum_t collnum, const void *record, size_t record_len) {
	sqlite3 *db = g_spiderdb_sqlite.getDb(collnum);
	if(!db) {
		log(LOG_ERROR,"sqlitespider: Could not get sqlite db for collection %d", collnum);
		return false;
	}
	DbTimerLogger lock_timer("sqlite-add:lock");
	ScopedSqlitedbLock ssl(db);
	lock_timer.finish();
	return addRecord(collnum,db,record,record_len);
}


static bool addRecord(collnum_t collnum, sqlite3 *db, const void *record, size_t record_len) {
	if(KEYNEG((const char*)record)) {
		log(LOG_ERROR,"sqlitespider: Got negative spiderrecord");
		gbshutdownCorrupted();
	}
	
	bool rc;
	if(Spiderdb::isSpiderRequest(reinterpret_cast<const key128_t *>(record)))
		rc = addRequestRecord(db,record,record_len);
	else
		rc = addReplyRecord(db,record,record_len);
	
	//inform the spidercollection that we have just added a record
	if(rc) {
		SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);
		if(sc) {
			if(Spiderdb::isSpiderRequest(reinterpret_cast<const key128_t *>(record)))
				sc->addSpiderRequest(reinterpret_cast<const SpiderRequest*>(record));
			else
				sc->addSpiderReply(reinterpret_cast<const SpiderReply*>(record));
		}
	}
	
	return rc;
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
		int err = sqlite3_errcode(db);
		log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
		g_errno = map_sqlite_error_to_gb_errno(err);
		return false;
	}
	
	sqlite3_bind_int64(selectStatement, 1, (uint32_t)firstIp);
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
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			sqlite3_finalize(selectStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		
		sqlite3_bind_int64(insertStatement, 1, (uint32_t)firstIp);
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
		SpiderdbRequestFlags rqf;
		rqf.m_recycleContent  = sreq->m_recycleContent;
		rqf.m_isAddUrl  = sreq->m_isAddUrl;
		rqf.m_isPageReindex  = sreq->m_isPageReindex;
		rqf.m_isUrlCanonical  = sreq->m_isUrlCanonical;
		rqf.m_isPageParser  = sreq->m_isPageParser;
		rqf.m_urlIsDocId  = sreq->m_urlIsDocId;
		rqf.m_isRSSExt  = sreq->m_isRSSExt;
		rqf.m_isUrlPermalinkFormat  = sreq->m_isUrlPermalinkFormat;
		rqf.m_forceDelete  = sreq->m_forceDelete;
		rqf.m_isInjecting  = sreq->m_isInjecting;
		rqf.m_hadReply  = sreq->m_hadReply;
		rqf.m_fakeFirstIp  = sreq->m_fakeFirstIp;
		rqf.m_hasAuthorityInlink  = sreq->m_hasAuthorityInlink;
		rqf.m_hasAuthorityInlinkValid  = sreq->m_hasAuthorityInlinkValid;
		rqf.m_avoidSpiderLinks  = sreq->m_avoidSpiderLinks;
		sqlite3_bind_int(insertStatement, 11, (int)rqf);
		if(sreq->m_priority>=0)
			sqlite3_bind_int(insertStatement, 12, sreq->m_priority);
		else
			sqlite3_bind_null(insertStatement, 12);
		sqlite3_bind_int(insertStatement, 13, sreq->m_errCount);
		sqlite3_bind_int(insertStatement, 14, sreq->m_sameErrCount);
		sqlite3_bind_text(insertStatement, 15, sreq->m_url,-1,SQLITE_TRANSIENT);
		
		if(sqlite3_step(insertStatement) != SQLITE_DONE) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Insert error: %s", sqlite3_errstr(err));
			sqlite3_finalize(insertStatement);
			sqlite3_finalize(selectStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
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
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			sqlite3_finalize(selectStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
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
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Update error: %s", sqlite3_errstr(err));
			sqlite3_finalize(updateStatement);
			sqlite3_finalize(selectStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		return true;
	} else {
		int err = sqlite3_errcode(db);
		log(LOG_WARN,"sqlitespider: sqlite3_step(...select...) failed with %s", sqlite3_errstr(err));
		sqlite3_finalize(selectStatement);
		g_errno = map_sqlite_error_to_gb_errno(err);
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
	if(srep->m_errCode==EFAKEFIRSTIP || srep->m_errCode==EDOCFORCEDELETE) {
		//To clean up the spider-requests with the fakeip key (and flag) Gb generates spider-replies with a specific
		//error code that tells this logic to delete the equivalent spider-request row
		static const char delete_statement[] =
			"DELETE FROM spiderdb"
			"  WHERE m_firstIp=? and m_uh48=?";
		
		sqlite3_stmt *deleteStatement = NULL;
		if(sqlite3_prepare_v2(db, delete_statement, -1, &deleteStatement, &pzTail) != SQLITE_OK) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		
		sqlite3_bind_int64(deleteStatement, 1, (uint32_t)firstIp);
		sqlite3_bind_int64(deleteStatement, 2, uh48);
		
		if(sqlite3_step(deleteStatement) != SQLITE_DONE) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: delete error: %s",sqlite3_errstr(err));
			sqlite3_finalize(deleteStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		sqlite3_finalize(deleteStatement);
		return true;
	} else if(srep->m_errCode==0) {
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
			"      m_contentHash32 = ?,"
			"      m_requestFlags = (IFNULL(m_requestFlags,0) | ?)"
			"  WHERE m_firstIp=? and m_uh48=?";
		sqlite3_stmt *updateStatement = NULL;
		if(sqlite3_prepare_v2(db, update_statement, -1, &updateStatement, &pzTail) != SQLITE_OK) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		int requestFlagBits = 0;
		if(srep->m_hasAuthorityInlinkValid && srep->m_hasAuthorityInlink) {
			//a bit cumbersome but flexible when we rearrange the bitmasks
			SpiderdbRequestFlags a(0), b(0);
			b.m_hasAuthorityInlink |= true;
			requestFlagBits = ((int)b) - ((int)a);
		}
		
		sqlite3_bind_double(updateStatement, 1, srep->m_percentChangedPerDay);
		sqlite3_bind_int(updateStatement, 2, srep->m_spideredTime);
		sqlite3_bind_int(updateStatement, 3, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 4, srep->m_httpStatus);
		sqlite3_bind_int(updateStatement, 5, srep->m_langId);
		SpiderdbReplyFlags rpf;
		rpf.m_isRSS                = srep->m_isRSS;
		rpf.m_isPermalink          = srep->m_isPermalink;
		rpf.m_isIndexed            = srep->m_isIndexed;
		rpf.m_fromInjectionRequest = srep->m_fromInjectionRequest;
		rpf.m_isIndexedINValid     = srep->m_isIndexedINValid;
		sqlite3_bind_int(updateStatement, 6, (int)rpf);
		sqlite3_bind_int(updateStatement, 7, srep->m_contentHash32);
		sqlite3_bind_int(updateStatement, 8, requestFlagBits);
		sqlite3_bind_int64(updateStatement, 9, (uint32_t)firstIp);
		sqlite3_bind_int64(updateStatement, 10, uh48);
		
		if(sqlite3_step(updateStatement) != SQLITE_DONE) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Update error: %s",sqlite3_errstr(err));
			sqlite3_finalize(updateStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
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
			"      m_sameErrCount = CASE WHEN m_errCode=? THEN IFNULL(m_sameErrCount,0) + 1 ELSE 0 END,"
			"      m_errCode = ?,"
			"      m_replyFlags = IFNULL(m_replyFlags,0)"
			"  WHERE m_firstIp=? and m_uh48=?";
		sqlite3_stmt *updateStatement = NULL;
		if(sqlite3_prepare_v2(db, update_statement, -1, &updateStatement, &pzTail) != SQLITE_OK) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			g_errno = map_sqlite_error_to_gb_errno(err);
			return false;
		}
		
		sqlite3_bind_int(updateStatement, 1, srep->m_spideredTime);
		sqlite3_bind_int(updateStatement, 2, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 3, srep->m_httpStatus);
		sqlite3_bind_int(updateStatement, 4, srep->m_errCode);
		sqlite3_bind_int(updateStatement, 5, srep->m_errCode);
		sqlite3_bind_int64(updateStatement, 6, (uint32_t)firstIp);
		sqlite3_bind_int64(updateStatement, 7, uh48);
		
		if(sqlite3_step(updateStatement) != SQLITE_DONE) {
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Update error: %s",sqlite3_errstr(err));
			sqlite3_finalize(updateStatement);
			g_errno = map_sqlite_error_to_gb_errno(err);
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
	logTrace(g_conf.m_logTraceSpider, "SpiderdbRdbSqliteBridge::getList() ->");
	sqlite3 *db = g_spiderdb_sqlite.getDb(collnum);
	if(!db) {
		log(LOG_ERROR,"sqlitespider: Could not get sqlite db for collection %d", collnum);
		g_errno = ENOCOLLREC;
		return false;
	}
	
	DbTimerLogger lock_timer("sqlite-getlist:lock");
	ScopedSqlitedbLock ssl(db);
	lock_timer.finish();
	
	int32_t firstIpStart = Spiderdb::getFirstIp(&startKey);
	int32_t firstIpEnd = Spiderdb::getFirstIp(&endKey);
	int64_t uh48Start = Spiderdb::getUrlHash48(&startKey);
	int64_t uh48End = Spiderdb::getUrlHash48(&endKey);
	
	DbTimerLogger prepare_timer("sqlite-getlist:prepare");
	bool breakMidIPAddressAllowed;
	const char *pzTail="";
	sqlite3_stmt *stmt;
	if(firstIpStart==firstIpEnd) {
		logTrace(g_conf.m_logTraceSpider, "single ip-range");
		//since we are dealing with just a single ip-address it is fine to cut the data into chunks
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
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			g_errno = EBADENGINEER;
			return false;
		}
		sqlite3_bind_int64(stmt, 1, (uint32_t)firstIpStart);
		sqlite3_bind_int64(stmt, 2, uh48Start);
		sqlite3_bind_int64(stmt, 3, uh48End);
	} else {
		logTrace(g_conf.m_logTraceSpider, "multiple-ip range");
		if(uh48Start!=0) {
			log(LOG_ERROR, " SpiderdbRdbSqliteBridge::getList(): startip!=endip, and uh48Start!=0");
			gbshutdownLogicError();
		}
		//this code is not clever enough to deal with mid-ip breaks when spanning multiple ips
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
			int err = sqlite3_errcode(db);
			log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errstr(err),pzTail);
			g_errno = EBADENGINEER;
			return false;
		}
		sqlite3_bind_int64(stmt, 1, (uint32_t)firstIpStart);
		sqlite3_bind_int64(stmt, 2, (uint32_t)firstIpEnd);
	}
	prepare_timer.finish();
	
	DbTimerLogger read_timer("sqlite-getlist:read");
	key128_t listLastKey;
	IOBuffer io_buffer;
	int rc;
	while((rc=sqlite3_step(stmt))==SQLITE_ROW) {
		//fetch all columns. null checks are done later
		int32_t firstIp                   = sqlite3_column_int(stmt, 0);
		int64_t uh48                      = sqlite3_column_int64(stmt, 1);
		int32_t hosthash32                = sqlite3_column_int(stmt, 2);
		int32_t domHash32                 = sqlite3_column_int(stmt, 3);
		int32_t siteHash32                = sqlite3_column_int(stmt, 4);
		int32_t siteNumInlinks            = sqlite3_column_int(stmt, 5);
		int32_t pageNumInlinks            = sqlite3_column_int(stmt, 6);
		int32_t addedTime                 = sqlite3_column_int(stmt, 7);
		int32_t discoveryTime             = sqlite3_column_int(stmt, 8);
		int32_t contentHash32             = sqlite3_column_int(stmt, 9);
		SpiderdbRequestFlags requestFlags = sqlite3_column_int(stmt, 10);
		int32_t priority                  = sqlite3_column_int(stmt, 11);
		int32_t errCount                  = sqlite3_column_int(stmt, 12);
		int32_t sameErrCount              = sqlite3_column_int(stmt, 13);
		const unsigned char *url          = sqlite3_column_text(stmt, 14);
		double percentChangedPerDay       = sqlite3_column_double(stmt, 15);
		int32_t spideredTime              = sqlite3_column_int(stmt, 16);
		int32_t errCode                   = sqlite3_column_int(stmt, 17);
		int32_t httpStatus                = sqlite3_column_int(stmt, 18);
		int32_t langId                    = sqlite3_column_int(stmt, 19);
		SpiderdbReplyFlags replyFlags     = sqlite3_column_int(stmt, 20);
		
		
		if(breakMidIPAddressAllowed) {
			if(minRecSizes>0 && io_buffer.used() >= (size_t)minRecSizes)
				break;
		} else {
			if(!io_buffer.empty() && Spiderdb::getFirstIp(&listLastKey)!=firstIp) {
				if(minRecSizes>0 && io_buffer.used() >= (size_t)minRecSizes)
					break;
			}
		}
			
		if(sqlite3_column_type(stmt,20)!=SQLITE_NULL) {
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
			srep.m_isRSS                    = replyFlags.m_isRSS;
			srep.m_isPermalink              = replyFlags.m_isPermalink;
			srep.m_isIndexed                = replyFlags.m_isIndexed;
			srep.m_hasAuthorityInlink       = requestFlags.m_hasAuthorityInlink;
			srep.m_fromInjectionRequest     = replyFlags.m_fromInjectionRequest;
			srep.m_isIndexedINValid         = replyFlags.m_isIndexedINValid;
			srep.m_hasAuthorityInlinkValid  = requestFlags.m_hasAuthorityInlinkValid;
			srep.m_siteNumInlinksValid      = sqlite3_column_type(stmt,5)!=SQLITE_NULL;

			if(io_buffer.spare()<(size_t)srep.getRecSize())
				io_buffer.reserve_extra(io_buffer.used()/2+srep.getRecSize());
			memcpy(io_buffer.end(), &srep, sizeof(srep));
			io_buffer.push_back(sizeof(srep));
		} else
			replyFlags = 0;
		
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_key = Spiderdb::makeKey(firstIp,uh48,true,0,false);
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
		sreq.m_isAddUrl                 = requestFlags.m_isAddUrl;
		sreq.m_isPageReindex            = requestFlags.m_isPageReindex;
		sreq.m_isUrlCanonical           = requestFlags.m_isUrlCanonical;
		sreq.m_isPageParser             = requestFlags.m_isPageParser;
		sreq.m_urlIsDocId               = requestFlags.m_urlIsDocId;
		sreq.m_isRSSExt                 = requestFlags.m_isRSSExt;
		sreq.m_isUrlPermalinkFormat     = requestFlags.m_isUrlPermalinkFormat;
		sreq.m_recycleContent           = requestFlags.m_recycleContent;
		sreq.m_forceDelete              = requestFlags.m_forceDelete;
		sreq.m_isInjecting              = requestFlags.m_isInjecting;
		sreq.m_hadReply                 = requestFlags.m_hadReply;
		sreq.m_fakeFirstIp              = requestFlags.m_fakeFirstIp;
		sreq.m_hasAuthorityInlink       = requestFlags.m_hasAuthorityInlink;
		sreq.m_hasAuthorityInlinkValid  = requestFlags.m_hasAuthorityInlinkValid;
		sreq.m_siteNumInlinksValid      = sqlite3_column_type(stmt,5)!=SQLITE_NULL;
		sreq.m_avoidSpiderLinks         = requestFlags.m_avoidSpiderLinks;
		sreq.m_ufn                      = 0; //only used in-memory
		sreq.m_priority                 = priority;
		sreq.m_errCount                 = errCount;
		strcpy(sreq.m_url,(const char*)url);
		sreq.setDataSize();

		if(io_buffer.spare()<(size_t)sreq.getRecSize())
			io_buffer.reserve_extra(io_buffer.used()/2+sreq.getRecSize());
		memcpy(io_buffer.end(), &sreq, sreq.getRecSize());
		io_buffer.push_back(sreq.getRecSize());
		
		listLastKey = sreq.m_key;
	}
	if(rc!=SQLITE_DONE && rc!=SQLITE_ROW) {
		int err = sqlite3_errcode(db);
		log(LOG_ERROR,"sqlitespider: Fetch error: %s",sqlite3_errstr(err));
		g_errno = EBADENGINEER; //TODO
		return false;
	}
	sqlite3_finalize(stmt);
	read_timer.finish();
	ssl.unlock();

	
	int32_t listSize = io_buffer.used();
	char *listMemory;
	if(listSize>0) {
		listMemory = (char*)mmalloc(listSize, "sqliterdblist");
		if(!listMemory) {
			log(LOG_ERROR,"sqlitespider: OOM allocating spiderdb rdblist (%d bytes)", listSize);
			return false;
		}
		memcpy(listMemory, io_buffer.begin(), io_buffer.used());
	} else
		listMemory = NULL;
	key128_t listFirstKey = Spiderdb::makeFirstKey(firstIpStart, uh48Start);
	if(rc==SQLITE_ROW) {
		//early break, so use the listLastKey as-is
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
	if(listSize!=0)
		list->setLastKey((const char*)&listLastKey);
	logTrace( g_conf.m_logTraceSpider, "sqlitespider: listSize = %d", list->getListSize());
	
	return true;
}



static int map_sqlite_error_to_gb_errno(int err) {
	switch(err) {
		case SQLITE_NOMEM:      return ENOMEM;
		case SQLITE_FULL:       return ENOSPC;
		case SQLITE_CORRUPT:    return ECORRUPTDATA;
		default:                return EINTERNALERROR;
	}
}
