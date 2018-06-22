#include "UrlMatchList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "Url.h"
#include "GbUtil.h"
#include "Dir.h"
#include "Hostdb.h"
#include "third-party/sparsepp/sparsepp/spp.h"
#include "JobScheduler.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>


UrlMatchList g_urlBlackList("urlblacklist*.txt");
UrlMatchList g_urlWhiteList("urlwhitelist.txt");
UrlMatchList g_urlProxyList("urlproxylist.txt");
UrlMatchList g_urlRetryProxyList("urlretryproxylist.txt");

typedef std::vector<UrlMatch> urlmatchlist_t;
typedef spp::sparse_hash_map<std::string, urlmatchlist_t> urlmatchlist_map_t;

struct UrlMatchListItem {
	spp::sparse_hash_set<std::string> m_domainMatches;
	urlmatchlist_map_t m_listMatches;
	urlmatchlist_t m_urlMatches;
};

UrlMatchList::UrlMatchList(const char *filename)
	: m_filename(filename)
	, m_dirname()
	, m_loading(false)
	, m_urlMatchList(new UrlMatchListItem)
	, m_lastModifiedTimes() {
	size_t pos = m_filename.find_last_of('/');
	if (pos != std::string::npos) {
		m_dirname = m_filename.substr(0, pos);
		m_filename.erase(0, pos + 1);
	}
}

bool UrlMatchList::init() {
	log(LOG_INFO, "Initializing UrlMatchList with %s", m_filename.c_str());

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
	if (g_jobScheduler.submit(reload, nullptr, state, thread_type_config_load, 0)) {
		return;
	}

	// unable to submit job (load on main thread)
	reload(state);
}

void UrlMatchList::reload(void *state) {
	UrlMatchList *urlMatchList = static_cast<UrlMatchList*>(state);

	// don't load multiple times at the same time
	if (urlMatchList->m_loading.exchange(true)) {
		return;
	}

	urlMatchList->load();
	urlMatchList->m_loading = false;
}

static bool parseDomain(urlmatchlistitem_ptr_t *urlMatchList, const std::string &col2, const std::string &col3, const std::string &col4) {
	// verify that col2 is actually a domain
	Url url;
	url.set(col2.c_str());

	if (static_cast<size_t>(url.getDomainLen()) != col2.length()) {
		return false;
	}

	if (col3.empty() && col4.empty()) {
		(*urlMatchList)->m_domainMatches.insert(col2);
		return true;
	}

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

	auto matcher = std::shared_ptr<urlmatchdomain_t>(new urlmatchdomain_t(col2, allowStr, pathcriteria));
	auto &list = (*urlMatchList)->m_listMatches[matcher->m_domain];
	list.emplace_back(matcher);

	return true;
}

static void parseHost(urlmatchlistitem_ptr_t *urlMatchList, const std::string &col2, const std::string &col3) {
	auto matcher = std::shared_ptr<urlmatchhost_t>(new urlmatchhost_t(col2, col3));

	Url url;
	url.set(matcher->m_host.c_str());

	auto &list = (*urlMatchList)->m_listMatches[std::string(url.getDomain(), url.getDomainLen())];
	list.emplace_back(matcher);
}

static void parseRegex(urlmatchlistitem_ptr_t *urlMatchList, const std::string &col2, const std::string &col3) {
	// check for wildcard domain
	std::string domain(col2);
	if (domain.length() == 1 && domain[0] == '*') {
		domain.clear();
	}

	auto matcher = std::shared_ptr<urlmatchregex_t>(new urlmatchregex_t(col3, GbRegex(col3.c_str(), PCRE_NO_AUTO_CAPTURE, PCRE_STUDY_JIT_COMPILE), domain));
	if (domain.empty()) {
		(*urlMatchList)->m_urlMatches.emplace_back(matcher);
	} else {
		auto &list = (*urlMatchList)->m_listMatches[matcher->m_domain];
		list.emplace_back(matcher);
	}
}

static void parseHostSuffix(urlmatchlistitem_ptr_t *urlMatchList, const std::string &col2) {
	auto matcher = std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(url_match_hostsuffix, col2));

	Url url;
	url.set(matcher->m_str.c_str());

	auto &list = (*urlMatchList)->m_listMatches[std::string(url.getDomain(), url.getDomainLen())];
	list.emplace_back(matcher);
}

bool UrlMatchList::load() {
	std::string dirname(m_dirname);
	if (dirname.empty()) {
		dirname = g_hostdb.m_dir;
	}

	Dir dir;
	if (!dir.set(dirname.c_str()) || !dir.open()) {
		logError("Had error opening directory %s", dirname.c_str());
		return false;
	}

	urlmatchlistitem_ptr_t tmpUrlMatchList(new UrlMatchListItem);

	std::vector<std::string> filePaths;
	bool anyFileModified = false;
	while (const char *filename = dir.getNextFilename(m_filename.c_str())) {
		std::string filePath(filename);
		if (!m_dirname.empty()) {
			filePath.insert(0, "/");
			filePath.insert(0, m_dirname);
		}

		logTrace(g_conf.m_logTraceUrlMatchList, "Loading %s", filePath.c_str());

		struct stat st;
		if (stat(filePath.c_str(), &st) != 0) {
			// probably not found
			log(LOG_INFO, "UrlMatchList::load: Unable to stat %s", filePath.c_str());
			continue;
		}

		filePaths.push_back(filePath);

		time_t lastModifiedTime = m_lastModifiedTimes[filePath];
		if (lastModifiedTime != 0 && lastModifiedTime == st.st_mtime) {
			// not modified. assume successful
			logTrace(g_conf.m_logTraceUrlMatchList, "Not modified");
			continue;
		}

		anyFileModified = true;

		m_lastModifiedTimes[filePath] = st.st_mtime;
	}

	if (!anyFileModified) {
		return false;
	}

	int totalCount = 0;
	bool loadedFile = false;
	for (const auto &filePath : filePaths) {
		log(LOG_INFO, "Loading '%s' for UrlMatchList", filePath.c_str());

		int count = 0;
		std::ifstream file(filePath.c_str());
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
					if (firstColEnd == 6 && memcmp(line.data(), "domain", 6) == 0) {
						if (!parseDomain(&tmpUrlMatchList, col2, col3, col4)) {
							logError("Invalid line found. Ignoring line='%s'", line.c_str());
							// catch domain parsing errors here
							gbshutdownLogicError();
							continue;
						}
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 'f':
					// file
					if (firstColEnd == 4 && memcmp(line.data(), "file", 4) == 0) {
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(url_match_file, col2)));
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 'h':
					// host
					if (firstColEnd == 4 && memcmp(line.data(), "host", 4) == 0) {
						parseHost(&tmpUrlMatchList, col2, col3);
					} else if (firstColEnd == 10 && memcmp(line.data(), "hostsuffix", 10) == 0) {
						parseHostSuffix(&tmpUrlMatchList, col2);
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 'p':
					if (firstColEnd == 5 && memcmp(line.data(), "param", 5) == 0) {
						// query param
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchparam_t>(new urlmatchparam_t(url_match_queryparam, col2, col3)));
					} else if (firstColEnd == 4 && memcmp(line.data(), "path", 4) == 0) {
						// path
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(url_match_path, col2)));
					} else if (firstColEnd == 9 && memcmp(line.data(), "pathparam", 9) == 0) {
						// path param
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchparam_t>(new urlmatchparam_t(url_match_pathparam, col2, col3)));
					} else if (firstColEnd == 11 && memcmp(line.data(), "pathpartial", 11) == 0) {
						// path
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(url_match_pathpartial, col2)));
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 'q':
					if (firstColEnd == 10 && memcmp(line.data(), "queryparam", 10) == 0) {
						// query param
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchparam_t>(new urlmatchparam_t(url_match_queryparam, col2, col3)));
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 'r':
					// regex
					if (firstColEnd == 5 && memcmp(line.data(), "regex", 5) == 0 && !col3.empty()) {
						parseRegex(&tmpUrlMatchList, col2, col3);
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				case 't':
					// tld
					if (firstColEnd == 3 && memcmp(line.data(), "tld", 3) == 0) {
						tmpUrlMatchList->m_urlMatches.emplace_back(std::shared_ptr<urlmatchtld_t>(new urlmatchtld_t(col2)));
					} else {
						logError("Invalid line found. Ignoring line='%s'", line.c_str());
						continue;
					}
					break;
				default:
					logError("Invalid line found. Ignoring line='%s'", line.c_str());
					continue;
			}

			logTrace(g_conf.m_logTraceUrlMatchList, "Adding criteria '%s' to list", line.c_str());
			++count;
		}

		loadedFile = true;
		log(LOG_INFO, "Loaded '%s' with %d entries for UrlMatchList", filePath.c_str(), count);

		totalCount += count;
	}

	if (loadedFile) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Number of url-match entries in %s: %d", m_filename.c_str(), totalCount);
		swapUrlMatchList(tmpUrlMatchList);
	}

	return loadedFile;
}

static bool matchList(const urlmatchlist_map_t &matcher, const std::string &key, const Url &url, const UrlParser &urlParser) {
	auto it = matcher.find(key);
	if (it != matcher.end()) {
		for (auto urlMatch : it->second) {
			if (urlMatch.match(url, urlParser)) {
				if (g_conf.m_logTraceUrlMatchList) {
					urlMatch.logMatch(url);
				}
				return true;
			}
		}
	}

	return false;
}

bool UrlMatchList::isUrlMatched(const Url &url) {
	UrlParser urlParser(url.getUrl(), url.getUrlLen(), TITLEREC_CURRENT_VERSION);

	auto urlMatchList = getUrlMatchList();

	std::string domain(url.getDomain(), url.getDomainLen());

	// simple domain match
	if (urlMatchList->m_domainMatches.count(domain) > 0) {
		return true;
	}

	// check urlmatches using domain as key
	if (matchList(urlMatchList->m_listMatches, domain, url, urlParser)) {
		return true;
	}

	for (auto const &urlMatch : urlMatchList->m_urlMatches) {
		if (urlMatch.match(url, urlParser)) {
			if (g_conf.m_logTraceUrlMatchList) {
				urlMatch.logMatch(url);
			}
			return true;
		}
	}
	
	return false;
}

urlmatchlistitemconst_ptr_t UrlMatchList::getUrlMatchList() {
	return m_urlMatchList;
}

void UrlMatchList::swapUrlMatchList(urlmatchlistitemconst_ptr_t urlMatchList) {
	std::atomic_store(&m_urlMatchList, urlMatchList);
}
