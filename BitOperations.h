#ifndef BIT_OPERATIONS_H_
#define BIT_OPERATIONS_H_

#include <inttypes.h>

// assume only one bit is set for this (used by Address.cpp)
int32_t getBitPosLL   ( uint8_t *bit );

int32_t getHighestLitBit  ( unsigned char     bits ) ;
int32_t getHighestLitBit  ( uint16_t    bits ) ;

// these are bit #'s, like 0,1,2,3,...63 for int64_ts
int32_t getLowestLitBitLL ( uint64_t bits ) ;

// this is the value, like 0,1,2,4, ... 4billion
uint32_t      getHighestLitBitValue   ( uint32_t      bits ) ;
uint64_t getHighestLitBitValueLL ( uint64_t bits ) ;



inline int32_t getHighestLitBit ( uint16_t bits ) {
	unsigned char b = *((unsigned char *)(&bits) + 1);
	if ( ! b ) return getHighestLitBit ( (unsigned char) bits );
	return 8 + getHighestLitBit ( (unsigned char) b );
}

inline int32_t getHighestLitBit ( unsigned char c ) {
	static const char a[256] = { 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
				     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
				     5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
				     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
				     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
				     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
				     6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
				     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
	return a[(unsigned char) c];
}

extern const char g_a[];

inline int32_t getNumBitsOn8 ( unsigned char c ) {
	return g_a[(unsigned char) c];
}

inline int32_t getNumBitsOn16 ( uint16_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] ;
}

inline int32_t getNumBitsOn32 ( uint32_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] ;
}

inline int32_t getNumBitsOn64 ( uint64_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] +
		g_a [ *((unsigned char *)(&bits) + 4)  ] +
		g_a [ *((unsigned char *)(&bits) + 5)  ] +
		g_a [ *((unsigned char *)(&bits) + 6)  ] +
		g_a [ *((unsigned char *)(&bits) + 7)  ] ;
}

inline int32_t getNumBitsOnX ( unsigned char *s , int32_t slen ) {
	if ( slen == 1 ) return getNumBitsOn8 ( *s );
	if ( slen == 2 ) return getNumBitsOn16 ( *(uint16_t *)s );
	if ( slen == 4 ) return getNumBitsOn32 ( *(uint32_t *)s );
	if ( slen == 3 ) 
		return  getNumBitsOn8 ( s[0] ) +
			getNumBitsOn8 ( s[1] ) +
			getNumBitsOn8 ( s[2] ) ;
	int32_t total = 0;
	for ( int32_t i = 0 ; i < slen ; i++ )
		total += getNumBitsOn8 ( s[i] );
	return total;
}

// assume only one bit is set for this (used by Address.cpp)
inline int32_t getBitPosLL ( uint8_t *bit ) {
	// which int32_t is it in?
	if ( *(int32_t *)bit ) {
		if ( bit[0] ) return getHighestLitBit ( bit[0] );
		if ( bit[1] ) return getHighestLitBit ( bit[1] ) + 8;
		if ( bit[2] ) return getHighestLitBit ( bit[2] ) + 16;
		if ( bit[3] ) return getHighestLitBit ( bit[3] ) + 24;
		char *xx=NULL;*xx=0; 
	}
	if ( bit[4] ) return getHighestLitBit ( bit[4] ) + 32;
	if ( bit[5] ) return getHighestLitBit ( bit[5] ) + 40;
	if ( bit[6] ) return getHighestLitBit ( bit[6] ) + 48;
	if ( bit[7] ) return getHighestLitBit ( bit[7] ) + 56;
	char *xx=NULL;*xx=0; 
	return -1;
}

#endif
