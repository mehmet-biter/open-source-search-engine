#ifndef IPADDRESS_CHECKS_H_
#define IPADDRESS_CHECKS_H_

#include <inttypes.h>


void initialize_ip_address_checks();

//guess distance to IP
//   0 = ourselves
//   1 = LAN
//   2 = nearby
//  >2 = internet

unsigned ip_distance(uint32_t ip/*network-order*/);

#endif
