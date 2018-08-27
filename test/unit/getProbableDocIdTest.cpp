#include <gtest/gtest.h>
#include "Docid.h"

TEST(ProbableDocIdTest, Simple) {
	EXPECT_EQ(Docid::getProbableDocId("www.example.net"),216725988828);
	EXPECT_EQ(Docid::getProbableDocId("www.example.com"),261371470594);
}


TEST(ProbableDocIdTest, Quirks) {
	EXPECT_EQ(Docid::getProbableDocId("abc.dk"),156767366042);
	EXPECT_EQ(Docid::getProbableDocId("www.a.very.long.domain.name.with.unknown.TLD.this-will-never-be-an-official-tld"),136857858271);
}


TEST(ProbableDocIdTest, TwoLevelTLD) {
	EXPECT_EQ(Docid::getProbableDocId("www.example.co.uk"),131891560943);
	EXPECT_EQ(Docid::getProbableDocId("www.example.dtu.dk"),187559601845);
	
	EXPECT_EQ(Docid::getProbableDocId("www.example.co.dk"),271301917664);
	EXPECT_EQ(Docid::getProbableDocId("www.example.dtu.ua"),202434669729);
}
