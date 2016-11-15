#include "Linkdb.h"
#include "Posdb.h"
#include "Spider.h"
#include "Log.h"
#include "Conf.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] RDBNAME KEY\n", argv0);
	fprintf(stdout, "Decode RDB key\n");
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

int main(int argc, char **argv) {
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "--h") == 0 || strcmp(argv[1], "--help") == 0 ) {
		print_usage(argv[0]);
		return 1;
	}

	// initialize library
	g_mem.init();
	hashinit();

	g_conf.init(NULL);

	// parse key
	const char *input = argv[2];
	size_t inputLen = strlen(input);
	if (starts_with(input, "0x")) {
		input += 2;
		inputLen -= 2;
	}

	char key[MAX_KEY_BYTES] = {0};
	const char *p = input + inputLen - 1;
	char *pKey = key;
	for ( ; p >= input ; ) {
		auto first = htob(*p--);
		auto second = htob(*p--);
		*pKey  = second;
		*pKey <<= 4;
		*pKey |= first;
		pKey++;
	}

	const char *rdb = argv[1];
	if (strcmp(rdb, "linkdb") == 0) {
		Linkdb::printKey(key);
	} else if (strcmp(rdb, "posdb") == 0) {
		Posdb::printKey(key);
	} else if (strcmp(rdb, "spiderdb") == 0) {
		Spiderdb::printKey(key);
	} else {
		fprintf(stdout, "Unsupported RDB %s\n", rdb);
		return 1;
	}

	return 0;
}
