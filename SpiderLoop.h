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

	// for spidering/parsing/indexing a url(s)
	class XmlDoc *m_docs [ MAX_SPIDERS ];

	HashTableX m_lockTable;

	RdbCache   m_winnerListCache;

	bool m_activeListValid;
	bool m_activeListModified;


private:
	static void indexedDocWrapper ( void *state ) ;
	static void doneSleepingWrapperSL ( int fd , void *state ) ;
	static void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;

	void spiderDoledUrls ( ) ;
	bool gotDoledbList2  ( ) ;

	// . returns false if blocked and "callback" will be called,
	//   true otherwise
	// . returns true and sets g_errno on error
	bool spiderUrl9(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum);

	bool spiderUrl2 (collnum_t collnum);

	bool indexedDoc ( class XmlDoc *doc );

	CollectionRec *getActiveList();
	void buildActiveList ( ) ;

	int32_t m_numSpidersOut;

	// . this is "i" where m_msg14[i] is the highest m_msg14 in use
	// . we use it to limit our scanning to the first "i" m_msg14's
	int32_t m_maxUsed;

	// state memory for calling SpiderUrl2() (maybe also getLocks()!)
	SpiderRequest *m_sreq;

	key96_t     *m_doledbKey;

	int32_t m_launches;

	// . list for getting next url(s) to spider
	RdbList m_list;

	// for getting RdbLists
	Msg5 m_msg5;

	SpiderColl *m_sc;

	bool m_gettingDoledbList;

	CollectionRec *m_crx;
	CollectionRec *m_activeList;
	CollectionRec *m_bookmark;

	int32_t m_activeListCount;
	uint32_t m_recalcTime;
	bool m_recalcTimeValid;

	int64_t m_lastCallTime;

	int64_t m_doleStart;

	int32_t m_processed;
};

extern class SpiderLoop g_spiderLoop;

void handleRequestc1 ( UdpSlot *slot , int32_t niceness );

#endif // GB_SPIDERLOOP_H
