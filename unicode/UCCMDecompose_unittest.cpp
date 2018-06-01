#include "UCCMDecompose.h"
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
	
	dc = Unicode::recursive_combining_mark_decompose(0x0041, buf, 64);	//LATIN CAPITAL LETTER A
	assert(dc==0);
	
	dc = Unicode::recursive_combining_mark_decompose(0x00C7, buf, 64);	//LATIN CAPITAL LETTER C WITH CEDILLA
	assert(dc==2);
	assert(buf[0]==0x0043);
	assert(buf[1]==0x0327);
	
	dc = Unicode::recursive_combining_mark_decompose(0x01D7, buf, 64);	//LATIN CAPITAL LETTER U WITH DIAERESIS AND ACUTE
	assert(dc==3);
	assert(buf[0]==0x0055);
	assert(buf[1]==0x0308);
	assert(buf[2]==0x0301);

	dc = Unicode::recursive_combining_mark_decompose(0x01D7, buf, 2);	//LATIN CAPITAL LETTER U WITH DIAERESIS AND ACUTE
	assert(dc==0);
	
	//now the difference between the plain canonical decomposition and the combining-mark decomposition
	
	dc = Unicode::recursive_combining_mark_decompose(0x01C6, buf, 64);	//LATIN SMALL LETTER DZ WITH CARON
	assert(dc==3);
	assert(buf[0]==0x0064);
	assert(buf[1]==0x007A);
	assert(buf[2]==0x030C);
	
	
	//Test composition works
	
	UChar32 src[3],dst[3];
	unsigned dstlen;
	
	src[0] = 'A';
	src[1] = 0x0300; //grave
	dstlen = Unicode::iterative_combining_mark_compose(src,2,dst);
	assert(dstlen==1);
	assert(dst[0]==0x00C0); //LATIN CAPITAL LETTER A WITH GRAVE
	
	src[0] = 'Y';
	src[1] = 0x0301; //acute
	dstlen = Unicode::iterative_combining_mark_compose(src,2,dst);
	assert(dstlen==1);
	assert(dst[0]==0x00DD); //LATIN CAPITAL LETTER Y WITH ACUTE
	
	src[0] = 0x00E6;
	src[1] = 0x0301; //acute
	dstlen = Unicode::iterative_combining_mark_compose(src,2,dst);
	assert(dstlen==1);
	assert(dst[0]==0x01FD); //LATIN SMALL LETTER AE WITH ACUTE
	
	src[0] = 'Y';
	src[1] = 0x030A; //ring-above
	dstlen = Unicode::iterative_combining_mark_compose(src,2,dst);
	assert(dstlen==2);
	assert(dst[0]=='Y');
	assert(dst[1]==0x030A);
	
	src[0] = 0x0075;
	src[1] = 0x031B; //horn
	dstlen = Unicode::iterative_combining_mark_compose(src,2,dst);
	assert(dstlen==1);
	assert(dst[0]==0x01B0); //LATIN SMALL LETTER U WITH HORN
	

	return 0;
}
