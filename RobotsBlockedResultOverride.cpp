#include "RobotsBlockedResultOverride.h"
#include "ResultOverride.h"
#include "Loop.h"
#include "Log.h"
#include "Conf.h"
#include "Url.h"
#include "GbUtil.h"

#include <sys/stat.h>
#include <fstream>

RobotsBlockedResultOverride g_robotsBlockedResultOverride;

static const char *s_resultoverride_filename = "robotsblockedresultoverride.txt";

RobotsBlockedResultOverride::RobotsBlockedResultOverride()
	: m_filename(s_resultoverride_filename)
	, m_resultOverrideMap(new resultoverridemap_t)
	, m_lastModifiedTime(0) {
}

bool RobotsBlockedResultOverride::init() {
	if (!g_loop.registerSleepCallback(60000, this, &reload, "RobotsBlockedResultOverride::reload", 0)) {
		log(LOG_WARN, "RobotsBlockedResultOverride::init: Failed to register callback.");
		return false;
	}

	load();

	return true;
}

void RobotsBlockedResultOverride::reload(int /*fd*/, void *state) {
	RobotsBlockedResultOverride *robotsBlockedResultOverride = static_cast<RobotsBlockedResultOverride*>(state);
	robotsBlockedResultOverride->load();
}

bool RobotsBlockedResultOverride::load() {
	g_conf.m_logTraceRobotsBlocked = true;
	logTrace(g_conf.m_logTraceRobotsBlocked, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "RobotsBlockedResultOverride::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceRobotsBlocked, "Not modified");
		return true;
	}

	resultoverridemap_ptr_t tmpResultOverride(new resultoverridemap_t);

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

		tmpResultOverride->emplace(tokens[0], ResultOverride(tokens[1], tokens[2]));
		logTrace(g_conf.m_logTraceRobotsBlocked, "Adding criteria '%s' to list", line.c_str());
	}

	swapResultOverride(tmpResultOverride);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceRobotsBlocked, "Loaded %s", m_filename);
	return true;
}

std::string RobotsBlockedResultOverride::getTitle(const std::string &lang, const Url &url) {
	logTrace(g_conf.m_logTraceRobotsBlocked, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto resultOverrideMap = getResultOverrideMap();
	auto it = resultOverrideMap->find(lang);
	if (it != resultOverrideMap->end()) {
		return it->second.getTitle(url);
	}

	// return as default
	return std::string(url.getHost(), url.getHostLen());
}

std::string RobotsBlockedResultOverride::getSummary(const std::string &lang, const Url &url) {
	logTrace(g_conf.m_logTraceRobotsBlocked, "lang=%s url=%s", lang.c_str(), url.getUrl());

	auto resultOverrideMap = getResultOverrideMap();
	auto it = resultOverrideMap->find(lang);
	if (it != resultOverrideMap->end()) {
		return it->second.getSummary(url);
	}

	// return as default
	return "No description available";
}

void RobotsBlockedResultOverride::swapResultOverride(resultoverridemapconst_ptr_t resultOverrideMap) {
	std::atomic_store(&m_resultOverrideMap, resultOverrideMap);
}

resultoverridemapconst_ptr_t RobotsBlockedResultOverride::getResultOverrideMap() {
	return m_resultOverrideMap;
}
