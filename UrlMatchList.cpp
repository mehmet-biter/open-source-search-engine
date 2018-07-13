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

typedef std::vector<UrlMatch> urlmatches_t;
typedef std::vector<urlmatches_t> urlmatcheslist_t;
typedef spp::sparse_hash_map<std::string, urlmatcheslist_t> urlmatchlist_map_t;

struct UrlMatchCriterias {
	bool m_hasDomain;
	std::string m_domain;

	urlmatcheslist_t m_urlMatches;
};

struct UrlMatchListItem {
	spp::sparse_hash_set<std::string> m_domainMatches;
	spp::sparse_hash_set<std::string> m_tldMatches;

	urlmatchlist_map_t m_domainUrlMatchesList;
	urlmatcheslist_t m_urlMatchesList;
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

static bool parseMatchParam(urlmatches_t *urlMatches, urlmatchtype_t type, const std::vector<std::string> &tokens, bool invert) {
	// validate
	if (tokens.size() < 2 || tokens.size() > 3) {
		return false;
	}

	const std::string &name = tokens[1];
	std::string value;
	if (tokens.size() == 3) {
		value = tokens[2];
	}

	urlMatches->emplace_back(std::shared_ptr<urlmatchparam_t>(new urlmatchparam_t(type, name, value)), invert);
	return true;
}

static bool parseMatchSet(urlmatches_t *urlMatches, urlmatchtype_t type, const std::vector<std::string> &tokens, bool invert) {
	// validate
	if (tokens.size() != 2) {
		return false;
	}

	const std::string &str = tokens[1];
	auto matcher = std::shared_ptr<urlmatchset_t>(new urlmatchset_t(type, str));
	urlMatches->emplace_back(matcher, invert);

	return true;
}

static bool parseMatchStrWithCriteria(urlmatches_t *urlMatches, urlmatchtype_t type, const std::vector<std::string> &tokens,
                                      bool invert, urlmatchstr_t::matchcriteria_t default_matchcriteria = urlmatchstr_t::matchcriteria_exact) {
	// validate
	if (tokens.size() < 2 || tokens.size() > 3) {
		return false;
	}

	const std::string &str = tokens[1];

	if (tokens.size() == 2) {
		urlMatches->emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(type, str, default_matchcriteria)), invert);
	} else {
		const std::string &match_criteria = tokens[2];
		urlMatches->emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(type, str, match_criteria)), invert);
	}

	return true;
}

static bool parseMatchStr(urlmatches_t *urlMatches, urlmatchtype_t type, const std::vector<std::string> &tokens, bool invert) {
	// validate
	if (tokens.size() != 2) {
		return false;
	}

	const std::string &str = tokens[1];

	urlMatches->emplace_back(std::shared_ptr<urlmatchstr_t>(new urlmatchstr_t(type, str, "")), invert);
	return true;
}

static bool parsePathCriteria(urlmatches_t *urlMatches, const std::vector<std::string> &tokens, bool invert) {
	// validate
	if (tokens.size() != 2) {
		return false;
	}

	const std::string &path_criteria = tokens[1];

	auto matcher = std::shared_ptr<urlmatchpathcriteria_t>(new urlmatchpathcriteria_t(path_criteria));
	urlMatches->emplace_back(matcher, invert);

	return true;
}

static bool parseRegex(urlmatches_t *urlMatches, const std::vector<std::string> &tokens, bool invert) {
	// validate
	if (tokens.size() != 2) {
		return false;
	}

	const std::string &regex = tokens[1];
	auto matcher = std::shared_ptr<urlmatchregex_t>(new urlmatchregex_t(regex, GbRegex(regex.c_str(), PCRE_NO_AUTO_CAPTURE, PCRE_STUDY_JIT_COMPILE)));
	urlMatches->emplace_back(matcher, invert);

	return true;
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

		bool foundInvalid = false;
		int count = 0;
		std::ifstream file(filePath.c_str());
		std::string line;
		while (std::getline(file, line)) {
			// ignore comments & empty lines
			if (line.length() == 0 || line[0] == '#') {
				continue;
			}

			std::replace(line.begin(), line.end(), '\t', ' ');

			auto criterias = split(line, " AND ");

			urlmatches_t urlMatches;
			for (auto criteria : criterias) {
				auto tokens = split(criteria, ' ');
				tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const std::string &s) { return s.empty(); }), tokens.end());

				bool invert = false;
				if (tokens[0] == "NOT") {
					invert = true;
					tokens.erase(tokens.begin());
				}

				const std::string &type = tokens[0];

				switch (type[0]) {
					case 'd':
						// domain
						if (type.compare("domain") == 0) {
							if (!parseMatchStrWithCriteria(&urlMatches, url_match_domain, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'e':
						// extension
						if (type.compare("extension") == 0) {
							if (!parseMatchStr(&urlMatches, url_match_extension, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'f':
						// file
						if (type.compare("file") == 0) {
							if (!parseMatchStr(&urlMatches, url_match_file, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'h':
						// host
						if (type.compare("host") == 0) {
							if (!parseMatchStrWithCriteria(&urlMatches, url_match_host, tokens, invert, urlmatchstr_t::matchcriteria_suffix)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'm':
						// middomain
						if (type.compare("middomain") == 0) {
							if (!parseMatchStr(&urlMatches, url_match_middomain, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'p':
						if (type.compare("param") == 0) {
							// query param
							if (!parseMatchParam(&urlMatches, url_match_queryparam, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else if (type.compare("path") == 0) {
							// path
							if (!parseMatchStrWithCriteria(&urlMatches, url_match_path, tokens, invert, urlmatchstr_t::matchcriteria_prefix)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else if (type.compare("pathcriteria") == 0) {
							if (!parsePathCriteria(&urlMatches, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else if (type.compare("pathparam") == 0) {
							// path param
							if (!parseMatchParam(&urlMatches, url_match_pathparam, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else if (type.compare("port") == 0) {
							// port
							if (!parseMatchStr(&urlMatches, url_match_port, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 'r':
						// regex
						if (type.compare("regex") == 0) {
							if (!parseRegex(&urlMatches, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 's':
						if (type.compare("scheme") == 0) {
							// scheme
							if (!parseMatchStr(&urlMatches, url_match_scheme, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else if (type.compare("subdomain") == 0) {
							// subdomain
							if (!parseMatchSet(&urlMatches, url_match_subdomain, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					case 't':
						// tld
						if (type.compare("tld") == 0) {
							if (!parseMatchSet(&urlMatches, url_match_tld, tokens, invert)) {
								logError("Invalid '%s' line found. Ignoring line='%s'", type.c_str(), line.c_str());
								foundInvalid = true;
								continue;
							}
						} else {
							logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
							foundInvalid = true;
							continue;
						}
						break;
					default:
						logError("Invalid type '%s' found. Ignoring line='%s'", type.c_str(), line.c_str());
						foundInvalid = true;
						continue;
				}
			}

			if (foundInvalid) {
				continue;
			}

			logTrace(g_conf.m_logTraceUrlMatchList, "Adding criteria '%s' to list", line.c_str());
			++count;

			if (urlMatches.empty()) {
				gbshutdownLogicError();
			}

			if (urlMatches.size() == 1) {
				const UrlMatch &urlMatch = urlMatches.front();
				if (urlMatch.getType() == url_match_domain) {
					tmpUrlMatchList->m_domainMatches.insert(urlMatch.getDomain());
					continue;
				}
			}

			/// @todo ALC do we need shortcut for TLD?

			auto func = [](const UrlMatch &match) {
				return match.getType() == url_match_domain ||
				       match.getType() == url_match_host;
			};

			auto it = std::find_if(urlMatches.begin(), urlMatches.end(), func);
			if (it != urlMatches.end()) {
				std::string domain = it->getDomain();
				if (!domain.empty()) {
					auto &list = tmpUrlMatchList->m_domainUrlMatchesList[domain];
					list.emplace_back(urlMatches);
					continue;
				}
			}

			tmpUrlMatchList->m_urlMatchesList.emplace_back(urlMatches);
		}

		if (foundInvalid) {
			/// @todo ALC log to eventlog

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

static bool matchUrlMatches(const urlmatches_t &urlMatches, const Url &url, const UrlParser &urlParser) {
	logTrace(g_conf.m_logTraceUrlMatchList, "Matching url matches for url=%s", url.getUrl());

	bool matchAll = true;
	for (auto const &urlMatch : urlMatches) {
		if (!urlMatch.match(url, urlParser)) {
			matchAll = false;
			break;
		}

		urlMatch.logMatch(url);
	}

	if (matchAll) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Matched all for url=%s", url.getUrl());
		return true;
	}

	return false;
}

static bool matchList(const urlmatchlist_map_t &matcher, const std::string &key, const Url &url, const UrlParser &urlParser) {
	logTrace(g_conf.m_logTraceUrlMatchList, "Matching list with key=%s", key.c_str());
	auto it = matcher.find(key);
	if (it != matcher.end()) {
		for (auto urlMatches : it->second) {
			if (matchUrlMatches(urlMatches, url, urlParser)) {
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
		logTrace(g_conf.m_logTraceUrlMatchList, "Url match criteria domain='%s' matched url '%s'", domain.c_str(), url.getUrl());
		return true;
	}

	// simple tld match
	auto pos = domain.find_last_of('.');
	if (pos != std::string::npos) {
		std::string tld = domain.substr(pos + 1);
		if (urlMatchList->m_tldMatches.count(tld) > 0) {
			logTrace(g_conf.m_logTraceUrlMatchList, "Url match criteria tld='%s' matched url '%s'", tld.c_str(), url.getUrl());
			return true;
		}
	}

	// check urlmatches using domain as key
	if (matchList(urlMatchList->m_domainUrlMatchesList, domain, url, urlParser)) {
		return true;
	}

	for (auto const &urlMatches : urlMatchList->m_urlMatchesList) {
		if (matchUrlMatches(urlMatches, url, urlParser)) {
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
