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
TEST(ProbableDocIdTest, WholeURLs) {
	EXPECT_EQ(Docid::getProbableDocId("http://www.example.co.uk/snarf/foo.html"),252979708372);
	EXPECT_EQ(Docid::getProbableDocId("https://www.example.co.uk/snarf/foo.html"),150254485977);
	EXPECT_EQ(Docid::getProbableDocId("http://www.example.co.uk/foo.html"),161048396274);
	EXPECT_EQ(Docid::getProbableDocId("http://www.example.co.uk/snarf/foo.html?boo=bar"),59068187089);
	EXPECT_EQ(Docid::getProbableDocId("http://www.example.co.uk:8080/snarf/foo.html"),40110146042);
	EXPECT_EQ(Docid::getProbableDocId("http://www.example.co.uk"),131891560943);
	EXPECT_EQ(Docid::getProbableDocId("http://192.0.2.17/snarf/foo.html"),2217203697);
	EXPECT_EQ(Docid::getProbableDocId("http://192.0.2.17:8090/snarf/foo.html"),94908962801);
}
