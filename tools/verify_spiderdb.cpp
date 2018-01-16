#include "XmlDoc.h"
#include "Collectiondb.h"
#include "SpiderCache.h"
#include "Titledb.h"
#include "Doledb.h"
#include "CountryCode.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "third-party/sparsepp/sparsepp/spp.h"
#include <libgen.h>
#include <arpa/inet.h>
#include <fstream>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH FIRSTIPFILE\n", argv0);
	fprintf(stdout, "Verify spiderdb\n");
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

	char firstIpPath[PATH_MAX];
	realpath(argv[2], firstIpPath);

	g_hostdb.init(-1, false, false, true, path);
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


	spp::sparse_hash_map<uint64_t, uint32_t> hostFirstIpMap;

	// load firstIp file
	std::ifstream file(firstIpPath);
	std::string line;
	while (std::getline(file, line)) {
		// ignore empty lines
		if (line.length() == 0) {
			continue;
		}

		auto firstColEnd = line.find_first_of('|');

		std::string hostStr = line.substr(0, firstColEnd);
		std::string firstIpStr = line.substr(firstColEnd + 1);

		in_addr addr;
		if (inet_pton(AF_INET, firstIpStr.c_str(), &addr) != 1) {
			// invalid ip
			logTrace(true, "Ignoring invalid firstIp=%s", firstIpStr.c_str());
			continue;
		}

		uint64_t host = strtoull(hostStr.c_str(), nullptr, 0);
		hostFirstIpMap[host] = addr.s_addr;
	}

	Msg5 msg5;
	RdbList list;

	key128_t startKey;

	key128_t endKey;
	endKey.setMax();

	for (;;) {
		if (!msg5.getList(RDB_SPIDERDB, cr->m_collnum, &list, &startKey, &endKey, 10000000, true, 0, -1, NULL, NULL, 0, true, -1, false)) {
			logf(LOG_TRACE, "msg5.getlist didn't block");
			break;
		}

		if (list.isEmpty()) {
			break;
		}

		for (list.resetListPtr(); !list.isExhausted(); list.skipCurrentRecord()) {
			char *srec = list.getCurrentRec();

			if (Spiderdb::isSpiderReply((key128_t *)srec)) {
				continue;
			}

			const SpiderRequest *sreq = (SpiderRequest *)srec;
			if (KEYNEG((const char *)&(sreq->m_key)) || sreq->m_fakeFirstIp) {
				continue;
			}

			// validate firstIP
			if (sreq->m_firstIp != g_spiderdb.getFirstIp(&sreq->m_key)) {
				printf("invalid firstip\n");
				continue;
			}

			// compare with tagdb
			auto it = hostFirstIpMap.find(sreq->m_key.n1);
			if (it != hostFirstIpMap.end()) {
				if (it->second != (uint32_t)sreq->m_firstIp) {
					printf("invalid firstip 2\n");
					continue;
				}
			}
		}

		startKey = *(key128_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if (startKey < *(key128_t *) list.getLastKey()) {
			break;
		}
	}

	cleanup();

	return 0;
}

