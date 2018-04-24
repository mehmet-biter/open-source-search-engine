#include "XmlDoc.h"
#include "Collectiondb.h"
#include "SpiderCache.h"
#include "Titledb.h"
#include "Doledb.h"
#include "CountryCode.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "Domains.h"
#include <libgen.h>
#include <limits.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH DOCID\n", argv0);
	fprintf(stdout, "Print titlerec\n");
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
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 ) {
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

	g_hostdb.init(-1, false, false, true, path);
	g_conf.init(path);

	const char *errmsg;
	if (!UnicodeMaps::load_maps("ucdata",&errmsg)) {
		log("Unicode initialization failed!");
		exit(1);
	}

	if(!initializeDomains(g_hostdb.m_dir)) {
		log("Domains initialization failed!");
		exit(1);
	}

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

	g_log.m_disabled = false;
	g_log.m_logPrefix = false;

	CollectionRec *cr = g_collectiondb.getRec("main");
	if (!cr) {
		logf(LOG_TRACE, "No main collection found");
	}

	uint64_t docId = strtoul(argv[2], NULL, 10);
	logf(LOG_TRACE, "Getting titlerec for docId=%" PRIu64, docId);

	Msg5 msg5;
	RdbList list;

	key96_t startKey = Titledb::makeFirstKey(docId);
	key96_t endKey = Titledb::makeLastKey(docId);

	if (!msg5.getList(RDB_TITLEDB, cr->m_collnum, &list, &startKey, &endKey, 10485760, true, 0, -1, NULL, NULL, 0, true, -1, false)) {
		logf(LOG_TRACE, "msg5.getlist didn't block");
		cleanup();
		exit(1);
	}

	if (list.isEmpty()) {
		logf(LOG_TRACE, "Unable to find titlerec for docId=%" PRIu64, docId);
		cleanup();
		exit(1);
	}

	for (list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
		XmlDoc xmlDoc;
		if (!xmlDoc.set2(list.getCurrentRec(), list.getCurrentRecSize(), "main", NULL, 0)) {
			logf(LOG_TRACE, "Unable to set XmlDoc for docId=%" PRIu64, docId);
			cleanup();
			exit(1);
		}

		logf(LOG_TRACE, "XmlDoc info");
		logf(LOG_TRACE, "\tfirstUrl   : %.*s", xmlDoc.size_firstUrl, xmlDoc.ptr_firstUrl);
		logf(LOG_TRACE, "\tredirUrl   : %.*s", xmlDoc.size_redirUrl, xmlDoc.ptr_redirUrl);
		logf(LOG_TRACE, "\trootTitle  : %.*s", xmlDoc.size_rootTitleBuf, xmlDoc.ptr_rootTitleBuf);
		//	logf(LOG_TRACE, "\timageData  :");
		logf(LOG_TRACE, "\t");
		loghex(LOG_TRACE, xmlDoc.ptr_utf8Content, xmlDoc.size_utf8Content, "\tutf8Content:");
		logf(LOG_TRACE, "\tsite       : %.*s", xmlDoc.size_site, xmlDoc.ptr_site);

		logf(LOG_TRACE, "\tlinkInfo (from titlerec)");
		LinkInfo *linkInfo = xmlDoc.getLinkInfo1();
		logf(LOG_TRACE, "\t\tm_numGoodInlinks     : %d", linkInfo->m_numGoodInlinks);
		logf(LOG_TRACE, "\t\tm_numInlinksInternal : %d", linkInfo->m_numInlinksInternal);
		logf(LOG_TRACE, "\t\tm_numStoredInlinks   : %d", linkInfo->m_numStoredInlinks);

		int i = 0;
		for (Inlink *inlink = linkInfo->getNextInlink(NULL); inlink; inlink = linkInfo->getNextInlink(inlink)) {
			logf(LOG_TRACE, "\t\tinlink #%d", i++);
			logf(LOG_TRACE, "\t\t\tdocId        : %" PRIu64, inlink->m_docId);
			logf(LOG_TRACE, "\t\t\turl          : %s", inlink->getUrl());
			logf(LOG_TRACE, "\t\t\tlinktext     : %s", inlink->getLinkText());
			logf(LOG_TRACE, "\t\t\tcountry      : %s", getCountryCode(inlink->m_country));
			logf(LOG_TRACE, "\t\t\tlanguage     : %s", getLanguageAbbr(inlink->m_language));
		}

		loghex(LOG_TRACE, xmlDoc.ptr_linkdbData, xmlDoc.size_linkdbData, "\tlinkdbData");

		logf(LOG_TRACE, "\ttagRec (from titlerec)");
		TagRec *tagRec = xmlDoc.getTagRec();
		for (Tag *tag = tagRec->getFirstTag(); tag; tag = tagRec->getNextTag(tag)) {
			SafeBuf sb;
			tag->printDataToBuf(&sb);
			logf(LOG_TRACE, "\t\t%-12s: %s", getTagStrFromType(tag->m_type), sb.getBufStart());
		}

		logf(LOG_TRACE, "\t");

		logf(LOG_TRACE, "Links info (from parsed titlerec)");
		g_log.m_disabled = true;
		Links *links = xmlDoc.getLinks();
		g_log.m_disabled = false;
		for (int i = 0; i < links->getNumLinks(); ++i) {
			Url u;
			u.set( links->getLinkPtr(i), links->getLinkLen(i), true, false );
			uint32_t h32 = u.getHostHash32();
			int64_t uh64 = hash64n(u.getUrl(), u.getUrlLen());

			logf(LOG_TRACE, "\tlink      : %.*s", links->getLinkLen(i), links->getLinkPtr(i));
			logf(LOG_TRACE, "\tsitehash32: 0x%08" PRIx32 ", urlhash=0x%012" PRIx64 "", h32, uh64);
		}
	}
	cleanup();

	return 0;
}
