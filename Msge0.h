// Gigablast Inc., copyright November 2007

#ifndef GB_MSGE0_H
#define GB_MSGE0_H

#define MAX_OUTSTANDING_MSGE0 20

#include "Linkdb.h"
#include "Tagdb.h"

class TagRec;

class Msge0 {

public:

	Msge0();
	~Msge0();
	void reset();

	bool getTagRecs ( const char        **urlPtrs      ,
			  linkflags_t  *urlFlags     ,
			  int32_t          numUrls      ,
			  bool          skipOldLinks ,
			  TagRec *baseTagRec ,
			  collnum_t  collnum,
			  int32_t          niceness     ,
			  void         *state        ,
			  void (*callback)(void *state) ) ;

	TagRec       *getTagRec(int32_t i)       { return m_tagRecPtrs[i]; }
	const TagRec *getTagRec(int32_t i) const { return m_tagRecPtrs[i]; }

	int32_t getErrno() const { return m_errno; }
	TagRec ***getTagRecPtrsPtr() { return &m_tagRecPtrs; } //XmlDoc needs this due to ptr-ptr idiocy

private:
	static void gotTagRecWrapper(void *state);
	bool launchRequests();
	bool sendMsg8a(int32_t slotIndex);
	bool doneSending(int32_t slotIndex);

	TagRec *allocateTagRec();

	collnum_t m_collnum;
	int32_t  m_niceness  ;

	const char **m_urlPtrs;
	linkflags_t *m_urlFlags;
	int32_t   m_numUrls;

	bool   m_skipOldLinks;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	int32_t  m_bufSize;

	int32_t   m_slabNum;
	char **m_slab;
	char  *m_slabPtr;
	char  *m_slabEnd;

	TagRec *m_baseTagRec;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	int32_t        *m_tagRecErrors;
	TagRec     **m_tagRecPtrs;
	int32_t        *m_numTags;

	int32_t  m_numRequests;
	int32_t  m_numReplies;
	int32_t  m_n;

	Url     m_urls        [ MAX_OUTSTANDING_MSGE0 ]; 
	int32_t    m_ns          [ MAX_OUTSTANDING_MSGE0 ]; 
	bool    m_used        [ MAX_OUTSTANDING_MSGE0 ]; 
	Msg8a   m_msg8as      [ MAX_OUTSTANDING_MSGE0 ]; //for getting tag bufs

	void     *m_state;
	void    (*m_callback)(void *state);

	// for errors
	int32_t      m_errno;
};

#endif // GB_MSGE0_H
