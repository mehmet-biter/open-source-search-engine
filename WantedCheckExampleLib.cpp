#include "WantedCheckerApi.h"

//Example library for checking if a domain/url/document is wanted or not.


static WantedCheckApi::DomainCheckResult example_check_domain(const std::string &domain) {
	WantedCheckApi::DomainCheckResult result;
	result.wanted = true;
	
	//Filter out blatant spam
	if(domain=="spam.example.com")
		result.wanted = false;
	if(domain=="phishing.example.com")
		result.wanted = false;
	if(domain=="totally-not-a-scam-trust-me.example.com")
		result.wanted = false;
	
	//Filter out "statistics" sites or similar that embed common domains as sub-domains.
	//This is quite tricky but as an example we filter out www.blablabla.com.something (.com.br being the exception)
	if(domain.find(".com.")!=std::string::npos &&
	   domain.find(".com.br")==std::string::npos)
		result.wanted = false;
	return result;
}


static WantedCheckApi::UrlCheckResult example_check_url(const std::string &url) {
	WantedCheckApi::UrlCheckResult result;
	result.wanted = true;
	//filter out the fictitious scheme "spam://"
	if(url.substr(0,7)=="spam://")
		result.wanted = false;
	if(url.find("evil-penguin-on-hoverboard")!=std::string::npos)
		result.wanted = false;
	return result;
}


//No example for content filtering

// static WantedCheckApi::ContentMultiCheckResult example_check_multi_content(const std::vector<WantedCheckApi::Content> &/*content*/) {
// 	WantedCheckApi::ContentMultiCheckResult result;
// 	result.result = result.wanted;
// 	return result;
// }



WantedCheckApi::APIDescriptorBlock wanted_check_api_descriptor_block = {
	example_check_domain,
	example_check_url,
	NULL //example_check_multi_content
};
