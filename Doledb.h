// Matt Wells, copyright Nov 2002

#ifndef GB_DOLEDB_H
#define GB_DOLEDB_H


#include "Rdb.h"
#include "types.h"
#include "hash.h"
#include "Sanity.h"


class Msg5;
class RdbList;

void gotDoledbListWrapper2 ( void *state , RdbList *list , Msg5 *msg5 ) ;


// . store urls that can be spidered right NOW in doledb
// . SpiderLoop.cpp doles out urls from its local spiderdb into 
//   the doledb rdb of remote hosts (and itself as well sometimes!)
// . then each host calls SpiderLoop::spiderDoledUrls() to spider the
//   urls doled to their group (shard) in doledb
class Doledb {
public:

	void reset();
	
	bool init ( );

	// . see "overview of spidercache" below for key definition
	// . these keys when hashed are clogging up the hash table
	//   so i am making the 7 reserved bits part of the urlhash48...
	static key96_t makeKey(int32_t priority, uint32_t spiderTime, int64_t urlHash48, bool isDelete) {
		// sanity checks
		if ( priority  & 0xffffff00           ) { gbshutdownAbort(true); }
		if ( urlHash48 & 0xffff000000000000LL ) { gbshutdownAbort(true); }
		key96_t k;
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		k.n1 |= (spiderTime >>8);
		k.n0 = spiderTime & 0xff;
		k.n0 <<= 48;
		k.n0 |= urlHash48;
		// 7 bits reserved
		k.n0 <<= 7;
		// still reserved but when adding to m_doleReqTable it needs
		// to be more random!! otherwise the hash table is way slow!
		k.n0 |= (urlHash48 & 0x7f);
		// 1 bit for negative bit
		k.n0 <<= 1;
		// we are positive or not? setting this means we are positive
		if ( ! isDelete ) k.n0 |= 0x01;
		return k;
	}

	static key96_t makeFirstKey2 ( int32_t priority ) {
		key96_t k;
		k.setMin();
		// set priority
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		return k;
	}

	static key96_t makeLastKey2 ( int32_t priority ) {
		key96_t k;
		k.setMax();
		// set priority
		k.n1 = (255 - priority);
		k.n1 <<= 24;
		k.n1 |= 0x00ffffff;
		return k;
	}

	static int32_t getPriority(key96_t *k) {
		return 255 - ((k->n1 >> 24) & 0xff);
	}

	static int32_t getSpiderTime(key96_t *k) {
		uint32_t spiderTime = (k->n1) & 0xffffff;
		spiderTime <<= 8;
		// upper 8 bits of k.n0 are lower 8 bits of spiderTime
		spiderTime |= (uint32_t)((k->n0) >> (64-8));
		return (int32_t)spiderTime;
	}

	static int32_t getIsDel(key96_t *k) {
		if ( (k->n0 & 0x01) ) return 0;
		return 1;
	}

	static int64_t getUrlHash48 ( key96_t *k ) {
		return (k->n0>>8)&0x0000ffffffffffffLL;
	}

	Rdb *getRdb() { return &m_rdb;}

private:
	Rdb m_rdb;
};

extern class Doledb g_doledb;

void nukeDoledb ( collnum_t collnum ) ;

#endif // GB_DOLEDB_H
