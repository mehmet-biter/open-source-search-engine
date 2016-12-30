#include <gtest/gtest.h>
#include "Posdb.h"
#include "GigablastTestUtils.h"
#include "Conf.h"

static void saveAndReloadPosdbBucket() {
	g_posdb.getRdb()->saveTree(false);
	g_posdb.getRdb()->getBuckets()->clear();
	g_posdb.getRdb()->loadTree();
}

static void dumpPosdb() {
	g_posdb.getRdb()->dumpTree();
}

class PosdbTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
	}

	static void TearDownTestCase() {
	}

	void SetUp() {
		GbTest::initializeRdbs();
		m_rdb = g_posdb.getRdb();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}

	Rdb *m_rdb;
};

TEST_F(PosdbTest, AddRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 1; i <= total_records; i++) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
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
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
	}
	saveAndReloadPosdbBucket();

	// second round (document deleted)
	for (int i = 1; i <= total_records; i++) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0, true);
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
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'b', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0);

	// second round
	// doc contains 3 words (a, c, d)
	GbTest::addPosdbKey(m_rdb, 'b', docId, 0, true);
	GbTest::addPosdbKey(m_rdb, 'd', docId, 0);

	// third round
	// doc contains 4 words (a, d, e, f)
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0, true);
	GbTest::addPosdbKey(m_rdb, 'e', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'f', docId, 0);

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
		GbTest::addPosdbKey(m_rdb, i, docId, 0, (i == 0));
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
	}

	static void TearDownTestCase() {
		g_conf.m_noInMemoryPosdbMerge = m_savedMergeConf;
	}

	void SetUp() {
		GbTest::initializeRdbs();
		m_rdb = g_posdb.getRdb();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}

	Rdb *m_rdb;

	static bool m_savedMergeConf;
};

bool PosdbNoMergeTest::m_savedMergeConf = g_conf.m_noInMemoryPosdbMerge;

TEST_F(PosdbNoMergeTest, AddRecord) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 1; i <= total_records; i++) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
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
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
	}
	saveAndReloadPosdbBucket();

	// second round (document deleted)
	for (int i = 1; i <= total_records; i++) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0, true);
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
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'b', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0);

	// second round
	// doc contains 3 words (a, c, d)
	GbTest::addPosdbKey(m_rdb, 'b', docId, 0, true);
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'd', docId, 0);

	// third round
	// doc contains 4 words (a, d, e, f)
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0, true);

	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'd', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'e', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'f', docId, 0);

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());

	expectRecord(&list, 'a', docId);
	expectRecord(&list, 'd', docId);
	expectRecord(&list, 'e', docId);
	expectRecord(&list, 'f', docId);

	EXPECT_TRUE(list.isExhausted());
}

TEST_F(PosdbNoMergeTest, AddRecordDeleteDocWithoutRdbFiles) {
	static const int total_records = 10;
	static const int64_t docId = 1;

	// use extremes
	const char *startKey = KEYMIN();
	const char *endKey = KEYMAX();
	int32_t numPosRecs  = 0;
	int32_t numNegRecs = 0;

	RdbBuckets *buckets = g_posdb.getRdb()->getBuckets();
	RdbList list;

	// spider document
	for (int i = 1; i < total_records; ++i) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
	}
	saveAndReloadPosdbBucket();

	// verify that data returned is the same as data inserted above
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());
	for (int i = 1; i < total_records; ++i, list.skipCurrentRecord()) {
		const char *rec = list.getCurrentRec();
		EXPECT_EQ(i, Posdb::getTermId(rec));
		EXPECT_EQ(docId, Posdb::getDocId(rec));
		EXPECT_FALSE(KEYNEG(rec));
	}
	EXPECT_TRUE(list.isExhausted());

	// deleted document
	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0, true);
	}
	saveAndReloadPosdbBucket();

	// verify that data returned is the same as data inserted above
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());
	EXPECT_TRUE(list.isExhausted());

	// respidered document
	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(m_rdb, i, docId, 0);
	}
	saveAndReloadPosdbBucket();

	// verify that data returned is the same as data inserted above
	buckets->getList(0, startKey, endKey, -1, &list, &numPosRecs, &numNegRecs, Posdb::getUseHalfKeys());
	for (int i = 1; i < total_records; ++i, list.skipCurrentRecord()) {
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
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'b', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0);
	dumpPosdb();

	// second round
	// doc contains 3 words (a, c, d)
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'c', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'd', docId, 0);
	dumpPosdb();

	// third round
	// doc contains 4 words (a, d, e, f)
	GbTest::addPosdbKey(m_rdb, 'a', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'd', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'e', docId, 0);
	GbTest::addPosdbKey(m_rdb, 'f', docId, 0);
	dumpPosdb();

	GbTest::addPosdbKey(m_rdb, POSDB_DELETEDOC_TERMID, docId, 0, true);

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
