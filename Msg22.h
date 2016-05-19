// Matt Wells, copyright Jul 2001

// . get a TitleRec from url/coll or docId

#ifndef GB_MSG22_H
#define GB_MSG22_H

#include "Url.h"
#include "Multicast.h"

// m_url[0]!=0 if this is a url-based request and NOT docid-based
class Msg22Request {
public:
	int64_t m_docId;
	int32_t      m_niceness;
	int32_t      m_maxCacheAge;
	collnum_t m_collnum;
	unsigned char      m_justCheckTfndb  :1;
	unsigned char      m_getAvailDocIdOnly:1;
	unsigned char      m_addToCache      :1;
	unsigned char      m_inUse           :1;
	char      m_url[MAX_URL_LEN+1];

	Msg22Request();
	
	int32_t getSize () {
		return (m_url - (char *)&m_docId) + 1+gbstrlen(m_url); }
	int32_t getMinSize() {
		return (m_url - (char *)&m_docId) + 1; }
};

class Msg22 {

 public:
	Msg22();
	~Msg22();

	static bool registerHandler ( ) ;

	bool getAvailDocIdOnly ( class Msg22Request  *r              ,
				 int64_t preferredDocId ,
				 char *coll ,
				 void *state ,
				 void (* callback)(void *state) ,
				 int32_t niceness ) ;

	// . make sure you keep url/coll on your stack cuz we just point to it
	// . see the other getTitleRec() description below for more details
	// . use a maxCacheAge of 0 to avoid the cache
	bool getTitleRec ( class Msg22Request *r ,
			   char      *url     ,
			   int64_t  docId   ,
			   char      *coll    ,
			   char     **titleRecPtrPtr  ,
			   int32_t      *titleRecSizePtr ,
			   bool       justCheckTfndb ,
			   bool       getAvailDocIdOnly  ,
			   void      *state          , 
			   void     (* callback) (void *state ),
			   int32_t       niceness       ,
			   bool       addToCache     ,
			   int32_t       maxCacheAge    ,
			   int32_t       timeout );

	int64_t getAvailDocId ( ) { return m_availDocId; }

	// public so C wrappers can call
	void gotReply ( ) ;

	char **m_titleRecPtrPtr;
	int32_t  *m_titleRecSizePtr;

	void    (* m_callback ) (void *state);
	void     *m_state       ;

	bool      m_found;
	int64_t m_availDocId;
	// the error getting the title rec is stored here
	int32_t      m_errno;

	bool m_outstanding ;

	// for sending the Msg22
	Multicast m_mcast;

	class Msg22Request *m_r;
};

#endif // GB_MSG22_H
