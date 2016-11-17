#include "Url.h"
#include "SiteGetter.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] URL\n", argv0);
	fprintf(stdout, "Print url info\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
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

	Url url;
	url.set(input, inputLen);
	url.print();

	SiteGetter sg;
	sg.getSite(input, NULL, 0, 0, 0);
	logf(LOG_TRACE, "Site info");
	logf(LOG_TRACE, "  site         : %.*s", sg.getSiteLen(), sg.getSite());
	logf(LOG_TRACE, "  sitehash32   : %" PRIx32, hash32(sg.getSite(), sg.getSiteLen(), 0));

	return 0;
}
