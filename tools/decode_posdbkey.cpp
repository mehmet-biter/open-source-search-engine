#include "Posdb.h"
#include "Log.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] KEY\n", argv0);
	fprintf(stdout, "Decode PosDB key\n");
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
	if (argc < 2) {
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

	const char *input = argv[1];
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

	Posdb::printKey(key);

	return 0;
}
