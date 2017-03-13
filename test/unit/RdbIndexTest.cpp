#include <gtest/gtest.h>
#include "RdbIndex.h"
#include "RdbBuckets.h"
#include "Posdb.h"
#include "GigablastTestUtils.h"

static uint64_t getDocId(docidsconst_ptr_t docIds, size_t index) {
	return ((*docIds.get())[index] >> RdbIndex::s_docIdOffset);
}

static bool isDel(docidsconst_ptr_t docIds, size_t index) {
	return (((*docIds.get())[index] & 0x01) == 0);
}

// 000
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdSingleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int64_t termId = 1;
	static const int64_t docId = 1;
	static const int32_t wordPos = 0;

	GbTest::addPosdbKey(&buckets, termId, docId, wordPos);

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
}

// 001
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdSingleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;
	static const int64_t docId = 1;

	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(&buckets, termId, docId, i);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
}

// 010
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdMultipleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;
	static const int32_t wordPos = 0;

	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(&buckets, termId, i, wordPos);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, getDocId(docIds, i));
	}
}

// 011
TEST(RdbIndexTest, GenerateFromBucketSingleTermIdMultipleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t termId = 1;

	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(&buckets, termId, i, i);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, getDocId(docIds, i));
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
		GbTest::addPosdbKey(&buckets, i, docId, wordPos);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
}

// 101
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdSingleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int64_t docId = 1;

	for (int i = 0; i < total_records; i++) {
		GbTest::addPosdbKey(&buckets, i, docId, i);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
}

// 110
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdMultipleDocIdSingleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;
	static const int32_t wordPos = 0;

	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(&buckets, i, i, wordPos);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, getDocId(docIds, i));
	}
}

// 111
TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdMultipleDocIdMultipleWordPos) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;

	for (int i = 0; i < total_records; ++i) {
		GbTest::addPosdbKey(&buckets, i, i, i);
	}

	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);
	index.generateIndex(0, &buckets);

	auto docIds = index.getDocIds();
	EXPECT_EQ(total_records, docIds->size());
	for (int i = 0; i < total_records; ++i) {
		EXPECT_EQ(i, getDocId(docIds, i));
	}
}

TEST(RdbIndexTest, AddDeleteKey) {
	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);

	static const int64_t termId = 1;
	static const int64_t docId = 1;
	static const int32_t wordPos = 1;

	GbTest::addPosdbKey(&index, termId, docId, wordPos, false);
	GbTest::addPosdbKey(&index, termId, docId, wordPos, true);

	// force merge
	index.writeIndex(true);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
	EXPECT_TRUE(isDel(docIds, 0));

	// cleanup
	index.unlink();
}

TEST(RdbIndexTest, DeleteAddKey) {
	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);

	static const int64_t termId = 1;
	static const int64_t docId = 1;
	static const int32_t wordPos = 1;

	GbTest::addPosdbKey(&index, termId, docId, wordPos, true);
	GbTest::addPosdbKey(&index, termId, docId, wordPos, false);

	// force merge
	index.writeIndex(true);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
	EXPECT_FALSE(isDel(docIds, 0));

	// cleanup
	index.unlink();
}

TEST(RdbIndexTest, DeleteAddKeySave) {
	RdbIndex index;
	index.set(".", "test-posdbidx", Posdb::getFixedDataSize(), Posdb::getUseHalfKeys(), Posdb::getKeySize(), RDB_POSDB);

	static const int64_t termId = 1;
	static const int64_t docId = 1;
	static const int32_t wordPos = 1;

	GbTest::addPosdbKey(&index, termId, docId, wordPos, true);
	index.writeIndex(true);

	GbTest::addPosdbKey(&index, termId, docId, wordPos, false);
	index.writeIndex(true);

	auto docIds = index.getDocIds();
	EXPECT_EQ(1, docIds->size());
	EXPECT_EQ(docId, getDocId(docIds, 0));
	EXPECT_FALSE(isDel(docIds, 0));

	// cleanup
	index.unlink();
}
