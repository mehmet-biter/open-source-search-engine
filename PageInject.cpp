#include "gb-include.h"

#include "PageInject.h"
#include "HttpServer.h"
#include "Pages.h"
#include "XmlDoc.h"
#include "PageParser.h"
#include "Repair.h"
#include "HttpRequest.h"
#include "UdpSlot.h"
#include "UdpServer.h"
#include "Stats.h"
#include "Collectiondb.h"
#include "Process.h"
#include "GbUtil.h"
#include "Dir.h"
#include "ip.h"
#include "Conf.h"
#include "Mem.h"
#include <fcntl.h>


static bool sendHttpReply        ( void *state );

// scan each parm for OBJ_IR (injection request)
// and set it from the hr class then.
// use ptr_string/size_string stuff to point into the hr buf.
// but if we call serialize() then it makes news ones into its own blob.
// so we gotta know our first and last ptr_* pointers for serialize/deseria().
// kinda like how search input works
void setInjectionRequestFromParms(TcpSocket *sock, HttpRequest *hr, CollectionRec *cr, InjectionRequest *ir) {
	// just in case set all to zero
	memset ( ir , 0 , sizeof(InjectionRequest ));

	if ( ! cr ) {
		log(LOG_WARN, "inject: no coll rec");
		return;
	}

	// use this, is more reliable, "coll" can disappear from under us
	ir->m_collnum = cr->m_collnum;

	// scan the parms
	for ( int i = 0 ; i < g_parms.getNumParms(); i++ ) {
		Parm *m = g_parms.getParm(i);
		if ( m->m_obj != OBJ_IR ) continue;
		// get it
		if ( m->m_type == TYPE_CHARPTR ||
		     m->m_type == TYPE_FILEUPLOADBUTTON ) {
			int32_t stringLen;
			const char *str =hr->getString(m->m_cgi,&stringLen,m->m_def);
			// avoid overwriting the "url" parm with the "u" parm
			// since it is just an alias
			if ( ! str ) continue;
			// serialize it as a string
			char *foo = (char *)ir + m->m_off;
			char **ptrPtr = (char **)foo;
			// store the ptr pointing into hr buf for now
			*ptrPtr = (char*)str;
			// how many strings are we past ptr_url?
			int32_t count = ptrPtr - &ir->ptr_url;
			// and length. include \0
			int32_t *sizePtr = &ir->size_url + count;
			*sizePtr = stringLen + 1;
			continue;
		}
		// numbers are easy
		else if ( m->m_type == TYPE_INT32 ) {
			int32_t *ii = (int32_t *)((char *)ir + m->m_off);
			int32_t def = atoll(m->m_def);
			*ii = hr->getLong(m->m_cgi,def);
		}
		else if ( m->m_type == TYPE_CHECKBOX || 
			  m->m_type == TYPE_BOOL ) {
			char *ii = (char *)((char *)ir + m->m_off);
			int32_t def = atoll(m->m_def);
			*ii = (char)hr->getLong(m->m_cgi,def);
		}
		else if ( m->m_type == TYPE_IP ) {
			char *ii = (char *)((char *)ir + m->m_off);
			const char *is = hr->getString(m->m_cgi,NULL);
			*(int32_t *)ii = 0; // default ip to 0
			// otherwise, set the ip
			if ( is ) *(int32_t *)ii = atoip(is);
		}
		// if unsupported let developer know
		else { g_process.shutdownAbort(true); }
	}


	// if content is "" make it NULL so XmlDoc will download it
	// if user really wants empty content they can put a space in there
	// TODO: update help then...
	if ( ir->ptr_content && ! ir->ptr_content[0]  )
		ir->ptr_content = NULL;

	if ( ir->ptr_url && ! ir->ptr_url[0] ) 
		ir->ptr_url = NULL;
}

static Host *getHostToHandleInjection(char *url) {
	Url norm;
	norm.set(url);

	int64_t docId = Titledb::getProbableDocId ( &norm );
	uint32_t shardNum = getShardNumFromDocId(docId);
	Host *host = g_hostdb.getHostWithSpideringEnabled(shardNum);

	bool isWarcInjection = false;
	size_t ulen = strlen(url);
	if (ulen > 10 && (strcmp(url + ulen - 8, ".warc.gz") == 0 || strcmp(url + ulen - 5, ".warc") == 0)) {
		isWarcInjection = true;
	}

	if (!isWarcInjection) {
		return host;
	}

	// warc files end up calling XmlDoc::indexWarcOrArc() which spawns
	// a msg7 injection request for each doc in the warc/arc file
	// so let's do load balancing differently for them so one host
	// doesn't end up doing a bunch of wget/gunzips on warc files 
	// thereby bottlenecking the cluster.

	// old logic:
	// get the first hostid that we have not sent a msg7 injection request to that is still out

	// new logic:
	// replaced with simpler logic of hashing the url and mod it with number of shards
	/// @todo ALC we may want to replace this with logic to pick least loaded shard instead of least loaded in shard
	return g_hostdb.getLeastLoadedInShard(static_cast<uint32_t>(norm.getUrlHash64() % g_hostdb.getNumShards()), 0);
}

static void gotUdpReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg7 *THIS = (Msg7 *)state;
	THIS->gotUdpReply(slot);
}

void Msg7::gotUdpReply ( UdpSlot *slot ) {

	// dont' free the sendbuf that is Msg7::m_sir/m_sirSize. msg7 will
	// free it. 
	slot->m_sendBufAlloc = NULL;

	m_replyIndexCode = EBADENGINEER;
	if ( slot->m_readBuf && slot->m_readBufSize >= 12 ) {
		m_replyIndexCode = *(int32_t *)(slot->m_readBuf);
		m_replyDocId     = *(int64_t *)(slot->m_readBuf+4);
	}

	m_callback ( m_state );
}

// . "sir" is the serialized injectionrequest
// . this is called from the http interface, as well as from
//   XmlDoc::indexWarcOrArc() to inject individual recs/docs from the warc/arc
// . returns false and sets g_errno on error, true on success
bool Msg7::sendInjectionRequestToHost ( InjectionRequest *ir , 
					void *state ,
					void (* callback)(void *) ) {

	// ensure it is our own
	if ( &m_injectionRequest != ir ) { g_process.shutdownAbort(true); }

	// ensure url not beyond limit
	if ( ir->ptr_url &&
	     strlen(ir->ptr_url) > MAX_URL_LEN ) {
		g_errno = EURLTOOBIG;
		log(LOG_WARN, "inject: url too big.");
		return false;
	}

	int32_t sirSize = 0;
	char *sir = serializeMsg2 ( ir ,
				    sizeof(InjectionRequest),
				    &ir->ptr_url,
				    &ir->size_url ,
				    &sirSize );
	// oom?
	if ( ! sir ) {
		log(LOG_WARN, "inject: failed to serialize request");
		return false;
	}

	// free any old one if we are being reused
	if ( m_sir ) {
		mfree ( m_sir , m_sirSize , "m7ir" );
		m_sir = NULL;
	}

	m_state = state;
	m_callback = callback;

	// save it for freeing later
	m_sir = sir;
	m_sirSize = sirSize;

	// forward it to another shard?
	Host *host = getHostToHandleInjection ( ir->ptr_url );

	log(LOG_DEBUG, "inject: sending injection request of url %s reqsize=%i to host #%" PRId32,
	    ir->ptr_url,(int)sirSize,host->m_hostId);

	// . ok, forward it to another host now
	// . and call got gotForwardedReplyWrapper when reply comes in
	// . returns false and sets g_errno on error
	// . returns true on success
	if (g_udpServer.sendRequest(sir, sirSize, msg_type_7, host->m_ip, host->m_port, host->m_hostId, NULL, this, gotUdpReplyWrapper, udpserver_sendrequest_infinite_timeout, MAX_NICENESS)) {
		// we also return true on success, false on error
		return true;
	}

	if ( ! g_errno ) { g_process.shutdownAbort(true); }
	// there was an error, g_errno should be set
	return false;
}

static void sendHttpReplyWrapper(void *state) {
	sendHttpReply ( state );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we are called by Parms::sendPageGeneric() to handle this request
//   which was called by Pages.cpp's sendDynamicReply() when it calls 
//   pg->function() which is called by HttpServer::sendReply(s,r) when it 
//   gets an http request
// . so "hr" is on the stack in HttpServer::requestHandler() which calls
//   HttpServer::sendReply() so we gotta copy it here
bool sendPageInject ( TcpSocket *sock , HttpRequest *hr ) {

	if ( ! g_conf.m_injectionsEnabled ) {
		g_errno = EINJECTIONSDISABLED;
		log(LOG_WARN, "inject: injection disabled");
		return g_httpServer.sendErrorReply(sock,500,"injection is "
						   "disabled by "
						   "the administrator in "
						   "the master "
						   "controls");
	}

	char format = hr->getReplyFormat();
	const char *coll  = hr->getString("c",NULL);

	// no url parm?
	if ( format != FORMAT_HTML && ! coll ) {
		g_errno = ENOCOLLREC;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	if ( g_repairMode ) { 
		g_errno = EREPAIRING;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( sock , hr );
	bool isCollAdmin = g_conf.isCollAdmin ( sock , hr );
	if ( ! isMasterAdmin && ! isCollAdmin ) {
		g_errno = ENOPERM;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	// make a new state
	Msg7 *msg7;
	try { msg7= new (Msg7); }
	catch(std::bad_alloc&) {
		g_errno = ENOMEM;
		log(LOG_WARN, "PageInject: new(%i): %s", (int)sizeof(Msg7),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}
	mnew ( msg7, sizeof(Msg7) , "PageInject" );

	// save some state info into msg7 directly
	msg7->m_socket = sock;
	msg7->m_format = format;
	msg7->m_replyIndexCode = 0;
	msg7->m_replyDocId = 0;
	
	msg7->m_hr.copy ( hr );

	// use Parms.cpp like how we set GigablastRequest to initialize parms
	// from the http request. i.e. setGigablastRequest(). 
	// the InjectionRequest::ptr_*  members will reference into
	// msg7->m_hr buffers so they should be ok.
	InjectionRequest *ir = &msg7->m_injectionRequest;
	setInjectionRequestFromParms (sock, &msg7->m_hr, cr, ir );

	// if no url do not inject
	if ( ! ir->ptr_url )
		return sendHttpReply ( msg7 );

	// when we receive the udp reply then send back the http reply
	// we return true on success, which means it blocked... so return false
	if ( msg7->sendInjectionRequestToHost(ir,msg7,sendHttpReplyWrapper)) 
		return false;

	if ( ! g_errno ) {
		log(LOG_ERROR, "inject: blocked with no error!");
		g_process.shutdownAbort(true); 
	}
		
	// error?
	log(LOG_INFO, "inject: error forwarding reply: %s (%i)", mstrerror(g_errno), g_errno);

	// it did not block, i gues we are done
	return sendHttpReply ( msg7 );
}

bool sendHttpReply ( void *state ) {
	// get the state properly
	Msg7 *msg7= (Msg7 *) state;

	InjectionRequest *ir = &msg7->m_injectionRequest;

	// extract info from state
	TcpSocket *sock = msg7->m_socket;

	int64_t docId  = msg7->m_replyDocId; // xd->m_docId;

	// might already be EURLTOOBIG set from above
	if ( ! g_errno ) g_errno = msg7->m_replyIndexCode;

	int32_t      hostId = 0;

	char format = msg7->m_format;

	// no url parm?
	if ( ! g_errno && ! ir->ptr_url && format != FORMAT_HTML )
		g_errno = EMISSINGINPUT;

	if ( g_errno && g_errno != EDOCUNCHANGED ) {
		int32_t save = g_errno;
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		g_errno = save;
		const char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,save,msg,NULL);
	}

	StackBuf<320> am;
	am.setLabel("injbuf");
	const char *ct = NULL;

	// a success reply, include docid and url i guess
	if ( format == FORMAT_XML ) {
		am.safePrintf("<response>\n");
		am.safePrintf("\t<statusCode>%" PRId32"</statusCode>\n",
			      (int32_t)g_errno);
		am.safePrintf("\t<statusMsg><![CDATA[");
		cdataEncode(&am, mstrerror(g_errno));
		am.safePrintf("]]></statusMsg>\n");

		am.safePrintf("\t<docId>%" PRId64"</docId>\n",docId);

		am.safePrintf("</response>\n");
		ct = "text/xml";
	}

	if ( format == FORMAT_JSON ) {
		am.safePrintf("{\"response\":{\n");
		am.safePrintf("\t\"statusCode\":%" PRId32",\n",(int32_t)g_errno);
		am.safePrintf("\t\"statusMsg\":\"");
		am.jsonEncode(mstrerror(g_errno));
		am.safePrintf("\",\n");
		am.safePrintf("\t\"docId\":%" PRId64",\n",docId);//xd->m_docId);

		// subtract ",\n"
		am.m_length -= 2;
		am.safePrintf("\n}\n}\n");
		ct = "application/json";
	}

	if ( format == FORMAT_XML || format == FORMAT_JSON ) {
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		return g_httpServer.sendDynamicPage(sock,
						    am.getBufStart(),
						    am.length(),
						    0,
						    false,
						    ct );
	}

	char *url = ir->ptr_url;
	
	// . if we're talking w/ a robot he doesn't care about this crap
	// . send him back the error code (0 means success)
	if ( url && ir->m_shortReply ) {
		char buf[1024*32];
		char *p = buf;
		// return docid and hostid
		if ( ! g_errno ) p += sprintf ( p , 
						"0,docId=%" PRId64","
						"hostId=%" PRId32"," , 
						docId , hostId );
		// print error number here
		else  p += sprintf ( p , "%" PRId32",0,0,", (int32_t)g_errno );
		// print error msg out, too or "Success"
		p += sprintf ( p , "%s", mstrerror(g_errno));
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		return g_httpServer.sendDynamicPage ( sock,buf, strlen(buf) ,
						      -1/*cachetime*/);
	}

	SafeBuf sb;

	// print admin bar
	g_pages.printAdminTop ( &sb, sock , &msg7->m_hr );

	// print a response msg if rendering the page after a submission
	if ( g_errno ) {
		sb.safePrintf ( "<center>Error injecting url: <b>%s[%i]</b></center>",
		                mstrerror(g_errno) , g_errno);
	} else if ( ir->ptr_url && ir->ptr_url[0] ) {
		sb.safePrintf ( "<center><b>Sucessfully injected %s</center><br>", ir->ptr_url);
	}

	// print the table of injection parms
	g_parms.printParmTable ( &sb , sock , &msg7->m_hr );

	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;

	// nuke state
	mdelete ( msg7, sizeof(Msg7) , "PageInject" );
	delete (msg7);
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	// . i thought we need -2 for cacheTime, but i guess not
	return g_httpServer.sendDynamicPage (sock, 
					     sb.getBufStart(),
					     sb.length(), 
					     -1/*cachetime*/);
}

/////////////
//
// HANDLE INCOMING UDP INJECTION REQUEST
//
////////////

static XmlDoc *s_injectHead = NULL;
static XmlDoc *s_injectTail = NULL;

XmlDoc *getInjectHead() { return s_injectHead; }

// send back a reply to the originator of the msg7 injection request
static void sendUdpReply7(void *state) {
	XmlDoc *xd = (XmlDoc *)state;

	// remove from linked list
	if ( xd->m_nextInject ) 
		xd->m_nextInject->m_prevInject = xd->m_prevInject;
	if ( xd->m_prevInject )
		xd->m_prevInject->m_nextInject = xd->m_nextInject;
	if ( s_injectHead == xd )
		s_injectHead = xd->m_nextInject;
	if ( s_injectTail == xd )
		s_injectTail = xd->m_prevInject;
	xd->m_nextInject = NULL;
	xd->m_prevInject = NULL;

	UdpSlot *slot = xd->m_injectionSlot;

	// injecting a warc seems to not set m_indexCodeValid to true
	// for the container doc... hmmm...
	int32_t indexCode = -1;
	int64_t docId = 0;
	if ( xd->m_indexCodeValid ) indexCode = xd->m_indexCode;
	if ( xd->m_docIdValid     ) docId = xd->m_docId;
	mdelete ( xd, sizeof(XmlDoc) , "PageInject" );
	delete (xd);


	if ( g_errno ) {
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}
	// just send back the 4 byte indexcode, which is 0 on success,
	// otherwise it is the errno
	char *tmp = slot->m_shortSendBuffer;
	char *p = tmp;
	memcpy ( p , (char *)&indexCode , 4 );
	p += 4;
	memcpy ( p , (char *)&docId , 8 );
	p += 8;

	g_udpServer.sendReply(tmp,(p-tmp),NULL,0,slot);
}

void handleRequest7 ( UdpSlot *slot , int32_t netnice ) {
	InjectionRequest *ir = (InjectionRequest *)slot->m_readBuf;

	// now just supply the first guy's char ** and size ptr
	if ( ! deserializeMsg2 ( &ir->ptr_url, &ir->size_url ) ) {
		char ipbuf[16];
		log(LOG_WARN, "inject: error deserializing inject request from host ip %s port %i",
		    iptoa(slot->getIp(),ipbuf), (int)slot->getPort());
		g_errno = EBADREQUEST;
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}
		

	// the url can be like xyz.com. so need to do another corruption test for ia
	if (!ir->ptr_url) {
		log(LOG_WARN, "inject: trying to inject NULL url.");
		g_errno = EBADURL;
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}

	CollectionRec *cr = g_collectiondb.getRec ( ir->m_collnum );
	if (!cr) {
		log(LOG_WARN, "inject: cr rec is null %i", ir->m_collnum);
		g_errno = ENOCOLLREC;
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}

	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch(std::bad_alloc&) {
		g_errno = ENOMEM;
		log(LOG_WARN, "PageInject: import failed: new(%i): %s", (int)sizeof(XmlDoc),mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}
	mnew ( xd, sizeof(XmlDoc) , "PageInject" );

	xd->m_injectionSlot = slot;

	// add to linked list
	xd->m_nextInject = NULL;
	xd->m_prevInject = NULL;
	if ( s_injectTail ) {
		s_injectTail->m_nextInject = xd;
		xd->m_prevInject = s_injectTail;
		s_injectTail = xd;
	}
	else {
		s_injectHead = xd;
		s_injectTail = xd;
	}
	if(ir->ptr_content && ir->ptr_content[ir->size_content - 1]) {
		// XmlDoc expects this buffer to be null terminated.
		g_process.shutdownAbort(true);
	}

	if ( ! xd->injectDoc ( ir->ptr_url ,
			       cr ,
			       ir->ptr_content ,
			       // if this doc is a 'container doc' then
			       // hasMime applies to the SUBDOCS only!!
			       ir->m_hasMime, // content starts with http mime?
			       ir->m_charset,
			       ir->m_langId,
			       ir->m_deleteUrl,
			       // warcs/arcs include the mime so we don't
			       // look at this in that case in 
			       // XmlDoc::injectDoc() when it calls set4()
			       ir->ptr_contentTypeStr, // text/html text/xml
			       ir->m_spiderLinks ,
			       ir->m_newOnly, // index iff new
			       ir->m_skipContentHashCheck,
			       xd, // state ,
			       sendUdpReply7 ,

			       // extra shit
			       ir->m_firstIndexed,
			       ir->m_lastSpidered ,
			       // the ip of the url being injected.
			       // use 0 if unknown and it won't be valid.
			       ir->m_injectDocIp
			       ) )
		// we blocked...
		return;

	// if injected without blocking, send back reply
	sendUdpReply7 ( xd );
}



//////////////////
//
// TITLEREC INJECT IMPORT CODE
//
//////////////////

Msg7::Msg7 () {
	m_xd = NULL;
	m_sir = NULL;
	m_inUse = false;
	reset();
}

Msg7::~Msg7 () {
	reset();
}

void Msg7::reset() { 
	m_round = 0;
	m_sbuf.reset();

	if ( m_xd ) {
		mdelete ( m_xd, sizeof(XmlDoc) , "PageInject" );
		delete (m_xd);
		m_xd = NULL;
	}

	if ( m_sir ) {
		mfree ( m_sir , m_sirSize , "m7ir" );
		m_sir = NULL;
	}

	// Coverity
	memset(&m_injectionRequest, 0, sizeof(m_injectionRequest));
	m_startTime = 0;
	m_replyIndexCode = 0;
	m_replyDocId = 0;
	m_sirSize = 0;
	m_needsSet = false;
	m_socket = NULL;
	m_state = NULL;
	m_callback = NULL;
	m_format = 0;
	m_stashxd = NULL;
}
