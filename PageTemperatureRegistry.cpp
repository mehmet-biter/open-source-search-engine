#include "PageTemperatureRegistry.h"
#include "ScopedLock.h"
#include "ScalingFunctions.h"
#include "Log.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <math.h>
#include <float.h>      // FLT_EPSILON, DBL_EPSILON
#include <algorithm>


PageTemperatureRegistry g_pageTemperatureRegistry;
static GbMutex load_lock;

static const char filename[] = "page_temperatures.dat";


bool PageTemperatureRegistry::load() {
	ScopedLock sl(load_lock);
	log(LOG_DEBUG, "Loading %s", filename);

	struct stat st;
	if(stat(filename,&st)!=0) {
		log(LOG_WARN,"fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	unsigned new_active_index = 1-active_index;
	if(!mmf[new_active_index].open(filename)) {
		if(errno==ENOENT)
			log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		else
			log(LOG_WARN,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	if(mmf[new_active_index].size()%sizeof(uint64_t)) {
		log(LOG_WARN,"%s size is not a multiple of %zu", filename, sizeof(uint64_t));
		return false;
	}
	
	
	//ok, is the file sorted as we expect it to be?
	if(mmf[new_active_index].size() >= sizeof(uint64_t)*5) {
		//just probe 5 elements
		size_t c = mmf[new_active_index].size() / sizeof(uint64_t);
		auto e = reinterpret_cast<const uint64_t*>(mmf[new_active_index].start());
		if(e[c/5*0]<e[c/5*1] &&
		   e[c/5*1]<e[c/5*2] &&
		   e[c/5*2]<e[c/5*3] &&
		   e[c/5*3]<e[c-1  ])
			; //excellent
		else {
			log(LOG_WARN,"%s is not sorted. Regenerate or sort it", filename);
			return false;
		}
	}

	//Default temperature for unregistered pages is a bit tricky.
	//Initially an unregistered page is likely just freshly crawled but an old one. So the average
	//temperature is a good guess. On the other hand when we have crawled most of the internet
	//then an unregistered page indicates a new page and it like has low temperature.
	//There is no obvious correct value.

	//If there is a .meta file then use the values from that
	bool using_meta = false;
	char meta_filename[1024];
	sprintf(meta_filename,"%s.meta",filename);
	FILE *fp_meta = fopen(meta_filename,"r");
	if(fp_meta) {
		unsigned tmp_min_temperature;
		unsigned tmp_max_temperature;
		unsigned tmp_default_temperature;
		if(fscanf(fp_meta,"%u%u%u",&tmp_min_temperature,&tmp_max_temperature,&tmp_default_temperature)==3) {
			if(tmp_min_temperature<tmp_max_temperature &&
			   tmp_default_temperature>=tmp_min_temperature &&
			   tmp_default_temperature<=tmp_max_temperature)
			{
				min_temperature = tmp_min_temperature;
				max_temperature = tmp_max_temperature;
				default_temperature = tmp_default_temperature;
				using_meta = true;
			} else
				log(LOG_WARN,"Invalid values in %s", meta_filename);
		}
		fclose(fp_meta);
	}
	
	if(!using_meta) {
		//otherwise calculate min/max/avg
		unsigned new_min_temperature = 0x3ffffff;
		unsigned new_max_temperature = 0;
		auto *begin = reinterpret_cast<const uint64_t*>(mmf[new_active_index].start());
		auto end = begin + mmf[new_active_index].size()/sizeof(uint64_t);
		for(auto *e = begin; e<end; e++) {
			//uint64_t docid = *e>>26;
			unsigned temperature = *e&0x3ffffff;
			if(temperature<new_min_temperature) new_min_temperature=temperature;
			if(temperature>new_max_temperature) new_max_temperature=temperature;
		}
		min_temperature = new_min_temperature;
		max_temperature = new_max_temperature;
		default_temperature = (min_temperature+max_temperature)/2;
	}

	temperature_range_for_scaling = max_temperature-min_temperature;
	
	min_temperature_log = min_temperature>0 ? log(min_temperature) : DBL_EPSILON;
	max_temperature_log = log(max_temperature);
	temperature_range_for_scaling_log = log(temperature_range_for_scaling);
	default_temperature_log = log(default_temperature);

	if(!using_meta)
		log(LOG_WARN, "meta-file %s could not be loaded. Using default temperature of %u which can scew results for new pages", meta_filename, default_temperature);
	
	log(LOG_DEBUG, "pagetemp: min_temperature=%u",min_temperature);
	log(LOG_DEBUG, "pagetemp: max_temperature=%u",max_temperature);
	log(LOG_DEBUG, "pagetemp: default_temperature=%u",default_temperature);

	log(LOG_DEBUG, "%s loaded (%lu items)", filename, (unsigned long)mmf[new_active_index].size()/sizeof(uint64_t));
	
	//swap in and done.
	
	active_index.store(new_active_index,std::memory_order_release);

	stat_ino = st.st_ino;
	stat_mtime = st.st_mtime;
	
	return true;
}


void PageTemperatureRegistry::unload() {
	mmf[0].close();
	mmf[1].close();
	//min/max temperatures are kept as-is
}


void PageTemperatureRegistry::reload_if_needed() {
	struct stat st;
	if(stat(filename,&st)!=0)
		return;
	if(st.st_ino!=stat_ino || st.st_mtime!=stat_mtime)
		load();
}


unsigned PageTemperatureRegistry::query_page_temperature_internal(uint64_t docid) const {
	return query_page_temperature_internal(docid,default_temperature);
}


unsigned PageTemperatureRegistry::query_page_temperature_internal(uint64_t docid, unsigned raw_default) const {
	auto ai = active_index.load(std::memory_order_consume);
	auto start = reinterpret_cast<const uint64_t *>(mmf[ai].start());
	auto count = mmf[ai].size()/sizeof(uint64_t);
	auto end = start+count;
	
	auto pos = std::lower_bound(start, end, docid<<26);
	if(pos!=end && *pos>>26 == docid) {
		return *pos&0x3ffffff;
	}
	return raw_default;
}



bool PageTemperatureRegistry::query_page_temperature(uint64_t docid, double range_min, double range_max, double *temperature) const {
	auto ai = active_index.load(std::memory_order_consume);
	auto start = reinterpret_cast<const uint64_t *>(mmf[ai].start());
	auto count = mmf[ai].size()/sizeof(uint64_t);
	auto end = start+count;
	
	auto pos = std::lower_bound(start, end, docid<<26);
	if(pos!=end && *pos>>26 == docid) {
		*temperature = scale_temperature(range_min,range_max,*pos&0x3ffffff);
		return *pos&0x3ffffff;
	}
	return false;
}

double PageTemperatureRegistry::scale_temperature(double range_min, double range_max, unsigned raw_temperature) const {
	double temperature_26bit_log = log(raw_temperature);
	return scale_linear(temperature_26bit_log, min_temperature_log, max_temperature_log, range_min, range_max);
}

double PageTemperatureRegistry::query_default_page_temperature(double range_min, double range_max) const {
	return scale_linear(default_temperature_log, min_temperature_log, max_temperature_log, range_min, range_max);
}
