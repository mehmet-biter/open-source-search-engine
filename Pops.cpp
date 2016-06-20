#include "gb-include.h"

#include "Pops.h"
#include "Words.h"
#include "StopWords.h"
#include "Speller.h"
#include "Process.h"


Pops::Pops () {
	m_pops = NULL;
}

Pops::~Pops() {
	if ( m_pops && m_pops != (int32_t *)m_localBuf ) {
		mfree ( m_pops , m_popsSize , "Pops" );
	}
}

bool Pops::set ( const Words *words , int32_t a , int32_t b ) {
	int32_t nw = words->getNumWords();

	int32_t need = nw * 4;
	if ( need > POPS_BUF_SIZE ) m_pops = (int32_t *)mmalloc(need,"Pops");
	else                        m_pops = (int32_t *)m_localBuf;
	if ( ! m_pops ) return false;
	m_popsSize = need;

	for ( int32_t i = a ; i < b && i < nw ; i++ ) {
		// skip if not indexable
		int64_t wid = words->getWordId(i);
		if ( !wid ) {
			m_pops[i] = 0;
			continue;
		}

		// once again for the 50th time partap's utf16 crap gets in 
		// the way... we have to have all kinds of different hashing
		// methods because of it...
		uint64_t key;
		const char *wp = words->getWord(i);
		int32_t wlen = words->getWordLen(i);
		key = hash64d( wp, wlen );
		m_pops[i] = g_speller.getPhrasePopularity( wp, key, 0 );

		// sanity check
		if ( m_pops[i] < 0 ) { g_process.shutdownAbort(true); }

		if ( m_pops[i] == 0 ) {
			m_pops[i] = 1;
		}
	}

	return true;
}

