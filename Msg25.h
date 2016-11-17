#ifndef MSG25_H_
#define MSG25_H_

#include "types.h"
#include "SafeBuf.h"
#include "Msg20.h"     // for getting this url's LinkInfo from another cluster
#include "HashTableX.h"
#include "Msg22.h"
#include "Msg5.h"
#include "max_coll_len.h"
#include "max_url_len.h"

class LinkInfo;
class Inlink;
class Multicast;
class UdpSlot;


void  handleRequest25(UdpSlot *slot, int32_t netnice);


// . get the inlinkers to this SITE (any page on this site)
// . use that to compute a site quality
// . also get the inlinkers sorted by date and see how many good inlinkers
//   we had since X days ago. (each inlinker needs a pub/birth date)
class Msg25Request {
public:
	// either MODE_PAGELINKINFO or MODE_SITELINKINFO
	char          m_mode;
	int32_t       m_ip;
	int64_t       m_docId;
	collnum_t     m_collnum;
	bool          m_isInjecting;
	bool          m_printInXml;

	// when we get a reply we call this
	void         *m_state;
	void        (*m_callback)(void *state);

	int32_t       m_siteNumInlinks;
	LinkInfo     *m_oldLinkInfo;
	int32_t       m_niceness;
	bool          m_doLinkSpamCheck;
	bool          m_oneVotePerIpDom;
	bool          m_canBeCancelled;
	int32_t       m_lastUpdateTime;
	bool          m_onlyNeedGoodInlinks;
	bool          m_getLinkerTitles;
	int32_t       m_ourHostHash32;
	int32_t       m_ourDomHash32;

	// new stuff
	int32_t       m_siteHash32;
	int64_t       m_siteHash64;
	int64_t       m_linkHash64;
	// for linked list of these guys in g_lineTable in Linkdb.cpp
	// but only used on the server end, not client end
	Msg25Request *m_next;
	// the mutlicast we use
	Multicast    *m_mcast;
	UdpSlot      *m_udpSlot;
	bool          m_printDebugMsgs;
	// store final LinkInfo reply in here
	SafeBuf      *m_linkInfoBuf;

	char         *ptr_site;
	char         *ptr_url;
	const char   *ptr_oldLinkInfo;

	int32_t       size_site;
	int32_t       size_url;
	int32_t       size_oldLinkInfo;

	//variable data begins here

	int32_t getStoredSize();
	void serialize();
	void deserialize();
};


// . get ALL the linkText classes for a url and merge 'em into a LinkInfo class
// . also gets the link-adjusted quality of our site's url (root url)
// . first gets all docIds of docs that link to that url via an link: search
// . gets the LinkText, customized for our url, from each docId in that list
// . merge them into a final LinkInfo class for your url


#define MAX_LINKERS 3000

// if a linker is a "title rec not found" or log spam, then we get another
// linker's titleRec. churn through up to these many titleRecs in an attempt
// to get MAX_LINKERS good titlerecs before giving up.
//#define MAX_DOCIDS_TO_SAMPLE 25000
// on news.google.com, 22393 of the 25000 are link spam, and we only end
// up getting 508 good inlinks, so rais from 25000 to 50000
//#define MAX_DOCIDS_TO_SAMPLE 50000
// try a ton of lookups so we can ditch xfactor and keep posdb key as
// simple as possible. just make sure we recycle link info a lot!
#define MAX_DOCIDS_TO_SAMPLE 1000000

// go down from 300 to 100 so XmlDoc::getRecommendLinksBuf() can launch
// like 5 msg25s and have no fear of having >500 msg20 requests outstanding
// which clogs things up
// crap, no, on gk144 we got 128 hosts now, so put back to 300...
// if we have less hosts then limit this proportionately in Linkdb.cpp
#define	MAX_MSG20_OUTSTANDING 300

#define MAX_NOTE_BUF_LEN 20000

#define MSG25_MAX_REQUEST_SIZE (MAX_URL_LEN+MAX_COLL_LEN+64)


class Msg25 {

 public:

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . this sets Msg25::m_siteRootQuality and Msg25::m_linkInfo
	// . "url/coll" should NOT be on stack in case weBlock
	// . if "reallyGetLinkInfo" is false we don't actually try to fetch
	//   any link text and return true right away, really saves a bunch
	//   of disk seeks when spidering small collections that don't need
	//   link text/info indexing/analysis
	bool getLinkInfo2 (char      *site,
			   char      *url,
			   bool       isSiteLinkInfo,
			   int32_t    ip,
			   int64_t    docId,
			   collnum_t  collnum,
			   char      *qbuf,
			   int32_t    qbufSize,
			   void      *state,
			   void     (*callback)(void *state),
			   bool       isInjecting,
			   bool       printDebugMsgs, // into "Msg25::m_pbuf"
			   bool       printInXml,
			   int32_t    siteNumInlinks,
			   LinkInfo  *oldLinkInfo,
			   int32_t    niceness,
			   bool       doLinkSpamCheck,
			   bool       oneVotePerIpDom,
			   bool       canBeCancelled,
			   int32_t    lastUpdateTime,
			   bool       onlyNeedGoodInlinks,
			   bool       getLinkerTitles, //= false,
			   // if an inlinking document has an outlink
			   // of one of these hashes then we set
			   // Msg20Reply::m_hadLinkToOurDomOrHost.
			   // it is used to remove an inlinker to a related
			   // docid, which also links to our main seo url
			   // being processed. so we do not recommend
			   // such links since they already link to a page
			   // on your domain or hostname. set BOTH to zero
			   // to not perform this algo in handleRequest20()'s
			   // call to XmlDoc::getMsg20Reply().
			   int32_t     ourHostHash32,
			   int32_t     ourDomHash32,
			   SafeBuf    *myLinkInfoBuf);
	Msg25();
	~Msg25();
	void reset();

	// a new parm referencing the request we got over the network
	Msg25Request * m_req25;

	Msg20Reply *getLoser(Msg20Reply *r, Msg20Reply *p);
	const char *isDup   (Msg20Reply *r, Msg20Reply *p);

	bool addNote ( const char *note, int32_t noteLen, int64_t docId );

	// m_linkInfo ptr references into here. provided by caller.
	SafeBuf *m_linkInfoBuf;

	SafeBuf m_realBuf;

	// private:
	// these need to be public for wrappers to call:
	bool gotTermFreq(bool msg42Called);
	bool getRootTitleRec();
	bool gotRootTitleRec();
	bool gotDocId();
	bool gotRootLinkText();
	bool gotRootLinkText2();
	bool getLinkingDocIds();
	bool gotList();
	bool gotClusterRecs();
	bool sendRequests();
	bool gotLinkText(class Msg20Request *req);
	bool gotMsg25Reply();
	bool doReadLoop();

	// input vars
	char *m_url;
	char *m_site;

	int32_t m_ourHostHash32;
	int32_t m_ourDomHash32;

	int32_t m_round;
	uint64_t m_linkHash64;
	key224_t m_nextKey;

	bool       m_retried;
	bool       m_prependWWW;
	bool       m_onlyNeedGoodInlinks;
	bool       m_getLinkerTitles;
	int64_t  m_docId;
	collnum_t m_collnum;
	void      *m_state;
	void     (* m_callback) ( void *state );

	int32_t m_siteNumInlinks;
	int32_t m_mode;
	bool m_printInXml;


	// url info
	int32_t m_ip;
	int32_t m_top;
	int32_t m_midDomHash;

	bool m_gettingList;

	// . we now use Msg2 since it has "restrictIndexdb" support to limit
	//   indexdb searches to just the root file to decrease disk seeks
	Msg5 m_msg5;
	RdbList m_list;

	Inlink *m_k;

	// for getting the root title rec so we can share its pwids
	Msg22 m_msg22;

	int32_t      m_maxNumLinkers;

	// should we free the m_replyPtrs on destruction? default=true
	bool         m_ownReplies;

	// Now we just save the replies we get back from Msg20::getSummary()
	// We point to them with a LinkTextReply, which is just a pointer
	// and some access functions.
	Msg20Reply  *m_replyPtrs[MAX_LINKERS];
	int32_t      m_replySizes[MAX_LINKERS];
	int32_t      m_numReplyPtrs;

	Msg20        m_msg20s        [MAX_MSG20_OUTSTANDING];
	Msg20Request m_msg20Requests [MAX_MSG20_OUTSTANDING];
	char         m_inUse         [MAX_MSG20_OUTSTANDING];
	// for "fake" replies
	Msg20Reply   m_msg20Replies  [MAX_MSG20_OUTSTANDING];

	int32_t      m_numDocIds;
	int32_t      m_cblocks;
	int32_t      m_uniqueIps;

	int32_t      m_minRecSizes;

	// Msg20 is for getting the LinkInfo class from this same url's
	// titleRec from another (usually much larger) gigablast cluster/netwrk
	Msg20        m_msg20;

	// how many msg20s have we sent/recvd?
	int32_t      m_numRequests;
	int32_t      m_numReplies;

	int32_t      m_linkSpamOut;

	// have we had an error for any transaction?
	int32_t      m_errno;

	SafeBuf      m_tmp;
	SafeBuf     *m_pbuf; // will point to m_tmp if m_printDebugMsgs

	// copied from CollectionRec
	bool         m_oneVotePerIpDom;
	bool         m_doLinkSpamCheck;
	bool         m_isInjecting;
	char         m_canBeCancelled;
	int32_t      m_lastUpdateTime;

	Multicast m_mcast;

	int32_t      m_good;
	int32_t      m_errors;
	int32_t      m_noText;
	int32_t      m_reciprocal;

	bool         m_spideringEnabled;

	int32_t      m_dupCount;
	int32_t      m_vectorDups;
	int32_t      m_spamLinks;
	int32_t      m_niceness;
	int32_t      m_numFromSameIp;
	int32_t      m_sameMidDomain;

	// stats for allow some link spam inlinks to vote
	int32_t      m_spamCount;
	int32_t      m_spamWeight;
	int32_t      m_maxSpam;

	char m_siteQuality;
	int32_t m_siteNumFreshInlinks;

	// this is used for the linkdb list
	HashTableX   m_ipTable;
	HashTableX   m_fullIpTable;
	HashTableX   m_firstIpTable;

	// this is for deduping docids because we now combine the linkdb
	// list of docids with the old inlinks in the old link info
	//HashTableT <int64_t, char> m_docIdTable;
	HashTableX m_docIdTable;

	// special counts
	int32_t      m_ipDupsLinkdb;
	int32_t      m_docIdDupsLinkdb;
	int32_t      m_linkSpamLinkdb;
	int32_t      m_ipDups;

	uint32_t     m_groupId;
	int64_t      m_probDocId;

	LinkInfo    *m_oldLinkInfo;

	char         m_buf[MAX_NOTE_BUF_LEN];
	char        *m_bufPtr;
	char        *m_bufEnd;
	HashTableX   m_table;

	char         m_request[MSG25_MAX_REQUEST_SIZE];
	int32_t      m_requestSize;

	HashTableX   m_adBanTable;

	// for setting <absScore2> or determining if a search results
	// inlinkers also have the query terms. buzz.
	char        *m_qbuf;
	int32_t      m_qbufSize;
};


#endif
