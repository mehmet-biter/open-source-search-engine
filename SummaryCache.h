#ifndef GB_SUMMARYCACHE_H
#define GB_SUMMARYCACHE_H

#include <inttypes.h>
#include <stddef.h>
#include <map>
#include "GbMutex.h"

class SummaryCache {
	SummaryCache(const SummaryCache&);
	SummaryCache& operator=(const SummaryCache&);
	
	struct Item {
		int64_t timestamp;
		void *data;
		size_t datalen;
	};
	std::map<int64_t,Item> m;
	std::map<int64_t,Item>::iterator purge_iter;
	int64_t max_age;
	size_t max_memory;
	size_t memory_used;
	GbMutex mtx;
	
public:
	SummaryCache();
	~SummaryCache() { clear(); }

	void configure(int64_t max_age, size_t max_memory);

	void clear();

	void insert(int64_t key, const void *data, size_t datalen);
	bool lookup(int64_t key, const void **data, size_t *datalen);

private:
	void purge_step();
	void forced_purge_step();
};


extern SummaryCache g_stable_summary_cache;   //for summaries based on tags and no highlighting
extern SummaryCache g_unstable_summary_cache; //for summaries based on content or with highlighting

#endif
