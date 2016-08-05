// Matt Wells, copyright Nov 2000

// . datagram send control slot
// . UdpServer stores these things in an RdbTree

#ifndef GB_UDPSLOT_H
#define GB_UDPSLOT_H

#include "UdpProtocol.h"
#include "MsgType.h"

#define SMALLDGRAMS

// . we want to avoid the overhead of IP level fragmentation
// . so for an MTU of 1500 we got 28 bytes overhead (IP and UDP headers)
// . later we can try large DGRAM_SIZE values to see if faster
#ifdef SMALLDGRAMS
// newspaperarchive machines need this smaller size
#define DGRAM_SIZE (1500-28-10)
#else
// . let's see if smaller dgrams fix the ping spike problem on gk0c
// . this is in addition to lower the ack windows from 12 to 4
#define DGRAM_SIZE 16400
#endif

// . the 45k dgram doesn't travel well over the internet, and javier needs
//   to do that for the "interface client" code
#define DGRAM_SIZE_INTERNET (1500-28-10)

// . kernel 2.6.8.1 does not like big dgram sizes for loopback
// . can not go above the MTU for the lo device in ifconfig -a
#define DGRAM_SIZE_LB (16400)

// the max of all dgram sizes, of DGRAM_SIZE and of DGRAM_SIZE_LB
#define DGRAM_SIZE_CEILING (30*1492)

// . and for the dns
// . a host was coring because the dgram it got back was bigger than this
//   so i upped from 2000 to 2800. the dns dgram reply it got was 2646 bytes
#define DGRAM_SIZE_DNS (2800)

// . we keep bit counts for every dgram, not just those in a window now
// . therefore, we limit by this for the time being
// . now allow for up to a trunc limit of 1 million --> 7 Megabytes
// . when compiling for Chris or Mark, use the 60M max msg size
// . newspaper archive has s0=20000000 which is up to 180MB termlists!
// . newspaper archive was hitting the wall at 600MB so i upped to 900MB, the
//   downside is that it uses more memory per UdpSlot
// raised from 50MB to 80MB so Msg13 compression proxy can send back big replies > 5MB
#define MAX_DGRAMS (((80*1024*1024) / DGRAM_SIZE) + 1)

#define MAX_ABSDOCLEN ((MAX_DGRAMS * DGRAM_SIZE)-50000)

// . the max size of an incoming request for a hot udp server
// . we cannot call malloc so it must fit in here
// . now we need tens of thousands of udp slots, so keep this small
#define TMPBUFSIZE (250)

class Host;

class UdpSlot {
	// this will help to hide more of UdpSlot implementation from the rest of the codebase
	friend class UdpServer;

public:
	int32_t getNumDgramsRead() const {
		return m_readBitsOn;
	}

	int32_t getNumDgramsSent() const {
		return m_sentBitsOn;
	}

	int32_t getNumAcksRead() const {
		return m_readAckBitsOn;
	}

	int32_t getNumAcksSent() const {
		return m_sentAckBitsOn;
	}

	msg_type_t getMsgType() const {
		return m_msgType;
	}

	// what is our niceness level?
	int32_t getNiceness() const {
		return m_niceness;
	}

	bool hasCallback() const {
		return (m_callback);
	}

	int32_t getTransId() const {
		return m_transId;
	}

	uint32_t getIp() const {
		return m_ip;
	}

	uint16_t getPort() const {
		return m_port;
	}

	int32_t getHostId() const {
		return m_hostId;
	}

	int64_t getTimeout() const {
		return m_timeout;
	}

	int32_t getResendTime() const {
		return m_resendTime;
	}

	char getResendCount() const {
		return m_resendCount;
	}

	int32_t getErrno() const {
		return m_errno;
	}

	int32_t getDatagramsToSend() const {
		return m_dgramsToSend;
	}

	int32_t getDatagramsToRead() const {
		return m_dgramsToRead;
	}

	int64_t getStartTime() const {
		return m_startTime;
	}

	int64_t getFirstSendTime() const {
		return m_firstSendTime;
	}

	int64_t getLastReadTime() const {
		return m_lastReadTime;
	}

	int64_t getLastSendTime() const {
		return m_lastSendTime;
	}

	bool hasCalledHandler() const {
		return m_calledHandler;
	}

	bool hasCalledCallback() const {
		return m_calledCallback;
	}

	// a ptr to the Host class for shotgun info
	Host *m_host;

	// . transmission-related variables
	// . send/ack times are when they were put on the udp stack by sendto()
	//   and may not be the time they were actually sent
	char *m_sendBuf;
	int32_t m_sendBufSize;
	char *m_sendBufAlloc;
	int32_t m_sendBufAllocSize;

	// reception-related variables
	char *m_readBuf;      // store recv'd msg in here.
	int32_t m_readBufSize;  // w/o the dgram headers.
	int32_t m_readBufMaxSize;

protected:
	// set the UdpSlot's protocol, endpoint info, transId, timeout
	void connect(UdpProtocol *proto, sockaddr_in *endPoint, Host *host, int32_t hostId, int32_t transId,
	             int64_t timeout, int64_t now, int32_t niceness);

	// same as above
	void connect(UdpProtocol *proto, uint32_t ip, uint16_t port, Host *host, int32_t hostId, int32_t transId,
	             int64_t timeout, int64_t now, int32_t niceness);

	// reset the slot if ip/port has changed
	void resetConnect();

	// . set up this slot for a send (call after connect() above)
	// . returns false and sets errno on error
	// . use a backoff of -1 for the default
	bool sendSetup(char *msg, int32_t msgSize, char *alloc, int32_t allocSize, msg_type_t msgType, int64_t now,
	               void *state, void (*callback)(void *state, class UdpSlot *), int32_t niceness, int16_t backoff,
	               int16_t maxWait);

	// . send a datagram from this slot on "sock" (call after sendSetup())
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	int32_t sendDatagramOrAck(int sock, bool allowResends, int64_t now);

	// . returns false and sets errno on error, true otherwise
	// . tries to send ACK on "sock" if we read a dgram
	// . tries to send a dgram if we read an ACK
	// . sets *discard to true if caller should discard the dgram
	bool readDatagramOrAck(const void *buf, int32_t numRead, int64_t now, bool *discard);

	// . will reset to send() will start sending at the first unacked dgram
	// . if "reset" is true then will resend ALL dgrams
	void prepareForResend(int64_t now, bool resendAll);


	// this does not include ACKs to read
	bool isDoneReading() {
		if (m_dgramsToRead == 0) {
			return false;
		}
		if (hasDgramsToRead()) {
			return false;
		}
		return true;
	}

	// this does not include ACKs to send
	bool isDoneSending() {
		if (m_dgramsToSend == 0) {
			return false;
		}
		if (hasDgramsToSend()) {
			return false;
		}
		return true;
	}

	bool isTransactionComplete() {
		if (!isDoneReading()) {
			return false;
		}
		if (!isDoneSending()) {
			return false;
		}
		if (hasAcksToRead()) {
			return false;
		}
		if (hasAcksToSend()) {
			return false;
		}
		return true;
	}

	// . for sending purposes, the max scoring UdpSlot sends first
	// . return < 0 if nothing to send
	int32_t getScore ( int64_t now );

	void printState() ;

	// call this callback on timout,error or transaction completion.
	// pass it a ptr to ourselves. It returns true if WE should delete
	// the UdpSlot. Otherwise, it must deleted later by a callback that
	// records all the slots in a list so no one forgets them.
	// Typically, you should just have your callback here return true
	// so you don't have to call deleteSlot(slot) and don't have to 
	// worry about wasting memory.
	void (*m_callback )(void *state, class UdpSlot *slot);

	// this callback is used for letting caller know when his reply has
	// been sent (it's kinda a hack)
	void (*m_callback2 )(void *state, class UdpSlot *slot);

	// if callback2 can be called from a signal handler then make this true
	bool m_isCallback2Hot;

	// . save a POINTER to caller's state;
	// . caller must ensure it's not on the stack
	void *m_state;

	uint32_t m_ip;            // the endpoint host's address
	uint16_t m_port;          // the endpoint host's address

	int64_t m_timeout;       // deltaT in milliseconds
	int32_t m_errno;         // anything go wrong?  0 means ok.
	int32_t m_localErrno;    // are we sending back an error reply?

	// the counts of lit bits for the bits above
	int32_t m_readBitsOn;
	int32_t m_sentBitsOn;
	int32_t m_readAckBitsOn;
	int32_t m_sentAckBitsOn;

	// when the request/reply was read, we set this to the current time so
	// we can measure how long it sits in the queue until the handler
	// or callback is called
	int64_t m_queuedTime;

	// last times of a read/send on this slot in milliseconds since epoch
	int64_t m_lastReadTime;
	int64_t m_lastSendTime;

	// remember our niceness level
	int32_t m_niceness;

public:

	char m_convertedNiceness;

protected:
	// did we call the handler for this?
	bool m_calledHandler;
	bool m_calledCallback;

	// and for doubly linked list of callback candidates
	UdpSlot *m_callbackListNext;
	UdpSlot *m_callbackListPrev;

private:
	// . send an ACK
	// . returns -2 if nothing to send, -1 on error, 0 if blocked,
	//   1 if sent something
	// . should only be called by sendDatagramOrAck() above
	int32_t sendAck(int sock, int64_t now, int32_t dgramNum = -1, int32_t weInitiated = -2, bool cancelTrans = false);

	// . or by readDataGramOrAck() to read a faked ack for protocols that
	//   don't use ACKs
	void readAck(int32_t dgramNum, int64_t now);

	// reset/set m_resendTime based on m_resendCount
	void setResendTime();

	// . returns false and sets errno on error
	// . like sendSetup() but setting up for reading
	// . called when an incoming request arrives
	// . we create a new UdpSlot and call this to handle the request
	bool makeReadBuf(int32_t msgSize, int32_t numDgrams);

	bool hasDgramsToRead() {
		return (m_readBitsOn < m_dgramsToRead);
	}

	bool hasDgramsToSend() {
		return (m_sentBitsOn < m_dgramsToSend);
	}

	bool hasAcksToSend() {
		if (!m_proto->useAcks()) {
			return false;
		}
		return (m_sentAckBitsOn < m_dgramsToRead);
	}

	bool hasAcksToRead() {
		if (!m_proto->useAcks()) {
			return false;
		}
		return (m_readAckBitsOn < m_dgramsToSend);
	}

	// . for internal use
	// . set a window bit
	void setBit(int32_t dgramNum, unsigned char *bits) {
		// lazy initialize,since initializing all bits is too expensive
		if (dgramNum >= m_numBitsInitialized) {
			m_sentBits2[dgramNum >> 3] = 0;
			m_readBits2[dgramNum >> 3] = 0;
			m_sentAckBits2[dgramNum >> 3] = 0;
			m_readAckBits2[dgramNum >> 3] = 0;
			m_numBitsInitialized += 8;
		}
		bits[dgramNum >> 3] |= (1 << (dgramNum & 0x07));
	}

	// clear a window bit
	void clrBit(int32_t dgramNum, unsigned char *bits) {
		// lazy initialize,since initializing all bits is too expensive
		if (dgramNum >= m_numBitsInitialized) {
			m_sentBits2[dgramNum >> 3] = 0;
			m_readBits2[dgramNum >> 3] = 0;
			m_sentAckBits2[dgramNum >> 3] = 0;
			m_readAckBits2[dgramNum >> 3] = 0;
			m_numBitsInitialized += 8;
		}
		bits[dgramNum >> 3] &= ~(1 << (dgramNum & 0x07));
	}

	// get value of a window bit
	bool isOn(int32_t dgramNum, unsigned char *bits) {
		// lazy initialize,since initializing all bits is too expensive
		if (dgramNum >= m_numBitsInitialized) {
			m_sentBits2[dgramNum >> 3] = 0;
			m_readBits2[dgramNum >> 3] = 0;
			m_sentAckBits2[dgramNum >> 3] = 0;
			m_readAckBits2[dgramNum >> 3] = 0;
			m_numBitsInitialized += 8;
		}
		return bits[dgramNum >> 3] & (1 << (dgramNum & 0x07));
	}

	// . get the first lit bit position after bit #i
	// . returns numBits if no bits AFTER i are lit
	int32_t getNextLitBit(int32_t i, unsigned char *bits, int32_t numBits) {
		for (int32_t j = i + 1; j < numBits; j++) {
			if (isOn(j, bits)) {
				return j;
			}
		}
		return numBits;
	}

	// . get the first unlit bit position after bit #i
	// . returns numBits if no bits AFTER i are unlit
	int32_t getNextUnlitBit(int32_t i, unsigned char *bits, int32_t numBits) {
		for (int32_t j = i + 1; j < numBits; j++) {
			if (!isOn(j, bits)) {
				return j;
			}
		}
		return numBits;
	}

	void fixSlot();

	int32_t m_transId;       // unique transaction ID (like socket fd)

	int32_t m_hostId;        // the endpoint host's hostId in hostmap
	msg_type_t m_msgType;       // i like to use this for class routing

	UdpProtocol *m_proto;

	// keep track of the next in line to send
	int32_t m_nextToSend;
	int32_t m_firstUnlitSentAckBit;

	// . this is bigger for loopback sends/reads
	// . we set it just low enough to avoid IP layer fragmentation
	int32_t m_maxDgramSize;

	int32_t m_resendTime;        // resend after this (in ms)
	char m_resendCount; // how many times we've tried to resend

	int32_t m_dgramsToSend;
	int32_t m_dgramsToRead; // closely related to m_bytesToRead.

	// has a sig been queued to call our callback
	bool m_isQueued;

	// . birth time of the udpslot
	// . m_sendTimes are relative to this
	int64_t m_startTime;

	// these are for measuring bps (bandwidth) for g_stats
	int64_t m_firstSendTime;

	// now caller can decide initial backoff, doubles each time no ack rcvd
	int16_t m_backoff;
	// don't wait longer than this, however
	int16_t m_maxWait;

	// save cpu by not having to call memset() on m_sentBits et al
	int32_t m_numBitsInitialized;

	// memset clears from here and above. so put anything that needs to
	// be set to zero above this line.

	// . i've discarded the window since msg size is limited
	// . this way is faster
	// . these bits determine what dgrams we've sent/read/sentAck/readAck
	unsigned char m_sentBits2    [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_readBits2    [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_sentAckBits2 [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_readAckBits2 [ (MAX_DGRAMS / 8) + 1 ];

public:
	// we keep the unused slots in a linked list in UdpServer
	UdpSlot *m_availableListNext;

	// and for doubly linked list of used slots
	UdpSlot *m_activeListNext;
	UdpSlot *m_activeListPrev;

	// store the key so when returning slot we can remove from hash table
	key_t m_key;

	char m_maxResends;

	bool m_incoming;

	// . for the hot udp server, we cannot call malloc in the sig handler
	//   so we set m_readBuf to this to read in int16_t requests
	// . caller should pre-allocated m_readBuf when calling sendRequest()
	//   if he expects a large reply
	// . incoming requests simply cannot be bigger than this for the
	//   hot udp server
	char m_tmpBuf[TMPBUFSIZE];

	char *m_hostname;

private:

	char m_preferEth;
};

extern int32_t g_cancelAcksSent;
extern int32_t g_cancelAcksRead;

#endif // GB_UDPSLOT_H
