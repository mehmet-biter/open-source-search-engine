#ifndef GB_GBCACHE_H
#define GB_GBCACHE_H

#include <inttypes.h>
#include <stddef.h>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <sstream>
#include "Mem.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "fctypes.h"
#include "Log.h"

template <typename TKey, typename TData>
class GbCache {
public:
	GbCache()
		: m_mtx()
		, m_queue()
		, m_map()
		, m_max_age(300) // 5 minutes
		, m_max_item(10000)
		, m_log_trace(false)
		, m_log_cache_name("cache") {
	}

	~GbCache() {
		clear();
	}

	void configure(int64_t max_age, size_t max_item, bool log_trace, const char *log_cache_name) {
		ScopedLock sl(m_mtx);

		m_max_age = max_age;
		m_max_item = max_item;
		m_log_trace = log_trace;
		m_log_cache_name = log_cache_name;

		if (disabled()) {
			clear_unlocked();
		}
	}

	void clear() {
		ScopedLock sl(m_mtx);
		clear_unlocked();
	}

	void insert(const TKey &key, const TData &data) {
		ScopedLock sl(m_mtx);

		// cache disabled
		if (disabled()) {
			return;
		}

		purge_step();

		if (m_log_trace) {
			logTrace(m_log_trace, "inserting key='%s' to %s", getKeyStr(key).c_str(), m_log_cache_name);
		}

		CacheItem item(data);

		auto map_it = m_map.find(key);
		if (map_it == m_map.end()) {
			m_map.insert(std::make_pair(key, item));
		} else {
			map_it->second = item;
			auto queue_it = std::find(m_queue.begin(), m_queue.end(), key);
			if (queue_it != m_queue.end()) {
				m_queue.erase(queue_it);
			}
		}

		m_queue.push_back(key);

		if (m_queue.size() > m_max_item) {
			purge_step(true);
		}
	}

	bool lookup(const TKey &key, TData *data) {
		ScopedLock sl(m_mtx);

		// cache disabled
		if (disabled()) {
			return false;
		}

		purge_step();

		auto map_it = m_map.find(key);
		if (map_it != m_map.end() && !expired(map_it->second)) {
			logTrace(m_log_trace, "found key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
			*data = map_it->second.m_data;
			return true;
		} else {
			logTrace(m_log_trace, "unable to find key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
			return false;
		}
	}

private:
	GbCache(const GbCache&);
	GbCache& operator=(const GbCache&);

	struct CacheItem {
		CacheItem(const TData &data)
			: m_timestamp(getTime())
			, m_data(data) {
		}

		int64_t m_timestamp;
		TData m_data;
	};

	bool expired(const CacheItem &item) const {
		return (item.m_timestamp + m_max_age < getTime());
	}

	bool disabled() const {
		return (m_max_age == 0 || m_max_item == 0);
	}

	void clear_unlocked() {
		m_map.clear();
		m_queue.clear();
	}

	static std::string getKeyStr(const TKey &key) {
		std::stringstream os;
		os << key;
		return os.str();
	}

	void purge_step(bool forced=false) {
		if (m_queue.empty()) {
			return;
		}

		auto iter = m_map.find(m_queue.front());
		assert(iter != m_map.end());

		if (forced || expired(iter->second)) {
			m_map.erase(iter);
			m_queue.pop_front();
		}
	}

	GbMutex m_mtx;
	std::deque<TKey> m_queue;
	std::unordered_map<TKey,CacheItem> m_map;

	int64_t m_max_age;
	size_t m_max_item;

	bool m_log_trace;
	const char *m_log_cache_name;
};


#endif //GB_GBCACHE_H
