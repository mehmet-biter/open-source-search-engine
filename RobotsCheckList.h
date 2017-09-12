#ifndef ROBOTSCHECKLIST_H
#define ROBOTSCHECKLIST_H

#include <memory>
#include <vector>
#include <string>

typedef std::vector<std::string> robotschecklist_t;
typedef std::shared_ptr<robotschecklist_t> robotschecklist_ptr_t;
typedef std::shared_ptr<const robotschecklist_t> robotschecklistconst_ptr_t;

class RobotsCheckList {
public:
	RobotsCheckList();

	bool init();

	bool isHostBlocked(const char *host);

	static void reload(int /*fd*/, void *state);

protected:
	bool load();

	const char *m_filename;

private:
	robotschecklistconst_ptr_t getRobotsCheckList();
	void swapRobotsCheckList(robotschecklistconst_ptr_t robotsCheckList);

	robotschecklistconst_ptr_t m_robotsCheckList;

	time_t m_lastModifiedTime;
};

extern RobotsCheckList g_robotsCheckList;

#endif //ROBOTSCHECKLIST_H
