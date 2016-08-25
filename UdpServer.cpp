#include "gb-include.h"

#include "UdpServer.h"
#include "Dns.h"      // g_dnsDistributed.extractHostname()
#include "Profiler.h"
#include "Stats.h"
#include "Proxy.h"
#include "Process.h"
#include "Loop.h"
#include "IPAddressChecks.h"
#include "BitOperations.h"
#include "Msg0.h" //RDBIDOFFSET
#include "Rdb.h" //RDB_...
#include "max_niceness.h"
#include "ScopedLock.h"
#include <assert.h>

// . any changes made to the slots should only be done without risk of
//   interruption because makeCallbacks() reads from the slots to call
//   callbacks, we don't want it reading garbage


int32_t g_dropped = 0;
int32_t g_corruptPackets = 0;
static int32_t g_consecutiveOOMErrors = 0;
bool g_inHandler = false;

// . making a hot udp server (realtime signal based)
// . caller calls to sendRequest() or sendReply() should turn off interrupts
//   before messing with our data
// . timepoll should turn off interrupts, too
// . we should call sigqueue if a callback needs to be made if we're hot

// a global class extern'd in .h file
UdpServer g_udpServer;

// used when sendRequest() is called with a NULL callback
static void defaultCallbackWrapper(void * /*state*/, UdpSlot * /*slot*/) {
}


// now redine key_t as our types.h should have it
#define key_t  u_int96_t

// free send/readBufs
void UdpServer::reset() {

	// clear our slots
	if ( ! m_slots ) return;
	log(LOG_DEBUG,"db: resetting udp server");
	mfree ( m_slots , m_maxSlots * sizeof(UdpSlot) , "UdpServer" );
	m_slots = NULL;
	if ( m_buf ) mfree ( m_buf , m_bufSize , "UdpServer");
	m_buf = NULL;
}


UdpServer::UdpServer ( ) {
	m_sock = -1;
	m_slots = NULL;
	m_maxSlots = 0;
	m_buf = NULL;
	m_outstandingConverts = 0;
	m_writeRegistered = false;
}

UdpServer::~UdpServer() {
	reset();
}


//Enlarge receive or send buffer on UDP socket. The trouble is that if we try
//to set them too high then setsockopt() just fails, so we have to do a binary
//search for the maximum size. Or use some ghastly linux-specificy way of
//seeing what the kernel will allow.
static void enlargeUdpSocketBufffer(int fd, const char *bufname, int optname, int proposed_size)
{
	int current_buffer_size;
	socklen_t optlen = sizeof(current_buffer_size);

	if(getsockopt(fd, SOL_SOCKET, optname, (char*)&current_buffer_size, &optlen))	{
		log(LOG_ERROR,"udp: Could not getsockopt() on fd %d, errno = %d", fd, errno);
		return;
	}
	
	if(current_buffer_size>=proposed_size) {
		log(LOG_DEBUG, "udp: %s buffer on fd %d is already at %d", bufname, fd, current_buffer_size);
		return;
	}
	

	int buffer_size = proposed_size;

	while(buffer_size > current_buffer_size) {
		if(setsockopt(fd, SOL_SOCKET, optname, (const char*)&buffer_size, sizeof(buffer_size)) == 0)
			break;
		// Buffer too large, let's try with half the size...
		buffer_size /=2;
	}

	log(LOG_DEBUG, "udp: %s buffer on fd %d enlarged from %d to %d", bufname, fd, current_buffer_size, buffer_size);
}           


// . returns false and sets g_errno on error
// . use 1 socket for recving and sending
// . pollTime is how often to call timePollWrapper() (in milliseconds)
// . it should be at least the minimal slot timeout
bool UdpServer::init ( uint16_t port, UdpProtocol *proto,
		       int32_t readBufSize , int32_t writeBufSize , 
		       int32_t pollTime , int32_t maxSlots , bool isDns ){

	// save this
	m_isDns = isDns;

	// we now alloc so we don't blow up stack
	if ( m_slots ) {
		g_process.shutdownAbort(true);
	}

	if ( maxSlots < 100           ) maxSlots = 100;
	m_slots =(UdpSlot *)mmalloc(maxSlots*sizeof(UdpSlot),"UdpServer");
	if ( ! m_slots ) {
		log("udp: Failed to allocate %" PRId32" bytes.", maxSlots*(int32_t)sizeof(UdpSlot));
		return false;
	}

	log(LOG_DEBUG,"udp: Allocated %" PRId32" bytes for %" PRId32" sockets.",
	     maxSlots*(int32_t)sizeof(UdpSlot),maxSlots);
	m_maxSlots = maxSlots;

	// dgram size
	log(LOG_DEBUG,"udp: Using dgram size of %" PRId32" bytes.", (int32_t)DGRAM_SIZE);

	// set up linked list of available slots
	m_availableListHead = &m_slots[0];
	for (int32_t i = 0; i < m_maxSlots - 1; i++) {
		m_slots[i].m_availableListNext = &m_slots[i + 1];
	}
	m_slots[m_maxSlots - 1].m_availableListNext = NULL;

	// the linked list of slots in use
	m_activeListHead = NULL;
	m_activeListTail = NULL;

	// linked list of callback candidates
	m_callbackListHead = NULL;

	// . set up hash table that converts key (ip/port/transId) to a slot
	// . m_numBuckets must be power of 2
	m_numBuckets = getHighestLitBitValue ( m_maxSlots * 6 );
	m_bucketMask = m_numBuckets - 1;
	// alloc space for hash table
	m_bufSize = m_numBuckets * sizeof(UdpSlot *);
	m_buf     = (char *)mmalloc ( m_bufSize , "UdpServer" );
	if ( ! m_buf ) {
		log("udp: Failed to allocate %" PRId32" bytes for table.",m_bufSize);
		return false;
	}

	m_ptrs = (UdpSlot **)m_buf;

	// clear
	memset ( m_ptrs , 0 , sizeof(UdpSlot *)*m_numBuckets );
	log(LOG_DEBUG,"udp: Allocated %" PRId32" bytes for table.",m_bufSize);

	m_numUsedSlots   = 0;
	m_numUsedSlotsIncoming   = 0;
	// clear this
	m_isShuttingDown = false;
	// . TODO: IMPORTANT: FIX this to read and save from disk!!!!
	// . NOTE: only need to fix if doing incremental sync/storage??
	m_nextTransId = 0;
	// clear handlers
	memset ( m_handlers, 0 , sizeof(void(* )(UdpSlot *slot,int32_t)) * 128);

    // save the port in case we need it later
    m_port    = port;
	// no requests waiting yet
	m_requestsInWaiting = 0;
	// special count
	m_msg07sInWaiting = 0;
	m_msgc1sInWaiting = 0;
	m_msg25sInWaiting = 0;
	m_msg39sInWaiting = 0;
	m_msg20sInWaiting = 0;
	m_msg0csInWaiting = 0;
	m_msg0sInWaiting  = 0;
	// maintain a ptr to the protocol
	m_proto   = proto;
	// sanity test so we can peek at the rdbid in a msg0 request
	if( ! m_isDns && RDBIDOFFSET +1 > m_proto->getMaxPeekSize() ) {
		g_process.shutdownAbort(true);
	}

	// set up our socket
	m_sock  = socket ( AF_INET, SOCK_DGRAM , 0 );

	if ( m_sock < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		log(LOG_WARN, "udp: Failed to create socket: %s.", mstrerror(g_errno));
		return false;
	}
	// sockaddr_in provides interface to sockaddr
	struct sockaddr_in name;
	// reset it all just to be safe
	memset(&name,0,sizeof(name));
	name.sin_family      = AF_INET;
	name.sin_addr.s_addr = INADDR_ANY;
	name.sin_port        = htons(port);
	// we want to re-use port it if we need to restart
	int options  = 1;
	if ( setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, &options,sizeof(options)) < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		log( LOG_WARN, "udp: Call to  setsockopt: %s.",mstrerror(g_errno));
	    return false;
	}
	// only do this if not dns!!! some dns servers require it and will
	// just drop the packets if it doesn't match, because this will make
	// it always 0
	// NO! we really need this now that we use roadrunner wirless which
	// has bad udp packet checksums all the time!
	// options = 1;
	//if ( ! m_isDns && setsockopt(m_sock, SOL_SOCKET, SO_NO_CHECK, &options,sizeof(options)) < 0 ) {
	//	// copy errno to g_errno
	//	g_errno = errno;
	//	log("udp: Call to  setsockopt: %s.",mstrerror(g_errno));
	//  return false;
	//}

	// the lower the RT signal we use, the higher our priority

	// . before we start getting signals on this socket let's make sure
	//   we have a handler registered with the Loop class
	// . this makes m_sock non-blocking, too
	// . use the original niceness for this
	if ( ! g_loop.registerReadCallback ( m_sock, this, readPollWrapper, 0 )) {
		return false;
	}

	// . also register for writing to the socket, when it's ready
	// . use the original niceness for this, too
	// . what does this really mean? shouldn't we only use it
	//   when we try to write but the write buf is full so we have
	//   to try again later when it becomes unfull?
	// if ( ! g_loop.registerWriteCallback ( m_sock, this, sendPollWrapper, 0 ))
	// 		return false;

	// . also register for 30 ms tix (was 15ms)
	//   but we aren't using tokens any more so I raised it
	// . it's low so we can claim any unclaimed tokens!
	// . now resends are at 20ms... i'd go lower, but sigtimedqueue() only
	//   has a timer resolution of 20ms, probably due to kernel time slicin
	if ( ! g_loop.registerSleepCallback ( pollTime, this, timePollWrapper, 0 )) {
		return false;
	}

	// . set the read buffer size to 256k for high priority socket
	//   so our indexlists don't have to be re-transmitted so much in case
	//   we delay a bit
	// . set after calling socket() but before calling bind() for tcp
	//   because of http://jes.home.cern.ch/jes/gige/acenic.html
	// . do these cmds on the cmd line as root for gigabit ethernet
	// . echo 262144 > /proc/sys/net/core/rmem_max
	// . echo 262144 > /proc/sys/net/core/wmem_max
	//if ( niceness == 0 ) opt = 2*1024*1024 ;
	// print the size of the buffers
	enlargeUdpSocketBufffer(m_sock, "Receive", SO_RCVBUF, readBufSize);
	enlargeUdpSocketBufffer(m_sock, "Send", SO_SNDBUF, writeBufSize);

	// bind this name to the socket
	if ( bind ( m_sock, (struct sockaddr *)(void*)&name, sizeof(name)) < 0) {
		// copy errno to g_errno
		g_errno = errno;
	    //if ( g_errno == EINVAL ) { port++; goto again; }
	    close ( m_sock );
	    log( LOG_WARN, "udp: Failed to bind to port %hu: %s.", port,strerror(g_errno));
	    return false;
	}

	// init stats
	m_eth0BytesIn    = 0LL;
	m_eth0BytesOut   = 0LL;
	m_eth0PacketsIn  = 0LL;
	m_eth0PacketsOut = 0LL;
	m_eth1BytesIn    = 0LL;
	m_eth1BytesOut   = 0LL;
	m_eth1PacketsIn  = 0LL;
	m_eth1PacketsOut = 0LL;

	// for packets coming in from other clusters usually for importing
	// link information
	m_outsiderPacketsIn  = 0LL;
	m_outsiderBytesIn    = 0LL;
	m_outsiderPacketsOut = 0LL;
	m_outsiderBytesOut   = 0LL;

	// log an innocent msg
	//log ( 0, "udp: listening on port %hu with sd=%" PRId32" and "
	//      , m_port, m_sock );
	log ( LOG_INIT, "udp: Listening on UDP port %hu with fd=%i.", m_port, m_sock );
	// print dgram sizes
	//log("udp:  using max dgram size of %" PRId32" bytes", DGRAM_SIZE );
	return true;
}

// . use a backoff of -1 for the default
// . use maxWait of -1 for the default
// . returns false and sets g_errno on error
// . returns true on success
bool UdpServer::sendRequest(char *msg,
                            int32_t msgSize,
                            msg_type_t msgType,
                            uint32_t ip,
                            uint16_t port,
                            int32_t hostId,
                            UdpSlot **retslot, // can be NULL
                            void *state,
                            void    (*callback)(void *state, UdpSlot *slot),
                            int64_t timeout, // in milliseconds
                            int32_t niceness,
                            const char *extraInfo,
                            int16_t backoff,
                            int16_t maxWait,
                            int32_t maxResends) {

	// sanity check
	if ( ! m_handlers[msgType] && msgType != msg_type_dns ) {
		g_process.shutdownAbort(true);
	}

	// NULLify slot if any
	if ( retslot ) {
		*retslot = NULL;
	}

	// if shutting down return an error
	if ( m_isShuttingDown ) { 
		g_errno = ESHUTTINGDOWN; 
		return false; 
	}

	// ensure timeout ok
	if ( timeout < 0 ) { 
		//g_errno = EBADENGINEER;
		log(LOG_LOGIC,"udp: sendrequest: Timeout is negative. ");
		g_process.shutdownAbort(true);
	}

	// . we only allow niceness 0 or 1 now
	// . this niceness is only used for makeCallbacks()
	if ( niceness > 1 ) niceness = 1;
	if ( niceness < 0 ) niceness = 0;

	// set up shotgunning for this hostId
	Host *h = NULL;
	uint32_t ip2 = ip;

	// . now we always set UdpSlot::m_host
	// . hostId is -1 when sending to a host in g_hostdb2 (hosts2.conf)
	if ( hostId >= 0 ) {
		h = g_hostdb.getHost ( hostId );
	}

	// get it from g_hostdb2 then via ip lookup if still NULL
	if ( ! h ) {
		h = g_hostdb.getHost ( ip , port );
	}

	// sanity check
	if ( h && ip && ip != (uint32_t)-1 && h->m_ip != ip && h->m_ipShotgun != ip && ip != 0x0100007f ) { // "127.0.0.1"
		log(LOG_LOGIC,"udp: provided hostid does not match ip");
		g_process.shutdownAbort(true);
	}

	// always use the primary ip for making the key, 
	// do not use the shotgun ip. because we can be getting packets
	// from either ip for the same transaction.
	if ( h ) {
		ip2 = h->m_ip;
	}

	ScopedLock sl(m_mtx);

	// get a new transId
	int32_t transId = getTransId();

	// make a key for this new slot
	key_t key = m_proto->makeKey (ip2,port,transId,true/*weInitiated?*/);

	// . create a new slot to control the transmission of this request
	// . should set g_errno on failure
	UdpSlot *slot = getEmptyUdpSlot(key, false);
	if ( ! slot ) {
		log( LOG_WARN, "udp: All %" PRId32" slots are in use.",m_maxSlots);
		return false;
	}

	logDebug(g_conf.m_logDebugUdp, "udp: sendrequest: ip2=%s port=%" PRId32" msgType=0x%02x msgSize=%" PRId32" "
			 "transId=%" PRId32" (niceness=%" PRId32") slot=%p.",
	         iptoa(ip2),(int32_t)port, (unsigned char)msgType, (int32_t)msgSize,
	         (int32_t)transId, (int32_t)niceness , slot );
	
	// . get time 
	int64_t now = gettimeofdayInMillisecondsLocal();

	// connect to the ip/port (udp-style: does not do much)
	slot->connect(m_proto, ip, port, h, hostId, transId, timeout, now, niceness);

	// . use default callback if none provided
	// . slot has a callback iff it's an outgoing request
	if ( ! callback ) {
		callback = defaultCallbackWrapper;
	}

	// set up for a send
	if (!slot->sendSetup(msg, msgSize, msg, msgSize, msgType, now, state, callback, niceness, backoff, maxWait, extraInfo)) {
		freeUdpSlot(slot);
		log( LOG_WARN, "udp: Failed to initialize udp socket for sending req: %s",mstrerror(g_errno));
		return false;
	}

	if (slot->m_callbackListNext || slot->m_callbackListPrev) {
		g_process.shutdownAbort(true);
	}

	// set this
	slot->m_maxResends = maxResends;

	// keep sending dgrams until we have no more or hit ACK_WINDOW limit
	if ( ! doSending(slot, true /*allow resends?*/, now) ) {
		freeUdpSlot(slot);
		log(LOG_WARN, "udp: Failed to send dgrams for udp socket.");
		return false;
	}

	// let caller know the slot if he wants to
	if ( retslot ) {
		*retslot = slot;
	}

	return true;
}

// returns false and sets g_errno on error, true otherwise
void UdpServer::sendErrorReply(UdpSlot *slot, int32_t errnum) {
	logDebug(g_conf.m_logDebugUdp, "udp: sendErrorReply slot=%p errnum=%" PRId32, slot, errnum);

	// bitch if it is 0
	if ( errnum == 0 ) {
		log(LOG_LOGIC,"udp: sendErrorReply: errnum is 0.");
		g_process.shutdownAbort(true); 
	}

	// clear g_errno in case it was set
	g_errno = 0;

	// make a little msg
	char *msg = slot->m_tmpBuf;
	*(int32_t *)msg = htonl(errnum) ;

	// set the m_localErrno in "slot" so it will set the dgrams error bit
	slot->m_localErrno = errnum;

	sendReply(msg, 4, msg, 4, slot);
}

// . destroys slot on error or completion (frees m_readBuf,m_sendBuf)
// . use a backoff of -1 for the default
void UdpServer::sendReply(char *msg, int32_t msgSize, char *alloc, int32_t allocSize, UdpSlot *slot, void *state,
                          void (*callback2)(void *state, UdpSlot *slot), int16_t backoff, int16_t maxWait,
                          bool isCallback2Hot) {
	logDebug(g_conf.m_logDebugUdp, "udp: sendReply slot=%p", slot);

	// the callback should be NULL
	if ( slot->hasCallback() ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"udp: sendReply: Callback is non-NULL.");
		return;
	}

	if ( ! msg && msgSize > 0 ) {
		log( LOG_WARN, "udp: calling sendreply with null send buffer and positive size! will probably core." );
	}

	// record some statistics on how long these msg handlers are taking
	int64_t now = gettimeofdayInMillisecondsLocal();
	// m_queuedTime should have been set before m_handlers[] was called
	int32_t delta = now - slot->m_queuedTime;
	int32_t n = slot->getNiceness();
	if ( n < 0 ) n = 0;
	if ( n > 1 ) n = 1;
	// add to average, this is now the reply GENERATION, not handler time
	g_stats.m_msgTotalOfHandlerTimes [slot->getMsgType()][n] += delta;
	g_stats.m_msgTotalHandlersCalled [slot->getMsgType()][n]++;
	// bucket number is log base 2 of the delta
	if ( delta > 64000 ) delta = 64000;
	int32_t bucket = getHighestLitBit ( (uint16_t)delta );
	// MAX_BUCKETS is probably 16 and #define'd in Stats.h
	if ( bucket >= MAX_BUCKETS ) bucket = MAX_BUCKETS-1;
	g_stats.m_msgTotalHandlersByTime [slot->getMsgType()][n][bucket]++;
	// we have to use a different clock for measuring how long to
	// send the reply now
	slot->m_queuedTime = now;


	// . get hostid from slot so we can shotgun the reply back
	// . but if sending a ping reply back for PingServer, he wants us
	//   to use the shotgun port iff he did, and not if he did not.
	//   so just make sure slot->m_host is NULL so we send back to the same
	//   ip/port that sent to us.
	//if ( g_conf.m_useShotgun && ! useSameSwitch )
	// now we always set m_host, we use s_shotgun to toggle
	slot->m_host = g_hostdb.getHost ( slot->getIp() , slot->getPort() );
	//else slot->m_host = NULL;

	ScopedLock sl(m_mtx);

	// discount this
	if ( slot->m_convertedNiceness == 1 && slot->getNiceness() == 0 ) {
		logDebug(g_conf.m_logDebugUdp, "udp: unconverting slot=%p", slot);

		// go back to niceness 1 for sending back, otherwise their
		// the callback will be called with niceness 0!!
		//slot->m_niceness = 1;
		slot->m_convertedNiceness = 2;
		m_outstandingConverts--;
	}

	// if msgMaxSize is -1 use msgSize
	//if ( msgMaxSize == -1 ) msgMaxSize = msgSize;

	// . use a NULL callback since we're sending a reply
	// . set up for a send
	if (!slot->sendSetup(msg, msgSize, alloc, allocSize, slot->getMsgType(), now, NULL, NULL, slot->getNiceness(), backoff, maxWait)) {
		log( LOG_WARN, "udp: Failed to initialize udp socket for sending reply: %s", mstrerror(g_errno));
		mfree ( alloc , allocSize , "UdpServer");
		// was EBADENGINEER
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		sendErrorReply ( slot , g_errno);
		return ;
	}
	// set the callback2 , it might not be NULL if we're recording stats
	// OR we need to call Msg21::freeBandwidth() after sending
	slot->m_state          = state;
	slot->m_callback2      = callback2;
	slot->m_isCallback2Hot = isCallback2Hot;
	// set this
	slot->m_maxResends = -1;

	logDebug(g_conf.m_logDebugUdp, "udp: Sending reply transId=%" PRId32" msgType=0x%02x (niceness=%" PRId32").",
	         slot->getTransId(),slot->getMsgType(), (int32_t)slot->getNiceness());
	// keep sending dgrams until we have no more or hit ACK_WINDOW limit
	if ( ! doSending(slot, true /*allow resends?*/, now) ) {
		// . on error deal with that
		// . errors from doSending() are from 
		//   UdpSlot::sendDatagramOrAck()
		//   which are usually errors from sendto() or something
		// . TODO: we may have to destroy this slot ourselves now...
		log(LOG_WARN, "udp: Got error sending dgrams.");
		// destroy it i guess
		destroySlot ( slot );
	}
	// status is 0 if this blocked
	//if ( status == 0 ) return;
	// destroy slot on completion of send or on error
	// mdw destroySlot ( slot );
	// return if send completed
	//if ( status != -1) return;
	// make a log note on send failure
}

// . this wrapper is called when m_sock is ready for writing
// . should only be called by Loop.cpp since it calls callbacks
// . should only be called if in an interrupt or interrupts are off!!
void UdpServer::sendPollWrapper(int fd, void *state) {
	UdpServer *that = static_cast<UdpServer*>(state);
	// begin the read/send/callback loop
	that->sendPoll(true, gettimeofdayInMilliseconds());
}

// . returns false and sets g_errno on error, true otherwise
// . will send an ACK or dgram
// . this is called by sendRequest() which is not async safe
//   and by sendPollWrapper()
// . that means we can be calling doSending() on a slot made in
//   sendRequest() and then be interrupted by sendPollWrapper()
// . Fortunately, we have a lock around it in sendRequest()!
bool UdpServer::doSending(UdpSlot *slot, bool allowResends, int64_t now) {

	// if UdpServer::cancel() was called and this slot's callback was
	// called, make sure to hault sending if we are in a quickpoll
	// interrupt...
	if ( slot->hasCalledCallback() ) {
		log("udp: trying to send on called callback slot");
		return true;
	}

 loop:
	int32_t status = 0;
	// . don't do any sending until we leave the wait state

	// . if the score of this slot is -1, don't send on it!
	// . this now will allow one dgram to be resent even if we don't
	//   have the token
	//int32_t score = slot->getScore(now);
	//log("score is %" PRId32, score);
	if ( slot->getScore(now) < 0 ) goto done;
	//if ( score < 0 ) return true;
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	// . it will send a dgram or an ACK
	status = slot->sendDatagramOrAck ( m_sock , allowResends , now );
	// return 1 if nothing to send
	if ( status == -2 ) goto done;
	// return -1 on error
	if ( status == -1 ) {
		log("udp: Had error sending dgram: %s.",mstrerror(g_errno));
		goto done;
	}
	// return 0 if we blocked on this dgram
	if ( status ==  0 ) {
		// but Loop should call us again asap because I don't think
		// we'll get a ready to write signal... don't count on it
		m_needToSend = true;
		// ok, now it should
		if ( ! m_writeRegistered ) {
			g_loop.registerWriteCallback ( m_sock, this, sendPollWrapper, 0 ); // niceness
			m_writeRegistered = true;
		}
		goto done;
	}
	// otherwise keep looping, we might be able to send more
	goto loop;
	// come here to turn the interrupts back on if we turned them off
 done:
	if ( status == -1 ) {
		return false;
	}
	return true;
}

// . should only be called from process() since this is not re-entrant
// . sends all the awaiting dgrams it can
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . tries to send msgs that are the "most caught up" to their ACKs first
// . call the callback of slots that are TIMEDOUT or get an error!
// . verified that this is not interruptible
// . MDW: THIS IS NOW called by Loop.cpp when our udp socket is ready for
//   sending on, and a previous sendto() would have blocked.
bool UdpServer::sendPoll(bool allowResends, int64_t now) {
	// just so caller knows we don't need to send again yet
	m_needToSend = false;
	// if we don'thave anything to send, or we're waiting on ACKS, then
	// just return false, we didn't do anything.
	// assume we didn't process anything
	bool something = false;
	
	ScopedLock sl(m_mtx);
	for(;;) {
		// . don't do any sending until we leave the wait state
		// or if is shutting down
		if ( m_isShuttingDown )
			return false;
		// . get the next slot to send on
		// . it sets "isResend" to true if it's a resend
		// . this sets g_errno to ETIMEOUT if the slot it returns has timed out
		// . in that case we'll destroy that slot
		UdpSlot *slot = getBestSlotToSend ( now );
		// . slot is NULL if no more slots need sending
		// . return true if we processed something
		if ( ! slot ) {
			// if nobody needs to send now unregister write callback
			// so select() loop in Loop.cpp does not keep freaking out
			if ( ! m_needToSend && m_writeRegistered ) {
				g_loop.unregisterWriteCallback(m_sock, this, sendPollWrapper);
				m_writeRegistered = false;
			}
			return something;
		}
		// otherwise, we can send something
		something = true;
		// . if this slot timed out because we haven't written a reply yet
		//   then DO NOT call the callback again, just wait for the handler
		//   to timeout and send a reply
		// . otherwise, you'll just keep looping the same request to the
		//   same handler and cause problems (mdw)
		// if timed out then nuke it
		//if ( g_errno == ETIMEDOUT ) goto slotDone;
		// . tell slot to send a datagram OR ACK for us
		// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
		//   1 if sent something
		//if(slot->sendDatagramOrAck (m_sock, true, m_niceness) == 0 ) return ;
		// . send all we can from this slot
		// . when shutting down during a dump we can get EBADF during a send
		//   so do not loop forever
		// . this returns false on error, i haven't seen it happen though
		if ( ! doSending(slot, allowResends, now) )
			return true;
	}
}

// . returns NULL if no slots need sending
// . otherwise returns a slot
// . slot may have dgrams or ACKs to send
// . sets g_errno to ETIMEDOUT if that slot is timed out as well as set
//   that slot's m_doneSending to true
// . let's send the shortest first, but weight by how long it's been waiting!
// . f(x) = a*(now - startTime) + b/msgSize
// . verified that this is not interruptible
UdpSlot *UdpServer::getBestSlotToSend ( int64_t now ) {
	m_mtx.verify_is_locked();
	// . we send msgs that are mostly "caught up" with their acks first
	// . the slot with the lowest score gets sent
	// . re-sends have priority over NONre-sends(ACK was not recvd in time)
	int32_t maxScore = -1;
	UdpSlot *maxi = NULL;

  	// . we send dgrams with the lowest "score" first
	// . the "score" is just number of ACKs you're waiting for
	// . that way transmissions that are the most caught up to their ACKs
	//   are considered faster so we send to them first
	// . we set the hi bit in the score for non-resends so dgrams that 
	//   are being resent take precedence
	for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
		// . we don't allow time out on slots waiting for us to send
		//   stuff, because we'd just end up calling the handler
		//   too many times. we could invent a "stop" cmd or something.
		// . mdw

		// . how many acks are we currently waiting for from dgrams
		//   that have already been sent?
		// . can be up to ACK_WINDOW_SIZE (16?).
		// . we're a "Fastest First" (FF) protocol stack.
		int32_t score = slot->getScore ( now );
		// a negative score means it's not a candidate
		if ( score < 0 ) {
			continue;
		}
		// see if we're a winner
		if ( score > maxScore ) {
			maxi = slot;
			maxScore = score;
		}
	}

	// return the winning slot
	return maxi;
}

// . must give level of niceness for continuing the transaction at that lvl
bool UdpServer::registerHandler( msg_type_t msgType, void (* handler)(UdpSlot *, int32_t niceness) ) {
	if (m_handlers[msgType]) {
		log(LOG_LOGIC, "udp: msgType %02x already in use.", msgType);
		return false;
	}

	m_handlers[msgType] = handler;
	return true;
}

// . read and send as much as we can before calling any callbacks
// . if forceCallbacks is true we call them regardless if we read/sent anything
void UdpServer::process(int64_t now, int32_t maxNiceness) {
	// bail if no main sock
	if ( m_sock < 0 ) return ;

	//log("process");

	// if we call this while in the sighandler it crashes since
	// gettimeofdayInMillisecondsLocal() is not async safe
	int64_t startTimer = gettimeofdayInMillisecondsLocal();
 bigloop:
	bool needCallback = false;
 loop:
	// did we read or send something?
	bool something = false;
	// a common var
	UdpSlot *slot;
	// read loop
 readAgain:
	// bail if no main sock, could have been shutdown in the middle
	if ( m_sock < 0 ) return ;
	// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
	// . *slot is set to the slot on which the dgram was read
	// . *slot will be NULL on some errors (read errors or alloc errors)
	// . *slot will be NULL if we read and processed a slotless ACK
	// . *slot will be NULL if we read nothing (0 bytes read & 0 returned)
	int32_t status;
	{
		ScopedLock sl(m_mtx);
		status = readSock(&slot, now);
	}
	// if we read something
	if ( status != 0 ) {
		// if no slot was set, it was a slotless read so keep looping
		if ( ! slot ) { g_errno = 0; goto readAgain; }
		// if there was a read error let makeCallback() know about it
		if ( status == -1 ) {
			slot->m_errno = g_errno;
			// prepare to call the callback by adding it to this
			// special linked list
			if ( g_errno ) {
				ScopedLock sl(m_mtx);
				addToCallbackLinkedList ( slot );
			}
			// sanity
			if ( ! g_errno )
				log("udp: missing g_errno from read error");
		}
		// we read something
		something = true;
		// try sending an ACK on the slot we read something from
		doSending(slot, false, now);
	}
	// if we read something, try for more
	if ( something ) { 
		//if ( slot->m_errno || slot->isTransactionComplete())
		//log("got something");
		needCallback = true; 
		goto loop; 
	}
	// if we don't need a callback, bail
	if ( ! needCallback ) {
		if ( m_needBottom ) goto callBottom;
		else              return;
	}
	// . set flag to call low priority callbacks 
	// . need to force it on here because makeCallbacks() may
	//   return false when there are only low priority (high niceness)
	//   callbacks to call...
	m_needBottom = true;
	// . TODO: if we read/sent nothing don't bother calling callbacks
	// . call any outstanding callbacks
	// . now we have a niceness bit in the dgram header. if set, those 
	//   callback will only be called after all unset dgram's callbacks are
	// . this returns true if we called one
	if ( makeCallbacks(/*niceness level*/ 0) ) {
		// set flag to call low priority callbacks 
		m_needBottom = true;
		// note it
		//log("made callback");
		// but not now, only when we don't call any high priorities
		goto bigloop;
	}
 callBottom:
	if(maxNiceness < 1) return;
	// if we call this while in the sighandler it crashes since
	// gettimeofdayInMillisecondsLocal() is not async safe
	int64_t elapsed = gettimeofdayInMillisecondsLocal() - startTimer;
	if(elapsed < 10) {
		// we did not call any, so resort to nice callbacks
		// . only go to bigloop if we called a callback
		if ( makeCallbacks(/*niceness level*/1) )
			goto bigloop;
		// no longer need to be called
		// if we did anything loop back up
		// . but only if we haven't been looping forever,
		// . if so we need to relinquish control to loop.
		// 		log(LOG_WARN, "udp: give back control. after %" PRId64,
		// 		    elapsed);
		//goto bigloop;	
	}
	else {
		m_needBottom = true;
	}
}

// . this wrapper is called when the Loop class has found that m_sock
//   needs to be read from (it got a SIGIO/GB_SIGRTMIN signal for it)
// . should only be called if in an interrupt or interrupts are off!!
void UdpServer::readPollWrapper(int fd, void *state) {
	UdpServer *that = static_cast<UdpServer*>(state);
	// begin the read/send/callback loop
	that->process(gettimeofdayInMilliseconds());
}


// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
int32_t UdpServer::readSock(UdpSlot **slotPtr, int64_t now) {
	// NULLify slot
	*slotPtr = NULL;
	sockaddr_in from;
	socklen_t fromLen = sizeof ( struct sockaddr );
	char readBuffer[64*1024];
	int readSize = recvfrom ( m_sock,
				  readBuffer,
				  sizeof(readBuffer),
				  0,                        //flags
				  (sockaddr *)(void*)&from,
				  &fromLen);

	logDebug(g_conf.m_logDebugLoop, "loop: readsock: readSize=%i m_sock/fd=%i", readSize,m_sock);

	// cancel silly g_errnos and return 0 since we blocked
	if ( readSize < 0 ) {
		g_errno = errno;

		if ( g_errno == 0 || g_errno == EILSEQ || g_errno == EAGAIN ) {
			g_errno = 0;
			return 0;
		}

		// Interrupted system call (4) (from valgrind)
		log( LOG_WARN, "udp: readDgram: %s (%d).", mstrerror( g_errno ), g_errno );
		return -1;
	}

	uint32_t ip2;
	Host *h;
	key_t key;
	UdpSlot *slot;
	int32_t dgramNum;
	bool wasAck;
	int32_t transId;
	bool discard = true;
	bool status;
	msg_type_t msgType;
	int32_t niceness;

	// get the ip
	uint32_t ip = from.sin_addr.s_addr;
	// if it's 127.0.0.1 then change it to our ip
	if ( ip == g_hostdb.getLoopbackIp() ) ip = g_hostdb.getMyIp();
	// . if ip is not from a host in hosts.conf, discard it
	// . don't bother checking for dns server, who knows where that is
	// . now also allow all admin ips
	else if ( m_proto->useAcks() &&
		  ! is_trusted_protocol_ip(ip) &&
		  ! g_hostdb.isIpInNetwork ( ip ) &&
		  ! g_conf.isMasterIp ( ip ) &&
		  ! g_conf.isConnectIp ( ip ) ) {
		// bitch, wait at least 5 seconds though
		static int32_t s_lastTime = 0;
		static int64_t s_count = 0LL;
		s_count++;
		if ( getTime() - s_lastTime > 5 ) {
			s_lastTime = getTime();
			log(LOG_WARN, "udp: Received unauthorized udp packet from %s. Count=%" PRId64".",iptoa(ip),s_count);
		}
		// make it return 1 cuz we did read something
		status = true;
		// not an ack? assume not
		wasAck = false;
		// assume no shotgun
		h      = NULL;
		// discard it
		discard = true;
		// read it into the temporary discard buf
		goto discard;
	}
	// get hostid of the ip, use that instead of ip to make the key
	// since shotgunning may change the ip
	ip2 = ip;
	// i modified Hostdb::hashHosts() to hash the loopback ip now!
	h   = g_hostdb.getHost ( ip , ntohs(from.sin_port) );
	// . just use ip for hosts from hosts2.conf
	// . because sendReques() usually gets a hostId of -1 when sending
	//   to a host in hosts2.conf and therefore makeKey() initially uses
	//   the ip address of the hosts2.conf host
	//if ( h && h->m_hostdb != &g_hostdb ) h = NULL;
	// probably a reply from a dns server?
	//if ( ! h ) { g_process.shutdownAbort(true); }
	// always use the primary ip for making the key, 
	// do not use the shotgun ip. because we can be getting packets
	// from either ip for the same transaction. h can be NULL if the packet
	// is from a dns server.
	if ( h ) ip2 = h->m_ip;
	//logf(LOG_DEBUG,"net: got h=%" PRIu32,(int32_t)h);
	// generate a unique KEY for this TRANSACTION 
	key = m_proto->makeKey ( readBuffer            ,
				 readSize              ,
				 //from.sin_addr.s_addr, // network order
				 ip2                   , // ip        ,
				 //ip                  , // network order
				 ntohs(from.sin_port)  );// host order
	// get the corresponding slot for this key, if it exists
	slot = getUdpSlot ( key );
	// get the dgram number on this dgram
	dgramNum = m_proto->getDgramNum ( readBuffer, readSize );
	// was it an ack?
	wasAck   = m_proto->isAck       ( readBuffer, readSize );
	// everybody has a transId
	transId  = m_proto->getTransId  ( readBuffer, readSize );
	// other vars we'll use later
	discard = true;
	status  = true;
	// if we don't already have a slot set up for it then it can be:
	// #1) a new incoming request
	// #2) a reply we ACKed but it didn't get our ACK and we've closed
	// #3) a stray ACK???
	// #4) a reply but we timed out and our slot is gone
	msgType = static_cast<msg_type_t>(m_proto->getMsgType(readBuffer, readSize));
	niceness = m_proto->isNice     ( readBuffer, readSize );
	// general count
	if ( niceness == 0 ) {
		g_stats.m_packetsIn[msgType][0]++;
		if ( wasAck ) g_stats.m_acksIn[msgType][0]++;
	}
	else {
		g_stats.m_packetsIn[msgType][1]++;
		if ( wasAck ) g_stats.m_acksIn[msgType][1]++;
	}
	// if we're shutting down do not accept new connections, discard
	if ( m_isShuttingDown ) goto discard; 
	if ( ! slot ) {
		// condition #3
		if ( wasAck ) {
			if ( g_conf.m_logDebugUdp )
				log(LOG_DEBUG,
				    "udp: Read stray ACK, transId=%" PRId32", "
				    "ip2=%s "
				    "port=%" PRId32" "
				    "dgram=%" PRId32" "
				    "dst=%s:%hu "
				    "k.n1=%" PRIu32" n0=%" PRIu64".",
				    transId,
				    iptoa(ip2),
				    (int32_t)ntohs(from.sin_port) ,
				    dgramNum,
				    iptoa(ip)+6,
				    (uint16_t)ntohs(from.sin_port),
				    key.n1,key.n0);
			// tmp debug
			//g_process.shutdownAbort(true);
			//return 1;
			goto discard;
		}
		// condition #2
		if ( m_proto->isReply ( readBuffer, readSize ) ) {
			// if we don't use ACK then do nothing!
			if ( ! m_proto->useAcks () ) {
				// print out the domain in the packet
				/*
				char tmp[512];
				g_dnsDistributed.extractHostname(header,dgram+12,tmp);
				// iptoa not async sig safe
				log("udp: dns reply too late "
				     "or reply from a resend "
				     "(host=%s,dnsip=%s)",
				     tmp, iptoa(ip)); 
				*/
				log(LOG_REMIND,"dns: Dns reply too late "
				     "or reply from a resend.");
				//return 1; 
				goto discard;
			}
			// . if they didn't get our ACK they might resend to us
			//   even though we think the transaction is completed
			// . either our send is slow or their read buf is slow
			// . to avoid these msg crank up the resend time
			// . Multicast likes to send you AND your groupees
			//   the same request, take the first reply it gets
			//   and dump the rest, this is probably why we get 
			//   this often
			if ( g_conf.m_logDebugUdp )
				log(LOG_DEBUG,
				    "udp: got dgram we acked, but we closed, "
				    "transId=%" PRId32" dgram=%" PRId32" dgramSize=%i "
				    "fromIp=%s fromPort=%i msgType=0x%02x",
				    transId, dgramNum , readSize,
				    iptoa((int32_t)from.sin_addr.s_addr) , 
				    ntohs(from.sin_port) , msgType );
		cancelTrans:
			// temporary slot for sending back bogus ack
			UdpSlot tmp;
			// . send them another ACK so they shut up
			// . they might not have gotten due to network error
			// . this will clear "tmp" with memset
			tmp.connect (m_proto,&from,NULL,-1,transId, 10000/*timeout*/,
				      now , 0 ); // m_niceness );
			// . if this blocks, that sucks, we'll probably get
			//   another untethered read... oh well...
			// . ack from 0 to infinite to prevent more from coming
			tmp.sendAck(m_sock,now,dgramNum,true/*weInit'ed?*/,
				    true/*cancelTrans?*/);
			//return 1;
			goto discard;
		}
		// . if we're shutting down do not accept new connections
		// . just ignore
		if ( m_isShuttingDown ) goto discard; // return 1;
		// shortcut
		bool isProxy = g_proxy.isProxy();
		// do not read any incoming request if half the slots are
		// being used for incoming requests right now. we don't want
		// to lose all of our memory. MDW
		bool getSlot = true;
		if ( msgType == msg_type_7 && m_msg07sInWaiting >= 100 )
			getSlot = false;

		// crawl update info from Spider.cpp
		if ( msgType == msg_type_c1 && m_msgc1sInWaiting >= 100 )
			getSlot = false;

		// msg25 spawns an indexdb request lookup and unless we limit
		// the msg25 requests we can jam ourslves if all the indexdb
		// lookups hit ourselves... we won't have enough free slots
		// to answer the msg0 indexdb lookups!
		if ( msgType == msg_type_25 && m_msg25sInWaiting >= 70 )
			getSlot = false;

		// . i've seen us freeze up from this too
		// . but only drop spider's msg39s
		if ( msgType == msg_type_39 && m_msg39sInWaiting >= 10 && niceness )
			getSlot = false;
		// try to prevent another lockup condition of msg20 spawing
		// a msg22 request to self but failing...
		if ( msgType == msg_type_20 && m_msg20sInWaiting >= 50 && niceness )
			getSlot = false;

		// . msg13 is clogging thiings up when we synchost a host
		//   and it comes back up
		// . allow spider compression proxy to have a bunch
		// . MDW: do we need this one anymore? relax it a little.
		if ( msgType == msg_type_13 && m_numUsedSlotsIncoming>400 &&
		     m_numUsedSlots>800 && !isProxy)
			getSlot = false;

		// . avoid slamming thread queues with sectiondb disk reads
		// . mdw 1/22/2014 take this out now too, we got ssds
		//   let's see if taking this out fixes the jam described
		//   below
		// . mdw 1/31/2014 got stuck doing linktext 0x20 lookups 
		//   leading to tagdb lookups with not enough slots left!!! 
		//   so decrease 0x20
		//   and/or increase 0x00. ill boost from 500 to 1500 
		//   although i
		//   think we should limit the msg20 niceness 1 requests really
		//   when slot usage is high... ok, i changed Msg25.cpp to only
		//   allow 1 msg20 out if 300+ sockets are in use.
		// . these kinds of techniques ultimately just end up
		//   in loop, the proper way is to throttle back the # of
		//   outstanding tagdb lookups or whatever at the source
		//   otherwise we jam up
		// . tagdb lookups were being dropped because of this being
		//   500 so i raised to 900. a lot of them were from
		//   'get outlink tag recs' or 'get link info' (0x20)
		if ( msgType == msg_type_0 && m_numUsedSlots > 1500 && niceness ) {
			// allow a ton of those tagdb lookups to come in
			char rdbId = 0;
			if ( readSize > RDBIDOFFSET )
				rdbId = readBuffer[RDBIDOFFSET];
			if ( rdbId != RDB_TAGDB )
				getSlot = false;
		}

		// lower priorty slots are dropped first
		if ( m_numUsedSlots >= 1300 && niceness > 0 && ! isProxy &&
		     // we dealt with special tagdb msg00's above so
		     // do not deal with them here
		     msgType != msg_type_0 )
			getSlot = false;

		// . reserve 300 slots for outgoing query-related requests
		// . this was 1700, but the max udp slots is set to 3500
		//   in main.cpp, so let's up this to 2300. i don't want to
		//   drop stuff like Msg39 because it takes 8 seconds before
		//   it is re-routed in Multicast.cpp! now that we show what
		//   msgtypes are being dropped exactly in PageStats.cpp we
		//   will know if this is hurting us.
		if ( m_numUsedSlots >= 2300 && ! isProxy ) getSlot = false;
		// never drop ping packets! they do not send out requests
		if ( msgType == msg_type_11 ) getSlot = true;
		// getting a titlerec does not send out a 2nd request. i really
		// hate those title rec timeout msgs.
		if ( msgType == msg_type_22 && niceness == 0 ) getSlot = true;
		
		if ( getSlot ) 
			// get a new UdpSlot
			slot = getEmptyUdpSlot(key, true);
		// return -1 on failure
		if ( ! slot ) {
			// return -1
			status = false;
			// discard it!
			// only log this message up to once per second to avoid
			// flooding the log
			static int64_t s_lastTime = 0LL;
			g_dropped++;
			// count each msgType we drop
			if ( niceness == 0 ) g_stats.m_dropped[msgType][0]++;
			else                 g_stats.m_dropped[msgType][1]++;
			if ( now - s_lastTime >= 1000 ) {
				s_lastTime = now;
				log(LOG_INFO, "udp: No udp slots to handle datagram. (msgType=0x%x niceness=%" PRId32") "
				    "Discarding. It should be resent. Dropped dgrams=%" PRId32".", msgType,niceness,g_dropped);
			}
			goto discard;
		}
		// default timeout, sender has 60 seconds to send request!
		int64_t timeout = 60000;
		// connect this slot (callback should be NULL)
		slot->connect ( m_proto ,  
				&from   ,  // ip/port
				// we now put in the host, which may be NULL
				// if not in cluster, but we need this for
				// keeping track of dgrams sent/read to/from
				// this host (Host::m_dgramsTo/From)
				h       , // NULL    ,  // hostPtr
				-1      ,  // hostId
				transId ,  
				timeout      ,  // timeout in 60 secs
				now     ,
				// . slot->m_niceness should be set to this now
				// . originally m_niceness is that of this udp
				//   server, and we were using it as the slot's
				//   but it should be correct now...
				niceness ); // 0 // m_niceness );
		// don't count ping towards this
		if ( msgType != msg_type_11 ) {
			// if we connected to a request slot, count it
			m_requestsInWaiting++;
			// special count
			if ( msgType == msg_type_7 ) m_msg07sInWaiting++;
			if ( msgType == msg_type_c1 ) m_msgc1sInWaiting++;
			if ( msgType == msg_type_25 ) m_msg25sInWaiting++;
			if ( msgType == msg_type_39 ) m_msg39sInWaiting++;
			if ( msgType == msg_type_20 ) m_msg20sInWaiting++;
			if ( msgType == msg_type_c ) m_msg0csInWaiting++;
			if ( msgType == msg_type_0 ) m_msg0sInWaiting++;
			// debug msg
			//log("in waiting up to %" PRId32,m_requestsInWaiting );
			//log("in waiting up to %" PRId32" (0x%02x) ",
			//     m_requestsInWaiting, slot->m_msgType );
		}

	}
	// let caller know the slot associated with reading this dgram
	*slotPtr = slot;
	// . it returns false and sets g_errno on error
	discard  = false;

	// . HACK: kinda. 
	// . change the ip we reply on to wherever the sender came from!
	// . because we know that that eth port is mostly likely the best
	// . that way if he resends a request on a different ip because we 
	//   did not ack him because the eth port went down, we need to send
	//   our ack on his changed src ip. really only the sendAck() routine
	//   uses this ip, because the send datagram thing will send on the
	//   preferred eth port, be it eth0 or eth1, based on if it got a 
	//   timely ACK or not.
	// . pings should never switch ips though... this was causing
	//   Host::m_inProgress1 to be unset instead of m_inProgress2 and
	//   we were never able to regain a dead host on eth1 in PingServer.cpp
	if ( ip != slot->getIp() && slot->getMsgType() != msg_type_11 ) {
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,"udp: changing ip to %s for acking",
			    iptoa(ip));
		slot->m_ip = ip;
	}

	//if ( ! slot->m_host ) { g_process.shutdownAbort(true);}
	status   = slot->readDatagramOrAck(readBuffer,readSize,now,&discard);

	// we we could not allocate a read buffer to hold the request/reply
	// just send a cancel ack so the send will call its callback with
	// g_errno set
	// MDW: it won't make it into the m_callbackListHead linked list with
	// this logic.... maybe it just times out or resends later...
	if ( ! status && g_errno == ENOMEM ) goto cancelTrans;

	// if it is now a complete REPLY, callback will need to be called
	// so insert into the callback linked list, m_callbackListHead.
	// we have to put slots with NULL callbacks in here since they
	// are incoming requests to handle.
	if ( //slot->m_callback && 
	     // if we got an error reading the reply (or sending req?) then
	     // consider it completed too?
	     // ( slot->isTransactionComplete() || slot->m_errno ) &&
	    ( slot->isDoneReading() || slot->getErrno() ) ) {
		// prepare to call the callback by adding it to this
		// special linked list
		addToCallbackLinkedList ( slot );
	}


	//	if(g_conf.m_sequentialProfiling) {
	// 		if(slot->isDoneReading()) 
	// 			log(LOG_TIMING, "admin: read last dgram: "
	//                           "%" PRId32" %s", slot->getNiceness(),peek);
	//	}

 discard:
	// . update stats, just put them all in g_udpServer
	// . do not count acks
	// . do not count discarded dgrams here
	if ( ! wasAck && readSize > 0 ) {
		// in case shotgun ip equals ip, check this first
		if ( h && h->m_ip == ip ) {
			g_udpServer.m_eth0PacketsIn += 1;
			g_udpServer.m_eth0BytesIn   += readSize;
		}
		// it can come from outside the cluster so check this
		else if ( h && h->m_ipShotgun == ip ) {
			g_udpServer.m_eth1PacketsIn += 1;
			g_udpServer.m_eth1BytesIn   += readSize;
		}
		// count packets to/from hosts outside separately usually
		// for importing link information. this can be from the dns
		// quite often!!
		else {
			//log("ip=%s",iptoa(ip));
			g_udpServer.m_outsiderPacketsIn += 1;
			g_udpServer.m_outsiderBytesIn   += readSize;
		}
	}
	// return -1 on error
	if ( ! status ) return -1;
	// . return 1 cuz we did read the dgram ok
	// . if we read a dgram, ACK will be sent in readPoll() after we return
	return 1;
}		

// . try calling makeCallback() on all slots
// . return true if we called one
// . this is basically double entrant!!! CAUTION!!!
// . if niceness is 0 we may be in a quickpoll or may not be. but we
//   will not enter a quickpoll in that case.
// . however, if we are in a quickpoll and call makeCallbacks then
//   it will use niceness 0 exclusively, but the function that was niceness
//   1 and called quickpoll may itself have been indirectly in 
//   makeCallbacks(1), so we have to make sure that if we change the
//   linked list here, we make sure the parent adjusts.
// . the problem is when we call this with niceness 1 and we convert
//   a niceness 1 callback to 0...
bool UdpServer::makeCallbacks(int32_t niceness) {
	// if nothing to call, forget it
	if (!m_callbackListHead) {
		return false;
	}

	logDebug(g_conf.m_logDebugUdp, "udp: makeCallbacks: start. nice=%" PRId32, niceness);

	// assume noone called
	int32_t numCalled = 0;
	if(niceness > 0) m_needBottom = false;

	bool doNicenessConversion = true;

	// this stops merges from getting done because the write threads never
	// get launched
	if ( g_numUrgentMerges )
		doNicenessConversion = false;

	// or if saving or something
	if ( g_process.m_mode )
		doNicenessConversion = false;

	int64_t startTime = gettimeofdayInMillisecondsLocal();

	ScopedLock sl(m_mtx);

 fullRestart:

	// take care of certain handlers/callbacks before any others
	// regardless of niceness levels because these handlers are so fast
	int32_t pass = 0;

 nextPass:

	UdpSlot *nextSlot = NULL;

	// only scan those slots that are ready
	for ( UdpSlot *slot = m_callbackListHead ; slot ; slot = nextSlot ) {
		// because makeCallback() can delete the slot, use this
		nextSlot = slot->m_callbackListNext;
		// call quick handlers in pass 0, they do not take any time
		// and if they do not get called right away can cause this host
		// to bottleneck many hosts
		if ( pass == 0 ) {
			// only call handlers in pass 0, not reply callbacks
			if ( slot->hasCallback() ) continue;
			// only call certain msg handlers...
			if ( slot->getMsgType() != msg_type_11 &&  // ping
			     slot->getMsgType() != msg_type_1 &&  // add  RdbList
			     slot->getMsgType() != msg_type_0   ) // read RdbList
				continue;
			// BUT the Msg1 list to add has to be small! if it is
			// big then it should wait until later.
			if ( slot->getMsgType() == msg_type_1 &&
			     slot->m_readBufSize > 150 ) continue;
			// only allow niceness 0 msg 0x00 requests here since
			// we call a msg8a from msg20.cpp summary generation
			// which uses msg0 to read tagdb list from disk
			if ( slot->getMsgType() == msg_type_0 && slot->getNiceness() ) {
				// to keep udp slots from clogging up with 
				// tagdb reads allow even niceness 1 tagdb 
				// reads through. cache rate should be super
				// higher and reads short.
				char rdbId = 0;
				if ( slot->m_readBuf &&
				     slot->m_readBufSize > RDBIDOFFSET ) 
					rdbId = slot->m_readBuf[RDBIDOFFSET];
				if ( rdbId != RDB_TAGDB )
					continue;
			}
		}

		// skip if not level we want
		if ( niceness <= 0 && slot->getNiceness() > 0 && pass>0) continue;
		// set g_errno before calling
		g_errno = slot->getErrno();
		// if we got an error from him, set his stats
		Host *h = NULL;
		if ( g_errno && slot->getHostId() >= 0 )
			h = g_hostdb.getHost ( slot->getHostId() );
		if ( h ) {
			h->m_errorReplies++;
			if ( g_errno == ETRYAGAIN ) 
				h->m_pingInfo.m_etryagains++;
		}

		// try to call the callback for this slot
		// time it now
		int64_t start2 = 0;
		bool logIt = false;
		if ( slot->getNiceness() == 0 ) logIt = true;
		if ( logIt ) start2 = gettimeofdayInMillisecondsLocal();

		logDebug(g_conf.m_logDebugUdp,"udp: calling callback/handler for slot=%p pass=%" PRId32" nice=%" PRId32,
		         slot, (int32_t)pass,(int32_t)slot->getNiceness());

		// . crap, this can alter the linked list we are scanning
		//   if it deletes the slot! yes, but now we use "nextSlot"
		// . return false on error and sets g_errno, true otherwise
		// . return true if we called one
		// . skip to next slot if did not call callback/handler
		pthread_mutex_unlock(&m_mtx.mtx);
		if (!makeCallback(slot)) {
			pthread_mutex_lock(&m_mtx.mtx);
			continue;
		}
		pthread_mutex_lock(&m_mtx.mtx);

		// remove it from the callback list to avoid re-call
		removeFromCallbackLinkedList(slot);

		int64_t took = logIt ? (gettimeofdayInMillisecondsLocal()-start2) : 0;
		if ( took > 1000 || (slot->getNiceness()==0 && took>100))
			logf(LOG_DEBUG,"udp: took %" PRId64" ms to call "
			     "callback/handler for "
			     "msgtype=0x%" PRIx32" "
			     "nice=%" PRId32" "
			     "callback=%p",
			     took,
			     (int32_t)slot->getMsgType(),
			     (int32_t)slot->getNiceness(),
			     slot->m_callback);
		numCalled++;

		// log how long callback took
		if(niceness > 0 && 
		   (gettimeofdayInMillisecondsLocal() - startTime) > 5 ) {
			//bail if we're taking too long and we're a 
			//low niceness request.  we can always come 
			//back.
			//TODO: call sigqueue if we need to
			//now we just tell loop to poll
			//if(g_conf.m_sequentialProfiling) {
			//	log(LOG_TIMING, "admin: UdpServer spent "
			//	    "%" PRId64" ms doing"
			//	    " %" PRId32" low priority callbacks."
			//	    " last was:  %s", 
			//	    elapsed, numCalled, 
			//	    g_profiler.getFnName(cbAddr));
			//}
			m_needBottom = true;
			// now we just finish out the list with a 
			// lower niceness
			//niceness = 0;
			return numCalled;
		}

		// CRAP, what happens is we are not in a quickpoll,
		// we call some handler/callback, we enter a quickpoll,
		// we convert him, send him, delete him, then return
		// back to this function and the linked list is
		// altered because we double entered this function
		// from within a quickpoll. so if we are not in a 
		// quickpoll, we have to reset the linked list scan after
		// calling makeCallback(slot) below.
		goto fullRestart;
	}
	// clear
	g_errno = 0;

	// if we just did pass 0 now we do pass 1
	if ( ++pass == 1 ) goto nextPass;	

	return numCalled;
}


// . return false on error and sets g_errno, true otherwise
// . g_errno may already be set when this is called... that's the reason why
//   it was called
// . this is also called by readTimeoutPoll()
// . IMPORTANT: call this every time after you read or send a dgram/ACK
// .            or when g_errno gets set
// . return true if we called one
bool UdpServer::makeCallback(UdpSlot *slot) {
	// get msgType
	msg_type_t msgType = slot->getMsgType();
	// . if we are the low priority server we do not make callbacks
	//   until there are no ongoing transactions in the high priority 
	//   server
	// . BUT, we are always allowed to call Msg0's m_callback2 so we can
	//   give back the bandwidth token (via Msg21) HACK!
	// . undo this for now

	// watch out for illegal msgTypes
	//if ( msgType < 0 ) {
	//	log(LOG_LOGIC,"udp: makeCallback: Illegal msgType.");
	//	return false; 
	//}

	// for timing callbacks and handlers
	int64_t start = 0;
	int64_t now ;
	int32_t delta , n , bucket;
	int32_t saved;
	bool saved2;
	//bool incInt;

	// debug timing
	if ( g_conf.m_logDebugUdp )
		start = gettimeofdayInMillisecondsLocal();

	// callback is non-NULL if we initiated the transaction 
	if ( slot->hasCallback() ) {

		// assume the slot's error when making callback
		// like EUDPTIMEDOUT
		if ( ! g_errno ) {
			g_errno = slot->getErrno();
		}

		// . if transaction has not fully completed, bail
		// . unless there was an error
		// . g_errno could be ECANCELLED
		if ( ! g_errno && ! slot->isTransactionComplete()) {
			//log("udp: why calling callback when not ready???");
			return false;
		}
		
		// debug msg
		if ( g_conf.m_logDebugUdp ) {
			int64_t now  = gettimeofdayInMillisecondsLocal();
			int64_t took = now - slot->getStartTime();
			//if ( took > 10 )
			int32_t Mbps = 0;
			if ( took > 0 ) Mbps = slot->m_readBufSize / took;
			Mbps = (Mbps * 1000) / (1024*1024);
			log(LOG_DEBUG,"udp: Got reply transId=%" PRId32" "
			    "msgType=0x%02x "
			    "g_errno=%s "
			    "niceness=%" PRId32" "
			    "callback=%p "
			    "took %" PRId64" ms (%" PRId32" Mbps).",
			    slot->getTransId(),msgType,
			    mstrerror(g_errno),
			    slot->getNiceness(),
			    slot->m_callback ,
			    took , Mbps );
			start = now;
		}
		// mark it in the stats for PageStats.cpp
		if      ( g_errno == EUDPTIMEDOUT )
			g_stats.m_timeouts[msgType][slot->getNiceness()]++;
		else if ( g_errno == ENOMEM ) 
			g_stats.m_nomem[msgType][slot->getNiceness()]++;
		else if ( g_errno ) 
			g_stats.m_errors[msgType][slot->getNiceness()]++;

		if ( g_conf.m_maxCallbackDelay >= 0 )//&&slot->m_niceness==0) 
			start = gettimeofdayInMillisecondsLocal();

		// sanity check for double callbacks
		if ( slot->hasCalledCallback() ) {
			g_process.shutdownAbort(true);
		}

		slot->m_calledCallback = true;

		// now we got a reply or an g_errno so call the callback

		if ( g_conf.m_logDebugLoop && slot->getMsgType() != msg_type_11 )
			log(LOG_DEBUG,"loop: enter callback for 0x%" PRIx32" "
			    "nice=%" PRId32,(int32_t)slot->getMsgType(),slot->getNiceness());


		// . sanity check - if in a high niceness callback, we should
		//   only be calling niceness 0 callbacks here
		//   NOTE: calling UdpServer::cancel() is an exception
		// . no, because Loop.cpp calls udpserver's callback on its
		//   fd with niceness 0, and it in turn can call niceness 1
		//   udp slots
		//if(g_niceness==0 && slot->m_niceness && g_errno!=ECANCELLED){
		//	g_process.shutdownAbort(true);}

		// sanity check. has this slot been excised from linked list?
		if (slot->m_activeListPrev && slot->m_activeListPrev->m_activeListNext != slot) {
			g_process.shutdownAbort(true);
		}

		// sanity check. has this slot been excised from linked list?
		if (slot->m_activeListPrev && slot->m_activeListPrev->m_activeListNext != slot) {
			g_process.shutdownAbort(true);
		}

		// save niceness
		saved = g_niceness;
		// set it
		g_niceness = slot->getNiceness();
		// make sure not 2
		if ( g_niceness >= 2 ) g_niceness = 1;

		slot->m_callback(slot->m_state, slot);

		// restore it
		g_niceness = saved;

		if ( g_conf.m_logDebugLoop && slot->getMsgType() != msg_type_11 )
			log(LOG_DEBUG,"loop: exit callback for 0x%" PRIx32" "
			    "nice=%" PRId32,(int32_t)slot->getMsgType(),slot->getNiceness());

		if ( g_conf.m_maxCallbackDelay >= 0 ) {
			int64_t elapsed = gettimeofdayInMillisecondsLocal()-
				start;
			if ( slot->getNiceness() == 0 &&
			     elapsed >= g_conf.m_maxCallbackDelay )
				log("udp: Took %" PRId64" ms to call "
				    "callback for msgType=0x%02x niceness=%" PRId32,
				    elapsed,slot->getMsgType(),
				    (int32_t)slot->getNiceness());
		}

		// time it
		if ( g_conf.m_logDebugUdp )
			log(LOG_DEBUG,"udp: Reply callback took %" PRId64" ms.",
			    gettimeofdayInMillisecondsLocal() - start );
		// clear any g_errno that may have been set
		g_errno = 0;
		// . now lets destroy the slot, bufs and all
		// . if the caller wanted to hang on to request or reply then
		//   it should NULLify slot->m_sendBuf and/or slot->m_readBuf
		destroySlot ( slot );
		return true;
	}
	// don't repeat call the handler if we already called it
	if ( slot->hasCalledHandler() ) {
		// . if transaction has not fully completed, keep sending
		// . unless there was an error
		if ( ! g_errno && 
		     ! slot->isTransactionComplete() &&
		     ! slot->getErrno() ) {
			if ( g_conf.m_logDebugUdp )
				log("udp: why calling handler "
				    "when not ready?");
			return false;
		}
		
		// . now we sent the reply so try calling callback2
		// . this is usually NULL, but so I could make pretty graphs
		//   of transmission time it won't be
		// . if callback2 is hot it will be called here, possibly,
		//   more than once, but we also call m_callback2 later, too,
		//   since we cannot call destroySlot() in a hot sig handler
		if ( slot->m_callback2 ) {
			// . since we can be re-entered by the sig handler
			//   make sure he doesn't call this callback while
			//   we are in the middle of it
			// . but if we're in a sig handler now, this will
			//   have to be called again to destroy the slot, so
			//   this only prevents an extra callback from a 
			//   sig handler really
			slot->m_isCallback2Hot = false;

			if ( g_conf.m_logDebugLoop )
				log(LOG_DEBUG,"loop: enter callback2 for "
				    "0x%" PRIx32,(int32_t)slot->getMsgType());

			// call it
			slot->m_callback2 ( slot->m_state , slot ); 

			if ( g_conf.m_logDebugLoop )
				log(LOG_DEBUG,"loop: exit callback2 for 0x%" PRIx32,
				    (int32_t)slot->getMsgType());

			// debug msg
			if ( g_conf.m_logDebugUdp ) {
				int64_t now  = 
					gettimeofdayInMillisecondsLocal();
				int64_t took = now - start ;
				//if ( took > 10 )
					log(LOG_DEBUG,
					    "udp: Callback2 transId=%" PRId32" "
					    "msgType=0x%02x "
					    "g_errno=%s callback2=%p"
					    " took %" PRId64" ms.",
					    slot->getTransId(),msgType,
					    mstrerror(g_errno),
					    slot->m_callback2,
					    took );
			}
			// clear any g_errno that may have been set
			g_errno = 0;
		}
		// nuke the slot, we gave them a reply...
		destroySlot ( slot );
		//log("udp: why double calling handler?");
		// this kind of callback doesn't count
		return false;
	}
	// . if we're not done reading the request, don't call the handler
	// . we now destroy it if the request timed out
	if ( ! slot->isDoneReading () ) {
		// . if g_errno not set, keep reading the new request
		// . otherwise it's usually EUDPTIMEOUT, set by readTimeoutPoll
		// . multicast will abandon sending a request if it doesn't
		//   get a response in X seconds, then it may move on to 
		//   using another transaction id to resend the request
		if ( ! g_errno ) return false;
		// log a msg
		log(LOG_LOGIC,
		    "udp: makeCallback: Requester stopped sending: %s.",
		    mstrerror(g_errno));
		// . nuke the half-ass request slot
		// . now if they continue later to send this request we
		//   will auto-ACK the dgrams, but we won't send a reply and
		//   the requester will time out waiting for the reply
		destroySlot ( slot );
		return false;
	}
	// save it
	saved2 = g_inHandler;
	// flag it so Loop.cpp does not re-nice quickpoll niceness
	g_inHandler = true;
	// . otherwise it was an incoming request we haven't answered yet
	// . call the registered handler to handle it
	// . bail if no handler
	if ( ! m_handlers [ msgType ] ) {
		log(LOG_LOGIC,
		    "udp: makeCallback: Recvd unsupported msg type 0x%02x."
		    " Did you forget to call registerHandler() for your "
		    "message class from main.cpp?", (char)msgType);
		g_inHandler = false;
		destroySlot ( slot );
		return false;
	}
	// let loop.cpp know we're done then
	g_inHandler = saved2;

	// debug msg
	if ( g_conf.m_logDebugUdp )
		log(LOG_DEBUG,"udp: Calling handler for transId=%" PRId32" "
		    "msgType=0x%02x.", slot->getTransId() , msgType );


	// record some statistics on how long this was waiting to be called
	now = gettimeofdayInMillisecondsLocal();
	delta = now - slot->m_queuedTime;
	// sanity check
	if ( slot->m_queuedTime == -1 ) { g_process.shutdownAbort(true); }
	n = slot->getNiceness();
	if ( n < 0 ) n = 0;
	if ( n > 1 ) n = 1;
	// add to average
	g_stats.m_msgTotalOfQueuedTimes [msgType][n] += delta;
	g_stats.m_msgTotalQueued        [msgType][n]++;
	// bucket number is log base 2 of the delta
	if ( delta > 64000 ) delta = 64000;
	bucket = getHighestLitBit ( (uint16_t)delta );
	// MAX_BUCKETS is probably 16 and #define'd in Stats.h
	if ( bucket >= MAX_BUCKETS ) bucket = MAX_BUCKETS-1;
	g_stats.m_msgTotalQueuedByTime [msgType][n][bucket]++;


	// time it
	start = now; // gettimeofdayInMilliseconds();

	// use this for recording how long it takes to generate the reply
	slot->m_queuedTime = now;

	// log it now
	if ( slot->getMsgType() != msg_type_11 && g_conf.m_logDebugLoop )
		log(LOG_DEBUG,"loop: enter handler for 0x%" PRIx32" nice=%" PRId32,
		    (int32_t)slot->getMsgType(),(int32_t)slot->getNiceness());

	// . sanity check - if in a high niceness callback, we should
	//   only be calling niceness 0 callbacks here.
	// . no, because udpserver uses niceness 0 on its fd, and that will
	//   call niceness 1 slots here
	//if ( g_niceness==0 && slot->m_niceness ) { g_process.shutdownAbort(true);}

	// save niceness
	saved = g_niceness;
	// set it
	g_niceness = slot->getNiceness();
	// make sure not 2
	if ( g_niceness >= 2 ) g_niceness = 1;

	bool oom = g_mem.getUsedMemPercentage() >= 99.0;

	// if we are out of mem basically, do not waste time fucking around
	if ( slot->getMsgType() != msg_type_11 && slot->getNiceness() == 0 && oom ) {
		// log it
		static int32_t lcount = 0;
		if ( lcount == 0 )
			log(LOG_DEBUG,"loop: sending back enomem for ""msg 0x%02x", slot->getMsgType());
		if ( ++lcount == 20 ) lcount = 0;
		
		g_consecutiveOOMErrors++;
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. UsedMem=%" PRId64", MaxMem=%" PRId64, __FILE__, __func__, __LINE__, g_mem.getUsedMem(), g_mem.getMaxMem() );
		sendErrorReply ( slot , ENOMEM );
		
		if( g_consecutiveOOMErrors == 200 )
		{
			log(LOG_ERROR,"%s:%s:%d: 200 replies could not be sent due to of Out of Memory. SHUTTING DOWN.", __FILE__, __func__, __LINE__);
			g_process.shutdownAbort(false);
		}
	}
	else {
		if( !oom )
		{
			g_consecutiveOOMErrors = 0;
		}
		// save it
		bool saved2 = g_inHandler;
		// flag it so Loop.cpp does not re-nice quickpoll niceness
		g_inHandler = true;
		// sanity
		if ( slot->hasCalledHandler() ) {
			g_process.shutdownAbort(true);
		}

		// set this here now so it doesn't get its niceness converted
		// then it re-enters the same handler here but in a quickpoll!
		slot->m_calledHandler = true;

		// sanity so msg0.cpp hack works
		if ( slot->getNiceness() == 99 ) { g_process.shutdownAbort(true); }
		// . this is the niceness of the server, not the slot
		// . NO, now it is the slot's niceness. that makes sense.
		m_handlers [ slot->getMsgType() ] ( slot , slot->getNiceness() ) ;
		// let loop.cpp know we're done then
		g_inHandler = saved2;
	}

	// restore
	g_niceness = saved;

	if ( slot->getMsgType() != msg_type_11 && g_conf.m_logDebugLoop )
		log(LOG_DEBUG,"loop: exit handler for 0x%" PRIx32" nice=%" PRId32,
		    (int32_t)slot->getMsgType(),(int32_t)slot->getNiceness());

	// we called the handler, don't call it again
	slot->m_calledHandler = true;

	// i've seen a bunch of msg20 handlers called in a row take over 
	// 10 seconds and the heartbeat gets starved and dumps core
	if ( slot->getMsgType() == msg_type_20 )
		g_process.callHeartbeat();

	// g_errno was set from m_errno before calling the handler, but to
	// make sure the slot doesn't get destroyed now, reset this to 0. see
	// comment about Msg20 above.
	slot->m_errno = 0;

	if ( g_conf.m_maxCallbackDelay >= 0 ) {
		int64_t elapsed = gettimeofdayInMillisecondsLocal() - start;
		if ( elapsed >= g_conf.m_maxCallbackDelay &&
		     slot->getNiceness() == 0 )
			log("udp: Took %" PRId64" ms to call "
			    "HANDLER for msgType=0x%02x niceness=%" PRId32,
			    elapsed,slot->getMsgType(),(int32_t)slot->getNiceness());
	}

	// bitch if it blocked for too long
	//took = gettimeofdayInMilliseconds() - start;
	//mt = LOG_INFO;
	//if ( took <= 50 ) mt = LOG_TIMING;
	//if ( took > 10 )
	//	log(mt,"net: Handler transId=%" PRId32" slot=%" PRIu32" "
	// this is kinda obsolete now that we have the stats above
	if ( g_conf.m_logDebugNet ) {
		int64_t took = gettimeofdayInMillisecondsLocal() - start;
		log(LOG_DEBUG,"net: Handler transId=%" PRId32" slot=%p "
		    "msgType=0x%02x msgSize=%" PRId32" "
		    "g_errno=%s callback=%p "
		    "niceness=%" PRId32" "
		    "took %" PRId64" ms.",
		    (int32_t)slot->getTransId() , slot,
		    msgType, (int32_t)slot->m_readBufSize , mstrerror(g_errno),
		    slot->m_callback,
		    (int32_t)slot->getNiceness(),
		    took );
	}
	// clear any g_errno that may have been set
	g_errno = 0;
	// calling a handler counts
	return true;
}

// this wrapper is called every 15 ms by the Loop class
void UdpServer::timePollWrapper(int fd, void *state) {
	UdpServer *that  = static_cast<UdpServer*>(state);
	that->timePoll();
}

void UdpServer::timePoll ( ) {

	if ( ! m_activeListHead ) return;
	// debug msg
	//if ( g_conf.m_logDebugUdp ) log("enter timePoll");
	// only repeat once
	//bool first = true;
	// get time now
	int64_t now = gettimeofdayInMillisecondsLocal();
	// before timing everyone out or starting resends, just to make
	// sure we read everything. we have have just been blocking on a int32_t
	// handler or callback or sequence of those things and have stuff
	// waiting to be read.
	process(now);
	// get again if changed
	now = gettimeofdayInMillisecondsLocal();
	// loop:
	// do read/send/callbacks
	//	process(now);
	// then do the timeout-ing
	if ( readTimeoutPoll ( now ) ) {
		// if we timed something out or reset it then call the
		// callbacks to do sending and loop back up
		makeCallbacks(MAX_NICENESS); // -1
		// try sending on slots even though we haven't read from them
		//sendPoll ( true , now );
		// repeat in case the send got reset
		//		if ( first ) { first = false; goto loop; }
	}
}


// . this is called once per second
// . return false and sets g_errno on error
// . calls the callback of REPLY-reception slots that have timed out
// . just nuke the REQUEST-reception slots that have timed out
// . returns true if we timed one out OR reset one for resending
bool UdpServer::readTimeoutPoll ( int64_t now ) {
	// did we do something? assume not.
	bool something = false;
	// loop over occupied slots
	ScopedLock sl(m_mtx);
	for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
		// clear g_errno
		g_errno = 0;
		// debug msg
		if ( g_conf.m_logDebugUdp ) {
			log(LOG_DEBUG,
			    "udp: resend TRY tid=%" PRId32" "
			    "dst=%s:%hu "
			    "doneReading=%" PRId32" "
			    "dgramsToSend=%" PRId32" "
			    "resendTime=%" PRId32" "
			    "lastReadTime=%" PRIu64" "
			    "delta=%" PRIu64" "
			    "lastSendTime=%" PRIu64" "
			    "delta=%" PRIu64" "
			    "timeout=%" PRIu64" "
			    "sentBitsOn=%" PRId32" "
			    "readAckBitsOn=%" PRId32" ",
			    slot->getTransId(),
			    iptoa(slot->getIp()),
			    (uint16_t) slot->getPort(),
			    (int32_t) slot->isDoneReading(),
			    slot->getDatagramsToSend(),
			    slot->getResendTime(),
			    (uint64_t) slot->getLastReadTime(),
			    (uint64_t) (now - slot->getLastReadTime()),
			    (uint64_t) slot->getLastSendTime(),
			    (uint64_t) (now - slot->getLastSendTime()),
			    (uint64_t) slot->getTimeout(),
			    slot->m_sentBitsOn,
			    slot->m_readAckBitsOn);
		}

		// if the reading is completed, but we haven't generated a
		// reply yet, then continue because when reply is generated
		// UdpServer::sendReply(slot) will be called and we don't
		// want slot to be destroyed because it timed out...
		if ( slot->isDoneReading() && slot->getDatagramsToSend() <= 0 ) {
			continue;
		}

		// fix if clock changed!
		if ( slot->getLastReadTime() > now ) {
			slot->m_lastReadTime = now;
		}
		if ( slot->getLastSendTime() > now ) {
			slot->m_lastSendTime = now;
		}

		// get time elapsed since last read
		int64_t elapsed = now - slot->getLastReadTime();
		// set all timeouts to 4 secs if we are shutting down
		if ( m_isShuttingDown && slot->getTimeout() > 4000 ) {
			slot->m_timeout = 4000;
		}
		
		// . deal w/ slots that are timed out
		// . could be 1 of the 4 things:
		// . 1. they take too long to send their reply
		// . 2. they take too long to send their request
		// . 3. they take too long to ACK our reply 
		// . 4. they take too long to ACK our request
		// . only flag it if we haven't already...
		if ( elapsed >= slot->getTimeout() && slot->getErrno() != EUDPTIMEDOUT ) {
			// . set slot's m_errno field
			// . makeCallbacks() should call its callback
			slot->m_errno = EUDPTIMEDOUT;
			// prepare to call the callback by adding it to this
			// special linked list
			addToCallbackLinkedList ( slot );
			// let caller know we did something
			something = true;
			// keep going
			continue;
		}

		// how long since last send?
		int64_t delta = now - slot->getLastSendTime();

		// if elapsed is negative, then someone changed the system
		// clock on us, so it won't hurt to resend just to update
		// otherwise, we could be waiting years to resend
		if ( delta < 0 ) {
			delta = slot->getResendTime();
		}

		// continue if we just sent something
		if ( delta < slot->getResendTime() ) {
			continue;
		}

		// . if this host went dead on us all of a sudden then force
		//   a time out
		// . only perform this check once every .5 seconds at most
		//   to prevent performance degradation
		// . REMOVED BECAUSE: this prevents msg 0x011 (pings) from 
		//   getting through!
		/*
		if ( now - s_lastDeadCheck >= 500 ) {
			// set for next time
			s_lastDeadCheck = now;
			// get Host entry
			Host *host = NULL;
			// if hostId provided use that
			if ( slot->m_hostId >= 0 ) 
				host=g_hostdb.getHost ( slot->m_hostId );
			// get host entry from ip/port
			else	host=g_hostdb.getHost(slot->m_ip,slot->m_port);
			// check if dead
			if ( host && g_hostdb.isDead ( host ) ) {
				// if so, destroy this slot
				g_errno = EHOSTDEAD;
				makeCallback ( slot );
				return;
			}
		}
		*/
		// if we don't have anything ready to send continue
		if ( slot->getDatagramsToSend() <= 0 ) continue;
		// if shutting down, rather than resending the reply, just
		// force it as if it were sent. then makeCallbacks can 
		// destroy it.
		if ( m_isShuttingDown ) {
			// do not let this function free the buffers, they
			// may not be allocated really. this may cause a memory
			// leak.
			slot->m_readBuf      = NULL;
			slot->m_sendBufAlloc = NULL;
			// just nuke the slot... this will leave the memory
			// leaked... (memleak, memory leak, memoryleak)
			destroySlot ( slot );
			continue;
		}
		// should we resend all dgrams?
		bool resendAll = false;
		// . HACK: if our request was sent but 30 seconds have passed
		//   and we got no reply, resend our whole request!
		// . this fixes the stuck Msg10 fiasco because it uses
		//   timeouts of 1 year
		// . this is mainly for msgs with infinite timeouts
		// . so if recpipient crashes and comes back up later then
		//   we can resend him EVERYTHING!!
		// . TODO: what if we get reply before we sent everything!?!?
		// . if over 30 secs has passed, resend it ALL!!
		// . this will reset the sent bits and read ack bits
		if ( slot->m_sentBitsOn == slot->m_readAckBitsOn ) {
			// give him 30 seconds to send a reply 
			if ( elapsed < 30000 ) continue;
			// otherwise, resend the whole thing, he
			resendAll = true;
		}

		//
		// SHIT, sometimes a summary generator on a huge asian lang
		// page takes over 1 second and we are unable to send acks
		// for an incoming msg20 request etc, and this code triggers..
		// maybe QUICKPOLL(0) should at least send/read the udp ports?
		//
		// FOR NOW though since hosts do not go down that much
		// let's also require that it has been 5 secs or more...
		//

		int64_t timeout = 5000;
		// spider time requests typically have timeouts of 1 year!
		// so we end up waiting for the host to come back online
		// before the spider can proceed.
		if ( slot->getNiceness() ) {
			timeout = slot->getTimeout();
		}

		// check it
		if ( slot->m_maxResends >= 0 &&
		     // if maxResends it 0, do not do ANY resend! just err out.
		     slot->getResendCount() >= slot->m_maxResends &&
		     // did not get all acks
		     slot->m_sentBitsOn > slot->m_readAckBitsOn &&
		     // fix too many timing out slot msgs when a host is
		     // hogging the cpu on a niceness 0 thing...
		     //elapsed > 5000 &&
		     // respect slot's timeout too!
		     elapsed > timeout &&
		     // only do this when sending a request
		     slot->hasCallback() ) {
			// should this be ENOACK or something?
			slot->m_errno = EUDPTIMEDOUT;
			// prepare to call the callback by adding it to this
			// special linked list
			addToCallbackLinkedList ( slot );
			// let caller know we did something
			something = true;
			// note it
			log(LOG_INFO, "udp: Timing out slot (msgType=0x%" PRIx32") "
			    "after %" PRId32" resends. hostid=%" PRId32" "
			    "(elapsed=%" PRId64")" ,
			    (int32_t)slot->getMsgType(),
			    (int32_t)slot->getResendCount() ,
			    slot->getHostId(),elapsed);
			// keep going
			continue;
		}			
		// . this should clear the sentBits of all unacked dgrams
		//   so they can be resent
		// . this doubles m_resendTime and updates m_resendCount
		slot->prepareForResend ( now , resendAll );
		// . we resend our first unACKed dgram if some time has passed
		// . send as much as we can on this slot
		doSending(slot, true /*allow resends?*/, now);
		// return if we had an error sending, like EBADF we get
		// when we've shut down the servers...
		if ( g_errno == EBADF ) return something;
		//slot->sendDatagramOrAck(m_sock,true/*resends?*/,m_niceness);
		// always call this after every send/read
		//makeCallback(slot);
		something = true;
	}
	// return true if we did something
	return something;
}

// . IMPORTANT: only called for transactions that we initiated!!!
//   so we know to set the key.n0 hi bit
// . may be called twice on same slot by Multicast::destroySlotsInProgress()
void UdpServer::destroySlot ( UdpSlot *slot ) {
	// return if no slot
	if ( ! slot ) {
		return;
	}

	logDebug(g_conf.m_logDebugUdp, "udp: destroy slot=%p", slot);

	// if we're deleting a slot that was an incoming request then
	// decrement m_requestsInWaiting (exclude pings)
	if ( ! slot->hasCallback() && slot->getMsgType() != msg_type_11 ) {
		// one less request in waiting
		m_requestsInWaiting--;
		// special count
		if ( slot->getMsgType() == msg_type_7 ) m_msg07sInWaiting--;
		if ( slot->getMsgType() == msg_type_c1 ) m_msgc1sInWaiting--;
		if ( slot->getMsgType() == msg_type_25 ) m_msg25sInWaiting--;
		if ( slot->getMsgType() == msg_type_39 ) m_msg39sInWaiting--;
		if ( slot->getMsgType() == msg_type_20 ) m_msg20sInWaiting--;
		if ( slot->getMsgType() == msg_type_c ) m_msg0csInWaiting--;
		if ( slot->getMsgType() == msg_type_0 ) m_msg0sInWaiting--;
		// debug msg, good for msg routing distribution, too
		//log("in waiting down to %" PRId32" (0x%02x) ",
		//     m_requestsInWaiting, slot->getMsgType() );
	}

	// save buf ptrs so we can free them
	char *rbuf     = slot->m_readBuf;
	int32_t  rbufSize = slot->m_readBufMaxSize;
	char *sbuf     = slot->m_sendBufAlloc;
	int32_t  sbufSize = slot->m_sendBufAllocSize;
	// don't free our static buffer
	if ( rbuf == slot->m_tmpBuf ) rbuf = NULL;
	// sometimes handlers will use our slots m_tmpBuf to store the reply
	if ( sbuf == slot->m_tmpBuf ) sbuf = NULL;
	// nothing allocated. used by Msg13.cpp g_fakeBuf
	if ( sbufSize == 0 ) sbuf = NULL;
	// NULLify here now just in case
	slot->m_readBuf      = NULL;
	slot->m_sendBuf      = NULL;
	slot->m_sendBufAlloc = NULL;
	// . sig handler may allocate new read buf here!!!!... but not now
	//   since we turned interrupts off
	// . free this slot available right away so sig handler won't
	//   write into m_readBuf or use m_sendBuf, but it may claim it!
	ScopedLock sl(m_mtx);
	freeUdpSlot(slot);
	// free the send/read buffers
	if ( rbuf ) mfree ( rbuf , rbufSize , "UdpServer");
	if ( sbuf ) mfree ( sbuf , sbufSize , "UdpServer");
}



// . called once per second from Process.cpp::shutdown2() when we are trying
//   to shutdown
// . we'll stop answering ping requests
// . we'll wait for replies to those notes, but timeout is 3 seconds
//   we're shutting down so they won't bother sending requests to us
// . this will wait until all fully received requests have had their
//   reply sent to them
// . in the meantime it will send back error replies to all new 
//   incoming requests
// . this will do a blocking close on the listening socket descriptor
// . this will call the callback when shutdown was completed
// . returns false if blocked, true otherwise
// . set g_errno on error
bool UdpServer::shutdown ( bool urgent ) {

	if      ( ! m_isShuttingDown && m_port == 0 )
		log(LOG_INFO,"gb: Shutting down dns resolver.");
	else if ( ! m_isShuttingDown ) 
		log(LOG_INFO,"gb: Shutting down udp server port %hu.",m_port);

	ScopedLock sl(m_mtx);

	// so we know not to accept new connections
	m_isShuttingDown = true;

	// wait for all transactions to complete
	time_t now = getTime();
	int32_t count = 0;
	if(!urgent) {
		for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
			// if we initiated, then don't count it
			if ( slot->hasCallback() ) continue;
			// don't bother with pings or other hosts shutdown 
			if ( slot->getMsgType() == msg_type_11 ) continue;
			// set all timeouts to 3 secs
			if ( slot->getTimeout() > 3000 ) {
				slot->m_timeout = 3000;
			}
			// . don't count lagging slots that haven't got 
			//   a read in 5 sec
			if ( now - slot->getLastReadTime() > 5 ) continue;
			// don't count if timer fucked up
			if ( now - slot->getLastReadTime() < 0 ) continue;
			// count it
			count++;
		}
	}
	if ( count > 0 ) {
		log(LOG_LOGIC,"udp: stilll processing udp traffic after "
		    "shutdown note was sent.");
		return false;
	}

	if ( m_port == 0 )
		log(LOG_INFO,"gb: Closing dns resolver.");
	else
		log(LOG_INFO,"gb: Closing udp server socket port %hu.",m_port);

	// close our socket descriptor, may block to finish sending
	int s = m_sock;
	// . make it -1 so thread exits
	// . g_process.shutdown2() will wait untill all threads exit before
	//   exiting the main process
	// . the timepollwrapper should kick our udp thread out of its 
	//   lock on recvfrom so that it will see that m_sock is -1 and
	//   it will exit
	m_sock = -1;
	// then close it
	close ( s );

	if ( m_port == 0 )
		log(LOG_INFO,"gb: Shut down dns resolver successfully.");
	else
		log(LOG_INFO,"gb: Shut down udp server port %hu successfully.",
		    m_port);

	// all done
	return true;
}

bool UdpServer::timeoutDeadHosts ( Host *h ) {
	// we never have a request out to a proxy, and if we
	// do take the proxy down i don't want us timing out gk0
	// or gk1! which have hostIds 0 and 1, like the proxy0
	// and proxy1 do...
	if ( h->m_isProxy ) return true;

	ScopedLock sl(m_mtx);
	// find sockets out to dead hosts and change the timeout
	for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
		// only change requests to dead hosts
		if ( slot->getHostId() < 0 ) continue;
		//! g_hostdb.isDead(slot->m_hostId) ) continue;
		if ( slot->getHostId() != h->m_hostId ) continue;
		// if we didn't initiate, then don't count it
		if ( ! slot->hasCallback() ) continue;
		// don't bother with pings or other hosts shutdown broadcasts
		if ( slot->getMsgType() == msg_type_11 ) continue;
		// set all timeouts to 5 secs
		//if ( slot->m_timeout > 1 ) slot->m_timeout = 1;
		slot->m_timeout = 0;
	}
	return true;
}

// verified that this is not interruptible
UdpSlot *UdpServer::getEmptyUdpSlot(key_t k, bool incoming) {
	m_mtx.verify_is_locked();
	UdpSlot *slot = removeFromAvailableLinkedList();

	// return NULL if none left
	if (!slot) {
		g_errno = ENOSLOTS;
		if (g_conf.m_logNetCongestion) {
			log(LOG_WARN, "udp: %" PRId32" of %" PRId32" udp slots occupied. None available to handle this new transaction.",
			    (int32_t) m_numUsedSlots, (int32_t) m_maxSlots);
		}
		return NULL;
	}

	addToActiveLinkedList(slot);

	// count it
	m_numUsedSlots++;

	if ( incoming ) {
		m_numUsedSlotsIncoming++;
	}

	slot->m_incoming = incoming;

	// now store ptr in hash table
	slot->m_key = k;
	addKey(k, slot);

	logDebug(g_conf.m_logDebugUdp, "udp: get %s empty slot=%p with key=%s", incoming ? "incoming" : "outgoing", slot, KEYSTR(&k, sizeof(key_t)));
	return slot;
}

void UdpServer::addKey ( key_t k , UdpSlot *ptr ) {
	logDebug(g_conf.m_logDebugUdp, "udp: add key=%s with slot=%p", KEYSTR(&k, sizeof(key_t)), ptr);

	// we assume that k.n1 is the transId. if this changes we should
	// change this to keep our hash lookups fast
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] )
		if ( ++i >= m_numBuckets ) i = 0;
	m_ptrs[i] = ptr;
}

// verify that interrupts are always off before calling this
UdpSlot *UdpServer::getUdpSlot ( key_t k ) {
	// . hash into table
	// . transId is key.n1, use that as hash
	// . m_numBuckets must be a power of 2
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) {
		if (++i >= m_numBuckets) {
			i = 0;
		}
	}

	// if empty, return NULL
	return m_ptrs[i];
}

void UdpServer::addToAvailableLinkedList(UdpSlot *slot) {
	log(LOG_DEBUG, "udp: adding slot=%p to available list", slot);

	slot->m_availableListNext = m_availableListHead;
	m_availableListHead = slot;
}

UdpSlot* UdpServer::removeFromAvailableLinkedList() {
	// return NULL if none left
	if ( ! m_availableListHead ) {
		logDebug(g_conf.m_logDebugUdp, "udp: unable to remove slot from available list");
		return NULL;
	}

	UdpSlot *slot = m_availableListHead;

	// remove from linked list of available slots
	m_availableListHead = slot->m_availableListNext;

	logDebug(g_conf.m_logDebugUdp, "udp: removing slot=%p from available list", slot);

	return slot;
}

void UdpServer::addToCallbackLinkedList(UdpSlot *slot) {
	m_mtx.verify_is_locked();
	// debug log
	if (g_conf.m_logDebugUdp) {
		if (slot->getErrno()) {
			log(LOG_DEBUG, "udp: adding slot=%p with err=%s to callback list", slot, mstrerror(slot->m_errno) );
		} else {
			log(LOG_DEBUG, "udp: adding slot=%p to callback list", slot);
		}
	}

	// must not be in there already, lest we double add it
	if ( isInCallbackLinkedList ( slot ) ) {
		logDebug(g_conf.m_logDebugUdp, "udp: avoided double add slot=%p", slot);
		return;
	}

	slot->m_callbackListNext = NULL;
	slot->m_callbackListPrev = NULL;

	if ( ! m_callbackListTail ) {
		m_callbackListHead = slot;
		m_callbackListTail = slot;
	} else {
		// insert at end of linked list otherwise
		m_callbackListTail->m_callbackListNext = slot;
		slot->m_callbackListPrev = m_callbackListTail;
		m_callbackListTail = slot;
	}
}

bool UdpServer::isInCallbackLinkedList(UdpSlot *slot) {
	m_mtx.verify_is_locked();
	// return if not in the linked list
	if ( slot->m_callbackListPrev || slot->m_callbackListNext || m_callbackListHead == slot ) {
		return true;
	}
	return false;
}

void UdpServer::removeFromCallbackLinkedList(UdpSlot *slot) {
	m_mtx.verify_is_locked();
	logDebug(g_conf.m_logDebugUdp, "udp: removing slot=%p from callback list", slot);

	// return if not in the linked list
	if ( slot->m_callbackListPrev == NULL && slot->m_callbackListNext == NULL && m_callbackListHead != slot ) {
		return;
	}

	// excise from linked list otherwise
	if ( m_callbackListHead == slot ) {
		m_callbackListHead = slot->m_callbackListNext;
	}
	if ( m_callbackListTail == slot )
		m_callbackListTail = slot->m_callbackListPrev;

	if ( slot->m_callbackListPrev ) {
		slot->m_callbackListPrev->m_callbackListNext = slot->m_callbackListNext;
	}

	if ( slot->m_callbackListNext ) {
		slot->m_callbackListNext->m_callbackListPrev = slot->m_callbackListPrev;
	}

	// and so we do not try to re-excise it
	slot->m_callbackListPrev = NULL;
	slot->m_callbackListNext = NULL;
}

void UdpServer::addToActiveLinkedList(UdpSlot *slot) {
	m_mtx.verify_is_locked();
	logDebug(g_conf.m_logDebugUdp, "udp: adding slot=%p to active list", slot);

	// put the used slot at the tail so older slots are at the head and
	// makeCallbacks() can take care of the callbacks that have been
	// waiting the longest first...

	slot->m_activeListNext = NULL;
	slot->m_activeListPrev = NULL;

	if (m_activeListTail) {
		// insert at end of linked list otherwise
		m_activeListTail->m_activeListNext = slot;
		slot->m_activeListPrev = m_activeListTail;
		m_activeListTail = slot;
	} else {
		m_activeListHead = slot;
		m_activeListTail = slot;
	}
}

void UdpServer::removeFromActiveLinkedList(UdpSlot *slot) {
	m_mtx.verify_is_locked();
	logDebug(g_conf.m_logDebugUdp, "udp: removing slot=%p from active list", slot);

	// return if not in the linked list
	if ( slot->m_activeListPrev == NULL && slot->m_activeListNext == NULL && m_activeListHead != slot ) {
		return;
	}

	// excise from linked list otherwise
	if ( m_activeListHead == slot ) {
		m_activeListHead = slot->m_activeListNext;
	}
	if ( m_activeListTail == slot )
		m_activeListTail = slot->m_activeListPrev;

	if ( slot->m_activeListPrev ) {
		slot->m_activeListPrev->m_activeListNext = slot->m_activeListNext;
	}

	if ( slot->m_activeListNext ) {
		slot->m_activeListNext->m_activeListPrev = slot->m_activeListPrev;
	}

	// and so we do not try to re-excise it
	slot->m_activeListPrev = NULL;
	slot->m_activeListNext = NULL;
}

// verified that this is not interruptible
void UdpServer::freeUdpSlot(UdpSlot *slot ) {
	m_mtx.verify_is_locked();
	logDebug(g_conf.m_logDebugUdp, "udp: free slot=%p", slot);

	removeFromActiveLinkedList(slot);

	// also from callback candidates if we should
	removeFromCallbackLinkedList(slot);

	// discount it
	m_numUsedSlots--;

	if ( slot->m_incoming ) m_numUsedSlotsIncoming--;

	// add to linked list of available slots
	addToAvailableLinkedList(slot);

	// . get bucket number in hash table
	// . may have change since table often gets rehashed
	key_t k = slot->m_key;
	int32_t i = hashLong(k.n1) & m_bucketMask;
	while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) 
		if ( ++i >= m_numBuckets ) i = 0;
	// sanity check
	if ( ! m_ptrs[i] ) {
		log(LOG_LOGIC,"udp: freeUdpSlot: Not in hash table.");
		g_process.shutdownAbort(true);
	}

	logDebug(g_conf.m_logDebugUdp, "udp: freeUdpSlot: Freeing slot tid=%" PRId32" dst=%s:%" PRIu32" slot=%p",
	         slot->getTransId(), iptoa(slot->getIp()), (uint32_t)slot->getPort(), slot);

	// remove the bucket
	m_ptrs [ i ] = NULL;
	// rehash all buckets below
	if ( ++i >= m_numBuckets ) i = 0;
	// keep looping until we hit an empty slot
	while ( m_ptrs[i] ) {
		UdpSlot *ptr = m_ptrs[i];
		m_ptrs[i] = NULL;
		// re-hash it
		addKey ( ptr->m_key , ptr );
		if ( ++i >= m_numBuckets ) i = 0;		
	}
}

void UdpServer::cancel ( void *state , msg_type_t msgType ) {
	// . if we have transactions in progress wait
	// . but if we're waiting for a reply, don't bother
	pthread_mutex_lock(&m_mtx.mtx);
	for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
		// skip if not a match
		if (slot->m_state != state || slot->getMsgType() != msgType) {
			continue;
		}

		// note it
		log(LOG_INFO,"udp: cancelled udp slot=%p msgType=0x%02x.", slot, slot->getMsgType());

		// let them know why we are calling the callback prematurely
		g_errno = ECANCELLED;
		// stop waiting for reply, this will call destroySlot(), too
		pthread_mutex_unlock(&m_mtx.mtx);
		makeCallback(slot);
		pthread_mutex_lock(&m_mtx.mtx);
	}
	pthread_mutex_unlock(&m_mtx.mtx);
}

void UdpServer::replaceHost ( Host *oldHost, Host *newHost ) {
	log ( LOG_INFO, "udp: Replacing slots for ip: "
	      "%" PRIu32"/%" PRIu32" port: %" PRIu32,
	      (uint32_t)oldHost->m_ip, 
	      (uint32_t)oldHost->m_ipShotgun,
	      (uint32_t)oldHost->m_port );//, oldHost->m_port2 );
	ScopedLock sl(m_mtx);
	// . loop over outstanding transactions looking for ones to oldHost
	for ( UdpSlot *slot = m_activeListHead; slot; slot = slot->m_activeListNext ) {
		// ignore incoming
		if ( ! slot->hasCallback() ) continue;
		// check for ip match
		if ( slot->getIp() != oldHost->m_ip &&
		     slot->getIp() != oldHost->m_ipShotgun )
			continue;
		// check for port match
		if ( this == &g_udpServer && slot->getPort() != oldHost->m_port )
			continue;
		// . match, replace the slot ip/port with the newHost
		// . first remove the old hashed key for this slot
		// . get bucket number in hash table
		// . may have change since table often gets rehashed
		key_t k = slot->m_key;
		int32_t i = hashLong(k.n1) & m_bucketMask;
		while ( m_ptrs[i] && m_ptrs[i]->m_key != k ) 
			if ( ++i >= m_numBuckets ) i = 0;
		// sanity check
		if ( ! m_ptrs[i] ) {
			log(LOG_LOGIC,"udp: replaceHost: Slot not in hash table.");
			g_process.shutdownAbort(true);
		}

		logDebug(g_conf.m_logDebugUdp, "udp: replaceHost: Rehashing slot tid=%" PRId32" dst=%s:%" PRIu32" slot=%p",
		         slot->getTransId(), iptoa(slot->getIp()), (uint32_t)slot->getPort(), slot);

		// remove the bucket
		m_ptrs [ i ] = NULL;
		// rehash all buckets below
		if ( ++i >= m_numBuckets ) i = 0;
		// keep looping until we hit an empty slot
		while ( m_ptrs[i] ) {
			UdpSlot *ptr = m_ptrs[i];
			m_ptrs[i] = NULL;
			// re-hash it
			addKey ( ptr->m_key , ptr );
			if ( ++i >= m_numBuckets ) i = 0;		
		}

		// careful with this! if we were using shotgun, use that
		// otherwise We core in PingServer because the 
		// m_inProgress[1-2] does net mesh
		if ( slot->getIp() == oldHost->m_ip )
			slot->m_ip = newHost->m_ip;
		else
			slot->m_ip = newHost->m_ipShotgun;

		// replace the data in the slot
		slot->m_port = newHost->m_port;
		//if ( this == &g_udpServer ) slot->m_port = newHost->m_port;
		//else			      slot->m_port = newHost->m_port2;
		//slot->m_transId = getTransId();
		// . now readd the slot to the hash table
		key_t key = m_proto->makeKey ( slot->getIp(),
					       slot->getPort(),
					       slot->getTransId(),
					       true/*weInitiated?*/);
		addKey ( key, slot );
		slot->m_key = key;
		slot->resetConnect();
		// log it
		log(LOG_INFO, "udp: Reset Slot For Replaced Host: transId=%" PRId32" msgType=%i",
		    slot->getTransId(), slot->getMsgType());
	}
}


void UdpServer::printState() {
	log(LOG_TIMING, 
	    "admin: UdpServer - ");

	for ( UdpSlot *slot = m_activeListHead ; slot ; slot = slot->m_activeListNext ) {
		slot->printState();
	}	
}

int32_t UdpServer::getNumUsedSlots() const {
	ScopedLock sl(const_cast<GbMutex&>(m_mtx));
	return m_numUsedSlots;
}

void UdpServer::saveActiveSlots(int fd, msg_type_t msg_type) {
	ScopedLock sl(m_mtx);
	for (const UdpSlot *slot = m_activeListHead; slot; slot = slot->m_activeListNext) {
		// skip if not wanted msg type
		if (slot->getMsgType() != msg_type) {
			continue;
		}

		// skip if got reply
		if (slot->m_readBuf) {
			continue;
		}

		// write hostid sent to
		int32_t hostId = slot->getHostId();
		write(fd, &hostId, 4);

		// write that
		write(fd, &slot->m_sendBufSize, 4);

		// then the buf data itself
		write(fd, slot->m_sendBuf, slot->m_sendBufSize);
	}
}

std::vector<UdpStatistic> UdpServer::getStatistics() const {
	std::vector<UdpStatistic> statistics;

	for (const UdpSlot *slot = m_activeListHead; slot; slot = slot->m_activeListNext) {
		statistics.push_back(UdpStatistic(*slot));
	}

	return statistics;
}
