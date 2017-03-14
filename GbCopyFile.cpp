#include "GbCopyFile.h"
#include "Log.h"
#include "Sanity.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <sys/stat.h>


//note: keep copy functionality in-sync with moveFile()

static const size_t io_buffer_size = 1024*1024;

int copyFile(const char *src, const char *dst)
{
	//Allocate IO buffer
	char *buffer = new char[io_buffer_size];
	//Use O_DIRECT because the source file will soon be gone and shouldn't
	//pollute the kernels file system cache
	int fd_src = open(src,O_RDONLY|O_DIRECT);
	if(fd_src<0) {
	        int saved_errno = errno;
		if(errno==EINVAL) {
			//open(...O_DIRECT) can fail due to filesystem (eg. on development machines /tmp can be a
			//tmpfs which curiously doesn't support O_DIRECT. Other file systems, eg.  NFS have the same issue.
			fd_src = open(src,O_RDONLY);
			if(fd_src<0) {
				saved_errno = errno;
				log(LOG_ERROR,"copyFile:open(%s) failed with errno=%d (%s)", src, errno, strerror(errno));
				delete[] buffer;
				errno = saved_errno;
				return -1;
			}
		} else {
			log(LOG_ERROR,"copyFile:open(%s) failed with errno=%d (%s)", src, errno, strerror(errno));
			delete[] buffer;
			errno = saved_errno;
			return -1;
		}
	}
	//Tell the OS that we are going to read the source file sequentially
	(void)posix_fadvise(fd_src,0,0,POSIX_FADV_SEQUENTIAL);

	//Open destination. We use O_TRUNC instead of O_EXCL because if we crashed
	//during a copy the destination file will be left over (it is a bit risky).
	//We don't use O_DIRECT nor posix_fadvise(fd_dst,0,0,POSIX_FADV_SEQUENTIAL)
	//because the destination is likely to be used immediately when finished
	int fd_dst = open(dst,O_CREAT|O_TRUNC|O_WRONLY,0666);
	if(fd_dst<0) {
		int saved_errno = errno;
		log(LOG_ERROR,"move_file:open(%s) failed with errno=%d (%s)", dst,errno,strerror(errno));
		(void)close(fd_src);
		delete[] buffer;
		errno = saved_errno;
		return -1;
	}
	
	struct stat st_src;
	(void)fstat(fd_src, &st_src);
	
	//sendfile() should be the most efficient way, but it may fail on some (older) kernels
	long rc = sendfile(fd_dst, fd_src, NULL, st_src.st_size);
	if(rc<0) {
		//fallback:
		//if sendfile() fails, then do a traditional roll-your-own copy loop
		for(;;) {
			long bytes_read = read(fd_src, buffer, io_buffer_size);
			if(bytes_read==0)
				break;
			if(bytes_read<0) {
				int saved_errno = errno;
				log(LOG_ERROR,"moveFile:open(%s) failed with errno=%d (%s)", src,errno,strerror(errno));
				close(fd_src);
				close(fd_dst);
				unlink(dst);
				delete[] buffer;
				errno = saved_errno;
				return -1;
			}
			long bytes_written = write(fd_dst, buffer, (size_t)bytes_read);
			if(bytes_written!=bytes_read) {
				int saved_errno = errno;
				log(LOG_ERROR,"moveFile:write(%s) failed with errno=%d (%s)", dst,errno,strerror(errno));
				close(fd_src);
				close(fd_dst);
				unlink(dst);
				delete[] buffer;
				errno = saved_errno;
				return -1;
			}
		}
	}

	struct stat st_dst;
	(void)fstat(fd_dst, &st_dst);

	if (st_dst.st_size != st_src.st_size) {
		logError("copying file from src=%s to dst=%s ends up with different file sizes. src=%ld bytes dst=%ld bytes",
		         src, dst, st_src.st_size, st_dst.st_size);
		gbshutdownCorrupted();
	}

	close(fd_src);
	close(fd_dst);
	delete[] buffer;
    
	return 0;
}
