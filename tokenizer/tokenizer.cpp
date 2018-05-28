#include "tokenizer.h"
#include <string.h>
#include "UCMaps.h"
#include "utf8_fast.h"


static bool is_word_script(Unicode::script_t s);

//How we tokenize:
//The string is split into <alfanum> and <not-alfanum> substrings
//Sounds simple. Except...
//The tokens are normally alternating between alfanum and not-alfanum, but if a substring changes script
//in the middle then we split there. It is complicated by the special script values:
//  common:	used in multiple scripts, eg digits 0-9, but also eg. thai currency symbol
//  inherit:	has the script of the preceding character. This is normally a decomposed diacritic or combining mark
void plain_tokenizer_phase_1(const char *str, size_t len, TokenizerResult *tr) {
	plain_tokenizer_phase_1_downcall(str,len,0,tr);
}

void plain_tokenizer_phase_1_downcall(const char *str, size_t len, size_t pos_base, TokenizerResult *tr) {
	for(size_t i = 0; i<len; ) {
		UChar32 c = utf8Decode(str+i);
		bool in_alnum_token = ucIsWordChar_fast(c);
		Unicode::script_t current_token_script = UnicodeMaps::query_script(c);
		
		std::string::size_type j;
		for(j = i + getUtf8CharSize(str+i); j<len; ) {
			UChar32 c = utf8Decode(str+j);
			if(!is_ignorable_fast(c)) {
				bool char_alnum = ucIsWordChar_fast(c);
				if(char_alnum!=in_alnum_token)
					break;
				Unicode::script_t char_script = UnicodeMaps::query_script(c);
				if(char_alnum && !is_word_script(char_script))
					break;
				if(char_script!=current_token_script) {
					if(char_script==Unicode::script_t::Inherited)
						; //fine, effectively same script, continue token
					else if(char_script==Unicode::script_t::Common)
						; //fine, continue token
					else if(current_token_script==Unicode::script_t::Common)
						current_token_script = char_script; //sticky, continue token
					else
						break;
				}
			} else {
				//codepoint is ignorable. so we do that.
			}
			j += getUtf8CharSize(str+j);
		}
		//found token [i..j)
		tr->tokens.emplace_back(pos_base+i,pos_base+j, str+i,j-i, true, in_alnum_token);
		i = j;
	}
}


//Is the script a script where words are separated?
//Eg. Latin/Coptic/Arabic/Hebrew normally are, while Hiragana/Korean/Thai normally aren't
//Then there are oddballs where spaces are optional, or ancient forms where spaces weren't
//used (but normally rendered with spaces in papers)
//Based partially on https://r12a.github.io/scripts/featurelist/
static bool is_word_script(Unicode::script_t s) {
	switch(s) {
		case Unicode::script_t::Arabic: return true;
		case Unicode::script_t::Armenian: return true;
		case Unicode::script_t::Bengali: return true;
		case Unicode::script_t::Buginese: return true;
		case Unicode::script_t::Canadian_Aboriginal: return true;
		case Unicode::script_t::Cherokee: return true;
		case Unicode::script_t::Coptic: return true;
		case Unicode::script_t::Cyrillic: return true;
		case Unicode::script_t::Devanagari: return true;
		case Unicode::script_t::Ethiopic: return true; //in modern texts
		case Unicode::script_t::Georgian: return true;
		case Unicode::script_t::Greek: return true;
		case Unicode::script_t::Gujarati: return true;
		case Unicode::script_t::Gurmukhi: return true;
		case Unicode::script_t::Hangul: return true;
		case Unicode::script_t::Hebrew: return true;
		case Unicode::script_t::Kannada: return true;
		case Unicode::script_t::Latin: return true;
		case Unicode::script_t::Malayalam: return true;
		case Unicode::script_t::Mandaic: return true;
		case Unicode::script_t::Mongolian: return true;
		case Unicode::script_t::Nko: return true;
		case Unicode::script_t::Runic: return true; //in modern rendition
		case Unicode::script_t::Sinhala: return true;
		case Unicode::script_t::Sundanese: return true;
		case Unicode::script_t::Syriac: return true;
		case Unicode::script_t::Tamil: return true;
		case Unicode::script_t::Telugu: return true;
		case Unicode::script_t::Thaana: return true;
		case Unicode::script_t::Tifinagh: return true;
		
		case Unicode::script_t::Balinese: return false;
		case Unicode::script_t::Han: return false; //chinese/kanji
		case Unicode::script_t::Hiragana: return false;
		case Unicode::script_t::Javanese: return false;
		case Unicode::script_t::Katakana: return false;
		case Unicode::script_t::Khmer: return false;
		case Unicode::script_t::Lao: return false;
		case Unicode::script_t::Myanmar: return false;
		case Unicode::script_t::Thai: return false; //only phrases and sentences are. But we could do better here as there are rules for word boundaries
		case Unicode::script_t::Tibetan: return false;
		
		default:
			//Best guess: yes. Most of the current 142 scripts used in unicode are ancient scripts and probably rendered with spaces in modern papers
			//If you know better then you are welcome to classify all 142 of them.
			return true;
	}
}
