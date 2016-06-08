// diffbot api implementaion

//
// WHAT APIs are here?
//
// . 1. the CrawlBot API to start a crawl 
// . 2. To directly process a provided URL (injection)
// . 3. the Cache API so phantomjs can quickly check the cache for files
//      and quickly add files to the cache.
//

// Related pages:
//
// * http://diffbot.com/dev/docs/  (Crawlbot API tab, and others)
// * http://diffbot.com/dev/crawl/

#include "Errno.h"
#include "PageCrawlBot.h"
#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h" // g_msg
#include "PageInject.h" // Msg7
#include "Repair.h"
#include "Parms.h"
#include "SpiderLoop.h"

// so user can specify the format of the reply/output
//#define FMT_HTML 1
//#define FMT_XML  2
//#define FMT_JSON 3
//#define FMT_CSV  4
//#define FMT_TXT  5

void doneSendingWrapper ( void *state , TcpSocket *sock ) ;
bool sendBackDump ( TcpSocket *s,HttpRequest *hr );
CollectionRec *addNewDiffbotColl ( char *addColl , char *token,char *name ,
				   class HttpRequest *hr ) ;
bool resetUrlFilters ( CollectionRec *cr ) ;

bool setSpiderParmsFromHtmlRequest ( TcpSocket *socket ,
				     HttpRequest *hr , 
				     CollectionRec *cr ) ;


////////////////
//
// SUPPORT FOR DOWNLOADING an RDB DUMP
//
// We ask each shard for 10MB of Spiderdb records. If 10MB was returned
// then we repeat. Everytime we get 10MB from each shard we print the
// Spiderdb records out into "safebuf" and transmit it to the user. once
// the buffer has been transmitted then we ask the shards for another 10MB
// worth of spider records.
//
////////////////


// use this as a state while dumping out spiderdb for a collection
class StateCD {
public:
	StateCD () { m_needsMime = true; }
	void sendBackDump2 ( ) ;
	bool readDataFromRdb ( ) ;
	bool sendList ( ) ;
	void printSpiderdbList ( RdbList *list , SafeBuf *sb ,
				 char **lastKeyPtr ) ;
	void printTitledbList ( RdbList *list , SafeBuf *sb ,
				char **lastKeyPtr );

	int64_t m_lastUh48;
	int32_t m_lastFirstIp;
	int64_t m_prevReplyUh48;
	int32_t m_prevReplyFirstIp;
	int32_t m_prevReplyError;
	time_t m_prevReplyDownloadTime;

	char m_fmt;
	Msg4 m_msg4;
	HttpRequest m_hr;
	Msg7 m_msg7;
	int32_t m_dumpRound;
	int64_t m_accumulated;

	WaitEntry m_waitEntry;

	bool m_isFirstTime;
	bool m_printedFirstBracket;
	bool m_printedEndingBracket;
	bool m_printedItem;

	bool m_needHeaderRow;

	SafeBuf m_seedBank;
	SafeBuf m_listBuf;

	bool m_needsMime;
	char m_rdbId;
	bool m_downloadJSON;
	collnum_t m_collnum;
	int32_t m_numRequests;
	int32_t m_numReplies;
	int32_t m_minRecSizes;
	bool m_someoneNeedsMore;
	TcpSocket *m_socket;
	Msg0 m_msg0s[MAX_HOSTS];
	key128_t m_spiderdbStartKeys[MAX_HOSTS];
	key_t m_titledbStartKeys[MAX_HOSTS];
	RdbList m_lists[MAX_HOSTS];
	bool m_needMore[MAX_HOSTS];

};

// . basically dump out spiderdb
// . returns urls in csv format in reply to a 
//   "GET /api/download/%s_data.json"
//   "GET /api/download/%s_data.xml"
//   "GET /api/download/%s_urls.csv"
//   "GET /api/download/%s_pages.txt"
//   where %s is the collection name
// . the ordering of the urls is not specified so whatever order they are
//   in spiderdb will do
// . the gui that lists the urls as they are spidered in real time when you
//   do a test crawl will just have to call this repeatedly. it shouldn't
//   be too slow because of disk caching, and, most likely, the spider requests
//   will all be in spiderdb's rdbtree any how
// . because we are distributed we have to send a msg0 request to each 
//   shard/group asking for all the spider urls. dan says 30MB is typical
//   for a csv file, so for now we will just try to do a single spiderdb
//   request.
bool sendBackDump ( TcpSocket *sock, HttpRequest *hr ) {

	char *path = hr->getPath();
	int32_t pathLen = hr->getPathLen();
	char *pathEnd = path + pathLen;

	char *str = strstr ( path , "/download/" );
	if ( ! str ) {
		const char *msg = "bad download request";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}

	// when downloading csv socket closes because we can take minutes
	// before we send over the first byte, so try to keep open
	//int parm = 1;
	//if(setsockopt(sock->m_sd,SOL_TCP,SO_KEEPALIVE,&parm,sizeof(int))<0){
	//	log("crawlbot: setsockopt: %s",mstrerror(errno));
	//	errno = 0;
	//}

	//int32_t pathLen = hr->getPathLen();
	char rdbId = RDB_NONE;
	bool downloadJSON = false;
	int32_t fmt;
	char *xx;
	int32_t dt = CT_JSON;

	if ( ( xx = strstr ( path , "_data.json" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_JSON;
		downloadJSON = true;
		dt = CT_JSON;
	}
	else if ( ( xx = strstr ( path , "_html.json" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_JSON;
		downloadJSON = true;
		dt = CT_HTML;
	}
	else if ( ( xx = strstr ( path , "_data.csv" ) ) ) {
		rdbId = RDB_TITLEDB;
		downloadJSON = true;
		fmt = FORMAT_CSV;
	}
	else if ( ( xx = strstr ( path , "_urls.csv" ) ) ) {
		rdbId = RDB_SPIDERDB;
		fmt = FORMAT_CSV;
	}
	else if ( ( xx = strstr ( path , "_urls.txt" ) ) ) {
		rdbId = RDB_SPIDERDB;
		fmt = FORMAT_TXT;
	}
	else if ( ( xx = strstr ( path , "_pages.txt" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_TXT;
	}

	// sanity, must be one of 3 download calls
	if ( rdbId == RDB_NONE ) {
		const char *msg = "usage: downloadurls, downloadpages, downloaddata";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}


	char *coll = str + 10;
	if ( coll >= pathEnd ) {
		const char *msg = "bad download request2";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}

	// get coll
	char *collEnd = xx;

	//CollectionRec *cr = getCollRecFromHttpRequest ( hr );
	CollectionRec *cr = g_collectiondb.getRec ( coll , collEnd - coll );
	if ( ! cr ) {
		const char *msg = "token or id (crawlid) invalid";
		log("crawlbot: invalid token or crawlid to dump");
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}



	// . if doing download of csv, make it search results now!
	// . make an httprequest on stack and call it
	if ( fmt == FORMAT_CSV && rdbId == RDB_TITLEDB ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=csv&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%" PRId32"&"
			       "c=%s&n=1000000&"
			       // stream it now
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      "\r\n\r\n"
			       , dr
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	// . if doing download of json, make it search results now!
	// . make an httprequest on stack and call it
	if ( fmt == FORMAT_JSON && rdbId == RDB_TITLEDB && dt == CT_HTML ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=json&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%" PRId32"&"
			       "c=%s&n=1000000&"
			       // we can stream this because unlink csv it
			       // has no header row that needs to be 
			       // computed from all results.
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			       //"q=gbsortby%%3Agbspiderdate&"
			       //"prepend=type%%3A%s"
			       "q=type%%3Ahtml"
			      "\r\n\r\n"
			       , dr 
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	if ( fmt == FORMAT_JSON && rdbId == RDB_TITLEDB ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=json&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%" PRId32"&"
			       "c=%s&n=1000000&"
			       // we can stream this because unlink csv it
			       // has no header row that needs to be 
			       // computed from all results.
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			       "q=gbsortby%%3Agbspiderdate&"
			       "prepend=type%%3Ajson"
			       "\r\n\r\n"
			       , dr 
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	// . now the urls.csv is also a query on gbss files
	// . make an httprequest on stack and call it
	// . only do this for version 3 
	//   i.e. GET /v3/crawl/download/token-collectionname_urls.csv
	if ( fmt == FORMAT_CSV && 
	     rdbId == RDB_SPIDERDB &&
	     path[0] == '/' &&
	     path[1] == 'v' &&
	     path[2] == '3' ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		// never dedup
		int32_t dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search?"
			       // this is not necessary
			       //"icc=1&"
			       "format=csv&"
			       // no site clustering
			       "sc=0&"
			       // never dedup.
			       "dr=0&"
			       "c=%s&"
			       "n=10000000&"
			       // stream it now
			       // can't stream until we fix headers be printed
			       // in Msg40.cpp. so gbssUrl->Url etc.
			       // mdw: ok should work now
			       "stream=1&"
			       //"stream=0&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // do not compute summary. 0 lines.
			       //"ns=0&"
			       "q=gbrevsortbyint%%3AgbssSpiderTime+"
			       "gbssIsDiffbotObject%%3A0"
			       "&"
			       //"prepend=type%%3Ajson"
			       "\r\n\r\n"
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
	       return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");

	// initialize the new state
	st->m_rdbId = rdbId;
	st->m_downloadJSON = downloadJSON;
	st->m_socket = sock;
	// the name of the collections whose spiderdb we read from
	st->m_collnum = cr->m_collnum;

	st->m_fmt = fmt;
	st->m_isFirstTime = true;

	st->m_printedFirstBracket = false;
	st->m_printedItem = false;
	st->m_printedEndingBracket = false;

	// for csv...
	st->m_needHeaderRow = true;

	st->m_lastUh48 = 0LL;
	st->m_lastFirstIp = 0;
	st->m_prevReplyUh48 = 0LL;
	st->m_prevReplyFirstIp = 0;
	st->m_prevReplyError = 0;
	st->m_prevReplyDownloadTime = 0LL;
	st->m_dumpRound = 0;
	st->m_accumulated = 0LL;

	// debug
	//log("mnew1: st=%" PRIx32,(int32_t)st);

	// begin the possible segmented process of sending back spiderdb
	// to the user's browser
	st->sendBackDump2();
	// i dont think this return values matters at all since httpserver.cpp
	// does not look at it when it calls sendReply()
	return true;
}


// . all wrappers call this
// . returns false if would block, true otherwise
bool readAndSendLoop ( StateCD *st , bool readFirst ) {

 subloop:

	// if we had a broken pipe on the sendChunk() call then hopefully
	// this will kick in...
	if ( g_errno ) {
		log("crawlbot: readAndSendLoop: %s",mstrerror(g_errno));
		readFirst = true;
		st->m_someoneNeedsMore = false;
	}

	// wait if some are outstanding. how can this happen?
	if ( st->m_numRequests > st->m_numReplies ) {
		log("crawlbot: only got %" PRId32" of %" PRId32" replies. waiting for "
		    "all to come back in.",
		    st->m_numReplies,st->m_numRequests);
		return false;
	}

	// are we all done? we still have to call sendList() to 
	// set socket's streamingMode to false to close things up
	if ( readFirst && ! st->m_someoneNeedsMore ) {
		log("crawlbot: done sending for download request");
		mdelete ( st , sizeof(StateCD) , "stcd" );
		delete st;
		return true;
	}

	// begin reading from each shard and sending the spiderdb records
	// over the network. return if that blocked
	if ( readFirst && ! st->readDataFromRdb ( ) ) return false;

	// did user delete their collection midstream on us?
	if ( g_errno ) {
		log("crawlbot: read shard data had error: %s",
		    mstrerror(g_errno));
		goto subloop;
	}

	// send it to the browser socket. returns false if blocks.
	if ( ! st->sendList() ) return false;

	// read again i guess
	readFirst = true;

	// hey, it did not block... tcpserver caches writes...
	goto subloop;
}

void StateCD::sendBackDump2 ( ) {

	m_numRequests = 0;
	m_numReplies  = 0;

	// read 10MB from each shard's spiderdb at a time
	//m_minRecSizes = 9999999;
	// 1ook to be more fluid
	m_minRecSizes = 99999;

	// we stop reading from all shards when this becomes false
	m_someoneNeedsMore = true;

	// initialize the spiderdb startkey "cursor" for each shard's spiderdb
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		m_needMore[i] = true;
		KEYMIN((char *)&m_spiderdbStartKeys[i],sizeof(key128_t));
		KEYMIN((char *)&m_titledbStartKeys[i],sizeof(key_t));
	}

	// begin reading from the shards and trasmitting back on m_socket
	readAndSendLoop ( this , true );
}


static void gotListWrapper7 ( void *state ) {
	// get the Crawler dump State
	StateCD *st = (StateCD *)state;
	// inc it up here
	st->m_numReplies++;
	// wait for all
	if ( st->m_numReplies < st->m_numRequests ) return;
	// read and send loop
	readAndSendLoop( st , false );
}
	

bool StateCD::readDataFromRdb ( ) {

	// set end key to max key. we are limiting using m_minRecSizes for this
	key128_t ek; KEYMAX((char *)&ek,sizeof(key128_t));

	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	// collection got nuked?
	if ( ! cr ) {
		log("crawlbot: readdatafromrdb: coll %" PRId32" got nuked",
		    (int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return true;
	}

	// top:
	// launch one request to each shard
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		// reset each one
		m_lists[i].freeList();
		// if last list was exhausted don't bother
		if ( ! m_needMore[i] ) continue;
		// count it
		m_numRequests++;
		// this is the least nice. crawls will yield to it mostly.
		int32_t niceness = 0;
		// point to right startkey
		char *sk ;
		if ( m_rdbId == RDB_SPIDERDB )
			sk = (char *)&m_spiderdbStartKeys[i];
		else
			sk = (char *)&m_titledbStartKeys[i];
		// get host
		Host *h = g_hostdb.getLiveHostInShard(i);
		// show it
		int32_t ks = getKeySizeFromRdbId(m_rdbId);
		log("dump: asking host #%" PRId32" for list sk=%s",
		    h->m_hostId,KEYSTR(sk,ks));
		// msg0 uses multicast in case one of the hosts in a shard is
		// dead or dies during this call.
		if ( ! m_msg0s[i].getList ( h->m_hostId , // use multicast
					    h->m_ip,
					    h->m_port,
					    0, // maxcacheage
					    false, // addtocache?
					    m_rdbId,
					   cr->m_collnum,
					   &m_lists[i],
					   sk,
					   (char *)&ek,
					   // get at most about
					   // "minRecSizes" worth of spiderdb
					   // records
					   m_minRecSizes,
					   this,
					    gotListWrapper7 ,
					    niceness ) ) {
			log("crawlbot: blocked getting list from shard");
			// continue if it blocked
			continue;
		}
		log("crawlbot: did not block getting list from shard err=%s",
		    mstrerror(g_errno));
		// we got a reply back right away...
		m_numReplies++;
	}
	// all done? return if still waiting on more msg0s to get their data
	if ( m_numReplies < m_numRequests ) return false;
	// i guess did not block, empty single shard? no, must have been
	// error becaues sendList() would have sent back on the tcp
	// socket and blocked and returned false if not error sending
	return true;
}

bool StateCD::sendList ( ) {
	// get the Crawler dump State
	// inc it
	//m_numReplies++;
	// sohw it
	log("crawlbot: got list from shard. req=%" PRId32" rep=%" PRId32,
	    m_numRequests,m_numReplies);
	// return if still awaiting more replies
	if ( m_numReplies < m_numRequests ) return false;

	SafeBuf sb;
	//sb.setLabel("dbotdmp");

	const char *ct = "text/csv";
	if ( m_fmt == FORMAT_JSON )
		ct = "application/json";
	if ( m_fmt == FORMAT_XML )
		ct = "text/xml";
	if ( m_fmt == FORMAT_TXT )
		ct = "text/plain";
	if ( m_fmt == FORMAT_CSV )
		ct = "text/csv";

	// . if we haven't yet sent an http mime back to the user
	//   then do so here, the content-length will not be in there
	//   because we might have to call for more spiderdb data
	if ( m_needsMime ) {
		m_needsMime = false;
		HttpMime mime;
		mime.makeMime ( -1, // totel content-lenght is unknown!
				0 , // do not cache (cacheTime)
				0 , // lastModified
				0 , // offset
				-1 , // bytesToSend
				NULL , // ext
				false, // POSTReply
				ct, // "text/csv", // contenttype
				"utf-8" , // charset
				-1 , // httpstatus
				NULL ); //cookie
		sb.safeMemcpy(mime.getMime(),mime.getMimeLen() );
	}

	if ( ! m_printedFirstBracket && m_fmt == FORMAT_JSON ) {
		sb.safePrintf("[\n");
		m_printedFirstBracket = true;
	}

	// we set this to true below if any one shard has more spiderdb
	// records left to read
	m_someoneNeedsMore = false;

	//
	// got all replies... create the HTTP reply and send it back
	//
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		if ( ! m_needMore[i] ) continue;
		// get the list from that group
		RdbList *list = &m_lists[i];

		// should we try to read more?
		m_needMore[i] = false;

		// report it
		log("dump: got list of %" PRId32" bytes from host #%" PRId32" round #%" PRId32,
		    list->getListSize(),i,m_dumpRound);


		if ( list->isEmpty() ) {
			list->freeList();
			continue;
		}

		// get the format
		//char *format = cr->m_diffbotFormat.getBufStart();
		//if ( cr->m_diffbotFormat.length() <= 0 ) format = NULL;
		//char *format = NULL;

		// this cores because msg0 does not transmit lastkey
		//char *ek = list->getLastKey();

		char *lastKeyPtr = NULL;

		// now print the spiderdb list out into "sb"
		if ( m_rdbId == RDB_SPIDERDB ) {
			// print SPIDERDB list into "sb"
			printSpiderdbList ( list , &sb , &lastKeyPtr );
			//  update spiderdb startkey for this shard
			KEYSET((char *)&m_spiderdbStartKeys[i],lastKeyPtr,
			       sizeof(key128_t));
			// advance by 1
			m_spiderdbStartKeys[i] += 1;
		}

		else if ( m_rdbId == RDB_TITLEDB ) {
			// print TITLEDB list into "sb"
			printTitledbList ( list , &sb , &lastKeyPtr );
			//  update titledb startkey for this shard
			KEYSET((char *)&m_titledbStartKeys[i],lastKeyPtr,
			       sizeof(key_t));
			// advance by 1
			m_titledbStartKeys[i] += 1;
		}

		else { char *xx=NULL;*xx=0; }

		// figure out why we do not get the full list????
		//if ( list->m_listSize >= 0 ) { // m_minRecSizes ) {
		m_needMore[i] = true;
		m_someoneNeedsMore = true;
		//}

		// save mem
		list->freeList();
	}

	m_dumpRound++;

	//log("rdbid=%" PRId32" fmt=%" PRId32" some=%" PRId32" printed=%" PRId32,
	//    (int32_t)m_rdbId,(int32_t)m_fmt,(int32_t)m_someoneNeedsMore,
	//    (int32_t)m_printedEndingBracket);

	m_socket->m_streamingMode = true;

	// if nobody needs to read more...
	if ( ! m_someoneNeedsMore && ! m_printedEndingBracket ) {
		// use this for printing out urls.csv as well...
		m_printedEndingBracket = true;
		// end array of json objects. might be empty!
		if ( m_rdbId == RDB_TITLEDB && m_fmt == FORMAT_JSON )
			sb.safePrintf("\n]\n");
		//log("adding ]. len=%" PRId32,sb.length());
		// i'd like to exit streaming mode here. i fixed tcpserver.cpp
		// so if we are called from makecallback() there it won't
		// call destroysocket if we WERE in streamingMode just yet
		m_socket->m_streamingMode = false;		
	}

	TcpServer *tcp = &g_httpServer.m_tcp;

	// . transmit the chunk in sb
	// . steals the allocated buffer from sb and stores in the 
	//   TcpSocket::m_sendBuf, which it frees when socket is
	//   ultimately destroyed or we call sendChunk() again.
	// . when TcpServer is done transmitting, it does not close the
	//   socket but rather calls doneSendingWrapper() which can call
	//   this function again to send another chunk
	if ( ! tcp->sendChunk ( m_socket , 
				&sb  ,
				this ,
				doneSendingWrapper ) )
		return false;

	// we are done sending this chunk, i guess tcp write was cached
	// in the network card buffer or something
	return true;
}

// TcpServer.cpp calls this when done sending TcpSocket's m_sendBuf
void doneSendingWrapper ( void *state , TcpSocket *sock ) {
	StateCD *st = (StateCD *)state;
	// error on socket?
	//if ( g_errno ) st->m_socketError = g_errno;
	//TcpSocket *socket = st->m_socket;
	st->m_accumulated += sock->m_totalSent;

	log("crawlbot: done sending on socket %" PRId32"/%" PRId32" [%" PRId64"] bytes",
	    sock->m_totalSent,
	    sock->m_sendBufUsed,
	    st->m_accumulated);


	readAndSendLoop ( st , true );

	return;
}

void StateCD::printSpiderdbList ( RdbList *list,SafeBuf *sb,char **lastKeyPtr){
	// declare these up here
	SpiderRequest *sreq = NULL;
	SpiderReply   *srep = NULL;
	int32_t badCount = 0;

	int32_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	uint32_t lastSpidered = 0;

	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// save it
		*lastKeyPtr = rec;
		// we encounter the spiderreplies first then the
		// spiderrequests for the same url
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			srep = (SpiderReply *)rec;
			if ( sreq ) lastSpidered = 0;
			sreq = NULL;
			if ( lastSpidered == 0 )
				lastSpidered = srep->m_spideredTime;
			else if ( srep->m_spideredTime > lastSpidered )
				lastSpidered = srep->m_spideredTime;
			m_prevReplyUh48 = srep->getUrlHash48();
			m_prevReplyFirstIp = srep->m_firstIp;
			// 0 means indexed successfully. not sure if
			// this includes http status codes like 404 etc.
			// i don't think it includes those types of errors!
			m_prevReplyError = srep->m_errCode;
			m_prevReplyDownloadTime = srep->m_spideredTime;
			continue;
		}
		// ok, we got a spider request
		sreq = (SpiderRequest *)rec;
		
		if ( sreq->isCorrupt() ) {
			log("spider: encountered a corrupt spider req "
			    "when dumping cn=%" PRId32". skipping.",
			    (int32_t)cr->m_collnum);
			continue;
		}

		// sanity check
		if ( srep && srep->getUrlHash48() != sreq->getUrlHash48()){
			badCount++;
			//log("diffbot: had a spider reply with no "
			//    "corresponding spider request for uh48=%" PRId64
			//    , srep->getUrlHash48());
			//char *xx=NULL;*xx=0;
		}

		// print the url if not yet printed
		int64_t uh48 = sreq->getUrlHash48  ();
		int32_t firstIp = sreq->m_firstIp;
		bool printIt = false;
		// there can be multiple spiderrequests for the same url!
		if ( m_lastUh48 != uh48 ) printIt = true;
		// sometimes the same url has different firstips now that
		// we have the EFAKEFIRSTIP spider error to avoid spidering
		// seeds twice...
		if ( m_lastFirstIp != firstIp ) printIt = true;
		if ( ! printIt ) continue;
		m_lastUh48 = uh48;
		m_lastFirstIp = firstIp;

		// make sure spiderreply is for the same url!
		if ( srep && srep->getUrlHash48() != sreq->getUrlHash48() )
			srep = NULL;
		if ( ! srep )
			lastSpidered = 0;

		bool isProcessed = false;
		if ( srep ) isProcessed = false;

		// 1 means spidered, 0 means not spidered, -1 means error
		int32_t status = 1;
		// if unspidered, then we don't match the prev reply
		// so set "status" to 0 to indicate hasn't been
		// downloaded yet.
		if ( m_lastUh48 != m_prevReplyUh48 ) status = 0;
		if ( m_lastFirstIp != m_prevReplyFirstIp ) status = 0;
		// if it matches, perhaps an error spidering it?
		if ( status && m_prevReplyError ) status = -1;

		// use the time it was added to spiderdb if the url
		// was not spidered
		time_t time = sreq->m_addedTime;
		// if it was spidered, successfully or got an error,
		// then use the time it was spidered
		if ( status ) time = m_prevReplyDownloadTime;

		const char *msg = "Successfully Downloaded";//Crawled";
		if ( status == 0 ) msg = "Not downloaded";//Unexamined";
		if ( status == -1 ) {
			msg = mstrerror(m_prevReplyError);
			// do not print "Fake First Ip"...
			if ( m_prevReplyError == EFAKEFIRSTIP )
				msg = "Initial crawl request";
			// if the initial crawl request got a reply then that
			// means the spiderrequest was added under the correct
			// firstip... so skip it. i am assuming that the
			// correct spidrerequest got added ok here...
			if ( m_prevReplyError == EFAKEFIRSTIP )
				continue;
		}

		// matching url filter, print out the expression
		int32_t ufn ;
		ufn = ::getUrlFilterNum(sreq,
					srep,
					nowGlobalMS,
					false,
					MAX_NICENESS,
					cr,
					false, // isoutlink?
					NULL,
					-1); // langIdArg
		const char *expression = NULL;
		int32_t  priority = -4;
		// sanity check
		if ( ufn >= 0 ) { 
			expression = cr->m_regExs[ufn].getBufStart();
			priority   = cr->m_spiderPriorities[ufn];
		}

		if ( ! expression ) {
			expression = "error. matches no expression!";
			priority = -4;
		}

		// when spidering rounds we use the 
		// lastspidertime>={roundstart} --> spiders disabled rule
		// so that we do not spider a url twice in the same round
		if ( ufn >= 0 && //! cr->m_spidersEnabled[ufn] ) {
		     cr->m_regExs[ufn].length() &&
		     // we set this to 0 instead of using the checkbox
		     strstr(cr->m_regExs[ufn].getBufStart(),"round") ) {
			//cr->m_maxSpidersPerRule[ufn] <= 0 ) {
			priority = -5;
		}

		const char *as = "discovered";
		if ( sreq && 
		     ( sreq->m_isInjecting ||
		       sreq->m_isAddUrl ) ) {
			as = "manually added";
		}

		// print column headers?
		if ( m_isFirstTime ) {
			m_isFirstTime = false;
			sb->safePrintf("\"Url\","
				       "\"Entry Method\","
				       );
			if ( cr->m_isCustomCrawl )
				sb->safePrintf("\"Processed?\",");
			sb->safePrintf(
				       "\"Add Time\","
				       "\"Last Crawled\","
				       "\"Last Status\","
				       "\"Matching Expression\","
				       "\"Matching Action\"\n");
		}

		// "csv" is default if json not specified
		if ( m_fmt == FORMAT_JSON ) 
			sb->safePrintf("[{"
				       "{\"url\":"
				       "\"%s\"},"
				       "{\"time\":"
				       "\"%" PRIu32"\"},"

				       "{\"status\":"
				       "\"%" PRId32"\"},"

				       "{\"statusMsg\":"
				       "\"%s\"}"
				       
				       "}]\n"
				       , sreq->m_url
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       , status
				       , msg
				       );
		// but default to csv
		else {
		    if (cr && cr->m_isCustomCrawl == 1 && sreq && !sreq->m_isAddUrl && !sreq->m_isInjecting) {
		        if (cr->m_diffbotUrlCrawlPattern.m_length == 0
		            && cr->m_diffbotUrlProcessPattern.m_length == 0) {
		            // If a crawl and there are no urlCrawlPattern or urlCrawlRegEx values, only return URLs from seed domain
		            //if (sreq && !sreq->m_sameDom)
		            //    continue;
		        } else {
		            // TODO: if we get here, we have a crawl with a custom urlCrawlPattern and/or custom
		            //       urlProcessPattern. We have to check if the current url matches the pattern

		        }
		    }

			sb->safePrintf("\"%s\",\"%s\","
				       , sreq->m_url
				       , as
				       );
			if ( cr->m_isCustomCrawl )
				sb->safePrintf("%" PRId32",",(int32_t)isProcessed);
			sb->safePrintf(
				       "%" PRIu32",%" PRIu32",\"%s\",\"%s\",\""
				       //",%s"
				       //"\n"
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       // last time spidered, 0 if none
				       , lastSpidered
				       //, status
				       , msg
				       // the url filter expression it matches
				       , expression
				       // the priority
				       //, priorityMsg
				       //, iptoa(sreq->m_firstIp)
				       );
			// print priority
			//if ( priority == SPIDER_PRIORITY_FILTERED )
			// we just turn off the spiders now
			if ( ufn >= 0 && cr->m_maxSpidersPerRule[ufn] <= 0 )
				sb->safePrintf("url ignored");
			//else if ( priority == SPIDER_PRIORITY_BANNED )
			//	sb->safePrintf("url banned");
			else if ( priority == -4 )
				sb->safePrintf("error");
			else if ( priority == -5 )
				sb->safePrintf("will spider next round");
			else 
				sb->safePrintf("%" PRId32,priority);
			sb->safePrintf("\""
				       "\n");
		}
	}

	if ( ! badCount ) return;

	log("diffbot: had a spider reply with no "
	    "corresponding spider request %" PRId32" times", badCount);
}



void StateCD::printTitledbList ( RdbList *list,SafeBuf *sb,char **lastKeyPtr){

	XmlDoc xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// save it
	*lastKeyPtr = NULL;

	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// skip ifnegative
		if ( (rec[0] & 0x01) == 0x00 ) continue;
		// set it
		*lastKeyPtr = rec;
		// reset first since set2() can't call reset()
		xd.reset();
		// uncompress it
		if ( ! xd.set2 ( rec ,
				 0, // maxSize unused
				 cr->m_coll ,
				 NULL , // ppbuf
				 0 , // niceness
				 NULL ) ) { // spiderRequest
			log("diffbot: error setting titlerec in dump");
			continue;
		}
		// must be of type json to be a diffbot json object
		if ( m_downloadJSON && xd.m_contentType != CT_JSON ) continue;
		// or if downloading web pages...
		if ( ! m_downloadJSON ) {
			// skip if json object content type
			if ( xd.m_contentType == CT_JSON ) continue;
			// . just print the cached page
			// . size should include the \0
			sb->safeStrcpy ( xd.m_firstUrl.getUrl());
			// then \n
			sb->pushChar('\n');
			// then page content
			sb->safeStrcpy ( xd.ptr_utf8Content );
			// null term just in case
			//sb->nullTerm();
			// separate pages with \0 i guess
			sb->pushChar('\0');
			// \n
			sb->pushChar('\n');
			continue;
		}
	}
}

//////////////////////////////////////////
//
// MAIN API STUFF I GUESS
//
//////////////////////////////////////////


bool sendReply2 (TcpSocket *socket , int32_t fmt , char *msg ) {
	// log it
	log("crawlbot: %s",msg);

	const char *ct = "text/html";

	// send this back to browser
	SafeBuf sb;
	if ( fmt == FORMAT_JSON ) {
		sb.safePrintf("{\n\"response\":\"success\",\n"
			      "\"message\":\"%s\"\n}\n"
			      , msg );
		ct = "application/json";
	}
	else
		sb.safePrintf("<html><body>"
			      "success: %s"
			      "</body></html>"
			      , msg );

	//return g_httpServer.sendErrorReply(socket,500,sb.getBufStart());
	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0, // cachetime
					     false, // POST reply?
					     ct);
}


bool sendErrorReply2 ( TcpSocket *socket , int32_t fmt , const char *msg ) {

	// log it
	log("crawlbot: sending back 500 http status '%s'",msg);

	const char *ct = "text/html";

	// send this back to browser
	SafeBuf sb;
	if ( fmt == FORMAT_JSON ) {
		sb.safePrintf("{\"error\":\"%s\"}\n"
			      , msg );
		ct = "application/json";
	}
	else
		sb.safePrintf("<html><body>"
			      "failed: %s"
			      "</body></html>"
			      , msg );

	// log it
	//log("crawlbot: %s",msg );

	//return g_httpServer.sendErrorReply(socket,500,sb.getBufStart());
	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0, // cachetime
					     false, // POST reply?
					     ct ,
					     500 ); // error! not 200...
}

bool printCrawlBotPage2 ( class TcpSocket *s , 
			  class HttpRequest *hr ,
			  char fmt,
			  class SafeBuf *injectionResponse ,
			  class SafeBuf *urlUploadResponse ,
			  collnum_t collnum ) ;

void addedUrlsToSpiderdbWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;
	SafeBuf rr;
	rr.safePrintf("Successfully added urls for spidering.");
	printCrawlBotPage2 ( st->m_socket,
			     &st->m_hr ,
			     st->m_fmt,
			     NULL ,
			     &rr ,
			     st->m_collnum );
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel2: st=%" PRIx32,(int32_t)st);
}

class HelpItem {
public:
	const char *m_parm;
	const char *m_desc;
};

static class HelpItem s_his[] = {
	{"format","Use &format=html to show HTML output. Default is JSON."},
	{"token","Required for all operations below."},

	{"name","Name of the crawl. If missing will just show "
	 "all crawls owned by the given token."},

	{"delete=1","Deletes the crawl."},
	{"reset=1","Resets the crawl. Removes all seeds."},
	{"restart=1","Restarts the crawl. Keeps the seeds."},

	{"pause",
	 "Specify 1 or 0 to pause or resume the crawl respectively."},

	{"repeat","Specify number of days as floating point to "
	 "recrawl the pages. Set to 0.0 to NOT repeat the crawl."},

	{"crawlDelay","Wait this many seconds between crawling urls from the "
	 "same IP address. Can be a floating point number."},

	//{"deleteCrawl","Same as delete."},
	//{"resetCrawl","Same as delete."},
	//{"pauseCrawl","Same as pause."},
	//{"repeatCrawl","Same as repeat."},

	{"seeds","Whitespace separated list of URLs used to seed the crawl. "
	 "Will only follow outlinks on the same domain of seed URLs."
	},
	{"spots",
	 "Whitespace separated list of URLs to add to the crawl. "
	 "Outlinks will not be followed." },
	{"urls",
	 "Same as spots."},
	//{"spiderLinks","Use 1 or 0 to spider the links or NOT spider "
	// "the links, respectively, from "
	// "the provided seed or addUrls parameters. "
	// "The default is 1."},


	{"maxToCrawl", "Specify max pages to successfully download."},
	//{"maxToDownload", "Specify max pages to successfully download."},

	{"maxToProcess", "Specify max pages to successfully process through "
	 "diffbot."},
	{"maxRounds", "Specify maximum number of crawl rounds. Use "
	 "-1 to indicate no max."},

	{"onlyProcessIfNew", "Specify 1 to avoid re-processing pages "
	 "that have already been processed once before."},

	{"notifyEmail","Send email alert to this email when crawl hits "
	 "the maxtocrawl or maxtoprocess limit, or when the crawl "
	 "completes."},
	{"notifyWebhook","Fetch this URL when crawl hits "
	 "the maxtocrawl or maxtoprocess limit, or when the crawl "
	 "completes."},
	{"obeyRobots","Obey robots.txt files?"},
	//{"restrictDomain","Restrict downloaded urls to domains of seeds?"},

	{"urlCrawlPattern","List of || separated strings. If the url "
	 "contains any of these then we crawl the url, otherwise, we do not. "
	 "An empty pattern matches all urls."},

	{"urlProcessPattern","List of || separated strings. If the url "
	 "contains any of these then we send url to diffbot for processing. "
	 "An empty pattern matches all urls."},

	{"pageProcessPattern","List of || separated strings. If the page "
	 "contains any of these then we send it to diffbot for processing. "
	 "An empty pattern matches all pages."},

	{"urlCrawlRegEx","Regular expression that the url must match "
	 "in order to be crawled. If present then the urlCrawlPattern will "
	 "be ignored. "
	 "An empty regular expression matches all urls."},

	{"urlProcessRegEx","Regular expression that the url must match "
	 "in order to be processed. "
	 "If present then the urlProcessPattern will "
	 "be ignored. "
	 "An empty regular expression matches all urls."},

	{"apiUrl","Diffbot api url to use. We automatically append "
	 "token and url to it."},


	//{"expression","A pattern to match in a URL. List up to 100 "
	// "expression/action pairs in the HTTP request. "
	// "Example expressions:"},
	//{"action","Take the appropriate action when preceeding pattern is "
	// "matched. Specify multiple expression/action pairs to build a "
	// "table of filters. Each URL being spidered will take the given "
	// "action of the first expression it matches. Example actions:"},


	{NULL,NULL}
};

void collOpDoneWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;
	TcpSocket *socket = st->m_socket;
	log("crawlbot: done with blocked op.");
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel3: st=%" PRIx32,(int32_t)st);
	g_httpServer.sendDynamicPage (socket,"OK",2);
}

// . when we receive the request from john we call broadcastRequest() from
//   Pages.cpp. then msg28 sends this replay with a &cast=0 appended to it
//   to every host in the network. then when msg28 gets back replies from all 
//   those hosts it calls sendPageCrawlbot() here but without a &cast=0
// . so if no &cast is present we are the original!!!
bool sendPageCrawlbot ( TcpSocket *socket , HttpRequest *hr ) {

	// print help
	int32_t help = hr->getLong("help",0);
	if ( help ) {
		SafeBuf sb;
		sb.safePrintf("<html>"
			      "<title>Crawlbot API</title>"
			      "<h1>Crawlbot API</h1>"
			      "<b>Use the parameters below on the "
			      "<a href=\"/crawlbot\">/crawlbot</a> page."
			      "</b><br><br>"
			      "<table>"
			      );
		for ( int32_t i = 0 ; i < 1000 ; i++ ) {
			HelpItem *h = &s_his[i];
			if ( ! h->m_parm ) break;
			sb.safePrintf( "<tr>"
				       "<td>%s</td>"
				       "<td>%s</td>"
				       "</tr>"
				       , h->m_parm
				       , h->m_desc
				       );
		}
		sb.safePrintf("</table>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	// . now show stats for the current crawl
	// . put in xml or json if format=xml or format=json or
	//   xml=1 or json=1 ...
	char fmt = FORMAT_JSON;

	// token is always required. get from json or html form input
	//char *token = getInputString ( "token" );
	char *token = (char*)hr->getString("token");
	char *name = (char*)hr->getString("name");

	// . try getting token-name from ?c= 
	// . the name of the collection is encoded as <token>-<crawlname>
	const char *c = hr->getString("c");
	char tmp[MAX_COLL_LEN+100];
	if ( ! token && c ) {
		strncpy ( tmp , c , MAX_COLL_LEN );
		token = tmp;
		name = strstr(tmp,"-");
		if ( name ) {
			*name = '\0';
			name++;
		}
		// change default formatting to html
		fmt = FORMAT_HTML;
	}

	if (token){
			for ( int32_t i = 0 ; i < gbstrlen(token) ; i++ ){
				token[i]=tolower(token[i]);
			}
		}


	const char *fs = hr->getString("format",NULL,NULL);
	// give john a json api
	if ( fs && strcmp(fs,"html") == 0 ) fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"json") == 0 ) fmt = FORMAT_JSON;
	if ( fs && strcmp(fs,"xml") == 0 ) fmt = FORMAT_XML;
	// if we got json as input, give it as output
	//if ( JS.getFirstItem() ) fmt = FORMAT_JSON;



	if ( ! token && fmt == FORMAT_JSON ) { // (cast==0|| fmt == FORMAT_JSON ) ) {
		const char *msg = "invalid token";
		return sendErrorReply2 (socket,fmt,msg);
	}

	if ( ! token ) {
		// print token form if html
		SafeBuf sb;
		sb.safePrintf("In order to use crawlbot you must "
			      "first LOGIN:"
			      "<form action=/crawlbot method=get>"
			      "<br>"
			      "<input type=text name=token size=50>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "<br>"
			      "<b>- OR -</b>"
			      "<br> SIGN UP"
			      "<form action=/crawlbot method=get>"
			      "Name: <input type=text name=name size=50>"
			      "<br>"
			      "Email: <input type=text name=email size=50>"
			      "<br>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "</body>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	if ( gbstrlen(token) > 32 ) { 
		//log("crawlbot: token is over 32 chars");
		const char *msg = "crawlbot: token is over 32 chars";
		return sendErrorReply2 (socket,fmt,msg);
	}

	const char *seeds = hr->getString("seeds");
	const char *spots = hr->getString("spots");

	// /v2/bulk api support:
	if ( ! spots ) spots = hr->getString("urls");

	if ( spots && ! spots[0] ) spots = NULL;
	if ( seeds && ! seeds[0] ) seeds = NULL;

	bool restartColl = hr->hasField("restart");

	// default name to next available collection crawl name in the
	// case of a delete operation...
	const char *msg = NULL;
	if ( hr->hasField("delete") ) msg = "deleted";
	// need to re-add urls for a restart
	//if ( hr->hasField("restart") ) msg = "restarted";
	if ( hr->hasField("reset") ) msg = "reset";
	if ( msg ) { // delColl && cast ) {
		// this was deleted... so is invalid now
		name = NULL;
		// no longer a delete function, we need to set "name" below
		//delColl = false;//NULL;
		// john wants just a brief success reply
		SafeBuf tmp;
		tmp.safePrintf("{\"response\":\"Successfully %s job.\"}",
			       msg);
		char *reply = tmp.getBufStart();
		if ( ! reply ) {
			if ( ! g_errno ) g_errno = ENOMEM;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
		return g_httpServer.sendDynamicPage( socket,
						     reply,
						     gbstrlen(reply),
						     0, // cacheTime
						     false, // POSTReply?
						     "application/json"
						     );
	}

	// if name is missing default to name of first existing
	// collection for this token. 
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) { // cast
		if (  name ) break;
		// do not do this if doing an
		// injection (seed) or add url or del coll or reset coll !!
		if ( seeds ) break;
		if ( spots ) break;
		//if ( delColl ) break;
		//if ( resetColl ) break;
		if ( restartColl ) break;
		CollectionRec *cx = g_collectiondb.m_recs[i];
		// deleted collections leave a NULL slot
		if ( ! cx ) continue;
		// skip if token does not match
		if ( strcmp ( cx->m_diffbotToken.getBufStart(),token) )
			continue;
		// got it
		name = cx->m_diffbotCrawlName.getBufStart();
		break;
	}

	if ( ! name ) {
		// if the token is valid
		const char *ct = "application/json";
		const char *msg = "{}\n";
		return g_httpServer.sendDynamicPage ( socket, 
						      msg,
						      gbstrlen(msg) ,
						      -1 , // cachetime
						      false ,
						      ct ,
						      200 ); // http status
	}


	if ( gbstrlen(name) > 30 ) { 
		//log("crawlbot: name is over 30 chars");
		const char *msg = "crawlbot: name is over 30 chars";
		return sendErrorReply2 (socket,fmt,msg);
	}

	// make the collection name so it includes the token and crawl name
	char collName[MAX_COLL_LEN+1];
	// sanity
	if ( MAX_COLL_LEN < 64 ) { char *xx=NULL;*xx=0; }
	// make a compound name for collection of token and name
	sprintf(collName,"%s-%s",token,name);

	// if they did not specify the token/name of an existing collection
	// then cr will be NULL and we'll add it below
	CollectionRec *cr = g_collectiondb.getRec(collName);

	// i guess bail if not there?
	if ( ! cr ) {
		log("crawlbot: missing coll rec for coll %s",collName);
		//char *msg = "invalid or missing collection rec";
		const char *msg = "Could not create job because missing seeds or "
			"urls.";
		return sendErrorReply2 (socket,fmt,msg);
	}

	// make a new state
	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
		return sendErrorReply2 ( socket , fmt , mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");

	// copy crap
	st->m_hr.copy ( hr );
	st->m_socket = socket;
	st->m_fmt = fmt;
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;

	// save seeds
	if ( cr && restartColl ) { // && cast ) {
		// bail on OOM saving seeds
		if ( ! st->m_seedBank.safeMemcpy ( &cr->m_diffbotSeeds ) ||
		     ! st->m_seedBank.pushChar('\0') ) {
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
	}

	//
	// if we can't compile the provided regexes, return error
	//
	if ( cr ) {
		const char *rx1 = hr->getString("urlCrawlRegEx",NULL);
		if ( rx1 && ! rx1[0] ) rx1 = NULL;
		const char *rx2 = hr->getString("urlProcessRegEx",NULL);
		if ( rx2 && ! rx2[0] ) rx2 = NULL;
		// this will store the compiled regular expression into ucr
		regex_t re1;
		regex_t re2;
		int32_t status1 = 0;
		int32_t status2 = 0;
		if ( rx1 )
			status1 = regcomp ( &re1 , rx1 ,
					    REG_EXTENDED|REG_ICASE|
					    REG_NEWLINE|REG_NOSUB);
		if ( rx2 )
			status2 = regcomp ( &re2 , rx2 ,
					    REG_EXTENDED|REG_ICASE|
					    REG_NEWLINE|REG_NOSUB);
		if ( rx1 ) regfree ( &re1 );
		if ( rx2 ) regfree ( &re2 );
		SafeBuf em;
		if ( status1 ) {
			log("xmldoc: regcomp %s failed.",rx1);
			em.safePrintf("Invalid regular expresion: %s",rx1);
		}
		else if ( status2 ) {
			log("xmldoc: regcomp %s failed.",rx2);
			em.safePrintf("Invalid regular expresion: %s",rx2);
		}
		if ( status1 || status2 ) {
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			char *msg = em.getBufStart();
			return sendErrorReply2(socket,fmt,msg);
		}
	}

	// check seed bank now too for restarting a crawl
	if ( st->m_seedBank.length() && ! seeds )
		seeds = st->m_seedBank.getBufStart();

	const char *coll = "NONE";
	if ( cr ) coll = cr->m_coll;

	if ( seeds )
		log("crawlbot: adding seeds=\"%s\" coll=%s (%" PRId32")",
		    seeds,coll,(int32_t)st->m_collnum);

	char bulkurlsfile[1024];
	// when a collection is restarted the collnum changes to avoid
	// adding any records destined for that collnum that might be on 
	// the wire. so just put these in the root dir
	snprintf(bulkurlsfile, 1024, "%sbulkurls-%s.txt", 
		 g_hostdb.m_dir , coll );//, (int32_t)st->m_collnum );
	if ( spots && cr && cr->m_isCustomCrawl == 2 ) {
	    int32_t spotsLen = (int32_t)gbstrlen(spots);
		log("crawlbot: got spots (len=%" PRId32") to add coll=%s (%" PRId32")",
		    spotsLen,coll,(int32_t)st->m_collnum);
		FILE *f = fopen(bulkurlsfile, "w");
		if (f != NULL) {
		    // urls are space separated.
		    // as of 5/14/2014, it appears that spots is space-separated for some URLs (the first two)
		    // and newline-separated for the remainder. Make a copy that's space separated so that restarting bulk jobs works.
		    // Alternatives:
		    //  1) just write one character to disk at a time, replacing newlines with spaces
		    //  2) just output what you have, and then when you read in, replace newlines with spaces
		    //  3) probably the best option: change newlines to spaces earlier in the pipeline
		    char *spotsCopy = (char*) mmalloc(spotsLen+1, "create a temporary copy of spots that we're about to delete");
		    for (int i = 0; i < spotsLen; i++) {
		        char c = spots[i];
		        if (c == '\n')
		            c = ' ';
		        spotsCopy[i] = c;
		    }
		    spotsCopy[spotsLen] = '\0';
		    fprintf(f, "%s", spotsCopy);
		    fclose(f);
		    mfree(spotsCopy, spotsLen+1, "no longer need copy");
		}
	}

	// if restart flag is on and the file with bulk urls exists, 
	// get spots from there
	SafeBuf bb;
	if ( !spots && restartColl && cr && cr->m_isCustomCrawl == 2 ) {
		bb.load(bulkurlsfile);
		bb.nullTerm();
		spots = bb.getBufStart();
		log("crawlbot: restarting bulk job file=%s bufsize=%" PRId32" for %s",
		    bulkurlsfile,bb.length(), cr->m_coll);
	}

	///////
	// 
	// handle file of urls upload. can be HUGE!
	//
	///////
	if ( spots || seeds ) {
		// error
		if ( g_repair.isRepairActive() &&
		     g_repair.m_collnum == st->m_collnum ) {
			log("crawlbot: repair active. can't add seeds "
			    "or spots while repairing collection.");
			g_errno = EREPAIRING;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}

		// this returns NULL with g_errno set
		bool status = true;
		if ( ! getSpiderRequestMetaList ( seeds, &st->m_listBuf, true, cr ) ) {
			status = false;
		}

		// do not spider links for spots
		if ( ! getSpiderRequestMetaList ( spots, &st->m_listBuf, false, NULL ) ) {
			status = false;
		}

		// empty?
		int32_t size = st->m_listBuf.length();
		// error?
		if ( ! status ) {
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
		// if not list
		if ( ! size ) {
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,"no urls found");
		}
		// add to spiderdb
		if ( ! st->m_msg4.addMetaList( &( st->m_listBuf ), cr->m_collnum, st, addedUrlsToSpiderdbWrapper, 0 ) ) {
			// blocked!
			return false;
		}

		// did not block, print page!
		addedUrlsToSpiderdbWrapper(st);
		return true;
	}

	// we do not need the state i guess

	////////////
	//
	// print the html or json page of all the data
	//
	printCrawlBotPage2 ( socket,hr,fmt,NULL,NULL,cr->m_collnum);

	// get rid of that state
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel4: st=%" PRIx32,(int32_t)st);
	return true;
}

bool printCrawlDetailsInJson ( SafeBuf *sb , CollectionRec *cx ) {
    return printCrawlDetailsInJson( sb , cx , HTTP_REQUEST_DEFAULT_REQUEST_VERSION);
}

bool printCrawlDetailsInJson ( SafeBuf *sb , CollectionRec *cx, int version ) {

	SafeBuf tmp;
	int32_t crawlStatus = -1;
	getSpiderStatusMsg ( cx , &tmp , &crawlStatus );
	CrawlInfo *ci = &cx->m_localCrawlInfo;
	int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
	if ( sentAlert ) sentAlert = 1;

	const char *crawlTypeStr = "crawl";
	//char *nomen = "crawl";
	if ( cx->m_isCustomCrawl == 2 ) {
		crawlTypeStr = "bulk";
		//nomen = "job";
	}

	// don't print completed time if spidering is going on
	uint32_t completed = cx->m_diffbotCrawlEndTime;
	// if not yet done, make this zero
	if ( crawlStatus == SP_INITIALIZING ) completed = 0;
	if ( crawlStatus == SP_NOURLS ) completed = 0;
	//if ( crawlStatus == SP_PAUSED ) completed = 0;
	//if ( crawlStatus == SP_ADMIN_PAUSED ) completed = 0;
	if ( crawlStatus == SP_INPROGRESS ) completed = 0;

	sb->safePrintf("\n\n{"
		      "\"name\":\"%s\",\n"
		      "\"type\":\"%s\",\n"

		       "\"jobCreationTimeUTC\":%" PRId32",\n"
		       "\"jobCompletionTimeUTC\":%" PRId32",\n"

		      //"\"alias\":\"%s\",\n"
		      //"\"crawlingEnabled\":%" PRId32",\n"
		      "\"jobStatus\":{" // nomen = jobStatus / crawlStatus
		      "\"status\":%" PRId32","
		      "\"message\":\"%s\"},\n"
		      "\"sentJobDoneNotification\":%" PRId32",\n"
		      //"\"crawlingPaused\":%" PRId32",\n"
		      "\"objectsFound\":%" PRId64",\n"
		      "\"urlsHarvested\":%" PRId64",\n"
		      //"\"urlsExamined\":%" PRId64",\n"
		      "\"pageCrawlAttempts\":%" PRId64",\n"
		      "\"pageCrawlSuccesses\":%" PRId64",\n"
		      "\"pageCrawlSuccessesThisRound\":%" PRId64",\n"

		      "\"pageProcessAttempts\":%" PRId64",\n"
		      "\"pageProcessSuccesses\":%" PRId64",\n"
		      "\"pageProcessSuccessesThisRound\":%" PRId64",\n"

		      "\"maxRounds\":%" PRId32",\n"
		      "\"repeat\":%f,\n"
		      "\"crawlDelay\":%f,\n"

		      //,cx->m_coll
		      , cx->m_diffbotCrawlName.getBufStart()
		      , crawlTypeStr

		       , cx->m_diffbotCrawlStartTime
		       // this is 0 if not over yet
		       , completed

		      //, alias
		      //, (int32_t)cx->m_spideringEnabled
		      , crawlStatus
		      , tmp.getBufStart()
		      , sentAlert
		      //, (int32_t)paused
		      , cx->m_globalCrawlInfo.m_objectsAdded -
		      cx->m_globalCrawlInfo.m_objectsDeleted
		      , cx->m_globalCrawlInfo.m_urlsHarvested
		      //,cx->m_globalCrawlInfo.m_urlsConsidered
		      , cx->m_globalCrawlInfo.m_pageDownloadAttempts
		      , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
		      , cx->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound

		      , cx->m_globalCrawlInfo.m_pageProcessAttempts
		      , cx->m_globalCrawlInfo.m_pageProcessSuccesses
		      , cx->m_globalCrawlInfo.m_pageProcessSuccessesThisRound

		      , (int32_t)cx->m_maxCrawlRounds
		      , cx->m_collectiveRespiderFrequency
		      , cx->m_collectiveCrawlDelay
		      );

	sb->safePrintf("\"obeyRobots\":%" PRId32",\n"
		      , (int32_t)cx->m_useRobotsTxt );

	// if not a "bulk" injection, show crawl stats
	if ( cx->m_isCustomCrawl != 2 ) {

		sb->safePrintf(
			      // settable parms
			      "\"maxToCrawl\":%" PRId64",\n"
			      "\"maxToProcess\":%" PRId64",\n"
			      //"\"restrictDomain\":%" PRId32",\n"
			      "\"onlyProcessIfNew\":%" PRId32",\n"
			      , cx->m_maxToCrawl
			      , cx->m_maxToProcess
			      //, (int32_t)cx->m_restrictDomain
			      , (int32_t)cx->m_diffbotOnlyProcessIfNewUrl
			      );
		sb->safePrintf("\"seeds\":\"");
		sb->safeUtf8ToJSON ( cx->m_diffbotSeeds.getBufStart());
		sb->safePrintf("\",\n");
	}

	sb->safePrintf("\"roundsCompleted\":%" PRId32",\n",
		      cx->m_spiderRoundNum);

	sb->safePrintf("\"roundStartTime\":%" PRIu32",\n",
		      cx->m_spiderRoundStartTime);

	sb->safePrintf("\"currentTime\":%" PRIu32",\n",
		       (uint32_t)getTimeGlobal() );
	sb->safePrintf("\"currentTimeUTC\":%" PRIu32",\n",
		       (uint32_t)getTimeGlobal() );


	sb->safePrintf("\"apiUrl\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotApiUrl.getBufStart() );
	sb->safePrintf("\",\n");


	sb->safePrintf("\"urlCrawlPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlCrawlPattern.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"urlProcessPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlProcessPattern.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"pageProcessPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotPageProcessPattern.getBufStart() );
	sb->safePrintf("\",\n");


	sb->safePrintf("\"urlCrawlRegEx\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlCrawlRegEx.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"urlProcessRegEx\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlProcessRegEx.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"maxHops\":%" PRId32",\n",
		       (int32_t)cx->m_diffbotMaxHops);

	char *token = cx->m_diffbotToken.getBufStart();
	char *name = cx->m_diffbotCrawlName.getBufStart();

	const char *mt = "crawl";
	if ( cx->m_isCustomCrawl == 2 ) mt = "bulk";

	sb->safePrintf("\"downloadJson\":"
		      "\"http://api.diffbot.com/v%d/%s/download/"
		      "%s-%s_data.json\",\n"
	          , version
		      , mt
		      , token
		      , name
		      );

	sb->safePrintf("\"downloadUrls\":"
		      "\"http://api.diffbot.com/v%d/%s/download/"
		      "%s-%s_urls.csv\",\n"
	          , version
		      , mt
		      , token
		      , name
		      );

	sb->safePrintf("\"notifyEmail\":\"");
	sb->safeUtf8ToJSON ( cx->m_notifyEmail.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"notifyWebhook\":\"");
	sb->safeUtf8ToJSON ( cx->m_notifyUrl.getBufStart() );
	sb->safePrintf("\"\n");

	// end that collection rec
	sb->safePrintf("}\n");

	return true;
}

bool printCrawlDetails2 (SafeBuf *sb , CollectionRec *cx , char format ) {

	SafeBuf tmp;
	int32_t crawlStatus = -1;
	getSpiderStatusMsg ( cx , &tmp , &crawlStatus );
	CrawlInfo *ci = &cx->m_localCrawlInfo;
	int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
	if ( sentAlert ) sentAlert = 1;

	// don't print completed time if spidering is going on
	uint32_t completed = cx->m_diffbotCrawlEndTime; // time_t
	// if not yet done, make this zero
	if ( crawlStatus == SP_INITIALIZING ) completed = 0;
	if ( crawlStatus == SP_NOURLS ) completed = 0;
	//if ( crawlStatus == SP_PAUSED ) completed = 0;
	//if ( crawlStatus == SP_ADMIN_PAUSED ) completed = 0;
	if ( crawlStatus == SP_INPROGRESS ) completed = 0;

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("{"
			       "\"response\":{\n"
			       "\t\"statusCode\":%" PRId32",\n"
			       "\t\"statusMsg\":\"%s\",\n"
			       "\t\"jobCreationTimeUTC\":%" PRId32",\n"
			       "\t\"jobCompletionTimeUTC\":%" PRId32",\n"
			       "\t\"sentJobDoneNotification\":%" PRId32",\n"
			       "\t\"urlsHarvested\":%" PRId64",\n"
			       "\t\"pageCrawlAttempts\":%" PRId64",\n"
			       "\t\"pageCrawlSuccesses\":%" PRId64",\n"
			       , crawlStatus
			       , tmp.getBufStart()
			       , cx->m_diffbotCrawlStartTime
			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t\"currentTime\":%" PRIu32",\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t\"currentTimeUTC\":%" PRIu32"\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t}\n");
		sb->safePrintf("}\n");
	}

	if ( format == FORMAT_XML ) {
		sb->safePrintf("<response>\n"
			       "\t<statusCode>%" PRId32"</statusCode>\n"
			       , crawlStatus
			       );
		sb->safePrintf(
			       "\t<statusMsg><![CDATA[%s]]></statusMsg>\n"
			       "\t<jobCreationTimeUTC>%" PRId32
			       "</jobCreationTimeUTC>\n"
			       , (char *)tmp.getBufStart()
			       , (int32_t)cx->m_diffbotCrawlStartTime
			       );
		sb->safePrintf(
			       "\t<jobCompletionTimeUTC>%" PRId32
			       "</jobCompletionTimeUTC>\n"

			       "\t<sentJobDoneNotification>%" PRId32
			       "</sentJobDoneNotification>\n"

			       "\t<urlsHarvested>%" PRId64"</urlsHarvested>\n"

			       "\t<pageCrawlAttempts>%" PRId64
			       "</pageCrawlAttempts>\n"

			       "\t<pageCrawlSuccesses>%" PRId64
			       "</pageCrawlSuccesses>\n"

			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t<currentTime>%" PRIu32"</currentTime>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t<currentTimeUTC>%" PRIu32"</currentTimeUTC>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("</response>\n");
	}

	return true;
}

bool printCrawlBotPage2 ( TcpSocket *socket , 
			  HttpRequest *hr ,
			  char fmt, // format
			  SafeBuf *injectionResponse ,
			  SafeBuf *urlUploadResponse ,
			  collnum_t collnum ) {
	

	// store output into here
	SafeBuf sb;

	if ( fmt == FORMAT_HTML )
		sb.safePrintf(
			      "<html>"
			      "<title>Crawlbot - "
			      "Web Data Extraction and Search Made "
			      "Easy</title>"
			      "<body>"
			      );

	CollectionRec *cr = g_collectiondb.m_recs[collnum];

	// was coll deleted while adding urls to spiderdb?
	if ( ! cr ) {
		g_errno = EBADREQUEST;
		const char *msg = "invalid crawl. crawl was deleted.";
		return sendErrorReply2(socket,fmt,msg);
	}

	char *token = cr->m_diffbotToken.getBufStart();
	char *name = cr->m_diffbotCrawlName.getBufStart();

	// this is usefful
	SafeBuf hb;
	hb.safePrintf("<input type=hidden name=name value=\"%s\">"
		      "<input type=hidden name=token value=\"%s\">"
		      "<input type=hidden name=format value=\"html\">"
		      , name
		      , token );
	hb.nullTerm();

	// and this
	SafeBuf lb;
	lb.safePrintf("name=");
	lb.urlEncode(name);
	lb.safePrintf ("&token=");
	lb.urlEncode(token);
	if ( fmt == FORMAT_HTML ) lb.safePrintf("&format=html");
	lb.nullTerm();


	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf("<table border=0>"
			      "<tr><td>"
			      "<b><font size=+2>"
			      "<a href=/crawlbot?token=%s>"
			      "Crawlbot</a></font></b>"
			      "<br>"
			      "<font size=-1>"
			      "Crawl, Datamine and Index the Web"
			      "</font>"
			      "</td></tr>"
			      "</table>"
			      , token
			      );
		sb.safePrintf("<center><br>");
		// first print help
		sb.safePrintf("[ <a href=/crawlbot?help=1>"
			      "api help</a> ] &nbsp; "
			      // json output
			      "[ <a href=\"/crawlbot?token=%s&format=json&"
			      "name=%s\">"
			      "json output"
			      "</a> ] &nbsp; "
			      , token 
			      , name );
		// random coll name to add
		uint32_t r1 = rand();
		uint32_t r2 = rand();
		uint64_t rand64 = (uint64_t) r1;
		rand64 <<= 32;
		rand64 |=  r2;
		char newCollName[MAX_COLL_LEN+1];
		snprintf(newCollName,MAX_COLL_LEN,"%s-%016" PRIx64,
			 token , rand64 );
		// first print "add new collection"
		sb.safePrintf("[ <a href=/crawlbot?name=%016" PRIx64"&token=%s&"
			      "format=html&addCrawl=%s>"
			      "add new crawl"
			      "</a> ] &nbsp; "
			      "[ <a href=/crawlbot?token=%s>"
			      "show all crawls"
			      "</a> ] &nbsp; "
			      , rand64
			      , token
			      , newCollName
			      , token
			      );
	}
	

	bool firstOne = true;

	//
	// print list of collections controlled by this token
	//
	for ( int32_t i = 0 ; fmt == FORMAT_HTML && i<g_collectiondb.m_numRecs;i++ ){
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// get its token if any
		char *ct = cx->m_diffbotToken.getBufStart();
		if ( ! ct ) continue;
		// skip if token does not match
		if ( strcmp(ct,token) )
			continue;
		// highlight the tab if it is what we selected
		bool highlight = false;
		if ( cx == cr ) highlight = true;
		const char *style = "";
		if  ( highlight ) {
			style = "style=text-decoration:none; ";
			sb.safePrintf ( "<b><font color=red>");
		}
		// print the crawl id. collection name minus <TOKEN>-
		sb.safePrintf("<a %shref=/crawlbot?token=", style);
		sb.urlEncode(token);
		sb.safePrintf("&name=");
		sb.urlEncode(cx->m_diffbotCrawlName.getBufStart());
		sb.safePrintf("&format=html>"
			      "%s (%" PRId32")"
			      "</a> &nbsp; "
			      , cx->m_diffbotCrawlName.getBufStart()
			      , (int32_t)cx->m_collnum
			      );
		if ( highlight )
			sb.safePrintf("</font></b>");
	}

	if ( fmt == FORMAT_HTML )
		sb.safePrintf ( "</center><br/>" );

	// the ROOT JSON [
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("{\n");

	// injection is currently not in use, so this is an artifact:
	if ( fmt == FORMAT_JSON && injectionResponse )
		sb.safePrintf("\"response\":\"%s\",\n\n"
			      , injectionResponse->getBufStart() );

	if ( fmt == FORMAT_JSON && urlUploadResponse )
		sb.safePrintf("\"response\":\"%s\",\n\n"
			      , urlUploadResponse->getBufStart() );


	//////
	//
	// print collection summary page
	//
	//////

	// the items in the array now have type:bulk or type:crawl
	// so call them 'jobs'
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("\"jobs\":[");//\"collections\":");

	int32_t summary = hr->getLong("summary",0);
	// enter summary mode for json
	if ( fmt != FORMAT_HTML ) summary = 1;
	// start the table
	if ( summary && fmt == FORMAT_HTML ) {
		sb.safePrintf("<table border=1 cellpadding=5>"
			      "<tr>"
			      "<td><b>Collection</b></td>"
			      "<td><b>Objects Found</b></td>"
			      "<td><b>URLs Harvested</b></td>"
			      "<td><b>URLs Examined</b></td>"
			      "<td><b>Page Download Attempts</b></td>"
			      "<td><b>Page Download Successes</b></td>"
			      "<td><b>Page Download Successes This Round"
			      "</b></td>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td><b>Page Process Successes This Round"
			      "</b></td>"
			      "</tr>"
			      );
	}

	const char *name3 = hr->getString("name");

	// scan each coll and get its stats
	for ( int32_t i = 0 ; summary && i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// must belong to us
		if ( strcmp(cx->m_diffbotToken.getBufStart(),token) )
			continue;


		// just print out single crawl info for json
		if ( fmt != FORMAT_HTML && cx != cr && name3 ) 
			continue;

		// if json, print each collectionrec
		if ( fmt == FORMAT_JSON ) {
			if ( ! firstOne ) 
				sb.safePrintf(",\n\t");
			firstOne = false;
			//char *alias = "";
			//if ( cx->m_collectionNameAlias.length() > 0 )
			//	alias=cx->m_collectionNameAlias.getBufStart();
			//int32_t paused = 1;

			//if ( cx->m_spideringEnabled ) paused = 0;
			if ( cx->m_isCustomCrawl )
				printCrawlDetailsInJson ( &sb , cx , 
						  getVersionFromRequest(hr) );
			else
				printCrawlDetails2 ( &sb,cx,FORMAT_JSON );

			// print the next one out
			continue;
		}


		// print in table
		sb.safePrintf("<tr>"
			      "<td>%s</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      //"<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"
			      , cx->m_coll
			      , cx->m_globalCrawlInfo.m_objectsAdded -
			        cx->m_globalCrawlInfo.m_objectsDeleted
			      , cx->m_globalCrawlInfo.m_urlsHarvested
			      //, cx->m_globalCrawlInfo.m_urlsConsidered
			      , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cx->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound
			      , cx->m_globalCrawlInfo.m_pageProcessAttempts
			      , cx->m_globalCrawlInfo.m_pageProcessSuccesses
			      , cx->m_globalCrawlInfo.m_pageProcessSuccessesThisRound
			      );
	}
	if ( summary && fmt == FORMAT_HTML ) {
		sb.safePrintf("</table></html>" );
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	if ( fmt == FORMAT_JSON ) 
		// end the array of collection objects
		sb.safePrintf("\n]\n");

	///////
	//
	// end print collection summary page
	//
	///////


	//
	// show urls being crawled (ajax) (from Spider.cpp)
	//
	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf ( "<table width=100%% cellpadding=5 "
				"style=border-width:1px;border-style:solid;"
				"border-color:black;>"
				//"bgcolor=#%s>\n" 
				"<tr><td colspan=50>"// bgcolor=#%s>"
				"<b>Last 10 URLs</b> (%" PRId32" spiders active)"
				//,LIGHT_BLUE
				//,DARK_BLUE
				,(int32_t)g_spiderLoop.m_numSpidersOut);
		const char *str = "<font color=green>Resume Crawl</font>";
		int32_t pval = 0;
		if ( cr->m_spideringEnabled )  {
			str = "<font color=red>Pause Crawl</font>";
			pval = 1;
		}
		sb.safePrintf(" "
			      "<a href=/crawlbot?%s"
			      "&pauseCrawl=%" PRId32"><b>%s</b></a>"
			      , lb.getBufStart() // has &name=&token= encoded
			      , pval
			      , str
			      );

		sb.safePrintf("</td></tr>\n" );

		// the table headers so SpiderRequest::printToTable() works
		if ( ! SpiderRequest::printTableHeaderSimple(&sb,true) ) 
			return false;
		// shortcut
		XmlDoc **docs = g_spiderLoop.m_docs;
		// row count
		int32_t j = 0;
		// first print the spider recs we are spidering
		for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
			// get it
			XmlDoc *xd = docs[i];
			// skip if empty
			if ( ! xd ) continue;
			// sanity check
			if ( ! xd->m_sreqValid ) { char *xx=NULL;*xx=0; }
			// skip if not our coll rec!
			//if ( xd->m_cr != cr ) continue;
			if ( xd->m_collnum != cr->m_collnum ) continue;
			// grab it
			SpiderRequest *oldsr = &xd->m_sreq;
			// get status
			const char *status = xd->m_statusMsg;
			// show that
			if ( ! oldsr->printToTableSimple ( &sb , status,xd,j)) 
				return false;
			j++;
		}

		// end the table
		sb.safePrintf ( "</table>\n" );
		sb.safePrintf ( "<br>\n" );

	} // end html format




	// this is for making sure the search results are not cached
	uint32_t r1 = rand();
	uint32_t r2 = rand();
	uint64_t rand64 = (uint64_t) r1;
	rand64 <<= 32;
	rand64 |=  r2;


	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf("<br>"
			      "<table border=0 cellpadding=5>"
			      
			      // OBJECT search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Objects:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      // site clustering off
			      "<input type=hidden name=sc value=0>"
			      // dup removal off
			      "<input type=hidden name=dr value=0>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%" PRId64">"
			      // bypass ajax, searchbox, logo, etc.
			      "<input type=hidden name=id value=12345>"
			      // restrict search to json objects
			      "<input type=hidden name=prepend "
			      "value=\"type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"

			      // PAGE search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Pages:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      // site clustering off
			      "<input type=hidden name=sc value=0>"
			      // dup removal off
			      "<input type=hidden name=dr value=0>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%" PRId64">"
			      // bypass ajax, searchbox, logo, etc.
			      "<input type=hidden name=id value=12345>"
			      // restrict search to NON json objects
			      "<input type=hidden "
			      "name=prepend value=\"-type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"
			      
			      // add url input box
			      "<form method=get action=/crawlbot>"
			      "<tr>"
			      "<td>"
			      "<b>Add Seed Urls: </b>"
			      "</td><td>"
			      "<input type=text name=seeds size=50>"
			      "%s" // hidden tags
			      " "
			      "<input type=submit name=submit value=OK>"

			      , cr->m_coll
			      , rand64
			      , cr->m_coll
			      , rand64
			      , hb.getBufStart() // hidden tags
			      );
	}

	if ( injectionResponse && fmt == FORMAT_HTML )
		sb.safePrintf("<br><font size=-1>%s</font>\n"
			      ,injectionResponse->getBufStart() 
			      );

	if ( fmt == FORMAT_HTML )
		sb.safePrintf(
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Add Spot URLs:</b></td>"
			      
			      "<td>"
			      "<input type=text name=spots size=50> "
			      "<input type=submit name=submit value=OK>"
			      "%s" // hidden tags

			      "</form>"

			      "</td>"
			      "</tr>"
			      
			      "</table>"
			      "<br>"
			      //, cr->m_coll
			      , hb.getBufStart()
			      );


	//
	// show stats
	//
	if ( fmt == FORMAT_HTML ) {

		const char *seedStr = cr->m_diffbotSeeds.getBufStart();
		if ( ! seedStr ) seedStr = "";

		SafeBuf tmp;
		int32_t crawlStatus = -1;
		getSpiderStatusMsg ( cr , &tmp , &crawlStatus );
		CrawlInfo *ci = &cr->m_localCrawlInfo;
		int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
		if ( sentAlert ) sentAlert = 1;

		sb.safePrintf(

			      "<form method=get action=/crawlbot>"
			      "%s"
			      , hb.getBufStart() // hidden input token/name/..
			      );
		sb.safePrintf("<TABLE border=0>"
			      "<TR><TD valign=top>"

			      "<table border=0 cellpadding=5>"

			      //
			      "<tr>"
			      "<td><b>Crawl Name:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Type:</td>"
			      "<td>%" PRId32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Token:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Seeds:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status:</td>"
			      "<td>%" PRId32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status Msg:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Start Time:</td>"
			      "<td>%" PRIu32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Last Crawl Completion Time:</td>"
			      "<td>%" PRIu32"</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Rounds Completed:</td>"
			      "<td>%" PRId32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Has Urls Ready to Spider:</td>"
			      "<td>%" PRId32"</td>"
			      "</tr>"

			      , cr->m_diffbotCrawlName.getBufStart()
			      
			      , (int32_t)cr->m_isCustomCrawl

			      , cr->m_diffbotToken.getBufStart()

			      , seedStr

			      , crawlStatus
			      , tmp.getBufStart()

			      , cr->m_diffbotCrawlStartTime
			      // this is 0 if not over yet
			      , cr->m_diffbotCrawlEndTime

			      , cr->m_spiderRoundNum
			      , cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider

			      );

		// show crawlinfo crap
		CrawlInfo *cis = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();
		sb.safePrintf("<tr><td><b>Ready Hosts</b></td><td>");
		for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
			CrawlInfo *ci = &cis[i];
			if ( ! ci ) continue;
			if ( ! ci->m_hasUrlsReadyToSpider ) continue;
			Host *h = g_hostdb.getHost ( i );
			if ( ! h ) continue;
			sb.safePrintf("<a href=http://%s:%i/crawlbot?c=%s>"
				      "%i</a> "
				      , iptoa(h->m_ip)
				      , (int)h->m_httpPort
				      , cr->m_coll
				      , (int)i
				      );
		}
		sb.safePrintf("</tr>\n");


		sb.safePrintf(
			      "<tr>"
			      "<td><b>Objects Found</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>URLs Harvested</b> (inc. dups)</td>"
			      "<td>%" PRId64"</td>"
     
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Attempts</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes This Round</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes This Round</b></td>"
			      "<td>%" PRId64"</td>"
			      "</tr>"

			      
			      , cr->m_globalCrawlInfo.m_objectsAdded -
			        cr->m_globalCrawlInfo.m_objectsDeleted
			      , cr->m_globalCrawlInfo.m_urlsHarvested

			      , cr->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound

			      , cr->m_globalCrawlInfo.m_pageProcessAttempts
			      , cr->m_globalCrawlInfo.m_pageProcessSuccesses
			      , cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound
			      );


		uint32_t now = (uint32_t)getTimeGlobalNoCore();

		sb.safePrintf("<tr>"
			      "<td><b>Download Objects:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_data.csv>"
			      "csv</a>"

			      " &nbsp; "

			      "<a href=/crawlbot/download/%s_data.json>"
			      "json full dump</a>"

			      " &nbsp; "

			      , cr->m_coll
			      , cr->m_coll

			      );

		sb.safePrintf(
			      // newest json on top of results
			      "<a href=/search?icc=1&format=json&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "json full search (newest on top)</a>"


			      " &nbsp; "

			      // newest json on top of results, last 10 mins
			      "<a href=/search?icc=1&format=json&"
			      // disable site clustering
			      "sc=0&"
			      // doNOTdupcontentremoval:
			      "dr=0&"
			      "c=%s&n=10000000&rand=%" PRIu64"&scores=0&id=1&"
			      "stream=1&" // stream results back as we get them
			      "q="
			      // put NEWEST on top
			      "gbsortbyint%%3Agbspiderdate+"
			      // min spider date = now - 10 mins
			      "gbminint%%3Agbspiderdate%%3A%" PRId32"&"
			      //"debug=1"
			      "prepend=type%%3Ajson"
			      ">"
			      "json search (last 30 seconds)</a>"



			      "</td>"
			      "</tr>"
			      
			      // json search with gbsortby:gbspiderdate
			      , cr->m_coll
			      , rand64


			      // json search with gbmin:gbspiderdate
			      , cr->m_coll
			      , rand64
			      , now - 30 // 60 // last 1 minute

			      );


		sb.safePrintf (
			      "<tr>"
			      "<td><b>Download Products:</b> "
			      "</td><td>"
			      // make it search.csv so excel opens it
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbrevsortby%%3Aproduct.offerPrice&"
			      "prepend=type%%3Ajson"
			      //"+type%%3Aproduct%%7C"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbrevsortby%%3Aproduct.offerPrice&"
			      "prepend=type%%3Ajson"
			      ">"
			      "html</a>"

			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Download Urls:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_urls.csv>"
			      "csv</a>"

			      " <a href=/v3/crawl/download/%s_urls.csv>"
			      "new csv format</a>"

			      " <a href=/search?q=gbsortby"
			      "int%%3AgbssSpiderTime&n=50&c=%s>"
			      "last 50 download attempts</a>"
			      
			      "</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Latest Objects:</b> "
			      "</td><td>"
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "html</a>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Latest Products:</b> "
			      "</td><td>"
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson+type%%3Aproduct"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10&rand=%" PRIu64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson+type%%3Aproduct"
			      ">"
			      "html</a>"

			      "</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Download Pages:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_pages.txt>"
			      "txt</a>"
			      //
			      "</td>"
			      "</tr>"

			      "</table>"

			      "</TD>"
			      
			      // download products html
			      , cr->m_coll
			      , rand64

			      , cr->m_coll
			      , rand64

			      //, cr->m_coll
			      //, cr->m_coll
			      //, cr->m_coll

			      // urls.csv old
			      , cr->m_coll

			      // urls.csv new format v3
			      , cr->m_coll


			      // last 50 downloaded urls
			      , cr->m_coll

			      // latest objects in html
			      , cr->m_coll
			      , rand64

			      // latest objects in csv
			      , cr->m_coll
			      , rand64

			      // latest products in html
			      , cr->m_coll
			      , rand64

			      // latest products in csv
			      , cr->m_coll
			      , rand64

			      // download pages
			      , cr->m_coll
			      );


		// spacer column
		sb.safePrintf("<TD>"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "</TD>"
			      );

		sb.safePrintf( "<TD valign=top>"
			      "<table cellpadding=5 border=0>"
			      );

		const char *urtYes = " checked";
		const char *urtNo  = "";
		if ( ! cr->m_useRobotsTxt ) {
			urtYes = "";
			urtNo  = " checked";
		}

		const char *isNewYes = "";
		const char *isNewNo  = " checked";
		if ( cr->m_diffbotOnlyProcessIfNewUrl ) {
			isNewYes = " checked";
			isNewNo  = "";
		}

		const char *api = cr->m_diffbotApiUrl.getBufStart();
		if ( ! api ) api = "";
		SafeBuf apiUrl;
		apiUrl.htmlEncode ( api , gbstrlen(api), true , 0 );
		apiUrl.nullTerm();

		const char *px1 = cr->m_diffbotUrlCrawlPattern.getBufStart();
		if ( ! px1 ) px1 = "";
		SafeBuf ppp1;
		ppp1.htmlEncode ( px1 , gbstrlen(px1) , true , 0 );
		ppp1.nullTerm();

		const char *px2 = cr->m_diffbotUrlProcessPattern.getBufStart();
		if ( ! px2 ) px2 = "";
		SafeBuf ppp2;
		ppp2.htmlEncode ( px2 , gbstrlen(px2) , true , 0 );
		ppp2.nullTerm();

		const char *px3 = cr->m_diffbotPageProcessPattern.getBufStart();
		if ( ! px3 ) px3 = "";
		SafeBuf ppp3;
		ppp3.htmlEncode ( px3 , gbstrlen(px3) , true , 0 );
		ppp3.nullTerm();

		const char *rx1 = cr->m_diffbotUrlCrawlRegEx.getBufStart();
		if ( ! rx1 ) rx1 = "";
		SafeBuf rrr1;
		rrr1.htmlEncode ( rx1 , gbstrlen(rx1), true , 0 );

		const char *rx2 = cr->m_diffbotUrlProcessRegEx.getBufStart();
		if ( ! rx2 ) rx2 = "";
		SafeBuf rrr2;
		rrr2.htmlEncode ( rx2 , gbstrlen(rx2), true , 0 );

		const char *notifEmail = cr->m_notifyEmail.getBufStart();
		const char *notifUrl   = cr->m_notifyUrl.getBufStart();
		if ( ! notifEmail ) notifEmail = "";
		if ( ! notifUrl   ) notifUrl = "";

		sb.safePrintf(
			      
			      //
			      //
			      "<tr>"
			      "<td><b>Repeat Crawl:</b> "
			      "</td><td>"
			      "<input type=text name=repeat "
			      "size=10 value=\"%f\"> "
			      "<input type=submit name=submit value=OK>"
			      " days"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Diffbot API Url:</b> "
			      "</td><td>"
			      "<input type=text name=apiUrl "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Crawl Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=urlCrawlPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Process Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=urlProcessPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=pageProcessPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Crawl RegEx:</b> "
			      "</td><td>"
			      "<input type=text name=urlCrawlRegEx "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Process RegEx:</b> "
			      "</td><td>"
			      "<input type=text name=urlProcessRegEx "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max hopcount to seeds:</b> "
			      "</td><td>"
			      "<input type=text name=maxHops "
			      "size=9 value=%" PRId32"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"
			      "<tr>"

            "<td><b>Only Process If New:</b> "
			      "</td><td>"
			      "<input type=radio name=onlyProcessIfNew "
			      "value=1%s> yes &nbsp; "
			      "<input type=radio name=onlyProcessIfNew "
			      "value=0%s> no &nbsp; "
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Delay (seconds):</b> "
			      "</td><td>"
			      "<input type=text name=crawlDelay "
			      "size=9 value=%f> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Page Crawl Successes:</b> "
			      "</td><td>"
			      "<input type=text name=maxToCrawl "
			      "size=9 value=%" PRId64"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Page Process Successes:</b>"
			      "</td><td>"
			      "<input type=text name=maxToProcess "
			      "size=9 value=%" PRId64"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Rounds:</b>"
			      "</td><td>"
			      "<input type=text name=maxRounds "
			      "size=9 value=%" PRId32"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Notification Email:</b>"
			      "</td><td>"
			      "<input type=text name=notifyEmail "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Notification URL:</b>"
			      "</td><td>"
			      "<input type=text name=notifyWebhook "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr><td>"
			      "<b>Use Robots.txt when crawling?</b> "
			      "</td><td>"
			      "<input type=radio name=obeyRobots "
			      "value=1%s> yes &nbsp; "
			      "<input type=radio name=obeyRobots "
			      "value=0%s> no &nbsp; "
			      "</td>"
			      "</tr>"

			      "</table>"

			      "</TD>"
			      "</TR>"
			      "</TABLE>"


			      , cr->m_collectiveRespiderFrequency

			      , apiUrl.getBufStart()
			      , ppp1.getBufStart()
			      , ppp2.getBufStart()
			      , ppp3.getBufStart()

			      , rrr1.getBufStart()
			      , rrr2.getBufStart()
            
            , cr->m_diffbotMaxHops

			      , isNewYes
			      , isNewNo
			      
			      , cr->m_collectiveCrawlDelay


			      , cr->m_maxToCrawl 
			      , cr->m_maxToProcess
			      , (int32_t)cr->m_maxCrawlRounds

			      , notifEmail
			      , notifUrl

			      , urtYes
			      , urtNo

			      //, rdomYes
			      //, rdomNo

			      );
	}

	//
	// show simpler url filters table
	//
	if ( fmt == FORMAT_HTML ) {
		// 
		// END THE BIG FORM
		//
		sb.safePrintf("</form>");
	}

	//
	// show reset and delete crawl buttons
	//
	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf(
			      "<table cellpadding=5>"
			      "<tr>"

			      "<td>"


			      // reset collection form
			      "<form method=get action=/crawlbot>"
			      "%s" // hidden tags
			      , hb.getBufStart()
			      );
		sb.safePrintf(

			      "<input type=hidden name=reset value=1>"
			      // also show it in the display, so set "c"
			      "<input type=submit name=button value=\""
			      "Reset this collection\">"
			      "</form>"
			      // end reset collection form
			      "</td>"

			      "<td>"

			      // delete collection form
			      "<form method=get action=/crawlbot>"
			      "%s"
			      //, (int32_t)cr->m_collnum
			      , hb.getBufStart()
			      );

		sb.safePrintf(

			      "<input type=hidden name=delete value=1>"
			      "<input type=submit name=button value=\""
			      "Delete this collection\">"
			      "</form>"
			      // end delete collection form
			      "</td>"


			      // restart collection form
			      "<td>"
			      "<form method=get action=/crawlbot>"
			      "%s"
			      "<input type=hidden name=restart value=1>"
			      "<input type=submit name=button value=\""
			      "Restart this collection\">"
			      "</form>"
			      "</td>"

			      // restart collection form
			      "<td>"
			      "<form method=get action=/crawlbot>"
			      "%s"
			      "<input type=hidden name=roundStart value=1>"
			      "<input type=submit name=button value=\""
			      "Restart spider round\">"
			      "</form>"
			      "</td>"


			      "</tr>"
			      "</table>"

			      //, (int32_t)cr->m_collnum
			      , hb.getBufStart()
			      , hb.getBufStart()
			      //, (int32_t)cr->m_collnum
			      );
	}


	// the ROOT JSON }
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("}\n");

	const char *ct = "text/html";
	if ( fmt == FORMAT_JSON ) ct = "application/json";
	if ( fmt == FORMAT_XML ) ct = "text/xml";

	// this could be in html json or xml
	return g_httpServer.sendDynamicPage ( socket, 
					      sb.getBufStart(), 
					      sb.length(),
					      -1 , // cachetime
					      false ,
					      ct );
}

// . do not add dups into m_diffbotSeeds safebuf
// . return 0 if not in table, 1 if in table. -1 on error adding to table.
int32_t isInSeedBuf ( CollectionRec *cr , const char *url, int len ) {

	HashTableX *ht = &cr->m_seedHashTable;

	// if table is empty, populate it
	if ( ht->m_numSlotsUsed <= 0 ) {
		// initialize the hash table
		if ( ! ht->set(8,0,1024,NULL,0,false,1,"seedtbl") ) 
			return -1;
		// populate it from list of seed urls
		char *p = cr->m_diffbotSeeds.getBufStart();
		for ( ; p && *p ; ) {
			// get url
			char *purl = p;
			// advance to next
			for ( ; *p && !is_wspace_a(*p) ; p++ );
			// make end then
			char *end = p;
			// skip possible white space. might be \0.
			if ( *p ) p++;
			// hash it
			int64_t h64 = hash64 ( purl , end-purl );
			if ( ! ht->addKey ( &h64 ) ) return -1;
		}
	}

	// is this url in the hash table?
	int64_t u64 = hash64 ( url, len );
	
	if ( ht->isInTable ( &u64 ) ) return 1;

	// add it to hashtable
	if ( ! ht->addKey ( &u64 ) ) return -1;

	// WAS not in table
	return 0;
}

// just use "fakeips" based on the hash of each url hostname/subdomain
// so we don't waste time doing ip lookups.
bool getSpiderRequestMetaList ( const char *doc, SafeBuf *listBuf, bool spiderLinks, CollectionRec *cr ) {
	if ( ! doc ) {
		return true;
	}

	// . scan the list of urls
	// . assume separated by white space \n \t or space
	const char *p = doc;

	uint32_t now = (uint32_t)getTimeGlobal();

	// a big loop
	while ( true ) {
		// skip white space (\0 is not a whitespace)
		for ( ; is_wspace_a(*p) ; p++ );

		// all done?
		if ( ! *p ) break;

		// save it
		const char *saved = p;

		// advance to next white space
		for ( ; ! is_wspace_a(*p) && *p ; p++ );

		// set end
		const char *end = p;

		// get that url
		Url url;
		url.set( saved, end - saved );

		// if not legit skip
		if ( url.getUrlLen() <= 0 ) continue;

		// need this
		int64_t probDocId = g_titledb.getProbableDocId(&url);

		// make it
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_firstIp = url.getHostHash32(); // fakeip!

		// avoid ips of 0 or -1
		if ( sreq.m_firstIp == 0 || sreq.m_firstIp == -1 ) {
			sreq.m_firstIp = 1;
		}

		sreq.m_hostHash32 = url.getHostHash32();
		sreq.m_domHash32  = url.getDomainHash32();
		sreq.m_siteHash32 = url.getHostHash32();
		//sreq.m_probDocId  = probDocId;
		sreq.m_hopCount   = 0; // we're a seed
		sreq.m_hopCountValid = true;
		sreq.m_addedTime = now;
		sreq.m_isWWWSubdomain = url.isSimpleSubdomain();
		
		sreq.m_fakeFirstIp = 1;
		sreq.m_isAddUrl = 1;

		// spider links?
		if ( ! spiderLinks ) {
			sreq.m_avoidSpiderLinks = 1;
		}

		// save the url!
		strcpy ( sreq.m_url , url.getUrl() );
		// finally, we can set the key. isDel = false
		sreq.setKey ( sreq.m_firstIp , probDocId , false );

		int32_t oldBufSize = listBuf->getCapacity();
		int32_t need = listBuf->getLength() + 100 + sreq.getRecSize();
		int32_t newBufSize = 0;

		if ( need > oldBufSize ) {
			newBufSize = oldBufSize + 100000;
		}

		if ( newBufSize && ! listBuf->reserve ( newBufSize ) ) {
			// return false with g_errno set
			return false;
		}

		// store rdbid first
		if ( ! listBuf->pushChar(RDB_SPIDERDB) ) {
			// return false with g_errno set
			return false;
		}

		// store it
		if ( ! listBuf->safeMemcpy ( &sreq , sreq.getRecSize() ) ) {
			// return false with g_errno set
			return false;
		}

		if ( ! cr ) {
			continue;
		}

		// do not add dups into m_diffbotSeeds safebuf
		int32_t status = isInSeedBuf ( cr , saved , end - saved );

		// error?
		if ( status == -1 ) {
			log ( LOG_WARN, "crawlbot: error adding seed to table: %s", mstrerror(g_errno) );
			return true;
		}

		// already in buf
		if ( status == 1 ) {
			continue;
		}

		// add url into m_diffbotSeeds, \n separated list
		if ( cr->m_diffbotSeeds.length() ) {
			// make it space not \n so it looks better in the
			// json output i guess
			cr->m_diffbotSeeds.pushChar( ' ' ); // \n
		}

		cr->m_diffbotSeeds.safeMemcpy (url.getUrl(), url.getUrlLen());
		cr->m_diffbotSeeds.nullTerm();
	}

	// all done
	return true;
}

