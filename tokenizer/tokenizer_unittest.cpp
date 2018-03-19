#include "tokenizer.h"
#include "UCMaps.h"
#include <string.h>
#include "utf8_fast.h"
#include <assert.h>


class T1 {
	const char *s;
	TokenizerResult tr;
public:
	   T1(const char *str_)
	  : s(str_)
	{
		plain_tokenizer_phase_1(s,strlen(s),&tr);
		printf("tokens: %u\n", (unsigned)tr.size());
		for(unsigned i=0; i<tr.size(); i++)
			printf("  #%u: [%lu..%lu) '%.*s'\n", i, tr[i].start_pos, tr[i].end_pos, (int)tr[i].token_len, tr[i].token_start);
	}
	bool empty() const { return tr.empty(); }
	size_t token_count() const { return tr.size(); }
	std::string str(int i) const {
		return std::string(tr[i].token_start, tr[i].token_start+tr[i].token_len);
	}
	const TokenRange& token(int i) const { return tr[i]; }
};


class T2 {
	const char *s;
	TokenizerResult tr;
public:
	   T2(const char *str_, lang_t lang, const char *country_code=0)
	  : s(str_)
	{
		plain_tokenizer_phase_1(s,strlen(s),&tr);
		printf("phase1-tokens: %u\n", (unsigned)tr.size());
		for(unsigned i=0; i<tr.size(); i++)
			printf("  #%u: [%lu..%lu) '%.*s'\n", i, tr[i].start_pos, tr[i].end_pos, (int)tr[i].token_len, tr[i].token_start);
		size_t p1tokens = tr.size();
		plain_tokenizer_phase_2(lang,country_code,&tr);
		printf("phase2-tokens: %u\n", (unsigned)(tr.size()-p1tokens));
		for(unsigned i=p1tokens; i<tr.size(); i++)
			printf("  #%u: [%lu..%lu) '%.*s'\n", i, tr[i].start_pos, tr[i].end_pos, (int)tr[i].token_len, tr[i].token_start);
	}
	bool empty() const { return tr.empty(); }
	size_t token_count() const { return tr.size(); }
	std::string str(int i) const {
		return std::string(tr[i].token_start, tr[i].token_start+tr[i].token_len);
	}
	const TokenRange& token(int i) const { return tr[i]; }

	bool has_token(const char *s) const {
		size_t sl = strlen(s);
		for(size_t i=0; i<tr.tokens.size(); i++) {
			const auto &t = tr.tokens[i];
			if(t.token_len==sl && memcmp(t.token_start,s,sl)==0)
				return true;
		}
		return false;
	}
};



int main(void) {
	UnicodeMaps::load_maps("../ucdata/",0);
	printf("Test line %d\n",__LINE__);
	{
		T1 t("");
		assert(t.empty());
		assert(t.token_count()==0);
	}
	printf("Test line %d\n",__LINE__);
	{
		static const char two_spaces[] = "  ";
		T1 t(two_spaces);
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==2);
		assert(t.token(0).token_start==two_spaces);
		assert(t.token(0).token_len==2);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("a");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("abc");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==3);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("abc ");
		assert(!t.empty());
		assert(t.token_count()==2);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==3);
		assert(t.token(1).start_pos==3);
		assert(t.token(1).end_pos==4);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t(" abc");
		assert(!t.empty());
		assert(t.token_count()==2);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==1);
		assert(t.token(1).start_pos==1);
		assert(t.token(1).end_pos==4);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t(" abc ");
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==1);
		assert(t.token(1).start_pos==1);
		assert(t.token(1).end_pos==4);
		assert(t.token(2).start_pos==4);
		assert(t.token(2).end_pos==5);
	}
	printf("Test line %d\n",__LINE__);
	{
		static const char two_words[] = "abc def";
		T1 t(two_words);
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==3);
		assert(t.token(0).token_start==two_words+0);
		assert(t.token(0).token_len==3);
		assert(t.token(0).is_alfanum);
		assert(t.token(1).start_pos==3);
		assert(t.token(1).end_pos==4);
		assert(t.token(1).token_start==two_words+3);
		assert(t.token(1).token_len==1);
		assert(!t.token(1).is_alfanum);
		assert(t.token(2).start_pos==4);
		assert(t.token(2).end_pos==7);
		assert(t.token(2).token_start==two_words+4);
		assert(t.token(2).token_len==3);
		assert(t.token(2).is_alfanum);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t(".");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t(";:");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==2);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("; ;");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==3);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("abc;def:ghj?");
		assert(!t.empty());
		assert(t.token_count()==6);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==3);
		assert(t.token(1).start_pos==3);
		assert(t.token(1).end_pos==4);
		assert(t.token(2).start_pos==4);
		assert(t.token(2).end_pos==7);
		assert(t.token(3).start_pos==7);
		assert(t.token(3).end_pos==8);
		assert(t.token(4).start_pos==8);
		assert(t.token(4).end_pos==11);
		assert(t.token(5).start_pos==11);
		assert(t.token(5).end_pos==12);
	}
	
	//non-english latin letters
	printf("Test line %d\n",__LINE__);
	{
		T1 t("Æbleflæsk");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.token(0).start_pos==0);
		assert(t.token(0).end_pos==11);
	}
	//no detailed position/range checks from now on
	printf("Test line %d\n",__LINE__);
	{
		T1 t("holde øje");
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="holde");
		assert(t.str(1)==" ");
		assert(t.str(2)=="øje");
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("Ærø");
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("abćd efǿ");
		assert(!t.empty());
		assert(t.token_count()==3);
	}
	
	//mixed scripts
	printf("Test line %d\n",__LINE__);
	{
		T1 t("abcЀЁЂ");		//latin-cyrillic
		assert(!t.empty());
		assert(t.token_count()==2);
		assert(t.str(0)=="abc");
		assert(t.str(1)=="ЀЁЂ");
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("123Ђ");		//common-cyrillic
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("123ЀЁЂ");		//common-cyrillic (with rare diacritics)
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("ЀЁЂ123");		//cyrillic-common
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("ЀЁЂ123abc");	//cyrillic-common-latin
		assert(!t.empty());
		assert(t.token_count()==2);
		assert(t.str(0)=="ЀЁЂ123");
		assert(t.str(1)=="abc");
	}
	printf("Test line %d\n",__LINE__);
	{
		T1 t("04ДФ005");		//common-cyrillic-common
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.str(0)=="04ДФ005");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T1 t("25Ω");		//common/greek
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.str(0)=="25Ω");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T1 t("25Ω");		//common/common (ohm sign)
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.str(0)=="25Ω");
	}
	
	//combining diacritics and change in script, more specifically combining acute accent U0301 with latin and cyrillic
	printf("Test line %d\n",__LINE__);
	{
		char buf[128];
		int i=0;
		i += utf8Encode(0x0065, buf+i);	//u0065 latin small e
		i += utf8Encode(0x0301, buf+i);	//u0301 combining acute accent
						//  = é 
		i += utf8Encode(0x0418, buf+i);	//u0418 И
		i += utf8Encode(0x0306, buf+i);	//u0306 combining breve accent
						//  = Й (u0419)
		buf[i] = '\0';
		T1 t(buf);
		assert(!t.empty());
		assert(t.token_count()==2);
	}
	
	//hyphenation
	printf("Test line %d\n",__LINE__);
	{
		T1 t("cd-rom"); //with U+002D hyphen-minus
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="cd");
		assert(t.str(1)=="-");
		assert(t.str(2)=="rom");
	}

	printf("Test line %d\n",__LINE__);
	{
		T1 t("cd­rom"); //with U+00AD soft hyphen
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.str(0)=="cd­rom");
	}

	printf("Test line %d\n",__LINE__);
	{
		T1 t("cd‐rom"); //with U+2010 hyphen
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="cd");
		assert(t.str(1)=="‐");
		assert(t.str(2)=="rom");
	}

	printf("Test line %d\n",__LINE__);
	{
		T1 t("up‧ward"); //with U+2027 hyphenation point
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="up");
		assert(t.str(1)=="‧");
		assert(t.str(2)=="ward");
	}

	printf("Test line %d\n",__LINE__);
	{
		T1 t("be⁞ostær"); //with U+205E four dots - used by dictionaries to show undesirable word breaks.
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="be");
		assert(t.str(1)=="⁞");
		assert(t.str(2)=="ostær");
	}

	//scripts without word separation
	printf("Test line %d\n",__LINE__);
	{
		T1 t("ᬅᬆᬇ");
		assert(!t.empty());
		assert(t.token_count()==3);
	}

	//subscript
	printf("Test line %d\n",__LINE__);
	{
		T1 t("H₂O"); //water
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T1 t("H₂O₂"); //hydrogen peroxide
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T1 t("H₂SO₄"); //sulphuric acid
		assert(!t.empty());
		assert(t.token_count()==1);
	}
	
	//superscript
	printf("Test line %d\n",__LINE__);
	{
		T1 t("E=mc²");
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="E");
		assert(t.str(1)=="=");
		assert(t.str(2)=="mc²");
	}
	printf("Test line %d\n",__LINE__);
	
	{
		T1 t("j*=σT⁴");
		assert(!t.empty());
		assert(t.token_count()==4);
		assert(t.str(0)=="j");
		assert(t.str(1)=="*=");
		assert(t.str(2)=="σ");
		assert(t.str(3)=="T⁴");
	}
	
	//nbsp
	printf("Test line %d\n",__LINE__);
	{
		T1 t("aaa bbb");
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="aaa");
		assert(t.str(1)==" ");
		assert(t.str(2)=="bbb");
	}
	
	//zero-width space
	printf("Test line %d\n",__LINE__);
	{
		T1 t("aaa​bbb");
		assert(!t.empty());
		assert(t.token_count()==1);
		assert(t.str(0)=="aaa​bbb");
	}
//	øhm. skal zerowidth space virkelige være del af et ord?
	
	
	//////////////////////////////////////////////////////////////////////////////
	// plain-phase2 tests
	
	//basic
	printf("Test line %d\n",__LINE__);
	{
		T2 t("",langUnknown);
		assert(t.empty());
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("aaa bbb",langUnknown);
		assert(t.token_count()==3);
	}
	
	//general ligatures
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Vrĳdag",langUnknown);
		assert(t.token_count()==2);
		assert(t.str(1)=="Vrijdag");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Kerfuﬄe",langUnknown);
		assert(t.token_count()==2);
		assert(t.str(1)=="Kerfuffle");
	}
	
	//english-specific ligatures
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Encyclopædia",langEnglish);
		assert(t.token_count()==3);
		assert(t.str(0)=="Encyclopædia");
		assert(t.str(1)=="Encyclopaedia" || t.str(1)=="Encyclopedia");
		assert(t.str(2)=="Encyclopaedia" || t.str(2)=="Encyclopedia");
	}
	//french-specific ligatures
	printf("Test line %d\n",__LINE__);
	{
		T2 t("bœuf",langFrench);
		assert(t.token_count()==2);
		assert(t.str(0)=="bœuf");
		assert(t.str(1)=="boeuf");
	}
	
	//danish-specific
	printf("Test line %d\n",__LINE__);
	{
		T2 t("aaa bbb",langDanish);
		assert(t.token_count()==3);
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Vesterallé",langDanish);
		assert(t.token_count()==2);
		assert(t.str(1)=="Vesteralle");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Müller",langDanish);
		assert(t.token_count()==1);
		assert(t.str(0)=="Müller");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Chloë",langDanish);
		assert(t.token_count()==2);
		assert(t.str(0)=="Chloë");
		assert(t.str(1)=="Chloe");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Škoda",langDanish);
		assert(t.token_count()==2);
		assert(t.str(0)=="Škoda");
		assert(t.str(1)=="Skoda");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Bûche de Noël",langDanish);
		assert(t.token_count()==7);
		assert(t.str(5)=="Buche");
		assert(t.str(6)=="Noel");
	}

	//diacritics hands-off
	printf("Test line %d\n",__LINE__);
	{
		T2 t("diaérèse française",langFrench); //note: does not actually contain a diaeresis :-)
		assert(!t.empty());
		assert(t.token_count()==3);
		assert(t.str(0)=="diaérèse");
		assert(t.str(1)==" ");
		assert(t.str(2)=="française");
	}

	//posessive-s
	printf("Test line %d\n",__LINE__);
	{
		T2 t("John's dog",langUnknown); //U+0027 Apostrophe
		assert(t.token_count()==5); //phase-2 removes the standalone 's' token
		assert(t.str(5)=="Johns");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("John`s dog",langUnknown); //U+0060 grave
		assert(t.token_count()==5);
		assert(t.str(5)=="Johns");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("John´s dog",langUnknown); //U+00B4 acute accent
		assert(t.token_count()==5);
		assert(t.str(5)=="Johns");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("John’s dog",langUnknown); //U+2019 Right single quotation mark
		assert(t.token_count()==5);
		assert(t.str(5)=="Johns");
		//according to unicode NamesList.txt this is actually the preferred codepoint. Uhm, okay....
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("John′s dog",langUnknown); //U+2032 Prime
		assert(t.token_count()==5);
		assert(t.str(5)=="Johns");
	}
	
	//hyphenation
	printf("Test line %d\n",__LINE__);
	{
		T2 t("cd-rom",langUnknown); //U+002D Hypen-minus
		assert(t.token_count()==4);
		assert(t.str(3)=="cdrom");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("cd -rom",langUnknown);
		assert(t.token_count()==3);
	}
	
	//test on soft-hypen is special because that is caught in other logic because it is default-ignorable codepoint
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("cd­rom",langUnknown); //U+00AD Soft hyphen
		assert(t.token_count()==1);
		assert(t.str(0)=="cd­rom"); //with soft hyphen
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("cd‐rom",langUnknown); //U+2010 Hypen
		assert(t.token_count()==4);
		assert(t.str(3)=="cdrom");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("cd‑rom",langUnknown); //U+2011 non-breaking hypen
		assert(t.token_count()==4);
		assert(t.str(3)=="cdrom");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("Newcastle-Upon-Tyne",langUnknown);
		assert(t.token_count()==8);
		assert(t.has_token("Newcastle"));
		assert(t.has_token("Upon"));
		assert(t.has_token("Tyne"));
		assert(t.has_token("NewcastleUpon"));
		assert(t.has_token("UponTyne"));
		assert(t.has_token("NewcastleUponTyne"));
	}
	
	// phone numbers
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langUnknown);
		assert(t.token_count()==11);
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langDanish);
		assert(t.token_count()==12);
		assert(t.str(11)=="70270431");
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langDanish); //U+00AD non-breaking space
		assert(t.token_count()==12);
		assert(t.str(11)=="70270431");
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langUnknown,"dk");
		assert(t.token_count()==12);
		assert(t.str(11)=="70270431");
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langUnknown,"it");
		assert(t.token_count()==11);
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("lottotallene 70 27 04 31 42 17 er fup",langDanish,"dk");
		assert(t.token_count()==17);
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("70 27 04 31",langDanish,"dk");
		assert(t.token_count()==8);
		assert(t.str(7)=="70270431");
	}

	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 70 27 04 31 boo",langNorwegian);
		assert(t.token_count()==12);
		assert(t.str(11)=="70270431");
	}

	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 04621 / 99 99 99 boo",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	{
		T2 t("04621 / 99 99 99 boo",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	{
		T2 t("foo 04621 / 99 99 99",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	{
		T2 t("foo 04621 / 99 99 99 99999",langGerman,"de");
		assert(!t.has_token("04621999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 0461 9999-9999 boo",langGerman,"de");
		assert(t.has_token("046199999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 04621-999999 boo",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 04621 999999 boo",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 04621 - 999 99 99 boo",langGerman,"de");
		assert(t.has_token("046219999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 04621 9999-99 boo",langGerman,"de");
		assert(t.has_token("04621999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 040/999.999-999 boo",langGerman,"de");
		assert(t.has_token("040999999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 49(0)40-999999-999 boo",langGerman,"de");
		assert(t.has_token("040999999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo (040) 99 99-99 99 boo",langGerman,"de");
		assert(t.has_token("04099999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 0341/999 99 99 boo",langGerman,"de");
		assert(t.has_token("03419999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo +49 89 9 999 999-9 boo",langGerman,"de");
		assert(t.has_token("08999999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo +49 (0)89/99 99 99-99 boo",langGerman,"de");
		assert(t.has_token("08999999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo +49 40 999999999 boo",langGerman,"de");
		assert(t.has_token("040999999999"));
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo 02 357 1113 1719 2327 boo",langGerman,"de");
		assert(!t.has_token("02357111317192327"));
		assert(!t.has_token("0235711131719"));
		assert(!t.has_token("023571113171"));
		assert(!t.has_token("02357111317"));
	}
	
	
	// subscript, phase 2
	printf("Test line %d\n",__LINE__);
	{
		T2 t("H₂O",langUnknown); //water
		assert(t.token_count()==2);
		assert(t.str(1)=="H2O");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("H₂O₂",langUnknown); //hydrogen peroxide
		assert(t.token_count()==2);
		assert(t.str(1)=="H2O2");
	}
	
	printf("Test line %d\n",__LINE__);
	{
		T2 t("H₂SO₄",langUnknown); //sulphuric acid
		assert(t.token_count()==2);
		assert(t.str(1)=="H2SO4");
	}

	// superscript, phase 2
	printf("Test line %d\n",__LINE__);
	{
		T2 t("foo²boo",langUnknown);
		assert(!t.empty());
		assert(t.token_count()==2);
		assert(t.str(0)=="foo²boo");
		assert(t.str(1)=="foo2boo");
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("E=mc²",langUnknown);
		assert(!t.empty());
		assert(t.token_count()==6);
		assert(t.str(0)=="E");
		assert(t.str(1)=="=");
		assert(t.str(2)=="mc²");
		assert(t.has_token("mc2"));
		assert(t.has_token("mc"));
		assert(t.has_token("2"));
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("j*=σT⁴",langUnknown);
		assert(!t.empty());
		assert(t.token_count()==7);
		assert(t.str(0)=="j");
		assert(t.str(1)=="*=");
		assert(t.str(2)=="σ");
		assert(t.str(3)=="T⁴");
		assert(t.has_token("T4"));
		assert(t.has_token("T"));
		assert(t.has_token("4"));
	}
	
	//ampersand
	printf("Test line %d\n",__LINE__);
	{
		T2 t("potato&carrot",langUnknown);
		assert(!t.empty());
		assert(t.token_count()==3);
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("potato&carrot",langUnknown,"us");
		assert(!t.empty());
		assert(t.token_count()==4);
		assert(t.str(3)=="and");
	}
	printf("Test line %d\n",__LINE__);
	{
		T2 t("kartoffel&gulerod",langDanish);
		assert(!t.empty());
		assert(t.token_count()==4);
		assert(t.str(3)=="og");
	}
	
	return 0;
}
