#ifndef ROBOTSBLOCKEDRESULTOVERRIDE_H
#define ROBOTSBLOCKEDRESULTOVERRIDE_H

#include "LanguageResultOverride.h"


class RobotsBlockedResultOverride : public LanguageResultOverride {
public:
	RobotsBlockedResultOverride();

	bool init();

	static void reload(int /*fd*/, void *state);

	std::string getTitle(const std::string &lang, const Url &url) const;
	std::string getSummary(const std::string &lang, const Url &url) const;
};

extern RobotsBlockedResultOverride g_robotsBlockedResultOverride;


#endif //ROBOTSBLOCKEDRESULTOVERRIDE_H
