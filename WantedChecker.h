#ifndef WANTEDCHECKER_H_
#define WANTEDCHECKER_H_
#include "WantedCheckerApi.h"


namespace WantedChecker {

bool initialize();
void finalize();

typedef WantedCheckApi::DomainCheckResult DomainCheckResult;
DomainCheckResult check_domain(const std::string &domain);

typedef WantedCheckApi::UrlCheckResult UrlCheckResult;
UrlCheckResult check_url(const std::string &url);

typedef WantedCheckApi::SingleContentCheckResult SingleContentCheckResult;
SingleContentCheckResult check_single_content(const std::string &url, const void *content, size_t content_len);

	
} //namespace

#endif
