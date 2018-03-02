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
static void combine_possessive_s_tokens(TokenizerResult *tr, lang_t lang);
static void combine_hyphenated_words(TokenizerResult *tr);
static void recognize_telephone_numbers(TokenizerResult *tr, lang_t lang, const char *country_code);


//pass 2 tokenizer / language-dependent tokenization
//When we know the language there may be tokens that were split into multiple tokes by tokenize() but can
//be considered a whole token. Eg. contractions, hyphenation, and other oddball stuff.
//If we furthermore knew the locale then we could recognize numbers, phone numbers, post codes. But we
//don't know the locale with certainty, so we take the language as a hint.
//Also joins words separated with hyphen (all 10 of them)
void plain_tokenizer_phase_2(const char * /*str*/, size_t /*len*/, lang_t lang, const char *country_code, TokenizerResult *tr) {
	decompose_stylistic_ligatures(tr);
	//TODO: language-specific ligatures
	remove_combining_marks(tr,lang);
	combine_possessive_s_tokens(tr,lang);
	//TODO: chemical formulae
	//TODO: subscript
	//TODO: superscript
	combine_hyphenated_words(tr);
	recognize_telephone_numbers(tr,lang,country_code);

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


//////////////////////////////////////////////////////////////////////////////

//Join word-with-possessive-s into a single token
//Also take care of misused/abused other marks, such as modifier letters, prime marks, etc. Even in native English text
//the apostrophe is sometimes morphed into weird codepoints. So we take all codepoints whose glyphs look like a blotch
//that could conceivably stand in for apostrophe. We do this in all languages because the abuse seem to know no language barrier
static void combine_possessive_s_tokens(TokenizerResult *tr, lang_t /*lang*/) {
	//Loop through original tokens, looking for <word> <blotch> "s". Combine the word with the letter s.
	const size_t org_token_count = tr->size();
	for(size_t i=0; i+2<org_token_count; i++) {
		const auto &t0 = (*tr)[i];
		const auto &t1 = (*tr)[i+1];
		const auto &t2 = (*tr)[i+2];
		//must be word-nonword-word
		if(!t0.is_alfanum)
			continue;
		if(t1.is_alfanum)
			continue;
		if(!t2.is_alfanum)
			continue;
		//t2 must be "s"
		if(t2.token_len!=1 || *t2.token_start!='s')
			continue;
		//t1 must be a single blotch
		if(t1.token_len>4)
			continue;
		UChar32 uc[2];
		int ucs = decode_utf8_string(t1.token_start,t1.token_len,uc);
		if(ucs!=1)
			continue;
		if(uc[0]!=0x0027 && //APOSTROPHE
		   uc[0]!=0x0060 && //GRAVE ACCENT
		   uc[0]!=0x00B4 && //ACUTE ACCENT
		   uc[0]!=0x2018 && //LEFT SINGLE QUOTATION MARK
		   uc[0]!=0x2019 && //RIGHT SINGLE QUOTATION MARK
		   uc[0]!=0x201B && //SINGLE HIGH-REVERSED-9 QUOTATION MARK
		   uc[0]!=0x2032 && //PRIME
		   uc[0]!=0x2035)   //REVERSED PRIME
		continue;
		
		size_t combined_token_length = t0.token_len + 1;
		char *s = (char*)tr->egstack.alloc(combined_token_length);
		memcpy(s, t0.token_start, t0.token_len);
		s[t0.token_len] = 's';
		tr->tokens.emplace_back(t0.start_pos,t2.end_pos, s, combined_token_length, true);
	}
}

//note about above: We don't check for:
// 		   uc[0]!=0x02B9 && //MODIFIER LETTER PRIME
// 		   uc[0]!=0x02BB && //MODIFIER LETTER TURNED COMMA
// 		   uc[0]!=0x02BC && //MODIFIER LETTER APOSTROPHE
// 		   uc[0]!=0x02BD && //MODIFIER LETTER REVERSED COMMA
// 		   uc[0]!=0x02CA && //MODIFIER LETTER ACUTE ACCENT
// 		   uc[0]!=0x02CB && //MODIFIER LETTER GRAVE ACCENT
// 		   uc[0]!=0x02D9 && //DOT ABOVE
//because they are classifed as word-chars because they are used by IPA



//////////////////////////////////////////////////////////////////////////////
// Combining hyphenated words
//Eg. if the source text contains:
//	aaa-bbb-ccc ddd eee
//it will be level-1-tokenized into:
//	'aaa' '-' 'bbb' '-' 'ccc' ' ' 'ddd' ' ' 'eee'
//and the upper layer will generate the bigrams:
//	aaa bbb
//	bbb ccc
//	ccc ddd
//	ddd eee
//however, for better search results we'd like to get bigrams over more words, because in most languages hyphenation doesn't
//have hard rules. Eg. for "smurf cd-rom" we would liek the bigram "smurfcdrom". So we look for hyphenated words and treat them
//as a single word, allowign the upper layer to generate larger bigrams. One challange is if there are obscenely long hyphenated words,
//eg. some chemical components "1-Methyl-2-pyrrolidinone"
//should be generate all possible joins, or just prefix joins, or just suffix joins, og just a single join?
//We chose 2-grams and then the whole word.

static bool is_hyphen(const TokenRange &tr) {
	if(tr.token_len<1 || tr.token_len>4)
		return false;
	if(tr.token_len==1)
		return *tr.token_start == '-'; //002D Hypen-minus
	if(*tr.token_start < (char)0x80)
		return false;
	UChar32 uc[4];
	int codepoints = decode_utf8_string(tr.token_start,tr.token_len,uc);
	if(codepoints!=1)
		return false;
	return uc[0]==0x00AD || //Soft hypen
	       uc[0]==0x2010 || //Hypen
	       uc[0]==0x2011;   //Non-breaking hypen
}

static void combine_hyphenated_words(TokenizerResult *tr) {
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; ) {
		auto const &first_token = (*tr)[0];
		if(!first_token.is_alfanum) {
			i++;
			continue;
		}
		size_t j = i+1;
		for( ; j+1<org_token_count; ) {
			if(!is_hyphen((*tr)[j]))
				break;
			if(!(*tr)[j+1].is_alfanum)
				break;
			j += 2;
		}
		//we now have a potential range [i..j[
		if(j-i >= 3) {
			//we have multiple words
			//make bigram-joins
			for(size_t k=i; k+2<j; k+=2) {
				auto const &t0 = (*tr)[k];
				auto const &t1 = (*tr)[k+2];
				size_t sl = t0.token_len + t1.token_len;
				char *s = (char*)tr->egstack.alloc(sl);
				memcpy(s, t0.token_start, t0.token_len);
				memcpy(s+t0.token_len, t1.token_start, t1.token_len);
				tr->tokens.emplace_back(t0.start_pos, t1.end_pos, s, sl, true);
			}
			if(j-i > 3) {
				//make whole-join
				size_t sl=0;
				for(size_t k=i; k<j; k+=2)
					sl += (*tr)[k].token_len;
				char *s = (char*)tr->egstack.alloc(sl);
				char *p=s;
				for(size_t k=i; k<j; k+=2) {
					memcpy(p, (*tr)[k].token_start, (*tr)[k].token_len);
					p += (*tr)[k].token_len;
				}
				tr->tokens.emplace_back((*tr)[i].start_pos, (*tr)[j-1].end_pos, s, sl, true);
			}
		}
		
		i = j;
	}
}



//////////////////////////////////////////////////////////////////////////////
// Telephone recognition stuff

static void recognize_telephone_numbers_denmark(TokenizerResult *tr);

static void recognize_telephone_numbers(TokenizerResult *tr, lang_t lang, const char *country_code) {
	if(!country_code)
		country_code="";
	if(lang==langDanish || strcmp(country_code,"dk")==0)
		recognize_telephone_numbers_denmark(tr);
}

static bool is_ascii_digits(const TokenRange &tr) {
	for(size_t i=0; i<tr.token_len; i++)
		if(!is_digit(*(tr.token_start+i)))
			return false;
	return true;
}

static bool is_whitespace(const TokenRange &tr) {
	const char *p = tr.token_start;
	const char *end = tr.token_start+tr.token_len;
	while(p<end) {
		int cs = getUtf8CharSize(p);
		if(p+cs>end)
			return -1; //decode error
		if(!is_wspace_utf8(p))
			return false;
		p += cs;
	}
	return true;
}

static void recognize_telephone_numbers_denmark(TokenizerResult *tr) {
	//Closed numbering plan, 8 digits.
	//recommended format "9999 9999" (already handled as bigram)
	//most common format: "99 99 99 99"
	//less common: "99-99-99-99" (handled by whyphenation logic)
	//number may be prefixed with "+45"
	
	const size_t org_token_count = tr->size();
	for(size_t i=0; i+6<org_token_count; i++) {
		const auto &t0 = (*tr)[i+0];
		const auto &t1 = (*tr)[i+1];
		const auto &t2 = (*tr)[i+2];
		const auto &t3 = (*tr)[i+3];
		const auto &t4 = (*tr)[i+4];
		const auto &t5 = (*tr)[i+5];
		const auto &t6 = (*tr)[i+6];
		if(!t0.is_alfanum ||
		    t1.is_alfanum ||
		   !t2.is_alfanum ||
		    t3.is_alfanum ||
		   !t4.is_alfanum ||
		    t5.is_alfanum ||
		   !t6.is_alfanum)
			continue;
		if(t0.token_len!=2 ||
		   t2.token_len!=2 ||
		   t4.token_len!=2 ||
		   t6.token_len!=2)
			continue;
		if(!is_ascii_digits(t0) ||
		   !is_whitespace(t1) ||
		   !is_ascii_digits(t2) ||
		   !is_whitespace(t3) ||
		   !is_ascii_digits(t4) ||
		   !is_whitespace(t5) ||
		   !is_ascii_digits(t6))
			continue;
		//ok, looks like a danish phone number in format "99 99 99 99"
		if(i>=2 &&
		   (*tr)[i-2].is_alfanum &&
		   is_ascii_digits((*tr)[i-2]))
			continue; //preceeding token is also a number. Don't index this. It could be lottery number or similar.
		if(i+8<org_token_count &&
		   (*tr)[i+8].is_alfanum &&
		   is_ascii_digits((*tr)[i+8]))
			continue; //succeding token is also a number. Don't index this. It could be lottery number or similar.
		size_t sl = t0.token_len+t2.token_len+t4.token_len+t6.token_len;
		char *s = (char *)tr->egstack.alloc(sl);
		char *p=s;
		memcpy(p, t0.token_start, t0.token_len);
		p += t0.token_len;
		memcpy(p, t2.token_start, t2.token_len);
		p += t2.token_len;
		memcpy(p, t4.token_start, t4.token_len);
		p += t4.token_len;
		memcpy(p, t6.token_start, t6.token_len);
		//p += t6.token_len;
		
		tr->tokens.emplace_back(t0.start_pos, t6.end_pos, s, sl, true);
	}
}
