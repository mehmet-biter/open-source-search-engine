#ifndef GB_RDBINDEXQUERY_H
#define GB_RDBINDEXQUERY_H

#include "RdbIndex.h"
#include <stdint.h>

class RdbBase;

class RdbIndexQuery {
public:
	RdbIndexQuery(RdbBase *base);
	~RdbIndexQuery();

	int32_t getFilePos(uint64_t docId, bool isMerging) const;
	bool documentIsInFile(uint64_t docId, int32_t filenum) const;

	int32_t getNumFiles() const { return m_numFiles; }
	bool hasPendingGlobalIndexJob() const { return m_hasPendingGlobalIndexJob; }

	void printIndex() const;

private:
	RdbIndexQuery();
	RdbIndexQuery(const RdbIndexQuery&);
	RdbIndexQuery& operator=(const RdbIndexQuery&);

	RdbIndexQuery(docidsconst_ptr_t globalIndexData, docidsconst_ptr_t treeIndexData, int32_t numFiles, bool hasPendingGlobalIndexJob);

	docidsconst_ptr_t m_globalIndexData;
	docidsconst_ptr_t m_treeIndexData;
	int32_t m_numFiles;
	bool m_hasPendingGlobalIndexJob;
};

#endif // GB_RDBINDEXQUERY_H
