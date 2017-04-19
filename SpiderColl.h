// Matt Wells, copyright Nov 2002

#ifndef GB_SPIDERCOLL_H
#define GB_SPIDERCOLL_H


#include "RdbList.h"
#include "RdbTree.h"
#include "HashTableX.h"
#include "Msg5.h"
#include "Msg4Out.h"
#include "hash.h"
#include "RdbCache.h"
#include "Spider.h"  //MAX_SP_REPLY_SIZE
#include "types.h"
#include "max_coll_len.h"
#include <time.h>


class CollectionRec;
class SpiderRequest;
class SpiderReply;


#define OVERFLOWLISTSIZE 200

// we have one SpiderColl for each collection record
class SpiderColl {
public:
	SpiderColl(CollectionRec *cr);
	~SpiderColl();

	CollectionRec *getCollectionRec();
	void setCollectionRec(CollectionRec *cr);

	void clearLocks();

	// called by main.cpp on exit to free memory
	void reset();

	static bool tryToDeleteSpiderColl(SpiderColl *sc, const char *msg);

	// corresponding to CollectionRec::m_siteListBuf
	bool  m_siteListIsEmpty;
	bool  m_siteListIsEmptyValid;
	// data buckets in this table are of type
	HashTableX m_siteListDomTable;
	// substring matches like "contains:goodstuff" or
	// later "regex:.*"
	SafeBuf m_negSubstringBuf;
	SafeBuf m_posSubstringBuf;

	// . do not re-send CrawlInfoLocal for a coll if not update
	// . we store the flags in here as true if we should send our
	//   CrawlInfoLocal for this coll to this hostId
	char m_sendLocalCrawlInfoToHost[MAX_HOSTS];

	Msg4 m_msg4x;

	bool isInDupCache(const SpiderRequest *sreq, bool addToCache);

	// Rdb.cpp calls this
	bool addSpiderReply(const SpiderReply *srep);
	bool addSpiderRequest(const SpiderRequest *sreq, int64_t nowGlobalMS);

	// doledb cursor keys for each priority to speed up performance
	key96_t m_nextKeys[MAX_SPIDER_PRIORITIES];

	int64_t m_lastPrintCount;
	int64_t m_lastPrinted;

	// used by SpiderLoop.cpp
	int32_t m_spidersOut;

	// . hash of collection name this arena represents
	// . 0 for main collection
	collnum_t m_collnum;
	char m_coll[MAX_COLL_LEN + 1];
	class CollectionRec *getCollRec();
	const char *getCollName();

	void removeFromDoledbIpTable(int32_t firstIp);
	int32_t getDoledbIpTableCount() const;
	void disableDoledbIpTableWrites();
	bool isDoledbIpTableEmpty() const;
	void clearDoledbIpTable();

	HashTableX m_localTable;

	bool printWaitingTree ( ) ;

	bool addToWaitingTree(int32_t firstIp);
	uint64_t getNextSpiderTimeFromWaitingTree ( ) ;
	void populateDoledbFromWaitingTree ( );

	void populateWaitingTreeFromSpiderdb ( bool reentry ) ;

	int32_t getWaitingTableCount() const;
	void disableWaitingTableWrites();
	void clearWaitingTable();

	bool     m_waitingTreeNeedsRebuild;
	RdbTree    m_waitingTree;
	RdbMem     m_waitingMem; // used by m_waitingTree
	key96_t      m_waitingTreeKey;
	bool       m_waitingTreeKeyValid;

	void resetWaitingTreeNextKey() { m_waitingTreeNextKey.setMin(); }
	time_t getLastScanTime() const { return m_lastScanTime; }

	int32_t getScanningIp() const {return m_scanningIp; }

	bool m_deleteMyself;

	// start key for reading doledb
	key96_t m_msg5StartKey;

	void devancePriority();
	void setPriority(int32_t pri);

	key96_t m_nextDoledbKey;
	int32_t  m_pri2;

	bool gettingSpiderdbList() const { return m_gettingList1; }

	// how many outstanding spiders a priority has
	int32_t m_outstandingSpiders[MAX_SPIDER_PRIORITIES];

	bool printStats ( SafeBuf &sb ) ;

	bool isFirstIpInOverflowList ( int32_t firstIp ) ;

private:
	bool load();

	bool makeDoledbIPTable();
	bool addToDoledbIpTable(SpiderRequest *sreq);
	bool isInDoledbIpTable(int32_t firstIp) const;

	bool validateDoleBuf(SafeBuf *doleBuf);
	bool addDoleBufIntoDoledb(SafeBuf *doleBuf, bool isFromCache);

	bool updateSiteNumInlinksTable(int32_t siteHash32, int32_t sni, time_t tstamp);

	uint64_t getSpiderTimeMS(SpiderRequest *sreq, int32_t ufn, SpiderReply *srep);

	bool makeWaitingTable();
	bool addToWaitingTable(int32_t firstIp, int64_t timeMs);
	bool getFromWaitingTable(int32_t firstIp, int64_t *timeMs);
	void removeFromWaitingTable(int32_t firstIp);
	bool isInWaitingTable(int32_t firstIp) const;
	bool setWaitingTableSize(int32_t numSlots);

	int32_t getNextIpFromWaitingTree ( );

	// broke up scanSpiderdb into simpler functions:
	bool evalIpLoop ( ) ;
	bool readListFromSpiderdb ( ) ;
	bool scanListForWinners ( ) ;
	bool addWinnersIntoDoledb ( ) ;

	key128_t m_firstKey;
	key128_t m_nextKey;
	key128_t m_endKey;

	bool m_lastReplyValid;
	char m_lastReplyBuf[MAX_SP_REPLY_SIZE];

	bool m_isLoading;

	// for scanning the wait tree...
	bool m_isPopulatingDoledb;

	bool m_didRead;

	RdbCache m_dupCache;

	HashTableX m_waitingTable;
	mutable GbMutex m_waitingTableMtx;

	// m_doledbIpTable (HashTableX, 96 bit keys, no data)
	// Purpose: let's us know how many SpiderRequests have been doled out for a given firstIP
	// Key is simply a 4-byte IP.
	// Data is the number of doled out SpiderRequests from that IP.
	// we use m_doledbIpTable for keeping counts based on ip of what is doled out.
	HashTableX m_doledbIpTable;
	mutable GbMutex m_doledbIpTableMtx;

	RdbTree m_winnerTree;
	HashTableX m_winnerTable;
	int32_t m_tailIp;
	int32_t m_tailPriority;
	int64_t m_tailTimeMS;
	int64_t m_tailUh48;
	int32_t      m_tailHopCount;
	int64_t m_minFutureTimeMS;

	// list for loading spiderdb recs during the spiderdb scan
	RdbList        m_list;

	int32_t     m_numAdded;
	int64_t m_numBytesScanned;

	// freshest m_siteNumInlinks per site stored in here
	HashTableX m_sniTable;
	GbMutex m_sniTableMtx;

	// maps a domainHash32 to a crawl delay in milliseconds
	HashTableX m_cdTable;
	GbMutex m_cdTableMtx;

	RdbCache m_lastDownloadCache;

	bool m_countingPagesIndexed;

	int64_t m_lastReqUh48a;
	int64_t m_lastReqUh48b;
	int64_t m_lastRepUh48;

	// spiderdb scan for populating waiting tree
	RdbList m_waitingTreeList;
	Msg5 m_msg5b;
	bool m_gettingWaitingTreeList;
	key128_t m_waitingTreeNextKey;
	key128_t m_waitingTreeEndKey;
	time_t m_lastScanTime;

	int32_t m_scanningIp;
	int32_t m_gotNewDataForScanningIp;
	int32_t m_lastListSize;
	int32_t m_lastScanningIp;
	int64_t m_totalBytesScanned;

	// for reading lists from spiderdb
	Msg5 m_msg5;
	bool m_gettingList1;

	int32_t *m_overflowList;
	int64_t  m_totalNewSpiderRequests;
	int64_t  m_lastSreqUh48;

	int32_t m_cblocks[20];
	int32_t m_pageNumInlinks;
	int32_t m_lastCBlockIp;
		
	int32_t  m_lastOverflowFirstIp;

	CollectionRec *m_cr;

	static void gotSpiderdbListWrapper(void *state, RdbList *list, Msg5 *msg5);
	static void gotSpiderdbWaitingTreeListWrapper(void *state, RdbList *list, Msg5 *msg5);
};

#endif // GB_SPIDERCOLL_H
