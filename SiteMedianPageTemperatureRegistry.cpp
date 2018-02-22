#include "SiteMedianPageTemperatureRegistry.h"
#include "ScopedLock.h"
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
static GbMutex mtx;

static const char primary_filename[] = "site_median_page_temperatures.dat";
static const char secondary_filename[] = "site_median_page_temperatures.dat.new";

namespace {
struct Entry {
	uint32_t sitehash32;
	uint32_t default_pagerank;
};
}


bool SiteMedianPageTemperatureRegistry::open() {
	ScopedLock sl(mtx);
	int fd = ::open(primary_filename,O_RDWR|O_APPEND|O_CREAT,0666);
	if(fd<0) {
		log(LOG_ERROR,"open(%s) failed, errno=%d (%s)", primary_filename, errno, strerror(errno));
		return false;
	}
	if(!load(fd,&primary_map,primary_filename)) {
		::close(fd);
		return false;
	}
	primary_fd = fd;
	
	
	fd = ::open(secondary_filename,O_RDWR|O_APPEND);
	if(fd>=0) {
		if(!load(fd,&secondary_map,secondary_filename)) {
			::close(fd);
			::close(primary_fd);
			primary_fd=-1;
			primary_map.clear();
			return false;
		}
		secondary_fd = fd;
	} else if(errno==ENOENT) {
		; //ok
	} else {
		log(LOG_ERROR,"open(%s) failed, errno=%d (%s)", secondary_filename, errno, strerror(errno));
		return false;
	}
	
	return true;
}

bool SiteMedianPageTemperatureRegistry::load(int fd, std::map<uint32_t,unsigned> *m, const char *filename) {
	log(LOG_INFO,"Loading %s", filename);
	struct stat st;
	if(fstat(fd,&st)!=0) {
		log(LOG_ERROR,"fstat(%s) failed, errno=%d (%s)", filename, errno, strerror(errno));
		::close(fd);
		return false;
	}
	if(st.st_size%sizeof(Entry)) {
		log(LOG_ERROR,"%s size is wrong", filename);
		::close(fd);
		return false;
	}
	
	static const size_t N=64*1024/sizeof(Entry);
	Entry e[N];
	for(;;) {
		ssize_t bytes_read = read(fd, e, sizeof(e));
		if(bytes_read<0) {
			log(LOG_ERROR,"read(%s) failed, errno=%d (%s)", filename, errno, strerror(errno));
			return false;
		} else if(bytes_read==0) {
			break; //eof
		}
		unsigned n = bytes_read/sizeof(Entry);
		for(unsigned i=0; i<n; i++)
			m->emplace(e[i].sitehash32,e[i].default_pagerank);
	}
	
	log(LOG_INFO,"Loaded %s", filename);
	return true;
}


void SiteMedianPageTemperatureRegistry::close() {
	if(primary_fd>=0) {
		::close(primary_fd);
		primary_fd = -1;
	}
	primary_map.clear();
	if(secondary_fd>=0) {
		::close(secondary_fd);
		secondary_fd = -1;
	}
	secondary_map.clear();
}


bool SiteMedianPageTemperatureRegistry::lookup(uint32_t sitehash32, unsigned *default_site_page_temperature) const {
	ScopedLock sl(mtx);
	auto iter = primary_map.find(sitehash32);
	if(iter!=primary_map.end()) {
		*default_site_page_temperature = iter->second;
		return true;
	} else
		return false;
}


bool SiteMedianPageTemperatureRegistry::add(uint32_t sitehash32, unsigned default_site_page_temperature) {
	ScopedLock sl(mtx);
	//if the value hasn't changed then there is no need for appending to the files
	auto iter = primary_map.find(sitehash32);
	if(iter!=primary_map.end() || iter->second==default_site_page_temperature)
		return true;

	
	Entry e;
	e.sitehash32 = sitehash32;
	e.default_pagerank = default_site_page_temperature;
	ssize_t bytes_written;
	bytes_written = write(primary_fd,&e,sizeof(e));
	if(bytes_written!=sizeof(e)) {
		log(LOG_ERROR,"Could not append to %s, errno=%d (%s)", primary_filename, errno, strerror(errno));
		return false;
	}
	if(secondary_fd>=0) {
		bytes_written = write(secondary_fd,&e,sizeof(e));
		if(bytes_written!=sizeof(e)) {
			log(LOG_ERROR,"Could not append to %s, errno=%d (%s)", secondary_filename, errno, strerror(errno));
			return false;
		}
	}
	primary_map[sitehash32] = default_site_page_temperature;
	if(secondary_fd>=0)
		secondary_map[sitehash32] = default_site_page_temperature;
	return true;
}


bool SiteMedianPageTemperatureRegistry::prepare_new_generation() {
	ScopedLock sl(mtx);
	if(secondary_fd>=0) {
		//preparing again means throwing away the current secondary
		::close(secondary_fd);
		secondary_fd = -1;
		::unlink(secondary_filename);
		secondary_map.clear();
	}
	
	secondary_fd = ::open(secondary_filename, O_RDWR|O_APPEND|O_CREAT|O_TRUNC,0666);
	if(secondary_fd<0) {
		log(LOG_ERROR,"open(%s) failed, errno=%d (%s)", secondary_filename, errno, strerror(errno));
		return false;
	}
	return true;
}


void SiteMedianPageTemperatureRegistry::switch_generation() {
	ScopedLock sl(mtx);
	if(secondary_fd<0) {
		log(LOG_WARN,"SiteMedianPageTemperatureRegistry::switch_generation() called, but there is no new generation in progress");
		return;
	}
	
	if(::rename(secondary_filename,primary_filename)<0) {
		log(LOG_ERROR,"rename(%s -> %s) failed, errno=%d (%s)", secondary_filename, primary_filename, errno, strerror(errno));
		return;
	}
	::close(primary_fd);
	primary_fd = secondary_fd;
	secondary_fd = -1;
	std::swap(primary_map,secondary_map);
	secondary_map.clear();
}
