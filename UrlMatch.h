#ifndef GB_URLMATCH_H_
#define GB_URLMATCH_H_

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include "GbRegex.h"
#include "UrlParser.h"

enum urlmatchtype_t {
	url_match_extension,
	url_match_domain,
	url_match_file,
	url_match_host,
	url_match_middomain,
	url_match_path,
	url_match_pathcriteria,
	url_match_pathparam,
	url_match_pathpartial,
	url_match_port,
	url_match_queryparam,
	url_match_regex,
	url_match_scheme,
	url_match_subdomain,
	url_match_tld
};

struct urlmatchstr_t {
	enum matchcriteria_t {
		matchcriteria_exact,
		matchcriteria_prefix,
		matchcriteria_suffix,
		matchcriteria_partial
	};

	urlmatchstr_t(urlmatchtype_t type, const std::string &str, const std::string &match_criteria);
	urlmatchstr_t(urlmatchtype_t type, const std::string &str, matchcriteria_t matchcriteria);

	urlmatchtype_t m_type;
	std::string m_str;
	matchcriteria_t m_matchcriteria;
};

struct urlmatchset_t {
	urlmatchset_t(urlmatchtype_t type, const std::string &str);

	urlmatchtype_t m_type;
	std::string m_str;
	std::unordered_set<std::string> m_set;
};

struct urlmatchparam_t {
	urlmatchparam_t(urlmatchtype_t type, const std::string &name, const std::string &value);

	urlmatchtype_t m_type;
	std::string m_name;
	std::string m_value;
};

struct urlmatchpathcriteria_t {
	enum pathcriteria_t {
		pathcriteria_all,
		pathcriteria_index_only,
		pathcriteria_rootpages_only
	};

	urlmatchpathcriteria_t(const std::string &path_criteria);

	std::string m_str;
	pathcriteria_t m_pathcriteria;
};
struct urlmatchregex_t {
	urlmatchregex_t(const std::string &regexStr, const GbRegex &regex);

	GbRegex m_regex;
	std::string m_regexStr;
};

class Url;

class UrlMatch {
public:
	UrlMatch(const std::shared_ptr<urlmatchstr_t> &urlmatchstr, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchset_t> &urlmatchset, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchparam_t> &urlmatchparam, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchpathcriteria_t> &urlmatchpathcriteria, bool m_invert);
	UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex, bool m_invert);

	urlmatchtype_t getType() const;
	std::string getDomain() const;

	bool match(const Url &url, const UrlParser &urlParser) const;
	void logMatch(const Url &url) const;
	void logCriteria(const Url &url) const;

private:
	void log(const Url &url, const char **type, const char **value) const;

	bool m_invert;

	urlmatchtype_t m_type;

	std::shared_ptr<urlmatchstr_t> m_str;
	std::shared_ptr<urlmatchset_t> m_set;

	std::shared_ptr<urlmatchparam_t> m_param;
	std::shared_ptr<urlmatchpathcriteria_t> m_pathcriteria;
	std::shared_ptr<urlmatchregex_t> m_regex;
};

#endif //GB_URLMATCH_H_
