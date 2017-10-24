#include "FxBlobCache.h"
#include <string.h>

template<typename K>
FxBlobCache<K>::FxBlobCache()
  : mtx(),
    key_map(),
    timestamp_map(),
    max_items(0),
    items(0),
    max_memory(0),
    memory_used(0),
    max_age(0),
    lookups(0),
    lookup_hits(0),
    lookup_misses(0),
    inserts(0),
    removes(0)
{
}


template<typename K>
void FxBlobCache<K>::configure(unsigned max_items, size_t max_memory, time_t) {
	this->max_items = max_items;
	this->max_memory = max_memory;
	this->max_age = max_age;
}


template<typename K>
bool FxBlobCache<K>::lookup(const K &key, void **ptr, size_t *size) {
	lookups++;
	auto iter = key_map.find(key);
	if(iter==key_map.end()) {
		lookup_misses++;
		return false;
	}
	time_t now = time(0);
	time_t age = now - iter->second.timestamp;
	if(age>max_age) {
		lookup_misses++;
		return false;
	}
	*ptr = iter->second.ptr;
	*size = iter->second.size;
	lookup_hits++;
	return true;
}


template<typename K>
bool FxBlobCache<K>::insert(const K &key, const void *ptr, size_t size) {
	if(size>max_memory ||
	   max_memory==0 ||
	   max_items==0)
		return false; //there will never be room
	
	//remove current element, if any
	remove_(key);
	
	//purge items until there is room for the new item
	while(items+1>max_items)
		purge_one_item();
	while(memory_used+size>max_memory)
		purge_one_item();
	
	//slightly tedious because we want exception guarantees of consistent state and no memory leaks
	try {
		char *buf = new char[size];
	
		time_t now = time(0);
		Item item;
		item.timestamp = now;
		item.ptr = buf;
		memcpy(item.ptr, ptr, size);
		item.size = size;
		
		try {
			key_map[key] = item;
			try {
				timestamp_map.insert(std::make_pair(item.timestamp,key));
				
				items++;
				memory_used += size;
				inserts++;
				
				return true;
			} catch(std::bad_alloc&) {
				key_map.erase(key);
				throw;
			}
		} catch(std::bad_alloc&) {
			delete[] buf;
			throw;
		}
	} catch(std::bad_alloc&) {
		return false;
	}
}

template<typename K>
void FxBlobCache<K>::remove(const K &key) {
	remove_(key);
	removes++;
}

template<typename K>
void FxBlobCache<K>::remove_(const K &key) {
	auto key_iter = key_map.find(key);
	if(key_iter==key_map.end())
		return;
	auto timestamp_iter = timestamp_map.find(key_iter->second.timestamp);
	//find and remove the right timestamp map entry
	while(timestamp_iter!=timestamp_map.end()) {
		if(timestamp_iter->second==key) {
			timestamp_map.erase(timestamp_iter);
			break;
		}
		++timestamp_iter;
	}
	memory_used -= key_iter->second.size;
	delete[] (char*)(key_iter->second.ptr);
	key_map.erase(key_iter);
	items--;
}


template<typename K>
void FxBlobCache<K>::clear() {
	for(auto &e : key_map)
		delete[] (char*)(e.second.ptr);
	key_map.clear();
	timestamp_map.clear();
	items = 0;
	memory_used = 0;
	lookups = 0;
	lookup_hits = 0;
	lookup_misses = 0;
	inserts = 0;
}


template<typename K>
typename FxBlobCache<K>::Statistics FxBlobCache<K>::query_statistics() {
	Statistics statistics;
	statistics.max_items = max_items;
	statistics.items = items;
	statistics.max_memory = max_memory;
	statistics.memory_used = memory_used;
	statistics.lookups = lookups;
	statistics.lookup_hits = lookup_hits;
	statistics.lookup_misses = lookup_misses;
	statistics.inserts = inserts;
	statistics.removes = removes;
	return statistics;
}


template<typename K>
void FxBlobCache<K>::purge_one_item() {
	auto timestamp_iter = timestamp_map.begin();
	auto key_iter = key_map.find(timestamp_iter->second);
	memory_used -= key_iter->second.size;
	delete[] (char*)(key_iter->second.ptr);
	key_map.erase(key_iter);
	timestamp_map.erase(timestamp_iter);
	items--;
}


#ifdef UNITTEST
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
int main(void) {
	printf("Testing initialization\n");
	{
		FxBlobCache<int> cache;
		assert(cache.empty());
		auto statistics = cache.query_statistics();
		assert(statistics.max_items==0);
		assert(statistics.items==0);
		assert(statistics.max_memory==0);
		assert(statistics.memory_used==0);
		assert(statistics.lookups==0);
		assert(statistics.lookup_misses==0),
		assert(statistics.lookup_hits==0);
		assert(statistics.inserts==0);
		assert(statistics.removes==0);
	}
	
	printf("Testing empty lookup\n");
	{
		FxBlobCache<int> cache;
		void *ptr;
		size_t size;
		assert(!cache.lookup(0,&ptr,&size));
		auto statistics = cache.query_statistics();
		assert(statistics.lookups==1);
		assert(statistics.lookup_misses==1);
	}
	
	printf("Testing single-insert + lookup\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000,10);
		cache.insert(17,"abcd",4);
		auto statistics = cache.query_statistics();
		assert(statistics.inserts==1);
		void *ptr;
		size_t size;
		assert(cache.lookup(17,&ptr,&size));
		statistics = cache.query_statistics();
		assert(statistics.lookups==1);
		assert(statistics.lookup_hits==1);
		assert(statistics.items==1);
		assert(statistics.memory_used==4);
		assert(size==4);
		assert(memcmp(ptr,"abcd",4)==0);
	}
	
	printf("Testing single-insert + timeout\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000,1);
		cache.insert(17,"abcd",4);
		sleep(2);
		void *ptr;
		size_t size;
		assert(!cache.lookup(17,&ptr,&size));
	}
	
	printf("Testing insert+remove\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000,10);
		cache.insert(17,"abcd",4);
		cache.insert(42,"0123",4);
		cache.insert(117,"xyz",3);
		cache.remove(17);
		cache.remove(117);
		void *ptr;
		size_t size;
		assert(!cache.lookup(17,&ptr,&size));
		assert(cache.lookup(42,&ptr,&size));
		assert(!cache.lookup(117,&ptr,&size));
		auto statistics = cache.query_statistics();
		assert(statistics.items==1);
		assert(statistics.memory_used==4);
		assert(statistics.removes==2);
	}
	
	printf("Testing 2-insert+replacement\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000,1);
		cache.insert(17, "abcd",4);
		cache.insert(42, "0123",4);
		cache.insert(117,"xyz",3);
		cache.insert(17, "efgh",4);
		cache.insert(42, "4567",4);
		void *ptr;
		size_t size;
		assert(cache.lookup(17,&ptr,&size));
		assert(size==4);
		assert(memcmp(ptr,"efgh",4)==0);
		assert(cache.lookup(42,&ptr,&size));
		assert(size==4);
		assert(memcmp(ptr,"4567",4)==0);
		assert(cache.lookup(117,&ptr,&size));
		assert(size==3);
		assert(memcmp(ptr,"xyz",3)==0);
		auto statistics = cache.query_statistics();
		assert(statistics.items==3);
		assert(statistics.memory_used==11);
		assert(statistics.removes==0);
	}
	
	printf("Testing insert+clear\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000,1);
		cache.insert(17, "abcd",4);
		cache.insert(42, "0123",4);
		cache.clear();
		assert(cache.empty());
		void *ptr;
		size_t size;
		assert(!cache.lookup(17,&ptr,&size));
		assert(!cache.lookup(42,&ptr,&size));
		auto statistics = cache.query_statistics();
		assert(statistics.items==0);
		assert(statistics.memory_used==0);
	}
	
	printf("Testing max-items\n");
	{
		FxBlobCache<int> cache;
		cache.configure(100,1000000000,1000);
		for(int key=0; key<105; key++)
			cache.insert(key, "abcd",4);
		void *ptr;
		size_t size;
		assert(!cache.lookup(0,&ptr,&size));
		assert(cache.lookup(10,&ptr,&size));
		auto statistics = cache.query_statistics();
		assert(statistics.items==100);
		assert(statistics.removes==0);
	}
	
	printf("Testing max-memory\n");
	{
		FxBlobCache<int> cache;
		cache.configure(1000000000,1000,1000);
		for(int key=0; key<105; key++)
			cache.insert(key, "abcdefghij",10);
		void *ptr;
		size_t size;
		assert(!cache.lookup(0,&ptr,&size));
		assert(cache.lookup(10,&ptr,&size));
		auto statistics = cache.query_statistics();
		assert(statistics.memory_used<=1000);
		assert(statistics.memory_used>=1000-10);
		assert(statistics.removes==0);
	}
}
#endif
