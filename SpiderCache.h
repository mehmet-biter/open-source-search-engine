#ifndef GB_SPIDERCACHE_H
#define GB_SPIDERCACHE_H

#include "types.h"

class SpiderColl;

class SpiderCache {

public:

	// returns false and set g_errno on error
	bool init ( ) ;

	SpiderCache ( ) ;

	// what SpiderColl does a SpiderRec with this key belong?
	SpiderColl *getSpiderColl ( collnum_t collNum ) ;

	SpiderColl *getSpiderCollIffNonNull ( collnum_t collNum ) ;

	// called by main.cpp on exit to free memory
	void reset();

	void save ( bool useThread );

	bool needsSave ( ) ;
	void doneSaving ( ) ;
};

extern class SpiderCache g_spiderCache;

#endif //GB_SPIDERCACHE_H
