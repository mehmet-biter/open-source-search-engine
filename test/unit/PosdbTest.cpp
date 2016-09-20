#include "gtest/gtest.h"
#include "Posdb.h"

class PosdbTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
		g_posdb.init();
		g_collectiondb.loadAllCollRecs();
		g_collectiondb.addRdbBaseToAllRdbsForEachCollRec();
	}

	static void TearDownTestCase() {
		g_posdb.reset();
		g_collectiondb.reset();
	}
};


static bool addPosdbKey(int64_t termId, int64_t docId, bool delKey = false) {
	char key[MAX_KEY_BYTES];
	Posdb::makeKey(&key, termId, docId, 0, 0, 0, 0, 0, 0, 0, 0, false, delKey, false);
	g_posdb.getRdb()->addRecord(0, key, NULL, 0);
}


TEST_F(PosdbTest, PosdbAddRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 0; i < total_records; i++) {
		addPosdbKey(i, docId);
	}

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	for (int i = 0; i < total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}
