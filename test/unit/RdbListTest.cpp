#include <gtest/gtest.h>
#include "RdbList.h"
#include "Posdb.h"
#include "sort.h"
#include "GigablastTestUtils.h"
#include "Conf.h"
#include "Lang.h"

#define sizeof_arr(x) (sizeof(x) / sizeof(x[0]))

static const char* makePosdbKey(char *key, int64_t termId, uint64_t docId, int32_t wordPos, bool isDelKey) {
	Posdb::makeKey(key, termId, docId, wordPos, 0, 0, 0, 0, 0, langUnknown, 0, false, isDelKey, false);
	return key;
}

static const char* makeTitledbKey(char *key, uint64_t docId, int64_t urlHash48, bool isDelKey) {
	key96_t k = Titledb::makeKey(docId, urlHash48, isDelKey);
	memcpy(key, &k, sizeof(key96_t));
	return key;
}

static const char* makeTitledbData(const char *data) {
	char *mdata = (char*)malloc(strlen(data) + 1);
	strcpy(mdata, data);
	return mdata;
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// RdbList test                                                               //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////
class RdbListTest : public ::testing::Test {
protected:
	static void SetUpTestCase() {
	}

	static void TearDownTestCase() {
	}

	void SetUp() {
		GbTest::initializeRdbs();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}
};

TEST_F(RdbListTest, MergeTestPosdbEmptyAll) {
	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	size_t lists1_size = sizeof_arr(lists1);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(0, final1.getListSize());
}

TEST_F(RdbListTest, MergeTestPosdbEmptyOne) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, false), 0, nullptr);

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	size_t lists1_size = sizeof_arr(lists1);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(list1.getListSize(), final1.getListSize());
	for (list1.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list1.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list1.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list1.getCurrentRec(), final1.getCurrentRec(), list1.getCurrentRecSize()));
	}

	// keys go from deleted to present
	RdbList *lists2[2];
	lists2[0] = &list2;
	lists2[1] = &list1;

	size_t lists2_size = sizeof_arr(lists2);

	RdbList final2;
	final2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final2.prepareForMerge(lists2, lists2_size, -1);
	final2.merge_r(lists2, lists2_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(list1.getListSize(), final2.getListSize());
	for (list1.resetListPtr(), final2.resetListPtr(); !final2.isExhausted(); list1.skipCurrentRecord(), final2.skipCurrentRecord()) {
		EXPECT_EQ(list1.getCurrentRecSize(), final2.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list1.getCurrentRec(), final2.getCurrentRec(), list1.getCurrentRecSize()));
	}
}

// verify that list order is from oldest to newest (last list will override first list)
TEST_F(RdbListTest, MergeTestPosdbVerifyListOrder) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, false), 0, nullptr);

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, true), 0, nullptr);

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	size_t lists1_size = sizeof_arr(lists1);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(0, final1.getListSize());

	// keys go from deleted to present
	RdbList *lists2[2];
	lists2[0] = &list2;
	lists2[1] = &list1;

	size_t lists2_size = sizeof_arr(lists2);

	RdbList final2;
	final2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final2.prepareForMerge(lists2, lists2_size, -1);
	final2.merge_r(lists2, lists2_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(list1.getListSize(), final2.getListSize());
	for (list1.resetListPtr(), final2.resetListPtr(); !final2.isExhausted(); list1.skipCurrentRecord(), final2.skipCurrentRecord()) {
		EXPECT_EQ(list1.getCurrentRecSize(), final2.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list1.getCurrentRec(), final2.getCurrentRec(), list1.getCurrentRecSize()));
	}
}

TEST_F(RdbListTest, MergeTestPosdbVerifyRemoveNegRecords) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, false), 0, nullptr);

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 0x02, 0x02, 0x02, true), 0, nullptr);

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	size_t lists1_size = sizeof_arr(lists1);

	// remove negative records
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(0, final1.getListSize());

	// don't remove negative records
	RdbList final2;
	final2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final2.prepareForMerge(lists1, lists1_size, -1);
	final2.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, 0, 0);

	// verify merged list
	EXPECT_EQ(list2.getListSize(), final2.getListSize());
	for (list2.resetListPtr(), final2.resetListPtr(); !final2.isExhausted(); list2.skipCurrentRecord(), final2.skipCurrentRecord()) {
		EXPECT_EQ(list2.getCurrentRecSize(), final2.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list2.getCurrentRec(), final2.getCurrentRec(), list2.getCurrentRecSize()));
	}
}

TEST_F(RdbListTest, MergeTestTitledb) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list1.addRecord(makeTitledbKey(key, 0x01, 0x01, false), 1, makeTitledbData("1"));
	list1.addRecord(makeTitledbKey(key, 0x02, 0x02, false), 1, makeTitledbData("2"));
	list1.addRecord(makeTitledbKey(key, 0x03, 0x03, false), 1, makeTitledbData("3"));

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list2.addRecord(makeTitledbKey(key, 0x04, 0x04, false), 1, makeTitledbData("4"));
	list2.addRecord(makeTitledbKey(key, 0x05, 0x05, false), 1, makeTitledbData("5"));
	list2.addRecord(makeTitledbKey(key, 0x06, 0x06, false), 1, makeTitledbData("6"));

	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;
	size_t lists1_size = sizeof_arr(lists1);

	char startKey[MAX_KEY_BYTES];
	char endKey[MAX_KEY_BYTES];

	makeTitledbKey(startKey, 0x01, 0x01, false);
	makeTitledbKey(endKey, 0x06, 0x06, false);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, startKey, endKey, -1, false, RDB_TITLEDB, 0, 0);

	// verify merged list
	int i = 1;
	for (final1.resetListPtr(); !final1.isExhausted(); final1.skipCurrentRecord()) {
		key96_t k = final1.getCurrentKey();
		makeTitledbKey(key, i, i, false);
		EXPECT_EQ(0, memcmp(key, &k, 12));
		++i;
	}

	// total records + 1
	EXPECT_EQ(7, i);

	final1.getEndKey(endKey);
	EXPECT_EQ(0, memcmp(endKey, makeTitledbKey(key, 0x06, 0x06, false), 12));
}

TEST_F(RdbListTest, MergeTestTitledbDelEndKey) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list1.addRecord(makeTitledbKey(key, 0x01, 0x01, false), 1, makeTitledbData("1"));
	list1.addRecord(makeTitledbKey(key, 0x02, 0x02, false), 1, makeTitledbData("2"));
	list1.addRecord(makeTitledbKey(key, 0x03, 0x03, false), 1, makeTitledbData("3"));

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list2.addRecord(makeTitledbKey(key, 0x04, 0x04, false), 1, makeTitledbData("4"));
	list2.addRecord(makeTitledbKey(key, 0x05, 0x05, false), 1, makeTitledbData("5"));
	list2.addRecord(makeTitledbKey(key, 0x06, 0x06, true), 0, nullptr);

	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;
	size_t lists1_size = sizeof_arr(lists1);

	char startKey[MAX_KEY_BYTES];
	char endKey[MAX_KEY_BYTES];

	makeTitledbKey(startKey, 0x01, 0x01, false);
	makeTitledbKey(endKey, 0x06, 0x06, true);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, startKey, endKey, -1, false, RDB_TITLEDB, 0, 0);

	// verify merged list
	int i = 1;
	for (final1.resetListPtr(); !final1.isExhausted(); final1.skipCurrentRecord()) {
		key96_t k = final1.getCurrentKey();
		makeTitledbKey(key, i, i, false);
		EXPECT_EQ(0, memcmp(key, &k, 12));
		++i;
	}

	// total records + 1
	EXPECT_EQ(6, i);

	final1.getEndKey(endKey);
	EXPECT_EQ(0, memcmp(endKey, makeTitledbKey(key, 0x05, 0x05, false), 12));
}

TEST_F(RdbListTest, MergeTestTitledbDoubleDelEndKey) {
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list1.addRecord(makeTitledbKey(key, 0x01, 0x01, false), 1, makeTitledbData("1"));
	list1.addRecord(makeTitledbKey(key, 0x02, 0x02, false), 1, makeTitledbData("2"));
	list1.addRecord(makeTitledbKey(key, 0x03, 0x03, false), 1, makeTitledbData("3"));

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	list2.addRecord(makeTitledbKey(key, 0x04, 0x04, false), 1, makeTitledbData("4"));
	list2.addRecord(makeTitledbKey(key, 0x05, 0x05, true), 0, nullptr);
	list2.addRecord(makeTitledbKey(key, 0x06, 0x06, true), 0, nullptr);

	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;
	size_t lists1_size = sizeof_arr(lists1);

	char startKey[MAX_KEY_BYTES];
	char endKey[MAX_KEY_BYTES];

	makeTitledbKey(startKey, 0x01, 0x01, false);
	makeTitledbKey(endKey, 0x06, 0x06, true);

	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Titledb::getFixedDataSize(), true, Titledb::getUseHalfKeys(), Titledb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, startKey, endKey, -1, false, RDB_TITLEDB, 0, 0);

	// verify merged list
	int i = 1;
	for (final1.resetListPtr(); !final1.isExhausted(); final1.skipCurrentRecord()) {
		key96_t k = final1.getCurrentKey();
		makeTitledbKey(key, i, i, (i == 5));
		EXPECT_EQ(0, memcmp(key, &k, 12));
		++i;
	}

	// total records + 1
	EXPECT_EQ(6, i);

	final1.getEndKey(endKey);
	EXPECT_EQ(0, memcmp(endKey, makeTitledbKey(key, 0x05, 0x05, false), 12));
}

////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// RdbList index test                                                         //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

class RdbListNoMergeTest : public ::testing::Test {
public:
	void SetUp() {
		GbTest::initializeRdbs();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}
};

static void addListToTree(rdbid_t rdbId, collnum_t collNum, RdbList *list) {
	Rdb *rdb = getRdbFromId(rdbId);
	rdb->addList(collNum, list);
	rdb->dumpTree();
	rdb->getBase(0)->markNewFileReadable();
	rdb->getBase(0)->generateGlobalIndex();
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbSingleDocSpiderSpiderSpider) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId = 1;


	// spider doc (a, b, c, d, e)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// respider doc (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// respider doc (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list3.getListSize(), final1.getListSize());
	for (list3.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbSingleDocSpiderSpiderDelete) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId = 1;

	// spider doc (a, b, c, d, e)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// respider doc (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// delete doc
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, POSDB_DELETEDOC_TERMID, docId, 0, true), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list3.getListSize(), final1.getListSize());
	for (list3.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbSingleDocSpiderDeleteSpider) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId = 1;

	// spider doc (a, b, c, d, e)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// delete doc
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, POSDB_DELETEDOC_TERMID, docId, 0, true), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// respider doc (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list3.getListSize(), final1.getListSize());
	for (list3.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbSingleDocMergeStartSecondFile) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId = 1;


	// spider doc (a, b, c, d, e)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// respider doc (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 'a', docId, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// respider doc (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[2];
	lists1[0] = &list2;
	lists1[1] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 1);

	EXPECT_EQ(list3.getListSize(), final1.getListSize());
	for (list3.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbMultiDocS1S2N1S2S1N2) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId1 = 1;
	const int64_t docId2 = 2;

	// spider doc 1 (a, b, c, d, e)
	// spider doc 2 (a, b, c)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId1, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId1, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId1, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId1, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId1, 4, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId2, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId2, 2, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// respider doc 2 (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId2, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId2, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId2, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId2, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// respider doc 1 (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId1, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId1, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId1, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId1, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId1, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	Rdb *rdb = getRdbFromId(RDB_POSDB);
	rdb->getBase(0)->generateGlobalIndex();
	rdb->getBase(0)->printGlobalIndex();

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list2.getListSize() + list3.getListSize(), final1.getListSize());

	final1.resetListPtr();
	for (list2.resetListPtr(); !list2.isExhausted(); list2.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list2.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list2.getCurrentRec(), final1.getCurrentRec(), list2.getCurrentRecSize()));
	}

	for (list3.resetListPtr(); !list3.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbMultiDocS1N2N1S2S1N2) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId1 = 1;
	const int64_t docId2 = 2;


	// spider doc 1 (a, b, c, d, e)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId1, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId1, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId1, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId1, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId1, 4, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId2, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId2, 2, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// spider doc 2 (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId2, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId2, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId2, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId2, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// respider doc 1 (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId1, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId1, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId1, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId1, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId1, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list2.getListSize() + list3.getListSize(), final1.getListSize());

	for (list2.resetListPtr(), final1.resetListPtr(); !list2.isExhausted(); list2.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list2.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list2.getCurrentRec(), final1.getCurrentRec(), list2.getCurrentRecSize()));
	}

	for (list3.resetListPtr(); !list3.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbMultiDocS1S2D1S2S1N2) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId1 = 1;
	const int64_t docId2 = 2;


	// spider doc 1 (a, b, c, d, e)
	// spider doc 2 (a, b, c)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId1, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId1, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId1, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId1, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId1, 4, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId2, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId2, 2, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// deleted doc 1
	// respider doc 2 (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, POSDB_DELETEDOC_TERMID, docId1, 0, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId2, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId2, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId2, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId2, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// spider doc 1 (r, s, t, l, n)
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, 'r', docId1, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId1, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId1, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId1, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId1, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	// first record from list2 is not in output list
	list2.resetListPtr();
	EXPECT_EQ(list2.getListSize() - list2.getCurrentRecSize() + list3.getListSize(), final1.getListSize());

	for (list2.skipCurrentRecord(), final1.resetListPtr(); !list2.isExhausted(); list2.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list2.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list2.getCurrentRec(), final1.getCurrentRec(), list2.getCurrentRecSize()));
	}

	for (list3.resetListPtr(); !list3.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}

TEST_F(RdbListNoMergeTest, MergeTestPosdbMultiDocS1S2D1S2S1D2) {
	char key[MAX_KEY_BYTES];
	const rdbid_t rdbId = RDB_POSDB;
	const collnum_t collNum = 0;
	const int64_t docId1 = 1;
	const int64_t docId2 = 2;


	// spider doc 1 (a, b, c, d, e)
	// spider doc 2 (a, b, c)
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 'a', docId1, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId1, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId1, 2, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'd', docId1, 3, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'e', docId1, 4, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'b', docId2, 1, false), 0, nullptr);
	list1.addRecord(makePosdbKey(key, 'c', docId2, 2, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list1);

	// deleted doc 1
	// respider doc 2 (a, c, b, e, d)
	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, POSDB_DELETEDOC_TERMID, docId1, 0, true), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'a', docId2, 0, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'c', docId2, 1, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'b', docId2, 2, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'e', docId2, 3, false), 0, nullptr);
	list2.addRecord(makePosdbKey(key, 'd', docId2, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list2);

	// spider doc 1 (r, s, t, l, n)
	// deleted doc 2
	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list3.addRecord(makePosdbKey(key, POSDB_DELETEDOC_TERMID, docId2, 0, true), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'r', docId1, 0, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 's', docId1, 1, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 't', docId1, 2, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'l', docId1, 3, false), 0, nullptr);
	list3.addRecord(makePosdbKey(key, 'n', docId1, 4, false), 0, nullptr);
	addListToTree(rdbId, collNum, &list3);

	// keys go from oldest to newest
	RdbList *lists1[3];
	lists1[0] = &list1;
	lists1[1] = &list2;
	lists1[2] = &list3;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	RdbList final1;
	final1.set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, lists1_size, -1);
	final1.merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, 0);

	EXPECT_EQ(list3.getListSize(), final1.getListSize());
	for (list3.resetListPtr(), final1.resetListPtr(); !final1.isExhausted(); list3.skipCurrentRecord(), final1.skipCurrentRecord()) {
		EXPECT_EQ(list3.getCurrentRecSize(), final1.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list3.getCurrentRec(), final1.getCurrentRec(), list3.getCurrentRecSize()));
	}
}


static const int64_t s_docIdStart = 1;
static const int32_t s_wordPosStart = 0;

static void addPosdbKey18b(RdbList *list, int64_t *termId, int64_t *docId, int32_t *wordPos, bool isDelKey) {
	char key[MAX_KEY_BYTES];
	++(*termId);
	*docId = s_docIdStart;
	*wordPos = s_wordPosStart;
	list->addRecord(makePosdbKey(key, *termId, *docId, *wordPos, isDelKey), 0, nullptr);
}

static void addPosdbKey12b(RdbList *list, int64_t *termId, int64_t *docId, int32_t *wordPos, bool isDelKey) {
	char key[MAX_KEY_BYTES];
	++(*docId);
	*wordPos = s_wordPosStart;
	list->addRecord(makePosdbKey(key, *termId, *docId, *wordPos, isDelKey), 0, nullptr);
}

static void addPosdbKey06b(RdbList *list, int64_t *termId, int64_t *docId, int32_t *wordPos, bool isDelKey) {
	char key[MAX_KEY_BYTES];
	++(*wordPos);
	list->addRecord(makePosdbKey(key, *termId, *docId, *wordPos, isDelKey), 0, nullptr);
}

static void addPosdbKey(RdbList *list, uint8_t type, int64_t *termId, int64_t *docId, int32_t *wordPos, bool isDelKey) {
	switch (type) {
		case 6:
			addPosdbKey06b(list, termId, docId, wordPos, isDelKey);
			break;
		case 12:
			addPosdbKey12b(list, termId, docId, wordPos, isDelKey);
			break;
		case 18:
			addPosdbKey18b(list, termId, docId, wordPos, isDelKey);
			break;
		default:
			gbshutdownLogicError();
	}
}

static void createPosdbList(collnum_t collNum, RdbList *list, int64_t termId, uint8_t type1, uint8_t type2, uint8_t type3, uint8_t type4) {
	list->set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	int64_t docId = s_docIdStart;
	int32_t wordPos = s_wordPosStart;

	addPosdbKey(list, type1, &termId, &docId, &wordPos, false);
	addPosdbKey(list, type2, &termId, &docId, &wordPos, false);
	addPosdbKey(list, type3, &termId, &docId, &wordPos, false);
	addPosdbKey(list, type4, &termId, &docId, &wordPos, false);
	addListToTree(RDB_POSDB, collNum, list);
}

static void mergePosdbLists(collnum_t collNum, RdbList *final1, RdbList *list1, RdbList *list2, int32_t startFileNum = 0) {
	// keys go from oldest to newest
	RdbList *lists1[2];
	lists1[0] = list1;
	lists1[1] = list2;

	size_t lists1_size = sizeof_arr(lists1);

	// merge
	final1->set(nullptr, 0, nullptr, 0, Posdb::getFixedDataSize(), true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1->prepareForMerge(lists1, lists1_size, -1);
	final1->merge_r(lists1, lists1_size, KEYMIN(), KEYMAX(), -1, false, RDB_POSDB, collNum, startFileNum);
}

static void expectEqualList(RdbList *list1, RdbList *list2) {
	EXPECT_EQ(list2->getListSize(), list1->getListSize());
	for (list2->resetListPtr(), list1->resetListPtr(); !list1->isExhausted(); list2->skipCurrentRecord(), list1->skipCurrentRecord()) {
		EXPECT_EQ(list2->getCurrentRecSize(), list1->getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list2->getCurrentRec(), list1->getCurrentRec(), list2->getCurrentRecSize()));
	}
}

class RdbListNoMergePosdbMultiRecTest : public RdbListNoMergeTest, public ::testing::WithParamInterface<::testing::tuple<uint8_t, uint8_t, uint8_t>> {
};

INSTANTIATE_TEST_CASE_P(RdbListNoMergePosdbMultiRec,
                        RdbListNoMergePosdbMultiRecTest,
                        ::testing::Combine(::testing::Values(6, 12, 18),
                                           ::testing::Values(6, 12, 18),
                                           ::testing::Values(6, 12, 18)));

TEST_P(RdbListNoMergePosdbMultiRecTest, MergeTestPosdbMultiDoc) {
	const collnum_t collNum = 0;

	// spider doc
	RdbList list1;
	createPosdbList(collNum, &list1, 'a', 18, ::testing::get<0>(GetParam()), ::testing::get<1>(GetParam()), ::testing::get<2>(GetParam()));

	// respider doc
	RdbList list2;
	createPosdbList(collNum, &list2, 's', 18, ::testing::get<0>(GetParam()), ::testing::get<1>(GetParam()), ::testing::get<2>(GetParam()));

	// merge
	RdbList final1;
	mergePosdbLists(collNum, &final1, &list1, &list2);

	// check result
	expectEqualList(&final1, &list2);
}

/// @todo ALC verify if disabled test below it's still needed

int cmpKey (const void *h1, const void *h2) {
	if ( *(key96_t *)h1 < *(key96_t *)h2 ) return -1;
	if ( *(key96_t *)h1 > *(key96_t *)h2 ) return  1;
	return 0;
}

// copied from mergetest.cpp
TEST(RdbListTest, DISABLED_MergeTest1) {
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	// # of keys to in each list
	int32_t nk = 200000;
	// # keys wanted
	int32_t numKeysWanted = 200000;
	// get # lists to merge
	int32_t numToMerge = 4 ;
	// print start time
	logf(LOG_DEBUG,"smt:: randomizing begin. %" PRId32" lists of %" PRId32" keys.", numToMerge, nk);

	// make a list of compressed (6 byte) docIds
	key96_t *keys0 = (key96_t *)malloc(sizeof(key96_t) * nk);
	key96_t *keys1 = (key96_t *)malloc(sizeof(key96_t) * nk);
	key96_t *keys2 = (key96_t *)malloc(sizeof(key96_t) * nk);
	key96_t *keys3 = (key96_t *)malloc(sizeof(key96_t) * nk);

	// store radnom docIds in this list
	uint32_t *p = (uint32_t *)keys0;
	// random docIds
	for (int32_t i = 0; i < nk; i++) {
		*p++ = rand();
		*p++ = rand();
		*p++ = rand();
	}
	p = (uint32_t *)keys1;
	for (int32_t i = 0; i < nk; i++) {
		*p++ = rand();
		*p++ = rand();
		*p++ = rand();
	}
	p = (uint32_t *)keys2;
	for (int32_t i = 0; i < nk; i++) {
		*p++ = rand();
		*p++ = rand();
		*p++ = rand();
	}
	p = (uint32_t *)keys3;
	for (int32_t i = 0; i < nk; i++) {
		*p++ = rand();
		*p++ = rand();
		*p++ = rand();
	}

	// sort em up
	gbsort(keys0, nk, sizeof(key96_t), cmpKey);
	gbsort(keys1, nk, sizeof(key96_t), cmpKey);
	gbsort(keys2, nk, sizeof(key96_t), cmpKey);
	gbsort(keys3, nk, sizeof(key96_t), cmpKey);

	// set lists
	RdbList list0;
	RdbList list1;
	RdbList list2;
	RdbList list3;

	list0.set((char *)keys0, nk * sizeof(key96_t), (char *)keys0, nk * sizeof(key96_t),
	          0,
	          false,
	          false, sizeof(key96_t));
	list1.set((char *)keys1,
	          nk * sizeof(key96_t),
	          (char *)keys1,
	          nk * sizeof(key96_t),
	          0,
	          false,
	          false, sizeof(key96_t));
	list2.set((char *)keys2,
	          nk * sizeof(key96_t),
	          (char *)keys2,
	          nk * sizeof(key96_t),
	          0,
	          false,
	          false, sizeof(key96_t));
	list3.set((char *)keys3,
	          nk * sizeof(key96_t),
	          (char *)keys3,
	          nk * sizeof(key96_t),
	          0,
	          false,
	          false, sizeof(key96_t));

	// merge
	RdbList *lists[4];
	lists[0] = &list0;
	lists[1] = &list1;
	lists[2] = &list2;
	lists[3] = &list3;

	RdbList list;
	list.prepareForMerge (lists,numToMerge,numKeysWanted * sizeof(key96_t));

	// start time
	logf(LOG_DEBUG,"starting merge");
	int64_t t = gettimeofdayInMilliseconds();
	// do it
	list.merge_r(lists, numToMerge, KEYMIN(), KEYMAX(), numKeysWanted, true, RDB_NONE, 0, 0);

	// completed
	int64_t now = gettimeofdayInMilliseconds();

	logf(LOG_DEBUG, "smt:: %" PRId32" list NEW MERGE took %" PRIu64" ms", numToMerge,now-t);
	// time per key
	int32_t size = list.getListSize() / sizeof(key96_t);
	double tt = ((double)(now - t))*1000000.0 / ((double)size);
	logf(LOG_DEBUG,"smt:: %f nanoseconds per key", tt);

	// stats
	double d = (1000.0*(double)(size)) / ((double)(now - t));
	logf(LOG_DEBUG,"smt:: %f cycles per final key" , 400000000.0 / d );
	logf(LOG_DEBUG,"smt:: we can do %" PRId32" adds per second" ,(int32_t)d);

	logf(LOG_DEBUG,"smt:: final list size = %" PRId32"",list.getListSize());
}

static bool addRecord(RdbList *list, uint32_t n1, uint64_t n0) {
	key96_t k;
	k.n1 = n1;
	k.n0 = n0;

	return list->addRecord((const char*)&k, 0, nullptr);
}

// copied from mergetest.cpp
TEST(RdbListTest, DISABLED_MergeTest2) {
	RdbList list1;
	list1.set(nullptr, 0, nullptr, 0, 0, true, true, sizeof(key96_t));

	RdbList list2;
	list2.set(nullptr, 0, nullptr, 0, 0, true, true, sizeof(key96_t));

	RdbList list3;
	list3.set(nullptr, 0, nullptr, 0, 0, true, true, sizeof(key96_t));

	// make a list of keys
	addRecord(&list1, 0xa0, 0x01LL);
	addRecord(&list1, 0xb0, 0x00LL);
	addRecord(&list1, 0xa0, 0x01LL);


	addRecord(&list2, 0xa0, 0x00LL);
	addRecord(&list2, 0xb0, 0x01LL);
	addRecord(&list2, 0xd0, 0x00LL);

	addRecord(&list3, 0xa0, 0x01LL);
	addRecord(&list3, 0xb0, 0x01LL);
	addRecord(&list3, 0xd0, 0x01LL);
	addRecord(&list3, 0xd0, 0x00LL);
	addRecord(&list3, 0xd0, 0x00LL);
	addRecord(&list3, 0xd0, 0x00LL);
	addRecord(&list3, 0xe0, 0x01LL);
	addRecord(&list3, 0xe0, 0x81LL);

	RdbList *lists[3];
	lists[0] = &list1;
	lists[1] = &list2;
	lists[2] = &list3;

	RdbList final;
	final.set(nullptr, 0, nullptr, 0, 0, true, true, sizeof(key96_t));

	int32_t min = -1;
	final.prepareForMerge(lists, 3, min);

	// print all lists
	logf(LOG_DEBUG, "-------list #1-------");
	list1.printList();
	logf(LOG_DEBUG, "-------list #2-------");
	list2.printList();
	logf(LOG_DEBUG, "-------list #3-------");
	list3.printList();

	final.merge_r(lists, 3, KEYMIN(), KEYMAX(), min, true, RDB_NONE, 0, 0);

	logf(LOG_DEBUG,"------list final------");
	final.printList();
}
