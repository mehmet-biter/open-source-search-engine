#ifndef HOSTFLAGS_H_
#define HOSTFLAGS_H_

// for the Host::m_flags
#define PFLAG_HASSPIDERS     0x01
#define PFLAG_MERGING        0x02
#define PFLAG_DUMPING        0x04
// these two flags are used by DailyMerge.cpp to sync the daily merge
// between all the hosts in the cluster
#define PFLAG_MERGEMODE0     0x08
#define PFLAG_MERGEMODE0OR6  0x10
#define PFLAG_REBALANCING    0x20
#define PFLAG_FOREIGNRECS    0x40
#define PFLAG_RECOVERYMODE   0x80


int getOurHostFlags();

#endif
