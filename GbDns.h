#ifndef GB_GBDNS_H
#define GB_GBDNS_H

#include <vector>
#include <string>
#include <netinet/in.h>

namespace GbDns {
	struct DnsResponse {
		std::vector<in_addr_t> m_ips;
		std::vector<std::string> m_nameservers;
	};

	bool initialize();
	bool initializeSettings();

	void finalize();

	void getARecord(const char *hostname, void (*callback)(DnsResponse *response, void *state), void *state);
	void getNSRecord(const char *hostname, void (*callback)(DnsResponse *response, void *state), void *state);

	void makeCallbacks();
};


#endif //GB_GBDNS_H
