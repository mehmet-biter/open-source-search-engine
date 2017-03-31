// Matt Wells, copyright Apr 2002

// . this handles memory alloaction for an Rdb class
// . Rdb::addRecord often copies the record data to add to the tree
//   so it now uses the RdbMem::dup() here

#ifndef GB_RDBMEM_H
#define GB_RDBMEM_H

#include "types.h"
#include <inttypes.h>

class Rdb;

class RdbMem {

 public:

	RdbMem();
	~RdbMem();

	// initialize us with the RdbDump class your rdb is using
	bool init(const Rdb *rdb, int32_t memToAlloc,
		  const char *allocName);

	void clear();

	void reset();

	// . if a dump is not going on this uses the primary mem space
	void *dupData(const char *data, int32_t dataSize);

	// used by dupData
	void *allocData(int32_t dataSize);

	// how much mem is available?
	int32_t getAvailMem() const {
		// don't allow ptrs to equal each other...
		if ( m_ptr1 == m_ptr2 ) return 0;
		if ( m_ptr1 <  m_ptr2 ) return m_ptr2 - m_ptr1 - 1;
		return m_ptr1 - m_ptr2 - 1;
	}

	int32_t getTotalMem() const { return m_memSize; }

	int32_t getUsedMem() const { return m_memSize - getAvailMem(); }

	// used to determine when to dump
	bool is90PercentFull() const { return m_is90PercentFull; }

private:
	friend class Rdb;
	// keep hold of this class
	const Rdb *m_rdb;

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
