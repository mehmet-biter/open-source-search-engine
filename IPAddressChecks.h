#ifndef GB_IPADDRESSCHECKS_H
#define GB_IPADDRESSCHECKS_H

#include <inttypes.h>


void initialize_ip_address_checks();

//guess distance to IP
static const unsigned ip_distance_ourselves = 0;
static const unsigned ip_distance_lan = 1;
static const unsigned ip_distance_nearby = 2;
//  >2 = internet
unsigned ip_distance(uint32_t ip/*network-order*/);

//Is the IP an internal IP as in "we control the hosts and allow more aggressive crawling"
bool is_internal_net_ip(uint32_t ip/*network-order*/);

//Is the IP an internal-like that we allow to use the udp protocol without being registered as part of the cluster?
bool is_trusted_protocol_ip(uint32_t ip/*network-order*/);

//Make a guess if the two IPs are on the same network / controlled by the same
//entity and links between them should be treated as internal links
bool is_same_network_linkwise(uint32_t ip_a/*network order*/, uint32_t ip_b/*network order*/);

#endif // GB_IPADDRESSCHECKS_H
