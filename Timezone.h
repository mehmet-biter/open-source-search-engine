#ifndef TIMEZONE_H_
#define TIMEZONE_H_

#include "gb-include.h"

#define BADTIMEZONE 999999

// "s" is the timezone, like "EDT" and we return # of secs to add to UTC
// to get the current time in that time zone.
// returns BADTIMEZONE if "s" is unknown timezone
int32_t getTimeZone ( const char *s ) ;

void resetTimezoneTables();

#endif
