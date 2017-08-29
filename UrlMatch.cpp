#include "UrlMatch.h"
#include "Url.h"
#include "hash.h"
#include "GbUtil.h"
#include "Log.h"
#include "Conf.h"
#include <algorithm>

urlmatchtld_t::urlmatchtld_t(const std::string &tlds)
	: m_tldsStr(tlds)
	, m_tlds(split(tlds, ',')) {
}

urlmatchdomain_t::urlmatchdomain_t(const std::string &domain, const std::string &allow, pathcriteria_t pathcriteria)
	: m_domain(domain)
	, m_allow(split(allow, ','))
	, m_pathcriteria(pathcriteria) {
}

urlmatchhost_t::urlmatchhost_t(const std::string &host, const std::string &path)
	: m_host(host)
	, m_path(path) {
}

urlmatchpath_t::urlmatchpath_t(const std::string &path)
	: m_path(path) {
}

urlmatchregex_t::urlmatchregex_t(const std::string &regexStr, const GbRegex &regex, const std::string &domain)
	: m_regex(regex)
	, m_regexStr(regexStr)
	, m_domain(domain) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchtld_t> &urlmatchtld)
	: m_type(url_match_tld)
	, m_tld(urlmatchtld) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchdomain_t> &urlmatchdomain)
	: m_type(url_match_domain)
	, m_domain(urlmatchdomain) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchhost_t> &urlmatchhost)
	: m_type(url_match_host)
	, m_host(urlmatchhost) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchpath_t> &urlmatchpath)
	: m_type(url_match_path)
	, m_path(urlmatchpath) {

}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex)
	: m_type(url_match_regex)
	, m_regex(urlmatchregex) {
}

bool UrlMatch::match(const Url &url) const {
	switch (m_type) {
		case url_match_domain:
			if (m_domain->m_domain.length() == static_cast<size_t>(url.getDomainLen()) &&
			   memcmp(m_domain->m_domain.c_str(), url.getDomain(), url.getDomainLen()) == 0) {
				// check subdomain
				if (!m_domain->m_allow.empty()) {
					auto subDomainLen = (url.getDomain() == url.getHost()) ? 0 : url.getDomain() - url.getHost() - 1;
					std::string subDomain(url.getHost(), subDomainLen);
					bool match = (std::find(m_domain->m_allow.cbegin(), m_domain->m_allow.cend(), subDomain) == m_domain->m_allow.cend());
					if (!match) {
						// check for pathcriteria
						switch (m_domain->m_pathcriteria) {
							case urlmatchdomain_t::pathcriteria_allow_all:
								return false;
							case urlmatchdomain_t::pathcriteria_allow_index_only:
								return (url.getPathLen() > 1);
							case urlmatchdomain_t::pathcriteria_allow_rootpages_only:
								return (url.getPathDepth(false) > 0);
						}
					}
				}

				return true;
			}
			break;
		case url_match_host:
			if (m_host->m_host.length() == static_cast<size_t>(url.getHostLen()) &&
			    memcmp(m_host->m_host.c_str(), url.getHost(), url.getHostLen()) == 0) {
				if (m_host->m_path.empty()) {
					return true;
				}

				return (m_host->m_path.length() <= static_cast<size_t>(url.getPathLenWithCgi()) &&
				        memcmp(m_host->m_path.c_str(), url.getPath(), m_host->m_path.length()) == 0);
			}
			break;
		case url_match_path:
			return (m_path->m_path.length() <= static_cast<size_t>(url.getPathLenWithCgi()) &&
			        memcmp(m_path->m_path.c_str(), url.getPath(), m_path->m_path.length()) == 0);
		case url_match_regex:
			if (m_regex->m_domain.empty() || (!m_regex->m_domain.empty() &&
				m_regex->m_domain.length() == static_cast<size_t>(url.getDomainLen()) &&
				memcmp(m_regex->m_domain.c_str(), url.getDomain(), url.getDomainLen()) == 0)) {
				return m_regex->m_regex.match(url.getUrl());
			}
			break;
		case url_match_tld:
			if (!m_tld->m_tlds.empty()) {
				const char *tld = url.getTLD();
				size_t tldLen = static_cast<size_t>(url.getTLDLen());
				const char *dotPos = static_cast<const char *>(memchr(tld, '.', tldLen));
				if (dotPos) {
					tldLen -= (dotPos - tld + 1);
					tld = dotPos + 1;
				}

				return (std::find(m_tld->m_tlds.cbegin(), m_tld->m_tlds.cend(), std::string(tld, tldLen)) != m_tld->m_tlds.cend());
			}
			break;
	}

	return false;
}

void UrlMatch::logMatch(const Url &url) const {
	const char *type = NULL;
	const char *value = NULL;

	switch (m_type) {
		case url_match_domain:
			type = "domain";
			value = m_domain->m_domain.c_str();
			break;
		case url_match_host:
			type = "host";
			value = m_host->m_host.c_str();
			break;
		case url_match_path:
			type = "path";
			value = m_path->m_path.c_str();
			break;
		case url_match_regex:
			type = "regex";
			value = m_regex->m_regexStr.c_str();
			break;
		case url_match_tld:
			type = "tld";
			value = m_tld->m_tldsStr.c_str();
	}

	logTrace(g_conf.m_logTraceUrlMatchList, "Url match criteria %s='%s' matched url '%s'", type, value, url.getUrl());
}
