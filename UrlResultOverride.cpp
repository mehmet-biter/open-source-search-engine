#include "UrlResultOverride.h"
#include "LanguageResultOverride.h"
#include "ResultOverride.h"
#include "Loop.h"
#include "Log.h"
#include "Conf.h"
#include "Url.h"
#include "GbUtil.h"

#include <sys/stat.h>
#include <fstream>

UrlResultOverride g_urlResultOverride;

static const char *s_resultoverride_filename = "urlresultoverride.txt";

UrlResultOverride::UrlResultOverride()
	: m_filename(s_resultoverride_filename)
	, m_urlResultOverrideMap(new urlresultoverridemap_t)
	, m_lastModifiedTime(0) {
}

bool UrlResultOverride::init() {
	if (!g_loop.registerSleepCallback(60000, this, &reload, "UrlResultOverride::reload", 0)) {
		log(LOG_WARN, "UrlResultOverride::init: Failed to register callback.");
		return false;
	}

	load();

	return true;
}

void UrlResultOverride::reload(int /*fd*/, void *state) {
	UrlResultOverride *urlResultOverride = static_cast<UrlResultOverride*>(state);
	urlResultOverride->load();
}

bool UrlResultOverride::load() {
	logTrace(g_conf.m_logTraceUrlResultOverride, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "UrlResultOverride::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceUrlResultOverride, "Not modified");
		return true;
	}

	urlresultoverridemap_ptr_t tmpUrlResultOverride(new urlresultoverridemap_t);
	languageresultoverridemap_ptr_t tmpLanguageResultOverride(new languageresultoverridemap_t);
	std::string prevUrl;

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		std::vector<std::string> tokens = split(line, '|');
		if (tokens.size() != 4) {
			logError("Invalid line found. Ignoring line='%s'", line.c_str());
			continue;
		}

		std::string currentUrl(tokens[0]);

		if (!prevUrl.empty() && prevUrl.compare(currentUrl) != 0) {
			logTrace(g_conf.m_logTraceUrlResultOverride, "Adding url '%s' to url map", prevUrl.c_str());
			tmpLanguageResultOverride = std::make_shared<languageresultoverridemap_t>();
			tmpUrlResultOverride->emplace(prevUrl, LanguageResultOverride(tmpLanguageResultOverride));
		}

		tmpLanguageResultOverride->emplace(tokens[1], ResultOverride(tokens[2], tokens[3]));
		logTrace(g_conf.m_logTraceUrlResultOverride, "Adding criteria '%s' to language map", line.c_str());

		prevUrl = currentUrl;
	}

	// add last item
	tmpUrlResultOverride->emplace(prevUrl, LanguageResultOverride(tmpLanguageResultOverride));

	swapUrlResultOverride(tmpUrlResultOverride);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceUrlResultOverride, "Loaded %s", m_filename);
	return true;
}

std::string UrlResultOverride::getTitle(const std::string &lang, const Url &url) {
	logTrace(g_conf.m_logTraceUrlResultOverride, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto urlResultOverrideMap = getUrlResultOverrideMap();
	auto it = urlResultOverrideMap->find(url.getUrl());
	if (it != urlResultOverrideMap->end()) {
		logTrace(g_conf.m_logTraceUrlResultOverride, "found url=%s", url.getUrl());
		return it->second.getTitle(lang, url);
	}

	logTrace(g_conf.m_logTraceUrlResultOverride, "lang=%s url=%s no override", lang.c_str(), url.getUrl());

	// empty means no override
	return "";
}

std::string UrlResultOverride::getSummary(const std::string &lang, const Url &url) {
	logTrace(g_conf.m_logTraceUrlResultOverride, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto urlResultOverrideMap = getUrlResultOverrideMap();
	auto it = urlResultOverrideMap->find(url.getUrl());
	if (it != urlResultOverrideMap->end()) {
		logTrace(g_conf.m_logTraceUrlResultOverride, "found url=%s", url.getUrl());
		return it->second.getSummary(lang, url);
	}

	logTrace(g_conf.m_logTraceUrlResultOverride, "lang=%s url=%s no override", lang.c_str(), url.getUrl());

	// empty means no override
	return "";
}

void UrlResultOverride::swapUrlResultOverride(urlresultoverridemapconst_ptr_t resultOverrideMap) {
	std::atomic_store(&m_urlResultOverrideMap, resultOverrideMap);
}

urlresultoverridemapconst_ptr_t UrlResultOverride::getUrlResultOverrideMap() {
	return m_urlResultOverrideMap;
}
