#ifndef TIMEZONE_H_
#define TIMEZONE_H_

#include "gb-include.h"

// now time zones
struct TimeZone {
	char m_name[16];
	// tzinfo:
        int32_t m_hourMod;
        int32_t m_minMod;
        int32_t m_modType;
};

#define BADTIMEZONE 999999

// "s" is the timezone, like "EDT" and we return # of secs to add to UTC
// to get the current time in that time zone.
// returns BADTIMEZONE if "s" is unknown timezone
int32_t getTimeZone ( char *s ) ;

// . returns how many words starting at i are in the time zone
// . 0 means not a timezone
int32_t getTimeZoneWord ( int32_t i , int64_t *wids , int32_t nw , 
		       TimeZone **tzptr , int32_t niceness );


void resetTimezoneTables();

#endif
