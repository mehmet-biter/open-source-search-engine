#include "RdbBuckets.h"
#include "Posdb.h"
#include "Log.h"
#include "Conf.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] FILE\n", argv0);
	fprintf(stdout, "Dump records in RDB buckets\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
}

static bool starts_with(const char *haystack, const char *needle) {
	size_t haystackLen = strlen(haystack);
	size_t needleLen = strlen(needle);
	if (haystackLen < needleLen) {
		return false;
	}

	return (memcmp(haystack, needle, needleLen) == 0);
}

static void printRecord(const char *key, int32_t ks) {
	if (ks == sizeof(posdbkey_t)) {
		Posdb::printKey(key);
	} else {
		fprintf(stdout, "Unsupported rdb type\n");
		exit(1);
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "--h") == 0 || strcmp(argv[1], "--help") == 0 ) {
		print_usage(argv[0]);
		return 1;
	}

	char filepath[PATH_MAX];

	char dir[PATH_MAX];
	strcpy(filepath, argv[1]);
	strcpy(dir, dirname(filepath));

	char filename[PATH_MAX];
	strcpy(filepath, argv[1]);
	strcpy(filename, basename(filepath));

	// initialize library
	g_mem.init();
	hashinit();

	g_conf.init(NULL);

	BigFile bigFile;
	bigFile.set(dir, filename);

	RdbBuckets buckets;
	if (starts_with(filename, "posdb")) {
		buckets.set(Posdb::getFixedDataSize(), g_conf.m_posdbMaxTreeMem, "buckets-posdb", RDB_POSDB, "posdb", Posdb::getKeySize());
		if (!buckets.fastLoad(&bigFile, "posdb")) {
			fprintf(stdout, "Unable to load bucket\n");
			return 1;
		}

		buckets.printBuckets(printRecord);
	} else {
		fprintf(stdout, "Unsupported rdb type\n");
		return 1;
	}

	return 0;
}
