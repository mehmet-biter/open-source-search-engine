#ifndef GB_PAGEREINDEX_H
#define GB_PAGEREINDEX_H

#include "Msg4Out.h"
#include "SafeBuf.h"

// . for adding docid-based spider requests to spiderdb
// . this is the original method, for queuing up docid-based spider requests
class Msg1c {

public:

	Msg1c();

	bool reindexQuery ( char *query ,
			    collnum_t collnum, // char *coll  ,
			    int32_t startNum ,
			    int32_t endNum ,
			    bool forceDel ,
			    int32_t langId,
			    void *state ,
			    void (* callback) (void *state ) ) ;
	
	bool gotList ( );

	collnum_t m_collnum;
	int32_t m_startNum;
	int32_t m_endNum;
	bool m_forceDel;
	void *m_state;
	void (* m_callback) (void *state);
	int32_t m_niceness;
	Msg3a m_msg3a;
	Msg4 m_msg4;
	SafeBuf m_sb;
	int32_t m_numDocIds;
	int32_t m_numDocIdsAdded;
	Query  m_qq;
};

#endif // GB_PAGEREINDEX_H
