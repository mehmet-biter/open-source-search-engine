#include "SpiderdbSqlite.h"
#include "ScopedLock.h"
#include "Hostdb.h"
#include "Collectiondb.h"
#include "Conf.h"
#include "Log.h"
#include <stddef.h>


static sqlite3 *openDb(const char *sqlitedbName);

SpiderdbSqlite g_spiderdb_sqlite(RDB_SPIDERDB_SQLITE);
SpiderdbSqlite g_spiderdb_sqlite2(RDB2_SPIDERDB2_SQLITE);



void SpiderdbSqlite::finalize() {
	ScopedLock sl(mtx);
	for(auto e : dbs)
		sqlite3_close(e.second);
	dbs.clear();
}
	

sqlite3 *SpiderdbSqlite::getDb(collnum_t collnum) {
	ScopedLock sl(mtx);
	auto iter = dbs.find(collnum);
	if(iter!=dbs.end())
		return iter->second;
	else
		return getOrCreateDb(collnum);
}


sqlite3 *SpiderdbSqlite::getOrCreateDb(collnum_t collnum) {
	const auto cr = g_collectiondb.getRec(collnum);
	
	char collectionDirName[1024];
	sprintf(collectionDirName, "%scoll.%s.%d", g_hostdb.m_dir, cr->m_coll, (int)collnum);
	
	char sqlitedbName[1024];
	if(rdbid==RDB_SPIDERDB_SQLITE)
		sprintf(sqlitedbName, "%s/spiderdb.sqlite3", collectionDirName);
	else	
		sprintf(sqlitedbName, "%s/spiderdbRebuild.sqlite3", collectionDirName);
	
	sqlite3 *db = openDb(sqlitedbName);
	if(!db)
		return NULL;
	
	dbs[collnum] = db;
	
	return db;
}



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


static sqlite3 *openDb(const char *sqlitedbName) {
	sqlite3 *db;
	if(g_conf.m_readOnlyMode) {
		//read-only, creation is not allowed
		int rc = sqlite3_open_v2(sqlitedbName,&db,SQLITE_OPEN_READONLY,NULL);
		if(rc!=SQLITE_OK) {
			log(LOG_ERROR,"sqlite: Could not open %s: %s", sqlitedbName, sqlite3_errmsg(db));
			return NULL;
		}
		return db;
	}
	//read-write, creation is allowed
	
	if(access(sqlitedbName,F_OK)==0) {
		int rc = sqlite3_open_v2(sqlitedbName,&db,SQLITE_OPEN_READWRITE,NULL);
		if(rc!=SQLITE_OK) {
			log(LOG_ERROR,"sqlite: Could not open %s: %s", sqlitedbName, sqlite3_errmsg(db));
			return NULL;
		}
		return db;
	}
	
	int rc = sqlite3_open_v2(sqlitedbName,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL);
	if(rc!=SQLITE_OK) {
		log(LOG_ERROR,"sqlite: Could not create %s: %s", sqlitedbName, sqlite3_errmsg(db));
		return NULL;
	}
	
	char *errmsg = NULL;
	if(sqlite3_exec(db, create_table_statmeent, NULL, NULL, &errmsg) != SQLITE_OK) {
		log(LOG_ERROR,"sqlite: %s",sqlite3_errmsg(db));
		sqlite3_close(db);
		unlink(sqlitedbName);
		return NULL;
	}
	
	return db;
}
