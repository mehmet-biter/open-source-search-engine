#include <gtest/gtest.h>
#include "GigablastTestUtils.h"
#include "Loop.h"
#include "Collectiondb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "SpiderCache.h"
#include "Doledb.h"
#include "Clusterdb.h"
#include "Linkdb.h"

static void deleteRdbFiles() {
	// delete all rdb files
	for (rdbid_t rdbId = RDB_NONE; rdbId < RDB_END; rdbId = (rdbid_t)((int)rdbId + 1)) {
		Rdb *rdb = getRdbFromId(rdbId);
		if (rdb) {
			for (int32_t i = 0; i < rdb->getNumBases(); ++i ) {
				RdbBase *base = rdb->getBase(i);
				if (base) {
					for (int32_t j = 0; j < base->getNumFiles(); ++j) {
						ASSERT_TRUE(base->getFile(j)->unlink());
						ASSERT_TRUE(base->getMap(j)->unlink());

						if (base->getIndex(j)) {
							ASSERT_TRUE(base->getIndex(j)->unlink());
						}
					}

					if (base->getTreeIndex()) {
						ASSERT_TRUE(base->getTreeIndex()->unlink());
					}
				}
			}

			// unlink tree/buckets as well
			if (rdb->getNumUsedNodes() > 0) {
				std::string path(g_hostdb.m_dir);
				path.append("/").append(rdb->getDbname()).append(rdb->useTree() ? "" : "-buckets").append("-saved.dat");
				unlink(path.c_str());
			}
		}
	}
}

void GbTest::initializeRdbs() {
	ASSERT_TRUE(g_loop.init());

	ASSERT_TRUE(Rdb::initializeRdbDumpThread());

	ASSERT_TRUE(g_collectiondb.loadAllCollRecs());

	ASSERT_TRUE(g_posdb.init());
	ASSERT_TRUE(g_titledb.init());
	ASSERT_TRUE(g_tagdb.init());
	ASSERT_TRUE(g_spiderdb.init());
	ASSERT_TRUE(g_doledb.init());
	ASSERT_TRUE(g_spiderCache.init());
	ASSERT_TRUE(g_clusterdb.init());
	ASSERT_TRUE(g_linkdb.init());

	ASSERT_TRUE(g_collectiondb.addRdbBaseToAllRdbsForEachCollRec());
}

void GbTest::resetRdbs() {
	deleteRdbFiles();

	g_linkdb.reset();
	g_clusterdb.reset();
	g_spiderCache.reset();
	g_doledb.reset();
	g_spiderdb.reset();
	g_tagdb.reset();
	g_titledb.reset();
	g_posdb.reset();

	g_collectiondb.reset();

	Rdb::finalizeRdbDumpThread();

	g_loop.reset();
	new(&g_loop) Loop(); // some variables are not Loop::reset. Call the constructor to re-initialize them
}

void GbTest::addPosdbKey(Rdb *rdb, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey, bool isShardByTermId) {
	char key[MAX_KEY_BYTES];
	::Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, isDelKey, isShardByTermId);
	rdb->addRecord(0, key, NULL, 0);
}

void GbTest::addPosdbKey(RdbBuckets *buckets, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey) {
	char key[MAX_KEY_BYTES];
	::Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, isDelKey, false);
	buckets->addNode(0, key, NULL, 0);
}

void GbTest::addPosdbKey(RdbIndex *index, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey) {
    char key[MAX_KEY_BYTES];
	::Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, isDelKey, false);
    index->addRecord(key);
}

void GbTest::addPosdbKey(RdbList *list, int64_t termId, int64_t docId, int32_t wordPos, bool isDelKey) {
	char key[MAX_KEY_BYTES];
	::Posdb::makeKey(&key, termId, docId, wordPos, 0, 0, 0, 0, 0, 0, 0, false, isDelKey, false);
	list->addRecord(key, 0, NULL);
}
