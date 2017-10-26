//
// Created by alc on 10/25/17.
//

#include "LanguageResultOverride.h"
#include "ResultOverride.h"
#include "Url.h"
#include "Log.h"
#include "Conf.h"
#include "GbUtil.h"

#include <sys/stat.h>
#include <fstream>

LanguageResultOverride::LanguageResultOverride(const char *filename)
	: m_filename(filename)
	, m_languageResultOverrideMap(new languageresultoverridemap_t)
	, m_lastModifiedTime(0) {
}

LanguageResultOverride::LanguageResultOverride(languageresultoverridemap_ptr_t languageResultOverrideMap)
	: m_filename(nullptr)
	, m_languageResultOverrideMap(languageResultOverrideMap)
	, m_lastModifiedTime(0) {
}

bool LanguageResultOverride::load() {
	// sanity check to make sure we don't call load with a null filename
	if (m_filename == nullptr) {
		gbshutdownLogicError();
	}

	logTrace(g_conf.m_logTraceLanguageResultOverride, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "RobotsBlockedResultOverride::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceLanguageResultOverride, "Not modified");
		return true;
	}

	languageresultoverridemap_ptr_t tmpLanguageResultOverride(new languageresultoverridemap_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		std::vector<std::string> tokens = split(line, '|');
		if (tokens.size() != 3) {
			logError("Invalid line found. Ignoring line='%s'", line.c_str());
			continue;
		}

		tmpLanguageResultOverride->emplace(tokens[0], ResultOverride(tokens[1], tokens[2]));

		logTrace(g_conf.m_logTraceLanguageResultOverride, "Adding criteria '%s' to list", line.c_str());
	}

	swapLanguageResultOverride(tmpLanguageResultOverride);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceLanguageResultOverride, "Loaded %s", m_filename);
	return true;
}

std::string LanguageResultOverride::getTitle(const std::string &lang, const Url &url) const {
	logTrace(g_conf.m_logTraceLanguageResultOverride, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto resultOverrideMap = getLanguageResultOverrideMap();
	auto it = resultOverrideMap->find(lang);
	if (it != resultOverrideMap->end()) {
		logTrace(g_conf.m_logTraceLanguageResultOverride, "found lang=%s", lang.c_str());
		return it->second.getTitle(url);
	}

	// default to english
	if (lang.compare("en") != 0) {
		auto it = resultOverrideMap->find("en");
		if (it != resultOverrideMap->end()) {
			logTrace(g_conf.m_logTraceLanguageResultOverride, "found default lang=en");
			return it->second.getTitle(url);
		}
	}

	// empty means no override
	return "";
}

std::string LanguageResultOverride::getSummary(const std::string &lang, const Url &url) const {
	logTrace(g_conf.m_logTraceLanguageResultOverride, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto resultOverrideMap = getLanguageResultOverrideMap();
	auto it = resultOverrideMap->find(lang);
	if (it != resultOverrideMap->end()) {
		logTrace(g_conf.m_logTraceLanguageResultOverride, "found lang=%s", lang.c_str());
		return it->second.getSummary(url);
	}

	// default to english
	if (lang.compare("en") != 0) {
		auto it = resultOverrideMap->find("en");
		if (it != resultOverrideMap->end()) {
			logTrace(g_conf.m_logTraceLanguageResultOverride, "found default lang=en");
			return it->second.getSummary(url);
		}
	}

	// empty means no override
	return "";
}

void LanguageResultOverride::swapLanguageResultOverride(languageresultoverridemapconst_ptr_t languageResultOverrideMap) {
	std::atomic_store(&m_languageResultOverrideMap, languageResultOverrideMap);
}

languageresultoverridemapconst_ptr_t LanguageResultOverride::getLanguageResultOverrideMap() const {
	return m_languageResultOverrideMap;
}
