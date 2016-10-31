#ifndef GB_COPY_FILE_H_
#define GB_COPY_FILE_H_

//Copy a file. Returns non-zero on error and errno will be set
int copyFile(const char *src, const char *dst);

#endif
