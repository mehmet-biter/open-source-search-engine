
// See docs in language.h

#include "gb-include.h"
#include "LanguageIdentifier.h"
#include "Tagdb.h"
#include "Speller.h"
#include "CountryCode.h"
#include "Categories.h"
#include "Linkdb.h"

LanguageIdentifier g_langId;

const uint8_t *langToTopic[] = {
	(uint8_t*)"Unknown",
	(uint8_t*)"English",
	(uint8_t*)"Français",
	(uint8_t*)"Español",
	(uint8_t*)"Russian",
	(uint8_t*)"There Is No 5!",
	(uint8_t*)"Japanese",
	(uint8_t*)"Chinese_Traditional",
	(uint8_t*)"Chinese_Simplified",
	(uint8_t*)"Korean",
	(uint8_t*)"Deutsch", // 10
	(uint8_t*)"Nederlands",
	(uint8_t*)"Italiano",
	(uint8_t*)"Suomi",
	(uint8_t*)"Svenska",
	(uint8_t*)"Norsk",
	(uint8_t*)"PortuguÃªs",
	(uint8_t*)"Vietnamese",
	(uint8_t*)"Arabic",
	(uint8_t*)"Hebrew",
	(uint8_t*)"Bahasa_Indonesia", // 20
	(uint8_t*)"Greek",
	(uint8_t*)"Thai", // 22
	(uint8_t*)"Hindi",
	(uint8_t*)"Bangla",
	(uint8_t*)"Polska",
	(uint8_t*)"Tagalog"
};

LanguageIdentifier::LanguageIdentifier() {
	return;
}

uint8_t LanguageIdentifier::findLangFromDMOZTopic(char *topic) {
	int x;
	for(x = 0; x < (int)(sizeof(langToTopic)/sizeof(uint8_t *)); x++) {
		if ( ! langToTopic[x] ) continue;
		if(!strncasecmp((char*)langToTopic[x], topic,
				gbstrlen((char *)langToTopic[x])))
			return(x);
	}
	return(langUnknown);
}

uint8_t LanguageIdentifier::guessCountryTLD(const char *url) {
	uint8_t country = 0;
	char code[3];
	code[0] = code[1] = code [2] = 0;

	// check for prefix
	if(url[9] == '.') {
		code[0] = url[7];
		code[1] = url[8];
		code[2] = 0;
		country = g_countryCode.getIndexOfAbbr(code);
		if(country) return(country);
	}

	// Check for two letter TLD
	const char *cp = strchr(url+7, ':');
	if(!cp)
		cp = strchr(url+7, '/');
	if(cp && *(cp -3) == '.') {
		cp -= 2;
		code[0] = cp[0];
		code[1] = cp[1];
		code[2] = 0;
		country = g_countryCode.getIndexOfAbbr(code);
		if(country) return(country);
	}
	return(country);
}
