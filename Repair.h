// Copyright Gigablast, Inc. Mar 2007

#ifndef GB_REPAIR_H
#define GB_REPAIR_H

#include "RdbList.h"
#include "Msg5.h"
#include "Msg1.h"
#include "Msg4.h"
#include "Tagdb.h"

#define SR_BUFSIZE 2048

extern char g_repairMode;

class Repair {
public:

	Repair();

	// is the scan active and adding recs to the secondary rdbs?
	bool isRepairActive() ;

	bool init();
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
	//bool gotTfndbList ( );
	//bool getTagRec ( void **state ) ;
	bool getTitleRec ( );
	bool injectTitleRec ( ) ; // TitleRec *tr );
	//bool computeRecs ( );
	//bool getRootQuality ( );
	//bool addToSpiderdb2 ( ) ; 
	//bool addToTfndb2 ( ); 
	//bool addToClusterdb2 ( );
	//bool addToIndexdb2 ( );
	//bool addToIndexdb2b( );
	//bool addToDatedb2 ( );
	//bool addToTitledb2 ( );
	//bool addToLinkdb2 ( );

	// spiderdb scan functions
	//bool scanSpiderdb ( );
	//bool getTfndbListPart2 ( );
	//bool getTagRecPart2 ( );
	//bool getRootQualityPart2 ( );
	//bool addToSpiderdb2Part2 ( );
	//bool addToTfndb2Part2 ( );

	// called by Pages.cpp
	bool printRepairStatus ( SafeBuf *sb , int32_t fromIp );

	// if we core, call these so repair can resume where it left off
	bool save();
	bool load();

	bool       m_completed;

	// general scan vars
	Msg5       m_msg5;
	Msg4       m_msg4;
	bool       m_needsCallback;
	char       m_docQuality;
	RdbList    m_titleRecList;
	int64_t  m_docId;
	char       m_isDelete;
	RdbList    m_ulist;
	RdbList    m_addlist;
	int64_t  m_totalMem;
	int32_t       m_stage ;
	int32_t       m_tfn;
	int32_t       m_count;
	bool       m_updated;

	// titledb scan vars
	key96_t      m_nextTitledbKey;
	key96_t      m_nextSpiderdbKey;
	key96_t      m_nextPosdbKey;
	key128_t   m_nextLinkdbKey;
	key96_t      m_endKey;
	int64_t  m_uh48;
	int32_t       m_priority;
	uint64_t   m_contentHash;
	key96_t      m_clusterdbKey ;
	key96_t      m_spiderdbKey;
	char       m_srBuf[SR_BUFSIZE];
	char       m_tmpBuf[32];
	RdbList    m_linkdbListToAdd;
	uint64_t   m_chksum1LongLong;

	// spiderdb scan vars
	bool       m_isNew;
	TagRec     m_tagRec;


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
	int64_t  m_recsOutOfOrder;
	int64_t  m_recsetErrors;
	int64_t  m_recsCorruptErrors;
	int64_t  m_recsXmlErrors;
	int64_t  m_recsDupDocIds;
	int64_t  m_recsNegativeKeys;
	int64_t  m_recsOverwritten;
	int64_t  m_recsUnassigned;
	int64_t  m_noTitleRecs;
	int64_t  m_recsWrongGroupId;
	int64_t  m_recsRoot;
	int64_t  m_recsNonRoot;
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
	//int32_t       m_collLen;
	collnum_t  m_collnum;
	char       m_newColl[MAX_COLL_LEN];
	int32_t       m_newCollLen;
	collnum_t  m_newCollnum;

	// . m_colli is the index into m_colls
	// . m_colli is the index into g_collectiondb.m_recs if the list
	//   of collections to repair was empty
	int32_t       m_colli;

	// list of collections to repair, only valid of g_conf.m_collsToRepair
	// is not empty
	int32_t       m_collOffs[100];
	int32_t       m_collLens[100];
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
};

// the global class
extern Repair g_repair;

#endif // GB_REPAIR_H
