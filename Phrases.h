// Matt Wells, copyright Jul 2001

// . generate phrases and store their hashes into m_phraseIds[] array
// . hash() will then hash the phraseIds into the TermTable (hashtable)
// . will it hash a word as a phrase if it's the only word? No, it will not.
//   it only hashes 2+ word phrases

#ifndef GB_PHRASES_H
#define GB_PHRASES_H

#include <inttypes.h>
#include <stddef.h>
#include "max_words.h"

class Words;
class Bits;


class Phrases {
public:

	Phrases();
	~Phrases();
	void reset() ;

	// . set the hashes (m_phraseIds) of the phrases for these words
	// . "bits" describes the words in a phrasing context
	bool set(const Words *words, const Bits *bits );

	int64_t getPhraseId(int i) const {
		return m_phraseIds2[i];
	}

	// . store phrase that starts with word #i into "buf"
	// . we also NULL terminated it in "buf"
	void getPhrase(int32_t i, char *buf, size_t bufsize, int32_t *phrLen) const;

	int32_t getNumWordsInPhrase2( int32_t i ) const {
		return m_numWordsTotal2[i];
	}

	int32_t getMinWordsInPhrase( int32_t i , int64_t *pid ) const;

private:
	void setPhrase(int32_t i);

	char  m_localBuf [ MAX_WORDS * 14 ];

	char *m_buf;
	int32_t  m_bufSize;

	// the two word hash
	int64_t *m_phraseIds2;

	// for the two word phrases:
	unsigned char *m_numWordsTotal2;
	int32_t m_numPhrases; // should equal the # of words

	// placeholders to avoid passing to subroutine
	const Words *m_words;
	const int64_t *m_wids;
	const char * const *m_wptrs;
	const int32_t *m_wlens;

	const Bits *m_bits;
};

#endif // GB_PHRASES_H
