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
#ifndef FX_CONTENTMATCHLIST_H
#define FX_CONTENTMATCHLIST_H

#include "MatchList.h"

class ContentMatchList : public MatchList<std::string> {
public:
	ContentMatchList();
	bool isContentMatched(const char *content, size_t contentLen);
};

extern ContentMatchList g_contentRetryProxyList;

#endif // FX_CONTENTMATCHLIST_H
