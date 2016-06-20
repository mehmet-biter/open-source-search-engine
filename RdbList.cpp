#include "gb-include.h"

#include "RdbList.h"
#include "Mem.h"      // for g_mem.malloc()
#include "Rdb.h"
#include "Tagdb.h"
#include "Titledb.h"
#include "Spider.h"
#include "BitOperations.h"
#include "Process.h"


//#define _MERGEDEBUG_


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


static bool cmp_6bytes_equal(const void *p1, const void *p2)
{
	uint32_t u32_1 = *(const uint32_t*)p1;
	uint32_t u32_2 = *(const uint32_t*)p2;
	if(u32_1!=u32_2) return false;
	uint16_t u16_1 = *(const uint16_t*)(((const char*)p1)+4);
	uint16_t u16_2 = *(const uint16_t*)(((const char*)p2)+4);
	return u16_1==u16_2;
}


void RdbList::constructor () {
	m_list        = NULL;
	m_alloc       = NULL;
	m_allocSize   = 0;
	m_useHalfKeys = false;
	m_ownData     = false;
	reset();
}

RdbList::RdbList () {
	m_list        = NULL;
	m_alloc       = NULL;
	m_allocSize   = 0;
	m_useHalfKeys = false;
	m_ownData     = false;
	reset();
}

// free m_list on destruction
RdbList::~RdbList () {
	freeList();
}

void RdbList::destructor() {
	freeList();
}

void RdbList::freeList () {
	if ( m_ownData && m_alloc ) mfree ( m_alloc , m_allocSize ,"RdbList");
	m_list      = NULL;
	m_alloc     = NULL;
	m_allocSize = 0;
	reset();
}

void RdbList::resetListPtr () {
	m_listPtr = m_list;
	m_listPtrHi = NULL;
	m_listPtrLo = NULL;
	// this is used if m_useHalfKeys is true
	//if   ( m_list && m_listSize >= 12 ) m_listPtrHi = m_list + 6;
	if   ( m_list && m_listSize >= m_ks ) {
		m_listPtrHi = m_list + (m_ks-6);
		m_listPtrLo = m_list + (m_ks-12);
	}
}

// . this now just resets the size to 0, does not do any freeing
// . free will only happen on list destruction
void RdbList::reset ( ) {
	// . if we don't own our data then, NULLify it
	// . if we do own the data, don't free it
	if ( ! m_ownData ) { m_alloc = NULL; m_allocSize = 0; }
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
void RdbList::set ( char  *list          ,
		    int32_t   listSize      ,
		    char  *alloc         ,
		    int32_t   allocSize     ,
		    //key_t  startKey      ,
		    //key_t  endKey        ,
		    const char  *startKey      ,
		    const char  *endKey        ,
		    int32_t   fixedDataSize ,
		    bool   ownData       ,
		    bool   useHalfKeys   ,
		    char   keySize       ) {
	// free and NULLify any old m_list we had to make room for our new list
	freeList();
	// set this first since others depend on it
	m_ks = keySize;
	// sanity check (happens when IndexReadInfo exhausts a list to Msg2)
	//if ( startKey > endKey )
	if ( KEYCMP(startKey,endKey,m_ks) > 0 )
		log(LOG_REMIND,"db: rdblist: set: startKey > endKey.");
	// safety check
	if ( fixedDataSize != 0 && useHalfKeys ) {
		log(LOG_LOGIC,"db: rdblist: set: useHalfKeys 1 when "
		    "fixedDataSize not 0.");
		useHalfKeys = false;
	}
	// got an extremely ugly corrupt stack core without this check
	if ( m_list && m_listSize == 0 ){
		log ( LOG_WARN, "rdblist: listSize of 0 but list pointer not "
		      "NULL!" );
		m_list = NULL;
	}
	// set our list parms
	m_list          = list;
	m_listSize      = listSize;
	m_alloc         = alloc;
	m_allocSize     = allocSize;
	m_listEnd       = list + listSize;
	//m_startKey      = startKey;
	//m_endKey        = endKey;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey  ,endKey  ,m_ks);
	m_fixedDataSize = fixedDataSize;
	m_ownData       = ownData;
	m_useHalfKeys   = useHalfKeys;
	// use this call now to set m_listPtr and m_listPtrHi based on m_list
	resetListPtr();
}

// like above but uses 0/maxKey for startKey/endKey
void RdbList::set (char *list          ,
		   int32_t  listSize      ,
		   char *alloc         ,
		   int32_t  allocSize     ,
		   int32_t  fixedDataSize ,
		   bool  ownData       ,
		   bool  useHalfKeys   ,
		   char  keySize       ) {
	//key_t startKey = 0;
	//key_t endKey ; endKey.setMax();
	const char *startKey = KEYMIN();
	const char *endKey   = KEYMAX();
	set ( list          ,
	      listSize      ,
	      alloc         ,
	      allocSize     ,
	      //startKey      ,
	      //endKey        ,
	      startKey      ,
	      endKey        ,
	      fixedDataSize ,
	      ownData       ,
	      useHalfKeys   ,
	      keySize       );
}

// just set the start and end keys
//void RdbList::set ( key_t startKey , key_t endKey ) {
void RdbList::set ( const char *startKey, const char *endKey ) {
	//m_startKey = startKey;
	//m_endKey   = endKey;
	KEYSET ( m_startKey , startKey , m_ks );
	KEYSET ( m_endKey   , endKey   , m_ks );
}

//key_t RdbList::getLastKey  ( ) {
char *RdbList::getLastKey  ( ) {
	if ( ! m_lastKeyIsValid ) {
		log("db: rdblist: getLastKey: m_lastKey not valid.");
		g_process.shutdownAbort(true);
	}
	return m_lastKey;
};

//void RdbList::setLastKey  ( key_t k ) {
void RdbList::setLastKey  ( const char *k ) {
	//m_lastKey = k;
	KEYSET ( m_lastKey , k , m_ks );
	m_lastKeyIsValid = true;
}

// this has to scan through each record for variable sized records and
// if m_useHalfKeys is true
int32_t RdbList::getNumRecs ( ) {
	// we only keep this count for lists of variable sized records
	if ( m_fixedDataSize == 0 && ! m_useHalfKeys )
	//	return m_listSize / ( sizeof(key_t) + m_fixedDataSize );
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
//bool RdbList::addRecord ( key_t &key , int32_t dataSize , char *data ,
bool RdbList::addRecord ( const char *key, int32_t dataSize, const char *data,
			  bool bitch ) {

	if ( m_ks == 18 ) { // m_rdbId == RDB_POSDB ) {
		// sanity
		if ( key[0] & 0x06 ) {
			log("rdblist: posdb: cannot add bad key. please "
			    "delete posdb-buckets-saved.dat and restart.");
			// return true so rdbbuckets::getlist doesn't stop
			//return true;
			g_process.shutdownAbort(true);
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
		g_process.shutdownAbort(true);
//		exit(-1);
	}
	// get total size of the record
	//int32_t recSize = sizeof(key_t) + dataSize;
	int32_t recSize = m_ks + dataSize;
	// sanity
	if ( dataSize && KEYNEG(key) ) { g_process.shutdownAbort(true); }
	// . include the 4 bytes to store the dataSize if it's not fixed
	// . negative keys never have a datasize field now
	if ( m_fixedDataSize < 0 && !KEYNEG(key) ) recSize += 4;
	// grow the list if we need to
	if ( m_listEnd + recSize >  m_alloc + m_allocSize )
		if ( ! growList ( m_allocSize + recSize ) )
			return false;// log("RdbList::merge: growList failed");

	// sanity check
	//if ( m_listEnd != m_list+m_listSize ) { g_process.shutdownAbort(true); }
	// . special case for half keys
	// . if high 6 bytes are the same as last key,
	//   then just store low 6 bytes
	if ( m_useHalfKeys &&
	     m_listPtrHi   &&
	     //memcmp ( m_listPtrHi, ((char *)&key)+6, 6 ) == 0 ) {
	     cmp_6bytes_equal ( m_listPtrHi, key+(m_ks-6) ) ) {
		// store low 6 bytes of key into m_list
		//*(int32_t *)&m_list[m_listSize] = *(int32_t *)&key;
		//*(int16_t *)(&m_list[m_listSize+4]) =
		//	*(int16_t *)&(((char *)&key)[4]);
		//KEYSET(&m_list[m_listSize],key,m_ks-6);
		memcpy(m_listEnd,key,m_ks-6);
		// turn on half bit
		//m_list[m_listSize] |= 0x02;
		*m_listEnd |= 0x02;
		// grow list
		//m_listSize += 6;
		//m_listEnd  += 6;
		m_listSize += (m_ks - 6);
		m_listEnd  += (m_ks - 6);
		return true;
	}
	// store the key at the end of the list
	//*(key_t *)(&m_list[m_listSize]) = key;
	KEYSET ( &m_list[m_listSize], key, m_ks );
	// update the ptr
	if ( m_useHalfKeys ) {
		// we're the new hi key
		//m_listPtrHi = (m_list + m_listSize + 6);
		m_listPtrHi = (m_list + m_listSize + (m_ks - 6));
		// turn off half bit
		m_list[m_listSize] &= 0xfd;
	}
	//m_listSize += sizeof(key_t);
	//m_listEnd  += sizeof(key_t);
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
bool RdbList::prepareForMerge ( RdbList **lists         ,
				int32_t      numLists      ,
				int32_t      minRecSizes   ) {
	// return false if we don't own the data
	if ( ! m_ownData ) {
		log("db: rdblist: prepareForMerge: Data not owned.");
		g_process.shutdownAbort(true);
//		exit(-1);
	}
	// . reset ourselves
	// . sets m_listSize to 0 and m_ownData to true
	// . does not free m_list, however
	// . NO! we want to keep what we got and add records on back
	//reset();
	// do nothing if no lists passed in
	if ( numLists <= 0 ) return true;
	// . we inherit our dataSize/dedup from who we're merging
	// . TODO: all lists may not be the same fixedDataSize
	m_fixedDataSize = lists[0]->m_fixedDataSize;
	// assume we use half keys
	m_useHalfKeys = lists[0]->m_useHalfKeys;
	// inherit key size
	m_ks = lists[0]->m_ks;

	// minRecSizes is only a good size-constraining parameter if
	// we know the max rec size, cuz we could overshoot list
	// by a rec of size 1 meg!! quite a bit! then we would have to
	// call growList() in the merge_r() routine... that won't work since
	// we'd be in a thread.
	if ( m_fixedDataSize >= 0 && minRecSizes > 0 ) {
		//int32_t newmin = minRecSizes + sizeof(key_t) + m_fixedDataSize;
		int32_t newmin = minRecSizes + m_ks + m_fixedDataSize;
		// we have to grow another 12 cuz we set "first" in
		// indexMerge_r() to false and try to add another rec to see
		// if there was an annihilation
		//newmin += sizeof(key_t);
		newmin += m_ks;
		// watch out for wrap around
		if ( newmin < minRecSizes ) newmin = 0x7fffffff;
		minRecSizes = newmin;
	}
	else if ( m_fixedDataSize <  0 ) minRecSizes = -1;

	// . temporarily set m_listPtr/m_listEnd of each list based on
	//   the contraints: startKey/endKey
	// . compute our max list size from all these ranges
	int32_t maxListSize = 0;
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// each list should be constrained already
		maxListSize += lists[i]->getListSize();
		// ensure same dataSize type for each list
		if (lists[i]->getFixedDataSize() == m_fixedDataSize) continue;
		// bitch if not
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"db: rdblist: prepareForMerge: Non-uniform "
		    "fixedDataSize. %" PRId32" != %" PRId32".",
		    lists[i]->getFixedDataSize(), m_fixedDataSize );
		return false;
	}
	// . set the # of bytes we need to merge at minimum
	// . include our current list size, too
	// . our current list MUST NOT intersect w/ these lists
	m_mergeMinListSize = maxListSize + m_listSize ;
	if ( minRecSizes >= 0 && m_mergeMinListSize > minRecSizes )
		m_mergeMinListSize = minRecSizes;
	// . now alloc space for merging these lists
	// . won't shrink our m_list buffer, might grow it a bit if necessary
	// . this should keep m_listPtr and m_listPtrHi in order, too
	// . grow like 12 bytes extra since posdb might compress off 12
	//   bytes in merge_r code.
	int32_t grow = m_mergeMinListSize;

	//if ( m_ks == 18 ) grow += 12;
	// tack on a bit because rdbs that use compression like clusterdb,
	// posdb, etc. in the merge_r() code check for buffer break and
	// they use a full key size! so add that on here! otherwise, they
	// exit before getting the full mintomerge and come up short
	grow += m_ks;

	if ( growList ( grow ) ) return true;
	// otherwise, bitch about error
	return false; // log("RdbList::merge: growList failed");
}

// . get the current records key
// . this needs to be fast!!
//key_t RdbList::getKey ( char *rec ) {
void RdbList::getKey ( const char *rec , char *key ) const {

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

	//if ( ! m_useHalfKeys ) return *(key_t *)rec;
	if ( ! m_useHalfKeys || ! isHalfBitOn ( rec ) ) {
		KEYSET(key,rec,m_ks);
		return;
	}
	// seems like we don't have to be aligned to do this!
	//if ( ! isHalfBitOn ( rec ) ) return *(key_t *)rec;
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
	//key_t key ;
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
	if ( m_ks != 12 ) { g_process.shutdownAbort(true); }
	// set top most 4 bytes from hi key
	//*(int32_t  *)(&((char *)&key)[8]) = *(int32_t  *)&m_listPtrHi[2];
	// next 2 bytes from hi key
	//*(int16_t *)(&((char *)&key)[6]) = *(int16_t *)m_listPtrHi;
	// next 4 bytes from rec
	//*(int32_t  *)(&((char *)&key)[2]) = *(int32_t  *)&rec    [2];
	// last 2 bytes from rec
	//*(int16_t *)(&((char *)&key)[0]) = *(int16_t *) rec;
	// turn half bit off since this is the full 12 bytes
	//*(char *)(&key) &= 0xfd;
	//return key;
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
	if ( (rec[0] & 0x01) == 0 ) return 0;
	if ( m_fixedDataSize >= 0 ) return m_fixedDataSize;
	//return *(int32_t  *)(rec+sizeof(key_t));
	return *(int32_t  *)(rec+m_ks);
}

char *RdbList::getData ( char *rec ) {
	if ( m_fixedDataSize == 0 ) return NULL;
	//if ( m_fixedDataSize  > 0 ) return rec + sizeof(key_t) ;
	//return rec + sizeof(key_t) + 4;
	if ( m_fixedDataSize  > 0 ) return rec + m_ks;
	// negative key? then no data
	if ( (rec[0] & 0x01) == 0 ) return NULL;
	return rec + m_ks + 4;
}


// returns false on error and set g_errno
bool RdbList::growList ( int32_t newSize ) {
	// return false if we don't own the data
	if ( ! m_ownData ) {
		log(LOG_LOGIC,"db: rdblist: growlist: Data not owned.");
		g_process.shutdownAbort(true);
//		exit(-1);
	}
	// sanity check
	if ( newSize <  0 ) {
		log(LOG_LOGIC,"db: rdblist: growlist: Size is negative.");
		g_process.shutdownAbort(true);
//		exit(-1);
	}
	// don't shrink list
	if ( newSize <= m_allocSize ) return true;
	// debug msg
	// log("RdbList::growList 0x%" PTRFMT "from %" PRId32" to %" PRId32,
	//     (PTRTYPE)this,m_allocSize , newSize );
	// make a new buffer
	char *tmp =(char *) mrealloc ( m_alloc,m_allocSize,newSize,"RdbList");

	// debug msg
	//log("tmp=%" PRIx32, (int32_t)tmp);
	// debug msg
	//if ( newSize > 2500000 /*about 2.5megs*/ ) {
	//	log("BIG LIST SIZE");
	//	sleep(50000);
	//}
	// return false and g_errno should be set to ENOMEM
	// do not log down this low, log higher up -- out of memory
	//return log("RdbList::growList: couldn't realloc from %" PRId32" "
	//	   "to %" PRId32, m_allocSize , newSize );
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
bool RdbList::checkList_r ( bool removeNegRecs , bool sleepOnProblem ,
			    char rdbId ) {

	// bail if empty
	if ( m_listSize <= 0 || ! m_list ) return true;

	// ensure m_listSize jives with m_listEnd
	if ( m_listEnd - m_list != m_listSize ) {
		log("db: Data end does not correspond to data size.");
		if ( sleepOnProblem ) { g_process.shutdownAbort(true);}
		if ( sleepOnProblem ) sleep(50000);
		return false;
	}
	// . watch out for positive fixed size lists
	// . crap negative keys will not have data! so you can't do
	//   this check really!!!
	if ( removeNegRecs &&
	     m_fixedDataSize > 0 &&
	     ( m_listSize % (m_fixedDataSize+m_ks))!=0){
		log("db: Odd data size. Corrupted data file.");
		if ( sleepOnProblem ) {g_process.shutdownAbort(true); }
		if ( sleepOnProblem ) sleep(50000);
		return false;
	}

	//log("m_list=%" PRId32,(int32_t)m_list);
	//key_t oldk;
	//oldk.n0 = 0 ; oldk.n1 = 0;
	char oldk[MAX_KEY_BYTES];
	KEYSET(oldk,KEYMIN(),m_ks);
	// point to start of list
	resetListPtr();
	// we can accept keys == endKey + 1 because we may have dup keys
	// which cause Msg3.cpp:setEndPages() to hiccup, cuz it subtracts
	// one from the start key of a page... blah blah
	//key_t acceptable ;
	//acceptable.n1 = m_endKey.n1 ;
	//acceptable.n0 = m_endKey.n0 ;
	//acceptable += (uint32_t) 1;
	char acceptable[MAX_KEY_BYTES];
	KEYSET ( acceptable , m_endKey , m_ks );
	KEYADD ( acceptable , m_ks );
	// watch out for wrap around...
	//if ( acceptable.n0 == 0 && acceptable.n1 == 0 ) {
	//	acceptable.n1 = m_endKey.n1 ;
	//	acceptable.n0 = m_endKey.n0 ;
	if ( KEYCMP(acceptable,KEYMIN(),m_ks)==0 )
		KEYSET ( acceptable , m_endKey , m_ks );
	char k[MAX_KEY_BYTES];

	static int32_t th = 0;
	if ( ! th ) th = hash64Lower_a ( "roottitles" , 10 );

	while ( ! isExhausted() ) {
		//key_t k = getCurrentKey();
		getCurrentKey( k );
		// if titleRec, check size
		if ( rdbId == RDB_TITLEDB && ! KEYNEG(k) ) {
			int32_t dataSize = getCurrentDataSize();
			char *data = NULL;
			if ( dataSize >= 4 ) data = getCurrentData();
			if ( data &&
			     (*(int32_t *)data < 0 ||
			      *(int32_t *)data > 100000000 ) ) {
				g_process.shutdownAbort(true); }
		}
		// tagrec?
		if ( rdbId == RDB_TAGDB && ! KEYNEG(k) ) {
			//TagRec *gr = (TagRec *)getCurrentRec();
			//Tag *tag = gr->getFirstTag   ( );
			//for ( ; tag ; tag = gr->getNextTag ( tag ) ) {
			Tag *tag = (Tag *)getCurrentRec();
			if ( tag->m_type == th ) {
				char *tdata = tag->getTagData();
				int32_t tsize = tag->getTagDataSize();
				// core if tag val is not \0 terminated
				if ( tsize > 0 && tdata[tsize-1]!='\0' ) {
					log("db: bad root title tag");
					g_process.shutdownAbort(true); }
			}
		}
		if ( rdbId == RDB_SPIDERDB && ! KEYNEG(k) &&
		     getCurrentDataSize() > 0 ) {
			//char *data = getCurrentData();
			char *rec = getCurrentRec();
			// bad url in spider request?
			if ( g_spiderdb.isSpiderRequest ( (key128_t *)rec ) ){
				SpiderRequest *sr = (SpiderRequest *)rec;
				if ( sr->isCorrupt() ) {
					log("db: spider req corrupt");
					g_process.shutdownAbort(true);
				}
			}
		}
		// title bad uncompress size?
		if ( rdbId == RDB_TITLEDB && ! KEYNEG(k) ) {
			char *rec = getCurrentRec();
			int32_t usize = *(int32_t *)(rec+12+4);
			if ( usize <= 0 || usize>100000000 ) {
				log("db: bad titlerec uncompress size");
				g_process.shutdownAbort(true);
			}
		}
		// debug msg
		// pause if it's google
		//if ((((k.n0)  >> 1) & 0x0000003fffffffffLL)  == 70166155664)
		//	log("hey you!");
		//int32_t dataSize = getCurrentDataSize();
		//if ( m_ks >= 18 ) // include linkdb and posdb now
		//	log("db: key=%s",KEYSTR((unsigned char *)k,m_ks));
		// special checks for debugging linkdb bug
		//if ( m_ks == 24 ) {
		//	unsigned char hc;
		//	hc = g_linkdb.getLinkerHopCount_uk((key192_t *)k);
		//	if ( hc ) { g_process.shutdownAbort(true); }
		//}
		//log("key.n1=%" PRId32" key.n0=%" PRId64" dsize=%" PRId32,
		//	k.n1,k.n0,dataSize);
		//if ( k <  oldk      ) {
		//if ( k < m_startKey ) {
		if ( KEYCMP(k,m_startKey,m_ks)<0 ) {
			log("db: Key before start key in list of records.");
			log("db: sk=%s",KEYSTR(m_startKey,m_ks));
			log("db: k2=%s",KEYSTR(k,m_ks));
			if ( sleepOnProblem ) {g_process.shutdownAbort(true); }
			if ( sleepOnProblem ) sleep(50000);
			return false;
		}
		if ( KEYCMP(k,oldk,m_ks)<0 ) {
			log(
			    "db: Key out of order in list of records.");
			log("db: k1=%s",KEYSTR(oldk,m_ks));
			log("db: k2=%s",KEYSTR(k,m_ks));
			//log("db: k1.n1=%" PRIx64" k1.n0=%" PRIx64,
			//    KEY1(oldk,m_ks),KEY0(oldk));
			//log("db:k2.n1=%" PRIx64" k2.n0=%" PRIx64,KEY1(k,m_ks),KEY0(k));
			//g_process.shutdownAbort(true);
			//if ( sleepOnProblem ) {g_process.shutdownAbort(true); }
			//if ( sleepOnProblem ) sleep(50000);
			return false;
		}
		//if ( k > acceptable ) {
		if ( KEYCMP(k,acceptable,m_ks)>0 ) {
			log("db: Key after end key in list of records.");
			//log("db: k.n1=%" PRIx32" k.n0=%" PRIx64,k.n1,k.n0);
			log("db: k2=%s",KEYSTR(k,m_ks));
			log("db: ak=%s",KEYSTR(acceptable,m_ks));
			//log("db:e.n1=%" PRIx32" e.n0=%" PRIx64,m_endKey.n1,m_endKey.n0);
			log("db: ek=%s",KEYSTR(m_endKey,m_ks));
			if ( sleepOnProblem ) {g_process.shutdownAbort(true); }
			if ( sleepOnProblem ) sleep(50000);
			return false;
		}
		// check for delete keys
		//if ( (k.n0 & 0x01LL) == 0LL ) {
		if ( KEYNEG(k) ) {
			if ( removeNegRecs ) {
				log("db: Got unmet negative key.");
				if ( sleepOnProblem ) {g_process.shutdownAbort(true);}
				if ( sleepOnProblem ) sleep(50000);
				return false;
			}
			// ensure delete keys have no dataSize
			if ( m_fixedDataSize == -1 &&
			     getCurrentDataSize() != 0 ) {
				log("db: Got negative key with "
				    "positive dataSize.");
				// what's causing this???
				g_process.shutdownAbort(true);
				if ( sleepOnProblem ) {g_process.shutdownAbort(true);}
				if ( sleepOnProblem ) sleep(50000);
				return false;
			}
		}
		//oldk = k;
		KEYSET ( oldk , k , m_ks );
		// save old guy
		char *saved = m_listPtr;
		// test this
		//int32_t recSize = getCurrentRecSize();
		//log("db: recsize=%" PRId32,recSize);
		// advance to next guy
		skipCurrentRecord();
		// test this - no, might be end of list!
		//recSize = getCurrentRecSize();
		//log("db: recsize2=%" PRId32,recSize);
		// sometimes dataSize is too big in corrupt lists
		if ( m_listPtr > m_listEnd ) {
			log(
			    "db: Got record with bad data size field. "
			    "Corrupted data file.");
			if ( sleepOnProblem ) {g_process.shutdownAbort(true);}
			if ( sleepOnProblem ) sleep(50000);
			return false;
		}
		// don't go backwards, and make sure to go forwards at
		// least 6 bytes, the min size of a key (half key)
		if ( m_listPtr < saved + 6 ) {
			log(
			    "db: Got record with bad data size field. "
			    "Corrupted data file.");
			if ( sleepOnProblem ) {g_process.shutdownAbort(true);}
			if ( sleepOnProblem ) sleep(50000);
			return false;
		}
	}
	// . check last key
	// . oldk ALWAYS has the half bit clear, so clear it on lastKey
	// . this isn't so much a check for corruption as it is a check
	//   to see if the routines that set the m_lastKey were correct
	//if ( m_lastKeyIsValid && oldk != m_lastKey ) {
	if ( m_lastKeyIsValid && KEYCMP(oldk,m_lastKey,m_ks) != 0 ) {
		log(LOG_LOGIC,
		    "db: rdbList: checkList_r: Got bad last key.");
		log(LOG_LOGIC,
		    //"db: rdbList: checkList_r: k.n1=%" PRIx32" k.n0=%" PRIx64,
		    //oldk.n1,oldk.n0);
		    "db: rdbList: checkList_r: key=%s",
		    KEYSTR(oldk,m_ks));
		log(LOG_LOGIC,
		    //"db: rdbList: checkList_r: l.n1=%" PRIx32" l.n0=%" PRIx64,
		    //m_lastKey.n1,m_lastKey.n0);
		    "db: rdbList: checkList_r: key=%s",
		    KEYSTR(m_lastKey,m_ks) );
		if ( sleepOnProblem ) {g_process.shutdownAbort(true);}
		if ( sleepOnProblem ) sleep(50000);
		// fix it
		//m_lastKey = oldk;
		KEYSET(m_lastKey,oldk,m_ks);
	}
	// . otherwise, last key is now valid
	// . this is only good for the call to Msg5::getRemoteList()
	if ( ! m_lastKeyIsValid ) {
		//m_lastKey = oldk;
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
	//key_t oldk;
	char  oldk[MAX_KEY_BYTES];
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
		//key_t k = getCurrentKey();
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
		//if ( k < oldk && oldRecSize ) {
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
		//oldk       = k;
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

	// sanity. assume posdb???
	//if ( m_ks == 18 ) {
	//	if ( ! checkList_r ( false,false,RDB_POSDB) )
	//		log("rdblist: something wrong with repaired list");
	//}

	// all is ok
	return true;
}


int RdbList::printPosdbList ( int32_t logtype ) {

	log(logtype, "%s:%s: BEGIN",__FILE__,__func__);

	//log("m_list=%" PRId32,(int32_t)m_list);
	// save
	char *oldp   = m_listPtr;
	const char *oldphi = m_listPtrHi;
	resetListPtr();
	log(logtype, "db: STARTKEY=%s, m_ks=%d, datasize=%" PRId32,KEYSTR(m_startKey,m_ks), (int)m_ks, m_listSize);

	size_t key_size;
	// 48bit 38bit 4bit 4bit 18bit
	log(logtype,"db:   ........term_id ......doc_id rank lang wordpos del ");


	while ( ! isExhausted() ) {
		//key_t k = getCurrentKey();
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
		uint64_t alignment_bit0 = 0;
		uint64_t lang_bit6 = 0;

		if( key_size == 18 )
		{
			term_id				= extract_bits(key,96,144);
		}

		if( key_size >= 12 )
		{
	        doc_id				= extract_bits(key,58,96);
	        alignment_bit0		= extract_bits(key,57,58);
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
				log(logtype,"db:   %15" PRId64" %12" PRId64" %4" PRId64" %4" PRId64" %7" PRId64" %3s", term_id, doc_id, site_rank, lang_id, word_pos, !nodelete_marker?"Y":"N");
				break;
			case 12:
				log(logtype,"db:   %15s %12" PRId64" %4" PRId64" %4" PRId64" %7" PRId64" %3s", "-", doc_id, site_rank, lang_id, word_pos, !nodelete_marker?"Y":"N");
				break;
			default:
				log(logtype,"db:   %15s %12s %4s %4s %7" PRId64" %3s", "-", "-", "-", "-", word_pos, !nodelete_marker?"Y":"N");
				break;
		}

		skipCurrentRecord();
	}

	if ( m_lastKeyIsValid )
		log(logtype,  "db: LASTKEY=%s", KEYSTR(m_lastKey,m_ks));

	log(logtype, "db: ENDKEY=%s",KEYSTR(m_endKey,m_ks));

	//resetListPtr();
	m_listPtr   = oldp;
	m_listPtrHi = oldphi;

	log(logtype, "%s:%s: END",__FILE__,__func__);
	return 0;
}




int RdbList::printList ( int32_t logtype ) {

	if ( m_ks == 18 ) { // m_rdbId == RDB_POSDB ) {
		return printPosdbList(logtype);
	}

	log(logtype, "%s:%s: BEGIN",__FILE__,__func__);

	//log("m_list=%" PRId32,(int32_t)m_list);
	// save
	char *oldp   = m_listPtr;
	const char *oldphi = m_listPtrHi;
	resetListPtr();
	log(logtype, "db: STARTKEY=%s",KEYSTR(m_startKey,m_ks));

	while ( ! isExhausted() ) {
		//key_t k = getCurrentKey();
		char k[MAX_KEY_BYTES];
		getCurrentKey(k);
		int32_t dataSize = getCurrentDataSize();

		const char *d;
		if ( (*m_listPtr & 0x01) == 0x00 )
		{
			d = " (del)";
		}
		else
		{
			d = "";
		}

		log(logtype, "db: k=%s dsize=%07" PRId32"%s", KEYSTR(k,m_ks),dataSize,d);
		skipCurrentRecord();
	}

	if ( m_lastKeyIsValid )
		log(logtype,  "db: LASTKEY=%s", KEYSTR(m_lastKey,m_ks));

	log(logtype, "db: ENDKEY=%s",KEYSTR(m_endKey,m_ks));
	//resetListPtr();
	m_listPtr   = oldp;
	m_listPtrHi = oldphi;

	log(logtype, "%s:%s: END",__FILE__,__func__);
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
bool RdbList::constrain ( const char   *startKey    ,
			  char   *endKey      ,
			  int32_t    minRecSizes ,
			  int32_t    hintOffset  ,
			  const char   *hintKey     ,
			  const char   *filename    ,
			  int32_t    niceness    ) {
	// return false if we don't own the data
	if ( ! m_ownData ) {
		g_errno = EBADLIST;
		return log("db: constrain: Data not owned.");
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
		return log("db: First key is 6 bytes. Corrupt data "
			   "file.");
	}

	// sanity. hint key should be full key
	if ( m_ks == 18 && hintKey && (hintKey[0]&0x06)){
		g_errno = ECORRUPTDATA;
		return log("db: Hint key is corrupt.");
		//g_process.shutdownAbort(true);}
	}

	if ( hintOffset > m_listSize ) { //g_process.shutdownAbort(true); }
		g_errno = ECORRUPTDATA;
		return log("db: Hint offset %" PRId32" > %" PRId32" is corrupt."
			   ,hintOffset,
			   m_listSize);
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
	//while ( p < m_listEnd && getKey(p) < startKey ) {
	while ( p < m_listEnd ) {
		QUICKPOLL(niceness);
		getKey(p,k);
#ifdef GBSANITYCHECK
		// check key order!
		if ( KEYCMP(k,lastKey,m_ks)<= 0 ) {
			log("constrain: key=%s out of order",
			    KEYSTR(k,m_ks));
			g_process.shutdownAbort(true);
		}
		KEYSET(lastKey,k,m_ks);
#endif
		// stop if we are >= startKey
		if ( KEYCMP(k,startKey,m_ks) >= 0 ) break;
#ifdef GBSANITYCHECK
		// debug msg
		log("constrain: skipping key=%s rs=%" PRId32,
		    KEYSTR(k,m_ks),getRecSize(p));
#endif
		// . since we don't call skipCurrentRec() we must update
		//  m_listPtrHi ourselves
		// . this is fruitless if m_useHalfKeys is false...
		//if ( ! isHalfBitOn ( p ) ) m_listPtrHi = p + 6;
		if ( ! isHalfBitOn ( p ) ) m_listPtrHi = p + (m_ks-6);
		// posdb uses two compression bits
		if ( m_ks == 18 && !(p[0]&0x04)) m_listPtrLo = p + (m_ks-12);
		// get size of this rec, this can be negative if corrupt!
		int32_t recSize = getRecSize ( p );
		// watch out for corruption, let Msg5 fix it
		if ( recSize < 0 ) {
			m_listPtrHi = savelistPtrHi ;
			m_listPtrLo = savelistPtrLo ;
			g_errno = ECORRUPTDATA;
			return log("db: Got record size of %" PRId32" < 0. "
				   "Corrupt data file.",recSize);
		}
		p += recSize;
	}

	// . if p is exhausted list is empty, all keys were under startkey
	// . if p is already over endKey, we had no keys in [startKey,endKey]
	// . I don't think this call is good if p >= listEnd, it would go out
	//   of bounds
	//   corrupt data could send it well beyond listEnd too.
	if ( p < m_listEnd )
		getKey(p,k);

	//if ( p >= m_listEnd || getKey(p) > endKey ) {
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
		if ( p[0] & 0x04 ) p -= 12;
		else               p -= 6;
		// write the full key back into "p"
		KEYSET(p,k,m_ks);
	}
	// . if p points to a 6 byte key, make it 12 bytes
	// . this is the only destructive part of this function
	else if ( m_useHalfKeys && isHalfBitOn ( p ) ) {
		// the key returned should have half bit cleared
		//key_t k = getKey(p);
		getKey(p,k);
		// write the key back 6 bytes
		p -= 6;
		//*(key_t *)p = k;
		KEYSET(p,k,m_ks);
	}

#ifdef GBSANITYCHECK
	log("constrain: hk=%s",KEYSTR(hintKey,m_ks));
	log("constrain: hintOff=%" PRId32,hintOffset);
#endif

	// inc m_list , m_alloc should remain where it is
	m_list = p;
	// . set p to the hint
	// . this is the last key in the map before the endkey i think
	// . saves us from having to scan the WHOLE list
	p = firstStart + hintOffset;
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
	//   with the *(key_t *)p = k above!!!! tricky...
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
	//else if ( getKey(p) != hintKey || hintKey > endKey ) {
	else if ( KEYCMP(k,hintKey,m_ks)!=0 || KEYCMP(hintKey,endKey,m_ks)>0) {
		log("db: Corrupt data or map file. Bad hint for %s.",filename);
		// . until we fix the corruption, drop a core
		// . no, a lot of files could be corrupt, just do it for merge
		//g_process.shutdownAbort(true);
		p           = m_list;
		m_listPtr   = m_list;
		//m_listPtrHi = m_list + 6;
		m_listPtrHi = m_list + (m_ks-6);
		m_listPtrLo = m_list + (m_ks-12);
	}
	// . max a max ptr based on minRecSizes
	// . if p hits or exceeds this we MUST stop
	char *maxPtr = m_list + minRecSizes;
	// watch out for wrap around!
	if ( maxPtr < m_list ) maxPtr = m_listEnd;
	// if mincRecSizes is -1... do not constrain on this
	if ( minRecSizes < 0 ) maxPtr = m_listEnd;
	// size of last rec we read in the list
	int32_t size = -1 ;
	// advance until endKey or minRecSizes kicks us out
	//while ( p < m_listEnd && getKey(p) <= endKey && p < maxPtr ) {
	while ( p < m_listEnd ) {
		QUICKPOLL(niceness);
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
			return log("db: Corrupt record size of %" PRId32" "
				   "bytes in %s.",size,filename);
		}
		// set hiKey in case m_useHalfKeys is true for this list
		//if ( size == 12 ) m_listPtrHi = p + 6 ;
		if ( size == m_ks ) m_listPtrHi = p + (m_ks-6) ;
		// posdb uses two compression bits
		if ( m_ks == 18 && !(p[0]&0x04)) m_listPtrLo = p + (m_ks-12);
		// watch out for wrap
		char *oldp = p;
		p += size;
		// if size is corrupt we can breech the whole list and cause
		// m_listSize to explode!!!
		if ( p > m_listEnd || p < oldp ) {
			m_list      = savelist;
			m_listPtrHi = savelistPtrHi;
			m_listPtrLo = savelistPtrLo;
			m_listPtr   = savelist;
			g_errno = ECORRUPTDATA;
			return log("db: Corrupt record size of %" PRId32" "
				   "bytes in %s.",size,filename);
		}
	}
	// . if minRecSizes was limiting constraint, reset m_endKey to lastKey
	// . if p equals m_listEnd it is ok, too... this happens mostly when
	//   we get the list from the tree so there is not *any* slack
	//   left over.
	//if ( p < m_listEnd && getKey(p) <= endKey && p >= maxPtr && size >0){
	if ( p < m_listEnd ) getKey(p,k);
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
			return log("db: Corrupt record size of %" PRId32" "
				   "bytes in %s.",size,filename);
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
		log("db: Encountered bad endkey in %s. listSize=%" PRId32,
		    filename,m_listSize);
		g_process.shutdownAbort(true);
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
	//m_startKey  = startKey;
	//m_endKey    = endKey;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);
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
void RdbList::merge_r ( RdbList **lists         ,
			int32_t      numLists      ,
			const char     *startKey      ,
			const char     *endKey        ,
			int32_t      minRecSizes   ,
			bool      removeNegRecs ,
			char      rdbId         ,
			int32_t      niceness      ) {
	// sanity
	if ( ! m_ownData ) {
		log("list: merge_r data not owned");
		g_process.shutdownAbort(true);
	}
	// bail if none! i saw a doledb merge do this from Msg5.cpp
	// and it was causing a core because m_MergeMinListSize was -1
	if ( numLists == 0 ) return;
	// save this
	int32_t startListSize = m_listSize;
	// did they call prepareForMerge()?
	if ( m_mergeMinListSize == -1 ) {
		log(LOG_LOGIC,"db: rdblist: merge_r: prepareForMerge() not "
			"called. ignoring error and returning emtpy list.");
		// this happens if we nuke doledb during a merge of it. it
		// is just bad timing
		return;
		// save state and dump core, sigBadHandler will catch this
		// g_process.shutdownAbort(true);
	}
	// already there?
	if ( minRecSizes >= 0 && m_listSize >= minRecSizes ) return;
	// now if we're only merging 2 data-less lists to it super fast
	//if ( m_useHalfKeys ) {
	//	log(LOG_LOGIC,"db: rdblist: merge_r: call indexMerge_r() not "
	//	    "merge_r()");
	//	g_process.shutdownAbort(true);	exit(-1);
	//}
	// warning msg
	if ( m_listPtr != m_listEnd )
		log(LOG_LOGIC,"db: rdblist: merge_r: warning. "
		    "merge not storing at end of list for %s.",
		    getDbnameFromId((uint8_t)rdbId));
	// set our key range
	//m_startKey = startKey;
	//m_endKey   = endKey;
	KEYSET(m_startKey,startKey,m_ks);
	KEYSET(m_endKey,endKey,m_ks);
	// . NEVER end in a negative rec key (dangling negative rec key)
	// . we don't want any positive recs to go un annhilated
	// . but don't worry about this check if start and end keys are equal
	//if ( m_startKey != m_endKey && (m_endKey.n0 & 0x01) == 0x00 )
	// . MDW: this happens during the qainject1() qatest in qa.cpp that
	//   deletes all the urls then does a dump of just negative keys.
	//   so let's comment it out for now
	if ( KEYCMP(m_startKey,m_endKey,m_ks)!=0 && KEYNEG(m_endKey) ) {
		// log(LOG_LOGIC,"db: rdblist: merge_r: Illegal endKey for "
		//     "merging rdb=%s. fixing.",getDbnameFromId(rdbId));
		// make it legal so it will be read first NEXT time
		KEYSUB(m_endKey,m_ks);
	}
	// do nothing if no lists passed in
	if ( numLists <= 0 ) return;
	// inherit the key size of what we merge
	m_ks = lists[0]->m_ks;
	// sanity check
	for ( int32_t i = 1 ; i < numLists ; i++ )
		if ( lists[i]->m_ks != m_ks ) {
			log("db: non conforming key size of %" PRId32" != %" PRId32" for "
			    "list #%" PRId32".",(int32_t)lists[i]->m_ks,(int32_t)m_ks,i);
			g_process.shutdownAbort(true);
		}
	// bail if nothing requested
	if ( minRecSizes == 0 ) return;

	if ( rdbId  == RDB_POSDB ) {
		posdbMerge_r ( lists ,
			       numLists ,
			       startKey ,
			       endKey ,
			       m_mergeMinListSize,
			       removeNegRecs ,
			       niceness );
		return;
	}

	int32_t required = -1;
	// . if merge not necessary, print a warning message.
	// . caller should have just called constrain() then
	if ( numLists == 1 ) {
		// we do this sometimes to remove the negative keys!!
		//log(LOG_LOGIC,"db: rdblist: merge_r: merge_r called on one "
		//    "list.");
		// this seems to nuke our list!!
		//g_process.shutdownAbort(true);
		required = m_listSize + lists[0]->m_listSize;
	}
	// otherwise, list #j has the minKey, although may not be min
	int32_t  mini ;
	int32_t  i    ;
	// . find a value for "m_lastKey" that does not exist in any of lists
	// . we increment by 2 too
	// . if minKey is a delete, then make it a non-delete key
	// . add 2 to ensure that it stays a non-delete key
	//key_t lastKey  ;
	char  lastKey[MAX_KEY_BYTES];
	bool  lastKeyIsValid = false;
	//key_t lastPosKey;
	//key_t highestKey;
	char  lastPosKey[MAX_KEY_BYTES];
	char  highestKey[MAX_KEY_BYTES];
	bool  firstTime = true;
	//char *lastNegKey = NULL;
	char  lastNegKey[MAX_KEY_BYTES];
	int32_t  lastNegi = -1;
	// init highestKey
	//highestKey.n1 = 0;
	//highestKey.n0 = 0LL;
	KEYSET(highestKey,KEYMIN(),m_ks);
	// this is used for rolling back delete records
	int32_t lastListSize = m_listSize;
	// for seeing if negative rec is OLDER than positve key before
	// annilating them together
	//int32_t lastMini = -1;
	// two vars for removing negative recs from the end of the final list
	int32_t  savedListSize = -1;
	//key_t savedLastKey;
	//key_t savedHighestKey;
	char  savedLastKey[MAX_KEY_BYTES];
	char  savedHighestKey[MAX_KEY_BYTES];
	// reset each list's ptr
	for ( i = 0 ; i < numLists ; i++ ) lists[i]->resetListPtr();
	// don't breech the list's boundary when adding keys from merge
	char *allocEnd = m_alloc + m_allocSize;
	// sanity
	//if ( ! m_alloc ) { g_process.shutdownAbort(true); }
	// now begin the merge loop
	//key_t ckey;
	//key_t mkey;
	char ckey[MAX_KEY_BYTES];
	char mkey[MAX_KEY_BYTES];
	//int64_t prevDocId = 0LL;
	char minKey[MAX_KEY_BYTES];

	int64_t tt1 = getTagTypeFromStr( "sitenuminlinksfresh");
	int64_t tt2 = getTagTypeFromStr( "sitepop");

 top:
	// get the biggest possible minKey so everyone's <= it
	//key_t minKey;
	//minKey.n0 = 0xffffffffffffffffLL;
	//minKey.n1 = 0xffffffff;
	KEYSET(minKey,KEYMAX(),m_ks);
	// assume we have no min key
	mini = -1;
	// . loop over the lists
	// . get newer rec with same key as older rec FIRST
	for ( i = 0 ; i < numLists ; i++ ) {
		// TODO: to speed up extract from list of RdbLists
		if ( lists[i]->isExhausted() ) continue;
		// see if the current key from this scan's read buffer is 2 big
		//ckey = lists[i]->getCurrentKey();
		//mkey = minKey;
		lists[i]->getCurrentKey(ckey);
		KEYSET(mkey,minKey,m_ks);
		// treat negatives and positives as equals for this
		//ckey.n0 |= 0x01;
		//mkey.n0 |= 0x01;
		*ckey |= 0x01;
		*mkey |= 0x01;
		// clear compression bits if posdb
		if ( m_ks == 18 ) *ckey &= 0xf9;
		//
		// TODO: if merging titledb recs mask out all but the docids???
		// then we don't have to worry about adding the negative
		// key in Msg14.cpp adding to RDB_TITLEDB. that was causing
		// us to add then delete the tfndb rec for the same docid
		// because of the TITLEDB/TFNDB logic in Rdb::addList/Record()
		// crap, then i would have to deal with rdbtree too! so
		// comment this out..
		//if ( rdbId == RDB_TITLEDB ) {
		//	// all but the least significant 7 bytes are docid bits
		//	// for the most part
		//	memset(ckey,7,0);
		//	memset(mkey,7,0);
		//	// these 2 bits are not docid bits
		//	ckey[7] &= 0xfc;
		//	mkey[7] &= 0xfc;
		//}
                //if ( ckey > mkey ) continue;
		if ( KEYCMP(ckey,mkey,m_ks)>0 ) continue;
		// if this guy is newer and equal, skip the old guy
		//if ( ckey == mkey && mini >= 0 )
		if ( KEYCMP(ckey,mkey,m_ks)==0 && mini >= 0 )
			lists[mini]->skipCurrentRecord();
		// now this new guy is the min key
                //minKey  = lists[i]->getCurrentKey();
		lists[i]->getCurrentKey(minKey);
                mini    = i;
	}
	// we're done if all lists are exhausted
	if ( mini == -1 ) goto done;
	// . bail if minKey out of range
	// . lists are not constrained properly anymore with the addition of
	//   tfndblist in Msg5.cpp
	//if ( minKey > endKey ) goto done;
	if ( KEYCMP(minKey,endKey,m_ks)>0 ) goto done;
	//if ( removeNegRecs && (minKey.n0 & 0x01) == 0x00 ) goto skip;
	if ( removeNegRecs && KEYNEG(minKey) ) {
		required -= m_ks;
		lastNegi   = mini;
		//lastNegKey = lists[mini]->getCurrentRec();
		lists[mini]->getCurrentKey(lastNegKey);
		goto skip;
	}
	// special filter to remove obsolete tags from tagdb
	if ( rdbId == RDB_TAGDB ) {
		Tag *tag = (Tag *)lists[mini]->getCurrentRec();
		if ( tag->m_type == tt1 || tag->m_type == tt2 ) {
			required -= tag->getRecSize();//m_ks;
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
		if ( KEYCMPNEGEQ(minKey,lastNegKey,m_ks)==0 ) goto skip;
		// otherwise, we are beyond it...
		//lastNegKey = NULL;
		lastNegi = -1;
	}
	/*
	// posdb?
	if ( m_ks == 18 ) {
		// if adding the key would breech us, goto done
		// TODO: what about compression?
		if (m_list + m_listSize + 6 >allocEnd ) goto done;
		// add it using compression bits
		addRecord ( minKey ,0,NULL,false);
	}
	// new linkedb?
	else if ( m_ks == sizeof(key224_t) ) {
		// if adding the key would breech us, goto done
		// TODO: what about compression?
		if (m_list + m_listSize + 18 >allocEnd ) goto done;
		// add it using compression bits
		addRecord ( minKey ,0,NULL,false);
	}
	*/
	// . copy the winning record into our list
	// . these increment store at m_list+m_listSize and inc m_listSize
	if ( m_fixedDataSize == 0 ) {
		// if adding the key would breech us, goto done
		//if (m_list + m_listSize + sizeof(key_t) >allocEnd) goto done;
		if (m_list + m_listSize + m_ks >allocEnd ) goto done;
		// watch out
		//int32_t foo;
		//if ( m_ks == 18 && m_listSize == 20136 )
		//	foo = 1;
		// add it using compression bits
		addRecord ( minKey ,0,NULL,false);
		// add the record to end of list
		//*(key_t *)(m_list + m_listSize) = minKey;
		//KEYSET(m_list+m_listSize,minKey,m_ks);
		//m_listSize += sizeof(key_t);
		//m_listSize += m_ks;
	}
	else {
		// if adding the key would breech us, goto done
		//int32_t recSize=sizeof(key_t)+lists[mini]->getCurrentDataSize();
		int32_t recSize=m_ks+lists[mini]->getCurrentDataSize();
		// negative keys have no datasize entry
		if (m_fixedDataSize < 0 && ! KEYNEG(minKey) ) recSize += 4;
		if (m_list + m_listSize + recSize > allocEnd) goto done;
		// . fix m_listEnd so it doesn't try to call growList() on us
		// . normally we don't set this right until we're done merging
		m_listEnd = m_list + m_listSize;
		// add the record to end of list
		addRecord ( minKey                            ,
			    lists[mini]->getCurrentDataSize() ,
			    lists[mini]->getCurrentData()     );
	}
	// if we are positive and unannhilated, store it in case
	// last key we get is negative and removeNegRecs is true we need to
	// know the last positive key to set m_lastKey
	//if ( (*(char *)&minKey & 0x01) == 0x01 ) lastPosKey = minKey;
	if ( !KEYNEG(minKey) ) KEYSET(lastPosKey,minKey,m_ks);
	//lastKey        = minKey;
	KEYSET(lastKey,minKey,m_ks);
	//lastMini       = mini;
	lastKeyIsValid = true;
 skip:
	// get the next key in line and goto top
	lists[mini]->skipCurrentRecord();
	// keep adding/merging more records if we still have more room w/o grow
	if ( m_listSize < m_mergeMinListSize ) goto top;

 done:
	// . is the last key we stored negative, a dangling negative?
	// . if not, skip this next section
	//if ( lastKeyIsValid && (*(char *)&lastKey & 0x01) == 0x01 )
	if ( lastKeyIsValid && !KEYNEG(lastKey) )
		goto positive;

	// are negatives allowed?
	if ( removeNegRecs ) {
		// . keep chugging if there MAY be keys left
		// . they will replace us if they are added cuz "removeNegRecs"
		//   is true
		//if ( mini >= 0 && minKey < endKey ) goto top;
		if ( mini >= 0 && KEYCMP(minKey,endKey,m_ks)<0 ) goto top;
		// . otherwise, all lists were exhausted
		// . peel the dangling negative off the top
		// . highestKey is irrelevant here cuz all lists are exhausted
		m_listSize = lastListSize;
		// fix this
		if ( required >= 0 ) required = lastListSize;
		//lastKey    = lastPosKey;
		KEYSET(lastKey,lastPosKey,m_ks);
	}

	// if all lists are exhausted, we're really done
	if ( mini < 0 ) goto positive;

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
		//savedLastKey    = lastKey;
		KEYSET(savedLastKey,lastKey,m_ks);
		//savedHighestKey = highestKey;
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
	//lastKey    = savedLastKey;
	KEYSET(lastKey,savedLastKey,m_ks);
	//highestKey = savedHighestKey;
	KEYSET(highestKey,savedHighestKey,m_ks);

 positive:
	// but don't set the listSize negative
	if ( m_listSize < 0 ) m_listSize = 0;

	// set these 2 things for our final merged list
	m_listEnd = m_list + m_listSize;
	m_listPtr = m_listEnd;

	// . set this for RdbMerge class i guess
	// . it may not actually be present if it was a dangling
	//   negative rec that we removed 3 lines above
	if ( m_listSize > startListSize ) { // > 0 ) {
		//m_lastKey = lastKey;
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
		//key_t endKey;
		//if ( m_lastKey < highestKey ) endKey = highestKey;
		//else                          endKey = m_lastKey;
		char endKey[MAX_KEY_BYTES];
		if ( KEYCMP(m_lastKey,highestKey,m_ks)<0 )
			KEYSET(endKey,highestKey,m_ks);
		else
			KEYSET(endKey,m_lastKey ,m_ks);
		// if endkey is now negative we must have a dangling negative
		// so make it positive (dangling = unmatched)
		//if ( (*(char *)&endKey & 0x01) == 0x00 )
		if ( KEYNEG(endKey) )
			//endKey += (uint32_t)1;
			KEYADD(endKey,m_ks);
		// be careful not to increase original endkey, though
		//if ( endKey < m_endKey ) m_endKey = endKey;
		if ( KEYCMP(endKey,m_endKey,m_ks)<0 )
			KEYSET(m_endKey,endKey,m_ks);
	}

	// . sanity check. if merging one list, make sure we get it
	// . but if minRecSizes kicked us out first, then we might have less
	//   then "required"
	if ( required >= 0 && m_listSize < required && m_listSize<minRecSizes){
		g_process.shutdownAbort(true); }

	// dedup for spiderdb
	//if ( rdbId == RDB_SPIDERDB )
	//	dedupSpiderdbList ( this , removeNegRecs );

	/*
	if ( rdbId  == RDB_POSDB ) {
		RdbList ttt;
		ttt.m_ks = 18;
		ttt.m_fixedDataSize = 0;
		KEYSET(ttt.m_startKey,m_startKey,m_ks);
		KEYSET(ttt.m_endKey,m_endKey,m_ks);
		ttt.prepareForMerge ( lists,numLists,minRecSizes);
		ttt.posdbMerge_r ( lists ,
				   numLists ,
				   startKey ,
				   endKey ,
				   m_mergeMinListSize,
				   removeNegRecs ,
				   filtered ,
				   niceness );
		// compare
		int32_t min = ttt.m_listSize;
		if ( min > m_listSize ) min = m_listSize;
		for ( int32_t k = 0 ; k < min ; k++ ) {
			if ( ttt.m_list[k] !=  m_list[k] ) {
				g_process.shutdownAbort(true);}
		}
		if ( ttt.m_listSize != m_listSize ) { g_process.shutdownAbort(true);}
		if ( ttt.m_listPtr - ttt.m_list !=
			    m_listPtr - m_list ) { g_process.shutdownAbort(true); }
		if ( ttt.m_listPtrLo - ttt.m_list !=
			    m_listPtrLo - m_list ) { g_process.shutdownAbort(true); }
		if ( ttt.m_listPtrHi - ttt.m_list !=
			    m_listPtrHi - m_list ) { g_process.shutdownAbort(true); }
		if ( ttt.m_listEnd - ttt.m_list !=
			    m_listEnd - m_list ) { g_process.shutdownAbort(true); }
		if ( ttt.m_fixedDataSize != m_fixedDataSize){
			g_process.shutdownAbort(true); }
		if ( ttt.m_useHalfKeys != m_useHalfKeys){g_process.shutdownAbort(true); }
		//if ( ttt.m_list &&
		//     memcmp ( ttt.m_list , m_list , ttt.m_listSize ) ){
		//	g_process.shutdownAbort(true);}
		if ( KEYCMP(ttt.m_endKey,m_endKey,m_ks) !=0){
			g_process.shutdownAbort(true);}
		if ( m_lastKeyIsValid &&
		     KEYCMP(ttt.m_lastKey,m_lastKey,m_ks)!=0){
			g_process.shutdownAbort(true);}
		if ( m_lastKeyIsValid !=ttt.m_lastKeyIsValid){
			g_process.shutdownAbort(true);}
	}
	*/
}

#include "Msg3.h" // #define for MAX_RDB_FILES

////////
//
// SPECIALTY MERGE FOR POSDB
//
///////



bool RdbList::posdbMerge_r ( RdbList **lists         ,
			     int32_t      numLists      ,
			     const char     *startKey      ,
			     const char     *endKey        ,
			     int32_t      minRecSizes   ,
			     bool      removeNegKeys ,
			     int32_t      niceness       ) {
	// sanity
	if ( m_ks != sizeof(key144_t) ) { g_process.shutdownAbort(true); }
	//no-op check
	if ( numLists == 0 ) return true;

#ifdef _MERGEDEBUG_
	log(LOG_LOGIC,"%s:%s: removeNegKeys: %s", __FILE__, __func__, removeNegKeys?"true":"false");
	log(LOG_LOGIC,"%s:%s: sk.n1=%" PRIx64" sk.n0=%" PRIx64" ek.n1=%" PRIx64" ek.n0=%" PRIx64,
	    __FILE__,__func__, KEY1(startKey,m_ks),KEY0(startKey),KEY1(endKey,m_ks),KEY0(endKey));
	log(LOG_LOGIC,"%s:%s: numLists: %" PRId32", m_allocSize=%" PRId32", m_mergeMinListSize=%" PRId32", minRecSizes=%" PRId32,
		__FILE__,__func__, numLists, m_allocSize, m_mergeMinListSize, minRecSizes);
#endif

	// did they call prepareForMerge()?
	if ( m_allocSize < m_mergeMinListSize ) {
		log(LOG_LOGIC,"db: rdblist: posdbMerge_r: prepareForMerge() "
		    "not called.");
		// save state and dump core, sigBadHandler will catch this
		g_process.shutdownAbort(true);
	}
	// warning msg
	if ( m_listPtr != m_listEnd )
		log(LOG_LOGIC,"db: rdblist: posdbMerge_r: warning. "
		    "merge not storing at end of list.");
	// sanity check
	if ( numLists>0 && lists[0]->m_ks != m_ks ) { g_process.shutdownAbort(true); }
	// set this list's boundary keys
	KEYSET(m_startKey,startKey,sizeof(key144_t));
	KEYSET(m_endKey,endKey,sizeof(key144_t));
	// . NEVER end in a negative rec key (dangling negative rec key)
	// . we don't want any positive recs to go un annhilated
	// . but don't worry about this check if start and end keys are equal
	//if ( m_startKey != m_endKey && (m_endKey.n0 & 0x01) == 0x00 )
	// . MDW: this happens during the qainject1() qatest in qa.cpp that
	//   deletes all the urls then does a dump of just negative keys.
	//   so let's comment it out for now
	// if ( KEYCMP(m_startKey,m_endKey,m_ks)!=0 && KEYNEG(m_endKey) ) {
	// 	log(LOG_LOGIC,"db: rdblist: posdbMerge_r: Illegal endKey for "
	// 	    "merging");
	// 	// this happens when dumping datedb... wtf?
	// 	//g_process.shutdownAbort(true);
	// }
	// bail if nothing requested
	if ( minRecSizes == 0 ) return true;

	// maxPtr set by minRecSizes
	const char *maxPtr = m_list + minRecSizes;
	// watch out for wrap around
	if ( maxPtr < m_list ) maxPtr = m_alloc + m_allocSize;
	// don't exceed what we alloc'd though
	if ( maxPtr > m_alloc + m_allocSize ) maxPtr = m_alloc + m_allocSize;

	// debug note
 	if ( m_listSize && g_conf.m_logDebugBuild )
		log(LOG_LOGIC,"db: storing recs in a non-empty list for merge"
		    " probably from recall from negative key loss");

	// bitch if too many lists
	if ( numLists > MAX_RDB_FILES + 1 ) {
		// set errno, cuz g_errno is used by main process only
		errno = EBADENGINEER;
		log(LOG_LOGIC,"db: rdblist: posdbMerge_r: Too many "
		    "lists for merging.");
		g_process.shutdownAbort(true);
	}

	// initialize the arrays, 1-1 with the unignored lists
	const char  *ptrs[ MAX_RDB_FILES + 1 ];
	const char  *ends[ MAX_RDB_FILES + 1 ];
	char       hiKeys[ MAX_RDB_FILES + 1 ][6];
	char       loKeys[ MAX_RDB_FILES + 1 ][6];
	// set the ptrs that are non-empty
	int32_t n = 0;
	// convenience ptr
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// skip if empty
		if ( lists[i]->isEmpty() ) continue;

#ifdef _MERGEDEBUG_
		log(LOG_LOGIC,"%s:%s: DUMPING LIST %" PRId32, __FILE__,__func__, i);
		lists[i]->printList(LOG_LOGIC);
#endif

		// . first key of a list must ALWAYS be 18 byte
		// . bitch if it isn't, that should be fixed!
		// . cheap sanity check
		if ( (lists[i]->getList()[0]) & 0x06 ) {
			errno = EBADENGINEER;
			log(LOG_LOGIC,"db: posdbMerge_r: First key of list is "
			    "a compressed key.");
			g_process.shutdownAbort(true);
		}
		// set ptrs
		ends    [n] = lists[i]->getListEnd ();
		ptrs    [n] = lists[i]->getList    ();
		memcpy(hiKeys[n], lists[i]->getList() + 12, 6);
		memcpy(loKeys[n], lists[i]->getList() +  6, 6);
		n++;
	}

	// new # of lists, in case any lists were empty
	numLists = n;

	// . are all lists and trash exhausted?
	// . all their keys are supposed to be <= m_endKey
	if ( numLists <= 0 ) return true;

	char *pp = NULL;

	// see Posdb.h for format of a 18/12/6-byte posdb key

	char *new_listPtr = m_listPtr;
	while ( numLists > 0 && new_listPtr < maxPtr) {

		// assume key in first list is the winner
		const char *minPtrBase = ptrs  [0]; // lowest  6 bytes
		const char *minPtrLo   = loKeys[0]; // next    6 bytes
		const char *minPtrHi   = hiKeys[0]; // highest 6 bytes
		int16_t mini     = 0;         // int16_t -> must be able to accomodate MAX_RDB_FILES!!

		// merge loop over the lists, get the smallest key
		for ( int32_t i = 1 ; i < numLists ; i++ ) {
			char ss = bfcmpPosdb (minPtrBase,minPtrLo,minPtrHi,
					 ptrs[i],loKeys[i],hiKeys[i]);
			// . continue if tie, so we get the oldest first
			// . treat negative and positive keys as identical for this
			if ( ss <  0 ) continue;
			// advance old winner. this happens if this key is positive
			// and minPtrBase/Lo/Hi was a negative key! so this is
			// the annihilation. skip the positive key.
			if ( ss == 0 ) goto skip;
			// we got a new min
			minPtrBase = ptrs  [i];
			minPtrLo   = loKeys[i];
			minPtrHi   = hiKeys[i];
			mini     = i;
		}

		// ignore if negative i guess, just skip it
		if ( removeNegKeys && (minPtrBase[0] & 0x01) == 0x00 ) goto skip;

		// save ptr
		pp = new_listPtr;

		// store key
		if ( m_listPtrHi && cmp_6bytes_equal(minPtrHi,m_listPtrHi)) {
			if(m_listPtrLo && cmp_6bytes_equal(minPtrLo,m_listPtrLo)) {
				// 6-byte entry
				memcpy(new_listPtr, minPtrBase, 6);
				new_listPtr += 6;
				*pp |= 0x06; //turn on both compression bits
			} else  {
				// 12-byte entry
				memcpy(new_listPtr, minPtrBase, 6);
				new_listPtr += 6;
				memcpy(new_listPtr, minPtrLo, 6);
				m_listPtrLo  = new_listPtr; // point to the new lo key
				new_listPtr += 6;
				*pp = (*pp&~0x04)|0x02; //turn on exactly 1 compression bit
			}
		} else {
			// 18-byte entry
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
		if ( ptrs[mini] < ends[mini] ) {
			// is new key 6 bytes? then do not touch hi/lo ptrs
			if ( ptrs[mini][0] & 0x04 ) {
			}
			// is new key 12 bytes?
			else if ( ptrs[mini][0] & 0x02 ) {
				memcpy(loKeys[mini], ptrs[mini] +  6, 6);
			}
			// is new key 18 bytes? full key.
			else {
				memcpy(hiKeys[mini], ptrs[mini] + 12, 6);
				memcpy(loKeys[mini], ptrs[mini] +  6, 6);
			}
		} else {
			//
			// REMOVE THE LIST at mini
			//

			// otherwise, remove him from array
			for ( int32_t i = mini ; i < numLists - 1 ; i++ ) {
				ptrs    [i] = ptrs    [i+1];
				ends    [i] = ends    [i+1];
				memcpy(hiKeys[i], hiKeys[i+1], 6);
				memcpy(loKeys[i], loKeys[i+1], 6);
			}
			// one less list to worry about
			numLists--;
		}
	}

	m_listPtr = new_listPtr;

	// come here to try to fix any dangling negatives

	// if last key we added is positive, skip this stuff
	//if ( (*minPtrBase & 0x01) == 0x01 ) goto positive;

	// if no lists left and no recyclable trash remains, nothing we can do
	//if ( numLists <= 0 ) goto positive;

	// . if there is a negative/positive key combo
	//   they should annihilate in the primary for loop above!! UNLESS
	//   one list was truncated at the end and we did not get its
	//   annihilating key... strange, but i guess it could happen...

	//positive:

	// set new size and end of this merged list
	m_listSize = m_listPtr - m_list;
	m_listEnd  = m_list    + m_listSize;

	// return now if we're empty... all our recs annihilated?
	if ( m_listSize <= 0 ) return true;

	// if we are tacking this merge onto a non-empty list
	// and we just had negative keys then pp could be NULL.
	// we would log "storing recs in a non-empty list" from
	// above and "pp" would be NULL.
	if ( pp ) {
		// the last key we stored
		char *e = m_lastKey;
		// record the last key we added in m_lastKey
		gbmemcpy ( e , pp , 6 );
		// take off compression bits
		*e &= 0xf9;
		e += 6;
		gbmemcpy ( e , m_listPtrLo , 6 );
		e += 6;
		gbmemcpy ( e , m_listPtrHi , 6 );
		// validate it now
		m_lastKeyIsValid = true;
	}

	if ( m_listSize && ! m_lastKeyIsValid )
		log("db: why last key not valid?");

	// under what was requested? then done.
	if ( m_listSize < minRecSizes ) {
#ifdef _MERGEDEBUG_
		log(LOG_LOGIC,"%s:%s:%d: Done.", __FILE__,__func__, __LINE__);
		printList(LOG_LOGIC);
#endif
		return true;
	}

	// or if no more lists
	if ( numLists <= 0 ) {
#ifdef _MERGEDEBUG_
		log(LOG_LOGIC,"%s:%s:%d: Done.", __FILE__,__func__, __LINE__);
		printList(LOG_LOGIC);
#endif
		return true;
	}

	// save original end key
	char orig[MAX_KEY_BYTES];
	memcpy ( orig, m_endKey, sizeof(key144_t) );

	// . we only need to shrink the endKey if we fill up our list and
	//   there's still keys under m_endKey left over to merge
	// . if no keys remain to merge, then don't decrease m_endKey
	// . i don't want the endKey decreased unnecessarily because
	//   it means there's no recs up to the endKey
	memcpy ( m_endKey, m_lastKey, sizeof(key144_t) );
	// if endkey is now negative we must have a dangling negative
	// so make it positive (dangling = unmatched)
	if ( KEYNEG(m_endKey) )
		KEYADD(m_endKey, sizeof(key144_t));
	// be careful not to increase original endkey, though
	if ( KEYCMP(orig,m_endKey,sizeof(key144_t))<0 )
		KEYSET(m_endKey, orig, sizeof(key144_t));

#ifdef _MERGEDEBUG_
		log(LOG_LOGIC,"%s:%s:%d: Done.", __FILE__,__func__, __LINE__);
		printList(LOG_LOGIC);
#endif

	return true;
}

void RdbList::setFromPtr ( char *p , int32_t psize , char rdbId ) {

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
