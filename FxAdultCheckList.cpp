//
// Copyright (C) 2017 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#include "FxAdultCheckList.h"
#include "Log.h"
#include "termid_mask.h"
#include "Phrases.h"
#include "Words.h"
#include <stddef.h>
#include <fstream>
#include <sys/stat.h>


AdultCheckList g_adultCheckList;

AdultCheckList::AdultCheckList() : m_initialized(false) {}

AdultCheckList::~AdultCheckList(){}


bool AdultCheckList::init() {
	if( m_initialized ) {
		return true;
	}

	log(LOG_INFO, "Initializing AdultCheckList");
	if( load() ) {
		m_initialized = true;
	}
	return m_initialized;
}


bool AdultCheckList::load() {

	//
	// Initialize dirty single words
	//
	int32_t need4 = 10000 * 4 + 5000;
	if (!m_dirtyTerms.set(sizeof(int64_t), 4, need4, NULL, 0, false, "dirty", false, 0)) {
		log(LOG_ERROR,"Could not initialize dirty word hashtable");
		return false;
	}

	if( !loadScoredTermList(&m_dirtyTerms, "adultwords.txt") ) {
		log(LOG_ERROR,"Could not load dirty word file");
	}

	//
	// Initialize dirty phrases (bigrams) - use same hash table as words
	//
	if( !loadScoredTermList(&m_dirtyTerms, "adultphrases.txt") ) {
		log(LOG_ERROR,"Could not load dirty phrase file");
	}

	return true;
}



bool AdultCheckList::loadScoredTermList(HashTableX *ht, const char *filename) {
	log(LOG_INFO, "Loading %s", filename);

	struct stat st;
	if (stat(filename, &st) != 0) {
		// probably not found
		log(LOG_INFO, "loadScoredTermlist: Unable to stat %s", filename);
		return false;
	}

	std::ifstream file(filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		auto firstColEnd = line.find_first_of("|");
		size_t secondCol = line.find_first_not_of("|", firstColEnd);
		if( firstColEnd == std::string::npos || secondCol == std::string::npos) {
			// invalid format
			log(LOG_ERROR,"Invalid line read: %.*s", (int)line.length(), line.data());
			continue;
		}
		size_t secondColEnd = line.find_first_of("|", secondCol);
		size_t thirdCol = line.find_first_not_of("|", secondColEnd);
		if (thirdCol == std::string::npos) {
			// invalid format
			log(LOG_ERROR,"Invalid line read: %.*s", (int)line.length(), line.data());
			continue;
		}

		std::string lang = std::string(line, 0, firstColEnd);
		std::string col2(line, secondCol, secondColEnd - secondCol);
		std::string col3 = std::string(line, thirdCol);

		int32_t dwscore = atoi(col3.data());

		if( dwscore < 1 || col2.length() < 1 || col3.length() < 1 ) {
			log(LOG_ERROR,"Invalid line read: %.*s", (int)line.length(), line.data());
			continue;
		}

		//log(LOG_ERROR,"read: %s [%" PRId32 "] [%s]", col2.c_str(), dwscore, lang.c_str());

		int64_t dwid = hash64Lower_utf8_nospaces(col2.data(), col2.length());

		if( !ht->addKey(&dwid, &dwscore) ) {
			log(LOG_ERROR,"COULD NOT ADD [%.*s] TO DIRTY WORD LIST", (int)col2.length(), col2.data());
			return false;
		}
	}

	log(LOG_INFO, "Loaded %s", filename);
	return true;
}




bool AdultCheckList::getDirtyScore(Words *w, Phrases *p, HashTableX *uniqueTermIds, int32_t *docAdultScore, int32_t *numUniqueDirtyWords, int32_t *numUniqueDirtyPhrases, char *debbuf, int32_t &debbuf_used, int32_t debbuf_size) {

	if( !w || !uniqueTermIds || !docAdultScore || !numUniqueDirtyWords || !numUniqueDirtyPhrases ) {
		return false;
	}

	if( debbuf ) {
		debbuf[debbuf_used] = '\0';
	}

	int rc;
	bool debug=false;

	const uint64_t *wids = reinterpret_cast<const uint64_t*>(w->getWordIds());
	int32_t nw = w->getNumWords();

	// log(LOG_ERROR,"=== numWords=%" PRId32 ", debbuf=%p, debbuf_size=%" PRId32 " ===", nw, debbuf, debbuf_size);
	for(int32_t i=0; i < nw; i++) {
		if( !wids[i] ) {
			continue;
		}

		char *s = w->getWord(i);
		if( !s ) {
			continue;
		}

		int32_t slen = w->getWordLen(i);
		int64_t termId = hash64Lower_utf8_nospaces( s , slen );

		// only process if we haven't seen it before
		if ( uniqueTermIds->getSlot( &termId ) >= 0 ) {
			if(debug) log(LOG_ERROR,"ALREADY SEEN WORD %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK));
		}
		else {
			// add to hash table. return NULL and set g_errno on error
			if ( !uniqueTermIds->addKey(&termId)) {
				log(LOG_ERROR,"Could not add termId to uniqueTermIds hash table");
			}

			int32_t *sc = (int32_t*)m_dirtyTerms.getValue64(termId);
			if( sc ) {
				if(debug) log(LOG_ERROR,"DIRTY WORD %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ") score %" PRId32 ". debbuf_used=%" PRId32 ", debbuf_size=%" PRId32 "", i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK), *sc, debbuf_used, debbuf_size);
				(*docAdultScore) += *sc;
				(*numUniqueDirtyWords)++;

				if( debbuf ) {
					// 2=", ", 2="w:"
					if( debbuf_used+slen+2+2+1 < debbuf_size ) {
						if(debbuf_used ) {
							rc = snprintf(&debbuf[debbuf_used], debbuf_size - debbuf_used, ", ");
							if( rc > 0 ) {
								debbuf_used += rc;
							}
						}

						rc = snprintf(&debbuf[debbuf_used], debbuf_size - debbuf_used, "w:%.*s", slen, s);
						if( rc > 0 ) {
							debbuf_used += rc;
						}
					}
				}
			}
			else {
				if(debug) log(LOG_ERROR,"WORD %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK));
			}
		}


		const uint64_t *pids = reinterpret_cast<const uint64_t*>(p->getPhraseIds2());

		if( !pids[i] ) {
			// No phrases
			continue;
		}

		int32_t plen;
		char pbuf[256];
		p->getPhrase(i, pbuf, sizeof(pbuf), &plen);

		if( plen ) {
			int64_t phraseId = hash64Lower_utf8_nospaces( pbuf , plen );

			if ( uniqueTermIds->getSlot ( &phraseId ) >= 0 ) {
				if(debug) log(LOG_ERROR,"ALREADY SEEN PHRASE %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK));
				continue;
			}

			// add to hash table. return NULL and set g_errno on error
			if ( !uniqueTermIds->addKey(&phraseId)) {
				log(LOG_ERROR,"Could not add phraseId to uniqueTermIds hash table");
			}

			int32_t *sc = (int32_t*)m_dirtyTerms.getValue64(phraseId);
			if( sc ) {
				if(debug) log(LOG_ERROR,"DIRTY PHRASE %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ") score %" PRId32 ". debbuf_used=%" PRId32 ", debbuf_size=%" PRId32 "", i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK), *sc, debbuf_used, debbuf_size);
				(*docAdultScore) += *sc;
				(*numUniqueDirtyPhrases)++;

				if( debbuf ) {
					// 2=", ", 2="p:"
					if( debbuf_used+plen+2+2+1 < debbuf_size ) {
						if(debbuf_used) {
							rc = snprintf(&debbuf[debbuf_used], debbuf_size-debbuf_used, ", ");
							if( rc > 0 ) {
								debbuf_used += rc;
							}
						}
						rc = snprintf(&debbuf[debbuf_used], debbuf_size-debbuf_used, "p:%.*s", plen, pbuf);
						if( rc > 0 ) {
							debbuf_used += rc;
						}
					}
				}
			}
			else {
				if(debug) log(LOG_ERROR,"PHRASE %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK));
			}
		}
	}

	if( debbuf ) {
		debbuf[debbuf_used] = '\0';
	}

	return true;
}

