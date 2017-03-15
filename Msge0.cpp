#include "Msge0.h"
#include "Process.h"
#include "Tagdb.h"
#include "ip.h"
#include "Mem.h"
#include "ScopedLock.h"
#include <new>


Msge0::Msge0()
  : m_collnum(0),
    m_niceness(0),
    m_urlPtrs(NULL),
    m_urlFlags(NULL),
    m_numUrls(0),
    m_buf(NULL),
    m_bufSize(0),
    m_baseTagRec(NULL),
    m_tagRecErrors(NULL),
    m_tagRecPtrs(NULL),
    m_tagRecs(NULL),
    m_numRequests(0),
    m_numReplies(0),
    m_n(0),
    m_mtx(),
    m_state(NULL),
    m_callback(NULL),
    m_errno(0)
{
	for(int i=0; i<MAX_OUTSTANDING_MSGE0; i++)
		m_ns[i] = 0;
	for(int i=0; i<MAX_OUTSTANDING_MSGE0; i++)
		m_used[i] = false;
}

Msge0::~Msge0() {
	reset();
}


void Msge0::reset() {
	m_errno = 0;
	//free TagRecs that are not the base tag
	for ( int32_t i = 0 ; i < m_n ; i++ ) {
		if(m_tagRecPtrs[i] && m_tagRecPtrs[i]!=m_baseTagRec)
			m_tagRecPtrs[i]->~TagRec();
	}
	if ( m_buf ) {
		mfree ( m_buf , m_bufSize,"Msge0buf");
	}
	m_buf = NULL;
	m_tagRecErrors = NULL;
	m_tagRecPtrs = NULL;
	m_tagRecs = NULL;
	m_numRequests = 0;
	m_numReplies = 0;
	m_n = 0;
	
	for(int i=0; i<MAX_OUTSTANDING_MSGE0; i++)
		m_urls[i].reset();
	for(int i=0; i<MAX_OUTSTANDING_MSGE0; i++)
		m_msg8as[i].reset();
}


// . get various information for each url in a list of urls
// . urls in "urlBuf" are \0 terminated
// . used to be called getSiteRecs()
// . you can pass in a list of docIds rather than urlPtrs
bool Msge0::getTagRecs ( const char        **urlPtrs           ,
			 const linkflags_t *urlFlags          , //Links::m_linkFlags
			 int32_t          numUrls           ,
			 TagRec       *baseTagRec        ,
			 collnum_t     collnum,
			 int32_t          niceness          ,
			 void         *state             ,
			 void        (*callback)(void *state) ) {
	reset();
	// bail if no urls or linkee
	if ( numUrls <= 0 ) return true;

	// save all input parms
	m_urlPtrs          = urlPtrs;
	m_urlFlags         = urlFlags;
	m_numUrls          = numUrls;
	m_baseTagRec       = baseTagRec;
	m_collnum          = collnum;
	m_niceness         = niceness;
	m_state            = state;
	m_callback         = callback;

	// . how much mem to alloc?
	// . include an extra 4 bytes for each one to hold possible errno
	int32_t needPerUrl = sizeof(int32_t)  // error
		           + sizeof(TagRec*)  // tag ptr
		           + sizeof(TagRec);  // m_tagRecs
		
	// one per url
	int32_t needTotal = needPerUrl * numUrls;
	// allocate the buffer to hold all the info we gather
	m_buf = (char *)mcalloc ( needTotal , "Msge0buf" );
	if ( ! m_buf ) return true;
	m_bufSize = needTotal;
	// clear it all
	memset ( m_buf , 0 , m_bufSize );
	// set the ptrs!
	char *p = m_buf;
	m_tagRecErrors      = (int32_t *)p ; p += numUrls * sizeof(int32_t);
	m_tagRecPtrs        = (TagRec **)p ; p += numUrls * sizeof(TagRec *);
	m_tagRecs           = (TagRec*)p;    p += numUrls * sizeof(TagRec);
	// initialize
	m_numRequests = 0;
	m_numReplies  = 0;
	// . point to first url to process
	// . url # m_n
	m_n = 0;
	// clear the m_used flags
	for(int i=0; i<MAX_OUTSTANDING_MSGE0; i++)
		m_used[i] = false;

	// . launch the requests
	return launchRequests();
}

// we only come back up here 1) in the very beginning or 2) when a url 
// completes its pipeline of requests
bool Msge0::launchRequests() {
	// reset any error code
	g_errno = 0;

	// if all hosts are getting a diffbot reply with 50 spiders and they
	// all timeout at the same time we can very easily clog up the
	// udp sockets, so use this to limit... i've seen the whole
	// spider tables stuck with "getting outlink tag rec vector"statuses
	const int32_t maxOut = g_udpServer.getNumUsedSlots() > 500 ? 1 : MAX_OUTSTANDING_MSGE0;
	
	ScopedLock sl(m_mtx);
	while(m_n < m_numUrls && m_numRequests - m_numReplies < maxOut) {
		// if url is same host as the tagrec provided, just reference that!
		if ( m_urlFlags && (m_urlFlags[m_n] & LF_SAMEHOST) && m_baseTagRec) {
			m_tagRecPtrs[m_n] = (TagRec *)m_baseTagRec;
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}
		// . get the next url
		// . if m_xd is set, create the url from the ad id
		const char *p = m_urlPtrs[m_n];
		// get the length
		int32_t  plen = strlen(p);
		// . grab a slot
		int32_t i;
		for ( i = 0; i < MAX_OUTSTANDING_MSGE0 ; i++ )
			if ( ! m_used[i] ) break;
		// sanity check
		if ( i >= MAX_OUTSTANDING_MSGE0 ) { g_process.shutdownAbort(true); }
		// normalize the url
		m_urls[i].set( p, plen );
		// save the url number, "n"
		m_ns  [i] = m_n++;
		// claim it
		m_used[i] = true;

		// . start it off
		// . this will start the pipeline for this url
		m_numRequests++;
		sendMsg8a(i);
	}
	
	if( m_n >= m_numUrls )
		return m_numRequests == m_numReplies;
	return false;
}

bool Msge0::sendMsg8a(int32_t slotIndex) {
	// handle errors
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	g_errno = 0;
	Msg8a  *m   = &m_msg8as[slotIndex];
	// save state into Msg8a
	m->m_msge0 =  this;
	m->m_msge0State = slotIndex;

	// we are processing the nth url
	int32_t n = m_ns[slotIndex];
	// now use it
	m_tagRecPtrs[n] = new (m_tagRecs+n) TagRec();

	// . this now employs the tagdb filters table for lookups
	// . that is really a hack until we find a way to identify subsites
	//   on a domain automatically, like blogspot.com/users/harry/ is a 
	//   subsite.
	if ( !m->getTagRec( &m_urls[slotIndex], m_collnum, m_niceness, m, gotTagRecWrapper, m_tagRecPtrs[n] ))
		return false;
	doneSending_unlocked(slotIndex);
	return true;
}

void Msge0::gotTagRecWrapper(void *state) {
	Msg8a *m     = reinterpret_cast<Msg8a*>(state);
	Msge0  *THIS = m->m_msge0;
	int32_t    slotIndex    = m->m_msge0State;
	
	if(!THIS->m_used[slotIndex])
		g_process.shutdownAbort(true);
	
	THIS->doneSending(slotIndex);
	
	// try to launch more, returns false if not done
	if ( ! THIS->launchRequests() ) return;
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}


void Msge0::doneSending(int32_t slotIndex) {
	ScopedLock sl(m_mtx);
	doneSending_unlocked(slotIndex);
}


void Msge0::doneSending_unlocked(int32_t slotIndex) {
	// we are processing the nth url
	int32_t   n    = m_ns[slotIndex];
	// save the error if msg8a had one
	m_tagRecErrors[n] = g_errno;
	// also, set m_errno for this Msge0 class...
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// reset error for successive calls to other msgs
	g_errno = 0;

	// tally it up
	m_numReplies++;
	// free it
	m_used[slotIndex] = false;
}
