#ifndef URLMATCHHOSTLIST_H
#define URLMATCHHOSTLIST_H

#include <memory>
#include "third-party/sparsepp/sparsepp/spp.h"

typedef spp::sparse_hash_set<std::string> urlmatchhostlist_t;
typedef std::shared_ptr<urlmatchhostlist_t> urlmatchhostlist_ptr_t;
typedef std::shared_ptr<const urlmatchhostlist_t> urlmatchhostlistconst_ptr_t;

class Url;

class UrlMatchHostList {
public:
	UrlMatchHostList();

	bool load(const char *filename, bool matchHost);
	void unload();

	bool isUrlMatched(const Url &url);

private:
	urlmatchhostlistconst_ptr_t getUrlMatchHostList();
	void swapUrlMatchHostList(urlmatchhostlistconst_ptr_t urlMatchHostList);

	const char *m_filename;
	bool m_matchHost;

	urlmatchhostlistconst_ptr_t m_urlmatchhostlist;
};

extern UrlMatchHostList g_urlHostBlackList;

#endif //URLMATCHHOSTLIST_H
