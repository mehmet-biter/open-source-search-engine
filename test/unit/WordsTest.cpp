#include <gtest/gtest.h>

#include "Words.h"

TEST(WordsTest, VerifySize) {
	// set c to a curling quote in unicode
	int32_t c = 0x201c; // 0x235e;

	// encode it into utf8
	char dst[5];

	// point to it
	char *p = dst;

	// put space in there
	*p++ = ' ';

	// "numBytes" is how many bytes it stored into 'dst"
	int32_t numBytes = utf8Encode ( c , p );

	// must be 3 bytes
	EXPECT_EQ(3, numBytes);

	// check it
	int32_t size = getUtf8CharSize(p);
	EXPECT_EQ(3, size);

	// is that punct
	EXPECT_TRUE(is_punct_utf8(p));
}

TEST(WordsTest, simple_tokenization) {
	char buf[256];
	{
		strcpy(buf,"hello");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),1);
	}
	{
		strcpy(buf,"  ");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),1);
	}
	{
		strcpy(buf,"hello ");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),2);
	}
	{
		strcpy(buf," hello");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),2);
	}
	{
		strcpy(buf,"hello world");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),3);
	}
	{
		strcpy(buf,"Hello world!");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),4);
		EXPECT_EQ(words.getWordLen(0),5);
		EXPECT_EQ(words.getWordLen(1),1);
		EXPECT_EQ(words.getWordLen(2),5);
		EXPECT_EQ(words.getWordLen(3),1);
	}
	{
		strcpy(buf,"Hello, world");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),3);
		EXPECT_EQ(words.getWordLen(0),5);
		EXPECT_EQ(words.getWordLen(1),2);
		EXPECT_EQ(words.getWordLen(2),5);
	}
}

TEST(WordsTest, latin1_tokenization) {
	char buf[256];
	{
		strcpy(buf,"Æbleflæsk og øl");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),5);
		EXPECT_EQ(words.getWordLen(0),11);
		EXPECT_EQ(words.getWordLen(1),1);
		EXPECT_EQ(words.getWordLen(2),2);
		EXPECT_EQ(words.getWordLen(3),1);
		EXPECT_EQ(words.getWordLen(4),3);
	}
}

TEST(WordsTest, mixed_script_tokenization) {
	char buf[256];
	{
		strcpy(buf,"Æbleflæsk og γιαούρτι");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),5);
		EXPECT_EQ(words.getWordLen(0),11);
		EXPECT_EQ(words.getWordLen(1),1);
		EXPECT_EQ(words.getWordLen(2),2);
		EXPECT_EQ(words.getWordLen(3),1);
		EXPECT_EQ(words.getWordLen(4),16);
	}
	{
		strcpy(buf,"Æbleflæskγιαούρτι");
		Words words;
		EXPECT_TRUE(words.set(buf,false));
		EXPECT_EQ(words.getNumWords(),2);
		EXPECT_EQ(words.getWordLen(0),11);
		EXPECT_EQ(words.getWordLen(1),16);
	}
}
