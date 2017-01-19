#include "UrlBlockList.h"
#include "ScopedLock.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

UrlBlockList g_urlBlockList;

static const char s_url_filename[] = "urlblocklist.txt";

UrlBlockList::UrlBlockList()
	: m_filename(s_url_filename)
	, m_urlRegexList(new regexlist_t)
	, m_lastModifiedTime(0) {
}

bool UrlBlockList::init() {
	log(LOG_INFO, "Initializing UrlBlockList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, 0, true)) {
		log(LOG_WARN, "UrlBlockList: Failed register callback.");
		return false;
	}

	return true;
}

void UrlBlockList::reload(int /*fd*/, void *state) {
	UrlBlockList *urlBlockList = static_cast<UrlBlockList*>(state);
	urlBlockList->load();
}

bool UrlBlockList::load() {
#if (__GNUC__ > 4) || (__GNUC_MINOR__>=9)
	logTrace(g_conf.m_logTraceUrlBlockList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceUrlBlockList, "Not modified");
		return true;
	}

	regexlist_ptr_t tmpUrlRegexList(new regexlist_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		tmpUrlRegexList->push_back(std::make_pair(line, std::regex(line)));
		logTrace(g_conf.m_logTraceUrlBlockList, "Adding regex '%s' to list", line.c_str());
	}

	swapUrlRegexList(tmpUrlRegexList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceUrlBlockList, "Loaded %s", m_filename);

#else
	logTrace(g_conf.m_logTraceUrlBlockList, "Not loading %s (gcc <4.9 STL is broken)", m_filename);
#endif
	return true;
}

bool UrlBlockList::isUrlBlocked(const char *url) {
	auto urlRegexList = getUrlRegexList();

	for (auto urlRegexPair : *urlRegexList) {
		std::cmatch match;
		if (std::regex_search(url, match, urlRegexPair.second)) {
			logTrace(g_conf.m_logTraceUrlBlockList, "Regex '%s' matched url '%s'", urlRegexPair.first.c_str(), url);
			return true;
		}
	}

	logTrace(g_conf.m_logTraceUrlBlockList, "No match found for url '%s'", url);
	return false;
}

regexlistconst_ptr_t UrlBlockList::getUrlRegexList() {
	return m_urlRegexList;
}

void UrlBlockList::swapUrlRegexList(regexlistconst_ptr_t urlRegexList) {
	std::atomic_store(&m_urlRegexList, urlRegexList);
}

