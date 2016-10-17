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
#include <inttypes.h>
#include "Sanity.h"


class SafeBuf;


class Mem {

 public:

	Mem();
	~Mem();

	bool init ( );

	void *gbmalloc  ( size_t size , const char *note  );
	void *gbcalloc  ( size_t size , const char *note);
	void *gbrealloc ( void *oldPtr, size_t oldSize, size_t newSize, const char *note);
	void gbfree(void *ptr, const char *note, size_t size, bool checksize);
	char *dup     ( const void *data , size_t dataSize , const char *note);
	char *strdup  ( const char *string, const char *note );

	int32_t validate();

	// this one does not include new/delete mem, only *alloc()/free() mem
	size_t getUsedMem() const;
	// the max mem ever allocated
	size_t getMaxAllocated() const { return m_maxAllocated; }
	size_t getMaxAlloc  () const { return m_maxAlloc; }
	const char *getMaxAllocBy() const { return m_maxAllocBy; }
	// the max mem we can use!
	size_t getMaxMem() const;

	int32_t getNumAllocated() const { return m_numAllocated; }

	int64_t getNumTotalAllocated() const { return m_numTotalAllocated; }
	
	float getUsedMemPercentage() const;
	int32_t getOOMCount() const { return m_outOfMems; }
	uint32_t getMemTableSize() const { return m_memtablesize; }
	int64_t getFreeMem() const;
	void setMemTableSize(uint32_t sz) { m_memtablesize = sz; }

	void incrementOOMCount() { m_outOfMems++; }

	// who underan/overran their buffers?
	int  printBreeches () ;
	// print mem usage stats
	int  printMem      ( ) ;

	void addMem(void *mem, size_t size, const char *note, char isnew);
	bool rmMem(void *mem, size_t size, const char *note, bool checksize);
	bool lblMem(void *mem, size_t size, const char *note);
	void addnew(void *ptr, size_t size, const char *note);
	void delnew(void *ptr, size_t size, const char *note);

	bool printMemBreakdownTable(SafeBuf* sb, 
				    char *lightblue, 
				    char *darkblue);

	size_t m_maxAllocated; // at any one time
	size_t m_maxAlloc; // the biggest single alloc ever done
	const char *m_maxAllocBy; // the biggest single alloc ever done

private:
	int32_t getMemSlot(void *mem);

	// currently used mem (estimate)
	size_t m_used;

	// count how many allocs/news failed
	int32_t m_outOfMems;

	int32_t          m_numAllocated;
	int64_t     m_numTotalAllocated;
	uint32_t m_memtablesize;

	int printBreeches_unlocked();
	int printBreech(int32_t i);
};

extern class Mem g_mem;

static inline void *mmalloc(size_t size, const char *note) {
	return g_mem.gbmalloc(size, note);
}

static inline void *mcalloc(size_t size, const char *note) {
	return g_mem.gbcalloc(size, note);
}

static inline void *mrealloc(void *oldPtr, size_t oldSize, size_t newSize, const char *note) {
	return g_mem.gbrealloc(oldPtr, oldSize, newSize, note);
}

static inline void mfree(void *ptr, size_t size, const char *note) {
	return g_mem.gbfree(ptr, note, size, true);
}

static inline char *mdup(const void *data, size_t dataSize, const char *note) {
	return g_mem.dup(data, dataSize, note);
}

static inline char *mstrdup(const char *string, const char *note) {
	return g_mem.strdup(string, note);
}

static inline void mnew(void *ptr, size_t size, const char *note) {
	return g_mem.addnew(ptr, size, note);
}

static inline void mdelete(void *ptr, size_t size, const char *note) {
	return g_mem.delnew(ptr, size, note);
}

static inline bool relabel(void *ptr, size_t size, const char *note) {
	return g_mem.lblMem(ptr, size, note);
}



#endif // GB_MEM_H
