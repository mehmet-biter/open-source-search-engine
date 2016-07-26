// Matt Wells, copyright Nov 2001

#ifndef GB_DOMAINS_H
#define GB_DOMAINS_H

// . get the domain name (name + tld) from a hostname
// . returns NULL if not in the accepted list
// . "host" must be NULL terminated and in LOWER CASE
// . returns ptr into host that marks the domain name
char *getDomain ( char *host , int32_t hostLen , char *tld , int32_t *dlen );

// when host is like 192.0.2.1  use this one
char *getDomainOfIp ( char *host , int32_t hostLen , int32_t *dlen );

// used by getDomain() above
char *getTLD ( char *host , int32_t hostLen ) ;

const char* getPrivacoreBlacklistedTLD();

#endif // GB_DOMAINS_H
