#include "gb-include.h"

#include "Msg1.h"
#include "Clusterdb.h"
#include "Spider.h"
#include "Rdb.h"
#include "Profiler.h"
#include "Repair.h"
#include "Process.h"


static void gotReplyWrapper1 ( void    *state , void *state2 ) ;
static void handleRequest1   ( UdpSlot *slot  , int32_t niceness ) ;

// . all these parameters should be preset
bool Msg1::registerHandler ( ) {
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( msg_type_1, handleRequest1 ) )
		return false;
	return true;
}

// . have an array of Msg1s we use for doing no-wait sending
// . this is like the stacks in Threads.cpp
// . BUG: when set to 800 and adding a file of 1 million urls on newspaper
//        archive machines, Multicast bitches about ENOSLOTS, and sometimes
//        udp bitches about no slots available and we don't get all the urls
//        added. so let's decrease from 800 to 20.
#define MAX_MSG1S 100
static Msg1  s_msg1 [ MAX_MSG1S ];
static int32_t  s_next [ MAX_MSG1S ];
static int32_t  s_head = 0 ;
static bool  s_init = false;
static Msg1 *getMsg1    ( ) ;
static void  returnMsg1 ( void *state );
static void  init       ( );
// returns NULL if none left
Msg1 *getMsg1 ( ) {
	if ( ! s_init ) { init(); s_init = true; }
	if ( s_head == -1 ) return NULL;
	int32_t i = s_head;
	s_head = s_next [ s_head ];
	// debug msg
	//log("got mcast=%" PRId32,(int32_t)(&s_msg1[i].m_mcast));
	return &s_msg1[i];
}
void returnMsg1 ( void *state ) {
	Msg1 *msg1 = (Msg1 *)state;
	// free this if we have to
	msg1->m_ourList.freeList();
	// debug msg
	//log("return mcast=%" PRId32,(int32_t)(&msg1->m_mcast));
	int32_t i = msg1 - s_msg1;
	if ( i < 0 || i > MAX_MSG1S ) {
		log(LOG_LOGIC,"net: msg1: Major problem adding data."); 
		g_process.shutdownAbort(true); }
	if ( s_head == -1 ) { s_head    = i      ; s_next[i] = -1; }
	else                { s_next[i] = s_head ; s_head    =  i; }
}
void init ( ) {
	// init the linked list
	for ( int32_t i = 0 ; i < MAX_MSG1S ; i++ ) {
		if ( i == MAX_MSG1S - 1 ) s_next[i] = -1;
		else                      s_next[i] = i + 1;
		// these guys' constructor is not called, so do it?
		//s_msg1[i].m_ourList.m_alloc = NULL;
	}
	s_head = 0;
}

bool Msg1::addRecord ( char *rec , 
		       int32_t recSize , 
		       char          rdbId             ,
		       collnum_t collnum ,
		       void         *state             ,
		       void (* callback)(void *state)  ,
		       int32_t          niceness          ) {

	key_t sk;
	key_t ek;
	sk.setMin();
	ek.setMax();
	//RdbList list;
	m_tmpList.set ( rec , 
		   recSize ,
		   rec ,
		   recSize ,
		   (char *)&sk,
		   (char *)&ek,
		   -1 , // fixed data size
		   false , // owndata?
		   false , // use half keys?
		   sizeof(key_t));
	return addList ( &m_tmpList ,
			 rdbId ,
			 collnum,//g_collectiondb.m_recs[collnum]->m_coll ,
			 state ,
			 callback ,
			 false , // force local?
			 niceness );
}

// . send an add command to all machines in the appropriate group
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . groupId is -1 if we choose it automatically
// . if waitForReply is false we return true right away, but we can only
//   launch MAX_MSG1S requests without waiting for replies, and
//   when the reply does come back we do NOT call the callback
bool Msg1::addList ( RdbList      *list              ,
		     char          rdbId             ,
		     collnum_t collnum, // char         *coll              ,
		     void         *state             ,
		     void (* callback)(void *state)  ,
		     bool          forceLocal        ,
		     int32_t          niceness          ,
		     bool          injecting         ,
		     bool          waitForReply      ,
		     bool         *inTransit         ) {
	// warning
	if ( collnum<0 ) log(LOG_LOGIC,"net: bad collection. msg1.cpp.");
	// if list has no records in it return true
	if ( ! list || list->isEmpty() ) return true;
	// sanity check
	if ( list->m_ks !=  8 &&
	     list->m_ks != 12 &&
	     list->m_ks != 16 &&
	     list->m_ks != 24 ) { 
		g_process.shutdownAbort(true); }
	// start at the beginning
	list->resetListPtr();
	// if caller does not want reply try to accomodate him
	if ( ! waitForReply && list != &m_ourList ) {
		Msg1 *Y = getMsg1();
		if ( ! Y ) { 
			waitForReply = true; 
			log(LOG_DEBUG,"net: msg1: "
			    "No floating request slots "
			    "available for adding data. "
			    "Blocking on reply."); 
			goto skip; 
		}
		// steal the list, we don't want caller to free it
		gbmemcpy ( &Y->m_ourList , list , sizeof(RdbList) );
		
 		QUICKPOLL(niceness);
		
		// if list is small enough use our buf
		if ( ! list->m_ownData && list->m_listSize <= MSG1_BUF_SIZE ) {
			gbmemcpy ( Y->m_buf , list->m_list , list->m_listSize );
			Y->m_ourList.m_list    = Y->m_buf;
			Y->m_ourList.m_listEnd = Y->m_buf + list->m_listSize;
			Y->m_ourList.m_alloc   = NULL;
			Y->m_ourList.m_ownData = false;
		}
		// otherwise, we cannot copy it and i don't want to mdup it...
		else if ( ! list->m_ownData ) {
			log(LOG_LOGIC,"net: msg1: List must own data. Bad "
			    "engineer.");
			g_process.shutdownAbort(true); 
		}
		// lastly, if it was a clean steal, don't let list free it
		else list->m_ownData = false;
		// reset m_listPtr and m_listPtrHi so we pass the isExhausted()
		// check in sendSomeOfList() below
		Y->m_ourList.resetListPtr();
		// sanity test
		if ( Y->m_ourList.isExhausted() ) {
			log(LOG_LOGIC,"net: msg1: List is exhausted. "
			    "Bad engineer."); 
			g_process.shutdownAbort(true); }
		// now re-call
		bool inTransit;
		bool status = Y->addList ( &Y->m_ourList ,
					   rdbId         ,
					   collnum       ,
					   Y             , // state
					   returnMsg1    , // callback
					   forceLocal    ,
					   niceness      ,
					   injecting     ,
					   waitForReply  ,
					   &inTransit    ) ;
		// if we really blocked return false
		if ( ! status ) return false;
		// otherwise, it may have returned true because waitForReply
		// is false, but the request may still be in transit
		if ( inTransit ) return true;
		// debug msg
		//log("did not block, listSize=%" PRId32,m->m_ourList.m_listSize);
		// we did it without blocking, but it is still in transit
		// unless there was an error
		if ( g_errno ) log("net: Adding data to %s had error: %s.",
				   getDbnameFromId(rdbId),
				   mstrerror(g_errno));
		// otherwise, if not in transit and no g_errno then it must
		// have really completed without blocking. in which case
		// we are done with "Y"
		returnMsg1 ( (void *)Y );
		return true;
	}
 skip:
	// remember these vars
	m_list          = list;
	m_rdbId         = rdbId;
	m_collnum       = collnum;
	m_state         = state;
	m_callback      = callback;
	m_forceLocal    = forceLocal;
	m_niceness      = niceness;
	m_injecting     = injecting;
	m_waitForReply  = waitForReply;

	QUICKPOLL(niceness);
	// reset m_listPtr to point to first record again
	list->resetListPtr();
	// is the request in transit? assume not (assume did not block)
	if ( inTransit ) *inTransit = false;
	// . not all records in the list may belong to the same group
	// . records should be sorted by key so we don't need to sort them
	// . if this did not block, return true
	if ( sendSomeOfList ( ) ) return true;
	// it is in transit
	if ( inTransit ) *inTransit = true;
	// if we should waitForReply return false
	if ( m_waitForReply ) return false;
	// tell caller we did not block on the reply, even though we did
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . if the list is sorted by keys this will be the most efficient
bool Msg1::sendSomeOfList ( ) {
	// sanity check
	if ( m_list->m_ks !=  8 &&
	     m_list->m_ks != 12 &&
	     m_list->m_ks != 16 &&
	     m_list->m_ks != 24 ) { 
		g_process.shutdownAbort(true); }
	// debug msg
	//log("sendSomeOfList: mcast=%" PRIu32" exhausted=%" PRId32,
	//    (int32_t)&m_mcast,(int32_t)m_list->isExhausted());
 loop:
	// return true if list exhausted and nothing left to add
	if ( m_list->isExhausted() ) return true;
	// get key of the first record in the list
	//key_t firstKey = m_list->getCurrentKey();
	char firstKey[MAX_KEY_BYTES];
	m_list->getCurrentKey(firstKey);
 	QUICKPOLL(m_niceness);
	// get groupId from this key
	//uint32_t groupId ; 
	// . use the new Hostdb.h inlined function
	uint32_t shardNum = getShardNum ( m_rdbId , firstKey );

	// point to start of data we're going to send
	char *dataStart = m_list->getListPtr();
	// how many records belong to the same group as "firstKey"
	//key_t key;
	char key[MAX_KEY_BYTES];
	while ( ! m_list->isExhausted() ) {
		//key = m_list->getCurrentKey();
		m_list->getCurrentKey(key);
#ifdef GBSANITYCHECK
		// no half bits in here!
		// debug point
		if ( m_list->useHalfKeys() && 
		     m_list->isHalfBitOn ( m_list->getCurrentRec() ) )
			log(LOG_LOGIC,"net: msg1: Got half bit. Bad "
			    "engineer.");
#endif
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		//if ( getGroupId(m_rdbId,key) != groupId ) goto done;
		if ( getShardNum(m_rdbId,key) != shardNum ) goto done;

		// . break so we don't send more than MAX_DGRAMS defined in 
		//   UdpServer.cpp.
		// . let's boost it from 16k to 64k for speed
		if ( m_list->getListPtr() - dataStart > 64*1024 ) goto done;
		// . point to next record
		// . will point passed records if no more left!
 		QUICKPOLL(m_niceness);
		//int32_t crec = m_list->getCurrentRecSize();
		m_list->skipCurrentRecord();
		// sanity check
		if ( m_list->m_listPtr > m_list->m_listEnd ) {
			g_process.shutdownAbort(true); }
	}
 done:
	// now point to the end of the data
	char *dataEnd = m_list->getListPtr();
	// . if force local is true we force the data to be added locally
	// . this fixes the bug we had from spiderdb since a key got corrupted
	//   just enough to put it into a different groupId (but not out
	//   of order) so we couldn't delete it cuz our delete keys would go
	//   elsewhere
	if ( m_forceLocal && shardNum != getMyShardNum() &&
	     ! g_conf.m_interfaceMachine ) {
		// make the groupId local, our group
		//groupId = g_hostdb.m_groupId;
		// bitch about this to log it
		log("net: Data does not belong in shard %" PRIu32", but adding "
		    "to %s anyway. Probable data corruption.",
		    (uint32_t)shardNum,getDbnameFromId(m_rdbId));
	}
	
 	QUICKPOLL(m_niceness);

	// sanity test for new rdbs
	if ( m_list->m_fixedDataSize != getDataSizeFromRdbId(m_rdbId) ) {
		g_process.shutdownAbort(true); }

	// . now send this list to the host
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . if it blocked return false
	if ( ! sendData ( shardNum , dataStart , dataEnd - dataStart ) )
		return false;
	// if there was an error return true
	if ( g_errno ) return true;
	// otherwise, keep adding
	goto loop;
}

// . return false if blocked, true otherwise
// . sets g_errno on error
bool Msg1::sendData ( uint32_t shardNum, char *listData , int32_t listSize) {
	// debug msg
	//log("sendData: mcast=%" PRIu32" listSize=%" PRId32,
	//    (int32_t)&m_mcast,(int32_t)listSize);

	// bail if this is an interface machine, don't write to the main
	if ( g_conf.m_interfaceMachine ) return true;
	// return true if no data
	if ( listSize == 0 ) return true;
	// how many hosts in this group
	//int32_t numHosts = g_hostdb.getNumHostsPerShard();
	// . NOTE: for now i'm removing this until I handle ETRYAGAIN errors
	//         properly... by waiting and retrying...
	// . if this is local data just for us just do an addList to OUR rdb
	/*
	if ( groupId == g_hostdb.m_groupId  && numHosts == 1 ) {
		// this sets g_errno on error
		Msg0 msg0;
		Rdb *rdb = msg0.getRdb ( (char) m_rdbId );
		if ( ! rdb ) return true;
		// make a list from this data
		RdbList list;
		list.set (listData,listSize,listSize,rdb->getFixedDataSize(),
			  false) ; // ownData?
		// this returns false and sets g_errno on error
		rdb->addList ( &list );
		// . if we got a ETRYAGAIN cuz the buffer we add to was full
		//   then we should sleep and try again!
		// . return false cuz this blocks for a period of time
		//   before trying again
		if ( g_errno == ETRYAGAIN ) {
			// try adding again in 1 second
			registerSleepCallback ( 1000, slot, tryAgainWrapper1 );
			// return now
			return false;
		}
		// . always return true cuz we did not block
		// . g_errno may be set
		return true;
	}
	*/
	// if the data is being added to our group, don't send ourselves
	// a msg1, if we can add it right now
	// MDW: crap this is getting ETRYAGAIN and it isn't being tried again
	// i guess and Spider.cpp fails to add to doledb but the doleiptable
	// maintains a positive count, thereby hanging the spiders. let's
	// just always go through multicast so it will auto-retry ETRYAGAIN
	/*
	bool sendToSelf = true;
	if ( shardNum == getMyShardNum() &&
	     ! g_conf.m_interfaceMachine ) {
		// get the rdb to which it belongs, use Msg0::getRdb()
		Rdb *rdb = getRdbFromId ( (char) m_rdbId );
		if ( ! rdb ) goto skip;
		// key size
		int32_t ks = getKeySizeFromRdbId ( m_rdbId );
		// reset g_errno
		g_errno = 0;
		// . make a list from this data
		// . skip over the first 4 bytes which is the rdbId
		// . TODO: embed the rdbId in the msgtype or something...
		RdbList list;
		// set the list
		list.set ( listData ,
			   listSize ,
			   listData ,
			   listSize ,
			   rdb->getFixedDataSize() ,
			   false                   ,  // ownData?
			   rdb->useHalfKeys()      ,
			   ks                      ); 
		// note that
		//log("msg1: local addlist niceness=%" PRId32,m_niceness);
		// this returns false and sets g_errno on error
		rdb->addList ( m_coll , &list , m_niceness );
		// if titledb, add tfndb recs to map the title recs
		//if ( ! g_errno && rdb == g_titledb.getRdb() && m_injecting ) 
		//	// this returns false and sets g_errno on error
		//	updateTfndb ( m_coll , &list , true , m_niceness);
		// if no error, no need to use a Msg1 UdpSlot for ourselves
		if ( ! g_errno ) sendToSelf = false;
		else {
			log("rdb: msg1 coll=%s rdb=%s had error: %s",
			    m_coll,rdb->m_dbname,mstrerror(g_errno));
			// this is messing up generate catdb's huge rdblist add
			// why did we put it in there??? from msg9b.cpp
			//return true;
		}
		
 		QUICKPOLL(m_niceness);
		// if we're the only one in the group, bail, we're done
		if ( ! sendToSelf &&
		     g_hostdb.getNumHostsPerShard() == 1 ) return true;
	}
skip:
	*/
	// . make an add record request to multicast to a bunch of machines
	// . this will alloc new space, returns NULL on failure
	//char *request = makeRequest ( listData, listSize, groupId , 
	//m_rdbId , &requestLen );
	//int32_t collLen = strlen ( m_coll );
	// . returns NULL and sets g_errno on error
	// . calculate total size of the record
	// . 1 byte for rdbId, 1 byte for flags,
	//   then collection NULL terminated, then list
	int32_t requestLen = 1 + 1 + sizeof(collnum_t) + listSize ;
	// make the request
	char *request = (char *) mmalloc ( requestLen ,"Msg1" );
	if ( ! request ) return true;
	char *p = request;
	// store the rdbId at top of request
	*p++ = m_rdbId;
	// then the flags
	*p = 0;
	if ( m_injecting ) *p |= 0x80;
	p++;
	// then collection name
	//gbmemcpy ( p , m_coll , collLen );
	//p += collLen;
	//*p++ = '\0';
	*(collnum_t *)p = m_collnum;
	p += sizeof(collnum_t);
	// sanity check
	//if ( collLen <= 0 ) {
	//	log(LOG_LOGIC,"net: No collection specified for list add.");
	//	//g_process.shutdownAbort(true);
	//	g_errno = ENOCOLLREC;
	//	return true;
	//}
	//if ( m_deleteRecs    ) request[1] |= 0x80;
	//if ( m_overwriteRecs ) request[1] |= 0x40;
	// store the list after coll
	gbmemcpy ( p , listData , listSize );
 	QUICKPOLL(m_niceness);
	// for small packets
	//int32_t niceness = 2;
	//if ( requestLen < TMPBUFSIZE - 32 ) niceness = 0;
	//log("msg1: sending mcast niceness=%" PRId32,m_niceness);

	// . multicast to all hosts in group "groupId"
	// . multicast::send() returns false and sets g_errno on error
	// . we return false if we block, true otherwise
	// . will loop indefinitely if a host in this group is down
	// key is useless for us
	if (m_mcast.send(request, requestLen, msg_type_1, true, shardNum, true, 0, this, NULL, gotReplyWrapper1, multicast_msg1_senddata_timeout, m_niceness, -1, getDbnameFromId(m_rdbId))) {
		return false;
	}

 	QUICKPOLL(m_niceness);
	// g_errno should be set
	log("net: Had error when sending request to add data to %s in shard "
	    "#%" PRIu32": %s.", getDbnameFromId(m_rdbId),shardNum,mstrerror(g_errno));
	return true;	
}

// . this should only be called by m_mcast when it has successfully sent to
//   ALL hosts in group "groupId"
void gotReplyWrapper1 ( void *state , void *state2 ) {
	Msg1 *THIS = (Msg1 *)state;
	// print the error if any
	if ( g_errno && g_errno != ETRYAGAIN ) 
		log("net: Got bad reply when attempting to add data "
		    "to %s: %s",getDbnameFromId(THIS->m_rdbId),
		    mstrerror(g_errno));

	//int32_t address = (int32_t)THIS->m_callback;

	// if our list to send is exhausted then we're done!
	if ( THIS->m_list->isExhausted() ) {

		//if(g_conf.m_profilingEnabled){
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state ); 
		//if(g_conf.m_profilingEnabled){
		//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %" PRId32,
		//		    (int32_t)address);
		//}

		return; 
	}
	// otherwise we got more to send to groups
	if ( THIS->sendSomeOfList() ) {
		//if(g_conf.m_profilingEnabled){
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state ); 
		//if(g_conf.m_profilingEnabled){
		//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %" PRId32,
		//		    (int32_t)address);
		//}
		return; 
	}
}

static void addedList   ( UdpSlot *slot , Rdb *rdb );

// . destroys the slot if false is returned
// . this is registered in Msg1::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
void handleRequest1 ( UdpSlot *slot , int32_t netnice ) {


	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	int32_t niceness = slot->getNiceness();

	// select udp server based on niceness
	UdpServer *us = &g_udpServer;
	// must at least have an rdbId
	if ( readBufSize <= 4 ) {
		g_errno = EREQUESTTOOSHORT;
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. Request too short", __FILE__, __func__, __LINE__);
		us->sendErrorReply(slot, g_errno);
		return;
	}
	char *p    = readBuf;
	char *pend = readBuf + readBufSize;
	// extract rdbId
	char rdbId = *p++;
	// get the rdb to which it belongs, use Msg0::getRdb()
	Rdb *rdb = getRdbFromId ( (char) rdbId );
	if ( ! rdb ) { 
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. Bad rdbid", __FILE__, __func__, __LINE__);
		us->sendErrorReply(slot, EBADRDBID);
		return;
	}
	// keep track of stats
	rdb->readRequestAdd ( readBufSize );
	// reset g_errno
	g_errno = 0;
	// are we injecting some title recs?
	bool injecting;
	if ( *p & 0x80 ) injecting = true;
	else             injecting = false;
	p++;
	// then collection
	//char *coll = p;
	//p += strlen (p) + 1;
	collnum_t collnum = *(collnum_t *)p;
	p += sizeof(collnum_t);
	// . make a list from this data
	// . skip over the first 4 bytes which is the rdbId
	// . TODO: embed the rdbId in the msgtype or something...
	RdbList list;
	// set the list
	list.set ( p        , // readBuf     + 4         ,
		   pend - p , // readBufSize - 4         ,
		   p        , // readBuf     + 4         ,
		   pend - p , // readBufSize - 4         ,
		   rdb->getFixedDataSize() ,
		   false                   ,  // ownData?
		   rdb->useHalfKeys()      ,
		   rdb->getKeySize ()      ); 
	// note it
	//log("msg1: handlerequest1 calling addlist niceness=%" PRId32,niceness);
	//log("msg1: handleRequest1 niceness=%" PRId32,niceness);
	// this returns false and sets g_errno on error
	rdb->addList ( collnum , &list , niceness);
	// if titledb, add tfndb recs to map the title recs
	//if ( ! g_errno && rdb == g_titledb.getRdb() && injecting ) 
	//	updateTfndb ( coll , &list , true, 0);
	// but if deleting a "new" and unforced record from spiderdb
	// then only delete tfndb record if it was tfn=255
	//if ( ! g_errno && rdb == g_spiderdb.getRdb() )
	//	updateTfndb2 ( coll , &list , false );
	// retry on some errors
	addedList ( slot , rdb );
}

// g_errno may be set when this is called
void addedList ( UdpSlot *slot , Rdb *rdb ) {
	// no memory means to try again
	if ( g_errno == ENOMEM ) {
		g_errno = ETRYAGAIN;
	}

	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC && g_repairMode > 0 ) {
		g_errno = ETRYAGAIN;
	}

	// it seems like someone can delete a collection and there can
	// be adds in transit to doledb and it logs
	// "doledb bad collnum of 30110"
	// so just absorb those
	if ( g_errno == ENOCOLLREC ) {
		log(LOG_WARN, "msg1: missing collrec to add to to %s. just dropping.", rdb->m_dbname);
		g_errno = 0;
	}

	// chalk it up
	rdb->sentReplyAdd(0);

	// are we done
	if (!g_errno) {
		// . send an empty (non-error) reply as verification
		// . slot should be auto-nuked on transmission/timeout of reply
		// . udpServer should free the readBuf
		g_udpServer.sendReply_ass(NULL, 0, NULL, 0, slot);
		return;
	}

	// on other errors just send the err code back
	log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror(g_errno));
	g_udpServer.sendErrorReply(slot, g_errno);
}
