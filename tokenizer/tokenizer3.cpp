#include "tokenizer.h"
#include "Xml.h"

void xml_tokenizer_phase_1(const Xml *xml, TokenizerResult *tr) {
	if(xml->getNumNodes()==0)
		return;
	const char *first_pos = xml->getNode(0);
	
	for(int i=0; i<xml->getNumNodes(); i++) {
		const char *node = xml->getNode(i);
		int node_len = xml->getNodeLen(i);
		
		if(!xml->isTag(i)) {
			//Not a tag. Just run a plain tokenizer on the contained text
			plain_tokenizer_phase_1_downcall(node,node_len, node-first_pos, tr);
		} else {
			//tag
			nodeid_t node_id = xml->getNodeId(i);
			if(xml->isBackTag(i))
				node_id |= BACKBIT;
			tr->tokens.emplace_back(node-first_pos, node-first_pos+node_len, node,node_len, node_id,i);
		}
	}
}
