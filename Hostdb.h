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

#include <sys/ioctl.h>            // ioctl() - get our ip address from a socket
#include <net/if.h>               // for struct ifreq passed to ioctl()    
#include "Xml.h" // host file in xml

enum {
	ME_IOERR = 1,
	ME_100MBPS,
	ME_UNKNWN
};

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
#define PFLAG_OUTOFSYNC      0x100

#define HT_GRUNT   0x01
#define HT_SPARE   0x02
#define HT_PROXY   0x04
#define HT_QCPROXY 0x08
#define HT_SCPROXY 0x10
#define HT_ALL_PROXIES (HT_PROXY|HT_QCPROXY|HT_SCPROXY)

int32_t *getLocalIps ( ) ;

class PingInfo {
 public:
	// m_lastPing MUST be on top for now...
	//int64_t m_lastPing;
	// this timestamp MUST be on top because we set requestSize to 8
	// and treat it like an old 8-byte ping in PingServer.cpp
	int64_t m_localHostTimeMS;
	int32_t m_hostId;
	int32_t m_loadAvg;
	float m_percentMemUsed;
	float m_cpuUsage;
	int32_t m_totalDocsIndexed;
	int32_t m_slowDiskReads;
	int32_t m_hostsConfCRC;
	float m_diskUsage;
	int32_t m_flags;
	// some new stuff
	int32_t m_numCorruptDiskReads;
	int32_t m_numOutOfMems;
	int32_t m_socketsClosedFromHittingLimit;

	int32_t m_totalResends;
	int32_t m_etryagains;

	int32_t m_udpSlotsInUseIncoming;
	int32_t m_tcpSocketsInUse;

	int16_t m_currentSpiders;
	collnum_t m_dailyMergeCollnum;
	int16_t m_hdtemps[4];
	char m_gbVersionStr[21];
	char m_repairMode;
	char m_kernelErrors;
	uint8_t m_recoveryLevel;
};

class Host {
public:

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
	uint16_t m_externalHttpPort;
	uint16_t m_externalHttpsPort;

	uint16_t m_port ;          // Mattster Protocol (MP) UDP port
	uint16_t m_httpPort ;      // http port
	uint16_t m_httpsPort;
	int32_t           m_ping;
	int32_t           m_pingShotgun;
	int32_t           m_pingMax;
	// have we ever got a ping reply from him?
	char           m_gotPingReply;
	double         m_loadAvg;
	// the first time we went OOM (out of mem, i.e. >= 99% mem used)
	int64_t      m_firstOOMTime;

	// did gb log system errors that were given in g_conf.m_errstr ?
	bool           m_kernelErrorReported;

	// last time g_hostdb.ping(i) was called for this host in milliseconds.
	int64_t      m_lastPing;

	char m_tmpBuf[4];
	int16_t m_tmpCount;

	// . first time we sent an unanswered ping request to this host
	// . used so we can determine when to send an email alert
	int64_t      m_startTime;
	// is a ping in progress for this host?
	char           m_inProgress1;
	// shotgun
	char           m_inProgress2;
	int64_t      m_numPingReplies;

	// send to eth 0 or 1 when sending to this host?
	char           m_preferEth;

	// machine #, a machine can have several hosts on it
	int32_t           m_machineNum;
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
	// 0 means no, 1 means yes, 2 means unknown
	char           m_syncStatus;

	// we now include the working dir in the hosts.conf file
	// so main.cpp can do gb --install and gb --allstart
	char           m_dir [ 128 ];

	char           m_hostname[16];

	// the secondary ethernet interface hostname
	char           m_hostname2[16];

	// client port
	uint16_t m_dnsClientPort;

	// was host in gk0 cluster and retired because its twin got
	// ssds, so it was no longer really needed.
	bool           m_retired;
	// used for logging when a host goes dead for the first time
	bool           m_wasAlive;
	bool           m_wasEverAlive;
	int64_t      m_timeOfDeath;
	// this toggles between 0 and 1 for alternating packet sends to
	// eth0 and eth1 of this host
	char           m_shotgunBit;
	// how many total error replies we got from this host
	int32_t           m_errorReplies;

	// now include a note for every host
	char           m_note[128];
	char           m_doingSync;

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

	int64_t      m_dgramsTo;
	int64_t      m_dgramsFrom;

	char           m_repairMode;

	// for timing how long the msg39 takes from this host
	int32_t           m_splitsDone;
	int64_t      m_splitTimes;

	// . the hostdb to which this host belongs!
	// . getHost(ip,port) will return a Host ptr from either 
	//   g_hostdb or g_hostdb2, so UdpServer.cpp needs to know which it
	//   is from when making the UdpSlot key.
	class Hostdb  *m_hostdb;

	// Syncdb.cpp uses these
	char           m_inSync ;

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
};

#define MAX_HOSTS 512
#define MAX_SPARES 64
#define MAX_PROXIES 6

// . this is used by Bandwidth.h and Msg21.cpp
// . it should really be about the same as MAX_HOSTS if we have one host
//   per machine
#define MAX_MACHINES 256

// for table for mapping key to a groupid
#define MAX_KSLOTS 8192

class Hostdb {

 public:

	// . set up our Hostdb
	// . sets itself from g_conf (our configuration class)
	// . returns false on fatal error
	// . gets filename from Conf.h class
	bool init ( int32_t hostId , char *netname = NULL,
		    bool proxyHost = false , char useTempCluster = 0 ,
		    char *cwd = NULL );

	// if config changes this *should* change
	int32_t getCRC();

	char *getNetName ( );

	Hostdb();
	~Hostdb();
	void reset();

	uint32_t  getMyIp         ( ) { return m_myIp; }
	uint16_t getMyPort       ( ) { return m_myPort; }
	int32_t           getMyHostId     ( ) { return m_hostId; }
	int32_t           getMyMachineNum ( ) { return m_myMachineNum; }
	uint32_t  getLoopbackIp   ( ) { return m_loopbackIp; }
	Host          *getMyHost       ( ) { return m_myHost; }
	bool           amProxy         ( ) { return m_myHost->isProxy(); }
	Host          *getMyShard      ( ) { return m_myShard; }
	int32_t getMyShardNum ( ) { return m_myHost->m_shardNum; }

	// . one machine may have several hosts
	// . get the machine # the hostId resides on
	int32_t getMachineNum ( int32_t hostId ) {
		return getHost(hostId)->m_machineNum; }

	int32_t getNumMachines ( ) { return m_numMachines; }

	// we consider the host dead if we didn't get a ping reply back
	// after 10 seconds
	bool  isDead ( int32_t hostId ) ;

	bool  isDead ( Host *h ) ;

	bool hasDeadHost ( );

	bool kernelErrors (Host *h) { return h->m_pingInfo.m_kernelErrors; }

	int64_t getNumGlobalRecs ( );

	bool isShardDead ( int32_t shardNum ) ;

	Host *getLiveHostInShard ( int32_t shardNum );

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
		if ( numHosts ) *numHosts = m_numHostsPerShard;
		return &m_hosts[shardNum * m_numHostsPerShard]; 
	}

	// get the host that has this path/ip
	Host *getHost2 ( char *cwd , int32_t *localIps ) ;
	Host *getProxy2 ( char *cwd , int32_t *localIps ) ;

	// . like above but just gets one host
	Host *getHost ( int32_t hostId ) {
		if ( hostId < 0 ) { char *xx=NULL;*xx=0; }
		return m_hostPtrs[hostId]; 
	}

	Host *getSpare ( int32_t spareId ) {
		return m_spareHosts[spareId]; }

	Host *getProxy ( int32_t proxyId ) {
		return m_proxyHosts[proxyId]; }

	int32_t  getNumHosts ( ) { return m_numHosts; }
	int32_t  getNumProxy ( ) { return m_numProxyHosts; }
	int32_t getNumProxies ( ) { return m_numProxyHosts; }
	int32_t getNumGrunts  ( ) { return m_numHosts; }
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
	Host *getHost          ( uint32_t ip , uint16_t port ) ;
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

	// sync a host with its twin
	bool syncHost ( int32_t syncHostId, bool useSecondaryIps );
	void syncStart_r ( bool amThread );
	void syncDone ( );

	int32_t getBestIp ( Host *h , int32_t fromIp ) ;
	
	Host *getBestSpiderCompressionProxy ( int32_t *key ) ;

	// only used by 'gb scale <hostdb.conf>' cmd for now
	void resetPortTables ();

	// returns best IP to use for "h" which is a host in hosts2.conf
	int32_t getBestHosts2IP ( Host *h );

	// our host's info used by Udp* classes for internal communication
	uint32_t  m_myIp;
	uint32_t  m_myIpShotgun;
	uint16_t m_myPort;
	int32_t           m_myMachineNum;
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

	int32_t    m_numMachines;

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

	bool createHostsConf( char *cwd );
	bool m_created;

	int32_t m_crc;
	int32_t m_crcValid;

	// for sync
	Host *m_syncHost;
	bool  m_syncSecondaryIps;

	char  m_useTmpCluster;

	uint32_t getShardNum (char rdbId, const void *key );
	uint32_t getShardNumFromDocId ( int64_t d ) ;

	// assume to be for posdb here
	uint32_t getShardNumByTermId ( const void *key );

	uint32_t m_map[MAX_KSLOTS];
};

extern class Hostdb g_hostdb;

extern Host     *g_listHosts [ MAX_HOSTS * 4 ];
extern uint32_t  g_listIps   [ MAX_HOSTS * 4 ];
extern uint16_t  g_listPorts [ MAX_HOSTS * 4 ];
extern int32_t      g_listNumTotal;

inline uint32_t getShardNum ( char rdbId, const void *key ) {
	return g_hostdb.getShardNum ( rdbId , key );
}

inline uint32_t getMyShardNum ( ) { 
	return g_hostdb.m_myHost->m_shardNum; 
}

inline uint32_t getShardNumFromDocId ( int64_t d ) {
	return g_hostdb.getShardNumFromDocId ( d );
}

#endif // GB_HOSTDB_H
