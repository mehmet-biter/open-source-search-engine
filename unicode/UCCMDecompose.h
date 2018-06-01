#ifndef UCCMDECOMPOSE_H_
#define UCCMDECOMPOSE_H_
#include <inttypes.h>

typedef uint32_t UChar32;

namespace Unicode {

//Similar to recursie_canonical_decompose, but uses the combining-mark table instead
unsigned recursive_combining_mark_decompose(UChar32 c, UChar32 *buf, unsigned buflen);
//reverse operation
unsigned iterative_combining_mark_compose(UChar32 src[], unsigned srclen, UChar32 dst[]);

}

#endif
