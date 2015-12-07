#include "gb-include.h"

#include "Pops.h"
#include "Words.h"
#include "StopWords.h"
#include "Speller.h"

Pops::Pops () {
	m_pops = NULL;
}

Pops::~Pops() {
	if ( m_pops && m_pops != (int32_t *)m_localBuf )
		mfree ( m_pops , m_popsSize , "Pops" );
}

bool Pops::set ( Words *words , int32_t a , int32_t b ) {
	int32_t        nw        = words->getNumWords();
	int64_t  *wids      = words->getWordIds ();
	char      **wp        = words->m_words;
	int32_t       *wlen      = words->m_wordLens;

	// point to scores
	//int32_t *ss = NULL;
	//if ( scores ) ss = scores->m_scores;

	int32_t need = nw * 4;
	if ( need > POPS_BUF_SIZE ) m_pops = (int32_t *)mmalloc(need,"Pops");
	else                        m_pops = (int32_t *)m_localBuf;
	if ( ! m_pops ) return false;
	m_popsSize = need;

	for ( int32_t i = a ; i < b && i < nw ; i++ ) {
		// skip if not indexable
		if ( ! wids[i] ) { m_pops[i] = 0; continue; }
		// or if score <= 0
		//if ( ss && ss[i] <= 0 ) { m_pops[i] = 0; continue; }
		// it it a common word? like "and" "the"... see StopWords.cpp
		/*
		if ( isCommonWord ( (int32_t)wids[i] ) ) {
		max:
			m_pops[i] = MAX_POP; 
			continue; 
		}

		else if ( wlen[i] <= 1 && is_lower(wp[i][0]) ) 
			goto max;
		*/
		// once again for the 50th time partap's utf16 crap gets in 
		// the way... we have to have all kinds of different hashing
		// methods because of it...
		uint64_t key ; // = wids[i];
		key = hash64d(wp[i],wlen[i]);
		m_pops[i] = g_speller.getPhrasePopularity(wp[i], key,true);
		// sanity check
		if ( m_pops[i] < 0 ) { char *xx=NULL;*xx=0; }
		if ( m_pops[i] == 0 ) m_pops[i] = 1;
	}
	return true;
}

