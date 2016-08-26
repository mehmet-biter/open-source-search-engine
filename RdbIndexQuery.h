#ifndef GB_RDBINDEXQUERY_H
#define GB_RDBINDEXQUERY_H

#include <memory>
#include <vector>
#include <stdint.h>
#include "RdbIndex.h"

class RdbBase;

class RdbIndexQuery {
public:
	RdbIndexQuery(RdbBase *base, int filePos);
	~RdbIndexQuery();

	int32_t getFilePos(uint64_t docId) const;

private:
	RdbIndexQuery();
	RdbIndexQuery(const RdbIndexQuery&);
	RdbIndexQuery& operator=(const RdbIndexQuery&);

	RdbIndexQuery(docidsconst_ptr_t globalIndexData, docidsconst_ptr_t treeIndexData, int32_t numFiles);

	docidsconst_ptr_t m_globalIndexData;
	docidsconst_ptr_t m_treeIndexData;
	int32_t m_numFiles;
};

#endif // GB_RDBINDEXQUERY_H
