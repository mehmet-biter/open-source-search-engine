#include "tokenizer.h"
#include "UCMaps.h"
#include "UCDecompose.h"
#include "UCCMDecompose.h"
#include "utf8_fast.h"
#include "ligature_decomposition.h"
#include "tokenizer_util.h"
#include <string.h>
#include <algorithm>


static const size_t max_word_codepoints = 128; //longest word we will consider working on

static void decompose_stylistic_ligatures(TokenizerResult *tr);
static void decompose_language_specific_ligatures(TokenizerResult *tr, lang_t lang, const char *country_code);
static void remove_combining_marks(TokenizerResult *tr, lang_t lang);
static void combine_possessive_s_tokens(TokenizerResult *tr, lang_t lang);
static void combine_hyphenated_words(TokenizerResult *tr);
static void recognize_telephone_numbers(TokenizerResult *tr, lang_t lang, const char *country_code);
static void tokenize_superscript(TokenizerResult *tr);
static void tokenize_subscript(TokenizerResult *tr);
static void rewrite_ampersands(TokenizerResult *tr, lang_t lang, const char *country_code);
static void recognize_cplusplus_and_other(TokenizerResult *tr);
static void collapse_slash_abbreviations(TokenizerResult *tr);
static void remove_duplicated_tokens(TokenizerResult *tr, size_t org_token_count);


//pass 2 tokenizer / language-dependent tokenization
//When we know the language there may be tokens that were split into multiple tokes by tokenize() but can
//be considered a whole token. Eg. contractions, hyphenation, and other oddball stuff.
//If we furthermore knew the locale then we could recognize numbers, phone numbers, post codes. But we
//don't know the locale with certainty, so we take the language as a hint.
//Also joins words separated with hyphen (all 10 of them)
void plain_tokenizer_phase_2(lang_t lang, const char *country_code, TokenizerResult *tr) {
	size_t org_token_count = tr->tokens.size();
	if(!country_code)
		country_code = "";
	decompose_stylistic_ligatures(tr);
	decompose_language_specific_ligatures(tr,lang,country_code);
	collapse_slash_abbreviations(tr);
	remove_combining_marks(tr,lang);
	combine_possessive_s_tokens(tr,lang);
	//TODO: chemical formulae
	tokenize_subscript(tr);
	tokenize_superscript(tr);
	//TODO: detect circumflex used for power, eg m^2
	combine_hyphenated_words(tr);
	recognize_telephone_numbers(tr,lang,country_code);
	//TODO: recognize_numbers(tr,lang,country_code)
	//TODO: support use by query with quotation marks for suppressing alternatives (eg, "john's cat" should be not generate the "johns" special bigram)
	rewrite_ampersands(tr,lang,country_code);
	recognize_cplusplus_and_other(tr);
	
	//The phase-2 sub-rules can have produced duplicated tokens. Eg. the hyphenation joining and the telephone number recognition may both have
	//joined  "111-222-333" into a single token. Remove the duplicates.
	//phase-2 may have removed/replaced some of the original tokens, so 'org_token_count' is just a good guess.
	//Make the guess exact
	while(org_token_count > tr->tokens.size())
		org_token_count--;
	while(org_token_count>0 && !tr->tokens[org_token_count-1].is_primary)
		org_token_count--;
	
	remove_duplicated_tokens(tr,org_token_count);
}


static void remove_duplicated_tokens(TokenizerResult *tr, size_t org_token_count) {
	auto &tokens = tr->tokens;
	std::sort(tokens.begin()+org_token_count, tokens.end(), [](const TokenRange&tr0, const TokenRange &tr1) {
		return tr0.start_pos < tr1.start_pos ||
		       (tr0.start_pos == tr1.start_pos && tr0.end_pos<tr1.end_pos);
	});

	for(size_t i=org_token_count; i+1<tokens.size(); i++) {
		if(tokens[i].start_pos == tokens[i+1].start_pos &&
		   tokens[i].end_pos == tokens[i+1].end_pos &&
		   tokens[i].token_len == tokens[i+1].token_len &&
		   memcmp(tokens[i].token_start,tokens[i+1].token_start,tokens[i].token_len)==0)
			tokens.erase(tokens.begin()+i+1);
	}
}


//////////////////////////////////////////////////////////////////////////////
// Ligature stuff

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
			tr->tokens.emplace_back(token.start_pos,token.end_pos, s,new_token_utf8_len, false, true);
		}
	}
}


static void replace_ligature(const UChar32 original_codepoint[], unsigned original_codepoints, UChar32 ligature_codepoint, const UChar32 replacement_codepoint[], unsigned replacement_codepoints,
			     TokenizerResult *tr, const TokenRange &token) {
	bool found=false;
	for(unsigned i=0; i<original_codepoints; i++)
		if(original_codepoint[i]==ligature_codepoint)
			found=true;
	if(!found)
		return;
	
	UChar32 uc_new_token[max_word_codepoints*3]; //worst-case expansion
	unsigned new_codepoints=0;
	for(unsigned i=0; i<original_codepoints; i++) {
		if(original_codepoint[i]!=ligature_codepoint)
			uc_new_token[new_codepoints++] = original_codepoint[i];
		else {
			for(unsigned j=0; j<replacement_codepoints; j++)
				uc_new_token[new_codepoints++] = replacement_codepoint[j];
		}
	}
	char new_token_utf8[sizeof(uc_new_token)];
	size_t new_token_utf8_len = encode_utf8_string(uc_new_token,new_codepoints,new_token_utf8);
	char *s = (char*)tr->egstack.alloc(new_token_utf8_len+1);
	memcpy(s,new_token_utf8,new_token_utf8_len);
	tr->tokens.emplace_back(token.start_pos,token.end_pos, s,new_token_utf8_len, false, true);
}


static void decompose_english_specific_ligatures(TokenizerResult *tr) {
	//The ligature Œ/œ is used in some french loanwords, and can be decomposed into oe or o (amœba -> amoeba, œconomist -> economist)
	//The ligature Æ/æ is used in some latin loanwords, and can be decomposed into ae or e (encyclopædia -> encyclopaedia/encyclopedia)
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		const auto &token = (*tr)[i];
		if(!token.is_alfanum)
			continue;
		UChar32 uc_org_token[max_word_codepoints];
		if(token.token_len > sizeof(uc_org_token)/4)
			continue;
		int org_codepoints = decode_utf8_string(token.token_start,token.token_len,uc_org_token);
		if(org_codepoints<=0)
			continue; //decode error or empty token
		const auto org_token_copy = token; //need a copy becahse push_back may reallocate
		replace_ligature(uc_org_token,org_codepoints, 0x0152, (const UChar32[]){'O','E'},2, tr,org_token_copy); //LATIN CAPITAL LIGATURE OE
		replace_ligature(uc_org_token,org_codepoints, 0x0152, (const UChar32[]){'O'},1,     tr,org_token_copy); //LATIN CAPITAL LIGATURE OE
		replace_ligature(uc_org_token,org_codepoints, 0x0153, (const UChar32[]){'o','e'},2, tr,org_token_copy); //LATIN SMALL LIGATURE OE
		replace_ligature(uc_org_token,org_codepoints, 0x0153, (const UChar32[]){'o'},1,     tr,org_token_copy); //LATIN SMALL LIGATURE OE
		replace_ligature(uc_org_token,org_codepoints, 0x00C6, (const UChar32[]){'A','E'},2, tr,org_token_copy); //LATIN CAPITAL LETTER AE
		replace_ligature(uc_org_token,org_codepoints, 0x00C6, (const UChar32[]){'E'},1,     tr,org_token_copy); //LATIN CAPITAL LETTER AE
		replace_ligature(uc_org_token,org_codepoints, 0x00E6, (const UChar32[]){'a','e'},2, tr,org_token_copy); //LATIN SMALL LETTER AE
		replace_ligature(uc_org_token,org_codepoints, 0x00E6, (const UChar32[]){'e'},1,     tr,org_token_copy); //LATIN SMALL LETTER AE
	}
}

static void decompose_french_specific_ligatures(TokenizerResult *tr) {
	//Long story. But the essensce of it is that Œ/œ are real letters in French, but due to technical limitations OE/oe was used until unicode became widespread.
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		const auto &token = (*tr)[i];
		if(!token.is_alfanum)
			continue;
		UChar32 uc_org_token[max_word_codepoints];
		if(token.token_len > sizeof(uc_org_token)/4)
			continue;
		int org_codepoints = decode_utf8_string(token.token_start,token.token_len,uc_org_token);
		if(org_codepoints<=0)
			continue; //decode error or empty token
		const auto org_token_copy = token; //need a copy becahse push_back may reallocate
		replace_ligature(uc_org_token,org_codepoints, 0x0152, (const UChar32[]){'O','E'},2, tr,org_token_copy);
		replace_ligature(uc_org_token,org_codepoints, 0x0153, (const UChar32[]){'o','e'},2, tr,org_token_copy);
	}
}

static void decompose_german_ligatures(TokenizerResult * /*tr*/) {
	//German has no plain ligatures. But it does have ẞ/ß which are a bit complicated.
	//When lowercase ß (U+00DF) is uppercased it changes to:
	//  a: SS
	//  b: SZ (sometimes, it's complicated…)
	//  c: ẞ (U+1E9E) (since 2017)
	//When uppercase ẞ (U+1E9E) is lowercased it turns into a plain ß.
	//The ß letter is not used in Switzerland and Liechtenstein.
	//So in titles with all-caps we may see:
	//  GOTHAERSTRASSE, GOTHAERSTRASZE or GOTHAERSTRAẞE.
	// When indexing we turn them to lowercase an index
	//  gothaerstrasse, gothaerstrasze, or gothaestraße
	//It is more efficient (space+time) to handle this with word variations at query time.
	//  - When German/Austrian user search for gothaerstraße the word_variations should automatically
	//    expand it to gothaerstrasse and possibly gothaerstrasze.
	//  - When a Swiss searches for gothaerstrasse word_variations should automatically
	//    expand it to gothaerstraße and possibly gothaerstrasze.
	//
	//note: Straße decomposes the ß to -ss-, so it is a bad example for -sz-. A better example would be
	//"Maßstab", where the German orthography until 1980 required that ß was uppercased to SZ to avoid confusion.
	//
	//Bottom line: it doesn't appear we have to do anything special at indexing time.
}

static void decompose_language_specific_ligatures(TokenizerResult *tr, lang_t lang, const char * /*country_code*/) {
	if(lang==langEnglish)
		decompose_english_specific_ligatures(tr);
	else if(lang==langFrench)
		decompose_french_specific_ligatures(tr);
	else if(lang==langGerman)
		decompose_german_ligatures(tr);
}


//////////////////////////////////////////////////////////////////////////////
// Combining marks removal

static void remove_combining_marks_danish(TokenizerResult *tr);
static void remove_combining_marks_norwegian(TokenizerResult *tr);


static void remove_combining_marks(TokenizerResult *tr, lang_t lang) {
	switch(lang) {
		case langDanish:
			remove_combining_marks_danish(tr);
			return;
		case langNorwegian:
			remove_combining_marks_norwegian(tr);
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
			continue; //Don't remove diacritics in words longer than <max_word_codepoints> characters
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
			
			//compose the codepoint again (if possible)
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
			tr->tokens.emplace_back(token.start_pos,token.end_pos, s,new_token_utf8_len, false, true);
		}
	}
}

//According to my Norwegian contact:
//  - ring-above	(Å/å)		Mandatory
//  - umlaut		(äüö)		Well-known and easily accessible. In words from Swedish or German
//  - acute-accent	(éin)		Optional, used for stress marking, or in French loanwords.
//  - grave accent      (fòr)		Optional, used for clarifying meaning of homonyms
//  - circumflex	(fôr)		Optional, used for clarifying meaning of homonyms
static void remove_combining_marks_norwegian(TokenizerResult *tr) {
	//this happens to be the exact same rules as for Danish so let's just use that function
	remove_combining_marks_danish(tr);
}

//////////////////////////////////////////////////////////////////////////////
// Possessive-s handling

//Join word-with-possessive-s into a single token
//Also take care of misused/abused other marks, such as modifier letters, prime marks, etc. Even in native English text
//the apostrophe is sometimes morphed into weird codepoints. So we take all codepoints whose glyphs look like a blotch
//that could conceivably stand in for apostrophe. We do this in all languages because the abuse seem to know no language barrier
static void combine_possessive_s_tokens(TokenizerResult *tr, lang_t lang) {
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
		if(t2.token_len!=1 || (t2.token_start[0]!='s' && t2.token_start[0]!='S'))
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
		
		const TokenRange t0_copy = t0; //copied due to vector modification
		
		//generate the token <word>+apostrophe+'s'
		size_t token_a_len = t0_copy.token_len + 1 + 1;
		char *token_a = (char*)tr->egstack.alloc(token_a_len);
		memcpy(token_a, t0_copy.token_start, t0.token_len);
		token_a[t0_copy.token_len] = '\'';
		token_a[t0_copy.token_len+1] = 's';
		tr->tokens.emplace_back(t0_copy.start_pos,t2.end_pos,token_a, token_a_len, false, true);

		//German/Danish/Norwegian/Swedish don't use apostrophe for possessive-s, however some web pages may use it for
		//stylistic/origin reasons (eg McDonald's) or because they like the "greengrocer's apostrophe".
		//So index <word>+'s' too
		if(lang!=langEnglish && lang!=langDutch) {
			size_t token_b_len = t0_copy.token_len + 1;
			char *token_b = (char*)tr->egstack.alloc(token_b_len);
			memcpy(token_b, t0_copy.token_start, t0.token_len);
			token_b[t0_copy.token_len] = 's';
			tr->tokens.emplace_back(t0_copy.start_pos,t2.end_pos,token_b, token_b_len, false, true);
		}
		//In the case of "John's car" we now have the tokens:
		//  John
		//  Johns
		//  car
		//and XmlDoc_indexing.cpp will generate the bigram "johns+car", but also "s+car".
		//We remove the 's' token because it (a) causes trouble with weird bigrams, and (b) it has little meaning by itself.
		tr->tokens.erase(tr->tokens.begin()+i+2);
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
//have hard rules. Eg. for "smurf cd-rom" we would like the bigram "smurfcdrom". So we look for hyphenated words and treat them
//as a single word, allowing the upper layer to generate larger bigrams. One challenge is if there are obscenely long hyphenated words,
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
		auto const &first_token = (*tr)[i];
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
				tr->tokens.emplace_back(t0.start_pos, t1.end_pos, s, sl, false, true);
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
				tr->tokens.emplace_back((*tr)[i].start_pos, (*tr)[j-1].end_pos, s, sl, false, true);
			}
		}
		
		i = j;
	}
}



//////////////////////////////////////////////////////////////////////////////
// Telephone number recognition

static void recognize_telephone_numbers_denmark_norway(TokenizerResult *tr);
static void recognize_telephone_numbers_germany(TokenizerResult *tr);

static void recognize_telephone_numbers(TokenizerResult *tr, lang_t lang, const char *country_code) {
	if(lang==langDanish || strcmp(country_code,"dk")==0 ||
	   lang==langNorwegian || strcmp(country_code,"no")==0)
		recognize_telephone_numbers_denmark_norway(tr);
	else if(strcmp(country_code,"de")==0)
		recognize_telephone_numbers_germany(tr);
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

static void recognize_telephone_numbers_denmark_norway(TokenizerResult *tr) {
	//Closed numbering plan, 8 digits.
	//Denmark:
	//  recommended format "9999 9999" (already handled as bigram)
	//  most common format: "99 99 99 99"
	//  less common: "99-99-99-99" (handled by hyphenation logic)
	//  number may be prefixed with "+45"
	//Norway:
	//  most common format: "99 99 99 99"
	
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
		//ok, looks like a danish/norwegian phone number in format "99 99 99 99"
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
		
		tr->tokens.emplace_back(t0.start_pos, t6.end_pos, s, sl, false, true);
	}
}

static void recognize_telephone_numbers_germany(TokenizerResult *tr) {
	//Semi-open numbering plan, 10 or 11 digits, including areacode
	//Areacodes can be 2-5 digits and subscripber numbers can be up to 8
	//Recommended formats:
	//	(0xx) xxxx-xxxx
	//	(0xxx) xxxx-xxxx
	//	(0xxxx) xxx-xxxx
	//	(03xxxx) xx-xxxx
	//But actually:
	//	+49 4621 99999
	//	+49 4621 999999
	//	04621 / 99 99 99
	//	0461 9999-9999
	//	04621-999999
	//	04621 999999
	//	04621 - 999 99 99
	//	040/999.999-999
	//	49(0)40-999999-999
	//	(040) 99 99-99 99
	//	0341/999 99 99
	//	+49 89 9 999 999-9
	//	+49 (0)89/99 99 99-99
	//	+49 40 999999999
	//So we look for a sequence of alfanum-alldigits tokens separated by space, slash, dash, dot and parentheses, yielding 10 or 11 digits. Terminated by non-digit alfanum or eof
	//The first token must start with a zero, or the preceeding alfanum token must be "+49"
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		const auto &t0 = (*tr)[i];
		if(!t0.is_alfanum)
			continue;
		if(!is_ascii_digits(t0))
			continue;
		if(t0.token_len>11)
			continue;
		if(t0.token_len==1 && t0.token_start[0]!='0')
			continue; //single-digit never seen to start a number
		const TokenRange *preceeding_alfanum_token = NULL;
		size_t preceeding_alfanum_token_idx=0;
		for(size_t j=i; j!=0; j--) {
			if((*tr)[j-1].is_alfanum) {
				preceeding_alfanum_token = &((*tr)[j-1]);
				preceeding_alfanum_token_idx = j-1;
				break;
			}
			if((*tr)[j-1].nodeid)
				break; //an html tag also breaks
		}
		bool could_start_phone_number = true;
		bool preceeded_by_plus_49 = false;
		if(preceeding_alfanum_token) {
			if(t0.token_start[0]=='0') {
				//preceeding alfanum token must be either non-digit or "49"
				if(preceeding_alfanum_token->is_alfanum) {
					if(is_ascii_digits(*preceeding_alfanum_token) &&
					   (preceeding_alfanum_token->token_len!=2 || memcmp(preceeding_alfanum_token->token_start,"49",2)!=0))
						could_start_phone_number = false;
				}
			} else {
				//preceeding alfanum token must be (+)"49"
				if(preceeding_alfanum_token->is_alfanum) {
					if(!is_ascii_digits(*preceeding_alfanum_token) ||
					   preceeding_alfanum_token->token_len!=2 || memcmp(preceeding_alfanum_token->token_start,"49",2)!=0)
						could_start_phone_number = false;
				}
			}
			if(preceeding_alfanum_token->token_len==2 && memcmp(preceeding_alfanum_token->token_start,"49",2)==0) {
				//if the pre-preceeding token ends with '+' then good
				if(preceeding_alfanum_token_idx>0) {
					const auto &ppt = (*tr)[preceeding_alfanum_token_idx-1];
					if(ppt.token_len>0 && ppt.token_end()[-1]=='+')
						preceeded_by_plus_49 = true;
				}
			}
		}
		if(!could_start_phone_number)
			continue;
		//ok, token[i] could be the start of a phone number
		//scan forward collecting all-digit tokens.
		size_t j;
		size_t collected_digit_count=0;
		for(j=i; j<org_token_count && j<i+12; j++) {
			const auto &t = (*tr)[j];
			if(t.is_alfanum) {
				if(is_ascii_digits(t))
					collected_digit_count += t.token_len;
				else
					break;
				if(collected_digit_count>12)
					break;
			} else if(!t.nodeid) {
				for(size_t k=0; k<t.token_len; k++)
					if(t.token_start[k]!=' ' &&
					   t.token_start[k]!='/' &&
					   t.token_start[k]!='-' &&
					   t.token_start[k]!='(' &&
					   t.token_start[k]!=')')
						break;
			} else {
				//html tag
				break;
			}
		}
		if(collected_digit_count<10 || collected_digit_count>12)
			continue;
		if(collected_digit_count==12 && t0.token_start[0]!='0')
			continue;
		char *s = (char*)tr->egstack.alloc(collected_digit_count+1);
		char *p=s;
		if(t0.token_start[0]!='0' && preceeded_by_plus_49)
			*p++ = '0';
		for(size_t k=i; k<j; k++) {
			const auto &t = (*tr)[k];
			if(t.is_alfanum) {
				memcpy(p,t.token_start,t.token_len);
				p += t.token_len;
			}
		}
		tr->tokens.emplace_back(t0.start_pos, (*tr)[j-1].end_pos, s, p-s, false, true);
	}
	
}

//////////////////////////////////////////////////////////////////////////////
// Superscript and subscript
static void tokenize_superscript(TokenizerResult *tr) {
	//The phase-1 tokenizer considers "E=mc²" three tokens.
	//Because people normally don't type the superscript-2 we generate a variant with plain digit
	//If the superscript is at the end of the token then we also generate two tokens split. This is
	//a workaround for footnote numbers directly attached to the preceeding word.
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		auto const &t = (*tr)[i];
		if(!t.is_alfanum)
			continue;
		if(t.token_len>max_word_codepoints)
			continue;
		UChar32 org_uc[max_word_codepoints];
		int ucs = decode_utf8_string(t.token_start,t.token_len,org_uc);
		if(ucs<=0)
			continue;
		UChar32 new_uc[max_word_codepoints];
		bool any_changed = false;
		int num_changed=0;
		int change_pos=-1;
		for(int j=0; j<ucs; j++) {
			//UnicodeData.txt has many entries with <super> decomposition but we only look for
			//a subset of those (we don't care about IPA extensions, ideographic annotations, ...)
			UChar32  n = org_uc[j];
			switch(org_uc[j]) {
				case 0x00AA: //FEMININE ORDINAL INDICATOR
					n = 0x0061; break;
				case 0x00B2: //SUPERSCRIPT TWO
					n = 0x0032; break;
				case 0x00B3: //SUPERSCRIPT THREE
					n = 0x0033; break;
				case 0x00B9: //SUPERSCRIPT ONE
					n = 0x0031; break;
				case 0x00BA: //MASCULINE ORDINAL INDICATOR
					n = 0x006F; break;
				case 0x2070: //SUPERSCRIPT ZERO
					n = 0x0030; break;
				case 0x2071: //SUPERSCRIPT LATIN SMALL LETTER I
					n = 0x0069; break;
				case 0x2074: //SUPERSCRIPT FOUR
					n = 0x0034; break;
				case 0x2075: //SUPERSCRIPT FIVE
					n = 0x0035; break;
				case 0x2076: //SUPERSCRIPT SIX
					n = 0x0036; break;
				case 0x2077: //SUPERSCRIPT SEVEN
					n = 0x0037; break;
				case 0x2078: //SUPERSCRIPT EIGHT
					n = 0x0038; break;
				case 0x2079: //SUPERSCRIPT NINE
					n = 0x0039; break;
// 				case 0x207A: //SUPERSCRIPT PLUS SIGN
// 					n = 0x002B; break;
// 				case 0x207B: //SUPERSCRIPT MINUS
// 					n = 0x2212; break;
// 				case 0x207C: //SUPERSCRIPT EQUALS SIGN
// 					n = 0x003D; break;
// 				case 0x207D: //SUPERSCRIPT LEFT PARENTHESIS
// 					n = 0x0028; break;
// 				case 0x207E: //SUPERSCRIPT RIGHT PARENTHESIS
// 					n = 0x0029; break;
				case 0x207F: //SUPERSCRIPT LATIN SMALL LETTER N
					n = 0x006E; break;
				default:
					break;
			}
			new_uc[j] = n;
			if(n!=org_uc[j]) {
				any_changed = true;
				num_changed++;
				change_pos = j;
			}
		}
		if(any_changed) {
			const auto org_token_copy = t; //need copy because emplace_back may realloc
			char *s = (char*)tr->egstack.alloc(ucs*4);
			size_t sl = encode_utf8_string(new_uc,ucs,s);
			tr->tokens.emplace_back(org_token_copy.start_pos,org_token_copy.end_pos, s,sl, false, true);
			if(num_changed==1 && change_pos==ucs-1) {
				//footnote special (and spanish/portuguese ordinal)
				s = (char*)tr->egstack.alloc((ucs-1)*4);
				sl = encode_utf8_string(new_uc,ucs-1,s);
				tr->tokens.emplace_back(org_token_copy.start_pos,org_token_copy.start_pos+sl, s,sl, false, true);
				s = (char*)tr->egstack.alloc(4);
				sl = encode_utf8_string(new_uc+ucs-1,1,s);
				tr->tokens.emplace_back(org_token_copy.end_pos-sl,org_token_copy.end_pos, s,sl, false, true);
			}
		}
	}
}

static void tokenize_subscript(TokenizerResult *tr) {
	//The phase-1 tokenizer considers "H₂O" a single token
	//We generate the variant without the subcsript, "H2O"
	const size_t org_token_count = tr->size();
	for(size_t i=0; i<org_token_count; i++) {
		auto const &t = (*tr)[i];
		if(!t.is_alfanum)
			continue;
		if(t.token_len>max_word_codepoints)
			continue;
		UChar32 org_uc[max_word_codepoints];
		int ucs = decode_utf8_string(t.token_start,t.token_len,org_uc);
		if(ucs<=0)
			continue;
		UChar32 new_uc[max_word_codepoints];
		bool any_changed = false;
		for(int j=0; j<ucs; j++) {
			//we should really be using UnicodeData.txt's <sub> decompositions, but it's currently hardly worth it.
			UChar32  n = org_uc[j];
			switch(org_uc[j]) {
				case 0x1D62: //LATIN SUBSCRIPT SMALL LETTER I
					n = 0x0069; break;
				case 0x1D63: //LATIN SUBSCRIPT SMALL LETTER R
					n = 0x0072; break;
				case 0x1D64: //LATIN SUBSCRIPT SMALL LETTER U
					n = 0x0075; break;
				case 0x1D65: //LATIN SUBSCRIPT SMALL LETTER V
					n = 0x0076; break;
				case 0x1D66: //GREEK SUBSCRIPT SMALL LETTER BETA
					n = 0x03B2; break;
				case 0x1D67: //GREEK SUBSCRIPT SMALL LETTER GAMMA
					n = 0x03B3; break;
				case 0x1D68: //GREEK SUBSCRIPT SMALL LETTER RHO
					n = 0x03C1; break;
				case 0x1D69: //GREEK SUBSCRIPT SMALL LETTER PHI
					n = 0x03C6; break;
				case 0x1D6A: //GREEK SUBSCRIPT SMALL LETTER CHI
					n = 0x03C7; break;
				case 0x2080: //SUBSCRIPT ZERO
					n = 0x0030; break;
				case 0x2081: //SUBSCRIPT ONE
					n = 0x0031; break;
				case 0x2082: //SUBSCRIPT TWO
					n = 0x0032; break;
				case 0x2083: //SUBSCRIPT THREE
					n = 0x0033; break;
				case 0x2084: //SUBSCRIPT FOUR
					n = 0x0034; break;
				case 0x2085: //SUBSCRIPT FIVE
					n = 0x0035; break;
				case 0x2086: //SUBSCRIPT SIX
					n = 0x0036; break;
				case 0x2087: //SUBSCRIPT SEVEN
					n = 0x0037; break;
				case 0x2088: //SUBSCRIPT EIGHT
					n = 0x0038; break;
				case 0x2089: //SUBSCRIPT NINE
					n = 0x0039; break;
				case 0x208A: //SUBSCRIPT PLUS SIGN
					n = 0x002B; break;
				case 0x208B: //SUBSCRIPT MINUS
					n = 0x2212; break;
				case 0x208C: //SUBSCRIPT EQUALS SIGN
					n = 0x003D; break;
				case 0x208D: //SUBSCRIPT LEFT PARENTHESIS
					n = 0x0028; break;
				case 0x208E: //SUBSCRIPT RIGHT PARENTHESIS
					n = 0x0029; break;
				case 0x2090: //LATIN SUBSCRIPT SMALL LETTER A
					n = 0x0061; break;
				case 0x2091: //LATIN SUBSCRIPT SMALL LETTER E
					n = 0x0065; break;
				case 0x2092: //LATIN SUBSCRIPT SMALL LETTER O
					n = 0x006F; break;
				case 0x2093: //LATIN SUBSCRIPT SMALL LETTER X
					n = 0x0078; break;
				case 0x2094: //LATIN SUBSCRIPT SMALL LETTER SCHWA
					n = 0x0259; break;
				case 0x2095: //LATIN SUBSCRIPT SMALL LETTER H
					n = 0x0068; break;
				case 0x2096: //LATIN SUBSCRIPT SMALL LETTER K
					n = 0x006B; break;
				case 0x2097: //LATIN SUBSCRIPT SMALL LETTER L
					n = 0x006C; break;
				case 0x2098: //LATIN SUBSCRIPT SMALL LETTER M
					n = 0x006D; break;
				case 0x2099: //LATIN SUBSCRIPT SMALL LETTER N
					n = 0x006E; break;
				case 0x209A: //LATIN SUBSCRIPT SMALL LETTER P
					n = 0x0070; break;
				case 0x209B: //LATIN SUBSCRIPT SMALL LETTER S
					n = 0x0073; break;
				case 0x209C: //LATIN SUBSCRIPT SMALL LETTER T
					n = 0x0074; break;
				case 0x2C7C: //LATIN SUBSCRIPT SMALL LETTER J
					n = 0x006A; break;
				default:
					break;
			}
			new_uc[j] = n;
			if(n!=org_uc[j])
				any_changed = true;
		}
		if(any_changed) {
			char *s = (char*)tr->egstack.alloc(ucs*4);
			size_t sl = encode_utf8_string(new_uc,ucs,s);
			tr->tokens.emplace_back(t.start_pos,t.end_pos, s,sl, false, true);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////
// Ampersand

//In many langauges the ampersand is used in abbreviations, company names, etc for the word equivalent of "and"

static void rewrite_ampersands(TokenizerResult *tr, const char *ampersand_word, size_t ampersand_word_len);

static void rewrite_ampersands(TokenizerResult *tr, lang_t lang, const char *country_code) {
	if(lang==langDanish || strcmp(country_code,"da")==0)
		rewrite_ampersands(tr, "og",2);
	else if(lang==langEnglish || strcmp(country_code,"us")==0 || strcmp(country_code,"uk")==0 || strcmp(country_code,"au")==0 || strcmp(country_code,"nz")==0)
		rewrite_ampersands(tr, "and",3);
	else if(lang==langGerman || strcmp(country_code,"de")==0 || strcmp(country_code,"at")==0 || strcmp(country_code,"li")==0)
		rewrite_ampersands(tr, "und",3);
}


static void rewrite_ampersands(TokenizerResult *tr, const char *ampersand_word, size_t ampersand_word_len) {
	char *s = NULL;
	for(const auto &t : tr->tokens) {
		if(t.token_len==1 && *t.token_start=='&') {
			if(!s) {
				s = (char*)tr->egstack.alloc(ampersand_word_len);
				memcpy(s,ampersand_word,ampersand_word_len);
			}
			tr->tokens.emplace_back(t.start_pos,t.end_pos, s,ampersand_word_len, false, true);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////
// Oddballs

static void recognize_alfanum_nonalfanum_pair(TokenizerResult *tr, const char *tokenstr0, const char *tokenstr1, const char *replacement_token) {
	size_t tokenstr0_len = strlen(tokenstr0);
	size_t tokenstr1_len = strlen(tokenstr1);
	size_t rlen = strlen(replacement_token);
	for(size_t i=0; i+1<tr->size(); i++) {
		const auto &t0 = (*tr)[i];
		const auto &t1 = (*tr)[i+1];
		if(t0.is_alfanum && !t1.is_alfanum && !t1.nodeid &&
		   t0.token_len==tokenstr0_len && t1.token_len>=tokenstr1_len &&
		   memcmp(t0.token_start,tokenstr0,tokenstr0_len)==0 && memcmp(t1.token_start,tokenstr1,tokenstr1_len)==0)
		{
			//now check that t1 after the tokenstr1 prefix has only whitespace and and punctuation
			//(todo): check punctuation/whitespace correctly For half-alfanum tokens as "C++"
			if(t1.token_len==tokenstr1_len ||
			   t1.token_start[tokenstr1_len]==' ' || t1.token_start[tokenstr1_len]=='.' || t1.token_start[tokenstr1_len]==',')
			{
				char *s = (char*)tr->egstack.alloc(rlen);
				memcpy(s,replacement_token,rlen);
				tr->tokens.emplace_back(t0.start_pos,t1.start_pos+tokenstr1_len, s,rlen, false, true);
			}
		}
	}
}

static void recognize_cplusplus_and_other(TokenizerResult *tr) {
	//Recognize some alfanum+nonalfanum token pairs, such as:
	//	C++	programming language
	//	F#	programming language
	//	C#	programming language
	//	A*	graph search algorithm
	//We do not recognize operators in programming languages (eg a+=7) or similar. That would need a more dedicated instance for such indexing.
	recognize_alfanum_nonalfanum_pair(tr, "C","++", "C++");
	recognize_alfanum_nonalfanum_pair(tr, "c","++", "C++");
	recognize_alfanum_nonalfanum_pair(tr, "F","#", "F#");
	recognize_alfanum_nonalfanum_pair(tr, "f","#", "f#");
	recognize_alfanum_nonalfanum_pair(tr, "C","#", "C#");
	recognize_alfanum_nonalfanum_pair(tr, "c","#", "c#");
	recognize_alfanum_nonalfanum_pair(tr, "A","*", "A*");
}


//////////////////////////////////////////////////////////////////////////////
// slash-abbreviation stuff, eg "m/sec"
bool is_slash_abbreviation(const char *s, size_t slen) {
	//Minor todo: handle the other slashes than plain U+002F, someone might have used U+2044 or U+2215
	//but that doesn't appear to happen that frequently.
	if(slen<3)
		return false;
	if(slen>4+1+4)
		return false;
	if(memchr(s,'/',slen)==0)
		return false;
	if(s[0]=='/')
		return false;
	if(s[slen-1]=='/')
		return false;
	UChar32 uc_org_token[max_word_codepoints];
	if(slen*4 > sizeof(uc_org_token))
		return false;
	int org_codepoints = decode_utf8_string(s,slen,uc_org_token);
	if(org_codepoints<3)
		return false;
	int slash_count = 0;
	for(int i=0; i<org_codepoints; i++) {
		if(uc_org_token[i]=='/')
			slash_count++;
		else if(UnicodeMaps::is_alphabetic(uc_org_token[i]))
			;
		else
			return false;
	}
	if(slash_count!=1)
		return false;
	//ok, we have <alfa> <slash> <alfa>
	//for <single-alfa> <slash> <single-alfa> we assume it is a slash-abbreviation
	if(org_codepoints==3)
		return true;
	
	//The other abbreviations are hardcoded.
	static const char *a[] = {
		"m/sec",
		"km/h",
		"m/sek",
		"km/t",
		"m/s²",
		"s/n",
		"S/N",
		"Mb/s",		//should we also ahve the common mistake mb/s (millibits per second)?
		"kB/s",
	};
	for(size_t i=0; i<sizeof(a)/sizeof(a[0]); i++) {
		if(strlen(a[i])==slen && memcmp(a[i],s,slen)==0)
			return true;
	}
	return false;
}


static void collapse_slash_abbreviations(TokenizerResult *tr) {
	size_t org_token_count = tr->size();
	for(size_t i=1; i+2<org_token_count; i++) {
		const auto &t0 = (*tr)[i+0];
		const auto &t1 = (*tr)[i+1];
		const auto &t2 = (*tr)[i+2];
		if(!t0.is_alfanum || t1.is_alfanum || !t2.is_alfanum)
			continue;
		if(t1.token_len!=1 || t1.token_start[0]!='/')
			continue;
		if(!t0.is_primary || !t1.is_primary || !t2.is_primary)
			continue;
		if(t0.token_end()!=t1.token_start || t1.token_end()!=t2.token_start)
			continue;
		if(!is_slash_abbreviation(t0.token_start, t0.token_len+t1.token_len+t2.token_len))
			continue;
		size_t sl = t0.token_len + t2.token_len;
		char *s = (char*)tr->egstack.alloc(sl);
		memcpy(s, t0.token_start, t0.token_len);
		memcpy(s+t0.token_len, t2.token_start, t2.token_len);
		tr->tokens.emplace_back(t0.start_pos, t2.end_pos, s,sl, false, true);
		tr->tokens.erase(tr->tokens.begin()+i, tr->tokens.begin()+i+3);
		org_token_count -= 3;
		i -= 2;
	}
}
