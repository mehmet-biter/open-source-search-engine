// Matt Wells, copyright Nov 2002

#ifndef GB_MSG12_H
#define GB_MSG12_H


#include "Rdb.h"
#include "Conf.h"
#include "Titledb.h"
#include "Hostdb.h"
#include "RdbList.h"
#include "RdbTree.h"
#include "HashTableX.h"
#include <time.h>


// key96_t:
#define DOLEDBKEY key_t


class LockRequest {
public:
	int64_t m_lockKeyUh48;
	int32_t m_lockSequence;
	int32_t m_firstIp;
	char m_removeLock;
	collnum_t m_collnum;
};

class ConfirmRequest {
public:
	int64_t m_lockKeyUh48;
	collnum_t m_collnum;
	key_t m_doledbKey;
	int32_t  m_firstIp;
	int32_t m_maxSpidersOutPerIp;
};


class Msg12 {
 public:

	Msg12();

	bool confirmLockAcquisition ( ) ;

	//uint32_t m_lockGroupId;

	LockRequest m_lockRequest;

	ConfirmRequest m_confirmRequest;

	// stuff for getting the msg12 lock for spidering a url
	bool getLocks       ( int64_t probDocId,
			      char *url ,
			      DOLEDBKEY *doledbKey,
			      collnum_t collnum,
			      int32_t sameIpWaitTime, // in milliseconds
			      int32_t maxSpidersOutPerIp,
			      int32_t firstIp,
			      void *state,
			      void (* callback)(void *state) );
	bool gotLockReply   ( class UdpSlot *slot );
	bool removeAllLocks ( );

	// these two things comprise the lock request buffer
	//uint64_t  m_lockKey;
	// this is the new lock key. just use docid for docid-only spider reqs.
	uint64_t  m_lockKeyUh48;
	int32_t                m_lockSequence;

	int64_t  m_origUh48;
	int32_t       m_numReplies;
	int32_t       m_numRequests;
	int32_t       m_grants;
	bool       m_removing;
	bool       m_confirming;
	char      *m_url; // for debugging
	void      *m_state;
	void      (*m_callback)(void *state);
	bool       m_gettingLocks;
	bool       m_hasLock;

	collnum_t  m_collnum;
	DOLEDBKEY  m_doledbKey;
	int32_t       m_sameIpWaitTime;
	int32_t       m_maxSpidersOutPerIp;
	int32_t       m_firstIp;
	Msg4       m_msg4;
};

void handleRequest12 ( UdpSlot *udpSlot , int32_t niceness ) ;

void removeExpiredLocks ( int32_t hostId );


#endif // GB_MSG12_H
