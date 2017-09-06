#ifndef GB_URLMATCH_H_
#define GB_URLMATCH_H_

#include <string>
#include <memory>
#include <vector>
#include "GbRegex.h"

struct urlmatchdomain_t {
	enum pathcriteria_t {
		pathcriteria_allow_all,
		pathcriteria_allow_index_only,
		pathcriteria_allow_rootpages_only
	};

	urlmatchdomain_t(const std::string &domain, const std::string &allow, pathcriteria_t pathcriteria);

	std::string m_domain;
	std::vector<std::string> m_allow;
	pathcriteria_t m_pathcriteria;
};

struct urlmatchfile_t {
	urlmatchfile_t(const std::string &file);

	std::string m_file;
};

struct urlmatchhost_t {
	urlmatchhost_t(const std::string &host, const std::string &path);

	std::string m_host;
	std::string m_path;
};

struct urlmatchpath_t {
	urlmatchpath_t(const std::string &path);

	std::string m_path;
};

struct urlmatchregex_t {
	urlmatchregex_t(const std::string &regexStr, const GbRegex &regex, const std::string &domain = "");

	GbRegex m_regex;
	std::string m_regexStr;
	std::string m_domain;
};

struct urlmatchtld_t {
	urlmatchtld_t(const std::string &tlds);

	std::string m_tldsStr;
	std::vector<std::string> m_tlds;
};

class Url;

class UrlMatch {
public:
	UrlMatch(const std::shared_ptr<urlmatchdomain_t> &urlmatchdomain);
	UrlMatch(const std::shared_ptr<urlmatchfile_t> &urlmatchfile);
	UrlMatch(const std::shared_ptr<urlmatchhost_t> &urlmatchhost);
	UrlMatch(const std::shared_ptr<urlmatchpath_t> &urlmatchpath);
	UrlMatch(const std::shared_ptr<urlmatchregex_t> &urlmatchregex);
	UrlMatch(const std::shared_ptr<urlmatchtld_t> &urlmatchtld);

	bool match(const Url &url) const;
	void logMatch(const Url &url) const;

private:
	enum urlmatchtype_t {
		url_match_domain,
		url_match_file,
		url_match_host,
		url_match_path,
		url_match_regex,
		url_match_tld
	};

	urlmatchtype_t m_type;

	std::shared_ptr<urlmatchdomain_t> m_domain;
	std::shared_ptr<urlmatchfile_t> m_file;
	std::shared_ptr<urlmatchhost_t> m_host;
	std::shared_ptr<urlmatchpath_t> m_path;
	std::shared_ptr<urlmatchregex_t> m_regex;
	std::shared_ptr<urlmatchtld_t> m_tld;
};

#endif //GB_URLMATCH_H_
