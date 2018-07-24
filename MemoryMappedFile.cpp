#include "MemoryMappedFile.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>


bool MemoryMappedFile::open(const char *filename, bool for_write)
{
	close();
	
	int fd = ::open(filename,for_write ? O_RDWR : O_RDONLY);
	if(fd<0) {
		return false;
	}
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		::close(fd);
		return false;
	}
	
	int mmap_prot = for_write ? PROT_READ|PROT_WRITE : PROT_READ;
	int mmap_flags = MAP_SHARED;
	
	//note: mmap'in actual files with MAP_HUGETLB currently fails on all kernels but if someone some day add support for file-backed huge-tlb then good.
	pv = mmap(NULL, st.st_size, mmap_prot, mmap_flags|MAP_HUGETLB, fd, 0);
	if(pv==MAP_FAILED) //try without huge-tlb
		pv = mmap(NULL, st.st_size, mmap_prot, mmap_flags, fd, 0);
	if(pv==MAP_FAILED) {
		::close(fd);
		pv = NULL;
		return false;
	}
	::close(fd);
	
	sz = st.st_size;
	
	return true;
}


void MemoryMappedFile::close() {
	if(pv) {
		munmap(pv,sz);
		pv=0;
		sz=0;
	}
}


void MemoryMappedFile::advise_normal()     const { advise(MADV_NORMAL); }
void MemoryMappedFile::advise_random()     const { advise(MADV_RANDOM); }
void MemoryMappedFile::advice_sequential() const { advise(MADV_SEQUENTIAL); }
void MemoryMappedFile::advise_willneed()   const { advise(MADV_WILLNEED); }
void MemoryMappedFile::advise_dontneed()   const { advise(MADV_DONTNEED); }

void MemoryMappedFile::advise(int advice) const {
	if(pv)
		madvise(pv, sz, advice);
}
