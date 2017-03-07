#ifndef GB_URLBLOCK_H
#define GB_URLBLOCK_H

#include <string>
#include <string>
#include <memory>
#include <vector>
#include "GbRegex.h"

struct urlblockdomain_t {
	urlblockdomain_t(const std::string &domain, const std::string &allow);

	std::string m_domain;
	std::vector<std::string> m_allow;
};

struct urlblockhost_t {
	urlblockhost_t(const std::string &host, const std::string &path);

	std::string m_host;
	std::string m_path;
};

struct urlblockpath_t {
	urlblockpath_t(const std::string &path);

	std::string m_path;
};

struct urlblockregex_t {
	urlblockregex_t(const std::string &regexStr, const GbRegex &regex, const std::string &domain = "");

	GbRegex m_regex;
	std::string m_regexStr;
	std::string m_domain;
};

struct urlblocktld_t {
	urlblocktld_t(const std::string &tlds);

	std::string m_tldsStr;
	std::vector<std::string> m_tlds;
};

class Url;

class UrlBlock {
public:
	UrlBlock(const std::shared_ptr<urlblockdomain_t> &urlblockdomain);
	UrlBlock(const std::shared_ptr<urlblockhost_t> &urlblockhost);
	UrlBlock(const std::shared_ptr<urlblockpath_t> &urlblockpath);
	UrlBlock(const std::shared_ptr<urlblockregex_t> &urlblockregex);
	UrlBlock(const std::shared_ptr<urlblocktld_t> &urlblocktld);

	bool match(const Url &url) const;
	void logMatch(const Url &url) const;

private:
	enum urlblocktype_t {
		url_block_domain,
		url_block_host,
		url_block_path,
		url_block_regex,
		url_block_tld
	};

	urlblocktype_t m_type;

	std::shared_ptr<urlblockdomain_t> m_domain;
	std::shared_ptr<urlblockhost_t> m_host;
	std::shared_ptr<urlblockpath_t> m_path;
	std::shared_ptr<urlblockregex_t> m_regex;
	std::shared_ptr<urlblocktld_t> m_tld;
};

#endif //GB_URLBLOCK_H
