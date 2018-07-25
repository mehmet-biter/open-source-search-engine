#include "CountryCode.h"
#include "HashTable.h"
#include "HashTableX.h"
#include "Lang.h"
#include "Process.h"

#include <sys/types.h>


CountryCode g_countryCode;

static const char * s_countryCode[] = {
	"zz", // Unknown
	"ad", // Principality of Andorra
	"ae", // United Arab Emirates
	"af", // Islamic State of Afghanistan
	"ag", // Antigua and Barbuda
	"ai", // Anguilla
	"al", // Albania
	"am", // Armenia
	"an", // Netherlands Antilles
	"ao", // Angola
	"aq", // Antarctica
	"ar", // Argentina
	"as", // American Samoa
	"at", // Austria
	"au", // Australia
	"aw", // Aruba
	"az", // Azerbaidjan
	"ba", // Bosnia-Herzegovina
	"bb", // Barbados
	"bd", // Bangladesh
	"be", // Belgium
	"bf", // Burkina Faso
	"bg", // Bulgaria
	"bh", // Bahrain
	"bi", // Burundi
	"bj", // Benin
	"bm", // Bermuda
	"bn", // Brunei Darussalam
	"bo", // Bolivia
	"br", // Brazil
	"bs", // Bahamas
	"bt", // Bhutan
	"bv", // Bouvet Island
	"bw", // Botswana
	"by", // Belarus
	"bz", // Belize
	"ca", // Canada
	"cc", // Cocos (Keeling) Islands
	"cf", // Central African Republic
	"cd", // The Democratic Republic of the Congo
	"cg", // Congo
	"ch", // Switzerland
	"ci", // Ivory Coast (Cote D'Ivoire)
	"ck", // Cook Islands
	"cl", // Chile
	"cm", // Cameroon
	"cn", // China
	"co", // Colombia
	"cr", // Costa Rica
	"cs", // Former Czechoslovakia
	"cu", // Cuba
	"cv", // Cape Verde
	"cx", // Christmas Island
	"cy", // Cyprus
	"cz", // Czech Republic
	"de", // Germany
	"dj", // Djibouti
	"dk", // Denmark
	"dm", // Dominica
	"do", // Dominican Republic
	"dz", // Algeria
	"ec", // Ecuador
	"ee", // Estonia
	"eg", // Egypt
	"eh", // Western Sahara
	"er", // Eritrea
	"es", // Spain
	"et", // Ethiopia
	"fi", // Finland
	"fj", // Fiji
	"fk", // Falkland Islands
	"fm", // Micronesia
	"fo", // Faroe Islands
	"fr", // France
	"fx", // France (European Territory)
	"ga", // Gabon
	"gb", // Great Britain
	"gd", // Grenada
	"ge", // Georgia
	"gf", // French Guyana
	"gh", // Ghana
	"gi", // Gibraltar
	"gl", // Greenland
	"gm", // Gambia
	"gn", // Guinea
	"gp", // Guadeloupe (French)
	"gq", // Equatorial Guinea
	"gr", // Greece
	"gs", // S. Georgia & S. Sandwich Isls.
	"gt", // Guatemala
	"gu", // Guam (USA)
	"gw", // Guinea Bissau
	"gy", // Guyana
	"hk", // Hong Kong
	"hm", // Heard and McDonald Islands
	"hn", // Honduras
	"hr", // Croatia
	"ht", // Haiti
	"hu", // Hungary
	"id", // Indonesia
	"ie", // Ireland
	"il", // Israel
	"in", // India
	"io", // British Indian Ocean Territory
	"iq", // Iraq
	"ir", // Iran
	"is", // Iceland
	"it", // Italy
	"jm", // Jamaica
	"jo", // Jordan
	"jp", // Japan
	"ke", // Kenya
	"kg", // Kyrgyz Republic (Kyrgyzstan)
	"kh", // Kingdom of Cambodia
	"ki", // Kiribati
	"km", // Comoros
	"kn", // Saint Kitts & Nevis Anguilla
	"kp", // North Korea
	"kr", // South Korea
	"kw", // Kuwait
	"ky", // Cayman Islands
	"kz", // Kazakhstan
	"la", // Laos
	"lb", // Lebanon
	"lc", // Saint Lucia
	"li", // Liechtenstein
	"lk", // Sri Lanka
	"lr", // Liberia
	"ls", // Lesotho
	"lt", // Lithuania
	"lu", // Luxembourg
	"lv", // Latvia
	"ly", // Libya
	"ma", // Morocco
	"mc", // Monaco
	"md", // Moldavia
	"mg", // Madagascar
	"mh", // Marshall Islands
	"mk", // Macedonia
	"ml", // Mali
	"mm", // Myanmar
	"mn", // Mongolia
	"mo", // Macau
	"mp", // Northern Mariana Islands
	"mq", // Martinique (French)
	"mr", // Mauritania
	"ms", // Montserrat
	"mt", // Malta
	"mu", // Mauritius
	"mv", // Maldives
	"mw", // Malawi
	"mx", // Mexico
	"my", // Malaysia
	"mz", // Mozambique
	"na", // Namibia
	"nc", // New Caledonia (French)
	"ne", // Niger
	"nf", // Norfolk Island
	"ng", // Nigeria
	"ni", // Nicaragua
	"nl", // Netherlands
	"no", // Norway
	"np", // Nepal
	"nr", // Nauru
	"nt", // Neutral Zone
	"nu", // Niue
	"nz", // New Zealand
	"om", // Oman
	"pa", // Panama
	"pe", // Peru
	"pf", // Polynesia (French)
	"pg", // Papua New Guinea
	"ph", // Philippines
	"pk", // Pakistan
	"pl", // Poland
	"pm", // Saint Pierre and Miquelon
	"pn", // Pitcairn Island
	"pr", // Puerto Rico
	"pt", // Portugal
	"pw", // Palau
	"py", // Paraguay
	"qa", // Qatar
	"re", // Reunion (French)
	"ro", // Romania
	"ru", // Russian Federation
	"rw", // Rwanda
	"sa", // Saudi Arabia
	"sb", // Solomon Islands
	"sc", // Seychelles
	"sd", // Sudan
	"se", // Sweden
	"sg", // Singapore
	"sh", // Saint Helena
	"si", // Slovenia
	"sj", // Svalbard and Jan Mayen Islands
	"sk", // Slovak Republic
	"sl", // Sierra Leone
	"sm", // San Marino
	"sn", // Senegal
	"so", // Somalia
	"sr", // Suriname
	"st", // Saint Tome (Sao Tome) and Principe
	"su", // Former USSR
	"sv", // El Salvador
	"sy", // Syria
	"sz", // Swaziland
	"tc", // Turks and Caicos Islands
	"td", // Chad
	"tf", // French Southern Territories
	"tg", // Togo
	"th", // Thailand
	"tj", // Tadjikistan
	"tk", // Tokelau
	"tm", // Turkmenistan
	"tn", // Tunisia
	"to", // Tonga
	"tp", // East Timor
	"tr", // Turkey
	"tt", // Trinidad and Tobago
	"tv", // Tuvalu
	"tw", // Taiwan
	"tz", // Tanzania
	"ua", // Ukraine
	"ug", // Uganda
	"uk", // United Kingdom
	"um", // USA Minor Outlying Islands
	"us", // United States
	"uy", // Uruguay
	"uz", // Uzbekistan
	"va", // Holy See (Vatican City State)
	"vc", // Saint Vincent & Grenadines
	"ve", // Venezuela
	"vg", // Virgin Islands (British)
	"vi", // Virgin Islands (USA)
	"vn", // Vietnam
	"vu", // Vanuatu
	"wf", // Wallis and Futuna Islands
	"ws", // Samoa
	"ye", // Yemen
	"yt", // Mayotte
	"yu", // Yugoslavia
	"za", // South Africa
	"zm", // Zambia
	"zr", // Zaire
	"zw",  // Zimbabwe


	// political entities
	"bl" , // saint bathelemy
	"gg" , // saint martin
	"mf" ,
	"im" ,  // isle of man
	"je" ,   // jersey
	"me" ,  // montenegro
	"ps" , // gaza strip
	"rs" , // serbia
	"tl" // east timor REPEAT!!
};

// map a country id to the two letter country abbr
const char *getCountryCode ( uint16_t crid ) {
	return s_countryCode[crid];
}

uint8_t guessCountryTLD(const char *url) {
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

// get the id from a 2 character country code
uint8_t getCountryId ( const char *cc ) {
	static bool s_init = false;
	static char buf[2000];
	static HashTableX ht;
	char tmp[4];
	if ( ! s_init ) {
		s_init = true;
		// hash them up
		ht.set ( 4 , 1 , -1,buf,2000,false,"ctryids");
		// now add in all the country codes
		int32_t n = (int32_t) sizeof(s_countryCode) / sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			const char *s    = s_countryCode[i];
			//int32_t  slen = strlen ( s );
			// sanity check
			if ( !s[0] || !s[1] || s[2]) { g_process.shutdownAbort(true); }
			// map it to a 4 byte key
			tmp[0]=s[0];
			tmp[1]=s[1];
			tmp[2]=0;
			tmp[3]=0;
			// a val of 0 does not mean empty in HashTableX,
			// that is an artifact of HashTableT
			uint8_t val = i; // +1;
			// add 1 cuz 0 means lang unknown
			if ( ! ht.addKey ( tmp , &val ) ) {
				g_process.shutdownAbort(true); }
		}
	}
	// lookup
	tmp[0]=to_lower_a(cc[0]);
	tmp[1]=to_lower_a(cc[1]);
	tmp[2]=0;
	tmp[3]=0;
	int32_t slot = ht.getSlot ( tmp );
	if ( slot < 0 ) return 0;
	void *val = ht.getValueFromSlot ( slot );
	return *(uint8_t *)val ;
}

static const char *s_countryName[] = {
	"Unknown",
	"Principality of Andorra",
	"United Arab Emirates",
	"Islamic State of Afghanistan",
	"Antigua and Barbuda",
	"Anguilla",
	"Albania",
	"Armenia",
	"Netherlands Antilles",
	"Angola",
	"Antarctica",
	"Argentina",
	"American Samoa",
	"Austria",
	"Australia",
	"Aruba",
	"Azerbaidjan",
	"Bosnia-Herzegovina",
	"Barbados",
	"Bangladesh",
	"Belgium",
	"Burkina Faso",
	"Bulgaria",
	"Bahrain",
	"Burundi",
	"Benin",
	"Bermuda",
	"Brunei Darussalam",
	"Bolivia",
	"Brazil",
	"Bahamas",
	"Bhutan",
	"Bouvet Island",
	"Botswana",
	"Belarus",
	"Belize",
	"Canada",
	"Cocos (Keeling) Islands",
	"Central African Republic",
	"The Democratic Republic of the Congo",
	"Congo",
	"Switzerland",
	"Ivory Coast (Cote D'Ivoire)",
	"Cook Islands",
	"Chile",
	"Cameroon",
	"China",
	"Colombia",
	"Costa Rica",
	"Former Czechoslovakia",
	"Cuba",
	"Cape Verde",
	"Christmas Island",
	"Cyprus",
	"Czech Republic",
	"Germany",
	"Djibouti",
	"Denmark",
	"Dominica",
	"Dominican Republic",
	"Algeria",
	"Ecuador",
	"Estonia",
	"Egypt",
	"Western Sahara",
	"Eritrea",
	"Spain",
	"Ethiopia",
	"Finland",
	"Fiji",
	"Falkland Islands",
	"Micronesia",
	"Faroe Islands",
	"France",
	"France (European Territory)",
	"Gabon",
	"Great Britain",
	"Grenada",
	"Georgia",
	"French Guyana",
	"Ghana",
	"Gibraltar",
	"Greenland",
	"Gambia",
	"Guinea",
	"Guadeloupe (French)",
	"Equatorial Guinea",
	"Greece",
	"S. Georgia & S. Sandwich Isls.",
	"Guatemala",
	"Guam (USA)",
	"Guinea Bissau",
	"Guyana",
	"Hong Kong",
	"Heard and McDonald Islands",
	"Honduras",
	"Croatia",
	"Haiti",
	"Hungary",
	"Indonesia",
	"Ireland",
	"Israel",
	"India",
	"British Indian Ocean Territory",
	"Iraq",
	"Iran",
	"Iceland",
	"Italy",
	"Jamaica",
	"Jordan",
	"Japan",
	"Kenya",
	"Kyrgyz Republic (Kyrgyzstan)",
	"Kingdom of Cambodia",
	"Kiribati",
	"Comoros",
	"Saint Kitts & Nevis Anguilla",
	"North Korea",
	"South Korea",
	"Kuwait",
	"Cayman Islands",
	"Kazakhstan",
	"Laos",
	"Lebanon",
	"Saint Lucia",
	"Liechtenstein",
	"Sri Lanka",
	"Liberia",
	"Lesotho",
	"Lithuania",
	"Luxembourg",
	"Latvia",
	"Libya",
	"Morocco",
	"Monaco",
	"Moldavia",
	"Madagascar",
	"Marshall Islands",
	"Macedonia",
	"Mali",
	"Myanmar",
	"Mongolia",
	"Macau",
	"Northern Mariana Islands",
	"Martinique (French)",
	"Mauritania",
	"Montserrat",
	"Malta",
	"Mauritius",
	"Maldives",
	"Malawi",
	"Mexico",
	"Malaysia",
	"Mozambique",
	"Namibia",
	"New Caledonia (French)",
	"Niger",
	"Norfolk Island",
	"Nigeria",
	"Nicaragua",
	"Netherlands",
	"Norway",
	"Nepal",
	"Nauru",
	"Neutral Zone",
	"Niue",
	"New Zealand",
	"Oman",
	"Panama",
	"Peru",
	"Polynesia (French)",
	"Papua New Guinea",
	"Philippines",
	"Pakistan",
	"Poland",
	"Saint Pierre and Miquelon",
	"Pitcairn Island",
	"Puerto Rico",
	"Portugal",
	"Palau",
	"Paraguay",
	"Qatar",
	"Reunion (French)",
	"Romania",
	"Russian Federation",
	"Rwanda",
	"Saudi Arabia",
	"Solomon Islands",
	"Seychelles",
	"Sudan",
	"Sweden",
	"Singapore",
	"Saint Helena",
	"Slovenia",
	"Svalbard and Jan Mayen Islands",
	"Slovak Republic",
	"Sierra Leone",
	"San Marino",
	"Senegal",
	"Somalia",
	"Suriname",
	"Saint Tome (Sao Tome) and Principe",
	"Former USSR",
	"El Salvador",
	"Syria",
	"Swaziland",
	"Turks and Caicos Islands",
	"Chad",
	"French Southern Territories",
	"Togo",
	"Thailand",
	"Tadjikistan",
	"Tokelau",
	"Turkmenistan",
	"Tunisia",
	"Tonga",
	"East Timor",
	"Turkey",
	"Trinidad and Tobago",
	"Tuvalu",
	"Taiwan",
	"Tanzania",
	"Ukraine",
	"Uganda",
	"United Kingdom",
	"USA Minor Outlying Islands",
	"United States",
	"Uruguay",
	"Uzbekistan",
	"Holy See (Vatican City State)",
	"Saint Vincent & Grenadines",
	"Venezuela",
	"Virgin Islands (British)",
	"Virgin Islands (USA)",
	"Vietnam",
	"Vanuatu",
	"Wallis and Futuna Islands",
	"Samoa",
	"Yemen",
	"Mayotte",
	"Yugoslavia",
	"South Africa",
	"Zambia",
	"Zaire",
	"Zimbabwe", 

	// political entities
	"Saint Barthelemy"  ,// "bl"
	"Saint Martin" , // "gg"
	"Guadeloupe",
	"Isle of Man" , // "im"
	"Jersey" , // "je"
	"Montenegro" , // "me"
	"Gaza Strip" , // "ps"
	"Serbia" , // "rs"
	"East Timor"
};

static const int s_numCountryCodes = sizeof(s_countryCode)/sizeof(s_countryCode[0]);

CountryCode::CountryCode() {
	m_init = false;
	//reset();
}

CountryCode::~CountryCode() {
	m_abbrToName.reset();
	m_abbrToIndex.reset();
}

// this is initializing, not resetting - mdw
void CountryCode::init(void) {
	uint16_t idx;
	if(m_init) {
		m_abbrToName.reset();
		m_abbrToIndex.reset();
	}
	m_init = true;
	if(!m_abbrToName.set(s_numCountryCodes) ||
			!m_abbrToIndex.set(s_numCountryCodes)) {
		// if we can't allocate memory, then we'll
		// just leave it uninitialized
		m_init = false;
		return;
	}
	for(int x = 0; x < s_numCountryCodes; x++) {
		idx = s_countryCode[x][0];
		idx = idx << 8;
		idx |= s_countryCode[x][1];
		m_abbrToName.addKey(idx, s_countryName[x]);
		m_abbrToIndex.addKey(idx, x);
	}

}

bool CountryCode::loadHashTable(void) {
	init();

	return m_init;
}

void CountryCode::reset ( ) {
}

const char *CountryCode::getAbbr(int index) {
	if(index < 0 || index >= s_numCountryCodes) {
		index = 0;
	}
	return(s_countryCode[index]);
}

const char *CountryCode::getName(int index) {
	if(index < 0 || index >= s_numCountryCodes) {
		index = 0;
	}
	return(s_countryName[index]);
}

int CountryCode::getIndexOfAbbr(const char *abbr) {
	if(!m_init) return(0);
	uint16_t idx;
	if(!abbr) return(0);
	idx = abbr[0];
	idx = idx << 8;
	idx |= abbr[1];
	int32_t slot = m_abbrToIndex.getSlot(idx);
	if(slot < 0) return(0);
	return(m_abbrToIndex.getValueFromSlot(slot));
}
