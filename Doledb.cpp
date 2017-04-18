#include "Doledb.h"
#include "SpiderCache.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"


Doledb g_doledb;


/////////////////////////
/////////////////////////      DOLEDB
/////////////////////////

// reset rdb
void Doledb::reset() { m_rdb.reset(); }

bool Doledb::init ( ) {
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
	{
		RdbCacheLock rcl(g_spiderLoop.m_winnerListCache);
		g_spiderLoop.m_winnerListCache.clear ( collnum );
	}

	// . nuke doledb for this collnum
	// . it will unlink the files and maps for doledb for this collnum
	// . it will remove all recs of this collnum from its tree too
	if (g_doledb.getRdb()->isSavingTree()) {
		g_loop.registerSleepCallback(100, &collnum, nukeDoledbWrapper);
		return;
	}

	// . ok, tree is not saving, it should complete entirely from this call
	g_doledb.getRdb()->deleteAllRecs(collnum);

	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
	if ( sc ) {
		// . make sure to nuke m_doleIpTable as well
		sc->clearDoleIpTable();
		// need to recompute this!
		//sc->m_ufnMapValid = false;

		// log it
		log("spider: rebuilding %s from doledb nuke", sc->getCollName());
		// activate a scan if not already activated
		sc->m_waitingTreeNeedsRebuild = true;
		// if a scan is ongoing, this will re-set it
		sc->resetWaitingTreeNextKey();
		// clear it?
		sc->m_waitingTree.clear();
		sc->m_waitingTable.clear();
		// kick off the spiderdb scan to rep waiting tree and doledb
		sc->populateWaitingTreeFromSpiderdb(false);
	}

	// note it
	log("spider: finished nuking doledb for coll (%" PRId32")",
	    (int32_t)collnum);
}
