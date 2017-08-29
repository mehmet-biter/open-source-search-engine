#ifndef DOCIDDELETE_H
#define DOCIDDELETE_H

#include <time.h>
#include <cstdint>

namespace DocDelete {
	bool initialize();
	void finalize();

	void reload(int /*fd*/, void */*state*/);

	void processFile(void *item);
	void processDoc(void *item);

	void processedDoc(void *state);
}


#endif //DOCIDDELETE_H
