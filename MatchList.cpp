//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#include "MatchList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "JobScheduler.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

template <class T>
MatchList<T>::MatchList(const char *filename)
	: m_filename(filename)
	, m_loading(false)
	, m_matchList(new matchlist_t<T>)
	, m_lastModifiedTime(0) {
}

template <class T>
bool MatchList<T>::init() {
	log(LOG_INFO, "Initializing MatchList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, "MatchList<T>::reload", 0)) {
		log(LOG_WARN, "MatchList<T>:: Failed to register callback.");
		return false;
	}

	// we do a load here instead of using sleep callback with immediate set to true so
	// we don't rely on g_loop being up and running to use matchlist
	load();

	return true;
}

template <class T>
void MatchList<T>::reload(int /*fd*/, void *state) {
	if (g_jobScheduler.submit(reload, nullptr, state, thread_type_config_load, 0)) {
		return;
	}

	// unable to submit job (load on main thread)
	reload(state);
}

template <class T>
void MatchList<T>::reload(void *state) {
	MatchList *matchList = static_cast<MatchList*>(state);

	// don't load multiple times at the same time
	if (matchList->m_loading.exchange(true)) {
		return;
	}

	matchList->load();
	matchList->m_loading = false;
}

template <class T>
bool MatchList<T>::load() {
	logTrace(g_conf.m_logTraceMatchList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "MatchList<T>::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceMatchList, "%s not modified", m_filename);
		return true;
	}

	matchlist_ptr_t<T> tmpMatchList(new matchlist_t<T>);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		addToMatchList(tmpMatchList, line);
		logTrace(g_conf.m_logTraceMatchList, "Adding criteria '%s' to list", line.c_str());
	}

	swapMatchList(tmpMatchList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceMatchList, "Loaded %s", m_filename);
	return true;
}

template <class T>
void MatchList<T>::addToMatchList(matchlist_ptr_t<T> &matchList, const std::string &line) {
	gbshutdownLogicError();
}

template <>
void MatchList<std::string>::addToMatchList(matchlist_ptr_t<std::string> &matchList, const std::string &line) {
	matchList->emplace_back(line);
}

template <class T>
matchlistconst_ptr_t<T> MatchList<T>::getMatchList() {
	return m_matchList;
}

template <class T>
void MatchList<T>::swapMatchList(matchlistconst_ptr_t<T> matchList) {
	std::atomic_store(&m_matchList, matchList);
}

// explicit instantiations
template class MatchList<std::string>;
template class MatchList<uint32_t>;
