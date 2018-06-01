#include <gtest/gtest.h>
#include "Bits.h"
#include "tokenizer.h"
#include "TitleRecVersion.h"
#include "Xml.h"
#include "HttpMime.h"
#include <string.h>


TEST(BitsTest, basic) {
	static const char str[]="Hello large world.";
	TokenizerResult tr;
	plain_tokenizer_phase_1(str,strlen(str), &tr);
	ASSERT_EQ(tr.size(),6);
	
	Bits bits;
	ASSERT_TRUE(bits.setForSummary(&tr));
	EXPECT_TRUE(bits.m_swbits[0]&D_STARTS_SENTENCE);
	for(unsigned i=1; i<tr.size(); i++)
		EXPECT_FALSE(bits.m_swbits[i]&D_STARTS_SENTENCE);
	for(unsigned i=0; i<tr.size(); i++) {
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_QUOTES);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_TITLE);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_PARENTHESES);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_HYPERLINK);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_BOLDORITALICS);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_LIST);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_SUP);
		EXPECT_FALSE(bits.m_swbits[i]&D_IN_BLOCKQUOTE);
	}
}


TEST(BitsTest, two_sentences) {
	static const char str[]="Hello world. How are you today?";
	TokenizerResult tr;
	plain_tokenizer_phase_1(str,strlen(str), &tr);
	Bits bits;
	ASSERT_TRUE(bits.setForSummary(&tr));
	int num_sentence_starts = 0;
	for(unsigned i=0; i<tr.size(); i++)
		if(bits.m_swbits[i]&D_STARTS_SENTENCE)
			num_sentence_starts++;
	ASSERT_EQ(num_sentence_starts,2);
}


TEST(BitsTest, two_nonlatin_sentences) {
	static const char str[]="Γειά σου Κόσμε. Πώς είσαι σήμερα; Πρόστιμο;";
	TokenizerResult tr;
	plain_tokenizer_phase_1(str,strlen(str), &tr);
	Bits bits;
	ASSERT_TRUE(bits.setForSummary(&tr));
	int num_sentence_starts = 0;
	for(unsigned i=0; i<tr.size(); i++)
		if(bits.m_swbits[i]&D_STARTS_SENTENCE)
			num_sentence_starts++;
	ASSERT_EQ(num_sentence_starts,3);
}


TEST(BitsTest, parentheses) {
	static const char str[]="Hello (small and beautiful) world.";
	TokenizerResult tr;
	plain_tokenizer_phase_1(str,strlen(str), &tr);
	ASSERT_EQ(tr.size(),10);
	Bits bits;
	ASSERT_TRUE(bits.setForSummary(&tr));
	int num_sentence_starts = 0;
	int num_words_in_parentheses = 0;
	for(unsigned i=0; i<tr.size(); i++) {
		if(bits.m_swbits[i]&D_STARTS_SENTENCE)
			num_sentence_starts++;
		if(bits.m_swbits[i]&D_IN_PARENTHESES && tr[i].is_alfanum)
			num_words_in_parentheses++;
	}
	ASSERT_TRUE(num_sentence_starts>=1);
	ASSERT_EQ(num_words_in_parentheses,3);
}

TEST(BitsTest, simple_html) {
	static const char html[] = "<html><head><title>giraffe</title><body><p>Hello<sup>some conditions may apply</sup> world.</p>";
	Xml xml;
	ASSERT_TRUE(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
	TokenizerResult tr;
	xml_tokenizer_phase_1(&xml,&tr);
	Bits bits;
	ASSERT_TRUE(bits.setForSummary(&tr));
	int num_sentence_starts = 0;
	int num_words_in_sup = 0;
	for(unsigned i=0; i<tr.size(); i++) {
		if(bits.m_swbits[i]&D_STARTS_SENTENCE)
			num_sentence_starts++;
		if(bits.m_swbits[i]&D_IN_SUP && tr[i].is_alfanum)
			num_words_in_sup++;
	}
	ASSERT_TRUE(num_sentence_starts>=2);
	ASSERT_EQ(num_words_in_sup,4);
}
