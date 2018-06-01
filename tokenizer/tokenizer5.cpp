#include"tokenizer.h"
#include "hash.h"

void calculate_tokens_hashes(TokenizerResult *tr) {
	for(auto &token : tr->tokens)
		if(token.is_alfanum)
			token.token_hash = hash64Lower_utf8(token.token_start,token.token_len);
}
