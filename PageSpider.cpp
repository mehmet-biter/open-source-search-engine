#include "Msg5.h"
#include "HttpRequest.h"
#include "RdbList.h"
#include "SafeBuf.h"
#include "HttpServer.h"
#include "Collectiondb.h"
#include "Doledb.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "SpiderCache.h"
#include "XmlDoc.h"
#include "Pages.h"
#include "PageInject.h"
#include "ScopedLock.h"
#include "Process.h"
#include "ip.h"
#include "Mem.h"

namespace {

	class State11 {
	public:
		int32_t          m_numRecs;
		Msg5          m_msg5;
		RdbList       m_list;
		TcpSocket    *m_socket;
		HttpRequest   m_r;
		collnum_t     m_collnum;
		const char   *m_coll;
		int32_t          m_count;
		key96_t         m_startKey;
		key96_t         m_endKey;
		int32_t          m_minRecSizes;
		bool          m_done;
		SafeBuf       m_safeBuf;
		int32_t          m_priority;
	};

} //namespace

static bool loadLoop ( class State11 *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the urls we got in doledb
// . doledb is sorted by priority complement then spider time
// . do not show urls in doledb whose spider time has not yet been reached,
//   so only show the urls spiderable now
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSpiderdb ( TcpSocket *s , HttpRequest *r ) {
	// set up a msg5 and RdbLists to get the urls from spider queue
	State11 *st ;
	try { st = new (State11); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageSpiderdb: new(%i): %s",
		    (int)sizeof(State11),mstrerror(g_errno));
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State11) , "PageSpiderdb" );
	// get the priority/#ofRecs from the cgi vars
	st->m_numRecs  = r->getLong ("n", 20  );
	st->m_r.copy ( r );
	// get collection name
	const char *coll = st->m_r.getString ( "c" , NULL , NULL );
	// get the collection record to see if they have permission
	//CollectionRec *cr = g_collectiondb.getRec ( coll );

	// the socket read buffer will remain until the socket is destroyed
	// and "coll" points into that
	st->m_coll = coll;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;
	// set socket for replying in case we block
	st->m_socket = s;
	st->m_count = 0;
	st->m_priority = MAX_SPIDER_PRIORITIES - 1;
	// get startKeys/endKeys/minRecSizes
	st->m_startKey    = Doledb::makeFirstKey2 (st->m_priority);
	st->m_endKey      = Doledb::makeLastKey2  (st->m_priority);
	st->m_minRecSizes = 20000;
	st->m_done        = false;
	// returns false if blocked, true otherwise
	return loadLoop ( st ) ;
}

static void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) ;
static bool sendPage        ( State11 *st );
static bool printList       ( State11 *st );

static bool loadLoop ( State11 *st ) {
loop:
	// let's get the local list for THIS machine (use msg5)
	if ( ! st->m_msg5.getList  ( RDB_DOLEDB          ,
	                             st->m_collnum       ,
	                             &st->m_list         ,
	                             st->m_startKey      ,
	                             st->m_endKey        ,
	                             st->m_minRecSizes   ,
	                             true                , // include tree
	                             0                   , // max age
	                             0                   , // start file #
	                             -1                  , // # files
	                             st                  , // callback state
	                             gotListWrapper3     ,
	                             0                   , // niceness
	                             true,                 // do err correction
	                             NULL,                 // cacheKeyPtr
	                             0,                    // retryNum
	                             -1,                   // maxRetries
	                             -1,                   // syncPoint
	                             false,                // isRealMerge
	                             true))                // allowPageCache
		return false;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return true;
	}
	// otherwise, load more
	goto loop;
}

static void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// cast it
	State11 *st = (State11 *)state;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return;
	}
	// otherwise, load more
	loadLoop( (State11 *)state );
}


// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
static bool printList ( State11 *st ) {
	// useful
	time_t nowGlobal = getTime();

	// print the spider recs we got
	SafeBuf *sbTable = &st->m_safeBuf;
	// shorcuts
	RdbList *list = &st->m_list;
	// row count
	int32_t j = 0;
	// put it in there
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// stop if we got enough
		if ( st->m_count >= st->m_numRecs )  break;
		// get the doledb key
		key96_t dk = list->getCurrentKey();
		// update to that
		st->m_startKey = dk;
		// inc by one
		st->m_startKey++;
		// get spider time from that
		int32_t spiderTime = Doledb::getSpiderTime ( &dk );
		// skip if in future
		if ( spiderTime > nowGlobal ) continue;
		// point to the spider request *RECORD*
		char *rec = list->getCurrentData();
		// skip negatives
		if ( (dk.n0 & 0x01) == 0 ) continue;
		// count it
		st->m_count++;
		// what is this?
		if ( list->getCurrentRecSize() <= 16 ) { g_process.shutdownAbort(true);}
		// sanity check. requests ONLY in doledb
		if ( ! Spiderdb::isSpiderRequest ( (key128_t *)rec )) {
			log("spider: not printing spiderreply");
			continue;
			//g_process.shutdownAbort(true);
		}
		// get the spider rec, encapsed in the data of the doledb rec
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// print it into sbTable
		if ( ! sreq->printToTable ( sbTable,"ready",NULL,j))
			return false;
		// count row
		j++;
	}
	// need to load more?
	if ( st->m_count >= st->m_numRecs ||
	     // if list was a partial, this priority is short then
	     list->getListSize() < st->m_minRecSizes ) {
		// . try next priority
		// . if below 0 we are done
		if ( --st->m_priority < 0 ) st->m_done = true;
		// get startKeys/endKeys/minRecSizes
		st->m_startKey    = Doledb::makeFirstKey2 (st->m_priority);
		st->m_endKey      = Doledb::makeLastKey2  (st->m_priority);
		// if we printed something, print a blank line after it
		if ( st->m_count > 0 )
			sbTable->safePrintf("<tr><td colspan=30>..."
				                    "</td></tr>\n");
		// reset for each priority
		st->m_count = 0;
	}


	return true;
}

static bool sendPage(State11 *st) {
	// generate a query string to pass to host bar
	char qs[64]; sprintf ( qs , "&n=%" PRId32, st->m_numRecs );

	// store the page in here!
	SafeBuf sb;
	if( !sb.reserve ( 64*1024 ) ) {
		logError("Could not reserve needed mem, bailing!");
		return false;
	}

	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r , qs );


	// get spider coll
	collnum_t collnum = g_collectiondb.getCollnum ( st->m_coll );
	// and coll rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );

	if ( ! cr ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
		                                     sb.length() );
	}

	// print reason why spiders are not active for this collection
	int32_t tmp2;
	SafeBuf mb;
	if ( cr ) getSpiderStatusMsg ( cr , &mb , &tmp2 );
	if ( mb.length() && tmp2 != SP_INITIALIZING )
		sb.safePrintf(//"<center>"
			"<table cellpadding=5 "
				//"style=\""
				//"border:2px solid black;"
				"max-width:600px\" "
				"border=0"
				">"
				"<tr>"
				//"<td bgcolor=#ff6666>"
				"<td>"
				"For collection <i>%s</i>: "
				"<b><font color=red>%s</font></b>"
				"</td>"
				"</tr>"
				"</table>\n"
			, cr->m_coll
			, mb.getBufStart() );


	// begin the table
	sb.safePrintf ( "<table %s>\n"
		                "<tr><td colspan=50>"
		                //"<center>"
		                "<b>Currently Spidering on This Host</b>"
		                " (%" PRId32" spiders)"
		//" (%" PRId32" locks)"
		//"</center>"
		"</td></tr>\n"
		, TABLE_STYLE
		, (int32_t)g_spiderLoop.m_numSpidersOut
	//, g_spiderLoop.m_lockTable.m_numSlotsUsed
	);
	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb , true ) ) return false;
	// count # of spiders out
	int32_t j = 0;
	// first print the spider recs we are spidering
	for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
		// get it
		XmlDoc *xd = g_spiderLoop.m_docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// sanity check
		if ( ! xd->m_sreqValid ) { g_process.shutdownAbort(true); }
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		const char *status = xd->m_statusMsg;
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}
	// now print the injections as well!
	XmlDoc *xd = getInjectHead ( ) ;
	for ( ; xd ; xd = xd->m_nextInject ) {
		// how does this happen?
		if ( ! xd->m_sreqValid ) continue;
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		SafeBuf xb;
		xb.safePrintf("[<font color=red><b>injecting</b></font>] %s",
		              xd->m_statusMsg);
		char *status = xb.getBufStart();
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}

	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// then spider collection
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);


	//
	// spiderdb rec stats, from scanning spiderdb
	//

	// if not there, forget about it
	if ( sc ) sc->printStats ( sb );

	// done if no sc
	if ( ! sc ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
		                                     sb.length() );
	}

	/////
	//
	// READY TO SPIDER table
	//
	/////

	int32_t ns = 0;
	if ( sc ) ns = sc->m_doleIpTable.getNumSlotsUsed();

	// begin the table
	sb.safePrintf ( "<table %s>\n"
		                "<tr><td colspan=50>"
		                "<b>URLs Ready to Spider for collection "
		                "<font color=red><b>%s</b>"
		                "</font>"
		                " (%" PRId32" ips in doleiptable)"
		,
		TABLE_STYLE,
		st->m_coll ,
		ns );

	// print time format: 7/23/1971 10:45:32
	time_t nowUTC = getTimeGlobal();
	struct tm *timeStruct ;
	char time[256];
	struct tm tm_buf;
	timeStruct = gmtime_r(&nowUTC,&tm_buf);
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb.safePrintf("</b>" //  (current time = %s = %" PRIu32") "
		              "</td></tr>\n"
		//,time,nowUTC
	);

	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb ,false ) ) return false;
	// the the doledb spider recs
	char *bs = st->m_safeBuf.getBufStart();
	if ( bs && ! sb.safePrintf("%s",bs) ) return false;
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );



	/////////////////
	//
	// PRINT WAITING TREE
	//
	// each row is an ip. print the next url to spider for that ip.
	//
	/////////////////
	sb.safePrintf ( "<table %s>\n"
		                "<tr><td colspan=50>"
		                "<b>IPs Waiting for Selection Scan for collection "
		                "<font color=red><b>%s</b>"
		                "</font>"
		,
		            TABLE_STYLE,
		            st->m_coll );
	// print time format: 7/23/1971 10:45:32
	int64_t timems = gettimeofdayInMilliseconds();
	sb.safePrintf("</b> (current time = %" PRIu64")(totalcount=%" PRId32")"
		"(waittablecount=%" PRId32")",
		timems,
		sc->m_waitingTree.getNumUsedNodes(),
		sc->m_waitingTable.getNumUsedSlots());

	double a = (double)Spiderdb::getUrlHash48 ( &sc->m_firstKey );
	double b = (double)Spiderdb::getUrlHash48 ( &sc->m_endKey );
	double c = (double)Spiderdb::getUrlHash48 ( &sc->m_nextKey );
	double percent = (100.0 * (c-a)) ;
	if ( b-a > 0 ) percent /= (b-a);
	if ( percent > 100.0 ) percent = 100.0;
	if ( percent < 0.0 ) percent = 0.0;
	sb.safePrintf("(spiderdb scan for ip %s is %.2f%% complete)",
	              iptoa(sc->getScanningIp()),
	              (float)percent );

	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("<tr bgcolor=#%s>",DARK_BLUE);
	sb.safePrintf("<td><b>spidertime (MS)</b></td>\n");
	sb.safePrintf("<td><b>firstip</b></td>\n");
	sb.safePrintf("</tr>\n");
	// the the waiting tree

	int32_t count = 0;
	{
		ScopedLock sl(sc->m_waitingTree.getLock());
		for (int32_t node = sc->m_waitingTree.getFirstNode_unlocked(); node >= 0;
		     node = sc->m_waitingTree.getNextNode_unlocked(node)) {
			// get key
			const key96_t *key = reinterpret_cast<const key96_t *>(sc->m_waitingTree.getKey_unlocked(node));
			// get ip from that
			int32_t firstIp = (key->n0) & 0xffffffff;
			// get the timedocs
			uint64_t spiderTimeMS = key->n1;
			// shift upp
			spiderTimeMS <<= 32;
			// or in
			spiderTimeMS |= (key->n0 >> 32);
			const char *note = "";

			// get the rest of the data
			sb.safePrintf("<tr bgcolor=#%s>"
				              "<td>%" PRId64"%s</td>"
				"<td>%s</td>"
				"</tr>\n",
				LIGHT_BLUE,
				(int64_t)spiderTimeMS,
				note,
				iptoa(firstIp));
			// stop after 20
			if (++count == 20) break;
		}
	}
	// ...
	if ( count )
		sb.safePrintf("<tr bgcolor=#%s>"
			              "<td colspan=10>...</td></tr>\n",
		              LIGHT_BLUE);
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// get the socket
	TcpSocket *s = st->m_socket;
	// then we can nuke the state
	mdelete ( st , sizeof(State11) , "PageSpiderdb" );
	delete (st);
	// erase g_errno for sending
	g_errno = 0;
	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(),sb.length() );
}