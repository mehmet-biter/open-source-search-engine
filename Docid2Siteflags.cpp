#include "Docid2Siteflags.h"
#include "Log.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <algorithm>


//format of docid->siteid file:
//  docid2siteid_file ::= { entry }
//  entry ::= flags26 | docid38 | siteid32
//  docid38::= 38-bit (actual) docid
//  flags26::= 26 bit flags for boolean lists
//  siteid32::= 32-bit hash of site (as per SiteGetter)
//Note: entries that have no bits set in flags26 are not written to output file.
//
// Important: the flags+docid fields are 64 bit in total. docid is in the high bits. this makes sorting+binary search easy.


Docid2FlagsAndSiteMap g_d2fasm;

static const char filename[] = "docid2flagsandsitemap.dat";

static bool cmp(const Docid2FlagsAndSiteMapEntry &e1, const Docid2FlagsAndSiteMapEntry &e2) {
	//Normally this would be:
	//  return e1.docid < e2.docid;
	//However, we do a dirty trick here: we just treat docid+flags as a uint64_t.
	//This works fine because we will not have duplicated docids in the table and we don't care about the flags.
	//This generates more efficient code than what gcc does with packed structs and bitfields.
	return *(const uint64_t*)&e1 < *(const uint64_t*)&e2;
}


bool Docid2FlagsAndSiteMap::load()
{
	log(LOG_DEBUG, "Loading %s", filename);
	
	int fd = open(filename, O_RDONLY);
	if(fd<0) {
		log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	//load the entries in one go
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		log(LOG_WARN,"fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		close(fd);
		return false;
	}
	
	if(st.st_size%sizeof(Docid2FlagsAndSiteMapEntry)) {
		log(LOG_WARN,"%s size is not a multiple of %zu", filename, sizeof(Docid2FlagsAndSiteMapEntry));
		close(fd);
		return false;
	}
	size_t entry_count = st.st_size/sizeof(Docid2FlagsAndSiteMapEntry);
	
	std::vector<Docid2FlagsAndSiteMapEntry> new_entries;
	new_entries.resize(entry_count);
	
	ssize_t bytes_read = read(fd, &(new_entries[0]), st.st_size);
	if(bytes_read!=st.st_size) {
		log(LOG_WARN,"read(%s) returned short count", filename);
		close(fd);
		return false;
	}
	
	close(fd);
	
	
	std::sort(new_entries.begin(), new_entries.end(), cmp);
	
	//swap in and done.
	
	unsigned new_active_index = 1-active_index;
	std::swap(entries[new_active_index],new_entries);
	active_index.store(new_active_index,std::memory_order_release);

	log(LOG_DEBUG, "Loaded %s (%lu entries)", filename, (unsigned long)entries [new_active_index].size());
	return true;
}


void Docid2FlagsAndSiteMap::unload() {
	entries[0].clear();
	entries[1].clear();
}


bool Docid2FlagsAndSiteMap::lookupSiteHash(uint64_t docid, uint32_t *sitehash32) {
	Docid2FlagsAndSiteMapEntry tmp;
	tmp.docid = docid;
	tmp.flags = 0;
	auto const &e = entries[active_index.load(std::memory_order_consume)];
	auto pos = std::lower_bound(e.begin(), e.end(), tmp, cmp);
	if(pos!=e.end()) {
		if(pos->docid == docid) {
			*sitehash32 = pos->sitehash32;
			return true;
		} else
			return false;
	} else
		return false;
}


bool Docid2FlagsAndSiteMap::lookupFlags(uint64_t docid, unsigned *flags) {
	Docid2FlagsAndSiteMapEntry tmp;
	tmp.docid = docid;
	tmp.flags = 0;
	auto const &e = entries[active_index.load(std::memory_order_consume)];
	auto pos = std::lower_bound(e.begin(), e.end(), tmp, cmp);
	if(pos!=e.end()) {
		if(pos->docid == docid) {
			*flags = pos->flags;
			return true;
		} else
			return false;
	} else
		return false;
}
