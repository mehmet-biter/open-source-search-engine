#ifndef SPIDERDB_RDB_SQLITE_BRIDGE_H_
#define SPIDERDB_RDB_SQLITE_BRIDGE_H_
#include "collnum_t.h"
#include <stddef.h>
#include <vector>

class RdbList;
class u_int128_t;

//Helper function for bridging the old Rdb-style spiderdb records to the new sqlite-based database

namespace SpiderdbRdbSqliteBridge {

struct BatchedRecord {
	collnum_t collnum;
	const void *record;
	size_t record_len;
	BatchedRecord(collnum_t collnum_, const void *record_, size_t record_len_)
	  : collnum(collnum_), record(record_), record_len(record_len_)
	  {}
};
bool addRecords(const std::vector<BatchedRecord> &records);
bool addRecords2(const std::vector<BatchedRecord> &records); //secondary db

//Fetch all records or a subset of the recoreds with startKey<=key<=endKey, and try to limit the rdblist size of recSizes
//Returns false on error
bool getList(collnum_t collnum, RdbList *list, const u_int128_t &startKey, const u_int128_t &endKey,
             int32_t minRecSizes);

bool getFirstIps(collnum_t collnum, RdbList *list, int32_t firstIpStart, int32_t firstIpEnd, int32_t minRecSizes);
}

#endif
