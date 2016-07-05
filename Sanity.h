#ifndef GB_SANITY_H
#define GB_SANITY_H

// Ugly - but so is lots of code in .h files
#ifndef __lint /*flexelint cannot handle c++11 yet*/
[[ noreturn ]] void gbshutdownAbort( bool save_on_abort );
#else
void gbshutdownAbort( bool save_on_abort );
#endif

#endif // GB_SANITY_H
