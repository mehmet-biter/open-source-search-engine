#ifndef GB_GBLANGUAGE_H
#define GB_GBLANGUAGE_H

#include "Lang.h"

namespace GbLanguage {
	lang_t getLangIdCLD2(bool isPlainText, const char *content, int32_t contentLen,
	                     const char *contentLanguage, int32_t contentLanguageLen,
	                     const char *tld, int32_t tldLen);

	lang_t getLangIdCLD3(const char *content, int32_t contentLen);
};


#endif //GB_GBLANGUAGE_H
