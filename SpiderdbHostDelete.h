#ifndef SPIDERDBHOSTDELETE_H
#define SPIDERDBHOSTDELETE_H

namespace SpiderdbHostDelete {
	bool initialize();
	void finalize();

	void reload(int /*fd*/, void */*state*/);

	void processFile(void *item);
}

#endif //SPIDERDBHOSTDELETE_H
