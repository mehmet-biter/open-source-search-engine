#include "WantedChecker.h"
#include "WantedCheckerApi.h"
#include "Log.h"
#include <dlfcn.h>
#include <errno.h>
#include <string.h>



static const char shlib_name[] = "wanted_check_api.so";



////////////////////////////////////////////////////////////////////////////////
// A set of no-op builtin "callouts"

static WantedCheckApi::DomainCheckResult noop_check_domain(const std::string &/*domain*/) {
	WantedCheckApi::DomainCheckResult result;
	result.wanted = true;
	return result;
}

static WantedCheckApi::UrlCheckResult noop_check_url(const std::string &/*url*/) {
	WantedCheckApi::UrlCheckResult result;
	result.wanted = true;
	return result;
}

static WantedCheckApi::ContentCheckResult noop_check_content(const std::vector<WantedCheckApi::Content> &/*content*/) {
	WantedCheckApi::ContentCheckResult result;
	result.result = result.wanted;
	return result;
}




//Handle the the loaded shlib
static void *p_shlib = 0;

//The effective descriptor (always contains non-null function pointers)
static WantedCheckApi::APIDescriptorBlock effective_descriptor_block = {
	noop_check_domain,
	noop_check_url,
	noop_check_content
};



bool WantedChecker::initialize() {
	log(LOG_INFO,"Initializing wanted-checking");
	p_shlib = dlopen(shlib_name, RTLD_NOW|RTLD_LOCAL);
	
	if(p_shlib==0) {
		log(LOG_WARN,"Initializing wanted-checking: '%s' could not be loaded (%s)", shlib_name, strerror(errno));
		return true;
	}
	
	const void *p_descriptor = dlsym(p_shlib,"wanted_check_api_descriptor_block");
	if(!p_descriptor) {
		log(LOG_WARN,"wanted-checkign: shlib does not contain the symbol 'wanted_check_api_descriptor_block'");
		dlclose(p_shlib);
		p_shlib = 0;
		return true;
	}
	
	const WantedCheckApi::APIDescriptorBlock *desc = reinterpret_cast<const WantedCheckApi::APIDescriptorBlock*>(p_descriptor);
	
	if(desc->check_domain_pfn)
		effective_descriptor_block.check_domain_pfn = desc->check_domain_pfn;
	if(desc->check_url_pfn)
		effective_descriptor_block.check_url_pfn = desc->check_url_pfn;
	if(desc->check_content_pfn)
		effective_descriptor_block.check_content_pfn = desc->check_content_pfn;
	
	log(LOG_INFO,"Initialized wanted-checking");
	return true;
}


void WantedChecker::finalize() {
	log(LOG_INFO,"Finalizing wanted-checking");
	
	effective_descriptor_block.check_domain_pfn = noop_check_domain;
	effective_descriptor_block.check_url_pfn = noop_check_url;
	effective_descriptor_block.check_content_pfn = noop_check_content;
	
	if(p_shlib) {
		dlclose(p_shlib);
		p_shlib = 0;
	}
	
	log(LOG_INFO,"Finalized wanted-checking");
}
