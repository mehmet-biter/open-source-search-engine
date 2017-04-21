#include "PageTemperatureRegistry.h"
#include "Log.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>


PageTemperatureRegistry g_pageTemperatureRegistry;

static const char filename[] = "page_temperatures.dat";


bool PageTemperatureRegistry::load() {
	log(LOG_DEBUG, "Loading %s", filename);

	FILE *fp = fopen(filename, "r");
	if(!fp) {
		log(LOG_INFO,"Couldn't open %s, errno=%d (%s)", filename, errno, strerror(errno));
		return false;
	}
	
	struct stat st;
	if(fstat(fileno(fp),&st)!=0) {
		log(LOG_WARN,"fstat(%s) failed with errno=%d (%s)", filename, errno, strerror(errno));
		fclose(fp);
		return false;
	}
	
	if(st.st_size%8) {
		log(LOG_WARN,"%s is size %lu which is not a multiple of 8. Damaged file?", filename, (unsigned long)st.st_size);
		fclose(fp);
		return false;
	}
	
	size_t new_entries = st.st_size/8;
	
	unsigned new_hash_table_size = (unsigned)(new_entries * 1.125);
	uint64_t *new_slot = new uint64_t[new_hash_table_size];
	memset(new_slot, 0, sizeof(uint64_t)*new_hash_table_size);
	
	unsigned new_min_temperature = 0x3ffffff;
	unsigned new_max_temperature = 0;
	uint64_t tmp_slot;
	while(fread(&tmp_slot,8,1,fp)==1) {
		uint64_t docid = tmp_slot>>26;
		unsigned temperature = tmp_slot&0x3ffffff;
		unsigned start_idx = ((uint32_t)docid) % new_hash_table_size;
		while(new_slot[start_idx])
			start_idx = (start_idx+1)%new_hash_table_size;
		new_slot[start_idx] = tmp_slot;
		if(temperature<new_min_temperature) new_min_temperature=temperature;
		if(temperature>new_max_temperature) new_max_temperature=temperature;
		
	}
	
	fclose(fp);
	
	delete[] slot;
	slot = new_slot;
	entries = new_entries;
	hash_table_size = new_hash_table_size;
	
	min_temperature = new_min_temperature;
	max_temperature = new_max_temperature;

	//Default temperature for unregistered pages is a bit tricky.
	//Initially an unregistered page is likely just freshly crawled but an old one. So the average
	//temperature is a good guess. On the other hand when we have crawled most of the internet
	//then an unregistered page indicates a new page and it like has low temperature.
	//There is no obvious correct value.
	default_temperature = (min_temperature+max_temperature)/2;

	//we have calculated min/max above. But if there is a .meta file then use the values from that
	bool using_meta = false;
	char meta_filename[1024];
	sprintf(meta_filename,"%s.meta",filename);
	FILE *fp_meta = fopen(meta_filename,"r");
	if(fp_meta) {
		unsigned tmp_min_temperature;
		unsigned tmp_max_temperature;
		unsigned tmp_default_temperature;
		if(fscanf(fp_meta,"%u%u%u",&tmp_max_temperature,&tmp_min_temperature,&tmp_default_temperature)==3) {
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

	temperature_range_for_scaling = max_temperature-min_temperature;
	
	if(!using_meta)
		log(LOG_WARN, "meta-file %s could not be loaded. Using default temperature of %u which can scew results for new pages", meta_filename, default_temperature);
	
	log(LOG_DEBUG, "pagetemp: min_temperature=%u",min_temperature);
	log(LOG_DEBUG, "pagetemp: max_temperature=%u",max_temperature);
	log(LOG_DEBUG, "pagetemp: default_temperature=%u",default_temperature);
	
	log(LOG_DEBUG, "%s loaded (%lu items)", filename, (unsigned long)new_entries);
	return true;
}


void PageTemperatureRegistry::unload() {
	delete[] slot;
	slot = 0;
	entries = 0;
	//min/max temperatures are kept as-is
}


unsigned PageTemperatureRegistry::query_page_temperature_internal(uint64_t docid) const {
	unsigned idx = ((uint32_t)docid) % hash_table_size;
	while(slot[idx]) {
		if(slot[idx]>>26 == docid)
			return slot[idx]&0x3ffffff;
		idx = (idx+1)%hash_table_size;
	}
	//Unregistered page. Return an default temperature
	return default_temperature;
}


double PageTemperatureRegistry::query_page_temperature(uint64_t docid) const {
	if(hash_table_size==0)
		return 1.0;
	unsigned temperature_26bit = query_page_temperature_internal(docid);
	//Then scale to a number in the rangte [0..1]
	//It is a bit annoying to do this computation for each lookup but it saves memory
	return ((double)(temperature_26bit - min_temperature)) / temperature_range_for_scaling;
}
