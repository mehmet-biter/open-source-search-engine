#include <gtest/gtest.h>
#include "RdbIndex.h"
#include "RdbBuckets.h"
#include "Posdb.h"

static bool addPosdbKey(RdbBuckets *buckets, int64_t termId, int64_t docId, int32_t wordPos, bool delKey = false) {
	char key[MAX_KEY_BYTES];
	Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, delKey, false);
	buckets->addNode(0, key, NULL, 0);
}

// 000
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdSingleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int64_t termId = 1;
	static const int64_t docId = 1;
	static const int32_t wordPos = 0;

	addPosdbKey(&buckets, termId, docId, wordPos);

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, (*docIds.get())[0]);
}

// 001
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdSingleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;
	static const int64_t docId = 1;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, termId, docId, i);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, (*docIds.get())[0]);
}

// 010
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdMultipleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;
	static const int32_t wordPos = 0;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, termId, i, wordPos);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, (*docIds.get())[i]);
	}
}

// 011
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdMultipleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, termId, i, i);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, (*docIds.get())[i]);
	}
}

// 100
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdSingleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t docId = 1;
	static const int32_t wordPos = 1;

	for (int i = 0; i < total_records; i++) {
		addPosdbKey(&buckets, i, docId, wordPos);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, (*docIds.get())[0]);
}

// 101
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdSingleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 0; i < total_records; i++) {
		addPosdbKey(&buckets, i, docId, i);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, (*docIds.get())[0]);
}

// 110
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdMultipleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int32_t wordPos = 0;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, i, i, wordPos);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, (*docIds.get())[i]);
	}
}

// 111
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdMultipleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, i, i, i);
	}

	RdbIndex index;
	index.set("./", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, (*docIds.get())[i]);
	}
}