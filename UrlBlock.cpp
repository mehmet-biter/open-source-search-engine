#include "UrlBlock.h"
#include "Url.h"
#include "hash.h"
#include "GbUtil.h"
#include "Log.h"
#include "Conf.h"
#include <algorithm>

urlblocktld_t::urlblocktld_t(const std::string &tlds)
	: m_tldsStr(tlds)
	, m_tlds(split(tlds, ',')) {
}

urlblockdomain_t::urlblockdomain_t(const std::string &domain, const std::string &allow)
	: m_domain(domain)
	, m_allow(split(allow, ',')) {
}

urlblockhost_t::urlblockhost_t(const std::string &host, const std::string &path)
	: m_host(host)
	, m_path(path) {
}

urlblockpath_t::urlblockpath_t(const std::string &path)
	: m_path(path) {
}

urlblockregex_t::urlblockregex_t(const std::string &regexStr, const GbRegex &regex, const std::string &domain)
	: m_regex(regex)
	, m_regexStr(regexStr)
	, m_domain(domain) {
}

UrlBlock::UrlBlock(const std::shared_ptr<urlblocktld_t> &urlblocktld)
	: m_type(url_block_tld)
	, m_tld(urlblocktld) {
}

UrlBlock::UrlBlock(const std::shared_ptr<urlblockdomain_t> &urlblockdomain)
	: m_type(url_block_domain)
	, m_domain(urlblockdomain) {
}

UrlBlock::UrlBlock(const std::shared_ptr<urlblockhost_t> &urlblockhost)
	: m_type(url_block_host)
	, m_host(urlblockhost) {
}

UrlBlock::UrlBlock(const std::shared_ptr<urlblockpath_t> &urlblockpath)
	: m_type(url_block_path)
	, m_path(urlblockpath) {

}

UrlBlock::UrlBlock(const std::shared_ptr<urlblockregex_t> &urlblockregex)
	: m_type(url_block_regex)
	, m_regex(urlblockregex) {
}

bool UrlBlock::match(const Url &url) const {
	switch (m_type) {
		case url_block_domain:
			if (m_domain->m_domain.length() == static_cast<size_t>(url.getDomainLen()) &&
			   memcmp(m_domain->m_domain.c_str(), url.getDomain(), url.getDomainLen()) == 0) {
				// check subdomain
				if (!m_domain->m_allow.empty()) {
					auto subDomainLen = (url.getDomain() == url.getHost()) ? 0 : url.getDomain() - url.getHost() - 1;
					std::string subDomain(url.getHost(), subDomainLen);
					return (std::find(m_domain->m_allow.cbegin(), m_domain->m_allow.cend(), subDomain) == m_domain->m_allow.cend());
				}

				return true;
			}
			break;
		case url_block_host:
			if (m_host->m_host.length() == static_cast<size_t>(url.getHostLen()) &&
			    memcmp(m_host->m_host.c_str(), url.getHost(), url.getHostLen()) == 0) {
				if (m_host->m_path.empty()) {
					return true;
				}

				return (m_host->m_path.length() <= static_cast<size_t>(url.getPathLenWithCgi()) &&
				        memcmp(m_host->m_path.c_str(), url.getPath(), m_host->m_path.length()) == 0);
			}
			break;
		case url_block_path:
			return (m_path->m_path.length() <= static_cast<size_t>(url.getPathLenWithCgi()) &&
			        memcmp(m_path->m_path.c_str(), url.getPath(), m_path->m_path.length()) == 0);
		case url_block_regex:
			if (m_regex->m_domain.empty() || (!m_regex->m_domain.empty() &&
				m_regex->m_domain.length() == static_cast<size_t>(url.getDomainLen()) &&
				memcmp(m_regex->m_domain.c_str(), url.getDomain(), url.getDomainLen()) == 0)) {
				return m_regex->m_regex.match(url.getUrl());
			}
			break;
		case url_block_tld:
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

void UrlBlock::logMatch(const Url &url) const {
	const char *type = NULL;
	const char *value = NULL;

	switch (m_type) {
		case url_block_domain:
			type = "domain";
			value = m_domain->m_domain.c_str();
			break;
		case url_block_host:
			type = "host";
			value = m_host->m_host.c_str();
			break;
		case url_block_path:
			type = "path";
			value = m_path->m_path.c_str();
			break;
		case url_block_regex:
			type = "regex";
			value = m_regex->m_regexStr.c_str();
			break;
		case url_block_tld:
			type = "tld";
			value = m_tld->m_tldsStr.c_str();
	}

	logTrace(g_conf.m_logTraceUrlBlockList, "Url block criteria %s='%s' matched url '%s'", type, value, url.getUrl());
}