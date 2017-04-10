#include "Multicast.h"
#include "UdpSlot.h"
#include "UdpServer.h"
#include "Hostdb.h"
#include "Stats.h"
#include "Conf.h"
#include "Loop.h"         // registerSleepCallback()
#include "ScopedLock.h"
#include "Process.h"
#include "ip.h"
#include "Mem.h"

// TODO: if we're ordered to close and we still are waiting on stuff
//       to send we should send as much as we can and save the remaining
//       slots to disk for sending later??



void Multicast::constructor() {
	m_msg      = NULL;
	m_readBuf  = NULL;
	m_inUse    = false;
	for(int i=0; i<MAX_HOSTS_PER_GROUP; i++)
		m_host[i].reset();
}

void Multicast::destructor() {
	reset();
}

Multicast::Multicast()
  : m_msg(NULL),
    m_msgSize(0),
    m_msgType((msg_type_t)-1),
    m_ownMsg(false),
    m_slot(NULL),
    m_inUse(false),
    m_replyingHost(NULL),
    m_replyLaunchTime(0),
    m_hackFileId(0),
    m_hackFileOff(0),
    m_importState(NULL),
    m_mtx(),
    m_state(NULL), m_state2(NULL),
    m_callback(NULL),
    m_totalTimeout(0),
    m_startTime(0),
    m_numReplies(0),
    m_host(),
    m_numHosts(0),
    m_readBuf(NULL),
    m_readBufSize(0),
    m_readBufMaxSize(0),
    m_ownReadBuf(false),
    m_registeredSleep(false),
    m_niceness(0),
    m_lastLaunch(0),
    m_freeReadBuf(false),
    m_key(0),
    m_sentToTwin(false)
{
	constructor();
}

Multicast::~Multicast() {
	reset();
}


// free the send/read (request/reply) bufs we pirated from a UdpSlot or
// got from the caller
void Multicast::reset ( ) {
	// if this is called while we are shutting down and Scraper has a 
	// MsgE out it cores
	if ( m_inUse && ! g_process.m_exiting ) {
		log(LOG_ERROR, "net: Resetting multicast which is in use. msgType=0x%02x", (int)m_msgType);
		g_process.shutdownAbort(true);
	}

	if ( m_msg   && m_ownMsg ) 
		mfree ( m_msg   , m_msgSize   , "Multicast" );
	if ( m_readBuf && m_ownReadBuf && m_freeReadBuf ) 
		mfree ( m_readBuf , m_readBufMaxSize , "Multicast" );

	m_msg      = NULL;
	m_readBuf  = NULL;
	m_inUse    = false;
	m_replyingHost = NULL;

	for(int i=0; i<MAX_HOSTS_PER_GROUP; i++)
		m_host[i].reset();
}

// . an individual transaction's udpSlot is not be removed because we might 
//   get it a reply from it later after it's timeout
// . returns false and sets g_errno on error
// . caller can now pass in his own reply buffer
// . if "freeReplyBuf" is true that means it needs to be freed at some point
//   otherwise, it's probably on the stack or part of a larger allocate class.
bool Multicast::send(char *msg, int32_t msgSize, msg_type_t msgType, bool ownMsg, uint32_t shardNum, bool sendToWholeGroup_,
                     int32_t key, void *state, void *state2, void (*callback)(void *state, void *state2),
                     int64_t totalTimeout, int32_t niceness, int32_t firstHostId, bool freeReplyBuf) {
	// make sure not being re-used!
	if ( m_inUse ) {
		log( LOG_ERROR, "net: Attempt to re-use active multicast");
		g_process.shutdownAbort(true);
	}
	reset();

	// it is now in use
	m_inUse = true;
	// set the parameters in this class
	m_msg              = msg;
	m_ownMsg           = ownMsg;
	m_ownReadBuf       = true;
	m_freeReadBuf      = freeReplyBuf;
	m_msgSize          = msgSize;
	m_msgType          = msgType;
	m_state            = state;
	m_state2           = state2;
	m_callback         = callback;
	m_totalTimeout     = totalTimeout; // in milliseconds
	m_niceness         = niceness;
	// this can't be -1 i guess
	if ( totalTimeout <= 0 ) { g_process.shutdownAbort(true); }
	m_startTime        = gettimeofdayInMilliseconds();
	m_numReplies       = 0;
	m_readBuf          = NULL;
	m_readBufSize      = 0;
	m_readBufMaxSize   = 0;
	m_registeredSleep  = false;
	m_sentToTwin       = false;
	m_key              = key;

	// clear m_retired, m_errnos, m_slots
	for(int i=0; i<MAX_HOSTS_PER_GROUP; i++)
		m_host[i].reset();

	// . get the list of hosts in this shard
	Host *hostList = g_hostdb.getShard ( shardNum , &m_numHosts );

	// now copy the ptr into our array
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		m_host[i].m_hostPtr = &hostList[i];
	}

	// . pick the fastest host in the group
	// . this should pick the fastest one we haven't already sent to yet
	if ( ! sendToWholeGroup_ ) {
		bool retVal = sendToHostLoop(key,firstHostId);

		// on error, un-use this class
		if ( ! retVal ) {
			m_inUse = false;
		}

		return retVal;
	} else {
		// . send to ALL hosts in this group if sendToWholeGroup is true
		// . blocks forever until sends to all hosts are successfull
		sendToWholeGroup();
	
		// . sendToWholeGroup() always blocks, but we return true if no g_errno
		// . we actually keep looping until all hosts get the msg w/o error
		return true;
	}
}

///////////////////////////////////////////////////////
//                                                   //
//                  GROUP SEND                       //
//                                                   //
///////////////////////////////////////////////////////

// . keeps calling itself back on any error
// . resends to host/ip's that had error forever
// . callback only called when all hosts transmission are successful
// . it does not send to hosts whose m_errnos is 0
// . TODO: deal with errors from g_udpServer::sendRequest() better
// . returns false and sets g_errno on error
void Multicast::sendToWholeGroup() {
	ScopedLock sl(m_mtx);
	// see if anyone gets an error
	bool hadError = false;
	// . cast the msg to ALL hosts in the m_hosts group of hosts
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// cancel any errors
		g_errno = 0;
		// get the host
		Host *h = m_host[i].m_hostPtr;
		// if we got a nice reply from him skip him
		//slots[i] && m_host[i].m_slot->doneReading() ) continue;
		if ( m_host[i].m_retired ) continue;

		// . timeout is in seconds
		// . timeout is just the time remaining for the whole groupcast
		// int32_t timeout = m_startTime + m_totalTimeout - getTime();
		// . since we now must get non-error replies from ALL hosts
		//   in the group we no longer have a "totalTimeout" per se
		// reset the g_errno for host #i
		m_host[i].m_errno = 0;
		// send to the same port as us!
		int16_t destPort = h->m_port;

		// if from hosts2.conf pick the best ip!
		//int32_t  bestIp  = h->m_ip;
		int32_t bestIp = g_hostdb.getBestHosts2IP ( h );

		// retire the host to prevent resends
		m_host[i].m_retired = true;
		int32_t hid = h->m_hostId;
		// . send to a single host
		// . this creates a transaction control slot, "udpSlot"
		// . returns false and sets g_errno on error
		if (g_udpServer.sendRequest(m_msg, m_msgSize, m_msgType, bestIp, destPort, hid, &m_host[i].m_slot, this, gotReply2, m_totalTimeout, m_niceness)) {
			continue;
		}
		// g_errno must have been set, remember it
		m_host[i].m_errno = g_errno;
		// we had an error
		hadError = true;
		// bring him out of retirement to try again later in time
		m_host[i].m_retired = false;
		// log the error
		log("net: Got error sending add data request (0x%02x) to host #%d: %s. Sleeping one second and retrying.",
		    (int)m_msgType, h->m_hostId, mstrerror(g_errno));
		// . clear it, we'll try again
		// . if we don't clear Msg1::addList(), which returns
		//   true if it did not block, false if it did, will pick up
		//   on it and wierd things might happen.
		g_errno = 0;
		// continue if we're already registered for sleep callbacks
		if ( m_registeredSleep ) continue;
		// otherwise register for sleep callback to try again
		g_loop.registerSleepCallback( 5000/*ms*/, this, sleepWrapper2, m_niceness );
		m_registeredSleep = true;
	}
	// if we had an error then we'll be called again in a second
	if ( hadError ) return;
	// otherwise, unregister sleep callback if we had no error
	if ( m_registeredSleep ) {
		g_loop.unregisterSleepCallback ( this , sleepWrapper2 );
		m_registeredSleep = false;
	}
}

void Multicast::sleepWrapper2(int bogusfd, void *state) {
	Multicast *THIS = static_cast<Multicast*>(state);
	// try another round of sending to see if hosts had errors or not
	THIS->sendToWholeGroup();
}


void Multicast::gotReply2(void *state, UdpSlot *slot) {
	Multicast *THIS = static_cast<Multicast*>(state);
        THIS->gotReply2(slot);
}

// . otherwise, we were sending to a whole group so ALL HOSTS must produce a 
//   successful reply
// . we keep re-trying forever until they do
void Multicast::gotReply2 ( UdpSlot *slot ) {
	ScopedLock sl(m_mtx);
	// don't ever let UdpServer free this send buf (it is m_msg)
	slot->m_sendBufAlloc = NULL;
	// save this for msg4 logic that calls injection callback
	m_slot = slot;
	// . log the error
	// . ETRYAGAIN often happens when we are falling too far behind in
	//   our merging (see Rdb.cpp) and we enter urgent merge mode
	// . it may also happen if tree is too full and is being dumped to disk
	//if ( g_errno && g_errno != ETRYAGAIN ) 
	//	log("net: Got error reply sending to a host during a "
	//	    "group send: %s.", mstrerror(g_errno) );
	// set m_errnos for this slot
	int32_t i;
	for(i = 0; i < m_numHosts; i++)
		if(m_host[i].m_slot == slot)
			break;
	if ( i == m_numHosts ) {
		// if it matched no slot that's wierd
		log(LOG_LOGIC,"net: multicast: Not our slot.");
		return;
	}
	// set m_errnos to g_errno, if any
	m_host[i].m_errno = g_errno;
	// if g_errno was not set we have a legit reply
	if ( ! g_errno )
		m_numReplies++;
	// reset g_errno in case we do more sending
	g_errno = 0;
	// . if we got all the legit replies we're done, call the callback
	// . all slots should be destroyed by UdpServer in this case
	if ( m_numReplies >= m_numHosts ) {
		// allow us to be re-used now, callback might relaunch
		m_inUse = false;
		if ( m_callback ) {
			sl.unlock();
			m_callback ( m_state , m_state2 );
		}
		return;
	}
	// if this guy had no error then wait for more callbacks
	if(!m_host[i].m_errno)
		return;
	// bring this slot out of retirement so we can send to him again
	m_host[i].m_retired = false;
	// do indeed log the try again things, cuz we have gotten into a 
	// nasty loop with them that took me a while to track down
	bool logIt = false;
	static int32_t s_elastTime = 0;
	if(m_host[i].m_errno != ETRYAGAIN )
		logIt = true;
	else {
		// log it every 10 seconds even if it was a try again
		int32_t now = getTime();
		if (now - s_elastTime > 10) {
			s_elastTime = now;
			logIt=true;
		}
	}
	// don't log ETRYAGAIN, may come across as bad when it is normal
	if(m_host[i].m_errno == ETRYAGAIN)
		logIt = false;
	// log a failure msg
	if ( logIt ) {
		const Host *h = slot->m_host;
		if ( h ) 
			log("net: Got error sending request to hostId %d (msgType=0x%02x transId=%d net=%s): %s. Retrying.",
			    h->m_hostId, (int)slot->getMsgType(), slot->getTransId(),
			    g_hostdb.getNetName(),mstrerror(m_host[i].m_errno) );
		else
			log("net: Got error sending request to %s:%d (msgType=0x%02x transId=%d net=%s): %s. Retrying.",
			    iptoa(slot->getIp()), (int32_t)slot->getPort(),
			    (int)slot->getMsgType(), slot->getTransId(),
			    g_hostdb.getNetName(),mstrerror(m_host[i].m_errno) );
	}
	// . let's sleep for a second before retrying the send
	// . the g_errno could be ETRYAGAIN which happens if we're trying to 
	//   add data but the other host is temporarily full
	// . continue if we're already registered for sleep callbacks
	if(m_registeredSleep)
		return;
	// . otherwise register for sleep callback to try again
	// . sleepWrapper2() will call sendToWholeGroup() for us
	g_loop.registerSleepCallback( 5000/*ms*/, this, sleepWrapper2, m_niceness );
	m_registeredSleep = true;
}

///////////////////////////////////////////////////////
//                                                   //
//                  PICK & SEND                      //
//                                                   //
///////////////////////////////////////////////////////

// . returns false and sets g_errno on error
// . returns true if managed to send to one host ok (initially at least)
// . uses key to pick the first host to send to (for consistency)
// . after we pick a host and launch the request to him the sleepWrapper1
//   will call this at regular intervals, so be careful,
bool Multicast::sendToHostLoop(int32_t key, int32_t firstHostId) {
	// erase any errors we may have got
	g_errno = 0 ;

	for (;;) {
		// what if this host is dead?!?!?
		int32_t i = pickBestHost(key, firstHostId);

		// . if no more hosts return FALSE
		// . we need to return false to the caller of us below
		if (i < 0) {
			g_errno = ENOHOSTS;
			return false;
		}

		// do not resend to retired hosts
		if( m_host[i].m_retired ) {
			g_errno = ENOHOSTS;
			return false;
		}

		// . send to this guy, if we haven't yet
		// . returns false and sets g_errno on error
		// . if it returns true, we sent ok, so we should return true
		// . will return false if the whole thing is timed out and g_errno
		//   will be set to ETIMEDOUT
		// . i guess ENOSLOTS means the udp server has no slots available
		//   for sending, so its pointless to try to send to another host
		if (sendToHost(i)) {
			return true;
		}

		// . if no more slots, we're done, don't loop!
		// . pointless as well if no time left in the multicast
		// . or if shutting down the server! otherwise it loops forever and
		//   won't exit when sending a msg20 request. i've seen this...
		if (g_errno == ENOSLOTS || g_errno == EUDPTIMEDOUT || g_errno == ESHUTTINGDOWN) {
			return false;
		}

		// otherwise try another host and hope for the best
		g_errno = 0;
		key = 0;

		// what kind of error leads us here? EBUFTOOSMALL or EBADENGINEER...
	}
}

// . pick the fastest host from m_hosts based on avg roundtrip time for ACKs
// . skip hosts in our m_retired[] list of hostIds
// . returns -1 if none left to pick
int32_t Multicast::pickBestHost ( uint32_t key , int32_t firstHostId ) {
	// bail if no hosts
	if ( m_numHosts == 0 ) {
		return -1;
	}

	// . if firstHostId not -1, try it first
	// . Msg0 uses this only to select hosts on same machine for now
	// . Msg20 now uses this to try to make sure the lower half of docids
	//   go to one twin and the upper half to the other. this makes the
	//   tfndb page cache twice as effective when getting summaries.
	if ( firstHostId >= 0 ) {
		//log("got first hostId!!!!");
		// find it in group
		int32_t i;
		for ( i = 0 ; i < m_numHosts ; i++ )
			if ( m_host[i].m_hostPtr->m_hostId == firstHostId ) break;
		// if not found bitch
		if ( i >= m_numHosts ) {
			log(LOG_LOGIC,"net: multicast: HostId %" PRId32" not in group.", firstHostId );
			g_process.shutdownAbort(true);
		}
		// if we got a match and it's not dead, return it
		if ( i < m_numHosts && ! g_hostdb.isDead ( m_host[i].m_hostPtr ) )
			return i;
	}

	// round robin selection
	int32_t   numDead   =  0;
	int32_t   dead      = -1;
	int32_t   n         = 0;
	//int32_t   count     = 0;
	bool   balance   = g_conf.m_doStripeBalancing;
	// always turn off stripe balancing for all but these msgTypes
	if ( m_msgType != msg_type_39 ) {
		balance = false;
	}
	// . pick the guy in our "stripe" first if we are doing these msgs
	// . this will prevent a ton of msg39s from hitting one host and
	//   "spiking" it.
	if ( balance ) n = g_hostdb.m_myHost->m_stripe;
	// . if key is not zero, use it to select a host in this group
	// . if the host we want is dead then do it the old way
	// . ignore the key if balance is true though! MDW
	if ( key != 0 && ! balance ) {
		// often the groupId was selected based on the key, so lets
		// randomize everything up a bit
		uint32_t i = hashLong ( key ) % m_numHosts;
		// if he's not dead or retired use him right away
		if ( ! m_host[i].m_retired &&
		     ! g_hostdb.isDead ( m_host[i].m_hostPtr ) )
			return i;
	}

	// no no no we need to randomize the order that we try them
	Host *fh = m_host[n].m_hostPtr;
	// if this host is not dead, use him
	if ( ! m_host[n].m_retired &&
	     ! g_hostdb.isDead(fh) )
		return n;

	// . ok now select the kth available host
	// . make a list of the candidates
	int32_t cand[32];
	int32_t nc = 0;
	for ( int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// get the host
		Host *h = m_host[i].m_hostPtr;
		// count those that are dead
		if ( g_hostdb.isDead ( h ) )
			numDead++;
		// skip host if we've retired it
		if ( m_host[i].m_retired ) continue;
		// if this host is not dead use him
		if ( !g_hostdb.isDead(h) )
			cand[nc++] = i;
		// pick a dead that isn't retired
		dead = i;
	}
	// if a host was alive and untried, use him next
	if ( nc > 0 ) {
		int32_t k = ((uint32_t)m_key) % nc;
		return cand[k];
	}
	// . come here if all hosts were DEAD
	// . try sending to a host that is dead, but not retired now
	// . if all deadies are retired this will return -1
	// . sometimes a host can appear to be dead even though it was
	//   just under severe load
	if ( numDead == m_numHosts ) return dead;
	// otherwise, they weren't all dead so don't send to a deadie
	return -1;
}

// . returns false and sets error on g_errno
// . returns true if kicked of the request (m_msg)
// . sends m_msg to host "h"
bool Multicast::sendToHost ( int32_t i ) {
	// sanity check
	if ( i >= m_numHosts ) { g_process.shutdownAbort(true); }
	// sanity check , bitch if retired
	if ( m_host[i].m_retired ) {
		log(LOG_LOGIC,"net: multicast: Host #%d is retired. Bad engineer.",i);
		//g_process.shutdownAbort(true);
		return true;
	}
	// . add this host to our retired list so we don't try again
	// . only used by pickBestHost() and sendToHost()
	m_host[i].m_retired = true;
	// what time is it now?
	int64_t nowms = gettimeofdayInMilliseconds();
	// save the time
	m_host[i].m_launchTime = nowms;
	// sometimes clock is updated on us
	if ( m_startTime > nowms )
		m_startTime = nowms;
	// . timeout is in seconds
	// . timeout is just the time remaining for the whole groupcast
	int64_t timeRemaining = m_startTime + m_totalTimeout - nowms;
	// . if timeout is negative then reset start time so we try forever
	// . no, this could be called by a re-route in sleepWrapper1 in which
	//   case we really should timeout.
	// . this can happen if sleepWrapper found a timeout before UdpServer
	//   got its timeout.
	if ( timeRemaining <= 0 ) {
		//m_startTime = getTime();; timeout = m_totalTimeout;}
		//g_errno = ETIMEDOUT; 
		// this can happen if the udp reply timed out!!! like if a
		// host is under severe load... with Msg23::getLinkText()
		// or Msg22::getTitleRec() timing out on us. basically, our
		// msg23 request tried to send a msg22 request which timed out
		// on it so it sent us back this error.
		if ( g_errno != EUDPTIMEDOUT ) 
			log(LOG_INFO,"net: multicast: had negative timeout, %" PRId64". "
			    "startTime=%" PRId64" totalTimeout=%" PRId64" "
			    "now=%" PRId64". msgType=0x%02x "
			    "niceness=%" PRId32" clock updated?",
			    timeRemaining,m_startTime,m_totalTimeout,
			    nowms, (int)m_msgType,
			    (int32_t)m_niceness);
		timeRemaining = m_totalTimeout;
	}
	// get the host
	Host *h = m_host[i].m_hostPtr;
	// send to the same port as us!
	int16_t destPort = h->m_port;

	// if from hosts2.conf pick the best ip!
	int32_t bestIp = g_hostdb.getBestHosts2IP ( h );

	// sanity check
	//if ( g_hostdb.isDead(h) ) {
	//	log("net: trying to send to dead host.");
	//	g_process.shutdownAbort(true);
	//}
	// don't set hostid if we're sending to a remote cluster
	int32_t hid = h->m_hostId;
	// if sending to a proxy keep this set to -1
	if ( h->m_type != HT_GRUNT ) hid = -1;
	// max resends. if we resend a request dgram this many times and
	// got no ack, bail out with g_errno set to ENOACK. this is better
	// than the timeout because it takes like 20 seconds to mark a 
	// host as dead and takes "timeRemaining" seconds to timeout the
	// request
	int32_t maxResends = -1;
	// . only use for nicness 0
	// . it uses a backoff scheme, increments delay for first few resends:
	// . it starts of at 33ms, then 66, then 132, then 200 from there out
	if ( m_niceness == 0 ) maxResends = 4;
	// . send to a single host
	// . this creates a transaction control slot, "udpSlot"
	// . return false and sets g_errno on error
	// . returns true on successful launch and calls callback on completion
	ScopedLock sl(m_mtx);
	if (!g_udpServer.sendRequest(m_msg, m_msgSize, m_msgType, bestIp, destPort, hid, &m_host[i].m_slot, this, gotReply1, timeRemaining, m_niceness, NULL, -1, -1, maxResends)) {
		log(LOG_WARN, "net: Had error sending msgtype 0x%02x to host #%d: %s. Not retrying.",
		    (int)m_msgType, h->m_hostId, mstrerror(g_errno));
		// i've seen ENOUDPSLOTS available msg here along with oom
		// condition...
		//g_process.shutdownAbort(true); 
		return false;
	}
	// mark it as outstanding
	m_host[i].m_inProgress = true;
	// set our last launch date
	m_lastLaunch = nowms ; // gettimeofdayInMilliseconds();
	// timing debug
	//log("Multicast sent to hostId %" PRId32", this=%" PRId32", transId=%" PRId32,
	//    h->m_hostId, (int32_t)this , m_host[i].m_slot->m_transId );
	// . let's sleep so we have a chance to launch to another host in
	//   the same group in case this guy takes too long
	// . don't re-register if we already did
	if(!m_registeredSleep) {
		int wait = calculateTimeout();
		if(wait>=0) {
			// . otherwise register for sleep callback to try again
			// . sleepCallback1Wrapper() will call sendToHostLoop() for us
			g_loop.registerSleepCallback(wait/*ms*/, this, sleepCallback1Wrapper, m_niceness );
			m_registeredSleep = true;
		}
	}
	// successful launch
	return true;
}

int Multicast::calculateTimeout() {
	// . don't relaunch any niceness 1 stuff for a while
	// . it often gets suspended due to query traffic
	//if ( m_niceness > 0 && elapsed < 800000 ) return;
	if ( m_niceness > 0 ) return -1;

	// . Msg36 is used to get the length of an IndexList (termFreq)
	//   and is very fast, all in memory, don't wait more than 50ms
	// . if we do re-route this is sucks cuz we'll get slightly different
	//   termFreqs which impact the total results count as well as summary
	//   generation since it's based on termFreq, not too mention the
	//   biggest impact being ordering of search results since the
	//   score weight is based on termFreq as well
	// . but unfortunately, this scheme doesn't increase the ping time
	//   of dead hosts that much!!
	// . NOTE: 2/26/04: i put most everything to 8000 ms since rerouting
	//   too much on an already saturated network of drives just 
	//   excacerbates the problem. this stuff was originally put here
	//   to reroute for when a host went down... let's keep it that way

	switch ( m_msgType ) {
		// msg to get a summary from a query (calls msg22)
		// put to 5 seconds now since some hosts freezeup still it seems
		// and i haven't seen a summary generation of 5 seconds
		case msg_type_20:
			return 5000;
		// msg 0x20 calls this to get the title rec
		case msg_type_22:
			return 1000;
		// . msg to get an index list over the net
		// . this limit should really be based on length of the index list
		case msg_type_0:
			// this should just be for when a host goes down, not for
			// performance reasons, cuz we do pretty good load balancing
			// and when things get saturated, rerouting excacerbates it
			return 8000;
		// msg to get docIds from a query, may take a while
		case msg_type_39: {
			// how many docsids request? first 4 bytes of request.
			int32_t docsWanted = 10;
			int32_t nqterms        = 0;
			if ( m_msg ) {
				docsWanted     = *(int32_t *)(m_msg);
				nqterms        = *(int32_t *)(m_msg+4);
			}

			// never re-route if it has a rerank, those take forever
			// . how many milliseconds of waiting before we re-route?
			// . 100 ms per doc wanted, but if they all end up
			//   clustering then docsWanted is no indication of the
			//   actual number of titleRecs (or title keys) read
			// . it may take a while to do dup removal on 1 million docs
			int32_t wait = 5000 + 100  * docsWanted;
			// those big UOR queries should not get re-routed all the time
			if ( nqterms > 0 ) {
				wait += 1000 * nqterms;
			}
			if ( wait < 8000 ) {
				wait = 8000;
			}
			return wait;
		}
		// don't relaunch anything else unless over 8 secs
		default:
			return 8000;
	}

}

// this is called every 50 ms so we have the chance to launch our request
// to a more responsive host
void Multicast::sleepCallback1Wrapper ( int bogusfd , void    *state ) {
	Multicast *that = static_cast<Multicast*>(state);
	that->sleepCallback1();
}

void Multicast::sleepCallback1() {
	// . if our last launch was less than X seconds ago, wait another tick
	// . we often send out 2+ requests and end up getting one reply before
	//   the others and that results in us getting unwanted dgrams...
	// . increasing this delay here results in fewer wasted requests but
	//   if a host goes down you don't want a user to wait too long
	// . after a host goes down it's ping takes a few secs to decrease
	// . if a host is shutdown properly it will broadcast a msg to
	//   all hosts using Hostdb::broadcast() informing them that it's 
	//   going down so they know to stop sending to it and mark him as
	//   dead

	int64_t now = gettimeofdayInMilliseconds();
	// watch out for someone advancing the system clock
	if ( m_lastLaunch > now ) m_lastLaunch = now;
	// get elapsed time since we started the send
	int32_t elapsed = now - m_lastLaunch;
	//log("elapsed = %" PRId32" type=0x%02x",elapsed,m_msgType);


	// TODO: if the host went dead on us, re-route


	// find out which host timedout
	Host *hd = NULL;
	for(int i=0; i<m_numHosts && !hd; i++)
		if(m_host[i].m_retired)
			hd = m_host[i].m_hostPtr;
	// 11/21/06: now we only reroute if the host we sent to is marked as
	// dead unless it is a msg type that takes little reply generation time
	if ( hd && ! g_hostdb.isDead(hd)  ) {
		return;
	}

	// cancel any outstanding transactions iff we have a m_replyBuf
	// that we must read the reply into because we cannot share!!
	if ( m_readBuf ) {
		destroySlotsInProgress ( NULL );
	}

	// . do a loop over all hosts in the group
	// . if a whole group of twins is down this will loop forever here
	//   every Xms, based the sleepWrapper timer for the msgType
	if ( g_conf.m_logDebugQuery ) {
		for (int32_t i = 0 ; i < m_numHosts ; i++ ) {
			if ( ! m_host[i].m_slot         ) continue;
			// transaction is not in progress ifm_host[i].m_errno is set
			const char *ee = "";
			if ( m_host[i].m_errno ) ee = mstrerror(m_host[i].m_errno);
			log(LOG_DEBUG, "net: Multicast::sleepWrapper1: tried host %s:%d %s",
			    iptoa(m_host[i].m_slot->getIp()),
			    (int32_t)m_host[i].m_slot->getPort(), ee);
		}
	}

	// log msg that we are trying to re-route
	//log("Multicast::sleepWrapper1: trying to re-route msgType=0x%02x "
	//    "to new host",   m_msgType );	

	// . otherwise, launch another request if we can
	// . returns true if we successfully sent to another host
	// . returns false and sets g_errno if no hosts left or other error
	if ( sendToHostLoop(0,-1) ) {
		// log msg that we were successful
		int32_t hid = -1;
		if ( hd ) hid = hd->m_hostId;
		log(LOG_WARN,
		    "net: Multicast::sleepWrapper1: rerouted msgType=0x%02x from host #%d to new host after waiting %d ms",
		    (int)m_msgType, hid,elapsed);
		// . mark it in the stats for PageStats.cpp
		// . this is timeout based rerouting
		g_stats.m_reroutes[(int)m_msgType][m_niceness]++;
		return;
	}
	// if we registered the sleep callback we must have launched a 
	// request to a host so let gotReplyWrapperM1() deal with closeUpShop()

	// . let replyWrapper1 be called if we got one launched
	// . it should then call closeUpShop()
	//if ( m_numLaunched ) return;
	// otherwise, no outstanding requests and we failed to send to another
	// host, probably because :
	// 1. Msg34 timed out on all hosts
	// 2. there were no udp slots available (which is bad)
	//log("Multicast:: re-route failed for msgType=%02x. abandoning.",
	//     m_msgType );
	// . the next send failed to send to a host, so close up shop
	// . this is probably because the Msg34s timed out and we could not
	//   find a next "best host" to send to because of that
	//closeUpShop ( NULL );
	// . we were not able to send to another host, maybe it was dead or
	//   there are no hosts left!
	// . i guess keep sleeping until host comes back up or transaction
	//   is cancelled
	//log("Multicast::sleepWrapper1: re-route of msgType=0x%02x failed",
	//    m_msgType);
}


void Multicast::gotReply1(void *state, UdpSlot *slot) {
	Multicast *THIS = static_cast<Multicast*>(state);
	THIS->gotReply1(slot);
}

// come here if we've got a reply from a host that's not part of a group send
void Multicast::gotReply1 ( UdpSlot *slot ) {		
	ScopedLock sl(m_mtx);

	// don't ever let UdpServer free this send buf (it is m_msg)
	slot->m_sendBufAlloc = NULL;

	// remove the slot from m_slots so it doesn't get nuked in
	// gotSlot(slot) routine above
	int32_t i = 0;

	// careful! we might have recycled a slot!!! start with top and go down
	// because UdpServer might give us the same slot ptr on our 3rd try
	// that we had on our first try!
	for ( i = 0 ; i < m_numHosts ; i++ ) {
		// skip if not in progress
		if ( ! m_host[i].m_inProgress ) continue;
		// slot must match
		if ( m_host[i].m_slot == slot ) break;
	}
	// if it matched no slot that's wierd
	if ( i >= m_numHosts ) {
		log(LOG_LOGIC,"net: multicast: Not our slot 2."); 
		g_process.shutdownAbort(true);
	}

	// setm_host[i].m_errno, if any
	if ( g_errno ) {
		m_host[i].m_errno = g_errno;
	}

	// mark it as no longer in progress
	m_host[i].m_inProgress = false;

	Host *h = m_host[i].m_hostPtr;

	// save the host we got a reply from
	m_replyingHost    = h;
	m_replyLaunchTime = m_host[i].m_launchTime;

	if ( m_sentToTwin ) {
		log(LOG_DEBUG, "net: Twin msgType=0x%" PRIx32" (this=0x%" PTRFMT") reply: %s.",
		    (int32_t) m_msgType, (PTRTYPE) this, mstrerror(g_errno));
	}

	sl.unlock();

	// on error try sending the request to another host
	// return if we kicked another request off ok
	if ( g_errno ) {
		// do not log not found on an external network
		if ( g_errno != ENOTFOUND ) {
			// log the error
			Host *h = g_hostdb.getUdpHost(slot->getIp(), slot->getPort());
			if (h) {
				log(LOG_WARN, "net: Multicast got error in reply from hostId %d (msgType=0x%02x transId=%d nice=%d net=%s): %s.",
				    h->m_hostId, (int)slot->getMsgType(), slot->getTransId(),
				    m_niceness,
				    g_hostdb.getNetName(), mstrerror(g_errno));
			} else {
				log(LOG_WARN, "net: Multicast got error in reply from %s:%d (msgType=0x%02x transId=%d nice =%d net=%s): %s.",
				    iptoa(slot->getIp()), (int32_t) slot->getPort(),
				    (int)slot->getMsgType(), slot->getTransId(), m_niceness,
				    g_hostdb.getNetName(), mstrerror(g_errno));
			}
		}

		// . try to send to another host
		// . on successful sending return, we'll be called on reply
		// . this also returns false if no new hosts left to send to
		// . only try another host if g_errno is NOT ENOTFOUND cuz
		//   we have quite a few missing clustRecs and titleRecs
		//   and doing a second lookup will decrease query response
		// . if the Msg22 lookup cannot find the titleRec for indexing
		//   purposes, it should check any twin hosts because this
		//   is very important... if this is for query time, however,
		//   then accept the ENOTFOUND without spawning another request
		// . but if the record is really not there we waste seeks!
		// . EBADENGINEER is now used by titledb's Msg22 when a docid
		//   is in tfndb but not in titledb (or id2 is invalid)
		// . it is more important that we serve the title rec than
		//   the performance gain. if you want the performance gain
		//   then you should repair your index to avoid this. therefore
		//   send to twin on ENOTFOUND
		// . often, though, we are restring to indexdb root so after
		//   doing a lot of deletes there will be a lot of not founds
		//   that are really not found (not corruption) so don't do it
		//   anymore
		// . let's go for accuracy even for queries
		// . until i fix the bug of losing titlerecs for some reason
		//   probably during merges now, we reroute on ENOTFOUND.
		bool sendToTwin = true;
		if ( g_errno == EBADENGINEER       ) sendToTwin = false;
		if ( g_errno == EMSGTOOBIG         ) sendToTwin = false;
		if ( g_errno == E2BIG              ) sendToTwin = false;
		if ( g_errno == EUNCOMPRESSERROR   ) sendToTwin = false;
		// ok, let's give up on ENOTFOUND, because the vast majority
		// of time it seems it is really not on the twin either...
		if (g_errno == ENOTFOUND && (m_msgType == msg_type_20 || m_msgType == msg_type_22)) {
			sendToTwin = false;
		}

		// do not send to twin if we are out of time
		time_t now           = getTime();
		int32_t   timeRemaining = m_startTime + m_totalTimeout - now;
		if ( timeRemaining <= 0 ) sendToTwin = false;
		// send to the twin
		if ( sendToTwin && sendToHostLoop(0,-1) ) {
			log(LOG_INFO, "net: Trying to send request msgType=0x%" PRIx32" to a twin. (this=0x%" PTRFMT")",
			    (int32_t)m_msgType,(PTRTYPE)this);
			m_sentToTwin = true;
			// . keep stats
			// . this is error based rerouting
			// . this can be timeouts as well, if the
			//   receiver sent a request itself and that
			//   timed out...
			g_stats.m_reroutes[(int)m_msgType][m_niceness]++;
			return;
		}
		// . otherwise we've failed on all hosts
		// . re-instate g_errno,might have been set by sendToHostLoop()
		g_errno =m_host[i].m_errno;
	}
	closeUpShop ( slot );
}

void Multicast::closeUpShop ( UdpSlot *slot ) {
	// sanity check
	if (!m_inUse) {
		g_process.shutdownAbort(true);
	}

	// destroy the OTHER slots we've spawned that are in progress
	destroySlotsInProgress ( slot );

	if (slot) {
		// . now we have a good reply... but not if g_errno is set
		// . save the reply of this slot here
		// . this is bad if we got an g_errno above, it will set the slot's
		//   readBuf to NULL up there, and that will make m_readBuf NULL here
		//   causing a mem leak. i fixed by adding an mfree on m_replyBuf
		//   in Multicast::reset() routine.
		// . i fixed again by ensuring we do not set m_ownReadBuf to false
		//   in getBestReply() below if m_readBuf is NULL
		m_readBuf = slot->m_readBuf;
		m_readBufSize = slot->m_readBufSize;
		m_readBufMaxSize = slot->m_readBufMaxSize;

		// . if the slot had an error, propagate it so it will be set when
		//   we call the callback.
		if (!g_errno) {
			g_errno = slot->getErrno();
		}

		// . sometimes UdpServer will read the reply into a temporary buffer
		// . this happens if the udp server is hot (async signal based) and
		//   m_replyBuf is NULL because he cannot malloc a buf to read into
		//   because malloc is not async signal safe
		if (slot->m_tmpBuf == slot->m_readBuf) {
			m_freeReadBuf = false;
		}
		// don't let UdpServer free the readBuf now that we point to it
		slot->m_readBuf = NULL;

		// save slot so msg4 knows what slot replied in udpserver
		// for doing its flush callback logic
		m_slot = slot;
	}

	// unregister our sleep wrapper if we did
	if ( m_registeredSleep ) {
		g_loop.unregisterSleepCallback(this, sleepCallback1Wrapper);
		m_registeredSleep = false;
	}

	// allow us to be re-used now, callback might relaunch
	m_inUse = false;

	// now call the user callback if it exists
	if ( m_callback ) {
		m_callback ( m_state , m_state2 );
	}
}

// destroy all slots that may be in progress (except "slot")
void Multicast::destroySlotsInProgress ( UdpSlot *slot ) {
	// do a loop over all hosts in the group
	for (int32_t i = 0 ; i < m_numHosts ; i++ ) {
		// . destroy all slots but this one that are in progress
		// . we'll be destroyed when we return from the cback
		if ( ! m_host[i].m_slot         ) continue;
		// transaction is not in progress ifm_host[i].m_errno is set
		if (  m_host[i].m_errno        ) continue;
		// dont' destroy us, it'll happen when we return
		if (   m_host[i].m_slot == slot ) continue;
		// must be in progress
		if ( ! m_host[i].m_inProgress ) continue;
		// sometimes the slot is recycled from under us because
		// we already got a reply from it
		//if ( m_host[i].m_slot->m_state != this ) continue;
		// don't free his sendBuf, readBuf is ok to free, however
		m_host[i].m_slot->m_sendBufAlloc = NULL;

		// destroy this slot that's in progress
		g_udpServer.destroySlot ( m_host[i].m_slot );
		// do not re-destroy. consider no longer in progress.
		m_host[i].m_inProgress = false;
	}
}


// we set *freeReply to true if you'll need to free it
char *Multicast::getBestReply(int32_t *replySize, int32_t *replyMaxSize, bool *freeReply, bool steal) {
	*replySize    = m_readBufSize;
	*replyMaxSize = m_readBufMaxSize;
	if(steal) {
		m_freeReadBuf = false;
	}
	*freeReply    = m_freeReadBuf;
	// this can be NULL if we destroyed the slot in progress only to
	// try another host who was dead!
	if ( m_readBuf ) m_ownReadBuf  = false;
	return m_readBuf;
}
