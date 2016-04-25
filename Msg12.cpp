
#include "gb-include.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Msg12.h"
#include "Doledb.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "Threads.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"




void gotLockReplyWrapper ( void *state , UdpSlot *slot ) {
	// cast it
	Msg12 *msg12 = (Msg12 *)state;
	// . call handler
	// . returns false if waiting for more replies to come in
	if ( ! msg12->gotLockReply ( slot ) ) return;
	// if had callback, maybe from PageReindex.cpp
	if ( msg12->m_callback ) msg12->m_callback ( msg12->m_state );
	// ok, try to get another url to spider
	else                     g_spiderLoop.spiderDoledUrls();
}

Msg12::Msg12 () {
	m_numRequests = 0;
	m_numReplies  = 0;
}

// . returns false if blocked, true otherwise.
// . returns true and sets g_errno on error
// . before we can spider for a SpiderRequest we must be granted the lock
// . each group shares the same doledb and each host in the group competes
//   for spidering all those urls. 
// . that way if a host goes down is load is taken over
bool Msg12::getLocks ( int64_t uh48, // probDocId , 
		       char *url ,
		       DOLEDBKEY *doledbKey,
		       collnum_t collnum,
		       int32_t sameIpWaitTime,
		       int32_t maxSpidersOutPerIp,
		       int32_t firstIp,
		       void *state ,
		       void (* callback)(void *state) ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// do not use locks for injections
	//if ( m_sreq->m_isInjecting ) return true;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset
	m_numRequests = 0;
	m_numReplies  = 0;
	m_grants   = 0;
	m_removing = false;
	m_confirming = false;
	// make sure is really docid
	//if ( probDocId & ~DOCID_MASK ) { char *xx=NULL;*xx=0; }
	// . mask out the lower bits that may change if there is a collision
	// . in this way a url has the same m_probDocId as the same url
	//   in the index. i.e. if we add a new spider request for url X and
	//   url X is already indexed, then they will share the same lock 
	//   even though the indexed url X may have a different actual docid
	//   than its probable docid.
	// . we now use probable docids instead of uh48 because query reindex
	//   in PageReindex.cpp adds docid based spider requests and we
	//   only know the docid, not the uh48 because it is creating
	//   SpiderRequests from docid-only search results. having to look
	//   up the msg20 summary for like 1M search results is too painful!
	//m_lockKey = g_titledb.getFirstProbableDocId(probDocId);
	// . use this for locking now, and let the docid-only requests just use
	//   the docid
	m_lockKeyUh48 = makeLockTableKey ( uh48 , firstIp );
	m_url = url;
	m_callback = callback;
	m_state = state;
	m_hasLock = false;
	m_origUh48 = uh48;
	// support ability to spider multiple urls from same ip
	m_doledbKey = *doledbKey;
	m_collnum = collnum;
	m_sameIpWaitTime = sameIpWaitTime;
	m_maxSpidersOutPerIp = maxSpidersOutPerIp;
	m_firstIp = firstIp;

	// sanity check, just 6 bytes! (48 bits)
	if ( uh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	if ( m_lockKeyUh48 & 0xffff000000000000LL ) { char *xx=NULL;*xx=0; }

	// cache time
	int32_t ct = 120;

	// if docid based assume it was a query reindex and keep it short!
	// otherwise we end up waiting 120 seconds for a query reindex to
	// go through on a docid we just spidered. TODO: use m_urlIsDocId
	// MDW: check this out
	if ( url && is_digit(url[0]) ) ct = 2;

	// . this seems to be messing us up and preventing us from adding new
	//   requests into doledb when only spidering a few IPs.
	// . make it random in the case of twin contention
	ct = rand() % 10;

	// . check our cache to avoid repetitive asking
	// . use -1 for maxAge to indicate no max age
	// . returns -1 if not in cache
	// . use maxage of two minutes, 120 seconds
	int32_t lockTime ;
	lockTime = g_spiderLoop.m_lockCache.getLong(0,m_lockKeyUh48,ct,true);
	// if it was in the cache and less than 2 minutes old then return
	// true now with m_hasLock set to false.
	if ( lockTime >= 0 ) {
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: cached missed lock for %s "
			     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
		return true;
	}

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending lock request for %s "
		     "lockkey=%"UINT64"",  m_url,m_lockKeyUh48);

	// now the locking group is based on the probable docid
	//m_lockGroupId = g_hostdb.getGroupIdFromDocId(m_lockKey);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();

	// shortcut
	UdpServer *us = &g_udpServer;


	static int32_t s_lockSequence = 0;
	// remember the lock sequence # in case we have to call remove locks
	m_lockSequence = s_lockSequence++;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 0;
	lr->m_lockSequence = m_lockSequence;
	lr->m_collnum = collnum;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// point to start of the 12 byte request buffer
	char *request = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead (h) ) continue;
		// note it
		if ( g_conf.m_logDebugSpider )
			logf(LOG_DEBUG,"spider: sent lock "
			     "request #%"INT32" for lockkey=%"UINT64" %s to "
			     "hid=%"INT32"",m_numRequests,m_lockKeyUh48,
			     m_url,h->m_hostId);
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 udpserver_sendrequest_infinite_timeout ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// i guess nothing... hmmm... all dead?
	//char *xx=NULL; *xx=0; 
	// m_hasLock should be false... all lock hosts seem dead... wait
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: all lock hosts seem dead for %s "
		     "lockkey=%"UINT64"", m_url,m_lockKeyUh48);
	return true;
}


// returns true if all done, false if waiting for more replies
bool Msg12::gotLockReply ( UdpSlot *slot ) {

	// no longer use this
	char *xx=NULL;*xx=0;

	// got reply
	m_numReplies++;
	// don't let udpserver free the request, it's our m_request[]
	slot->m_sendBufAlloc = NULL;
	// check for a hammer reply
	char *reply     = slot->m_readBuf;
	int32_t  replySize = slot->m_readBufSize;
	// if error, treat as a not grant
	if ( g_errno ) {
		bool logIt = true;
		// note it
		if ( g_conf.m_logDebugSpider )
			log("spider: got msg12 reply error = %s",
			    mstrerror(g_errno));
		// if we got an ETRYAGAIN when trying to confirm our lock
		// that means doledb was saving/dumping to disk and we 
		// could not remove the record from doledb and add an
		// entry to the waiting tree, so we need to keep trying
		if ( g_errno == ETRYAGAIN && m_confirming ) {
			// c ount it again
			m_numRequests++;
			// use what we were using
			char *request     = (char *)&m_confirmRequest;
			int32_t  requestSize = sizeof(ConfirmRequest);
			Host *h = g_hostdb.getHost(slot->m_hostId);
			// send request to him
			UdpServer *us = &g_udpServer;
			if ( ! us->sendRequest ( request      ,
						 requestSize  ,
						 0x12         , // msgType
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL         , // retSlotPtrPt
						 this         , // state data
						 gotLockReplyWrapper ,
						 udpserver_sendrequest_infinite_timeout ) ) 
				return false;
			// error?
			// don't spam the log!
			static int32_t s_last = 0;
			int32_t now = getTimeLocal();
			if ( now - s_last >= 1 ) {
				s_last = now;
				log("spider: error re-sending confirm "
				    "request: %s",  mstrerror(g_errno));
			}
		}
		// only log every 10 seconds for ETRYAGAIN
		if ( g_errno == ETRYAGAIN ) {
			static time_t s_lastTime = 0;
			time_t now = getTimeLocal();
			logIt = false;
			if ( now - s_lastTime >= 3 ) {
				logIt = true;
				s_lastTime = now;
			}
		}
		if ( logIt )
			log ( "sploop: host had error getting lock url=%s"
			      ": %s" ,
			      m_url,mstrerror(g_errno) );
	}
	// grant or not
	if ( replySize == 1 && ! g_errno && *reply == 1 ) m_grants++;
	// wait for all to get back
	if ( m_numReplies < m_numRequests ) return false;
	// all done if we were removing
	if ( m_removing ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done removing all locks "
			   "(replies=%"INT32") for %s",
			   m_numReplies,m_url);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		return true;
	}
	// all done if we were confirming
	if ( m_confirming ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: done confirming all locks "
			   "for %s uh48=%"INT64"",m_url,m_origUh48);//m_sreq->m_url);
		// we are done
		m_gettingLocks = false;
		// . keep processing
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) return g_spiderLoop.spiderUrl2();
		// if we had a callback let our parent call it
		return true;
	}

	// if got ALL locks, spider it
	if ( m_grants == m_numReplies ) {
		// note it
		if ( g_conf.m_logDebugSpider )
		      logf(LOG_DEBUG,"spider: got lock for docid=lockkey=%"UINT64"",
			   m_lockKeyUh48);
		// flag this
		m_hasLock = true;
		// we are done
		//m_gettingLocks = false;


		///////
		//
		// now tell our group (shard) to remove from doledb
		// and re-add to waiting tree. the evalIpLoop() function
		// should skip this probable docid because it is in the 
		// LOCK TABLE!
		//
		// This logic should allow us to spider multiple urls
		// from the same IP at the same time.
		//
		///////

		// returns false if would block
		if ( ! confirmLockAcquisition ( ) ) return false;
		// . we did it without blocking, maybe cuz we are a single node
		// . ok, they are all back, resume loop
		// . if the collection was nuked from under us the spiderUrl2
		//   will return true and set g_errno
		if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;

	}
	// note it
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: missed lock for %s lockkey=%"UINT64" "
		     "(grants=%"INT32")",   m_url,m_lockKeyUh48,m_grants);

	// . if it was locked by another then add to our lock cache so we do
	//   not try to lock it again
	// . if grants is not 0 then one host granted us the lock, but not
	//   all hosts, so we should probably keep trying on it until it is
	//   locked up by one host
	if ( m_grants == 0 ) {
		int32_t now = getTimeGlobal();
		g_spiderLoop.m_lockCache.addLong(0,m_lockKeyUh48,now,NULL);
	}

	// reset again
	m_numRequests = 0;
	m_numReplies  = 0;
	// no need to remove them if none were granted because another
	// host in our group might have it 100% locked. 
	if ( m_grants == 0 ) {
		// no longer in locks operation mode
		m_gettingLocks = false;
		// ok, they are all back, resume loop
		//if ( ! m_callback ) g_spiderLoop.spiderUrl2 ( );
		// all done
		return true;
	}
	// note that
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: sending request to all in shard to "
		     "remove lock uh48=%"UINT64". grants=%"INT32"",
		     m_lockKeyUh48,(int32_t)m_grants);
	// remove all locks we tried to get, BUT only if from our hostid!
	// no no! that doesn't quite work right... we might be the ones
	// locking it! i.e. another one of our spiders has it locked...
	if ( ! removeAllLocks ( ) ) return false; // true;
	// if did not block, how'd that happen?
	log("sploop: did not block in removeAllLocks: %s",mstrerror(g_errno));
	return true;
}

bool Msg12::removeAllLocks ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// skip if injecting
	//if ( m_sreq->m_isInjecting ) return true;
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"spider: removing all locks for %s %"UINT64"",
		     m_url,m_lockKeyUh48);
	// we are now removing 
	m_removing = true;

	LockRequest *lr = &m_lockRequest;
	lr->m_lockKeyUh48 = m_lockKeyUh48;
	lr->m_lockSequence = m_lockSequence;
	lr->m_firstIp = m_firstIp;
	lr->m_removeLock = 1;

	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	char *request     = (char *)lr;//m_lockKey;
	int32_t  requestSize = sizeof(LockRequest);//12;

	// now the locking group is based on the probable docid
	//uint32_t groupId = g_hostdb.getGroupIdFromDocId(m_lockKeyUh48);
	// ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( groupId );
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same group that is going to spider it!
	// i.e. our group! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyGroup();
	// shortcut
	UdpServer *us = &g_udpServer;
	// set the hi bit though for this one
	//m_lockKey |= 0x8000000000000000LL;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 udpserver_sendrequest_infinite_timeout ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}

bool Msg12::confirmLockAcquisition ( ) {

	// ensure not in use. not msg12 replies outstanding.
	if ( m_numRequests != m_numReplies ) { char *xx=NULL;*xx=0; }

	// no longer use this
	char *xx=NULL;*xx=0;

	// we are now removing 
	m_confirming = true;

	// make that the request
	// . point to start of the 12 byte request buffer
	// . m_lockSequence should still be valid
	ConfirmRequest *cq = &m_confirmRequest;
	char *request     = (char *)cq;
	int32_t  requestSize = sizeof(ConfirmRequest);
	// sanity
	if ( requestSize == sizeof(LockRequest)){ char *xx=NULL;*xx=0; }
	// set it
	cq->m_collnum   = m_collnum;
	cq->m_doledbKey = m_doledbKey;
	cq->m_firstIp   = m_firstIp;
	cq->m_lockKeyUh48 = m_lockKeyUh48;
	cq->m_maxSpidersOutPerIp = m_maxSpidersOutPerIp;
	// . use the locking group from when we sent the lock request
	// . get ptr to list of hosts in the group
	//Host *hosts = g_hostdb.getGroup ( m_lockGroupId );
	// the same group (shard) that has the spiderRequest/Reply is
	// the one responsible for locking.
	Host *hosts = g_hostdb.getMyShard();
	// this must select the same shard that is going to spider it!
	// i.e. our shard! because we check our local lock table to see
	// if a doled url is locked before spidering it ourselves.
	//Host *hosts = g_hostdb.getMyShard();
	// shortcut
	UdpServer *us = &g_udpServer;
	// get # of hosts in each mirror group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// reset counts
	m_numRequests = 0;
	m_numReplies  = 0;
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: confirming lock for uh48=%"UINT64" firstip=%s",
		    m_lockKeyUh48,iptoa(m_firstIp));
	// loop over hosts in that shard
	for ( int32_t i = 0 ; i < hpg ; i++ ) {
		// get a host
		Host *h = &hosts[i];
		// skip if dead! no need to get a reply from dead guys
		if ( g_hostdb.isDead ( h ) ) continue;
		// send request to him
		if ( ! us->sendRequest ( request      ,
					 // a size of 2 should mean confirm
					 requestSize  ,
					 0x12         , // msgType
					 h->m_ip      ,
					 h->m_port    ,
					 h->m_hostId  ,
					 NULL         , // retSlotPtrPtr
					 this         , // state data
					 gotLockReplyWrapper ,
					 udpserver_sendrequest_infinite_timeout ) ) 
			// udpserver returns false and sets g_errno on error
			return true;
		// count them
		m_numRequests++;
	}
	// block?
	if ( m_numRequests > 0 ) return false;
	// did not block
	return true;
}


void handleRequest12 ( UdpSlot *udpSlot , int32_t niceness ) {
	// get request
	char *request = udpSlot->m_readBuf;
	int32_t  reqSize = udpSlot->m_readBufSize;
	// shortcut
	UdpServer *us = &g_udpServer;
	// breathe
	QUICKPOLL ( niceness );

	// shortcut
	char *reply = udpSlot->m_tmpBuf;

	//
	// . is it confirming that he got all the locks?
	// . if so, remove the doledb record and dock the doleiptable count
	//   before adding a waiting tree entry to re-pop the doledb record
	//
	if ( reqSize == sizeof(ConfirmRequest) ) {
		char *msg = NULL;
		ConfirmRequest *cq = (ConfirmRequest *)request;

		// confirm the lock
		HashTableX *ht = &g_spiderLoop.m_lockTable;
		int32_t slot = ht->getSlot ( &cq->m_lockKeyUh48 );
		if ( slot < 0 ) { 
			log("spider: got a confirm request for a key not "
			    "in the table! coll must have been deleted "
			    " or reset "
			    "while lock request was outstanding.");
			g_errno = EBADENGINEER;
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			us->sendErrorReply ( udpSlot , g_errno );
			return;
			//char *xx=NULL;*xx=0; }
		}
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot ( slot );
		lock->m_confirmed = true;

		// note that
		if ( g_conf.m_logDebugSpider ) // Wait )
			log("spider: got confirm lock request for ip=%s",
			    iptoa(lock->m_firstIp));

		// get it
		SpiderColl *sc = g_spiderCache.getSpiderColl(cq->m_collnum);
		// make it negative
		cq->m_doledbKey.n0 &= 0xfffffffffffffffeLL;
		// and add the negative rec to doledb (deletion operation)
		Rdb *rdb = &g_doledb.m_rdb;
		if ( ! rdb->addRecord ( cq->m_collnum,
					(char *)&cq->m_doledbKey,
					NULL , // data
					0    , //dataSize
					1 )){ // niceness
			// tree is dumping or something, probably ETRYAGAIN
			if ( g_errno != ETRYAGAIN ) {msg = "error adding neg rec to doledb";	log("spider: %s %s",msg,mstrerror(g_errno));
			}
			//char *xx=NULL;*xx=0;
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// now remove from doleiptable since we removed from doledb
		if ( sc ) sc->removeFromDoledbTable ( cq->m_firstIp );

		// how many spiders outstanding for this coll and IP?
		//int32_t out=g_spiderLoop.getNumSpidersOutPerIp ( cq->m_firstIp);

		// DO NOT add back to waiting tree if max spiders
		// out per ip was 1 OR there was a crawldelay. but better
		// yet, take care of that in the winReq code above.

		// . now add to waiting tree so we add another spiderdb
		//   record for this firstip to doledb
		// . true = callForScan
		// . do not add to waiting tree if we have enough outstanding
		//   spiders for this ip. we will add to waiting tree when
		//   we receive a SpiderReply in addSpiderReply()
		if ( sc && //out < cq->m_maxSpidersOutPerIp &&
		     // this will just return true if we are not the 
		     // responsible host for this firstip
		    // DO NOT populate from this!!! say "false" here...
		     ! sc->addToWaitingTree ( 0 , cq->m_firstIp, false ) &&
		     // must be an error...
		     g_errno ) {
			msg = "FAILED TO ADD TO WAITING TREE";
			log("spider: %s %s",msg,mstrerror(g_errno));
			
			log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
			us->sendErrorReply ( udpSlot , g_errno );
			return;
		}
		// success!!
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}



	// sanity check
	if ( reqSize != sizeof(LockRequest) ) {
		log("spider: bad msg12 request size of %"INT32"",reqSize);
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply ( udpSlot , EBADREQUEST );
		return;
	}
	// deny it if we are not synced yet! otherwise we core in 
	// getTimeGlobal() below
	if ( ! isClockInSync() ) { 
		// log it so we can debug it
		//log("spider: clock not in sync with host #0. so "
		//    "returning etryagain for lock reply");
		// let admin know why we are not spidering
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply ( udpSlot , ETRYAGAIN );
		return;
	}

	LockRequest *lr = (LockRequest *)request;
	//uint64_t lockKey = *(int64_t *)request;
	//int32_t lockSequence = *(int32_t *)(request+8);
	// is this a remove operation? assume not
	//bool remove = false;
	// get top bit
	//if ( lockKey & 0x8000000000000000LL ) remove = true;

	// mask it out
	//lockKey &= 0x7fffffffffffffffLL;
	// sanity check, just 6 bytes! (48 bits)
	if ( lr->m_lockKeyUh48 &0xffff000000000000LL ) { char *xx=NULL;*xx=0; }
	// note it
	if ( g_conf.m_logDebugSpider )
		log("spider: got msg12 request uh48=%"INT64" remove=%"INT32"",
		    lr->m_lockKeyUh48, (int32_t)lr->m_removeLock);
	// get time
	int32_t nowGlobal = getTimeGlobal();
	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

	int32_t hostId = g_hostdb.getHostId ( udpSlot->m_ip , udpSlot->m_port );
	// this must be legit - sanity check
	if ( hostId < 0 ) { char *xx=NULL;*xx=0; }

	// remove expired locks from locktable
	removeExpiredLocks ( hostId );

	int64_t lockKey = lr->m_lockKeyUh48;

	// check tree
	int32_t slot = ht->getSlot ( &lockKey ); // lr->m_lockKeyUh48 );
	// put it here
	UrlLock *lock = NULL;
	// if there say no no
	if ( slot >= 0 ) lock = (UrlLock *)ht->getValueFromSlot ( slot );

	// if doing a remove operation and that was our hostid then unlock it
	if ( lr->m_removeLock && 
	     lock && 
	     lock->m_hostId == hostId &&
	     lock->m_lockSequence == lr->m_lockSequence ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// ok, at this point all remove ops return
	if ( lr->m_removeLock ) {
		reply[0] = 1;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}

	/////////
	//
	// add new lock
	//
	/////////


	// if lock > 1 hour old then remove it automatically!!
	if ( lock && nowGlobal - lock->m_timestamp > MAX_LOCK_AGE ) {
		// note it for now
		log("spider: removing lock after %"INT32" seconds "
		    "for lockKey=%"UINT64" hid=%"INT32"",
		    (nowGlobal - lock->m_timestamp),
		    lr->m_lockKeyUh48,hostId);
		// unlock it
		ht->removeSlot ( slot );
		// it is gone
		lock = NULL;
	}
	// if lock still there, do not grant another lock
	if ( lock ) {
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: refusing lock for lockkey=%"UINT64" hid=%"INT32"",
			    lr->m_lockKeyUh48,hostId);
		reply[0] = 0;
		us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
		return;
	}
	// make the new lock
	UrlLock tmp;
	tmp.m_hostId       = hostId;
	tmp.m_lockSequence = lr->m_lockSequence;
	tmp.m_timestamp    = nowGlobal;
	tmp.m_expires      = 0;
	tmp.m_firstIp      = lr->m_firstIp;
	tmp.m_collnum      = lr->m_collnum;

	// when the spider returns we remove its lock on reception of the
	// spiderReply, however, we actually just set the m_expires time
	// to 5 seconds into the future in case there is a current request
	// to get a lock for that url in progress. but, we do need to
	// indicate that the spider has indeed completed by setting
	// m_spiderOutstanding to true. this way, addToWaitingTree() will
	// not count it towards a "max spiders per IP" quota when deciding
	// on if it should add a new entry for this IP.
	tmp.m_spiderOutstanding = true;
	// this is set when all hosts in the group (shard) have granted the
	// lock and the host sends out a confirmLockAcquisition() request.
	// until then we do not know if the lock will be granted by all hosts
	// in the group (shard)
	tmp.m_confirmed    = false;

	// put it into the table
	if ( ! ht->addKey ( &lockKey , &tmp ) ) {
		// return error if that failed!
		
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		us->sendErrorReply ( udpSlot , g_errno );
		return;
	}
	// note it for now
	if ( g_conf.m_logDebugSpider )
		log("spider: granting lock for lockKey=%"UINT64" hid=%"INT32"",
		    lr->m_lockKeyUh48,hostId);
	// grant the lock
	reply[0] = 1;
	us->sendReply_ass ( reply , 1 , reply , 1 , udpSlot );
	return;
}

// hostId is the remote hostid sending us the lock request
void removeExpiredLocks ( int32_t hostId ) {
	// when we last cleaned them out
	static time_t s_lastTime = 0;

	int32_t nowGlobal = getTimeGlobalNoCore();

	// only do this once per second at the most
	if ( nowGlobal <= s_lastTime ) return;

	// shortcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;

 restart:

	// scan the slots
	int32_t ns = ht->m_numSlots;
	// . clean out expired locks...
	// . if lock was there and m_expired is up, then nuke it!
	// . when Rdb.cpp receives the "fake" title rec it removes the
	//   lock, only it just sets the m_expired to a few seconds in the
	//   future to give the negative doledb key time to be absorbed.
	//   that way we don't repeat the same url we just got done spidering.
	// . this happens when we launch our lock request on a url that we
	//   or a twin is spidering or has just finished spidering, and
	//   we get the lock, but we avoided the negative doledb key.
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// cast lock
		UrlLock *lock = (UrlLock *)ht->getValueFromSlot(i);
		int64_t lockKey = *(int64_t *)ht->getKeyFromSlot(i);
		// if collnum got deleted or reset
		collnum_t collnum = lock->m_collnum;
		if ( collnum >= g_collectiondb.m_numRecs ||
		     ! g_collectiondb.m_recs[collnum] ) {
			log("spider: removing lock from missing collnum "
			    "%"INT32"",(int32_t)collnum);
			goto nuke;
		}
		// skip if not yet expired
		if ( lock->m_expires == 0 ) continue;
		if ( lock->m_expires >= nowGlobal ) continue;
		// note it for now
		if ( g_conf.m_logDebugSpider )
			log("spider: removing lock after waiting. elapsed=%"INT32"."
			    " lockKey=%"UINT64" hid=%"INT32" expires=%"UINT32" "
			    "nowGlobal=%"UINT32"",
			    (nowGlobal - lock->m_timestamp),
			    lockKey,hostId,
			    (uint32_t)lock->m_expires,
			    (uint32_t)nowGlobal);
	nuke:
		// nuke the slot and possibly re-chain
		ht->removeSlot ( i );
		// gotta restart from the top since table may have shrunk
		goto restart;
	}
	// store it
	s_lastTime = nowGlobal;
}		


