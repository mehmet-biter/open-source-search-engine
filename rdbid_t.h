#ifndef RDBID_T_H_
#define RDBID_T_H_


// . each Rdb instance has an ID
// . these ids are also return values for getIdFromRdb()
enum rdbid_t {
	RDB_NONE = 0,
	RDB_TAGDB = 1,
	// RDB_INDEXDB = 2,
	RDB_TITLEDB = 3,
	// RDB_SECTIONDB = 4,
	// RDB_SYNCDB = 5,
	RDB_SPIDERDB = 6,
	RDB_DOLEDB = 7,
	// RDB_TFNDB = 8,
	RDB_CLUSTERDB = 9,
	// RDB_CATDB = 10,
	// RDB_DATEDB = 11,
	RDB_LINKDB = 12,
	RDB_STATSDB = 13,
	// RDB_PLACEDB = 14,
	// RDB_REVDB = 15,
	RDB_POSDB = 16,
	// RDB_CACHEDB = 17,
	// RDB_SERPDB = 18,
	// RDB_MONITORDB = 19,
	// RDB_PARMDB = 20, // kind of a fake rdb for modifying collrec/g_conf parms

// . secondary rdbs for rebuilding done in PageRepair.cpp
// . we add new recs into these guys and then make the original rdbs
//   point to them when we are done.
	// RDB2_INDEXDB2 = 21,
	RDB2_TITLEDB2 = 22,
	// RDB2_SECTIONDB2 = 23,
	RDB2_SPIDERDB2 = 24,
	// RDB2_TFNDB2 = 25,
	RDB2_CLUSTERDB2 = 26,
	// RDB2_DATEDB2 = 27,
	RDB2_LINKDB2 = 28,
	// RDB2_PLACEDB2 = 29,
	// RDB2_REVDB2 = 30,
	RDB2_TAGDB2 = 31,
	RDB2_POSDB2 = 32,
	// RDB2_CATDB2 = 33,
	RDB_END
};


#endif //RDBID_T_H_
