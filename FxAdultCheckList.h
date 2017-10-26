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
#ifndef FXADULTCHECKLIST_H_
#define FXADULTCHECKLIST_H_

#include <inttypes.h>
#include <stddef.h>
#include <string>
#include "HashTableX.h"

class Phrases;
class Words;
class Xml;
class XmlDoc;
class Url;

class AdultCheckList {
public:
	AdultCheckList();
	~AdultCheckList();
	bool init();

	bool getDirtyScore(Words *w, Phrases *p, HashTableX *uniqueTermIds, int32_t *docAdultScore, int32_t *numUniqueDirtyWords, int32_t *numUniqueDirtyPhrases, char *debbuf, int32_t &debbuf_used, int32_t debbuf_size);

protected:
	bool load();

private:
	bool loadScoredTermList(HashTableX *ht, const char *filename);

	HashTableX m_dirtyTerms;
	bool m_initialized;
};

extern AdultCheckList g_adultCheckList;

#endif
