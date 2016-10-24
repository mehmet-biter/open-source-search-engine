#ifndef GB_MOVE_FILE2_H_
#define GB_MOVE_FILE2_H_

//Move/rename a file an a 2-phase commit model
int moveFile2Phase1(const char *src, const char *dst); //link or copy to destination
int moveFile2Phase2(const char *src, const char *dst); //unlink source

#endif
