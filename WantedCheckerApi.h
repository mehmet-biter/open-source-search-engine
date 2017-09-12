#ifndef WANTEDCHECKERAPI_H
#define WANTEDCHECKERAPI_H
#include <string>
#include <vector>
#include <stddef.h>


namespace WantedCheckApi {

// check_domain()
//
//Checks if the domain is wanted. Eg "www.example.com"
//The callout may employ any logic to determine it, including statistical
//analysis of the domain name. Eg "fjsiurtiu.sjlqvnmsdf.ibuycarz.com" looks
//spammy.
//This callout is used before inserting into spider queue or doing DNS lookups.

struct DomainCheckResult {
	bool wanted;
};

typedef DomainCheckResult (*check_domain_t)(const std::string &domain);


// check_url()
//
//The callout is meant for checking the path component that cannot easily be
//done with regex. Eg.
//  proxy.example.com/www.spammy.spam.com
//  stat.example.com/www.legit_site.com
//  statistik.example.dk/statistik/www.legit_site.com?timespan=1y
//This callout is used before inserting into spider queue or doing DNS lookups.

struct UrlCheckResult {
	bool wanted;
};

typedef UrlCheckResult (*check_url_t)(const std::string &url);


// check_multi_content
//
//Called after content has been fetched and transcoded into UTF-8
//Possible outcomes:
//  wanted
//  unwanted
//  dont_know_but_please_fetch_me_some_other_doc(casino.js)
// The first item in the 'content' array is the page we are asking about. The
// other items are documents the callout asked for.

struct MultiContentCheckResult {
	enum {
		wanted,
		unwanted,
		fetch_other_page
	} result;
	std::string other_url_to_fetch;
};

struct MultiContent {
	const void *ptr;
	size_t size;
	std::string content_type;
	int http_result;             //-1 = could not fetch
};


typedef MultiContentCheckResult (*check_multi_content_t)(const std::vector<MultiContent> &content);


struct APIDescriptorBlock {
	check_domain_t check_domain_pfn;
	check_url_t check_url_pfn;
	check_multi_content_t check_multi_content_pfn;
	
};

} //namespace


//this is the symbol we will locate in the shlib
extern WantedCheckApi::APIDescriptorBlock wanted_check_api_descriptor_block;


#endif
