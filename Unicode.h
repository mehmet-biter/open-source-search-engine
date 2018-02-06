#ifndef GB_UNICODE_H
#define GB_UNICODE_H

#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include "unicode/UCMaps.h"
#include <iconv.h>

// Initialize unicode word parser
bool 	ucInit(const char *path = NULL);
void ucResetMaps();

//////////////////////////////////////////////////////





// Try to detect the Byte Order Mark of a Unicode Document
const char *	ucDetectBOM(const char *buf, int32_t bufsize);

//////////////////////////////////////////////////////////////
//  Inline functions
//////////////////////////////////////////////////////////////

#endif // GB_UNICODE_H
