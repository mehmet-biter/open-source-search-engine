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
#ifndef FXADULTCHECK_H_
#define FXADULTCHECK_H_

#include <inttypes.h>
#include <stddef.h>
#include <string>
#include "FxAdultCheckList.h"

class AdultCheck {
public:
	AdultCheck(XmlDoc *xd, bool debug=false);
	~AdultCheck();

	bool init();
	bool isDocAdult();
	int32_t getScore();
	int32_t getNumUniqueDirtyWords();
	int32_t getNumUniqueDirtyPhrases();
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
	int32_t m_docAdultScore;
	int32_t m_numUniqueDirtyWords;
	int32_t m_numUniqueDirtyPhrases;
	int32_t m_numWordsChecked;
	bool m_emptyDocumentBody;
	bool m_resultValid;
	bool m_result;
};


bool isAdultTLD(const char *tld, size_t tld_len);


#endif
