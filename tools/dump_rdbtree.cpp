#include "RdbTree.h"
#include "Titledb.h"
#include "Collectiondb.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "GbUtil.h"
#include "Version.h"
#include <libgen.h>
#include <limits.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] FILE\n", argv0);
	fprintf(stdout, "Dump records in RDB tree\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
}

static void printRecord(rdbid_t rdbId, const char *key) {
	switch (rdbId) {
		case RDB_TITLEDB:
			Titledb::printKey(key);
			break;
		default:
			fprintf(stdout, "Unsupported rdb type\n");
			exit(1);
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ) {
		print_usage(argv[0]);
		return 1;
	}

    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0 ) {
        printVersion(basename(argv[0]));
        return 1;
    }

	char filepath[PATH_MAX];


	char dir[PATH_MAX];
	realpath(argv[1], filepath);
	strcpy(dir, dirname(filepath));
	size_t dirLen = strlen(dir);
	if (dir[dirLen] != '/') {
		strcat(dir, "/");
	}

	char filename[PATH_MAX];
	strcpy(filepath, argv[1]);
	strcpy(filename, basename(filepath));

	// initialize library
	g_mem.init();
	hashinit();

	g_hostdb.init(-1, false, false, true, dir);
	g_conf.init(NULL);
	g_collectiondb.loadAllCollRecs();

	strcpy(g_hostdb.m_dir, dir);

	RdbTree tree;
	if (starts_with(filename, "titledb")) {
		int32_t maxTreeNodes  = g_conf.m_titledbMaxTreeMem / (1*1024);
		tree.set(Titledb::getFixedDataSize(), maxTreeNodes, g_conf.m_titledbMaxTreeMem, false, "tree-titledb", "titledb", Titledb::getKeySize(), RDB_TITLEDB);

		BigFile file;
		file.set(dir, filename);

		int32_t dataMem = g_conf.m_titledbMaxTreeMem - tree.getTreeOverhead();
		RdbMem rdbMem;
		rdbMem.init(dataMem, "mem-titledb");
		if (!tree.fastLoad(&file, &rdbMem)) {
			fprintf(stdout, "Unable to load tree\n");
			return 1;
		}

		tree.printTree(printRecord);
	} else {
		fprintf(stdout, "Unsupported rdb type\n");
		return 1;
	}

	return 0;
}
