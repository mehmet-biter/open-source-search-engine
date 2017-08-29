#include "UrlBlockCheck.h"
#include "Url.h"
#include "UrlMatchList.h"
#include "WantedChecker.h"
#include "Conf.h"
#include "Log.h"
#include "Statistics.h"


bool isUrlBlocked(const Url &url, int *p_errno) {
	Statistics::increment_url_block_counter_call();
	
	if(g_urlBlackList.isUrlMatched(url)) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url is blacklisted: %s", url.getUrl());
		Statistics::increment_url_block_counter_blacklisted();
		if(p_errno)
			*p_errno = EDOCBLOCKEDURL;
		return true;
	}
	
	if(g_urlWhiteList.isUrlMatched(url)) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url is whitelisted: %s", url.getUrl());
		Statistics::increment_url_block_counter_whitelisted();
		return false;
	}
	
	//now call the shlib functions for checking if the URL is wanted or not
	if(!WantedChecker::check_domain(std::string(url.getHost(),url.getHostLen())).wanted) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url block shlib matched (domain) url '%s'", url.getUrl());
		Statistics::increment_url_block_counter_shlib_domain_block();
		if(p_errno)
			*p_errno = EDOCBLOCKEDSHLIBDOMAIN;
		return true;
	}
	if(!WantedChecker::check_url(std::string(url.getUrl(),url.getUrlLen())).wanted) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url block shlib matched (full URL) url '%s'", url.getUrl());
		Statistics::increment_url_block_counter_shlib_url_block();
		if(p_errno)
			*p_errno = EDOCBLOCKEDSHLIBURL;
		return true;
	}
	
	Statistics::increment_url_block_counter_default();
	return false;
}
