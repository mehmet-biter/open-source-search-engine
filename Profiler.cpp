#include <execinfo.h>
#include "gb-include.h"
#include "Profiler.h"
#include "HttpRequest.h"
#include "Pages.h"
#include "Conf.h"
#include "Hostdb.h"
#include "Loop.h"
#include "Mem.h"
#include <fcntl.h>

Profiler g_profiler;

Profiler::Profiler() : 
	m_realTimeProfilerRunning(false),
	rtNumEntries(0),
	hitEntries(NULL),
	m_addressMap(NULL),
	m_addressMapSize(0),
	m_rootFrame(0),
	m_lastDeltaAddress(0),
	m_lastDeltaAddressTime(0),
	m_frameTraces(NULL),
	m_numUsedFrameTraces(0)
{

	// Coverity
	m_totalFrames = 0;
}

Profiler::~Profiler() {//reset();
	reset();
}

bool Profiler::reset(){
	m_fn.reset();

	if(hitEntries)
		mfree(hitEntries, sizeof(HitEntry) * rtNumEntries,
			"hitEntries");
	hitEntries = NULL;
	if(m_addressMap)
		mfree(m_addressMap, sizeof(uint32_t) * m_addressMapSize,
			"m_addressMap");
	m_addressMap = NULL;

	m_ipBuf.purge();

	return true;
}

bool Profiler::init() {
	if ( m_ipBuf.getCapacity() <= 0 &&
	     ! m_ipBuf.reserve ( 5000000 , "profbuf" ) )
		return false;

	return true;
}

char* Profiler::getFnName( PTRTYPE address,int32_t *nameLen){
	FnInfo *fnInfo;
	int32_t slot=m_fn.getSlot(&address);
	if(slot!=-1)
		fnInfo=(FnInfo *)m_fn.getValueFromSlot(slot);
	else 
		return NULL;
	if (nameLen)
	*nameLen=strlen(fnInfo->m_fnName);
	return fnInfo->m_fnName;
}
	
bool sendPageProfiler ( TcpSocket *s , HttpRequest *r ) {
	SafeBuf sb;
	sb.reserve2x(32768);

	//read in all of the possible cgi parms off the bat:
	const char *coll = r->getString ("c");
	if ( ! coll || ! coll[0] ) {
		coll = g_conf.getDefaultColl( );
	}

	int startRt=(int)r->getLong("rtstart",0);
	int stopRt=(int)r->getLong("rtstop",0);
	
	g_pages.printAdminTop ( &sb , s , r );

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin && ! isCollAdmin ) {
		startRt = 0;
		stopRt = 0;
	}

	if (!g_conf.m_profilingEnabled) {
		sb.safePrintf("<font color=#ff0000><b><center>"
			      "Sorry, this feature is temporarily disabled. "
			      "Enable it in MasterControls.</center></b></font>");
	} else {
		if (g_profiler.m_realTimeProfilerRunning) {
			if (stopRt) {
				g_profiler.stopRealTimeProfiler(false);
				g_profiler.m_ipBuf.purge();
			}
		} else if (startRt) {
			g_profiler.startRealTimeProfiler();
		}
				
		g_profiler.printRealTimeInfo(&sb, coll);
	}

	return g_httpServer.sendDynamicPage ( s, (char*)sb.getBufStart(), sb.length(), -1, false);
}

FrameTrace *
FrameTrace::set(const uint32_t addr) {
	address = addr;
	return this;
}

FrameTrace *
FrameTrace::add(const uint32_t addr) {
	//log("add %x", addr);
	// We should be finding children most of the time not adding them.
	int32_t left = 0;
	int32_t right = m_numChildren - 1;
	while(left <= right) {
		const int32_t middle = (left + right) >> 1;
		FrameTrace *frame = m_children[middle];
		if(frame->address == addr) {
			//log("found %x %x", addr, frame);
			return frame;
		}
		if(frame->address < addr) {
			left = middle + 1;
		}
		else {
			right = middle - 1;
		}
	}
	if(m_numChildren == MAX_CHILDREN) {
		log("profiler: Relatime profiler frame trace node full!");
		// This node is full.
		return NULL;
	}
	// Did not find it, add it
	FrameTrace *frame = g_profiler.getNewFrameTrace(addr);
	if(!frame) {
		// Our static buffer must be used up.
		return NULL;
	}
	memmove( 	m_children + left + 1,
			m_children + left,
			sizeof(FrameTrace *) * (m_numChildren - left));
	m_children[left] = frame;
	++m_numChildren;
	return frame;
}

void
FrameTrace::dump(	SafeBuf *out,
			const uint32_t level,
			uint32_t printStart) const {
	if(level) {
		char *name = g_profiler.getFnName(address);
		out->pad(' ', level);
		uint32_t l;
		if(name && (l = strlen(name))) {
			out->safePrintf("%s ", name);
		} else {
			l = sizeof("Unknown ") - 2;
			out->safePrintf("Unknown ");
		}
		if(hits) {
			out->pushChar(' ');
			out->pad('.', printStart - level - l - 3);
			out->pushChar('|');
			out->safePrintf(" %-10i", hits);
			out->pushChar('|');
		} else {
			out->pad(' ', printStart - level - l - 2);
			out->safePrintf("|           |");
		}
		out->safePrintf(" %#.8x |", address);
		out->safePrintf("\n");
	} else {
		out->safePrintf("|Stack Trace");
		printStart = getPrintLen(0) + 3;
		out->pad(' ', printStart - sizeof("|Stack Trace"));
		out->safePrintf("| Hits      | Address    |\n");
		out->pad('-', printStart + sizeof("| Hits      | Address    |") - 2);
		out->safePrintf("\n");
	}
	for(uint32_t i = 0; i < m_numChildren; ++i) 
		m_children[i]->dump(out, level + 2, printStart);
}

uint32_t
FrameTrace::getPrintLen(const uint32_t level) const {
	uint32_t ret = level;
	if(level) {
		char *name = g_profiler.getFnName(address);
		uint32_t l;
		if(!(name && (l = strlen(name)))) {
			l = sizeof("Unknown");
		}
		ret += l;
	}
	for(uint32_t i = 0; i < m_numChildren; ++i) {
		const uint32_t l = m_children[i]->getPrintLen(level + 2);
		if(l > ret) ret = l;
	}
	return ret;
}

void
Profiler::getStackFrame() {

	// turn off automatically after 60000 samples
	if ( m_totalFrames++ >= 60000 ) {
		stopRealTimeProfiler(false);
		return;
	}

	// the lines calling functions
	if ( g_profiler.m_ipBuf.getAvail() <= 8*32 )
	 	return;

	// support 64-bit
	void *trace[32];
	int32_t numFrames = backtrace((void **)trace, 32);
	if(numFrames < 3) return;

	// . now just store the Instruction Ptrs into a count hashtable
	// . skip ahead 2 to avoid the sigalrm function handler
	for ( int32_t i = 2 ; i < numFrames  ; i++ ) {
		// even if we are 32-bit, make this 64-bit for ease
		uint64_t addr = (uint64_t)(PTRTYPE)trace[i];

		// the call stack path for profiling the worst paths
		g_profiler.m_ipBuf.pushLongLong(addr);
		continue;
	}

	// indicate end of call stack path
	g_profiler.m_ipBuf.pushLongLong(0LL);//addr);

	return;
}

void
Profiler::startRealTimeProfiler() {
	log(LOG_INIT, "admin: starting real time profiler");
	init();
	m_realTimeProfilerRunning = true;
	m_totalFrames = 0;

	//set up SIGPROF to be sent every 5 milliseconds
	struct itimerval value;
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_usec = 1000;
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 5000;
	if( setitimer( ITIMER_PROF, &value, NULL ) != 0) {
		log(LOG_INIT, "admin: Profiler::startRealTimeProfiler(): setitimer() failed, errno=%d",errno);
	}
}

void
Profiler::stopRealTimeProfiler(bool keepData) {
	log(LOG_INIT, "admin: stopping real time profiler");
	m_realTimeProfilerRunning = false;

	//disable SIGPROF
	struct itimerval value;
	value.it_interval.tv_sec = 0;
	value.it_interval.tv_usec = 0;
	value.it_value.tv_sec = 0;
	value.it_value.tv_usec = 0;
	setitimer( ITIMER_PROF, &value, NULL );
}

class PathBucket {
public:
	char *m_pathStackPtr;
	int32_t m_calledTimes;
};
       

static int cmpPathBucket (const void *A, const void *B) {
	const PathBucket *a = *(const PathBucket **)A;
	const PathBucket *b = *(const PathBucket **)B;
	if      ( a->m_calledTimes < b->m_calledTimes ) return  1;
	else if ( a->m_calledTimes > b->m_calledTimes ) return -1;
	return 0;
}

bool
Profiler::printRealTimeInfo(SafeBuf *sb, const char *coll) {
	if(!m_realTimeProfilerRunning) {
		sb->safePrintf("<table %s>",TABLE_STYLE);
		sb->safePrintf("<tr class=hdrow><td colspan=7>"
			 "<center><b>Real Time Profiler "
			 "<a href=\"/admin/profiler?c=%s"
			 "&rtstart=1\">"
			 "(Start)</a></b></center>"
			       "</td></tr>\n",coll);
		sb->safePrintf("</table><br><br>\n");
		return true;
	}
	stopRealTimeProfiler(true);


	sb->safePrintf("<table %s>",TABLE_STYLE);
	sb->safePrintf("<tr class=hdrow>"
		       "<td colspan=7>"
			 "<b>Top 100 Profiled Line Numbers "
		       //"<a href=\"/admin/profiler?c=%s"
		       // "&rtall=%i\">%s</a>"
		       //,coll,
		       // rtall, showMessage);
		       );
	sb->safePrintf(
		       // "<a href=\"/admin/profiler?c=%s&rtstop=1\">"
		       // "(Stop)</a> [Click refresh to get latest profile "
		       // "stats][Don't forget to click STOP when done so you "
		       // "don't leave the profiler running which can slow "
		       //"things down.]"
		       "</b>"
		       "</td></tr>\n"
		       //,coll
		       );

	// system call to get the function names and line numbers
	// just dump the buffer
	char *ip = (char *)m_ipBuf.getBufStart();
	char *ipEnd = (char *)m_ipBuf.getBufPtr();
	SafeBuf ff;
	ff.safePrintf("%strash/profile.txt",g_hostdb.m_dir);
	char *filename = ff.getBufStart();
	unlink ( filename );
	int fd = open ( filename , O_RDWR | O_CREAT , getFileCreationFlags() );
	if ( fd < 0 ) {
		sb->safePrintf("FAILED TO OPEN %s for writing: %s"
			       ,ff.getBufStart(),mstrerror(errno));
		return false;
	}
	for ( ; ip < ipEnd ; ip += sizeof(uint64_t) ) {
		// 0 marks end of call stack
		if ( *(long long *)ip == 0 ) continue;
		char tmp[64];
		int tlen = sprintf(tmp, "0x%llx\n", *(long long *)ip);
		int nw = write ( fd , tmp , tlen );
		if ( nw != tlen )
			log("profiler: write failed");
	}
	::close(fd);
	SafeBuf cmd;
	SafeBuf newf;
	newf.safePrintf("%strash/output.txt",g_hostdb.m_dir);
	// print the addr again somehow so we know
	cmd.safePrintf("addr2line  -a -s -p -C -f -e ./gb < %s | "
		       "sort | uniq -c | sort -rn > %s"
		       ,filename,newf.getBufStart());
	gbsystem ( cmd.getBufStart() );

	SafeBuf out;
	out.load ( newf.getBufStart());

	// restrict to top 100 lines
	char *x = out.getBufStart();

	if ( ! x ) {
		sb->safePrintf("FAILED TO READ trash/output.txt: %s"
			       ,mstrerror(g_errno));
		return false;
	}

	int lineCount = 0;
	for ( ; *x ; x++ ) {
		if ( *x != '\n' ) continue;
		if ( ++lineCount >= 100 ) break;
	}
	char c = *x;
	*x = '\0';

	sb->safePrintf("<tr><td colspan=10>"
		       "<pre>"
		       "%s"
		       "</pre>"
		       "</td>"
		       "</tr>"
		       "</table>"
		       , out.getBufStart() 
		       );

	*x = c;

	// now each function is in outbuf with the addr, so make a map
	// and use that map to display the top paths below. we hash the addrs
	// in each callstack together and the count those hashes to get
	// the top winners. and then convert the top winners to the
	// function names.
	char *p = out.getBufStart();
	HashTableX map;
	map.set ( 8,8,1024,NULL,0,false,"pmtb");
	for ( ; *p ; ) {
		// get addr
		uint64_t addr64;
		sscanf ( p , "%*i %" PRIx64" ", &addr64 );
		// skip if 0
		if ( addr64 ) {
			// record it
			int64_t off = p - out.getBufStart();
			map.addKey ( &addr64 , &off );
		}
		// skip to next line
		for ( ; *p && *p !='\n' ; p++ );
		if ( *p ) p++;
	}

	// now scan m_ipBuf (Instruction Ptr Buf) and make the callstack hashes
	ip = (char *)m_ipBuf.getBufStart();
	ipEnd = (char *)m_ipBuf.getBufPtr();
	char *firstOne = NULL;
	uint64_t hhh = 0LL;
	HashTableX pathTable;
	pathTable.set ( 8,sizeof(PathBucket),1024,NULL,0,false,"pbproftb");
	for ( ; ip < ipEnd ; ip += sizeof(uint64_t) ) {
		if ( ! firstOne ) firstOne = ip;
		uint64_t addr64 = *(uint64_t *)ip;
		// end of a stack
		if ( addr64 != 0LL ) { 
			hhh ^= addr64;
			continue;
		}
		// remove the last one though, because that is the line #
		// of the innermost function and we don't want to include it
		//hhh ^= lastAddr64;
		// it's the end, so add it into table
		PathBucket *pb = (PathBucket *)pathTable.getValue ( &hhh );
		if ( pb ) {
			pb->m_calledTimes++;
			firstOne = NULL;
			hhh = 0LL;
			continue;
		}
		// make a new one
		PathBucket npb;
		npb.m_pathStackPtr = firstOne;
		npb.m_calledTimes  = 1;
		pathTable.addKey ( &hhh , &npb );
		// start over for next path
		firstOne = NULL;
		hhh = 0LL;
	}

	// now make a buffer of pointers to the pathbuckets in the table
	SafeBuf sortBuf;
	for ( int32_t i = 0 ; i < pathTable.getNumSlots() ; i++ ) {
		// skip empty slots
		if ( ! pathTable.m_flags[i] ) continue;
		// get the bucket
		PathBucket *pb = (PathBucket *)pathTable.getValueFromSlot(i);
		// store the ptr
		sortBuf.safeMemcpy ( &pb , sizeof(PathBucket *) );
	}
	// now sort it up
	int32_t count = sortBuf.length() / sizeof(PathBucket *);
	qsort(sortBuf.getBufStart(),count,sizeof(PathBucket *),cmpPathBucket);


	// show profiled paths
	sb->safePrintf("<br><br><table %s>",TABLE_STYLE);
	sb->safePrintf("<tr class=hdrow>"
		       "<td colspan=7>"
		       "<b>Top 50 Profiled Paths</b>"
		       "</td></tr>"
		       "<tr><td colspan=10><pre>");

	// now print the top 50 out
	char *sp = sortBuf.getBufStart();
	char *spend = sp + sortBuf.length();
	int toPrint = 50;
	for ( ; sp < spend && toPrint > 0 ; sp += sizeof(PathBucket *) ) {
		toPrint--;
		PathBucket *pb = *(PathBucket **)sp;
		// get the callstack into m_ipBuf
		uint64_t *cs = (uint64_t *)pb->m_pathStackPtr;
		// scan those
		for ( ; *cs ; cs++ ) {
			// lookup this addr
			long *outOffPtr = (long *)map.getValue ( cs );
			if ( ! outOffPtr ) { 
				sb->safePrintf("        [0x%" PRIx64"]\n",*cs);
				continue;
			}
			// print that line out until \n
			char *a = out.getBufStart() + *outOffPtr;
			for ( ; *a && *a != '\n' ; a++ )
				sb->pushChar(*a);
			sb->pushChar('\n');
		}
		// the count
		sb->safePrintf("<b>%i</b>",(int)pb->m_calledTimes);
		sb->safePrintf("\n-----------------------------\n");
	}
	

	sb->safePrintf("</pre></td></tr></table>");

	// just leave it off if we printed something. but if we just
	// turn the profiler on then m_ipBuf will be empty so start it
	if ( m_ipBuf.length() == 0 )
		g_profiler.startRealTimeProfiler();	

	return true;
}

void
Profiler::cleanup() {
	m_rootFrame = 0;
}

FrameTrace *
Profiler::getNewFrameTrace(const uint32_t addr) {
	if(m_numUsedFrameTraces >= MAX_FRAME_TRACES) {
		log("profiler: Real time profiler ran out of static memory");
		return NULL;
	}
	return m_frameTraces[m_numUsedFrameTraces++].set(addr);
}
