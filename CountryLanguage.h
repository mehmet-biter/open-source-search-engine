//
// Copyright (C) 2018 Privacore ApS - https://www.privacore.com
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
#ifndef FX_COUNTRYLANGUAGE_H
#define FX_COUNTRYLANGUAGE_H

#include <cstdint>
#include <string>
#include "Lang.h"


namespace CountryLanguage {
	bool init();
	std::string getHttpAcceptLanguageStr(const char *host, int32_t hostLen);
};


#endif //FX_COUNTRYLANGUAGE_H
