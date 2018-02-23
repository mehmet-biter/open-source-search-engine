#ifndef SPIDERDB_UTIL_H_
#define SPIDERDB_UTIL_H_
#include <inttypes.h>
#include "collnum_t.h"

namespace SpiderdbUtil {

bool deleteRecord(collnum_t collnum, int32_t firstIp, int64_t uh48);

}

#endif
