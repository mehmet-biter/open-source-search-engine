#include "gb-include.h"

#include "Pops.h"
#include "Words.h"
#include "StopWords.h"
#include "Speller.h"

Pops::Pops () {
	m_pops = NULL;
}

Pops::~Pops() {
	if ( m_pops && m_pops != (int32_t *)m_localBuf ) {
		mfree ( m_pops , m_popsSize , "Pops" );
	}
}

bool Pops::set ( Words *words , int32_t a , int32_t b ) {
	int32_t nw = words->getNumWords();
	int64_t *wids = words->getWordIds();
	char **wp = words->m_words;
	int32_t *wlen = words->m_wordLens;

	int32_t need = nw * 4;
	if ( need > POPS_BUF_SIZE ) m_pops = (int32_t *)mmalloc(need,"Pops");
	else                        m_pops = (int32_t *)m_localBuf;
	if ( ! m_pops ) return false;
	m_popsSize = need;

	for ( int32_t i = a ; i < b && i < nw ; i++ ) {
		// skip if not indexable
		if ( !wids[i] ) {
			m_pops[i] = 0;
			continue;
		}

		// once again for the 50th time partap's utf16 crap gets in 
		// the way... we have to have all kinds of different hashing
		// methods because of it...
		uint64_t key;
		key = hash64d( wp[i], wlen[i] );
		m_pops[i] = g_speller.getPhrasePopularity( wp[i], key, 0 );

		// sanity check
		if ( m_pops[i] < 0 ) { char *xx=NULL;*xx=0; }

		if ( m_pops[i] == 0 ) {
			m_pops[i] = 1;
		}
	}

	return true;
}

