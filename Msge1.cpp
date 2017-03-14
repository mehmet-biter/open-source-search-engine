#include "gb-include.h"
#include "Process.h"
#include "ip.h"
#include "Conf.h"
#include "Mem.h"

#include "Msge1.h"

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
			  int32_t     nowGlobal              ,
			  bool     addTags                ) {

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
	m_addTags          = addTags;

	// . how much mem to alloc?
	// . include an extra 4 bytes for each one to hold possible errno
	int32_t need = 4 + 4; // ip + error
	// one per url
	need *= numUrls;
	// allocate the buffer to hold all the info we gather
	m_buf = (char *)mcalloc ( need , "Msge1buf" );
	if ( ! m_buf ) return true;
	m_bufSize = need;

	// clear it all
	memset(m_buf, 0, m_bufSize);

	// set the ptrs!
	char *p = m_buf;
	m_ipBuf             = (int32_t *)p ; p += numUrls * 4;
	m_ipErrors          = (int32_t *)p ; p += numUrls * 4;

	// initialize
	m_numRequests = 0;
	m_numReplies  = 0;

	// . point to first url to process
	// . url # m_n
	m_n = 0;

	// clear the m_used flags
	memset(m_used, 0, sizeof(m_used));

	// . launch the requests
	// . a request can be a msg8a, msgc, msg50 or msg20 request depending
	//   on what we need to get
	// . when a reply returns, the next request is launched for that url
	// . we keep a msge1Slot state for each active url in the buffer
	// . we can have up to MAX_ACTIVE urls active
	if ( ! launchRequests ( 0 ) ) return false;

	// none blocked, we are done
	return true;
}

// we only come back up here 1) in the very beginning or 2) when a url 
// completes its pipeline of requests
bool Msge1::launchRequests ( int32_t starti ) {
	// reset any error code
	g_errno = 0;
 loop:
	// stop if no more urls. return true if we got all replies! no block.
	if ( m_n >= m_numUrls ) return (m_numRequests == m_numReplies);
	// if we are maxed out, we basically blocked!
	if (m_numRequests - m_numReplies >= MAX_OUTSTANDING_MSGE1)return false;

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
		goto loop; 
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
		goto loop; 
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
		// what is this? i no longer have this bug really - i fixed
		// it - but it did core here probably from a bad dns reply!
		// so take this out...
		//if ( ip == 3 ) { g_process.shutdownAbort(true); }
		m_ipBuf[m_n] = ip;
		m_numRequests++; 
		m_numReplies++; 
		m_n++; 
		goto loop; 
	}

	// . grab a slot
	// . m_msg8as[i], m_msgCs[i], m_msg50s[i], m_msg20s[i]
	int32_t i;
	for ( i = starti ; i < MAX_OUTSTANDING_MSGE1 ; i++ )
		if ( ! m_used[i] ) break;
	// sanity check
	if ( i >= MAX_OUTSTANDING_MSGE1 ) { g_process.shutdownAbort(true); }
	// normalize the url
	//m_urls[i].set ( p , plen );
	// save the url number, "n"
	m_ns  [i] = m_n;
	// claim it
	m_used[i] = true;

	// . start it off
	// . this will start the pipeline for this url
	// . it will set m_used[i] to true if we use it and block
	// . it will increment m_numRequests and NOT m_numReplies if it blocked
	//sendMsgC ( i , dom , dlen );
	sendMsgC ( i , host , hlen );
	// consider it launched
	m_numRequests++;
	// inc the url count
	m_n++;
	// try to do another
	goto loop;
}


bool Msge1::sendMsgC ( int32_t i , const char *host , int32_t hlen ) {
	// we are processing the nth url
	int32_t   n    = m_ns[i];
	// set m_errno if we should at this point
	if ( ! m_errno && g_errno != ENOTFOUND ) m_errno = g_errno;
	// reset it
	g_errno = 0;

	// using the the ith msgC
	MsgC  *m    = &m_msgCs[i];
	// save i and this in the msgC itself
	m->m_msge1 = this;
	m->m_msge1State = i;

	if (!m->getIp(host, hlen, &m_ipBuf[n], m, gotMsgCWrapper)) {
		return false;
	}
	return doneSending ( i );
}	

void Msge1::gotMsgCWrapper(void *state, int32_t ip) {
	MsgC   *m    = (MsgC  *)state;
	Msge1  *THIS = m->m_msge1;
	int32_t    i    = m->m_msge1State;
	if ( ! THIS->doneSending ( i ) ) return;
	// try to launch more, returns false if not done
	if ( ! THIS->launchRequests(i) ) return;
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}

bool Msge1::doneSending ( int32_t i ) {
	// we are processing the nth url
	int32_t n = m_ns[i];
	// save the error
	m_ipErrors[n] = g_errno;
	// save m_errno
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// clear it
	g_errno = 0;
	return addTag ( i );
}

bool Msge1::addTag ( int32_t i ) {

	// we are processing the nth url
	int32_t n = m_ns[i];
	// get ip we got
	//int32_t ip = m_ipBuf[n];

	//
	// HACK: hijack this MsgC to use as a "state" for call to msg9a
	// so we can add the "firstip" tag, since we did not have one!
	//

	// using the the ith msgC
	MsgC  *m    = &m_msgCs[i];
	// save i and this in the msgC itself
	m->m_msge1 = this;
	m->m_msge1State = i;
	// store the domain here
	//char *domBuf = m->m_request;
	// get the domain
	//int32_t  dlen = 0;
	//char *dom  = getDomFast ( m_urlPtrs[n] , &dlen );

	// make it all host based
	//char *hostBuf = m->m_request;
	// get the host
	int32_t  hlen = 0;
	const char *host  = getHostFast ( m_urlPtrs[n] , &hlen );


	// if invalid or ip-based, skip it!
	if ( ! host || hlen <= 0 ) 
		return doneAddingTag ( i );

	if ( ! m_addTags )
		return doneAddingTag ( i );

	// now let xmldoc add the firstip tags of each outlink!
	return doneAddingTag ( i );
}

bool Msge1::doneAddingTag ( int32_t i ) {
	// unmangle
	//*m_pathPtr[i] = '/';
	m_numReplies++;
	// free it
	m_used[i] = false;
	// we did not block
	return true;
}
