// Copyright Matt Wells Nov 2000

// . uses UDP only (we'll do the TCP part later)
// . derived from UdpServer 
// . always sends lookup to fastest dns
// . periodically pings other servers
// . if our primary dns goes down it's speed will be changed in hostmap
//   and we'll have a new primary
// . if a request times out we try the next dns

#ifndef GB_DNS_H
#define GB_DNS_H

#include "UdpServer.h"
#include "DnsProtocol.h"
#include "RdbCache.h"

#define MAX_DNS_IPS 32


struct DnsState;
class Host;


class Dns { 

 public:

	Dns();

	// reset the udp server and rdb cache
	void reset();

	// . we create our own udpServer in here since we can't share that
	//   because of the protocol differences
	// . read our dns servers from the conf file
	bool init(uint16_t clientPort);

	// . check errno to on return or on callback to ensure no error
	// . we set ip to 0 if not found
	// . returns -1 and sets errno on error
	// . returns 0 if transaction blocked, 1 if completed
	bool getIp ( const char  *hostname,
		     int32_t   hostnameLen,
		     int32_t  *ip,
		     void  *state,
		     void (*callback)(void *state, int32_t ip))
	{
		return getIp(hostname,hostnameLen,ip,state,callback,
		             NULL, 60, false);
	}

	const UdpServer& getUdpServer() const { return m_udpServer; }
	UdpServer&       getUdpServer()       { return m_udpServer; }
	RdbCache *getCache () { return &m_rdbCache; }
	RdbCache *getCacheLocal () { return &m_rdbCacheLocal; }

	// returns true if in cache, and sets *ip
	bool isInCache(key96_t key, int32_t *ip);

	// add this hostnamekey/ip pair to the cache
	void addToCache(key96_t hostnameKey, int32_t ip, int32_t ttl = -1);

	// is it in the /etc/hosts file?
	bool isInFile(key96_t key, int32_t *ip);

	static key96_t getKey(const char *hostname, int32_t hostnameLen);

	Host *getIPLookupHost(key96_t key);

private:
	static void gotIpWrapper(void *state, UdpSlot *slot);
	static void gotIpOfDNSWrapper(void *state , int32_t ip);
	static void returnIp(DnsState *ds, int32_t ip);

	bool loadFile();

	bool sendToNextDNS(struct DnsState *ds);

	bool getIpOfDNS(DnsState *ds);

	// . pull the hostname out of a dns reply packet's query resource rec.
	bool extractHostname(const char *dgram, const char *record, char *hostname);

	bool getIp ( const char  *hostname,
		     int32_t   hostnameLen,
		     int32_t  *ip,
		     void  *state,
		     void (*callback)(void *state, int32_t ip),
		     DnsState *ds,
		     int32_t   timeout,
		     bool   dnsLookup);

	// . returns the ip
	// . called to parse the ip out of the reply in "slot"
	// . must be public so gotIpWrapper() can call it
	// . also update the timestamps in our private hostmap
	// . returns -1 on error
	// . returns 0 if ip does not exist
	int32_t gotIp(UdpSlot *slot, struct DnsState *dnsState);

	// . we have our own udp server
	// . it contains our HostMap and DnsProtocol ptrs
	// . keep public so gotIpWrapper() can use it to destroy the slot
	UdpServer m_udpServer;

	// . key is a hash of hostname
	// . record/slot contains a 4 byte ip entry (if in key is in cache)
	// . cache is shared with other dbs
	RdbCache  m_rdbCache;
	RdbCache  m_rdbCacheLocal;

	DnsProtocol m_proto;

	int16_t  m_dnsClientPort;

	// /etc/hosts in hashed into this table
	int32_t   *m_ips;
	key96_t  *m_keys;
	int32_t    m_numSlots;
};

//This stores the ip's for the machine where 
//hash96(hostname) % g_hostdb.m_numHosts = cluster(group)
extern class Dns g_dns;

#endif // GB_DNS_H
