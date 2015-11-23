/// \file language.c \brief Language detection utility routines.
///
/// Contains the main utility function, guessLanguage(), and all
/// the support routines for detecting the language of a web page.
///

// using a different macro because there's already a Language.h
#ifndef LANGUAGEIDENTIFIER_H
#define LANGUAGEIDENTIFIER_H

#include "gb-include.h"

/// Contains methods of language identification by various means.
class LanguageIdentifier {
	public:
		/// Constructor, does very little.
		LanguageIdentifier();

		/// Destructor, does very little.
		~LanguageIdentifier() { return; }

		/// Find a language from DMOZ topic.
		///
		/// The function name is a bit misleading, we expect
		/// the country from the Top/World/X node.
		///
		/// @param topic the country name
		///
		/// @return the language, or langUnknown
		///
		uint8_t findLangFromDMOZTopic(char *topic);

	uint8_t guessCountryTLD(const char *url);
};

extern class LanguageIdentifier g_langId;
extern const uint8_t *langToTopic[];

#endif // LANGUAGEIDENTIFIER_H
