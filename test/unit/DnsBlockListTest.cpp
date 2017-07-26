#include <gtest/gtest.h>
#include "DnsBlockList.h"

class TestDnsBlockList : public DnsBlockList {
public:
	TestDnsBlockList(const char *filename)
		: DnsBlockList() {
		m_filename = filename;
	}

	using DnsBlockList::load;
};

TEST(DnsBlockListTest, BlockList) {
	TestDnsBlockList dnsBlockList("blocklist/dns.txt");
	dnsBlockList.load();

	// full match
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("ns1.example.com"));
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("ns1.parked.example.com"));
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("abc.parked.example.com"));
	EXPECT_FALSE(dnsBlockList.isDnsBlocked("abcparked.example.com"));
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("badexample.com"));
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("abadexample.com"));
	EXPECT_TRUE(dnsBlockList.isDnsBlocked("averybadexample.com"));
}