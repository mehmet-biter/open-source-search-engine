#include "gtest/gtest.h"

#include "Mem.h"
#include "Unicode.h"
#include "hash.h"
#include "Conf.h"

bool g_recoveryMode = false;
int32_t g_recoveryLevel = 0;

int main(int argc, char **argv) {
	// initialize Gigablast
	g_conf.m_maxMem = 1000000000LL;

	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();

	g_log.init("/dev/stdout");

	if ( !ucInit() ) {
		log("Unicode initialization failed!");
		exit(1);
	}

	hashinit();

	::testing::InitGoogleTest(&argc, argv);

	int ret = RUN_ALL_TESTS();

	resetDecompTables();

	return ret;
}
