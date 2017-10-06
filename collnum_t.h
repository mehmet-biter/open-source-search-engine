#ifndef COLLNUM_T_H_
#define COLLNUM_T_H_
#include <inttypes.h>

// Up to 32768 collections possible, MUST be signed
// A collnum_t of -1 is used by RdbCache to mean "no collection"
typedef int16_t collnum_t;

#endif
