// Matt Wells, copyright Jun 2001

#ifndef _IPROUTINES_H_
#define _IPROUTINES_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int32_t  atoip ( const char *s , int32_t slen );
int32_t  atoip ( const char *s );
char *iptoa ( int32_t ip );

// . get domain of ip address
// . first byte is the host (little endian)
int32_t  ipdom ( int32_t ip ) ;

// most significant 2 bytes of ip
int32_t  iptop ( int32_t ip ) ;

// returns number of top bytes in comon
int32_t  ipCmp ( int32_t ip1 , int32_t ip2 ) ;


#endif

