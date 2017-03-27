// Matt Wells, copyright Jan 2007

#ifndef GB_MATCHES2_H
#define GB_MATCHES2_H

#include <inttypes.h>
#include <cstring>

// use these routines for matching any of a list of substrings in the haystack.
// the Matches array is the list of substrings to match in the "haystack". this
// should be *very* fast.
class Needle {
public:
	Needle(const char *string, char id, char isSection)
		: m_string(string)
		, m_stringSize(strlen(string))
		, m_id(id)
		, m_isSection(isSection) {
	}

	const char *m_string;
	size_t  m_stringSize;
	char  m_id;
	// if m_isSection is true, getMatch() only matches if haystack 
	// ptr is < linkPos
	char  m_isSection; 
};

class NeedleMatch {
public:
	NeedleMatch()
		: m_count(0)
		, m_firstMatch(nullptr) {
	}

	int32_t m_count;
	char *m_firstMatch;
};

char *getMatches2(const Needle *needles, NeedleMatch *needlesMatch, int32_t numNeedles,
                  char *haystack, int32_t haystackSize, char *linkPos, bool *hadPreMatch);



#endif // GB_MATCHES2_H
