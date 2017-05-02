// Matt Wells, Copyright Nov 2000

// . every host (aka server) has a unique positive 32 bit hostId
// . to get the groupId of a hostId we must reverse the bits and then
//   AND the result with g_hostdb.m_groupMask
// . host record is about 32 bytes 
// . we keep a list of all hosts in the conf file
// . this new model is much easier to work with
// . format of hosts in the xml conf file
// . <host0> <ip> 1.2.3.4 </> <port> 1234 </>

#ifndef GB_HOSTDB_H
#define GB_HOSTDB_H

#include "types.h"
#include "rdbid_t.h"
#include "Sanity.h"
#include "GbMutex.h"
#include <atomic>

/// @note ALC there used to be a sync host functionality that was removed
/// in commit 08e8eeb2a53b41763b5d7f97a0b953bebb04517a because it wasn't working

// for the Host::m_flags
#define PFLAG_HASSPIDERS     0x01
#define PFLAG_MERGING        0x02
#define PFLAG_DUMPING        0x04
// these two flags are used by DailyMerge.cpp to sync the daily merge
// between all the hosts in the cluster
#define PFLAG_MERGEMODE0     0x08
#define PFLAG_MERGEMODE0OR6  0x10
#define PFLAG_REBALANCING    0x20
#define PFLAG_FOREIGNRECS    0x40
#define PFLAG_RECOVERYMODE   0x80

#define HT_GRUNT   0x01
#define HT_SPARE   0x02
#define HT_PROXY   0x04
#define HT_QCPROXY 0x08
#define HT_SCPROXY 0x10

int32_t *getLocalIps ( ) ;

class PingInfo {
public:
	int64_t m_unused0; //used to be a timestamp for clock synchronization
	int32_t m_hostId;
	int32_t m_unused2; //used for the m_loadAvg
	float m_unused3; //used to me m_percentMemUsed;
	float m_unused4; //used to be m_cpuUsage
	int32_t m_totalDocsIndexed;
	int32_t m_hostsConfCRC;
	float m_unused7; //used to be m_diskUsage
	int32_t m_flags;
	// some new stuff
	int32_t m_unused9;
	int32_t m_unused10;
	int32_t m_unused11;

	int32_t m_unused12;
	int32_t m_unused13;

	int16_t m_unused14;
	collnum_t m_dailyMergeCollnum;

	char m_gbVersionStr[21];
	char m_repairMode;
	uint8_t m_unused18;
};

class Host {
public:
	int32_t getInternalHttpPort()  const { return m_httpPort; }
	int32_t getInternalHttpsPort() const { return m_httpsPort; }
	int32_t getExternalHttpPort()  const { return m_httpPort; }
	int32_t getExternalHttpsPort() const { return m_httpsPort; }

	int32_t           m_hostId ;

	// shards and groups are basically the same, but let's keep it simple.
	// since we use m_map in Hostdb.cpp to map the bits of a key to
	// the shard # now (not a groupId anymore, since groupId does not
	// work for non binary powers of shards)
	uint32_t m_shardNum;

	uint32_t  m_ip ;        // used for internal communcation (udp)

	// what stripe are we in? i.e. what twin number are we?
	int32_t           m_stripe;

	// this ip is now the shotgun ip. the problem with an "external" ip
	// is that people internal to the network cannot usually see it!!
	// so now this is just a secondary regular ip address for servers with
	// dual gigabit ethernet ports. make performance much better.
	uint32_t  m_ipShotgun;  

	uint16_t m_port ;          // Mattster Protocol (MP) UDP port

	int32_t           m_ping;
	int32_t           m_pingShotgun;
	int32_t           m_pingMax;
	// have we ever got a ping reply from him?
	bool           m_gotPingReply;

	// last time g_hostdb.ping(i) was called for this host in milliseconds.
	int64_t      m_lastPing;

	// . first time we sent an unanswered ping request to this host
	// . used so we can determine when to send an email alert
	int64_t      m_startTime;
	// is a ping in progress for this host?
	bool           m_inProgress1;
	// shotgun
	bool           m_inProgress2;
	int64_t      m_numPingReplies;

	// send to eth 0 or 1 when sending to this host?
	char           m_preferEth;

	// . this is used for sending email alerts to admin about dead hosts
	// .  0 means we can send an email for this host if he goes dead on us
	// . +1 means we already sent an email for him since he was down and 
	//      he hasn't come back up since
	// . -1 means he went down gracefully so no alert is needed
	// . -2 means he went down while g_conf.m_sendEmailAlert was false
	// . -3 means another host was responsible for sending to him, not us
	// . -4 means we are currently in progress sending an email for him
	// . -5 means he went down before host we alerted admin about revived
	int32_t           m_emailCode;

	// we now include the working dir in the hosts.conf file
	// so main.cpp can do gb --install and gb --allstart
	char           m_dir[128];
	char           m_mergeDir[128];
	char           m_mergeLockDir[128];

	char           m_hostname[16];

	// the secondary ethernet interface hostname
	char           m_hostname2[16];

	// client port
	uint16_t m_dnsClientPort;

	// was host in gk0 cluster and retired because its twin got
	// ssds, so it was no longer really needed.
	bool           m_retired;
	// this toggles between 0 and 1 for alternating packet sends to
	// eth0 and eth1 of this host
	char           m_shotgunBit;
	// how many total error replies we got from this host
	int32_t           m_errorReplies;

	// now include a note for every host
	char           m_note[128];

	bool           m_isProxy;

	// used for compressing spidered docs before sending back to local
	// network to save on local loop bandwidth costs
	char           m_type;

	bool isProxy() { return (m_type == HT_PROXY); }
	bool isGrunt() { return (m_type == HT_GRUNT); }

	// for m_type == HT_QCPROXY, we forward the query to the regular proxy
	// at this Ip:Port. we should receive a compressed 0xfd reply and
	// we uncompress it and return it to the browser.
	int32_t           m_forwardIp;
	int16_t          m_forwardPort;

	std::atomic<int64_t>     m_dgramsTo;
	std::atomic<int64_t>     m_dgramsFrom;

	std::atomic<int32_t>     m_totalResends; //how many UDP packets has been resent
	std::atomic<int32_t>     m_etryagains;   //how many times a request got an ETRYAGAIN

	char           m_repairMode;

	// for timing how long the msg39 takes from this host
	int32_t           m_splitsDone;
	int64_t      m_splitTimes;

	// . used by Parms.cpp for broadcasting parm change requests
	// . each parm change request has an id
	// . this let's us know which id is in progress and what the last
	//   id completed was
	int32_t m_currentParmIdInProgress;
	int32_t m_lastParmIdCompleted;
	class ParmNode *m_currentNodePtr;
	int32_t m_lastTryError;
	int32_t m_lastTryTime;

	bool m_spiderEnabled;
	bool m_queryEnabled;

	PingInfo m_pingInfo;//RequestBuf;

	void updateLastResponseReceiveTimestamp(uint64_t t) { m_lastResponseReceiveTimestamp=t; }
	void updateLastRequestSendTimestamp(uint64_t t) { m_lastRequestSendTimestamp=t; }
	uint64_t getLastResponseReceiveTimestamp() const { return m_lastResponseReceiveTimestamp; }
	uint64_t getLastRequestSendTimestamp() const { return m_lastRequestSendTimestamp; }

private:
	uint64_t m_lastResponseReceiveTimestamp;
	std::atomic<uint64_t> m_lastRequestSendTimestamp;

	friend class Hostdb;
	uint16_t m_httpPort ;      // http port
	uint16_t m_httpsPort;
};

#include "max_hosts.h"
#define MAX_SPARES 64
#define MAX_PROXIES 6

// for table for mapping key to a groupid
#define MAX_KSLOTS 8192

class Hostdb {

 public:

	// . set up our Hostdb
	// . sets itself from g_conf (our configuration class)
	// . returns false on fatal error
	// . gets filename from Conf.h class
	bool init(int32_t hostId, char *netname=NULL, bool proxyHost=false, bool useTempCluster=false, const char *cwd=NULL);

	// if config changes this *should* change
	int32_t getCRC();

	const char *getNetName ( );

	Hostdb();
	~Hostdb();
	void reset();

	uint32_t  getMyIp         ( ) { return m_myIp; }
	uint16_t getMyPort       ( ) { return m_myPort; }
	int32_t           getMyHostId     ( ) { return m_hostId; }
	uint32_t  getLoopbackIp   ( ) { return m_loopbackIp; }
	Host          *getMyHost       ( ) { return m_myHost; }
	bool           amProxy         ( ) { return m_myHost->isProxy(); }
	Host          *getMyShard      ( ) { return m_myShard; }
	int32_t getMyShardNum ( ) { return m_myHost->m_shardNum; }

	// we consider the host dead if we didn't get a ping reply back
	// after 10 seconds
	bool  isDead ( int32_t hostId ) ;

	bool  isDead(const Host *h);

	bool hasDeadHost ( );
	int getNumHostsDead();

	int64_t getNumGlobalRecs ( );

	bool isShardDead ( int32_t shardNum ) ;

	Host *getLeastLoadedInShard ( uint32_t shardNum , char niceness );
	int32_t getHostIdWithSpideringEnabled ( uint32_t shardNum );
	Host *getHostWithSpideringEnabled ( uint32_t shardNum );

	// in the entire cluster. return host #0 if its alive, otherwise
	// host #1, etc.
	Host *getFirstAliveHost ();

	// . returns false if blocks and will call your callback later
	// . returns true if doesn't block
	// . sets errno on error
	// . used to get array of hosts in 1 group usually for transmitting to
	// . returned group includes all hosts in host "hostId"'s group
	// . RdbList will be populated with the hosts in that group
	// . we do not create an RdbList, you must do that
	// . callback passes your RdbList back to you
	Host *getShard ( uint32_t shardNum , int32_t *numHosts = NULL ) {
		if(shardNum>=(unsigned)m_numShards) gbshutdownLogicError();
		if ( numHosts ) *numHosts = m_numHostsPerShard;
		return &m_hosts[shardNum * m_numHostsPerShard]; 
	}

	// get the host that has this path/ip
	Host *getHost2 ( const char *cwd , int32_t *localIps ) ;
	Host *getProxy2 ( const char *cwd , int32_t *localIps ) ;

	// . like above but just gets one host
	Host *getHost ( int32_t hostId ) {
		if ( hostId < 0 ) { gbshutdownAbort(true); }
		return m_hostPtrs[hostId]; 
	}

	Host *getSpare ( int32_t spareId ) {
		return m_spareHosts[spareId]; }

	Host *getProxy ( int32_t proxyId ) {
		return m_proxyHosts[proxyId]; }

	int32_t getNumHosts() { return m_numHosts; }
	int32_t getNumProxy() { return m_numProxyHosts; }
	int32_t getNumProxies() { return m_numProxyHosts; }
	int32_t getNumGrunts() { return m_numHosts; }

	// how many of the hosts are non-dead?
	int32_t  getNumHostsAlive ( ) { return m_numHostsAlive; }
	int32_t  getNumProxyAlive ( ) { return m_numProxyAlive; }
	int32_t  getNumShards () { return m_numShards; }
	int32_t  getNumIndexSplits() { return m_indexSplits; }

	// how many hosts in this group?
	int32_t  getNumHostsPerShard ( ) { return m_numHostsPerShard; }

	// goes with Host::m_stripe
	int32_t  getNumStripes ( ) { 
		// BR 20160316: Make sure noquery hosts are not used when dividing
		// docIds for querying (Msg39)
		return m_numStripeHostsPerShard; 
	}

	// hash the hosts into the hash tables for lookup
	bool  hashHosts();
	bool  hashHost ( bool udp , Host *h , uint32_t ip , uint16_t port ) ;
	int32_t  getHostId        ( uint32_t ip , uint16_t port ) ;
	Host *getHostByIp      ( uint32_t ip ) ;
	Host *getUdpHost       ( uint32_t ip , uint16_t port ) ;
	Host *getTcpHost       ( uint32_t ip , uint16_t port ) ;
	bool isIpInNetwork     ( uint32_t ip ) ;
	Host *getHostFromTable ( bool udp , uint32_t ip , uint16_t port ) ;

	// sets the note for a host
	bool setNote ( int32_t hostId, const char *note, int32_t noteLen );
	bool setSpareNote ( int32_t spareId, const char *note, int32_t noteLen );
	
	// replace a host with a spare
	bool replaceHost ( int32_t origHostId, int32_t spareHostId );

	// write a hosts.conf file
	bool saveHostsConf ( );

	int32_t getBestIp(const Host *h);
	
	Host *getBestSpiderCompressionProxy ( int32_t *key ) ;

	// only used by 'gb scale <hostdb.conf>' cmd for now
	void resetPortTables ();

	// returns best IP to use for "h" which is a host in hosts2.conf
	int32_t getBestHosts2IP(const Host *h);

	void updatePingInfo(Host *h, const PingInfo &pi);

	// our host's info used by Udp* classes for internal communication
	uint32_t  m_myIp;
	uint32_t  m_myIpShotgun;
	uint16_t m_myPort;
	Host          *m_myHost;
	Host          *m_myShard;

	// the loopback ip (127.0.0.1)
	uint32_t  m_loopbackIp;

	// . the hosts are stored from g_conf in xml to here
	// . m_hosts[i] is the ith Host entry
	Host  *m_hosts;
	int32_t   m_numHosts;
	int32_t   m_numHostsAlive;

	int32_t   m_allocSize;

	// . this maps a hostId to the appropriate host!
	// . we can't use m_hosts[i] because we sort it by groupId for getGroup
	Host  *m_hostPtrs[MAX_HOSTS];

	// we must have the same number of hosts in each group
	int32_t   m_numHostsPerShard;

	// Number of hosts per shared not counting noquery hosts
	int32_t	m_numStripeHostsPerShard;

	// store the file in m_buf
	char m_buf [MAX_HOSTS * 128];
	int32_t m_bufSize;

	// this maps shard # to the array of hosts in that shard
	Host *m_shards[MAX_HOSTS];

	// the hash table of the ips in hosts.conf
	int32_t *m_ips;
	int32_t  m_numIps;

	// . our group info
	int32_t          m_hostId;      // our hostId
	int32_t          m_numShards;
	char          m_dir[256];
	char          m_httpRootDir[256];
	char          m_logFilename[256];

	int32_t          m_indexSplits; 

	char          m_netName[32];

	// spare hosts list
	Host *m_spareHosts[MAX_SPARES];
	int32_t  m_numSpareHosts;

	// proxy host list
	Host *m_proxyHosts[MAX_PROXIES];
	int32_t  m_numProxyHosts;
	int32_t  m_numProxyAlive;

	int32_t  m_numTotalHosts;

	bool m_initialized;

	bool createHostsConf( const char *cwd );
	bool m_created;

	int32_t m_crc;
	bool m_crcValid;

	bool  m_useTmpCluster;

	uint32_t getShardNum(rdbid_t rdbId, const void *key) const;

	uint32_t getShardNumFromDocId (int64_t d) const;

	// assume to be for posdb here
	uint32_t getShardNumByTermId(const void *key) const;

	uint32_t m_map[MAX_KSLOTS];

private:
	GbMutex m_mtxPinginfo; //protects the pinginfo in the hosts
};

extern class Hostdb g_hostdb;

extern Host     *g_listHosts [ MAX_HOSTS * 4 ];
extern uint32_t  g_listIps   [ MAX_HOSTS * 4 ];
extern uint16_t  g_listPorts [ MAX_HOSTS * 4 ];
extern int32_t      g_listNumTotal;

static inline uint32_t getShardNum(rdbid_t rdbId, const void *key) {
	return g_hostdb.getShardNum ( rdbId , key );
}

inline uint32_t getMyShardNum ( ) { 
	return g_hostdb.m_myHost->m_shardNum; 
}

inline int32_t getMyHostId() {
	return g_hostdb.m_myHost->m_hostId;
}

inline uint32_t getShardNumFromDocId ( int64_t d ) {
	return g_hostdb.getShardNumFromDocId ( d );
}

#endif // GB_HOSTDB_H
