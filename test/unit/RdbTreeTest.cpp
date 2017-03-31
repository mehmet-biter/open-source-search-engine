#include <gtest/gtest.h>
#include <fctypes.h>
#include <Mem.h>
#include <sort.h>
#include "RdbTree.h"
#include "Log.h"

int keycmp(const void *p1, const void *p2) {
	// returns 0 if equal, -1 if p1 < p2, +1 if p1 > p2
	if ( *(key96_t *)p1 < *(key96_t *)p2 ) return -1;
	if ( *(key96_t *)p1 > *(key96_t *)p2 ) return  1;
	return 0;
}

// moved from main.cpp
TEST(RdbTreeTest, HashTest) {
	int32_t numKeys = 500000;
	log(LOG_INFO, "db: speedtest: generating %" PRId32" random keys.", numKeys);

	// seed randomizer
	srand((int32_t)gettimeofdayInMilliseconds());

	// make list of one million random keys
	key96_t *k = (key96_t *)mmalloc(sizeof(key96_t) * numKeys, "main");
	ASSERT_TRUE(k);

	int32_t *r = (int32_t *)(void *)k;
	int32_t size = 0;
	int32_t first = 0;
	for (int32_t i = 0; i < numKeys * 3; i++) {
		if ((i % 3) == 2 && first++ < 50000) {
			r[i] = 1234567;
			size++;
		} else
			r[i] = rand();
	}

	// init the tree
	RdbTree rt;
	ASSERT_TRUE(rt.set(0, numKeys + 1000, numKeys * 28, false, "tree-test"));

	// add to regular tree
	int64_t t = gettimeofdayInMilliseconds();
	for (int32_t i = 0; i < numKeys; i++) {
		ASSERT_GE(rt.addNode_unlocked((collnum_t)0, (const char *)&(k[i]), NULL, 0), 0);
	}
	// print time it took
	int64_t e = gettimeofdayInMilliseconds();
	log(LOG_INFO, "db: added %" PRId32" keys to rdb tree in %" PRId64" ms", numKeys, e - t);

	// sort the list of keys
	t = gettimeofdayInMilliseconds();
	gbsort(k, numKeys, sizeof(key96_t), keycmp);
	// print time it took
	e = gettimeofdayInMilliseconds();
	log(LOG_INFO, "db: sorted %" PRId32" in %" PRId64" ms", numKeys, e - t);

	// get the list
	key96_t kk;
	kk.n0 = 0LL;
	kk.n1 = 1234567;
	int32_t n = rt.getNextNode_unlocked((collnum_t)0, (char *)&kk);
	// loop it
	t = gettimeofdayInMilliseconds();
	int32_t count = 0;
	while (n >= 0 && --first >= 0) {
		n = rt.getNextNode_unlocked(n);
		count++;
	}
	e = gettimeofdayInMilliseconds();
	log(LOG_INFO, "db: getList for %" PRId32" nodes in %" PRId64" ms", count, e - t);
}