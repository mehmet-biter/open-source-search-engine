#ifndef FX_TOKENIZER_H_
#define FX_TOKENIZER_H_
#include <vector>
#include <string>
#include "nodeid_t.h"
#include "Lang.h"
#include "EGStack.h"


struct TokenRange {
	TokenRange(size_t start_pos_, size_t end_pos_, const char *token_start_, size_t token_len_, bool is_alfanum_)
	  : start_pos(start_pos_), end_pos(end_pos_),
	    token_start(token_start_), token_len(token_len_),
	    is_alfanum(is_alfanum_),
	    token_hash(0),
	    nodeid(0),
	    xml_node_index(0)
	  { }
	TokenRange(size_t start_pos_, size_t end_pos_, const char *token_start_, size_t token_len_, nodeid_t node_id, int32_t xml_node_index_)
	  : start_pos(start_pos_), end_pos(end_pos_),
	    token_start(token_start_), token_len(token_len_),
	    is_alfanum(false),
	    token_hash(0),
	    nodeid(node_id),
	    xml_node_index(xml_node_index_)
	  { }
	
	size_t          start_pos, end_pos;    //[start..end[ in source text
	const char *    token_start;           //ptr to token start text; may or may not be inside original source material
	size_t          token_len;             //length of token in bytes. May be different than end_pos-start_pos
	bool            is_alfanum;            //is this a word or not?
	int64_t         token_hash;            //from hash64Lower_utf8(), 0 if !is_alfanum
	nodeid_t        nodeid;                //html/xml tag id
	int32_t         xml_node_index;        //index into Xml instance
};


class TokenizerResult {
public:

	bool empty() const { return tokens.empty(); }
	size_t size() const { return tokens.size(); }
	TokenRange& operator[](size_t i) { return tokens[i]; }
	const TokenRange& operator[](size_t i) const { return tokens[i]; }
	
	void clear() {
		tokens.clear();
		egstack.clear();
	}

	std::vector<TokenRange> tokens;
	EGStack egstack; //heap for extra tokens
};


void plain_tokenizer_phase_1(const char *str, size_t len, TokenizerResult *tr);
void plain_tokenizer_phase_1_downcall(const char *str, size_t len, size_t pos_base, TokenizerResult *tr);
void plain_tokenizer_phase_2(lang_t lang, const char *country_code, TokenizerResult *tr);
class Xml;
void xml_tokenizer_phase_1(const Xml *xml, TokenizerResult *tr);
void xml_tokenizer_phase_2(const Xml *xml, lang_t lang, const char *country_code, TokenizerResult *tr);

void calculate_tokens_hashes(TokenizerResult *tr);


#endif
