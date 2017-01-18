#ifndef GB_URLBLOCKLIST_H
#define GB_URLBLOCKLIST_H

#include <vector>
#include <regex>
#include "GbMutex.h"

typedef std::vector<std::pair<std::string, std::regex>> regexlist_t;
typedef std::shared_ptr<regexlist_t> regexlist_ptr_t;
typedef std::shared_ptr<const regexlist_t> regexlistconst_ptr_t;

class UrlBlockList {
public:
	UrlBlockList();

	bool init();

	bool isUrlBlocked(const char *url);

	static void reload(int /*fd*/, void *state);

protected:
	bool load();

	const char *m_filename;

private:
	regexlistconst_ptr_t getUrlRegexList();
	void swapUrlRegexList(regexlistconst_ptr_t urlRegexList);

	regexlistconst_ptr_t m_urlRegexList;
	GbMutex m_urlRegexListMtx;

	time_t m_lastModifiedTime;
};

extern UrlBlockList g_urlBlockList;

#endif //GB_URLBLOCKLIST_H
