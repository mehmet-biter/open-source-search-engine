#ifndef GB_PAGEINJECT_H
#define GB_PAGEINJECT_H
#include "XmlDoc.h"
#include "Parms.h"

// for getting list of injections currently being processed on this host
// for printing in the Spider Queue table in Spider.cpp
class XmlDoc *getInjectHead ( ) ;

void  handleRequest7 ( class UdpSlot *slot , int32_t netnice ) ;

bool sendPageInject ( class TcpSocket *s, class HttpRequest *hr );



void setInjectionRequestFromParms ( class TcpSocket *sock , 
				    class HttpRequest *hr ,
				    class CollectionRec *cr ,
				    class InjectionRequest *ir ) ;

class InjectionRequest {
 public:

	int32_t   m_injectDocIp;
	bool      m_spiderLinks;
	bool      m_shortReply;
	bool      m_newOnly;
	bool      m_skipContentHashCheck;
	bool      m_deleteUrl;
	bool      m_hasMime;
	int32_t   m_charset;
	int32_t   m_langId;
	collnum_t m_collnum; // more reliable than m_coll
	uint32_t  m_firstIndexed;
	uint32_t  m_lastSpidered;
	int32_t   m_indexCode;
	int32_t   m_httpStatus;

	char *ptr_url;
	char *ptr_redirUrl;
	char *ptr_contentTypeStr;
	char *ptr_content;

	int32_t size_url;
	int32_t size_redirUrl;
	int32_t size_contentTypeStr;
	int32_t size_content;

	// variable data begins here
};


class Msg7 {

public:

	//GigablastRequest m_gr;
	InjectionRequest m_injectionRequest;

	int64_t    m_startTime;

	int32_t m_replyIndexCode;
	int64_t m_replyDocId;

	char *m_sir;
	int32_t m_sirSize;

	bool       m_needsSet;
	XmlDoc    *m_xd;
	TcpSocket *m_socket;
	SafeBuf    m_sb;
	char m_round;
	HashTableX m_linkDedupTable;

	// referenced by InjectionRequest::ptr_content
	SafeBuf m_contentBuf;

	SafeBuf m_sbuf; // for holding entire titlerec for importing

	void *m_state;
	void (* m_callback )(void *state);

	Msg7 ();
	~Msg7 ();
	bool m_inUse;
	int32_t m_format;
	HttpRequest m_hr;

	class XmlDoc *m_stashxd;

	void reset();

	void gotUdpReply ( class UdpSlot *slot ) ;

	bool sendInjectionRequestToHost ( InjectionRequest *ir ,
					  void *state ,
					  void (* callback)(void *) );

};

#endif // GB_PAGEINJECT_H
