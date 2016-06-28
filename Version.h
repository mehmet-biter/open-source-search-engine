#ifndef GB_VERSION_H
#define GB_VERSION_H

int32_t getVersionSize () ;
char *getVersion ( ) ;
const char* getBuildConfig();
const char* getCommitId();

void printVersion();

#endif // GB_VERSION_H
