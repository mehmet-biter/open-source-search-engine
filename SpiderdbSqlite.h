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
private:
	sqlite3 *getOrCreateDb(collnum_t collnum);
};

extern SpiderdbSqlite g_spiderdb_sqlite;
extern SpiderdbSqlite g_spiderdb_sqlite2;


//see Spider.h for field definitions/comments/caveats

//To save space we have to pack several flags into bitfields. This is done for both some requestand reply flags. To make things a bit easier with
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
	bool m_hasAuthorityInlink:1;
	bool m_fromInjectionRequest:1;
	bool m_isIndexedINValid:1;
	SpiderdbReplyFlags() : m_reserved(0) {}
	SpiderdbReplyFlags(int v) { *reinterpret_cast<int*>(this) = v; }
	SpiderdbReplyFlags(unsigned v) { *reinterpret_cast<unsigned*>(this) = v; }
	operator int() const { return *reinterpret_cast<const int*>(this); }
	operator unsigned() const { return *reinterpret_cast<const unsigned*>(this); }
private:
	//force the compiler to use 32 bits for this struct
	uint32_t m_reserved:26;
};


struct RawSpiderdbRecord {
	int32_t         m_firstIp;
	int32_t         m_uh48;
	//Request fields:
	int32_t         m_hostHash32;
	int32_t         m_domHash32;
	int32_t         m_siteHash32;
	int32_t         m_siteNumInlinks;
	int32_t         m_pageNumInlinks;
	int32_t         m_addedTime;
	int32_t         m_discoveryTime;
	int32_t         m_contentHash32; //0 = unknown/invalid
	union {
		SpiderdbRequestFlags requestFlags;
		uint32_t m_requestFlags;
	};
	int32_t         m_priority;
	bool            m_priorityValid;
	int32_t         m_errCount;
	bool            m_errCountValid;
	int32_t         m_sameErrCount;
	std::string     m_url;
	//Reply fields
	float           m_percentChangedPerDay;
	bool            m_percentChangedPerDayValid;
	int32_t         m_spideredTime;
	bool            m_spideredTimeValid;
	int32_t         m_errCodeValid;
	bool            m_errCode;
	int32_t         m_httpStatus;
	bool            m_httpStatusValid;
	int32_t         m_langId;
	bool            m_langIdValid;
	union {
		SpiderdbReplyFlags replyFlags;
		uint32_t m_replyFlags;
	};
};

#endif
