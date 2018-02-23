#ifndef GB_SECTIONS_H
#define GB_SECTIONS_H

#include "SafeBuf.h"
#include "XmlNode.h"

class Words;
class Bits;
class Url;


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
#define SEC_IN_HEAD      0x0080 // in <head>
#define SEC_IN_TITLE     0x0100 // in title
#define SEC_IN_HEADER    0x0200 // in <hN> tags
#define SEC_IN_IFRAME    0x0400
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

#define NOINDEXFLAGS (SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_IN_IFRAME)

typedef int64_t sec_t;

class Section {
public:

	// . the section immediately containing us
	// . used by Events.cpp to count # of timeofdays in section
	Section *m_parent;

	// . we are in a linked list of sections
	// . this is how we keep order
	Section *m_next;
	Section *m_prev;

	// . if we are an element in a list, what is the list container section
	// . a containing section is a section containing MULTIPLE 
	//   smaller sections
	// . right now we limit such contained elements to text sections only
	// . used to set SEC_HAS_MENUBROTHER flag
	Section *m_listContainer;

	// the sibling section before/after us. can be NULL.
	Section *m_prevBrother;
	Section *m_nextBrother;

	// if we are in a bold section in a sentence section then this
	// will point to the sentence section that contains us. if we already
	// are a sentence section then this points to itself.
	Section *m_sentenceSection;

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

	Section *m_nextSentence;

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
	bool contains(const Section *arg) const {
		return ( m_a <= arg->m_a && m_b >= arg->m_b );
	}

	// do we contain section "arg"?
	bool strictlyContains(const Section *arg) const {
		if ( m_a <  arg->m_a && m_b >= arg->m_b ) return true;
		if ( m_a <= arg->m_a && m_b >  arg->m_b ) return true;
		return false;
	}
};


class Sections {
public:
	Sections();
	~Sections();

	void reset();

	// . returns false if blocked, true otherwise
	// . returns true and sets g_errno on error
	// . sets m_sections[] array, 1-1 with words array "w"
	bool set(const Words *w, Bits *bits, const Url *url, uint8_t contentType);

private:
	bool verifySections ( ) ;

	void setNextBrotherPtrs ( bool setContainer ) ;

	void setNextSentPtrs();

	static void printFlags(SafeBuf *sbuf , const Section *sn );

public:
	bool print(SafeBuf *sbuf, int32_t hiPos, const int32_t *wposVec, const char *densityVec, const char *wordSpamVec, const char *fragVec) const;

private:
	struct PrintData {
		SafeBuf *sbuf;
		int32_t hiPos;
		const int32_t *wposVec;
		const char *densityVec;
		const char *wordSpamVec;
		const char *fragVec;
	};
	bool print(PrintData *pd) const;
	bool printSectionDiv(PrintData *pd, const Section *) const;

	bool isHardSection(const Section *sn) const;

	bool setMenus ( );

	void setHeader(int32_t r,  Section *first, sec_t flag);

	bool setHeadingBit ( ) ;

	void setTagHashes ( ) ;

	// save it
	const Words    *m_words;
	int32_t         m_nw;       //from m_word->getNumWords()
	Bits           *m_bits;
	uint8_t         m_contentType;

	// url ends in .rss or .xml ?
	bool  m_isRSSExt;

public:
	// these are 1-1 with the Words::m_words[] array
	Section **m_sectionPtrs;

	// allocate m_sections[] buffer
	Section        *m_sections;
	int32_t         m_numSections;
private:
	int32_t         m_maxNumSections;

	// this holds the Sections instances in a growable array
	SafeBuf m_sectionBuf;

	// this holds ptrs to sections 1-1 with words array, so we can
	// see what section a word is in.
	SafeBuf m_sectionPtrBuf;

	const int64_t  *m_wids;
	const int32_t      *m_wlens;
	const char * const *m_wptrs;
	const nodeid_t   *m_tids;

	bool addSentenceSections ( ) ;

	Section *insertSubSection ( int32_t a, int32_t b, int32_t newBaseHash ) ;

public:
	Section *m_rootSection; // the first section, aka m_firstSection
private:
	Section *m_lastSection;

	Section *m_lastAdded;

public:
	// kinda like m_rootSection, the first sentence section that occurs
	// in the document, is NULL iff no sentences in document
	Section *m_firstSentence;
};

#endif // GB_SECTIONS_H
