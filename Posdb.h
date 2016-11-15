// Matt Wells, Copyright May 2012

// . format of an 18-byte posdb key
//   tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
//   tttttttt tttttttt dddddddd dddddddd  d = docId (38 bits)
//   dddddddd dddddddd dddddd0r rrrggggg  r = siterank, g = langid
//   wwwwwwww wwwwwwww wwGGGGss ssvvvvFF  w = word postion , s = wordspamrank
//   pppppb1N MMMMLZZD                    v = diversityrank, p = densityrank
//                                        M = multiplier, b = in outlink text
//                                        L = langIdShiftBit (upper bit)
//   G: 0 = body 
//      1 = intitletag 
//      2 = inheading 
//      3 = inlist 
//      4 = inmetatag
//      5 = inlinktext
//      6 = tag
//      7 = inneighborhood
//      8 = internalinlinktext
//      9 = inurl
//     10 = inmenu
//
//   F: 0 = original term
//      1 = conjugate/sing/plural
//      2 = synonym
//      3 = hyponym

//   NOTE: N bit is 1 if the shard of the record is determined by the
//   termid (t bits) and NOT the docid (d bits). N stands for "nosplit"
//   and you can find that logic in XmlDoc.cpp and Msg4.cpp. We store 
//   the hash of the content like this so we can see if it is a dup.

//   NOTE: M bits hold scaling factor (logarithmic) for link text voting
//   so we do not need to repeat the same link text over and over again.
//   Use M bits to hold # of inlinks the page has for other terms.

//   NOTE: for inlinktext terms the spam rank is the siterank of the
//   inlinker!

//   NOTE: densityrank for title is based on # of title words only. same goes
//   for incoming inlink text.

//   NOTE: now we can b-step into the termlist looking for a docid match
//   and not worry about misalignment from the double compression scheme
//   because if the 6th byte's low bit is clear that means its a docid
//   12-byte key, otherwise its the word position 6-byte key since the delbit
//   can't be clear for those!

//   THEN we can play with a tuner for how these various things affect
//   the search results ranking.


#ifndef GB_POSDB_H
#define GB_POSDB_H

#include "Rdb.h"
#include "Titledb.h" // DOCID_MASK/MAX_DOCID
#include "HashTableX.h"
#include "Sanity.h"
#include "termid_mask.h"


#define MAXSITERANK      0x0f // 4 bits
#define MAXLANGID        0x3f // 6 bits (5 bits go in 'g' the other in 'L')
#define MAXWORDPOS       0x0003ffff // 18 bits
#define MAXDENSITYRANK   0x1f // 5 bits
#define MAXWORDSPAMRANK  0x0f // 4 bits
#define MAXDIVERSITYRANK 0x0f // 4 bits
#define MAXHASHGROUP     0x0f // 4 bits
#define MAXMULTIPLIER    0x0f // 4 bits
#define MAXISSYNONYM     0x03 // 2 bits

// values for G bits in the posdb key
#define HASHGROUP_BODY                 0 // body implied
#define HASHGROUP_TITLE                1 
#define HASHGROUP_HEADING              2 // body implied
#define HASHGROUP_INLIST               3 // body implied
#define HASHGROUP_INMETATAG            4
#define HASHGROUP_INLINKTEXT           5
#define HASHGROUP_INTAG                6
#define HASHGROUP_NEIGHBORHOOD         7
#define HASHGROUP_INTERNALINLINKTEXT   8
#define HASHGROUP_INURL                9
#define HASHGROUP_INMENU               10 // body implied
#define HASHGROUP_END                  11

#define POSDB_DELETEDOC_TERMID    0

const char *getHashGroupString ( unsigned char hg );
float getTermFreqWeight  ( int64_t termFreq , int64_t numDocsInColl );

typedef key144_t posdbkey_t;


void printTermList ( int32_t i, const char *list, int32_t listSize ) ;


class Posdb {

 public:

	// resets rdb
	void reset();

	// sets up our m_rdb from g_conf (global conf class)
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( int32_t treeMem );

	bool verify ( const char *coll );

	bool addColl ( const char *coll, bool doVerify = true );

	// . make a 16-byte key from all these components
	// . since it is 16 bytes, the big bit will be set
	static void makeKey ( void              *kp             ,
		       int64_t          termId         ,
		       uint64_t docId          , 
		       int32_t               wordPos        ,
		       char               densityRank    ,
		       char               diversityRank  ,
		       char               wordSpamRank   ,
		       char               siteRank       ,
		       char               hashGroup      ,
		       char               langId         ,
		       // multiplier: we convert into 7 bits in this function
		       int32_t               multiplier     ,
		       bool               isSynonym      ,
		       bool               isDelKey       ,
		       bool               shardByTermId  );


	static void printKey(const char *key);
	static int printList ( RdbList &list ) ;

	// we map the 32bit score to like 7 bits here
	static void setMultiplierBits ( void *vkp , unsigned char mbits ) {
		key144_t *kp = (key144_t *)vkp;
		if ( mbits > MAXMULTIPLIER ) { gbshutdownAbort(true); }
		kp->n0 &= 0xfc0f;
		// map score to bits
		kp->n0 |= ((uint16_t)mbits) << 4;
	}
	
	static void setDocIdBits ( void *vkp , uint64_t docId ) {
		key144_t *kp = (key144_t *)vkp;
		kp->n1 &= 0x000003ffffffffffLL;
		kp->n1 |= (docId<<(32+10));
		kp->n2 &= 0xffffffffffff0000LL;
		kp->n2 |= docId>>22;
	}
	
	static void setSiteRankBits ( void *vkp , char siteRank ) {
		key144_t *kp = (key144_t *)vkp;
		if ( siteRank > MAXSITERANK ) { gbshutdownAbort(true); }
		kp->n1 &= 0xfffffe1fffffffffLL;
		kp->n1 |= ((uint64_t)siteRank)<<(32+5);
	}
	
	static void setLangIdBits ( void *vkp , char langId ) {
		key144_t *kp = (key144_t *)vkp;
		if ( langId > MAXLANGID ) { gbshutdownAbort(true); }
		kp->n1 &= 0xffffffe0ffffffffLL;
		// put the lower 5 bits here
		kp->n1 |= ((uint64_t)(langId&0x1f))<<(32);
		// and the upper 6th bit here. n0 is a int16_t.
		// 0011 1111
		if ( langId & 0x20 ) kp->n0 |= 0x08;
	}

	// set the word position bits et al to this float
	static void setFloat ( void *vkp , float f ) {
		*(float *)(((char *)vkp) + 2) = f; }

	static void setInt ( void *vkp , int32_t x ) {
		*(int32_t *)(((char *)vkp) + 2) = x; }

	// and read the float as well
	static float getFloat ( const void *vkp ) {
		return *(const float *)(((char *)vkp) + 2); }

	static int32_t getInt ( const void *vkp ) {
		return *(const int32_t *)(((char *)vkp) + 2); }

	static void setAlignmentBit ( void *vkp , char val ) {
		char *p = (char *)vkp;
		if ( val ) p[1] = p[1] | 0x02;
		else       p[1] = p[1] & 0xfd;
	}

	static bool isAlignmentBitClear ( const void *vkp ) {
		return ( ( ((const char *)vkp)[1] & 0x02 ) == 0x00 );
	}

	static void makeStartKey ( void *kp, int64_t termId , 
			    int64_t docId=0LL){
		return makeKey ( kp,
				 termId , 
				 docId,
				 0, // wordpos
				 0, // density
				 0, // diversity
				 0, // wordspam
				 0, // siterank
				 0, // hashgroup
				 0, // langid
				 0, // multiplier
				 0, // issynonym/etc.
				 true ,  // isdelkey
				 false ); // shardbytermid?
	}

	static void makeEndKey  ( void *kp,int64_t termId, 
			   int64_t docId = MAX_DOCID ) {
		return makeKey ( kp,
				 termId , 
				 docId,
				 MAXWORDPOS,
				 MAXDENSITYRANK,
				 MAXDIVERSITYRANK,
				 MAXWORDSPAMRANK,
				 MAXSITERANK,
				 MAXHASHGROUP,
				 MAXLANGID,
				 MAXMULTIPLIER,
				 true, // issynonym/etc.
				 false, // isdelkey
				 true);// shard by termid?
	}

	// we got two compression bits!
	static unsigned char getKeySize ( const void *key ) {
		if ( (((const char *)key)[0])&0x04 ) return 6;
		if ( (((const char *)key)[0])&0x02 ) return 12;
		return 18;
	}

	static int64_t getTermId ( const void *key ) {
		return ((const key144_t *)key)->n2 >> 16;
	}

	static int64_t getDocId ( const void *key ) {
		const char *k = (const char*)key;
		uint64_t d = *(const uint64_t*)(k+4);
		d >>= (64-38);
		return (int64_t)d;
	}

	static unsigned char getSiteRank ( const void *key ) {
		return (((const key144_t *)key)->n1 >> 37) & MAXSITERANK;
	}

	static unsigned char getLangId ( const void *key ) {
		if ( ((const char *)key)[0] & 0x08 )
			return ((((const key144_t *)key)->n1 >> 32) & 0x1f) | 0x20;
		else
			return ((((const key144_t *)key)->n1 >> 32) & 0x1f) ;
	}

	static unsigned char getHashGroup ( const void *key ) {
		//return (((key144_t *)key)->n1 >> 10) & MAXHASHGROUP;
		//return ((((const unsigned char *)key)[3]) >>2) & MAXHASHGROUP;
		//posdb sometimes have crap in it, so protect intersection from dealing with undefined hash groups
		unsigned char tmp = ((((const unsigned char *)key)[3]) >>2) & MAXHASHGROUP;
		return tmp<=10 ? tmp : 10;

	}

	static int32_t getWordPos ( const void *key ) {
		//return (((key144_t *)key)->n1 >> 14) & MAXWORDPOS;
		return (*((const uint32_t *)((unsigned char *)key+2))) >> (8+6);
	}

	static inline void setWordPos ( char *key , uint32_t wpos ) {
		// truncate
		wpos &= MAXWORDPOS;
		if ( wpos & 0x01 ) key[3] |= 0x40;
		else               key[3] &= ~((unsigned char)0x40);
		if ( wpos & 0x02 ) key[3] |= 0x80;
		else               key[3] &= ~((unsigned char)0x80);
		wpos >>= 2;
		key[4] = ((char *)&wpos)[0];
		key[5] = ((char *)&wpos)[1];
	}

	static unsigned char getWordSpamRank ( const void *key ) {
		//return (((const key144_t *)key)->n1 >> 6) & MAXWORDSPAMRANK;
		return ((((const uint16_t *)key)[1]) >>6) & MAXWORDSPAMRANK;
	}

	static unsigned char getDiversityRank ( const void *key ) {
		//return (((const key144_t *)key)->n1 >> 2) & MAXDIVERSITYRANK;
		return ((((const unsigned char *)key)[2]) >>2) & MAXDIVERSITYRANK;
	}

	static unsigned char getIsSynonym ( const void *key ) {
		return (((const key144_t *)key)->n1 ) & 0x03;
	}

	static unsigned char getIsHalfStopWikiBigram ( const void *key ) {
		return ((const char *)key)[2] & 0x01;
	}

	static unsigned char getDensityRank ( const void *key ) {
		return ((*(const uint16_t *)key) >> 11) & MAXDENSITYRANK;
	}

	static inline void setDensityRank ( char *key , unsigned char dr ) {
		// shift up
		dr <<= 3;
		// clear out
		key[1] &= 0x07;
		// or in
		key[1] |= dr;
	}

	static char isShardedByTermId ( const void *key ) {return ((const char *)key)[1] & 0x01; }

	static void setShardedByTermIdBit ( void *key ) { 
		char *k = (char *)key;
		k[1] |= 0x01;
	}

	static unsigned char getMultiplier ( const void *key ) {
		return ((*(const uint16_t *)key) >> 4) & MAXMULTIPLIER; }

	int64_t getTermFreq ( collnum_t collnum, int64_t termId ) ;

	Rdb      *getRdb   ( ) { return &m_rdb; }

	// Rdb init variables
	static inline int32_t getFixedDataSize() { return 0; }
	static inline bool getUseHalfKeys() { return true; }
	static inline char getKeySize() { return sizeof(posdbkey_t); }

private:
	Rdb m_rdb;
};


class RdbCache;

extern Posdb g_posdb;
extern Posdb g_posdb2;
extern RdbCache g_termFreqCache;

void reinitializeRankingSettings();

#endif // GB_POSDB_H
