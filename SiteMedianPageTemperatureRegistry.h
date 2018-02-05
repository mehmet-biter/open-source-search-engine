#ifndef SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#define SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#include <stddef.h>
#include <inttypes.h>


class SiteMedianPageTemperatureRegistry {
	SiteMedianPageTemperatureRegistry(const SiteMedianPageTemperatureRegistry&) = delete;
	SiteMedianPageTemperatureRegistry& operator=(const SiteMedianPageTemperatureRegistry&) = delete;
public:
	SiteMedianPageTemperatureRegistry() : ptr(nullptr), bytes(0) {}
	~SiteMedianPageTemperatureRegistry() { unload(); }
	
	bool load();
	void unload();
	
	bool lookup(uint32_t sitehash32, unsigned *default_site_page_temperature);
private:
	void *ptr;
	size_t bytes;
};


extern SiteMedianPageTemperatureRegistry g_smptr;
#endif
