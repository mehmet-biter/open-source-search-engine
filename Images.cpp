#include "gb-include.h"
#include "Images.h"
#include "Conf.h"
#include "Query.h"
#include "Xml.h"
#include "Words.h"
#include "Sections.h"
#include "XmlDoc.h"
#include "Collectiondb.h"
#include "JobScheduler.h"
#include "Hostdb.h"
#include "Process.h"
#include "Posdb.h"
#include "File.h"
#include <pthread.h>
#include <fcntl.h>
#include <arpa/inet.h>


// TODO: image is bad if repeated on same page, check for that

//static void gotTermFreqWrapper ( void *state ) ;
static void gotTermListWrapper ( void *state ) ;
static void thumbStartWrapper_r ( void *state );
static void getImageInfo(const char *buf, int32_t size, int32_t *dx, int32_t *dy, int32_t *it);

Images::Images ( ) {
	reset();
}

void Images::reset() {
	m_imgData        = NULL;
	m_imgDataSize    = 0;
	m_setCalled      = false;
	m_thumbnailValid = false;
	m_imgReply       = NULL;
	m_imgReplyLen    = 0;
	m_imgReplyMaxLen = 0;
	m_numImages      = 0;
	m_imageBufValid  = false;
	m_phase = 0;

	// Coverity
	m_i = 0;
	m_j = 0;
	m_xd = NULL;
	m_state = NULL;
	m_callback = NULL;
	m_xysize = 0;
	m_errno = 0;
	m_hadError = 0;
	m_stopDownloading = false;
	memset(m_statusBuf, 0, sizeof(m_statusBuf));
	m_collnum = 0;
	m_docId = 0;
	m_latestIp = 0;
	memset(m_imageNodes, 0, sizeof(m_imageNodes));
	memset(m_termIds, 0, sizeof(m_termIds));
	memset(m_errors, 0, sizeof(m_errors));
	m_pageUrl = NULL;
	m_xml = NULL;
	memset(&m_msg13Request, 0, sizeof(m_msg13Request));
	m_httpStatus = 0;
	m_imgType = 0;
	m_dx = 0;
	m_dy = 0;
	m_thumbnailSize = 0;
	m_tdx = 0;
	m_tdy = 0;
}

void Images::setCandidates ( Url *pageUrl , Words *words , Xml *xml , Sections *sections ) {
	// not valid for now
	m_thumbnailValid = false;
	// reset our array of image node candidates
	m_numImages = 0;
	// flag it
	m_setCalled = true;
	// strange...
	if ( m_imgReply ) { g_process.shutdownAbort(true); }
	// save this
	m_xml       = xml;
	m_pageUrl   = pageUrl;

	//
	// first add any open graph candidate.
	// basically they page telling us the best image straight up.
	//

	int32_t node2 = -1;
	int32_t startNode = 0;

	// . field can be stuff like "summary","description","keywords",...
	// . if "convertHtmlEntites" is true we change < to &lt; and > to &gt;
	// . <meta property="og:image" content="http://example.com/rock2.jpg"/>
	// . <meta property="og:image" content="http://example.com/rock3.jpg"/>
 ogimgloop:
	char ubuf[2000];
	int32_t ulen = xml->getMetaContent( ubuf, 1999, "og:image", 8, "property", startNode, &node2 );

	// update this in case goto ogimgloop is called
	startNode = node2 + 1;
	// see section below for explanation of what we are storing here...
	if ( node2 >= 0 ) {
		// save it
		m_imageNodes[m_numImages] = node2;
		Query q;
		if ( ulen > MAX_URL_LEN ) goto ogimgloop;
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		iu.set( pageUrl, ubuf, ulen );
		// skip if invalid domain or TLD
		if ( iu.getDomainLen() <= 0 ) goto ogimgloop;
		// for looking it up on disk to see if unique or not
		char buf[2000];
		// if we don't put in quotes it expands '|' into
		// the "PiiPe" operator in Query.cpp
		snprintf ( buf , 1999, "gbimage:\"%s\"",iu.getUrl());
		// TODO: make sure this is a no-split termid storage thingy
		// in Msg14.cpp
		if ( ! q.set2 ( buf, langUnknown, NULL, false, true, ABS_MAX_QUERY_TERMS ) ) return;
		// sanity test
		if(q.getNumTerms()!=1) { g_process.shutdownAbort(true); }
		// store the termid
		m_termIds[m_numImages] = q.getTermId(0);
		// advance the counter
		m_numImages++;
		// try to get more graph images if we have some room
		if ( m_numImages + 2 < MAX_IMAGES ) goto ogimgloop;
	}
	


	//m_pageSite  = pageSite;
	// scan the words
	int32_t       nw     = words->getNumWords();
	nodeid_t  *tids   = words->getTagIds();
	int64_t *wids   = words->getWordIds();
	//int32_t      *scores = scoresArg->m_scores;
	Section **sp = NULL; 
	if ( sections ) sp = sections->m_sectionPtrs;
	// not if we don't have any identified sections
	if ( sections && sections->m_numSections <= 0 ) sp = NULL;
	// the positive scored window
	int32_t firstPosScore = -1;
	int32_t lastPosScore  = -1;
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT;
	// find positive scoring window
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if in bad section
		if ( sp && (sp[i]->m_flags & badFlags) ) continue;
		if ( wids[i]   != 0 ) continue;
		// set first positive scoring guy
		if ( firstPosScore == -1 ) firstPosScore = i;
		// keep track of last guy
		lastPosScore = i;
	}
	// sanity check
	if ( getNumXmlNodes() > 512 ) { g_process.shutdownAbort(true); }
	// . pedal firstPosScore back until we hit a section boundary
	// . i.e. stop once we hit a front/back tag pair, like <div> and </div>
	char tc[512];
	memset ( tc , 0 , 512 );
	int32_t a = firstPosScore;
	for ( ; a >= 0 ; a-- ) {
		// get the tid
		nodeid_t tid = tids[a];
		// remove back bit, if any
		tid &= BACKBITCOMP;
		// skip if not a tag, or a generic xml tag
		if ( tid <= 1 ) continue;
		// mark it
		if ( words->isBackTag(a) ) tc[tid] |= 0x02;
		else                       tc[tid] |= 0x01;
		// continue if not a full front/back pair
		if ( tc[tid] != 0x03 ) continue;
		// continue if not a "section" type tag (see Scores.cpp)
		if ( tid != TAG_DIV      &&
		     tid != TAG_TEXTAREA &&
                     tid != TAG_TR       &&
                     tid != TAG_TD       &&
                     tid != TAG_TABLE      ) 
			continue;
		// ok we should stop now
		break;
	}		
	// min is 0
	if ( a < 0 ) a = 0;

	// now look for the image urls within this window
	for ( int32_t i = a ; i < lastPosScore ; i++ ) {
		// skip if not <img> tag
		if (tids[i] != TAG_IMG ) continue;
		// get the node num into Xml.cpp::m_nodes[] array
		int32_t nn = words->getNodes()[i];
		// check width to rule out small decorating imgs
		int32_t width = xml->getLong(nn,nn+1,"width", -1 );
		if ( width != -1 && width < 50 ) continue;
		// same with height
		int32_t height = xml->getLong(nn,nn+1, "height", -1 );
		if ( height != -1 && height < 50 ) continue;
		// get the url of the image
		int32_t  srcLen;
		const char *src = xml->getString(nn,"src",&srcLen);
		// skip if none
		if ( srcLen <= 2 ) continue;
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		iu.set( pageUrl, src, srcLen );
		// skip if invalid domain or TLD
		if ( iu.getDomainLen() <= 0 ) continue;
		// skip if not from same domain as page url
		//int32_t dlen = pageUrl->getDomainLen();
		//if ( iu.getDomainLen() != dlen ) continue;
		//if(strncmp(iu.getDomain(),pageUrl->getDomain(),dlen))continue
		// get the full url
		const char *u    = iu.getUrl();
		int32_t  ulen = iu.getUrlLen();
		// skip common crap
		if ( strncasestr(u,ulen,"logo"           ) ) continue;
		if ( strncasestr(u,ulen,"comment"        ) ) continue;
		if ( strncasestr(u,ulen,"print"          ) ) continue;
		if ( strncasestr(u,ulen,"subscribe"      ) ) continue;
		if ( strncasestr(u,ulen,"header"         ) ) continue;
		if ( strncasestr(u,ulen,"footer"         ) ) continue;
		if ( strncasestr(u,ulen,"menu"           ) ) continue;
		if ( strncasestr(u,ulen,"button"         ) ) continue;
		if ( strncasestr(u,ulen,"banner"         ) ) continue;
		if ( strncasestr(u,ulen,"ad.doubleclick.") ) continue;
		if ( strncasestr(u,ulen,"ads.webfeat."   ) ) continue;
		if ( strncasestr(u,ulen,"xads.zedo."     ) ) continue;

		// save it
		m_imageNodes[m_numImages] = nn;

		// before we lookup the image url to see if it is unique we
		// must first make sure that we have an adequate number of
		// permalinks from this same site with this same hop count.
		// we need at least 10 before we extract image thumbnails.
		char buf[2000];
		// set the query
		Query q;

		// if we do have 10 or more, then we lookup the image url to
		// make sure it is indeed unique
		sprintf ( buf , "gbimage:\"%s\"",u);
		// TODO: make sure this is a no-split termid storage thingy
		// in Msg14.cpp
		if ( ! q.set2 ( buf, langUnknown, NULL, false, true, ABS_MAX_QUERY_TERMS ) )
			// return true with g_errno set on error
			return;
		if(q.getNumTerms()!= 1) { g_process.shutdownAbort(true); }
		// store the termid
		m_termIds[m_numImages] = q.getTermId(0);

		// advance the counter
		m_numImages++;

		// break if full
		if ( m_numImages >= MAX_IMAGES ) break;
	}
}

// . returns false if blocked, returns true otherwise
// . sets g_errno on error
bool Images::getThumbnail ( const char *pageSite,
			    int32_t  siteLen  ,
			    int64_t docId ,
			    XmlDoc *xd ,
			    collnum_t collnum,//char *coll ,
			    //char **statusPtr ,
			    void *state ,
			    void   (*callback)(void *state) ) {
	// sanity check
	if ( ! m_setCalled ) { g_process.shutdownAbort(true); }
	// we haven't had any error
	m_hadError  = 0;
	// no reason to stop yet
	m_stopDownloading = false;
	// reset here now
	m_i = 0;
	m_j = 0;
	m_phase = 0;

	// sanity check
	if ( ! m_pageUrl ) { g_process.shutdownAbort(true); }
	// sanity check
	if ( ! pageSite ) { g_process.shutdownAbort(true); }
	// we need to be a permalink
	//if ( ! isPermalink ) return true;

	// save these
	//m_statusPtr = statusPtr;
	// save this
	m_collnum = collnum;
	m_docId = docId;
	m_callback = callback;
	m_state = state;

	// if no candidates, we are done, no error
	if ( m_numImages == 0 ) return true;

	//Vector *v = xd->getTagVector();
	// this will at least have one component, the 0/NULL component
	uint32_t *tph = xd->getTagPairHash32();
	// must not block or error on us
	if ( tph == (void *)-1 ) { g_process.shutdownAbort(true); }
	// must not error on use?
	if ( ! tph ) { g_process.shutdownAbort(true); }

	// . see DupDetector.cpp, very similar to this
	// . see how many pages we have from our same site with our same 
	//   html template (and that are permalinks)
	char buf[2000];
	// site MUST NOT start with "http://"
	if ( siteLen>=7 && memcmp(pageSite, "http://", 7)==0) { g_process.shutdownAbort(true); }
	// this must match what we hash in XmlDoc::hashNoSplit()
	sprintf ( buf , "gbsitetemplate:%" PRIu32"%*.*s", (uint32_t)*tph, (int)siteLen,(int)siteLen,pageSite);
	// TODO: make sure this is a no-split termid storage thingy
	// in Msg14.cpp
	Query q;
	if ( ! q.set2 ( buf, langUnknown, NULL, false, true, ABS_MAX_QUERY_TERMS ) )
		// return true with g_errno set on error
		return true;
	if(q.getNumTerms()!=1) { g_process.shutdownAbort(true); }
	// store the termid
	int64_t termId = q.getTermId(0);

	key144_t startKey ;
	key144_t endKey   ;
	Posdb::makeStartKey(&startKey,termId);
	Posdb::makeEndKey  (&endKey  ,termId);

	// get shard of that (this termlist is sharded by termid -
	// see XmlDoc.cpp::hashNoSplit() where it hashes gbsitetemplate: term)
	int32_t shardNum = g_hostdb.getShardNumByTermId ( &startKey );

	logDebug(g_conf.m_logDebugImage, "image: image checking %s list on shard %" PRId32,buf,shardNum);

	// just use msg0 and limit to like 1k or something
	if ( ! m_msg0.getList ( -1    , // hostid
				RDB_POSDB ,
				m_collnum      ,
				&m_list     , // RdbList ptr
				(const char *)&startKey,
				(const char *)&endKey,
				1024        , // minRecSize
				this        ,
				gotTermListWrapper ,
				MAX_NICENESS       ,
				false , // err correction?
				true  , // inc tree?
				-1    , // firstHostId
				0     , // start filenum
				-1    , // numFiles
				30000 , // timeout
				false , // isRealMerge?
				false , // doIndexdbSplit?
				shardNum ))// force paritysplit
		return false;


	// did not block
	return gotTermFreq();
}

// returns false if blocked, true otherwise
bool Images::gotTermFreq ( ) {
	// error?
	if ( g_errno ) return true;
	// bail if less than 10
	//int64_t nt = m_msg36.getTermFreq();
	// each key but the first is 12 bytes (compressed)
	int64_t nt = (m_list.getListSize() - 6)/ 12;
	// . return true, without g_errno set, we are done
	// . if we do not have 10 or more webpages that share this same 
	//   template then do not do image extraction at all, it is too risky
	//   that we get a bad image
	// . MDW: for debugging, do not require 10 pages of same template
	//if ( nt < 10 ) return true;
	if ( nt < -2 ) return true;
	// now see which of the image urls are unique
	if ( ! launchRequests () ) return false;
	// i guess we did not block
	return true;
}

// . returns false if blocked, true otherwise
// . see if other pages we've indexed have this same image url
bool Images::launchRequests ( ) {
	// loop over all images
	for ( int32_t i = m_i ; i < m_numImages ; i++ ) {
		// advance
		m_i++;
		// assume no error
		m_errors[i] = 0;
		// make the keys. each term is a gbimage:<imageUrl> term
		// so we are searching for the image url to see how often
		// it is repeated on other pages.
		key144_t startKey ; 
		key144_t endKey   ;
		Posdb::makeStartKey(&startKey,m_termIds[i]);
		Posdb::makeEndKey  (&endKey  ,m_termIds[i]);
		uint32_t shardNum;
		// assume to be for posdb here
		shardNum = g_hostdb.getShardNumByTermId ( &startKey );

		logDebug(g_conf.m_logDebugImage, "image: image checking shardnum %" PRId32" (termid0=%" PRIu64")for image url #%" PRId32,
		         shardNum ,m_termIds[i],i);

		// get the termlist
		if ( ! m_msg0.getList ( -1    , // hostid
					RDB_POSDB,
					m_collnum      ,
					&m_list     , // RdbList ptr
					(const char *)&startKey,
					(const char *)&endKey,
					1024        , // minRecSize
					this        ,
					gotTermListWrapper ,
					MAX_NICENESS       ,
					false , // err correction?
					true  , // inc tree?
					-1    , // firstHostId
					0     , // start filenum
					-1    , // numFiles
					30000 , // timeout
					false , // isRealMerge?
					false , // doIndexdbSplit?
					shardNum ))// force paritysplit
			return false;
		// process the msg36 response
		gotTermList ();
	}
	// i guess we didn't block
	return downloadImages();
}

// we got a reply
void gotTermListWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// process/store the reply
	THIS->gotTermList();
	// try to launch more, returns false if it blocks
	if ( ! THIS->launchRequests() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

void Images::gotTermList ( ) {
	int32_t i = m_i - 1;
	// i guess get errors too
	if ( g_errno ) m_errors[i] = g_errno;
	// set a globalish flag
	if ( ! m_hadError ) m_hadError = g_errno;
	// on error skip this
	if ( g_errno ) return;
	// check docids in termlist
	m_list.resetListPtr();

	// loop over it
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRecord() ) {
		// is it us? if so ignore it
		if ( Posdb::getDocId(m_list.getCurrentRec()) == m_docId ) continue;
		// crap, i guess our image url is not unique. mark it off.
		m_errors[i] = EDOCDUP;
		// no need to go further
		break;
	}
	
}

bool Images::downloadImages() {
	// all done if we got a valid thumbnail
	//if ( m_thumbnailValid ) return true;

	int32_t  srcLen;
	const char *src = NULL;

	// . download each leftover image
	// . stop as soon as we get one with good dimensions
	// . make a thumbnail of that one
	for (  ; m_j < m_numImages ; m_j++ , m_phase = 0 ) {

		// did collection get nuked?
		CollectionRec *cr = g_collectiondb.getRec(m_collnum);
		if ( ! cr ) { g_errno = ENOCOLLREC; return true; }

		// clear error
		g_errno = 0;

		if ( m_phase == 0 ) {
			// advance
			m_phase++;
			// only if not diffbot, we set "src" above for it

			// get the url of the image
			src = getImageUrl ( m_j , &srcLen );
			// use "pageUrl" as the baseUrl
			m_imageUrl.set( m_pageUrl, src, srcLen );
			// if we should stop, stop
			if ( m_stopDownloading ) break;
			// skip if bad or not unique
			if ( m_errors[m_j] ) continue;
			// set status msg
			sprintf ( m_statusBuf ,"downloading image %" PRId32,m_j);
			// point to it
			if ( m_xd ) m_xd->setStatus ( m_statusBuf );
		}

		// get image ip
		if ( m_phase == 1 ) {
			// advance
			m_phase++;
			// this increments phase if it should
			if ( ! getImageIp() ) return false;
			// error?
			if ( g_errno ) continue;
		}

		// download the actual image
		if ( m_phase == 2 ) {
			// advance
			m_phase++;
			// download image data
			if ( ! downloadImage() ) return false;
			// error downloading?
			if ( g_errno ) continue;
		}

		// get thumbnail using threaded call to netpbm stuff
		if ( m_phase == 3 ) {
			// advance
			m_phase++;
			// call pnmscale etc. to make thumbnail
			if ( ! makeThumb() ) return false;
			// error downloading?
			if ( g_errno ) continue;
		}

		// error making thumb or just not a good thumb size?
		if ( ! m_thumbnailValid ) {
			// free old image we downloaded, if any
			m_msg13.reset();
			// i guess do this too, it was pointing at it in msg13
			m_imgReply = NULL;
			// try the next image candidate
			continue;
		}

		// it's a keeper
		int32_t urlSize = m_imageUrl.getUrlLen() + 1; // include \0
		// . make our ThumbnailArray out of it
		int32_t need = 0;
		// the array itself
		need += sizeof(ThumbnailArray);
		// and each thumbnail it contains
		need += urlSize;
		need += m_thumbnailSize;
		need += sizeof(ThumbnailInfo);
		// reserve it
		if( !m_imageBuf.reserve ( need ) ) {
			logError("Could not reserve needed %" PRId32 " bytes, bailing!", need);
			return false;
		}

		// point to array
		ThumbnailArray *ta =(ThumbnailArray *)m_imageBuf.getBufStart();
		// set that as much as possible, version...
		ta->m_version = 0;
		// and thumb count
		ta->m_numThumbnails = 1;
		// now store the thumbnail info
		ThumbnailInfo *ti = ta->getThumbnailInfo (0);
		// and set our one thumbnail
		ti->m_origDX = m_dx;
		ti->m_origDY = m_dy;
		ti->m_dx = m_tdx;
		ti->m_dy = m_tdy;
		ti->m_urlSize = urlSize;
		ti->m_dataSize = m_thumbnailSize;
		// now copy the data over sequentially
		char *p = ti->m_buf;
		// the image url
		gbmemcpy(p,m_imageUrl.getUrl(),urlSize);
		p += urlSize;
		// the image thumbnail data
		gbmemcpy(p,m_imgData,m_thumbnailSize);
		p += m_thumbnailSize;
		// update buf length of course
		m_imageBuf.setLength ( p - m_imageBuf.getBufStart() );

		// validate the buffer
		m_imageBufValid = true;

		// save mem. do this after because m_imgData uses m_msg13's
		// reply buf to store the thumbnail for now...
		m_msg13.reset();
		m_imgReply = NULL;

		g_errno = 0;

		return true;
	}

	// don't tell caller EBADIMG it will make him fail to index doc
	g_errno = 0;

	return true;
}

static void gotImgIpWrapper ( void *state , int32_t ip ) {
	Images *THIS = (Images *)state;
	// control loop
	if ( ! THIS->downloadImages() ) return;
	// call callback at this point, we are done with the download loop
	THIS->m_callback ( THIS->m_state );
}

bool Images::getImageIp ( ) {
	if (!m_msgc.getIp(m_imageUrl.getHost(), m_imageUrl.getHostLen(), &m_latestIp, this, gotImgIpWrapper)) {
		// we blocked
		return false;
	}
	return true;
}

static void downloadImageWrapper ( void *state ) {
	Images *THIS = (Images *)state;
	// control loop
	if ( ! THIS->downloadImages() ) return;
	// all done
	THIS->m_callback ( THIS->m_state );
}

bool Images::downloadImage ( ) {
	// error?
	if ( m_latestIp == 0 || m_latestIp == -1 ) {
		log(LOG_DEBUG,"images: ip of %s is %" PRId32" (%s)",
		    m_imageUrl.getUrl(),m_latestIp,mstrerror(g_errno));
		// ignore errors
		g_errno = 0;
		return true;
	}
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }
	// assume success
	m_httpStatus = 200;
	// set the request
	Msg13Request *r = &m_msg13Request;
	r->reset();
	r->m_maxTextDocLen  = 200000;
	r->m_maxOtherDocLen = 500000;
	r->m_urlIp = m_latestIp;
	// url is the most important
	r-> ptr_url = m_imageUrl.getUrl();
	r->size_url = m_imageUrl.getUrlLen()+1; // include \0
	// . try to download it
	// . i guess we are ignoring hammers at this point
	if ( ! m_msg13.getDoc(r,this,downloadImageWrapper))
		return false;

	return true;
}

// Use of ThreadEntry parameter is NOT thread safe
static void makeThumbWrapper ( void *state, job_exit_t exit_type ) {
	Images *that = (Images *)state;

	// store saved error into g_errno
	g_errno = that->m_errno;

	// control loop
	if ( ! that->downloadImages() ) return;

	// all done
	that->m_callback ( that->m_state );
}

bool Images::makeThumb ( ) {
	// did it have an error?
	if ( g_errno ) {
		// just give up on all of them if one has an error
		log ( LOG_WARN, "image: had error downloading image on page %s: %s. "
		      "Not downloading any more.",
		      m_pageUrl->getUrl(),mstrerror(g_errno));
		// stop it
		m_stopDownloading = true;
		return true;
	}
	char *buf;
	int32_t  bufLen, bufMaxLen;
	HttpMime mime;
	m_imgData     = NULL;
	m_imgDataSize = 0;

	log( LOG_DEBUG, "image: gotImage() entered." );
	// . if there was a problem, just ignore, don't let it stop getting
	//   the real page.
	if ( g_errno ) {
		log( "ERROR? g_errno puked: %s", mstrerror(g_errno) );
		//g_errno = 0;
		return true;
	}
	//if ( ! slot ) return true;
	// extract image data from the socket
	buf       = m_msg13.m_replyBuf;
	bufLen    = m_msg13.m_replyBufSize;
	bufMaxLen = m_msg13.m_replyBufAllocSize;
	// no image?
	if ( ! buf || bufLen <= 0 ) {
		g_errno = EBADIMG;
		return true;
	}
	// we are image candidate #i
	//int32_t i = m_j - 1;
	// get img tag node
	// get the url of the image
	int32_t  srcLen;
	const char *src = getImageUrl ( m_j , &srcLen );
	// set it to the full url
	Url iu;
	// use "pageUrl" as the baseUrl
	iu.set( m_pageUrl, src, srcLen );
	// get the mime
	if ( ! mime.set ( buf, bufLen, &iu ) ) {		
		log ( "image: MIME.set() failed in gotImage()" );
		// give up on the remaining images then
		m_stopDownloading = true;
		g_errno = EBADIMG;
		return true;
	}
	// set the status so caller can see
	int32_t httpStatus = mime.getHttpStatus();
	// check the status
	if ( httpStatus != 200 ) {
		log( LOG_DEBUG, "image: http status of img download is %" PRId32".",
		     m_httpStatus);
		// give up on the remaining images then
		m_stopDownloading = true;
		g_errno = EBADIMG;
		return true;
	}
	// make sure this is an image
	m_imgType = mime.getContentType();
	if ( m_imgType < CT_GIF || m_imgType > CT_TIFF ) {
		log( LOG_DEBUG, "image: gotImage() states that this image is "
		     "not in a format we currently handle." );
		// try the next image if any
		g_errno = EBADIMG;
		return true;
	}
	// get the content
	m_imgData     = buf + mime.getMimeLen();
	m_imgDataSize = bufLen - mime.getMimeLen();
	// Reset socket, so socket doesn't free the data, now we own
	// We must free the buf after thumbnail is inserted in TitleRec
	m_imgReply       = buf;//slot->m_readBuf;
	m_imgReplyLen    = bufLen;//slot->m_readBufSize;
	m_imgReplyMaxLen = bufMaxLen;//slot->m_readBufMaxSize;
	// do not let UdpServer free the reply, we own it now
	//slot->m_readBuf = NULL;

	if ( m_imgReplyLen == 0 ) {
		log( LOG_DEBUG, "image: Returned empty image reply!" );
		g_errno = EBADIMG;
		return true;
	}

	// get next if too small
	if ( m_imgDataSize < 20 ) { g_errno = EBADIMG; return true; }

	int32_t imageType;
	getImageInfo ( m_imgData, m_imgDataSize, &m_dx, &m_dy, &imageType );

	// log the image dimensions
	log( LOG_DEBUG,"image: Image Link: %s", iu.getUrl() );
	log( LOG_DEBUG,"image: Max Buffer Size: %" PRIu32" bytes.",m_imgReplyMaxLen);
	log( LOG_DEBUG,"image: Image Original Size: %" PRIu32" bytes.",m_imgReplyLen);
	log( LOG_DEBUG,"image: Image Buffer @ 0x%" PTRFMT" - 0x%" PTRFMT"",(PTRTYPE)m_imgReply,
	     (PTRTYPE)(m_imgReply+m_imgReplyMaxLen) );
	log( LOG_DEBUG, "image: Size: %" PRIu32"px x %" PRIu32"px", m_dx, m_dy );

	// what is this?
	if ( m_dx <= 0 || m_dy <= 0 ) {
		log(LOG_DEBUG, "image: Image has bad dimensions.");
		g_errno = EBADIMG;
		return true;
	}


	// skip if bad dimensions
	if( ((m_dx < 50) || (m_dy < 50)) && ((m_dx > 0) && (m_dy > 0)) ) {
		log(LOG_DEBUG,
		    "image: Image is too small to represent a news article." );
		g_errno = EBADIMG;
		return true;
	}

	// skip if bad aspect ratio. 5x1 or 1x5 is bad i guess
	if ( m_dx > 0 && m_dy > 0 ) {
		float aspect = (float)m_dx / (float)m_dy;
		if ( aspect < .2 || aspect > 5.0 ) {
			log(LOG_DEBUG,
			    "image: Image aspect ratio is worse that 5 to 1");
			g_errno = EBADIMG;
			return true;
		}
	}

	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }

	// save how big of thumbnails we should make. user can change
	// this in the 'spider controls'
	m_xysize = cr->m_thumbnailMaxWidthHeight ;
	// make it 250 pixels if no decent value provided
	if ( m_xysize <= 0 ) m_xysize = 250;
	// and keep it sane
	if ( m_xysize > 2048 ) m_xysize = 2048;

	// update status
	if ( m_xd ) m_xd->setStatus ( "making thumbnail" );
	// log it
	log ( LOG_DEBUG, "image: gotImage() thumbnailing image." );
	// create the thumbnail...
	// reset this... why?
	g_errno = 0;
	// reset this since filterStart_r() will set it on error
	m_errno = 0;
	// callThread returns true on success, in which case we block
	if ( g_jobScheduler.submit(thumbStartWrapper_r, makeThumbWrapper, this, thread_type_generate_thumbnail, MAX_NICENESS) ) {
		return false;
	}
	// threads might be off
	logf ( LOG_DEBUG, "image: Calling thumbnail gen without thread.");
	thumbStartWrapper_r ( this );
	return true;
}

// Use of ThreadEntry parameter is NOT thread safe
void thumbStartWrapper_r ( void *state ) {
	Images *that = (Images *)state;
	// assume no error
	that->m_errno = 0;

	that->thumbStart_r ( true /* am thread?*/ );

	if (g_errno && !that->m_errno) {
		that->m_errno = g_errno;
	}
}

void Images::thumbStart_r ( bool amThread ) {

	int64_t start = gettimeofdayInMilliseconds();

	//static char  scmd[200] = "%stopnm %s | "
	//                         "pnmscale -xysize 100 100 - | "
	//                         "ppmtojpeg - > %s";

	
	log( LOG_DEBUG, "image: thumbStart_r entered." );

	//DIR  *d;
	//char  cmd[2500];
	//sprintf( cmd, "%strash", g_hostdb.m_dir );

	makeTrashDir();

	// get thread id. pthread_t is 64 bit and pid_t is 32 bit on
	// 64 bit oses
	pthread_t id = pthread_self();

	// pass the input to the program through this file
	// rather than a pipe, since popen() seems broken.
	// m_dir ends in / so this should work.
	char in[364];
	snprintf ( in , 363,"%strash/in.%" PRId64
		   , g_hostdb.m_dir, (int64_t)id );
	unlink ( in );

	log( LOG_DEBUG, "image: thumbStart_r create in file." );

	// collect the output from the filter from this file
	// m_dir ends in / so this should work.
	char out[364];
	snprintf ( out , 363,"%strash/out.%" PRId64
		   , g_hostdb.m_dir, (int64_t)id );
        unlink ( out );

	log( LOG_DEBUG, "image: thumbStart_r create out file." );

        // ignore errno from those unlinks        
        errno = 0;

        // Open/Create temporary file to store image to
        int   fhndl;
        if( (fhndl = open( in, O_RDWR+O_CREAT ,
			   getFileCreationFlags()
			   )) < 0 ) {
               log(LOG_WARN,  "image: Could not open file, %s, for writing: %s - %d.",
       		    in, mstrerror( m_errno ), fhndl );
	       m_imgDataSize = 0;
       	       return;
        }

        // Write image data into temporary file
        if( write( fhndl, m_imgData, m_imgDataSize ) < 0 ) {
               log(LOG_WARN,  "image: Could not write to file, %s: %s.",
       		    in, mstrerror( m_errno ) );
       	       close( fhndl );
	       unlink( in );
	       m_imgDataSize = 0;
       	       return;
        }

        // Close temporary image file now that we have finished writing
        if( close( fhndl ) < 0 ) {
               log(LOG_WARN,  "image: Could not close file, %s, for writing: %s.",
       	            in, mstrerror( m_errno ) );
	       unlink( in );
	       m_imgDataSize = 0;
      	       return;
        }
	fhndl = 0;

        // Grab content type from mime
	//int32_t imgType = mime.getContentType();
        char  ext[5];
        switch( m_imgType ) {
               case CT_GIF:
		       strcpy( ext, "gif" );
	               break;
               case CT_JPG:
		       strcpy( ext, "jpeg" );
		       break;
               case CT_PNG:
		       strcpy( ext, "png" );
		       break;
               case CT_TIFF:
		       strcpy( ext, "tiff" );
		       break;
	       case CT_BMP:
		       strcpy( ext, "bmp" );
		       break;
        } 

	char  cmd[2500];

	//sprintf( cmd, scmd, ext, in, out);
	const char *wdir = g_hostdb.m_dir;
	// wdir ends in / so this should work.
	snprintf( cmd, sizeof(cmd),
		 "LD_LIBRARY_PATH=%s %s%stopnm %s | "
		 "LD_LIBRARY_PATH=%s %spnmscale -xysize %" PRId32" %" PRId32" - | "
		  // put all its stderr msgs into /dev/null
		  // so "jpegtopnm: WRITING PPM FILE" doesn't clog console
		 "LD_LIBRARY_PATH=%s %sppmtojpeg - > %s"
		  , wdir, wdir, ext, in
		  , wdir, wdir, m_xysize, m_xysize
		  , wdir, wdir, out
		 );
	cmd[sizeof(cmd)-1] = '\0';

	// if they already have netpbm package installed use that then
	static bool s_checked = false;
	static bool s_hasNetpbm = false;
	if ( ! s_checked ) {
		s_checked = true;
		File f;
		f.set("/usr/bin/pnmscale");
		s_hasNetpbm = f.doesExist() ;
	}
	if ( s_hasNetpbm )
		snprintf( cmd, sizeof(cmd),
			  "%stopnm %s | "
			  "pnmscale -xysize %" PRId32" %" PRId32" - | "
			  "ppmtojpeg - > %s"
			  , ext, in
			  , m_xysize, m_xysize
			  , out
			  );
		
        
        // Call clone function for the shell to execute command
	int err = system( cmd ); // m_thmbconvTimeout );

	//if( (m_dx != 0) && (m_dy != 0) )
	//	unlink( in );
	unlink ( in );

	if ( err == 127 ) {
		m_errno = EBADENGINEER;
		log("image: /bin/sh does not exist.");
		unlink ( out );
		m_stopDownloading = true;
		return;
	}
	// this will happen if you don't upgrade glibc to 2.2.4-32 or above
	if ( err != 0 ) {
		m_errno = EBADENGINEER;
		log(LOG_WARN, "image: Call to system(\"%s\") had error.",cmd);
		unlink ( out );
		m_stopDownloading = true;
		return;
	}

        // Open new file with thumbnail image
        if( (fhndl = open( out, O_RDONLY )) < 0 ) {
               log(LOG_WARN,  "image: Could not open file, %s, for reading: %s.",
		    out, mstrerror( m_errno ) );
		unlink ( out );
		m_stopDownloading = true;
	       return;
        }

	if( (m_thumbnailSize = lseek( fhndl, 0, SEEK_END )) < 0 ) {
		log( LOG_WARN, "image: Seek of file, %s, returned invalid size: %" PRId32,
		     out, m_thumbnailSize );
		m_stopDownloading = true;
		close(fhndl);
		unlink ( out );
		return;
	}

	if( m_thumbnailSize > m_imgReplyMaxLen ) {
		log(LOG_DEBUG,"image: Image thumbnail larger than buffer!" );
		log(LOG_DEBUG,"image: File Read Bytes: %" PRId32, m_thumbnailSize);
		log(LOG_DEBUG,"image: Buf Max Bytes  : %" PRId32,m_imgReplyMaxLen );
		log(LOG_DEBUG,"image: -----------------------" );
		log(LOG_DEBUG,"image: Diff           : %" PRId32,
		     m_imgReplyMaxLen-m_thumbnailSize );
		close(fhndl);
		unlink ( out );
		return;

	}

	if( lseek( fhndl, 0, SEEK_SET ) < 0 ) {
		log( "image: Seek couldn't rewind file, %s.", out );
		m_stopDownloading = true;
		close(fhndl);
		unlink ( out );
		return;
	}

        // . Read contents back into image ptr
	// . this is somewhat of a hack since it overwrites the original img
        if( (m_thumbnailSize = read( fhndl, m_imgData, m_imgDataSize )) < 0 ) {
                log( LOG_WARN, "image: Could not read from file, %s: %s.",
 		     out, mstrerror( m_errno ) );
	        close( fhndl );
		m_stopDownloading = true;
		unlink( out );
	        return;
        }

        if( close( fhndl ) < 0 ) {
                log( LOG_WARN, "image: Could not close file, %s, for reading: %s.",
 		     out, mstrerror( m_errno ) );
		unlink( out );
		m_stopDownloading = true;
		unlink ( out );
 	        return;
        }
	fhndl = 0;
       	unlink( out );
	int64_t stop = gettimeofdayInMilliseconds();
	// tell the loop above not to download anymore, we got one
	m_thumbnailValid = true;

	// MDW: this was m_imgReply
	getImageInfo ( m_imgData , m_thumbnailSize , &m_tdx , &m_tdy , NULL );

	// now make the meta data struct
	// <imageUrl>\0<width><height><thumbnailData>
	


	log( LOG_DEBUG, "image: Thumbnail size: %" PRId32" bytes.", m_imgDataSize );
	log( LOG_DEBUG, "image: Thumbnail dx=%" PRId32" dy=%" PRId32".", m_tdx,m_tdy );
	log( LOG_DEBUG, "image: Thumbnail generated in %" PRId64"ms.", stop-start );
}


// . *it is the image type
void getImageInfo(const char *buf, int32_t bufSize,
                  int32_t *dx, int32_t *dy, int32_t *it)
{

	// default to zeroes
	*dx = 0;
	*dy = 0;

	// get the dimensions of the image
	if( strncasestr( buf, 20, "Exif"  ) ) {
		log(LOG_DEBUG, "image: Image Link: ");
		log(LOG_DEBUG, "image: We currently do not handle EXIF image "
		     "types." );
		// try the nextone
		return;
	}
	else if( strncasestr( buf, 20, "GIF"  ) ) {
		if ( it ) *it = CT_GIF;
		log( LOG_DEBUG, "image: GIF INFORMATION:" );
		if( bufSize > 9 ) {
			*dx = ((uint32_t)buf[7]) << 8;
			*dx += (unsigned char)buf[6];
			*dy = ((uint32_t)buf[9]) << 8;
			*dy += (unsigned char)buf[8];
		}
	}
	else if( strncasestr( buf, 20, "JFIF" ) ) {
		if ( it ) *it = CT_JPG;
		log( LOG_DEBUG, "image: JPEG INFORMATION:" );
		int32_t i;
		for( i = 0; i < bufSize; i++ ) {
			if( bufSize < i+8 )
				break;
			if( (unsigned char)buf[i] != 0xFF ) continue;
			if( (unsigned char)buf[i+1] == 0xC0 ){
				*dy = ((uint32_t)buf[i+5]) << 8;
				*dy += (unsigned char)buf[i+6];
				*dx = ((uint32_t)buf[i+7]) << 8;
				*dx += (unsigned char)buf[i+8];
				break;
			}
			else if( (unsigned char) buf[i+1] == 0xC2 ) {
				*dy = ((uint32_t)buf[i+5]) << 8;
				*dy += (unsigned char)buf[i+6];
				*dx = ((uint32_t)buf[i+7]) << 8;
				*dx += (unsigned char)buf[i+8];
				break;
			}
		}
	}
	else if( strncasestr( buf, 20, "PNG" ) ) {
		if ( it ) *it = CT_PNG;
		log( LOG_DEBUG, "image: PNG INFORMATION:" );
		if( bufSize > 25 ) {
			*dx=(uint32_t)(*(uint32_t *)&buf[16]);
			*dy=(uint32_t)(*(uint32_t *)&buf[20]);
			// these are in network order
			*dx = ntohl(*dx);
			*dy = ntohl(*dy);
		}
	}
	else if( strncasestr( buf, 20, "MM" ) ) {
		if ( it ) *it = CT_TIFF;
		log( LOG_DEBUG, "image: TIFF INFORMATION:" );
		int32_t startCnt = (uint32_t)buf[7]+4;
		for( int32_t i = startCnt; i < bufSize; i += 12 ) {
			if( bufSize < i+10 )
				break;
			if( buf[i] != 0x01 ) continue;
			if( buf[i+1] == 0x01 )
				*dy = (uint32_t)
					(*(uint16_t *)&buf[i+8]);
			else if( buf[i+1] == 0x00 )
				*dx = (uint32_t)
					(*(uint16_t *)&buf[i+8]);
		}
	}
	else if( strncasestr( buf, 20, "II" ) ) {
		if ( it ) *it = CT_TIFF;
		log( LOG_DEBUG, "image: TIFF INFORMATION:" );
		int32_t startCnt = (uint32_t)buf[7]+4;
		for( int32_t i = startCnt; i < bufSize; i += 12 ) {
			if( bufSize < i+10 )
				break;
			if( buf[i] == 0x01 && buf[i+1] == 0x01 )
				*dy = (uint32_t)
					(*(uint16_t *)&buf[i+8]);
			if( buf[i] == 0x00 && buf[i+1] == 0x01 )
				*dx = (uint32_t)
					(*(uint16_t *)&buf[i+8]);
		}
	}
	else if( strncasestr( buf, 20, "BM" ) ) {
		if ( it ) *it = CT_BMP;
		log( LOG_DEBUG, "image: BMP INFORMATION:" );
		if( bufSize > 27 ) {
			*dx=(uint32_t)(*(uint32_t *)&buf[18]);
			*dy=(uint32_t)(*(uint32_t *)&buf[22]);
		}
	}
	else 
		log( LOG_DEBUG, "image: Image Corrupted? No type found in "
		     "data." );
}

// container is maxWidth X maxHeight, so try to fix widget in there
bool ThumbnailInfo::printThumbnailInHtml ( SafeBuf *sb , 
					   int32_t maxWidth ,
					   int32_t maxHeight,
					   bool printLink ,
					   int32_t *retNewdx ,
					   const char *style ,
					   char format )  {
	if ( ! style ) style = "";
	// account for scrollbar on the right
	//maxSide -= (int32_t)SCROLLBAR_WIDTH;
	// avoid distortion.
	// if image is wide, use that to scale
	if ( m_dx <= 0 ) return true;
	if ( m_dy <= 0 ) return true;
	float xscale = 
		(float)maxWidth/
		(float)m_dx;
	float yscale = 
		(float)maxHeight/
		(float)m_dy;
	float min = xscale;
	if ( yscale < min ) min = yscale;
	int32_t newdx = (int32_t)((float)m_dx * min);
	int32_t newdy = (int32_t)((float)m_dy * min);

	// might be FORMAT_AJAX!
	if ( printLink && format !=FORMAT_XML && format != FORMAT_JSON )
		sb->safePrintf("<a href=%s>", getUrl() );

	if ( format !=FORMAT_XML && format != FORMAT_JSON )
		sb->safePrintf("<img width=%" PRId32" height=%" PRId32" align=left "
			       "%s"
			       "src=\"data:image/"
			       "jpg;base64,"
			       , newdx
			       , newdy
			       , style
			       );

	if ( format == FORMAT_XML )
		sb->safePrintf("\t<imageBase64>");

	if ( format == FORMAT_JSON )
		sb->safePrintf("\t\"imageBase64\":\"");

	// encode image in base 64
	sb->base64Encode ( getData(), m_dataSize );
	if ( format !=FORMAT_XML && format != FORMAT_JSON ) {
		sb->safePrintf("\">");
		if ( printLink ) sb->safePrintf ("</a>");
	}

	if ( format == FORMAT_XML )
		sb->safePrintf("</imageBase64>\n");

	if ( format == FORMAT_JSON )
		sb->safePrintf("\",\n");

	// widget needs to know the width of the thumb for formatting
	// the text either on top of the thumb or to the right of it
	if ( retNewdx ) *retNewdx = newdx;
	return true;
}


const char *Images::getImageUrl ( int32_t j , int32_t *urlLen ) {

	int32_t node = m_imageNodes[j];
	int32_t srcLen = 0;
	char *src = m_xml->getString(node,"src",&srcLen);
	// maybe it was an og:image meta tag
	if ( ! src ) 
		src = m_xml->getString(node,"content",&srcLen);

	// wtf?
	if ( ! src ) 
		log("image: image bad/null src");

	*urlLen = srcLen;
	return src;
}
