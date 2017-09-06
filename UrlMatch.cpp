#include "UrlMatch.h"
#include "Url.h"
#include "hash.h"
#include "GbUtil.h"
#include "Log.h"
#include "Conf.h"
#include "UrlParser.h"
#include <algorithm>

urlmatchstr_t::urlmatchstr_t(urlmatchtype_t type, const std::string &str)
	: m_type(type)
	, m_str(str) {
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

urlmatchparam_t::urlmatchparam_t(const std::string &name, const std::string &value)
	: m_name(name)
	, m_value(value) {
}

urlmatchregex_t::urlmatchregex_t(const std::string &regexStr, const GbRegex &regex, const std::string &domain)
	: m_regex(regex)
	, m_regexStr(regexStr)
	, m_domain(domain) {
}

urlmatchtld_t::urlmatchtld_t(const std::string &tlds)
	: m_tldsStr(tlds)
	, m_tlds(split(tlds, ',')) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchstr_t> &urlmatchstr)
	: m_type(urlmatchstr->m_type)
	, m_str(urlmatchstr){
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchdomain_t> &urlmatchdomain)
	: m_type(url_match_domain)
	, m_domain(urlmatchdomain) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchhost_t> &urlmatchhost)
	: m_type(url_match_host)
	, m_host(urlmatchhost) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchparam_t> &urlmatchparam)
	: m_type(url_match_param)
	, m_param(urlmatchparam) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex)
	: m_type(url_match_regex)
	, m_regex(urlmatchregex) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchtld_t> &urlmatchtld)
	: m_type(url_match_tld)
	, m_tld(urlmatchtld) {
}

static bool matchString(const std::string &needle, const char *haystack, int32_t haystackLen, bool matchPrefix = false) {
	bool matchingLen = matchPrefix ? (needle.length() <= static_cast<size_t>(haystackLen)) : (needle.length() == static_cast<size_t>(haystackLen));
	return (matchingLen && memcmp(needle.c_str(), haystack, needle.length()) == 0);
}

bool UrlMatch::match(const Url &url) const {
	switch (m_type) {
		case url_match_domain:
			if (matchString(m_domain->m_domain, url.getDomain(), url.getDomainLen())) {
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
		case url_match_file:
			return matchString(m_str->m_str, url.getFilename(), url.getFilenameLen());
		case url_match_host:
			if (matchString(m_host->m_host, url.getHost(), url.getHostLen())) {
				if (m_host->m_path.empty()) {
					return true;
				}

				return matchString(m_host->m_path, url.getPath(), url.getPathLenWithCgi(), true);
			}
			break;
		case url_match_param:
			if (strncasestr(url.getQuery(), m_param->m_name.c_str(), url.getQueryLen()) != NULL) {
				// not the most efficient, but there is already parsing logic for query parameter in UrlParser
				UrlParser urlParser(url.getUrl(), url.getUrlLen(), TITLEREC_CURRENT_VERSION);
				auto queryMatches = urlParser.matchQueryParam(UrlComponent::Matcher(m_param->m_name.c_str()));
				if (m_param->m_value.empty()) {
					return (!queryMatches.empty());
				}

				for (auto &queryMatch : queryMatches) {
					if (matchString(m_param->m_value, queryMatch->getValue(), queryMatch->getValueLen())) {
						return true;
					}
				}
			}
			break;
		case url_match_path:
			return matchString(m_str->m_str, url.getPath(), url.getPathLenWithCgi(), true);
		case url_match_regex:
			if (m_regex->m_domain.empty() || (!m_regex->m_domain.empty() && matchString(m_regex->m_domain, url.getDomain(), url.getDomainLen()))) {
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
		case url_match_file:
			type = "file";
			value = m_str->m_str.c_str();
			break;
		case url_match_host:
			type = "host";
			value = m_host->m_host.c_str();
			break;
		case url_match_param:
			type = "param";
			value = m_param->m_name.c_str();
			break;
		case url_match_path:
			type = "path";
			value = m_str->m_str.c_str();
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
