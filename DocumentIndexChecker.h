#ifndef GB_DOCUMENT_INDEX_CHECKER_H_
#define GB_DOCUMENT_INDEX_CHECKER_H_

#include "RdbIndexQuery.h"

//Thin wrapper around RdbIndexQuery so PosdbTable doesn't have to know about indexes and file numbers
class DocumentIndexChecker : public RdbIndexQuery {
	int32_t fileNum;
public:
	DocumentIndexChecker(RdbBase *base)
	  : RdbIndexQuery(base),
	    fileNum(-1)
	  {}
	
	void setFileNum(int32_t fileNum) {
		this->fileNum = fileNum;
	}
	
	bool exists(int64_t docId) const {
		return documentIsInFile(docId,fileNum);
	}
};

#endif
