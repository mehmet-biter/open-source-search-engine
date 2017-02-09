#include "UrlBlockList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>
#include <features.h>

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
#if defined(__GNUC__) || defined(__clang__)
#if __GNUC_PREREQ(5, 0) || defined(__clang__)
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

		tmpUrlRegexList->push_back(std::make_pair(line, GbRegex(line.c_str(), PCRE_NO_AUTO_CAPTURE, PCRE_STUDY_JIT_COMPILE)));
		logTrace(g_conf.m_logTraceUrlBlockList, "Adding regex '%s' to list", line.c_str());
	}

	swapUrlRegexList(tmpUrlRegexList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceUrlBlockList, "Loaded %s", m_filename);
#else
#warning "Url block feature is disabled"
	logTrace(g_conf.m_logTraceUrlBlockList, "Not loading %s (g++ <4.9 regex STL is broken; std::atomic_store is not supported)", m_filename);
#endif
#endif
	return true;
}

bool UrlBlockList::isUrlBlocked(const char *url) {
#if defined(__GNUC__) || defined(__clang__)
#if __GNUC_PREREQ(5, 0) || defined(__clang__)
	auto urlRegexList = getUrlRegexList();

	for (auto const &urlRegexPair : *urlRegexList) {
		if (urlRegexPair.second.match(url)) {
			logTrace(g_conf.m_logTraceUrlBlockList, "Regex '%s' matched url '%s'", urlRegexPair.first.c_str(), url);
			return true;
		}
	}

	logTrace(g_conf.m_logTraceUrlBlockList, "No match found for url '%s'", url);
#endif
#endif
	return false;
}

regexlistconst_ptr_t UrlBlockList::getUrlRegexList() {
	return m_urlRegexList;
}

void UrlBlockList::swapUrlRegexList(regexlistconst_ptr_t urlRegexList) {
#if defined(__GNUC__) || defined(__clang__)
#if __GNUC_PREREQ(5, 0) || defined(__clang__)
	std::atomic_store(&m_urlRegexList, urlRegexList);
#endif
#endif
}

