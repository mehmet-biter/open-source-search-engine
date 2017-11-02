#ifndef ROBOTSBLOCKEDRESULTOVERRIDE_H
#define ROBOTSBLOCKEDRESULTOVERRIDE_H

#include "LanguageResultOverride.h"
#include <atomic>


class RobotsBlockedResultOverride : public LanguageResultOverride {
public:
	RobotsBlockedResultOverride();

	bool init();

	static void reload(int /*fd*/, void *state);
	static void reload(void *state);

	std::string getTitle(const std::string &lang, const Url &url) const override;
	std::string getSummary(const std::string &lang, const Url &url) const override;

private:
	std::atomic_bool m_loading;
};

extern RobotsBlockedResultOverride g_robotsBlockedResultOverride;


#endif //ROBOTSBLOCKEDRESULTOVERRIDE_H
