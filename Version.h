#ifndef GB_VERSION_H
#define GB_VERSION_H
#include <inttypes.h>

int32_t getVersionSize () ;
char *getVersion ( ) ;
const char* getBuildConfig();
const char* getCommitId();

void printVersion(const char *name = "Gigablast");

#endif // GB_VERSION_H
