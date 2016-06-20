#ifndef GB_TYPES_H
#define GB_TYPES_H

#include <string.h>

// Ugly - but so is lots of code in .h files
extern void gbshutdownAbort( bool save_on_abort );

// . up to 32768 collections possible, MUST be signed
// . a collnum_t of -1 is used by RdbCache to mean "no collection"
typedef int16_t collnum_t;

#define MAX_KEY_BYTES 28

class u_int96_t {

 public:
	// it's little endian
	uint64_t n0; // the low  int64_t
	uint32_t      n1; // the high int32_t 

	u_int96_t (                ) { }
	u_int96_t ( uint32_t i ) {	n0 = i; n1 = 0; }

	void setMin ( ) { n0 = 0LL; n1 = 0; }

	void setMax ( ) { n0 = 0xffffffffffffffffLL; n1 = 0xffffffff; }

	bool operator == ( u_int96_t i ) const {
		return ( i.n0 == n0 && i.n1 == n1);}
	bool operator != ( u_int96_t i ) const {
		return ( i.n0 != n0 || i.n1 != n1);}
	void operator =  ( u_int96_t i ) {
		n0 = i.n0; n1 = i.n1; }

	bool operator != ( uint32_t i ) const {
		return ( i    != n0 ); }
	void operator =  ( uint32_t i ) {
		n0 = i; n1 = 0; }
	int32_t operator &  ( uint32_t i ) const {
		return n0 & i; }

	void operator |= ( u_int96_t i ) {
		n0 |= i.n0;
		n1 |= i.n1;
	}

	// NOTE: i must be bigger than j!?
	void operator -= ( uint32_t i ) {
		if ( n0 - i > n0 ) n1--;
		n0 -= i;
	}

	void operator += ( uint32_t i ) { // watch out for carry
		if ( n0 + i < n0 ) n1++;
		n0 += i; }

	bool operator >  ( u_int96_t i ) const {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		return false;
	}
	bool operator <  ( u_int96_t i ) const {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	}
	bool operator <= ( u_int96_t i ) const {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		if ( n0 > i.n0 ) return false;
		return true;
	}
	bool operator >= ( u_int96_t i ) const {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		if ( n0 < i.n0 ) return false;
		return true;
	}

} __attribute__((packed, aligned(4)));

class u_int128_t {

 public:
	// it's little endian
	uint64_t n0; // the low  int64_t
	uint64_t n1; // the high int32_t 

	u_int128_t (                ) { }
	u_int128_t ( uint32_t i ) {	n0 = i; n1 = 0; }

	void setMin ( ) { n0 = 0LL; n1 = 0LL; }

	void setMax ( ) { n0=0xffffffffffffffffLL; n1=0xffffffffffffffffLL;}

	bool operator == ( u_int128_t i ) const {
		return ( i.n0 == n0 && i.n1 == n1);}
	bool operator != ( u_int128_t i ) const {
		return ( i.n0 != n0 || i.n1 != n1);}
	void operator =  ( u_int128_t i ) {
		n0 = i.n0; n1 = i.n1; }

	bool operator != ( uint32_t i ) const {
		return ( i    != n0 ); }
	void operator =  ( uint32_t i ) {
		n0 = i; n1 = 0; }
	int32_t operator &  ( uint32_t i ) const {
		return n0 & i; }

	void operator |= ( u_int128_t i ) {
		n0 |= i.n0;
		n1 |= i.n1;
	}

	// NOTE: i must be bigger than j!?
	void operator -= ( uint32_t i ) {
		if ( n0 - i > n0 ) n1--;
		n0 -= i;
	}

	void operator += ( uint32_t i ) { // watch out for carry
		if ( n0 + i < n0 ) n1++;
		n0 += i; }

	bool operator >  ( u_int128_t i ) const {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		return false;
	}
	bool operator <  ( u_int128_t i ) const {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	}
	bool operator <= ( u_int128_t i ) const {
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		if ( n0 > i.n0 ) return false;
		return true;
	}
	bool operator >= ( u_int128_t i ) const {
		if ( n1 > i.n1 ) return true;
		if ( n1 < i.n1 ) return false;
		if ( n0 > i.n0 ) return true;
		if ( n0 < i.n0 ) return false;
		return true;
	}
	// TODO: should we fix this?
	int32_t operator %  ( uint32_t mod ) const {
		return n0 % mod;
	}

} __attribute__((packed, aligned(4)));

class key192_t {
 public:
	// it's little endian
	uint64_t n0; // the low  int64_t
	uint64_t n1; // the medium int64_t
	uint64_t n2; // the high int64_t

	bool operator == ( key192_t i ) const {
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 
			 );}

	void operator += ( uint32_t i ) { // watch out for carry
		if ( n0 + i < n0 ) {
			if ( n1 + i < n1 )
				n2++;
			n1 += i;
		}
		n0 += i; 
	}

	bool operator <  ( key192_t i ) const {
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	}
	void setMin ( ) { n0 = 0LL; n1 = 0LL; n2 = 0LL; }


	void setMax ( ) { 
		n0=0xffffffffffffffffLL; 
		n1=0xffffffffffffffffLL;
		n2=0xffffffffffffffffLL;
	}


} __attribute__((packed, aligned(4)));

class key224_t {
 public:
	// it's little endian
	uint32_t      n0;
	uint64_t n1; // the low  int64_t
	uint64_t n2; // the medium int64_t
	uint64_t n3; // the high int64_t

	bool operator == ( key224_t i ) const {
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 &&
			 i.n3 == n3
			 );}

	void operator += ( uint32_t i ) { // watch out for carry
		if ( n0 + i > n0 ) { n0 += i; return; }
		if ( n1 + 1 > n1 ) { n1 += 1; n0 += i; return; }
		if ( n2 + 1 > n2 ) { n2 += 1; n1 += 1; n0 += i; return; }
		n3 += 1; n2 += 1; n1 += 1; n0 += i; return;
	}

	// NOTE: i must be bigger than j!?
	void operator -= ( uint32_t i ) {
		if ( n0 - i < n0 ) {n0 -= i;return;}
		if ( n1 - i < n1 ) { n1--; n0 -=i; return; }
		if ( n2 - i < n2 ) { n2--; n1--; n0 -=i; return; }
		n3--; n2--; n1--; n0 -= i;
	}

	bool operator <  ( key224_t i ) const {
		if ( n3 < i.n3 ) return true;
		if ( n3 > i.n3 ) return false;
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	}
	void setMin ( ) { n0 = 0; n1 = 0LL; n2 = 0LL; n3 = 0LL; }


	void setMax ( ) { 
		n0=0xffffffff;
		n1=0xffffffffffffffffLL; 
		n2=0xffffffffffffffffLL;
		n3=0xffffffffffffffffLL;
	}


} __attribute__((packed, aligned(4)));

class key144_t {
 public:
	// it's little endian
	uint16_t  n0; // the low int16_t
	uint64_t  n1; // the medium int64_t
	uint64_t  n2; // the high int64_t

	bool operator == ( key144_t i ) { 
		return ( i.n0 == n0 && 
			 i.n1 == n1 && 
			 i.n2 == n2 
			 );}

	void operator += ( uint32_t i ) { // watch out for carry
		if ( (uint16_t)(n0+i) > n0 ) { n0 += i; return; }
		if ( n1 + 1 > n1 ) { n1 += 1; n0 += i; return; }
		n2 += 1; n1 += 1; n0 += i; return;
	}

	// NOTE: i must be bigger than j!?
	void operator -= ( uint32_t i ) {
		if ( (uint16_t)(n0 - i) < n0 ) {n0 -= i;return;}
		if ( n1 - i < n1 ) { n1--; n0 -=i; return; }
		n2--; n1--; n0 -= i;
	}

	bool operator <  ( key144_t i ) {
		if ( n2 < i.n2 ) return true;
		if ( n2 > i.n2 ) return false;
		if ( n1 < i.n1 ) return true;
		if ( n1 > i.n1 ) return false;
		if ( n0 < i.n0 ) return true;
		return false;
	}
	void setMin ( ) { n0 = 0; n1 = 0LL; n2 = 0LL; }


	void setMax ( ) { 
		n0=0xffff;
		n1=0xffffffffffffffffLL;
		n2=0xffffffffffffffffLL;
	}


} __attribute__((packed, aligned(2)));


// handy quicky functions
inline char KEYCMP ( const char *k1, const char *k2, char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		if ( (*(const uint64_t *)(k1+10)) <
		     (*(const uint64_t *)(k2+10)) ) return -1;
		if ( (*(const uint64_t *)(k1+10)) >
		     (*(const uint64_t *)(k2+10)) ) return  1;
		if ( (*(const uint64_t *)(k1+2)) <
		     (*(const uint64_t *)(k2+2)) ) return -1;
		if ( (*(const uint64_t *)(k1+2)) >
		     (*(const uint64_t *)(k2+2)) ) return  1;
		if ( (*(const uint16_t *)(k1)) <
		     (*(const uint16_t *)(k2)) ) return -1;
		if ( (*(const uint16_t *)(k1)) >
		     (*(const uint16_t *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 12 ) { 
		if ( (*(const uint64_t *)(k1+4)) < 
		     (*(const uint64_t *)(k2+4)) ) return -1;
		if ( (*(const uint64_t *)(k1+4)) > 
		     (*(const uint64_t *)(k2+4)) ) return  1;
		if ( (*(const uint32_t *)(k1)) < 
		     (*(const uint32_t *)(k2)) ) return -1;
		if ( (*(const uint32_t *)(k1)) > 
		     (*(const uint32_t *)(k2)) ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 16 ) {
		if ( (*(const uint64_t *)(k1+8)) <
		     (*(const uint64_t *)(k2+8)) ) return -1;
		if ( (*(const uint64_t *)(k1+8)) >
		     (*(const uint64_t *)(k2+8)) ) return  1;
		if ( (*(const uint64_t *)(k1)) <
		     (*(const uint64_t *)(k2)) ) return -1;
		if ( (*(const uint64_t *)(k1)) >
		     (*(const uint64_t *)(k2)) ) return  1;
		return 0;
	}
	// allow half key comparison too
	if ( keySize == 6 ) {
		if ( (*(const uint32_t *)(k1+2)) <
		     (*(const uint32_t *)(k2+2)) ) return -1;
		if ( (*(const uint32_t *)(k1+2)) >
		     (*(const uint32_t *)(k2+2)) ) return  1;
		if ( (*(const uint16_t *)(k1+0)) <
		     (*(const uint16_t *)(k2+0)) ) return -1;
		if ( (*(const uint16_t *)(k1+0)) >
		     (*(const uint16_t *)(k2+0)) ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 24 ) {
		if ( (*(const uint64_t *)(k1+16)) <
		     (*(const uint64_t *)(k2+16)) ) return -1;
		if ( (*(const uint64_t *)(k1+16)) >
		     (*(const uint64_t *)(k2+16)) ) return  1;
		if ( (*(const uint64_t *)(k1+8)) <
		     (*(const uint64_t *)(k2+8)) ) return -1;
		if ( (*(const uint64_t *)(k1+8)) >
		     (*(const uint64_t *)(k2+8)) ) return  1;
		if ( (*(const uint64_t *)(k1)) <
		     (*(const uint64_t *)(k2)) ) return -1;
		if ( (*(const uint64_t *)(k1)) >
		     (*(const uint64_t *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 28 ) {
		if ( (*(const uint64_t *)(k1+20)) <
		     (*(const uint64_t *)(k2+20)) ) return -1;
		if ( (*(const uint64_t *)(k1+20)) >
		     (*(const uint64_t *)(k2+20)) ) return  1;
		if ( (*(const uint64_t *)(k1+12)) <
		     (*(const uint64_t *)(k2+12)) ) return -1;
		if ( (*(const uint64_t *)(k1+12)) >
		     (*(const uint64_t *)(k2+12)) ) return  1;
		if ( (*(const uint64_t *)(k1+4)) <
		     (*(const uint64_t *)(k2+4)) ) return -1;
		if ( (*(const uint64_t *)(k1+4)) >
		     (*(const uint64_t *)(k2+4)) ) return  1;
		if ( (*(const uint32_t *)(k1)) <
		     (*(const uint32_t *)(k2)) ) return -1;
		if ( (*(const uint32_t *)(k1)) >
		     (*(const uint32_t *)(k2)) ) return  1;
		return 0;
	}
	if ( keySize == 8 ) {
		if ( (*(const uint64_t *)(k1+0)) <
		     (*(const uint64_t *)(k2+0)) ) return -1;
		if ( (*(const uint64_t *)(k1+0)) >
		     (*(const uint64_t *)(k2+0)) ) return  1;
		return 0;
	}
	gbshutdownAbort(true);
	return 0;
}


inline char KEYCMP( const char *k1, int32_t a, const char *k2, int32_t b, char keySize ) {
	return KEYCMP(k1+a*keySize, k2+b*keySize, keySize);
}


inline char KEYCMPNEGEQ ( const char *k1, const char *k2, char keySize ) {
	// posdb
	if ( keySize == 18 ) { 
		if ( (*(const uint64_t *)(k1+10)) < 
		     (*(const uint64_t *)(k2+10)) ) return -1;
		if ( (*(const uint64_t *)(k1+10)) > 
		     (*(const uint64_t *)(k2+10)) ) return  1;
		if ( (*(const uint64_t *)(k1+2)) < 
		     (*(const uint64_t *)(k2+2)) ) return -1;
		if ( (*(const uint64_t *)(k1+2)) > 
		     (*(const uint64_t *)(k2+2)) ) return  1;
		uint16_t k1n0 = ((*(uint16_t*)(k1)) & ~0x01UL);
		uint16_t k2n0 = ((*(uint16_t*)(k2)) & ~0x01UL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	if ( keySize == 24 ) { 
		if ( (*(const uint64_t *)(k1+16)) < 
		     (*(const uint64_t *)(k2+16)) ) return -1;
		if ( (*(const uint64_t *)(k1+16)) > 
		     (*(const uint64_t *)(k2+16)) ) return  1;
		if ( (*(const uint64_t *)(k1+8)) < 
		     (*(const uint64_t *)(k2+8)) ) return -1;
		if ( (*(const uint64_t *)(k1+8)) > 
		     (*(const uint64_t *)(k2+8)) ) return  1;
		uint64_t k1n0 = 
			((*(uint64_t*)(k1)) & ~0x01ULL);
		uint64_t k2n0 = 
			((*(uint64_t*)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// linkdb
	if ( keySize == 28 ) { 
		if ( (*(const uint64_t *)(k1+20)) < 
		     (*(const uint64_t *)(k2+20)) ) return -1;
		if ( (*(const uint64_t *)(k1+20)) > 
		     (*(const uint64_t *)(k2+20)) ) return  1;
		if ( (*(const uint64_t *)(k1+12)) < 
		     (*(const uint64_t *)(k2+12)) ) return -1;
		if ( (*(const uint64_t *)(k1+12)) > 
		     (*(const uint64_t *)(k2+12)) ) return  1;
		if ( (*(const uint64_t *)(k1+4)) < 
		     (*(const uint64_t *)(k2+4)) ) return -1;
		if ( (*(const uint64_t *)(k1+4)) > 
		     (*(const uint64_t *)(k2+4)) ) return  1;
		uint64_t k1n0 = 
			((*(const uint32_t *)(k1)) & ~0x01ULL);
		uint64_t k2n0 = 
			((*(const uint32_t *)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	if ( keySize == 12 ) { 
		if ( (*(const uint64_t *)(k1+4)) < 
		     (*(const uint64_t *)(k2+4)) ) return -1;
		if ( (*(const uint64_t *)(k1+4)) > 
		     (*(const uint64_t *)(k2+4)) ) return  1;
		uint32_t k1n0 = ((*(uint32_t*)(k1)) & ~0x01UL);
		uint32_t k2n0 = ((*(uint32_t*)(k2)) & ~0x01UL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// must be size of 16 then
	if ( keySize == 16 ) {
		if ( (*(const uint64_t *)(k1+8)) <
		     (*(const uint64_t *)(k2+8)) ) return -1;
		if ( (*(const uint64_t *)(k1+8)) >
		     (*(const uint64_t *)(k2+8)) ) return  1;
		uint64_t k1n0 = ((*(const uint64_t *)(k1)) & ~0x01ULL);
		uint64_t k2n0 = ((*(const uint64_t *)(k2)) & ~0x01ULL);
		if ( k1n0 < k2n0 ) return -1;
		if ( k1n0 > k2n0 ) return  1;
		return 0;
	}
	// allow half key comparison too
	if ( keySize == 6 ) {
		if ( (*(uint32_t  *)(k1+2)) <
		     (*(uint32_t  *)(k2+2)) ) return -1;
		if ( (*(uint32_t  *)(k1+2)) >
		     (*(uint32_t  *)(k2+2)) ) return  1;
		if ( (*(const uint16_t *)(k1+0)) <
		     (*(const uint16_t *)(k2+0)) ) return -1;
		if ( (*(const uint16_t *)(k1+0)) >
		     (*(const uint16_t *)(k2+0)) ) return  1;
		return 0;
	}
	gbshutdownAbort(true);
	return 0;
}


// shit, how was i supposed to know this is defined in sys/types.h...
#define key_t   u_int96_t

// or you can be less ambiguous with these types
typedef u_int96_t  key96_t;
typedef u_int96_t  uint96_t;
typedef u_int128_t key128_t;
typedef u_int128_t uint128_t;




static inline char *KEYSTR ( const void *vk , int32_t ks ) {
	const char *k = (char *)vk;
	static char tmp1[128];
	static char tmp2[128];
	static char s_flip = 0;
	char *tmp;
	if ( s_flip == 0 ) {
		tmp = tmp1;
		s_flip = 1;
	}
	else {
		tmp = tmp2;
		s_flip = 0;
	}
	char *s = tmp;
	*s++ = '0';
	*s++ = 'x';
	for ( const unsigned char *p = (const unsigned char *)k + ks - 1 ; 
	      p >= (const unsigned char *)k ; p-- ) {
		unsigned char v = *p >> 4;
		if ( v <= 9 ) *s++ = v + '0';
		else          *s++ = v - 10 + 'a';
		v = *p & 0x0f;
		if ( v <= 9 ) *s++ = v + '0';
		else          *s++ = v - 10 + 'a';
	}
	*s = '\0';
	return tmp;
}

static inline uint16_t KEY0 ( const char *k , int32_t ks ) {
	if ( ks == 18 ) return *(const uint16_t *)k;
	else { gbshutdownAbort(true); }
	return 0;
}

static inline int64_t KEY1 ( const char *k , char keySize ) {
	if ( keySize == 12 ) return *(const int32_t *)(k+8);
	if ( keySize == 18 ) return *(const int64_t *)(k+2);
	// otherwise, assume 16
	return *(int64_t *)(k+8);
}

static inline int64_t KEY2 ( const char *k , char keySize ) {
	if ( keySize == 18 ) return *(const int64_t *)(k+10);
	gbshutdownAbort(true);
	return 0;
}



static inline int64_t KEY0 ( const char *k ) {
	return *(const int64_t *)k;
}
static inline void KEYSET ( char *k1, const char *k2, char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		*(int16_t *)(k1  ) = *(const int16_t *)(k2  );
		*(int64_t *)(k1+2) = *(const int64_t *)(k2+2);
		*(int64_t *)(k1+10) = *(const int64_t *)(k2+10);
		return;
	}
	if ( keySize == 12 ) {
		*(int64_t *) k1    = *(const int64_t *) k2;
		*(int32_t      *)(k1+8) = *(const int32_t      *)(k2+8);
		return;
	}
	// otherwise, assume 16
	if ( keySize == 16 ) {
		*(int64_t *)(k1  ) = *(const int64_t *)(k2  );
		*(int64_t *)(k1+8) = *(const int64_t *)(k2+8);
		return;
	}
	if ( keySize == 24 ) {
		*(int64_t *)(k1  ) = *(const int64_t *)(k2  );
		*(int64_t *)(k1+8) = *(const int64_t *)(k2+8);
		*(int64_t *)(k1+16) = *(const int64_t *)(k2+16);
		return;
	}
	if ( keySize == 28 ) {
		*(int64_t *)(k1  ) = *(const int64_t *)(k2  );
		*(int64_t *)(k1+8) = *(const int64_t *)(k2+8);
		*(int64_t *)(k1+16) = *(const int64_t *)(k2+16);
		*(int32_t *)(k1+24) = *(const int32_t *)(k2+24);
		return;
	}
	if ( keySize == 8 ) {
		*(int64_t *)(k1  ) = *(const int64_t *)(k2  );
		return;
	}
	//if ( keySize == 4 ) {
	//	*(int32_t *)(k1  ) = *(const int32_t *)(k2  );
	//	return;
	//}
	gbshutdownAbort(true);
	return;
}

static inline bool KEYNEG ( const char *k, int32_t a, char keySize ) {
	// posdb
	if ( keySize == 18 ) {
		if ( (k[a*18] & 0x01) == 0x00 ) return true;
		return false;
	}
	if ( keySize == 12 ) {
		if ( (k[a*12] & 0x01) == 0x00 ) return true;
		return false;
	}
	// otherwise, assume 16 bytes
	if (keySize == 16 ) {
		if ( (k[a*16] & 0x01) == 0x00 ) return true;
		return false;
	}
	if ( keySize == 24 ) {
		if ( (k[a*24] & 0x01) == 0x00 ) return true;
		return false;
	}
	if ( keySize == 28 ) {
		if ( (k[a*28] & 0x01) == 0x00 ) return true;
		return false;
	}
	if ( keySize == 8 ) {
		if ( (k[a*8] & 0x01) == 0x00 ) return true;
		return false;
	}
	gbshutdownAbort(true);
	return false;
}

static inline bool KEYNEG ( const char *k ) {
	if ( (k[0] & 0x01) == 0x00 ) return true;
	return false;
}

static inline bool KEYNEG ( key_t k ) {
	if ( (k.n0 & 0x01) == 0x00 ) return true;
	return false;
}

static inline void KEYADD ( char *k , char keySize ) {
	// posdb
	if ( keySize == 18 ) { *((key144_t *)k) += (int32_t)1; return; }
	if ( keySize == 12 ) { *((key96_t  *)k) += (int32_t)1; return; }
	if ( keySize == 16 ) { *((key128_t *)k) += (int32_t)1; return; }
	if ( keySize == 8  ) { *((uint64_t *)k) += (int32_t)1; return; }
	if ( keySize == 24 ) { *((key192_t *)k) += (int32_t)1; return; }
	if ( keySize == 28 ) { *((key224_t *)k) += (int32_t)1; return; }
	gbshutdownAbort(true);
}

static inline void KEYSUB ( char *k , char keySize ) {
	if ( keySize == 18 ) { *((key144_t *)k) -= (int32_t)1; return; }
	if ( keySize == 12 ) { *((key96_t  *)k) -= (int32_t)1; return; }
	if ( keySize == 16 ) { *((key128_t *)k) -= (int32_t)1; return; }
	if ( keySize == 28 ) { *((key224_t *)k) -= (int32_t)1; return; }
	gbshutdownAbort(true);
}

static inline void KEYOR ( char *k , int32_t opor ) {
	*((uint32_t *)k) |= opor;
	//if ( keySize == 12 ) ((key12_t *)k)->n0 |= or;
	//else                 ((key16_t *)k)->n0 |= or;
}

static inline void KEYXOR ( char *k , int32_t opxor ) {
	*((uint32_t *)k) ^= opxor;
}

static inline void KEYMIN ( char *k, char keySize ) {
	memset ( k , 0 , keySize );
}

static inline void KEYMAX ( char *k, char keySize ) {
	for ( int32_t i = 0 ; i < keySize ; i++ ) k[i]=(char)0xff;
}

static inline const char *KEYMIN() { return  "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"
			 "\0\0\0\0"; }
static inline const char *KEYMAX() {
	static const int s_foo[] = {
		(int)0xffffffff ,
	        (int)0xffffffff ,
		(int)0xffffffff ,
		(int)0xffffffff ,
		(int)0xffffffff ,
		(int)0xffffffff ,
		(int)0xffffffff ,
		(int)0xffffffff
	};
	return (const char *)s_foo;
}


#endif // GB_TYPES_H
