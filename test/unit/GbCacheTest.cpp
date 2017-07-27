#include <gtest/gtest.h>
#include "GbCache.h"

TEST(GbCacheTest, InsertLookup) {
	GbCache<int64_t, std::string> cache;
	cache.configure(60, 10, false);

	int64_t key = 1;
	cache.insert(key, std::to_string(key));

	std::string stored_data;
	EXPECT_TRUE(cache.lookup(key, &stored_data));
	EXPECT_STREQ(std::to_string(key).c_str(), stored_data.c_str());
}

TEST(GbCacheTest, InsertLookupExpired) {
	GbCache<int64_t, std::string> cache;
	cache.configure(1, 10, false);

	int64_t key = 1;
	cache.insert(key, std::to_string(key));
	sleep(2);

	std::string stored_data;
	EXPECT_FALSE(cache.lookup(key, &stored_data));
}

TEST(GbCacheTest, InsertLookupMaxed) {
	GbCache<int64_t, std::string> cache;
	cache.configure(60, 5, false);

	for (int64_t key = 1; key <= 10; ++key) {
		cache.insert(key, std::to_string(key));
	}

	for (int64_t key = 1; key <= 5; ++key) {
		std::string stored_data;
		EXPECT_FALSE(cache.lookup(key, &stored_data));
	}

	for (int64_t key = 6; key <= 10; ++key) {
		std::string stored_data;
		EXPECT_TRUE(cache.lookup(key, &stored_data));
		EXPECT_STREQ(std::to_string(key).c_str(), stored_data.c_str());
	}
}