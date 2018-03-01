#include "tokenizer.h"
#include "UCMaps.h"
#include "UCDecompose.h"
#include "UCCMDecompose.h"
#include "utf8_fast.h"
#include "ligature_decomposition.h"
#include <string.h>


static const size_t max_word_codepoints = 128; //longest word we will consider working on

static void decompose_stylistic_ligatures(TokenizerResult *tr);
static void remove_combining_marks(TokenizerResult *tr, lang_t lang);


//pass 2 tokenizer / language-dependent tokenization
//When we know the language there may be tokens that were split into multiple tokes by tokenize() but can
//be considered a whole token. Eg. contractions, hyphenation, and other oddball stuff.
//If we furthermore knew the locale then we could recognize numbers, phone numbers, post codes. But we
//don't know the locale with certainty, so we take the language as a hint.
//Also joins words separated with hyphen (all 10 of them)
void plain_tokenizer_phase_2(const char * /*str*/, size_t /*len*/, lang_t lang, TokenizerResult *tr) {
	decompose_stylistic_ligatures(tr);
	//TODO: language-specific ligatures
	remove_combining_marks(tr,lang);
	//TODO: chemical formulae
	//TODO: subscript
	//TODO: superscript
	//possessive-s

}


static void decompose_stylistic_ligatures(TokenizerResult *tr) {
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		const auto &token = (*tr)[i];
		if(!token.is_alfanum)
			continue;
		UChar32 uc_org_token[max_word_codepoints];
		if(token.token_len > sizeof(uc_org_token)/4)
			continue; //Don't decompose ligatures in words longer than <max_word_codepoints> characters
		int org_codepoints = decode_utf8_string(token.token_start,token.token_len,uc_org_token);
		if(org_codepoints<=0)
			continue; //decode error or empty token
		UChar32 uc_new_token[max_word_codepoints*3];
		int new_codepoints=0;
		for(int i=0; i<org_codepoints; i++) {
			unsigned decomposed_ligature_codepoints = decompose_stylistic_ligature(uc_org_token[i], uc_new_token+new_codepoints);
			if(decomposed_ligature_codepoints==0) //not a decomposable ligature codepoint
				uc_new_token[new_codepoints++] = uc_org_token[i];
			else
				new_codepoints += decomposed_ligature_codepoints;
		}
		if(new_codepoints!=org_codepoints) {
			char new_token_utf8[max_word_codepoints*4];
			size_t new_token_utf8_len = encode_utf8_string(uc_new_token,new_codepoints,new_token_utf8);
			char *s = (char*)tr->egstack.alloc(new_token_utf8_len+1);
			memcpy(s,new_token_utf8,new_token_utf8_len);
			s[new_token_utf8_len] = '\0';
			tr->tokens.emplace_back(token.start_pos,token.end_pos, s,new_token_utf8_len, true);
		}
	}
}


static void remove_combining_marks_danish(TokenizerResult *tr);


static void remove_combining_marks(TokenizerResult *tr, lang_t lang) {
	switch(lang) {
		case langDanish:
			remove_combining_marks_danish(tr);
			return;
		default:
			break;
	}
}


//Combining marks used in Danish:
//  - ring-above	(Å/å)		Mandatory
//  - umlaut		(äüö)		Well-known and easily accessible. In words from Swedish or German
//  - acute-accent	(allé)		Optional, used for stress marking, or in French loanwords.

static void remove_combining_marks_danish(TokenizerResult *tr) {
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		const auto &token = (*tr)[i];
		if(!token.is_alfanum)
			continue;
		UChar32 uc_org_token[max_word_codepoints];
		if(token.token_len > sizeof(uc_org_token)/4)
			continue; //Don't decompose ligatures in words longer than <max_word_codepoints> characters
		int org_codepoints = decode_utf8_string(token.token_start,token.token_len,uc_org_token);
		if(org_codepoints<=0)
			continue; //decode error or empty token
		UChar32 uc_new_token[max_word_codepoints*3];
		int new_codepoints=0;
		bool any_combining_marks_removed = false;
		for(int i=0; i<org_codepoints; i++) {
			if(uc_org_token[i]==0x00C5 || //Å
			   uc_org_token[i]==0x00E5 || //å
			   uc_org_token[i]==0x00C4 || //Ä
			   uc_org_token[i]==0x00D6 || //Ö
			   uc_org_token[i]==0x00DC || //Ü
			   uc_org_token[i]==0x00E4 || //ä
			   uc_org_token[i]==0x00F6 || //ö
			   uc_org_token[i]==0x00FC)   //ü
			{
				uc_new_token[new_codepoints++] = uc_org_token[i];
				continue;
			}
			
			//decompose codepoint
			UChar32 tmp[16];
			unsigned tmpl = Unicode::recursive_combining_mark_decompose(uc_org_token[i], tmp, 16);
			if(tmpl==0) {
				uc_new_token[new_codepoints++] = uc_org_token[i];
				continue;
			}
			
			//strip unwanted marks
			bool modified = false;
			for(unsigned j=0; j<tmpl; j++) {
				//what we should do: check if the general_category of the codepoint is 'Mn' (non-spacing mark)
				//what we actually do: just check if the range/block is "Combining Diacritical Marks"
				//This will ignore cyrillic/hebrew/N'ko/arabic marks
				if(tmp[j]>=0x0300 && tmp[j]<=0x036F) {
					memmove(tmp+j,tmp+j+1,(tmpl-j)*4);
					tmpl--;
					modified = true;
				}
			}
			if(!modified) {
				uc_new_token[new_codepoints++] = uc_org_token[i];
				continue;
			}
			
			//compose the codepoint again 8if possible)
			UChar32 final[16];
			unsigned final_len = Unicode::iterative_combining_mark_compose(tmp,tmpl,final);
			
			memcpy(uc_new_token+new_codepoints, final, final_len*4);
			new_codepoints += final_len;
			any_combining_marks_removed = true;
		}
		if(any_combining_marks_removed) {
			char new_token_utf8[max_word_codepoints*4];
			size_t new_token_utf8_len = encode_utf8_string(uc_new_token,new_codepoints,new_token_utf8);
			char *s = (char*)tr->egstack.alloc(new_token_utf8_len+1);
			memcpy(s,new_token_utf8,new_token_utf8_len);
			s[new_token_utf8_len] = '\0';
			tr->tokens.emplace_back(token.start_pos,token.end_pos, s,new_token_utf8_len, true);
		}
	}
}
