#include "SiteDefaultPageTemperatureRemoteRegistry.h"


bool SiteDefaultPageTemperatureRemoteRegistry::initialize() {
	return true;
}


void SiteDefaultPageTemperatureRemoteRegistry::finalize() {
}


bool SiteDefaultPageTemperatureRemoteRegistry::lookup(int32_t /*sitehash32*/, int64_t /*docId*/, void * /*ctx*/, callback_t /*callback*/) {
	return false;
}
