#include "SpiderdbHostDelete.h"
#include "Collectiondb.h"
#include "Conf.h"
#include "GbThreadQueue.h"
#include "UrlMatchHostList.h"
#include "Spider.h"
#include "Process.h"
#include "ScopedLock.h"
#include <fstream>
#include <sys/stat.h>
#include <ctime>

static const char *s_spiderdbhost_filename = "spiderdbhostdelete.txt";
static const char *s_spiderdbhost_tmp_filename = "spiderdbhostdelete.txt.processing";

static time_t s_spiderdbhost_lastModifiedTime = 0;

static const char *s_spiderdburl_filename = "spiderdburldelete.txt";
static const char *s_spiderdburl_tmp_filename = "spiderdburldelete.txt.processing";

static time_t s_spiderdburl_lastModifiedTime = 0;

static GbMutex s_sleepMtx;
static pthread_cond_t s_sleepCond = PTHREAD_COND_INITIALIZER;
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
	{
		ScopedLock sl(s_sleepMtx);
		s_stop = true;
		pthread_cond_broadcast(&s_sleepCond);
	}

	s_fileThreadQueue.finalize();
}

struct FileItem {
	FileItem(const char *tmpFilename, bool matchHost, bool resume)
		: m_tmpFilename(tmpFilename)
		, m_matchHost(matchHost)
		, m_resume(resume) {
	}

	const char *m_tmpFilename;
	bool m_matchHost;
	bool m_resume;
};

static void reloadSpiderdbHostDelete(bool matchHost) {
	if (!s_fileThreadQueue.isEmpty()) {
		// we're currently processing tmp file
		return;
	}

	const char *filename = nullptr;
	const char *tmpFilename = nullptr;
	time_t *lastModifiedTime = nullptr;

	if (matchHost) {
		filename = s_spiderdbhost_filename;
		tmpFilename = s_spiderdbhost_tmp_filename;
		lastModifiedTime = &s_spiderdbhost_lastModifiedTime;
	} else {
		filename = s_spiderdburl_filename;
		tmpFilename = s_spiderdburl_tmp_filename;
		lastModifiedTime = &s_spiderdburl_lastModifiedTime;
	}

	bool resume = false;
	struct stat st;
	if (stat(tmpFilename, &st) == 0) {
		if (spiderdbHostDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", tmpFilename);
			return;
		}

		resume = true;
	} else {
		if (stat(filename, &st) != 0) {
			// probably not found
			logTrace(g_conf.m_logTraceSpiderdbHostDelete, "SpiderdbHostDelete::load: Unable to stat %s", filename);
			*lastModifiedTime = 0;
			return;
		}

		// we only process the file if we have 2 consecutive loads with the same m_time
		if (*lastModifiedTime == 0 || *lastModifiedTime != st.st_mtime) {
			*lastModifiedTime = st.st_mtime;
			logTrace(g_conf.m_logTraceSpiderdbHostDelete, "SpiderdbHostDelete::load: Modified time changed between load");
			return;
		}

		// only start processing if spidering is disabled
		if (spiderdbHostDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", filename);
			return;
		}

		// make sure file is not changed while we're processing it
		int rc = rename(filename, tmpFilename);
		if (rc == -1) {
			log(LOG_WARN, "Unable to rename '%s' to '%s' due to '%s'", filename, tmpFilename, mstrerror(errno));
			return;
		}
	}

	s_fileThreadQueue.addItem(new FileItem(tmpFilename, matchHost, resume));
}

void SpiderdbHostDelete::reload(int /*fd*/, void */*state*/) {
	// spiderdburldelete.txt
	reloadSpiderdbHostDelete(false);

	// spiderdbhostdelete.txt
	reloadSpiderdbHostDelete(true);
}

void SpiderdbHostDelete::processFile(void *item) {
	FileItem *fileItem = static_cast<FileItem*>(item);

	log(LOG_INFO, "Processing %s", fileItem->m_tmpFilename);

	g_urlHostBlackList.load(fileItem->m_tmpFilename, fileItem->m_matchHost);

	CollectionRec *collRec = g_collectiondb.getRec("main");
	if (!collRec) {
		gbshutdownLogicError();
	}
	RdbBase *base = collRec->getBase(RDB_SPIDERDB);
	Rdb *rdb = g_spiderdb.getRdb();

	if (!fileItem->m_resume) {
		// dump tree
		rdb->submitRdbDumpJob(true);

		{
			ScopedLock sl(s_sleepMtx);
			while (!s_stop && rdb->hasPendingRdbDumpJob()) {
				timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				ts.tv_sec += 1;

				pthread_cond_timedwait(&s_sleepCond, &s_sleepMtx.mtx, &ts);
			}

			if (s_stop) {
				delete fileItem;
				return;
			}
		}
	}

	// tight merge (only force merge all when not resuming)
	if (!base->attemptMerge(0, !fileItem->m_resume)) {
		// unable to start merge
		g_urlHostBlackList.unload();
		delete fileItem;
		return;
	}

	{
		ScopedLock sl(s_sleepMtx);
		while (!s_stop && rdb->isMerging()) {
			timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			ts.tv_sec += 60;

			pthread_cond_timedwait(&s_sleepCond, &s_sleepMtx.mtx, &ts);
		}

		if (s_stop) {
			delete fileItem;
			return;
		}
	}

	log(LOG_INFO, "Processed %s", fileItem->m_tmpFilename);

	g_urlHostBlackList.unload();

	// delete files
	unlink(fileItem->m_tmpFilename);

	delete fileItem;
}