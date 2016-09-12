// Matt Wells, copyright Jun 2001

// . db of XmlDocs

#ifndef GB_TITLEDB_H
#define GB_TITLEDB_H

// how many bits is our docId? (4billion * 64 = 256 billion docs)
#define NUMDOCIDBITS 38
#define DOCID_MASK   (0x0000003fffffffffLL)
#define MAX_DOCID    DOCID_MASK

#include "TitleRecVersion.h"
#include "Rdb.h"
#include "Url.h"
#include "Conf.h"
#include "Xml.h"

// new key format:
// . <docId>     - 38 bits
// . <urlHash48> - 48 bits  (used when looking up by url and not docid)
// . <delBit>    -  1 bit

class Titledb {

 public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	//bool addColl ( char *coll, bool doVerify = true );

	// init m_rdb
	bool init ();

	// init secondary/rebuild titledb
	bool init2 ( int32_t treeMem ) ;

	// . get the probable docId from a url/coll
	// . it's "probable" because it may not be the actual docId because
	//   in the case of a collision we pick a nearby docId that is 
	//   different but guaranteed to be in the same group/cluster, so you 
	//   can be assured the top 32 bits of the docId will be unchanged
	static uint64_t getProbableDocId ( Url *url , bool mask = true ) {
		uint64_t probableDocId = hash64b(url->getUrl(),0);
		// Linkdb::getUrlHash() does not mask it
		if ( mask ) probableDocId = probableDocId & DOCID_MASK;
		// clear bits 6-13 because we want to put the domain hash there
		// dddddddd dddddddd ddhhhhhh hhdddddd
		probableDocId &= 0xffffffffffffc03fULL;
		uint32_t h = hash8(url->getDomain(), url->getDomainLen());
		//shift the hash by 6
		h <<= 6;
		// OR in the hash
		probableDocId |= h;
		return probableDocId;
	}

	// a different way to do it
	static uint64_t getProbableDocId ( const char *url  ) {
		Url u;
		u.set( url );
		return getProbableDocId ( &u ); 
	}

	// a different way to do it
	static uint64_t getProbableDocId(const char *url,const char *dom,int32_t domLen) {
		uint64_t probableDocId = hash64b(url,0) & 
			DOCID_MASK;
		// clear bits 6-13 because we want to put the domain hash there
		probableDocId &= 0xffffffffffffc03fULL;
		uint32_t h = hash8(dom,domLen);
		//shift the hash by 6
		h <<= 6;
		// OR in the hash
		probableDocId |= h;
		return probableDocId;
	}

	// turn off the last 6 bits
	static uint64_t getFirstProbableDocId ( int64_t d ) {
		return d & 0xffffffffffffffc0LL; }

	// turn on the last 6 bits for the end docId
	static uint64_t getLastProbableDocId  ( int64_t d ) {
		return d | 0x000000000000003fLL; }

	// . the top NUMDOCIDBITs of "key" are the docId
	// . we use the top X bits of the keys to partition the records
	// . using the top bits to partition allows us to keep keys that
	//   are near each other (euclidean metric) in the same partition
	static int64_t getDocIdFromKey ( key96_t *key ) {
		uint64_t docId;
		docId = ((uint64_t)key->n1)<<(NUMDOCIDBITS - 32);
		docId|=                      key->n0 >>(64-(NUMDOCIDBITS-32));
		return docId;
	}
	static int64_t getDocId ( key96_t *key ) { return getDocIdFromKey(key); }
	static int64_t getDocIdFromKey ( key96_t  key ) {
		return getDocIdFromKey(&key);}

	static uint8_t getDomHash8FromDocId (int64_t d) {
		return (d & ~0xffffffffffffc03fULL) >> 6; }

	static int64_t getUrlHash48 ( key96_t *k ) {
		return ((k->n0 >> 10) & 0x0000ffffffffffffLL); }

	// . dptr is a char ptr to the docid
	// . used by IndexTable2.cpp
	// . "dptr" is pointing into a 6-byte indexdb key
	// . see IndexTable2.cpp, grep for gbmemcpy() to see
	//   how the docid is parsed out of this key (or see
	//   Indexdb.h)
	// . return  ((*((uint16_t *)dptr)) >> 8) & 0xff; }
	static uint8_t getDomHash8 ( uint8_t *dptr ) { return dptr[1]; }

	// does this key/docId/url have it's titleRec stored locally?
	static bool isLocal ( int64_t docId );
	static bool isLocal ( Url *url ) {
		return isLocal ( getProbableDocId(url) ); }
	static bool isLocal ( key96_t key ) {
		return isLocal (getDocIdFromKey(&key));}


	Rdb *getRdb() { return &m_rdb; }

	// . make the key of a TitleRec from a docId
	// . remember to set the low bit so it's not a delete
	// . hi bits are set in the key
	static key96_t makeKey ( int64_t docId, int64_t uh48, bool isDel );

	static key96_t makeFirstKey ( int64_t docId ) {
		return makeKey ( docId , 0, true ); }

	static key96_t makeLastKey  ( int64_t docId ) {
		return makeKey ( docId , 0xffffffffffffLL, false ); }

	// . this is an estimate of the number of docs in the WHOLE db network
	// . we assume each group/cluster has about the same # of docs as us
	int64_t getGlobalNumDocs ( ) { 
		return m_rdb.getNumTotalRecs()*
			(int64_t)g_hostdb.m_numShards;}

	int32_t getLocalNumDocs () { return m_rdb.getNumTotalRecs(); }
	int32_t getNumDocsInMem () { return m_rdb.getNumUsedNodes(); }
	int32_t getMemUsed      () { return m_rdb.getTreeMemOccupied(); }

	// holds binary format title entries
	Rdb m_rdb;
};

extern class Titledb g_titledb;
extern class Titledb g_titledb2;

#endif // GB_TITLEDB_H
