#ifndef BIT_OPERATIONS_H_
#define BIT_OPERATIONS_H_

#include <inttypes.h>

static inline int32_t getHighestLitBit(unsigned char c) {
	return c ? 7-(__builtin_clz(c)-24) : 0;
}

static inline int32_t getHighestLitBit(uint16_t bits) {
	return bits ? 15-(__builtin_clz(bits)-16) : 0;
}

// this is the value, like 0,1,2,4, ... 4billion
static inline uint32_t getHighestLitBitValue(uint32_t bits) {
	return bits ? 1<<(32-__builtin_clz(bits)-1) : 0;
}

static inline uint64_t getHighestLitBitValueLL(uint64_t bits) {
	return bits ? ((uint64_t)1)<<(64-__builtin_clzll(bits)-1) : 0;
}


static inline int32_t getNumBitsOn8 ( unsigned char c ) {
	return __builtin_popcount(c);
}

static inline int32_t getNumBitsOn16 ( uint16_t bits ) {
	return  __builtin_popcount(bits);
}

static inline int32_t getNumBitsOn32 ( uint32_t bits ) {
	return __builtin_popcount(bits);
}

static inline int32_t getNumBitsOn64 ( uint64_t bits ) {
	return __builtin_popcountll(bits);
}

static inline int32_t getNumBitsOnX ( const unsigned char *s , int32_t slen ) {
	if ( slen == 1 ) return getNumBitsOn8 ( *s );
	if ( slen == 2 ) return getNumBitsOn16 ( *(const uint16_t *)(const void*)s );
	if ( slen == 4 ) return getNumBitsOn32 ( *(const uint32_t *)(const void*)s );
	if ( slen == 3 ) 
		return  getNumBitsOn8 ( s[0] ) +
			getNumBitsOn8 ( s[1] ) +
			getNumBitsOn8 ( s[2] ) ;
	int32_t total = 0;
	for ( int32_t i = 0 ; i < slen ; i++ )
		total += getNumBitsOn8 ( s[i] );
	return total;
}


static inline int32_t getBitPosLL ( const uint8_t *bit ) {
	// which int32_t is it in?
	if ( *(const int32_t *)(const void*)bit ) {
		if ( bit[0] ) return getHighestLitBit ( bit[0] );
		if ( bit[1] ) return getHighestLitBit ( bit[1] ) + 8;
		if ( bit[2] ) return getHighestLitBit ( bit[2] ) + 16;
		if ( bit[3] ) return getHighestLitBit ( bit[3] ) + 24;
	}
	if ( bit[4] ) return getHighestLitBit ( bit[4] ) + 32;
	if ( bit[5] ) return getHighestLitBit ( bit[5] ) + 40;
	if ( bit[6] ) return getHighestLitBit ( bit[6] ) + 48;
	if ( bit[7] ) return getHighestLitBit ( bit[7] ) + 56;
	for(int i=8; ; i++) {
		if(bit[i])
			return getHighestLitBit ( bit[i] ) + i*8;
	}
}


static inline uint64_t extract_bits(const char *key, unsigned lsb, unsigned msb) {
//        assert(msb>lsb);
        unsigned start_byte = lsb/8;
        unsigned start_bit_in_first_byte = lsb%8;
        uint64_t v = *(const uint64_t*)(key+start_byte);
        v >>= start_bit_in_first_byte;
        uint64_t mask = ~((~UINT64_C(0))<<(msb-lsb));
        v &= mask;
        return v;
}


#endif
