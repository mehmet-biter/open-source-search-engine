#ifndef SITEDEFAULTPAGETEMPERATUREREMOTEREGISTRY_H_
#define SITEDEFAULTPAGETEMPERATUREREMOTEREGISTRY_H_
#include <inttypes.h>


namespace SiteDefaultPageTemperatureRemoteRegistry {

bool initialize();
void finalize();


//Look up the site-default page temperature.
enum class lookup_result_t {
	error,                    //something went wrong, look for g_errno for details
	page_temperature_known,   //page-specific temperature is known, use that
	site_temperature_known,   //site-default temperature known, good
	site_unknown              //site unknown, use global default temperature
};
typedef void (*callback_t)(void *ctx, unsigned siteDefaultPageTemperature, lookup_result_t result);
bool lookup(int32_t sitehash32, int64_t docId, void *ctx, callback_t callback);


} //namespace


#endif
