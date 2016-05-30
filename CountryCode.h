#ifndef GB_COUNTRYCODE_H
#define GB_COUNTRYCODE_H

#include "HashTableT.h"
#include "types.h"

// . used by Events.cpp to keep things small
// . get a single byte country id from a 2 character country code
uint8_t getCountryId ( char *cc ) ;

// map a country id to the two letter country abbr
const char *getCountryCode ( uint8_t crid );

class CountryCode {
	public:
		CountryCode();
		~CountryCode();
		void init(void);
		const char *getAbbr(int index);
		const char *getName(int index);
		int getIndexOfAbbr(const char *abbr);
		bool loadHashTable(void);
		void reset();
		uint64_t getLanguagesWritten(int index);

	private:
		bool m_init;
		HashTableT<uint16_t, int>m_abbrToIndex;
		HashTableT<uint16_t, const char *>m_abbrToName;
};

extern CountryCode g_countryCode;

#endif // GB_COUNTRYCODE_H
