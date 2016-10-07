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

int32_t RdbIndexQuery::getFilePos(uint64_t docId, bool *isDel) const {
	if (m_treeIndexData.get()) {
		auto it = std::lower_bound(m_treeIndexData->cbegin(), m_treeIndexData->cend(), docId << RdbIndex::s_docIdOffset);
		if (it != m_treeIndexData->cend() && ((*it >> RdbIndex::s_docIdOffset) == docId)) {
			if (isDel) {
				*isDel = ((*it & RdbIndex::s_delBitMask) == 0);
			}
			return m_numFiles;
		}
	}

	auto it = std::lower_bound(m_globalIndexData->cbegin(), m_globalIndexData->cend(), docId << RdbBase::s_docIdFileIndex_docIdWithDelKeyOffset);
	if (it != m_globalIndexData->cend() && ((*it >> RdbBase::s_docIdFileIndex_docIdOffset) == docId)) {
		if (isDel) {
			*isDel = ((*it & RdbBase::s_docIdFileIndex_delBitMask) == 0);
		}
		return static_cast<int32_t>(*it & RdbBase::s_docIdFileIndex_filePosMask);
	}

	// mismatch in idx & data files?
	logError("Unable to find docId=%lu in global index", docId);
	gbshutdownLogicError();
}

void RdbIndexQuery::printIndex() const {
	if (m_treeIndexData.get()) {
		for (auto key : *m_treeIndexData) {
			logf(LOG_TRACE, "db: docId=%" PRId64" index=%" PRId32" isDel=%d",
			     key >> RdbIndex::s_docIdOffset,
			     m_numFiles,
			     ((key & RdbIndex::s_delBitMask) == 0));
		}
	}

	for (auto key : *m_globalIndexData) {
		logf(LOG_TRACE, "db: docId=%" PRId64" index=%" PRId64" isDel=%d",
		     (key & RdbBase::s_docIdFileIndex_docIdMask) >> RdbBase::s_docIdFileIndex_docIdOffset,
		     key & RdbBase::s_docIdFileIndex_filePosMask,
		     ((key & RdbBase::s_docIdFileIndex_delBitMask) == 0));
	}
}