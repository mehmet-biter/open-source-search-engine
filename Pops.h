// Matt Wells, copyright Aug 2005

#ifndef GB_POPS_H
#define GB_POPS_H

#define POPS_BUF_SIZE (10*1024)

// the max popularity score a word can have
#define MAX_POP 0x7fff

class Words;

// the popularity vector for the Words class, 1-1 with those words
class Pops {
public:

	Pops();
	~Pops();

	// . set m_pops to the popularity of each word in "words"
	// . m_pops[] is 1-1 with the words in "words"
	// . must have computed the word ids (words->m_wordIds must be valid)
	bool set ( const Words *words, int32_t a, int32_t b );

	// from 0.0 to 1.0
	float getNormalizedPop( int32_t i ) const {
		return (float)m_pops[i] / (float)MAX_POP;
	}

private:
	int32_t *m_pops;
	int32_t  m_popsSize; // in bytes
	char  m_localBuf [ POPS_BUF_SIZE ];
};

#endif // GB_POPS_H
