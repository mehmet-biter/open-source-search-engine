#include "UCMaps.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>


UnicodeMaps::FullMap<Unicode::script_t>           UnicodeMaps::g_unicode_script_map;
UnicodeMaps::FullMap<Unicode::general_category_t> UnicodeMaps::g_unicode_general_category_map;
UnicodeMaps::FullMap<uint32_t>                    UnicodeMaps::g_unicode_properties_map;
UnicodeMaps::FullMap<bool>                        UnicodeMaps::g_unicode_unicode_is_alphabetic_map;
UnicodeMaps::FullMap<bool>                        UnicodeMaps::g_unicode_wordchars_map;
UnicodeMaps::SparseMap<UChar32>                   UnicodeMaps::g_unicode_lowercase_map;
UnicodeMaps::SparseMap<UChar32>                   UnicodeMaps::g_unicode_decomposition_map;


namespace {

template<class T>
bool load_map(T *map, const char *dir, const char *filename, const char **errstr) {
	//if a load() fails but errno is not set then it is a format/consistency error
	char full_filename[1024];
	sprintf(full_filename,"%s/%s",dir,filename);
	errno = 0;
	if(map->load(full_filename)) {
		return true;
	} else {
		if(errno)
			*errstr = strerror(errno);
		else {
			static char errmsg[sizeof(full_filename)+256];
			sprintf(errmsg,"Unicode map format/consistency error in %s",full_filename);
			*errstr = errmsg;
		}
		return false;
	}
}
}

bool UnicodeMaps::load_maps(const char *dir, const char **errstr) {
	return load_map(&g_unicode_script_map,dir,"unicode_scripts.dat",errstr) &&
	       load_map(&g_unicode_general_category_map,dir,"unicode_general_categories.dat",errstr) &&
	       load_map(&g_unicode_properties_map,dir,"unicode_properties.dat",errstr) &&
	       load_map(&g_unicode_wordchars_map,dir,"unicode_wordchars.dat",errstr) &&
	       load_map(&g_unicode_unicode_is_alphabetic_map,dir,"unicode_is_alphabetic.dat",errstr) &&
	       load_map(&g_unicode_lowercase_map,dir,"unicode_to_lowercase.dat",errstr) &&
	       load_map(&g_unicode_decomposition_map,dir,"unicode_canonical_decomposition.dat",errstr);
}
