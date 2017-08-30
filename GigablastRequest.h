#ifndef GIGABLAST_REQUEST_H_
#define GIGABLAST_REQUEST_H_

class HttpRequest;
class TcpSocket;

#include "Msg4Out.h"
#include "HttpRequest.h"

// generic gigablast request. for all apis offered.
class GigablastRequest {
 public:
	GigablastRequest()
		: m_hr()
		, m_socket(NULL)
		, m_coll(NULL)
		, m_url(NULL)
		, m_docId(0)
		, m_strip(0)
		, m_includeHeader(false)
		, m_urlsBuf(NULL)
		, m_stripBox(false)
		, m_harvestLinks(false)
		, m_listBuf()
		, m_msg4()
		, m_query(NULL)
		, m_srn(0)
		, m_ern(0)
		, m_qlang(NULL)
		, m_forceDel(false)
		, m_recycleContent(false) {
	}

	//
	// make a copy of the http request because the original is
	// on the stack. AND the "char *" types below will reference into
	// this because they are listed as TYPE_CHARPTR in Parms.cpp.
	// that saves us memory as opposed to making them all SafeBufs.
	//
	HttpRequest m_hr;

	// ptr to socket to send reply back on
	TcpSocket *m_socket;

	// TYPE_CHARPTR
	char *m_coll;

	////////////
	//
	// /admin/inject parms
	//
	////////////
	// these all reference into m_hr or into the Parm::m_def string!
	char *m_url; // also for /get

	///////////
	//
	// /get parms (for getting cached web pages)
	//
	///////////
	int64_t m_docId;
	int32_t m_strip;
	bool m_includeHeader;

	///////////
	//
	// /admin/addurl parms
	//
	///////////
	char *m_urlsBuf;
	bool m_stripBox;
	bool  m_harvestLinks;
	SafeBuf m_listBuf;
	Msg4 m_msg4;

	/////////////
	//
	// /admin/reindex parms
	//
	////////////
	char *m_query;
	int32_t  m_srn;
	int32_t  m_ern;
	char *m_qlang;
	bool  m_forceDel;
	bool  m_recycleContent;
};

#endif
