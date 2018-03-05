#include "tokenizer.h"
#include "XmlNode.h"

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
			UChar32 uc=0;
			switch(*token2.token_start) {
				case '1': uc=0x00B9; break;
				case '2': uc=0x00B2; break;
				case '3': uc=0x00B3; break;
				case '4': uc=0x2074; break;
				case '5': uc=0x2075; break;
				case '6': uc=0x2076; break;
				case '7': uc=0x2077; break;
				case '8': uc=0x2078; break;
				case '9': uc=0x2079; break;
				case 'a': uc=0x00AA; break;
				case 'o': uc=0x00BA; break;
				case 'i': uc=0x2071; break;
				case 'n': uc=0x207F; break;
			}
			if(uc!=0) {
				char utf8[4];
				size_t utf8len = utf8Encode(uc,utf8);
				size_t sl = token0.token_len + utf8len;
				char *s = (char*)tr->egstack.alloc(sl);
				memcpy(s, token0.token_start, token0.token_len);
				memcpy(s+token0.token_len, utf8, utf8len);
				tr->tokens.emplace_back(token0.start_pos, token2.end_pos, s,sl, true);
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
			UChar32 uc=0;
			switch(*token2.token_start) {
				case '0': uc=0x2080; break;
				case '1': uc=0x2081; break;
				case '2': uc=0x2082; break;
				case '3': uc=0x2083; break;
				case '4': uc=0x2084; break;
				case '5': uc=0x2085; break;
				case '6': uc=0x2086; break;
				case '7': uc=0x2087; break;
				case '8': uc=0x2088; break;
				case '9': uc=0x2089; break;
				case 'a': uc=0x2090; break;
				case 'e': uc=0x2091; break;
				case 'h': uc=0x2095; break;
				case 'i': uc=0x1D62; break;
				case 'j': uc=0x2C7C; break;
				case 'k': uc=0x2096; break;
				case 'l': uc=0x2097; break;
				case 'm': uc=0x2098; break;
				case 'n': uc=0x2099; break;
				case 'o': uc=0x2092; break;
				case 'p': uc=0x209A; break;
				case 'r': uc=0x1D63; break;
				case 's': uc=0x209B; break;
				case 't': uc=0x209C; break;
				case 'u': uc=0x1D64; break;
				case 'v': uc=0x1D65; break;
				case 'x': uc=0x2093; break;
			}
			if(uc!=0) {
				char utf8[4];
				size_t utf8len = utf8Encode(uc,utf8);
				size_t sl = token0.token_len + utf8len;
				char *s = (char*)tr->egstack.alloc(sl);
				memcpy(s, token0.token_start, token0.token_len);
				memcpy(s+token0.token_len, utf8, utf8len);
				tr->tokens.emplace_back(token0.start_pos, token2.end_pos, s,sl, true);
			}
			//TODO: collect multiple words for correclty tokenizing eg. H₂O₂
		}
	}
}
