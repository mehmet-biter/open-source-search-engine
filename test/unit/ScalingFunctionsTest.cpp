#include <gtest/gtest.h>
#include "ScalingFunctions.h"

TEST(ScalingFunctionTest, ScaleLinear) {
	EXPECT_EQ(  0, scale_linear(  0, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ(100, scale_linear( 10, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ( 50, scale_linear(  5, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ(  0, scale_linear( -4, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ(100, scale_linear( 15, 0.0,10.0, 0.0,100.0));
}

TEST(ScalingFunctionTest, ScaleQuadratic) {
	EXPECT_EQ(  0, scale_quadratic(  0, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ(100, scale_quadratic( 10, 0.0,10.0, 0.0,100.0));
	EXPECT_EQ(  1, scale_quadratic( 1.0, 1.0,2.0, 1.0,4.0));
	EXPECT_EQ(  4, scale_quadratic( 2.0, 1.0,2.0, 1.0,4.0));
	EXPECT_TRUE(scale_quadratic( 1.1, 1.0,2.0, 1.0,4.0) < 1.5);
	EXPECT_TRUE(scale_quadratic( 1.5, 1.0,2.0, 1.0,4.0) < 2.5);
	EXPECT_TRUE(scale_quadratic( 1.9, 1.0,2.0, 1.0,4.0) > 3);
}
