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
#include "FxCheckAdult.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "termid_mask.h"
#include "Phrases.h"
#include "Words.h"
#include "XmlDoc.h"

TermCheckList g_checkAdultList;


CheckAdult::CheckAdult(XmlDoc *xd, bool debug) :
	m_debbuf(NULL), m_debbufUsed(0), m_debbufSize(0), m_docMatchScore(-1),
	m_numUniqueMatchedWords(0), m_numUniqueMatchedPhrases(0), m_numWordsChecked(0),
	m_emptyDocumentBody(false), m_resultValid(false), m_result(false) {

	if( !xd ) {
		log(LOG_ERROR, "CheckAdult::CheckAdult passed NULL-pointer");
		gbshutdownLogicError();
	}

	m_url = xd->getFirstUrl();
	if( m_url == (Url *)-1 ) {
		m_url = NULL;
	}

	m_xml = xd->getXml();
	if( m_xml == (Xml *)-1 ) {
		m_xml = NULL;
	}

	m_words = xd->getWords();
	if( m_words == (Words *)-1 ) {
		m_words = NULL;
	}

	m_phrases = xd->getPhrases();
	if( m_phrases == (Phrases *)-1 ) {
		m_phrases = NULL;
	}
	
	if( debug ) {
		m_debbufSize = 2000;
		m_debbuf = (char *)mmalloc(m_debbufSize, "CheckAdult");
		if( m_debbuf ) {
			// zero-terminate now as we may not need it, but may try logging it later
			m_debbuf[0] = '\0';
		}
		else {
			m_debbufSize = 0;
		}
	}
}


CheckAdult::~CheckAdult() {
	if( m_debbuf ) {
		mfree(m_debbuf, m_debbufSize, "CheckAdult");
	}
}


int32_t CheckAdult::getScore() {
	return m_docMatchScore;
}

int32_t CheckAdult::getNumUniqueMatchedWords() {
	return m_numUniqueMatchedWords;
}

int32_t CheckAdult::getNumUniqueMatchedPhrases() {
	return m_numUniqueMatchedPhrases;
}

int32_t CheckAdult::getNumWordsChecked() {
	return m_numWordsChecked;
}

bool CheckAdult::hasEmptyDocumentBody() {
	return m_emptyDocumentBody;
}

const char *CheckAdult::getReason() {
	return m_reason.c_str();
}

const char *CheckAdult::getDebugInfo() {
	if( m_debbuf ) {
		return m_debbuf;
	}
	return "";
}


bool CheckAdult::hasAdultRatingTag() {
	if( !m_xml ) {
		return false;
	}

	int32_t mtlen;

	// https://webmasters.googleblog.com/2012/04/1000-words-about-images.html
	// http://www.safelabeling.org/how.htm
	// http://www.rtalabel.org/index.php?content=howto
	char *mtag = m_xml->getMetaContentPointer( "rating", 6, "name", &mtlen );

	if( !mtag || mtlen <= 0 ) {
		// http://www.billdietrich.me/Computers.html#ContentRating
		// https://en.wikipedia.org/wiki/User:ArneBab/Voluntary_Content_Rating
		mtag = m_xml->getMetaContentPointer( "voluntary content rating", 24, "name", &mtlen );
	}

	switch( mtlen ) {
		case 5:
			if( strncasecmp(mtag, "adult", mtlen) == 0 ) {
				return true;
			}
			break;
		case 6:
			if( strncasecmp(mtag, "mature", mtlen) == 0 ) {
				return true;
			}
			break;
		case 7:
			// non-standard, seen in the wild
			if( strncasecmp(mtag, "adulto", mtlen) == 0 ) {
				return true;
			}
			break;
		case 10:
			// non-standard, seen in the wild
			if( strncasecmp(mtag, "restricted", mtlen) == 0 ) {
				return true;
			}
			break;
		case 27:
			if( strncasecmp(mtag, "RTA-5042-1996-1400-1577-RTA", mtlen) == 0 ) {
				return true;
			}
			break;
	}

	if( mtlen > 0 ) {
		// non-standard, seen in the wild
		if( strncasestr(mtag, "porn", mtlen) ||
			strncasestr(mtag, "porno", mtlen) ||
			strncasestr(mtag, "adult", mtlen) ||
			strncasestr(mtag, "fuck", mtlen) ||
			strncasestr(mtag, "sex", mtlen) ||
			strncasestr(mtag, "xxx", mtlen) ) {
				return true;
			}
	}

	// YouTube
	mtag = m_xml->getMetaContentPointer( "isFamilyFriendly", 16, "itemprop", &mtlen );
	switch( mtlen ) {
		case 5:
			if( strncasecmp(mtag, "false", mtlen) == 0 ) {
				return true;
			}
			break;
	}

	return false;
}



bool CheckAdult::hasAdultAds() {
	if( !m_xml ) {
		return false;
	}

	int32_t mtlen;

	//
	// Adult ad networks verification tags
	//
	char *mtag = m_xml->getMetaContentPointer( "ero_verify", 10, "name", &mtlen );
	if( mtag && mtlen > 0 ) {
		return true;
	}

	mtag = m_xml->getMetaContentPointer( "juicyads-site-verification", 26, "name", &mtlen );
	if( mtag && mtlen > 0 ) {
		return true;
	}

	mtag = m_xml->getMetaContentPointer( "trafficjunky-site-verification", 30, "name", &mtlen );
	if( mtag && mtlen > 0 ) {
		return true;
	}

	mtag = m_xml->getMetaContentPointer( "adamo-site-verification", 23, "name", &mtlen );
	if( mtag && mtlen > 0 ) {
		return true;
	}
	return false;
}



bool CheckAdult::isDocAdult() {
	// Hash table used to hold unique termIds to make sure we only count each unique word once
	HashTableX uniqueTermIds;

	if( m_resultValid ) {
		return m_result;
	}

	m_docMatchScore = 0;
	//
	// Check for adult TLDs
	//
	if( m_url && m_url->isAdult() ) {
		m_reason = "adultTLD";
		m_docMatchScore += 1000;
		logTrace(g_conf.m_logTraceTermCheckList, "Adult TLD found in %s", m_url->getUrl());
	}

	//
	// Check for adult content meta tags
	//
	if( !m_docMatchScore ) {
		if( hasAdultRatingTag() ) {
			m_reason = "adultRatingTag";
			m_docMatchScore += 1000;
			logTrace(g_conf.m_logTraceTermCheckList, "Rating tag found in %s", m_url->getUrl());
		}

		if( !m_docMatchScore &&
			hasAdultAds() ) {
			m_reason = "adultAds";
			m_docMatchScore += 1000;
			logTrace(g_conf.m_logTraceTermCheckList, "Adult ads found in %s", m_url->getUrl());
		}
	}	


	//
	// If not blocked by the cheaper checks, do the hard work and check document content
	//
	if( !m_docMatchScore ) {
		//
		// Score words and phrases from the document body text
		//

		if( m_words ) {
			if (!uniqueTermIds.set(sizeof(int64_t), 0, m_words->getNumWords()+5000, NULL, 0, false, "uniquetermids", false, 0)) {
				log(LOG_ERROR,"isDocAdult: Could not initialize uniqueTermIds hash table");
			}

			if( !m_words->getNumWords() ) {
				// No words in document body
				m_emptyDocumentBody = true;
			}
			else {
				g_checkAdultList.getScore(m_words, m_phrases, &uniqueTermIds, &m_docMatchScore, &m_numUniqueMatchedWords, &m_numUniqueMatchedPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
				m_numWordsChecked += m_words->getNumWords();
			}
			logTrace(g_conf.m_logTraceTermCheckList, "%" PRId32 " words checked (%" PRId32 " unique) in body: %s. %" PRId32 " unique matched words, %" PRId32 " unique matched phrases. Score: %" PRId32 "",
				m_words->getNumWords(), uniqueTermIds.getNumUsedSlots(), m_url->getUrl(), m_numUniqueMatchedWords, m_numUniqueMatchedPhrases, m_docMatchScore);
		}
		else {
			// No words in document body
			m_emptyDocumentBody = true;
			logTrace(g_conf.m_logTraceTermCheckList, "Document body is empty in %s", m_url->getUrl());
		}

		//
		// Score words and phrases from the document meta tags
		//
		if( m_xml ) {
			Words metaw;
			Bits metab;
			Phrases metap;
			int32_t mtlen;

			char *mtag = m_xml->getMetaContentPointer( "keywords", 8, "name", &mtlen );
			if( mtlen > 0 ) {
				metaw.set(mtag, mtlen);
			}
			mtag = m_xml->getMetaContentPointer( "description", 11, "name", &mtlen );
			if( mtlen > 0 ) {
				metaw.addWords(mtag, mtlen);
			}
			if( metaw.getNumWords() ) {
				if( !metab.set(&metaw) ) {
					log(LOG_ERROR,"isDocAdult: Could not set bits for meta words");
				}
				if( !metap.set(&metaw, &metab) ) {
					log(LOG_ERROR,"isDocAdult: Could not set phrases for meta words");
				}
				g_checkAdultList.getScore(&metaw, &metap, &uniqueTermIds, &m_docMatchScore, &m_numUniqueMatchedWords, &m_numUniqueMatchedPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
				m_numWordsChecked += metaw.getNumWords();

				logTrace(g_conf.m_logTraceTermCheckList, "%" PRId32 " words checked (%" PRId32 " unique) in meta tags: %s. %" PRId32 " unique matched words, %" PRId32 " unique matched phrases. Score: %" PRId32 "",
					metaw.getNumWords(), uniqueTermIds.getNumUsedSlots(), m_url->getUrl(), m_numUniqueMatchedWords, m_numUniqueMatchedPhrases, m_docMatchScore);
			}
		}

		//
		// Score words and phrases from URL
		//
		if( m_url ) {
			Words urlw;
			Bits urlb;
			Phrases urlp;

			urlw.set(m_url->getUrl(), m_url->getUrlLen());
			if( !urlb.set(&urlw) ) {
				log(LOG_ERROR,"isDocAdult: Could not set bits for URL words");
			}
			if( !urlp.set(&urlw, &urlb) ) {
				log(LOG_ERROR,"isDocAdult: Could not set phrases for URL words");
			}
			g_checkAdultList.getScore(&urlw, &urlp, &uniqueTermIds, &m_docMatchScore, &m_numUniqueMatchedWords, &m_numUniqueMatchedPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
			m_numWordsChecked += urlw.getNumWords();

			logTrace(g_conf.m_logTraceTermCheckList, "%" PRId32 " words checked (%" PRId32 " unique) in URL: %s. %" PRId32 " unique matched words, %" PRId32 " unique matched phrases. Score: %" PRId32 "", 
				urlw.getNumWords(), uniqueTermIds.getNumUsedSlots(), m_url->getUrl(), m_numUniqueMatchedWords, m_numUniqueMatchedPhrases, m_docMatchScore);
		}

		//
		// Additional check for adult content compliance statement
		//
		// "18 U.S.C. 2257 Record-Keeping Requirements Compliance Statement"
		// "18 USC. 2257 Record-Keeping Requirements Compliance Statement"
		int64_t hs18 = hash64Lower_utf8_nospaces("18", 2);
		int64_t hsu = hash64Lower_utf8_nospaces("u", 1);
		int64_t hss = hash64Lower_utf8_nospaces("s", 1);
		int64_t hsc = hash64Lower_utf8_nospaces("c", 1);
		int64_t hsusc = hash64Lower_utf8_nospaces("usc", 3);
		int64_t hs2257 = hash64Lower_utf8_nospaces("2257", 4);
		int64_t hsrecord = hash64Lower_utf8_nospaces("record", 6);
		int64_t hskeeping = hash64Lower_utf8_nospaces("keeping", 7);
		int64_t hsrequirements = hash64Lower_utf8_nospaces("requirements", 12);
		int64_t hscompliance = hash64Lower_utf8_nospaces("compliance", 10);

		if( uniqueTermIds.getSlot(&hs18) >= 0 &&
			uniqueTermIds.getSlot(&hs2257) >= 0 &&
			uniqueTermIds.getSlot(&hsrecord) >= 0 &&
			uniqueTermIds.getSlot(&hskeeping) >= 0 &&
			uniqueTermIds.getSlot(&hsrequirements) >= 0 &&
			uniqueTermIds.getSlot(&hscompliance) >= 0 &&
			(	(uniqueTermIds.getSlot(&hsu) >= 0 &&
				uniqueTermIds.getSlot(&hss) >= 0 &&
				uniqueTermIds.getSlot(&hsc) >= 0) ||
				uniqueTermIds.getSlot(&hsusc) >= 0
			)) {
			//m_reason = "USC2257Disclaimer";

			// Give it a score of 10 and count it as a phrase
			m_docMatchScore += 10;
			m_numUniqueMatchedPhrases++;
			logTrace(g_conf.m_logTraceTermCheckList, "USC 2257 compliance statement found in %s: score=%" PRId32 "", m_url->getUrl(), m_docMatchScore);
		}

        //TODO:
        //18 U.S.C. 2257
        //Title 18 U.S.C. 2257 Compliance Statement
        //Compliance with 18 U.S.C. &sect; 2257

        // <meta http-equiv="PICS-Label" content='(pics-1.1 "http://www.icra.org/ratingsv02.html" comment "ICRAonline EN v2.0" l gen true for "" r (nb 1 nc 1 nd 1 ne 1 nh 1 ni 1 vz 1 la 1 oz 1 cz 1) "http://www.rsac.org/ratingsv01.html" l gen true for "" r (n 3 s 3 v 0 l 4))' />

        //Beskyt dine børn mod erotiske sites med
        //Protect your children against Adult Content with
        //Eltern können ihre Kinder vor ungeeigneten Inhalten schützen mit
        //Protégez vos enfants contre le Contenu pour adultes au moyen de
        //Skydda dina barn mot innehåll som endast är avsett för vuxna med hjälp av
        //Beskytt barna dine mot voksent innhold med
        //Proteggete i vostri figli dal contenuto erotico di questo sito con
        //Los padres, protegen a sus menores del Contenido Adulto con
        //Os Pais devem usar um dos seguintes programas para salvaguardar os filhos do conteúdo erótico
        //Bescherm minderjarigen tegen expliciete beelden op internet met software als Netnanny, Cyberpatrol of Cybersitter.

		if( m_docMatchScore > 0 ) {
			m_reason = "adultTerms";
		}
	}

	logTrace(g_conf.m_logTraceTermCheckList, "Final score %" PRId32 " for: %s. %" PRId32 " unique matched words, %" PRId32 " unique matched phrases", 
		m_docMatchScore, m_url->getUrl(), m_numUniqueMatchedWords, m_numUniqueMatchedPhrases);

	m_result = false;
	if( ( m_docMatchScore >= 30 || m_numUniqueMatchedWords > 7) ||
		( m_docMatchScore >= 30 || m_numUniqueMatchedPhrases >= 3) ) {
		m_result = true;
	}
	m_resultValid = true;
	
	return m_result;
}



// Check for adult TLDs
// https://tld-list.com/tld-categories/adult
bool isAdultTLD(const char *tld, size_t tld_len) {
	switch(tld_len) {
		case 3:
			if( strncasecmp(tld, "cam", tld_len) == 0 ||
				strncasecmp(tld, "sex", tld_len) == 0 ||
				strncasecmp(tld, "xxx", tld_len) == 0 ) {
				return true;
			}
			break;
		case 4:
			if( strncasecmp(tld, "porn", tld_len) == 0 ||
				strncasecmp(tld, "sexy", tld_len) == 0 ||
				strncasecmp(tld, "tube", tld_len) == 0 ) {
				return true;
			}
			break;
		case 5:
			if( strncasecmp(tld, "adult", tld_len) == 0 ) {
				return true;
			}
			break;
		case 6:
			if( strncasecmp(tld, "webcam", tld_len) == 0 ) {
				return true;
			}
			break;
		default:
			break;
	}

	return false;
}


