//
// Created by alc on 4/5/17.
//

#include "SpiderCache.h"
#include "SpiderColl.h"
#include "RdbTree.h"
#include "ScopedLock.h"
#include "Collectiondb.h"
#include "Mem.h"

/////////////////////////
/////////////////////////      SpiderCache
/////////////////////////


// . reload everything this many seconds
// . this was originally done to as a lazy compensation for a bug but
//   now i do not add too many of the same domain if the same domain wait
//   is ample and we know we'll be refreshed in X seconds anyway
//#define DEFAULT_SPIDER_RELOAD_RATE (3*60*60)


// for caching in s_ufnTree
//#define MAX_NODES (30)

// a global class extern'd in .h file
SpiderCache g_spiderCache;

SpiderCache::SpiderCache ( ) {
	//m_numSpiderColls   = 0;
	//m_isSaving = false;
}

// returns false and set g_errno on error
bool SpiderCache::init ( ) {

	//for ( int32_t i = 0 ; i < MAX_COLL_RECS ; i++ )
	//	m_spiderColls[i] = NULL;

	// success
	return true;
}

// return false if any tree save blocked
void SpiderCache::save ( bool useThread ) {
	// bail if already saving
	//if ( m_isSaving ) return true;
	// assume saving
	//m_isSaving = true;
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		RdbTree *tree = &sc->m_waitingTree;
		if ( ! tree->needsSave() ) continue;
		// if already saving from a thread
		if ( tree->isSaving() ) continue;
		char dir[1024];
		sprintf(dir,"%scoll.%s.%" PRId32,g_hostdb.m_dir,
			sc->m_coll,(int32_t)sc->m_collnum);
		// log it for now
		log("spider: saving waiting tree for cn=%" PRId32,(int32_t)i);
		// returns false if it blocked, callback will be called
		tree->fastSave(dir, useThread, NULL, NULL);
	}
}

bool SpiderCache::needsSave ( ) {
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		if ( sc->m_waitingTree.needsSave() ) return true;
	}
	return false;
}

void SpiderCache::reset ( ) {
	log(LOG_DEBUG,"spider: resetting spidercache");
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.getNumRecs(); i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		ScopedLock sl(cr->m_spiderCollMutex);
		SpiderColl *sc = cr->m_spiderColl;
		if ( ! sc ) continue;
		sc->reset();
		mdelete ( sc , sizeof(SpiderColl) , "SpiderColl" );
		delete ( sc );
		cr->m_spiderColl = NULL;
	}
}

SpiderColl *SpiderCache::getSpiderCollIffNonNull ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	if ( collnum >= g_collectiondb.getNumRecs()) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	// empty?
	if ( ! cr ) return NULL;
	// return it if non-NULL
	ScopedLock sl(cr->m_spiderCollMutex); //not really needed but shuts up helgrind+drd
	return cr->m_spiderColl;
}

// . get SpiderColl for a collection
// . if it is NULL for that collection then make a new one
SpiderColl *SpiderCache::getSpiderColl ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;

	// shortcut
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	// collection might have been reset in which case collnum changes
	if ( ! cr ) return NULL;
	ScopedLock sl(cr->m_spiderCollMutex);
	// return it if non-NULL
	SpiderColl *sc = cr->m_spiderColl;
	if ( sc ) return sc;

	// make it
	try { sc = new SpiderColl(cr); }
	catch ( ... ) {
		log("spider: failed to make SpiderColl for collnum=%" PRId32,
			(int32_t)collnum);
		return NULL;
	}
	// register it
	mnew ( sc , sizeof(SpiderColl), "SpiderColl" );
	// store it
	cr->m_spiderColl = sc;
	// note it
	logf(LOG_DEBUG,"spider: made spidercoll=%" PTRFMT" for cr=%" PTRFMT"",
		(PTRTYPE)sc,(PTRTYPE)cr);

	// note it!
	log(LOG_DEBUG,"spider: adding new spider collection for %s", cr->m_coll);
	// that was it
	return sc;
}