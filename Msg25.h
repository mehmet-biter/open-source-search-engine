#ifndef MSG25_H_
#define MSG25_H_

#include "types.h"
#include "SafeBuf.h"

class LinkInfo;
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


#endif
