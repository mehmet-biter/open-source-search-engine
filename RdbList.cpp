#include "RdbList.h"
#include "Rdb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "BitOperations.h"
#include "RdbIndexQuery.h"
#include "Posdb.h"
#include <set>
#include <assert.h>

static const int signature_init = 0x07b39a1b;


// . compares to keys split into 6 byte ptrs
// . returns -1, 0 , 1 if a < b , a == b , a > b
// . for comparison purposes, we must set 0x02 (half bits) on all keys
//   so negative keys will always be ordered before their positive

static inline char bfcmpPosdb ( const char *alo, const char *ame, const char *ahi ,
			        const char *blo, const char *bme, const char *bhi ) {
	if (*(const uint32_t *)( ahi+2 )<*(const uint32_t *)(bhi+2)) return -1;
	if (*(const uint32_t *)( ahi+2 )>*(const uint32_t *)(bhi+2)) return  1;
	if (*(const uint16_t *)( ahi   )<*(const uint16_t *)(bhi  )) return -1;
	if (*(const uint16_t *)( ahi   )>*(const uint16_t *)(bhi  )) return  1;

	if (*(const uint32_t *)( ame+2 )<*(const uint32_t *)(bme+2)) return -1;
	if (*(const uint32_t *)( ame+2 )>*(const uint32_t *)(bme+2)) return  1;
	if (*(const uint16_t *)( ame   )<*(const uint16_t *)(bme  )) return -1;
	if (*(const uint16_t *)( ame   )>*(const uint16_t *)(bme  )) return  1;

	if (*(uint32_t  *)( alo+2 )<*(uint32_t  *)(blo+2)) return -1;
	if (*(uint32_t  *)( alo+2 )>*(uint32_t  *)(blo+2)) return  1;

	if ( ((*(const uint16_t *)( alo   ))|0x0007) <
	     ((*(const uint16_t *)  blo    )|0x0007)  ) return -1;
	if ( ((*(const uint16_t *)( alo   ))|0x0007) >
	     ((*(const uint16_t *)  blo    )|0x0007)  ) return  1;

	return 0;
};


static bool cmp_6bytes_equal(const void *p1, const void *p2) {
	uint32_t u32_1 = *(const uint32_t*)p1;
	uint32_t u32_2 = *(const uint32_t*)p2;
	if(u32_1!=u32_2) return false;
	uint16_t u16_1 = *(const uint16_t*)(((const char*)p1)+4);
	uint16_t u16_2 = *(const uint16_t*)(((const char*)p2)+4);
	return u16_1==u16_2;
}


void RdbList::constructor () {
	verify_signature();
	m_list        = NULL;
	m_alloc       = NULL;
	m_allocSize   = 0;
	m_useHalfKeys = false;
	m_ownData     = false;
	reset();
}

RdbList::RdbList () {
//	log(LOG_TRACE,"RdbList(%p)::RdbList()",this);
	set_signature();
	m_list        = NULL;
	m_alloc       = NULL;
	m_allocSize   = 0;
	m_useHalfKeys = false;
	m_ownData     = false;
	m_fixedDataSize = 0;
	
	// PVS-Studio
	m_startKey[0] = '\0';
	m_endKey[0] = '\0';
	m_lastKey[0] = '\0';

	reset();
}

// free m_list on destruction
RdbList::~RdbList () {
	verify_signature();
//	log(LOG_TRACE,"RdbList(%p)::~RdbList()",this);
	freeList();
	clear_signature();
}

void RdbList::destructor() {
	assert(this);
	verify_signature();
	freeList();
}

void RdbList::freeList () {
	verify_signature();
	if ( m_ownData && m_alloc ) mfree ( m_alloc , m_allocSize ,"RdbList");
	m_list      = NULL;
	m_alloc     = NULL;
	m_allocSize = 0;
	reset();
}

void RdbList::resetListPtr () {
	verify_signature();
	m_listPtr = m_list;
	m_listPtrHi = NULL;
	m_listPtrLo = NULL;
	// this is used if m_useHalfKeys is true
	if   ( m_list && m_listSize >= m_ks ) {
		m_listPtrHi = m_list + (m_ks-6);
		m_listPtrLo = m_list + (m_ks-12);
	}
}

// . this now just resets the size to 0, does not do any freeing
// . free will only happen on list destruction
void RdbList::reset ( ) {
	verify_signature();
	// . if we don't own our data then, NULLify it
	// . if we do own the data, don't free it
	if ( ! m_ownData ) { 
		m_alloc = NULL; 
		m_allocSize = 0; 
	}
	m_listSize  = 0;
	m_list      = m_alloc;
	m_listEnd   = m_list;
	m_ownData   = true;
	// use this call now to set m_listPtr and m_listPtrHi
	resetListPtr();
	// init to -1 so we know if merge_r() was called w/o calling
	// prepareForMerge()
	m_mergeMinListSize = -1;
	m_lastKeyIsValid = false;
	// default key size to 12 bytes
	m_ks = 12;
}

// . set from a pre-existing list
// . all keys of records in list must be in [startKey,endKey]
void RdbList::set(char *list, int32_t listSize, char *alloc, int32_t allocSize, const char *startKey, const char *endKey,
                  int32_t fixedDataSize, bool ownData, bool useHalfKeys, char keySize) {
	assert(this);
	verify_signature();
	logTrace(g_conf.m_logTraceRdbList, "BEGIN. list=%p listSize=%" PRId32" alloc=%p allocSize=%" PRId32,
	         list, listSize, alloc, allocSize);
	char logbuf1[50],logbuf2[50];
	logTrace(g_conf.m_logTraceRdbList, "startKey=%s endKey=%s keySize=%hhu fixedDataSize=%" PRId32,
	         KEYSTR(startKey, keySize,logbuf1), KEYSTR(endKey, keySize,logbuf2), keySize, fixedDataSize);

	// free and NULLify any old m_list we had to make room for our new list
	freeList();

	// set this first since others depend on it
	m_ks = keySize;

	// sanity check (happens when IndexReadInfo exhausts a list to Msg2)
	if (KEYCMP(startKey, endKey, m_ks) > 0) {
		log(LOG_WARN, "db: rdblist: set: startKey > endKey.");
		gbshutdownCorrupted();
	}

	// safety check
	if (fixedDataSize != 0 && useHalfKeys) {
		log(LOG_LOGIC, "db: rdblist: set: useHalfKeys 1 when fixedDataSize not 0.");
		useHalfKeys = false;
	}

	// got an extremely ugly corrupt stack core without this check
	if (m_list && m_listSize == 0) {
		log(LOG_WARN, "rdblist: listSize of 0 but list pointer not NULL!");
		m_list = NULL;
	}

	// set our list parms
	m_list          = list;
	m_listSize      = listSize;
	m_alloc         = alloc;
	m_allocSize     = allocSize;
	m_listEnd       = list + listSize;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey  ,endKey  ,m_ks);
	m_fixedDataSize = fixedDataSize;
	m_ownData       = ownData;
	m_useHalfKeys   = useHalfKeys;

	// use this call now to set m_listPtr and m_listPtrHi based on m_list
	resetListPtr();

	logTrace(g_conf.m_logTraceRdbList, "END");
}

// like above but uses 0/maxKey for startKey/endKey
void RdbList::set(char *list, int32_t listSize, char *alloc, int32_t allocSize,
                  int32_t fixedDataSize, bool ownData, bool useHalfKeys, char keySize) {
	verify_signature();
	set(list, listSize, alloc, allocSize, KEYMIN(), KEYMAX(), fixedDataSize, ownData, useHalfKeys, keySize);
}


void RdbList::stealFromOtherList(RdbList *other_list)
{
	if(other_list==this) gbshutdownLogicError();
	if(!other_list->m_ownData) gbshutdownLogicError();
	
	freeList();
	
	m_list             = other_list->m_list;
	m_listSize         = other_list->m_listSize;
	m_alloc            = other_list->m_alloc;
	m_allocSize        = other_list->m_allocSize;
	m_listEnd          = other_list->m_listEnd;
	KEYSET(m_startKey,   other_list->m_startKey,other_list->m_ks);
	KEYSET(m_endKey,     other_list->m_endKey,  other_list->m_ks);
	m_fixedDataSize    = other_list->m_fixedDataSize;
	m_ownData          = other_list->m_ownData;
	m_useHalfKeys      = other_list->m_useHalfKeys;
	KEYSET(m_lastKey,    other_list->m_lastKey,  other_list->m_ks);
	m_lastKeyIsValid   = other_list->m_lastKeyIsValid;
	m_mergeMinListSize = other_list->m_mergeMinListSize;
	m_ks               = other_list->m_ks;
	resetListPtr();
	
	other_list->m_list      = NULL;
	other_list->m_alloc     = NULL;
	other_list->m_allocSize = 0;
	other_list->reset();
}


// just set the start and end keys
void RdbList::set ( const char *startKey, const char *endKey ) {
	verify_signature();
	KEYSET ( m_startKey , startKey , m_ks );
	KEYSET ( m_endKey   , endKey   , m_ks );
}

const char *RdbList::getLastKey() const {
	verify_signature();
	if (!m_lastKeyIsValid) {
		log(LOG_ERROR, "db: rdblist: getLastKey: m_lastKey not valid.");
		gbshutdownAbort(true);
	}

	return m_lastKey;
}

void RdbList::setLastKey  ( const char *k ) {
	verify_signature();
	//m_lastKey = k;
	KEYSET ( m_lastKey , k , m_ks );
	m_lastKeyIsValid = true;
}

// this has to scan through each record for variable sized records and
// if m_useHalfKeys is true
int32_t RdbList::getNumRecs ( ) {
	verify_signature();
	// we only keep this count for lists of variable sized records
	if ( m_fixedDataSize == 0 && ! m_useHalfKeys )
		return m_listSize / ( m_ks + m_fixedDataSize );
	// save the list ptr
	char *saved = m_listPtr;
	const char *hi    = m_listPtrHi;
	// reset m_listPtr and m_listPtrHi
	resetListPtr();
	// count each record individually since they're variable size
	int32_t count = 0;
	// go through each record
	while ( ! isExhausted() ) {
		count++;
		skipCurrentRecord();
	}
	// restore list ptr
	m_listPtr   = saved;
	m_listPtrHi = hi;
	// return the count
	return count;
}

// . returns false and sets g_errno on error
// . used by merge() above to add records to merged list
// . used by RdbTree to construct an RdbList from branches of records
// . NOTE: does not set m_endKey/m_startKey/ etc..
bool RdbList::addRecord ( const char *key, int32_t dataSize, const char *data, bool bitch ) {
	verify_signature();
	if ( m_ks == 18 ) {
		// sanity
		if ( key[0] & 0x06 ) {
			log(LOG_ERROR, "rdblist: posdb: cannot add bad key. please delete posdb-buckets-saved.dat and restart.");
			gbshutdownAbort(true);
		}

		// grow the list if we need to
		if ( m_listEnd + 18 >  m_alloc + m_allocSize )
			if ( ! growList ( m_allocSize + 18 ) )
				return false;
		if ( m_listPtrHi && cmp_6bytes_equal ( m_listPtrHi, key+12 ) ){
			// compare next 6 bytes
			if ( cmp_6bytes_equal ( m_listPtrLo,key+6) ) {
				// store in end key
				memcpy(m_listEnd,key,6);
				// turn on both half bits
				*m_listEnd |= 0x06;
				// clear magic bit

				// grow list
				m_listSize += 6;
				m_listEnd  += 6;
				return true;
			}
			// no match...
			memcpy(m_listEnd,key,12);
			// need to update this then
			m_listPtrLo = m_listEnd+6;
			// turn on just one compression bit
			*m_listEnd |= 0x02;
			// grow list
			m_listSize += 12;
			m_listEnd  += 12;
			return true;
		}
		// no compression
		memcpy(m_listEnd,key,18);
		m_listPtrLo = m_listEnd+6;
		m_listPtrHi = m_listEnd+12;
		m_listSize += 18;
		m_listEnd  += 18;
		return true;
	}


	// return false if we don't own the data
	if ( ! m_ownData && bitch ) {
		log(LOG_LOGIC,"db: rdblist: addRecord: Data not owned.");
		gbshutdownAbort(true);
	}
	// get total size of the record
	int32_t recSize = m_ks + dataSize;

	// sanity
	if ( dataSize && KEYNEG(key) ) {
		gbshutdownAbort(true);
	}

	// . include the 4 bytes to store the dataSize if it's not fixed
	// . negative keys never have a datasize field now
	if ( m_fixedDataSize < 0 && !KEYNEG(key) ) recSize += 4;

	// grow the list if we need to
	if ( m_listEnd + recSize >  m_alloc + m_allocSize ) {
		if ( !growList( m_allocSize + recSize ) ) {
			return false;// log("RdbList::merge: growList failed");
		}
	}

	// . special case for half keys
	// . if high 6 bytes are the same as last key,
	//   then just store low 6 bytes
	if ( m_useHalfKeys &&
	     m_listPtrHi   &&
	     cmp_6bytes_equal ( m_listPtrHi, key+(m_ks-6) ) ) {
		// store low 6 bytes of key into m_list
		memcpy(m_listEnd,key,m_ks-6);
		// turn on half bit
		*m_listEnd |= 0x02;
		// grow list
		m_listSize += (m_ks - 6);
		m_listEnd  += (m_ks - 6);
		return true;
	}

	// store the key at the end of the list
	KEYSET ( &m_list[m_listSize], key, m_ks );

	// update the ptr
	if ( m_useHalfKeys ) {
		// we're the new hi key
		//m_listPtrHi = (m_list + m_listSize + 6);
		m_listPtrHi = (m_list + m_listSize + (m_ks - 6));
		// turn off half bit
		m_list[m_listSize] &= 0xfd;
	}

	m_listSize += m_ks;
	m_listEnd  += m_ks;
	// return true if we're dataless

	if ( m_fixedDataSize == 0 ) return true;

	// copy the dataSize to the list if it's not fixed or negative...
	if ( m_fixedDataSize == -1 && !KEYNEG(key) ) {
		*(int32_t *)(&m_list[m_listSize]) = dataSize ;
		m_listSize += 4;
		m_listEnd  += 4;
	}

	// copy the data itself to the list
	memcpy ( &m_list[m_listSize] , data , dataSize );
	m_listSize += dataSize;
	m_listEnd  += dataSize;

	return true;
}

// . this prepares this list for a merge
// . call this before calling merge_r() below to do the actual merge
// . this will pre-allocate space for this list to hold the mergees
// . this is useful because you can call it in the main process before
//   before calling merge_r() in a thread
// . allocates on top of m_listSize
// . returns false and sets g_errno on error, true on success
bool RdbList::prepareForMerge(RdbList **lists, int32_t numLists, int32_t minRecSizes) {
	verify_signature();
	logTrace(g_conf.m_logTraceRdbList, "BEGIN. numLists=%" PRId32" minRecSizes=%" PRId32, numLists, minRecSizes);

	// return false if we don't own the data
	if (!m_ownData) {
		log(LOG_ERROR, "db: rdblist: prepareForMerge: Data not owned.");
		gbshutdownAbort(true);
	}

	// . reset ourselves
	// . sets m_listSize to 0 and m_ownData to true
	// . does not free m_list, however
	// . NO! we want to keep what we got and add records on back
	//reset();
	// do nothing if no lists passed in
	if (numLists <= 0) {
		return true;
	}

	// . we inherit our dataSize/dedup from who we're merging
	// . TODO: all lists may not be the same fixedDataSize
	m_fixedDataSize = lists[0]->m_fixedDataSize;

	// assume we use half keys
	m_useHalfKeys = lists[0]->m_useHalfKeys;

	// inherit key size
	m_ks = lists[0]->m_ks;

	logTrace(g_conf.m_logTraceRdbList, "m_fixedDataSize=%" PRId32" m_useHalfKeys=%s m_ks=%" PRId32,
	         m_fixedDataSize, m_useHalfKeys ? "true" : "false", m_ks);

	// minRecSizes is only a good size-constraining parameter if
	// we know the max rec size, cuz we could overshoot list
	// by a rec of size 1 meg!! quite a bit! then we would have to
	// call growList() in the merge_r() routine... that won't work since
	// we'd be in a thread.
	if (m_fixedDataSize >= 0 && minRecSizes > 0) {
		int32_t newmin = minRecSizes + m_ks + m_fixedDataSize;

		// we have to grow another 12 cuz we set "first" in
		// indexMerge_r() to false and try to add another rec to see
		// if there was an annihilation
		newmin += m_ks;

		// watch out for wrap around
		if ( newmin < minRecSizes ) {
			newmin = 0x7fffffff;
		}

		minRecSizes = newmin;
	} else if ( m_fixedDataSize <  0 ) {
		minRecSizes = -1;
	}

	// . temporarily set m_listPtr/m_listEnd of each list based on
	//   the contraints: startKey/endKey
	// . compute our max list size from all these ranges
	int32_t maxListSize = 0;
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// each list should be constrained already
		maxListSize += lists[i]->getListSize();

		// ensure same dataSize type for each list
		if (lists[i]->getFixedDataSize() == m_fixedDataSize) {
			continue;
		}

		// bitch if not
		log(LOG_LOGIC,"db: rdblist: prepareForMerge: Non-uniform fixedDataSize. %" PRId32" != %" PRId32".",
		    lists[i]->getFixedDataSize(), m_fixedDataSize );

		g_errno = EBADENGINEER;
		return false;
	}

	// . set the # of bytes we need to merge at minimum
	// . include our current list size, too
	// . our current list MUST NOT intersect w/ these lists
	m_mergeMinListSize = maxListSize + m_listSize ;
	if (minRecSizes >= 0 && m_mergeMinListSize > minRecSizes) {
		m_mergeMinListSize = minRecSizes;
	}

	logTrace(g_conf.m_logTraceRdbList, "minRecSizes=%" PRId32 " maxListSize=%" PRId32" m_listSize=%" PRId32" m_mergeMinListSize=%" PRId32,
	         minRecSizes, maxListSize, m_listSize, m_mergeMinListSize);

	// . now alloc space for merging these lists
	// . won't shrink our m_list buffer, might grow it a bit if necessary
	// . this should keep m_listPtr and m_listPtrHi in order, too
	// . grow like 12 bytes extra since posdb might compress off 12
	//   bytes in merge_r code.
	int32_t grow = m_mergeMinListSize;

	// tack on a bit because rdbs that use compression like clusterdb,
	// posdb, etc. in the merge_r() code check for buffer break and
	// they use a full key size! so add that on here! otherwise, they
	// exit before getting the full mintomerge and come up short
	grow += m_ks;

	if (growList(grow)) {
		return true;
	}

	// otherwise, bitch about error
	return false; // log("RdbList::merge: growList failed");
}

// . get the current records key
// . this needs to be fast!!
void RdbList::getKey ( const char *rec , char *key ) const {
	assert(this);
	verify_signature();

	// posdb?
	if ( m_ks == 18 ) {
		if ( rec[0]&0x04 ) {
			memcpy ( key+12,m_listPtrHi,6);
			memcpy ( key+6 ,m_listPtrLo,6);
			memcpy ( key,rec,6);
			// clear compressionbits (1+2+4+8)
			key[0] &= 0xf9;
			return;
		}
		if ( rec[0]&0x02 ) {
			memcpy ( key+12 ,m_listPtrHi,6);
			memcpy ( key,rec,12);
			// clear compressionbits (1+2+4+8)
			key[0] &= 0xf9;
			return;
		}
		memcpy ( key , rec , 18 );
		return;
	}

	if ( ! m_useHalfKeys || ! isHalfBitOn ( rec ) ) {
		KEYSET(key,rec,m_ks);
		return;
	}

	// set to last big key we read
	// linkdb
	if ( m_ks == sizeof(key224_t) ) {
		// set top most 4 bytes from hi key
		*(int32_t  *)(&key[24]) = *(int32_t  *)&m_listPtrHi[2];
		// next 2 bytes from hi key
		*(int16_t *)(&key[22]) = *(int16_t *)m_listPtrHi;
		// next 8 bytes from rec
		*(int64_t *)(&key[ 14]) = *(int64_t *)&rec    [14];
		// next 8 bytes from rec
		*(int64_t *)(&key[  6]) = *(int64_t *)&rec    [ 6];
		// next 4 bytes from rec
		*(int32_t *)(&key[  2]) = *(int32_t *)&rec    [ 2];
		// last 2 bytes from rec
		*(int16_t *)(&key[ 0]) = *(int16_t *) rec;
		// turn half bit off since this is the full 16 bytes
		*key &= 0xfd;
		return;
	}
	if ( m_ks == 24 ) {
		// set top most 4 bytes from hi key
		*(int32_t  *)(&key[20]) = *(int32_t  *)&m_listPtrHi[2];
		// next 2 bytes from hi key
		*(int16_t *)(&key[18]) = *(int16_t *)m_listPtrHi;
		// next 8 bytes from rec
		*(int64_t *)(&key[ 10]) = *(int64_t *)&rec    [10];
		// next 8 bytes from rec
		*(int64_t *)(&key[  2]) = *(int64_t *)&rec    [ 2];
		// last 2 bytes from rec
		*(int16_t *)(&key[ 0]) = *(int16_t *) rec;
		// turn half bit off since this is the full 16 bytes
		*key &= 0xfd;
		return;
	}
	if ( m_ks == 16 ) {
		// set top most 4 bytes from hi key
		*(int32_t  *)(&key[12]) = *(int32_t  *)&m_listPtrHi[2];
		// next 2 bytes from hi key
		*(int16_t *)(&key[10]) = *(int16_t *)m_listPtrHi;
		// next 4 bytes from rec
		*(int32_t  *)(&key[ 6]) = *(int32_t  *)&rec    [6];
		// next 4 bytes from rec
		*(int32_t  *)(&key[ 2]) = *(int32_t  *)&rec    [2];
		// last 2 bytes from rec
		*(int16_t *)(&key[ 0]) = *(int16_t *) rec;
		// turn half bit off since this is the full 16 bytes
		*key &= 0xfd;
		return;
	}
	// sanity
	if ( m_ks != 12 ) {
		gbshutdownAbort(true);
	}

	*(int32_t  *)(&key[8]) = *(int32_t  *)&m_listPtrHi[2];
	// next 2 bytes from hi key
	*(int16_t *)(&key[6]) = *(int16_t *)m_listPtrHi;
	// next 4 bytes from rec
	*(int32_t  *)(&key[2]) = *(int32_t  *)&rec    [2];
	// last 2 bytes from rec
	*(int16_t *)(&key[0]) = *(int16_t *) rec;
	// turn half bit off since this is the full 12 bytes
	*key &= 0xfd;
}

int32_t RdbList::getDataSize ( const char *rec ) const {
	if ( m_fixedDataSize == 0 ) return 0;
	// negative keys always have no datasize entry
	if ( KEYNEG(rec) ) return 0;
	if ( m_fixedDataSize >= 0 ) return m_fixedDataSize;
	return *(int32_t  *)(rec+m_ks);
}

char *RdbList::getData ( char *rec ) {
	if ( m_fixedDataSize == 0 ) return NULL;
	if ( m_fixedDataSize  > 0 ) return rec + m_ks;
	// negative key? then no data
	if ( KEYNEG(rec) ) return NULL;
	return rec + m_ks + 4;
}


// returns false on error and set g_errno
bool RdbList::growList(int32_t newSize) {
	assert(this);
	verify_signature();
	logTrace(g_conf.m_logTraceRdbList, "BEGIN. newSize=%" PRId32, newSize);

	// return false if we don't own the data
	if (!m_ownData) {
		log(LOG_LOGIC,"db: rdblist: growlist: Data not owned.");
		gbshutdownAbort(true);
	}

	// sanity check
	if (newSize < 0) {
		log(LOG_LOGIC,"db: rdblist: growlist: Size is negative.");
		gbshutdownAbort(true);
	}

	// don't shrink list
	if (newSize <= m_allocSize) {
		return true;
	}

	// make a new buffer
	char *tmp =(char *) mrealloc ( m_alloc,m_allocSize,newSize,"RdbList");

	if ( ! tmp ) return false;
	// if we got a different address then re-set the list
	// TODO: fix this to keep our old list
	if ( tmp != m_list ) {
		m_listPtr   = tmp + ( m_listPtr   - m_alloc );
		m_list      = tmp + ( m_list      - m_alloc );
		m_listEnd   = tmp + ( m_listEnd   - m_alloc );
		// this may be NULL, if so, keep it that way
		if ( m_listPtrHi )
			m_listPtrHi = tmp + ( m_listPtrHi - m_alloc );
		if ( m_listPtrLo )
			m_listPtrLo = tmp + ( m_listPtrLo - m_alloc );
	}
	// assign m_list and reset m_allocSize
	m_alloc     = tmp;
	m_allocSize = newSize;

	// . we need to reset to set m_listPtr and m_listPtrHi
	// . NO! prepareForMerge() may be on its second call! we want to
	//   add new merged recs on to end of this list then
	//resetListPtr();

	return true;
}

// . TODO: check keys to make sure they belong to this group!!
// . I had a problem where a foreign spider rec was in our spiderdb and
//   i couldn't delete it because the del key would go to the foreign group!
// . as a temp patch i added a msg1 force local group option
bool RdbList::checkList_r(bool abortOnProblem, rdbid_t rdbId) {
	assert(this);
	verify_signature();
	// bail if empty
	if ( m_listSize <= 0 || ! m_list ) return true;

	// ensure m_listSize jives with m_listEnd
	if ( m_listEnd - m_list != m_listSize ) {
		log(LOG_WARN, "db: Data end does not correspond to data size.");
		if ( abortOnProblem ) { gbshutdownAbort(true); }
		return false;
	}

	char oldk[MAX_KEY_BYTES] = {0};
	KEYSET(oldk,KEYMIN(),m_ks);
	// point to start of list
	resetListPtr();
	// we can accept keys == endKey + 1 because we may have dup keys
	// which cause Msg3.cpp:setEndPages() to hiccup, cuz it subtracts
	// one from the start key of a page... blah blah
	char acceptable[MAX_KEY_BYTES];
	KEYSET ( acceptable , m_endKey , m_ks );
	KEYINC ( acceptable , m_ks );
	// watch out for wrap around...
	if ( KEYCMP(acceptable,KEYMIN(),m_ks)==0 )
		KEYSET ( acceptable , m_endKey , m_ks );
	char k[MAX_KEY_BYTES];

	static const int32_t roottitles_hashvalue = hash64Lower_a("roottitles", 10);

	while ( ! isExhausted() ) {
		getCurrentKey( k );
		// if titleRec, check size
		if ( rdbId == RDB_TITLEDB && ! KEYNEG(k) ) {
			int32_t dataSize = getCurrentDataSize();
			char *data = NULL;
			if ( dataSize >= 4 ) data = getCurrentData();
			if ( data &&
			     (*(int32_t *)data < 0 ||
			      *(int32_t *)data > 100000000 ) ) {
				gbshutdownAbort(true); }
		}
		// tagrec?
		if ( rdbId == RDB_TAGDB && ! KEYNEG(k) ) {
			Tag *tag = (Tag *)getCurrentRec();
			if ( tag->m_type == roottitles_hashvalue ) {
				char *tdata = tag->getTagData();
				int32_t tsize = tag->getTagDataSize();
				// core if tag val is not \0 terminated
				if ( tsize > 0 && tdata[tsize-1]!='\0' ) {
					log(LOG_ERROR, "db: bad root title tag");
					gbshutdownAbort(true); }
			}
		}
		if ( rdbId == RDB_SPIDERDB && ! KEYNEG(k) &&
		     getCurrentDataSize() > 0 ) {
			char *rec = getCurrentRec();
			// bad url in spider request?
			if ( g_spiderdb.isSpiderRequest ( (key128_t *)rec ) ){
				SpiderRequest *sr = (SpiderRequest *)rec;
				if ( sr->isCorrupt() ) {
					log(LOG_ERROR, "db: spider req corrupt");
					gbshutdownAbort(true);
				}
			}
		}
		// title bad uncompress size?
		if ( rdbId == RDB_TITLEDB && ! KEYNEG(k) ) {
			char *rec = getCurrentRec();
			int32_t usize = *(int32_t *)(rec+12+4);
			if ( usize <= 0 || usize>100000000 ) {
				log(LOG_ERROR, "db: bad titlerec uncompress size");
				gbshutdownAbort(true);
			}
		}

		if ( KEYCMP(k,m_startKey,m_ks)<0 ) {
			log("db: Key before start key in list of records.");
			char logbuf1[50],logbuf2[50];
			log("db: sk=%s",KEYSTR(m_startKey,m_ks,logbuf1));
			log("db: k2=%s",KEYSTR(k,m_ks,logbuf2));
			if ( abortOnProblem ) { gbshutdownAbort(true); }
			return false;
		}
		if ( KEYCMP(k,oldk,m_ks)<0 ) {
			log(
			    "db: Key out of order in list of records.");
			char logbuf1[50],logbuf2[50];
			log("db: k1=%s",KEYSTR(oldk,m_ks,logbuf1));
			log("db: k2=%s",KEYSTR(k,m_ks,logbuf2));

			return false;
		}
		if ( KEYCMP(k,acceptable,m_ks)>0 ) {
			log("db: Key after end key in list of records.");
			//log("db: k.n1=%" PRIx32" k.n0=%" PRIx64,k.n1,k.n0);
			char logbuf1[50],logbuf2[50],logbuf3[50];
			log("db: k2=%s",KEYSTR(k,m_ks,logbuf1));
			log("db: ak=%s",KEYSTR(acceptable,m_ks,logbuf2));
			log("db: ek=%s",KEYSTR(m_endKey,m_ks,logbuf3));
			if ( abortOnProblem ) { gbshutdownAbort(true); }
			return false;
		}
		// check for delete keys
		if ( KEYNEG(k) ) {
			// ensure delete keys have no dataSize
			if ( m_fixedDataSize == -1 &&
			     getCurrentDataSize() != 0 ) {
				log( LOG_WARN, "db: Got negative key with positive dataSize.");
				// what's causing this???
				gbshutdownAbort(true);
			}
		}

		KEYSET ( oldk , k , m_ks );
		// save old guy
		char *saved = m_listPtr;

		// advance to next guy
		skipCurrentRecord();

		// sometimes dataSize is too big in corrupt lists
		if ( m_listPtr > m_listEnd ) {
			log(LOG_ERROR, "db: Got record with bad data size field. Corrupted data file.");
			if ( abortOnProblem ) { gbshutdownAbort(true); }
			return false;
		}
		// don't go backwards, and make sure to go forwards at
		// least 6 bytes, the min size of a key (half key)
		if ( m_listPtr < saved + 6 ) {
			log(LOG_ERROR, "db: Got record with bad data size field. Corrupted data file.");
			if ( abortOnProblem ) {gbshutdownAbort(true);}
			return false;
		}
	}
	// . check last key
	// . oldk ALWAYS has the half bit clear, so clear it on lastKey
	// . this isn't so much a check for corruption as it is a check
	//   to see if the routines that set the m_lastKey were correct
	if ( m_lastKeyIsValid && KEYCMP(oldk,m_lastKey,m_ks) != 0 ) {
		log(LOG_LOGIC, "db: rdbList: checkList_r: Got bad last key.");
		char logbuf1[50],logbuf2[50];
		log(LOG_LOGIC, "db: rdbList: checkList_r: key=%s", KEYSTR(oldk,m_ks,logbuf1));
		log(LOG_LOGIC, "db: rdbList: checkList_r: key=%s", KEYSTR(m_lastKey,m_ks,logbuf2));
		if ( abortOnProblem ) {gbshutdownAbort(true);}
		// fix it
		KEYSET(m_lastKey,oldk,m_ks);
	}
	// . otherwise, last key is now valid
	// . this is only good for the call to Msg5::getRemoteList()
	if ( ! m_lastKeyIsValid ) {
		KEYSET(m_lastKey,oldk,m_ks);
		m_lastKeyIsValid = true;
	}

	// don't do this any more cuz we like to call merge_r back-to-back
	// and like to keep our m_listPtr/m_listPtrHi intact
	//resetListPtr();

	// all is ok
	return true;
}

// . return false and set g_errno on error
// . repairlist repair the list
bool RdbList::removeBadData_r ( ) {
	int32_t  orderCount = 0;
	int32_t  rangeCount = 0;
	int32_t  loopCount  = 0;
	assert(this);
	log("rdblist: trying to remove bad data from list");
 top:
	if ( ++loopCount >= 2000 ) {
		log("db: Giving up on repairing list. It is probably "
		    "a big chunk of low keys followed by a big chunk of "
		    "high keys and should just be patched by a twin.");
		reset();
		return true;
	}

	resetListPtr();
	// . if not fixed size, remove all the data for now
	// . TODO: make this better, man
	if ( m_fixedDataSize == -1 ) {
		// don't call reset because it sets m_ks back to 12
		//reset();
		m_listSize = 0;
		m_list = NULL;
		m_listPtr = NULL;
		m_listEnd = NULL;
		m_mergeMinListSize = -1;
		m_lastKeyIsValid = false;
		return true;
	}
	char  oldk[MAX_KEY_BYTES]={0};
	int32_t  oldRecSize = 0;
	char *bad     = NULL;
	char *badEnd  = NULL;
	int32_t  oldSize = m_listSize;
	int32_t  minSize = m_ks - 6;
	// posdb recs can be 6 12 or 18 bytes
	if ( m_ks == 18 ) minSize = 6;
	while ( ! isExhausted() ) {
		char *rec = getCurrentRec();
		// watch out for rec sizes that are too small
		//if ( rec + 6 > m_listEnd ) {
		if ( rec + minSize > m_listEnd ) {
			log("db: Record size of %" PRId32" is too big. "
			    "Truncating list at record.",minSize);
			m_listEnd = rec;
			m_listSize = m_listEnd - m_list;
			goto top;
		}
		int32_t size = getCurrentRecSize();
		// or too big
		if ( rec + size > m_listEnd ) {
			log("db: Record size of %" PRId32" is too big. "
			    "Truncating list at record.",size);
			m_listEnd = rec;
			m_listSize = m_listEnd - m_list;
			goto top;
		}
		// size must be at least 6 -- corruption causes negative sizes
		//if ( size < 6 ) {
		if ( size < minSize ) {
			log( "db: Record size of %" PRId32" is too small. "
			    "Truncating list at record.",size);
			m_listEnd = rec;
			m_listSize = m_listEnd - m_list;
			goto top;
		}
		char k[MAX_KEY_BYTES];
		getCurrentKey ( k );
		//if ( k < m_startKey || k > m_endKey ) {
		if ( KEYCMP(k,m_startKey,m_ks)<0 || KEYCMP(k,m_endKey,m_ks)>0){
			// if this is the first bad rec, mark it
			if ( ! bad ) {
				bad    = rec ;
				badEnd = rec ;
			}
			// advance end ptr
			badEnd += size;
			// skip this key
			skipCurrentRecord();
			rangeCount++;
			continue;
		}
		// . if bad already set from bad range, extract it now in
		//   case we also have an out of order key which sets its own
		//   bad range
		// . if we were good, bury any badness we might have had before
		if ( bad ) {
			int32_t n = m_listEnd - badEnd;
			memmove ( bad , badEnd , n );
			// decrease list size
			int32_t bsize = badEnd - bad;
			m_listSize -= bsize;
			m_listEnd  -= bsize;
			bad = NULL;
			goto top;
		}
		// if we don't remove out of order keys, then we might
		// get out of order keys in the map, causing us not to be
		// able to load because we won't get passed RdbMap::verifyMap()
		if ( KEYCMP(k,oldk,m_ks)<0 && oldRecSize ) {
			// bury both right away
			bad    = rec - oldRecSize;
			badEnd = rec + size;
			int32_t n = m_listEnd - badEnd;
			memmove ( bad , badEnd , n );
			// decrease list size
			int32_t bsize = badEnd - bad;
			m_listSize -= bsize;
			m_listEnd  -= bsize;
			orderCount++;
			// we don't keep a stack of old rec sizes so we
			// must start over from the top... can make us take
			// quite long... TODO: make it more efficient
			goto top;
		}
		// save k for setting m_lastKey correctly
		KEYSET(oldk,k,m_ks);
		oldRecSize = size;
		skipCurrentRecord();
	}
	// if we had badness at the end, bury it, no memmove required
	if ( bad ) {
		// decrease list size
		int32_t bsize = badEnd - bad;
		m_listSize -= bsize;
		m_listEnd  -= bsize;
	}
	// ensure m_lastKey
	//m_lastKey = oldk;
	KEYSET(m_lastKey,oldk,m_ks);
	m_lastKeyIsValid = true;

	resetListPtr();
	// msg -- taken out since will be in thread usually
	log(
	    "db: Removed %" PRId32" bytes of data from list to make it sane." ,
	    oldSize-m_listSize );
	log(
	    "db: Removed %" PRId32" recs to fix out of order problem.",orderCount*2);
	log(
	    "db: Removed %" PRId32" recs to fix out of range problem.",rangeCount  );

	// all is ok
	return true;
}


int RdbList::printPosdbList() {
	// save
	char *oldp   = m_listPtr;
	const char *oldphi = m_listPtrHi;
	resetListPtr();
	char logbuf1[50];
	logf(LOG_DEBUG, "db: STARTKEY=%s, m_ks=%d, datasize=%" PRId32,KEYSTR(m_startKey,m_ks,logbuf1), (int)m_ks, m_listSize);

	size_t key_size;
	// 48bit 38bit 4bit 4bit 18bit
	logf(LOG_DEBUG,"db:   ........term_id ......doc_id rank lang wordpos del ");


	while ( ! isExhausted() ) {
		char k[MAX_KEY_BYTES];
		getCurrentKey(k);

		if( m_ks == 18 )
		{
	        if(m_listPtr[0]&0x04) {
	                //it is a 6-byte pos key
	                key_size = 6;
	        } else if(m_listPtr[0]&0x02) {
	                //it is a 12-byte docid+pos key
	                key_size = 12;
	        } else {
	                key_size = 18;
	        }
		}
		else
		{
			key_size = m_ks;
		}

		char *key = &m_listPtr[0];

		uint64_t term_id = 0;
		uint64_t doc_id = 0;
		uint64_t site_rank = 0;
		uint64_t lang_id = 0;
//		uint64_t alignment_bit0 = 0;
		uint64_t lang_bit6 = 0;

		if( key_size == 18 )
		{
			term_id				= extract_bits(key,96,144);
		}

		if( key_size >= 12 )
		{
	        doc_id				= extract_bits(key,58,96);
//	        alignment_bit0		= extract_bits(key,57,58);
        	site_rank			= extract_bits(key,53,57);
        	lang_id				= extract_bits(key,48,53);
	        lang_bit6 			= extract_bits(key, 3, 4);
	        if(lang_bit6!=0) {
                lang_id |= 0x20;
			}
		}

        uint64_t	word_pos			= extract_bits(key,30,48);
//        uint64_t	hash_group			= extract_bits(key,26,30);
//        uint64_t	word_spam_rank		= extract_bits(key,22,26);
//        uint64_t	diversity_rank		= extract_bits(key,18,22);
//        uint64_t	synonym_flags		= extract_bits(key,16,18);
//        uint64_t	density_rank		= extract_bits(key,11,16);
//        uint64_t	in_outlink_text		= extract_bits(key,10,11);
//        uint64_t	alignment_bit1		= extract_bits(key, 9,10);
//        uint64_t	nosplit				= extract_bits(key, 8, 9);
//        uint64_t	multiplier			= extract_bits(key, 4, 8);
        uint64_t	nodelete_marker		= extract_bits(key, 0, 1);

		switch(key_size)
		{
			case 18:
				logf(LOG_DEBUG,"db:   %15" PRId64" %12" PRId64" %4" PRId64" %4" PRId64" %7" PRId64" %3s", term_id, doc_id, site_rank, lang_id, word_pos, !nodelete_marker?"Y":"N");
				break;
			case 12:
				logf(LOG_DEBUG,"db:   %15s %12" PRId64" %4" PRId64" %4" PRId64" %7" PRId64" %3s", "-", doc_id, site_rank, lang_id, word_pos, !nodelete_marker?"Y":"N");
				break;
			default:
				logf(LOG_DEBUG,"db:   %15s %12s %4s %4s %7" PRId64" %3s", "-", "-", "-", "-", word_pos, !nodelete_marker?"Y":"N");
				break;
		}

		skipCurrentRecord();
	}

	if ( m_lastKeyIsValid )
		logf(LOG_DEBUG,  "db: LASTKEY=%s", KEYSTR(m_lastKey,m_ks,logbuf1));

	logf(LOG_DEBUG, "db: ENDKEY=%s",KEYSTR(m_endKey,m_ks,logbuf1));

	//resetListPtr();
	m_listPtr   = oldp;
	m_listPtrHi = oldphi;

	return 0;
}


int RdbList::printList() {
	if ( m_ks == 18 ) { // m_rdbId == RDB_POSDB ) {
		return printPosdbList();
	}

	// save
	char *oldp   = m_listPtr;
	const char *oldphi = m_listPtrHi;
	resetListPtr();
	char logbuf1[50];
	logf(LOG_DEBUG, "db: STARTKEY=%s",KEYSTR(m_startKey,m_ks,logbuf1));

	while ( ! isExhausted() ) {
		char k[MAX_KEY_BYTES];
		getCurrentKey(k);
		int32_t dataSize = getCurrentDataSize();

		const char *d;
		if ( KEYNEG(m_listPtr) ) {
			d = " (del)";
		} else {
			d = "";
		}

		logf(LOG_DEBUG, "db: k=%s dsize=%07" PRId32"%s", KEYSTR(k,m_ks,logbuf1),dataSize,d);
		skipCurrentRecord();
	}

	if ( m_lastKeyIsValid )
		logf(LOG_DEBUG,  "db: LASTKEY=%s", KEYSTR(m_lastKey,m_ks,logbuf1));

	logf(LOG_INFO, "db: ENDKEY=%s",KEYSTR(m_endKey,m_ks,logbuf1));

	m_listPtr   = oldp;
	m_listPtrHi = oldphi;

	return 0;
}


// . ensure all recs in this list are in [startKey,endKey]
// . used to ensure that m_listSize does not exceed minRecSizes by more than
//   one record, but we'd have to change the endKey then!!! so i took it out.
// . only for use by indexdb and dbs that use half keys
// . returns false and sets g_errno on error, true otherwise
// . "offsetHint" is where to start looking for the last key <= endKey
// . it shoud have been supplied by Msg3's RdbMap
// . this is only called by Msg3.cpp
// . CAUTION: destructive! may write 6 bytes so key at m_list is 12 bytes
// . at hintOffset bytes offset into m_list, the key is hintKey
// . these hints allow us to constrain the tail without looping over all recs
// . CAUTION: ensure we update m_lastKey and make it valid if m_listSize > 0
// . mincRecSizes is really only important when we read just 1 list
// . it's a really good idea to keep it as -1 otherwise
bool RdbList::constrain(const char *startKey, char *endKey, int32_t minRecSizes,
                        int32_t hintOffset, const char *hintKey, rdbid_t rdbId, const char *filename) {
//	log(LOG_TRACE,"RdbList(%p)::constrain()",this);
	assert(this);
	verify_signature();
	// return false if we don't own the data
	if ( ! m_ownData ) {
		g_errno = EBADLIST;
		log(LOG_WARN, "db: constrain: Data not owned.");
		return false;
	}

	// bail if empty
	if ( m_listSize == 0 ) {
		// tighten the keys
		KEYSET(m_startKey,startKey,m_ks);
		KEYSET(m_endKey,endKey,m_ks);
		return true;
	}

	// ensure we our first key is 12 bytes if m_useHalfKeys is true
	if ( m_useHalfKeys && isHalfBitOn ( m_list ) ) {
		g_errno = ECORRUPTDATA;
		log(LOG_WARN, "db: First key is 6 bytes. Corrupt data file.");
		return false;
	}

	// sanity. hint key should be full key
	if ( m_ks == 18 && hintKey && (hintKey[0]&0x06)) {
		g_errno = ECORRUPTDATA;
		log(LOG_WARN, "db: Hint key is corrupt.");
		return false;
	}

	if ( hintOffset > m_listSize ) {
		g_errno = ECORRUPTDATA;
		log(LOG_WARN, "db: Hint offset %" PRId32" > %" PRId32" is corrupt.", hintOffset, m_listSize);
		return false;
	}

	if ( rdbId == RDB_POSDB ) {
		return posdbConstrain(startKey, endKey, minRecSizes, hintOffset, hintKey, filename);
	}

	// save original stuff in case we encounter corruption so we can
	// roll it back and let checkList_r and repairList_r deal with it
	char *savelist      = m_list;
	const char *savelistPtrHi = m_listPtrHi;
	const char *savelistPtrLo = m_listPtrLo;

#ifdef GBSANITYCHECK
	char logbuf1[50];
	char lastKey[MAX_KEY_BYTES];
	KEYMIN(lastKey,m_ks);
#endif

	// . remember the start of the list at the beginning
	// . hint is relative to this
	char *firstStart = m_list;

	// reset our m_listPtr and m_listPtrHi
	resetListPtr();

	// point to start of this list to constrain it
	char *p = m_list;

	// . advance "p" while < startKey
	// . getKey() needsm_listPtrHi to be correct
	char k[MAX_KEY_BYTES];

	while ( p < m_listEnd ) {
		getKey(p,k);
#ifdef GBSANITYCHECK
		// check key order!
		if ( KEYCMP(k,lastKey,m_ks)<= 0 ) {
			log("constrain: key=%s out of order",
			    KEYSTR(k,m_ks,logbuf1));
			gbshutdownAbort(true);
		}
		KEYSET(lastKey,k,m_ks);
#endif
		// stop if we are >= startKey
		if ( KEYCMP(k,startKey,m_ks) >= 0 ) {
			break;
		}

#ifdef GBSANITYCHECK
		// debug msg
		log("constrain: skipping key=%s rs=%" PRId32, KEYSTR(k,m_ks,logbuf1), getRecSize(p));
#endif

		// . since we don't call skipCurrentRec() we must update m_listPtrHi ourselves
		// . this is fruitless if m_useHalfKeys is false...
		if (!isHalfBitOn(p)) {
			m_listPtrHi = p + (m_ks - 6);
		}

		// posdb uses two compression bits
		if (m_ks == 18 && !(p[0] & 0x04)) {
			m_listPtrLo = p + (m_ks - 12);
		}

		// get size of this rec, this can be negative if corrupt!
		int32_t recSize = getRecSize ( p );

		// watch out for corruption, let Msg5 fix it
		if ( recSize < 0 ) {
			m_listPtrHi = savelistPtrHi ;
			m_listPtrLo = savelistPtrLo ;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Got record size of %" PRId32" < 0. Corrupt data file.",recSize);
			return false;
		}

		p += recSize;
	}

	// . if p is exhausted list is empty, all keys were under startkey
	// . if p is already over endKey, we had no keys in [startKey,endKey]
	// . I don't think this call is good if p >= listEnd, it would go out
	//   of bounds
	//   corrupt data could send it well beyond listEnd too.
	if ( p < m_listEnd ) {
		getKey(p, k);
	}

	if ( p >= m_listEnd || KEYCMP(k,endKey,m_ks)>0 ) {
		// make list empty
		m_listSize  = 0;
		m_listEnd   = m_list;
		// tighten the keys
		KEYSET(m_startKey,startKey,m_ks);
		KEYSET(m_endKey,endKey,m_ks);
		// reset to set m_listPtr and m_listPtrHi
		resetListPtr();
		return true;
	}

	// posdb uses two compression bits
	if ( m_ks == 18 && (p[0] & 0x06) ) {
		// store the full key into "k" buffer
		getKey(p,k);
		// how far to go back?
		if ( p[0] & 0x04 ) {
			p -= 12;
		} else {
			p -= 6;
		}

		// write the full key back into "p"
		KEYSET(p,k,m_ks);
	}
	// . if p points to a 6 byte key, make it 12 bytes
	// . this is the only destructive part of this function
	else if ( m_useHalfKeys && isHalfBitOn ( p ) ) {
		// the key returned should have half bit cleared
		getKey(p,k);
		// write the key back 6 bytes
		p -= 6;
		KEYSET(p,k,m_ks);
	}

#ifdef GBSANITYCHECK
	log("constrain: hk=%s",KEYSTR(hintKey,m_ks,logbuf1));
	log("constrain: hintOff=%" PRId32,hintOffset);
#endif

	// inc m_list , m_alloc should remain where it is
	m_list = p;

	// . set p to the hint
	// . this is the last key in the map before the endkey i think
	// . saves us from having to scan the WHOLE list
	p = firstStart + hintOffset;


	// Sanity
	if( !hintKey ) {
		logError("hintKey is NULL before use!");
		gbshutdownAbort(true);
	}
	// set our hi key temporarily cuz the actual key in the list may
	// only be the lower 6 bytes
	//m_listPtrHi = ((char *)&hintKey) + 6;
	m_listPtrHi = hintKey + (m_ks-6);
	m_listPtrLo = hintKey + (m_ks-12);

	// . store the key @p into "k"
	// . "k" should then equal the hint key!!! check it below
	getKey(p,k);

	// . dont' start looking for the end before our new m_list
	// . don't start at m_list+6 either cuz we may have overwritten that
	//   with the *(key96_t *)p = k above!!!! tricky...
	if ( p < m_list + m_ks ) {
		p           = m_list;
		m_listPtr   = m_list;
		//m_listPtrHi = m_list + 6;
		m_listPtrHi = m_list + (m_ks-6);
		m_listPtrLo = m_list + (m_ks-12);
	}
	// . if first key is over endKey that's a bad hint!
	// . might it be a corrupt RdbMap?
	// . reset "p" to beginning if hint is bad
	else if ( hintKey && (KEYCMP(k,hintKey,m_ks)!=0 || KEYCMP(hintKey,endKey,m_ks)>0) ) {
		log(LOG_WARN, "db: Corrupt data or map file. Bad hint for %s.", filename);
		// . until we fix the corruption, drop a core
		// . no, a lot of files could be corrupt, just do it for merge
		//gbshutdownAbort(true);
		p           = m_list;
		m_listPtr   = m_list;
		m_listPtrHi = m_list + (m_ks-6);
		m_listPtrLo = m_list + (m_ks-12);
	}

	// . max a max ptr based on minRecSizes
	// . if p hits or exceeds this we MUST stop
	char *maxPtr = m_list + minRecSizes;

	// watch out for wrap around!
	if ( (intptr_t)maxPtr < (intptr_t)m_list ) {
		maxPtr = m_listEnd;
	}

	// if mincRecSizes is -1... do not constrain on this
	if ( minRecSizes < 0 ) {
		maxPtr = m_listEnd;
	}

	// size of last rec we read in the list
	int32_t size = -1 ;

	// advance until endKey or minRecSizes kicks us out
	while ( p < m_listEnd ) {
		getKey(p,k);
		if ( KEYCMP(k,endKey,m_ks)>0 ) break;
		if ( p >= maxPtr ) break;
		size = getRecSize ( p );
		// watch out for corruption, let Msg5 fix it
		if ( size < 0 ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Corrupt record size of %" PRId32" bytes in %s. line=%d", size, filename, __LINE__);
			return false;
		}
		// set hiKey in case m_useHalfKeys is true for this list
		if ( size == m_ks ) {
			m_listPtrHi = p + (m_ks-6) ;
		}

		// posdb uses two compression bits
		if ( m_ks == 18 && !(p[0]&0x04)) {
			m_listPtrLo = p + (m_ks-12);
		}

		// watch out for wrap
		char *oldp = p;
		p += size;

		// if size is corrupt we can breech the whole list and cause
		// m_listSize to explode!!!
		if ( (intptr_t)p > (intptr_t)m_listEnd || (intptr_t)p < (intptr_t)oldp ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Corrupt record size of %" PRId32" bytes in %s. line=%d", size, filename, __LINE__);
			return false;
		}
	}
	// . if minRecSizes was limiting constraint, reset m_endKey to lastKey
	// . if p equals m_listEnd it is ok, too... this happens mostly when
	//   we get the list from the tree so there is not *any* slack
	//   left over.
	if ( p < m_listEnd ) {
		getKey(p,k);
	}

	if ( p < m_listEnd && KEYCMP(k,endKey,m_ks)<=0 && p>=maxPtr && size>0){
		// this line seemed to have made us make corrupt lists. So
		// deal with the slack in Msg5 directly.
		//(p == m_listEnd && p >= maxPtr && size >0) ) {
		// watch out for corruption, let Msg5 fix it
		if ( p - size < m_alloc ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Corrupt record size of %" PRId32" bytes in %s. line=%d", size, filename, __LINE__);
			return false;
		}
		// set endKey to last key in our constrained list
		//endKey = getKey ( p - size );
		getKey(p-size,endKey);
	}
	// cut the tail
	m_listEnd   = p;
	m_listSize  = m_listEnd - m_list;
	// bitch if size is -1 still
	if ( size == -1 ) {
		log(LOG_ERROR, "db: Encountered bad endkey in %s. listSize=%" PRId32, filename, m_listSize);
		gbshutdownAbort(true);
	}
	// otherwise store the last key if size is not -1
	else if ( m_listSize > 0 ) {
		//m_lastKey        = getKey ( p - size );
		getKey(p-size,m_lastKey);
		m_lastKeyIsValid = true;
	}

	// reset to set m_listPtr and m_listPtrHi
	resetListPtr();

	// and the keys can be tightened
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);
	verify_signature();
//	log(LOG_TRACE,"RdbList(%p)::constrain(): finished",this);
	return true;
}

static void getPosdbKey(const char *rec , char *key) {
	// p[0] = 0x06 (size 6), p[0] = 0x02 (size 12), p[0] = 0x00 (size 18)
	if (rec[0] & 0x04) {
		memcpy(key, rec, 6);
		// clear compression bits
		key[0] &= 0xf9;
	} else if (rec[0] & 0x02) {
		memcpy(key, rec, 12);
		// clear compression bits
		key[0] &= 0xf9;
	} else {
		memcpy(key, rec, 18);
	}
}

bool RdbList::posdbConstrain(const char *startKey, char *endKey, int32_t minRecSizes,
                             int32_t hintOffset, const char *hintKey, const char *filename) {
	// sanity
	if ( m_ks != sizeof(key144_t) ) {
		gbshutdownAbort(true);
	}

	// save original stuff in case we encounter corruption so we can
	// roll it back and let checkList_r and repairList_r deal with it
	char *savelist      = m_list;
	const char *savelistPtrHi = m_listPtrHi;
	const char *savelistPtrLo = m_listPtrLo;

#ifdef GBSANITYCHECK
	char lastKey[MAX_KEY_BYTES];
	KEYMIN(lastKey,m_ks);
#endif

	// . remember the start of the list at the beginning
	// . hint is relative to this
	char *firstStart = m_list;

	// reset our m_listPtr and m_listPtrHi
	resetListPtr();

	// point to start of this list to constrain it
	char *p = m_list;

	// . advance "p" while < startKey
	// . getKey() needsm_listPtrHi to be correct
	char k[MAX_KEY_BYTES];

	while ( p < m_listEnd ) {
		getPosdbKey(p, k);

#ifdef GBSANITYCHECK
		// check key order!
		if ( KEYCMP(k,lastKey,m_ks)<= 0 ) {
			log("constrain: key=%s out of order",
			    KEYSTR(k,m_ks,logbuf1));
			gbshutdownAbort(true);
		}
		KEYSET(lastKey,k,m_ks);
#endif
		// stop if we are >= startKey
		if (KEYCMP(k, startKey, 18) >= 0) {
			break;
		}

#ifdef GBSANITYCHECK
		// debug msg
		log("constrain: skipping key=%s rs=%" PRId32, KEYSTR(k,m_ks,logbuf1), getRecSize(p));
#endif
		int32_t recSize = 18;
		if (p[0] & 0x04) {
			recSize = 6;
		} else if (p[0] & 0x02) {
			recSize = 12;
			m_listPtrLo = p + 6;
		} else {
			m_listPtrHi = p + 12;
			m_listPtrLo = p + 6;
		}
		p += recSize;
	}

	// . if p is exhausted list is empty, all keys were under startkey
	// . if p is already over endKey, we had no keys in [startKey,endKey]
	// . I don't think this call is good if p >= listEnd, it would go out of bounds
	//   corrupt data could send it well beyond listEnd too.
	if (p < m_listEnd) {
		getPosdbKey(p, k);
	}

	if (p >= m_listEnd || KEYCMP(k, endKey, 18) > 0) {
		// make list empty
		m_listSize  = 0;
		m_listEnd   = m_list;

		// tighten the keys
		KEYSET(m_startKey, startKey, 18);
		KEYSET(m_endKey, endKey, 18);

		// reset to set m_listPtr and m_listPtrHi
		resetListPtr();
		return true;
	}

	if ((p[0] & 0x06)) {
		// how far to go back?
		if (p[0] & 0x04) {
			p -= 12;
		} else {
			p -= 6;
		}

		// write the full key back into "p"
		KEYSET(p, k, 18);
	}

	// inc m_list , m_alloc should remain where it is
	m_list = p;

	// . set p to the hint
	// . this is the last key in the map before the endkey i think
	// . saves us from having to scan the WHOLE list
	p = firstStart + hintOffset;

	// set our hi key temporarily cuz the actual key in the list may
	// only be the lower 6 bytes
	m_listPtrHi = hintKey + 12;
	m_listPtrLo = hintKey + 6;

	// . store the key @p into "k"
	// . "k" should then equal the hint key!!! check it below
	getKey(p,k);

	bool resetPtr = false;
	// . dont' start looking for the end before our new m_list
	// . don't start at m_list+6 either cuz we may have overwritten that with the *(key96_t *)p = k above!!!! tricky...
	if ( p < m_list + 18 ) {
		resetPtr = true;
	} 
	else { 
		// Sanity
		if( !hintKey ) {
			logError("hintKey is NULL before use!");
			gbshutdownAbort(true);
		}
		
		if (KEYCMP(k, hintKey, 18) != 0 || KEYCMP(hintKey, endKey, 18) > 0) {
			// . if first key is over endKey that's a bad hint!
			// . might it be a corrupt RdbMap?
			// . reset "p" to beginning if hint is bad
			log(LOG_WARN, "db: Corrupt data or map file. Bad hint for %s.", filename);
			resetPtr = true;
		}
	}

	if (resetPtr) {
		p = m_list;
		m_listPtr = m_list;
		m_listPtrHi = m_list + 12;
		m_listPtrLo = m_list + 6;
	}

	// . max a max ptr based on minRecSizes
	// . if p hits or exceeds this we MUST stop
	char *maxPtr = m_list + minRecSizes;

	// watch out for wrap around!
	if ( (intptr_t)maxPtr < (intptr_t)m_list ) {
		maxPtr = m_listEnd;
	}

	// if mincRecSizes is -1... do not constrain on this
	if ( minRecSizes < 0 ) {
		maxPtr = m_listEnd;
	}

	// size of last rec we read in the list
	int32_t recSize = -1;

	// advance until endKey or minRecSizes kicks us out
	while ( p < m_listEnd ) {
		getPosdbKey(p, k);

		if (KEYCMP(k, endKey, 18) > 0) {
			break;
		}

		if (p >= maxPtr) {
			break;
		}

		recSize = 18;
		if (p[0] & 0x04) {
			recSize = 6;
		} else if (p[0] & 0x02) {
			recSize = 12;
			m_listPtrLo = p + 6;
		} else {
			m_listPtrHi = p + 12;
			m_listPtrLo = p + 6;
		}
		// watch out for wrap
		char *oldp = p;
		p += recSize;

		// if size is corrupt we can breech the whole list and cause
		// m_listSize to explode!!!
		if ( (intptr_t)p > (intptr_t)m_listEnd || (intptr_t)p < (intptr_t)oldp ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Corrupt record size of %" PRId32" bytes in %s. line=%d", recSize, filename, __LINE__);
			return false;
		}
	}

	// . if minRecSizes was limiting constraint, reset m_endKey to lastKey
	// . if p equals m_listEnd it is ok, too... this happens mostly when
	//   we get the list from the tree so there is not *any* slack
	//   left over.
	if (p < m_listEnd) {
		getPosdbKey(p, k);
	}

	if (p < m_listEnd && KEYCMP(k, endKey, 18) <= 0 && p >= maxPtr && recSize > 0) {
		// watch out for corruption, let Msg5 fix it
		if ( p - recSize < m_alloc ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			log(LOG_WARN, "db: Corrupt record size of %" PRId32" bytes in %s. line=%d", recSize, filename, __LINE__);
			return false;
		}

		// set endKey to last key in our constrained list
		getKey(p - recSize, endKey);
	}

	// cut the tail
	m_listEnd   = p;
	m_listSize  = m_listEnd - m_list;

	// bitch if size is -1 still
	if (recSize == -1) {
		log(LOG_ERROR, "db: Encountered bad endkey in %s. listSize=%" PRId32, filename, m_listSize);
		gbshutdownAbort(true);
	} else if ( m_listSize > 0 ) {
		// otherwise store the last key if size is not -1
		getKey(p - recSize, m_lastKey);
		m_lastKeyIsValid = true;
	}

	// reset to set m_listPtr and m_listPtrHi
	resetListPtr();

	// and the keys can be tightened
	KEYSET(m_startKey,startKey,18);
	KEYSET(m_endKey,endKey,18);
	return true;
}

// . merges a bunch of lists together
// . one of the most complicated routines in Gigablast
// . the newest record (in the highest list #) wins key ties
// . all provided lists must have their recs in [startKey,endKey]
//   so you should have called RdbList::constrain() on them
// . should only be used by Msg5 to merge diskLists (Msg3) and treeList
// . we no longer do annihilation, instead the newest key, be it negative
//   or positive, will override all the others
// . the logic would have been much simpler had we chosen to use distinct
//   keys for distinct titleRecs, but that would hurt our incremental updates
// . m_listPtr will equal m_listEnd when this is done so you can concantenate
//   with successive calls
// . we add merged lists to this->m_listPtr, NOT this->m_list
// . m_mergeMinListSize must be set appropriately by calling prepareForMerge()
//   before calling this
// . CAUTION: you should call constrain() on all "lists" before calling this
//   so we don't have to do boundary checks on the keys here
void RdbList::merge_r(RdbList **lists, int32_t numLists, const char *startKey, const char *endKey, int32_t minRecSizes,
                      bool removeNegRecs, rdbid_t rdbId, collnum_t collNum, int32_t startFileNum) {
	assert(this);
	verify_signature();
	// sanity
	if (!m_ownData) {
		log(LOG_ERROR, "list: merge_r data not owned");
		gbshutdownAbort(true);
	}

	// bail if none! i saw a doledb merge do this from Msg5.cpp
	// and it was causing a core because m_MergeMinListSize was -1
	if (numLists == 0) {
		return;
	}

	// save this
	int32_t startListSize = m_listSize;

	// did they call prepareForMerge()?
	if ( m_mergeMinListSize == -1 ) {
		log(LOG_LOGIC,"db: rdblist: merge_r: prepareForMerge() not called. ignoring error and returning emtpy list.");
		// this happens if we nuke doledb during a merge of it. it is just bad timing
		return;
		// save state and dump core, sigBadHandler will catch this
		// gbshutdownAbort(true);
	}

	// already there?
	if ( minRecSizes >= 0 && m_listSize >= minRecSizes ) {
		return;
	}

	// warning msg
	if ( m_listPtr != m_listEnd ) {
		log(LOG_LOGIC, "db: rdblist: merge_r: warning. merge not storing at end of list for %s.",
		    getDbnameFromId(rdbId));
	}

	// set our key range
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);

	// . MDW: this happens during the qainject1() qatest in qa.cpp that
	//   deletes all the urls then does a dump of just negative keys.
	//   so let's comment it out for now
	if ( KEYCMP(m_startKey,m_endKey,m_ks)!=0 && KEYNEG(m_endKey) ) {
		// make it legal so it will be read first NEXT time
		KEYDEC(m_endKey,m_ks);
	}

	// do nothing if no lists passed in
	if ( numLists <= 0 ) return;

	// inherit the key size of what we merge
	m_ks = lists[0]->m_ks;

	// sanity check
	for ( int32_t i = 1 ; i < numLists ; i++ ) {
		if ( lists[ i ]->m_ks != m_ks ) {
			log( LOG_WARN, "db: non conforming key size of %" PRId32" != %" PRId32" for "
			     "list #%" PRId32".", ( int32_t ) lists[ i ]->m_ks, ( int32_t ) m_ks, i );
			gbshutdownAbort(true);
		}
	}

	// bail if nothing requested
	if ( minRecSizes == 0 ) {
		return;
	}

	Rdb* rdb = getRdbFromId(rdbId);
	if (rdbId == RDB_POSDB) {
		posdbMerge_r(lists, numLists, startKey, endKey, m_mergeMinListSize, removeNegRecs, rdb->isUseIndexFile(), collNum, startFileNum);
		verify_signature();
		return;
	}

	// check that we're not using index for other rdb file than posdb
	if (rdb->isUseIndexFile()) {
		/// @todo ALC logic to use index file is not implemented for any rdb other than posdb. add it below if required
		gbshutdownLogicError();
	}

	int32_t required = -1;
	// . if merge not necessary, print a warning message.
	// . caller should have just called constrain() then
	if ( numLists == 1 ) {
		// we do this sometimes to remove the negative keys!!
		required = m_listSize + lists[0]->m_listSize;
	}
	// otherwise, list #j has the minKey, although may not be min
	int32_t  mini ;
	int32_t  i    ;
	// . find a value for "m_lastKey" that does not exist in any of lists
	// . we increment by 2 too
	// . if minKey is a delete, then make it a non-delete key
	// . add 2 to ensure that it stays a non-delete key
	char  lastKey[MAX_KEY_BYTES]={0};
	bool  lastKeyIsValid = false;
	char  lastPosKey[MAX_KEY_BYTES]={0};
	char  highestKey[MAX_KEY_BYTES];
	bool  firstTime = true;
	char  lastNegKey[MAX_KEY_BYTES]={0};
	int32_t  lastNegi = -1;

	// init highestKey
	KEYSET(highestKey,KEYMIN(),m_ks);

	// this is used for rolling back delete records
	int32_t lastListSize = m_listSize;

	// two vars for removing negative recs from the end of the final list
	int32_t  savedListSize = -1;
	char  savedLastKey[MAX_KEY_BYTES];
	char  savedHighestKey[MAX_KEY_BYTES];

	// reset each list's ptr
	for ( i = 0 ; i < numLists ; i++ ) lists[i]->resetListPtr();

	// don't breech the list's boundary when adding keys from merge
	char *allocEnd = m_alloc + m_allocSize;

	// now begin the merge loop
	char ckey[MAX_KEY_BYTES];
	char mkey[MAX_KEY_BYTES];
	char minKey[MAX_KEY_BYTES];

	// cleanup deprecated tags
	std::set<int64_t> remove_tags;
	if ( rdbId == RDB_TAGDB ) {
		/// @todo ALC only need this to clean out existing tagdb records. (remove once it's cleaned up!)
		remove_tags.insert( getTagTypeFromStr( "rootlang" ) );
		remove_tags.insert( getTagTypeFromStr( "manualfilter" ) );
		remove_tags.insert( getTagTypeFromStr( "dateformat" ) );
		remove_tags.insert( getTagTypeFromStr( "venueaddress" ) );
		remove_tags.insert( getTagTypeFromStr( "hascontactinfo" ) );
		remove_tags.insert( getTagTypeFromStr( "contactaddress" ) );
		remove_tags.insert( getTagTypeFromStr( "contactemails" ) );
		remove_tags.insert( getTagTypeFromStr( "hascontactform" ) );
		remove_tags.insert( getTagTypeFromStr( "ingoogle" ) );
		remove_tags.insert( getTagTypeFromStr( "ingoogleblogs" ) );
		remove_tags.insert( getTagTypeFromStr( "ingooglenews" ) );
		remove_tags.insert( getTagTypeFromStr( "abyznewslinks.address" ) );
		remove_tags.insert( getTagTypeFromStr( "sitenuminlinksuniqueip" ) );
		remove_tags.insert( getTagTypeFromStr( "sitenuminlinksuniquecblock" ) );
		remove_tags.insert( getTagTypeFromStr( "sitenuminlinkstotal" ) );
		remove_tags.insert( getTagTypeFromStr( "comment" ) );
		remove_tags.insert( getTagTypeFromStr( "sitepop" ) );
		remove_tags.insert( getTagTypeFromStr( "sitenuminlinksfresh" ) );
		remove_tags.insert( getTagTypeFromStr( "pagerank" ) );
		remove_tags.insert( getTagTypeFromStr( "ruleset" ) );
	}

top:
	// get the biggest possible minKey so everyone's <= it
	KEYSET(minKey,KEYMAX(),m_ks);

	// assume we have no min key
	mini = -1;

	// . loop over the lists
	// . get newer rec with same key as older rec FIRST
	for ( i = 0 ; i < numLists ; i++ ) {
		// TODO: to speed up extract from list of RdbLists
		if ( lists[i]->isExhausted() ) {
			continue;
		}

		// see if the current key from this scan's read buffer is 2 big
		lists[i]->getCurrentKey(ckey);
		KEYSET(mkey,minKey,m_ks);

		// treat negatives and positives as equals for this
		*ckey |= 0x01;
		*mkey |= 0x01;

		// clear compression bits if posdb
		if ( m_ks == 18 ) {
			*ckey &= 0xf9;
		}

		if ( KEYCMP(ckey,mkey,m_ks) > 0 ) {
			continue;
		}

		// if this guy is newer and equal, skip the old guy
		if ( KEYCMP(ckey,mkey,m_ks)==0 && mini >= 0 ) {
			lists[ mini ]->skipCurrentRecord();
		}

		lists[i]->getCurrentKey(minKey);
		mini    = i;
	}

	// we're done if all lists are exhausted
	if ( mini == -1 ) {
		goto done;
	}

	if ( KEYCMP(minKey,endKey,m_ks)>0 ) {
		goto done;
	}

	if ( removeNegRecs && KEYNEG(minKey) ) {
		required -= m_ks;
		lastNegi   = mini;
		lists[mini]->getCurrentKey(lastNegKey);
		goto skip;
	}

	// special filter to remove obsolete tags from tagdb
	if ( rdbId == RDB_TAGDB ) {
		Tag *tag = (Tag *)lists[mini]->getCurrentRec();
		if ( remove_tags.find( tag->m_type ) != remove_tags.end() ) {
			required -= tag->getRecSize();
			goto skip;
		}
	}

	// remember state before we are stored in case we're annihilated and
	// we hafta roll back to it
	lastListSize   = m_listSize;

	// before storing key, if last key was negative and its
	// "i" was > our "i", and we match, then erase us...
	if ( lastNegi > mini ) {
		// does it annihilate us?
		if ( KEYCMPNEGEQ(minKey,lastNegKey,m_ks)==0 ) {
			goto skip;
		}

		// otherwise, we are beyond it...
		lastNegi = -1;
	}

	// . copy the winning record into our list
	// . these increment store at m_list+m_listSize and inc m_listSize
	if ( m_fixedDataSize == 0 ) {
		// if adding the key would breech us, goto done
		if (m_list + m_listSize + m_ks > allocEnd ) {
			goto done;
		}

		// add it using compression bits
		addRecord ( minKey ,0,NULL,false);
	} else {
		// if adding the key would breech us, goto done
		int32_t recSize=m_ks+lists[mini]->getCurrentDataSize();

		// negative keys have no datasize entry
		if (m_fixedDataSize < 0 && ! KEYNEG(minKey) ) {
			recSize += 4;
		}

		if (m_list + m_listSize + recSize > allocEnd) {
			goto done;
		}

		// . fix m_listEnd so it doesn't try to call growList() on us
		// . normally we don't set this right until we're done merging
		m_listEnd = m_list + m_listSize;

		// add the record to end of list
		addRecord ( minKey, lists[mini]->getCurrentDataSize(), lists[mini]->getCurrentData() );
	}

	// if we are positive and unannhilated, store it in case
	// last key we get is negative and removeNegRecs is true we need to
	// know the last positive key to set m_lastKey
	if ( !KEYNEG(minKey) ) {
		KEYSET(lastPosKey,minKey,m_ks);
	}

	KEYSET(lastKey,minKey,m_ks);
	lastKeyIsValid = true;

skip:
	// get the next key in line and goto top
	lists[mini]->skipCurrentRecord();
	// keep adding/merging more records if we still have more room w/o grow
	if ( m_listSize < m_mergeMinListSize ) {
		goto top;
	}

 done:
	// . is the last key we stored negative, a dangling negative?
	// . if not, skip this next section
	if ( lastKeyIsValid && !KEYNEG(lastKey) ) {
		goto positive;
	}

	// are negatives allowed?
	if ( removeNegRecs ) {
		// . keep chugging if there MAY be keys left
		// . they will replace us if they are added cuz "removeNegRecs" is true
		if ( mini >= 0 && KEYCMP(minKey,endKey,m_ks)<0 ) {
			goto top;
		}
		// . otherwise, all lists were exhausted
		// . peel the dangling negative off the top
		// . highestKey is irrelevant here cuz all lists are exhausted
		m_listSize = lastListSize;
		// fix this
		if ( required >= 0 ) {
			required = lastListSize;
		}
		KEYSET(lastKey,lastPosKey,m_ks);
	}

	// if all lists are exhausted, we're really done
	if ( mini < 0 ) {
		goto positive;
	}

	// . we are done iff the next key does not match us (+ or -)
	// . so keep running until last key is positive, or we
	//   have two different, adjacent negatives on the top at which time
	//   we can peel the last one off and accept the dangling negative
	// . if this is our first time here, set some flags
	if ( firstTime ) {
		// next time we come here, it won't be our first time
		firstTime = false;
		// save our state because next rec may not annihilate
		// with this one and be saved on the list and we have to
		// peel it off and accept this dangling negative as unmatched
		savedListSize   = m_listSize;
		KEYSET(savedLastKey,lastKey,m_ks);
		KEYSET(savedHighestKey,highestKey,m_ks);
		goto top;
	}

	// . if this is our second time here, the added key MUST be a
	//   negative that did not match
	// . if it was positive, we would have jumped to "positive:" above
	// . if it was a dup negative, it wouldn't have come here to done: yet
	// . roll back over that unnecessary unmatching negative key to
	//   expose our original negative key, an acceptable dangling negative
	m_listSize = savedListSize;
	KEYSET(lastKey,savedLastKey,m_ks);
	KEYSET(highestKey,savedHighestKey,m_ks);

 positive:
	// but don't set the listSize negative
	if ( m_listSize < 0 ) {
		m_listSize = 0;
	}

	// set these 2 things for our final merged list
	m_listEnd = m_list + m_listSize;
	m_listPtr = m_listEnd;

	// . set this for RdbMerge class i guess
	// . it may not actually be present if it was a dangling
	//   negative rec that we removed 3 lines above
	if ( m_listSize > startListSize ) {
		KEYSET(m_lastKey,lastKey,m_ks);
		m_lastKeyIsValid = true;
	}

	// mini can be >= 0 and no keys may remain... so check here
	for ( i = 0 ; i < numLists ; i++ )
		if ( ! lists[i]->isExhausted() ) break;
	bool keysRemain = (i < numLists);

	// . we only need to shrink the endKey if we fill up our list and
	//   there's still keys under m_endKey left over to merge
	// . if no keys remain to merge, then don't decrease m_endKey
	// . i don't want the endKey decreased unnecessarily because
	//   it means there's no recs up to the endKey
	if ( m_listSize >= minRecSizes && keysRemain ) {
		// the highestKey may have been annihilated, but it is still
		// good for m_endKey, just not m_lastKey
		char endKey[MAX_KEY_BYTES];
		if ( KEYCMP(m_lastKey,highestKey,m_ks)<0 )
			KEYSET(endKey,highestKey,m_ks);
		else
			KEYSET(endKey,m_lastKey ,m_ks);
		// if endkey is now negative we must have a dangling negative
		// so make it positive (dangling = unmatched)
		if ( KEYNEG(endKey) )
			KEYINC(endKey,m_ks);
		// be careful not to increase original endkey, though
		if ( KEYCMP(endKey,m_endKey,m_ks)<0 )
			KEYSET(m_endKey,endKey,m_ks);
	}

	// . sanity check. if merging one list, make sure we get it
	// . but if minRecSizes kicked us out first, then we might have less
	//   then "required"
	if ( required >= 0 && m_listSize < required && m_listSize<minRecSizes){
		gbshutdownAbort(true);
	}
}

////////
//
// SPECIALTY MERGE FOR POSDB
//
///////

bool RdbList::posdbMerge_r(RdbList **lists, int32_t numLists, const char *startKey, const char *endKey, int32_t minRecSizes,
                           bool removeNegKeys, bool useIndexFile, collnum_t collNum, int32_t startFileNum) {
	logTrace(g_conf.m_logTraceRdbList, "BEGIN");

	// sanity
	if (m_ks != sizeof(key144_t)) {
		gbshutdownAbort(true);
	}

	// no-op check
	if (numLists == 0) {
		return true;
	}

	logTrace(g_conf.m_logTraceRdbList, "lists=%p numLists=%" PRId32" minRecSizes=%" PRId32 " removeNegKeys=%s",
	         lists, numLists, minRecSizes, removeNegKeys ? "true" : "false");
	char logbuf1[50],logbuf2[50];
	logTrace(g_conf.m_logTraceRdbList, "startKey=%s endKey=%s", KEYSTR(startKey,m_ks,logbuf1), KEYSTR(endKey,m_ks,logbuf2));
	logTrace(g_conf.m_logTraceRdbList, "m_allocSize=%" PRId32" m_mergeMinListSize=%" PRId32, m_allocSize, m_mergeMinListSize);

	// did they call prepareForMerge()?
	if (m_allocSize < m_mergeMinListSize) {
		log(LOG_LOGIC, "db: rdblist: posdbMerge_r: prepareForMerge() not called.");
		// save state and dump core, sigBadHandler will catch this
		gbshutdownAbort(true);
	}

	// warning msg
	if (m_listPtr != m_listEnd) {
		log(LOG_LOGIC, "db: rdblist: posdbMerge_r: warning. merge not storing at end of list.");
	}

	// sanity check
	if (numLists > 0 && lists[0]->m_ks != m_ks) {
		gbshutdownAbort(true);
	}

	// set this list's boundary keys
	KEYSET(m_startKey, startKey, sizeof(key144_t));
	KEYSET(m_endKey, endKey, sizeof(key144_t));

	// bail if nothing requested
	if (minRecSizes == 0) {
		return true;
	}

	// maxPtr set by minRecSizes
	const char *maxPtr = m_list + minRecSizes;

	// watch out for wrap around
	if ((intptr_t)maxPtr < (intptr_t)m_list) {
		maxPtr = m_alloc + m_allocSize;
	}

	// don't exceed what we alloc'd though
	if (maxPtr > m_alloc + m_allocSize) {
		maxPtr = m_alloc + m_allocSize;
	}

	if (m_listSize) {
		logDebug(g_conf.m_logDebugBuild, "db: storing recs in a non-empty list for merge probably from recall from negative key loss");
	}

	// bitch if too many lists
	if (numLists > MAX_RDB_FILES + 1) {
		log(LOG_LOGIC, "db: rdblist: posdbMerge_r: Too many lists for merging.");
		gbshutdownAbort(true);
	}

	// initialize the arrays, 1-1 with the unignored lists
	const char  *ptrs[ MAX_RDB_FILES + 1 ];
	const char  *ends[ MAX_RDB_FILES + 1 ];
	char       hiKeys[ MAX_RDB_FILES + 1 ][6];
	char       loKeys[ MAX_RDB_FILES + 1 ][6];
	// set the ptrs that are non-empty
	int32_t n = 0;

	// convenience ptr
	for (int32_t i = 0; i < numLists; i++) {
		logTrace(g_conf.m_logTraceRdbList, "===== dumping list #%" PRId32" =====", i);

		// skip if empty
		if (lists[i]->isEmpty()) {
			logTrace(g_conf.m_logTraceRdbList, "empty list");
			continue;
		}

		if (g_conf.m_logTraceRdbList) {
			lists[i]->printList();
		}

		// . first key of a list must ALWAYS be 18 byte
		// . bitch if it isn't, that should be fixed!
		// . cheap sanity check
		if ((lists[i]->getList()[0]) & 0x06) {
			errno = EBADENGINEER;
			log(LOG_LOGIC,"db: posdbMerge_r: First key of list is a compressed key.");
			gbshutdownAbort(true);
		}

		// set ptrs
		ends[n] = lists[i]->getListEnd();
		ptrs[n] = lists[i]->getList();

		memcpy(hiKeys[n], lists[i]->getList() + 12, 6);
		memcpy(loKeys[n], lists[i]->getList() + 6, 6);

		n++;
	}

	// new # of lists, in case any lists were empty
	numLists = n;

	// . are all lists and trash exhausted?
	// . all their keys are supposed to be <= m_endKey
	if (numLists <= 0) {
		return true;
	}

	char *pp = NULL;

	// see Posdb.h for format of a 18/12/6-byte posdb key
	RdbIndexQuery rdbIndexQuery(getRdbBase(RDB_POSDB, collNum));
	char *new_listPtr = m_listPtr;
	int32_t listOffset = 0;

	while (numLists > 0 && new_listPtr < maxPtr) {
		// assume key in first list is the winner
		const char *minPtrBase = ptrs  [0]; // lowest  6 bytes
		const char *minPtrLo   = loKeys[0]; // next    6 bytes
		const char *minPtrHi   = hiKeys[0]; // highest 6 bytes
		int16_t mini = 0; // int16_t -> must be able to accomodate MAX_RDB_FILES!!

		logTrace(g_conf.m_logTraceRdbList, "new_listPtr=%p numLists=%" PRId32". assume key in the first list is the winner", new_listPtr, numLists);

		// merge loop over the lists, get the smallest key
		for (int32_t i = 1; i < numLists; i++) {
			char ss = bfcmpPosdb(minPtrBase, minPtrLo, minPtrHi, ptrs[i], loKeys[i], hiKeys[i]);

			// . continue if tie, so we get the oldest first
			// . treat negative and positive keys as identical for this
			if (ss < 0) {
				logTrace(g_conf.m_logTraceRdbList, "i=%" PRId32" ss < 0. continue", i);
				continue;
			}

			// advance old winner. this happens if this key is positive
			// and minPtrBase/Lo/Hi was a negative key! so this is
			// the annihilation. skip the positive key.
			if (ss == 0) {
				logTrace(g_conf.m_logTraceRdbList, "i=%" PRId32" ss == 0. skip", i);
				goto skip;
			}

			logTrace(g_conf.m_logTraceRdbList, "new min i=%" PRId32, i);

			// we got a new min
			minPtrBase = ptrs  [i];
			minPtrLo   = loKeys[i];
			minPtrHi   = hiKeys[i];
			mini     = i;
		}

		// ignore if negative i guess, just skip it
		if (removeNegKeys && KEYNEG(minPtrBase)) {
			logTrace(g_conf.m_logTraceRdbList, "removeNegKeys. skip");
			goto skip;
		}

		if (useIndexFile) {
			int64_t docId;

			if (minPtrBase[0] & 0x04) {
				// 6-byte pos key
				docId = extract_bits(minPtrLo, 10, 48);
			} else {
				// 12-byte docid+pos key
				docId = extract_bits(minPtrBase, 58, 96);
			}

			int32_t filePos = rdbIndexQuery.getFilePos(docId);

			if (filePos > (mini + listOffset) + startFileNum) {
				// docId is present in newer file
				logTrace(g_conf.m_logTraceRdbList, "docId in newer list. skip. filePos=%" PRId32" mini=%" PRId16, filePos, mini);
				goto skip;
			}
		}

		// save ptr
		pp = new_listPtr;

		// store key
		if (m_listPtrHi && cmp_6bytes_equal(minPtrHi, m_listPtrHi)) {
			if (m_listPtrLo && cmp_6bytes_equal(minPtrLo, m_listPtrLo)) {
				// 6-byte entry
				logTrace(g_conf.m_logTraceRdbList, "store 6-byte key");
				memcpy(new_listPtr, minPtrBase, 6);
				new_listPtr += 6;
				*pp |= 0x06; //turn on both compression bits
			} else {
				// 12-byte entry
				logTrace(g_conf.m_logTraceRdbList, "store 12-byte key");
				memcpy(new_listPtr, minPtrBase, 6);
				new_listPtr += 6;
				memcpy(new_listPtr, minPtrLo, 6);
				m_listPtrLo  = new_listPtr; // point to the new lo key
				new_listPtr += 6;
				*pp = (*pp&~0x04)|0x02; //turn on exactly 1 compression bit
			}
		} else {
			// 18-byte entry
			logTrace(g_conf.m_logTraceRdbList, "store 18-byte key");
			memcpy(new_listPtr, minPtrBase, 6);
			new_listPtr += 6;
			memcpy(new_listPtr, minPtrLo, 6);
			m_listPtrLo  = new_listPtr; // point to the new lo key
			new_listPtr += 6;
			memcpy(new_listPtr, minPtrHi, 6);
			m_listPtrHi  = new_listPtr; // point to the new hi key
			new_listPtr += 6;
			*pp = *pp&~0x06; //turn off all compression bits
		}

		// . if it is truncated then we just skip it
		// . it may have set oldList* stuff above, but that should not matter
		// . TODO: BUT! if endKey has same termid as currently truncated key
		//   then we should bail out now and boost the endKey to the max for
		//   this termid (the we can fix Msg5::needsRecall() )
		// . TODO: what if last key we were able to add was NEGATIVE???

skip:
		// advance winning src list ptr
		if      ( ptrs[mini][0] & 0x04 ) ptrs [ mini ] += 6;
		else if ( ptrs[mini][0] & 0x02 ) ptrs [ mini ] += 12;
		else                             ptrs [ mini ] += 18;

		// if the src list that we advanced is not exhausted, then continue
		if (ptrs[mini] < ends[mini]) {
			// is new key 6 bytes? then do not touch hi/lo ptrs
			if ( ptrs[mini][0] & 0x04 ) {
				// no-op
				logTrace(g_conf.m_logTraceRdbList, "mini=%" PRId32" new 6-byte key", mini);
			} else if ( ptrs[mini][0] & 0x02 ) {
				// is new key 12 bytes?
				logTrace(g_conf.m_logTraceRdbList, "mini=%" PRId32" new 12-byte key", mini);
				memcpy(loKeys[mini], ptrs[mini] +  6, 6);
			} else {
				// is new key 18 bytes? full key.
				logTrace(g_conf.m_logTraceRdbList, "mini=%" PRId32" new 18-byte key", mini);
				memcpy(hiKeys[mini], ptrs[mini] + 12, 6);
				memcpy(loKeys[mini], ptrs[mini] +  6, 6);
			}
		} else {
			//
			// REMOVE THE LIST at mini
			//
			logTrace(g_conf.m_logTraceRdbList, "remove list at mini=%" PRId32" numLists=%" PRId32, mini, numLists);

			// otherwise, remove him from array
			for (int32_t i = mini; i < numLists - 1; i++) {
				ptrs[i] = ptrs[i + 1];
				ends[i] = ends[i + 1];
				memcpy(hiKeys[i], hiKeys[i + 1], 6);
				memcpy(loKeys[i], loKeys[i + 1], 6);
			}

			// one less list to worry about
			numLists--;

			// only increase offset if it's not the last list we remove
			if (mini < numLists) {
				listOffset++;
			}
		}
	}

	m_listPtr = new_listPtr;

	// . if there is a negative/positive key combo
	//   they should annihilate in the primary for loop above!! UNLESS
	//   one list was truncated at the end and we did not get its
	//   annihilating key... strange, but i guess it could happen...

	// set new size and end of this merged list
	m_listSize = m_listPtr - m_list;
	m_listEnd = m_list + m_listSize;

	// return now if we're empty... all our recs annihilated?
	if (m_listSize <= 0) {
		logTrace(g_conf.m_logTraceRdbList, "END. no more list");
		return true;
	}

	// if we are tacking this merge onto a non-empty list
	// and we just had negative keys then pp could be NULL.
	// we would log "storing recs in a non-empty list" from
	// above and "pp" would be NULL.
	if (pp) {
		// the last key we stored
		char *e = m_lastKey;

		// record the last key we added in m_lastKey
		gbmemcpy (e, pp, 6);

		// take off compression bits
		*e &= 0xf9;
		e += 6;
		gbmemcpy (e, m_listPtrLo, 6);
		e += 6;
		gbmemcpy (e, m_listPtrHi, 6);

		// validate it now
		m_lastKeyIsValid = true;
	}

	if (m_listSize && !m_lastKeyIsValid) {
		log(LOG_DEBUG, "db: why last key not valid?");
	}

	// under what was requested? then done.
	if (m_listSize < minRecSizes) {
		logTrace(g_conf.m_logTraceRdbList, "===== dumping merged list =====");
		if (g_conf.m_logTraceRdbList) {
			printList();
		}
		logTrace(g_conf.m_logTraceRdbList, "END. Less than requested m_listSize=%" PRId32" minRecSizes=%" PRId32, m_listSize, minRecSizes);
		return true;
	}

	// or if no more lists
	if (numLists <= 0) {
		logTrace(g_conf.m_logTraceRdbList, "===== dumping merged list =====");
		if (g_conf.m_logTraceRdbList) {
			printList();
		}
		logTrace(g_conf.m_logTraceRdbList, "END. No more list");
		return true;
	}

	// save original end key
	char orig[MAX_KEY_BYTES];
	memcpy(orig, m_endKey, sizeof(key144_t));

	// . we only need to shrink the endKey if we fill up our list and
	//   there's still keys under m_endKey left over to merge
	// . if no keys remain to merge, then don't decrease m_endKey
	// . i don't want the endKey decreased unnecessarily because
	//   it means there's no recs up to the endKey
	memcpy ( m_endKey, m_lastKey, sizeof(key144_t) );

	// if endkey is now negative we must have a dangling negative
	// so make it positive (dangling = unmatched)
	if (KEYNEG(m_endKey)) {
		KEYINC(m_endKey, sizeof(key144_t));
	}

	// be careful not to increase original endkey, though
	if (KEYCMP(orig, m_endKey, sizeof(key144_t)) < 0) {
		KEYSET(m_endKey, orig, sizeof(key144_t));
	}

	logTrace(g_conf.m_logTraceRdbList, "===== dumping merged list =====");
	if (g_conf.m_logTraceRdbList) {
		printList();
	}

	logTrace(g_conf.m_logTraceRdbList, "END. Done");
	return true;
}

void RdbList::setFromPtr(char *p, int32_t psize, rdbid_t rdbId) {

	// free and NULLify any old m_list we had to make room for our new list
	freeList();

	// set this first since others depend on it
	m_ks = getKeySizeFromRdbId ( rdbId );

	// set our list parms
	m_list          = p;
	m_listSize      = psize;
	m_alloc         = p;
	m_allocSize     = psize;
	m_listEnd       = m_list + m_listSize;

	KEYMIN(m_startKey,m_ks);
	KEYMAX(m_endKey  ,m_ks);

	m_fixedDataSize = getDataSizeFromRdbId ( rdbId );

	m_ownData       = false;//ownData;
	m_useHalfKeys   = false;//useHalfKeys;

	// use this call now to set m_listPtr and m_listPtrHi based on m_list
	resetListPtr();

}
