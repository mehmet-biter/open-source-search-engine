#include "DocDelete.h"
#include "Errno.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "XmlDoc.h"
#include "ScopedLock.h"
#include <fstream>
#include <sys/stat.h>

DocDelete g_docDelete;

static const char s_docdelete_filename[] = "docdelete.txt";
static const char s_docdelete_tmp_filename[] = "docdelete.txt.processing";
static const char s_docdelete_currentdocid_filename[] = "docdelete.txt.currentdocid";

static uint32_t s_pendingDocCount = 0;
static GbMutex s_pendingDocCountMtx;
static pthread_cond_t s_pendingDocCountCond = PTHREAD_COND_INITIALIZER;

static GbThreadQueue s_docDeleteFileThreadQueue;
static GbThreadQueue s_docDeleteDocThreadQueue;

struct DocDeleteItem {
	DocDeleteItem(const char *tmp_filename, int64_t currentDocId = -1)
		: m_tmp_filename(tmp_filename)
		, m_currentDocId(currentDocId) {
	}

	const char *m_tmp_filename;
	int64_t m_currentDocId;
};

DocDelete::DocDelete()
	: m_filename(s_docdelete_filename)
	, m_tmp_filename(s_docdelete_tmp_filename)
	, m_currentdocid_filename(s_docdelete_currentdocid_filename)
	, m_lastModifiedTime(0) {
}

bool DocDelete::init() {
	if (!s_docDeleteFileThreadQueue.initialize(processFile, "process-delfile")) {
		logError("Unable to initialize process file queue");
		return false;
	}

	if (!s_docDeleteDocThreadQueue.initialize(processDoc, "process-deldoc")) {
		logError("Unable to initialize process doc queue");
		return false;
	}

	if (!g_loop.registerSleepCallback(60000, this, &reload, "DocDelete::reload", 0)) {
		log(LOG_WARN, "DocDelete::init: Failed to register callback.");
		return false;
	}

	load();

	return true;
}

void DocDelete::reload(int /*fd*/, void *state) {
	DocDelete *docDelete = static_cast<DocDelete*>(state);
	docDelete->reload();
}

bool DocDelete::load() {
	struct stat st;

	int64_t currentDocId = -1;

	// we need to skip processed docid when processing pending file
	if (stat(m_tmp_filename, &st) == 0) {
		if (stat(m_currentdocid_filename, &st) == 0) {
			std::ifstream file(m_currentdocid_filename);
			std::string line;
			if (std::getline(file, line)) {
				currentDocId = strtoll(line.c_str(), NULL, 10);
			}
		}
	} else {
		if (stat(m_filename, &st) != 0) {
			// probably not found
			log(g_conf.m_logTraceDocDelete, "DocDelete::load: Unable to stat %s", m_filename);
			m_lastModifiedTime = 0;
			return false;
		}

		// we only process the file if we have 2 consecutive loads with the same m_time
		if (m_lastModifiedTime == 0 || m_lastModifiedTime != st.st_mtime) {
			m_lastModifiedTime = st.st_mtime;
			log(g_conf.m_logTraceDocDelete, "DocDelete::load: Modified time changed between load");
			return false;
		}

		// make sure file is not changed while we're processing it
		int rc = rename(m_filename, m_tmp_filename);
		if (rc == -1) {
			log(LOG_WARN, "Unable to rename '%s' to '%s' due to '%s'", m_filename, m_tmp_filename, mstrerror(errno));
			return false;
		}

		/// @todo initialize current docid file

	}

	s_docDeleteFileThreadQueue.addItem(new DocDeleteItem(m_tmp_filename, currentDocId));

	return true;
}

void DocDelete::reload() {
	struct stat st;

	// we're currently processing tmp file
	if (stat(m_tmp_filename, &st) == 0) {
		return;
	}

	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(g_conf.m_logTraceDocDelete, "DocDelete::load: Unable to stat %s", m_filename);
		m_lastModifiedTime = 0;
		return;
	}

	// we only process the file if we have 2 consecutive loads with the same m_time
	if (m_lastModifiedTime == 0 || m_lastModifiedTime != st.st_mtime) {
		m_lastModifiedTime = st.st_mtime;
		log(g_conf.m_logTraceDocDelete, "DocDelete::load: Modified time changed between load");
		return;
	}

	// make sure file is not changed while we're processing it
	int rc = rename(m_filename, m_tmp_filename);
	if (rc == -1) {
		log(LOG_WARN, "Unable to rename '%s' to '%s' due to '%s'", m_filename, m_tmp_filename, mstrerror(errno));
		return;
	}

	s_docDeleteFileThreadQueue.addItem(new DocDeleteItem(m_tmp_filename));
}


void DocDelete::indexedDoc(void *state) {
	// reprocess xmldoc
	s_docDeleteDocThreadQueue.addItem(state);
}

static void waitPendingCount(int max) {
	ScopedLock sl(s_pendingDocCountMtx);
	while (s_pendingDocCount > max) {
		pthread_cond_wait(&s_pendingDocCountCond, &s_pendingDocCountMtx.mtx);
	}
}

static void incrementPendingCount() {
	ScopedLock sl(s_pendingDocCountMtx);
	++s_pendingDocCount;
}

static void decrementPendingCount() {
	ScopedLock sl(s_pendingDocCountMtx);
	--s_pendingDocCount;
	pthread_cond_signal(&s_pendingDocCountCond);
}

void DocDelete::processFile(void *item) {
	DocDeleteItem *docDeleteItem = static_cast<DocDeleteItem*>(item);

	log(LOG_INFO, "Processing %s", docDeleteItem->m_tmp_filename);

	// start processing file
	bool foundCurrentDocId = (docDeleteItem->m_currentDocId == -1);
	std::ifstream file(docDeleteItem->m_tmp_filename);
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

		if (foundCurrentDocId || docDeleteItem->m_currentDocId == docId) {
			foundCurrentDocId = true;
			logTrace(g_conf.m_logTraceDocDelete, "Processing docId=%" PRId64, docId);

			XmlDoc *xmlDoc = new XmlDoc();
			xmlDoc->set3(docId, "main", 0);
			xmlDoc->m_deleteFromIndex = true;
			xmlDoc->setCallback(xmlDoc, indexedDoc);
			s_docDeleteDocThreadQueue.addItem(xmlDoc);

			incrementPendingCount();
		}

		waitPendingCount(10);
	}

	waitPendingCount(0);

	log(LOG_INFO, "Processed %s", docDeleteItem->m_tmp_filename);

	unlink(docDeleteItem->m_tmp_filename);

	delete docDeleteItem;
}

void DocDelete::processDoc(void *item) {
	XmlDoc *xmlDoc = static_cast<XmlDoc*>(item);
	if (!xmlDoc->m_indexedDoc && !xmlDoc->indexDoc()) {
		// blocked
		return;
	}

	// done
	delete xmlDoc;

	decrementPendingCount();
}

