// Matt Wells, copyright Jun 2001

#ifndef _IPROUTINES_H_
#define _IPROUTINES_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int32_t  atoip ( const char *s , int32_t slen );
int32_t  atoip ( const char *s );//, int32_t slen );
char *iptoa ( int32_t ip );
// . get domain of ip address
// . first byte is the host (little endian)
int32_t  ipdom ( int32_t ip ) ;
// most significant 2 bytes of ip
int32_t  iptop ( int32_t ip ) ;
// . is least significant byte a zero?
// . if it is then this ip is probably representing a whole ip domain
bool  isIpDom ( int32_t ip ) ;
// are last 2 bytes 0's?
int32_t  isIpTop ( int32_t ip ) ;

// returns number of top bytes in comon
int32_t  ipCmp ( int32_t ip1 , int32_t ip2 ) ;


#endif

