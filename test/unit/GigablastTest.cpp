#include <gtest/gtest.h>

#include "Mem.h"
#include "Unicode.h"
#include "hash.h"
#include "Conf.h"
#include "Hostdb.h"

#include <stdlib.h>
#include <libgen.h>
#include <limits.h>

int main(int argc, char **argv) {
	// initialize library
	g_mem.init();
	hashinit();

	// current dir
	char tmpPath[PATH_MAX];
	realpath(argv[0], tmpPath);

	char currentPath[PATH_MAX];
	strcpy(currentPath, dirname(tmpPath));
	size_t currentPathLen = strlen(currentPath);
	if (currentPath[currentPathLen] != '/') {
		strcat(currentPath, "/");
	}

	g_hostdb.init(-1, NULL, false, false, currentPath);
	g_conf.init(NULL);

	g_log.init("/dev/stdout");

	if ( !ucInit() ) {
		log("Unicode initialization failed!");
		exit(1);
	}

	::testing::InitGoogleTest(&argc, argv);

	int ret = RUN_ALL_TESTS();

	resetDecompTables();

	return ret;
}
