#include "UrlMatchList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "Url.h"
#include "GbUtil.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>


UrlMatchList g_urlBlackList("urlblacklist.txt");
UrlMatchList g_urlWhiteList("urlwhitelist.txt");



UrlMatchList::UrlMatchList(const char *filename)
	: m_filename(filename)
	, m_urlMatchList(new urlmatchlist_t)
	, m_lastModifiedTime(0) {
}

bool UrlMatchList::init() {
	log(LOG_INFO, "Initializing UrlMatchList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, "UrlMatchList::reload", 0)) {
		log(LOG_WARN, "UrlMatchList: Failed to register callback.");
		return false;
	}

	// we do a load here instead of using sleep callback with immediate set to true so
	// we don't rely on g_loop being up and running to use urlmatchlist
	load();

	return true;
}

void UrlMatchList::reload(int /*fd*/, void *state) {
	UrlMatchList *urlMatchList = static_cast<UrlMatchList*>(state);
	urlMatchList->load();
}

static void parseDomain(urlmatchlist_ptr_t *urlMatchList, const std::string &col2, const std::string &col3, const std::string &col4) {
	std::string allowStr;
	if (!col3.empty()) {
		if (starts_with(col3.c_str(), "allow=")) {
			allowStr.append(col3, 6, std::string::npos);
		}
	}

	urlmatchdomain_t::pathcriteria_t pathcriteria = urlmatchdomain_t::pathcriteria_allow_all;
	if (!col4.empty()) {
		if (col4.compare("allowindexpage") == 0) {
			pathcriteria = urlmatchdomain_t::pathcriteria_allow_index_only;
		} else if (col4.compare("allowrootpages") == 0) {
			pathcriteria = urlmatchdomain_t::pathcriteria_allow_rootpages_only;
		}
	}

	(*urlMatchList)->emplace_back(std::shared_ptr<urlmatchdomain_t>(new urlmatchdomain_t(col2, allowStr, pathcriteria)));
}

bool UrlMatchList::load() {
	logTrace(g_conf.m_logTraceUrlMatchList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "UrlMatchList::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceUrlMatchList, "Not modified");
		return true;
	}

	urlmatchlist_ptr_t tmpUrlMatchList(new urlmatchlist_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		// look for first space or tab
		auto firstColEnd = line.find_first_of(" \t");
		size_t secondCol = line.find_first_not_of(" \t", firstColEnd);

		if (firstColEnd == std::string::npos || secondCol == std::string::npos) {
			// invalid format
			continue;
		}

		size_t secondColEnd = line.find_first_of(" \t", secondCol);

		size_t thirdCol = line.find_first_not_of(" \t", secondColEnd);
		size_t thirdColEnd = line.find_first_of(" \t", thirdCol);

		size_t fourthCol = line.find_first_not_of(" \t", thirdColEnd);
		size_t fourthColEnd = line.find_first_of(" \t", fourthCol);

		std::string col2(line, secondCol, secondColEnd - secondCol);
		std::string col3;
		if (thirdCol != std::string::npos) {
			col3 = std::string(line, thirdCol, thirdColEnd - thirdCol);
		}

		std::string col4;
		if (fourthCol != std::string::npos) {
			col4 = std::string(line, fourthCol, fourthColEnd - fourthCol);
		}

		switch (line[0]) {
			case 'd':
				// domain
				if (memcmp(line.c_str(), "domain", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlMatchList, "");
					continue;
				}

				parseDomain(&tmpUrlMatchList, col2, col3, col4);
				break;
			case 'h':
				// host
				if (memcmp(line.c_str(), "host", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlMatchList, "");
					continue;
				}

				tmpUrlMatchList->emplace_back(std::shared_ptr<urlmatchhost_t>(new urlmatchhost_t(col2, col3)));
				break;
			case 'p':
				// path
				if (memcmp(line.c_str(), "path", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlMatchList, "");
					continue;
				}

				tmpUrlMatchList->emplace_back(std::shared_ptr<urlmatchpath_t>(new urlmatchpath_t(col2)));
				break;
			case 'r':
				// regex
				if (memcmp(line.c_str(), "regex", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlMatchList, "");
					continue;
				}

				if (col3.empty()) {
					// invalid format
					continue;
				}

				// check for wildcard domain
				if (col2.length() == 1 && col2[0] == '*') {
					col2.clear();
				}

				tmpUrlMatchList->emplace_back(std::shared_ptr<urlmatchregex_t>(new urlmatchregex_t(col3, GbRegex(col3.c_str(), PCRE_NO_AUTO_CAPTURE, PCRE_STUDY_JIT_COMPILE), col2)));
				break;
			case 't':
				if (memcmp(line.c_str(), "tld", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlMatchList, "");
					continue;
				}

				tmpUrlMatchList->emplace_back(std::shared_ptr<urlmatchtld_t>(new urlmatchtld_t(col2)));
				break;
			default:
				logError("Invalid line found. Ignoring line='%s'", line.c_str());
				continue;
		}

		logTrace(g_conf.m_logTraceUrlMatchList, "Adding criteria '%s' to list", line.c_str());
	}

	logTrace(g_conf.m_logTraceUrlMatchList, "Number of url-match entries in %s: %ld", m_filename, (long)tmpUrlMatchList->size());
	swapUrlMatchList(tmpUrlMatchList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceUrlMatchList, "Loaded %s", m_filename);
	return true;
}

bool UrlMatchList::isUrlMatched(const Url &url) {
	auto urlMatchList = getUrlMatchList();

	for (auto const &urlMatch : *urlMatchList) {
		if (urlMatch.match(url)) {
			if (g_conf.m_logTraceUrlMatchList) {
				urlMatch.logMatch(url);
			}
			return true;
		}
	}
	
	return false;
}

urlmatchlistconst_ptr_t UrlMatchList::getUrlMatchList() {
	return m_urlMatchList;
}

void UrlMatchList::swapUrlMatchList(urlmatchlistconst_ptr_t urlMatchList) {
	std::atomic_store(&m_urlMatchList, urlMatchList);
}
