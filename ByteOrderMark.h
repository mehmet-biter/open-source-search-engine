#ifndef UNICODE_BOM_H_
#define UNICODE_BOM_H_
#include <inttypes.h>


//Try to detect the Byte Order Mark of a Unicode Document
//Returns either of:
//	"UTF-16BE"
//	"UTF-32LE"
//	"UTF-16LE"
//	"UTF-8"
//	"UTF-32BE"
//	NULL
const char *ucDetectBOM(const char *buf, int32_t bufsize);

#endif
