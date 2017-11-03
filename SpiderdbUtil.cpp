#include "SpiderdbUtil.h"
#include "SpiderdbSqlite.h"
#include "Log.h"


bool SpiderdbUtil::deleteRecord(collnum_t collnum, int32_t firstIp, int64_t uh48) {
	sqlite3 *db = g_spiderdb_sqlite.getDb(collnum);
	if(!db)
		return false;
	
	ScopedSqlitedbLock ssl(db);
	
	const char *pzTail="";
	sqlite3_stmt *stmt = NULL;
	if(sqlite3_prepare_v2(db, "delete from spiderdb where m_firstip=? and m_uh48=?", -1, &stmt, &pzTail) != SQLITE_OK) {
		log(LOG_ERROR,"sqlitespider: Statement preparation error %s at or near %s",sqlite3_errmsg(db),pzTail);
		return false;
	}
	
	sqlite3_bind_int64(stmt, 1, (uint32_t)firstIp);
	sqlite3_bind_int64(stmt, 2, uh48);
	
	int select_rc = sqlite3_step(stmt);
	if(select_rc!=SQLITE_DONE) {
		//some kind of error
		log(LOG_ERROR, "sqlitespider: could not delete spiderdb row: %s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	
	sqlite3_finalize(stmt);
	return true;
}
