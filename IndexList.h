// Matt Wells, copyright Jun 2001

// . IndexList is a list of keys 
// . some keys are 12 bytes, some are 6 bytes (compressed)
// . see Indexdb.h for format of the keys

// . you can set from a TitleRec/SiteRec pair
// . you can set from a TermTable
// . we use this class to generate the final indexList for a parsed document
// . we have to do some #include's in the .cpp cuz the TitleRec contains an
//   IndexList for holding indexed link Text
// . the TermTable has an addIndexList() function 
// . we map 32bit scores from a TermTable to 8bits scores by taking the log
//   of the score if it's >= 128*256, otherwise, we keep it as is


// override all funcs in RdbList where m_useShortKeys is true...
// skipCurrentRec() etc need to use m_useHalfKeys in RdbList cuz
// that is needed by many generic routines, like merge_r, RdbMap, Msg1, Msg22..
// We would need to make it a virtual function which would slow things down...
// or make those classes have specialized functions for IndexLists... in 
// addition to the RdbLists they already support

#ifndef GB_INDEXLIST_H
#define GB_INDEXLIST_H

#include "RdbList.h"
#include "Titledb.h"

class IndexList : public RdbList {

 public:

	// these 2 assume 12 and 6 byte keys respectively
	int64_t getCurrentDocId () {
		if ( isHalfBitOn ( m_listPtr ) ) return getDocId6 (m_listPtr);
		else                             return getDocId12(m_listPtr);
	}
	int64_t getDocId12 ( char *rec ) {
		return ((*(uint64_t *)(rec)) >> 2) & DOCID_MASK; }
	int64_t getDocId6 ( char *rec ) {
		int64_t docid;
		*(int32_t *)(&docid) = *(int32_t *)(void*)rec;
		((char *)&docid)[4] = rec[4];
		docid >>= 2;
		return docid & DOCID_MASK;
	}
};

#endif // GB_INDEXLIST_H
