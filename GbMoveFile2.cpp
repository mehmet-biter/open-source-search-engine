#include "GbMoveFile2.h"
#include "GbCopyFile.h"
#include <unistd.h>
#include <errno.h>


int moveFile2Phase1(const char *src, const char *dst)
{
	//Remove destination. Ignore any errors.
	(void)unlink(dst);

	//Try making a hard link
	if(link(src, dst)==0) {
		//excellent
		return 0;
	}

	if(errno!=EXDEV)
		return -1;

	//Files are on different file systems. We must copy.
	return copyFile(src,dst);
}


int moveFile2Phase2(const char *src, const char *dst)
{
	if(access(dst,F_OK)!=0)
		return -1; //something has gone terribly wrong

	return unlink(src);
}

