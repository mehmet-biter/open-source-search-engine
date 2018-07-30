#include "Serialize.h"
#include "Mem.h"
#include "Errno.h"
#include "Sanity.h"
#include "gbmemcpy.h"
#include <string.h>


int32_t getMsgStoredSize(int32_t baseSize,
			 const int32_t *firstSizeParm,
			 const int32_t *lastSizeParm)
{
	int32_t size = baseSize;
	// add up string buffer sizes
	const int32_t *sizePtr = firstSizeParm;
	const int32_t *sizeEnd = lastSizeParm;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		size += *sizePtr;
	return size;
}


// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg(int32_t             baseSize,
		   const int32_t      *firstSizeParm,
		   const int32_t      *lastSizeParm,
		   const char * const *firstStrPtr,
		   const void         *thisPtr,
		   int32_t            *retSize,
		   char               *userBuf,
		   int32_t             userBufSize)
{
	return serializeMsg(baseSize,
	                    const_cast<int32_t*>(firstSizeParm),
			    const_cast<int32_t*>(lastSizeParm),
			    const_cast<char**>(firstStrPtr),
			    const_cast<void*>(thisPtr),
			    retSize,
			    userBuf,
			    userBufSize,
			    false);
}


char *serializeMsg(int32_t   baseSize,
		   int32_t  *firstSizeParm,
		   int32_t  *lastSizeParm,
		   char    **firstStrPtr,
		   void     *thisPtr,
		   int32_t  *retSize,
		   char     *userBuf,
		   int32_t   userBufSize,
		   bool      makePtrsRefNewBuf)
{
	// make a buffer to serialize into
	char *buf  = NULL;
	//int32_t  need = getStoredSize();
	int32_t need = getMsgStoredSize(baseSize,firstSizeParm,lastSizeParm);
	// big enough?
	if ( need <= userBufSize ) buf = userBuf;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy the easy stuff
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , baseSize );
	p += baseSize; // getBaseSize();
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;
	int32_t  *sizeEnd = lastSizeParm;
	char **strPtr  = firstStrPtr;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *strPtr ) goto skip;
		// sanity check -- cannot copy onto ourselves
		if ( p > *strPtr && p < *strPtr + *sizePtr ) {
			gbshutdownLogicError(); }
		// copy the string into the buffer
		gbmemcpy ( p , *strPtr , *sizePtr );
	skip:
		// . make it point into the buffer now
		// . MDW: why? that is causing problems for the re-call in
		//   Msg3a, it calls this twice with the same "m_r"
		if ( makePtrsRefNewBuf ) *strPtr = p;
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	return buf;
}


char *serializeMsg2(void     *thisPtr,
		    int32_t   objSize,
		    char    **firstStrPtr,
		    int32_t  *firstSizeParm,
		    int32_t  *retSize)
{

	// make a buffer to serialize into
	int32_t baseSize = (char *)firstStrPtr - (char *)thisPtr;
	char **endStrPtr = (char**)firstSizeParm;
	int nptrs = endStrPtr - firstStrPtr;
	int32_t need = baseSize;
	need += nptrs * sizeof(char *);
	need += nptrs * sizeof(int32_t);
	// tally up the string sizes
	int32_t  *srcSizePtr = (int32_t *)firstSizeParm;
	char **srcStrPtr  = (char **)firstStrPtr;
	int32_t totalStringSizes = 0;
	for ( int i = 0 ; i < nptrs ; i++ ) {
		if ( srcStrPtr[i] == NULL ) continue;
		totalStringSizes += srcSizePtr[i];

	}
	int32_t stringBufferOffset = need;
	need += totalStringSizes;
	// alloc serialization buffer
	char *buf = (char *)mmalloc ( need , "sm2" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy everything over except strings themselves
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , stringBufferOffset );
	// point to the string buffer
	p += stringBufferOffset;
	// then store the strings!
	char **dstStrPtr = (char **)(buf + baseSize );
	int32_t *dstSizePtr = (int32_t *)(buf + baseSize+sizeof(char *)*nptrs);
	for ( int count = 0 ; count < nptrs ; count++ ) {
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *srcStrPtr )
			goto skip;
		// if this is valid then size can't be 0! fix upstream.
		if ( ! *srcSizePtr ) { gbshutdownLogicError(); }
		// sanity check -- cannot copy onto ourselves
		if ( p > *srcStrPtr && p < *srcStrPtr + *srcSizePtr ) {
			gbshutdownLogicError(); }
		// copy the string into the buffer
		gbmemcpy ( p , *srcStrPtr , *srcSizePtr );
	skip:
		// point it now into the string buffer
		*dstStrPtr = p;
		// if it is 0 length, make ptr NULL in destination
		if ( *srcSizePtr == 0 || *srcStrPtr == NULL ) {
			*dstStrPtr = NULL;
			*dstSizePtr = 0;
		}
		// advance our destination ptr
		p += *dstSizePtr;
		// advance both ptrs to next string
		srcSizePtr++;
		srcStrPtr++;
		dstSizePtr++;
		dstStrPtr++;
	}
	return buf;
}


// convert offsets back into ptrs
int32_t deserializeMsg(int32_t   baseSize,
		       int32_t  *firstSizeParm,
		       int32_t  *lastSizeParm,
		       char    **firstStrPtr,
		       char     *stringBuf)
{
	// point to our string buffer
	char *p = stringBuf;
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;
	int32_t  *sizeEnd = lastSizeParm;
	char **strPtr  = firstStrPtr;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) { g_errno = ECORRUPTDATA; return -1;}
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	return baseSize + (p - stringBuf);
}


bool deserializeMsg2(char    **firstStrPtr,
		     int32_t  *firstSizeParm)
{
	int nptrs=((char *)firstSizeParm-(char *)firstStrPtr)/sizeof(char *);
	// point to our string buffer
	char *p = ((char *)firstSizeParm + sizeof(int32_t)*nptrs);
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;
	char **strPtr  = firstStrPtr;
	int count = 0;
	for ( ; count < nptrs ; count++ ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) return false;
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	return true;
}
