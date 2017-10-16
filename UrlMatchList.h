#ifndef GB_URLMATCHLIST_H_
#define GB_URLMATCHLIST_H_

#include "UrlMatch.h"
#include <vector>
#include <map>
#include <memory>

typedef std::vector<UrlMatch> urlmatchlist_t;
typedef std::shared_ptr<urlmatchlist_t> urlmatchlist_ptr_t;
typedef std::shared_ptr<const urlmatchlist_t> urlmatchlistconst_ptr_t;

class Url;

class UrlMatchList {
public:
	UrlMatchList(const char *filename);

	bool init();

	bool isUrlMatched(const Url &url);

	static void reload(int /*fd*/, void *state);

protected:
	bool load();

private:
	urlmatchlistconst_ptr_t getUrlMatchList();
	void swapUrlMatchList(urlmatchlistconst_ptr_t urlMatchList);

	std::string m_filename;
	std::string m_dirname;
	urlmatchlistconst_ptr_t m_urlMatchList;

	std::map<std::string, time_t> m_lastModifiedTimes;
};

extern UrlMatchList g_urlBlackList;
extern UrlMatchList g_urlWhiteList;

#endif //GB_URLMATCHLIST_H_
