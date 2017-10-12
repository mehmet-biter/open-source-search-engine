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

static const char *s_docdelete_filename = "docdelete.txt";
static const char *s_docdelete_tmp_filename = "docdelete.txt.processing";
static const char *s_docdelete_lastdocid_filename = "docdelete.txt.lastpos";

static time_t s_docdelete_lastModifiedTime = 0;

static const char *s_docdeleteurl_filename = "docdeleteurl.txt";
static const char *s_docdeleteurl_tmp_filename = "docdeleteurl.txt.processing";
static const char *s_docdeleteurl_lastpos_filename = "docdeleteurl.txt.lastpos";

static time_t s_docdeleteurl_lastModifiedTime = 0;

static std::vector<struct DocDeleteDocItem*> s_pendingDocItems;
static GbMutex s_pendingDocItemsMtx;
static pthread_cond_t s_pendingDocItemsCond = PTHREAD_COND_INITIALIZER;

static std::atomic<bool> s_stop(false);

static GbThreadQueue s_docDeleteFileThreadQueue;
static GbThreadQueue s_docDeleteDocThreadQueue;

struct DocDeleteFileItem {
	DocDeleteFileItem(const char *tmpFilename, const char *lastPosFilename, const std::string &lastPos, bool isDocDeleteUrl)
		: m_tmpFilename(tmpFilename)
		, m_lastPosFilename(lastPosFilename)
		, m_lastPos(lastPos)
		, m_isDocDeleteUrl(isDocDeleteUrl) {
	}

	const char *m_tmpFilename;
	const char *m_lastPosFilename;
	std::string m_lastPos;
	bool m_isDocDeleteUrl;
};

struct DocDeleteDocItem {
	DocDeleteDocItem(const std::string &key, const char* lastPosFilename, int64_t lastPos)
		: m_key(key)
		, m_lastPosFilename(lastPosFilename)
		, m_lastPos(lastPos)
		, m_xmlDoc(new XmlDoc()) {
	}

	std::string m_key;
	const char *m_lastPosFilename;
	int64_t m_lastPos;
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
	pthread_cond_broadcast(&s_pendingDocItemsCond);

	s_docDeleteFileThreadQueue.finalize();
	s_docDeleteDocThreadQueue.finalize();
}

static void reloadDocDelete(bool isDocDeleteUrl) {
	if (!s_docDeleteFileThreadQueue.isEmpty()) {
		// we're currently processing tmp file
		return;
	}

	const char *filename = nullptr;
	const char *tmpFilename = nullptr;
	const char *lastPosFilename = nullptr;
	time_t *lastModifiedTime = nullptr;

	if (isDocDeleteUrl) {
		filename = s_docdeleteurl_filename;
		tmpFilename = s_docdeleteurl_tmp_filename;
		lastPosFilename = s_docdeleteurl_lastpos_filename;
		lastModifiedTime = &s_docdeleteurl_lastModifiedTime;
	} else {
		filename = s_docdelete_filename;
		tmpFilename = s_docdelete_tmp_filename;
		lastPosFilename = s_docdelete_lastdocid_filename;
		lastModifiedTime = &s_docdelete_lastModifiedTime;
	}

	struct stat st;
	std::string lastPos;
	if (stat(tmpFilename, &st) == 0) {
		if (docDeleteDisabled()) {
			log(LOG_INFO, "Processing of %s is disabled", tmpFilename);
			return;
		}

		if (stat(lastPosFilename, &st) == 0) {
			std::ifstream file(lastPosFilename);
			std::string line;
			if (std::getline(file, line)) {
				lastPos = line;
			}
		}
	} else {
		if (stat(filename, &st) != 0) {
			// probably not found
			logTrace(g_conf.m_logTraceDocDelete, "DocDelete::load: Unable to stat %s", filename);
			*lastModifiedTime = 0;
			return;
		}

		// we only process the file if we have 2 consecutive loads with the same m_time
		if (*lastModifiedTime == 0 || *lastModifiedTime != st.st_mtime) {
			*lastModifiedTime = st.st_mtime;
			logTrace(g_conf.m_logTraceDocDelete, "DocDelete::load: Modified time changed between load");
			return;
		}

		// only start processing if spidering is disabled
		if (docDeleteDisabled()) {
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

	s_docDeleteFileThreadQueue.addItem(new DocDeleteFileItem(tmpFilename, lastPosFilename, lastPos, isDocDeleteUrl));
}

void DocDelete::reload(int /*fd*/, void */*state*/) {
	// docdelete.txt
	reloadDocDelete(false);

	// docdeleteurl.txt
	reloadDocDelete(true);
}

static void waitPendingDocCount(unsigned maxCount) {
	ScopedLock sl(s_pendingDocItemsMtx);
	while (!s_stop && s_pendingDocItems.size() > maxCount) {
		pthread_cond_wait(&s_pendingDocItemsCond, &s_pendingDocItemsMtx.mtx);
	}
}

static void addPendingDoc(DocDeleteDocItem *item) {
	logTrace(g_conf.m_logTraceDocDelete, "Adding %s", item->m_key.c_str());

	ScopedLock sl(s_pendingDocItemsMtx);
	s_pendingDocItems.push_back(item);
}

static void removePendingDoc(DocDeleteDocItem *docItem) {
	logTrace(g_conf.m_logTraceDocDelete, "Removing %s", docItem->m_key.c_str());

	ScopedLock sl(s_pendingDocItemsMtx);
	auto it = std::find(s_pendingDocItems.begin(), s_pendingDocItems.end(), docItem);

	// docid must be there
	if (it == s_pendingDocItems.end()) {
		gbshutdownLogicError();
	}

	if (it == s_pendingDocItems.begin()) {
		std::ofstream lastPosFile(docItem->m_lastPosFilename, std::ofstream::out|std::ofstream::trunc);
		lastPosFile << docItem->m_lastPos << "|" << docItem->m_key << std::endl;
	}

	s_pendingDocItems.erase(it);
	pthread_cond_signal(&s_pendingDocItemsCond);
}

void DocDelete::processFile(void *item) {
	DocDeleteFileItem *fileItem = static_cast<DocDeleteFileItem*>(item);

	log(LOG_INFO, "Processing %s", fileItem->m_tmpFilename);

	// start processing file
	std::ifstream file(fileItem->m_tmpFilename);

	bool isInterrupted = false;

	bool foundLastPos = fileItem->m_lastPos.empty();

	int64_t lastPos = 0;
	std::string lastPosKey;
	if (!fileItem->m_lastPos.empty()) {
		lastPos = strtoll(fileItem->m_lastPos.c_str(), NULL, 10);
		lastPosKey = fileItem->m_lastPos.substr(fileItem->m_lastPos.find('|') + 1);

		if (lastPosKey.empty()) {
			lastPos = 0;
		}
	}

	// skip to last position
	if (lastPos) {
		file.seekg(lastPos);
	}

	int64_t currentFilePos = file.tellg();
	std::string line;
	while (std::getline(file, line)) {
		// ignore empty lines
		if (line.length() == 0) {
			continue;
		}

		std::string key = fileItem->m_isDocDeleteUrl ? line : line.substr(0, line.find('|'));

		if (foundLastPos) {
			logTrace(g_conf.m_logTraceDocDelete, "Processing key='%s'", key.c_str());
			DocDeleteDocItem *docItem = new DocDeleteDocItem(key, fileItem->m_lastPosFilename, currentFilePos);

			if (fileItem->m_isDocDeleteUrl) {
				SpiderRequest sreq;
				sreq.setFromAddUrl(key.c_str());
				sreq.m_isAddUrl = 0;

				docItem->m_xmlDoc->set4(&sreq, NULL, "main", NULL, 0);
			} else {
				int64_t docId = strtoll(line.c_str(), NULL, 10);

				docItem->m_xmlDoc->set3(docId, "main", 0);
			}

			docItem->m_xmlDoc->m_deleteFromIndex = true;
			docItem->m_xmlDoc->m_blockedDoc = false;
			docItem->m_xmlDoc->m_blockedDocValid = true;
			docItem->m_xmlDoc->setCallback(docItem, processedDoc);

			addPendingDoc(docItem);
			s_docDeleteDocThreadQueue.addItem(docItem);

			waitPendingDocCount(10);
		} else if (lastPosKey.compare(key) == 0) {
			foundLastPos = true;
		}

		// stop processing when we're shutting down or spidering is enabled
		if (s_stop || docDeleteDisabled()) {
			isInterrupted = true;
			break;
		}

		currentFilePos = file.tellg();
	}

	waitPendingDocCount(0);

	if (isInterrupted) {
		log(LOG_INFO, "Interrupted processing of %s", fileItem->m_tmpFilename);
		delete fileItem;
		return;
	}

	log(LOG_INFO, "Processed %s", fileItem->m_tmpFilename);

	// delete files
	unlink(fileItem->m_tmpFilename);
	unlink(fileItem->m_lastPosFilename);

	delete fileItem;
}

void DocDelete::processDoc(void *item) {
	DocDeleteDocItem *docItem = static_cast<DocDeleteDocItem*>(item);
	XmlDoc *xmlDoc = docItem->m_xmlDoc;

	// done
	if (xmlDoc->m_indexedDoc || xmlDoc->indexDoc()) {
		removePendingDoc(docItem);

		delete xmlDoc;
		delete docItem;
	}
}

void DocDelete::processedDoc(void *state) {
	// reprocess xmldoc
	s_docDeleteDocThreadQueue.addItem(state);
}

