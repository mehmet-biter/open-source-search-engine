#ifndef URLRESULTOVERRIDE_H
#define URLRESULTOVERRIDE_H


#include <string>
#include <map>
#include <memory>
#include <atomic>

class LanguageResultOverride;

typedef std::map<std::string, LanguageResultOverride> urlresultoverridemap_t;
typedef std::shared_ptr<urlresultoverridemap_t> urlresultoverridemap_ptr_t;
typedef std::shared_ptr<const urlresultoverridemap_t> urlresultoverridemapconst_ptr_t;

class Url;

class UrlResultOverride {
public:
	UrlResultOverride();

	bool init();

	static void reload(int /*fd*/, void *state);
	static void reload(void *state);

	std::string getTitle(const std::string &lang, const Url &url);
	std::string getSummary(const std::string &lang, const Url &url);

protected:
	bool load();

	const char *m_filename;

private:
	void swapUrlResultOverride(urlresultoverridemapconst_ptr_t resultOverrideMap);
	urlresultoverridemapconst_ptr_t getUrlResultOverrideMap();

	std::atomic_bool m_loading;
	urlresultoverridemapconst_ptr_t m_urlResultOverrideMap;
	time_t m_lastModifiedTime;
};

extern UrlResultOverride g_urlResultOverride;


#endif //URLRESULTOVERRIDE_H
