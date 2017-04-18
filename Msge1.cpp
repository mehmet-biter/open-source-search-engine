#include "Msge1.h"
#include "Process.h"
#include "Tagdb.h"
#include "ip.h"
#include "UrlBlockList.h"
#include "Conf.h"
#include "Mem.h"
#include "ScopedLock.h"


Msge1::Msge1()
  : m_niceness(0),
    m_urlPtrs(NULL),
    m_urlFlags(NULL),
    m_numUrls(0),
    m_addTags(false),
    m_buf(NULL),
    m_bufSize(0),
    m_ipBuf(NULL),
    m_ipErrors(NULL),
    m_numRequests(0),
    m_numReplies(0),
    m_n(0),
    m_mtx(),
    m_msgCs(),
    m_grv(NULL),
    m_state(NULL),
    m_callback(NULL),
    m_nowGlobal(0),
    m_errno(0)
{
	for(int i=0; i<MAX_OUTSTANDING_MSGE1; i++)
		m_ns[i] = 0;
	for(int i=0; i<MAX_OUTSTANDING_MSGE1; i++)
		m_used[i] = false;
}

Msge1::~Msge1() {
	reset();
}


void Msge1::reset() {
	m_errno = 0;
	if ( m_buf ) {
		mfree(m_buf, m_bufSize, "Msge1buf");
		m_buf = NULL;
	}
	m_ipBuf = NULL;
	m_ipErrors = NULL;
	m_numRequests = 0;
	m_numReplies = 0;
	m_n = 0;
	
	for(int i=0; i<MAX_OUTSTANDING_MSGE1; i++)
		m_ns[i] = 0;
	for(int i=0; i<MAX_OUTSTANDING_MSGE1; i++)
		m_used[i] = false;
}


// . get various information for each url in a list of urls
// . urls in "urlBuf" are \0 terminated
// . used to be called getSiteRecs()
// . you can pass in a list of docIds rather than urlPtrs
bool Msge1::getFirstIps ( TagRec **grv ,
			  const char **urlPtrs,
			  const linkflags_t *urlFlags,
			  int32_t     numUrls                ,
			  int32_t     niceness               ,
			  void    *state                  ,
			  void   (*callback)(void *state) ,
			  int32_t     nowGlobal) {

	reset();
	// bail if no urls or linkee
	if ( numUrls <= 0 ) return true;

	// save all input parms
	m_grv              = grv;
	m_urlPtrs          = urlPtrs;
	m_urlFlags         = urlFlags;
	m_numUrls          = numUrls;
	m_niceness         = niceness;
	m_state            = state;
	m_callback         = callback;
	m_nowGlobal        = nowGlobal;

	// . how much mem to alloc?
	// . include an extra 4 bytes for each one to hold possible errno
	int32_t needPerUrl = sizeof(*m_ipBuf)
	                   + sizeof(*m_ipErrors);
	// one per url
	int32_t needTotal = needPerUrl * numUrls;
	// allocate the buffer to hold all the info we gather
	m_buf = (char *)mcalloc ( needTotal , "Msge1buf" );
	if ( ! m_buf ) return true;
	m_bufSize = needTotal;
	// clear it all
	memset ( m_buf , 0 , m_bufSize );
	// set the ptrs!
	char *p = m_buf;
	m_ipBuf             = (int32_t *)p ; p += numUrls * sizeof(*m_ipBuf);
	m_ipErrors          = (int32_t *)p ; p += numUrls * sizeof(*m_ipErrors);
	// initialize
	m_numRequests = 0;
	m_numReplies  = 0;
	// . point to first url to process
	// . url # m_n
	m_n = 0;
	// clear the m_used flags
	for(int i=0; i<MAX_OUTSTANDING_MSGE1; i++)
		m_used[i] = false;

	// . launch the requests
	return launchRequests(0);
}

// we only come back up here 1) in the very beginning or 2) when a url 
// completes its pipeline of requests
bool Msge1::launchRequests ( int32_t starti ) {
	// reset any error code
	g_errno = 0;

	const int32_t maxOut = MAX_OUTSTANDING_MSGE1;

	ScopedLock sl(m_mtx);
	while(m_n < m_numUrls && m_numRequests - m_numReplies < maxOut) {
		// grab the "firstip" from the tagRec if we can
		TagRec *gr  = m_grv[m_n];
		Tag    *tag = NULL;
		if ( gr ) tag = gr->getTag("firstip");
		int32_t ip;
		// grab the ip that was in there
		if ( tag ) ip = atoip(tag->getTagData());
		// if we had it but it was 0 or -1, then time that out
		// after a day or so in case it works again! 0 and -1 mean
		// NXDOMAIN or timeout error, etc.
		if ( tag && ( ip == 0 || ip == -1 ) )
			if ( m_nowGlobal - tag->m_timestamp > 3600*24 ) tag = NULL;
		// . if we still got the tag, use that, even if ip is 0 or -1
		// . this keeps things fast
		// . this makes sure doConsistencyCheck() does not block too in
		//   XmlDoc.cpp... cuz it cores if it does block
		if ( tag ) {
			// now "ip" might actually be -1 or 0 (invalid) so be careful
			m_ipBuf[m_n] = ip;
			// what is this?
			//if ( ip == 3 ) { g_process.shutdownAbort(true); }
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}

		// or if banned
		Tag *btag = NULL;
		if ( gr ) btag = gr->getTag("manualban");
		if ( btag && btag->getTagData()[0] !='0') {
			// debug for now
			if ( g_conf.m_logDebugDns )
				log("dns: skipping dns lookup on banned hostname");
			// -1 means time out i guess
			m_ipBuf[m_n] = -1;
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}

		// . get the next url
		// . if m_xd is set, create the url from the ad id
		const char *p = m_urlPtrs[m_n];

		// if it is ip based that makes things easy
		int32_t  hlen = 0;
		const char *host = getHostFast ( p , &hlen );

		// reset this again
		ip = 0;
		// see if the hostname is actually an ip like "1.2.3.4"
		if ( host && is_digit(host[0]) ) ip = atoip ( host , hlen );
		// if legit this is non-zero
		if ( ip ) {
			m_ipBuf[m_n] = ip;
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}

		Url url;
		url.set(p);
		if(g_urlBlockList.isUrlBlocked(url)) {
			// debug for now
			if(g_conf.m_logDebugDns)
				log("dns: skipping dns lookup of '%*.*s' because the URL is blocked", (int)url.getHostLen(), (int)url.getHostLen(), url.getHost());
			// -1 means time out i guess
			m_ipBuf[m_n] = -1;
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}
		
		// . grab a slot
		int32_t i;
		for ( i = starti ; i < MAX_OUTSTANDING_MSGE1 ; i++ )
			if ( ! m_used[i] ) break;
		// sanity check
		if ( i >= MAX_OUTSTANDING_MSGE1 ) { g_process.shutdownAbort(true); }
		// save the url number, "n"
		m_ns  [i] = m_n++;
		// claim it
		m_used[i] = true;

		// . start it off
		// . this will start the pipeline for this url
		// . it will set m_used[i] to true if we use it and block
		// . it will increment m_numRequests and NOT m_numReplies if it blocked
		m_numRequests++;
		sendMsgC ( i , host , hlen );
	}

	if( m_n >= m_numUrls )
		return m_numRequests == m_numReplies;
	return false;
}


bool Msge1::sendMsgC(int32_t slotIndex, const char *host, int32_t hlen) {
	// set m_errno if we should at this point
	if ( ! m_errno && g_errno != ENOTFOUND ) m_errno = g_errno;
	g_errno = 0;

	MsgC  *m    = &m_msgCs[slotIndex];
	// save state into MsgC
	m->m_msge1 = this;
	m->m_msge1State = slotIndex;

	// we are processing the nth url
	int32_t n = m_ns[slotIndex];

	if (!m->getIp(host, hlen, &m_ipBuf[n], m, gotMsgCWrapper))
		return false;
	doneSending_unlocked(slotIndex);
	return true;
}

void Msge1::gotMsgCWrapper(void *state, int32_t ip) {
	MsgC   *m    = (MsgC  *)state;
	Msge1  *THIS = m->m_msge1;
	int32_t    slotIndex    = m->m_msge1State;

	if(!THIS->m_used[slotIndex])
		g_process.shutdownAbort(true);

	THIS->doneSending(slotIndex);

	// try to launch more, returns false if not done
	if ( ! THIS->launchRequests(slotIndex) ) return;
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}


void Msge1::doneSending(int32_t slotIndex) {
	ScopedLock sl(m_mtx);
	doneSending_unlocked(slotIndex);
}


void Msge1::doneSending_unlocked(int32_t slotIndex) {
	// we are processing the nth url
	int32_t n = m_ns[slotIndex];
	// save the error if msgC had one
	m_ipErrors[n] = g_errno;
	// save m_errno
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// reset error for successive calls to other msgs
	g_errno = 0;

	// tally it up
	m_numReplies++;
	// free it
	m_used[slotIndex] = false;
}
