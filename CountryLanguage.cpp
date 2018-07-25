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
#include "CountryLanguage.h"
#include "GbUtil.h"
#include "CountryCode.h"
#include "Lang.h"
#include <fstream>
#include <sys/stat.h>
#include <sstream>
#include <map>

static std::map<uint8_t, std::string> s_country_accept_languages;

static bool initCountryAcceptLanguages() {
	static const char *s_filename = "countryacceptlanguages.txt";

	struct stat st;
	if (stat(s_filename, &st) != 0) {
		return false;
	}

	std::ifstream file(s_filename);
	std::string line;
	while (std::getline(file, line)) {
		// ignore comments & empty lines
		if (line.length() == 0 || line[0] == '#') {
			continue;
		}

		auto tokens = split(line, '|');
		if (tokens.size() != 2) {
			// invalid format
			continue;
		}

		uint8_t country_id = getCountryId(tokens[0].c_str());
		if (country_id == 0 && tokens[0].compare("zz") != 0) {
			continue;
		}

		s_country_accept_languages[country_id] = tokens[1];
	}

	return true;
}

std::string CountryLanguage::getHttpAcceptLanguageStr(const char *url) {
	uint8_t country_id = guessCountryTLD(url);

	auto it = s_country_accept_languages.find(country_id);
	if (it == s_country_accept_languages.end()) {
		// try to get default
		if (country_id != 0) {
			it = s_country_accept_languages.find(0);
		}

		if (it == s_country_accept_languages.end()) {
			// nothing is found (use en as default)
			return "en";
		}
	}

	return it->second;
}

bool CountryLanguage::init() {
	return initCountryAcceptLanguages();
}
