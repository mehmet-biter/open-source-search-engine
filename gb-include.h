#ifndef GB_INCLUDE_H
#define GB_INCLUDE_H

//The two gbmem* functions below are legacy. Don't use them in new code
#define gbmemcpy(xx,yy,zz) memmove(xx,yy,zz)
#define memcpy_ass(xx,yy,zz) memmove(xx,yy,zz)

// do/while construct to make it usable like a function
// Do not add ending ; after while(0)
#define GB_DIE_HARD() 		\
	do { 			\
		abort(); 	\
	} while(0)



#include <inttypes.h>

#if __WORDSIZE == 64
#define PTRTYPE  uint64_t
#define SPTRTYPE int64_t
#define PTRFMT  "lx"
#endif

#if __WORDSIZE == 32
#define PTRTYPE  unsigned long //uint32_t
#define SPTRTYPE int32_t
#define PTRFMT  "lx"
#endif

#include <ctype.h>	// Log.h
#include <errno.h>	// Errno.h
#include <sys/errno.h>	// Errno.h
#include <stdarg.h>	// Log.h
#include <stdint.h>	// commonly included in include files
#include <stdio.h>	// commonly included in include files
#include <stdlib.h>	// commonly included in include files
#include <string.h>	// commonly included in include files
#include <unistd.h>	// commonly included in include files

#include "types.h"	// commonly included in includ files
#include "fctypes.h"	// commonly included in includ files
#include "hash.h"	// commonly included in includ files

#include "Errno.h"	// commonly included in include files
#include "Log.h"	// commonly included in include files

// cygwin fix
#ifndef O_ASYNC
#define O_ASYNC 0
#endif

#endif // GB_INCLUDE_H
