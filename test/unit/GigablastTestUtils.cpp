#include <gtest/gtest.h>
#include "GigablastTestUtils.h"
#include "Loop.h"
#include "Collectiondb.h"
#include "Statsdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Doledb.h"
#include "Clusterdb.h"
#include "Linkdb.h"

void GbTest::initializeRdbs() {
	ASSERT_TRUE(g_loop.init());
	ASSERT_TRUE(g_collectiondb.loadAllCollRecs());
	ASSERT_TRUE(g_statsdb.init());
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
	g_collectiondb.reset();
}

static UdpProtocol s_udpProtocol;

void GbTest::initializeUdpServer() {
	ASSERT_TRUE(g_udpServer.init(18200, &s_udpProtocol, 20000000, 20000000, 20, 3500, false));
	ASSERT_TRUE(Msg22::registerHandler());
}