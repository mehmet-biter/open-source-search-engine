#ifndef GB_PAGERESULTS_H
#define GB_PAGERESULTS_H

#include "HttpRequest.h"
#include "SafeBuf.h"
#include "Msg40.h"
#include "SearchInput.h"
#include "Speller.h" // MAX_FRAG_SIZE

// height of each search result div in the widget
#define RESULT_HEIGHT 120
// other widget parms
#define SERP_SPACER 1
#define PADDING 8
#define SCROLLBAR_WIDTH 20


class XmlDoc;
class TcpSocket;

class State0 {
public:
	State0();

	// store results page in this safebuf
	SafeBuf      m_sb;

	bool         m_header;

	collnum_t    m_collnum;
	SearchInput  m_si;
	Msg40        m_msg40;
	TcpSocket   *m_socket;
	int64_t    m_startTime;
	bool         m_gotResults;
	int32_t         m_errno;
        int32_t         m_numDocIds;
	int64_t    m_took; // how long it took to get the results
	HttpRequest  m_hr;
	SafeBuf m_qesb;

	XmlDoc *m_xd;
	int64_t m_socketStartTimeHack;

	lang_t m_primaryQueryLanguage;
};


#endif // GB_PAGERESULTS_H
