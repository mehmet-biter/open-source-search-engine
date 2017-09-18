#include "UrlMatchHostList.h"
#include "Log.h"
#include "Conf.h"
#include "Url.h"
#include <fstream>
#include <sys/stat.h>


UrlMatchHostList g_urlHostBlackList;

UrlMatchHostList::UrlMatchHostList()
	: m_filename()
	, m_urlmatchhostlist(new urlmatchhostlist_t) {
}

bool UrlMatchHostList::load(const char *filename) {
	m_filename = filename;

	logTrace(g_conf.m_logTraceUrlMatchHostList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "UrlMatchHostList::load: Unable to stat %s", m_filename);
		return false;
	}

	urlmatchhostlist_ptr_t tmpUrlMatchHostList(new urlmatchhostlist_t);

	std::ifstream file(m_filename);
	std::string line;
	unsigned total = 0;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		tmpUrlMatchHostList->insert(line);
		++total;

		if ((total % 100000) == 0) {
			log(LOG_INFO, "UrlMatchHostList::load: current total=%u", total);
		}

		logTrace(g_conf.m_logTraceUrlMatchHostList, "Adding criteria '%s' to list", line.c_str());
	}

	logTrace(g_conf.m_logTraceUrlMatchHostList, "Number of urlhost-match entries in %s: %ld", m_filename, (long)tmpUrlMatchHostList->size());
	swapUrlMatchHostList(tmpUrlMatchHostList);

	logTrace(g_conf.m_logTraceUrlMatchHostList, "Loaded %s", m_filename);
	return true;
}

void UrlMatchHostList::unload() {
	swapUrlMatchHostList(urlmatchhostlist_ptr_t(new urlmatchhostlist_t));
}

bool UrlMatchHostList::isUrlMatched(const Url &url) {
	auto urlmatchhostlist = getUrlMatchHostList();

	std::string host(url.getHost(), url.getHostLen());
	return (urlmatchhostlist->count(host) > 0);
}

urlmatchhostlistconst_ptr_t UrlMatchHostList::getUrlMatchHostList() {
	return m_urlmatchhostlist;
}

void UrlMatchHostList::swapUrlMatchHostList(urlmatchhostlistconst_ptr_t urlMatchHostList) {
	std::atomic_store(&m_urlmatchhostlist, urlMatchHostList);
}
