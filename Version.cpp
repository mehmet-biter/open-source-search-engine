
// automated versioning for gigablast
#include <stdio.h>
#include "Mem.h"
#include "Log.h"

static char s_vbuf[32];

// includes \0
// "Sep 19 2014 12:10:58\0"
int32_t getVersionSize () {
	return 20 + 1;
}

char *getVersion ( ) {
	static bool s_init = false;
	if ( s_init ) return s_vbuf;
	s_init = true;
	sprintf(s_vbuf,"%s %s", __DATE__, __TIME__ );
	// PingServer.cpp needs this exactly to be 24
	if ( gbstrlen(s_vbuf) != getVersionSize() - 1 ) { 
		log("getVersion: %s %" PRId32" != %" PRId32,
		    s_vbuf,
		    (int32_t)gbstrlen(s_vbuf),
		    getVersionSize() - 1);
		char *xx=NULL;*xx=0; 
	}
	return s_vbuf;
}

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#ifndef GIT_COMMIT_ID
#define GIT_COMMIT_ID unknown
#endif

#ifndef BUILD_CONFIG
#define BUILD_CONFIG unknown
#endif

void printVersion() {
	fprintf(stdout,"Gigablast Version      : %s\n", getVersion());
	fprintf(stdout,"Gigablast Build config : %s\n", TO_STRING(BUILD_CONFIG));
	fprintf(stdout,"Gigablast Git commit   : %s\n", TO_STRING(GIT_COMMIT_ID));
}

#undef STRINGIFY
#undef TO_STRING
