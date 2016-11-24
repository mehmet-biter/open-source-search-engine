#include <gtest/gtest.h>
#include "SiteGetter.h"

TEST(SiteGetterTest, GetSite) {
	SiteGetter sg;
	EXPECT_TRUE(sg.getSite("http://dr.dk/", NULL, 0, 0, 0));
	EXPECT_STREQ("www.dr.dk", sg.getSite());
}