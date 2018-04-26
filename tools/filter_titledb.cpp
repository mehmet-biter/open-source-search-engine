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
#include "utf8_convert.h"
#include "Domains.h"
#include "Version.h"
#include "GbUtil.h"
#include "ip.h"
#include <libgen.h>
#include <algorithm>
#include <limits.h>
#include <getopt.h>
#include <zlib.h>
#include <cassert>

static void print_usage(const char *argv0) {
	fprintf(stdout, "Usage: %s [-h] PATH\n", argv0);
	fprintf(stdout, "Filter titlerec to custom archive files\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "  -h, --help     display this help and exit\n");
	fprintf(stdout, "  -t, --tlds     list of comma separated tlds to filter (eg: com,dk)\n");
	fprintf(stdout, "  -l, --langs    list of comma separated languages to filter (eg: en,da)\n");
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

int writeTitleRec(FILE *file, z_stream &strm, const XmlDoc &xmlDoc) {
	char ipbuf[16];
	char timebuf[32];

	// we can't put the original content type cause we'll try to convert it again
	const char *contentType;
	switch (xmlDoc.m_contentType) {
		case CT_TEXT:
			contentType = "text/plain";
			break;
		case CT_XML:
			contentType = "application/xml";
			break;
		default:
			contentType = "text/html";
			break;
	}

	// we can't pass in the original charset because we'll try to convert it again when importing

	SafeBuf sb;

	// header
	sb.safePrintf("FXARC/1.0\r\n");
	sb.safePrintf("Target-URI: %.*s\r\n", xmlDoc.size_firstUrl - 1, xmlDoc.ptr_firstUrl);
	sb.safePrintf("First-Indexed: %s\r\n", formatTime(xmlDoc.m_firstIndexedDate, timebuf));
	sb.safePrintf("Last-Indexed: %s\r\n", formatTime(xmlDoc.m_spideredTime, timebuf));
	sb.safePrintf("IP-Address: %s\r\n", iptoa(xmlDoc.m_ip, ipbuf));
	sb.safePrintf("Content-Language: %s\r\n", getLanguageAbbr(xmlDoc.m_langId));
	sb.safePrintf("Content-Type: %s\r\n", contentType);

	int32_t contentLen = xmlDoc.size_utf8Content ? xmlDoc.size_utf8Content - 1 : 0;
	sb.safePrintf("Content-Length: %d\r\n", contentLen);
	sb.safePrintf("\r\n");

	/// @todo ALC we can potentially compress content here
	// content
	sb.safePrintf("%.*s", contentLen, xmlDoc.ptr_utf8Content);

	// end record
	sb.safePrintf("\r\n");
	sb.safePrintf("\r\n");

	strm.avail_in = sb.length();
	strm.next_in = (unsigned char*)sb.getBufStart();

	char *buffer = (char*)mmalloc(sb.length() + 30, "CompressBuf");
	uint32_t bufferLen = sb.length() + 30;

	/* run deflate() on input until output buffer not full, finish
	   compression if all of source has been read in */
	do {
		strm.avail_out = bufferLen;
		strm.next_out = (unsigned char*)buffer;

		int ret = deflate(&strm, Z_SYNC_FLUSH);    /* no bad return value */
		assert(ret != Z_STREAM_ERROR);  /* state not clobbered */

		unsigned have = bufferLen - strm.avail_out;
		if (fwrite(buffer, 1, have, file) != have || ferror(file)) {
			(void)deflateEnd(&strm);
			return Z_ERRNO;
		}
	} while (strm.avail_out == 0);

	mfree(buffer, bufferLen, "CompressBuf");

	return 0;
}

int main(int argc, char **argv) {
	static option options[] = {
		{"help", no_argument, nullptr, 'h'},
		{"version", no_argument, nullptr, 'g'},
		{"tlds", optional_argument, nullptr, 't'},
		{"langs", optional_argument, nullptr, 'l'},
		{nullptr, 0, nullptr, 0}
	};

	std::vector<std::string> tlds;
	std::vector<std::string> langs;

	int c;
	int index;
	while((c = getopt_long(argc, argv, "hvt::l::", options, &index)) != -1) {
		switch (c) {
			case 0:
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			case 'v':
				printVersion(basename(argv[0]));
				return 0;
			case 't':
				tlds = split(optarg, ',');
				break;
			case 'l':
				langs = split(optarg, ',');
				break;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	const char *configured_path = argv[optind];
	if (configured_path == nullptr) {
		print_usage(argv[0]);
		return 1;
	}

	g_log.m_disabled = true;

	// initialize library
	g_mem.init();
	hashinit();

	// current dir
	char path[PATH_MAX];
	realpath(configured_path, path);
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

	FILE *file = fopen("titledb_filtered.fxarc.zz", "w+b");

	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	int retVal = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
	assert(retVal == Z_OK);

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

			// extract the url
			Url *url = xmlDoc.getFirstUrl();
			std::string tld(url->getTLD(), url->getTLDLen());
			std::string lang(getLanguageAbbr(xmlDoc.m_langId));

			const auto it_tld = std::find(tlds.begin(), tlds.end(), tld);
			const auto it_lang = std::find(langs.begin(), langs.end(), lang);

			if (it_tld != tlds.end() || it_lang != langs.end()) {
				if (writeTitleRec(file, strm, xmlDoc) != 0) {
					fprintf(stderr, "Error writing data\n");
					exit(1);
				}
			}
		}

		startKey = *(key96_t *)list.getLastKey();
		startKey++;

		// watch out for wrap around
		if ( startKey < *(key96_t *)list.getLastKey() ) {
			break;
		}
	}

	char buffer[256];

	strm.avail_in = 0;
	strm.next_in = nullptr;
	strm.avail_out = 256;
	strm.next_out = (unsigned char*)buffer;
	int ret = deflate(&strm, Z_FINISH);
	assert(ret != Z_STREAM_ERROR);

	unsigned have = 256 - strm.avail_out;
	if (fwrite(buffer, 1, have, file) != have || ferror(file)) {
		(void)deflateEnd(&strm);
		fprintf(stderr, "Error writing end data\n");
		exit(1);
	}

	(void)deflateEnd(&strm);

	cleanup();

	return 0;
}
