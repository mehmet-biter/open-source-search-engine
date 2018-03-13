#include "tokenizer.h"
#include "UCMaps.h"
#include "TitleRecVersion.h"
#include "Xml.h"
#include "HttpMime.h"
#include <assert.h>


static bool has_token(const TokenizerResult &tr, const char *s) {
	size_t sl = strlen(s);
	for(size_t i=0; i<tr.tokens.size(); i++) {
		const auto &t = tr.tokens[i];
		if(t.token_len==sl && memcmp(t.token_start,s,sl)==0)
			return true;
	}
	return false;
}


int main(void) {
	{
		static const char html[] = "";
		Xml xml;
		assert(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
		TokenizerResult tr;
		xml_tokenizer_phase_1(&xml,&tr);
		assert(tr.tokens.empty());
	}
	{
		static const char html[] = "<html><title>zzz</title><body><h1>aaa</h2><p>bbb ccc</p></body></html>";
		Xml xml;
		assert(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
		TokenizerResult tr;
		xml_tokenizer_phase_1(&xml,&tr);
		assert(!tr.tokens.empty());
		assert(has_token(tr,"zzz"));
		assert(has_token(tr,"aaa"));
		assert(has_token(tr,"bbb"));
		assert(has_token(tr," "));
		assert(has_token(tr,"ccc"));
		assert(!has_token(tr,"body"));
		assert(!has_token(tr,"html"));
		assert(!has_token(tr,"title"));
		assert(!has_token(tr,"h1"));
		
		assert(tr.tokens.size()==15);
		//html
		assert(tr.tokens[0].start_pos==0);
		assert(tr.tokens[0].end_pos==6);
		assert(tr.tokens[0].token_start==html);
		assert(!tr.tokens[0].is_alfanum);
		assert(tr.tokens[0].nodeid==TAG_HTML);
		assert(tr.tokens[0].xml_node_index==0);
		//title
		assert(tr.tokens[1].start_pos==6);
		assert(tr.tokens[1].end_pos==13);
		assert(!tr.tokens[1].is_alfanum);
		assert(tr.tokens[1].nodeid==TAG_TITLE);
		assert(tr.tokens[1].xml_node_index==1);
		//zzz
		assert(tr.tokens[2].start_pos==13);
		assert(tr.tokens[2].end_pos==16);
		assert(tr.tokens[2].is_alfanum);
		assert(tr.tokens[2].nodeid==0);
		//title end
		assert(tr.tokens[3].start_pos==16);
		assert(tr.tokens[3].end_pos==24);
		assert(!tr.tokens[3].is_alfanum);
		assert(tr.tokens[3].nodeid&BACKBIT);
		assert(tr.tokens[3].xml_node_index=1);
		//body
		assert(tr.tokens[4].start_pos==24);
		assert(tr.tokens[4].end_pos==30);
		assert(!tr.tokens[4].is_alfanum);
		assert(tr.tokens[4].nodeid==TAG_BODY);
		//body
		assert(tr.tokens[5].start_pos==30);
		assert(tr.tokens[5].end_pos==34);
		assert(!tr.tokens[5].is_alfanum);
		assert(tr.tokens[5].nodeid==TAG_H1);
		//aaa
		assert(tr.tokens[6].start_pos==34);
		assert(tr.tokens[6].end_pos==37);
		assert(tr.tokens[6].token_len==3);
		assert(memcmp(tr.tokens[6].token_start,"aaa",3)==0);
		assert(tr.tokens[6].is_alfanum);
		assert(tr.tokens[6].nodeid==0);
		
		//good enough
	}
	
	{
		static const char html[] = "<html><title>zzz</title><body><h1>aaa</h2><p>H<sub>2</sub>O</p></body></html>";
		Xml xml;
		assert(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
		TokenizerResult tr;
		xml_tokenizer_phase_1(&xml,&tr);
		assert(!tr.tokens.empty());
		assert(has_token(tr,"zzz"));
		assert(has_token(tr,"H"));
		assert(has_token(tr,"2"));
		assert(has_token(tr,"O"));
		xml_tokenizer_phase_2(&xml,langUnknown,0,&tr);
		assert(has_token(tr,"H₂"));
		//assert(has_token(tr,"H2O")); //not implemented yet
		//assert(has_token(tr,"H₂O")); //not implemented yet
	}
	
	{
		static const char html[] = "<html><title>yyy</title>cd&shy;rom<body></body></html>";
		Xml xml;
		assert(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
		TokenizerResult tr;
		xml_tokenizer_phase_1(&xml,&tr);
		assert(!tr.tokens.empty());
		assert(has_token(tr,"yyy"));
		xml_tokenizer_phase_2(&xml,langUnknown,0,&tr);
		assert(has_token(tr,"cdrom"));
	}
	
	{
		static const char html[] = "<html><title>yyy</title>cd­rom<body></body></html>";
		Xml xml;
		assert(xml.set((char*)html,sizeof(html)-1, TITLEREC_CURRENT_VERSION, CT_HTML));
		TokenizerResult tr;
		xml_tokenizer_phase_1(&xml,&tr);
		assert(!tr.tokens.empty());
		assert(has_token(tr,"yyy"));
		xml_tokenizer_phase_2(&xml,langUnknown,0,&tr);
		assert(has_token(tr,"cdrom"));
	}
}
