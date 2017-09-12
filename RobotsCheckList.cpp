#include "RobotsCheckList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

RobotsCheckList g_robotsCheckList;

static const char s_robots_filename[] = "robotschecklist.txt";

RobotsCheckList::RobotsCheckList()
	: m_filename(s_robots_filename)
	, m_robotsCheckList(new robotschecklist_t)
	, m_lastModifiedTime(0) {
}

bool RobotsCheckList::init() {
	log(LOG_INFO, "Initializing RobotsCheckList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, "RobotsCheckList::reload", 0)) {
		log(LOG_WARN, "RobotsCheckList:: Failed to register callback.");
		return false;
	}

	load();

	return true;
}

void RobotsCheckList::reload(int /*fd*/, void *state) {
	RobotsCheckList *robotsCheckList = static_cast<RobotsCheckList*>(state);
	robotsCheckList->load();
}

bool RobotsCheckList::load() {
	logTrace(g_conf.m_logTraceRobotsCheckList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "RobotsCheckList::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceRobotsCheckList, "Not modified");
		return true;
	}

	robotschecklist_ptr_t tmpRobotsCheckList(new robotschecklist_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		tmpRobotsCheckList->emplace_back(line);
		logTrace(g_conf.m_logTraceRobotsCheckList, "Adding criteria '%s' to list", line.c_str());
	}

	swapRobotsCheckList(tmpRobotsCheckList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceRobotsCheckList, "Loaded %s", m_filename);
	return true;
}

bool RobotsCheckList::isHostBlocked(const char *host) {
	auto robotsCheckList = getRobotsCheckList();

	for (auto const &criteria : *robotsCheckList) {
		if (criteria.front() == '*') {
			// wildcard
			if (strcasecmp(criteria.c_str() + 1, host + (strlen(host) - (criteria.length() - 1))) == 0) {
				logTrace(g_conf.m_logTraceRobotsCheckList, "Robots check list criteria %s matched host '%s'", criteria.c_str(), host);
				return true;
			}
		} else {
			if (strcasecmp(criteria.c_str(), host) == 0) {
				logTrace(g_conf.m_logTraceRobotsCheckList, "Robots check list %s matched host '%s'", criteria.c_str(), host);
				return true;
			}
		}
	}

	return false;
}

robotschecklistconst_ptr_t RobotsCheckList::getRobotsCheckList() {
	return m_robotsCheckList;
}

void RobotsCheckList::swapRobotsCheckList(robotschecklistconst_ptr_t robotsCheckList) {
	std::atomic_store(&m_robotsCheckList, robotsCheckList);
}
