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
#ifndef FXCHECKADULT_H_
#define FXCHECKADULT_H_

#include <inttypes.h>
#include <stddef.h>
#include <string>
#include "FxTermCheckList.h"

class CheckAdult {
public:
	CheckAdult(XmlDoc *xd, bool debug=false);
	~CheckAdult();

	bool init();
	bool isDocAdult();
	int32_t getScore();
	int32_t getNumUniqueMatchedWords();
	int32_t getNumUniqueMatchedPhrases();
	int32_t getNumWordsChecked();
	bool hasEmptyDocumentBody();
	const char *getReason();
	const char *getDebugInfo();

private:
	bool hasAdultRatingTag();
	bool hasAdultAds();

	Url *m_url;
	Xml *m_xml;
	Words *m_words;
	Phrases *m_phrases;

	char *m_debbuf;
	int m_debbufUsed;
	int m_debbufSize;

	std::string m_reason;
	int32_t m_docMatchScore;
	int32_t m_numUniqueMatchedWords;
	int32_t m_numUniqueMatchedPhrases;
	int32_t m_numWordsChecked;
	bool m_emptyDocumentBody;
	bool m_resultValid;
	bool m_result;
};


bool isAdultTLD(const char *tld, size_t tld_len);

extern TermCheckList g_checkAdultList;


#endif
