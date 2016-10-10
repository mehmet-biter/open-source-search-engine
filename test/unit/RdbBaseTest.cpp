#include <gtest/gtest.h>
#include <Posdb.h>
#include "RdbBase.h"
#include "GigablastTestUtils.h"

class RdbBaseTest : public ::testing::Test {
public:
	static void SetUpTestCase() {
		g_conf.m_noInMemoryPosdbMerge = true;
	}

	static void TearDownTestCase() {
		g_conf.m_noInMemoryPosdbMerge = m_savedMergeConf;
	}

	void SetUp() {
		GbTest::initializeRdbs();
	}

	void TearDown() {
		GbTest::resetRdbs();
	}

	static bool m_savedMergeConf;
};

bool RdbBaseTest::m_savedMergeConf = g_conf.m_noInMemoryPosdbMerge;

class RdbBasePosdbIndexSingleDocTest : public RdbBaseTest, public ::testing::WithParamInterface<::testing::tuple<int64_t, int64_t, int64_t>> {
};

INSTANTIATE_TEST_CASE_P(RdbBasePosdbIndexSingleDoc,
                        RdbBasePosdbIndexSingleDocTest,
                        ::testing::Combine(::testing::Values(POSDB_DELETEDOC_TERMID, 1),
                                           ::testing::Values(POSDB_DELETEDOC_TERMID, 1),
                                           ::testing::Values(POSDB_DELETEDOC_TERMID, 1)));

TEST_P(RdbBasePosdbIndexSingleDocTest, PosdbGenerateIndexSingleDocId) {
	const collnum_t collNum = 0;
	const int64_t docId = 1;

	RdbBase *base = g_posdb.getRdb()->getBase(collNum);

	ASSERT_EQ(0, base->addNewFile(-1));
	RdbIndex *index0 = base->getIndex(0);
	int64_t termId0 = ::testing::get<0>(GetParam());
	GbTest::addPosdbKey(index0, termId0, docId, 0, ::testing::get<0>(GetParam()) == POSDB_DELETEDOC_TERMID);
	index0->writeIndex();
	index0->printIndex();


	ASSERT_EQ(1, base->addNewFile(-1));
	RdbIndex *index1 = base->getIndex(1);
	int64_t termId1 = ::testing::get<1>(GetParam());
	GbTest::addPosdbKey(index1, termId1, docId, 0, ::testing::get<1>(GetParam()) == POSDB_DELETEDOC_TERMID);
	index1->writeIndex();
	index1->printIndex();

	ASSERT_EQ(2, base->addNewFile(-1));
	RdbIndex *index2 = base->getIndex(2);
	int64_t termId2 = ::testing::get<2>(GetParam());
	GbTest::addPosdbKey(index2, termId2, docId, 0, ::testing::get<2>(GetParam()) == POSDB_DELETEDOC_TERMID);
	index2->writeIndex();
	index2->printIndex();

	base->generateGlobalIndex();
	auto globalIndex = base->getGlobalIndex();
	ASSERT_EQ(1, globalIndex->size());
	int64_t result = (((docId << RdbIndex::s_docIdOffset) | (!termId2 == POSDB_DELETEDOC_TERMID)) << RdbBase::s_docIdFileIndex_docIdOffset | 2);
	logf(LOG_TRACE, "inputDocId=%lld result=%llx first=%llx", docId, result, *globalIndex->begin());
	EXPECT_EQ(result, *globalIndex->begin());
	base->printGlobalIndex();
}
