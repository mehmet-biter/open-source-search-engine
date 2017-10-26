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
#include "FxAdultCheck.h"
#include "Log.h"
#include "Conf.h"
#include "Mem.h"
#include "termid_mask.h"
#include "Phrases.h"
#include "Words.h"
#include "XmlDoc.h"
#include <stddef.h>
#include <fstream>
#include <sys/stat.h>


AdultCheck::AdultCheck(XmlDoc *xd, bool debug) {
	m_docAdultScore = 0;
	m_numUniqueDirtyWords = 0;
	m_numUniqueDirtyPhrases = 0;
	m_numWordsChecked = 0;
	m_emptyDocumentBody = false;
	m_result = false;
	m_resultValid = false;

	if( xd ) {
		m_url = xd->getFirstUrl();
		if( m_url == (Url *)-1 ) {
			log(LOG_WARN, "XmlDoc::getFirstUrl() failed in AdultCheck::AdultCheck");
			m_url = NULL;
		}

		m_xml = xd->getXml();
		if( m_xml == (Xml *)-1 ) {
			log(LOG_WARN, "XmlDoc::getXml() failed in AdultCheck::AdultCheck");
			m_xml = NULL;
		}

		m_words = xd->getWords();
		if( m_words == (Words *)-1 ) {
			log(LOG_WARN, "XmlDoc::getWords() failed in AdultCheck::AdultCheck");
			m_words = NULL;
		}

		m_phrases = xd->getPhrases();
		if( m_phrases == (Phrases *)-1 ) {
			log(LOG_WARN, "XmlDoc::getPhrases() failed in AdultCheck::AdultCheck");
			m_phrases = NULL;
		}
	}
	else {
		m_url = NULL;
		m_xml = NULL;
		m_words = NULL;
		m_phrases = NULL;
	}

	m_debbufSize = 0;
	m_debbufUsed = 0;
	m_debbuf = NULL;
	
	if( debug ) {
		m_debbufSize = 2000;
		m_debbuf = (char *)mmalloc(m_debbufSize, "adultcheck");
		if( !m_debbuf ) {
			m_debbufSize = 0;
		}
	}
}


AdultCheck::~AdultCheck() {
	if( m_debbuf ) {
		mfree(m_debbuf, m_debbufSize, "adultcheck");
	}
}


int32_t AdultCheck::getScore() {
	if( m_resultValid ) {
		return m_docAdultScore;
	}
	return -1;
}

int32_t AdultCheck::getNumUniqueDirtyWords() {
	if( m_resultValid ) {
		return m_numUniqueDirtyWords;
	}
	return -1;
}

int32_t AdultCheck::getNumUniqueDirtyPhrases() {
	if( m_resultValid ) {
		return m_numUniqueDirtyPhrases;
	}
	return -1;
}

int32_t AdultCheck::getNumWordsChecked() {
	if( m_resultValid ) {
		return m_numWordsChecked;
	}
	return -1;
}

bool AdultCheck::hasEmptyDocumentBody() {
	if( m_resultValid ) {
		return m_emptyDocumentBody;
	}
	return false;
}

const char *AdultCheck::getReason() {
	return m_reason.c_str();
}

const char *AdultCheck::getDebugInfo() {
	if( m_debbuf ) {
		return m_debbuf;
	}
	return "";
}


bool AdultCheck::hasAdultRatingTag() {
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



bool AdultCheck::hasAdultAds() {
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



bool AdultCheck::isDocAdult() {
	// Hash table used to hold unique termIds to make sure we only count each unique word once
	HashTableX uniqueTermIds;

	if( m_resultValid ) {
		return m_result;
	}

	//
	// Check for adult TLDs
	//
	if( m_url && m_url->isAdult() ) {
		m_reason = "adultTLD";
		m_docAdultScore += 1000;
	}

	//
	// Check for adult content meta tags
	//
	if( !m_docAdultScore ) {
		if( hasAdultRatingTag() ) {
			m_reason = "adultRatingTag";
			m_docAdultScore += 1000;
		}

		if( !m_docAdultScore &&
			hasAdultAds() ) {
			m_reason = "adultAds";
			m_docAdultScore += 1000;
		}
	}	


	//
	// If not blocked by the cheaper checks, do the hard work and check document content
	//
	if( !m_docAdultScore ) {
		//
		// Score words and phrases from the document body text
		//

		if( m_words ) {
			if (!uniqueTermIds.set(sizeof(int64_t), 0, m_words->getNumWords()+5000, NULL, 0, false, "uniquetermids", false, 0)) {
				log(LOG_ERROR,"Could not initialize uniqueTermIds hash table");
			}

			if( !m_words->getNumWords() ) {
				// No words in document body
				m_emptyDocumentBody = true;
			}
			else {
				g_adultCheckList.getDirtyScore(m_words, m_phrases, &uniqueTermIds, &m_docAdultScore, &m_numUniqueDirtyWords, &m_numUniqueDirtyPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
				m_numWordsChecked += m_words->getNumWords();
			}
		}
		else {
			// No words in document body
			m_emptyDocumentBody = true;
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
				metaw.set(mtag, mtlen, true);
			}
			mtag = m_xml->getMetaContentPointer( "description", 11, "name", &mtlen );
			if( mtlen > 0 ) {
				//log(LOG_ERROR, "SETTING DESCRIPTION WORDS");
				metaw.addWords(mtag, mtlen, true);
			}
			if( metaw.getNumWords() ) {
				if( !metab.set(&metaw) ) {
					log(LOG_ERROR,"COULD NOT SET BITS FOR META WORDS");
				}
				if( !metap.set(&metaw, &metab) ) {
					log(LOG_ERROR,"COULD NOT SET PHRASES FOR META WORDS");
				}
				g_adultCheckList.getDirtyScore(&metaw, &metap, &uniqueTermIds, &m_docAdultScore, &m_numUniqueDirtyWords, &m_numUniqueDirtyPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
				m_numWordsChecked += metaw.getNumWords();
			}
		}

		//
		// Score words and phrases from URL
		//
		if( m_url ) {
			Words urlw;
			Bits urlb;
			Phrases urlp;

			urlw.set(m_url->getUrl(), m_url->getUrlLen(), true);
			if( !urlb.set(&urlw) ) {
				log(LOG_ERROR,"COULD NOT SET BITS FOR URL WORDS");
			}
			if( !urlp.set(&urlw, &urlb) ) {
				log(LOG_ERROR,"COULD NOT SET PHRASES FOR URL WORDS");
			}
			g_adultCheckList.getDirtyScore(&urlw, &urlp, &uniqueTermIds, &m_docAdultScore, &m_numUniqueDirtyWords, &m_numUniqueDirtyPhrases, m_debbuf, m_debbufUsed, m_debbufSize);
			m_numWordsChecked += urlw.getNumWords();
		}

		if( m_docAdultScore > 0 ) {
			m_reason = "adultTerms";
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
			m_reason = "USC2257Disclaimer";
			m_docAdultScore+=1000;
			//log(LOG_ERROR,"@@@ USC 2257 compliance statement FOUND in %s: score=%" PRId32 "", url->getUrl(), m_docAdultScore);
		}

        //TODO:
        //18 U.S.C. 2257
        //Title 18 U.S.C. 2257 Compliance Statement
        //Compliance with 18 U.S.C. &sect; 2257

        //<meta http-equiv="PICS-Label" content='(pics-1.1 "http://www.icra.org/ratingsv02.html" comment "ICRAonline EN v2.0" l gen true for "" r (nb 1 nc 1 nd 1 ne 1 nh 1 ni 1 vz 1 la 1 oz 1 cz 1) "http://www.rsac.org/ratingsv01.html" l gen true for "" r (n 3 s 3 v 0 l 4))' />

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
	}


	bool adult = false;
	if( ( m_docAdultScore >= 30 || m_numUniqueDirtyWords > 7) ||
		( m_docAdultScore >= 30 || m_numUniqueDirtyPhrases >= 3) ) {
		adult = true;
	}
	
	
	m_result = adult;
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


