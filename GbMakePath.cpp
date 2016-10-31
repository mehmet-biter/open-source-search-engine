#include "GbMakePath.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


int makePath(const char *path, int mode) {
	//first check if the path already exist
	struct stat st;
	if(stat(path,&st)==0) {
		if(S_ISDIR(st.st_mode))
			return 0; //excellent
		else {
			errno = ENOTDIR;
			return -1;
		}
	}
	
	//loop over the components in the path, creating them if necessary
	const char *path_end = path+strlen(path);
	for(const char *component_start = path; component_start<path_end; ) {
		const char *component_end = strchr(component_start+1,'/');
		if(!component_end) {
			component_end = component_start;
			while(*component_end)
				component_end++;
		}
		char buf[1024];
		memcpy(buf, path, (size_t)(component_end-path));
		buf[component_end-path] = '\0';
		if(stat(buf,&st)==0) {
			if(!S_ISDIR(st.st_mode)) {
				errno = ENOTDIR;
				return -1;
			}
		} else {
			int rc = mkdir(buf, mode);
			if(rc!=0)
				return -1;
		}
		
		component_start = component_end;
		if(*component_start=='/')
			component_start++;
	}

	return 0;	
}


#ifdef UNITTEST
#include <assert.h>

int main(void) {
	rmdir("/tmp/GbMakePath/dir1/dir2");
	rmdir("/tmp/GbMakePath/dir1");
	rmdir("/tmp/GbMakePath");

	assert(makePath("/tmp/GbMakePath/dir1/dir2",0777)==0);
	assert(access("/tmp/GbMakePath/dir1/dir2",X_OK)==0);
	rmdir("/tmp/GbMakePath/dir1/dir2");
	rmdir("/tmp/GbMakePath/dir1");
	rmdir("/tmp/GbMakePath");

	assert(makePath("/tmp/GbMakePath//dir1//dir2",0777)==0);
	assert(access("/tmp/GbMakePath/dir1/dir2",X_OK)==0);
	rmdir("/tmp/GbMakePath/dir1/dir2");
	rmdir("/tmp/GbMakePath/dir1");
	rmdir("/tmp/GbMakePath");

	assert(makePath("/tmp/GbMakePath/dir1/dir2",0777)==0);
	assert(makePath("/tmp/GbMakePath/dir1/dir2",0777)==0);
	assert(makePath("/tmp/GbMakePath/dir1",0777)==0);

	assert(makePath("/proc/foobooblarg",0777)!=0);

	return 0;
}
#endif
