#include "DnsBlockList.h"
#include "Log.h"
#include "Conf.h"
#include "Loop.h"
#include "JobScheduler.h"
#include <fstream>
#include <sys/stat.h>
#include <atomic>

DnsBlockList g_dnsBlockList;

static const char s_dns_filename[] = "dnsblocklist.txt";

DnsBlockList::DnsBlockList()
	: m_filename(s_dns_filename)
	, m_loading(false)
	, m_dnsBlockList(new dnsblocklist_t)
	, m_lastModifiedTime(0) {
}

bool DnsBlockList::init() {
	log(LOG_INFO, "Initializing DnsBlockList with %s", m_filename);

	if (!g_loop.registerSleepCallback(60000, this, &reload, "DnsBlockList::reload", 0)) {
		log(LOG_WARN, "DnsBlockList:: Failed to register callback.");
		return false;
	}

	// we do a load here instead of using sleep callback with immediate set to true so
	// we don't rely on g_loop being up and running to use dnsblocklist
	load();

	return true;
}

void DnsBlockList::reload(int /*fd*/, void *state) {
	if (g_jobScheduler.submit(reload, nullptr, state, thread_type_config_load, 0)) {
		return;
	}

	// unable to submit job (load on main thread)
	reload(state);
}

void DnsBlockList::reload(void *state) {
	DnsBlockList *dnsBlockList = static_cast<DnsBlockList*>(state);

	// don't load multiple times at the same time
	if (dnsBlockList->m_loading.exchange(true)) {
		return;
	}

	dnsBlockList->load();
	dnsBlockList->m_loading = false;
}

bool DnsBlockList::load() {
	logTrace(g_conf.m_logTraceDnsBlockList, "Loading %s", m_filename);

	struct stat st;
	if (stat(m_filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "DnsBlockList::load: Unable to stat %s", m_filename);
		return false;
	}

	if (m_lastModifiedTime != 0 && m_lastModifiedTime == st.st_mtime) {
		// not modified. assume successful
		logTrace(g_conf.m_logTraceDnsBlockList, "Not modified");
		return true;
	}

	dnsblocklist_ptr_t tmpDnsBlockList(new dnsblocklist_t);

	std::ifstream file(m_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		tmpDnsBlockList->emplace_back(line);
		logTrace(g_conf.m_logTraceDnsBlockList, "Adding criteria '%s' to list", line.c_str());
	}

	swapDnsBlockList(tmpDnsBlockList);
	m_lastModifiedTime = st.st_mtime;

	logTrace(g_conf.m_logTraceDnsBlockList, "Loaded %s", m_filename);
	return true;
}

bool DnsBlockList::isDnsBlocked(const char *dns) {
	auto dnsBlockList = getDnsBlockList();

	for (auto const &dnsBlock : *dnsBlockList) {
		if (dnsBlock.front() == '*') {
			// wildcard
			if (strcasecmp(dnsBlock.c_str() + 1, dns + (strlen(dns) - (dnsBlock.length() - 1))) == 0) {
				logTrace(g_conf.m_logTraceDnsBlockList, "Dns block criteria %s matched dns '%s'", dnsBlock.c_str(), dns);
				return true;
			}
		} else {
			if (strcasecmp(dnsBlock.c_str(), dns) == 0) {
				logTrace(g_conf.m_logTraceDnsBlockList, "Dns block criteria %s matched dns '%s'", dnsBlock.c_str(), dns);
				return true;
			}
		}
	}

	return false;
}

dnsblocklistconst_ptr_t DnsBlockList::getDnsBlockList() {
	return m_dnsBlockList;
}

void DnsBlockList::swapDnsBlockList(dnsblocklistconst_ptr_t dnsBlockList) {
	std::atomic_store(&m_dnsBlockList, dnsBlockList);
}
