#include "SiteMedianPageTemperatureRegistry.h"
#include "Log.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


SiteMedianPageTemperatureRegistry g_smptr;

static const char filename[] = "site_median_page_temperatures.dat";

namespace {
struct Entry {
	uint32_t sitehash32;
	uint32_t median_pagerank;
};
}

bool SiteMedianPageTemperatureRegistry::load() {
	log(LOG_DEBUG, "Loading %s", filename);

	unload();
	int fd = open(filename,O_RDONLY);
	if(fd<0) {
		log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		log(LOG_WARN,"fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		close(fd);
		return false;
	}
	
	if(st.st_size%sizeof(Entry)) {
		log(LOG_WARN,"%s is size %lu which is not a multiple of 8. Damaged file?", filename, (unsigned long)st.st_size);
		close(fd);
		return false;
	}
	
	ptr = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if(ptr==MAP_FAILED) {
		log(LOG_WARN,"mmap(%s) failed, errno=%d (%s)", filename, errno, strerror(errno));
		close(fd);
		return false;
	}
	
	bytes = st.st_size;
	close(fd);
	
	log(LOG_DEBUG, "%s loaded (%lu items)", filename, bytes/sizeof(Entry));
	return true;
}


void SiteMedianPageTemperatureRegistry::unload() {
	if(ptr) {
		munmap(ptr,bytes);
		ptr = NULL;
		bytes = 0;
	}
}


static bool cmp(const Entry &e1, const Entry &e2) {
	return e1.sitehash32 < e2.sitehash32;
}

bool SiteMedianPageTemperatureRegistry::lookup(uint32_t sitehash32, unsigned *default_site_page_temperature) {
	const Entry *e=(const Entry*)ptr;
	size_t n = bytes/sizeof(Entry);
	Entry find_e;
	find_e.sitehash32=sitehash32;
	auto pos = std::lower_bound(e, e+n, find_e, cmp);
	if(pos!=e+n && pos->sitehash32==sitehash32) {
		*default_site_page_temperature = pos->median_pagerank;
		return true;
	} else
		return false;
}
