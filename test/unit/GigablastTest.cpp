#include "gtest/gtest.h"

#include "Mem.h"
#include "Unicode.h"

int g_inMemcpy=0;
bool g_recoveryMode = false;
int32_t g_recoveryLevel = 0;

bool sendPageSEO(TcpSocket *, HttpRequest *) __attribute__((weak));

// make the stubs here. seo.o will override them
bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {
	//	return g_httpServer.sendErrorReply(s,500,"Seo support not present");
	return true;
}

int main(int argc, char **argv) {
	// initialize Gigablast
	g_conf.m_maxMem = 1000000000LL;

	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();

	g_log.init("/dev/stdout");
	g_conf.m_logDebugBuild = true;

	if (!ucInit()) {
		log("Unicode initialization failed!");
		exit(1);
	}

	::testing::InitGoogleTest(&argc, argv);

	int ret = RUN_ALL_TESTS();

	resetDecompTables();

	return ret;
}
