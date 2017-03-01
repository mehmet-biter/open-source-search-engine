#ifndef GB_URLBLOCKLIST_H
#define GB_URLBLOCKLIST_H

#include <vector>
#include "UrlBlock.h"

typedef std::vector<UrlBlock> urlblocklist_t;
typedef std::shared_ptr<urlblocklist_t> urlblocklist_ptr_t;
typedef std::shared_ptr<const urlblocklist_t> urlblocklistconst_ptr_t;

class Url;

class UrlBlockList {
public:
	UrlBlockList();

	bool init();

	bool isUrlBlocked(const Url &url);

	static void reload(int /*fd*/, void *state);

protected:
	bool load();

	const char *m_filename;

private:
	urlblocklistconst_ptr_t getUrlBlockList();
	void swapUrlBlockList(urlblocklistconst_ptr_t urlRegexList);

	urlblocklistconst_ptr_t m_urlBlockList;

	time_t m_lastModifiedTime;
};

extern UrlBlockList g_urlBlockList;

#endif //GB_URLBLOCKLIST_H
