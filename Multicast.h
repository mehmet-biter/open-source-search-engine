// Matt Wells, Copyright Jun 2001


// . TODO: if i'm sending to 1 host in a specified group how do you know
//         to switch adn try anoher host in the group?  What if target host 
//         has to access disk ,etc...???

// . this class is used for performing multicasting through a UdpServer
// . the Multicast class is used to govern a multicast
// . each UdpSlot in the Multicast class corresponds to an ongoing transaction
// . takes up 24*4 = 96 bytes
// . TODO: have broadcast option for hosts in same network
// . TODO: watch out for director splitting the groups - it may
//         change the groups we select
// . TODO: set individual host timeouts yourself based on their std.dev pings

#ifndef GB_MULTICAST_H
#define GB_MULTICAST_H

#include "Hostdb.h"  // getGroup(), getTimes(), stampHost()
#include "UdpServer.h"        // sendRequest()
#include "Loop.h"         // registerSleepCallback()

#define MAX_HOSTS_PER_GROUP 10

//various timeouts, in milliseconds
static const int64_t multicast_infinite_send_timeout       = 9999999999;
static const int64_t multicast_msg20_summary_timeout       =       1500;
static const int64_t multicast_msg1_senddata_timeout       =      60000;
static const int64_t multicast_msg3a_default_timeout       =      10000;
static const int64_t multicast_msg3a_maximum_timeout       =      60000;
static const int64_t multicast_msg1c_getip_default_timeout =      60000;


class Multicast {

 public:

	Multicast  ( ) ;
	~Multicast ( ) ;
	void constructor ( );
	void destructor  ( );

	// . returns false and sets errno on error
	// . returns true on success -- your callback will be called
	// . check errno when your callback is called
	// . we timeout the whole shebang after "totalTimeout" seconds
	// . if "sendToWholeGroup" is true then this sends your msg to all 
	//   hosts in the group with id "groupId"
	// . if "sendToWholeGroup" is true we don't call callback until we 
	//   got a non-error reply from each host in the group
	// . it will keep re-sending errored replies every 1 second, forever
	// . this is useful for safe-sending adds to databases
	// . spider will reach max spiders and be stuck until
	// . if "sendToWholeGroup" is false we try to get a reply as fast
	//   as possible from any of the hosts in the group "groupId"
	// . callback will be called when a good reply is gotten or when all
	//   hosts in the group have failed to give us a good reply
	// . generally, for querying for data make "sendToWholeGroup" false
	// . generally, when adding      data make "sendToWholeGroup" true
	// . "totalTimeout" is for the whole multicast
	// . "key" is used to pick a host in the group if sendToWholeGroup
	//   is false
	// . Msg40 uses largest termId in winning group as the key to ensure
	//   that queries with the same largest termId go to the same machine
	// . likewise, Msg37 uses it to get the term frequency consistently
	//   so it doesn't jitter
	// . if you pass in a "replyBuf" we'll store the reply in there
	// . "doDiskLoadBalancing" is no longer used.
	bool send ( char       *msg             ,
		    int32_t        msgSize         ,
		    uint8_t     msgType         ,
		    // does this Multicast own this "msg"? if so, it will
		    // free it when done with it.
		    bool        ownMsg          , 
		    // send this request to a host or hosts who have
		    // m_groupId equal to this
		    //uint32_t groupId       , 
		    uint32_t shardNum ,
		    // should the request be sent to all hosts in the group
		    // "groupId", or just one host. Msg1 adds data to all 
		    /// hosts in the group so it sets this to true.
		    bool        sendToWholeShard, // Group, 
		    // if "key" is not 0 then it is used to select
		    // a host in the group "groupId" to send to.
		    int32_t        key             ,
		    void       *state           , // callback state
		    void       *state2          , // callback state
		    void      (*callback)(void *state,void *state2),
		    int64_t        totalTimeout    , //relative timeout in milliseconds
		    int32_t        niceness        ,
		    int32_t        firstHostId     = -1 ,// first host to try
		    char       *replyBuf        = NULL ,
		    int32_t        replyBufMaxSize = 0 ,
		    bool        freeReplyBuf    = true ,
		    bool        doDiskLoadBalancing = false ,
		    int32_t        maxCacheAge     = -1   , // no age limit
		    key_t       cacheKey        =  0   ,
		    char        rdbId           =  0   , // bogus rdbId
		    int32_t        minRecSizes     = -1   ,// unknown read size
		    bool        sendToSelf      = true ,// if we should.
		    int32_t        redirectTimeout = -1 ,
		    class Host *firstProxyHost  = NULL );

	// . get the reply from your NON groupSend
	// . if *freeReply is true then you are responsible for freeing this 
	//   reply now, otherwise, don't free it
	char *getBestReply ( int32_t *replySize , 
			     int32_t *replyMaxSize, 
			     bool *freeReply ,
			     bool  steal = false);

	// free all non-NULL ptrs in all UdpSlots, and free m_msg
	void reset ( ) ;

	// private:

	void destroySlotsInProgress ( UdpSlot *slot );

	// keep these public so C wrapper can call them
	bool sendToHostLoop ( int32_t key, int32_t hostNumToTry, int32_t firstHostId );
	bool sendToHost    ( int32_t i ); 
	int32_t pickBestHost  ( uint32_t key , int32_t hostNumToTry );
	void gotReply1     ( UdpSlot *slot ) ;
	void closeUpShop   ( UdpSlot *slot ) ;

	void sendToGroup   ( ) ;
	void gotReply2     ( UdpSlot *slot ) ;

	// . stuff set directly by send() parameters
	char       *m_msg;
	int32_t        m_msgSize;
	uint8_t     m_msgType;
	bool        m_ownMsg;
	//uint32_t m_groupId;
	void       *m_state;
	void       *m_state2;
	void       (* m_callback)( void *state , void *state2 );
	int64_t       m_totalTimeout;   // in milliseconds

	class UdpSlot *m_slot;

	// . m_slots[] is our list of concurrent transactions
	// . we delete all the slots only after cast is done
	int64_t        m_startTime;   // milliseconds since the epoch

	// # of replies we've received
	int32_t        m_numReplies;

	// . the group we're sending to or picking from
	// . up to MAX_HOSTS_PER_GROUP hosts
	// . m_retired, m_slots, m_errnos correspond with these 1-1
	Host       *m_hostPtrs[MAX_HOSTS_PER_GROUP];
	int32_t        m_numHosts;

	// . hostIds that we've tried to send to but failed
	// . pickBestHost() skips over these hostIds
	bool        m_retired    [MAX_HOSTS_PER_GROUP];

	// we can have up to 8 hosts per group
	UdpSlot    *m_slots      [MAX_HOSTS_PER_GROUP]; 
	// did we have an errno with this slot?
	int32_t        m_errnos     [MAX_HOSTS_PER_GROUP]; 
	// transaction in progress?
	char        m_inProgress [MAX_HOSTS_PER_GROUP]; 
	int64_t   m_launchTime [MAX_HOSTS_PER_GROUP];

	// steal this from the slot(s) we get
	char       *m_readBuf;
	int32_t        m_readBufSize;
	int32_t        m_readBufMaxSize;

	// if caller passes in a reply buf then we reference it here
	char       *m_replyBuf;
	int32_t        m_replyBufSize;
	int32_t        m_replyBufMaxSize;

	// we own it until caller calls getBestReply()
	bool        m_ownReadBuf;
	// are we registered for a callback every 1 second
	bool        m_registeredSleep;

	int32_t        m_niceness;

	// . last sending of the request to ONE host in a group (pick & send)
	// . in milliseconds
	int64_t   m_lastLaunch;
	Host       *m_lastLaunchHost;

	// only free m_reply if this is true
	bool        m_freeReadBuf;

	int32_t        m_key;

	//bool  m_doDiskLoadBalancing;
	char  m_rdbId              ;

	// Msg1 might be able to add data to our tree to save a net trans.
	bool        m_sendToSelf;

	int32_t        m_retryCount;

	char        m_sentToTwin;

	int32_t        m_redirectTimeout;
	char        m_inUse;

	// for linked list of available Multicasts in Msg4.cpp
	class Multicast *m_next;

	// host we got reply from. used by Msg3a for timing.
	Host      *m_replyingHost;
	// when the request was launched to the m_replyingHost
	int64_t  m_replyLaunchTime;

	// more hack stuff used by PageInject.cpp
	int32_t m_hackFileId;
	int64_t m_hackFileOff;
	class ImportState *m_importState;
};

#endif // GB_MULTICAST_H
