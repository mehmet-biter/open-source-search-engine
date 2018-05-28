#ifndef TOKENIZER_UTIL_H
#define TOKENIZER_UTIL_H

#include "../utf8.h"

UChar32 normal_to_superscript_codepoint(UChar32 c); //returns 0 if there is none
UChar32 normal_to_subscript_codepoint(UChar32 c); //returns 0 if there is none


#endif
