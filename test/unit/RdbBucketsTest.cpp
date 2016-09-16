#include "gtest/gtest.h"
#include "RdbBuckets.h"
#include "Posdb.h"

static bool addPosdbKey(RdbBuckets *buckets, int64_t termId, int64_t docId) {
	char key[MAX_KEY_BYTES];
	Posdb::makeStartKey(&key, termId, docId);
	buckets->addNode(0, key, NULL, 0);
}

TEST(RdbBucketTest, AddNode) {
	static const int total_records = 10;
	static const int64_t docId = 1;
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	for (int i = 0; i < total_records; i++) {
		addPosdbKey(&buckets, i, docId);
	}

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbList list;
	buckets.getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	for (int i = 0; i < total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}