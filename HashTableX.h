// Matt Wells,  Copyright, Dec. 2002

// . generic hash table class

#ifndef GB_HASHTABLEX_H
#define GB_HASHTABLEX_H

#include "SafeBuf.h"
#include "Sanity.h"


class HashTableX {

 public:

	bool set ( int32_t  keySize         ,
		   int32_t  dataSize        ,
		   int32_t  initialNumSlots , // = 0    ,
		   char *buf             , // = NULL ,
		   int32_t  bufSize         , // = 0    ,
		   bool  allowDups       , // = false ,
		   int32_t  niceness        , // = MAX_NICENESS ,
		   const char *allocName  ,
		   bool  useKeyMagic = false );

	// key size is 0 if UNinitialized
	bool isInitialized ( ) const { return (m_ks != 0); }

	 HashTableX       ( );
	~HashTableX       ( );

	// . add key/value entry to hash table
	// . will grow hash table if it needs to
	// . returns false and sets g_errno on error, returns true otherwise
	bool addKey ( const void *key , const void *value , int32_t *slot = NULL );

	// for value-less hashtables
	bool addKey ( const void *key );

	// . remove key/value entry to hash table. 
	// . returns false and sets g_errno on error.
	bool removeKey  ( const void *key );

	// same as remove
	bool deleteSlot ( int32_t n ) { return removeSlot(n); }

	// like removeKey. returns false and sets g_errno on error.
	bool removeSlot ( int32_t n );


	// a replacement for TermTable.cpp
	bool addTerm ( const int64_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	}
	// a replacement for TermTable.cpp
	uint32_t getScore ( const int64_t *wid ) const {
		int32_t slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(const uint32_t *)getValueFromSlot ( slot );
	}
	// a replacement for TermTable.cpp
	uint64_t getScore64FromSlot ( int32_t slot ) const {
		return *(const uint64_t *)getValueFromSlot ( slot ); }


	bool addTerm32 ( const int32_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	}
	bool addTerm32 ( const uint32_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	}
	bool addScore ( const int32_t *key , int32_t score = 1 ) {
		return addTerm32 ( key , score ); 
	}
	uint32_t getScore32 ( const int32_t *wid ) const {
		int32_t slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(const uint32_t *)getValueFromSlot ( slot );
	}


	bool addTerm144 ( const key144_t *kp , int32_t score = 1 ) {

		/*
		// debug XmlDoc.cpp's hash table
		int64_t termId = ((key144_t *)kp)->n2 >> 16;
		uint64_t d = 0LL;
		d = ((unsigned char *)kp)[11];
		d <<= 32;
		d |= *(uint32_t *)(((unsigned char *)kp)+7);
		d >>= 2;
		if ( d==110324895284 && termId == 39206941907955LL ) {
			log("got it");
			gbshutdownAbort(true);
		}
		*/
		// grow it!
		if ( (m_numSlots < 20 || 4 * m_numSlotsUsed >= m_numSlots) &&
		     m_numSlots < m_maxSlots ) {
			int64_t growTo ;
			growTo = ((int64_t)m_numSlots * 150LL )/100LL+20LL;
			if ( growTo > m_maxSlots ) growTo = m_maxSlots;
			if ( ! setTableSize ( (int32_t)growTo , NULL , 0 ) ) 
				return false;
		}
		// hash it up
		int32_t n = hash32 ( (const char *)kp, 18 );
		// then mask it
		n &= m_mask;
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) {
				gbmemcpy( &((key144_t *)m_keys)[n] ,kp,18);
				m_vals[n*m_ds] = score;
				m_flags[n] = 1;
				m_numSlotsUsed++;
				return true;
			}
			// get the key there
			if (((key144_t *)m_keys)[n] == *kp) {
				uint32_t *val = (uint32_t *)&m_vals[n*m_ds];
				// overflow check
				if ( *val + (uint32_t)score < *val ) 
					*val = 0xffffffff;
				else 
					*val = *val + score;
				return true;
			}
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		// crazy!
		log("hash: table is full!");
		gbshutdownAbort(true);
		/*NOTREACHED*/
		return true;
	}

	// return 32-bit checksum of keys in table
	int32_t getKeyChecksum32 () const;

	// . used by ../english/Bits.h to store stop words, abbr's, ...
	// . returns the score for this termId (0 means empty usually)
	// . return 0 if key not in hash table
	void *getValue ( const void *key ) {
		// make it fast
		if ( m_ks == 4 ) return getValue32 ( *(const int32_t *)key );
		if ( m_ks == 8 ) return getValue64 ( *(const int64_t *)key );
		// returns -1 if key not in hash table
		int32_t n = getOccupiedSlotNum ( key );
		if ( n < 0 ) return NULL;
		return &m_vals[n*m_ds];
	}

	// . specialized for 32-bit keys for speed
	// . returns NULL if not in table
	void *getValue32 ( int32_t key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 4 ) { gbshutdownAbort(true); }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			n = key & m_mask;
		}
		else {
			// get lower 32 bits of key
			//n = (uint32_t)key;
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			// use magic to "randomize" key a little
			n^=g_hashtab[(unsigned char)((char *)&key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((int32_t *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	}

	// . specialized for 64-bit keys for speed
	// . returns NULL if not in table
	void *getValue64 ( int64_t key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 8 ) { gbshutdownAbort(true); }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			// get lower 32 bits of key
			n = key & m_mask;
		}
		else {
			// use magic to "randomize" key a little
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			n ^= g_hashtab[(unsigned char)((char *)&key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((int64_t *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	}

	// value of 0 means empty
	bool isEmpty ( const void *key ) const { return (getSlot(key) < 0); }

	bool isInTable ( const void *key ) const { return (getSlot(key) >= 0); }

	bool isEmpty ( int32_t n ) const { return (m_flags[n] == 0); }

	bool isTableEmpty ( ) const { return (m_numSlotsUsed == 0); }

	void *      getKey ( int32_t n )       { return m_keys + n * m_ks; }
	const void *getKey ( int32_t n ) const { return m_keys + n * m_ks; }
	void *      getKeyFromSlot ( int32_t n )       { return m_keys + n * m_ks; }
	const void *getKeyFromSlot ( int32_t n ) const { return m_keys + n * m_ks; }

	int64_t getKey64FromSlot ( int32_t n ) const {
		return *(int64_t *)(m_keys+n*m_ks); }

	int32_t getSlot ( const void *key ) const { return getOccupiedSlotNum ( key ); }

	int32_t getNextSlot ( int32_t slot, const void *key ) const;

	// count how many slots have this key
	int32_t getCount ( const void *key ) const;

	void setValue ( int32_t n , const void *val ) { 
		if      (m_ds == 4) ((int32_t *)m_vals)[n] = *(const int32_t *)val;
		else if (m_ds == 8) ((int64_t *)m_vals)[n] = *(const int64_t *)val;
		else                gbmemcpy(m_vals+n*m_ds,val,m_ds);
	}

	void *      getValueFromSlot ( int32_t n )       { return m_vals + n * m_ds; }
	const void *getValueFromSlot ( int32_t n ) const { return m_vals + n * m_ds; }
	void *      getDataFromSlot  ( int32_t n )       { return m_vals + n * m_ds; }
	const void *getDataFromSlot  ( int32_t n ) const { return m_vals + n * m_ds; }

	// frees the used memory, etc.
	void  reset  ( );

	// removes all key/value pairs from hash table, vacates all slots
	void  clear  ( );

	// how many are occupied?
	int32_t getNumSlotsUsed ( ) const { return m_numSlotsUsed; }
	int32_t getNumUsedSlots ( ) const { return m_numSlotsUsed; }

	bool isEmpty() const { 
		if ( m_numSlotsUsed == 0 ) return true;
		return false; }

	// how many are there total? used and unused.
	int32_t getNumSlots ( ) const { return m_numSlots; }

	// both return false and set g_errno on error, true otherwise
	bool load ( const char *dir, const char *filename , 
		    char **tbuf = NULL , int32_t *tsize = NULL );

	bool save ( const char *dir, const char *filename , 
		    const char  *tbuf = NULL , int32_t  tsize = 0);

	bool setTableSize ( int32_t numSlots , char *buf , int32_t bufSize );

	void disableWrites () { m_isWritable = false; }
	void enableWrites  () { m_isWritable = true ; }
	bool m_isWritable;

 private:

	int32_t getOccupiedSlotNum ( const void *key ) const;

 public:

	// . the array of buckets in which we store the terms
	// . scores are allowed to exceed 8 bits for weighting purposes
	char  *m_keys;
	char  *m_vals;
	char  *m_flags;

	int32_t     m_numSlots;
	int32_t     m_numSlotsUsed;
	uint32_t m_mask;

	char  m_doFree;
	char *m_buf;
	int32_t  m_bufSize;

	char m_useKeyMagic;

	int32_t m_ks;
	int32_t m_ds;
	char m_allowDups;
	int32_t m_niceness;

	// a flag used by XmlDoc.cpp
	bool m_addIffNotUnique;

	bool m_isSaving;
	bool m_needsSave;

	// limits growing to this # of slots total
	int64_t  m_maxSlots;

	const char *m_allocName;
	
	int32_t m_maskKeyOffset;

	// the addon buf used by SOME hashtables. data that the ptrs
	// in the table itself reference.
	char *m_txtBuf;
	int32_t  m_txtBufSize;
};

#endif // GB_HASHTABLEX_H
