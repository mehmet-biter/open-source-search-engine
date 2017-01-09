#ifndef GB_COMPRESS_H_
#define GB_COMPRESS_H_

#include <inttypes.h>
#include "zlib.h" // Z_OK, etc.

int gbuncompress(unsigned char *dest, uint32_t *destLen,
		 const unsigned char *source, uint32_t sourceLen);

int gbcompress(unsigned char *dest, uint32_t *destLen,
	       const unsigned char *source, uint32_t sourceLen);

#endif
