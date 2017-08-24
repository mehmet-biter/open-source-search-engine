#include "UrlBlockCheck.h"
#include "Url.h"
#include "UrlMatchList.h"
#include "WantedChecker.h"
#include "Conf.h"
#include "Log.h"


bool isUrlBlocked(const Url &url) {
	if(g_urlBlackList.isUrlMatched(url))
		return true;
	
	//TODO: check whitelist
	
	//now call the shlib functions for checking if the URL is wanted or not
	if(!WantedChecker::check_domain(std::string(url.getHost(),url.getHostLen())).wanted) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url block shlib matched (domain) url '%s'", url.getUrl());
		return true;
	}
	if(!WantedChecker::check_url(std::string(url.getUrl(),url.getUrlLen())).wanted) {
		logTrace(g_conf.m_logTraceUrlMatchList, "Url block shlib matched (full URL) url '%s'", url.getUrl());
		return true;
	}

	return false;
}
