#include "UCCMDecompose.h"
#include "UCMaps.h"
#include <string.h>


//Decompose a codepoint using both canonical and compatible decompositions
unsigned Unicode::recursive_combining_mark_decompose(UChar32 c, UChar32 *buf, unsigned buflen) {
	auto decompose_data = UnicodeMaps::g_unicode_combining_mark_decomposition_map.lookup(c);
	
	if(!decompose_data)
		return 0; //not decomposable
	
	if(decompose_data->count>buflen)
		return 0; //buffer too small. Act as if it cannot be decomposed.
	
	unsigned decomposed_count=0;
	for(unsigned i=0; i<decompose_data->count; i++) {
		unsigned dc = recursive_combining_mark_decompose(decompose_data->values[i],buf,buflen);
		if(dc==0) {
			buf[0] = decompose_data->values[i];
			buf++;
			buflen--;
			decomposed_count++;
		} else {
			buf += dc;
			buflen -= dc;
			decomposed_count += dc;
		}
		if(decompose_data->count-i > buflen)
			return 0;
	}
	return decomposed_count;
}


unsigned Unicode::iterative_combining_mark_compose(UChar32 src[], unsigned srclen, UChar32 dst[]) {
	memcpy(dst,src,srclen*4);
	unsigned dstlen = srclen;
	
	while(dstlen>=2) {
		auto c = UnicodeMaps::g_unicode_combining_mark_decomposition_map.reverse_lookup(dst[0],dst[1]);
		if(c!=0) {
			dst[0] = c;
			memmove(dst+1,dst+2, (dstlen-2)*4);
			dstlen--;
		} else
			break;
	}
	
	return dstlen;
}
