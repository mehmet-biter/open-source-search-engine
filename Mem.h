// Matt Wells, copyright Sep 2001

// . mostly just wrappers for most memory functions
// . allows us to constrain memory
// . also calls mlockall() on construction to avoid swapping out any mem
// . TODO: primealloc(int slotSize,int numSlots) :
//         pre-allocs a table of these slots for faster mmalloc'ing

#ifndef GB_MEM_H
#define GB_MEM_H

#include <new>
#include <stddef.h>            //for NULL
#include <string.h>            //for strlen()
#include <inttypes.h>
// Ugly - but so is lots of code in .h files
extern void gbshutdownAbort( bool save_on_abort );


extern bool g_inMemFunction;

class SafeBuf;


inline int gbstrlen ( const char *s ) {
	if ( ! s ) { gbshutdownAbort(true); }
	return strlen(s);
}

class Mem {

 public:

	Mem();
	~Mem();

	bool init ( );

	void *gbmalloc  ( int size , const char *note  );
	void *gbcalloc  ( int size , const char *note);
	void *gbrealloc ( void *oldPtr, int oldSize, int newSize,
				const char *note);
	void  gbfree    ( void *ptr , int size , const char *note);
	char *dup     ( const void *data , int32_t dataSize , const char *note);
	char *strdup  ( const char *string , const char *note ) {
		return dup ( string , gbstrlen ( string ) + 1 , note ); }

	int32_t validate();

	// this one does not include new/delete mem, only *alloc()/free() mem
	int64_t getUsedMem () { return m_used; }
	// the max mem ever alloced
	int64_t getMaxAlloced() { return m_maxAlloced; }
	int64_t getMaxAlloc  () { return m_maxAlloc; }
	const char *getMaxAllocBy() { return m_maxAllocBy; }
	// the max mem we can use!
	int64_t getMaxMem () ;

	int32_t getNumAllocated() { return m_numAllocated; }

	int64_t getNumTotalAllocated() { return m_numTotalAllocated; }

	// who underan/overran their buffers?
	int  printBreech   ( int32_t i , char core ) ;
	int  printBreeches ( char core ) ;
	// print mem usage stats
	int  printMem      ( ) ;
	void addMem ( void *mem , int32_t size , const char *note, char isnew);
	bool rmMem  ( void *mem , int32_t size , const char *note ) ;
	bool lblMem ( void *mem , int32_t size , const char *note );

	int32_t getMemSlot  ( void *mem );

	void addnew ( void *ptr , int32_t size , const char *note ) ;
	void delnew ( void *ptr , int32_t size , const char *note ) ;

	bool printMemBreakdownTable(SafeBuf* sb, 
				    char *lightblue, 
				    char *darkblue);

	int64_t m_maxAlloced; // at any one time
	int64_t m_maxAlloc; // the biggest single alloc ever done
	const char *m_maxAllocBy; // the biggest single alloc ever done

	// currently used mem (estimate)
	int64_t m_used;

	// count how many allocs/news failed
	int32_t m_outOfMems;

	int32_t          m_numAllocated;
	int64_t     m_numTotalAllocated;
	uint32_t m_memtablesize;
};

extern class Mem g_mem;

//#define mmalloc(size,note) malloc(size)
//#define mfree(ptr,size,note) free(ptr)
//#define mrealloc(oldPtr,oldSize,newSize,note) realloc(oldPtr,newSize)
inline void *mmalloc ( int size , const char *note ) { 
	return g_mem.gbmalloc(size,note); }
inline void *mcalloc ( int size , const char *note ) { 
	return g_mem.gbcalloc(size,note); }
inline void *mrealloc (void *oldPtr, int oldSize, int newSize,
			const char *note) {
	return g_mem.gbrealloc(oldPtr,oldSize,newSize,note);}
inline void  mfree    ( void *ptr , int size , const char *note) {
	return g_mem.gbfree(ptr,size,note);}
inline char *mdup     ( const void *data , int32_t dataSize , const char *note) {
	return g_mem.dup(data,dataSize,note);}
inline char *mstrdup  ( const char *string , const char *note ) {
	return g_mem.strdup(string,note);}
inline void mnew ( void *ptr , int32_t size , const char *note ) {
	return g_mem.addnew ( ptr , size , note );}
inline void mdelete ( void *ptr , int32_t size , const char *note ) {
	return g_mem.delnew ( ptr , size , note ); }
inline bool relabel   ( void *ptr , int32_t size , const char *note ) {
	return g_mem.lblMem( ptr, size, note ); }



#endif // GB_MEM_H
