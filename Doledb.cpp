#include "Doledb.h"
#include "SpiderCache.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "ScopedLock.h"
#include "Collectiondb.h"
#include "Conf.h"
#include "Loop.h"


Doledb g_doledb;


static void nukeAllDoledbsPeriodically(int, void *);

/////////////////////////
/////////////////////////      DOLEDB
/////////////////////////

// reset rdb
void Doledb::reset() { m_rdb.reset(); }

bool Doledb::init ( ) {
	if(g_conf.m_doledbNukeInterval>0) {
		log(LOG_INFO,"spider: nuking Doledb periodically is enabled, interval = %d seconds", g_conf.m_doledbNukeInterval);
		g_loop.registerSleepCallback(g_conf.m_doledbNukeInterval*1000, NULL, nukeAllDoledbsPeriodically, "nukeAllDoledbsPeriodically");
	} else
		log(LOG_INFO,"@spider: nuking Doledb periodically is disabled");
	
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	// . use 5MB for the tree
	int32_t maxTreeMem    = 150000000; // 150MB
	int32_t maxTreeNodes  = maxTreeMem / 78;

	// initialize our own internal rdb
	return m_rdb.init ( "doledb"                    ,
			    -1                          , // fixedDataSize
			    2                           , // MinFilesToMerge
			    maxTreeMem                  ,
			    maxTreeNodes                ,
			    false,                         // half keys?
			    12,             // key size
			    false);         //useIndexFile
}

//
// remove all recs from doledb for the given collection
//
static void nukeDoledbWrapper ( int fd , void *state ) {
	g_loop.unregisterSleepCallback ( state , nukeDoledbWrapper );
	collnum_t collnum = *(collnum_t *)state;
	nukeDoledb ( collnum );
}

void nukeDoledb ( collnum_t collnum ) {
	// in case we changed url filters for this collection #
	g_spiderLoop.nukeWinnerListCache(collnum);

	// . nuke doledb for this collnum
	// . it will unlink the files and maps for doledb for this collnum
	// . it will remove all recs of this collnum from its tree too
	if (g_doledb.getRdb()->isSavingTree()) {
		g_loop.registerSleepCallback(100, &collnum, nukeDoledbWrapper, "Doledb::nukeDoledbWrapper");
		return;
	}

	// . ok, tree is not saving, it should complete entirely from this call
	g_doledb.getRdb()->deleteAllRecs(collnum);

	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
	if ( sc ) {
		// . make sure to nuke m_doledbIpTable as well
		sc->clearDoledbIpTable();
		// need to recompute this!
		//sc->m_ufnMapValid = false;

		{
			ScopedLock sl(sc->m_waitingTree.getLock());

			// log it
			log("spider: rebuilding %s from doledb nuke", sc->getCollName());
			// activate a scan if not already activated
			sc->m_waitingTreeNeedsRebuild = true;
			// if a scan is ongoing, this will re-set it
			sc->resetWaitingTreeNextKey();
			// clear it?
			sc->m_waitingTree.clear_unlocked();
			sc->clearWaitingTable();
		}
		// kick off the spiderdb scan to rep waiting tree and doledb
		sc->populateWaitingTreeFromSpiderdb(false);
	}

	// note it
	log("spider: finished nuking doledb for coll (%" PRId32")",
	    (int32_t)collnum);
}


static void nukeAllDoledbsWrapper(int /*fd*/, void *state) {
	g_loop.unregisterSleepCallback(state, nukeAllDoledbsWrapper);
	nukeAllDoledbs();
}

void nukeAllDoledbs() {
	log(LOG_INFO,"spider: Beginning nuking all doledbs");
	if (g_doledb.getRdb()->isSavingTree()) {
		g_loop.registerSleepCallback(100, NULL, nukeAllDoledbsWrapper, "nukeAllDoledbsWrapper");
		return;
	}
	
	for(collnum_t collnum=g_collectiondb.getFirstCollnum();
	    collnum<g_collectiondb.getNumRecs();
	    collnum++)
	{
		if(g_collectiondb.getRec(collnum)) {
			// in case we changed url filters for this collection #
			g_spiderLoop.nukeWinnerListCache(collnum);
			
			// . nuke doledb for this collnum
			// . it will unlink the files and maps for doledb for this collnum
			// . it will remove all recs of this collnum from its tree too
			g_doledb.getRdb()->deleteAllRecs(collnum);

			SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
			if ( sc ) {
				// . make sure to nuke m_doledbIpTable as well
				sc->clearDoledbIpTable();
				// need to recompute this!
				//sc->m_ufnMapValid = false;

				{
					ScopedLock sl(sc->m_waitingTree.getLock());

					// log it
					log("spider: rebuilding %s from doledb nuke", sc->getCollName());
					// activate a scan if not already activated
					sc->m_waitingTreeNeedsRebuild = true;
					// if a scan is ongoing, this will re-set it
					sc->resetWaitingTreeNextKey();
					// clear it?
					sc->m_waitingTree.clear_unlocked();
					sc->clearWaitingTable();
				}
				// kick off the spiderdb scan to rep waiting tree and doledb
				sc->populateWaitingTreeFromSpiderdb(false);
			}
		}
	}

	// note it
	log(LOG_INFO,"spider: finished nuking all doledbs");
}


//Nuking doledb+waitingtree shouldn't really be necessary but the code handling spiderdb+doledb+doledbiptable+waitingtree+waitingtable+doledbiptable isn't 100% error-free
//so we sometimes end up with lost records and priority inversions. The easiest solution for this right now is to periodically nuke all doledbs.
static void nukeAllDoledbsPeriodically(int, void *) {
	nukeAllDoledbs();
}
