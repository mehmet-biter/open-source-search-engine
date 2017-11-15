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
#include "BlockList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "JobScheduler.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

BlockList::BlockList(const char *filename)
	: m_filename(filename)
	, m_loading(false)
	, m_blockList(new blocklist_t)
	, m_lastModifiedTime(0) {
}

bool BlockList::init() {
	log(LOG_INFO, "Initializing BlockList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, "BlockList::reload", 0)) {
		log(LOG_WARN, "BlockList:: Failed to register callback.");
		return false;
	}

	// we do a load here instead of using sleep callback with immediate set to true so
	// we don't rely on g_loop being up and running to use blocklist
	load();

	return true;
}

void BlockList::reload(int /*fd*/, void *state) {
	if (g_jobScheduler.submit(reload, nullptr, state, thread_type_config_load, 0)) {
		return;
	}

	// unable to submit job (load on main thread)
	reload(state);
}

void BlockList::reload(void *state) {
	BlockList *blockList = static_cast<BlockList*>(state);

	// don't load multiple times at the same time
	if (blockList->m_loading.exchange(true)) {
		return;
	}

	blockList->load();
	blockList->m_loading = false;
}

bool BlockList::load() {
	logTrace(g_conf.m_logTraceBlockList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "BlockList::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceBlockList, "%s not modified", m_filename);
		return true;
	}

	blocklist_ptr_t tmpBlockList(new blocklist_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		tmpBlockList->emplace_back(line);
		logTrace(g_conf.m_logTraceBlockList, "Adding criteria '%s' to list", line.c_str());
	}

	swapBlockList(tmpBlockList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceBlockList, "Loaded %s", m_filename);
	return true;
}

blocklistconst_ptr_t BlockList::getBlockList() {
	return m_blockList;
}

void BlockList::swapBlockList(blocklistconst_ptr_t blockList) {
	std::atomic_store(&m_blockList, blockList);
}

