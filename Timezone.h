#ifndef GB_TIMEZONE_H
#define GB_TIMEZONE_H

#include <inttypes.h>

#define BADTIMEZONE 999999

// "s" is the timezone, like "EDT" and we return # of secs to add to UTC
// to get the current time in that time zone.
// returns BADTIMEZONE if "s" is unknown timezone
int32_t getTimeZone ( const char *s ) ;

void resetTimezoneTables();

#endif // GB_TIMEZONE_H
