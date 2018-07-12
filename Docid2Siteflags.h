#ifndef DOCID2FLAGSANDSITEMAP_H_
#define DOCID2FLAGSANDSITEMAP_H_

#include "MemoryMappedFile.h"
#include <inttypes.h>
#include <atomic>


class Docid2FlagsAndSiteMap {
	MemoryMappedFile mmf[2];
	std::atomic<unsigned> active_index;
	long timestamp;

public:
	Docid2FlagsAndSiteMap() : active_index(0), timestamp(-1) {}
	~Docid2FlagsAndSiteMap() {}
	
	bool load();
	void reload_if_needed();
	void unload();
	
	bool empty() const { return mmf[active_index].size()==0; }
	
	bool lookupSiteHash(uint64_t docid, uint32_t *sitehash32);
	bool lookupFlags(uint64_t docid, unsigned *flags);
};

extern Docid2FlagsAndSiteMap g_d2fasm;

#endif
