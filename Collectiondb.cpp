#include "gb-include.h"

#include "Collectiondb.h"
#include "Xml.h"
#include "Url.h"
#include "Loop.h"
#include "Spider.h"  // for calling SpiderLoop::collectionsUpdated()
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Clusterdb.h"
#include "Linkdb.h"
#include "Spider.h"
#include "Repair.h"
#include "Parms.h"
#include "Process.h"
#include "HttpRequest.h"
#include <sys/stat.h> //mkdir()

static HashTableX g_collTable;

// a global class extern'd in .h file
Collectiondb g_collectiondb;

Collectiondb::Collectiondb ( ) {
	m_wrapped = 0;
	m_numRecs = 0;
	m_numRecsUsed = 0;
	m_numCollsSwappedOut = 0;
	m_initializing = false;
	m_needsSave = false;
	m_recs = NULL;

	// sanity
	if ( RDB_END2 >= RDB_END ) {
		return;
	}

	log("db: increase RDB_END2 to at least %" PRId32" in Collectiondb.h",(int32_t)RDB_END);
	g_process.shutdownAbort(true);
}

// reset rdb
void Collectiondb::reset() {
	log(LOG_INFO,"db: resetting collectiondb.");
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) {
			continue;
		}
		mdelete ( m_recs[i], sizeof(CollectionRec), "CollectionRec" );
		delete ( m_recs[i] );
		m_recs[i] = NULL;
	}
	m_numRecs     = 0;
	m_numRecsUsed = 0;
	g_collTable.reset();
}

// . save to disk
// . returns false if blocked, true otherwise
bool Collectiondb::save ( ) {
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	if ( g_inAutoSave && m_numRecsUsed > 20 && g_hostdb.m_hostId != 0 ) {
		return true;
	}

	// which collection rec needs a save
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i]              ) continue;
		// temp debug message
		//logf(LOG_DEBUG,"admin: SAVING collection #%" PRId32" ANYWAY",i);
		if ( ! m_recs[i]->m_needsSave ) {
			continue;
		}

		//log(LOG_INFO,"admin: Saving collection #%" PRId32".",i);
		m_recs[i]->save ( );
	}
	// oh well
	return true;
}



///////////
//
// fill up our m_recs[] array based on the coll.*.*/coll.conf files
//
///////////
bool Collectiondb::loadAllCollRecs ( ) {

	m_initializing = true;

	char dname[1024];
	// MDW: sprintf ( dname , "%s/collections/" , g_hostdb.m_dir );
	sprintf ( dname , "%s" , g_hostdb.m_dir );
	Dir d;
	d.set ( dname );
	if ( ! d.open ()) {
		log( LOG_WARN, "admin: Could not load collection config files." );
		return false;
	}

	int32_t count = 0;
	const char *f;
	while ( ( f = d.getNextFilename ( "*" ) ) ) {
		// skip if first char not "coll."
		if ( strncmp ( f , "coll." , 5 ) != 0 ) continue;
		// must end on a digit (i.e. coll.main.0)
		if ( ! is_digit (f[strlen(f)-1]) ) continue;
		// count them
		count++;
	}

	// reset directory for another scan
	d.set ( dname );
	if ( ! d.open ()) {
		log( LOG_WARN, "admin: Could not load collection config files." );
		return false;
	}

	// note it
	//log(LOG_INFO,"db: loading collection config files.");
	// . scan through all subdirs in the collections dir
	// . they should be like, "coll.main/" and "coll.mycollection/"
	while ( ( f = d.getNextFilename ( "*" ) ) ) {
		// skip if first char not "coll."
		if ( strncmp ( f , "coll." , 5 ) != 0 ) continue;
		// must end on a digit (i.e. coll.main.0)
		if ( ! is_digit (f[strlen(f)-1]) ) continue;
		// point to collection
		const char *coll = f + 5;
		// NULL terminate at .
		const char *pp = strchr ( coll , '.' );
		if ( ! pp ) continue;
		char collname[256];
		memcpy(collname,coll,pp-coll);
		collname[pp-coll] = '\0';
		// get collnum
		collnum_t collnum = atol ( pp + 1 );
		// add it
		if ( ! addExistingColl ( collname, collnum ) )
			return false;
		// swap it out if we got 100+ collections
		// if ( count < 100 ) continue;
		// CollectionRec *cr = getRec ( collnum );
		// if ( cr ) cr->swapOut();
	}
	// if no existing recs added... add coll.main.0 always at startup
	if ( m_numRecs == 0 ) {
		log("admin: adding main collection.");
		addNewColl ( "main", true, 0 );
	}

	m_initializing = false;

	return true;
}

// after we've initialized all rdbs in main.cpp call this to clean out
// our rdb trees
bool Collectiondb::cleanTrees() {
	// remove any nodes with illegal collnums
	g_posdb.getRdb()->cleanTree();
	g_titledb.getRdb()->cleanTree();
	g_spiderdb.getRdb()->cleanTree();
	g_doledb.getRdb()->cleanTree();

	// success
	return true;
}

#include "Statsdb.h"

// same as addOldColl()
bool Collectiondb::addExistingColl ( const char *coll, collnum_t collnum ) {

	int32_t i = collnum;

	// ensure does not already exist in memory
	collnum_t oldCollnum = getCollnum(coll);
	if ( oldCollnum >= 0 ) {
		g_errno = EEXIST;
		log("admin: Trying to create collection \"%s\" but "
		    "already exists in memory. Do an ls on "
		    "the working dir to see if there are two "
		    "collection dirs with the same coll name",coll);
		g_process.shutdownAbort(true);
	}

	// also try by #, i've seen this happen too
	CollectionRec *ocr = getRec ( i );
	if ( ocr ) {
		g_errno = EEXIST;
		log(LOG_WARN, "admin: Collection id %i is in use already by "
		    "%s, so we can not add %s. moving %s to trash."
		    ,(int)i,ocr->m_coll,coll,coll);
		SafeBuf cmd;
		int64_t now = gettimeofdayInMilliseconds();
		cmd.safePrintf ( "mv coll.%s.%i trash/coll.%s.%i.%" PRIu64
				 , coll
				 ,(int)i
				 , coll
				 ,(int)i
				 , now );
		//log("admin: %s",cmd.getBufStart());
		gbsystem ( cmd.getBufStart() );
		return true;
	}

	// create the record in memory
	CollectionRec *cr;
	try {
		cr = new (CollectionRec);
	}
	catch(std::bad_alloc) {
		log( LOG_WARN, "admin: Failed to allocated %" PRId32" bytes for new collection record for '%s'.",
		     (int32_t)sizeof(CollectionRec),coll);
		return false;
	}
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" );

	// set collnum right for g_parms.setToDefault() call just in case
	// because before it was calling CollectionRec::reset() which
	// was resetting the RdbBases for the m_collnum which was garbage
	// and ended up resetting random collections' rdb. but now
	// CollectionRec::CollectionRec() sets m_collnum to -1 so we should
	// not need this!
	//cr->m_collnum = oldCollnum;

	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr , OBJ_COLL , cr );

	strcpy ( cr->m_coll , coll );
	cr->m_collLen = strlen ( coll );
	cr->m_collnum = i;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	cr->m_needsSave = false;
	//log("admin: loaded old coll \"%s\"",coll);

	// load coll.conf file
	if ( ! cr->load ( coll , i ) ) {
		mdelete ( cr, sizeof(CollectionRec), "CollectionRec" );
		log(LOG_WARN, "admin: Failed to load coll.%s.%" PRId32"/coll.conf",coll,i);
		delete ( cr );
		if ( m_recs ) m_recs[i] = NULL;
		return false;
	}

	if ( ! registerCollRec ( cr ) ) return false;

	// we need to compile the regular expressions or update the url
	// filters with new logic that maps crawlbot parms to url filters
	return cr->rebuildUrlFilters ( );
}

// . add a new rec
// . returns false and sets g_errno on error
// . was addRec()
// . "isDump" is true if we don't need to initialize all the rdbs etc
//   because we are doing a './gb dump ...' cmd to dump out data from
//   one Rdb which we will custom initialize in main.cpp where the dump
//   code is. like for instance, posdb.
bool Collectiondb::addNewColl ( const char *coll, bool saveIt,
				// Parms.cpp reserves this so it can be sure
				// to add the same collnum to every shard
				collnum_t newCollnum ) {
	//do not send add/del coll request until we are in sync with shard!!
	// just return ETRYAGAIN for the parmlist...

	if( !coll ) {
		logError("Called with NULL coll parameter");
		return false;
	}

	// ensure coll name is legit
	const char *p = coll;
	for ( ; *p ; p++ ) {
		if ( is_alnum_a(*p) ) continue;
		if ( *p == '-' ) continue;
		if ( *p == '_' ) continue; // underscore now allowed
		break;
	}
	if ( *p ) {
		g_errno = EBADENGINEER;
		log( LOG_WARN, "admin: '%s' is a malformed collection name because it contains the '%c' character.",coll,*p);
		return false;
	}
	if ( newCollnum < 0 ) { g_process.shutdownAbort(true); }

	// if empty... bail, no longer accepted, use "main"
	if ( !coll[0] ) {
		g_errno = EBADENGINEER;
		log( LOG_WARN, "admin: Trying to create a new collection but no collection name provided. "
		     "Use the 'c' cgi parameter to specify it.");
		return false;
	}
	// or if too big
	if ( strlen(coll) > MAX_COLL_LEN ) {
		g_errno = ENOBUFS;
		log( LOG_WARN, "admin: Trying to create a new collection whose name '%s' of %zd chars is longer than the "
		     "max of %" PRId32" chars.", coll, strlen(coll), (int32_t)MAX_COLL_LEN );
		return false;
	}

	// ensure does not already exist in memory
	if ( getCollnum ( coll ) >= 0 ) {
		g_errno = EEXIST;
		log( LOG_WARN, "admin: Trying to create collection '%s' but already exists in memory.",coll);
		// just let it pass...
		g_errno = 0 ;
		return true;
	}

	// MDW: ensure not created on disk since time of last load
	char dname[512];
	sprintf(dname, "%scoll.%s.%" PRId32"/",g_hostdb.m_dir,coll,(int32_t)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir ) {
		closedir ( dir );
		g_errno = EEXIST;
		log(LOG_WARN, "admin: Trying to create collection %s but directory %s already exists on disk.",coll,dname);
		return false;
	}

	// create the record in memory
	CollectionRec *cr;
	try {
		cr = new (CollectionRec);
	}
	catch(std::bad_alloc) {
		log( LOG_WARN, "admin: Failed to allocated %" PRId32" bytes for new collection record for '%s'.",
		     ( int32_t ) sizeof( CollectionRec ), coll );
		return false;
	}

	// register the mem
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" );

	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr , OBJ_COLL , cr );

	// set coll id and coll name for coll id #i
	strcpy ( cr->m_coll , coll );
	cr->m_collLen = strlen ( coll );
	cr->m_collnum = newCollnum;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	//
	// BEGIN NEW CODE
	//


	// . this will core if a host was dead and then when it came
	//   back up host #0's parms.cpp told it to add a new coll
	cr->m_diffbotCrawlStartTime = getTimeGlobalNoCore();
	cr->m_diffbotCrawlEndTime   = 0;

	// . just the basics on these for now
	// . if certain parms are changed then the url filters
	//   must be rebuilt, as well as possibly the waiting tree!!!
	// . need to set m_urlFiltersHavePageCounts etc.
	cr->rebuildUrlFilters ( );

	cr->m_useRobotsTxt = true;

	// reset crawler stats.they should be loaded from crawlinfo.txt
	memset ( &cr->m_localCrawlInfo , 0 , sizeof(CrawlInfo) );
	memset ( &cr->m_globalCrawlInfo , 0 , sizeof(CrawlInfo) );

	// note that
	log("colldb: initial revival for %s",cr->m_coll);

	// . assume we got some urls ready to spider
	// . Spider.cpp will wait SPIDER_DONE_TIME seconds and if it has no
	//   urls it spidered in that time these will get set to 0
	cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 1;
	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = 1;

	// set some defaults. max spiders for all priorities in this
	// collection. NO, default is in Parms.cpp.
	//cr->m_maxNumSpiders = 10;

	//cr->m_needsSave = 1;

	// start the spiders!
	cr->m_spideringEnabled = true;

	// override this?
	saveIt = true;

	//
	// END NEW CODE
	//

	//log("admin: adding coll \"%s\" (new=%" PRId32")",coll,(int32_t)isNew);

	// MDW: create the new directory
 retry22:
	if ( ::mkdir ( dname, getDirCreationFlags() ) ) {
		// valgrind?
		if ( errno == EINTR ) goto retry22;
		g_errno = errno;
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" );
		delete ( cr );
		log( LOG_WARN, "admin: Creating directory %s had error: %s.", dname,mstrerror(g_errno));
		return false;
	}

	// save it into this dir... might fail!
//	if ( saveIt && ! cr->save() ) {
	if ( ! cr->save() ) {
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" );
		delete ( cr );
		log( LOG_WARN, "admin: Failed to save file %s: %s", dname,mstrerror(g_errno));
		return false;
	}


	if ( ! registerCollRec ( cr ) ) {
		return false;
	}

	// add the rdbbases for this coll, CollectionRec::m_bases[]
	return addRdbBasesForCollRec ( cr );
}

void CollectionRec::setBasePtr(rdbid_t rdbId, class RdbBase *base) {
	if ( rdbId < 0 || rdbId >= RDB_END ) { g_process.shutdownAbort(true); }
	// Rdb::deleteColl() will call this even though we are swapped in
	// but it calls it with "base" set to NULL after it nukes the RdbBase
	// so check if base is null here.
	if ( base && m_bases[ (unsigned char)rdbId ]){ g_process.shutdownAbort(true); }
	m_bases [ (unsigned char)rdbId ] = base;
}

RdbBase *CollectionRec::getBasePtr(rdbid_t rdbId) {
	if ( rdbId < 0 || rdbId >= RDB_END ) { g_process.shutdownAbort(true); }
	return m_bases [ (unsigned char)rdbId ];
}

// . returns NULL w/ g_errno set on error.
// . TODO: ensure not called from in thread, not thread safe
RdbBase *CollectionRec::getBase(rdbid_t rdbId) {
	return m_bases[(unsigned char)rdbId];
}


// . called only by addNewColl() and by addExistingColl()
bool Collectiondb::registerCollRec ( CollectionRec *cr ) {
	// add m_recs[] and to hashtable
	return setRecPtr ( cr->m_collnum , cr );
}

// swap it in
bool Collectiondb::addRdbBaseToAllRdbsForEachCollRec ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		CollectionRec *cr = m_recs[i];
		if ( ! cr ) continue;
		// add rdb base files etc. for it
		addRdbBasesForCollRec ( cr );
	}

	// now clean the trees. moved this into here from
	// addRdbBasesForCollRec() since we call addRdbBasesForCollRec()
	// now from getBase() to load on-demand for saving memory
	cleanTrees();

	return true;
}

bool Collectiondb::addRdbBasesForCollRec ( CollectionRec *cr ) {

	char *coll = cr->m_coll;

	//////
	//
	// if we are doing a dump from the command line, skip this stuff
	//
	//////
	if ( g_dumpMode ) return true;

	// tell rdbs to add one, too
	if ( ! g_posdb.getRdb()->addRdbBase1        ( coll ) ) goto hadError;
	if ( ! g_titledb.getRdb()->addRdbBase1      ( coll ) ) goto hadError;
	if ( ! g_tagdb.getRdb()->addRdbBase1        ( coll ) ) goto hadError;
	if ( ! g_clusterdb.getRdb()->addRdbBase1    ( coll ) ) goto hadError;
	if ( ! g_linkdb.getRdb()->addRdbBase1       ( coll ) ) goto hadError;
	if ( ! g_spiderdb.getRdb()->addRdbBase1     ( coll ) ) goto hadError;
	if ( ! g_doledb.getRdb()->addRdbBase1       ( coll ) ) goto hadError;

	// now clean the trees
	//cleanTrees();

	// debug message
	//log ( LOG_INFO, "db: verified collection \"%s\" (%" PRId32").",
	//      coll,(int32_t)cr->m_collnum);

	// tell SpiderCache about this collection, it will create a
	// SpiderCollection class for it.
	//g_spiderCache.reset1();

	// success
	return true;

 hadError:
	log(LOG_WARN, "db: error registering coll: %s",mstrerror(g_errno));
	return false;
}

/// this deletes the collection, not just part of a reset.
bool Collectiondb::deleteRec2 ( collnum_t collnum ) { //, WaitEntry *we ) {
	// do not allow this if in repair mode
	if ( g_repair.isRepairActive() && g_repair.m_collnum == collnum ) {
		log(LOG_WARN, "admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}
	// bitch if not found
	if ( collnum < 0 ) {
		g_errno = ENOTFOUND;
		log(LOG_LOGIC,"admin: Collection #%" PRId32" is bad, "
		    "delete failed.",(int32_t)collnum);
		return true;
	}
	CollectionRec *cr = m_recs [ collnum ];
	if ( ! cr ) {
		log(LOG_WARN, "admin: Collection id problem. Delete failed.");
		g_errno = ENOTFOUND;
		return true;
	}

	if ( g_process.isAnyTreeSaving() ) {
		// note it
		log("admin: tree is saving. waiting2.");
		// all done
		return false;
	}

	char *coll = cr->m_coll;

	// note it
	log(LOG_INFO,"db: deleting coll \"%s\" (%" PRId32")",coll,
	    (int32_t)cr->m_collnum);

	// we need a save
	m_needsSave = true;

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now

	// . TODO: remove from g_sync
	// . remove from all rdbs
	g_posdb.getRdb()->delColl    ( coll );

	g_titledb.getRdb()->delColl    ( coll );
	g_tagdb.getRdb()->delColl ( coll );
	g_spiderdb.getRdb()->delColl   ( coll );
	g_doledb.getRdb()->delColl     ( coll );
	g_clusterdb.getRdb()->delColl  ( coll );
	g_linkdb.getRdb()->delColl     ( coll );

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
	if ( sc ) {
		// remove locks from lock table:
		sc->clearLocks();

		// you have to set this for tryToDeleteSpiderColl to
		// actually have a shot at deleting it
		sc->m_deleteMyself = true;

		sc->setCollectionRec ( NULL );
		// this will put it on "death row" so it will be deleted
		// once Msg5::m_waitingForList/Merge is NULL
		tryToDeleteSpiderColl ( sc , "10" );

		// don't let cr reference us anymore, sc is on deathrow
		// and "cr" is delete below!
		cr->m_spiderColl = NULL;
	}

	//////
	//
	// remove from m_recs[]
	//
	//////
	setRecPtr ( cr->m_collnum , NULL );

	// free it
	mdelete ( cr, sizeof(CollectionRec),  "CollectionRec" );
	delete ( cr );

	// do not do this here in case spiders were outstanding
	// and they added a new coll right away and it ended up getting
	// recs from the deleted coll!!
	//while ( ! m_recs[m_numRecs-1] ) m_numRecs--;

	// update the time
	//updateTime();
	// done
	return true;
}

// ensure m_recs[] is big enough for m_recs[collnum] to be a ptr
bool Collectiondb::growRecPtrBuf ( collnum_t collnum ) {

	// an add, make sure big enough
	int32_t need = ((int32_t)collnum+1)*sizeof(CollectionRec *);
	int32_t have = m_recPtrBuf.length();
	int32_t need2 = need - have;

	// if already big enough
	if ( need2 <= 0 ) {
		m_recs [ collnum ] = NULL;
		return true;
	}

	m_recPtrBuf.setLabel ("crecptrb");

	// . true here means to clear the new space to zeroes
	// . this shit works based on m_length not m_capacity
	if ( ! m_recPtrBuf.reserve ( need2 ,NULL, true ) ) {
		log( LOG_WARN, "admin: error growing rec ptr buf2.");
		return false;
	}

	// sanity
	if ( m_recPtrBuf.getCapacity() < need ) { g_process.shutdownAbort(true); }

	// set it
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();

	// update length of used bytes in case we re-alloc
	m_recPtrBuf.setLength ( need );

	// re-max
	int32_t max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);
	// sanity
	if ( collnum >= max ) { g_process.shutdownAbort(true); }

	// initialize slot
	m_recs [ collnum ] = NULL;

	return true;
}


bool Collectiondb::setRecPtr ( collnum_t collnum , CollectionRec *cr ) {

	// first time init hashtable that maps coll to collnum
	if ( g_collTable.m_numSlots == 0 &&
	     ! g_collTable.set(8,sizeof(collnum_t), 256,NULL,0, false,"nhshtbl")) {
		return false;
	}

	// sanity
	if ( collnum < 0 ) { g_process.shutdownAbort(true); }

	// sanity
	int32_t max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);

	// set it
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();

	// tell spiders to re-upadted the active list
	g_spiderLoop.m_activeListValid = false;
	g_spiderLoop.m_activeListModified = true;

	// a delete?
	if ( ! cr ) {
		// sanity
		if ( collnum >= max ) { g_process.shutdownAbort(true); }
		// get what's there
		CollectionRec *oc = m_recs[collnum];
		// let it go
		m_recs[collnum] = NULL;
		// if nothing already, done
		if ( ! oc ) return true;
		// tally it up
		m_numRecsUsed--;
		// delete key
		int64_t h64 = hash64n(oc->m_coll);
		// if in the hashtable UNDER OUR COLLNUM then nuke it
		// otherwise, we might be called from resetColl2()
		void *vp = g_collTable.getValue ( &h64 );
		if ( ! vp ) return true;
		collnum_t ct = *(collnum_t *)vp;
		if ( ct != collnum ) return true;
		g_collTable.removeKey ( &h64 );
		return true;
	}

	// ensure m_recs[] is big enough for m_recs[collnum] to be a ptr
	if ( ! growRecPtrBuf ( collnum ) ) {
		return false;
	}

	// sanity
	if ( cr->m_collnum != collnum ) { g_process.shutdownAbort(true); }

	// add to hash table to map name to collnum_t
	int64_t h64 = hash64n(cr->m_coll);
	// debug
	//log("coll: adding key %" PRId64" for %s",h64,cr->m_coll);
	if ( ! g_collTable.addKey ( &h64 , &collnum ) )
		return false;

	// ensure last is NULL
	m_recs[collnum] = cr;

	// count it
	m_numRecsUsed++;

	//log("coll: adding key4 %" PRIu64" for coll \"%s\" (%" PRId32")",h64,cr->m_coll,
	//    (int32_t)i);

	// reserve it
	if ( collnum >= m_numRecs ) m_numRecs = collnum + 1;

	// sanity to make sure collectionrec ptrs are legit
	for ( int32_t j = 0 ; j < m_numRecs ; j++ ) {
		if ( ! m_recs[j] ) continue;
		if ( m_recs[j]->m_collnum == 1 ) continue;
	}

	return true;
}

// . returns false if we need a re-call, true if we completed
// . returns true with g_errno set on error
bool Collectiondb::resetColl2( collnum_t oldCollnum, collnum_t newCollnum, bool purgeSeeds ) {
	// do not allow this if in repair mode
	if ( g_repair.isRepairActive() && g_repair.m_collnum == oldCollnum ) {
		log(LOG_WARN, "admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}

	//log("admin: resetting collnum %" PRId32,(int32_t)oldCollnum);

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now
	if ( g_process.isAnyTreeSaving() ) {
		// we could not complete...
		return false;
	}

	CollectionRec *cr = m_recs [ oldCollnum ];

	// let's reset crawlinfo crap
	cr->m_globalCrawlInfo.reset();
	cr->m_localCrawlInfo.reset();

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(oldCollnum);
	if ( sc ) {
		// remove locks from lock table:
		sc->clearLocks();

		// this will put it on "death row" so it will be deleted
		// once Msg5::m_waitingForList/Merge is NULL
		tryToDeleteSpiderColl ( sc, "11" );

		cr->m_spiderColl = NULL;
	}

	// reset spider round
	cr->m_spiderRoundNum = 0;
	cr->m_spiderRoundStartTime = 0;

	cr->m_spiderStatus = SP_INITIALIZING; // this is 0
	//cr->m_spiderStatusMsg = NULL;

	// reset seed buf
	if ( purgeSeeds ) {
		// free the buffer of seed urls
		cr->m_diffbotSeeds.purge();
		// reset seed dedup table
		HashTableX *ht = &cr->m_seedHashTable;
		ht->reset();
	}

	// so XmlDoc.cpp can detect if the collection was reset since it
	// launched its spider:
	cr->m_lastResetCount++;


	if ( newCollnum >= m_numRecs ) m_numRecs = (int32_t)newCollnum + 1;

	// advance sanity check. did we wrap around?
	// right now we #define collnum_t int16_t
	if ( m_numRecs > 0x7fff ) { g_process.shutdownAbort(true); }

	// make a new collnum so records in transit will not be added
	// to any rdb...
	cr->m_collnum = newCollnum;

	// update the timestamps since we are restarting/resetting
	cr->m_diffbotCrawlStartTime = getTimeGlobalNoCore();
	cr->m_diffbotCrawlEndTime   = 0;


	////////
	//
	// ALTER m_recs[] array
	//
	////////

	// Rdb::resetColl() needs to know the new cr so it can move
	// the RdbBase into cr->m_bases[rdbId] array. recycling.
	setRecPtr ( newCollnum , cr );

	// a new directory then since we changed the collnum
	char dname[512];
	sprintf(dname, "%scoll.%s.%" PRId32"/",
		g_hostdb.m_dir,
		cr->m_coll,
		(int32_t)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir ) {
	     closedir ( dir );
		//g_errno = EEXIST;
		log(LOG_WARN, "admin: Trying to create collection %s but "
		    "directory %s already exists on disk.",cr->m_coll,dname);
	}
	if ( ::mkdir ( dname ,
		       getDirCreationFlags() ) ) {
		       // S_IRUSR | S_IWUSR | S_IXUSR |
		       // S_IRGRP | S_IWGRP | S_IXGRP |
		       // S_IROTH | S_IXOTH ) ) {
		// valgrind?
		//if ( errno == EINTR ) goto retry22;
		//g_errno = errno;
		log(LOG_WARN, "admin: Creating directory %s had error: "
		    "%s.", dname,mstrerror(g_errno));
	}

	// . unlink all the *.dat and *.map files for this coll in its subdir
	// . remove all recs from this collnum from m_tree/m_buckets
	// . updates RdbBase::m_collnum
	// . so for the tree it just needs to mark the old collnum recs
	//   with a collnum -1 in case it is saving...
	g_posdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_titledb.getRdb()->deleteColl   ( oldCollnum , newCollnum );
	g_tagdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_spiderdb.getRdb()->deleteColl  ( oldCollnum , newCollnum );
	g_doledb.getRdb()->deleteColl    ( oldCollnum , newCollnum );
	g_clusterdb.getRdb()->deleteColl ( oldCollnum , newCollnum );
	g_linkdb.getRdb()->deleteColl    ( oldCollnum , newCollnum );

	// reset crawl status too!
	cr->m_spiderStatus = SP_INITIALIZING;

	// . set m_recs[oldCollnum] to NULL and remove from hash table
	// . do after calls to deleteColl() above so it wont crash
	setRecPtr ( oldCollnum , NULL );


	// save coll.conf to new directory
	cr->save();

	// and clear the robots.txt cache in case we recently spidered a
	// robots.txt, we don't want to use it, we want to use the one we
	// have in the test-parser subdir so we are consistent
	//RdbCache *robots = Msg13::getHttpCacheRobots();
	//RdbCache *others = Msg13::getHttpCacheOthers();
	// clear() was removed do to possible corruption
	//robots->clear ( oldCollnum );
	//others->clear ( oldCollnum );

	//g_templateTable.reset();
	//g_templateTable.save( g_hostdb.m_dir , "turkedtemplates.dat" );

	// repopulate CollectionRec::m_sortByDateTable. should be empty
	// since we are resetting here.
	//initSortByDateTable ( coll );

	// done
	return true;
}

// a hack function
bool addCollToTable ( const char *coll , collnum_t collnum ) {
	// readd it to the hashtable that maps name to collnum too
	int64_t h64 = hash64n(coll);
	g_collTable.set(8,sizeof(collnum_t), 256,NULL,0, false,"nhshtbl");
	return g_collTable.addKey ( &h64 , &collnum );
}

// get coll rec specified in the HTTP request
CollectionRec *Collectiondb::getRec ( HttpRequest *r , bool useDefaultRec ) {
	const char *coll = r->getString ( "c" );
	if ( coll && ! coll[0] ) coll = NULL;
	// maybe it is crawlbot?
	const char *name = NULL;
	const char *token = NULL;
	if ( ! coll ) {
		name = r->getString("name");
		token = r->getString("token");
	}
	char tmp[MAX_COLL_LEN+1];
	if ( ! coll && token && name ) {
		snprintf(tmp,MAX_COLL_LEN,"%s-%s",token,name);
		coll = tmp;
	}

	// default to main first
	if ( ! coll && useDefaultRec ) {
		CollectionRec *cr = g_collectiondb.getRec("main");
		if ( cr ) return cr;

		return getFirstRec ();
	}

	// give up?
	if ( ! coll ) return NULL;
	//if ( ! coll || ! coll[0] ) coll = g_conf.m_defaultColl;
	return g_collectiondb.getRec ( coll );
}

const char *Collectiondb::getDefaultColl ( HttpRequest *r ) {
	const char *coll = r->getString ( "c" );
	if ( coll && ! coll[0] ) coll = NULL;
	if ( coll ) return coll;
	CollectionRec *cr = NULL;
	// default to main first
	if ( ! coll ) {
		cr = g_collectiondb.getRec("main");
		// CAUTION: cr could be deleted so don't trust this ptr
		// if you give up control of the cpu
		if ( cr ) return cr->m_coll;
	}
	// try next in line
	if ( ! coll ) {
		cr = getFirstRec ();
		if ( cr ) return cr->m_coll;
	}
	// give up?
	return NULL;
}


// . get collectionRec from name
// . returns NULL if not available
CollectionRec *Collectiondb::getRec ( const char *coll ) {
	if ( ! coll ) coll = "";
	return getRec ( coll , strlen(coll) );
}

CollectionRec *Collectiondb::getRec ( const char *coll , int32_t collLen ) {
	if ( ! coll ) coll = "";
	collnum_t collnum = getCollnum ( coll , collLen );
	if ( collnum < 0 ) return NULL;
	return m_recs [ (int32_t)collnum ];
}

CollectionRec *Collectiondb::getRec ( collnum_t collnum) {
	if ( collnum >= m_numRecs || collnum < 0 ) {
		// Rdb::resetBase() gets here, so don't always log.
		// it is called from CollectionRec::reset() which is called
		// from the CollectionRec constructor and ::load() so
		// it won't have anything in rdb at that time
		//log("colldb: collnum %" PRId32" > numrecs = %" PRId32,
		//    (int32_t)collnum,(int32_t)m_numRecs);
		return NULL;
	}
	return m_recs[collnum];
}


CollectionRec *Collectiondb::getFirstRec ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i];
	return NULL;
}

collnum_t Collectiondb::getFirstCollnum ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return i;
	return (collnum_t)-1;
}

char *Collectiondb::getFirstCollName ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i]->m_coll;
	return NULL;
}

char *Collectiondb::getCollName ( collnum_t collnum ) {
	if ( collnum < 0 || collnum > m_numRecs ) return NULL;
	if ( ! m_recs[(int32_t)collnum] ) return NULL;
	return m_recs[collnum]->m_coll;
}

collnum_t Collectiondb::getCollnum ( const char *coll ) {

	int32_t clen = 0;
	if ( coll ) clen = strlen(coll );
	return getCollnum ( coll , clen );
}

collnum_t Collectiondb::getCollnum ( const char *coll , int32_t clen ) {

	// default empty collection names
	if ( coll && ! coll[0] ) coll = NULL;
	if ( ! coll ) {
		coll = g_conf.m_defaultColl;
		clen = strlen(coll);
	}
	if ( ! coll[0] ) {
		coll = "main";
		clen = strlen(coll);
	}

	// not associated with any collection
	if ( coll[0]=='s' && coll[1] =='t' &&
	     strcmp ( "statsdb\0", coll ) == 0)
		return 0;

	// because diffbot may have thousands of crawls/collections
	// let's improve the speed here. try hashing it...
	int64_t h64 = hash64(coll,clen);
	void *vp = g_collTable.getValue ( &h64 );
	if ( ! vp ) return -1; // not found
	return *(collnum_t *)vp;
}


// what collnum will be used the next time a coll is added?
collnum_t Collectiondb::reserveCollNum ( ) {

	if ( m_numRecs < 0x7fff ) {
		collnum_t next = m_numRecs;
		// make the ptr NULL at least to accomodate the
		// loop that scan up to m_numRecs lest we core
		growRecPtrBuf ( next );
		m_numRecs++;
		return next;
	}

	// collnum_t is signed right now because we use -1 to indicate a
	// bad collnum.
	int32_t scanned = 0;
	// search for an empty slot
	for ( int32_t i = m_wrapped ; ; i++ ) {
		// because collnum_t is 2 bytes, signed, limit this here
		if ( i > 0x7fff ) i = 0;
		// how can this happen?
		if ( i < 0      ) i = 0;
		// if we scanned the max # of recs we could have, we are done
		if ( ++scanned >= m_numRecs ) break;
		// skip if this is in use
		if ( m_recs[i] ) continue;
		// start after this one next time
		m_wrapped = i+1;
		// note it
		log("colldb: returning wrapped collnum "
		    "of %" PRId32,(int32_t)i);
		return (collnum_t)i;
	}

	log("colldb: no new collnum available. consider upping collnum_t");
	// none available!!
	return -1;
}


///////////////
//
// COLLECTIONREC
//
///////////////

#include "gb-include.h"

//#include "CollectionRec.h"
//#include "Collectiondb.h"
#include "HttpServer.h"     // printColors2()
#include "Msg5.h"
#include "Spider.h"
#include "Process.h"
#include "Domains.h"


CollectionRec::CollectionRec() {
	m_spiderCorruptCount = 0;
	m_collnum = -1;
	m_coll[0] = '\0';
	m_updateRoundNum = 0;
	memset(&m_bases, 0, sizeof(m_bases));
	// how many keys in the tree of each rdb? we now store this stuff
	// here and not in RdbTree.cpp because we no longer have a maximum
	// # of collection recs... MAX_COLLS. each is a 32-bit "int32_t" so
	// it is 4 * RDB_END...
	memset(&m_numNegKeysInTree, 0, sizeof(m_numNegKeysInTree));
	memset(&m_numPosKeysInTree, 0, sizeof(m_numPosKeysInTree));
	m_spiderColl = NULL;
	m_overflow  = 0x12345678;
	m_overflow2 = 0x12345678;
	// the spiders are currently uninhibited i guess
	m_spiderStatus = SP_INITIALIZING; // this is 0
	// inits for sortbydatetable
	m_msg5       = NULL;
	m_importState = NULL;
	// JAB - track which regex parsers have been initialized
	//log(LOG_DEBUG,"regex: %p initalizing empty parsers", m_pRegExParser);

	// clear these out so Parms::calcChecksum can work and so Parms.cpp doesn't work with uninitialized data
	//m_regExs: ctor() done
	clearUrlFilters();

	//m_requests = 0;
	//m_replies = 0;
	//m_doingCallbacks = false;

	m_lastResetCount = 0;

	// for diffbot caching the global spider stats
	reset();

	// Coverity
	m_nextActive = NULL;
	m_needsSave = false;
	m_urlFiltersHavePageCounts = false;
	m_collLen = 0;
	m_dailyMergeStarted = 0;
	m_dailyMergeTrigger = 0;
	memset(m_dailyMergeDOWList, 0, sizeof(m_dailyMergeDOWList));
	m_treeCount = 0;
	m_spideringEnabled = true;
	m_spiderDelayInMilliseconds = 0;
	m_isActive = false;
	m_spiderRoundStartTime = 0;
	m_spiderRoundNum = 0;
	m_makeImageThumbnails = 0;
	m_thumbnailMaxWidthHeight = 0;
	m_indexSpiderReplies = 0;
	m_indexBody = 0;
	m_outlinksRecycleFrequencyDays = 0.0;
	m_dedupingEnabled = 0;
	m_dupCheckWWW = 0;
	m_detectCustomErrorPages = 0;
	m_useSimplifiedRedirects = 0;
	m_useIfModifiedSince = 0;
	m_useTimeAxis = 0;
	m_buildVecFromCont = 0;
	m_maxPercentSimilarPublishDate = 0;
	m_useSimilarityPublishDate = 0;
	m_oneVotePerIpDom = 0;
	m_doUrlSpamCheck = 0;
	m_doLinkSpamCheck = 0;
	memset(m_tagdbColl, 0, sizeof(m_tagdbColl));
	m_delete404s = 0;
	m_siteClusterByDefault = 0;
	m_doIpLookups = 0;
	m_useRobotsTxt = true;
	m_obeyRelNoFollowLinks = 0;
	m_forceUseFloaters = 0;
	m_automaticallyUseProxies = 0;
	m_automaticallyBackOff = 0;
	m_recycleContent = 0;
	m_getLinkInfo = 0;
	m_computeSiteNumInlinks = 0;
	m_indexInlinkNeighborhoods = 0;
	m_removeBannedPages = 0;
	m_percentSimilarSummary = 0;
	m_summDedupNumLines = 0;
	m_maxQueryTerms = 0;
	m_sameLangWeight = 0.0;
	memset(m_defaultSortLanguage2, 0, sizeof(m_defaultSortLanguage2));
	m_importEnabled = 0;
	m_numImportInjects = 0;
	m_posdbMinFilesToMerge = 0;
	m_titledbMinFilesToMerge = 0;
	m_linkdbMinFilesToMerge = 0;
	m_tagdbMinFilesToMerge = 0;
	m_spiderdbMinFilesToMerge = 0;
	m_dedupResultsByDefault = 0;
	m_doTagdbLookups = 0;
	m_deleteTimeouts = 0;
	m_allowAdultDocs = 0;
	m_useCanonicalRedirects = 0;
	m_maxNumSpiders = 0;
	m_useCurrentTime = 0;
	m_titleMaxLen = 0;
	m_summaryMaxLen = 0;
	m_summaryMaxNumLines = 0;
	m_summaryMaxNumCharsPerLine = 0;
	m_getDocIdScoringInfo = false;
	m_diffbotCrawlStartTime = 0;
	m_diffbotCrawlEndTime = 0;
	m_numRegExs9 = 0;
	m_doQueryHighlighting = 0;
	memset(m_summaryFrontHighlightTag, 0, sizeof(m_summaryFrontHighlightTag));
	memset(m_summaryBackHighlightTag, 0, sizeof(m_summaryBackHighlightTag));
	m_spellCheck = false;
	m_spiderTimeMin = 0;
	m_spiderTimeMax = 0;
	m_maxAddUrlsPerIpDomPerDay = 0;
	m_maxTextDocLen = 0;
	m_maxOtherDocLen = 0;
	m_summaryMaxWidth = 0;
	m_maxRobotsCacheAge = 0;
	m_queryExpansion = false;
	m_rcache = false;
	m_hideAllClustered = false;
	m_END_COPY = 0;
	m_hackFlag = 0;
}

CollectionRec::~CollectionRec() {
	//invalidateRegEx ();
        reset();
}


void CollectionRec::clearUrlFilters()
{
	memset( m_spiderFreqs, 0, sizeof(m_spiderFreqs) );
	memset( m_spiderPriorities, 0, sizeof(m_spiderPriorities) );
	memset( m_maxSpidersPerRule, 0, sizeof(m_maxSpidersPerRule) );
	memset( m_spiderIpWaits, 0, sizeof(m_spiderIpWaits) );
	memset( m_spiderIpMaxSpiders, 0, sizeof(m_spiderIpMaxSpiders) );
	memset( m_harvestLinks, 0, sizeof(m_harvestLinks) );
	memset( m_forceDelete, 0, sizeof(m_forceDelete) );

	m_numRegExs				= 0;
	m_numSpiderFreqs		= 0;
	m_numSpiderPriorities	= 0;
	m_numMaxSpidersPerRule	= 0;
	m_numSpiderIpWaits		= 0;
	m_numSpiderIpMaxSpiders	= 0;
	m_numHarvestLinks		= 0;
	m_numForceDelete		= 0;
}


void CollectionRec::reset() {

	//log("coll: resetting collnum=%" PRId32,(int32_t)m_collnum);

	// . grows dynamically
	// . setting to 0 buckets should never have error
	//m_pageCountTable.set ( 4,4,0,NULL,0,false,MAX_NICENESS,"pctbl" );

	// make sure we do not leave spiders "hanging" waiting for their
	// callback to be called... and it never gets called
	//if ( m_callbackQueue.length() > 0 ) { g_process.shutdownAbort(true); }
	//if ( m_doingCallbacks ) { g_process.shutdownAbort(true); }
	//if ( m_replies != m_requests  ) { g_process.shutdownAbort(true); }
	m_localCrawlInfo.reset();
	m_globalCrawlInfo.reset();
	//m_requests = 0;
	//m_replies = 0;
	// free all RdbBases in each rdb
	for ( int32_t i = 0 ; i < g_process.m_numRdbs ; i++ ) {
	     Rdb *rdb = g_process.m_rdbs[i];
	     rdb->resetBase ( m_collnum );
	}

	for ( int32_t i = 0 ; i < g_process.m_numRdbs ; i++ ) {
		RdbBase *base = m_bases[i];
		if ( ! base ) continue;
		mdelete (base, sizeof(RdbBase), "Rdb Coll");
		delete  (base);
	}

	SpiderColl *sc = m_spiderColl;
	// debug hack thing
	//if ( sc == (SpiderColl *)0x8888 ) return;
	// if never made one, we are done
	if ( ! sc ) return;

	// spider coll also!
	sc->m_deleteMyself = true;

	// if not currently being accessed nuke it now
	tryToDeleteSpiderColl ( sc , "12" );
}

// . load this data from a conf file
// . values we do not explicitly have will be taken from "default",
//   collection config file. if it does not have them then we use
//   the value we received from call to setToDefaults()
// . returns false and sets g_errno on load error
bool CollectionRec::load ( const char *coll , int32_t i ) {
	// also reset some counts not included in parms list
	reset();
	// before we load, set to defaults in case some are not in xml file
	g_parms.setToDefault ( (char *)this , OBJ_COLL , this );
	// get the filename with that id
	File f;
	char tmp2[1024];
	sprintf ( tmp2 , "%scoll.%s.%" PRId32"/coll.conf", g_hostdb.m_dir , coll,i);
	f.set ( tmp2 );
	if ( ! f.doesExist () ) {
		log( LOG_WARN, "admin: %s does not exist.",tmp2);
		return false;
	}
	// set our collection number
	m_collnum = i;
	// set our collection name
	m_collLen = strlen ( coll );
	if ( coll != m_coll)
		strcpy ( m_coll , coll );

	if ( ! g_conf.m_doingCommandLine )
		log(LOG_INFO,"db: Loading conf for collection %s (%" PRId32")",coll,
		    (int32_t)m_collnum);

	// the default conf file
	char tmp1[1024];
	snprintf ( tmp1 , 1023, "%sdefault.conf" , g_hostdb.m_dir );

	// . set our parms from the file.
	// . accepts OBJ_COLLECTIONREC or OBJ_CONF
	g_parms.setFromFile ( this , tmp2 , tmp1 , OBJ_COLL );

	// this only rebuild them if necessary
	rebuildUrlFilters();//setUrlFiltersToDefaults();

	//
	// LOAD the crawlinfo class in the collectionrec for diffbot
	//
	// LOAD LOCAL
	snprintf ( tmp1 , 1023, "%scoll.%s.%" PRId32"/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	log(LOG_DEBUG,"db: Loading %s",tmp1);
	m_localCrawlInfo.reset();
	SafeBuf sb;
	// fillfromfile returns 0 if does not exist, -1 on read error
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_localCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		gbmemcpy ( &m_localCrawlInfo , sb.getBufStart(),sb.length() );


	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log(LOG_INFO, "coll: Loaded %s (%" PRId32") local hasurlsready=%" PRId32,
		    m_coll,
		    (int32_t)m_collnum,
		    (int32_t)m_localCrawlInfo.m_hasUrlsReadyToSpider);


	// we introduced the this round counts, so don't start them at 0!!
	if ( m_spiderRoundNum == 0 &&
	     m_localCrawlInfo.m_pageDownloadSuccessesThisRound <
	     m_localCrawlInfo.m_pageDownloadSuccesses ) {
		log(LOG_WARN, "coll: fixing process count this round for %s",m_coll);
		m_localCrawlInfo.m_pageDownloadSuccessesThisRound =
			m_localCrawlInfo.m_pageDownloadSuccesses;
	}

	// we introduced the this round counts, so don't start them at 0!!
	if ( m_spiderRoundNum == 0 &&
	     m_localCrawlInfo.m_pageProcessSuccessesThisRound <
	     m_localCrawlInfo.m_pageProcessSuccesses ) {
		log(LOG_WARN, "coll: fixing process count this round for %s",m_coll);
		m_localCrawlInfo.m_pageProcessSuccessesThisRound =
			m_localCrawlInfo.m_pageProcessSuccesses;
	}

	// LOAD GLOBAL
	snprintf ( tmp1 , 1023, "%scoll.%s.%" PRId32"/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	log(LOG_DEBUG,"db: Loading %s",tmp1);
	m_globalCrawlInfo.reset();
	sb.reset();
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_globalCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		gbmemcpy ( &m_globalCrawlInfo , sb.getBufStart(),sb.length() );

	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log(LOG_INFO, "coll: Loaded %s (%" PRId32") global hasurlsready=%" PRId32,
		    m_coll,
		    (int32_t)m_collnum,
		    (int32_t)m_globalCrawlInfo.m_hasUrlsReadyToSpider);

	// the list of ip addresses that we have detected as being throttled
	// and therefore backoff and use proxies for
	if ( ! g_conf.m_doingCommandLine ) {
		sb.reset();
		sb.safePrintf("%scoll.%s.%" PRId32"/",
			      g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
		m_twitchyTable.m_allocName = "twittbl";
		m_twitchyTable.load ( sb.getBufStart() , "ipstouseproxiesfor.dat" );
	}

	// ignore errors i guess
	g_errno = 0;

	return true;
}


bool CollectionRec::rebuildUrlFilters2 ( ) {

	// tell spider loop to update active list
	g_spiderLoop.m_activeListValid = false;

	const char *s = m_urlFiltersProfile.getBufStart();

	// leave custom profiles alone
	if ( strcmp(s,"custom" ) == 0 ) {
		return true;
	}


	// Bugfix. Make sure all arrays are cleared so e.g. ForceDelete 
	// entries are not 'inherited' by new filter rules because they
	// are not set for each filter.
	clearUrlFilters();


	if ( !strcmp(s,"privacore" ) ) {
		return rebuildPrivacoreRules();
	}

	if ( !strcmp(s,"shallow" ) ) {
		return rebuildShallowRules();
	}

	//if ( strcmp(s,"web") )
	// just fall through for that


	if ( !strcmp(s,"english") )
		return rebuildLangRules( "en","com,us,gov");

	if ( !strcmp(s,"german") )
		return rebuildLangRules( "de","de");

	if ( !strcmp(s,"french") )
		return rebuildLangRules( "fr","fr");

	if ( !strcmp(s,"norwegian") )
		return rebuildLangRules( "nl","nl");

	if ( !strcmp(s,"spanish") )
		return rebuildLangRules( "es","es");

	if ( !strcmp(s,"romantic") )
		return rebuildLangRules("en,de,fr,nl,es,sv,no,it,fi,pt", "de,fr,nl,es,sv,no,it,fi,pt,com,gov,org" );

	int32_t n = 0;

	/*
	m_regExs[n].set("default");
	m_regExs[n].nullTerm();
	m_spiderFreqs     [n] = 30; // 30 days default
	m_spiderPriorities[n] = 0;
	m_maxSpidersPerRule[n] = 99;
	m_spiderIpWaits[n] = 1000;
	m_spiderIpMaxSpiders[n] = 7;
	m_harvestLinks[n] = true;
	*/

	// max spiders per ip
	int32_t ipms = 7;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// a non temporary error, like a 404? retry once per 5 days
	m_regExs[n].set("errorcount>=1");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 5; // 5 day retry
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 2;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// 20+ unique c block parent request urls means it is important!
	m_regExs[n].set("numinlinks>7 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 52;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// 20+ unique c block parent request urls means it is important!
	m_regExs[n].set("numinlinks>7");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 51;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;



	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .04166; // 60 minutes
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .04166; // 60 minutes
	n++;

	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	// do not harvest links if we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_spiderFreqs  [n] = 5.0;
		m_harvestLinks [n] = false;
	}
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	// do not harvest links if we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_spiderFreqs  [n] = 5.0;
		m_harvestLinks [n] = false;
	}
	n++;

	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 20;
	// turn off spidering if hopcount is too big and we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = false;
	}
	else {
		n++;
	}

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 19;
	// turn off spidering if hopcount is too big and we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = false;
	}
	else {
		n++;
	}

	m_regExs[n].set("default");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = false;
	}
	n++;


	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	return true;
}



bool CollectionRec::rebuildPrivacoreRules () {
	const char *langWhitelistStr = "xx,en,bg,sr,ca,cs,da,et,fi,fr,de,el,hu,is,ga,it,lv,lt,lb,nl,pl,pt,ro,es,sv,no,vv";

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("lang!=%s", langWhitelistStr);
	m_harvestLinks       [n] = false;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("tld==%s", getPrivacoreBlacklistedTLD());
	m_harvestLinks       [n] = false;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	// 3 or more non-temporary errors - delete it
	m_regExs[n].set("errorcount>=3 && !hastmperror");
	m_harvestLinks       [n] = false;
	m_spiderFreqs        [n] = 0; 		// 1 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	// 3 or more temporary errors - slow down retries a bit
	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 3; 		// 1 days default
	m_maxSpidersPerRule  [n] = 1; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 45;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	// 1 or more temporary errors - retry in a day
	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; 		// 1 days default
	m_maxSpidersPerRule  [n] = 1; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 45;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; 		// 7 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 85;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; 		// 7 days default
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 50;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; 	// 7 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 48;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;		// 7 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 18;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;	// 10 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 17;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;	// 20 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 16;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;	// 20 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 15;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;		// 40 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 14;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;		// 40 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 13;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 12;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 11;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;

	m_regExs[n].set("default");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 1;
	m_forceDelete        [n] = 0;		// Do NOT delete
	n++;


	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	return true;
}




bool CollectionRec::rebuildLangRules ( const char *langStr , const char *tldStr ) {

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && isnew && tld==%s",
			       tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && isnew && "
			       "lang==%s,xx"
			       ,langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	// m_regExs[n].set("hopcount==0 && iswww && isnew");
	// m_harvestLinks       [n] = true;
	// m_spiderFreqs        [n] = 7; // 30 days default
	// m_maxSpidersPerRule  [n] = 9; // max spiders
	// m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	// m_spiderIpWaits      [n] = 1000; // same ip wait
	// m_spiderPriorities   [n] = 20;
	// n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && lang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 19;
	n++;





	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && isnew && lang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 18;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && lang==%s,xx",langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 17;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && isnew && lang==%s,xx",
			       tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;

	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 16;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && lang==%s,xx",langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 15;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && isnew && lang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 14;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && lang==%s,xx",langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 13;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && isnew && lang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 12;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && tld==%s",tldStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && lang==%s,xx",langStr);
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 11;
	n++;



	m_regExs[n].set("default");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	n++;

	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	// done rebuilding CHINESE rules
	return true;
}

bool CollectionRec::rebuildShallowRules ( ) {

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	n++;




	//
	// stop if hopcount>=2 for things tagged shallow in sitelist
	//
	m_regExs[n].set("tag:shallow && hopcount>=2");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 0; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;


	// if # of pages in this site indexed is >= 10 then stop as well...
	m_regExs[n].set("tag:shallow && sitepages>=10");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 0; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;




	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;




	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;




	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;





	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;


	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;




	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;




	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;



	m_regExs[n].set("default");
	m_harvestLinks       [n] = true;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	n++;

	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	// done rebuilding SHALLOW rules
	return true;
}

// returns false on failure and sets g_errno, true otherwise
bool CollectionRec::save ( ) {
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	//File f;
	char tmp[1024];
	//sprintf ( tmp , "%scollections/%" PRId32".%s/c.conf",
	//	  g_hostdb.m_dir,m_id,m_coll);
	// collection name HACK for backwards compatibility
	//if ( m_collLen == 0 )
	//	sprintf ( tmp , "%scoll.main/coll.conf", g_hostdb.m_dir);
	//else
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/coll.conf",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	if ( ! g_parms.saveToXml ( (char *)this , tmp ,OBJ_COLL)) {
		return false;
	}
	// log msg
	//log (LOG_INFO,"db: Saved %s.",tmp);//f.getFilename());

	//
	// save the crawlinfo class in the collectionrec for diffbot
	//
	// SAVE LOCAL
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	//log("coll: saving %s",tmp);
	// in case emergency save from malloc core, do not alloc
	char stack[1024];
	SafeBuf sb(stack,1024);
	//m_localCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_localCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.safeSave ( tmp ) == -1 ) {
		log(LOG_WARN, "db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	// SAVE GLOBAL
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	//log("coll: saving %s",tmp);
	sb.reset();
	//m_globalCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_globalCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.safeSave ( tmp ) == -1 ) {
		log(LOG_WARN, "db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}

	// the list of ip addresses that we have detected as being throttled
	// and therefore backoff and use proxies for
	sb.reset();
	sb.safePrintf("%scoll.%s.%" PRId32"/",
		      g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	m_twitchyTable.save ( sb.getBufStart() , "ipstouseproxiesfor.dat" );

	// do not need a save now
	m_needsSave = false;

	return true;
}

void nukeDoledb ( collnum_t collnum );

// . anytime the url filters are updated, this function is called
// . it is also called on load of the collection at startup
bool CollectionRec::rebuildUrlFilters ( ) {

	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log(LOG_INFO, "coll: Rebuilding url filters for %s ufp=%s",m_coll,
		    m_urlFiltersProfile.getBufStart());

	// set the url filters based on the url filter profile, if any
	rebuildUrlFilters2();

	// set this so we know whether we have to keep track of page counts
	// per subdomain/site and per domain. if the url filters have
	// 'sitepages' 'domainpages' 'domainadds' or 'siteadds' we have to keep
	// the count table SpiderColl::m_pageCountTable.
	m_urlFiltersHavePageCounts = false;
	for ( int32_t i = 0 ; i < m_numRegExs ; i++ ) {
		// get the ith rule
		SafeBuf *sb = &m_regExs[i];
		char *p = sb->getBufStart();
		if ( strstr(p,"sitepages") ||
		     strstr(p,"domainpages") ||
		     strstr(p,"siteadds") ||
		     strstr(p,"domainadds") ) {
			m_urlFiltersHavePageCounts = true;
			break;
		}
	}

	// if collection is brand new being called from addNewColl()
	// then sc will be NULL
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(m_collnum);

	// . do not do this at startup
	// . this essentially resets doledb
	if ( g_doledb.m_rdb.isInitialized() &&
	     // somehow this is initialized before we set m_recs[m_collnum]
	     // so we gotta do the two checks below...
	     sc &&
	     // must be a valid coll
	     m_collnum < g_collectiondb.m_numRecs &&
	     g_collectiondb.m_recs[m_collnum] ) {


		log(LOG_INFO, "coll: resetting doledb for %s (%li)",m_coll, (long)m_collnum);

		// clear doledb recs from tree
		//g_doledb.getRdb()->deleteAllRecs ( m_collnum );
		nukeDoledb ( m_collnum );

		// add it back
		//if ( ! g_doledb.getRdb()->addRdbBase2 ( m_collnum ) )
		//	log("coll: error re-adding doledb for %s",m_coll);

		// just start this over...
		// . MDW left off here
		//tryToDelete ( sc );
		// maybe this is good enough
		//if ( sc ) sc->m_waitingTreeNeedsRebuild = true;

		//CollectionRec *cr = sc->m_cr;

		// . rebuild sitetable? in PageBasic.cpp.
		// . re-adds seed spdierrequests using msg4
		// . true = addSeeds
		// . no, don't do this now because we call updateSiteList()
		//   when we have &sitelist=xxxx in the request which will
		//   handle updating those tables
		//updateSiteListTables ( m_collnum ,
		//		       true ,
		//		       cr->m_siteListBuf.getBufStart() );
	}

	return true;
}

int64_t CollectionRec::getNumDocsIndexed() {
	RdbBase *base = getBase(RDB_TITLEDB);//m_bases[RDB_TITLEDB];
	if ( ! base ) return 0LL;
	return base->getNumGlobalRecs();
}

// messes with m_spiderColl->m_sendLocalCrawlInfoToHost[MAX_HOSTS]
// so we do not have to keep sending this huge msg!
bool CollectionRec::shouldSendLocalCrawlInfoToHost ( int32_t hostId ) {
	if ( ! m_spiderColl ) return false;
	if ( hostId < 0 ) { g_process.shutdownAbort(true); }
	if ( hostId >= g_hostdb.getNumHosts() ) { g_process.shutdownAbort(true); }
	// sanity
	return m_spiderColl->m_sendLocalCrawlInfoToHost[hostId];
}

void CollectionRec::localCrawlInfoUpdate() {
	if ( ! m_spiderColl ) return;
	// turn on all the flags
	memset(m_spiderColl->m_sendLocalCrawlInfoToHost,1,g_hostdb.getNumHosts());
}

// right after we send copy it for sending we set this so we do not send
// again unless localCrawlInfoUpdate() is called
void CollectionRec::sentLocalCrawlInfoToHost ( int32_t hostId ) {
	if ( ! m_spiderColl ) return;
	m_spiderColl->m_sendLocalCrawlInfoToHost[hostId] = 0;
}
