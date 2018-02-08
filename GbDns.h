#ifndef GB_GBDNS_H
#define GB_GBDNS_H

#include <vector>
#include <string>
#include <netinet/in.h>

namespace GbDns {
	struct DnsResponse {
		DnsResponse();

		std::vector<in_addr_t> m_ips;
		std::vector<std::string> m_nameservers;
		int m_errno;
	};

	bool initialize();
	bool initializeSettings();

	void finalize();

	bool getARecord(const char *hostname, size_t hostnameLen, void (*callback)(DnsResponse *response, void *state), void *state, GbDns::DnsResponse *response);
	void getNSRecord(const char *hostname, size_t hostnameLen, void (*callback)(DnsResponse *response, void *state), void *state);

	void makeCallbacks();
};


#endif //GB_GBDNS_H
