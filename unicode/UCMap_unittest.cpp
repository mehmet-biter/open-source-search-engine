#include "UCMap.h"
#include "UCEnums.h"

#include <assert.h>

int main() {
	{
		UnicodeMaps::FullMap<bool> m;
		assert(m.empty());
		assert(m.size()==0);
		assert(m.lookup(17)==nullptr);
	}
	
	{
		UnicodeMaps::FullMap<Unicode::script_t> m;
		assert(m.load("unicode_scripts.dat"));
		assert(m.lookup2(0x0041)==Unicode::script_t::Latin); //A
		assert(m.lookup2(0x0061)==Unicode::script_t::Latin); //a
		assert(m.lookup2(0x00E6)==Unicode::script_t::Latin); //æ
		assert(m.lookup2(0x0153)==Unicode::script_t::Latin); //œ
		
		assert(m.lookup2(0x0030)==Unicode::script_t::Common); //0
		assert(m.lookup2(0x003A)==Unicode::script_t::Common); //:
		assert(m.lookup2(0x00A3)==Unicode::script_t::Common); //£
		
		assert(m.lookup2(0x0391)==Unicode::script_t::Greek); //Α
		assert(m.lookup2(0x03A9)==Unicode::script_t::Greek); //Ω
		assert(m.lookup2(0x03C9)==Unicode::script_t::Greek); //ω
		
		assert(m.lookup2(0x0400)==Unicode::script_t::Cyrillic);
		assert(m.lookup2(0x0420)==Unicode::script_t::Cyrillic);
		assert(m.lookup2(0x04DD)==Unicode::script_t::Cyrillic);
		
		assert(m.lookup2(0x0910)==Unicode::script_t::Devanagari);
		assert(m.lookup2(0x0930)==Unicode::script_t::Devanagari);
		assert(m.lookup2(0x0939)==Unicode::script_t::Devanagari);
		
		assert(m.lookup2(0x1E00)==Unicode::script_t::Latin); //Ḁ
	}
	
	{
		UnicodeMaps::FullMap<Unicode::general_category_t> m;
		assert(m.load("unicode_general_categories.dat"));
		assert(m.lookup2('A')==Unicode::general_category_t::Lu);
		assert(m.lookup2('Z')==Unicode::general_category_t::Lu);
		assert(m.lookup2('a')==Unicode::general_category_t::Ll);
		assert(m.lookup2('z')==Unicode::general_category_t::Ll);
		assert(m.lookup2(0x00C6)==Unicode::general_category_t::Lu); //Æ
		assert(m.lookup2(0x00E6)==Unicode::general_category_t::Ll); //æ
		assert(m.lookup2(0x0391)==Unicode::general_category_t::Lu); //Α
		assert(m.lookup2(0x03B1)==Unicode::general_category_t::Ll); //α
		assert(m.lookup2(0x01C8)==Unicode::general_category_t::Lt); //ǈ
		assert(m.lookup2('0')==Unicode::general_category_t::Nd);
		assert(m.lookup2('!')==Unicode::general_category_t::Po);
		assert(m.lookup2('-')==Unicode::general_category_t::Pd);
		assert(m.lookup2(0x2010)==Unicode::general_category_t::Pd);
	}
	
	{
		UnicodeMaps::FullMap<uint32_t> m;
		assert(m.load("unicode_properties.dat"));
		assert(m.lookup2('0')&Unicode::ASCII_Hex_Digit);
		assert(m.lookup2('A')&Unicode::ASCII_Hex_Digit);
		assert(m.lookup2('-')&Unicode::Hyphen);
		assert(m.lookup2(' ')&Unicode::White_Space);
		assert(m.lookup2(0x00A0)&Unicode::White_Space); //nbsp
		assert(m.lookup2(0x2115)&Unicode::Other_Math); //ℕ
	}
	
	{
		UnicodeMaps::FullMap<bool> m;
		assert(m.load("unicode_wordchars.dat"));
		assert(m.lookup2('A'));
		assert(m.lookup2('Z'));
		assert(m.lookup2('a'));
		assert(m.lookup2('z'));
		assert(m.lookup2('0'));
		assert(m.lookup2('9'));
		assert(m.lookup2(0x00E6)); //æ
		assert(m.lookup2(0x0391)); //Α
		assert(m.lookup2(0x03A9)); //Ω 03A9;GREEK CAPITAL LETTER OMEGA
		assert(!m.lookup2(' '));
		assert(!m.lookup2('`'));
		assert(!m.lookup2(0x00A4)); //¤
		assert(!m.lookup2(0x2031)); //‱
		assert(!m.lookup2(0x2289)); //⊉
		assert(!m.lookup2(0x2327)); //⌧
		assert(!m.lookup2('_')); //underscore/low-line
		assert(!m.lookup2(0x00B7)); //00B7;MIDDLE DOT
		assert(!m.lookup2(0x201c)); //201C;LEFT DOUBLE QUOTATION MARK
		assert(m.lookup2(0x2126)); //Ω 2126;OHM SIGN
		assert(!m.lookup2(0x2121)); //℡ 2121;TELEPHONE SIGN
		
		assert(m.lookup2(0x00B2)); //00B2;SUPERSCRIPT TWO
		assert(m.lookup2(0x2070)); //2070;SUPERSCRIPT ZERO
		assert(!m.lookup2(0x207D)); //207D;SUPERSCRIPT LEFT PARENTHESIS
		assert(m.lookup2(0x2082)); //2082;SUBSCRIPT TWO
	}
	
	{
		UnicodeMaps::SparseMap<UChar32> m;
		assert(m.load("unicode_to_lowercase.dat"));
		{
			//non-alphabetic codepoints and already-lowercase codepoints should not be in the map at all
			assert(!m.lookup('@'));
			assert(!m.lookup('0'));
			assert(!m.lookup('a'));
		}
		{
			auto e = m.lookup('A');
			assert(e);
			assert(e->count==1);
			assert(e->values[0]=='a');
		}
		{
			auto e = m.lookup('Z');
			assert(e);
			assert(e->count==1);
			assert(e->values[0]=='z');
		}
		{
			auto e = m.lookup(0x00C6); //Æ
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x00E6); //æ
		}
		{
			//lowercase i with dot (we don't want turkish case mapping)
			auto e = m.lookup('I'); //I
			assert(e);
			assert(e->count==1);
			assert(e->values[0]=='i');
		}
		{
			//german sharp-s is already lowercase
			assert(!m.lookup(0x00DF)); //ß
		}
		{
			//german sharp-s uppercase is rarely used, but if we see it then its lowercase version is 0x00DF
			auto e = m.lookup(0x1E9E); //ẞ
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x00DF);
		}
		{
			auto e = m.lookup(0x0410); //А (cyrillic)
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x0430); //а
		}
		{
			auto e = m.lookup(0x0553); //Փ (armenian)
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x0583); //փ
		}
		{
			auto e = m.lookup(0x1E00); //Ḁ (LATIN CAPITAL LETTER A WITH RING BELOW)
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x1E01); //ḁ
		}
		{
			//titlecase codepoints should also work
			auto e = m.lookup(0x01F2); //ǲ (LATIN CAPITAL LETTER D WITH SMALL LETTER Z)
			assert(e);
			assert(e->count==1);
			assert(e->values[0]==0x01F3); //ǳ
		}
	}
	
	{
		UnicodeMaps::SparseMap<UChar32> m;
		assert(m.load("unicode_canonical_decomposition.dat"));
		assert(!m.lookup('0'));
		assert(!m.lookup('A'));
		assert(!m.lookup('a'));
		
		auto e0 = m.lookup(0x00C0); //À LATIN CAPITAL LETTER A WITH GRAVE
		assert(e0);
		assert(e0->count==2);
		assert(e0->values[0]==0x0041);
		assert(e0->values[1]==0x0300);
		
		//hmm. we should probably also be able to decompose ligatures, eg U0133 ĳ
	}
}
