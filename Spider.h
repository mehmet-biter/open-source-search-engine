// Matt Wells, copyright Nov 2002

#ifndef GB_SPIDER_H
#define GB_SPIDER_H

#define MAX_SPIDER_PRIORITIES 128
#define MAX_DAYS 365

#include "Rdb.h"
#include "Conf.h"
#include "Titledb.h"
#include "Hostdb.h"
#include "RdbList.h"
#include "RdbTree.h"
#include "HashTableX.h"
#include <time.h>
#include "Msg5.h"      // local getList()
#include "Msg4.h"
#include "Msg1.h"
#include "hash.h"
#include "RdbCache.h"
#include "Msg12.h"


// lower from 1300 to 300
#define MAXUDPSLOTS 300

extern int32_t g_corruptCount;
extern char s_countsAreValid;


// . this is in seconds
// . had to make this 4 hours since one url was taking more than an hour
//   to lookup over 80,000 places in placedb. after an hour it had only
//   reached about 30,000
//   http://pitchfork.com/news/tours/833-julianna-barwick-announces-european-and-north-american-dates/
// . this problem with this now is that it will lock an entire IP until it
//   expires if we have maxSpidersPerIp set to 1. so now we try to add
//   a SpiderReply for local errors like when XmlDoc::indexDoc() sets g_errno,
//   we try to add a SpiderReply at least.
#define MAX_LOCK_AGE (3600*4)

// . size of spiderecs to load in one call to readList
// . i increased it to 1MB to speed everything up, seems like cache is 
//   getting loaded up way too slow
#define SR_READ_SIZE (512*1024)

// seems like timecity.com as gigabytes of spiderdb data so up from 40 to 400
//#define MAX_WINNER_NODES 400

// up it to 2000 because shard #15 has slow disk reads and some collections
// are taking forever to spider because the spiderdb scan is so slow.
// we reduce this below if the spiderdb is smaller.
#define MAX_WINNER_NODES 2000



// for diffbot, this is for xmldoc.cpp to update CollectionRec::m_crawlInfo
// which has m_pagesCrawled and m_pagesProcessed.
//bool updateCrawlInfo ( CollectionRec *cr , 
//		       void *state ,
//		       void (* callback)(void *state) ,
//		       bool useCache = true ) ;

// . values for CollectionRec::m_spiderStatus
// . reasons why crawl is not happening
#define SP_INITIALIZING 0
#define SP_MAXROUNDS    1 // hit max rounds limit
#define SP_MAXTOCRAWL   2 // hit max to crawl limit
#define SP_MAXTOPROCESS 3 // hit max to process limit
#define SP_ROUNDDONE    4 // spider round is done
#define SP_NOURLS       5 // initializing
#define SP_PAUSED       6 // user paused spider
#define SP_INPROGRESS   7 // it is going on!
#define SP_ADMIN_PAUSED 8 // g_conf.m_spideringEnabled = false
#define SP_COMPLETED    9 // crawl is done, and no repeatCrawl is scheduled
#define SP_SEEDSERROR  10 // all seeds had an error preventing crawling

bool tryToDeleteSpiderColl ( SpiderColl *sc , const char *msg ) ;
void spiderRoundIncremented ( class CollectionRec *cr ) ;

bool hasPositivePattern ( char *content ) ;
bool doesStringContainPattern ( char *content , char *pattern ) ;

bool getSpiderStatusMsg ( class CollectionRec *cx , 
			  class SafeBuf *msg , 
			  int32_t *status ) ;

int32_t getFakeIpForUrl1 ( char *url1 ) ;
int32_t getFakeIpForUrl2 ( Url  *url2 ) ;



// Overview of Spider
//
// this new spider algorithm ensures that urls get spidered even if a host
// is dead. and even if the url was being spidered by a host that suddenly went
// dead.
//
// . Spider.h/.cpp contains all the code related to spider scheduling
// . Spiderdb holds the SpiderRecs which indicate the time to spider a url
// . there are 2 types of SpiderRecs: SpiderRequest and SpiderReply recs
//
//
// There are 3 main components to the spidering process:
// 1) spiderdb
// 2) the "waiting tree"
// 3) doledb
//
// spiderdb holds all the spiderrequests/spiderreplies sorted by 
// their IP
//
// the waiting tree holds at most one entry for an IP indicating that
// we should scan all the spiderrequests/spiderreplies for that IP in
// spiderdb, find the "best" one(s) and add it (them) to doledb.
//
// doledb holds the best spiderrequests from spiderdb sorted by
// "priority".  priorities range from 0 to 127, the highest priority.
// basically doledb holds the urls that are ready for spidering now.




// Spiderdb
//
// the spiderdb holds all the SpiderRequests and SpiderReplies, each of which
// are sorted by their "firstIP" and then by their 48-bit url hash, 
// "uh48". the parentDocId is also kept in the key to prevent collisions.
// Each group (shard) of hosts is responsible for spidering a fixed set of 
// IPs. 


// Dividing Workload by IP Address
//
// Each host is responsible for its own set of IP addresses. Each SpiderRequest
// contains an IP address called m_firstIP. It alone is responsible for adding
// SpiderRequests from this set of IPs to doledb.
// the doled out 
// SpiderRequests are added to doledb using Msg4. Once in doledb, a 
// SpiderRequest is ready to be spidered by any host in the group (shard), 
// provided that that host gets all the locks.



// "firstIP"
//
// when we lookup the ip address of the subdomain of an outlink for the first
// time we store that ip address into tagdb using the tag named "firstip".
// that way anytime we add outlinks from the same subdomain in the future they
// are guaranteed to get the same "firstip" even if the actual ip changed. this
// allows us to consistently throttle urls from the same subdomain, even if
// the subdomain gets a new ip. this also increaseses performance when looking
// up the "ips" of every outlink on a page because we are often just hitting
// tagdb, which is much faster than doing dns lookups, that might miss the 
// dns cache!


// Adding a SpiderRequest
//
// When a SpiderRequest is added to spiderdb in Rdb.cpp it calls
// SpiderColl::addSpiderRequest(). If our host is responsible for doling
// that firstIP, we check m_doleIPTable to see if that IP address is
// already in doledb. if it is then we bail. Next we compute the url filter
// number of the url in order to compute its spider time, then we add
// it to the waiting tree. It will not get added to the waiting tree if
// the current entry in the waiting tree has an earlier spider time.
// then when the waiting tree is scanned it will read SpiderRequests from
// spiderdb for just that firstIP and add the best one to doledb when it is
// due to be spidered.


// Waiting Tree
//
// The waiting tree is a b-tree where the keys are a spiderTime/IPaddress tuple
// of the corresponding SpiderRequest. Think of its keys as requests to
// spider something from that IP address at the given time, spiderTime.
// The entries are sorted by spiderTime first then IP address. 
// It let's us know the earliest time we can spider a SpiderRequest 
// from an IP address. We have exactly one entry in the waiting tree from
// every IP address that is in Spiderdb. "m_waitingTable" maps an IP 
// address to its entry in the waiting tree. If an IP should not be spidered
// until the future then its spiderTime in the waiting tree will be in the
// future. 


// Adding a SpiderReply
//
// We intercept SpiderReplies being added to Spiderdb in Rdb.cpp as well by
// calling SpiderColl::addSpiderReply().  Then we get the firstIP
// from that and we look in spiderdb to find a replacement SpiderRequest
// to add to doledb. To make this part easy we just add the firstIP to the
// waiting tree with a spiderTime of 0. so when the waiting tree scan happens
// it will pick that up and look in spiderdb for the best SpiderRequest with
// that same firstIP that can be spidered now, and then it adds that to
// doledb. (To prevent from having to scan int32_t spiderdb lists and speed 
// things up we might want to keep a little cache that maps a firstIP to 
// a few SpiderRequests ready to be spidered).



// Deleting Dups
//
// we now remove spiderdb rec duplicates in the spiderdb merge. we also call 
// getUrlFilterNum() on each spiderdb rec during the merge to see if it is
// filtered and not indexed, and if so, we delete it. we also delete all but
// the latest SpiderReply for a given uh48/url. And we remove redundant
// SpiderRequests like we used to do in addSpiderRequest(), which means that
// the merge op needs to keep a small little table to scan in order to
// compare all the SpiderRequests in the list for the same uh48. all of this
// deduping takes place on the final merged list which is then further
// filtered by this by calling Spiderdb.cpp::filterSpiderdbRecs(RdbList *list).
// because the list is just a random piece of spiderdb, boundary issues will
// cause some records to leak through, but with enough file merge operations
// they should eventually be zapped.


// DoleDB
//
// This holds SpiderRequests that are ready to be spidered right now. A host
// in our group (shard) will call getLocks() to get the locks for a 
// SpiderRequest in doledb that it wants to spider. it must receive grants 
// from every alive machine in the group in order to properly get the lock. 
// If it receives a rejection from one host it release the lock on all the 
// other hosts. It is kind of random to get a lock, similar to ethernet 
// collision detection.


// Dole IP Table
//
// m_doleIpTable (HashTableX, 96 bit keys, no data)
// Purpose: let's us know how many SpiderRequests have been doled out for
// a given firstIP
// Key is simply a 4-byte IP.
// Data is the number of doled out SpiderRequests from that IP. 
// we use m_doleIpTable for keeping counts based on ip of what is doled out. 


//
// g_doledb
//
// Purpose: holds the spider request your group (shard) is supposed to spider 
// according to getGroupIdToSpider(). 96-bit keys, data is the spider rec with 
// key. ranked according to when things should be spidered.
// <~priority>             8bits
// <spiderTime>           32bits
// <urlHash48>            48bits (to avoid collisions)
// <reserved>              7bits (used 7 bits from urlhash48 to avoid collisio)
// <delBit>                1bit
// DATA:
// <spiderRec>             Xbits (spiderdb record to spider, includes key)
// everyone in group (shard) tries to spider this shit in order.
// you call SpiderLoop::getLocks(sr,hostId) to get the lock for it before
// you can spider it. everyone in the group (shard) gets the lock request. 
// if you do not get granted lock by all alive hosts in the group (shard) then
// you call Msg12::removeAllLocks(sr,hostId). nobody tries to spider
// a doledb spider rec if the lock is granted to someone else, just skip it.
// if a doling host goes dead, then its twins will dole for it after their
// SpiderColl::m_nextReloadTime is reached and they reset their cache and
// re-scan spiderdb. XmlDoc adds the negative key to RDB_DOLEDB so that
// should remove it from doledb when the spidering is complete, and when 
// Rdb.cpp receives a "fake" negative TITLEDB key it removes the doledbKey lock
// from m_lockTable. See XmlDoc.cpp "fake titledb key". 
// Furthermore, if Rdb.cpp receives a positive doledbKey
// it might update SpiderColl::m_nextKeys[priority] so that the next read of
// doledb starts there when SpiderLoop::spiderDoledUrls() calls
// msg5 to read doledb records from disk.
// TODO: when a host dies, consider speeding up the reload. might be 3 hrs!
// PROBLEM: what if a host dies with outstanding locks???


// SpiderLoop::m_lockTable (HashTableX(6,8))
// Purpose: allows a host to lock a doledb key for spidering. used by Msg12
// and SpiderLoop. a host must get the lock for all its alive twins in its
// group (shard) before it can spider the SpiderRequest, otherwise, it will 
// removeall the locks from the hosts that did grant it by calling
// Msg12::removeAllLocks(sr,hostId).


// GETTING A URL TO SPIDER
//
// To actually spider something, we do a read of doledb to get the next
// SpiderRequest. Because there are so many negative/positive key annihilations
// in doledb, we keep a "cursor key" for each spider priority in doledb.
// We get a "lock" on the url so no other hosts in our group (shard) can 
// spider it from doledb. We get the lock if all hosts in the shard 
// successfully grant it to us, otherwise, we inform all the hosts that
// we were unable to get the lock, so they can unlock it.
//
// SpiderLoop::spiderDoledUrls() will scan doledb for each collection that
// has spidering enabled, and get the SpiderRequests in doledb that are
// in need of spidering. The keys in doledb are sorted by highest spider
// priority first and then by the "spider time". If one spider priority is
// empty or only has spiderRequests in it that can be spidered in the future,
// then the next priority is read.
//
// any host in our group (shard) can spider a request in doledb, but they must 
// lock it by calling getLocks() first and all hosts in the group (shard) must
// grant them the lock for that url otherwise they remove all the locks and
// try again on another spiderRequest in doledb.
//
// Each group (shard) is responsible for spidering a set of IPs in spiderdb.
// and each host in the group (shard) has its own subset of those IPs for which
// it is responsible for adding to doledb. but any host in the group (shard)
// can spider any request/url in doledb provided they get the lock.


// evalIpLoop()
//
// The waiting tree is populated at startup by scanning spiderdb (see
// SpiderColl::evalIpLoop()), which might take a while to complete, 
// so it is running in the background while the gb server is up. it will
// log "10836674298 spiderdb bytes scanned for waiting tree re-population"
// periodically in the log as it tries to do a complete spiderdb scan 
// every 24 hours. It should not be necessary to scan spiderdb more than
// once, but it seems we are leaking ips somehow so we do the follow-up
// scans for now. (see populateWaitingTreeFromSpiderdb() in Spider.cpp)
// It will also perform a background scan if the admin changes the url
// filters table, which dictates that we recompute everything.
//
// evalIpLoop() will recompute the "url filter number" (matching row)
// in the url filters table for each url in each SpiderRequest it reads.
// it will ignore spider requests whose urls
// are "filtered" or "banned". otherwise they will have a spider priority >= 0.
// So it calls ::getUrlFilterNum() for each url it scans which is where
// most of the cpu it uses will probably be spent. It picks the best
// url to spider for each IP address. It only picks one per IP right now.
// If the best url has a scheduled spider time in the future, it will add it 
// to the waiting tree with that future timestamp. The waiting tree only
// stores one entry for each unique IP, so it tries to store
// the entry with the earliest computed scheduled spider time, but if 
// some times are all BEFORE the current time, it will resolve conflicts
// by preferring those with the highest priority. Tied spider priorities
// should be resolved by minimum hopCount probably. 
//
// If the spidertime of the URL is overdue then evalIpLoop() will NOT add
// it to waiting tree, but will add it to doledb directly to make it available
// for spidering immediately. It calls m_msg4.addMetaList() to add it to 
// doledb on all hosts in its group (shard). It uses s_ufnTree for keeping 
// track of the best urls to spider for a given IP/spiderPriority.
//
// evalIpLoop() can also be called with its m_nextKey/m_endKey limited
// to just scan the SpiderRequests for a specific IP address. It does
// this after adding a SpiderReply. addSpiderReply() calls addToWaitingTree()
// with the "0" time entry, and addToWaitingTree() calls 
// populateDoledbFromWaitingTree() which will see that "0" entry and call
// evalIpLoop(true) after setting m_nextKey/m_endKey for that IP.



// POPULATING DOLEB
//
// SpiderColl::populateDoledbFromWaitingTree() scans the waiting tree for
// entries whose spider time is due. so it gets the IP address and spider
// priority from the waiting tree. but then it calls evalIpLoop() 
// restricted to that IP (using m_nextKey,m_endKey) to get the best
// SpiderRequest from spiderdb for that IP to add to doledb for immediate 
// spidering. populateDoledbFromWaitingTree() is called a lot to try to
// keep doledb in sync with waiting tree. any time an entry in the waiting
// tree becomes available for spidering it should be called right away so
// as not to hold back the spiders. in general it should exit quickly because
// it calls getNextIpFromWaitingTree() which most of the time will return 0
// indicating there are no IPs in the waiting tree ready to be spidered.
// Which is why as we add SpiderRequests to doledb for an IP we also
// remove that IP from the waiting tree. This keeps this check fast.


// SUPPORTING MULTIPLE SPIDERS PER IP
//
// In order to allow multiple outstanding spiders per IP address, if, say,
// maxSpidersPerIp is > 1, we now promptly add the negative doledb key
// as soon as a lock is granted and we also add an entry to the waiting tree
// which will result in an addition to doledb of the next unlocked 
// SpiderRequest. This logic is mostly in Spider.cpp's Msg12::gotLockReply().
//
// Rdb.cpp will see that we added a "fakedb" record 

// A record is only removed from Doledb after the spider adds the negative
// doledb record in XmlDoc.cpp when it is done. XmlDoc.cpp also adds a
// "fake" negative titledb record to remove the lock on that url at the
// same time.
// 
// So, 1) we can allow for multiple doledb entries per IP and the assigned
// host can reply with "wait X ms" to honor the spiderIpWait constraint,
// or 2) we can delete the doledb entry after the lock is granted, and then
// we can immediately add a "currentTime + X ms" entry to the waiting tree to 
// add the next doledb record for this IP X ms from now.
//
// I kind of like the 2nd approach because then there is only one entry
// per IP in doledb. that is kind of nice. So maybe using the same
// logic that is used by Spider.cpp to release a lock, we can say,
// "hey, i got the lock, delete it from doledb"...




// . what groupId (shardId) should spider/index this spider request?
// . CAUTION: NOT the same group (shard) that stores it in spiderdb!!!
// . CAUTION: NOT the same group (shard) that doles it out to spider!!!
//uint32_t getGroupIdToSpider ( char *spiderRec );


// The 128-bit Spiderdb record key128_t for a rec in Spiderdb is as follows:
//
// <32 bit firstIp>             (firstIp of the url to spider)
// <48 bit normalized url hash> (of the url to spider)
// <1  bit isRequest>           (a SpiderRequest or SpiderReply record?)
// <38 bit docid of parent>     (to avoid collisions!)
// <8  bit reserved>            (was "spiderLinks"/"forced"/"retryNum")
// <1  bit delbit>              (0 means this is a *negative* key)

// there are two types of SpiderRecs really, a "request" to spider a url
// and a "reply" or report on the attempted spidering of a url. in this way
// Spiderdb is a perfect log of desired and actual spider activity.

// . Spiderdb contains an m_rdb which has SpiderRecs/urls to be spidered
// . we split the SpiderRecs up w/ the hosts in our group (shard) by IP of the
//   url. 
// . once we've spidered a url it gets added with a negative spiderdb key
//   in XmlDoc.cpp
class Spiderdb {

  public:

	// reset rdb
	void reset();
	
	// set up our private rdb for holding SpiderRecs
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( int32_t treeMem );

	bool verify ( char *coll );

	Rdb *getRdb  ( ) { return &m_rdb; }

	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	int64_t getUrlHash48( key128_t *k ) {
		return (((k->n1)<<16) | k->n0>>(64-16)) & 0xffffffffffffLL;
	}
	
	bool isSpiderRequest( key128_t *k ) {
		return (k->n0>>(64-17))&0x01;
	}

	bool isSpiderReply( key128_t *k ) {
		return ((k->n0>>(64-17))&0x01)==0x00;
	}

	int64_t getParentDocId( key128_t *k ) {
		return (k->n0>>9)&DOCID_MASK;
	}

	// same as above
	int64_t getDocId( key128_t *k ) {
		return (k->n0>>9)&DOCID_MASK;
	}

	int32_t getFirstIp( key128_t *k ) {
		return (k->n1>>32);
	}
	
	key128_t makeKey( int32_t firstIp, int64_t urlHash48, bool isRequest, int64_t parentDocId, bool isDel );

	key128_t makeFirstKey( int32_t firstIp ) {
		return makeKey( firstIp, 0LL, false, 0LL, true );
	}

	key128_t makeLastKey( int32_t firstIp ) {
		return makeKey( firstIp, 0xffffffffffffLL, true, MAX_DOCID, false );
	}

	// print the spider rec
	int32_t print( char *srec , SafeBuf *sb = NULL );
};

void dedupSpiderdbList ( RdbList *list );

extern class Spiderdb g_spiderdb;
extern class Spiderdb g_spiderdb2;

class SpiderRequest {
public:

	// we now define the data so we can use this class to cast
	// a SpiderRec outright
	key128_t   m_key;
	
	int32_t    m_dataSize;

	// this ip is taken from the TagRec for the domain of the m_url,
	// but we do a dns lookup initially if not in tagdb and we put it in
	// tagdb then. that way, even if the domain gets a new ip, we still
	// use the original ip for purposes of deciding which groupId (shardId)
	// is responsible for storing, doling/throttling this domain. if the
	// ip lookup results in NXDOMAIN or another error then we generally
	// do not add it to tagdb in msge*.cpp. this ensures that any given
	// domain will always be spidered by the same group (shard) of hosts 
	// even if the ip changes later on. this also increases performance 
	// since we do a lot fewer dns lookups on the outlinks.
	int32_t    m_firstIp;

	int32_t    m_hostHash32;
	int32_t    m_domHash32;
	int32_t    m_siteHash32;

	// this is computed from every outlink's tagdb record but i guess
	// we can update it when adding a spider rec reply
	int32_t    m_siteNumInlinks;

	// . when this request was first was added to spiderdb
	// . Spider.cpp dedups the oldest SpiderRequests that have the
	//   same bit flags as this one. that way, we get the most uptodate
	//   date in the request... UNFORTUNATELY we lose m_addedTime then!!!
	uint32_t  m_addedTime; // time_t

	// if m_isNewOutlink is true, then this SpiderRequest is being added 
	// for a link that did not exist on this page the last time it was
	// spidered. XmlDoc.cpp needs to set XmlDoc::m_min/maxPubDate for
	// m_url. if m_url's content does not contain a pub date explicitly
	// then we can estimate it based on when m_url's parent was last
	// spidered (when m_url was not an outlink on its parent page)
	uint32_t  m_parentPrevSpiderTime; // time_t

	// # of spider requests from different c-blocks. capped at 255.
	// taken from the # of SpiderRequests.
	uint8_t    m_pageNumInlinks;
	uint8_t    m_reservedb2;
	uint8_t    m_reservedb3;
	uint8_t    m_reservedb4;

	// info on the page we were harvest from
	int32_t    m_reservedb5;
	int32_t    m_reservedb6;
	int32_t    m_reservedb7;

	// if there are several spiderrequests for a url, this should be
	// the earliest m_addedTime, basically, the url discovery time. this is
	// NOT valid in spiderdb, but only set upon selecting the url to spider
	// when we scan all of the SpiderRequests it has.
	int32_t m_discoveryTime;

	int32_t m_reservedc2;

	// . replace this with something we need for smart compression
	// . this is zero if none or invalid
	int32_t    m_contentHash32;

	// . each request can have a different hop count
	// . this is only valid if m_hopCountValid is true!
	// . i made this a int16_t from int32_t to support m_parentLangId etc above
	int16_t   m_hopCount;
	
	uint8_t m_reservedb8;

	unsigned char    m_reserved2k:1;
	unsigned char    m_recycleContent:1;
	unsigned char    m_reserved2i:1;
	unsigned char    m_reserved2j:1;

	unsigned char    m_reserved2e:1;
	unsigned char    m_reserved2f:1;
	unsigned char    m_reserved2g:1;
	unsigned char    m_reserved2h:1;

	//
	// our bit flags
	//

	unsigned    m_hopCountValid:1;

	// are we a request/reply from the add url page?
	unsigned    m_isAddUrl:1;

	// are we a request/reply from PageReindex.cpp
	unsigned    m_isPageReindex:1;

	// are we a request/reply from PageInject.cpp
	unsigned    m_reserved3a:1;

	// or from PageParser.cpp directly
	unsigned    m_isPageParser:1;

	unsigned    m_reserved3q:1;

	// . is the url a docid (not an actual url)
	// . could be a "query reindex"
	unsigned    m_urlIsDocId:1;

	// does m_url end in .rss .xml .atom? or a related rss file extension?
	unsigned    m_isRSSExt:1;

	// is url in a format known to be a permalink format?
	unsigned    m_isUrlPermalinkFormat:1;

	// is url "rpc.weblogs.com/shortChanges.xml"?
	unsigned    m_isPingServer:1;

	// . are we a delete instruction? (from Msg7.cpp as well)
	// . if you want it to be permanently banned you should ban or filter
	//   it in the urlfilters/tagdb. so this is kinda useless...
	unsigned    m_forceDelete:1;

	// are we a fake spider rec? called from Test.cpp now!
	unsigned    m_isInjecting:1;

	// are we a respider request from Sections.cpp
	//unsigned    m_fromSections:1;
	// a new flag. replaced above. did we have a corresponding SpiderReply?
	unsigned    m_hadReply:1;

	unsigned    m_reserved3b:1;
	unsigned    m_reserved3c:1;

	// is first ip a hash of url or docid or whatever?
	unsigned    m_fakeFirstIp:1;

	// www.xxx.com/*? or xxx.com/*?
	unsigned    m_isWWWSubdomain:1;

	unsigned    m_reserved3x        :1;
	unsigned    m_reserved3s        :1;
	unsigned    m_reserved3t        :1;
	unsigned    m_reserved3u        :1;
	unsigned    m_reserved3v        :1;
	unsigned    m_reserved3o        :1;
	unsigned    m_reserved3p        :1;
	unsigned    m_reserved3r        :1;
	unsigned    m_reserved3l        :1;
	unsigned    m_reserved3w        :1;


	// 
	// these bits also in SpiderReply
	//

	unsigned    m_reserved3h:1;

	// expires after a certain time or if ownership changed
	// did it have an inlink from a really nice site?
	unsigned    m_hasAuthorityInlink :1;

	unsigned    m_reserved3m         :1;
	unsigned    m_reserved3j         :1;
	unsigned    m_reserved3d         :1;
	unsigned    m_reserved3i              :1;

	unsigned    m_hasAuthorityInlinkValid :1;
	unsigned    m_reserved3n              :1;
	unsigned    m_reserved3k              :1;
	unsigned    m_reserved3e              :1;
	unsigned    m_reserved3f              :1;
	unsigned    m_reserved3g              :1;

	unsigned    m_siteNumInlinksValid     :1;

	// we set this to one from Diffbot.cpp when urldata does not
	// want the url's to have their links spidered. default is to make
	// this 0 and to not avoid spidering the links.
	unsigned    m_avoidSpiderLinks:1;

	unsigned    m_reserved3y:1;

	//
	// INTERNAL USE ONLY
	//

	// . what url filter num do we match in the url filters table?
	// . determines our spider priority and wait time
	int16_t   m_ufn;

	// . m_priority is dynamically computed like m_spiderTime
	// . can be negative to indicate filtered, banned, skipped, etc.
	// . for the spiderrec request, this is invalid until it is set
	//   by the SpiderCache logic, but for the spiderrec reply this is
	//   the priority we used!
	char    m_priority;

	// . this is copied from the most recent SpiderReply into here
	// . its so XMlDoc.cpp can increment it and add it to the new
	//   SpiderReply it adds in case there is another download error ,
	//   like ETCPTIMEDOUT or EDNSTIMEDOUT
	char    m_errCount;

	// we really only need store the url for *requests* and not replies
	char    m_url[MAX_URL_LEN+1];

	// . basic functions
	// . clear all
	void reset() { 
		memset ( this , 0 , (char *)m_url - (char *)&m_key ); 
		// -1 means uninitialized, this is required now
		m_ufn = -1;
		// this too
		m_priority = -1;
	}

	static int32_t getNeededSize ( int32_t urlLen ) {
		return sizeof(SpiderRequest) - (int32_t)MAX_URL_LEN + urlLen; }

	int32_t getRecSize () { return m_dataSize + 4 + sizeof(key128_t); }

	int32_t getUrlLen() {
		return m_dataSize -
		       // subtract the \0
		       ((char *)m_url-(char *)&m_firstIp) - 1;
	}

	char *getUrlPath() {
		char *p = m_url;
		for ( ; *p ; p++ ) {
			if ( *p != ':' ) continue;
			p++; 
			if ( *p != '/' ) continue;
			p++; 
			if ( *p != '/' ) continue;
			p++;
			break;
		}
		if ( ! *p ) return NULL;
		// skip until / then
		for ( ; *p && *p !='/' ; p++ ) ;
		if ( *p != '/' ) return NULL;
		// return root path of / if there.
		return p;
	}

	void setKey ( int32_t firstIp, int64_t parentDocId, int64_t uh48, bool isDel ) ;

	void setKey ( int32_t firstIp, int64_t parentDocId , bool isDel ) { 
		int64_t uh48 = hash64b ( m_url );
		setKey ( firstIp , parentDocId, uh48, isDel );
	}

	void setDataSize ( );

	int64_t  getUrlHash48() {
		return g_spiderdb.getUrlHash48( &m_key );
	}

	int64_t getParentDocId() {
		return g_spiderdb.getParentDocId( &m_key );
	}

	int32_t print( class SafeBuf *sb );

	int32_t printToTable( SafeBuf *sb, const char *status, class XmlDoc *xd, int32_t row ) ;

	// for diffbot...
	int32_t printToTableSimple( SafeBuf *sb, const char *status, class XmlDoc *xd, int32_t row ) ;
	static int32_t printTableHeader ( SafeBuf *sb, bool currentlSpidering ) ;
	static int32_t printTableHeaderSimple ( SafeBuf *sb, bool currentlSpidering ) ;

	// returns false and sets g_errno on error
	bool setFromAddUrl ( char *url ) ;
	bool setFromInject ( char *url ) ;

	bool isCorrupt ( );
} __attribute__((packed, aligned(4)));

// . XmlDoc adds this record to spiderdb after attempting to spider a url
//   supplied to it by a SpiderRequest
// . before adding a SpiderRequest to the spider cache, we scan through
//   all of its SpiderRecReply records and just grab the last one. then
//   we pass that to ::getUrlFilterNum()
// . if it was not a successful reply, then we try to populate it with
//   the member variables from the last *successful* reply before passing
//   it to ::getUrlFilterNum()
// . getUrlFilterNum() also takes the SpiderRequest record as well now
// . we only keep the last X successful SpiderRecReply records, and the 
//   last unsucessful Y records (only if more recent), and we nuke all the 
//   other SpiderRecReply records
class SpiderReply {
public:
	// we now define the data so we can use this class to cast
	// a SpiderRec outright
	key128_t   m_key;

	// this can be used for something else really. all SpiderReplies are fixed sz
	int32_t    m_dataSize;

	// for calling getHostIdToDole()
	int32_t    m_firstIp;

	// we need this too in case it changes!
	int32_t    m_siteHash32;

	// and this for updating crawl delay in m_cdTable
	int32_t    m_domHash32;

	// since the last successful SpiderRecReply
	float   m_percentChangedPerDay;

	// when we attempted to spider it
	uint32_t  m_spideredTime; // time_t

	// . value of g_errno/m_indexCode. 0 means successfully indexed.
	// . might be EDOCBANNED or EDOCFILTERED
	int32_t    m_errCode;

	// this is fresher usually so we can use it to override 
	// SpiderRequest's m_siteNumLinks
	int32_t    m_siteNumInlinks;

	// the actual pub date we extracted (0 means none, -1 unknown)
	int32_t    m_pubDate;

	// . this is zero if none or invalid
	int32_t    m_contentHash32;

	// in milliseconds, from robots.txt (-1 means none)
	// TODO: store in tagdb, lookup when we lookup tagdb recs for all out outlinks
	int32_t    m_crawlDelayMS;

	// . when we basically finished DOWNLOADING it
	// . use 0 if we did not download at all
	// . used by Spider.cpp to space out urls using sameIpWait
	int64_t  m_downloadEndTime;

	// . like "404" etc. "200" means successfully downloaded
	// . we can still successfully index pages that are 404 or permission
	//   denied, because we might have link text for them.
	int16_t   m_httpStatus;

	// . only non-zero if errCode is set!
	// . 1 means it is the first time we tried to download and got an error
	// . 2 means second, etc.
	char    m_errCount;

	// what language was the page in?
	char    m_langId;

	//
	// our bit flags
	//

	// XmlDoc::isSpam() returned true for it!
	//unsigned char    m_isSpam:1; 
	// was the page in rss format?
	unsigned    m_isRSS:1;

	// was the page a permalink?
	unsigned    m_isPermalink:1;

	// are we a pingserver page?
	unsigned    m_isPingServer:1;

	// was it in the index when we were done?
	unsigned    m_isIndexed:1;

	// 
	// these bits also in SpiderRequest
	//

	unsigned    m_reserved3:1;

	// did it have an inlink from a really nice site?
	unsigned    m_hasAuthorityInlink:1;

	unsigned    m_reserved002   :1;
	unsigned    m_reserved001   :1;
	unsigned    m_reserved5     :1;
	unsigned    m_reserved006   :1;

	// make this "INvalid" not valid since it was set to 0 before
	// and we want to be backwards compatible
	unsigned    m_isIndexedINValid :1;

	// expires after a certain time or if ownership changed
	unsigned    m_reserved4               :1;
	unsigned    m_reserved003             :1;
	unsigned    m_hasAuthorityInlinkValid :1;
	unsigned    m_reserved004             :1;
	unsigned    m_reserved005             :1;
	unsigned    m_reserved007             :1;
	unsigned    m_reserved2               :1;
	unsigned    m_siteNumInlinksValid     :1;

	// was the request an injection request
	unsigned    m_fromInjectionRequest    :1;

	unsigned    m_reserved008             :1;
	unsigned    m_reserved009             :1;

	// . was it in the index when we started?
	// . we use this with m_isIndexed above to adjust quota counts for
	//   this m_siteHash32 which is basically just the subdomain/host
	//   for SpiderColl::m_quotaTable
	unsigned    m_wasIndexed              :1;

	// this also pertains to m_isIndexed as well:
	unsigned    m_wasIndexedValid         :1;

	// how much buf will we need to serialize ourselves?
	int32_t getRecSize () { return m_dataSize + 4 + sizeof(key128_t); }

	// clear all
	void reset() { memset ( this , 0 , sizeof(SpiderReply) ); }

	void setKey ( int32_t firstIp, int64_t parentDocId, int64_t uh48, bool isDel ) ;

	int32_t print ( class SafeBuf *sbarg );

	int64_t  getUrlHash48  () {
		return g_spiderdb.getUrlHash48(&m_key);
	}

	int64_t getParentDocId (){
		return g_spiderdb.getParentDocId(&m_key);
	}
} __attribute__((packed, aligned(4)));

// was 1000 but breached, now equals SR_READ_SIZE/sizeof(SpiderReply)
#define MAX_BEST_REQUEST_SIZE (MAX_URL_LEN+1+sizeof(SpiderRequest))
#define MAX_DOLEREC_SIZE      (MAX_BEST_REQUEST_SIZE+sizeof(key_t)+4)
#define MAX_SP_REPLY_SIZE     (sizeof(SpiderReply))

// are we responsible for this ip?
bool isAssignedToUs ( int32_t firstIp ) ;

#define SPIDERDBKEY key128_t

class SpiderColl;

class SpiderCache {

 public:

	// returns false and set g_errno on error
	bool init ( ) ;

	SpiderCache ( ) ;

	// what SpiderColl does a SpiderRec with this key belong?
	SpiderColl *getSpiderColl ( collnum_t collNum ) ;

	SpiderColl *getSpiderCollIffNonNull ( collnum_t collNum ) ;

	// called by main.cpp on exit to free memory
	void reset();

	void save ( bool useThread );

	bool needsSave ( ) ;
	void doneSaving ( ) ;

	bool m_isSaving;
};

extern class SpiderCache g_spiderCache;

/////////
//
// we now include the firstip in the case where the same url
// has 2 spiderrequests where one is a fake firstip. in that scenario
// we will miss the spider request to spider, the waiting tree
// node will be removed, and the spider round will complete, 
// which triggers a waiting tree recompute and we end up spidering
// the dup spider request right away and double increment the round.
//
/////////
inline int64_t makeLockTableKey ( int64_t uh48 , int32_t firstIp ) {
	return uh48 ^ (uint32_t)firstIp;
}

inline int64_t makeLockTableKey ( SpiderRequest *sreq ) {
	return makeLockTableKey(sreq->getUrlHash48(),sreq->m_firstIp);
}

inline int64_t makeLockTableKey ( SpiderReply *srep ) {
	return makeLockTableKey(srep->getUrlHash48(),srep->m_firstIp);
}


class UrlLock {
public:
	int32_t m_hostId;
	int32_t m_lockSequence;
	int32_t m_timestamp;
	int32_t m_expires;
	int32_t m_firstIp;
	char m_spiderOutstanding;
	char m_confirmed;
	collnum_t m_collnum;
};

int32_t getUrlFilterNum ( class SpiderRequest *sreq , 
		       class SpiderReply *srep,
		       int32_t nowGlobal,
		       bool isForMsg20,
		       int32_t niceness,
		       class CollectionRec *cr,
		       bool isOutlink,
			  HashTableX *quotaTable,
			  int32_t langIdArg );

void parseWinnerTreeKey ( key192_t  *k ,
			  int32_t      *firstIp ,
			  int32_t      *priority ,
			  int32_t *hopCount,
			  int64_t  *spiderTimeMS ,
			  int64_t *uh48 );

key192_t makeWinnerTreeKey ( int32_t firstIp ,
			     int32_t priority ,
			     int32_t hopCount,
			     int64_t spiderTimeMS ,
			     int64_t uh48 );

#endif // GB_SPIDER_H
