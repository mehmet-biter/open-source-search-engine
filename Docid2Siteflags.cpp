#include "Docid2Siteflags.h"
#include "Log.h"
#include "Conf.h"
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

struct Docid2FlagsAndSiteMapEntry {
	uint64_t flags      : 26;
	uint64_t docid      : 38;
	uint32_t sitehash32 : 32;
} __attribute__((packed, aligned(4)));




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
	
	struct stat st;
	if(stat(filename,&st)!=0) {
		log(LOG_WARN,"stat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}

	unsigned new_active_index = 1-active_index;
	if(!mmf[new_active_index].open(filename)) {
		if(errno==ENOENT)
			log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		else
			log(LOG_WARN,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	//load the entries in one go
	
	if(mmf[new_active_index].size()%sizeof(Docid2FlagsAndSiteMapEntry)) {
		log(LOG_WARN,"%s size is not a multiple of %zu", filename, sizeof(Docid2FlagsAndSiteMapEntry));
		return false;
	}
	
	//ok, is the file sorted as we expect it to be?
	if(mmf[new_active_index].size() >= sizeof(Docid2FlagsAndSiteMapEntry)*5) {
		//just probe 5 elements
		size_t c = mmf[new_active_index].size() / sizeof(Docid2FlagsAndSiteMapEntry);
		auto e = reinterpret_cast<const Docid2FlagsAndSiteMapEntry*>(mmf[new_active_index].start());
		if(e[c/5*0].docid<e[c/5*1].docid &&
		   e[c/5*1].docid<e[c/5*2].docid &&
		   e[c/5*2].docid<e[c/5*3].docid &&
		   e[c/5*3].docid<e[c-1  ].docid)
			; //excellent
		else {
			log(LOG_WARN,"%s is not sorted. Either regenerate it or use 'sort_docid2flagsandsitemap'", filename);
			return false;
		}
	}
	
	//swap in and done.
	
	active_index.store(new_active_index,std::memory_order_release);

	timestamp = st.st_mtime;

	log(LOG_DEBUG, "Loaded %s (%lu entries)", filename, (unsigned long)mmf[new_active_index].size()/sizeof(Docid2FlagsAndSiteMapEntry));
	return true;
}


void Docid2FlagsAndSiteMap::reload_if_needed() {
	struct stat st;
	if(stat(filename,&st)!=0)
		return; //probably not found
	if(timestamp==-1 || timestamp!=st.st_mtime)
		load();
}


void Docid2FlagsAndSiteMap::unload() {
	mmf[0].close();
	mmf[1].close();
}


bool Docid2FlagsAndSiteMap::lookupSiteHash(uint64_t docid, uint32_t *sitehash32) {
	Docid2FlagsAndSiteMapEntry tmp;
	tmp.docid = docid;
	tmp.flags = 0;
	auto ai = active_index.load(std::memory_order_consume);
	auto start = reinterpret_cast<const Docid2FlagsAndSiteMapEntry *>(mmf[ai].start());
	auto count = mmf[ai].size()/sizeof(Docid2FlagsAndSiteMapEntry);
	auto end = start+count;
	
	auto pos = std::lower_bound(start, end, tmp, cmp);
	if(pos!=end && pos->docid == docid) {
		*sitehash32 = pos->sitehash32;
		logTrace(g_conf.m_logTraceDocid2FlagsAndSiteMap, "Found record sitehash32=%u for docid=%lu", *sitehash32, docid);
		return true;
	}

	logTrace(g_conf.m_logTraceDocid2FlagsAndSiteMap, "Record not found for docid=%lu", docid);
	return false;
}


bool Docid2FlagsAndSiteMap::lookupFlags(uint64_t docid, unsigned *flags) {
	Docid2FlagsAndSiteMapEntry tmp;
	tmp.docid = docid;
	tmp.flags = 0;
	auto ai = active_index.load(std::memory_order_consume);
	auto start = reinterpret_cast<const Docid2FlagsAndSiteMapEntry *>(mmf[ai].start());
	auto count = mmf[ai].size()/sizeof(Docid2FlagsAndSiteMapEntry);
	auto end = start+count;
	
	auto pos = std::lower_bound(start, end, tmp, cmp);
	if(pos!=end && pos->docid == docid) {
		*flags = pos->flags;
		logTrace(g_conf.m_logTraceDocid2FlagsAndSiteMap, "Found record flags=%u for docid=%lu", *flags, docid);
		return true;
	}

	logTrace(g_conf.m_logTraceDocid2FlagsAndSiteMap, "Record not found for docid=%lu", docid);
	return false;
}
