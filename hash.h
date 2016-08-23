// Matt Wells, Copyright Apr 2001

// . don't use XOR for hashing, "dog" would be the same as "god"

#ifndef GB_HASH_H
#define GB_HASH_H

#include "Unicode.h"

//#define SEED8  148
//#define SEED16 22081
//#define SEED32 987654321
//#define SEED64 5148502070294393521LL
//#define SEED8  9876
//#define SEED32 87654321
//#define SEED64 7651331LL

// call this before calling any hash*() routines so we can fill our table
extern uint64_t g_hashtab[256][256];

#include "types.h"
#include "fctypes.h"
#include "Sanity.h"

bool hashinit ();

unsigned char hash8            ( const char *s , int32_t len ) ;
uint64_t      hash64n_nospaces ( const char *s , int32_t len ) ;
uint32_t hash32n          ( const char *s ) ;
uint32_t hash32           ( const char *s, int32_t len,uint32_t startHash=0);
uint32_t hash32h          ( uint32_t h1 , uint32_t h2 ) ;
uint64_t      hash64h          ( uint64_t h1 , uint64_t h2 );
uint32_t hash32Lower_a    ( const char *s, int32_t len,uint32_t startHash=0);
uint64_t      hash64n          ( const char *s, uint64_t startHash =0LL);
uint96_t      hash96           ( const char *s, int32_t slen);

// . these convert \n to \0 when hashing
// . these hash all punct as a space, except for hyphen and single quote!
// . these lower-case all alnum chars, even crazy utf8 chars that can be cap'd
// . these only take utf8 strings
uint64_t hash64d ( const char *s, int32_t slen );



static inline uint64_t hash64b ( const char *s , uint64_t startHash = 0) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( s[i] ) {
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
		i++;
	}
	return h;
}

static inline uint64_t hash64 ( const char *s, int32_t len,
				   uint64_t startHash = 0 ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) { 
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
		i++;
	}
	return h;
}

static inline uint64_t hash64_cont ( const char *s, int32_t len,
					uint64_t startHash ,
					int32_t *conti ) {
	uint64_t h = startHash;
	int32_t i = 0;//*conti;
	while ( i < len ) { 
		h ^= g_hashtab [(unsigned char)(i+*conti)][(unsigned char)s[i]];
		i++;
	}
	*conti = *conti + i;
	return h;
}

static inline uint32_t hash32Fast ( uint32_t h1 , uint32_t h2 ) {
	return (h2 << 1) ^ h1;
}

// . combine 2 hashes into 1
// . TODO: ensure this is a good way
// . used for combining words' hashes into phrases (also fields,collections)..
static inline uint64_t hash64 (uint64_t h1,uint64_t h2){
	// treat the 16 bytes as a string now instead of multiplying them
	uint64_t h = 0;

	h ^= g_hashtab [ 0] [ *((unsigned char *)(&h1)+0) ] ;
	h ^= g_hashtab [ 1] [ *((unsigned char *)(&h1)+1) ] ;
	h ^= g_hashtab [ 2] [ *((unsigned char *)(&h1)+2) ] ;
	h ^= g_hashtab [ 3] [ *((unsigned char *)(&h1)+3) ] ;
	h ^= g_hashtab [ 4] [ *((unsigned char *)(&h1)+4) ] ;
	h ^= g_hashtab [ 5] [ *((unsigned char *)(&h1)+5) ] ;
	h ^= g_hashtab [ 6] [ *((unsigned char *)(&h1)+6) ] ;
	h ^= g_hashtab [ 7] [ *((unsigned char *)(&h1)+7) ] ;

	h ^= g_hashtab [ 8] [ *((unsigned char *)(&h2)+0) ] ;
	h ^= g_hashtab [ 9] [ *((unsigned char *)(&h2)+1) ] ;
	h ^= g_hashtab [10] [ *((unsigned char *)(&h2)+2) ] ;
	h ^= g_hashtab [11] [ *((unsigned char *)(&h2)+3) ] ;
	h ^= g_hashtab [12] [ *((unsigned char *)(&h2)+4) ] ;
	h ^= g_hashtab [13] [ *((unsigned char *)(&h2)+5) ] ;
	h ^= g_hashtab [14] [ *((unsigned char *)(&h2)+6) ] ;
	h ^= g_hashtab [15] [ *((unsigned char *)(&h2)+7) ] ;

	return h;
}


static inline uint64_t hash64Lower_a ( const char *s, int32_t len,
					uint64_t startHash = 0) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_lower_a(s[i])];
		i++;
	}
	return h;
}

// utf8
static inline uint64_t hash64Lower_utf8 ( const char *p, int32_t len, uint64_t startHash = 0) {
	uint64_t h = startHash;
	uint8_t i = 0;
	const char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// convert utf8 apostrophe to ascii apostrophe so Words.cpp
		// gets the right wid for stuff like "you're" when the
		// apostrophe is in utf8
		//if ( p[0]==(char)0xe2 && 
		//     p[1]==(char)0x80 && 
		//     cs==3 && 
		//     (p[2]==(char)0x99||p[2]==(char)0x9c) ) {
		//	h ^= g_hashtab [i++][(uint8_t)'\''];
		//	continue;
		//}
		// otherwise, lower case it
		x = utf8Decode((char *)p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );
		// sanity check
		if ( ncs > 4 ) { gbshutdownAbort(true); }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}

static inline uint64_t hash64Lower_utf8_nospaces ( const char *p, int32_t len  ) {
	uint64_t h = 0LL;
	uint8_t i = 0;
	const char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			// skip spaces
			if ( is_wspace_a(*p) ) continue;
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// otherwise, lower case it
		x = utf8Decode((char *)p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );
		// sanity check
		if ( ncs > 4 ) { gbshutdownAbort(true); }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}


static inline uint64_t hash64Lower_utf8_cont ( const char *p,
					int32_t len, 
					uint64_t startHash ,
					int32_t *conti ) {
	uint64_t h = startHash;
	uint8_t i = *conti;
	const char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++][(uint8_t)to_lower_a(*p)];
			continue;
		}

		// otherwise, lower case it
		x = utf8Decode((char *)p);

		// convert to lower
		y = ucToLower (x);

		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );

		// sanity check
		if ( ncs > 4 ) { gbshutdownAbort(true); }

		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}

		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 )
			continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 )
			continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 )
			continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	// update this so caller can re-call with the right i
	*conti = i;
	return h;
}

static inline uint32_t hash32_cont ( const char *p, int32_t plen,
			      uint32_t startHash , int32_t *conti ) {
	uint32_t h = startHash;
	uint8_t i = *conti;
	const char *pend = p + plen;
	for ( ; p < pend ; p++ ) {
		h ^= (uint32_t)g_hashtab [i++] [(uint8_t)(*p)];
	}
	// update this so caller can re-call with the right i
	*conti = i;
	return h;
}


// utf8

// exactly like above but p is NULL terminated for sure
static inline uint64_t hash64Lower_utf8 ( const char *p ) {
	uint64_t h = 0;
	uint8_t i = 0;
	UChar32 x;
	UChar32 y;
	char cs;
	for ( ; *p ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// otherwise, lower case it
		x = utf8Decode(p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , (char *)tmp );
		// sanity check
		if ( ncs > 4 ) { gbshutdownAbort(true); }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}


static inline uint64_t hash64Upper_a ( const char *s , int32_t len ,
					  uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_upper_a(s[i])]; 
		i++; 
	}
	return h;
}


static inline uint32_t hashLong ( uint32_t x ) {
	uint32_t h = 0;
	unsigned char *p = (unsigned char *)&x;
	h ^= (uint32_t) g_hashtab [0][p[0]];
	h ^= (uint32_t) g_hashtab [1][p[1]];
	h ^= (uint32_t) g_hashtab [2][p[2]];
	h ^= (uint32_t) g_hashtab [3][p[3]];
	return h;
}

#endif // GB_HASH_H
