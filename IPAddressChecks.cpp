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


//Determine if the IP is one that we control/own and we therefore are allowed to
//crawl more agressively. We assume that the "intranet" covers the loopback
//interface, private networks and direct LAN networks by default. Eventually
//this may get extended with configuration but this seems like the right thing
//to do out-of-the-box.
bool is_internal_net_ip(uint32_t ip/*network-order*/)
{
	ip = ntohl(ip);
	
	//loopback?
	if(ip==0x7f000001)
		return true;
	//linux loopback?
	if((ip&0xff000000) == 0x7f000000)
		return true;
	
	//private networks?
	if((ip&0xff000000)==0x0a000000) //10.0.0.0/8
		return true;
	if((ip&0xfff00000)==0xac100000) //172.16.0.0/12
		return true;
	if((ip&0xffff0000)==0xc0a80000) //192.168.0.0/16
		return true;
	//Private networks could still be over a limited WAN link but at least
	//it will not annoy external innocent parties.
	
	//On direct lan?
	for(size_t i=0; i<local_nets; i++)
		if((ip&local_net_mask[i])==(local_net_address[i]&local_net_mask[i]))
			return true;
	
	//todo: allow configuration of "intranet networks"
	
	//probably not an intranet host, so we err on the side of caution
	return false;
}


// //Determine if the IP is one that we would trust a UDP packet from without the
// //IP being part of the cluster. We trust loopback interface, private networks
// //and direct LAN networks by default. Eventually this may get extended with
// //configuration but this seems like the right thing to do out-of-the-box.
bool is_trusted_protocol_ip(uint32_t ip/*network-order*/)
{
	ip = ntohl(ip);
	
	//loopback?
	if(ip==0x7f000001)
		return true;
	//linux loopback?
	if((ip&0xff000000) == 0x7f000000)
		return true;
	
	//private networks?
	if((ip&0xff000000)==0x0a000000) //10.0.0.0/8
		return true;
	if((ip&0xfff00000)==0xac100000) //172.16.0.0/12
		return true;
	if((ip&0xffff0000)==0xc0a80000) //192.168.0.0/16
		return true;
	
	//On direct lan?
	for(size_t i=0; i<local_nets; i++)
		if((ip&local_net_mask[i])==(local_net_address[i]&local_net_mask[i]))
			return true;
	
	//Trusted/private networks could still be over a WAN link
	//todo: allow configuration of "trusted networks"
	
	//probably not a trusted host, so we err on the side of caution
	return false;
}


#if 0
Disabled until we have measured if there is any benefit of having these checks concerning internal/external links based solely on IP-address.
//This is a replacement for the hardcoded checks on the /16 prefix of the ip addresses
//This is slightly more intelligent.
//The correct solution would be to have access to whois information and check the
//identity of the owner, and have special cases for all hosting providers.
bool is_same_network_linkwise(uint32_t ip_a/*network order*/, uint32_t ip_b/*network order*/)
{
	if(ip_a==ip_b) {
		//same IP - same network
		//This is inaccurate for some hosting providers, apache with virtual
		//domains and general frontends eg. cloudflare. It is widly inaccurate for CDNs
		//but there are rarely permalinks to CNDs so it's not a major problem.
		return true;
	}
	if((ip_a&0x000000ff)!=(ip_b&0x000000ff)) {
		//Top 8 bit are different so they are not even on the same class-A network. So must be different.
		//This fails if a company has a fragmented address space due to mergers. Eg. HP now owns Compaqs
		//networks and Compaq owned DEC's networks, so ... quite messy and inexact. But better than nothing.
		return false;
	}
	//make educated guesses for some of the known class-A owners
	//from the test above we know that the top 8 bites are quals
	if((ip_a&0xff)==3)
		return true; //General Electric
	if((ip_a&0xff)==6)
		return true; //DoD
	if((ip_a&0xff)==7)
		return true; //DoD
	if((ip_a&0xff)==9)
		return true; //IBM
	if((ip_a&0xff)==11)
		return true; //DoD
	if((ip_a&0xff)==15)
		return true; //HP
	//make a wild guess
	if((ip_a&0x0000ffff)==(ip_b&0x0000ffff)) //same /16 net?
		return true;
	return false;
}
#endif
