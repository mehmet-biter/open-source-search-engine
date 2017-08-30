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

bool isUrlUnwanted(const Url &url, const char **reason) {
	//
	// Check if url is on our blocklist
	//
	int errorCode = 0;
	if (isUrlBlocked(url, &errorCode)) {
		if (reason) {
			*reason = "blocked unknown";

			switch (errorCode) {
				case EDOCBLOCKEDURL:
					*reason = "blocked list";
					break;
				case EDOCBLOCKEDSHLIBDOMAIN:
					*reason = "blocked domain";
					break;
				case EDOCBLOCKEDSHLIBURL:
					*reason = "blocked url";
					break;
			}
		}
		return true;
	}

	//
	// Check if url is different after stripping common tracking/session parameters
	//
	Url url_stripped;
	url_stripped.set(url.getUrl(), url.getUrlLen(), false, true);

	if (strcmp(url.getUrl(), url_stripped.getUrl()) != 0) {
		if (reason) {
			*reason = "unwanted params";
		}
		return true;
	}

	return false;
}
