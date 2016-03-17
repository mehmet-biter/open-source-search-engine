#include "BitOperations.h"

//everything is inlined due to the use of __builtin_... functions of GCC/clang/...
//If your compiler doesn't have them you will have to roll your own

int BitOperations_one_external_symbol; //at least one external symbol is required by the C standard, and also shuts up flexelint about this issue.


#ifdef UNITTEST
#include <assert.h>
#include <stdio.h>

int main(void) {
	assert(getHighestLitBit((unsigned char)0x00)==0);
	assert(getHighestLitBit((unsigned char)0x01)==0);
	assert(getHighestLitBit((unsigned char)0x02)==1);
	assert(getHighestLitBit((unsigned char)0x04)==2);
	assert(getHighestLitBit((unsigned char)0xff)==7);

	assert(getHighestLitBit((uint16_t)0x0102)==8);
	assert(getHighestLitBit((uint16_t)0xf102)==15);
	assert(getHighestLitBit((uint16_t)0x0000)==0);

	assert(getHighestLitBitValue(0)==0);
	assert(getHighestLitBitValue(1)==1);
	assert(getHighestLitBitValue(2)==2);
	assert(getHighestLitBitValue(3)==2);
	assert(getHighestLitBitValue(4)==4);
	assert(getHighestLitBitValue(5)==4);
	assert(getHighestLitBitValue(6)==4);
	assert(getHighestLitBitValue(7)==4);
	assert(getHighestLitBitValue(8)==8);
	assert(getHighestLitBitValue(65535)==32768);
	assert(getHighestLitBitValue(65536)==65536);
	assert(getHighestLitBitValue(0x7FFFFFFFU)==0x40000000U);
	assert(getHighestLitBitValue(0x8FFFFFFFU)==0x80000000U);
	
	assert(getHighestLitBitValueLL((uint64_t)0ul)==(uint64_t)0ul);
	assert(getHighestLitBitValueLL((uint64_t)1ul)==(uint64_t)1ul);
	assert(getHighestLitBitValueLL((uint64_t)2ul)==(uint64_t)2ul);
	assert(getHighestLitBitValueLL((uint64_t)3ul)==(uint64_t)2ul);
	assert(getHighestLitBitValueLL((uint64_t)4ul)==(uint64_t)4ul);
	for(unsigned i=0; i<60; i++)
		assert(getHighestLitBitValueLL(((uint64_t)1ull)<<i) == ((uint64_t)1ull)<<i);
	
	assert(getNumBitsOn8(0x00)==0);
	assert(getNumBitsOn8(0x01)==1);
	assert(getNumBitsOn8(0x11)==2);
	assert(getNumBitsOn8(0x23)==3);
	assert(getNumBitsOn8(0xff)==8);

	assert(getNumBitsOn16(0x0000)==0);
	assert(getNumBitsOn16(0x0001)==1);
	assert(getNumBitsOn16(0x0100)==1);
	assert(getNumBitsOn16(0x0230)==3);
	assert(getNumBitsOn16(0xffff)==16);

	assert(getNumBitsOn32(0x00000000)==0);
	assert(getNumBitsOn32(0x00000001)==1);
	assert(getNumBitsOn32(0x00000100)==1);
	assert(getNumBitsOn32(0x00000230)==3);
	assert(getNumBitsOn32(0x00023000)==3);
	assert(getNumBitsOn32(0x02300000)==3);
	assert(getNumBitsOn32(0xffffffff)==32);

	assert(getNumBitsOn64(0x0000000000000000LL)==0);
	assert(getNumBitsOn64(0x0000000000000001LL)==1);
	assert(getNumBitsOn64(0x0000000000000230LL)==3);
	assert(getNumBitsOn64(0x0000000000023000LL)==3);
	assert(getNumBitsOn64(0x0000000002300000LL)==3);
	assert(getNumBitsOn64(0x0000000230000000LL)==3);
	assert(getNumBitsOn64(0x0000023000000000LL)==3);
	assert(getNumBitsOn64(0x0002300000000000LL)==3);
	assert(getNumBitsOn64(0x0230000000000000LL)==3);
	assert(getNumBitsOn64(0xffffffffffffffffLL)==64);

	static const unsigned char xbits[]={0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
	assert(getNumBitsOnX(xbits,0) == 0);
	assert(getNumBitsOnX(xbits,1) == 0);
	assert(getNumBitsOnX(xbits,2) == 1);
	assert(getNumBitsOnX(xbits,3) == 2);
	assert(getNumBitsOnX(xbits,4) == 4);
	assert(getNumBitsOnX(xbits,5) == 5);
	assert(getNumBitsOnX(xbits,6) == 7);
	assert(getNumBitsOnX(xbits,7) == 9);
	assert(getNumBitsOnX(xbits,8) ==12);
	assert(getNumBitsOnX(xbits,9) ==13);
	assert(getNumBitsOnX(xbits,10) == 15);
}
#endif
