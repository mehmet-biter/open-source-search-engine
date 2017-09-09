#include "DocDelete.h"
#include "Errno.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "XmlDoc.h"
#include "ScopedLock.h"
#include "Collectiondb.h"
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <atomic>

static const char *s_filename = "docdelete.txt";
static const char *s_tmp_filename = "docdelete.txt.processing";
static const char *s_lastdocid_filename = "docdelete.txt.docid";

static std::vector<int64_t> s_pendingDocIds;
static GbMutex s_pendingDocIdsMtx;
static pthread_cond_t s_pendingDocIdsCond = PTHREAD_COND_INITIALIZER;


time_t s_lastModifiedTime = 0;

static std::atomic<bool> s_stop(false);
static GbThreadQueue s_docDeleteFileThreadQueue;
static GbThreadQueue s_docDeleteDocThreadQueue;

struct DocDeleteFileItem {
	DocDeleteFileItem(int64_t lastDocId = -1)
		: m_lastDocId(lastDocId)
		, m_lastDocIdFile(s_lastdocid_filename) {
	}

	int64_t m_lastDocId;
	std::ofstream m_lastDocIdFile;
};

struct DocDeleteDocItem {
	DocDeleteDocItem(std::ofstream &lastDocIdFile)
		: m_lastDocIdFile(lastDocIdFile)
		, m_xmlDoc(new XmlDoc()) {
	}

	std::ofstream &m_lastDocIdFile;
	XmlDoc *m_xmlDoc;
};

static bool docDeleteDisabled() {
	CollectionRec *collRec = g_collectiondb.getRec("main");
	if (g_conf.m_spideringEnabled) {
		if (collRec) {
			if (collRec->m_spideringEnabled) {
				return true;
			} else {
				return g_hostdb.hasDeadHostCached();
			}
		}

		return true;
	}

	return g_hostdb.hasDeadHostCached();
}

bool DocDelete::initialize() {
	if (!s_docDeleteFileThreadQueue.initialize(processFile, "process-delfile")) {
		logError("Unable to initialize process file queue");
		return false;
	}

	if (!s_docDeleteDocThreadQueue.initialize(processDoc, "process-deldoc")) {
		logError("Unable to initialize process doc queue");
		return false;
	}

	if (!g_loop.registerSleepCallback(60000, NULL, &reload, "DocDelete::reload", 0)) {
		log(LOG_WARN, "DocDelete::init: Failed to register callback.");
		return false;
	}

	reload(0, NULL);

	return true;
}

void DocDelete::finalize() {
	s_stop = true;
	pthread_cond_broadcast(&s_pendingDocIdsCond);

	s_docDeleteFileThreadQueue.finalize();
	s_docDeleteDocThreadQueue.finalize();
}

void DocDelete::reload(int /*fd*/, void */*state*/) {
	if (!s_docDeleteFileThreadQueue.isEmpty()) {
		// we're currently processing tmp file
		return;
	}

	struct stat st;
	int64_t lastDocId = -1;
	if (stat(s_tmp_filename, &st) == 0) {
		if (docDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", s_tmp_filename);
			return;
		}

		if (stat(s_lastdocid_filename, &st) == 0) {
			std::ifstream file(s_lastdocid_filename);
			std::string line;
			if (std::getline(file, line)) {
				lastDocId = strtoll(line.c_str(), NULL, 10);
			}
		}
	} else {
		if (stat(s_filename, &st) != 0) {
			// probably not found
			logTrace(g_conf.m_logTraceDocDelete, "DocDelete::load: Unable to stat %s", s_filename);
			s_lastModifiedTime = 0;
			return;
		}

		// we only process the file if we have 2 consecutive loads with the same m_time
		if (s_lastModifiedTime == 0 || s_lastModifiedTime != st.st_mtime) {
			s_lastModifiedTime = st.st_mtime;
			logTrace(g_conf.m_logTraceDocDelete, "DocDelete::load: Modified time changed between load");
			return;
		}

		// only start processing if spidering is disabled
		if (docDeleteDisabled()) {
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

	s_docDeleteFileThreadQueue.addItem(new DocDeleteFileItem(lastDocId));
}

static void waitPendingDocCount(unsigned maxCount) {
	ScopedLock sl(s_pendingDocIdsMtx);
	while (!s_stop && s_pendingDocIds.size() > maxCount) {
		pthread_cond_wait(&s_pendingDocIdsCond, &s_pendingDocIdsMtx.mtx);
	}
}

static void addPendingDoc(int64_t docId) {
	logTrace(g_conf.m_logTraceDocDelete, "Adding %" PRId64, docId);

	ScopedLock sl(s_pendingDocIdsMtx);
	s_pendingDocIds.push_back(docId);
}

static void removePendingDoc(std::ofstream &lastDocIdFile, int64_t docId) {
	logTrace(g_conf.m_logTraceDocDelete, "Removing %" PRId64, docId);

	ScopedLock sl(s_pendingDocIdsMtx);
	auto it = std::find(s_pendingDocIds.begin(), s_pendingDocIds.end(), docId);

	// docid must be there
	if (it == s_pendingDocIds.end()) {
		gbshutdownLogicError();
	}

	if (it == s_pendingDocIds.begin()) {
		lastDocIdFile.seekp(0);
		lastDocIdFile << *it << std::endl;
	}

	s_pendingDocIds.erase(it);
	pthread_cond_signal(&s_pendingDocIdsCond);
}

void DocDelete::processFile(void *item) {
	DocDeleteFileItem *fileItem = static_cast<DocDeleteFileItem*>(item);

	log(LOG_INFO, "Processing %s", s_tmp_filename);

	// start processing file
	std::ifstream file(s_tmp_filename);
	std::ofstream lastDocIdFile(s_lastdocid_filename, std::ofstream::out|std::ofstream::trunc);

	bool isInterrupted = false;
	bool foundLastDocId = (fileItem->m_lastDocId == -1);
	std::string line;
	while (std::getline(file, line)) {
		// ignore empty lines
		if (line.length() == 0) {
			continue;
		}

		char *pend = NULL;
		int64_t docId = strtoll(line.c_str(), &pend, 10);
		if (*pend != '|') {
			log(LOG_INFO, "Skipping line %s due to invalid format", line.c_str());
			continue;
		}

		if (foundLastDocId) {
			logTrace(g_conf.m_logTraceDocDelete, "Processing docId=%" PRId64, docId);

			DocDeleteDocItem *docItem = new DocDeleteDocItem(lastDocIdFile);
			docItem->m_xmlDoc->set3(docId, "main", 0);
			docItem->m_xmlDoc->m_deleteFromIndex = true;
			docItem->m_xmlDoc->setCallback(docItem, processedDoc);

			addPendingDoc(docId);

			s_docDeleteDocThreadQueue.addItem(docItem);

			waitPendingDocCount(10);
		} else if (fileItem->m_lastDocId == docId) {
			foundLastDocId = true;
		}

		// stop processing when we're shutting down or spidering is enabled
		if (s_stop || docDeleteDisabled()) {
			isInterrupted = true;
			break;
		}
	}

	waitPendingDocCount(0);

	if (isInterrupted) {
		log(LOG_INFO, "Interrupted processing of %s", s_tmp_filename);
		delete fileItem;
		return;
	}

	log(LOG_INFO, "Processed %s", s_tmp_filename);

	// delete files
	unlink(s_tmp_filename);
	unlink(s_lastdocid_filename);

	delete fileItem;
}

void DocDelete::processDoc(void *item) {
	DocDeleteDocItem *docItem = static_cast<DocDeleteDocItem*>(item);
	XmlDoc *xmlDoc = docItem->m_xmlDoc;

	// done
	if (xmlDoc->m_indexedDoc || xmlDoc->indexDoc()) {
		removePendingDoc(docItem->m_lastDocIdFile, xmlDoc->m_docId);

		delete xmlDoc;
		delete docItem;
	}
}

void DocDelete::processedDoc(void *state) {
	// reprocess xmldoc
	s_docDeleteDocThreadQueue.addItem(state);
}

