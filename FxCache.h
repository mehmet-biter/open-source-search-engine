//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#ifndef FX_FXCACHE_H
#define FX_FXCACHE_H

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

template <typename TKey, typename TData, typename TKeyHash = std::hash<TKey>>
class FxCache {
public:
	FxCache()
		: m_mtx()
		, m_queue()
		, m_map()
		, m_total_size(0)
		, m_max_age(300000) // 5 minutes
		, m_max_item(10000)
		, m_max_size(20000000) // 20 Mb
		, m_log_trace(false)
		, m_log_cache_name("cache") {
	}

	~FxCache() {
		clear();
	}

	void configure(int64_t max_age, size_t max_item, int64_t max_size, bool log_trace, const char *log_cache_name) {
		ScopedLock sl(m_mtx);

		m_max_age = max_age;
		m_max_item = max_item;
		m_max_size = max_size;

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

		CacheItem item(data);

		if (m_log_trace) {
			logTrace(m_log_trace, "inserting key='%s' size=%zu to %s", getKeyStr(key).c_str(), item.m_dataSize, m_log_cache_name);
		}

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
		m_total_size += item.m_dataSize;

		if (m_queue.size() > m_max_item || m_total_size > m_max_size) {
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
		if (map_it != m_map.end()) {
			if (expired(map_it->second)) {
				logTrace(m_log_trace, "expired key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
				return false;
			} else {
				logTrace(m_log_trace, "found key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
				*data = map_it->second.m_data;
				return true;
			}
		} else {
			logTrace(m_log_trace, "unable to find key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
			return false;
		}
	}

	bool remove(const TKey &key) {
		ScopedLock sl(m_mtx);

		// cache disabled
		if (disabled()) {
			return false;
		}

		auto map_it = m_map.find(key);
		if (map_it != m_map.end() && !expired(map_it->second)) {
			logTrace(m_log_trace, "removing key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
			m_total_size -= map_it->second.m_dataSize;
			m_map.erase(map_it);

			auto queue_it = std::find(m_queue.begin(), m_queue.end(), key);
			m_queue.erase(queue_it);
			return true;
		} else {
			logTrace(m_log_trace, "unable to find key='%s' in %s", getKeyStr(key).c_str(), m_log_cache_name);
			return false;
		}
	}

private:
	FxCache(const FxCache&);
	FxCache& operator=(const FxCache&);

	struct CacheItem {
		CacheItem(const TData &data)
			: m_timestamp(gettimeofdayInMilliseconds())
			, m_data(data)
			, m_dataSize(sizeof(data)) {
		}

		CacheItem(const TData &data, size_t dataSize)
			: m_timestamp(gettimeofdayInMilliseconds())
			, m_data(data)
			, m_dataSize(dataSize) {
		}

		int64_t m_timestamp;
		TData m_data;
		size_t m_dataSize;
	};

	bool expired(const CacheItem &item) const {
		return (item.m_timestamp + m_max_age < gettimeofdayInMilliseconds());
	}

	bool disabled() const {
		return (m_max_age == 0 || m_max_item == 0 || m_max_size == 0);
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
			logTrace(m_log_trace, "removing key='%s' in %s", getKeyStr(iter->first).c_str(), m_log_cache_name);
			m_total_size -= iter->second.m_dataSize;
			m_map.erase(iter);
			m_queue.pop_front();
		}
	}

	GbMutex m_mtx;
	std::deque<TKey> m_queue;                           // queue of items to expire, ordered by epiration time
	std::unordered_map<TKey,CacheItem,TKeyHash> m_map;  // cached items

	int64_t m_total_size;                               // total data size

	int64_t m_max_age;                                  // max item age (expiry) in msecs
	size_t m_max_item;                                  // maximum number of items
	int64_t m_max_size;

	bool m_log_trace;
	const char *m_log_cache_name;
};


#endif //FX_FXCACHE_H
