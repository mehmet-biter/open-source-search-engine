#include "ByteOrderMark.h"
#include <stddef.h>


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
