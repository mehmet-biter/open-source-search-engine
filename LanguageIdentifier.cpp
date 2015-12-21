#include "LanguageIdentifier.h"
#include "CountryCode.h"

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
