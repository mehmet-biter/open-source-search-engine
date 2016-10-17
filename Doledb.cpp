#include "gb-include.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"


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
			    false                       ); // half keys?
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

	//g_spiderLoop.m_winnerListCache.verify();	
	// in case we changed url filters for this collection #
	{
		RdbCacheLock rcl(g_spiderLoop.m_winnerListCache);
		g_spiderLoop.m_winnerListCache.clear ( collnum );
	}

	//g_spiderLoop.m_winnerListCache.verify();	

	//WaitEntry *we = (WaitEntry *)state;

	//if ( we->m_registered )
	//	g_loop.unregisterSleepCallback ( we , doDoledbNuke );

	// . nuke doledb for this collnum
	// . it will unlink the files and maps for doledb for this collnum
	// . it will remove all recs of this collnum from its tree too
	if ( g_doledb.getRdb()->isSavingTree () ) {
		g_loop.registerSleepCallback(100,&collnum,nukeDoledbWrapper);
		//we->m_registered = true;
		return;
	}

	// . ok, tree is not saving, it should complete entirely from this call
	g_doledb.getRdb()->deleteAllRecs ( collnum );

	// re-add it back so the RdbBase is new'd
	//g_doledb.getRdb()->addColl2 ( we->m_collnum );

	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull ( collnum );

	if ( sc ) {
		sc->m_lastUrlFiltersUpdate = getTimeLocal();//GlobalNoCore();
		// . make sure to nuke m_doleIpTable as well
		sc->m_doleIpTable.clear();
		// need to recompute this!
		//sc->m_ufnMapValid = false;

		// log it
		log("spider: rebuilding %s from doledb nuke",
		    sc->getCollName());
		// activate a scan if not already activated
		sc->m_waitingTreeNeedsRebuild = true;
		// if a scan is ongoing, this will re-set it
		sc->m_nextKey2.setMin();
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



