#include "UCMap.h"
#include "UCEnums.h"
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>


template<class V>
void UnicodeMaps::FullMap<V>::clear() {
	if(value) {
		munmap((void*)(value),bytes);
		value=nullptr;
		values=0;
		bytes = 0;
	}
}


template<class V>
bool UnicodeMaps::FullMap<V>::load(const char *filename) {
	int fd = open(filename,O_RDONLY);
	if(fd<0)
		return false;
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		close(fd);
		return false;
	}
	
	bytes = (size_t)(st.st_size);
	if((bytes%sizeof(V)) != 0) {
		close(fd);
		return false;
	}
	
	void *ptr = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
	if(ptr==MAP_FAILED) {
		close(fd);
		return false;
	}
	
	close(fd);
	
	value = reinterpret_cast<const V*>(ptr);
	values = bytes/sizeof(V);
	
	return true;
}



template<class V>
void UnicodeMaps::SparseMap<V>::clear() {
	if(mmap_ptr) {
		m.clear();
		munmap(mmap_ptr,bytes);
		mmap_ptr = nullptr;
		bytes = 0;
	}
}


template<class V>
bool UnicodeMaps::SparseMap<V>::load(const char *filename) {
	int fd = open(filename,O_RDONLY);
	if(fd<0)
		return false;
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		close(fd);
		return false;
	}
	
	bytes = (size_t)(st.st_size);
	void *ptr = mmap(NULL, bytes, PROT_READ, MAP_SHARED, fd, 0);
	if(ptr==MAP_FAILED) {
		close(fd);
		return false;
	}
	
	close(fd);
	
	char *p = (char*)ptr;
	char *end = p + bytes;
	while(p+4+4<=end) {
		UChar32 codepoint = *(UChar32*)p;
		p += 4;
		Entry *e = (Entry*)p;
		if(e->count==0) return false;
		if(e->count>10) return false;
		
		p+= sizeof(e->count) + sizeof(e->values[0])*e->count;
		
		m[codepoint] = e;
		if(p>end)
			return false;
	}
	
	return true;
}


//explicit template instantiations
template class UnicodeMaps::FullMap<bool>;
template class UnicodeMaps::FullMap<Unicode::script_t>;
template class UnicodeMaps::FullMap<Unicode::general_category_t>;
template class UnicodeMaps::FullMap<uint32_t>;
template class UnicodeMaps::SparseMap<UChar32>;
