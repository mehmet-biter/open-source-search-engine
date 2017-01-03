// Copyright Gigablast, Inc. Mar 2007

#ifndef GB_REPAIR_H
#define GB_REPAIR_H

#include "RdbList.h"
#include "Msg5.h"
#include "repair_mode.h"

#define SR_BUFSIZE 2048


class XmlDoc;
class CollectionRec;


class Repair {
public:

	Repair();

	// is the scan active and adding recs to the secondary rdbs?
	bool isRepairActive() const;
	bool isRepairingColl(collnum_t coll) const { return m_collnum==coll; }

	bool init();
	// if we core, call this so repair can resume where it left off
	bool save();

	// called by Parms.cpp
	bool printRepairStatus(SafeBuf *sb);

	bool linkdbRebuildPending() const { return m_rebuildLinkdb; }

private:
	//void allHostsReady();
	void initScan();
	void resetForNewCollection();
	void getNextCollToRepair();
	bool loop( void *state = NULL );
	bool dumpLoop();
	void resetSecondaryRdbs();
	bool dumpsCompleted();
	void updateRdbs ( ) ;

	// titledbscan functions
	bool scanRecs();
	bool gotScanRecList ( );
	bool getTitleRec ( );
	bool injectTitleRec ( ) ; // TitleRec *tr );


	bool load();

	// general scan vars
	Msg5       m_msg5;
	bool       m_needsCallback;
	RdbList    m_titleRecList;
	int64_t  m_docId;
	int64_t  m_totalMem;
	int32_t       m_stage ;
	int32_t       m_count;
	bool       m_updated;

	// titledb scan vars
	key96_t      m_nextTitledbKey;
	key96_t      m_endKey;

	// . state info
	// . indicator of what we save to disk
	char       m_SAVE_START;
	int64_t  m_lastDocId;
	int64_t  m_prevDocId;
	bool       m_completedFirstScan  ;
	bool       m_completedSpiderdbScan ;
	key96_t      m_lastTitledbKey;
	key96_t      m_lastSpiderdbKey;

	int64_t  m_recsScanned;
	int64_t  m_recsetErrors;
	int64_t  m_recsCorruptErrors;
	int64_t  m_recsDupDocIds;
	int64_t  m_recsNegativeKeys;
	int64_t  m_recsUnassigned;
	int64_t  m_recsWrongGroupId;
	int64_t  m_recsInjected;

	// spiderdb scan stats
	int32_t       m_spiderRecsScanned  ;
	int32_t       m_spiderRecSetErrors ;
	int32_t       m_spiderRecNotAssigned ;
	int32_t       m_spiderRecBadTLD      ;

	// generic scan parms
	bool       m_rebuildTitledb    ;
	bool       m_rebuildPosdb    ;
	bool       m_rebuildClusterdb  ;
	bool       m_rebuildSpiderdb   ;
	bool       m_rebuildSitedb     ;
	bool       m_rebuildLinkdb     ;
	bool       m_rebuildTagdb      ;
	bool       m_fullRebuild       ;

	bool       m_rebuildRoots      ;
	bool       m_rebuildNonRoots   ;

	// current collection being repaired
	collnum_t  m_collnum;

	// . m_colli is the index into m_colls
	// . m_colli is the index into g_collectiondb.m_recs if the list
	//   of collections to repair was empty
	int32_t       m_colli;

	// list of collections to repair, only valid of g_conf.m_collsToRepair
	// is not empty
	static const int32_t maxCollections = 100;
	int32_t       m_collOffs[maxCollections];
	int32_t       m_collLens[maxCollections];
	int32_t       m_numColls;
	// end the stuff to be saved
	char       m_SAVE_END;

	// i'd like to save these but they are ptrs
	CollectionRec *m_cr;

	//for timing a repair process
	int64_t  m_startTime;

	// if repairing is disabled in the middle of a repair
	bool       m_isSuspended;

	// keep track of how many injects we have out
	int32_t       m_numOutstandingInjects;
	bool       m_allowInjectToLoop;

	// sanity check
	bool  m_msg5InUse ;

	bool  m_saveRepairState;

	bool  m_isRetrying;

	static void repairWrapper(int fd, void *state);
	static void loopWrapper(void *state, RdbList *list, Msg5 *msg5);

	static bool saveAllRdbs(void *state, void (*callback)(void *state));
	static bool anyRdbNeedsSave();
	static void doneSavingRdb(void *state);
	static void doneWithIndexDoc(XmlDoc *xd);
	static void doneWithIndexDocWrapper(void *state);
};

// the global class
extern Repair g_repair;

#endif // GB_REPAIR_H
