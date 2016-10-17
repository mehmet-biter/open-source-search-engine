#include "Msg39.h"
#include "UdpSlot.h"
#include "Stats.h"
#include "JobScheduler.h"
#include "UdpServer.h"
#include "RdbList.h"
#include "Sanity.h"
#include "Posdb.h"
#include "Mem.h"
#include "GbSignature.h"
#include <new>
#include "ScopedLock.h"
#include <pthread.h>
#include <assert.h>


#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

static const int signature_init = 0x2c9a3f0e;


// called to send back the reply
static void  sendReply         ( UdpSlot *slot         ,
				 Msg39   *msg39        ,
				 char    *reply        ,
				 int32_t     replySize    ,
				 int32_t     replyMaxSize ,
				 bool     hadError     );



namespace {

//a structure for keeping while some sub-task is handled by a different thread/job
class JobState {
public:
	declare_signature
	Msg39 *msg39;
	bool result_ready;
	pthread_mutex_t mtx;
	pthread_cond_t cond;	
	JobState(Msg39 *msg39_)
	  : msg39(msg39_),
	    result_ready(false)
	{
		pthread_mutex_init(&mtx,NULL);
		pthread_cond_init(&cond,NULL);
		set_signature();
	}
	~JobState() {
		verify_signature();
		pthread_mutex_destroy(&mtx);
		pthread_cond_destroy(&cond);
		clear_signature();
	}
	void wait_for_finish() {
		verify_signature();
		ScopedLock sl(mtx);
		while(!result_ready)
			pthread_cond_wait(&cond,&mtx);
		verify_signature();
	}
};

//a simple function just signals that the job has been finished
static void JobFinishedCallback(void *state) {
	JobState *js = static_cast<JobState*>(state);
	verify_signature_at(js->signature);
	ScopedLock sl(js->mtx);
	assert(!js->result_ready); //can only be called once
	js->result_ready = true;
	int rc = pthread_cond_signal(&js->cond);
	assert(rc==0);
}

} //anonymous namespace



void Msg39Request::reset() {
	memset(this, 0, sizeof(*this));
	m_docsToGet               = 10;
	m_niceness                = MAX_NICENESS;
	m_maxAge                  = 0;
	m_maxQueryTerms           = 9999;
	//m_compoundListMaxSize     = 20000000;
	m_language                = 0;
	m_queryExpansion          = false;
	m_debug                   = 0;
	m_getDocIdScoringInfo     = true;
	m_doSiteClustering        = true;
	m_hideAllClustered        = false;
	//m_doIpClustering          = true;
	m_doDupContentRemoval     = true;
	m_addToCache              = false;
	m_familyFilter            = false;
	m_timeout                 = -1; // -1 means auto-compute
	m_stripe                  = 0;
	m_collnum                 = -1;
	m_useQueryStopWords       = true;
	m_doMaxScoreAlgo          = true;

	ptr_query                 = NULL; // in utf8?
	ptr_whiteList             = NULL;
	size_query                = 0;
	size_whiteList            = 0;
	m_sameLangWeight          = 20.0;

	// -1 means to not to docid range restriction
	m_minDocId = -1LL;
	m_maxDocId = -1LL;

	m_numDocIdSplits = 1;

	// for widget, to only get results to append to last docid
	m_maxSerpScore = 0.0;
	m_minSerpDocId = 0LL;

	// . search results knobs
	// . accumulate the top 10 term pairs from inlink text. lower
	//   it down from 10 here.
	m_realMaxTop = MAX_TOP;
}


bool Msg39::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x39
	if ( ! g_udpServer.registerHandler ( msg_type_39, &handleRequest39 ))
		return false;
	return true;
}


Msg39::Msg39 ()
  : m_lists(NULL),
    m_clusterBuf(NULL)
{
	m_inUse = false;
	reset();
}


Msg39::~Msg39 () {
	reset();
}


void Msg39::reset() {
	if ( m_inUse ) gbshutdownLogicError();
	m_allocatedTree = false;
	//m_numDocIdSplits = 1;
	m_query.reset();
	m_numTotalHits = 0;
	m_gotClusterRecs = 0;
	reset2();
	if(m_clusterBuf) {
		mfree ( m_clusterBuf, m_clusterBufSize, "Msg39cluster");
		m_clusterBuf = NULL;
	}

	// Coverity
	m_slot = NULL;
	m_msg39req = NULL;
	m_startTime = 0;
	m_startTimeQuery = 0;
	m_errno = 0;
	m_clusterBufSize = 0;
	m_clusterDocIds = NULL;
	m_clusterLevels = NULL;
	m_clusterRecs = NULL;
	m_numClusterDocIds = 0;
	m_numVisible = 0;
	m_debug = false;
}


void Msg39::reset2() {
	delete[] m_lists;
	m_lists = NULL;
	m_msg2.reset();
	m_posdbTable.reset();
}


// . handle a request to get a the search results, list of docids only
// . returns false if slot should be nuked and no reply sent
// . sometimes sets g_errno on error
void Msg39::handleRequest39(UdpSlot *slot, int32_t netnice) {
	// use Msg39 to get the lists and intersect them
	try {
		Msg39 *that = new Msg39;
		//register msg39 memory
		mnew ( that, sizeof(Msg39) , "Msg39" );
		// clear it
		g_errno = 0;
		// . get the resulting docIds, usually blocks
		// . sets g_errno on error
		that->getDocIds ( slot ) ;
	} catch(std::bad_alloc) {
		g_errno = ENOMEM;
		log("msg39: new(%" PRId32"): %s", 
		    (int32_t)sizeof(Msg39),mstrerror(g_errno));
		sendReply ( slot , NULL , NULL , 0 , 0 ,true);
	}
}

// this must always be called sometime AFTER handleRequest() is called
static void sendReply ( UdpSlot *slot , Msg39 *msg39 , char *reply , int32_t replyLen ,
		 int32_t replyMaxSize , bool hadError ) {
	// debug msg
	if ( g_conf.m_logDebugQuery || (msg39&&msg39->m_debug) ) 
		logf(LOG_DEBUG,"query: msg39: [%" PTRFMT"] "
		     "Sending reply len=%" PRId32".",
		     (PTRTYPE)msg39,replyLen);

	// sanity
	if ( hadError && ! g_errno ) gbshutdownLogicError();

	// no longer in use. msg39 will be NULL if ENOMEM or something
	if ( msg39 ) msg39->m_inUse = false;

	// i guess clear this
	int32_t err = g_errno;
	g_errno = 0;
	// send an error reply if g_errno is set
	if ( err ) {
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply. error=%s", __FILE__, __func__, __LINE__, mstrerror(err));
		g_udpServer.sendErrorReply( slot, err );
	} else {
		g_udpServer.sendReply(reply, replyLen, reply, replyMaxSize, slot);
	}

	// always delete ourselves when done handling the request
	if ( msg39 ) {
		mdelete ( msg39 , sizeof(Msg39) , "Msg39" );
		delete (msg39);
//msg39->~Msg39();
//memset(msg39,-3,sizeof(*msg39));
//::operator delete((void*)msg39);
	}
}


// . sets g_errno on error
// . calls gotDocIds to send a reply
void Msg39::getDocIds ( UdpSlot *slot ) {
	// remember the slot
	m_slot = slot;
	// reset this
	m_errno = 0;
	// get the request
        m_msg39req  = reinterpret_cast<Msg39Request*>(m_slot->m_readBuf);
        int32_t requestSize = m_slot->m_readBufSize;
        // ensure it's size is ok
        if ( (unsigned)requestSize < sizeof(Msg39Request) ) {
		g_errno = EBADREQUESTSIZE;
		log(LOG_ERROR,"query: msg39: getDocIds: msg39request is too small (%d bytes): %s.",
		    requestSize, mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return;
	}

	// deserialize it before we do anything else
	int32_t finalSize = deserializeMsg ( sizeof(Msg39Request),
					     &m_msg39req->size_termFreqWeights,
					     &m_msg39req->size_whiteList,
					     &m_msg39req->ptr_termFreqWeights,
					     ((char*)m_msg39req) + sizeof(*m_msg39req) );

	// sanity check
	if ( finalSize != requestSize ) {
		log("msg39: sending bad request.");
		g_errno = EBADREQUESTSIZE;
		log(LOG_ERROR,"query: msg39: getDocIds: msg39request deserialization size mismatch (%d != %d): %s.",
		    finalSize, requestSize, mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return;
	}

	log(LOG_DEBUG,"query: msg39: processing query '%*.*s', this=%p", (int)m_msg39req->size_query, (int)m_msg39req->size_query, m_msg39req->ptr_query, this);
	// OK, we have deserialized and checked the msg39request and we can now process
	// it by shoveling into the jobe queue. that means that the main thread (or whoever
	// called us) is freed up and can do other stuff.
	if(!g_jobScheduler.submit(&coordinatorThreadFunc,
				  NULL,                          //finish-callback. We don't care
				  this,
				  thread_type_query_coordinator,
				  m_msg39req->m_niceness))
	{
		log(LOG_ERROR,"Could not add query-coordinator job. Doing it in foreground");
		getDocIds2();
	}
}


void Msg39::coordinatorThreadFunc(void *state) {
	Msg39 *that = static_cast<Msg39*>(state);
	log(LOG_DEBUG, "query: msg39: in coordinatorThreadFunc: this=%p", that);
	that->getDocIds2();
}


// . the main function to get the docids for the provided query in "req"
// . it always blocks i guess
void Msg39::getDocIds2() {

	// flag it as in use
	m_inUse = true;
	
	//record start time of the query
	m_startTimeQuery = gettimeofdayInMilliseconds();

	// a handy thing
	m_debug = false;
	if ( m_msg39req->m_debug       ) m_debug = true;
	if ( g_conf.m_logDebugQuery  ) m_debug = true;
	if ( g_conf.m_logTimingQuery ) m_debug = true;

        CollectionRec *cr = g_collectiondb.getRec ( m_msg39req->m_collnum );
        if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log(LOG_LOGIC,"query: msg39: getDocIds: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}

	// . set our m_query instance
	if ( ! m_query.set2 ( m_msg39req->ptr_query,
			      m_msg39req->m_language ,
			      m_msg39req->m_queryExpansion ,
			      m_msg39req->m_useQueryStopWords ,
			      m_msg39req->m_maxQueryTerms ) ) {
		log("query: msg39: setQuery: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}

	// wtf?
	if ( g_errno ) gbshutdownLogicError();

	// set m_errno
	if ( m_query.m_truncated ) m_errno = EQUERYTRUNCATED;
	// ensure matches with the msg3a sending us this request
	if ( m_query.getNumTerms() != m_msg39req->m_nqt ) {
		g_errno = EBADENGINEER;
		log("query: Query parsing inconsistency for q=%s. "
		    "%i != %i. "
		    "langid=%" PRId32". Check langids and m_queryExpansion parms "
		    "which are the only parms that could be different in "
		    "Query::set2(). You probably have different mysynoyms.txt "
		    "files on two different hosts! check that!!"
		    ,m_query.m_orig
		    ,(int)m_query.getNumTerms()
		    ,(int)m_msg39req->m_nqt
		    ,(int32_t)m_msg39req->m_language
		    );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}
	// debug
	if ( m_debug )
		logf(LOG_DEBUG,"query: msg39: [%" PTRFMT"] Got request "
		     "for q=%s", (PTRTYPE) this,m_query.m_orig);

	// reset this
	m_toptree.reset();

	controlLoop();
}


// . returns false if blocks true otherwise
// 1. read all termlists for docid range
// 2. intersect termlists to get the intersecting docids
// 3. increment docid ranges and keep going
// 4. when done return the top docids
void Msg39::controlLoop ( ) {
//	log(LOG_DEBUG,"query: Msg39(%p)::controlLoop(): m_msg39req->m_numDocIdSplits=%d m_msg39req->m_timeout=%" PRId64, this, m_msg39req->m_numDocIdSplits, m_msg39req->m_timeout);
//log("@@@ Msg39::controlLoop: m_startTimeQuery=%" PRId64, m_startTimeQuery);
	//log("@@@ Msg39::controlLoop: now             =%" PRId64, gettimeofdayInMilliseconds());
	
	const int numFiles = getRdbBase(RDB_POSDB,m_msg39req->m_collnum)->getNumFiles();
//	log(LOG_DEBUG,"controlLoop(): numFiles=%d",numFiles);
	
	//todo: choose docid splits based on expected largest rdblist / most common term
	int numDocIdSplits = 1;
//	log(LOG_DEBUG,"controlLoop(): numDocIdSplits=%d",numDocIdSplits);
	
	const int totalChunks = (numFiles+1)*numDocIdSplits;
	int chunksSearched = 0;
	
	for(int fileNum = 0; fileNum<numFiles+1; fileNum++) {
//		if(fileNum!=numFiles)
//			log(LOG_DEBUG,"controlLoop(): fileNum=%d (of %d)", fileNum, numFiles);
//		else
//			log(LOG_DEBUG,"controlLoop(): fileNum=tree");
		
		int64_t docidRangeStart = 0;
		const int64_t docidRangeDelta = MAX_DOCID / (int64_t)numDocIdSplits;
		
		for(int docIdSplitNumber = 0; docIdSplitNumber < numDocIdSplits; docIdSplitNumber++) {
//			log(LOG_DEBUG,"controlLoop(): splitNumber=%d (of %d)", docIdSplitNumber, numDocIdSplits);
			
			if(docIdSplitNumber!=0) {
				//Estimate if we can do this and next ranges within the deadline
				int64_t now = gettimeofdayInMilliseconds();
				int64_t time_spent_so_far = now - m_startTimeQuery;
				int64_t time_per_range = time_spent_so_far / docIdSplitNumber;
				int64_t estimated_this_range_finish_time = now + time_per_range;
				int64_t deadline = m_startTimeQuery + m_msg39req->m_timeout;
				log(LOG_DEBUG,"query: Msg39::controlLoop(): now=%" PRId64" time_spent_so_far=%" PRId64" time_per_range=%" PRId64" estimated_this_range_finish_time=%" PRId64" deadline=%" PRId64,
				    now, time_spent_so_far, time_per_range, estimated_this_range_finish_time, deadline);
				if(estimated_this_range_finish_time > deadline) {
					//estimated completion time crosses the deadline.
					log(LOG_INFO,"Msg39::controlLoop(): range %d/%d would cross deadline. Skipping", docIdSplitNumber, m_msg39req->m_numDocIdSplits);
					goto skipRest;
				}
			}
			
			// Reset ourselves, partially, anyway, not m_query etc.
			reset2();
			
			// Calculate docid range and fetch lists
			int64_t d0 = docidRangeStart;
			docidRangeStart += docidRangeDelta;
			if(docIdSplitNumber+1 == numDocIdSplits)
				docidRangeStart = MAX_DOCID;
			else if(docidRangeStart + 20 > MAX_DOCID)
				docidRangeStart = MAX_DOCID;
			int64_t d1 = docidRangeStart;
			
//			log(LOG_DEBUG,"docid range: [%" PRId64"..%" PRIu64")", d0,d1);
			if(fileNum!=numFiles)
				getLists(fileNum,d0,d1);
			else
				getLists(-1,d0,d1);
			if ( g_errno ) {
				log(LOG_ERROR,"Msg39::controlLoop: got error %d after getLists()", g_errno);
				goto hadError;
			}

			// Intersect the lists we loaded (using a thread)
			intersectLists();
			if ( g_errno ) {
				log(LOG_ERROR,"Msg39::controlLoop: got error %d after intersectLists()", g_errno);
				goto hadError;
			}
			
			// Sum up stats
			if ( m_posdbTable.m_t1 ) {
				// . measure time to add the lists in bright green
				// . use darker green if rat is false (default OR)
				g_stats.addStat_r ( 0, m_posdbTable.m_t1, m_posdbTable.m_t2, 0x0000ff00 );
			}
			// accumulate total hits count over each docid split
			m_numTotalHits += m_posdbTable.getTotalHits();
			// minus the shit we filtered out because of gbminint/gbmaxint/
			// gbmin/gbmax/gbsortby/gbrevsortby/gbsortbyint/gbrevsortbyint
			m_numTotalHits -= m_posdbTable.m_filtered;
			
			chunksSearched++;
		}
//		log(LOG_DEBUG,"controlLoop(): End of docid-split loop");
	}
skipRest:
//	log(LOG_DEBUG,"controlLoop(): End of file loop");
	

//	log(LOG_DEBUG, "query: msg39(this=%p): All chunks done. Now getting cluster records",this);
	
	// ok, we are done, get cluster recs of the winning docids
	// . this loads them using msg51 from clusterdb
	// . if m_msg39req->m_doSiteClustering is false it just returns true
	// . this sets m_gotClusterRecs to true if we get them
	getClusterRecs();
	// error setting clusterrecs?
	if ( g_errno ) {
		log(LOG_ERROR,"Msg39::controlLoop: got error %d after getClusterRecs()", g_errno);
		goto hadError;
	}

	// process the cluster recs if we got them
	if ( m_gotClusterRecs && ! gotClusterRecs() ) {
		log(LOG_ERROR,"Msg39::controlLoop: got error after gotClusterRecs()");
		goto hadError;
	}

	// . all done! set stats and send back reply
	// . only sends back the cluster recs if m_gotClusterRecs is true
	estimateHitsAndSendReply(chunksSearched/(double)totalChunks);

	return;

hadError:
	log(LOG_LOGIC,"query: msg39: controlLoop: got error: %s.", mstrerror(g_errno) );
	sendReply ( m_slot, this, NULL, 0, 0, true );
}



// . returns false if blocked, true otherwise
// . sets g_errno on error
// . called either from 
//   1) doDocIdSplitLoop
//   2) or getDocIds2() if only 1 docidsplit
void Msg39::getLists(int fileNum, int64_t docIdStart, int64_t docIdEnd) {
	log(LOG_DEBUG, "query: msg39(this=%p)::getLists()",this);

	if ( m_debug ) m_startTime = gettimeofdayInMilliseconds();
	// . ask Indexdb for the IndexLists we need for these termIds
	// . each rec in an IndexList is a termId/score/docId tuple

	// . restrict to this docid?
	// . will really make gbdocid:| searches much faster!
	int64_t dr = m_query.m_docIdRestriction;
	if ( dr ) {
		docIdStart = dr;
		docIdEnd   = dr + 1;
	}
	
	// if we have twins, then make sure the twins read different
	// pieces of the same docid range to make things 2x faster
	int32_t numStripes = g_hostdb.getNumStripes();
	int64_t delta2 = ( docIdEnd - docIdStart ) / numStripes;
	int32_t stripe = g_hostdb.getMyHost()->m_stripe;
	docIdStart += delta2 * stripe; // is this right? // BR 20160313: Doubt it..
	docIdEnd = docIdStart + delta2;
	// add 1 to be safe so we don't lose a docid
	docIdEnd++;
	// TODO: add triplet support later for this to split the
	// read 3 ways. 4 ways for quads, etc.
	//if ( g_hostdb.getNumStripes() >= 3 ) gbshutdownLogicError();
	// do not go over MAX_DOCID  because it gets masked and
	// ends up being 0!!! and we get empty lists
	if ( docIdEnd > MAX_DOCID ) docIdEnd = MAX_DOCID;

	if ( g_conf.m_logDebugQuery )
	{
		log(LOG_DEBUG,"query: docId start %" PRId64, docIdStart);
		log(LOG_DEBUG,"query: docId   end %" PRId64, docIdEnd);
	}

	//
	// set startkey/endkey for each term/termlist
	//
	for ( int32_t i = 0 ; i < m_query.getNumTerms() ; i++ ) {
		// shortcuts
		QueryTerm *qterm = &m_query.m_qterms[i];
		char *sk = qterm->m_startKey;
		char *ek = qterm->m_endKey;
		// get the term id
		int64_t tid = m_query.getTermId(i);
		// if only 1 stripe
		//if ( g_hostdb.getNumStripes() == 1 ) {
		//	docIdStart = 0;
		//	docIdEnd   = MAX_DOCID;
		//}
		// debug
		if ( m_debug )
			log("query: setting sk/ek for docids %" PRId64
			    " to %" PRId64" for termid=%" PRId64
			    , docIdStart
			    , docIdEnd
			    , tid
			    );
		// store now in qterm
		g_posdb.makeStartKey ( sk , tid , docIdStart );
		g_posdb.makeEndKey   ( ek , tid , docIdEnd   );
		qterm->m_ks = sizeof(posdbkey_t);
	}

	// debug msg
	if ( m_debug || g_conf.m_logDebugQuery ) {
		for ( int32_t i = 0 ; i < m_query.getNumTerms() ; i++ ) {
			// get the term in utf8
			//char bb[256];
			const QueryTerm *qt = &m_query.m_qterms[i];
			//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
			//char *tpc = qt->m_term + qt->m_termLen;
			char sign = qt->m_termSign;
			if ( sign == 0 ) sign = '0';
			QueryWord *qw = qt->m_qword;
			int32_t wikiPhrId = qw->m_wikiPhraseId;
			if ( m_query.isPhrase(i) ) wikiPhrId = 0;
			char leftwikibigram = 0;
			char rightwikibigram = 0;
			if ( qt->m_leftPhraseTerm &&
			     qt->m_leftPhraseTerm->m_isWikiHalfStopBigram )
				leftwikibigram = 1;
			if ( qt->m_rightPhraseTerm &&
			     qt->m_rightPhraseTerm->m_isWikiHalfStopBigram )
				rightwikibigram = 1;

			int32_t isSynonym = 0;
			const QueryTerm *synterm = qt->m_synonymOf;
			if ( synterm )
				isSynonym = true;
			SafeBuf sb;
			sb.safePrintf(
			     "query: msg39: [%" PTRFMT"] "
			     "query term #%" PRId32" \"%*.*s\" "
			     "phr=%" PRId32" termId=%" PRIu64" rawTermId=%" PRIu64" "
			     "tfweight=%.02f "
			     "sign=%c "
			     "numPlusses=%hhu "
			     "required=%" PRId32" "
			     "fieldcode=%" PRId32" "

			     "ebit=0x%0" PRIx64" "
			     "impBits=0x%0" PRIx64" "

			     "wikiphrid=%" PRId32" "
			     "leftwikibigram=%" PRId32" "
			     "rightwikibigram=%" PRId32" "
			     "hc=%" PRId32" "
			     "otermLen=%" PRId32" "
			     "isSynonym=%" PRId32" "
			     "querylangid=%" PRId32" " ,
			     (PTRTYPE)this ,
			     i          ,
			     (int)qt->m_termLen, (int)qt->m_termLen, qt->m_term,
			     (int32_t)m_query.isPhrase(i) ,
			     m_query.getTermId(i) ,
			     m_query.getRawTermId(i) ,
			     ((float *)m_msg39req->ptr_termFreqWeights)[i] ,
			     sign , //c ,
			     0 , 
			     (int32_t)qt->m_isRequired,
			     (int32_t)qt->m_fieldCode,

			     (int64_t)qt->m_explicitBit  ,
			     (int64_t)qt->m_implicitBits ,

			     wikiPhrId,
			     (int32_t)leftwikibigram,
			     (int32_t)rightwikibigram,
			     (int32_t)m_query.m_qterms[i].m_hardCount ,
			     (int32_t)m_query.getTermLen(i) ,
			     isSynonym,
			     (int32_t)m_query.m_langId );
			if ( synterm ) {
				int32_t stnum = synterm - m_query.m_qterms;
				sb.safePrintf("synofterm#=%" PRId32,stnum);
				//sb.safeMemcpy(st->m_term,st->m_termLen);
				sb.pushChar(' ');
				sb.safePrintf("synwid0=%" PRId64" ",qt->m_synWids0);
				sb.safePrintf("synwid1=%" PRId64" ",qt->m_synWids1);
				sb.safePrintf("synalnumwords=%" PRId32" ",
					      qt->m_numAlnumWordsInSynonym);
				// like for synonym "nj" it's base,
				// "new jersey" has 2 alnum words!
				sb.safePrintf("synbasealnumwords=%" PRId32" ",
					      qt->m_numAlnumWordsInBase);
				sb.safePrintf("synterm=\"%*.*s\" ", (int)synterm->m_termLen, (int)synterm->m_termLen, synterm->m_term);
			}
			logf(LOG_DEBUG,"%s",sb.getBufStart());

		}
	}
	// timestamp log
	if ( m_debug ) 
		log(LOG_DEBUG,"query: msg39: [%" PTRFMT"] "
		    "Getting %" PRId32" index lists ",
		     (PTRTYPE)this,m_query.getNumTerms());
	// . now get the index lists themselves
	// . return if it blocked
	// . not doing a merge (last parm) means that the lists we receive
	//   will be an appending of a bunch of lists so keys won't be in order
	// . merging is uneccessary for us here because we hash the keys anyway
	// . and merging takes up valuable cpu time
	// . caution: the index lists returned from Msg2 are now compressed
	// . now i'm merging because it's 10 times faster than hashing anyway
	//   and the reply buf should now always be <= minRecSizes so we can
	//   pre-allocate one better, and, 3) this should fix the yahoo.com 
	//   reindex bug


	int32_t nqt = m_query.getNumTerms();
	try {
		m_lists = new RdbList[nqt];
	} catch(std::bad_alloc) {
		log(LOG_ERROR,"new[%d] RdbList failed", nqt);
		g_errno = ENOMEM;
		return;
	}

	JobState jobState(this);
	
	// call msg2
	if ( ! m_msg2.getLists ( m_msg39req->m_collnum,
				 m_msg39req->m_addToCache,
				 m_query.m_qterms,
				 m_query.getNumTerms(),
				 m_msg39req->ptr_whiteList,
				 // we need to restrict docid range for
				 // whitelist as well! this is from
				 // doDocIdSplitLoop()
				 fileNum,
				 docIdStart,
				 docIdEnd,
				 //m_query.getNumTerms(),
				 // 1-1 with query terms
				 m_lists                    ,
				 &jobState,                                 //state
				 &JobFinishedCallback,                      //callback
				 m_msg39req->m_allowHighFrequencyTermCache,
				 m_msg39req->m_niceness,
				 m_debug                      )) {
		log(LOG_DEBUG,"m_msg2.getLists returned false - waiting for job to finish");
		jobState.wait_for_finish();
	} else
		log(LOG_DEBUG,"m_msg2.getLists returned true. Must be done");
}



// . now come here when we got the necessary index lists
// . returns false if blocked, true otherwise
// . sets g_errno on error
void Msg39::intersectLists ( ) {
	log(LOG_DEBUG, "query: msg39(this=%p)::intersectLists()",this);
	// timestamp log
	if ( m_debug ) {
		log(LOG_DEBUG,"query: msg39: [%" PTRFMT"] "
		    "Got %" PRId32" lists in %" PRId64" ms"
		    , (PTRTYPE)this,m_query.getNumTerms(),
		     gettimeofdayInMilliseconds() - m_startTime);
		m_startTime = gettimeofdayInMilliseconds();
	}

	// ensure collection not deleted from under us
	if ( ! g_collectiondb.getRec ( m_msg39req->m_collnum ) ) {
		g_errno = ENOCOLLREC;
		log("msg39: Had error getting termlists: %s.",
		    mstrerror(g_errno));
		return;
	}

	// . set the IndexTable so it can set it's score weights from the
	//   termFreqs of each termId in the query
	// . this now takes into account the special termIds used for sorting
	//   by date (0xdadadada and 0xdadadad2 & TERMID_MASK)
	// . it should weight them so much so that the summation of scores
	//   from other query terms cannot make up for a lower date score
	// . this will actually calculate the top
	// . this might also change m_query.m_termSigns
	// . this won't do anything if it was already called
	m_posdbTable.init ( &m_query, m_debug, this, &m_toptree, &m_msg2, m_msg39req);

	// . we have to do this here now too
	// . but if we are getting weights, we don't need m_toptree!
	// . actually we were using it before for rat=0/bool queries but
	//   i got rid of NO_RAT_SLOTS
	if ( ! m_allocatedTree && ! m_posdbTable.allocTopScoringDocIdsData() ) {
		if ( ! g_errno ) {
			gbshutdownLogicError();
		}
		//sendReply ( m_slot , this , NULL , 0 , 0 , true);
		return;
	}

	// if msg2 had ALL empty lists we can cut it short
	if ( m_posdbTable.m_topTree->m_numNodes == 0 ) {
		//estimateHitsAndSendReply ( );
		return;
	}

	// we have to allocate this with each call because each call can
	// be a different docid range from doDocIdSplitLoop.
	if ( ! m_posdbTable.allocWhiteListTable() ) {
		log(LOG_WARN,"msg39: Had error allocating white list table: %s.", mstrerror(g_errno));
		if ( ! g_errno ) {
			gbshutdownLogicError();
		}
		//sendReply (m_slot,this,NULL,0,0,true);
		return;
	}

	// do not re do it if doing docid range splitting
	m_allocatedTree = true;

	// . now we must call this separately here, not in allocTopScoringDocIdsData()
	// . we have to re-set the QueryTermInfos with each docid range split
	//   since it will set the list ptrs from the msg2 lists
	if ( ! m_posdbTable.setQueryTermInfo () ) {
		return;
	}

	// print query term bit numbers here
	for ( int32_t i = 0 ; m_debug && i < m_query.getNumTerms() ; i++ ) {
		const QueryTerm *qt = &m_query.m_qterms[i];
		SafeBuf sb;
		sb.safePrintf("query: msg39: BITNUM query term #%" PRId32" \"%*.*s\" "
			      "termid=%" PRId64" bitnum=%" PRId32" ",
			      i,
			      (int)qt->m_termLen, (int)qt->m_termLen, qt->m_term,
			      qt->m_termId, qt->m_bitNum );
		// put it back
		logf(LOG_DEBUG,"%s",sb.getBufStart());
	}


	// timestamp log
	if ( m_debug ) {
		log(LOG_DEBUG,"query: msg39: [%" PTRFMT"] "
		    "Preparing to intersect "
		     "took %" PRId64" ms",
		     (PTRTYPE)this, 
		    gettimeofdayInMilliseconds() - m_startTime );
		m_startTime = gettimeofdayInMilliseconds();
	}

	JobState jobState(this);
	
	// time it
	int64_t start = gettimeofdayInMilliseconds();

	// check tree
	if ( m_toptree.m_nodes == NULL ) {
		log(LOG_LOGIC,"query: msg39: Badness."); 
		gbshutdownLogicError();
	}

	// . create the thread
	// . only one of these type of threads should be launched at a time
	if ( g_jobScheduler.submit(&intersectListsThreadFunction,
	                           0, //no finish callback
				   &jobState,
				   thread_type_query_intersect,
				   m_msg39req->m_niceness) ) {
		jobState.wait_for_finish();
	} else
		m_posdbTable.intersectLists10_r();
	

	// time it
	int64_t diff = gettimeofdayInMilliseconds() - start;
	if ( diff > 10 ) log("query: Intersection job took %" PRId64" ms",diff);
	log(LOG_DEBUG, "query: msg39(this=%p)::intersectLists() finished",this);
}



// Use of ThreadEntry parameter is NOT thread safe
void Msg39::intersectListsThreadFunction ( void *state ) {
	JobState *js = static_cast<JobState*>(state);
	Msg39 *that = js->msg39;

	// assume no error since we're at the start of thread call
	that->m_errno = 0;

	// . do the add
	// . addLists() returns false and sets errno on error
	// . hash the lists into our table
	// . this returns false and sets g_errno on error
	// . Msg2 always compresses the lists so be aware that the termId
	//   has been discarded
	that->m_posdbTable.intersectLists10_r ( );

	// . exit the thread
	// . threadDoneWrapper will be called by g_loop when he gets the 
	//   thread's termination signal
	if (g_errno && !that->m_errno) {
		that->m_errno = g_errno;
	}

	//signal completion directly instead of goiign via the jobscheduler+main thread
	JobFinishedCallback(state);
}


// . set the clusterdb recs in the top tree
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
void Msg39::getClusterRecs ( ) {

	if ( ! m_msg39req->m_doSiteClustering )
		return; //nothing to do

	// make buf for arrays of the docids, cluster levels and cluster recs
	int32_t nodeSize  = 8 + 1 + 12;
	int32_t numDocIds = m_toptree.m_numUsedNodes;
	m_clusterBufSize = numDocIds * nodeSize;
	m_clusterBuf = (char *)mmalloc(m_clusterBufSize, "Msg39cluster");
	// on error, return true, g_errno should be set
	if ( ! m_clusterBuf ) {
		log("query: msg39: Failed to alloc buf for clustering.");
		sendReply(m_slot,this,NULL,0,0,true);
		return;
	}

	// assume we got them
	m_gotClusterRecs = true;

	// parse out the buf
	char *p = m_clusterBuf;
	// docIds
	m_clusterDocIds = (int64_t *)p; p += numDocIds * 8;
	m_clusterLevels = (char      *)p; p += numDocIds * 1;
	m_clusterRecs   = (key96_t     *)p; p += numDocIds * 12;
	// sanity check
	if ( p > m_clusterBuf + m_clusterBufSize ) gbshutdownLogicError();
	
	// loop over all results
	int32_t nd = 0;
	for ( int32_t ti = m_toptree.getHighNode();
	      ti >= 0 ;
	      ti = m_toptree.getPrev(ti) , nd++ ) {
		// get the guy
		TopNode *t = &m_toptree.m_nodes[ti];
		// get the docid
		//int64_t  docId = getDocIdFromPtr(t->m_docIdPtr);
		// store in array
		m_clusterDocIds[nd] = t->m_docId;
		// assume not gotten
		m_clusterLevels[nd] = CR_UNINIT;
		// assume not found, make the whole thing is 0
		m_clusterRecs[nd].n1 = 0;
		m_clusterRecs[nd].n0 = 0LL;
	}

	// store number
	m_numClusterDocIds = nd;

	// sanity check
	if ( nd != m_toptree.m_numUsedNodes ) gbshutdownLogicError();

	JobState jobState(this);
	
	// . ask msg51 to get us the cluster recs
	// . it should read it all from the local drives
	// . "maxAge" of 0 means to not get from cache (does not include disk)
	if ( ! m_msg51.getClusterRecs ( m_clusterDocIds       ,
					m_clusterLevels       ,
					m_clusterRecs         ,
					m_numClusterDocIds    ,
					m_msg39req->m_collnum,
					0                     , // maxAge
					false                 , // addToCache
					&jobState,              //state
					&JobFinishedCallback,   //callback
					m_msg39req->m_niceness,
					m_debug             ) )
	{
		jobState.wait_for_finish();
	}
}


// return false and set g_errno on error
bool Msg39::gotClusterRecs() {
	if(!m_gotClusterRecs)
		return true; //nothing to do

	if(!setClusterLevels(m_clusterRecs,
			     m_clusterDocIds,
			     m_numClusterDocIds,
			     2,  // maxdocidsperhostname (todo: configurable)
			     m_msg39req->m_doSiteClustering,
			     m_msg39req->m_familyFilter,
			     m_debug,
			     m_clusterLevels))
	{
		m_errno = g_errno;
		return false;
	}

	m_numVisible = 0;

	// now put the info back into the top tree
	int32_t nd = 0;
	for(int32_t ti = m_toptree.getHighNode();
	    ti >= 0;
	    ti = m_toptree.getPrev(ti) , nd++ ) {
		// get the guy
		TopNode *t = &m_toptree.m_nodes[ti];
		// sanity check
		if(t->m_docId!=m_clusterDocIds[nd]) gbshutdownLogicError();
		// set it
		t->m_clusterLevel = m_clusterLevels[nd];
		t->m_clusterRec   = m_clusterRecs  [nd];
		// visible?
		if(t->m_clusterLevel==CR_OK)
			m_numVisible++;
	}

	log(LOG_DEBUG,"query: msg39: %" PRId32" docids out of %" PRId32" are visible",
	    m_numVisible,nd);

	// we don't need this anymore
	mfree ( m_clusterBuf, m_clusterBufSize, "Msg39cluster");
	m_clusterBuf = NULL;

	return true;
}


void Msg39::estimateHitsAndSendReply(double pctSearched) {

	// no longer in use
	m_inUse = false;

	// numDocIds counts docs in all tiers when using toptree.
	int32_t numDocIds = m_toptree.m_numUsedNodes;

	// if we got clusterdb recs in here, use 'em
	if(m_gotClusterRecs)
		numDocIds = m_numVisible;

	// don't send more than the docs that are asked for
	if(numDocIds>m_msg39req->m_docsToGet)
		numDocIds = m_msg39req->m_docsToGet;

	// # of QueryTerms in query
	int32_t nqt = m_query.m_numTerms;

	// make the reply
	Msg39Reply mr;
	mr.reset();
	mr.m_numDocIds = numDocIds;
	// . total estimated hits
	mr.m_estimatedHits = m_numTotalHits;  //this is now an EXACT count
	mr.m_pctSearched = pctSearched;
	// sanity check
	mr.m_nqt = nqt;
	// the m_errno if any
	mr.m_errno = m_errno;
	// shortcut
	PosdbTable *pt = &m_posdbTable;
	// the score info, in no particular order right now
	mr.ptr_scoreInfo  = pt->m_scoreInfoBuf.getBufStart();
	mr.size_scoreInfo = pt->m_scoreInfoBuf.length();
	// that has offset references into posdbtable::m_pairScoreBuf
	// and m_singleScoreBuf, so we need those too now
	mr.ptr_pairScoreBuf    = pt->m_pairScoreBuf.getBufStart();
	mr.size_pairScoreBuf   = pt->m_pairScoreBuf.length();
	mr.ptr_singleScoreBuf  = pt->m_singleScoreBuf.getBufStart();
	mr.size_singleScoreBuf = pt->m_singleScoreBuf.length();

	// reserve space for these guys, we fill them in below
	mr.ptr_docIds       = NULL;
	mr.ptr_scores       = NULL;
	mr.ptr_clusterRecs  = NULL;
	// this is how much space to reserve
	mr.size_docIds      = sizeof(int64_t) * numDocIds;
	mr.size_scores      = sizeof(double) * numDocIds;
	// if not doing site clustering, we won't have these perhaps...
	if(m_gotClusterRecs)
		mr.size_clusterRecs = sizeof(key96_t) *numDocIds;
	else
		mr.size_clusterRecs = 0;

	// . that is pretty much it,so serialize it into buffer,"reply"
	// . mr.ptr_docIds, etc., will point into the buffer so we can
	//   re-serialize into it below from the tree
	// . returns NULL and sets g_errno on error
	// . "true" means we should make mr.ptr_* reference into the
	//   newly  serialized buffer.
	int32_t  replySize;
	char *reply = serializeMsg(sizeof(Msg39Reply),   // baseSize
				   &mr.size_docIds,      // firstSizeParm
				   &mr.size_clusterRecs, // lastSizePrm
				   &mr.ptr_docIds,       // firstStrPtr
				   &mr,                  // thisPtr
				   &replySize,
				   NULL,
				   0,
				   true);
	if(!reply){
		log("query: Could not allocated memory "
		    "to hold reply of docids to send back.");
		sendReply(m_slot,this,NULL,0,0,true);
		return;
	}
	int64_t *topDocIds = (int64_t*)mr.ptr_docIds;
	double *topScores  = (double*) mr.ptr_scores;
	key96_t *topRecs     = (key96_t*)  mr.ptr_clusterRecs;

	// sanity
	if(nqt!=m_msg2.getNumLists())
		log("query: nqt mismatch for q=%s",m_query.m_orig);

	int32_t docCount = 0;
	// loop over all results in the TopTree
	for(int32_t ti = m_toptree.getHighNode();
	    ti >= 0;
	    ti = m_toptree.getPrev(ti))
	{
		// get the guy
		TopNode *t = &m_toptree.m_nodes[ti];
		// skip if clusterLevel is bad!
		if(m_gotClusterRecs && t->m_clusterLevel!=CR_OK)
			continue;

		// sanity check
		if(t->m_docId<0)
			gbshutdownLogicError();
		//add it to the reply
		topDocIds[docCount] = t->m_docId;
		topScores[docCount] = t->m_score;
		if(m_toptree.m_useIntScores)
			topScores[docCount] = (double)t->m_intScore;
		// supply clusterdb rec? only for full splits
		if(m_gotClusterRecs)
			topRecs [docCount] = t->m_clusterRec;
		docCount++;

		if(m_debug) {
			logf(LOG_DEBUG,"query: msg39: [%" PTRFMT"] "
			    "%03" PRId32") docId=%012" PRIu64" sum=%.02f",
			    (PTRTYPE)this, docCount,
			    t->m_docId,t->m_score);
		}
		//don't send more than the docs that are wanted
		if(docCount>=numDocIds)
			break;
	}
	if(docCount>300 && m_debug)
		log("query: Had %" PRId32" nodes in top tree",docCount);

	// this is sensitive info
	if(m_debug) {
		log(LOG_DEBUG,
		    "query: msg39: [%" PTRFMT"] "
		    "Intersected lists took %" PRId64" (%" PRId64") "
		    "ms "
		    "docIdsToGet=%" PRId32" docIdsGot=%" PRId32" "
		    "q=%s",
		    (PTRTYPE)this                        ,
		    m_posdbTable.m_addListsTime       ,
		    gettimeofdayInMilliseconds() - m_startTime ,
		    m_msg39req->m_docsToGet                       ,
		    numDocIds                         ,
		    m_query.getQuery());
	}

	// now send back the reply
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(reply,replySize);
#endif
	sendReply(m_slot,this,reply,replySize,replySize,false);
	return;
}
