#include <gtest/gtest.h>
#include "BitOperations.h"

TEST(BitOperationsTest, GetHighestLitBit) {
	EXPECT_TRUE(getHighestLitBit((unsigned char)0x00)==0);
	EXPECT_TRUE(getHighestLitBit((unsigned char)0x01)==0);
	EXPECT_TRUE(getHighestLitBit((unsigned char)0x02)==1);
	EXPECT_TRUE(getHighestLitBit((unsigned char)0x04)==2);
	EXPECT_TRUE(getHighestLitBit((unsigned char)0xff)==7);

	EXPECT_TRUE(getHighestLitBit((uint16_t)0x0102)==8);
	EXPECT_TRUE(getHighestLitBit((uint16_t)0xf102)==15);
	EXPECT_TRUE(getHighestLitBit((uint16_t)0x0000)==0);
}

TEST(BitOperationsTest, GetHighestLitBitValue) {
	EXPECT_TRUE(getHighestLitBitValue(0)==0);
	EXPECT_TRUE(getHighestLitBitValue(1)==1);
	EXPECT_TRUE(getHighestLitBitValue(2)==2);
	EXPECT_TRUE(getHighestLitBitValue(3)==2);
	EXPECT_TRUE(getHighestLitBitValue(4)==4);
	EXPECT_TRUE(getHighestLitBitValue(5)==4);
	EXPECT_TRUE(getHighestLitBitValue(6)==4);
	EXPECT_TRUE(getHighestLitBitValue(7)==4);
	EXPECT_TRUE(getHighestLitBitValue(8)==8);
	EXPECT_TRUE(getHighestLitBitValue(65535)==32768);
	EXPECT_TRUE(getHighestLitBitValue(65536)==65536);
	EXPECT_TRUE(getHighestLitBitValue(0x7FFFFFFFU)==0x40000000U);
	EXPECT_TRUE(getHighestLitBitValue(0x8FFFFFFFU)==0x80000000U);
}

TEST(BitOperationsTest, GetHighestLitBitValueLL) {
	EXPECT_TRUE(getHighestLitBitValueLL((uint64_t)0ul)==(uint64_t)0ul);
	EXPECT_TRUE(getHighestLitBitValueLL((uint64_t)1ul)==(uint64_t)1ul);
	EXPECT_TRUE(getHighestLitBitValueLL((uint64_t)2ul)==(uint64_t)2ul);
	EXPECT_TRUE(getHighestLitBitValueLL((uint64_t)3ul)==(uint64_t)2ul);
	EXPECT_TRUE(getHighestLitBitValueLL((uint64_t)4ul)==(uint64_t)4ul);
	for(unsigned i=0; i<60; i++)
		EXPECT_TRUE(getHighestLitBitValueLL(((uint64_t)1ull)<<i) == ((uint64_t)1ull)<<i);
}

TEST(BitOperationsTest, GetNumBitsOn8) {
	EXPECT_TRUE(getNumBitsOn8(0x00)==0);
	EXPECT_TRUE(getNumBitsOn8(0x01)==1);
	EXPECT_TRUE(getNumBitsOn8(0x11)==2);
	EXPECT_TRUE(getNumBitsOn8(0x23)==3);
	EXPECT_TRUE(getNumBitsOn8(0xff)==8);
}

TEST(BitOperationsTest, GetNumBitsOn16) {
	EXPECT_TRUE(getNumBitsOn16(0x0000)==0);
	EXPECT_TRUE(getNumBitsOn16(0x0001)==1);
	EXPECT_TRUE(getNumBitsOn16(0x0100)==1);
	EXPECT_TRUE(getNumBitsOn16(0x0230)==3);
	EXPECT_TRUE(getNumBitsOn16(0xffff)==16);
}

TEST(BitOperationsTest, GetNumBitsOn32) {
	EXPECT_TRUE(getNumBitsOn32(0x00000000)==0);
	EXPECT_TRUE(getNumBitsOn32(0x00000001)==1);
	EXPECT_TRUE(getNumBitsOn32(0x00000100)==1);
	EXPECT_TRUE(getNumBitsOn32(0x00000230)==3);
	EXPECT_TRUE(getNumBitsOn32(0x00023000)==3);
	EXPECT_TRUE(getNumBitsOn32(0x02300000)==3);
	EXPECT_TRUE(getNumBitsOn32(0xffffffff)==32);
}

TEST(BitOperationsTest, GetNumBitsOn64) {
	EXPECT_TRUE(getNumBitsOn64(0x0000000000000000LL)==0);
	EXPECT_TRUE(getNumBitsOn64(0x0000000000000001LL)==1);
	EXPECT_TRUE(getNumBitsOn64(0x0000000000000230LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0000000000023000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0000000002300000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0000000230000000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0000023000000000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0002300000000000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0x0230000000000000LL)==3);
	EXPECT_TRUE(getNumBitsOn64(0xffffffffffffffffLL)==64);
}

TEST(BitOperationsTest, GetNumBitsOnX) {
	static const unsigned char xbits[]={0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
	EXPECT_TRUE(getNumBitsOnX(xbits,0) == 0);
	EXPECT_TRUE(getNumBitsOnX(xbits,1) == 0);
	EXPECT_TRUE(getNumBitsOnX(xbits,2) == 1);
	EXPECT_TRUE(getNumBitsOnX(xbits,3) == 2);
	EXPECT_TRUE(getNumBitsOnX(xbits,4) == 4);
	EXPECT_TRUE(getNumBitsOnX(xbits,5) == 5);
	EXPECT_TRUE(getNumBitsOnX(xbits,6) == 7);
	EXPECT_TRUE(getNumBitsOnX(xbits,7) == 9);
	EXPECT_TRUE(getNumBitsOnX(xbits,8) ==12);
	EXPECT_TRUE(getNumBitsOnX(xbits,9) ==13);
	EXPECT_TRUE(getNumBitsOnX(xbits,10) == 15);
}

