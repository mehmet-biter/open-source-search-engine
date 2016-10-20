// Copyright Gigablast, Inc. Apr 2008

// tight merge indexdb at the given time every day

#ifndef GB_DAILYMERGE_H
#define GB_DAILYMERGE_H

#include <ctime>

class CollectionRec;

class DailyMerge {
public:

	bool init();

	// is the scan active and adding recs to the secondary rdbs?
	bool isMergeActive() { return (m_mergeMode >= 1); }

	void dailyMergeLoop ( ) ;

	CollectionRec *m_cr;
	char           m_mergeMode;
	bool           m_spideringEnabled;
//	char           m_injectionEnabled;
	bool           m_didDaily;
	time_t         m_savedStartTime;
};

// the global class
extern DailyMerge g_dailyMerge;

#endif // GB_DAILYMERGE_H
