#include <gtest/gtest.h>
#include "Posdb.h"

static bool addPosdbKey(int64_t termId, int64_t docId, bool delKey = false) {
	char key[MAX_KEY_BYTES];
	Posdb::makeKey(&key, termId, docId, 0, 0, 0, 0, 0, 0, 0, 0, false, delKey, false);
	g_posdb.getRdb()->addRecord(0, key, NULL, 0);
}

static void saveAndReloadPosdbBucket() {
	g_posdb.getRdb()->saveTree(false);
	g_posdb.getRdb()->getBuckets()->clear();
	g_posdb.getRdb()->loadTree();
}

static void dumpPosdb() {
	g_posdb.getRdb()->dumpTree(1);
}

static void deletePosdb() {
	// delete rdb files
	RdbBase *base = g_posdb.getRdb()->getBase(0);
	for (int i = 0; i < base->getNumFiles(); ++i) {
		base->getFile(i)->unlink();
		base->getMap(i)->unlink();
		base->getIndex(i)->unlink();
	}

	// delete posdb bucket file
	std::string path(g_posdb.getRdb()->getDir());
	path.append("/").append(g_posdb.getRdb()->getDbname()).append("-buckets-saved.dat");
	unlink(path.c_str());
}

class PosdbTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
	}

	static void TearDownTestCase() {
	}

	void SetUp() {
		g_posdb.init();
		g_collectiondb.loadAllCollRecs();
		g_collectiondb.addRdbBaseToAllRdbsForEachCollRec();
		deletePosdb();
	}

	void TearDown() {
		deletePosdb();
		g_posdb.reset();
		g_collectiondb.reset();
	}
};

TEST_F(PosdbTest, AddRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 1; i <= total_records; i++) {
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
	for (int i = 1; i <= total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}

TEST_F(PosdbTest, AddDeleteRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	// first round
	for (int i = 1; i <= total_records; i++) {
		addPosdbKey(i, docId);
	}
	saveAndReloadPosdbBucket();

	// second round (document deleted)
	for (int i = 1; i <= total_records; i++) {
		addPosdbKey(i, docId, true);
	}
	saveAndReloadPosdbBucket();

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	for (int i = 1; i <= total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
		EXPECT_EQ(true, KEYNEG(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}

TEST_F(PosdbTest, AddDeleteRecordMultiple) {
	static const int64_t docId = 1;

	// first round
	// doc contains 3 words (a, b, c)
	addPosdbKey(1, docId);
	addPosdbKey(2, docId);
	addPosdbKey(3, docId);

	// second round
	// doc contains 3 words (a, c, d)
	addPosdbKey(2, docId, true);
	addPosdbKey(4, docId);

	// third round
	// doc contains 4 words (a, d, e, f)
	addPosdbKey(3, docId, true);
	addPosdbKey(5, docId);
	addPosdbKey(6, docId);

	/// @todo ALC add expected result

	logf(LOG_DEBUG, "++++++++++ a");
	g_posdb.getRdb()->getBuckets()->printBuckets();
	logf(LOG_DEBUG, "++++++++++ b");
	saveAndReloadPosdbBucket();
	logf(LOG_DEBUG, "++++++++++ c");
	g_posdb.getRdb()->getBuckets()->printBuckets();

}


TEST_F(PosdbTest, AddRecordDeleteDoc) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = total_records; i >= 0; i--) {
		addPosdbKey(i, docId, (i == 0));
	}

	saveAndReloadPosdbBucket();

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	for (int i = 0; i <= total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
		EXPECT_EQ((i == 0), KEYNEG(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}

class PosdbNoMergeTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
		g_conf.m_noInMemoryPosdbMerge = true;

		g_posdb.init();
		g_collectiondb.loadAllCollRecs();
		g_collectiondb.addRdbBaseToAllRdbsForEachCollRec();
		deletePosdb();
	}

	static void TearDownTestCase() {
		deletePosdb();
		g_posdb.reset();
		g_collectiondb.reset();


		g_conf.m_noInMemoryPosdbMerge = m_savedMergeConf;
	}

	void SetUp() {
	}

	void TearDown() {
		// we don't want leftover entries between tests
		g_posdb.getRdb()->getBuckets()->clear();
	}

	static bool m_savedMergeConf;
};

bool PosdbNoMergeTest::m_savedMergeConf = g_conf.m_noInMemoryPosdbMerge;

TEST_F(PosdbNoMergeTest, AddRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 1; i <= total_records; i++) {
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
	for (int i = 1; i <= total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
	}

}

TEST_F(PosdbNoMergeTest, AddDeleteRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	// first round
	for (int i = 1; i <= total_records; i++) {
		addPosdbKey(i, docId);
	}
	saveAndReloadPosdbBucket();

	// second round (document deleted)
	for (int i = 1; i <= total_records; i++) {
		addPosdbKey(i, docId, true);
	}
	saveAndReloadPosdbBucket();

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());
	EXPECT_TRUE(list.isExhausted());
}

static void expectRecord(RdbList *list, int64_t termId, int64_t docId, bool isDel = false) {
	ASSERT_FALSE(list->isExhausted());

	const char *rec = list->getCurrentRec();
	EXPECT_EQ(termId, Posdb::getTermId(rec));
	EXPECT_EQ(docId, Posdb::getDocId(rec));
	EXPECT_EQ(isDel, KEYNEG(rec));
	list->skipCurrentRecord();
}

TEST_F(PosdbNoMergeTest, AddDeleteRecordMultiple) {
	static const int64_t docId = 1;

	// first round
	// doc contains 3 words (a, b, c)
	addPosdbKey(1, docId);
	addPosdbKey(2, docId);
	addPosdbKey(3, docId);

	// second round
	// doc contains 3 words (a, c, d)
	addPosdbKey(2, docId, true);
	addPosdbKey(1, docId);
	addPosdbKey(3, docId);
	addPosdbKey(4, docId);

	// third round
	// doc contains 4 words (a, d, e, f)
	addPosdbKey(3, docId, true);

	addPosdbKey(1, docId);
	addPosdbKey(4, docId);
	addPosdbKey(5, docId);
	addPosdbKey(6, docId);

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	expectRecord(&list, 1, docId);
	expectRecord(&list, 4, docId);
	expectRecord(&list, 5, docId);
	expectRecord(&list, 6, docId);

	EXPECT_TRUE(list.isExhausted());
}

// delete keys are not persisted when rdb files are not present
TEST_F(PosdbNoMergeTest, AddRecordDeleteDocWithoutRdbFiles) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = total_records; i >= 0; i--) {
		addPosdbKey(i, docId, (i == 0));
	}

	saveAndReloadPosdbBucket();

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	for (int i = 1; i <= total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
		EXPECT_FALSE(KEYNEG(rec));
	}
	EXPECT_TRUE(list.isExhausted());
}

TEST_F(PosdbNoMergeTest, AddRecordDeleteDocWithRdbFiles) {
	static const int64_t docId = 1;

	// first round
	// doc contains 3 words (a, b, c)
	addPosdbKey(1, docId);
	addPosdbKey(2, docId);
	addPosdbKey(3, docId);
	dumpPosdb();

	// second round
	// doc contains 3 words (a, c, d)
	addPosdbKey(1, docId);
	addPosdbKey(3, docId);
	addPosdbKey(4, docId);
	dumpPosdb();

	// third round
	// doc contains 4 words (a, d, e, f)
	addPosdbKey(1, docId);
	addPosdbKey(4, docId);
	addPosdbKey(5, docId);
	addPosdbKey(6, docId);
	dumpPosdb();

	addPosdbKey(0, docId, true);

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	// verify that data returned is the same as data inserted above
	expectRecord(&list, 0, docId, true);
}