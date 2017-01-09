#ifndef GB_SANITY_H
#define GB_SANITY_H

// Ugly - but so is lots of code in .h files

[[ noreturn ]] void gbshutdownAbort( bool save_on_abort );
[[ noreturn ]] void gbshutdownResourceError();
[[ noreturn ]] void gbshutdownLogicError();
[[ noreturn ]] void gbshutdownCorrupted();

#endif // GB_SANITY_H
