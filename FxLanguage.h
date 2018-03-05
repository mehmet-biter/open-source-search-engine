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
#ifndef FX_FXLANGUAGE_H
#define FX_FXLANGUAGE_H

#include "Lang.h"

namespace FxLanguage {
	// CLD2/CLD3
	lang_t getLangIdCLD2(bool isPlainText, const char *content, int32_t contentLen,
	                     const char *contentLanguage, int32_t contentLanguageLen,
	                     const char *tld, int32_t tldLen, bool bestEffort = false);

	lang_t getLangIdCLD3(const char *content, int32_t contentLen);

	lang_t pickLanguage(lang_t contentLangIdCld2, lang_t contentLangIdCld3, lang_t summaryLangIdCld2,
	                    lang_t charsetLangId, lang_t langIdGB);
};


#endif //FX_FXLANGUAGE_H
