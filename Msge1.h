// Gigablast Inc., copyright November 2007

#ifndef GB_MSGE1_H
#define GB_MSGE1_H

#define MAX_OUTSTANDING_MSGE1 20

#include "MsgC.h"
#include "Linkdb.h"
#include "Tagdb.h"

class Msge1 {

public:

	Msge1();
	~Msge1();
	void reset();

	// . this gets the ip from "firstip" tag in "grv" array of TagRecs
	//   if it is there. otherwise, it will do an ip lookup.
	// . this will also consult the stored files on disk if this is the
	//   "test" collection to avoid any dns lookups whatsoever, unless
	//   that file is not present
	// . the purpose of this is to just get "firstips" for XmlDoc 
	//   to set the SpiderRequest::m_firstIp member which is used to
	//   determine which hostId is exclusively responsible for 
	//   doling/throttling these urls/requests out to other hosts to
	//   spider them. see Spider.h/.cpp for more info
	bool getFirstIps ( class TagRec **grv                   ,
			   const char **urlPtrs,
			   const linkflags_t *urlFlags,
			   int32_t          numUrls                ,
			   int32_t          niceness               ,
			   void         *state                  ,
			   void        (*callback)(void *state) ,
			   int32_t          nowGlobal              ,
			   bool          addTags                );

	int32_t getErrno() const { return m_errno; }
	int32_t **getIpBufPtr() { return &m_ipBuf; } //xdmlDoc needs this ptr-ptr

private:
	static void gotMsgCWrapper(void *state, int32_t ip);
	bool launchRequests(int32_t starti);
	bool sendMsgC(int32_t slotIndex, const char *host, int32_t hlen);
	void doneSending(int32_t slotIndex);
	bool addTag(int32_t slotIndex);
	bool doneAddingTag(int32_t slotIndex);

	int32_t  m_niceness  ;

	const char **m_urlPtrs;
	const linkflags_t *m_urlFlags;
	int32_t   m_numUrls;
	bool   m_addTags;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	int32_t  m_bufSize;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	int32_t        *m_ipBuf;
	int32_t        *m_ipErrors;

	int32_t  m_numRequests;
	int32_t  m_numReplies;
	int32_t  m_n;

	int32_t    m_ns          [ MAX_OUTSTANDING_MSGE1 ]; 
	bool    m_used        [ MAX_OUTSTANDING_MSGE1 ]; 
	MsgC    m_msgCs       [ MAX_OUTSTANDING_MSGE1 ]; // ips

	// vector of TagRec ptrs
	TagRec **m_grv;
	

	void     *m_state;
	void    (*m_callback)(void *state);

	int32_t m_nowGlobal;

	// for errors
	int32_t      m_errno;
};

#endif // GB_MSGE1_H
