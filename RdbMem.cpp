#include "RdbMem.h"
#include "Rdb.h"
#include "Mem.h"
#include "Sanity.h"

// RdbMem allocates a fixed chunk of memory and initially sets m_ptr1 to point at the start and m_ptr2 at the end
//    |--------------------------------------------------|
//    ^                                                  ^
//    m_ptr1                                             m_ptr2
//    m_mem
//
// allocData() normally takes memory from the primary region, m_mem..m_ptr1 (or m_ptr1..m_mem+end, see later)
//
//    |--------------------------------------------------|
//    ^         ^                                        ^
//              m_ptr1                                   m_ptr2
//    m_mem
//
// During dumping allocation from the primary region is suspended and the secondary region is used:
//    |--------------------------------------------------|
//    ^         ^                               ^
//              m_ptr1                          m_ptr2
//    m_mem
//
// After dump has finsihed the region roles are swapped and the old primary is emptied:
//    |--------------------------------------------------|
//    ^                                         ^
//    m_ptr2                                    m_ptr1
//    m_mem
// and memory allocation grows downward


//isj: why not just use a circular buffer and make caller do a mark() ?


RdbMem::RdbMem()
  : m_rdb(NULL),
    m_ptr1(NULL),
    m_ptr2(NULL),
    m_mem(NULL),
    m_memSize(0),
    m_is90PercentFull(false),
    m_90up(NULL),
    m_90down(NULL),
    m_allocName(NULL)
{
}


RdbMem::~RdbMem() {
	if(m_mem)
		mfree(m_mem, m_memSize, m_allocName);
}


void RdbMem::reset() {
	if(m_mem)
		mfree(m_mem, m_memSize, m_allocName);
	m_ptr1 = m_ptr2 = m_mem = NULL;
	m_memSize = 0;
}


void RdbMem::clear() {
	// set up primary/secondary mem ptrs
	m_ptr1 = m_mem;
	// secondary mem initially grow downward
	m_ptr2 = m_mem + m_memSize;

	m_is90PercentFull = false;
}	


// initialize us with the RdbDump class your rdb is using
bool RdbMem::init(const Rdb *rdb, int32_t memToAlloc, const char *allocName) {
	m_rdb  = rdb;
	m_allocName = allocName;
	// return true if no mem
	if(memToAlloc<=0)
		return true;
	// get the initial mem
	m_mem = (char*)mmalloc(memToAlloc, m_allocName);
	if(!m_mem) {
		log(LOG_WARN, "RdbMem::init: %s", mstrerror(g_errno));
		return false;
	}
	m_memSize = memToAlloc;
	// rush it into mem for real
	memset(m_mem, 0, memToAlloc);
	// set up primary/secondary mem ptrs
	m_ptr1 = m_mem;
	// secondary mem initially grow downward
	m_ptr2 = m_mem + m_memSize;
	// . set our limit markers
	// . one for when primary mem, m_ptr1, is growing upward
	//   and the other for when it's growing downward
	int64_t limit = ((int64_t)m_memSize * 90LL) / 100LL;
	m_90up   = m_mem + limit;
	m_90down = m_mem + m_memSize - limit;
	// success
	return true;
}


// . if a dump is not going on this uses the primary mem space
// . if a dump is going on and this key has already been dumped
//   (we check RdbDump::getFirstKey()/getLastKey()) add it to the
//   secondary mem space, otherwise add it to the primary mem space
void *RdbMem::dupData(const char *data, int32_t dataSize) {
	char *s = (char*)allocData(dataSize);
	if(!s)
		return NULL;
	memcpy(s, data, dataSize);
	return s;
}


void *RdbMem::allocData(int32_t dataSize) {
	// . otherwise, use the primary mem
	// . if primary mem growing down...
	if(m_ptr1>m_ptr2){
		// return NULL if it would breech
		if(m_ptr1-dataSize<=m_ptr2)
			return NULL;
		// otherwise, grow downward
		m_ptr1 -= dataSize;
		// are we at the 90% limit?
		if ( m_ptr1 < m_90down ) m_is90PercentFull = true;
		// return the ptr
		return m_ptr1;
	}
	// . if it's growing up...
	// . return NULL if it would breech
	if(m_ptr1+dataSize>=m_ptr2)
		return NULL;
	// otherwise, grow upward
	m_ptr1 += dataSize;
	// are we at the 90% limit?
	if(m_ptr1>m_90up)
		m_is90PercentFull = true;
	// note it
	//log("rdbmem: ptr1b=%" PRIu32" size=%" PRId32,(int32_t)m_ptr1-dataSize,dataSize);
	// return the ptr
	return m_ptr1 - dataSize;
}
