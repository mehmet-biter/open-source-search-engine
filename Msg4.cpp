#include "gb-include.h"

#include "UdpServer.h"
#include "Hostdb.h"
#include "Msg0.h"      // for getRdb(char rdbId)
#include "Msg4.h"
#include "Clusterdb.h"
#include "Spider.h"
#include "Rdb.h"
#include "Profiler.h"
#include "Repair.h"
#include "Multicast.h"
#include "JobScheduler.h"
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
//#define MSG4_WAIT 500

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

static bool       addMetaList ( const char *p , class UdpSlot *slot = NULL );
static void       gotReplyWrapper4 ( void    *state   , void *state2   ) ;
static void       storeLineWaiters ( ) ;
static void       handleRequest4   ( UdpSlot *slot    , int32_t  niceness ) ;
static void       sleepCallback4   ( int bogusfd      , void *state    ) ;
static bool       sendBuffer       ( int32_t hostId , int32_t niceness ) ;
static Multicast *getMulticast     ( ) ;
static void       returnMulticast  ( Multicast *mcast ) ;
//static void processSpecialSignal ( collnum_t collnum , char *p ) ;
//static bool storeList2 ( RdbList *list , char rdbId , collnum_t collnum,
//			 bool forceLocal, bool splitList , int32_t niceness );
static bool storeRec   ( collnum_t      collnum , 
			 char           rdbId   ,
			 uint32_t  gid     ,
			 int32_t           hostId  ,
			 const char          *rec     ,
			 int32_t           recSize ,
			 int32_t           niceness ) ;

// all these parameters should be preset
bool registerHandler4 ( ) {
	
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}
	
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( 0x04, handleRequest4 ) )
	{
		log(LOG_ERROR,"%s:%s: Could not register with UDP server!", __FILE__,__func__);
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

	// spider hang bug
	//logf(LOG_DEBUG,"msg4: registering handler.");

	// for now skip it
	//return true;

	// . restore state from disk
	// . false means repair is not active
	if ( ! loadAddsInProgress ( NULL ) ) {
		log(LOG_WARN, "init: Could not load addsinprogress.dat. Ignoring.");
		g_errno = 0;
	}

	// . register sleep handler every 5 seconds = 5000 ms
	// . right now MSG4_WAIT is 500ms... i lowered it from 5s
	//   to speed up spidering so it would harvest outlinks
	//   faster and be able to spider them right away.
	// . returns false on failure
	bool rc = g_loop.registerSleepCallback(MSG4_WAIT,NULL,sleepCallback4 );

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - returning %s", __FILE__,__func__, rc?"true":"false");
	}
	
	return rc;
}


static void flushLocal ( ) ;

// scan all host bufs and try to send on them
void sleepCallback4 ( int bogusfd , void    *state ) {
	// wait for clock to be in sync
	if ( ! isClockInSync() ) return;
	// flush them buffers
	flushLocal();
}

void flushLocal ( ) {
	g_errno = 0;
	// put the line waiters into the buffers in case they are not there
	//storeLineWaiters();
	// now try to send the buffers
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) 
		sendBuffer ( i , MAX_NICENESS );
	g_errno = 0;
}

//static void (* s_flushCallback) ( void *state ) = NULL ;
//static void  * s_flushState = NULL;

// for holding flush callback data
static SafeBuf s_callbackBuf;
static int32_t    s_numCallbacks = 0;

class CBEntry {
public:
	int64_t m_timestamp;
	void (*m_callback)(void *);
	void *m_callbackState;
};


// . injecting into the "qatest123" coll flushes after each inject
// . returns false if blocked and callback will be called
bool flushMsg4Buffers ( void *state , void (* callback) (void *) ) {
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}

	
	// if all empty, return true now
	if ( ! hasAddsInQueue () ) 
	{
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - nothing queued, returning true", __FILE__,__func__);
		}
		return true;
	}

	// how much per callback?
	int32_t cbackSize = sizeof(CBEntry);
	// ensure big enough for first call
	if ( s_callbackBuf.m_capacity == 0 ) { // length() == 0 ) {
		// make big
		if ( ! s_callbackBuf.reserve ( 300 * cbackSize ) ) {
			// return true with g_errno set on error
			log(LOG_ERROR,"%s:%s: END - error allocating space for flush callback, returning true", __FILE__,__func__);
			return true;
		}

		// then init
		s_callbackBuf.zeroOut();
	}

	// scan for empty slot
	char *buf = s_callbackBuf.getBufStart();
	CBEntry *cb    = (CBEntry *)buf;
	CBEntry *cbEnd = (CBEntry *)(buf + s_callbackBuf.getCapacity());

	// find empty slot
	for ( ; cb < cbEnd && cb->m_callback ;  cb++ ) ;

	// no room?
	if ( cb >= cbEnd ) {
		log(LOG_ERROR, "%s:%s: END. msg4: no room for flush callback. count=%"INT32", returning true",
		    __FILE__,__func__,(int32_t)s_numCallbacks);
		    
		g_errno = EBUFTOOSMALL;
		return true;
	}
	
	// add callback to list
	// time must be the same as used by UdpSlot::m_startTime
	cb->m_callback = callback;
	cb->m_callbackState = state;

	// inc count
	s_numCallbacks++;

	//if ( s_flushCallback ) { char *xx=NULL;*xx=0; }
	// start it up
	flushLocal();

	// scan msg4 slots for maximum start time so we can only
	// call the flush done callback when all msg4 slots in udpserver
	// have start times STRICTLY GREATER THAN that, then we will
	// be guaranteed that everything we added has been replied to!
	UdpSlot *slot = g_udpServer.getActiveHead();
	int64_t max = 0LL;
	for ( ; slot ; slot = slot->m_next ) {
		// get its time stamp 
		if ( slot->m_msgType != 0x04 ) continue;
		// must be initiated by us
		if ( ! slot->m_callback ) continue;
		// get it
		if ( max && slot->m_startTime < max ) continue;
		// got a new max
		max = slot->m_startTime;
	}

	// set time AFTER the udpslot gets its m_startTime set so
	// now will be >= each slot's m_startTime.
	cb->m_timestamp = max;

	// can we sometimes flush without blocking? maybe...
	//if ( ! hasAddsInQueue () ) return true;
	// assign it
	//s_flushState    = state;
	//s_flushCallback = callback;
	// we are waiting now
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
	}
	return false;
}


// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue   ( ) {
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}
	
	// if there is an outstanding multicast...
	if ( s_mcastsOut > s_mcastsIn ) 
	{
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - multicast waiting, returning true", __FILE__,__func__);
		}
		return true;
	}
	
	// if we have a msg4 waiting in line...
	if ( s_msg4Head               ) 
	{
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - msg4 waiting, returning true", __FILE__,__func__);
		}
		return true;
	}
		
	// if we have a host buf that has something in it...
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		if ( ! s_hostBufs[i] ) 
		{
			continue;
		}
			
		if ( *(int32_t *)s_hostBufs[i] > 4 ) 
		{
			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - hostbuf waiting, returning true", __FILE__,__func__);
			}
			return true;
		}
	}

	// otherwise, we have nothing queued up to add
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - nothing queued, returning false", __FILE__,__func__);
	}
	return false;
}


// returns false if blocked
bool Msg4::addMetaList ( const char  *metaList                , 
			 int32_t   metaListSize            ,
			 char  *coll                    ,
			 void  *state                   ,
			 void (* callback)(void *state) ,
			 int32_t   niceness                ,
			 char   rdbId                   ) {

	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	return addMetaList ( metaList     ,
			     metaListSize ,
			     collnum      ,
			     state        ,
			     callback     ,
			     niceness     ,
			     rdbId        );
}

bool Msg4::addMetaList ( SafeBuf *sb ,
			 collnum_t  collnum                  ,
			 void      *state                    ,
			 void      (* callback)(void *state) ,
			 int32_t       niceness                 ,
			 char       rdbId                    ,
			 int32_t       shardOverride ) {
	return addMetaList ( sb->getBufStart() ,
			     sb->length() ,
			     collnum ,
			     state ,
			     callback ,
			     niceness ,
			     rdbId ,
			     shardOverride );
}


bool Msg4::addMetaList ( const char      *metaList                 , 
			 int32_t       metaListSize             ,
			 collnum_t  collnum                  ,
			 void      *state                    ,
			 void      (* callback)(void *state) ,
			 int32_t       niceness                 ,
			 char       rdbId                    ,
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
	//if ( collnum < 0 || collnum > 1000 ) { char *xx=NULL;*xx=0; }
	if ( collnum < 0 ) { char *xx=NULL;*xx=0; }

	// if first time set this
	m_currentPtr   = metaList;
	m_metaList     = metaList;
	m_metaListSize = metaListSize;
	m_collnum      = collnum;
	m_state        = state;
	m_callback     = callback;
	m_rdbId        = rdbId;
	m_niceness     = niceness;
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
			log("msg4: queueing body msg4=0x%"PTRFMT"",(PTRTYPE)this);
		// mark it
		m_inUse = true;
		// all done then, but return false so caller does not free
		// this msg4
		return false;
	}

	// then do it
	if ( addMetaList2 ( ) ) return true;

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
	//if ( s_msg4Head || s_msg4Tail ) { char *xx=NULL; *xx=0; }
	if ( s_msg4Head || s_msg4Tail ) {
		log("msg4: got unexpected head"); // :)
		goto retry;
	}

	// . spider hang bug
	// . debug log. seems to happen a lot if not using threads..
	if ( g_jobScheduler.are_new_jobs_allowed() )
		logf(LOG_DEBUG,"msg4: queueing head msg4=0x%"PTRFMT"",(PTRTYPE)this);

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

bool isInMsg4LinkedList ( Msg4 *msg4 ) {
	Msg4 *m = s_msg4Head;
	for ( ; m ; m = m->m_next ) 
		if ( m == msg4 ) return true;
	return false;
}

bool Msg4::addMetaList2 ( ) {
	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}


	const char *p = m_currentPtr;

	// get the collnum
	//collnum_t collnum = g_collectiondb.getCollnum ( m_coll );

	const char *pend = m_metaList + m_metaListSize;

#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(p,pend-p);
#endif
	//if ( m_collnum < 0 || m_collnum > 1000 ) { char *xx=NULL;*xx=0; }
	if ( m_collnum < 0 ) { char *xx=NULL;*xx=0; }

	// store each record in the list into the send buffers
	for ( ; p < pend ; ) {
		// first is rdbId
		char rdbId = m_rdbId;
		if ( rdbId < 0 ) rdbId = *p++;
		// get nosplit
		//bool nosplit = ( rdbId & 0x80 ) ;
		// mask off rdbId
		rdbId &= 0x7f;

		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s:   rdbId: %02x", __FILE__,__func__, rdbId);
		}


		// get the key of the current record
		const char *key = p; 
		// negative key?
		bool del = !( *p & 0x01 );

		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s:   Negative key: %s", __FILE__,__func__, del?"true":"false");
		}

		// get the key size. a table lookup in Rdb.cpp.
		int32_t ks = getKeySizeFromRdbId ( rdbId );
			
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: Key size: %"INT32"", __FILE__,__func__, ks);
		}
			
			
		// skip key
		p += ks;
		// set this
		//bool split = true; if ( nosplit ) split = false;
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		//uint32_t gid = getGroupId ( rdbId , key , split );
		uint32_t shardNum = getShardNum( rdbId , key );

		// override it from Rebalance.cpp for redistributing records
		// after updating hosts.conf?
		if ( m_shardOverride >= 0 ) shardNum = m_shardOverride;
			
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s:   shardNum: %"INT32"", __FILE__,__func__, shardNum);
		}

		// get the record, is -1 if variable. a table lookup.
		// . negative keys have no data
		// . this unfortunately is not true according to RdbList.cpp
		int32_t dataSize = del ? 0 : getDataSizeFromRdbId ( rdbId );

		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s:   dataSize: %"INT32"", __FILE__,__func__, dataSize);
		}
			
		// if variable read that in
		if ( dataSize == -1 ) {
			// -1 means to read it in
			dataSize = *(int32_t *)p;
			// sanity check
			if ( dataSize < 0 ) { char *xx=NULL;*xx=0; }

			// skip dataSize
			p += 4;

			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s:   dataSize: %"INT32" (variable size read)", __FILE__,__func__, dataSize);
			}
		}

		// skip over the data, if any
		p += dataSize;
		
		// breach us?
		if ( p > pend ) { char *xx=NULL;*xx=0; }
			
		// i fixed UdpServer.cpp to NOT call msg4 handlers when in
		// a quickpoll, in case we receive a niceness 0 msg4 request
		QUICKPOLL(m_niceness);
 		
		// convert the gid to the hostid of the first host in this
		// group. uses a quick hash table.
		//int32_t hostId = g_hostdb.makeHostIdFast ( gid );
		Host *hosts = g_hostdb.getShard ( shardNum );
		int32_t hostId = hosts[0].m_hostId;
		
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s:   hostId: %"INT32"", __FILE__,__func__, hostId);
			loghex(LOG_TRACE, key, ks, "Key: (hexdump)");
		}
		
		
		// . add that rec to this groupId, gid, includes the key
		// . these are NOT allowed to be compressed (half bit set)
		//   and this point
		// . this returns false and sets g_errno on failure
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(key,p-key);
#endif
		if ( storeRec ( m_collnum, rdbId, shardNum, hostId, key, p - key, m_niceness )) {
			// . point to next record
			// . will point past records if no more left!
			m_currentPtr = p; // += recSize;
			// debug log
			// int off = (int)(m_currentPtr-m_metaList);
			// log("msg4: cpoff=%i",off);
			// debug
			// get next rec
			continue;
		}

		// g_errno is not set if the store rec could not send the
		// buffer because no multicast was available
		if ( g_errno ) 
		{
			log(LOG_ERROR, "%s:%s: build: Msg4 storeRec had error: %s.",
			    __FILE__, __func__, mstrerror(g_errno));
		}

		// clear this just in case
		g_errno = 0;

		// if g_errno was not set, this just means we do not have
		// room for the data yet, and try again later
		return false;
	}

	// . send out all bufs
	// . before we were caching to reduce packet traffic, but
	//   since we don't use the network for sending termlists let's
	//   try going back to making it even more real-time
	//if ( ! isClockInSync() ) return true;
	// flush them buffers
	//flushLocal();
			       
	// in case this was being used to hold the data, free it
	m_tmpBuf.purge();

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - OK, true", __FILE__,__func__);
	}

	return true;
}


// . modify each Msg4 request as follows
// . collnum(2bytes)|rdbId(1bytes)|listSize&rawlistData|...
// . store these requests in the buffer just like that
bool storeRec ( collnum_t      collnum , 
		char           rdbId   ,
		uint32_t  shardNum, //gid
		int32_t           hostId  ,
		const char          *rec     ,
		int32_t           recSize ,
		int32_t           niceness ) {
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&collnum,sizeof(collnum));
	VALGRIND_CHECK_MEM_IS_DEFINED(&rdbId,sizeof(rdbId));
	VALGRIND_CHECK_MEM_IS_DEFINED(&shardNum,sizeof(shardNum));
	VALGRIND_CHECK_MEM_IS_DEFINED(&recSize,sizeof(recSize));
	VALGRIND_CHECK_MEM_IS_DEFINED(rec,recSize);
#endif
	// loop back up here if you have to flush the buffer
 retry:

	// sanity check
	//if ( recSize==16 && rdbId==RDB_SPIDERDB && *(int32_t *)(rec+12)!=0 ) {
	//	char *xx=NULL; *xx=0; }
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
	if ( used < 12 ) { char *xx = NULL; *xx = 0; }
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
		if ( ! sendBuffer ( hostId , niceness ) ) return false;
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
bool sendBuffer ( int32_t hostId , int32_t niceness ) {
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
		//char *xx=NULL ; *xx=0; }
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
		logf(LOG_DEBUG,"syncdb: sending msg4 request zid=%"UINT64"",zid);

	// this is the request
	char *request     = buf;
	int32_t  requestSize = used;
	// . launch the request
	// . we now have this multicast timeout if a host goes dead on it
	//   and it fails to send its payload
	// . in that case we should restart from the top and we will add
	//   the dead host ids to the top, and multicast will avoid sending
	//   to hostids that are dead now
	key_t k; k.setMin();
	if ( mcast->send ( request    , // sets mcast->m_msg    to this
			   requestSize, // sets mcast->m_msgLen to this
			   0x04       , // msgType for add rdb record
			   false      , // does multicast own msg?
			   shardNum,//groupId , // group to send to (groupKey)
			   true       , // send to whole group?
			   0          , // key is useless for us
			   (void *)(PTRTYPE)allocSize  , // state data
			   (void *)mcast      , // state data
			   gotReplyWrapper4 ,
			   // this was 60 seconds, but if we saved the
			   // addsinprogress at the wrong time we might miss
			   // it when its between having timed out and
			   // having been resent by us!
			   multicast_infinite_send_timeout   , // timeout
			   MAX_NICENESS, // niceness
			   -1         , // first host to try
			   NULL       , // replyBuf        = NULL ,
			   0          , // replyBufMaxSize = 0 ,
			   true       , // freeReplyBuf    = true ,
			   false      , // doDiskLoadBalancing = false ,
			   -1         , // no max cache age limit
			   k          , // cache key
			   RDB_NONE   , // bogus rdbId
			   -1         , // unknown minRecSizes read size
			   true      )) { // sendToSelf?
		// . let storeRec() do all the allocating...
		// . only let the buffer go once multicast succeeds
		s_hostBufs [ hostId ] = NULL;
		// success
		return true;
	}

	// g_errno should be set
	log("net: Had error when sending request to add data to rdb shard "
	    "#%"UINT32": %s.", shardNum,mstrerror(g_errno));

	returnMulticast ( mcast );

	return false;
}

Multicast *getMulticast ( ) {
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
	if ( avail->m_inUse ) { char *xx=NULL;*xx=0; }
	// return that
	return avail;
}

void returnMulticast ( Multicast *mcast ) {
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
void gotReplyWrapper4 ( void *state , void *state2 ) {
	//logf(LOG_DEBUG,"build: got msg4 reply");
	int32_t       allocSize = (int32_t)(PTRTYPE)state;
	Multicast *mcast     = (Multicast *)state2;
	// get the request we sent
	char *request     = mcast->m_msg;
	//int32_t  requestSize = mcast->m_msgSize;
	// get the buffer alloc size
	//int32_t allocSize = requestSize;
	//if ( allocSize < MAXHOSTBUFSIZE ) allocSize = MAXHOSTBUFSIZE;
	if ( request ) mfree ( request , allocSize , "Msg4" );
	// make sure no one else can free it!
	mcast->m_msg = NULL;

	// get the udpslot that is replying here
	UdpSlot *replyingSlot = mcast->m_slot;
	if ( ! replyingSlot ) { char *xx=NULL;*xx=0; }

	returnMulticast ( mcast );

	storeLineWaiters ( ); // try to launch more msg4 requests in waiting

	//
	// now if all buffers are empty, let any flush request know that
	//

	// bail if no callbacks to call
	if ( s_numCallbacks == 0 ) return;

	//log("msg4: got msg4 reply. replyslot starttime=%"INT64" slot=0x%"XINT32"",
	//    replyingSlot->m_startTime,(int32_t)replyingSlot);

	// get the oldest msg4 slot starttime
	UdpSlot *slot = g_udpServer.getActiveHead();
	int64_t min = 0LL;
	for ( ; slot ; slot = slot->m_next ) {
		// get its time stamp
		if ( slot->m_msgType != 0x04 ) continue;
		// must be initiated by us
		if ( ! slot->m_callback ) continue;
		// if it is this replying slot or already had the callback
		// called, then ignore it...
		if ( slot->m_calledCallback ) continue;
		// ignore incoming slot! that could be the slot we were
		// waiting for to complete so its starttime will always
		// be less than our callback's m_timestamp
		//if ( slot == replyingSlot ) continue;
		// log it
		//log("msg4: slot starttime = %"INT64" ",slot->m_startTime);
		// get it
		if ( min && slot->m_startTime >= min ) continue;
		// got a new min
		min = slot->m_startTime;
	}

	// log it
	//log("msg4: slots min = %"INT64" ",min);

	// scan for slots whose callbacks we can call now
	char *buf = s_callbackBuf.getBufStart();
	CBEntry *cb    = (CBEntry *)buf;
	CBEntry *cbEnd = (CBEntry *)(buf + s_callbackBuf.getCapacity());

	// find empty slot
	for ( ; cb < cbEnd ;  cb++ ) {
		// skip if empty
		if ( ! cb->m_callback ) continue;
		// debug
		//log("msg4: cb timestamp = %"INT64"",cb->m_timestamp);
		// wait until callback's stored time is <= all msg4
		// slot's start times, then we can guarantee that all the
		// msg4s required for this callback have replied.
		// min will be zero if no msg4s in there, so call callback.
		if ( min && cb->m_timestamp >= min ) continue;
		// otherwise, call the callback!
		cb->m_callback ( cb->m_callbackState );
		// take out of queue now by setting callback ptr to 0
		cb->m_callback = NULL;
		// discount
		s_numCallbacks--;
	}

	// of course, skip this part if nobody called a flush
	//if ( ! s_flushCallback ) return;
	// if not completely empty, wait!
	if ( hasAddsInQueue () ) {
		// flush away some more just in case
		flushLocal();
		// and wait
		return;
	}
	// seems good to go!
	//s_flushCallback ( s_flushState );
	// nuke it
	//s_flushCallback = NULL;
}

void storeLineWaiters ( ) {
	// try to store all the msg4's lists that are waiting in line
 loop:
	Msg4 *msg4 = s_msg4Head;
	// now were we waiting on a multicast to return in order to send
	// another request?  return if not.
	if ( ! msg4 ) return;
	// grab the first Msg4 in line. ret fls if blocked adding more of list.
	if ( ! msg4->addMetaList2 ( ) ) return;
	// hey, we were able to store that Msg4's list, remove him
	s_msg4Head = msg4->m_next;
	// empty? make tail NULL too then
	if ( ! s_msg4Head ) s_msg4Tail = NULL;
	// . if his callback was NULL, then was loaded in loadAddsInProgress()
	// . we no longer do that so callback should never be null now
	if ( ! msg4->m_callback ) { char *xx=NULL;*xx=0; }
	// log this now i guess. seems to happen a lot if not using threads
	if ( g_jobScheduler.are_new_jobs_allowed() )
		logf(LOG_DEBUG,"msg4: calling callback for msg4=0x%"PTRFMT"",
		     (PTRTYPE)msg4);
	// release it
	msg4->m_inUse = false;
	// call his callback
	msg4->m_callback ( msg4->m_state );
	// ensure not re-added - no, msg4 might be freed now!
	//msg4->m_next = NULL;
	// try the next Msg4 in line
	goto loop;
}

#include "Process.h"

// . destroys the slot if false is returned
// . this is registered in Msg4::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
void handleRequest4 ( UdpSlot *slot , int32_t netnice ) {

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}

	// easy var
	UdpServer *us = &g_udpServer;

	// if we just came up we need to make sure our hosts.conf is in
	// sync with everyone else before accepting this! it might have
	// been the case that the sender thinks our hosts.conf is the same
	// since last time we were up, so it is up to us to check this
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		g_errno = EBADHOSTSCONF;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply ( slot , g_errno );
		
		log(LOG_WARN,"%s:%s: END - hostsConfInDisagreement", __FILE__,__func__);
		return;
	}

	// need to be in sync first
	if ( ! g_pingServer.m_hostsConfInAgreement ) {
		// . if we do not know the sender's hosts.conf crc, wait 4 it
		// . this is 0 if not received yet
		if ( ! slot->m_host->m_pingInfo.m_hostsConfCRC ) {
			g_errno = EWAITINGTOSYNCHOSTSCONF;
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			us->sendErrorReply ( slot , g_errno );
			
			log(LOG_WARN,"%s:%s: END - EWAITINGTOSYNCHOSTCONF", __FILE__,__func__);
			return;
		}
		
		// compare our hosts.conf to sender's otherwise
		if ( slot->m_host->m_pingInfo.m_hostsConfCRC != 
		     g_hostdb.getCRC() ) {
			g_errno = EBADHOSTSCONF;
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			us->sendErrorReply ( slot , g_errno );
			
			log(LOG_WARN,"%s:%s: END - EBADHOSTSCONF", __FILE__,__func__);
			return;
		}
	}


	//logf(LOG_DEBUG,"build: handling msg4 request");
	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	
	// must at least have an rdbId
	if ( readBufSize < 7 ) {
		g_errno = EREQUESTTOOSHORT;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply ( slot , g_errno );
		
		log(LOG_ERROR,"%s:%s: END - EREQUESTTOOSHORT", __FILE__,__func__);
		return;
	}
	

	// get total buf used
	int32_t used = *(int32_t *)readBuf; //p += 4;

	// sanity check
	if ( used != readBufSize ) {
		// if we send back a g_errno then multicast retries forever
		// so just absorb it!
		log(LOG_ERROR,"%s:%s: msg4: got corrupted request from hostid %"INT32" "
		    "used [%"INT32"] != readBufSize [%"INT32"]",
		    __FILE__, 
		    __func__,
		    slot->m_host->m_hostId,
		    used,
		    readBufSize);

		loghex(LOG_ERROR, readBuf, (readBufSize < 160 ? readBufSize : 160), "readBuf (first max. 160 bytes)");
		    
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;
		//us->sendErrorReply(slot,ECORRUPTDATA);return;}
		
		log(LOG_ERROR,"%s:%s: END", __FILE__,__func__);
		return;
	}

	// if we did not sync our parms up yet with host 0, wait...
	if ( g_hostdb.m_hostId != 0 && ! g_parms.m_inSyncWithHost0 ) {
		// limit logging to once per second
		static int32_t s_lastTime = 0;
		int32_t now = getTimeLocal();
		if ( now - s_lastTime >= 1 ) {
			s_lastTime = now;
			log("msg4: waiting to sync with "
			    "host #0 before accepting data");
		}
		// tell send to try again shortly
		g_errno = ETRYAGAIN;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply(slot,g_errno);
		
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - ETRYAGAIN. Waiting to sync with host #0", __FILE__,__func__);
		}
		return; 
	}

	// this returns false with g_errno set on error
   if ( ! addMetaList ( readBuf , slot ) ) {
   		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply(slot,g_errno);
		
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - addMetaList returned false. g_errno=%d", __FILE__,__func__, g_errno);
		}
		return; 
	}

	// good to go
	us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - OK", __FILE__,__func__);
	}
}


// . Syncdb.cpp will call this after it has received checkoff keys from
//   all the alive hosts for this zid/sid
// . returns false and sets g_errno on error, returns true otherwise
bool addMetaList ( const char *p , UdpSlot *slot ) {

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"syncdb: calling addMetalist zid=%"UINT64"",
		     *(int64_t *)(p+4));

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

	// . this request consists of multiple recs, so add each one
	// . collnum(2bytes)/rdbId(1byte)/recSize(4bytes)/recData/...
 loop:
	// extract collnum, rdbId, recSize
	collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);
	char      rdbId   = *(char      *)p; p += 1;
	int32_t      recSize = *(int32_t      *)p; p += 4;
	// shortcut
	//UdpServer *us = &g_udpServer;
	// . get the rdb to which it belongs, use Msg0::getRdb()
	// . do not call this for every rec if we do not have to
	if ( rdbId != lastRdbId ) {
		rdb = getRdbFromId ( (char) rdbId );

		// an uninitialized secondary rdb? it will have a keysize
		// of 0 if its never been intialized from the repair page.
		// don't core any more, we probably restarted this shard
		// and it needs to wait for host #0 to syncs its
		// g_conf.m_repairingEnabled to '1' so it can start its
		// Repair.cpp repairWrapper() loop and init the secondary
		// rdbs so "rdb" here won't be NULL any more.
		if ( rdb && rdb->m_ks <= 0 ) {
			time_t currentTime = getTime();
			static time_t s_lastTime = 0;
			if ( currentTime > s_lastTime + 10 ) {
				s_lastTime = currentTime;
				log("msg4: oops. got an rdbId key for a "
				    "secondary "
				    "rdb and not in repair mode. waiting to "
				    "be in repair mode.");
				g_errno = ETRYAGAIN;
				return false;
				//char *xx=NULL;*xx=0;
			}
		}

		if ( ! rdb ) {
			if ( slot ) 
				log("msg4: rdbId of %"INT32" unrecognized "
				    "from hostip=%s. "
				    "dropping WHOLE request", (int32_t)rdbId,
				    iptoa(slot->m_ip));
			else
				log("msg4: rdbId of %"INT32" unrecognized. "
				    "dropping WHOLE request", (int32_t)rdbId);
			g_errno = ETRYAGAIN;
			return false;
		}
	}

	// . if already in addList and we are quickpoll interruptint, try again
	// . happens if our niceness gets converted to 0
	if ( rdb->m_inAddList ) {
		g_errno = ETRYAGAIN;
		return false;
	}

	// sanity check
	if ( p + recSize > pend ) { g_errno = ECORRUPTDATA; return false; }
	// reset g_errno
	g_errno = 0;
	// . make a list from this data
	// . skip over the first 4 bytes which is the rdbId
	// . TODO: embed the rdbId in the msgtype or something...
	RdbList list;
	// sanity check
	if ( rdb->getKeySize() == 0 ) {
		log("seems like a stray /e/repair-addsinprogress.dat file "
		    "rdbId=%"INT32". waiting to be in repair mode."
		    ,(int32_t)rdbId);
		    //not in repair mode. dropping.",(int32_t)rdbId);
		g_errno = ETRYAGAIN;
		return false;
		//char *xx=NULL;*xx=0;
		// drop it for now!!
		//p += recSize;
		//if ( p < pend ) goto loop;
		// all done
		//return true;
	}
	// set the list
	list.set ( (char*)p                , //todo: dodgy cast. RdbList should be fixed
		   recSize                 ,
		   (char*)p                , //todo: dodgy cast. RdbList should be fixed
		   recSize                 ,
		   rdb->getFixedDataSize() ,
		   false                   ,  // ownData?
		   rdb->useHalfKeys()      ,
		   rdb->getKeySize ()      ); 
	// advance over the rec data to point to next entry
	p += recSize;
	// keep track of stats
	rdb->readRequestAdd ( recSize );
	// this returns false and sets g_errno on error
	bool status =rdb->addList(collnum, &list, MAX_NICENESS );

	// bad coll #? ignore it. common when deleting and resetting
	// collections using crawlbot. but there are other recs in this
	// list from different collections, so do not abandon the whole 
	// meta list!! otherwise we lose data!!
	if ( g_errno == ENOCOLLREC && !status ) { g_errno = 0; status = true; }

	// do the next record here if there is one
	if ( status && p < pend ) goto loop;

	// no memory means to try again
	if ( g_errno == ENOMEM ) g_errno = ETRYAGAIN;
	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC  &&
	     g_repairMode > 0       )
	     //g_repair.m_fullRebuild   )
		g_errno = ETRYAGAIN;
	// ignore enocollrec errors since collection can be reset while
	// spiders are on now.
	//if ( g_errno == ENOCOLLREC )
	//	g_errno = 0;
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
bool saveAddsInProgress ( const char *prefix ) {

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

	int32_t fd = open ( filename, O_RDWR | O_CREAT | O_TRUNC ,
			    getFileCreationFlags() );
			 // S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH );
	if ( fd < 0 ) {
		log ("build: Failed to open %s for writing: %s",
		     filename,strerror(errno));
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

	// scan in progress msg4 requests too!
	UdpSlot *slot = g_udpServer.m_head2;
	for ( ; slot ; slot = slot->m_next2 ) {
		// skip if not msg4
		if ( slot->m_msgType != 0x04 ) continue;
		// skip if we did not initiate it
		if ( ! slot->m_callback ) continue;
		// skip if got reply
		if ( slot->m_readBuf ) continue;
		// write hostid sent to
		write ( fd , &slot->m_hostId , 4 );
		// write that
		write ( fd , &slot->m_sendBufSize , 4 );
		// then the buf data itself
		write ( fd , slot->m_sendBuf , slot->m_sendBufSize );
	}
	

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
	sprintf ( newFilename , "%s%saddsinprogress.dat",
		  g_hostdb.m_dir , prefix );

	::rename ( filename , newFilename );
	return true;
}



// . returns false on an unrecoverable error, true otherwise
// . sets g_errno on error
bool loadAddsInProgress ( const char *prefix ) {

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: BEGIN", __FILE__,__func__);
	}

	if ( g_conf.m_readOnlyMode ) 
	{
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - Read-only mode. Returning true", __FILE__,__func__);
		}
		return true;
	}


	// open the file
	char filename[1024];

	// . a load when in repair mode means something special
	// . see Repair.cpp's call to loadAddState()
	// . if we saved the add state while in repair mode when we exited
	//   then we need to restore just that
	if ( ! prefix ) prefix = "";
	sprintf ( filename, "%s%saddsinprogress.dat",
		  g_hostdb.m_dir , prefix );

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: filename [%s]", __FILE__,__func__, filename);
	}

	// if file does not exist, return true, not really an error
	struct stat stats;
	stats.st_size = 0;
	int status = stat ( filename , &stats );
	if ( status != 0 && errno == ENOENT ) 
	{
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - not found, returning true", __FILE__,__func__);
		}
		return true;
	}

	// get the fileSize into "pend"
	int32_t p    = 0;
	int32_t pend = stats.st_size;

	int32_t fd = open ( filename, O_RDONLY );
	if ( fd < 0 ) {
		log(LOG_ERROR, "%s:%s: Failed to open %s for reading: %s",__FILE__,__func__,filename,strerror(errno));
		g_errno = errno;
		
		if( g_conf.m_logTraceMsg ) {
			log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
		}
		return false;
	}

	log(LOG_INFO,"build: Loading %"INT32" bytes from %s",pend,filename);

	// . deserialize each hostbuf
	// . the # of host bufs
	int32_t numHostBufs;
	read ( fd , (char *)&numHostBufs , 4 ); 
	p += 4;
	if ( numHostBufs != s_numHostBufs ) {
		g_errno = EBADENGINEER;

		log(LOG_ERROR,"%s:%s: build: addsinprogress.dat has wrong number of host bufs.",
			__FILE__,__func__);
		return false;
	}

	// deserialize each hostbuf
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		// break if nothing left to read
		if ( p >= pend ) break;
		// USED size of the buf
		int32_t used;
		read ( fd , (char *)&used , 4 );
		p += 4;
		// if used is 0, a NULL buffer, try to read the next one
		if ( used == 0 || used == 4 ) { 
			s_hostBufs    [i] = NULL;
			s_hostBufSizes[i] = 0;
			continue;
		}
		// malloc the min buf size
		int32_t allocSize = MAXHOSTBUFSIZE;
		if ( allocSize < used ) allocSize = used;
		// alloc the buf space, returns NULL and sets g_errno on error
		char *buf = (char *)mmalloc ( allocSize , "Msg4" );
		if ( ! buf ) 
		{
			log(LOG_ERROR,"build: Could not alloc %"INT32" bytes for "
					"reading %s",allocSize,filename);

			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
		
		// the buf itself
		int32_t nb = read ( fd , buf , used );
		// sanity
		if ( nb != used ) {
			// reset the buffer usage
			//*(int32_t *)(p-4) = 4;
			*(int32_t *)buf = 4;
			// return false
			log(LOG_ERROR,"%s:%s: error reading addsinprogress.dat: %s", 
				__FILE__,__func__,mstrerror(errno));
				   
			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
		// skip over it
		p += used;
		// sanity check
		if ( *(int32_t *)buf != used ) {
			log(LOG_ERROR, "%s:%s: file %s is bad.",__FILE__,__func__,filename);
			g_process.shutdownAbort(false);
			return false;
		}
		// set the array
		s_hostBufs     [i] = buf;
		s_hostBufSizes [i] = allocSize;
	}

	// scan in progress msg4 requests too that we stored in this file too
	for ( ; ; ) {
		// break if nothing left to read
		if ( p >= pend ) break;
		// hostid sent to
		int32_t hostId;
		read ( fd , (char *)&hostId , 4 );
		p += 4;
		// get host
		Host *h = g_hostdb.getHost(hostId);
		// must be there
		if ( ! h ) {
			close (fd);
			log(LOG_ERROR, "%s:%s: bad msg4 hostid %"INT32"",__FILE__,__func__,hostId);

			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
		// host many bytes
		int32_t numBytes;
		read ( fd , (char *)&numBytes , 4 );
		p += 4;
		// allocate buffer
		char *buf = (char *)mmalloc ( numBytes , "msg4loadbuf");
		if ( ! buf ) {
			close ( fd );
			log(LOG_ERROR, "%s:%s: could not alloc msg4 buf",__FILE__,__func__);
			
			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
		
		// the buffer
		int32_t nb = read ( fd , buf , numBytes );
		if ( nb != numBytes ) {
			close ( fd );
			log(LOG_ERROR,"%s:%s: build: bad msg4 buf read", __FILE__,__func__);

			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
		p += numBytes;

		// send it!
		if ( ! g_udpServer.sendRequest ( buf ,
						 numBytes ,
						 0x04     ,   // msgType
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL         ,
						 NULL         , // state data
						 NULL , // callback
						 udpserver_sendrequest_infinite_timeout)){// timeout
			close ( fd );
			// report it
			log(LOG_WARN, "%s:%s: could not resend reload buf: %s",
				   __FILE__,__func__,mstrerror(g_errno));

			if( g_conf.m_logTraceMsg ) {
				log(LOG_TRACE,"%s:%s: END - returning false", __FILE__,__func__);
			}
			return false;
		}
	}


	// all done
	close ( fd );

	if( g_conf.m_logTraceMsg ) {
		log(LOG_TRACE,"%s:%s: END - OK, returning true", __FILE__,__func__);
	}
	return true;
}



