#ifndef DNS_INTERNALS_H_
#define DNS_INTERNALS_H_

// Structures used by Dns.cpp internall, but must be availabel for
// HashTableT.cpp for explicit template instantiation


// structure for TLD root name servers
struct TLDIPEntry {
	int32_t		expiry;
	int32_t		numTLDIPs;
	uint32_t	TLDIP[MAX_DNS_IPS];
};

// this is for storing callbacks. need to keep a queue of ppl waiting for
// the ip of the same hostname so we don't send dup requests to the same
// dns server.
class CallbackEntry {
public:
	void *m_state;
	void (*m_callback)(void *state, int32_t ip);
	struct DnsState *m_ds;
	//class CallbackEntry *m_next;
	// we can't use a data ptr because slots get moved around when one
	// is deleted.
	int64_t m_nextKey;
	// debug info
	int32_t m_listSize;
	int32_t m_listId;
};


#endif
