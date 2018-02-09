#ifndef UCDECOMPOSE_H_
#define UCDECOMPOSE_H_
#include <inttypes.h>

typedef uint32_t UChar32;

namespace Unicode {

//Normalize a codepoint into its canonical sequence. Do this recursively until all codepoints are canonical.
//Puts result into 'buf' which must be at least 'buflen' elements long. Returns resulting
//length in codepoints. Or 0 if the source codepoint has no canonical decomposition (eg. non-decomposable 'A')
unsigned recursive_canonical_decompose(UChar32 c, UChar32 *buf, unsigned buflen);

}

#endif
