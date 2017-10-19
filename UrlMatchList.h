#ifndef GB_URLMATCHLIST_H_
#define GB_URLMATCHLIST_H_

#include "UrlMatch.h"
#include <map>
#include <memory>

struct UrlMatchListItem;

typedef std::shared_ptr<UrlMatchListItem> urlmatchlistitem_ptr_t;
typedef std::shared_ptr<const UrlMatchListItem> urlmatchlistitemconst_ptr_t;

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
	urlmatchlistitemconst_ptr_t getUrlMatchList();
	void swapUrlMatchList(urlmatchlistitemconst_ptr_t urlMatchList);

	std::string m_filename;
	std::string m_dirname;
	urlmatchlistitemconst_ptr_t m_urlMatchList;

	std::map<std::string, time_t> m_lastModifiedTimes;
};

extern UrlMatchList g_urlBlackList;
extern UrlMatchList g_urlWhiteList;

#endif //GB_URLMATCHLIST_H_
