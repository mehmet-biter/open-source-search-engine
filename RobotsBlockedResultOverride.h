#ifndef ROBOTSBLOCKEDRESULTOVERRIDE_H
#define ROBOTSBLOCKEDRESULTOVERRIDE_H

#include <string>
#include <map>
#include <memory>

struct ResultOverride;

typedef std::map<std::string, ResultOverride> resultoverridemap_t;
typedef std::shared_ptr<resultoverridemap_t> resultoverridemap_ptr_t;
typedef std::shared_ptr<const resultoverridemap_t> resultoverridemapconst_ptr_t;

class Url;

class RobotsBlockedResultOverride {
public:
	RobotsBlockedResultOverride();

	bool init();

	static void reload(int /*fd*/, void *state);

	std::string getTitle(const std::string &lang, const Url &url);
	std::string getSummary(const std::string &lang, const Url &url);

protected:
	bool load();

	const char *m_filename;

private:
	void swapResultOverride(resultoverridemapconst_ptr_t resultOverrideMap);
	resultoverridemapconst_ptr_t getResultOverrideMap();

	resultoverridemapconst_ptr_t m_resultOverrideMap;
	time_t m_lastModifiedTime;
};

extern RobotsBlockedResultOverride g_robotsBlockedResultOverride;


#endif //ROBOTSBLOCKEDRESULTOVERRIDE_H
