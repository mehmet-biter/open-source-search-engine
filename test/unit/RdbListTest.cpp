#include "gtest/gtest.h"
#include "RdbList.h"
#include "Posdb.h"
//#include "sort.h"
//#include <stdlib.h>

static const char* makePosdbKey(char *key, int64_t termId, uint64_t docId, int32_t wordPos, bool isDelKey) {
	Posdb::makeKey(key, termId, docId, wordPos, 0, 0, 0, 0, 0, langUnknown, 0, false, isDelKey, false);
	return key;
}

TEST(RdbListTest, MergeTestPosdbEmptyAll) {
	g_conf.m_logTraceRdbList = true;
	// setup test
	RdbList list1;
	list1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	RdbList list2;
	list2.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	RdbList final1;
	final1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, 2, -1);
	final1.merge_r(lists1, 2, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB);

	// verify merged list
	EXPECT_EQ(0, final1.getListSize());
}

TEST(RdbListTest, MergeTestPosdbEmptyOne) {
	g_conf.m_logTraceRdbList = true;
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, false), 0, NULL);

	RdbList list2;
	list2.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	RdbList final1;
	final1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, 2, -1);
	final1.merge_r(lists1, 2, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB);

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

	RdbList final2;
	final2.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final2.prepareForMerge(lists2, 2, -1);
	final2.merge_r(lists2, 2, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB);

	// verify merged list
	EXPECT_EQ(list1.getListSize(), final2.getListSize());
	for (list1.resetListPtr(), final2.resetListPtr(); !final2.isExhausted(); list1.skipCurrentRecord(), final2.skipCurrentRecord()) {
		EXPECT_EQ(list1.getCurrentRecSize(), final2.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list1.getCurrentRec(), final2.getCurrentRec(), list1.getCurrentRecSize()));
	}
}

// verify that list order is from oldest to newest (last list will override first list)
TEST(RdbListTest, MergeTestPosdbVerifyListOrder) {
	g_conf.m_logTraceRdbList = true;
	char key[MAX_KEY_BYTES];

	// setup test
	RdbList list1;
	list1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, false), 0, NULL);
	list1.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, false), 0, NULL);

	RdbList list2;
	list2.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x01, true), 0, NULL);
	list2.addRecord(makePosdbKey(key, 0x01, 0x01, 0x02, true), 0, NULL);
	list2.addRecord(makePosdbKey(key, 0x01, 0x02, 0x01, true), 0, NULL);
	list2.addRecord(makePosdbKey(key, 0x02, 0x01, 0x01, true), 0, NULL);
	list2.addRecord(makePosdbKey(key, 0x02, 0x02, 0x01, true), 0, NULL);

	// keys go from present to deleted
	RdbList *lists1[2];
	lists1[0] = &list1;
	lists1[1] = &list2;

	RdbList final1;
	final1.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final1.prepareForMerge(lists1, 2, -1);
	final1.merge_r(lists1, 2, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB);

	// verify merged list
	EXPECT_EQ(0, final1.getListSize());

	// keys go from deleted to present
	RdbList *lists2[2];
	lists2[0] = &list2;
	lists2[1] = &list1;

	RdbList final2;
	final2.set(NULL, 0, NULL, 0, 0, true, Posdb::getUseHalfKeys(), Posdb::getKeySize());
	final2.prepareForMerge(lists2, 2, -1);
	final2.merge_r(lists2, 2, KEYMIN(), KEYMAX(), -1, true, RDB_POSDB);

	// verify merged list
	EXPECT_EQ(list1.getListSize(), final2.getListSize());
	for (list1.resetListPtr(), final2.resetListPtr(); !final2.isExhausted(); list1.skipCurrentRecord(), final2.skipCurrentRecord()) {
		EXPECT_EQ(list1.getCurrentRecSize(), final2.getCurrentRecSize());
		EXPECT_EQ(0, memcmp(list1.getCurrentRec(), final2.getCurrentRec(), list1.getCurrentRecSize()));
	}
}

//int cmpKey (const void *h1, const void *h2) {
//	if ( *(key96_t *)h1 < *(key96_t *)h2 ) return -1;
//	if ( *(key96_t *)h1 > *(key96_t *)h2 ) return  1;
//	return 0;
//}
//
//// copied from mergetest.cpp
//TEST(RdbListTest, MergeTest1) {
//	g_conf.m_logTraceRdbList = true;
//	// seed with same value so we get same rand sequence for all
//	srand ( 1945687 );
//	// # of keys to in each list
//	int32_t nk = 200000;
//	// # keys wanted
//	int32_t numKeysWanted = 200000;
//	// get # lists to merge
//	int32_t numToMerge = 4 ;
//	// print start time
//	logf(LOG_DEBUG,"smt:: randomizing begin. %" PRId32" lists of %" PRId32" keys.", numToMerge, nk);
//
//	// make a list of compressed (6 byte) docIds
//	key96_t *keys0 = (key96_t *)malloc(sizeof(key96_t) * nk);
//	key96_t *keys1 = (key96_t *)malloc(sizeof(key96_t) * nk);
//	key96_t *keys2 = (key96_t *)malloc(sizeof(key96_t) * nk);
//	key96_t *keys3 = (key96_t *)malloc(sizeof(key96_t) * nk);
//
//	// store radnom docIds in this list
//	uint32_t *p = (uint32_t *)keys0;
//	// random docIds
//	for (int32_t i = 0; i < nk; i++) {
//		*p++ = rand();
//		*p++ = rand();
//		*p++ = rand();
//	}
//	p = (uint32_t *)keys1;
//	for (int32_t i = 0; i < nk; i++) {
//		*p++ = rand();
//		*p++ = rand();
//		*p++ = rand();
//	}
//	p = (uint32_t *)keys2;
//	for (int32_t i = 0; i < nk; i++) {
//		*p++ = rand();
//		*p++ = rand();
//		*p++ = rand();
//	}
//	p = (uint32_t *)keys3;
//	for (int32_t i = 0; i < nk; i++) {
//		*p++ = rand();
//		*p++ = rand();
//		*p++ = rand();
//	}
//
//	// sort em up
//	gbsort(keys0, nk, sizeof(key96_t), cmpKey);
//	gbsort(keys1, nk, sizeof(key96_t), cmpKey);
//	gbsort(keys2, nk, sizeof(key96_t), cmpKey);
//	gbsort(keys3, nk, sizeof(key96_t), cmpKey);
//
//	// set lists
//	RdbList list0;
//	RdbList list1;
//	RdbList list2;
//	RdbList list3;
//
//	list0.set((char *)keys0, nk * sizeof(key96_t), (char *)keys0, nk * sizeof(key96_t),
//	          0,
//	          false,
//	          false, sizeof(key96_t));
//	list1.set((char *)keys1,
//	          nk * sizeof(key96_t),
//	          (char *)keys1,
//	          nk * sizeof(key96_t),
//	          0,
//	          false,
//	          false, sizeof(key96_t));
//	list2.set((char *)keys2,
//	          nk * sizeof(key96_t),
//	          (char *)keys2,
//	          nk * sizeof(key96_t),
//	          0,
//	          false,
//	          false, sizeof(key96_t));
//	list3.set((char *)keys3,
//	          nk * sizeof(key96_t),
//	          (char *)keys3,
//	          nk * sizeof(key96_t),
//	          0,
//	          false,
//	          false, sizeof(key96_t));
//
//	// merge
//	RdbList *lists[4];
//	lists[0] = &list0;
//	lists[1] = &list1;
//	lists[2] = &list2;
//	lists[3] = &list3;
//
//	RdbList list;
//	list.prepareForMerge (lists,numToMerge,numKeysWanted * sizeof(key96_t));
//
//	// start time
//	logf(LOG_DEBUG,"starting merge");
//	int64_t t = gettimeofdayInMilliseconds();
//	// do it
//	list.merge_r(lists, numToMerge, KEYMIN(), KEYMAX(), numKeysWanted, true, RDB_NONE);
//
//	// completed
//	int64_t now = gettimeofdayInMilliseconds();
//
//	logf(LOG_DEBUG, "smt:: %" PRId32" list NEW MERGE took %" PRIu64" ms", numToMerge,now-t);
//	// time per key
//	int32_t size = list.getListSize() / sizeof(key96_t);
//	double tt = ((double)(now - t))*1000000.0 / ((double)size);
//	logf(LOG_DEBUG,"smt:: %f nanoseconds per key", tt);
//
//	// stats
//	double d = (1000.0*(double)(size)) / ((double)(now - t));
//	logf(LOG_DEBUG,"smt:: %f cycles per final key" , 400000000.0 / d );
//	logf(LOG_DEBUG,"smt:: we can do %" PRId32" adds per second" ,(int32_t)d);
//
//	logf(LOG_DEBUG,"smt:: final list size = %" PRId32"",list.getListSize());
//}
//
//static bool addRecord(RdbList *list, uint32_t n1, uint64_t n0) {
//	key96_t k;
//	k.n1 = n1;
//	k.n0 = n0;
//
//	return list->addRecord((const char*)&k, 0, NULL);
//}
//
//// copied from mergetest.cpp
//TEST(RdbListTest, MergeTest2) {
//	g_conf.m_logTraceRdbList = true;
//
//	RdbList list1;
//	list1.set(NULL, 0, NULL, 0, 0, true, true, sizeof(key96_t));
//
//	RdbList list2;
//	list2.set(NULL, 0, NULL, 0, 0, true, true, sizeof(key96_t));
//
//	RdbList list3;
//	list3.set(NULL, 0, NULL, 0, 0, true, true, sizeof(key96_t));
//
//	// make a list of keys
//	addRecord(&list1, 0xa0, 0x01LL);
//	addRecord(&list1, 0xb0, 0x00LL);
//	addRecord(&list1, 0xa0, 0x01LL);
//
//
//	addRecord(&list2, 0xa0, 0x00LL);
//	addRecord(&list2, 0xb0, 0x01LL);
//	addRecord(&list2, 0xd0, 0x00LL);
//
//	addRecord(&list3, 0xa0, 0x01LL);
//	addRecord(&list3, 0xb0, 0x01LL);
//	addRecord(&list3, 0xd0, 0x01LL);
//	addRecord(&list3, 0xd0, 0x00LL);
//	addRecord(&list3, 0xd0, 0x00LL);
//	addRecord(&list3, 0xd0, 0x00LL);
//	addRecord(&list3, 0xe0, 0x01LL);
//	addRecord(&list3, 0xe0, 0x81LL);
//
//	RdbList *lists[3];
//	lists[0] = &list1;
//	lists[1] = &list2;
//	lists[2] = &list3;
//
//	RdbList final;
//	final.set(NULL, 0, NULL, 0, 0, true, true, sizeof(key96_t));
//
//	int32_t min = -1;
//	final.prepareForMerge(lists, 3, min);
//
//	// print all lists
//	logf(LOG_DEBUG, "-------list #1-------");
//	list1.printList();
//	logf(LOG_DEBUG, "-------list #2-------");
//	list2.printList();
//	logf(LOG_DEBUG, "-------list #3-------");
//	list3.printList();
//
//	final.merge_r(lists, 3, KEYMIN(), KEYMAX(), min, true, RDB_NONE);
//
//	logf(LOG_DEBUG,"------list final------");
//	final.printList();
//}
