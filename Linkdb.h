// Gigablast, Inc.  Copyright April 2007

// Linkdb - stores link information

// . Format of a 28-byte key in linkdb
// . used by Msg25::getPageLinkInfo()
// .
// . HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH H = sitehash32 of linkEE
// . pppppppp pppppppp pppppppp pppppppp p = linkEEHash, q = ~linkerSiteRank
// . pppppppp pppppppS qqqqqqqq cccccccc c = lower ip byte, S = isLinkSpam?
// . IIIIIIII IIIIIIII IIIIIIII dddddddd I = upper 3 bytes of ip
// . dddddddd dddddddd dddddddd dddddd00 d = linkerdocid,h = half bit,Z =delbit
// . mmmmmmmm mmmmmm0N 00000000 000000ss N = 1 if it was added to existing page
// . ssssssss ssssssss ssssssss sssssshZ s = sitehash32 of linker
//   m = discovery date in days since jan 1
//   
// NOTE: the "c" bits were the hopcount of the inlinker, but we changed
// them to the lower ip byte so steve can show the # of unique ips linking
// to your page or site.
//
// The ip is it network order (the top 24 bit that is. The low 8 bits are a separate field)

#ifndef GB_LINKDB_H
#define GB_LINKDB_H

#define LDBKS sizeof(key224_t)

#define LDB_MAXURLHASH  0x00007fffffffffffLL

// The date in the records are stored as days-since-2006. That means that when
// we use 14 bits for it we won't get a date overflow until 2006 + 2^14/265 = 2050
//
//                   year 2010
#define LINKDBEPOCH (1325376000-365*86400*4)

#include "Rdb.h"
#include "Titledb.h"
#include "SafeBuf.h"
#include "Sanity.h"

class Msg20Reply;
class UdpSlot;
class Multicast;
class LinkInfo;


// . returns false if blocked, true otherwise
// . sets errno on error
// . your req->m_callback will be called with the Msg25Reply
bool getLinkInfo ( SafeBuf *reqBuf , // store msg25 request in here
		   Multicast *mcast , // use this to send msg 0x25 request
		   const char      *site ,
		   const char      *url  ,
		   bool       isSiteLinkInfo ,
		   int32_t       ip                  ,
		   int64_t  docId               ,
		   collnum_t collnum ,
		   void      *state               ,
		   void (* callback)(void *state) ,
		   bool       isInjecting         ,
		   SafeBuf   *pbuf                ,
		   bool printInXml ,
		   int32_t       siteNumInlinks      ,
		   const LinkInfo  *oldLinkInfo         ,
		   int32_t       niceness            ,
		   bool       doLinkSpamCheck     ,
		   bool       oneVotePerIpDom     ,
		   bool       canBeCancelled      ,
		   int32_t       lastUpdateTime      ,
		   bool       onlyNeedGoodInlinks  ,
		   bool       getLinkerTitles , //= false ,
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
		   int32_t       ourHostHash32 ,
		   int32_t       ourDomHash32 ,
		   SafeBuf *myLinkInfoBuf );



int32_t getSiteRank ( int32_t sni ) ;

class Linkdb {
public:
	void reset();

	bool init();
	bool init2(int32_t treeMem);

	Rdb *getRdb() { return &m_rdb; }

	// this makes a "url" key
	static key224_t makeKey_uk ( uint32_t  linkeeSiteHash32 ,
			      uint64_t  linkeeUrlHash64  ,
			      bool      isLinkSpam     ,
			      unsigned char linkerSiteRank , // 0-15 i guess
			      uint32_t  linkerIp       ,
			      int64_t linkerDocId    ,
			      uint32_t      discoveryDate  ,
			      uint32_t      lostDate       ,
			      bool      newAddToOldPage   ,
			      uint32_t linkerSiteHash32 ,
			      bool      isDelete       );
	

	static key224_t makeStartKey_uk ( uint32_t linkeeSiteHash32 ,
				   uint64_t linkeeUrlHash64  = 0LL ) {
		return makeKey_uk ( linkeeSiteHash32,
				    linkeeUrlHash64,
				    false, // linkspam?
				    255, // 15, // ~siterank
				    0, // ip
				    0, // docid
				    0, //discovery date
				    0, // lostdate
				    false, // newaddtopage
				    0, // linkersitehash
				    true); // is delete?
	}

	static key224_t makeEndKey_uk ( uint32_t linkeeSiteHash32 ,
				 uint64_t linkeeUrlHash64  = 
				 0xffffffffffffffffLL ) {
		return makeKey_uk ( linkeeSiteHash32,
				    linkeeUrlHash64,
				    true, // linkspam?
				    0, // ~siterank
				    0xffffffff, // ip
				    MAX_DOCID, // docid
				    0xffffffff, //discovery date
				    0xffffffff, // lostdate
				    true, // newaddtopage
				    0xffffffff, // linkersitehash
				    false); // is delete?
	}

	//
	// accessors for "url" keys in linkdb
	//

	static uint32_t getLinkeeSiteHash32_uk(const key224_t *key) {
		return (key->n3) >> 32;
	}

	static uint64_t getLinkeeUrlHash64_uk(const key224_t *key) {
		uint64_t h = key->n3;
		h &= 0x00000000ffffffffLL;
		h <<= 15;
		h |= key->n2 >> 49;
		return h;
	}

	static bool isLinkSpam_uk(const key224_t *key) {
		if ((key->n2) & 0x1000000000000LL) return true; 
		return false;
	}

	static unsigned char getLinkerSiteRank_uk(const key224_t *k) {
		unsigned char rank = (k->n2 >> 40) & 0xff;
		// complement it back
		rank = (unsigned char)~rank;//LDB_MAXSITERANK - rank;
		return rank;
	}

	static int32_t getLinkerIp_uk(const key224_t *k) {
		uint32_t ip ;
		// the most significant part of the ip is the lower byte!!!
		ip = (uint32_t)((k->n2>>8)&0x00ffffff);
		ip |= ((k->n2>>8) & 0xff000000);
		return ip;
	}

	static void setIp32_uk ( void *k , uint32_t ip ) {
		char *ips = (char *)&ip;
		char *ks = (char *)k;
		ks[16] = ips[3];
		ks[15] = ips[2];
		ks[14] = ips[1];
		ks[13] = ips[0];
	}


	// we are missing the lower byte, it will be zero
	static int32_t getLinkerIp24_uk(const key224_t *k) {
		return (int32_t)((k->n2>>8)&0x00ffffff); 
	}

	static int64_t getLinkerDocId_uk(const key224_t *k) {
		uint64_t d = k->n2 & 0xff;
		d <<= 30;
		d |= k->n1 >>34;
		return d;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	static int32_t getDiscoveryDate_uk(const void *k) {
		uint32_t date = ((const key224_t *)k)->n1 >> 18;
		date &= 0x00003fff;
		// if 0 return that
		if ( date == 0 ) return 0;
		// multiply by seconds in days then
		date *= 86400;
		// add OUR epoch
		date += LINKDBEPOCH;
		// and use that
		return date;
	}

	// . in days since jan 1, 2012 utc
	// . timestamp of jan 1, 2012 utc is 1325376000
	static void setDiscoveryDate_uk ( void *k , int32_t date ) {
		// subtract jan 1 2012
		date -= LINKDBEPOCH;
		// convert into days
		date /= 86400;
		// sanity
		if ( date > 0x3fff || date < 0 ) { gbshutdownAbort(true); }
		// clear old bits
		((key224_t *)k)->n1 &= 0xffffffff03ffffLL;
		// scale us into it
		((key224_t *)k)->n1 |= ((uint64_t)date) << 18;
	}

	static int32_t getLostDate_uk(const void *k) {
		uint32_t date = ((const key224_t *)k)->n1 >> 2;
		date &= 0x00003fff;
		// if 0 return that
		if ( date == 0 ) return 0;
		// multiply by seconds in days then
		date *= 86400;
		// add OUR epoch
		date += LINKDBEPOCH;
		// and use that
		return date;
	}

	static uint32_t getLinkerSiteHash32_uk(const void *k) {
		uint32_t sh32 = ((const key224_t *)k)->n1 & 0x00000003;
		sh32 <<= 30;
		sh32 |= ((const key224_t *)k)->n0 >> 2;
		return sh32;
	}

	static void printKey(const char *k);

private:
	Rdb m_rdb;
};

extern class Linkdb g_linkdb;
extern class Linkdb g_linkdb2;


// . takes a bunch of Msg20Replies and makes a serialized buffer, LinkInfo
// . LinkInfo's buffer consists of a bunch of serialized "Inlinks" as defined
//   below
// . THINK OF THIS CLASS as a Msg25 reply ("Msg25Reply") class

class Xml;

// how big can the rss item we store in the Inlink::ptr_rssItem be?
#define MAX_RSSITEM_SIZE 30000

class LinkInfo {

 public:

	int32_t   getStoredSize  ( ) const { return m_lisize; }
	int32_t   getSize        ( ) const { return m_lisize; }
	time_t getLastUpdated ( ) const { return (time_t)m_lastUpdated; }

	int32_t   getNumLinkTexts() const { return m_numStoredInlinks; }
	int32_t   getNumGoodInlinks() const { return m_numGoodInlinks; }

	class Inlink *getNextInlink ( class Inlink *k ) ;

	bool getItemXml ( Xml *xml ) ;

	bool hasLinkText ( );

	// for PageTitledb
	bool print(class SafeBuf *sb, const char *coll );

	bool hasRSSItem();

	// a small header, followed by the buf of "Inlinks", m_buf[]
	char       m_version;
	// we only keep usually no more than 10 or so internal guys, so this
	// can be a single byte
	char       m_numInlinksInternal;
	char       m_reserved1; // was m_siteRootQuality
	char       m_reserved2;
	// includes Inlinks in m_buf[] below
	int32_t       m_lisize;
	// this is really a time_t but that changes and this can't change!
	int32_t       m_lastUpdated;
	// this is precisely how many inlinks we stored in m_buf[] below
	int32_t       m_numStoredInlinks;//m_numTotalInlinks;
	// . only valid if titleRec version >= 119, otherwise its always 0
	// . this count includes internal as well as external links, i.e. just
	//   the total inlinks we got, counting at most one inlink per page. 
	//   it is not very useful i guess, but steve wants it.
	int32_t       m_totalInlinkingDocIds;//reserved3;
	// . how many inlinks did we have that were "good"?
	// . this is typically less than the # of Inlinks stored in m_buf below
	//   because it does not include internal cblock inlinks
	int32_t       m_numGoodInlinks;
	// . # of c blocks linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own intenral cblock
	int32_t       m_numUniqueCBlocks;//m_pagePop;
	// . # of IPs linking to this page/site
	// . only valid if titlerecversion >= 119
	// . includes your own internal ip
	int32_t       m_numUniqueIps;//numInlinksFresh; // was m_reserved3;

	// serialize "Inlinks" into this buffer, m_buf[]
	char   m_buf[0];
} __attribute__((packed, aligned(4)));


#define MAXINLINKSTRINGBUFSIZE 2048

class Inlink {

 public:

	Inlink() { reset(); }

	// zero ourselves out
	void reset() ;

	void set ( const Msg20Reply *reply );

	// set ourselves from a serialized older-versioned Inlink
	void set2 ( const Inlink *old );

	bool setXmlFromRSS      ( Xml *xml ) ;

	// . set a Msg20Reply from ourselves
	// . Msg25 uses this to recycle old inlinks that are now gone
	// . allows us to preserve ptr_rssInfo, etc.
	void setMsg20Reply ( Msg20Reply *r ) ;

	int32_t getStoredSize ( ) const;

	// . return ptr to the buffer we serialize into
	// . return NULL and set g_errno on error
	char *serialize ( int32_t *retSize     ,
			  char *userBuf     ,
			  int32_t  userBufSize ,
			  bool  makePtrsRefNewBuf ) const;

	int32_t       m_ip                  ; //0
	int64_t  m_docId               ; // 4
	int32_t       m_firstSpidered       ; // 12
	int32_t       m_lastSpidered        ; // 16
	int32_t	   m_nextSpiderDate	 ; // 20
	// like in the titleRec, the lower 2 bits of the datedbDate have
	// special meaning. 
	// 0x00 --> datedb date extracted from content (pubdate)
	// 0x01 --> datedb date based on estimated "modified" time (moddate)
	// 0x10 --> datedb date is when same-site root was estimated to have
	//          first added that url as an outlink (discoverdate) (TODO)
	int32_t       m_datedbDate          ; // 24
	// this date is used as the discovery date for purposes of computing
	// LinkInfo::m_numInlinksFresh
	int32_t       m_firstIndexedDate    ; // 28
	//int32_t       m_baseScore           ;
	int32_t       m_pageNumInlinks      ; // 32
	int32_t       m_siteNumInlinks      ; // 36
	// record the word position we hashed this link text with
	// so we can match it to the DocIdScoringInfo stuff
	int32_t       m_wordPosStart;//reservedc;//pagePop        // 40
	int32_t       m_firstIp; // 44

	// . int32_t     m_reserved1           ;
	// . how many strings do we have?
	// . makes it easy to add new strings later
	uint16_t   m_reserved_NumStrings          ; // 48
	// . and were our first string ptrs starts
	// . allows us to set ourselves from an "old" Inlink 
	uint16_t   m_reserved_FirstStrPtrOffset   ; // 50

	uint16_t   m_numOutlinks         ; // 52
	// i guess no need to store this stuff if we are storing the url
	// in ptr_urlBuf below. we can call Url::set() then Url::getHostHash()
	// NO, because the site is now only contained in the TagRec now and
	// we compute the site in SiteGetter.cpp, so it is more complicated!!!
	// we get the tag rec of each outlink, and get the site from that
	// and hash that and store it here

	// we got a 2 byte padding before this PADPADPADPADP
	int16_t    m_pad0;

	int32_t       m_siteHash            ; // www.hompages.com/~fred/ // 56

	// single bit flags
	uint16_t   m_isPermalink      : 1 ; // 60
	uint16_t   m_outlinkInContent : 1 ;
	uint16_t   m_outlinkInComment : 1 ;
	uint16_t   m_isReserved       : 1 ; // was u-n-i-c-o-d-e- bit
	uint16_t   m_isLinkSpam       : 1 ;
	// if we imported it from the old LinkInfo. helps us preserve rssInfo,
	// hopcounts, etc.
	uint16_t   m_recycled         : 1 ;
	uint16_t   m_reserved4        : 1 ;
	uint16_t   m_reserved5        : 1 ;
	uint16_t   m_reserved6        : 1 ;
	uint16_t   m_reserved7        : 1 ;
	uint16_t   m_reserved8        : 1 ;
	uint16_t   m_reserved9        : 1 ;
	uint16_t   m_reserveda        : 1 ;
	uint16_t   m_reservedb        : 1 ;

	uint16_t   m_country             ; // 62
	uint8_t    m_language            ; // 64
	char       m_siteRank; // 65
	char       m_hopcount            ;  // 66
	char       m_linkTextScoreWeight ; // 0-100% (was m_inlinkWeight) //67

	char *getUrl ( ) { 
		if ( size_urlBuf == 0 ) return NULL;
		return m_buf ;//+ off_urlBuf; 
	}
	char *getLinkText ( ) { 
		if ( size_linkText == 0 ) return NULL;
		//return m_buf + off_linkText; 
		return m_buf + 
			size_urlBuf;
	}
	char *getSurroundingText ( ) { 
		if ( size_surroundingText == 0 ) return NULL;
		//return m_buf + off_surroundingText; 
		return m_buf + 
			size_urlBuf +
			size_linkText;
	}
	char *getRSSItem ( ) { 
		if ( size_rssItem == 0 ) return NULL;
		//return m_buf + off_rssItem; 
		return m_buf + 
			size_urlBuf +
			size_linkText + 
			size_surroundingText;
	}
	const char *getCategories ( ) {
		if ( size_categories == 0 ) return NULL;
		//return m_buf + off_categories; 
		return m_buf + 
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem;
	}
	char *getGigabitQuery ( ) { 
		if ( size_gigabitQuery == 0 ) return NULL;
		//return m_buf + off_gigabitQuery; 
		return m_buf + 
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem +
			size_categories;
	}
	char *getTemplateVector ( ) { 
		if ( size_templateVector == 0 ) return NULL;
		//return m_buf + off_templateVector; 
		return m_buf + 
			size_urlBuf +
			size_linkText + 
			size_surroundingText +
			size_rssItem +
			size_categories +
			size_gigabitQuery;
	}
		

	//
	// add new non-strings right above this line
	//

	// . the url, link text and neighborhoods are stored in here
	// . no need to store vector for voting deduping in here because
	//   that use MsgE's Msg20Replies directly
	// . this is just stuff we want in the title rec
	int32_t    off_urlBuf            ; // 68
	int32_t    off_linkText          ;
	int32_t    off_surroundingText   ; // neighborhoods
	// . this is the rss item that links to us
	// . if calling Msg25::getLinkInfo() with getLinkerTitles set to
	//   true then this is the title!
	int32_t    off_rssItem           ;
	// . zakbot and the turk categorize site roots, and kids inherit
	//   the categories from their parent inlinkers
	// . we can't really use tagdb cuz that operates on subdirectories
	//   which may not be upheld for some sites. (like cnn.com!, the 
	//   stories are not proper subdirectories...)
	// . so inherit the category from our inlinkers. "sports", "world", ...
	// . comma-separated (in ascii)
	int32_t    off_categories        ;
	// . augments our own gigabits vector, used for finding related docs
	// . used along with the template vector for deduping pgs at index time
	// . now we used for finding similar docs AND categorizing
	// . comma-separated
	// . each gigabit has a count in []'s. score in body x1, title x5,
	//   and inlink text x5. i.e. "News[10],blue devils[5],... 
	// . always in UTF-8
	int32_t    off_gigabitQuery      ;
	// . the html tag vector. 
	// . used for deduping voters (anti-spam tech)
	// . used along with the gigabit vector for deduping pgs at index time
	// . now we used for finding similar docs and for categorizing (spam)
	int32_t    off_templateVector    ;

	//
	// add new strings right above this line
	//

	int32_t       size_urlBuf           ;
	int32_t       size_linkText         ;
	int32_t       size_surroundingText  ;
	int32_t       size_rssItem          ;
	int32_t       size_categories       ;
	int32_t       size_gigabitQuery     ;
	int32_t       size_templateVector   ;


	char       m_buf[MAXINLINKSTRINGBUFSIZE] ;
} __attribute__((packed, aligned(4)));


////////
//
// LINKS CLASS
//
////////

//typedef int16_t linkflags_t;
typedef int32_t linkflags_t;

// all the links (urls), separated by \0's, are put into a buf of this size
#define LINK_BUF_SIZE (100*1024)

// Link Flags
#define LF_SAMEHOST      0x0001 // same hostname
#define LF_SAMEDOM       0x0002 // same domain
#define LF_SITEROOT      0x0004 // for blogrolls
#define LF_SAMESITE      0x0008 // only get offsite outlink info in Msg20.cpp
#define LF_OLDLINK       0x0010 // set this if it was on the pg last spider tim
#define LF_RSS           0x0020 // is it from an rss <link href=> tag?
#define LF_PERMALINK     0x0040 // a probable permalink? of permalink format?
#define LF_SUBDIR        0x0080 // is the outlink in a subdir of parent?
#define LF_AHREFTAG      0x0100 // an <a href=> outlink
#define LF_LINKTAG       0x0200 // a <link> outlink
#define LF_FBTAG         0x0400 // a feed burner original outlink
#define LF_SELFLINK      0x0800 // links to self
#define LF_SELFPERMALINK 0x1000 // has "permalink" "link text" or attribute
#define LF_STRONGPERM    0x2000 // is permalink of /yyyy/mm/dd/ format
#define LF_EDUTLD        0x4000
#define LF_GOVTLD        0x8000

#define LF_NOFOLLOW     0x10000

bool isPermalink (
		   class Links *links       ,
		   class Url   *u           ,
		   char         contentType ,
		   class LinkInfo    *linkInfo    ,
		   bool         isRSS       ,
		   const char       **note        = NULL  ,
		   char        *pathOverride= NULL  ,
		   bool         ignoreCgi   = false ,
		   linkflags_t  *extraFlags = NULL  ) ;

class Links {

public:
	Links();
	~Links();
	void reset();

	// call this before calling hash() and write()
	bool set ( bool useRelNoFollow ,
		   Xml *xml, 
		   Url *parentUrl ,
		   // use NULL for this if you do not have a baseUrl
		   Url *baseUrl , 
		   int32_t version,
		   bool  parentIsPermalink , // = false ,
		   const Links *oldLinks         , // for LF_OLDLINKS flag
		   // this is used by Msg13.cpp to quickly get ptrs
		   // to the links in the document, no normalization!
		   bool doQuickSet = false );

	// set from a simple text buffer
	bool set ( const char *buf ) ;

	bool print ( SafeBuf *sb ) ;

	// Link in ascii text
	bool addLink(const char *link, int32_t linkLen, int32_t nodeNum, bool setLinkHashes,
		     int32_t titleRecVersion, bool isRSS ,
		     int32_t tagId , linkflags_t flagsArg );

	// . link spam functions. used by linkspam.cpp's setLinkSpam().
	// . also used by Linkdb.cpp to create a linkdb list to add to rdb
	// . we do not add outlinks to linkdb if they are "link spam"
	void setAllSpamBits ( const char *note ) { m_spamNote = note; }
	void setSpamBit  ( const char *note , int32_t i ) { m_spamNotes[i] = note; }
	void setSpamBits ( const char *note , int32_t i ) {
		for (int32_t j=i ; j<m_numLinks ; j++) m_spamNotes[j] = note;}
	// . m_spamNote is set if it is ALL link spam... set above
	// . internal outlinks are never considered link spam since we "dedup"
	//   them by ip in Msg25/LinkInfo::merge() anyway
	bool isLinkSpam(int32_t i) const {
		if ( isInternalDom(i) ) return false; 
		if ( m_spamNote       ) return true; 
		return m_spamNotes[i]; 
	}
	const char *getSpamNote(int32_t i) const {
	        if ( isInternalDom(i) ) return "good";
		if ( m_spamNote       ) return m_spamNote;
		if ( m_spamNotes[i]   ) return m_spamNotes[i];
		return "good";
	}

	// for spidering links purposes, we consider "internal" to be same 
	// hostname
	bool isInternalHost(int32_t i) const { return (m_linkFlags[i] & LF_SAMEHOST); }

	// we do not subjugate same domain links to link spam detection in
	// linkspam.cpp::setLinkSpam()
	bool isInternalDom(int32_t i) const { return (m_linkFlags[i] & LF_SAMEDOM); }

	bool isOld(int32_t i) const { return m_linkFlags[i] & LF_OLDLINK; }

	// . returns false and sets g_errno on error
	// . remove links from our m_linkPtrs[] if they are in "old"
	bool flagOldLinks ( const class Links *old ) ;

	// . does link #n have link text that has at least 1 alnum char in it?
	// . used for scoring link: terms to make link-text adds more efficient
	bool hasLinkText ( int32_t n, int32_t version );

	// . returns false on error and sets errno
	// . get our outgoing link text for this url
	// . store it into "buf"
	int32_t getLinkText ( const char  *linkee ,
			   bool   getSiteLinkInfo ,
			   char  *buf       ,
			   int32_t   maxBufLen ,
			   char **itemPtr   ,
			   int32_t  *itemLen   ,
			   int32_t   *retNode1 ,
			   int32_t   *retLinkNum);

	int32_t getLinkText2 ( int32_t i,
			   char  *buf       ,
			   int32_t   maxBufLen ,
			   char **itemPtr   ,
			   int32_t  *itemLen   ,
			   int32_t   *retNode1 );

	// returns list of \0 terminated, normalized links
	char       *getLinkBuf()       { return m_allocBuf; }
	const char *getLinkBuf() const { return m_allocBuf; }
	int32_t getLinkBufLen() const {
		if ( m_allocBuf ) return m_bufPtr - m_allocBuf;
		return 0;
	}
	//uint32_t *getLinkHashes () { return m_linkHashes; }
	int32_t getNumLinks() const { return m_numLinks; }

	int32_t getLinkLen(int32_t i) const { return m_linkLens[i]; }
	char       *getLinkPtr(int32_t i)	{ return m_linkPtrs  [i]; }
	const char *getLinkPtr(int32_t i) const { return m_linkPtrs  [i]; }
	uint32_t    getLinkHash32 ( int32_t i ) const { return (uint32_t)m_linkHashes[i]; }
	uint64_t    getLinkHash64(int32_t i) const { return m_linkHashes[i]; }
	uint64_t    getHostHash64(int32_t i) const { return m_hostHashes[i]; }
	int32_t     getDomHash32(int32_t i) const { return m_domHashes[i]; }
	int32_t     getNodeNum(int32_t i) const { return m_linkNodes[i];  }
	bool        hasRelNoFollow() const { return m_hasRelNoFollow; }

	int32_t findLinkNum(char* url, int32_t urlLen);

	int32_t getMemUsed() const { return m_allocSize; }

	bool hasSelfPermalink() const { return m_hasSelfPermalink; }
	bool hasRSSOutlink() const { return m_hasRSSOutlink; }
	bool hasSubdirOutlink() const { return m_hasSubdirOutlink; }

	// private:

	Xml   *m_xml;
	Url   *m_baseUrl;
	Url   *m_parentUrl;
	bool   m_parentIsPermalink;

	// . we store all links in this buf
	// . each link ends in a \0
	// . convenient for passing to Msg10
	// . each link is in complete http:// format with base url, etc.
	char   *m_buf;
	// pointer to the end of the buffer
	char  *m_bufPtr;

	// this is non-NULL if all outlinks are considered link spam, 
	// otherwise, individual outlinks will have their m_spamNotes[i] be
	// non-NULL, and point to the string that describes why they are 
	// link spam.
	const char  *m_spamNote;

	char          **m_linkPtrs;//   [MAX_LINKS];
	int32_t           *m_linkLens;//   [MAX_LINKS];
	int32_t           *m_linkNodes;//  [MAX_LINKS];
	uint64_t       *m_linkHashes;// [MAX_LINKS];
	uint64_t       *m_hostHashes;// [MAX_LINKS];
	int32_t           *m_domHashes;// [MAX_LINKS];
	linkflags_t    *m_linkFlags;
	const char          **m_spamNotes;

	bool m_doQuickSet;

	// do we have an rss link? i.e. are we an RSS feed
	bool           m_hasRSS;
	bool           m_isFeedBurner;

	char          *m_linkBuf;
	int32_t           m_allocLinks;
	int32_t           m_numLinks;
	int32_t           m_numNodes;

	bool m_hasRelNoFollow;

	bool m_stripParams;
	
	uint32_t  m_allocSize;
	char          *m_allocBuf;

	bool  m_addSiteRootFlags;
	char *m_coll;

	bool  m_flagged;

	bool  m_hasSelfPermalink;
	bool  m_hasRSSOutlink;
	bool  m_hasSubdirOutlink;
	char *m_rssOutlinkPtr;
	int32_t  m_rssOutlinkLen;

	// . returns  0 if probably not a permalink
	// . returns  1 if probably is a permalink
	// . returns -1 if not enough information to make a decision
	char isPermalink( const char ** /*note*/ ) {
		return -1;
	}

	int32_t m_numOutlinksAdded;
};


#endif // GB_LINKDB_H
