#include "HostFlags.h"
#include "SpiderLoop.h"
#include "Process.h"
#include "Rebalance.h"
#include "Repair.h"
#include "DailyMerge.h"


extern bool g_recoveryMode; //over in main.cpp


int getOurHostFlags() {
	// flags indicating our state
	int32_t flags = 0;
	// let others know we are doing our daily merge and have turned off
	// our spiders. when host #0 indicates this state it will wait
	// for all other hosts to enter the mergeMode. when other hosts
	// receive this state from host #0, they will start their daily merge.
	if ( g_spiderLoop.getNumSpidersOut() > 0 ) flags |= PFLAG_HASSPIDERS;
	if ( g_process.isRdbMerging()         ) flags |= PFLAG_MERGING;
	if ( g_process.isRdbDumping()         ) flags |= PFLAG_DUMPING;
	if ( g_rebalance.m_isScanning         ) flags |= PFLAG_REBALANCING;
	if ( g_recoveryMode                   ) flags |= PFLAG_RECOVERYMODE;
	if ( g_rebalance.m_numForeignRecs     ) flags |= PFLAG_FOREIGNRECS;
	if ( g_dailyMerge.m_mergeMode    == 0 ) flags |= PFLAG_MERGEMODE0;
	if ( g_dailyMerge.m_mergeMode ==0 || g_dailyMerge.m_mergeMode == 6 )
		flags |= PFLAG_MERGEMODE0OR6;
	return flags;
}
