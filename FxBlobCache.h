#ifndef FXBLOBCACHE_H_
#define FXBLOBCACHE_H_
#include "GbMutex.h"
#include "ScopedLock.h"
#include <stddef.h>
#include <time.h>
#include <map>

//A (yet another) cache:
//  - FIFO replacement policy
//  - Limited by memory and/or number of entries
//  - The valuetype is always raw memory
//  - Lookup() returns just reference to the memory
//  - Can be locked with a scoped lock (FxBlobCacheLock)

template<typename K> class FxBlobCacheLock;

template<typename K>
class FxBlobCache {
public:
	FxBlobCache();
	~FxBlobCache() { clear(); }
	FxBlobCache(const FxBlobCache&) = delete;
	FxBlobCache& operator=(const FxBlobCache&) = delete;
	
	void configure(unsigned max_items, size_t max_memory, time_t max_age);
	
	bool lookup(const K &key, void **ptr, size_t *size);
	bool insert(const K &key, const void *ptr, size_t size);
	void remove(const K &key);
	
	bool empty() const { return items==0; }
	void clear();
	
	struct Statistics {
		unsigned max_items, items;
		size_t max_memory, memory_used;
		unsigned long lookups, lookup_hits, lookup_misses;
		unsigned long inserts;
		unsigned long removes;
	};
	
	Statistics query_statistics();

private:
	GbMutex mtx;
	friend class FxBlobCacheLock<K>;
	struct Item {
		time_t timestamp;
		void *ptr;
		size_t size;
	};
	std::map<K,Item> key_map;
	std::multimap<time_t,K> timestamp_map;
	unsigned max_items;
	unsigned items;
	size_t max_memory;
	size_t memory_used;
	time_t max_age;
	//pure statistics
	unsigned long lookups;
	unsigned long lookup_hits;
	unsigned long lookup_misses;
	unsigned long inserts;
	unsigned long removes;
	
	void purge_one_item();
	void remove_(const K &key);
};

template<typename K>
class FxBlobCacheLock : public ScopedLock {
public:
	FxBlobCacheLock(FxBlobCache<K> &cache)
	  : ScopedLock(cache.mtx)
	{ }
};

#endif //FXBLOBCACHE_H_
