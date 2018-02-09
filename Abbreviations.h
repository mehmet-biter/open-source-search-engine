// Matt Wells, copyright Jul 2001

#ifndef GB_ABBREVIATIONS_H
#define GB_ABBREVIATIONS_H
#include <inttypes.h>


// . is the word with this word id an abbreviation?
// . word id is just the hash64() of the word
bool isAbbr ( int64_t wid , bool *hasWordAfter = NULL ) ;

// to free the table's memory, Process::reset() will call this
void resetAbbrTable ( ) ;

#endif // GB_ABBREVIATIONS_H
