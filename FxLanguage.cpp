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
#include "FxLanguage.h"

#include <cstdio>
#include "third-party/cld2/public/compact_lang_det.h"
#include "third-party/cld2/public/encodings.h"

#include "third-party/cld3/src/nnet_language_identifier.h"

#include "Log.h"
#include "Conf.h"

static lang_t convertLangCLD2(CLD2::Language language) {
	switch (language) {
		// writing system: afro-asiatic -> berber
		case CLD2::X_Tifinagh:
			return langUnwanted;

		// afro-asiatic -> chadic -> west chadic
		case CLD2::HAUSA:
			return langUnwanted;

		// afro-asiatic -> cushitic
		case CLD2::SOMALI:
		case CLD2::AFAR:
		case CLD2::OROMO:
			return langUnwanted;

		// writing system: afro-asiatic -> egyptian
		case CLD2::X_Coptic:
		case CLD2::X_Egyptian_Hieroglyphs:
			return langUnwanted;

		// writing system: somali
		case CLD2::X_Osmanya:
			return langUnwanted;

		// afro-asiatic -> khasian
		case CLD2::KHASI:
			return langUnwanted;

		// afro-asiatic -> semitic -> central semitic
		case CLD2::HEBREW:
		case CLD2::X_Hebrew:
			return langHebrew;
		case CLD2::ARABIC:
		case CLD2::X_Arabic:
			return langArabic;
		case CLD2::MALTESE:
			return langMaltese;
		case CLD2::SYRIAC:
		case CLD2::X_Syriac:
		case CLD2::X_Sundanese:
			return langUnwanted;

		// afro-asiatic -> semitic -> south semitic
		case CLD2::AMHARIC:
		case CLD2::TIGRINYA:
			return langUnwanted;

		// writing system: afro-asiatic -> semitic
		case CLD2::X_Ugaritic:
		case CLD2::X_Mandaic:
		case CLD2::X_Samaritan:
		case CLD2::X_Imperial_Aramaic:
		case CLD2::X_Old_South_Arabian:
		case CLD2::X_Ethiopic:
		case CLD2::X_Cuneiform:
		case CLD2::X_Phoenician:
			return langUnwanted;

		// writing system: algic -> algonquian
		case CLD2::X_Canadian_Aboriginal:
			return langUnwanted;

		// austroasiatic -> khmer
		case CLD2::KHMER:
		case CLD2::X_Khmer:
			return langUnwanted;

		// writing system: austroasiatic -> munda
		case CLD2::X_Ol_Chiki:
		case CLD2::X_Sora_Sompeng:
			return langUnwanted;

		// austroasiatic -> vietic
		case CLD2::VIETNAMESE:
			return langVietnamese;

		// austronesian -> malayo-polynesian -> east barito
		case CLD2::MALAGASY:
			return langMalgasy;

		// austronesian -> malayo-polynesian -> nuclear malayo-polynesian
		case CLD2::INDONESIAN:
			return langIndonesian;
		case CLD2::MALAY:
		case CLD2::JAVANESE:
		case CLD2::X_Javanese:
		case CLD2::SUNDANESE:
			return langUnwanted;

		// writing system: austronesian -> malayo-polynesian -> nuclear malayo-polynesian
		case CLD2::X_Buginese:
		case CLD2::X_Balinese:
		case CLD2::X_Batak:
		case CLD2::X_Rejang:
		case CLD2::X_Cham:
			return langUnwanted;

		// austronesian -> malayo-polynesian -> oceanic
		case CLD2::TONGA:
		case CLD2::MAORI:
		case CLD2::FIJIAN:
		case CLD2::NAURU:
		case CLD2::SAMOAN:
		case CLD2::HAWAIIAN:
			return langUnwanted;

		// austronesian -> malayo-polynesian -> philippine
		case CLD2::TAGALOG:
		case CLD2::X_Tagalog:
			return langTagalog;
		case CLD2::CEBUANO:
		case CLD2::PAMPANGA:
		case CLD2::WARAY_PHILIPPINES:
		case CLD2::X_Hanunoo:
			return langUnwanted;

		// writing system: philippine
		case CLD2::X_Buhid:
		case CLD2::X_Tagbanwa:
			return langUnwanted;

		// dravidian -> southern
		case CLD2::TELUGU:
		case CLD2::X_Telugu:
			return langTelugu;
		case CLD2::MALAYALAM:
		case CLD2::X_Malayalam:
		case CLD2::TAMIL:
		case CLD2::X_Tamil:
		case CLD2::KANNADA:
		case CLD2::X_Kannada:
			return langUnwanted;

		// english creole -> atlantic
		case CLD2::KRIO:
			return langUnwanted;

		// english creole -> pacific
		case CLD2::BISLAMA:
			return langUnwanted;

		// eskimo-aleut -> eskimo -> inuit
		case CLD2::GREENLANDIC:
			return langGreenlandic;
		case CLD2::INUPIAK:
		case CLD2::INUKTITUT:
			return langUnwanted;

		// french creole
		case CLD2::SESELWA:
			return langUnwanted;

		// hmong-mien -> hmongic
		case CLD2::HMONG:
			return langUnwanted;

		// writing system: hmong-mien -> hmongic
		case CLD2::X_Miao:
			return langUnwanted;

		// indo-european -> albanian
		case CLD2::ALBANIAN:
			return langUnwanted;

		// writing system: indo-european -> anatolian
		case CLD2::X_Lydian:
		case CLD2::X_Lycian:
		case CLD2::X_Carian:
			return langUnwanted;

		// indo-european -> armenian
		case CLD2::ARMENIAN:
		case CLD2::X_Armenian:
			return langArmenian;

		// indo-european -> balto-slavic -> slavic
		case CLD2::POLISH:
			return langPolish;
		case CLD2::RUSSIAN:
			return langRussian;
		case CLD2::CZECH:
			return langCzech;
		case CLD2::LATVIAN:
			return langLatvian;
		case CLD2::LITHUANIAN:
			return langLithuanian;
		case CLD2::BULGARIAN:
			return langBulgarian;
		case CLD2::CROATIAN:
		case CLD2::SERBIAN:
		case CLD2::BOSNIAN:
		case CLD2::MONTENEGRIN:
			return langSerboCroatian;
		case CLD2::UKRAINIAN:
		case CLD2::MACEDONIAN:
		case CLD2::BELARUSIAN:
			return langUnwanted;
		case CLD2::SLOVENIAN:
			return langSlovenian;
		case CLD2::SLOVAK:
			return langSlovak;

		// writing system: indo-european -> balto-slavic -> slavic
		case CLD2::X_Cyrillic:
			return langUnknown;
		case CLD2::X_Glagolitic:
			return langUnwanted;

		// indo-european -> celtic -> insular celtic
		case CLD2::IRISH:
			return langIrish;
		case CLD2::WELSH:
			return langWelsh;
		case CLD2::SCOTS_GAELIC:
			return langScottishGaelic;
		case CLD2::MANX:
			return langManx;
		case CLD2::BRETON:
			return langUnknown;

		// writing system: old irish
		case CLD2::X_Ogham:
			return langUnwanted;

		// indo-european -> germanic -> east germanic
		case CLD2::X_Gothic:
			return langGothic;

		// indo-european -> germanic -> north germanic
		case CLD2::DANISH:
			return langDanish;
		case CLD2::NORWEGIAN:
		case CLD2::NORWEGIAN_N:
			return langNorwegian;
		case CLD2::SWEDISH:
			return langSwedish;
		case CLD2::ICELANDIC:
			return langIcelandic;
		case CLD2::FAROESE:
			return langFaroese;

		// indo-european -> germanic -> west germanic
		case CLD2::ENGLISH:
			return langEnglish;
		case CLD2::DUTCH:
			return langDutch;
		case CLD2::GERMAN:
			return langGerman;
		case CLD2::LUXEMBOURGISH:
			return langLuxembourgish;
		case CLD2::FRISIAN:
		case CLD2::YIDDISH:
		case CLD2::SCOTS:
			return langUnknown;
		case CLD2::AFRIKAANS:
			return langUnwanted;

		// writing system: indo-european -> germanic
		case CLD2::X_Runic:
		case CLD2::X_Deseret:
		case CLD2::X_Shavian:
			return langUnwanted;

		// indo-european -> hellenic
		case CLD2::GREEK:
		case CLD2::X_Greek:
			return langGreek;

		// writing system: greek
		case CLD2::X_Cypriot:
		case CLD2::X_Linear_B:
			return langUnwanted;

		// indo-european -> indo-aryan -> central zone
		case CLD2::HINDI:
			return langHindi;
		case CLD2::URDU:
			return langUnwanted;

		// indo-european -> indo-aryan -> dardic
		case CLD2::KASHMIRI:
			return langUnwanted;

		// indo-european -> indo-aryan -> eastern zone
		case CLD2::BENGALI:
		case CLD2::X_Bengali:
			return langBengala;
		case CLD2::BIHARI:
		case CLD2::ORIYA:
		case CLD2::X_Oriya:
		case CLD2::ASSAMESE:
			return langUnwanted;

		// indo-european -> indo-aryan -> greater punjabi
		case CLD2::PUNJABI:
			return langUnwanted;

		// indo-european -> indo-aryan -> northwestern zone
		case CLD2::SINDHI:
			return langUnwanted;

		// indo-european -> indo-aryan -> sanskrit
		case CLD2::NEPALI:
		case CLD2::SANSKRIT:
			return langUnwanted;

		// indo-european -> indo-aryan -> southern
		case CLD2::MARATHI:
		case CLD2::SINHALESE:
		case CLD2::X_Sinhala:
		case CLD2::DHIVEHI:
			return langUnwanted;

		// indo-european -> indo-aryan -> western
		case CLD2::GUJARATI:
		case CLD2::X_Gujarati:
		case CLD2::RAJASTHANI:
			return langUnwanted;

		// writing system: indo-european -> indo-aryan
		case CLD2::X_Thaana:
		case CLD2::X_Devanagari:
		case CLD2::X_Gurmukhi:
		case CLD2::X_Brahmi:
		case CLD2::X_Kaithi:
		case CLD2::X_Chakma:
		case CLD2::X_Sharada:
		case CLD2::X_Kharoshthi:
		case CLD2::X_Syloti_Nagri:
		case CLD2::X_Saurashtra:
		case CLD2::X_Takri:
			return langUnwanted;

		// indo-european -> indo-iranian -> iranian
		case CLD2::PERSIAN:
			return langPersian;
		case CLD2::KURDISH:
			return langKurdish;
		case CLD2::PASHTO:
		case CLD2::TAJIK:
		case CLD2::OSSETIAN:
			return langUnwanted;

		// writing system: indo-european -> indo-iranian -> iranian
		case CLD2::X_Avestan:
		case CLD2::X_Inscriptional_Pahlavi:
		case CLD2::X_Inscriptional_Parthian:
		case CLD2::X_Old_Persian:
			return langUnwanted;

		// indo-european -> italic -> latino-faliscan
		case CLD2::LATIN:
		case CLD2::X_Latin:
			return langLatin;

		// indo-european -> italic -> romance
		case CLD2::FRENCH:
			return langFrench;
		case CLD2::ITALIAN:
			return langItalian;
		case CLD2::PORTUGUESE:
			return langPortuguese;
		case CLD2::SPANISH:
			return langSpanish;
		case CLD2::ROMANIAN:
			return langRomanian;
		case CLD2::GALICIAN:
			return langGalician;
		case CLD2::CATALAN:
			return langCatalan;
		case CLD2::OCCITAN:
		case CLD2::RHAETO_ROMANCE:
		case CLD2::CORSICAN:
			return langUnknown;
		case CLD2::HAITIAN_CREOLE:
		case CLD2::MAURITIAN_CREOLE:
			return langUnwanted;

		// writing system: indo-european -> italic
		case CLD2::X_Old_Italic:
			return langUnwanted;

		// iroquoian -> southern iroquoian
		case CLD2::CHEROKEE:
		case CLD2::X_Cherokee:
			return langUnwanted;

		// japonic
		case CLD2::JAPANESE:
			return langJapanese;

		// writing system: japanese
		case CLD2::X_Hiragana:
		case CLD2::X_Katakana:
			return langJapanese;

		// kartvelian -> karto-zan
		case CLD2::GEORGIAN:
		case CLD2::X_Georgian:
			return langGeorgian;

		// koreanic
		case CLD2::KOREAN:
			return langKorean;

		// writing system: korean
		case CLD2::X_Hangul:
			return langKorean;

		// mongolic
		case CLD2::MONGOLIAN:
			return langUnwanted;

		// writing system: mongolian
		case CLD2::X_Phags_Pa:
		case CLD2::X_Mongolian:
			return langUnwanted;

		// ngbandi-based creole
		case CLD2::SANGO:
			return langUnwanted;

		// niger-congo -> atlantic-congo -> benue-congo
		case CLD2::SWAHILI:
		case CLD2::XHOSA:
		case CLD2::ZULU:
		case CLD2::SESOTHO:
		case CLD2::LINGALA:
		case CLD2::SHONA:
		case CLD2::KINYARWANDA:
		case CLD2::RUNDI:
		case CLD2::SISWANT:
		case CLD2::TSONGA:
		case CLD2::TSWANA:
		case CLD2::GANDA:
		case CLD2::LOZI:
		case CLD2::LUBA_LULUA:
		case CLD2::NYANJA:
		case CLD2::PEDI:
		case CLD2::TUMBUKA:
		case CLD2::VENDA:
		case CLD2::NDEBELE:
			return langUnwanted;

		// writing system: niger-congo -> atlantic-congo -> benue-congo
		case CLD2::X_Bamum:
			return langUnwanted;

		// niger-congo -> atlantic-congo -> kwa
		case CLD2::TWI:
		case CLD2::AKAN:
		case CLD2::GA:
			return langUnwanted;

		// writing system: niger-congo -> mande
		case CLD2::X_Nko:
		case CLD2::X_Vai:
			return langUnwanted;

		// niger-congo -> atlantic-congo -> senegambian
		case CLD2::WOLOF:
			return langUnwanted;

		// niger-congo -> atlantic-congo -> volta-niger
		case CLD2::YORUBA:
		case CLD2::IGBO:
		case CLD2::EWE:
			return langUnwanted;

		// nilo-saharan -> eastern sudanic -> nilotic
		case CLD2::LUO_KENYA_AND_TANZANIA:
			return langUnwanted;

		// northwest caucasian -> abazgi
		case CLD2::ABKHAZIAN:
			return langUnwanted;

		// quechuan languages
		case CLD2::QUECHUA:
		case CLD2::AYMARA:
			return langUnwanted;

		// sino-tibetan -> lolo-burmese
		case CLD2::BURMESE:
		case CLD2::X_Yi:
			return langUnwanted;

		// sino-tibetan -> mahakiranti
		case CLD2::LIMBU:
		case CLD2::X_Limbu:
		case CLD2::NEWARI:
			return langUnwanted;

		// sino-tibetan -> tibeto-kanauri
		case CLD2::TIBETAN:
		case CLD2::X_Tibetan:
		case CLD2::DZONGKHA:
			return langUnwanted;

		// sino-tibetan -> sinitic
		case CLD2::CHINESE:
			return langChineseSimp;
		case CLD2::CHINESE_T:
			return langChineseTrad;

		// writing system: sino-tibetan
		case CLD2::X_Lepcha:
		case CLD2::X_Kayah_Li:
		case CLD2::X_Lisu:
		case CLD2::X_Myanmar:
		case CLD2::X_Meetei_Mayek:
			return langUnwanted;

		// writing system: chinese
		case CLD2::X_Bopomofo:
		case CLD2::X_Han:
			return langChineseTrad;

		// tai-kadai -> tai
		case CLD2::THAI:
		case CLD2::X_Thai:
			return langThai;
		case CLD2::LAOTHIAN:
		case CLD2::X_Lao:
		case CLD2::ZHUANG:
			return langUnwanted;

		// writing system: tai-kadai -> tai
		case CLD2::X_Tai_Le:
		case CLD2::X_New_Tai_Lue:
		case CLD2::X_Tai_Tham:
		case CLD2::X_Tai_Viet:
			return langUnwanted;

		// tupi-guarani -> guarani
		case CLD2::GUARANI:
			return langUnwanted;

		// turkic -> common turkic -> oghuz
		case CLD2::TURKISH:
			return langTurkish;
		case CLD2::AZERBAIJANI:
		case CLD2::TURKMEN:
			return langUnwanted;

		// turkic -> common turkic -> karluk
		case CLD2::UZBEK:
		case CLD2::UIGHUR:
			return langUnwanted;

		// turkic -> common turkic -> kipchak
		case CLD2::KYRGYZ:
		case CLD2::KAZAKH:
		case CLD2::TATAR:
		case CLD2::BASHKIR:
			return langUnwanted;

		// writing system: turkic
		case CLD2::X_Old_Turkic:
			return langUnwanted;

		// uralic -> finnic
		case CLD2::FINNISH:
			return langFinnish;
		case CLD2::ESTONIAN:
			return langEstonian;

		// uralic -> finno-ugric
		case CLD2::HUNGARIAN:
			return langHungarian;

		// vasconic
		case CLD2::BASQUE:
			return langBasque;

		// constructed language
		case CLD2::ESPERANTO:
			return langEsperanto;
		case CLD2::INTERLINGUA:
		case CLD2::INTERLINGUE:
		case CLD2::VOLAPUK:
		case CLD2::X_KLINGON:
			return langUnwanted;

		// unclassified
		case CLD2::X_Meroitic_Cursive:
		case CLD2::X_Meroitic_Hieroglyphs:
			return langUnwanted;

		// writing system
		case CLD2::X_Braille:
			return langUnwanted;

		// unknown
		case CLD2::X_BORK_BORK_BORK:
		case CLD2::X_PIG_LATIN:
		case CLD2::X_HACKER:
		case CLD2::X_ELMER_FUDD:
		case CLD2::X_Common:
		case CLD2::X_Inherited:
		case CLD2::TG_UNKNOWN_LANGUAGE:
		case CLD2::UNKNOWN_LANGUAGE:
		default:
			return langUnknown;
	}
}

static const std::map<std::string, CLD2::Language> s_langCLD3toCLD2 = {
	{ "af", CLD2::AFRIKAANS },
	{ "am", CLD2::AMHARIC },
	{ "ar", CLD2::ARABIC},
	{ "az", CLD2::AZERBAIJANI },
	{ "be", CLD2::BELARUSIAN },
	{ "bg", CLD2::BULGARIAN },
	{ "bg-Latn", CLD2::BULGARIAN },
	{ "bn", CLD2::BENGALI },
	{ "bs", CLD2::BOSNIAN },
	{ "ca", CLD2::CATALAN },
	{ "ceb", CLD2::CEBUANO },
	{ "co", CLD2::CORSICAN },
	{ "cs", CLD2::CZECH },
	{ "cy", CLD2::WELSH },
	{ "da", CLD2::DANISH },
	{ "de", CLD2::GERMAN },
	{ "el", CLD2::GREEK },
	{ "el-Latn", CLD2::GREEK },
	{ "en", CLD2::ENGLISH },
	{ "eo", CLD2::ESPERANTO },
	{ "es", CLD2::SPANISH },
	{ "et", CLD2::ESTONIAN },
	{ "eu", CLD2::BASQUE },
	{ "fa", CLD2::PERSIAN },
	{ "fi", CLD2::FINNISH },
	{ "fil", CLD2::TAGALOG },
	{ "fr", CLD2::FRENCH },
	{ "fy", CLD2::FRISIAN },
	{ "ga", CLD2::IRISH },
	{ "gd", CLD2::SCOTS_GAELIC },
	{ "gl", CLD2::GALICIAN },
	{ "gu", CLD2::GUJARATI },
	{ "ha", CLD2::HAUSA },
	{ "haw", CLD2::HAWAIIAN },
	{ "hi", CLD2::HINDI },
	{ "hi-Latn", CLD2::HINDI },
	{ "hmn", CLD2::HMONG },
	{ "hr", CLD2::CROATIAN },
	{ "ht", CLD2::HAITIAN_CREOLE },
	{ "hu", CLD2::HUNGARIAN },
	{ "hy", CLD2::ARMENIAN },
	{ "id", CLD2::INDONESIAN },
	{ "ig", CLD2::IGBO },
	{ "is", CLD2::ICELANDIC },
	{ "it", CLD2::ITALIAN },
	{ "iw", CLD2::HEBREW },
	{ "ja", CLD2::JAPANESE },
	{ "ja-Latn", CLD2::JAPANESE },
	{ "jv", CLD2::JAVANESE },
	{ "ka", CLD2::GEORGIAN },
	{ "kk", CLD2::KAZAKH },
	{ "km", CLD2::KHMER },
	{ "kn", CLD2::KANNADA },
	{ "ko", CLD2::KOREAN },
	{ "ku", CLD2::KURDISH },
	{ "ky", CLD2::KYRGYZ },
	{ "la", CLD2::LATIN },
	{ "lb", CLD2::LUXEMBOURGISH },
	{ "lo", CLD2::LAOTHIAN },
	{ "lt", CLD2::LITHUANIAN },
	{ "lv", CLD2::LATVIAN },
	{ "mg", CLD2::MALAGASY },
	{ "mi", CLD2::MAORI },
	{ "mk", CLD2::MACEDONIAN },
	{ "ml", CLD2::MALAYALAM },
	{ "mn", CLD2::MONGOLIAN },
	{ "mr", CLD2::MARATHI },
	{ "ms", CLD2::MALAY },
	{ "mt", CLD2::MALTESE },
	{ "my", CLD2::BURMESE },
	{ "ne", CLD2::NEPALI },
	{ "nl", CLD2::DUTCH },
	{ "no", CLD2::NORWEGIAN },
	{ "ny", CLD2::NYANJA },
	{ "pa", CLD2::PUNJABI },
	{ "pl", CLD2::POLISH },
	{ "ps", CLD2::PASHTO },
	{ "pt", CLD2::PORTUGUESE },
	{ "ro", CLD2::ROMANIAN },
	{ "ru", CLD2::RUSSIAN },
	{ "ru-Latn", CLD2::RUSSIAN },
	{ "sd", CLD2::SINDHI },
	{ "si", CLD2::SINHALESE },
	{ "sk", CLD2::SLOVAK },
	{ "sl", CLD2::SLOVENIAN },
	{ "sm", CLD2::SAMOAN },
	{ "sn", CLD2::SHONA },
	{ "so", CLD2::SOMALI },
	{ "sq", CLD2::ALBANIAN },
	{ "sr", CLD2::SERBIAN },
	{ "st", CLD2::SESOTHO },
	{ "su", CLD2::SUNDANESE },
	{ "sv", CLD2::SWEDISH },
	{ "sw", CLD2::SWAHILI },
	{ "ta", CLD2::TAMIL },
	{ "te", CLD2::TELUGU },
	{ "tg", CLD2::TAJIK },
	{ "th", CLD2::THAI },
	{ "tr", CLD2::TURKISH },
	{ "uk", CLD2::UKRAINIAN },
	{ "ur", CLD2::URDU },
	{ "uz", CLD2::UZBEK },
	{ "vi", CLD2::VIETNAMESE },
	{ "xh", CLD2::XHOSA },
	{ "yi", CLD2::YIDDISH },
	{ "yo", CLD2::YORUBA },
	{ "zh", CLD2::CHINESE },
	{ "zh-Latn", CLD2::CHINESE },
	{ "zu", CLD2::ZULU }
};

static lang_t convertLangCLD3(std::string &language) {
	try {
		CLD2::Language langCLD2 = s_langCLD3toCLD2.at(language);
		return convertLangCLD2(langCLD2);
	} catch (std::out_of_range &e) {
		return langUnknown;
	}
}

lang_t FxLanguage::getLangIdCLD2(bool isPlainText, const char *content, int32_t contentLen,
                                 const char *contentLanguage, int32_t contentLanguageLen,
                                 const char *tld, int32_t tldLen) {
	if (contentLen == 0) {
		return langUnknown;
	}

	// detect language hints

	// language tag format:
	//   Language-Tag = Primary-tag *( "-" Subtag )
	//   Primary-tag = 1*8ALPHA
	//   Subtag = 1*8ALPHA
	// HTTP header Content-Language: field
	std::string content_language_hint(contentLanguage, contentLanguageLen);
	std::string tld_hint(tld, tldLen);
	int encoding_hint = CLD2::UNKNOWN_ENCODING; // encoding detector applied to the input document
	CLD2::Language language_hint = CLD2::UNKNOWN_LANGUAGE; // any other context

	logDebug(g_conf.m_logDebugLang, "lang: cld2: using content_language_hint='%s' tld_hint='%s'", content_language_hint.c_str(), tld_hint.c_str());

	CLD2::CLDHints cldhints = {content_language_hint.c_str(), tld_hint.c_str(), encoding_hint, language_hint};

	int flags = 0;
	//flags |= CLD2::kCLDFlagBestEffort;

	CLD2::Language language3[3] = {CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE, CLD2::UNKNOWN_LANGUAGE};
	int percent3[3] = {};
	double normalized_score3[3] = {};

	CLD2::ResultChunkVector *resultchunkvector = NULL;

	int text_bytes = 0;
	bool is_reliable = false;
	int valid_prefix_bytes = 0;

	CLD2::Language language = CLD2::ExtDetectLanguageSummaryCheckUTF8(content,
	                                                                  contentLen,
	                                                                  isPlainText,
	                                                                  &cldhints,
	                                                                  flags,
	                                                                  language3,
	                                                                  percent3,
	                                                                  normalized_score3,
	                                                                  resultchunkvector,
	                                                                  &text_bytes,
	                                                                  &is_reliable,
	                                                                  &valid_prefix_bytes);

	if (!is_reliable) {
		logDebug(g_conf.m_logDebugLang, "lang: cld2: lang0: %s(%d%% %3.0fp)", CLD2::LanguageCode(language3[0]), percent3[0], normalized_score3[0]);
		logDebug(g_conf.m_logDebugLang, "lang: cld2: lang1: %s(%d%% %3.0fp)", CLD2::LanguageCode(language3[1]), percent3[1], normalized_score3[1]);
		logDebug(g_conf.m_logDebugLang, "lang: cld2: lang2: %s(%d%% %3.0fp)", CLD2::LanguageCode(language3[2]), percent3[2], normalized_score3[2]);
		return langUnknown;
	}

	logDebug(g_conf.m_logDebugLang, "lang: cld2: lang: %s", CLD2::LanguageCode(language));
	return convertLangCLD2(language);
}

lang_t FxLanguage::getLangIdCLD3(const char *content, int32_t contentLen) {
	if (contentLen == 0) {
		return langUnknown;
	}

	int minBytes = chrome_lang_id::NNetLanguageIdentifier::kMinNumBytesToConsider;
	int maxBytes = std::max(chrome_lang_id::NNetLanguageIdentifier::kMaxNumBytesToConsider, contentLen);

	chrome_lang_id::NNetLanguageIdentifier lang_id(minBytes, maxBytes);
	auto result = lang_id.FindLanguage(content);
	if (!result.is_reliable) {
		logDebug(g_conf.m_logDebugLang, "lang: cld3: lang: %s(%f %f) is_reliable=%d",
		    result.language.c_str(), result.probability, result.proportion, result.is_reliable);
		return langUnknown;
	}

	logDebug(g_conf.m_logDebugLang, "lang: cld3: lang: %s", result.language.c_str());
	return convertLangCLD3(result.language);
}

lang_t FxLanguage::pickLanguage(lang_t contentLangIdCld2, lang_t contentLangIdCld3, lang_t summaryLangIdCld2,
                                lang_t charsetLangId, lang_t langIdGB) {
	if (summaryLangIdCld2 == langChineseSimp || summaryLangIdCld2 == langChineseTrad) {
		return summaryLangIdCld2;
	}

	if (contentLangIdCld3 == langChineseSimp || contentLangIdCld3 == langChineseTrad || contentLangIdCld3 == langJapanese) {
		return contentLangIdCld3;
	}

	if (contentLangIdCld2 != langUnknown) {
		return contentLangIdCld2;
	}

	if (contentLangIdCld3 != langUnknown) {
		return contentLangIdCld3;
	}

	if (summaryLangIdCld2 != langUnknown) {
		return summaryLangIdCld2;
	}

	if (charsetLangId != langUnknown) {
		return charsetLangId;
	}

	return langIdGB;
}