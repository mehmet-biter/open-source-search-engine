// Matt Wells, copyright Nov 2001

#ifndef GB_DOMAINS_H
#define GB_DOMAINS_H

// . get the domain name (name + tld) from a hostname
// . returns ptr into host that marks the domain name
// returns NULL if it is TLD-only
const char *getDomain(const char *host, int32_t hostLen, const char *tld, int32_t *dlen);

// when host is like 192.0.2.1  use this one
char *getDomainOfIp ( char *host , int32_t hostLen , int32_t *dlen );
const char *getDomainOfIp(const char *host, int32_t hostLen, int32_t *dlen);

// used by getDomain() above
const char *getTLD ( const char *host , int32_t hostLen ) ;
//eternally stable output by using non-dynamic list of TLDs
const char *getTLD_static(const char *host, int32_t hostLen);

//is the string (com or co.uk) a known TLD?
bool isTLD(const char *tld, int32_t tldLen);

bool initializeDomains(const char *data_dir);
void finalizeDomains();

#endif // GB_DOMAINS_H
