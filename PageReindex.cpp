#include "HttpServer.h"
#include "Msg0.h"
#include "Msg1.h"
#include "Msg20.h"
#include "Collectiondb.h"
#include "Hostdb.h"
#include "Conf.h"
#include "Query.h"
#include "RdbList.h"
#include "Pages.h"
#include "Msg3a.h"
#include "sort.h"
#include "Spider.h"
#include "XmlDoc.h"
#include "PageInject.h" // Msg7
#include "PageReindex.h"
#include "GigablastRequest.h"
#include "Process.h"


class State13 {
public:
	Msg1c      m_msg1c;
	GigablastRequest m_gr;
};

static void doneReindexing ( void *state ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . query re-index interface
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageReindex ( TcpSocket *s , HttpRequest *r ) {
	// make a state
	State13 *st ;
	try { st = new (State13); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%i): %s", 
		    (int)sizeof(State13),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State13) , "PageReindex" );

	// set this. also sets gr->m_hr
	GigablastRequest *gr = &st->m_gr;
	// this will fill in GigablastRequest so all the parms we need are set
	g_parms.setGigablastRequest ( s , r , gr );

	TcpSocket *sock = gr->m_socket;

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( gr->m_coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;

		// g_errno should be set so it will return an error response
		g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
		mdelete ( st , sizeof(State13) , "PageTagdb" );
		delete (st);
		return true;

	}


	collnum_t collnum = cr->m_collnum;

	// if no query send back the page blanked out i guess
	if ( ! gr->m_query || ! gr->m_query[0] ) {
		doneReindexing ( st );
		return true;
	}

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		g_errno = ENOPERM;
		doneReindexing ( st );
		return true;
	}

	int32_t langId = getLangIdFromAbbr ( gr->m_qlang );

	// let msg1d do all the work now
	if ( ! st->m_msg1c.reindexQuery ( gr->m_query ,
					  collnum,
					  gr->m_srn , // startNum ,
					  gr->m_ern , // endNum   ,
					  (bool)gr->m_forceDel,
					  langId,
					  st ,
					  doneReindexing ) )
		return false;

	// no waiting
	doneReindexing ( st );
	return true;
}

void doneReindexing ( void *state ) {
	// cast it
	State13 *st = (State13 *)state;

	GigablastRequest *gr = &st->m_gr;

	// note it
	if ( gr->m_query && gr->m_query[0] )
		log(LOG_INFO,"admin: Done with query reindex. %s",
		    mstrerror(g_errno));

	////
	//
	// print the html page
	//
	/////

	HttpRequest *hr = &gr->m_hr;

	char format = hr->getReplyFormat();

	SafeBuf sb;

	const char *ct = "text/html";
	if ( format == FORMAT_JSON ) ct = "application/json";
	if ( format == FORMAT_XML  ) {
		ct = "text/xml";

		sb.safePrintf("<response>\n"
			      "\t<statusCode>0</statusCode>\n"
			      "\t<statusMsg>Success</statusMsg>\n"
			      "\t<matchingResults>%" PRId32"</matchingResults>\n"
			      "</response>"
			      , st->m_msg1c.m_numDocIdsAdded
			      );
		g_httpServer.sendDynamicPage ( gr->m_socket,
					       sb.getBufStart(),
					       sb.length(),
					       -1,
					       false,ct);
		mdelete ( st , sizeof(State13) , "PageTagdb" );
		delete (st);
		return;
	}

	if ( format == FORMAT_JSON ) {
		sb.safePrintf("{\"response\":{\n"
			      "\t\"statusCode\":0,\n"
			      "\t\"statusMsg\":\"Success\",\n"
			      "\t\"matchingResults\":%" PRId32"\n"
			      "}\n"
			      "}\n"
			      , st->m_msg1c.m_numDocIdsAdded
			      );
		g_httpServer.sendDynamicPage ( gr->m_socket,
					       sb.getBufStart(),
					       sb.length(),
					       -1,
					       false,ct);
		mdelete ( st , sizeof(State13) , "PageTagdb" );
		delete (st);
		return;
	}



	g_pages.printAdminTop ( &sb , gr->m_socket , &gr->m_hr );

	sb.safePrintf("<style>"
		       ".poo { background-color:#%s;}\n"
		       "</style>\n" ,
		       LIGHT_BLUE );


	//
	// print error msg if any
	//

	if ( gr->m_query && gr->m_query[0] && ! g_errno )
		sb.safePrintf ( "<center><font color=red><b>Success. "
			  "Added %" PRId32" docid(s) to "
			  "spider queue.</b></font></center><br>" , 
			  st->m_msg1c.m_numDocIdsAdded );

	if ( gr->m_query && gr->m_query[0] && g_errno )
		sb.safePrintf ( "<center><font color=red><b>Error. "
				 "%s</b></font></center><br>" , 
				 mstrerror(g_errno));


	// print the reindex interface
	g_parms.printParmTable ( &sb , gr->m_socket , &gr->m_hr  );


	g_httpServer.sendDynamicPage ( gr->m_socket,
				       sb.getBufStart(),
				       sb.length(),
				       -1,
				       false);

	mdelete ( st , sizeof(State13) , "PageTagdb" );
	delete (st);
}

////////////////////////////////////////////////////////
//
//
// Msg1c if for reindexing docids
//
//
////////////////////////////////////////////////////////

static void gotDocIdListWrapper ( void *state );
static void addedListWrapper ( void *state ) ;

Msg1c::Msg1c() {
	m_numDocIds = 0;
	m_numDocIdsAdded = 0;
	m_collnum = -1;
	m_callback = NULL;
	// Coverity
	m_startNum = 0;
	m_endNum = 0;
	m_forceDel = false;
	m_state = NULL;
	m_niceness = 0;
}

bool Msg1c::reindexQuery ( char *query ,
			   collnum_t collnum ,
			   int32_t startNum ,
			   int32_t endNum ,
			   bool forceDel ,
			   int32_t langId,
			   void *state ,
			   void (* callback) (void *state ) ) {

	m_collnum = collnum;//           = coll;
	m_startNum       = startNum;
	m_endNum         = endNum;
	m_forceDel       = forceDel;
	m_state          = state;
	m_callback       = callback;
	m_numDocIds      = 0;
	m_numDocIdsAdded = 0;

	m_niceness = MAX_NICENESS;

	// langunknown?
	m_qq.set2 ( query , langId , true );

	// sanity fix
	if ( endNum - startNum > MAXDOCIDSTOCOMPUTE )
		endNum = startNum + MAXDOCIDSTOCOMPUTE;

	// reset again just in case
	m_req.reset();

	// set our Msg39Request
	m_req.m_collnum = m_collnum;
	m_req.m_docsToGet                 = endNum;
	m_req.m_niceness                  = 0,
	m_req.m_getDocIdScoringInfo       = false;
	m_req.m_doSiteClustering          = false;
	m_req.m_doDupContentRemoval       = false;
	m_req.ptr_query                   = m_qq.m_orig;
	m_req.size_query                  = m_qq.m_origLen+1;
	m_req.m_timeout                   = 86400*1000; // a whole day. todo: should we just go for infinite here?
	m_req.m_queryExpansion            = true; // so it's like regular rslts
	// add language dropdown or take from [query reindex] link
	m_req.m_language                  = langId;
	//m_req.m_debug = 1;

	// log for now
	logf(LOG_DEBUG,"reindex: qlangid=%" PRId32" q=%s",langId,query);

	g_errno = 0;
	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	if ( ! m_msg3a.getDocIds ( &m_req, NULL, &m_qq, this, gotDocIdListWrapper )) {
		return false;
	}

	// . this returns false if blocks, true otherwise
	// . sets g_errno on failure
	return gotList ( );
}

void gotDocIdListWrapper ( void *state ) {
	// cast
	Msg1c *m = (Msg1c *)state;

	// return if this blocked
	if ( ! m->gotList ( ) ) return;

	// call callback otherwise
	m->m_callback ( m->m_state );
}

// . this returns false if blocks, true otherwise
// . sets g_errno on failure
bool Msg1c::gotList ( ) {

	if ( g_errno ) return true;

	int64_t *tmpDocIds = m_msg3a.getDocIds();
	int32_t       numDocIds = m_msg3a.getNumDocIds();

	if ( m_startNum > 0) {
		numDocIds -= m_startNum;
		tmpDocIds = &tmpDocIds[m_startNum];
	}

	m_numDocIds = numDocIds; // save for reporting
	// log it
	log(LOG_INFO,"admin: Got %" PRId32" docIds for query reindex.", numDocIds);
	// bail if no need
	if ( numDocIds <= 0 ) return true;

	// force spiders on on entire network. they will progagate from 
	// host #0... 
	g_conf.m_spideringEnabled = true;

	int32_t nowGlobal = getTimeGlobal();

	HashTableX dt;
	char dbuf[1024];
	dt.set(8,0,64,dbuf,1024,false,"ddocids");

	m_sb.setLabel("reiadd");

	State13 *st = (State13 *)m_state;
	GigablastRequest *gr = &st->m_gr;

	m_numDocIdsAdded = 0;

	// list consists of docIds, loop through each one
 	for(int32_t i = 0; i < numDocIds; i++) {
		int64_t docId = tmpDocIds[i];
		// when searching events we get multiple docids that are same
		if ( dt.isInTable ( &docId ) ) continue;
		// add it
		if ( ! dt.addKey ( &docId ) ) return true;

		SpiderRequest sr;
		sr.reset();

		// url is a docid!
		sprintf ( sr.m_url , "%" PRIu64 , (uint64_t)docId );

		// make a fake first ip
		// use only 64k values so we don't stress doledb/waittrees/etc.
		// for large #'s of docids
		int32_t firstIp = (docId & 0x0000ffff);

		// bits 6-13 of the docid are the domain hash so use those
		// when doing a REINDEX (not delete!) to ensure that requests
		// on the same domain go to the same shard, at least when
		// we have up to 256 shards. if we have more than 256 shards
		// at this point some shards will not participate in the
		// query reindex/delete process because of this, so 
		// we'll want to allow more bits in in that case perhaps.
		// check out Hostdb::getShardNum(RDB_SPIDERDB) in Hostdb.cpp
		// to see what shard is responsible for storing and indexing 
		// this SpiderRequest based on the firstIp.
		if ( ! m_forceDel ) { 
			// if we are a REINDEX not a delete because 
			// deletes don't need to spider/redownload the doc
			// so the distribution can be more random
			firstIp >>= 6;
			firstIp &= 0xff;
		}

		// 0 is not a legit val. it'll core below.
		if ( firstIp == 0 ) {
			firstIp = 1;
		}

		// use a fake ip
		sr.m_firstIp        =  firstIp;
		// we are not really injecting...
		sr.m_isInjecting    =  false;//true;
		sr.m_hopCount       = -1;
		sr.m_isPageReindex  =  1;
		sr.m_urlIsDocId     =  1;
		sr.m_fakeFirstIp    =  1;

		// now you can recycle content instead of re-downloading it
		// for every docid
		sr.m_recycleContent = gr->m_recycleContent;
		// if this is zero we end up getting deduped in
		// dedupSpiderList() if there was a SpiderReply whose
		// spider time was > 0
		sr.m_addedTime = nowGlobal;
	    sr.m_forceDelete = m_forceDel ? 1 : 0;

		// . complete its m_key member
		// . parentDocId is used to make the key, but only allow one
		//   page reindex spider request per url... so use "0"
		// . this will set "uh48" to hash64b(m_url) which is the docid
		sr.setKey( firstIp, 0LL , false );

		// how big to serialize
		int32_t recSize = sr.getRecSize();

		m_numDocIdsAdded++;
	
		// store it
		if ( ! m_sb.safeMemcpy ( (char *)&sr , recSize ) ) {
			// g_errno must be set
			if ( ! g_errno ) { g_process.shutdownAbort(true); }

			log(LOG_LOGIC,
			    "admin: Query reindex size of %" PRId32" "
			    "too big. Aborting. Bad engineer." , 
			    (int32_t)0);//m_list.getListSize() );
			return true;
		}
	}

	// free "finalBuf" etc. for msg39
	m_msg3a.reset();

	log("reindex: adding docid list to spiderdb");

	return m_msg4.addMetaList(&m_sb, m_collnum, this, addedListWrapper, RDB_SPIDERDB);
}

void addedListWrapper ( void *state ) {
	// note that
	log("reindex: done adding list to spiderdb");
	// cast
	Msg1c *m = (Msg1c *)state;
	// call callback, all done
	m->m_callback ( m->m_state );
}
