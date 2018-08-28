#include "Url.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "hash.h"
#include "Version.h"
#include <sys/stat.h>
#include <errno.h>
#include <string>
#include <fstream>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] FILE\n", argv0);
	fprintf(stdout, "Clean url (normalize, strip unwanted parameters)\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
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

	// initialize library
	g_mem.init();
	hashinit();

	g_conf.init(NULL);

	g_log.m_logPrefix = false;

	const char *filename = argv[1];

	struct stat st;
	if (stat(filename, &st) != 0) {
		// probably not found
		fprintf(stderr, "Unable to load file %s", argv[1]);
		return ENOENT;
	}

	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0) {
			continue;
		}

		Url url;
		url.set(line.c_str(), line.length(), false, true);
		fprintf(stdout, "%s\n", url.getUrl());

//		bool modified = (strcmp(line.c_str(), url.getUrl()) != 0);
//		if (modified) {
//			fprintf(stdout, "%s\n", url.getUrl());
//			fprintf(stdout, "%s\n%s\n", line.c_str(), url.getUrl());
//		}
	}

	return 0;
}
