// Matt Wells, copyright Apr 2002

// . this handles memory alloaction for an Rdb class
// . Rdb::addRecord often copies the record data to add to the tree
//   so it now uses the RdbMem::dup() here

#ifndef GB_RDBMEM_H
#define GB_RDBMEM_H

#include "types.h"
#include "GbMutex.h"
#include <inttypes.h>

class RdbMem {

 public:

	RdbMem();
	~RdbMem();

	// initialize us with the RdbDump class your rdb is using
	bool init(int32_t memToAlloc, const char *allocName);

	void clear();

	void reset();

	GbMutex& getLock() { return m_mtx; }

	// . if a dump is not going on this uses the primary mem space
	void *dupData(const char *data, int32_t dataSize);

	// used by dupData
	void *allocData(int32_t dataSize);

	// how much mem is available?
	int32_t getAvailMem() const;

	int32_t getTotalMem() const { return m_memSize; }

	int32_t getUsedMem() const { return m_memSize - getAvailMem(); }

	// used to determine when to dump
	bool is90PercentFull() const;

private:
	friend class Rdb;

	mutable GbMutex m_mtx;

	// the primary mem
	char *m_ptr1;
	// the secondary mem
	char *m_ptr2;

	// the full mem we alloc'd initially
	char *m_mem;
	int32_t  m_memSize;

	// this is true when our primary mem is 90% of m_memSize
	bool  m_is90PercentFull;

	// limit ptrs for primary mem ptr, m_ptr1, to breech to be 90% full
	char *m_90up   ;
	char *m_90down ;

	const char *m_allocName;
};

#endif // GB_RDBMEM_H
