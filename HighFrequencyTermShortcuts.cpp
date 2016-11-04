#include "HighFrequencyTermShortcuts.h"
#include "Log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif


//Useful if the user forces the search engine to search for high-frequency words
//such as "is", "the", "a" or non-english equivalents. The query is overly
//broad and unspecific but we still have to return something vaguely relevant
//without accepting a denial-of-service as it is.
//
// HighFrequencyTermShortcuts can load a binary file with pre-calculated PosDB
// entries for common terms.

/// Format of the data file:
// Simple and not small.
// Payload per termid is PosDB format compatible
// Output by 'termfreq_posdb_extract'
// 
// term_file ::=
// 	{ term }
// term ::=
// 	term_id, posdb_entry_count, {posdb_entry}
// 
// term_id ::=
// 	64bit                                               (*only 48 bits are actually used*)
// posdb_entry_count
// 	32bit
// posdb_entry ::=
// 	18 bytes
// 
// Integers are neither aligned nor padded. They are stored in host order.



HighFrequencyTermShortcuts g_hfts;

static const char filename[] = "high_frequency_term_posdb_shortcuts.dat";


//like memcmp() but compares in reverse direction
static int memcmpr(const void *p1_, const void *p2_, size_t sz) {
	const uint8_t *p1 = (const uint8_t*)p1_;
	const uint8_t *p2 = (const uint8_t*)p2_;
	for(size_t i=0; i<sz; i++) {
		uint8_t b1 = p1[sz-i-1];
		uint8_t b2 = p2[sz-i-1];
		if(b1<b2) return -1;
		if(b1>b2) return  1;
	}
	return 0;
}

static int cmp18(const void *p1, const void *p2) {
	return memcmpr(p1,p2,18);
}


bool HighFrequencyTermShortcuts::load()
{
	log(LOG_DEBUG, "Loading %s", filename);
	
	int fd = open(filename, O_RDONLY);
	if(fd<0) {
		log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	//load the shortcut file in one go
	
	struct stat st;
	if(fstat(fd,&st)!=0) {
		log(LOG_WARN,"fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		close(fd);
		return false;
	}
	
	char *new_buffer = new char[st.st_size];
	ssize_t bytes_read = read(fd,new_buffer,st.st_size);
	if(bytes_read!=st.st_size) {
		log(LOG_WARN,"fstat(%s) returned short count", filename);
		close(fd);
		delete[] new_buffer;
		return false;
	}
	
	close(fd);
	
	
	//parse content and set up pointers in term-entries
	std::map<uint64_t,TermEntry> new_entries;
	
	const char *end = new_buffer + st.st_size;
	char *p = new_buffer;
	while(p+8+4<=end) {
		uint64_t term_id = *(const uint64_t*)p;
		p += 8;
		uint32_t posdb_entries = *(const uint32_t*)p;
		p += 4;
		
		if(posdb_entries>100000)
			break; //invalid (highly unlikely)
		if(p + posdb_entries*18 >end)
			break; //invalid
		
		//entries per term are not guaranteed to be sorted. do that now
		qsort(p, posdb_entries, 18, cmp18);
		
		new_entries[term_id].p = p;
		new_entries[term_id].bytes = posdb_entries*18;
		if(posdb_entries>0) {
			memcpy(new_entries[term_id].start_key, p, 18);
			memcpy(new_entries[term_id].end_key, p+(posdb_entries-1)*18, 18);
		} else {
			memset(new_entries[term_id].start_key, 0, 18);
			memset(new_entries[term_id].end_key, 0, 18);
		}
		p += posdb_entries*18;
	}
	if(p!=end) {
		//truncated, overlong, invalid, or bogus file
		log(LOG_WARN,"Inconsistency or data error detected in %s", filename);
		delete[] new_buffer;
		return false;
	}
	
	//ok, content seem to check out.
	entries.swap(new_entries);
	if( buffer ) {
		delete[] (char*)buffer;
	}
	buffer = new_buffer;
	
	//All the entries are full 18-byte entries in all their glory
	//But PosdbTable::intersectLists10_r() doesn't like that and fails in
	//a "sanity check" due to unhealthy knowledge of not only the
	//posdb format but also the workings and algorithms.
	//So we have to compress the non-entries to 12 byte.
	//technically we should also compress to 6 bytes for same docids, but
	//we "know" that there aren't such entries in the shortcut file.
	for(std::map<uint64_t,TermEntry>::iterator iter = entries.begin();
	    iter!=entries.end();
	    ++iter)
	{
		const char *src = (const char*)iter->second.p;
		size_t src_bytes = iter->second.bytes;
		const char *src_end = src+src_bytes;
		char *dst = (char*)iter->second.p;
		size_t dst_bytes = 0;
		for(const char *p = src; p<src_end; p+=18) {
			if(p==src) {
				dst += 18;
				dst_bytes +=18;
			} else {
				memmove(dst,p,12);
				dst[0] |= 0x02; //now it's a 12-byte key
				dst += 12;
				dst_bytes += 12;
			}
		}
		iter->second.bytes = dst_bytes;
#ifdef _VALGRIND_
		VALGRIND_MAKE_MEM_UNDEFINED(dst, src_bytes-dst_bytes);
#endif
	}
	
	log(LOG_DEBUG, "%s loaded", filename);
	return true;
}


void HighFrequencyTermShortcuts::unload()
{
	entries.clear();
	if( buffer ) {
		delete[] (char*)buffer;
	}
	buffer = NULL;
}



bool HighFrequencyTermShortcuts::query_term_shortcut(uint64_t term_id,
                                                     const void **posdb_entries, size_t *bytes,
                                                     void *start_key, void *end_key)
{
	std::map<uint64_t,TermEntry>::const_iterator i = entries.find(term_id);
	if(i==entries.end())
		return false;
	else {
		*posdb_entries = i->second.p;
		*bytes = i->second.bytes;
		memcpy(start_key, i->second.start_key, 18);
		memcpy(end_key, i->second.end_key, 18);
		return true;
	}
}

