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
#include "FxTermCheckList.h"
#include "Conf.h"
#include "Log.h"
#include "termid_mask.h"
#include "Phrases.h"
#include "tokenizer.h"
#include <fstream>
#include <sys/stat.h>

TermCheckList::TermCheckList() : m_initialized(false) {}

TermCheckList::~TermCheckList(){}


bool TermCheckList::init(const char *fname1, const char *fname2) {
	//
	// Initialize single words
	//
	int32_t need4 = 10000 * 4 + 5000;
	if (!m_terms.set(sizeof(int64_t), 4, need4, NULL, 0, false, "termchecklist", false, 0)) {
		log(LOG_ERROR,"Could not initialize term hashtable");
		return false;
	}

	if( fname1 && !loadScoredTermList(&m_terms, fname1) ) {
		log(LOG_ERROR,"Could not load '%s'", fname1);
	}

	//
	// Initialize phrases (bigrams) - use same hash table as words
	//
	if( fname2 && !loadScoredTermList(&m_terms, fname2) ) {
		log(LOG_ERROR,"Could not load '%s'", fname1);
	}

	m_initialized = true;
	return m_initialized;
}


bool TermCheckList::loadScoredTermList(HashTableX *ht, const char *filename) {
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
			log(LOG_ERROR,"Invalid line read from %s: %.*s", filename, (int)line.length(), line.data());
			continue;
		}
		size_t secondColEnd = line.find_first_of("|", secondCol);
		size_t thirdCol = line.find_first_not_of("|", secondColEnd);
		if (thirdCol == std::string::npos) {
			// invalid format
			log(LOG_ERROR,"Invalid line read from %s: %.*s", filename, (int)line.length(), line.data());
			continue;
		}

		std::string lang = std::string(line, 0, firstColEnd);
		std::string col2(line, secondCol, secondColEnd - secondCol);
		std::string col3 = std::string(line, thirdCol);

		int32_t dwscore = atoi(col3.data());

		if( dwscore < 1 || col2.length() < 1 || col3.length() < 1 ) {
			log(LOG_ERROR,"Invalid line read from %s: %.*s", filename, (int)line.length(), line.data());
			continue;
		}

		//log(LOG_ERROR,"read: %s [%" PRId32 "] [%s]", col2.c_str(), dwscore, lang.c_str());

		int64_t dwid = hash64Lower_utf8_nospaces(col2.data(), col2.length());

		if( !ht->addKey(&dwid, &dwscore) ) {
			log(LOG_ERROR,"Could not add [%.*s] to word list", (int)col2.length(), col2.data());
			return false;
		}
	}

	log(LOG_INFO, "Loaded %s", filename);
	return true;
}



bool TermCheckList::getScore(const TokenizerResult &tr, Phrases *p, HashTableX *uniqueTermIds, int32_t *docScore, int32_t *numUniqueWords, int32_t *numUniquePhrases, char *debbuf, int32_t &debbuf_used, int32_t debbuf_size) {

	if( !uniqueTermIds || !docScore || !numUniqueWords || !numUniquePhrases ) {
		return false;
	}

	if( debbuf ) {
		debbuf[debbuf_used] = '\0';
	}

	int rc;

	for(size_t i=0; i < tr.size(); i++) {
		const auto &token = tr[i];
		if(!token.is_alfanum || token.token_hash==0) {
			continue;
		}

		const char *s = NULL;
		int32_t slen = 0;
		if( g_conf.m_logTraceTermCheckList || debbuf ) {
			s = token.token_start;
			slen = token.token_len;
		}

		int64_t termId = token.token_hash;

		// only process if we haven't seen it before
		if ( uniqueTermIds->getSlot( &termId ) >= 0 ) {
			//logTrace(g_conf.m_logTraceTermCheckList, "Already seen word %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK));
		}
		else {
			// add to hash table. return NULL and set g_errno on error
			if ( !uniqueTermIds->addKey(&termId)) {
				log(LOG_ERROR,"Could not add termId to uniqueTermIds hash table");
			}

			int32_t *sc = (int32_t*)m_terms.getValue64(termId);
			if( sc ) {
				logTrace(g_conf.m_logTraceTermCheckList, "Match word %d: %.*s -> %" PRIu64 " (%" PRId64 ") score %" PRId32 ". debbuf_used=%" PRId32 ", debbuf_size=%" PRId32 "", (int)i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK), *sc, debbuf_used, debbuf_size);
				(*docScore) += *sc;
				(*numUniqueWords)++;

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
				//logTrace(g_conf.m_logTraceTermCheckList, "Word %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, slen, s, (uint64_t)termId, (uint64_t)(termId & TERMID_MASK));
			}
		}


		if( !p->getPhraseId(i)) {
			// No phrases
			continue;
		}

		int32_t plen=0;
		char pbuf[256]={0};
		if( g_conf.m_logTraceTermCheckList || debbuf ) {
			p->getPhrase(i, pbuf, sizeof(pbuf)-1, &plen);
		}

		int64_t phraseId = p->getPhraseId(i);

		if ( uniqueTermIds->getSlot ( &phraseId ) >= 0 ) {
			//logTrace(g_conf.m_logTraceTermCheckList, "Already seen phrase %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK));
			continue;
		}

		// add to hash table. return NULL and set g_errno on error
		if ( !uniqueTermIds->addKey(&phraseId)) {
			log(LOG_ERROR,"Could not add phraseId to uniqueTermIds hash table");
		}

		int32_t *sc = (int32_t*)m_terms.getValue64(phraseId);
		if( sc ) {
			logTrace(g_conf.m_logTraceTermCheckList, "Match phrase %d: %.*s -> %" PRIu64 " (%" PRId64 ") score %" PRId32 ". debbuf_used=%" PRId32 ", debbuf_size=%" PRId32 "", (int)i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK), *sc, debbuf_used, debbuf_size);
			(*docScore) += *sc;
			(*numUniquePhrases)++;

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
			//logTrace(g_conf.m_logTraceTermCheckList, "Phrase %" PRId32 ": %.*s -> %" PRIu64 " (%" PRId64 ")", i, plen, pbuf, (uint64_t)phraseId, (uint64_t)(phraseId & TERMID_MASK));
		}
	}

	if( debbuf ) {
		debbuf[debbuf_used] = '\0';
	}

	return true;
}

