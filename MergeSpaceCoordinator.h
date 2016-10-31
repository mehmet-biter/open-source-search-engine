#ifndef MERGESPACECOORDINATOR_H_
#define MERGESPACECOORDINATOR_H_

#include "GbMutex.h"
#include <inttypes.h>
#include <string>
#include <pthread.h>


class MergeSpaceCoordinator {
	MergeSpaceCoordinator(const MergeSpaceCoordinator&);
	MergeSpaceCoordinator& operator =(const MergeSpaceCoordinator&);

	pthread_t hold_lock_tid;
	bool hold_lock_thread_running;
	GbMutex mtx;
	pthread_cond_t cond;
	bool please_shutdown;
	int held_lock_number; //-1 = none
	std::string lock_dir;
	int min_lock_files;
	std::string merge_space_dir;
	
	friend void *hold_lock_thread_function_trampoline(void*);
	void hold_lock_thread_function();

public:
	MergeSpaceCoordinator(const char *lock_dir, int min_lock_files, const char *merge_space_dir);
	~MergeSpaceCoordinator();

	bool acquire(uint64_t how_much);
	bool acquired() const;
	void relinquish();
};

#endif //MERGESPACECOORDINATOR_H_
