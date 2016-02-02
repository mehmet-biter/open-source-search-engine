#include "IPAddressChecks.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>


#define MAX_LOCAL_IPS 256
static uint32_t local_ip[MAX_LOCAL_IPS];
static size_t local_ips = 0;

#define MAX_LOCAL_NETS 256
static uint32_t local_net_address[MAX_LOCAL_NETS];
static uint32_t local_net_mask[MAX_LOCAL_NETS];
static size_t local_nets = 0;

void initialize_ip_address_checks()
{
	local_ips = 0;
	local_nets = 0;
	
	struct ifaddrs *ifap = NULL;
	if(getifaddrs(&ifap)==0) {
		for(struct ifaddrs *p = ifap; p && local_ips<MAX_LOCAL_IPS; p=p->ifa_next) {
			if(!p->ifa_addr)
				continue;
			switch(p->ifa_addr->sa_family) {
				case AF_INET:
					local_ip[local_ips++] = ntohl(((struct sockaddr_in*)(void*)p->ifa_addr)->sin_addr.s_addr);
					local_net_address[local_nets] = ntohl(((struct sockaddr_in*)(void*)p->ifa_addr)->sin_addr.s_addr);
					local_net_mask[local_nets] = ntohl(((struct sockaddr_in*)(void*)p->ifa_netmask)->sin_addr.s_addr);
					local_nets++;
					break;
				case AF_INET6: //todo
				default:
					//unknown address type
					continue;
			}
		}

		freeifaddrs(ifap);
	}
}


unsigned ip_distance(uint32_t ip/*network-order*/)
{
	ip = ntohl(ip);
	
	//quick tests first
	//127.0.0.1 ?
	if(ip==0x7f000001)
		return ip_distance_ourselves;
	//linux speciality: default loopback network is 127.0.0.0/8
	if((ip&0xff000000) == 0x7f000000)
		return ip_distance_ourselves;
	
	//ok, not loopback. Is it one of our own local IP-addresses?
	for(size_t i=0; i<local_ips; i++)
		if(local_ip[i] == ip)
			return ip_distance_ourselves;
	
	//is it on one of the local nets?
	for(size_t i=0; i<local_nets; i++)
		if((ip&local_net_mask[i])==(local_net_address[i]&local_net_mask[i]))
			return ip_distance_lan;
	
	//todo: support configuration of "nearby" networks, or do dynamic measurement of them.
	//hackish way of test if it is a nearby network: do they share the /16 prefix?
	for(size_t i=0; i<local_ips; i++)
		if((ip&0xffff0000)==(local_ip[i]&0xffff0000))
			return ip_distance_nearby;
	
	return 3;
}

