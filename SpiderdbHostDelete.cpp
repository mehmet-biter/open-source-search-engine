#include "SpiderdbHostDelete.h"
#include "Collectiondb.h"
#include "Conf.h"
#include "GbThreadQueue.h"
#include "UrlMatchHostList.h"
#include "Spider.h"
#include "Process.h"
#include <fstream>
#include <sys/stat.h>
#include <ctime>

static const char *s_filename = "spiderdbhostdelete.txt";
static const char *s_tmp_filename = "spiderdbhostdelete.txt.processing";

static time_t s_lastModifiedTime = 0;

static std::atomic<bool> s_stop(false);

static GbThreadQueue s_fileThreadQueue;

static bool spiderdbHostDeleteDisabled() {
	if (g_process.isRdbDumping() || g_process.isRdbMerging()) {
		return true;
	}

	if (g_conf.m_spideringEnabled) {
		CollectionRec *collRec = g_collectiondb.getRec("main");
		if (collRec) {
			return collRec->m_spideringEnabled;
		}

		return true;
	}

	return false;
}

bool SpiderdbHostDelete::initialize() {
	if (!s_fileThreadQueue.initialize(processFile, "process-hostdel")) {
		logError("Unable to initialize process file queue");
		return false;
	}

	if (!g_loop.registerSleepCallback(60000, NULL, &reload, "SpiderdbHostDelete::reload", 0)) {
		log(LOG_WARN, "SpiderdbHostDelete::init: Failed to register callback.");
		return false;
	}

	reload(0, NULL);

	return true;
}

void SpiderdbHostDelete::finalize() {
	s_stop = true;

	s_fileThreadQueue.finalize();
}

struct FileItem {
	FileItem(bool resume)
		: m_resume(resume) {
	}

	bool m_resume;
};
void SpiderdbHostDelete::reload(int /*fd*/, void */*state*/) {
	if (!s_fileThreadQueue.isEmpty()) {
		// we're currently processing tmp file
		return;
	}

	bool resume = false;
	struct stat st;
	if (stat(s_tmp_filename, &st) == 0) {
		if (spiderdbHostDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", s_tmp_filename);
			return;
		}

		resume = true;
	} else {
		if (stat(s_filename, &st) != 0) {
			// probably not found
			logTrace(g_conf.m_logTraceSpiderdbHostDelete, "SpiderdbHostDelete::load: Unable to stat %s", s_filename);
			s_lastModifiedTime = 0;
			return;
		}

		// we only process the file if we have 2 consecutive loads with the same m_time
		if (s_lastModifiedTime == 0 || s_lastModifiedTime != st.st_mtime) {
			s_lastModifiedTime = st.st_mtime;
			logTrace(g_conf.m_logTraceSpiderdbHostDelete, "SpiderdbHostDelete::load: Modified time changed between load");
			return;
		}

		// only start processing if spidering is disabled
		if (spiderdbHostDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", s_filename);
			return;
		}

		// make sure file is not changed while we're processing it
		int rc = rename(s_filename, s_tmp_filename);
		if (rc == -1) {
			log(LOG_WARN, "Unable to rename '%s' to '%s' due to '%s'", s_filename, s_tmp_filename, mstrerror(errno));
			return;
		}
	}

	s_fileThreadQueue.addItem(new FileItem(resume));
}

void SpiderdbHostDelete::processFile(void *item) {
	FileItem *fileItem = static_cast<FileItem*>(item);
	bool resume = fileItem->m_resume;
	delete fileItem;

	log(LOG_INFO, "Processing %s", s_tmp_filename);

	g_urlHostBlackList.load(s_tmp_filename);

	CollectionRec *collRec = g_collectiondb.getRec("main");
	if (!collRec) {
		gbshutdownLogicError();
	}
	RdbBase *base = collRec->getBase(RDB_SPIDERDB);
	Rdb *rdb = g_spiderdb.getRdb();

	if (!resume) {
		// dump tree
		rdb->submitRdbDumpJob(true);
		while (!s_stop && rdb->hasPendingRdbDumpJob()) {
			sleep(1);
		}

		if (s_stop) {
			return;
		}
	}

	// tight merge
	if (!base->attemptMerge(0, true)) {
		// unable to start merge
		return;
	}

	while (!s_stop && rdb->isMerging()) {
		sleep(60);
	}

	if (s_stop) {
		return;
	}

	log(LOG_INFO, "Processed %s", s_tmp_filename);

	g_urlHostBlackList.unload();

	// delete files
	unlink(s_tmp_filename);
}