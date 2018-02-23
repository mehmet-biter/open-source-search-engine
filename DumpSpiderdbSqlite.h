#ifndef DUMP_SPIDERDB_SQLITE_H_
#define DUMP_SPIDERDB_SQLITE_H_

#include <cstdint>

void dumpSpiderdbSqlite(const char *collname, int32_t firstIp);
void dumpSpiderdbSqliteCsv(const char *collname, bool interpret_values);

#endif
