#include "UCDecompose.h"
#include "UCMaps.h"

//Unicode defines canonical decomposition as a way to decompose a codepoint into multiple codepoitns, typically a base character and some modifiers.
//Eg.:
//  0x0041 (A)	is already canonical
//  0x00C7 (Ç)	decomposes into 0x0043 (C) and 0x0327 (cedilla)
//  0x01D7 (Ǘ)	decomposes into 0x00DC (Ü) and 0x0301 (acute accent), and 0x00DC further decomposes into 0x0055 (U) and 0x0308 (diaresis)
//so it has to be done recursively (typically no more than 2 levels though)
//Unicode calls this "Normalization Form D (NFD)" / "Canonical Decomposition"
unsigned Unicode::recursive_canonical_decompose(UChar32 c, UChar32 *buf, unsigned buflen) {
	auto decompose_data = UnicodeMaps::g_unicode_canonical_decomposition_map.lookup(c);
	
	if(!decompose_data)
		return 0; //not decomposable
	
	if(decompose_data->count>buflen)
		return 0; //buffer too small. Act as if it cannot be decomposed.
	
	unsigned decomposed_count=0;
	for(unsigned i=0; i<decompose_data->count; i++) {
		unsigned dc = recursive_canonical_decompose(decompose_data->values[i],buf,buflen);
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
