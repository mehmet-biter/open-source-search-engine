#include "RdbIndexQuery.h"
#include "RdbBase.h"
#include <algorithm>

RdbIndexQuery::RdbIndexQuery(RdbBase *base)
	: RdbIndexQuery(base->getGlobalIndex() ? base->getGlobalIndex() : docidsconst_ptr_t(),
	                base->getTreeIndex() ? base->getTreeIndex()->getDocIds() : docidsconst_ptr_t(),
	                base->getNumFiles()) {
}

RdbIndexQuery::RdbIndexQuery(docidsconst_ptr_t globalIndexData, docidsconst_ptr_t treeIndexData, int32_t numFiles)
	: m_globalIndexData(globalIndexData)
	, m_treeIndexData(treeIndexData)
	, m_numFiles(numFiles) {
}

RdbIndexQuery::~RdbIndexQuery() {
}

int32_t RdbIndexQuery::getFilePos(uint64_t docId) const {
	if (m_treeIndexData.get() && std::binary_search(m_treeIndexData->begin(), m_treeIndexData->end(), docId)) {
		return m_numFiles;
	}

	auto it = std::lower_bound(m_globalIndexData->cbegin(), m_globalIndexData->cend(), docId << 24);
	if ((*it >> 24) == docId) {
		return static_cast<int32_t>(*it & 0xffff);
	}

	// mismatch in idx & data files?
	logError("Unable to find docId=%lu in global index", docId);
	gbshutdownLogicError();
}
