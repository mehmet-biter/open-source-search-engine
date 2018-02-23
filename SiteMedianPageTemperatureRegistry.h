#ifndef SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#define SITE_MEDIAN_PAGE_TEMPERATURE_REGISTRY_H_
#include <inttypes.h>
#include <map>


class SiteMedianPageTemperatureRegistry {
	SiteMedianPageTemperatureRegistry(const SiteMedianPageTemperatureRegistry&) = delete;
	SiteMedianPageTemperatureRegistry& operator=(const SiteMedianPageTemperatureRegistry&) = delete;
public:
	SiteMedianPageTemperatureRegistry() : primary_fd(-1), secondary_fd(-1), primary_map(), secondary_map() {}
	~SiteMedianPageTemperatureRegistry() { close(); }
	
	bool open();
	void close();
	
	bool lookup(uint32_t sitehash32, unsigned *default_site_page_temperature) const;
	
	bool add(uint32_t sitehash32, unsigned default_site_page_temperature);
	
	bool prepare_new_generation();                     //prepare to make current data obsolete and replaced with new incoming data
	void switch_generation();                          //replace with data since preparation
private:
	int primary_fd;
	int secondary_fd;
	std::map<uint32_t,unsigned> primary_map;
	std::map<uint32_t,unsigned> secondary_map;
	bool load(int fd, std::map<uint32_t,unsigned> *m, const char *filename);
};


extern SiteMedianPageTemperatureRegistry g_smptr;
#endif
