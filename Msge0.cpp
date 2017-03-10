#include "Msge0.h"
#include "Process.h"
#include "Tagdb.h"
#include "ip.h"
#include "Mem.h"
#include <new>


Msge0::Msge0()
  : m_collnum(0),
    m_niceness(0),
    m_urlPtrs(NULL),
    m_urlFlags(NULL),
    m_numUrls(0),
    m_skipOldLinks(0),
    m_buf(NULL),
    m_bufSize(0),
    m_slabNum(-1),
    m_slab(NULL),
    m_slabPtr(NULL),
    m_slabEnd(NULL),
    m_baseTagRec(NULL),
    m_tagRecErrors(NULL),
    m_tagRecPtrs(NULL),
    m_numTags(NULL),
    m_numRequests(0),
    m_numReplies(0),
    m_n(0),
    m_state(NULL),
    m_callback(NULL),
    m_errno(0)
{
	memset(m_ns, 0, sizeof(m_ns));
	memset(m_used, 0, sizeof(m_used));
	reset();
}

Msge0::~Msge0() {
	reset();
}

static const size_t SLAB_SIZE = sizeof(TagRec)*20;

void Msge0::reset() {
	m_errno = 0;
	for ( int32_t i = 0 ; i < m_n ; i++ ) {
		// cast it
		TagRec *tr = m_tagRecPtrs[i];
		// skip if empty
		if ( ! tr ) {
			continue;
		}
		// skip if base
		if ( tr == m_baseTagRec ) {
			continue;
		}
		// free the rdblist memory in the TagRec::m_list
		m_tagRecPtrs[i]->~TagRec();
	}
	for ( int32_t i = 0; m_slab && i <= m_slabNum; i++ ) {
		mfree ( m_slab[i] , SLAB_SIZE , "msgeslab" );
	}
	m_slabNum = -1;
	m_slabPtr = NULL;
	m_slabEnd = NULL;
	if ( m_buf ) {
		mfree ( m_buf , m_bufSize,"Msge0buf");
	}
	m_buf = NULL;
	m_numRequests = 0;
	m_numReplies = 0;
	m_n = 0;
}


// . get various information for each url in a list of urls
// . urls in "urlBuf" are \0 terminated
// . used to be called getSiteRecs()
// . you can pass in a list of docIds rather than urlPtrs
bool Msge0::getTagRecs ( const char        **urlPtrs           ,
			 linkflags_t  *urlFlags          , //Links::m_linkFlags
			 int32_t          numUrls           ,
			// if skipOldLinks && urlFlags[i]&LF_OLDLINK, skip it
			 bool          skipOldLinks      ,
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
	m_skipOldLinks     = skipOldLinks;
	m_baseTagRec       = baseTagRec;
	m_collnum          = collnum;
	m_niceness         = niceness;
	m_state            = state;
	m_callback         = callback;

	// . how much mem to alloc?
	// . include an extra 4 bytes for each one to hold possible errno
	int32_t need = 
		4 + // error
		sizeof(TagRec *) + // tag ptr
		sizeof(char *) ; // slab ptr
	// one per url
	need *= numUrls;
	// allocate the buffer to hold all the info we gather
	m_buf = (char *)mcalloc ( need , "Msge0buf" );
	if ( ! m_buf ) return true;
	m_bufSize = need;
	// clear it all
	memset ( m_buf , 0 , m_bufSize );
	// set the ptrs!
	char *p = m_buf;
	m_tagRecErrors      = (int32_t    *)p ; p += numUrls * 4;
	m_tagRecPtrs        = (TagRec **)p ; p += numUrls * sizeof(TagRec *);
	m_slab              = (char   **)p ; p += numUrls * sizeof(char *);
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
	// . a request can be a msg8a, msgc, msg50 or msg20 request depending
	//   on what we need to get
	// . when a reply returns, the next request is launched for that url
	// . we keep a msgESlot state for each active url in the buffer
	// . we can have up to MAX_ACTIVE urls active
	if ( ! launchRequests() ) return false;

	// none blocked, we are done
	return true;
}

// we only come back up here 1) in the very beginning or 2) when a url 
// completes its pipeline of requests
bool Msge0::launchRequests() {
	// reset any error code
	g_errno = 0;

	for(;;) {
		// stop if no more urls. return true if we got all replies! no block.
		if ( m_n >= m_numUrls ) return (m_numRequests == m_numReplies);
		// if all hosts are getting a diffbot reply with 50 spiders and they
		// all timeout at the same time we can very easily clog up the
		// udp sockets, so use this to limit... i've seen the whole
		// spider tables stuck with "getting outlink tag rec vector"statuses
		int32_t maxOut = MAX_OUTSTANDING_MSGE0;
		if ( g_udpServer.getNumUsedSlots() > 500 ) maxOut = 1;
		// if we are maxed out, we basically blocked!
		if (m_numRequests - m_numReplies >= maxOut ) return false;
		// . skip if "old"
		// . we are not planning on adding this to spiderdb, so Msg16
		//   want to skip the ip lookup, etc.
		if ( m_urlFlags && (m_urlFlags[m_n] & LF_OLDLINK) && m_skipOldLinks ) {
			m_numRequests++;
			m_numReplies++;
			m_n++;
			continue;
		}
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
		// . m_msg8as[i], m_msgCs[i], m_msg50s[i], m_msg20s[i]
		int32_t i;
		// make this 0 since "maxOut" now changes!!
		for ( i = 0; i < MAX_OUTSTANDING_MSGE0 ; i++ )
			if ( ! m_used[i] ) break;
		// sanity check
		if ( i >= MAX_OUTSTANDING_MSGE0 ) { g_process.shutdownAbort(true); }
		// normalize the url
		m_urls[i].set( p, plen );
		// save the url number, "n"
		m_ns  [i] = m_n;
		// claim it
		m_used[i] = true;

		// note it
		//if ( g_conf.m_logDebugSpider )
		//	log(LOG_DEBUG,"spider: msge0: processing url %s",
		//	    m_urls[i].getUrl());

		// . start it off
		// . this will start the pipeline for this url
		// . it will set m_used[i] to true if we use it and block
		// . it will increment m_numRequests and NOT m_numReplies if it blocked
		m_numRequests++;
		sendMsg8a(i);
		// inc the url count
		m_n++;
	}
}

bool Msge0::sendMsg8a ( int32_t i ) {
	// handle errors
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	g_errno = 0;
	Msg8a  *m   = &m_msg8as[i];
	// save state into Msg8a
	m->m_msge0 =  this;
	m->m_msge0State = i;

	// we are processing the nth url
	int32_t n = m_ns[i];
	// now use it
	m_tagRecPtrs[n] = allocateTagRec();
	if(!m_tagRecPtrs[n]) {
		m_numReplies++; //we won't get a response on that one
		return true;
	}

	// . this now employs the tagdb filters table for lookups
	// . that is really a hack until we find a way to identify subsites
	//   on a domain automatically, like blogspot.com/users/harry/ is a 
	//   subsite.
	if ( !m->getTagRec( &m_urls[i], m_collnum, m_niceness, m, gotTagRecWrapper, m_tagRecPtrs[n] ))
		return false;
	return doneSending ( i );
}

void Msge0::gotTagRecWrapper(void *state) {
	Msg8a *m     = (Msg8a *)state;
	//TagRec *m    = (TagRec *)state;
	Msge0  *THIS = m->m_msge0;
	int32_t    i    = m->m_msge0State;
	if ( ! THIS->doneSending ( i ) ) return;
	// try to launch more, returns false if not done
	if ( ! THIS->launchRequests() ) return;
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}

bool Msge0::doneSending ( int32_t i ) {
	// we are processing the nth url
	int32_t   n    = m_ns[i];
	// save the error if msg8a had one
	m_tagRecErrors[n] = g_errno;
	// also, set m_errno for this Msge0 class...
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// reset error for successive calls to other msgs
	g_errno = 0;

	//
	// copy the Tags from Msg8a into a "slab".
	// alloc a new slab if not enough room.
	//

	// tally it up
	m_numReplies++;
	//if ( m_getSiteRecs ) ruleset = m_siteRecBuf[n].m_filenum;
	//log ( LOG_DEBUG, "build: Finished Msge0 for url [%" PRId32",%" PRId32"]: %s",
	//      n, i, m_urls[i].getUrl() );
	// free it
	m_used[i] = false;
	// we did not block
	return true;
}



TagRec *Msge0::allocateTagRec() {
	static const int32_t need = sizeof(TagRec);
	// how much space left in the latest buffer
	if ( m_slabPtr + need > m_slabEnd ) {
		char *newSlab = (char *)mmalloc (SLAB_SIZE,"msgeslab");
		if(!newSlab) {
			if ( ! m_errno ) m_errno = g_errno;
			// error out
			log("msge0: slab alloc: %s",mstrerror(g_errno));
			return NULL;
		}
		m_slab[++m_slabNum] = newSlab;
		m_slabPtr = m_slab[m_slabNum];
		m_slabEnd = m_slabPtr + SLAB_SIZE;
	}
	TagRec *tagRec = new (m_slabPtr) TagRec();
	m_slabPtr += sizeof(TagRec);
	return tagRec;
}
