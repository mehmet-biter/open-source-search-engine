#include "Unicode.h"
#include "Sanity.h"
#include "Log.h"
#include "utf8.h"
#include <string.h>


#define VERIFY_UNICODE_CHECKSUMS 1

#define CHKSUM_UPPERMAP          1241336150
#define CHKSUM_LOWERMAP          1023166806
#define CHKSUM_PROPERTIES        33375957
#define CHKSUM_COMBININGCLASS    526097805
#define CHKSUM_SCRIPTS           1826246000
#define CHKSUM_KDMAP             1920116453

bool ucInit(const char *path) {

	char file[384];
	if (path == NULL) path = "./";

	// Might want to move this out of ucInit someday
	// but right now it's the only thing that uses .so files (?)
	char gbLibDir[512];
	snprintf(gbLibDir, 512, "%s/lib",path);
	// i don't think this is used any more because we don't have it!
	//log(LOG_INIT, "ucinit: Setting LD_RUN_PATH to \"%s\"",gbLibDir);
	if (setenv("LD_RUN_PATH", gbLibDir, 1)){
		log(LOG_INIT, "Failed to set LD_RUN_PATH");
	}
	//char *ldpath = getenv("LD_RUN_PATH");
	// i don't think this is used any more because we don't have it!
	//log(LOG_DEBUG, "ucinit: LD_RUN_PATH: %s\n", ldpath);


	strcpy(file, path);
	strcat(file, "/ucdata/uppermap.dat");
	if (!loadUnicodeTable(&g_ucUpperMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_UPPERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/lowermap.dat");
	if (!loadUnicodeTable(&g_ucLowerMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_LOWERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/properties.dat");
	if (!loadUnicodeTable(&g_ucProps, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_PROPERTIES))
		goto failed;

	strcpy(file, path);
	strcat(file, "/ucdata/scripts.dat");
	if (!loadUnicodeTable(&g_ucScripts, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_SCRIPTS))
		goto failed;

	// MDW: do we need this for converting from X to utf8? or for
	// the is_alnum(), etc. functions?
	if (!loadDecompTables(path)) {
		goto failed;
	}

	return true;
	
failed:
	log(LOG_WARN, "uni: unable to load all property tables");
	return false;
}

void ucResetMaps() {
	g_ucUpperMap.reset();
	g_ucLowerMap.reset();
	g_ucProps.reset();
	g_ucScripts.reset();
}

const char *ucDetectBOM(const char *buf, int32_t bufsize){
	if (bufsize < 4) return NULL;
	// copied from ICU
	if(buf[0] == '\xFE' && buf[1] == '\xFF') {
		return  "UTF-16BE";
	} else if(buf[0] == '\xFF' && buf[1] == '\xFE') {
		if(buf[2] == '\x00' && buf[3] =='\x00') {
			return "UTF-32LE";
		} else {
			return  "UTF-16LE";
		}
	} else if(buf[0] == '\xEF' && buf[1] == '\xBB' && buf[2] == '\xBF') {
		return  "UTF-8";
	} else if(buf[0] == '\x00' && buf[1] == '\x00' && 
		  buf[2] == '\xFE' && buf[3]=='\xFF') {
		return  "UTF-32BE";
	}

	return NULL;
}


void resetUnicode ( ) {
}
