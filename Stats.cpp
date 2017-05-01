#include "gb-include.h"

#include <errno.h>
#include "Stats.h"
#include "Conf.h"
#include "PingServer.h"
#include "ip.h"
#include "Mem.h"

class Stats g_stats;

// just clear our points array when we're born
Stats::Stats ( ) {
	m_next               = 0;
	memset ( m_pts , 0 , sizeof(StatPoint)*MAX_POINTS );

	memset(m_msg3aRecalls, 0, sizeof(m_msg3aRecalls));

	clearMsgStats();
	
	// Coverity
	m_uptimeStart = 0;
	memset(m_filterStats, 0, sizeof(m_filterStats));
};


void Stats::clearMsgStats() {
//	char *start = &m_start;
//	char *end   = &m_end;
//	memset ( start , 0 , end - start );
	// Version understandable by Coverity

	// char      m_start;
	memset(m_msgTotalOfSendTimes, 0, sizeof(m_msgTotalOfSendTimes));
	memset(m_msgTotalSent, 0, sizeof(m_msgTotalSent));
	memset(m_msgTotalSentByTime, 0, sizeof(m_msgTotalSentByTime));
	memset(m_msgTotalOfQueuedTimes, 0, sizeof(m_msgTotalOfQueuedTimes));
	memset(m_msgTotalQueued, 0, sizeof(m_msgTotalQueued));
	memset(m_msgTotalQueuedByTime, 0, sizeof(m_msgTotalQueuedByTime));
	memset(m_msgTotalOfHandlerTimes, 0, sizeof(m_msgTotalOfHandlerTimes));
	memset(m_msgTotalHandlersCalled, 0, sizeof(m_msgTotalHandlersCalled));
	memset(m_msgTotalHandlersByTime, 0, sizeof(m_msgTotalHandlersByTime));
	memset(m_packetsIn, 0, sizeof(m_packetsIn));
	memset(m_packetsOut, 0, sizeof(m_packetsOut));
	memset(m_acksIn, 0, sizeof(m_acksIn));
	memset(m_acksOut, 0, sizeof(m_acksOut));
	memset(m_reroutes, 0, sizeof(m_reroutes));
	memset(m_errors, 0, sizeof(m_errors));
	memset(m_timeouts, 0, sizeof(m_timeouts));
	memset(m_nomem, 0, sizeof(m_nomem));
	memset(m_dropped, 0, sizeof(m_dropped));
	memset(m_cancelRead, 0, sizeof(m_cancelRead));
	m_parsingInconsistencies = 0;
	m_totalOverflows = 0;
	m_compressedBytesIn = 0;
	m_uncompressedBytesIn = 0;
	m_compressAllDocs = 0;
	m_compressAllBytesIn = 0;
	m_compressAllBytesOut = 0;
	m_compressMimeErrorDocs = 0;
	m_compressMimeErrorBytesIn = 0;
	m_compressMimeErrorBytesOut = 0;
	m_compressUnchangedDocs = 0;
	m_compressUnchangedBytesIn = 0;
	m_compressUnchangedBytesOut = 0;
	m_compressBadContentDocs = 0;
	m_compressBadContentBytesIn = 0;
	m_compressBadContentBytesOut = 0;
	m_compressBadLangDocs = 0;
	m_compressBadLangBytesIn = 0;
	m_compressBadLangBytesOut = 0;
	m_compressBadCharsetDocs = 0;
	m_compressBadCharsetBytesIn = 0;
	m_compressBadCharsetBytesOut = 0;
	m_compressBadCTypeDocs = 0;
	m_compressBadCTypeBytesIn = 0;
	m_compressBadCTypeBytesOut = 0;
	m_compressHasIframeDocs = 0;
	m_compressPlainLinkDocs = 0;
	m_compressPlainLinkBytesIn = 0;
	m_compressPlainLinkBytesOut = 0;
	m_compressEmptyLinkDocs = 0;
	m_compressEmptyLinkBytesIn = 0;
	m_compressEmptyLinkBytesOut = 0;
	m_compressFullPageDocs = 0;
	m_compressFullPageBytesIn = 0;
	m_compressFullPageBytesOut = 0;
	m_compressHasDateDocs = 0;
	m_compressHasDateBytesIn = 0;
	m_compressHasDateBytesOut = 0;
	m_compressRobotsTxtDocs = 0;
	m_compressRobotsTxtBytesIn = 0;
	m_compressRobotsTxtBytesOut = 0;
	m_compressUnknownTypeDocs = 0;
	m_compressUnknownTypeBytesIn = 0;
	m_compressUnknownTypeBytesOut = 0;
	//char      m_end;

}

//static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

void Stats::addStat_r ( int32_t        numBytes    , 
			int64_t   startTime   ,
			int64_t   endTime     ,
			int32_t        color       ,
			char        type        ,
			char *fname) {

	// lock up
	//pthread_mutex_lock ( &s_lock );

	// claim next before another thread does
	int32_t n = m_next++;
	// watch out if another thread just inc'ed n
	if ( n >= MAX_POINTS ) n = 0;
	// stick our point in the array
	StatPoint *p = & m_pts [ n ];
	p->m_numBytes  = numBytes;
	p->m_startTime = startTime ;
	p->m_endTime   = endTime;
	p->m_color     = color;
	p->m_type      = type;

	if(fname) {
		if(m_keyCols.length() > 512) m_keyCols.reset();
		p->m_color     = hash32n(fname);
		p->m_color &= 0xffffff;
		m_keyCols.safePrintf(""
				     "<td bgcolor=#%x>"
				     "&nbsp; &nbsp;</td>"
				     "<td> %s "
				     "</td>"
				     "", p->m_color,fname);
	}

	// advance the next available slot ptr, wrap if necessary
	if ( m_next >= MAX_POINTS ) m_next = 0;

	// unlock
	//pthread_mutex_unlock ( &s_lock );
}

void Stats::addPoint (StatPoint **points    , 
		      int32_t       *numPoints ,
		      StatPoint  *p         ) {
	// go down each line of points
	for ( int32_t i = 0 ; i < MAX_LINES ; i++ ) {
		// is there room for us in this line?
		int32_t n = numPoints[i];
		// if line is full, skip it
		if ( n >= MAX_POINTS ) continue;
		int32_t j;
		// make a boundary around point there already
		int64_t a = p->m_startTime;
		int64_t b = p->m_endTime;
		// . for a space to appear we need to be separated
		//   by this many milliseconds
		// . this is milliseconds per pixel
		// . right now it's about 5
		int32_t border = DT / DX ;
		if ( border <= 0 ) border = 1;
		a -= 4*border;
		b += 4*border;
		// debug
		//log("a=%" PRId64" b=%" PRId64" d=%" PRId32,a,b,4*border);
		for ( j = 0 ; j < n ; j++ ) {
			// get that point
			StatPoint *pp = points[MAX_POINTS * i + j];
			// . do we intersect this point (horizontal line)?
			// . if so, break out
			if ( pp->m_startTime >= a && pp->m_startTime <= b )
				break;
			if ( pp->m_endTime   >= a && pp->m_endTime   <= b )
				break;
		}
		// if j is < n then there's no room
		if ( j < n ) continue;
		// otherwise, add our point
		points[MAX_POINTS * i + n] = p;
		numPoints[i]++;
		return;
	}
}

// draw a HORIZONTAL line in html
static void drawLine2(SafeBuf &sb,
		      int32_t x1,
		      int32_t x2,
		      int32_t fy1, 
		      int32_t color,
		      int32_t width) {

	sb.safePrintf("<div style=\"position:absolute;"
		      "left:%" PRId32";"
		      "top:%" PRId32";"
		      "background-color:#%06" PRIx32";"
		      "z-index:5;"
		      "min-height:%" PRId32"px;"
		      "min-width:%" PRId32"px;\"></div>\n"
		      , x1
		      , (fy1 - width/2) - 20 //- 300
		      , color
		      , width
		      , x2 - x1
		      );
}


//
// new code for drawing graph in html with absolute divs instead
// of using GIF plotter library which had issues
//
void Stats::printGraphInHtml ( SafeBuf &sb ) {

	// gif size
	char tmp[64];
	sprintf ( tmp , "%" PRId32"x%" PRId32, (int32_t)DX+40 , (int32_t)DY+40 ); // "1040x440"

	// find time ranges
	int64_t t2 = 0;
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// set min/max
		if ( m_pts[i].m_endTime   > t2 ) t2 = m_pts[i].m_endTime;
	}
	// now compute the start time for the graph
	int64_t t1 = 0x7fffffffffffffffLL;
	// now recompute t1
	for ( int32_t i = 0 ; i < MAX_POINTS ; i++ ) {
		// skip empties
		if ( m_pts[i].m_startTime == 0 ) continue;
		// can't be behind more than 1 second
		if ( m_pts[i].m_startTime   < t2 - DT ) continue;
		// otherwise, it's a candidate for the first time
		if ( m_pts[i].m_startTime < t1 ) t1 = m_pts[i].m_startTime;
	}

	//
	// main graphing window
	//
	sb.safePrintf("<div style=\"position:relative;"
		      "background-color:#c0c0c0;"

		      // match style of tables
		      "border-radius:10px;"
		      "border:#6060f0 2px solid;"
		      
		      //"overflow-y:hidden;"
		      "overflow-x:hidden;"
		      //"z-index:-10;"
		      // the tick marks we print below are based on it
		      // being a window of the last 20 seconds... and using
		      // DX pixels
		      "min-width:%" PRId32"px;"
		      "min-height:%" PRId32"px;"
		      //"width:100%%;"
		      //"min-height:600px;"
		      //"margin-top:10px;"
		      "margin-bottom:10px;"
		      //"margin-right:10px;"
		      //"margin-left:10px;"
		      "\">"
		      ,(int32_t)DX
		      ,(int32_t)DY +20); // add 10 more for "2s" labels etc.

	// 10 x-axis tick marks
	for ( int x = DX/20 ; x <= DX ; x += DX/20 ) {
		// tick mark
		//plotter.line ( x , -20 , x , 20 );
		sb.safePrintf("<div style=\"position:absolute;"
			      "left:%" PRId32";"
			      "bottom:0;"
			      "background-color:#000000;"
			      "z-index:110;"
			      "min-height:20px;"
			      "min-width:3px;\"></div>\n"
			      , (int32_t)x-1
			      );
		// generate label
		sb.safePrintf("<div style=\"position:absolute;"
			      "left:%" PRId32";"
			      "bottom:20;"
			      //"background-color:#000000;"
			      "z-index:110;"
			      "min-height:20px;"
			      "min-width:3px;\">%.01fs</div>\n"
			      , (int32_t)x-10
			      // the label:
			      ,(float)(DT* (int64_t)x / (int64_t)DX)/1000.0
			      );

	}

	// . each line consists of several points
	// . we need to know each point for adding otherlines
	// . is about [400/6][1024] = 70k
	// . each line can contain multiple data points
	// . each data point is expressed as a horizontal line segment
	void *lrgBuf;
	int32_t lrgSize = 0;
	lrgSize += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	lrgSize += MAX_LINES * sizeof(int32_t);
	lrgBuf = (char *) mmalloc(lrgSize, "Stats.cpp"); 
	if (! lrgBuf) {
	    log("could not allocate memory for local buffer in Stats.cpp"
		"%" PRId32" bytes needed", lrgSize);
	    return;
	}
	char *lrgPtr = (char *)lrgBuf;
	StatPoint **points = (StatPoint **)lrgPtr;   
	lrgPtr += MAX_LINES * MAX_POINTS * sizeof(StatPoint *);
	int32_t *numPoints = (int32_t *)lrgPtr;
	lrgPtr += MAX_LINES * sizeof(int32_t);
	memset ( (char *)numPoints , 0 , MAX_LINES * sizeof(int32_t) );

	// store the data points into "lines"
	int32_t count = MAX_POINTS;
	for ( int32_t i = m_next ; count >= 0 ; i++ , count-- ) {
		// wrap around the array
		if ( i >= MAX_POINTS ) i = 0;
		// skip point if empty
		if ( m_pts[i].m_startTime == 0 ) continue;
		// skip if too early
		if ( m_pts[i].m_endTime < t1 ) continue;
		// . find the lowest line the will hold us
		// . this adds point to points[x][n] where x is determined
		addPoint ( points , numPoints , &m_pts[i] );
	}

	int y1 = 21;
	// plot the points (lines) in each line
	for ( int32_t i = 0 ; i < MAX_LINES    ; i++ ) {
		// increase vert
		y1 += MAX_WIDTH + 1;
		// wrap back down if necessary
		if ( y1 >= DY ) y1 = 21;
		// plt all points in this row
		for ( int32_t j = 0 ; j < numPoints[i] ; j++ ) {
			// get the point
			StatPoint *p =  points[MAX_POINTS * i + j];
			// transform time to x coordinates
			int x1 = (p->m_startTime - t1) * (int64_t)DX / DT;
			int x2 = (p->m_endTime   - t1) * (int64_t)DX / DT;
			// if x2 is negative, skip it
			if ( x2 < 0 ) continue;
			// if x1 is negative, boost it to -2
			if ( x1 < 0 ) x1 = -2;
			// . line thickness is function of read/write size
			// . take logs
			int w = (int)log(((double)p->m_numBytes)/8192.0) + 3;
			//log("log of %" PRId32" is %i",m_pts[i].m_numBytes,w);
			if ( w < 3         ) w = 3;
			if ( w > MAX_WIDTH ) w = MAX_WIDTH;

			// ensure at least 3 units wide for visibility
			if ( x2 < x1 + 3 ) x2 = x1 + 3;
			// . flip the y so we don't have to scroll the browser down
			// . DY does not include the axis and tick marks
			int32_t fy1 = DY - y1 + 20 ;
			// plot it
			drawLine2 ( sb , x1 , x2 , fy1 , p->m_color , w );
		}
	}

	sb.safePrintf("</div>\n");

	mfree(lrgBuf, lrgSize, "Stats.cpp");
}
