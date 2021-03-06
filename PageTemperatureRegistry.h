#ifndef PAGETEMPERATUREREGISTRY_H_
#define PAGETEMPERATUREREGISTRY_H_

#include "MemoryMappedFile.h"
#include <inttypes.h>
#include <stddef.h>
#include <sys/types.h>
#include <atomic>


//A registry of the "hotness" of documents, meaning how well-liked they are, how often referenced
//by credible news sites, how fresh it is, etc. The temperature is used for ranking search results.
class PageTemperatureRegistry {
	MemoryMappedFile mmf[2];
	std::atomic<unsigned> active_index;
	ino_t stat_ino;
	time_t stat_mtime;
	
	unsigned min_temperature;
	unsigned max_temperature;
	unsigned temperature_range_for_scaling;
	unsigned default_temperature;

	double min_temperature_log;
	double max_temperature_log;
	double temperature_range_for_scaling_log;
	double default_temperature_log;

	unsigned query_page_temperature_internal(uint64_t docid) const;
	unsigned query_page_temperature_internal(uint64_t docid, unsigned raw_default) const;
	
public:
	PageTemperatureRegistry()
	  : active_index(0), stat_ino(0), stat_mtime(-1),
	    min_temperature(0), max_temperature(10), default_temperature(5)
	    {}
	~PageTemperatureRegistry() { unload(); }
	
	bool load();
	void unload();
	void reload_if_needed();
	
	bool query_page_temperature(uint64_t docid, double range_min, double range_max, double *temperature) const;
	double scale_temperature(double range_min, double range_max, unsigned raw_temperature) const;
	double query_default_page_temperature(double range_min, double range_max) const;
	
	bool empty() const { return mmf[active_index].size()==0; }
};

extern PageTemperatureRegistry g_pageTemperatureRegistry;

#endif
