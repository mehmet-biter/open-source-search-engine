
// automated versioning for gigablast
#include <stdio.h>
#include "Mem.h"
#include "Log.h"
#include "Process.h"

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#ifndef GIT_COMMIT_ID
#define GIT_COMMIT_ID unknown
#endif

#ifndef BUILD_CONFIG
#define BUILD_CONFIG unknown
#endif

static char s_vbuf[32];

// includes \0
// "Sep 19 2014 12:10:58\0"
unsigned getVersionSize () {
	return 20 + 1;
}

char *getVersion ( ) {
	static bool s_init = false;
	if ( s_init ) {
		return s_vbuf;
	}

	sprintf(s_vbuf,"%s %s", __DATE__, __TIME__ );
	s_init = true;

	// PingServer.cpp needs this exactly to be 24
	if ( strlen(s_vbuf) != getVersionSize() - 1 ) { 
		log( LOG_ERROR, "getVersion: %s %" PRId32" != %" PRId32, s_vbuf, (int32_t)strlen(s_vbuf), getVersionSize() - 1);
		g_process.shutdownAbort(true); 
	}

	return s_vbuf;
}

const char* getBuildConfig() {
	return TO_STRING(BUILD_CONFIG);
}

const char* getCommitId() {
	return TO_STRING(GIT_COMMIT_ID);
}

void printVersion() {
	fprintf(stdout,"Gigablast Version      : %s\n", getVersion());
	fprintf(stdout,"Gigablast Build config : %s\n", getBuildConfig());
	fprintf(stdout,"Gigablast Git commit   : %s\n", getCommitId());
}

#undef STRINGIFY
#undef TO_STRING
