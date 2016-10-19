#ifndef GIGABLAST_REQUEST_H_
#define GIGABLAST_REQUEST_H_

class HttpRequest;
class TcpSocket;

#include "Msg4.h"
#include "HttpRequest.h"

// generic gigablast request. for all apis offered.
class GigablastRequest {
 public:

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
	char  m_recycleContent;
};

#endif
