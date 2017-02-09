#include "gb-include.h"

#include "Msg3a.h"
#include "Wiki.h"
#include "sort.h"

#include "Stats.h"
#include "HashTableT.h"
#include "SearchInput.h"
#include "Process.h"
#include "Posdb.h"
#include "Collectiondb.h"
#include "ScalingFunctions.h"
#include "Conf.h"
#include "Lang.h"
#include "Mem.h"


static void gotReplyWrapper3a     ( void *state , void *state2 ) ;

Msg3a::Msg3a ( ) {
	constructor();
}

void Msg3a::constructor ( ) {
	// final buf hold the final merged docids, etc.
	m_finalBufSize = 0;
	m_finalBuf     = NULL;
	m_docsToGet    = 0;
	m_numDocIds    = 0;
	m_collnums     = NULL;
	m_q            = NULL;

	// need to call all safebuf constructors now to set m_label
	m_rbuf2.constructor();

	// NULLify all the reply buffer ptrs
	for ( int32_t j = 0; j < MAX_SHARDS; j++ )
		m_reply[j] = NULL;
	m_rbufPtr = NULL;
	for ( int32_t j = 0; j < MAX_SHARDS; j++ )
		m_mcast[j].constructor();

	// Coverity
	m_state = NULL;
	m_callback = NULL;
	m_numQueriedHosts = 0;
	m_moreDocIdsAvail = false;
	m_errno = 0;
	m_startTime = 0;
	m_numReplies = 0;
	m_skippedShards = 0;
	m_numTotalEstimatedHits = 0;
	m_pctSearched = 0.0;
	m_rbufSize = 0;
	memset(m_rbuf, 0, sizeof(m_rbuf));
	m_debug = false;
	m_docIds = NULL;
	m_scores = NULL;
	m_flags = NULL;
	m_scoreInfos = NULL;
	m_clusterRecs = NULL;
	m_clusterLevels = NULL;
	m_cursor = 0;
	memset(m_replyMaxSize, 0, sizeof(m_replyMaxSize));
}

Msg3a::~Msg3a ( ) {
	reset();
	for ( int32_t j = 0; j < MAX_SHARDS; j++ )
		m_mcast[j].destructor();
}

void Msg3a::reset ( ) {

	// . NULLify all the reply buffer ptrs
	// . have to count DOWN with "i" because of the m_reply[i-1][j] check
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) {
		if ( ! m_reply[j] ) continue;
		mfree(m_reply[j],m_replyMaxSize[j],  "Msg3aR");
		m_reply[j] = NULL;
	}
	for ( int32_t j = 0; j < MAX_SHARDS; j++ )
		m_mcast[j].reset();
	// and the buffer that holds the final docids, etc.
	if ( m_finalBuf )
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
	// free the request
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize, "Msg3a" );
		m_rbufPtr = NULL;
	}
	m_rbuf2.purge();
	m_finalBuf     = NULL;
	m_finalBufSize = 0;
	m_docsToGet    = 0;
	m_errno        = 0;
	m_numDocIds    = 0;
	m_collnums     = NULL;
	m_numTotalEstimatedHits = 0LL;
}



// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "query/coll" should NOT be on the stack in case we block
// . uses Msg36 to retrieve term frequencies for each termId in query
// . sends Msg39 request to get docids from each indexdb shard
// . merges replies together
// . we print out debug info if debug is true
// . "maxAge"/"addToCache" is talking about the clusterdb cache as well
//   as the indexdb cache for caching termlists read from disk on the machine
//   that contains them on disk.
// . "docsToGet" is how many search results are requested
// . "useDateLists" is true if &date1=X, &date2=X or &sdate=1 was specified
// . "sortByDate" is true if we should rank the results by newest pubdate 1st
// . "soreByDateWeight" is 1.0 to do a pure sort byte date, and 0.0 to just
//   sort by the regular score. however, the termlists are still read from
//   datedb, so we tend to prefer fresher results.
// . [date1,date2] define a range of dates to restrict the pub dates of the
//   search results to. they are -1 to indicate none.
// . "restrictIndexdbForQuery" limits termlists to the first indexdb file
// . "requireAllTerms" is true if all search results MUST contain the required
//   query terms, otherwise, such results are preferred, but the result set
//   will contain docs that do not have all required query terms.
// . "compoundListMaxSize" is the maximum size of the "compound" termlist
//   formed in Msg2.cpp by merging together all the termlists that are UOR'ed
//   together. this size is in bytes.
// . if "familyFilter" is true the results will not have their adult bits set
// . if language > 0, results will be from that language (language filter)
// . if rerankRuleset >= 0, we re-rank the docids by calling PageParser.cpp
//   on the first (X in &n=X) results and getting a new score for each.
// . if "artr" is true we also call PageParser.cpp on the root url of each
//   result, since the root url's quality is used to compute the quality
//   of the result in Msg16::computeQuality(). This will slow things down lots.
//   artr means "apply ruleset to roots".
// . if "recycleLinkInfo" is true then the rerank operation will not call
//   Msg25 to recompute the inlinker information used in
//   Msg16::computeQuality(), but rather deserialize it from the TitleRec.
//   Computing the link info takes a lot of time as well.
bool Msg3a::getDocIds(const SearchInput *si, Query *q, void *state,
	void (*callback)( void *state )) {

	// in case re-using it
	reset();
	// this should be &SearchInput::m_q
	m_q        = q;
	m_callback = callback;
	m_state    = state;

	if ( m_msg39req.m_collnum < 0 )
		log(LOG_LOGIC,"net: bad collection. msg3a. %" PRId32, (int32_t)m_msg39req.m_collnum);

	// for a sanity check in Msg39.cpp
	m_msg39req.m_nqt = m_q->getNumTerms();

	// we like to know if there was *any* problem even though we hide
	// title recs that are not found.
	m_errno     = 0;
	// reset this to zero in case we have error or something
	m_numDocIds = 0;
	// total # of estimated hits
	m_numTotalEstimatedHits = 0;
	// we modify this, so copy it from request
	m_docsToGet = m_msg39req.m_docsToGet;

	// . return now if query empty, no docids, or none wanted...
	// . if query terms = 0, might have been "x AND NOT x"
	if ( m_q->getNumTerms() <= 0 ) {
		return true;
	}

	// . set g_errno if not found and return true
	// . coll is null terminated
	CollectionRec *cr = g_collectiondb.getRec(m_msg39req.m_collnum);
	if ( ! cr ) { 
		g_errno = ENOCOLLREC; 
		return true; 
	}

	// query is truncated if had too many terms in it
	if ( m_q->m_truncated ) {
		log("query: query truncated: %s", m_q->m_orig);
		m_errno = EQUERYTRUNCATED;
	}

	// a handy thing
	m_debug = false;
	if ( m_msg39req.m_debug      ) m_debug = true;
	if ( g_conf.m_logDebugQuery  ) m_debug = true;
	if ( g_conf.m_logTimingQuery ) m_debug = true;

	// time how long it takes to get the term freqs
	if ( m_debug ) {
		// show the query terms
		printTerms ( );
		m_startTime = gettimeofdayInMilliseconds();
		logf(LOG_DEBUG,"query: msg3a: [%" PTRFMT"] getting termFreqs.",
		     (PTRTYPE)this);
	}

	setTermFreqWeights(m_msg39req.m_collnum, m_q);

	if ( m_debug ) {
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// get the term in utf8
			QueryTerm *qt = &m_q->m_qterms[i];

			// this term freq is estimated from the rdbmap and
			// does not hit disk...
			logf(LOG_DEBUG,"query: term #%" PRId32" \"%*.*s\" "
			     "termid=%" PRId64" termFreq=%" PRId64" termFreqWeight=%.03f",
			     i,
			     qt->m_termLen,
			     qt->m_termLen,
			     qt->m_term,
			     qt->m_termId,
			     qt->m_termFreq,
			     qt->m_termFreqWeight);
		}
	}

	// time how long to get each shard's docids
	m_startTime = gettimeofdayInMilliseconds();

	// reset replies received count
	m_numReplies  = 0;
	m_skippedShards = 0;
	// shortcut
	int32_t n = m_q->m_numTerms;

	/////////////////////////////
	//
	// set the Msg39 request
	//
	/////////////////////////////

	// free if we should
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize , "Msg3a");
		m_rbufPtr = NULL;
	}

	float   tfw      [ABS_MAX_QUERY_TERMS];
	for ( int32_t j = 0; j < n ; j++ ) {
		// get the jth query term
		QueryTerm *qt = &m_q->m_qterms[j];

		// serialize these too
		tfw[j] = qt->m_termFreqWeight;
	}

	// serialize this
	m_msg39req.ptr_termFreqWeights  = (char *)tfw;//m_termFreqWeights;
	m_msg39req.size_termFreqWeights = 4 * n;
	// store query into request, might have changed since we called
	// Query::expandQuery() above
	m_msg39req.ptr_query  = m_q->m_orig;
	m_msg39req.size_query = m_q->m_origLen+1;

	// free us?
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize, "Msg3a" );
		m_rbufPtr = NULL;
	}
	m_msg39req.m_stripe = 0;
	// . (re)serialize the request
	// . returns NULL and sets g_errno on error
	// . "m_rbuf" is a local storage space that can save a malloc
	// . do not "makePtrsRefNewBuf" because if we do that and this gets
	//   called a 2nd time because m_getWeights got set to 0, then we
	//   end up copying over ourselves.
	m_rbufPtr = serializeMsg ( sizeof(Msg39Request),
				   &m_msg39req.size_termFreqWeights,
				   &m_msg39req.size_whiteList,
				   &m_msg39req.ptr_termFreqWeights,
				   &m_msg39req,
				   &m_rbufSize ,
				   m_rbuf ,
				   RBUF_SIZE);

	if ( ! m_rbufPtr ) return true;

	// how many seconds since our main process was started?
	long long now = gettimeofdayInMilliseconds();
	long elapsed = (now - g_stats.m_startTime) / 1000;

	// free this one too
	m_rbuf2.purge();
	// and copy that!
	if ( ! m_rbuf2.safeMemcpy ( m_rbufPtr , m_rbufSize ) ) {
		return true;
	}

	// and tweak it
	((Msg39Request *)(m_rbuf2.getBufStart()))->m_stripe = 1;

	/////////////////////////////
	//
	// end formulating the Msg39 request
	//
	/////////////////////////////

	//todo: scale the get-doc-ids timeout according to number of query terms
	int64_t timeout = multicast_msg3a_default_timeout;
	// override? this is USUALLY -1, but DupDectector.cpp needs it
	// high because it is a spider time thing.
	if ( m_msg39req.m_timeout > 0 ) {
		timeout = m_msg39req.m_timeout;
		timeout += g_conf.m_msg3a_msg39_network_overhead;
	}
	if ( timeout > multicast_msg3a_maximum_timeout ) {
		timeout = multicast_msg3a_maximum_timeout;
	}
	
	int64_t qh = m_q->getQueryHash();

	int32_t totalNumHosts = g_hostdb.getNumHosts();
	
	m_numQueriedHosts = 0;
	
	// only send to one host?
	if ( ! m_q->isSplit() ) {
		totalNumHosts = 1;
	}


	// now we run it over ALL hosts that are up!
	for ( int32_t i = 0; i < totalNumHosts ; i++ ) { // m_indexdbSplit; i++ ) {
		// get that host
		Host *h = g_hostdb.getHost(i);

		if(!h->m_queryEnabled) {
			continue;
		}
		
		m_numQueriedHosts++;

		// if not a full split, just round robin the group, i am not
		// going to sweat over performance on non-fully split indexes
		// because they suck really bad anyway compared to full
		// split indexes. "gid" is already set if we are not split.
		int32_t shardNum = h->m_shardNum;
		int32_t firstHostId = h->m_hostId;
		// get strip num
		char *req = m_rbufPtr;
		// if sending to twin, use slightly different request
		if ( h->m_stripe == 1 ) req = m_rbuf2.getBufStart();
		// if we are a non-split query, like gbdom:xyz.com just send
		// to the host that has the first termid local. it will call
		// msg2 to download all termlists. msg2 should be smart
		// enough to download the "non split" termlists over the net.
		// TODO: fix msg2 to do that...
		if ( ! m_q->isSplit() ) {
			int64_t     tid  = m_q->getTermId(0);
			
			key144_t k;
			Posdb::makeKey ( &k ,
					  tid,
					  0LL, // docid
					  0, // dist
					  MAXDENSITYRANK, // density rank
					  MAXDIVERSITYRANK, // diversity rank
					  MAXWORDSPAMRANK, // wordspamrank
					  0, // siterank
					  0, // hashgroup
					  // we set to docLang in final hash loop
					  langUnknown,// langid
					  0, // multiplier
					  0, // syn?
					  false , // delkey?
					  false ); // sharded by termid

			// split = false! do not split
			//gid = getGroupId ( RDB_POSDB,&k,false);
			shardNum = g_hostdb.getShardNumByTermId(&k);
			firstHostId = -1;
		}

		// debug log
		if ( m_debug ) {
			logf(LOG_DEBUG,"query: Msg3a[%" PTRFMT"]: forwarding request "
			     "of query=%s to shard %" PRIu32".",
			     (PTRTYPE)this, m_q->getQuery(), shardNum);
		}

		// send to this guy
		Multicast *m = &m_mcast[i];
		// clear it for transmit
		m->reset();

		// if all hosts in group dead, just skip it!
		if ( g_hostdb.isShardDead ( shardNum ) ) {
			m_numReplies++;
			log("msg3a: skipping dead shard # %i "
			    "(elapsed=%li)",(int)shardNum,elapsed);
			continue;
		}

		if ( si && !si->m_askOtherShards && h!=g_hostdb.getMyHost()) {
			m_numReplies++;
			continue;
		}

		// . send out a msg39 request to each shard
		// . multicasts to a host in group "groupId"
		// . we always block waiting for the reply with a multicast
		// . returns false and sets g_errno on error
		// . sends the request to fastest host in group "groupId"
		// . if that host takes more than about 5 secs then sends to
		//   next host
		// . key should be largest termId in group we're sending to
		bool status = m->send(req, m_rbufSize, msg_type_39, false, shardNum, false, (int32_t)qh, this, m, gotReplyWrapper3a, timeout, m_msg39req.m_niceness, firstHostId, true);
		// if successfully launch, do the next one
		if ( status ) {
			continue;
		}
		// . this serious error should make the whole query fail
		// . must allow other replies to come in though, so keep going
		m_numReplies++;
		log("query: Multicast Msg3a had error: %s", mstrerror(g_errno));
		m_errno = g_errno;
		g_errno = 0;
	}

	// return false if blocked on a reply
	if ( m_numReplies < m_numQueriedHosts ) {
		return false;//indexdbSplit )
	}

	// . otherwise, we did not block... error?
	// . it must have been an error or just no new lists available!!
	// . if we call gotAllShardReplies() here, and we were called by
	//   mergeLists() we end up calling mergeLists() again... bad. so
	//   just return true in that case.
	//return gotAllShardReplies();
	return true;
}



void gotReplyWrapper3a ( void *state , void *state2 ) {
	Msg3a *THIS = (Msg3a *)state;


	// set it
	Multicast *m = (Multicast *)state2;

	// update host table
	Host *h = m->m_replyingHost;

	// update time
	int64_t endTime = gettimeofdayInMilliseconds();

	
	// timestamp log
	if ( THIS->m_debug )
	{
		logf(LOG_DEBUG,"query: msg3a: [%" PTRFMT"] got reply #%" PRId32" (of %" PRId32") in %" PRId64" ms. Hostid=%" PRId32". err=%s", 
			(PTRTYPE)THIS, 
			THIS->m_numReplies+1,
			THIS->m_numQueriedHosts,
		     endTime - THIS->m_startTime,
		     h ? h->m_hostId : -1,
		     mstrerror(g_errno) );
	}
	else 
	if ( g_errno )
	{
		logf(LOG_DEBUG,"msg3a: error reply. [%" PTRFMT"] got reply #%" PRId32". Hostid=%" PRId32". err=%s", 
			(PTRTYPE)THIS, THIS->m_numReplies ,
		     h ? h->m_hostId : -1,
		     mstrerror(g_errno) );
	}


	// if one shard times out, ignore it!
	if ( g_errno == EQUERYTRUNCATED || g_errno == EUDPTIMEDOUT )
	{
		g_errno = 0;
	}

	// record it
	if ( g_errno && ! THIS->m_errno )
	{
		THIS->m_errno = g_errno;
	}

	
	// i guess h is NULL on error?
	if ( h ) {
		// how long did it take from the launch of request until now
		// for host "h" to give us the docids?
		int64_t delta = (endTime - m->m_replyLaunchTime);
		// . sanity check
		// . ntpd can screw with our local time and make this negative
		if ( delta >= 0 ) {
			// count the shards
			h->m_splitsDone++;
			// accumulate the times so we can do an average display
			// in PageHosts.cpp.
			h->m_splitTimes += delta;
		}
	}
	// update count of how many replies we got
	THIS->m_numReplies++;
	// bail if still awaiting more replies
	if ( THIS->m_numReplies < THIS->m_numQueriedHosts ) return;
	// return if gotAllShardReplies() blocked
	if ( ! THIS->gotAllShardReplies( ) ) return;
	// set g_errno i guess so parent knows
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// call callback if we did not block, since we're here. all done.
	THIS->m_callback ( THIS->m_state );
}

bool Msg3a::gotAllShardReplies ( ) {

	// if any of the shard requests had an error, give up and set m_errno
	// but don't set if for non critical errors like query truncation
	if ( m_errno ) {
		g_errno = m_errno;
		return true;
	}

	// also reset the finalbuf and the oldNumTopDocIds
	if ( m_finalBuf ) {
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
		m_finalBuf     = NULL;
		m_finalBufSize = 0;
	}

	// update our estimated total hits
	m_numTotalEstimatedHits = 0;
	double pctSearchedSum = 0.0;

	for ( int32_t i = 0; i < m_numQueriedHosts ; i++ ) {
		// get that host that gave us the reply
		//Host *h = g_hostdb.getHost(i);
		// . get the reply from multicast
		// . multicast should have destroyed all slots, but saved reply
		// . we are responsible for freeing the reply
		// . we need to call this even if g_errno or m_errno is
		//   set so we can free the replies in Msg3a::reset()
		// . if we don't call getBestReply() on it multicast should
		//   free it, because Multicast::m_ownReadBuf is still true
		Multicast *m = &m_mcast[i];
		bool freeit = false;
		int32_t  replySize = 0;
		int32_t  replyMaxSize;
		// . only get it if the reply not already full
		// . if reply already processed, skip
		// . perhaps it had no more docids to give us or all termlists
		//   were exhausted on its disk and this is a re-call
		// . we have to re-process it for count m_numTotalEstHits, etc.
		char *rbuf = m->getBestReply(&replySize,
					     &replyMaxSize,
					     &freeit,
					     true); //stealIt?
		// . we must be able to free it... we must own it
		// . this is true if we should free it, but we should not have
		//   to free it since it is owned by the slot?
		if ( freeit ) {
			log(LOG_LOGIC,"query: msg3a: Steal failed.");
			g_process.shutdownAbort(true);
		}
		if(rbuf) {
			// in case of mem leak, re-label from "mcast" to this so we
			// can determine where it came from, "Msg3a-GBR"
			relabel( rbuf, replyMaxSize , "Msg3a-GBR" );
		}
		// bad reply?
		if ( ! rbuf || replySize < 29 ) {
			m_skippedShards++;
			log(LOG_LOGIC,"query: msg3a: Bad reply (size=%i) from "
			    "host #%" PRId32". Dead? Timeout? OOM?"
			    ,(int)replySize
			    ,i);
			m_reply       [i] = NULL;
			m_replyMaxSize[i] = 0;

			// it might have been timd out, just ignore it!!
			continue;
		}
		// cast it
		Msg39Reply *mr = (Msg39Reply *)rbuf;
		// how did this happen?
		// if ( replySize < 29 && ! mr->m_errno ) {
		// 	// if size is 0 it can be Msg39 giving us an error!
		// 	g_errno = EBADREPLYSIZE;
		// 	m_errno = EBADREPLYSIZE;
		// 	log(LOG_LOGIC,"query: msg3a: Bad reply size "
		// 	    "of %" PRId32".",
		// 	    replySize);
		// 	// all reply buffers should be freed on reset()
		// 	return true;
		// }

		// can this be non-null? we shouldn't be overwriting one
		// without freeing it...
		if ( m_reply[i] )
			// note the mem leak now
			log("query: mem leaking a 0x39 reply");

		// cast it and set it
		m_reply       [i] = mr;
		m_replyMaxSize[i] = replyMaxSize;
		// sanity check
		if ( mr->m_nqt != m_q->getNumTerms() ) {
			g_errno = EBADREPLY;
			m_errno = EBADREPLY;
			log("query: msg3a: Shard reply qterms=%" PRId32" != %" PRId32".",
			    (int32_t)mr->m_nqt,(int32_t)m_q->getNumTerms() );
			return true;
		}
		// return if shard had an error, but not for a non-critical
		// error like query truncation
		if ( mr->m_errno && mr->m_errno != EQUERYTRUNCATED ) {
			g_errno = mr->m_errno;
			m_errno = mr->m_errno;
			log("query: msg3a: Shard had error: %s",
			    mstrerror(g_errno));
			return true;
		}
		// deserialize it (just sets the ptr_ and size_ member vars)
		//mr->deserialize ( );
		if ( ! deserializeMsg ( sizeof(Msg39Reply) ,
					&mr->size_docIds,
					&mr->size_clusterRecs,
					&mr->ptr_docIds,
					((char*)mr) + sizeof(*mr) ) ) {
			g_errno = ECORRUPTDATA;
			m_errno = ECORRUPTDATA;
			log("query: msg3a: Shard had error: %s",
			    mstrerror(g_errno));
			return true;

		}

		// add of the total hits from each shard, this is how many
		// total results the lastest shard is estimated to be able to
		// return
		// . THIS should now be exact since we read all termlists
		//   of posdb...
		m_numTotalEstimatedHits += mr->m_estimatedHits;
		pctSearchedSum += mr->m_pctSearched;

		// debug log stuff
		if ( ! m_debug ) continue;
		// cast these for printing out
		int64_t *docIds    = (int64_t *)mr->ptr_docIds;
		double    *scores    = (double    *)mr->ptr_scores;
		const unsigned *flags = (const unsigned*)mr->ptr_flags;
		// print out every docid in this shard reply
		for ( int32_t j = 0; j < mr->m_numDocIds ; j++ ) {
			// print out score_t
			logf( LOG_DEBUG,
			     "query: msg3a: [%p] %03d shard=%d docId=%012" PRIu64" domHash=0x%02x score=%f flags=0x%04x",
			     this,
			     j, i,
			     docIds[j],
			     (int32_t)Titledb::getDomHash8FromDocId(docIds[j]),
			     scores[j],
			     flags[j]);
		}
	}

	m_pctSearched = pctSearchedSum/m_numQueriedHosts;

	// this seems to always return true!
	mergeLists ( );

	return true;
}

// . merge all the replies together
// . put final merged docids into m_docIds[],m_bitScores[],m_scores[],...
// . this calls Msg51 to get cluster levels when done merging
// . Msg51 remembers clusterRecs from previous call to avoid repeating lookups
// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg3a::mergeLists() {

	// time how long the merge takes
	if(m_debug) {
		logf( LOG_DEBUG, "query: msg3a: --- Final DocIds --- " );
		m_startTime = gettimeofdayInMilliseconds();
	}

	// reset our final docids count here in case we are a re-call
	m_numDocIds = 0;
	// a secondary count, how many unique docids we scanned, and not
	// necessarily added to the m_docIds[] array
	//m_totalDocCount = 0; // int32_t docCount = 0;
	m_moreDocIdsAvail = true;


	if(m_numQueriedHosts > MAX_SHARDS) { g_process.shutdownAbort(true); }
	if(m_docsToGet <= 0) { g_process.shutdownAbort(true); }

	// . point to the various docids, etc. in each shard reply
	// . tcPtr = term count. how many required query terms does the doc
	//   have? formerly called topExplicits in IndexTable2.cpp
	int64_t     *diPtr [MAX_SHARDS];
	double        *rsPtr [MAX_SHARDS];
	unsigned    *flagsPtr[MAX_SHARDS];
	key96_t         *ksPtr [MAX_SHARDS];
	int64_t     *diEnd [MAX_SHARDS];
	for(int32_t j = 0; j < m_numQueriedHosts ; j++) {
		if(Msg39Reply *mr =m_reply[j]) {
			diPtr[j] = (int64_t*)mr->ptr_docIds;
			rsPtr[j] = (double*) mr->ptr_scores;
			flagsPtr[j] = (unsigned*)mr->ptr_flags;
			ksPtr[j] = (key96_t*)mr->ptr_clusterRecs;
			diEnd[j] = (int64_t*)(mr->ptr_docIds + mr->m_numDocIds * 8);
		} else {
			// if we have gbdocid:| in query this could be NULL
			diPtr[j] = NULL;
			diEnd[j] = NULL;
			rsPtr[j] = NULL;
			flagsPtr[j] = NULL;
			ksPtr[j] = NULL;
		}
	}

	// clear if we had it
	if(m_finalBuf) {
		mfree(m_finalBuf, m_finalBufSize, "Msg3aF" );
		m_finalBuf     = NULL;
		m_finalBufSize = 0;
	}


	// . how much do we need to store final merged docids, etc.?
	// . docid=8 score=4 bitScore=1 clusterRecs=key96_t clusterLevls=1
	int32_t nd1 = m_docsToGet;
	int32_t nd2 = 0;
	for(int32_t j = 0; j < m_numQueriedHosts; j++) {
		if(Msg39Reply *mr = m_reply[j])
			nd2 += mr->m_numDocIds;
	}
	// pick the min docid count from the above two methods
	int32_t nd = nd1;
	if(nd2 < nd1)
		nd = nd2;

	int32_t need =  nd * (8+sizeof(double)+sizeof(unsigned)+
			   sizeof(key96_t)+sizeof(DocIdScore *)+1);
	if(need < 0) {
		log("msg3a: need is %i, nd = %i is too many docids",
		    (int)need,(int)nd);
		g_errno = EBUFTOOSMALL;
		return true;
	}

	// allocate it
	m_finalBuf     = (char *)mmalloc(need , "finalBuf" );
	m_finalBufSize = need;
	// g_errno should be set if this fails
	if(!m_finalBuf)
		return true;
	// hook into it
	char *p = m_finalBuf;
	m_docIds        = (int64_t*)    p; p += nd * 8;
	m_scores        = (double*)     p; p += nd * sizeof(double);
	m_flags         = (unsigned*)   p; p += nd * sizeof(unsigned);
	m_clusterRecs   = (key96_t*)    p; p += nd * sizeof(key96_t);
	m_clusterLevels = (char*)       p; p += nd * 1;
	m_scoreInfos    = (DocIdScore**)p; p+=nd*sizeof(DocIdScore *);

	// sanity check
	char *pend = m_finalBuf + need;
	if(p != pend) { g_process.shutdownAbort(true); }
	// hash table for doing site clustering, provided we
	// are fully split and we got the site recs now
	HashTableT<int64_t,int32_t> htable2;
	if(m_msg39req.m_doSiteClustering && !htable2.set (nd*2))
		return true;

	//
	// ***MERGE ALL SHARDS INTO m_docIds[], etc.***
	//
	// . merge all lists in m_replyDocIds[splitNum]
	// . we may be re-called later after m_docsToGet is increased
	//   if too many docids were clustered/filtered out after the call
	//   to Msg51.
	do {
		// the winning docid will be diPtr[maxj]
		int32_t maxj = -1;

		// get the next highest-scoring docids from all shard termlists
		for(int32_t j = 0; j < m_numQueriedHosts; j++) {
			// . skip exhausted lists
			// . these both should be NULL if reply was skipped because
			//   we did a gbdocid:| query
			if(diPtr[j] >= diEnd[j]) {
				continue;
			}
			// compare the score
			if(maxj == -1) { 
				maxj = j; 
				continue; 
			}
			if(*rsPtr[j] < *rsPtr[maxj]) {
				continue;
			}
			if(*rsPtr[j] > *rsPtr[maxj]) { 
				maxj = j; 
				continue; 
			}
			// prefer lower docids on top
			if(*diPtr[j] < *diPtr[maxj]) { 
				maxj = j; 
				continue;
			}
		}

		if(maxj == -1) {
			m_moreDocIdsAvail = false;
			goto doneMerge;
		}

		// only do this logic if we have clusterdb recs included
		if(m_msg39req.m_doSiteClustering &&
		     // if the clusterLevel was set to CR_*errorCode* then this key
		     // will be 0, so in that case, it might have been a not found
		     // or whatever, so let it through regardless
		     ksPtr[maxj]->n0 != 0LL &&
		     ksPtr[maxj]->n1 != 0  ) {
			// if family filter on and is adult...
			if(m_msg39req.m_familyFilter &&
			     Clusterdb::hasAdultContent((char*)ksPtr[maxj]) )
				goto skip;
			// get the hostname hash, a int64_t
			int32_t sh = Clusterdb::getSiteHash26 ((char*)ksPtr[maxj]);
			// do we have enough from this hostname already?
			int32_t slot = htable2.getSlot(sh );
			// if this hostname already visible, do not over-display it...
			if(slot >= 0) {
				// get the count
				int32_t val = htable2.getValueFromSlot(slot );
				// . if already 2 or more, give up
				// . if the site hash is 0, that usually means a
				//   "not found" in clusterdb, and the accompanying
				//   cluster level would be set as such, but since we
				//   did not copy the cluster levels over in the merge
				//   algo above, we don't know for sure... cluster recs
				//   are set to 0 in the Msg39.cpp clustering.
				if(sh && val >= 2)
					goto skip;
				// if only allowing one...
				if(sh && val >= 1 && m_msg39req.m_hideAllClustered)
					goto skip;
				// inc the count
				val++;
				// store it
				htable2.setValue(slot , val );
			}
			// . add it, this should be pre-allocated!
			// . returns false and sets g_errno on error
			else if(! htable2.addKey(sh,1))
				return true;
		}

		// always inc this
		//m_totalDocCount++;
		// only do this if we need more
		if(m_numDocIds < m_docsToGet) {
			// get DocIdScore class for this docid
			Msg39Reply *mr = m_reply[maxj];
			// point to the array of DocIdScores
			DocIdScore *ds = (DocIdScore *)mr->ptr_scoreInfo;
			int32_t nds = mr->size_scoreInfo/sizeof(DocIdScore);
			DocIdScore *dp = NULL;
			for(int32_t i = 0; i < nds; i++) {
				if(ds[i].m_docId == *diPtr[maxj]) {
					dp = &ds[i];
					break;
				}
			}
			// add the max to the final merged lists
			m_docIds[m_numDocIds] = *diPtr[maxj];

			// wtf?
			if(!dp) {
				// this is empty if no scoring info
				// supplied!
				if(m_msg39req.m_getDocIdScoringInfo)
					log("msg3a: CRAP! got empty score info for d=%" PRId64,
					    m_docIds[m_numDocIds]);
			}
			// point to the single DocIdScore for this docid
			m_scoreInfos[m_numDocIds] = dp;

			// reset this just in case
			if(dp) {
				dp->m_singleScores = NULL;
				dp->m_pairScores   = NULL;
			}

			// now fix DocIdScore::m_pairScores and m_singleScores
			// ptrs so they reference into the
			// Msg39Reply::ptr_pairScoreBuf and ptr_singleSingleBuf
			// like they should. it seems we do not free the
			// Msg39Replies so we should be ok referencing them.
			if(dp && dp->m_singlesOffset >= 0)
				dp->m_singleScores =
					(SingleScore*)(mr->ptr_singleScoreBuf+dp->m_singlesOffset);
			if(dp && dp->m_pairsOffset >= 0)
				dp->m_pairScores =
					(PairScore*)  (mr->ptr_pairScoreBuf +dp->m_pairsOffset);


			// turn it into a float, that is what rscore_t is.
			// we do this to make it easier for PostQueryRerank.cpp
			m_scores[m_numDocIds]=(double)*rsPtr[maxj];
			m_flags[m_numDocIds] = *flagsPtr[maxj];
			if(m_msg39req.m_doSiteClustering)
				m_clusterRecs[m_numDocIds]= *ksPtr[maxj];

			// point to next available slot to add to
			m_numDocIds++;
		}


		// if it has ALL the required query terms, count it
		//if(*bsPtr[maxj] & 0x60 ) m_numAbove++;

	 skip:
		// increment the shard pointers from which we took the max
		rsPtr[maxj]++;
		diPtr[maxj]++;
		flagsPtr[maxj]++;
		ksPtr[maxj]++;
		// get the next highest docid and add it in
	} while(m_numDocIds < m_docsToGet);

 doneMerge:

	if(m_debug) {
		// show how long it took
		logf( LOG_DEBUG,"query: msg3a: [%" PTRFMT"] merged %" PRId32" docs from %" PRId32" shards in %" PRIu64" ms. ",
		      (PTRTYPE)this,
		       m_numDocIds, (int32_t)m_numQueriedHosts,
		       gettimeofdayInMilliseconds() - m_startTime
		      );
		// show the final merged docids
		for(int32_t i = 0; i < m_numDocIds; i++) {
			int32_t sh = 0;
			if(m_msg39req.m_doSiteClustering )
				sh=Clusterdb::getSiteHash26((char *)
							   &m_clusterRecs[i]);
			// print out score_t
			logf(LOG_DEBUG,"query: msg3a: [%p] %03d) merged docId=%012" PRIu64" score=%f flags=0x%04x hosthash=0x%x",
			     this,
			     i,
			     m_docIds[i],
			     (double)m_scores[i],
			     m_flags[i],
			     sh);
		}
	}

	// if we had a full split, we should have gotten the cluster recs
	// from each shard already
	memset(m_clusterLevels , CR_OK , m_numDocIds );

	return true;
}

void Msg3a::printTerms ( ) {
	// loop over all query terms
	int32_t n = m_q->getNumTerms();
	// do the loop
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the term in utf8
		//char bb[256];
		// "s" points to the term, "tid" the termId
		//char      *s;
		//int32_t       slen;
		//int64_t  tid;
		//char buf[2048];
		//buf[0]='\0';
		int64_t tid  = m_q->m_qterms[i].m_termId;
		char *s    = m_q->m_qterms[i].m_term;
		if ( ! s ) {
			logf(LOG_DEBUG,"query: term #%" PRId32" "
			     "\"<notstored>\" (%" PRIu64")",
			     i,tid);
		}
		else {
			int32_t slen = m_q->m_qterms[i].m_termLen;
			char c = s[slen];
			s[slen] = '\0';
			//utf16ToUtf8(bb, 256, s , slen );
			//sprintf(buf," termId#%" PRId32"=%" PRId64,i,tid);
			// this term freq is estimated from the rdbmap and
			// does not hit disk...
			logf(LOG_DEBUG,"query: term #%" PRId32" \"%s\" (%" PRIu64")",
			     i,s,tid);
			s[slen] = c;
		}
	}
}


static float getTermFreqWeight(int64_t termFreq, int64_t numDocsInColl) {
	if(numDocsInColl>0)
		return scale_linear(((float)termFreq)/numDocsInColl, g_conf.m_termFreqWeightFreqMin, g_conf.m_termFreqWeightFreqMax, g_conf.m_termFreqWeightMin, g_conf.m_termFreqWeightMax);
	else
		return 1.0; //whatever...
}


void setTermFreqWeights ( collnum_t collnum , Query *q ) {
	int64_t numDocsInColl = 0;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB, collnum );
	if ( base ) numDocsInColl = base->estimateNumGlobalRecs();

	// issue? set it to 1000 if so
	if ( numDocsInColl < 0 ) {
		log("query: Got num docs in coll of %" PRId64" < 0",numDocsInColl);
		// avoid divide by zero below
		numDocsInColl = 1;
	}

	// now get term freqs again, like the good old days
	// just use rdbmap to estimate!
	for ( int32_t i = 0 ; i < q->getNumTerms(); i++ ) {
		QueryTerm *qt = &q->m_qterms[i];
		// GET THE TERMFREQ for setting weights
		int64_t tf = g_posdb.getTermFreq ( collnum ,qt->m_termId);
		qt->m_termFreq = tf;
		float tfw = getTermFreqWeight(tf,numDocsInColl);
		qt->m_termFreqWeight = tfw;
	}
}
