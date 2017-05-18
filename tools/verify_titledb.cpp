#include "XmlDoc.h"
#include "Collectiondb.h"
#include "SpiderCache.h"
#include "Titledb.h"
#include "Doledb.h"
#include "CountryCode.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include <libgen.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH\n", argv0);
	fprintf(stdout, "Verify titledb\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
}

static void cleanup() {
	g_log.m_disabled = true;

	g_linkdb.reset();
	g_clusterdb.reset();
	g_spiderCache.reset();
	g_doledb.reset();
	g_spiderdb.reset();
	g_tagdb.reset();
	g_titledb.reset();
	g_posdb.reset();

	g_collectiondb.reset();

	g_loop.reset();
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

	g_log.m_disabled = true;

	// initialize library
	g_mem.init();
	hashinit();

	// current dir
	char path[PATH_MAX];
	realpath(argv[1], path);
	size_t pathLen = strlen(path);
	if (path[pathLen] != '/') {
		strcat(path, "/");
	}

	g_hostdb.init(-1, false, false, path);
	g_conf.init(path);

	ucInit();

	// initialize rdbs
	g_loop.init();

	g_collectiondb.loadAllCollRecs();

	g_posdb.init();
	g_titledb.init();
	g_tagdb.init();
	g_spiderdb.init();
	g_doledb.init();
	g_spiderCache.init();
	g_clusterdb.init();
	g_linkdb.init();

	g_collectiondb.addRdbBaseToAllRdbsForEachCollRec();

	g_log.m_disabled = true;
	g_log.m_logPrefix = false;

	CollectionRec *cr = g_collectiondb.getRec("main");
	if (!cr) {
		logf(LOG_TRACE, "No main collection found");
	}

	Msg5 msg5;
	RdbList list;

	key96_t startKey;

	key96_t endKey;
	endKey.setMax();

	for (;;) {
		if (!msg5.getList(RDB_TITLEDB, cr->m_collnum, &list, &startKey, &endKey, 500000000, true, 0, -1, NULL, NULL, 0, true, -1, false)) {
			logf(LOG_TRACE, "msg5.getlist didn't block");
			break;
		}

		if (list.isEmpty()) {
			break;
		}

		for (list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			XmlDoc xmlDoc;
			key96_t key = list.getCurrentKey();
			int64_t docId = Titledb::getDocIdFromKey(&key);
			if (!xmlDoc.set2(list.getCurrentRec(), list.getCurrentRecSize(), "main", NULL, 0)) {
				logf(LOG_TRACE, "Unable to set XmlDoc for docId=%" PRIu64, docId);
				break;
			}

			//fprintf(stdout, "Processing docid=%" PRId64"\r", docId);

			time_t ts = xmlDoc.m_spideredTime;
			struct tm tm_buf;
			struct tm *timeStruct = localtime_r(&ts,&tm_buf);
			char buf[128];
			strftime(buf, 128, "%b-%d-%Y %H:%M:%S", timeStruct);

			// validate linkinfo
			if (xmlDoc.ptr_linkInfo1->m_version != 0 ||
			    xmlDoc.ptr_linkInfo1->m_lisize < 0 || xmlDoc.ptr_linkInfo1->m_lisize != xmlDoc.size_linkInfo1 ||
			    xmlDoc.ptr_linkInfo1->m_numStoredInlinks < 0 || xmlDoc.ptr_linkInfo1->m_numGoodInlinks < 0) {
				fprintf(stderr, "\ndocid=%" PRId64" url='%.*s spidered='%s'\n", docId, xmlDoc.size_firstUrl, xmlDoc.ptr_firstUrl, buf);
			}
		}
		startKey = *(key96_t *)list.getLastKey();
		startKey++;
		// watch out for wrap around
		if (startKey < *(key96_t *) list.getLastKey()) {
			break;
		}
	}

	cleanup();

	return 0;
}

