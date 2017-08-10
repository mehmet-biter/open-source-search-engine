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

} //namespace

#endif
