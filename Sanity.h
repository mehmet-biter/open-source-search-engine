#ifndef GB_SANITY_H
#define GB_SANITY_H

// Ugly - but so is lots of code in .h files
#ifndef __lint /*flexelint cannot handle c++11 yet*/

[[ noreturn ]] void gbshutdownAbort( bool save_on_abort );
[[ noreturn ]] void gbshutdownResourceError();
[[ noreturn ]] void gbshutdownLogicError();
[[ noreturn ]] void gbshutdownCorrupted();

#else

void gbshutdownAbort( bool save_on_abort );
void gbshutdownLogicError();
void gbshutdownCorrupted();

#endif


#endif // GB_SANITY_H
