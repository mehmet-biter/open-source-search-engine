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

	bool printLockTable ( );

	int32_t getNumSpidersOutPerIp ( int32_t firstIp , collnum_t collnum ) ;

	// free all XmlDocs and m_list
	void reset();

	// . call this no matter what
	// . if spidering is disabled this will sleep about 10 seconds or so
	//   before checking to see if it's been enabled
	void startLoop();

	void spiderDoledUrls ( ) ;
	bool gotDoledbList2  ( ) ;

	// . returns false if blocked and "callback" will be called, 
	//   true otherwise
	// . returns true and sets g_errno on error
	bool spiderUrl9(SpiderRequest *sreq, key96_t *doledbKey, collnum_t collnum);

	bool spiderUrl2 ( );

	// state memory for calling SpiderUrl2() (maybe also getLocks()!)
	SpiderRequest *m_sreq;

	collnum_t  m_collnum;
	key96_t     *m_doledbKey;
	void      *m_state;
	void     (*m_callback)(void *state);

	bool indexedDoc ( class XmlDoc *doc );

	int32_t m_numSpidersOut;

	int32_t m_launches;

	// for spidering/parsing/indexing a url(s)
	class XmlDoc *m_docs [ MAX_SPIDERS ];

	// . this is "i" where m_msg14[i] is the highest m_msg14 in use
	// . we use it to limit our scanning to the first "i" m_msg14's
	int32_t m_maxUsed;

	// . list for getting next url(s) to spider
	RdbList m_list;

	// for getting RdbLists
	Msg5 m_msg5;

	class SpiderColl *m_sc;

	bool m_gettingDoledbList;
	HashTableX m_lockTable;
	// save on msg12 lookups! keep somewhat local...
	RdbCache   m_lockCache;

	RdbCache   m_winnerListCache;

	CollectionRec *getActiveList();
	void buildActiveList ( ) ;
	class CollectionRec *m_crx;
	class CollectionRec *m_activeList;
	CollectionRec *m_bookmark;
	bool m_activeListValid;
	bool m_activeListModified;
	int32_t m_activeListCount;
	uint32_t m_recalcTime;
	bool m_recalcTimeValid;

	int64_t m_lastCallTime;

	int64_t m_doleStart;

	int32_t m_processed;
};

extern class SpiderLoop g_spiderLoop;

void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;
void handleRequestc1 ( UdpSlot *slot , int32_t niceness );

#endif // GB_SPIDERLOOP_H
