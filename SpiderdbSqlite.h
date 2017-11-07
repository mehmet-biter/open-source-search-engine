#ifndef SPIDERDB_H_
#define SPIDERDB_H_
#include "GbMutex.h"
#include "collnum_t.h"
#include "rdbid_t.h"
#include <inttypes.h>
#include <string>
#include <map>
#include "sqlite3.h"


class SpiderdbSqlite {
	std::map<collnum_t,sqlite3*> dbs;
	GbMutex mtx;
	rdbid_t rdbid;
public:
	SpiderdbSqlite(rdbid_t rdbid_) : dbs(), mtx(), rdbid(rdbid_) {}
	~SpiderdbSqlite() { finalize(); }
	SpiderdbSqlite(const SpiderdbSqlite&) = delete;
	SpiderdbSqlite& operator=(const SpiderdbSqlite&) = delete;
	
	void finalize(); //closes all DBs
	
	sqlite3 *getDb(collnum_t collnum);
	
	void closeDb(collnum_t collnum);
	
	static void swapinSecondarySpiderdb(collnum_t collnum, const char *collname);
private:
	sqlite3 *getOrCreateDb(collnum_t collnum);
};

extern SpiderdbSqlite g_spiderdb_sqlite;
extern SpiderdbSqlite g_spiderdb_sqlite2;



class ScopedSqlitedbLock {
	sqlite3 *db;
	ScopedSqlitedbLock(const ScopedSqlitedbLock&) = delete;
	ScopedSqlitedbLock& operator=(const ScopedSqlitedbLock&) = delete;
public:
	ScopedSqlitedbLock(sqlite3 *db_);
	~ScopedSqlitedbLock() { unlock(); }
	void unlock();
};


//see Spider.h for bitfield definitions/comments/caveats

//To save space we have to pack several flags into bitfields. This is done for both some request and reply
//flags. To make things a bit easier with manipulating them the structs also have conversion to/from ints.
//Do not insert or delete bitfield members as the values are persisted in spiderdb. Instead take bit from
//the bottom m_reserved for new fields and mark deleted fields as reserved.

struct SpiderdbRequestFlags {
	bool m_recycleContent:1;
	bool m_isAddUrl:1;
	bool m_isPageReindex:1;
	bool m_isUrlCanonical:1;
	bool m_isPageParser:1;
	bool m_urlIsDocId:1;
	bool m_isRSSExt:1;
	bool m_isUrlPermalinkFormat:1;
	bool m_forceDelete:1;
	bool m_isInjecting:1;
	bool m_hadReply:1;
	bool m_fakeFirstIp:1;
	bool m_hasAuthorityInlink:1;
	bool m_hasAuthorityInlinkValid:1;
	bool m_avoidSpiderLinks:1;
	SpiderdbRequestFlags() : m_reserved(0) {}
	SpiderdbRequestFlags(int v) { *reinterpret_cast<int*>(this) = v; }
	SpiderdbRequestFlags(unsigned v) { *reinterpret_cast<unsigned*>(this) = v; }
	SpiderdbRequestFlags& operator=(const SpiderdbRequestFlags&) = default;
	SpiderdbRequestFlags& operator=(int v) { *reinterpret_cast<int*>(this) = v; return *this; }
	SpiderdbRequestFlags& operator=(unsigned v) { *reinterpret_cast<unsigned*>(this) = v; return *this; }
	operator int() const { return *reinterpret_cast<const int*>(this); }
	operator unsigned() const { return *reinterpret_cast<const unsigned*>(this); }
private:
	//force the compiler to use 32 bits for this struct
	uint32_t m_reserved:17;
};

struct SpiderdbReplyFlags {
	bool m_isRSS:1;
	bool m_isPermalink:1;
	bool m_isIndexed:1;
	bool m_fromInjectionRequest:1;
	bool m_isIndexedINValid:1;
	SpiderdbReplyFlags() : m_reserved(0) {}
	SpiderdbReplyFlags(int v) { *reinterpret_cast<int*>(this) = v; }
	SpiderdbReplyFlags(unsigned v) { *reinterpret_cast<unsigned*>(this) = v; }
	SpiderdbReplyFlags& operator=(const SpiderdbReplyFlags&) = default;
	SpiderdbReplyFlags& operator=(int v) { *reinterpret_cast<int*>(this) = v; return *this; }
	SpiderdbReplyFlags& operator=(unsigned v) { *reinterpret_cast<unsigned*>(this) = v; return *this; }
	operator int() const { return *reinterpret_cast<const int*>(this); }
	operator unsigned() const { return *reinterpret_cast<const unsigned*>(this); }
private:
	//force the compiler to use 32 bits for this struct
	uint32_t m_reserved:27;
};

#endif
