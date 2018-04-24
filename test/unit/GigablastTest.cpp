#include <gtest/gtest.h>

#include "Mem.h"
#include "UCMaps.h"
#include "hash.h"
#include "Conf.h"
#include "Hostdb.h"
#include "Domains.h"

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

	g_hostdb.init(-1, false, false, true, currentPath);
	g_conf.init(NULL);

	g_log.init("/dev/stdout");

	// merge space
	strcpy(g_conf.m_mergespaceLockDirectory, currentPath);
	strcat(g_conf.m_mergespaceLockDirectory, "tmp/gb_merge_locks");
	strcpy(g_conf.m_mergespaceDirectory, currentPath);
	strcat(g_conf.m_mergespaceDirectory, "tmp/gb_merge_space");

	/// @todo ALC cleanup tmp merge space

	const char *errmsg;
	if ( !UnicodeMaps::load_maps("ucdata",&errmsg) ) {
		log("Unicode initialization failed!");
		exit(1);
	}

	if(!initializeDomains(g_hostdb.m_dir)) {
		log("Domains initialization failed!");
		exit(1);
	}

	::testing::InitGoogleTest(&argc, argv);

	int ret = RUN_ALL_TESTS();

	UnicodeMaps::unload_maps();

	return ret;
}
