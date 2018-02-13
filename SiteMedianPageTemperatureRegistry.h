#ifndef SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#define SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#include <stddef.h>
#include <inttypes.h>
#include <sys/types.h>


class SiteMedianPageTemperatureRegistry {
	SiteMedianPageTemperatureRegistry(const SiteMedianPageTemperatureRegistry&) = delete;
	SiteMedianPageTemperatureRegistry& operator=(const SiteMedianPageTemperatureRegistry&) = delete;
public:
	SiteMedianPageTemperatureRegistry() : ptr(nullptr), bytes(0), stat_ino(0), stat_mtime(-1) {}
	~SiteMedianPageTemperatureRegistry() { unload(); }
	
	bool load();
	void unload();
	void reload_if_needed();
	
	bool lookup(uint32_t sitehash32, unsigned *default_site_page_temperature);
private:
	void *ptr;
	size_t bytes;
	ino_t stat_ino;
	time_t stat_mtime;
};


extern SiteMedianPageTemperatureRegistry g_smptr;
#endif
