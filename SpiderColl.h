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
#include <vector>


class CollectionRec;
class SpiderRequest;
class SpiderReply;



// we have one SpiderColl for each collection record
class SpiderColl {
public:
	SpiderColl(CollectionRec *cr);
	~SpiderColl();

	CollectionRec *getCollectionRec();
	const CollectionRec *getCollectionRec() const;
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

	Msg4 m_msg4x;

	bool isInDupCache(const SpiderRequest *sreq, bool addToCache);

	// Rdb.cpp calls this
	bool addSpiderReply(const SpiderReply *srep);
	bool addSpiderRequest(const SpiderRequest *sreq);

	// doledb cursor keys for each priority to speed up performance
	key96_t m_nextKeys[MAX_SPIDER_PRIORITIES];

	// used by SpiderLoop.cpp
	int32_t m_spidersOut;

	// . hash of collection name this arena represents
	// . 0 for main collection
	collnum_t m_collnum;
	char m_coll[MAX_COLL_LEN + 1];
	CollectionRec *getCollRec();
	const CollectionRec *getCollRec() const;
	const char *getCollName() const;

	void removeFromDoledbIpTable(int32_t firstIp);
	int32_t getDoledbIpTableCount() const;
	bool isDoledbIpTableEmpty() const;
	void clearDoledbIpTable();
	std::vector<uint32_t> getDoledbIpTable() const;

	HashTableX m_siteIndexedDocumentCount;

	bool printWaitingTree();

	bool addToWaitingTree(int32_t firstIp);
	void populateDoledbFromWaitingTree();

	void populateWaitingTreeFromSpiderdb(bool reentry);

	int32_t getWaitingTableCount() const;
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

	// how many outstanding spiders a priority has
	int32_t m_outstandingSpiders[MAX_SPIDER_PRIORITIES];

	bool isFirstIpInOverflowList(int32_t firstIp) const;

private:
	bool load();

	bool makeDoledbIPTable();
	bool addToDoledbIpTable(const SpiderRequest *sreq);
	bool isInDoledbIpTable(int32_t firstIp) const;

	bool validateDoleBuf(const SafeBuf *doleBuf);
	bool addDoleBufIntoDoledb(SafeBuf *doleBuf, bool isFromCache);

	bool updateSiteNumInlinksTable(int32_t siteHash32, int32_t sni, time_t tstamp);

	uint64_t getSpiderTimeMS(const SpiderRequest *sreq, int32_t ufn, const SpiderReply *srep, int64_t nowMS);

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
	char m_lastReplyBuf[sizeof(SpiderReply)];

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
	int64_t m_minFutureTimeMS;

	// list for loading spiderdb recs during the spiderdb scan
	RdbList        m_list;

	int32_t     m_numAdded;
	int64_t m_numBytesScanned;

	int64_t m_lastPrintCount;

	// freshest m_siteNumInlinks per site stored in here
	HashTableX m_sniTable;
	GbMutex m_sniTableMtx;

	// maps a domainHash32 to a crawl delay in milliseconds
	HashTableX m_cdTable;
	GbMutex m_cdTableMtx;

	RdbCache m_lastDownloadCache;

	int64_t m_lastReindexTimeMS;

	bool m_countingPagesIndexed;

	int64_t m_lastReqUh48a;
	int64_t m_lastReqUh48b;
	int64_t m_lastRepUh48;

	// spiderdb scan for populating waiting tree
	RdbList m_waitingTreeList;		// temporary list for reading spiderdb records
	bool m_gettingWaitingTreeList;	// true when waiting for spiderdb read to complete
	key128_t m_waitingTreeNextKey;	// temporary key used for reading spiderdb records
	time_t m_lastScanTime;

	std::atomic<int32_t> m_scanningIp;
	std::atomic<int32_t> m_gotNewDataForScanningIp;
	int32_t m_lastListSize;
	int32_t m_lastScanningIp;
	int64_t m_totalBytesScanned;

	int32_t *m_overflowList;
	int64_t  m_totalNewSpiderRequests;
	int64_t  m_lastSreqUh48;

	int32_t m_cblocks[20];
	int32_t m_pageNumInlinks;
	int32_t m_lastCBlockIp;
		
	int32_t  m_lastOverflowFirstIp;

	CollectionRec *m_cr;

	static void gotSpiderdbWaitingTreeListWrapper(void *state, RdbList *list, Msg5 *msg5);
};

#endif // GB_SPIDERCOLL_H
