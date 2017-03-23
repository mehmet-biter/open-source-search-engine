#ifndef GB_RDBTESTUTILS_H
#define GB_RDBTESTUTILS_H

class Rdb;
class RdbBuckets;
class RdbIndex;
class RdbList;

namespace GbTest {
	void initializeRdbs();
	void resetRdbs();

	void addPosdbKey(Rdb *rdb, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey = false, bool isShardByTermId = false);
	void addPosdbKey(RdbBuckets *buckets, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey = false);
	void addPosdbKey(RdbIndex *index, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey = false);
	void addPosdbKey(RdbList *list, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey = false);
}


#endif // GB_RDBTESTUTILS_H
