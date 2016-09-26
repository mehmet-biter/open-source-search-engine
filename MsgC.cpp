#include "gb-include.h"
#include "Process.h"

#include "MsgC.h"
#include "Mem.h"


MsgC::MsgC ( ) {
	m_ipPtr = NULL;
	m_callback = NULL;
	m_state2 = NULL;
	m_state3 = NULL;
}

MsgC::~MsgC ( ) {
}

static void gotReplyWrapper ( void *state , void *state2 ) ;
static void handleRequest   ( UdpSlot *slot , int32_t niceness  ) ;
static void gotMsgCIpWrapper( void *state, int32_t ip);
	
bool MsgC::registerHandler ( ) {
	// . register ourselves with the high priority udp server
	// . it calls our callback when it receives a msg of type 0x0c
	if ( ! g_udpServer.registerHandler ( msg_type_c, handleRequest ))
		return false;
	return true;
}

// returns false if blocked, true otherwise
bool MsgC::getIp(const char *hostname, int32_t hostnameLen, int32_t *ip, void *state,
                 void (*callback)(void *state, int32_t ip), int32_t niceness) {
	m_mcast.reset();
	m_callback=callback;
	m_ipPtr = ip;
	// sanity check
	if ( ! m_ipPtr ) { g_process.shutdownAbort(true); }
	// First check if g_dns has it. This function is a part of the 
	// g_dns.getIp() function, except that we do not lookup the ip in
	// the dns server directly after not finding it in the cache.
	if ( !hostname || hostnameLen <= 0 ) {
		log(LOG_LOGIC,"dns: msgc: Asked to get IP of zero length "
		    "hostname.");
		*ip = 0;
		return true;
	}
	// . don't accept large hostnames
	// . technically the limit is 255 but i'm stricter
	if ( hostnameLen >= 254 ) {
		g_errno = EHOSTNAMETOOBIG;
		log("dns: msgc: Asked to get IP of hostname over 253 "
			   "characters long.");
		*ip = 0;
		return true;
	}
	// debug
	//char c = hostname[hostnameLen];
	//if ( c != 0 ) hostname[hostnameLen] = 0;
	log(LOG_DEBUG,"dns: msgc: getting ip for [%s]", hostname);
	    
	    
	//if ( c != 0 ) hostname[hostnameLen] = c;
	// if url is already in a.b.c.d format return that
	if ( is_digit(hostname[0]) ) {
		*ip = atoip ( hostname , hostnameLen );
		// somebody put a http://3.0/ link in here
		//if ( *ip >= 1 && *ip < 10 )
		//	log("dns: hostname had ip %s",iptoa(*ip));
		//if ( *ip == 3 ) { g_process.shutdownAbort(true); }
		if ( *ip != 0 ) return true;
	}
	
	// key is hash of the hostname
	key96_t key = g_dns.getKey ( hostname , hostnameLen );
	
	// is it in the /etc/hosts file?
	if ( g_conf.m_useEtcHosts && g_dns.isInFile ( key , ip ))return 1;
		
		
	// . try getting from the cache first
	// . this returns true if was in the cache and sets *ip to the ip
	// . ip is set to 0 for non-existent domains, and -1 if there was
	//   a dns timed out error getting it the last time. these will be
	//   cached for about a day.
	if ( g_dns.isInCache ( key , ip ) ) {
		if ( *ip == 3 ) { g_process.shutdownAbort(true); }
		// debug msg
		//log(LOG_DEBUG, "dns::getIp: %s (key=%" PRIu64") has ip=%s in cache!!!",
		//     tmp,key.n0,iptoa(*ip));
		return true;
	}


	// So its not in the local dns cache, so lets look in the 
	// s_localDnsCache. For that I need to pass the url
	m_u.set( hostname, hostnameLen );
	/*
	*ip=g_spiderCache.getLocalIp(&m_u);
	if (*ip !=0) {
		//Lets add to the local dns 
		g_dns.addToCache(key,*ip);
		return true;
	}
	*/
	//c = hostname[hostnameLen];
	//if ( c != 0 ) hostname[hostnameLen] = 0;

	//if ( c != 0 ) hostname[hostnameLen] = c;
	// Ok, so now it is not in dns cache or in spider cache, so lets send
	// the dns lookup request 
	// to a host in the cluster based on the hash of the hostname DIV'd
	// with the number of hosts in the cluster, g_hostdb.m_numHosts. The
	// request should be looked up in a new RdbCache (not Dns.cpp's local
	// cache) once delivered to the responsible host.
	// We have to send hostname, and the size is hostnameLen
	strncpy(m_request,hostname,hostnameLen);	
	int32_t requestSize = hostnameLen;
	// null end it
	m_request[requestSize]='\0';
	requestSize++;
	//uint32_t groupNum=key.n0 % g_hostdb.m_numShards;
	//uint32_t groupId=g_hostdb.getGroupId(groupNum);
	// get a hostid that should house this ip in its local cache
	Host *host = NULL; // g_dns.getResponsibleHost ( key );

	// with the new iframe tag expansion logic in Msg13.cpp, the
	// spider proxy will create a newXmlDoc to do that and will call
	// MsgC to lookup ips.. so we do not want to send a msgc request
	// to ourselves, so just call the dns directly
	if ( g_hostdb.m_myHost->m_isProxy ) {
		if ( g_dns.getIp( hostname, 
				  hostnameLen, 
				  ip, 
				  state,
				  callback)) 
			return true;
		// ok, we blocked, call callback when done
		return false;
	}

	// there was logic for getting ip from a proxy here
	// removed in commit bab9e9da06a8edeb8a7677c2e90f72766f6ba782 as it was never used

	host = g_dns.getResponsibleHost ( key );

	if ( g_conf.m_logDebugDns ) {
		int32_t fip = 0;
		if ( host ) fip = host->m_ip;
		log(LOG_DEBUG,"dns: msgc: multicasting for ip for %s to %s.",
		    hostname,iptoa(fip));
	}

	// it should have set g_errno to EDEADHOST if this happens
	if ( ! host ) {
		log("dns: please add primary dns to spider proxy gb.conf");
		if ( ! g_errno ) { g_process.shutdownAbort(true); }
		return true;
	}

	int32_t          firstHostId = host->m_hostId;
	// the handling server will timeout its dns algorithm and send us
	// back an EDNSTIMEDOUT error, so we do not need to have any timeout
	// here unless we are niceness 0, which we need in case the handling
	// servers goes down, we do not want to wait for it and would rather
	// call the callback with an EUDPTIMEDOUT error after 60 seconds.
	int64_t timeout = (niceness==0) ? multicast_msg1c_getip_default_timeout : multicast_infinite_send_timeout;

	// key is useless for us
	if (!m_mcast.send(m_request, requestSize, msg_type_c, false, host->m_shardNum, false, 0, this, state, gotReplyWrapper, timeout, niceness, firstHostId, NULL, false)) {
		//did not block, error
		log(LOG_DEBUG,"dns: msgc: mcast had error: %s",
		    mstrerror(g_errno));
		return true;
	}
	//Should always block, unless error
	return false;
}

void gotReplyWrapper ( void *state , void *state2 ) {
	MsgC *THIS = (MsgC *)state;
	// this can set g_errno. sets to ETRYAGAIN if checksum is wrong
	// so XmlDoc.cpp should try again! maybe later...
	int32_t ip = THIS->gotReply();
	// debug
	if ( g_conf.m_logDebugDns ) {
		logf(LOG_DEBUG,"dns: msgc: got reply of %s for %s. "
		     "state=0x%" PTRFMT" mcast=0x%" PTRFMT"",
		     iptoa(*THIS->m_ipPtr),THIS->m_u.getUrl(),(PTRTYPE)state2,
		     (PTRTYPE)&THIS->m_mcast);
	}
	THIS->m_callback(state2,ip);
}

int32_t MsgC::gotReply(){
	int32_t replySize,maxSize;
	bool freeIt;
	char *reply = m_mcast.getBestReply (&replySize, &maxSize, &freeIt);
	*m_ipPtr = 0;
	int32_t ip2 = 0;

	// sanity check
	if (replySize != 12 || !reply ){
		g_errno = EBADREPLYSIZE;
		log( "dns: msgc: Bad reply size of %" PTRFMT"",
		     (PTRTYPE)reply );
	}
        else {
		*m_ipPtr = *(int32_t *)reply;
		// repeated ip
		ip2 = *(int32_t *)(reply + 4);
		// an actual checksum
		//int32_t crc = *(int32_t *)(reply + 8);
	}
	// debug
	log(LOG_DEBUG,"dns: msgc: got reply of %s for %s.",
	    iptoa(*m_ipPtr),m_u.getUrl());
	// test checkusm
	if ( *m_ipPtr != ip2 ) {
		log("dns: ip checksum is incorrect. %" PRIu32" != %" PRIu32". "
		    "setting to -1.",  *m_ipPtr,ip2);
		g_errno = ETRYAGAIN;
		*m_ipPtr = -1;
		return -1;
	}

	// . if we have to free the buffer
	// . if freeIt is false that maeans we own the reply buffer
	if (!freeIt) {
		//log (LOG_DEBUG,"msgC: Multicast asked to free buffer");
		mfree(reply,maxSize,"MulticastMsgC");
	}
	// sanity check
	if ( (uint32_t)*m_ipPtr <= 255 &&
	     (uint32_t)*m_ipPtr >  0      ) {
		log("dns: msgc: got msgc ip reply of %" PRIu32" for %s. wtf? trying "
		    "again.", *m_ipPtr,m_u.getUrl());
		g_errno = ETRYAGAIN;
		*m_ipPtr = 0;
		//g_process.shutdownAbort(true); }
	}
	// . don't add to cache if there was an error.
	// . at this level, these are multicast errors, not dns errors
	if(g_errno) return *m_ipPtr;

	// Now we can add stuff to the local dns and spider cache.
	key96_t key = g_dns.getKey ( m_u.getHost(),m_u.getHostLen() );
	// just cache for hour locally since ttl may not have been that high
	// as given to us from the authoratative name server. 
	// TODO: return the ttl as well.
       	g_dns.addToCache(key,*m_ipPtr,60*60*24);
	// and tell the spider the ip of this url so it can do its IP based
	// throttling.
	//g_spiderCache.addLocalIp(&m_u,*m_ipPtr);
	return *m_ipPtr;
}

// . only return false if you want slot to be nuked w/o replying
// . MUST always call g_udpServer::sendReply() or sendErrorReply()
void handleRequest(UdpSlot *slot, int32_t niceness) {
	// get the request, should be the hostname
	char *hostname = slot->m_readBuf;

	// do not include the \0 at the end in the length
	int32_t  hostnameLen = slot->m_readBufSize - 1;

	int32_t ip=0;

	log(LOG_DEBUG,"dns: msgc: handle request called for %s state=%" PTRFMT, hostname,(PTRTYPE)slot);

	// check dns cache for the hostname. This should also send to
	// the dnsServer. If it is not in the cache, getIp puts it in.
	if (g_dns.getIp(hostname, hostnameLen, &ip, slot, gotMsgCIpWrapper)) {
		gotMsgCIpWrapper(slot, ip);
	}
	return;
}
	


void gotMsgCIpWrapper( void *state, int32_t ip){
	UdpSlot *slot=(UdpSlot *) state;

	log(LOG_DEBUG,"dns: msgc sending reply for state=%" PTRFMT".",(PTRTYPE)state);

	//to fit the ip address
	//char reply[12];
	// don't put it on the stack because sendReply does not copy!
	char *reply = slot->m_tmpBuf;
	int32_t replySize=12;
	if ( TMPBUFSIZE < replySize ) { g_process.shutdownAbort(true); }
	//	reply=(char*) mmalloc(replySize,"MsgC");
	char *p = reply;
	*(int32_t *)p = ip; p += 4;
	// repeat it as a checksum
	*(int32_t *)p = ip; p += 4;
	// an actual checksum
	*(int32_t *)p = hash32h ( ip , 0 ); p += 4;

	g_udpServer.sendReply(reply, replySize, NULL, 0, slot);

	return;
}
