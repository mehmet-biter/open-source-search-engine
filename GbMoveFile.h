#ifndef GB_MOVE_FILE_H_
#define GB_MOVE_FILE_H_

//Move/rename a file, even across devices. Returns non-zero on error and errno will be set
int moveFile(const char *src, const char *dst);

#endif
