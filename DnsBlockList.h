#ifndef GB_DNSBLOCKLIST_H
#define GB_DNSBLOCKLIST_H

#include <memory>
#include <vector>
#include <string>
#include <atomic>

typedef std::vector<std::string> dnsblocklist_t;
typedef std::shared_ptr<dnsblocklist_t> dnsblocklist_ptr_t;
typedef std::shared_ptr<const dnsblocklist_t> dnsblocklistconst_ptr_t;

class DnsBlockList {
public:
	DnsBlockList();

	bool init();

	bool isDnsBlocked(const char *dns);

	static void reload(int /*fd*/, void *state);
	static void reload(void *state);

protected:
	bool load();

	const char *m_filename;

private:
	dnsblocklistconst_ptr_t getDnsBlockList();
	void swapDnsBlockList(dnsblocklistconst_ptr_t dnsBlockList);

	std::atomic_bool m_loading;
	dnsblocklistconst_ptr_t m_dnsBlockList;

	time_t m_lastModifiedTime;
};

extern DnsBlockList g_dnsBlockList;

#endif //GB_DNSBLOCKLIST_H
