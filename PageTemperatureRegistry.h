#ifndef PAGETEMPERATUREREGISTRY_H_
#define PAGETEMPERATUREREGISTRY_H_

#include <inttypes.h>
#include <stddef.h>


//A registry of the "hotness" of documents, meaning how well-liked they are, how often referenced
//by credible news sites, how fresh it is, etc. The temperature is used for ranking search results.
class PageTemperatureRegistry {
	uint64_t *slot;
	size_t entries;
	unsigned hash_table_size;
	unsigned min_temperature;
	unsigned max_temperature;
	unsigned temperature_range_for_scaling;
	unsigned default_temperature;

	double min_temperature_log;
	double max_temperature_log;
	double temperature_range_for_scaling_log;
	double default_temperature_log;

	unsigned query_page_temperature_internal(uint64_t docid) const;
public:
	PageTemperatureRegistry()
	  : slot(0), entries(0),
	    min_temperature(0), max_temperature(10), default_temperature(5)
	    {}
	~PageTemperatureRegistry() { unload(); }
	
	bool load();
	void unload();
	
	double query_page_temperature(uint64_t docid, double range_min, double range_max) const;
	
	bool empty() const { return entries==0; }
};

extern PageTemperatureRegistry g_pageTemperatureRegistry;

#endif
