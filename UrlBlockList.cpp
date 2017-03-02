#include "UrlBlockList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "Url.h"
#include "GbUtil.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

UrlBlockList g_urlBlockList;

static const char s_url_filename[] = "urlblocklist.txt";

UrlBlockList::UrlBlockList()
	: m_filename(s_url_filename)
	, m_urlBlockList(new urlblocklist_t)
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

	urlblocklist_ptr_t tmpUrlBlockList(new urlblocklist_t);

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

		std::string col2(line, secondCol, secondColEnd - secondCol);
		std::string col3;
		if (thirdCol != std::string::npos) {
			col3 = std::string(line, thirdCol);
		}

		switch (line[0]) {
			case 'd':
				// domain
				if (memcmp(line.c_str(), "domain", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlBlockList, "");
					continue;
				}

				if (starts_with(col3.c_str(), "allow=")) {
					col3.erase(0, 6);
				} else {
					col3.clear();
				}
				tmpUrlBlockList->push_back(UrlBlock(std::shared_ptr<urlblockdomain_t>(new urlblockdomain_t(col2, col3))));
				break;
			case 'h':
				// host
				if (memcmp(line.c_str(), "host", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlBlockList, "");
					continue;
				}

				tmpUrlBlockList->push_back(UrlBlock(std::shared_ptr<urlblockhost_t>(new urlblockhost_t(col2))));
				break;
			case 'p':
				// path
				if (memcmp(line.c_str(), "path", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlBlockList, "");
					continue;
				}

				tmpUrlBlockList->push_back(UrlBlock(std::shared_ptr<urlblockpath_t>(new urlblockpath_t(col2))));
				break;
			case 'r':
				// regex
				if (memcmp(line.c_str(), "regex", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlBlockList, "");
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

				tmpUrlBlockList->push_back(UrlBlock(std::shared_ptr<urlblockregex_t>(new urlblockregex_t(col3, GbRegex(col3.c_str(), PCRE_NO_AUTO_CAPTURE, PCRE_STUDY_JIT_COMPILE), col2))));
				break;
			case 't':
				if (memcmp(line.c_str(), "tld", firstColEnd) != 0) {
					logTrace(g_conf.m_logTraceUrlBlockList, "");
					continue;
				}

				tmpUrlBlockList->push_back(UrlBlock(std::shared_ptr<urlblocktld_t>(new urlblocktld_t(col2))));
				break;
			default:
				continue;
		}

		logTrace(g_conf.m_logTraceUrlBlockList, "Adding criteria '%s' to list", line.c_str());
	}

	swapUrlBlockList(tmpUrlBlockList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceUrlBlockList, "Loaded %s", m_filename);
	return true;
}

bool UrlBlockList::isUrlBlocked(const Url &url) {
	auto urlRegexList = getUrlBlockList();

	for (auto const &urlBlock : *urlRegexList) {
		if (urlBlock.match(url)) {
			if (g_conf.m_logTraceUrlBlockList) {
				urlBlock.logMatch(url);
			}
			return true;
		}
	}

	return false;
}

urlblocklistconst_ptr_t UrlBlockList::getUrlBlockList() {
	return m_urlBlockList;
}

void UrlBlockList::swapUrlBlockList(urlblocklistconst_ptr_t urlRegexList) {
	std::atomic_store(&m_urlBlockList, urlRegexList);
}

