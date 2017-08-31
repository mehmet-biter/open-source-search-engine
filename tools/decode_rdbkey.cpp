#include "Linkdb.h"
#include "Posdb.h"
#include "Spider.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "GbUtil.h"
#include "types.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] RDBNAME KEY\n", argv0);
	fprintf(stdout, "Decode RDB key\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
}

int main(int argc, char **argv) {
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ) {
		print_usage(argv[0]);
		return 1;
	}

	// initialize library
	g_mem.init();
	hashinit();

	g_conf.init(NULL);

	const char *keyPtr = NULL;
	char keyBytes[MAX_KEY_BYTES] = {0};
	key96_t key96;
	if (argc == 3) {
		// parse key
		const char *input = argv[2];
		size_t inputLen = strlen(input);
		if (starts_with(input, "0x")) {
			input += 2;
			inputLen -= 2;
		}

		const char *p = input + inputLen - 1;
		char *pKey = keyBytes;
		for ( ; p >= input ; ) {
			auto first = htob(*p--);
			auto second = htob(*p--);
			*pKey  = second;
			*pKey <<= 4;
			*pKey |= first;
			pKey++;
		}
		keyPtr = keyBytes;
	} else if (argc == 4) {
		key96.n0 = strtoull(argv[2], NULL, 0);
		key96.n1 = strtoul(argv[3], NULL, 0);
		keyPtr = (char*)&key96;
	}

	const char *rdb = argv[1];
	if (strcmp(rdb, "linkdb") == 0) {
		Linkdb::printKey(keyPtr);
	} else if (strcmp(rdb, "posdb") == 0) {
		Posdb::printKey(keyPtr);
	} else if (strcmp(rdb, "spiderdb") == 0) {
		Spiderdb::printKey(keyPtr);
	} else if (strcmp(rdb, "titledb") == 0) {
		Titledb::printKey(keyPtr);
	} else {
		fprintf(stdout, "Unsupported RDB %s\n", rdb);
		return 1;
	}

	return 0;
}
