#include "tokenizer.h"
#include "tokenizer_util.h"
#include "XmlNode.h"
#include <string.h>

static void recognize_superscript(TokenizerResult *tr);
static void recognize_subscript(TokenizerResult *tr);

void xml_tokenizer_phase_2(const Xml * /*xml*/, lang_t lang, const char *country_code, TokenizerResult *tr) {
	plain_tokenizer_phase_2(lang,country_code,tr);
	recognize_superscript(tr);
	recognize_subscript(tr);
}


static void recognize_superscript(TokenizerResult *tr) {
	//Look for <word> <sup-tag> <single-character-word> <sup-end-tag>
	const size_t org_token_count = tr->size();
	for(size_t i=0; i+2<org_token_count; i++) {
		const auto &token0 = (*tr)[i];
		const auto &token1 = (*tr)[i+1];
		const auto &token2 = (*tr)[i+2];
		const auto &token3 = (*tr)[i+3];
		if(token0.is_alfanum &&
		   token1.nodeid == TAG_SUP &&
		   token2.is_alfanum && token2.token_len==1 &&
		   token3.nodeid == (TAG_SUP|BACKBIT))
		{
			UChar32 uc = normal_to_superscript_codepoint(*token2.token_start);
			if(uc!=0) {
				char utf8[4];
				size_t utf8len = utf8Encode(uc,utf8);
				size_t sl = token0.token_len + utf8len;
				char *s = (char*)tr->egstack.alloc(sl);
				memcpy(s, token0.token_start, token0.token_len);
				memcpy(s+token0.token_len, utf8, utf8len);
				tr->tokens.emplace_back(token0.start_pos, token2.end_pos, s,sl, false, true);
			}
		}
	}
}


static void recognize_subscript(TokenizerResult *tr) {
	//Look for <word> <sub-tag> <single-character-word> <sub-end-tag>
	const size_t org_token_count = tr->size();
	for(size_t i=0; i+2<org_token_count; i++) {
		const auto &token0 = (*tr)[i];
		const auto &token1 = (*tr)[i+1];
		const auto &token2 = (*tr)[i+2];
		const auto &token3 = (*tr)[i+3];
		if(token0.is_alfanum &&
		   token1.nodeid == TAG_SUB &&
		   token2.is_alfanum && token2.token_len==1 &&
		   token3.nodeid == (TAG_SUB|BACKBIT))
		{
			UChar32 uc = normal_to_subscript_codepoint(*token2.token_start);
			if(uc!=0) {
				char utf8[4];
				size_t utf8len = utf8Encode(uc,utf8);
				size_t sl = token0.token_len + utf8len;
				char *s = (char*)tr->egstack.alloc(sl);
				memcpy(s, token0.token_start, token0.token_len);
				memcpy(s+token0.token_len, utf8, utf8len);
				tr->tokens.emplace_back(token0.start_pos, token2.end_pos, s,sl, false, true);
			}
			//TODO: collect multiple words for correctly tokenizing eg. H₂O₂
		}
	}
}
