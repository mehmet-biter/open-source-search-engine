#include "gb-include.h"

#include "UdpServer.h"
#include "Hostdb.h"
#include "Msg0.h"      // for getRdb(char rdbId)
#include "Msg4.h"
#include "Conf.h"
#include "UdpSlot.h"
#include "Clusterdb.h"
#include "Spider.h"
#include "Rdb.h"
#include "Profiler.h"
#include "Repair.h"
#include "Multicast.h"
#include "JobScheduler.h"
#include "Process.h"
#include "PingServer.h"
#include "ip.h"
#include "max_niceness.h"
#include "Mem.h"
#include <sys/stat.h> //stat()
#include <fcntl.h>


#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

//////////////
//
// Send out our records to add every X ms here:
//
// Batching up the add requests saves udp traffic
// on large networks (100+ hosts).
//
// . currently: send out adds once every 500ms
// . when this was 5000ms (5s) it would wait like
//   5s to spider a url after adding it.
//
//////////////

// article1.html and article11.html are dups but they are being spidered
// within 500ms of another
#define MSG4_WAIT 100


// we have up to this many outstanding Multicasts to send add requests to hosts
#define MAX_MCASTS 128
static Multicast  s_mcasts[MAX_MCASTS];
static Multicast *s_mcastHead = NULL;
static Multicast *s_mcastTail = NULL;
static int32_t       s_mcastsOut = 0;
static int32_t       s_mcastsIn  = 0;

// we have one buffer for each host in the cluster
static char *s_hostBufs     [MAX_HOSTS];
static int32_t  s_hostBufSizes [MAX_HOSTS];
static int32_t  s_numHostBufs;

// . each host has a 32k add buffer which is sent when full or every 10 seconds
// . buffer will be more than 32k if the record to add is larger than 32k
#define MAXHOSTBUFSIZE (32*1024)

// the linked list of Msg4s waiting in line
static Msg4 *s_msg4Head = NULL;
static Msg4 *s_msg4Tail = NULL;

// . TODO: use this instead of spiderrestore.dat
// . call this once for every Msg14 so it can add all at once...
// . make Msg14 add the links before anything else since that uses Msg10
// . also, need to update spiderdb rec for the url in Msg14 using Msg4 too!
// . need to add support for passing in array of lists for Msg14

static bool addMetaList(const char *p, class UdpSlot *slot = NULL);
static void gotReplyWrapper4(void *state, void *state2);
static void handleRequest4(UdpSlot *slot, int32_t niceness);
static void sleepCallback4(int bogusfd, void *state);
static void flushLocal();
static bool sendBuffer(int32_t hostId);
static Multicast *getMulticast();
static void returnMulticast(Multicast *mcast);
static bool storeRec(collnum_t collnum, char rdbId, uint32_t gid, int32_t hostId, const char *rec, int32_t recSize);


Msg4::Msg4() : m_inUse(false) {
	m_callback = NULL;
	m_state = NULL;
	m_rdbId = RDB_NONE;
	m_collnum = 0;
	m_shardOverride = 0;
	m_metaList = NULL;
	m_metaListSize = 0;
	m_currentPtr = NULL;
	m_next = NULL;
}

// why wasn't this saved in addsinprogress.dat file?
Msg4::~Msg4() {
	if (m_inUse) {
		log(LOG_ERROR, "BAD: MSG4 in use!!!!!! this=%p", this);
	}
}



// all these parameters should be preset
bool Msg4::registerHandler() {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );
	
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( msg_type_4, handleRequest4 ) ) {
		log(LOG_ERROR,"%s:%s: Could not register with UDP server!", __FILE__, __func__ );
		return false;
	}

	// clear the host bufs
	s_numHostBufs = g_hostdb.getNumShards();
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ )
		s_hostBufs[i] = NULL;

	// init the linked list of multicasts
	s_mcastHead = &s_mcasts[0];
	s_mcastTail = &s_mcasts[MAX_MCASTS-1];
	for ( int32_t i = 0 ; i < MAX_MCASTS - 1 ; i++ ) 
		s_mcasts[i].m_next = &s_mcasts[i+1];
	// last guy has nobody after him
	s_mcastTail->m_next = NULL;

	// nobody is waiting in line
	s_msg4Head = NULL;
	s_msg4Tail = NULL;

	// . restore state from disk
	// . false means repair is not active
	if ( ! loadAddsInProgress ( NULL ) ) {
		log(LOG_WARN, "init: Could not load addsinprogress.dat. Ignoring.");
		g_errno = 0;
	}

	// . register sleep handler every 5 seconds = 5000 ms
	// . right now MSG4_WAIT is 100ms... i lowered it from 5s
	//   to speed up spidering so it would harvest outlinks
	//   faster and be able to spider them right away.
	// . returns false on failure
	bool rc = g_loop.registerSleepCallback( MSG4_WAIT, NULL, sleepCallback4 );

	logTrace( g_conf.m_logTraceMsg4, "END - returning %s", rc?"true":"false");

	return rc;
}

// scan all host bufs and try to send on them
static void sleepCallback4(int bogusfd, void *state) {
	// wait for clock to be in sync
	if ( ! isClockInSync() ) return;
	// flush them buffers
	flushLocal();
}

static void flushLocal() {
	g_errno = 0;
	// put the line waiters into the buffers in case they are not there
	//storeLineWaiters();
	// now try to send the buffers
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) 
		sendBuffer ( i );
	g_errno = 0;
}

// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue() {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	// if there is an outstanding multicast...
	if ( s_mcastsOut > s_mcastsIn ) {
		logTrace( g_conf.m_logTraceMsg4, "END - multicast waiting, returning true" );
		return true;
	}
	
	// if we have a msg4 waiting in line...
	if ( s_msg4Head ) {
		logTrace( g_conf.m_logTraceMsg4, "END - msg4 waiting, returning true" );
		return true;
	}
		
	// if we have a host buf that has something in it...
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		if ( ! s_hostBufs[i] ) {
			continue;
		}
			
		if ( *(int32_t *)s_hostBufs[i] > 4 ) {
			logTrace( g_conf.m_logTraceMsg4, "END - hostbuf waiting, returning true" );
			return true;
		}
	}

	// otherwise, we have nothing queued up to add
	logTrace( g_conf.m_logTraceMsg4, "END - nothing queued, returning false" );
	return false;
}

bool Msg4::addMetaList ( SafeBuf *sb, collnum_t collnum, void *state, void (* callback)(void *state),
                         rdbid_t rdbId, int32_t shardOverride ) {
	return addMetaList ( sb->getBufStart(), sb->length(), collnum, state, callback, rdbId, shardOverride );
}

bool Msg4::addMetaList ( const char *metaList, int32_t metaListSize, collnum_t collnum, void *state,
                         void (* callback)(void *state), rdbid_t rdbId,
                         // Rebalance.cpp needs to add negative keys to
                         // remove foreign records from where they no
                         // longer belong because of a new hosts.conf file.
                         // This will be -1 if not be overridden.
                         int32_t       shardOverride ) {
	// not in progress
	m_inUse = false;

	// empty lists are easy!
	if ( metaListSize == 0 ) return true;

	// sanity
	if ( collnum < 0 ) { g_process.shutdownAbort(true); }

	// if first time set this
	m_currentPtr   = metaList;
	m_metaList     = metaList;
	m_metaListSize = metaListSize;
	m_collnum      = collnum;
	m_state        = state;
	m_callback     = callback;
	m_rdbId        = rdbId;
	m_next         = NULL;
	m_shardOverride = shardOverride;

 retry:

	// get in line if there's a line
	if ( s_msg4Head ) {
		// add ourselves to the line
		s_msg4Tail->m_next = this;
		// we are the new tail
		s_msg4Tail = this;
		// debug log. seems to happen a lot if not using threads..
		if ( g_jobScheduler.are_new_jobs_allowed() )
			log("msg4: queueing body msg4=0x%" PTRFMT"",(PTRTYPE)this);
		// mark it
		m_inUse = true;
		// all done then, but return false so caller does not free
		// this msg4
		return false;
	}

	// then do it
	if ( addMetaList2 ( ) ) {
		return true;
	}

	// . sanity check
	// . we sometimes get called with niceness 0 from possibly
	//   an injection or something and from a quickpoll
	//   inside addMetList2() in which case our addMetaList2() will
	//   fail, assuming s_msg4Head got set, BUT it SHOULD be OK because
	//   being interrupted at the one QUICKPOLL() in addMetaList2()
	//   doesn't seem like it would hurt.
	// . FURTHEMORE the multicast seems to always be called with
	//   MAX_NICENESS so i'm not sure how niceness 0 will really help
	//   with any of this stuff.
	//if ( s_msg4Head || s_msg4Tail ) { g_process.shutdownAbort(true); }
	if ( s_msg4Head || s_msg4Tail ) {
		log( LOG_WARN, "msg4: got unexpected head");
		goto retry;
	}

	// . spider hang bug
	// . debug log. seems to happen a lot if not using threads..
	if ( g_jobScheduler.are_new_jobs_allowed() )
		log( LOG_DEBUG, "msg4: queueing head msg4=0x%" PTRFMT, (PTRTYPE)this );

	// mark it
	m_inUse = true;

	// . wait in line
	// . when the s_hostBufs[hostId] is able to accomodate our
	//   record this loop will be resumed and the caller's callback
	//   will be called once we are able to successfully queue up
	//   all recs in the list
	// . we are the only one in line, otherwise, we would have exited
	//   the start of this function
	s_msg4Head = this;
	s_msg4Tail = this;
	
	// return false so caller blocks. we will call his callback
	// when we are able to add his list to the hostBufs[] queue
	// and then he can re-use this Msg4 class for other things.
	return false;
}

bool Msg4::isInLinkedList(const Msg4 *msg4) {
	for(Msg4 *m = s_msg4Head; m; m = m->m_next)
		if(m == msg4)
			return true;
	return false;
}

bool Msg4::addMetaList2 ( ) {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	const char *p = m_currentPtr;
	const char *pend = m_metaList + m_metaListSize;

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(p,pend-p);
#endif
	if ( m_collnum < 0 ) { g_process.shutdownAbort(true); }

	// store each record in the list into the send buffers
	for ( ; p < pend ; ) {
		// first is rdbId
		rdbid_t rdbId = m_rdbId;
		if ( rdbId == RDB_NONE ) {
			rdbId = (rdbid_t)(*p++ & 0x7f);
		}

		// get the key of the current record
		const char *key = p;

		// get the key size. a table lookup in Rdb.cpp.
		int32_t ks = getKeySizeFromRdbId ( rdbId );

		// negative key?
		bool del = !( *key & 0x01 );

		// skip key
		p += ks;
		// set this
		uint32_t shardNum = getShardNum( rdbId , key );

		// override it from Rebalance.cpp for redistributing records
		// after updating hosts.conf?
		if ( m_shardOverride >= 0 ) {
			shardNum = m_shardOverride;
		}

		// get the record, is -1 if variable. a table lookup.
		// . negative keys have no data
		// . this unfortunately is not true according to RdbList.cpp
		int32_t dataSize = del ? 0 : getDataSizeFromRdbId ( rdbId );

		// if variable read that in
		if ( dataSize == -1 ) {
			// -1 means to read it in
			dataSize = *(int32_t *)p;
			// sanity check
			if ( dataSize < 0 ) { g_process.shutdownAbort(true); }

			// skip dataSize
			p += 4;
		}

		// skip over the data, if any
		p += dataSize;
		
		// breach us?
		if ( p > pend ) { g_process.shutdownAbort(true); }
 		
		// convert the gid to the hostid of the first host in this
		// group. uses a quick hash table.
		Host *hosts = g_hostdb.getShard ( shardNum );
		int32_t hostId = hosts[0].m_hostId;

		logTrace(g_conf.m_logTraceMsg4, "  rdb=%s key=%s keySize=%" PRId32" isDel=%d dataSize=%" PRId32" shardNum=%" PRId32" hostId=%" PRId32,
		         getDbnameFromId(rdbId), KEYSTR(key, ks), ks, del, shardNum, dataSize, hostId);

		// . add that rec to this groupId, gid, includes the key
		// . these are NOT allowed to be compressed (half bit set)
		//   and this point
		// . this returns false and sets g_errno on failure
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(key,p-key);
#endif
		if ( storeRec ( m_collnum, rdbId, shardNum, hostId, key, p - key )) {
			// . point to next record
			// . will point past records if no more left!
			m_currentPtr = p;

			// get next rec
			continue;
		}

		// g_errno is not set if the store rec could not send the
		// buffer because no multicast was available
		if ( g_errno ) {
			log( LOG_ERROR, "%s:%s: build: Msg4 storeRec had error: %s.", __FILE__, __func__, mstrerror(g_errno) );
		}

		// clear this just in case
		g_errno = 0;

		// if g_errno was not set, this just means we do not have
		// room for the data yet, and try again later
		return false;
	}

	// in case this was being used to hold the data, free it
	m_tmpBuf.purge();

	logTrace( g_conf.m_logTraceMsg4, "END - OK, true" );
	return true;
}


// . modify each Msg4 request as follows
// . collnum(2bytes)|rdbId(1bytes)|listSize&rawlistData|...
// . store these requests in the buffer just like that
static bool storeRec(collnum_t      collnum,
		     char           rdbId,
		     uint32_t       shardNum,
		     int32_t        hostId,
		     const char    *rec,
		     int32_t        recSize ) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&collnum,sizeof(collnum));
	VALGRIND_CHECK_MEM_IS_DEFINED(&rdbId,sizeof(rdbId));
	VALGRIND_CHECK_MEM_IS_DEFINED(&shardNum,sizeof(shardNum));
	VALGRIND_CHECK_MEM_IS_DEFINED(&recSize,sizeof(recSize));
	VALGRIND_CHECK_MEM_IS_DEFINED(rec,recSize);
#endif
	// loop back up here if you have to flush the buffer
 retry:

	// . how many bytes do we need to store the request?
	// . USED(4 bytes)/collnum/rdbId(1)/recSize(4bytes)/recData
	// . "USED" is only used for mallocing new slots really
	int32_t  needForRec = sizeof(collnum_t) + 1 + 4 + recSize;
	int32_t  needForBuf = 4 + needForRec;
	// 8 bytes for the zid
	needForBuf += 8;
	// how many bytes of the buffer are occupied or "in use"?
	char *buf = s_hostBufs[hostId];
	// if NULL, try to allocate one
	if ( ! buf  || s_hostBufSizes[hostId] < needForBuf ) {
		// how big to make it
		int32_t size = MAXHOSTBUFSIZE;
		// must accomodate rec at all costs
		if ( size < needForBuf ) size = needForBuf;
		// make them all the same size
		buf = (char *)mmalloc ( size , "Msg4a" );
		// if still no luck, we cannot send this msg
		if ( ! buf ) return false;
		
		if(s_hostBufs[hostId]) {
			//if the old buf was too small, resize
			gbmemcpy( buf, s_hostBufs[hostId], 
				*(int32_t*)(s_hostBufs[hostId])); 
			mfree ( s_hostBufs[hostId], 
				s_hostBufSizes[hostId] , "Msg4b" );
		}
		// if we are making a brand new buf, init the used
		// size to "4" bytes
		else {
			// itself(4) PLUS the zid (8 bytes)
			*(int32_t *)buf = 4 + 8;
			*(int64_t *)(buf+4) = 0; //clear zid. Not needed, but otherwise leads to uninitialized btyes in a write() syscall
		}
		// add it
		s_hostBufs    [hostId] = buf;
		s_hostBufSizes[hostId] = size;
	}
	// . first int32_t is how much of "buf" is used
	// . includes everything even itself
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf,4);
#endif
	int32_t  used = *(int32_t *)buf;
	// sanity chec. "used" must include the 4 bytes of itself
	if ( used < 12 ) { g_process.shutdownAbort(true); }
	// how much total buf space do we have, used or unused?
	int32_t  maxSize = s_hostBufSizes[hostId];
	// how many bytes are available in "buf"?
	int32_t  avail   = maxSize - used;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf+4+8,used-4-8);
#endif
	// if we can not fit list into buffer...
	if ( avail < needForRec ) {
		// . send what is already in the buffer and clear it
		// . will set s_hostBufs[hostId] to NULL
		// . this will return false if no available Multicasts to
		//   send the buffer, in which case we must tell the caller
		//   to block and wait for us to call his callback, only then
		//   will he be able to proceed. we will call his callback
		//   as soon as we can copy... use this->m_msg1 to add the
		//   list that was passed in...
		if ( ! sendBuffer ( hostId ) ) return false;
		// now the buffer should be empty, try again
		goto retry;
	}
	// point to where to store the list
	char *start = buf + used;
	char *p     = start;
	// store the record and all the info for it
	*(collnum_t *)p = collnum; p += sizeof(collnum_t);
	*(char      *)p = rdbId  ; p += 1;
	*(int32_t      *)p = recSize; p += 4;
	gbmemcpy ( p , rec , recSize ); p += recSize;
	// update buffer used
	*(int32_t *)buf = used + (p - start);
	// all done, did not "block"
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(start,p-start);
#endif
	return true;
}

// . returns false if we were UNable to get a multicast to launch the buffer, 
//   true otherwise
// . returns false and sets g_errno on error
static bool sendBuffer(int32_t hostId) {
	//logf(LOG_DEBUG,"build: sending buf");
	// how many bytes of the buffer are occupied or "in use"?
	char *buf       = s_hostBufs    [hostId];
	int32_t  allocSize = s_hostBufSizes[hostId];
	// skip if empty
	if ( ! buf ) return true;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf,4);
#endif
	// . get size used in buf
	// . includes everything, including itself!
	int32_t used = *(int32_t *)buf;
	// if empty, bail
	if ( used <= 12 ) return true;
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(buf+4+8,used-4-8);
#endif
	// grab a vehicle for sending the buffer
	Multicast *mcast = getMulticast();
	// if we could not get one, wait in line for one to become available
	if ( ! mcast ) {
		//logf(LOG_DEBUG,"build: no mcast available");
		return false;
	}
	// NO! storeRec() will alloc it!
	/*
	// make it point to another
	char *newBuf = (char *)mmalloc ( MAXHOSTBUFSIZE , "Msg4Buf" );
	// assign it to the new Buf
	s_hostBufs [ hostId ] = newBuf;
	// reset used
	if ( newBuf ) {
		*(int32_t *)newBuf = 4;
		s_hostBufSizes[hostId] = MAXHOSTBUFSIZE;
	}
	else 	s_hostBufSizes[hostId] = 0; //if we were oom reset size
	*/
	// get groupId
	//uint32_t groupId = g_hostdb.getGroupIdFromHostId ( hostId );
	Host *h = g_hostdb.getHost(hostId);
	uint32_t shardNum = h->m_shardNum;
	// get group #
	//int32_t groupNum = g_hostdb.getGroupNum ( groupId );

	// sanity check. our clock must be in sync with host #0's or with
	// a host from his group, group #0
	if ( ! isClockInSync() ) { 
		log("msg4: msg4: warning sending out adds but clock not in "
		    "sync with host #0");
		//g_process.shutdownAbort(true); }
	}
	// try to keep all zids unique, regardless of their group
	static uint64_t s_lastZid = 0;
	// select a "zid", a sync id
	uint64_t zid = gettimeofdayInMilliseconds();
	// keep it strictly increasing
	if ( zid <= s_lastZid ) zid = s_lastZid + 1;
	// update it
	s_lastZid = zid;
	// shift up 1 so Syncdb::makeKey() is easier
	zid <<= 1;
	// set some things up
	char *p = buf + 4;
	// . sneak it into the top of the buffer
	// . TODO: fix the code above for this new header
	*(uint64_t *)p = zid;
	p += 8;
	// syncdb debug
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"syncdb: sending msg4 request zid=%" PRIu64,zid);

	// this is the request
	char *request     = buf;
	int32_t  requestSize = used;
	// . launch the request
	// . we now have this multicast timeout if a host goes dead on it
	//   and it fails to send its payload
	// . in that case we should restart from the top and we will add
	//   the dead host ids to the top, and multicast will avoid sending
	//   to hostids that are dead now
	// key is useless for us
	// timeout was 60 seconds, but if we saved the addsinprogress at the wrong time we might miss
	// it when its between having timed out and having been resent by us!
	if (mcast->send(request, requestSize, msg_type_4, false, shardNum, true, 0, (void *)(PTRTYPE)allocSize, (void *)mcast, gotReplyWrapper4, multicast_infinite_send_timeout, MAX_NICENESS, -1, true)) {
		// . let storeRec() do all the allocating...
		// . only let the buffer go once multicast succeeds
		s_hostBufs [ hostId ] = NULL;
		// success
		return true;
	}

	// g_errno should be set
	log("net: Had error when sending request to add data to rdb shard "
	    "#%" PRIu32": %s.", shardNum,mstrerror(g_errno));

	returnMulticast ( mcast );

	return false;
}

static Multicast *getMulticast() {
	// get head
	Multicast *avail = s_mcastHead;
	// return NULL if none available
	if ( ! avail ) return NULL;
	// if all are out then forget it!
	if ( s_mcastsOut - s_mcastsIn >= MAX_MCASTS ) return NULL;
	// remove from head of linked list
	s_mcastHead = avail->m_next;
	// if we were the tail, none now
	if ( s_mcastTail == avail ) s_mcastTail = NULL;
	// count it
	s_mcastsOut++;
	// sanity
	if ( avail->m_inUse ) { g_process.shutdownAbort(true); }
	// return that
	return avail;
}

static void returnMulticast(Multicast *mcast) {
	// return this multicast
	mcast->reset();
	// we are at the tail, nobody is after us
	mcast->m_next = NULL;
	// if no tail we are both head and tail
	if ( ! s_mcastTail ) s_mcastHead         = mcast;
	// put after the tail
	else                 s_mcastTail->m_next = mcast;
	// and we are the new tail
	s_mcastTail = mcast;
	// count it
	s_mcastsIn++;
}

// just free the request
static void gotReplyWrapper4(void *state , void *state2) {
	int32_t allocSize = (int32_t) (PTRTYPE) state;
	Multicast *mcast = (Multicast *) state2;

	// get the request we sent
	char *request = mcast->m_msg;

	if (request) {
		mfree(request, allocSize, "Msg4");
	}

	// make sure no one else can free it!
	mcast->m_msg = NULL;

	// get the udpslot that is replying here
	UdpSlot *replyingSlot = mcast->m_slot;
	if (!replyingSlot) {
		g_process.shutdownAbort(true);
	}

	returnMulticast(mcast);

	Msg4::storeLineWaiters(); // try to launch more msg4 requests in waiting
}

void Msg4::storeLineWaiters ( ) {
	// try to store all the msg4's lists that are waiting in line
	for (;;) {
		Msg4 *msg4 = s_msg4Head;

		// now were we waiting on a multicast to return in order to send
		// another request?  return if not.
		if (!msg4) {
			return;
		}

		// grab the first Msg4 in line. ret fls if blocked adding more of list.
		if (!msg4->addMetaList2()) {
			return;
		}

		// hey, we were able to store that Msg4's list, remove him
		s_msg4Head = msg4->m_next;

		// empty? make tail NULL too then
		if (!s_msg4Head) {
			s_msg4Tail = NULL;
		}

		// . if his callback was NULL, then was loaded in loadAddsInProgress()
		// . we no longer do that so callback should never be null now
		if (!msg4->m_callback) {
			g_process.shutdownAbort(true);
		}

		// log this now i guess. seems to happen a lot if not using threads
		if (g_jobScheduler.are_new_jobs_allowed()) {
			logf(LOG_DEBUG, "msg4: calling callback for msg4=0x%" PTRFMT"", (PTRTYPE) msg4);
		}

		// release it
		msg4->m_inUse = false;

		// call his callback
		msg4->m_callback(msg4->m_state);

		// try the next Msg4 in line
	}
}

// . destroys the slot if false is returned
// . this is registered in Msg4::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
static void handleRequest4(UdpSlot *slot, int32_t netnice) {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	// if we just came up we need to make sure our hosts.conf is in
	// sync with everyone else before accepting this! it might have
	// been the case that the sender thinks our hosts.conf is the same
	// since last time we were up, so it is up to us to check this
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		g_errno = EBADHOSTSCONF;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply ( slot , g_errno );
		
		log(LOG_WARN,"%s:%s: END - hostsConfInDisagreement", __FILE__, __func__ );
		return;
	}

	// need to be in sync first
	if ( ! g_pingServer.m_hostsConfInAgreement ) {
		// . if we do not know the sender's hosts.conf crc, wait 4 it
		// . this is 0 if not received yet
		if (!slot->m_host->m_pingInfo.m_hostsConfCRC) {
			g_errno = EWAITINGTOSYNCHOSTSCONF;
			logError("call sendErrorReply");
			g_udpServer.sendErrorReply ( slot , g_errno );
			
			log(LOG_WARN,"%s:%s: END - EWAITINGTOSYNCHOSTCONF", __FILE__, __func__ );
			return;
		}
		
		// compare our hosts.conf to sender's otherwise
		if (slot->m_host->m_pingInfo.m_hostsConfCRC != g_hostdb.getCRC()) {
			g_errno = EBADHOSTSCONF;
			logError("call sendErrorReply");
			g_udpServer.sendErrorReply ( slot , g_errno );
			
			log(LOG_WARN,"%s:%s: END - EBADHOSTSCONF", __FILE__, __func__ );
			return;
		}
	}

	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	
	// must at least have an rdbId
	if (readBufSize < 7) {
		g_errno = EREQUESTTOOSHORT;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply ( slot , g_errno );
		
		log(LOG_ERROR,"%s:%s: END - EREQUESTTOOSHORT", __FILE__, __func__ );
		return;
	}
	

	// get total buf used
	int32_t used = *(int32_t *)readBuf; //p += 4;

	// sanity check
	if ( used != readBufSize ) {
		// if we send back a g_errno then multicast retries forever
		// so just absorb it!
		logError("msg4: got corrupted request from hostid %" PRId32" used [%" PRId32"] != readBufSize [%" PRId32"]",
		    slot->m_host->m_hostId, used, readBufSize);

		loghex(LOG_ERROR, readBuf, (readBufSize < 160 ? readBufSize : 160), "readBuf (first max. 160 bytes)");

		g_udpServer.sendReply(NULL, 0, NULL, 0, slot);
		//g_udpServer.sendErrorReply(slot,ECORRUPTDATA);return;}

		logError("END");
		return;
	}

	// if we did not sync our parms up yet with host 0, wait...
	if ( g_hostdb.m_hostId != 0 && ! g_parms.m_inSyncWithHost0 ) {
		// limit logging to once per second
		static int32_t s_lastTime = 0;
		int32_t now = getTimeLocal();
		if ( now - s_lastTime >= 1 ) {
			s_lastTime = now;
			log(LOG_INFO, "msg4: waiting to sync with host #0 before accepting data");
		}
		// tell send to try again shortly
		g_errno = ETRYAGAIN;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply(slot,g_errno);
		
		logTrace( g_conf.m_logTraceMsg4, "END - ETRYAGAIN. Waiting to sync with host #0" );
		return; 
	}

	// this returns false with g_errno set on error
	if (!addMetaList(readBuf, slot)) {
		logError("calling sendErrorReply error='%s'", mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,g_errno);

		logTrace(g_conf.m_logTraceMsg4, "END - addMetaList returned false. g_errno=%d", g_errno);
		return;
	}

	// good to go
	g_udpServer.sendReply(NULL, 0, NULL, 0, slot);

	logTrace(g_conf.m_logTraceMsg4, "END - OK");
}


// . Syncdb.cpp will call this after it has received checkoff keys from
//   all the alive hosts for this zid/sid
// . returns false and sets g_errno on error, returns true otherwise
static bool addMetaList(const char *p, UdpSlot *slot) {
	logDebug(g_conf.m_logDebugSpider, "syncdb: calling addMetalist zid=%" PRIu64, *(int64_t *) (p + 4));

	// get total buf used
	int32_t used = *(int32_t *)p;
	// the end
	const char *pend = p + used;
	// skip the used amount
	p += 4;
	// skip zid
	p += 8;

	Rdb  *rdb       = NULL;
	char  lastRdbId = -1;

	/// @note we can have multiple meta list here

	// check if we have enough room for the whole request
	std::map<rdbid_t, std::pair<int32_t, int32_t>> rdbRecSizes;

	const char *pstart = p;
	while (p < pend) {
		// extract rdbId, recSize
		p += sizeof(collnum_t);
		rdbid_t rdbId = static_cast<rdbid_t>(*(char *)p);
		p += 1;
		int32_t recSize = *(int32_t *)p;
		p += 4;

		// . get the rdb to which it belongs, use Msg0::getRdb()
		// . do not call this for every rec if we do not have to
		if (rdbId != lastRdbId || !rdb) {
			rdb = getRdbFromId(rdbId);

			if (!rdb) {
				log(LOG_WARN, "msg4: rdbId of %" PRId32" unrecognized from hostip=%s. dropping WHOLE request",
				    (int32_t)rdbId, slot ? iptoa(slot->getIp()) : "unknown");
				g_errno = ETRYAGAIN;
				return false;
			}

			// an uninitialized secondary rdb? it will have a keysize
			// of 0 if its never been intialized from the repair page.
			// don't core any more, we probably restarted this shard
			// and it needs to wait for host #0 to syncs its
			// g_conf.m_repairingEnabled to '1' so it can start its
			// Repair.cpp repairWrapper() loop and init the secondary
			// rdbs so "rdb" here won't be NULL any more.
			if (rdb->getKeySize() <= 0) {
				time_t currentTime = getTime();
				static time_t s_lastTime = 0;
				if (currentTime > s_lastTime + 10) {
					s_lastTime = currentTime;
					log(LOG_WARN, "msg4: oops. got an rdbId key for a secondary "
							"rdb and not in repair mode. waiting to be in repair mode.");
					g_errno = ETRYAGAIN;
					return false;
				}
			}
		}

		// sanity check
		if (p + recSize > pend) {
			g_errno = ECORRUPTDATA;
			return false;
		}

		auto &item = rdbRecSizes[rdbId];
		item.first += 1;

		int32_t dataSize = recSize - rdb->getKeySize();
		if (rdb->getFixedDataSize() == -1) {
			dataSize -= 4;
		}

		item.second += dataSize;

		// reset g_errno
		g_errno = 0;

		// advance over the rec data to point to next entry
		p += recSize;
	}

	bool hasRoom = true;
	for (auto item : rdbRecSizes) {
		Rdb *rdb = getRdbFromId(item.first);
		if (!rdb->hasRoom(item.second.first, item.second.second)) {
			rdb->dumpTree(1);
			hasRoom = false;
		}
	}

	if (!hasRoom) {
		g_errno = ETRYAGAIN;
		return false;
	}

	/// @todo ALC we can probably improve performance by preprocessing records in previous loop

	// reset p to before 'check' loop
	p = pstart;

	// . this request consists of multiple recs, so add each one
	// . collnum(2bytes)/rdbId(1byte)/recSize(4bytes)/recData/...
	while (p < pend) {
		// extract collnum, rdbId, recSize
		collnum_t collnum = *(collnum_t *)p;
		p += sizeof(collnum_t);
		rdbid_t rdbId = static_cast<rdbid_t>(*(char *)p);
		p += 1;
		int32_t recSize = *(int32_t *)p;
		p += 4;

		// . get the rdb to which it belongs, use Msg0::getRdb()
		// . do not call this for every rec if we do not have to
		if (rdbId != lastRdbId || !rdb) {
			rdb = getRdbFromId(rdbId);

			if (!rdb) {
				log(LOG_WARN, "msg4: rdbId of %" PRId32" unrecognized from hostip=%s. dropping WHOLE request",
				    (int32_t)rdbId, slot ? iptoa(slot->getIp()) : "unknown");
				g_errno = ETRYAGAIN;
				return false;
			}
		}

		// reset g_errno
		g_errno = 0;

		// . make a list from this data
		// . skip over the first 4 bytes which is the rdbId
		// . TODO: embed the rdbId in the msgtype or something...
		RdbList list;

		// set the list
		// todo: dodgy cast to char*. RdbList should be fixed
		list.set((char *)p, recSize, (char *)p, recSize, rdb->getFixedDataSize(), false, rdb->useHalfKeys(), rdb->getKeySize());

		// advance over the rec data to point to next entry
		p += recSize;

		// keep track of stats
		rdb->readRequestAdd(recSize);

		// this returns false and sets g_errno on error
		bool status = rdb->addList(collnum, &list, MAX_NICENESS);

		// bad coll #? ignore it. common when deleting and resetting
		// collections using crawlbot. but there are other recs in this
		// list from different collections, so do not abandon the whole
		// meta list!! otherwise we lose data!!
		if (g_errno == ENOCOLLREC && !status) {
			g_errno = 0;
			status = true;
		}

		if (!status) {
			break;
		}

		// do the next record here if there is one
	}

	// no memory means to try again
	if ( g_errno == ENOMEM ) g_errno = ETRYAGAIN;
	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC  && g_repairMode > 0       )
		g_errno = ETRYAGAIN;

	// are we done
	if ( g_errno ) return false;

	// success
	return true;
}


//
// serialization code
//

// . when we core, save this stuff so we can re-add when we come back up
// . have a sleep wrapper that tries to flush the buffers every 10 seconds
//   or so.
// . returns false on error, true on success
// . does not do any mallocs in case we are OOM and need to save
// . BUG: might be trying to send an old bucket, so scan udp slots too? or
//   keep unsent buckets in the list?
bool saveAddsInProgress(const char *prefix) {

	if ( g_conf.m_readOnlyMode ) return true;

	// this does not work so skip it for now
	//return true;

	// open the file
	char filename[1024];

	// if saving while in repair mode, that means all of our adds must
	// must associated with the repair. if we send out these add requests
	// when we restart and not in repair mode then we try to add to an
	// rdb2 which has not been initialized and it does not work.
	if ( ! prefix ) prefix = "";
	sprintf ( filename , "%s%saddsinprogress.saving", 
		  g_hostdb.m_dir , prefix );

	int32_t fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, getFileCreationFlags());
	if (fd < 0) {
		log(LOG_WARN, "build: Failed to open %s for writing: %s", filename, strerror(errno));
		return false;
	}

	log(LOG_INFO,"build: Saving %s",filename);

	// the # of host bufs
	write ( fd , (char *)&s_numHostBufs , 4 );
	// serialize each hostbuf
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		// get the size
		int32_t used = 0;
		// if not null, how many bytes are used in it?
		if ( s_hostBufs[i] ) used = *(int32_t *)s_hostBufs[i];
		// size of the buf
		write ( fd , (char *)&used , 4 );
		// skip if none
		if ( ! used ) continue;
		// if only 4 bytes used, that is basically empty, the first
		// 4 bytes is how much of the total buffer is used, including
		// those 4 bytes.
		if ( used == 4 ) continue;
		// the buf itself
		write ( fd , s_hostBufs[i] , used );
	}

	// save in progress msg4 requests too!
	g_udpServer.saveActiveSlots(fd, msg_type_4);

	// MDW: if msg4 was stored in the linked list then caller 
	// never got his callback called, so the spider will redo
	// this url later...

	// . serialize each Msg4 that is waiting in line
	// . need to preserve their list ptrs so to avoid re-adds?
	/*
	Msg4 *msg4 = s_msg4Head;
	while ( msg4 ) {
		msg4->save ( fd );
		// next msg4
		msg4 = msg4->m_next;
	}
	*/

	// all done
	close ( fd );
	// if all was successful, rename the file
	char newFilename[1024];

	// if saving while in repair mode, that means all of our adds must
	// must associated with the repair. if we send out these add requests
	// when we restart and not in repair mode then we try to add to an
	// rdb2 which has not been initialized and it does not work.
	sprintf(newFilename, "%s%saddsinprogress.dat", g_hostdb.m_dir, prefix);

	if( ::rename(filename , newFilename) == -1 ) {
		logError("Error renaming file [%s] to [%s] (%d: %s)", filename, newFilename, errno, mstrerror(errno));
	}

	return true;
}



// . returns false on an unrecoverable error, true otherwise
// . sets g_errno on error
bool loadAddsInProgress(const char *prefix) {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	if ( g_conf.m_readOnlyMode ) {
		logTrace( g_conf.m_logTraceMsg4, "END - Read-only mode. Returning true" );
		return true;
	}


	// open the file
	char filename[1024];

	// . a load when in repair mode means something special
	// . see Repair.cpp's call to loadAddState()
	// . if we saved the add state while in repair mode when we exited
	//   then we need to restore just that
	if ( ! prefix ) prefix = "";
	sprintf ( filename, "%s%saddsinprogress.dat", g_hostdb.m_dir , prefix );

	logTrace( g_conf.m_logTraceMsg4, "filename [%s]", filename);

	int32_t fd = open ( filename, O_RDONLY );
	if ( fd < 0 ) {
		if(errno==ENOENT) {
			logTrace( g_conf.m_logTraceMsg4, "END - not found, returning true" );
			return true;
		}
		log(LOG_ERROR, "%s:%s: Failed to open %s for reading: %s",__FILE__,__func__,filename,strerror(errno));
		g_errno = errno;

		logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
		return false;
	}

	struct stat stats;
	if(fstat(fd, &stats) != 0) {
		log(LOG_ERROR, "fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		close(fd);
		return false;
	}
	int32_t p    = 0;
	int32_t pend = stats.st_size;

	
	log(LOG_INFO,"build: Loading %" PRId32" bytes from %s",pend,filename);

	// . deserialize each hostbuf
	// . the # of host bufs
	int32_t numHostBufs;
	int32_t nb;
	nb = (int32_t)read(fd, (char *)&numHostBufs, 4); 
	if ( nb != 4 ) {
		close ( fd );
		logError("Read of message size returned %" PRId32 " bytes instead of 4", nb);
		logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
		return false;
	}

	p += 4;
	if ( numHostBufs != s_numHostBufs ) {
		close ( fd );
		g_errno = EBADENGINEER;
		log( LOG_ERROR, "%s:%s: build: addsinprogress.dat has wrong number of host bufs.", __FILE__, __func__ );
		return false;
	}

	// deserialize each hostbuf
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		// break if nothing left to read
		if ( p >= pend ) {
			break;
		}
		
		// USED size of the buf
		int32_t used;
		nb = (int32_t)read(fd, (char *)&used, 4);
		if ( nb != 4 ) {
			close ( fd );
			logError("Read of message size returned %" PRId32 " bytes instead of 4", nb);
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		p += 4;

		// if used is 0, a NULL buffer, try to read the next one
		if ( used == 0 || used == 4 ) { 
			s_hostBufs    [i] = NULL;
			s_hostBufSizes[i] = 0;
			continue;
		}

		// malloc the min buf size
		int32_t allocSize = MAXHOSTBUFSIZE;
		if ( allocSize < used ) {
			allocSize = used;
		}

		// alloc the buf space, returns NULL and sets g_errno on error
		char *buf = (char *)mmalloc ( allocSize , "Msg4" );
		if( !buf ) 
		{
			close ( fd );
			log(LOG_ERROR,"build: Could not alloc %" PRId32" bytes for reading %s",allocSize,filename);
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		
		// the buf itself
		nb = (int32_t)read(fd, buf, used);
		// sanity
		if ( nb != used ) {
			close ( fd );
			// reset the buffer usage
			//*(int32_t *)(p-4) = 4;
			*(int32_t *)buf = 4;
			// return false
			log(LOG_ERROR,"%s:%s: error reading addsinprogress.dat: %s", __FILE__, __func__, mstrerror(errno));
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		// skip over it
		p += used;
		// sanity check
		if ( *(int32_t *)buf != used ) {
			log(LOG_ERROR, "%s:%s: file %s is bad.",__FILE__,__func__,filename);
			g_process.shutdownAbort(false);
		}
		// set the array
		s_hostBufs     [i] = buf;
		s_hostBufSizes [i] = allocSize;
	}

	// scan in progress msg4 requests too that we stored in this file too
	for ( ; ; ) {
		// break if nothing left to read
		if ( p >= pend ) {
			break;
		}

		int32_t nb;

		// hostid sent to
		int32_t hostId;
		nb = (int32_t)read(fd, (char *)&hostId, 4);
		if ( nb != 4 ) {
			close ( fd );
			logError("Read of message size returned %" PRId32 " bytes instead of 4", nb);
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}

		p += 4;
		// get host
		Host *h = g_hostdb.getHost(hostId);
		// must be there
		if ( ! h ) {
			close (fd);
			log(LOG_ERROR, "%s:%s: bad msg4 hostid %" PRId32,__FILE__,__func__,hostId);

			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}

		int32_t numBytes;
		nb = (int32_t)read(fd, (char *)&numBytes, 4);
		if ( nb != 4 ) {
			close ( fd );
			logError("Read of message size returned %" PRId32 " bytes instead of 4", nb);
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		p += 4;

		// allocate buffer
		char *buf = (char *)mmalloc ( numBytes , "msg4loadbuf");
		if ( ! buf ) {
			close ( fd );
			log(LOG_ERROR, "%s:%s: could not alloc msg4 buf",__FILE__,__func__);
			
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		
		// the buffer
		nb = (int32_t)read(fd, buf, numBytes);
		if ( nb != numBytes ) {
			close ( fd );
			log(LOG_ERROR,"%s:%s: build: bad msg4 buf read", __FILE__, __func__ );
			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
		p += numBytes;

		// send it!
		if (!g_udpServer.sendRequest(buf, numBytes, msg_type_4, h->m_ip, h->m_port, h->m_hostId, NULL, NULL, NULL, udpserver_sendrequest_infinite_timeout)) {
			close ( fd );
			// report it
			log(LOG_WARN, "%s:%s: could not resend reload buf: %s",
				   __FILE__,__func__,mstrerror(g_errno));

			logTrace( g_conf.m_logTraceMsg4, "END - returning false" );
			return false;
		}
	}


	// all done
	close ( fd );

	logTrace( g_conf.m_logTraceMsg4, "END - OK, returning true" );
	return true;
}
