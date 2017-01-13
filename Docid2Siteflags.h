#ifndef DOCID2FLAGSANDSITEMAP_H_
#define DOCID2FLAGSANDSITEMAP_H_

#include <inttypes.h>
#include <vector>
#include <atomic>


struct Docid2FlagsAndSiteMapEntry {
	uint64_t flags      : 26;
	uint64_t docid      : 38;
	uint32_t sitehash32 : 32;
} __attribute__((packed, aligned(4)));


class Docid2FlagsAndSiteMap {
	std::vector<Docid2FlagsAndSiteMapEntry> entries[2];
	std::atomic<unsigned> active_index;

public:
	Docid2FlagsAndSiteMap() : active_index(0) {}
	~Docid2FlagsAndSiteMap() {}
	
	bool load();
	void unload();
	
	bool empty() const { return entries[active_index].empty(); }
	
	bool lookupSiteHash(uint64_t docid, uint32_t *sitehash32);
	bool lookupFlags(uint64_t docid, unsigned *flags);
};

extern Docid2FlagsAndSiteMap g_d2fasm;

#endif
