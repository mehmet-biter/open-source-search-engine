#include <gtest/gtest.h>
#include "RdbIndex.h"
#include "RdbBuckets.h"
#include "Posdb.h"

static bool addPosdbKey(RdbBuckets *buckets, int64_t termId, int64_t docId, int32_t wordPos, bool delKey = false) {
	char key[MAX_KEY_BYTES];
	Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, delKey, false);
	buckets->addNode(0, key, NULL, 0);
}

TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdSingleDocId) {
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

TEST(RdbIndexTest, GenerateFromBucketSingleTermIdMultipleDocId) {
	RdbBuckets buckets;
	buckets.set(Posdb::getFixedDataSize(), 1024 * 1024, "test-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());

	static const int total_records = 10;

	for (int i = 0; i < total_records; ++i) {
		addPosdbKey(&buckets, 1, i, i);
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

TEST(RdbIndexTest, GenerateFromBucketMultipleTermIdMultipleDocId) {
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