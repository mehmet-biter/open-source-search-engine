#ifndef GB_SECTIONS_H
#define GB_SECTIONS_H

#include "HashTableX.h"
#include "Msg0.h"
#include "Bits.h"
#include "Words.h"
#include "Rdb.h"

// KEY:
// ssssssss ssssssss ssssssss ssssssss  s = 48 bit site hash
// ssssssss ssssssss hhhhhhhh hhhhhhhh  h = hash value (32 bits of the 64 bits!)
// hhhhhhhh hhhhhhhh tttttttt dddddddd  t = tag type
// dddddddd dddddddd dddddddd ddddddHD  d = docid

// DATA:
// SSSSSSSS SSSSSSSS SSSSSSSS SSSSSSSS  S = SectionVote::m_score
// NNNNNNNN NNNNNNNN NNNNNNNN NNNNNNNN  N = SectionVote::m_numSampled

// h: hash value. typically the lower 32 bits of the 
//    Section::m_contentHash64 vars. we
//    do not need the full 64 bits because we have the 48 bit site hash included
//    to reduce collisions substantially.

// 
// BEGIN SECTION BIT FLAGS (sec_t)
// values for Section::m_flags, of type sec_t
// 

// . these are descriptive flags, they are computed when Sections is set
// . SEC_NOTEXT sections do not vote, i.e. they are not stored in Sectiondb
#define SEC_NOTEXT       0x0001 // implies section has no alnum words
//#define SEC_UNUSED     0x0002
//#define SEC_UNUSED     0x0004
#define SEC_SCRIPT       0x0008
#define SEC_STYLE        0x0010
#define SEC_SELECT       0x0020
//#define SEC_UNUSED     0x0040
//#define SEC_UNUSED     0x0080
#define SEC_IN_TITLE     0x0100 // in title
#define SEC_IN_HEADER    0x0200 // in <hN> tags
//#define SEC_UNUSED     0x0400 
#define SEC_HIDDEN       0x0800 // <div style="display: none">
//#define SEC_UNUSED     0x1000
#define SEC_FAKE         0x2000 // <hr>/<br>/sentence based faux section
#define SEC_NOSCRIPT     0x4000
//#define SEC_UNUSED     0x8000

#define SEC_MENU               0x00010000
#define SEC_LINK_TEXT          0x00020000
#define SEC_MENU_HEADER        0x00040000
//#define SEC_UNUSED           0x00080000
//#define SEC_UNUSED           0x00100000
#define SEC_HEADING            0x00200000
//#define SEC_UNUSED           0x00400000
//#define SEC_UNUSED           0x00800000
#define SEC_SENTENCE           0x01000000 // made by a sentence?
#define SEC_PLAIN_TEXT         0x02000000
//#define SEC_UNUSED           0x04000000
//#define SEC_UNUSED                0x00008000000LL
//#define SEC_UNUSED                0x00010000000LL
//#define SEC_UNUSED                0x00020000000LL
//#define SEC_UNUSED                0x00040000000LL
//#define SEC_UNUSED                0x00080000000LL

//#define SEC_UNUSED                0x00100000000LL
#define SEC_MENU_SENTENCE           0x00200000000LL
//#define SEC_UNUSED                0x00400000000LL
//#define SEC_UNUSED                0x00800000000LL

// . some random-y numbers for Section::m_baseHash
// . used by splitSection() function
#define BH_BULLET  7845934
#define BH_SENTENCE 4590649
#define BH_IMPLIED  95468323

#define NOINDEXFLAGS (SEC_SCRIPT|SEC_STYLE|SEC_SELECT)

// the section type (bit flag vector for SEC_*) is currently 32 bits
typedef int64_t sec_t;

class Section {
public:

	// . the section immediately containing us
	// . used by Events.cpp to count # of timeofdays in section
	class Section *m_parent;

	// . we are in a linked list of sections
	// . this is how we keep order
	class Section *m_next;
	class Section *m_prev;

	// . if we are an element in a list, what is the list container section
	// . a containing section is a section containing MULTIPLE 
	//   smaller sections
	// . right now we limit such contained elements to text sections only
	// . used to set SEC_HAS_MENUBROTHER flag
	class Section *m_listContainer;

	// the sibling section before/after us. can be NULL.
	class Section *m_prevBrother;
	class Section *m_nextBrother;

	// if we are in a bold section in a sentence section then this
	// will point to the sentence section that contains us. if we already
	// are a sentence section then this points to itself.
	class Section *m_sentenceSection;

	// position of the first and last alnum word contained directly OR
	// indirectly in this section. use -1 if no text contained...
	int32_t m_firstWordPos;
	int32_t m_lastWordPos;

	// alnum positions for words contained directly OR indirectly as well
	int32_t m_alnumPosA;
	int32_t m_alnumPosB;

	// . for sentences that span multiple sections UNEVENLY
	// . see aliconference.com and abqtango.com for this crazy things
	// . for like 99% of all sections these guys equal m_firstWordPos and
	//   m_lastWordPos respectively
	int32_t m_senta;
	int32_t m_sentb;

	class Section *m_nextSent;

	// hash of this tag's baseHash and all its parents baseHashes combined
	uint32_t  m_tagHash;

	// for debug output display of color coded nested sections
	uint32_t m_colorHash;

	// tagid of this section, 0 means none (like sentence section, etc.)
	nodeid_t m_tagId;

	// usually just the m_tagId, but hashes in the class attributes of
	// div and span tags, etc. to make them unique
	uint32_t  m_baseHash;

	// kinda like m_baseHash but for xml tags and only hashes the
	// tag name and none of the fields
	uint32_t  m_xmlNameHash;

	// hash of all the alnum words DIRECTLY in this section
	uint64_t  m_contentHash64;

	// . range of words in Words class we encompass
	// . m_wordStart and m_wordEnd are the tag word #'s
	// . ACTUALLY it is a half-closed interval [a,b) like all else
	//   so m_b-1 is the word # of the ending tag, BUT split sections
	//   do not include ending tags!!! (i.e. <hr>, <br>, &bull, etc.)
	//   that were made with a call to splitSection()
	int32_t  m_a;//wordStart;
	int32_t  m_b;//wordEnd;

	// our depth. # of tags in the hash
	int32_t  m_depth;

	// container for the #define'd SEC_* values above
	sec_t m_flags;

	char m_used;

	int32_t m_gbFrameNum;

	// do we contain section "arg"?
	bool contains( class Section *arg ) {
		return ( m_a <= arg->m_a && m_b >= arg->m_b );
	}

	// do we contain section "arg"?
	bool strictlyContains ( class Section *arg ) {
		if ( m_a <  arg->m_a && m_b >= arg->m_b ) return true;
		if ( m_a <= arg->m_a && m_b >  arg->m_b ) return true;
		return false;
	}
};

#define SECTIONS_LOCALBUFSIZE 500

class Sections {
public:
	Sections();
	~Sections();

	void reset();

	// . returns false if blocked, true otherwise
	// . returns true and sets g_errno on error
	// . sets m_sections[] array, 1-1 with words array "w"
	bool set(class Words *w, class Bits *bits, class Url *url, char *coll, int32_t niceness, uint8_t contentType );

	bool verifySections ( ) ;

	bool growSections ( );

	void setNextBrotherPtrs ( bool setContainer ) ;

	// this is used by Events.cpp Section::m_nextSent
	void setNextSentPtrs();

	void printFlags ( class SafeBuf *sbuf , class Section *sn ) ;

	bool print(SafeBuf *sbuf, int32_t hiPos, int32_t *wposVec, char *densityVec,
			   char *wordSpamVec, char *fragVec );

	bool printSectionDiv( Section *sk );
	class SafeBuf *m_sbuf;

	bool isHardSection ( Section *sn );

	bool setMenus ( );

	void setHeader ( int32_t r , class Section *first , sec_t flag ) ;

	bool setHeadingBit ( ) ;

	void setTagHashes ( ) ;

	// save it
	class Words *m_words    ;
	class Bits  *m_bits     ;
	class Url   *m_url      ;
	char        *m_coll     ;
	int32_t         m_niceness ;
	uint8_t      m_contentType;

	int32_t *m_wposVec;
	char *m_densityVec;
	char *m_wordSpamVec;
	char *m_fragVec;
	
	// url ends in .rss or .xml ?
	bool  m_isRSSExt;

	// word #'s (-1 means invalid)
	int32_t m_titleStart;

	// these are 1-1 with the Words::m_words[] array
	class Section **m_sectionPtrs;

	// save this too
	int32_t m_nw ;

	// allocate m_sections[] buffer
	class Section  *m_sections;
	int32_t            m_numSections;
	int32_t            m_maxNumSections;

	// this holds the Sections instances in a growable array
	SafeBuf m_sectionBuf;

	// this holds ptrs to sections 1-1 with words array, so we can
	// see what section a word is in.
	SafeBuf m_sectionPtrBuf;

	bool m_isTestColl;

	// assume no malloc
	char  m_localBuf [ SECTIONS_LOCALBUFSIZE ];

	int64_t  *m_wids;
	int32_t       *m_wlens;
	char      **m_wptrs;
	nodeid_t   *m_tids;

	int32_t       m_hiPos;

	bool addSentenceSections ( ) ;

	class Section *insertSubSection ( int32_t a, int32_t b, int32_t newBaseHash ) ;

	class Section *m_rootSection; // the first section, aka m_firstSection
	class Section *m_lastSection;

	class Section *m_lastAdded;

	// kinda like m_rootSection, the first sentence section that occurs
	// in the document, is NULL iff no sentences in document
	class Section *m_firstSent;
};

// . the key in sectiondb is basically the Section::m_tagHash 
//   (with a docId) and the data portion of the Rdb record is this SectionVote
// . the Sections::m_nsvt and m_osvt hash tables contain SectionVotes
//   as their data value and use an tagHash key as well
class SectionVote {
public:
	// . seems like addVote*() always uses a score of 1.0
	// . seems to be a weight used when setting Section::m_votesFor[Not]Dup
	// . not sure if we really use this now
	float m_score;
	// . how many times does this tagHash occur in this doc?
	// . this eliminates the need for the SV_UNIQUE section type
	// . this is not used for tags of type contenthash or taghash
	// . seems like pastdate and futuredate and eurdatefmt 
	//   are the only vote types that actually really use this...
	float m_numSampled;
} __attribute__((packed, aligned(4)));

#endif // GB_SECTIONS_H
