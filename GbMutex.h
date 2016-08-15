#ifndef GB_MUTEX_H_
#define GB_MUTEX_H_

#include <pthread.h>

class GbMutex {
	GbMutex(const GbMutex&);
	GbMutex& operator=(const GbMutex&);
	static const pthread_mutex_t mtx_init;
public:
	pthread_mutex_t mtx;

	GbMutex() { mtx=mtx_init; }

#ifndef DEBUG_MUTEXES

	~GbMutex() {}

	void lock() {
		pthread_mutex_lock(&mtx);
	}

	void unlock() {
		pthread_mutex_unlock(&mtx);
	}

	void verify_is_locked() {}

#else

	~GbMutex();

	void lock();
	void unlock();
	void verify_is_locked();

#endif
};


#endif //GB_MUTEX_H_
