#ifndef ADULT_CHECK_H_
#define ADULT_CHECK_H_

#include <inttypes.h>
#include <stddef.h>

int32_t getAdultPoints ( char *s, int32_t slen, const char *url );

bool isAdult(const char *s, int32_t slen, const char **loc = nullptr);

bool isAdultUrl(const char *s, int32_t slen);

bool isAdultTLD(const char *tld, size_t tld_len);

#endif
