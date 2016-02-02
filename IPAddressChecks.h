#ifndef IPADDRESS_CHECKS_H_
#define IPADDRESS_CHECKS_H_

#include <inttypes.h>


void initialize_ip_address_checks();

//guess distance to IP
static const unsigned ip_distance_ourselves = 0;
static const unsigned ip_distance_lan = 1;
static const unsigned ip_distance_nearby = 2;
//  >2 = internet
unsigned ip_distance(uint32_t ip/*network-order*/);

#endif
