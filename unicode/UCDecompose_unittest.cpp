#include "UCDecompose.h"
#include "UCMaps.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
	const char *errmsg=0;
	if(!UnicodeMaps::load_maps(".",&errmsg)) {
		fprintf(stderr,"%s\n",errmsg);
		return 1;
	}
	
	UChar32 buf[64];
	unsigned dc;
	
	dc = Unicode::recursive_canonical_decompose(0x0041, buf, 64);
	assert(dc==0);
	
	dc = Unicode::recursive_canonical_decompose(0x00C7, buf, 64);
	assert(dc==2);
	assert(buf[0]==0x0043);
	assert(buf[1]==0x0327);
	
	dc = Unicode::recursive_canonical_decompose(0x01D7, buf, 64);
	assert(dc==3);
	assert(buf[0]==0x0055);
	assert(buf[1]==0x0308);
	assert(buf[2]==0x0301);

	dc = Unicode::recursive_canonical_decompose(0x01D7, buf, 2);
	assert(dc==0);
	
	return 0;
}
