// Matt Wells, copyright Nov 2002

#ifndef GB_SPIDERLOOP_H
#define GB_SPIDERLOOP_H

#include "RdbList.h"
#include "HashTableX.h"
#include "Msg5.h"
#include "hash.h"
#include "RdbCache.h"
#include <time.h>

// . the spider loop
// . it gets urls to spider from the SpiderCache global class, g_spiderCache
// . supports robots.txt
// . supports <META NAME="ROBOTS" CONTENT="NOINDEX">  (no indexing)
// . supports <META NAME="ROBOTS" CONTENT="NOFOLLOW"> (no links)
// . supports limiting spiders per domain

// . max spiders we can have going at once for this process
// . limit to 50 to preven OOM conditions
#define MAX_SPIDERS 300


class UdpSlot;
class SpiderRequest;
class SpiderColl;
class CollectionRec;
class XmlDoc;
class UrlLock;

class SpiderLoop {
public:
	~SpiderLoop();
	SpiderLoop();

	// free all XmlDocs and m_list
	void reset();

	// . call this no matter what
	// . if spidering is disabled this will sleep about 10 seconds or so
	//   before checking to see if it's been enabled
	void init();

	int32_t getNumSpidersOutPerIp ( int32_t firstIp , collnum_t collnum ) ;
	int32_t getNumSpidersOut() const { return m_numSpidersOut; }

	bool isLocked(int64_t key) const;
	int32_t getLockCount() const;
	bool addLock(int64_t key, const UrlLock *lock);
	void removeLock(int64_t key);
	void clearLocks(collnum_t collnum);

	// for spidering/parsing/indexing a url(s)
	XmlDoc *m_docs [ MAX_SPIDERS ];

	RdbCache   m_winnerListCache;

	void invalidateActiveList() { m_activeListValid = false; }

private:
	static void indexedDocWrapper ( void *state ) ;
	static void doneSleepingWrapperSL ( int fd , void *state ) ;
	static void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;

	void spiderDoledUrls ( ) ;
	bool gotDoledbList2  ( ) ;

	// . returns false if blocked and "callback" will be called,
	//   true otherwise
	// . returns true and sets g_errno on error
	bool spiderUrl(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum);
	bool spiderUrl2(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum);

	bool indexedDoc ( XmlDoc *doc );

	CollectionRec *getActiveList();
	void buildActiveList ( ) ;

	int32_t m_numSpidersOut;

	// . this is "i" where m_msg14[i] is the highest m_msg14 in use
	// . we use it to limit our scanning to the first "i" m_msg14's
	int32_t m_maxUsed;

	int32_t m_launches;

	HashTableX m_lockTable;
	mutable GbMutex m_lockTableMtx;

	// . list for getting next url(s) to spider
	RdbList m_list;

	// for getting RdbLists
	Msg5 m_msg5;

	SpiderColl *m_sc;

	bool m_gettingDoledbList;

	CollectionRec *m_crx;
	CollectionRec *m_activeList;
	CollectionRec *m_bookmark;

	bool m_activeListValid;
	int32_t m_activeListCount;

	uint32_t m_recalcTime;
	bool m_recalcTimeValid;

	int64_t m_doleStart;
};

extern SpiderLoop g_spiderLoop;

void handleRequestc1 ( UdpSlot *slot , int32_t niceness );

#endif // GB_SPIDERLOOP_H
