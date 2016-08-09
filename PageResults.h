#ifndef GB_PAGERESULTS_H
#define GB_PAGERESULTS_H

#include "SafeBuf.h"
#include "Msg40.h"
#include "Msg0.h"
#include "Speller.h" // MAX_FRAG_SIZE

// height of each search result div in the widget
#define RESULT_HEIGHT 120
// other widget parms
#define SERP_SPACER 1
#define PADDING 8
#define SCROLLBAR_WIDTH 20


class State0 {
public:

	// store results page in this safebuf
	SafeBuf      m_sb;

	// if socket closes before we get a chance to send back
	// search results, we will know by comparing this to
	// m_socket->m_numDestroys
	int32_t         m_numDestroys;
	bool         m_header;

	collnum_t    m_collnum;
	//Query        m_q;
	SearchInput  m_si;
	Msg40        m_msg40;
	TcpSocket   *m_socket;
	int64_t    m_startTime;
	bool         m_gotResults;
	char         m_spell  [MAX_FRAG_SIZE]; // spelling recommendation
	bool         m_gotSpell;
	int32_t         m_errno;
        int32_t         m_numDocIds;
	int64_t    m_took; // how long it took to get the results
	HttpRequest  m_hr;
	bool         m_printedHeaderRow;
	//char         m_qe[MAX_QUERY_LEN+1];
	SafeBuf m_qesb;

	// stuff for doing redownloads
	bool    m_didRedownload;
	XmlDoc *m_xd;
	int32_t    m_oldContentHash32;
	int64_t m_socketStartTimeHack;
};


bool printSearchResultsHeader ( class State0 *st ) ;
bool printResult ( class State0 *st,  int32_t ix , int32_t *numPrintedSoFar );
bool printSearchResultsTail ( class State0 *st ) ;



#endif // GB_PAGERESULTS_H
