#include "UrlMatch.h"
#include "Url.h"
#include "hash.h"
#include "GbUtil.h"
#include "Log.h"
#include "Conf.h"
#include "UrlParser.h"
#include <algorithm>

urlmatchstr_t::urlmatchstr_t(urlmatchtype_t type, const std::string &str, const std::string &match_criteria)
	: m_type(type)
	, m_str(str)
	, m_matchcriteria(matchcriteria_exact) {
	if (match_criteria.compare("matchprefix") == 0) {
		m_matchcriteria = matchcriteria_prefix;
	} else if (match_criteria.compare("matchsuffix") == 0) {
		m_matchcriteria = matchcriteria_suffix;
	} else if (match_criteria.compare("matchpartial") == 0) {
		m_matchcriteria = matchcriteria_partial;
	}
}

urlmatchstr_t::urlmatchstr_t(urlmatchtype_t type, const std::string &str, matchcriteria_t matchcriteria)
	: m_type(type)
	  , m_str(str)
	  , m_matchcriteria(matchcriteria) {
}

urlmatchset_t::urlmatchset_t(urlmatchtype_t type, const std::string &str)
	: m_type(type)
	, m_str(str)
	, m_set() {
	auto values = split(m_str, ',');
	m_set.insert(values.begin(), values.end());
}

urlmatchparam_t::urlmatchparam_t(urlmatchtype_t type, const std::string &name, const std::string &value)
	: m_type(type)
	, m_name(name)
	, m_value(value) {
}

urlmatchpathcriteria_t::urlmatchpathcriteria_t(const std::string &str)
	: m_str(str)
	, m_pathcriteria(pathcriteria_all) {
	if (str.compare("indexpage") == 0) {
		m_pathcriteria = pathcriteria_index_only;
	} else if (str.compare("rootpages") == 0) {
		m_pathcriteria = pathcriteria_rootpages_only;
	}
}

urlmatchregex_t::urlmatchregex_t(const std::string &regexStr, const GbRegex &regex)
	: m_regex(regex)
	, m_regexStr(regexStr) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchstr_t> &urlmatchstr, bool invert)
	: m_invert(invert)
	, m_type(urlmatchstr->m_type)
	, m_str(urlmatchstr) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchset_t> &urlmatchset, bool invert)
	: m_invert(invert)
	, m_type(urlmatchset->m_type)
	, m_set(urlmatchset) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchparam_t> &urlmatchparam, bool invert)
	: m_invert(invert)
	, m_type(urlmatchparam->m_type)
	, m_param(urlmatchparam) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchpathcriteria_t> &urlmatchpathcriteria, bool invert)
	: m_invert(invert)
	, m_type(url_match_pathcriteria)
	, m_pathcriteria(urlmatchpathcriteria) {
}

UrlMatch::UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex, bool invert)
	: m_invert(invert)
	, m_type(url_match_regex)
	, m_regex(urlmatchregex) {
}

urlmatchtype_t UrlMatch::getType() const {
	return m_type;
}

std::string UrlMatch::getDomain() const {
	if (m_str->m_matchcriteria == urlmatchstr_t::matchcriteria_exact || m_str->m_matchcriteria == urlmatchstr_t::matchcriteria_suffix) {
		switch (m_type) {
			case url_match_domain:
				return m_str->m_str;
			case url_match_host: {
				Url url;
				url.set(m_str->m_str.c_str());
				return std::string(url.getDomain(), url.getDomainLen());
			}
			default:
				// do nothing
				break;
		}
	}

	return "";
}

static bool matchStringExact(const std::string &needle, const char *haystack, int32_t haystackLen) {
	return ((needle.length() == static_cast<size_t>(haystackLen)) && memcmp(needle.c_str(), haystack, needle.length()) == 0);
}

static bool matchStringPrefix(const std::string &needle, const char *haystack, int32_t haystackLen) {
	return ((needle.length() <= static_cast<size_t>(haystackLen)) && memcmp(needle.c_str(), haystack, needle.length()) == 0);
}

static bool matchStringSuffix(const std::string &needle, const char *haystack, int32_t haystackLen, char segmentSeparator) {
	if ((needle.length() <= static_cast<size_t>(haystackLen)) && memcmp(needle.c_str(), haystack + haystackLen - needle.length(), needle.length()) == 0) {
		if (segmentSeparator == '\0') {
			return true;
		} else {
			// we need full segment match
			if ((needle.length() == static_cast<size_t>(haystackLen)) ||
			    // haystack starts segment separator
			    (needle[0] == segmentSeparator) ||
			    // haystack doesn't start with a dot, but we always want a full segment match
			    (haystack[haystackLen - needle.length() - 1] == segmentSeparator)) {
				return true;
			}
		}
	}

	return false;
}

static bool matchStringPartial(const std::string &needle, const char *haystack, int32_t haystackLen) {
	return (strncasestr(haystack, needle.c_str(), haystackLen) != nullptr);
}

static bool matchString(std::shared_ptr<urlmatchstr_t> matchstr, const char *haystack, int32_t haystackLen, char segmentSeparator = '\0') {
	switch (matchstr->m_matchcriteria) {
		case urlmatchstr_t::matchcriteria_exact:
			return matchStringExact(matchstr->m_str, haystack, haystackLen);
		case urlmatchstr_t::matchcriteria_prefix:
			return matchStringPrefix(matchstr->m_str, haystack, haystackLen);
		case urlmatchstr_t::matchcriteria_suffix:
			return matchStringSuffix(matchstr->m_str, haystack, haystackLen, segmentSeparator);
		case urlmatchstr_t::matchcriteria_partial:
			return matchStringPartial(matchstr->m_str, haystack, haystackLen);
	}

	return false;
}

bool UrlMatch::match(const Url &url, const UrlParser &urlParser) const {
	logCriteria(url);

	switch (m_type) {
		case url_match_domain:
			return m_invert ^ matchString(m_str, url.getDomain(), url.getDomainLen(), '.');
		case url_match_extension:
			return m_invert ^ matchString(m_str, url.getExtension(), url.getExtensionLen());
		case url_match_file:
			return m_invert ^ matchString(m_str, url.getFilename(), url.getFilenameLen());
		case url_match_host:
			return m_invert ^ matchString(m_str, url.getHost(), url.getHostLen(), '.');
		case url_match_middomain:
			return m_invert ^ matchString(m_str, url.getMidDomain(), url.getMidDomainLen(), '.');
		case url_match_queryparam:
			if (strncasestr(url.getQuery(), m_param->m_name.c_str(), url.getQueryLen()) != nullptr) {
				// not the most efficient, but there is already parsing logic for query parameter in UrlParser
				auto queryMatches = urlParser.matchQueryParam(UrlComponent::Matcher(m_param->m_name.c_str()));
				if (m_param->m_value.empty()) {
					return m_invert ^ !queryMatches.empty();
				}

				for (auto &queryMatch : queryMatches) {
					if (matchStringExact(m_param->m_value, queryMatch->getValue(), queryMatch->getValueLen())) {
						return m_invert ^ true;
					}
				}
			}
			break;
		case url_match_path:
			return m_invert ^ matchString(m_str, url.getPath(), url.getPathLenWithCgi(), '/');
		case url_match_pathcriteria:
			// check for pathcriteria
			switch (m_pathcriteria->m_pathcriteria) {
				case urlmatchpathcriteria_t::pathcriteria_all:
					return m_invert ^ false;
				case urlmatchpathcriteria_t::pathcriteria_index_only:
					return m_invert ^ (url.getPathLen() <= 1);
				case urlmatchpathcriteria_t::pathcriteria_rootpages_only:
					return m_invert ^ (url.getPathDepth(false) == 0);
			}
			break;
		case url_match_pathparam:
			if (strncasestr(url.getPath(), m_param->m_name.c_str(), url.getPathLen()) != nullptr) {
				// not the most efficient, but there is already parsing logic for path parameter in UrlParser
				auto pathParamMatches = urlParser.matchPathParam(UrlComponent::Matcher(m_param->m_name.c_str()));
				if (m_param->m_value.empty()) {
					return m_invert ^ (!pathParamMatches.empty());
				}

				for (auto &pathParamMatch : pathParamMatches) {
					if (matchStringExact(m_param->m_value, pathParamMatch->getValue(), pathParamMatch->getValueLen())) {
						return m_invert ^ true;
					}
				}
			}
			break;
		case url_match_pathpartial:
			return m_invert ^ matchStringPartial(m_str->m_str, url.getPath(), url.getPathLen());
		case url_match_port: {
			std::string port = std::to_string(url.getPort());
			return m_invert ^ matchStringExact(m_str->m_str, port.c_str(), port.length());
		}
		case url_match_regex:
			return m_invert ^ m_regex->m_regex.match(url.getUrl());
		case url_match_scheme:
			return m_invert ^ matchStringExact(m_str->m_str, url.getScheme(), url.getSchemeLen());
		case url_match_subdomain: {
			auto subDomainLen = (url.getDomain() == url.getHost()) ? 0 : url.getDomain() - url.getHost() - 1;
			std::string subDomain(url.getHost(), subDomainLen);
			return m_invert ^ (m_set->m_set.count(subDomain) > 0);
		}
		case url_match_tld: {
			std::string domain(url.getDomain(), url.getDomainLen());
			auto pos = domain.find_last_of('.');
			if (pos != std::string::npos) {
				std::string tld = domain.substr(pos + 1);
				return m_invert ^ (m_set->m_set.count(tld) > 0);
			}
		} break;
	}

	return m_invert ^ false;
}

void UrlMatch::log(const Url &url, const char **type, const char **value) const {
	switch (m_type) {
		case url_match_domain:
			*type = "domain";
			*value = m_str->m_str.c_str();
			break;
		case url_match_extension:
			*type = "extension";
			*value = m_str->m_str.c_str();
			break;
		case url_match_file:
			*type = "file";
			*value = m_str->m_str.c_str();
			break;
		case url_match_host:
			*type = "host";
			*value = m_str->m_str.c_str();
			break;
		case url_match_middomain:
			*type = "middomain";
			*value = m_str->m_str.c_str();
			break;
		case url_match_queryparam:
			*type = "param";
			*value = m_param->m_name.c_str();
			break;
		case url_match_path:
			*type = "path";
			*value = m_str->m_str.c_str();
			break;
		case url_match_pathcriteria:
			*type = "pathcriteria";
			*value = m_pathcriteria->m_str.c_str();
			break;
		case url_match_pathparam:
			*type = "pathparam";
			*value = m_param->m_name.c_str();
			break;
		case url_match_pathpartial:
			*type = "pathpartial";
			*value = m_str->m_str.c_str();
			break;
		case url_match_port:
			*type = "port";
			*value = m_str->m_str.c_str();
			break;
		case url_match_regex:
			*type = "regex";
			*value = m_regex->m_regexStr.c_str();
			break;
		case url_match_tld:
			*type = "tld";
			*value = m_set->m_str.c_str();
			break;
		case url_match_scheme:
			*type = "scheme";
			*value = m_str->m_str.c_str();
			break;
		case url_match_subdomain:
			*type = "subdomain";
			*value = m_set->m_str.c_str();
			break;
	}
}

void UrlMatch::logCriteria(const Url &url) const {
	if (!g_conf.m_logTraceUrlMatchList) {
		return;
	}

	const char *type = nullptr;
	const char *value = nullptr;

	log(url, &type, &value);
	logTrace(g_conf.m_logTraceUrlMatchList, "Matching url '%s' with criteria %s='%s' invert=%s",
	         url.getUrl(), type, value, m_invert ? "true" : "false");
}

void UrlMatch::logMatch(const Url &url) const {
	if (!g_conf.m_logTraceUrlMatchList) {
		return;
	}

	const char *type = nullptr;
	const char *value = nullptr;

	log(url, &type, &value);
	logTrace(g_conf.m_logTraceUrlMatchList, "Url match criteria %s='%s' matched url '%s' invert=%s",
	         type, value, url.getUrl(), m_invert ? "true" : "false");
}


