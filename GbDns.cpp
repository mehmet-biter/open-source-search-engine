#include "GbDns.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "Conf.h"
#include "Mem.h"
#include "third-party/c-ares/ares.h"
#include "ip.h"
#include "GbThreadQueue.h"
#include "FxCache.h"
#include <arpa/nameser.h>
#include <netdb.h>
#include <vector>
#include <string>
#include <queue>

static ares_channel s_channel;
static pthread_t s_thread;
static bool s_stop = false;
static bool s_finalized = false;

static pthread_cond_t s_channelCond = PTHREAD_COND_INITIALIZER;
static GbMutex s_channelMtx;

static std::queue<struct DnsItem*> s_callbackQueue;
static GbMutex s_callbackQueueMtx;

static GbThreadQueue s_requestQueue;
static FxCache<std::string, GbDns::DnsResponse> s_cache;

static void a_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen);
static void ns_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen);

template<typename T>
class AresList {
public:
	AresList();
	~AresList();

	T* getHead();
	void append(T *item);

private:
	T *m_head;
};

template<typename T> AresList<T>::AresList()
	: m_head(NULL) {
}

template<typename T> AresList<T>::~AresList() {
	while (m_head) {
		T *node = m_head;
		m_head = m_head->next;
		mfree(node, sizeof(T), "ares-item");
	}
}

template<typename T> T* AresList<T>::getHead() {
	return m_head;
}

template<typename T> void AresList<T>::append(T *node) {
	node->next = NULL;

	if (m_head) {
		T *last = m_head;
		while (last->next) {
			last = last->next;
		}
		last->next = node;
	} else {
		m_head = node;
	}
}

static void* processing_thread(void *args) {
	while (!s_stop) {
		fd_set read_fds, write_fds;
		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);

		int nfds;

		{
			ScopedLock sl(s_channelMtx);
			nfds = ares_fds(s_channel, &read_fds, &write_fds);
			if (nfds == 0) {
				// wait until new request comes in
				pthread_cond_wait(&s_channelCond, &s_channelMtx.mtx);
				continue;
			}
		}

		timeval *tvp = NULL;
		timeval tv;

		{
			ScopedLock sl(s_channelMtx);
			tvp = ares_timeout(s_channel, NULL, &tv);
		}

		int count = select(nfds, &read_fds, &write_fds, NULL, tvp);
		int status = errno;
		if (count < 0 && status != EINVAL) {
			logError("select fail: %d", status);
			continue;
		}

		{
			ScopedLock sl(s_channelMtx);
			ares_process(s_channel, &read_fds, &write_fds);
		}
	}

	return 0;
}

struct DnsItem {
	enum RequestType {
		request_type_a,
		request_type_ns
	};

	DnsItem(RequestType reqType, const char *hostname, size_t hostnameLen,
	        void (*callback)(GbDns::DnsResponse *response, void *state), void *state)
		: m_reqType(reqType)
		, m_hostname(hostname, hostnameLen)
		, m_callback(callback)
		, m_state(state)
		, m_response() {
	}

	RequestType m_reqType;
	std::string m_hostname;
	void (*m_callback)(GbDns::DnsResponse *response, void *state);
	void *m_state;

	GbDns::DnsResponse m_response;
};

static void processRequest(void *item) {
	DnsItem *dnsItem = static_cast<DnsItem*>(item);

	ares_callback callback;
	int type;

	switch (dnsItem->m_reqType) {
		case DnsItem::request_type_a:
			callback = a_callback;
			type = T_A;
			break;
		case DnsItem::request_type_ns:
			callback = ns_callback;
			type = T_NS;
			break;
	}

	ScopedLock sl(s_channelMtx);
	ares_query(s_channel, dnsItem->m_hostname.c_str(), C_IN, type, callback, dnsItem);
	pthread_cond_signal(&s_channelCond);
}

bool GbDns::initializeSettings() {
	log(LOG_INFO, "dns: Initializing settings");

	// setup dns servers
	AresList<ares_addr_port_node> servers;
	for (int i = 0; i < g_conf.m_numDns; ++i) {
		ares_addr_port_node *server = (ares_addr_port_node*)mmalloc(sizeof(ares_addr_port_node), "ares-server");
		if (server == NULL) {
			logError("Unable allocate ares server");
			return false;
		}

		server->addr.addr4.s_addr = g_conf.m_dnsIps[i];
		server->family = AF_INET;
		server->udp_port = g_conf.m_dnsPorts[i];

		servers.append(server);
	}

	{
		ScopedLock sl(s_channelMtx);
		if (ares_set_servers_ports(s_channel, servers.getHead()) != ARES_SUCCESS) {
			logError("Unable to set ares server settings");
			return false;
		}
	}

	s_cache.configure(g_conf.m_dnsCacheMaxAge*1000, g_conf.m_dnsCacheSize, g_conf.m_logTraceDnsCache, "dns cache");

	return true;
}

bool GbDns::initialize() {
	log(LOG_INFO, "dns: Initializing library");

	if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
		logError("Unable to init ares library");
		return false;
	}

	if (ares_init(&s_channel) != ARES_SUCCESS) {
		logError("Unable to init ares channel");
		return false;
	}

	int optmask = ARES_OPT_FLAGS;
	ares_options options;
	memset(&options, 0, sizeof(options));

	// don't default init servers (use null values from options)
	optmask |= ARES_OPT_SERVERS;

	// lookup from hostfile & dns servers
	options.lookups = strdup("fb");
	optmask |= ARES_OPT_LOOKUPS;

	// round-robin selection of nameservers
	optmask |= ARES_OPT_ROTATE;

	if (ares_init_options(&s_channel, &options, optmask) != ARES_SUCCESS) {
		logError("Unable to init ares options");
		return false;
	}

	if (!initializeSettings()) {
		return false;
	}

	// create processing thread
	if (pthread_create(&s_thread, nullptr, processing_thread, nullptr) != 0) {
		logError("Unable to create ares processing thread");
		return false;
	}

	if (!s_requestQueue.initialize(processRequest, "process-dns")) {
		logError("Unable to initialize request queue");
		return false;
	}

	return true;
}

void GbDns::finalize() {
	if (s_finalized) {
		return;
	}

	log(LOG_INFO, "dns: Finalizing library");

	s_finalized = true;
	s_stop = true;

	pthread_cond_broadcast(&s_channelCond);
	pthread_join(s_thread, nullptr);

	ares_destroy(s_channel);
	ares_library_cleanup();

	s_requestQueue.finalize();
}

GbDns::DnsResponse::DnsResponse()
	: m_ips()
	, m_nameservers()
	, m_errno(0) {
}

static int convert_ares_errorno(int ares_errno) {
	switch (ares_errno) {
		// Server error codes (ARES_ENODATA indicates no relevant answer)
		case ARES_ENODATA:
		case ARES_ENOTFOUND:
			return EDNSNOTFOUND;

		case ARES_EFORMERR:
		case ARES_ENOTIMP:
			return EDNSBADREQUEST;

		case ARES_ESERVFAIL:
			return EDNSSERVFAIL;

		case ARES_EREFUSED:
			return EDNSREFUSED;

		// Locally generated error codes
		case ARES_EBADQUERY:
		case ARES_EBADNAME:
		case ARES_EBADFAMILY:
			return EDNSBADREQUEST;

		case ARES_EBADRESP:
		case ARES_EBADSTR:
			return EDNSBADRESPONSE;

		case ARES_ECONNREFUSED:
		case ARES_ETIMEOUT:
			return EDNSTIMEDOUT;

//		case ARES_EOF:
//			break;
//		case ARES_EFILE:
//			break;

		case ARES_ENOMEM:
			return ENOMEM;

		case ARES_EDESTRUCTION:
			return ESHUTTINGDOWN;

		// ares_getnameinfo error codes
		case ARES_EBADFLAGS:
			return EDNSBADREQUEST;

		// ares_getaddrinfo error codes
		case ARES_ENONAME:
		case ARES_EBADHINTS:
			return EDNSBADREQUEST;

		// Uninitialized library error code
//		case ARES_ENOTINITIALIZED:
//			return EBADENGINEER;

		// ares_library_init error codes
//		case ARES_ELOADIPHLPAPI:
//		case ARES_EADDRGETNETWORKPARAMS:
//			return EBADENGINEER;

		// More error codes
//		case ARES_ECANCELLED:
//			return ECANCELLED;
	}

	// defaults to no error code
	return 0;
}

static void addToCallbackQueue(DnsItem *item, bool addToCache=true) {
	if (addToCache) {
		if (item->m_reqType == DnsItem::request_type_a) {
			s_cache.insert(item->m_hostname, item->m_response);
		} else if (item->m_reqType == DnsItem::request_type_ns) {
			if (!item->m_response.m_nameservers.empty()) {
				GbDns::DnsResponse response;
				if (s_cache.lookup(item->m_hostname, &response)) {
					// merge response
					response.m_nameservers = item->m_response.m_nameservers;
					s_cache.insert(item->m_hostname, response);
				}
			}
		}
	}

	ScopedLock sl(s_callbackQueueMtx);
	logTrace(g_conf.m_logTraceDns, "adding to callback queue item=%p", item);
	s_callbackQueue.push(item);
}

static void a_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen) {
	logTrace(g_conf.m_logTraceDns, "BEGIN");

	DnsItem *item = static_cast<DnsItem*>(arg);

	if (status != ARES_SUCCESS) {
		logTrace(g_conf.m_logTraceDns, "ares_error=%d(%s)", status, ares_strerror(status));

		if (abuf == NULL) {
			logTrace(g_conf.m_logTraceDns, "no abuf returned");

			item->m_response.m_errno = convert_ares_errorno(status);
			addToCallbackQueue(item);

			logTrace(g_conf.m_logTraceDns, "END");
			return;
		}
	}

	hostent *host = nullptr;
	int naddrttls = 5;
	ares_addrttl addrttls[naddrttls];
	status = ares_parse_a_reply_ext(abuf, alen, &host, addrttls, &naddrttls);
	if (status == ARES_SUCCESS) {
		for (int i = 0; i < naddrttls; ++i) {
			char ipbuf[16];
			logTrace(g_conf.m_logTraceDns, "ip=%s ttl=%d", iptoa(addrttls[i].ipaddr.s_addr, ipbuf), addrttls[i].ttl);
			item->m_response.m_ips.push_back(addrttls[i].ipaddr.s_addr);
		}

		for (int i = 0; host->h_aliases[i] != NULL; ++i) {
			logTrace(g_conf.m_logTraceDns, "ns[%d]='%s'", i, host->h_aliases[i]);
			item->m_response.m_nameservers.push_back(host->h_aliases[i]);
		}

		ares_free_hostent(host);
	} else {
		logTrace(g_conf.m_logTraceDns, "ares_error=%d(%s)", status, ares_strerror(status));

		item->m_response.m_errno = convert_ares_errorno(status);
	}

	addToCallbackQueue(item);

	logTrace(g_conf.m_logTraceDns, "END");
}

void GbDns::getARecord(const char *hostname, size_t hostnameLen, void (*callback)(GbDns::DnsResponse *response, void *state), void *state) {
	logTrace(g_conf.m_logTraceDns, "BEGIN hostname='%.*s'", static_cast<int>(hostnameLen), hostname);

	DnsItem *item = new DnsItem(DnsItem::request_type_a, hostname, hostnameLen, callback, state);

	// check if hostname is ip
	if (is_digit(item->m_hostname[0])) {
		in_addr addr;

		// hostname is ip. skip dns lookup
		if (inet_pton(AF_INET, item->m_hostname.c_str(), &addr) == 1) {
			item->m_response.m_ips.push_back(addr.s_addr);
			addToCallbackQueue(item, false);

			logTrace(g_conf.m_logTraceDns, "END. hostname is IP addr");
			return;
		}
	}

	if (g_conf.m_useEtcHosts) {
		ScopedLock sl(s_channelMtx);
		hostent *host = nullptr;
		if (ares_gethostbyname_file(s_channel, item->m_hostname.c_str(), AF_INET, &host) == ARES_SUCCESS) {
			item->m_response.m_ips.push_back(((in_addr*)host->h_addr_list[0])->s_addr);
			addToCallbackQueue(item, false);

			ares_free_hostent(host);

			logTrace(g_conf.m_logTraceDns, "END. hostname found in /etc/host");
			return;
		}
	}

	// check cache
	if (s_cache.lookup(item->m_hostname, &(item->m_response))) {
		addToCallbackQueue(item, false);

		logTrace(g_conf.m_logTraceDns, "END. hostname found in cache");
		return;
	}

	s_requestQueue.addItem(item);

	logTrace(g_conf.m_logTraceDns, "END");
}

static void ns_callback(void *arg, int status, int timeouts, unsigned char *abuf, int alen) {
	logTrace(g_conf.m_logTraceDns, "BEGIN");
	DnsItem *item = static_cast<DnsItem*>(arg);

	if (status != ARES_SUCCESS) {
		logTrace(g_conf.m_logTraceDns, "ares_error='%s'", ares_strerror(status));

		if (abuf == NULL) {
			item->m_response.m_errno = convert_ares_errorno(status);
			addToCallbackQueue(item);

			logTrace(g_conf.m_logTraceDns, "END");
			return;
		}
	}

	hostent *host = nullptr;
	status = ares_parse_ns_reply(abuf, alen, &host);
	if (status == ARES_SUCCESS) {
		for (int i = 0; host->h_aliases[i] != NULL; ++i) {
			logTrace(g_conf.m_logTraceDns, "ns[%d]='%s'", i, host->h_aliases[i]);
			item->m_response.m_nameservers.push_back(host->h_aliases[i]);
		}

		ares_free_hostent(host);
	}

	if (status != ARES_SUCCESS) {
		logTrace(g_conf.m_logTraceDns, "ares_error=%d(%s)", status, ares_strerror(status));
		if (status != ARES_EDESTRUCTION) {
			item->m_response.m_errno = convert_ares_errorno(status);
			addToCallbackQueue(item);
		} else {
			delete item;
		}

		logTrace(g_conf.m_logTraceDns, "END");
		return;
	}

	addToCallbackQueue(item);
	logTrace(g_conf.m_logTraceDns, "END");
}

void GbDns::getNSRecord(const char *hostname, size_t hostnameLen, void (*callback)(GbDns::DnsResponse *response, void *state), void *state) {
	logTrace(g_conf.m_logTraceDns, "BEGIN hostname='%.*s'", static_cast<int>(hostnameLen), hostname);
	DnsItem *item = new DnsItem(DnsItem::request_type_ns, hostname, hostnameLen, callback, state);

	s_requestQueue.addItem(item);

	logTrace(g_conf.m_logTraceDns, "END");
}

void GbDns::makeCallbacks() {
	s_callbackQueueMtx.lock();
	while (!s_callbackQueue.empty()) {
		DnsItem *item = s_callbackQueue.front();
		s_callbackQueueMtx.unlock();

		logTrace(g_conf.m_logTraceDns, "processing callback queue item=%p", item);

		item->m_callback(&(item->m_response), item->m_state);

		logTrace(g_conf.m_logTraceDns, "removing callback queue item=%p", item);
		delete item;

		s_callbackQueueMtx.lock();
		s_callbackQueue.pop();
	}
	s_callbackQueueMtx.unlock();
}
