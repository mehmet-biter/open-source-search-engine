#include "XmlDoc.h"
#include "Collectiondb.h"
#include "SpiderCache.h"
#include "Titledb.h"
#include "Doledb.h"
#include "CountryCode.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "UrlBlockCheck.h"
#include "UrlMatchList.h"
#include "WantedChecker.h"
#include "ip.h"
#include "utf8_convert.h"
#include <libgen.h>
#include <algorithm>
#include <limits.h>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH\n", argv0);
	fprintf(stdout, "Dump titlerec with wrong encoding\n");
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

	WantedChecker::finalize();
}

static bool find_str(const char *haystack, size_t haystackLen, const std::string &needle) {
	return (memmem(haystack, haystackLen, needle.c_str(), needle.size()) != nullptr);
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

	if (!utf8_convert_initialize()) {
		log(LOG_ERROR, "db: utf-8 conversion initialization failed!");
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
		return 1;
	}

	// initialize shlib & blacklist
	if (!WantedChecker::initialize()) {
		fprintf(stderr, "Unable to initialize WantedChecker");
		return 1;
	}

	g_urlBlackList.init();
	g_urlWhiteList.init();

	Msg5 msg5;
	RdbList list;

	key96_t startKey;
	startKey.setMin();

	key96_t endKey;
	endKey.setMax();

	std::vector<std::pair<eIANACharset, std::string>> wrong_encodings;

	std::vector<const char*> inputs = {
		// danish
		"æ",
		"ø",
		"å",
		"Æ",
		"Ø",
		"Å",
		// swedish
		"ä",
		"ö",
		"Ä",
		"Ö",
		// german
		"ü",
		"ß",
		"Ü",
		"ẞ",
	};

	std::vector<std::pair<eIANACharset,eIANACharset>> from_to_charsets = {
		std::make_pair(csUTF8, csISOLatin1),
		std::make_pair(csUTF8, csISOLatin2),
		std::make_pair(csUTF8, csISOLatin4),
		std::make_pair(csUTF8, cslatin6),
		std::make_pair(csUTF8, cswindows1250),
		std::make_pair(csUTF8, cswindows1252),
		std::make_pair(csUTF8, cswindows1257),
	};

	for (const auto &from_to_charset : from_to_charsets) {
		for (const auto &input : inputs) {
			char buffer[8];
			if (ucToAny(buffer, sizeof(buffer), get_charset_str(from_to_charset.first),
			            input, strlen(input), get_charset_str(from_to_charset.second), -1) > 0) {
				// successful
				std::string bufferStr(buffer);
				wrong_encodings.emplace_back(from_to_charset.second, bufferStr);
			}
		}
	}

	while (msg5.getList(RDB_TITLEDB, cr->m_collnum, &list, &startKey, &endKey, 10485760, true, 0, -1, NULL, NULL, 0, true, -1, false)) {
		if (list.isEmpty()) {
			break;
		}

		for (list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			key96_t key = list.getCurrentKey();
			int64_t docId = Titledb::getDocIdFromKey(&key);

			XmlDoc xmlDoc;
			if (!xmlDoc.set2(list.getCurrentRec(), list.getCurrentRecSize(), "main", NULL, 0)) {
				logf(LOG_TRACE, "Unable to set XmlDoc for docId=%" PRIu64, docId);
				continue;
			}

			// ignore empty titlerec
			if (xmlDoc.size_utf8Content == 0) {
				continue;
			}

			for (auto const &wrong_encoding : wrong_encodings) {
				if ((xmlDoc.m_charset == wrong_encoding.first) &&
					find_str(xmlDoc.ptr_utf8Content, xmlDoc.size_utf8Content, wrong_encoding.second)) {
					int32_t *firstIp = xmlDoc.getFirstIp();
					if (!firstIp || firstIp == (int32_t *)-1) {
						logf(LOG_TRACE, "Blocked firstIp for docId=%" PRId64, docId);
						continue;
					}

					Url *url = xmlDoc.getFirstUrl();

					char ipbuf[16];
					fprintf(stdout, "%" PRId64"|%s|bad encoding %s|%s\n",
					        docId, iptoa(*firstIp, ipbuf), get_charset_str(xmlDoc.m_charset), url->getUrl());

					// don't check for more
					break;
				}
			}
		}

		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if (startKey < *(key96_t *)list.getLastKey()) {
			break;
		}
	}

	cleanup();

	return 0;
}
