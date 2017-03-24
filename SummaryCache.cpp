#include "SummaryCache.h"
#include "Mem.h"
#include "fctypes.h"
#include "ScopedLock.h"

SummaryCache g_stable_summary_cache;
SummaryCache g_unstable_summary_cache;



static const char memory_note[] = "cached_summary";


SummaryCache::SummaryCache()
  : m(),
    purge_iter(m.begin()),
    max_age(1000), //1 second
    max_memory(1000000), //1 megabyte
    memory_used(0),
    mtx()
{
}


void SummaryCache::configure(int64_t max_age_, size_t max_memory_)
{
	max_age = max_age_;
	max_memory = max_memory_;
}


void SummaryCache::clear()
{
	ScopedLock sl(mtx);
	for(std::map<int64_t,Item>::iterator iter = m.begin();
	    iter!=m.end();
	    ++iter)
		mfree(iter->second.data,iter->second.datalen,memory_note);
	m.clear();
	purge_iter = m.begin();
	memory_used = 0;
}


void SummaryCache::insert(int64_t key, const void *data, size_t datalen)
{
	ScopedLock sl(mtx);
	
	purge_step();
	
	if(max_age==0 || max_memory==0)
		return; //cache disabled
	
	std::map<int64_t,Item>::iterator iter = m.find(key);
	if(iter!=m.end()) {
		//remove the old entry first
		if(purge_iter==iter)
			++purge_iter;
		mfree(iter->second.data,iter->second.datalen,memory_note);
		memory_used -= iter->second.datalen;
		m.erase(iter);
	}
	
	Item item;
	item.timestamp = 0; //temporarily, for exception+memoryleak reason
	item.data = 0;
	item.datalen = 0;
	
	iter = m.insert(std::make_pair(key,item)).first;
	
	void *datacopy = mmalloc(datalen, memory_note);
	if(!datacopy) {
		m.erase(iter);
		return;
	}
	memcpy(datacopy,data,datalen);
	
	iter->second.data = datacopy;
	iter->second.datalen = datalen;
	iter->second.timestamp = gettimeofdayInMilliseconds();
	memory_used += datalen;
	
	if(memory_used>max_memory)
		forced_purge_step();
}


bool SummaryCache::lookup(int64_t key, const void **data, size_t *datalen)
{
	ScopedLock sl(mtx);
	
	purge_step();
	std::map<int64_t,Item>::iterator iter = m.find(key);
	if(iter!=m.end() && iter->second.timestamp+max_age>=gettimeofdayInMilliseconds()) {
		*data = iter->second.data;
		*datalen = iter->second.datalen;
		return true;
	} else
		return false;
}


void SummaryCache::purge_step()
{
	if(purge_iter==m.end())
		purge_iter = m.begin();
	else {
		int64_t now = gettimeofdayInMilliseconds();
		if(purge_iter->second.timestamp+max_age<now) {
			std::map<int64_t,Item>::iterator iter = purge_iter;
			++purge_iter;
			mfree(iter->second.data,iter->second.datalen,memory_note);
			memory_used -= iter->second.datalen;
			m.erase(iter);
		} else
			++purge_iter;
	}
}


void SummaryCache::forced_purge_step()
{
	if(purge_iter==m.end())
		purge_iter = m.begin();
	else {
		std::map<int64_t,Item>::iterator iter = purge_iter;
		++purge_iter;
		mfree(iter->second.data,iter->second.datalen,memory_note);
		memory_used -= iter->second.datalen;
		m.erase(iter);
	}
}
