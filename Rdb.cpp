#include "gb-include.h"

#include "Rdb.h"
#include "Clusterdb.h"
#include "Hostdb.h"
#include "Tagdb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Spider.h"
#include "Spider.h"
#include "Repair.h"
#include "Process.h"
#include "Statsdb.h"
#include "Sections.h"
#include "Spider.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Linkdb.h"
#include "hash.h"
#include "Stats.h"
#include "GbMoveFile.h"
#include "max_niceness.h"
#include <sys/stat.h> //mdir()

Rdb::Rdb ( ) {

	m_lastReclaim = -1;

	m_cacheLastTime  = 0;
	m_cacheLastTotal = 0LL;

	//m_numBases = 0;
	m_collectionlessBase = NULL;
	m_initialized = false;
	m_numMergesOut = 0;

	// Coverity
	m_fixedDataSize = 0;
	m_dbnameLen = 0;
	m_isCollectionLess = false;
	m_useIndexFile = false;
	m_useTree = false;
	m_closeState = NULL;
	m_closeCallback = NULL;
	m_maxTreeMem = 0;
	m_minToMerge = 0;
	m_dumpErrno = 0;
	m_useHalfKeys = false;
	m_urgent = false;
	m_niceness = false;
	m_fn = 0;
	m_dumpCollnum = 0;
	m_inDumpLoop = false;
	m_rdbId = RDB_NONE;
	m_ks = 0;
	m_pageSize = 0;
	// PVS-Studio
	m_isTitledb = false;
	memset(m_dbname, 0, sizeof(m_dbname));
	memset(m_treeAllocName, 0, sizeof(m_treeAllocName));
	memset(m_memAllocName, 0, sizeof(m_memAllocName));

	reset();
}

void Rdb::reset ( ) {
	if (m_collectionlessBase) {
		RdbBase *base = m_collectionlessBase;
		mdelete (base, sizeof(RdbBase), "Rdb Coll");
		delete  (base);
		m_collectionlessBase = NULL;
	}
	// reset tree and cache
	m_tree.reset();
	m_buckets.reset();
	m_mem.reset();
	//m_cache.reset();
	m_lastWrite = 0LL;
	m_isClosing = false;
	m_isClosed  = false;
	m_isSaving  = false;
	m_isReallyClosing = false;
	m_registered      = false;
	m_lastTime        = 0LL;
}

Rdb::~Rdb ( ) {
	reset();
}

RdbBase *Rdb::getBase ( collnum_t collnum )  {
	if ( m_isCollectionLess ) 
		return m_collectionlessBase;
	// RdbBase for statsdb, etc. resides in collrec #0 i guess
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return NULL;
	// this might load the rdbbase on demand now
	return cr->getBase ( m_rdbId ); // m_bases[(unsigned char)m_rdbId];
}

// used by Rdb::addBase1()
void Rdb::addBase ( collnum_t collnum , RdbBase *base ) {
	// if we are collectionless, like g_statsdb.m_rdb etc.. shared by all collections essentially.
	if ( m_isCollectionLess ) {
		m_collectionlessBase = base;
		return;
	}
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return;
	//if ( cr->m_bases[(unsigned char)m_rdbId] ) { g_process.shutdownAbort(true); }
	RdbBase *oldBase = cr->getBasePtr ( m_rdbId );
	if ( oldBase ) { g_process.shutdownAbort(true); }
	cr->setBasePtr ( m_rdbId , base );
	log ( LOG_DEBUG,"db: added base to collrec "
	    "for rdb=%s rdbid=%" PRId32" coll=%s collnum=%" PRId32" "
	      "base=0x%" PTRFMT"",
	    m_dbname,(int32_t)m_rdbId,cr->m_coll,(int32_t)collnum,
	      (PTRTYPE)base);
}

bool Rdb::init(const char *dbname,
		  int32_t           fixedDataSize        ,
		  int32_t           minToMerge           ,
		  int32_t           maxTreeMem           ,
		  int32_t           maxTreeNodes         ,
		  bool           useHalfKeys          ,
		  char           keySize              ,
		 bool            isCollectionLess,
		 bool			useIndexFile ) {
	// reset all
	reset();

	// statsdb
	m_isCollectionLess = isCollectionLess;

	// save the dbname NULL terminated into m_dbname/m_dbnameLen
	m_dbnameLen = strlen ( dbname );
	gbmemcpy ( m_dbname , dbname , m_dbnameLen );
	m_dbname [ m_dbnameLen ] = '\0';

	// store the other parameters for initializing each Rdb
	m_fixedDataSize    = fixedDataSize;
	m_maxTreeMem       = maxTreeMem;
	m_useHalfKeys      = useHalfKeys;
	m_ks               = keySize;
	m_useIndexFile     = useIndexFile;
	m_inDumpLoop       = false;

	// set our id
	m_rdbId = getIdFromRdb(this);
	if (m_rdbId <= 0) {
		log( LOG_LOGIC, "db: dbname of %s is invalid.", dbname );
		return false;
	}

	// sanity check
	if (m_ks != getKeySizeFromRdbId(m_rdbId)) {
		g_process.shutdownAbort(true);
	}

	// get page size
	switch(m_rdbId) {
		case RDB_POSDB:
		case RDB2_POSDB2:
		case RDB_TITLEDB:
		case RDB2_TITLEDB2:
		case RDB_SPIDERDB:
		case RDB_DOLEDB:
		case RDB2_SPIDERDB2:
		case RDB_LINKDB:
		case RDB2_LINKDB2:
			m_pageSize = GB_INDEXDB_PAGE_SIZE;
			break;
		default:
			m_pageSize = GB_TFNDB_PAGE_SIZE;
	}

	// we can't merge more than MAX_RDB_FILES files at a time
	if ( minToMerge > MAX_RDB_FILES ) minToMerge = MAX_RDB_FILES;
	m_minToMerge = minToMerge;
		
	m_useTree = true;
	if ( m_rdbId == RDB_POSDB || m_rdbId == RDB2_POSDB2 ) {
		m_useTree = false;
	}

	if(m_useTree) { 
		// statsdb is collectionless really so pass on to tree
		int32_t rdbId = m_rdbId != RDB_STATSDB ? m_rdbId : -1;
		sprintf(m_treeAllocName,"tree-%s",m_dbname);
		if (!m_tree.set(fixedDataSize, maxTreeNodes, maxTreeMem, false, m_treeAllocName, false, m_dbname, m_ks, rdbId)) {
			log( LOG_ERROR, "db: Failed to set tree." );
			return false;
		}
	} else {
		sprintf(m_treeAllocName,"buckets-%s",m_dbname);
		if (!m_buckets.set(fixedDataSize, maxTreeMem, m_treeAllocName, m_rdbId, m_dbname, m_ks)) {
			log( LOG_ERROR, "db: Failed to set buckets." );
			return false;
		}
	}

	// now get how much mem the tree is using (not including stored recs)
	int32_t dataMem;
	if (m_useTree) dataMem = maxTreeMem - m_tree.getTreeOverhead();
	else          dataMem = maxTreeMem - m_buckets.getMemOccupied( );

	sprintf(m_memAllocName,"mem-%s",m_dbname);

	if ( fixedDataSize != 0 && ! m_mem.init ( this, dataMem, m_memAllocName ) ) {
		log( LOG_ERROR, "db: Failed to initialize memory: %s.", mstrerror( g_errno ) );
		return false;
	}

	// load any saved tree
	if ( ! loadTree ( ) ) {
		log( LOG_ERROR, "db: Failed to load tree." );
		return false;
	}

	m_initialized = true;

	// success
	return true;
}

// . when the PageRepair.cpp rebuilds our rdb for a particular collection
//   we clear out the old data just for that collection and point to the newly
//   rebuilt data
// . rdb2 is the rebuilt/secondary rdb we want to set this primary rdb to
// . rename, for safe keeping purposes, current old files to :
//   trash/coll.mycoll.timestamp.indexdb0001.dat.part30 and
//   trash/timestamp.indexdb-saved.dat
// . rename newly rebuilt files from indexdbRebuild0001.dat.part30 to
//   indexdb0001.dat.part30 (just remove the "Rebuild" from the filename)
// . remove all recs for that coll from the tree AND cache because the rebuilt
//   rdb is replacing the primary rdb for this collection
// . the rebuilt secondary tree should be empty! (force dumped)
// . reload the maps/files in the primary rdb after we remove "Rebuild" from 
//   their filenames
// . returns false and sets g_errno on error
bool Rdb::updateToRebuildFiles ( Rdb *rdb2 , char *coll ) {
	// how come not in repair mode?
	if ( ! g_repairMode ) { g_process.shutdownAbort(true); }
	// make a dir in the trash subfolder to hold them
	uint32_t t = (uint32_t)getTime();
	char dstDir[256];
	// make the trash dir if not there
	snprintf(dstDir, sizeof(dstDir), "%s/trash/" , g_hostdb.m_dir );
	dstDir[ sizeof(dstDir)-1 ] = '\0';
	
	int32_t status = ::mkdir ( dstDir , getDirCreationFlags() );
	if ( status && errno != EEXIST ) {
		g_errno = errno;
		log(LOG_WARN, "repair: Could not mkdir(%s): %s",dstDir, mstrerror(errno));
		return false;
	}

	// we have to create it
	snprintf(dstDir, sizeof(dstDir), "%s/trash/rebuilt%" PRIu32"/" , g_hostdb.m_dir , t );
	dstDir[ sizeof(dstDir)-1 ] = '\0';
	
	status = ::mkdir ( dstDir , getDirCreationFlags() );
	if ( status && errno != EEXIST ) {
		g_errno = errno;
		log(LOG_WARN, "repair: Could not mkdir(%s): %s",dstDir, mstrerror(errno));
		return false;
	}

	// clear it in case it existed
	g_errno = 0;

	// if some things need to be saved, how did that happen?
	// we saved everything before we entered repair mode and did not
	// allow anything more to be added... and we do not allow any
	// collections to be deleted via Collectiondb::deleteRec() when
	// in repair mode... how could this happen?
	//if ( m_needsSave ) { g_process.shutdownAbort(true); }
	// delete old collection recs
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) {
		log(LOG_WARN, "db: Exchange could not find coll, %s.",coll);
		return false;
	}
	collnum_t collnum = cr->m_collnum;

	RdbBase *base = getBase ( collnum );
	if ( ! base ) {
		log(LOG_WARN, "repair: Could not find old base for %s.", coll);
		return false;
	}

	RdbBase *base2 = rdb2->getBase ( collnum );
	if ( ! base2 ) {
		log(LOG_WARN, "repair: Could not find new base for %s.", coll);
		return false;
	}

	if ( rdb2->getNumUsedNodes() != 0 ) {
		log(LOG_WARN, "repair: Recs present in rebuilt tree for db %s and collection %s.", m_dbname, coll);
		return false;
	}

	logf(LOG_INFO,"repair: Updating rdb %s for collection %s.",
	     m_dbname,coll);

	// now MOVE the tree file on disk
	char src[1024];
	char dst[1024];
	if(m_useTree) {
		sprintf ( src , "%s/%s-saved.dat" , g_hostdb.m_dir , m_dbname );
		sprintf ( dst , "%s/%s-saved.dat" ,         dstDir , m_dbname );
	}
	else {
		sprintf ( src , "%s/%s-buckets-saved.dat", g_hostdb.m_dir , m_dbname );
		sprintf ( dst , "%s/%s-buckets-saved.dat", dstDir , m_dbname );
	}

	const char *structName = m_useTree ? "tree" : "buckets";

	logf(LOG_INFO,"repair: Moving *-saved.dat %s from %s to %s", structName, src, dst);

	errno = 0;

	// ignore missing file error
	if (moveFile(src, dst) != 0 && errno != ENOENT) {
		log( LOG_ERROR, "repair: Moving saved %s had error: %s.", structName, mstrerror( errno ) );
		return false;
	}

	log("repair: Moving saved %s: %s",structName, mstrerror(errno));

	// now move our map and data files to the "trash" subdir, "dstDir"
	logf(LOG_INFO,"repair: Moving old data and map files to trash.");
	if ( ! base->moveToTrash(dstDir) ) {
		log(LOG_WARN, "repair: Trashing new rdb for %s failed.", coll);
		return false;
	}

	// . now rename the newly rebuilt files to our filenames
	// . just removes the "Rebuild" from their filenames
	logf(LOG_INFO,"repair: Renaming new data and map files.");
	if ( ! base2->removeRebuildFromFilenames() ) {
		log(LOG_WARN, "repair: Renaming old rdb for %s failed.", coll);
		return false;
	}

	// reset the rdb bases (clears out files and maps from mem)
	base->reset ();
	base2->reset();

	// reload the newly rebuilt files into the primary rdb
	logf(LOG_INFO,"repair: Loading new data and map files.");
	if ( ! base->setFiles() ) {
		log(LOG_WARN, "repair: Failed to set new files for %s.", coll);
		return false;
	}

	// allow rdb2->reset() to succeed without dumping core
	rdb2->m_tree.setNeedsSave(false);
	rdb2->m_buckets.setNeedsSave(false);
	
	// . make rdb2, the secondary rdb used for rebuilding, give up its mem
	// . if we do another rebuild its ::init() will be called by PageRepair
	rdb2->reset();

	// clean out tree, newly rebuilt rdb does not have any data in tree
	if ( m_useTree ) m_tree.delColl ( collnum );
	else             m_buckets.delColl( collnum );
	// reset our cache
	//m_cache.clear ( collnum );

	// Success
	return true;
}

// . returns false and sets g_errno on error, returns true on success
// . if this rdb is collectionless we set m_collectionlessBase in addBase()
bool Rdb::addRdbBase1 ( const char *coll ) {
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	return addRdbBase2 ( collnum );
}

bool Rdb::addRdbBase2 ( collnum_t collnum ) { // addColl2()

	if ( ! m_initialized ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "db: adding coll to uninitialized rdb!");
		return false;
	}

	// statsdb
	if ( m_isCollectionLess )
		collnum = (collnum_t)0;
	// ensure no max breech
	if ( collnum < (collnum_t) 0 ) {
		g_errno = ENOBUFS;
		int64_t maxColls = 1LL << (sizeof(collnum_t)*8);
		log(LOG_WARN, "db: %s: Failed to add collection #%i. Would breech maximum number of collections, %" PRId64".",
		    m_dbname,collnum,maxColls);
		return false;
	}


	CollectionRec *cr = NULL;
	const char *coll = NULL;
	if ( ! m_isCollectionLess ) cr = g_collectiondb.getRec(collnum);
	if ( cr ) coll = cr->m_coll;

	if ( m_isCollectionLess )
		coll = "collectionless";

	// . ensure no previous one exists
	// . well it will be there but will be uninitialized, m_rdb will b NULL
	RdbBase *base = NULL;
	if ( cr ) base = cr->getBasePtr ( m_rdbId );
	if ( base ) { // m_bases [ collnum ] ) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "db: Rdb for db \"%s\" and collection \"%s\" (collnum %" PRId32") exists.",
		    m_dbname,coll,(int32_t)collnum);
		return false;
	}
	// make a new one
	RdbBase *newColl = NULL;
	try {newColl= new(RdbBase);}
	catch(...){
		g_errno = ENOMEM;
		log(LOG_WARN, "db: %s: Failed to allocate %" PRId32" bytes for collection \"%s\".",
		    m_dbname,(int32_t)sizeof(Rdb),coll);
		return false;
	}
	mnew(newColl, sizeof(RdbBase), "Rdb Coll");
	//m_bases [ collnum ] = newColl;

	base = newColl;
	// add it to CollectionRec::m_bases[] base ptrs array
	addBase ( collnum , newColl );

	// . set CollectionRec::m_numPos/NegKeysInTree[rdbId]
	// . these counts are now stored in the CollectionRec and not
	//   in RdbTree since the # of collections can be huge!
	if ( m_useTree ) {
		m_tree.setNumKeys ( cr );
	}


	RdbTree    *tree = NULL;
	RdbBuckets *buckets = NULL;
	if(m_useTree) tree    = &m_tree;
	else          buckets = &m_buckets;

	// . init it
	// . g_hostdb.m_dir should end in /
	if ( ! base->init ( g_hostdb.m_dir,
					m_dbname        ,
					m_fixedDataSize ,
					m_minToMerge    ,
					m_useHalfKeys   ,
					m_ks            ,
					m_pageSize      ,
					coll            ,
					collnum         ,
					tree            ,
					buckets         ,
					&m_dump         ,
					this            ,
					m_useIndexFile ) ) {
		logf(LOG_INFO,"db: %s: Failed to initialize db for "
		     "collection \"%s\".", m_dbname,coll);
		//exit(-1);
		return false;
	}

	//if ( (int32_t)collnum >= m_numBases ) m_numBases = (int32_t)collnum + 1;
	// Success
	return true;
}

bool Rdb::resetBase ( collnum_t collnum ) {
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return true;
	// get the ptr, don't use CollectionRec::getBase() so we do not swapin
	RdbBase *base = cr->getBasePtr (m_rdbId);
	if ( ! base ) return true;
	base->reset();
	return true;
}

bool Rdb::deleteAllRecs ( collnum_t collnum ) {

	// remove from tree
	if(m_useTree) m_tree.delColl    ( collnum );
	else          m_buckets.delColl ( collnum );

	// only for doledb now, because we unlink we do not move the files
	// into the trash subdir and doledb is easily regenerated. i don't
	// want to take the risk with other files.
	if ( m_rdbId != RDB_DOLEDB ) { g_process.shutdownAbort(true); }

	CollectionRec *cr = g_collectiondb.getRec ( collnum );

	// deleted from under us?
	if ( ! cr ) {
		log("rdb: deleteallrecs: cr is NULL");
		return true;
	}

	//Rdbbase *base = cr->m_bases[(unsigned char)m_rdbId];
	RdbBase *base = cr->getBase(m_rdbId);
	if ( ! base ) return true;

	// scan files in there
	for ( int32_t i = 0 ; i < base->getNumFiles() ; i++ ) {
		BigFile *f = base->getFile(i);
		// move to trash
		char newdir[1024];
		sprintf(newdir, "%strash/",g_hostdb.m_dir);
		f->move ( newdir );
	}

	// nuke all the files
	base->reset();

	// reset rec counts
	cr->m_numNegKeysInTree[RDB_DOLEDB] = 0;
	cr->m_numPosKeysInTree[RDB_DOLEDB] = 0;

	return true;
}

bool makeTrashDir() {
	char trash[1024];
	sprintf(trash, "%strash/",g_hostdb.m_dir);
	if ( ::mkdir ( trash , getDirCreationFlags() ) ) {
		if ( errno != EEXIST ) {
			log("dir: mkdir %s had error: %s",
			    trash,mstrerror(errno));
			return false;
		}
		// clear it
		errno = 0;
	}
	return true;
}


bool Rdb::deleteColl( collnum_t collnum, collnum_t newCollnum) {
	// remove these collnums from tree
	if(m_useTree) {
		m_tree.delColl(collnum);
	}
	else {
		m_buckets.delColl(collnum);
	}

	// . close all files, set m_numFiles to 0 in RdbBase
	// . TODO: what about outstanding merge or dump operations?
	// . it seems like we can't really recycle this too easily 
	//   because reset it not resetting filenames or directory name?
	//   just nuke it and rebuild using addRdbBase2()...
	RdbBase *oldBase = getBase ( collnum );
	mdelete (oldBase, sizeof(RdbBase), "Rdb Coll");
	delete  (oldBase);

	// NULL it out...
	CollectionRec *oldcr = g_collectiondb.getRec(collnum);
	if( !oldcr ) {
		logError("could not get record for collection %d", (int)collnum);
		return false;
	}
	oldcr->setBasePtr ( m_rdbId , NULL );
	char *coll = oldcr->m_coll;

	const char *msg = "deleted";

	// if just resetting recycle base
	if (collnum != newCollnum) {
		addRdbBase2(newCollnum);
		msg = "moved";
	}

	
	log(LOG_DEBUG,"db: %s base from collrec "
	    "rdb=%s rdbid=%" PRId32" coll=%s collnum=%" PRId32" newcollnum=%" PRId32,
	    msg,m_dbname,(int32_t)m_rdbId,coll,(int32_t)collnum,
	    (int32_t)newCollnum);

	// move the files into trash
	// nuke it on disk
	char oldname[1024];
	sprintf(oldname, "%scoll.%s.%" PRId32"/",g_hostdb.m_dir,coll,
		(int32_t)collnum);
	char newname[1024];
	sprintf(newname, "%strash/coll.%s.%" PRId32".%" PRId64"/",g_hostdb.m_dir,coll,
		(int32_t)collnum,gettimeofdayInMilliseconds());
	// ensure ./trash dir is there
	makeTrashDir();
	// move into that dir
	if( ::rename ( oldname , newname ) == -1 ) {
		logError("Failed renaming [%s] to [%s]. errno %d: %s.", oldname, newname, errno, mstrerror(errno) );
	}

	log ( LOG_DEBUG, "db: cleared data for coll \"%s\" (%" PRId32") rdb=%s.",
	       coll,(int32_t)collnum ,getDbnameFromId(m_rdbId));

	return true;
}

// returns false and sets g_errno on error, returns true on success
bool Rdb::delColl(const char *coll) {
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	
	if( collnum < (collnum_t)0 ) {
		log(LOG_WARN, "Failed to delete collection. Could not look up collection [%s]", coll);
		return false;
	}
	RdbBase *base = getBase ( collnum );

	// ensure its there
	if(!base) {
		g_errno = EBADENGINEER;
		log(LOG_WARN, "db: %s: Failed to delete collection #%i. Does not exist.", m_dbname,collnum);
		return false;
	}

	// move all files to trash and clear the tree/buckets
	deleteColl(collnum, collnum);
	return true;
}

// . returns false if blocked true otherwise
// . sets g_errno on error
// . CAUTION: only set urgent to true if we got a SIGSEGV or SIGPWR...
bool Rdb::close ( void *state , void (* callback)(void *state ), bool urgent , bool isReallyClosing ) {
	// unregister in case already registered
	if ( m_registered )
		g_loop.unregisterSleepCallback (this,closeSleepWrapper);
	// reset g_errno
	g_errno = 0;
	// return true if no RdbBases in m_bases[] to close
	if ( getNumBases() <= 0 ) return true;
	// return true if already closed
	if ( m_isClosed ) return true;
	// don't call more than once
	if ( m_isSaving ) return true;
	// update last write time so main.cpp doesn't keep calling us
	m_lastWrite = gettimeofdayInMilliseconds();
	// set the m_isClosing flag in case we're waiting for a dump.
	// then, when the dump is done, it will come here again
	m_closeState       = state;
	m_closeCallback    = callback;
	m_urgent           = urgent;
	m_isReallyClosing = isReallyClosing;
        if ( m_isReallyClosing ) m_isClosing = true;
	// . don't call more than once
	// . really only for when isReallyClosing is false... just a quick save
	m_isSaving = true;
	// suspend any merge permanently (not just for this rdb), we're exiting
	if ( m_isReallyClosing ) {
		g_merge.haltMerge();
	}
	// . allow dumps to complete unless we're urgent
	// . if we're urgent, we'll end up with a half dumped file, which
	//   is ok now, since it should get its RdbMap auto-generated for it
	//   when we come back up again
	if ( ! m_urgent && m_inDumpLoop ) { // m_dump.isDumping() ) {
		m_isSaving = false;
		const char *tt = "save";
		if ( m_isReallyClosing ) tt = "close";
		log(LOG_INFO, "db: Cannot %s %s until dump finishes.", tt, m_dbname);
		return false;
	}


	// if a write thread is outstanding, and we exit now, we can end up
	// freeing the buffer it is writing and it will core... and things
	// won't be in sync with the map when it is saved below...
	if ( m_isReallyClosing &&
	     // if we cored, we are urgent and need to make sure we save even
	     // if we are merging this rdb...
	     ! m_urgent &&
	     g_merge.getRdbId() == m_rdbId &&
	     g_merge.isMerging() ) {
		// do not spam this message
		int64_t now = gettimeofdayInMilliseconds();
		if ( now - m_lastTime >= 500 ) {
			log(LOG_INFO,"db: Waiting for merge to finish last "
			    "write for %s.",m_dbname);
			m_lastTime = now;
		}
		g_loop.registerSleepCallback (500,this,closeSleepWrapper);
		m_registered = true;
		// allow to be called again
		m_isSaving = false;
		return false;
	}

	// if we were merging to a file and are being closed urgently
	// save the map! Also save the maps of the files we were merging
	// in case the got their heads chopped (RdbMap::chopHead()) which
	// we do to save disk space while merging.
	// try to save the cache, may not save
	if (m_isReallyClosing) {
		// now loop over bases
		for (int32_t i = 0; i < g_collectiondb.getNumRecs(); i++) {
			// shut it down
			RdbBase *base = getBase(i);
			if (base) {
				base->closeMaps(m_urgent);
				base->closeIndexes(m_urgent);
			}
		}
	}

	// save it using a thread?
	bool useThread = !(m_urgent || m_isReallyClosing);

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	if (m_useTree) {
		if (!m_tree.fastSave(getDir(), m_dbname, useThread, this, doneSavingWrapper)) {
			return false;
		}
	} else {
		if (!m_buckets.fastSave(getDir(), useThread, this, doneSavingWrapper)) {
			return false;
		}
	}

	// save index for tree as well
	for (int32_t i = 0; i < g_collectiondb.getNumRecs(); i++) {
		RdbBase *base = getBase(i);
		if (base) {
			base->saveTreeIndex();
		}
	}

	// we saved it w/o blocking OR we had an g_errno
	doneSaving();
	return true;
}

void Rdb::closeSleepWrapper ( int fd , void *state ) {
	Rdb *THIS = (Rdb *)state;
	// sanity check
	if ( ! THIS->m_isClosing ) { g_process.shutdownAbort(true); }
	// continue closing, this returns false if blocked
	if (!THIS->close(THIS->m_closeState, THIS->m_closeCallback, false, true)) {
		return;
	}
	// otherwise, we call the callback
	THIS->m_closeCallback ( THIS->m_closeState );
}

void Rdb::doneSavingWrapper ( void *state ) {
	Rdb *THIS = (Rdb *)state;
	THIS->doneSaving();
	// . call the callback if any
	// . this let's PageMaster.cpp know when we're closed
	if (THIS->m_closeCallback) THIS->m_closeCallback(THIS->m_closeState);
}

void Rdb::doneSaving ( ) {
	// bail if g_errno was set
	if ( g_errno ) {
		log(LOG_WARN, "db: Had error saving %s-saved.dat: %s.", m_dbname,mstrerror(g_errno));
		g_errno = 0;
		//m_needsSave = true;
		m_isSaving = false;
		return;
	}

	// sanity
	if ( m_dbname[0]=='\0' ) {
		g_process.shutdownAbort(true);
	}

	// display any error, if any, otherwise prints "Success"
	logf(LOG_INFO,"db: Successfully saved %s-saved.dat.", m_dbname);

	// mdw ---> file doesn't save right, seems like it keeps the same length as the old file...
	// . we're now closed
	// . keep m_isClosing set to true so no one can add data
	if ( m_isReallyClosing ) m_isClosed = true;

	// call it again now
	m_isSaving = false;
}

bool Rdb::isSavingTree() const {
	if ( m_useTree ) return m_tree.isSaving();
	return m_buckets.isSaving();
}

bool Rdb::saveTree ( bool useThread ) {
	const char *dbn = m_dbname;
	if ( ! dbn[0] ) {
		dbn = "unknown";
	}

	// . if RdbTree::m_needsSave is false this will return true
	// . if RdbTree::m_isSaving  is true this will return false
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	if (m_useTree) {
		if (m_tree.needsSave()) {
			log( LOG_DEBUG, "db: saving tree %s", dbn );
		}
		return m_tree.fastSave ( getDir(), m_dbname, useThread, NULL, NULL );
	} else {
		if (m_buckets.needsSave()) {
			log( LOG_DEBUG, "db: saving buckets %s", dbn );
		}
		return m_buckets.fastSave ( getDir(), useThread, NULL, NULL );
	}
}

bool Rdb::saveTreeIndex(bool /* useThread */) {
	if( !m_useIndexFile ) {
		return true;
	}

	// now loop over bases
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) {
			continue;
		}

		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if (base) {
			base->saveTreeIndex();
		}
	}
	return true;
}

bool Rdb::saveIndexes() {
	// now loop over bases
	for (int32_t i = 0; i < getNumBases(); i++) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if (!cr) {
			continue;
		}

		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if (base) {
			base->saveIndexes();
		}
	}
	return true;
}

bool Rdb::saveMaps () {
	// now loop over bases
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) {
			continue;
		}

		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if ( base ) {
			base->saveMaps();
		}
	}
	return true;
}


// returns false and sets g_errno on error
bool Rdb::loadTree ( ) {
	// get the filename of the saved tree
	char filename[256];
	sprintf(filename,"%s-saved.dat",m_dbname);

	//log (0,"Rdb::loadTree: loading %s",filename);

	// set a BigFile to this filename
	BigFile file;
	file.set ( getDir(), filename);
	bool treeExists = file.doesExist();
	bool status = false ;
	if ( treeExists ) {
		// load the table with file named "THISDIR/saved"
		status = m_tree.fastLoad ( &file , &m_mem ) ;
		// we close it now instead of him
	}
	
	if ( m_useTree ) {
		file.close();
		if ( !status && treeExists ) {
			log( LOG_ERROR, "db: Could not load saved tree." );
			return false;
		}
	}
	else {
		if ( !m_buckets.loadBuckets( m_dbname ) ) {
			log( LOG_ERROR, "db: Could not load saved buckets." );
			return false;
		}

		int32_t numKeys = m_buckets.getNumKeys();
		
		// log("db: Loaded %" PRId32" recs from %s's buckets on disk.",
		//     numKeys, m_dbname);
		
		if(!m_buckets.testAndRepair()) {
			log( LOG_ERROR, "db: unrepairable buckets, remove and restart." );
			g_process.shutdownAbort(true);
		}

		if(treeExists) {
			m_buckets.addTree( &m_tree );
			if ( m_buckets.getNumKeys() - numKeys > 0 ) {
				log( LOG_ERROR, "db: Imported %" PRId32" recs from %s's tree to buckets.",
				     m_buckets.getNumKeys()-numKeys, m_dbname);
			}

			if ( g_conf.m_readOnlyMode ) {
				m_buckets.setNeedsSave(false);
			} else {
				char newFilename[256];
				sprintf(newFilename,"%s-%" PRId32".old", filename, (int32_t)getTime());
				file.rename(newFilename,NULL);
				m_tree.reset();
			}
			file.close();

		}
	}

	return true;
}

static time_t s_lastTryTime = 0;

// . start dumping the tree
// . returns false and sets g_errno on error
bool Rdb::dumpTree ( int32_t niceness ) {
	logTrace( g_conf.m_logTraceRdb, "BEGIN %s", m_dbname );

	if (getNumUsedNodes() <= 0) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: No used nodes/keys. Returning true", m_dbname );
		return true;
	}

	// never dump doledb any more. it's rdbtree only.
	if ( m_rdbId == RDB_DOLEDB ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: Rdb is doledb. Returning true", m_dbname );
		return true;
	}

	// bail if already dumping
	//if ( m_dump.isDumping() ) return true;
	if ( m_inDumpLoop ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: Already dumping. Returning true", m_dbname );
		return true;
	}

	// . if tree is saving do not dump it, that removes things from tree
	// . i think this caused a problem messing of RdbMem before when
	//   both happened at once
	if (isSavingTree()) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: Rdb tree/bucket is saving. Returning true", m_dbname );
		return true;
	}

	// . if Process is saving, don't start a dump
	if ( g_process.m_mode == Process::SAVE_MODE ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: Process is in save mode. Returning true", m_dbname );
		return true;
	}

	// if it has been less than 3 seconds since our last failed attempt
	// do not try again to avoid flooding our log
	if ( getTime() - s_lastTryTime < 3 ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: Less than 3 seconds since last attempt. Returning true", m_dbname );
		return true;
	}

	// don't dump if not 90% full
	if ( ! needsDump() ) {
		log(LOG_INFO, "db: %s tree not 90 percent full but dumping.",m_dbname);
		//return true;
	}

	// reset g_errno -- don't forget!
	g_errno = 0;

	// . wait for all unlinking and renaming activity to flush out
	// . we do not want to dump to a filename in the middle of being
	//   unlinked
	bool anyCollectionManipulatingFiles = false;
	for(collnum_t collnum = 0; collnum<getNumBases(); collnum++) {
		RdbBase *base = getBase(collnum);
		if(base && base->isManipulatingFiles()) {
			anyCollectionManipulatingFiles = true;
			break;
		}
	}
	if(anyCollectionManipulatingFiles) {
		// update this so we don't try too much and flood the log
		// with error messages from RdbDump.cpp calling log() and
		// quickly kicking the log file over 2G which seems to 
		// get the process killed
		s_lastTryTime = getTime();
		log( LOG_INFO, "db: Waiting for previous unlink/rename operations to finish before dumping %s.", m_dbname );

		logTrace( g_conf.m_logTraceRdb, "END. %s: at least one collection is manipulating files. Returning false",
		          m_dbname );
		return false;
	}

	// remember niceness for calling setDump()
	m_niceness = niceness;

	// debug msg
	log(LOG_INFO,"db: Dumping %s to disk. nice=%" PRId32,m_dbname,niceness);

	// record last dump time so main.cpp will not save us this period
	m_lastWrite = gettimeofdayInMilliseconds();

	// only try to fix once per dump session
	int64_t start = m_lastWrite; //gettimeofdayInMilliseconds();

	// do not do chain testing because that is too slow
	if ( m_useTree && ! m_tree.checkTree ( false /* printMsgs?*/, false/*chain?*/) ) {
		log( LOG_ERROR, "db: %s tree was corrupted in memory. Trying to fix. Your memory is probably bad. "
		     "Please replace it.", m_dbname);

		// if fix failed why even try to dump?
		if ( ! m_tree.fixTree() ) {
			// only try to dump every 3 seconds
			s_lastTryTime = getTime();
			log( LOG_ERROR, "db: Could not fix in memory data for %s. Abandoning dump.", m_dbname );
			logTrace( g_conf.m_logTraceRdb, "END. %s: Unable to fix tree. Returning false", m_dbname );
			return false;
		}
	}

	log( LOG_INFO, "db: Checking validity of in memory data of %s before dumping, "
	     "took %" PRId64" ms.",m_dbname,gettimeofdayInMilliseconds()-start );

	////
	//
	// see what collnums are in the tree and just try those
	//
	////
	CollectionRec *cr = NULL;
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs() ; i++ ) {
		cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		// reset his tree count flag thing
		cr->m_treeCount = 0;
	}
	if ( m_useTree ) {
		// now scan the rdbtree and inc treecount where appropriate
		for ( int32_t i = 0 ; i < m_tree.getMinUnusedNode() ; i++ ) {
			// skip node if parents is -2 (unoccupied)
			if ( m_tree.isEmpty() ) {
				continue;
			}

			// get rec from tree collnum
			cr = g_collectiondb.getRec(m_tree.getCollnum(i));
			if ( cr ) {
				cr->m_treeCount++;
			}
		}
	} else {
		for(int32_t i = 0; i < m_buckets.getNumBuckets(); i++) {
			const RdbBucket *b = m_buckets.getBucket(i);
			collnum_t cn = b->getCollnum();
			int32_t nk = b->getNumKeys();
			cr = g_collectiondb.getRec(cn);
			if ( cr ) {
				cr->m_treeCount += nk;
			}
		}
	}

	// loop through collections, dump each one
	m_dumpCollnum = (collnum_t)-1;
	// clear this for dumpCollLoop()
	g_errno = 0;
	m_dumpErrno = 0;
	m_fn = -1000;

	// this returns false if blocked, which means we're ok, so we ret true
	if ( ! dumpCollLoop ( ) ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: dumpCollLoop blocked. Returning true", m_dbname );
		return true;
	}

	// if it returns true with g_errno set, there was an error
	if ( g_errno ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: dumpCollLoop g_error=%s. Returning false", m_dbname, mstrerror( g_errno) );
		return false;
	}

	// otherwise, it completed without blocking
	doneDumping();

	logTrace( g_conf.m_logTraceRdb, "END. %s: Done dumping. Returning true", m_dbname );
	return true;
}

// returns false if blocked, true otherwise
bool Rdb::dumpCollLoop ( ) {
	logTrace( g_conf.m_logTraceRdb, "BEGIN %s", m_dbname );

	for(;;) {

		// if no more, we're done...
		if ( m_dumpCollnum >= getNumBases() ) {
			logTrace( g_conf.m_logTraceRdb, "END. %s: No more. Returning true", m_dbname );
			return true;
		}

		// the only was g_errno can be set here is from a previous dump
		// error?
		if ( g_errno ) {
		hadError:
			// if swapped out, this will be NULL, so skip it
			RdbBase *base = NULL;
			if ( m_dumpCollnum >= 0 ) {
				CollectionRec *cr = g_collectiondb.getRec(m_dumpCollnum);
				if ( cr ) {
					base = cr->getBasePtr( m_rdbId );
				}
			}

			log( LOG_ERROR, "build: Error dumping collection: %s.",mstrerror(g_errno));
			// . if we wrote nothing, remove the file
			// . if coll was deleted under us, base will be NULL!
			if ( base &&   (! base->getFile(m_fn)->doesExist() ||
			      base->getFile(m_fn)->getFileSize() <= 0) ) {
				log("build: File %s is zero bytes, removing from memory.",base->getFile(m_fn)->getFilename());
				base->buryFiles ( m_fn , m_fn+1 );
			}

			// game over, man
			doneDumping();
			// update this so we don't try too much and flood the log
			// with error messages
			s_lastTryTime = getTime();

			logTrace( g_conf.m_logTraceRdb, "END. %s: Done dumping with g_errno=%s. Returning true",
		        	  m_dbname, mstrerror( g_errno ) );
			return true;
		}

		// advance for next round
		m_dumpCollnum++;

		// don't bother getting the base for all collections because
		// we end up swapping them in
		for ( ; m_dumpCollnum < getNumBases() ; m_dumpCollnum++ ) {
			// collection rdbs like statsdb are ok to process
			if ( m_isCollectionLess ) {
				break;
			}

			// otherwise get the coll rec now
			if ( !g_collectiondb.getRec(m_dumpCollnum) ) {
				// skip if empty
				continue;
			}

			// ok, it's good to dump
			break;
		}

		// if no more, we're done...
		if ( m_dumpCollnum >= getNumBases() ) {
			return true;
		}

		// swap it in for dumping purposes if we have to
		// "cr" is NULL potentially for collectionless rdbs, like statsdb,
		// do we can't involve that...
		RdbBase *base = getBase(m_dumpCollnum);

		// hwo can this happen? error swappingin?
		if ( ! base ) { 
			log( LOG_WARN, "rdb: dumpcollloop base was null for cn=%" PRId32, (int32_t)m_dumpCollnum);
			goto hadError;
		}

		// before we create the file, see if tree has anything for this coll
		if(m_useTree) {
			const char *k = KEYMIN();
			int32_t nn = m_tree.getNextNode ( m_dumpCollnum , k );
			if ( nn < 0 )
				continue;
			if ( m_tree.getCollnum(nn) != m_dumpCollnum )
				continue;
		} else {
			if(!m_buckets.collExists(m_dumpCollnum))
				continue;
		}

		// . MDW ADDING A NEW FILE SHOULD BE IN RDBDUMP.CPP NOW... NO!

		// if we add to many files then we can not merge, because merge op
		// needs to add a file too
		static int32_t s_flag = 0;
		if ( base->getNumFiles() + 1 >= MAX_RDB_FILES ) {
			if ( s_flag < 10 )
				log( LOG_WARN, "db: could not dump tree to disk for cn="
				    "%i %s because it has %" PRId32" files on disk. "
				    "Need to wait for merge operation.",
				    (int)m_dumpCollnum,m_dbname,base->getNumFiles());
			s_flag++;
			continue;
		}

		// this file must not exist already, we are dumping the tree into it
		m_fn = base->addNewFile();
		if ( m_fn < 0 ) {
			log( LOG_LOGIC, "db: rdb: Failed to add new file to dump %s: %s.", m_dbname, mstrerror( g_errno ) );
			return false;
		}

		log(LOG_INFO,"build: Dumping to %s/%s for coll \"%s\".",
		    base->getFile(m_fn)->getDir(),
		    base->getFile(m_fn)->getFilename() ,
		    g_collectiondb.getCollName ( m_dumpCollnum ) );

		// what is the avg rec size?
		int32_t numRecs = getNumUsedNodes();
		int32_t avgSize;

		if(m_useTree) {
			if ( numRecs <= 0 ) numRecs = 1;
			avgSize = m_tree.getMemOccupiedForList() / numRecs;
		} else {
			avgSize = m_buckets.getRecSize();
		}

		// . don't get more than 3000 recs from the tree because it gets slow
		// . we'd like to write as much out as possible to reduce possible
		//   file interlacing when synchronous writes are enabled. RdbTree::
		//   getList() should really be sped up by doing the neighbor node
		//   thing. would help for adding lists, too, maybe.
		int32_t bufSize = 300 * 1024;
		int32_t bufSize2 = 3000 * avgSize;
		if (bufSize2 < bufSize) {
			bufSize = bufSize2;
		}

		if (!m_useTree) {
			//buckets are much faster at getting lists
			bufSize *= 4;
		}

		RdbBuckets *buckets = NULL;
		RdbTree *tree = NULL;
		if (m_useTree) {
			tree = &m_tree;
		} else {
			buckets = &m_buckets;
		}

		// . RdbDump will set the filename of the map we pass to this
		// . RdbMap should dump itself out CLOSE!
		// . it returns false if blocked, true otherwise & sets g_errno on err
		// . but we only return false on error here
		if (!m_dump.set(base->getCollnum(),
	                	base->getFile(m_fn),
	                	buckets,
	                	tree,
	                	base->getTreeIndex(),
	                	base->getMap(m_fn),
	                	base->getIndex(m_fn),
	                	bufSize, // write buf size
	                	m_niceness, // niceness of 1 will NOT block
	                	this, // state
	                	doneDumpingCollWrapper,
	                	m_useHalfKeys,
	                	0LL,  // dst start offset
	                	KEYMIN(),  // prev last key
	                	m_ks,  // keySize
	                	this)) {// for setting m_needsToSave
			logTrace( g_conf.m_logTraceRdb, "END. %s: RdbDump blocked. Returning false", m_dbname );
			return false;
		}

		// error?
		if ( g_errno ) {
			log("rdb: error dumping = %s . coll deleted from under us?",
			    mstrerror(g_errno));
			// shit, what to do here? this is causing our RdbMem
			// to get corrupted!
			// because if we end up continuing it calls doneDumping()
			// and updates RdbMem! maybe set a permanent error then!
			// and if that is there do not clear RdbMem!
			m_dumpErrno = g_errno;
			// for now core out
			//g_process.shutdownAbort(true);
		}
	}
}

void Rdb::doneDumpingCollWrapper ( void *state ) {
	Rdb *THIS = (Rdb *)state;

	// we just finished dumping to a file, 
	// so allow it to try to merge again.
	//RdbBase *base = THIS->getBase(THIS->m_dumpCollnum);

	logTrace( g_conf.m_logTraceRdb, "%s", THIS->m_dbname );

	// return if the loop blocked
	if ( ! THIS->dumpCollLoop() ) {
		return;
	}

	// otherwise, call big wrapper
	THIS->doneDumping();
}

// Moved a lot of the logic originally here in Rdb::doneDumping into 
// RdbDump.cpp::dumpTree()
void Rdb::doneDumping ( ) {
	log(LOG_INFO,"db: Done dumping %s: %s.",m_dbname,
	    mstrerror(m_dumpErrno));

	// free mem in the primary buffer
	if ( ! m_dumpErrno ) {
		m_mem.freeDumpedMem( &m_tree );
	}

	for (collnum_t collnum = 0; collnum < getNumBases(); collnum++) {
		RdbBase *base = getBase(collnum);
		if (base) {
			base->generateGlobalIndex();
			base->markNewFileReadable();
		}
	}

	// . tell RdbDump it is done
	// . we have to set this here otherwise RdbMem's memory ring buffer
	//   will think the dumping is no longer going on and use the primary
	//   memory for allocating new titleRecs and such and that is not good!
	m_inDumpLoop = false;

	// . on g_errno the dumped file will be removed from "sync" file and
	//   from m_files and m_maps
	// . TODO: move this logic into RdbDump.cpp
	//for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
	//	if ( m_bases[i] ) m_bases[i]->doneDumping();
	//}
	// if we're closing shop then return
	if ( m_isClosing ) { 
		// continue closing, this returns false if blocked
		if (!close(m_closeState, m_closeCallback, false, true)) {
			return;
		}
		// otherwise, we call the callback
		m_closeCallback ( m_closeState );
		return; 
	}
	// try merge for all, first one that needs it will do it, preventing
	// the rest from doing it
	// don't attempt merge if we're niceness 0
	if ( !m_niceness ) return;
	//attemptMerge ( 1 , false );
	attemptMergeAllCallback(0,NULL);
}

void forceMergeAll(rdbid_t rdbId) {
	// set flag on all RdbBases
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr )
		{
			log(LOG_INFO,"%s:%s:%d: coll %" PRId32" - could not get CollectionRec", __FILE__,__func__,__LINE__,i);
			continue;
		}
		RdbBase *base = cr->getBase ( rdbId );
		if ( ! base ) 
		{
			log(LOG_INFO,"%s:%s:%d: coll %" PRId32" - could not get RdbBase", __FILE__,__func__,__LINE__,i);
			continue;
		}

		log(LOG_INFO,"%s:%s:%d: coll %" PRId32" - Set next merge to Forced", __FILE__,__func__,__LINE__,i);
		base->forceNextMerge();
	}

	// and try to merge now
	attemptMergeAll();
}

// this should be called every few seconds by the sleep callback, too
void attemptMergeAllCallback ( int fd , void *state ) {
	attemptMergeAll();
}


// . TODO: if rdbbase::attemptMerge() needs to launch a merge but can't
//   then do NOT remove from linked list. maybe set a flag like 'needsMerge'
void attemptMergeAll() {

	// wait for any current merge to stop!
	if ( g_merge.isMerging() ) {
		log(LOG_INFO,"Attempted merge, but merge already running");
		return;
	}

	const int32_t niceness = MAX_NICENESS;
	const bool forceMergeAll = false;
	static collnum_t s_lastCollnum = 0;

	// limit to 1000 checks to save the cpu since we call this once every 2 seconds.
	for(int loop_count=0; loop_count<1000 && loop_count<g_collectiondb.getNumRecs(); loop_count++) {
		if(s_lastCollnum >= g_collectiondb.getNumRecs())
			s_lastCollnum = 0;

		CollectionRec *cr = g_collectiondb.getRec(s_lastCollnum);
		s_lastCollnum++;
		if(!cr)
			continue;

		// args = niceness, forceMergeAll, doLog, minToMergeOverride
		// if RdbBase::attemptMerge() returns true that means it
		// launched a merge and it will call attemptMergeAll2() when
		// the merge completes.
		static const rdbid_t rdbid[] = {
			RDB_POSDB,
			RDB_TITLEDB,
			RDB_TAGDB,
			RDB_LINKDB,
			RDB_SPIDERDB,
			RDB_CLUSTERDB,
			// also try to merge on rdbs being rebuilt
			RDB2_POSDB2,
			RDB2_TITLEDB2,
			RDB2_TAGDB2,
			RDB2_LINKDB2,
			RDB2_SPIDERDB2,
			RDB2_CLUSTERDB2
		};
		
		for(unsigned i=0; i<sizeof(rdbid)/sizeof(rdbid[0]); i++) {
			RdbBase *base = cr->getBasePtr(rdbid[i]);
			if(base && base->attemptMerge(niceness,forceMergeAll))
				return;
		}
	}
}

// . return false and set g_errno on error
// . TODO: speedup with m_tree.addSortedKeys() already partially written
bool Rdb::addList ( collnum_t collnum , RdbList *list, int32_t niceness ) {
	// pick it
	if ( collnum < 0 || collnum > getNumBases() || ! getBase(collnum) ) {
		g_errno = ENOCOLLREC;
		log(LOG_WARN, "db: %s bad collnum of %i.",m_dbname,collnum);
		return false;
	}
	// make sure list is reset
	list->resetListPtr();
	// if nothing then just return true
	if ( list->isExhausted() ) return true;
	// sanity check
	if ( list->getKeySize() != m_ks ) { g_process.shutdownAbort(true); }
	// we now call getTimeGlobal() so we need to be in sync with host #0
	if ( ! isClockInSync () ) {
		// log("rdb: can not add data because clock not in sync with "
		//     "host #0. issuing try again reply.");
		g_errno = ETRYAGAIN; 
		return false;
	}

	// if we are well into repair mode, level 2, do not add anything
	// to spiderdb or titledb... that can mess up our titledb scan.
	// we always rebuild tfndb, clusterdb and spiderdb
	// but we often just repair titledb, indexdb and datedb because 
	// they are bigger. it may add to indexdb/datedb
	if ( g_repair.isRepairActive() &&
	     // but only check for collection we are repairing/rebuilding
	     collnum == g_repair.m_collnum &&
		// exception, spider status docs can be deleted from titledb
		// if user turns off 'index spider replies' before doing
		// the rebuild, when not rebuilding titledb.
	     ((m_rdbId == RDB_TITLEDB && list->getListSize() != 12 )    ||
	       m_rdbId == RDB_POSDB      ||
	       m_rdbId == RDB_CLUSTERDB  ||
	       m_rdbId == RDB_LINKDB     ||
	       m_rdbId == RDB_DOLEDB     ||
	       m_rdbId == RDB_SPIDERDB   ) ) {

		// allow banning of sites still
		log("db: How did an add come in while in repair mode? rdbId=%" PRId32,(int32_t)m_rdbId);
		g_errno = EREPAIRING;
		return false;
	}

	// . if we don't have enough room to store list, initiate a dump and
	//   return g_errno of ETRYAGAIN
	// . otherwise, we're guaranteed to have room for this list
	if ( ! hasRoom(list) ) {
		// if tree is empty, list will never fit!!!
		if ( m_useTree && m_tree.getNumUsedNodes() <= 0 ) {
			g_errno = ELISTTOOBIG;
			log( LOG_WARN, "db: Tried to add a record that is simply too big (%" PRId32" bytes) to ever fit in "
				   "the memory space for %s. Please increase the max memory for %s in gb.conf.",
				   list->getListSize(), m_dbname, m_dbname );
			return false;
		}

		// force initiate the dump now, but not if we are niceness 0
		// because then we can't be interrupted with quickpoll!
		if ( niceness != 0 ) {
			logTrace( g_conf.m_logTraceRdb, "%s: Not enough room. Calling dumpTree", m_dbname );
			dumpTree( 1/*niceness*/ );
		}

		// set g_errno after intiating the dump!
		g_errno = ETRYAGAIN;

		// return false since we didn't add the list
		return false;
	}

	do {
		char key[MAX_KEY_BYTES];
		list->getCurrentKey(key);
		int32_t  dataSize;
		char *data;

		// negative keys have no data
		if ( ! KEYNEG(key) ) {
			dataSize = list->getCurrentDataSize();
			data     = list->getCurrentData();
		}
		else {
			dataSize = 0;
			data     = NULL;
		}

		if ( ! addRecord ( collnum , key , data , dataSize ) ) {
			// bitch
			static int32_t s_last = 0;
			int32_t now = time(NULL);

			// . do not log this more than once per second to stop log spam
			// . i think this can really lockup the cpu, too
			if ( now - s_last != 0 ) {
				log( LOG_INFO, "db: Had error adding data to %s: %s.", m_dbname, mstrerror( g_errno ));
			}

			s_last = now;

			// force initiate the dump now if addRecord failed for no mem
			if ( g_errno == ENOMEM ) {
				// start dumping the tree to disk so we have room 4 add
				if ( niceness != 0 ) {
					logTrace( g_conf.m_logTraceRdb, "%s: Not enough memory. Calling dumpTree", m_dbname );
					dumpTree( 1/*niceness*/ );
				}
				// tell caller to try again later (1 second or so)
				g_errno = ETRYAGAIN;
			}

			// discontinue adding any more of the list
			return false;
		}
	} while ( list->skipCurrentRecord() ); // skip to next record, returns false on end of list

	// if tree is >= 90% full dump it
	if ( m_dump.isDumping() ) {
		logTrace( g_conf.m_logTraceRdb, "END. %s: is already dumping. Returning true", m_dbname );
		return true;
	}

	// return true if not ready for dump yet
	if ( ! needsDump () ) {
		//logTrace( g_conf.m_logTraceRdb, "END. %s: doesn't need dump. Returning true", m_dbname );
		return true;
	}

	// if dump started ok, return true
	if ( niceness != 0 ) {
		if ( dumpTree( 1/*niceness*/ ) ) {
			logTrace( g_conf.m_logTraceRdb, "END. %s: dumped tree. Returning true", m_dbname );
			return true;
		}
	}

	// technically, since we added the record, it is not an error
	g_errno = 0;

	// . otherwise, bitch and return false with g_errno set
	// . usually this is because it is waiting for an unlink/rename
	//   operation to complete... so make it LOG_INFO
	log(LOG_INFO,"db: Failed to dump data to disk for %s.",m_dbname);

	return true;
}

bool Rdb::needsDump ( ) const {
	if ( m_mem.is90PercentFull () ) {
		return true;
	}

	if ( m_useTree ) {
		if ( m_tree.is90PercentFull() ) {
			return true;
		}
	} else {
		if ( m_buckets.needsDump() ) {
			return true;
		}
	}

	// if adding to doledb and it has been > 1 day then force a dump
	// so that all the negative keys in the tree annihilate with the
	// keys on disk to make it easier to read a doledb list
	if ( m_rdbId != RDB_DOLEDB ) {
		return false;
	}

	// or dump doledb if a ton of negative recs...
	// otherwise, no need to dump doledb just yet
	return ( m_tree.getNumNegativeKeys() > 50000 );
}

bool Rdb::hasRoom(int32_t totalRecs, int32_t totalDataSize) {
	logTrace(g_conf.m_logTraceRdb, "BEGIN %s: numRecs=%" PRId32" dataSize=%" PRId32" availMem=%" PRId32,
	         m_dbname, totalRecs, totalDataSize, m_mem.getAvailMem());

	// nodes
	if (m_useTree) {
		if (m_tree.getNumAvailNodes() < totalRecs) {
			logTrace(g_conf.m_logTraceRdb, "END %s: Insufficient tree nodes. Returning false", m_dbname);
			return false;
		}
	} else {
		if (!m_buckets.hasRoom(totalRecs)) {
			logTrace(g_conf.m_logTraceRdb, "END %s: Insufficient buckets. Returning false", m_dbname);
			return false;
		}
	}

	// memory (only use for data)
	bool result = (m_mem.getAvailMem() >= totalDataSize);
	logTrace(g_conf.m_logTraceRdb, "END %s: Memory check. Returning %s", m_dbname, result ? "true" : "false");
	return result;
}


bool Rdb::hasRoom(RdbList *list) {
	// how many nodes will tree need?
	int32_t numNodes = list->getNumRecs( );

	// does tree have room for these nodes?
	if (m_useTree) {
		if (m_tree.getNumAvailNodes() < numNodes) {
			return false;
		}
	} else {
		if (!m_buckets.hasRoom(numNodes)) {
			return false;
		}
	}

	// how many nodes will tree need?
	// how much space will RdbMem, m_mem, need?
	int64_t overhead = m_ks;
	if ( list->getFixedDataSize() == -1 ) {
		overhead += 4;
	}

	// how much mem will the data use?
	int64_t dataSpace = (int64_t)list->getListSize() - ((int64_t)numNodes * overhead);

	// if we are doledb, we are a tree-only rdb, so try to reclaim
	// memory from deleted nodes. works by condesing the used memory.
	if ( m_rdbId == RDB_DOLEDB && 
	     // if there is no room left in m_mem (RdbMem class)...
	     ( m_mem.getAvailMem() < dataSpace || g_conf.m_forceIt) &&
	     // and last time we tried this, if any, it reclaimed 1MB+
	     (m_lastReclaim>1024*1024||m_lastReclaim==-1||g_conf.m_forceIt)){
		// reclaim the memory now. returns -1 and sets g_errno on error
		int32_t reclaimed = reclaimMemFromDeletedTreeNodes();
		// reset force flag
		g_conf.m_forceIt = false;
		// ignore errors for now
		g_errno = 0;
		// how much did we free up?
		if ( reclaimed >= 0 )
			m_lastReclaim = reclaimed;
	}

	// does m_mem have room for "dataSpace"?
	if ( (int64_t)m_mem.getAvailMem() < dataSpace ) return false;
	// otherwise, we do have room
	return true;
}

// . NOTE: low bit should be set , only antiKeys (deletes) have low bit clear
// . returns false and sets g_errno on error, true otherwise
// . if RdbMem, m_mem, has no mem, sets g_errno to ETRYAGAIN and returns false
//   because dump should complete soon and free up some mem
// . this overwrites dups
bool Rdb::addRecord(collnum_t collnum, char *key, char *data, int32_t dataSize) {
	logTrace(g_conf.m_logTraceRdb, "BEGIN %s: collnum=%" PRId32" key=%s dataSize=%" PRId32, m_dbname, collnum, KEYSTR(key, m_ks), dataSize);

	if (!getBase(collnum)) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: addRecord: collection #%i is gone.", collnum);
		logTrace(g_conf.m_logTraceRdb, "END. %s: collection gone. Returning false", m_dbname);
		return false;
	}

	// skip if tree not writable
	if ( ! g_process.m_powerIsOn ) {
		// log it every 3 seconds
		static int32_t s_last = 0;
		int32_t now = getTime();
		if ( now - s_last > 3 ) {
			s_last = now;
			log("db: addRecord: power is off. try again.");
		}
		logTrace(g_conf.m_logTraceRdb, "END. %s: Power loss. Returning false", m_dbname);
		g_errno = ETRYAGAIN; 
		return false;
	}

	// we can also use this logic to avoid adding to the waiting tree
	// because Process.cpp locks all the trees up at once and unlocks
	// them all at once as well. so since SpiderRequests are added to
	// spiderdb and then alter the waiting tree, this statement should
	// protect us.
	if (!isWritable()) {
		g_errno = ETRYAGAIN;
		logTrace(g_conf.m_logTraceRdb, "END. %s: Not writable. Returning false", m_dbname);
		return false;
	}

	// bail if we're closing
	if (m_isClosing) {
		g_errno = ECLOSING;
		logTrace(g_conf.m_logTraceRdb, "END. %s: Closing. Returning false", m_dbname);
		return false;
	}

	// sanity check
	if (KEYNEG(key)) {
		if ((dataSize > 0 && data)) {
			log( LOG_WARN, "db: Got data for a negative key." );
			g_process.shutdownAbort(true);
		}
	} else if ( m_fixedDataSize >= 0 && dataSize != m_fixedDataSize ) {
		// sanity check
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: addRecord: DataSize is %" PRId32" should be %" PRId32, dataSize,m_fixedDataSize );
		g_process.shutdownAbort(true);
	}

	// do not add if range being dumped at all because when the
	// dump completes it calls deleteList() and removes the nodes from
	// the tree, so if you were overriding a node currently being dumped
	// we would lose it.
	if ( m_dump.isDumping() &&
		 // ensure the dump is dumping the collnum of this key
		 m_dump.getCollNum() == collnum &&
		 m_dump.getLastKeyInQueue() &&
		 // the dump should not split positive/negative keys so
		 // if our positive/negative twin should be in the dump with us
		 // or not in the dump with us, so any positive/negative
		 // annihilation below should be ok and we should be save
		 // to call deleteNode() below
		 KEYCMP(key,m_dump.getFirstKeyInQueue(),m_ks)>=0 &&
		 //oppKey <= m_dump.getLastKeyInQueue ()   ) goto addIt;
		 KEYCMP(key,m_dump.getLastKeyInQueue (),m_ks)<=0   )  {
		    // tell caller to wait and try again later
	    g_errno = ETRYAGAIN;
		logTrace(g_conf.m_logTraceRdb, "END. %s: Dumping. Returning false", m_dbname);
	    return false;
	}

	// save orig
	char *orig = NULL;

	// copy the data before adding if we don't already own it
	if (data) {
		// save orig
		orig = data;

		// sanity check
		if ( m_fixedDataSize == 0 && dataSize > 0 ) {
			g_errno = EBADENGINEER;
			log(LOG_LOGIC,"db: addRecord: Data is present. Should not be");
			logTrace(g_conf.m_logTraceRdb, "END. %s: Data is present. Returning false", m_dbname);
			return false;
		}

		data = (char *) m_mem.dupData ( data, dataSize, collnum);
		if ( ! data ) { 
			g_errno = ETRYAGAIN; 
			log(LOG_WARN, "db: Could not allocate %" PRId32" bytes to add data to %s. Retrying.",dataSize,m_dbname);
			logTrace(g_conf.m_logTraceRdb, "END. %s: Unable to allocate data. Returning false", m_dbname);
			return false;
		}
	}

	if (m_rdbId == RDB_DOLEDB && g_conf.m_logDebugSpider) {
		// must be 96 bits
		if (m_ks != 12) {
			g_process.shutdownAbort(true);
		}

		// set this
		key96_t doleKey = *(key96_t *)key;

		// remove from g_spiderLoop.m_lockTable too!
		if (KEYNEG(key)) {
			// log debug
			logf(LOG_DEBUG,"spider: removed doledb key for pri=%" PRId32" time=%" PRIu32" uh48=%" PRIu64,
			     (int32_t)g_doledb.getPriority(&doleKey),
			     (uint32_t)g_doledb.getSpiderTime(&doleKey),
			     g_doledb.getUrlHash48(&doleKey));
		} else {
			// do not overflow!
			// log debug
			SpiderRequest *sreq = (SpiderRequest *)data;
			logf(LOG_DEBUG, "spider: added doledb key for pri=%" PRId32" time=%" PRIu32" uh48=%" PRIu64" u=%s",
			     (int32_t)g_doledb.getPriority(&doleKey),
			     (uint32_t)g_doledb.getSpiderTime(&doleKey),
			     g_doledb.getUrlHash48(&doleKey),
			     sreq->m_url);
		}
	}

	// make the opposite key of "key"
	char oppKey[MAX_KEY_BYTES];
	KEYSET(oppKey, key, m_ks);
	KEYXOR(oppKey, 0x01);

	if (m_useIndexFile) {
		bool isSpecialKey = false;
		if (m_rdbId == RDB_POSDB || m_rdbId == RDB2_POSDB2) {
			isSpecialKey = (Posdb::getTermId(key) == POSDB_DELETEDOC_TERMID);
		} else {
			/// @todo ALC cater for other rdb types here
			gbshutdownLogicError();
		}

		// there are no negative keys when we're using index (except special keys eg: posdb with termId 0)
		// if we're adding key that have a corresponding opposite key, it means we want to remove the key from the tree
		// even if it's a positive key (how else would we remove the special negative key?)
		bool deleted = m_useTree ? m_tree.deleteNode(collnum, oppKey, true) : m_buckets.deleteNode(collnum, oppKey);
		if (deleted) {
			// assume that we don't need to delete from index even when we get positive special key
			// since positive special key will only be inserted when a new document is added
			// this means that other keys should overwrite the existing deleted docId
			logTrace(g_conf.m_logTraceRdb, "END. %s: Key with corresponding opposite key deleted in tree. Returning true", m_dbname);
			return true;
		}

		// if we have no files on disk for this db, don't bother preserving a a negative rec, it just wastes tree space
		if (KEYNEG(key)) {
			// return if all data is in the tree
			if (getBase(collnum)->getNumFiles() == 0) {
				logTrace(g_conf.m_logTraceRdb, "END. %s: Negative key with all data in tree. Returning true", m_dbname);
				return true;
			}

			// we should only store special delete keys (eg: posdb with termId 0)
			// we will have non-special keys here to simplify logic in XmlDoc::getMetaList (and we can't really be sure
			// if the key we're adding is in RdbTree/RdbBuckets at that point of time. It could potentially be dumped
			// after the check.
			if (!isSpecialKey) {
				logTrace(g_conf.m_logTraceRdb, "END. %s: Negative key with non-zero termId found. Returning true", m_dbname);
				return true;
			}
		} else {
			// make sure that positive special key is not persisted (reasons as delete key above; the XmlDoc::getMetaList part)
			if (isSpecialKey) {
				logTrace(g_conf.m_logTraceRdb, "END. %s: Positive key with zero termId found. Returning true", m_dbname);
				return true;
			}
		}
	} else {
		if (m_useTree) {
			// . TODO: save this tree-walking state for adding the node!!!
			// . TODO: use somethin like getNode(key,&lastNode) then addNode (lastNode,key,data,dataSize)
			// . #1) if we're adding a positive key, replace negative counterpart
			//       in the tree, because we'll override the positive rec it was
			//       deleting
			// . #2) if we're adding a negative key, replace positive counterpart
			//       in the tree, but we must keep negative rec in tree in case
			//       the positive counterpart was overriding one on disk (as in #1)

			// . freeData should be true, the tree doesn't own the data
			//   so it shouldn't free it really
			m_tree.deleteNode(collnum, oppKey, true);

			/// @todo ALC is this necessary? we remove delete keys when we dump to Rdb anyway for the first file
			// if we have no files on disk for this db, don't bother preserving a a negative rec, it just wastes tree space
			if (KEYNEG(key)) {
				// return if all data is in the tree
				if (getBase(collnum)->getNumFiles() == 0) {
					logTrace(g_conf.m_logTraceRdb, "END. %s: Negative key with all data in tree. Returning true", m_dbname);
					return true;
				}
				// . otherwise, assume we match a positive...
			}
		}
	}

	// . cancel any spider request that is a dup in the dupcache to save disk space
	// . twins might have different dupcaches so they might have different dups,
	//   but it shouldn't be a big deal because they are dups!
	if (m_rdbId == RDB_SPIDERDB && !KEYNEG(key)) {
		// . this will create it if spiders are on and its NULL
		// . even if spiders are off we need to create it so 
		//   that the request can adds its ip to the waitingTree
		SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);

		// skip if not there
		if (!sc) {
			logTrace(g_conf.m_logTraceRdb, "END. %s: No spider coll. Returning true", m_dbname);
			return true;
		}

		/// @todo ALC we're making an assumption that data passed in is part of a SpiderRequest (fix this!)
		SpiderRequest *sreq = (SpiderRequest *)(orig - 4 - sizeof(key128_t));

		// is it really a request and not a SpiderReply?
		if (g_spiderdb.isSpiderRequest(&(sreq->m_key))) {
			// skip if in dup cache. do NOT add to cache since
			// addToWaitingTree() in Spider.cpp will do that when called
			// from addSpiderRequest() below
			if (sc->isInDupCache(sreq, false)) {
				logDebug(g_conf.m_logDebugSpider, "spider: adding spider req %s is dup. skipping.", sreq->m_url);
				logTrace(g_conf.m_logTraceRdb, "END. %s: Duplicated spider req. Returning true", m_dbname);
				return true;
			}

			// if we are overflowing...
			if (!sreq->m_isAddUrl && !sreq->m_isPageReindex && !sreq->m_urlIsDocId && !sreq->m_forceDelete &&
			    sc->isFirstIpInOverflowList(sreq->m_firstIp)) {
				g_stats.m_totalOverflows++;
				logDebug(g_conf.m_logDebugSpider, "spider: skipping for overflow url %s ", sreq->m_url);
				logTrace(g_conf.m_logTraceRdb, "END. %s: Overflow. Returning true", m_dbname);
				return true;
			}
		}
	}

	int32_t tn;
	if (m_useTree) {
		tn = m_tree.addNode(collnum, key, data, dataSize);
		if (tn < 0) {
			// enhance the error message
			const char *ss = m_tree.isSaving() ? " Tree is saving." : "";
			log(LOG_INFO, "db: Had error adding data to %s: %s. %s", m_dbname, mstrerror(g_errno), ss);
			return false;
		}
	} else {
		// . TODO: add using "lastNode" as a start node for the insertion point
		// . should set g_errno if failed
		// . caller should retry on g_errno of ETRYAGAIN or ENOMEM
		tn = m_buckets.addNode(collnum, key, data, dataSize);
		if (tn < 0) {
			// enhance the error message
			const char *ss = m_buckets.isSaving() ? " Buckets are saving." : "";
			log(LOG_INFO, "db: Had error adding data to %s: %s. %s", m_dbname, mstrerror(g_errno), ss);
			return false;
		}
	}

	// Add data record to the current index file for the -saved.dat file.
	// This index is stored in the Rdb record- the individual part file
	// indexes are in RdbBase and are read-only except when merging).
	// we only add to index after adding to tree/buckets
	RdbIndex *index = getBase(collnum)->getTreeIndex();
	if (index) {
		index->addRecord(key);
	}

	// if adding to spiderdb, add to cache, too (except negative key)
	if ((m_rdbId == RDB_SPIDERDB || m_rdbId == RDB_DOLEDB) && !KEYNEG(key)) {
		// . this will create it if spiders are on and its NULL
		// . even if spiders are off we need to create it so
		//   that the request can adds its ip to the waitingTree
		SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);
		// skip if not there
		if (!sc) {
			logTrace(g_conf.m_logTraceRdb, "END. %s: Done. No spider coll. Returning true", m_dbname);
			return true;
		}

		// if doing doledb...
		if (m_rdbId == RDB_DOLEDB) {
			int32_t pri = g_doledb.getPriority((key96_t *)key);
			// skip over corruption
			if (pri < 0 || pri >= MAX_SPIDER_PRIORITIES) {
				logTrace(g_conf.m_logTraceRdb, "END. %s: Done. Skip over corruption", m_dbname);
				return true;
			}
			// if added positive key is before cursor, update curso
			if (KEYCMP(key, (char *)&sc->m_nextKeys[pri], sizeof(key96_t)) < 0) {
				KEYSET((char *)&sc->m_nextKeys[pri], key, sizeof(key96_t));
				logDebug(g_conf.m_logDebugSpider, "spider: cursor reset pri=%" PRId32" to %s", pri, KEYSTR(key, 12));
			}

			logTrace(g_conf.m_logTraceRdb, "END. %s: Done. For doledb. Returning true", m_dbname);

			// that's it for doledb mods
			return true;
		}

		// . ok, now add that reply to the cache

		/// @todo ALC we're making an assumption that data passed in is part of a SpiderRequest (fix this!)
		// assume this is the rec (4 byte dataSize,spiderdb key is now 16 bytes)
		SpiderRequest *sreq = (SpiderRequest *)(orig - 4 - sizeof(key128_t));

		// is it really a request and not a SpiderReply?
		if (g_spiderdb.isSpiderRequest(&sreq->m_key)) {
			// add the request

			// log that. why isn't this undoling always
			logDebug(g_conf.m_logDebugSpider, "spider: rdb: added spider request to spiderdb rdb tree addnode=%" PRId32
					" request for uh48=%" PRIu64" prntdocid=%" PRIu64" firstIp=%s spiderdbkey=%s",
			         tn, sreq->getUrlHash48(), sreq->getParentDocId(), iptoa(sreq->m_firstIp),
			         KEYSTR((char *)&sreq->m_key, sizeof(key128_t)));

			// false means to NOT call evaluateAllRequests()
			// because we call it below. the reason we do this
			// is because it does not always get called
			// in addSpiderRequest(), like if its a dup and
			// gets "nuked". (removed callEval arg since not
			// really needed)
			sc->addSpiderRequest(sreq, gettimeofdayInMilliseconds());
		} else {
			// otherwise repl
			SpiderReply *rr = (SpiderReply *)sreq;

			// log that. why isn't this undoling always
			logDebug(g_conf.m_logDebugSpider, "rdb: rdb: got spider reply for uh48=%" PRIu64, rr->getUrlHash48());

			// add the reply
			sc->addSpiderReply(rr);

			/// @todo ALC why are we removing this here? this check should be at where we're trying to insert this
			// don't actually add it if "fake". i.e. if it
			// was an internal error of some sort... this will
			// make it try over and over again i guess...
			// no because we need some kinda reply so that gb knows
			// the pagereindex docid-based spider requests are done,
			// at least for now, because the replies were not being
			// added for now. just for internal errors at least...
			// we were not adding spider replies to the page reindexes
			// as they completed and when i tried to rerun it
			// the title recs were not found since they were deleted,
			// so we gotta add the replies now.
			int32_t indexCode = rr->m_errCode;
			if (indexCode == EABANDONED) {
				log(LOG_WARN, "rdb: not adding spiderreply to rdb because it was an internal error for uh48=%" PRIu64
				              " errCode = %s", rr->getUrlHash48(), mstrerror(indexCode));
				m_tree.deleteNode(tn, false);
			}
		}

		// clear errors from adding to SpiderCache
		g_errno = 0;
	}

	logTrace(g_conf.m_logTraceRdb, "END. %s: Done. Returning true", m_dbname);
	return true;
}

// . use the maps and tree to estimate the size of this list w/o hitting disk
// . used by Indexdb.cpp to get the size of a list for IDF weighting purposes
int64_t Rdb::getListSize(collnum_t collnum, const char *startKey, const char *endKey, char *max, int64_t oldTruncationLimit) const {
	// pick it
	if ( collnum < 0 || collnum > getNumBases() || ! getBase(collnum) ) {
		log(LOG_WARN, "db: %s bad collnum of %i", m_dbname, collnum);
		return 0;
	}
	return getBase(collnum)->estimateListSize(startKey, endKey, max, oldTruncationLimit);
}

int64_t Rdb::getNumGlobalRecs() const {
	return (getNumTotalRecs() * g_hostdb.m_numShards);
}

// . return number of positive records - negative records
int64_t Rdb::getNumTotalRecs(bool useCache) const {

	// are we statsdb? then we have no associated collections
	// because we are used globally, by all collections
	if (m_isCollectionLess)
		return m_collectionlessBase->getNumTotalRecs();

	// this gets slammed w/ too many collections so use a cache...
	int32_t now = 0;
	if ( useCache ) {
		now = getTimeLocal();
		if ( now - m_cacheLastTime == 0 ) 
			return m_cacheLastTotal;
	}

	// same as num recs
	int32_t nb = getNumBases();

	int64_t total = 0LL;

	//return 0; // too many collections!!
	for ( int32_t i = 0 ; i < nb ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if ( ! base ) continue;
		total += base->getNumTotalRecs();
	}
	// . add in the btree
	// . TODO: count negative and positive recs in the b-tree
	//total += m_tree.getNumPositiveKeys();
	//total -= m_tree.getNumNegativeKeys();
	if ( now ) {
		m_cacheLastTime = now;
		m_cacheLastTotal = total;
	}

	return total;
}


int64_t Rdb::getCollNumTotalRecs(collnum_t collnum) const {

	if ( collnum < 0 ) return 0;

	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return 0;
	// if swapped out, this will be NULL, so skip it
	RdbBase *base = cr->getBasePtr(m_rdbId);
	if ( ! base ) {
		log("rdb: getcollnumtotalrecs: base swapped out");
		return 0;
	}
	return base->getNumTotalRecs();
}



// . how much mem is allocated for all of our maps?
// . we have one map per file
int64_t Rdb::getMapMemAllocated() const {
	int64_t total = 0;
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		// skip null base if swapped out
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		RdbBase *base = cr->getBasePtr(m_rdbId);		
		if ( ! base ) continue;
		total += base->getMapMemAllocated();
	}
	return total;
}

// sum of all parts of all big files
int32_t Rdb::getNumSmallFiles() const {
	int32_t total = 0;
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		// skip null base if swapped out
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		RdbBase *base = cr->getBasePtr(m_rdbId);		
		if ( ! base ) continue;
		total += base->getNumSmallFiles();
	}
	return total;
}

// sum of all parts of all big files
int32_t Rdb::getNumFiles() const {
	int32_t total = 0;
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if ( ! base ) continue;
		total += base->getNumFiles();
	}
	return total;
}

int64_t Rdb::getDiskSpaceUsed() const {
	int64_t total = 0;
	for ( int32_t i = 0 ; i < getNumBases() ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		// if swapped out, this will be NULL, so skip it
		RdbBase *base = cr->getBasePtr(m_rdbId);
		if ( ! base ) continue;
		total += base->getDiskSpaceUsed();
	}
	return total;
}

bool Rdb::isMerging() const {
	// use this for speed
	return m_numMergesOut!=0;
}
	

// maps an rdbId to an Rdb
Rdb *getRdbFromId ( rdbid_t rdbId ) {
	switch(rdbId) {
		case RDB_TAGDB: return g_tagdb.getRdb();
		case RDB_POSDB: return g_posdb.getRdb();
		case RDB_TITLEDB: return g_titledb.getRdb();
		case RDB_SPIDERDB: return g_spiderdb.getRdb();
		case RDB_DOLEDB: return g_doledb.getRdb();
		case RDB_CLUSTERDB: return g_clusterdb.getRdb();
		case RDB_LINKDB: return g_linkdb.getRdb();
		case RDB_STATSDB: return g_statsdb.getRdb();

		case RDB2_POSDB2: return g_posdb2.getRdb();
		case RDB2_TITLEDB2: return g_titledb2.getRdb();
		case RDB2_SPIDERDB2: return g_spiderdb2.getRdb();
		case RDB2_CLUSTERDB2: return g_clusterdb2.getRdb();
		case RDB2_LINKDB2: return g_linkdb2.getRdb();
		case RDB2_TAGDB2: return g_tagdb2.getRdb();
		default:
			return NULL;
	}
}
		
// the opposite of the above
rdbid_t getIdFromRdb ( Rdb *rdb ) {
	if ( rdb == g_tagdb.getRdb    () ) return RDB_TAGDB;
	if ( rdb == g_posdb.getRdb   () ) return RDB_POSDB;
	if ( rdb == g_titledb.getRdb   () ) return RDB_TITLEDB;
	if ( rdb == g_spiderdb.getRdb  () ) return RDB_SPIDERDB;
	if ( rdb == g_doledb.getRdb    () ) return RDB_DOLEDB;
	if ( rdb == g_clusterdb.getRdb () ) return RDB_CLUSTERDB;
	if ( rdb == g_statsdb.getRdb   () ) return RDB_STATSDB;
	if ( rdb == g_linkdb.getRdb    () ) return RDB_LINKDB;
	if ( rdb == g_posdb2.getRdb   () ) return RDB2_POSDB2;
	if ( rdb == g_tagdb2.getRdb     () ) return RDB2_TAGDB2;
	if ( rdb == g_titledb2.getRdb   () ) return RDB2_TITLEDB2;
	if ( rdb == g_spiderdb2.getRdb  () ) return RDB2_SPIDERDB2;
	if ( rdb == g_clusterdb2.getRdb () ) return RDB2_CLUSTERDB2;
	if ( rdb == g_linkdb2.getRdb    () ) return RDB2_LINKDB2;

	log(LOG_LOGIC,"db: getIdFromRdb: no rdbId for %s.",rdb->getDbname());
	return RDB_NONE;
}

bool isSecondaryRdb ( rdbid_t rdbId ) {
	switch ( rdbId ) {
		case RDB2_POSDB2   : return true;
		case RDB2_TAGDB2     : return true;
		case RDB2_TITLEDB2   : return true;
		case RDB2_SPIDERDB2  : return true;
		case RDB2_CLUSTERDB2 : return true;
		case RDB2_LINKDB2 : return true;
		default:
			return false;
	}
}

// use a quick table now...
char getKeySizeFromRdbId(rdbid_t rdbId) {
	static bool s_flag = true;
	static char s_table1[RDB_END];
	if ( s_flag ) {
		// only stock the table once
		s_flag = false;

		// . loop over all possible rdbIds
		// . RDB_NONE is 0!
		for (int32_t i = 1; i < RDB_END; ++i) {
			// assume 12
			int32_t ks = 12;

			if (i == RDB_SPIDERDB || i == RDB2_SPIDERDB2 || i == RDB_TAGDB || i == RDB2_TAGDB2) {
				ks = 16;
			} else if (i == RDB_POSDB || i == RDB2_POSDB2) {
				ks = sizeof(key144_t);
			} else if (i == RDB_LINKDB || i == RDB2_LINKDB2) {
				ks = sizeof(key224_t);
			}

			// set the table
			s_table1[i] = ks;
		}
	}

	// sanity check
	if (s_table1[rdbId] == 0) {
		log(LOG_ERROR, "rdb: bad lookup rdbid of %i", (int)rdbId);
		g_process.shutdownAbort(true); 
	}

	return s_table1[rdbId];
}

// returns -1 if dataSize is variable
int32_t getDataSizeFromRdbId ( uint8_t rdbId ) {
	static bool s_flag = true;
	static int32_t s_table2[RDB_END];
	if ( rdbId >= RDB_END )
		g_process.shutdownAbort(true);
	if ( s_flag ) {
		// sanity check
		// loop over all possible rdbIds
		for ( int32_t i = 1 ; i < RDB_END ; i++ ) {
			// assume none
			int32_t ds = 0;
			// only these are 16 as of now
			if ( i == RDB_POSDB ||
			     i == RDB_CLUSTERDB ||
			     i == RDB_LINKDB )
				ds = 0;
			else if ( i == RDB_TITLEDB ||
				  i == RDB_TAGDB   ||
				  i == RDB_SPIDERDB ||
				  i == RDB_DOLEDB )
				ds = -1;
			else if ( i == RDB_STATSDB )
				ds = sizeof(StatData);
			else if ( i == RDB2_POSDB2 ||
				  i == RDB2_CLUSTERDB2 ||
				  i == RDB2_LINKDB2 )
				ds = 0;
			else if ( i == RDB2_TITLEDB2 ||
				  i == RDB2_TAGDB2   ||
				  i == RDB2_SPIDERDB2 )
				ds = -1;
			else {
				continue;
			}

			// set the table
			s_table2[i] = ds;
		}
		// only stock the table once
		s_flag = false;
	}
	return s_table2[rdbId];
}

// get the dbname
const char *getDbnameFromId(rdbid_t rdbId) {
        const Rdb *rdb = getRdbFromId(rdbId);
	if ( rdb )
		return rdb->getDbname();
	else {
		log(LOG_LOGIC,"db: rdbId of %" PRId32" is invalid.",(int32_t)rdbId);
		return "INVALID";
	}
}

// get the RdbBase class for an rdbId and collection name
RdbBase *getRdbBase(rdbid_t rdbId, const char *coll) {
	Rdb *rdb = getRdbFromId ( rdbId );
	if ( ! rdb ) {
		log("db: Collection \"%s\" does not exist.",coll);
		return NULL;
	}
	// statdb is a special case
	collnum_t collnum ;
	if ( rdb->isCollectionless() )
		collnum = (collnum_t) 0;
	else    
		collnum = g_collectiondb.getCollnum ( coll );
	if(collnum == -1) {
		g_errno = ENOCOLLREC;
		return NULL;
	}
	return rdb->getBase(collnum);
}


// get the RdbBase class for an rdbId and collection name
RdbBase *getRdbBase(rdbid_t rdbId, collnum_t collnum) {
	Rdb *rdb = getRdbFromId ( rdbId );
	if ( ! rdb ) {
		log("db: Collection #%" PRId32" does not exist.",(int32_t)collnum);
		return NULL;
	}
	if ( rdb->isCollectionless() ) collnum = (collnum_t) 0;
	return rdb->getBase(collnum);
}

int32_t Rdb::getNumUsedNodes ( ) const {
	 if(m_useTree) return m_tree.getNumUsedNodes(); 
	 return m_buckets.getNumKeys();
}

int32_t Rdb::getMaxTreeMem() const {
	if(m_useTree) return m_tree.getMaxMem();
	return m_buckets.getMaxMem();
}

int32_t Rdb::getNumNegativeKeys() const {
	 if(m_useTree) return m_tree.getNumNegativeKeys(); 
	 return m_buckets.getNumNegativeKeys();
}


int32_t Rdb::getTreeMemOccupied() const {
	 if(m_useTree) return m_tree.getMemOccupied(); 
	 return m_buckets.getMemOccupied();
}

int32_t Rdb::getTreeMemAllocated () const {
	 if(m_useTree) return m_tree.getMemAllocated(); 
	 return m_buckets.getMemAllocated();
}

void Rdb::disableWrites () {
	if(m_useTree) m_tree.disableWrites();
	else m_buckets.disableWrites();
}
void Rdb::enableWrites  () {
	if(m_useTree) m_tree.enableWrites();
	else m_buckets.enableWrites();
}

bool Rdb::isWritable ( ) const {
	if(m_useTree) return m_tree.isWritable();
	return m_buckets.isWritable();
}

bool Rdb::needsSave() const {
	if(m_useTree) return m_tree.needsSave();
	else return m_buckets.needsSave();
}

void Rdb::cleanTree() {
	if(m_useTree) return m_tree.cleanTree();
	else return m_buckets.cleanBuckets();
}

// if we are doledb, we are a tree-only rdb, so try to reclaim
// memory from deleted nodes. works by condensing the used memory.
// returns how much we reclaimed.
int32_t Rdb::reclaimMemFromDeletedTreeNodes() {

	log("rdb: reclaiming tree mem for doledb");

	// this only works for non-dumped RdbMem right now, i.e. doledb only
	if ( m_rdbId != RDB_DOLEDB ) { g_process.shutdownAbort(true); }

	// start scanning the mem pool
	char *p    = m_mem.m_mem;
	char *pend = m_mem.m_ptr1;

	char *memEnd = m_mem.m_mem + m_mem.m_memSize;

	char *dst = p;

	int32_t inUseOld = pend - p;

	char *pstart = p;

	int32_t marked = 0;
	int32_t occupied = 0;

	HashTableX ht;
	if (!ht.set ( 4, 
		      4, 
		      m_tree.getNumUsedNodes()*2,
		      NULL , 0 , 
		      false ,
		      "trectbl",
		      true )) // useMagic? yes..
		return -1;

	int32_t dups = 0;

	// mark the data of unoccupied nodes somehow
	int32_t nn = m_tree.getMinUnusedNode();
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// skip empty nodes in tree
		if ( m_tree.isEmpty(i) ) {marked++; continue; }
		// get data ptr
		char *data = m_tree.getData(i);
		// and key ptr, if negative skip it
		//char *key = m_tree.getKey(i);
		//if ( (key[0] & 0x01) == 0x00 ) { occupied++; continue; }
		// sanity, ensure legit
		if ( data < pstart ) { g_process.shutdownAbort(true); }
		// offset
		int32_t doff = (int32_t)(data - pstart);
		// a dup? sanity check
		if ( ht.isInTable ( &doff ) ) {
			int32_t *vp = (int32_t *) ht.getValue ( &doff );
			log("rdb: reclaim got dup oldi=0x%" PTRFMT" "
			    "newi=%" PRId32" dataoff=%" PRId32"."
			    ,(PTRTYPE)vp,i,doff);
			//while ( 1 == 1 ) sleep(1);
			dups++;
			continue;
		}
		// indicate it is legit
		int32_t val = i;
		ht.addKey ( &doff , &val );
		occupied++;
	}

	if ( occupied + dups != m_tree.getNumUsedNodes() ) 
		log("rdb: reclaim mismatch1");

	if ( ht.getNumSlotsUsed() + dups != m_tree.getNumUsedNodes() )
		log("rdb: reclaim mismatch2");

	int32_t skipped = 0;

	// the spider requests should be linear in there. so we can scan
	// them. then put their offset into a map that maps it to the new
	// offset after doing the memmove().
	for ( ; p < pend ; ) {
		SpiderRequest *sreq = (SpiderRequest *)p;
		int32_t oldOffset = p - pstart;
		int32_t recSize = sreq->getRecSize();
		// negative key? this shouldn't happen
		if ( (sreq->m_key.n0 & 0x01) == 0x00 ) {
			log("rdb: reclaim got negative doldb key in scan");
			p += sizeof(key96_t);
			skipped++;
			continue;
		}
		// if not in hash table it was deleted from tree i guess
		if ( ! ht.isInTable ( &oldOffset ) ) {
			p += recSize;
			skipped++; 
			continue;
		}

		// corrupted? or breach of mem buf?
		if ( sreq->isCorrupt() ||  dst + recSize > memEnd ) {
			log( LOG_WARN, "rdb: not readding corrupted doledb1 in scan. deleting from tree.");
			g_process.shutdownAbort(true);
		}

		//// re -add with the proper value now
		//
		// otherwise, copy it over if still in tree
		gbmemcpy ( dst , p , recSize );
		int32_t newOffset = dst - pstart;
		// store in map, overwrite old value of 1
		ht.addKey ( &oldOffset , &newOffset );
		dst += recSize;
		p += recSize;
	}

	int32_t inUseNew = dst - pstart;

	// update mem class as well
	m_mem.m_ptr1 = dst;

	// how much did we reclaim
	int32_t reclaimed = inUseOld - inUseNew;

	if ( reclaimed < 0 ) { g_process.shutdownAbort(true); }
	if ( inUseNew  < 0 ) { g_process.shutdownAbort(true); }
	if ( inUseNew  > m_mem.m_memSize ) { g_process.shutdownAbort(true); }

	// now update data ptrs in the tree, m_data[]
	for ( int i = 0 ; i < nn ; i++ ) {
		// skip empty nodes in tree
		if ( m_tree.isEmpty(i)) continue;
		// update the data otherwise
		char *data = m_tree.getData(i);
		// sanity, ensure legit
		if ( data < pstart ) { g_process.shutdownAbort(true); }
		int32_t offset = data - pstart;
		int32_t *newOffsetPtr = (int32_t *)ht.getValue ( &offset );
		if ( ! newOffsetPtr ) { g_process.shutdownAbort(true); }
		char *newData = pstart + *newOffsetPtr;
		m_tree.setData(i, newData);
	}

	log("rdb: reclaimed %" PRId32" bytes after scanning %" PRId32" "
	    "undeleted nodes and %" PRId32" deleted nodes for doledb"
	    ,reclaimed,nn,marked);

	// return # of bytes of mem we reclaimed
	return reclaimed;
}
