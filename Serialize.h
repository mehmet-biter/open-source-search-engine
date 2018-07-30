#ifndef SERIALIZE_H_
#define SERIALIZE_H_

#include <inttypes.h>

// 
// these three functions replace the Msg.cpp/.h class
//
// actually "lastParm" point to the thing right after the lastParm
int32_t getMsgStoredSize(int32_t baseSize,
			 const int32_t *firstSizeParm,
			 const int32_t *lastSizeParm);
// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg(int32_t             baseSize,
		   const int32_t      *firstSizeParm,
		   const int32_t      *lastSizeParm,
		   const char * const *firstStrPtr,
		   const void         *thisPtr,
		   int32_t            *retSize,
		   char               *userBuf,
		   int32_t             userBufSize);
char *serializeMsg ( int32_t  baseSize,
		     int32_t *firstSizeParm,
		     int32_t *lastSizeParm,
		     char **firstStrPtr,
		     void *thisPtr,
		     int32_t *retSize,
		     char *userBuf,
		     int32_t  userBufSize,
		     bool  makePtrsRefNewBuf);

char *serializeMsg2 ( void *thisPtr ,
		      int32_t objSize ,
		      char **firstStrPtr ,
		      int32_t *firstSizeParm ,
		      int32_t *retSize );

// convert offsets back into ptrs
// returns -1 on error
int32_t deserializeMsg ( int32_t  baseSize ,
		      int32_t *firstSizeParm ,
		      int32_t *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) ;

bool deserializeMsg2 ( char **firstStrPtr , int32_t  *firstSizeParm );


#endif
