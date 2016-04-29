// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef GB_MSG4_H
#define GB_MSG4_H

bool registerHandler4   ( ) ;
bool saveAddsInProgress ( const char *filenamePrefix );
bool loadAddsInProgress ( const char *filenamePrefix );
// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue     ( ) ;

//#include "RdbList.h"


bool isInMsg4LinkedList ( class Msg4 *msg4 ) ;

#include "SafeBuf.h"

class Msg4 {

 public:
	// meta list format =
	// (rdbId | 0x08) then rdb record [if nosplit]
	// (rdbId | 0x00) then rdb record [if split  ]
	bool addMetaList ( const char *metaList                 ,
			   int32_t  metaListSize             ,
			   char *coll                     ,
			   void *state                    ,
			   void (* callback)(void *state) ,
			   int32_t  niceness                 ,
			   char  rdbId = -1               );


	bool addMetaList ( class SafeBuf *sb ,
			   collnum_t  collnum                  ,
			   void      *state                    ,
			   void      (* callback)(void *state) ,
			   int32_t       niceness                 ,
			   char       rdbId = -1               ,
			   int32_t       shardOverride = -1       );

	// this one is faster...
	// returns false if blocked
	bool addMetaList ( const char      *metaList                 ,
			   int32_t       metaListSize             ,
			   collnum_t  collnum                  ,
			   void      *state                    ,
			   void      (* callback)(void *state) ,
			   int32_t       niceness                 ,
			   char       rdbId = -1               ,
			   int32_t       shardOverride = -1       );

	bool addMetaList2 ( );

	Msg4() { m_inUse = false; };
	// why wasn't this saved in addsinprogress.dat file?
	~Msg4() { if ( m_inUse ) log("BAD: MSG4 in use!!!!!!"); };

	// private:

	void         (*m_callback ) ( void *state );
	void          *m_state;

	SafeBuf m_tmpBuf;

	char      m_rdbId;
	char      m_inUse;
	collnum_t m_collnum;
	int32_t      m_niceness;

	int32_t m_shardOverride;

	const char *m_metaList     ;
	int32_t  m_metaListSize ;
	const char *m_currentPtr   ; // into m_metaList

	// the linked list for waiting in line
	class Msg4 *m_next;
};

// returns false if blocked and callback will be called when flush is done
bool flushMsg4Buffers ( void *state , void (* callback) (void *) ) ;

#endif // GB_MSG4_H
