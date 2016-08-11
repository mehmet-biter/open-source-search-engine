#include "gb-include.h"

#include "UdpSlot.h"
#include "UdpServer.h"
#include "Stats.h"
#include "Proxy.h"
#include "IPAddressChecks.h"
#include "BitOperations.h"
#include "Process.h"
#include "Conf.h"
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

int32_t g_cancelAcksSent = 0;
int32_t g_cancelAcksRead = 0;

// max resend time (max backoff) for niceness 0
// . i lowered this because the wire supports 1 full packet about every 120
//   MICROSECONDS. so in 40ms we could send ~350 1500-byte packets!!!
// . i also lowered the ack window down to 2 dgrams so this makes more sense
// let's not clog everybody up
#define MAX_RESEND_0 200

// max resend time (max backoff) for niceness 1+
// let's not clog up our network switch internet port
#define MAX_RESEND_1 15000

// start resend time for niceness 0

// . now i increase resend time from 120 to 250  because packets don't seem
//   to be getting lost as much as before since i increase 
//   /proc/sys/net/core/rmem_default and rmem_max to 10Megs
// . before, when it was 65k, kernel was dropping packets like a blind waiter
// . so try 250ms now, hopefully it will cut down on uneccessary resends
// . also it would help to make UdpServer use unmasked interrupt signals
//   to be more responsive
// . but now we also have a problem of doing a bunch of sends, they just
//   get put on the queue and some may be silently dropped (send sendto())
// . we base this resend time assuming we sent the packet when we called
// . sigtimedwait() only has a resolution of 20ms!!! so make due...
// . i lowered this down to 20 since our window is much smaller now
// . there's typically about 120 microseconds between full packets so we
//   should resend quickly!!
// . keep it to 40ms due to kernel time slicing problems
// . but now that we have our query compression proxy over the internet, we got
//   pings that are like 50ms...
// . this was at 70 but gk0 pings to scproxy1 at like 150ms a lot via
//   the roadrunner wireless link, so lets crank this up
#define RESEND_0 170

// . for short msgs we can resend more rapidly
// . it doesn't help to go lower than 20ms cuz that's sigtimedwait()'s limit
// . keep it to 40ms due to kernel time slicing problems
// we are going over the internet to our query compression proxy now
#define RESEND_0_SHORT 170

// start resend time for niceness 1+
// because of roadrunner... (See above)
#define RESEND_1 200

// try to fix a bunch of msg99 replies coming into host 0 at once
#define RESEND_1_LOCAL 100


// . the ack window is back and bigger, now 100 dgrams
// . this gives the receives a chance to respond to being blasted
// . without this acks being sent back are often lost for some reason,
//   ?maybe it's just loopback sends?
// i don't know if that was the cause of it, i think it might be something
// else, so to try to prevent from dropping packets (ifconfig) let's put this down again.
#define ACK_WINDOW_SIZE    4

// size of window for local transactions over loopback interface
// see comment above for why we put this back from 12 to 4
#define ACK_WINDOW_SIZE_LB    4

void UdpSlot::connect ( UdpProtocol *proto    ,
			sockaddr_in *endPoint ,
			Host        *host     ,
			int32_t         hostId   ,
			int32_t         transId  ,
			int64_t         timeout  , // in milliseconds
			int64_t    now      ,
			int32_t         niceness ) {
	// map loopback ip to our ip
	uint32_t ip = endPoint->sin_addr.s_addr;
	if (ip == g_hostdb.getLoopbackIp()) {
		ip = g_hostdb.getMyIp();
	}

	connect(proto, ip, ntohs(endPoint->sin_port), host, hostId, transId, timeout, now, niceness);
}

// . call this after you make a new UdpSlot
// . make new slot using mcalloc() so it's zero'd out
// . NOTE: callback must be non-NULL if you're going to send a request
void UdpSlot::connect ( UdpProtocol    *proto    ,
			uint32_t   ip       ,
			uint16_t  port     ,
			Host           *host     ,
			int32_t            hostId   ,
			int32_t            transId  ,
			int64_t            timeout  , // in milliseconds
			int64_t       now      ,
			int32_t            niceness ) {
	// avoid that heavy memset_ass() call using this logic.
	// we will clear on demand using m_numBitsInitialized logic in UdpSlot.h
	int32_t size = offsetof(UdpSlot,m_sentBits2);
	memset((void *) this, 0, size);
	// store this info
	m_proto    = proto    ;
	m_ip       = ip       ; // keep in network order
	m_port     = port     ; // keep in host order
	m_host     = host     ;
	m_hostId   = hostId   ;
	m_transId  = transId  ;
	m_timeout  = timeout  ;
	m_niceness = niceness ;
	// initialize our time of birth
	m_startTime = now;
	// reset this
	m_queuedTime = -1;
	
	//determine datagram size
	if (!m_proto->useAcks()) {
		m_maxDgramSize = DGRAM_SIZE_DNS;
	} else {
		switch(ip_distance(m_ip)) {
			case ip_distance_ourselves:
				m_maxDgramSize = DGRAM_SIZE_LB;
				break;
			case ip_distance_lan:
				//todo: check link MTU
				//fallthrough
			case ip_distance_nearby:
				m_maxDgramSize = DGRAM_SIZE;
				break;
			default:
				m_maxDgramSize = DGRAM_SIZE_INTERNET;
				break;
		}
	}
}

void UdpSlot::resetConnect ( ) {
	if (m_ip == g_hostdb.getLoopbackIp()) {
		m_ip = g_hostdb.getMyIp();
	}

	// . compute max dgram size
	// . if we're sending to loopback make bigger
	// . dns has its own max size (DNS_DGRAM_SIZE)
	// . if we're going over the internet (interface machine)
	//   use a smaller DGRAM so it makes it
	if (!m_proto->useAcks()) {
		m_maxDgramSize = DGRAM_SIZE_DNS;
	} else if ((m_ip & 0x000000ff) != (g_hostdb.m_myIp & 0x000000ff) || !g_hostdb.isIpInNetwork(m_ip)) {
		// this as 0x0000ffff but we use 10.5.* and 10.6.* addresses
		m_maxDgramSize = DGRAM_SIZE_INTERNET;
		g_process.shutdownAbort(true);
	} else if ( ip_distance(m_ip)==ip_distance_ourselves ) {
		m_maxDgramSize = DGRAM_SIZE_LB;
	} else {
		m_maxDgramSize = DGRAM_SIZE;
	}

	// reset the slot
	m_readBitsOn = 0;
	m_sentBitsOn = 0;
	m_readAckBitsOn = 0;
	m_sentAckBitsOn = 0;
	m_nextToSend = 0;
	m_firstUnlitSentAckBit = 0;
	m_numBitsInitialized = 0;

	// . set m_dgramsToSend
	// . similar to UdpProtocol::getNumDgrams(char *dgram,int32_t dgramSize)
	int32_t dataSpace  = m_maxDgramSize ;

	if (m_proto->stripHeaders()) {
		dataSpace -= m_proto->getHeaderSize(m_sendBufSize);
	}

	m_dgramsToSend  = m_sendBufSize / dataSpace;
	if ( m_sendBufSize % dataSpace != 0 ) {
		m_dgramsToSend++;
	}

	// if msgSize was given as 0 force a dgram to be sent
	if ( m_sendBufSize == 0 ) {
		m_dgramsToSend = 1;
	}
}

// . call this only AFTER calling connect() above
// . callback is non-NULL iff you're sending a request
// . callback is NULL     ifd you're sending a reply
// . returns false and sets g_errno on error
bool UdpSlot::sendSetup(char *msg, int32_t msgSize, char *alloc, int32_t allocSize, msg_type_t msgType, int64_t now,
                        void *state, void (*callback)(void *state, UdpSlot *slot), int32_t niceness, int16_t backoff,
                        int16_t maxWait, const char *extraInfo) {

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(msg,msgSize);
#endif
	// can't be too big
	if ( msgSize / m_maxDgramSize + 1 >= MAX_DGRAMS ) {
		int32_t maxMsgSize = m_maxDgramSize * MAX_DGRAMS;
		log(LOG_LOGIC,"udp: Msg size of %" PRId32" bytes is too big "
		    "to send. Max dgram size = %" PRId32". Max dgrams = "
		    "%" PRId32". Max msg size = %" PRId32" msgtype=0x%02x. Please "
		    "increase the #define MAX_DGRAMS in UdpSlot.h and "
		    "recompile to fix this.",
		    (int32_t)msgSize,(int32_t)m_maxDgramSize,
		    (int32_t)MAX_DGRAMS,maxMsgSize,
		    msgType);
		g_errno = EMSGTOOBIG;
		return false;
	}

	// fill in the supplied parameters
	m_sendBuf          = msg;
	m_sendBufSize      = msgSize;
	m_sendBufAllocSize = allocSize;
	m_sendBufAlloc     = alloc;
	m_callback         = callback;
	m_state            = state;
	m_msgType          = msgType;
	m_lastSendTime     = now;
	m_lastReadTime     = now;
	m_niceness         = niceness;
	m_backoff          = backoff;
	m_maxWait          = maxWait;

	// we haven't sent anything yet so reset this to -1
	m_firstSendTime    = -1;

	// creation time
	//m_sendSetupCalled  = now;

	// set m_resendTime, based on m_resendCount and m_niceness
	setResendTime();

	// . set m_dgramsToSend
	// . similar to UdpProtocol::getNumDgrams(char *dgram,int32_t dgramSize)
	int32_t dataSpace  = m_maxDgramSize ;
	if ( m_proto->stripHeaders() ) {
		dataSpace -= m_proto->getHeaderSize(msgSize);
	}

	m_dgramsToSend  = msgSize / dataSpace;
	if ( msgSize % dataSpace != 0 ) {
		m_dgramsToSend++;
	}

	// if msgSize was given as 0 force a dgram to be sent
	if ( msgSize == 0 ) {
		m_dgramsToSend = 1;
	}

	// save additional info (if present)
	if (extraInfo) {
		strcpy(m_extraInfo, extraInfo);
	} else {
		m_extraInfo[0] = '\0';
	}

	// send to particular ip, but not for pings
	if ( m_msgType == msg_type_11 ) {
		return true;
	}
	if ( ! m_host          ) {
		return true;
	}
	// inherit this from the last transactions
	m_preferEth = m_host->m_preferEth;
	// and set our ip accordingly
	if ( m_host->m_preferEth == 1 ) m_ip = m_host->m_ipShotgun;
	else                            m_ip = m_host->m_ip;

	return true;
}

// resets a UdpSlot for a resend
void UdpSlot::prepareForResend ( int64_t now , bool resendAll ) {
	// clear all if reset is true
	if ( resendAll ) {
		for ( int32_t i = 0 ; i < m_dgramsToSend ; i++ ) 
			clrBit ( i , m_readAckBits2 );
		m_readAckBitsOn = 0;
	}
	// how many sentBits we cleared
	int32_t cleared = 0;
	// clear each sent bit if it hasn't gotten an ACK
	for ( int32_t i = 0 ; i < m_dgramsToSend ; i++ ) {
		// continue if we already have an ack for this one
		if ( isOn ( i , m_readAckBits2 ) ) continue;
		// continue if it's already cleared
		if ( ! isOn ( i , m_sentBits2 ) ) continue;
		// mark dgram #i as unsent since we don't have ACK for it yet
		clrBit ( i , m_sentBits2 );
		// reduce the lit bit count
		m_sentBitsOn--;
		// may have to adjust m_nextToSend
		if ( i < m_nextToSend ) m_nextToSend = i;
		// count each cleared bit
		cleared++;
	}

	// . if we were using eth0 try using eth1, and vice versa
	// . those linksys switches seem to go down all the time and come
	//   back up after a few hours
	// . only do this on the 2nd resend
	if ( g_conf.m_useShotgun &&
	     // need to be sending to a host in the network
	     m_host &&
	     // shotgun ip (eth1) must be different than eth0 ip
	     m_host->m_ip != m_host->m_ipShotgun &&
	     // pingserver.cpp sends to the exact ips it needs to
	     m_msgType != msg_type_11 ) {
		// . were we using the eth0 ip? if so, switch to eth1
		// . do not switch though if the ping is really bad for eth1
		if ( m_preferEth == 0 &&  m_host->m_pingShotgun<3000 ){
			// set m_ip to ip of eth1
			m_ip = m_host->m_ipShotgun;
			// this is now only used when sendSetup() is called
			// for the start of sending a request/reply
			m_host->m_preferEth = 1;
			// use eth1 to talk to this guy for this tid
			m_preferEth = 1;
			logDebug(g_conf.m_logDebugUdp, "udp: switching to eth1 for host #%" PRId32" tid=%" PRId32,
			         m_host->m_hostId, m_transId);
		}
		// . otherwise, we were using the eth1 (shotgun) ip
		// . do not switch though if the ping is really bad for eth0
		else if ( m_preferEth == 1 && m_host->m_ping < 3000 ) {
			// set m_ip to ip of eth0
			m_ip = m_host->m_ip;
			// this is now only used when sendSetup() is called
			// for the start of sending a request/reply
			m_host->m_preferEth = 0;
			// use eth0 to talk to this guy for this tid
			m_preferEth = 0;
			logDebug(g_conf.m_logDebugUdp, "udp: switching to eth0 for host #%" PRId32" tid=%" PRId32,
			         m_host->m_hostId, m_transId);
		}
		// . just some debug notes
		// . this happens when host cores and both eth0 and eth1 r dead
		//logf(LOG_DEBUG,"udp: not switching. preferEth=%" PRId32" "
		//     "pingSHotgun=%" PRId32" ping=%" PRId32,(int32_t)m_host->m_preferEth,
		//     m_host->m_pingShotgun,m_host->m_ping);
	}

	// . tally the count
	// . need to increment since won't resend to eth1 unless this is 2
	m_resendCount++; 
	// debug msg
	if ( g_conf.m_logDebugUdp || (g_conf.m_logDebugDns && !m_proto->useAcks()) ) {
		logf(LOG_DEBUG, "udp: resending slot all=%" PRId32" tid=%" PRId32" dst=%s:%hu count=%" PRId32" host=0x%" PTRFMT
		     " cleared=%" PRId32,
		     (int32_t) resendAll,
		     m_transId,
		     iptoa(m_ip),//+9,
		     (uint16_t) m_port,
		     (int32_t) m_resendCount,
		     (PTRTYPE) m_host,
		     (int32_t) cleared);
	}

	// . after UdpServer::readTimeOutPoll() calls this prepareForResend()
	//   he then calls doSending()
	// . but we cannot send unless the token is free or we're older (500ms)
	//   than the guy that has the token
	// . therefore let's update the m_lastSentTime if we didn't send
	//   anything, just so readTimeoutPoll() quits calling us every time
	m_lastSendTime = now;

	// . don't increase our m_resendTime if we didn't resend anything
	// . that way when the token is available or 500ms younger than us
	//   we won't be waiting 600ms until we can check that!
	if ( cleared == 0 ) {
		return;
	}

	// update stats for this host for the PageHosts.cpp table
	Host *h = m_host;
	if ( ! h && m_hostId >= 0 ) h = g_hostdb.getHost ( m_hostId );
	if ( h                    ) h->m_pingInfo.m_totalResends += cleared;
	// . set the resend time based on m_resendCount and m_niceness
	// . this typically doubles m_resendTime with each resendCount
	setResendTime ();
}

void UdpSlot::setResendTime() {
	// otherwise, calculate how much time since our last send
	int32_t max ;
	if      ( m_maxWait  >= 0 ) max = m_maxWait;
	else if ( m_niceness == 0 ) max = MAX_RESEND_0;
	else                        max = MAX_RESEND_1;
	// if backoff not negative use that
	if ( m_backoff >= 0 ) { 
		// compute resend time
		int32_t bs  = ( 1 << m_resendCount );
		int32_t val = ((int32_t)m_backoff) * bs;
		// check for overflow
		if       ( val < (int32_t)m_backoff )
			m_resendTime = max;
		else if  ( val < bs )
			m_resendTime = max;
		else if  (  bs < 0 )
			m_resendTime = max;
		else
			m_resendTime = val;
		// . don't exceed the max, though of .4 seconds
		// . it's crucial to keep this fairly low because an old slot
		//   can only steal the token when a dgram or ack is read
		//   into that slot
		if ( m_resendTime > max ) m_resendTime = max;
		return;
	}

	// is it a local ip?
	bool isLocal = ip_distance(m_ip)<=ip_distance_nearby;
	// . keep our resend times up-to-date
	// . recompute a new resend time in milliseconds for the winning slot
	// . we double,triple,... the deviation as our backoff scheme
	// . get the avg/stdDev round trip times for this host from the hostmap
	// . these times may change every time we receive an ACK for this host
	// . the new resend time is like double
	if ( m_niceness == 0 ) {
		// if size is short we typically use smaller resend time
		if ( m_dgramsToSend <= 1 ) m_resendTime = RESEND_0_SHORT;
		else                       m_resendTime = RESEND_0;
		// save for checking for overflow
		int32_t tt = m_resendTime;
		// 30 ms resend time for starters for high priority slots
		m_resendTime *= ( 1 << m_resendCount );
		// watch out for overflow
		if ( m_resendTime < tt ) m_resendTime = max;
		// don't exceed the max, though of .4 seconds
		if ( m_resendTime > max ) m_resendTime = max;
		// quick and somewhat incorrect overflow check
		if ( m_resendTime <= 0 ) m_resendTime = max;
	} else {
		int32_t base = RESEND_1;
		if ( isLocal ) base = RESEND_1_LOCAL;
		m_resendTime = base * ( 1 << m_resendCount );
		// watch out for overflow
		if ( m_resendTime < base ) m_resendTime = max;
		//try to prevent everyone from synching up on 
		//a bogged down host when spidering.
		m_resendTime += rand() % m_resendTime;
		// don't exceed the max, though of 30 seconds
		if ( m_resendTime > max ) m_resendTime = max;
		// quick and somewhat incorrect overflow check
		if ( m_resendTime <= 0 ) m_resendTime = max;
	}

	// if we're dns protocol, always use resendTime of 4 seconds
	if ( ! m_proto->useAcks() ) {
		m_resendTime = 4000;
	}
}

// . returns values:
// . -2 if nothing to send
// . -1 on error, 
// .  0 if blocked, 
// .  1 if completed sending a datagram/ACK
// . sets g_errno on error
// . this is only called by UdpServer::doSending()
// . we try to do ALL the reading before calling this so we can send
//   many ACKs back in one packet
int32_t UdpSlot::sendDatagramOrAck ( int sock, bool allowResends, int64_t now ){
	//log("sendDatagramOrAck");
	// if acks we've sent isn't caught up to what we read, send an ack
	if ( m_sentAckBitsOn < m_readBitsOn && m_proto->useAcks() ) 
		return sendAck ( sock , now );
	// we may have received an ack for an implied resend (from ack gap)
	// so we clear some bits, but then got an ACK back later
	while ( m_nextToSend < m_dgramsToSend &&
		isOn ( m_nextToSend , m_sentBits2 ) ) 
		m_nextToSend++;
	// if we've sent it all return -2
	if ( m_sentBitsOn >= m_dgramsToSend ) return -2;
	// or if we hit the end of the road, but m_sentBitsOn is not full,
	// then m_nextToSend must have been too high
	if ( m_nextToSend >= m_dgramsToSend ) {
		log(LOG_LOGIC,
		    "udp: senddatagramorack: m_nextToSend=%" PRId32" >= %" PRId32". "
		    "Fixing it. Do not panic.", 
		    m_nextToSend , m_dgramsToSend );
		fixSlot();
		return 1;
	}
	// get the ip
	int32_t ip = m_ip;
	// . if this is a send to our ip use the loopback interface
	// . MTU is very high here
	if ( ip_distance(m_ip)==ip_distance_ourselves )
		ip = g_hostdb.getLoopbackIp();
	// pick a dgram to send
	int32_t dgramNum = m_nextToSend;

	// . store dgram #dgramNum from this send buf into "dgram"
	// . let the protocol set the dgram from the m_sendBuf for us
	char buf [ DGRAM_SIZE_CEILING ];
	// should hold all headers
	char saved [ 32 ];
	// the header size
	int32_t headerSize = m_proto->getHeaderSize(0);
	// bitch if too big
	if ( headerSize > 32 ) {
		log(LOG_LOGIC,"udp: senddatagramorack: header size of %" PRId32" "
		    "is bigger than 32.",headerSize); return -1; }
	// . now from here on we only use headerSize so we can strip the header
	// . so if the protocol wants the headers, leave them in...
	if ( ! m_proto->stripHeaders() ) headerSize = 0;
	// offset into send buffer, the data to send
	int32_t offset = dgramNum * ( m_maxDgramSize - headerSize );
	// what should we send, and how much?
	char *send      = m_sendBuf     + offset;
	int32_t  sendSize  = m_sendBufSize - offset;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(send,sendSize);
#endif
	// truncate to max size of dgram we're allowed
	if ( sendSize > m_maxDgramSize - headerSize ) 
		sendSize = m_maxDgramSize - headerSize;
	// where to store the dgram, header and data, assume "buf"
	char *dgram = buf;
	// size of dgram, header and data
	int32_t  dgramSize = headerSize + sendSize;
	// if we're NOT the 1st dgram we can store into send buf directly
	if ( dgramNum != 0 ) {
		// where to store the header? right into send buf
		dgram = send - headerSize;
		// but save before overwriting
		memcpy_ass ( saved , dgram , headerSize );
	}
	// store header into "dgram"
	m_proto->setHeader(dgram, m_sendBufSize, m_msgType, dgramNum, m_transId, m_callback, m_localErrno, m_niceness);
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(dgram,headerSize);
#endif
	// . if we're the first dgram, we can't back up for the header...
	// . copy data into dgram if we're the 1st dgram
	if ( dgramNum == 0 ) 
		memcpy_ass ( dgram + headerSize , send , sendSize );

	// if we are the proxy sending a udp packet to our flock, then make
	// sure that we send to tmp cluster if we should
	if      ( g_proxy.isProxy() && g_conf.m_useTmpCluster && m_host ) 
		m_port = m_host->m_port + 1;
	else if ( m_host )
		m_port = m_host->m_port    ;

	// we need a destination stored in a sockaddr for passing to sendto()
	// get sending info from the send control slot (network order)
	// TODO: ensure network order
	struct sockaddr_in to;
	memset(&to,0,sizeof(to));
	to.sin_family      = AF_INET;
	//to.sin_addr.s_addr = .... more complicated than that
	to.sin_port        = htons ( m_port );
	// are we sending to loopback? if so, treat as eth0.
	if ( ip_distance(ip) == ip_distance_ourselves ) {
		to.sin_addr.s_addr = ip;
		// update stats, just put them all in g_udpServer
		g_udpServer.m_eth0PacketsOut += 1;
		g_udpServer.m_eth0BytesOut   += dgramSize;
	} else if ( m_host && m_host->m_hostdb == &g_hostdb ) {
		// don't fuck with it if we are ping though, because that needs to specify the exact ip!
		if ( m_msgType == msg_type_11 ) {
			to.sin_addr.s_addr = ip;
		} else if ( m_preferEth == 1 ) {
			// we now pick ip based on this. if we fail to get a timely ACK
			// then we set switch eth preferences. helps when a switch crashes.
			to.sin_addr.s_addr = m_host->m_ipShotgun;
		} else {
			to.sin_addr.s_addr = m_host->m_ip;
		}

		// update stats, just put them all in g_udpServer
		g_udpServer.m_eth0PacketsOut += 1;
		g_udpServer.m_eth0BytesOut   += dgramSize;
	} else {
		// count packets to/from hosts outside the cluster separately
		// these guys are importing link text usually
		to.sin_addr.s_addr = ip;
		g_udpServer.m_outsiderPacketsOut += 1;
		g_udpServer.m_outsiderBytesOut   += dgramSize;
	}

	// . this socket should be non-blocking (i.e. return immediately)
	// . this should set g_errno on error!
	// MSG_DONTROUTE makes dns fail
	int bytesSent = sendto(sock, dgram, dgramSize, 0, (struct sockaddr *) (void *) &to, sizeof(to));

	// restore what we overwrote
	if ( dgramNum != 0 ) {
		memcpy_ass ( dgram , saved , headerSize );
	}

	// return -1 on error or 0 if blocked
	if ( bytesSent < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		if ( g_errno == EAGAIN  ) { g_errno = 0; return 0;}
		// not in linux
		// . "output queue for a network interface was full"
		// . however, linux just silently drops packets!!!!!!!
		// . i think using more than 1GB in this process brings this
		//   problem up, the kernel's kmalloc fails...
		if ( g_errno == ENOBUFS ) { 
			// log it once every 3 seconds so they know
			static int32_t s_lastTime = 0;
			static int32_t s_count    = 0;
			int32_t t = getTime();
			if ( t - s_lastTime > 3 ||
			     s_lastTime - t > 3   ) { // clock skew?
				s_lastTime = getTime();
				log(LOG_WARN, "udp: got ENOBUFS kernel bug %" PRId32" times.", ++s_count);
			}
			//g_errno = 0; 
			//return 0;
			return -1;
		} 
		// log the error
		log(LOG_WARN, "udp: Call to sendto had error (ignoring): %s.", mstrerror(g_errno)) ;
		// . now immediately switch the eth port to see if that helps!
		// . actually, just pretend we sent it. we won't get an ack
		//   and the resend algo will switch ports
		//return -1;
		bytesSent = dgramSize;
	}
	// this should not happen
	if ( bytesSent != dgramSize ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "udp: sendto only sent %i bytes, not %" PRId32". Undersend.", bytesSent,dgramSize);
		return -1;
	}
	// general count
	if ( m_niceness == 0 ) g_stats.m_packetsOut[m_msgType][0]++;
	else                   g_stats.m_packetsOut[m_msgType][1]++;
	// keep stats
	if ( m_host ) m_host->m_dgramsTo++;
	// keep track of dgrams sent outside of our cluster
	//else          g_stats.m_dgramsToStrangers++;
	// get time now
	//int64_t  now = gettimeofdayInMilliseconds();
	// . if it's our first, mark this for g_stats UDP_*_OUT_BPS
	// . sendSetup() will set m_firstSendTime to -1
	if (m_sentBitsOn == 0 && m_firstSendTime == -1) m_firstSendTime =now;
	// mark this dgram as sent
	setBit ( dgramNum , m_sentBits2 );
	// count the bit we lit
	m_sentBitsOn++;
	// update last send time stamp even if we're a resend
	m_lastSendTime = now;
	// update m_nextToSend
	m_nextToSend = getNextUnlitBit ( dgramNum, m_sentBits2,m_dgramsToSend);
	// log network info
	if ( g_conf.m_logDebugUdp ) {
		//int32_t shotgun = 0;
		//if ( g_conf.m_useShotgun &&   s_useShotgunIp ) shotgun = 1;
		int32_t eth = 1;
		if ( m_host && m_host->m_ip == to.sin_addr.s_addr ) eth = 0;
		// if sending outside, always use eth0
		if ( ! m_host ) eth = 0;
		//if ( m_host->m_ip == (uint32_t)ip ) eth = 0;
		int32_t hid = -1;
		if ( m_host && m_host->m_hostdb == &g_hostdb ) 
			hid = m_host->m_hostId;

		int32_t kk = 0; if ( m_callback ) kk = 1;
		log(LOG_DEBUG,
		    "udp: sent dgram "
		    "dgram=%" PRId32" "
		    "dgrams=%" PRId32" "
		    "msg=0x%02x "
		    "tid=%" PRId32" "
		    "dst=%s:%hu "
		    "eth=%" PRId32" "
		    "init=%" PRId32" "
		    "age=%" PRId32" "
		    "dsent=%" PRId32" "
		    "aread=%" PRId32" "
		    "len=%" PRId32" "
		    "msgSz=%" PRId32" "
		    "cnt=%" PRId32" "
		    "wait=%" PRId32" "
		    "error=%" PRId32" "
		    "k.n1=%" PRIu32" n0=%" PRIu64" "
		    "maxdgramsz=%" PRId32" "
		    "hid=%" PRId32,
		    (int32_t)dgramNum, 
		    (int32_t)m_dgramsToSend,
		    (int16_t)m_msgType,
		    m_transId,
		    //iptoa(m_ip),//+9,
		    iptoa(to.sin_addr.s_addr),
		    (uint16_t)m_port,
		    eth,//shotgun,
		    (int32_t)kk ,
		    (int32_t)(now-m_startTime) ,
		    (int32_t)m_sentBitsOn , 
		    (int32_t)m_readAckBitsOn ,
		    (int32_t)bytesSent ,
		    (int32_t)m_sendBufSize, 
		    (int32_t)m_resendCount, 
		    (int32_t)m_resendTime ,
		    (int32_t)m_localErrno ,
		    m_key.n1,m_key.n0 ,
		    m_maxDgramSize ,
		    hid );
	}

	// return 1 cuz we didn't block
	return 1;
}

// assume m_readBits2, m_sendBits2, m_sentAckBits2 and m_readAckBits2 are 
// correct and update m_firstUnlitSentAckBit, m_sentAckBitsOn, m_readBitsOn,
// m_readAckBitsOn and m_sentBitsOn
void UdpSlot::fixSlot ( ) {
	// log it
	log(LOG_LOGIC,
	    "udp: before fixSlot(): "
	    "m_readBitsOn=%" PRId32" "
	    "m_readAckBitsOn=%" PRId32" "
	    "m_sentBitsOn=%" PRId32" "
	    "m_sentAckBitsOn=%" PRId32" "
	    "m_firstUnlitSentAckBit=%" PRId32" "
	    "m_nextToSend=%" PRId32" " ,
	    m_readBitsOn,
	    m_readAckBitsOn,
	    m_sentBitsOn,
	    m_sentAckBitsOn,
	    m_firstUnlitSentAckBit,
	    m_nextToSend );

	m_readBitsOn    = 0;
	m_readAckBitsOn = 0;
	m_sentBitsOn    = 0;
	m_sentAckBitsOn = 0;
	for ( int32_t i = 0 ; i < m_dgramsToRead ; i++ ) {
		if ( isOn ( i , m_readBits2    ) ) m_readBitsOn++;
		// we send back an ack for every dgram read
		if ( isOn ( i , m_sentAckBits2 ) ) m_sentAckBitsOn++;
	}
	for ( int32_t i = 0 ; i < m_dgramsToSend ; i++ ) {
		if ( isOn ( i , m_sentBits2    ) ) m_sentBitsOn++;
		// we must read an ack for every dgram sent
		if ( isOn ( i , m_readAckBits2 ) ) m_readAckBitsOn++;
	}

	// start at bit #0 so this doesn't loop forever
      m_firstUnlitSentAckBit=getNextUnlitBit(-1,m_sentAckBits2,m_dgramsToRead);
      m_nextToSend          =getNextUnlitBit(-1,m_sentBits2   ,m_dgramsToSend);

	log(LOG_LOGIC,
	    "udp: after fixSlot(): "
	    "m_readBitsOn=%" PRId32" "
	    "m_readAckBitsOn=%" PRId32" "
	    "m_sentBitsOn=%" PRId32" "
	    "m_sentAckBitsOn=%" PRId32" "
	    "m_firstUnlitSentAckBit=%" PRId32" "
	    "m_nextToSend=%" PRId32" " ,
	    m_readBitsOn,
	    m_readAckBitsOn,
	    m_sentBitsOn,
	    m_sentAckBitsOn,
	    m_firstUnlitSentAckBit,
	    m_nextToSend );
}

// . this should be called only after read poll has nothing left to read so
//   we can combine many ACKs into one mega ACK and save packets per second
//   on the network (reduce by half?)
// . returns values:
// . -2 if nothing to send
// . -1 on error, 
// .  0 if blocked, 
// .  1 if completed sending a datagram/ACK
// . if we Initiated is the default -2, then we use m_callback to determine
//   if we initiated the transaction or not
// . if m_callback is NULL we did NOT intiate the transaction
// . we should only be called if m_sentAckBitsOn < m_readBitsOn, i.e.
//   when we're not caught up with ACKing with what we've read
int32_t UdpSlot::sendAck ( int sock , int64_t now , 
			int32_t dgramNum , int32_t weInitiated ,
			bool cancelTrans ) {
	// protection from garbled dgrams
	if ( dgramNum >= MAX_DGRAMS ) {
		log(LOG_LOGIC,
		    "udp: Sending ack for dgram #%" PRId32" > max dgram of %" PRId32".",
		    dgramNum,(int32_t)MAX_DGRAMS); return 1; }
	// remember if forced or not
	//int32_t forced = dgramNum;
	// if this was not supplied, look at m_callback to determine it
	if ( weInitiated == -2 ) {
		if ( m_callback ) weInitiated = 1;
		else              weInitiated = 0;
	}
	// a little dgram buffer
	char dgram[DGRAM_SIZE_CEILING];

	// . if dgramNum is -1, send the next ack in line
	// . it's the first bit in m_sentAckBits2 that is 0 while being
	//   lit in m_readBits
	if ( dgramNum == -1 ) {
		// m_firstUnlitSentAckBit is the first clr bit in m_sentAckBits
		dgramNum = m_firstUnlitSentAckBit;
		// . now find the first bit in m_sentAckBits2 that is off
		//   yet on in m_readBits
		// . the OLD statement below didn't check to see if dgramNum is
		//   then off in m_sentAckBits!!!
		// . let's do it custom then!
		// . we know that m_sentAckBitsOn < m_readBitsOn so the 
		//   we must find a bit with these properties
		for ( ; dgramNum < m_dgramsToRead ; dgramNum++ ) {
		       // if bit off in m_readBits2, it's not an ACK candidate
		       if(!isOn(dgramNum,m_readBits2))continue;
		       // if bit is off in m_sentAckBits2, that's the one!
		       if(!isOn(dgramNum,m_sentAckBits2))break;
		}
		// if we had no match, that's an error!
		if ( dgramNum >= m_dgramsToRead ) {
			log(LOG_LOGIC,
			    "udp: Sending ack for dgram #%" PRId32" which is passed "
			    "the number of dgrams we have to read, %" PRId32". "
			    "Fixing. Do not panic.",
			    dgramNum , m_dgramsToRead );
			fixSlot();
			//g_process.shutdownAbort(true);
			//sleep(50000);
			//return -1;
			return -1;
		}
	}
	// . ask the protocol class to make an ACK for us and store in "dgram"
	// . we initiated the transaction if our callback is non-NULL
	int32_t dgramSize = m_proto->makeAck ( dgram        , 
					    dgramNum     ,
					    m_transId    ,
					    weInitiated  ,
					    cancelTrans  );
	// get the ip
	uint32_t ip = m_ip;
	// . if this is a send to our ip use the loopback interface
	// . MTU is very high here
	//if ( !g_conf.m_interfaceMachine &&
	//     m_ip == g_hostdb.getMyIp() )
	//if ( !g_conf.m_interfaceMachine && g_hostdb.isMyIp(m_ip) )
	if ( ip_distance(m_ip)==ip_distance_ourselves )
		ip = g_hostdb.getLoopbackIp();

	// if we are the proxy sending a udp packet to our flock, then make
	// sure that we send to tmp cluster if we should
	if      ( g_proxy.isProxy() && g_conf.m_useTmpCluster && m_host ) 
		m_port = m_host->m_port + 1;
	else if ( m_host )
		m_port = m_host->m_port    ;

	// get the ip address of dest. host from the slot
	struct sockaddr_in to;
	memset(&to,0,sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr =         ip;
	to.sin_port        = htons ( m_port );

	// stat count
	if ( cancelTrans ) g_cancelAcksSent++;
	// . this socket should be non-blocking (i.e. return immediately)
	// . this should set g_errno on error
	int bytesSent = sendto ( sock      , 
				 dgram     ,
				 dgramSize ,
				 0         ,
				 (struct sockaddr *)(void*)&to , 
				 sizeof ( to ) );
	// return -1 on error, 0 if blocked
	if ( bytesSent < 0 ) {
		// copy errno to g_errno
		g_errno = errno;
		if ( g_errno == EAGAIN  ) { g_errno = 0; return 0; }
		if ( g_errno == ENOBUFS ) { g_errno = 0; return 0; }
		log("udp: error sending ack: %s",mstrerror(g_errno));
		return -1;
	}
	// this should not happen
	if ( bytesSent != dgramSize ) {
		g_errno = EBADENGINEER;
		log("udp: sendto only sent %i bytes, not %" PRId32". Undersend.",
		    bytesSent,dgramSize);
		//sleep(50000);
		return -1;
	}
	// general count
	if ( m_niceness == 0 ) g_stats.m_packetsOut[m_msgType][0]++;
	else                   g_stats.m_packetsOut[m_msgType][1]++;
	// we were an ack
	if ( m_niceness == 0 ) g_stats.m_acksOut[m_msgType][0]++;
	else                   g_stats.m_acksOut[m_msgType][1]++;
	// keep stats
	if ( m_host ) m_host->m_dgramsTo++;
	if ( ! isOn ( dgramNum , m_sentAckBits2 ) ) {
		// mark this ack as sent
		setBit ( dgramNum , m_sentAckBits2 );
		// count the bit we lit
		m_sentAckBitsOn++;
	}
	// update last send time stamp even if we're a resend
	m_lastSendTime = now; // gettimeofdayInMilliseconds();
	// . dgramNum should neveber <, though
	// . but this can happen if we're hot (signal handler)??? how???
	if ( dgramNum < m_firstUnlitSentAckBit ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,
		    "udp: Sending ack for dgram #%" PRId32" which should have "
		    "already been sent. Next ack to send should be for dgram "
		    "# %" PRId32". Fixing. Do not panic.",
		    dgramNum , m_firstUnlitSentAckBit );
		//g_process.shutdownAbort(true);
		fixSlot();
		return 1;
	}
	// . only update m_firstUnlitSentAckBit if we dgramNum was
	//   the first unlit bit in m_sentAckBits
	// . otherwise, we had a read hole so we had to skip dgramNum around
	if (dgramNum <= m_firstUnlitSentAckBit) 
		m_firstUnlitSentAckBit = getNextUnlitBit(dgramNum,
							 m_sentAckBits2,
							 m_dgramsToRead);
	// log msg
	if ( g_conf.m_logDebugUdp ) { // || cancelTrans ) {
		//#ifdef _UDPDEBUG_
		int32_t kk = 0; if ( m_callback ) kk = 1;
		int32_t hid = -1;
		if ( m_host && m_host->m_hostdb == &g_hostdb ) 
			hid = m_host->m_hostId;
		logf(LOG_DEBUG,
		    "udp: sent ACK   "
		    "dgram=%" PRId32" "
		    "msg=0x%02x "
		    "tid=%" PRId32" "
		    "src=%s:%hu "
		    "init=%" PRId32" "
		    "age=%" PRId32" "
		    "cancel=%" PRId32" "
		    "dread=%" PRId32" "
		    "asent=%" PRId32" "
		     "hid=%" PRId32,
		    (int32_t)dgramNum, 
		    (int16_t)m_msgType , 
		    m_transId,
		     iptoa(m_ip),//+9 , 
		    (uint16_t)m_port, 
		    (int32_t)kk , 
		    (int32_t)(gettimeofdayInMilliseconds() - m_startTime) , 
		    (int32_t)cancelTrans,
		    (int32_t)m_readBitsOn , 
		    (int32_t)m_sentAckBitsOn  ,
		     hid);
		//#endif
	}
	return 1;
}

// . returns false and sets g_errno on error, true otherwise
// . if the read dgram had an error code we set g_errno to that and ret false
// . anyone calling this should call sendDatagramOrAck() immediately afterwards
//   in case the send was blocking on receiving an ACK or we should send an ACK
// . updates: m_readBits2, m_readBitsOn, m_sentAckBits2, m_sentAckBitsOn
//            m_firstUnlitSentAckBit
bool UdpSlot::readDatagramOrAck ( const void *readBuffer_,
				  int32_t     readSize,
				  int64_t     now     ,
				  bool       *discard) {
	const char * const readBuffer = (const char*)readBuffer_;
	// assume discard
	*discard = true;
	// get dgram Number
	int32_t dgramNum = m_proto->getDgramNum ( readBuffer, readSize );
	// protection from garbled dgrams
	if ( dgramNum >= MAX_DGRAMS ) {
		log(LOG_LOGIC,
		    "udp: Reading for dgram #%" PRId32" > max dgram of %" PRId32".",
		    dgramNum,(int32_t)MAX_DGRAMS);
		return true;
	}
	// was it a cancel signal?
	if ( m_proto->isCancelTrans ( readBuffer, readSize ) ) {
		//if ( g_conf.m_logDebugUdp ) 
		//logf(LOG_INFO,//LOG_DEBUG,
		log(LOG_DEBUG,
		     "udp: Read cancel ack hdrlen=%" PRId32" tid=%" PRId32" "
		     "src=%s:%hu msgType=0x%02x weInitiated=%" PTRFMT" "
		    "sent=%" PRId32" "
		    "sendbufalloc=%" PTRFMT" sendbufsize=%" PRIu32,
		     readSize , m_proto->getTransId ( readBuffer,readSize ),
		     iptoa(m_ip),m_port,
		     m_proto->getMsgType(readBuffer,readSize),
		    (PTRTYPE)m_callback,
		    m_sentBitsOn,
		    (PTRTYPE)m_sendBufAlloc,
		    (uint32_t)m_sendBufSize);
		// stat count
		g_cancelAcksRead++;
		// what happens is that if we are handling a request and we
		// try to send back the reply on this slot, it will have been
		// destroyed by a call to makeCallbacks(). but really the
		// purpose is to avoid sending large termlists back and
		// wasting network bandwidth, so let's just avoid this if
		// we are not IN THE MIDDLE OF doing a large send. when we 
		// start the send it will probably send us another cancel ack
		// and we can abort it then. before, this was causing Msg20
		// to crash because the requester would send us a cancel ack
		// and destroy the slot that msg20 would try to send its reply
		// on. It's reply was delayed and when it finally came round
		// the slot was destroyed...
		// hey, m_sentBitsOn can be non-zero even if we haven't sent
		// anything because m_sentBits[i] gets forced on below if we
		// read an ack...
		if ( m_sentBitsOn <= 0 ) return true;
		// sometimes it points to a separate send buffer
		//if ( ! m_sendBufAlloc  ) return true;
		// msg1 sends back an empty reply (0 bytes) so we need to
		// check m_resendCount as well, because if we have never
		// generated a reply it should be 0!  we are having problems
		// with acks getting dropped on the floor and the reply
		// keeps getting re-sent over and over, and the received
		// cancel acks are ignore because msg1 has a 0 byte reply...
		if ( ! m_sendBufSize && m_resendCount<=0  ) return true;
		// record if we cancelled it. how many cancel acks we read!
		if ( m_niceness == 0 ) g_stats.m_cancelRead[m_msgType][0]++;
		else                   g_stats.m_cancelRead[m_msgType][1]++;
		// force to be done so UdpServer::makeCallback() will close it
		m_dgramsToRead  = 1;
		m_dgramsToSend  = 1;
		m_readBitsOn    = 1;
		m_sentBitsOn    = 1;
		m_readAckBitsOn = 1;
		m_sentAckBitsOn = 1;
		// assume the receiver ran out of memory
		m_errno         = ENOMEM;
		return true;
	}
	// handle acks
	if ( m_proto->isAck ( readBuffer, readSize ) ) {
		readAck ( dgramNum , now );
		// keep stats
		if ( m_host ) m_host->m_dgramsFrom++;
		return true;
	}
	// . now we have a regular dgram to process
	// . get the timestamp in microseconds
	// log msg
	if ( g_conf.m_logDebugUdp ) {
		int32_t hid = -1;
		if ( m_host && m_host->m_hostdb == &g_hostdb ) 
			hid = m_host->m_hostId;
		int32_t kk = 0; if ( m_callback ) kk = 1;
		log(LOG_DEBUG,
		    "udp: Read dgram "
		    "dgram=%" PRId32" "
		    "msg=0x%02x "
		    "tid=%" PRId32" "
		    "src=%s:%hu "
		    "init=%" PRId32" "
		    "age=%" PRId32" "
		    "dread=%" PRId32" "
		    "asent=%" PRId32" "
		    //"len=%" PRId32" "
		    "msgSz=%" PRId32" "
		    "error=%" PRId32" "
		    "hid=%" PRId32,
		    (int32_t)dgramNum,
		    (int16_t)m_proto->getMsgType(readBuffer,readSize),
		    (int32_t)m_proto->getTransId(readBuffer,readSize),
		    iptoa(m_ip), 
		    (uint16_t)m_port,
		    (int32_t)kk,
		    (int32_t)(gettimeofdayInMilliseconds() - m_startTime) ,
		    (int32_t)m_readBitsOn , 
		    (int32_t)m_sentAckBitsOn , 
		    //(int32_t)readSize ,
		    (int32_t)m_proto->getMsgSize(readBuffer,readSize) ,
		    (int32_t)(m_proto->hadError(readBuffer,readSize)),
		    hid);
	}
	// update time of last read
	m_lastReadTime = gettimeofdayInMilliseconds();
	// if it's passing us an g_errno then set our g_errno from it
	if ( m_proto->hadError ( readBuffer, readSize ) ) {
		// bitch if not dgramNum #0
		if ( dgramNum != 0 ) 
			log(LOG_LOGIC,"udp: Error dgram is not dgram #0.");
		// it's new to us, set the read bits
		setBit ( dgramNum, m_readBits2 );
		// we read one dgram
		m_readBitsOn = 1;
		// only one dgram to read
		m_dgramsToRead = 1;
		// tell caller we haven't read anything
		m_readBufSize  = 0;
		// . but set the remote error bit so we know it's not local 
		// . why? this was messing up g_errno interp. in Multicast!
		g_errno = m_proto->getErrno(readBuffer,readSize);//|REMOTE_ERROR_BIT;
		// return false cuz this was a remote-side error
		return false;
	}
	// . if he's sending a REPLY then set all of our m_readAckBits
	//   because he must have sent ACKs (or tried) for all dgrams in the
	//   request
	// . did we initiate?
	// . AND did we miss some ACKs he sent to us?
	if ( m_callback && m_readAckBitsOn != m_dgramsToSend ) { 
		// catch em all up
		for ( int32_t i = 0 ; i < m_dgramsToSend ; i++ ) 
			setBit ( i , m_readAckBits2 );
		m_readAckBitsOn = m_dgramsToSend;
		if ( g_conf.m_logDebugUdp ) 
			log(LOG_DEBUG,"udp: Cramming ACKs "
			    "tid=%" PRId32" "
			    "dst=%s:%hu" ,
			    m_transId ,
			    iptoa(m_ip),
			    (uint16_t)m_port);
	}

	// . if it's our first, mark this for g_stats UDP_*_IN_BPS
	// did we already receive this dgram? 
	if ( isOn(dgramNum,m_readBits2) ) {
		// did we already send the ack for it?
		if ( isOn(dgramNum,m_sentAckBits2) ) {
			// clear the ack we sent for this so we send it again
			clrBit ( dgramNum , m_sentAckBits2 );
			// reduce lit bit count of sent acks
			m_sentAckBitsOn--;
			// update the next ack to send
			if ( dgramNum < m_firstUnlitSentAckBit )
				m_firstUnlitSentAckBit = dgramNum;
			return true;
		}
		return true;
	}

	// . copy the msg meat into our m_readBuf
	// . how big is the dgram header?
	int32_t headerSize  = m_proto->getHeaderSize ( readBuffer, readSize );
	// make it zero if proto wants them in m_readBuf
	if ( ! m_proto->stripHeaders() ) headerSize = 0;
	// . we store transId, size, type, etc. in the UdpSlot
	// . we store the msg in it's pre-sent form (w/o dgram headers)
	// . "maxDataSize" is max bytes of msg data per dgram (w/o hdr)
	int32_t maxDataSize = m_maxDgramSize - headerSize;
	int32_t offset      = dgramNum       * maxDataSize;
	/*
	// . this checks for undersends (dgrams with not enough data)
	// . we set "size" to the space available in readBuf for dgram's data
	// . we then truncate to maxDataSize in case it's too big
	// . "size" should equal the msg (w/o hdr) in the dgram
	// . return true on error
	// NO: added the "dgramsToRead > 0" to allow underSends on 1 dgram msgs
	int size   = m_readBufSize - offset;
	if ( size > maxDataSize ) size = maxDataSize;
        if ( size != dgramSize - headerSize ) {
		g_errno = EBADENGINEER;
		// remove dgram from queue
		discardDgram();
		log("UdpSlot::readDgram: read undersend") ;
	    return false;
	}
	// this checks for oversends, dgrams that fall outside our readBuf
	if ( offset + dgramSize - headerSize > m_readBufSize ) {
		g_errno = EBADENGINEER;
		// remove dgram from queue
		discardDgram();
		log("UdpSlot::readDgram: read buf overflow") ;
	    return false;
	}
	*/

	// we'll read it ourselves, so tell caller not to read it

	// . how many bytes should be in this dgram?
	// . this will be -1 if unknown, but under a dgram's worth of bytes
	// . -1 is used for the DNS protocol
	int32_t msgSize = m_proto->getMsgSize ( readBuffer, readSize );

	// if this is the first dgram then set this shit
	if ( m_readBitsOn == 0 ) {
		// how many dgrams are we reading for this msg?
		m_dgramsToRead = m_proto->getNumDgrams(msgSize,m_maxDgramSize);
		// set the msgType from the dgram header
		m_msgType = static_cast<msg_type_t>(m_proto->getMsgType(readBuffer, readSize));
		// how big is the msg? remember it
		m_readBufSize = msgSize;
		// . set the cback niceness
		// . ONLY if slot is new! otherwise, we keep the sender's
		//   niceness. so if the slot niceness got converted by
		//   the handler we do not re-nice it on our end.
		if ( ! m_sendBuf )
			m_niceness = m_proto->isNice ( readBuffer, readSize );
	}

	// . if m_readBuf is NULL then init m_readBuf/m_readBufMaxSize big
	//   enough to hold "msgSize" bytes
	// . if we are hot then do not call malloc but try to use m_tmpBuf
	// . if we fail, return false and set g_errno

	// . init m_readBuf, m_readBufMaxSize and m_dgramsToRed
	// . only inits m_readBuf and m_readBufMaxSize if these are 0
	// 
	// ERROR!!!! cannot call malloc() in a signal handler
	// But now, IF WE'RE HOT, sendRequest() should pre-allocate m_readBuf 
	// and if we're reading in an incoming request then it cannot be bigger
	// than the slot's m_quickBuf which we set m_readBuf to in 
	// makeReadBuf if we're hot...
	//

	// just use m_tmpBuf if our sendBuf is NULL and we are reading a
	// small request. But Msg17 thinks this is allocated and tells Msg40
	// to free it.
	// i'm commenting this out to find the rdbtree corruption bug
	//if ( ! m_sendBuf && ! m_readBuf &&
	//     msgSize < TMPBUFSIZE && m_msgType != msg_type_17 ) {
	//	m_readBuf        = m_tmpBuf;
	//	m_readBufMaxSize = TMPBUFSIZE;
	//}

	// . this dgram should let us know how big the entire msg is
	// . so allocate space for m_readBuf
	// . we may already have a read buf if caller passed one in
 retry:
	if ( ! m_readBuf ) {
		if ( ! makeReadBuf ( msgSize , m_dgramsToRead ) ) {
			log(LOG_WARN, "udp: Failed to allocate %" PRId32" bytes to read request or reply for udp socket.", msgSize);
			return false;
		}
		// track down the mem leak.
		// someone is not freeing their read buf!!
		//logf(LOG_DEBUG,"udpslot alloc %" PRId32" at 0x%" PRIx32" msgType=%02x",
		//     msgSize,m_readBuf,m_msgType);
	}
	
	// if we don't have enough room alloc a read buffer
	if ( msgSize > m_readBufMaxSize ) {
		// now we must alloc a buffer
		m_readBuf = NULL;
		goto retry;
	}

	// return false if we have no room for the entire reply
	if ( msgSize > m_readBufMaxSize ) {
		g_errno = EBUFTOOSMALL;
		log( LOG_WARN, "udp: Msg size of %" PRId32" bytes is too big for the "
			   "buffer size, %" PRId32", we allocated. msgType=0x%02x.",
			   msgSize, m_readBufMaxSize , m_msgType );
		return false;
	}

	// we're doing the call to recvfrom() for sure now
	*discard = false;

	// dgram #'s above 0 can be copied directly into m_readBuf
	if ( dgramNum > 0 ) { 
		// how much DATA can we read from this dgram?
		int32_t avail  = m_readBufMaxSize - offset;
		if ( avail  > maxDataSize ) avail = maxDataSize;
		// include header too
		int32_t toRead = avail + headerSize;
		// where to put it?
		char *dest = m_readBuf + offset - headerSize;
		// sanity check, watch out for bad headers...
		if ( toRead < 0 ) {
			// throw this dgram away
			*discard = true;
			//g_errno = ECORRUPTDATA;
			// do not spam the logs
			static int32_t s_badCount = 0;
			s_badCount++;

			log(LOG_WARN, "udp: got %" PRId32" bad dgram headers. "
			    "dgramNum=%" PRId32" offset=%" PRId32" "
			    "readBufMaxSize=%" PRId32". IS hosts.conf OUT OF SYNC???",
			    s_badCount,(int32_t)dgramNum,(int32_t)offset,
			    (int32_t)m_readBufMaxSize);

			// this actually helps us to identify when hosts.conf
			// is out of sync between hosts, so core
			// SEEMS like the roadrunner wireless connection
			// is spiking our packets sometimes with noise...
			//g_process.shutdownAbort(true);
			return false;
		}
		// save what's before us
		char tmp[32];
		memcpy_ass ( tmp , dest , headerSize );
		memcpy(dest, readBuffer, toRead);
		//log("udp: recvfrom1 = %i",(int)numRead);
		// restore what was at the header before we stored it there
		memcpy_ass ( dest , tmp , headerSize );
		// keep stats
		if ( m_host ) m_host->m_dgramsFrom++;
		// keep track of dgrams sent outside of our cluster
		//else          g_stats.m_dgramsFromStrangers++;
		// it's new to us, set the read bits
		setBit ( dgramNum, m_readBits2 );
		// inc the lit bit count
		m_readBitsOn++;
		// if our proto doesn't use acks, treat this as an ACK as well
		if ( ! m_proto->useAcks () ) readAck(0/*dgramNum*/,now);
		// if read everything, set the queued timer
		if ( m_readBitsOn >= m_dgramsToRead )
			m_queuedTime = gettimeofdayInMilliseconds();
		// all done
		return true;
	}

	// otherwise, copy into our tmp buffer
	char dgram [DGRAM_SIZE_CEILING];
	memcpy(dgram, readBuffer, readSize);
	// keep stats
	if ( m_host ) m_host->m_dgramsFrom++;
	// keep track of dgrams sent outside of our cluster
	//else          g_stats.m_dgramsFromStrangers++;

	// where to put it? it might not be dgram #0...
	char *dest = m_readBuf + offset ;
	// what to put?
	char *src  = dgram + headerSize ;
	// how much to put
	int32_t  len  = readSize - headerSize;
	// if msgSize was -1 then m_readBufSize will be -1
	if ( m_readBufSize == -1 ) m_readBufSize = len;
	// bounce it back into m_readBuf
	memcpy_ass ( dest , src , len );
	// it's new to us, set the read bits
	setBit ( dgramNum, m_readBits2 );
	// inc the lit bit count
	m_readBitsOn++;
	// if our proto doesn't use acks, treat this as an ACK as well
	if ( ! m_proto->useAcks () ) readAck(0/*dgramNum*/,now);
	// if read everything, set the queued timer
	if ( m_readBitsOn >= m_dgramsToRead )
		m_queuedTime = gettimeofdayInMilliseconds();
	// success
	return true;
}

// called to process an ack we read for dgram # "dgramNum"
void UdpSlot::readAck ( int32_t dgramNum, int64_t now ) {
	// protection from garbled dgrams
	if ( dgramNum >= MAX_DGRAMS ) {
		log(LOG_LOGIC,
		    "udp: Reading ack for dgram #%" PRId32" > max dgram of %" PRId32".",
		    dgramNum,(int32_t)MAX_DGRAMS); 
                return ; }
	// . get time now
	// . make async safe
	//int64_t now = gettimeofdayInMilliseconds();
	// update lastRead time for this transaction
	m_lastReadTime = gettimeofdayInMilliseconds();
	// cease all resending
	m_resendCount  = 0;
	// reset the resendTime to the starting point before back-off scheme
	setResendTime();
	// if this is a dup ack, ignore it
	if ( isOn ( dgramNum , m_readAckBits2 ) ) return;
	// mark this ack as read
	setBit ( dgramNum , m_readAckBits2 );
	// update lit bit count
	m_readAckBitsOn++;
	// if it was marked as unsent, fix that
	if ( ! isOn ( dgramNum , m_sentBits2 ) ) {
		// bitch if we do not even have a send buffer. why is he acking
		// something we haven't even had to a chance to generate let 
		// alone send? network error?
		if ( ! m_sendBufAlloc || m_dgramsToSend <= 0 ) 
			log("udp: Read ack but send buf is empty.");
		// mark this dgram as sent
		setBit ( dgramNum , m_sentBits2 );
		// update lit bit count
		m_sentBitsOn++;
	}
	// . we often receive an out of order ACK
	// . this usually means that the receiver did not get the dgrams
	//   for the gap of acks because of a collision
	// . we detect this gap and automatically re-send the dgrams w/o delay
	// . if our right neighbor read ack bit is off then mark all off bits 
	//   on our right as having sent bits of 0, until we hit a lit ack bit
	for ( int32_t i = dgramNum - 1 ; i >= 0 ; i-- ) {
		// stop after hitting a lit bit
		if ( isOn ( i , m_readAckBits2 ) ) break;
		// mark as unsent iff it's marked as sent
		if ( ! isOn ( i , m_sentBits2 ) ) continue;
		// mark as unsent
		clrBit ( i , m_sentBits2 );
		// reduce the lit bit count
		m_sentBitsOn--;
		// update m_nextToSend
		if ( i < m_nextToSend ) m_nextToSend = i;
	}

	// if the reply or request was fully acknowledged by the receiver
	// then record some statistics
	if ( ! hasAcksToRead() ) {
		now = gettimeofdayInMilliseconds();
		int32_t delta = now - m_startTime;
		// but if we were sending a reply, use m_queuedTime
		// as the start time of the send. we set m_queuedTime
		// to the current time in sendReply().
		if ( ! m_callback ) delta = now - m_queuedTime; 
		int32_t n = m_niceness;
		if ( n < 0 ) n = 0;
		if ( n > 1 ) n = 1;
		int32_t r = 0;
		// if m_callback then we are sending a request, not a reply,
		// because only the sender of the request has a callback
		if ( m_callback ) r = 1;
		// add to average
		g_stats.m_msgTotalOfSendTimes[m_msgType][n][r] += delta;
		g_stats.m_msgTotalSent       [m_msgType][n][r]++;
		// bucket number is log base 2 of the delta
		if ( delta > 64000 ) delta = 64000;
		int32_t bucket = getHighestLitBit ( (uint16_t)delta );
		g_stats.m_msgTotalSentByTime [m_msgType][n][r][bucket]++;
		// set the queued time for stats on how long it sits in the
		// queue.
		m_queuedTime = now;
	}

	// to save memory in UdpSlot we only keep track of every 8th dgram time
	//if ( (dgramNum & 0x07) == 0 ) {
		// get when we sent this dgram
		//int64_t start = getSendTime ( dgramNum >> 3 ) ;
		// trip time
		//int32_t   tripTime = now - start;
		// debug msg
		//log("tripTime = %" PRId32, tripTime );
		// . update host time
		// . we should also stamp the host each time we re-send, too
		// . this is now handled by g_hostdb::pingHost()
		//g_hostdb.stampHost( m_hostId , tripTime, false/*timedOut?*/);
	//}
	// log msg
	if ( g_conf.m_logDebugUdp ) {
		//#ifdef _UDPDEBUG_
		int32_t kk = 0; if ( m_callback ) kk = 1;
		int32_t hid = -1;
		if ( m_host && m_host->m_hostdb == &g_hostdb ) 
			hid = m_host->m_hostId;
		log(LOG_DEBUG,
		    "udp: Read ACK   "
		    "dgram=%" PRId32" "
		    "msg=0x%02x "
		    "tid=%" PRId32" "
		    "src=%s:%hu "
		    "init=%" PRId32" "
		    "age=%" PRId64" "
		    "dsent=%" PRId32" "
		    "aread=%" PRId32" "
		    "hid=%" PRId32,
		    (int32_t)dgramNum, 
		    (int16_t)m_msgType , 
		    m_transId,
		    iptoa(m_ip) , 
		    (uint16_t)m_port, 
		    (int32_t)kk , 
		    now - m_startTime, 
		    (int32_t)m_sentBitsOn , 
		    (int32_t)m_readAckBitsOn ,
		    hid);
		//#endif
	}
}

// returns false and sets g_errno on error
bool UdpSlot::makeReadBuf ( int32_t msgSize , int32_t numDgrams ) {
	// bitch if it's already there
	if ( m_readBuf ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC, "udp: makereadbuf: Read buf already there.");
		return false;
	}
	// ensure msg not too big
	if ( msgSize > m_proto->getMaxMsgSize() ) {
		g_errno = EMSGTOOBIG;
		log(LOG_LOGIC,"udp: makereadbuf: msg size of %" PRId32" is "
			   "too big. Max is %" PRId32".",msgSize,
			   (int32_t)m_proto->getMaxMsgSize());
		return false;
	}
	// if msgSize is -1 then it is under 1 dgram, but assume the worst
	if ( msgSize == -1 ) msgSize = m_maxDgramSize;

	// . create a msg buf to hold msg, zero out everything...
	// . label it "umsg" so we can grep the *.cpp files for it
	char bb[10];
	bb[0] = 'u';
	bb[1] = 'm';
	bb[2] = 's';
	bb[3] = 'g';

	/// @todo ALC simpler method to convert to hex?
	// msgType is 8 bits
	char val ;
	val = ((m_msgType >> 4) & 0x0f);
	if ( val <= 9 ) bb[4] = '0' + val;
	else            bb[4] = 'a' + val - 10;
	val = ((m_msgType     ) & 0x0f);
	if ( val <= 9 ) bb[5] = '0' + val;
	else            bb[5] = 'a' + val - 10;
	bb[6] = '\0';
	//sprintf(bb,"UdpSlot 0x%02x",m_msgType);
	m_readBuf = (char *) mmalloc ( msgSize , bb ); // "UdpSlot") ;
	if ( ! m_readBuf ) {
		m_readBufSize = 0;
		log(LOG_WARN, "udp: Failed to allocate %" PRId32" bytes to read request or reply on udp socket.", msgSize);
		return false;
	}
	m_readBufMaxSize = msgSize;
	// let the caller know we're good
	return true;
}

// . returns a score of -1 if nothing to send
// . higher scoring slots will do their sending first
// . may have ACKs to send or plain old dgrams to send
// . now is current time in milliseconds since the epoch
int32_t UdpSlot::getScore ( int64_t now ) {
	// do not do sends if callback was called. maybe cancelled?
	// this was causing us to get into an infinite loop in 
	// UdpServer.cpp's sendPoll_ass(). there wasn't anything to send i
	// guess because it got cancelled (?) but we kept returning it here
	// nonetheless.
	if ( m_calledCallback )
		return -1;

	// send ACKs before regular dgrams so we don't hold people up
	if ( m_sentAckBitsOn < m_readBitsOn && m_proto->useAcks() ) 
		return 0x7fffffff;
	// return a score of -1 if we've sent all dgrams (and no acks to send)
	if ( m_sentBitsOn    >= m_dgramsToSend ) return -1;

	// . let's use a window now, give acks a chance to catch up somewhat
	// . if send is local, use a larger ack window of ?64? dgrams
	//if ( ( m_ip != g_hostdb.getMyIp() || g_conf.m_interfaceMachine ) &&
	//if ( ( ! g_hostdb.isMyIp(m_ip)  || g_conf.m_interfaceMachine ) &&
	if ( ip_distance(m_ip)!=ip_distance_ourselves &&
	     m_sentBitsOn >= m_readAckBitsOn + ACK_WINDOW_SIZE    ) return -1;
	// well, give a window size of 100 to loopbacks
	//if ( ( m_ip == g_hostdb.getMyIp() && !g_conf.m_interfaceMachine ) &&
	//if ( ( g_hostdb.isMyIp(m_ip) && !g_conf.m_interfaceMachine ) &&
	if ( ip_distance(m_ip)==ip_distance_ourselves &&
	     m_sentBitsOn >= m_readAckBitsOn + ACK_WINDOW_SIZE_LB ) return -1;

	// return 1 if now is 0
	if ( now == 0LL ) return 1;
	// sort regular sends by the last send time
	int64_t score  = now - m_lastSendTime + 1000;
	// watch out if someone changed the system clock on us
	if ( score < 1000 ) score = 1000;
	// . if we've resent before, wait enough time to send again!
	// . m_resendCount resets when we read an ack (in readAck())
	//if ( m_resendCount > 0 && now - m_lastReadTime < m_resendTime ) {
	//log("now=%" PRId64"-lastRead=%" PRId64" <%" PRId32 , now,m_lastReadTime,m_resendTime);
	//	return -1;
	//}
	// let's give smaller msgs more pts to reduce latency
	//if ( m_sendBufSize <= 1  *1024 ) return score + 30 ;
	//if ( m_sendBufSize <= 10 *1024 ) return score + 20;
	//if ( m_sendBufSize <= 100*1024 ) return score + 10;
	// . is it a resend?
	// . get the time we sent the first unacked dgram
	// m_firstUnackedDgram
	// bool resend = ( score >= m_resendTime );
	// if it's a resend set the hi bit to give it precedence
	// if ( resend ) score |= 0x80000000;
	// else          score &= 0x7fffffff;
	return score;
}


void UdpSlot::printState() {
	//int64_t now = gettimeofdayInMilliseconds();
	log(LOG_TIMING, 
	    "admin: UdpSlot - type:Msg%2" PRIx32" nice:%" PRId32" "
	    "queued:%" PRId32" "
	    "handlerCalled:%" PRId32,
	    (int32_t)m_msgType, 
	    m_niceness, 
	    (int32_t)m_isQueued, 
	    (int32_t)m_calledHandler);
	
}
